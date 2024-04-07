#pragma once

#include <chrono>
#include <string>
#include <optional>
#include "mongoose.h"
#include "mg/connection.h"
#include "mg/mqtt_options.h"
#include "mg/http_server.h"
#include "mg/eventing_handler.h"
#include "mg/mqtt_client.h"

namespace mg {
	class timer;

	class manager {
	public:
		friend timer;

		explicit manager() noexcept;
		~manager() noexcept;

		void poll(std::chrono::milliseconds timeout) const noexcept;

		std::optional<connection> listen(eventing_handler* self, const std::string& url) noexcept;
		std::optional<connection> listen(http_server* self, const std::string& url) noexcept;
		std::optional<connection> connect(mqtt_client* self, const std::string& url, const mqtt_options& options) noexcept;

		mg::timer create_timer(std::chrono::milliseconds period, bool repeating, bool run_now) noexcept;
	private:
		mg_mgr* _manager;
	};
}