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
    SensorFunction(const std::string& function_name, const SensorAttributes& attributes);

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
     * @param state The state to set
     * @param value The value to send for this sensor
     */
    void update(T value);

private:
    bool m_has_data;
protected:
    SensorAttributes m_attributes;
    T m_value;
};
