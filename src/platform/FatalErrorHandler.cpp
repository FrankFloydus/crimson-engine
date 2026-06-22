////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "platform/FatalErrorHandler.h"

#include "core/Logger.h"

#include <atomic>
#include <array>
#include <chrono>
#include <cstdint>
#include <csignal>
#include <ctime>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <system_error>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <DbgHelp.h>
#endif

#if defined(_MSC_VER)
#include <crtdbg.h>
#endif

namespace quader {
namespace {

std::filesystem::path gLogPath;
std::atomic_bool gHandlingFatalError{false};

/**
 * Describes the crash artifacts written by the fatal error boundary.
 */
struct CrashArtifacts {
    std::filesystem::path reportPath;
    std::filesystem::path dumpPath;
    std::string dumpStatus;
};

#if defined(_WIN32)
using FatalExceptionPointers = EXCEPTION_POINTERS;
#else
using FatalExceptionPointers = void;
#endif

#if defined(_WIN32)
std::mutex gSymbolMutex;
bool gSymbolsInitialized = false;

std::uint32_t currentProcessId() {
    return static_cast<std::uint32_t>(GetCurrentProcessId());
}

std::uint32_t currentThreadId() {
    return static_cast<std::uint32_t>(GetCurrentThreadId());
}
#else
std::uint32_t currentProcessId() {
    return 0;
}

std::uint32_t currentThreadId() {
    return 0;
}
#endif

std::wstring widen(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    const int required = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (required <= 0) {
        return std::wstring(value.begin(), value.end());
    }
    std::wstring wide(static_cast<std::size_t>(required - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, wide.data(), required);
    return wide;
}

std::string timestampForCrashFile() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
#if defined(_WIN32)
    localtime_s(&localTime, &time);
#else
    localTime = *std::localtime(&time);
#endif

    std::ostringstream stream;
    stream << std::put_time(&localTime, "%Y%m%d-%H%M%S");
    return stream.str();
}

std::string timestampForCrashReport() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
#if defined(_WIN32)
    localtime_s(&localTime, &time);
#else
    localTime = *std::localtime(&time);
#endif

    std::ostringstream stream;
    stream << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");
    return stream.str();
}

std::filesystem::path crashReportDirectory() {
    std::filesystem::path base = gLogPath.empty() ? std::filesystem::current_path() : gLogPath.parent_path();
    if (base.empty()) {
        base = std::filesystem::current_path();
    }

    std::filesystem::path directory = base / "crash-reports";
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (!error) {
        return directory;
    }
    return base;
}

std::filesystem::path crashArtifactPath(const char* extension) {
    std::ostringstream filename;
    filename << "quader-fatal-" << timestampForCrashFile() << "-p" << currentProcessId() << "-t" << currentThreadId()
             << extension;
    return crashReportDirectory() / filename.str();
}

void appendTextFile(const std::filesystem::path& path, const std::string& text) {
    if (path.empty()) {
        return;
    }

    std::ofstream file(path, std::ios::out | std::ios::app | std::ios::binary);
    if (file.is_open()) {
        file << text;
        file.flush();
    }
}

void writeTextFile(const std::filesystem::path& path, const std::string& text) {
    if (path.empty()) {
        return;
    }

    std::ofstream file(path, std::ios::out | std::ios::trunc | std::ios::binary);
    if (file.is_open()) {
        file << text;
        file.flush();
    }
}

void showFatalMessageBox(FatalErrorInfo info, const std::string& detail, const CrashArtifacts& artifacts) {
    std::string message =
            "Quader hit a fatal internal error and must close.\n\n"
            "Error code:\n" +
            std::string{info.code} +
            "\n\nQuick message:\n" +
            info.quickMessage +
            "\n\nDetails:\n" +
            detail +
            "\n\nCrash report:\n" +
            artifacts.reportPath.string() +
            "\n\nMinidump:\n" +
            (artifacts.dumpPath.empty() ? artifacts.dumpStatus : artifacts.dumpPath.string()) +
            "\n\nLog file:\n" +
            gLogPath.string();
    MessageBoxW(nullptr,
            widen(message).c_str(),
            L"Quader Fatal Error",
            MB_OK | MB_ICONERROR | MB_TASKMODAL);
}

std::string hexValue(std::uintptr_t value) {
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex << value;
    return stream.str();
}

#if defined(_WIN32)
std::string moduleDescriptionForAddress(std::uintptr_t address) {
    HMODULE module = nullptr;
    if (GetModuleHandleExA(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCSTR>(address),
                &module) == FALSE ||
            module == nullptr) {
        return {};
    }

    std::array<char, MAX_PATH> path{};
    const DWORD length = GetModuleFileNameA(module, path.data(), static_cast<DWORD>(path.size()));
    const std::filesystem::path modulePath =
            length > 0u ? std::filesystem::path(std::string(path.data(), length)) : std::filesystem::path("<module>");
    const auto base = reinterpret_cast<std::uintptr_t>(module);

    std::ostringstream stream;
    stream << "module=" << modulePath.filename().string() << " rva=" << hexValue(address - base);
    return stream.str();
}

void initializeSymbolsLocked() {
    if (gSymbolsInitialized) {
        return;
    }

    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    gSymbolsInitialized = SymInitialize(GetCurrentProcess(), nullptr, TRUE) == TRUE;
}

std::string describeAddressLocked(std::uintptr_t address) {
    std::ostringstream stream;
    stream << hexValue(address);
    if (std::string module = moduleDescriptionForAddress(address); !module.empty()) {
        stream << " [" << module << "]";
    }

    initializeSymbolsLocked();
    if (!gSymbolsInitialized) {
        return stream.str();
    }

    std::array<char, sizeof(SYMBOL_INFO) + MAX_SYM_NAME> symbolBuffer{};
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer.data());
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = MAX_SYM_NAME;

