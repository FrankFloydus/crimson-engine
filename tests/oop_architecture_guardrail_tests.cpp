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

using quader::test::guardrail::contains_any;
using quader::test::guardrail::files_with_extension;
using quader::test::guardrail::lines_containing;
using quader::test::guardrail::normalized_relative;
using quader::test::guardrail::read_file;
using quader::test::guardrail::source_files_under;
using quader::test::guardrail::source_root;
using quader::test::guardrail::source_slice;
using quader::test::guardrail::split_lines;
using quader::test::guardrail::starts_with;

std::string cmake_source_truth() {
  std::string source = read_file(source_root() / "CMakeLists.txt");
  for (const std::filesystem::path &file :
       files_with_extension(source_root() / "cmake", ".cmake")) {
    source.append("\n");
    source.append(read_file(file));
  }
  return source;
}

bool covered_by_allowed_c_make_glob(std::string_view relative,
                                    const std::string &cmake) {
  if (starts_with(relative,
                  "src/sdk/core/modeling/internal/mesh/polygon/operations/")) {
    return cmake.find("mesh/polygon/operations/*.cpp") != std::string::npos;
  }
  if (starts_with(relative, "src/sdk/core/modeling/internal/mesh/polygon/")) {
    return cmake.find("mesh/polygon/*.cpp") != std::string::npos;
  }
  if (starts_with(relative, "src/sdk/core/modeling/internal/mesh/geometry/")) {
    return cmake.find("mesh/geometry/geometry.cpp") != std::string::npos;
  }
  if (starts_with(relative, "src/sdk/core/modeling/internal/io/qdr/")) {
    return cmake.find("io/qdr/" +
                      std::string(std::filesystem::path(relative).filename()
                                      .generic_string())) != std::string::npos;
  }
  return false;
}

bool looks_constructor_only_cpp(const std::string &source) {
  const std::regex constructor_only(
      R"(^\s*#include\s+\"[^\"]+\"\s*(?:namespace\s+\w+\s*\{\s*){0,2}\w+::\w+\([^)]*\)\s*:)",
      std::regex::ECMAScript);
  const std::regex method_definition(
      R"(\w+::\w+\s*\([^)]*\)\s*(?:const\s*)?\{)");
  const auto begin =
      std::sregex_iterator(source.begin(), source.end(), method_definition);
  const auto end = std::sregex_iterator();
  return std::regex_search(source, constructor_only) &&
         std::distance(begin, end) <= 1;
}

std::string trim(std::string value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    }).base();
    if (first >= last) {
        return {};
    }
    return {first, last};
}

std::string first_include_line(const std::string &source) {
  for (const std::string &line : split_lines(source)) {
    const std::string trimmed = trim(line);
    if (starts_with(trimmed, "#include ")) {
      return trimmed;
    }
  }
  return {};
}

std::string include_root_relative(const std::filesystem::path &file) {
  std::string relative = normalized_relative(file);
  constexpr std::string_view kSdkInternalPrefix =
      "src/sdk/core/modeling/internal/";
  if (starts_with(relative, kSdkInternalPrefix)) {
    relative.erase(0, kSdkInternalPrefix.size());
    return relative;
  }

  constexpr std::string_view kSourcePrefix = "src/";
  if (starts_with(relative, kSourcePrefix)) {
    relative.erase(0, kSourcePrefix.size());
  }
  return relative;
}

std::vector<std::string>
matching_header_first_include_forms(const std::filesystem::path &header) {
  const std::string include_path = include_root_relative(header);
  return {
      "#include \"" + include_path + "\"",
      "#include <" + include_path + ">",
      "#include \"" + header.filename().generic_string() + "\"",
  };
}

bool has_matching_header_first_include(const std::filesystem::path &source_file,
                                       const std::filesystem::path &header) {
  const std::string first_include = first_include_line(read_file(source_file));
  const std::vector<std::string> allowed =
      matching_header_first_include_forms(header);
  return std::find(allowed.begin(), allowed.end(), first_include) !=
         allowed.end();
}

bool is_matching_header_first_legacy_exception(std::string_view relative) {
  return relative == "src/editor/modeling/runtime/session.cpp";
}

bool is_sdk_internal_thirdparty_path(std::string_view relative) {
  return starts_with(relative, "src/sdk/core/modeling/internal/thirdparty/");
}

std::vector<std::string> significant_header_lines(const std::string &source) {
  std::vector<std::string> lines;
  bool in_block_comment = false;
  bool at_file_start = true;

  for (const std::string &line : split_lines(source)) {
    std::string trimmed = trim(line);
    if (at_file_start && trimmed.size() >= 3 &&
        static_cast<unsigned char>(trimmed[0]) == 0xEF &&
        static_cast<unsigned char>(trimmed[1]) == 0xBB &&
        static_cast<unsigned char>(trimmed[2]) == 0xBF) {
      trimmed.erase(0, 3);
    }
    at_file_start = false;
    while (!trimmed.empty()) {
      if (in_block_comment) {
        const std::size_t comment_end = trimmed.find("*/");
        if (comment_end == std::string::npos) {
          trimmed.clear();
          break;
        }
        trimmed = trim(trimmed.substr(comment_end + 2));
        in_block_comment = false;
        continue;
      }

      if (starts_with(trimmed, "//")) {
        trimmed.clear();
        break;
      }
      if (starts_with(trimmed, "/*")) {
        const std::size_t comment_end = trimmed.find("*/", 2);
        if (comment_end == std::string::npos) {
          trimmed.clear();
          in_block_comment = true;
          break;
        }
        trimmed = trim(trimmed.substr(comment_end + 2));
        continue;
      }

      lines.push_back(trimmed);
      break;
    }
  }

  return lines;
}

bool has_header_self_containment_boundary(const std::string &source) {
  const std::vector<std::string> lines = significant_header_lines(source);
  if (lines.empty()) {
    return false;
  }
  if (lines.front() == "#pragma once") {
    return true;
  }
  return lines.size() >= 2 && starts_with(lines[0], "#ifndef ") &&
         starts_with(lines[1], "#define ");
}

std::size_t count_occurrences(std::string_view haystack,
                              std::string_view needle) {
  std::size_t count = 0;
  std::size_t position = haystack.find(needle);
  while (position != std::string_view::npos) {
    ++count;
    position = haystack.find(needle, position + needle.size());
  }
  return count;
}

std::string source_text_under(const std::filesystem::path &root) {
  std::string combined;
  for (const std::filesystem::path &file : source_files_under(root)) {
    combined.append("\n// ");
    combined.append(normalized_relative(file));
    combined.append("\n");
    combined.append(read_file(file));
  }
  return combined;
}

bool is_logger_policy_owner(std::string_view relative) {
  return relative == "src/core/logger.cpp" ||
         relative == "src/core/logger.hpp";
}

bool is_fatal_policy_owner_or_startup(std::string_view relative) {
  return relative == "src/platform/fatal_error_handler.cpp" ||
         relative == "src/platform/fatal_error_handler.hpp" ||
         relative == "src/main.cpp";
}

bool is_console_logger_memory_access(std::string_view relative,
                                     const std::string &line) {
  return relative == "src/ui/console_panel.cpp" &&
         (line.find("Logger::clear_memory()") != std::string::npos ||
          line.find("Logger::memory_revision()") != std::string::npos ||
          line.find("Logger::entries_snapshot()") != std::string::npos ||
          line.find("Logger::entries_since(") != std::string::npos);
}

