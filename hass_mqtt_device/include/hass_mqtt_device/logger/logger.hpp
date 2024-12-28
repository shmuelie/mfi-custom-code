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
#define LOG_INFO(...) spdlog::info(__VA_ARGS__)
#define LOG_WARN(...) spdlog::warn(__VA_ARGS__)
#define LOG_ERROR(...) spdlog::error(__VA_ARGS__)
#define LOG_DEBUG(...) spdlog::debug(__VA_ARGS__)
#define LOG_TRACE(...) spdlog::trace(__VA_ARGS__)

#else

#define INIT_LOGGER(debug)
#define LOG_INFO(...)
#define LOG_WARN(...)
#define LOG_ERROR(...)
#define LOG_DEBUG(...)
#define LOG_TRACE(...)

#endif