#include "mfi/sensor.h"
#include "mfi/config.h"

#include <iostream>
#include <fstream>

using namespace std;
using namespace mfi;

string const root{ "/proc/power/" };
string const power_path{ root + "active_pwr" };
string const current_path{ root + "i_rms" };
string const voltage_path{ root + "v_rms" };
string const power_factor_path{ root + "pf" };
string const relay_path{ root + "relay" };
string const config_file{ "/etc/persistent/cfg/config_file" };

sensor::sensor(uint8_t id) : _id(id) {
}

uint8_t sensor::id() const {
	return _id;
}

double sensor::read(string const& path) const {
	ifstream stream{ path + to_string(_id) };
	double value;
	stream >> value;
	return value;
}

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
	int value;
	stream >> value;
	return value == 1;
}

void sensor::relay(bool value) const {
	ofstream stream{ relay_path + to_string(_id), ios::out };
	stream << (value ? 1 : 0);
}

string const sensor::name() const {
	return config::read(config_file, "port." + to_string(_id) + ".sensorId", "");
}

string const sensor::label() const {
	return config::read(config_file, "port." + to_string(_id) + ".label", "");
}