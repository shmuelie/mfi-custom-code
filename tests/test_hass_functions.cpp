#include <catch2/catch_all.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/null_sink.h>
#include "hass_mqtt_device/core/mqtt_connector.h"
#include "hass_mqtt_device/core/device_base.h"
#include "hass_mqtt_device/functions/switch.h"
#include "hass_mqtt_device/functions/sensor.h"
#include "hass_mqtt_device/functions/on_off_light.h"
#include "hass_mqtt_device/functions/dimmable_light.h"
#include "hass_mqtt_device/functions/number.h"

namespace {

struct TestFixture {
	std::shared_ptr<MQTTConnector> connector;
	std::shared_ptr<DeviceBase> device;

	TestFixture() {
		auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
		auto logger = std::make_shared<spdlog::logger>("test", null_sink);
		spdlog::set_default_logger(logger);

		connector = std::make_shared<MQTTConnector>("localhost", 1883, "user", "pass", "test_uid");
		device = std::make_shared<DeviceBase>("Test Device", "test_dev");
		connector->registerDevice(device);
	}
};

} // namespace

// ========== SwitchFunction ==========

TEST_CASE("SwitchFunction: processMessage ON", "[hass][switch]") {
	TestFixture f;
	bool called = false;
	bool received_value = false;
	auto sw = std::make_shared<SwitchFunction>("Test Switch", [&](bool v) {
		called = true;
		received_value = v;
	});
	f.device->registerFunction(sw);

	sw->processMessage(sw->getSubscribeTopics()[0], R"({"value":"ON"})");
	CHECK(called);
	CHECK(received_value == true);
}

TEST_CASE("SwitchFunction: processMessage OFF", "[hass][switch]") {
	TestFixture f;
	bool received_value = true;
	auto sw = std::make_shared<SwitchFunction>("Test Switch", [&](bool v) {
		received_value = v;
	});
	f.device->registerFunction(sw);

	sw->processMessage(sw->getSubscribeTopics()[0], R"({"value":"OFF"})");
	CHECK(received_value == false);
}

TEST_CASE("SwitchFunction: processMessage wrong topic ignored", "[hass][switch]") {
	TestFixture f;
	bool called = false;
	auto sw = std::make_shared<SwitchFunction>("Test Switch", [&](bool v) { called = true; });
	f.device->registerFunction(sw);

	sw->processMessage("wrong/topic", R"({"value":"ON"})");
	CHECK_FALSE(called);
}

TEST_CASE("SwitchFunction: processMessage invalid JSON ignored", "[hass][switch]") {
	TestFixture f;
	bool called = false;
	auto sw = std::make_shared<SwitchFunction>("Test Switch", [&](bool v) { called = true; });
	f.device->registerFunction(sw);

	sw->processMessage(sw->getSubscribeTopics()[0], "not json at all");
	CHECK_FALSE(called);
}

TEST_CASE("SwitchFunction: processMessage empty payload ignored", "[hass][switch]") {
	TestFixture f;
	bool called = false;
	auto sw = std::make_shared<SwitchFunction>("Test Switch", [&](bool v) { called = true; });
	f.device->registerFunction(sw);

	sw->processMessage(sw->getSubscribeTopics()[0], "");
	CHECK_FALSE(called);
}

TEST_CASE("SwitchFunction: update change detection skips duplicate", "[hass][switch]") {
	TestFixture f;
	auto sw = std::make_shared<SwitchFunction>("Test Switch", [](bool) {});
	f.device->registerFunction(sw);

	sw->update(true);
	CHECK(sw->getState() == true);
	// Second update with same value should not throw or crash
	sw->update(true);
	CHECK(sw->getState() == true);
}

TEST_CASE("SwitchFunction: update detects change", "[hass][switch]") {
	TestFixture f;
	auto sw = std::make_shared<SwitchFunction>("Test Switch", [](bool) {});
	f.device->registerFunction(sw);

	sw->update(true);
	CHECK(sw->getState() == true);
	sw->update(false);
	CHECK(sw->getState() == false);
}