bool has_nearby_declaration_doc(const std::vector<std::string> &file_lines,
                                std::size_t declaration_line) {
  if (declaration_line == 0) {
    return false;
  }

  std::size_t cursor = declaration_line;
  while (cursor > 0) {
    --cursor;
    const std::string previous = trim(file_lines[cursor]);
    if (previous.empty()) {
      continue;
    }
    return previous == "*/" || previous.rfind("///", 0) == 0 ||
           previous.rfind("//!", 0) == 0 ||
           previous.find("@brief") != std::string::npos;
  }

  return false;
}

} // namespace

TEST(OopArchitectureGuardrailTests, TypeDeclarationsHaveMeaningfulDocumentation) {
    const std::regex declaration(R"(^\s*(class|struct|enum(?:\s+class)?)\s+([A-Za-z_][A-Za-z0-9_]*)\b)");

    for (const std::filesystem::path &root :
         {source_root() / "src", source_root() / "tests",
          source_root() / "benchmarks"}) {
      for (const std::string_view extension :
           {std::string_view{".hpp"}, std::string_view{".cpp"}}) {
        for (const std::filesystem::path &file :
             files_with_extension(root, extension)) {
          const std::string source = read_file(file);
          const std::vector<std::string> file_lines = split_lines(source);
          const std::string relative = normalized_relative(file);
          if (is_sdk_internal_thirdparty_path(relative)) {
            continue;
          }

          for (std::size_t index = 0; index < file_lines.size(); ++index) {
            const std::string &line = file_lines[index];
            if (!std::regex_search(line, declaration) ||
                line.find(';') != std::string::npos) {
              continue;
            }

            EXPECT_TRUE(has_nearby_declaration_doc(file_lines, index))
                << relative << ":" << (index + 1) << ": " << line;
          }
        }
      }
    }
}

TEST(OopArchitectureGuardrailTests, SourceCppFilesAreNotEmptyOrConstructorOnly) {
  for (const std::filesystem::path &file :
       files_with_extension(source_root() / "src", ".cpp")) {
    const std::string source = read_file(file);
    const std::string relative = normalized_relative(file);

    EXPECT_FALSE(source.empty()) << relative;
    EXPECT_FALSE(looks_constructor_only_cpp(source)) << relative;
  }
}

TEST(OopArchitectureGuardrailTests, SourceFilesWithMatchingHeadersIncludeThemFirst) {
  for (const std::filesystem::path &file :
       files_with_extension(source_root() / "src", ".cpp")) {
    const std::filesystem::path header =
        file.parent_path() / (file.stem().string() + ".hpp");
    if (!std::filesystem::exists(header)) {
      continue;
    }

    const std::string relative = normalized_relative(file);
    if (is_sdk_internal_thirdparty_path(relative)) {
      continue;
    }
    if (is_matching_header_first_legacy_exception(relative)) {
      EXPECT_EQ(first_include_line(read_file(file)),
                "#include \"session_internal.hpp\"")
          << relative;
      continue;
    }

    EXPECT_TRUE(has_matching_header_first_include(file, header))
        << relative << " first include was: "
        << first_include_line(read_file(file));
  }
}

TEST(OopArchitectureGuardrailTests, ProjectHeadersDeclareSelfContainmentBoundary) {
  for (const std::string_view extension :
       {std::string_view{".hpp"}, std::string_view{".h"}}) {
    for (const std::filesystem::path &file :
         files_with_extension(source_root() / "src", extension)) {
      EXPECT_TRUE(has_header_self_containment_boundary(read_file(file)))
          << normalized_relative(file);
    }
  }
}

TEST(OopArchitectureGuardrailTests, EverySourceCppIsPartOfCMakeSourceTruth) {
  const std::string cmake = cmake_source_truth();

  for (const std::filesystem::path &file :
       files_with_extension(source_root() / "src", ".cpp")) {
    const std::string relative = normalized_relative(file);
    if (is_sdk_internal_thirdparty_path(relative)) {
      continue;
    }
    EXPECT_TRUE(cmake.find(relative) != std::string::npos ||
                covered_by_allowed_c_make_glob(relative, cmake))
        << relative;
  }
}

TEST(OopArchitectureGuardrailTests, UserFacingToolsAreNotPassThroughAliases) {
  for (const std::filesystem::path &file : files_with_extension(
           source_root() / "src/editor/commands/modeling", ".hpp")) {
    const std::string source = read_file(file);
    const std::string relative = normalized_relative(file);
    if (relative.find("_tool.hpp") == std::string::npos ||
        (relative.find("/commands/") == std::string::npos &&
         relative.find("/context/") == std::string::npos &&
         relative.find("/preview/") == std::string::npos)) {
      continue;
    }

    EXPECT_EQ(source.find("using "), std::string::npos) << relative;
    EXPECT_EQ(source.find("= default;"), std::string::npos) << relative;
    EXPECT_EQ(source.find("SdkOperationTool"), std::string::npos) << relative;
  }
}

TEST(OopArchitectureGuardrailTests, ToolCatalogHeadersAreDeclarationsOnly) {
  for (const std::filesystem::path &file : {
           source_root() /
               "src/editor/commands/modeling_command_tool_catalog.hpp",
           source_root() / "src/editor/tools/viewport_tool_catalog.hpp",
           source_root() /
               "src/editor/tools/viewport_tool_descriptor_catalog.hpp",
       }) {
    const std::string source = read_file(file);
    const std::string relative = normalized_relative(file);
    EXPECT_EQ(source.find("static const"), std::string::npos) << relative;
    EXPECT_EQ(source.find("inline std::span"), std::string::npos) << relative;
    EXPECT_EQ(source.find("std::array"), std::string::npos) << relative;
  }
}

TEST(OopArchitectureGuardrailTests, ModelingInternalDomainHeadersAvoidFacadeDependency) {
  for (const std::filesystem::path &root : {
           source_root() / "src/editor/modeling/runtime",
           source_root() / "src/editor/modeling/picking",
           source_root() / "src/editor/modeling/preview",
           source_root() / "src/editor/modeling/runtime_state",
       }) {
    for (const std::filesystem::path &file :
         files_with_extension(root, ".hpp")) {
      const std::string source = read_file(file);
      const std::string relative = normalized_relative(file);
      EXPECT_EQ(
          source.find("editor/modeling/services/modeling_session_service.hpp"),
          std::string::npos)
          << relative;
    }
  }
}

TEST(OopArchitectureGuardrailTests, SceneDocumentDoesNotExposeRawRegistryAccess) {
  const std::string header =
      read_file(source_root() / "src/scene/scene_document.hpp");

  EXPECT_EQ(header.find("registry& registry("), std::string::npos);
  EXPECT_EQ(header.find("entt::registry& registry("), std::string::npos);
}

TEST(OopArchitectureGuardrailTests, UiDoesNotBypassCommandAndPreviewBoundaries) {
  for (const std::filesystem::path &root :
       {source_root() / "src/ui", source_root() / "src/app",
        source_root() / "src/viewport"}) {
    for (const std::filesystem::path &file :
         files_with_extension(root, ".cpp")) {
      const std::string source = read_file(file);
      const std::string relative = normalized_relative(file);
      EXPECT_FALSE(contains_any(source,
                                {
                                    "execute_instant_operation",
                                    "execute_context_action",
                                    "commit_active_operation_preview",
                                    "cancel_active_operation_preview",
                                    "executeExternal",
                                }))
          << relative;
    }
  }
}

