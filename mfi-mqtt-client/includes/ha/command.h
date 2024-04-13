#pragma once

#include <chrono>
#include "ha/discoverable.h"

namespace ha {
	class command : public discoverable {
	public:
		std::chrono::seconds update_interval() const noexcept;
		std::chrono::system_clock::time_point last_updated() const noexcept;
		const std::string previous_published_state() const noexcept;
	protected:
		explicit command(const std::string& name, discoverable_domain domain = discoverable_domain::_switch, const std::string& id) noexcept;

		virtual const std::string state() noexcept = 0;
		void last_updated(std::chrono::system_clock::time_point value) noexcept;
	private:
		std::chrono::system_clock::time_point _last_updated;
	};
}