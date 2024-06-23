#pragma once

#include "hass_mqtt_device/functions/sensor.h"

namespace mfi_mqtt_client::functions {
	class voltage : public SensorFunction<double> {
	public:
		explicit voltage() noexcept;
	};
}