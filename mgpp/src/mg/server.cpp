#include "mg/server.h"

using namespace std;
using namespace std::chrono;
using namespace mg;

server::server() noexcept {
	mg_mgr_init(&_manager);
}

server::~server() noexcept {
	mg_mgr_free(&_manager);
}

void server::poll(milliseconds timeout) noexcept {
	mg_mgr_poll(&_manager, timeout.count());
}

optional<connection> server::listen(const string& url) noexcept {
	mg_connection* c = mg_listen(&_manager, url.c_str(), &server::_event_handler, this);
	if (c != nullptr) {
		return c;
	}
	return nullopt;
}

void server::_event_handler(mg_connection* c, int event, void* event_data) noexcept {
	server* self = static_cast<server*>(c->fn_data);
	self->event_handler(c, static_cast<mg::event>(event), event_data);
}