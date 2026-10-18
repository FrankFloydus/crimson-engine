////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace quader {

/**
 * Enumerates FatalErrorCode values used by the platform layer.
 */
enum class FatalErrorCode {
  UnknownFatal,
  RuntimeAbort,
  UnhandledCppException,
  UnhandledNonStandardException,
  InvalidRuntimeParameter,
  WindowsUnhandledException,
  ManualFatalReport,
  QtFatalMessage,
};

/**
 * Stores the fatal error data contract used by the platform integration layer.
 */
struct FatalErrorInfo {
  const char *code;
  const char *quick_message;
};

/**
 * Stores deterministic report and optional dump paths for one fatal event.
 */
struct FatalArtifactPaths {
  std::filesystem::path report_path;
  std::filesystem::path dump_path;
};

/**
 * Stores platform-neutral data used to format the durable fatal report.
 */
struct FatalReportData {
  std::string generated_at;
  std::uint32_t process_id = 0;
  std::uint32_t thread_id = 0;
  std::string app_version;
  std::filesystem::path executable_path;
  std::string error_code;
  std::string quick_message;
  std::string detail;
  std::filesystem::path log_path;
  std::filesystem::path report_path;
  std::filesystem::path dump_path;
  std::string dump_status;
  std::string exception_record;
  std::string stack_trace;
};

/**
 * Reports which path accepted a fatal report write.
 */
struct FatalReportWriteResult {
  std::filesystem::path path;
  bool written = false;
  bool used_fallback = false;
};

/// Bounds optional out-of-process diagnostics; the text report is written first.
struct FatalDiagnosticsOptions {
  // Empty selects quader_fatal_diagnostics.exe beside the current executable.
  std::filesystem::path helper_path;
  // Clamped to 1..10000 ms so a helper can never indefinitely block fatal exit.
  std::uint32_t timeout_ms = 10'000U;
};

FatalErrorInfo fatal_error_info(FatalErrorCode code);
[[nodiscard]] FatalArtifactPaths make_fatal_artifact_paths(
    const std::filesystem::path &directory, std::string_view timestamp,
    std::uint32_t process_id, std::uint32_t thread_id,
    bool include_dump = true);
[[nodiscard]] std::string
format_fatal_error_report(const FatalReportData &data);
[[nodiscard]] FatalReportWriteResult write_fatal_error_report(
    const std::filesystem::path &preferred_path,
    const std::filesystem::path &fallback_path, std::string_view text);
/** Converts UTF-8 text into the native wide string used by fatal presentation. */
[[nodiscard]] std::wstring
platform_utf8_to_wide(std::string_view value);
void install_fatal_error_handler(std::filesystem::path log_path,
                                  FatalDiagnosticsOptions diagnostics = {});
/** Captures Qt fatal messages before Windows fast-fail; preserves other handlers. */
void install_qt_fatal_error_handler();
[[noreturn]] void report_fatal_error_and_exit(FatalErrorCode code,
                                              const char *detail = nullptr,
                                              int exit_code = 1);
[[noreturn]] void report_fatal_error_and_exit(const char *reason,
                                              int exit_code = 1);

} // namespace quader
