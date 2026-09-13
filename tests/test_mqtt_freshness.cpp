#include <catch2/catch_all.hpp>
#include "hass_mqtt_device/functions/sensor.h"
#include "mfi_mqtt_client/port.h"
#include <spdlog/sinks/null_sink.h>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <cstdlib>

using namespace std::chrono_literals;

namespace {
struct sent_message {
	std::string topic;
	json payload;
	int qos;
	bool retained;
	publication ticket;
	std::chrono::steady_clock::time_point time;
};

class capture_device final : public DeviceBase {
public:
	capture_device() : DeviceBase("fixture", "fixture") {}
	bool connected = true;
	bool auto_ack = true;
	std::uint64_t epoch = 1;
	std::uint64_t sequence = 0;
	std::chrono::steady_clock::time_point now{};
	std::vector<sent_message> messages;
	std::set<std::uint64_t> acked;
	std::function<bool(std::string const&, json const&)> reject;

	bool isConnected() const override { return connected; }
	publication publishMessage(std::string const& topic, json const& payload, int qos, bool retain) override {
		if (!connected || (reject && reject(topic, payload))) {
			return {};
		}
		publication ticket{MOSQ_ERR_SUCCESS, epoch, ++sequence};
		messages.push_back({topic, payload, qos, retain, ticket, now});
		if (auto_ack || qos == 0) {
			acked.insert(ticket.sequence);
		}
		return ticket;
	}
	publication_state publicationState(publication const& ticket) const override {
		if (!connected || !ticket.accepted() || ticket.epoch != epoch) {
			return publication_state::failed;
		}
		return acked.contains(ticket.sequence) ? publication_state::complete : publication_state::pending;
	}
	std::vector<sent_message> on(std::string const& topic) const {
		std::vector<sent_message> found;
		for (auto const& message : messages) {
			if (message.topic == topic) {
				found.push_back(message);
			}
		}
		return found;
	}
};

struct fixture {
	std::shared_ptr<MQTTConnector> connector;
	std::shared_ptr<capture_device> device;
	fixture() {
		spdlog::set_default_logger(std::make_shared<spdlog::logger>("freshness",
			std::make_shared<spdlog::sinks::null_sink_mt>()));
		connector = std::make_shared<MQTTConnector>("localhost", 1883, "", "", "fixture");
		device = std::make_shared<capture_device>();
		connector->registerDevice(device);
	}
	std::shared_ptr<SensorFunction<double>> sensor(std::string name = "Power", sensor_policy policy = sensor_policy::power(), int precision = 4) {
		auto sensor = std::make_shared<SensorFunction<double>>(name, SensorAttributes{
			.device_class = "power", .state_class = "measurement",
			.unit_of_measurement = "W", .suggested_display_precision = precision
		}, policy);
		device->registerFunction(sensor);
		return sensor;
	}
	void establish() {
		device->beginConnection(device->epoch);
		device->service();
		device->service();
	}
	void update(std::shared_ptr<SensorFunction<double>> const& sensor, double value, std::chrono::seconds elapsed) {
		device->now = std::chrono::steady_clock::time_point{} + elapsed;
		sensor->update(value, device->now);
		device->service();
	}
};

class hardware_tree {
	std::filesystem::path previous = std::filesystem::current_path();
	std::filesystem::path root;
public:
	hardware_tree() {
		auto pattern = (std::filesystem::temp_directory_path() / "mfi-freshness-XXXXXX").string();
		auto created = ::mkdtemp(pattern.data());
		if (!created) {
			throw std::runtime_error("Cannot create hardware fixture");
		}
		root = created;
		std::filesystem::current_path(root);
		write("etc/board.info", "board.name=mPower Pro\nboard.shortname=mpower\nboard.sysid=e648\n");
		write("etc/version", "fixture\n");
		write("etc/persistent/cfg/config_file", "port.0.label=One\nport.1.label=Two\n");
	}
	~hardware_tree() {
		std::filesystem::current_path(previous);
		std::filesystem::remove_all(root);
	}
	void write(std::string const& path, std::string const& text) {
		std::filesystem::create_directories(std::filesystem::path(path).parent_path());
		std::ofstream out(path);
		out << text;
	}
};
}

