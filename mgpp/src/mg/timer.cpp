#include "mg/timer.h"
#include "mg/manager.h"

using namespace std;
using namespace std::chrono;
using namespace mg;

timer::timer(mg_timer* timer, manager* manager) noexcept : _timer(timer), _manager(manager) {
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