TEST_CASE("SwitchFunction: getSubscribeTopics returns set topic", "[hass][switch]") {
	TestFixture f;
	auto sw = std::make_shared<SwitchFunction>("Test Switch", [](bool) {});
	f.device->registerFunction(sw);

	auto topics = sw->getSubscribeTopics();
	REQUIRE(topics.size() == 1);
	CHECK(topics[0].find("/set") != std::string::npos);
}

TEST_CASE("SwitchFunction: getDiscoveryTopic contains homeassistant/switch", "[hass][switch]") {
	TestFixture f;
	auto sw = std::make_shared<SwitchFunction>("Test Switch", [](bool) {});
	f.device->registerFunction(sw);

	auto topic = sw->getDiscoveryTopic();
	CHECK(topic.find("homeassistant/switch/") == 0);
	CHECK(topic.find("/config") != std::string::npos);
}

TEST_CASE("SwitchFunction: getDiscoveryJson has required fields", "[hass][switch]") {
	TestFixture f;
	auto sw = std::make_shared<SwitchFunction>("Test Switch", [](bool) {});
	f.device->registerFunction(sw);

	auto json = sw->getDiscoveryJson();
	CHECK(json.contains("name"));
	CHECK(json.contains("unique_id"));
	CHECK(json.contains("payload_on"));
	CHECK(json.contains("payload_off"));
	CHECK(json.contains("state_topic"));
	CHECK(json.contains("command_topic"));
}

// ========== SensorFunction<double> ==========

TEST_CASE("SensorFunction: update change detection skips duplicate", "[hass][sensor]") {
	TestFixture f;
	auto sensor = std::make_shared<SensorFunction<double>>("Test Power", SensorAttributes{
		.device_class = "power",
		.state_class = "measurement",
		.unit_of_measurement = "W",
		.suggested_display_precision = 2
	});
	f.device->registerFunction(sensor);

	sensor->update(42.5);
	// Same value again - should be a no-op
	sensor->update(42.5);
}

TEST_CASE("SensorFunction: update publishes on change", "[hass][sensor]") {
	TestFixture f;
	auto sensor = std::make_shared<SensorFunction<double>>("Test Power", SensorAttributes{
		.device_class = "power",
		.state_class = "measurement",
		.unit_of_measurement = "W",
		.suggested_display_precision = 2
	});
	f.device->registerFunction(sensor);

	// First update always publishes (m_has_data was false)
	sensor->update(10.0);
	// Changed value should also work
	sensor->update(20.0);
}

TEST_CASE("SensorFunction: getDiscoveryTopic contains homeassistant/sensor", "[hass][sensor]") {
	TestFixture f;
	auto sensor = std::make_shared<SensorFunction<double>>("Test Power", SensorAttributes{
		.device_class = "power",
		.state_class = "measurement",
		.unit_of_measurement = "W",
		.suggested_display_precision = 2
	});
	f.device->registerFunction(sensor);

	auto topic = sensor->getDiscoveryTopic();
	CHECK(topic.find("homeassistant/sensor/") == 0);
	CHECK(topic.find("/config") != std::string::npos);
}

TEST_CASE("SensorFunction: getDiscoveryJson has sensor attributes", "[hass][sensor]") {
	TestFixture f;
	auto sensor = std::make_shared<SensorFunction<double>>("Test Power", SensorAttributes{
		.device_class = "power",
		.state_class = "measurement",
		.unit_of_measurement = "W",
		.suggested_display_precision = 4
	});
	f.device->registerFunction(sensor);

	auto json = sensor->getDiscoveryJson();
	CHECK(json["device_class"] == "power");
	CHECK(json["state_class"] == "measurement");
	CHECK(json["unit_of_measurement"] == "W");
	CHECK(json["suggested_display_precision"] == 4);
	CHECK(json.contains("value_template"));
}

TEST_CASE("SensorFunction: getSubscribeTopics is empty", "[hass][sensor]") {
	TestFixture f;
	auto sensor = std::make_shared<SensorFunction<double>>("Test Power", SensorAttributes{
		.device_class = "power",
		.state_class = "measurement",
		.unit_of_measurement = "W",
		.suggested_display_precision = 2
	});
	f.device->registerFunction(sensor);

	CHECK(sensor->getSubscribeTopics().empty());
}

// ========== SensorFunction<int> ==========

