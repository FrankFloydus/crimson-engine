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
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace {

using quader::test::guardrail::normalized_relative;
using quader::test::guardrail::read_source_file;
using quader::test::guardrail::source_files_under;
using quader::test::guardrail::source_root;
using quader::test::guardrail::split_lines;
using quader::test::guardrail::starts_with;

/**
 * Declares the source-include dependency row for one project module.
 */
struct SourceModulePolicy {
  std::string_view module;
  std::vector<std::string_view> path_prefixes;
  std::vector<std::string_view> allowed_dependencies;
};

/**
 * Records a temporary include edge that the matrix intentionally keeps visible.
 */
struct IncludeException {
  std::string_view includer;
  std::string_view include_path;
  std::string_view reason;
};

/**
 * Captures one parsed CMake project-target link edge.
 */
struct CMakeLinkEdge {
  std::string target;
  std::string dependency;
  std::string source_file;
};

/**
 * Declares the allowed project-target links for one CMake target.
 */
struct CMakeTargetPolicy {
  std::string_view target;
  std::vector<std::string_view> allowed_project_links;
};

const std::vector<SourceModulePolicy> &source_module_policies() {
  static const std::vector<SourceModulePolicy> kPolicies = {
      {"editor_commands",
       {"src/editor/commands/"},
       {"assets", "core", "diagnostics", "editor_commands",
        "editor_history", "editor_metadata", "editor_modeling",
        "editor_selection", "scene"}},
      {"editor_history",
       {"src/editor/history/"},
       {"core", "editor_commands", "editor_history", "editor_modeling",
        "scene"}},
      {"editor_input",
       {"src/editor/input/"},
       {"api", "editor_input", "viewport"}},
      {"editor_session",
       {"src/editor/session/"},
       {"assets", "diagnostics", "editor_commands", "editor_history",
        "editor_modeling", "editor_selection", "editor_session", "scene"}},
      {"editor_interactions",
       {"src/editor/interactions/"},
       {"diagnostics", "editor_interactions", "editor_modeling",
        "mesh_geometry", "mesh_polygon"}},
      {"editor_metadata",
       {"src/editor/metadata/"},
       {"assets", "core", "editor_interactions", "editor_metadata",
        "editor_modeling", "editor_tools", "viewport"}},
      {"editor_modeling",
       {"src/editor/modeling/"},
       {"assets", "core", "diagnostics", "editor_commands",
        "editor_history", "editor_interactions", "editor_metadata",
        "editor_modeling", "editor_projection", "editor_selection",
        "editor_tools", "editor_transform", "math", "mesh_geometry",
        "mesh_polygon", "render", "scene", "viewport"}},
      {"editor_projection",
       {"src/editor/projection/"},
       {"assets", "core", "diagnostics", "editor_interactions",
        "editor_modeling", "editor_projection", "math", "mesh_polygon",
        "render", "scene"}},
      {"editor_render",
       {"src/editor/render/"},
       {"api", "render"}},
      {"editor_selection",
       {"src/editor/selection/"},
       {"assets", "core", "diagnostics", "editor_commands",
        "editor_modeling", "editor_selection", "math", "mesh_polygon",
        "scene"}},
      {"editor_tools",
       {"src/editor/tools/"},
       {"assets", "core", "editor_interactions", "editor_modeling",
        "editor_tools", "scene", "viewport"}},
      {"editor_transform",
       {"src/editor/transform/"},
       {"api", "assets", "editor_commands", "editor_history",
        "editor_transform", "math", "scene"}},
      {"editor_viewport",
       {"src/editor/viewport/"},
       {"api", "core", "diagnostics", "editor_interactions",
        "editor_modeling", "editor_selection", "editor_tools",
        "editor_viewport", "render", "viewport"}},
      {"mesh_geometry", {"src/mesh/geometry/"}, {"mesh_geometry"}},
      {"mesh_polygon",
       {"src/mesh/polygon/"},
       {"diagnostics", "math", "mesh_geometry", "mesh_polygon"}},
      {"io_qdr",
       {"src/io/qdr/"},
       {"assets", "core", "diagnostics", "io_qdr", "mesh_polygon",
        "scene"}},
      {"api",
       {"src/api/"},
       {"api", "assets", "core", "diagnostics", "editor_commands",
        "editor_history", "editor_input", "editor_modeling",
        "editor_render", "editor_selection", "editor_session", "editor_tools",
        "editor_transform", "editor_viewport", "io_qdr", "math",
        "mesh_polygon", "render", "scene", "viewport"}},
      {"app",
       {"src/app/", "src/main.cpp"},
       {"api", "app", "assets", "core", "diagnostics", "editor_render",
        "editor_session", "platform", "render", "scene", "ui"}},
      {"assets", {"src/assets/"}, {"assets", "core", "math"}},
      {"core", {"src/core/"}, {"core"}},
      {"diagnostics", {"src/diagnostics/"}, {"diagnostics"}},
      {"math", {"src/math/"}, {"math"}},
      {"platform", {"src/platform/"}, {"core", "editor_input", "platform"}},
      {"render",
       {"src/render/"},
       {"assets", "core", "diagnostics", "editor_interactions",
        "editor_modeling", "editor_viewport", "math", "mesh_polygon",
        "render", "scene", "viewport"}},
      {"scene",
       {"src/scene/"},
       {"assets", "core", "diagnostics", "math", "scene"}},
      {"ui",
       {"src/ui/"},
       {"api", "assets", "core", "diagnostics", "editor_commands",
        "editor_interactions", "editor_modeling", "editor_render", "editor_tools",
        "editor_transform", "editor_viewport", "math", "render", "scene",
        "ui", "viewport"}},
      {"viewport",
       {"src/viewport/"},
       {"api", "core", "diagnostics", "editor_commands", "editor_input",
        "editor_interactions", "editor_metadata", "editor_modeling",
        "editor_tools", "math", "render", "viewport"}},
  };
  return kPolicies;
}

const std::vector<IncludeException> &allowed_include_exceptions() {
  static const std::vector<IncludeException> kExceptions = {
      {"src/editor/commands/editor_command_registry.hpp",
       "api/quader_editor_api_types.hpp",
       "command registry still consumes public editor command value types"},
      {"src/io/qdr/qdr_document_reader.cpp",
       "editor/projection/mesh_projection_service.hpp",
       "project QDR reader still rebuilds mesh payloads through editor "
       "projection"},
      {"src/io/qdr/qdr_runtime_io_compat.hpp",
       "editor/modeling/runtime/session.hpp",
       "runtime compatibility I/O is still in the QDR module"},
      {"src/scene/document/project_session.hpp",
       "editor/session/project_session.hpp",
       "legacy scene include wrapper during the session move"},
  };
  return kExceptions;
}

const std::vector<CMakeTargetPolicy> &cmake_target_policies() {
  static const std::vector<CMakeTargetPolicy> kPolicies = {
      {"filament_sdk", {}},
      {"quader", {"quader_app", "quader_runtime"}},
      {"quader_api",
       {"quader_assets", "quader_commands", "quader_core",
        "quader_document", "quader_editor_modeling", "quader_io_qdr",
        "quader_modeling_profile", "quader_render", "quader_viewport"}},
      {"quader_app",
       {"quader_api", "quader_assets", "quader_core", "quader_document",
        "quader_modeling_profile", "quader_platform", "quader_render",
        "quader_ui", "quader_viewport"}},
      {"quader_assets",
       {"quader_core", "quader_core_utilities", "quader_math",
        "quader_modeling_polygon",
        "quader_polygon"}},
      {"quader_commands",
       {"quader_assets", "quader_core", "quader_document",
        "quader_editor_metadata", "quader_editor_modeling",
        "quader_modeling", "quader_modeling_profile",
        "quader_modeling_runtime", "quader_viewport"}},
      {"quader_core",
       {"quader_assets", "quader_core_ui_support",
        "quader_core_utilities", "quader_document",
        "quader_editor_services", "quader_project_qdr_io",
        "quader_viewport_services"}},
      {"quader_core_ui_support", {"quader_core_utilities"}},
      {"quader_core_utilities", {}},
      {"quader_document", {"quader_assets", "quader_core", "quader_math"}},
      {"quader_editor_input",
       {"quader_api", "quader_core", "quader_viewport"}},
      {"quader_editor_metadata", {"quader_modeling_runtime"}},
      {"quader_editor_modeling",
       {"quader_assets", "quader_commands", "quader_core",
        "quader_document", "quader_editor_metadata", "quader_geometry",
        "quader_io_qdr", "quader_modeling", "quader_modeling_polygon",
        "quader_modeling_profile", "quader_modeling_runtime",
        "quader_polygon", "quader_qdr_runtime_io", "quader_render",
        "quader_viewport"}},
      {"quader_editor_services",
       {"quader_document", "quader_modeling", "quader_modeling_profile"}},
      {"quader_geometry", {"quader_math", "quader_modeling_gte_math"}},
      {"quader_guardrail_tests", {}},
      {"quader_io_qdr",
       {"quader_assets", "quader_core", "quader_document",
        "quader_editor_modeling", "quader_modeling_polygon",
        "quader_modeling_profile", "quader_modeling_runtime",
        "quader_polygon", "quader_qdr_document_io",
        "quader_qdr_runtime_io", "quader_qdr_shared_io"}},
      {"quader_math", {"quader_core"}},
      {"quader_modeling",
       {"quader_editor_metadata", "quader_modeling_profile",
        "quader_modeling_runtime", "quader_qdr_runtime_io"}},
      {"quader_modeling_domain_metadata_loop_tests",
       {"quader_editor_metadata"}},
      {"quader_modeling_domain_polygon_loop_tests",
       {"quader_geometry", "quader_modeling_polygon",
        "quader_qdr_document_io"}},
      {"quader_modeling_domain_runtime_loop_tests",
       {"quader_editor_metadata", "quader_geometry",
        "quader_modeling_polygon", "quader_modeling_runtime",
        "quader_qdr_document_io", "quader_qdr_runtime_io"}},
      {"quader_modeling_gte_math", {}},
      {"quader_modeling_polygon",
       {"quader_geometry", "quader_math", "quader_modeling_profile"}},
      {"quader_modeling_profile", {}},
      {"quader_modeling_runtime",
       {"quader_geometry", "quader_modeling_polygon",
        "quader_modeling_profile"}},
      {"quader_platform", {"quader_core", "quader_editor_input"}},
      {"quader_polygon",
       {"quader_geometry", "quader_math", "quader_modeling_profile"}},
      {"quader_project_qdr_io",
       {"quader_document", "quader_modeling", "quader_modeling_profile",
        "quader_qdr_document_io", "quader_qdr_shared_io"}},
      {"quader_qdr_document_io",
       {"quader_modeling_polygon", "quader_modeling_profile",
        "quader_qdr_shared_io"}},
      {"quader_qdr_modeling_runtime_io",
       {"quader_modeling_profile", "quader_modeling_runtime",
        "quader_qdr_document_io"}},
      {"quader_qdr_runtime_io",
       {"quader_modeling_profile", "quader_modeling_runtime",
        "quader_qdr_document_io"}},
      {"quader_qdr_shared_io",
       {"quader_modeling_polygon", "quader_modeling_profile"}},
      {"quader_render",
       {"filament_sdk", "quader_api", "quader_assets", "quader_core",
        "quader_document", "quader_editor_modeling", "quader_geometry",
        "quader_modeling_polygon", "quader_modeling_profile",
        "quader_polygon", "quader_viewport"}},
      {"quader_runtime",
       {"quader_api", "quader_app", "quader_modeling_profile",
        "quader_platform", "quader_render", "quader_ui"}},
      {"quader_tests", {"quader_app", "quader_runtime"}},
      {"quader_ui",
       {"quader_api", "quader_assets", "quader_commands", "quader_core",
        "quader_document", "quader_editor_modeling",
        "quader_modeling_profile", "quader_render", "quader_viewport"}},
      {"quader_viewport",
       {"quader_api", "quader_commands", "quader_core",
        "quader_editor_metadata", "quader_editor_modeling",
        "quader_math", "quader_render"}},
      {"quader_viewport_services",
       {"quader_core_utilities", "quader_editor_metadata",
        "quader_modeling", "quader_modeling_profile"}},
      {"qdr_performance_baseline",
       {"quader_modeling_polygon", "quader_modeling_profile",
        "quader_modeling_runtime"}},
  };
  return kPolicies;
}

bool contains_value(const std::vector<std::string_view> &values,
                    std::string_view value) {
  return std::find(values.begin(), values.end(), value) != values.end();
}

std::optional<std::string_view> module_for_relative_path(
    std::string_view relative_path) {
  for (const SourceModulePolicy &policy : source_module_policies()) {
    for (std::string_view prefix : policy.path_prefixes) {
      if (relative_path == prefix || starts_with(relative_path, prefix)) {
        return policy.module;
      }
    }
  }
  return std::nullopt;
}

const SourceModulePolicy *source_policy_for(std::string_view module) {
  for (const SourceModulePolicy &policy : source_module_policies()) {
    if (policy.module == module) {
      return &policy;
    }
  }
  return nullptr;
}

bool source_matrix_allows(std::string_view module,
                          std::string_view dependency) {
  const SourceModulePolicy *policy = source_policy_for(module);
  return policy != nullptr &&
         contains_value(policy->allowed_dependencies, dependency);
}

std::optional<std::string>
project_include_relative_path(const std::filesystem::path &includer,
                              std::string_view include_path) {
  const std::string include_string(include_path);
  const std::filesystem::path include_file(include_string);
  const std::filesystem::path project_relative =
      starts_with(include_string, "src/") ? source_root() / include_file
                                          : source_root() / "src" / include_file;
  if (std::filesystem::exists(project_relative)) {
    return normalized_relative(project_relative);
  }

  const std::filesystem::path local_relative = includer.parent_path() / include_file;
  if (std::filesystem::exists(local_relative)) {
    return normalized_relative(local_relative);
  }

  return std::nullopt;
}

std::optional<std::string> include_path_from_line(const std::string &line) {
  static const std::regex kIncludeRegex(R"(^\s*#include\s+[<"]([^>"]+)[>"])");
  std::smatch match;
  if (!std::regex_search(line, match, kIncludeRegex)) {
    return std::nullopt;
  }
  return match[1].str();
}

std::string include_exception_key(std::string_view includer,
                                  std::string_view include_path) {
  return std::string(includer) + " -> " + std::string(include_path);
}

const IncludeException *allowed_include_exception(std::string_view includer,
                                                  std::string_view include_path) {
  for (const IncludeException &exception : allowed_include_exceptions()) {
    if (exception.includer == includer &&
        exception.include_path == include_path) {
      return &exception;
    }
  }
  return nullptr;
}

bool is_cmake_visibility_token(std::string_view token) {
  return token == "PUBLIC" || token == "PRIVATE" || token == "INTERFACE";
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
    if (!in_quote &&
        std::isspace(static_cast<unsigned char>(character)) != 0) {
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

const CMakeTargetPolicy *cmake_policy_for(std::string_view target) {
  for (const CMakeTargetPolicy &policy : cmake_target_policies()) {
    if (policy.target == target) {
      return &policy;
    }
  }
  return nullptr;
}

bool is_known_project_cmake_target(std::string_view target) {
  return cmake_policy_for(target) != nullptr;
}

bool is_dynamic_cmake_token(std::string_view token) {
  return token.find('$') != std::string_view::npos;
}

bool looks_like_concrete_project_cmake_target(std::string_view token) {
  return !is_dynamic_cmake_token(token) &&
         (starts_with(token, "quader") || starts_with(token, "qdr_") ||
          token == "filament_sdk");
}

std::vector<CMakeLinkEdge> project_cmake_link_edges() {
  std::vector<CMakeLinkEdge> edges;

  for (const std::filesystem::path &relative_path :
       {std::filesystem::path("CMakeLists.txt"),
        std::filesystem::path("cmake/QuaderModeling.cmake")}) {
    const std::string cmake = read_source_file(relative_path);
    std::size_t cursor = 0;
    while ((cursor = cmake.find("target_link_libraries(", cursor)) !=
           std::string::npos) {
      const std::size_t args_begin = cursor + std::string_view(
                                               "target_link_libraries(")
                                               .size();
      const std::size_t args_end = cmake.find(')', args_begin);
      if (args_end == std::string::npos) {
        ADD_FAILURE() << relative_path.generic_string()
                      << ": unterminated target_link_libraries call";
        break;
      }

      const std::vector<std::string> tokens =
          tokenize_cmake_arguments(std::string_view(cmake).substr(
              args_begin, args_end - args_begin));
      cursor = args_end + 1;
      if (tokens.empty() || is_dynamic_cmake_token(tokens.front())) {
        continue;
      }

      const std::string &target = tokens.front();
      for (std::size_t index = 1; index < tokens.size(); ++index) {
        const std::string &dependency = tokens[index];
        if (is_cmake_visibility_token(dependency) ||
            is_dynamic_cmake_token(dependency)) {
          continue;
        }
        if (is_known_project_cmake_target(dependency) ||
            looks_like_concrete_project_cmake_target(dependency)) {
          edges.push_back({target, dependency, relative_path.generic_string()});
        }
      }
    }
  }

  return edges;
}

} // namespace

TEST(DependencyMatrixGuardrailTests,
     SourceProjectIncludesMatchModuleDependencyMatrix) {
  std::set<std::string> used_exceptions;

  for (const std::filesystem::path &file :
       source_files_under(source_root() / "src")) {
    const std::string includer = normalized_relative(file);
    const std::optional<std::string_view> source_module =
        module_for_relative_path(includer);
    ASSERT_TRUE(source_module.has_value()) << includer;

    const std::vector<std::string> lines = split_lines(read_source_file(includer));
    for (std::size_t line_index = 0; line_index < lines.size(); ++line_index) {
      const std::optional<std::string> include_path =
          include_path_from_line(lines[line_index]);
      if (!include_path.has_value()) {
        continue;
      }

      const std::optional<std::string> dependency_relative =
          project_include_relative_path(file, *include_path);
      if (!dependency_relative.has_value()) {
        continue;
      }

      const std::optional<std::string_view> dependency_module =
          module_for_relative_path(*dependency_relative);
      ASSERT_TRUE(dependency_module.has_value()) << *dependency_relative;

      if (source_matrix_allows(*source_module, *dependency_module)) {
        continue;
      }

      const IncludeException *exception =
          allowed_include_exception(includer, *include_path);
      if (exception != nullptr) {
        used_exceptions.insert(
            include_exception_key(exception->includer, exception->include_path));
        continue;
      }

      ADD_FAILURE() << includer << ":" << (line_index + 1) << " includes "
                    << *include_path << " (" << *source_module << " -> "
                    << *dependency_module
                    << ") outside the project dependency matrix";
    }
  }

  for (const IncludeException &exception : allowed_include_exceptions()) {
    EXPECT_TRUE(used_exceptions.contains(
        include_exception_key(exception.includer, exception.include_path)))
        << exception.includer << " -> " << exception.include_path << ": "
        << exception.reason;
  }
}

TEST(DependencyMatrixGuardrailTests,
     CMakeProjectTargetLinksMatchDependencyMatrix) {
  for (const CMakeLinkEdge &edge : project_cmake_link_edges()) {
    const CMakeTargetPolicy *policy = cmake_policy_for(edge.target);
    ASSERT_NE(policy, nullptr)
        << edge.source_file << ": add target " << edge.target
        << " to the CMake dependency matrix before linking project target "
        << edge.dependency;

    EXPECT_TRUE(contains_value(policy->allowed_project_links, edge.dependency))
        << edge.source_file << ": target " << edge.target << " links "
        << edge.dependency << " outside the CMake dependency matrix";
  }
}
