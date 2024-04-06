#pragma once

#include <string>
#include <chrono>
#include <vector>
#include "mongoose.h"
#include "mg/mqtt_property.h"

namespace mg {
	class mqtt_options {
	public:
		explicit mqtt_options() noexcept;
		mqtt_options(mg_mqtt_opts opts) noexcept;

		const std::string username() const noexcept;
		void username(const std::string& value) noexcept;
		const std::string password() const noexcept;
		void password(const std::string& value) noexcept;
		const std::string client_id() const noexcept;
		void client_id(const std::string& value) noexcept;
		const std::string topic() const noexcept;
		void topic(const std::string& value) noexcept;
		const std::string message() const noexcept;
		void message(const std::string& value) noexcept;
		uint8_t quality_of_service() const noexcept;
		void quality_of_service(uint8_t value) noexcept;
		uint8_t version() const noexcept;
		void version(uint8_t value) noexcept;
		std::chrono::seconds keep_alive() const noexcept;
		void keep_alive(std::chrono::seconds value) noexcept;
		bool retain() const noexcept;
		void retain(bool value) noexcept;
		bool clean() const noexcept;
		void clean(bool value) noexcept;
		const std::vector<mqtt_property>& properties() const noexcept;
		const std::vector<mqtt_property>& will_properties() const noexcept;

		operator mg_mqtt_opts () const noexcept;
	private:
		mg_mqtt_opts _opts;
		std::vector<mqtt_property> _props;
		std::vector<mqtt_property> _will_props;
	};
}