#include "mfi_server.h"
#include "join.h"

using namespace std;
using namespace mg;
using namespace mfi;

mfi_server::mfi_server() noexcept : http_server() {
}

static const string to_json(sensor sensor) {
	return "{\"power\":" + to_string(sensor.power()) + ",\"current\":" + to_string(sensor.current()) + ",\"voltage\":" + to_string(sensor.voltage()) + ",\"relay\":" + (sensor.relay() ? "true" : "false") + "}";
}

http_response mfi_server::status_handler() noexcept {
	const vector<sensor>& sensors = _board.sensors();
	vector<string> sensorsJson{};
	for (sensor sensor : _board.sensors()) {
		sensorsJson.push_back(to_json(sensor));
	}
	return { 200, map<string, string>{
		{"Content-Type", "application/json"}
	}, ("[" + join(sensorsJson.cbegin(), sensorsJson.cend(), ",") + "]") };
}

http_response mfi_server::sensor_handler(const string& method, const vector<string>& captures, const string& body) noexcept {
	if (captures.size() != 1) {
		return { 400, "Invalid path" };
	}

	uint8_t sensorId;
	try {
		sensorId = static_cast<uint8_t>(stoul(captures.at(0), nullptr, 10));
	}
	catch (invalid_argument& ex) {
		return { 400, ex.what() };
	}

	if (sensorId >= _board.sensors().size()) {
		return { 400, "Invalid Sensor ID" };
	}

	sensor sensor = _board.sensors().at(sensorId);
	if (method == "GET") {
		return { 200, map<string, string>{
			{"Content-Type", "application/json"}
		}, to_json(sensor) };
	}
	else if (method == "POST") {
		if (body == "on") {
			sensor.relay(true);
			return 200;
		}
		else if (body == "off") {
			sensor.relay(false);
			return 200;
		}
		else {
			return { 400, "Unexpected body " + body };
		}
	}
	else {
		return { 405, "Unexpected method " + method };
	}
}

http_response mfi_server::http_handler(const http_message& message) noexcept {
	vector<string> captures{};
	if (message.match_uri("/api/v2/status")) {
		return status_handler();
	}
	else if (message.match_uri<1>("/api/v2/sensor/*", captures)) {
		return sensor_handler(message.method(), captures, message.body());
	}
	else {
		return 404;
	}
}