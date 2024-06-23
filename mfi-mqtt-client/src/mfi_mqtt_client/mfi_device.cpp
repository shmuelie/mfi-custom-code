#include "mfi_mqtt_client/mfi_device.h"

using namespace mfi;
using namespace mfi_mqtt_client;
using namespace std;

mfi_device::mfi_device(const board& board) noexcept :
	DeviceBase(board.hostname(), board.hostname()),
	_board(board),
	_sensors() {
	for (auto& sensor : board.sensors()) {
		_sensors.push_back(mfi_sensor{ board, sensor });
	}
}

void mfi_device::update() noexcept {
	for (auto& sensor : _sensors) {
		sensor.update();
	}
}