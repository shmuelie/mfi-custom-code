#pragma once

#include <memory>
#include "mongoose.h"
#include "mg/connection.h"
#include "mg/event.h"

namespace mg {
	class manager;

	class eventing_handler {
	public:
		friend mg::manager;

		explicit eventing_handler() noexcept;
		explicit eventing_handler(const std::shared_ptr<mg::manager>& manager) noexcept;

		const std::shared_ptr<mg::manager> manager() const noexcept;
	protected:
		virtual void event_handler(const connection& connection, event event, void* event_data) noexcept = 0;
		static void _event_handler(mg_connection* c, int event, void* event_data) noexcept;
	private:
		std::shared_ptr<mg::manager> _manager;
	};
}