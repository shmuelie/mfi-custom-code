#pragma once

#include <atomic>
#include <memory>
#include <thread>
#include "mfi_update/periodic_updater.h"

namespace mfi_update {
	/**
	 * One background preparation, with main-thread-only scheduling and application.
	 * Only request_stop() may be called concurrently. Hooks are frozen on construction;
	 * they must not access main-thread state and must cooperate with cancellation.
	 * This class never calls MQTT APIs. All ownership is retained until stop()/join.
	 */
	class background_updater final {
	public:
		using clock = update_schedule::clock;

		background_updater(updater up, std::uint32_t interval_seconds,
			std::vector<std::string> argv, preparation_limits limits = {});
		~background_updater();
		background_updater(background_updater const&) = delete;
		background_updater& operator=(background_updater const&) = delete;

		/** Pending work never causes a wait. A ready outcome is emitted once. */
		std::optional<update_result> tick();
		std::optional<update_result> tick(clock::time_point now);
		/** Call only after offline shutdown. Worker completion/join precedes ready. */
		update_result apply_ready(std::function<bool()> const& should_cancel);
		/** Thread-safe, nonblocking; not async-signal-safe. Stops future jobs too. */
		void request_stop() noexcept;
		/** Owner-thread cleanup/join; does not wait out the preparation budget. */
		void stop() noexcept;

	private:
		updater _updater;
		update_schedule _schedule;
		std::vector<std::string> _argv;
		preparation_limits _limits;
		std::atomic_bool _cancelled{false};
		std::atomic_bool _done{false};
		std::thread _worker;
		std::optional<preparation_context> _context;
		std::optional<preparation_result> _result;
		std::optional<prepared_update> _ready;
	};

	std::unique_ptr<background_updater> make_background_updater(
		bool enabled, std::uint32_t interval_seconds, std::string const& repo,
		std::string const& proxy, bool insecure, std::string const& tool_name,
		std::string const& current_version_text, std::vector<std::string> argv);
}