TEST(OopArchitectureGuardrailTests,
     DiagnosticsGlobalsStayInCoreAndPlatformOwners) {
  const std::string root_agents = read_file(source_root() / "AGENTS.md");
  const std::string logger_header =
      read_file(source_root() / "src/core/logger.hpp");
  const std::string logger_source =
      read_file(source_root() / "src/core/logger.cpp");
  const std::string fatal_source =
      read_file(source_root() / "src/platform/fatal_error_handler.cpp");
  const std::string main_source = read_file(source_root() / "src/main.cpp");

  EXPECT_NE(root_agents.find("## Diagnostics And Fatal Errors"),
            std::string::npos);
  EXPECT_NE(root_agents.find("Logger"),
            std::string::npos);
  EXPECT_NE(root_agents.find("src/platform/fatal_error_handler.*"),
            std::string::npos);
  EXPECT_NE(root_agents.find("explicit shared-core diagnostics exceptions"),
            std::string::npos);
  EXPECT_NE(root_agents.find("Fatal-error handling is platform-owned"),
            std::string::npos);

  EXPECT_NE(logger_header.find("class Logger"), std::string::npos);
  EXPECT_NE(logger_header.find("#define LOG_ERROR"), std::string::npos);
  EXPECT_NE(logger_source.find("std::shared_ptr<spdlog::logger> g_logger"),
            std::string::npos);
  EXPECT_NE(logger_source.find("spdlog::set_default_logger(g_logger)"),
            std::string::npos);
  EXPECT_NE(fatal_source.find("std::set_terminate(terminate_handler)"),
            std::string::npos);
  EXPECT_NE(fatal_source.find("SetUnhandledExceptionFilter("),
            std::string::npos);
  EXPECT_NE(fatal_source.find("MessageBoxW("), std::string::npos);
  EXPECT_NE(main_source.find("Logger::initialize("), std::string::npos);
  EXPECT_NE(main_source.find("install_fatal_error_handler(log_path)"),
            std::string::npos);

  const std::vector<std::string_view> logger_global_configuration = {
      "spdlog::set_default_logger",
      "spdlog::drop_all(",
      "std::shared_ptr<spdlog::logger> g_",
      "static std::shared_ptr<spdlog::logger>",
      "std::unique_ptr<spdlog::logger> g_",
      "static std::unique_ptr<spdlog::logger>",
  };
  const std::vector<std::string_view> fatal_runtime_hooks = {
      "std::set_terminate(",
      "std::signal(SIGABRT",
      "SetUnhandledExceptionFilter(",
      "_set_invalid_parameter_handler(",
      "_set_abort_behavior(",
      "MessageBoxW(",
      "MessageBoxA(",
      "SDL_ShowSimpleMessageBox(",
      "MiniDumpWriteDump(",
      "install_fatal_error_handler(",
      "report_fatal_error_and_exit(",
  };
  const std::vector<std::string_view> ui_app_diagnostic_globals = {
      "Logger::",
      "std::shared_ptr<spdlog::logger>",
      "std::unique_ptr<spdlog::logger>",
      "spdlog::logger g_",
      "g_logger",
      "g_log_sink",
      "g_diagnostic",
      "g_diagnostics",
      "g_crash",
      "g_fatal",
  };

  for (const std::filesystem::path &file :
       source_files_under(source_root() / "src")) {
    const std::string source = read_file(file);
    const std::string relative = normalized_relative(file);

    for (const std::string &line : split_lines(source)) {
      if (contains_any(line, logger_global_configuration)) {
        EXPECT_TRUE(is_logger_policy_owner(relative))
            << relative << ": " << line;
      }
      if (contains_any(line, fatal_runtime_hooks)) {
        EXPECT_TRUE(is_fatal_policy_owner_or_startup(relative))
            << relative << ": " << line;
      }
    }
  }

  for (const std::filesystem::path &root :
       {source_root() / "src/ui", source_root() / "src/app"}) {
    for (const std::filesystem::path &file : source_files_under(root)) {
      const std::string source = read_file(file);
      const std::string relative = normalized_relative(file);

      for (const std::string &line : split_lines(source)) {
        if (is_console_logger_memory_access(relative, line)) {
          continue;
        }
        EXPECT_FALSE(contains_any(line, ui_app_diagnostic_globals))
            << relative << ": " << line;
      }
    }
  }
}

TEST(OopArchitectureGuardrailTests, UiShellDoesNotOwnToolImplementations) {
  const std::string source = read_file(source_root() / "src/ui/ui_system.cpp");

  for (std::string_view forbidden : {
           "selectFromViewport",
           "drawTransformGizmo",
           "GizmoAxis",
           "GizmoStartTransform",
           "boxPreview_",
           "insertEdgeLoopDragActive_",
           "cutDragActive_",
           "mirrorDragActive_",
           "begin_insert_edge_loop_drag",
           "update_knife_hover",
           "click_cut",
           "click_mirror",
           "accept_poly_point",
           "update_box_hover",
           "begin_transform_drag",
           "commit_transform_drag",
       }) {
    EXPECT_EQ(source.find(forbidden), std::string::npos) << forbidden;
  }
}

TEST(OopArchitectureGuardrailTests,
     UIContextSplitsIntentPresentationAndViewportSurfaces) {
  const std::string header = read_file(source_root() / "src/ui/ui_context.hpp");
  const std::string context =
      source_slice(header, "struct UIContext {", "};");
  const std::string presentation =
      source_slice(header, "struct UIScenePresentationContext {", "};");

  EXPECT_NE(header.find("struct UIIntentContext"), std::string::npos);
  EXPECT_NE(header.find("struct UIScenePresentationContext"),
            std::string::npos);
  EXPECT_NE(header.find("struct UIInspectorPreviewContext"),
            std::string::npos);
  EXPECT_NE(header.find("struct UIPreferencesContext"), std::string::npos);
  EXPECT_EQ(header.find("struct UIViewportSurfaceContext"), std::string::npos);
  EXPECT_NE(context.find("UIIntentContext intent;"), std::string::npos);
  EXPECT_NE(context.find("UIScenePresentationContext scene;"),
            std::string::npos);
  EXPECT_NE(context.find("UIInspectorPreviewContext *inspector_preview"),
            std::string::npos);
  EXPECT_NE(context.find("UIPreferencesContext preferences;"),
            std::string::npos);
  EXPECT_EQ(context.find("viewport"), std::string::npos);

  EXPECT_NE(presentation.find("const SceneDocument &document"),
            std::string::npos);
  EXPECT_NE(presentation.find("const AssetRegistry &assets"),
            std::string::npos);
  EXPECT_NE(presentation.find("const ModelingSessionService &modeling"),
            std::string::npos);
  EXPECT_NE(presentation.find("const EditorSelectionService &selection"),
            std::string::npos);

  for (std::string_view forbidden : {
           "QuaderEditorApi &editor",
           "SceneRenderBridge &render_bridge",
           "SceneDocument &document",
           "AssetRegistry &assets",
           "ModelingSessionService &modeling",
           "EditorSelectionService &selection",
           "ViewportSettings &viewport_settings",
           "ViewportWorkspaceService",
           "QtViewportPanel",
           "QtViewportSurface",
           "RenderSystem",
           "FrameTiming frame_timing",
           "RenderFramePerformanceStats render_performance",
       }) {
    EXPECT_EQ(context.find(forbidden), std::string::npos) << forbidden;
  }

  for (const std::filesystem::path &file :
       source_files_under(source_root() / "src/ui")) {
    const std::string relative = normalized_relative(file);
    const std::string source = read_file(file);
    for (const std::string &line :
         lines_containing(source, {"context.viewport->", "context.viewport ==",
                                   "*context.viewport",
                                   "ui_context.viewport->"})) {
      EXPECT_TRUE(relative == "src/ui/qt_viewport_panel.cpp")
          << relative << ": " << line;
    }
    for (const std::string &line :
         lines_containing(source, {"context.inspector_preview"})) {
      EXPECT_TRUE(relative == "src/ui/inspector_panel.cpp")
          << relative << ": " << line;
    }
  }
}

