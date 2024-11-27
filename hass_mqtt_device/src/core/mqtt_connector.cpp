// Include the corresponding header file
#include "hass_mqtt_device/core/mqtt_connector.h"
#include "hass_mqtt_device/core/device_base.h"

// Include any other necessary headers
#include "hass_mqtt_device/logger/logger.hpp" // For logging
#include <array>
#include <chrono>
#include <mosquitto.h>
#include <string>
#include <thread>

constexpr std::array<int, 8> backoff_ladder = {1000, 1000, 5000, 5000, 5000, 15000, 30000, 30000};
int backoff_state = 0;

// Constructor implementation
MQTTConnector::MQTTConnector(const std::string& server,
                             const int port,
                             const std::string& username,
                             const std::string& password,
                             const std::string& unique_id)
    : m_server(server)
    , m_port(port)
    , m_username(username)
    , m_password(password)
    , m_unique_id(unique_id)
    , m_mosquitto(nullptr)
{
    LOG_DEBUG("MQTTConnector created with server: {}", server);

    // Initialize the MQTT library
    mosquitto_lib_init();
}

std::string MQTTConnector::getAvailabilityTopic() const
{
    std::string topic = "home/" + getId() + "/availability";
    return topic;
};

// Connect to the MQTT server
bool MQTTConnector::connect()
{
    LOG_DEBUG("Connecting to MQTT server: {}", m_server);

    m_mosquitto = mosquitto_new(nullptr, true, this);
    if(m_mosquitto == nullptr)
    {
        LOG_ERROR("Failed to create mosquitto instance");
        return false;
    }

    // Set the username and password
    mosquitto_username_pw_set(m_mosquitto, m_username.c_str(), m_password.c_str());

    // Set the callbacks
    mosquitto_connect_callback_set(m_mosquitto, connectCallback);
    mosquitto_disconnect_callback_set(m_mosquitto, disconnectCallback);
    mosquitto_subscribe_callback_set(m_mosquitto, subscribeCallback);
    mosquitto_unsubscribe_callback_set(m_mosquitto, unsubscribeCallback);
    mosquitto_message_callback_set(m_mosquitto, messageCallback);

    // Set the lwt availability topic for all devices
    publishLWT();

    int rc = mosquitto_connect(m_mosquitto, m_server.c_str(), m_port, 60);
    if(rc != MOSQ_ERR_SUCCESS)
    {
        LOG_ERROR("Failed to connect to MQTT server: {}", mosquitto_strerror(rc));
        return false;
    }
    LOG_DEBUG("Connected to MQTT server: {}", m_server);
    m_is_connected = true;

    return true;
}

// Disconnect from the MQTT server
void MQTTConnector::disconnect()
{
    LOG_DEBUG("Disconnecting from MQTT server: {}", m_server);
    mosquitto_disconnect(m_mosquitto);
}

// Check if connected to the MQTT server
bool MQTTConnector::isConnected() const
{
    return m_is_connected;
}

// Register a device to listen for its MQTT topics
void MQTTConnector::registerDevice(std::shared_ptr<DeviceBase> device)
{
    // Make sure the device is not already registered. Same Id is fine, but same
    // name and id is not
    for(auto& registered_device : m_registered_devices)
    {
        if(registered_device->getCleanName() == device->getCleanName() && registered_device->getId() == device->getId())
        {
            LOG_ERROR("Device with id {} and name {} already registered", device->getId(), device->getCleanName());
            throw std::runtime_error("Device with name already registered");
        }
    }

    device->setParentConnector(shared_from_this());
    m_registered_devices.push_back(device);

    // If connected, subscribe to the topic
    if(m_is_connected)
    {
        disconnect();
        connect();
    }
    LOG_DEBUG("Device registered with name: {}", device->getName());
}

// Unregister a device
void MQTTConnector::unregisterDevice(const std::string& device_name)
{
    for(auto it = m_registered_devices.begin(); it != m_registered_devices.end(); it++)
    {
        if((*it)->getId() == device_name)
        {
            m_registered_devices.erase(it);
            break;
        }
    }
}

// Get a device by name
std::shared_ptr<DeviceBase> MQTTConnector::getDevice(const std::string& device_name) const
{
    for(const auto& device : m_registered_devices)
    {
        if(device->getName() == device_name || device->getCleanName() == device_name)
        {
            return device;
        }
    }
    return nullptr;
}

