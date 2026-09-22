#include "mfi_mqtt_client/native_device.h"
#include "mfi_mqtt_client/identity.h"
#include <cmath>
#include <limits>
#include <set>

namespace mfi_mqtt_client {
namespace {
	json error_result(char const* reason) {
		return {{"status", "error"}, {"reason", reason}};
	}

	json read_error(mfi::sensor_read_error error) {
		switch (error) {
		case mfi::sensor_read_error::open_failed: return error_result("open_failed");
		case mfi::sensor_read_error::read_failed: return error_result("read_failed");
		case mfi::sensor_read_error::invalid_number:
		case mfi::sensor_read_error::nonfinite: return error_result("invalid_data");
		}
		return error_result("invalid_data");
	}

	json measurement(mfi::sensor_read_result const& result, double maximum) {
		if (auto error = std::get_if<mfi::sensor_read_error>(&result)) {
			return read_error(*error);
		}
		auto value = std::get<double>(result);
		if (!std::isfinite(value) || value < 0 || value > maximum) {
			return error_result("invalid_data");
		}
		return {{"status", "ok"}, {"value", std::round(value * 10000) / 10000}};
	}

	json relay_result(mfi::relay_read_result const& result) {
		if (auto error = std::get_if<mfi::sensor_read_error>(&result)) {
			return read_error(*error);
		}
		return {{"status", "ok"}, {"value", std::get<bool>(result) ? "ON" : "OFF"}};
	}

