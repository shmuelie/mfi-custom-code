#pragma once

#include <string>
#include <vector>
#include <regex>
#include "mg/http_server.h"
#include "mfi.h"

class mfi_http_server : public mg::http_server {
public:
	explicit mfi_http_server() noexcept;

protected:
	virtual mg::http_response http_handler(const mg::http_message& message) noexcept override;

private:
	mg::http_response sensor_handler() noexcept;
	mg::http_response sensor_handler(const std::string& method, std::uint8_t sensorId, const std::string& body) noexcept;
	mg::http_response led_handler(const std::string& method, const std::string& body) noexcept;

	mfi::board _board{};
	std::regex _v2Regex{};
};
