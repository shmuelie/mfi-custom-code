#include "mfi_mqtt_client/mfi_device.h"
#include <iostream>
#include <hass_mqtt_device/logger/logger.hpp>

using namespace mfi;
using namespace mfi_mqtt_client;
using namespace std;

mfi_device::mfi_device(
	board const& board,
	string const& server,
	int port,
	string const& username,
	string const& password) :
	MQTTConnector(server, port, username, password, board.hostname()),
	_board(board),
	_sensors(),
	_led(),
	_light() {
	_light = make_shared<OnOffLightDevice>("LED", [this](auto v) { this->led().color(v ? led_color::blue : led_color::off); }, "LED");
}

void mfi_device::init() {
	for (auto& sensor : _board.sensors()) {
		auto mfiSensor = make_shared<mfi_sensor>(_board, sensor);
		mfiSensor->init();
		_sensors.push_back(mfiSensor);
		try {
			registerDevice(mfiSensor);
		}
		catch (std::exception const& e) {
			LOG_ERROR("Error registering sensor {}: {}", to_string(sensor.id()), e.what());
		}
	}
	try {
		registerDevice(_light);
	}
	catch (std::exception const& e) {
		LOG_ERROR("Error registering led: {}", e.what());
	}
}

void mfi_device::update() {
	for (auto& sensor : _sensors) {
		sensor->update();
	}
	try {
		_light->update(_led.color() != led_color::off);
	}
	catch (std::exception const& e) {
		LOG_ERROR("Error updating led: {}", e.what());
	}
}

led mfi_device::led() const noexcept {
	return _led;
}