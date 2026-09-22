#include "mfi_mqtt_client/identity.h"
#include <CLI/CLI.hpp>
#include <algorithm>
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace mfi_mqtt_client {
namespace {
	class file_descriptor {
	public:
		explicit file_descriptor(int value) : value(value) {
			if (value < 0) {
				throw std::system_error(errno, std::generic_category(), "Cannot open identity/configuration file");
			}
		}
		~file_descriptor() { ::close(value); }
		file_descriptor(file_descriptor const&) = delete;
		file_descriptor& operator=(file_descriptor const&) = delete;
		int value;
	};

	void check(int result, char const* operation) {
		if (result < 0) {
			throw std::system_error(errno, std::generic_category(), operation);
		}
	}

	std::string read_config(int fd) {
		struct stat info{};
		check(::fstat(fd, &info), "Cannot inspect configuration");
		if (!S_ISREG(info.st_mode) || info.st_size > 65536) {
			throw std::runtime_error("Configuration must be a regular file of at most 64 KiB");
		}
		std::string text;
		std::array<char, 4096> buffer{};
		for (;;) {
			auto count = ::read(fd, buffer.data(), buffer.size());
			if (count < 0 && errno == EINTR) {
				continue;
			}
			check(static_cast<int>(count), "Cannot read configuration");
			if (count == 0) {
				break;
			}
			text.append(buffer.data(), static_cast<std::size_t>(count));
			if (text.size() > 65536) {
				throw std::runtime_error("Configuration exceeds 64 KiB");
			}
		}
		return text;
	}

	std::optional<std::string> parse_id(std::string const& text) {
		std::istringstream input(text);
		std::vector<CLI::ConfigItem> items;
		try {
			items = CLI::ConfigTOML{}.from_config(input);
		}
		catch (CLI::ParseError const&) {
			throw std::runtime_error("Cannot parse configuration; repair it before provisioning");
		}
		std::optional<std::string> id;
		for (auto const& item : items) {
			if (item.name != "device_id" && item.name != "device-id") {
				continue;
			}
			if (!item.parents.empty() || id || item.inputs.size() != 1 || !valid_uuid(item.inputs[0])) {
				throw std::runtime_error("Invalid or duplicate device_id; expected one top-level 32-character lowercase hex UUID");
			}
			id = item.inputs[0];
		}
		return id;
	}

	class temporary_file {
	public:
		explicit temporary_file(std::string path) : path(std::move(path)), fd(::mkstemp(this->path.data())) {}
		~temporary_file() { ::unlink(path.c_str()); }
		std::string path;
		file_descriptor fd;
	};
}

bool valid_uuid(std::string_view value) noexcept {
	return value.size() == 32 && std::all_of(value.begin(), value.end(), [](char ch) {
		return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
	});
}

std::string random_uuid() {
	file_descriptor entropy(::open("/dev/urandom", O_RDONLY | O_CLOEXEC));
	std::array<unsigned char, 16> bytes{};
	std::size_t offset = 0;
	while (offset < bytes.size()) {
		auto count = ::read(entropy.value, bytes.data() + offset, bytes.size() - offset);
		if (count < 0 && errno == EINTR) {
			continue;
		}
		check(static_cast<int>(count), "Cannot read OS entropy");
		if (count == 0) {
			throw std::runtime_error("OS entropy source ended unexpectedly");
		}
		offset += static_cast<std::size_t>(count);
	}
	bytes[6] = (bytes[6] & 0x0f) | 0x40;
	bytes[8] = (bytes[8] & 0x3f) | 0x80;
	constexpr char hex[] = "0123456789abcdef";
	std::string result;
	for (auto byte : bytes) {
		result += hex[byte >> 4];
		result += hex[byte & 0x0f];
	}
	return result;
}

std::optional<std::string> configured_device_id(std::string const& path) {
	file_descriptor file(::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
	return parse_id(read_config(file.value));
}

std::string select_device_id(std::optional<std::string> const& configured, std::string const& requested) {
	if (!requested.empty() && !valid_uuid(requested)) {
		throw std::runtime_error("--device-id must be a 32-character lowercase hex UUID");
	}
	if (!configured || !valid_uuid(*configured)) {
		throw std::runtime_error("A persisted device_id is required; run --initialize-device-id /absolute/config and then use --config");
	}
	if (!requested.empty() && *configured != requested) {
		throw std::runtime_error("--device-id conflicts with the persisted device_id; do not replace identity during startup");
	}
	return *configured;
}

std::string initialize_device_id(std::string const& path) {
	if (path.empty() || path.front() != '/' || path.back() == '/') {
		throw std::runtime_error("--initialize-device-id requires an absolute configuration file path");
	}
	auto parent = path.substr(0, path.find_last_of('/') + 1);
	file_descriptor directory(::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
	check(::flock(directory.value, LOCK_EX | LOCK_NB), "Another identity provisioning operation is active");
	struct stat before{};
	bool exists = ::lstat(path.c_str(), &before) == 0;
	if (!exists && errno != ENOENT) {
		check(-1, "Cannot inspect configuration");
	}
	std::string original;
	if (exists) {
		if (!S_ISREG(before.st_mode) || before.st_nlink != 1) {
			throw std::runtime_error("Refusing to replace a symlink, hard-linked or non-regular configuration");
		}
		file_descriptor file(::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
		original = read_config(file.value);
		if (auto id = parse_id(original)) {
			return *id;
		}
	}
	auto id = random_uuid();
	// Prepending keeps the new setting at top level without rewriting secrets or sections.
	auto updated = "device_id = \"" + id + "\"\n" + original;
	if (updated.size() > 65536) {
		throw std::runtime_error("Provisioned configuration would exceed 64 KiB");
	}
	temporary_file staging(parent + ".mfi-device-id-XXXXXX");
	if (exists) {
		struct stat staged{};
		check(::fstat(staging.fd.value, &staged), "Cannot inspect temporary configuration");
		if (staged.st_uid != before.st_uid || staged.st_gid != before.st_gid) {
			check(::fchown(staging.fd.value, before.st_uid, before.st_gid), "Cannot preserve configuration ownership");
		}
		check(::fchmod(staging.fd.value, before.st_mode & 0777), "Cannot preserve configuration permissions");
	}
	std::size_t offset = 0;
	while (offset < updated.size()) {
		auto count = ::write(staging.fd.value, updated.data() + offset, updated.size() - offset);
		if (count < 0 && errno == EINTR) {
			continue;
		}
		check(static_cast<int>(count), "Cannot persist device_id");
		if (count == 0) {
			throw std::runtime_error("Incomplete device_id write");
		}
		offset += static_cast<std::size_t>(count);
	}
	check(::fsync(staging.fd.value), "Cannot sync provisioned configuration");
	if (exists) {
		struct stat current{};
		check(::lstat(path.c_str(), &current), "Configuration disappeared during provisioning");
		file_descriptor file(::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
		if (current.st_dev != before.st_dev || current.st_ino != before.st_ino
			|| current.st_mode != before.st_mode || current.st_uid != before.st_uid
			|| current.st_gid != before.st_gid || current.st_nlink != 1 || read_config(file.value) != original) {
			throw std::runtime_error("Configuration changed during provisioning; retry after stopping its editor");
		}
		check(::rename(staging.path.c_str(), path.c_str()), "Cannot atomically install device_id");
	}
	else {
		check(::link(staging.path.c_str(), path.c_str()), "Cannot install device_id without overwriting configuration");
		check(::unlink(staging.path.c_str()), "Cannot remove temporary configuration link");
	}
	check(::fsync(directory.value), "Cannot sync configuration directory; verify persistence before using native mode");
	return id;
}
}
