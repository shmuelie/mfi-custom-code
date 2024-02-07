#include "mfi_http_server.h"
#include "mfi/led.h"
#include <cmath>
#include "string_helpers.h"

using namespace std;
using namespace mg;
using namespace mfi;
using namespace string_helpers;

mfi_http_server::mfi_http_server() noexcept : http_server() {
}

static const string to_json(sensor sensor) {
	return "{\"power\":" + to_string(sensor.power()) + ",\"current\":" + to_string(sensor.current()) + ",\"voltage\":" + to_string(sensor.voltage()) + ",\"relay\":" + (sensor.relay() ? "true" : "false") + "}";
}

http_response mfi_http_server::status_handler() noexcept {
	const vector<sensor>& sensors = _board.sensors();
	vector<string> sensorsJson{};
	for (sensor sensor : _board.sensors()) {
		sensorsJson.push_back(to_json(sensor));
	}
	return { map<string, string>{
		{"Content-Type", "application/json"}
	}, ("[" + join(sensorsJson.cbegin(), sensorsJson.cend(), ",") + "]") };
}

http_response mfi_http_server::sensor_handler(const string& method, const vector<string>& captures, const string& body) noexcept {
	if (captures.size() != 1) {
		return { 400, "Invalid path" };
	}

	auto sensorId = try_stoul<uint8_t>(captures.at(0), nullptr, 10);

	if (!sensorId.has_value() || sensorId.value() > _board.sensors().size()) {
		return { 400, "Invalid Sensor ID" };
	}

	sensor sensor = _board.sensors().at(sensorId.value() - 1);
	if (method == "GET") {
		return { map<string, string>{
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

http_response mfi_http_server::led_handler(const string& method, const string& body) noexcept {
	led l{};
	if (method == "GET") {
		return { map<string, string>{
			{"Content-Type", "application/json"}
		}, "{\"color\":" + to_string(static_cast<int>(l.color())) + ",\"frequency\":" + to_string(l.frequency()) + "}" };
	}
	else if (method == "POST") {
		mg_str json = mg_str_n(body.c_str(), body.length());
		double intpart;
		double color;
		if (mg_json_get_num(json, "$.color", &color)) {
			if (modf(color, &intpart) != 0.0) {
				return { 400, "Invalid color " + to_string(color) };
			}

			l.color(static_cast<led_color>(static_cast<int>(intpart)));
		}

		double frequency;
		if (mg_json_get_num(json, "$.frequency", &frequency)) {
			if (modf(frequency, &intpart) != 0.0) {
				return { 400, "Invalid frequencey " + to_string(frequency) };
			}

			l.frequency(static_cast<uint32_t>(intpart));
		}

		return 200;
	}
	else {
		return { 405, "Unexpected method " + method };
	}
}

http_response mfi_http_server::http_handler(const http_message& message) noexcept {
	vector<string> captures{};
	if (message.match_uri("/api/v2/status")) {
		return status_handler();
	}
	else if (message.match_uri<1>("/api/v2/sensor/*", captures)) {
		return sensor_handler(message.method(), captures, message.body());
	}
	else if (message.match_uri("/api/v2/led")) {
		return led_handler(message.method(), message.body());
	}
	else {
		return 404;
	}
}