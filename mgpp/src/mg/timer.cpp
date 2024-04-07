#include "mg/timer.h"
#include "mg/manager.h"

using namespace std;
using namespace std::chrono;
using namespace mg;

timer::timer(const shared_ptr<manager>& manager, milliseconds period, bool repeating, bool run_now, function<void()> callback) noexcept : _manager(manager), _callback(callback) {
	unsigned int flags = MG_TIMER_ONCE;
	if (repeating) {
		flags |= MG_TIMER_REPEAT;
	}
	if (run_now) {
		flags |= MG_TIMER_RUN_NOW;
	}

	_timer = mg_timer_add(manager->_manager, period.count(), flags, &timer::callback, this);
}

timer::~timer() noexcept {
	stop();
}

void timer::stop() noexcept {
	if (running()) {
		mg_timer_free(&_manager->_manager->timers, _timer);
		_timer = nullptr;
	}
}

milliseconds timer::expire() const noexcept {
	if (running()) {
		return milliseconds{ _timer->expire };
	}
	return milliseconds::zero();
}

milliseconds timer::period() const noexcept {
	if (running()) {
		return milliseconds{ _timer->period_ms };
	}
	return milliseconds::zero();
}

bool timer::running() const noexcept {
	return _timer != nullptr;
}

bool timer::repeating() const noexcept {
	if (running()) {
		return (_timer->flags & MG_TIMER_REPEAT) == MG_TIMER_REPEAT;
	}
	return false;
}

void timer::callback(void* arg) noexcept {
	timer* self = static_cast<timer*>(arg);
	self->_callback();
}
