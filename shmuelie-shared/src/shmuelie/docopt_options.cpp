#include "shmuelie/docopt_options.h"
#include "shmuelie/string_helpers.hpp"

using namespace std;
using namespace docopt;
using namespace shmuelie;

docopt_options::docopt_options(string const& doc, vector<string> const& argv) noexcept {
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

vector<string> const& docopt_options::errors() const noexcept {
	return _errors;
}

void docopt_options::add_error(string const& error_str) noexcept {
	_errors.push_back(error_str);
}

optional<string> const docopt_options::get_string(string const& key) const noexcept {
	auto const value = _values.find(key);
	if (value != _values.end()) {
		return value->second.asString();
	}
	return nullopt;
}

optional<uint16_t> docopt_options::get_uint16(string const& key) const noexcept {
	auto const value = _values.find(key);
	if (value != _values.end()) {
		return try_stoul<uint16_t>(value->second.asString());
	}
	return nullopt;
}

optional<int> docopt_options::get_int(string const& key) const noexcept {
	auto const value = _values.find(key);
	if (value != _values.end()) {
		return try_stoi<int>(value->second.asString());
	}
	return nullopt;
}

optional<uint8_t> docopt_options::get_uint8(string const& key) const noexcept {
	auto const value = _values.find(key);
	if (value != _values.end()) {
		return try_stoul<uint8_t>(value->second.asString());
	}
	return nullopt;
}