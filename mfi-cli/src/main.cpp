#include <CLI/CLI.hpp>
#include "mfi_cli/info_command.h"
#include "version_info.h"

int main(int argc, char* argv[]) {
	CLI::App app{ PROJECT_DESCRIPTION };
	app.set_version_flag("--version", PROJECT_NAME " " PROJECT_VERSION);
	app.require_subcommand();
	mfi_cli::info_command info{ app };

	try {
		app.parse(argc, argv);
	}
	catch (CLI::ParseError const& e) {
		return app.exit(e);
	}
}