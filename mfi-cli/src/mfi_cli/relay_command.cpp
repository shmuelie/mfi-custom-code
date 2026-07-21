#include "mfi_cli/relay_command.h"
#include "mfi_cli/helpers.h"
#include "mfi.h"
#include <iostream>

using namespace std;
using namespace CLI;
using namespace mfi_cli;
using namespace mfi;

namespace {
	sensor require_sensor(uint16_t id) {
		board b;
		auto s = find_sensor(b, static_cast<uint8_t>(id));
		if (!s) {
			throw ValidationError("No sensor with ID " + to_string(id));
		}
		return *s;
	}
}

relay_command::relay_command(App& app) : _id{ 0 }, _state{} {
	auto command = app.add_subcommand("relay", "Read and control port relays");
	command->require_subcommand();

	auto get_command = command->add_subcommand("get", "Display the relay state for a sensor");
	get_command->add_option("id", _id, "The sensor ID to query")->required();
	get_command->callback([this]() { get(); });

	auto set_command = command->add_subcommand("set", "Set the relay state for a sensor");
	set_command->add_option("id", _id, "The sensor ID to control")->required();
	set_command->add_option("state", _state, "The relay state to set")->required()->
		check(IsMember({ "on", "off" }));
	set_command->callback([this]() { set(_state == "on"); });

	auto on_command = command->add_subcommand("on", "Turn a sensor's relay on");
	on_command->add_option("id", _id, "The sensor ID to control")->required();
	on_command->callback([this]() { set(true); });

	auto off_command = command->add_subcommand("off", "Turn a sensor's relay off");
	off_command->add_option("id", _id, "The sensor ID to control")->required();
	off_command->callback([this]() { set(false); });

	auto toggle_command = command->add_subcommand("toggle", "Toggle a sensor's relay");
	toggle_command->add_option("id", _id, "The sensor ID to control")->required();
	toggle_command->callback([this]() { toggle(); });
}

void relay_command::get() {
	auto s = require_sensor(_id);
	cout << (s.relay() ? "On" : "Off") << endl;
}

void relay_command::set(bool value) {
	auto s = require_sensor(_id);
	s.relay(value);
	cout << "Sensor " << to_string(_id) << " relay set to " << (value ? "On" : "Off") << endl;
}

void relay_command::toggle() {
	auto s = require_sensor(_id);
	bool value = !s.relay();
	s.relay(value);
	cout << "Sensor " << to_string(_id) << " relay toggled to " << (value ? "On" : "Off") << endl;
}
