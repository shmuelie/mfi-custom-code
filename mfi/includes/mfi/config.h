#pragma once

#include <string>
#include <map>

namespace mfi {
	class config {
	public:
		static const std::map<std::string, std::string> read_all(const std::string& file);
		static const std::string read(const std::string& file, const std::string& prop);
	};
}