////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "platform/fatal_error_handler.hpp"
#include "quader_app_version.hpp"

#include <QByteArray>
#include <QString>
#include <QtCore/qlogging.h>

#include <algorithm>
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
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

#if defined(_MSC_VER)
#include <crtdbg.h>
#endif

namespace quader {
namespace {

std::filesystem::path g_log_path;
FatalDiagnosticsOptions g_diagnostics;
std::atomic_bool g_handling_fatal_error{false};
std::atomic<QtMessageHandler> previous_qt_message_handler{nullptr};
std::mutex qt_message_handler_install_mutex;

/**
 * Describes the crash artifacts written by the fatal error boundary.
 */
struct CrashArtifacts {
  std::filesystem::path report_path;
  std::filesystem::path dump_path;
  std::string dump_status;
};

#if defined(_WIN32)
using FatalExceptionPointers = EXCEPTION_POINTERS;
#else
using FatalExceptionPointers = void;
#endif

#if defined(_WIN32)
std::uint32_t current_process_id() {
  return static_cast<std::uint32_t>(GetCurrentProcessId());
}

std::uint32_t current_thread_id() {
  return static_cast<std::uint32_t>(GetCurrentThreadId());
}
#else
std::uint32_t current_process_id() {
  return 0;
}

std::uint32_t current_thread_id() {
  return 0;
}
#endif

std::wstring utf8_to_wide_fallback(std::string_view value) {
  return std::wstring(value.begin(), value.end());
}

} // namespace

std::wstring platform_utf8_to_wide(std::string_view value) {
  if (value.empty()) {
    return {};
  }
#if defined(_WIN32)
  if (value.size() > static_cast<std::size_t>(
                         (std::numeric_limits<int>::max)())) {
    return utf8_to_wide_fallback(value);
  }
  const int source_size = static_cast<int>(value.size());
  const int required =
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                          source_size, nullptr, 0);
  if (required <= 0) {
    return utf8_to_wide_fallback(value);
  }
  std::wstring wide(static_cast<std::size_t>(required), L'\0');
  const int converted = MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), source_size, wide.data(),
      required);
  if (converted != required) {
    return utf8_to_wide_fallback(value);
  }
  return wide;
#else
  return utf8_to_wide_fallback(value);
#endif
}

FatalArtifactPaths make_fatal_artifact_paths(
    const std::filesystem::path &directory, std::string_view timestamp,
    std::uint32_t process_id, std::uint32_t thread_id, bool include_dump) {
  std::ostringstream filename;
  filename << "quader-fatal-" << timestamp << "-p" << process_id << "-t"
           << thread_id;
  const std::string stem = filename.str();
  return {
      .report_path = directory / (stem + ".txt"),
      .dump_path = include_dump ? directory / (stem + ".dmp")
                               : std::filesystem::path{},
  };
}

std::string format_fatal_error_report(const FatalReportData &data) {
  std::ostringstream report;
  report << "Quader fatal error report\n";
  report << "Generated: " << data.generated_at << "\n";
  report << "Process id: " << data.process_id << "\n";
  report << "Thread id: " << data.thread_id << "\n";
  report << "App version: " << data.app_version << "\n";
  report << "Executable: " << data.executable_path.string() << "\n";
  report << "Error code: " << data.error_code << "\n";
  report << "Quick message: " << data.quick_message << "\n";
  report << "Detail: " << data.detail << "\n";
  report << "Log file: " << data.log_path.string() << "\n";
  report << "Report file: " << data.report_path.string() << "\n";
  report << "Dump file: "
         << (data.dump_path.empty() ? std::string{"<none>"}
                                    : data.dump_path.string())
         << "\n";
  report << "Dump status: " << data.dump_status << "\n\n";
  report << data.exception_record << "\n\n";
  report << data.stack_trace << "\n";
  return report.str();
}

FatalReportWriteResult write_fatal_error_report(
    const std::filesystem::path &preferred_path,
    const std::filesystem::path &fallback_path, std::string_view text) {
  const auto try_write = [text](const std::filesystem::path &path) {
    if (path.empty()) {
      return false;
    }
    std::error_code directory_error;
    if (!path.parent_path().empty()) {
      std::filesystem::create_directories(path.parent_path(), directory_error);
    }
    if (directory_error) {
      return false;
    }
    std::ofstream file(path, std::ios::out | std::ios::trunc |
                                 std::ios::binary);
    if (!file.is_open()) {
      return false;
    }
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    file.flush();
    return file.good();
  };

  if (try_write(preferred_path)) {
    return {.path = preferred_path, .written = true, .used_fallback = false};
  }
  if (fallback_path != preferred_path && try_write(fallback_path)) {
    return {.path = fallback_path, .written = true, .used_fallback = true};
  }
  return {.path = preferred_path, .written = false, .used_fallback = false};
}