TEST(OopArchitectureGuardrailTests, InteractionToolMathStaysInSharedOwner) {
  const std::string owner = read_file(
      source_root() / "src/editor/interactions/tool_math_internal.hpp");
  const std::string extrude =
      read_file(source_root() / "src/editor/interactions/extrude_tool.cpp");
  const std::string knife =
      read_file(source_root() / "src/editor/interactions/knife_tool.cpp");
  const std::string snapping =
      read_file(source_root() / "src/editor/interactions/tool_snapping.cpp");

  EXPECT_NE(owner.find("tool_geometry_vec2"), std::string::npos);
  EXPECT_NE(owner.find("tool_geometry_vec3"), std::string::npos);
  EXPECT_NE(owner.find("tool_poly_vec2"), std::string::npos);
  EXPECT_NE(owner.find("tool_poly_vec3"), std::string::npos);
  EXPECT_NE(owner.find("vec3_normalize_or"), std::string::npos);
  EXPECT_NE(extrude.find("tool_math_internal.hpp"), std::string::npos);
  EXPECT_NE(extrude.find("normalized_axis("), std::string::npos);
  EXPECT_NE(extrude.find("safe_grid_size("), std::string::npos);
  EXPECT_NE(knife.find("vec3_normalize_or"), std::string::npos);
  EXPECT_EQ(knife.find("quader/modeling/neutral/geometry/geometry.hpp"),
            std::string::npos);
  EXPECT_EQ(knife.find("tool_geometry_vec2"), std::string::npos);
  EXPECT_EQ(knife.find("tool_geometry_vec3"), std::string::npos);
  EXPECT_NE(snapping.find("vec3_normalize_or"), std::string::npos);

  for (const std::string &source : {extrude, knife, snapping}) {
    EXPECT_EQ(source.find("to_geometry_vec2"), std::string::npos);
    EXPECT_EQ(source.find("to_geometry_vec3"), std::string::npos);
    EXPECT_EQ(source.find("to_poly_vec2"), std::string::npos);
    EXPECT_EQ(source.find("to_poly_vec3"), std::string::npos);
    EXPECT_EQ(source.find("QVec3 normalize_or("), std::string::npos);
    EXPECT_EQ(source.find("auto normalize_or"), std::string::npos);
    EXPECT_EQ(source.find("QVec3 normalized_axis("), std::string::npos);
    EXPECT_EQ(source.find("double safe_grid_size("), std::string::npos);
    EXPECT_EQ(source.find("double vec2_distance_squared("), std::string::npos);
  }
}

TEST(OopArchitectureGuardrailTests, UiShellLeavesSceneOverlayExtractionToRenderBuilder) {
  const std::string ui_system =
      read_file(source_root() / "src/ui/ui_system.cpp");
  const std::string viewport_panel =
      read_file(source_root() / "src/ui/qt_viewport_panel.cpp");
  const std::string ui_shell = ui_system + viewport_panel;
  const std::string builder = read_file(
      source_root() / "src/render/overlay/viewport_scene_overlay_builder.cpp");
  const std::string render_bridge =
      read_file(source_root() / "src/render/scene_render_bridge.cpp");

  EXPECT_NE(builder.find("append_hierarchy_selection_wires"), std::string::npos);
  EXPECT_NE(builder.find("append_group_selection_cages"), std::string::npos);
  EXPECT_NE(builder.find("append_point_light_gizmos"), std::string::npos);
  EXPECT_NE(builder.find("append_scene_wireframe_overlay"), std::string::npos);
  EXPECT_NE(builder.find("strip_wireframe_object_face_fills"), std::string::npos);
  EXPECT_NE(render_bridge.find(
                "ViewportSceneOverlayBuilder::build_selection_frame("),
            std::string::npos);
  EXPECT_NE(render_bridge.find(
                "ViewportSceneOverlayBuilder::strip_wireframe_selection_fills("),
            std::string::npos);
  EXPECT_NE(viewport_panel.find(
                "QuaderRenderApiRuntimeAccess::sync_scene_asset_overlays("),
            std::string::npos);
  EXPECT_NE(viewport_panel.find(
                "QuaderRenderApiRuntimeAccess::build_scene_overlay_frame("),
            std::string::npos);
  EXPECT_EQ(viewport_panel.find("build_render_overlay_"), std::string::npos);
  EXPECT_EQ(viewport_panel.find(
                "#include \"render/overlay/viewport_scene_overlay_builder.hpp\""),
            std::string::npos);
  EXPECT_EQ(viewport_panel.find("ViewportSceneOverlayBuilder::"),
            std::string::npos);
  EXPECT_EQ(viewport_panel.find("render_bridge.sync_selected_object_overlay("),
            std::string::npos);
  EXPECT_EQ(viewport_panel.find("render_bridge.sync_scene_wireframe_overlay("),
            std::string::npos);

  for (std::string_view forbidden : {
           "append_mesh_selection_wire",
           "append_hierarchy_selection_wires",
           "append_group_selection_cages",
           "append_scene_wireframe_overlay",
           "strip_wireframe_object_face_fills",
           "build_wireframe_frame(",
           "WireEdgeKey",
       }) {
    EXPECT_EQ(ui_shell.find(forbidden), std::string::npos) << forbidden;
  }
}

TEST(OopArchitectureGuardrailTests, ViewportRuntimeAndResetUseDeclaredToolOwners) {
  const std::string runtime_header =
      read_file(source_root() / "src/editor/tools/viewport_tool_runtime.hpp");
  const std::string runtime_source =
      read_file(source_root() / "src/editor/tools/viewport_tool_runtime.cpp");
  const std::string interaction_header =
      read_file(source_root() /
                "src/editor/viewport/viewport_interaction_controller.hpp");
  const std::string interaction_source =
      read_file(source_root() /
                "src/editor/viewport/viewport_interaction_controller.cpp");
  const std::string gizmo_header = read_file(
      source_root() / "src/editor/viewport/transform_gizmo_interaction.hpp");
  const std::string gizmo_source = read_file(
      source_root() / "src/editor/viewport/transform_gizmo_interaction.cpp");

  EXPECT_EQ(runtime_header.find("modal_viewport_tool.hpp"), std::string::npos);
  EXPECT_EQ(runtime_header.find("transform_viewport_tool.hpp"),
            std::string::npos);
  EXPECT_EQ(runtime_source.find("dynamic_cast"), std::string::npos);
  EXPECT_NE(runtime_source.find("tool.kind() == ToolKind::Modal"),
            std::string::npos);
  EXPECT_NE(runtime_source.find("tool.kind() == ToolKind::Transform"),
            std::string::npos);

  EXPECT_NE(interaction_header.find("reset(QuaderEditorApi *editor = nullptr)"),
            std::string::npos);
  EXPECT_NE(interaction_source.find("transform_gizmo_.reset(editor);"),
            std::string::npos);
  EXPECT_NE(interaction_source.find("selection_.reset();"), std::string::npos);
  EXPECT_NE(gizmo_header.find("reset(QuaderEditorApi *editor = nullptr)"),
            std::string::npos);
  EXPECT_NE(gizmo_source.find("cancel_active_drag(*editor)"), std::string::npos);
  EXPECT_NE(gizmo_source.find("clear_frame();"), std::string::npos);
  EXPECT_NE(gizmo_source.find("current_frame_.reset();"), std::string::npos);
}

