#pragma once

#include <CLI/CLI.hpp>
#include <cstdint>

namespace mfi_cli {
	/**
	 * @brief Implements the `sensor` subcommand tree for querying port readings.
	 */
	class sensor_command final {
	public:
		/**
		 * @brief Registers the `sensor` subcommands with the given app.
		 * @param app The CLI11 app to register with.
		 */
		explicit sensor_command(CLI::App& app);
	private:
		void list();
		void get();

		uint16_t _id;
	};
}
