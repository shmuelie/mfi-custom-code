#pragma once

#include <string>
#include <map>

namespace mfi {
	/**
	 * @brief Reads INI-style key=value configuration files.
	 */
	class config final {
	public:
		/**
		 * @brief Reads all key=value pairs from a configuration file.
		 * @param file The path to the configuration file.
		 * @return A map of all key=value pairs found in the file.
		 * @throws std::runtime_error if the file cannot be read.
		 */
		static std::map<std::string, std::string> const read_all(const std::string& file);

		/**
		 * @brief Reads a single value from a configuration file.
		 * @param file The path to the configuration file.
		 * @param prop The key to look up.
		 * @return The value associated with the key, or an empty string if not found.
		 * @throws std::runtime_error if the file cannot be read.
		 */
		static std::string const read(const std::string& file, const std::string& prop);

		/**
		 * @brief Reads a single value from a configuration file with a default.
		 * @param file The path to the configuration file.
		 * @param prop The key to look up.
		 * @param defaultValue The value to return if the key is not found or the file cannot be read.
		 * @return The value associated with the key, or defaultValue if not found.
		 */
		static std::string const read(const std::string& file, const std::string& prop, const std::string& defaultValue);
	};
}