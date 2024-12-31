#include "mfi_mqtt_client/mfi_sensor.h"
#include <iostream>
#include <hass_mqtt_device/logger/logger.hpp>

using namespace std;
using namespace mfi;
using namespace mfi_mqtt_client;

string get_device_name(sensor const& sensor) {
	auto label = sensor.label();
	auto name = sensor.name();

	if (label != "") {
		return label;
	}
	if (name != "") {
		return name;
	}
	return to_string(sensor.id());
}

#define TRY_REGISTER(FUNC) try {\
	registerFunction(FUNC);\
}\
catch(std::exception const& e) {\
	LOG_ERROR("Error registering {}: {}", #FUNC, e.what());\
	throw;\
}

mfi_sensor::mfi_sensor(board const& board, sensor const& sensor) :
	DeviceBase(get_device_name(sensor), to_string(sensor.id())),
	_model(board.name()),
	_modelId(to_string(board.id())),
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
	LOG_ERROR("Error updating {}: {}", #TYPE, e.what());\
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

string mfi_sensor::getModelId() const {
	return _modelId;
}