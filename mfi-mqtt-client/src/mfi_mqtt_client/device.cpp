#include "mfi_mqtt_client/device.h"

using namespace mfi;
using namespace mfi_mqtt_client;
using namespace std;

device::device(
	board const& board,
	string const& server,
	int port,
	string const& username,
	string const& password,
	sensor_policy policy,
	system_metrics_options const& system_options) :
	DeviceBase(board.hostname(), board.hostname()),
	_ports(),
	_board(board),
	_connector(make_shared<MQTTConnector>(server, port, username, password, board.hostname())),
	_policy(policy) {
	system_options.validate();
	if (system_options.enabled) {
		_system_metrics = make_unique<system_metrics>(system_options);
	}
}

void device::init() {
	auto self = this->shared_from_this();

	_connector->registerDevice(self);

	if (_system_metrics) {
		_system_metrics->init(self);
	}

	for (auto& sensor : _board.sensors()) {
		auto mfiSensor = make_shared<port>(_board, sensor, _policy);
		_ports.push_back(mfiSensor);
		mfiSensor->init(self);
	}
}

void device::update() {
	for (auto& port : _ports) {
		port->update();
	}
}

void device::update_system() {
	if (_system_metrics) {
		_system_metrics->update();
	}
}

void device::beginConnection(std::uint64_t epoch) {
	DeviceBase::beginConnection(epoch);
	if (_system_metrics) {
		_system_metrics->reset_connection();
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

bool device::shutdown() {
	return _connector->shutdown(std::chrono::seconds(5));
}

void device::processMessages(int timeout) {
	_connector->processMessages(timeout);
}