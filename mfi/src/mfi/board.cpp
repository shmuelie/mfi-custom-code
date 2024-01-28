#include "mfi/board.h"
#include "mfi/sensor.h"

#include <iostream>
#include <fstream>

using namespace std;
using namespace mfi;

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

	for (uint8_t sensorId = 1; sensorId <= sensorCount; sensorId++) {
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