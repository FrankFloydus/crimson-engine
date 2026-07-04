////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace quader {

inline constexpr std::string_view kQuaderSentryDsn =
    "https://98a8a85b3607d260260db1ec475c4669@o1000722.ingest.us.sentry.io/4511676431204352";

/**
 * Enumerates Sentry event levels used by the platform diagnostics boundary.
 */
enum class SentryEventLevel {
  Info,
  Error,
  Fatal,
};

/**
 * Stores Sentry Native startup configuration for Quader.
 */
struct SentryClientOptions {
  std::string dsn;
  std::filesystem::path database_path;
  std::filesystem::path handler_path;
  std::string release;
  std::string environment;
  bool debug = false;
};

[[nodiscard]] bool initialize_sentry_client(
    const SentryClientOptions &options);
void capture_sentry_message(SentryEventLevel level, std::string_view logger,
                            std::string_view message);
void shutdown_sentry_client();
[[nodiscard]] bool sentry_client_is_initialized();

} // namespace quader