    DWORD64 displacement = 0;
    if (SymFromAddr(GetCurrentProcess(), static_cast<DWORD64>(address), &displacement, symbol) == TRUE) {
        stream << " " << symbol->Name;
        if (displacement != 0u) {
            stream << "+" << hexValue(static_cast<std::uintptr_t>(displacement));
        }
    }

    IMAGEHLP_LINE64 line{};
    line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
    DWORD lineDisplacement = 0;
    if (SymGetLineFromAddr64(GetCurrentProcess(), static_cast<DWORD64>(address), &lineDisplacement, &line) == TRUE) {
        stream << " (" << line.FileName << ":" << line.LineNumber << ")";
    }

    return stream.str();
}

std::string formatStackFrameLocked(std::size_t index, std::uintptr_t address) {
    std::ostringstream stream;
    stream << "  #" << std::setw(2) << std::setfill('0') << index << " " << describeAddressLocked(address);
    return stream.str();
}

std::string captureCurrentStackTrace(std::uint32_t skipFrames = 0) {
    constexpr USHORT kMaxFrames = 64;
    std::array<void*, kMaxFrames> frames{};
    const USHORT frameCount = CaptureStackBackTrace(skipFrames + 1u, kMaxFrames, frames.data(), nullptr);
    if (frameCount == 0u) {
        return "Stack trace unavailable: CaptureStackBackTrace returned no frames.";
    }

    std::lock_guard<std::mutex> lock(gSymbolMutex);
    std::ostringstream stream;
    stream << "Stack trace (most recent call first):";
    for (USHORT index = 0; index < frameCount; ++index) {
        stream << "\n" << formatStackFrameLocked(index, reinterpret_cast<std::uintptr_t>(frames[index]));
    }
    return stream.str();
}

std::string describeExceptionRecord(FatalExceptionPointers* exceptionInfo) {
    if (exceptionInfo == nullptr || exceptionInfo->ExceptionRecord == nullptr) {
        return "Exception record: unavailable.";
    }

    const EXCEPTION_RECORD* record = exceptionInfo->ExceptionRecord;
    std::ostringstream stream;
    stream << "Exception record:\n";
    stream << "  Code: " << hexValue(static_cast<std::uintptr_t>(record->ExceptionCode)) << "\n";
    stream << "  Flags: " << hexValue(static_cast<std::uintptr_t>(record->ExceptionFlags)) << "\n";
    stream << "  Address: ";
    {
        std::lock_guard<std::mutex> lock(gSymbolMutex);
        stream << describeAddressLocked(reinterpret_cast<std::uintptr_t>(record->ExceptionAddress));
    }
    if (record->NumberParameters > 0u) {
        stream << "\n  Parameters:";
        for (DWORD index = 0; index < record->NumberParameters; ++index) {
            stream << "\n    [" << index << "] " << hexValue(static_cast<std::uintptr_t>(record->ExceptionInformation[index]));
        }
    }
    return stream.str();
}

