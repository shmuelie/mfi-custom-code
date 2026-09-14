#include <catch2/catch_all.hpp>
#include "hass_mqtt_device/functions/sensor.h"
#include "mfi_mqtt_client/port.h"
#include "mfi_mqtt_client/device.h"
#include "mfi_mqtt_client/system_metrics.h"
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

struct system_fixture {
	hardware_tree hardware;
	fixture mqtt;
	mfi_mqtt_client::system_metrics metrics{{.proc_root = "./proc"}};

	system_fixture() {
		cpu_time(0, 0);
		hardware.write("proc/meminfo", "MemTotal: 65536 kB\nMemAvailable: 49152 kB\n");
		metrics.init(mqtt.device);
		establish();
	}
	void establish() {
		mqtt.establish();
		metrics.reset_connection();
		mqtt.device->service();
	}
	void cpu_time(unsigned user, unsigned idle) {
		hardware.write("proc/stat", "cpu " + std::to_string(user) + " 0 0 " + std::to_string(idle) + "\n");
	}
	void tick(int second) {
		mqtt.device->now = std::chrono::steady_clock::time_point{} + std::chrono::seconds(second);
		metrics.update(mqtt.device->now);
		mqtt.device->service();
	}
	std::shared_ptr<SensorFunction<double>> sensor(std::string const& name) {
		auto function = mqtt.device->findFunction(name);
		REQUIRE(function);
		return std::static_pointer_cast<SensorFunction<double>>(function);
	}
	std::vector<sent_message> samples(std::string const& name) {
		return mqtt.device->on(sensor(name)->getDiscoveryJson()["state_topic"]);
	}
};
}

TEST_CASE("System metrics: options reject invalid freshness and empty proc root", "[mqtt][system-metrics]") {
	using mfi_mqtt_client::system_metrics_options;
	CHECK_NOTHROW(system_metrics_options{}.validate());
	CHECK_THROWS_AS((system_metrics_options{.polling_interval = 0s}.validate()), std::invalid_argument);
	CHECK_THROWS_AS((system_metrics_options{.polling_interval = 61s}.validate()), std::invalid_argument);
	CHECK_THROWS_AS((system_metrics_options{.refresh_interval = 0s}.validate()), std::invalid_argument);
	CHECK_THROWS_AS((system_metrics_options{.expire_after = 179s}.validate()), std::invalid_argument);
	CHECK_THROWS_AS((system_metrics_options{.expire_after = 259201s}.validate()), std::invalid_argument);
	CHECK_THROWS_AS((system_metrics_options{.proc_root = ""}.validate()), std::invalid_argument);
}

TEST_CASE("System metrics: device defaults enable five sensors and allow opt out", "[mqtt][system-metrics]") {
	hardware_tree hardware;
	for (bool enabled : {false, true}) {
		mfi::board board;
		auto device = std::make_shared<mfi_mqtt_client::device>(board, "localhost", 1883, "", "",
			sensor_policy::power(), mfi_mqtt_client::system_metrics_options{.enabled = enabled});
		device->init();
		CHECK(device->getFunctions().size() == (enabled ? 37 : 32));
		CHECK(static_cast<bool>(device->findFunction("CPU Utilization")) == enabled);
	}
}

TEST_CASE("System metrics: discovery and CPU warmup preserve transport readiness", "[mqtt][system-metrics]") {
	system_fixture f;
	REQUIRE(f.mqtt.device->getFunctions().size() == 5);
	CHECK(f.mqtt.device->readyForOnline());
	f.tick(0);
	CHECK(f.samples("CPU Utilization").empty());
	CHECK(f.mqtt.device->readyForOnline());
	for (auto const& name : {"CPU Utilization", "Memory Total", "Memory Available", "Memory Used", "Memory Utilization"}) {
		auto sensor = f.sensor(name);
		auto discovery = sensor->getDiscoveryJson();
		CHECK(discovery["entity_category"] == "diagnostic");
		CHECK(discovery["state_class"] == "measurement");
		CHECK(discovery["expire_after"] == 180);
		CHECK(discovery["suggested_display_precision"] == 1);
		auto availability = f.mqtt.device->on(*sensor->availabilityTopic());
		REQUIRE_FALSE(availability.empty());
		CHECK(availability.front().payload["availability"] == "offline");
		CHECK(availability.front().qos == 1);
		CHECK(availability.front().retained);
		if (std::string(name) == "CPU Utilization" || std::string(name) == "Memory Utilization") {
			CHECK(discovery["unit_of_measurement"] == "%");
			CHECK_FALSE(discovery.contains("device_class"));
		}
		else {
			CHECK(discovery["unit_of_measurement"] == "MiB");
			CHECK(discovery["device_class"] == "data_size");
		}
	}
	CHECK(f.samples("Memory Total").front().payload == json{{"value", 64.0}});
	CHECK(f.samples("Memory Available").front().payload == json{{"value", 48.0}});
	CHECK(f.samples("Memory Used").front().payload == json{{"value", 16.0}});
	CHECK(f.samples("Memory Utilization").front().payload == json{{"value", 25.0}});
	f.cpu_time(25, 75);
	f.tick(9);
	CHECK(f.samples("CPU Utilization").empty());
	f.tick(10);
	auto cpu = f.samples("CPU Utilization");
	REQUIRE(cpu.size() == 1);
	CHECK(cpu.front().payload == json{{"value", 25.0}});
	CHECK_FALSE(cpu.front().retained);
	CHECK(cpu.front().qos == 0);
}

