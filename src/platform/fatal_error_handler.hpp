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
};

/**
 * Stores the Fatal Error Info data contract used by the platform integration layer.
 */
struct FatalErrorInfo {
    const char* code;
    const char *quick_message;
};

FatalErrorInfo fatal_error_info(FatalErrorCode code);
/** Converts UTF-8 text into the native wide string used by fatal presentation. */
[[nodiscard]] std::wstring
platform_utf8_to_wide(std::string_view value);
void install_fatal_error_handler(std::filesystem::path log_path);
[[noreturn]] void report_fatal_error_and_exit(FatalErrorCode code,
                                              const char *detail = nullptr,
                                              int exit_code = 1);
[[noreturn]] void report_fatal_error_and_exit(const char *reason,
                                              int exit_code = 1);

} // namespace quader
