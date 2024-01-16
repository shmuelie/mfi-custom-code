#pragma once

#include "mongoose.h"

namespace mg {
	class connection {
	public:
		connection(mg_connection* c) noexcept;
	private:
		mg_connection* _connection;
	};
}