////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#if defined(QUADER_TEST_HEADLESS_GEOMETRY)
#include <QCoreApplication>
#include <QEventLoop>
#include <QGuiApplication>
#include <QRect>
#include <QScreen>
#include <qpa/qwindowsysteminterface.h>
#endif

namespace {

constexpr std::string_view kTestProfilePrefix =
    "quader-tests-local-app-data-";

#if defined(QUADER_TEST_HEADLESS_GEOMETRY)
// Minimal QPA owns a virtual screen and never creates desktop windows. Its
// 240x320 default is a device stub, not the application's supported work area.
void configure_headless_test_screen() {
  if (QGuiApplication::platformName() != QStringLiteral("minimal")) return;
  QScreen *const screen = QGuiApplication::primaryScreen();
  if (screen == nullptr) return;
  const QRect geometry(0, 0, 1920, 1080);
  QWindowSystemInterface::handleScreenGeometryChange(screen, geometry, geometry);
  QWindowSystemInterface::handleScreenLogicalDotsPerInchChange(screen, 96., 96.);
  QWindowSystemInterface::sendWindowSystemEvents(QEventLoop::AllEvents);
}
Q_COREAPP_STARTUP_FUNCTION(configure_headless_test_screen)
#endif

std::optional<std::filesystem::path> create_test_profile() {
  std::error_code error;
  const std::filesystem::path temporary_root =
      std::filesystem::temp_directory_path(error);
  if (error) {
    return std::nullopt;
  }

  const auto tick = static_cast<std::uint64_t>(
      std::chrono::steady_clock::now().time_since_epoch().count());
  for (std::uint32_t attempt = 0u; attempt < 100u; ++attempt) {
    const std::filesystem::path candidate =
        temporary_root /
        (std::string(kTestProfilePrefix) + std::to_string(tick) + "-" +
         std::to_string(attempt));
    error.clear();
    if (std::filesystem::create_directory(candidate, error)) {
      return candidate;
    }
    if (error) {
      return std::nullopt;
    }
  }
  return std::nullopt;
}

bool use_test_profile(const std::filesystem::path &profile) {
#if defined(_WIN32)
  return _putenv_s("LOCALAPPDATA", profile.string().c_str()) == 0;
#else
  return ::setenv("LOCALAPPDATA", profile.string().c_str(), 1) == 0;
#endif
}

bool use_qt_plugin_path(const char *executable_path) {
  std::error_code error;
  const std::filesystem::path executable_directory =
      std::filesystem::absolute(executable_path, error).parent_path();
  if (error || executable_directory.empty()) {
    return false;
  }
#if defined(_WIN32)
  return _putenv_s("QT_PLUGIN_PATH", executable_directory.string().c_str()) ==
         0;
#else
  return ::setenv("QT_PLUGIN_PATH", executable_directory.string().c_str(), 1) ==
         0;
#endif
}

bool remove_test_profile(const std::filesystem::path &profile) {
  std::error_code error;
  std::filesystem::remove_all(profile, error);
  return !error;
}

} // namespace

#if defined(QUADER_TEST_HEADLESS_GEOMETRY)
TEST(HeadlessModelerScreenTests, UsesTheSupportedVirtualWorkAreaWithoutDesktopWindows) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "headless-modeler-screen";
  char *argv[] = {name, nullptr};
  QGuiApplication application(argc, argv);
  if (QGuiApplication::platformName() != QStringLiteral("minimal"))
    GTEST_SKIP() << "The native-theme subprocess has its own platform screen.";
  ASSERT_NE(QGuiApplication::primaryScreen(), nullptr);
  EXPECT_EQ(QGuiApplication::primaryScreen()->geometry(), QRect(0, 0, 1920, 1080));
  EXPECT_EQ(QGuiApplication::primaryScreen()->availableGeometry(), QRect(0, 0, 1920, 1080));
  EXPECT_DOUBLE_EQ(QGuiApplication::primaryScreen()->logicalDotsPerInch(), 96.);
}
#endif

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);

  if (!use_qt_plugin_path(argv[0])) {
    std::cerr << "Could not configure the Qt test plugin path.\n";
    return 2;
  }

  const std::optional<std::filesystem::path> profile = create_test_profile();
  if (!profile.has_value()) {
    std::cerr << "Could not create an isolated Quader test profile.\n";
    return 2;
  }
  if (!use_test_profile(*profile)) {
    std::cerr << "Could not activate the isolated Quader test profile.\n";
    (void)remove_test_profile(*profile);
    return 2;
  }

  const int test_result = RUN_ALL_TESTS();
  if (!remove_test_profile(*profile)) {
    std::cerr << "Could not remove the isolated Quader test profile: "
              << profile->string() << '\n';
    return test_result == 0 ? 2 : test_result;
  }
  return test_result;
}
