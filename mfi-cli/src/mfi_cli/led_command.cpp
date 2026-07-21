#include "mfi_cli/led_command.h"
#include "mfi_cli/helpers.h"
#include "mfi.h"
#include <iostream>

using namespace std;
using namespace CLI;
using namespace mfi_cli;
using namespace mfi;

led_command::led_command(App& app) : _color{ led_color::off }, _frequency{ 0 } {
	auto command = app.add_subcommand("led", "Read and control the device LED");
	command->require_subcommand();

	command->
		add_subcommand("get", "Display the LED color and frequency")->
		callback([this]() { get(); });

	auto set_command = command->add_subcommand("set", "Set the LED color and/or frequency");
	CheckedTransformer color_transform{
		map<string, led_color>{
			{ "off", led_color::off },
			{ "blue", led_color::blue },
			{ "yellow", led_color::yellow },
			{ "both", led_color::both },
			{ "alternate", led_color::alternate }
		}
	};
	set_command->add_option("-c,--color", _color, "The LED color/style to set")->transform(color_transform);
	set_command->add_option("-f,--frequency", _frequency,
		"The alternate frequency (used when color is alternate)");
	set_command->callback([this, set_command]() { set(*set_command); });
}

void led_command::get() {
	led l;
	cout
		<< "Color: " << format_led_color(l.color()) << "\n"
		<< "Frequency: " << l.frequency() << endl;
}

void led_command::set(App const& set_command) {
	bool has_color = set_command.count("--color") > 0;
	bool has_frequency = set_command.count("--frequency") > 0;
	if (!has_color && !has_frequency) {
		throw ValidationError("led set requires at least one of --color or --frequency");
	}
	led l;
	if (has_color) {
		l.color(_color);
		cout << "LED color set to " << format_led_color(_color) << endl;
	}
	if (has_frequency) {
		l.frequency(_frequency);
		cout << "LED frequency set to " << _frequency << endl;
	}
}
