////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "platform/fatal_error_handler.hpp"

#include "core/logger.hpp"

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

std::filesystem::path g_log_path;
std::atomic_bool g_handling_fatal_error{false};

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
std::mutex g_symbol_mutex;
bool g_symbols_initialized = false;

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

std::wstring widen(const std::string &value) {
  if (value.empty()) {
    return {};
  }
  const int required =
      MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
  if (required <= 0) {
    return std::wstring(value.begin(), value.end());
  }
  std::wstring wide(static_cast<std::size_t>(required - 1), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, wide.data(), required);
  return wide;
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

std::filesystem::path crash_artifact_path(const char *extension) {
  std::ostringstream filename;
  filename << "quader-fatal-" << timestamp_for_crash_file() << "-p"
           << current_process_id() << "-t" << current_thread_id() << extension;
  return crash_report_directory() / filename.str();
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

void write_text_file(const std::filesystem::path &path,
                     const std::string &text) {
  if (path.empty()) {
    return;
  }

  std::ofstream file(path, std::ios::out | std::ios::trunc | std::ios::binary);
  if (file.is_open()) {
    file << text;
    file.flush();
  }
}

void show_fatal_message_box(FatalErrorInfo info, const std::string &detail,
                            const CrashArtifacts &artifacts) {
  std::string message =
      "Quader hit a fatal internal error and must close.\n\n"
      "Error code:\n" +
      std::string{info.code} + "\n\nQuick message:\n" + info.quick_message +
      "\n\nDetails:\n" + detail + "\n\nCrash report:\n" +
      artifacts.report_path.string() + "\n\nMinidump:\n" +
      (artifacts.dump_path.empty() ? artifacts.dump_status
                                   : artifacts.dump_path.string()) +
      "\n\nLog file:\n" + g_log_path.string();
  MessageBoxW(nullptr, widen(message).c_str(), L"Quader Fatal Error",
              MB_OK | MB_ICONERROR | MB_TASKMODAL);
}

std::string hex_value(std::uintptr_t value) {
  std::ostringstream stream;
  stream << "0x" << std::uppercase << std::hex << value;
  return stream.str();
}

#if defined(_WIN32)
std::string module_description_for_address(std::uintptr_t address) {
  HMODULE module = nullptr;
  if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                             GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCSTR>(address), &module) == FALSE ||
      module == nullptr) {
    return {};
  }

  std::array<char, MAX_PATH> path{};
  const DWORD length =
      GetModuleFileNameA(module, path.data(), static_cast<DWORD>(path.size()));
  const std::filesystem::path module_path =
      length > 0u ? std::filesystem::path(std::string(path.data(), length))
                  : std::filesystem::path("<module>");
  const auto base = reinterpret_cast<std::uintptr_t>(module);

  std::ostringstream stream;
  stream << "module=" << module_path.filename().string()
         << " rva=" << hex_value(address - base);
  return stream.str();
}

void initialize_symbols_locked() {
  if (g_symbols_initialized) {
    return;
  }

  SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
  g_symbols_initialized =
      SymInitialize(GetCurrentProcess(), nullptr, TRUE) == TRUE;
}

std::string describe_address_locked(std::uintptr_t address) {
  std::ostringstream stream;
  stream << hex_value(address);
  if (std::string module = module_description_for_address(address);
      !module.empty()) {
    stream << " [" << module << "]";
  }

  initialize_symbols_locked();
  if (!g_symbols_initialized) {
    return stream.str();
  }

  std::array<char, sizeof(SYMBOL_INFO) + MAX_SYM_NAME> symbol_buffer{};
  auto *symbol = reinterpret_cast<SYMBOL_INFO *>(symbol_buffer.data());
  symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
  symbol->MaxNameLen = MAX_SYM_NAME;

  DWORD64 displacement = 0;
  if (SymFromAddr(GetCurrentProcess(), static_cast<DWORD64>(address),
                  &displacement, symbol) == TRUE) {
    stream << " " << symbol->Name;
    if (displacement != 0u) {
      stream << "+" << hex_value(static_cast<std::uintptr_t>(displacement));
    }
  }

  IMAGEHLP_LINE64 line{};
  line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
  DWORD line_displacement = 0;
  if (SymGetLineFromAddr64(GetCurrentProcess(), static_cast<DWORD64>(address),
                           &line_displacement, &line) == TRUE) {
    stream << " (" << line.FileName << ":" << line.LineNumber << ")";
  }

  return stream.str();
}

std::string format_stack_frame_locked(std::size_t index,
                                      std::uintptr_t address) {
  std::ostringstream stream;
  stream << "  #" << std::setw(2) << std::setfill('0') << index << " "
         << describe_address_locked(address);
  return stream.str();
}

