#pragma once

#include "shmuelie/docopt_options.h"

namespace mfi_mqtt_client {
	class options : public shmuelie::docopt_options {
	public:
		explicit options(const std::string& doc, const std::vector<std::string>& argv) noexcept;

		const std::string& server() const noexcept;

		uint16_t port() const noexcept;

		const std::string& username() const noexcept;

		const std::string& password() const noexcept;
	private:
		std::string _server{};
		uint16_t _port = 0;
		std::string _username{};
		std::string _password{};
	};
}