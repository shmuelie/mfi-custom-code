#include "mg/server.h"

using namespace std;
using namespace mg;

server::server() noexcept : eventing_handler() {
}

optional<connection> server::listen(const string& url) noexcept {
	mg_connection* c = mg_listen(&_manager, url.c_str(), &server::_event_handler, this);
	if (c != nullptr) {
		return c;
	}
	return nullopt;
}