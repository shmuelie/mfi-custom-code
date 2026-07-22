#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>
#include "mfi_update/updater.h"

namespace mfi_update {
	/**
	 * @brief Drives periodic self-update checks from a long-running tool's loop.
	 *
	 * The tool calls tick() from its existing loop; the checker runs an update
	 * at most once per configured interval. On a successful update the process
	 * image is replaced and tick() does not return.
	 */
	class periodic_updater {
	public:
		using clock = std::chrono::steady_clock;

		/**
		 * @brief Constructs a periodic updater.
		 * @param up The underlying updater.
		 * @param interval Seconds between checks.
		 * @param argv Arguments to re-exec with on update.
		 */
		periodic_updater(updater up, std::uint32_t interval_seconds, std::vector<std::string> argv);

		/**
		 * @brief Runs a check if the interval has elapsed since the last one.
		 * @param now The current time (injectable for tests).
		 * @return The result of the check, or std::nullopt if it was not yet due.
		 */
		std::optional<update_result> tick(clock::time_point now);

		/** @brief tick() using the real clock. */
		std::optional<update_result> tick();

	private:
		updater _updater;
		std::chrono::seconds _interval;
		std::vector<std::string> _argv;
		clock::time_point _last_check{};
		bool _established{ false };
	};

	/**
	 * @brief Builds a periodic_updater from flat option values, or nullopt if disabled.
	 *
	 * Convenience for tool main()s: applies repo ("owner/name"), proxy, and
	 * insecure onto a config, resolves it (env proxy fallback), and constructs a
	 * periodic_updater for @p tool_name at @p current_version_text with @p argv.
	 *
	 * @return A configured periodic_updater, or nullopt when disabled or the
	 *         current version cannot be parsed.
	 */
	std::optional<periodic_updater> make_periodic_updater(
		bool enabled, std::uint32_t interval_seconds, std::string const& repo,
		std::string const& proxy, bool insecure, std::string const& tool_name,
		std::string const& current_version_text, std::vector<std::string> argv);
}
