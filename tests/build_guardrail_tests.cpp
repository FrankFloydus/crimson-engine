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
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <regex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <Windows.h>
#endif

namespace {

using quader::test::guardrail::files_with_extension;
using quader::test::guardrail::normalized_relative;
using quader::test::guardrail::read_file;
using quader::test::guardrail::read_source_file;
using quader::test::guardrail::source_path;
using quader::test::guardrail::source_files_under;
using quader::test::guardrail::source_root;
using quader::test::guardrail::source_slice;
using quader::test::guardrail::starts_with;

void expect_contains(const std::string &source, const std::string &needle) {
  EXPECT_NE(source.find(needle), std::string::npos) << needle;
}

void expect_missing(const std::string &source, const std::string &needle) {
  EXPECT_EQ(source.find(needle), std::string::npos) << needle;
}

/**
 * Captures one child-process status and its merged standard output.
 */
struct CommandResult {
  int exit_code{-1};
  std::string output;
};

CommandResult run_command_capture(const std::string &command) {
#if defined(_WIN32)
  FILE *pipe = _popen((command + " 2>&1").c_str(), "r");
#else
  FILE *pipe = popen((command + " 2>&1").c_str(), "r");
#endif
  if (pipe == nullptr) {
    return {};
  }

  std::string output;
  std::array<char, 4096> buffer{};
  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) !=
         nullptr) {
    output += buffer.data();
  }

#if defined(_WIN32)
  const int exit_code = _pclose(pipe);
#else
  const int exit_code = pclose(pipe);
#endif
  return {exit_code, std::move(output)};
}

std::string quoted_process_argument(const std::filesystem::path &path) {
  return "\"" + path.string() + "\"";
}

std::string powershell_literal(const std::filesystem::path &path) {
  std::string value = path.string();
  std::size_t offset = 0;
  while ((offset = value.find('\'', offset)) != std::string::npos) {
    value.insert(offset, 1, '\'');
    offset += 2;
  }
  return "'" + value + "'";
}

std::filesystem::path current_executable_path() {
#if defined(_WIN32)
  std::vector<wchar_t> buffer(32768, L'\0');
  const DWORD length = ::GetModuleFileNameW(
      nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  if (length == 0 || length == static_cast<DWORD>(buffer.size())) {
    throw std::runtime_error("Could not resolve the current executable path.");
  }
  return std::filesystem::path(std::wstring(buffer.data(), length));
#else
  return std::filesystem::canonical("/proc/self/exe");
#endif
}

void expect_contains_in_order(const std::string &source,
                              const std::vector<std::string> &needles) {
  std::size_t offset = 0;
  for (const std::string &needle : needles) {
    const std::size_t found = source.find(needle, offset);
    ASSERT_NE(found, std::string::npos) << needle << "\n" << source;
    offset = found + needle.size();
  }
}

std::string joined(std::string_view left, std::string_view right) {
  return std::string(left) + std::string(right);
}

std::vector<std::string> retired_modeling_test_framework_needles() {
  const std::string framework_name = joined("Catch", "2");
  const std::string framework_lower = joined("cat", "ch2");
  const std::string framework_upper = joined("CATCH", "2");
  return {
      framework_name,
      framework_lower,
      framework_upper,
      joined("Catch", "::"),
      joined("#include <cat", "ch2/"),
      joined("TEST", "_CASE("),
      joined("SEC", "TION("),
      joined("REQUIRE", "("),
      joined("CHECK", "("),
      joined("QUADER_", framework_upper),
      joined("QUADER_ALLOW_EXTERNAL_", framework_upper + "_FETCH"),
      joined("QUADER_MODELING_TEST_SUPPORT_", framework_upper),
      joined(framework_lower, "-BSL"),
      joined("thirdparty/", framework_lower),
      joined("_deps/", framework_lower),
  };
}

std::size_t find_retired_framework_reference(std::string_view source,
                                           std::string_view needle) {
  if (!needle.ends_with('(')) {
    return source.find(needle);
  }
  std::size_t offset = source.find(needle);
  while (offset != std::string_view::npos) {
    const bool continues_identifier =
        offset != 0u &&
        (std::isalnum(static_cast<unsigned char>(source[offset - 1u])) != 0 ||
         source[offset - 1u] == '_');
    if (!continues_identifier) {
      return offset;
    }
    offset = source.find(needle, offset + needle.size());
  }
  return std::string_view::npos;
}

bool is_comment_capable_project_file(
    const std::filesystem::path &relative_path) {
  const std::string path = relative_path.generic_string();
  if (starts_with(path, "thirdparty/") ||
      starts_with(path, "src/sdk/core/modeling/internal/thirdparty/") ||
      starts_with(path, "vcpkg_installed/")) {
    return false;
  }

  const std::string name = relative_path.filename().string();
  const std::string extension = relative_path.extension().string();
  const std::vector<std::string> comment_capable_extensions = {
      ".c",     ".cc",  ".cpp", ".cxx", ".h",    ".hh",  ".hpp",  ".hxx",
      ".cmake", ".ps1", ".md",  ".txt", ".lang", ".yml", ".yaml",
  };
  const std::vector<std::string> comment_capable_names = {
      ".clang-tidy",    ".clang-format", ".editorconfig",
      ".gitattributes", ".gitignore",    "CMakeLists.txt",
  };

  return std::find(comment_capable_extensions.begin(),
                   comment_capable_extensions.end(),
                   extension) != comment_capable_extensions.end() ||
         std::find(comment_capable_names.begin(), comment_capable_names.end(),
                   name) != comment_capable_names.end();
}

std::vector<std::filesystem::path> project_owned_comment_capable_files() {
  const std::filesystem::path root = source_root();
  const std::vector<std::filesystem::path> roots = {
      ".clang-tidy",          ".gitignore",        "AGENTS.md",
      "architecture-review.md", "INPUT_KEYBINDINGS.md", "memory.md",
      "tasklist.md",           "CMakeLists.txt",    "benchmarks",
      "cmake",                "examples",          "resources/locales",
      "src",                  "tests",             "tools",
  };

  std::vector<std::filesystem::path> files;
  for (const std::filesystem::path &relative_root : roots) {
    const std::filesystem::path path = root / relative_root;
    if (!std::filesystem::exists(path)) {
      continue;
    }
    if (std::filesystem::is_regular_file(path)) {
      if (is_comment_capable_project_file(relative_root)) {
        files.push_back(relative_root);
      }
      continue;
    }

    for (const std::filesystem::directory_entry &entry :
         std::filesystem::recursive_directory_iterator(path)) {
      if (!entry.is_regular_file()) {
        continue;
      }

      const std::filesystem::path relative =
          std::filesystem::relative(entry.path(), root);
      if (is_comment_capable_project_file(relative)) {
        files.push_back(relative);
      }
    }
  }

  std::sort(files.begin(), files.end());
  return files;
}

bool is_cpp_owned_source_extension(const std::string &extension) {
  const std::vector<std::string> extensions = {
      ".c",   ".cc",  ".cpp", ".cxx", ".h",   ".hh",
      ".hpp", ".hxx", ".ixx", ".inl", ".ipp", ".tpp",
  };
  return std::find(extensions.begin(), extensions.end(), extension) !=
         extensions.end();
}

std::vector<std::filesystem::path> project_owned_cpp_files() {
  const std::filesystem::path root = source_root();
  const std::vector<std::filesystem::path> roots = {
      "benchmarks", "examples", "src", "tests", "tools",
  };

  std::vector<std::filesystem::path> files;
  for (const std::filesystem::path &relative_root : roots) {
    const std::filesystem::path path = root / relative_root;
    if (!std::filesystem::exists(path)) {
      continue;
    }

    for (const std::filesystem::directory_entry &entry :
         std::filesystem::recursive_directory_iterator(path)) {
      if (!entry.is_regular_file() ||
          !is_cpp_owned_source_extension(entry.path().extension().string())) {
        continue;
      }

      files.push_back(std::filesystem::relative(entry.path(), root));
    }
  }

  std::sort(files.begin(), files.end());
  return files;
}

bool is_snake_case_file_stem(const std::filesystem::path &relative_path) {
  static const std::regex kSnakeCase(R"(^[a-z0-9]+(_[a-z0-9]+)*$)");
  return std::regex_match(relative_path.stem().string(), kSnakeCase);
}

bool contains_stale_project_cpp_path(std::string source) {
  std::replace(source.begin(), source.end(), '\\', '/');
  static const std::regex kStalePath(
      R"((?:src|tests|examples|benchmarks)/[A-Za-z0-9_./-]*[A-Z][A-Za-z0-9_./-]*\.(?:h|cpp)\b|(?:src|tests|examples|benchmarks)/[A-Za-z0-9_./-]*\.(?:h|hh|hxx|cc|cxx|ixx|inl)\b)");
  return std::regex_search(source, kStalePath);
}

std::vector<std::filesystem::path> project_owned_recursive_files() {
  const std::filesystem::path root = source_root();
  const std::vector<std::filesystem::path> roots = {
      ".clang-tidy",          ".gitignore", "AGENTS.md",
      "architecture-review.md", "CMakeLists.txt", "benchmarks",
      "cmake",                "examples",   "memory.md",
      "tasklist.md",          "INPUT_KEYBINDINGS.md", "resources", "src",
      "tests",                "tools",
  };

  std::vector<std::filesystem::path> files;
  for (const std::filesystem::path &relative_root : roots) {
    const std::filesystem::path path = root / relative_root;
    if (!std::filesystem::exists(path)) {
      continue;
    }
    if (std::filesystem::is_regular_file(path)) {
      files.push_back(relative_root);
      continue;
    }

    for (const std::filesystem::directory_entry &entry :
         std::filesystem::recursive_directory_iterator(path)) {
      if (entry.is_regular_file()) {
        files.push_back(std::filesystem::relative(entry.path(), root));
      }
    }
  }

  std::sort(files.begin(), files.end());
  return files;
}

std::vector<std::filesystem::path> guardrail_source_files() {
  const std::filesystem::path root = source_root();
  const std::vector<std::filesystem::path> roots = {
      "src",        "tests",          "cmake",
      "tools",      "CMakeLists.txt", "CMakePresets.json",
      "vcpkg.json",
  };
  const std::vector<std::string> extensions = {
      ".c", ".cc", ".cpp", ".hpp", ".cmake", ".txt", ".json", ".ps1",
  };

  std::vector<std::filesystem::path> files;
  for (const std::filesystem::path &relative_root : roots) {
    const std::filesystem::path path = root / relative_root;
    if (!std::filesystem::exists(path)) {
      continue;
    }
    if (std::filesystem::is_regular_file(path)) {
      files.push_back(relative_root);
      continue;
    }

    for (const std::filesystem::directory_entry &entry :
         std::filesystem::recursive_directory_iterator(path)) {
      if (!entry.is_regular_file()) {
        continue;
      }

      const std::string extension = entry.path().extension().string();
      if (std::find(extensions.begin(), extensions.end(), extension) ==
          extensions.end()) {
        continue;
      }

      files.push_back(std::filesystem::relative(entry.path(), root));
    }
  }

  return files;
}

bool contains_git_commit_literal(const std::string &source) {
  static const std::regex kCommitLiteral(
      R"((^|[^0-9a-fA-F])[0-9a-fA-F]{40}([^0-9a-fA-F]|$))");
  return std::regex_search(source, kCommitLiteral);
}

std::size_t count_occurrences(std::string_view source,
                              std::string_view needle) {
  std::size_t count = 0;
  std::size_t offset = 0;
  while ((offset = source.find(needle, offset)) != std::string_view::npos) {
    ++count;
    offset += needle.size();
  }
  return count;
}

bool ends_with(std::string_view value, std::string_view suffix) {
  return value.size() >= suffix.size() &&
         value.substr(value.size() - suffix.size()) == suffix;
}

std::string trimmed(std::string value) {
  const auto first =
      std::find_if_not(value.begin(), value.end(), [](unsigned char character) {
        return std::isspace(character) != 0;
      });
  const auto last = std::find_if_not(value.rbegin(), value.rend(),
                                     [](unsigned char character) {
                                       return std::isspace(character) != 0;
                                     })
                        .base();
  if (first >= last) {
    return {};
  }
  return {first, last};
}

std::size_t matching_cmake_call_end(std::string_view source,
                                    std::size_t call_begin) {
  const std::size_t open = source.find('(', call_begin);
  if (open == std::string_view::npos) {
    return std::string_view::npos;
  }

  int depth = 0;
  bool in_quote = false;
  bool in_comment = false;
  for (std::size_t index = open; index < source.size(); ++index) {
    const char character = source[index];
    if (in_comment) {
      if (character == '\n') {
        in_comment = false;
      }
      continue;
    }
    if (!in_quote && character == '#') {
      in_comment = true;
      continue;
    }
    if (character == '"') {
      in_quote = !in_quote;
      continue;
    }
    if (in_quote) {
      continue;
    }
    if (character == '(') {
      ++depth;
      continue;
    }
    if (character == ')') {
      --depth;
      if (depth == 0) {
        return index + 1;
      }
    }
  }

  return std::string_view::npos;
}

std::vector<std::string> tokenize_cmake_arguments(std::string_view source) {
  std::vector<std::string> tokens;
  std::string token;
  bool in_quote = false;
  bool in_comment = false;

  for (const char character : source) {
    if (in_comment) {
      if (character == '\n') {
        in_comment = false;
      }
      continue;
    }
    if (!in_quote && character == '#') {
      in_comment = true;
      continue;
    }
    if (character == '"') {
      in_quote = !in_quote;
      continue;
    }
    if (!in_quote && std::isspace(static_cast<unsigned char>(character)) != 0) {
      if (!token.empty()) {
        tokens.push_back(token);
        token.clear();
      }
      continue;
    }
    token.push_back(character);
  }
  if (!token.empty()) {
    tokens.push_back(token);
  }
  return tokens;
}

std::string cmake_target_source_blocks(std::string_view source,
                                       std::string_view target) {
  const std::vector<std::string_view> commands = {
      "add_library",
      "add_executable",
      "target_sources",
  };
  std::string blocks;

  for (std::string_view command : commands) {
    const std::string marker = std::string(command) + "(";
    std::size_t cursor = 0;
    while ((cursor = source.find(marker, cursor)) != std::string_view::npos) {
      const std::size_t args_begin = cursor + marker.size();
      const std::size_t call_end = matching_cmake_call_end(source, cursor);
      if (call_end == std::string_view::npos) {
        break;
      }
      const std::size_t args_end = call_end - 1;
      const std::vector<std::string> tokens = tokenize_cmake_arguments(
          source.substr(args_begin, args_end - args_begin));
      if (!tokens.empty() && tokens.front() == target) {
        blocks.append(source.substr(cursor, call_end - cursor));
        blocks.push_back('\n');
      }
      cursor = call_end;
    }
  }

  return blocks;
}

std::vector<std::filesystem::path>
cmake_target_source_files(std::string_view source,
                          const std::vector<std::string_view> &targets) {
  std::string source_blocks;
  for (std::string_view target : targets) {
    source_blocks.append(cmake_target_source_blocks(source, target));
  }

  std::vector<std::filesystem::path> active_sources;
  for (const std::filesystem::path &relative_path : guardrail_source_files()) {
    const std::string normalized = relative_path.generic_string();
    if (starts_with(normalized, "src/") &&
        source_blocks.find(normalized) != std::string::npos) {
      active_sources.push_back(relative_path);
    }
  }
  std::sort(active_sources.begin(), active_sources.end());
  active_sources.erase(
      std::unique(active_sources.begin(), active_sources.end()),
      active_sources.end());
  return active_sources;
}

/**
 * Describes manifest and license evidence required for a project dependency.
 */
struct ThirdPartyLicenseExpectation {
  std::string_view package_id;
  std::string_view manifest_needle;
  std::string_view license_path;
  bool direct_vcpkg_dependency;
};

const std::vector<ThirdPartyLicenseExpectation> &
third_party_license_expectations() {
  static const std::vector<ThirdPartyLicenseExpectation> kExpectations = {
      {"", "OpenMesh", "licenses/openmesh-BSD-3-Clause.txt", false},
      {"", "earcut.hpp", "licenses/earcut-hpp-ISC.txt", false},
      {"nlohmann-json", "nlohmann/json", "licenses/nlohmann-json-MIT.txt", true},
      {"bgfx", "bgfx", "licenses/bgfx-BSD-2-Clause.txt", true},
      {"meshoptimizer", "meshoptimizer", "licenses/meshoptimizer-MIT.txt", true},
      {"vulkan-headers", "Vulkan Headers",
       "licenses/vulkan-headers-Apache-2.0-OR-MIT.txt", true},
      {"flatbuffers", "FlatBuffers", "licenses/flatbuffers-Apache-2.0.txt",
       true},
      {"", "Google Material Icons",
       "licenses/google-material-icons-Apache-2.0.txt", false},
      {"", "Home Video font", "licenses/home-video-CC0-1.0.txt", false},
      {"", "Godot AgX", "licenses/godot-MIT.txt", false},
      {"zstd", "Zstandard",
       "licenses/zstd-BSD-3-Clause-OR-GPL-2.0-only.txt", true},
      {"entt", "EnTT", "licenses/entt-MIT.txt", true},
      {"glm", "OpenGL Mathematics (GLM)", "licenses/glm-MIT.txt", true},
      {"qtbase", "Qt Base", "licenses/qtbase-LGPL-3.0-only.txt", true},
      {"qtimageformats", "Qt Image Formats",
       "licenses/qtimageformats-LGPL-3.0-only.txt", true},
      {"", "libjpeg-turbo",
       "licenses/libjpeg-turbo-BSD-3-Clause-IJG.txt", false},
      {"tiff", "LibTIFF", "licenses/libtiff-libtiff.txt", true},
      {"", "libwebp", "licenses/libwebp-BSD-3-Clause-and-PATENTS.txt",
       false},
      {"stduuid", "stduuid", "licenses/stduuid-MIT.txt", true},
      {"gtest", "GoogleTest", "licenses/gtest-BSD-3-Clause.txt", true},
      {"lodepng", "lodepng", "licenses/lodepng-Zlib.txt", true},
      {"sentry-native", "Sentry Native SDK", "licenses/sentry-native-MIT.txt",
       true},
      {"zlib", "zlib", "licenses/zlib-Zlib.txt", true},
      {"spdlog", "spdlog", "licenses/spdlog-MIT.txt", true},
      {"", "{fmt}", "licenses/fmt-MIT.txt", false},
      {"", "Tracy Profiler", "licenses/tracy-BSD-3-Clause.txt", false},
  };
  return kExpectations;
}

void skip_json_whitespace(const std::string &source, std::size_t &offset) {
  while (offset < source.size() &&
         std::isspace(static_cast<unsigned char>(source[offset])) != 0) {
    ++offset;
  }
}

std::string parse_json_string(const std::string &source, std::size_t &offset) {
  std::string value;
  EXPECT_LT(offset, source.size());
  EXPECT_EQ(source[offset], '"');
  ++offset;
  while (offset < source.size()) {
    const char current = source[offset++];
    if (current == '"') {
      break;
    }
    if (current == '\\' && offset < source.size()) {
      value.push_back(source[offset++]);
      continue;
    }
    value.push_back(current);
  }
  return value;
}

void append_vcpkg_dependency_array(const std::string &manifest,
                                   std::size_t dependencies_start,
                                   std::vector<std::string> &dependencies) {
  int array_depth = 1;
  int object_depth = 0;
  std::size_t offset = dependencies_start + 1;
  while (offset < manifest.size() && array_depth > 0) {
    const char current = manifest[offset];
    if (current == '"') {
      const int string_array_depth = array_depth;
      const int string_object_depth = object_depth;
      std::string token = parse_json_string(manifest, offset);
      if (string_array_depth == 1 && string_object_depth == 0) {
        dependencies.push_back(std::move(token));
        continue;
      }
      if (string_array_depth == 1 && string_object_depth == 1 &&
          token == "name") {
        skip_json_whitespace(manifest, offset);
        EXPECT_LT(offset, manifest.size());
        EXPECT_EQ(manifest[offset], ':');
        ++offset;
        skip_json_whitespace(manifest, offset);
        if (offset < manifest.size() && manifest[offset] == '"') {
          dependencies.push_back(parse_json_string(manifest, offset));
        }
      }
      continue;
    }
    if (current == '[') {
      ++array_depth;
    } else if (current == ']') {
      --array_depth;
    } else if (current == '{') {
      ++object_depth;
    } else if (current == '}') {
      --object_depth;
    }
    ++offset;
  }
}

std::vector<std::string> direct_vcpkg_dependencies() {
  const std::string manifest = read_source_file("vcpkg.json");
  std::vector<std::string> dependencies;
  std::size_t search_offset = 0;
  while (search_offset < manifest.size()) {
    const std::size_t dependencies_key =
        manifest.find("\"dependencies\"", search_offset);
    if (dependencies_key == std::string::npos) {
      break;
    }
    const std::size_t dependencies_start =
        manifest.find('[', dependencies_key);
    EXPECT_NE(dependencies_start, std::string::npos);
    if (dependencies_start == std::string::npos) {
      break;
    }
    append_vcpkg_dependency_array(manifest, dependencies_start, dependencies);
    search_offset = dependencies_start + 1;
  }

  std::sort(dependencies.begin(), dependencies.end());
  dependencies.erase(std::unique(dependencies.begin(), dependencies.end()),
                     dependencies.end());
  return dependencies;
}

const ThirdPartyLicenseExpectation *
expectation_for_direct_vcpkg_dependency(std::string_view package_id) {
  const std::vector<ThirdPartyLicenseExpectation> &expectations =
      third_party_license_expectations();
  const auto found =
      std::find_if(expectations.begin(), expectations.end(),
                   [&](const ThirdPartyLicenseExpectation &expectation) {
                     return expectation.direct_vcpkg_dependency &&
                            expectation.package_id == package_id;
                   });
  return found == expectations.end() ? nullptr : &(*found);
}

} // namespace

TEST(BuildGuardrailTests,
     ProjectOwnedCommentCapableFilesCarryProprietaryBanner) {
  for (const std::filesystem::path &relative_path :
       project_owned_comment_capable_files()) {
    const std::string source = read_source_file(relative_path);
    EXPECT_NE(source.find("Copyright (c) 2026 Francesco Di Blasi"),
              std::string::npos)
        << relative_path.generic_string();
    EXPECT_NE(source.find("protected proprietary source code"),
              std::string::npos)
        << relative_path.generic_string();
    EXPECT_NE(source.find("No permission is granted to use, copy, modify, "
                          "distribute, or sublicense"),
              std::string::npos)
        << relative_path.generic_string();
  }
}

TEST(BuildGuardrailTests, FormerSdkBoundaryIsAbsorbedIntoAppBuild) {
  const std::string root_c_make = read_source_file("CMakeLists.txt");
  const std::string absorbed_c_make =
      read_source_file("cmake/QuaderModeling.cmake");
  const std::string build_files = root_c_make + "\n" + absorbed_c_make;
  const std::string presets = read_source_file("CMakePresets.json");

  EXPECT_FALSE(std::filesystem::exists(source_path("sdk")));

  expect_missing(build_files, "QUADER_BUILD_SDK_TESTS");
  expect_missing(build_files, "QUADER_ENABLE_MODELING_SDK");
  expect_missing(build_files, "QUADER_MODELING_SDK_DIR");
  expect_missing(build_files, "QUADER_HAS_MODELING_SDK");
  expect_missing(build_files,
                 "add_subdirectory(\"${QUADER_MODELING_SDK_DIR}\"");
  expect_missing(build_files, "quader_sdk_tests");
  expect_missing(build_files, "quader_io_qdr");
  expect_missing(build_files, "quader_modeling_editor_core");
  expect_missing(build_files, "quader_modeling_editor_contract");
  expect_missing(build_files, "quader_add_host_contract_domain_test");
  expect_missing(
      build_files,
      joined("quader_add_modeling_domain_cat",
             "ch2_test(qdr_custom_engine_modeling_session_smoke_tests"));
  expect_missing(build_files,
                 "quader_add_qdr_runtime_domain_test(qdr_io_session_tests");
  expect_missing(build_files,
                 "quader_add_modeling_runtime_domain_test(qdr_editor_core_");
  expect_missing(build_files, "qdr_editor_core_*.cpp");
  expect_missing(build_files, "qdr_host_contract_");
  expect_missing(build_files, "tests/modeling_domain/host_contract");
  expect_missing(build_files,
                 "qdr_custom_engine_modeling_session_smoke_tests.cpp");
  expect_missing(build_files, "qdr_io_session_tests.cpp");

  expect_missing(presets, "vs2026-sdk-tests");
  expect_missing(presets, "QUADER_BUILD_SDK_TESTS");
  expect_missing(presets, "quader_sdk_tests");
  expect_contains(presets, "\"name\": \"vs2026-release\"");
  expect_contains(presets, "${sourceDir}/build/vs2026");
  expect_missing(presets, "ninja-msvc");

  expect_contains(build_files, "QUADER_CANONICAL_POLYGON_SOURCES");
  expect_contains(build_files,
                  "${QUADER_MODELING_SDK_INTERNAL_DIR}/mesh/geometry/geometry.cpp");
  expect_contains(build_files, "src/scene/document");
  expect_contains(build_files, "src/editor/modeling/runtime");
  expect_contains(build_files, "src/editor/modeling/operations");
  expect_contains(build_files, "src/editor/interactions");
  expect_contains(build_files, "src/editor/modeling/services");
  expect_contains(build_files, "src/editor/commands");
  expect_contains(build_files, "src/editor/history");
  expect_contains(build_files, "src/editor/metadata");
  expect_contains(build_files, "src/io/qdr");
  expect_contains(build_files, "resources/modeling_assets/materials/default");
  expect_contains(build_files, "quader_modeling_sdk_qdr_document");
  expect_contains(build_files, "quader_project_qdr_io");
  expect_contains(build_files,
                  "qdr_custom_engine_modeling_runtime_smoke_tests");
  expect_missing(build_files, "qdr_runtime_io_compat_tests");
  expect_missing(build_files, "quader_qdr_shared_io");
  expect_missing(build_files, "quader_qdr_document_io");
  expect_missing(build_files, "quader_qdr_runtime_io");
  expect_contains(build_files,
                  "tests/modeling_domain/editor_metadata/"
                  "qdr_editor_metadata_operation_catalog_tests.cpp");
  expect_contains(build_files, "qdr_editor_metadata_operation_catalog_tests");
  expect_missing(build_files, "src/modeling");
  expect_missing(build_files, "src/modeling/workspace");
  expect_missing(build_files, "src/modeling/tool/");
  expect_missing(build_files, "src/modeling/tools/");
  expect_missing(build_files, "src/document");
  expect_missing(build_files, "src/editor/actions");
  expect_missing(build_files, "src/editor/mesh");
  expect_missing(build_files, "src/geometry/");
  expect_missing(build_files, "resources/modeling/default_materials");
  expect_missing(build_files, "src/editor/tools/host_contract");
  expect_contains(build_files, "QUADER_ALLOW_EXTERNAL_OPENMESH_FETCH");
  for (const std::string &needle : retired_modeling_test_framework_needles()) {
    expect_missing(build_files, needle);
  }

  EXPECT_TRUE(
      std::filesystem::exists(source_path("src/editor/commands/document")));
}

TEST(BuildGuardrailTests,
     PolygonOperationResultsReportStructuredTopologyDeltas) {
  const std::string document_header =
      read_source_file("src/sdk/core/modeling/neutral/include/quader/modeling/"
                       "neutral/polygon/document_operations.hpp");
  expect_contains(document_header, "struct OperationElementDelta");
  expect_contains(document_header, "std::vector<ElementId> vertices;");
  expect_contains(document_header, "std::vector<Edge> edges;");
  expect_contains(document_header, "std::vector<ElementId> faces;");
  expect_contains(document_header, "OperationElementDelta created;");
  expect_contains(document_header, "OperationElementDelta deleted;");
  expect_contains(document_header, "OperationElementDelta affected;");

  /**
   * Stores the expected structured result-delta markers for one operation.
   */
  struct OperationResultExpectation {
    std::string_view path;
    std::vector<std::string_view> delta_needles;
  };

  const std::vector<OperationResultExpectation> expectations = {
      {"src/sdk/core/modeling/internal/mesh/polygon/operations/"
       "extrude_faces_operation.cpp",
       {"OperationResult result;", "result.created.vertices",
        "result.created.faces", "result.affected.faces", "return result;"}},
      {"src/sdk/core/modeling/internal/mesh/polygon/operations/"
       "thicken_faces_operation.cpp",
       {"OperationResult result;", "result.created.vertices",
        "result.created.faces", "result.affected.faces", "return result;"}},
      {"src/sdk/core/modeling/internal/mesh/polygon/operations/"
       "split_edges_operation.cpp",
       {"OperationResult result;", "result.created.vertices",
        "result.created.edges", "result.affected.edges",
        "result.affected.faces", "return result;"}},
      {"src/sdk/core/modeling/internal/mesh/polygon/operations/"
       "delete_selection_operation.cpp",
       {"OperationResult result;", "result.deleted = deleted_element_delta",
        "return result;"}},
  };

  for (const OperationResultExpectation &expectation : expectations) {
    const std::string source = read_source_file(expectation.path);
    for (std::string_view needle : expectation.delta_needles) {
      expect_contains(source, std::string(needle));
    }
    EXPECT_EQ(source.find("return { true, {} };"), std::string::npos)
        << expectation.path
        << " should not report successful topology changes only through "
           "changed/message and remapped Selection.";
  }
}

TEST(BuildGuardrailTests,
     PolygonDocumentPublicSurfaceIsSplitBehindCompatibilityUmbrella) {
  /**
   * Stores the ownership markers expected in one split polygon header.
   */
  struct PolygonHeaderExpectation {
    std::string_view path;
    std::vector<std::string_view> owner_needles;
  };

  const std::vector<PolygonHeaderExpectation> headers = {
      {"src/sdk/core/modeling/neutral/include/quader/modeling/neutral/"
       "polygon/document_model.hpp",
       {"struct Document", "add_vertex", "make_cube"}},
      {"src/sdk/core/modeling/neutral/include/quader/modeling/neutral/"
       "polygon/document_selection.hpp",
       {"struct Selection", "select_only", "selection_center"}},
      {"src/sdk/core/modeling/neutral/include/quader/modeling/neutral/"
       "polygon/document_operations.hpp",
       {"struct OperationResult", "PlaneCutRequest", "thicken_selected_faces"}},
      {"src/sdk/core/modeling/neutral/include/quader/modeling/neutral/"
       "polygon/document_topology.hpp",
       {"FacePerimeterInfo", "document_edges", "face_normal"}},
      {"src/sdk/core/modeling/neutral/include/quader/modeling/neutral/"
       "polygon/document_mesh_compilation.hpp",
       {"CompiledMesh", "compile_document"}},
      {"src/sdk/core/modeling/neutral/include/quader/modeling/neutral/"
       "polygon/document_picking.hpp",
       {"PickOptions", "PickResult", "pick_element"}},
      {"src/sdk/core/modeling/neutral/include/quader/modeling/neutral/"
       "polygon/document_overlay_preview.hpp",
       {"OverlayData", "KnifeSegmentPreview", "build_overlay"}},
      {"src/sdk/core/modeling/neutral/include/quader/modeling/neutral/"
       "polygon/element_handles.hpp",
       {"struct VertexId", "struct FaceId", "struct EdgeKey"}},
  };

  const std::string umbrella =
      read_source_file("src/sdk/core/modeling/neutral/include/quader/modeling/"
                       "neutral/polygon/document.hpp");
  for (const PolygonHeaderExpectation &header : headers) {
    const std::string path(header.path);
    ASSERT_TRUE(std::filesystem::exists(source_path(path))) << path;
    const std::string source = read_source_file(path);
    for (const std::string_view needle : header.owner_needles) {
      expect_contains(source, std::string(needle));
    }
    expect_missing(source, "#include <quader/modeling/neutral/polygon/document.hpp>");

    const std::string include_path =
        path.substr(std::string("src/sdk/core/modeling/neutral/include/quader/"
                                "modeling/neutral/polygon/")
                        .size());
    expect_contains(umbrella, "#include <quader/modeling/neutral/polygon/" + include_path + ">");
  }

  expect_contains(umbrella, "#include <quader/modeling/neutral/polygon/poly_operation.hpp>");
  expect_missing(umbrella, "struct Document {");
  expect_missing(umbrella, "struct Selection {");
  expect_missing(umbrella, "struct PickOptions {");
  expect_missing(umbrella, "struct OverlayData {");

  const std::string poly_operation =
      read_source_file("src/sdk/core/modeling/neutral/include/quader/modeling/"
                       "neutral/polygon/poly_operation.hpp");
  expect_contains(poly_operation,
                  "#include <quader/modeling/neutral/polygon/document_operations.hpp>");
  expect_missing(poly_operation, "#include <quader/modeling/neutral/polygon/document.hpp>");
}

