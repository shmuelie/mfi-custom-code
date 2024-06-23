#include "mg/manager.h"

using namespace std;
using namespace std::chrono;
using namespace mg;

manager::manager() noexcept : _manager(nullptr) {
	mg_mgr_init(_manager);
}

manager::~manager() noexcept {
	mg_mgr_free(_manager);
}

void manager::poll(milliseconds timeout) const noexcept {
	mg_mgr_poll(_manager, timeout.count());
}

optional<connection> manager::listen(eventing_handler* self, const string& url) noexcept {
	mg_connection* c = mg_listen(_manager, url.c_str(), &eventing_handler::_event_handler, self);

	if (c != nullptr) {
		return c;
	}
	return nullopt;
}

optional<connection> manager::listen(http_server* self, const string& url) noexcept {
	mg_connection* c = mg_http_listen(_manager, url.c_str(), &http_server::_event_handler, self);

	if (c != nullptr) {
		return c;
	}
	return nullopt;
}