#pragma once

#include <cstdint>
#include <mosquitto.h>

struct publication {
	int error = MOSQ_ERR_NO_CONN;
	std::uint64_t epoch = 0;
	std::uint64_t sequence = 0;

	bool accepted() const noexcept { return error == MOSQ_ERR_SUCCESS; }
};

enum class publication_state {
	pending,
	complete,
	failed
};
