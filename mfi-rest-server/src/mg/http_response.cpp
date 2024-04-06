#include "mg/http_response.h"

using namespace std;
using namespace mg;

http_response::http_response(int status_code, const map<string, string>& headers, const string& body) noexcept :
	_status_code(status_code), _headers(headers), _body(body) {
}

http_response::http_response(int status_code) noexcept : http_response(status_code, {}, "") {
}

http_response::http_response(const string& body) noexcept : http_response(200, {}, body) {
}

http_response::http_response(int status_code, const string& body) noexcept : http_response(status_code, {}, body) {
}

http_response::http_response(int status_code, const map<string, string>& headers) noexcept : http_response(status_code, headers, "") {
}

http_response::http_response(const map<string, string>& headers, const string& body) noexcept : http_response(200, headers, body) {
}

int http_response::status_code() const noexcept {
	return _status_code;
}

const map<string, string>& http_response::headers() const noexcept {
	return _headers;
}

const string& http_response::body() const noexcept {
	return _body;
}