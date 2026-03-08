#include <catch2/catch_all.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/null_sink.h>
#include "hass_mqtt_device/core/mqtt_connector.h"
#include "hass_mqtt_device/core/device_base.h"
#include "hass_mqtt_device/functions/switch.h"
#include "hass_mqtt_device/functions/sensor.h"

namespace {

struct DeviceTestFixture {
	std::shared_ptr<MQTTConnector> connector;

	DeviceTestFixture() {
		auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
		auto logger = std::make_shared<spdlog::logger>("test", null_sink);
		spdlog::set_default_logger(logger);

		connector = std::make_shared<MQTTConnector>("localhost", 1883, "user", "pass", "conn_uid");
	}
};

} // namespace

// ========== DeviceBase construction ==========

TEST_CASE("DeviceBase: getName returns device name", "[device]") {
	DeviceTestFixture f;
	auto dev = std::make_shared<DeviceBase>("My Device", "my_id");
	f.connector->registerDevice(dev);

	CHECK(dev->getName() == "My Device");
}

TEST_CASE("DeviceBase: getId returns sanitized id", "[device]") {
	DeviceTestFixture f;
	auto dev = std::make_shared<DeviceBase>("My Device", "my_id");
	f.connector->registerDevice(dev);

	CHECK(dev->getId() == "my_id");
}

TEST_CASE("DeviceBase: getCleanName sanitizes name", "[device]") {
	DeviceTestFixture f;
	auto dev = std::make_shared<DeviceBase>("My Device!", "id");
	f.connector->registerDevice(dev);

	CHECK(dev->getCleanName() == "my_device");
}

TEST_CASE("DeviceBase: getUniqueId includes connector id", "[device]") {
	DeviceTestFixture f;
	auto dev = std::make_shared<DeviceBase>("Dev", "dev_id");
	f.connector->registerDevice(dev);

	auto uid = dev->getUniqueId();
	CHECK(uid.find("conn_uid") != std::string::npos);
	CHECK(uid.find("dev_id") != std::string::npos);
}

TEST_CASE("DeviceBase: getUniqueId with empty id uses 'empty' placeholder", "[device]") {
	DeviceTestFixture f;
	auto dev = std::make_shared<DeviceBase>("Dev", "");
	f.connector->registerDevice(dev);

	// getValidHassString("") returns "empty", so m_id is "empty" not ""
	auto uid = dev->getUniqueId();
	CHECK(uid.find("conn_uid") != std::string::npos);
	CHECK(uid.find("empty") != std::string::npos);
}

TEST_CASE("DeviceBase: getFullId combines uniqueId and cleanName", "[device]") {
	DeviceTestFixture f;
	auto dev = std::make_shared<DeviceBase>("My Dev", "dev1");
	f.connector->registerDevice(dev);

	auto full = dev->getFullId();
	CHECK(full.find("conn_uid") != std::string::npos);
	CHECK(full.find("dev1") != std::string::npos);
	CHECK(full.find("my_dev") != std::string::npos);
}

// ========== Function registration ==========

TEST_CASE("DeviceBase: registerFunction adds function", "[device][register]") {
	DeviceTestFixture f;
	auto dev = std::make_shared<DeviceBase>("Dev", "id");
	f.connector->registerDevice(dev);

	auto sw = std::make_shared<SwitchFunction>("Relay", [](bool) {});
	dev->registerFunction(sw);

	CHECK(dev->getFunctions().size() == 1);
}

TEST_CASE("DeviceBase: registerFunction rejects duplicate name", "[device][register]") {
	DeviceTestFixture f;
	auto dev = std::make_shared<DeviceBase>("Dev", "id");
	f.connector->registerDevice(dev);

	auto sw1 = std::make_shared<SwitchFunction>("Relay", [](bool) {});
	auto sw2 = std::make_shared<SwitchFunction>("Relay", [](bool) {});
	dev->registerFunction(sw1);

	CHECK_THROWS_AS(dev->registerFunction(sw2), std::runtime_error);
}

