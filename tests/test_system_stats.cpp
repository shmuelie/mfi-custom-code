#include <catch2/catch_all.hpp>
#include "mfi_mqtt_client/system_stats.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>

using namespace mfi_mqtt_client;

namespace {
	class proc_tree final {
	public:
		proc_tree() {
			if (!::mkdtemp(_root.data())) {
				throw std::runtime_error("Cannot create system stats fixture");
			}
		}

		~proc_tree() {
			std::remove(path("stat").c_str());
			std::remove(path("meminfo").c_str());
			::rmdir(_root.c_str());
		}

		proc_tree(proc_tree const&) = delete;
		proc_tree& operator=(proc_tree const&) = delete;

		std::string const& root() const noexcept {
			return _root;
		}

		std::string path(std::string const& name) const {
			return _root + "/" + name;
		}

		void write(std::string const& name, std::string const& text) const {
			std::ofstream output;
			output.exceptions(std::ios::failbit | std::ios::badbit);
			output.open(path(name), std::ios::binary | std::ios::trunc);
			output << text;
			output.close();
		}

	private:
		std::string _root = "/tmp/mfi-system-stats-XXXXXX";
	};

	template<typename sample>
	void require_error(sample const& result, std::string_view diagnostic) {
		REQUIRE(std::holds_alternative<system_read_error>(result));
		auto const& message = std::get<system_read_error>(result).message;
		INFO(message);
		CHECK(message.find(diagnostic) != std::string::npos);
	}

	void require_cpu(cpu_sample const& result, double expected) {
		if (auto const error = std::get_if<system_read_error>(&result)) {
			INFO(error->message);
			FAIL("Expected CPU utilization");
		}
		REQUIRE(std::holds_alternative<double>(result));
		auto const value = std::get<double>(result);
		CHECK(std::isfinite(value));
		CHECK(value >= 0);
		CHECK(value <= 100);
		CHECK(value == Catch::Approx(expected));
	}

	memory_stats require_memory(memory_sample const& result) {
		if (auto const error = std::get_if<system_read_error>(&result)) {
			INFO(error->message);
			FAIL("Expected memory statistics");
		}
		REQUIRE(std::holds_alternative<memory_stats>(result));
		auto const value = std::get<memory_stats>(result);
		CHECK(std::isfinite(value.total_mib));
		CHECK(std::isfinite(value.available_mib));
		CHECK(std::isfinite(value.used_mib));
		CHECK(std::isfinite(value.utilization));
		CHECK(value.total_mib > 0);
		CHECK(value.available_mib >= 0);
		CHECK(value.available_mib <= value.total_mib);
		CHECK(value.used_mib >= 0);
		CHECK(value.used_mib <= value.total_mib);
		CHECK(value.utilization >= 0);
		CHECK(value.utilization <= 100);
		return value;
	}

	std::string cpu_line(std::array<std::string, 8> const& counters) {
		std::string line = "cpu";
		for (auto const& counter : counters) {
			line += " " + counter;
		}
		return line + "\n";
	}
}

TEST_CASE("System stats: CPU first sample and explicit reset need warmup", "[mqtt][system-stats]") {
	proc_tree tree;
	system_stats_reader reader(tree.root());
	tree.write("stat", "cpu 0 0 0 0\n");
	REQUIRE(std::holds_alternative<cpu_warmup>(reader.read_cpu()));
	tree.write("stat", "cpu 1 0 0 1\n");
	require_cpu(reader.read_cpu(), 50);
	reader.reset_cpu();
	reader.reset_cpu();
	REQUIRE(std::holds_alternative<cpu_warmup>(reader.read_cpu()));
	tree.write("stat", "cpu 2 0 0 1\n");
	require_cpu(reader.read_cpu(), 100);
}

