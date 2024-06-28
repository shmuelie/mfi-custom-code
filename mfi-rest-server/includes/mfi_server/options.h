#pragma once

#include "shmuelie/docopt_options.h"

class options : public shmuelie::docopt_options {
public:
	options(std::string const& doc, std::vector<std::string> const& argv) noexcept;

	std::string const& ip() const noexcept;

	std::uint16_t port() const noexcept;

	std::uint8_t log_level() const noexcept;
private:
	std::string _ip{};
	std::uint16_t _port = 0;
	std::uint8_t _logLevel = 0;
};