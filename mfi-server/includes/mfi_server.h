#pragma once

#include <string>
#include "mg/http_server.h"
#include "mfi.h"

class mfi_server : public mg::http_server {
public:
	explicit mfi_server() noexcept;

protected:
	virtual mg::http_response http_handler(const mg::http_message& message) noexcept override;

private:
	mfi::board _board{};
};
