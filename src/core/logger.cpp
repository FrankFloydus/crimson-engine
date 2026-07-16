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
  explicit MemorySink(std::size_t max_entries)
      : max_entries_(std::max<std::size_t>(1, max_entries)) {}

  std::vector<LogEntry> snapshot() const {
    std::lock_guard lock(entries_mutex_);
    return {entries_.begin(), entries_.end()};
  }

    void clear() {
      std::lock_guard lock(entries_mutex_);
      entries_.clear();
    }

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        spdlog::memory_buf_t formatted;
        formatter_->format(msg, formatted);

        LogEntry entry;
        entry.level = msg.level;
        entry.logger_name.assign(msg.logger_name.data(),
                                 msg.logger_name.size());
        entry.message = fmt::to_string(formatted);
        if (msg.source.filename != nullptr) {
            entry.file = msg.source.filename;
            entry.line = static_cast<int>(msg.source.line);
        }

        std::lock_guard lock(entries_mutex_);
        entries_.push_back(std::move(entry));
        while (entries_.size() > max_entries_) {
          entries_.pop_front();
        }
    }

    void flush_() override {}

private:
  std::size_t max_entries_ = 0;
  mutable std::mutex entries_mutex_;
  std::deque<LogEntry> entries_;
};

std::mutex g_logger_mutex;
std::shared_ptr<spdlog::logger> g_logger;
std::shared_ptr<MemorySink> g_memory_sink;

void initialize_locked(LoggerConfig config) {
  std::vector<spdlog::sink_ptr> sinks;
  auto memory_sink = std::make_shared<MemorySink>(config.max_entries);
  sinks.push_back(memory_sink);

  try {
    sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(
        config.file_path, true));
  } catch (const spdlog::spdlog_ex &) {
    // File logging should never prevent the editor from booting.
  }

#ifdef _WIN32
  sinks.push_back(std::make_shared<spdlog::sinks::msvc_sink_mt>());
#endif

  auto logger = std::make_shared<spdlog::logger>(config.name, sinks.begin(),
                                                 sinks.end());
  logger->set_level(config.level);
  logger->set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");
  logger->flush_on(spdlog::level::info);
  g_memory_sink = std::move(memory_sink);
  g_logger = std::move(logger);
  spdlog::set_default_logger(g_logger);
}

std::shared_ptr<MemorySink> memory_sink_snapshot() {
  std::lock_guard lock(g_logger_mutex);
  if (!g_logger || !g_memory_sink) {
    initialize_locked({});
  }
  return g_memory_sink;
}

} // namespace

void Logger::initialize(LoggerConfig config) {
  std::lock_guard lock(g_logger_mutex);
  initialize_locked(std::move(config));
}

void Logger::shutdown() {
  std::shared_ptr<spdlog::logger> logger;
  std::shared_ptr<MemorySink> memory_sink;
  {
    std::lock_guard lock(g_logger_mutex);
    logger = std::move(g_logger);
    memory_sink = std::move(g_memory_sink);
    spdlog::drop_all();
  }
  if (logger) {
    logger->flush();
  }
}

void Logger::clear_memory() {
  const std::shared_ptr<MemorySink> memory_sink = memory_sink_snapshot();
  memory_sink->clear();
}

std::vector<LogEntry> Logger::entries_snapshot() {
  const std::shared_ptr<MemorySink> memory_sink = memory_sink_snapshot();
  return memory_sink->snapshot();
}

std::shared_ptr<spdlog::logger> Logger::instance() {
  std::lock_guard lock(g_logger_mutex);
  if (!g_logger || !g_memory_sink) {
    initialize_locked({});
  }
  return g_logger;
}

} // namespace quader
