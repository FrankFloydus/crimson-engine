////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <filesystem>

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
    const char* quickMessage;
};

FatalErrorInfo fatalErrorInfo(FatalErrorCode code);
void installFatalErrorHandler(std::filesystem::path logPath);
[[noreturn]] void reportFatalErrorAndExit(FatalErrorCode code, const char* detail = nullptr, int exitCode = 1);
[[noreturn]] void reportFatalErrorAndExit(const char* reason, int exitCode = 1);

} // namespace quader