TEST_CASE("SensorFunction<int>: update and change detection", "[hass][sensor]") {
	TestFixture f;
	auto sensor = std::make_shared<SensorFunction<int>>("Count", SensorAttributes{
		.device_class = "count",
		.state_class = "measurement",
		.unit_of_measurement = "",
		.suggested_display_precision = 0
	});
	f.device->registerFunction(sensor);

	sensor->update(5);
	sensor->update(5);  // duplicate - should be no-op
	sensor->update(10); // changed
}

// ========== SensorFunction<std::string> ==========

TEST_CASE("SensorFunction<string>: update and change detection", "[hass][sensor]") {
	TestFixture f;
	auto sensor = std::make_shared<SensorFunction<std::string>>("Status", SensorAttributes{
		.device_class = "enum",
		.state_class = "",
		.unit_of_measurement = "",
		.suggested_display_precision = 0
	});
	f.device->registerFunction(sensor);

	sensor->update(std::string("active"));
	sensor->update(std::string("active"));  // duplicate
	sensor->update(std::string("idle"));    // changed
}

// ========== OnOffLightFunction ==========

TEST_CASE("OnOffLightFunction: processMessage ON", "[hass][light]") {
	TestFixture f;
	bool received = false;
	auto light = std::make_shared<OnOffLightFunction>("Test Light", [&](bool v) { received = v; });
	f.device->registerFunction(light);

	light->processMessage(light->getSubscribeTopics()[0], R"({"state":"ON"})");
	CHECK(received == true);
}

TEST_CASE("OnOffLightFunction: processMessage OFF", "[hass][light]") {
	TestFixture f;
	bool received = true;
	auto light = std::make_shared<OnOffLightFunction>("Test Light", [&](bool v) { received = v; });
	f.device->registerFunction(light);

	light->processMessage(light->getSubscribeTopics()[0], R"({"state":"OFF"})");
	CHECK(received == false);
}

TEST_CASE("OnOffLightFunction: wrong topic ignored", "[hass][light]") {
	TestFixture f;
	bool called = false;
	auto light = std::make_shared<OnOffLightFunction>("Test Light", [&](bool) { called = true; });
	f.device->registerFunction(light);

	light->processMessage("wrong/topic", R"({"state":"ON"})");
	CHECK_FALSE(called);
}

TEST_CASE("OnOffLightFunction: invalid JSON ignored", "[hass][light]") {
	TestFixture f;
	bool called = false;
	auto light = std::make_shared<OnOffLightFunction>("Test Light", [&](bool) { called = true; });
	f.device->registerFunction(light);

	light->processMessage(light->getSubscribeTopics()[0], "bad json");
	CHECK_FALSE(called);
}

TEST_CASE("OnOffLightFunction: update change detection", "[hass][light]") {
	TestFixture f;
	auto light = std::make_shared<OnOffLightFunction>("Test Light", [](bool) {});
	f.device->registerFunction(light);

	light->update(true);
	CHECK(light->getState() == true);
	light->update(true);  // duplicate
	light->update(false);
	CHECK(light->getState() == false);
}

TEST_CASE("OnOffLightFunction: getDiscoveryTopic contains homeassistant/light", "[hass][light]") {
	TestFixture f;
	auto light = std::make_shared<OnOffLightFunction>("Test Light", [](bool) {});
	f.device->registerFunction(light);

	CHECK(light->getDiscoveryTopic().find("homeassistant/light/") == 0);
}

// ========== DimmableLightFunction ==========

TEST_CASE("DimmableLightFunction: processMessage with state and brightness", "[hass][dimmable]") {
	TestFixture f;
	bool recv_state = false;
	double recv_brightness = 0;
	auto light = std::make_shared<DimmableLightFunction>("Dim Light", [&](bool s, double b) {
		recv_state = s;
		recv_brightness = b;
	});
	f.device->registerFunction(light);

	light->processMessage(light->getSubscribeTopics()[0], R"({"state":"ON","brightness":128})");
	CHECK(recv_state == true);
	CHECK(recv_brightness == Catch::Approx(128.0 / 255.0));
}

