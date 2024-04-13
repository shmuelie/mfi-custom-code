#pragma once

#include <chrono>
#include <string>
#include "ha/discoverable.h"
#include "mg/mqtt_options.h"

namespace ha {
	class sensor : public discoverable {
	public:
		std::chrono::seconds update_interval() const noexcept;
		std::chrono::system_clock::time_point last_updated() const noexcept;

		virtual void reset_checks() noexcept = 0;
		virtual const mg::mqtt_options state(bool respect_checks = false) noexcept = 0;
	protected:
		explicit sensor() noexcept;

		void update_interval(std::chrono::seconds value) noexcept;
		void last_updated(std::chrono::system_clock::time_point value) noexcept;
	private:
		std::chrono::seconds _update_interval;
		std::chrono::system_clock::time_point _last_updated;
	};
}