#include <chrono>
#include <thread>
#include <CLI/CLI.hpp>
#include "mfi_server/mfi_http_server.h"
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
	for (;;) {
		std::this_thread::sleep_for(1000s);
	}
}