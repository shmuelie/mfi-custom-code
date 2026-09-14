#include "mfi_mqtt_client/system_stats.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <string_view>
#include <utility>

namespace {
	using mfi_mqtt_client::system_read_error;

	constexpr std::size_t max_line_bytes = 4096;
	constexpr std::size_t max_read_bytes = 256 * 1024;
	constexpr std::string_view whitespace = " \t\r\n\v\f";
	constexpr std::array<std::string_view, 10> cpu_fields{
		"user", "nice", "system", "idle", "iowait",
		"irq", "softirq", "steal", "guest", "guest_nice"
	};
	constexpr std::array<std::string_view, 7> memory_fields{
		"MemTotal", "MemAvailable", "MemFree", "Buffers", "Cached", "SReclaimable", "Shmem"
	};

	struct end_of_file {};
	using line_sample = std::variant<std::string, end_of_file, system_read_error>;

	class proc_file final {
	public:
		explicit proc_file(std::string path) :
			_path(std::move(path)),
			_file(std::fopen(_path.c_str(), "r"), &std::fclose) {
			if (!_file) {
				_open_error = errno;
			}
		}

		system_read_error error(std::string const& message) const {
			return {_path + ": " + message};
		}

		line_sample read_line() {
			if (!_file) {
				return error("cannot open: " + std::string(std::strerror(_open_error)));
			}

			std::string line;
			errno = 0;
			for (;;) {
				auto const character = std::fgetc(_file.get());
				if (character == EOF) {
					if (std::ferror(_file.get())) {
						auto const read_error = errno;
						return error(read_error == 0 ? "read failed" :
							"read failed: " + std::string(std::strerror(read_error)));
					}
					if (line.empty()) {
						return end_of_file{};
					}
					return line;
				}
				if (++_bytes_read > max_read_bytes) {
					return error("input exceeds " + std::to_string(max_read_bytes) + " bytes");
				}
				if (character == '\n') {
					return line;
				}
				if (line.size() == max_line_bytes) {
					return error("line exceeds " + std::to_string(max_line_bytes) + " bytes");
				}
				line.push_back(static_cast<char>(character));
			}
		}

	private:
		std::string _path;
		std::unique_ptr<std::FILE, decltype(&std::fclose)> _file;
		int _open_error = 0;
		std::size_t _bytes_read = 0;
	};

	void skip_whitespace(std::string_view& text) noexcept {
		auto const first = text.find_first_not_of(whitespace);
		text.remove_prefix(first == std::string_view::npos ? text.size() : first);
	}

	std::string_view next_token(std::string_view& text) noexcept {
		skip_whitespace(text);
		auto const end = text.find_first_of(whitespace);
		auto const token = text.substr(0, end);
		text.remove_prefix(token.size());
		return token;
	}

	std::variant<std::uint64_t, system_read_error> parse_unsigned(
		std::string_view token, std::string const& field) {
		if (token.empty()) {
			return system_read_error{field + ": missing unsigned decimal integer"};
		}
		std::uint64_t value = 0;
		for (auto const character : token) {
			if (character < '0' || character > '9') {
				return system_read_error{field + ": expected unsigned decimal integer"};
			}
			auto const digit = static_cast<std::uint64_t>(character - '0');
			if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) {
				return system_read_error{field + ": unsigned 64-bit integer overflow"};
			}
			value = value * 10 + digit;
		}
		return value;
	}

	bool checked_add(std::uint64_t left, std::uint64_t right, std::uint64_t& result) noexcept {
		if (right > std::numeric_limits<std::uint64_t>::max() - left) {
			return false;
		}
		result = left + right;
		return true;
	}
}

namespace mfi_mqtt_client {
	system_stats_reader::system_stats_reader(std::string proc_root) :
		_proc_root(std::move(proc_root)) {
	}

	void system_stats_reader::reset_cpu() noexcept {
		_cpu_baseline.reset();
	}

