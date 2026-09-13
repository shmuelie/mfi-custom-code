#include "hass_mqtt_device/core/mqtt_connector.h"
#include "hass_mqtt_device/core/device_base.h"
#include "hass_mqtt_device/core/helper_functions.hpp"
#include "hass_mqtt_device/logger/logger.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <thread>

namespace {
constexpr std::array<int, 8> backoff_ladder = {1000, 1000, 5000, 5000, 5000, 15000, 30000, 30000};
constexpr auto delivery_deadline = std::chrono::seconds(5);
constexpr std::size_t pending_limit = 256;
}

MQTTConnector::MQTTConnector(const std::string& server, int port,
	const std::string& username, const std::string& password, const std::string& unique_id)
	: m_server(server), m_port(port), m_username(username), m_password(password),
	  m_unique_id(getValidHassString(unique_id)), m_mosquitto(nullptr),
	  m_logger(spdlog::default_logger()), m_backoff_state(0), m_slept_for(0)
{
	if (int rc = mosquitto_lib_init(); rc != MOSQ_ERR_SUCCESS) {
		throw std::runtime_error(mosquitto_strerror(rc));
	}
}

MQTTConnector::~MQTTConnector()
{
	try {
		shutdown();
	}
	catch (std::exception const& error) {
		LOG_ERROR("MQTT shutdown failed: {}", error.what());
		abortConnection();
	}
	mosquitto_lib_cleanup();
}

std::string MQTTConnector::getAvailabilityTopic() const
{
	return "home/" + getId() + "/availability";
}

std::vector<std::shared_ptr<DeviceBase>> MQTTConnector::devices() const
{
	std::vector<std::shared_ptr<DeviceBase>> result;
	for (auto const& entry : m_registered_devices) {
		if (auto device = entry.lock()) {
			result.push_back(std::move(device));
		}
	}
	return result;
}

bool MQTTConnector::connect()
{
	abortConnection();
	m_stopping = false;
	m_mosquitto = mosquitto_new(m_unique_id.c_str(), true, this);
	if (!m_mosquitto) {
		LOG_ERROR("Failed to create MQTT client");
		return false;
	}
	int rc = mosquitto_username_pw_set(m_mosquitto, m_username.c_str(), m_password.c_str());
	if (rc != MOSQ_ERR_SUCCESS) {
		LOG_ERROR("Failed to configure MQTT credentials: {}", mosquitto_strerror(rc));
		abortConnection();
		return false;
	}
	mosquitto_connect_callback_set(m_mosquitto, connectCallback);
	mosquitto_disconnect_callback_set(m_mosquitto, disconnectCallback);
	mosquitto_message_callback_set(m_mosquitto, messageCallback);
	mosquitto_publish_callback_set(m_mosquitto, publishCallback);
	mosquitto_subscribe_callback_set(m_mosquitto, subscribeCallback);
	mosquitto_log_callback_set(m_mosquitto, logCallback);
	if (!publishLWT()) {
		abortConnection();
		return false;
	}
	m_connect_started = std::chrono::steady_clock::now();
	rc = mosquitto_connect_async(m_mosquitto, m_server.c_str(), m_port, 60);
	if (rc != MOSQ_ERR_SUCCESS) {
		LOG_ERROR("MQTT connection failed: {}", mosquitto_strerror(rc));
		abortConnection();
		return false;
	}
	m_connecting = true;
	return true;
}

void MQTTConnector::abortConnection()
{
	// Do not send DISCONNECT here: failures must preserve the broker's Last Will.
	if (m_mosquitto) {
		mosquitto_destroy(m_mosquitto);
		m_mosquitto = nullptr;
	}
	m_is_connected = false;
	m_connecting = false;
	m_just_connected = false;
	++m_epoch;
	m_pending.clear();
	m_offline.reset();
	m_online.reset();
}

bool MQTTConnector::shutdown(std::chrono::milliseconds timeout)
{
	m_stopping = true;
	if (!isConnected()) {
		abortConnection();
		return true;
	}
	auto message = publishMessage(getAvailabilityTopic(), {{"availability", "offline"}});
	auto deadline = std::chrono::steady_clock::now() + timeout;
	while (message.accepted() && publicationState(message) == publication_state::pending
		&& std::chrono::steady_clock::now() < deadline) {
		auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
			deadline - std::chrono::steady_clock::now());
		int rc = mosquitto_loop(m_mosquitto, static_cast<int>(
			std::clamp<std::int64_t>(remaining.count(), 0, 50)), 1);
		if (rc != MOSQ_ERR_SUCCESS) {
			LOG_ERROR("MQTT offline flush failed: {}", mosquitto_strerror(rc));
			break;
		}
	}
	bool acknowledged = message.accepted()
		&& publicationState(message) == publication_state::complete;
	if (acknowledged) {
		int rc = mosquitto_disconnect(m_mosquitto);
		if (rc == MOSQ_ERR_SUCCESS) {
			rc = mosquitto_loop_write(m_mosquitto, 1);
		}
		if (rc != MOSQ_ERR_SUCCESS && rc != MOSQ_ERR_NO_CONN) {
			LOG_ERROR("MQTT disconnect failed: {}", mosquitto_strerror(rc));
		}
	}
	else {
		LOG_ERROR("MQTT offline was not acknowledged before shutdown; preserving Last Will");
	}
	abortConnection();
	return acknowledged;
}

