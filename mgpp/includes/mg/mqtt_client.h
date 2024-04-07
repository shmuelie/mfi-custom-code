#pragma once

#include <optional>
#include "mongoose.h"
#include "mg/eventing_handler.h"
#include "mg/mqtt_options.h"
#include "mg/mqtt_message.h"

namespace mg {
	class mqtt_client : public eventing_handler {
	public:
		friend class manager;

		explicit mqtt_client(const std::string& url, const mqtt_options& options) noexcept;

		void start() noexcept;
		bool is_connected() const noexcept;
	protected:
		virtual void event_handler(const connection& connection, event event, void* event_data) noexcept override;
		virtual void message_handler(const mqtt_message& message) noexcept = 0;
		virtual void open_handler() noexcept = 0;
	private:
		static void _timer_handler(void* arg) noexcept;
		std::optional<connection> _connection;
		std::string _url;
		mqtt_options _starting_options;
	};
}