TEST_CASE("System stats: CPU exact idle busy and mixed aggregate ratios", "[mqtt][system-stats]") {
	auto const [before, after, expected] =
		GENERATE(Catch::Generators::table<std::string, std::string, double>({
			{"cpu 1 2 3 4 5 6 7 8\n", "cpu 1 2 3 14 15 6 7 8\n", 0},
			{"cpu 1 2 3 4 5 6 7 8\n", "cpu 11 12 13 4 5 16 17 18\n", 100},
			{"cpu 0 0 0 0 0 0 0 0\n", "cpu 10 10 10 10 10 10 10 10\n", 75},
			{"cpu 100 0 100 200 0 0 0 0\ncpu0 0 0 0 0\ncpu1 0 0 0 0\n",
			 "cpu 130 0 110 260 0 0 0 0\ncpu0 100 0 0 0\ncpu1 malformed\n", 40},
			{"intr 42\ncpu0 999 0 0 0\n cpu\t1 0 0 3\r\n",
			 "cpu0 999 0 0 0\n\tcpu 2 0 0 6", 25}
		}));
	proc_tree tree;
	system_stats_reader reader(tree.root());
	tree.write("stat", before);
	REQUIRE(std::holds_alternative<cpu_warmup>(reader.read_cpu()));
	tree.write("stat", after);
	require_cpu(reader.read_cpu(), expected);
}

TEST_CASE("System stats: CPU guest counters are validated but never double counted", "[mqtt][system-stats]") {
	proc_tree tree;
	system_stats_reader reader(tree.root());
	tree.write("stat", "cpu 100 100 0 100 0 0 0 0 50 50\n");
	REQUIRE(std::holds_alternative<cpu_warmup>(reader.read_cpu()));
	tree.write("stat", "cpu 110 110 0 180 0 0 0 0 60 60\n");
	require_cpu(reader.read_cpu(), 20);
	tree.write("stat", "cpu 120 120 0 260 0 0 0 0 18446744073709551615 18446744073709551615\n");
	require_cpu(reader.read_cpu(), 20);
}

TEST_CASE("System stats: CPU absent trailing legacy fields default to zero", "[mqtt][system-stats]") {
	auto const count = GENERATE(4, 5, 6, 7, 8, 9, 10);
	std::string trailing;
	for (int index = 4; index < count; ++index) {
		trailing += " 0";
	}
	proc_tree tree;
	system_stats_reader reader(tree.root());
	tree.write("stat", "cpu 10 0 0 10" + trailing + "\n");
	REQUIRE(std::holds_alternative<cpu_warmup>(reader.read_cpu()));
	tree.write("stat", "cpu 20 0 0 20" + trailing + "\n");
	require_cpu(reader.read_cpu(), 50);
}

TEST_CASE("System stats: CPU 64-bit counters retain small deltas", "[mqtt][system-stats]") {
	auto const [before, after] = GENERATE(Catch::Generators::table<std::string, std::string>({
		{"cpu 4294967296 0 0 4294967296\n", "cpu 4294967297 0 0 4294967297\n"},
		{"cpu 18446744073709551500 0 0 0\n", "cpu 18446744073709551501 0 0 1\n"}
	}));
	proc_tree tree;
	system_stats_reader reader(tree.root());
	tree.write("stat", before);
	REQUIRE(std::holds_alternative<cpu_warmup>(reader.read_cpu()));
	tree.write("stat", after);
	require_cpu(reader.read_cpu(), 50);
}

TEST_CASE("System stats: CPU rejects malformed negative and overflowing counters", "[mqtt][system-stats]") {
	auto const invalid = GENERATE("-1", "+1", "x", "1x", "1.0", "0x10", "18446744073709551616");
	auto const position = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
	CAPTURE(invalid, position);
	proc_tree tree;
	system_stats_reader reader(tree.root());
	tree.write("stat", "cpu 1 0 0 1\n");
	REQUIRE(std::holds_alternative<cpu_warmup>(reader.read_cpu()));
	std::string line = "cpu";
	for (int index = 0; index < 11; ++index) {
		line += " ";
		line += index == position ? invalid : "1";
	}
	tree.write("stat", line + "\n");
	require_error(reader.read_cpu(), "unsigned");
	tree.write("stat", "cpu 2 0 0 2\n");
	REQUIRE(std::holds_alternative<cpu_warmup>(reader.read_cpu()));
	tree.write("stat", "cpu 3 0 0 3\n");
	require_cpu(reader.read_cpu(), 50);
}