namespace {

std::filesystem::path current_executable_path() {
#if defined(_WIN32)
  std::vector<wchar_t> buffer(32768U, L'\0');
  const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                                         static_cast<DWORD>(buffer.size()));
  if (length > 0U && length < buffer.size()) {
    return std::filesystem::path(std::wstring(buffer.data(), length));
  }
#endif
  return {};
}

std::string timestamp_for_crash_file() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t time = std::chrono::system_clock::to_time_t(now);
  std::tm local_time{};
#if defined(_WIN32)
  localtime_s(&local_time, &time);
#else
  local_time = *std::localtime(&time);
#endif

  std::ostringstream stream;
  stream << std::put_time(&local_time, "%Y%m%d-%H%M%S");
  return stream.str();
}

std::string timestamp_for_crash_report() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t time = std::chrono::system_clock::to_time_t(now);
  std::tm local_time{};
#if defined(_WIN32)
  localtime_s(&local_time, &time);
#else
  local_time = *std::localtime(&time);
#endif

  std::ostringstream stream;
  stream << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S");
  return stream.str();
}

std::filesystem::path crash_report_directory() {
  std::filesystem::path base = g_log_path.empty()
                                   ? std::filesystem::current_path()
                                   : g_log_path.parent_path();
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

std::filesystem::path crash_report_fallback_path(
    const std::filesystem::path &filename) {
  std::error_code error;
  std::filesystem::path directory = std::filesystem::temp_directory_path(error);
  if (error || directory.empty()) {
    directory = std::filesystem::current_path();
  }
  return directory / "Quader" / "crash-reports" / filename;
}

void append_text_file(const std::filesystem::path &path,
                      const std::string &text) {
  if (path.empty()) {
    return;
  }

  std::ofstream file(path, std::ios::out | std::ios::app | std::ios::binary);
  if (file.is_open()) {
    file << text;
    file.flush();
  }
}

void show_fatal_message_box(FatalErrorInfo info, const std::string &detail,
                            const CrashArtifacts &artifacts) {
  if (const char *suppress_ui =
          std::getenv("QUADER_TEST_SUPPRESS_FATAL_UI");
      suppress_ui != nullptr && std::string_view(suppress_ui) == "1") {
    return;
  }
  std::string message =
      "Quader hit a fatal internal error and must close.\n\n"
      "Error code:\n" +
      std::string{info.code} + "\n\nQuick message:\n" + info.quick_message +
      "\n\nDetails:\n" + detail + "\n\nCrash report:\n" +
      artifacts.report_path.string() + "\n\nMinidump:\n" +
      (artifacts.dump_path.empty() ? artifacts.dump_status
                                   : artifacts.dump_path.string()) +
      "\n\nLog file:\n" + g_log_path.string();
  MessageBoxW(nullptr, platform_utf8_to_wide(message).c_str(),
              L"Quader Fatal Error",
              MB_OK | MB_ICONERROR | MB_TASKMODAL);
}

std::string hex_value(std::uintptr_t value) {
  std::ostringstream stream;
  stream << "0x" << std::uppercase << std::hex << value;
  return stream.str();
}

/// Captures addresses without loading symbols or entering DbgHelp.
struct FatalStack {
  std::array<std::uintptr_t, 64> addresses{};
  std::uint32_t count = 0;
};

FatalStack capture_raw_stack() {
  FatalStack stack;
#if defined(_WIN32)
  std::array<void *, 64> frames{};
  const USHORT count = CaptureStackBackTrace(
      2U, static_cast<DWORD>(frames.size()), frames.data(), nullptr);
  stack.count = count;
  for (std::size_t index = 0; index < count; ++index) {
    stack.addresses[index] = reinterpret_cast<std::uintptr_t>(frames[index]);
  }
#endif
  return stack;
}

std::string format_raw_stack(const FatalStack &stack) {
  std::ostringstream stream;
  stream << "Stack trace (raw addresses; symbolization is optional):";
  if (stack.count == 0U) {
    stream << "\n  No stack frames available.";
  }
  for (std::uint32_t index = 0; index < stack.count; ++index) {
    stream << "\n  #" << std::setw(2) << std::setfill('0') << index
           << " " << hex_value(stack.addresses[index]);
  }
  return stream.str();
}

std::string describe_exception_record(FatalExceptionPointers *exception_pointers) {
#if defined(_WIN32)
  if (exception_pointers != nullptr && exception_pointers->ExceptionRecord != nullptr) {
    const EXCEPTION_RECORD &record = *exception_pointers->ExceptionRecord;
    std::ostringstream stream;
    stream << "Exception record:\n  Code: " << hex_value(record.ExceptionCode)
           << "\n  Flags: " << hex_value(record.ExceptionFlags)
           << "\n  Address: "
           << hex_value(reinterpret_cast<std::uintptr_t>(record.ExceptionAddress));
    const DWORD count = (std::min)(record.NumberParameters,
                                   static_cast<DWORD>(EXCEPTION_MAXIMUM_PARAMETERS));
    for (DWORD index = 0; index < count; ++index) {
      stream << "\n  Parameter[" << index << "]: "
             << hex_value(record.ExceptionInformation[index]);
    }
    return stream.str();
  }
#else
  (void)exception_pointers;
#endif
  return "Exception record: unavailable.";
}

/// Carries optional results while the initial text report remains durable.
struct FatalDiagnosticsResult {
  std::string status;
  std::string symbolized_stack;
  bool dump_written = false;
};

#if defined(_WIN32)
std::filesystem::path diagnostic_helper_path() {
  if (!g_diagnostics.helper_path.empty()) {
    return g_diagnostics.helper_path;
  }
  std::vector<wchar_t> buffer(32768U, L'\0');
  const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                                           static_cast<DWORD>(buffer.size()));
  if (length == 0U || length >= buffer.size()) {
    return {};
  }
  return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path() /
         L"quader_fatal_diagnostics.exe";
}

