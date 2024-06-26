#include "mg/http_message.h"

using namespace std;
using namespace mg;

http_message::http_message(mg_http_message* message) noexcept : _message(message) {
}

bool http_message::match_uri(string const& glob) const noexcept {
	return mg_http_match_uri(_message, glob.c_str());
}

string const http_message::method() const noexcept {
	mg_str& method = _message->method;
	return to_string(method);
}

string const http_message::body() const noexcept {
	mg_str& body = _message->body;
	return to_string(body);
}

string const http_message::uri() const noexcept {
	mg_str& uri = _message->uri;
	return to_string(uri);
}