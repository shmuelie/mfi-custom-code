#pragma once

#include <chrono>
#include "mongoose.h"
#include "mg/connection.h"

namespace mg {
	enum class event {
		http_mesesage = MG_EV_HTTP_MSG
	};

	class eventing_handler {
	public:
		explicit eventing_handler() noexcept;
		~eventing_handler() noexcept;

		void poll(std::chrono::milliseconds timeout) noexcept;
	protected:
		virtual void event_handler(const connection& connection, event event, void* event_data) noexcept = 0;
		static void _event_handler(mg_connection* c, int event, void* event_data) noexcept;
		mg_mgr _manager;
	};
}