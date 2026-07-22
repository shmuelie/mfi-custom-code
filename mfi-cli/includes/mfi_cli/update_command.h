#pragma once

#include <CLI/CLI.hpp>
#include <cstdint>
#include <string>

namespace mfi_cli {
	/**
	 * @brief Implements the `update` subcommand: self-update from GitHub Releases.
	 */
	class update_command final {
	public:
		/**
		 * @brief Registers the `update` subcommand with the given app.
		 * @param app The CLI11 app to register with.
		 */
		explicit update_command(CLI::App& app);
	private:
		void run();

		std::string _repo;
		std::string _proxy;
		std::string _bin_dir;
		bool _insecure;
		bool _check_only;
	};
}
