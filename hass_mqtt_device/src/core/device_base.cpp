/**
 * @author      Morgan Tørvolt
 * @contributors somebody, hopefully@someday.com
 * @copyright   See LICENSE file
 */

// Include the corresponding header file
#include "hass_mqtt_device/core/device_base.h"
#include "hass_mqtt_device/core/function_base.h"
#include "hass_mqtt_device/core/helper_functions.hpp"

// Include any other necessary headers
#include "hass_mqtt_device/logger/logger.hpp" // For logging

DeviceBase::DeviceBase(const std::string& device_name, const std::string& id)
	: m_device_name(device_name)
	, m_id(getValidHassString(id))
{
	LOG_DEBUG("Creating device with name: {} id {}", getName(), getId());
}

std::string DeviceBase::getId() const
{
	return m_id;
}

std::string DeviceBase::getName() const
{
	return m_device_name;
}

std::string DeviceBase::getCleanName() const
{
	return getValidHassString(getName());
}

std::string DeviceBase::getUniqueId() const
{
	// Get the connector
	if(auto connector = m_connector.lock())
	{
		// Return the full id
		std::string unique_id = connector->getId();
		if(!m_id.empty())
		{
			unique_id += "_" + m_id;
		}
		return unique_id;
	}
	// If the connector is no longer alive, throw
	throw std::runtime_error("MQTTConnector is not alive");
}

std::string DeviceBase::getFullId() const
{
	// Return the full id
	return getUniqueId() + "_" + getCleanName();
}

std::vector<std::string> DeviceBase::getSubscribeTopics() const
{
	// Loop through all functions and get their topics
	std::vector<std::string> topics;
	for(const auto& function : m_functions)
	{
		auto functionTopics = function->getSubscribeTopics();
		topics.insert(topics.end(), functionTopics.begin(), functionTopics.end());
	}
	// Make sure there are no duplicates, throw an error if there are
	std::sort(topics.begin(), topics.end());
	auto last = std::unique(topics.begin(), topics.end());
	if(last != topics.end())
	{
		LOG_ERROR("Duplicate topics found for device {}", getName());
		throw std::runtime_error("Duplicate topics found for device");
	}
	return topics;
}

void DeviceBase::registerFunction(std::shared_ptr<FunctionBase> function)
{
	// Check if the function with the same discovery topic already exists
	LOG_DEBUG("Registering function with name {}", function->getCleanName());
	for(auto& existingFunction : m_functions)
	{
		if(existingFunction->getCleanName() == function->getCleanName())
		{
			LOG_ERROR("Function with discovery topic {} already exists", function->getDiscoveryTopic());
			throw std::runtime_error("Function with discovery topic already exists");
		}
	}
	function->setParentDevice(shared_from_this());
	m_functions.push_back(function);
}

std::shared_ptr<FunctionBase> DeviceBase::findFunction(const std::string& name) const
{
	// Loop through all functions and check if the name matches
	for(const auto& function : m_functions)
	{
		if(function->getName() == name || function->getCleanName() == name)
		{
			return function;
		}
	}
	// If no function was found, return an empty pointer
	return std::shared_ptr<FunctionBase>();
}

