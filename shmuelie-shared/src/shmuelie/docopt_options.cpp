#include "shmuelie/docopt_options.h"

using namespace std;
using namespace docopt;
using namespace shmuelie;

docopt_options::docopt_options(const string& doc, const vector<string>& argv) noexcept {
	try {
		_values = docopt_parse(doc, argv);
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
	catch (exception const& error) {
		_errors.push_back(error.what());
	}
}

bool docopt_options::help() const noexcept {
	return _help;
}

bool docopt_options::version() const noexcept {
	return _version;
}

const vector<string>& docopt_options::errors() const noexcept {
	return _errors;
}

const map<string, value>& docopt_options::values() const noexcept {
	return _values;
}

void docopt_options::add_error(const string& error_str) noexcept {
	_errors.push_back(error_str);
}