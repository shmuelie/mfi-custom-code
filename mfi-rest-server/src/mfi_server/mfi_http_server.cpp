#include "mfi_server/mfi_http_server.h"
#include "mfi/led.h"
#include <cmath>
#include "shmuelie/string_helpers.h"
#include "mg/logger.h"

using namespace std;
using namespace mg;
using namespace mfi;
using namespace shmuelie;

mfi_http_server::mfi_http_server() noexcept : http_server() {
}

static string json_escape(string const& str) {
	string result;
	result.reserve(str.size());
	for (char c : str) {
		switch (c) {
		case '"':  result += "\\\""; break;
		case '\\': result += "\\\\"; break;
		case '\n': result += "\\n"; break;
		case '\r': result += "\\r"; break;
		case '\t': result += "\\t"; break;
		default:   result += c; break;
		}
	}
	return result;
}

static string const to_json(sensor sensor) {
	return "{\"power\":" + to_string(sensor.power()) + ",\"current\":" + to_string(sensor.current()) + ",\"voltage\":" + to_string(sensor.voltage()) + ",\"relay\":" + (sensor.relay() ? "true" : "false") + ",\"name\":\"" + json_escape(sensor.name()) + "\",\"label\":\"" + json_escape(sensor.label()) + "\"}";
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

http_response mfi_http_server::sensor_handler(string const& method, uint8_t sensorId, string const& body) noexcept {
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

http_response mfi_http_server::led_handler(string const& method, string const& body) noexcept {
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
			int color_int = static_cast<int>(intpart);
			if (color_int < 0 || color_int > 4) {
				return { 400, "Color must be between 0 and 4" };
			}

			l.color(static_cast<led_color>(color_int));
		}

		double frequency;
		if (mg_json_get_num(json, "$.frequency", &frequency)) {
			if (modf(frequency, &intpart) != 0.0 || intpart < 0) {
				return { 400, "Invalid frequency " + to_string(frequency) };
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
	}, "{\"hostName\":\"" + json_escape(_board.hostname()) + "\",\"modelName\":\"" + json_escape(_board.name()) + "\",\"modelId\":" + to_string(_board.id()) + ",\"sensorCount\":" + to_string(_board.sensors().size()) + "}"};
}

#define STR_(S) #S
#define SERVER(V) "mfi-server/" STR_(V)

static http_response add_server_headers(http_response const& response) {
	map<string, string> headers{ response.headers() };
	headers.emplace("Server", SERVER(MFI_REST_SERVER_VERSION));
	return { response.status_code(), headers, response.body() };
}

static constexpr const char* API_PREFIX = "/api/v2/";
static constexpr size_t API_PREFIX_LEN = 8;

http_response mfi_http_server::http_handler(http_message const& message) noexcept {
	string const& uri = message.uri();

	if (uri.compare(0, API_PREFIX_LEN, API_PREFIX) != 0) {
		return { 404, map<string, string>{
			{ "Server", SERVER(MFI_REST_SERVER_VERSION) }
		} };
	}

	string path = uri.substr(API_PREFIX_LEN);

	if (path == "led") {
		return add_server_headers(led_handler(message.method(), message.body()));
	}
	if (path == "info") {
		return add_server_headers(info_handler());
	}
	if (path == "sensor") {
		return add_server_headers(sensor_handler());
	}
	if (path.compare(0, 7, "sensor/") == 0) {
		string idStr = path.substr(7);
		auto sensorId = shmuelie::try_stoul<uint8_t>(idStr);
		if (sensorId && *sensorId >= 1 && *sensorId <= _board.sensors().size()) {
			return add_server_headers(sensor_handler(message.method(), *sensorId, message.body()));
		}
	}

	return { 404, map<string, string>{
		{ "Server", SERVER(MFI_REST_SERVER_VERSION) }
	} };
}