#include <catch2/catch.hpp>
#include "hass_mqtt_device/core/helper_functions.hpp"

TEST_CASE("getValidHassString: basic lowercase conversion", "[helper_functions]") {
	CHECK(getValidHassString("Hello") == "hello");
	CHECK(getValidHassString("UPPER") == "upper");
}

TEST_CASE("getValidHassString: spaces become underscores", "[helper_functions]") {
	CHECK(getValidHassString("Port 1 Power") == "port_1_power");
}

TEST_CASE("getValidHassString: special characters removed", "[helper_functions]") {
	CHECK(getValidHassString("test!@#value") == "testvalue");
	CHECK(getValidHassString("a(b)c") == "abc");
}

TEST_CASE("getValidHassString: empty string returns 'empty'", "[helper_functions]") {
	CHECK(getValidHassString("") == "empty");
	CHECK(getValidHassString("!@#$%") == "empty");
}

TEST_CASE("getValidHassString: already clean string unchanged", "[helper_functions]") {
	CHECK(getValidHassString("simple") == "simple");
	CHECK(getValidHassString("already_clean") == "already_clean");
}

TEST_CASE("getValidHassString: mixed content", "[helper_functions]") {
	CHECK(getValidHassString("Port 1 Relay") == "port_1_relay");
	CHECK(getValidHassString("Kitchen Power (W)") == "kitchen_power_w");
}

TEST_CASE("getValidHassString: digits preserved", "[helper_functions]") {
	CHECK(getValidHassString("sensor123") == "sensor123");
	CHECK(getValidHassString("Port 8") == "port_8");
}
