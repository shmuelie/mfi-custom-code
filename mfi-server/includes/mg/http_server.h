#pragma once

#include "mongoose.h"
#include "mg/server.h"
#include "mg/http_message.h"
#include "mg/http_response.h"

namespace mg {
	class http_server : public server {
	public:
		explicit http_server() noexcept;

		virtual std::optional<connection> listen(const std::string& url) noexcept override;

	protected:
		virtual void event_handler(const connection& connection, event event, void* event_data) noexcept override;
		virtual http_response http_handler(const http_message& message) noexcept = 0;
	};
}