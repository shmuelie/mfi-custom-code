#include "mfi_mqtt_client/mfi_sensor.h"
#include <iostream>

using namespace std;
using namespace mfi;
using namespace mfi_mqtt_client;

string get_device_name(board const& board, sensor const& sensor) {
	auto hostname = board.hostname();
	auto label = sensor.label();
	auto name = sensor.name();
	auto id = sensor.id();

	if (label != "") {
		return label;
	}
	if (name != "") {
		return name;
	}
	return hostname + "/" + to_string(id);
}

string get_device_id(sensor const& sensor) {
	auto name = sensor.name();
	auto id = sensor.id();

	if (name != "") {
		return name;
	}
	return to_string(id);
}

#define TRY_REGISTER(FUNC) try {\
	registerFunction(FUNC);\
}\
catch(std::exception& e) {\
	std::cout << "Error registering " << #FUNC << ": " << e.what() << std::endl;\
	throw;\
}

mfi_sensor::mfi_sensor(board const& board, sensor const& sensor) :
	DeviceBase(get_device_name(board, sensor), get_device_id(sensor)),
	_sensor(sensor),
	_power(make_shared<functions::power>()),
	_current(make_shared<functions::current>()),
	_voltage(make_shared<functions::voltage>()),
	_relay(make_shared<SwitchFunction>("relay", [this](auto v) { this->relay(v); })) {
}

void mfi_sensor::init() {
	TRY_REGISTER(_power);
	TRY_REGISTER(_current);
	TRY_REGISTER(_voltage);
	TRY_REGISTER(_relay);
}

#define TRY_UPDATE(TYPE) try {\
	_##TYPE->update(_sensor.TYPE());\
}\
catch(std::exception& e) {\
	std::cout << "Error updating " << #TYPE << ": " << e.what() << std::endl;\
	throw;\
}

void mfi_sensor::update() {
	TRY_UPDATE(power);
	TRY_UPDATE(current);
	TRY_UPDATE(voltage);
	TRY_UPDATE(relay);
}

void mfi_sensor::relay(bool value) {
	_sensor.relay(value);
}