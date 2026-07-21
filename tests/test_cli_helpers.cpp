#include <catch2/catch_all.hpp>
#include "mfi_cli/helpers.h"
#include "mfi/board.h"
#include "mfi/led.h"
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

namespace {

// RAII helper that lays down the ./etc + ./proc tree that mfi::board reads on
// non-MIPS builds (board.info, version, and per-sensor power files).
class BoardFileTree {
public:
	explicit BoardFileTree(const std::string& sysid) {
		fs::create_directories("./etc");
		fs::create_directories("./proc/power");
		std::ofstream info{"./etc/board.info"};
		info << "board.name=Test Board\n"
			 << "board.shortname=testboard\n"
			 << "board.sysid=" << sysid << "\n";
		info.close();
		std::ofstream ver{"./etc/version"};
		ver << "MF.test\n";
	}

	~BoardFileTree() {
		fs::remove_all("./etc");
		fs::remove_all("./proc");
	}
};

} // namespace

// ========== find_sensor ==========

TEST_CASE("cli: find_sensor returns matching sensor", "[cli][helpers]") {
	BoardFileTree tree{"0xe648"}; // 8 sensors
	mfi::board b;

	auto s = mfi_cli::find_sensor(b, 3);
	REQUIRE(s.has_value());
	CHECK(s->id() == 3);
}

TEST_CASE("cli: find_sensor returns nullopt for unknown id", "[cli][helpers]") {
	BoardFileTree tree{"0xe648"}; // 8 sensors
	mfi::board b;

	CHECK_FALSE(mfi_cli::find_sensor(b, 99).has_value());
}

TEST_CASE("cli: find_sensor returns nullopt on single-port board", "[cli][helpers]") {
	BoardFileTree tree{"0xe671"}; // 1 sensor
	mfi::board b;

	CHECK(mfi_cli::find_sensor(b, 1).has_value());
	CHECK_FALSE(mfi_cli::find_sensor(b, 2).has_value());
}

// ========== led_color parse/format ==========

TEST_CASE("cli: parse_led_color valid values", "[cli][helpers]") {
	CHECK(mfi_cli::parse_led_color("off") == mfi::led_color::off);
	CHECK(mfi_cli::parse_led_color("blue") == mfi::led_color::blue);
	CHECK(mfi_cli::parse_led_color("yellow") == mfi::led_color::yellow);
	CHECK(mfi_cli::parse_led_color("both") == mfi::led_color::both);
	CHECK(mfi_cli::parse_led_color("alternate") == mfi::led_color::alternate);
}

TEST_CASE("cli: parse_led_color rejects invalid value", "[cli][helpers]") {
	CHECK_FALSE(mfi_cli::parse_led_color("purple").has_value());
	CHECK_FALSE(mfi_cli::parse_led_color("").has_value());
	CHECK_FALSE(mfi_cli::parse_led_color("Blue").has_value()); // case-sensitive
}

TEST_CASE("cli: format_led_color round-trips with parse", "[cli][helpers]") {
	for (auto color : {mfi::led_color::off, mfi::led_color::blue, mfi::led_color::yellow,
					   mfi::led_color::both, mfi::led_color::alternate}) {
		auto text = mfi_cli::format_led_color(color);
		auto parsed = mfi_cli::parse_led_color(text);
		REQUIRE(parsed.has_value());
		CHECK(*parsed == color);
	}
}
