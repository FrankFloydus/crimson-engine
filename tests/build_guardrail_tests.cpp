////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "support/guardrail_file_scan.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

namespace {

using quader::test::guardrail::files_with_extension;
using quader::test::guardrail::normalized_relative;
using quader::test::guardrail::read_file;
using quader::test::guardrail::read_source_file;
using quader::test::guardrail::source_path;
using quader::test::guardrail::source_root;
using quader::test::guardrail::source_slice;
using quader::test::guardrail::starts_with;

void expect_contains(const std::string &source, const std::string &needle) {
  EXPECT_NE(source.find(needle), std::string::npos) << needle;
}

void expect_missing(const std::string &source, const std::string &needle) {
  EXPECT_EQ(source.find(needle), std::string::npos) << needle;
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
      "architecture-review.md", "CMakeLists.txt",    "benchmarks",
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
      "refactor.md",          "resources",  "src",
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
  static const std::regex kCommitLiteral(R"([0-9a-fA-F]{40})");
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
      {"", "nlohmann/json", "licenses/nlohmann-json-MIT.txt", false},
      {"entt", "EnTT", "licenses/entt-MIT.txt", true},
      {"glm", "OpenGL Mathematics (GLM)", "licenses/glm-MIT.txt", true},
      {"qtbase", "Qt Base", "licenses/qtbase-LGPL-3.0-only.txt", true},
      {"stduuid", "stduuid", "licenses/stduuid-MIT.txt", true},
      {"gtest", "GoogleTest", "licenses/gtest-BSD-3-Clause.txt", true},
      {"lodepng", "lodepng", "licenses/lodepng-Zlib.txt", true},
      {"sentry-native", "Sentry Native SDK", "licenses/sentry-native-MIT.txt",
       true},
      {"zlib", "zlib", "licenses/zlib-Zlib.txt", true},
      {"spdlog", "spdlog", "licenses/spdlog-MIT.txt", true},
      {"", "{fmt}", "licenses/fmt-MIT.txt", false},
      {"taskflow", "Taskflow", "licenses/taskflow-MIT.txt", true},
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

std::vector<std::string> direct_vcpkg_dependencies() {
  const std::string manifest = read_source_file("vcpkg.json");
  const std::size_t dependencies_key = manifest.find("\"dependencies\"");
  EXPECT_NE(dependencies_key, std::string::npos);
  const std::size_t dependencies_start = manifest.find('[', dependencies_key);
  EXPECT_NE(dependencies_start, std::string::npos);

  std::vector<std::string> dependencies;
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
  expect_contains(presets, "\"name\": \"ninja-msvc\"");
  expect_contains(presets, "\"generator\": \"Ninja Multi-Config\"");
  expect_contains(presets, "${sourceDir}/build/ninja-msvc");

  expect_contains(build_files, "src/mesh/polygon");
  expect_contains(build_files, "src/mesh/geometry");
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
  expect_contains(build_files, "quader_qdr_document_io");
  expect_contains(build_files, "quader_qdr_runtime_io");
  expect_contains(build_files,
                  "qdr_custom_engine_modeling_runtime_smoke_tests");
  expect_contains(build_files, "qdr_runtime_io_compat_tests");
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
      read_source_file("src/mesh/polygon/document_operations.hpp");
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
      {"src/mesh/polygon/operations/extrude_faces_operation.cpp",
       {"OperationResult result;", "result.created.vertices",
        "result.created.faces", "result.affected.faces", "return result;"}},
      {"src/mesh/polygon/operations/thicken_faces_operation.cpp",
       {"OperationResult result;", "result.created.vertices",
        "result.created.faces", "result.affected.faces", "return result;"}},
      {"src/mesh/polygon/operations/split_edges_operation.cpp",
       {"OperationResult result;", "result.created.vertices",
        "result.created.edges", "result.affected.edges",
        "result.affected.faces", "return result;"}},
      {"src/mesh/polygon/operations/delete_selection_operation.cpp",
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
      {"src/mesh/polygon/document_model.hpp",
       {"struct Document", "add_vertex", "make_cube"}},
      {"src/mesh/polygon/document_selection.hpp",
       {"struct Selection", "select_only", "selection_center"}},
      {"src/mesh/polygon/document_operations.hpp",
       {"struct OperationResult", "PlaneCutRequest", "thicken_selected_faces"}},
      {"src/mesh/polygon/document_topology.hpp",
       {"FacePerimeterInfo", "document_edges", "face_normal"}},
      {"src/mesh/polygon/document_mesh_compilation.hpp",
       {"CompiledMesh", "compile_document"}},
      {"src/mesh/polygon/document_picking.hpp",
       {"PickOptions", "PickResult", "pick_element"}},
      {"src/mesh/polygon/document_overlay_preview.hpp",
       {"OverlayData", "KnifeSegmentPreview", "build_overlay"}},
      {"src/mesh/polygon/element_handles.hpp",
       {"struct VertexId", "struct FaceId", "struct EdgeKey"}},
  };

  const std::string umbrella =
      read_source_file("src/mesh/polygon/document.hpp");
  for (const PolygonHeaderExpectation &header : headers) {
    const std::string path(header.path);
    ASSERT_TRUE(std::filesystem::exists(source_path(path))) << path;
    const std::string source = read_source_file(path);
    for (const std::string_view needle : header.owner_needles) {
      expect_contains(source, std::string(needle));
    }
    expect_missing(source, "#include <mesh/polygon/document.hpp>");

    const std::string include_path =
        path.substr(std::string("src/mesh/polygon/").size());
    expect_contains(umbrella, "#include <mesh/polygon/" + include_path + ">");
  }

  expect_contains(umbrella, "#include <mesh/polygon/poly_operation.hpp>");
  expect_missing(umbrella, "struct Document {");
  expect_missing(umbrella, "struct Selection {");
  expect_missing(umbrella, "struct PickOptions {");
  expect_missing(umbrella, "struct OverlayData {");

  const std::string poly_operation =
      read_source_file("src/mesh/polygon/poly_operation.hpp");
  expect_contains(poly_operation,
                  "#include <mesh/polygon/document_operations.hpp>");
  expect_missing(poly_operation, "#include <mesh/polygon/document.hpp>");
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

TEST(BuildGuardrailTests, RootQuaderTestsListsEveryRootTestSource) {
  const std::string root_c_make = read_source_file("CMakeLists.txt");
  const std::string modeling_c_make =
      read_source_file("cmake/QuaderModeling.cmake");
  const std::string sdk_consumer_c_make =
      read_source_file("tests/sdk_consumer/CMakeLists.txt");
  const std::string test_build_files =
      root_c_make + "\n" + modeling_c_make + "\n" + sdk_consumer_c_make;

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
     PhaseOneTargetMapRejectsMonolithicCoreAndRuntimeSourceBlocks) {
  const std::string root_c_make = read_source_file("CMakeLists.txt");
  const std::string modeling_c_make =
      read_source_file("cmake/QuaderModeling.cmake");
  const std::string build_files = root_c_make + "\n" + modeling_c_make;
  const std::string architecture_review =
      read_source_file("architecture-review.md");

  for (const std::string &target_area :
       {"`core`", "`math`", "`geometry`", "`polygon`", "`assets`", "`document`",
        "`commands`", "`editor_modeling`", "`viewport`", "`render`", "`io_qdr`",
        "`api`", "`ui`", "`app`"}) {
    expect_contains(architecture_review, target_area);
  }

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

  for (const std::filesystem::path &relative_path :
       {std::filesystem::path{"memory.md"},
        std::filesystem::path{"refactor.md"}}) {
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
  std::vector<std::filesystem::path> files =
      project_owned_comment_capable_files();
  files.push_back("memory.md");
  files.push_back("refactor.md");

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
      EXPECT_EQ(source.find(needle), std::string::npos)
          << relative_path.generic_string() << " contains " << needle;
    }
  }
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
  const std::string build_files = root_c_make + "\n" + modeling;

  const std::vector<std::string> documented_targets = {
      "quader_geometry",
      "quader_modeling_polygon",
      "quader_modeling_runtime",
      "quader_qdr_document_io",
      "quader_qdr_runtime_io",
      "quader_editor_metadata",
      "quader_modeling_domain_tests",
      "quader_modeling_domain_polygon_loop_tests",
      "quader_modeling_domain_runtime_loop_tests",
      "quader_modeling_domain_metadata_loop_tests",
  };
  for (const std::string &target : documented_targets) {
    expect_contains(review, "`" + target + "`");
    expect_contains(build_files, target);
  }

  expect_missing(review, "`qdr_modeling_domain_smoke`");
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

TEST(BuildGuardrailTests, ModelingSdkPublicHeadersStayNativeOwnerFree) {
  const std::vector<std::string> banned_needles = {
      "SceneDocument",
      "ProjectDocument",
      "ProjectSession",
      "AssetRegistry",
      "ModelingSessionService",
      "SceneRenderBridge",
      "ViewportOverlayFrame",
      "QuaderEditorApi",
      "SDL",
      "ImGui",
      "filament",
      "entt",
      "nlohmann",
      "#include <mesh/",
      "#include \"mesh/",
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
  const std::string host_contract =
      read_source_file("src/editor/metadata/host_contract.cpp");
  const std::string default_catalog =
      read_source_file("src/assets/default_material_catalog.hpp");

  expect_contains(
      root_c_make,
      "$<TARGET_FILE_DIR:quader>/materials/default/default_albedo.png");
  expect_contains(
      root_c_make,
      "$<TARGET_FILE_DIR:quader>/materials/default/default_roughness.png");
  expect_contains(default_catalog, "kDefaultMaterialAlbedoTexturePackagePath");
  expect_contains(default_catalog,
                  "kDefaultMaterialRoughnessTexturePackagePath");
  expect_contains(app_source, "ProjectAssetBootstrapService");
  expect_contains(bootstrap_source, "default_material_catalog.hpp");
  expect_contains(bootstrap_source, "kDefaultMaterialAlbedoTexturePackagePath");
  expect_contains(bootstrap_source,
                  "kDefaultMaterialRoughnessTexturePackagePath");
  expect_contains(host_contract, "default_material_catalog.hpp");
  expect_contains(host_contract, "kDefaultMaterialAlbedoTexturePackagePath");
  expect_contains(host_contract, "kDefaultMaterialRoughnessTexturePackagePath");
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
  expect_contains(root_c_make, "../_SOURCES/filament-v1.71.6-windows");
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
      "lib/x86_64/${_quader_filament_runtime}/${_quader_filament_lib}.lib");
  expect_contains(root_c_make, "lib/x86_64/mdd/matdbg.lib");
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
      tool,
      "if ($Path -match "
      "'^(AGENTS\\.md|architecture-review\\.md|memory\\.md|refactor\\.md|INPUT_KEYBINDINGS\\.md)$')",
      "if ($Path -match '^tools/')");
  const std::string examples_route = source_slice(
      tool, "if ($Path -match '^examples/')",
      "if ($Path -match '^src/mesh/geometry/')");

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
  expect_contains(tool, "assign_material");
  expect_contains(tool, "assign_material_slot");
  expect_contains(tool, "selection_operations.cpp");
  expect_contains(tool, "QdrModelingRuntimeOperationCommandTests.*");
  expect_contains(tool, "QdrModelingRuntimeToolOperationContractTests.*");
  expect_contains(tool, "Add-PathPlan");
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
  expect_contains(tool, "src/mesh/polygon/");
  expect_contains(tool, "src/editor/transform/");
  expect_contains(tool, "EditorApiTests.*Transform*");
  expect_contains(tool, "modeling/runtime");
  expect_contains(tool, "src/editor/metadata/");
  expect_contains(tool, "resources/locales/");
  expect_contains(tool, "resources/ibl/");
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
  expect_contains(root_agents, "quader_modeling_domain_polygon_loop_tests");
  expect_contains(root_agents, "-MeasureFull");
  expect_contains(root_agents, "-ChangedPath");
  expect_contains(root_agents, "before -> after");
}

TEST(BuildGuardrailTests, TestingDocsExplainPlacementAndSelectionRules) {
  const std::string root_agents = read_source_file("AGENTS.md");
  const std::string tests_guide = read_source_file("tests/AGENTS.md");
  const std::string modeling_guide =
      read_source_file("tests/modeling_domain/AGENTS.md");

  expect_contains(root_agents, "## Test Placement");
  expect_contains(root_agents, "## Test Selection");
  expect_contains(root_agents, "quader_guardrail_tests");
  expect_contains(root_agents, "quader_tests");
  expect_contains(root_agents, "quader_modeling_domain_tests");
  expect_contains(root_agents, "tests/modeling_domain/operation/"
                                 "qdr_document_operation_<feature>_tests.cpp");
  expect_contains(root_agents,
                  "qdr_modeling_runtime_operation_<feature>_tests.cpp");
  expect_contains(root_agents,
                  "tools/affected_check.ps1 -PlanOnly -ChangedPath");
  expect_contains(root_agents, "--gtest_filter=SuiteName.*");
  expect_contains(root_agents, "-MeasureFull");

  expect_contains(tests_guide, "## Placement Rules");
  expect_contains(tests_guide,
                  "Root `tests/*_tests.cpp` files belong to `quader_tests`");
  expect_contains(tests_guide,
                  "Root guardrail files belong to `quader_guardrail_tests`");
  expect_contains(
      tests_guide,
      "Modeling-domain behavior belongs under `tests/modeling_domain/`");
  expect_contains(tests_guide, "## Selection Rules");

  expect_contains(modeling_guide, "## Placement Rules");
  expect_contains(modeling_guide, "qdr_document_operation_<feature>_tests.cpp");
  expect_contains(modeling_guide,
                  "qdr_modeling_runtime_operation_<feature>_tests.cpp");
  expect_contains(modeling_guide,
                  "qdr_modeling_runtime_tool_<feature>_tests.cpp");
  expect_contains(modeling_guide, "## Selection Rules");
  expect_contains(modeling_guide, "quader_modeling_domain_polygon_loop_tests");
  expect_contains(modeling_guide, "quader_modeling_domain_runtime_loop_tests");
  expect_contains(modeling_guide, "quader_modeling_domain_metadata_loop_tests");
}

TEST(BuildGuardrailTests, LocalizationResourcesArePackagedWithRuntime) {
  const std::string root_c_make = read_source_file("CMakeLists.txt");

  expect_contains(root_c_make, "src/core/localization_service.cpp");
  expect_contains(root_c_make, "tests/localization_tests.cpp");
  expect_contains(root_c_make, "$<TARGET_FILE_DIR:quader>/locales");
  expect_contains(root_c_make, "resources/locales");
  expect_contains(root_c_make, "copy_directory");

  EXPECT_FALSE(read_source_file("resources/locales/en-US.lang").empty());
  EXPECT_FALSE(read_source_file("resources/locales/it-IT.lang").empty());
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
  expect_contains(root_c_make, "src/ui/qt_color_swatch_button.cpp");
  expect_contains(root_c_make, "src/ui/qt_auxiliary_window_state.cpp");
  expect_contains(root_c_make, "src/ui/qt_settings_dialog.cpp");
  expect_contains(root_c_make, "src/ui/qt_tool_settings_window.cpp");
  expect_contains(vcpkg_manifest, "\"name\": \"qtbase\"");
  expect_contains(vcpkg_manifest, "\"widgets\"");
  expect_contains(third_party_manifest, "Qt Base");
  expect_contains(third_party_manifest,
                  "licenses/qtbase-LGPL-3.0-only.txt");
  expect_contains(qt_license, "GNU LESSER GENERAL PUBLIC LICENSE");

  // Fusion supplies the complete widget appearance. The app must not package
  // the retired ImGui material or UI fonts that override the stock theme.
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
  expect_contains(root_c_make, "$<TARGET_FILE_DIR:quader>/icons");
  expect_contains(root_c_make, "copy_directory");
  EXPECT_FALSE(
      read_source_file("resources/materials/quader_overlay_icon.mat").empty());
  EXPECT_FALSE(read_source_file("resources/materials/quader_mesh.mat").empty());
  EXPECT_FALSE(
      read_source_file("resources/materials/quader_mesh_lit.mat").empty());
  EXPECT_TRUE(std::filesystem::exists(source_path("resources/icons/logo.png")));
  EXPECT_TRUE(
      std::filesystem::exists(source_path("resources/icons/point_light.png")));
  EXPECT_GT(std::filesystem::file_size(source_path("resources/icons/logo.png")),
            512u);
  EXPECT_GT(std::filesystem::file_size(
                source_path("resources/icons/point_light.png")),
            512u);
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

TEST(BuildGuardrailTests, QtViewportSwapchainsStayBehindRenderSystemAdapters) {
  const std::string ui_system_header = read_source_file("src/ui/ui_system.hpp");
  const std::string ui_system_source = read_source_file("src/ui/ui_system.cpp");
  const std::string viewport_panel_source =
      read_source_file("src/ui/qt_viewport_panel.cpp");
  const std::string viewport_surface_header =
      read_source_file("src/ui/qt_viewport_surface.hpp");
  const std::string viewport_surface_source =
      read_source_file("src/ui/qt_viewport_surface.cpp");
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
                  "RenderAdditionalSwapChain swap_chain_");
  expect_contains(viewport_panel_source, "QWidget::createWindowContainer");
  expect_contains(viewport_surface_source, "native_window_handle()");
  expect_contains(viewport_surface_source,
                  "render_system_->create_additional_swap_chain(");
  expect_contains(viewport_surface_source,
                  "render_system_->begin_frame(swap_chain_.get())");
  expect_contains(viewport_surface_source, "swap_chain_.reset()");
  expect_missing(viewport_surface_source, "filament::Engine");
  expect_missing(viewport_surface_source, "filament::SwapChain");

  expect_contains(render_system_header, "class RenderAdditionalSwapChain");
  expect_contains(render_system_header, "create_additional_swap_chain");
  expect_contains(render_system_header, "initialize_viewport_renderer");
  expect_contains(render_system_source,
                  "RenderSystem::create_additional_swap_chain(");
  expect_contains(render_system_source, "engine_->createSwapChain(native_window");
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
  const std::string tool_window_header =
      read_source_file("src/ui/qt_tool_settings_window.hpp");

  expect_contains(ui_system_header, "class QMainWindow");
  expect_contains(ui_system_header, "class QDockWidget");
  expect_contains(ui_system_header, "std::unique_ptr<QMainWindow> main_window_");
  expect_contains(ui_system_source, "#include <QMainWindow>");
  expect_contains(ui_system_source, "#include <QDockWidget>");
  expect_contains(ui_system_source,
                  "main_window_ = std::make_unique<QMainWindow>()");
  expect_contains(ui_system_source,
                  "main_window_->setObjectName(QStringLiteral("
                  "\"quaderMainWindow\"))");
  expect_contains(ui_system_source,
                  "main_window_->setCentralWidget(viewport_panel_)");
  expect_contains(ui_system_source, "QMainWindow::AllowNestedDocks");
  expect_contains(ui_system_source, "QMainWindow::AllowTabbedDocks");

  expect_contains(ui_system_source,
                  "UIPanelId::Outliner, *outliner_panel_, "
                  "Qt::LeftDockWidgetArea");
  expect_contains(ui_system_source,
                  "UIPanelId::Inspector, *inspector_panel_, "
                  "Qt::RightDockWidgetArea");
  expect_contains(ui_system_source,
                  "UIPanelId::Console, *console_panel_, "
                  "Qt::BottomDockWidgetArea");
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
  expect_contains(panel_manager_header,
                  "std::array<QDockWidget *, 4> docks_");
  expect_contains(panel_manager_source, "void UIPanelManager::add(");
  expect_contains(panel_manager_source, "QDockWidget *UIPanelManager::dock(");

  expect_contains(settings_dialog_header,
                  "class QtSettingsDialog final : public QDialog");
  expect_contains(tool_window_header,
                  "class QtToolSettingsWindow final : public QWidget");
  expect_contains(ui_system_source,
                  "new QtSettingsDialog(main_window_.get())");
  expect_contains(ui_system_source,
                  "new QtToolSettingsWindow(main_window_.get())");
}

TEST(BuildGuardrailTests, QtFrameRefreshKeepsExpensiveChromeEventDriven) {
  const std::string ui_system_source = read_source_file("src/ui/ui_system.cpp");
  const std::string main_menu_source =
      read_source_file("src/ui/qt_main_menu.cpp");
  const std::string frame_refresh = source_slice(
      ui_system_source, "void UISystem::refresh(",
      "void UISystem::prepare_viewports(");
  const std::string settings_show = source_slice(
      ui_system_source, "void UISystem::show_settings_dialog()",
      "} // namespace quader");

  expect_contains(frame_refresh, "outliner_panel_->refresh(context)");
  expect_contains(frame_refresh, "inspector_panel_->refresh(context)");
  expect_contains(frame_refresh, "console_panel_->refresh(context)");
  expect_contains(frame_refresh, "modeling_->transform_drag_active()");
  expect_contains(frame_refresh,
                  "!transform_drag_active && panel_visible(UIPanelId::Outliner)");
  expect_contains(frame_refresh,
                  "!transform_drag_active && panel_visible(UIPanelId::Inspector)");
  expect_missing(frame_refresh, "settings_dialog_->refresh()");
  expect_missing(frame_refresh, "main_menu_->refresh()");
  expect_missing(frame_refresh, "viewport_panel_->refresh_workspace_layout()");
  expect_contains(main_menu_source, "&QMenu::aboutToShow");
  expect_contains(settings_show, "settings_dialog_->refresh()");
}

TEST(BuildGuardrailTests, QtViewportKeepsMultiPaneLayoutWorkspace) {
  const std::string viewport_api_source =
      read_source_file("src/api/quader_viewport_api.cpp");
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
                  "ViewportWorkspaceService "
                  "workspace{ViewportWorkspaceLayoutPolicy::MultiPane}");
  expect_missing(viewport_api_source,
                 "ViewportWorkspaceService "
                 "workspace{ViewportWorkspaceLayoutPolicy::SinglePaneOnly}");
  expect_contains(main_menu_source,
                  "context.editor.viewports().set_layout(layout)");
  expect_contains(viewport_panel_header,
                  "std::array<Pane, kViewportPaneCount> panes_");
  expect_contains(viewport_panel_source, "ViewportPaneId::Perspective");
  expect_contains(viewport_panel_source, "ViewportPaneId::Top");
  expect_contains(viewport_panel_source, "ViewportPaneId::Front");
  expect_contains(viewport_panel_source, "ViewportPaneId::Side");
  expect_contains(viewport_panel_source,
                  "workspace_->active_pane_placements()");
  expect_contains(viewport_panel_source,
                  "QWidget::createWindowContainer(pane_state.surface");
  expect_contains(viewport_surface_header,
                  "class QtViewportSurface final : public QWindow");
  expect_contains(viewport_surface_source, "(void)winId()");
  expect_contains(viewport_surface_source,
                  "render_system_->create_additional_swap_chain(");
  expect_contains(viewport_surface_source,
                  "render_system_->begin_frame(swap_chain_.get())");
}

TEST(BuildGuardrailTests, QtRuntimePostBuildPurgesRetiredUiArtifactsOnly) {
  const std::string root_c_make = read_source_file("CMakeLists.txt");
  const std::string cleanup = source_slice(
      root_c_make,
      "COMMAND \"${CMAKE_COMMAND}\" -E make_directory \"$<TARGET_FILE_DIR:quader>/materials\"",
      "if(QUADER_ENABLE_SENTRY AND WIN32)");

  for (std::string_view retired_directory : {
           "$<TARGET_FILE_DIR:quader>/fonts",
           "$<TARGET_FILE_DIR:quader>/styles",
           "$<TARGET_FILE_DIR:quader>/ui",
           "$<TARGET_FILE_DIR:quader>/shaders",
       }) {
    expect_contains(cleanup, std::string{"remove_directory \""} +
                                 std::string{retired_directory} + "\"");
  }
  for (std::string_view retired_file : {
           "$<TARGET_FILE_DIR:quader>/SDL3.dll",
           "$<TARGET_FILE_DIR:quader>/rmlui.dll",
           "$<TARGET_FILE_DIR:quader>/rmlui_debugger.dll",
           "$<TARGET_FILE_DIR:quader>/quader_platform_sdl_win32.lib",
           "$<TARGET_FILE_DIR:quader>/quader_ui_imgui.lib",
           "$<TARGET_FILE_DIR:quader>/materials/imgui.filamat",
       }) {
    expect_contains(cleanup, std::string(retired_file));
  }

  for (std::string_view current_runtime_directory : {
           "$<TARGET_FILE_DIR:quader>/materials",
           "$<TARGET_FILE_DIR:quader>/icons",
           "$<TARGET_FILE_DIR:quader>/ibl",
           "$<TARGET_FILE_DIR:quader>/locales",
           "$<TARGET_FILE_DIR:quader>/platforms",
           "$<TARGET_FILE_DIR:quader>/imageformats",
       }) {
    expect_missing(cleanup, std::string{"remove_directory \""} +
                                std::string{current_runtime_directory} +
                                "\"");
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
           "src/ui/viewport_pane_overlay_cache.hpp",
           "src/ui/viewport_shortcut_controller.hpp",
       }) {
    expect_missing(read_source_file(std::filesystem::path(active_header)),
                   "ImGui");
  }
}

