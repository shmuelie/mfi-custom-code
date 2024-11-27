/**
 * @author      Morgan Tørvolt
 * @contributors somebody, hopefully@someday.com
 * @copyright   See LICENSE file
 */

// Include the corresponding header file
#include "hass_mqtt_device/functions/dimmable_light.h"
#include "hass_mqtt_device/core/device_base.h"

// Include any other necessary headers
#include "hass_mqtt_device/logger/logger.hpp" // For logging

DimmableLightFunction::DimmableLightFunction(const std::string& function_name,
                                             std::function<void(bool, double)> control_cb)
    : FunctionBase(function_name)
    , m_control_cb(control_cb)
{
}

void DimmableLightFunction::init()
{
    LOG_DEBUG("Initializing dimmable light function {}", getName());
}

std::vector<std::string> DimmableLightFunction::getSubscribeTopics() const
{
    // Create a vector of the topics
    std::vector<std::string> topics;
    topics.push_back(getBaseTopic() + "set");
    return topics;
}

std::string DimmableLightFunction::getDiscoveryTopic() const
{
    auto parent = m_parent_device.lock();
    if(!parent)
    {
        LOG_ERROR("Parent device is not available.");
        return "";
    }
    return "homeassistant/light/" + parent->getFullId() + "/" + getCleanName() + "/config";
}

json DimmableLightFunction::getDiscoveryJson() const
{
    json discoveryJson;
    discoveryJson["name"] = getName();
    discoveryJson["unique_id"] = getId();
    // On/off
    discoveryJson["state_topic"] = getBaseTopic() + "state";
    discoveryJson["command_topic"] = getBaseTopic() + "set";
    // Brightness
    discoveryJson["brightness"] = true;

    return discoveryJson;
}

void DimmableLightFunction::processMessage(const std::string& topic, const std::string& payload)
{
    LOG_DEBUG("Processing message for dimmable light function {} with topic {}", getName(), topic);

    // Check if the topic is really for us
    if(topic != getBaseTopic() + "set")
    {
        LOG_DEBUG("State topic is not for us ({} != {}).", topic, getBaseTopic() + "set");
        return;
    }

    // Decode the payload
    json payloadJson;
    try
    {
        payloadJson = json::parse(payload);
    }
    catch(const json::exception& e)
    {
        LOG_ERROR("JSON error in payload: {}. Error: {}", payload, e.what());
        return;
    }

    // Handle the sub topics
    double brightness = m_brightness;
    if(payloadJson.contains("brightness"))
    {
        brightness = payloadJson["brightness"];
        brightness /= 255.0;
    }
    m_control_cb(payloadJson["state"] == "ON", brightness);
}

void DimmableLightFunction::sendStatus() const
{
    auto parent = m_parent_device.lock();
    if(!parent)
    {
        return;
    }

    json payload;
    // Convert brightness to int scale 0-255, round to nearest int
    int brightness_int = std::round(m_brightness * 255.0);
    LOG_DEBUG("Sending status for dimmable light function {} with state {} and brightness {}",
              getName(),
              m_state,
              brightness_int);
    payload["state"] = m_state ? "ON" : "OFF";
    payload["brightness"] = brightness_int;
    parent->publishMessage(getBaseTopic() + "state", payload);
}

void DimmableLightFunction::update(bool state, double brightness)
{
    m_state = state;
    m_brightness = brightness;
    sendStatus();
}