TEST(OopArchitectureGuardrailTests, ViewportMaximizedStateBelongsToWorkspaceService) {
  const std::string input_header =
      read_file(source_root() / "src/viewport/viewport_input_types.hpp");
  const std::string api_input_header =
      read_file(source_root() / "src/api/quader_viewport_input_types.hpp");
  const std::string input_source =
      read_file(source_root() / "src/viewport/viewport_input_router.cpp");
  const std::string runtime_source =
      read_file(source_root() / "src/editor/tools/viewport_tool_runtime.cpp");
  const std::string workspace_header =
      read_file(source_root() / "src/viewport/viewport_workspace_service.hpp");
  const std::string workspace_source =
      read_file(source_root() / "src/viewport/viewport_workspace_service.cpp");

  const std::size_t state_start =
      api_input_header.find("struct ViewportToolState");
  ASSERT_NE(state_start, std::string::npos);
  const std::size_t state_end = api_input_header.find("};", state_start);
  ASSERT_NE(state_end, std::string::npos);
  const std::string state_body =
      api_input_header.substr(state_start, state_end - state_start);

  EXPECT_EQ(state_body.find("maximized"), std::string::npos);
  EXPECT_EQ(input_header.find("setMaximized"), std::string::npos);
  EXPECT_EQ(input_source.find("setMaximized"), std::string::npos);
  EXPECT_EQ(runtime_source.find("state.maximized"), std::string::npos);
  EXPECT_EQ(workspace_header.find("active_pane_placements(bool"),
            std::string::npos);
  EXPECT_EQ(workspace_header.find("active_split_handles(bool"),
            std::string::npos);
  EXPECT_EQ(workspace_source.find("active_pane_placements(bool"),
            std::string::npos);
  EXPECT_EQ(workspace_source.find("active_split_handles(bool"),
            std::string::npos);
  EXPECT_NE(workspace_header.find("focused_pane_maximized"), std::string::npos);
  EXPECT_NE(workspace_source.find("focused_pane_maximized_"), std::string::npos);
  EXPECT_NE(runtime_source.find("result.maximized_toggle_requested = true"),
            std::string::npos);
}

TEST(OopArchitectureGuardrailTests, OverlayGeometryAndPixelPolicyStayShared) {
  const std::string policy =
      read_file(source_root() / "src/render/viewport_overlay_policy.hpp");
  const std::string geometry = read_file(
      source_root() / "src/render/overlay/viewport_overlay_geometry.hpp");
  const std::string box = read_file(
      source_root() / "src/render/overlay/viewport_box_overlay_builder.cpp");
  const std::string marquee = read_file(
      source_root() /
      "src/render/overlay/viewport_selection_marquee_overlay_builder.cpp");
  const std::string gizmo = read_file(
      source_root() /
      "src/render/overlay/viewport_transform_gizmo_overlay_builder.cpp");
  const std::string interaction = read_file(
      source_root() / "src/editor/viewport/transform_gizmo_interaction.cpp");
  const std::string scene = read_file(
      source_root() / "src/render/overlay/viewport_scene_overlay_builder.cpp");
  const std::string drawn_plane =
      read_file(source_root() /
                "src/editor/projection/drawn_plane_overlay_projection.cpp");

  for (std::string_view constant : {
           std::string_view{"kOverlayToolBoxPreviewLineWidthPixels"},
           std::string_view{"kOverlaySelectionMarqueeLineWidthPixels"},
           std::string_view{"kOverlayDrawnPlanePointSizePixels"},
           std::string_view{"kOverlayDrawnPlaneLineWidthPixels"},
           std::string_view{"kOverlayLightMarkerPointSizePixels"},
           std::string_view{"kOverlayLightMarkerOutlineGrowthPixels"},
           std::string_view{"kOverlayPointLightGizmoSizePixels"},
           std::string_view{"kOverlayTransformGizmoSizePixels"},
           std::string_view{"kOverlayTransformGizmoPickRadiusPixels"},
           std::string_view{"kOverlayTransformGizmoMoveCenterRadiusPixels"},
           std::string_view{"kOverlayTransformGizmoScaleHandleSizePixels"},
           std::string_view{"kOverlayTransformGizmoScaleCenterSizePixels"},
           std::string_view{"kOverlayTransformGizmoAxisLineWidthPixels"},
       }) {
    EXPECT_NE(policy.find(constant), std::string::npos) << constant;
  }

    EXPECT_NE(geometry.find("viewport_overlay_normalize_or"), std::string::npos);
    EXPECT_NE(geometry.find("viewport_overlay_fallback_perpendicular"), std::string::npos);
    EXPECT_NE(box.find("viewport_overlay_normalize_or"), std::string::npos);
    EXPECT_NE(marquee.find("viewport_overlay_normalize_or"), std::string::npos);
    EXPECT_NE(gizmo.find("viewport_overlay_normalize_or"), std::string::npos);
    EXPECT_NE(gizmo.find("viewport_overlay_fallback_perpendicular"), std::string::npos);
    EXPECT_NE(interaction.find("kOverlayTransformGizmoSizePixels"), std::string::npos);
    EXPECT_NE(interaction.find("kOverlayTransformGizmoPickRadiusPixels"), std::string::npos);
    EXPECT_NE(scene.find("kOverlayLightMarkerPointSizePixels"), std::string::npos);
    EXPECT_NE(scene.find("kOverlayLightMarkerOutlineGrowthPixels"), std::string::npos);
    EXPECT_NE(scene.find("kOverlayPointLightGizmoSizePixels"), std::string::npos);
    EXPECT_NE(drawn_plane.find("kOverlayDrawnPlanePointSizePixels"),
              std::string::npos);
    EXPECT_NE(drawn_plane.find("kOverlayDrawnPlaneLineWidthPixels"),
              std::string::npos);

    for (const std::string& source : {box, marquee, gizmo}) {
        EXPECT_EQ(source.find("normalizedOr("), std::string::npos);
        EXPECT_EQ(source.find("normalized_or("), std::string::npos);
        EXPECT_EQ(source.find("safeNormalize("), std::string::npos);
        EXPECT_EQ(source.find("safe_normalize("), std::string::npos);
        EXPECT_EQ(source.find("fallbackPerpendicular("), std::string::npos);
        EXPECT_EQ(source.find(" fallback_perpendicular("), std::string::npos);
    }
}

TEST(OopArchitectureGuardrailTests, SceneEnvironmentCatalogOwnsIblResourceContract) {
  const std::string catalog =
      read_file(source_root() / "src/core/scene_environment_catalog.hpp");
  const std::string scene_components =
      read_file(source_root() / "src/scene/components.hpp");
  const std::string api_types =
      read_file(source_root() / "src/api/quader_api_types.hpp");
  const std::string inspector =
      read_file(source_root() / "src/ui/inspector_panel.cpp");
  const std::string environment_library =
      read_file(source_root() / "src/render/filament_environment_library.cpp");

  EXPECT_FALSE(std::filesystem::exists(
      source_root() / "src/scene/scene_environment_catalog.hpp"));
  EXPECT_NE(catalog.find("kDefaultSceneEnvironmentId"), std::string::npos);
  EXPECT_NE(catalog.find("scene_environment_catalog()"), std::string::npos);
  EXPECT_NE(catalog.find("scene_environment_descriptor("), std::string::npos);
  EXPECT_NE(scene_components.find("core/scene_environment_catalog.hpp"),
            std::string::npos);
  EXPECT_NE(api_types.find("core/scene_environment_catalog.hpp"),
            std::string::npos);
  EXPECT_NE(inspector.find("scene_environment_catalog()"), std::string::npos);
  EXPECT_NE(
      environment_library.find("scene_environment_descriptor(environment_id)"),
      std::string::npos);
  EXPECT_EQ(api_types.find("scene/scene_environment_catalog.hpp"),
            std::string::npos);
}

