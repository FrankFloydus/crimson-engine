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
      {"", "Geometric Tools Engine Mathematics",
       "licenses/geometrictools-BSL-1.0.txt", false},
      {"entt", "EnTT", "licenses/entt-MIT.txt", true},
      {"glm", "OpenGL Mathematics (GLM)", "licenses/glm-MIT.txt", true},
      {"sdl3", "SDL3", "licenses/sdl3-Zlib-MIT-Apache-2.0.txt", true},
      {"imgui", "Dear ImGui", "licenses/imgui-MIT.txt", true},
      {"", "Phosphor Icons", "licenses/phosphor-icons-MIT.txt", false},
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
  expect_missing(build_files, "src/geometry");
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
  expect_contains(options, "set(_QUADER_GEOMETRIC_TOOLS_COMMIT");
  expect_contains(options, "set(QUADER_GEOMETRIC_TOOLS_VENDOR_DIR");
  expect_contains(options, "function(quader_resolve_dependency_source");
  expect_contains(options, "ENV{${env_var}}");
  expect_contains(options, "Desktop/_QUADER_DEV/_deps/${dev_dep_dir}");
  expect_missing(root_c_make, "set(_QUADER_OPENMESH_COMMIT");
  expect_missing(root_c_make, "set(_QUADER_GEOMETRIC_TOOLS_COMMIT");
  expect_missing(modeling, "set(QUADER_OPENMESH_COMMIT");
  expect_missing(modeling, "gte-8.2-");
  expect_contains(modeling, "QUADER_GEOMETRIC_TOOLS_VENDOR_DIR");
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

  for (const std::filesystem::path &file :
       files_with_extension(source_root() / "tests", ".cpp")) {
    const std::string relative = normalized_relative(file);
    if (starts_with(relative, "tests/modeling_domain/")) {
      continue;
    }

    EXPECT_NE(root_c_make.find(relative), std::string::npos) << relative;
  }

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
  expect_contains(tool, "apply_uv");
  expect_contains(tool, "QdrDocumentOperationUv*");
  expect_contains(tool, "assign_material");
  expect_contains(tool, "assign_material_slot");
  expect_contains(tool, "selection_operations.cpp");
  expect_contains(tool, "QdrModelingRuntimeOperationCommandTests.*");
  expect_contains(tool, "QdrModelingRuntimeToolOperationContractTests.*");
  expect_contains(tool, "Add-PathPlan");
  expect_contains(tool, "Remove-RedundantBuildTargets");
  expect_contains(tool, "AGENTS\\.md");
  expect_contains(tool, "WindowGuardrailTests.*");
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

