#pragma once

#include "mg/mqtt_client.h"
#include "mg/timer.h"
#include "mfi.h"

namespace mfi_mqtt {
	class mfi_mqtt_client : public mg::mqtt_client {
	public:
		explicit mfi_mqtt_client(const std::shared_ptr<mg::manager>& manager, const std::string& url, const mg::mqtt_options& options) noexcept;
	protected:
		virtual void message_handler(const mg::mqtt_message& message) noexcept override;
		virtual void open_handler() noexcept override;
	private:
		void announce_availability() const noexcept;
		std::unique_ptr<mg::timer> _availability_timer;
		std::string _hostname;
		std::string _device_json;
		mfi::board _board{};
		std::string _integration_json;
		std::string _availability_topic;
		std::string _integration_topic;
	};
}