void MQTTConnector::disconnect() { shutdown(); }
bool MQTTConnector::isConnected() const { return m_is_connected; }

void MQTTConnector::registerDevice(std::shared_ptr<DeviceBase> device)
{
	for (auto const& existing : devices()) {
		if (existing->getCleanName() == device->getCleanName() && existing->getId() == device->getId()) {
			throw std::runtime_error("Device with name already registered");
		}
	}
	device->setParentConnector(shared_from_this());
	m_registered_devices.push_back(device);
	if (isConnected()) {
		shutdown();
		connect();
	}
}

void MQTTConnector::unregisterDevice(const std::string& name)
{
	std::erase_if(m_registered_devices, [&](auto const& entry) {
		auto device = entry.lock();
		return !device || device->getId() == name;
	});
}

std::shared_ptr<DeviceBase> MQTTConnector::getDevice(const std::string& name) const
{
	for (auto const& device : devices()) {
		if (device->getName() == name || device->getCleanName() == name) {
			return device;
		}
	}
	return nullptr;
}

void MQTTConnector::beginSession()
{
	m_just_connected = false;
	m_backoff_state = 0;
	m_slept_for = 0;
	m_offline = publishMessage(getAvailabilityTopic(), {{"availability", "offline"}});
	if (!m_offline->accepted()) {
		throw std::runtime_error("Initial MQTT offline publication rejected");
	}
	for (auto const& device : devices()) {
		device->beginConnection(m_epoch);
		for (auto const& topic : device->getSubscribeTopics()) {
			int rc = mosquitto_subscribe(m_mosquitto, nullptr, topic.c_str(), 0);
			if (rc != MOSQ_ERR_SUCCESS) {
				throw std::runtime_error(mosquitto_strerror(rc));
			}
		}
		device->sendDiscovery();
		device->sendStatus();
	}
}

void MQTTConnector::serviceSession()
{
	auto now = std::chrono::steady_clock::now();
	for (auto const& [id, pending] : m_pending) {
		if (now - pending.sent >= delivery_deadline) {
			LOG_ERROR("MQTT delivery stalled on {}; dropping old connection and queued state", pending.topic);
			abortConnection();
			return;
		}
	}
	bool ready = true;
	for (auto const& device : devices()) {
		device->service();
		ready = device->readyForOnline() && ready;
	}
	if (ready && !m_online && m_offline
		&& publicationState(*m_offline) == publication_state::complete) {
		auto online = publishMessage(getAvailabilityTopic(), {{"availability", "online"}});
		if (online.accepted()) {
			m_online = online;
		}
	}
}

void MQTTConnector::processMessages(int timeout, bool exit_on_event)
{
	if (timeout < 0) {
		throw std::invalid_argument("MQTT timeout must not be negative");
	}
	if (m_stopping) {
		return;
	}
	// Keep individual network waits short even when callers use a long poll period.
	timeout = std::min(timeout, 1000);
	if (!isConnected() && !m_connecting) {
		std::this_thread::sleep_for(std::chrono::milliseconds(timeout));
		m_slept_for += timeout;
		if (m_slept_for < backoff_ladder[m_backoff_state]) {
			return;
		}
		m_slept_for = 0;
		m_backoff_state = std::min(m_backoff_state + 1, static_cast<int>(backoff_ladder.size()) - 1);
		if (!connect()) {
			return;
		}
	}
	auto done = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
	do {
		int remaining = static_cast<int>(std::max<std::int64_t>(0,
			std::chrono::duration_cast<std::chrono::milliseconds>(
				done - std::chrono::steady_clock::now()).count()));
		int rc = mosquitto_loop(m_mosquitto, std::min(remaining, 100), 1);
		if (rc != MOSQ_ERR_SUCCESS) {
			LOG_ERROR("MQTT processing failed: {}", mosquitto_strerror(rc));
			abortConnection();
			return;
		}
		if (m_connecting && std::chrono::steady_clock::now() - m_connect_started >= delivery_deadline) {
			LOG_ERROR("MQTT CONNACK deadline exceeded");
			abortConnection();
			return;
		}
		try {
			if (m_just_connected) {
				beginSession();
			}
			if (isConnected()) {
				serviceSession();
			}
		}
		catch (std::exception const& error) {
			LOG_ERROR("MQTT session setup/service failed: {}", error.what());
			abortConnection();
			return;
		}
		if (!isConnected() && !m_connecting) {
			abortConnection();
			return;
		}
		if (exit_on_event) {
			break;
		}
	} while (std::chrono::steady_clock::now() < done);
}

