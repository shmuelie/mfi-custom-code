/**
 * @author      Morgan Tørvolt
 * @contributors somebody, hopefully@someday.com
 * @copyright   See LICENSE file
 */

#include "hass_mqtt_device/devices/dimmable_light.h"
#include "hass_mqtt_device/core/function_base.h"
#include "hass_mqtt_device/logger/logger.hpp"
#include <functional>
#include <memory>

/**
 * @brief Implements the light device
 *
 * Derived from DeviceBase
 */

DimmableLightDevice::DimmableLightDevice(const std::string& device_name,
                                         std::function<void(bool, double)> control_cb,
                                         const std::string& unique_id)
    : DeviceBase(device_name, unique_id)
    , m_control_cb(control_cb)
{
}

void DimmableLightDevice::init()
{
    m_dimmable_light = std::make_shared<DimmableLightFunction>("dimmable_light", m_control_cb);
    registerFunction(m_dimmable_light);
}

void DimmableLightDevice::update(bool state, double brightness)
{
    m_dimmable_light->update(state, brightness);
}
