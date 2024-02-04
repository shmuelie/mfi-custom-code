#pragma once

#include "mongoose.h"
#include <string>

namespace std {
	inline std::string to_string(const mg_str& __val) {
		return std::string{ __val.ptr, __val.len };
	}

	inline mg_str to_mg_str(const std::string& __val) {
		return mg_str_n(__val.c_str(), __val.size());
	}
}