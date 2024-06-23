#pragma once

#include <vector>
#include <memory>
#include "mfi/board.h"
#include "mfi_mqtt_client/mfi_sensor.h"
#include "hass_mqtt_device/core/mqtt_connector.h"

namespace mfi_mqtt_client {
	class mfi_device : public MQTTConnector {
	public:
		explicit mfi_device(const mfi::board& board, const std::string& server, int port, const std::string& username, const std::string& password) noexcept;

		void update() noexcept;
	private:
		mfi::board _board;
		std::vector<std::shared_ptr<mfi_sensor>> _sensors;
	};
}