#include "mfi_server/mfi_http_server.h"
#include "mfi/led.h"
#include <cmath>
#include "string_helpers.h"
#include <regex>

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

http_response mfi_http_server::sensor_handler(const string& method, const string& sensorId_, const string& body) noexcept {
	auto sensorId = try_stoul<uint8_t>(sensorId_, nullptr, 10);

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

#define STR_(S) #S
#define SERVER(V) "mfi-server/" STR_(V)

static http_response add_server_headers(const http_response& response) {
	map<string, string> headers{ response.headers() };
	headers.emplace("Server", SERVER(MFI_SERVER_VERSION));
	return { response.status_code(), headers, response.body() };
}

http_response mfi_http_server::http_handler(const http_message& message) noexcept {
	const regex uriRegex{ "^\\/api\\/v2\\/(sensor(?:\\/(0|1|2))?|status|led)$" };
	smatch uriMatch{};
	const string& uri = message.uri();
	if (regex_match(uri, uriMatch, uriRegex)) {
		const string primaryPath = uriMatch[1].str();
		if (primaryPath == "status") {
			auto response = status_handler();
			return add_server_headers(response);
		}
		else if (primaryPath == "led") {
			auto response = led_handler(message.method(), message.body());
			return add_server_headers(response);
		}

		// Must be sensor
		const string sensorId = uriMatch[2].str();
		auto response = sensor_handler(message.method(), sensorId, message.body());
		return add_server_headers(response);
	}

	return { 404, map<string, string>{
		{ "Server", SERVER(MFI_SERVER_VERSION) }
	} };
}