TEST(BuildGuardrailTests, BuildOptionsAndDependencyPinsHaveSingleCMakeOwner) {
  const std::string root_c_make = read_source_file("CMakeLists.txt");
  const std::string options =
      read_source_file("cmake/QuaderBuildOptions.cmake");
  const std::string modeling = read_source_file("cmake/QuaderModeling.cmake");

  expect_contains(root_c_make, "include(cmake/QuaderBuildOptions.cmake)");
  for (const std::string &option_name : {
           "QUADER_BUILD_TESTS",
           "QUADER_BUILD_NATIVE_APP",
           "QUADER_USE_OPENMESH_TOPOLOGY",
           "QUADER_ALLOW_EXTERNAL_OPENMESH_FETCH",
           "QUADER_ENABLE_TRACY",
           "QUADER_ENABLE_SENTRY",
           "QUADER_TRACY_ON_DEMAND",
           "QUADER_BUILD_PERFORMANCE_BENCHMARKS",
           "QUADER_ENABLE_PCH",
       }) {
    expect_contains(options, "option(" + option_name);
    expect_missing(root_c_make, "option(" + option_name);
    expect_missing(modeling, "option(" + option_name);
  }

  const std::string pch_helper =
      source_slice(root_c_make, "function(quader_enable_common_pch target)",
                   "endfunction()");
  expect_contains(pch_helper, "target_precompile_headers(${target}");
  expect_contains(pch_helper, "PRIVATE");
  expect_contains(pch_helper, "<string>");
  expect_contains(pch_helper, "<vector>");
  expect_contains(pch_helper, "<unordered_map>");
  expect_missing(pch_helper, "PUBLIC");
  expect_missing(pch_helper, "INTERFACE");
  expect_missing(pch_helper, "src/");
  expect_missing(pch_helper, "filament/");
  expect_missing(pch_helper, "SDL3/");
  expect_missing(pch_helper, "gtest/");

  expect_contains(options, "set(_QUADER_OPENMESH_COMMIT");
  expect_contains(options, "function(quader_resolve_dependency_source");
  expect_contains(options, "ENV{${env_var}}");
  expect_contains(options, "Desktop/_QUADER_DEV/_deps/${dev_dep_dir}");
  expect_missing(root_c_make, "set(_QUADER_OPENMESH_COMMIT");
  expect_missing(root_c_make, "GEOMETRIC_TOOLS");
  expect_missing(options, "GEOMETRIC_TOOLS");
  expect_missing(modeling, "set(QUADER_OPENMESH_COMMIT");
  expect_missing(modeling, "GEOMETRIC_TOOLS");
  EXPECT_EQ(count_occurrences(root_c_make + "\n" + modeling,
                              "Desktop/_QUADER_DEV/_deps/openmesh-"),
            0U);
  EXPECT_EQ(count_occurrences(root_c_make + "\n" + modeling,
                              "thirdparty/openmesh/openmesh-"),
            0U);
  EXPECT_EQ(count_occurrences(options,
                              "openmesh/openmesh-${_QUADER_OPENMESH_COMMIT}"),
            1U);
  const std::string build_policy =
      root_c_make + "\n" + options + "\n" + modeling;
  for (const std::string &needle : retired_modeling_test_framework_needles()) {
    expect_missing(build_policy, needle);
  }
}

TEST(BuildGuardrailTests,
     CrimsonRendererUsesPinnedSubmoduleAndNativeOnlyDependencies) {
  const std::string options =
      read_source_file("cmake/QuaderBuildOptions.cmake");
  const std::string composition =
      read_source_file("cmake/QuaderCrimsonComposition.cmake");
  const std::string submodules = read_source_file(".gitmodules");
  const std::string presets = read_source_file("CMakePresets.json");
  const std::string integration = read_source_file("thirdparty/crimson/README.md");
  const std::string manifest = read_source_file("vcpkg.json");
  const std::string native_dependencies =
      source_slice(manifest, "\"native-app\": {", "\"tests\": {");
  const std::string portable_dependencies =
      source_slice(manifest, "\"dependencies\":", "\"default-features\":");

  expect_contains(submodules, "path = thirdparty/crimson-renderer");
  expect_contains(submodules,
                  "url = https://github.com/qvertexworks/crimson-renderer.git");
  expect_contains(options,
                  "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/crimson-renderer");
  expect_missing(options + composition, "_CRIMSON_ENGINE");
  expect_missing(composition, "src/crimson-renderer/shared");
  expect_contains(composition,
                  "include(\"${QUADER_CRIMSON_SOURCE_DIR}/cmake/CrimsonProvenance.cmake\")");
  expect_contains(composition,
                  "crimson_validate_consumer_provenance(\"${CMAKE_CURRENT_SOURCE_DIR}\"");
  expect_contains(composition,
                  "\"thirdparty/crimson-renderer\" \"${QUADER_CRIMSON_SOURCE_DIR}\"");
  expect_contains(composition, "\"${_quader_crimson_dependency_prefix}\")");
  expect_contains(integration, "renderer-manifest.json");
  expect_contains(integration, "SPDX");
  expect_contains(composition, "target_link_libraries(quader_render_crimson");
  expect_contains(composition, "PUBLIC quader_render_common Crimson::Renderer");
  expect_missing(composition, "FetchContent");
  expect_missing(composition, "git clone");
  expect_contains(presets,
                  "\"VCPKG_OVERLAY_PORTS\": \"${sourceDir}/thirdparty/crimson-renderer/cmake/vcpkg-ports\"");
  for (const std::string &dependency :
       {"bgfx", "meshoptimizer", "vulkan-headers", "nlohmann-json"}) {
    expect_contains(native_dependencies, dependency);
    expect_missing(portable_dependencies, dependency);
  }
  expect_contains(native_dependencies, "\"tools\"");
}

TEST(BuildGuardrailTests,
     CanonicalBuildTreeSeparatesDebugTracyFromRelease) {
  const std::string root_c_make = read_source_file("CMakeLists.txt");
  const std::string options =
      read_source_file("cmake/QuaderBuildOptions.cmake");
  const std::string modeling =
      read_source_file("cmake/QuaderModeling.cmake");
  const std::string presets = read_source_file("CMakePresets.json");
  const std::string launcher = read_source_file("tools/boot_tracy.ps1");
  const std::string native_build_contract =
      read_source_file("tools/native_build_contract.ps1");
  const std::string root_agents = read_source_file("AGENTS.md");

  EXPECT_EQ(count_occurrences(presets, "\"binaryDir\""), 1U);
  expect_contains(presets, "${sourceDir}/build/vs2026");
  expect_contains(presets, "\"name\": \"vs2026-debug\"");
  expect_contains(presets, "\"name\": \"vs2026-release\"");
  expect_contains(presets, "\"QUADER_ENABLE_TRACY\": \"ON\"");
  expect_contains(presets, "\"QUADER_ENABLE_SENTRY\": \"OFF\"");
  expect_contains(presets, "\"QUADER_TRACY_ON_DEMAND\": \"ON\"");
  expect_missing(presets, "Ninja");
  expect_missing(presets, "ninja-msvc");

  expect_contains(options, "set(QUADER_ENABLE_TRACY ON CACHE BOOL");
  expect_contains(options, "set(QUADER_TRACY_ON_DEMAND ON CACHE BOOL");
  expect_contains(options, "../_SOURCES/tracy-0.13.1");
  expect_contains(modeling,
                  "$<$<CONFIG:Debug>:QUADER_ENABLE_TRACY=1>");
  expect_contains(modeling, "$<$<CONFIG:Debug>:TRACY_ENABLE=1>");
  expect_contains(modeling, "$<$<CONFIG:Debug>:TRACY_ON_DEMAND=1>");
  expect_contains(
      modeling,
      "$<$<CONFIG:Debug>:${_quader_tracy_client_target}>");
  expect_missing(modeling,
                 "INTERFACE QUADER_ENABLE_TRACY=1 TRACY_ENABLE=1");

  expect_contains(root_c_make,
                  "set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)");
  expect_contains(root_c_make,
                  "set(CMAKE_MAP_IMPORTED_CONFIG_DEBUG \"Release;\")");
  expect_contains(root_c_make,
                  "set(CMAKE_MSVC_RUNTIME_LIBRARY \"MultiThreadedDLL\")");
  expect_contains(root_c_make, "$<$<CONFIG:Debug>:NDEBUG>");
  expect_contains(root_c_make, "$<$<CONFIG:Debug>:QT_NO_DEBUG>");
  expect_contains(root_c_make,
                  "$<$<CONFIG:Debug>:_ITERATOR_DEBUG_LEVEL=0>");
  expect_contains(root_c_make, "$<$<CONFIG:Debug>:/O2>");
  expect_contains(root_c_make, "$<$<CONFIG:Debug>:/Ob2>");
  expect_contains(root_c_make, "$<$<CONFIG:Debug>:/INCREMENTAL:NO>");
  expect_contains(root_c_make, "$<$<CONFIG:Debug>:/OPT:REF>");
  expect_contains(root_c_make, "$<$<CONFIG:Debug>:/OPT:ICF>");
  expect_contains(root_c_make, "$<$<CONFIG:Release>:/O2>");
  expect_contains(root_c_make, "$<$<CONFIG:Release>:/Ob3>");
  expect_contains(root_c_make, "$<$<CONFIG:Release>:/OPT:REF>");
  expect_contains(root_c_make, "$<$<CONFIG:Release>:/OPT:ICF>");
  expect_contains(root_c_make,
                  "${FILAMENT_SDK_ROOT}/lib/x86_64/md/${lib_name}.lib");
  expect_missing(root_c_make, "lib/x86_64/mdd");
  expect_missing(root_c_make, "matdbg.lib");
  expect_contains(root_c_make, "--release");
  expect_contains(root_c_make, "--force");
  expect_contains(root_c_make, "msbuild/applocal.ps1");
  expect_contains(
      root_c_make,
      "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin");
  expect_contains(root_c_make,
                  "Deploying the complete recursive Release runtime ");
  expect_contains(root_c_make, "_QUADER_STALE_RUNTIME_PATHS");
  expect_contains(root_c_make, "set(VCPKG_APPLOCAL_DEPS OFF)");
  expect_missing(root_c_make,
                 "--$<IF:$<CONFIG:Debug>,debug,release>");

  expect_contains(launcher,
                  "build\\vs2026\\Debug\\quader.exe");
  expect_contains(
      launcher,
      "@('--build', '--preset', 'vs2026-debug', '--target', 'quader')");
  expect_contains(launcher, "Assert-ProfilingBuildProject");
  expect_contains(launcher, "Assert-NativeExecutableLinkContract");
  expect_contains(launcher, "Assert-NativeRuntimeDeployment");
  expect_contains(launcher, "native_build_contract.ps1");
  expect_contains(launcher, "Assert-RuntimeDependencyClosure");
  expect_contains(native_build_contract, "Resolve-DumpbinPath");
  expect_contains(native_build_contract, "md4c.dll");
  expect_contains(native_build_contract, "libpng16.dll");
  expect_contains(native_build_contract, "z.dll");
  expect_contains(native_build_contract, "Qt6Widgetsd.dll");
  expect_contains(native_build_contract, "platforms\\qwindowsd.dll");
  expect_contains(native_build_contract, "vcpkg_installed\\x64-windows\\bin");
  expect_contains(native_build_contract, "Get-FileHash -Algorithm SHA256");
  expect_contains(native_build_contract, "QUADER_ENABLE_SENTRY:BOOL=OFF");
  expect_missing(launcher, "[string]$QuaderExe");
  expect_missing(launcher, "[string]$CMakeCache");
  expect_missing(launcher, "[ValidateSet('Release', 'Debug')]");
  expect_missing(launcher, "$Configuration = 'Release'");
  expect_missing(launcher, "vs2026-release");

  expect_contains(root_agents,
                  "build/vs2026/Debug/quader.exe");
  expect_contains(root_agents,
                  "build/vs2026/Release/quader.exe");
  expect_missing(root_agents,
                  "cmake --build --preset vs2026-debug --target quader");
  expect_contains(root_agents,
                  "cmake --build --preset vs2026-release --target quader");
  expect_contains(root_agents,
                  "Public distribution ships `quader-0.1.exe`");
}

TEST(BuildGuardrailTests,
     ProfileGTestTargetsDeployMappedRuntimeDllsWithoutAppCleanupLoss) {
  const std::string root_c_make = read_source_file("CMakeLists.txt");
  const std::string modeling =
      read_source_file("cmake/QuaderModeling.cmake");
  const std::string presets = read_source_file("CMakePresets.json");
  const std::string sdk_only_check =
      read_source_file("cmake/QuaderSdkOnlyBuildCheck.cmake");

  expect_contains(presets, "\"VCPKG_APPLOCAL_DEPS\": \"OFF\"");
  expect_contains(sdk_only_check, "-DVCPKG_APPLOCAL_DEPS=OFF");

  const std::string deployment_helper = source_slice(
      root_c_make,
      "function(quader_deploy_test_runtime_dlls target)",
      "endfunction()");
  expect_contains(deployment_helper,
                  "$<TARGET_RUNTIME_DLLS:${target}>");
  expect_contains(deployment_helper, "copy_if_different");
  expect_contains(deployment_helper, "COMMAND_EXPAND_LISTS");

  const std::string guardrail_target = source_slice(
      root_c_make, "add_executable(quader_guardrail_tests",
      "add_test(NAME quader_guardrail_tests");
  expect_contains(
      guardrail_target,
      "quader_deploy_test_runtime_dlls(quader_guardrail_tests)");

  const std::string app_target =
      source_slice(root_c_make, "add_executable(quader_tests",
                   "add_test(NAME quader_tests");
  expect_contains(app_target,
                  "quader_deploy_test_runtime_dlls(quader_tests)");

  const std::string parity_target = source_slice(
      root_c_make, "add_executable(quader_modeling_sdk_parity_tests",
      "add_test(NAME quader_modeling_sdk_parity_tests");
  expect_contains(
      parity_target,
      "quader_deploy_test_runtime_dlls("
      "quader_modeling_sdk_parity_tests)");

  const std::string domain_target_helper = source_slice(
      modeling,
      "function(quader_add_modeling_domain_gtest_target target)",
      "endfunction()");
  expect_contains(domain_target_helper,
                  "quader_deploy_test_runtime_dlls(${target})");

  const std::string loop_target_helper = source_slice(
      modeling,
      "function(quader_add_modeling_domain_loop_target target)",
      "endfunction()");
  expect_contains(loop_target_helper,
                  "quader_deploy_test_runtime_dlls(${target})");

  const std::string consumer_target = source_slice(
      modeling,
      "add_executable(quader_modeling_sdk_consumer_tests",
      "add_test(NAME quader_modeling_sdk_consumer_tests");
  expect_contains(
      consumer_target,
      "quader_deploy_test_runtime_dlls("
      "quader_modeling_sdk_consumer_tests)");

  const std::string stale_runtime_policy = source_slice(
      root_c_make, "set(_QUADER_VCPKG_RUNTIME_DLLS",
      "set(_QUADER_STALE_RUNTIME_PATHS)");
  expect_contains(stale_runtime_policy,
                  "list(REMOVE_ITEM _QUADER_VCPKG_RUNTIME_DLLS");
  expect_contains(stale_runtime_policy, "gtest.dll");
  expect_contains(stale_runtime_policy, "gtest_main.dll");
}

TEST(BuildGuardrailTests, RootQuaderTestsListsEveryRootTestSource) {
  const std::string root_c_make = read_source_file("CMakeLists.txt");
  const std::string modeling_c_make =
      read_source_file("cmake/QuaderModeling.cmake");
  const std::string crimson_composition_c_make =
      read_source_file("cmake/QuaderCrimsonComposition.cmake");
  const std::string sdk_consumer_c_make =
      read_source_file("tests/sdk_consumer/CMakeLists.txt");
  const std::string test_build_files =
      root_c_make + "\n" + modeling_c_make + "\n" + crimson_composition_c_make +
      "\n" + sdk_consumer_c_make;

  for (const std::filesystem::path &file :
       files_with_extension(source_root() / "tests", ".cpp")) {
    const std::string relative = normalized_relative(file);
    if (starts_with(relative, "tests/modeling_domain/") ||
        starts_with(relative, "tests/sdk_consumer/")) {
      continue;
    }

    EXPECT_NE(test_build_files.find(relative), std::string::npos) << relative;
  }
  expect_contains(sdk_consumer_c_make, "main.cpp");

  const std::string guardrail_target = source_slice(
      root_c_make, "add_executable(quader_guardrail_tests",
      "add_test(NAME quader_guardrail_tests COMMAND quader_guardrail_tests)");
  const std::string app_target =
      source_slice(root_c_make, "add_executable(quader_tests",
                   "add_test(NAME quader_tests COMMAND quader_tests)");

  expect_contains(root_c_make, "add_executable(quader_guardrail_tests");
  expect_contains(root_c_make, "target_link_libraries(quader_guardrail_tests "
                               "PRIVATE GTest::gtest GTest::gtest_main)");
  expect_contains(guardrail_target,
                  "tests/agent_instruction_routing_tests.cpp");
  expect_contains(guardrail_target, "tests/support/guardrail_file_scan.cpp");
  expect_missing(guardrail_target, "quader_runtime");
  expect_missing(app_target, "tests/agent_instruction_routing_tests.cpp");

  for (const std::filesystem::path &file :
       files_with_extension(source_root() / "tests", ".cpp")) {
    const std::string relative = normalized_relative(file);
    if (starts_with(relative, "tests/modeling_domain/") ||
        !ends_with(relative, "_guardrail_tests.cpp")) {
      continue;
    }

    expect_contains(guardrail_target, relative);
    expect_missing(app_target, relative);
  }
}

TEST(BuildGuardrailTests,
     QuaderTestsDisableIncrementalLinkOnlyForMsvcDebug) {
  const std::string root_c_make = read_source_file("CMakeLists.txt");
  const std::string app_target =
      source_slice(root_c_make, "add_executable(quader_tests",
                   "add_test(NAME quader_tests COMMAND quader_tests)");
  const std::string msvc_options =
      source_slice(app_target, "if(MSVC)", "endif()");

  expect_contains(msvc_options,
                  "target_compile_options(quader_tests PRIVATE /bigobj)");
  expect_contains(msvc_options,
                  "target_link_options(quader_tests PRIVATE\n"
                  "            \"$<$<CONFIG:Debug>:/INCREMENTAL:NO>\")");
  expect_missing(msvc_options, "CONFIG:Release");
  expect_missing(msvc_options, "CONFIG:RelWithDebInfo");
  EXPECT_EQ(count_occurrences(msvc_options, "/INCREMENTAL:NO"), 1U);
}

TEST(BuildGuardrailTests,
     OwnerTargetMapRejectsMonolithicCoreAndRuntimeSourceBlocks) {
  const std::string root_c_make = read_source_file("CMakeLists.txt");
  const std::string modeling_c_make =
      read_source_file("cmake/QuaderModeling.cmake");
  const std::string build_files = root_c_make + "\n" + modeling_c_make;

  const std::string core_sources =
      cmake_target_source_blocks(build_files, "quader_core");
  for (const std::string &forbidden : {
           "src/assets/",
           "src/editor/commands/",
           "src/editor/history/",
           "src/editor/selection/",
           "src/editor/tools/",
           "src/io/qdr/",
           "src/scene/",
           "src/ui/",
           "src/viewport/",
       }) {
    expect_missing(core_sources, forbidden);
  }

  const std::string runtime_sources =
      cmake_target_source_blocks(build_files, "quader_runtime");
  for (const std::string &forbidden : {
           "src/app/",
           "src/api/quader_editor_api.cpp",
           "src/api/quader_render_api.cpp",
           "src/editor/transform/",
           "src/editor/viewport/",
           "src/platform/",
           "src/render/",
           "src/ui/",
       }) {
    expect_missing(runtime_sources, forbidden);
  }
}

TEST(BuildGuardrailTests, ProjectOwnedCppFilesUseSnakeCaseCppAndHppNames) {
  for (const std::filesystem::path &relative_path : project_owned_cpp_files()) {
    EXPECT_TRUE(is_snake_case_file_stem(relative_path))
        << relative_path.generic_string();
    EXPECT_TRUE(relative_path.extension() == ".cpp" ||
                relative_path.extension() == ".hpp")
        << relative_path.generic_string();
  }
}

TEST(BuildGuardrailTests,
     ProjectOwnedFilesDoNotContainBrokenHeaderExtensionTypos) {
  const std::string broken_header_extension = joined(".hpp", "pp");
  const std::string broken_source_extension = joined(".cpp", "pp");
  const std::vector<std::string> broken_identifier_needles = {
      joined("hpp", "ard"), joined("hpp", "as"), joined("hpp", "istory"),
      joined("cpp", "ard"), joined("cpp", "as"), joined("cpp", "istory"),
  };

  for (const std::filesystem::path &relative_path :
       project_owned_comment_capable_files()) {
    const std::string source = read_source_file(relative_path);
    EXPECT_EQ(source.find(broken_header_extension), std::string::npos)
        << relative_path.generic_string();
    EXPECT_EQ(source.find(broken_source_extension), std::string::npos)
        << relative_path.generic_string();
    for (const std::string &needle : broken_identifier_needles) {
      EXPECT_EQ(source.find(needle), std::string::npos)
          << relative_path.generic_string();
    }
  }

}

TEST(BuildGuardrailTests, TemporaryRenameFilesDoNotRemain) {
  for (const std::filesystem::path &relative_path :
       project_owned_recursive_files()) {
    EXPECT_EQ(relative_path.filename().string().find(".__quader_rename_"),
              std::string::npos)
        << relative_path.generic_string();
  }
}

TEST(BuildGuardrailTests, ProjectOwnedTextDoesNotReferenceOldCppPathNames) {
  const std::vector<std::filesystem::path> files =
      project_owned_comment_capable_files();

  for (const std::filesystem::path &relative_path : files) {
    EXPECT_FALSE(
        contains_stale_project_cpp_path(read_source_file(relative_path)))
        << relative_path.generic_string();
  }
}

TEST(BuildGuardrailTests, TestsUseOnlyGoogleTest) {
  for (const std::filesystem::path &file :
       files_with_extension(source_root() / "tests", ".cpp")) {
    const std::string relative = normalized_relative(file);
    const std::string source = read_source_file(relative);
    for (const std::string &needle :
         retired_modeling_test_framework_needles()) {
      expect_missing(source, needle);
    }
  }
  for (const std::filesystem::path &file :
       files_with_extension(source_root() / "tests", ".hpp")) {
    const std::string relative = normalized_relative(file);
    const std::string source = read_source_file(relative);
    for (const std::string &needle :
         retired_modeling_test_framework_needles()) {
      expect_missing(source, needle);
    }
  }
}

TEST(BuildGuardrailTests,
     ProjectOwnedFilesDoNotReferenceRetiredModelingTestFramework) {
  std::vector<std::filesystem::path> files =
      project_owned_comment_capable_files();
  files.push_back("memory.md");
  files.push_back("thirdparty/THIRD_PARTY_MANIFEST.md");

  for (const std::filesystem::path &relative_path : files) {
    if (!std::filesystem::exists(source_path(relative_path))) {
      continue;
    }
    const std::string source = read_source_file(relative_path);
    for (const std::string &needle :
         retired_modeling_test_framework_needles()) {
      EXPECT_EQ(find_retired_framework_reference(source, needle),
                std::string::npos)
          << relative_path.generic_string() << " contains " << needle;
    }
  }
}

TEST(BuildGuardrailTests, RetiredFrameworkMacrosRequireAnIdentifierBoundary) {
  const std::string macro = joined("CHECK", "(");
  EXPECT_EQ(find_retired_framework_reference("QT_VERSION_" + macro + "6, 8, 0)",
                                             macro),
            std::string::npos);
  EXPECT_EQ(find_retired_framework_reference("PRE" + macro + "value)", macro),
            std::string::npos);
  EXPECT_EQ(find_retired_framework_reference("1" + macro + "value)", macro),
            std::string::npos);
  EXPECT_NE(find_retired_framework_reference(macro + "value)", macro),
            std::string::npos);
  EXPECT_NE(find_retired_framework_reference("\n" + macro + "value)", macro),
            std::string::npos);
  EXPECT_NE(find_retired_framework_reference("QT_VERSION_" + macro +
                                                 "6, 8, 0);\n" + macro + "value)",
                                             macro),
            std::string::npos);
}

TEST(BuildGuardrailTests, CatalogGuardrailTestsDoNotMirrorProductionCatalogs) {
  const std::string modeling_catalog_tests =
      read_source_file("tests/modeling_tool_catalog_tests.cpp");
  const std::string viewport_catalog_tests =
      read_source_file("tests/viewport_tool_catalog_tests.cpp");
  const std::string modeling_domain_tool_contracts = read_source_file(
      "tests/modeling_domain/tool/"
      "qdr_modeling_runtime_tool_operation_contract_tests.cpp");

  expect_missing(modeling_catalog_tests, "ExpectedAction");
  expect_missing(modeling_catalog_tests, "expectedActions");
  expect_missing(modeling_catalog_tests, "expectedOperations");
  expect_contains(modeling_catalog_tests,
                  "modeling_instant_operation_catalog()");
  expect_contains(modeling_catalog_tests,
                  "quader_host_contract::action_catalog()");

  expect_missing(viewport_catalog_tests, "ExpectedTool");
  expect_missing(viewport_catalog_tests, "constexpr std::array");
  expect_contains(viewport_catalog_tests, "viewport_tool_descriptors()");

  expect_missing(modeling_domain_tool_contracts, "ExpectedTool");
  expect_missing(modeling_domain_tool_contracts, "ExpectedOperation");
  expect_missing(modeling_domain_tool_contracts, "constexpr std::array");
  expect_contains(modeling_domain_tool_contracts,
                  "qec::tool_registry().tools()");
  expect_contains(modeling_domain_tool_contracts,
                  "qec::operation_registry().entries()");
}

TEST(BuildGuardrailTests,
     ModelingDomainTestsUseCategorizedExplicitSourceRegistration) {
  const std::string modeling = read_source_file("cmake/QuaderModeling.cmake");

  expect_contains(
      modeling,
      "function(quader_add_modeling_domain_test_source category target)");
  expect_contains(modeling, "Unknown modeling-domain test category");
  expect_contains(modeling,
                  "function(quader_add_modeling_domain_loop_target target)");
  expect_contains(modeling,
                  "add_executable(${target} EXCLUDE_FROM_ALL ${ARGN})");
  expect_contains(modeling, "quader_modeling_domain_polygon_loop_tests");
  expect_contains(modeling, "quader_modeling_domain_runtime_loop_tests");
  expect_contains(modeling, "quader_modeling_domain_metadata_loop_tests");
  expect_contains(modeling, "add_test(NAME ${target} COMMAND ${target})");
  expect_missing(modeling,
                 "add_test(NAME quader_modeling_domain_polygon_loop_tests");
  expect_missing(modeling,
                 "add_test(NAME quader_modeling_domain_runtime_loop_tests");
  expect_missing(modeling,
                 "add_test(NAME quader_modeling_domain_metadata_loop_tests");
  expect_missing(modeling, "function(quader_add_polygon_domain_test");
  expect_missing(modeling, "function(quader_add_modeling_runtime_domain_test");
  expect_missing(modeling, "function(quader_add_editor_metadata_domain_test");
  expect_missing(modeling, "file(GLOB QUADER_POLYGON_OPERATION_TEST_SOURCES");
  expect_missing(modeling, "file(GLOB QUADER_RUNTIME_OPERATION_TEST_SOURCES");
  expect_missing(modeling, "file(GLOB QUADER_RUNTIME_TOOL_TEST_SOURCES");

  for (const std::filesystem::path &file :
       files_with_extension(source_root() / "tests/modeling_domain", ".cpp")) {
    const std::string relative = normalized_relative(file);
    EXPECT_NE(modeling.find(relative), std::string::npos) << relative;
  }

  const std::size_t openmesh_test = modeling.find(
      "tests/modeling_domain/qdr_document_openmesh_topology_query_tests.cpp");
  ASSERT_NE(openmesh_test, std::string::npos);
  const std::size_t openmesh_condition =
      modeling.rfind("if(QUADER_USE_OPENMESH_TOPOLOGY)", openmesh_test);
  ASSERT_NE(openmesh_condition, std::string::npos);
  const std::size_t openmesh_end = modeling.find("endif()", openmesh_condition);
  ASSERT_NE(openmesh_end, std::string::npos);
  EXPECT_LT(openmesh_condition, openmesh_test);
  EXPECT_LT(openmesh_test, openmesh_end);
}

TEST(BuildGuardrailTests, ArchitectureReviewNamesRealModelingDomainTargets) {
  const std::string review = read_source_file("architecture-review.md");
  const std::string root_c_make = read_source_file("CMakeLists.txt");
  const std::string modeling = read_source_file("cmake/QuaderModeling.cmake");
  const std::string crimson_composition =
      read_source_file("cmake/QuaderCrimsonComposition.cmake");
  const std::string build_files =
      root_c_make + "\n" + modeling + "\n" + crimson_composition;

  const std::regex target_pattern(R"(`(quader_[a-z0-9_]+)`)");
  for (std::sregex_iterator it(review.begin(), review.end(), target_pattern), end;
       it != end; ++it) {
    expect_contains(build_files, (*it)[1].str());
  }
  expect_contains(review, "quader::modeling_sdk");
}

TEST(BuildGuardrailTests, AppOwnedModelingSdkBoundaryIsAdditiveAndGuarded) {
  const std::string root_c_make = read_source_file("CMakeLists.txt");
  const std::string modeling_c_make =
      read_source_file("cmake/QuaderModeling.cmake");
  const std::string options =
      read_source_file("cmake/QuaderBuildOptions.cmake");
  const std::string build_files = root_c_make + "\n" + modeling_c_make;

  EXPECT_FALSE(std::filesystem::exists(source_path("sdk")));
  EXPECT_TRUE(
      std::filesystem::exists(source_path("src/sdk/core/modeling")));
  expect_contains(options, "option(QUADER_BUILD_NATIVE_APP");
  expect_contains(root_c_make, "if(QUADER_BUILD_NATIVE_APP)");
  expect_contains(build_files, "quader_modeling_sdk_geometry");
  expect_contains(build_files, "quader_modeling_sdk_polygon");
  expect_contains(build_files, "quader_modeling_sdk_qdr_document");
  expect_contains(build_files, "quader_modeling_sdk_runtime");
  expect_contains(build_files, "quader_modeling_sdk");
  expect_contains(build_files, "quader_modeling_sdk_consumer_tests");
  expect_contains(build_files, "quader_modeling_sdk_only_configure_build");
  expect_contains(build_files, "QUADER_SDK_CPP_EXAMPLES");
  expect_contains(build_files, "QUADER_MODELING_SDK_INCLUDE_DIR");
  expect_contains(build_files, "QuaderModelingSdkTargets.cmake");
  expect_missing(build_files, "QUADER_MODELING_SDK_DIR");
  expect_missing(build_files, "QUADER_BUILD_SDK_TESTS");
}

TEST(BuildGuardrailTests, QuaderTestsSandboxPerUserStorage) {
  const std::string root_c_make = read_source_file("CMakeLists.txt");
  const std::string test_main =
      read_source_file("tests/quader_tests_main.cpp");
  const std::string app_target =
      source_slice(root_c_make, "add_executable(quader_tests",
                   "add_test(NAME quader_tests COMMAND quader_tests)");

  expect_contains(app_target, "tests/quader_tests_main.cpp");
  expect_contains(app_target,
                  "target_link_libraries(quader_tests PRIVATE quader_runtime "
                  "quader_render_full_host GTest::gtest "
                  "quader_release_profile_full)");
  expect_missing(app_target, "GTest::gtest_main");
  expect_contains(test_main, "quader-tests-local-app-data-");
  expect_contains(test_main, "_putenv_s(\"LOCALAPPDATA\"");
  expect_contains(test_main, "const int test_result = RUN_ALL_TESTS()");
  expect_contains(test_main, "std::filesystem::remove_all(profile, error)");
}

