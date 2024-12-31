/**
 * @author      Morgan Tørvolt
 * @contributors somebody, hopefully@someday.com
 * @copyright   See LICENSE file
 */

#pragma once

#if __has_include(<spdlog/spdlog.h>)

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

 // Initialize the default logger with an output to console
#define INIT_LOGGER(debug)                                                     \
  spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");                   \
  spdlog::stdout_color_mt("console");                                          \
  spdlog::set_level(debug ? spdlog::level::trace : spdlog::level::info)

// Logging macros
#define LOG_INFO(...) spdlog::log(spdlog::source_loc(__FILE__, __LINE__, __func__), \
						spdlog::level::level_enum::info, __VA_ARGS__)
#define LOG_WARN(...) spdlog::log(spdlog::source_loc(__FILE__, __LINE__, __func__), \
						spdlog::level::level_enum::warn, __VA_ARGS__)
#define LOG_ERROR(...) spdlog::log(spdlog::source_loc(__FILE__, __LINE__, __func__), \
						spdlog::level::level_enum::err, __VA_ARGS__)
#define LOG_DEBUG(...) spdlog::log(spdlog::source_loc(__FILE__, __LINE__, __func__), \
						spdlog::level::level_enum::debug, __VA_ARGS__)
#define LOG_TRACE(...) spdlog::log(spdlog::source_loc(__FILE__, __LINE__, __func__), \
						spdlog::level::level_enum::trace, __VA_ARGS__)

#else

#define INIT_LOGGER(debug)
#define LOG_INFO(...)
#define LOG_WARN(...)
#define LOG_ERROR(...)
#define LOG_DEBUG(...)
#define LOG_TRACE(...)

#endif