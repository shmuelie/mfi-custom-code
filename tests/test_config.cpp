#include <catch2/catch_all.hpp>
#include "mfi/config.h"
#include <fstream>
#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;

class TempConfigFile {
public:
	explicit TempConfigFile(const std::string& content) {
		_path = fs::temp_directory_path() / ("mfi_test_config_" + std::to_string(reinterpret_cast<uintptr_t>(this)));
		std::ofstream out{_path};
		out << content;
	}
	~TempConfigFile() {
		fs::remove(_path);
	}
	std::string path() const { return _path.string(); }
private:
	fs::path _path;
};

// --- read_all ---

TEST_CASE("config::read_all: parses key=value pairs", "[config][read_all]") {
	TempConfigFile file("key1=value1\nkey2=value2\nkey3=value3\n");
	auto result = mfi::config::read_all(file.path());
	REQUIRE(result.size() == 3);
	CHECK(result.at("key1") == "value1");
	CHECK(result.at("key2") == "value2");
	CHECK(result.at("key3") == "value3");
}

TEST_CASE("config::read_all: skips lines without equals", "[config][read_all]") {
	TempConfigFile file("good=yes\nbadline\nalso_good=ok\n");
	auto result = mfi::config::read_all(file.path());
	REQUIRE(result.size() == 2);
	CHECK(result.at("good") == "yes");
	CHECK(result.at("also_good") == "ok");
}

TEST_CASE("config::read_all: handles value with equals sign", "[config][read_all]") {
	TempConfigFile file("key=val=ue\n");
	auto result = mfi::config::read_all(file.path());
	REQUIRE(result.size() == 1);
	CHECK(result.at("key") == "val=ue");
}

TEST_CASE("config::read_all: empty file returns empty map", "[config][read_all]") {
	TempConfigFile file("");
	auto result = mfi::config::read_all(file.path());
	CHECK(result.empty());
}

TEST_CASE("config::read_all: empty value", "[config][read_all]") {
	TempConfigFile file("key=\n");
	auto result = mfi::config::read_all(file.path());
	REQUIRE(result.size() == 1);
	CHECK(result.at("key") == "");
}

// --- read (two-arg, throws on missing) ---

TEST_CASE("config::read: returns value for existing key", "[config][read]") {
	TempConfigFile file("port.0.label=Kitchen\nport.1.label=Bedroom\n");
	auto result = mfi::config::read(file.path(), "port.0.label");
	CHECK(result == "Kitchen");
}

TEST_CASE("config::read: returns empty for missing key", "[config][read]") {
	TempConfigFile file("port.0.label=Kitchen\n");
	auto result = mfi::config::read(file.path(), "nonexistent");
	CHECK(result == "");
}

// --- read (three-arg, with default) ---

TEST_CASE("config::read: returns value when key exists", "[config][read_default]") {
	TempConfigFile file("port.0.sensorId=sensor1\n");
	auto result = mfi::config::read(file.path(), "port.0.sensorId", "default");
	CHECK(result == "sensor1");
}

TEST_CASE("config::read: returns default when key missing", "[config][read_default]") {
	TempConfigFile file("port.0.sensorId=sensor1\n");
	auto result = mfi::config::read(file.path(), "missing.key", "fallback");
	CHECK(result == "fallback");
}

TEST_CASE("config::read: returns default for nonexistent file", "[config][read_default]") {
	auto result = mfi::config::read("/nonexistent/path/file", "key", "default_val");
	CHECK(result == "default_val");
}

// --- regression: nonexistent file detection (was using .bad() instead of .is_open()) ---

TEST_CASE("config::read_all: throws on nonexistent file", "[config][read_all]") {
	CHECK_THROWS_AS(mfi::config::read_all("/nonexistent/path/file"), std::runtime_error);
}

TEST_CASE("config::read: throws on nonexistent file (2-arg)", "[config][read]") {
	CHECK_THROWS_AS(mfi::config::read("/nonexistent/path/file", "key"), std::runtime_error);
}
