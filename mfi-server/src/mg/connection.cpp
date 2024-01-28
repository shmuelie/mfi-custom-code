#include "mg/connection.h"
#include <vector>
#include "string_helpers.h"

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
	mg_http_reply(_connection, response.status_code(), headers, response.body().c_str());
}