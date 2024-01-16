#include "mg/connection.h"
#include <vector>
#include "join.h"

using namespace std;
using namespace mg;

connection::connection(mg_connection* c) noexcept : _connection(c) {
}

void connection::http_reply(int status_code, const map<string, string>& headers, const string& body) const noexcept {
	vector<string> headersVector;
	for (const map<string, string>::const_iterator::reference pair : headers) {
		headersVector.push_back(pair.first + ": " + pair.second);
	}
	mg_http_reply(_connection, status_code, join(headersVector.cbegin(), headersVector.cend(), "\r\n", "\r\n").c_str(), body.c_str());
}