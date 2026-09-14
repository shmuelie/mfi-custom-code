#include "mfi_mqtt_client/system_metrics.h"
#include <stdexcept>
#include <spdlog/spdlog.h>

namespace mfi_mqtt_client {
	void system_metrics_options::validate() const {
		if (polling_interval.count() < 1 || polling_interval.count() > 86400
			|| refresh_interval.count() < 1 || refresh_interval.count() > 86400
			|| expire_after.count() < 1 || expire_after.count() > 259200) {
			throw std::invalid_argument("System sampling/refresh must be 1-86400 seconds and expiry 1-259200 seconds");
		}
		if (polling_interval > refresh_interval || expire_after.count() / refresh_interval.count() < 3) {
			throw std::invalid_argument("System expiry must allow three refresh intervals, and sampling must not exceed refresh");
		}
		if (proc_root.empty()) {
			throw std::invalid_argument("System proc root must not be empty");
		}
	}

	system_metrics::system_metrics(system_metrics_options const& options) :
		_reader(options.proc_root),
		_polling_interval(options.polling_interval) {
		options.validate();
		auto policy = sensor_policy::telemetry(options.refresh_interval, options.expire_after);
		auto make_sensor = [&](char const* name, bool memory_size) {
			return std::make_shared<SensorFunction<double>>(name, SensorAttributes{
				.device_class = memory_size ? "data_size" : "",
				.state_class = "measurement",
				.unit_of_measurement = memory_size ? "MiB" : "%",
				.suggested_display_precision = 1,
				.entity_category = "diagnostic"
			}, policy);
		};
		_sensors = {
			make_sensor("CPU Utilization", false),
			make_sensor("Memory Total", true),
			make_sensor("Memory Available", true),
			make_sensor("Memory Used", true),
			make_sensor("Memory Utilization", false)
		};
	}

	void system_metrics::init(std::shared_ptr<DeviceBase> const& device) {
		for (auto const& sensor : _sensors) {
			device->registerFunction(sensor);
		}
	}

	void system_metrics::reset_connection() {
		_reader.reset_cpu();
		_next_sample.reset();
		for (auto const& sensor : _sensors) {
			sensor->await_sample();
		}
	}

	void system_metrics::update(clock::time_point now) {
		if (_next_sample && now < *_next_sample) {
			return;
		}
		_next_sample = now + _polling_interval;

		auto cpu_result = _reader.read_cpu();
		if (auto value = std::get_if<double>(&cpu_result)) {
			_sensors[cpu]->update(*value, now);
		}
		else if (auto error = std::get_if<system_read_error>(&cpu_result)) {
			_sensors[cpu]->invalidate(error->message);
		}
		else {
			_sensors[cpu]->await_sample();
		}

		auto memory_result = _reader.read_memory();
		if (auto value = std::get_if<memory_stats>(&memory_result)) {
			if (!_estimated || *_estimated != value->estimated) {
				if (value->estimated) {
					spdlog::warn("MemAvailable is absent; estimating available RAM from free memory, buffers and cache");
				}
				else if (_estimated) {
					spdlog::info("Memory reporting now uses kernel MemAvailable");
				}
				_estimated = value->estimated;
			}
			_sensors[memory_total]->update(value->total_mib, now);
			_sensors[memory_available]->update(value->available_mib, now);
			_sensors[memory_used]->update(value->used_mib, now);
			_sensors[memory_utilization]->update(value->utilization, now);
		}
		else {
			auto const& error = std::get<system_read_error>(memory_result);
			for (auto index : {memory_total, memory_available, memory_used, memory_utilization}) {
				_sensors[index]->invalidate(error.message);
			}
		}
	}
}