publication MQTTConnector::publishMessage(const std::string& topic, const json& payload, int qos, bool retain)
{
	if (!m_mosquitto || !isConnected()) {
		LOG_ERROR("Cannot publish {}: MQTT is disconnected", topic);
		return {};
	}
	if (m_pending.size() >= pending_limit
		|| (qos == 0 && std::any_of(m_pending.begin(), m_pending.end(),
			[&](auto const& entry) { return entry.second.qos == 0 && entry.second.topic == topic; }))) {
		LOG_WARN("MQTT publication backpressure on {}", topic);
		return {MOSQ_ERR_NOMEM, m_epoch, 0};
	}
	std::string body = payload.dump();
	if (body.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
		LOG_ERROR("MQTT payload exceeds transport limit on {}", topic);
		return {MOSQ_ERR_PAYLOAD_SIZE, m_epoch, 0};
	}
	int id = 0;
	m_publishing = true;
	m_early_completion.reset();
	int rc = mosquitto_publish(m_mosquitto, &id, topic.c_str(),
		static_cast<int>(body.size()), body.data(), qos, retain);
	m_publishing = false;
	if (rc != MOSQ_ERR_SUCCESS) {
		LOG_ERROR("MQTT publication failed on {}: {}", topic, mosquitto_strerror(rc));
		return {rc, m_epoch, 0};
	}
	if (m_pending.contains(id)) {
		LOG_ERROR("MQTT reused an outstanding packet identifier; dropping ambiguous connection");
		abortConnection();
		return {MOSQ_ERR_PROTOCOL, m_epoch, 0};
	}
	auto sequence = ++m_sequence;
	if (!m_early_completion || *m_early_completion != id) {
		m_pending.emplace(id, pending_publication{sequence, std::chrono::steady_clock::now(), topic, qos});
	}
	return {rc, m_epoch, sequence};
}

publication_state MQTTConnector::publicationState(publication const& message) const
{
	if (!message.accepted() || message.epoch != m_epoch || !isConnected()) {
		return publication_state::failed;
	}
	for (auto const& [id, pending] : m_pending) {
		if (pending.sequence == message.sequence) {
			return publication_state::pending;
		}
	}
	return publication_state::complete;
}

bool MQTTConnector::publishLWT()
{
	std::string body = R"({"availability":"offline"})";
	int rc = mosquitto_will_set(m_mosquitto, getAvailabilityTopic().c_str(),
		static_cast<int>(body.size()), body.data(), 1, true);
	if (rc != MOSQ_ERR_SUCCESS) {
		LOG_ERROR("Failed to set MQTT Last Will: {}", mosquitto_strerror(rc));
	}
	return rc == MOSQ_ERR_SUCCESS;
}

void MQTTConnector::connectCallback(mosquitto*, void* obj, int rc)
{
	auto& self = *static_cast<MQTTConnector*>(obj);
	self.m_connecting = false;
	self.m_is_connected = rc == 0;
	self.m_just_connected = rc == 0;
	if (rc != 0) {
		self.LOG_ERROR("MQTT connection refused: {}", mosquitto_connack_string(rc));
	}
}

void MQTTConnector::disconnectCallback(mosquitto*, void* obj, int rc)
{
	auto& self = *static_cast<MQTTConnector*>(obj);
	self.m_is_connected = false;
	self.m_connecting = false;
	self.LOG_INFO("MQTT disconnected: {}", mosquitto_strerror(rc));
}

void MQTTConnector::publishCallback(mosquitto*, void* obj, int id)
{
	auto& self = *static_cast<MQTTConnector*>(obj);
	if (self.m_publishing) {
		self.m_early_completion = id;
		// A local QoS 0 write completion is not a PUBACK for a reused QoS 1 id.
		auto pending = self.m_pending.find(id);
		if (pending != self.m_pending.end() && pending->second.qos != 0) {
			return;
		}
	}
	self.m_pending.erase(id);
}

void MQTTConnector::messageCallback(mosquitto*, void* obj, const mosquitto_message* message)
{
	auto& self = *static_cast<MQTTConnector*>(obj);
	if (self.m_stopping) {
		return;
	}
	try {
		std::string topic(message->topic);
		std::string payload;
		if (message->payload && message->payloadlen > 0) {
			payload.assign(static_cast<char const*>(message->payload), message->payloadlen);
		}
		for (auto const& device : self.devices()) {
			if (topic.starts_with("home/" + device->getFullId() + "/")) {
				device->processMessage(topic, payload);
			}
		}
	}
	catch (std::exception const& error) {
		self.LOG_ERROR("MQTT command failed: {}", error.what());
	}
}

void MQTTConnector::subscribeCallback(mosquitto*, void* obj, int, int count, const int* qos)
{
	auto& self = *static_cast<MQTTConnector*>(obj);
	for (int i = 0; i < count; ++i) {
		if (qos[i] == 128) {
			self.LOG_ERROR("MQTT subscription refused");
			self.m_is_connected = false;
		}
	}
}

void MQTTConnector::unsubscribeCallback(mosquitto*, void*, int) {}

void MQTTConnector::logCallback(mosquitto*, void* obj, int level, const char* message)
{
	auto& self = *static_cast<MQTTConnector*>(obj);
	if (level == MOSQ_LOG_ERR) {
		self.LOG_ERROR("Mosquitto: {}", message);
	}
	else if (level == MOSQ_LOG_WARNING) {
		self.LOG_WARN("Mosquitto: {}", message);
	}
}
