#pragma once

#include "mfi_mqtt_client/system_stats.h"
#include "hass_mqtt_device/functions/sensor.h"
#include <array>
#include <chrono>
#include <memory>
#include <optional>
#include <string>

namespace mfi_mqtt_client {
	struct system_metrics_options {
		bool enabled = true;
		std::chrono::seconds polling_interval{10};
		std::chrono::seconds refresh_interval{60};
		std::chrono::seconds expire_after{180};
		std::string proc_root = "/proc";

		void validate() const;
	};

	class system_metrics final {
	public:
		using clock = std::chrono::steady_clock;

		explicit system_metrics(system_metrics_options const& options = {});
		void init(std::shared_ptr<DeviceBase> const& device);
		void reset_connection();
		void update(clock::time_point now = clock::now());

	private:
		enum metric { cpu, memory_total, memory_available, memory_used, memory_utilization, count };
		std::array<std::shared_ptr<SensorFunction<double>>, count> _sensors;
		system_stats_reader _reader;
		std::chrono::seconds _polling_interval;
		std::optional<clock::time_point> _next_sample;
		std::optional<bool> _estimated;
	};
}
