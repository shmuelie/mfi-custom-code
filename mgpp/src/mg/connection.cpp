#include "mg/connection.h"
#include <vector>
#include "shmuelie/string_helpers.hpp"
#include "mg/logger.h"

using namespace std;
using namespace mg;

connection::connection(mg_connection* c) noexcept : _connection(c) {
}

void connection::reply(http_response const& response) const noexcept {
	char const* headers = nullptr;
	vector<string> headersVector{};
	for (map<string, string>::const_iterator::reference const pair : response.headers()) {
		headersVector.push_back(pair.first + ": " + pair.second);
	}
	if (headersVector.size() > 0) {
		headers = shmuelie::join(headersVector.cbegin(), headersVector.cend(), "\r\n", "\r\n").c_str();
	}
	logger::verbose("Responding to %M with status %d", make_tuple(mg_print_ip_port, &_connection->rem, response.status_code()));
	mg_http_reply(_connection, response.status_code(), headers, response.body().c_str());
}