TEST(BuildGuardrailTests, RuntimeUiUsesQtWidgetsFusionOnly) {
  const std::string root_c_make = read_source_file("CMakeLists.txt");
  const std::string vcpkg_manifest = read_source_file("vcpkg.json");
  const std::string third_party_manifest =
      read_source_file("thirdparty/THIRD_PARTY_MANIFEST.md");
  const std::string main_source = read_source_file("src/main.cpp");

  expect_contains(root_c_make,
                  "find_package(Qt6 CONFIG REQUIRED COMPONENTS Core Gui "
                  "Widgets)");
  expect_contains(root_c_make, "Qt6::Widgets");
  expect_contains(vcpkg_manifest, "\"name\": \"qtbase\"");
  expect_contains(main_source, "QApplication qt_application(argc, argv)");
  expect_contains(main_source,
                  "QStyleFactory::create(QStringLiteral(\"Fusion\"))");
  expect_contains(main_source, "QApplication::setStyle(fusion_style)");

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
            1u);
  EXPECT_EQ(count_occurrences(app_ui_source_text,
                               "QApplication::setStyle("),
            1u);
  EXPECT_EQ(count_occurrences(app_ui_source_text,
                              "QApplication::setPalette("),
            1u);
  EXPECT_EQ(count_occurrences(app_ui_source_text, "standardPalette()"), 1u);
  for (const std::string &forbidden_customization : {
            "setStyleSheet(",
            ".qss",
            "QProxyStyle",
           "QFontDatabase",
           "setFont(",
           "Qt::FramelessWindowHint",
           "Qt::CustomizeWindowHint",
           "Qt::WA_TranslucentBackground",
       }) {
    EXPECT_EQ(app_ui_source_text.find(forbidden_customization),
              std::string::npos)
        << forbidden_customization;
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
  EXPECT_EQ(version_match[2].str(), "0");
  const int patch = std::stoi(version_match[3].str());
  EXPECT_GE(patch, 1);
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

  expect_contains(agents, "## Versioning Rule");
  expect_contains(agents, "follow the app version bump rule");
  expect_contains(agents,
                  "Root `VERSION` is the app version source of truth");
  expect_contains(agents,
                  "`vcpkg.json` `version-string` must mirror it");
  expect_contains(agents, "major/minor fixed at `0.0`");
  expect_contains(agents, "within `1..9999`");
  expect_contains(agents, "before the final handoff `quader` build");
  expect_contains(agents, "Do not");
  expect_contains(agents, "planning, review, inspection-only, "
                          "failed/no-change, or explanation-only");
}

