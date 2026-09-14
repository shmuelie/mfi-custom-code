/**
 * @author      Morgan Tørvolt
 * @contributors somebody, hopefully@someday.com
 * @copyright   See LICENSE file
 */

#pragma once

#include "hass_mqtt_device/core/device_base.h"
#include "hass_mqtt_device/core/function_base.h"
#include <functional>
#include <memory>
#include <chrono>
#include <optional>
#include <stdexcept>

/**
 * @brief Struct that holds the attributes of a sensor
 *
 * This allows the user to add several types of sensors to one device using only one sensor object type.
 *
 * Example usage:
 * @code{.cpp}
 * SensorAttributes attributes;
 * attributes.device_class = "temperature";
 * attributes.unit = "°C";
 * attributes.precision = 1;
 * @endcode
 *
 * You can see the sensor device class types here:
 * https://github.com/home-assistant/core/blob/dev/homeassistant/components/sensor/strings.json
 * https://github.com/home-assistant/core/blob/dev/homeassistant/components/sensor/const.py
 */

struct SensorAttributes
{
    std::string device_class;
    std::string state_class;
    std::string unit_of_measurement;
    int suggested_display_precision;
    std::optional<std::string> entity_category;
};

struct sensor_policy {
    std::chrono::seconds refresh_interval{0};
    std::chrono::seconds expire_after{0};
    bool retain = true;
    bool reject_negative = false;

    static sensor_policy telemetry(std::chrono::seconds refresh = std::chrono::seconds(60),
                                   std::chrono::seconds expiry = std::chrono::seconds(180)) {
        if (refresh.count() <= 0) {
            throw std::invalid_argument("Telemetry refresh must be positive");
        }
        return {refresh, expiry, false, true};
    }

    static sensor_policy power(std::chrono::seconds refresh = std::chrono::seconds(60),
                               std::chrono::seconds expiry = std::chrono::seconds(180)) {
        return telemetry(refresh, expiry);
    }
};

/**
 * @brief Class for a sensor function
 *
 * Derived from function base
 */

template<typename T>
class SensorFunction : public FunctionBase
{
public:
    /**
     * @brief Construct a new SensorFunction object
     *
     * @param function_name The name of the function
     * @param attributes The sensors that this function has. The key is the name of the sensor, and the value is the
     * attributes of the sensor
     */
    using clock = std::chrono::steady_clock;
    SensorFunction(const std::string& function_name, const SensorAttributes& attributes,
                   sensor_policy policy = {});

    /**
     * @brief Implement init function for this function
     */
    void init() override;

    /**
     * @brief This is purely a data source, so it does not subscribe to anything
     *
     * @return An empty vector
     */
    [[nodiscard]] std::vector<std::string> getSubscribeTopics() const override
    {
        return {};
    };

    /**
     * @brief Implements the discovery topic function for this function
     *
     * @return The discovery topic for this function
     */
    [[nodiscard]] std::string getDiscoveryTopic() const override;

    /**
     * @brief Implements the discovery payload function for this function
     *
     * @return The discovery payload for this function
     */
    [[nodiscard]] json getDiscoveryJson() const override;

    /**
     * @brief Implement process message function for this function. Should never be called
     *
     * @param topic The topic of the message
     * @param payload The payload of the message
     */
    void processMessage(const std::string& topic, const std::string& payload) override
    {
    }

    /**
     * @brief Implement sending status for all values
     */
    void sendStatus() const override;

    /**
     * @brief Set the state of this function
     *
     * @param value The value to send for this sensor
     * @return Whether the measurement is valid, not whether publication succeeded.
     */
    bool update(T value);
    bool update(T value, clock::time_point now);
    // Acknowledged offline is ready for transport, but never a numeric sample.
    void await_sample();
    void invalidate(std::string const& reason);
    void resetConnection(std::uint64_t epoch) override;
    void service() override;
    bool readyForOnline() const override;
    std::optional<std::string> availabilityTopic() const override;

private:
    bool m_has_data = false;
    sensor_policy m_policy;
    mutable std::optional<T> m_published_value;
    std::optional<clock::time_point> m_last_publish;
    std::optional<std::string> m_fault;
    bool m_seen_poll = false;
    bool m_awaiting_sample = false;
    bool m_desired_health = false;
    std::optional<bool> m_acknowledged_health;
    std::optional<publication> m_pending_health;
    bool m_pending_health_value = false;
    void clear_sample();
    bool freshnessEnabled() const { return m_policy.refresh_interval.count() > 0; }
protected:
    SensorAttributes m_attributes;
    T m_value{};
};
