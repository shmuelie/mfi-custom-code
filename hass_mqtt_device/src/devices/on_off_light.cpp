/**
 * @author      Morgan Tørvolt
 * @contributors somebody, hopefully@someday.com
 * @copyright   See LICENSE file
 */

#include "hass_mqtt_device/devices/on_off_light.h"
#include "hass_mqtt_device/core/function_base.h"
#include "hass_mqtt_device/functions/on_off_light.h"
#include "hass_mqtt_device/logger/logger.hpp"
#include <functional>
#include <memory>

/**
 * @brief Implements the light device
 *
 * Derived from DeviceBase
 */

OnOffLightDevice::OnOffLightDevice(const std::string& device_name,
                                   std::function<void(bool)> control_cb,
                                   const std::string& unique_id)
    : DeviceBase(device_name, unique_id)
    , m_control_cb(control_cb)
{
}

void OnOffLightDevice::init()
{
    std::shared_ptr<OnOffLightFunction> on_off_light = std::make_shared<OnOffLightFunction>("on_off_light", m_control_cb);
    std::shared_ptr<FunctionBase> on_off_light_base = on_off_light;
    registerFunction(on_off_light_base);
}

void OnOffLightDevice::update(bool state)
{
    std::shared_ptr<OnOffLightFunction> on_off_light =
        std::dynamic_pointer_cast<OnOffLightFunction>(findFunction("on_off_light"));
    if(on_off_light)
    {
        on_off_light->update(state);
    }
    else
    {
        LOG_ERROR("Could not find on_off_light function");
    }
}
