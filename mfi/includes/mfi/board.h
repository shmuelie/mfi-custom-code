#pragma once

#include <string>
#include <vector>

namespace mfi {
	class sensor;

	/**
	 * @brief Reprsents the mFi device.
	*/
	class board {
	public:
		/**
		 * @brief Initializes a new instance of the mfi::board class.
		*/
		explicit board();

		/**
		 * @brief Gets the model name of the mFi device.
		*/
		const std::string& name() const;
		/**
		 * @brief Gets the model short name of the mFi device.
		*/
		const std::string& short_name() const;
		/**
		 * @brief Gets the model ID of the mFi device.
		*/
		uint16_t id() const;
		/**
		 * @brief Gets the collection of sensors on the mFi device.
		*/
		const std::vector<sensor>& sensors() const;

		/**
		 * @brief Gets the version of the mFi firmware.
		 */
		const std::string& version() const;
	private:
		std::string _name;
		std::string _shortName;
		uint16_t _id;
		std::vector<sensor> _sensors{};
		std::string _version;
	};
}