#include "mg/http_server.h"

using namespace std;
using namespace mg;

http_server::http_server() noexcept : server() {
}

optional<connection> http_server::listen(const string& url) noexcept {
	mg_connection* c = mg_http_listen(&_manager, url.c_str(), &server::_event_handler, this);
	if (c != nullptr) {
		return c;
	}
	return nullopt;
}

void http_server::event_handler(const connection& connection, event event, void* event_data) noexcept {
	if (event == event::http_mesesage) {
		mg_http_message* message = static_cast<mg_http_message*>(event_data);
		http_response response = http_handler(message);
		connection.reply(response);
	}
}