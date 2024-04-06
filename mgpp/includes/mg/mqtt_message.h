#pragma once

#include <string>
#include <vector>
#include "mongoose.h"
#include "mg/mqtt_command.h"

namespace mg {
	class mqtt_message {
	public:
		mqtt_message(mg_mqtt_message* message) noexcept;

		const std::string topic() const noexcept;
		const std::string data() const noexcept;
		const std::string datagram() const noexcept;
		uint16_t id() const noexcept;
		mqtt_command command() const noexcept;
		uint8_t quality_of_service() const noexcept;
		uint8_t acknowledgement_response() const noexcept;
		// TODO: properties
	private:
		mg_mqtt_message* _message;
	};
}