std::string capture_current_stack_trace(std::uint32_t skip_frames = 0) {
  constexpr USHORT kMaxFrames = 64;
  std::array<void *, kMaxFrames> frames{};
  const USHORT frame_count = CaptureStackBackTrace(skip_frames + 1u, kMaxFrames,
                                                   frames.data(), nullptr);
  if (frame_count == 0u) {
    return "Stack trace unavailable: CaptureStackBackTrace returned no frames.";
  }

  std::lock_guard<std::mutex> lock(g_symbol_mutex);
  std::ostringstream stream;
  stream << "Stack trace (most recent call first):";
  for (USHORT index = 0; index < frame_count; ++index) {
    stream << "\n"
           << format_stack_frame_locked(
                  index, reinterpret_cast<std::uintptr_t>(frames[index]));
  }
  return stream.str();
}

std::string describe_exception_record(FatalExceptionPointers *exception_pointers) {
  if (exception_pointers == nullptr ||
      exception_pointers->ExceptionRecord == nullptr) {
    return "Exception record: unavailable.";
  }

  const EXCEPTION_RECORD *record = exception_pointers->ExceptionRecord;
  std::ostringstream stream;
  stream << "Exception record:\n";
  stream << "  Code: "
         << hex_value(static_cast<std::uintptr_t>(record->ExceptionCode))
         << "\n";
  stream << "  Flags: "
         << hex_value(static_cast<std::uintptr_t>(record->ExceptionFlags))
         << "\n";
  stream << "  Address: ";
  {
    std::lock_guard<std::mutex> lock(g_symbol_mutex);
    stream << describe_address_locked(
        reinterpret_cast<std::uintptr_t>(record->ExceptionAddress));
  }
  if (record->NumberParameters > 0u) {
    stream << "\n  Parameters:";
    for (DWORD index = 0; index < record->NumberParameters; ++index) {
      stream << "\n    [" << index << "] "
             << hex_value(static_cast<std::uintptr_t>(
                    record->ExceptionInformation[index]));
    }
  }
  return stream.str();
}

std::string windows_error_status(const char *action, DWORD error) {
  std::ostringstream stream;
  stream << action << " failed with Win32 error " << error << " ("
         << hex_value(static_cast<std::uintptr_t>(error)) << ").";
  return stream.str();
}

std::string write_mini_dump(const std::filesystem::path &dump_path,
                            FatalExceptionPointers *exception_pointers) {
  if (dump_path.empty()) {
    return "Minidump skipped: no dump path.";
  }

  HANDLE file =
      CreateFileW(dump_path.wstring().c_str(), GENERIC_WRITE, 0, nullptr,
                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return windows_error_status("CreateFileW for minidump", GetLastError());
  }

  MINIDUMP_EXCEPTION_INFORMATION exception_parameters{};
  MINIDUMP_EXCEPTION_INFORMATION *exception_parameter_ptr = nullptr;
  if (exception_pointers != nullptr) {
    exception_parameters.ThreadId = GetCurrentThreadId();
    exception_parameters.ExceptionPointers = exception_pointers;
    exception_parameters.ClientPointers = FALSE;
    exception_parameter_ptr = &exception_parameters;
  }

  const auto dump_type = static_cast<MINIDUMP_TYPE>(
      MiniDumpNormal | MiniDumpWithHandleData | MiniDumpWithUnloadedModules |
      MiniDumpWithProcessThreadData | MiniDumpWithThreadInfo |
      MiniDumpWithFullMemoryInfo | MiniDumpWithIndirectlyReferencedMemory);

  const BOOL wrote_dump =
      MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
                        dump_type, exception_parameter_ptr, nullptr, nullptr);
  const DWORD error = wrote_dump == TRUE ? ERROR_SUCCESS : GetLastError();
  CloseHandle(file);
  if (wrote_dump != TRUE) {
    return windows_error_status("MiniDumpWriteDump", error);
  }
  return "Minidump written.";
}

