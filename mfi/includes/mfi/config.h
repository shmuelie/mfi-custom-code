#pragma once

#include <string>
#include <map>

namespace mfi {
	class config final {
	public:
		static std::map<std::string, std::string> const read_all(const std::string& file);
		static std::string const read(const std::string& file, const std::string& prop);
		static std::string const read(const std::string& file, const std::string& prop, const std::string& defaultValue);
	};
}