TEST(OopArchitectureGuardrailTests, RuntimeMaterialResourceContractsUseCatalogs) {
  const std::string material_catalog =
      read_file(source_root() / "src/render/filament_material_catalog.hpp");
  const std::string material_library =
      read_file(source_root() / "src/render/filament_material_library.cpp");
  const std::string default_catalog =
      read_file(source_root() / "src/assets/default_material_catalog.hpp");
  const std::string app = read_file(source_root() / "src/app/app.cpp");
  const std::string bootstrap = read_file(
      source_root() / "src/editor/session/project_asset_bootstrap_service.cpp");
  const std::string host =
      read_file(source_root() / "src/editor/metadata/host_contract.cpp");

  EXPECT_NE(material_catalog.find("kFilamentMaterialDirectory"),
            std::string::npos);
  EXPECT_NE(material_catalog.find("kQuaderMeshMaterialFile"),
            std::string::npos);
  EXPECT_NE(material_catalog.find("kOverlayIconMaterialFile"),
            std::string::npos);
  EXPECT_NE(material_catalog.find("kGroundGridMaterialFile"),
            std::string::npos);
  EXPECT_EQ(material_catalog.find("kImguiMaterialFile"), std::string::npos);
  EXPECT_NE(material_library.find("filament_material_catalog.hpp"),
            std::string::npos);
  EXPECT_NE(material_library.find("std::string(kFilamentMaterialDirectory)"),
            std::string::npos);
  EXPECT_NE(material_library.find("std::string(kOverlayIconMaterialFile)"),
            std::string::npos);
  EXPECT_EQ(material_library.find("\"materials\" / \"quader_mesh.filamat\""),
            std::string::npos);
  EXPECT_EQ(material_library.find("\"materials\" / \"imgui.filamat\""),
            std::string::npos);
  EXPECT_EQ(material_library.find("imgui.filamat"), std::string::npos);

  EXPECT_NE(default_catalog.find("kDefaultMaterialAlbedoTexturePackagePath"),
            std::string::npos);
  EXPECT_NE(default_catalog.find("kDefaultMaterialRoughnessTexturePackagePath"),
            std::string::npos);
  EXPECT_NE(app.find("ProjectAssetBootstrapService"), std::string::npos);
  EXPECT_NE(bootstrap.find("default_material_catalog.hpp"), std::string::npos);
  EXPECT_NE(host.find("default_material_catalog.hpp"), std::string::npos);
  EXPECT_NE(bootstrap.find("kDefaultMaterialAlbedoTexturePackagePath"),
            std::string::npos);
  EXPECT_NE(host.find("kDefaultMaterialAlbedoTexturePackagePath"),
            std::string::npos);
  EXPECT_EQ(app.find("default_material_catalog.hpp"), std::string::npos);
  EXPECT_EQ(app.find("\"materials/default/default_albedo.png\""),
            std::string::npos);
  EXPECT_EQ(bootstrap.find("\"materials/default/default_albedo.png\""),
            std::string::npos);
  EXPECT_EQ(host.find("\"materials/default/default_albedo.png\""),
            std::string::npos);
}

TEST(OopArchitectureGuardrailTests, SceneTransformDragServiceOwnsTransformPreviewTransactions) {
  EXPECT_FALSE(std::filesystem::exists(
      source_root() / "src/ui/transform_gizmo_controller.hpp"));
  EXPECT_FALSE(std::filesystem::exists(
      source_root() / "src/ui/transform_gizmo_controller.cpp"));
  EXPECT_FALSE(std::filesystem::exists(
      source_root() / "src/ui/viewport_selection_controller.hpp"));
  EXPECT_FALSE(std::filesystem::exists(
      source_root() / "src/ui/viewport_selection_controller.cpp"));

  const std::string service_source = read_file(
      source_root() / "src/editor/transform/scene_transform_drag_service.cpp");
  const std::string transform_drag_source =
      read_file(source_root() /
                "src/editor/modeling/services/modeling_transform_drag_service.cpp");
  const std::string modeling_source =
      read_file(source_root() /
                "src/editor/modeling/services/modeling_session_service.cpp");
  const std::string inspector_source =
      read_file(source_root() / "src/ui/inspector_panel.cpp");
  const std::string viewport_panel_header =
      read_file(source_root() / "src/ui/qt_viewport_panel.hpp");
  const std::string viewport_panel_source =
      read_file(source_root() / "src/ui/qt_viewport_panel.cpp");

  EXPECT_NE(service_source.find("SceneTransformPreviewTransaction"),
            std::string::npos);
  EXPECT_NE(service_source.find("has_selected_ancestor(document, entity)"),
            std::string::npos);
  EXPECT_NE(service_source.find("try_scene_matrix_inverse(world_transform)"),
            std::string::npos);
  EXPECT_NE(service_source.find(
                "transform_point(*world_inverse, world_pivot)"),
            std::string::npos);
  EXPECT_NE(service_source.find("selected_transform_pivot("), std::string::npos);
  EXPECT_NE(service_source.find("pin_target_local_pivot_to_world_point("),
            std::string::npos);
  EXPECT_NE(service_source.find("editor.scene().batch(label)"),
            std::string::npos);
  EXPECT_NE(transform_drag_source.find("scene_transform_drag_service_.begin("),
            std::string::npos);
  EXPECT_NE(transform_drag_source.find(
                "scene_transform_drag_service_.preview_world_transform_delta("),
            std::string::npos);
  EXPECT_NE(transform_drag_source.find(
                "scene_transform_drag_service_.preview_world_translation_delta("),
            std::string::npos);
  EXPECT_NE(transform_drag_source.find(
                "scene_transform_drag_service_.commit(*history)"),
            std::string::npos);
  EXPECT_NE(modeling_source.find("transform_drag_service_.begin_transform_drag("),
            std::string::npos);
  EXPECT_NE(inspector_source.find("active_transform_preview_"),
            std::string::npos);
  EXPECT_NE(inspector_source.find("preview_selected_to_primary_position("),
            std::string::npos);
  EXPECT_NE(inspector_source.find("preview_selected_to_primary_euler("),
            std::string::npos);
  EXPECT_NE(inspector_source.find("preview_selected_to_primary_scale("),
            std::string::npos);
  EXPECT_EQ(inspector_source.find("QuaderSceneTransformEdit"),
            std::string::npos);
  EXPECT_EQ(inspector_source.find(".scene().set_transforms("),
            std::string::npos);
  EXPECT_EQ(inspector_source.find("SelectedTransformPivot"), std::string::npos);
  EXPECT_EQ(inspector_source.find("local_pivot_for_world_point("),
            std::string::npos);
  EXPECT_EQ(
      inspector_source.find("selected_world_bounds(document, context.assets)"),
      std::string::npos);
  EXPECT_NE(viewport_panel_header.find(
                "ViewportInteractionController interaction_controller_"),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find(
                "interaction_controller_.handle_before_active_tool"),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find(
                "interaction_controller_.handle_after_active_tool"),
            std::string::npos);
  EXPECT_NE(viewport_panel_source.find(
                "interaction_controller_.transform_gizmo_frame()"),
            std::string::npos);
  EXPECT_EQ(viewport_panel_header.find("TransformGizmoController"),
            std::string::npos);
  EXPECT_EQ(viewport_panel_header.find("ViewportSelectionController"),
            std::string::npos);
}

