#pragma once

#include <string>
#include <vector>

class options {
public:
	explicit options(const std::string& doc, const std::vector<std::string>& argv) noexcept;

	const std::string& ip() const noexcept;

	std::uint16_t port() const noexcept;

	std::uint8_t log_level() const noexcept;

	bool help() const noexcept;

	bool version() const noexcept;

	const std::vector<std::string>& errors() const noexcept;
private:
	std::string _ip{};
	std::uint16_t _port = 0;
	std::uint8_t _logLevel = 0;
	bool _help = false;
	bool _version = false;
	std::vector<std::string> _errors{};
};