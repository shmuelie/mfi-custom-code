#pragma once

#include <string>
#include <map>
#include "mongoose.h"
#include "mg/http_response.h"
#include "mg/mqtt_options.h"

namespace mg {
	class connection {
	public:
		connection(mg_connection* c) noexcept;

		void reply(const mg::http_response& response) const noexcept;
		void publish(const mqtt_options& options) noexcept;
		void subscribe(const mqtt_options& options) noexcept;
	private:
		mg_connection* _connection;
	};
}