/**
 * @author      Morgan Tørvolt
 * @contributors somebody, hopefully@someday.com
 * @copyright   See LICENSE file
 */

#pragma once

#include "hass_mqtt_device/core/device_base.h"

/**
 * @brief Class for a switch device
 *
 * Derived from DeviceBase
 */

class SwitchDevice : public DeviceBase
{
public:
    /**
     * @brief Construct a new switch object
     *
     * @param device_name The name of the device
     * @param unique_id The unique id of the device
     * @param control_cb The callback to call when the state of the switch
     */
    SwitchDevice(const std::string& device_name, std::function<void(bool)> control_cb, const std::string& unique_id = "");

    /**
     * @brief Implement init function for this device
     */
    void init();

    /**
     * @brief Update the state of the switch. Should be called by the user and be
     * kept in sync with the actual state of the switch
     *
     * @param state The new state of the switch
     */
    void update(bool state);

private:
    std::function<void(bool)> m_control_cb;
};
