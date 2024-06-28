#pragma once

#include <string>
#include <vector>

namespace mfi {
	class sensor;

	/**
	 * @brief Reprsents the mFi device.
	*/
	class board final {
	public:
		/**
		 * @brief Initializes a new instance of the mfi::board class.
		*/
		board();

		/**
		 * @brief Gets the model name of the mFi device.
		*/
		std::string const& name() const;
		/**
		 * @brief Gets the model short name of the mFi device.
		*/
		std::string const& short_name() const;
		/**
		 * @brief Gets the model ID of the mFi device.
		*/
		uint16_t id() const;
		/**
		 * @brief Gets the collection of sensors on the mFi device.
		*/
		std::vector<sensor> const& sensors() const;

		/**
		 * @brief Gets the version of the mFi firmware.
		 */
		std::string const& version() const;

		/**
		 * @brief Gets the hostname of the device.
		 */
		std::string const& hostname() const;
	private:
		std::string _name;
		std::string _shortName;
		uint16_t _id;
		std::vector<sensor> _sensors{};
		std::string _version;
		std::string _hostname;
	};
}