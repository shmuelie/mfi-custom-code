#include "mfi_cli/config_command.h"
#include "mfi/config.h"
#include <iostream>
#include <stdexcept>

using namespace std;
using namespace CLI;
using namespace mfi_cli;
using namespace mfi;

config_command::config_command(App& app) : _file{}, _key{}, _default{} {
	auto command = app.add_subcommand("config", "Read INI-style configuration files");
	command->require_subcommand();

	auto list_command = command->add_subcommand("list", "List all key=value pairs in a file");
	list_command->add_option("file", _file, "The configuration file to read")->required();
	list_command->callback([this]() { list(); });

	auto get_command = command->add_subcommand("get", "Read a single value from a file");
	get_command->add_option("file", _file, "The configuration file to read")->required();
	get_command->add_option("key", _key, "The key to look up")->required();
	auto default_option = get_command->add_option("-d,--default", _default,
		"Value to return if the key is missing or the file cannot be read");
	get_command->callback([this, default_option]() { get(default_option); });
}

void config_command::list() {
	try {
		auto values = config::read_all(_file);
		for (auto const& [key, value] : values) {
			cout << key << "=" << value << endl;
		}
	}
	catch (runtime_error const&) {
		throw ValidationError("Unable to read file: " + _file);
	}
}

void config_command::get(Option const* default_option) {
	if (default_option->count() > 0) {
		cout << config::read(_file, _key, _default) << endl;
		return;
	}
	try {
		cout << config::read(_file, _key) << endl;
	}
	catch (runtime_error const&) {
		throw ValidationError("Unable to read file: " + _file);
	}
}