	cpu_sample system_stats_reader::read_cpu() {
		auto const previous = _cpu_baseline;
		reset_cpu();
		proc_file file(_proc_root + "/stat");

		for (;;) {
			auto line = file.read_line();
			if (auto const error = std::get_if<system_read_error>(&line)) {
				return *error;
			}
			if (std::holds_alternative<end_of_file>(line)) {
				return file.error("missing aggregate cpu line");
			}
			std::string_view text = std::get<std::string>(line);
			if (next_token(text) != "cpu") {
				continue;
			}

			cpu_baseline current;
			std::size_t count = 0;
			for (auto token = next_token(text); !token.empty(); token = next_token(text)) {
				auto const field = count < cpu_fields.size() ? std::string(cpu_fields[count]) :
					"counter " + std::to_string(count + 1);
				auto parsed = parse_unsigned(token, "cpu " + field);
				if (auto const error = std::get_if<system_read_error>(&parsed)) {
					return file.error(error->message);
				}
				// Guest time is already included in user/nice; validate but do not add it.
				if (count < current.counters.size()) {
					current.counters[count] = std::get<std::uint64_t>(parsed);
				}
				++count;
			}
			if (count < 4) {
				return file.error("aggregate cpu requires user, nice, system, and idle counters");
			}
			for (std::size_t index = 0; index < current.counters.size(); ++index) {
				if (!checked_add(current.total, current.counters[index], current.total)) {
					return file.error("cpu total counter sum overflow");
				}
				if (previous && current.counters[index] < previous->counters[index]) {
					return file.error("cpu " + std::string(cpu_fields[index]) + " counter regressed");
				}
			}
			// Both terms are included in the checked total, so their sum cannot overflow.
			current.idle = current.counters[3] + current.counters[4];
			if (!previous) {
				_cpu_baseline = current;
				return cpu_warmup{};
			}

			auto const delta_total = current.total - previous->total;
			auto const delta_idle = current.idle - previous->idle;
			if (delta_total == 0) {
				return file.error("cpu counters have zero elapsed ticks");
			}
			auto const utilization = 100.0 * (static_cast<double>(delta_total - delta_idle) /
				static_cast<double>(delta_total));
			_cpu_baseline = current;
			return utilization;
		}
	}

	memory_sample system_stats_reader::read_memory() const {
		proc_file file(_proc_root + "/meminfo");
		std::array<std::optional<std::uint64_t>, memory_fields.size()> values{};

		for (;;) {
			auto line = file.read_line();
			if (auto const error = std::get_if<system_read_error>(&line)) {
				return *error;
			}
			if (std::holds_alternative<end_of_file>(line)) {
				break;
			}
			std::string_view text = std::get<std::string>(line);
			skip_whitespace(text);
			auto const name = text.substr(0, text.find_first_of(": \t\r\n\v\f"));
			auto const field = std::find(memory_fields.begin(), memory_fields.end(), name);
			if (field == memory_fields.end()) {
				continue;
			}
			auto const index = static_cast<std::size_t>(field - memory_fields.begin());
			if (values[index]) {
				return file.error(std::string(name) + ": duplicate field");
			}
			text.remove_prefix(name.size());
			skip_whitespace(text);
			if (text.empty() || text.front() != ':') {
				return file.error(std::string(name) + ": missing ':' separator");
			}
			text.remove_prefix(1);
			auto parsed = parse_unsigned(next_token(text), std::string(name));
			if (auto const error = std::get_if<system_read_error>(&parsed)) {
				return file.error(error->message);
			}
			if (next_token(text) != "kB") {
				return file.error(std::string(name) + ": expected kB unit");
			}
			if (!next_token(text).empty()) {
				return file.error(std::string(name) + ": unexpected trailing data");
			}
			values[index] = std::get<std::uint64_t>(parsed);
		}

		auto const& total = values[0];
		if (!total) {
			return file.error("missing MemTotal");
		}
		if (*total == 0) {
			return file.error("MemTotal must be greater than zero");
		}

		std::uint64_t available = 0;
		auto const estimated = !values[1].has_value();
		if (!estimated) {
			available = *values[1];
			if (available > *total) {
				return file.error("MemAvailable exceeds MemTotal");
			}
		}
		else {
			for (std::size_t index = 2; index <= 5; ++index) {
				if (!values[index] && index <= 4) {
					return file.error("legacy available estimate requires " + std::string(memory_fields[index]));
				}
				if (!checked_add(available, values[index].value_or(0), available)) {
					return file.error("legacy available estimate overflow adding " + std::string(memory_fields[index]));
				}
			}
			auto const shared = values[6].value_or(0);
			available = available > shared ? available - shared : 0;
			available = std::min(available, *total);
		}

		auto const used = *total - available;
		return memory_stats{
			static_cast<double>(*total) / 1024.0,
			static_cast<double>(available) / 1024.0,
			static_cast<double>(used) / 1024.0,
			100.0 * (static_cast<double>(used) / static_cast<double>(*total)),
			estimated
		};
	}
}
