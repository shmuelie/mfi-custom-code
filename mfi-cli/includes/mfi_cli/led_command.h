#pragma once

#include <CLI/CLI.hpp>
#include <cstdint>
#include "mfi/led.h"

namespace mfi_cli {
	/**
	 * @brief Implements the `led` subcommand tree for reading and controlling the LED.
	 */
	class led_command final {
	public:
		/**
		 * @brief Registers the `led` subcommands with the given app.
		 * @param app The CLI11 app to register with.
		 */
		explicit led_command(CLI::App& app);
	private:
		void get();
		void set(CLI::App const& set_command);

		mfi::led_color _color;
		uint32_t _frequency;
	};
}
