#include "mfi_mqtt_client/options.h"
#include "shmuelie/string_helpers.hpp"

using namespace std;
using namespace mfi_mqtt_client;
using namespace shmuelie;

options::options(string const& doc, vector<string> const& argv) noexcept : docopt_options(doc, argv) {
	try {
		_server = get_string("--server").value();

		auto potentialPort = get_uint16("--port");
		if (!potentialPort) {
			add_error("Invalid port");
		}
		else {
			_port = potentialPort.value();
		}

		_polling_rate = get_int("--polling-rate").value_or(1000);
		_username = get_string("--username").value();
		_password = get_string("--password").value();
	}
	catch (exception const& error) {
		add_error(error.what());
	}
}

string const& options::server() const noexcept {
	return _server;
}

uint16_t options::port() const noexcept {
	return _port;
}

string const& options::username() const noexcept {
	return _username;
}

string const& options::password() const noexcept {
	return _password;
}

int options::polling_rate() const noexcept {
	return _polling_rate;
}