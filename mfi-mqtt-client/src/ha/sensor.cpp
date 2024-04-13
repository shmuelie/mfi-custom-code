#include "ha/sensor.h"

using namespace std;
using namespace std::chrono;
using namespace ha;
using namespace mg;

sensor::sensor() noexcept : _update_interval(), _last_updated() {
	domain(discoverable_domain::sensor);
}

seconds sensor::update_interval() const noexcept {
	return _update_interval;
}
void sensor::update_interval(seconds value) noexcept {
	_update_interval = value;
}

system_clock::time_point sensor::last_updated() const noexcept {
	return _last_updated;
}
void sensor::last_updated(system_clock::time_point value) noexcept {
	_last_updated = value;
}