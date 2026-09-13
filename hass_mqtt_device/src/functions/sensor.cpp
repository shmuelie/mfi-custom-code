/**
 * @author      Morgan Tørvolt
 * @contributors somebody, hopefully@someday.com
 * @copyright   See LICENSE file
 */

#include "hass_mqtt_device/functions/sensor.h"
#include "hass_mqtt_device/logger/logger.hpp"
#include <cmath>
#include <limits>
#include <type_traits>

template class SensorFunction<int>;
template class SensorFunction<float>;
template class SensorFunction<double>;
template class SensorFunction<std::string>;
template class SensorFunction<bool>;

template<typename T>
SensorFunction<T>::SensorFunction(const std::string& name, const SensorAttributes& attributes,
	sensor_policy policy)
	: FunctionBase(name), m_policy(policy), m_attributes(attributes)
{
	if (policy.refresh_interval.count() < 0 || policy.expire_after.count() < 0
		|| (freshnessEnabled() && (policy.retain
			|| policy.expire_after.count() / policy.refresh_interval.count() < 3))
		|| (!freshnessEnabled() && policy.expire_after.count() != 0)) {
		throw std::invalid_argument("Sensor expiry must allow three non-retained refresh intervals");
	}
}

template<typename T>
void SensorFunction<T>::init() {}

template<typename T>
std::string SensorFunction<T>::getDiscoveryTopic() const
{
	auto parent = m_parent_device.lock();
	if (!parent) {
		throw std::runtime_error("Sensor parent is unavailable");
	}
	return "homeassistant/sensor/" + parent->getFullId() + "/" + getCleanName() + "/config";
}

template<typename T>
json SensorFunction<T>::getDiscoveryJson() const
{
	json discovery{
		{"name", getName()}, {"unique_id", getId()}, {"state_topic", getBaseTopic() + "state"},
		{"value_template", "{{ value_json.value }}"}, {"device_class", m_attributes.device_class},
		{"state_class", m_attributes.state_class}, {"unit_of_measurement", m_attributes.unit_of_measurement},
		{"suggested_display_precision", m_attributes.suggested_display_precision}
	};
	if (freshnessEnabled()) {
		discovery["expire_after"] = m_policy.expire_after.count();
	}
	return discovery;
}

template<typename T>
std::optional<std::string> SensorFunction<T>::availabilityTopic() const
{
	if (freshnessEnabled()) {
		return getBaseTopic() + "availability";
	}
	return std::nullopt;
}

template<typename T>
void SensorFunction<T>::sendStatus() const
{
	// Expiring measurements are never refreshed from cached status/reconnect paths.
	if (!m_has_data || freshnessEnabled()) {
		return;
	}
	if (auto parent = m_parent_device.lock()) {
		if (parent->publishMessage(getBaseTopic() + "state", {{"value", m_value}}, 0, m_policy.retain).accepted()) {
			m_published_value = m_value;
		}
	}
}

template<typename T>
void SensorFunction<T>::resetConnection(std::uint64_t)
{
	m_published_value.reset();
	if (!freshnessEnabled()) {
		return;
	}
	m_has_data = false;
	m_last_publish.reset();
	m_seen_poll = false;
	m_desired_health = false;
	m_acknowledged_health.reset();
	m_pending_health.reset();
}

template<typename T>
void SensorFunction<T>::service()
{
	if (!freshnessEnabled()) {
		return;
	}
	auto parent = m_parent_device.lock();
	if (!parent || !parent->isConnected()) {
		return;
	}
	if (m_pending_health) {
		auto state = parent->publicationState(*m_pending_health);
		if (state == publication_state::pending) {
			return;
		}
		if (state == publication_state::complete) {
			m_acknowledged_health = m_pending_health_value;
		}
		else {
			m_acknowledged_health.reset();
		}
		m_pending_health.reset();
	}
	if (!m_acknowledged_health || *m_acknowledged_health != m_desired_health) {
		auto message = parent->publishMessage(*availabilityTopic(),
			{{"availability", m_desired_health ? "online" : "offline"}}, 1, true);
		if (message.accepted()) {
			m_pending_health = message;
			m_pending_health_value = m_desired_health;
		}
	}
}

template<typename T>
bool SensorFunction<T>::readyForOnline() const
{
	return !freshnessEnabled() || (m_seen_poll && m_acknowledged_health
		&& !m_pending_health && *m_acknowledged_health == m_desired_health);
}

template<typename T>
void SensorFunction<T>::invalidate(std::string const& reason)
{
	if (!m_fault || *m_fault != reason) {
		LOG_ERROR("Sensor {} invalid: {}", getName(), reason);
		m_fault = reason;
	}
	m_seen_poll = true;
	m_has_data = false;
	m_desired_health = false;
	m_published_value.reset();
	m_last_publish.reset();
	service();
}

template<typename T>
bool SensorFunction<T>::update(T value)
{
	return update(std::move(value), clock::now());
}

template<typename T>
bool SensorFunction<T>::update(T value, clock::time_point now)
{
	if constexpr (std::is_arithmetic_v<T>) {
		if (m_policy.reject_negative && value < 0) {
			invalidate("negative power");
			return false;
		}
	}
	if constexpr (std::is_floating_point_v<T>) {
		if (!std::isfinite(value)) {
			invalidate("nonfinite measurement");
			return false;
		}
		if (m_attributes.suggested_display_precision >= 0) {
			const T factor = std::pow(static_cast<T>(10), m_attributes.suggested_display_precision);
			if (!std::isfinite(factor) || factor < 1
				|| std::abs(value) > std::numeric_limits<T>::max() / factor) {
				invalidate("quantization overflow");
				return false;
			}
			value = std::round(value * factor) / factor;
			if (!std::isfinite(value)) {
				invalidate("nonfinite quantized measurement");
				return false;
			}
		}
	}
	if (m_fault) {
		LOG_INFO("Sensor {} recovered", getName());
		m_fault.reset();
	}
	m_seen_poll = true;
	m_has_data = true;
	m_value = value;
	service();
	auto parent = m_parent_device.lock();
	if (!parent || !parent->isConnected()) {
		return true;
	}
	if (freshnessEnabled() && (!m_acknowledged_health
		|| (!m_desired_health && (m_pending_health || *m_acknowledged_health)))) {
		// A pre-handshake read is not the initial publication attempt for this epoch.
		m_seen_poll = false;
		return true;
	}
	bool changed = !m_published_value || *m_published_value != value;
	bool due = freshnessEnabled() && (!m_last_publish || now - *m_last_publish >= m_policy.refresh_interval);
	if (changed || due) {
		auto message = parent->publishMessage(getBaseTopic() + "state", {{"value", value}}, 0, m_policy.retain);
		if (message.accepted()) {
			m_published_value = value;
			m_last_publish = now;
			if (freshnessEnabled()) {
				m_desired_health = true;
				service();
			}
		}
	}
	return true;
}