std::string
capture_exception_stack_trace(FatalExceptionPointers *exception_pointers) {
  if (exception_pointers == nullptr ||
      exception_pointers->ContextRecord == nullptr) {
    return capture_current_stack_trace();
  }

  CONTEXT context = *exception_pointers->ContextRecord;
  STACKFRAME64 frame{};
  DWORD machine_type = 0;

#if defined(_M_X64)
  machine_type = IMAGE_FILE_MACHINE_AMD64;
  frame.AddrPC.Offset = context.Rip;
  frame.AddrFrame.Offset = context.Rbp;
  frame.AddrStack.Offset = context.Rsp;
#elif defined(_M_IX86)
  machine_type = IMAGE_FILE_MACHINE_I386;
  frame.AddrPC.Offset = context.Eip;
  frame.AddrFrame.Offset = context.Ebp;
  frame.AddrStack.Offset = context.Esp;
#elif defined(_M_ARM64)
  machine_type = IMAGE_FILE_MACHINE_ARM64;
  frame.AddrPC.Offset = context.Pc;
  frame.AddrFrame.Offset = context.Fp;
  frame.AddrStack.Offset = context.Sp;
#else
  return capture_current_stack_trace();
#endif

  frame.AddrPC.Mode = AddrModeFlat;
  frame.AddrFrame.Mode = AddrModeFlat;
  frame.AddrStack.Mode = AddrModeFlat;

  std::lock_guard<std::mutex> lock(g_symbol_mutex);
  initialize_symbols_locked();

  constexpr std::size_t kMaxFrames = 64;
  std::ostringstream stream;
  stream << "Stack trace (fault context):";
  std::size_t frame_index = 0;
  while (frame_index < kMaxFrames &&
         StackWalk64(machine_type, GetCurrentProcess(), GetCurrentThread(),
                     &frame, &context, nullptr, SymFunctionTableAccess64,
                     SymGetModuleBase64, nullptr) == TRUE) {
    if (frame.AddrPC.Offset == 0u) {
      break;
    }
    stream << "\n"
           << format_stack_frame_locked(
                  frame_index,
                  static_cast<std::uintptr_t>(frame.AddrPC.Offset));
    ++frame_index;
  }

  if (frame_index == 0u) {
    return "Stack trace unavailable: StackWalk64 returned no frames.";
  }
  return stream.str();
}
#else
void show_fatal_message_box(FatalErrorInfo, const std::string &,
                            const CrashArtifacts &) {}
std::string capture_current_stack_trace(std::uint32_t = 0) {
  return "Stack trace unavailable on this platform.";
}

std::string capture_exception_stack_trace(FatalExceptionPointers *) {
  return capture_current_stack_trace();
}

std::string describe_exception_record(FatalExceptionPointers *) {
  return "Exception record: unavailable on this platform.";
}

std::string write_mini_dump(const std::filesystem::path &,
                            FatalExceptionPointers *) {
  return "Minidump unavailable on this platform.";
}
#endif

CrashArtifacts write_crash_artifacts(FatalErrorInfo info,
                                     const std::string &detail,
                                     const std::string &stack_trace,
                                     FatalExceptionPointers *exception_pointers) {
  CrashArtifacts artifacts;
  artifacts.report_path = crash_artifact_path(".txt");
#if defined(_WIN32)
  artifacts.dump_path = crash_artifact_path(".dmp");
  artifacts.dump_status =
      write_mini_dump(artifacts.dump_path, exception_pointers);
#else
  artifacts.dump_status = write_mini_dump({}, exception_pointers);
#endif

  std::ostringstream report;
  report << "Quader fatal error report\n";
  report << "Generated: " << timestamp_for_crash_report() << "\n";
  report << "Process id: " << current_process_id() << "\n";
  report << "Thread id: " << current_thread_id() << "\n";
  report << "Error code: " << info.code << "\n";
  report << "Quick message: " << info.quick_message << "\n";
  report << "Detail: " << detail << "\n";
  report << "Log file: " << g_log_path.string() << "\n";
  report << "Report file: " << artifacts.report_path.string() << "\n";
  report << "Dump file: "
         << (artifacts.dump_path.empty() ? std::string{"<none>"}
                                         : artifacts.dump_path.string())
         << "\n";
  report << "Dump status: " << artifacts.dump_status << "\n\n";
  report << describe_exception_record(exception_pointers) << "\n\n";
  report << stack_trace << "\n";

  const std::string text = report.str();
  write_text_file(artifacts.report_path, text);
  append_text_file(g_log_path, "\n" + text);
  return artifacts;
}

[[noreturn]] void
handle_fatal_error(FatalErrorCode code, const std::string &detail,
                   int exit_code, std::string stack_trace = {},
                   FatalExceptionPointers *exception_pointers = nullptr) {
  const FatalErrorInfo info = fatal_error_info(code);
  const bool already_handling = g_handling_fatal_error.exchange(true);
  if (!already_handling) {
    if (stack_trace.empty()) {
      stack_trace = capture_current_stack_trace(1u);
    }
    const CrashArtifacts artifacts =
        write_crash_artifacts(info, detail, stack_trace, exception_pointers);
    LOG_CRITICAL("Fatal error {}: {}\nDetail: {}\nLog file: {}\nCrash report: "
                 "{}\nMinidump: {}\nDump status: {}\n{}",
                 info.code, info.quick_message, detail, g_log_path.string(),
                 artifacts.report_path.string(), artifacts.dump_path.string(),
                 artifacts.dump_status, stack_trace);
    if (auto logger = Logger::instance()) {
      logger->flush();
    }
    show_fatal_message_box(info, detail, artifacts);
  }
  std::_Exit(exit_code);
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
                     capture_exception_stack_trace(exception_pointers),
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
  case FatalErrorCode::UnknownFatal:
  default:
    return {"QDR-FATAL-0000", "Unexpected fatal error."};
  }
}

void install_fatal_error_handler(std::filesystem::path log_path) {
  g_log_path = std::move(log_path);
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
