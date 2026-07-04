////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "support/guardrail_file_scan.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

namespace {

using quader::test::guardrail::source_root;

std::string lower_copy(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });
  return value;
}

bool is_style_tool_cpp_extension(const std::filesystem::path &path) {
  static constexpr std::array<std::string_view, 8> kCppExtensions = {
      ".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx"};
  const std::string extension = lower_copy(path.extension().string());
  return std::find(kCppExtensions.begin(), kCppExtensions.end(), extension) !=
         kCppExtensions.end();
}

bool style_tool_skips_snake_case_part(std::string_view part) {
  static constexpr std::array<std::string_view, 5> kSkippedParts = {
      "src", "sdk", "include", "tests", "examples"};
  return std::find(kSkippedParts.begin(), kSkippedParts.end(), part) !=
         kSkippedParts.end();
}

bool path_has_style_tool_snake_case_parts(
    const std::filesystem::path &relative_path) {
  static const std::regex kSnakeCase(R"(^[a-z0-9]+(_[a-z0-9]+)*$)");

  std::filesystem::path path_without_extension = relative_path;
  path_without_extension.replace_extension();
  for (const std::filesystem::path &part_path : path_without_extension) {
    const std::string part = part_path.generic_string();
    if (style_tool_skips_snake_case_part(part)) {
      continue;
    }
    if (!std::regex_match(part, kSnakeCase)) {
      return false;
    }
  }
  return true;
}

bool starts_with_q_word_prefix(const std::filesystem::path &path) {
  const std::string filename = path.filename().string();
  return filename.rfind("q_", 0) == 0;
}

std::vector<std::string> cpp_style_path_violations() {
  static constexpr std::array<std::string_view, 3> kProjectOwnedCppRoots = {
      "src", "tests", "examples"};

  std::vector<std::string> violations;
  const std::filesystem::path root = source_root();
  for (std::string_view root_name : kProjectOwnedCppRoots) {
    const std::filesystem::path scan_root = root / std::string(root_name);
    if (!std::filesystem::exists(scan_root)) {
      continue;
    }

    for (const std::filesystem::directory_entry &entry :
         std::filesystem::recursive_directory_iterator(scan_root)) {
      const std::filesystem::path relative =
          std::filesystem::relative(entry.path(), root);

      if (entry.is_directory()) {
        if (!path_has_style_tool_snake_case_parts(relative)) {
          violations.push_back("Directory must use snake_case: " +
                               relative.generic_string());
        }
        continue;
      }

      if (!entry.is_regular_file() ||
          !is_style_tool_cpp_extension(entry.path())) {
        continue;
      }

      if (!path_has_style_tool_snake_case_parts(relative)) {
        violations.push_back("C++ file path must use snake_case: " +
                             relative.generic_string());
      }
      if (starts_with_q_word_prefix(entry.path())) {
        violations.push_back(
            "`Q` is a prefix, not a word; use qname style: " +
            relative.generic_string());
      }
    }
  }

  std::sort(violations.begin(), violations.end());
  return violations;
}

} // namespace

TEST(CppStyleGuardrailTests,
     ProjectOwnedCppPathsMatchCheckCppStylePathOnlyRules) {
  for (const std::string &violation : cpp_style_path_violations()) {
    ADD_FAILURE() << violation;
  }
}