TEST(BuildGuardrailTests, ImGuiRuntimeResourcesArePackagedWithRuntime) {
  const std::string root_c_make = read_source_file("CMakeLists.txt");
  const std::string icon_header = read_source_file("src/ui/ui_icon_set.hpp");
  const std::string phosphor_header =
      read_source_file("src/ui/phosphor_icons.hpp");
  const std::string style_source =
      read_source_file("src/ui/ui_style_tokens.cpp");
  const std::string ui_system_source = read_source_file("src/ui/ui_system.cpp");
  const std::string icon_texture_service =
      read_source_file("src/render/ui_icon_texture_service.cpp");
  const std::string png_texture_loader =
      read_source_file("src/render/png_texture_loader.cpp");

  expect_contains(root_c_make, "src/ui/ui_system.cpp");
  expect_contains(root_c_make, "src/ui/viewport_panel.cpp");
  expect_contains(root_c_make, "src/render/ui_icon_texture_service.cpp");
  expect_contains(root_c_make, "src/render/png_texture_loader.cpp");
  expect_contains(root_c_make, "quader_register_filament_material");
  expect_contains(root_c_make, "QUADER_FILAMENT_MATERIAL_COPY_COMMANDS");
  expect_contains(root_c_make, "resources/materials/quader_mesh.mat");
  expect_contains(root_c_make, "resources/materials/quader_mesh_lit.mat");
  expect_contains(root_c_make,
                  "quader_register_filament_material(quader_mesh_material "
                  "resources/materials/quader_mesh.mat quader_mesh)");
  expect_contains(root_c_make,
                  "quader_register_filament_material(quader_mesh_lit_material "
                  "resources/materials/quader_mesh_lit.mat quader_mesh_lit)");
  expect_contains(root_c_make, "resources/materials/imgui.mat");
  expect_contains(root_c_make, "resources/materials/quader_overlay_icon.mat");
  expect_contains(root_c_make,
                  "quader_register_filament_material(quader_overlay_icon_"
                  "material resources/materials/quader_overlay_icon.mat "
                  "quader_overlay_icon)");
  expect_contains(root_c_make, "quader_register_filament_material(imgui_"
                               "material resources/materials/imgui.mat imgui)");
  expect_contains(root_c_make, "resources/fonts/Inter.ttf");
  expect_contains(root_c_make, "$<TARGET_FILE_DIR:quader>/fonts/Inter.ttf");
  expect_contains(root_c_make, "resources/fonts/Phosphor.ttf");
  expect_contains(root_c_make, "$<TARGET_FILE_DIR:quader>/fonts/Phosphor.ttf");
  expect_contains(root_c_make, "resources/icons");
  expect_contains(root_c_make, "$<TARGET_FILE_DIR:quader>/icons");
  expect_contains(root_c_make, "copy_if_different");
  expect_contains(root_c_make, "copy_directory");
  expect_contains(icon_header, "lock");
  expect_contains(icon_header, "unlock");
  expect_contains(icon_header, "hide");
  expect_contains(icon_header, "show");
  expect_contains(ui_system_source, "load_ui_icon_textures");
  expect_contains(icon_texture_service, "lock_icon.png");
  expect_contains(icon_texture_service, "unlock_icon.png");
  expect_contains(icon_texture_service, "hide_icon.png");
  expect_contains(icon_texture_service, "show_icon.png");
  expect_contains(icon_texture_service, "load_png_texture(");
  expect_contains(png_texture_loader, "lodepng::decode");
  expect_contains(png_texture_loader, "filament::Texture::Builder()");
  expect_contains(phosphor_header, "kPhosphorIconRangeStart");
  expect_contains(phosphor_header, "kPhosphorIconRangeEnd");
  expect_contains(style_source, "Phosphor.ttf");
  expect_contains(style_source, "GlyphExcludeRanges = kIconRanges");
  expect_contains(style_source, "MergeMode = true");

  EXPECT_FALSE(read_source_file("resources/materials/imgui.mat").empty());
  EXPECT_FALSE(
      read_source_file("resources/materials/quader_overlay_icon.mat").empty());
  EXPECT_FALSE(read_source_file("resources/materials/quader_mesh.mat").empty());
  EXPECT_FALSE(
      read_source_file("resources/materials/quader_mesh_lit.mat").empty());
  EXPECT_FALSE(read_source_file("resources/fonts/Inter.ttf").empty());
  EXPECT_FALSE(read_source_file("resources/fonts/Phosphor.ttf").empty());
  EXPECT_TRUE(std::filesystem::exists(source_path("resources/icons/logo.png")));
  EXPECT_TRUE(std::filesystem::exists(
      source_path("resources/icons/quader-logo@2x.png")));
  EXPECT_TRUE(
      std::filesystem::exists(source_path("resources/icons/point_light.png")));
  EXPECT_GT(std::filesystem::file_size(source_path("resources/icons/logo.png")),
            512u);
  EXPECT_GT(std::filesystem::file_size(
                source_path("resources/icons/quader-logo@2x.png")),
            512u);
  EXPECT_GT(std::filesystem::file_size(
                source_path("resources/icons/point_light.png")),
            512u);
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

TEST(BuildGuardrailTests, UiIconTexturesStayBehindRenderSystemAdapters) {
  const std::string ui_system_header = read_source_file("src/ui/ui_system.hpp");
  const std::string ui_system_source = read_source_file("src/ui/ui_system.cpp");
  const std::string viewport_panel_source =
      read_source_file("src/ui/viewport_panel.cpp");
  const std::string render_system_header =
      read_source_file("src/render/render_system.hpp");
  const std::string render_system_source =
      read_source_file("src/render/render_system.cpp");
  const std::string icon_texture_service =
      read_source_file("src/render/ui_icon_texture_service.cpp");
  const std::string png_texture_loader =
      read_source_file("src/render/png_texture_loader.cpp");

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

  expect_contains(render_system_header, "load_ui_icon_textures");
  expect_contains(render_system_header, "clear_ui_icon_textures");
  expect_contains(render_system_header, "initialize_imgui_renderer");
  expect_contains(render_system_header, "initialize_viewport_renderer");
  expect_contains(render_system_source, "ui_icon_textures_.load_icons");
  expect_contains(render_system_source, "ui_icon_textures_.clear");
  expect_contains(icon_texture_service, "load_png_texture(");
  expect_contains(png_texture_loader, "filament::Texture::Builder()");
  expect_contains(icon_texture_service, "textures_->register_texture(texture)");
  expect_contains(icon_texture_service,
                  "textures_->unregister_texture(record.handle)");
  expect_contains(icon_texture_service, "engine_->destroy(record.texture)");
}

TEST(BuildGuardrailTests, UiSystemKeepsSdlBehindAppPlatformAdapter) {
  const std::string root_c_make = read_source_file("CMakeLists.txt");
  const std::string ui_system_header = read_source_file("src/ui/ui_system.hpp");
  const std::string ui_system_source = read_source_file("src/ui/ui_system.cpp");
  const std::string ui_platform_adapter =
      read_source_file("src/ui/ui_platform_adapter.hpp");
  const std::string app_source = read_source_file("src/app/app.cpp");
  const std::string ui_link_block =
      source_slice(root_c_make, "target_link_libraries(quader_ui",
                   "target_compile_features(quader_ui");

  for (const std::string &forbidden : {"SDL", "imgui_impl_sdl3"}) {
    expect_missing(ui_system_header, forbidden);
    expect_missing(ui_system_source, forbidden);
    expect_missing(ui_platform_adapter, forbidden);
  }
  expect_contains(ui_system_header, "UIPlatformAdapter");
  expect_contains(ui_system_header, "UIPlatformEvent");
  expect_contains(ui_platform_adapter, "const void *native_event");
  expect_contains(app_source, "class SdlUIPlatformAdapter");
  expect_contains(app_source, "ImGui_ImplSDL3_InitForOther");
  expect_contains(app_source, "SDL_SetWindowRelativeMouseMode");
  expect_contains(app_source, "UIPlatformEvent{");
  expect_contains(app_source, ".native_event = event.native_event");
  expect_contains(app_source,
                  ".pressed_input_chord = event.pressed_input_chord");
  expect_missing(ui_link_block, "SDL3::SDL3");
}

TEST(BuildGuardrailTests, AppLoopConsumesTypedPlatformEvents) {
  const std::string root_c_make = read_source_file("CMakeLists.txt");
  const std::string app_header = read_source_file("src/app/app.hpp");
  const std::string app_source = read_source_file("src/app/app.cpp");
  const std::string platform_event_header =
      read_source_file("src/platform/platform_event.hpp");
  const std::string platform_event_source =
      read_source_file("src/platform/platform_event.cpp");

  expect_contains(root_c_make, "src/platform/platform_event.cpp");
  expect_contains(app_header, "PlatformEventWatch live_resize_event_watch_");
  expect_contains(app_source, "PlatformEventPump events(window_)");
  expect_contains(app_source, "events.poll(event)");
  expect_contains(app_source, "event.quit_requested");
  expect_contains(app_source, "event.mouse_motion");
  expect_contains(app_source, "event.window_pixel_size_changed");
  expect_contains(app_source, "event.window_chrome_state_changed");
  expect_contains(app_source, "event.live_resize_exposed_frame");
  expect_contains(app_source, "platform_wait_for_event_timeout(wait_ms)");
  expect_missing(app_header, "SDL_Event");
  expect_missing(app_source, "SDL_PollEvent");
  expect_missing(app_source, "SDL_WaitEventTimeout");
  expect_missing(app_source, "SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED");
  expect_missing(app_source, "SDL_EVENT_WINDOW_RESIZED");
  expect_missing(app_source, "SDL_EVENT_WINDOW_FOCUS_GAINED");
  expect_missing(app_source, "sdl_event_targets_window");
  expect_contains(platform_event_header, "struct PlatformEvent");
  expect_contains(platform_event_header, "class PlatformEventPump");
  expect_contains(platform_event_header, "class PlatformEventWatch");
  expect_contains(platform_event_source, "SDL_PollEvent");
  expect_contains(platform_event_source, "SDL_WaitEventTimeout");
  expect_contains(platform_event_source, "SDL_AddEventWatch");
  expect_contains(platform_event_source, "SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED");
  expect_contains(platform_event_source, "SDL_EVENT_WINDOW_FOCUS_GAINED");
}

TEST(BuildGuardrailTests, ImGuiShellViewportAndControlStylingStayVisible) {
  const std::string ui_system_source = read_source_file("src/ui/ui_system.cpp");
  const std::string main_menu_source =
      read_source_file("src/ui/main_menu_bar.cpp");
  const std::string style_source =
      read_source_file("src/ui/ui_style_tokens.cpp");
  const std::string icon_texture_service =
      read_source_file("src/render/ui_icon_texture_service.cpp");
  const std::string viewport_panel_source =
      read_source_file("src/ui/viewport_panel.cpp");
  const std::string window_chrome_source =
      source_slice(ui_system_source, "UISystem::draw_window_chrome",
                   "void UISystem::draw_dockspace");
  const std::string static_bottom_bar_source = source_slice(
      ui_system_source, "void draw_static_bottom_bar", "void UISystem::draw");
  const std::string chrome_menu_items = source_slice(
      main_menu_source, "MainMenuBarResult MainMenuBar::draw_menus",
      "} // namespace quader");

  const std::size_t dockspace_padding = ui_system_source.find(
      "ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f})");
  const std::size_t dockspace_begin =
      ui_system_source.find("ImGui::Begin(\"Main Dockspace\"");
  ASSERT_NE(dockspace_padding, std::string::npos);
  ASSERT_NE(dockspace_begin, std::string::npos);
  EXPECT_LT(dockspace_padding, dockspace_begin);
  expect_contains(ui_system_source, "ImGui::PopStyleVar(3)");
  expect_contains(ui_system_source, "kStaticBottomBarHeight = 28.0f");
  expect_contains(ui_system_source, "draw_static_bottom_bar");
  expect_contains(ui_system_source, "viewport.Size.y - kStaticBottomBarHeight");
  expect_contains(ui_system_source, "viewport->Size.y - chrome_height");
  expect_contains(ui_system_source, "kStaticBottomBarHeight");
  expect_contains(
      ui_system_source,
      "ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse");
  expect_contains(ui_system_source, "ImGuiWindowFlags_NoInputs");
  expect_contains(ui_system_source,
                  "ImGui::Begin(\"Quader Static Bottom Bar\"");
  expect_contains(ui_system_source, "quader_app_version.hpp");
  expect_contains(ui_system_source, "kCompressedSidebarMinWidth = 220.0f");
  expect_contains(ui_system_source, "kReservedViewportDockWidth = 300.0f");
  expect_contains(ui_system_source, "effective_sidebar_minimum_width");
  expect_contains(
      ui_system_source,
      "frame_style.sidebar_min_width = effective_sidebar_minimum_width");
  expect_contains(ui_system_source, "draw_dockspace(editor, quit_requested, "
                                    "document, modeling,");
  expect_contains(
      ui_system_source,
      "enforce_sidebar_dock_minimums(frame_style.sidebar_min_width)");
  expect_contains(
      ui_system_source,
      "clamp_docked_window_width(ui_panel_key(UIPanelId::Outliner),");
  expect_contains(
      ui_system_source,
      "clamp_docked_window_width(ui_panel_key(UIPanelId::Inspector),");
  expect_contains(ui_system_source, "sidebar_minimum_width);");
  expect_missing(ui_system_source,
                 "clamp_docked_window_width(ui_panel_key(UIPanelId::Outliner), "
                 "style_.sidebar_min_width)");
  expect_contains(static_bottom_bar_source,
                  "const char *right_label = kQuaderAppVersionDisplay");
  expect_missing(static_bottom_bar_source, "right_label = \"Quader\"");
  expect_contains(ui_system_source,
                  "ImGui::DockSpace(dockspace_id, dockspace_size");
  expect_contains(ui_system_source, "ui_packed_color(118, 92, 168)");
  expect_contains(ui_system_source, "kWindowChromeHeight = 35.0f");
  expect_contains(ui_system_source, "kWindowChromeControlsWidth");
  expect_contains(window_chrome_source, "ImGuiWindowFlags_MenuBar");
  expect_contains(window_chrome_source, "main_menu_bar_.draw_menus");
  expect_contains(icon_texture_service, "quader-logo@2x.png");
  expect_contains(main_menu_source,
                  "MainMenuBarResult MainMenuBar::draw_menus");
  expect_missing(main_menu_source, "drawInlinePopupMenu");
  expect_missing(main_menu_source, "ImGui::Button(label.c_str())");
  expect_missing(static_bottom_bar_source, "ImGuiWindowFlags_MenuBar");
  expect_missing(chrome_menu_items, "BeginMenuBar(");
  expect_missing(chrome_menu_items, "drawInline");
  expect_missing(ui_system_source,
                 "availableDockspaceSize.y - kStaticBottomBarHeight");
  expect_missing(ui_system_source, "ImGui::Dummy(barSize)");

  expect_contains(style_source, "style.FrameBorderSize = 1.0f");
  expect_contains(style_source,
                  "colors[ImGuiCol_Border] = style_color(72, 74, 82)");
  expect_contains(style_source,
                  "colors[ImGuiCol_FrameBg] = style_color(23, 24, 27)");
  expect_contains(style_source,
                  "colors[ImGuiCol_FrameBgHovered] = style_color(38, 40, 45)");
  expect_contains(style_source,
                  "colors[ImGuiCol_FrameBgActive] = style_color(50, 54, 61)");
  expect_contains(style_source,
                  "colors[ImGuiCol_CheckMark] = style_color(236, 241, 245)");
  expect_missing(style_source, "style.FrameBorderSize = 0.0f");

  expect_contains(viewport_panel_source,
                  "viewport_chrome_combo_width_for_text");
  expect_contains(viewport_panel_source, "ImGui::CalcTextSize");
  expect_contains(viewport_panel_source, "kViewportChromeViews");
  expect_contains(viewport_panel_source, "kViewportChromeShadingModes");
  expect_missing(viewport_panel_source,
                 "constexpr float kViewComboWidth = 112.0f");
  expect_missing(viewport_panel_source,
                 "constexpr float kShadingComboWidth = 104.0f");
}

TEST(BuildGuardrailTests, ImGuiViewportApiKeepsMultiPaneLayoutWorkspace) {
  const std::string viewport_api_source =
      read_source_file("src/api/quader_viewport_api.cpp");
  const std::string main_menu_source =
      read_source_file("src/ui/main_menu_bar.cpp");
  const std::string viewport_panel_source =
      read_source_file("src/ui/viewport_panel.cpp");

  expect_contains(viewport_api_source,
                  "ViewportWorkspaceService "
                  "workspace{ViewportWorkspaceLayoutPolicy::MultiPane}");
  expect_missing(viewport_api_source,
                 "ViewportWorkspaceService "
                 "workspace{ViewportWorkspaceLayoutPolicy::SinglePaneOnly}");
  expect_contains(main_menu_source,
                  "context.editor.viewports().set_layout(option.id)");
  expect_contains(viewport_panel_source,
                  "viewport_workspace.active_pane_placements()");
  expect_contains(viewport_panel_source,
                  "viewport_workspace.active_split_handles()");
  expect_missing(viewport_panel_source,
                 "return workspace().active_pane_placements()");
  expect_missing(viewport_panel_source,
                 "return workspace().active_split_handles()");
}

TEST(BuildGuardrailTests, RuntimeUiUsesNativeImGuiStackOnly) {
  const std::string root_c_make = read_source_file("CMakeLists.txt");
  const std::string manifest = read_source_file("vcpkg.json");

  expect_contains(root_c_make, "find_package(imgui CONFIG REQUIRED)");
  expect_contains(root_c_make, "imgui::imgui");
  expect_contains(manifest, "\"imgui\"");

  for (const std::filesystem::path &relative_path : guardrail_source_files()) {
    const std::string path = relative_path.generic_string();
    if (!starts_with(path, "src/")) {
      continue;
    }

    const std::string source = read_source_file(relative_path);
    expect_missing(source, "src/ui/controllers");
    expect_missing(source, "src/ui/components");
    expect_missing(source, "src/ui/core");
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

TEST(WindowGuardrailTests, BorderlessWindowModeIsDefault) {
  const std::string root_c_make = read_source_file("CMakeLists.txt");
  const std::string main_source = read_source_file("src/main.cpp");
  const std::string app_header = read_source_file("src/app/app.hpp");
  const std::string app_source = read_source_file("src/app/app.cpp");
  const std::string platform_header =
      read_source_file("src/platform/platform_window.hpp");
  const std::string platform_source =
      read_source_file("src/platform/platform_window.cpp");
  const std::string ui_system_header = read_source_file("src/ui/ui_system.hpp");
  const std::string ui_system_source = read_source_file("src/ui/ui_system.cpp");

  expect_contains(main_source, "bool borderless_window = true");
  expect_contains(main_source, "--borderless-window");
  expect_contains(main_source, "--native-window");
  expect_contains(main_source, "--no-borderless-window");
  expect_contains(main_source, "--render-backend=vulkan");
  expect_contains(main_source, "--render-backend=opengl");
  expect_contains(main_source, "command_line.render_backend");
  expect_contains(main_source, "command_line.borderless_window");
  expect_contains(app_header, "struct AppOptions");
  expect_contains(app_header, "bool borderless_window = true");
  expect_contains(app_header,
                  "RendererBackend render_backend = RendererBackend::Vulkan");
  expect_contains(app_header,
                  "handle_window_chrome_action(WindowChromeAction action)");
  expect_contains(app_source, ".borderless = options.borderless_window");
  expect_contains(app_source, ".backend = options.render_backend");
  expect_contains(app_source, "window_.process_chrome_event(event)");
  expect_contains(app_source, "ui_.take_window_chrome_action()");
  expect_contains(app_source, "window_.refresh_native_chrome()");
  expect_contains(app_source, "window_.minimize()");
  expect_contains(app_source, "window_.toggle_maximize_restore()");
  expect_contains(app_source, "window_.can_render_frames()");
  expect_contains(app_source, "render_frame(frame_clock_.tick())");
  expect_contains(app_source, "render_system_.begin_frame()");
  expect_contains(app_source,
                  "if (window_.native_chrome_interaction_active()) {");
  expect_contains(app_source, "return false;");
  expect_contains(app_source, "if (!window_.chrome_resize_active())");
  expect_missing(app_source, "!window_.chrome_resize_active() && "
                             "!window_.native_chrome_interaction_active()");
  expect_contains(app_source, "window_.native_chrome_interaction_active()");
  expect_contains(app_source, "wait_until_next_frame()");
  expect_contains(app_source, "platform_wait_for_event_timeout(wait_ms)");
  expect_contains(platform_header, "struct PlatformWindowOptions");
  expect_contains(platform_header, "kPlatformWindowMinimumWidth = 800");
  expect_contains(platform_header, "kPlatformWindowMinimumHeight = 600");
  expect_contains(platform_header, "refresh_native_chrome()");
  expect_contains(platform_header, "platform_window_can_render_frames");
  expect_contains(platform_header, "can_render_frames() const");
  expect_contains(platform_header,
                  "note_frame_presented(std::uint32_t framebuffer_width,");
  expect_contains(platform_header, "PlatformWindowChromeHitTest");
  expect_contains(platform_header, "platform_window_chrome_hit_test_result");
  expect_contains(platform_header, "PlatformWindowChromeResizeDrag");
  expect_contains(platform_header, "process_chrome_event");
  expect_contains(platform_header, "release_chrome_resize_cursor");
  expect_contains(platform_header, "native_chrome_message_active() const");
  expect_contains(platform_header, "native_live_resize_frame_active() const");
  expect_contains(platform_header, "native_chrome_interaction_active() const");
  expect_contains(platform_header,
                  "using LiveResizeFrameCallback = std::function<bool()>");
  expect_contains(platform_header, "schedule_live_resize_frame_callback");
  expect_contains(platform_header, "dispatch_live_resize_frame_callback");
  expect_contains(platform_header, "native_chrome_message_depth_");
  expect_contains(platform_header, "native_move_size_active_");
  expect_contains(platform_header, "native_live_resize_frame_active_");
  expect_contains(platform_header, "live_resize_frame_callback_pending_");
  expect_contains(platform_header, "live_resize_frame_pending_width_pixels_");
  expect_contains(platform_header, "prime_live_resize_frame_coalescing");
  expect_contains(platform_header, "apply_native_custom_chrome_styles");
  expect_missing(platform_source, "SDL_WINDOW_BORDERLESS");
  expect_contains(platform_source, "install_native_chrome_cursor_hook();");
  expect_contains(platform_source, "apply_native_custom_chrome_styles();");
  expect_contains(platform_source, "WS_CAPTION");
  expect_contains(platform_source, "WS_THICKFRAME");
  expect_contains(platform_source, "SWP_FRAMECHANGED");
  expect_contains(
      platform_source,
      "GetClientRect(static_cast<HWND>(native_chrome_window_handle_),");
  expect_contains(platform_source, "SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH");
  expect_contains(platform_source,
                  "SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, \"1\")");
  expect_contains(platform_source,
                  "SDL_SetWindowMinimumSize(window_, "
                  "kPlatformWindowMinimumWidth, kPlatformWindowMinimumHeight)");
  expect_contains(platform_source, "SDL_WINDOW_INPUT_FOCUS");
  expect_contains(platform_source, "SDL_HITTEST_DRAGGABLE");
  expect_contains(platform_source, "SDL_HITTEST_RESIZE_TOPLEFT");
  expect_contains(platform_source, "SDL_CaptureMouse");
  expect_contains(platform_source, "SDL_GetGlobalMouseState");
  expect_contains(platform_source, "SDL_SetWindowPosition");
  expect_contains(platform_source, "SDL_SetWindowSize");
  expect_contains(platform_source, "SDL_MinimizeWindow");
  expect_contains(platform_source, "SDL_SyncWindow failed after minimize");
  expect_contains(platform_source, "SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN");
  expect_contains(platform_source, "SDL occlusion can remain stale");
  expect_contains(platform_source, "SDL_MaximizeWindow");
  expect_contains(platform_source, "SDL_RestoreWindow");
  expect_contains(platform_source, "HTLEFT");
  expect_contains(platform_source, "HTTOPRIGHT");
  expect_contains(platform_source, "WM_SETCURSOR");
  expect_contains(platform_source, "chrome_resize_cursor_active_");
  expect_contains(platform_source, "SetWindowLongPtrW");
  expect_contains(platform_source, "GWLP_WNDPROC");
  expect_contains(platform_source, "IDC_SIZEWE");
  expect_contains(platform_source, "case SDL_HITTEST_DRAGGABLE:");
  expect_contains(platform_source, "return HTCAPTION;");
  expect_contains(platform_source,
                  "is_native_chrome_system_hit(SDL_HitTestResult hit)");
  expect_contains(platform_source, "native_chrome_window_handle_ != nullptr");
  expect_contains(platform_source, "is_native_chrome_system_hit(hit)");
  expect_contains(platform_source,
                  "return native_chrome_hit_test_code_for_hit(hit);");
  expect_contains(platform_source, "message == WM_ERASEBKGND");
  expect_contains(platform_source, "return 1;");
  expect_contains(platform_source, "message == WM_PAINT");
  expect_contains(platform_source,
                  "live_resize_frame_paint_scheduled_ = false");
  expect_contains(platform_source, "BeginPaint(hwnd, &paint)");
  expect_contains(platform_source, "EndPaint(hwnd, &paint)");
  expect_contains(platform_source, "NativeChromeMessageScope");
  expect_contains(platform_source, "native_move_size_active_ = true");
  expect_contains(platform_source,
                  "message == WM_SIZE && native_move_size_active_");
  expect_contains(platform_source, "redraw_native_resize_frame");
  expect_contains(platform_source, "const bool redraw_native_resize_frame =");
  expect_contains(platform_source,
                  "(message != WM_SIZE || w_param != SIZE_MINIMIZED)");
  expect_missing(platform_source, "const bool redraw_native_resize_frame =\n   "
                                  "         (message != WM_SIZE "
                                  "|| w_param != SIZE_MINIMIZED) &&\n          "
                                  "  (message == WM_SIZE ||\n   "
                                  "                 message == WM_SIZING");
  expect_missing(platform_source, "message == WM_WINDOWPOSCHANGED ||\n         "
                                  "           message == WM_DPICHANGED");
  expect_contains(platform_source, "prime_live_resize_frame_coalescing();");
  expect_missing(
      platform_source,
      "native_move_size_active_ &&\n            native_resize_active_ &&");
  expect_missing(platform_source, "kNativeMoveSizeRedrawTimerId");
  expect_missing(platform_source, "SetTimer(");
  expect_missing(platform_source, "KillTimer(");
  expect_missing(platform_source, "message == WM_TIMER");
  expect_contains(platform_source, "kLiveResizeFrameMessage = WM_APP");
  expect_contains(platform_source, "message == kLiveResizeFrameMessage");
  expect_contains(platform_source,
                  "PostMessageW(hwnd, kLiveResizeFrameMessage");
  expect_contains(platform_source, "owner.native_chrome_message_depth_ == 0");
  expect_contains(platform_source, "owner.live_resize_frame_callback_pending_");
  expect_contains(platform_source,
                  "owner.schedule_live_resize_frame_callback()");
  expect_contains(platform_source, "native_chrome_message_depth_ > 0");
  expect_contains(
      platform_source,
      "RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE)");
  expect_missing(platform_source, "RDW_UPDATENOW");
  expect_missing(platform_source,
                 "dispatch_live_resize_frame_callback();\n            return;");
  expect_contains(platform_source, "native_live_resize_frame_active_ = true");
  expect_contains(platform_source, "if (live_resize_frame_callback_())");
  expect_contains(platform_source,
                  "note_frame_presented(pending_width, pending_height)");
  expect_contains(platform_source,
                  "live_resize_frame_pending_width_pixels_ = pending_width");
  expect_contains(platform_source,
                  "live_resize_frame_pending_height_pixels_ = pending_height");
  expect_contains(
      platform_source,
      "native_move_size_active_ || native_chrome_message_depth_ > 0");
  expect_missing(
      platform_source,
      "void PlatformWindow::prime_live_resize_frame_coalescing() {\n    "
      "refresh_pixel_size();\n    live_resize_frame_size_valid_ = true;");
  expect_contains(platform_source,
                  "SetCursor(LoadCursor(nullptr, IDC_ARROW));");
  expect_contains(platform_source,
                  "message == WM_SETCURSOR && hit == SDL_HITTEST_DRAGGABLE");
  expect_contains(platform_source, "clear_native_move_size_state");
  expect_missing(platform_source,
                 "(native_resize_active_ || IsZoomed(hwnd) != FALSE)");
  expect_contains(platform_source, "DWMWA_WINDOW_CORNER_PREFERENCE");
  expect_contains(platform_source, "DWMWA_BORDER_COLOR");
  expect_contains(platform_source, "DWMWA_COLOR_DEFAULT");
  expect_contains(platform_source, "RegGetValueW");
  expect_contains(platform_source, "ColorPrevalence");
  expect_contains(platform_source, "AccentColor");
  expect_missing(platform_source, "SDL_SetWindowHitTest");
  expect_missing(platform_source, "WM_NCLBUTTONDOWN");
  expect_contains(root_c_make, "Dwmapi");
  expect_contains(root_c_make, "Advapi32");
  expect_contains(ui_system_header, "enum class WindowChromeAction");
  expect_contains(ui_system_header, "take_window_chrome_action()");
  expect_contains(ui_system_header, "needs_continuous_rendering() const");
  expect_contains(ui_system_header,
                  "std::optional<WindowChromeAction> pending_chrome_action_");
  expect_contains(ui_system_source,
                  "viewport_panel_->needs_continuous_rendering()");
  expect_contains(
      ui_system_source,
      "chrome_icon_button(\"minimize\", WindowChromeAction::Minimize");
  expect_contains(ui_system_source,
                  "chrome_icon_button(\"restore\", "
                  "WindowChromeAction::ToggleMaximizeRestore");
  expect_contains(ui_system_source,
                  "chrome_icon_button(\"close\", WindowChromeAction::Close");
  expect_contains(ui_system_source,
                  "pending_chrome_action_ = WindowChromeAction::Close");
}

TEST(WindowGuardrailTests, BorderlessMaximizeUsesMonitorWorkArea) {
  const std::string platform_source =
      read_source_file("src/platform/platform_window.cpp");

  expect_contains(platform_source, "native_monitor_work_area");
  expect_contains(platform_source,
                  "apply_borderless_chrome_maximized_work_area");
  expect_contains(platform_source,
                  "apply_borderless_chrome_maximized_client_area");
  expect_contains(platform_source, "WM_GETMINMAXINFO");
  expect_contains(platform_source, "WM_NCCALCSIZE");
  expect_contains(platform_source, "NCCALCSIZE_PARAMS");
  expect_contains(platform_source, "ShowWindow(hwnd, SW_MAXIMIZE)");
  expect_contains(platform_source, "ShowWindow(hwnd, SW_RESTORE)");
  expect_contains(platform_source,
                  "MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST)");
  expect_contains(platform_source, "GetMonitorInfoW(monitor, &monitor_info)");
  expect_contains(platform_source, "monitor_info.rcWork");
  expect_contains(platform_source, "monitor_info.rcMonitor");
  expect_contains(
      platform_source,
      "min_max_info.ptMaxPosition.x = work_area.left - monitor_area.left");
  expect_contains(
      platform_source,
      "min_max_info.ptMaxSize.y = work_area.bottom - work_area.top");
  expect_contains(platform_source,
                  "min_max_info.ptMaxTrackSize.y = min_max_info.ptMaxSize.y");
  expect_contains(platform_source, "GetSystemMetricsForDpi(SM_CXSIZEFRAME");
  expect_contains(platform_source, "GetSystemMetricsForDpi(SM_CXPADDEDBORDER");
  expect_contains(platform_source, "params.rgrc[0] = area->work");
  expect_contains(platform_source, "message == WM_SIZE");
  expect_contains(platform_source, "message == WM_SIZING");
  expect_missing(platform_source, "message == WM_SIZING ||\n                   "
                                  " message == WM_WINDOWPOSCHANGED");
  expect_missing(platform_source, "message == WM_MOVING");
  expect_missing(platform_source, "message == WM_WINDOWPOSCHANGED");
  expect_contains(platform_source, "message == WM_EXITSIZEMOVE");
  expect_contains(platform_source, "request_live_resize_frame();");
  expect_contains(platform_source, "request_live_resize_frame(true)");
  expect_missing(platform_source, "constrainBorderlessChromeMaximizedFrame");
  expect_missing(
      platform_source,
      "SDL_SyncWindow failed after borderless maximize work-area constraint");
  expect_missing(platform_source,
                 "message == WM_WINDOWPOSCHANGED && IsZoomed(hwnd) != FALSE");
  expect_contains(platform_source,
                  "min_max_info.ptMinTrackSize.x = "
                  "std::max<LONG>(min_max_info.ptMinTrackSize.x,");
  expect_contains(platform_source, "kPlatformWindowMinimumWidth)");
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
  const std::string platform_header =
      read_source_file("src/platform/platform_window.hpp");
  const std::string platform_source =
      read_source_file("src/platform/platform_window.cpp");
  const std::string ui_settings_source =
      read_source_file("src/ui/ui_settings.cpp");
  const std::string config_path_source =
      read_source_file("src/core/user_config_paths.cpp");

  expect_contains(root_c_make, "src/core/user_config_paths.cpp");
  expect_contains(root_c_make, "src/app/app_window_settings.cpp");
  expect_contains(app_header, "AppWindowSettings window_settings_");
  expect_contains(app_header, "save_window_settings()");
  expect_contains(app_source,
                  "AppWindowSettings::resolve_path(executable_dir)");
  expect_contains(app_source, "AppWindowSettings::load(window_settings_path_)");
  expect_contains(
      app_source,
      "const std::optional<PlatformWindowPlacement> saved_placement");
  expect_contains(app_source, "const PlatformWindowChromeFrame initial_frame");
  expect_contains(app_source,
                  "std::max(kPlatformWindowMinimumWidth, initial_frame.width)");
  expect_contains(
      app_source,
      "std::max(kPlatformWindowMinimumHeight, initial_frame.height)");
  expect_contains(app_source, "window_.apply_placement(*saved_placement)");
  expect_contains(app_source, "window_.placement()");
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
  expect_contains(platform_header, "struct PlatformWindowPlacement");
  expect_contains(platform_header,
                  "platform_window_chrome_resize_border_for_system_metrics");
  expect_contains(platform_header,
                  "platform_window_recover_frame_to_display_work_area");
  expect_contains(platform_header,
                  "std::optional<PlatformWindowPlacement> placement() const");
  expect_contains(platform_header,
                  "apply_placement(const PlatformWindowPlacement &placement)");
  expect_contains(platform_header, "live_resize_frame_size_valid_");
  expect_contains(platform_source, "GetSystemMetricsForDpi(SM_CXSIZEFRAME");
  expect_contains(platform_source, "GetSystemMetricsForDpi(SM_CYSIZEFRAME");
  expect_contains(platform_source,
                  "GetWindowPlacement(hwnd, &native_placement)");
  expect_contains(platform_source,
                  "SetWindowPlacement(hwnd, &native_placement)");
  expect_contains(platform_source, "WPF_RESTORETOMAXIMIZED");
  expect_contains(platform_source, "SDL_GetDisplayUsableBounds");
  expect_contains(platform_source,
                  "platform_window_recover_frame_to_display_work_area");
  expect_contains(platform_source, "WM_CANCELMODE");
  expect_contains(platform_source, "WM_CAPTURECHANGED");
  expect_contains(platform_source, "clear_native_move_size_state()");
  expect_contains(platform_source, "request_live_resize_frame(true)");
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
