#pragma once

#include <string>
#include <vector>
#include "mg/http_server.h"
#include "mfi.h"

class mfi_server : public mg::http_server {
public:
	explicit mfi_server() noexcept;

protected:
	virtual mg::http_response http_handler(const mg::http_message& message) noexcept override;

private:
	mg::http_response status_handler() noexcept;
	mg::http_response sensor_handler(const std::string& method, const std::vector<std::string>& captures, const std::string& body) noexcept;

	mfi::board _board{};
};
