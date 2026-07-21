#include "mfi_server/mfi_http_server.h"
#include "mfi/led.h"
#include <cmath>
#include <nlohmann/json.hpp>
#include "shmuelie/string_helpers.h"
#include "version_info.h"

using namespace std;
using namespace mfi;
using namespace shmuelie;
using json = nlohmann::json;

#define SERVER_HEADER "mfi-server/" PROJECT_VERSION

namespace {

	string json_escape(string const& str) {
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

	string const to_json(sensor sensor) {
		return "{\"power\":" + to_string(sensor.power()) + ",\"current\":" + to_string(sensor.current()) + ",\"voltage\":" + to_string(sensor.voltage()) + ",\"relay\":" + (sensor.relay() ? "true" : "false") + ",\"name\":\"" + json_escape(sensor.name()) + "\",\"label\":\"" + json_escape(sensor.label()) + "\"}";
	}

	string status_text(int code) {
		switch (code) {
		case 200: return "OK";
		case 400: return "Bad Request";
		case 404: return "Not Found";
		case 405: return "Method Not Allowed";
		default:  return "OK";
		}
	}

	string read_body(struct mg_connection* conn) {
		string body;
		char buf[1024];
		int read;
		while ((read = mg_read(conn, buf, sizeof(buf))) > 0) {
			body.append(buf, static_cast<size_t>(read));
		}
		return body;
	}

	void write_response(struct mg_connection* conn, mfi_http_server::response const& res) {
		string headers;
		bool has_server = false;
		for (auto const& [key, value] : res.headers) {
			headers += key + ": " + value + "\r\n";
			if (key == "Server") {
				has_server = true;
			}
		}
		if (!has_server) {
			headers += string("Server: ") + SERVER_HEADER + "\r\n";
		}
		mg_printf(conn,
			"HTTP/1.1 %d %s\r\n"
			"%s"
			"Content-Length: %zu\r\n"
			"Connection: close\r\n"
			"\r\n",
			res.status_code, status_text(res.status_code).c_str(),
			headers.c_str(), res.body.size());
		if (!res.body.empty()) {
			mg_write(conn, res.body.data(), res.body.size());
		}
	}

} // namespace

// ===================== CivetHandler adapters =====================

class mfi_http_server::info_handler : public CivetHandler {
public:
	explicit info_handler(mfi_http_server& server) : _server(server) {}
	bool handleGet(CivetServer*, struct mg_connection* conn) override {
		write_response(conn, _server.handle_info());
		return true;
	}
private:
	mfi_http_server& _server;
};

class mfi_http_server::sensor_list_handler : public CivetHandler {
public:
	explicit sensor_list_handler(mfi_http_server& server) : _server(server) {}
	bool handleGet(CivetServer*, struct mg_connection* conn) override {
		write_response(conn, _server.handle_sensor_list());
		return true;
	}
private:
	mfi_http_server& _server;
};

class mfi_http_server::sensor_item_handler : public CivetHandler {
public:
	explicit sensor_item_handler(mfi_http_server& server) : _server(server) {}
	bool handleGet(CivetServer* s, struct mg_connection* conn) override { return dispatch("GET", s, conn); }
	bool handlePost(CivetServer* s, struct mg_connection* conn) override { return dispatch("POST", s, conn); }
private:
	bool dispatch(string const& method, CivetServer*, struct mg_connection* conn) {
		auto const* info = mg_get_request_info(conn);
		string uri = info->local_uri ? info->local_uri : "";
		auto pos = uri.rfind('/');
		string id_str = (pos == string::npos) ? "" : uri.substr(pos + 1);
		auto id = shmuelie::try_stoul<uint8_t>(id_str);
		if (!id || *id < 1 || *id > _server.sensor_count()) {
			write_response(conn, { 404, { { "Server", SERVER_HEADER } }, "" });
			return true;
		}
		string body = (method == "POST") ? read_body(conn) : string{};
		write_response(conn, _server.handle_sensor_item(method, *id, body));
		return true;
	}
	mfi_http_server& _server;
};

class mfi_http_server::led_handler : public CivetHandler {
public:
	explicit led_handler(mfi_http_server& server) : _server(server) {}
	bool handleGet(CivetServer*, struct mg_connection* conn) override {
		write_response(conn, _server.handle_led("GET", ""));
		return true;
	}
	bool handlePost(CivetServer*, struct mg_connection* conn) override {
		write_response(conn, _server.handle_led("POST", read_body(conn)));
		return true;
	}
private:
	mfi_http_server& _server;
};

// ===================== mfi_http_server =====================

mfi_http_server::mfi_http_server() noexcept {
}

mfi_http_server::~mfi_http_server() = default;

size_t mfi_http_server::sensor_count() const noexcept {
	return _board.sensors().size();
}

bool mfi_http_server::listen(string const& listening_ports) {
	const char* options[] = {
		"listening_ports", listening_ports.c_str(),
		"num_threads", "2",
		nullptr
	};
	try {
		_server = make_unique<CivetServer>(options);
	}
	catch (CivetException const&) {
		return false;
	}
	if (!_server->getContext()) {
		_server.reset();
		return false;
	}

	_info_handler = make_unique<info_handler>(*this);
	_sensor_list_handler = make_unique<sensor_list_handler>(*this);
	_sensor_item_handler = make_unique<sensor_item_handler>(*this);
	_led_handler = make_unique<led_handler>(*this);

	// civetweb URI patterns (no std::regex). "$" anchors the exact /sensor
	// route so it doesn't shadow the /sensor/<id> prefix route.
	_server->addHandler("/api/v2/info", *_info_handler);
	_server->addHandler("/api/v2/sensor$", *_sensor_list_handler);
	_server->addHandler("/api/v2/sensor/", *_sensor_item_handler);
	_server->addHandler("/api/v2/led", *_led_handler);
	return true;
}

mfi_http_server::response mfi_http_server::handle_info() noexcept {
	return { 200,
		{ { "Content-Type", "application/json" } },
		"{\"hostName\":\"" + json_escape(_board.hostname()) + "\",\"modelName\":\"" + json_escape(_board.name()) + "\",\"modelId\":" + to_string(_board.id()) + ",\"sensorCount\":" + to_string(_board.sensors().size()) + "}" };
}

mfi_http_server::response mfi_http_server::handle_sensor_list() noexcept {
	vector<string> sensorsJson{};
	for (auto const& sensor : _board.sensors()) {
		sensorsJson.push_back(to_json(sensor));
	}
	return { 200,
		{ { "Content-Type", "application/json" } },
		"[" + join(sensorsJson.cbegin(), sensorsJson.cend(), ",") + "]" };
}

mfi_http_server::response mfi_http_server::handle_sensor_item(string const& method, uint8_t sensorId, string const& body) noexcept {
	sensor sensor = _board.sensors().at(sensorId - 1);
	if (method == "GET") {
		return { 200, { { "Content-Type", "application/json" } }, to_json(sensor) };
	}
	if (method == "POST") {
		if (body == "on" || body == "true" || body == "1") {
			sensor.relay(true);
			return { 200, {}, "" };
		}
		if (body == "off" || body == "false" || body == "0") {
			sensor.relay(false);
			return { 200, {}, "" };
		}
		return { 400, {}, "Unexpected body " + body };
	}
	return { 405, {}, "Unexpected method " + method };
}

mfi_http_server::response mfi_http_server::handle_led(string const& method, string const& body) noexcept {
	led l{};
	if (method == "GET") {
		return { 200,
			{ { "Content-Type", "application/json" } },
			"{\"color\":" + to_string(static_cast<int>(l.color())) + ",\"frequency\":" + to_string(l.frequency()) + "}" };
	}
	if (method == "POST") {
		json parsed;
		try {
			parsed = json::parse(body);
		}
		catch (json::exception const&) {
			return { 400, {}, "Invalid JSON body" };
		}

		if (parsed.contains("color")) {
			auto const& color_val = parsed["color"];
			if (!color_val.is_number()) {
				return { 400, {}, "Invalid color" };
			}
			double color = color_val.get<double>();
			double intpart;
			if (modf(color, &intpart) != 0.0) {
				return { 400, {}, "Invalid color " + to_string(color) };
			}
			int color_int = static_cast<int>(intpart);
			if (color_int < 0 || color_int > 4) {
				return { 400, {}, "Color must be between 0 and 4" };
			}
			l.color(static_cast<led_color>(color_int));
		}

		if (parsed.contains("frequency")) {
			auto const& freq_val = parsed["frequency"];
			if (!freq_val.is_number()) {
				return { 400, {}, "Invalid frequency" };
			}
			double frequency = freq_val.get<double>();
			double intpart;
			if (modf(frequency, &intpart) != 0.0 || intpart < 0) {
				return { 400, {}, "Invalid frequency " + to_string(frequency) };
			}
			l.frequency(static_cast<uint32_t>(intpart));
		}

		return { 200, {}, "" };
	}
	return { 405, {}, "Unexpected method " + method };
}
