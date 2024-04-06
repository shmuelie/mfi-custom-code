#include "mg/eventing_handler.h"

using namespace std::chrono;
using namespace mg;

eventing_handler::eventing_handler() noexcept {
	mg_mgr_init(&_manager);
}

eventing_handler::~eventing_handler() noexcept {
	mg_mgr_free(&_manager);
}

void eventing_handler::poll(milliseconds timeout) noexcept {
	mg_mgr_poll(&_manager, timeout.count());
}

void eventing_handler::_event_handler(mg_connection* c, int event, void* event_data) noexcept {
	eventing_handler* self = static_cast<eventing_handler*>(c->fn_data);
	self->event_handler(c, static_cast<mg::event>(event), event_data);
}