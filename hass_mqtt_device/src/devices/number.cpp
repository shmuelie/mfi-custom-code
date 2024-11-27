/**
 * @author      Morgan Tørvolt
 * @contributors somebody, hopefully@someday.com
 * @copyright   See LICENSE file
 */

#include "hass_mqtt_device/devices/number.h"
#include "hass_mqtt_device/core/function_base.h"
#include "hass_mqtt_device/logger/logger.hpp"
#include <functional>
#include <memory>

/**
 * @brief Implements the light device
 *
 * Derived from DeviceBase
 */

NumberDevice::NumberDevice(const std::string& device_name,
                           std::function<void(double)> control_cb,
                           const std::string& unique_id)
    : DeviceBase(device_name, unique_id)
    , m_control_cb(control_cb)
{
}

void NumberDevice::init()
{
    m_number = std::make_shared<NumberFunction>("number", m_control_cb);
    registerFunction(m_number);
}

void NumberDevice::update(double number)
{
    m_number->update(number);
}
