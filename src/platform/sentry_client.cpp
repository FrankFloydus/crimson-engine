////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "platform/sentry_client.hpp"

#include "core/logger.hpp"

#include <atomic>
#include <filesystem>
#include <string>
#include <system_error>

#if defined(QUADER_ENABLE_SENTRY) && QUADER_ENABLE_SENTRY
#include <sentry.h>
#endif

namespace quader {
namespace {

#if defined(QUADER_ENABLE_SENTRY) && QUADER_ENABLE_SENTRY
std::atomic_bool g_sentry_initialized{false};

sentry_level_t sentry_level(SentryEventLevel level) {
  switch (level) {
  case SentryEventLevel::Error:
    return SENTRY_LEVEL_ERROR;
  case SentryEventLevel::Fatal:
    return SENTRY_LEVEL_FATAL;
  case SentryEventLevel::Info:
  default:
    return SENTRY_LEVEL_INFO;
  }
}

void configure_sentry_path_options(sentry_options_t *sentry_options,
                                   const SentryClientOptions &options) {
  if (!options.database_path.empty()) {
#if defined(_WIN32)
    const std::wstring database_path = options.database_path.wstring();
    sentry_options_set_database_pathw(sentry_options, database_path.c_str());
#else
    const std::string database_path = options.database_path.string();
    sentry_options_set_database_path(sentry_options, database_path.c_str());
#endif
  }

  if (!options.handler_path.empty()) {
    if (!std::filesystem::exists(options.handler_path)) {
      LOG_WARN("Sentry crash handler is missing at {}",
               options.handler_path.string());
    }
#if defined(_WIN32)
    const std::wstring handler_path = options.handler_path.wstring();
    sentry_options_set_handler_pathw(sentry_options, handler_path.c_str());
#else
    const std::string handler_path = options.handler_path.string();
    sentry_options_set_handler_path(sentry_options, handler_path.c_str());
#endif
  }
}
#endif

} // namespace

bool initialize_sentry_client(const SentryClientOptions &options) {
#if defined(QUADER_ENABLE_SENTRY) && QUADER_ENABLE_SENTRY
  if (g_sentry_initialized.load()) {
    return true;
  }
  if (options.dsn.empty()) {
    LOG_WARN("Sentry Native startup skipped: DSN is empty");
    return false;
  }

  if (!options.database_path.empty()) {
    std::error_code error;
    std::filesystem::create_directories(options.database_path, error);
    if (error) {
      LOG_WARN("Unable to create Sentry database path {}: {}",
               options.database_path.string(), error.message());
    }
  }

  sentry_options_t *sentry_options = sentry_options_new();
  if (sentry_options == nullptr) {
    LOG_WARN("Sentry Native startup skipped: options allocation failed");
    return false;
  }

  sentry_options_set_dsn(sentry_options, options.dsn.c_str());
  if (!options.release.empty()) {
    sentry_options_set_release(sentry_options, options.release.c_str());
  }
  if (!options.environment.empty()) {
    sentry_options_set_environment(sentry_options, options.environment.c_str());
  }
  sentry_options_set_debug(sentry_options, options.debug ? 1 : 0);
  configure_sentry_path_options(sentry_options, options);

  const int init_result = sentry_init(sentry_options);
  if (init_result != 0) {
    LOG_WARN("Sentry Native initialization failed with code {}", init_result);
    return false;
  }

  g_sentry_initialized.store(true);
  LOG_INFO("Sentry Native initialized for release {} using database {}",
           options.release, options.database_path.string());
  return true;
#else
  (void)options;
  return false;
#endif
}

void capture_sentry_message(SentryEventLevel level, std::string_view logger,
                            std::string_view message) {
#if defined(QUADER_ENABLE_SENTRY) && QUADER_ENABLE_SENTRY
  if (!g_sentry_initialized.load()) {
    LOG_WARN("Skipped Sentry event capture because Sentry is not initialized");
    return;
  }

  const std::string stable_logger(logger);
  const std::string stable_message(message);
  sentry_capture_event(sentry_value_new_message_event(
      sentry_level(level), stable_logger.c_str(), stable_message.c_str()));
  LOG_INFO("Queued Sentry event: {}", stable_message);
#else
  (void)level;
  (void)logger;
  (void)message;
#endif
}

void shutdown_sentry_client() {
#if defined(QUADER_ENABLE_SENTRY) && QUADER_ENABLE_SENTRY
  if (!g_sentry_initialized.exchange(false)) {
    return;
  }
  const int dumped_envelopes = sentry_close();
  LOG_INFO("Sentry Native shut down; dumped envelopes: {}", dumped_envelopes);
#endif
}

bool sentry_client_is_initialized() {
#if defined(QUADER_ENABLE_SENTRY) && QUADER_ENABLE_SENTRY
  return g_sentry_initialized.load();
#else
  return false;
#endif
}

} // namespace quader
