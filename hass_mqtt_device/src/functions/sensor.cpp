/**
 * @author      Morgan Tørvolt
 * @contributors somebody, hopefully@someday.com
 * @copyright   See LICENSE file
 */

// Include the corresponding header file
#include "hass_mqtt_device/functions/sensor.h"
#include "hass_mqtt_device/core/device_base.h"

// Include any other necessary headers
#include "hass_mqtt_device/logger/logger.hpp" // For logging
#include <cmath>
#include <type_traits>

// Making sure that the template class is instantiated for the types that we want to use
template class SensorFunction<int>;
template class SensorFunction<float>;
template class SensorFunction<double>;
template class SensorFunction<std::string>;
template class SensorFunction<bool>;

template<typename T>
SensorFunction<T>::SensorFunction(const std::string& function_name, const SensorAttributes& attributes)
    : FunctionBase(function_name)
    , m_attributes(attributes)
    , m_has_data(false)
{}

template<typename T>
void SensorFunction<T>::init()
{
    LOG_DEBUG("Initializing sensor function {}", getName());
}

template<typename T>
std::string SensorFunction<T>::getDiscoveryTopic() const
{
    auto parent = m_parent_device.lock();
    if(!parent)
    {
        LOG_ERROR("Parent device is not available.");
        return "";
    }
    return "homeassistant/sensor/" + parent->getFullId() + "/" + getCleanName() + "/config";
}

template<typename T>
json SensorFunction<T>::getDiscoveryJson() const
{
    json discoveryJson;
    discoveryJson["name"] = getName();
    discoveryJson["unique_id"] = getId();
    discoveryJson["state_topic"] = getBaseTopic() + "state";
    discoveryJson["value_template"] = "{{ value_json.value }}";
    discoveryJson["device_class"] = m_attributes.device_class;
    discoveryJson["state_class"] = m_attributes.state_class;
    discoveryJson["unit_of_measurement"] = m_attributes.unit_of_measurement;
    discoveryJson["suggested_display_precision"] = m_attributes.suggested_display_precision;

    return discoveryJson;
}

template<typename T>
void SensorFunction<T>::sendStatus() const
{
    if(!m_has_data)
    {
        return;
    }
    auto parent = m_parent_device.lock();
    if(!parent)
    {
        return;
    }

    json payload;
    payload["value"] = m_value;
    // State is high-frequency telemetry: QoS 0 avoids unbounded out-queue growth
    // if the broker lags. Retained so HA still gets the last value on subscribe.
    parent->publishMessage(getBaseTopic() + "state", payload, 0, true);
}

template<typename T>
void SensorFunction<T>::update(T value)
{
    if constexpr(std::is_floating_point_v<T>)
    {
        // Quantize to the entity's display precision so sub-precision sensor
        // jitter does not trigger a publish every poll cycle.
        if(m_attributes.suggested_display_precision >= 0)
        {
            const T factor = std::pow(static_cast<T>(10), m_attributes.suggested_display_precision);
            value = std::round(value * factor) / factor;
        }
    }
    if(m_has_data && m_value == value)
    {
        return;
    }
    m_has_data = true;
    m_value = value;
    sendStatus();
}
