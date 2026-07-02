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
using quader::test::guardrail::normalized_relative;
using quader::test::guardrail::read_file;
using quader::test::guardrail::source_root;
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
  if (starts_with(relative, "src/mesh/polygon/operations/")) {
    return cmake.find("src/mesh/polygon/operations/*.cpp") != std::string::npos;
  }
  if (starts_with(relative, "src/mesh/polygon/")) {
    return cmake.find("src/mesh/polygon/*.cpp") != std::string::npos;
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

TEST(OopArchitectureGuardrailTests, InteractionSourceFilesIncludeTheirPublicHeaderFirst) {
  const std::filesystem::path root = source_root() / "src/editor/interactions";
  for (const std::filesystem::path &file : files_with_extension(root, ".cpp")) {
    const std::filesystem::path header =
        file.parent_path() / (file.stem().string() + ".hpp");
    if (!std::filesystem::exists(header)) {
      continue;
    }

    const std::string first_include = first_include_line(read_file(file));
    const std::string relative = normalized_relative(file);
    const std::string stem = file.stem().string();
    const std::vector<std::string> allowed{
        "#include <editor/interactions/" + stem + ".hpp>",
        "#include \"editor/interactions/" + stem + ".hpp\"",
        "#include \"" + stem + ".hpp\"",
    };

    EXPECT_NE(std::find(allowed.begin(), allowed.end(), first_include),
              allowed.end())
        << relative << " first include was: " << first_include;
  }
}

TEST(OopArchitectureGuardrailTests, EverySourceCppIsPartOfCMakeSourceTruth) {
  const std::string cmake = cmake_source_truth();

  for (const std::filesystem::path &file :
       files_with_extension(source_root() / "src", ".cpp")) {
    const std::string relative = normalized_relative(file);
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
  EXPECT_NE(knife.find("tool_geometry_vec2"), std::string::npos);
  EXPECT_NE(knife.find("tool_geometry_vec3"), std::string::npos);
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
      read_file(source_root() / "src/ui/viewport_panel.cpp");
  const std::string ui_shell = ui_system + viewport_panel;
  const std::string builder = read_file(
      source_root() / "src/render/overlay/viewport_scene_overlay_builder.cpp");

  EXPECT_NE(builder.find("append_hierarchy_selection_wires"), std::string::npos);
  EXPECT_NE(builder.find("append_group_selection_cages"), std::string::npos);
  EXPECT_NE(builder.find("append_scene_wireframe_overlay"), std::string::npos);
  EXPECT_NE(builder.find("strip_wireframe_object_face_fills"), std::string::npos);
  EXPECT_NE(
      viewport_panel.find("ViewportSceneOverlayBuilder::build_selection_frame("),
      std::string::npos);
  EXPECT_NE(viewport_panel.find(
                "ViewportSceneOverlayBuilder::strip_wireframe_selection_fills("),
            std::string::npos);
  EXPECT_NE(viewport_panel.find("render_bridge.sync_scene_wireframe_overlay("),
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
    EXPECT_NE(drawn_plane.find("kOverlayDrawnPlanePointSizePixels"),
              std::string::npos);
    EXPECT_NE(drawn_plane.find("kOverlayDrawnPlaneLineWidthPixels"),
              std::string::npos);

    for (const std::string& source : {box, marquee, gizmo}) {
        EXPECT_EQ(source.find("normalizedOr("), std::string::npos);
        EXPECT_EQ(source.find("safeNormalize("), std::string::npos);
        EXPECT_EQ(source.find("fallbackPerpendicular("), std::string::npos);
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
  const std::string host =
      read_file(source_root() / "src/editor/metadata/host_contract.cpp");

  EXPECT_NE(material_catalog.find("kFilamentMaterialDirectory"),
            std::string::npos);
  EXPECT_NE(material_catalog.find("kQuaderMeshMaterialFile"),
            std::string::npos);
  EXPECT_NE(material_catalog.find("kImguiMaterialFile"), std::string::npos);
  EXPECT_NE(material_library.find("filament_material_catalog.hpp"),
            std::string::npos);
  EXPECT_NE(material_library.find("std::string(kFilamentMaterialDirectory)"),
            std::string::npos);
  EXPECT_EQ(material_library.find("\"materials\" / \"quader_mesh.filamat\""),
            std::string::npos);
  EXPECT_EQ(material_library.find("\"materials\" / \"imgui.filamat\""),
            std::string::npos);

  EXPECT_NE(default_catalog.find("kDefaultMaterialAlbedoTexturePackagePath"),
            std::string::npos);
  EXPECT_NE(default_catalog.find("kDefaultMaterialRoughnessTexturePackagePath"),
            std::string::npos);
  EXPECT_NE(app.find("default_material_catalog.hpp"), std::string::npos);
  EXPECT_NE(host.find("default_material_catalog.hpp"), std::string::npos);
  EXPECT_NE(app.find("kDefaultMaterialAlbedoTexturePackagePath"),
            std::string::npos);
  EXPECT_NE(host.find("kDefaultMaterialAlbedoTexturePackagePath"),
            std::string::npos);
  EXPECT_EQ(app.find("\"materials/default/default_albedo.png\""),
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
  const std::string modeling_source =
      read_file(source_root() /
                "src/editor/modeling/services/modeling_session_service.cpp");
  const std::string inspector_source =
      read_file(source_root() / "src/ui/inspector_panel.cpp");
  const std::string viewport_panel_header =
      read_file(source_root() / "src/ui/viewport_panel.hpp");
  const std::string viewport_panel_source =
      read_file(source_root() / "src/ui/viewport_panel.cpp");

  EXPECT_NE(service_source.find("SceneTransformPreviewTransaction"),
            std::string::npos);
  EXPECT_NE(service_source.find("has_selected_ancestor(document, entity)"),
            std::string::npos);
  EXPECT_NE(
      service_source.find(
          "document_local_pivot_for_world_point(document, entity,"),
      std::string::npos);
  EXPECT_NE(service_source.find("selected_transform_pivot("), std::string::npos);
  EXPECT_NE(service_source.find("pin_target_local_pivot_to_world_point("),
            std::string::npos);
  EXPECT_NE(service_source.find("editor.scene().batch(label)"),
            std::string::npos);
  EXPECT_NE(modeling_source.find("scene_transform_drag_service_.begin("),
            std::string::npos);
  EXPECT_NE(modeling_source.find(
                "scene_transform_drag_service_.preview_world_transform_delta("),
            std::string::npos);
  EXPECT_NE(modeling_source.find("scene_transform_drag_service_.commit(*history)"),
            std::string::npos);
  EXPECT_NE(inspector_source.find("SceneTransformDragService transform_service"),
            std::string::npos);
  EXPECT_NE(inspector_source.find("move_selected_to_primary_position("),
            std::string::npos);
  EXPECT_NE(inspector_source.find(
                "rotate_selected_to_primary_euler_around_selection_pivot("),
            std::string::npos);
  EXPECT_NE(
      inspector_source.find("scale_selected_to_primary_scale_around_selection_pivot("),
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

TEST(OopArchitectureGuardrailTests, EditorSessionOwnsSharedPreviewTransactionLifecycle) {
  const std::string session_header =
      read_file(source_root() / "src/editor/modeling/runtime/session.hpp");
  const std::string preview_source =
      read_file(source_root() /
                "src/editor/modeling/runtime/session_preview_transactions.cpp");
  const std::string bridge_source = read_file(
      source_root() / "src/editor/modeling/operations/bridge_operation.cpp");
  const std::string combined = preview_source + "\n" + bridge_source;

  EXPECT_NE(session_header.find(
                "begin_preview_transaction(PreviewTransactionKind kind)"),
            std::string::npos);
  EXPECT_NE(session_header.find("finish_preview_update_from_base("),
            std::string::npos);
  EXPECT_NE(preview_source.find("EditorSession::begin_preview_transaction("),
            std::string::npos);
  EXPECT_NE(
      preview_source.find("EditorSession::finish_preview_update_from_base("),
      std::string::npos);
  EXPECT_NE(bridge_source.find(
                "begin_preview_transaction(PreviewTransactionKind::Bridge)"),
            std::string::npos);

  EXPECT_EQ(
      combined.find("preview_transaction_.kind = PreviewTransactionKind::"),
      std::string::npos);
  EXPECT_EQ(count_occurrences(combined, "preview_transaction_.active = true"),
            1U);
  EXPECT_EQ(count_occurrences(combined, "preview_transaction_.history_state = "
                                        "preview_transaction_.base_state"),
            1U);
  EXPECT_EQ(combined.find("if (!result.ok || !result.changed)"),
            std::string::npos);
}

TEST(OopArchitectureGuardrailTests, RenderFacadeDoesNotOwnOverlayProxyInternals) {
  const std::string header =
      read_file(source_root() / "src/render/scene_render_bridge.hpp");
  const std::string source =
      read_file(source_root() / "src/render/scene_render_bridge.cpp");
  const std::string combined = header + "\n" + source;

  for (std::string_view forbidden : {
           "OverlaySlot",
           "OverlayProxyGroup",
           "rebuildOverlayProxies",
           "destroyOverlayProxy",
           "toolPreviewMeshProxies_",
       }) {
    EXPECT_EQ(combined.find(forbidden), std::string::npos) << forbidden;
  }
}
