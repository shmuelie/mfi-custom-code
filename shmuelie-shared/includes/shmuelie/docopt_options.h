#pragma once

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <cstdint>
#include "docopt.h"

namespace shmuelie {
	class docopt_options {
	public:
		docopt_options(std::string const& doc, std::vector<std::string> const& argv) noexcept;

		bool help() const noexcept;

		bool version() const noexcept;

		std::vector<std::string> const& errors() const noexcept;

	protected:
		std::optional<std::string> const get_string(std::string const& key) const noexcept;
		std::optional<uint16_t> get_uint16(std::string const& key) const noexcept;
		std::optional<int> get_int(std::string const& key) const noexcept;
		std::optional<uint8_t> get_uint8(std::string const& key) const noexcept;
		void add_error(std::string const& error_str) noexcept;

	private:
		std::map<std::string, docopt::value> _values;
		bool _help = false;
		bool _version = false;
		std::vector<std::string> _errors{};
	};
}