TEST_CASE("DeviceBase: registerFunction allows different names", "[device][register]") {
	DeviceTestFixture f;
	auto dev = std::make_shared<DeviceBase>("Dev", "id");
	f.connector->registerDevice(dev);

	auto sw = std::make_shared<SwitchFunction>("Relay", [](bool) {});
	auto sensor = std::make_shared<SensorFunction<double>>("Power", SensorAttributes{
		.device_class = "power", .state_class = "measurement",
		.unit_of_measurement = "W", .suggested_display_precision = 2
	});
	dev->registerFunction(sw);
	dev->registerFunction(sensor);

	CHECK(dev->getFunctions().size() == 2);
}

// ========== findFunction ==========

TEST_CASE("DeviceBase: findFunction by name", "[device][find]") {
	DeviceTestFixture f;
	auto dev = std::make_shared<DeviceBase>("Dev", "id");
	f.connector->registerDevice(dev);

	auto sw = std::make_shared<SwitchFunction>("My Relay", [](bool) {});
	dev->registerFunction(sw);

	auto found = dev->findFunction("My Relay");
	REQUIRE(found != nullptr);
	CHECK(found->getName() == "My Relay");
}

TEST_CASE("DeviceBase: findFunction by clean name", "[device][find]") {
	DeviceTestFixture f;
	auto dev = std::make_shared<DeviceBase>("Dev", "id");
	f.connector->registerDevice(dev);

	auto sw = std::make_shared<SwitchFunction>("My Relay", [](bool) {});
	dev->registerFunction(sw);

	auto found = dev->findFunction("my_relay");
	REQUIRE(found != nullptr);
	CHECK(found->getName() == "My Relay");
}

TEST_CASE("DeviceBase: findFunction returns nullptr when not found", "[device][find]") {
	DeviceTestFixture f;
	auto dev = std::make_shared<DeviceBase>("Dev", "id");
	f.connector->registerDevice(dev);

	CHECK(dev->findFunction("nonexistent") == nullptr);
}

// ========== processMessage delegation ==========

TEST_CASE("DeviceBase: processMessage delegates to matching function", "[device][message]") {
	DeviceTestFixture f;
	auto dev = std::make_shared<DeviceBase>("Dev", "id");
	f.connector->registerDevice(dev);

	bool called = false;
	auto sw = std::make_shared<SwitchFunction>("Test Relay", [&](bool) { called = true; });
	dev->registerFunction(sw);

	// processMessage matches by function clean name in topic
	auto topics = sw->getSubscribeTopics();
	REQUIRE(!topics.empty());
	dev->processMessage(topics[0], R"({"value":"ON"})");
	CHECK(called);
}

TEST_CASE("DeviceBase: processMessage ignores non-matching topic", "[device][message]") {
	DeviceTestFixture f;
	auto dev = std::make_shared<DeviceBase>("Dev", "id");
	f.connector->registerDevice(dev);

	bool called = false;
	auto sw = std::make_shared<SwitchFunction>("Test Relay", [&](bool) { called = true; });
	dev->registerFunction(sw);

	dev->processMessage("completely/unrelated/topic", R"({"value":"ON"})");
	CHECK_FALSE(called);
}

// ========== getSubscribeTopics ==========

TEST_CASE("DeviceBase: getSubscribeTopics aggregates from functions", "[device][topics]") {
	DeviceTestFixture f;
	auto dev = std::make_shared<DeviceBase>("Dev", "id");
	f.connector->registerDevice(dev);

	auto sw = std::make_shared<SwitchFunction>("Relay 1", [](bool) {});
	auto sw2 = std::make_shared<SwitchFunction>("Relay 2", [](bool) {});
	dev->registerFunction(sw);
	dev->registerFunction(sw2);

	auto topics = dev->getSubscribeTopics();
	CHECK(topics.size() == 2);
}

TEST_CASE("DeviceBase: getSubscribeTopics empty when no subscribing functions", "[device][topics]") {
	DeviceTestFixture f;
	auto dev = std::make_shared<DeviceBase>("Dev", "id");
	f.connector->registerDevice(dev);

	auto sensor = std::make_shared<SensorFunction<double>>("Power", SensorAttributes{
		.device_class = "power", .state_class = "measurement",
		.unit_of_measurement = "W", .suggested_display_precision = 2
	});
	dev->registerFunction(sensor);

	auto topics = dev->getSubscribeTopics();
	CHECK(topics.empty());
}

// ========== Default virtual methods (tested via TestableDevice subclass) ==========

