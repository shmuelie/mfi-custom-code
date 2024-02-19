#include "mfi/sensor.h"
#include "mfi/config.h"

#include <iostream>
#include <fstream>

using namespace std;
using namespace mfi;

const string root{ "/proc/power/" };
const string power_path{ root + "active_pwr" };
const string current_path{ root + "i_rms" };
const string voltage_path{ root + "v_rms" };
const string power_factor_path{ root + "pf" };
const string relay_path{ root + "relay" };
const string config_file{ "/etc/persistent/cfg/config_file" };

sensor::sensor(uint8_t id) : _id(id) {
}

uint8_t sensor::id() const {
	return _id;
}

double sensor::read(const string& path) const {
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

const string sensor::name() const {
	try {
		return config::read(config_file, "port." + to_string(_id) + ".sensorId");
	}
	catch (runtime_error& e)
	{
		return string{};
	}
}

const string sensor::label() const {
	try {
		return config::read(config_file, "port." + to_string(_id) + ".label");
	}
	catch (runtime_error& e)
	{
		return string{};
	}
}