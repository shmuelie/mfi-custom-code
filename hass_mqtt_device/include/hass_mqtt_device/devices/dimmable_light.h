/**
 * @author      Morgan Tørvolt
 * @contributors somebody, hopefully@someday.com
 * @copyright   See LICENSE file
 */

#pragma once

#include "hass_mqtt_device/core/device_base.h"
#include "hass_mqtt_device/functions/dimmable_light.h"

/**
 * @brief Class for a light device
 *
 * Derived from DeviceBase
 */

class DimmableLightDevice : public DeviceBase
{
public:
    /**
     * @brief Construct a new Light object
     *
     * @param device_name The name of the device
     * @param unique_id The unique id of the device
     * @param control_cb The callback to call when receiving a control message
     */
    DimmableLightDevice(const std::string& device_name,
                        std::function<void(bool, double)> control_cb,
                        const std::string& unique_id = "");

    /**
     * @brief Implement init function for this device
     */
    void init();

    /**
     * @brief Update the state and brightness of the light. Should be called by the user and be
     * kept in sync with the actual state of the light
     *
     * @param state The new state of the light
     * @param brightness The updated brightness of the light, 0-1 value, 0 is off
     * or very dim, 1 is full brightness
     */
    void update(bool state, double brightness);

private:
    std::shared_ptr<DimmableLightFunction> m_dimmable_light;
    std::function<void(bool, double)> m_control_cb;
};