TEST(BuildGuardrailTests,
     PortableModelingCoreIsTheOnlyCompiledPolygonAndQdrTruth) {
  const std::string modeling_c_make =
      read_source_file("cmake/QuaderModeling.cmake");

  expect_contains(modeling_c_make,
                  "add_library(quader_modeling_polygon STATIC "
                  "${QUADER_CANONICAL_POLYGON_SOURCES})");
  expect_contains(modeling_c_make,
                  "src/sdk/core/modeling/src/polygon_document.cpp\n"
                  "    ${QUADER_CANONICAL_POLYGON_SOURCES})");
  expect_contains(modeling_c_make,
                  "add_library(quader_modeling_sdk_qdr_document STATIC\n"
                  "    src/sdk/core/modeling/src/qdr_document.cpp)");
  expect_missing(modeling_c_make, "quader_qdr_shared_io");
  expect_missing(modeling_c_make, "quader_qdr_document_io");
  expect_missing(modeling_c_make, "quader_qdr_runtime_io");
  expect_missing(modeling_c_make, "QUADER_CANONICAL_QDR_");
  expect_missing(modeling_c_make, "src/mesh/polygon/*.cpp");
  expect_missing(modeling_c_make, "src/mesh/geometry/geometry.cpp");

  EXPECT_FALSE(std::filesystem::exists(source_path("src/mesh")));
  EXPECT_TRUE(std::filesystem::exists(
      source_path("src/io/qdr/qdr_atomic_write.cpp")));
  for (std::string_view retired_json_path : {
           "src/io/qdr/qdr_document.hpp",
           "src/io/qdr/qdr_format.hpp",
           "src/io/qdr/qdr_polygon_document_codec.hpp",
           "src/io/qdr/qdr_project_codecs.cpp",
           "src/io/qdr/qdr_project_codecs.hpp",
           "src/io/qdr/qdr_root.hpp",
           "src/io/qdr/qdr_runtime_io_compat.cpp",
           "src/io/qdr/qdr_runtime_io_compat.hpp",
           "src/sdk/core/modeling/internal/thirdparty/nlohmann/json.hpp",
       }) {
    EXPECT_FALSE(std::filesystem::exists(source_path(retired_json_path)))
        << retired_json_path;
  }
  const std::filesystem::path retired_sdk_qdr =
      source_path("src/sdk/core/modeling/internal/io/qdr");
  EXPECT_TRUE(files_with_extension(retired_sdk_qdr, ".cpp").empty());
  EXPECT_TRUE(files_with_extension(retired_sdk_qdr, ".hpp").empty());

  const std::filesystem::path neutral_mesh = source_path(
      "src/sdk/core/modeling/neutral/include/quader/modeling/neutral");
  const std::filesystem::path implementation_mesh =
      source_path("src/sdk/core/modeling/internal/mesh");
  EXPECT_EQ(files_with_extension(neutral_mesh, ".hpp").size(), 37U);
  EXPECT_TRUE(files_with_extension(implementation_mesh, ".hpp").empty());

  const std::string retired_include = joined("#include <me", "sh/");
  const std::string retired_quoted_include = joined("#include \"me", "sh/");
  const std::string neutral_internal_include =
      joined("quader/modeling/neutral/polygon/", "internal/");
  constexpr std::array<std::string_view, 11> kNeutralInternalConsumers = {
      "src/editor/interactions/knife_tool.cpp",
      "src/editor/modeling/runtime/session_picking.cpp",
      "src/editor/modeling/runtime/session_selection_overlay.cpp",
      "src/editor/projection/mesh_projection_service.cpp",
      "tests/mesh_quad_uv_interpolation_tests.cpp",
      "tests/modeling_domain/"
      "qdr_document_openmesh_topology_query_tests.cpp",
      "tests/modeling_domain/qdr_poly_geometry_characterization_tests.cpp",
      "tests/modeling_domain/qdr_document_mesh_uv_tests.cpp",
      "tests/modeling_domain/operation/qdr_document_operation_bevel_edges_tests.cpp",
      "tests/modeling_domain/operation/"
      "qdr_document_operation_normal_shading_tests.cpp",
      "tests/modeling_domain/tool/"
      "qdr_modeling_runtime_tool_knife_tests.cpp",
  };
  for (std::string_view root : {"src", "tests", "examples", "benchmarks"}) {
    for (const std::filesystem::path &file :
         source_files_under(source_path(std::string(root)))) {
      const std::string relative = normalized_relative(file);
      const std::string source = read_file(file);
      EXPECT_EQ(source.find(retired_include), std::string::npos) << relative;
      EXPECT_EQ(source.find(retired_quoted_include), std::string::npos)
          << relative;
      if (!starts_with(relative, "src/sdk/core/modeling/") &&
          source.find(neutral_internal_include) != std::string::npos) {
        EXPECT_NE(std::find(kNeutralInternalConsumers.begin(),
                            kNeutralInternalConsumers.end(), relative),
                  kNeutralInternalConsumers.end())
            << relative;
      }
    }
  }
  for (const std::string_view consumer : kNeutralInternalConsumers) {
    EXPECT_NE(read_source_file(consumer).find(neutral_internal_include),
              std::string::npos)
        << consumer;
  }

  expect_contains(modeling_c_make, "QUADER_MODELING_SDK_NEUTRAL_INCLUDE_DIR");
  expect_missing(modeling_c_make,
                 "install(DIRECTORY \"${QUADER_MODELING_SDK_NEUTRAL_INCLUDE_DIR}");

  const std::string sdk_qdr_header = read_source_file(
      "src/sdk/core/modeling/include/quader/modeling/io/qdr_document.hpp");
  const std::string sdk_qdr_source =
      read_source_file("src/sdk/core/modeling/src/qdr_document.cpp");
  expect_contains(sdk_qdr_header, "Result<std::vector<std::byte>>");
  expect_contains(sdk_qdr_header, "std::span<const std::byte>");
  expect_missing(sdk_qdr_header, "std::string serialize_qdr");
  expect_missing(sdk_qdr_source, "nlohmann");
  expect_missing(sdk_qdr_source, "legacy_session");
}

TEST(BuildGuardrailTests,
     ModelingSdkInstallExportMatchesExternalConsumerTarget) {
  const std::string modeling_c_make =
      read_source_file("cmake/QuaderModeling.cmake");
  const std::string sdk_only_check =
      read_source_file("cmake/QuaderSdkOnlyBuildCheck.cmake");
  const std::string external_consumer =
      read_source_file("tests/sdk_consumer/CMakeLists.txt");

  for (const std::string &export_name :
       {"modeling_sdk_geometry", "modeling_sdk_polygon",
        "modeling_sdk_qdr_document", "modeling_sdk_runtime",
        "modeling_sdk"}) {
    expect_contains(modeling_c_make, "EXPORT_NAME " + export_name);
  }
  expect_contains(external_consumer, "quader::modeling_sdk");
  expect_contains(external_consumer,
                  "MSVC_RUNTIME_LIBRARY \"MultiThreadedDLL\"");
  expect_contains(external_consumer, "$<$<CONFIG:Debug>:NDEBUG>");
  expect_contains(
      external_consumer,
      "$<$<CONFIG:Debug>:_ITERATOR_DEBUG_LEVEL=0>");
  expect_contains(modeling_c_make,
                  "add_test(NAME quader_modeling_sdk_only_configure_build");
  expect_contains(modeling_c_make,
                  "-DQUADER_CXX_COMPILER=${CMAKE_CXX_COMPILER}");
  expect_contains(sdk_only_check,
                  "-DCMAKE_CXX_COMPILER=${QUADER_CXX_COMPILER}");
  expect_contains(sdk_only_check, "--install");
  expect_contains(sdk_only_check, "tests/sdk_consumer");
  expect_contains(sdk_only_check,
                  "-DCMAKE_PREFIX_PATH=${_quader_sdk_install_dir}");
  expect_contains(sdk_only_check,
                  "-DCMAKE_CONFIGURATION_TYPES=${QUADER_BUILD_CONFIG}");
  expect_contains(sdk_only_check,
                  "--target quader_modeling_sdk_external_consumer");
}

TEST(BuildGuardrailTests,
     ModelingSdkOnlyManifestFeatureExcludesNativeHostDependencies) {
  const std::string manifest = read_source_file("vcpkg.json");
  const std::string sdk_only_check =
      read_source_file("cmake/QuaderSdkOnlyBuildCheck.cmake");
  const std::string native_feature =
      source_slice(manifest, "\"native-app\": {", "\"tests\": {");
  const std::string tests_feature =
      source_slice(manifest, "\"tests\": {", "\n    }\n  }\n}");

  expect_contains(manifest, "\"dependencies\": []");
  expect_contains(manifest, "\"default-features\": [");
  expect_contains(manifest, "\"native-app\"");
  expect_contains(manifest, "\"tests\"");
  for (std::string_view native_only_dependency : {
           "entt", "flatbuffers", "glm", "lodepng", "qtbase",
           "qtimageformats", "sentry-native", "spdlog", "stduuid", "tiff",
           "zstd", "zlib",
       }) {
    expect_contains(native_feature,
                    "\"" + std::string(native_only_dependency) + "\"");
    expect_missing(tests_feature,
                   "\"" + std::string(native_only_dependency) + "\"");
  }
  expect_contains(tests_feature, "\"gtest\"");

  expect_contains(sdk_only_check,
                  "-DVCPKG_MANIFEST_NO_DEFAULT_FEATURES=ON");
  expect_contains(sdk_only_check, "-DVCPKG_MANIFEST_FEATURES=tests");
  expect_contains(sdk_only_check,
                  "SDK-only configure installed native app package");
  for (std::string_view native_codec_package : {
           "libjpeg-turbo", "libwebp", "qtimageformats", "tiff",
       }) {
    expect_contains(sdk_only_check, std::string(native_codec_package));
  }
}

TEST(BuildGuardrailTests,
     ModelingSdkOnlyGraphExcludesNativeModelingTargets) {
  const std::string modeling =
      read_source_file("cmake/QuaderModeling.cmake");
  const std::string sdk_only_check =
      read_source_file("cmake/QuaderSdkOnlyBuildCheck.cmake");
  const std::string options =
      read_source_file("cmake/QuaderBuildOptions.cmake");

  const std::string native_modeling_marker =
      "if(QUADER_BUILD_NATIVE_APP)\n"
      "add_library(quader_modeling_profile INTERFACE)";
  const std::string sdk_marker =
      "function(quader_configure_modeling_sdk_target target)";
  const std::string native_test_helpers_marker =
      "if(QUADER_BUILD_NATIVE_APP)\n"
      "set(QUADER_MODELING_DOMAIN_TEST_CATEGORIES";
  const std::string native_test_targets_marker =
      "if(QUADER_BUILD_TESTS AND QUADER_BUILD_NATIVE_APP)";
  const std::string sdk_tests_marker =
      "if(QUADER_BUILD_TESTS)\n"
      "    find_package(GTest CONFIG REQUIRED)\n"
      "    add_executable(quader_modeling_sdk_consumer_tests";

  const std::size_t native_modeling_begin =
      modeling.find(native_modeling_marker);
  const std::size_t sdk_begin = modeling.find(sdk_marker);
  const std::size_t native_test_helpers_begin =
      modeling.find(native_test_helpers_marker);
  const std::size_t native_test_targets_begin =
      modeling.find(native_test_targets_marker);
  const std::size_t sdk_tests_begin = modeling.find(sdk_tests_marker);
  ASSERT_NE(native_modeling_begin, std::string::npos);
  ASSERT_NE(sdk_begin, std::string::npos);
  ASSERT_NE(native_test_helpers_begin, std::string::npos);
  ASSERT_NE(native_test_targets_begin, std::string::npos);
  ASSERT_NE(sdk_tests_begin, std::string::npos);
  ASSERT_LT(native_modeling_begin, sdk_begin);
  ASSERT_LT(sdk_begin, native_test_helpers_begin);
  ASSERT_LT(native_test_helpers_begin, native_test_targets_begin);
  ASSERT_LT(native_test_targets_begin, sdk_tests_begin);

  const std::string native_modeling = modeling.substr(
      native_modeling_begin, sdk_begin - native_modeling_begin);
  for (const std::string_view native_target : {
           "quader_modeling_profile", "quader_geometry",
           "quader_modeling_polygon", "quader_modeling_runtime",
           "quader_editor_metadata", "OpenMeshCore",
       }) {
    expect_contains(native_modeling, std::string(native_target));
  }
  expect_contains(native_modeling, "if(QUADER_USE_OPENMESH_TOPOLOGY)");
  expect_missing(native_modeling, "quader_modeling_sdk_geometry");

  const std::string native_test_helpers = modeling.substr(
      native_test_helpers_begin,
      native_test_targets_begin - native_test_helpers_begin);
  expect_contains(native_test_helpers,
                  "function(quader_add_modeling_domain_test_source");
  expect_contains(native_test_helpers,
                  "function(quader_add_modeling_domain_gtest_target");
  expect_contains(native_test_helpers,
                  "function(quader_add_modeling_domain_loop_target");

  const std::string native_test_targets = modeling.substr(
      native_test_targets_begin, sdk_tests_begin - native_test_targets_begin);
  expect_contains(native_test_targets, "quader_modeling_domain_tests");
  expect_contains(native_test_targets,
                  "quader_modeling_domain_polygon_loop_tests");
  expect_contains(native_test_targets,
                  "quader_modeling_domain_runtime_loop_tests");
  expect_contains(native_test_targets,
                  "quader_modeling_domain_metadata_loop_tests");
  expect_missing(native_test_targets, "quader_modeling_sdk_consumer_tests");

  const std::string sdk_tests = modeling.substr(sdk_tests_begin);
  expect_contains(sdk_tests, "quader_modeling_sdk_consumer_tests");
  expect_contains(sdk_tests, "quader_modeling_sdk_only_configure_build");
  expect_missing(sdk_tests, "quader_modeling_domain_tests");
  expect_missing(sdk_tests, "QUADER_OPENMESH_SOURCE_DIR");
  expect_contains(
      modeling,
      "if(QUADER_BUILD_NATIVE_APP AND QUADER_BUILD_PERFORMANCE_BENCHMARKS)");

  expect_contains(options,
                  "option(QUADER_BUILD_NATIVE_APP \"Build the native Quader "
                  "Windows editor app.\" ON)");
  expect_contains(
      options,
      "option(QUADER_USE_OPENMESH_TOPOLOGY \"Use OpenMesh for "
      "topology-backed polygon edit operations.\" ON)");
  expect_missing(options, "QUADER_BUILD_NATIVE_MODELING");
  expect_missing(modeling, "QUADER_BUILD_NATIVE_MODELING");

  expect_contains(sdk_only_check, "-DQUADER_BUILD_NATIVE_APP=OFF");
  expect_contains(sdk_only_check, "-DQUADER_USE_OPENMESH_TOPOLOGY=OFF");
  expect_contains(sdk_only_check,
                  "-DQUADER_ALLOW_EXTERNAL_OPENMESH_FETCH=OFF");
  expect_contains(sdk_only_check, "-DQUADER_OPENMESH_SOURCE_DIR=");
  expect_missing(
      sdk_only_check,
      "-DQUADER_OPENMESH_SOURCE_DIR=${QUADER_OPENMESH_SOURCE_DIR}");
  expect_contains(sdk_only_check, "codemodel-v2");
  expect_contains(sdk_only_check, "sdk-only-targets.txt");
  expect_contains(sdk_only_check, "^OpenMesh");
  expect_contains(sdk_only_check, "^quader_modeling_domain_");
  for (const std::string_view forbidden_target : {
           "quader_geometry", "quader_modeling_profile",
           "quader_modeling_polygon", "quader_modeling_runtime",
           "quader_editor_metadata",
       }) {
    expect_contains(sdk_only_check, std::string(forbidden_target));
  }
  expect_contains(sdk_only_check, "quader_guardrail_tests");
  expect_contains(
      sdk_only_check,
      "^(quader_guardrail_tests|quader_modeling_sdk_consumer_tests)$");
  expect_contains(sdk_only_check,
                  "quader_modeling_sdk_only_configure_build");
}

TEST(BuildGuardrailTests, FatalDiagnosticsHelperStaysIsolatedAndBuiltWithHosts) {
  const std::string cmake = read_source_file("CMakeLists.txt");
  const std::string helper_target = source_slice(
      cmake, "add_executable(quader_fatal_diagnostics",
      "target_link_libraries(quader_fatal_diagnostics PRIVATE Dbghelp)");
  expect_contains(helper_target, "src/platform/fatal_diagnostics_main.cpp");
  expect_contains(helper_target,
                  "target_link_libraries(quader_fatal_diagnostics PRIVATE Dbghelp)");
  for (const char *dependency : {"quader_core", "quader_platform", "quader_runtime",
                                 "tracy", "Tracy", "Qt6::", "sentry"}) {
    expect_missing(helper_target, dependency);
  }
  expect_contains(cmake, "add_dependencies(quader\n    quader_fatal_diagnostics");
  expect_contains(cmake, "add_dependencies(quader_tests quader_runtime_resources "
                         "quader_fatal_diagnostics)");
  expect_contains(read_source_file("cmake/QuaderSdkOnlyBuildCheck.cmake"),
                  "_quader_sdk_only_target STREQUAL \"quader_fatal_diagnostics\"");
  const std::string parent =
      read_source_file("src/platform/fatal_error_handler.cpp");
  for (const char *dbghelp : {"<DbgHelp.h>", "MiniDumpWriteDump(", "SymInitialize(",
                              "SymFromAddr(", "StackWalk64("}) {
    expect_missing(parent, dbghelp);
  }
}

TEST(BuildGuardrailTests, ModelingSdkInstallAndConsumerUseRequestedConfiguration) {
  const std::string sdk_only_check =
      read_source_file("cmake/QuaderSdkOnlyBuildCheck.cmake");
  expect_contains(sdk_only_check,
                  "-DCMAKE_BUILD_TYPE=${QUADER_BUILD_CONFIG}");
  expect_contains(sdk_only_check,
                  "-DCMAKE_CONFIGURATION_TYPES=${QUADER_BUILD_CONFIG}");
  for (const std::string_view configure_command : {
           "set(_quader_configure_command",
           "set(_quader_external_consumer_configure_command",
       }) {
    const std::string configure = source_slice(
        sdk_only_check, configure_command, "execute_process(");
    expect_contains(configure, "${_quader_configuration_args}");
  }
}

TEST(BuildGuardrailTests, NativeDependencyToolsValidateSourceAndDeployment) {
  const std::string filament_sha256 = joined(
      "f7f215b087dc30c4a070a9fbb427f8e0",
      "c360058f14b068e40115d6d7b4c5d336");
  const std::string third_party_manifest =
      read_source_file("thirdparty/THIRD_PARTY_MANIFEST.md");
  const std::string filament_pin = read_source_file(
      "thirdparty/filament/filament-main-d1ccd94d8cd1-quader-vsync10.json");
  const std::string filament_builder = read_source_file("tools/build_filament_sdk.ps1");
  const std::string tracy_pin = read_source_file("thirdparty/tracy-0.13.1.json");
  const std::string native_verifier = read_source_file("tools/verify_native_build.ps1");

  expect_contains(filament_pin, filament_sha256);
  expect_contains(filament_pin, "\"upstream_ref\": \"main\"");
  expect_contains(filament_pin,
                  "\"upstream_commit\": \"" +
                      joined("d1ccd94d8cd152a7a5d8", "933d75d2c1d03d99d185") + "\"");
  expect_contains(filament_builder, "if (-not $AllowDownload)");
  expect_contains(filament_builder, "Get-FileHash -LiteralPath $archive -Algorithm SHA256");
  expect_contains(filament_builder, "if ($sourceSha -ne $pin.source_sha256)");
  expect_contains(filament_builder,
                  "$env:GIT_CEILING_DIRECTORIES = Split-Path -Parent $sourceRoot");
  expect_contains(filament_builder, "$env:GIT_CEILING_DIRECTORIES = $savedCeiling");
  expect_contains(filament_builder, "Invoke-FilamentPatch -Arguments @($patchPath)");
  expect_contains(tracy_pin,
                  joined("05cceee0df3b8d7c6fa87e9638af31", "1dbabc63cb"));
  expect_contains(tracy_pin, "licenses/tracy-BSD-3-Clause.txt");
  expect_contains(native_verifier, "[ValidateSet('Debug', 'Release')]");
  expect_contains(native_verifier, "[string[]]$Configuration = @('Release')");
  expect_contains(native_verifier, "foreach ($configurationName in $Configuration)");
  expect_contains(native_verifier, "Assert-NativeRuntimeDeployment");
  expect_contains(native_verifier, "Assert-RuntimeDependencyClosure");
  expect_contains(native_verifier, "Assert-ReleaseBuildProject");
  expect_contains(native_verifier, "Assert-ExecutableDebugDirectory");
  expect_contains(third_party_manifest, "## Filament Artifact Policy");
  expect_contains(third_party_manifest, filament_sha256);
}

TEST(BuildGuardrailTests, NativeBuildVerifierRejectsInstrumentationAndSymbolDrift) {
#if defined(_WIN32)
  const std::string command =
      "powershell -NoProfile -ExecutionPolicy Bypass -File " +
      quoted_process_argument(source_path("tools/test_native_build_contract.ps1"));
  const CommandResult result = run_command_capture(command);
  ASSERT_EQ(result.exit_code, 0) << result.output;
  expect_contains(result.output, "Native contract positive and negative fixtures passed.");
#else
  GTEST_SKIP() << "Native PE/MSVC tooling contract is exercised on Windows.";
#endif
}

TEST(BuildGuardrailTests, FilamentPatchAppliesInsideAncestorWorktree) {
#if defined(_WIN32)
  const std::string command =
      "powershell -NoProfile -ExecutionPolicy Bypass -File " +
      quoted_process_argument(source_path("tools/test_filament_patch.ps1"));
  const CommandResult result = run_command_capture(command);
  ASSERT_EQ(result.exit_code, 0) << result.output;
  expect_contains(result.output, "Filament patch applies inside an ancestor worktree");
#else
  GTEST_SKIP() << "PowerShell dependency staging is exercised on Windows.";
#endif
}

TEST(BuildGuardrailTests, FilamentPresentationPatchPreservesDitheringAndTargetRouting) {
  const std::string pin = read_source_file(
      "thirdparty/filament/filament-main-d1ccd94d8cd1-quader-vsync10.json");
  const std::string patch = read_source_file(
      "thirdparty/filament/filament-main-d1ccd94d8cd1-quader-vsync10.patch");
  expect_contains(pin, "\"patch_version\": 10");
  expect_contains(patch, "#define FILAMENT_QUADER_VSYNC_PATCH_VERSION 10");
  // Preserve upstream dither amplitude. Native GPU tests verify the resulting pixels;
  // this contract prevents a dependency update from replacing the presentation fix
  // with noise suppression or applying its decode to an unrelated render target.
  expect_missing(patch, "diff --git a/shaders/src/inline_dithering.fs");
  expect_contains(patch, "hasColorGrading && colorGrading->isOutputSRGB()");
  expect_contains(patch, "viewRenderTarget == mRenderTargetHandle && mSwapChain");
  expect_contains(patch, "mSwapChain->getFlags() & SwapChain::CONFIG_SRGB_COLORSPACE");
  expect_contains(patch, "needsSrgbPresentationDecode, needsAlphaChannel");
  expect_contains(patch, "RcasMode::ALPHA_PASSTHROUGH");
  expect_contains(patch, "inputPremultiplied ? postProcess.color.a : 1.0");
  expect_contains(patch, "linearColor * alpha, alpha");
}

TEST(BuildGuardrailTests,
     BinaryQdrDependenciesStayNativePrivateAndSchemaGenerated) {
  const std::string root_c_make = read_source_file("CMakeLists.txt");
  const std::string manifest = read_source_file("vcpkg.json");
  const std::string native_feature =
      source_slice(manifest, "\"native-app\": {", "\"tests\": {");
  const std::string tests_feature =
      source_slice(manifest, "\"tests\": {", "\n    }\n  }\n}");
  const std::string qdr_target = source_slice(
      root_c_make, "add_library(quader_project_qdr_io STATIC",
      "add_library(quader_project_session STATIC");
  const std::string generated_include = source_slice(
      qdr_target, "target_include_directories(quader_project_qdr_io",
      "target_compile_definitions(quader_project_qdr_io");
  const std::string qdr_links = source_slice(
      qdr_target, "target_link_libraries(quader_project_qdr_io",
      "add_library(quader_project_session STATIC");
  const std::string qdr_public_links =
      source_slice(qdr_links, "PUBLIC", "PRIVATE");

  expect_contains(root_c_make, "find_package(flatbuffers CONFIG REQUIRED)");
  expect_contains(root_c_make, "find_package(zstd CONFIG REQUIRED)");
  expect_contains(root_c_make,
                  "COMMAND flatbuffers::flatc --cpp --scoped-enums");
  expect_contains(root_c_make,
                  "src/io/qdr/schema/qdr_scene.fbs");
  expect_contains(qdr_target,
                  "add_dependencies(quader_project_qdr_io "
                  "quader_qdr_flatbuffers_generated)");
  expect_contains(generated_include, "PRIVATE");
  expect_contains(generated_include, "${QUADER_QDR_GENERATED_DIR}");
  expect_missing(generated_include, "PUBLIC");
  expect_contains(qdr_links, "flatbuffers::flatbuffers");
  expect_contains(qdr_links, "${QUADER_ZSTD_TARGET}");
  expect_missing(qdr_public_links, "flatbuffers::flatbuffers");
  expect_missing(qdr_public_links, "${QUADER_ZSTD_TARGET}");
  expect_contains(native_feature, "\"flatbuffers\"");
  expect_contains(native_feature, "\"zstd\"");
  expect_missing(tests_feature, "\"flatbuffers\"");
  expect_missing(tests_feature, "\"zstd\"");
}

TEST(BuildGuardrailTests, ModelingSdkPublicHeadersStayNativeOwnerFree) {
  const std::vector<std::string> banned_needles = {
      "SceneDocument",
      "SceneContentDocument",
      "SceneSession",
      "AssetRegistry",
      "ModelingSessionService",
      "SceneRenderBridge",
      "ViewportOverlayFrame",
      "QuaderEditorApi",
      "SDL",
      "ImGui",
      "filament",
      "entt",
      "flatbuffers",
      "nlohmann",
      "zstd",
      joined("#include <me", "sh/"),
      joined("#include \"me", "sh/"),
      "#include <io/qdr/",
      "#include \"io/qdr/",
      "#include <assets/",
      "#include \"assets/",
      "#include <scene/",
      "#include \"scene/",
      "#include <render/",
      "#include \"render/",
      "#include <ui/",
      "#include \"ui/",
      "#include <platform/",
      "#include \"platform/",
      "#include <api/",
      "#include \"api/",
  };

  for (const std::filesystem::path &file :
       files_with_extension(source_root() / "src/sdk/core/modeling/include",
                            ".hpp")) {
    const std::string source = read_file(file);
    for (const std::string &needle : banned_needles) {
      EXPECT_EQ(source.find(needle), std::string::npos)
          << normalized_relative(file) << " contains " << needle;
    }
  }
}

TEST(BuildGuardrailTests, DependencyCommitLiteralsStayInDependencyOwnerFiles) {
  const std::vector<std::filesystem::path> allowed_commit_owner_files = {
      "cmake/QuaderBuildOptions.cmake",
      "thirdparty/THIRD_PARTY_MANIFEST.md",
      // Pinned source citations document input behavior; they are not dependency pins.
      "src/ui/NUMERIC_INPUTS.md",
  };

  std::vector<std::filesystem::path> files =
      project_owned_comment_capable_files();
  files.push_back("thirdparty/THIRD_PARTY_MANIFEST.md");
  for (const std::filesystem::path &relative_path : files) {
    if (std::find(allowed_commit_owner_files.begin(),
                  allowed_commit_owner_files.end(),
                  relative_path) != allowed_commit_owner_files.end()) {
      continue;
    }
    EXPECT_FALSE(contains_git_commit_literal(read_source_file(relative_path)))
        << relative_path.generic_string();
  }
}

TEST(BuildGuardrailTests, MigratedModelingDomainTestFilesUseNativeNames) {
  const std::vector<std::string> forbidden_path_fragments = {
      "tests/modeling_domain/host_contract/",
      "qdr_editor_core_",
      "qdr_host_contract_",
      "qdr_custom_engine_modeling_session_smoke_tests",
      "qdr_io_session_tests",
      "src/modeling/",
      "src/modeling/workspace/",
      "src/modeling/tools/",
      "src/document/",
      "src/editor/actions/",
      "src/editor/mesh/",
      "src/geometry/",
      "resources/modeling/default_materials/",
      "src/viewport/tools/",
  };

  for (const std::filesystem::path &relative_path : guardrail_source_files()) {
    const std::string path = relative_path.generic_string();
    for (const std::string &fragment : forbidden_path_fragments) {
      EXPECT_EQ(path.find(fragment), std::string::npos)
          << path << " contains stale absorbed-SDK path fragment " << fragment;
    }
  }
}

TEST(BuildGuardrailTests,
     SourceBuildAndTestsDoNotUseOldSdkIncludeOrPackagePaths) {
  const std::vector<std::string> old_include_roots = {
      "quader_editor_core",
      "quader_poly",
      "quader_io",
      "quader_host_contract",
  };
  std::vector<std::string> forbidden;
  for (const std::string &root : old_include_roots) {
    forbidden.push_back(std::string("#include <") + root + "/");
    forbidden.push_back(std::string("#include \"") + root + "/");
  }
  forbidden.push_back(std::string("sdk/") + "packages/");
  forbidden.push_back(std::string("sdk\\") + "packages\\");

  for (const std::filesystem::path &relative_path : guardrail_source_files()) {
    const std::string source = read_source_file(relative_path);
    for (const std::string &needle : forbidden) {
      EXPECT_EQ(source.find(needle), std::string::npos)
          << relative_path.generic_string() << " contains " << needle;
    }
  }
}

TEST(BuildGuardrailTests, RootGuidanceDoesNotDescribeOldCopiedSdkBuildBoundary) {
  const std::vector<std::filesystem::path> guidance = {
      "AGENTS.md",
      "architecture-review.md",
      "src/editor/metadata/README.md",
      "src/io/qdr/README.md",
  };
  const std::vector<std::string> forbidden = {
      std::string("sdk/") + "packages/",
      "SDK checkout",
      "from the SDK side",
      "links `qdr_host_contract`, `qdr_editor_core`, and `qdr_io`",
      "through CMake `add_subdirectory`",
  };

  for (const std::filesystem::path &file : guidance) {
    const std::string source = read_source_file(file);
    for (const std::string &needle : forbidden) {
      EXPECT_EQ(source.find(needle), std::string::npos)
          << file.generic_string() << " contains " << needle;
    }
  }
}

TEST(BuildGuardrailTests, PublicExamplesDoNotExposeStaleSdkBoundaryLanguage) {
  const std::vector<std::filesystem::path> public_examples = {
      "examples/lua/README.md",
  };
  const std::vector<std::string> forbidden = {
      "SDK internals",
      "document, SDK, UI",
      "document, SDK, render",
      "expose document, SDK",
  };

  for (const std::filesystem::path &file : public_examples) {
    const std::string source = read_source_file(file);
    for (const std::string &needle : forbidden) {
      EXPECT_EQ(source.find(needle), std::string::npos)
          << file.generic_string() << " contains " << needle;
    }
  }
}

TEST(BuildGuardrailTests, DefaultMaterialMetadataMatchesRuntimePackagePath) {
  const std::string root_c_make = read_source_file("CMakeLists.txt");
  const std::string app_source = read_source_file("src/app/app.cpp");
  const std::string bootstrap_source = read_source_file(
      "src/editor/session/project_asset_bootstrap_service.cpp");
  const std::string provisioner_source = read_source_file(
      "src/io/project/project_material_provisioner.cpp");
  const std::string host_contract =
      read_source_file("src/editor/metadata/host_contract.cpp");
  const std::string default_catalog =
      read_source_file("src/assets/default_material_catalog.hpp");
  const std::string project_material_template = read_source_file(
      "resources/modeling_assets/materials/default/default_material.qmat");

  expect_contains(
      root_c_make,
      "${QUADER_RUNTIME_DEPLOY_DIR}/materials/default/default_albedo.png");
  expect_contains(
      root_c_make,
      "${QUADER_RUNTIME_DEPLOY_DIR}/materials/default/default_roughness.png");
  expect_contains(
      root_c_make,
      "${QUADER_RUNTIME_DEPLOY_DIR}/materials/default/default_material.qmat");
  expect_contains(default_catalog, "kDefaultMaterialAlbedoTexturePackagePath");
  expect_contains(default_catalog,
                  "kDefaultMaterialRoughnessTexturePackagePath");
  expect_contains(default_catalog, "kDefaultProjectMaterialId");
  expect_contains(default_catalog, "kDefaultProjectAlbedoTextureId");
  expect_contains(default_catalog, "kDefaultProjectMaterialTemplatePath");
  expect_contains(default_catalog, "kDefaultProjectAlbedoTemplatePath");
  expect_contains(app_source, "ProjectAssetBootstrapService");
  expect_contains(bootstrap_source, "default_material_catalog.hpp");
  expect_contains(bootstrap_source, "kDefaultMaterialAlbedoTexturePackagePath");
  expect_contains(bootstrap_source,
                  "kDefaultMaterialRoughnessTexturePackagePath");
  expect_contains(host_contract, "default_material_catalog.hpp");
  expect_contains(host_contract, "kDefaultMaterialAlbedoTexturePackagePath");
  expect_contains(host_contract, "kDefaultMaterialRoughnessTexturePackagePath");
  expect_contains(provisioner_source, "kDefaultProjectMaterialTemplatePath");
  expect_contains(provisioner_source, "kDefaultProjectAlbedoTemplatePath");
  expect_contains(project_material_template, "\"baseColorTexture\"");
  expect_missing(project_material_template, "\"roughnessTexture\"");
  expect_missing(host_contract, "assets/materials/default/");
  expect_missing(app_source, "default_material_catalog.hpp");
  expect_missing(app_source, "\"materials/default/default_albedo.png\"");
  expect_missing(bootstrap_source, "\"materials/default/default_albedo.png\"");
  expect_missing(host_contract, "\"materials/default/default_albedo.png\"");
}