TEST_CASE("MQTT freshness: constant zero and power refresh for one hour", "[mqtt][freshness]") {
	fixture f;
	auto power = f.sensor("Power");
	auto zero = f.sensor("Zero");
	f.establish();
	for (int second = 0; second <= 3600; ++second) {
		f.update(power, 100, std::chrono::seconds(second));
		f.update(zero, 0, std::chrono::seconds(second));
	}
	for (auto const& sensor : {power, zero}) {
		auto messages = f.device->on(sensor->getDiscoveryJson()["state_topic"]);
		REQUIRE(messages.size() == 61);
		for (std::size_t i = 0; i < messages.size(); ++i) {
			CHECK(messages[i].time == std::chrono::steady_clock::time_point{} + std::chrono::seconds(60 * i));
			CHECK(messages[i].qos == 0);
			CHECK_FALSE(messages[i].retained);
			CHECK(messages[i].payload["value"] == (sensor == power ? 100 : 0));
		}
	}
}

TEST_CASE("MQTT freshness: changes rounding and zero are immediate", "[mqtt][freshness]") {
	fixture f;
	auto power = f.sensor();
	f.establish();
	f.update(power, 42.500001, 0s);
	f.update(power, 42.500002, 1s);
	f.update(power, 43.25, 2s);
	f.update(power, 0, 3s);
	auto messages = f.device->on(power->getDiscoveryJson()["state_topic"]);
	REQUIRE(messages.size() == 3);
	CHECK(messages[0].payload["value"] == 42.5);
	CHECK(messages[1].time == std::chrono::steady_clock::time_point{} + 2s);
	CHECK(messages[2].payload["value"] == 0);
}

TEST_CASE("MQTT freshness: invalid input and quantization cannot serialize null", "[mqtt][freshness]") {
	double invalid = GENERATE(-0.000001, std::numeric_limits<double>::quiet_NaN(),
		std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(), 1e305);
	fixture f;
	auto power = f.sensor();
	f.establish();
	f.update(power, 100, 0s);
	CHECK_FALSE(power->update(invalid, std::chrono::steady_clock::time_point{} + 1s));
	f.device->service();
	auto messages = f.device->on(power->getDiscoveryJson()["state_topic"]);
	REQUIRE(messages.size() == 1);
	auto health = f.device->on(*power->availabilityTopic());
	REQUIRE(health.size() == 3);
	CHECK(health.back().payload["availability"] == "offline");
	f.update(power, 100, 2s);
	CHECK(f.device->on(power->getDiscoveryJson()["state_topic"]).size() == 2);
}

TEST_CASE("MQTT freshness: invalid precision is explicitly rejected", "[mqtt][freshness]") {
	fixture f;
	auto power = f.sensor("Power", sensor_policy::power(), 400);
	f.establish();
	CHECK_FALSE(power->update(100));
	CHECK(f.device->on(power->getDiscoveryJson()["state_topic"]).empty());
}

TEST_CASE("MQTT freshness: rejected publish stays offline and cannot replay cache", "[mqtt][freshness]") {
	fixture f;
	auto power = f.sensor();
	f.establish();
	f.device->reject = [](auto const&, auto const& payload) { return payload.contains("value"); };
	f.update(power, 100, 0s);
	power->invalidate("read failed");
	power->sendStatus();
	f.device->service();
	CHECK(f.device->on(power->getDiscoveryJson()["state_topic"]).empty());
	auto health = f.device->on(*power->availabilityTopic());
	REQUIRE(health.size() == 1);
	CHECK(health[0].payload["availability"] == "offline");
	f.device->reject = {};
	f.update(power, 100, 1s);
	CHECK(f.device->on(power->getDiscoveryJson()["state_topic"]).size() == 1);
	CHECK(f.device->on(*power->availabilityTopic()).back().payload["availability"] == "online");
}

