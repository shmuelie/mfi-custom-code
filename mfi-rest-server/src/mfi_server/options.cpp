#include "mfi_server/options.h"
#include "shmuelie/string_helpers.hpp"
#include "mongoose.h"

using namespace std;
using namespace shmuelie;

static bool validate_ip(string const& ip)
{
	// split the string into tokens
	vector<string> list = split(ip, '.');

	// if the token size is not equal to four
	if (list.size() != 4) {
		return false;
	}

	// validate each token
	for (std::string str : list)
	{
		// verify that the string is a number or not, and the numbers
		// are in the valid range
		if (!is_number(str) || stoi(str) > 255 || stoi(str) < 0) {
			return false;
		}
	}

	return true;
}

options::options(string const& doc, vector<string> const& argv) noexcept : docopt_options(doc, argv) {
	try {
		auto& ip = get_string("--ip").value();
		if (!validate_ip(ip)) {
			add_error("Invalid IP address");
		}
		else {
			_ip = ip;
		}

		auto potentialPort = get_uint16("--port");
		if (!potentialPort) {
			add_error("Invalid port");
		}
		else {
			_port = potentialPort.value();
		}

		auto potentialLogLevel = get_uint8("--log-level");
		if (!potentialLogLevel || potentialLogLevel.value() > MG_LL_VERBOSE) {
			add_error("Invalid log level");
		}
		else {
			_logLevel = potentialLogLevel.value();
		}
	}
	catch (exception const& error) {
		add_error(error.what());
	}
}

const string& options::ip() const noexcept {
	return _ip;
}

uint16_t options::port() const noexcept {
	return _port;
}

uint8_t options::log_level() const noexcept {
	return _logLevel;
}