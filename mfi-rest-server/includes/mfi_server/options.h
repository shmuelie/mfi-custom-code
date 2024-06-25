#pragma once

#include "shmuelie/docopt_options.h"

class options : public shmuelie::docopt_options {
public:
	explicit options(const std::string& doc, const std::vector<std::string>& argv) noexcept;

	const std::string& ip() const noexcept;

	std::uint16_t port() const noexcept;

	std::uint8_t log_level() const noexcept;
private:
	std::string _ip{};
	std::uint16_t _port = 0;
	std::uint8_t _logLevel = 0;
};