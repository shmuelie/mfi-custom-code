#pragma once

#include "mongoose.h"

namespace mg {
	enum class mqtt_command {
		connect = MQTT_CMD_CONNECT,
		connect_acknowledgement = MQTT_CMD_CONNACK,
		publish = MQTT_CMD_PUBLISH,
		publish_acknowledgement = MQTT_CMD_PUBACK,
		pubrec = MQTT_CMD_PUBREC,
		pubcomp = MQTT_CMD_PUBCOMP,
		subscribe = MQTT_CMD_SUBSCRIBE,
		subscribe_acknowledgement = MQTT_CMD_SUBACK,
		unsubscribe = MQTT_CMD_UNSUBSCRIBE,
		unsubscribe_acknowledgement = MQTT_CMD_UNSUBACK,
		ping_request = MQTT_CMD_PINGREQ,
		ping_response = MQTT_CMD_PINGRESP,
		disconnect = MQTT_CMD_DISCONNECT
	};
}