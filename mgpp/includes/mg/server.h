#pragma once

#include <string>
#include <optional>
#include "mongoose.h"
#include "mg/connection.h"
#include "mg/eventing_handler.h"

namespace mg {
	class server : public eventing_handler {
	public:
		explicit server() noexcept;

		virtual std::optional<connection> listen(const std::string& url) noexcept;
	};
}