// Process incoming MQTT messages
void MQTTConnector::processMessages(int timeout, bool exit_on_event)
{
    if(!isConnected())
    {
        LOG_DEBUG("Not connected to MQTT server. Attempting to reconnect.");
        static int slept_for = 0;
        LOG_DEBUG("Slept since last reconnect try {}ms", slept_for);
        std::this_thread::sleep_for(std::chrono::milliseconds(timeout));
        slept_for += timeout;
        if(slept_for < backoff_ladder[backoff_state])
        {
            return;
        }
        slept_for = 0;
        backoff_state++;
        if(backoff_state >= backoff_ladder.size())
        {
            backoff_state = backoff_ladder.size() - 1;
        }

        bool rc = connect();
        if(!rc)
        {
            LOG_ERROR("Failed to reconnect to MQTT server: {}. Continuing to sleep "
                      "and retry.",
                      mosquitto_strerror(rc));
            return;
        }
        backoff_state = 0;
    }

    // At this point, we are connected to the MQTT server
    // Get the monotonic time when we should be done processing messages
    // Get the monotonic time when we should be done processing messages
    auto done = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);

    while(true)
    {
        auto now = std::chrono::steady_clock::now();
        if(now >= done)
        {
            break;
        }

        // How much time left till done
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(done - now);
        int rc = mosquitto_loop(m_mosquitto, remaining.count(), 1);
        if(rc != MOSQ_ERR_SUCCESS && rc != MOSQ_ERR_NO_CONN)
        {
            LOG_ERROR("Failed to process MQTT messages: {}", mosquitto_strerror(rc));
        }
        if(exit_on_event)
        {
            break;
        }
    }
}

// Publish a message
void MQTTConnector::publishMessage(const std::string& topic, const json& payload)
{
    std::string payload_str = payload.dump();
    LOG_DEBUG("Publishing MQTT message to topic: {}", topic);
    LOG_DEBUG("MQTT message payload: {}", payload_str);
    int rc = mosquitto_publish(m_mosquitto, nullptr, topic.c_str(), payload_str.size(), payload_str.c_str(), 1, true);
    if(rc != MOSQ_ERR_SUCCESS)
    {
        LOG_ERROR("Failed to publish MQTT message: {}", mosquitto_strerror(rc));
    }
}

// publish last will and testament
void MQTTConnector::publishLWT()
{
    // Create the will message
    json payload;
    payload["availability"] = "offline";

    std::string payload_str = payload.dump();
    LOG_DEBUG("Publishing LWT MQTT message to topic: {}", getAvailabilityTopic());
    LOG_DEBUG("LWT MQTT message payload: {}", payload_str);
    int rc =
        mosquitto_will_set(m_mosquitto, getAvailabilityTopic().c_str(), payload_str.size(), payload_str.c_str(), 1, true);
    if(rc != MOSQ_ERR_SUCCESS)
    {
        LOG_ERROR("Failed to publish MQTT message: {}", mosquitto_strerror(rc));
    }
}

// Callback for incoming MQTT messages, implementing the on_message
void MQTTConnector::messageCallback(mosquitto*  /*mosq*/, void* obj, const mosquitto_message* message)
{
    LOG_DEBUG("Received MQTT message on topic: {}", message->topic);
    auto* connector = static_cast<MQTTConnector*>(obj);
    // Convert the topic and message to a string
    std::string topic(message->topic);
    std::string payload(static_cast<char*>(message->payload), message->payloadlen);

    for(auto& device : connector->m_registered_devices)
    {
        // Check if the topic starts with the device's topic after home/
        if(topic.find("home/" + device->getFullId()) == 0)
        {
            // Call the device's processMessage method
            device->processMessage(topic, payload);
        }
    }
}

// Callback for successful connection to the MQTT server, implementing
// on_connect
void MQTTConnector::connectCallback(mosquitto*  /*mosq*/, void* obj, int rc)
{
    LOG_DEBUG("Connected to MQTT server callback");
    auto* connector = static_cast<MQTTConnector*>(obj);

    // Subscribe to the topics of the registered devices
    for(auto& device : connector->m_registered_devices)
    {
        for(auto& topic : device->getSubscribeTopics())
        {
            LOG_DEBUG("Subscribing to topic: {}", topic);
            rc = mosquitto_subscribe(connector->m_mosquitto, nullptr, topic.c_str(), 0);
            if(rc != MOSQ_ERR_SUCCESS)
            {
                LOG_ERROR("Failed to subscribe to topic: {}", mosquitto_strerror(rc));
                return;
            }
        }
    }

    // Send the discovery messages for the registered devices
    LOG_DEBUG("Sending discovery messages for {} devices", connector->m_registered_devices.size());
    for(auto& device : connector->m_registered_devices)
    {
        device->sendDiscovery();
        device->sendStatus();
    }
    LOG_DEBUG("Discovery messages sent for {} devices", connector->m_registered_devices.size());

    connector->m_is_connected = true;
}

// Callback for disconnection from the MQTT server, implementing
// on_disconnect
void MQTTConnector::disconnectCallback(mosquitto*  /*mosq*/, void* obj, int  /*rc*/)
{
    LOG_INFO("Disconnected from MQTT server");
    auto* connector = static_cast<MQTTConnector*>(obj);
    connector->m_is_connected = false;
}

// Callback for successful subscription to an MQTT topic, implementing
// on_subscribe
void MQTTConnector::subscribeCallback(mosquitto*  /*mosq*/, void*  /*obj*/, int  /*mid*/, int  /*qos_count*/, const int*  /*granted_qos*/)
{
    LOG_DEBUG("Subscribed to MQTT topic");
}

// Callback for successful unsubscription to an MQTT topic, implementing
// on_unsubscribe
void MQTTConnector::unsubscribeCallback(mosquitto*  /*mosq*/, void*  /*obj*/, int  /*mid*/)
{
    LOG_ERROR("Unsubscribed from MQTT topic");
}
