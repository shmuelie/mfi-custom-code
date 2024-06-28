#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include "mongoose.h"

namespace mg {
	class manager;

	class timer {
	public:
		timer(std::shared_ptr<mg::manager> const& manager, std::chrono::milliseconds period, bool repeating, bool run_now, std::function<void()> callback) noexcept;
		~timer() noexcept;

		std::chrono::milliseconds period() const noexcept;
		std::chrono::milliseconds expire() const noexcept;
		void stop() noexcept;
		bool running() const noexcept;
		bool repeating() const noexcept;
	private:
		static void callback(void* arg) noexcept;

		mg_timer* _timer;
		std::shared_ptr<mg::manager> _manager;
		std::function<void()> _callback;
	};
}