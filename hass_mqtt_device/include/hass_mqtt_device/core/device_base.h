/**
 * @author      Morgan Tørvolt
 * @contributors somebody, hopefully@someday.com
 * @copyright   See LICENSE file
 */

#pragma once

#include "hass_mqtt_device/core/mqtt_connector.h"
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

using json = nlohmann::json;

class FunctionBase;

/**
 * @brief Base class for all devices that can be registered with the
 * MQTTConnector.
 *
 * This class provides the basic interface that all devices should implement.
 * Derived classes should provide specific implementations for their device
 * type.
 */

class DeviceBase : public std::enable_shared_from_this<DeviceBase>
{
public:
    /**
     * @brief Construct a new DeviceBase object
     *
     * @param device_name The name of the device
     * @param unique_id The unique id of the device. Can be empty if you don't have multiple devices to register
     */
    explicit DeviceBase(const std::string& device_name, const std::string& id = "");

    /**
     * @brief Destroy the DeviceBase object
     */
    virtual ~DeviceBase() = default;

    /**
     * @brief Get the MQTT topic for this device
     *
     * @return The MQTT topic for this device
     */
    std::string getId() const;

    /**
     * @brief Get the name of this device
     *
     * @return The name of this device
     */
    std::string getName() const;

    /**
     * @brief Get a clean version of the name of this device
     *
     * @return A cleaned up name of this device
     */
    std::string getCleanName() const;

    /**
     * @brief Get the unique id of this device
     *
     * @return The unique id of this device, this includes the connection unique id too
     */
    std::string getUniqueId() const;

    /**
     * @brief Get the id_name of this device
     *
     * Used for the MQTT topic by the functions of this device
     *
     * @return The name of this device
     */
    std::string getFullId() const;

    /**
     * @brief Get the MQTT topic to subscribe to for this device
     *
     * @return The MQTT topic for this device
     */
    std::vector<std::string> getSubscribeTopics() const;

    /**
     * @brief Add a function to this device
     *
     * @param function The function to add
     */
    void registerFunction(std::shared_ptr<FunctionBase> function);

    /**
     * @brief Find a function by name
     *
     * @param name The name of the function to find
     * @return A shared pointer to the function if found, nullptr otherwise
     */
    std::shared_ptr<FunctionBase> findFunction(const std::string& name) const;

    /**
     * @brief Get the functions of this device
     *
     * @return The functions of this device
     */
    std::vector<std::shared_ptr<FunctionBase>> getFunctions() const
    {
        return m_functions;
    }

    /**
     * @brief Process an incoming MQTT message
     *
     * @param topic The topic of the incoming message. This will be concatenated
     * with the device name and base topic
     * @param payload The payload of the incoming message
     */
    void processMessage(const std::string& topic, const std::string& payload);

    /**
     * @brief Publish an MQTT message
     *
     * @param topic The topic to publish to. This will be concatenated with the
     * device name and base topic
     * @param payload The payload to publish
     */
    void publishMessage(const std::string& topic, const json& payload);

    /**
     * @brief Send the home assistant discovery message for this device
     *
     * @note This method should be called after the device has been registered
     * with the MQTTConnector
     */
    void sendDiscovery();

    /**
     * @brief Send an update message for this device.
     *
     * Sends the state of all functions for this device.
     *
     * @note This method should be called after the device has been registered
     * with the MQTTConnector
     */
    void sendStatus();

    /**
     * @brief Send the home assistant will message for this device
     *
     * @note This method should be called after the device has been registered
     * with the MQTTConnector
     */
    void sendLWT();

protected:
    std::string m_device_name;
    std::string m_id;
    std::vector<std::shared_ptr<FunctionBase>> m_functions;
    std::weak_ptr<MQTTConnector> m_connector;

    virtual std::string getManufacturer() const;
    virtual std::string getModel() const;
    virtual std::string getVersion() const;

private:
    friend void MQTTConnector::registerDevice(std::shared_ptr<DeviceBase> device);
    void setParentConnector(std::weak_ptr<MQTTConnector> connector)
    {
        m_connector = connector;
    };
};