TEST(BuildGuardrailTests, LocalMachinePathsUseEnvironmentBackedDefaults) {
  const std::string root_c_make = read_source_file("CMakeLists.txt");
  const std::string options =
      read_source_file("cmake/QuaderBuildOptions.cmake");
  const std::string presets = read_source_file("CMakePresets.json");

  expect_contains(root_c_make, "ENV{FILAMENT_SDK_ROOT}");
  expect_contains(root_c_make,
                  "../_SOURCES/filament-main-d1ccd94d8cd1-quader-vsync10-windows");
  expect_contains(options, "QUADER_OPENMESH_SOURCE_DIR");
  expect_contains(options, "ENV{${env_var}}");
  expect_contains(options, "ENV{USERPROFILE}");
  for (const std::string &needle : retired_modeling_test_framework_needles()) {
    expect_missing(options, needle);
  }

  expect_contains(presets,
                  "$env{USERPROFILE}/vcpkg/scripts/buildsystems/vcpkg.cmake");
  expect_missing(presets, "C:/Users/Drako/vcpkg");
  expect_missing(presets, "CMAKE_C_COMPILER");
  expect_missing(presets, "CMAKE_CXX_COMPILER");
  expect_missing(presets, "Program Files/Microsoft Visual Studio");
  expect_missing(presets, "Program Files\\\\Microsoft Visual Studio");
}

TEST(BuildGuardrailTests, FilamentSdkIsValidatedAtConfigureTime) {
  const std::string root_c_make = read_source_file("CMakeLists.txt");

  const std::size_t validation = root_c_make.find(
      "quader_validate_filament_sdk(\"${FILAMENT_SDK_ROOT}\")");
  const std::size_t target =
      root_c_make.find("add_library(filament_sdk INTERFACE)");

  ASSERT_NE(validation, std::string::npos);
  ASSERT_NE(target, std::string::npos);
  EXPECT_LT(validation, target);

  expect_contains(root_c_make, "include/filament/Engine.h");
  expect_contains(root_c_make, "bin/matc.exe");
  expect_contains(
      root_c_make,
      "lib/x86_64/md/${_quader_filament_lib}.lib");
  expect_missing(root_c_make, "lib/x86_64/mdd");
  expect_missing(root_c_make, "matdbg.lib");
  expect_contains(root_c_make, "quader_validate_filament_patch(\"${sdk_root}\")");
  const std::string patch_validation = read_source_file("cmake/QuaderFilamentSdk.cmake");
  expect_contains(patch_validation, "quader-filament-sdk.json");
  expect_contains(patch_validation,
                  "foreach(field artifact upstream_version upstream_ref "
                  "upstream_commit source_sha256 patch_version)");
  expect_contains(patch_validation, "file(SHA256 \"${patch_path}\" expected_patch)");
  expect_contains(patch_validation, "file(SHA256 \"${sdk_root}/${relative_path}\" actual_hash)");
  expect_contains(patch_validation, "file_sha256");
  expect_contains(patch_validation, "runtime STREQUAL \"MD\"");
  expect_contains(patch_validation, "configuration STREQUAL \"Release\"");
  expect_missing(patch_validation, "file(DOWNLOAD");
}

TEST(BuildGuardrailTests, BootSmokeFailsOnLoggedErrorsAndRequiresLogCopy) {
  const std::string smoke = read_source_file("tools/boot_smoke.ps1");

  expect_contains(smoke, "[switch]$AllowMissingLog");
  expect_contains(smoke, "Quader log was not found");
  expect_contains(smoke, "(?im)\\[(error|critical)\\]|fatal error|\\bfatal\\b");
  expect_contains(smoke, "Error, critical, or fatal log entry found");
}

TEST(BuildGuardrailTests,
     AffectedCheckToolPrintsBeforeAfterAndUsesFilteredGoogleTestRuns) {
  const std::string tool = read_source_file("tools/affected_check.ps1");
  const std::string root_agents = read_source_file("AGENTS.md");
  const std::string instruction_route = source_slice(
      tool, "if ($Path -match '(^|/)AGENTS", "# Prose must precede package rules");
  const std::string examples_route = source_slice(
      tool, "if ($Path -match '^examples/')",
      "if ($Path -match '^src/sdk/core/modeling/(internal/mesh/geometry/|");

  expect_contains(tool, "Before full workflow");
  expect_contains(tool, "After affected workflow");
  expect_contains(tool, "Before -> after");
  expect_contains(tool, "MeasureFull");
  expect_contains(tool, "Resolve-CMakePresetBinaryDir");
  expect_contains(
      tool,
      "Get-CMakePresetProperty \"build\" $BuildPreset \"configurePreset\"");
  expect_contains(
      tool, "Get-CMakePresetProperty \"build\" $BuildPreset \"configuration\"");
  expect_missing(tool, "build\\vs2026\\$Configuration");
  expect_contains(tool, "quader_guardrail_tests.exe");
  expect_contains(tool, "quader_tests.exe");
  expect_contains(tool, "quader_modeling_domain_tests.exe");
  expect_contains(tool, "quader_modeling_sdk_consumer_tests.exe");
  expect_contains(tool, "quader_modeling_sdk_parity_tests.exe");
  expect_contains(tool, "quader_modeling_domain_polygon_loop_tests");
  expect_contains(tool, "quader_modeling_domain_runtime_loop_tests");
  expect_contains(tool, "quader_modeling_domain_metadata_loop_tests");
  expect_contains(tool, "--gtest_filter=");
  expect_contains(tool, "GuardrailTestsFilters");
  expect_contains(tool, "ModelingLoopFilters");
  expect_contains(tool, "Get-PolygonOperationFilterForSourcePath");
  expect_contains(tool, "Get-RuntimeOperationFilterForSourcePath");
  expect_contains(tool, "Get-CMakeTargetSourcePaths");
  expect_contains(tool,
                  "Get-CMakeTargetSourcePaths \"quader_guardrail_tests\"");
  expect_contains(tool,
                  "Target '$TargetName' was not found in CMakeLists.txt.");
  expect_missing(tool, "\"tests/agent_instruction_routing_tests.cpp\"");
  expect_missing(tool, "\"tests/build_guardrail_tests.cpp\"");
  expect_missing(tool, "\"tests/oop_architecture_guardrail_tests.cpp\"");
  expect_missing(tool, "\"tests/programmatic_api_guardrail_tests.cpp\"");
  expect_contains(tool, "fill_hole_from_edges");
  expect_contains(tool, "portable_operation_mapping");
  expect_missing(tool, "selection_operations.cpp");
  expect_contains(tool, "QdrModelingRuntimeOperationCommandTests.*");
  expect_contains(tool, "QdrModelingRuntimeToolOperationContractTests.*");
  expect_contains(tool, "Add-PathPlan");
  expect_contains(tool, "Add-SdkVerification");
  expect_contains(tool, "Add-SdkPublicHeaderVerification");
  expect_contains(tool, "Assert-GTestFilterSelectsTests");
  expect_contains(tool, "Remove-RedundantBuildTargets");
  expect_contains(tool, "AGENTS\\.md");
  expect_contains(
      tool,
      "WindowGuardrailTests.WindowPlacementPersistenceAndResizeRecoveryStayOwned");
  expect_contains(tool, "AppStartupTests.*");
  expect_contains(tool, "FatalErrorHandlerTests.*");
  expect_contains(tool, "BootSmokeFailsOnLoggedErrorsAndRequiresLogCopy");
  expect_contains(tool, "src/render/");
  expect_contains(tool, "src/ui/");
  expect_contains(tool,
                  "neutral/include/quader/modeling/neutral/polygon/");
  expect_contains(tool, "src/editor/transform/");
  expect_contains(tool, "EditorApiTests.*Transform*");
  expect_contains(tool, "modeling/runtime");
  expect_contains(tool, "src/editor/metadata/");
  expect_contains(tool, "resources/locales/");
  expect_contains(tool, "resources/ibl/");
  expect_contains(tool, "modeling_assets|textures");
  expect_contains(tool, "CMakeLists");
  expect_contains(tool, "vcpkg\\.json");
  expect_contains(instruction_route, "AgentInstructionRoutingTests.*");
  expect_contains(instruction_route,
                  "BuildGuardrailTests."
                  "ProjectOwnedCommentCapableFilesCarryProprietaryBanner");
  expect_contains(examples_route,
                  "ProgrammaticApiGuardrailTests."
                  "PublicExamplesStartFromEditorApiOrScriptSurface");
  expect_contains(examples_route,
                  "ProgrammaticApiGuardrailTests."
                  "PublicExamplesDoNotStartFromInternalOwnerHeaders");

  expect_contains(root_agents, "tools/affected_check.ps1");
  expect_contains(instruction_route, "tasklist");
  expect_contains(instruction_route, "AGENTS\\.md");
}

TEST(BuildGuardrailTests,
     AffectedCheckPlanRoutesSdkChangesToOwningExecutables) {
#if defined(_WIN32)
  /**
   * Describes the ordered and forbidden commands for one changed path.
   */
  struct PlanExpectation {
    std::string path;
    std::vector<std::string> expected_in_order;
    std::vector<std::string> forbidden;
  };

  const std::vector<PlanExpectation> expectations = {
      {"tasklist.md",
       {" quader_guardrail_tests", "AgentInstructionRoutingTests.*"},
       {" quader_tests", "ctest --preset vs2026-debug"}},
      {"src/ui/AGENTS.md",
       {" quader_guardrail_tests", "AgentInstructionRoutingTests.*"},
       {" quader_tests", " quader_ui"}},
      {"cmake/AGENTS.md",
       {" quader_guardrail_tests", "AgentInstructionRoutingTests.*"},
       {" quader_tests", "ctest --preset vs2026-debug"}},
      {"src/sdk/core/modeling/src/session.cpp",
       {" quader_modeling_sdk_consumer_tests",
        " quader_modeling_sdk_parity_tests",
        "quader_modeling_sdk_consumer_tests.exe --gtest_color=no "
        "--gtest_filter=*",
        "quader_modeling_sdk_parity_tests.exe --gtest_color=no "
        "--gtest_filter=*"},
       {}},
      {"src/sdk/core/modeling/include/quader/modeling/session/"
       "modeling_session.hpp",
       {" quader_guardrail_tests",
        " quader_modeling_sdk_consumer_tests",
        " quader_modeling_sdk_parity_tests",
        "quader_guardrail_tests.exe --gtest_color=no "
        "--gtest_filter=BuildGuardrailTests."
        "ModelingSdkPublicHeadersStayNativeOwnerFree",
        "quader_modeling_sdk_consumer_tests.exe --gtest_color=no "
        "--gtest_filter=*",
        "quader_modeling_sdk_parity_tests.exe --gtest_color=no "
        "--gtest_filter=*"},
       {}},
      {"tests/sdk/modeling_sdk_consumer_tests.cpp",
       {" quader_modeling_sdk_consumer_tests",
        "quader_modeling_sdk_consumer_tests.exe --gtest_color=no "
        "--gtest_filter=ModelingSdkConsumerTests.*"},
       {" quader_tests", " quader_modeling_sdk_parity_tests",
        "quader_modeling_sdk_parity_tests.exe"}},
      {"tests/sdk/modeling_sdk_parity_tests.cpp",
       {" quader_modeling_sdk_parity_tests",
         "quader_modeling_sdk_parity_tests.exe --gtest_color=no "
         "--gtest_filter=ModelingSdkParityHarnessTests.*:"
         "ModelingSdkParityTests.*"},
       {" quader_tests", " quader_modeling_sdk_consumer_tests",
        "quader_modeling_sdk_consumer_tests.exe"}},
      {"src/ui/outliner_panel.cpp",
       {" quader_guardrail_tests", " quader_tests",
        " quader_ui", "UiLayoutGuardrailTests.*",
        "LayerPanelTests.*", "OutlinerPanelTests.*", "Qt*"},
       {"ctest --preset vs2026-debug"}},
      {"src/sdk/core/modeling/internal/mesh/polygon/"
       "document_validation.cpp",
       {" quader_modeling_domain_polygon_loop_tests",
        " quader_modeling_sdk_consumer_tests",
        " quader_modeling_sdk_parity_tests",
        "quader_modeling_sdk_consumer_tests.exe --gtest_color=no "
        "--gtest_filter=*",
        "quader_modeling_sdk_parity_tests.exe --gtest_color=no "
        "--gtest_filter=*",
        "quader_modeling_domain_polygon_loop_tests.exe --gtest_color=no "
        "--gtest_filter=QdrDocument*:QdrDocumentOperation*:QdrPoly*"},
       {}},
      {"CMakeLists.txt",
       {"cmake --preset vs2026",
        " quader_guardrail_tests",
        " quader_modeling_domain_tests",
        " quader_modeling_sdk_consumer_tests",
        " quader_modeling_sdk_parity_tests",
        " quader_tests",
        "ctest --preset vs2026-debug --output-on-failure"},
       {" quader_modeling_sdk_only_configure_build"}},
      {"FEATURE_IDEAS.md",
       {"<nothing to run>"}, {"cmake", "ctest", "--gtest_filter"}},
      {"src/sdk/core/modeling/COMPATIBILITY.md",
       {"<nothing to run>"}, {"cmake", "ctest", "--gtest_filter"}},
      {"src/sdk/core/modeling/internal/mesh/polygon/README.md",
       {"<nothing to run>"}, {"cmake", "ctest", "--gtest_filter"}},
      {"thirdparty/filament/README.md",
       {"<nothing to run>"}, {"cmake", "ctest", "--gtest_filter"}},
      {"examples/lua/README.md",
       {"<nothing to run>"}, {"cmake", "ctest", "--gtest_filter"}},
      {"FEATURE_IDEAS.md, tests/sdk/modeling_sdk_consumer_tests.cpp ",
       {" quader_modeling_sdk_consumer_tests",
        "--gtest_filter=ModelingSdkConsumerTests.*"},
       {" quader_tests", " quader_modeling_sdk_parity_tests", "ctest"}},
      {"tests/affected-check-deleted-tests.cpp",
       {"cmake --preset vs2026", "ctest --preset vs2026-debug"}, {}},
      {"docs/affected-check-missing-document.md",
       {"cmake --preset vs2026", "ctest --preset vs2026-debug"}, {}},
      {"unknown/affected_check_route.fixture",
       {"cmake --preset vs2026",
        " quader_guardrail_tests",
        " quader_modeling_domain_tests",
        " quader_modeling_sdk_consumer_tests",
        " quader_modeling_sdk_parity_tests",
        " quader_tests",
        "ctest --preset vs2026-debug --output-on-failure"},
       {" quader_modeling_sdk_only_configure_build"}},
  };

  const std::filesystem::path script =
      source_path("tools/affected_check.ps1");
  for (const PlanExpectation &expectation : expectations) {
    SCOPED_TRACE(expectation.path);
    const std::string command =
        "powershell -NoProfile -ExecutionPolicy Bypass -File " +
        quoted_process_argument(script) + " -PlanOnly -ChangedPath \"" +
        expectation.path + "\"";
    const CommandResult result = run_command_capture(command);
    ASSERT_EQ(result.exit_code, 0) << result.output;

    const std::string marker = "After affected workflow:";
    const std::size_t affected = result.output.find(marker);
    ASSERT_NE(affected, std::string::npos) << result.output;
    const std::string affected_plan = result.output.substr(affected);
    // All selected test/library targets share one build invocation.
    const auto build = affected_plan.find("cmake --build");
    if (build != std::string::npos) {
      EXPECT_EQ(affected_plan.find("cmake --build", build + 1), std::string::npos)
          << affected_plan;
    }
    expect_contains_in_order(affected_plan, expectation.expected_in_order);
    for (const std::string &forbidden : expectation.forbidden) {
      expect_missing(affected_plan, forbidden);
    }
  }
#else
  GTEST_SKIP() << "affected_check.ps1 plans the native Windows VS2026 executable workflow.";
#endif
}

TEST(BuildGuardrailTests,
     AffectedCheckAppBuildDefaultsToReleaseSeparatelyFromTestTargets) {
#if defined(_WIN32)
  struct Expectation {
    std::string arguments;
    std::string app_preset;
    bool builds_tests;
    std::string expected_filter;
  };
  const std::vector<Expectation> cases = {
      {"-ChangedPath resources/locales/en-US.lang", "vs2026-release", true,
       "LocalizationTests.*"},
      {"-ChangedPath resources/textures/default_texture.png", "vs2026-release",
       true, "QuaderMeshMaterialTests.*"},
      {"-ChangedPath FEATURE_IDEAS.md -IncludeApp", "vs2026-release", false,
       ""},
      {"-ChangedPath FEATURE_IDEAS.md -IncludeApp -AppBuildPreset vs2026-debug",
       "vs2026-debug", false, ""},
  };
  for (const auto &test_case : cases) {
    SCOPED_TRACE(test_case.arguments);
    const auto result = run_command_capture(
        "powershell -NoProfile -ExecutionPolicy Bypass -File " +
        quoted_process_argument(source_path("tools/affected_check.ps1")) +
        " -PlanOnly " + test_case.arguments);
    ASSERT_EQ(result.exit_code, 0) << result.output;
    const auto marker = result.output.find("After affected workflow:");
    ASSERT_NE(marker, std::string::npos) << result.output;
    const auto plan = result.output.substr(marker);
    expect_contains(plan, "cmake --build --preset " + test_case.app_preset +
                              " --target quader");
    const auto first_build = plan.find("cmake --build");
    const auto second_build = plan.find("cmake --build", first_build + 1);
    if (test_case.builds_tests) {
      expect_contains_in_order(plan, {
          "cmake --build --preset vs2026-debug --target quader_guardrail_tests quader_tests",
          "cmake --build --preset vs2026-release --target quader",
          test_case.expected_filter});
      ASSERT_NE(second_build, std::string::npos);
      EXPECT_EQ(plan.find("cmake --build", second_build + 1), std::string::npos);
      if (test_case.arguments.find("resources/textures/") !=
          std::string::npos) {
        const auto guardrail_start = plan.find("quader_guardrail_tests.exe");
        const auto quader_start = plan.find("quader_tests.exe", guardrail_start);
        ASSERT_NE(guardrail_start, std::string::npos) << plan;
        ASSERT_NE(quader_start, std::string::npos) << plan;
        const std::string guardrail_run =
            plan.substr(guardrail_start, quader_start - guardrail_start);
        const auto quader_end = plan.find('\n', quader_start);
        const std::string quader_run =
            plan.substr(quader_start, quader_end - quader_start);
        expect_contains(
            guardrail_run,
            "BuildGuardrailTests."
            "RuntimeResourceDeploymentIsDependencyTrackedAndSynchronizesOwnedDirs");
        expect_contains(guardrail_run, "QuaderMeshMaterialTests.*");
        expect_missing(quader_run, "QuaderMeshMaterialTests.*");
      }
    } else {
      EXPECT_EQ(second_build, std::string::npos);
      expect_missing(plan, "--gtest_filter");
    }
    if (test_case.app_preset == "vs2026-release") {
      EXPECT_FALSE(std::regex_search(plan, std::regex(
          R"(cmake --build --preset vs2026-debug --target[^\r\n]*\bquader(?:\s|$))")))
          << plan;
    }
    expect_missing(plan, "ctest --preset");
  }
#else
  GTEST_SKIP() << "affected_check.ps1 plans native Windows builds.";
#endif
}

TEST(BuildGuardrailTests,
     AffectedCheckDefaultDiscoveryKeepsDeletedPathsAndExplicitScopeOverridesIt) {
#if defined(_WIN32)
  const auto script = powershell_literal(source_path("tools/affected_check.ps1"));
  // Substitute only Git discovery. Route the emitted deleted/untracked paths
  // through the real planner without modifying this shared checkout.
  const std::string command =
      "powershell -NoProfile -ExecutionPolicy Bypass -Command \""
      "function git { $global:LASTEXITCODE = 0; "
      "if ($args -contains 'diff') { "
      "if ($args -notcontains '--diff-filter=ACDMRTUXB' -or "
      "$args -notcontains '--no-renames') { throw 'Deletions/rename sources excluded' }; "
      "'tests/affected-check-deleted-tests.cpp' "
      "} elseif ($args -contains 'ls-files') { 'src/api/quader_tools_api.cpp' "
      "} else { throw 'Unexpected Git discovery request' } }; "
      "& " + script + " -PlanOnly; "
      "function git { throw 'Explicit scope must not discover unrelated dirty paths' }; "
      "& " + script + " -PlanOnly -ChangedPath 'FEATURE_IDEAS.md'\"";
  const auto result = run_command_capture(command);
  ASSERT_EQ(result.exit_code, 0) << result.output;
  const auto explicit_scope = result.output.find("Scope: explicit ChangedPath override");
  ASSERT_NE(explicit_scope, std::string::npos) << result.output;
  const auto integration = result.output.substr(0, explicit_scope);
  expect_contains(integration, "Scope: Git dirty-tree integration");
  expect_contains(integration, "missing or deleted path: tests/affected-check-deleted-tests.cpp");
  expect_contains(integration, "src/api/quader_tools_api.cpp");
  expect_contains(integration, "ctest --preset vs2026-debug --output-on-failure");
  const auto focused = result.output.substr(explicit_scope);
  expect_missing(focused, "src/api/quader_tools_api.cpp");
  expect_missing(focused, "affected-check-deleted-tests.cpp");
  const auto affected = focused.find("After affected workflow:");
  ASSERT_NE(affected, std::string::npos);
  expect_contains(focused.substr(affected), "<nothing to run>");

  for (const std::string argument : {",", "../FEATURE_IDEAS.md"}) {
    const auto rejected = run_command_capture(
        "powershell -NoProfile -ExecutionPolicy Bypass -Command \"& " + script +
        " -PlanOnly -ChangedPath '" + argument + "'\"");
    EXPECT_NE(rejected.exit_code, 0) << rejected.output;
    expect_contains(rejected.output, "repository-relative");
  }
#else
  GTEST_SKIP() << "affected_check.ps1 plans native Windows builds.";
#endif
}

TEST(BuildGuardrailTests,
     AffectedCheckRejectsMissingExecutableAndZeroSelectedTests) {
#if defined(_WIN32)
  const std::filesystem::path script =
      source_path("tools/affected_check.ps1");
  const std::filesystem::path executable = current_executable_path();
  const std::string missing_filter =
      "AffectedCheckDeliberatelyMissingFixture.*";
  const auto validation_command = [&script](
                                      const std::filesystem::path &target,
                                      const std::string &filter) {
    return "powershell -NoProfile -ExecutionPolicy Bypass -Command \". " +
           powershell_literal(script) + "; Assert-GTestFilterSelectsTests " +
           powershell_literal(target) + " '" + filter + "'\"";
  };

  const CommandResult zero_selected =
      run_command_capture(validation_command(executable, missing_filter));
  EXPECT_NE(zero_selected.exit_code, 0) << zero_selected.output;
  expect_contains(zero_selected.output, "selected zero tests");
  expect_contains(zero_selected.output, missing_filter);

  const std::filesystem::path missing_executable =
      source_root() / "affected-check-missing-fixture" /
      "missing_gtest_executable.exe";
  ASSERT_FALSE(std::filesystem::exists(missing_executable));
  const CommandResult missing =
      run_command_capture(validation_command(missing_executable, "*"));
  EXPECT_NE(missing.exit_code, 0) << missing.output;
  expect_contains(missing.output, "GoogleTest executable was not found");
  expect_contains(missing.output, "missing_gtest_executable.exe");

  const std::string missing_command =
      "powershell -NoProfile -ExecutionPolicy Bypass -Command \". " +
      powershell_literal(script) +
      "; Invoke-CommandLine 'missing fixture' @(" +
      powershell_literal(missing_executable) + ")\"";
  const CommandResult generic_missing = run_command_capture(missing_command);
  EXPECT_NE(generic_missing.exit_code, 0) << generic_missing.output;
  expect_contains(generic_missing.output, "Command executable was not found");
  expect_contains(generic_missing.output, "missing_gtest_executable.exe");
#else
  GTEST_SKIP() << "affected_check.ps1 launches native Windows verification executables.";
#endif
}

TEST(BuildGuardrailTests, TestingDocsExplainPlacementAndSelectionRules) {
  const std::string root_agents = read_source_file("AGENTS.md");
  const std::string tests_guide = read_source_file("tests/AGENTS.md");
  const std::string modeling_guide =
      read_source_file("tests/modeling_domain/AGENTS.md");

  expect_contains(root_agents, "tests/AGENTS.md");
  for (const char *contract :
       {"quader_guardrail_tests", "quader_tests", "quader_modeling_domain_tests",
        "tests/modeling_domain/", "tools/affected_check.ps1", "--gtest_filter",
        "-MeasureFull", "tools/regression_gate.ps1",
        "support/command_behavior_contract.cpp",
        "baselines/command_behavior.json"}) {
    expect_contains(tests_guide, contract);
  }

  expect_contains(modeling_guide, "qdr_document_operation_<feature>_tests.cpp");
  expect_contains(modeling_guide,
                  "qdr_modeling_runtime_operation_command_tests.cpp");
  expect_contains(modeling_guide,
                  "qdr_modeling_runtime_tool_<feature>_tests.cpp");
  expect_contains(modeling_guide, "quader_modeling_domain_polygon_loop_tests");
  expect_contains(modeling_guide, "quader_modeling_domain_runtime_loop_tests");
  expect_contains(modeling_guide, "quader_modeling_domain_metadata_loop_tests");
}

TEST(BuildGuardrailTests, LocalizationResourcesArePackagedWithRuntime) {
  const std::string root_c_make = read_source_file("CMakeLists.txt");

  expect_contains(root_c_make, "src/core/localization_service.cpp");
  expect_contains(root_c_make, "tests/localization_tests.cpp");
  expect_contains(root_c_make, "${QUADER_RUNTIME_DEPLOY_DIR}/locales");
  expect_contains(root_c_make, "resources/locales");
  expect_contains(root_c_make, "copy_directory");

  EXPECT_FALSE(read_source_file("resources/locales/en-US.lang").empty());
  EXPECT_FALSE(read_source_file("resources/locales/it-IT.lang").empty());
}

TEST(BuildGuardrailTests,
     UiGuidanceDescribesRetainedFrameSynchronization) {
  const std::string ui_guide = read_source_file("src/ui/AGENTS.md");
  const std::string tests_guide = read_source_file("tests/AGENTS.md");

  expect_contains(ui_guide, "UISystem::refresh");
  expect_contains(ui_guide, "commands().available()");
  expect_contains(ui_guide, "commands().descriptor(id)");
  expect_contains(ui_guide, "revision");
  expect_contains(tests_guide, "same-build, same-scene");
}

TEST(BuildGuardrailTests,
     RuntimeResourceDeploymentIsDependencyTrackedAndSynchronizesOwnedDirs) {
  const std::string root_c_make = read_source_file("CMakeLists.txt");
  const std::string resource_inputs = source_slice(
      root_c_make, "set(QUADER_RUNTIME_RESOURCE_INPUTS",
      "list(SORT QUADER_RUNTIME_RESOURCE_INPUTS)");
  const std::string resource_deployment = source_slice(
      root_c_make,
      "add_custom_command(\n    OUTPUT \"${QUADER_RUNTIME_RESOURCE_STAMP}\"",
      "add_custom_target(quader_runtime_resources");

  expect_contains(root_c_make, "QUADER_RUNTIME_RESOURCE_INPUTS");
  expect_contains(root_c_make, "CONFIGURE_DEPENDS");
  expect_contains(root_c_make, "QUADER_RUNTIME_RESOURCE_MANIFEST");
  expect_contains(root_c_make, "file(GENERATE");
  expect_contains(root_c_make,
                  "OUTPUT \"${QUADER_RUNTIME_RESOURCE_STAMP}\"");
  expect_contains(root_c_make,
                  "DEPENDS \"${QUADER_RUNTIME_RESOURCE_STAMP}\"");
  expect_contains(root_c_make,
                  "add_custom_target(quader_runtime_resources");
  expect_contains(root_c_make,
                  "add_dependencies(quader quader_runtime_resources)");
  expect_contains(root_c_make,
                  "${CMAKE_CURRENT_BINARY_DIR}/generated/materials");
  expect_contains(root_c_make,
                  "\"${QUADER_RUNTIME_RESOURCE_MANIFEST}\"");
  expect_contains(root_c_make,
                  "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt");
  expect_contains(
      root_c_make,
      "set(QUADER_SOLID_VIEWPORT_TEXTURE\n"
      "    \"${CMAKE_CURRENT_SOURCE_DIR}/resources/textures/"
      "default_texture.png\")");
  expect_contains(resource_inputs, "${QUADER_SOLID_VIEWPORT_TEXTURE}");
  expect_contains(
      resource_deployment,
      "\"${QUADER_SOLID_VIEWPORT_TEXTURE}\"\n"
      "            \"${QUADER_RUNTIME_DEPLOY_DIR}/textures/"
      "default_texture.png\"");

  for (const std::string &owned_dir :
       {"materials", "icons", "ibl", "locales", "textures"}) {
    expect_contains(root_c_make,
                    "${QUADER_RUNTIME_DEPLOY_DIR}/" + owned_dir);
  }
  expect_missing(root_c_make, "$<TARGET_FILE_DIR:quader>/locales");
  expect_missing(root_c_make, "$<TARGET_FILE_DIR:quader>/ibl");
  expect_missing(root_c_make,
                 "$<TARGET_FILE_DIR:quader>/materials/default");
}

TEST(BuildGuardrailTests, QtWidgetsRuntimeAndLicenseEvidenceAreConfigured) {
  const std::string root_c_make = read_source_file("CMakeLists.txt");
  const std::string vcpkg_manifest = read_source_file("vcpkg.json");
  const std::string third_party_manifest =
      read_source_file("thirdparty/THIRD_PARTY_MANIFEST.md");
  const std::string qt_license =
      read_source_file("thirdparty/licenses/qtbase-LGPL-3.0-only.txt");

  expect_contains(root_c_make,
                  "find_package(Qt6 CONFIG REQUIRED COMPONENTS Core Gui "
                  "Widgets)");
  expect_contains(root_c_make, "Qt6::Widgets");
  expect_contains(root_c_make, "src/ui/qt_viewport_panel.cpp");
  expect_contains(root_c_make, "src/ui/qt_viewport_surface.cpp");
  expect_contains(root_c_make, "src/ui/qt_application_theme.cpp");
  expect_contains(root_c_make, "src/ui/qt_color_swatch_button.cpp");
  expect_contains(root_c_make, "src/ui/qt_auxiliary_window_state.cpp");
  expect_contains(root_c_make, "src/ui/qt_settings_dialog.cpp");
  expect_contains(root_c_make, "src/ui/qt_tool_settings_panel.cpp");
  expect_contains(vcpkg_manifest, "\"name\": \"qtbase\"");
  expect_contains(vcpkg_manifest, "\"widgets\"");
  expect_contains(third_party_manifest, "Qt Base");
  expect_contains(third_party_manifest,
                  "licenses/qtbase-LGPL-3.0-only.txt");
  expect_contains(qt_license, "GNU LESSER GENERAL PUBLIC LICENSE");

  // Fusion supplies widget mechanics. The app must not package the retired
  // ImGui material or UI fonts as a second presentation system.
  expect_missing(root_c_make, "resources/materials/imgui.mat");
  expect_missing(root_c_make, "resources/fonts/Inter.ttf");
  expect_missing(root_c_make, "resources/fonts/Phosphor.ttf");
  expect_missing(root_c_make, "copy_directory\n            \"${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts\"");

  // Renderer-owned scene and overlay resources remain native app assets.
  expect_contains(root_c_make, "quader_register_filament_material");
  expect_contains(root_c_make, "QUADER_FILAMENT_MATERIAL_COPY_COMMANDS");
  expect_contains(root_c_make, "resources/materials/quader_mesh.mat");
  expect_contains(root_c_make, "resources/materials/quader_mesh_lit.mat");
  expect_contains(root_c_make, "resources/materials/quader_overlay_icon.mat");
  expect_contains(root_c_make, "resources/icons");
  expect_contains(root_c_make, "${QUADER_RUNTIME_DEPLOY_DIR}/icons");
  expect_contains(root_c_make, "copy_directory");
  EXPECT_FALSE(
      read_source_file("resources/materials/quader_overlay_icon.mat").empty());
  EXPECT_FALSE(read_source_file("resources/materials/quader_mesh.mat").empty());
  EXPECT_FALSE(
      read_source_file("resources/materials/quader_mesh_lit.mat").empty());
  EXPECT_TRUE(std::filesystem::exists(source_path("resources/icons/logo.png")));
  EXPECT_TRUE(
      std::filesystem::exists(source_path("resources/icons/point_light.png")));
  EXPECT_TRUE(std::filesystem::exists(
      source_path("resources/textures/default_texture.png")));
  EXPECT_GT(std::filesystem::file_size(source_path("resources/icons/logo.png")),
            512u);
  EXPECT_GT(std::filesystem::file_size(
                source_path("resources/icons/point_light.png")),
            512u);
}