std::wstring quote_process_argument(std::wstring_view argument) {
  std::wstring quoted = L"\"";
  std::size_t backslashes = 0;
  for (wchar_t character : argument) {
    if (character == L'\\') {
      ++backslashes;
      continue;
    }
    quoted.append(character == L'"' ? backslashes * 2U + 1U : backslashes, L'\\');
    backslashes = 0;
    quoted.push_back(character);
  }
  quoted.append(backslashes * 2U, L'\\');
  quoted.push_back(L'"');
  return quoted;
}

std::string read_symbolized_stack(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) {
    return {};
  }
  constexpr std::size_t kMaximumStackBytes = 64U * 1024U;
  std::string text(kMaximumStackBytes, '\0');
  input.read(text.data(), static_cast<std::streamsize>(text.size()));
  text.resize(static_cast<std::size_t>(input.gcount()));
  if (input.peek() != std::char_traits<char>::eof()) {
    text += "\n[Symbolized stack truncated.]";
  }
  return text;
}
#endif

FatalDiagnosticsResult collect_optional_diagnostics(
    const std::filesystem::path &dump_path, const FatalStack &stack,
    FatalExceptionPointers *exception_pointers) {
#if defined(_WIN32)
  const auto helper = diagnostic_helper_path();
  std::error_code error;
  if (helper.empty() || !std::filesystem::is_regular_file(helper, error)) {
    return {.status = "Detailed diagnostics unavailable: helper executable is missing: " +
                       helper.string()};
  }
  std::filesystem::path symbols_path = dump_path;
  symbols_path += L".symbols";
  std::wstring command = quote_process_argument(helper.wstring()) +
      L" --quader-fatal-diagnostics " + std::to_wstring(GetCurrentProcessId()) +
      L" " + std::to_wstring(GetCurrentThreadId()) +
      L" " + std::to_wstring(reinterpret_cast<std::uintptr_t>(exception_pointers)) +
      L" " + std::to_wstring(reinterpret_cast<std::uintptr_t>(stack.addresses.data())) +
      L" " + std::to_wstring(stack.count) +
      L" " + quote_process_argument(dump_path.wstring()) +
      L" " + quote_process_argument(symbols_path.wstring());
  std::vector<wchar_t> command_buffer(command.begin(), command.end());
  command_buffer.push_back(L'\0');
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(helper.wstring().c_str(), command_buffer.data(), nullptr,
                       nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                       &startup, &process)) {
    return {.status = "Detailed diagnostics helper launch failed with Win32 error " +
                       std::to_string(GetLastError()) + "."};
  }

  FatalDiagnosticsResult result;
  const DWORD deadline = (std::clamp)(g_diagnostics.timeout_ms, 1U, 10'000U);
  const DWORD wait = WaitForSingleObject(process.hProcess, deadline);
  if (wait == WAIT_OBJECT_0) {
    DWORD exit_code = 0;
    if (GetExitCodeProcess(process.hProcess, &exit_code)) {
      result.status = exit_code == 0U
          ? "Detailed diagnostics helper completed."
          : "Detailed diagnostics helper failed with exit code " +
                std::to_string(exit_code) + ".";
    } else {
      result.status = "Detailed diagnostics helper exit status is unavailable.";
    }
  } else {
    const DWORD wait_error = wait == WAIT_FAILED ? GetLastError() : 0U;
    result.status = wait == WAIT_TIMEOUT
        ? "Detailed diagnostics timed out after " + std::to_string(deadline) +
              " ms; the initial report and raw stack were preserved."
        : "Waiting for detailed diagnostics failed with Win32 error " +
              std::to_string(wait_error) + ".";
    // The helper only observes this process. Terminating it cannot damage the
    // target or its initial report. Reap within a second, never wait indefinitely.
    if (!TerminateProcess(process.hProcess, 0xDEADU)) {
      result.status += " Helper termination failed with Win32 error " +
                       std::to_string(GetLastError()) + ".";
    } else if (WaitForSingleObject(process.hProcess, 1000U) != WAIT_OBJECT_0) {
      result.status += " Helper termination did not complete within the cleanup deadline.";
    }
  }
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);

  // The helper renames complete artifacts from .part files. A published dump
  // remains useful even when later symbol loading exceeds the deadline.
  error.clear();
  result.dump_written = std::filesystem::is_regular_file(dump_path, error);
  if (result.dump_written) {
    result.status += " Minidump written.";
  } else {
    result.status += " Minidump unavailable.";
  }
  result.symbolized_stack = read_symbolized_stack(symbols_path);
  if (result.symbolized_stack.empty()) {
    result.status += " Symbolized stack unavailable; use the recorded raw stack.";
  }
  for (const auto &path : {symbols_path, std::filesystem::path(symbols_path.wstring() + L".part"),
                           std::filesystem::path(dump_path.wstring() + L".part")}) {
    error.clear();
    std::filesystem::remove(path, error);
  }
  return result;
