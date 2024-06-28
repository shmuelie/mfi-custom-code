#include "mg/http_response.h"

using namespace std;
using namespace mg;

http_response::http_response(int status_code, map<string, string> const& headers, string const& body) noexcept :
	_status_code(status_code), _headers(headers), _body(body) {
}

http_response::http_response(int status_code) noexcept : http_response(status_code, {}, "") {
}

http_response::http_response(string const& body) noexcept : http_response(200, {}, body) {
}

http_response::http_response(int status_code, string const& body) noexcept : http_response(status_code, {}, body) {
}

http_response::http_response(int status_code, map<string, string> const& headers) noexcept : http_response(status_code, headers, "") {
}

http_response::http_response(map<string, string> const& headers, string const& body) noexcept : http_response(200, headers, body) {
}

int http_response::status_code() const noexcept {
	return _status_code;
}

map<string, string> const& http_response::headers() const noexcept {
	return _headers;
}

string const& http_response::body() const noexcept {
	return _body;
}