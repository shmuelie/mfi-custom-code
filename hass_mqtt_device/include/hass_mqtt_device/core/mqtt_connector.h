/**
 * @author      Morgan Tørvolt
 * @contributors somebody, hopefully@someday.com
 * @copyright   See LICENSE file
 */

#pragma once

#include <memory> // For std::shared_ptr
#include <mosquitto.h>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <spdlog/spdlog.h>
#include "hass_mqtt_device/core/publication.h"
#include <chrono>
#include <map>
#include <optional>
#include <functional>

using json = nlohmann::json;

class DeviceBase;

struct mqtt_session_options {
    std::string availability_topic;
    std::function<std::string()> new_session_id;
};

/**
 * @brief Class for connecting to an MQTT server and registering devices to
 * listen for their MQTT topics
 *
 * @note This class is not thread-safe, so it should only be used from one
 * thread
 */

class MQTTConnector : public std::enable_shared_from_this<MQTTConnector>
{
public:
    /**
     * @brief Construct a new MQTTConnector object
     *
     * @param server The MQTT server to connect to
     * @param port The port to use when connecting to the MQTT server
     * @param username The username to use when connecting to the MQTT server
     * @param password The password to use when connecting to the MQTT server
     * @param unique_id The unique id of the device. This will be used as a common value for all devices that are
     * registered, and for the availability topic
     */
    MQTTConnector(const std::string& server,
                  int port,
                  const std::string& username,
                  const std::string& password,
                  const std::string& unique_id,
                  mqtt_session_options session_options = {});

    ~MQTTConnector();

    /**
     * @brief Get the unique id of the connection
     *
     * @return The unique id of the connection
     */
    std::string getId() const
    {
        return m_unique_id;
    };

    /**
     * @brief Get the unique id of the connection
     *
     * @return The unique id of the connection
     */
    std::string getAvailabilityTopic() const;

    /**
     * @brief Connect to the MQTT server
     */
    bool connect();

    /**
     * @brief Disconnect from the MQTT server
     */
    void disconnect();
    bool shutdown(std::chrono::milliseconds timeout = std::chrono::seconds(5));
    void abortConnection();

    /**
     * @brief Check if connected to the MQTT server
     *
     * @return true if connected to the MQTT server, false otherwise
     */
    bool isConnected() const;

    /**
     * @brief Register a device to listen for its MQTT topics
     *
     * @param device The device to register
     */
    void registerDevice(std::shared_ptr<DeviceBase> device);

    /**
     * @brief Unregister a device to stop listening for its MQTT topics
     *
     * @param device_name The device to unregister
     */
    void unregisterDevice(const std::string& device_name);

    /**
     * @brief Get a device by name
     *
     * @param device_name The name of the device to get
     */
    std::shared_ptr<DeviceBase> getDevice(const std::string& device_name) const;

    /**
     * @brief Process incoming MQTT messages. Needs to be called regularly with a
     * timeout
     *
     * @note This method should be called in the main loop
     *
     * @param timeout The timeout in milliseconds
     * @param exit_on_event If true, the method will return immediately if a
     * message is received
     */
    void processMessages(int timeout, bool exit_on_event = false);

    /**
     * @brief Send a message to the MQTT server
     *
     * @param topic The topic to publish to
     * @param payload The payload to publish
     * @param qos The MQTT QoS level (0, 1, or 2). Defaults to 1.
     * @param retain Whether the broker should retain the message. Defaults to true.
     */
    publication publishMessage(const std::string& topic, const json& payload, int qos = 1, bool retain = true);
    publication_state publicationState(publication const& message) const;
    std::uint64_t connectionEpoch() const noexcept { return m_epoch; }
    std::string const& sessionId() const noexcept { return m_session_id; }

private:
    /**
     * @brief Send a last will and testament message to the MQTT server
     */
    bool publishLWT();
    json availabilityPayload(bool online) const;
    void beginSession();
    void serviceSession();
    std::vector<std::shared_ptr<DeviceBase>> devices() const;
    static void publishCallback(mosquitto*, void*, int);

    /**
     * @brief Callback for incoming MQTT messages, implementing the on_message
     *
     * @param mosq The mosquitto instance
     * @param obj The user data
     * @param message The message
     */
    static void messageCallback(mosquitto* mosq, void* obj, const mosquitto_message* message);

    /**
     * @brief Callback for successful connection to the MQTT server, implementing
     * on_connect
     *
     * @param mosq The mosquitto instance
     * @param obj The user data
     * @param rc The connection result
     */
    static void connectCallback(mosquitto* mosq, void* obj, int rc);

    /**
     * @brief Callback for disconnection from the MQTT server, implementing
     * on_disconnect
     *
     * @param mosq The mosquitto instance
     * @param obj The user data
     * @param rc The disconnection result
     */
    static void disconnectCallback(mosquitto* mosq, void* obj, int rc);

    /**
     * @brief Callback for successful subscription to an MQTT topic, implementing
     * on_subscribe
     *
     * @param mosq The mosquitto instance
     * @param obj The user data
     * @param mid The message ID
     * @param qos_count The number of granted subscriptions
     * @param granted_qos The granted QoS levels
     */
    static void subscribeCallback(mosquitto* mosq, void* obj, int mid, int qos_count, const int* granted_qos);

    /**
     * @brief Callback for unsuccessful subscription to an MQTT topic,
     * implementing on_unsubscribe
     *
     * @param mosq The mosquitto instance
     * @param obj The user data
     * @param mid The message ID
     */
    static void unsubscribeCallback(mosquitto* mosq, void* obj, int mid);

    static void logCallback(mosquitto* msoq, void* obj, int level, const char* str);

    std::string m_server;
    int m_port;
    std::string m_username;
    std::string m_password;
    std::string m_unique_id;
    mqtt_session_options m_session_options;
    std::string m_session_id;
    bool m_is_connected = false;
    std::vector<std::weak_ptr<DeviceBase>> m_registered_devices;
    mosquitto* m_mosquitto;
    std::shared_ptr<spdlog::logger> m_logger;
    int m_backoff_state;
    int m_slept_for;
    bool m_connecting = false;
    bool m_just_connected = false;
    bool m_stopping = false;
    std::uint64_t m_epoch = 0;
    std::uint64_t m_sequence = 0;
    struct pending_publication {
        std::uint64_t sequence;
        std::chrono::steady_clock::time_point sent;
        std::string topic;
        int qos;
    };
    std::map<int, pending_publication> m_pending;
    bool m_publishing = false;
    std::optional<int> m_early_completion;
    std::optional<publication> m_offline;
    std::optional<publication> m_online;
    std::chrono::steady_clock::time_point m_connect_started{};
};
