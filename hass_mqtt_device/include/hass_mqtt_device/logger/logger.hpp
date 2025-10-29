/**
 * @author      Morgan Tørvolt
 * @contributors somebody, hopefully@someday.com
 * @copyright   See LICENSE file
 */

#pragma once

#include <spdlog/spdlog.h>

// Logging macros
#define LOG_INFO(...) m_logger->log(spdlog::source_loc(__FILE__, __LINE__, __func__), spdlog::level::level_enum::info, __VA_ARGS__)
#define LOG_WARN(...) m_logger->log(spdlog::source_loc(__FILE__, __LINE__, __func__), spdlog::level::level_enum::warn, __VA_ARGS__)
#define LOG_ERROR(...) m_logger->log(spdlog::source_loc(__FILE__, __LINE__, __func__), spdlog::level::level_enum::err, __VA_ARGS__)
#define LOG_DEBUG(...) m_logger->log(spdlog::source_loc(__FILE__, __LINE__, __func__), spdlog::level::level_enum::debug, __VA_ARGS__)
#define LOG_TRACE(...) m_logger->log(spdlog::source_loc(__FILE__, __LINE__, __func__), spdlog::level::level_enum::trace, __VA_ARGS__)