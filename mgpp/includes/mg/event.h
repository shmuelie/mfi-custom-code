#pragma once

#include "mongoose.h"

namespace mg {
	enum class event {
		http_mesesage = MG_EV_HTTP_MSG,
		connect = MG_EV_CONNECT,
		mqtt_open = MG_EV_MQTT_OPEN,
		mqtt_message = MG_EV_MQTT_MSG,
		mqtt_command = MG_EV_MQTT_CMD,
		error = MG_EV_ERROR,
		open = MG_EV_OPEN,
		poll = MG_EV_POLL,
		resolve = MG_EV_RESOLVE,
		accept = MG_EV_ACCEPT,
		tls_handshake = MG_EV_TLS_HS,
		read = MG_EV_READ,
		write = MG_EV_WRITE,
		close = MG_EV_CLOSE,
		websocket_open = MG_EV_WS_OPEN,
		websocket_message = MG_EV_WS_MSG,
		websocket_control_message = MG_EV_WS_CTL,
		sntp_time = MG_EV_SNTP_TIME,
		wakeup = MG_EV_WAKEUP
	};
}