#include "mfi_mqtt_client/mfi_device.h"

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
	for (auto& sensor : board.sensors()) {
		auto mfiSensor = make_shared<mfi_sensor>(sensor);
		_sensors.push_back(mfiSensor);
		registerDevice(mfiSensor);
	}
	_light = make_shared<OnOffLightDevice>("LED", [this](auto v) { this->led().color(v ? led_color::blue : led_color::off); }, "LED");
	registerDevice(_light);
}

void mfi_device::update() {
	for (auto& sensor : _sensors) {
		sensor->update();
	}
	_light->update(_led.color() != led_color::off);
}

led mfi_device::led() const noexcept {
	return _led;
}