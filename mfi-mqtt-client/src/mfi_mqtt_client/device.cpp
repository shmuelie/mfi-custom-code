#include "mfi_mqtt_client/device.h"
#include "mfi_mqtt_client/identity.h"

using namespace mfi;
using namespace mfi_mqtt_client;
using namespace std;

device::device(
	board const& board,
	string const& server,
	int port,
	string const& username,
	string const& password,
	sensor_policy policy, string const& native_id) :
	DeviceBase(board.hostname(), board.hostname()),
	_connector(make_shared<MQTTConnector>(server, port, username, password,
		native_id.empty() ? board.hostname() : native_id,
		native_id.empty() ? mqtt_session_options{} :
			mqtt_session_options{"mfi/" + native_id + "/availability", random_uuid})),
	_board(board),
	_policy(policy) {
}

void device::init() {
	auto self = this->shared_from_this();

	_connector->registerDevice(self);

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

json device::migration_map(string const& native_id) const {
	if (!valid_uuid(native_id) || _ports.empty() || _ports.size() != _board.sensors().size()) {
		throw invalid_argument("Migration export requires an initialized supported legacy device and valid native device_id");
	}
	json ports = json::array();
	for (auto const& port : _ports) {
		ports.push_back(port->migration_map());
	}
	return {{"schema_version", 1}, {"device_id", native_id},
		{"legacy_device_id", getId()}, {"legacy_full_id", getFullId()},
		{"legacy_availability_topic", _connector->getAvailabilityTopic()}, {"ports", ports}};
}