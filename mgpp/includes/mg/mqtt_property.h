#pragma once

#include <string>
#include "mongoose.h"

namespace mg {
	class mqtt_property {
	public:
		friend class mqtt_options;

		explicit mqtt_property() noexcept;
		mqtt_property(mg_mqtt_prop prop) noexcept;

		uint8_t id() const noexcept;
		void id(uint8_t value) noexcept;
		uint32_t integer_value() const noexcept;
		void integer_value(uint32_t value) noexcept;
		const std::string key() const noexcept;
		void key(const std::string& value) noexcept;
		const std::string value() const noexcept;
		void value(const std::string& value) noexcept;
	private:
		mg_mqtt_prop _prop;
	};
}