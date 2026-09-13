#include "mfi/sensor.h"
#include "mfi/config.h"

#include <iostream>
#include <fstream>
#include <cmath>
#include <charconv>
#include <string_view>

using namespace std;
using namespace mfi;

string const root{
#ifdef __TARGET_mips__
	"/proc/power/"
#else
	"./proc/power/"
#endif
};
string const config_file{
#ifdef __TARGET_mips__
	"/etc/persistent/cfg/config_file"
#else
	"./etc/persistent/cfg/config_file"
#endif
};

string const power_path{ root + "active_pwr" };
string const current_path{ root + "i_rms" };
string const voltage_path{ root + "v_rms" };
string const power_factor_path{ root + "pf" };
string const relay_path{ root + "relay" };

sensor::sensor(uint8_t id) : _id(id),
	_name(config::read(config_file, "port." + to_string(id - 1) + ".sensorId", "")),
	_label(config::read(config_file, "port." + to_string(id - 1) + ".label", "")) {
}

uint8_t sensor::id() const {
	return _id;
}

double sensor::read(string const& path) const {
	ifstream stream{ path + to_string(_id) };
	double value = 0;
	stream >> value;
	return value;
}

sensor_read_result sensor::read_checked(string const& path) const {
	ifstream stream{ path + to_string(_id) };
	if (!stream.is_open()) {
		return sensor_read_error::open_failed;
	}
	// Bound input independently of procfs so a damaged file cannot exhaust memory.
	string text;
	char ch;
	while (stream.get(ch)) {
		if (text.size() == 256) {
			return sensor_read_error::invalid_number;
		}
		text += ch;
	}
	if (stream.bad() || !stream.eof()) {
		return sensor_read_error::read_failed;
	}
	auto first = text.find_first_not_of(" \t\r\n\f\v");
	if (first == string::npos) {
		return sensor_read_error::invalid_number;
	}
	auto last = text.find_last_not_of(" \t\r\n\f\v");
	string_view number(text.data() + first, last - first + 1);
	if (number.front() == '+') {
		number.remove_prefix(1);
		if (number.empty() || number.front() == '+' || number.front() == '-') {
			return sensor_read_error::invalid_number;
		}
	}
	double value;
	auto parsed = from_chars(number.data(), number.data() + number.size(), value);
	if (parsed.ec != errc{} || parsed.ptr != number.data() + number.size()) {
		return sensor_read_error::invalid_number;
	}
	if (!isfinite(value)) {
		return sensor_read_error::nonfinite;
	}
	return value;
}

char const* mfi::describe(sensor_read_error error) noexcept {
	switch (error) {
	case sensor_read_error::open_failed: return "could not open measurement";
	case sensor_read_error::read_failed: return "could not read measurement";
	case sensor_read_error::invalid_number: return "invalid numeric measurement";
	case sensor_read_error::nonfinite: return "nonfinite measurement";
	}
	return "unknown measurement error";
}

sensor_read_result sensor::power_checked() const { return read_checked(power_path); }
sensor_read_result sensor::current_checked() const { return read_checked(current_path); }
sensor_read_result sensor::voltage_checked() const { return read_checked(voltage_path); }
sensor_read_result sensor::power_factor_checked() const { return read_checked(power_factor_path); }

double sensor::power() const {
	return read(power_path);
}

double sensor::current() const {
	return read(current_path);
}

double sensor::voltage() const {
	return read(voltage_path);
}

double sensor::power_factor() const {
	return read(power_factor_path);
}

bool sensor::relay() const {
	ifstream stream{ relay_path + to_string(_id) };
	int value = 0;
	stream >> value;
	return value == 1;
}

void sensor::relay(bool value) const {
	ofstream stream{ relay_path + to_string(_id), ios::out };
	stream << (value ? 1 : 0);
}

string const& sensor::name() const {
	return _name;
}

string const& sensor::label() const {
	return _label;
}