TEST_CASE("System stats: CPU requires all four original counters", "[mqtt][system-stats]") {
	auto const text = GENERATE("cpu\n", "cpu 1\n", "cpu 1 2\n", "cpu 1 2 3\n");
	proc_tree tree;
	system_stats_reader reader(tree.root());
	tree.write("stat", text);
	require_error(reader.read_cpu(), "requires user, nice, system, and idle");
	tree.write("stat", "cpu 1 2 3 4\n");
	REQUIRE(std::holds_alternative<cpu_warmup>(reader.read_cpu()));
}

TEST_CASE("System stats: CPU rejects total and idle sum overflow", "[mqtt][system-stats]") {
	auto const text = GENERATE(
		"cpu 18446744073709551615 1 0 0\n",
		"cpu 0 0 0 18446744073709551615 1\n",
		"cpu 0 0 0 1 0 0 0 18446744073709551615\n");
	proc_tree tree;
	system_stats_reader reader(tree.root());
	tree.write("stat", text);
	require_error(reader.read_cpu(), "sum overflow");
	tree.write("stat", "cpu 0 0 0 18446744073709551615\n");
	REQUIRE(std::holds_alternative<cpu_warmup>(reader.read_cpu()));
}

TEST_CASE("System stats: CPU detects each counter regression even if total grows", "[mqtt][system-stats]") {
	auto const index = GENERATE(std::size_t{0}, std::size_t{1}, std::size_t{2}, std::size_t{3},
		std::size_t{4}, std::size_t{5}, std::size_t{6}, std::size_t{7});
	proc_tree tree;
	system_stats_reader reader(tree.root());
	tree.write("stat", "cpu 10 10 10 10 10 10 10 10\n");
	REQUIRE(std::holds_alternative<cpu_warmup>(reader.read_cpu()));
	std::array<std::string, 8> counters;
	counters.fill("30");
	counters[index] = "9";
	tree.write("stat", cpu_line(counters));
	require_error(reader.read_cpu(), "regressed");
	REQUIRE(std::holds_alternative<cpu_warmup>(reader.read_cpu()));
	counters.fill("40");
	tree.write("stat", cpu_line(counters));
	auto const expected = index == 3 || index == 4 ? 100.0 * 60 / 101 : 100.0 * 81 / 101;
	require_cpu(reader.read_cpu(), expected);
}

TEST_CASE("System stats: CPU zero elapsed ticks is an error and clears baseline", "[mqtt][system-stats]") {
	proc_tree tree;
	system_stats_reader reader(tree.root());
	tree.write("stat", "cpu 1 2 3 4 5 6 7 8 9 10\n");
	REQUIRE(std::holds_alternative<cpu_warmup>(reader.read_cpu()));
	tree.write("stat", "cpu 1 2 3 4 5 6 7 8 10 11\n");
	require_error(reader.read_cpu(), "zero elapsed ticks");
	REQUIRE(std::holds_alternative<cpu_warmup>(reader.read_cpu()));
	tree.write("stat", "cpu 2 2 3 5 5 6 7 8\n");
	require_cpu(reader.read_cpu(), 50);
}

TEST_CASE("System stats: CPU missing file or aggregate clears baseline", "[mqtt][system-stats]") {
	proc_tree tree;
	system_stats_reader reader(tree.root());
	tree.write("stat", "cpu 1 0 0 1\n");
	REQUIRE(std::holds_alternative<cpu_warmup>(reader.read_cpu()));
	SECTION("missing file") {
		REQUIRE(std::remove(tree.path("stat").c_str()) == 0);
		require_error(reader.read_cpu(), "/stat: cannot open");
	}
	SECTION("empty file") {
		tree.write("stat", "");
		require_error(reader.read_cpu(), "missing aggregate cpu");
	}
	SECTION("per-core counters are not a substitute") {
		tree.write("stat", "cpu0 1 2 3 4\ncpu1 1 2 3 4\ncpus 1 2 3 4\n");
		require_error(reader.read_cpu(), "missing aggregate cpu");
	}
	tree.write("stat", "cpu 2 0 0 2\n");
	REQUIRE(std::holds_alternative<cpu_warmup>(reader.read_cpu()));
}