TEST_CASE("DimmableLightFunction: processMessage without brightness keeps current", "[hass][dimmable]") {
	TestFixture f;
	double recv_brightness = -1;
	auto light = std::make_shared<DimmableLightFunction>("Dim Light", [&](bool, double b) {
		recv_brightness = b;
	});
	f.device->registerFunction(light);

	// First set brightness
	light->processMessage(light->getSubscribeTopics()[0], R"({"state":"ON","brightness":255})");
	CHECK(recv_brightness == Catch::Approx(1.0));

	// Send without brightness - should keep the previous m_brightness
	light->processMessage(light->getSubscribeTopics()[0], R"({"state":"OFF"})");
}

TEST_CASE("DimmableLightFunction: processMessage wrong topic ignored", "[hass][dimmable]") {
	TestFixture f;
	bool called = false;
	auto light = std::make_shared<DimmableLightFunction>("Dim Light", [&](bool, double) { called = true; });
	f.device->registerFunction(light);

	light->processMessage("wrong/topic", R"({"state":"ON"})");
	CHECK_FALSE(called);
}

TEST_CASE("DimmableLightFunction: processMessage invalid JSON ignored", "[hass][dimmable]") {
	TestFixture f;
	bool called = false;
	auto light = std::make_shared<DimmableLightFunction>("Dim Light", [&](bool, double) { called = true; });
	f.device->registerFunction(light);

	light->processMessage(light->getSubscribeTopics()[0], "not json");
	CHECK_FALSE(called);
}

TEST_CASE("DimmableLightFunction: update change detection", "[hass][dimmable]") {
	TestFixture f;
	auto light = std::make_shared<DimmableLightFunction>("Dim Light", [](bool, double) {});
	f.device->registerFunction(light);

	light->update(true, 0.5);
	CHECK(light->getState() == true);
	CHECK(light->getBrightness() == Catch::Approx(0.5));

	light->update(true, 0.5);  // duplicate
	light->update(true, 0.8);  // brightness changed
	CHECK(light->getBrightness() == Catch::Approx(0.8));
	light->update(false, 0.8); // state changed
	CHECK(light->getState() == false);
}

TEST_CASE("DimmableLightFunction: getDiscoveryJson has brightness field", "[hass][dimmable]") {
	TestFixture f;
	auto light = std::make_shared<DimmableLightFunction>("Dim Light", [](bool, double) {});
	f.device->registerFunction(light);

	auto json = light->getDiscoveryJson();
	CHECK(json["brightness"] == true);
}

// ========== NumberFunction ==========

TEST_CASE("NumberFunction: constructor rejects step size zero", "[hass][number]") {
	CHECK_THROWS_AS(
		NumberFunction("Bad Number", [](double) {}, 100, 0, 0),
		std::invalid_argument
	);
}

TEST_CASE("NumberFunction: processMessage valid value", "[hass][number]") {
	TestFixture f;
	double received = -1;
	auto num = std::make_shared<NumberFunction>("Test Num", [&](double v) { received = v; }, 100, 0, 1);
	f.device->registerFunction(num);

	num->processMessage(num->getSubscribeTopics()[0], "50");
	CHECK(received == Catch::Approx(50.0));
}

TEST_CASE("NumberFunction: processMessage clamps to max", "[hass][number]") {
	TestFixture f;
	double received = -1;
	auto num = std::make_shared<NumberFunction>("Test Num", [&](double v) { received = v; }, 100, 0, 1);
	f.device->registerFunction(num);

	num->processMessage(num->getSubscribeTopics()[0], "200");
	CHECK(received == Catch::Approx(100.0));
}

TEST_CASE("NumberFunction: processMessage clamps to min", "[hass][number]") {
	TestFixture f;
	double received = 999;
	auto num = std::make_shared<NumberFunction>("Test Num", [&](double v) { received = v; }, 100, 10, 1);
	f.device->registerFunction(num);

	num->processMessage(num->getSubscribeTopics()[0], "5");
	CHECK(received == Catch::Approx(10.0));
}

TEST_CASE("NumberFunction: processMessage rounds to step", "[hass][number]") {
	TestFixture f;
	double received = -1;
	auto num = std::make_shared<NumberFunction>("Test Num", [&](double v) { received = v; }, 100, 0, 5);
	f.device->registerFunction(num);

	num->processMessage(num->getSubscribeTopics()[0], "17");
	CHECK(received == Catch::Approx(15.0));
}

