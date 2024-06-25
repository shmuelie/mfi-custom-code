#include "mfi_mqtt_client/options.h"
#include "shmuelie/string_helpers.hpp"

using namespace std;
using namespace mfi_mqtt_client;
using namespace shmuelie;

options::options(const string& doc, const vector<string>& argv) noexcept : docopt_options(doc, argv) {
	try {
		auto& args = values();

		_server = args.at("server").asString();

		auto potentialPort = try_stoul<uint16_t>(args.at("--port").asString());
		if (!potentialPort) {
			add_error("Invalid port");
		}
		else {
			_port = potentialPort.value();
		}

		_username = args.at("username").asString();
		_password = args.at("password").asString();
	}
	catch (exception const& error) {
		add_error(error.what());
	}
}

const string& options::server() const noexcept {
	return _server;
}

uint16_t options::port() const noexcept {
	return _port;
}

const string& options::username() const noexcept {
	return _username;
}

const string& options::password() const noexcept {
	return _password;
}