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
 * @brief Class for a number device
 *
 * Derived from function base
 */

class NumberFunction : public FunctionBase
{
public:
    /**
     * @brief Construct a new NumberFunction object
     *
     * @param function_name The name of the function
     * @param control_cb The callback function for controlling the device
     * @param max The maximum value of the number
     * @param min The minimum value of the number
     * @param step The step size of the number
     */
    NumberFunction(const std::string& function_name,
                   std::function<void(double)> control_cb,
                   double max = 100,
                   double min = 0,
                   double step = 1);

    /**
     * @brief Implement init function for this function
     */
    void init() override;

    /**
     * @brief Implements the subscribe topics function for this function
     *
     * @return The MQTT topic for this function
     */
    [[nodiscard]] std::vector<std::string> getSubscribeTopics() const override;

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
     * @brief Implement process message function for this function
     *
     * @param topic The topic of the message
     * @param payload The payload of the message
     */
    void processMessage(const std::string& topic, const std::string& payload) override;

    /**
     * @brief Implement sending status for all values
     */
    void sendStatus() const override;

    /**
     * @brief Set the state of this function
     *
     * @param state The state to set
     */
    void update(double number);

    /**
     * @brief Get the value of this function
     *
     * @return The value of this function
     */
    [[nodiscard]] double getNumber() const
    {
        return m_number;
    };

private:
protected:
    double m_number;
    double m_max;
    double m_min;
    double m_step;
    std::function<void(double)> m_control_cb;
};
