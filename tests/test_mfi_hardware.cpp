#include <catch2/catch.hpp>
#include "mfi/sensor.h"
#include "mfi/led.h"
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

namespace {

// RAII helper for creating proc-like file trees used by mfi::sensor and mfi::led
// The non-MIPS paths are ./proc/power/ and ./proc/led/
class ProcFileTree {
public:
	ProcFileTree() {
		fs::create_directories("./proc/power");
		fs::create_directories("./proc/led");
	}

	~ProcFileTree() {
		fs::remove_all("./proc");
	}

	void writeSensorFile(const std::string& name, int id, const std::string& value) {
		std::ofstream out{"./proc/power/" + name + std::to_string(id)};
		out << value;
	}

	void writeLedFile(const std::string& name, const std::string& value) {
		std::ofstream out{"./proc/led/" + name};
		out << value;
	}
};

} // namespace

// ========== mfi::sensor construction ==========

TEST_CASE("sensor: constructor and id", "[mfi][sensor]") {
	mfi::sensor s{1};
	CHECK(s.id() == 1);
}

TEST_CASE("sensor: different ids", "[mfi][sensor]") {
	mfi::sensor s1{1};
	mfi::sensor s2{8};
	CHECK(s1.id() == 1);
	CHECK(s2.id() == 8);
}

// ========== mfi::sensor file reads ==========

TEST_CASE("sensor: power reads from file", "[mfi][sensor][file]") {
	ProcFileTree tree;
	tree.writeSensorFile("active_pwr", 1, "12.5");

	mfi::sensor s{1};
	CHECK(s.power() == Approx(12.5));
}

TEST_CASE("sensor: current reads from file", "[mfi][sensor][file]") {
	ProcFileTree tree;
	tree.writeSensorFile("i_rms", 2, "0.125");

	mfi::sensor s{2};
	CHECK(s.current() == Approx(0.125));
}

TEST_CASE("sensor: voltage reads from file", "[mfi][sensor][file]") {
	ProcFileTree tree;
	tree.writeSensorFile("v_rms", 1, "120.5");

	mfi::sensor s{1};
	CHECK(s.voltage() == Approx(120.5));
}

TEST_CASE("sensor: power_factor reads from file", "[mfi][sensor][file]") {
	ProcFileTree tree;
	tree.writeSensorFile("pf", 1, "0.98");

	mfi::sensor s{1};
	CHECK(s.power_factor() == Approx(0.98));
}

TEST_CASE("sensor: relay reads true when 1", "[mfi][sensor][file]") {
	ProcFileTree tree;
	tree.writeSensorFile("relay", 1, "1");

	mfi::sensor s{1};
	CHECK(s.relay() == true);
}

TEST_CASE("sensor: relay reads false when 0", "[mfi][sensor][file]") {
	ProcFileTree tree;
	tree.writeSensorFile("relay", 1, "0");

	mfi::sensor s{1};
	CHECK(s.relay() == false);
}

TEST_CASE("sensor: relay write sets value", "[mfi][sensor][file]") {
	ProcFileTree tree;
	tree.writeSensorFile("relay", 3, "0");

	mfi::sensor s{3};
	s.relay(true);

	std::ifstream in{"./proc/power/relay3"};
	int value;
	in >> value;
	CHECK(value == 1);
}

TEST_CASE("sensor: relay write false sets 0", "[mfi][sensor][file]") {
	ProcFileTree tree;
	tree.writeSensorFile("relay", 1, "1");

	mfi::sensor s{1};
	s.relay(false);

	std::ifstream in{"./proc/power/relay1"};
	int value;
	in >> value;
	CHECK(value == 0);
}

TEST_CASE("sensor: read returns 0 for missing file", "[mfi][sensor][file]") {
	ProcFileTree tree;
	// Don't create the file
	mfi::sensor s{99};
	CHECK(s.power() == 0.0);
}

TEST_CASE("sensor: multiple sensors read independently", "[mfi][sensor][file]") {
	ProcFileTree tree;
	tree.writeSensorFile("active_pwr", 1, "100.0");
	tree.writeSensorFile("active_pwr", 2, "200.0");

	mfi::sensor s1{1};
	mfi::sensor s2{2};
	CHECK(s1.power() == Approx(100.0));
	CHECK(s2.power() == Approx(200.0));
}

// ========== mfi::led ==========

TEST_CASE("led: color reads blue", "[mfi][led][file]") {
	ProcFileTree tree;
	tree.writeLedFile("status", "1 0");

	mfi::led l;
	CHECK(l.color() == mfi::led_color::blue);
}

TEST_CASE("led: color reads yellow", "[mfi][led][file]") {
	ProcFileTree tree;
	tree.writeLedFile("status", "2 0");

	mfi::led l;
	CHECK(l.color() == mfi::led_color::yellow);
}

TEST_CASE("led: color reads off", "[mfi][led][file]") {
	ProcFileTree tree;
	tree.writeLedFile("status", "0 0");

	mfi::led l;
	CHECK(l.color() == mfi::led_color::off);
}

TEST_CASE("led: color reads both", "[mfi][led][file]") {
	ProcFileTree tree;
	tree.writeLedFile("status", "3 0");

	mfi::led l;
	CHECK(l.color() == mfi::led_color::both);
}

TEST_CASE("led: color reads alternate", "[mfi][led][file]") {
	ProcFileTree tree;
	tree.writeLedFile("status", "4 0");

	mfi::led l;
	CHECK(l.color() == mfi::led_color::alternate);
}

TEST_CASE("led: color throws on unknown", "[mfi][led][file]") {
	ProcFileTree tree;
	tree.writeLedFile("status", "9 0");

	mfi::led l;
	CHECK_THROWS_AS(l.color(), std::runtime_error);
}

TEST_CASE("led: color setter writes value", "[mfi][led][file]") {
	ProcFileTree tree;
	tree.writeLedFile("status", "0 0");

	mfi::led l;
	l.color(mfi::led_color::blue);

	std::ifstream in{"./proc/led/status"};
	int value;
	in >> value;
	CHECK(value == 1);
}

TEST_CASE("led: frequency reads value", "[mfi][led][file]") {
	ProcFileTree tree;
	tree.writeLedFile("freq", "500");

	mfi::led l;
	CHECK(l.frequency() == 500);
}

TEST_CASE("led: frequency setter writes value", "[mfi][led][file]") {
	ProcFileTree tree;
	tree.writeLedFile("freq", "0");

	mfi::led l;
	l.frequency(1000);

	std::ifstream in{"./proc/led/freq"};
	uint32_t value;
	in >> value;
	CHECK(value == 1000);
}
