#include "mfi_mqtt_client/options.h"

using namespace std;
using namespace mfi_mqtt_client;

options::options(const string& doc, const vector<string>& argv) noexcept : docopt_options(doc, argv) {
	try {
		auto& args = values();

		_server = args.at("server").asString();
		_port = static_cast<int>(args.at("port").asLong());
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