std::string windowsErrorStatus(const char* action, DWORD error) {
    std::ostringstream stream;
    stream << action << " failed with Win32 error " << error << " (" << hexValue(static_cast<std::uintptr_t>(error))
           << ").";
    return stream.str();
}

std::string writeMiniDump(const std::filesystem::path& dumpPath, FatalExceptionPointers* exceptionInfo) {
    if (dumpPath.empty()) {
        return "Minidump skipped: no dump path.";
    }

    HANDLE file = CreateFileW(dumpPath.wstring().c_str(),
            GENERIC_WRITE,
            0,
            nullptr,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return windowsErrorStatus("CreateFileW for minidump", GetLastError());
    }

    MINIDUMP_EXCEPTION_INFORMATION exceptionParameters{};
    MINIDUMP_EXCEPTION_INFORMATION* exceptionParameterPtr = nullptr;
    if (exceptionInfo != nullptr) {
        exceptionParameters.ThreadId = GetCurrentThreadId();
        exceptionParameters.ExceptionPointers = exceptionInfo;
        exceptionParameters.ClientPointers = FALSE;
        exceptionParameterPtr = &exceptionParameters;
    }

    const auto dumpType = static_cast<MINIDUMP_TYPE>(
            MiniDumpNormal |
            MiniDumpWithHandleData |
            MiniDumpWithUnloadedModules |
            MiniDumpWithProcessThreadData |
            MiniDumpWithThreadInfo |
            MiniDumpWithFullMemoryInfo |
            MiniDumpWithIndirectlyReferencedMemory);

    const BOOL wroteDump = MiniDumpWriteDump(GetCurrentProcess(),
            GetCurrentProcessId(),
            file,
            dumpType,
            exceptionParameterPtr,
            nullptr,
            nullptr);
    const DWORD error = wroteDump == TRUE ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);
    if (wroteDump != TRUE) {
        return windowsErrorStatus("MiniDumpWriteDump", error);
    }
    return "Minidump written.";
}

std::string captureExceptionStackTrace(FatalExceptionPointers* exceptionInfo) {
    if (exceptionInfo == nullptr || exceptionInfo->ContextRecord == nullptr) {
        return captureCurrentStackTrace();
    }

    CONTEXT context = *exceptionInfo->ContextRecord;
    STACKFRAME64 frame{};
    DWORD machineType = 0;

#if defined(_M_X64)
    machineType = IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset = context.Rip;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrStack.Offset = context.Rsp;
#elif defined(_M_IX86)
    machineType = IMAGE_FILE_MACHINE_I386;
    frame.AddrPC.Offset = context.Eip;
    frame.AddrFrame.Offset = context.Ebp;
    frame.AddrStack.Offset = context.Esp;
#elif defined(_M_ARM64)
    machineType = IMAGE_FILE_MACHINE_ARM64;
    frame.AddrPC.Offset = context.Pc;
    frame.AddrFrame.Offset = context.Fp;
    frame.AddrStack.Offset = context.Sp;
#else
    return captureCurrentStackTrace();
#endif

    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Mode = AddrModeFlat;

    std::lock_guard<std::mutex> lock(gSymbolMutex);
    initializeSymbolsLocked();

    constexpr std::size_t kMaxFrames = 64;
    std::ostringstream stream;
    stream << "Stack trace (fault context):";
    std::size_t frameIndex = 0;
    while (frameIndex < kMaxFrames &&
            StackWalk64(machineType,
                    GetCurrentProcess(),
                    GetCurrentThread(),
                    &frame,
                    &context,
                    nullptr,
                    SymFunctionTableAccess64,
                    SymGetModuleBase64,
                    nullptr) == TRUE) {
        if (frame.AddrPC.Offset == 0u) {
            break;
        }
        stream << "\n" << formatStackFrameLocked(frameIndex, static_cast<std::uintptr_t>(frame.AddrPC.Offset));
        ++frameIndex;
    }

    if (frameIndex == 0u) {
        return "Stack trace unavailable: StackWalk64 returned no frames.";
    }
    return stream.str();
}
#else
void showFatalMessageBox(FatalErrorInfo, const std::string&, const CrashArtifacts&) {}
std::string captureCurrentStackTrace(std::uint32_t = 0) {
    return "Stack trace unavailable on this platform.";
}

