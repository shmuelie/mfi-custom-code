#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include <CLI/CLI.hpp>
#include "mfi_server/mfi_http_server.h"
#include "mfi_update.h"
#include "shmuelie/string_helpers.h"
#include "version_info.h"

using namespace std::chrono_literals;

static std::string validate_ip(std::string const& ip)
{
	// split the string into tokens
	std::vector<std::string> list = shmuelie::split(ip, '.');

	// if the token size is not equal to four
	if (list.size() != 4) {
		return "Invalid IP address";
	}

	// validate each token
	for (std::string str : list)
	{
		if (!shmuelie::is_number(str)) {
			return "Invalid IP address";
		}
		int val;
		try {
			val = stoi(str);
		}
		catch (std::exception const&) {
			return "Invalid IP address";
		}
		if (val < 0 || val > 255) {
			return "Invalid IP address";
		}
	}

	return "";
}

int main(int argc, char* argv[]) {
	CLI::App app{ PROJECT_DESCRIPTION };
	app.set_version_flag("--version", PROJECT_NAME " " PROJECT_VERSION);

	std::string ip;
	app.add_option("-i,--ip", ip, "The IP address to listen on")->default_val("0.0.0.0")->check(validate_ip);
	uint16_t port;
	app.add_option("-p,--port", port, "The port to listen on")->default_val(8000);
	uint8_t log_level;
	app.add_option("-l,--log-level", log_level, "The log level to use (accepted for compatibility)")->default_val(0);
	(void)log_level;

	bool update_enabled;
	app.add_flag("--update,!--no-update", update_enabled, "Enable self-update from GitHub Releases")->default_val(true);
	uint32_t update_interval;
	app.add_option("--update-interval", update_interval, "Seconds between update checks")->default_val(86400);
	std::string update_repo;
	app.add_option("--update-repo", update_repo, "GitHub owner/name for updates")->default_val("shmuelie/mfi-custom-code");
	std::string update_proxy;
	app.add_option("--update-proxy", update_proxy, "Proxy host:port for the update downloader");
	bool update_insecure;
	app.add_flag("--update-insecure,!--update-check-cert", update_insecure, "Skip TLS cert verification when updating")->default_val(true);

	try {
		app.parse(argc, argv);
	}
	catch (CLI::ParseError const& e) {
		return app.exit(e);
	}

	mfi_http_server server{};
	if (!server.listen(ip + ":" + std::to_string(port))) {
		return -2;
	}

	std::vector<std::string> args;
	for (int i = 0; i < argc; ++i) {
		args.emplace_back(argv[i]);
	}
	auto updater = mfi_update::make_periodic_updater(
		update_enabled, update_interval, update_repo, update_proxy, update_insecure,
		PROJECT_NAME, PROJECT_VERSION, args);

	// Sleep in bounded slices so update checks are not starved by a long sleep.
	auto slice = std::chrono::seconds(update_interval < 3600 ? (update_interval == 0 ? 1 : update_interval) : 3600);
	for (;;) {
		std::this_thread::sleep_for(slice);
		if (updater) {
			try {
				auto result = updater->tick();
				if (result && *result != mfi_update::update_result::up_to_date) {
					std::cout << "Self-update: " << mfi_update::describe(*result) << std::endl;
				}
			}
			catch (std::exception const& e) {
				std::cerr << "Error during update check: " << e.what() << std::endl;
			}
		}
	}
}