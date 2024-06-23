#pragma once

#include <vector>
#include <memory>
#include "mfi.h"
#include "mfi_mqtt_client/mfi_sensor.h"
#include "hass_mqtt_device/core/mqtt_connector.h"
#include "hass_mqtt_device/devices/on_off_light.h"

namespace mfi_mqtt_client {
	class mfi_device : public MQTTConnector {
	public:
		explicit mfi_device(const mfi::board& board, const std::string& server, int port, const std::string& username, const std::string& password) noexcept;

		void update() noexcept;
		mfi::led led() const noexcept;
	private:
		mfi::board _board;
		mfi::led _led;
		std::vector<std::shared_ptr<mfi_sensor>> _sensors;
		std::shared_ptr<OnOffLightDevice> _light;
	};
}