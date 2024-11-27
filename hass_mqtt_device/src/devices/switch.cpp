/**
 * @author      Morgan Tørvolt
 * @contributors somebody, hopefully@someday.com
 * @copyright   See LICENSE file
 */

#include "hass_mqtt_device/devices/switch.h"
#include "hass_mqtt_device/core/function_base.h"
#include "hass_mqtt_device/functions/switch.h"
#include "hass_mqtt_device/logger/logger.hpp"
#include <functional>
#include <memory>
#include <utility>

/**
 * @brief Implements the switch device
 *
 * Derived from DeviceBase
 */

SwitchDevice::SwitchDevice(const std::string& device_name,
                           std::function<void(bool)> control_cb,
                           const std::string& unique_id)
    : DeviceBase(device_name, unique_id)
    , m_control_cb(std::move(control_cb))
{
}

void SwitchDevice::init()
{
    std::shared_ptr<SwitchFunction> sw = std::make_shared<SwitchFunction>("switch", m_control_cb);
    std::shared_ptr<FunctionBase> sw_base = sw;
    registerFunction(sw_base);
}

void SwitchDevice::update(bool state)
{
    std::shared_ptr<SwitchFunction> sw = std::dynamic_pointer_cast<SwitchFunction>(findFunction("switch"));
    if(sw)
    {
        sw->update(state);
    }
    else
    {
        LOG_ERROR("Could not find switch function");
    }
}
