#include "mfi_mqtt_client/mfi_device.h"

using namespace mfi;
using namespace mfi_mqtt_client;
using namespace std;

mfi_device::mfi_device(
	const board& board,
	const string& server,
	int port,
	const string& username,
	const string& password) noexcept :
	MQTTConnector(server, port, username, password, board.hostname()),
	_board(board),
	_sensors() {
	for (auto& sensor : board.sensors()) {
		auto mfiSensor = make_shared<mfi_sensor>(sensor);
		_sensors.push_back(mfiSensor);
		registerDevice(mfiSensor);
	}
}

void mfi_device::update() noexcept {
	for (auto& sensor : _sensors) {
		sensor->update();
	}
}