std::string captureExceptionStackTrace(FatalExceptionPointers*) {
    return captureCurrentStackTrace();
}

std::string describeExceptionRecord(FatalExceptionPointers*) {
    return "Exception record: unavailable on this platform.";
}

std::string writeMiniDump(const std::filesystem::path&, FatalExceptionPointers*) {
    return "Minidump unavailable on this platform.";
}
#endif

CrashArtifacts writeCrashArtifacts(
        FatalErrorInfo info,
        const std::string& detail,
        const std::string& stackTrace,
        FatalExceptionPointers* exceptionInfo) {
    CrashArtifacts artifacts;
    artifacts.reportPath = crashArtifactPath(".txt");
#if defined(_WIN32)
    artifacts.dumpPath = crashArtifactPath(".dmp");
    artifacts.dumpStatus = writeMiniDump(artifacts.dumpPath, exceptionInfo);
#else
    artifacts.dumpStatus = writeMiniDump({}, exceptionInfo);
#endif

    std::ostringstream report;
    report << "Quader fatal error report\n";
    report << "Generated: " << timestampForCrashReport() << "\n";
    report << "Process id: " << currentProcessId() << "\n";
    report << "Thread id: " << currentThreadId() << "\n";
    report << "Error code: " << info.code << "\n";
    report << "Quick message: " << info.quickMessage << "\n";
    report << "Detail: " << detail << "\n";
    report << "Log file: " << gLogPath.string() << "\n";
    report << "Report file: " << artifacts.reportPath.string() << "\n";
    report << "Dump file: " << (artifacts.dumpPath.empty() ? std::string{"<none>"} : artifacts.dumpPath.string())
           << "\n";
    report << "Dump status: " << artifacts.dumpStatus << "\n\n";
    report << describeExceptionRecord(exceptionInfo) << "\n\n";
    report << stackTrace << "\n";

    const std::string text = report.str();
    writeTextFile(artifacts.reportPath, text);
    appendTextFile(gLogPath, "\n" + text);
    return artifacts;
}

[[noreturn]] void handleFatalError(
        FatalErrorCode code,
        const std::string& detail,
        int exitCode,
        std::string stackTrace = {},
        FatalExceptionPointers* exceptionInfo = nullptr) {
    const FatalErrorInfo info = fatalErrorInfo(code);
    const bool alreadyHandling = gHandlingFatalError.exchange(true);
    if (!alreadyHandling) {
        if (stackTrace.empty()) {
            stackTrace = captureCurrentStackTrace(1u);
        }
        const CrashArtifacts artifacts = writeCrashArtifacts(info, detail, stackTrace, exceptionInfo);
        LOG_CRITICAL("Fatal error {}: {}\nDetail: {}\nLog file: {}\nCrash report: {}\nMinidump: {}\nDump status: {}\n{}",
                info.code,
                info.quickMessage,
                detail,
                gLogPath.string(),
                artifacts.reportPath.string(),
                artifacts.dumpPath.string(),
                artifacts.dumpStatus,
                stackTrace);
        if (auto logger = Logger::instance()) {
            logger->flush();
        }
        showFatalMessageBox(info, detail, artifacts);
    }
    std::_Exit(exitCode);
}

[[noreturn]] void terminateHandler() {
    if (std::exception_ptr current = std::current_exception()) {
        try {
            std::rethrow_exception(current);
        } catch (const std::exception& error) {
            handleFatalError(
                    FatalErrorCode::UnhandledCppException,
                    std::string{"Unhandled C++ exception: "} + error.what(),
                    1);
        } catch (...) {
            handleFatalError(FatalErrorCode::UnhandledNonStandardException, "Unhandled non-standard C++ exception.", 1);
        }
    }
    handleFatalError(
            FatalErrorCode::UnknownFatal,
            "std::terminate was called without an active exception.",
            1);
}

