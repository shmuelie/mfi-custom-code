#include <CLI/CLI.hpp>
#include "mfi_cli/info_command.h"
#include "mfi_cli/sensor_command.h"
#include "mfi_cli/relay_command.h"
#include "mfi_cli/led_command.h"
#include "mfi_cli/config_command.h"
#include "version_info.h"

int main(int argc, char* argv[]) {
	CLI::App app{ PROJECT_DESCRIPTION };
	app.set_version_flag("--version", PROJECT_NAME " " PROJECT_VERSION);
	app.require_subcommand();
	mfi_cli::info_command info{ app };
	mfi_cli::sensor_command sensor{ app };
	mfi_cli::relay_command relay{ app };
	mfi_cli::led_command led{ app };
	mfi_cli::config_command config{ app };

	try {
		app.parse(argc, argv);
	}
	catch (CLI::ParseError const& e) {
		return app.exit(e);
	}
}