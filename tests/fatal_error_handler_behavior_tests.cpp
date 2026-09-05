////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "platform/fatal_error_handler.hpp"
#include "platform/platform_file_dialog.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <DbgHelp.h>
#endif

namespace {

std::vector<wchar_t> explorer_selection_buffer(
    const std::initializer_list<std::wstring_view> segments) {
  std::vector<wchar_t> buffer;
  for (const std::wstring_view segment : segments) {
    buffer.insert(buffer.end(), segment.begin(), segment.end());
    buffer.push_back(L'\0');
  }
  buffer.push_back(L'\0');
  return buffer;
}

std::string read_text_file(const std::filesystem::path &path) {
  std::ifstream stream(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(stream),
          std::istreambuf_iterator<char>()};
}

#if defined(_WIN32)
std::filesystem::path current_executable_path() {
  std::vector<wchar_t> buffer(32768, L'\0');
  const DWORD length = GetModuleFileNameW(
      nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  if (length == 0 ||
      length == static_cast<DWORD>(buffer.size())) {
    return {};
  }
  return std::filesystem::path(std::wstring(buffer.data(), length));
}

/// Restores one process-wide environment variable after a Windows test.
class ScopedWideEnvironmentVariable final {
public:
  ScopedWideEnvironmentVariable(const wchar_t *name, std::wstring value)
      : name_(name) {
    if (const wchar_t *previous = _wgetenv(name); previous != nullptr) {
      previous_ = previous;
    }
    _wputenv_s(name_.c_str(), value.c_str());
  }

  ~ScopedWideEnvironmentVariable() {
    _wputenv_s(name_.c_str(), previous_.value_or(L"").c_str());
  }

  ScopedWideEnvironmentVariable(const ScopedWideEnvironmentVariable &) =
      delete;
  ScopedWideEnvironmentVariable &
  operator=(const ScopedWideEnvironmentVariable &) = delete;

private:
  std::wstring name_;
  std::optional<std::wstring> previous_;
};

std::filesystem::path unique_test_directory(std::wstring_view label) {
  return std::filesystem::temp_directory_path() /
         (std::wstring(label) + L"-p" + std::to_wstring(GetCurrentProcessId()) +
          L"-t" + std::to_wstring(GetTickCount64()));
}
#endif

} // namespace

TEST(PlatformFileDialogTests, ParsesSingleUnicodeExplorerSelection) {
  const std::vector<wchar_t> buffer = explorer_selection_buffer(
      {L"C:\\Progetti\\trame-\u00E8\\albedo-\u03B2.png"});

  const auto selected =
      quader::platform_file_dialog_detail::
          parse_explorer_file_dialog_selection(true, buffer);

  ASSERT_TRUE(selected.has_value());
  ASSERT_EQ(selected->size(), 1U);
  EXPECT_EQ(selected->front(),
            std::filesystem::path(
                L"C:\\Progetti\\trame-\u00E8\\albedo-\u03B2.png"));
}

TEST(PlatformFileDialogTests, ParsesMultipleUnicodeExplorerSelections) {
  const std::vector<wchar_t> buffer = explorer_selection_buffer(
      {L"C:\\Progetti\\trame-\u00E8", L"albedo-\u03B2.png",
       L"normal-\u03B3.webp"});

  const auto selected =
      quader::platform_file_dialog_detail::
          parse_explorer_file_dialog_selection(true, buffer);

  ASSERT_TRUE(selected.has_value());
  ASSERT_EQ(selected->size(), 2U);
  EXPECT_EQ(selected->at(0),
            std::filesystem::path(
                L"C:\\Progetti\\trame-\u00E8\\albedo-\u03B2.png"));
  EXPECT_EQ(selected->at(1),
            std::filesystem::path(
                L"C:\\Progetti\\trame-\u00E8\\normal-\u03B3.webp"));
}

TEST(PlatformFileDialogTests, TreatsCancelledOrMalformedSelectionAsAbsent) {
  const std::vector<wchar_t> selected =
      explorer_selection_buffer({L"C:\\Textures\\albedo.png"});
  EXPECT_FALSE(quader::platform_file_dialog_detail::
                   parse_explorer_file_dialog_selection(false, selected)
                   .has_value());

  const std::vector<wchar_t> empty{L'\0', L'\0'};
  EXPECT_FALSE(quader::platform_file_dialog_detail::
                   parse_explorer_file_dialog_selection(true, empty)
                   .has_value());

  const std::vector<wchar_t> unterminated{L'C', L':', L'\\'};
  EXPECT_FALSE(quader::platform_file_dialog_detail::
                   parse_explorer_file_dialog_selection(true, unterminated)
                   .has_value());
}

TEST(PlatformFileDialogTests, GrowsTooSmallSelectionBuffersSafely) {
  const auto reported_growth =
      quader::platform_file_dialog_detail::next_file_dialog_buffer_capacity(
          32'768U, 40'000U);
  ASSERT_TRUE(reported_growth.has_value());
  EXPECT_EQ(*reported_growth, 65'536U);

  const auto repeated_growth =
      quader::platform_file_dialog_detail::next_file_dialog_buffer_capacity(
          65'536U, 65'536U);
  ASSERT_TRUE(repeated_growth.has_value());
  EXPECT_EQ(*repeated_growth, 131'072U);

  EXPECT_FALSE(
      quader::platform_file_dialog_detail::next_file_dialog_buffer_capacity(
          0U, 1U)
          .has_value());
}

TEST(FatalErrorHandlerTests, BuildsDeterministicArtifactNamesAndReportText) {
  const quader::FatalArtifactPaths paths = quader::make_fatal_artifact_paths(
      std::filesystem::path("reports"), "20260717-102030", 42U, 7U, true);
  EXPECT_EQ(paths.report_path,
            std::filesystem::path("reports/quader-fatal-20260717-102030-p42-"
                                  "t7.txt"));
  EXPECT_EQ(paths.dump_path,
            std::filesystem::path("reports/quader-fatal-20260717-102030-p42-"
                                  "t7.dmp"));

  const std::string report = quader::format_fatal_error_report({
      .generated_at = "2026-07-17 10:20:30",
      .process_id = 42U,
      .thread_id = 7U,
      .error_code = "QDR-FATAL-0006",
      .quick_message = "Application requested fatal shutdown.",
      .detail = "Errore \xC3\xA8 riproducibile.",
      .log_path = "logs/quader.log",
      .report_path = paths.report_path,
      .dump_path = paths.dump_path,
      .dump_status = "Minidump written.",
      .exception_record = "Exception record: unavailable.",
      .stack_trace = "Stack trace: deterministic.",
  });
  EXPECT_NE(report.find("Generated: 2026-07-17 10:20:30"),
            std::string::npos);
  EXPECT_NE(report.find("Error code: QDR-FATAL-0006"), std::string::npos);
  EXPECT_NE(report.find("Errore \xC3\xA8 riproducibile."),
            std::string::npos);
  EXPECT_NE(report.find(paths.report_path.string()), std::string::npos);
}

#if defined(_WIN32)
TEST(FatalErrorHandlerTests, FallsBackWhenPreferredReportCannotBeWritten) {
  const std::filesystem::path root =
      unique_test_directory(L"quader-fatal-write");
  const std::filesystem::path blocked_path = root / L"blocked.txt";
  const std::filesystem::path fallback_path =
      root / L"rapporti-\u00E8" / L"fatal.txt";
  std::error_code error;
  std::filesystem::create_directories(blocked_path, error);
  ASSERT_FALSE(error);

  const std::string text = "fatal report UTF-8: \xC3\xA8\n";
  const quader::FatalReportWriteResult result =
      quader::write_fatal_error_report(blocked_path, fallback_path, text);
  EXPECT_TRUE(result.written);
  EXPECT_TRUE(result.used_fallback);
  EXPECT_EQ(result.path, fallback_path);
  EXPECT_EQ(read_text_file(fallback_path), text);

  std::filesystem::remove_all(root, error);
}

#if defined(_MSC_VER)
namespace {
LONG invoke_installed_fatal_filter(EXCEPTION_POINTERS *exception) {
  const auto filter = SetUnhandledExceptionFilter(nullptr);
  if (filter == nullptr) {
    ExitProcess(99U);
  }
  return filter(exception);
}

void raise_fatal_probe_exception() {
  // Keep SEH in a function with no C++ objects requiring stack unwinding.
  __try {
    RaiseException(0xE0424242UL, 0U, 0U, nullptr);
  } __except (invoke_installed_fatal_filter(GetExceptionInformation())) {
  }
}
} // namespace
#endif

TEST(FatalErrorHandlerTests, DISABLED_FatalChildProbe) {
  const wchar_t *enabled = _wgetenv(L"QUADER_FATAL_CHILD_PROBE");
  ASSERT_NE(enabled, nullptr);
  ASSERT_EQ(std::wstring_view(enabled), L"1");
  const wchar_t *log_path = _wgetenv(L"QUADER_FATAL_CHILD_LOG");
  ASSERT_NE(log_path, nullptr);
  const wchar_t *mode_value = _wgetenv(L"QUADER_FATAL_CHILD_DIAGNOSTICS");
  const std::wstring_view mode = mode_value == nullptr ? L"real" : mode_value;
  quader::FatalDiagnosticsOptions diagnostics;
  if (mode == L"missing") {
    diagnostics.helper_path = std::filesystem::path(log_path).parent_path() /
                              L"missing-fatal-helper.exe";
  } else if (mode == L"hang" || mode == L"failure") {
    diagnostics.helper_path = current_executable_path();
    // Keep the production deadline: loading the instrumented test executable on
    // a busy machine can take several seconds before its probe begins.
    // Only the injected helper process consumes these filters. This child was
    // already selected by its explicit command-line GoogleTest filter.
    _wputenv_s(L"GTEST_FILTER", mode == L"hang"
        ? L"FatalErrorHandlerTests.DISABLED_HangingDiagnosticsHelper"
        : L"FatalErrorHandlerTests.DISABLED_FailingDiagnosticsHelper");
    _wputenv_s(L"GTEST_ALSO_RUN_DISABLED_TESTS", L"1");
  }
  quader::install_fatal_error_handler(std::filesystem::path(log_path), diagnostics);
#if defined(_MSC_VER)
  if (mode == L"exception") {
    raise_fatal_probe_exception();
  }
#endif
  quader::report_fatal_error_and_exit(
      quader::FatalErrorCode::ManualFatalReport,
      "child probe detail: errore \xC3\xA8", 73);
}

TEST(FatalErrorHandlerTests, DISABLED_HangingDiagnosticsHelper) {
  const wchar_t *log = _wgetenv(L"QUADER_FATAL_CHILD_LOG");
  ASSERT_NE(log, nullptr);
  const std::filesystem::path log_path(log);
  const auto report_directory = log_path.parent_path() / L"crash-reports";
  for (const auto &entry : std::filesystem::directory_iterator(report_directory)) {
    if (entry.path().extension() != ".txt") {
      continue;
    }
    const std::string initial = read_text_file(entry.path());
    ASSERT_NE(initial.find("QDR-FATAL-0006"), std::string::npos);
    ASSERT_NE(initial.find("Stack trace (raw addresses"), std::string::npos);
    ASSERT_EQ(initial.find("Detailed diagnostics result:"), std::string::npos);
    // The marker proves the initial report was flushed before optional work.
    std::ofstream marker(log_path.parent_path() / L"helper-observed-report.txt");
    marker << GetCurrentProcessId() << "\n" << initial;
    marker.close();
    auto partial_dump = entry.path();
    partial_dump.replace_extension(L".dmp.part");
    std::ofstream partial(partial_dump, std::ios::binary);
    partial << "incomplete dump";
    partial.close();
    Sleep(INFINITE);
  }
  FAIL() << "No durable initial report existed when the helper began.";
}

TEST(FatalErrorHandlerTests, DISABLED_FailingDiagnosticsHelper) {
  ExitProcess(88U);
}

namespace {
struct FatalChildArtifacts {
  std::filesystem::path root;
  std::filesystem::path log_path;
  DWORD wait = WAIT_FAILED;
  DWORD exit_code = 0;
  ULONGLONG elapsed_ms = 0;
  std::vector<std::filesystem::path> reports;
  std::vector<std::filesystem::path> dumps;
  std::vector<std::filesystem::path> partial_files;
  std::string report;
};

FatalChildArtifacts run_fatal_child(std::wstring_view diagnostics) {
  FatalChildArtifacts result;
  const auto executable = current_executable_path();
  EXPECT_FALSE(executable.empty());
  result.root = unique_test_directory(L"quader-fatal-child");
  result.log_path = result.root / L"logs" / L"quader.log";
  std::error_code error;
  std::filesystem::create_directories(result.log_path.parent_path(), error);
  EXPECT_FALSE(error);

  ScopedWideEnvironmentVariable child_probe(L"QUADER_FATAL_CHILD_PROBE", L"1");
  ScopedWideEnvironmentVariable child_log(L"QUADER_FATAL_CHILD_LOG",
                                          result.log_path.wstring());
  ScopedWideEnvironmentVariable diagnostic_mode(L"QUADER_FATAL_CHILD_DIAGNOSTICS",
                                                 std::wstring(diagnostics));
  ScopedWideEnvironmentVariable suppress_ui(L"QUADER_TEST_SUPPRESS_FATAL_UI", L"1");
  ScopedWideEnvironmentVariable sentry_disabled(L"QUADER_SENTRY_DSN", L"");

  std::wstring command = L"\"" + executable.wstring() +
      L"\" --gtest_also_run_disabled_tests "
      L"--gtest_filter=FatalErrorHandlerTests.DISABLED_FatalChildProbe --gtest_color=no";
  std::vector<wchar_t> command_buffer(command.begin(), command.end());
  command_buffer.push_back(L'\0');
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  const BOOL created = CreateProcessW(
      executable.wstring().c_str(), command_buffer.data(), nullptr, nullptr,
      FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
  EXPECT_TRUE(created);
  if (!created) {
    return result;
  }
  const ULONGLONG start = GetTickCount64();
  result.wait = WaitForSingleObject(process.hProcess, 30000U);
  result.elapsed_ms = GetTickCount64() - start;
  if (result.wait != WAIT_OBJECT_0) {
    TerminateProcess(process.hProcess, 0xDEADU);
    (void)WaitForSingleObject(process.hProcess, 1000U);
  }
  EXPECT_TRUE(GetExitCodeProcess(process.hProcess, &result.exit_code));
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);

  const auto report_directory = result.log_path.parent_path() / L"crash-reports";
  if (std::filesystem::is_directory(report_directory)) {
    for (const auto &entry : std::filesystem::directory_iterator(report_directory)) {
      if (entry.path().extension() == ".txt") {
        result.reports.push_back(entry.path());
      } else if (entry.path().extension() == ".dmp") {
        result.dumps.push_back(entry.path());
      } else if (entry.path().extension() == ".part") {
        result.partial_files.push_back(entry.path());
      }
    }
  }
  if (result.reports.size() == 1U) {
    result.report = read_text_file(result.reports[0]);
  }
  return result;
}

void expect_fatal_child_report(const FatalChildArtifacts &result) {
  ASSERT_EQ(result.wait, WAIT_OBJECT_0);
  EXPECT_EQ(result.exit_code, 73U);
  ASSERT_EQ(result.reports.size(), 1U);
  EXPECT_TRUE(result.reports[0].filename().string().starts_with("quader-fatal-"));
  EXPECT_NE(result.report.find("Error code: QDR-FATAL-0006"), std::string::npos);
  EXPECT_NE(result.report.find("child probe detail: errore \xC3\xA8"), std::string::npos);
  EXPECT_NE(result.report.find("Stack trace (raw addresses"), std::string::npos);
  EXPECT_NE(read_text_file(result.log_path).find("QDR-FATAL-0006"), std::string::npos);
  EXPECT_TRUE(result.partial_files.empty());
}

void remove_fatal_child_artifacts(const FatalChildArtifacts &result) {
  std::error_code ignored;
  std::filesystem::remove_all(result.root, ignored);
}
} // namespace

TEST(FatalErrorHandlerTests,
     ChildProcessWritesArtifactsWithoutUiOrSentryInitialization) {
  const auto result = run_fatal_child(L"real");
  expect_fatal_child_report(result);
  ASSERT_EQ(result.dumps.size(), 1U) << result.report;
  EXPECT_GT(std::filesystem::file_size(result.dumps.front()), 0U);
  EXPECT_NE(result.report.find("Detailed diagnostics helper completed."), std::string::npos);
  EXPECT_NE(result.report.find("Minidump written."), std::string::npos);
  EXPECT_NE(result.report.find("Stack trace (symbolized"), std::string::npos);
  remove_fatal_child_artifacts(result);
}

TEST(FatalErrorHandlerTests, MissingHelperStillWritesReportAndExitsWithOriginalCode) {
  const auto result = run_fatal_child(L"missing");
  expect_fatal_child_report(result);
  EXPECT_TRUE(result.dumps.empty());
  EXPECT_NE(result.report.find("helper executable is missing"), std::string::npos);
  remove_fatal_child_artifacts(result);
}

TEST(FatalErrorHandlerTests, HelperFailureIsRecordedWithoutLosingTheInitialReport) {
  const auto result = run_fatal_child(L"failure");
  expect_fatal_child_report(result);
  EXPECT_TRUE(result.dumps.empty());
  EXPECT_NE(result.report.find("helper failed with exit code 88"), std::string::npos)
      << result.report;
  remove_fatal_child_artifacts(result);
}

TEST(FatalErrorHandlerTests, StuckHelperIsTerminatedAfterTheInitialReportIsDurable) {
  const auto result = run_fatal_child(L"hang");
  expect_fatal_child_report(result);
  EXPECT_TRUE(result.dumps.empty());
  EXPECT_NE(result.report.find("timed out after 10000 ms"), std::string::npos)
      << result.report;
  EXPECT_LT(result.elapsed_ms, 20'000U);
  std::ifstream marker(result.log_path.parent_path() / L"helper-observed-report.txt");
  ASSERT_TRUE(marker.is_open());
  DWORD helper_id = 0;
  marker >> helper_id;
  ASSERT_NE(helper_id, 0U);
  HANDLE helper = OpenProcess(SYNCHRONIZE, FALSE, helper_id);
  if (helper != nullptr) {
    EXPECT_EQ(WaitForSingleObject(helper, 0U), WAIT_OBJECT_0)
        << "A timed-out diagnostics helper must not survive fatal exit.";
    CloseHandle(helper);
  } else {
    EXPECT_EQ(GetLastError(), ERROR_INVALID_PARAMETER);
  }
  marker.close();
  remove_fatal_child_artifacts(result);
}

#if defined(_MSC_VER)
namespace {
template <typename Record>
bool read_dump_record(std::ifstream &dump, std::uint64_t offset, Record &record) {
  dump.clear();
  dump.seekg(static_cast<std::streamoff>(offset));
  dump.read(reinterpret_cast<char *>(&record), sizeof(record));
  return dump.good();
}
} // namespace

namespace {
void expect_fatal_dump_context(bool captured_context) {
  const auto result = run_fatal_child(captured_context ? L"real" : L"exception");
  ASSERT_EQ(result.wait, WAIT_OBJECT_0);
  EXPECT_EQ(result.exit_code, captured_context ? 73U : 1U);
  ASSERT_EQ(result.reports.size(), 1U);
  EXPECT_NE(result.report.find(captured_context ? "Error code: QDR-FATAL-0006"
                                               : "Error code: QDR-FATAL-0005"),
            std::string::npos);
  if (captured_context) {
    EXPECT_NE(result.report.find("Captured fatal-report context (synthetic exception)"),
              std::string::npos);
  } else {
    EXPECT_NE(result.report.find("0xE0424242"), std::string::npos);
  }
  EXPECT_NE(result.report.find("Stack trace (symbolized fault context"), std::string::npos);
  ASSERT_EQ(result.dumps.size(), 1U) << result.report;

  // Read bounded records directly; tests must not introduce another DbgHelp
  // caller that could race the instrumented test process's Tracy worker.
  std::ifstream dump(result.dumps.front(), std::ios::binary);
  ASSERT_TRUE(dump.is_open());
  MINIDUMP_HEADER header{};
  ASSERT_TRUE(read_dump_record(dump, 0U, header));
  ASSERT_EQ(header.Signature, MINIDUMP_SIGNATURE);
  ASSERT_LT(header.NumberOfStreams, 1024U);
  RVA exception_rva = 0;
  RVA thread_list_rva = 0;
  RVA memory_list_rva = 0;
  RVA memory64_list_rva = 0;
  for (ULONG32 index = 0; index < header.NumberOfStreams; ++index) {
    MINIDUMP_DIRECTORY directory{};
    ASSERT_TRUE(read_dump_record(
        dump, header.StreamDirectoryRva + index * sizeof(directory), directory));
    if (directory.StreamType == ExceptionStream) {
      exception_rva = directory.Location.Rva;
    } else if (directory.StreamType == ThreadListStream) {
      thread_list_rva = directory.Location.Rva;
    } else if (directory.StreamType == MemoryListStream) {
      memory_list_rva = directory.Location.Rva;
    } else if (directory.StreamType == Memory64ListStream) {
      memory64_list_rva = directory.Location.Rva;
    }
  }
  ASSERT_NE(exception_rva, 0U);
  ASSERT_NE(thread_list_rva, 0U);
  MINIDUMP_EXCEPTION_STREAM exception{};
  ASSERT_TRUE(read_dump_record(dump, exception_rva, exception));
  EXPECT_EQ(exception.ExceptionRecord.ExceptionCode,
            captured_context
                ? 0xE0514400U | static_cast<DWORD>(quader::FatalErrorCode::ManualFatalReport)
                : 0xE0424242UL);
  EXPECT_NE(exception.ThreadId, 0U);
  EXPECT_NE(exception.ExceptionRecord.ExceptionAddress, 0U);
  ASSERT_GE(exception.ThreadContext.DataSize, sizeof(CONTEXT));
  ASSERT_NE(exception.ThreadContext.Rva, 0U);
  CONTEXT context{};
  ASSERT_TRUE(read_dump_record(dump, exception.ThreadContext.Rva, context));
  EXPECT_NE(context.ContextFlags & CONTEXT_CONTROL, 0U);
  std::uint64_t fault_stack_pointer = 0;
#if defined(_M_X64)
  EXPECT_NE(context.Rip, 0U);
  EXPECT_NE(context.Rsp, 0U);
  fault_stack_pointer = context.Rsp;
#elif defined(_M_IX86)
  EXPECT_NE(context.Eip, 0U);
  EXPECT_NE(context.Esp, 0U);
  fault_stack_pointer = context.Esp;
#elif defined(_M_ARM64)
  EXPECT_NE(context.Pc, 0U);
  EXPECT_NE(context.Sp, 0U);
  fault_stack_pointer = context.Sp;
#endif

  ULONG32 thread_count = 0;
  ASSERT_TRUE(read_dump_record(dump, thread_list_rva, thread_count));
  ASSERT_LT(thread_count, 4096U);
  for (ULONG32 index = 0; index < thread_count; ++index) {
    MINIDUMP_THREAD thread{};
    ASSERT_TRUE(read_dump_record(
        dump, thread_list_rva + sizeof(thread_count) + index * sizeof(thread), thread));
    EXPECT_NE(thread.ThreadId, exception.ThreadId)
        << "The deadline coordinator must not be suspended by automatic collection.";
  }
  // Excluding a thread also excludes its automatic stack capture. An explicit
  // memory callback must retain the original fault stack for offline unwinding.
  ASSERT_NE(fault_stack_pointer, 0U);
  bool fault_stack_saved = false;
  if (memory_list_rva != 0U) {
    ULONG32 count = 0;
    ASSERT_TRUE(read_dump_record(dump, memory_list_rva, count));
    ASSERT_LT(count, 65536U);
    for (ULONG32 index = 0; index < count; ++index) {
      MINIDUMP_MEMORY_DESCRIPTOR memory{};
      ASSERT_TRUE(read_dump_record(dump, memory_list_rva + sizeof(count) +
          index * sizeof(memory), memory));
      if (memory.StartOfMemoryRange <= fault_stack_pointer &&
          fault_stack_pointer - memory.StartOfMemoryRange + sizeof(std::uintptr_t) <=
              memory.Memory.DataSize) {
        std::uintptr_t saved_word = 0;
        ASSERT_TRUE(read_dump_record(dump, memory.Memory.Rva + fault_stack_pointer -
            memory.StartOfMemoryRange, saved_word));
        fault_stack_saved = true;
      }
    }
  }
  if (memory64_list_rva != 0U) {
    ULONG64 count = 0;
    RVA64 data_rva = 0;
    ASSERT_TRUE(read_dump_record(dump, memory64_list_rva, count));
    ASSERT_TRUE(read_dump_record(dump, memory64_list_rva + sizeof(count), data_rva));
    ASSERT_LT(count, 65536U);
    for (ULONG64 index = 0; index < count; ++index) {
      MINIDUMP_MEMORY_DESCRIPTOR64 memory{};
      ASSERT_TRUE(read_dump_record(dump, memory64_list_rva + sizeof(count) +
          sizeof(data_rva) + index * sizeof(memory), memory));
      if (memory.StartOfMemoryRange <= fault_stack_pointer &&
          fault_stack_pointer - memory.StartOfMemoryRange + sizeof(std::uintptr_t) <=
              memory.DataSize) {
        std::uintptr_t saved_word = 0;
        ASSERT_TRUE(read_dump_record(dump, data_rva + fault_stack_pointer -
            memory.StartOfMemoryRange, saved_word));
        fault_stack_saved = true;
      }
      data_rva += memory.DataSize;
    }
  }
  EXPECT_TRUE(fault_stack_saved) << "The saved exception context needs its stack bytes.";
  dump.close();
  remove_fatal_child_artifacts(result);
}
} // namespace

TEST(FatalErrorHandlerTests, RealDumpExcludesWaiterButPreservesFaultExceptionContext) {
  expect_fatal_dump_context(false);
}

TEST(FatalErrorHandlerTests, ManualFatalDumpPreservesCapturedContextAndStackWithoutSuspendingWaiter) {
  expect_fatal_dump_context(true);
}
#endif
#endif
