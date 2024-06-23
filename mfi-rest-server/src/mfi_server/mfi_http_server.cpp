#include "mfi_server/mfi_http_server.h"
#include "mfi/led.h"
#include <cmath>
#include "string_helpers.h"
#include "mg/logger.h"

using namespace std;
using namespace mg;
using namespace mfi;
using namespace string_helpers;

mfi_http_server::mfi_http_server() noexcept : http_server() {
	string v2Regex{ "^\\/api\\/v2\\/(sensor(?:\\/(" };

	size_t sensorCount = _board.sensors().size();
	for (size_t i = 1; i <= sensorCount; i++) {
		v2Regex += to_string(i);
		if (i < sensorCount) {
			v2Regex += "|";
		}
	}

	v2Regex += "))?|led|info)$";

	logger::verbose("Final Regex: %s", make_tuple(v2Regex.c_str()));

	_v2Regex.assign(v2Regex);
}

static const string to_json(sensor sensor) {
	return "{\"power\":" + to_string(sensor.power()) + ",\"current\":" + to_string(sensor.current()) + ",\"voltage\":" + to_string(sensor.voltage()) + ",\"relay\":" + (sensor.relay() ? "true" : "false") + ",\"name\":\"" + sensor.name() + "\",\"label\":\"" + sensor.label() + "\"}";
}

http_response mfi_http_server::sensor_handler() noexcept {
	const vector<sensor>& sensors = _board.sensors();
	vector<string> sensorsJson{};
	for (sensor sensor : _board.sensors()) {
		sensorsJson.push_back(to_json(sensor));
	}
	return { map<string, string>{
		{"Content-Type", "application/json"}
	}, ("[" + join(sensorsJson.cbegin(), sensorsJson.cend(), ",") + "]") };
}

http_response mfi_http_server::sensor_handler(const string& method, uint8_t sensorId, const string& body) noexcept {
	sensor sensor = _board.sensors().at(sensorId - 1);
	if (method == "GET") {
		return { map<string, string>{
			{"Content-Type", "application/json"}
		}, to_json(sensor) };
	}
	else if (method == "POST") {
		if (body == "on" || body == "true" || body == "1") {
			sensor.relay(true);
			return 200;
		}
		else if (body == "off" || body == "false" || body == "0") {
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

http_response mfi_http_server::info_handler() noexcept {
	return { map<string, string>{
		{"Content-Type", "application/json"}
	}, "{\"hostName\":\"" + _board.hostname() + "\",\"modelName\":\"" + _board.name() + "\",\"modelId\":" + to_string(_board.id()) + ",\"sensorCount\":" + to_string(_board.sensors().size()) + "}"};
}

#define STR_(S) #S
#define SERVER(V) "mfi-server/" STR_(V)

static http_response add_server_headers(const http_response& response) {
	map<string, string> headers{ response.headers() };
	headers.emplace("Server", SERVER(MFI_REST_SERVER_VERSION));
	return { response.status_code(), headers, response.body() };
}

http_response mfi_http_server::http_handler(const http_message& message) noexcept {
	smatch uriMatch{};
	const string& uri = message.uri();
	if (regex_match(uri, uriMatch, _v2Regex)) {
		const string primaryPath = uriMatch[1].str();
		if (primaryPath == "led") {
			auto response = led_handler(message.method(), message.body());
			return add_server_headers(response);
		}
		else if (primaryPath == "info") {
			auto response = info_handler();
			return add_server_headers(response);
		}
		else {
			if (uriMatch[2].matched) {
				auto response = sensor_handler(message.method(), static_cast<uint8_t>(stoul(uriMatch[2].str())), message.body());
				return add_server_headers(response);
			}
			else {
				auto response = sensor_handler();
				return add_server_headers(response);
			}
		}
	}

	return { 404, map<string, string>{
		{ "Server", SERVER(MFI_SERVER_VERSION) }
	} };
}