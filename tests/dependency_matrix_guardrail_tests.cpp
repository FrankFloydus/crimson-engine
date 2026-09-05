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
#include <utility>
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
 * Records a CMake source fragment for parser tests and the production graph.
 */
struct CMakeSource {
  std::string source_file;
  std::string contents;
};

/**
 * Captures one parsed CMake command and its tokenized arguments.
 */
struct CMakeCall {
  std::string command;
  std::vector<std::string> arguments;
  std::string source_file;
};

/**
 * Captures every concrete first-party target and direct project link.
 */
struct CMakeProjectGraph {
  std::set<std::string> targets;
  std::vector<CMakeLinkEdge> links;
  std::vector<std::string> parser_issues;
};

/**
 * Declares the allowed project-target links for one CMake target.
 */
struct CMakeTargetPolicy {
  std::string_view target;
  std::vector<std::string_view> allowed_project_links;
};

/**
 * Keeps one known reverse owner edge visible until its extraction task lands.
 */
struct CMakeLinkException {
  std::string_view target;
  std::string_view dependency;
  std::string_view owner;
  std::string_view removal_task;
  std::string_view reason;
};

const std::vector<SourceModulePolicy> &source_module_policies() {
  // Export, instance flattening, and pivot rebasing share the projection owner's
  // neutral texture-mapping adapter instead of duplicating UV evaluation.
  static const std::vector<SourceModulePolicy> kPolicies = {
      {"editor_array",
       {"src/editor/array/"},
       {"assets", "core", "diagnostics", "editor_array", "editor_commands",
        "editor_history", "editor_instances", "editor_modeling",
        "editor_selection", "math", "scene"}},
      {"editor_commands",
       {"src/editor/commands/"},
       {"assets", "core", "diagnostics", "editor_commands", "editor_history",
        "editor_metadata", "editor_modeling", "editor_selection", "scene"}},
      {"editor_curve",
       {"src/editor/curve/"},
       {"assets", "core", "editor_commands", "editor_curve", "editor_history",
         "editor_interactions", "editor_modeling", "editor_selection", "math",
         "scene", "sdk_modeling", "viewport"}},
      {"editor_history",
       {"src/editor/history/"},
       {"core", "editor_commands", "editor_history", "editor_modeling",
        "scene", "sdk_modeling"}},
      {"editor_instances",
       {"src/editor/instances/"},
       {"assets", "core", "editor_commands", "editor_history",
        "editor_instances", "editor_modeling", "editor_projection", "editor_selection",
        "editor_session", "editor_tools", "io_project", "mesh_polygon",
        "scene"}},
      {"editor_input",
       {"src/editor/input/"},
       {"api", "core", "diagnostics", "editor_input", "viewport"}},
      {"editor_material",
       {"src/editor/material/"},
       {"assets", "core", "editor_commands", "editor_material",
        "editor_modeling", "editor_session", "io_project", "io_qdr",
        "mesh_polygon", "scene"}},
      {"editor_session",
       {"src/editor/session/"},
       {"assets", "core", "diagnostics", "editor_array", "editor_commands",
        "editor_history", "editor_instances", "editor_modeling",
        "editor_pivot", "editor_material",
        "editor_curve",
        "editor_placement", "editor_selection", "editor_session",
        "editor_tools", "io_project", "io_qdr", "scene", "viewport"}},
      {"editor_interactions",
       {"src/editor/interactions/"},
       {"diagnostics", "editor_interactions", "editor_modeling",
        "math", "mesh_geometry", "mesh_polygon"}},
      {"editor_metadata",
       {"src/editor/metadata/"},
       {"api", "assets", "core", "editor_interactions", "editor_metadata",
        "editor_modeling", "editor_tools", "viewport"}},
      {"editor_modeling",
       {"src/editor/modeling/"},
       {"assets", "core", "diagnostics", "editor_commands", "editor_history",
        "editor_interactions", "editor_metadata", "editor_modeling",
        "editor_projection", "editor_selection", "editor_tools",
        "editor_transform", "math", "mesh_geometry", "mesh_polygon", "render",
        "scene", "sdk_modeling", "viewport"}},
      {"editor_pivot",
       {"src/editor/pivot/"},
       {"assets", "core", "editor_history", "editor_modeling",
        "editor_pivot", "editor_projection", "editor_selection", "math", "mesh_polygon", "scene"}},
      {"editor_placement",
       {"src/editor/placement/"},
       {"assets", "core", "diagnostics", "editor_commands",
        "editor_history", "editor_instances", "editor_modeling",
        "editor_pivot", "editor_placement", "editor_session", "math",
        "mesh_geometry", "scene", "viewport"}},
      {"editor_projection",
       {"src/editor/projection/"},
       {"assets", "core", "diagnostics", "editor_interactions",
        "editor_modeling", "editor_projection", "math", "mesh_polygon",
        "render", "scene", "viewport"}},
      {"editor_render",
       {"src/editor/render/"},
       {"api", "assets", "editor_material", "editor_render",
        "editor_session", "render"}},
      {"editor_selection",
       {"src/editor/selection/"},
       {"assets", "core", "diagnostics", "editor_commands", "editor_modeling",
        "editor_selection", "math", "mesh_polygon", "scene"}},
      {"editor_tools",
       {"src/editor/tools/"},
       {"assets", "core", "diagnostics", "editor_array", "editor_curve",
        "editor_interactions", "editor_modeling", "editor_pivot",
        "editor_placement", "editor_selection", "editor_tools", "math",
        "scene", "viewport"}},
      {"editor_transform",
       {"src/editor/transform/"},
       {"api", "assets", "diagnostics", "editor_commands", "editor_history",
        "editor_transform", "math", "scene"}},
      {"editor_viewport",
       {"src/editor/viewport/"},
       {"api", "assets", "core", "diagnostics", "editor_array", "editor_curve",
        "editor_interactions", "editor_modeling", "editor_pivot",
        "editor_placement", "editor_projection", "editor_selection", "editor_tools",
        "editor_viewport", "math", "mesh_polygon", "render", "scene",
        "viewport"}},
      {"mesh_geometry",
       {"src/sdk/core/modeling/neutral/include/quader/modeling/neutral/"
        "geometry/",
        "src/sdk/core/modeling/internal/mesh/geometry/"},
       {"mesh_geometry", "sdk_thirdparty"}},
      {"mesh_polygon",
       {"src/sdk/core/modeling/neutral/include/quader/modeling/neutral/"
        "polygon/",
        "src/sdk/core/modeling/internal/mesh/polygon/"},
       {"diagnostics", "math", "mesh_geometry", "mesh_polygon",
        "sdk_modeling"}},
      {"mesh_io",
       {"src/io/mesh/"},
       {"assets", "core", "diagnostics", "mesh_io"}},
      {"io_qdr",
       {"src/io/qdr/"},
       {"assets", "core", "diagnostics", "io_project", "io_qdr",
        "mesh_polygon", "scene", "sdk_modeling"}},
      {"io_project",
       {"src/io/project/"},
       {"assets", "core", "io_project", "io_qdr"}},
      {"api",
       {"src/api/"},
       {"api",
        "assets",
        "core",
        "diagnostics",
        "editor_array",
        "editor_commands",
        "editor_curve",
        "editor_history",
        "editor_input",
        "editor_instances",
        "editor_material",
        "editor_modeling",
        "editor_placement",
        "editor_pivot",
        "editor_projection",
        "editor_render",
        "editor_selection",
        "editor_session",
        "editor_tools",
        "editor_transform",
        "editor_viewport",
        "io_project",
        "io_qdr",
        "math",
        "mesh_io",
        "mesh_polygon",
        "render",
        "scene",
        "sdk_modeling",
        "viewport"}},
      {"app",
       {"src/app/", "src/main.cpp"},
       {"api", "app", "assets", "core", "diagnostics", "editor_render",
        "editor_material", "editor_session", "platform", "render", "scene",
        "ui"}},
      {"assets",
       {"src/assets/"},
       {"assets", "core", "math", "mesh_polygon", "sdk_modeling"}},
      {"core", {"src/core/"}, {"core"}},
      {"diagnostics", {"src/diagnostics/"}, {"diagnostics"}},
      {"math", {"src/math/"}, {"math"}},
      {"platform", {"src/platform/"}, {"core", "editor_input", "platform"}},
      {"render",
       {"src/render/"},
       {"assets", "core", "diagnostics", "math", "mesh_geometry", "render",
        "scene", "sdk_modeling", "viewport"}},
      {"scene",
       {"src/scene/"},
       {"assets", "core", "diagnostics", "math", "scene", "sdk_modeling"}},
      {"sdk_thirdparty",
       {"src/sdk/core/modeling/internal/thirdparty/"},
       {"sdk_thirdparty"}},
      {"sdk_modeling",
       {"src/sdk/core/modeling/"},
       {"mesh_geometry", "mesh_polygon", "sdk_modeling",
        "sdk_thirdparty"}},
      {"ui",
       {"src/ui/"},
       {"api", "assets", "core", "diagnostics", "editor_array",
        "editor_commands", "editor_curve", "editor_input", "editor_interactions",
        "editor_metadata", "editor_modeling", "editor_pivot", "editor_placement",
        "editor_projection", "editor_render", "editor_tools",
        "editor_transform", "editor_viewport", "math", "platform",
        "render", "scene", "ui", "viewport"}},
      {"viewport",
       {"src/viewport/"},
       {"api", "core", "diagnostics", "editor_commands", "editor_input",
        "editor_interactions", "editor_metadata", "editor_modeling",
        "editor_tools", "math", "render", "sdk_modeling", "viewport"}},
  };
  return kPolicies;
}