TEST(OopArchitectureGuardrailTests, ModelingSdkOwnsSharedPreviewTransactionLifecycle) {
  const std::string session_header =
      read_file(source_root() / "src/editor/modeling/runtime/session.hpp");
  const std::string sdk_header = read_file(
      source_root() /
      "src/sdk/core/modeling/include/quader/modeling/session/modeling_session.hpp");
  const std::string sdk_preview = read_file(
      source_root() / "src/sdk/core/modeling/src/preview_api.cpp");
  const std::string adapter = read_file(
      source_root() /
      "src/editor/modeling/services/modeling_sdk_projection_adapter.cpp");

  EXPECT_EQ(session_header.find("begin_preview_transaction("),
            std::string::npos);
  EXPECT_EQ(session_header.find("finish_preview_update_from_base("),
            std::string::npos);
  EXPECT_FALSE(std::filesystem::exists(
      source_root() /
      "src/editor/modeling/runtime/session_preview_transactions.cpp"));
  EXPECT_FALSE(std::filesystem::exists(
      source_root() / "src/editor/modeling/operations/bridge_operation.cpp"));
  EXPECT_NE(sdk_header.find("begin_document_preview("), std::string::npos);
  EXPECT_NE(sdk_header.find("commit_preview("), std::string::npos);
  EXPECT_NE(sdk_header.find("cancel_preview("), std::string::npos);
  EXPECT_NE(sdk_preview.find("session.commit_preview("), std::string::npos);
  EXPECT_NE(sdk_preview.find("session.cancel_preview("), std::string::npos);
  EXPECT_NE(adapter.find("session_.begin_document_preview("),
            std::string::npos);
  EXPECT_NE(adapter.find("session_.commit_preview("), std::string::npos);
  EXPECT_NE(adapter.find("session_.cancel_preview("), std::string::npos);
}

TEST(OopArchitectureGuardrailTests,
     ModelingServiceDoesNotRetainRetiredNativeModelingTruth) {
  const std::string service = read_file(
      source_root() /
      "src/editor/modeling/services/modeling_session_service.cpp");
  const std::string adapter = read_file(
      source_root() /
      "src/editor/modeling/services/modeling_sdk_projection_adapter.cpp");

  EXPECT_EQ(service.find("RuntimeOperationExecutor"), std::string::npos);
  EXPECT_EQ(service.find("EditorSession session_"), std::string::npos);
  EXPECT_EQ(service.find("EditorSession modeling_session_"),
            std::string::npos);
  EXPECT_FALSE(std::filesystem::exists(
      source_root() /
      "src/editor/modeling/services/runtime_operation_executor.hpp"));
  EXPECT_NE(adapter.find("quader::modeling::ModelingSession session_"),
            std::string::npos);

  for (const std::filesystem::path &retired : {
           "src/editor/modeling/runtime/session_face_operations.cpp",
           "src/editor/modeling/runtime/session_history.cpp",
           "src/editor/modeling/runtime/session_lighting.cpp",
           "src/editor/modeling/runtime/session_preview_transactions.cpp",
           "src/editor/modeling/preview/transform_preview_service.cpp",
           "src/editor/modeling/preview/transform_preview_service.hpp",
       }) {
    EXPECT_FALSE(std::filesystem::exists(source_root() / retired)) << retired;
  }

  for (const std::filesystem::path &retired_operation : {
           "assign_material_operation.cpp",
           "bevel_vertices_operation.cpp",
           "bridge_operation.cpp",
           "duplicate_meshes_operation.cpp",
           "edge_bevel_operation.cpp",
           "half_cut_operation.cpp",
           "merge_by_distance_operation.cpp",
           "selection_operations.cpp",
           "slice_quad_operation.cpp",
           "thicken_operation.cpp",
       }) {
    EXPECT_FALSE(std::filesystem::exists(
        source_root() / "src/editor/modeling/operations" /
        retired_operation))
        << retired_operation;
  }
}

TEST(OopArchitectureGuardrailTests, RenderFacadeDoesNotOwnOverlayProxyInternals) {
  const std::string header =
      read_file(source_root() / "src/render/scene_render_bridge.hpp");
  const std::string source =
      read_file(source_root() / "src/render/scene_render_bridge.cpp");
  const std::string combined = header + "\n" + source;

  for (std::string_view forbidden : {
           "struct OverlaySlot",
           "struct OverlayProxyGroup",
           "std::array<OverlaySlot",
           "std::array<OverlayProxyGroup",
           "rebuildOverlayProxies",
           "destroyOverlayProxy",
           "toolPreviewMeshProxies_",
       }) {
    EXPECT_EQ(combined.find(forbidden), std::string::npos) << forbidden;
  }
}

TEST(OopArchitectureGuardrailTests, UiDoesNotOwnFilamentResourceAllocation) {
  const std::vector<std::string_view> forbidden = {
      "<filament/",
      "\"filament/",
      "filament::",
      "FilamentBufferDescriptorFactory",
      "TextureRegistry",
      "register_texture(",
      "unregister_texture(",
      "->destroy(",
  };

  for (const std::filesystem::path &file :
       source_files_under(source_root() / "src/ui")) {
    const std::string source = read_file(file);
    const std::string relative = normalized_relative(file);
    for (const std::string &line : split_lines(source)) {
      EXPECT_FALSE(contains_any(line, forbidden)) << relative << ": " << line;
    }
  }
}

TEST(OopArchitectureGuardrailTests,
     UiRuntimeHooksAreInstalledDuringAppComposition) {
  const std::string ui_system_header =
      read_file(source_root() / "src/ui/ui_system.hpp");
  const std::string ui_system_source =
      read_file(source_root() / "src/ui/ui_system.cpp");
  const std::string app_source =
      read_file(source_root() / "src/app/app.cpp");
  const std::string app_header =
      read_file(source_root() / "src/app/app.hpp");
  const std::string install_hooks = source_slice(
      ui_system_source, "void UISystem::install_runtime_hooks(",
      "void UISystem::show()");
  const std::string ui_refresh = source_slice(
      ui_system_source, "void UISystem::refresh(",
      "void UISystem::process_viewport_input()");
  ASSERT_FALSE(install_hooks.empty());
  ASSERT_FALSE(ui_refresh.empty());

  EXPECT_NE(ui_system_header.find(
                "void install_runtime_hooks(QuaderEditorApi &editor,"),
            std::string::npos);
  EXPECT_NE(ui_system_source.find("void UISystem::install_runtime_hooks("),
            std::string::npos);
  EXPECT_NE(app_source.find("ui_.install_runtime_hooks("), std::string::npos);
  EXPECT_NE(app_header.find("void bind_active_scene(SceneSession &scene)"),
            std::string::npos);
  EXPECT_NE(app_source.find(
                "[this](SceneSession &scene) { bind_active_scene(scene); }"),
            std::string::npos);
  EXPECT_NE(app_source.find("void App::bind_active_scene(SceneSession &scene)"),
            std::string::npos);
  EXPECT_NE(app_source.find("*editor_, scene.scene(), scene.assets(),"),
            std::string::npos);
  EXPECT_NE(app_source.find("scene.modeling()"),
            std::string::npos);
  EXPECT_EQ(app_header.find("SceneSession scene_session_"),
            std::string::npos);
  EXPECT_NE(install_hooks.find("editor.viewports().set_runtime_hooks("),
            std::string::npos);
  EXPECT_NE(install_hooks.find("editor.ui().set_runtime_hooks("),
            std::string::npos);
  EXPECT_NE(install_hooks.find("editor.preferences().set_runtime_hooks("),
            std::string::npos);
  EXPECT_EQ(ui_refresh.find("set_runtime_hooks("), std::string::npos);
}

