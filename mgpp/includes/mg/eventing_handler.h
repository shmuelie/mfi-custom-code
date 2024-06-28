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

		eventing_handler() noexcept;
		explicit eventing_handler(std::shared_ptr<mg::manager> const& manager) noexcept;

		std::shared_ptr<mg::manager> const manager() const noexcept;
	protected:
		virtual void event_handler(connection const& connection, event event, void* event_data) noexcept = 0;
		static void _event_handler(mg_connection* c, int event, void* event_data) noexcept;
	private:
		std::shared_ptr<mg::manager> _manager;
	};
}