TEST_CASE("System stats: modern memory prefers MemAvailable and ignores unknown fields", "[mqtt][system-stats]") {
	proc_tree tree;
	system_stats_reader const reader(tree.root() + "/");
	tree.write("meminfo", "Future: -999 nonsense\nHugePages_Total: 2\n"
		"\tMemAvailable:\t2048 kB\r\nCached: 8192 kB\nMemFree: 8192 kB\n"
		"Buffers: 8192 kB\nMemTotal:8192 kB");
	auto const result = require_memory(reader.read_memory());
	CHECK(result.total_mib == 8);
	CHECK(result.available_mib == 2);
	CHECK(result.used_mib == 6);
	CHECK(result.utilization == 75);
	CHECK_FALSE(result.estimated);
}

TEST_CASE("System stats: modern memory needs no legacy fields and converts KiB to MiB", "[mqtt][system-stats]") {
	proc_tree tree;
	system_stats_reader reader(tree.root());
	tree.write("meminfo", "MemTotal: 1536 kB\nMemAvailable: 512 kB\n");
	auto const result = require_memory(reader.read_memory());
	CHECK(result.total_mib == 1.5);
	CHECK(result.available_mib == 0.5);
	CHECK(result.used_mib == 1);
	CHECK(result.utilization == Catch::Approx(100.0 * 2 / 3));
	CHECK_FALSE(result.estimated);
}

TEST_CASE("System stats: modern memory permits zero and full availability", "[mqtt][system-stats]") {
	auto const available = GENERATE(0, 1024);
	proc_tree tree;
	system_stats_reader reader(tree.root());
	tree.write("meminfo", "MemTotal: 1024 kB\nMemAvailable: " + std::to_string(available) + " kB\n");
	auto const result = require_memory(reader.read_memory());
	CHECK(result.available_mib == available / 1024.0);
	CHECK(result.used_mib == (1024 - available) / 1024.0);
	CHECK(result.utilization == (available == 0 ? 100 : 0));
	CHECK_FALSE(result.estimated);
}

TEST_CASE("System stats: memory uses checked 64-bit integers before floating conversion", "[mqtt][system-stats]") {
	proc_tree tree;
	system_stats_reader reader(tree.root());
	tree.write("meminfo", "MemTotal: 18446744073709551615 kB\nMemAvailable: 18446744073709551614 kB\n");
	auto const result = require_memory(reader.read_memory());
	CHECK(result.total_mib == Catch::Approx(18446744073709551615.0 / 1024));
	CHECK(result.used_mib == 1.0 / 1024);
	CHECK(result.utilization > 0);
	CHECK(result.utilization == Catch::Approx(100.0 / 18446744073709551615.0));
}

TEST_CASE("System stats: legacy memory estimates with individually optional fields", "[mqtt][system-stats]") {
	auto const reclaimable = GENERATE(false, true);
	auto const shared = GENERATE(false, true);
	proc_tree tree;
	system_stats_reader reader(tree.root());
	std::string text = "MemTotal: 8192 kB\nMemFree: 1024 kB\nBuffers: 512 kB\nCached: 2048 kB\n";
	if (reclaimable) {
		text += "SReclaimable: 768 kB\n";
	}
	if (shared) {
		text += "Shmem: 256 kB\n";
	}
	tree.write("meminfo", text);
	auto const result = require_memory(reader.read_memory());
	auto const available = 3584 + (reclaimable ? 768 : 0) - (shared ? 256 : 0);
	CHECK(result.total_mib == 8);
	CHECK(result.available_mib == available / 1024.0);
	CHECK(result.used_mib == (8192 - available) / 1024.0);
	CHECK(result.utilization == Catch::Approx(100.0 * (8192 - available) / 8192));
	CHECK(result.estimated);
}

