#include "mfi_mqtt/mfi_mqtt_client.h"

using namespace std;
using namespace mfi_mqtt;
using namespace mg;

mfi_mqtt_client::mfi_mqtt_client(const shared_ptr<mg::manager>& manager, const string& url, const mqtt_options& options) noexcept : mqtt_client(manager, url, options) {
}

void mfi_mqtt_client::message_handler(const mqtt_message& message) noexcept {
}

void mfi_mqtt_client::open_handler() noexcept {
	_availability_timer = make_unique<mg::timer>(manager(), 1s, true, true, [this]() {
		this->availability_handler();
	});
}

void mfi_mqtt_client::availability_handler() noexcept {
}