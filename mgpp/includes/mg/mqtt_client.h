#pragma once

#include <optional>
#include "mongoose.h"
#include "mg/eventing_handler.h"
#include "mg/mqtt_options.h"
#include "mg/mqtt_message.h"
#include "mg/timer.h"

namespace mg {
	class mqtt_client : public eventing_handler {
	public:
		friend class manager;

		explicit mqtt_client(const std::string& url, const mqtt_options& options) noexcept;
		explicit mqtt_client(const std::shared_ptr<mg::manager>& manager, const std::string& url, const mqtt_options& options) noexcept;

		bool is_connected() const noexcept;
	protected:
		virtual void event_handler(const mg::connection& connection, event event, void* event_data) noexcept override;
		virtual void message_handler(const mqtt_message& message) noexcept = 0;
		virtual void open_handler() noexcept = 0;

		std::optional<mg::connection> connection() const noexcept;
	private:
		void timer_handler() noexcept;
		std::optional<mg::connection> _connection;
		std::string _url;
		mqtt_options _starting_options;
		std::shared_ptr<timer> _timer;
	};
}