	void check_text(std::string const& text) {
		if (text.empty() || text.size() > 128) {
			throw std::invalid_argument("Native descriptor names and versions must contain 1-128 UTF-8 bytes");
		}
	}
}

native_device::native_device(mfi::board const& board, std::string const& server, int port,
	std::string const& username, std::string const& password, sensor_policy policy,
	std::string const& device_id, std::string const& publisher_version) :
	device(board, server, port, username, password, policy, device_id),
	_device_id(device_id), _publisher_version(publisher_version) {
	if (!valid_uuid(device_id)) {
		throw std::invalid_argument("Invalid native device_id");
	}
	if (policy.refresh_interval.count() < 1 || policy.refresh_interval.count() > 86400
		|| policy.expire_after.count() > 259200
		|| policy.expire_after.count() / policy.refresh_interval.count() < 3) {
		throw std::invalid_argument("Invalid native refresh/expiration intervals");
	}
	for (auto const& sensor : board.sensors()) {
		if (sensor.id() < 1 || !_native_ports.emplace(sensor.id(), native_port{sensor}).second) {
			throw std::invalid_argument("Invalid or duplicate physical port ID");
		}
	}
	if (_native_ports.empty() || _native_ports.size() > 8) {
		throw std::invalid_argument("Native mode requires a supported one/eight-port board");
	}
	// Validate descriptor size and UTF-8 before contacting a broker.
	if (descriptor().dump().size() > 16384) {
		throw std::invalid_argument("Native descriptor exceeds 16 KiB");
	}
}

void native_device::init() {
	_connector->registerDevice(shared_from_this());
}

std::string native_device::base_topic() const { return "mfi/" + _device_id; }
std::string native_device::port_topic(int id) const { return base_topic() + "/port/" + std::to_string(id); }
std::string native_device::session_id() const { return _connector->sessionId(); }

json native_device::descriptor() const {
	check_text(_board.hostname());
	check_text(_board.name());
	check_text(_board.version());
	check_text(_publisher_version);
	json ports = json::array();
	for (auto const& [id, port] : _native_ports) {
		auto name = port.sensor.label().empty() ? port.sensor.name() : port.sensor.label();
		if (name.empty()) {
			name = "Port " + std::to_string(id);
		}
		check_text(name);
		ports.push_back({{"id", id}, {"name", name},
			{"capabilities", {"power", "current", "voltage", "relay"}}});
	}
	return {{"schema_version", 1}, {"device_id", _device_id}, {"name", _board.hostname()},
		{"manufacturer", "Ubiquiti Networks"}, {"model_id", std::to_string(_board.id())},
		{"model", _board.name()}, {"firmware_version", _board.version()},
		{"publisher_version", _publisher_version}, {"refresh_interval", _policy.refresh_interval.count()},
		{"expire_after", _policy.expire_after.count()}, {"ports", ports}};
}

std::vector<std::string> native_device::getSubscribeTopics() const {
	std::vector<std::string> topics;
	for (auto const& [id, port] : _native_ports) {
		topics.push_back(port_topic(id) + "/set");
	}
	return topics;
}

bool native_device::acceptsMessage(std::string const& topic) const {
	return topic.starts_with(base_topic() + "/");
}

void native_device::beginConnection(std::uint64_t) {
	_session = session_id();
	if (!valid_uuid(_session)) {
		throw std::runtime_error("Native connection has no valid session ID");
	}
	_discovery.reset();
	_command_fault.reset();
	for (auto& [id, port] : _native_ports) {
		port.published_roles = {};
		port.last_publish.reset();
		port.pending.reset();
		port.command.reset();
		port.sequence = 0;
	}
}

void native_device::sendDiscovery() {
	_discovery = publishMessage(base_topic() + "/config", descriptor(), 1, true);
	if (!_discovery->accepted()) {
		throw std::runtime_error("Native descriptor publication rejected");
	}
}

bool native_device::readyForOnline() const {
	if (!_discovery || publicationState(*_discovery) != publication_state::complete) {
		return false;
	}
	for (auto const& [id, port] : _native_ports) {
		if (!port.pending || publicationState(*port.pending) != publication_state::complete) {
			return false;
		}
	}
	return true;
}

void native_device::update() { update(clock::now()); }

void native_device::update(clock::time_point now) {
	if (!isConnected() || _session.empty()) {
		return;
	}
	for (auto& [id, port] : _native_ports) {
		sample(port, now);
	}
}

void native_device::service() {
	if (!isConnected()) {
		return;
	}
	for (auto& [id, port] : _native_ports) {
		if (port.command) {
			sample(port, clock::now());
		}
	}
}

void native_device::sample(native_port& port, clock::time_point now) {
	json roles{
		{"power", measurement(port.sensor.power_checked(), 1e9)},
		{"current", measurement(port.sensor.current_checked(), 1e6)},
		{"voltage", measurement(port.sensor.voltage_checked(), 1e6)},
		{"relay", relay_result(port.sensor.relay_checked())}
	};
	if (port.command) {
		if (port.command->write_failed) {
			roles["relay"] = error_result("write_failed");
		}
		else if (roles["relay"]["status"] == "ok"
			&& roles["relay"]["value"] != (port.command->desired ? "ON" : "OFF")) {
			roles["relay"] = error_result("state_mismatch");
		}
	}
	for (auto const& [role, result] : roles.items()) {
		if (!port.observed_roles.contains(role) || port.observed_roles[role] != result) {
			if (result["status"] == "error") {
				spdlog::warn("Native port {} {}: {}", port.sensor.id(), role, result["reason"].get<std::string>());
			}
			else if (port.observed_roles.contains(role) && port.observed_roles[role]["status"] == "error") {
				spdlog::info("Native port {} {} recovered", port.sensor.id(), role);
			}
		}
	}
	port.observed_roles = roles;
	if (port.pending && publicationState(*port.pending) == publication_state::pending) {
		return;
	}
	if (!port.command && port.last_publish && roles == port.published_roles
		&& now - *port.last_publish < _policy.refresh_interval) {
		return;
	}
	if (port.sequence == std::numeric_limits<std::int64_t>::max()) {
		_connector->abortConnection();
		throw std::runtime_error("Native sequence exhausted; a fresh connection is required");
	}
	auto payload = roles;
	payload["session_id"] = _session;
	payload["sequence"] = ++port.sequence;
	if (port.command) {
		payload["request_id"] = port.command->request_id;
	}
	auto sent = publishMessage(port_topic(port.sensor.id()) + "/state", payload, 0, false);
	if (!sent.accepted()) {
		throw std::runtime_error("Native state/confirmation publication rejected; no hardware command will be replayed");
	}
	port.pending = sent;
	port.command.reset();
	port.last_publish = now;
	port.published_roles = std::move(roles);
}

void native_device::reject_command(std::string const& reason) {
	if (_command_fault != reason) {
		spdlog::warn("Native command rejected: {}", reason);
		_command_fault = reason;
	}
}

void native_device::processMessage(std::string const& topic, std::string const& payload, bool retained) {
	if (retained || !isConnected() || _session.empty()) {
		reject_command("retained or disconnected");
		return;
	}
	if (payload.size() > messagePayloadLimit()) {
		reject_command("payload exceeds 512 bytes");
		return;
	}
	std::set<std::string> keys;
	bool duplicate = false;
	auto command = json::parse(payload, [&](int, json::parse_event_t event, json& value) {
		if (event == json::parse_event_t::key && !keys.insert(value.get<std::string>()).second) {
			duplicate = true;
		}
		return true;
	}, false);
	if (duplicate || !command.is_object() || command.size() != 3
		|| !command.contains("session_id") || !command["session_id"].is_string()
		|| !command.contains("request_id") || !command["request_id"].is_string()
		|| !command.contains("value") || !command["value"].is_string()
		|| command["session_id"] != _session
		|| !valid_uuid(command["request_id"].get<std::string>())
		|| (command["value"] != "ON" && command["value"] != "OFF")) {
		reject_command("invalid command or wrong session");
		return;
	}
	for (auto& [id, port] : _native_ports) {
		if (topic != port_topic(id) + "/set") {
			continue;
		}
		if (port.command) {
			reject_command("confirmation pending for this port");
			return;
		}
		bool desired = command["value"] == "ON";
		auto observed = port.sensor.relay_checked();
		auto value = std::get_if<bool>(&observed);
		bool failed = false;
		if (!value || *value != desired) {
			failed = port.sensor.relay_checked(desired).has_value();
		}
		port.command = confirmation{command["request_id"].get<std::string>(), desired, failed};
		_command_fault.reset();
		sample(port, clock::now());
		return;
	}
	reject_command("unknown physical port/topic");
}
}
