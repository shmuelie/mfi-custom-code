#pragma once

#include <atomic>
#include <chrono>

namespace mfi_update {
	struct preparation_limits {
		std::chrono::milliseconds total{120000};
		std::chrono::milliseconds cleanup{5000};
	};

	/** One absolute budget shared by every preparation stage, including handoff. */
	class preparation_context final {
	public:
		using clock = std::chrono::steady_clock;

		explicit preparation_context(preparation_limits limits = {},
			std::atomic_bool const* cancellation = nullptr) noexcept
			: _deadline(clock::now() + limits.total), _cleanup(limits.cleanup),
			  _cancellation(cancellation) {}

		bool cancelled() const noexcept {
			return _cancellation && _cancellation->load(std::memory_order_acquire);
		}
		bool timed_out() const noexcept { return clock::now() >= _deadline; }
		bool interrupted() const noexcept { return cancelled() || timed_out(); }
		clock::time_point deadline() const noexcept { return _deadline; }
		std::chrono::milliseconds cleanup_allowance() const noexcept { return _cleanup; }
		/** Set/read by the preparation thread; inspect only after its handoff. */
		void mark_cleanup_failed() const noexcept { _cleanup_failed = true; }
		bool cleanup_failed() const noexcept { return _cleanup_failed; }

	private:
		clock::time_point _deadline;
		std::chrono::milliseconds _cleanup;
		std::atomic_bool const* _cancellation;
		mutable bool _cleanup_failed{false};
	};
}
