#include "mfi/board_info.h"
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

board::board() {
	ifstream stream{ "/etc/board.info" };
	for (string line; getline(stream, line);) {
		auto equalityIndex = line.find('=');
		if (equalityIndex == -1)
		{
			continue;
		}

		auto key = line.substr(0, equalityIndex);
		auto value = line.substr(equalityIndex + 1);

		if (key == "board.name") {
			_name = value;
		}
		else if (key == "board.shortname") {
			_shortName = value;
		}
		else if (key == "board.sysid") {
			_id = static_cast<uint16_t>(stoul(value, nullptr, 16));
		}
	}

	uint8_t sensorCount = 0;
	switch (_id) {
	case 0xe648:
		sensorCount = 8;
		break;
	case 0xe671:
		sensorCount = 1;
		break;
	}

	for (uint8_t sensorId = 0; sensorId < sensorCount; sensorId++) {
		_sensors.push_back(sensor{ sensorId });
	}
}

const string& board::name() const {
	return _name;
}

const string& board::short_name() const {
	return _shortName;
}

uint16_t board::id() const {
	return _id;
}

const std::vector<sensor>& board::sensors() const {
	return _sensors;
}