TEST(BuildGuardrailTests,
     DimensionPixelFontIsNarrowlyEmbeddedAndHasPinnedLicenseEvidence) {
  const std::string root_c_make = read_source_file("CMakeLists.txt");
  const std::string manifest =
      read_source_file("thirdparty/THIRD_PARTY_MANIFEST.md");
  const std::string provenance =
      read_source_file("thirdparty/home-video-font-PROVENANCE.md");
  const std::string license =
      read_source_file("thirdparty/licenses/home-video-CC0-1.0.txt");
  const std::string resource_block = source_slice(
      root_c_make,
      "qt_add_resources(quader_ui \"quader_dimension_fonts\"",
      "quader_enable_common_pch(quader_ui)");

  expect_contains(resource_block, "PREFIX \"/quader/fonts\"");
  expect_contains(resource_block,
                  "BASE \"${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts\"");
  expect_contains(
      resource_block,
      "\"${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/HomeVideo-Regular.ttf\"");
  EXPECT_EQ(count_occurrences(resource_block, "HomeVideo-Regular.ttf"), 1U);
  expect_missing(resource_block, "GLOB");
  expect_missing(resource_block, "copy_directory");
  EXPECT_EQ(count_occurrences(root_c_make, "HomeVideo-Regular.ttf"), 1U);

  const std::filesystem::path font =
      source_path("resources/fonts/HomeVideo-Regular.ttf");
  ASSERT_TRUE(std::filesystem::is_regular_file(font));
  EXPECT_EQ(std::filesystem::file_size(font), 93580U);

  constexpr std::string_view kFontSha256 =
      "E73D67F92457BD05D24C42F43156BC422E98982E9E62D267AD6D1A3CC595BB01";
  for (const std::string &evidence : {manifest, provenance}) {
    expect_contains(evidence, "https://ggbot.itch.io/home-video-font");
    expect_contains(evidence, std::string(kFontSha256));
    expect_contains(evidence, "home-video-CC0-1.0.txt");
  }
  expect_contains(manifest, "Home Video font");
  expect_contains(manifest, "Regular v0.80");
  expect_contains(manifest, "home-video-font-PROVENANCE.md");
  expect_contains(provenance, "Home Video Regular v0.80");
  expect_contains(provenance, "OpenType Name ID 5): `0.80`");
  expect_contains(provenance, "OpenType Name IDs 9 and 8): `GGBotNet`");
  expect_contains(
      provenance,
      "OpenType Name ID 13): `Creative Commons Zero v1.0 Universal`");
  expect_contains(
      provenance,
      "OpenType Name ID 14): https://creativecommons.org/publicdomain/zero/1.0/");
  expect_contains(license, "Creative Commons Legal Code");
  expect_contains(license, "CC0 1.0 Universal");
  expect_contains(license, "2. Waiver.");
  expect_contains(license, "3. Public License Fallback.");

  // This dedicated pixel font does not revive the retired global UI font
  // package or the old loose-font deployment path.
  expect_missing(root_c_make, "resources/fonts/Inter.ttf");
  expect_missing(root_c_make, "resources/fonts/Phosphor.ttf");
  expect_missing(
      root_c_make,
      "copy_directory\n            \"${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts\"");
}

TEST(BuildGuardrailTests,
     QtTextureCodecPluginsUseReleaseDeploymentAndLicenseEvidence) {
  const std::string root_c_make = read_source_file("CMakeLists.txt");
  const std::string vcpkg_manifest = read_source_file("vcpkg.json");
  const std::string native_feature =
      source_slice(vcpkg_manifest, "\"native-app\": {", "\"tests\": {");
  const std::string sdk_only_check =
      read_source_file("cmake/QuaderSdkOnlyBuildCheck.cmake");
  const std::string launcher = read_source_file("tools/boot_tracy.ps1");
  const std::string native_build_contract =
      read_source_file("tools/native_build_contract.ps1");
  const std::string qt_image_formats_license = read_source_file(
      "thirdparty/licenses/qtimageformats-LGPL-3.0-only.txt");
  const std::string jpeg_license = read_source_file(
      "thirdparty/licenses/libjpeg-turbo-BSD-3-Clause-IJG.txt");
  const std::string tiff_license =
      read_source_file("thirdparty/licenses/libtiff-libtiff.txt");
  const std::string webp_license = read_source_file(
      "thirdparty/licenses/libwebp-BSD-3-Clause-and-PATENTS.txt");

  for (std::string_view required_manifest_token : {
           "\"jpeg\"", "\"png\"", "\"qtimageformats\"", "\"tiff\"",
           "\"webp\"", "\"widgets\"",
       }) {
    expect_contains(native_feature, std::string(required_manifest_token));
  }
  expect_missing(native_feature, "\"jasper\"");
  EXPECT_GE(count_occurrences(native_feature, "\"default-features\": false"),
            2U);

  for (std::string_view plugin_target : {
           "Qt6::QGifPlugin", "Qt6::QJpegPlugin", "Qt6::QTgaPlugin",
           "Qt6::QTiffPlugin", "Qt6::QWebpPlugin",
       }) {
    expect_contains(root_c_make, std::string(plugin_target));
  }
  expect_contains(root_c_make, "--add-plugin-types imageformats");
  expect_contains(root_c_make,
                  "--include-plugins qwindows,qgif,qjpeg,qtga,qtiff,qwebp");
  expect_contains(root_c_make,
                  "quader_deploy_windows_qt_runtime(quader)");
  expect_contains(root_c_make,
                  "quader_deploy_windows_qt_runtime(quader_tests)");
  expect_contains(root_c_make, "${_QUADER_VCPKG_RELEASE_BIN}");
  for (std::string_view runtime_dll : {
           "jpeg62.dll", "libsharpyuv.dll", "libwebp.dll",
           "libwebpdemux.dll", "libwebpmux.dll", "tiff.dll",
       }) {
    expect_contains(root_c_make, std::string(runtime_dll));
    expect_contains(native_build_contract, std::string(runtime_dll));
  }
  for (std::string_view plugin_dll : {
           "imageformats\\qgif.dll", "imageformats\\qjpeg.dll",
           "imageformats\\qtga.dll", "imageformats\\qtiff.dll",
           "imageformats\\qwebp.dll",
       }) {
    expect_contains(native_build_contract, std::string(plugin_dll));
  }
  expect_contains(launcher, "native_build_contract.ps1");
  for (std::string_view native_codec_package : {
           "libjpeg-turbo", "libwebp", "qtimageformats", "tiff",
       }) {
    expect_contains(sdk_only_check, std::string(native_codec_package));
  }

  expect_contains(qt_image_formats_license,
                  "GNU LESSER GENERAL PUBLIC LICENSE");
  expect_contains(jpeg_license,
                  "This software is based in part on the work of the ");
  expect_contains(tiff_license,
                  "Copyright (c) 1988-1997 Sam Leffler");
  expect_contains(webp_license, "Additional IP Rights Grant (Patents)");
}

TEST(BuildGuardrailTests, ApplicationIconIsEmbeddedForWindowsAndQt) {
  const std::string root_c_make = read_source_file("CMakeLists.txt");
  const std::string main_source = read_source_file("src/main.cpp");
  const std::string ui_system_source =
      read_source_file("src/ui/ui_system.cpp");
  const std::string windows_resource =
      read_source_file("resources/icons/quader.rc");

  expect_contains(root_c_make, "enable_language(RC)");
  expect_contains(root_c_make, "resources/icons/quader.rc");
  expect_contains(root_c_make, "resources/icons/quader.ico");
  expect_contains(root_c_make,
                  "qt_add_resources(quader \"quader_application_icons\"");
  expect_contains(root_c_make, "PREFIX \"/quader/icons\"");
  expect_contains(windows_resource,
                  "IDI_QUADER_APPLICATION ICON DISCARDABLE \"quader.ico\"");

  for (const std::string_view size : {"16", "32", "48", "64", "256"}) {
    const std::string relative_path =
        std::string{"resources/icons/quader_"} + std::string{size} + ".png";
    EXPECT_TRUE(std::filesystem::exists(source_path(relative_path)))
        << relative_path;
    expect_contains(root_c_make, relative_path);
    expect_contains(main_source,
                    std::string{":/quader/icons/quader_"} +
                        std::string{size} + ".png");
  }

  const std::filesystem::path native_icon =
      source_path("resources/icons/quader.ico");
  ASSERT_TRUE(std::filesystem::exists(native_icon));
  EXPECT_GT(std::filesystem::file_size(native_icon), 300000u);
  expect_contains(main_source,
                  "qt_application.setWindowIcon(application_icon)");
  expect_contains(ui_system_source,
                  "main_window_->setWindowIcon(QApplication::windowIcon())");
}

TEST(BuildGuardrailTests, SelectionToolbarIconsAreEmbeddedQtResources) {
  const std::string root_c_make = read_source_file("CMakeLists.txt");

  expect_contains(root_c_make,
                  "qt_add_resources(quader \"quader_selection_toolbar_icons\"");
  expect_contains(root_c_make, "PREFIX \"/quader/icons\"");
  for (const std::string_view mode :
       {"vertex", "edge", "face", "mesh", "group"}) {
    const std::string relative_path =
        std::string{"resources/icons/selection_"} + std::string{mode} +
        ".png";
    ASSERT_TRUE(std::filesystem::exists(source_path(relative_path)))
        << relative_path;
    EXPECT_GT(std::filesystem::file_size(source_path(relative_path)), 512u)
        << relative_path;
    expect_contains(root_c_make, relative_path);
  }
}

