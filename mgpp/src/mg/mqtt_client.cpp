#include "mg/mqtt_client.h"
#include "mg/manager.h"

using namespace std;
using namespace mg;

mqtt_client::mqtt_client(const string& url, const mqtt_options& options) noexcept : eventing_handler(), _url(url), _starting_options(options), _connection() {
}

mqtt_client::mqtt_client(const shared_ptr<mg::manager>& manager, const string& url, const mqtt_options& options) noexcept : eventing_handler(manager), _url(url), _starting_options(options), _connection() {
}

void mqtt_client::start() noexcept {
}

bool mqtt_client::is_connected() const noexcept {
	return _connection.has_value();
}

void mqtt_client::_timer_handler(void* arg) noexcept {
	mqtt_client* self = static_cast<mqtt_client*>(arg);

	if (!self->is_connected()) {
		optional<connection> c = self->manager()->connect(self, self->_url, self->_starting_options);
	}
}

void mqtt_client::event_handler(const connection& connection, event event, void* event_data) noexcept {
}