TEST_CASE("System stats: legacy availability clamps only after shared memory subtraction", "[mqtt][system-stats]") {
	auto const [fields, expected] = GENERATE(Catch::Generators::table<std::string, double>({
		{"MemFree: 1024 kB\nBuffers: 512 kB\nCached: 512 kB\n", 1024},
		{"MemFree: 1 kB\nBuffers: 2 kB\nCached: 3 kB\nShmem: 18446744073709551615 kB\n", 0},
		{"MemFree: 1 kB\nBuffers: 2 kB\nCached: 3 kB\nShmem: 6 kB\n", 0},
		{"MemFree: 1024 kB\nBuffers: 512 kB\nCached: 64 kB\nShmem: 1000 kB\n", 600},
		{"MemFree: 18446744073709551615 kB\nBuffers: 0 kB\nCached: 0 kB\n", 1024},
		{"MemFree: 0 kB\nBuffers: 0 kB\nCached: 0 kB\n", 0}
	}));
	proc_tree tree;
	system_stats_reader reader(tree.root());
	tree.write("meminfo", "MemTotal: 1024 kB\n" + fields);
	auto const result = require_memory(reader.read_memory());
	CHECK(result.available_mib == expected / 1024);
	CHECK(result.used_mib == (1024 - expected) / 1024);
	CHECK(result.utilization == Catch::Approx(100 * (1024 - expected) / 1024));
	CHECK(result.estimated);
}

TEST_CASE("System stats: memory rejects missing required fields zero total and invalid bounds", "[mqtt][system-stats]") {
	auto const [text, diagnostic] = GENERATE(Catch::Generators::table<std::string, std::string>({
		{"", "missing MemTotal"},
		{"MemAvailable: 1 kB\n", "missing MemTotal"},
		{"MemTotal: 0 kB\nMemAvailable: 0 kB\n", "MemTotal must be greater than zero"},
		{"MemTotal: 1 kB\nMemAvailable: 2 kB\n", "MemAvailable exceeds MemTotal"},
		{"MemTotal: 1 kB\nBuffers: 0 kB\nCached: 0 kB\n", "requires MemFree"},
		{"MemTotal: 1 kB\nMemFree: 0 kB\nCached: 0 kB\n", "requires Buffers"},
		{"MemTotal: 1 kB\nMemFree: 0 kB\nBuffers: 0 kB\n", "requires Cached"}
	}));
	proc_tree tree;
	system_stats_reader reader(tree.root());
	tree.write("meminfo", text);
	require_error(reader.read_memory(), diagnostic);
	tree.write("meminfo", "MemTotal: 1024 kB\nMemAvailable: 512 kB\n");
	CHECK(require_memory(reader.read_memory()).available_mib == 0.5);
}

TEST_CASE("System stats: every present known memory field requires strict numbers and kB units", "[mqtt][system-stats]") {
	auto const field = GENERATE("MemTotal", "MemAvailable", "MemFree", "Buffers", "Cached", "SReclaimable", "Shmem");
	auto const invalid = GENERATE("", "-1 kB", "+1 kB", "1x kB", "1.5 kB", "0x10 kB",
		"18446744073709551616 kB", "1", "1 KiB", "1 KB", "1 bytes", "1 kB extra", "kB");
	CAPTURE(field, invalid);
	std::string text;
	for (std::string_view name : {"MemTotal", "MemAvailable", "MemFree", "Buffers", "Cached", "SReclaimable", "Shmem"}) {
		text += std::string(name) + ": ";
		text += name == field ? invalid : (name == "MemTotal" ? "1024 kB" : "0 kB");
		text += "\n";
	}
	proc_tree tree;
	system_stats_reader reader(tree.root());
	tree.write("meminfo", text);
	require_error(reader.read_memory(), field);
}

TEST_CASE("System stats: memory does not substitute legacy estimate for invalid MemAvailable", "[mqtt][system-stats]") {
	auto const invalid = GENERATE("-1 kB", "1025 kB", "x kB", "1 MB");
	proc_tree tree;
	system_stats_reader reader(tree.root());
	tree.write("meminfo", std::string("MemAvailable: ") + invalid +
		"\nMemTotal: 1024 kB\nMemFree: 256 kB\nBuffers: 0 kB\nCached: 0 kB\n");
	require_error(reader.read_memory(), "MemAvailable");
}

