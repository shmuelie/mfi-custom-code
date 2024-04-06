#include "mg/mqtt_message.h"
#include "mg/strings.h"

using namespace std;
using namespace mg;

mqtt_message::mqtt_message(mg_mqtt_message* message) noexcept : _message(message) {
}

const string mqtt_message::topic() const noexcept {
	mg_str& topic = _message->topic;
	return to_string(topic);
}

const string mqtt_message::data() const noexcept {
	mg_str& data = _message->data;
	return to_string(data);
}

const string mqtt_message::datagram() const noexcept {
	mg_str& dgram = _message->dgram;
	return to_string(dgram);
}

uint16_t mqtt_message::id() const noexcept {
	return _message->id;
}

mqtt_command mqtt_message::command() const noexcept {
	return static_cast<mqtt_command>(_message->cmd);
}

uint8_t mqtt_message::quality_of_service() const noexcept {
	return _message->qos;
}

uint8_t mqtt_message::acknowledgement_response() const noexcept {
	return _message->ack;
}