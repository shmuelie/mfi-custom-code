#pragma once

#include <string>
#include <map>
#include "mongoose.h"
#include "mg/http_response.h"

namespace mg {
	class connection {
	public:
		connection(mg_connection* c) noexcept;

		void reply(mg::http_response const& response) const noexcept;
	private:
		mg_connection* _connection;
	};
}