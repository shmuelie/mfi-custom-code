#pragma once

#include <string>
#include <vector>
#include <regex>
#include "mg/http_server.h"
#include "mfi.h"

class mfi_http_server : public mg::http_server {
public:
	mfi_http_server() noexcept;

protected:
	virtual mg::http_response http_handler(mg::http_message const& message) noexcept override;

private:
	mg::http_response sensor_handler() noexcept;
	mg::http_response sensor_handler(std::string const& method, std::uint8_t sensorId, std::string const& body) noexcept;
	mg::http_response led_handler(std::string const& method, std::string const& body) noexcept;
	mg::http_response info_handler() noexcept;

	mfi::board _board{};
	std::regex _v2Regex{};
};
