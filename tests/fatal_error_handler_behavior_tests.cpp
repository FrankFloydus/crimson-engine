////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "platform/fatal_error_handler.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

namespace {

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

TEST(FatalErrorHandlerTests, DISABLED_FatalChildProbe) {
  const wchar_t *enabled = _wgetenv(L"QUADER_FATAL_CHILD_PROBE");
  ASSERT_NE(enabled, nullptr);
  ASSERT_EQ(std::wstring_view(enabled), L"1");
  const wchar_t *log_path = _wgetenv(L"QUADER_FATAL_CHILD_LOG");
  ASSERT_NE(log_path, nullptr);
  quader::install_fatal_error_handler(std::filesystem::path(log_path));
  quader::report_fatal_error_and_exit(
      quader::FatalErrorCode::ManualFatalReport,
      "child probe detail: errore \xC3\xA8", 73);
}

TEST(FatalErrorHandlerTests,
     ChildProcessWritesArtifactsWithoutUiOrSentryInitialization) {
  const std::filesystem::path executable = current_executable_path();
  ASSERT_FALSE(executable.empty());
  const std::filesystem::path root =
      unique_test_directory(L"quader-fatal-child");
  const std::filesystem::path log_path = root / L"logs" / L"quader.log";
  std::error_code error;
  std::filesystem::create_directories(log_path.parent_path(), error);
  ASSERT_FALSE(error);

  ScopedWideEnvironmentVariable child_probe(L"QUADER_FATAL_CHILD_PROBE",
                                               L"1");
  ScopedWideEnvironmentVariable child_log(L"QUADER_FATAL_CHILD_LOG",
                                             log_path.wstring());
  ScopedWideEnvironmentVariable suppress_ui(
      L"QUADER_TEST_SUPPRESS_FATAL_UI", L"1");
  ScopedWideEnvironmentVariable sentry_disabled(L"QUADER_SENTRY_DSN", L"");

  std::wstring command = L"\"" + executable.wstring() +
                         L"\" --gtest_also_run_disabled_tests "
                         L"--gtest_filter=FatalErrorHandlerTests."
                         L"DISABLED_FatalChildProbe --gtest_color=no";
  std::vector<wchar_t> command_buffer(command.begin(), command.end());
  command_buffer.push_back(L'\0');
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  ASSERT_TRUE(CreateProcessW(executable.wstring().c_str(),
                             command_buffer.data(), nullptr, nullptr, FALSE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                             &process));

  const DWORD wait = WaitForSingleObject(process.hProcess, 30000U);
  if (wait == WAIT_TIMEOUT) {
    TerminateProcess(process.hProcess, 0xDEADU);
  }
  EXPECT_EQ(wait, WAIT_OBJECT_0);
  DWORD exit_code = 0;
  ASSERT_TRUE(GetExitCodeProcess(process.hProcess, &exit_code));
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  EXPECT_EQ(exit_code, 73U);

  const std::filesystem::path report_directory =
      log_path.parent_path() / L"crash-reports";
  ASSERT_TRUE(std::filesystem::exists(report_directory));
  std::vector<std::filesystem::path> reports;
  std::vector<std::filesystem::path> dumps;
  for (const std::filesystem::directory_entry &entry :
       std::filesystem::directory_iterator(report_directory)) {
    if (entry.path().extension() == ".txt") {
      reports.push_back(entry.path());
    } else if (entry.path().extension() == ".dmp") {
      dumps.push_back(entry.path());
    }
  }
  ASSERT_EQ(reports.size(), 1U);
  EXPECT_TRUE(reports.front().filename().string().starts_with("quader-fatal-"));
  const std::string report = read_text_file(reports.front());
  EXPECT_NE(report.find("Error code: QDR-FATAL-0006"), std::string::npos);
  EXPECT_NE(report.find("child probe detail: errore \xC3\xA8"),
            std::string::npos);
  EXPECT_NE(report.find("Stack trace"), std::string::npos);
  EXPECT_NE(read_text_file(log_path).find("QDR-FATAL-0006"),
            std::string::npos);

  if (const wchar_t *validate_dump =
          _wgetenv(L"QUADER_VALIDATE_FATAL_MINIDUMP");
      validate_dump != nullptr && std::wstring_view(validate_dump) == L"1") {
    ASSERT_EQ(dumps.size(), 1U);
    EXPECT_GT(std::filesystem::file_size(dumps.front()), 0U);
  }

  std::filesystem::remove_all(root, error);
}
#endif
