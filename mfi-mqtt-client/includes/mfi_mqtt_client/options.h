#pragma once

#include "shmuelie/docopt_options.h"

namespace mfi_mqtt_client {
	class options : public shmuelie::docopt_options {
	public:
		options(std::string const& doc, std::vector<std::string> const& argv) noexcept;

		std::string const& server() const noexcept;

		uint16_t port() const noexcept;

		std::string const& username() const noexcept;

		std::string const& password() const noexcept;

		int polling_rate() const noexcept;
	private:
		std::string _server{};
		uint16_t _port = 0;
		std::string _username{};
		std::string _password{};
		int _polling_rate;
	};
}