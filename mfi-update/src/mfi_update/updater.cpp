#include "mfi_update/updater.h"
#include "mfi_update/downloader.h"

#include <array>
#include <cstdio>
#include <fstream>
#include <memory>
#include <sys/stat.h>
#include <unistd.h>

namespace mfi_update {

	updater::updater(std::string tool_name, semver current, config cfg)
		: _tool_name(std::move(tool_name)), _current(current), _config(std::move(cfg)) {
	}

	std::string updater::api_matching_refs_url() const {
		return "https://api.github.com/repos/" + _config.repo_owner + "/" + _config.repo_name +
			"/git/matching-refs/tags/" + _tool_name + "/";
	}

	std::string updater::api_release_url(std::string const& tag) const {
		return "https://api.github.com/repos/" + _config.repo_owner + "/" + _config.repo_name +
			"/releases/tags/" + tag;
	}

	std::optional<release_asset> updater::check() const {
		fetch_fn fetch = _fetch;
		if (!fetch) {
			auto kind = downloader::detect();
			if (!kind) {
				return std::nullopt;
			}
			downloader dl{ *kind, _config };
			fetch = [dl](std::string const& url) { return dl.fetch_to_string(url); };
		}

		auto refs = fetch(api_matching_refs_url());
		if (!refs) {
			return std::nullopt;
		}
		auto latest = pick_latest_tag(*refs, _tool_name);
		if (!latest) {
			return std::nullopt;
		}

		auto release = fetch(api_release_url(latest->second));
		if (!release) {
			return std::nullopt;
		}
		auto url = pick_asset_url(*release, _tool_name);
		if (!url) {
			return std::nullopt;
		}
		return release_asset{ latest->first, *url };
	}

	update_result updater::check_and_apply(std::vector<std::string> const& argv) const {
		if (!_config.enabled) {
			return update_result::disabled;
		}

		// Resolve a downloader once so both fetch and download share it.
		std::optional<downloader> dl;
		if (!_fetch || !_download) {
			auto kind = downloader::detect();
			if (!kind) {
				return update_result::no_downloader;
			}
			dl.emplace(*kind, _config);
		}

		fetch_fn fetch = _fetch;
		if (!fetch) {
			fetch = [dl](std::string const& url) { return dl->fetch_to_string(url); };
		}
		download_fn download = _download;
		if (!download) {
			download = [dl](std::string const& url, std::string const& path) {
				return dl->fetch_to_file(url, path);
			};
		}

		auto refs = fetch(api_matching_refs_url());
		if (!refs) {
			return update_result::check_failed;
		}
		auto latest = pick_latest_tag(*refs, _tool_name);
		if (!latest) {
			return update_result::check_failed;
		}
		if (latest->first <= _current) {
			return update_result::up_to_date;
		}

		auto release = fetch(api_release_url(latest->second));
		if (!release) {
			return update_result::check_failed;
		}
		auto url = pick_asset_url(*release, _tool_name);
		if (!url) {
			return update_result::check_failed;
		}

		std::string target = self_path(_config.bin_dir + "/" + _tool_name);
		std::string tmp = target + ".new";

		if (!download(*url, tmp)) {
			std::remove(tmp.c_str());
			return update_result::download_failed;
		}
		if (!is_valid_elf(tmp)) {
			std::remove(tmp.c_str());
			return update_result::download_failed;
		}
		::chmod(tmp.c_str(), 0755);

		apply_fn apply = _apply;
		if (!apply) {
			apply = [target](std::string const& new_path, std::vector<std::string> const& args) {
				return replace_and_reexec(new_path, target, args);
			};
		}
		if (!apply(tmp, argv)) {
			std::remove(tmp.c_str());
			return update_result::download_failed;
		}
		// If apply returned true without re-exec (e.g. in tests), report updated.
		return update_result::updated;
	}

	bool is_valid_elf(std::string const& path) noexcept {
		std::ifstream in{ path, std::ios::binary };
		if (!in) {
			return false;
		}
		std::array<char, 4> magic{};
		in.read(magic.data(), magic.size());
		if (in.gcount() != static_cast<std::streamsize>(magic.size())) {
			return false;
		}
		return magic[0] == '\x7f' && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F';
	}

	std::string self_path(std::string const& fallback) {
		std::array<char, 4096> buf{};
		ssize_t n = ::readlink("/proc/self/exe", buf.data(), buf.size() - 1);
		if (n > 0) {
			buf[static_cast<size_t>(n)] = '\0';
			return std::string{ buf.data() };
		}
		return fallback;
	}

	bool replace_and_reexec(std::string const& new_path, std::string const& target_path,
		std::vector<std::string> const& argv) noexcept {
		// rename() within the same directory is atomic and works even though the
		// target is the running executable (the live process keeps its inode).
		if (std::rename(new_path.c_str(), target_path.c_str()) != 0) {
			return false;
		}

		std::vector<char*> c_argv;
		c_argv.reserve(argv.size() + 1);
		for (auto const& a : argv) {
			c_argv.push_back(const_cast<char*>(a.c_str()));
		}
		c_argv.push_back(nullptr);

		::execv(target_path.c_str(), c_argv.data());
		// execv only returns on failure.
		return false;
	}

	std::string describe(update_result result) noexcept {
		switch (result) {
		case update_result::disabled:        return "updates disabled";
		case update_result::no_downloader:   return "no downloader (wget/curl) found";
		case update_result::up_to_date:      return "up to date";
		case update_result::check_failed:    return "update check failed";
		case update_result::download_failed: return "update download failed";
		case update_result::updated:         return "updated";
		}
		return "unknown";
	}
}