TEST_CASE("System stats: legacy memory never masks malformed optional fields or overflowing sums", "[mqtt][system-stats]") {
	auto const [fields, diagnostic] = GENERATE(Catch::Generators::table<std::string, std::string>({
		{"MemFree: 0 kB\nBuffers: 0 kB\nCached: 0 kB\nSReclaimable: -1 kB\n", "SReclaimable"},
		{"MemFree: 0 kB\nBuffers: 0 kB\nCached: 0 kB\nShmem: nope kB\n", "Shmem"},
		{"MemFree: 18446744073709551615 kB\nBuffers: 1 kB\nCached: 0 kB\n", "overflow adding Buffers"},
		{"MemFree: 18446744073709551615 kB\nBuffers: 0 kB\nCached: 1 kB\n", "overflow adding Cached"},
		{"MemFree: 18446744073709551615 kB\nBuffers: 0 kB\nCached: 0 kB\nSReclaimable: 1 kB\nShmem: 1 kB\n",
		 "overflow adding SReclaimable"}
	}));
	proc_tree tree;
	system_stats_reader reader(tree.root());
	tree.write("meminfo", "MemTotal: 1024 kB\n" + fields);
	require_error(reader.read_memory(), diagnostic);
}

TEST_CASE("System stats: memory rejects duplicate fields and missing separators", "[mqtt][system-stats]") {
	auto const field = GENERATE("MemTotal", "MemAvailable", "MemFree", "Buffers", "Cached", "SReclaimable", "Shmem");
	proc_tree tree;
	system_stats_reader reader(tree.root());
	SECTION("duplicate") {
		tree.write("meminfo", std::string(field) + ": 0 kB\n" + field + ": 0 kB\n");
		require_error(reader.read_memory(), "duplicate field");
	}
	SECTION("missing colon") {
		tree.write("meminfo", std::string(field) + " 1 kB\n");
		require_error(reader.read_memory(), "missing ':' separator");
	}
}

TEST_CASE("System stats: embedded NUL is not accepted as a numeric terminator", "[mqtt][system-stats]") {
	proc_tree tree;
	system_stats_reader reader(tree.root());
	tree.write("stat", std::string("cpu 1") + '\0' + "garbage 2 3 4\n");
	require_error(reader.read_cpu(), "cpu user");
	tree.write("meminfo", std::string("MemTotal: 1") + '\0' + "garbage kB\nMemAvailable: 0 kB\n");
	require_error(reader.read_memory(), "MemTotal");
}

TEST_CASE("System stats: bounded line reads accept the limit and reject one extra byte", "[mqtt][system-stats]") {
	auto const name = GENERATE("stat", "meminfo");
	proc_tree tree;
	system_stats_reader reader(tree.root());
	auto const suffix = std::string_view(name) == "stat" ? "\ncpu 1 2 3 4\n" :
		"\nMemTotal: 1024 kB\nMemAvailable: 512 kB\n";
	tree.write(name, std::string(4096, 'x') + suffix);
	if (std::string_view(name) == "stat") {
		REQUIRE(std::holds_alternative<cpu_warmup>(reader.read_cpu()));
	}
	else {
		require_memory(reader.read_memory());
	}
	tree.write(name, std::string(4097, 'x') + suffix);
	if (std::string_view(name) == "stat") {
		require_error(reader.read_cpu(), "line exceeds 4096 bytes");
		tree.write(name, "cpu 2 3 4 5\n");
		REQUIRE(std::holds_alternative<cpu_warmup>(reader.read_cpu()));
	}
	else {
		require_error(reader.read_memory(), "line exceeds 4096 bytes");
	}
}

TEST_CASE("System stats: bounded total reads accept the limit and reject one extra byte", "[mqtt][system-stats]") {
	auto const name = GENERATE("stat", "meminfo");
	proc_tree tree;
	system_stats_reader reader(tree.root());
	std::string const suffix = std::string_view(name) == "stat" ? "cpu 1 2 3 4\n" :
		"MemTotal: 1024 kB\nMemAvailable: 512 kB\n";
	std::string text(256 * 1024 - suffix.size(), '\n');
	text += suffix;
	tree.write(name, text);
	if (std::string_view(name) == "stat") {
		REQUIRE(std::holds_alternative<cpu_warmup>(reader.read_cpu()));
	}
	else {
		require_memory(reader.read_memory());
	}
	tree.write(name, "\n" + text);
	if (std::string_view(name) == "stat") {
		require_error(reader.read_cpu(), "input exceeds 262144 bytes");
		tree.write(name, "cpu 2 3 4 5\n");
		REQUIRE(std::holds_alternative<cpu_warmup>(reader.read_cpu()));
	}
	else {
		require_error(reader.read_memory(), "input exceeds 262144 bytes");
	}
}

