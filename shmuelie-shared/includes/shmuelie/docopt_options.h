#pragma once

#include <string>
#include <vector>
#include <map>
#include "docopt.h"

namespace shmuelie {
	class docopt_options {
	public:
		explicit docopt_options(const std::string& doc, const std::vector<std::string>& argv) noexcept;

		bool help() const noexcept;

		bool version() const noexcept;

		const std::vector<std::string>& errors() const noexcept;

	protected:
		const std::map<std::string, docopt::value>& values() const noexcept;
		void add_error(const std::string& error_str) noexcept;

	private:
		std::map<std::string, docopt::value> _values;
		bool _help = false;
		bool _version = false;
		std::vector<std::string> _errors{};
	};
}