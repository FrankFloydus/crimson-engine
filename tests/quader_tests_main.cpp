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

namespace {

constexpr std::string_view kTestProfilePrefix =
    "quader-tests-local-app-data-";

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

bool remove_test_profile(const std::filesystem::path &profile) {
  std::error_code error;
  std::filesystem::remove_all(profile, error);
  return !error;
}

} // namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);

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
