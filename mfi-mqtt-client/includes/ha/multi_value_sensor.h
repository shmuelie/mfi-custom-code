#pragma once

#include <map>
#include "ha/single_value_sensor.h"

namespace ha {
	class multi_value_sensor : public sensor {
	public:
		std::map<std::string, single_value_sensor> sensors() const noexcept;

		virtual void reset_checks() noexcept override;
		virtual const mg::mqtt_options state(bool respect_checks = false) noexcept override;
	protected:
		explicit multi_value_sensor(const std::string& name, std::chrono::seconds update_interval = 10s, const std::string& id = "") noexcept;

		virtual void update_sensor_values() noexcept = 0;
	};
}