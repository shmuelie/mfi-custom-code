#include "mg/http_server.h"
#include "mg/logger.h"
#include "mg/manager.h"

using namespace std;
using namespace mg;

http_server::http_server() noexcept : server() {
}

optional<connection> http_server::listen(const string& url) noexcept {
	return manager()->listen(this, url);
}

void http_server::event_handler(const connection& connection, event event, void* event_data) noexcept {
	if (event == event::http_mesesage) {
		mg_http_message* message = static_cast<mg_http_message*>(event_data);
		logger::info("Received request to %s", make_tuple(to_string(message->uri).c_str()));
		try {
			http_response response = http_handler(message);
			connection.reply(response);
		}
		catch (std::exception& ex) {
			connection.reply({ 500, ex.what() });
		}
	}
}