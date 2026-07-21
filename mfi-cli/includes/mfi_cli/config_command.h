#pragma once

#include <CLI/CLI.hpp>
#include <string>

namespace mfi_cli {
	/**
	 * @brief Implements the `config` subcommand tree for reading INI-style config files.
	 */
	class config_command final {
	public:
		/**
		 * @brief Registers the `config` subcommands with the given app.
		 * @param app The CLI11 app to register with.
		 */
		explicit config_command(CLI::App& app);
	private:
		void list();
		void get(CLI::Option const* default_option);

		std::string _file;
		std::string _key;
		std::string _default;
	};
}