namespace {
class TestableDevice : public DeviceBase {
public:
	using DeviceBase::DeviceBase;
	using DeviceBase::getManufacturer;
	using DeviceBase::getModel;
	using DeviceBase::getSoftwareVersion;
	using DeviceBase::getHardwareVersion;
	using DeviceBase::getConfigurationUrl;
	using DeviceBase::getModelId;
	using DeviceBase::getSerialNumber;
};
}

TEST_CASE("DeviceBase: default virtual methods return nullopt", "[device][virtuals]") {
	DeviceTestFixture f;
	auto dev = std::make_shared<TestableDevice>("Dev", "id");
	CHECK_FALSE(dev->getManufacturer().has_value());
	CHECK_FALSE(dev->getModel().has_value());
	CHECK_FALSE(dev->getSoftwareVersion().has_value());
	CHECK_FALSE(dev->getHardwareVersion().has_value());
	CHECK_FALSE(dev->getConfigurationUrl().has_value());
	CHECK_FALSE(dev->getModelId().has_value());
	CHECK_FALSE(dev->getSerialNumber().has_value());
}

// ========== FunctionBase ==========

TEST_CASE("FunctionBase: getName returns function name", "[function_base]") {
	DeviceTestFixture f;
	auto dev = std::make_shared<DeviceBase>("Dev", "id");
	f.connector->registerDevice(dev);

	auto sw = std::make_shared<SwitchFunction>("My Switch", [](bool) {});
	dev->registerFunction(sw);

	CHECK(sw->getName() == "My Switch");
}

TEST_CASE("FunctionBase: getCleanName sanitizes function name", "[function_base]") {
	DeviceTestFixture f;
	auto dev = std::make_shared<DeviceBase>("Dev", "id");
	f.connector->registerDevice(dev);

	auto sw = std::make_shared<SwitchFunction>("Port 1 Relay!", [](bool) {});
	dev->registerFunction(sw);

	CHECK(sw->getCleanName() == "port_1_relay");
}

TEST_CASE("FunctionBase: getId includes parent device fullId", "[function_base]") {
	DeviceTestFixture f;
	auto dev = std::make_shared<DeviceBase>("Dev", "dev_id");
	f.connector->registerDevice(dev);

	auto sw = std::make_shared<SwitchFunction>("Relay", [](bool) {});
	dev->registerFunction(sw);

	auto id = sw->getId();
	CHECK(id.find("conn_uid") != std::string::npos);
	CHECK(id.find("dev_id") != std::string::npos);
	CHECK(id.find("relay") != std::string::npos);
}

// ========== MQTTConnector ==========

TEST_CASE("MQTTConnector: getId returns unique_id", "[connector]") {
	DeviceTestFixture f;
	CHECK(f.connector->getId() == "conn_uid");
}

TEST_CASE("MQTTConnector: getAvailabilityTopic has correct format", "[connector]") {
	DeviceTestFixture f;
	CHECK(f.connector->getAvailabilityTopic() == "home/conn_uid/availability");
}

TEST_CASE("MQTTConnector: isConnected initially false", "[connector]") {
	DeviceTestFixture f;
	CHECK_FALSE(f.connector->isConnected());
}

TEST_CASE("MQTTConnector: registerDevice duplicate throws", "[connector]") {
	DeviceTestFixture f;
	auto dev1 = std::make_shared<DeviceBase>("Dev", "id");
	auto dev2 = std::make_shared<DeviceBase>("Dev", "id");
	f.connector->registerDevice(dev1);

	CHECK_THROWS_AS(f.connector->registerDevice(dev2), std::runtime_error);
}

TEST_CASE("MQTTConnector: getDevice by name", "[connector]") {
	DeviceTestFixture f;
	auto dev = std::make_shared<DeviceBase>("My Device", "id");
	f.connector->registerDevice(dev);

	CHECK(f.connector->getDevice("My Device") != nullptr);
	CHECK(f.connector->getDevice("nonexistent") == nullptr);
}

TEST_CASE("MQTTConnector: unregisterDevice removes device", "[connector]") {
	DeviceTestFixture f;
	auto dev = std::make_shared<DeviceBase>("Dev", "dev_id");
	f.connector->registerDevice(dev);

	CHECK(f.connector->getDevice("Dev") != nullptr);
	f.connector->unregisterDevice("dev_id");
	CHECK(f.connector->getDevice("Dev") == nullptr);
}
