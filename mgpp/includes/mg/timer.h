#pragma once

#include <chrono>
#include "mongoose.h"

namespace mg {
	class manager;

	class timer {
	public:
		friend manager;

		~timer() noexcept;

		std::chrono::milliseconds period() const noexcept;
		std::chrono::milliseconds expire() const noexcept;
		void stop() noexcept;
		bool running() const noexcept;
		bool repeating() const noexcept;
	private:
		timer(mg_timer* timer, manager* manager) noexcept;

		mg_timer* _timer;
		manager* _manager;
	};
}