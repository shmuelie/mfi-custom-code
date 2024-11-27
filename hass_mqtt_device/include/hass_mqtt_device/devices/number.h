/**
 * @author      Morgan Tørvolt
 * @contributors somebody, hopefully@someday.com
 * @copyright   See LICENSE file
 */

#pragma once

#include "hass_mqtt_device/core/device_base.h"
#include "hass_mqtt_device/functions/number.h"

/**
 * @brief Class for a light device
 *
 * Derived from DeviceBase
 */

class NumberDevice : public DeviceBase
{
public:
    /**
     * @brief Construct a new Light object
     *
     * @param device_name The name of the device
     * @param unique_id The unique id of the device
     * @param control_cb The callback to call when receiving a control message
     */
    NumberDevice(const std::string& device_name, std::function<void(double)> control_cb, const std::string& unique_id = "");

    /**
     * @brief Implement init function for this device
     */
    void init();

    /**
     * @brief Update the state of the number device. Should be called by the user
     * and be kept in sync with the actual state of whatever the number represents
     *
     * @param number The new value
     */
    void update(double number);

private:
    std::shared_ptr<NumberFunction> m_number;
    std::function<void(double)> m_control_cb;
};
