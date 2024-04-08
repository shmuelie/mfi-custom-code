#include "mfi_mqtt/mfi_mqtt_client.h"

using namespace std;
using namespace mfi_mqtt;
using namespace mg;

mfi_mqtt_client::mfi_mqtt_client(const shared_ptr<mg::manager>& manager, const string& url, const mqtt_options& options) noexcept : mqtt_client(manager, url, options) {
	char hostname[1024]{};
	hostname[1023] = '\0';
	gethostname(hostname, 1023);
	_hostname = hostname;

	_device_json =
		"{\"name\":\"" +
		_hostname +
		"\",\"identifiers\":\"mfi-mqtt-client-" +
		_hostname +
		"\",\"manufacturer\":\"Ubiquiti\",\"model\":\"" +
		_board.name() +
		"\",\"w_version\":\"" +
		_board.version() + "\"}";
	_integration_json = "{\"serial_number\":\"" + _hostname + "\",\"device\":" + _device_json + "}";
	_availability_topic = "homeassistant/sensor/" + _hostname + "/availability";
	_integration_topic = "mfi-mqtt-client/devices/" + _hostname;
}

void mfi_mqtt_client::message_handler(const mqtt_message& message) noexcept {
}

void mfi_mqtt_client::open_handler() noexcept {
	_availability_timer = make_unique<mg::timer>(manager(), 1s, true, true, [this]() {
	});
}

void mfi_mqtt_client::announce_availability() const noexcept {
	mqtt_options availabilityOptions{};
	availabilityOptions.topic(_availability_topic);
	availabilityOptions.message("online");
	availabilityOptions.retain(true);
	connection()->publish(availabilityOptions);

	mqtt_options integrationOptions{};
	integrationOptions.topic(_integration_topic);
	integrationOptions.message(_integration_json);
	availabilityOptions.retain(true);
	connection()->publish(integrationOptions);
}