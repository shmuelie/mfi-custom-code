#include <iostream>
#include <CLI/CLI.hpp>
#include "mfi_server/mfi_http_server.h"
#include "mg/manager.h"
#include "shmuelie/string_helpers.h"

using namespace std::chrono_literals;

#define STR_(S) #S
#define STR(S) STR_(S)

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
		// verify that the string is a number or not, and the numbers
		// are in the valid range
		if (!shmuelie::is_number(str) || stoi(str) > 255 || stoi(str) < 0) {
			return "Invalid IP address";
		}
	}

	return "";
}

int main(int argc, char* argv[]) {
	CLI::App app{ "mFi HTTP RESET Server" };
	app.set_version_flag("--version", "mFi HTTP REST Server " STR(MFI_SERVER_VERSION));

	std::string ip;
	app.add_option("-i,--ip", ip, "The IP address to listen on")->default_val("0.0.0.0")->check(validate_ip);
	uint16_t port;
	app.add_option("-p,--port", port, "The port to listen on")->default_val(8000);
	uint8_t log_level;
	app.add_option("-l,--log-level", log_level, "The log level to use")->default_val(0);

	mg_log_set(log_level);

	mfi_http_server server{};
	auto rootConnection = server.listen("http://" + ip + ":" + std::to_string(port));
	if (!rootConnection) {
		return -2;
	}
	for (;;) {
		server.manager()->poll(1000s);
	}
}