TEST_CASE("System metrics: independent ten second sampling and sixty second refresh", "[mqtt][system-metrics]") {
	system_fixture f;
	for (int second = 0; second <= 3600; ++second) {
		f.cpu_time(second * 25, second * 75);
		f.tick(second);
	}
	for (auto const& name : {"CPU Utilization", "Memory Total", "Memory Available", "Memory Used", "Memory Utilization"}) {
		auto messages = f.samples(name);
		bool cpu = std::string(name) == "CPU Utilization";
		REQUIRE(messages.size() == (cpu ? 60 : 61));
		for (std::size_t i = 0; i < messages.size(); ++i) {
			CHECK(messages[i].time == std::chrono::steady_clock::time_point{} + std::chrono::seconds((cpu ? 10 : 0) + i * 60));
			CHECK_FALSE(messages[i].retained);
			CHECK(messages[i].qos == 0);
		}
	}
}

TEST_CASE("System metrics: delayed offline acknowledgements do not delay outlet readiness", "[mqtt][system-metrics][regression]") {
	bool fault_before_sample = GENERATE(false, true);
	system_fixture f;
	auto power = f.mqtt.sensor();
	f.mqtt.device->auto_ack = false;
	++f.mqtt.device->epoch;
	f.establish();
	if (fault_before_sample) {
		f.sensor("Memory Total")->invalidate("read failed");
		power->invalidate("read failed");
	}
	f.tick(0);
	f.mqtt.update(power, 100, 0s);
	CHECK_FALSE(f.mqtt.device->readyForOnline());
	CHECK(f.mqtt.device->on(power->getDiscoveryJson()["state_topic"]).empty());
	for (auto const& name : {"CPU Utilization", "Memory Total", "Memory Available", "Memory Used", "Memory Utilization"}) {
		CHECK(f.samples(name).empty());
		CHECK_FALSE(f.sensor(name)->readyForOnline());
	}
	for (auto const& message : f.mqtt.device->messages) {
		if (message.ticket.epoch == f.mqtt.device->epoch) {
			f.mqtt.device->acked.insert(message.ticket.sequence);
		}
	}
	f.mqtt.device->auto_ack = true;
	f.tick(1);
	for (auto const& name : {"CPU Utilization", "Memory Total", "Memory Available", "Memory Used", "Memory Utilization"}) {
		CHECK(f.sensor(name)->readyForOnline());
		CHECK(f.samples(name).empty());
	}
	CHECK_FALSE(power->readyForOnline());
	CHECK_FALSE(f.mqtt.device->readyForOnline());
	f.mqtt.update(power, 100, 1s);
	CHECK(f.mqtt.device->readyForOnline());
	f.mqtt.device->sendStatus();
	CHECK(f.samples("Memory Total").empty());
	f.cpu_time(25, 75);
	f.hardware.write("proc/meminfo", "MemTotal: 65536 kB\nMemAvailable: 32768 kB\n");
	f.tick(9);
	CHECK(f.samples("Memory Total").empty());
	f.tick(10);
	REQUIRE(f.samples("Memory Utilization").size() == 1);
	CHECK(f.samples("Memory Utilization").front().payload == json{{"value", 50.0}});
	REQUIRE(f.samples("CPU Utilization").size() == 1);
	CHECK(f.samples("CPU Utilization").front().payload == json{{"value", 25.0}});
}