#else
  (void)dump_path;
  (void)stack;
  (void)exception_pointers;
  return {.status = "Detailed diagnostics unavailable on this platform."};
#endif
}

CrashArtifacts write_crash_artifacts(FatalErrorInfo info,
                                     const std::string &detail,
                                     const FatalStack &stack,
                                     FatalExceptionPointers *exception_pointers,
                                     bool captured_context) {
  CrashArtifacts artifacts;
  const FatalArtifactPaths paths = make_fatal_artifact_paths(
      crash_report_directory(), timestamp_for_crash_file(),
      current_process_id(), current_thread_id(),
#if defined(_WIN32)
      true);
#else
      false);
#endif
  artifacts.report_path = paths.report_path;
  artifacts.dump_path = paths.dump_path;
  artifacts.dump_status = "Detailed diagnostics pending; the initial report is saved first.";
  FatalReportData report_data{
      .generated_at = timestamp_for_crash_report(),
      .process_id = current_process_id(),
      .thread_id = current_thread_id(),
      .app_version = kQuaderAppVersion,
      .executable_path = current_executable_path(),
      .error_code = info.code,
      .quick_message = info.quick_message,
      .detail = detail,
      .log_path = g_log_path,
      .report_path = artifacts.report_path,
      .dump_path = artifacts.dump_path,
      .dump_status = artifacts.dump_status,
      .exception_record =
          (captured_context ? std::string{"Captured fatal-report context (synthetic exception):\n"}
                            : std::string{}) +
          describe_exception_record(exception_pointers),
      .stack_trace = format_raw_stack(stack),
  };
  std::string text = format_fatal_error_report(report_data);
  const FatalReportWriteResult write = write_fatal_error_report(
      artifacts.report_path,
      crash_report_fallback_path(artifacts.report_path.filename()), text);
  if (write.written) {
    artifacts.report_path = write.path;
    if (write.used_fallback) {
      append_text_file(write.path, "\nReport recovered to fallback path: " +
                                       write.path.string() + "\n");
      if (!artifacts.dump_path.empty()) {
        artifacts.dump_path = write.path;
        artifacts.dump_path.replace_extension(".dmp");
      }
    }
  }
  // Fatal reporting must not wait for Logger's process-wide or sink mutexes.
  // Direct append preserves the essential report even during a logging failure.
  append_text_file(g_log_path, "\n" + text);
