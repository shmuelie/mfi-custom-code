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
 * @brief Class for an on/off only light device
 *
 * Derived from function base
 */

class DimmableLightFunction : public FunctionBase
{
public:
    /**
     * @brief Construct a new DimmableLightFunction object
     *
     * @param function_name The name of the function
     * @param control_cb The callback function for controlling the device
     */
    DimmableLightFunction(const std::string& function_name, std::function<void(bool, double)> control_cb);

    /**
     * @brief Implement init function for this function
     */
    void init() override;

    /**
     * @brief Implements the subscribe topics function for this function
     *
     * @return The MQTT topic for this function
     */
    std::vector<std::string> getSubscribeTopics() const override;

    /**
     * @brief Implements the discovery topic function for this function
     *
     * @return The discovery topic for this function
     */
    std::string getDiscoveryTopic() const override;

    /**
     * @brief Implements the discovery payload function for this function
     *
     * @return The discovery payload for this function
     */
    json getDiscoveryJson() const override;

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
     * @brief Set the state and brightness of this function
     *
     * @param state The state to set
     * @param brightness The brightness to set (0-1)
     */
    void update(bool state, double brightness);

    /**
     * @brief Get the state of this function
     *
     * @return The state of this function
     */
    [[nodiscard]] bool getState() const
    {
        return m_state;
    };

    /**
     * @brief Get the brightness of this function
     *
     * @return The brightness of this function
     */
    bool getBrightness() const
    {
        return m_brightness;
    };

private:
protected:
    bool m_state;
    double m_brightness;
    std::function<void(bool, double)> m_control_cb;
};
