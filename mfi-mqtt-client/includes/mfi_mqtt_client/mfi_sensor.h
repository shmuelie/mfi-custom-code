#pragma once

#include <memory>
#include "hass_mqtt_device/core/device_base.h"
#include "hass_mqtt_device/functions/switch.h"
#include "mfi.h"
#include "mfi_mqtt_client/functions/current.h"
#include "mfi_mqtt_client/functions/power.h"
#include "mfi_mqtt_client/functions/voltage.h"

namespace mfi_mqtt_client {
	class mfi_sensor : public DeviceBase {
	public:
		mfi_sensor(mfi::board const& board, mfi::sensor const& sensor);

		void init();
		void update();
		void relay(bool value);
	protected:
		virtual std::string getManufacturer() const override;
		virtual std::string getModel() const override;
		virtual std::string getVersion() const override;
	private:
		mfi::sensor _sensor;
		std::shared_ptr<functions::power> _power;
		std::shared_ptr<functions::current> _current;
		std::shared_ptr<functions::voltage> _voltage;
		std::shared_ptr<SwitchFunction> _relay;
		std::string _model;
		std::string _version;
	};
}