#if defined(_WIN32)
  OutputDebugStringA(text.c_str());
#endif

  const FatalDiagnosticsResult diagnostics =
      collect_optional_diagnostics(artifacts.dump_path, stack, exception_pointers);
  artifacts.dump_status = diagnostics.status;
  if (!diagnostics.dump_written) {
    artifacts.dump_path.clear();
  }
  std::string supplement = "\nDetailed diagnostics result:\n" + diagnostics.status +
      "\nDump file: " + (artifacts.dump_path.empty() ? std::string{"<none>"}
                                                     : artifacts.dump_path.string()) +
      "\n";
  if (!diagnostics.symbolized_stack.empty()) {
    supplement += diagnostics.symbolized_stack + "\n";
  }
  append_text_file(artifacts.report_path, supplement);
  append_text_file(g_log_path, supplement);
  return artifacts;
}

[[noreturn]] void
handle_fatal_error(FatalErrorCode code, const std::string &detail,
                   int exit_code,
                   FatalExceptionPointers *exception_pointers = nullptr) {
  const FatalErrorInfo info = fatal_error_info(code);
  const bool already_handling = g_handling_fatal_error.exchange(true);
  if (!already_handling) {
    // Keep this address array and the exception context alive while the helper
    // reads the target process. No DbgHelp API executes in the failing process.
    bool captured_context = false;
#if defined(_WIN32)
    CONTEXT context{};
    EXCEPTION_RECORD record{};
    EXCEPTION_POINTERS pointers{};
    if (exception_pointers == nullptr) {
      // Manual/abort/terminate paths need a real register context and stack in
      // the dump too. Capture in this never-returning frame so it remains live.
      RtlCaptureContext(&context);
      record.ExceptionCode = 0xE0514400U | static_cast<DWORD>(code);
      record.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
#if defined(_M_X64)
      record.ExceptionAddress = reinterpret_cast<void *>(context.Rip);
#elif defined(_M_IX86)
      record.ExceptionAddress = reinterpret_cast<void *>(context.Eip);
#elif defined(_M_ARM64)
      record.ExceptionAddress = reinterpret_cast<void *>(context.Pc);
#endif
      pointers.ExceptionRecord = &record;
      pointers.ContextRecord = &context;
      exception_pointers = &pointers;
      captured_context = true;
    }
#endif
    const FatalStack stack = capture_raw_stack();
    const CrashArtifacts artifacts =
        write_crash_artifacts(info, detail, stack, exception_pointers, captured_context);
    show_fatal_message_box(info, detail, artifacts);
  }
  std::_Exit(exit_code);
}

void qt_fatal_message_handler(QtMsgType type,
                              const QMessageLogContext &context,
                              const QString &message) {
  if (type != QtFatalMsg) {
    const auto previous = previous_qt_message_handler.load(
        std::memory_order_acquire);
    if (previous != nullptr) {
      previous(type, context, message);
    }
    return;
  }

  const QByteArray utf8_message = message.toUtf8();
  std::string detail{"Qt fatal message: "};
  detail.append(utf8_message.constData(),
                static_cast<std::size_t>(utf8_message.size()));
  if (context.category != nullptr) {
    detail += "\nCategory: ";
    detail += context.category;
  }
  if (context.file != nullptr) {
    detail += "\nFile: ";
    detail += context.file;
  }
  if (context.line > 0) {
    detail += "\nLine: ";
    detail += std::to_string(context.line);
  }
  if (context.function != nullptr) {
    detail += "\nFunction: ";
    detail += context.function;
  }
  // Qt's Windows fatal termination bypasses SIGABRT and exception filters.
  // Enter our durable reporter directly, without taking any Logger locks.
  handle_fatal_error(FatalErrorCode::QtFatalMessage, detail, 1);
}

