#pragma once

#include <CLI/CLI.hpp>
#include <cstdint>
#include <string>

namespace mfi_cli {
	/**
	 * @brief Implements the `relay` subcommand tree for reading and controlling relays.
	 */
	class relay_command final {
	public:
		/**
		 * @brief Registers the `relay` subcommands with the given app.
		 * @param app The CLI11 app to register with.
		 */
		explicit relay_command(CLI::App& app);
	private:
		void get();
		void set(bool value);
		void toggle();

		uint16_t _id;
		std::string _state;
	};
}