TEST_CASE("System stats: CPU does not read unrelated data after aggregate", "[mqtt][system-stats]") {
	proc_tree tree;
	system_stats_reader reader(tree.root());
	tree.write("stat", "cpu 1 0 0 1\nintr " + std::string(8192, '9') + "\n");
	REQUIRE(std::holds_alternative<cpu_warmup>(reader.read_cpu()));
	tree.write("stat", "cpu 2 0 0 2\nintr " + std::string(8192, '9') + "\n");
	require_cpu(reader.read_cpu(), 50);
}

TEST_CASE("System stats: CPU and memory failures are independent", "[mqtt][system-stats]") {
	proc_tree tree;
	system_stats_reader reader(tree.root());
	tree.write("stat", "cpu 1 0 0 1\n");
	REQUIRE(std::holds_alternative<cpu_warmup>(reader.read_cpu()));
	require_error(reader.read_memory(), "/meminfo: cannot open");
	tree.write("meminfo", "MemTotal: invalid kB\n");
	require_error(reader.read_memory(), "MemTotal");
	tree.write("stat", "cpu 2 0 0 2\n");
	require_cpu(reader.read_cpu(), 50);

	tree.write("meminfo", "MemTotal: 1024 kB\nMemAvailable: 256 kB\n");
	tree.write("stat", "cpu invalid\n");
	require_error(reader.read_cpu(), "cpu user");
	CHECK(require_memory(reader.read_memory()).available_mib == 0.25);
	reader.reset_cpu();
	CHECK(require_memory(reader.read_memory()).available_mib == 0.25);
	tree.write("stat", "cpu 3 0 0 3\n");
	REQUIRE(std::holds_alternative<cpu_warmup>(reader.read_cpu()));
}

TEST_CASE("System stats: injected roots and CPU baselines are isolated", "[mqtt][system-stats]") {
	proc_tree first;
	proc_tree second;
	system_stats_reader first_reader(first.root());
	system_stats_reader second_reader(second.root());
	first.write("stat", "cpu 1 0 0 1\n");
	second.write("stat", "cpu 100 0 0 100\n");
	REQUIRE(std::holds_alternative<cpu_warmup>(first_reader.read_cpu()));
	REQUIRE(std::holds_alternative<cpu_warmup>(second_reader.read_cpu()));
	first.write("stat", "cpu 2 0 0 1\n");
	second.write("stat", "cpu 100 0 0 101\n");
	require_cpu(first_reader.read_cpu(), 100);
	require_cpu(second_reader.read_cpu(), 0);
	first.write("meminfo", "MemTotal: 1024 kB\nMemAvailable: 0 kB\n");
	second.write("meminfo", "MemTotal: 2048 kB\nMemAvailable: 2048 kB\n");
	CHECK(require_memory(first_reader.read_memory()).utilization == 100);
	CHECK(require_memory(second_reader.read_memory()).utilization == 0);
}

TEST_CASE("System stats: nonexistent roots return explicit open errors", "[mqtt][system-stats]") {
	proc_tree tree;
	system_stats_reader reader(tree.path("missing"));
	require_error(reader.read_cpu(), "/missing/stat: cannot open");
	require_error(reader.read_memory(), "/missing/meminfo: cannot open");
}

TEST_CASE("System stats: underlying read errors are distinct from missing data", "[mqtt][system-stats]") {
	proc_tree tree;
	system_stats_reader reader(tree.root());
	tree.write("stat", "cpu 1 2 3 4\n");
	REQUIRE(std::holds_alternative<cpu_warmup>(reader.read_cpu()));
	REQUIRE(std::remove(tree.path("stat").c_str()) == 0);
	REQUIRE(::mkdir(tree.path("stat").c_str(), 0700) == 0);
	REQUIRE(::mkdir(tree.path("meminfo").c_str(), 0700) == 0);
	require_error(reader.read_cpu(), "/stat: read failed");
	require_error(reader.read_memory(), "/meminfo: read failed");
	REQUIRE(::rmdir(tree.path("stat").c_str()) == 0);
	tree.write("stat", "cpu 2 3 4 5\n");
	REQUIRE(std::holds_alternative<cpu_warmup>(reader.read_cpu()));
}