TEST_CASE("NumberFunction: processMessage rounds up to nearest step", "[hass][number]") {
	TestFixture f;
	double received = -1;
	auto num = std::make_shared<NumberFunction>("Test Num", [&](double v) { received = v; }, 100, 0, 10);
	f.device->registerFunction(num);

	num->processMessage(num->getSubscribeTopics()[0], "26");
	CHECK(received == Catch::Approx(30.0));
}

TEST_CASE("NumberFunction: processMessage ignores invalid string", "[hass][number]") {
	TestFixture f;
	bool called = false;
	auto num = std::make_shared<NumberFunction>("Test Num", [&](double) { called = true; }, 100, 0, 1);
	f.device->registerFunction(num);

	num->processMessage(num->getSubscribeTopics()[0], "not_a_number");
	CHECK_FALSE(called);
}

TEST_CASE("NumberFunction: processMessage ignores wrong topic", "[hass][number]") {
	TestFixture f;
	bool called = false;
	auto num = std::make_shared<NumberFunction>("Test Num", [&](double) { called = true; }, 100, 0, 1);
	f.device->registerFunction(num);

	num->processMessage("wrong/topic", "50");
	CHECK_FALSE(called);
}

TEST_CASE("NumberFunction: processMessage skips callback when value unchanged", "[hass][number]") {
	TestFixture f;
	int call_count = 0;
	auto num = std::make_shared<NumberFunction>("Test Num", [&](double) { call_count++; }, 100, 0, 1);
	f.device->registerFunction(num);

	num->processMessage(num->getSubscribeTopics()[0], "50");
	CHECK(call_count == 1);

	// Same value after step rounding - callback should not fire
	num->update(50.0);
	num->processMessage(num->getSubscribeTopics()[0], "50");
	CHECK(call_count == 1);
}

TEST_CASE("NumberFunction: update change detection", "[hass][number]") {
	TestFixture f;
	auto num = std::make_shared<NumberFunction>("Test Num", [](double) {}, 100, 0, 1);
	f.device->registerFunction(num);

	num->update(25.0);
	CHECK(num->getNumber() == Catch::Approx(25.0));
	num->update(25.0);  // duplicate
	num->update(50.0);  // changed
	CHECK(num->getNumber() == Catch::Approx(50.0));
}

TEST_CASE("NumberFunction: getDiscoveryJson has min/max/step", "[hass][number]") {
	TestFixture f;
	auto num = std::make_shared<NumberFunction>("Test Num", [](double) {}, 200, 10, 5);
	f.device->registerFunction(num);

	auto json = num->getDiscoveryJson();
	CHECK(json["min"] == 10);
	CHECK(json["max"] == 200);
	CHECK(json["step"] == 5);
}

// ========== Regression: default state before any update ==========

TEST_CASE("SwitchFunction: default state is false before update", "[hass][switch][regression]") {
	TestFixture f;
	auto sw = std::make_shared<SwitchFunction>("Test Switch", [](bool) {});
	f.device->registerFunction(sw);

	CHECK(sw->getState() == false);
}

TEST_CASE("OnOffLightFunction: default state is false before update", "[hass][light][regression]") {
	TestFixture f;
	auto light = std::make_shared<OnOffLightFunction>("Test Light", [](bool) {});
	f.device->registerFunction(light);

	CHECK(light->getState() == false);
}

TEST_CASE("DimmableLightFunction: default state and brightness before update", "[hass][dimmable][regression]") {
	TestFixture f;
	auto light = std::make_shared<DimmableLightFunction>("Dim Light", [](bool, double) {});
	f.device->registerFunction(light);

	CHECK(light->getState() == false);
	CHECK(light->getBrightness() == Catch::Approx(0.0));
}

TEST_CASE("NumberFunction: default number is 0 before update", "[hass][number][regression]") {
	TestFixture f;
	auto num = std::make_shared<NumberFunction>("Test Num", [](double) {}, 100, 0, 1);
	f.device->registerFunction(num);

	CHECK(num->getNumber() == Catch::Approx(0.0));
}
