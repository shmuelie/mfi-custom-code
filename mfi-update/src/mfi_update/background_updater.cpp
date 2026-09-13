#include "mfi_update/background_updater.h"

#include <pthread.h>
#include <signal.h>

namespace mfi_update {
	namespace {
		// The worker inherits blocked termination signals; only the owning loop
		// translates its signal-safe flag into the atomic cancellation request.
		class worker_signal_mask final {
		public:
			worker_signal_mask() noexcept {
				sigset_t mask;
				::sigemptyset(&mask);
				::sigaddset(&mask, SIGTERM);
				::sigaddset(&mask, SIGINT);
				_valid = ::pthread_sigmask(SIG_BLOCK, &mask, &_previous) == 0;
			}
			~worker_signal_mask() {
				if (_valid) ::pthread_sigmask(SIG_SETMASK, &_previous, nullptr);
			}
			bool valid() const noexcept { return _valid; }
		private:
			sigset_t _previous{};
			bool _valid{false};
		};
	}

	background_updater::background_updater(updater up, std::uint32_t interval_seconds,
		std::vector<std::string> argv, preparation_limits limits)
		: _updater(std::move(up)), _schedule(interval_seconds),
		  _argv(std::move(argv)), _limits(limits) {}

	background_updater::~background_updater() { stop(); }

	void background_updater::request_stop() noexcept {
		_cancelled.store(true, std::memory_order_release);
	}

	void background_updater::stop() noexcept {
		request_stop();
		if (_worker.joinable()) _worker.join();
		_result.reset();
		_ready.reset();
		_context.reset();
	}

	std::optional<update_result> background_updater::tick() { return tick(clock::now()); }

	std::optional<update_result> background_updater::tick(clock::time_point now) {
		if (_worker.joinable()) {
			if (!_done.load(std::memory_order_acquire)) return std::nullopt;
			_worker.join();
			auto outcome = _result->result;
			if (_context->cleanup_failed()) outcome = update_result::cleanup_failed;
			else if (_cancelled.load(std::memory_order_acquire)) outcome = update_result::cancelled;
			else if (_context->timed_out()) outcome = update_result::timed_out;
			if (outcome == update_result::ready) _ready = std::move(_result->artifact);
			if (outcome == update_result::cleanup_failed) request_stop();
			_result.reset();
			_context.reset();
			return outcome;
		}
		if (_cancelled.load(std::memory_order_acquire)) {
			if (_ready) {
				_ready.reset();
				return update_result::cancelled;
			}
			return std::nullopt;
		}
		if (_ready || !_schedule.due(now)) return std::nullopt;

		_context.emplace(_limits, &_cancelled);
		_done.store(false, std::memory_order_release);
		worker_signal_mask mask;
		if (!mask.valid()) {
			_context.reset();
			return update_result::preparation_failed;
		}
		try {
			_worker = std::thread([this] {
				try {
					_result = _updater.prepare(*_context);
					if (_context->cleanup_failed()) {
						_result->artifact.reset();
						_result->result = update_result::cleanup_failed;
					} else if (_context->interrupted()) {
						_result->artifact.reset();
						_result->result = _context->cancelled() ? update_result::cancelled : update_result::timed_out;
					}
				} catch (...) {
					// Do not log exception text: URLs/proxy credentials may be embedded.
					_result.emplace(preparation_result{update_result::preparation_failed, std::nullopt});
				}
				_done.store(true, std::memory_order_release);
			});
		} catch (...) {
			_context.reset();
			return update_result::preparation_failed;
		}
		return std::nullopt;
	}

	update_result background_updater::apply_ready(std::function<bool()> const& should_cancel) {
		auto cancelled = [&] {
			if (_cancelled.load(std::memory_order_acquire)) return true;
			if (should_cancel && should_cancel()) {
				request_stop();
				return true;
			}
			return false;
		};
		if (cancelled()) {
			request_stop();
			_ready.reset();
			return update_result::cancelled;
		}
		if (_worker.joinable() || !_ready) return update_result::apply_failed;
		auto artifact = std::move(*_ready);
		_ready.reset();
		auto outcome = _updater.apply(artifact, _argv, cancelled);
		if (outcome == update_result::cancelled) request_stop();
		else cancelled();
		return outcome;
	}

	std::unique_ptr<background_updater> make_background_updater(
		bool enabled, std::uint32_t interval_seconds, std::string const& repo,
		std::string const& proxy, bool insecure, std::string const& tool_name,
		std::string const& current_version_text, std::vector<std::string> argv) {
		auto up = make_configured_updater(enabled, interval_seconds, repo, proxy, insecure,
			tool_name, current_version_text);
		if (!up) return nullptr;
		return std::make_unique<background_updater>(std::move(*up), interval_seconds, std::move(argv));
	}
}
