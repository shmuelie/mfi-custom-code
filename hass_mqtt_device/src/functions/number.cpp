/**
 * @author      Morgan Tørvolt
 * @contributors somebody, hopefully@someday.com
 * @copyright   See LICENSE file
 */

// Include the corresponding header file
#include "hass_mqtt_device/functions/number.h"
#include "hass_mqtt_device/core/device_base.h"

// Include any other necessary headers
#include "hass_mqtt_device/logger/logger.hpp" // For logging

NumberFunction::NumberFunction(const std::string& function_name,
                               std::function<void(double)> control_cb,
                               double max,
                               double min,
                               double step)
    : FunctionBase(function_name)
    , m_control_cb(control_cb)
    , m_max(max)
    , m_min(min)
    , m_step(step)
{
    if(m_step == 0)
    {
        LOG_ERROR("Step size is 0, this will cause problems with the slider in "
                  "Home Assistant");
        throw std::invalid_argument("Step size is 0, this will cause problems with "
                                    "the slider in Home Assistant");
    }
}

void NumberFunction::init()
{
    LOG_DEBUG("Initializing number function {}", getName());
}

std::vector<std::string> NumberFunction::getSubscribeTopics() const
{
    // Create a vector of the topics
    std::vector<std::string> topics;
    topics.push_back(getBaseTopic() + "set");
    return topics;
}

std::string NumberFunction::getDiscoveryTopic() const
{
    auto parent = m_parent_device.lock();
    if(!parent)
    {
        LOG_ERROR("Parent device is not available.");
        return "";
    }
    return "homeassistant/number/" + parent->getFullId() + "/" + getCleanName() + "/config";
}

json NumberFunction::getDiscoveryJson() const
{
    json discoveryJson;
    discoveryJson["name"] = getName();
    discoveryJson["unique_id"] = getId();
    // On/off
    discoveryJson["state_topic"] = getBaseTopic() + "state";
    discoveryJson["value_template"] = "{{ value_json.value }}";
    discoveryJson["command_topic"] = getBaseTopic() + "set";
    discoveryJson["min"] = m_min;
    discoveryJson["max"] = m_max;
    discoveryJson["step"] = m_step;

    return discoveryJson;
}

void NumberFunction::processMessage(const std::string& topic, const std::string& payload)
{
    LOG_DEBUG("Processing message for number function {} with topic {}", getName(), topic);

    // Check if the topic is really for us
    if(topic != getBaseTopic() + "set")
    {
        LOG_DEBUG("State topic is not for us ({} != {}).", topic, getBaseTopic() + "set");
        return;
    }

    // The number device always sends a string, so we need to convert it to a
    // double
    double value;
    try
    {
        value = std::stod(payload);
    }
    catch(const std::invalid_argument& e)
    {
        LOG_ERROR("Invalid argument: {}", e.what());
        return;
    }
    catch(const std::out_of_range& e)
    {
        LOG_ERROR("Out of range: {}", e.what());
        return;
    }
    if(value > m_max)
    {
        LOG_INFO("Value {} is larger than max value {}, setting to max value.", value, m_max);
        value = m_max;
    }
    if(value < m_min)
    {
        LOG_INFO("Value {} is smaller than min value {}, setting to min value.", value, m_min);
        value = m_min;
    }
    // Make sure the value is a multiple of the step
    if(m_step != 0)
    {
        value = std::round(value / m_step) * m_step;
    }

    if(value != m_number)
    {
        m_control_cb(value);
    }
}

void NumberFunction::sendStatus() const
{
    auto parent = m_parent_device.lock();
    if(!parent)
    {
        return;
    }

    json payload;
    payload["value"] = m_number;
    parent->publishMessage(getBaseTopic() + "state", payload);
}

void NumberFunction::update(double number)
{
    m_number = number;
    sendStatus();
}
