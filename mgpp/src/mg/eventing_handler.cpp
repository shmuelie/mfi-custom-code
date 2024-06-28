#include "mg/eventing_handler.h"
#include "mg/manager.h"

using namespace std;
using namespace mg;

eventing_handler::eventing_handler() noexcept : _manager(make_shared<mg::manager>()) {
}
eventing_handler::eventing_handler(shared_ptr<mg::manager> const& manager) noexcept : _manager(manager) {
}

void eventing_handler::_event_handler(mg_connection* c, int event, void* event_data) noexcept {
	eventing_handler* self = static_cast<eventing_handler*>(c->fn_data);
	self->event_handler(c, static_cast<mg::event>(event), event_data);
}

const shared_ptr<mg::manager> eventing_handler::manager() const noexcept {
	return _manager;
}