void DeviceBase::sendDiscovery()
{
	// Get availability topic from m_connector
	std::string availabilityTopic;
	if(auto connector = m_connector.lock())
	{
		availabilityTopic = connector->getAvailabilityTopic();
	}
	else
	{
		LOG_ERROR("Failed to send discovery message for device {}-{}: MQTTConnector is no longer alive",
				  getName(),
				  getId());
		throw std::runtime_error("Failed to send discovery message for device: MQTTConnector is no longer alive");
	}

	// Loop through all functions and gather their discovery parts
	LOG_DEBUG("Sending discovery for device: {}", getName());
	std::map<std::string, json> discoveryParts;
	for(auto& function : m_functions)
	{
		LOG_DEBUG("Sending discovery for function {}", function->getName());
		auto discoveryTopic = function->getDiscoveryTopic();
		auto discoveryJson = function->getDiscoveryJson();
		// Check if the discovery topic already exists, throw an error if it does
		if(discoveryParts.find(discoveryTopic) != discoveryParts.end())
		{
			LOG_ERROR("Duplicate discovery topic {} found for device {}", discoveryTopic, getName());
			throw std::runtime_error("Duplicate discovery topic found for device");
		}

		discoveryJson["schema"] = "json";
		discoveryJson["availability_topic"] = availabilityTopic;
		discoveryJson["availability_template"] = "{{ value_json.availability }}";

		// Add the device info to the discovery json
		discoveryJson["device"] = {{"name", getName()},
								   {"identifiers", {m_id}},
		};
		auto manufacturer = getManufacturer();
		if (manufacturer) {
			discoveryJson["device"]["manufacturer"] = manufacturer.value();
		}
		auto model = getModel();
		if (model) {
			discoveryJson["device"]["model"] = model.value();
		}
		auto sw_version = getSoftwareVersion();
		if (sw_version) {
			discoveryJson["device"]["sw_version"] = sw_version.value();
		}
		auto hw_version = getHardwareVersion();
		if (hw_version) {
			discoveryJson["device"]["hw_version"] = hw_version.value();
		}
		auto configuration_url = getConfigurationUrl();
		if (configuration_url) {
			discoveryJson["device"]["configuration_url"] = configuration_url.value();
		}
		auto model_id = getModelId();
		if (model_id) {
			discoveryJson["device"]["model_id"] = model_id.value();
		}
		auto serial_number = getSerialNumber();
		if (serial_number) {
			discoveryJson["device"]["serial_number"] = serial_number.value();
		}

		discoveryParts[discoveryTopic] = discoveryJson;
	}
	// Now to send the discovery messages
	for(auto& discoveryPart : discoveryParts)
	{
		LOG_DEBUG("Sending discovery message to topic: {}", discoveryPart.first);
		try
		{
			publishMessage(discoveryPart.first, discoveryPart.second);
		}
		catch(const std::exception& e)
		{
			LOG_ERROR("Failed to send discovery message for device {}-{}: {}", getName(), getId(), e.what());
			throw e;
		}
	}
}

std::optional<std::string> DeviceBase::getManufacturer() const
{
	return std::nullopt;
}

std::optional<std::string> DeviceBase::getModel() const
{
	return std::nullopt;
}

std::optional<std::string> DeviceBase::getSoftwareVersion() const
{
	return std::nullopt;
}

std::optional<std::string> DeviceBase::getHardwareVersion() const
{
	return std::nullopt;
}

std::optional<std::string> DeviceBase::getConfigurationUrl() const
{
	return std::nullopt;
}

std::optional<std::string> DeviceBase::getModelId() const
{
	return std::nullopt;
}

std::optional<std::string> DeviceBase::getSerialNumber() const
{
	return std::nullopt;
}

void DeviceBase::processMessage(const std::string& topic, const std::string& payload)
{
	LOG_DEBUG("Processing message for device {} with topic {}", getName(), topic);
	// Loop through all functions and check if the topic matches
	for(auto& function : m_functions)
	{
		// Check if the topic contains the function name
		if(topic.find(function->getCleanName()) != std::string::npos)
		{
			// Call the function's onMessage method
			function->processMessage(topic, payload);
		}
	}
}

void DeviceBase::publishMessage(const std::string& topic, const json& payload)
{
	// Check if the connector is still alive
	if(auto connector = m_connector.lock())
	{
		// Publish the message
		connector->publishMessage(topic, payload);
	}
	else
	{
		LOG_ERROR("Failed to publish MQTT message: MQTTConnector is no longer alive");
		throw std::runtime_error("Failed to publish MQTT message: MQTTConnector is no longer alive");
	}
}

void DeviceBase::sendStatus()
{
	// Get availability topic from m_connector
	std::string availabilityTopic;
	if(auto connector = m_connector.lock())
	{
		availabilityTopic = connector->getAvailabilityTopic();
	}
	else
	{
		LOG_ERROR("Failed to send discovery message for device {}-{}: MQTTConnector is no longer alive",
				  getName(),
				  getId());
		throw std::runtime_error("Failed to send discovery message for device: MQTTConnector is no longer alive");
	}

	// Create the will message
	json payload;
	payload["availability"] = "online";

	publishMessage(availabilityTopic, payload);

	// Publish the availability and status messages for all functions
	for(auto& function : m_functions)
	{
		function->sendStatus();
	}
}
