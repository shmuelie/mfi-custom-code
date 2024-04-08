#include "mg/mqtt_client.h"
#include "mg/manager.h"

using namespace std;
using namespace std::chrono_literals;
using namespace mg;

mqtt_client::mqtt_client(const string& url, const mqtt_options& options) noexcept : eventing_handler(), _url(url), _starting_options(options), _connection() {
	_timer = make_shared<timer>(manager(), 3s, true, true, &mqtt_client::timer_handler);
}

mqtt_client::mqtt_client(const shared_ptr<mg::manager>& manager, const string& url, const mqtt_options& options) noexcept : eventing_handler(manager), _url(url), _starting_options(options), _connection() {
	_timer = make_shared<timer>(this->manager(), 3s, true, true, &mqtt_client::timer_handler);
}

bool mqtt_client::is_connected() const noexcept {
	return _connection.has_value();
}

void mqtt_client::timer_handler() noexcept {
	if (!is_connected()) {
		optional<connection> c = manager()->connect(this, _url, _starting_options);
	}
}

void mqtt_client::event_handler(const connection& connection, event event, void* event_data) noexcept {
	switch (event)
	{
	case mg::event::mqtt_open:
		open_handler();
		break;
	case mg::event::mqtt_message:
		mqtt_message message = static_cast<mg_mqtt_message*>(event_data);
		message_handler(message);
		break;
	case mg::event::close:
		_connection = nullopt;
		break;
	}
}