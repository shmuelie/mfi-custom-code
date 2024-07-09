#pragma once

#include <CLI/CLI.hpp>

namespace mfi_cli {
	class info_command {
	public:
		explicit info_command(CLI::App& app);
		void run();
	private:
		bool _all_option;
	};
}