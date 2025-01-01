#pragma once

#include <memory>
#include "mfi.h"
#include "hass_mqtt_device/core/mqtt_connector.h"
#include "hass_mqtt_device/functions/switch.h"
#include "hass_mqtt_device/functions/sensor.h"

namespace mfi_mqtt_client {
	class port {
	public:
		explicit port(mfi::board const& board, mfi::sensor const& sensor);

		void init(std::shared_ptr<DeviceBase> const& device);
		void update();
		void relay(bool value);
	private:
		mfi::sensor _sensor;
		std::shared_ptr<SensorFunction<double>> _power;
		std::shared_ptr<SensorFunction<double>> _current;
		std::shared_ptr<SensorFunction<double>> _voltage;
		std::shared_ptr<SwitchFunction> _relay;
	};
}