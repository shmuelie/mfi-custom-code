#include "mfi_mqtt_client/port.h"

using namespace mfi;
using namespace mfi_mqtt_client;
using namespace std;

static string get_sensor_name(sensor const& sensor) {
	auto label = sensor.label();
	auto name = sensor.name();
	if (label != "") {
		return label;
	}
	if (name != "") {
		return name;
	}
	return "Port " + to_string(sensor.id());
}

port::port(board const& board, sensor const& sensor, sensor_policy policy) :
	_sensor(sensor),
	_power(make_shared<SensorFunction<double>>(get_sensor_name(sensor) + " Power", SensorAttributes{
		.device_class = "power",
		.state_class = "measurement",
		.unit_of_measurement = "W",
		.suggested_display_precision = 4
		}, policy)),
	_current(make_shared<SensorFunction<double>>(get_sensor_name(sensor) + " Current", SensorAttributes{
		.device_class = "current",
		.state_class = "measurement",
		.unit_of_measurement = "A",
		.suggested_display_precision = 4
		})),
	_voltage(make_shared<SensorFunction<double>>(get_sensor_name(sensor) + " Voltage", SensorAttributes{
		.device_class = "voltage",
		.state_class = "measurement",
		.unit_of_measurement = "V",
		.suggested_display_precision = 4
		})),
	_relay(make_shared<SwitchFunction>(get_sensor_name(sensor) + " Relay", [this](auto v) { this->relay(v); })) {
}

void port::init(shared_ptr<DeviceBase> const& device) {
	device->registerFunction(_power);
	device->registerFunction(_current);
	device->registerFunction(_voltage);
	device->registerFunction(_relay);
}

void port::update() {
	auto poll = [&](auto getter, auto const& function) {
		try {
			auto result = (_sensor.*getter)();
			if (auto value = get_if<double>(&result)) {
				function->update(*value);
			}
			else {
				function->invalidate(mfi::describe(get<sensor_read_error>(result)));
			}
		}
		catch (std::exception const& error) {
			function->invalidate(error.what());
		}
	};
	poll(&sensor::power_checked, _power);
	poll(&sensor::current_checked, _current);
	poll(&sensor::voltage_checked, _voltage);
	try {
		_relay->update(_sensor.relay());
	}
	catch (std::exception const& error) {
		spdlog::error("Port {} relay update failed: {}", _sensor.id(), error.what());
	}
}

void port::relay(bool value) {
	_sensor.relay(value);
}

json port::migration_map() const {
	auto role = [](FunctionBase const& function) {
		auto discovery = function.getDiscoveryJson();
		json result{{"unique_id", function.getId()},
			{"discovery_topic", function.getDiscoveryTopic()}, {"state_topic", discovery.at("state_topic")}};
		if (discovery.contains("command_topic")) {
			result["command_topic"] = discovery.at("command_topic");
		}
		return result;
	};
	return {{"id", _sensor.id()}, {"roles", {
		{"power", role(*_power)}, {"current", role(*_current)},
		{"voltage", role(*_voltage)}, {"relay", role(*_relay)}}}};
}