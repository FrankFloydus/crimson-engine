////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <spdlog/common.h>
#include <spdlog/logger.h>
#include <spdlog/spdlog.h>

#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace quader {

/**
 * Represents a Log Entry value used by the shared core service layer.
 */
struct LogEntry {
    spdlog::level::level_enum level = spdlog::level::info;
    std::string logger_name;
    std::string message;
    std::string file;
    int line = 0;
};

/**
 * Represents a Logger Config value used by the shared core service layer.
 */
struct LoggerConfig {
    std::string name = "Quader";
    std::string file_path = "quader.log";
    std::size_t max_entries = 2000;
    spdlog::level::level_enum level = spdlog::level::trace;
};

/**
 * Owns Logger behavior within the shared core service layer.
 */
class Logger {
public:
    static void initialize(LoggerConfig config = {});
    static void shutdown();
    static void clear_memory();
    static std::vector<LogEntry> entries_snapshot();
    static std::shared_ptr<spdlog::logger> instance();

    template <typename... Args>
    static void log(spdlog::source_loc loc,
            spdlog::level::level_enum level,
            spdlog::format_string_t<Args...> fmt,
            Args&&... args) {
      if (std::shared_ptr<spdlog::logger> logger = instance()) {
        logger->log(loc, level, fmt, std::forward<Args>(args)...);
      }
    }
};

} // namespace quader

#define LOG_TRACE(...) ::quader::Logger::log(spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, spdlog::level::trace, __VA_ARGS__)
#define LOG_DEBUG(...) ::quader::Logger::log(spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, spdlog::level::debug, __VA_ARGS__)
#define LOG_INFO(...) ::quader::Logger::log(spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, spdlog::level::info, __VA_ARGS__)
#define LOG_WARN(...) ::quader::Logger::log(spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, spdlog::level::warn, __VA_ARGS__)
#define LOG_ERROR(...) ::quader::Logger::log(spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, spdlog::level::err, __VA_ARGS__)
#define LOG_CRITICAL(...) ::quader::Logger::log(spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, spdlog::level::critical, __VA_ARGS__)