const std::vector<IncludeException> &allowed_include_exceptions() {
  static const std::vector<IncludeException> kExceptions = {
      {"src/editor/commands/editor_command_registry.hpp",
       "api/quader_editor_api_types.hpp",
       "command registry still consumes public editor command value types"},
      {"src/editor/session/quader_editor_api_session_access.hpp",
       "api/quader_api_session_internal_access.hpp",
       "host-only adapter rebinds the stable public facade at scene switches"},
      {"src/editor/session/quader_editor_api_session_access.hpp",
       "api/quader_editor_api.hpp",
       "host-only adapter rebinds the stable public facade at scene switches"},
      {"src/io/qdr/qdr_scene_binary_codec.cpp",
       "editor/projection/mesh_projection_service.hpp",
       "binary QDR hydration still rebuilds compiled mesh payloads through "
       "editor projection"},
  };
  return kExceptions;
}

const std::vector<CMakeTargetPolicy> &cmake_target_policies() {
  static const std::vector<CMakeTargetPolicy> kPolicies = {
      {"filament_sdk", {}},
      {"ground_grid_material", {}},
      {"qdr_dump", {"quader_project_qdr_io"}},
      {"qdr_fixture_performance_baseline", {"quader_runtime"}},
      {"qdr_performance_baseline",
       {"quader_assets", "quader_modeling", "quader_modeling_polygon",
        "quader_modeling_profile", "quader_modeling_runtime"}},
      {"qdr_phase8_performance_baseline",
       {"quader_modeling", "quader_modeling_sdk", "quader_render"}},
      {"qdr_scene_workspace_performance", {"quader_runtime"}},
      {"qdr_snap_to_floor_performance", {"quader_runtime"}},
      {"qdr_curve_viewport_performance", {"quader_runtime"}},
      {"qdr_validate", {"quader_project_qdr_io"}},
      {"quader", {"quader_runtime"}},
      {"quader_api", {"quader_core", "quader_modeling_profile"}},
      {"quader_app",
       {"quader_editor_api", "quader_modeling_profile", "quader_platform",
        "quader_render", "quader_ui"}},
      {"quader_assets", {"quader_core_utilities", "quader_modeling_sdk_curves"}},
      {"quader_core",
       {"quader_assets", "quader_core_ui_support", "quader_core_utilities",
        "quader_document", "quader_editor_services", "quader_mesh_io",
        "quader_project_qdr_io", "quader_project_session",
        "quader_viewport_services"}},
      {"quader_core_ui_support", {"quader_core_utilities"}},
      {"quader_core_utilities", {}},
      {"quader_document", {"quader_assets"}},
      {"quader_edit_wire_line_material", {}},
      {"quader_editor_metadata", {"quader_modeling_runtime"}},
      {"quader_editor_api",
       {"quader_api", "quader_modeling_profile", "quader_project_session",
        "quader_render", "quader_viewport_services"}},
      {"quader_editor_services",
       {"quader_document", "quader_modeling", "quader_modeling_profile",
        "quader_viewport_services"}},
      {"quader_editor_viewport",
       {"quader_editor_api", "quader_editor_services",
        "quader_modeling_profile", "quader_render",
        "quader_viewport_services"}},
      {"quader_example_create_box", {"quader_runtime"}},
      {"quader_example_script_modeling_workflow", {"quader_runtime"}},
      {"quader_geometry", {}},
      {"quader_guardrail_tests", {}},
      {"quader_instance_selection_fill_material", {}},
      {"quader_mesh_io", {"quader_assets"}},
      {"quader_mesh_lit_material", {}},
      {"quader_mesh_material", {}},
      {"quader_modeling",
       {"quader_editor_metadata", "quader_modeling_profile",
         "quader_modeling_runtime", "quader_modeling_sdk_runtime"}},
      {"quader_modeling_domain_editor_metadata_test_objects", {}},
      {"quader_modeling_domain_geometry_test_objects", {}},
      {"quader_modeling_domain_metadata_loop_tests",
       {"quader_editor_metadata"}},
      {"quader_modeling_domain_polygon_loop_tests",
       {"quader_geometry", "quader_modeling_polygon"}},
      {"quader_modeling_domain_polygon_test_objects", {}},
      {"quader_modeling_domain_runtime_loop_tests",
       {"quader_editor_metadata", "quader_geometry", "quader_modeling_polygon",
        "quader_modeling_runtime", "quader_modeling_sdk"}},
      {"quader_modeling_domain_runtime_test_objects", {}},
      {"quader_modeling_domain_smoke_test_objects", {}},
      {"quader_modeling_domain_tests",
       {"quader_editor_metadata", "quader_geometry",
        "quader_modeling_polygon", "quader_modeling_runtime"}},
      {"quader_modeling_polygon",
       {"quader_geometry", "quader_modeling_profile"}},
      {"quader_modeling_profile", {}},
      {"quader_modeling_runtime",
       {"quader_geometry", "quader_modeling_polygon",
        "quader_modeling_profile"}},
      {"quader_modeling_sdk",
       {"quader_modeling_sdk_qdr_document", "quader_modeling_sdk_runtime"}},
      {"quader_modeling_sdk_consumer_tests", {"quader_modeling_sdk"}},
      {"quader_modeling_sdk_geometry", {}},
      {"quader_modeling_sdk_parity_tests",
       {"quader_modeling_sdk", "quader_runtime"}},
      {"quader_modeling_sdk_curves", {}},
      {"quader_modeling_sdk_polygon", {"quader_modeling_sdk_geometry"}},
      {"quader_modeling_sdk_qdr_document", {"quader_modeling_sdk_polygon"}},
      {"quader_modeling_sdk_runtime",
       {"quader_modeling_sdk_curves", "quader_modeling_sdk_polygon",
        "quader_modeling_sdk_qdr_document"}},
      {"quader_overlay_icon_material", {}},
      {"quader_overlay_line_material", {}},
      {"quader_overlay_point_material", {}},
      {"quader_fatal_diagnostics", {}},
      {"quader_platform", {"quader_core"}},
      {"quader_project_qdr_io",
       {"quader_assets", "quader_document", "quader_modeling",
        "quader_modeling_profile", "quader_modeling_sdk_qdr_document"}},
      {"quader_project_session",
       {"quader_editor_services", "quader_project_qdr_io"}},
      {"quader_qdr_flatbuffers_generated", {}},
      {"quader_render",
       {"filament_sdk", "quader_document", "quader_modeling_profile",
        "quader_viewport_services"}},
      {"quader_runtime",
       {"quader_app", "quader_platform", "quader_render", "quader_ui"}},
      {"quader_runtime_resources", {}},
      {"quader_sdk_example_mesh_operation", {"quader_modeling_sdk"}},
      {"quader_sdk_example_qdr_roundtrip", {"quader_modeling_sdk"}},
      {"quader_sdk_example_session_preview", {"quader_modeling_sdk"}},
      {"quader_selected_object_line_material", {}},
      {"quader_tests", {"quader_runtime"}},
      {"quader_ui",
       {"quader_editor_api", "quader_editor_metadata", "quader_editor_viewport",
        "quader_modeling_profile", "quader_platform", "quader_render"}},
      {"quader_viewport_services",
       {"quader_core_utilities", "quader_editor_metadata", "quader_modeling",
        "quader_modeling_profile", "quader_modeling_runtime",
        "quader_modeling_sdk"}},
      {"vertex_color_overlay_material", {}},
  };
  return kPolicies;
}

