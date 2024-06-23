#include "mfi_mqtt_client/options.h"
#include "docopt.h"

using namespace std;
using namespace docopt;
using namespace mfi_mqtt_client;

options::options(const string& doc, const vector<string>& argv) noexcept {
	try {
		auto args = docopt_parse(doc, argv);

		_server = args.at("server").asString();
		_port = static_cast<int>(args.at("port").asLong());
		_username = args.at("username").asString();
		_password = args.at("password").asString();
	}
	catch (DocoptExitHelp const&) {
		_help = true;
	}
	catch (DocoptExitVersion const&) {
		_version = true;
	}
	catch (DocoptLanguageError const& error) {
		_errors.push_back(string{ "Docopt usage string could not be parsed\n" } + error.what());
	}
	catch (DocoptArgumentError const& error) {
		_errors.push_back(error.what());
	}
}

bool options::help() const noexcept {
	return _help;
}

bool options::version() const noexcept {
	return _version;
}

const vector<string>& options::errors() const noexcept {
	return _errors;
}

const string& options::server() const noexcept {
	return _server;
}

int options::port() const noexcept {
	return _port;
}

const string& options::username() const noexcept {
	return _username;
}

const string& options::password() const noexcept {
	return _password;
}