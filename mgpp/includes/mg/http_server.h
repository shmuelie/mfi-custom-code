#pragma once

#include "mongoose.h"
#include "mg/server.h"
#include "mg/http_message.h"
#include "mg/http_response.h"

namespace mg {
	class http_server : public server {
	public:
		friend class manager;

		http_server() noexcept;

		virtual std::optional<connection> listen(std::string const& url) noexcept override;

	protected:
		virtual void event_handler(connection const& connection, event event, void* event_data) noexcept override;
		virtual http_response http_handler(http_message const& message) noexcept = 0;
	};
}