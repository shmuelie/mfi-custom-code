#include "mg/http_message.h"

using namespace std;
using namespace mg;

http_message::http_message(mg_http_message* message) noexcept : _message(message) {
}

bool http_message::match_uri(const string& glob) const noexcept {
	return mg_http_match_uri(_message, glob.c_str());
}