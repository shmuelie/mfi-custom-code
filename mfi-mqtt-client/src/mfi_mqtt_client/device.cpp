#include "mfi_mqtt_client/device.h"

using namespace mfi;
using namespace mfi_mqtt_client;
using namespace std;

device::device(
	board const& board,
	string const& server,
	int port,
	string const& username,
	string const& password) :
	DeviceBase(board.hostname(), board.hostname()),
	_ports(),
	_board(board),
	_connector(make_shared<MQTTConnector>(server, port, username, password, board.hostname())) {
}

void device::init() {
	auto self = this->shared_from_this();

	for (auto& sensor : _board.sensors()) {
		auto mfiSensor = make_shared<port>(_board, sensor);
		_ports.push_back(mfiSensor);
		mfiSensor->init(self);
	}

	_connector->registerDevice(self);
}

void device::update() {
	for (auto& port : _ports) {
		port->update();
	}
}

optional<string> device::getManufacturer() const {
	return "Ubiquiti Networks";
}

optional<string> device::getModel() const {
	return _board.name();
}

optional<string> device::getSoftwareVersion() const {
	return _board.version();
}

optional<string> device::getModelId() const {
	return to_string(_board.id());
}

optional<string> device::getConfigurationUrl() const {
	return "http://" + _board.hostname();
}

bool device::connect() {
	return _connector->connect();
}

void device::processMessages(int timeout) {
	_connector->processMessages(timeout);
}