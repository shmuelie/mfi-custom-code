/**
 * @author      Morgan Tørvolt
 * @contributors somebody, hopefully@someday.com
 * @copyright   See LICENSE file
 */

// Include the corresponding header file
#include "hass_mqtt_device/functions/on_off_light.h"
#include "hass_mqtt_device/core/device_base.h"

// Include any other necessary headers
#include "hass_mqtt_device/core/mqtt_connector.h"
#include "hass_mqtt_device/logger/logger.hpp" // For logging

OnOffLightFunction::OnOffLightFunction(const std::string& function_name, std::function<void(bool)> control_cb)
    : FunctionBase(function_name)
    , m_control_cb(control_cb)
{
}

void OnOffLightFunction::init()
{
    LOG_DEBUG("Initializing on/off light function {}", getName());
}

std::vector<std::string> OnOffLightFunction::getSubscribeTopics() const
{
    // Create a vector of the topics
    std::vector<std::string> topics;
    topics.push_back(getBaseTopic() + "set");
    return topics;
}

std::string OnOffLightFunction::getDiscoveryTopic() const
{
    auto parent = m_parent_device.lock();
    if(!parent)
    {
        LOG_ERROR("Parent device is not available.");
        return "";
    }
    return "homeassistant/light/" + parent->getFullId() + "/" + getCleanName() + "/config";
}

json OnOffLightFunction::getDiscoveryJson() const
{
    auto parent = m_parent_device.lock();
    json discoveryJson;
    discoveryJson["name"] = getName();
    discoveryJson["unique_id"] = getId();
    // On/off
    discoveryJson["state_topic"] = getBaseTopic() + "state";
    discoveryJson["command_topic"] = getBaseTopic() + "set";

    return discoveryJson;
}

void OnOffLightFunction::processMessage(const std::string& topic, const std::string& payload)
{
    LOG_DEBUG("Processing message for on/off light function {} with topic {}", getName(), topic);

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
    m_control_cb(payloadJson["state"] == "ON");
}

void OnOffLightFunction::sendStatus() const
{
    auto parent = m_parent_device.lock();
    if(!parent)
    {
        return;
    }

    json payload;
    payload["state"] = m_state ? "ON" : "OFF";
    parent->publishMessage(getBaseTopic() + "state", payload);
}

void OnOffLightFunction::update(bool state)
{
    m_state = state;
    sendStatus();
}
