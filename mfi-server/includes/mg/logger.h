#pragma once

#include <string>
#include <source_location>
#include <tuple>
#include <functional>
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

		template<class... TArgs>
		static void error(const std::string& fmt, std::tuple<TArgs...> args, const std::source_location location = std::source_location::current()) noexcept {
			logger::log(log_level::error, fmt, location, args, std::index_sequence_for<TArgs...>{});
		}
		template<class... TArgs>
		static void info(const std::string& fmt, std::tuple<TArgs...> args, const std::source_location location = std::source_location::current()) noexcept {
			logger::log(log_level::info, fmt, location, args, std::index_sequence_for<TArgs...>{});
		}
		template<class... TArgs>
		static void debug(const std::string& fmt, std::tuple<TArgs...> args, const std::source_location location = std::source_location::current()) noexcept {
			logger::log(log_level::debug, fmt, location, args, std::index_sequence_for<TArgs...>{});
		}
		template<class... TArgs>
		static void verbose(const std::string& fmt, std::tuple<TArgs...> args, const std::source_location location = std::source_location::current()) noexcept {
			logger::log(log_level::verbose, fmt, location, args, std::index_sequence_for<TArgs...>{});
		}
		static void hex_dump(const void* buf, size_t len) noexcept;

	private:
		template<class Tuple, std::size_t... Is>
		static void log(
			log_level level,
			const std::string& fmt,
			const std::source_location location,
			Tuple args,
			std::index_sequence<Is...>) noexcept {
#if MG_ENABLE_LOG
			if (level <= logger::level()) {
				mg_log_prefix(static_cast<int>(level), location.file_name(), location.line(), location.function_name());
				mg_log(fmt.c_str(), std::get<Is>(std::forward<Tuple>(args))...);
			}
#endif
		}
	};
}