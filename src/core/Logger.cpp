////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "core/logger.hpp"

#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/basic_file_sink.h>
#ifdef _WIN32
#include <spdlog/sinks/msvc_sink.h>
#endif

#include <algorithm>
#include <deque>
#include <utility>

namespace quader {
namespace {

/**
 * Owns Memory Sink behavior within the shared core service layer.
 */
class MemorySink final : public spdlog::sinks::base_sink<std::mutex> {
public:
    explicit MemorySink(std::size_t maxEntries) : maxEntries_(std::max<std::size_t>(1, maxEntries)) {}

    std::vector<LogEntry> snapshot() const {
        std::lock_guard lock(entriesMutex_);
        return {entries_.begin(), entries_.end()};
    }

    void clear() {
        std::lock_guard lock(entriesMutex_);
        entries_.clear();
    }

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        spdlog::memory_buf_t formatted;
        formatter_->format(msg, formatted);

        LogEntry entry;
        entry.level = msg.level;
        entry.loggerName.assign(msg.logger_name.data(), msg.logger_name.size());
        entry.message = fmt::to_string(formatted);
        if (msg.source.filename != nullptr) {
            entry.file = msg.source.filename;
            entry.line = static_cast<int>(msg.source.line);
        }

        std::lock_guard lock(entriesMutex_);
        entries_.push_back(std::move(entry));
        while (entries_.size() > maxEntries_) {
            entries_.pop_front();
        }
    }

    void flush_() override {}

private:
    std::size_t maxEntries_ = 0;
    mutable std::mutex entriesMutex_;
    std::deque<LogEntry> entries_;
};

std::mutex gLoggerMutex;
std::shared_ptr<spdlog::logger> gLogger;
std::shared_ptr<MemorySink> gMemorySink;

} // namespace

void Logger::initialize(LoggerConfig config) {
    std::lock_guard lock(gLoggerMutex);

    std::vector<spdlog::sink_ptr> sinks;
    gMemorySink = std::make_shared<MemorySink>(config.maxEntries);
    sinks.push_back(gMemorySink);

    try {
        sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(config.filePath, true));
    } catch (const spdlog::spdlog_ex&) {
        // File logging should never prevent the editor from booting.
    }

#ifdef _WIN32
    sinks.push_back(std::make_shared<spdlog::sinks::msvc_sink_mt>());
#endif

    gLogger = std::make_shared<spdlog::logger>(config.name, sinks.begin(), sinks.end());
    gLogger->set_level(config.level);
    gLogger->set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");
    gLogger->flush_on(spdlog::level::info);
    spdlog::set_default_logger(gLogger);
}

void Logger::shutdown() {
    std::lock_guard lock(gLoggerMutex);
    if (gLogger) {
        gLogger->flush();
    }
    spdlog::drop_all();
    gLogger.reset();
    gMemorySink.reset();
}

void Logger::clearMemory() {
    ensureInitialized();
    gMemorySink->clear();
}

std::vector<LogEntry> Logger::entriesSnapshot() {
    ensureInitialized();
    return gMemorySink->snapshot();
}

std::shared_ptr<spdlog::logger> Logger::instance() {
    ensureInitialized();
    return gLogger;
}

void Logger::ensureInitialized() {
    std::lock_guard lock(gLoggerMutex);
    if (!gLogger) {
        LoggerConfig config;
        std::vector<spdlog::sink_ptr> sinks;
        gMemorySink = std::make_shared<MemorySink>(config.maxEntries);
        sinks.push_back(gMemorySink);
#ifdef _WIN32
        sinks.push_back(std::make_shared<spdlog::sinks::msvc_sink_mt>());
#endif
        gLogger = std::make_shared<spdlog::logger>(config.name, sinks.begin(), sinks.end());
        gLogger->set_level(config.level);
        gLogger->set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");
        gLogger->flush_on(spdlog::level::info);
        spdlog::set_default_logger(gLogger);
    }
}

} // namespace quader
