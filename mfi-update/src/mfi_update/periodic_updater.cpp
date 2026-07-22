#include "mfi_update/periodic_updater.h"

namespace mfi_update {

	periodic_updater::periodic_updater(updater up, std::uint32_t interval_seconds, std::vector<std::string> argv)
		: _updater(std::move(up)), _interval(interval_seconds), _argv(std::move(argv)) {
	}

	std::optional<update_result> periodic_updater::tick(clock::time_point now) {
		// The first tick just establishes the baseline; the boot downloader has
		// already fetched the latest, so we defer the first in-process check by
		// one full interval.
		if (!_established) {
			_established = true;
			_last_check = now;
			return std::nullopt;
		}
		if ((now - _last_check) < _interval) {
			return std::nullopt;
		}
		_last_check = now;
		return _updater.check_and_apply(_argv);
	}

	std::optional<update_result> periodic_updater::tick() {
		return tick(clock::now());
	}

	std::optional<periodic_updater> make_periodic_updater(
		bool enabled, std::uint32_t interval_seconds, std::string const& repo,
		std::string const& proxy, bool insecure, std::string const& tool_name,
		std::string const& current_version_text, std::vector<std::string> argv) {
		if (!enabled) {
			return std::nullopt;
		}
		auto current = parse_semver(current_version_text);
		if (!current) {
			return std::nullopt;
		}

		config c;
		if (!repo.empty()) {
			auto slash = repo.find('/');
			if (slash != std::string::npos) {
				c.repo_owner = repo.substr(0, slash);
				c.repo_name = repo.substr(slash + 1);
			}
		}
		if (!proxy.empty()) {
			c.proxy = proxy;
		}
		c.insecure = insecure;
		c.interval_seconds = interval_seconds;
		c.enabled = true;
		c = c.resolve();

		updater up{ tool_name, *current, c };
		return periodic_updater{ std::move(up), interval_seconds, std::move(argv) };
	}
}
