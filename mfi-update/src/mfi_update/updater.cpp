#include "mfi_update/updater.h"
#include "mfi_update/downloader.h"

#include <array>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <memory>
#include <pthread.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace mfi_update {

	namespace {
		void discard_file(std::string const& path) noexcept {
			if (!path.empty() && ::unlink(path.c_str()) != 0 && errno != ENOENT)
				std::fputs("update staged file cleanup failed\n", stderr);
		}
	}

	prepared_update::prepared_update(std::string path, std::string target) noexcept
		: _path(std::move(path)), _target(std::move(target)) {}

	prepared_update::~prepared_update() {
		discard_file(_path);
	}

	prepared_update::prepared_update(prepared_update&& other) noexcept
		: _path(std::exchange(other._path, {})), _target(std::move(other._target)) {}

	prepared_update& prepared_update::operator=(prepared_update&& other) noexcept {
		if (this != &other) {
			discard_file(_path);
			_path = std::exchange(other._path, {});
			_target = std::move(other._target);
		}
		return *this;
	}

	namespace {
		update_result interruption(preparation_context const& context, update_result otherwise) {
			if (context.cleanup_failed()) return update_result::cleanup_failed;
			if (context.cancelled()) return update_result::cancelled;
			if (context.timed_out()) return update_result::timed_out;
			return otherwise;
		}

		// Blocking only this thread is insufficient if a signal can reach another
		// thread. Default dispositions close that last handled-signal/exec race.
		class exec_signal_guard final {
		public:
			exec_signal_guard() noexcept {
				sigset_t signals;
				::sigemptyset(&signals);
				::sigaddset(&signals, SIGTERM);
				::sigaddset(&signals, SIGINT);
				_masked = ::pthread_sigmask(SIG_BLOCK, &signals, &_old_mask) == 0;
				if (!_masked) return;
				struct sigaction defaults{};
				defaults.sa_handler = SIG_DFL;
				::sigemptyset(&defaults.sa_mask);
				_term = ::sigaction(SIGTERM, &defaults, &_old_term) == 0;
				if (_term) _interrupt = ::sigaction(SIGINT, &defaults, &_old_interrupt) == 0;
			}
			~exec_signal_guard() {
				if (_interrupt) ::sigaction(SIGINT, &_old_interrupt, nullptr);
				if (_term) ::sigaction(SIGTERM, &_old_term, nullptr);
				if (_masked) ::pthread_sigmask(SIG_SETMASK, &_old_mask, nullptr);
			}
			bool valid() const noexcept { return _masked && _term && _interrupt; }
			bool pending() const noexcept {
				sigset_t pending;
				if (::sigpending(&pending) != 0) return true;
				return ::sigismember(&pending, SIGTERM) == 1 || ::sigismember(&pending, SIGINT) == 1;
			}
			bool unblock_for_exec() const noexcept {
				auto mask = _old_mask;
				::sigdelset(&mask, SIGTERM);
				::sigdelset(&mask, SIGINT);
				return ::pthread_sigmask(SIG_SETMASK, &mask, nullptr) == 0;
			}
		private:
			sigset_t _old_mask{};
			struct sigaction _old_term{}, _old_interrupt{};
			bool _masked{false}, _term{false}, _interrupt{false};
		};
	}

	updater::updater(std::string tool_name, semver current, config cfg)
		: _tool_name(std::move(tool_name)), _current(current), _config(std::move(cfg)) {
	}

	std::string updater::api_matching_refs_url() const {
		return "https://api.github.com/repos/" + _config.repo_owner + "/" + _config.repo_name +
			"/git/matching-refs/tags/" + _tool_name + "/";
	}

	std::string updater::api_release_url(std::string const& tag) const {
		return "https://api.github.com/repos/" + _config.repo_owner + "/" + _config.repo_name +
			"/releases/tags/" + tag;
	}

	std::optional<release_asset> updater::check() const {
		fetch_fn fetch = _fetch;
		if (!fetch) {
			auto kind = downloader::detect();
			if (!kind) {
				return std::nullopt;
			}
			downloader dl{ *kind, _config };
			fetch = [dl](std::string const& url) { return dl.fetch_to_string(url); };
		}

		auto refs = fetch(api_matching_refs_url());
		if (!refs) {
			return std::nullopt;
		}
		auto latest = pick_latest_tag(*refs, _tool_name);
		if (!latest) {
			return std::nullopt;
		}

		auto release = fetch(api_release_url(latest->second));
		if (!release) {
			return std::nullopt;
		}
		auto url = pick_asset_url(*release, _tool_name);
		if (!url) {
			return std::nullopt;
		}
		return release_asset{ latest->first, *url };
	}

	preparation_result updater::prepare(preparation_context const& context) const {
		auto failure = [&](update_result result) -> preparation_result {
			return {interruption(context, result), std::nullopt};
		};
		if (context.interrupted()) return failure(update_result::cancelled);
		if (!_config.enabled) {
			return failure(update_result::disabled);
		}

		// Resolve a downloader once so both fetch and download share it.
		std::optional<downloader> dl = _downloader;
		if (!dl && ((!_fetch && !_cancellable_fetch) || (!_download && !_cancellable_download))) {
			auto kind = downloader::detect();
			if (!kind) {
				return failure(update_result::no_downloader);
			}
			dl.emplace(*kind, _config);
		}

		auto fetch = [&](std::string const& url) -> std::optional<std::string> {
			if (context.interrupted()) return std::nullopt;
			if (_cancellable_fetch) return _cancellable_fetch(url, context);
			if (_fetch) return _fetch(url);
			return dl->fetch_to_string(url, context);
		};

		auto refs = fetch(api_matching_refs_url());
		if (!refs || context.interrupted()) {
			return failure(update_result::check_failed);
		}
		auto latest = pick_latest_tag(*refs, _tool_name);
		if (!latest || context.interrupted()) {
			return failure(update_result::check_failed);
		}
		if (latest->first <= _current) {
			return failure(update_result::up_to_date);
		}

		auto release = fetch(api_release_url(latest->second));
		if (!release || context.interrupted()) {
			return failure(update_result::check_failed);
		}
		auto url = pick_asset_url(*release, _tool_name);
		if (!url || context.interrupted()) {
			return failure(update_result::check_failed);
		}

		auto target = _target_path.empty() ? self_path(_config.bin_dir + "/" + _tool_name) : _target_path;
		auto tmp = target + ".new.XXXXXX";
		auto fd = ::mkstemp(tmp.data());
		if (fd < 0) return failure(update_result::download_failed);
		::close(fd);
		prepared_update artifact{std::move(tmp), std::move(target)};
		bool downloaded = false;
		if (!context.interrupted()) {
			if (_cancellable_download) downloaded = _cancellable_download(*url, artifact.path(), context);
			else if (_download) downloaded = _download(*url, artifact.path());
			else downloaded = dl->fetch_to_file(*url, artifact.path(), context);
		}
		if (!downloaded || context.interrupted() || !is_valid_elf(artifact.path()) ||
			::chmod(artifact.path().c_str(), 0755) != 0 || context.interrupted()) {
			return failure(update_result::download_failed);
		}
		return {update_result::ready, std::move(artifact)};
	}

	update_result updater::apply(prepared_update const& artifact, std::vector<std::string> const& argv,
		std::function<bool()> const& should_cancel) const {
		if (should_cancel && should_cancel()) return update_result::cancelled;
		if (_apply) {
			auto success = _apply(artifact.path(), argv);
			if (should_cancel && should_cancel())
				return success ? update_result::replaced_not_restarted : update_result::cancelled;
			return success ? update_result::updated : update_result::apply_failed;
		}
		return replace_and_reexec(artifact.path(), artifact.target(), argv, should_cancel);
	}

	update_result updater::check_and_apply(std::vector<std::string> const& argv) const {
		auto prepared = prepare(preparation_context{});
		if (!prepared.artifact) return prepared.result;
		auto result = apply(*prepared.artifact, argv);
		// Preserve the legacy failure classification for synchronous consumers.
		if (result == update_result::apply_failed || result == update_result::replaced_not_restarted)
			return update_result::download_failed;
		return result;
	}

	bool is_valid_elf(std::string const& path) noexcept {
		auto fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
		if (fd < 0) return false;
		struct stat status{};
		std::array<char, 4> magic{};
		bool valid = ::fstat(fd, &status) == 0 && S_ISREG(status.st_mode) &&
			::read(fd, magic.data(), magic.size()) == static_cast<ssize_t>(magic.size()) &&
			magic[0] == '\x7f' && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F';
		::close(fd);
		return valid;
	}

	std::string self_path(std::string const& fallback) {
		std::array<char, 4096> buf{};
		ssize_t n = ::readlink("/proc/self/exe", buf.data(), buf.size() - 1);
		if (n > 0) {
			buf[static_cast<size_t>(n)] = '\0';
			return std::string{ buf.data() };
		}
		return fallback;
	}

	bool replace_and_reexec(std::string const& new_path, std::string const& target_path,
		std::vector<std::string> const& argv) noexcept {
		return replace_and_reexec(new_path, target_path, argv, {}) == update_result::updated;
	}

	update_result replace_and_reexec(std::string const& new_path, std::string const& target_path,
		std::vector<std::string> const& argv, std::function<bool()> const& should_cancel) {
		if (should_cancel && should_cancel()) return update_result::cancelled;
		if (argv.empty()) return update_result::apply_failed;
		std::vector<char*> c_argv;
		c_argv.reserve(argv.size() + 1);
		for (auto const& a : argv) {
			c_argv.push_back(const_cast<char*>(a.c_str()));
		}
		c_argv.push_back(nullptr);

		exec_signal_guard signals;
		if (!signals.valid()) return update_result::apply_failed;
		auto cancelled = [&] { return (should_cancel && should_cancel()) || signals.pending(); };
		if (cancelled()) return update_result::cancelled;
		if (std::rename(new_path.c_str(), target_path.c_str()) != 0) return update_result::apply_failed;
		if (cancelled() || !signals.unblock_for_exec()) return update_result::replaced_not_restarted;
		// SIGTERM/SIGINT now have default dispositions, and are not blocked in
		// the new image. A signal after the final check cannot be swallowed.
		::execv(target_path.c_str(), c_argv.data());
		return update_result::replaced_not_restarted;
	}

	std::string describe(update_result result) noexcept {
		switch (result) {
		case update_result::disabled:        return "updates disabled";
		case update_result::no_downloader:   return "no downloader (wget/curl) found";
		case update_result::up_to_date:      return "up to date";
		case update_result::check_failed:    return "update check failed";
		case update_result::download_failed: return "update download failed";
		case update_result::updated:         return "updated";
		case update_result::ready:           return "update ready to apply";
		case update_result::cancelled:       return "update cancelled";
		case update_result::timed_out:       return "update preparation timed out";
		case update_result::apply_failed:    return "update replacement failed";
		case update_result::replaced_not_restarted: return "update replaced binary but did not restart";
		case update_result::preparation_failed: return "update preparation failed unexpectedly";
		case update_result::cleanup_failed: return "update child cleanup deadline exceeded";
		}
		return "unknown";
	}
}
