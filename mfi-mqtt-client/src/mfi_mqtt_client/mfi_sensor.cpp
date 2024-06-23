#include "mfi_mqtt_client/mfi_sensor.h"

using namespace std;
using namespace mfi;
using namespace mfi_mqtt_client;

string get_device_name(const board& board, const sensor& sensor) noexcept {
	auto hostname = board.hostname();
	auto label = sensor.label();
	auto name = sensor.name();
	auto id = sensor.id();

	string deviceName{ hostname };
	deviceName += "/";
	if (label != "") {
		deviceName += label;
	}
	else if (name != "") {
		deviceName += name;
	}
	else {
		deviceName += id;
	}

	return deviceName;
}

string get_device_id(const board& board, const sensor& sensor) noexcept {
	auto hostname = board.hostname();
	auto name = sensor.name();
	auto id = sensor.id();

	string deviceId{ hostname };
	deviceId += "/";
	if (name != "") {
		deviceId += name;
	}
	else {
		deviceId += id;
	}

	return deviceId;
}

mfi_sensor::mfi_sensor(const board& board, const sensor& sensor) noexcept :
	DeviceBase(get_device_name(board, sensor), get_device_id(board, sensor)),
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

void mfi_sensor::update() noexcept {
	_power->update(_sensor.power());
	_current->update(_sensor.current());
	_voltage->update(_sensor.voltage());
	_relay->update(_sensor.relay());
}

void mfi_sensor::relay(bool value) noexcept {
	_sensor.relay(value);
}