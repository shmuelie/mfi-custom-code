#include "mg/connection.h"
#include <vector>
#include "string_helpers.h"
#include "mg/logger.h"

using namespace std;
using namespace mg;

connection::connection(mg_connection* c) noexcept : _connection(c) {
}

void connection::reply(const http_response& response) const noexcept {
	const char* headers = nullptr;
	vector<string> headersVector{};
	for (const map<string, string>::const_iterator::reference pair : response.headers()) {
		headersVector.push_back(pair.first + ": " + pair.second);
	}
	if (headersVector.size() > 0) {
		headers = string_helpers::join(headersVector.cbegin(), headersVector.cend(), "\r\n", "\r\n").c_str();
	}
	logger::verbose("Responding to %M with status %d", make_tuple(mg_print_ip_port, &_connection->rem, response.status_code()));
	mg_http_reply(_connection, response.status_code(), headers, response.body().c_str());
}

void connection::publish(const mqtt_options& options) noexcept {
	const mg_mqtt_opts opts = options;
	mg_mqtt_pub(_connection, &opts);
}

void connection::subscribe(const mqtt_options& options) noexcept {
	const mg_mqtt_opts opts = options;
	mg_mqtt_sub(_connection, &opts);
}