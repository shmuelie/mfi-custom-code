#pragma once

#include <string>
#include "mongoose.h"

namespace mg {
	enum class log_level {
		none = MG_LL_NONE,
		error = MG_LL_ERROR,
		info = MG_LL_INFO,
		debug = MG_LL_DEBUG,
		verbose = MG_LL_VERBOSE
	};

	class logger {
	public:
		static log_level level() noexcept;
		static void level(log_level value) noexcept;

		template<typename... TArgs>
		static void error(const std::string& fmt, TArgs... args) noexcept {
			MG_ERROR((fmt.c_str(), args...));
		}
		template<typename... TArgs>
		static void info(const std::string& fmt, TArgs... args) noexcept {
			MG_INFO((fmt.c_str(), args...));
		}
		template<typename... TArgs>
		static void debug(const std::string& fmt, TArgs... args) noexcept {
			MG_DEBUG((fmt.c_str(), args...));
		}
		template<typename... TArgs>
		static void verbose(const std::string& fmt, TArgs... args) noexcept {
			MG_VERBOSE((fmt.c_str(), args...));
		}
		static void hex_dump(const void* buf, size_t len) noexcept;
	};
}