TEST(BuildGuardrailTests, EditorAggregateStaysOutOfDocumentApiTarget) {
  const std::string root_c_make = read_source_file("CMakeLists.txt");

  const std::string api_sources =
      cmake_target_source_blocks(root_c_make, "quader_api");
  const std::string runtime_sources =
      cmake_target_source_blocks(root_c_make, "quader_runtime");
  const std::string app_sources =
      cmake_target_source_blocks(root_c_make, "quader_app");

  expect_missing(api_sources, "src/api/quader_editor_api.cpp");
  expect_missing(api_sources, "src/api/quader_render_api.cpp");
  EXPECT_TRUE(runtime_sources.find("src/api/quader_editor_api.cpp") !=
                  std::string::npos ||
              app_sources.find("src/api/quader_editor_api.cpp") !=
                  std::string::npos)
      << "QuaderEditorApi belongs to the native editor/app composition target, "
         "not the document/modeling API target";
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
      cmake_target_source_blocks(root_c_make, "quader_viewport");

  expect_missing(runtime_sources,
                 "src/viewport/viewport_workspace_service.cpp");
  if (!viewport_sources.empty()) {
    expect_contains(viewport_sources,
                    "src/viewport/viewport_workspace_service.cpp");
  }

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
  expect_contains(ui_source, "std::make_unique<QMainWindow>()");
  expect_contains(ui_source,
                  "main_window_->setMinimumSize(kPlatformWindowMinimumWidth,");
  expect_contains(ui_source, "main_window_->setCentralWidget(viewport_panel_)");
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
                  "AppWindowSettings::resolve_path(executable_dir)");
  expect_contains(app_source, "AppWindowSettings::load(window_settings_path_)");
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
  expect_contains(app_source, "window->showMaximized()");
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
  expect_contains(window_placement_source,
                  "std::span<const PlatformWindowChromeFrame> ");
  expect_contains(window_placement_source, "intersection_area(");
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

  for (const std::string &needle : retired_modeling_test_framework_needles()) {
    expect_missing(manifest, needle);
  }

  EXPECT_FALSE(std::filesystem::exists(
      source_path(joined("thirdparty/licenses/cat", "ch2-BSL-1.0.txt"))));

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
