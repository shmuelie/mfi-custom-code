#include "mfi_cli/sensor_command.h"
#include "mfi_cli/helpers.h"
#include "mfi.h"
#include <iostream>

using namespace std;
using namespace CLI;
using namespace mfi_cli;
using namespace mfi;

namespace {
	void print_sensor(sensor const& s) {
		cout
			<< "ID: " << to_string(s.id()) << "\n"
			<< "Name: " << s.name() << "\n"
			<< "Label: " << s.label() << "\n"
			<< "Power: " << s.power() << "W\n"
			<< "Current: " << s.current() << "A\n"
			<< "Voltage: " << s.voltage() << "V\n"
			<< "Power Factor: " << s.power_factor() << "\n"
			<< "Relay: " << (s.relay() ? "On" : "Off") << endl;
	}
}

sensor_command::sensor_command(App& app) : _id{ 0 } {
	auto command = app.add_subcommand("sensor", "Query sensor/port readings");
	command->require_subcommand();

	command->
		add_subcommand("list", "List all sensors and their readings")->
		callback([this]() { list(); });

	auto get_command = command->add_subcommand("get", "Display readings for a single sensor");
	get_command->add_option("id", _id, "The sensor ID to query")->required();
	get_command->callback([this]() { get(); });
}

void sensor_command::list() {
	board b;
	auto const& sensors = b.sensors();
	if (sensors.empty()) {
		cout << "No sensors on this device." << endl;
		return;
	}
	bool first = true;
	for (auto const& s : sensors) {
		if (!first) {
			cout << "------------------" << endl;
		}
		first = false;
		print_sensor(s);
	}
}

void sensor_command::get() {
	board b;
	auto s = find_sensor(b, static_cast<uint8_t>(_id));
	if (!s) {
		throw ValidationError("No sensor with ID " + to_string(_id));
	}
	print_sensor(*s);
}
