#pragma once

#include "mongoose.h"
#include <string>

namespace std {
	inline std::string to_string(const mg_str& __val) {
		return std::string{ __val.ptr, __val.len };
	}
}