#pragma once

#include "mongoose.h"
#include <string>

namespace std {
	inline std::string to_string(mg_str const& __val) {
		return std::string{ __val.ptr, __val.len };
	}

	inline mg_str to_mg_str(std::string const& __val) {
		return mg_str_n(__val.c_str(), __val.size());
	}
}