#include "mfi/board.h"
#include "mfi/sensor.h"
#include "mfi/config.h"

#include <unistd.h>
#include <iostream>
#include <fstream>

using namespace std;
using namespace mfi;

board::board() {
	auto boardInfo = config::read_all("/etc/board.info");
	_name = boardInfo.at("board.name");
	_shortName = boardInfo.at("board.shortname");
	_id = static_cast<uint16_t>(stoul(boardInfo.at("board.sysid"), nullptr, 16));

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

	ifstream versionStream{ "/etc/version" };
	getline(versionStream, _version);

	char hostname[1024]{};
	hostname[1023] = '\0';
	gethostname(hostname, 1023);
	_hostname = string{ hostname };
}

string const& board::name() const {
	return _name;
}

string const& board::short_name() const {
	return _shortName;
}

uint16_t board::id() const {
	return _id;
}

std::vector<sensor> const& board::sensors() const {
	return _sensors;
}

string const& board::version() const {
	return _version;
}

string const& board::hostname() const {
	return _hostname;
}