[[noreturn]] void terminate_handler() {
  if (std::exception_ptr current = std::current_exception()) {
    try {
      std::rethrow_exception(current);
    } catch (const std::exception &error) {
      handle_fatal_error(
          FatalErrorCode::UnhandledCppException,
          std::string{"Unhandled C++ exception: "} + error.what(), 1);
    } catch (...) {
      handle_fatal_error(FatalErrorCode::UnhandledNonStandardException,
                         "Unhandled non-standard C++ exception.", 1);
    }
  }
  handle_fatal_error(FatalErrorCode::UnknownFatal,
                     "std::terminate was called without an active exception.",
                     1);
}

void abort_signal_handler(int signal_number) {
  (void)signal_number;
  handle_fatal_error(FatalErrorCode::RuntimeAbort,
                     "The runtime requested abort(). This means an internal "
                     "invariant failed in Quader or a rendering dependency.",
                     134);
}

#if defined(_WIN32)
LONG WINAPI
windows_unhandled_exception_filter(EXCEPTION_POINTERS *exception_pointers) {
  std::string reason = "Windows reported an unhandled fatal exception.";
  if (exception_pointers != nullptr &&
      exception_pointers->ExceptionRecord != nullptr) {
    const EXCEPTION_RECORD *record = exception_pointers->ExceptionRecord;
    reason += " Code: ";
    reason += hex_value(static_cast<std::uintptr_t>(record->ExceptionCode));
    reason += " Address: ";
    reason +=
        hex_value(reinterpret_cast<std::uintptr_t>(record->ExceptionAddress));
  }
  handle_fatal_error(FatalErrorCode::WindowsUnhandledException, reason, 1,
                     exception_pointers);
}
#endif

#if defined(_MSC_VER)
void invalid_parameter_handler(const wchar_t *expression,
                               const wchar_t *function, const wchar_t *file,
                               unsigned int line, uintptr_t) {
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
  handle_fatal_error(FatalErrorCode::InvalidRuntimeParameter, reason, 1);
}
#endif

} // namespace

FatalErrorInfo fatal_error_info(FatalErrorCode code) {
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
  case FatalErrorCode::QtFatalMessage:
    return {"QDR-FATAL-0007", "Fatal Qt message."};
  case FatalErrorCode::UnknownFatal:
  default:
    return {"QDR-FATAL-0000", "Unexpected fatal error."};
  }
}

void install_fatal_error_handler(std::filesystem::path log_path,
                                  FatalDiagnosticsOptions diagnostics) {
  g_log_path = std::move(log_path);
  g_diagnostics = std::move(diagnostics);
  g_diagnostics.timeout_ms = (std::clamp)(g_diagnostics.timeout_ms, 1U, 10'000U);
  std::set_terminate(terminate_handler);
  std::signal(SIGABRT, abort_signal_handler);
#if defined(_WIN32)
  SetUnhandledExceptionFilter(windows_unhandled_exception_filter);
#endif
#if defined(_MSC_VER)
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
  _set_invalid_parameter_handler(invalid_parameter_handler);
#if defined(_DEBUG)
  _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_DEBUG);
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_DEBUG);
#endif
#endif
}

void install_qt_fatal_error_handler() {
  std::lock_guard lock(qt_message_handler_install_mutex);
  const auto previous = qInstallMessageHandler(qt_fatal_message_handler);
  if (previous != qt_fatal_message_handler) {
    // Qt returns its default handler when no custom handler was installed.
    // Reinstallation must retain that predecessor instead of forwarding to us.
    previous_qt_message_handler.store(previous, std::memory_order_release);
  }
}

[[noreturn]] void report_fatal_error_and_exit(FatalErrorCode code,
                                              const char *detail,
                                              int exit_code) {
  handle_fatal_error(code,
                     detail != nullptr
                         ? std::string{detail}
                         : std::string{fatal_error_info(code).quick_message},
                     exit_code);
}

[[noreturn]] void report_fatal_error_and_exit(const char *reason,
                                              int exit_code) {
  report_fatal_error_and_exit(FatalErrorCode::ManualFatalReport, reason,
                              exit_code);
}

} // namespace quader
