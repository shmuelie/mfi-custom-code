#pragma once

#include <chrono>
#include <string>
#include <optional>
#include "mongoose.h"
#include "mg/connection.h"
#include "mg/http_server.h"
#include "mg/eventing_handler.h"

namespace mg {
	class timer;

	class manager {
	public:
		friend timer;

		manager() noexcept;
		~manager() noexcept;

		void poll(std::chrono::milliseconds timeout) const noexcept;

		std::optional<connection> listen(eventing_handler* self, std::string const& url) noexcept;
		std::optional<connection> listen(http_server* self, std::string const& url) noexcept;
	private:
		mg_mgr* _manager;
	};
}