#pragma once

#include <filesystem>

namespace quader {

enum class FatalErrorCode {
    UnknownFatal,
    RuntimeAbort,
    UnhandledCppException,
    UnhandledNonStandardException,
    InvalidRuntimeParameter,
    WindowsUnhandledException,
    ManualFatalReport,
};

struct FatalErrorInfo {
    const char* code;
    const char* quickMessage;
};

FatalErrorInfo fatalErrorInfo(FatalErrorCode code);
void installFatalErrorHandler(std::filesystem::path logPath);
[[noreturn]] void reportFatalErrorAndExit(FatalErrorCode code, const char* detail = nullptr, int exitCode = 1);
[[noreturn]] void reportFatalErrorAndExit(const char* reason, int exitCode = 1);

} // namespace quader
