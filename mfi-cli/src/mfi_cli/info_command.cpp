#include "mfi_cli/info_command.h"
#include "mfi.h"
#include <iostream>

using namespace std;
using namespace CLI;
using namespace mfi_cli;
using namespace mfi;

info_command::info_command(App& app) : _all_option{ false } {
	auto command = app.
		add_subcommand("info", "Display information about the mFi device")->
		callback([this]() { run(); });
	command->add_flag("--all, -a", _all_option, "Display all information")->default_val(false);
}

void info_command::run() {
	board b;
	cout
		<< "Device information\n"
		<< "------------------\n"
		<< "ID: " << b.id() << "\n"
		<< "Name: " << b.name() << "\n"
		<< "Short Name: " << b.short_name() << "\n"
		<< "Host Name: " << b.hostname() << "\n"
		<< "Firmware Version: " << b.version() << endl;
	if (_all_option) {
		cout << "Sensors:" << endl;
		for (auto& s : b.sensors()) {
			cout
				<< "\tID: " << to_string(s.id()) << "\n"
				<< "\tName: " << s.name() << "\n"
				<< "\tLabel: " << s.label() << "\n"
				<< "\tPower: " << s.power() << "W\n"
				<< "\tCurrent: " << s.current() << "A\n"
				<< "\tVoltage: " << s.voltage() << "V\n"
				<< "\tPower Factor: " << s.power_factor() << "\n"
				<< "\tRelay: " << (s.relay() ? "On" : "Off") << "\n" << endl;
		}
	}
}