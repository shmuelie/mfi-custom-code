#include "options.h"
#include "string_helpers.h"
#include "docopt.h"
#include "mongoose.h"

using namespace std;
using namespace docopt;
using namespace string_helpers;

static bool validate_ip(const string& ip)
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

options::options(const string& doc, const vector<string>& argv) noexcept {
	try {
		auto args = docopt_parse(doc, argv);

		auto& ip = args.at("--ip").asString();
		if (!validate_ip(ip)) {
			_errors.push_back("Invalid IP address");
		}
		else {
			_ip = ip;
		}

		auto potentialPort = try_stoul<uint16_t>(args.at("--port").asString());
		if (!potentialPort) {
			_errors.push_back("Invalid port");
		}
		else {
			_port = potentialPort.value();
		}

		auto potentialLogLevel = try_stoul<uint8_t>(args.at("--log-level").asString());
		if (!potentialLogLevel || potentialLogLevel.value() > MG_LL_VERBOSE) {
			_errors.push_back("Invalid log level");
		}
		else {
			_logLevel = potentialLogLevel.value();
		}
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

const string& options::ip() const noexcept {
	return _ip;
}

uint16_t options::port() const noexcept {
	return _port;
}

uint8_t options::log_level() const noexcept {
	return _logLevel;
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