TEST_CASE("MQTT freshness: rejected change retries unchanged next sample", "[mqtt][freshness]") {
	fixture f;
	auto power = f.sensor();
	f.establish();
	f.update(power, 100, 0s);
	f.device->reject = [](auto const&, auto const& payload) { return payload.contains("value"); };
	f.update(power, 200, 1s);
	f.device->reject = {};
	f.update(power, 200, 2s);
	auto messages = f.device->on(power->getDiscoveryJson()["state_topic"]);
	REQUIRE(messages.size() == 2);
	CHECK(messages.back().payload["value"] == 200);
	CHECK(messages.back().time == std::chrono::steady_clock::time_point{} + 2s);
}

TEST_CASE("MQTT freshness: reconnect and status never refresh cached power", "[mqtt][freshness]") {
	fixture f;
	auto power = f.sensor();
	f.establish();
	f.update(power, 100, 0s);
	f.device->connected = false;
	f.update(power, 200, 1s);
	f.device->connected = true;
	++f.device->epoch;
	f.establish();
	f.device->sendStatus();
	CHECK(f.device->on(power->getDiscoveryJson()["state_topic"]).size() == 1);
	power->invalidate("still unreadable");
	f.device->service();
	CHECK(f.device->on(*power->availabilityTopic()).back().payload["availability"] == "offline");
	f.update(power, 100, 2s);
	CHECK(f.device->on(power->getDiscoveryJson()["state_topic"]).size() == 2);
}

TEST_CASE("MQTT freshness: control acknowledgement gates online and superseded transitions", "[mqtt][freshness]") {
	fixture f;
	auto power = f.sensor();
	f.device->auto_ack = false;
	f.establish();
	f.update(power, 100, 0s);
	CHECK_FALSE(f.device->readyForOnline());
	CHECK(f.device->on(power->getDiscoveryJson()["state_topic"]).empty());
	f.device->acked.insert(f.device->messages.front().ticket.sequence);
	f.device->service();
	CHECK_FALSE(f.device->readyForOnline());
	f.update(power, 100, 1s);
	auto online = f.device->messages.back();
	CHECK(online.payload["availability"] == "online");
	power->invalidate("failed while online acknowledgement pending");
	CHECK_FALSE(f.device->readyForOnline());
	f.device->acked.insert(online.ticket.sequence);
	f.device->service();
	auto offline = f.device->messages.back();
	CHECK(offline.payload["availability"] == "offline");
	CHECK_FALSE(f.device->readyForOnline());
	f.device->acked.insert(offline.ticket.sequence);
	f.device->service();
	CHECK(f.device->readyForOnline());
}

TEST_CASE("MQTT freshness: discovery combines durable channel and transport availability", "[mqtt][freshness]") {
	fixture f;
	auto power = f.sensor();
	auto legacy = f.sensor("Legacy", {});
	f.device->sendDiscovery();
	auto power_config = f.device->on(power->getDiscoveryTopic()).back().payload;
	REQUIRE(power_config["availability"].size() == 2);
	CHECK(power_config["availability_mode"] == "all");
	CHECK(power_config["expire_after"] == 180);
	CHECK_FALSE(power_config.contains("availability_topic"));
	CHECK_FALSE(power_config.contains("availability_template"));
	CHECK(power_config["availability"][1]["topic"] == *power->availabilityTopic());
	auto legacy_config = f.device->on(legacy->getDiscoveryTopic()).back().payload;
	CHECK(legacy_config.contains("availability_topic"));
	CHECK_FALSE(legacy_config.contains("availability"));
	CHECK_FALSE(legacy_config.contains("expire_after"));
	f.update(legacy, 50, 0s);
	f.update(legacy, 50, 120s);
	auto messages = f.device->on(legacy->getDiscoveryJson()["state_topic"]);
	REQUIRE(messages.size() == 1);
	CHECK(messages[0].retained);
}

TEST_CASE("MQTT freshness: inconsistent freshness policy is rejected", "[mqtt][freshness]") {
	fixture f;
	CHECK_THROWS_AS(f.sensor("Power", sensor_policy::power(60s, 179s)), std::invalid_argument);
	CHECK_THROWS_AS(f.sensor("Power", sensor_policy{60s, 180s, true, true}), std::invalid_argument);
	CHECK_THROWS_AS(f.sensor("Power", sensor_policy{0s, 180s, false, true}), std::invalid_argument);
	CHECK_THROWS_AS(sensor_policy::power(0s, 0s), std::invalid_argument);
}