const std::vector<CMakeLinkException> &cmake_link_exceptions() {
  static const std::vector<CMakeLinkException> kExceptions;
  return kExceptions;
}

bool contains_value(const std::vector<std::string_view> &values,
                    std::string_view value) {
  return std::find(values.begin(), values.end(), value) != values.end();
}

std::optional<std::string_view>
module_for_relative_path(std::string_view relative_path) {
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
  const std::string includer_relative = normalized_relative(includer);
  if (starts_with(includer_relative, "src/sdk/core/modeling/")) {
    const std::filesystem::path sdk_neutral_relative =
        source_root() / "src/sdk/core/modeling/neutral/include" / include_file;
    if (std::filesystem::exists(sdk_neutral_relative)) {
      return normalized_relative(sdk_neutral_relative);
    }

    const std::filesystem::path sdk_internal_relative =
        source_root() / "src/sdk/core/modeling/internal" / include_file;
    if (std::filesystem::exists(sdk_internal_relative)) {
      return normalized_relative(sdk_internal_relative);
    }

    const std::filesystem::path sdk_thirdparty_relative =
        source_root() / "src/sdk/core/modeling/internal/thirdparty" /
        include_file;
    if (std::filesystem::exists(sdk_thirdparty_relative)) {
      return normalized_relative(sdk_thirdparty_relative);
    }
  }

  if (starts_with(include_string, "quader/modeling/neutral/")) {
    const std::filesystem::path neutral_relative =
        source_root() / "src/sdk/core/modeling/neutral/include" / include_file;
    if (std::filesystem::exists(neutral_relative)) {
      return normalized_relative(neutral_relative);
    }
  }

  const std::filesystem::path project_relative =
      starts_with(include_string, "src/")
          ? source_root() / include_file
          : source_root() / "src" / include_file;
  if (std::filesystem::exists(project_relative)) {
    return normalized_relative(project_relative);
  }

  const std::filesystem::path local_relative =
      includer.parent_path() / include_file;
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

const IncludeException *
allowed_include_exception(std::string_view includer,
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

bool is_dynamic_cmake_token(std::string_view token) {
  return token.find('$') != std::string_view::npos;
}

bool looks_like_concrete_project_cmake_target(std::string_view token) {
  return !is_dynamic_cmake_token(token) &&
         (starts_with(token, "quader") || starts_with(token, "qdr_") ||
          token == "filament_sdk");
}

bool is_cmake_identifier_character(char character) {
  return std::isalnum(static_cast<unsigned char>(character)) != 0 ||
         character == '_';
}

std::vector<CMakeCall>
parse_cmake_calls(const CMakeSource &source,
                  std::vector<std::string> &parser_issues) {
  std::vector<CMakeCall> calls;
  std::size_t cursor = 0;

  while (cursor < source.contents.size()) {
    if (std::isspace(static_cast<unsigned char>(source.contents[cursor])) !=
        0) {
      ++cursor;
      continue;
    }
    if (source.contents[cursor] == '#') {
      const std::size_t line_end = source.contents.find('\n', cursor);
      cursor = line_end == std::string::npos ? source.contents.size()
                                             : line_end + 1;
      continue;
    }
    if (!is_cmake_identifier_character(source.contents[cursor])) {
      ++cursor;
      continue;
    }

    const std::size_t command_begin = cursor;
    while (cursor < source.contents.size() &&
           is_cmake_identifier_character(source.contents[cursor])) {
      ++cursor;
    }
    const std::string command =
        source.contents.substr(command_begin, cursor - command_begin);
    while (cursor < source.contents.size() &&
           std::isspace(static_cast<unsigned char>(source.contents[cursor])) !=
               0) {
      ++cursor;
    }
    if (cursor >= source.contents.size() || source.contents[cursor] != '(') {
      continue;
    }

    const std::size_t arguments_begin = ++cursor;
    std::size_t depth = 1;
    bool in_quote = false;
    bool in_comment = false;
    for (; cursor < source.contents.size() && depth != 0; ++cursor) {
      const char character = source.contents[cursor];
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
      if (character == '"' &&
          (cursor == 0 || source.contents[cursor - 1] != '\\')) {
        in_quote = !in_quote;
        continue;
      }
      if (in_quote) {
        continue;
      }
      if (character == '(') {
        ++depth;
      } else if (character == ')') {
        --depth;
      }
    }

    if (depth != 0) {
      parser_issues.push_back(source.source_file +
                              ": unterminated CMake call " + command);
      break;
    }

    const std::size_t arguments_end = cursor - 1;
    calls.push_back(
        {command,
         tokenize_cmake_arguments(std::string_view(source.contents)
                                      .substr(arguments_begin,
                                              arguments_end - arguments_begin)),
         source.source_file});
  }

  return calls;
}

bool is_target_declaration_command(std::string_view command) {
  return command == "add_library" || command == "add_executable" ||
         command == "add_custom_target" ||
         command == "quader_add_modeling_domain_gtest_target" ||
         command == "quader_add_modeling_domain_loop_target" ||
         command == "quader_add_modeling_domain_test_object_library" ||
         command == "quader_register_filament_material";
}

bool is_alias_target_declaration(const CMakeCall &call) {
  return call.command == "add_library" &&
         std::find(call.arguments.begin(), call.arguments.end(), "ALIAS") !=
             call.arguments.end();
}

void add_derived_example_targets(CMakeProjectGraph &graph,
                                 const CMakeCall &call,
                                 std::string_view target_prefix,
                                 std::string_view dependency) {
  for (std::size_t index = 1; index < call.arguments.size(); ++index) {
    const std::filesystem::path source_path(call.arguments[index]);
    if (source_path.extension() != ".cpp") {
      continue;
    }
    const std::string target =
        std::string(target_prefix) + source_path.stem().string();
    graph.targets.insert(target);
    graph.links.push_back(
        {target, std::string(dependency), call.source_file});
  }
}

void add_derived_modeling_test_object_targets(CMakeProjectGraph &graph,
                                              const CMakeCall &call) {
  for (std::size_t index = 1; index < call.arguments.size(); ++index) {
    if (is_dynamic_cmake_token(call.arguments[index])) {
      continue;
    }
    graph.targets.insert("quader_modeling_domain_" + call.arguments[index] +
                         "_test_objects");
  }
}

std::string cmake_link_key(std::string_view target,
                           std::string_view dependency) {
  return std::string(target) + " -> " + std::string(dependency);
}

CMakeProjectGraph
parse_cmake_project_graph(const std::vector<CMakeSource> &sources) {
  CMakeProjectGraph graph;
  std::vector<CMakeCall> calls;
  for (const CMakeSource &source : sources) {
    std::vector<CMakeCall> source_calls =
        parse_cmake_calls(source, graph.parser_issues);
    calls.insert(calls.end(), source_calls.begin(), source_calls.end());
  }

  for (const CMakeCall &call : calls) {
    if (is_target_declaration_command(call.command) &&
        !call.arguments.empty() &&
        !is_dynamic_cmake_token(call.arguments.front()) &&
        !is_alias_target_declaration(call)) {
      graph.targets.insert(call.arguments.front());
    }

    if (call.command == "set" && !call.arguments.empty() &&
        call.arguments.front() == "QUADER_SDK_CPP_EXAMPLES") {
      add_derived_example_targets(graph, call, "quader_sdk_example_",
                                  "quader_modeling_sdk");
    } else if (call.command == "set" && !call.arguments.empty() &&
               call.arguments.front() == "QUADER_PUBLIC_CPP_EXAMPLES") {
      add_derived_example_targets(graph, call, "quader_example_",
                                  "quader_runtime");
    } else if (call.command == "set" && !call.arguments.empty() &&
               call.arguments.front() ==
                   "QUADER_MODELING_DOMAIN_TEST_CATEGORIES") {
      add_derived_modeling_test_object_targets(graph, call);
    }

    if (call.command == "quader_add_modeling_domain_gtest_target" &&
        !call.arguments.empty() &&
        !is_dynamic_cmake_token(call.arguments.front())) {
      for (const std::string_view dependency : {
               "quader_geometry", "quader_modeling_polygon",
               "quader_modeling_runtime", "quader_editor_metadata"}) {
        graph.links.push_back({call.arguments.front(), std::string(dependency),
                               call.source_file});
      }
    }
  }

  std::vector<CMakeLinkEdge> raw_links = std::move(graph.links);
  graph.links.clear();
  for (const CMakeCall &call : calls) {
    if (call.command != "target_link_libraries" || call.arguments.empty() ||
        is_dynamic_cmake_token(call.arguments.front())) {
      continue;
    }
    for (std::size_t index = 1; index < call.arguments.size(); ++index) {
      const std::string &dependency = call.arguments[index];
      if (is_cmake_visibility_token(dependency) ||
          is_dynamic_cmake_token(dependency) || dependency == "debug" ||
          dependency == "optimized" || dependency == "general") {
        continue;
      }
      raw_links.push_back(
          {call.arguments.front(), dependency, call.source_file});
    }
  }

  std::set<std::string> unique_links;
  for (const CMakeLinkEdge &edge : raw_links) {
    if (!graph.targets.contains(edge.dependency) &&
        !looks_like_concrete_project_cmake_target(edge.dependency)) {
      continue;
    }
    if (unique_links.insert(cmake_link_key(edge.target, edge.dependency))
            .second) {
      graph.links.push_back(edge);
    }
  }
  return graph;
}

const CMakeTargetPolicy *
cmake_policy_for(const std::vector<CMakeTargetPolicy> &policies,
                 std::string_view target) {
  for (const CMakeTargetPolicy &policy : policies) {
    if (policy.target == target) {
      return &policy;
    }
  }
  return nullptr;
}

const CMakeLinkException *cmake_link_exception_for(
    const std::vector<CMakeLinkException> &exceptions, std::string_view target,
    std::string_view dependency) {
  for (const CMakeLinkException &exception : exceptions) {
    if (exception.target == target && exception.dependency == dependency) {
      return &exception;
    }
  }
  return nullptr;
}

std::vector<std::string> validate_cmake_project_graph(
    const CMakeProjectGraph &graph,
    const std::vector<CMakeTargetPolicy> &policies,
    const std::vector<CMakeLinkException> &exceptions) {
  std::vector<std::string> issues = graph.parser_issues;
  std::set<std::string> policy_targets;
  std::set<std::string> live_links;
  std::set<std::string> used_exceptions;

  for (const CMakeLinkEdge &edge : graph.links) {
    live_links.insert(cmake_link_key(edge.target, edge.dependency));
  }

  for (const CMakeTargetPolicy &policy : policies) {
    if (!policy_targets.insert(std::string(policy.target)).second) {
      issues.push_back("duplicate policy target: " + std::string(policy.target));
    }
    if (!graph.targets.contains(std::string(policy.target))) {
      issues.push_back("stale policy target: " + std::string(policy.target));
    }
    for (std::string_view dependency : policy.allowed_project_links) {
      if (!graph.targets.contains(std::string(dependency))) {
        issues.push_back("policy target " + std::string(policy.target) +
                         " allows unknown project target " +
                         std::string(dependency));
      }
      if (!live_links.contains(cmake_link_key(policy.target, dependency))) {
        issues.push_back("stale allowed project link: " +
                         cmake_link_key(policy.target, dependency));
      }
    }
  }

  for (const std::string &target : graph.targets) {
    if (!policy_targets.contains(target)) {
      issues.push_back("live target missing policy row: " + target);
    }
  }

  std::set<std::string> declared_exceptions;
  for (const CMakeLinkException &exception : exceptions) {
    const std::string key =
        cmake_link_key(exception.target, exception.dependency);
    if (!declared_exceptions.insert(key).second) {
      issues.push_back("duplicate CMake link exception: " + key);
    }
    if (!graph.targets.contains(std::string(exception.target)) ||
        !graph.targets.contains(std::string(exception.dependency))) {
      issues.push_back("CMake link exception names an unknown target: " + key);
    }
    if (exception.owner.empty() || exception.reason.empty() ||
        exception.removal_task.find("P4-T05") == std::string_view::npos ||
        exception.removal_task.find("P5-T01") == std::string_view::npos) {
      issues.push_back("CMake link exception lacks owner/P4-T05/P5-T01 "
                       "removal evidence: " +
                       key);
    }
    const CMakeTargetPolicy *policy =
        cmake_policy_for(policies, exception.target);
    if (policy != nullptr &&
        contains_value(policy->allowed_project_links, exception.dependency)) {
      issues.push_back("CMake link is both allowed and exceptional: " + key);
    }
  }

  for (const CMakeLinkEdge &edge : graph.links) {
    const std::string key = cmake_link_key(edge.target, edge.dependency);
    if (!graph.targets.contains(edge.target)) {
      issues.push_back(edge.source_file + ": unknown project link source " +
                       edge.target);
      continue;
    }
    if (!graph.targets.contains(edge.dependency)) {
      issues.push_back(edge.source_file + ": unknown project dependency " +
                       edge.dependency + " linked by " + edge.target);
      continue;
    }

    const CMakeTargetPolicy *policy = cmake_policy_for(policies, edge.target);
    if (policy != nullptr &&
        contains_value(policy->allowed_project_links, edge.dependency)) {
      continue;
    }
    if (cmake_link_exception_for(exceptions, edge.target, edge.dependency) !=
        nullptr) {
      used_exceptions.insert(key);
      continue;
    }
    issues.push_back(edge.source_file + ": forbidden project link " + key);
  }

  for (const CMakeLinkException &exception : exceptions) {
    const std::string key =
        cmake_link_key(exception.target, exception.dependency);
    if (!used_exceptions.contains(key)) {
      issues.push_back("unused CMake link exception: " + key);
    }
  }
  return issues;
}

CMakeProjectGraph project_cmake_graph() {
  return parse_cmake_project_graph({
      {"CMakeLists.txt", read_source_file("CMakeLists.txt")},
      {"cmake/QuaderModeling.cmake",
       read_source_file("cmake/QuaderModeling.cmake")},
  });
}

bool issue_contains(const std::vector<std::string> &issues,
                    std::string_view needle) {
  return std::any_of(issues.begin(), issues.end(), [needle](const auto &issue) {
    return issue.find(needle) != std::string::npos;
  });
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

    const std::vector<std::string> lines =
        split_lines(read_source_file(includer));
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
        used_exceptions.insert(include_exception_key(exception->includer,
                                                     exception->include_path));
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
     CMakeProjectGraphAndPolicyHaveTotalCoverage) {
  const std::vector<std::string> issues = validate_cmake_project_graph(
      project_cmake_graph(), cmake_target_policies(), cmake_link_exceptions());
  for (const std::string &issue : issues) {
    ADD_FAILURE() << issue;
  }
}

TEST(DependencyMatrixGuardrailParserTests, RejectsStalePolicyRows) {
  const CMakeProjectGraph graph = parse_cmake_project_graph(
      {{"fixture.cmake", "add_library(quader_live STATIC live.cpp)"}});
  const std::vector<std::string> issues = validate_cmake_project_graph(
      graph, {{"quader_live", {}}, {"quader_retired", {}}}, {});

  EXPECT_TRUE(issue_contains(issues, "stale policy target: quader_retired"));
}

TEST(DependencyMatrixGuardrailParserTests, RejectsMissingPolicyRows) {
  const CMakeProjectGraph graph = parse_cmake_project_graph({
      {"fixture.cmake",
       "add_library(quader_listed STATIC listed.cpp)\n"
       "add_library(quader_unlisted STATIC unlisted.cpp)"},
  });
  const std::vector<std::string> issues = validate_cmake_project_graph(
      graph, {{"quader_listed", {}}}, {});

  EXPECT_TRUE(
      issue_contains(issues, "live target missing policy row: quader_unlisted"));
}

TEST(DependencyMatrixGuardrailParserTests, RejectsUnknownProjectEdges) {
  const CMakeProjectGraph graph = parse_cmake_project_graph({
      {"fixture.cmake",
       "add_library(quader_source STATIC source.cpp)\n"
       "target_link_libraries(quader_source PRIVATE quader_unknown)"},
  });
  const std::vector<std::string> issues =
      validate_cmake_project_graph(graph, {{"quader_source", {}}}, {});

  EXPECT_TRUE(issue_contains(issues,
                             "unknown project dependency quader_unknown"));
}

TEST(DependencyMatrixGuardrailParserTests,
     ParsesConditionalTargetsAndEdges) {
  const CMakeProjectGraph graph = parse_cmake_project_graph({
      {"fixture.cmake",
       "add_library(quader_base STATIC base.cpp)\n"
       "if(QUADER_ENABLE_OPTIONAL)\n"
       "  add_library(quader_optional STATIC optional.cpp)\n"
       "  target_link_libraries(quader_optional PRIVATE quader_base)\n"
       "endif()"},
  });
  const std::vector<std::string> issues = validate_cmake_project_graph(
      graph, {{"quader_base", {}}, {"quader_optional", {"quader_base"}}},
      {});

  EXPECT_TRUE(issues.empty());
}

TEST(DependencyMatrixGuardrailParserTests, RejectsForbiddenProjectEdges) {
  const CMakeProjectGraph graph = parse_cmake_project_graph({
      {"fixture.cmake",
       "add_library(quader_source STATIC source.cpp)\n"
       "add_library(quader_dependency STATIC dependency.cpp)\n"
       "target_link_libraries(quader_source PRIVATE quader_dependency)"},
  });
  const std::vector<std::string> issues = validate_cmake_project_graph(
      graph, {{"quader_dependency", {}}, {"quader_source", {}}}, {});

  EXPECT_TRUE(issue_contains(
      issues, "forbidden project link quader_source -> quader_dependency"));
}

TEST(DependencyMatrixGuardrailParserTests, RejectsUnusedCycleExceptions) {
  const CMakeProjectGraph graph = parse_cmake_project_graph({
      {"fixture.cmake",
       "add_library(quader_source STATIC source.cpp)\n"
       "add_library(quader_dependency STATIC dependency.cpp)"},
  });
  const std::vector<std::string> issues = validate_cmake_project_graph(
      graph, {{"quader_dependency", {}}, {"quader_source", {}}},
      {{"quader_source", "quader_dependency", "fixture owner",
        "P4-T05 then P5-T01", "fixture exception"}});

  EXPECT_TRUE(issue_contains(
      issues, "unused CMake link exception: quader_source -> "
              "quader_dependency"));
}
