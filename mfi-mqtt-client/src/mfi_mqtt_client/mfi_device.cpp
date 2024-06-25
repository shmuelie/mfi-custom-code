#include "mfi_mqtt_client/mfi_device.h"
#include <iostream>

using namespace mfi;
using namespace mfi_mqtt_client;
using namespace std;

mfi_device::mfi_device(
	const board& board,
	const string& server,
	int port,
	const string& username,
	const string& password) :
	MQTTConnector(server, port, username, password, board.hostname()),
	_board(board),
	_sensors(),
	_led(),
	_light() {
	_light = make_shared<OnOffLightDevice>("LED", [this](auto v) { this->led().color(v ? led_color::blue : led_color::off); }, "LED");
}

void mfi_device::init() {
	for (auto& sensor : _board.sensors()) {
		auto mfiSensor = make_shared<mfi_sensor>(sensor);
		mfiSensor->init();
		_sensors.push_back(mfiSensor);
		try {
			registerDevice(mfiSensor);
		}
		catch (std::exception& e) {
			std::cout << "Error registering sensor " << sensor.id() << ": " << e.what() << std::endl;
		}
	}
	try {
		registerDevice(_light);
	}
	catch (std::exception& e) {
		std::cout << "Error registering led: " << e.what() << std::endl;
	}
}

void mfi_device::update() {
	for (auto& sensor : _sensors) {
		sensor->update();
	}
	try {
		_light->update(_led.color() != led_color::off);
	}
	catch (std::exception& e) {
		std::cout << "Error updating led: " << e.what() << std::endl;
	}
}

led mfi_device::led() const noexcept {
	return _led;
}