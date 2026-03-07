#pragma once

#include <memory>
#include "mfi.h"
#include "hass_mqtt_device/core/mqtt_connector.h"
#include "hass_mqtt_device/functions/switch.h"
#include "hass_mqtt_device/functions/sensor.h"

namespace mfi_mqtt_client {
	/**
	 * @brief Represents a single port on an mFi device.
	 *
	 * Each port exposes power, current, and voltage sensor functions
	 * and a relay switch function to Home Assistant via MQTT.
	 */
	class port {
	public:
		/**
		 * @brief Constructs a port for the given sensor on the board.
		 * @param board The mFi board the sensor belongs to.
		 * @param sensor The hardware sensor to wrap.
		 */
		explicit port(mfi::board const& board, mfi::sensor const& sensor);

		/**
		 * @brief Registers all sensor and switch functions with the parent device.
		 * @param device The parent DeviceBase to register functions with.
		 */
		void init(std::shared_ptr<DeviceBase> const& device);

		/**
		 * @brief Reads current sensor values and publishes any changes.
		 */
		void update();

		/**
		 * @brief Sets the relay state for this port.
		 * @param value true to turn on, false to turn off.
		 */
		void relay(bool value);
	private:
		mfi::sensor _sensor;
		std::shared_ptr<SensorFunction<double>> _power;
		std::shared_ptr<SensorFunction<double>> _current;
		std::shared_ptr<SensorFunction<double>> _voltage;
		std::shared_ptr<SwitchFunction> _relay;
	};
}