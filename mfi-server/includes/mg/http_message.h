#pragma once

#include <string>
#include "mongoose.h"

namespace mg {
	class http_message {
	public:
		http_message(mg_http_message* message) noexcept;

		bool match_uri(const std::string& glob) const noexcept;
	private:
		mg_http_message* _message;
	};
}