TEST_CASE("System metrics: failures and recovery remain independent", "[mqtt][system-metrics]") {
	system_fixture f;
	f.tick(0);
	f.cpu_time(25, 75);
	f.tick(10);
	f.hardware.write("proc/stat", "cpu broken\n");
	f.hardware.write("proc/meminfo", "MemTotal: 65536 kB\nMemAvailable: 32768 kB\n");
	f.tick(20);
	CHECK(f.samples("CPU Utilization").size() == 1);
	CHECK(f.samples("Memory Utilization").back().payload == json{{"value", 50.0}});
	CHECK(f.mqtt.device->on(*f.sensor("CPU Utilization")->availabilityTopic()).back().payload["availability"] == "offline");
	f.cpu_time(100, 200);
	f.hardware.write("proc/meminfo", "MemTotal: invalid kB\n");
	f.tick(30);
	CHECK(f.samples("CPU Utilization").size() == 1);
	f.cpu_time(150, 250);
	f.tick(40);
	CHECK(f.samples("CPU Utilization").back().payload == json{{"value", 50.0}});
	for (auto const& name : {"Memory Total", "Memory Available", "Memory Used", "Memory Utilization"}) {
		CHECK(f.mqtt.device->on(*f.sensor(name)->availabilityTopic()).back().payload["availability"] == "offline");
	}
	f.hardware.write("proc/meminfo", "MemTotal: 65536 kB\nMemFree: 8192 kB\nBuffers: 8192 kB\nCached: 16384 kB\n");
	f.cpu_time(200, 300);
	f.tick(50);
	CHECK(f.samples("Memory Utilization").size() == 3);
	CHECK(f.samples("Memory Available").back().payload == json{{"value", 32.0}});
	CHECK(f.mqtt.device->readyForOnline());
}

TEST_CASE("System metrics: reconnect discards cached readings and CPU baseline", "[mqtt][system-metrics]") {
	system_fixture f;
	f.tick(0);
	f.cpu_time(25, 75);
	f.tick(10);
	f.mqtt.device->connected = false;
	f.cpu_time(100, 200);
	f.tick(20);
	f.mqtt.device->connected = true;
	++f.mqtt.device->epoch;
	f.establish();
	f.mqtt.device->sendStatus();
	CHECK(f.samples("CPU Utilization").size() == 1);
	CHECK(f.samples("Memory Total").size() == 1);
	CHECK(f.mqtt.device->readyForOnline());
	f.tick(21);
	CHECK(f.samples("CPU Utilization").size() == 1);
	CHECK(f.samples("Memory Total").size() == 2);
	f.cpu_time(150, 250);
	f.tick(31);
	REQUIRE(f.samples("CPU Utilization").size() == 2);
	CHECK(f.samples("CPU Utilization").back().payload == json{{"value", 50.0}});
}

TEST_CASE("MQTT freshness: awaiting telemetry does not bypass the power readiness gate", "[mqtt][freshness][system-metrics]") {
	fixture f;
	auto power = f.sensor();
	auto cpu = f.sensor("CPU Utilization", sensor_policy::telemetry());
	f.establish();
	cpu->await_sample();
	f.device->service();
	CHECK(cpu->readyForOnline());
	CHECK_FALSE(f.device->readyForOnline());
	CHECK(f.device->on(cpu->getDiscoveryJson()["state_topic"]).empty());
	f.update(power, 100, 0s);
	CHECK(f.device->readyForOnline());
	CHECK(f.device->on(cpu->getDiscoveryJson()["state_topic"]).empty());
}

TEST_CASE("MQTT freshness: pending readiness ends on publication or connection reset", "[mqtt][freshness][regression]") {
	bool published = GENERATE(false, true);
	fixture f;
	auto sensor = f.sensor("Diagnostic", sensor_policy::telemetry());
	f.establish();
	sensor->await_sample();
	if (published) {
		f.update(sensor, 10, 0s);
	}
	f.device->auto_ack = false;
	if (published) {
		sensor->invalidate("read failed");
	}
	else {
		++f.device->epoch;
		f.establish();
	}
	f.update(sensor, 20, 1s);
	for (auto const& message : f.device->messages) {
		if (message.ticket.epoch == f.device->epoch) {
			f.device->acked.insert(message.ticket.sequence);
		}
	}
	f.device->service();
	CHECK_FALSE(sensor->readyForOnline());
	f.device->sendStatus();
	CHECK(f.device->on(sensor->getDiscoveryJson()["state_topic"]).size() == (published ? 1 : 0));
	f.device->auto_ack = true;
	f.update(sensor, 30, 2s);
	CHECK(sensor->readyForOnline());
	auto samples = f.device->on(sensor->getDiscoveryJson()["state_topic"]);
	REQUIRE(samples.size() == (published ? 2 : 1));
	CHECK(samples.back().payload == json{{"value", 30.0}});
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
