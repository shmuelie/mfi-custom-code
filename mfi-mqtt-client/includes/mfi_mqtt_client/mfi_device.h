#pragma once

#include <vector>
#include <memory>
#include "hass_mqtt_device/core/device_base.h"
#include "mfi/board.h"
#include "mfi_mqtt_client/mfi_sensor.h"

namespace mfi_mqtt_client {
	class mfi_device : public DeviceBase {
	public:
		explicit mfi_device(const mfi::board& board) noexcept;

		void update() noexcept;
	private:
		mfi::board _board;
		std::vector<mfi_sensor> _sensors;
	};
}