void abortSignalHandler(int signalNumber) {
    (void)signalNumber;
    handleFatalError(
            FatalErrorCode::RuntimeAbort,
            "The runtime requested abort(). This means an internal invariant failed in Quader or a rendering dependency.",
            134);
}

#if defined(_WIN32)
LONG WINAPI windowsUnhandledExceptionFilter(EXCEPTION_POINTERS* exceptionInfo) {
    std::string reason = "Windows reported an unhandled fatal exception.";
    if (exceptionInfo != nullptr && exceptionInfo->ExceptionRecord != nullptr) {
        const EXCEPTION_RECORD* record = exceptionInfo->ExceptionRecord;
        reason += " Code: ";
        reason += hexValue(static_cast<std::uintptr_t>(record->ExceptionCode));
        reason += " Address: ";
        reason += hexValue(reinterpret_cast<std::uintptr_t>(record->ExceptionAddress));
    }
    handleFatalError(
            FatalErrorCode::WindowsUnhandledException,
            reason,
            1,
            captureExceptionStackTrace(exceptionInfo),
            exceptionInfo);
}
#endif

#if defined(_MSC_VER)
void invalidParameterHandler(
        const wchar_t* expression,
        const wchar_t* function,
        const wchar_t* file,
        unsigned int line,
        uintptr_t) {
    std::string reason = "The runtime detected an invalid parameter.";
    if (function != nullptr) {
        reason += " Function: ";
        reason += std::filesystem::path(function).string();
    }
    if (file != nullptr) {
        reason += " File: ";
        reason += std::filesystem::path(file).string();
    }
    if (line != 0u) {
        reason += " Line: ";
        reason += std::to_string(line);
    }
    if (expression != nullptr) {
        reason += " Expression: ";
        reason += std::filesystem::path(expression).string();
    }
    handleFatalError(FatalErrorCode::InvalidRuntimeParameter, reason, 1);
}
#endif

} // namespace

FatalErrorInfo fatalErrorInfo(FatalErrorCode code) {
    switch (code) {
    case FatalErrorCode::RuntimeAbort:
        return {"QDR-FATAL-0001", "Internal runtime abort."};
    case FatalErrorCode::UnhandledCppException:
        return {"QDR-FATAL-0002", "Unhandled application exception."};
    case FatalErrorCode::UnhandledNonStandardException:
        return {"QDR-FATAL-0003", "Unhandled non-standard application exception."};
    case FatalErrorCode::InvalidRuntimeParameter:
        return {"QDR-FATAL-0004", "Invalid runtime parameter."};
    case FatalErrorCode::WindowsUnhandledException:
        return {"QDR-FATAL-0005", "Unhandled Windows fatal exception."};
    case FatalErrorCode::ManualFatalReport:
        return {"QDR-FATAL-0006", "Application requested fatal shutdown."};
    case FatalErrorCode::UnknownFatal:
    default:
        return {"QDR-FATAL-0000", "Unexpected fatal error."};
    }
}

void installFatalErrorHandler(std::filesystem::path logPath) {
    gLogPath = std::move(logPath);
    std::set_terminate(terminateHandler);
    std::signal(SIGABRT, abortSignalHandler);
#if defined(_WIN32)
    SetUnhandledExceptionFilter(windowsUnhandledExceptionFilter);
#endif
#if defined(_MSC_VER)
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _set_invalid_parameter_handler(invalidParameterHandler);
#if defined(_DEBUG)
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_DEBUG);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_DEBUG);
#endif
#endif
}

[[noreturn]] void reportFatalErrorAndExit(FatalErrorCode code, const char* detail, int exitCode) {
    handleFatalError(code, detail != nullptr ? std::string{detail} : std::string{fatalErrorInfo(code).quickMessage}, exitCode);
}

[[noreturn]] void reportFatalErrorAndExit(const char* reason, int exitCode) {
    reportFatalErrorAndExit(FatalErrorCode::ManualFatalReport, reason, exitCode);
}

} // namespace quader
