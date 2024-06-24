#include "mfi_mqtt_client/mfi_sensor.h"

using namespace std;
using namespace mfi;
using namespace mfi_mqtt_client;

string get_device_name(const sensor& sensor) noexcept {
	auto label = sensor.label();
	auto name = sensor.name();
	auto id = sensor.id();

	if (label != "") {
		return label;
	}
	if (name != "") {
		return name;
	}
	return to_string(id);
}

string get_device_id(const sensor& sensor) noexcept {
	auto name = sensor.name();
	auto id = sensor.id();

	if (name != "") {
		return name;
	}
	return to_string(id);
}

mfi_sensor::mfi_sensor(const sensor& sensor) :
	DeviceBase(get_device_name(sensor), get_device_id(sensor)),
	_sensor(sensor),
	_power(make_shared<functions::power>()),
	_current(make_shared<functions::current>()),
	_voltage(make_shared<functions::voltage>()),
	_relay(make_shared<SwitchFunction>("relay", [this](auto v) { this->relay(v); })) {

	registerFunction(_power);
	registerFunction(_current);
	registerFunction(_voltage);
	registerFunction(_relay);
}

void mfi_sensor::update() {
	_power->update(_sensor.power());
	_current->update(_sensor.current());
	_voltage->update(_sensor.voltage());
	_relay->update(_sensor.relay());
}

void mfi_sensor::relay(bool value) noexcept {
	_sensor.relay(value);
}