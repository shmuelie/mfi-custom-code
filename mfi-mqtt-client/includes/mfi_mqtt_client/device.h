#pragma once

#include <vector>
#include <memory>
#include "mfi.h"
#include "hass_mqtt_device/core/mqtt_connector.h"
#include "mfi_mqtt_client/port.h"

namespace mfi_mqtt_client {
	class device : public DeviceBase {
	public:
		explicit device(mfi::board const& board, std::string const& server, int port, std::string const& username, std::string const& password);

		void init();
		void update();
		bool connect();
		void processMessages(int timeout);
	protected:
		virtual std::string getManufacturer() const override;
		virtual std::string getModel() const override;
		virtual std::string getSoftwareVersion() const override;
		virtual std::string getModelId() const override;
		virtual std::string getConfigurationUrl() const override;
	private:
		std::shared_ptr<MQTTConnector> _connector;
		std::vector<std::shared_ptr<port>> _ports;
		mfi::board _board;
	};
}