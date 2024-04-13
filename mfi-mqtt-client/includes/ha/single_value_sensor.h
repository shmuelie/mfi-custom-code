#pragma once

#include "ha/sensor.h"

namespace ha {
	class single_value_sensor : public sensor {
	public:
		const std::string previous_published_state() const noexcept;
		const std::string previous_published_attributes() const noexcept;

		virtual void reset_checks() noexcept override;
		virtual const mg::mqtt_options state(bool respect_checks = false) noexcept override;
	protected:
		explicit single_value_sensor(const std::string& name, std::chrono::seconds update_interval = 10s, const std::string& id = "", bool use_attributes = false) noexcept;

		virtual const std::string state() noexcept = 0;
		virtual const std::string attributes() noexcept = 0;
	};
}