TEST(OopArchitectureGuardrailTests,
     VisiblePivotResolutionHasOneSceneOwner) {
  const std::string resolver = read_file(
      source_root() / "src/scene/scene_transform_pivot.cpp");
  const std::string pivot_service = read_file(
      source_root() / "src/editor/pivot/pivot_tool_service.cpp");
  const std::string query_api =
      read_file(source_root() / "src/api/quader_query_api.cpp");
  const std::string transform_drag = read_file(
      source_root() / "src/editor/transform/scene_transform_drag_service.cpp");

  EXPECT_NE(resolver.find("resolve_entity_visible_pivot("),
            std::string::npos);
  EXPECT_NE(resolver.find("resolve_entity_transform_visible_pivot("),
            std::string::npos);
  EXPECT_NE(resolver.find("resolve_scene_selection_visible_pivot("),
            std::string::npos);
  EXPECT_NE(resolver.find("transform->custom_pivot"), std::string::npos);
  EXPECT_NE(resolver.find("entity_hierarchy_world_bounds("),
            std::string::npos);
  EXPECT_NE(pivot_service.find("resolve_entity_transform_visible_pivot("),
            std::string::npos);
  EXPECT_NE(query_api.find("resolve_scene_selection_visible_pivot("),
            std::string::npos);
  EXPECT_NE(transform_drag.find("resolve_scene_selection_visible_pivot("),
            std::string::npos);
  EXPECT_EQ(query_api.find("single_object_selection_custom_pivot("),
            std::string::npos);
  EXPECT_EQ(pivot_service.find("QVec3 world_origin_of("), std::string::npos);
}

TEST(OopArchitectureGuardrailTests, RenderCodeDoesNotCallDocumentOrAssetMutators) {
  const std::vector<std::string_view> forbidden = {
      "document.create_",
      "document.destroy",
      "document.remove_",
      "document.set_",
      "document.clear_",
      "scene_document.create_",
      "scene_document.destroy",
      "scene_document.remove_",
      "scene_document.set_",
      "scene_document.clear_",
      "assets.add_",
      "assets.replace_",
      "assets.remove_",
      "assets.set_",
      "asset_registry.add_",
      "asset_registry.replace_",
      "asset_registry.remove_",
      "asset_registry.set_",
      "registry.add_",
      "registry.replace_",
      "registry.remove_",
      "registry.set_",
  };

  for (const std::filesystem::path &file :
       source_files_under(source_root() / "src/render")) {
    const std::string source = read_file(file);
    const std::string relative = normalized_relative(file);
    for (const std::string &line : split_lines(source)) {
      EXPECT_FALSE(contains_any(line, forbidden)) << relative << ": " << line;
    }
  }
}

TEST(OopArchitectureGuardrailTests,
     ProjectMaterialsKeepStableIdentityAcrossSceneIoAndRenderBoundaries) {
  const std::string components =
      read_file(source_root() / "src/scene/components.hpp");
  const std::string qdr_schema =
      read_file(source_root() / "src/io/qdr/schema/qdr_scene.fbs");
  const std::string resolver =
      read_file(source_root() / "src/render/project_material_resolver.hpp");
  const std::string evaluation = read_file(
      source_root() / "src/render/material_evaluation_snapshot.hpp");
  const std::string extraction =
      read_file(source_root() / "src/render/scene_render_extraction.cpp");
  const std::string app =
      read_file(source_root() / "src/app/app.cpp");
  const std::string ui_system =
      read_file(source_root() / "src/ui/ui_system.cpp");

  EXPECT_NE(components.find("MaterialReference material"),
            std::string::npos);
  EXPECT_NE(qdr_schema.find("project_material_id:string"),
            std::string::npos);
  EXPECT_NE(qdr_schema.find("project_asset_dependencies"),
            std::string::npos);
  EXPECT_NE(resolver.find("ProjectAssetId id"), std::string::npos);
  EXPECT_NE(resolver.find("catalog_revision"), std::string::npos);
  EXPECT_NE(resolver.find("RenderMaterialEvaluationSnapshot evaluation"),
            std::string::npos);
  EXPECT_NE(evaluation.find("RenderMaterialEvaluationPayload"),
            std::string::npos);
  EXPECT_NE(evaluation.find("std::variant<std::monostate, "
                            "RenderStandardMaterialEvaluationSnapshot>"),
            std::string::npos);
  EXPECT_EQ(evaluation.find("ProjectMaterialDocument"), std::string::npos);
  EXPECT_EQ(evaluation.find("#include \"io/project/"), std::string::npos);
  EXPECT_EQ(evaluation.find("#include <Q"), std::string::npos);
  EXPECT_EQ(resolver.find("#include \"editor/session/"), std::string::npos);
  EXPECT_EQ(resolver.find("#include \"io/project/"), std::string::npos);
  EXPECT_NE(extraction.find("material.project_catalog_revision"),
            std::string::npos);
  EXPECT_NE(app.find("texture_cache_.find(definition.id)"),
            std::string::npos);
  EXPECT_NE(app.find(".emplace(\n                             definition.id,"),
            std::string::npos);
  EXPECT_NE(ui_system.find("project_identity"), std::string::npos);
  EXPECT_NE(ui_system.find("manifest_path().generic_string()"),
            std::string::npos);
}

TEST(OopArchitectureGuardrailTests,
     RenderSnapshotBoundaryOwnsSceneAndOverlayExtraction) {
  const std::string render_sources =
      source_text_under(source_root() / "src/render");
  const std::string snapshot_contract =
      read_file(source_root() / "src/render/scene_render_extraction.hpp");
  const std::string scene_render_bridge =
      read_file(source_root() / "src/render/scene_render_bridge.hpp");
  const std::string sync_service =
      read_file(source_root() / "src/render/scene_render_sync_service.hpp");
  const std::string selected_object_cache = read_file(
      source_root() / "src/render/overlay/selected_object_overlay_cache.hpp");
  const std::string scene_wireframe_cache = read_file(
      source_root() / "src/render/overlay/scene_wireframe_overlay_cache.hpp");
  const std::string mesh_record =
      source_slice(snapshot_contract, "struct RenderSceneMeshRecord",
                   "struct RenderSceneLightRecord");
  const std::string light_record =
      source_slice(snapshot_contract, "struct RenderSceneLightRecord",
                   "struct RenderSceneSnapshotDirtyState");

  EXPECT_NE(render_sources.find("RenderSceneSnapshot"), std::string::npos);
  EXPECT_NE(render_sources.find("RenderSceneMeshRecord"), std::string::npos);
  EXPECT_NE(render_sources.find("RenderSceneLightRecord"), std::string::npos);
  EXPECT_NE(render_sources.find("OverlaySnapshot"), std::string::npos);
  EXPECT_NE(render_sources.find("scene_revision"), std::string::npos);
  EXPECT_NE(render_sources.find("asset_revision"), std::string::npos);

  EXPECT_NE(scene_render_bridge.find("sync(const SceneDocument &document"),
            std::string::npos);
  EXPECT_NE(scene_render_bridge.find("const AssetRegistry &assets"),
            std::string::npos);
  EXPECT_NE(scene_render_bridge.find("RenderSceneSnapshot previous_scene_snapshot_"),
            std::string::npos);

  for (const std::string &source : {sync_service}) {
    EXPECT_EQ(source.find("const SceneDocument &"), std::string::npos);
    EXPECT_EQ(source.find("const AssetRegistry &"), std::string::npos);
    EXPECT_NE(source.find("RenderSceneSnapshot"), std::string::npos);
  }

  for (const std::string &source :
       {selected_object_cache, scene_wireframe_cache}) {
    EXPECT_EQ(source.find("const SceneDocument &"), std::string::npos);
    EXPECT_EQ(source.find("const AssetRegistry &"), std::string::npos);
    EXPECT_NE(source.find("Snapshot"), std::string::npos);
  }

  EXPECT_EQ(mesh_record.find("const MeshAsset *"), std::string::npos);
  EXPECT_EQ(mesh_record.find("const MeshComponent *"), std::string::npos);
  EXPECT_EQ(mesh_record.find("const MaterialComponent *"), std::string::npos);
  EXPECT_EQ(light_record.find("const LightComponent *"), std::string::npos);
}
