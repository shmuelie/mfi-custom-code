#include "mfi_mqtt_client/mfi_sensor.h"
#include <iostream>

using namespace std;
using namespace mfi;
using namespace mfi_mqtt_client;

string get_device_name(board const& board, sensor const& sensor) {
	auto label = sensor.label();
	auto name = sensor.name();

	if (label != "") {
		return label;
	}
	if (name != "") {
		return name;
	}
	return board.hostname() + "/" + to_string(sensor.id());
}

string get_device_id(board const& board, sensor const& sensor) {
	return board.hostname() + "/" + to_string(sensor.id());
}

#define TRY_REGISTER(FUNC) try {\
	registerFunction(FUNC);\
}\
catch(std::exception const& e) {\
	std::cout << "Error registering " << #FUNC << ": " << e.what() << std::endl;\
	throw;\
}

mfi_sensor::mfi_sensor(board const& board, sensor const& sensor) :
	DeviceBase(get_device_name(board, sensor), get_device_id(board, sensor)),
	_model(board.name()),
	_version(board.version()),
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
catch(std::exception const& e) {\
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

string mfi_sensor::getManufacturer() const {
	return "Ubiquiti Networks";
}

string mfi_sensor::getModel() const {
	return _model;
}

string mfi_sensor::getSoftwareVersion() const {
	return _version;
}