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
#include <cstdint>
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
    std::string message; // Compatibility projection including the log formatter.
    std::string file;
    int line = 0;
    std::uint64_t sequence = 0;
    // Structured source values let retained consumers choose their presentation.
    spdlog::log_clock::time_point timestamp{};
    std::string payload;
};

/**
 * Identifies the last in-memory log entry observed by a retained consumer.
 */
struct LogEntriesCursor {
    std::uint64_t generation = 0;
    std::uint64_t sequence = 0;

    friend bool operator==(const LogEntriesCursor &,
                           const LogEntriesCursor &) = default;
};

/**
 * Carries only the in-memory log changes after a consumer cursor.
 */
struct LogEntriesDelta {
    LogEntriesCursor cursor;
    std::uint64_t first_sequence = 0;
    std::uint64_t revision = 0;
    std::vector<LogEntry> entries;
    bool reset_required = false;
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
    [[nodiscard]] static std::uint64_t memory_revision() noexcept;
    static std::vector<LogEntry> entries_snapshot();
    static LogEntriesDelta entries_since(LogEntriesCursor cursor);
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
