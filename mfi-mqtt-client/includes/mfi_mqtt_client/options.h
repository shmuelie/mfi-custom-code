#pragma once

#include <string>
#include <vector>

namespace mfi_mqtt_client {
	class options {
	public:
		explicit options(const std::string& doc, const std::vector<std::string>& argv) noexcept;

		bool help() const noexcept;

		bool version() const noexcept;

		const std::vector<std::string>& errors() const noexcept;

		const std::string& server() const noexcept;

		int port() const noexcept;

		const std::string& username() const noexcept;

		const std::string& password() const noexcept;
	private:
		std::string _server{};
		int _port = 0;
		std::string _username{};
		std::string _password{};
		bool _help = false;
		bool _version = false;
		std::vector<std::string> _errors{};
	};
}