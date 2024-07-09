#include <CLI/CLI.hpp>
#include "mfi_cli/info_command.h"

#define STR_(S) #S
#define STR(S) STR_(S)

int main(int argc, char* argv[]) {
	CLI::App app{ "mFi command line tool" };
	app.set_version_flag("--version", "mFi CLI " STR(MFI_CLI_VERSION));
	app.require_subcommand();
	mfi_cli::info_command info{ app };

	try {
		app.parse(argc, argv);
	}
	catch (CLI::ParseError const& e) {
		return app.exit(e);
	}
}