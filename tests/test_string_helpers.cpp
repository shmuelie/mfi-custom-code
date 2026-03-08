#include <catch2/catch.hpp>
#include "shmuelie/string_helpers.h"

// --- split ---

TEST_CASE("split: basic delimiter split", "[string_helpers][split]") {
	auto result = shmuelie::split("a,b,c", ',');
	REQUIRE(result.size() == 3);
	CHECK(result[0] == "a");
	CHECK(result[1] == "b");
	CHECK(result[2] == "c");
}

TEST_CASE("split: no delimiter present returns whole string", "[string_helpers][split]") {
	auto result = shmuelie::split("hello", ',');
	REQUIRE(result.size() == 1);
	CHECK(result[0] == "hello");
}

TEST_CASE("split: empty string returns single empty element", "[string_helpers][split]") {
	auto result = shmuelie::split("", ',');
	REQUIRE(result.size() == 1);
	CHECK(result[0] == "");
}

TEST_CASE("split: leading delimiter", "[string_helpers][split]") {
	auto result = shmuelie::split(",a,b", ',');
	REQUIRE(result.size() == 3);
	CHECK(result[0] == "");
	CHECK(result[1] == "a");
	CHECK(result[2] == "b");
}

TEST_CASE("split: trailing delimiter", "[string_helpers][split]") {
	auto result = shmuelie::split("a,b,", ',');
	REQUIRE(result.size() == 3);
	CHECK(result[0] == "a");
	CHECK(result[1] == "b");
	CHECK(result[2] == "");
}

TEST_CASE("split: single character string", "[string_helpers][split]") {
	auto result = shmuelie::split("x", ',');
	REQUIRE(result.size() == 1);
	CHECK(result[0] == "x");
}

TEST_CASE("split: long string preserves content after last delimiter", "[string_helpers][split]") {
	auto result = shmuelie::split("first,second,third_longer_segment", ',');
	REQUIRE(result.size() == 3);
	CHECK(result[0] == "first");
	CHECK(result[1] == "second");
	CHECK(result[2] == "third_longer_segment");
}

TEST_CASE("split: consecutive delimiters", "[string_helpers][split]") {
	auto result = shmuelie::split("a,,b", ',');
	REQUIRE(result.size() == 3);
	CHECK(result[0] == "a");
	CHECK(result[1] == "");
	CHECK(result[2] == "b");
}

// --- join ---

TEST_CASE("join: basic join with default separator", "[string_helpers][join]") {
	std::vector<std::string> v = {"a", "b", "c"};
	auto result = shmuelie::join(v.cbegin(), v.cend());
	CHECK(result == "a, b, c");
}

TEST_CASE("join: custom separator", "[string_helpers][join]") {
	std::vector<std::string> v = {"x", "y"};
	auto result = shmuelie::join(v.cbegin(), v.cend(), " - ");
	CHECK(result == "x - y");
}

TEST_CASE("join: with concluder", "[string_helpers][join]") {
	std::vector<std::string> v = {"a", "b"};
	auto result = shmuelie::join(v.cbegin(), v.cend(), ", ", "!");
	CHECK(result == "a, b!");
}

TEST_CASE("join: single element", "[string_helpers][join]") {
	std::vector<std::string> v = {"only"};
	auto result = shmuelie::join(v.cbegin(), v.cend(), ", ");
	CHECK(result == "only");
}

TEST_CASE("join: empty range returns concluder", "[string_helpers][join]") {
	std::vector<std::string> v = {};
	auto result = shmuelie::join(v.cbegin(), v.cend(), ", ", "end");
	CHECK(result == "end");
}

// --- is_number ---

TEST_CASE("is_number: valid numbers", "[string_helpers][is_number]") {
	CHECK(shmuelie::is_number("0"));
	CHECK(shmuelie::is_number("123"));
	CHECK(shmuelie::is_number("9999999"));
}

TEST_CASE("is_number: empty string is not a number", "[string_helpers][is_number]") {
	CHECK_FALSE(shmuelie::is_number(""));
}

TEST_CASE("is_number: strings with non-digits", "[string_helpers][is_number]") {
	CHECK_FALSE(shmuelie::is_number("12a3"));
	CHECK_FALSE(shmuelie::is_number("abc"));
	CHECK_FALSE(shmuelie::is_number(" 123"));
	CHECK_FALSE(shmuelie::is_number("-1"));
}

TEST_CASE("is_number: brackets are not digits", "[string_helpers][is_number]") {
	CHECK_FALSE(shmuelie::is_number("[123]"));
	CHECK_FALSE(shmuelie::is_number("[]"));
	CHECK_FALSE(shmuelie::is_number("["));
	CHECK_FALSE(shmuelie::is_number("]"));
}

// --- try_stoul ---

TEST_CASE("try_stoul: valid conversion", "[string_helpers][try_stoul]") {
	auto result = shmuelie::try_stoul<uint16_t>("42");
	REQUIRE(result.has_value());
	CHECK(result.value() == 42);
}

TEST_CASE("try_stoul: hex conversion", "[string_helpers][try_stoul]") {
	auto result = shmuelie::try_stoul<uint16_t>("e648", nullptr, 16);
	REQUIRE(result.has_value());
	CHECK(result.value() == 0xe648);
}

TEST_CASE("try_stoul: invalid string returns nullopt", "[string_helpers][try_stoul]") {
	auto result = shmuelie::try_stoul<uint32_t>("not_a_number");
	CHECK_FALSE(result.has_value());
}

TEST_CASE("try_stoul: empty string returns nullopt", "[string_helpers][try_stoul]") {
	auto result = shmuelie::try_stoul<uint32_t>("");
	CHECK_FALSE(result.has_value());
}

// --- try_stoi ---

TEST_CASE("try_stoi: valid conversion", "[string_helpers][try_stoi]") {
	auto result = shmuelie::try_stoi<int>("123");
	REQUIRE(result.has_value());
	CHECK(result.value() == 123);
}

TEST_CASE("try_stoi: negative number", "[string_helpers][try_stoi]") {
	auto result = shmuelie::try_stoi<int>("-5");
	REQUIRE(result.has_value());
	CHECK(result.value() == -5);
}

TEST_CASE("try_stoi: invalid string returns nullopt", "[string_helpers][try_stoi]") {
	auto result = shmuelie::try_stoi<int>("xyz");
	CHECK_FALSE(result.has_value());
}
