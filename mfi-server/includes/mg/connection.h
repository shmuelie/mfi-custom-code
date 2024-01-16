#pragma once

#include <string>
#include <map>
#include "mongoose.h"

namespace mg {
	class connection {
	public:
		connection(mg_connection* c) noexcept;

		void http_reply(int status_code, const std::map<std::string, std::string>& headers, const std::string& body) const noexcept;
	private:
		mg_connection* _connection;
	};
}