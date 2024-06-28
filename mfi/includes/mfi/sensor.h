#pragma once

#include <string>

namespace mfi {
	/**
	 * @brief Represents a port or switch on the mFi device.
	*/
	class sensor final {
	public:
		/**
		 * @brief Initializes a new instance of the mfi::sensor class.
		 * @param id The ID of the sensor.
		*/
		explicit sensor(uint8_t id);

		/**
		 * @brief Gets the ID of the sensor.
		*/
		uint8_t id() const;
		/**
		 * @brief Gets the watts currently being drawn on the port.
		 */
		double power() const;
		/**
		 * @brief Gets the current currently being drawn on the port.
		*/
		double current() const;
		/**
		 * @brief Gets the voltage currently being drawn on the port.
		*/
		double voltage() const;
		double power_factor() const;
		/**
		 * @brief Gets a value indicating whether the port is on or off.
		*/
		bool relay() const;
		/**
		 * @brief Sets a value indicating whether the port is on or off.
		 * @param value A value indicating whether the port is on or off.
		*/
		void relay(bool value) const;

		/**
		 * @brief Gets the name of the sensor.
		 * @remarks Value is read from the system on every call.
		 */
		std::string const name() const;

		/**
		 * @brief Gets the label of the sensor.
		 * @remarks Value is read from the system on every call.
		 */
		std::string const label() const;
	private:
		uint8_t _id;
		double read(const std::string& path) const;
	};
}