#pragma once

#include <vector>
#include <memory>
#include "mfi.h"
#include "hass_mqtt_device/core/mqtt_connector.h"
#include "mfi_mqtt_client/port.h"

namespace mfi_mqtt_client {
	/**
	 * @brief Home Assistant MQTT device representing an mFi board.
	 *
	 * Wraps a mfi::board and exposes each sensor port as a set of
	 * Home Assistant entities (power, current, voltage sensors and
	 * relay switch) via MQTT auto-discovery.
	 */
	class device : public DeviceBase {
	public:
		/**
		 * @brief Constructs a device from a board and MQTT connection parameters.
		 * @param board The mFi board to expose.
		 * @param server The MQTT broker hostname or IP.
		 * @param port The MQTT broker port.
		 * @param username The MQTT username.
		 * @param password The MQTT password.
		 */
		explicit device(mfi::board const& board, std::string const& server, int port,
			std::string const& username, std::string const& password,
			sensor_policy policy = sensor_policy::power(), std::string const& native_id = {});

		/**
		 * @brief Initializes all sensor ports and registers with the MQTT connector.
		 * @note Must be called after construction and before connect().
		 */
		virtual void init();

		/**
		 * @brief Reads current sensor values and publishes changes via MQTT.
		 */
		virtual void update();
		json migration_map(std::string const& native_id) const;

		/**
		 * @brief Connects to the MQTT broker.
		 * @return true if the connection was successful.
		 */
		bool connect();
		bool shutdown();

		/**
		 * @brief Processes pending MQTT messages for the given duration.
		 * @param timeout The polling duration in milliseconds.
		 */
		void processMessages(int timeout);
	protected:
		virtual std::optional<std::string> getManufacturer() const override;
		virtual std::optional<std::string> getModel() const override;
		virtual std::optional<std::string> getSoftwareVersion() const override;
		virtual std::optional<std::string> getModelId() const override;
		virtual std::optional<std::string> getConfigurationUrl() const override;
		std::shared_ptr<MQTTConnector> _connector;
		mfi::board _board;
		sensor_policy _policy;
	private:
		std::vector<std::shared_ptr<port>> _ports;
	};
}