TEST_CASE("MQTT freshness: generic reconnect synchronizes accepted status cache", "[mqtt][freshness]") {
	bool accept_status = GENERATE(true, false);
	fixture f;
	auto sensor = f.sensor("Current", {});
	f.establish();
	f.update(sensor, 10, 0s);
	f.device->connected = false;
	f.update(sensor, 20, 1s);
	f.device->connected = true;
	++f.device->epoch;
	f.establish();
	if (!accept_status) {
		f.device->reject = [](auto const&, auto const&) { return true; };
	}
	f.device->sendStatus();
	f.device->reject = {};
	f.update(sensor, 10, 2s);
	f.update(sensor, 10, 120s);
	auto messages = f.device->on(sensor->getDiscoveryJson()["state_topic"]);
	REQUIRE(messages.size() == (accept_status ? 3 : 2));
	CHECK(messages.back().payload["value"] == 10);
	CHECK(messages.back().time == std::chrono::steady_clock::time_point{} + 2s);
	if (accept_status) {
		CHECK(messages[1].payload["value"] == 20);
	}
}

TEST_CASE("MQTT polling: checked reads preserve legacy getter behavior", "[mqtt][hardware]") {
	hardware_tree tree;
	mfi::sensor sensor(1);
	CHECK(sensor.power() == 0);
	CHECK(std::get<mfi::sensor_read_error>(sensor.power_checked()) == mfi::sensor_read_error::open_failed);
	tree.write("proc/power/active_pwr1", " 0 \n");
	CHECK(std::get<double>(sensor.power_checked()) == 0);
	tree.write("proc/power/active_pwr1", "12.5 trailing");
	CHECK(sensor.power() == 12.5);
	CHECK(std::holds_alternative<mfi::sensor_read_error>(sensor.power_checked()));
	for (auto text : {"", "nan", "inf", "1e999", "1e-9999", "-1e-9999", "garbage", "12 34", "++1", "+-1"}) {
		tree.write("proc/power/active_pwr1", text);
		CHECK(std::holds_alternative<mfi::sensor_read_error>(sensor.power_checked()));
	}
	tree.write("proc/power/active_pwr1", " \t+12.5e1\r\n");
	CHECK(std::get<double>(sensor.power_checked()) == 125);
	tree.write("proc/power/active_pwr1", std::string(257, '1'));
	CHECK(std::holds_alternative<mfi::sensor_read_error>(sensor.power_checked()));
	tree.write("proc/power/active_pwr1", "1e305");
	CHECK(std::get<double>(sensor.power_checked()) == 1e305);
	std::filesystem::remove("proc/power/active_pwr1");
	std::filesystem::create_directory("proc/power/active_pwr1");
	CHECK(std::holds_alternative<mfi::sensor_read_error>(sensor.power_checked()));
	tree.write("proc/power/i_rms1", "0.25");
	tree.write("proc/power/v_rms1", "120");
	tree.write("proc/power/pf1", "0.98");
	CHECK(std::get<double>(sensor.current_checked()) == 0.25);
	CHECK(std::get<double>(sensor.voltage_checked()) == 120);
	CHECK(std::get<double>(sensor.power_factor_checked()) == 0.98);
}

TEST_CASE("MQTT polling: failed measurement never skips healthy power or later port", "[mqtt][hardware]") {
	hardware_tree tree;
	tree.write("proc/power/active_pwr1", "bad");
	tree.write("proc/power/active_pwr2", "100");
	tree.write("proc/power/i_rms2", "bad");
	fixture f;
	mfi::board board;
	mfi_mqtt_client::port one(board, board.sensors()[0]);
	mfi_mqtt_client::port two(board, board.sensors()[1]);
	one.init(f.device);
	two.init(f.device);
	f.establish();
	one.update();
	two.update();
	f.device->service();
	CHECK(f.device->on("home/fixture/one_power/state").empty());
	auto two_messages = f.device->on("home/fixture/two_power/state");
	REQUIRE(two_messages.size() == 1);
	CHECK(two_messages[0].payload["value"] == 100);
	CHECK(f.device->on("home/fixture/one_power/availability").back().payload["availability"] == "offline");
}