TEST(BuildGuardrailTests,
     RetainedMaterialFilterAssetIsEmbeddedAndMatchesGenerationEvidence) {
  // Browser chrome now draws palette-aware glyphs. Preserve the generated
  // resource's embedding and provenance independently of its former UI use.
  constexpr std::string_view kIconRelativePath =
      "resources/icons/material_filter.png";
  constexpr std::string_view kSourceIdentity =
      "exec-964af5e5-a267-4b3f-a271-8c31876e0d97.png";
  constexpr std::string_view kSha256 =
      "9f8d203ef2fbd022eb7e42540eee729f0abc91975f326263a4cf807dbc6c3eab";
  constexpr std::string_view kExactPrompt =
      "> Use case: logo-brand\n"
      "> Asset type: compact Qt material-browser toolbar filter icon\n"
      "> Primary request: Create one original conventional centered "
      "funnel/filter silhouette with a genuinely transparent background.\n"
      "> Style/medium: crisp polished raster UI icon with a vector-like "
      "silhouette\n"
      "> Composition/framing: square canvas; one centered funnel filling "
      "roughly 86% of the canvas; broad simple geometry legible at 16–20 "
      "logical pixels\n"
      "> Color palette: dark navy outline, pale silver/light-periwinkle fill, "
      "and restrained flat one-step blue shading\n"
      "> Constraints: preserve genuine transparent alpha outside the icon; "
      "clean well-defined edges; no text; no watermark\n"
      "> Avoid: glow, drop shadow, orange, extra objects, tiny details, "
      "gradients beyond the restrained single shading step\n";

  const std::filesystem::path icon_path = source_path(kIconRelativePath);
  ASSERT_TRUE(std::filesystem::is_regular_file(icon_path));
  EXPECT_GT(std::filesystem::file_size(icon_path), 512u);

  std::array<unsigned char, 26> png_header{};
  std::ifstream icon(icon_path, std::ios::binary);
  ASSERT_TRUE(icon.is_open());
  icon.read(reinterpret_cast<char *>(png_header.data()),
            static_cast<std::streamsize>(png_header.size()));
  ASSERT_EQ(icon.gcount(), static_cast<std::streamsize>(png_header.size()));
  constexpr std::array<unsigned char, 8> kPngSignature = {
      0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
  EXPECT_TRUE(std::equal(kPngSignature.begin(), kPngSignature.end(),
                         png_header.begin()));
  EXPECT_EQ(png_header[24], 8u) << "the generated PNG must retain 8-bit data";
  EXPECT_EQ(png_header[25], 6u)
      << "PNG IHDR color type 6 is true RGBA, not a flattened RGB image";

  const CommandResult digest = run_command_capture(
      "cmake -E sha256sum " + quoted_process_argument(icon_path));
  ASSERT_EQ(digest.exit_code, 0) << digest.output;
  expect_contains(digest.output, std::string(kSha256));

  const std::string root_c_make = read_source_file("CMakeLists.txt");
  const std::string provenance = read_source_file(
      "resources/icons/material_filter_icon.provenance.md");
  expect_contains(root_c_make,
                  "qt_add_resources(quader_ui \"quader_material_browser_icons\"");
  expect_contains(root_c_make, "PREFIX \"/quader/icons\"");
  expect_contains(root_c_make, "BASE \"${CMAKE_CURRENT_SOURCE_DIR}/resources/icons\"");
  expect_contains(root_c_make, std::string(kIconRelativePath));
  expect_contains(provenance, std::string(kSourceIdentity));
  expect_contains(provenance, std::string(kSha256));

  std::string normalized_provenance = provenance;
  normalized_provenance.erase(
      std::remove(normalized_provenance.begin(), normalized_provenance.end(),
                  '\r'),
      normalized_provenance.end());
  constexpr std::string_view kPromptMarker = "Prompt:\n\n";
  const std::size_t prompt_offset = normalized_provenance.find(kPromptMarker);
  ASSERT_NE(prompt_offset, std::string::npos);
  EXPECT_EQ(normalized_provenance.substr(prompt_offset + kPromptMarker.size()),
            kExactPrompt)
      << "the provenance must preserve the exact ImageGen prompt";
}

TEST(BuildGuardrailTests, ToolToolbarIconsAreEmbeddedQtResources) {
  const std::string root_c_make = read_source_file("CMakeLists.txt");

  expect_contains(root_c_make,
                  "qt_add_resources(quader \"quader_tool_toolbar_icons\"");
  expect_contains(root_c_make, "PREFIX \"/quader/icons\"");
  for (const std::string_view tool :
       {"select", "move", "rotate", "scale", "pivot",
        "transform_local", "create_primitive", "extrude",
        "insert_edge_loop", "knife", "cut", "poly", "mirror",
        "linear_array", "radial_array"}) {
    const std::string relative_path =
        std::string{"resources/icons/tool_"} + std::string{tool} + ".png";
    ASSERT_TRUE(std::filesystem::exists(source_path(relative_path)))
        << relative_path;
    EXPECT_GT(std::filesystem::file_size(source_path(relative_path)), 512u)
        << relative_path;
    expect_contains(root_c_make, relative_path);
  }
}

TEST(BuildGuardrailTests, SnappingToolbarIconsAreEmbeddedQtResources) {
  const std::string root_c_make = read_source_file("CMakeLists.txt");

  expect_contains(root_c_make,
                  "qt_add_resources(quader \"quader_snapping_toolbar_icons\"");
  expect_contains(root_c_make, "PREFIX \"/quader/icons\"");
  for (const std::string_view icon :
       {"master", "grid", "vertex", "rotation"}) {
    const std::string relative_path =
        std::string{"resources/icons/snapping_"} + std::string{icon} +
        ".png";
    ASSERT_TRUE(std::filesystem::exists(source_path(relative_path)))
        << relative_path;
    EXPECT_GT(std::filesystem::file_size(source_path(relative_path)), 512u)
        << relative_path;
    expect_contains(root_c_make, relative_path);
  }
}

TEST(BuildGuardrailTests,
     ViewportDisplayToolbarIconsAreEmbeddedQtResources) {
  const std::string root_c_make = read_source_file("CMakeLists.txt");

  expect_contains(
      root_c_make,
      "qt_add_resources(quader \"quader_viewport_display_toolbar_icons\"");
  expect_contains(root_c_make, "PREFIX \"/quader/icons\"");
  for (const std::string_view icon :
       {"grid", "gizmos", "mesh_grid", "overlays"}) {
    const std::string relative_path =
        std::string{"resources/icons/viewport_"} + std::string{icon} + ".png";
    ASSERT_TRUE(std::filesystem::exists(source_path(relative_path)))
        << relative_path;
    EXPECT_GT(std::filesystem::file_size(source_path(relative_path)), 512u)
        << relative_path;
    expect_contains(root_c_make, relative_path);
  }
}

TEST(BuildGuardrailTests, DockChromeIconsAreEmbeddedQtResources) {
  const std::string root_c_make = read_source_file("CMakeLists.txt");

  expect_contains(root_c_make,
                  "qt_add_resources(quader \"quader_dock_chrome_icons\"");
  expect_contains(root_c_make, "PREFIX \"/quader/icons\"");
  for (const std::string_view icon : {"close", "float"}) {
    for (const std::string_view suffix : {".png", "_dark.png"}) {
      const std::string relative_path =
          std::string{"resources/icons/dock_"} + std::string{icon} +
          std::string{suffix};
      ASSERT_TRUE(std::filesystem::exists(source_path(relative_path)))
          << relative_path;
      EXPECT_GT(std::filesystem::file_size(source_path(relative_path)), 512u)
          << relative_path;
      expect_contains(root_c_make, relative_path);
    }
  }
  expect_missing(root_c_make, "dock_close_dark.svg");
  expect_missing(root_c_make, "dock_float_dark.svg");
}

TEST(BuildGuardrailTests, SentryNativeRuntimeIsPackagedWithCrashpad) {
  const std::string root_c_make = read_source_file("CMakeLists.txt");
  const std::string options =
      read_source_file("cmake/QuaderBuildOptions.cmake");
  const std::string manifest = read_source_file("vcpkg.json");
  const std::string main_source = read_source_file("src/main.cpp");
  const std::string sentry_source =
      read_source_file("src/platform/sentry_client.cpp");

  expect_contains(options, "option(QUADER_ENABLE_SENTRY");
  expect_contains(manifest, "\"sentry-native\"");
  expect_contains(root_c_make, "find_package(sentry CONFIG REQUIRED)");
  expect_contains(root_c_make, "sentry_crashpad::crashpad_wer");
  expect_contains(root_c_make, "IMPORTED_IMPLIB_DEBUG");
  expect_contains(root_c_make,
                  "target_compile_definitions(quader_runtime INTERFACE "
                  "QUADER_ENABLE_SENTRY=1)");
  expect_contains(root_c_make, "src/platform/sentry_client.cpp");
  expect_contains(root_c_make, "sentry::sentry");
  expect_contains(root_c_make, "QUADER_SENTRY_CRASHPAD_HANDLER");
  expect_contains(root_c_make, "QUADER_SENTRY_CRASHPAD_ZDLL");
  expect_contains(root_c_make, "crashpad_handler.exe");
  expect_contains(root_c_make,
                  "$<TARGET_FILE_DIR:quader>/crashpad_handler.exe");
  expect_contains(root_c_make, "$<TARGET_FILE_DIR:quader>/z.dll");

  expect_contains(main_source, "initialize_sentry_client");
  expect_contains(main_source, "quader@");
  expect_contains(main_source, "sentry-native");
  expect_contains(main_source, "--sentry-verify");
  expect_contains(main_source, "--sentry-test-event");
  expect_contains(main_source, "capture_sentry_message");
  expect_contains(main_source, "shutdown_sentry_client");

  expect_contains(sentry_source, "#include <sentry.h>");
  expect_contains(sentry_source, "sentry_options_set_dsn");
  expect_contains(sentry_source, "sentry_options_set_release");
  expect_contains(sentry_source, "sentry_options_set_environment");
  expect_contains(sentry_source, "sentry_options_set_debug");
  expect_contains(sentry_source, "sentry_options_set_database_pathw");
  expect_contains(sentry_source, "sentry_options_set_handler_pathw");
  expect_contains(sentry_source, "sentry_capture_event");
  expect_contains(sentry_source, "sentry_value_new_message_event");
  expect_contains(sentry_source, "sentry_close");
}

TEST(BuildGuardrailTests, QtViewportSurfacesStayBehindRenderHostAdapters) {
  const std::string ui_system_header = read_source_file("src/ui/ui_system.hpp");
  const std::string ui_system_source = read_source_file("src/ui/ui_system.cpp");
  const std::string viewport_panel_source =
      read_source_file("src/ui/qt_viewport_panel.cpp");
  const std::string viewport_surface_header =
      read_source_file("src/ui/qt_viewport_surface.hpp");
  const std::string viewport_surface_source =
      read_source_file("src/ui/qt_viewport_surface.cpp");
  const std::string render_host_header =
      read_source_file("src/render/render_host.hpp");
  const std::string render_system_header =
      read_source_file("src/render/render_system.hpp");
  const std::string render_system_source =
      read_source_file("src/render/render_system.cpp");

  expect_missing(ui_system_header, "TextureRegistry");
  expect_missing(ui_system_header, "filament::Engine");
  expect_missing(ui_system_header, "filament::Texture");
  expect_missing(ui_system_source, "#include <filament/");
  expect_missing(ui_system_source, "filament::Texture::Builder");
  expect_missing(ui_system_source, "TextureRegistry");
  expect_missing(ui_system_source, "icon_textures_");
  expect_missing(ui_system_source, "icon_engine_");
  expect_missing(ui_system_source, "render_system.engine()");
  expect_missing(ui_system_source, "render_system.textures()");
  expect_missing(ui_system_source, "render_system.renderer()");
  expect_missing(viewport_panel_source, "render_system.engine()");
  expect_missing(viewport_panel_source, "render_system.scene()");
  expect_missing(viewport_panel_source, "render_system.textures()");
  expect_missing(viewport_panel_source, "render_system.ground_grid_material()");

  expect_contains(viewport_surface_header,
                  "class QtViewportSurface final : public QWindow");
  expect_contains(viewport_surface_header,
                  "RenderHost *render_system_");
  expect_contains(viewport_surface_header,
                  "std::unique_ptr<RenderSurface> swap_chain_");
  expect_contains(viewport_panel_source, "QWidget::createWindowContainer");
  expect_contains(viewport_surface_source, "native_window_handle()");
  expect_contains(viewport_surface_source,
                  "render_system_->create_native_surface(");
  expect_contains(viewport_surface_source,
                  "render_system_->create_composited_surface(");
  expect_contains(viewport_surface_source,
                  "render_system_->begin_frame(*swap_chain_)");
  expect_contains(viewport_surface_source,
                  "render_system_->end_frame(*swap_chain_)");
  expect_contains(viewport_surface_source,
                  "render_system_->render_composited_viewport(");
  expect_contains(viewport_surface_source, "swap_chain_.reset()");
  expect_missing(viewport_surface_source, "filament::Engine");
  expect_missing(viewport_surface_source, "filament::SwapChain");

  expect_contains(render_host_header, "class RenderSurface");
  expect_contains(render_host_header, "virtual ~RenderSurface() = default");
  expect_contains(render_host_header,
                  "virtual std::unique_ptr<RenderSurface> create_native_surface(");
  expect_contains(render_host_header,
                  "virtual std::unique_ptr<RenderSurface> create_composited_surface(");
  expect_contains(render_system_header, "class RenderSystem : public RenderHost");
  expect_contains(render_system_source,
                  "RenderSystem::create_native_surface(");
  expect_contains(render_system_source,
                  "RenderSystem::create_composited_surface(");
  expect_contains(render_system_header, "class RenderAdditionalSwapChain");
  expect_contains(render_system_header,
                  "std::shared_ptr<RenderSurfaceState> state_");
  expect_contains(render_system_source,
                  "filament::Renderer *renderer = nullptr");
  expect_contains(render_system_header, "create_additional_swap_chain");
  expect_contains(render_system_header, "initialize_viewport_renderer");
  expect_contains(render_system_source,
                  "RenderSystem::create_additional_swap_chain(");
  expect_contains(render_system_source,
                  "engine_->createSwapChain(surface.native_window, flags)");
}

TEST(BuildGuardrailTests, ActiveAppAndUiUseQtWithoutSdlAdapters) {
  const std::string root_c_make = read_source_file("CMakeLists.txt");
  const std::string ui_system_header = read_source_file("src/ui/ui_system.hpp");
  const std::string ui_system_source = read_source_file("src/ui/ui_system.cpp");
  const std::string app_source = read_source_file("src/app/app.cpp");
  const std::string ui_sources =
      cmake_target_source_blocks(root_c_make, "quader_ui");
  const std::string ui_link_block = source_slice(
      root_c_make, "target_link_libraries(quader_ui",
      "target_compile_features(quader_ui");

  expect_contains(ui_link_block, "Qt6::Widgets");
  expect_missing(ui_link_block, "SDL3::SDL3");
  expect_missing(ui_link_block, "imgui::imgui");
  for (const std::string &legacy_source : {
           "src/ui/main_menu_bar.cpp",
           "src/ui/settings_panel.cpp",
           "src/ui/ui_platform_adapter.cpp",
           "src/ui/ui_style_tokens.cpp",
           "src/ui/viewport_panel.cpp",
       }) {
    expect_missing(ui_sources, legacy_source);
  }

  for (const std::string &forbidden :
       {"SDL", "ImGui", "UIPlatformAdapter", "UIPlatformEvent"}) {
    expect_missing(ui_system_header, forbidden);
    expect_missing(ui_system_source, forbidden);
    expect_missing(app_source, forbidden);
  }

  const std::vector<std::filesystem::path> active_sources =
      cmake_target_source_files(root_c_make,
                                {"quader", "quader_app", "quader_ui"});
  ASSERT_FALSE(active_sources.empty());
  for (const std::filesystem::path &relative_path : active_sources) {
    const std::string source = read_source_file(relative_path);
    for (const std::string &forbidden :
         {"#include <SDL", "SDL_", "#include <imgui", "ImGui::",
          "ImGui_Impl"}) {
      EXPECT_EQ(source.find(forbidden), std::string::npos)
          << relative_path.generic_string() << ": " << forbidden;
    }
  }
}

TEST(BuildGuardrailTests, AppLoopRunsOnQApplicationEvents) {
  const std::string main_source = read_source_file("src/main.cpp");
  const std::string app_header = read_source_file("src/app/app.hpp");
  const std::string app_source = read_source_file("src/app/app.cpp");

  expect_contains(main_source, "QApplication qt_application(argc, argv)");
  expect_contains(app_header, "bool initialize(QApplication &application");
  expect_contains(app_header, "QApplication *application_ = nullptr");
  expect_contains(app_source, "#include <QApplication>");
  expect_contains(app_source, "#include <QTimer>");
  expect_contains(app_source, "&QTimer::timeout");
  expect_contains(app_source, "&QApplication::aboutToQuit");
  expect_contains(app_source,
                  "&QApplication::aboutToQuit, frame_timer_.get()");
  expect_missing(app_source,
                 "&QApplication::aboutToQuit, application_");
  expect_contains(app_source, "return application_->exec()");
  expect_contains(app_source, "frame_timer_->start(0)");

  for (const std::string &legacy_event_path :
       {"PlatformEvent", "SDL_PollEvent", "SDL_WaitEventTimeout",
        "platform_wait_for_event_timeout", "native_chrome_interaction"}) {
    expect_missing(app_header, legacy_event_path);
    expect_missing(app_source, legacy_event_path);
  }
}

TEST(BuildGuardrailTests, QtShellUsesMainWindowDocksAndStableNames) {
  const std::string ui_system_header = read_source_file("src/ui/ui_system.hpp");
  const std::string ui_system_source = read_source_file("src/ui/ui_system.cpp");
  const std::string panel_header = read_source_file("src/ui/ui_panel.hpp");
  const std::string panel_manager_header =
      read_source_file("src/ui/ui_panel_manager.hpp");
  const std::string panel_manager_source =
      read_source_file("src/ui/ui_panel_manager.cpp");
  const std::string settings_dialog_header =
      read_source_file("src/ui/qt_settings_dialog.hpp");
  const std::string tool_panel_header =
      read_source_file("src/ui/qt_tool_settings_panel.hpp");

  expect_contains(ui_system_header, "class QMainWindow");
  expect_contains(ui_system_header, "class QDockWidget");
  expect_contains(ui_system_header, "std::unique_ptr<QMainWindow> main_window_");
  expect_contains(ui_system_source, "#include <QMainWindow>");
  expect_contains(ui_system_source, "#include <QDockWidget>");
  expect_contains(ui_system_source,
                  "auto main_window = std::make_unique<QtMainWindow>()");
  expect_contains(ui_system_source,
                  "main_window->set_close_request_handler(");
  expect_contains(ui_system_source,
                  "main_window_->setObjectName(QStringLiteral("
                  "\"quaderMainWindow\"))");
  expect_contains(ui_system_header, "class QStackedWidget");
  expect_contains(ui_system_header, "QStackedWidget *central_stack_");
  expect_contains(ui_system_header, "QtProjectHomePage *project_home_page_");
  expect_contains(ui_system_header, "QtSceneWorkspace *scene_workspace_");
  expect_contains(ui_system_source, "#include <QStackedWidget>");
  expect_contains(ui_system_source,
                  "central_stack_ = new QStackedWidget(main_window_.get())");
  expect_contains(ui_system_source,
                  "project_home_page_ = new QtProjectHomePage(central_stack_)");
  expect_contains(ui_system_source,
                  "scene_workspace_ = new QtSceneWorkspace(central_stack_)");
  expect_contains(ui_system_source,
                  "scene_workspace_->set_viewport_widget(viewport_container)");
  expect_contains(ui_system_source,
                  "viewport_panel_ = new QtViewportPanel(viewport_container, "
                  "composed_viewports)");
  expect_contains(ui_system_source,
                  "viewport_layout->addWidget(viewport_panel_, 1)");
  expect_contains(ui_system_source,
                  "central_stack_->addWidget(project_home_page_)");
  expect_contains(ui_system_source,
                  "central_stack_->addWidget(scene_workspace_)");
  expect_contains(ui_system_source,
                  "main_window_->setCentralWidget(central_stack_)");
  expect_contains(ui_system_source, "QMainWindow::AllowNestedDocks");
  expect_contains(ui_system_source, "QMainWindow::AllowTabbedDocks");

  expect_contains(ui_system_source,
                  "UIPanelId::Outliner, *outliner_panel_, "
                  "Qt::LeftDockWidgetArea");
  expect_contains(ui_system_source,
                  "UIPanelId::Inspector, *inspector_panel_, "
                  "Qt::RightDockWidgetArea");
  expect_contains(ui_system_source,
                  "UIPanelId::Layers, *layer_panel_, "
                  "Qt::RightDockWidgetArea");
  expect_contains(ui_system_source,
                  "UIPanelId::Console, *console_panel_, "
                  "Qt::BottomDockWidgetArea");
  expect_contains(ui_system_source,
                  "viewport_panel_->set_settings_panel(tool_settings_panel_)");
  expect_missing(ui_system_source, "UIPanelId::ToolSettings");
  expect_contains(ui_system_source, "new QDockWidget(");
  expect_contains(ui_system_source,
                  "QStringLiteral(\"dock_%1\").arg("
                  "QString::fromUtf8(ui_panel_stable_id(id)))");
  expect_contains(ui_system_source, "main_window_->addDockWidget(");
  expect_contains(ui_system_source,
                  "main_window_->saveState(kQtWindowStateVersion)");
  expect_contains(ui_system_source,
                  "main_window_->restoreState(state, kQtWindowStateVersion)");

  expect_contains(panel_header, "return \"viewport\"");
  expect_contains(panel_header, "return \"outliner\"");
  expect_contains(panel_header, "return \"inspector\"");
  expect_contains(panel_header, "return \"console\"");
  expect_contains(panel_header, "return \"layers\"");
  expect_missing(panel_header, "return \"tool_settings\"");
  expect_contains(panel_header, "kUiDockPanelIds");
  expect_contains(panel_manager_header,
                  "std::array<QDockWidget *, kUiPanelCount> docks_");
  expect_contains(panel_manager_source, "void UIPanelManager::add(");
  expect_contains(panel_manager_source, "QDockWidget *UIPanelManager::dock(");

  expect_contains(settings_dialog_header,
                  "class QtSettingsDialog final : public QDialog");
  expect_contains(tool_panel_header,
                  "class QtToolSettingsPanel final : public QWidget");
  expect_contains(ui_system_source,
                  "new QtSettingsDialog(main_window_.get())");
  expect_contains(ui_system_source,
                  "new QtToolSettingsPanel(main_window_.get())");
}

TEST(BuildGuardrailTests, QtFrameRefreshKeepsExpensiveChromeEventDriven) {
  const std::string ui_system_source = read_source_file("src/ui/ui_system.cpp");
  const std::string tool_settings_panel_source =
      read_source_file("src/ui/qt_tool_settings_panel.cpp");
  const std::string panel_manager_source =
      read_source_file("src/ui/ui_panel_manager.cpp");
  const std::string main_menu_source =
      read_source_file("src/ui/qt_main_menu.cpp");
  const std::string primitive_creation_source = read_source_file(
      "src/editor/modeling/services/primitive_creation_tool_service.cpp");
  const std::string frame_refresh = source_slice(
      ui_system_source, "void UISystem::refresh(",
      "void UISystem::prepare_viewports(");
  const std::string panel_presented = source_slice(
      ui_system_source, "bool UISystem::panel_presented(UIPanelId id) const",
      "void UISystem::set_panel_visible(");
  const std::string primitive_reset_availability = source_slice(
      primitive_creation_source,
      "bool PrimitiveCreationToolService::can_reset_settings() const",
      "bool PrimitiveCreationToolService::live_primitive_is_selected() const");
  const std::string settings_show = source_slice(
      ui_system_source, "void UISystem::show_settings_dialog()",
      "} // namespace quader");
  const std::string workspace_refresh = source_slice(
      ui_system_source, "void UISystem::refresh_project_workspace(",
      "void UISystem::refresh_face_texture_material(");
  const std::string show_home = source_slice(
      ui_system_source, "void UISystem::show_home()",
      "void UISystem::show_workspace()");
  const std::string show_workspace = source_slice(
      ui_system_source, "void UISystem::show_workspace()",
      "QuaderDirtyProjectDecision");
  const std::string status_refresh = source_slice(
      ui_system_source, "void UISystem::refresh_status_bar()",
      "void UISystem::present_lifecycle_result(");
  const std::string number_dispatch_lifecycle = source_slice(
      tool_settings_panel_source,
      "bool number_dispatch_lifecycle_is_current(",
      "void rebind_pending_number_dispatches_to_current_revision(");

  expect_contains(frame_refresh, "outliner_panel_->refresh(context)");
  expect_contains(frame_refresh, "layer_panel_->refresh(context)");
  expect_contains(frame_refresh, "inspector_panel_->refresh(context)");
  expect_contains(frame_refresh, "console_panel_->refresh(context)");
  expect_contains(frame_refresh, "modeling_->transform_drag_active()");
  expect_contains(
      frame_refresh,
      "frame_native_tools.curve_dragging");
  expect_contains(frame_refresh,
                  "!edit_drag_active && "
                  "panel_presented(UIPanelId::Outliner)");
  expect_contains(frame_refresh,
                  "document_->hierarchy_presentation_revision()");
  expect_contains(frame_refresh,
                  "const OutlinerPresentationStamp stamp");
  expect_contains(frame_refresh,
                  "outliner_presenter_.needs_refresh(stamp)");
  expect_contains(frame_refresh,
                  "!edit_drag_active && "
                  "panel_presented(UIPanelId::Layers)");
  expect_contains(frame_refresh,
                  "const LayersPresentationStamp stamp");
  expect_contains(frame_refresh,
                  "layers_presenter_.needs_refresh(stamp)");
  expect_missing(frame_refresh, "editor_->revisions()");
  expect_contains(frame_refresh,
                  "!edit_drag_active && !numeric_interaction_active &&");
  expect_contains(frame_refresh, "panel_presented(UIPanelId::Inspector)");
  expect_contains(frame_refresh, "panel_presented(UIPanelId::Console)");
  expect_contains(frame_refresh,
                  "!numeric_interaction_active || settings_lifecycle_changed");
  expect_contains(frame_refresh,
                  "viewport_panel_->refresh_settings_overlay(*editor_)");
  expect_contains(frame_refresh,
                  "presented_settings->operation_serial != "
                  "settings_operation_serial");
  expect_contains(frame_refresh,
                  "presented_settings->active_tool_revision != "
                  "frame_active_tool_revision");
  expect_contains(frame_refresh,
                  "presented_settings->operation_preview_active !=\n"
                  "          frame_operation_preview_active");
  expect_contains(frame_refresh,
                  "presented_settings->curve_controls_editable !=\n"
                  "          frame_native_tools.curve_controls_editable");
  expect_contains(frame_refresh, "refresh_project_workspace(false)");
  expect_missing(frame_refresh, "settings_dialog_->refresh()");
  expect_missing(frame_refresh, "main_menu_->refresh()");
  expect_missing(frame_refresh, "viewport_panel_->refresh_workspace_layout()");
  expect_missing(frame_refresh, "synchronize_auxiliary_window_visibility()");
  expect_missing(frame_refresh, "modeling_->selection_summary()");
  expect_contains(frame_refresh, "modeling_->content_revision()");
  expect_contains(frame_refresh, "modeling_->selection_revision()");
  expect_missing(frame_refresh, "panel_visible(UIPanelId::Outliner)");
  expect_missing(frame_refresh, "panel_visible(UIPanelId::Layers)");
  expect_missing(frame_refresh, "panel_visible(UIPanelId::Inspector)");
  expect_missing(frame_refresh, "panel_visible(UIPanelId::Console)");
  for (const std::string &broad_workspace_query :
       {"recent_projects()", "open_scenes()", ".materials()",
        ".active_material()", ".metadata()", ".material_preview(",
        ".material_presentations()"}) {
    expect_missing(frame_refresh, broad_workspace_query);
  }

  expect_contains(panel_manager_source,
                  "&QDockWidget::visibilityChanged");
  expect_contains(panel_manager_source, "presented_[presented_slot] = visible");
  expect_contains(panel_presented, "return panels_.is_presented(id)");
  expect_missing(ui_system_source, "visibleRegion().isEmpty()");

  const auto expect_presented_before =
      [&](std::string_view panel_id, std::string_view expensive_work) {
        const std::size_t presented =
            frame_refresh.find(std::string{"panel_presented(UIPanelId::"} +
                               std::string{panel_id} + ")");
        const std::size_t work = frame_refresh.find(expensive_work, presented);
        ASSERT_NE(presented, std::string::npos) << panel_id;
        ASSERT_NE(work, std::string::npos) << expensive_work;
        EXPECT_LT(presented, work) << panel_id;
      };
  expect_presented_before("Outliner", "outliner_panel_->refresh(context)");
  expect_presented_before("Layers", "layer_panel_->refresh(context)");
  expect_presented_before("Inspector", "modeling_->content_revision()");
  expect_presented_before("Inspector", "inspector_panel_->refresh(context)");
  expect_presented_before("Console", "console_panel_->refresh(context)");
  expect_presented_before("FaceTexture", "face_texture_panel_->refresh()");

  const std::size_t instance_edit_revision =
      workspace_refresh.find("instance_edit_revision()");
  const std::size_t workspace_gate = workspace_refresh.find(
      "project_workspace_refresh_signal_->exchange(");
  const std::size_t standard_material_refresh =
      workspace_refresh.find("refresh_standard_material_editor()");
  ASSERT_NE(instance_edit_revision, std::string::npos);
  ASSERT_NE(workspace_gate, std::string::npos);
  ASSERT_NE(standard_material_refresh, std::string::npos);
  EXPECT_LT(instance_edit_revision, workspace_gate);
  EXPECT_LT(workspace_gate, standard_material_refresh);
  for (std::string_view broad_workspace_query :
       {"recent_projects()", "open_scenes()"}) {
    const std::size_t query = workspace_refresh.find(broad_workspace_query);
    ASSERT_NE(query, std::string::npos) << broad_workspace_query;
    EXPECT_LT(workspace_gate, query) << broad_workspace_query;
  }
  const std::size_t material_browser_visibility = workspace_refresh.find(
      "material_browser_window_->isVisible()");
  const std::size_t material_catalog = workspace_refresh.find(
      "refresh_material_presentation_cache()");
  ASSERT_NE(material_browser_visibility, std::string::npos);
  ASSERT_NE(material_catalog, std::string::npos);
  EXPECT_LT(material_browser_visibility, material_catalog);
  expect_contains(workspace_refresh, "panel_presented(UIPanelId::FaceTexture)");
  expect_missing(workspace_refresh, ".active_material()");
  expect_missing(workspace_refresh, ".materials()");
  expect_missing(workspace_refresh, ".material_preview(");
  expect_missing(workspace_refresh, ".material_presentations()");
  const std::string asset_browser_source =
      read_source_file("src/ui/ui_system_asset_browser.cpp");
  const std::string browser_refresh = source_slice(
      asset_browser_source, "void UISystem::refresh_asset_browser(",
      "void UISystem::toggle_asset_browser(");
  const auto asset_revision = browser_refresh.find("asset_revision()");
  const auto asset_first_paint = browser_refresh.find(
      "asset_browser_->has_completed_visible_paint()");
  const auto asset_discovery_gate = browser_refresh.find(
      "const bool refresh_files = force || reveal_ready");
  const auto asset_gate = browser_refresh.find("if (changed)");
  const auto asset_snapshot = browser_refresh.find("asset_browser_snapshot(");
  ASSERT_NE(asset_revision, std::string::npos);
  ASSERT_NE(asset_first_paint, std::string::npos);
  ASSERT_NE(asset_discovery_gate, std::string::npos);
  ASSERT_NE(asset_gate, std::string::npos);
  ASSERT_NE(asset_snapshot, std::string::npos);
  EXPECT_LT(asset_revision, asset_first_paint);
  EXPECT_LT(asset_first_paint, asset_discovery_gate);
  EXPECT_LT(asset_discovery_gate, asset_gate);
  EXPECT_LT(asset_gate, asset_snapshot);
  expect_contains(browser_refresh, "asset_browser_discovery_pending_");
  expect_contains(browser_refresh, "panel_presented(UIPanelId::AssetBrowser)");
  expect_missing(browser_refresh, ".assets(");
  expect_missing(browser_refresh, "commands().available()");
  const std::string workspace_event_bindings = source_slice(
      ui_system_source, "void UISystem::bind_project_workspace_events(",
      "void UISystem::clear_project_workspace_events(");
  for (const std::string &material_event :
       {"kStandardMaterialOpened", "kStandardMaterialDirtyChanged",
        "kStandardMaterialSaved", "kStandardMaterialClosed"}) {
    expect_contains(workspace_event_bindings, material_event);
  }
  expect_contains(ui_system_source, "bind_project_workspace_events(editor)");
  expect_contains(ui_system_source,
                  "project_workspace_refresh_signal_->store(true");
  expect_contains(ui_system_source, "clear_project_workspace_events()");
  expect_contains(ui_system_source,
                  "configure_asset_browser()");
  expect_contains(ui_system_source,
                  "configure_material_browser(face_texture_panel_->browser())");
  const std::string face_material_refresh = source_slice(
      ui_system_source, "void UISystem::refresh_face_texture_material(",
      "void UISystem::configure_material_browser(");
  const std::size_t face_material_gate = face_material_refresh.find(
      "face_texture_panel_presenter_.needs_refresh(stamp)");
  const std::size_t face_material_setter = face_material_refresh.find(
      "face_texture_panel_->set_material_presentation(");
  ASSERT_NE(face_material_gate, std::string::npos);
  ASSERT_NE(face_material_setter, std::string::npos);
  EXPECT_LT(face_material_gate, face_material_setter);
  expect_missing(workspace_refresh, "last_status_revision()");

  const std::size_t home_unchanged =
      show_home.find("if (home_visible_)");
  const std::size_t home_setter = show_home.find("setCurrentWidget(");
  ASSERT_NE(home_unchanged, std::string::npos);
  ASSERT_NE(home_setter, std::string::npos);
  EXPECT_LT(home_unchanged, home_setter);
  expect_contains(show_home, "material_texture_info_window_->clear()");
  expect_contains(show_home, "material_texture_info_window_->hide()");
  const std::size_t workspace_unchanged =
      show_workspace.find("if (!presentation_changed)");
  const std::size_t workspace_setter =
      show_workspace.find("setCurrentWidget(");
  const std::size_t panel_settings =
      show_workspace.find("panels_.apply_settings(settings_)");
  ASSERT_NE(workspace_unchanged, std::string::npos);
  ASSERT_NE(workspace_setter, std::string::npos);
  ASSERT_NE(panel_settings, std::string::npos);
  EXPECT_LT(workspace_unchanged, workspace_setter);
  EXPECT_LT(workspace_unchanged, panel_settings);
  const std::size_t viewport_initialize_failure =
      show_workspace.find("Unable to initialize the native viewport surface");
  const std::size_t viewport_initialize_retry = show_workspace.find(
      "project_workspace_refresh_signal_->store(true",
      viewport_initialize_failure);
  ASSERT_NE(viewport_initialize_failure, std::string::npos);
  ASSERT_NE(viewport_initialize_retry, std::string::npos);
  EXPECT_LT(viewport_initialize_failure, viewport_initialize_retry);

  const std::size_t status_revision =
      status_refresh.find("last_status_revision()");
  const std::size_t status_gate =
      status_refresh.find("if (!status_presenter_.needs_refresh(stamp))");
  const std::size_t status_copy = status_refresh.find("last_status()", 1u);
  const std::size_t status_setter = status_refresh.find("status_message_->present(");
  ASSERT_NE(status_revision, std::string::npos);
  ASSERT_NE(status_gate, std::string::npos);
  ASSERT_NE(status_copy, std::string::npos);
  ASSERT_NE(status_setter, std::string::npos);
  EXPECT_LT(status_revision, status_gate);
  EXPECT_LT(status_gate, status_copy);
  EXPECT_LT(status_gate, status_setter);

  const std::size_t tool_settings_revision =
      frame_refresh.find("settings_revision(frame_focused_pane)");
  const std::size_t tool_settings_gate =
      frame_refresh.find("if (tool_settings_panel_ != nullptr &&");
  const std::size_t tool_settings_refresh =
      frame_refresh.find("tool_settings_panel_->refresh()",
                         tool_settings_gate);
  const std::size_t numeric_interaction_gate =
      frame_refresh.find("!numeric_interaction_active", tool_settings_gate);
  ASSERT_NE(tool_settings_revision, std::string::npos);
  ASSERT_NE(tool_settings_gate, std::string::npos);
  ASSERT_NE(numeric_interaction_gate, std::string::npos);
  ASSERT_NE(tool_settings_refresh, std::string::npos);
  EXPECT_LT(tool_settings_revision, tool_settings_gate);
  EXPECT_LT(numeric_interaction_gate, tool_settings_refresh);
  EXPECT_LT(tool_settings_gate, tool_settings_refresh);
  expect_missing(frame_refresh, "tools().active_settings()");
  expect_contains(number_dispatch_lifecycle, "settings_revision(target_pane_)");
  expect_contains(number_dispatch_lifecycle, "selected_tool_revision(target_pane_)");
  expect_missing(number_dispatch_lifecycle, "active_settings()");
  expect_contains(primitive_reset_availability,
                  "modeling_.box_drag_state()");
  expect_missing(primitive_reset_availability, "box_preview(");
  expect_contains(main_menu_source, "&QMenu::aboutToShow");
  expect_contains(settings_show, "settings_dialog_->refresh()");
}

TEST(BuildGuardrailTests,
     MaterialPresentationCatalogAndThumbnailsStayRevisionGated) {
  const std::string ui_system = read_source_file("src/ui/ui_system.cpp");
  const std::string ui_system_header =
      read_source_file("src/ui/ui_system.hpp");
  const std::string asset_registry =
      read_source_file("src/assets/asset_registry.cpp");
  const std::string asset_registry_header =
      read_source_file("src/assets/asset_registry.hpp");
  const std::string scene_document =
      read_source_file("src/scene/scene_document.cpp");
  const std::string scene_document_header =
      read_source_file("src/scene/scene_document.hpp");
  const std::string inspector =
      read_source_file("src/ui/inspector_panel.cpp");
  const std::string inspector_header =
      read_source_file("src/ui/inspector_panel.hpp");
  const std::string browser_header =
      read_source_file("src/ui/qt_material_browser_window.hpp");
  const std::string presentation_header =
      read_source_file("src/ui/qt_material_presentation.hpp");
  const std::string project_assets_api =
      read_source_file("src/api/quader_project_assets_api.cpp");
  const std::string material_presentation_service = read_source_file(
      "src/editor/material/material_presentation_service.cpp");
  const std::string retained_presenter =
      read_source_file("src/ui/retained_ui_presenter.hpp");

  const std::string cache_refresh = source_slice(
      ui_system, "void UISystem::refresh_material_presentation_cache(",
      "void UISystem::revalidate_material_find_replace_presentations()");
  ASSERT_FALSE(cache_refresh.empty());
  const std::size_t material_catalog_revision =
      cache_refresh.find("material_presentation_revision()");
  const std::size_t material_binding_revision =
      cache_refresh.find("document_->material_binding_revision()");
  const std::size_t mesh_binding_revision =
      cache_refresh.find("document_->mesh_binding_revision()");
  const std::size_t mesh_material_usage_revision =
      cache_refresh.find("assets_->mesh_material_usage_revision()");
  const std::size_t modeling_content_revision =
      cache_refresh.find("modeling_->content_revision()");
  const std::size_t modeling_object_map_revision =
      cache_refresh.find("modeling_->modeling_object_map_revision()");
  const std::size_t unchanged_gate = cache_refresh.find(
      "material_presentation_catalog_attempt_.has_value()");
  const std::size_t record_attempt = cache_refresh.find(
      "material_presentation_catalog_attempt_ = catalog_stamp");
  const std::size_t catalog_query =
      cache_refresh.find("editor_->project_assets().material_presentations()");
  const std::size_t thumbnail_decode =
      cache_refresh.find("make_qt_material_thumbnail(");
  ASSERT_NE(material_catalog_revision, std::string::npos);
  ASSERT_NE(material_binding_revision, std::string::npos);
  ASSERT_NE(mesh_binding_revision, std::string::npos);
  ASSERT_NE(mesh_material_usage_revision, std::string::npos);
  ASSERT_NE(modeling_content_revision, std::string::npos);
  ASSERT_NE(modeling_object_map_revision, std::string::npos);
  ASSERT_NE(unchanged_gate, std::string::npos);
  ASSERT_NE(record_attempt, std::string::npos);
  ASSERT_NE(catalog_query, std::string::npos);
  ASSERT_NE(thumbnail_decode, std::string::npos);
  EXPECT_LT(material_catalog_revision, unchanged_gate);
  EXPECT_LT(material_binding_revision, unchanged_gate);
  EXPECT_LT(mesh_binding_revision, unchanged_gate);
  EXPECT_LT(mesh_material_usage_revision, unchanged_gate);
  EXPECT_LT(modeling_content_revision, unchanged_gate);
  EXPECT_LT(modeling_object_map_revision, unchanged_gate);
  EXPECT_LT(unchanged_gate, catalog_query);
  EXPECT_LT(record_attempt, catalog_query)
      << "failed catalog reads must retain the attempted input stamp";
  EXPECT_LT(unchanged_gate, thumbnail_decode);
  expect_missing(cache_refresh, "assets_->revision()");
  expect_missing(cache_refresh, "document_->revision()");
  expect_missing(cache_refresh,
                 "editor_->materials().used_materials(");
  expect_missing(cache_refresh, "assets_->material_ids()");
  expect_missing(cache_refresh, ".material_preview(");
  expect_missing(cache_refresh, "project_assets().materials()");
  expect_contains(cache_refresh,
                  "material_presentation_signature_");
  expect_contains(cache_refresh,
                  "material_presentation_items_ = std::move(next)");
  expect_contains(cache_refresh, "++material_presentation_revision_");
  expect_contains(cache_refresh,
                  "synchronize_material_presentation_consumers()");
  expect_contains(project_assets_api,
                  "QuaderProjectAssetsApi::material_presentations()");
  expect_contains(project_assets_api,
                  "MaterialPresentationService::snapshot(");
  expect_contains(project_assets_api,
                  "MaterialPresentationService::revision(");
  expect_contains(material_presentation_service,
                  "scene->assets().material_ids()");
  expect_contains(material_presentation_service,
                  "material_catalog().entries()");
  expect_missing(project_assets_api, "scene->assets().material_ids()");
  expect_contains(retained_presenter, "class RetainedUiPresenter final");
  expect_contains(ui_system_header,
                  "RetainedUiPresenter<OutlinerPresentationStamp>");
  expect_contains(ui_system_header,
                  "RetainedUiPresenter<InspectorPresentationStamp>");
  expect_contains(ui_system_header,
                  "RetainedUiPresenter<InspectorMaterialQueryStamp>");
  expect_contains(
      ui_system_header,
      "RetainedUiPresenter<StandardMaterialEditorPresentationStamp>");
  expect_missing(ui_system_header, "inspector_material_query_valid_");
  expect_missing(ui_system_header,
                 "standard_material_editor_refresh_valid_");

  for (std::string_view cache_field : {
           "material_presentation_items_",
           "material_presentation_signature_",
           "material_presentation_revision_",
           "material_presentation_catalog_revision_",
           "material_presentation_catalog_attempt_",
           "material_presentation_context_observation_",
           "material_presentation_event_generation_signal_",
           "material_presentation_cache_valid_",
       }) {
    expect_contains(ui_system_header, std::string(cache_field));
  }
  expect_contains(presentation_header,
                  "std::shared_ptr<const QtMaterialThumbnail> thumbnail");
  expect_contains(inspector_header,
                  "std::vector<QtMaterialPresentationItem> materials");
  expect_contains(browser_header,
                  "using QtMaterialBrowserItem = QtMaterialPresentationItem");
  expect_missing(inspector, ".material_preview(");
  expect_missing(inspector, "project_assets().materials(");
  expect_missing(read_source_file("src/ui/qt_material_browser_window.cpp"),
                 ".material_preview(");
  expect_contains(asset_registry_header, "material_catalog_revision() const");
  expect_contains(asset_registry_header,
                  "mesh_material_usage_revision() const");
  expect_contains(asset_registry, "void AssetRegistry::mark_material_dirty()");
  expect_contains(
      asset_registry,
      "void AssetRegistry::mark_mesh_material_usage_dirty()");
  expect_contains(asset_registry,
                  "bool AssetRegistry::material_references_texture(");
  for (const std::pair<std::string_view, std::string_view> mesh_mutator : {
           std::pair{"AssetId AssetRegistry::add_mesh(MeshAsset mesh)",
                     "AssetId AssetRegistry::add_mesh(AssetId id"},
           std::pair{"bool AssetRegistry::remove_mesh(",
                     "bool AssetRegistry::remove_material("},
           std::pair{"bool AssetRegistry::replace_meshes(",
                     "bool AssetRegistry::replace_material("},
           std::pair{"bool AssetRegistry::update_mesh(",
                     "bool AssetRegistry::update_material("},
       }) {
    const std::string mutation = source_slice(
        asset_registry, mesh_mutator.first, mesh_mutator.second);
    expect_contains(mutation, "mark_modeling_assets_dirty(");
    expect_contains(mutation, "true);");
  }
  const std::string modeling_asset_dirty = source_slice(
      asset_registry, "void AssetRegistry::mark_modeling_assets_dirty(",
      "bool AssetRegistry::material_references_texture(");
  expect_contains(modeling_asset_dirty, "if (mesh_material_usage_dirty)");
  expect_contains(modeling_asset_dirty, "++mesh_material_usage_revision_");
  for (const std::pair<std::string_view, std::string_view> texture_mutator : {
           std::pair{"bool AssetRegistry::remove_texture(",
                     "bool AssetRegistry::replace_mesh("},
           std::pair{"bool AssetRegistry::replace_texture(",
                     "bool AssetRegistry::update_mesh("},
           std::pair{"bool AssetRegistry::update_texture(",
                     "const MeshAsset* AssetRegistry::mesh("},
       }) {
    const std::string mutation = source_slice(
        asset_registry, texture_mutator.first, texture_mutator.second);
    expect_contains(mutation, "material_references_texture(id)");
    expect_contains(mutation, "mark_material_dirty()");
  }
  expect_contains(scene_document_header,
                  "material_binding_revision() const");
  expect_contains(scene_document,
                  "++material_binding_revision_");
  const std::string instance_retarget = source_slice(
      scene_document, "bool SceneDocument::set_instance_occurrence(",
      "bool SceneDocument::clear_instance_occurrence(");
  const std::size_t instance_idempotent =
      instance_retarget.find("current != nullptr && *current == occurrence");
  const std::size_t instance_hierarchy_revision =
      instance_retarget.find("++hierarchy_presentation_revision_");
  ASSERT_NE(instance_idempotent, std::string::npos);
  ASSERT_NE(instance_hierarchy_revision, std::string::npos);
  EXPECT_LT(instance_idempotent, instance_hierarchy_revision);
  expect_missing(instance_retarget, "if (kind_changed)");

  const std::string workspace_events = source_slice(
      ui_system, "void UISystem::bind_project_workspace_events(",
      "void UISystem::clear_project_workspace_events(");
  const std::string workspace_refresh = source_slice(
      ui_system, "void UISystem::refresh_project_workspace(",
      "void UISystem::refresh_material_presentation_cache(");
  expect_contains(workspace_events, "kProjectOpened");
  expect_contains(workspace_events, "kProjectClosed");
  expect_contains(workspace_events, "kProjectAssetsChanged");
  expect_missing(workspace_events, "kActiveProjectMaterialChanged");
  expect_contains(workspace_events, "kActiveSceneChanged");
  expect_contains(workspace_events,
                  "refresh_signal->store(true, std::memory_order_relaxed)");
  expect_contains(workspace_events,
                  "material_signal->fetch_add(1u");
  const std::size_t workspace_gate = workspace_refresh.find(
      "project_workspace_refresh_signal_->exchange(");
  const std::size_t hidden_event_observation = workspace_refresh.find(
      "observe_material_presentation_events()");
  const std::size_t cache_refresh_call = workspace_refresh.find(
      "refresh_material_presentation_cache()");
  ASSERT_NE(workspace_gate, std::string::npos);
  ASSERT_NE(hidden_event_observation, std::string::npos);
  ASSERT_NE(cache_refresh_call, std::string::npos);
  EXPECT_LT(hidden_event_observation, workspace_gate)
      << "material invalidation must be observed even while consumers are "
         "hidden";
  EXPECT_LT(workspace_gate, cache_refresh_call);
  expect_missing(workspace_refresh,
                 "refresh_material_presentation_cache(true)");

  const std::string frame_refresh = source_slice(
      ui_system, "void UISystem::refresh(",
      "void UISystem::prepare_viewports(");
  const std::size_t inspector_presented = frame_refresh.find(
      "panel_presented(UIPanelId::Inspector)");
  const std::size_t outer_material_gate = frame_refresh.find(
      "inspector_presenter_.needs_refresh(stamp)",
      inspector_presented);
  const std::size_t outer_inspector_refresh =
      frame_refresh.find("inspector_panel_->refresh(context)",
                         inspector_presented);
  ASSERT_NE(inspector_presented, std::string::npos);
  ASSERT_NE(outer_material_gate, std::string::npos);
  ASSERT_NE(outer_inspector_refresh, std::string::npos);
  EXPECT_LT(inspector_presented, outer_material_gate);
  EXPECT_LT(outer_material_gate, outer_inspector_refresh);
  expect_contains(frame_refresh,
                  ".material_presentation_revision = "
                  "material_presentation_revision_");
  expect_contains(frame_refresh,
                  "inspector_presenter_.mark_presented(stamp)");
  expect_missing(frame_refresh, "project_assets().materials()");
  expect_missing(frame_refresh, ".material_preview(");

  const std::string inspector_material_refresh = source_slice(
      ui_system, "void UISystem::refresh_inspector_material_presentation()",
      "void UISystem::show_home()");
  const std::size_t inspector_material_gate =
      inspector_material_refresh.find(
          "if (!inspector_material_query_presenter_.needs_refresh(stamp))");
  const std::size_t selection_usage_query = inspector_material_refresh.find(
      "editor_->materials().used_materials(");
  ASSERT_NE(inspector_material_gate, std::string::npos);
  ASSERT_NE(selection_usage_query, std::string::npos);
  EXPECT_LT(inspector_material_gate, selection_usage_query);
  expect_contains(inspector_material_refresh,
                  "document_->hierarchy_presentation_revision()");
  expect_contains(inspector_material_refresh,
                  "document_->material_binding_revision()");
  expect_contains(inspector_material_refresh,
                  "document_->mesh_binding_revision()");
  expect_contains(inspector_material_refresh,
                  "assets_->mesh_material_usage_revision()");
  expect_contains(inspector_material_refresh,
                  "modeling_->modeling_object_map_revision()");
  expect_contains(inspector_material_refresh,
                  "inspector_material_query_presenter_.mark_presented(stamp)");

  const std::string inspector_refresh = source_slice(
      inspector, "void refresh(UIContext &context)", "private:");
  const std::size_t inner_material_gate = inspector_refresh.find(
      "built_material_presentation_revision_ !=");
  const std::size_t inner_rebuild = inspector_refresh.find("rebuild(summary)");
  ASSERT_NE(inner_material_gate, std::string::npos);
  ASSERT_NE(inner_rebuild, std::string::npos);
  EXPECT_LT(inner_material_gate, inner_rebuild);
  expect_contains(inspector,
                  "built_material_presentation_revision_ = "
                  "material_presentation_.revision");
}

TEST(BuildGuardrailTests,
     StandardMaterialRenderAdaptersRespectApiAndUiOwnership) {
  const std::string editor_header = read_source_file(
      "src/editor/render/standard_material_render_snapshot_adapter.hpp");
  const std::string editor_source = read_source_file(
      "src/editor/render/standard_material_render_snapshot_adapter.cpp");
  const std::string ui_header = read_source_file(
      "src/ui/standard_material_preview_render_snapshot_adapter.hpp");
  const std::string ui_source = read_source_file(
      "src/ui/standard_material_preview_render_snapshot_adapter.cpp");
  const std::string cmake = read_source_file("CMakeLists.txt");
  const std::string editor_api_target = source_slice(
      cmake, "add_library(quader_editor_api STATIC",
      "quader_enable_common_pch(quader_editor_api)");
  const std::string project_session_target = source_slice(
      cmake, "add_library(quader_project_session STATIC",
      "quader_configure_core_module(quader_project_session)");
  const std::string ui_target = source_slice(
      cmake, "add_library(quader_ui STATIC",
      "quader_enable_common_pch(quader_ui)");

  expect_missing(editor_header, "#include \"api/");
  expect_missing(editor_source, "#include \"api/");
  expect_missing(editor_header, "QuaderStandardMaterial");
  expect_missing(editor_source, "QuaderStandardMaterial");
  expect_contains(editor_header, "StandardMaterialRenderValues");
  expect_contains(editor_source,
                  "make_standard_material_render_evaluation(");
  expect_contains(ui_header,
                  "standard_material_preview_render_snapshot(");
  expect_contains(
      ui_source,
      "#include \"api/quader_standard_material_documents_api.hpp\"");
  expect_contains(ui_source,
                  "make_standard_material_render_evaluation(");
  expect_contains(ui_source, "preview.session_id == document.session_id");
  expect_contains(ui_source, "preview.id == document.id");
  expect_contains(ui_source,
                  "preview.draft_revision == document.draft_revision");
  expect_contains(
      project_session_target,
      "src/editor/render/standard_material_render_snapshot_adapter.cpp");
  expect_missing(
      editor_api_target,
      "src/editor/render/standard_material_render_snapshot_adapter.cpp");
  expect_missing(editor_api_target,
                 "standard_material_preview_render_snapshot_adapter.cpp");
  expect_contains(
      ui_target,
      "src/ui/standard_material_preview_render_snapshot_adapter.cpp");
}

TEST(BuildGuardrailTests,
     LocalizedChromeObservesCatalogRevisionAsWellAsLocaleId) {
  const std::string ui_system = read_source_file("src/ui/ui_system.cpp");
  const std::string ui_system_header =
      read_source_file("src/ui/ui_system.hpp");
  const std::string outliner = read_source_file("src/ui/outliner_panel.cpp");
  const std::string console = read_source_file("src/ui/console_panel.cpp");
  const std::string frame_refresh = source_slice(
      ui_system, "void UISystem::refresh(", "void UISystem::prepare_viewports(");
  const std::string localized_refresh = source_slice(
      frame_refresh, "if (localized_locale_ !=",
      "refresh_localized_chrome();");

  expect_contains(ui_system_header, "std::uint64_t localized_revision_");
  expect_contains(localized_refresh,
                  "localized_revision_ != localization_.revision()");
  expect_contains(localized_refresh,
                  "localized_revision_ = localization_.revision()");
  expect_contains(outliner,
                  "localized_revision_ != context.localization.revision()");
  expect_contains(outliner,
                  "localized_revision_ = localization_->revision()");
  expect_contains(console,
                  "localized_revision_ != context.localization.revision()");
  expect_contains(console,
                  "localized_revision_ = localization_->revision()");
}

TEST(BuildGuardrailTests,
     MaterialFindReplaceRevalidatesContextAndRetainedPresentations) {
  const std::string ui_system = read_source_file("src/ui/ui_system.cpp");
  const std::string initialize = source_slice(
      ui_system, "bool UISystem::initialize(",
      "void UISystem::install_runtime_hooks(");
  const std::string revalidate = source_slice(
      ui_system,
      "void UISystem::revalidate_material_find_replace_presentations()",
      "void UISystem::refresh_material_picker_presentations()");
  const std::string picker_refresh = source_slice(
      ui_system, "void UISystem::refresh_material_picker_presentations()",
      "void UISystem::synchronize_material_presentation_consumers()");
  const std::string validate = source_slice(
      ui_system, "void UISystem::validate_material_find_replace_context()",
      "void UISystem::import_material_textures()");
  ASSERT_FALSE(initialize.empty());
  ASSERT_FALSE(revalidate.empty());
  ASSERT_FALSE(picker_refresh.empty());
  ASSERT_FALSE(validate.empty());

  const std::size_t final_context_validation = initialize.find(
      "validate_material_find_replace_context();",
      initialize.find("set_replace_materials_callback("));
  const std::size_t final_replace = initialize.find(
      "replace_material_references(", final_context_validation);
  ASSERT_NE(final_context_validation, std::string::npos);
  ASSERT_NE(final_replace, std::string::npos);
  EXPECT_LT(final_context_validation, final_replace);

  const std::size_t picker_context_validation = initialize.find(
      "validate_material_find_replace_context();",
      initialize.find("set_accept_material_callback("));
  const std::size_t picker_dialog_write = initialize.find(
      "material_find_replace_dialog_->set_material(",
      picker_context_validation);
  ASSERT_NE(picker_context_validation, std::string::npos);
  ASSERT_NE(picker_dialog_write, std::string::npos);
  EXPECT_LT(picker_context_validation, picker_dialog_write);

  expect_contains(revalidate,
                  "material_find_replace_presentation_revision_");
  expect_contains(revalidate,
                  "material_presentation_for(current_find->reference, true)");
  expect_contains(
      revalidate,
      "material_presentation_for(current_replacement->reference, true)");
  expect_missing(revalidate, "used_materials(");
  expect_missing(revalidate, "project_assets().materials(");

  expect_contains(picker_refresh,
                  "material_picker_presentation_revision_");
  expect_contains(picker_refresh,
                  "material_picker_window_->selected_material()");
  expect_contains(picker_refresh,
                  "material_picker_window_->set_materials(");
  expect_contains(picker_refresh, "replacement_material_presentations()");

  expect_contains(validate, "material_find_replace_project_identity_");
  expect_contains(validate, "material_find_replace_scene_id_");
  expect_contains(validate,
                  "material_find_replace_scope_ == "
                  "QuaderMaterialUsageScope::Selection");
  expect_contains(validate, "document_->selection_revision()");
  expect_contains(validate, "modeling_->selection_revision()");
  expect_contains(validate, "modeling_->modeling_object_map_revision()");
  expect_contains(ui_system,
                  "material_find_replace_modeling_object_map_revision_ =");
  expect_contains(validate, "close_material_find_replace()");
}

TEST(BuildGuardrailTests, QtViewportKeepsMultiPaneLayoutWorkspace) {
  const std::string viewport_api_source =
      read_source_file("src/api/quader_viewport_api.cpp");
  const std::string scene_session_header =
      read_source_file("src/editor/session/scene_session.hpp");
  const std::string editor_api_source =
      read_source_file("src/api/quader_editor_api.cpp");
  const std::string main_menu_source =
      read_source_file("src/ui/qt_main_menu.cpp");
  const std::string viewport_panel_header =
      read_source_file("src/ui/qt_viewport_panel.hpp");
  const std::string viewport_panel_source =
      read_source_file("src/ui/qt_viewport_panel.cpp");
  const std::string viewport_surface_header =
      read_source_file("src/ui/qt_viewport_surface.hpp");
  const std::string viewport_surface_source =
      read_source_file("src/ui/qt_viewport_surface.cpp");

  expect_contains(viewport_api_source,
                  "ViewportWorkspaceService owned_workspace{");
  expect_contains(viewport_api_source,
                  "ViewportWorkspaceLayoutPolicy::MultiPane");
  expect_contains(viewport_api_source,
                  "ViewportWorkspaceService *workspace = &owned_workspace");
  expect_contains(viewport_api_source, "impl_->workspace = &next");
  expect_contains(scene_session_header,
                  "ViewportWorkspaceService viewport_workspace_{");
  expect_contains(scene_session_header,
                  "ViewportWorkspaceLayoutPolicy::MultiPane");
  expect_contains(editor_api_source,
                  "QuaderViewportApiRuntimeAccess::bind_scene_workspace(");
  expect_contains(editor_api_source, "scene.viewport_workspace()");
  expect_missing(viewport_api_source,
                 "ViewportWorkspaceService "
                 "workspace{ViewportWorkspaceLayoutPolicy::SinglePaneOnly}");
  expect_missing(scene_session_header,
                 "ViewportWorkspaceLayoutPolicy::SinglePaneOnly");
  expect_missing(main_menu_source,
                 "context.editor.get().viewports().set_layout(layout)");
  expect_contains(viewport_panel_header,
                  "std::array<Pane, kViewportPaneCount> panes_");
  expect_contains(viewport_panel_source, "ViewportPaneId::Perspective");
  expect_contains(viewport_panel_source, "ViewportPaneId::Top");
  expect_contains(viewport_panel_source, "ViewportPaneId::Front");
  expect_contains(viewport_panel_source, "ViewportPaneId::Side");
  expect_contains(viewport_panel_source,
                  "workspace_->active_pane_placements()");
  expect_contains(viewport_panel_source,
                  "QWidget::createWindowContainer(");
  expect_contains(viewport_panel_source,
                  "pane_state.surface, pane_state.surface_container");
  expect_contains(viewport_surface_header,
                  "class QtViewportSurface final : public QWindow");
  expect_contains(viewport_surface_source, "(void)winId()");
  expect_contains(viewport_surface_source,
                  "render_system_->create_native_surface(");
  expect_contains(viewport_surface_source,
                  "render_system_->create_composited_surface(");
  expect_contains(viewport_surface_source,
                  "render_system_->begin_frame(*swap_chain_)");
}

TEST(BuildGuardrailTests,
     RuntimeResourceDeploymentPurgesOwnedAndRetiredArtifactsOnly) {
  const std::string root_c_make = read_source_file("CMakeLists.txt");
  const std::string cleanup = source_slice(
      root_c_make,
      "add_custom_command(\n    OUTPUT \"${QUADER_RUNTIME_RESOURCE_STAMP}\"",
      "if(QUADER_ENABLE_SENTRY AND WIN32)");

  for (std::string_view retired_directory : {
           "${QUADER_RUNTIME_DEPLOY_DIR}/fonts",
           "${QUADER_RUNTIME_DEPLOY_DIR}/styles",
           "${QUADER_RUNTIME_DEPLOY_DIR}/ui",
           "${QUADER_RUNTIME_DEPLOY_DIR}/shaders",
       }) {
    expect_contains(cleanup, std::string{"remove_directory\n            \""} +
                                 std::string{retired_directory} + "\"");
  }
  for (std::string_view retired_file : {
           "${QUADER_RUNTIME_DEPLOY_DIR}/SDL3.dll",
           "${QUADER_RUNTIME_DEPLOY_DIR}/rmlui.dll",
           "${QUADER_RUNTIME_DEPLOY_DIR}/rmlui_debugger.dll",
           "${QUADER_RUNTIME_DEPLOY_DIR}/quader_platform_sdl_win32.lib",
           "${QUADER_RUNTIME_DEPLOY_DIR}/quader_ui_imgui.lib",
       }) {
    expect_contains(cleanup, std::string(retired_file));
  }

  for (std::string_view owned_runtime_directory : {
           "${QUADER_RUNTIME_DEPLOY_DIR}/materials",
           "${QUADER_RUNTIME_DEPLOY_DIR}/icons",
           "${QUADER_RUNTIME_DEPLOY_DIR}/ibl",
           "${QUADER_RUNTIME_DEPLOY_DIR}/locales",
           "${QUADER_RUNTIME_DEPLOY_DIR}/textures",
       }) {
    expect_contains(cleanup, std::string{"remove_directory\n            \""} +
                                 std::string{owned_runtime_directory} + "\"");
  }

  for (std::string_view qt_runtime_directory : {
           "$<TARGET_FILE_DIR:quader>/platforms",
           "$<TARGET_FILE_DIR:quader>/imageformats",
       }) {
    expect_missing(cleanup, std::string{qt_runtime_directory});
  }
}

TEST(BuildGuardrailTests,
     ActiveQtSourcesDoNotRetainImGuiSpecificLocalizationOrOwnershipNames) {
  const std::string localization_header =
      read_source_file("src/core/localization_service.hpp");
  const std::string localization_source =
      read_source_file("src/core/localization_service.cpp");
  expect_missing(localization_header, "imgui_label");
  expect_missing(localization_source, "imgui_label");

  for (std::string_view active_header : {
           "src/ui/ui_continuous_edit_tracker.hpp",
           "src/ui/ui_panel.hpp",
           "src/ui/viewport_hover_controller.hpp",
           "src/ui/viewport_shortcut_controller.hpp",
       }) {
    expect_missing(read_source_file(std::filesystem::path(active_header)),
                   "ImGui");
  }
}

TEST(BuildGuardrailTests, RuntimeUiUsesQtWidgetsWithFusionBaseline) {
  const std::string root_c_make = read_source_file("CMakeLists.txt");
  const std::string vcpkg_manifest = read_source_file("vcpkg.json");
  const std::string third_party_manifest =
      read_source_file("thirdparty/THIRD_PARTY_MANIFEST.md");
  const std::string main_source = read_source_file("src/main.cpp");
  const std::string theme_source =
      read_source_file("src/ui/qt_application_theme.cpp");

  expect_contains(root_c_make,
                  "find_package(Qt6 CONFIG REQUIRED COMPONENTS Core Gui "
                  "Widgets)");
  expect_contains(root_c_make, "Qt6::Widgets");
  expect_contains(vcpkg_manifest, "\"name\": \"qtbase\"");
  expect_contains(main_source, "QApplication qt_application(argc, argv)");
  expect_contains(main_source,
                  "QStyleFactory::create(QStringLiteral(\"Fusion\"))");
  expect_contains(main_source, "QApplication::setStyle(fusion_style)");
  expect_contains(main_source,
                  "quader::apply_qt_obsidian_application_theme(");
  expect_contains(theme_source, "QApplication::setPalette(");
  expect_contains(theme_source,
                  "qt_application_palette_for_contrast_preference(");
  expect_contains(theme_source,
                  "style_hints.setColorScheme(Qt::ColorScheme::Dark)");
  expect_contains(theme_source, "style_hints.unsetColorScheme()");
  expect_contains(theme_source,
                  "QAccessibilityHints::contrastPreferenceChanged");

  for (const std::string &retired_dependency :
       {"find_package(SDL3", "SDL3::SDL3", "find_package(imgui",
        "imgui::imgui", "imgui_material", "resources/materials/imgui.mat"}) {
    expect_missing(root_c_make, retired_dependency);
  }
  expect_missing(vcpkg_manifest, "\"sdl3\"");
  expect_missing(vcpkg_manifest, "\"imgui\"");
  expect_missing(third_party_manifest, "| SDL3 |");
  expect_missing(third_party_manifest, "| Dear ImGui |");
  expect_missing(third_party_manifest, "| Phosphor Icons |");

  std::string active_native_source_blocks;
  for (std::string_view target :
       {"quader_platform", "quader_render", "quader_ui", "quader_app",
        "quader"}) {
    active_native_source_blocks.append(
        cmake_target_source_blocks(root_c_make, target));
  }
  for (const std::string &retired_source : {
           "src/platform/platform_event.cpp",
           "src/platform/platform_window.cpp",
           "src/platform/sdl_event_utils.cpp",
           "src/platform/sdl_input_adapter.cpp",
           "src/render/imgui_filament_renderer.cpp",
           "src/ui/main_menu_bar.cpp",
           "src/ui/settings_panel.cpp",
           "src/ui/ui_platform_adapter.cpp",
           "src/ui/ui_style_tokens.cpp",
           "src/ui/viewport_panel.cpp",
       }) {
    expect_missing(active_native_source_blocks, retired_source);
  }

  for (std::string_view retired_path : {
           "src/platform/platform_event.cpp",
           "src/platform/platform_event.hpp",
           "src/platform/platform_window.cpp",
           "src/platform/platform_window.hpp",
           "src/platform/sdl_event_utils.cpp",
           "src/platform/sdl_event_utils.hpp",
           "src/platform/sdl_input_adapter.cpp",
           "src/platform/sdl_input_adapter.hpp",
           "src/render/imgui_filament_renderer.cpp",
           "src/render/imgui_filament_renderer.hpp",
           "src/render/ui_icon_texture_service.cpp",
           "src/render/ui_icon_texture_service.hpp",
           "src/ui/imgui_popup_utils.cpp",
           "src/ui/imgui_popup_utils.hpp",
           "src/ui/main_menu_bar.cpp",
           "src/ui/main_menu_bar.hpp",
           "src/ui/modal_tool_overlay_renderer.cpp",
           "src/ui/modal_tool_overlay_renderer.hpp",
           "src/ui/navigation_gizmo.cpp",
           "src/ui/navigation_gizmo.hpp",
           "src/ui/phosphor_icons.hpp",
           "src/ui/tool_settings_panel.cpp",
           "src/ui/tool_settings_panel.hpp",
           "src/ui/ui_dock_panel.hpp",
           "src/ui/ui_floating_panel.hpp",
           "src/ui/ui_icon_set.hpp",
           "src/ui/ui_layout.hpp",
           "src/ui/ui_platform_adapter.hpp",
           "src/ui/ui_style_tokens.cpp",
           "src/ui/ui_style_tokens.hpp",
           "src/ui/viewport_context_menu_presenter.cpp",
           "src/ui/viewport_context_menu_presenter.hpp",
           "src/ui/viewport_focus_controller.cpp",
           "src/ui/viewport_focus_controller.hpp",
           "src/ui/viewport_input_frame_builder.cpp",
           "src/ui/viewport_input_frame_builder.hpp",
           "src/ui/viewport_pane_controller.cpp",
           "src/ui/viewport_pane_controller.hpp",
           "src/ui/viewport_panel.cpp",
           "src/ui/viewport_panel.hpp",
           "src/ui/viewport_screen_geometry.hpp",
           "src/ui/viewport_settings_panel.cpp",
           "src/ui/viewport_settings_panel.hpp",
           "resources/fonts/Inter.ttf",
           "resources/fonts/Phosphor.ttf",
           "resources/fonts/OFL.txt",
           "resources/icons/empty_icon.png",
           "resources/icons/hide_icon.png",
           "resources/icons/lock_icon.png",
           "resources/icons/mesh_icon.png",
           "resources/icons/quader-logo@2x.png",
           "resources/icons/show_icon.png",
           "resources/icons/unlock_icon.png",
           "resources/materials/imgui.mat",
           "thirdparty/licenses/imgui-MIT.txt",
           "thirdparty/licenses/sdl3-Zlib-MIT-Apache-2.0.txt",
           "thirdparty/licenses/phosphor-icons-MIT.txt",
       }) {
    EXPECT_FALSE(std::filesystem::exists(source_path(retired_path)))
        << retired_path;
  }

  const std::vector<std::filesystem::path> app_ui_sources =
      cmake_target_source_files(root_c_make,
                                {"quader", "quader_app", "quader_ui"});
  ASSERT_FALSE(app_ui_sources.empty());
  std::string app_ui_source_text;
  for (const std::filesystem::path &relative_path : app_ui_sources) {
    app_ui_source_text.append(read_source_file(relative_path));
    std::filesystem::path header = relative_path;
    header.replace_extension(".hpp");
    if (std::filesystem::exists(source_path(header))) {
      app_ui_source_text.append(read_source_file(header));
    }
  }

  EXPECT_EQ(count_occurrences(app_ui_source_text,
                              "QStyleFactory::create("),
            2u);
  EXPECT_NE(app_ui_source_text.find("design_metrics_style_"),
            std::string::npos);
  EXPECT_EQ(count_occurrences(app_ui_source_text,
                               "QApplication::setStyle("),
            2u);
  EXPECT_EQ(count_occurrences(app_ui_source_text,
                              "QApplication::setPalette("),
            2u);
  EXPECT_NE(app_ui_source_text.find("QtUiScaleProxyStyle"),
            std::string::npos);
  EXPECT_NE(app_ui_source_text.find("preserved_palette"),
            std::string::npos);
  EXPECT_EQ(count_occurrences(main_source, "standardPalette()"), 1u);
  EXPECT_EQ(count_occurrences(theme_source, "standardPalette()"), 1u);
  EXPECT_EQ(count_occurrences(app_ui_source_text, "standardPalette()"), 2u);
  for (const char *global_stylesheet_call : {
           "QApplication::setStyleSheet(",
           "qApp->setStyleSheet(",
           "qt_application.setStyleSheet(",
       }) {
    expect_missing(app_ui_source_text, global_stylesheet_call);
  }
  for (const std::string &retired_ui_token :
       {"#include <SDL", "SDL_", "#include <imgui", "ImGui::",
        "ImGui_Impl"}) {
    EXPECT_EQ(app_ui_source_text.find(retired_ui_token), std::string::npos)
        << retired_ui_token;
  }
}

TEST(BuildGuardrailTests, AppVersionMetadataUsesRootVersionSource) {
  const std::string version = trimmed(read_source_file("VERSION"));
  std::smatch version_match;
  ASSERT_TRUE(std::regex_match(
      version, version_match,
      std::regex(R"((0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*))")));
  ASSERT_EQ(version_match.size(), 4U);
  EXPECT_EQ(version_match[1].str(), "0");
  EXPECT_EQ(version_match[2].str(), "1");
  const int patch = std::stoi(version_match[3].str());
  EXPECT_GE(patch, 0);
  EXPECT_LE(patch, 9999);

  const std::string root_c_make = read_source_file("CMakeLists.txt");
  const std::string manifest = read_source_file("vcpkg.json");
  const std::string version_template =
      read_source_file("src/core/quader_app_version.hpp.in");
  const std::string agents = read_source_file("AGENTS.md");

  expect_contains(
      root_c_make,
      "set(QUADER_VERSION_FILE \"${CMAKE_CURRENT_SOURCE_DIR}/VERSION\")");
  expect_contains(root_c_make,
                  "file(READ \"${QUADER_VERSION_FILE}\" QUADER_APP_VERSION)");
  expect_contains(
      root_c_make,
      "project(QuaderApp VERSION ${QUADER_APP_VERSION} LANGUAGES CXX)");
  expect_contains(root_c_make, "configure_file(");
  expect_contains(root_c_make, "src/core/quader_app_version.hpp.in");
  expect_contains(root_c_make, "quader_app_version.hpp");
  expect_contains(root_c_make, "\"${QUADER_GENERATED_DIR}\"");
  expect_contains(manifest, "\"version-string\": \"" + version + "\"");

  expect_contains(version_template,
                  "kQuaderAppVersionMajor = @QUADER_APP_VERSION_MAJOR@U");
  expect_contains(version_template,
                  "kQuaderAppVersionMinor = @QUADER_APP_VERSION_MINOR@U");
  expect_contains(version_template,
                  "kQuaderAppVersionPatch = @QUADER_APP_VERSION_PATCH@U");
  expect_contains(version_template,
                  "kQuaderAppVersion = \"@QUADER_APP_VERSION@\"");
  expect_contains(version_template,
                  "kQuaderAppVersionDisplay = \"v@QUADER_APP_VERSION@\"");

  for (const char *policy :
       {"VERSION", "vcpkg.json", "version-string", "9999"}) {
    expect_contains(agents, policy);
  }
}

TEST(BuildGuardrailTests, EditorAggregateHasDedicatedApiTarget) {
  const std::string root_c_make = read_source_file("CMakeLists.txt");

  const std::string api_sources =
      cmake_target_source_blocks(root_c_make, "quader_api");
  const std::string editor_api_sources =
      cmake_target_source_blocks(root_c_make, "quader_editor_api");
  const std::string app_sources =
      cmake_target_source_blocks(root_c_make, "quader_app");

  expect_missing(api_sources, "src/api/quader_editor_api.cpp");
  expect_missing(api_sources, "src/api/quader_render_api.cpp");
  expect_contains(editor_api_sources, "src/api/quader_editor_api.cpp");
  expect_contains(editor_api_sources, "src/api/quader_render_api.cpp");
  expect_missing(app_sources, "src/api/");
}

TEST(BuildGuardrailTests, ProductionSourcesCompileInOwningTargets) {
  const std::string root_c_make = read_source_file("CMakeLists.txt");
  const std::string api_sources =
      cmake_target_source_blocks(root_c_make, "quader_api");
  const std::string editor_api_sources =
      cmake_target_source_blocks(root_c_make, "quader_editor_api");
  const std::string editor_services_sources =
      cmake_target_source_blocks(root_c_make, "quader_editor_services");
  const std::string editor_viewport_sources =
      cmake_target_source_blocks(root_c_make, "quader_editor_viewport");
  const std::string viewport_sources =
      cmake_target_source_blocks(root_c_make, "quader_viewport_services");
  const std::string ui_sources =
      cmake_target_source_blocks(root_c_make, "quader_ui");
  const std::string app_sources =
      cmake_target_source_blocks(root_c_make, "quader_app");

  expect_missing(api_sources, "src/editor/");
  expect_missing(api_sources, "src/viewport/");
  expect_missing(editor_api_sources, "src/editor/");
  expect_missing(editor_api_sources, "src/render/");
  expect_missing(editor_services_sources, "src/api/");
  expect_missing(editor_viewport_sources, "src/api/");
  expect_missing(viewport_sources, "src/api/");
  expect_missing(viewport_sources, "src/editor/");
  expect_missing(ui_sources, "src/editor/");
  expect_missing(ui_sources, "src/api/");
  expect_missing(app_sources, "src/api/");
  expect_missing(app_sources, "src/editor/");

  expect_contains(editor_services_sources,
                  "src/editor/commands/editor_command_registry.cpp");
  expect_contains(editor_services_sources,
                  "src/editor/transform/mesh_arrangement_service.cpp");
  expect_contains(editor_services_sources,
                  "src/editor/transform/scene_transform_drag_service.cpp");
  expect_contains(editor_viewport_sources,
                  "src/editor/viewport/viewport_interaction_controller.cpp");
  expect_contains(viewport_sources,
                  "src/viewport/host_viewport_contract_adapter.cpp");
}

TEST(BuildGuardrailTests,
     ProjectApiOwnsLifecycleWhileSceneDocumentsOwnSaveCommands) {
  const std::string project_header =
      read_source_file("src/api/quader_project_api.hpp");
  const std::string project_source =
      read_source_file("src/api/quader_project_api.cpp");
  const std::string command_ids =
      read_source_file("src/api/quader_command_ids.hpp");
  const std::string command_registration =
      read_source_file("src/api/quader_commands_api.cpp");
  const std::string editor_command_catalog =
      read_source_file("src/editor/commands/editor_command_registry.cpp");
  const std::string project_public_surface = source_slice(
      project_header, "class QuaderProjectApi final {", "private:");
  const std::string typed_command_catalog = source_slice(
      command_ids,
      "inline constexpr QuaderCommandId kTypedCommandIds[] = {",
      "[[nodiscard]] inline constexpr std::span<const QuaderCommandId>");

  EXPECT_FALSE(std::regex_search(project_public_surface,
                                 std::regex(R"(\bsave\s*\()")))
      << "QuaderProjectApi must not expose scene persistence";
  EXPECT_FALSE(std::regex_search(project_public_surface,
                                 std::regex(R"(\bsave_as\s*\()")))
      << "QuaderProjectApi must not expose scene Save As";
  expect_missing(project_source, "QuaderProjectApi::save(");
  expect_missing(project_source, "QuaderProjectApi::save_as(");

  for (const std::string &product_catalog :
       {command_ids, command_registration, editor_command_catalog}) {
    expect_missing(product_catalog, "kProjectSave");
    expect_missing(product_catalog, "project.save");
  }

  for (const std::string_view command : {
           "kSceneDocumentSave",
           "kSceneDocumentSaveAs",
           "kSceneDocumentSaveAll",
       }) {
    expect_contains(command_ids, std::string(command));
    expect_contains(typed_command_catalog, std::string(command));
    expect_contains(command_registration, std::string(command));
  }
  expect_contains(command_ids, "\"scene_document.save\"");
  expect_contains(command_ids, "\"scene_document.save_as\"");
  expect_contains(command_ids, "\"scene_document.save_all\"");
  expect_contains(command_registration, "scene_documents_.save_active()");
  expect_contains(command_registration, "scene_documents_.save_as(*id)");
  expect_contains(command_registration, "scene_documents_.save_all()");
}

TEST(BuildGuardrailTests,
     ViewportWorkspaceServiceStaysViewportOwnedAndRuntimeFree) {
  const std::string root_c_make = read_source_file("CMakeLists.txt");
  const std::string header =
      read_source_file("src/viewport/viewport_workspace_service.hpp");
  const std::string source =
      read_source_file("src/viewport/viewport_workspace_service.cpp");

  const std::string runtime_sources =
      cmake_target_source_blocks(root_c_make, "quader_runtime");
  const std::string viewport_sources =
      cmake_target_source_blocks(root_c_make, "quader_viewport_services");

  expect_missing(runtime_sources,
                 "src/viewport/viewport_workspace_service.cpp");
  ASSERT_FALSE(viewport_sources.empty());
  expect_contains(viewport_sources,
                  "src/viewport/viewport_workspace_service.cpp");

  for (const std::string &forbidden : {
           "SDL",
           "filament",
           "SceneDocument",
           "AssetRegistry",
           "SceneRenderBridge",
       }) {
    EXPECT_EQ(header.find(forbidden), std::string::npos) << forbidden;
    EXPECT_EQ(source.find(forbidden), std::string::npos) << forbidden;
  }
}

TEST(DependencyLicenseGuardrailTests, LuaDependenciesRequireExplicitDecision) {
  const std::string root_c_make = read_source_file("CMakeLists.txt");
  const std::string manifest = read_source_file("vcpkg.json");

  for (const std::string &needle :
       {"find_package(Lua", "find_package(sol2", "lua::", "Lua::", "sol2::"}) {
    EXPECT_EQ(root_c_make.find(needle), std::string::npos) << needle;
  }
  for (const std::string &needle :
       {"\"lua\"", "\"lua54\"", "\"luajit\"", "\"sol2\""}) {
    EXPECT_EQ(manifest.find(needle), std::string::npos) << needle;
  }
}

TEST(WindowGuardrailTests,
     QtShellUsesStandardChromeAndRetiredSdlWindowSourcesAreGone) {
  const std::string root_c_make = read_source_file("CMakeLists.txt");
  const std::string main_source = read_source_file("src/main.cpp");
  const std::string ui_source = read_source_file("src/ui/ui_system.cpp");
  const std::string window_placement_header =
      read_source_file("src/platform/window_placement.hpp");

  expect_contains(main_source,
                  "QStyleFactory::create(QStringLiteral(\"Fusion\"))");
  expect_contains(main_source, "QApplication::setStyle(fusion_style)");
  expect_contains(ui_source, "std::make_unique<QtMainWindow>()");
  expect_contains(ui_source,
                  "main_window_->setMinimumSize(kPlatformWindowMinimumWidth,");
  expect_contains(ui_source, "main_window_->setCentralWidget(central_stack_)");
  expect_missing(ui_source, "Qt::FramelessWindowHint");
  expect_missing(ui_source, "setWindowFlags(");
  expect_contains(window_placement_header,
                  "kPlatformWindowMinimumWidth = 800");
  expect_contains(window_placement_header,
                  "kPlatformWindowMinimumHeight = 600");

  for (std::string_view retired_source :
       {"src/platform/platform_event.cpp",
        "src/platform/platform_event.hpp",
        "src/platform/platform_window.cpp",
        "src/platform/platform_window.hpp",
        "src/platform/sdl_event_utils.cpp",
        "src/platform/sdl_event_utils.hpp",
        "src/platform/sdl_input_adapter.cpp",
        "src/platform/sdl_input_adapter.hpp"}) {
    EXPECT_FALSE(std::filesystem::exists(source_path(retired_source)))
        << retired_source;
    expect_missing(root_c_make, std::string(retired_source));
  }
}

TEST(WindowGuardrailTests,
     WindowPlacementPersistenceAndResizeRecoveryStayOwned) {
  const std::string root_c_make = read_source_file("CMakeLists.txt");
  const std::string app_header = read_source_file("src/app/app.hpp");
  const std::string app_source = read_source_file("src/app/app.cpp");
  const std::string app_settings_header =
      read_source_file("src/app/app_window_settings.hpp");
  const std::string app_settings_source =
      read_source_file("src/app/app_window_settings.cpp");
  const std::string ui_system_source =
      read_source_file("src/ui/ui_system.cpp");
  const std::string window_placement_header =
      read_source_file("src/platform/window_placement.hpp");
  const std::string window_placement_source =
      read_source_file("src/platform/window_placement.cpp");
  const std::string ui_settings_source =
      read_source_file("src/ui/ui_settings.cpp");
  const std::string config_path_source =
      read_source_file("src/core/user_config_paths.cpp");

  expect_contains(root_c_make, "src/core/user_config_paths.cpp");
  expect_contains(root_c_make, "src/app/app_window_settings.cpp");
  expect_contains(root_c_make, "src/platform/window_placement.cpp");
  expect_missing(root_c_make, "src/platform/platform_window.cpp");
  expect_contains(app_header, "AppWindowSettings window_settings_");
  expect_contains(app_header, "apply_window_settings()");
  expect_contains(app_header, "save_window_settings()");
  expect_contains(app_source, "#include <QGuiApplication>");
  expect_contains(app_source, "#include <QMainWindow>");
  expect_contains(app_source, "#include <QScreen>");
  expect_contains(app_source,
                  "AppWindowSettings::resolve_project_path(");
  expect_contains(app_source, "AppWindowSettings::load(");
  expect_contains(app_settings_header, "resolve_project_path(");
  expect_contains(app_source,
                  "const std::optional<PlatformWindowPlacement> placement");
  expect_contains(app_source, "QGuiApplication::screens()");
  expect_contains(app_source, "screen->availableGeometry()");
  expect_contains(app_source, "const PlatformWindowChromeFrame frame");
  expect_contains(app_source,
                  "platform_window_recover_frame_to_display_work_area(");
  expect_contains(app_source,
                  "std::max(kPlatformWindowMinimumWidth, frame.width)");
  expect_contains(app_source,
                  "std::max(kPlatformWindowMinimumHeight, frame.height)");
  expect_contains(app_source, "window->setGeometry(frame.x, frame.y");
  expect_contains(
      app_source,
      "window->setWindowState(window->windowState() | Qt::WindowMaximized)");
  expect_missing(app_source, "window->showMaximized()");
  expect_contains(ui_system_source, "main_window_->show()");
  EXPECT_EQ(count_occurrences(ui_system_source, "main_window_->show()"), 1u);
  const std::string prepare_shell = source_slice(
      app_source, "bool App::prepare_composed_shell()",
      "bool App::initialize_shell()");
  ASSERT_FALSE(prepare_shell.empty());
  const std::size_t apply_window_settings =
      prepare_shell.find("apply_window_settings();");
  const std::size_t reveal_window =
      prepare_shell.find("ui_.show();", apply_window_settings);
  ASSERT_NE(apply_window_settings, std::string::npos);
  ASSERT_NE(reveal_window, std::string::npos);
  EXPECT_LT(apply_window_settings, reveal_window);
  const std::size_t restore_begin =
      app_source.find("void App::apply_window_settings()");
  const std::size_t native_geometry_sync = app_source.find(
      "native_window->setGeometry(window->geometry())", restore_begin);
  const std::size_t native_frame_margins =
      app_source.find("native_window->frameMargins()", restore_begin);
  ASSERT_NE(native_geometry_sync, std::string::npos);
  ASSERT_NE(native_frame_margins, std::string::npos);
  EXPECT_LT(native_geometry_sync, native_frame_margins);
  expect_contains(app_source, ".top = margins.top()");
  expect_contains(app_source, ".bottom = margins.bottom()");
  expect_contains(window_placement_header, "struct PlatformWindowFrameMargins");
  expect_contains(app_source, "window->normalGeometry()");
  expect_contains(app_source, "window->isMaximized()");
  expect_contains(app_source, "window_settings_.set_placement(");
  expect_contains(app_source, "window_settings_.save(window_settings_path_)");
  expect_contains(app_settings_header,
                  "std::optional<PlatformWindowPlacement>");
  expect_contains(app_settings_source, "\"window_settings.ini\"");
  expect_contains(app_settings_source, "\"NormalX\"");
  expect_contains(app_settings_source, "\"NormalWidth\"");
  expect_contains(app_settings_source, "\"Maximized\"");
  expect_contains(app_settings_source,
                  "quader_user_config_directory(executable_dir)");
  expect_contains(ui_settings_source,
                  "quader_user_config_directory(executable_dir)");
  expect_contains(config_path_source, "LOCALAPPDATA");
  expect_contains(config_path_source, "XDG_CONFIG_HOME");
  expect_contains(window_placement_header, "struct PlatformWindowPlacement");
  expect_contains(window_placement_header, "kPlatformWindowMinimumWidth");
  expect_contains(window_placement_header, "kPlatformWindowMinimumHeight");
  expect_contains(window_placement_header,
                  "platform_window_recover_frame_to_display_work_area(");
  expect_contains(window_placement_header, "platform_window_is_maximized(");
  expect_contains(window_placement_header,
                  "struct PlatformWindowClientExtent");
  expect_contains(window_placement_header,
                  "platform_window_client_extent(");
  expect_contains(window_placement_source,
                  "std::span<const PlatformWindowChromeFrame> ");
  expect_contains(window_placement_source, "intersection_area(");
  expect_contains(window_placement_source, "IsZoomed(");
  expect_contains(window_placement_source, "GetClientRect(");
  expect_contains(ui_system_source, "platform_window_is_maximized(");
  expect_contains(ui_system_source, "platform_window_client_extent(");
  expect_contains(window_placement_source, "clamp_origin(");
  expect_contains(window_placement_source,
                  "std::clamp(frame.width, kPlatformWindowMinimumWidth");
  expect_missing(window_placement_header, "QWidget");
  expect_missing(window_placement_header, "QWindow");
  expect_missing(window_placement_source, "QScreen");
  expect_missing(window_placement_source, "QGuiApplication");
}

TEST(DependencyLicenseGuardrailTests, AppThirdPartyManifestHasLicenseEvidence) {
  const std::string manifest =
      read_source_file("thirdparty/THIRD_PARTY_MANIFEST.md");
  const std::string root_c_make = read_source_file("CMakeLists.txt");
  const std::string vcpkg_manifest = read_source_file("vcpkg.json");

  for (const std::string &needle : retired_modeling_test_framework_needles()) {
    expect_missing(manifest, needle);
  }

  EXPECT_FALSE(std::filesystem::exists(
      source_path(joined("thirdparty/licenses/cat", "ch2-BSL-1.0.txt"))));
  EXPECT_FALSE(std::filesystem::exists(
      source_path("thirdparty/licenses/taskflow-MIT.txt")));
  EXPECT_FALSE(std::filesystem::exists(
      source_path("src/io/qdr/qdr_parallel_tasks.hpp")));
  expect_missing(manifest, "Taskflow");
  expect_missing(root_c_make, "Taskflow");
  expect_missing(vcpkg_manifest, "taskflow");

  for (const ThirdPartyLicenseExpectation &expectation :
       third_party_license_expectations()) {
    expect_contains(manifest, std::string(expectation.manifest_needle));
    expect_contains(manifest, std::string(expectation.license_path));
    EXPECT_FALSE(
        read_source_file("thirdparty/" + std::string(expectation.license_path))
            .empty())
        << expectation.license_path;
  }

  const std::vector<std::string> direct_dependencies =
      direct_vcpkg_dependencies();
  EXPECT_FALSE(direct_dependencies.empty());
  for (const std::string &package_id : direct_dependencies) {
    const ThirdPartyLicenseExpectation *expectation =
        expectation_for_direct_vcpkg_dependency(package_id);
    ASSERT_NE(expectation, nullptr) << "Add third-party manifest/license "
                                       "evidence for direct vcpkg dependency: "
                                    << package_id;
    expect_contains(manifest, std::string(expectation->manifest_needle));
    expect_contains(manifest, std::string(expectation->license_path));
  }
}

TEST(DependencyLicenseGuardrailTests,
     BinaryQdrDependenciesDocumentSelectedPermissiveLicenses) {
  const std::string manifest =
      read_source_file("thirdparty/THIRD_PARTY_MANIFEST.md");
  const std::string flatbuffers_license =
      read_source_file("thirdparty/licenses/flatbuffers-Apache-2.0.txt");
  const std::string zstd_license = read_source_file(
      "thirdparty/licenses/zstd-BSD-3-Clause-OR-GPL-2.0-only.txt");

  expect_contains(flatbuffers_license, "Apache License");
  expect_contains(flatbuffers_license, "Version 2.0, January 2004");
  expect_contains(zstd_license, "ZSTD is dual licensed under BSD and GPLv2");
  expect_contains(zstd_license, "BSD License");
  expect_contains(manifest, "Quader uses the BSD-3-Clause option");
}

TEST(BuildGuardrailTests, ValueHeadersDoNotIncludeGlmBridge) {
  for (const std::string_view header : {
           "src/math/types.hpp",
           "src/math/types/qvec2.hpp",
           "src/math/types/qvec3.hpp",
           "src/math/types/qquat.hpp",
           "src/math/types/qmat4.hpp",
           "src/math/types/qtransform.hpp",
       }) {
    expect_missing(read_source_file({header}), "glm");
  }
  expect_contains(read_source_file("src/math/glm_bridge.hpp"), "glm");
}

TEST(BuildGuardrailTests, QUmbrellaOnlyIncludesSplitVectorHeaders) {
  const std::string vec_umbrella = read_source_file("src/math/types.hpp");
  expect_contains(vec_umbrella, "#include \"math/types/qvec2.hpp\"");
  expect_contains(vec_umbrella, "#include \"math/types/qvec3.hpp\"");
  expect_missing(vec_umbrella, "struct QVec2");
  expect_missing(vec_umbrella, "struct QVec3");
}

TEST(BuildGuardrailTests, PolygonDocumentUsesSharedVectorTypes) {
  const std::string polygon_document =
      read_source_file("src/sdk/core/modeling/neutral/include/quader/modeling/"
                       "neutral/polygon/document_model.hpp");
  const std::string polygon_umbrella =
      read_source_file("src/sdk/core/modeling/neutral/include/quader/modeling/"
                       "neutral/polygon/document.hpp");

  expect_contains(polygon_document, "quader::QVec2");
  expect_contains(polygon_document, "quader::QVec3");
  expect_missing(polygon_document, "struct QVec2");
  expect_missing(polygon_document, "struct QVec3");
  expect_contains(polygon_umbrella,
                  "#include <quader/modeling/neutral/polygon/document_model.hpp>");
}

TEST(BuildGuardrailTests, NativeScenePersistenceDoesNotDispatchLegacyJson) {
  const std::string cmake = read_source_file("CMakeLists.txt");
  const std::string native_target = source_slice(
      cmake, "add_library(quader_project_qdr_io STATIC",
      "set(QUADER_QDR_GENERATED_DIR");
  ASSERT_FALSE(native_target.empty());
  expect_contains(native_target, "src/io/qdr/qdr_scene_binary_codec.cpp");
  expect_missing(native_target, "src/io/qdr/qdr_project_codecs.cpp");
  expect_missing(native_target, "src/io/qdr/qdr_document.cpp");

  const std::string reader =
      read_source_file({"src", "io", "qdr", "qdr_document_reader.cpp"});
  const std::string writer =
      read_source_file({"src", "io", "qdr", "qdr_document_writer.cpp"});
  const std::string codec = read_source_file(
      {"src", "io", "qdr", "qdr_scene_binary_codec.cpp"});
  for (const std::string *source : {&reader, &writer, &codec}) {
    expect_missing(*source, "nlohmann/json");
    expect_missing(*source, "deserialize_document(");
    expect_missing(*source, "qdr_project_codecs");
  }
  expect_contains(codec, "serialize_qdr_authored_topology(");
  expect_contains(codec, "deserialize_qdr_authored_topology(");
}

TEST(BuildGuardrailTests, NativeSourcesDoNotIncludePrivateModelingSdkHeaders) {
  for (const std::filesystem::path &file :
       source_files_under(source_path("src"))) {
    const std::string relative = normalized_relative(file);
    if (starts_with(relative, "src/sdk/core/modeling/")) {
      continue;
    }
    const std::string source = read_file(file);
    EXPECT_EQ(source.find("sdk/core/modeling/src/"), std::string::npos)
        << relative;
    EXPECT_EQ(source.find("polygon_document_native.hpp"), std::string::npos)
        << relative;
  }

  const std::string codec = read_source_file(
      {"src", "io", "qdr", "qdr_scene_binary_codec.cpp"});
  const std::string projection = read_source_file(
      {"src", "editor", "projection", "mesh_projection_service.cpp"});
  expect_contains(
      codec,
      "MeshProjectionService::sdk_persistence_document_from_polygon_document(");
  expect_contains(codec,
                  "MeshProjectionService::project_persisted_sdk_document_mesh(");
  expect_contains(codec,
                  "MeshProjectionService::compile_polygon_document_mesh(");
  expect_missing(codec, "PolygonDocument::from_authored_topology(");
  expect_missing(codec, "PolygonDocument::from_persisted_topology(");
  expect_missing(codec, ".authored_topology()");
  expect_contains(projection, "PolygonDocument::from_authored_topology(");
  expect_contains(projection, "PolygonDocument::from_persisted_topology(");
  expect_contains(projection, ".authored_topology()");
}

TEST(BuildGuardrailTests,
     UnsupportedSdkPlaceholdersAreVersionedAndDeadAbstractBasesAreRemoved) {
  const std::string modeling_api = read_source_file(
      {"src", "sdk", "core", "modeling", "include", "quader", "modeling",
       "api", "modeling_api.hpp"});
  const std::string tools_api = read_source_file(
      {"src", "sdk", "core", "modeling", "src", "tools_api.cpp"});
  const std::string tool_catalog = source_slice(
      tools_api, "std::vector<ToolDescriptor> ToolsApi::catalog() const",
      "std::string ToolsApi::active_tool() const");
  const std::string compatibility = read_source_file(
      "src/sdk/core/modeling/COMPATIBILITY.md");
  const std::string modeling_cmake =
      read_source_file("cmake/QuaderModeling.cmake");
  expect_contains(modeling_api, "scheduled for removal in 0.2.0");
  expect_contains(modeling_api, "[[deprecated(");
  expect_contains(tools_api, "ErrorCode::UnsupportedOperation");
  expect_missing(tool_catalog, "{\"pivot\", \"Pivot\"}");
  for (const std::string_view retained_surface : {
           "ModelingSession::document(ObjectId)",
           "ModelingBatch::with_mesh(string, function<void(MeshHandle)>)",
           "ToolsApi::activate_pivot_tool()",
           "ModelingApi::profiling()",
       }) {
    expect_contains(compatibility, std::string(retained_surface));
  }
  expect_contains(compatibility, "Remove in 0.2.0");
  expect_contains(modeling_cmake,
                  "${QUADER_MODELING_SDK_ROOT}/COMPATIBILITY.md");
  const std::string external_consumer =
      read_source_file("tests/sdk_consumer/main.cpp");
  expect_contains(external_consumer, "document_snapshot(");
  for (const std::string_view deprecated_example_surface : {
           "session.document(", ".with_mesh(", "activate_pivot_tool(",
           ".profiling(",
       }) {
    expect_missing(external_consumer,
                   std::string(deprecated_example_surface));
  }

  const std::string viewport_tool =
      read_source_file("src/editor/tools/viewport_tool.hpp");
  const std::string box_tool =
      read_source_file("src/editor/tools/base/two_stage_box_tool.cpp");
  expect_missing(viewport_tool, "commit_preview;");
  expect_missing(box_tool, "TwoStageBoxTool::commit_preview");

  const std::string bake =
      read_source_file({"src", "assets", "bake_service.hpp"});
  const std::string panel =
      read_source_file({"src", "ui", "ui_panel.hpp"});
  expect_missing(bake, "class BakeBackend");
  expect_missing(panel, "class UIPanel {");
  expect_missing(panel, "void draw(UIContext");
}

TEST(BuildGuardrailTests, SceneSelectableMutationStaysSceneOwned) {
  const std::string scene_header =
      read_source_file({"src", "scene", "scene_document.hpp"});
  const std::string scene_source =
      read_source_file({"src", "scene", "scene_document.cpp"});
  const std::string qdr_codec = read_source_file(
      {"src", "io", "qdr", "qdr_scene_binary_codec.cpp"});
  const std::string entity_state = read_source_file(
      {"src", "scene", "document", "scene_entity_state.cpp"});

  expect_contains(scene_header,
                  "bool set_selectability(EntityId id, bool selectable);");
  expect_contains(scene_header,
                  "bool set_selectable(EntityId id, bool selectable);");
  expect_contains(scene_source, "bool SceneDocument::set_selectability");
  expect_contains(scene_source, "bool SceneDocument::set_selectable");
  expect_contains(qdr_codec, "apply_scene_entity_state(");
  expect_contains(entity_state, "scene.set_selectability(");
  expect_contains(entity_state, "scene.set_selectable(");
  expect_missing(qdr_codec, "document.scene().set_selectability(");
  static const std::regex kSelectableAssignment(R"(->\s*selectable\s*=(?!=))");
  EXPECT_FALSE(std::regex_search(qdr_codec, kSelectableAssignment));

  for (const std::filesystem::path &file :
       source_files_under(source_path("src"))) {
    if (file.filename() != "scene_document.cpp") {
      EXPECT_FALSE(std::regex_search(read_file(file), kSelectableAssignment))
          << file.string();
    }
  }
}

TEST(BuildGuardrailTests, ProjectionIsReadOnlyAndModelingOwnsSceneSync) {
  const std::string cmake = read_source_file("CMakeLists.txt");
  expect_contains(cmake, "src/editor/projection/mesh_projection_service.cpp");
  expect_missing(
      cmake,
      "src/editor/projection/modeling_scene_reconcile_service.cpp");
  expect_contains(cmake,
                  "src/editor/projection/picking_cache_projection.cpp");
  expect_contains(cmake,
                  "src/editor/projection/selection_overlay_projection.cpp");
  expect_contains(
      cmake,
      "src/editor/modeling/services/modeling_mesh_sync_service.cpp");

  const std::string services = read_source_file(
      {"src", "editor", "modeling", "services",
       "modeling_session_services.cpp"});
  expect_missing(services, "ModelingSceneReconcileService::"
                           "reconcile_modeling_object_scene_nodes");
  expect_missing(services, "MeshProjectionService::"
                           "project_modeling_meshes_to_scene");
  expect_contains(services, "PickingCacheProjection::"
                            "scene_transformed_objects_for_picking");
  expect_contains(services, "PickingCacheProjection::"
                            "object_face_geometry_signature");

  const std::string mesh_sync = read_source_file(
      {"src", "editor", "modeling", "services",
       "modeling_mesh_sync_service.cpp"});
  for (const std::string_view required : {
           "install_projected_mesh",
           "replace_projected_mesh",
           "create_projected_mesh_entity",
           "destroy_projected_entity",
           "bind_projected_object",
       }) {
    expect_contains(mesh_sync, std::string(required));
  }
  for (const std::string_view removed_legacy_path : {
           "sync_scene_objects_from_modeling",
           "reconcile_modeling_object_scene_nodes",
           "project_modeling_meshes_to_scene",
           "build_projected_mesh_plans",
           "remember_projected_mesh_signature",
       }) {
    expect_missing(mesh_sync, std::string(removed_legacy_path));
  }

  const std::array<std::string_view, 15> forbidden_projection_mutations = {
      "AssetRegistry",          "SceneModelingSyncService",
      "create_entity(",         "create_entity_with_id(",
      "destroy_entity(",        "set_modeling_object(",
      "set_mesh(",              "set_material(",
      "add_mesh(",              "replace_mesh(",
      ".bind(",                 ".unbind_object(",
      "mark_scene_objects_synced(", "clear_selection(",
      "set_selected(",
  };
  for (const std::filesystem::path &file : source_files_under(
           source_path({"src", "editor", "projection"}))) {
    const std::string source = read_file(file);
    for (const std::string_view forbidden : forbidden_projection_mutations) {
      EXPECT_EQ(source.find(forbidden), std::string::npos)
          << file.string() << " contains projection-owned mutation token "
          << forbidden;
    }
  }

  const std::string facade = read_source_file(
      {"src", "editor", "modeling", "services",
       "modeling_session_service.cpp"});
  expect_missing(facade, "SelectionOverlayProjection::"
                         "project_selection_overlay_frame");
  expect_contains(services, "SelectionOverlayProjection::"
                            "project_selection_overlay_frame");
}

TEST(BuildGuardrailTests, RenderDoesNotDependOnProjectionOrModelingOwners) {
  for (const std::filesystem::path &file :
       source_files_under(source_path({"src", "render"}))) {
    const std::string source = read_file(file);
    EXPECT_EQ(source.find("editor/projection/"), std::string::npos)
        << file.string();
    EXPECT_EQ(source.find("#include \"modeling/"), std::string::npos)
        << file.string();
  }
}

TEST(BuildGuardrailTests, KeepsRenderHashPolicyInSingleOwner) {
  for (const std::string_view source : {
           "src/render/material_binding_service.cpp",
           "src/render/mesh_renderable_factory.cpp",
           "src/render/tool_preview_renderable_cache.cpp",
           "src/render/overlay/overlay_renderable_builder.cpp",
       }) {
    const std::string text = read_source_file({source});
    expect_missing(text, "kFnv");
    expect_missing(text, "hash_combine");
    expect_missing(text, "hash_float");
    expect_missing(text, "hashVec3");
    expect_contains(text, "RenderSignatureBuilder");
  }
}

TEST(BuildGuardrailTests, MeshAndLightRecordsAreValueSnapshotInputs) {
  const std::string snapshot_header =
      read_source_file("src/render/render_scene_snapshot.hpp");
  const std::string source_stamp_header =
      read_source_file("src/render/render_source_stamp.hpp");
  const std::string extraction_header =
      read_source_file("src/render/scene_render_extraction.hpp");
  const std::string extraction_source =
      read_source_file("src/render/scene_render_extraction.cpp");
  const std::string mesh_record =
      source_slice(snapshot_header, "struct RenderSceneMeshRecord",
                   "struct RenderSceneLightRecord");
  const std::string light_record =
      source_slice(snapshot_header, "struct RenderSceneLightRecord",
                   "struct RenderSceneSnapshotDirtyState");

  for (const std::string_view required : {
           "struct RenderSceneSnapshot",
           "RenderSceneMeshRecord",
           "RenderSceneLightRecord",
           "std::vector<RenderSceneMeshRecord>",
           "std::vector<RenderSceneLightRecord>",
           "mesh_revision",
           "material_revision",
       }) {
    expect_contains(snapshot_header, std::string(required));
  }
  expect_contains(snapshot_header,
                  "struct RenderSceneSnapshot : RenderSourceStamp");
  expect_contains(source_stamp_header, "scene_revision");
  expect_contains(source_stamp_header, "asset_revision");
  expect_missing(mesh_record, "const MeshAsset *");
  expect_missing(mesh_record, "const MeshComponent *");
  expect_missing(mesh_record, "const MaterialComponent *");
  expect_missing(light_record, "const LightComponent *");
  expect_contains(extraction_header, "build_render_scene_snapshot");
  expect_missing(extraction_source, ".mesh = assets.mesh(");
  expect_missing(extraction_source, ".mesh_component = mesh_component");
  expect_missing(extraction_source,
                 ".material_component = material_component");
  expect_missing(extraction_source, ".light = light");
}

TEST(BuildGuardrailTests, OverlayRecordsStayBehindRenderBoundary) {
  const std::string snapshot_header =
      read_source_file("src/render/render_scene_snapshot.hpp");
  const std::string render_bridge_source =
      read_source_file("src/render/scene_render_bridge.cpp");
  const std::string render_bridge_header =
      read_source_file("src/render/scene_render_bridge.hpp");
  const std::string render_api_source =
      read_source_file("src/api/quader_render_api.cpp");
  const std::string render_api_header =
      read_source_file("src/api/quader_render_api.hpp");
  const std::string render_runtime_access = read_source_file(
      "src/editor/render/quader_render_api_runtime_access.hpp");
  const std::string viewport_panel_source =
      read_source_file("src/ui/qt_viewport_panel.cpp");
  const std::string viewport_panel_header =
      read_source_file("src/ui/qt_viewport_panel.hpp");
  const std::string submission_coordinator = read_source_file(
      "src/render/overlay/viewport_overlay_submission_coordinator.cpp");

  for (const std::string_view token : {
           "SelectedObject",
           "SceneWireframe",
           "HierarchySelection",
           "GroupBounds",
           "SelectedLight",
           "ToolOverlay",
       }) {
    expect_contains(snapshot_header, std::string(token));
  }
  expect_contains(render_bridge_source, "RenderSceneSnapshot");
  expect_missing(viewport_panel_source, "ViewportSceneOverlayBuilder::");
  expect_missing(viewport_panel_source, "build_render_overlay_");
  expect_missing(viewport_panel_source, "sync_selected_object_overlay(");
  expect_missing(viewport_panel_source, "sync_scene_wireframe_overlay(");
  expect_missing(viewport_panel_source, "build_scene_overlay_frame(");
  expect_missing(viewport_panel_source, "render_bridge.set_overlay_frame(");
  expect_missing(viewport_panel_source,
                 "render_bridge.set_dynamic_overlay_frame(");
  expect_missing(viewport_panel_source,
                 "render_bridge.update_overlay_camera(");
  for (const std::string_view retired_direct_route : {
           "sync_selection_overlay(", "set_selection_overlay("}) {
    expect_missing(render_runtime_access, std::string(retired_direct_route));
    expect_missing(render_api_source, std::string(retired_direct_route));
  }
  expect_missing(render_api_header, "QuaderRenderOverlaySubmission");
  for (const std::string_view retired_bridge_setter : {
           "void set_overlay_frame(", "set_dynamic_overlay_frame(",
           "update_overlay_camera("}) {
    expect_missing(render_bridge_header, std::string(retired_bridge_setter));
  }
  expect_missing(render_api_source, "append_overlay_frame(");
  expect_missing(viewport_panel_header, "PaneOverlaySubmissionKey");
  expect_missing(viewport_panel_header, "PaneDynamicOverlaySubmissionKey");
  expect_missing(viewport_panel_source, "selection_overlay_cache_");
  EXPECT_FALSE(std::filesystem::exists(
      source_path("src/ui/viewport_pane_overlay_cache.hpp")));
  EXPECT_FALSE(std::filesystem::exists(
      source_path("src/ui/viewport_pane_overlay_cache.cpp")));
  expect_contains(viewport_panel_source,
                  "render_bridge.submit_viewport_overlay(");
  expect_contains(submission_coordinator,
                  "ViewportSceneOverlayBuilder::build_selection_frame(");
  expect_contains(submission_coordinator,
                  "ViewportOverlayStyleResolver::apply_from(");
  expect_contains(submission_coordinator,
                  "target.submit_persistent_overlay(");
  expect_contains(submission_coordinator,
                  "target.submit_dynamic_overlay(");
}

TEST(BuildGuardrailTests,
     PersistentSceneWireframeLineCacheUsesSnapshotClipPlanes) {
  const std::string selected_cache_source = read_source_file(
      "src/render/overlay/selected_object_overlay_cache.cpp");
  const std::string wireframe_cache_source = read_source_file(
      "src/render/overlay/scene_wireframe_overlay_cache.cpp");
  const std::string snapshot_header =
      read_source_file("src/render/overlay/render_overlay_snapshot.hpp");

  expect_contains(snapshot_header, "ViewportCameraClipPlanes clip_planes");
  expect_missing(selected_cache_source, "kSelectedObjectLineNearPlane");
  expect_missing(wireframe_cache_source, "kSceneWireframeLineNearPlane");
  expect_missing(selected_cache_source, "setParameter(\"nearPlane\", 0.05");
  expect_missing(wireframe_cache_source,
                 "setParameter(\"nearPlane\", 0.05");
  expect_missing(selected_cache_source, "SelectedObjectLineVertex");
  expect_missing(selected_cache_source, "update_line_proxy");
  expect_missing(selected_cache_source, "setParameter(\"nearPlane\"");
  expect_contains(wireframe_cache_source, "clip_planes.near_plane");
}

TEST(BuildGuardrailTests, SceneOverlayBuildersConsumeSnapshotsOnly) {
  const std::string builder_source = read_source_file(
      "src/render/overlay/viewport_scene_overlay_builder.cpp");
  const std::string builder_header = read_source_file(
      "src/render/overlay/viewport_scene_overlay_builder.hpp");
  const std::string selected_cache_source = read_source_file(
      "src/render/overlay/selected_object_overlay_cache.cpp");
  const std::string wireframe_cache_source = read_source_file(
      "src/render/overlay/scene_wireframe_overlay_cache.cpp");
  const std::string bridge_source =
      read_source_file("src/render/scene_render_bridge.cpp");
  const std::string bridge_header =
      read_source_file("src/render/scene_render_bridge.hpp");
  const std::string overlay_snapshot_header =
      read_source_file("src/render/overlay/render_overlay_snapshot.hpp");
  const std::string render_snapshot_header =
      read_source_file("src/render/render_scene_snapshot.hpp");

  expect_missing(builder_header, "const SceneDocument &");
  expect_missing(builder_header, "const AssetRegistry &");
  expect_missing(builder_source, "build_render_overlay_selection_snapshot");
  expect_missing(builder_source,
                 "build_render_overlay_scene_wireframe_snapshot");
  expect_contains(builder_source,
                  "const RenderOverlaySelectionSnapshot &snapshot");
  expect_contains(builder_source,
                  "const RenderOverlaySceneWireframeSnapshot &snapshot");
  expect_contains(selected_cache_source,
                  "const RenderOverlaySelectedObjectSnapshot &snapshot");
  expect_contains(wireframe_cache_source,
                  "const RenderOverlaySceneWireframeSnapshot &snapshot");
  expect_contains(bridge_header, "sync(const SceneDocument &document");
  expect_contains(bridge_header, "const AssetRegistry &assets");
  expect_missing(bridge_source,
                 "build_render_overlay_selected_object_snapshot");
  expect_missing(bridge_source,
                 "build_render_overlay_scene_wireframe_snapshot");
  expect_missing(bridge_header, "RenderOverlaySnapshotCache");
  expect_contains(overlay_snapshot_header,
                  "class RenderOverlaySnapshotCache final");
  expect_contains(bridge_header,
                  "const RenderOverlaySelectedObjectSnapshot");
  expect_contains(bridge_header,
                  "const RenderOverlaySceneWireframeSnapshot");
  expect_contains(render_snapshot_header,
                  "using RenderSelectedObjectOverlaySnapshotRecord");
  expect_contains(render_snapshot_header, "using OverlaySnapshot");
}

TEST(BuildGuardrailTests,
     SceneAndOverlayEarlyOutsQualifyReuseBySceneActivation) {
  const std::string binding_header =
      read_source_file("src/render/render_scene_binding.hpp");
  const std::string extraction_source =
      read_source_file("src/render/scene_render_extraction.cpp");
  const std::string sync_source =
      read_source_file("src/render/scene_render_sync_service.cpp");
  const std::string bridge_source =
      read_source_file("src/render/scene_render_bridge.cpp");
  const std::string selected_cache_source = read_source_file(
      "src/render/overlay/selected_object_overlay_cache.cpp");
  const std::string wireframe_cache_source = read_source_file(
      "src/render/overlay/scene_wireframe_overlay_cache.cpp");
  const std::string texture_cache_source =
      read_source_file("src/render/texture_binding_service.cpp");
  const std::string render_api_source =
      read_source_file("src/api/quader_render_api.cpp");

  expect_contains(binding_header, "scene_session_id");
  expect_contains(binding_header, "activation_generation");
  expect_contains(binding_header, "binding_revision");
  for (const std::string *source : {&extraction_source, &sync_source,
                                    &selected_cache_source,
                                    &wireframe_cache_source}) {
    expect_contains(*source, "render_source_dependencies_match(");
  }
  expect_contains(extraction_source, "RenderSourceDependency::SceneBinding");
  expect_contains(sync_source, "kRenderSceneSyncDocumentDependencies");
  expect_contains(selected_cache_source,
                  "RenderSourceDependency::SceneBinding");
  expect_contains(wireframe_cache_source,
                  "RenderSourceDependency::SceneBinding");
  expect_missing(sync_source, "synced_scene_binding_");
  const std::size_t retire_scene_meshes =
      sync_source.find("mesh_cache_->remove_except(no_scene_entities)");
  const std::size_t destroy_retired_materials = sync_source.find(
      "mesh_cache_->drain_retired_destroy_queue(\n"
      "      mesh_cache_->retired_proxy_count())");
  const std::size_t invalidate_scene_textures =
      sync_source.find("material_binding_->bind_scene(scene_binding)");
  ASSERT_NE(retire_scene_meshes, std::string::npos);
  ASSERT_NE(destroy_retired_materials, std::string::npos);
  ASSERT_NE(invalidate_scene_textures, std::string::npos);
  EXPECT_LT(retire_scene_meshes, destroy_retired_materials);
  EXPECT_LT(destroy_retired_materials, invalidate_scene_textures)
      << "A scene switch must destroy retired material instances before "
         "invalidating the textures they still reference.";

  const std::size_t clear_tool_previews =
      bridge_source.find("tool_preview_cache_.clear()");
  const std::size_t clear_viewport_overlays =
      bridge_source.find("overlay_cache_.clear_slot(index)");
  const std::size_t bind_scene_resources =
      bridge_source.find("sync_service_.bind_scene(scene_binding)");
  ASSERT_NE(clear_tool_previews, std::string::npos);
  ASSERT_NE(clear_viewport_overlays, std::string::npos);
  ASSERT_NE(bind_scene_resources, std::string::npos);
  EXPECT_LT(clear_tool_previews, bind_scene_resources);
  EXPECT_LT(clear_viewport_overlays, bind_scene_resources)
      << "Preview and overlay material instances must be destroyed before "
         "the outgoing scene texture cache is invalidated.";
  expect_contains(bridge_source, "bind_scene(snapshot.scene_binding)");
  expect_missing(selected_cache_source, "synced_scene_binding_");
  expect_missing(wireframe_cache_source, "synced_scene_binding_");
  expect_contains(texture_cache_source, "texture_cache_.clear()");
  expect_missing(texture_cache_source, "evict_obsolete_texture_revisions");
  expect_contains(render_api_source, ".scene_session_id =");
  expect_contains(render_api_source, ".activation_generation =");
  expect_contains(render_api_source, ".binding_revision =");
}

TEST(BuildGuardrailTests, OwnsIncrementalLightSyncPolicy) {
  const std::string header =
      read_source_file({"src", "render", "light_renderable_cache.hpp"});
  const std::string source =
      read_source_file({"src", "render", "light_renderable_cache.cpp"});
  const std::string sync_source =
      read_source_file({"src", "render", "scene_render_sync_service.cpp"});

  expect_contains(header, "rebuild_signature");
  expect_contains(header, "transform_signature");
  expect_contains(header, "filament_intensity");
  expect_contains(source, "return filament_light_intensity(light)");
  const std::string policy =
      read_source_file({"src", "render", "light_render_policy.hpp"});
  expect_contains(policy, "light_engine_values(light)");
  expect_missing(source, "ExposureCompensation");
  expect_contains(source, "needs_rebuild(light)");
  expect_contains(source, "update_light_transform(existing->second, "
                          "light.world_transform)");
  expect_contains(source, ".intensity(filament_intensity(light))");
  expect_contains(source, ".position(position_from_matrix(world_transform))");
  expect_contains(source, ".falloff(light_falloff_radius(light))");
  expect_contains(source, "light_manager.setPosition(instance, "
                          "position_from_matrix(world_transform))");
  expect_contains(sync_source, "light_cache_.sync_light");
  expect_missing(sync_source, "light_signature");
  expect_missing(sync_source, "update_light_transform");
}

TEST(BuildGuardrailTests, EditWireBatchesUseViewportClipStateForGpuExpansion) {
  const std::string builder_header =
      read_source_file("src/render/overlay/overlay_renderable_builder.hpp");
  const std::string builder_source =
      read_source_file("src/render/overlay/overlay_renderable_builder.cpp");
  const std::string proxy_source =
      read_source_file("src/render/overlay/overlay_proxy_factory.cpp");
  const std::string edit_wire_material =
      read_source_file("resources/materials/quader_edit_wire_line.mat");
  const std::string edit_wire_batch =
      source_slice(builder_header, "struct OverlayEditWireBatch",
                   "struct OverlayPointBatch");

  expect_contains(edit_wire_batch, "near_plane");
  expect_contains(edit_wire_batch, "far_plane");
  expect_contains(edit_wire_batch, "orthographic_projection");
  expect_missing(builder_source, "kOverlayEditWireNearPlane");
  expect_contains(proxy_source, "batch.near_plane");
  expect_contains(proxy_source, "batch.far_plane");
  expect_contains(proxy_source, "batch.orthographic_projection");
  expect_contains(edit_wire_material, "nearPlane");
  expect_contains(edit_wire_material, "farPlane");
  expect_contains(edit_wire_material, "orthographicProjection");
  expect_contains(edit_wire_material, "getClipFromViewMatrix()");
  expect_contains(edit_wire_material,
                  "return vec4(1000000.0, 1000000.0, 0.0, 1.0)");
}

TEST(BuildGuardrailTests, DelegatesOverlayAndPreviewProxyOwnership) {
  const std::string header =
      read_source_file({"src", "render", "scene_render_bridge.hpp"});
  const std::string source =
      read_source_file({"src", "render", "scene_render_bridge.cpp"});

  expect_contains(header, "OverlayRenderableCache overlay_cache_");
  expect_contains(header, "ToolPreviewRenderableCache tool_preview_cache_");
  expect_contains(source, "overlay_cache_.set_overlay_frame");
  expect_contains(source, "tool_preview_cache_.set_meshes");
  expect_missing(header, "struct OverlayProxy");
  expect_missing(header, "struct OverlaySlot");
  expect_missing(header, "struct OverlayProxyGroup");
  expect_missing(header, "overlaySlots_");
  expect_missing(header, "toolPreviewMeshProxies_");
  expect_missing(source, "rebuildOverlayProxies");
  expect_missing(source, "destroyOverlayProxy");
  expect_missing(source, "destroyToolPreviewMeshes");
}

TEST(BuildGuardrailTests,
     UsesInstancedDeviceDomainEndpointExpansionMaterial) {
  const std::string material =
      read_source_file("resources/materials/quader_selected_object_line.mat");
  const std::string backend = read_source_file(
      "src/render/overlay/instanced_wire_render_backend.cpp");

  expect_contains(material, "instanced : true");
  expect_contains(material, "getWorldFromModelMatrix");
  expect_contains(material, "getResolution");
  expect_contains(backend, "filament::InstanceBuffer::Builder");
  expect_contains(backend,
                  ".instances(transforms.size(), proxy.instance_buffer_)");
}

TEST(BuildGuardrailTests,
     DynamicToolPreviewUpdatesEveryDeformationDerivedGpuStream) {
  const std::string source =
      read_source_file("src/render/mesh_renderable_factory.cpp");
  const std::string update = source_slice(
      source, "bool MeshRenderableFactory::update_dynamic_geometry(",
      "template <typename Resource>");
  const std::string prepare = source_slice(
      source, "MeshRenderableFactory::prepare_dynamic_geometry_update(",
      "bool MeshRenderableFactory::update_dynamic_geometry(");

  expect_contains(prepare, "build_render_vertices<DynamicRenderVertexStatic>(mesh, dynamic_positions)");
  expect_contains(prepare, "packed->indices.assign(mesh.indices.begin(), mesh.indices.end())");
  expect_contains(prepare, "FilamentBufferDescriptorFactory::retained_bytes");
  expect_contains(update, "prepare_dynamic_geometry_update(mesh, dynamic_positions)");
  expect_contains(update, "setBufferAt(");
  expect_contains(update, "*engine_, 0");
  expect_contains(update, "*engine_, 1");
  expect_contains(update, "proxy.index_buffer->setBuffer(");
  expect_contains(update, "std::move(prepared->indices)");
}
