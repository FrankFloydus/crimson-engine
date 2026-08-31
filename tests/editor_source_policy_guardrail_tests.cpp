////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "support/guardrail_file_scan.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace quader {
namespace {

using test::guardrail::read_source_file;
using test::guardrail::source_root;
using test::guardrail::source_slice;

TEST(SelectionModeAdapterTests, SelectionModeIdsHaveOneHostCatalogAdapter) {
  const std::string adapter = read_source_file(
      "src/editor/modeling/services/selection_mode_adapter.hpp");
  const std::string action_catalog = read_source_file(
      "src/editor/modeling/services/modeling_action_catalog.cpp");
  const std::string viewport_panel =
      read_source_file("src/ui/qt_viewport_panel.cpp");
  const std::string viewport_adapter =
      read_source_file("src/viewport/host_viewport_contract_adapter.cpp");

  EXPECT_NE(adapter.find("host_selection_mode_id("), std::string::npos);
  EXPECT_NE(adapter.find("host_selection_mode_from_id("), std::string::npos);
  EXPECT_NE(action_catalog.find("host_selection_mode_id("), std::string::npos);
  EXPECT_NE(viewport_panel.find("app_selection_mode_to_viewport("),
            std::string::npos);
  EXPECT_NE(viewport_panel.find("viewport_selection_mode_to_app("),
            std::string::npos);
  EXPECT_NE(viewport_adapter.find("host_selection_mode_from_id("),
            std::string::npos);

  EXPECT_EQ(action_catalog.find("std::string selection_mode_id("),
            std::string::npos);
  EXPECT_EQ(viewport_panel.find("shelfControlIdForHostSelectionMode"),
            std::string::npos);
  EXPECT_EQ(viewport_panel.find("modelingSelectionModeFromHostSelectionMode"),
            std::string::npos);
  EXPECT_EQ(viewport_adapter.find("if (id == \"vertex\")"), std::string::npos);
  EXPECT_EQ(viewport_adapter.find("if (id == \"edge\")"), std::string::npos);
  EXPECT_EQ(viewport_adapter.find("if (id == \"face\")"), std::string::npos);
  EXPECT_EQ(viewport_adapter.find("if (id == \"object\")"), std::string::npos);
  EXPECT_EQ(viewport_adapter.find("if (id == \"group\")"), std::string::npos);
}

TEST(ModelingToolCatalogTests, HostActionCommandToolsAreDerivedFromActionCatalog) {
  const std::filesystem::path root = source_root();
  const std::string source =
      read_source_file(root / "src" / "editor" / "commands" /
                       "modeling_command_tool_catalog.cpp");
  const std::string host_preview_switch_case =
      std::string("case quader_host_contract::") + "HostPreviewKind::";

  EXPECT_NE(source.find("quader_host_contract::action_catalog()"),
            std::string::npos);
  EXPECT_NE(source.find("instant_operation_catalog_owns_action_id("),
            std::string::npos);
  EXPECT_NE(source.find("ModelingActionCatalog::preview_kind_from_host("),
            std::string::npos);
  EXPECT_EQ(source.find("context_descriptor_from_host_action(\""),
            std::string::npos);
  EXPECT_EQ(source.find("previewDescriptorFromHostAction("), std::string::npos);
  EXPECT_EQ(source.find(host_preview_switch_case), std::string::npos);
  EXPECT_EQ(source.find("static const std::array<const ContextActionTool*"),
            std::string::npos);
  EXPECT_EQ(source.find("static const std::array<const PreviewCommandTool*"),
            std::string::npos);

  const std::string test_source =
      read_source_file(root / "tests" / "modeling_tool_catalog_tests.cpp");
  EXPECT_EQ(test_source.find(host_preview_switch_case), std::string::npos);
}

TEST(ViewportToolGuardrailTests, RemovedLegacyViewportToolBase) {
  const std::filesystem::path root = source_root();
  const std::string tool_header =
      read_source_file(root / "src" / "editor" / "tools" / "viewport_tool.hpp");
  const std::string catalog_header = read_source_file(
      root / "src" / "editor" / "tools" / "viewport_tool_catalog.hpp");
  EXPECT_EQ(tool_header.find("ViewportToolBase"), std::string::npos);
  EXPECT_EQ(catalog_header.find("ViewportToolBase"), std::string::npos);
}

TEST(ViewportToolGuardrailTests, EditorOwnsViewportToolRuntimeSources) {
  const std::filesystem::path root = source_root();
  EXPECT_TRUE(std::filesystem::exists(root / "src" / "editor" / "tools" /
                                      "viewport_tool_runtime.hpp"));
  EXPECT_TRUE(std::filesystem::exists(root / "src" / "editor" / "tools" /
                                      "viewport_tool_runtime.cpp"));
  EXPECT_TRUE(std::filesystem::exists(root / "src" / "editor" / "tools" /
                                      "base" / "viewport_tool_catalog.cpp"));
  EXPECT_FALSE(std::filesystem::exists(root / "src" / "viewport" / "modes"));
}

TEST(ViewportToolGuardrailTests, ViewportToolContextUsesNarrowModelingPorts) {
  const std::filesystem::path root = source_root();
  const std::string tool_header =
      read_source_file(root / "src" / "editor" / "tools" / "viewport_tool.hpp");
  const std::string ports_header = read_source_file(
      root / "src" / "editor" / "tools" / "viewport_tool_ports.hpp");
  const std::string context =
      source_slice(tool_header, "struct ViewportToolContext {", "};");
  const std::string two_stage_box_tool = read_source_file(
      root / "src" / "editor" / "tools" / "base" / "two_stage_box_tool.cpp");
  const std::string drag_commit_tool = read_source_file(
      root / "src" / "editor" / "tools" / "base" / "drag_commit_tool.cpp");
  const std::string point_sequence_tool = read_source_file(
      root / "src" / "editor" / "tools" / "base" /
          "point_sequence_tool.cpp");
  const std::string drawn_plane_tool = read_source_file(
      root / "src" / "editor" / "tools" / "base" / "drawn_plane_tool.cpp");
  const std::string modal_viewport_tool = read_source_file(
      root / "src" / "editor" / "tools" / "base" /
          "modal_viewport_tool.cpp");

  EXPECT_NE(ports_header.find("class ViewportToolHoverPort"),
            std::string::npos);
  EXPECT_NE(ports_header.find("class ViewportToolPreviewPort"),
            std::string::npos);
  EXPECT_NE(ports_header.find("class ViewportToolCommitPort"),
            std::string::npos);
  EXPECT_NE(ports_header.find("class ViewportToolCancelPort"),
            std::string::npos);
  EXPECT_NE(ports_header.find("class ViewportToolSelectionModePort"),
            std::string::npos);
  EXPECT_NE(ports_header.find("class ViewportToolCommandResultPort"),
            std::string::npos);
  EXPECT_NE(context.find("ViewportToolPorts ports"), std::string::npos);
  EXPECT_EQ(context.find("ModelingSessionService"), std::string::npos);
  EXPECT_EQ(context.find("modeling"), std::string::npos);

  EXPECT_EQ(two_stage_box_tool.find("context.modeling"), std::string::npos);
  EXPECT_EQ(drag_commit_tool.find("context.modeling"), std::string::npos);
  EXPECT_EQ(point_sequence_tool.find("context.modeling"), std::string::npos);
  EXPECT_EQ(drawn_plane_tool.find("context.modeling"), std::string::npos);
  EXPECT_EQ(modal_viewport_tool.find("context.modeling"), std::string::npos);
}

TEST(ViewportToolGuardrailTests, BoxToolUsesFocusedContextInsteadOfBroadToolBag) {
  const std::filesystem::path root = source_root();
  const std::string tool_header =
      read_source_file(root / "src" / "editor" / "tools" / "viewport_tool.hpp");
  const std::string broad_context =
      source_slice(tool_header, "struct ViewportToolContext {", "};");
  const std::string box_tool_source = read_source_file(
      root / "src" / "editor" / "tools" / "base" / "two_stage_box_tool.cpp");

  EXPECT_NE(tool_header.find("struct ViewportBoxToolContext"),
            std::string::npos);
  EXPECT_NE(broad_context.find("ViewportBoxToolContext* box"),
            std::string::npos);
  EXPECT_EQ(broad_context.find("makeBoxEvent"), std::string::npos);
  EXPECT_EQ(broad_context.find("commit_box_preview"), std::string::npos);
  EXPECT_EQ(broad_context.find("next_mesh_name"), std::string::npos);
  EXPECT_EQ(broad_context.find("default_material"), std::string::npos);

  EXPECT_NE(
      box_tool_source.find("ViewportBoxToolContext &box_context = *context.box"),
      std::string::npos);
  EXPECT_EQ(box_tool_source.find("context.makeBoxEvent"), std::string::npos);
  EXPECT_EQ(box_tool_source.find("context.commit_box_preview"),
            std::string::npos);
  EXPECT_EQ(box_tool_source.find("context.next_mesh_name ="),
            std::string::npos);
  EXPECT_EQ(box_tool_source.find(" context.next_mesh_name"),
            std::string::npos);
}

TEST(ViewportToolGuardrailTests, BoxPreviewStateIsReadFromModelingRuntime) {
  const std::filesystem::path root = source_root();
  const std::string tool_header =
      read_source_file(root / "src" / "editor" / "tools" / "viewport_tool.hpp");
  const std::string modal_state =
      source_slice(tool_header, "struct ViewportModalToolState {", "};");
  const std::string box_tool_source = read_source_file(
      root / "src" / "editor" / "tools" / "base" / "two_stage_box_tool.cpp");
  const std::string router_header =
      read_source_file(root / "src" / "viewport" /
                       "viewport_input_router.hpp");
  const std::string router_source =
      read_source_file(root / "src" / "viewport" / "viewport_input_router.cpp");
  const std::string interaction_source = read_source_file(
      root / "src" / "editor" / "viewport" /
      "viewport_interaction_controller.cpp");

  EXPECT_EQ(modal_state.find("ModelingBoxPreview"), std::string::npos);
  EXPECT_EQ(box_tool_source.find("context.modal_state->box_preview"),
            std::string::npos);
  EXPECT_NE(box_tool_source.find("preview_port.box_preview("),
            std::string::npos);
  EXPECT_NE(interaction_source.find("tool_runtime_.box_preview(preview"),
            std::string::npos);
  EXPECT_EQ(router_source.find("ViewportToolRuntime"), std::string::npos);
  EXPECT_EQ(router_header.find("ModelingSessionService"), std::string::npos);
  EXPECT_EQ(router_source.find("box_preview(const ModelingSessionService"),
            std::string::npos);
}

TEST(ViewportToolGuardrailTests,
     NativePointerGesturesUseSharedIdentityAndReleaseSuppression) {
  const std::string native_header = read_source_file(
      "src/editor/tools/viewport_native_tool_interaction.hpp");
  const std::string native_source = read_source_file(
      "src/editor/tools/viewport_native_tool_interaction.cpp");
  const std::string tool_header =
      read_source_file("src/editor/tools/viewport_tool.hpp");
  const std::string box_source = read_source_file(
      "src/editor/tools/base/two_stage_box_tool.cpp");

  for (const std::string_view retired_flag : {
           "pivot_handle_dragging_", "linear_array_handle_dragging_",
           "curve_point_construction_dragging_",
           "curve_point_construction_moved_"}) {
    EXPECT_EQ(native_header.find(retired_flag), std::string::npos)
        << retired_flag;
  }
  EXPECT_NE(native_header.find("ViewportPointerGesture gesture"),
            std::string::npos);
  EXPECT_NE(native_header.find("ViewportPointerReleaseSuppression"),
            std::string::npos);
  EXPECT_NE(native_source.find("viewport_pointer_edge_after("),
            std::string::npos);
  EXPECT_NE(native_source.find("viewport_pointer_drag_threshold_reached("),
            std::string::npos);
  EXPECT_NE(tool_header.find("primary_pointer_gesture"), std::string::npos);
  EXPECT_NE(tool_header.find("primary_release_suppression"),
            std::string::npos);
  EXPECT_NE(box_source.find("viewport_pointer_edge_after("),
            std::string::npos);
  EXPECT_NE(box_source.find("viewport_pointer_drag_threshold_reached("),
            std::string::npos);
  EXPECT_NE(box_source.find("pointer_interaction_canceled"),
            std::string::npos);
}

TEST(ViewportToolGuardrailTests,
     QtUsesGenericNativeToolSettingsLifecycleDispatch) {
  const std::string filter =
      read_source_file("src/ui/qt_command_event_filter.cpp");
  const std::string commands =
      read_source_file("src/api/quader_commands_api.cpp");

  EXPECT_EQ(filter.find("ViewportToolId::CreatePrimitive"),
            std::string::npos);
  EXPECT_EQ(filter.find("kToolsApplyPrimitiveCreation"), std::string::npos);
  EXPECT_EQ(filter.find("kToolsCancelPrimitiveCreation"), std::string::npos);
  EXPECT_EQ(filter.find("is_primitive_tool"), std::string::npos);
  EXPECT_EQ(filter.find("is_primitive_finalize_command"),
            std::string::npos);
  EXPECT_NE(filter.find("dispatch_native_tool_settings_input_binding("),
            std::string::npos);
  EXPECT_NE(filter.find("execute_native_tool_settings_intent("),
            std::string::npos);
  const std::size_t interpret = filter.find("spin->interpretText()");
  const std::size_t execute =
      filter.find("execute_native_tool_settings_intent(");
  ASSERT_NE(interpret, std::string::npos);
  ASSERT_NE(execute, std::string::npos);
  EXPECT_LT(interpret, execute);
  EXPECT_NE(commands.find("active_native_tool_lifecycle(state.active_tool)"),
            std::string::npos);
  EXPECT_NE(commands.find("ViewportInputActionKind::ModalToolFinalize"),
            std::string::npos);
  EXPECT_NE(commands.find("ViewportShortcutKind::CancelActiveTool"),
            std::string::npos);
}

TEST(ViewportToolGuardrailTests, PointToolsUseFocusedContextInsteadOfBroadToolBag) {
  const std::filesystem::path root = source_root();
  const std::string tool_header =
      read_source_file(root / "src" / "editor" / "tools" / "viewport_tool.hpp");
  const std::string broad_context =
      source_slice(tool_header, "struct ViewportToolContext {", "};");
  const std::string drag_tool_source = read_source_file(
      root / "src" / "editor" / "tools" / "base" / "drag_commit_tool.cpp");
  const std::string point_sequence_tool_source = read_source_file(
      root / "src" / "editor" / "tools" / "base" / "point_sequence_tool.cpp");
  const std::string drawn_plane_tool_source = read_source_file(
      root / "src" / "editor" / "tools" / "base" / "drawn_plane_tool.cpp");

  EXPECT_NE(tool_header.find("struct ViewportPointToolContext"),
            std::string::npos);
  EXPECT_NE(broad_context.find("ViewportPointToolContext* point"),
            std::string::npos);
  EXPECT_EQ(broad_context.find("makeToolEventAt"), std::string::npos);
  EXPECT_EQ(broad_context.find("hit_drawn_plane_captured_point"),
            std::string::npos);
  EXPECT_EQ(broad_context.find("hit_drawn_plane_line"), std::string::npos);

  EXPECT_NE(drag_tool_source.find(
                "ViewportPointToolContext &point_context = *context.point"),
            std::string::npos);
  EXPECT_NE(point_sequence_tool_source.find(
                "ViewportPointToolContext *point_context = context.point"),
            std::string::npos);
  EXPECT_NE(drawn_plane_tool_source.find(
                "ViewportPointToolContext &point_context = *context.point"),
            std::string::npos);
}

TEST(ViewportToolGuardrailTests, DrawnPlaneToolUsesSharedCutMirrorLifecycle) {
  const std::filesystem::path root = source_root();
  const std::string drawn_plane_tool_source = read_source_file(
      root / "src" / "editor" / "tools" / "base" / "drawn_plane_tool.cpp");

  EXPECT_NE(drawn_plane_tool_source.find("enum class DrawnPlaneToolKind"),
            std::string::npos);
  EXPECT_NE(drawn_plane_tool_source.find("handle_drawn_plane_lifecycle("),
            std::string::npos);
  EXPECT_NE(drawn_plane_tool_source.find("begin_drawn_plane_point_drag("),
            std::string::npos);
  EXPECT_NE(drawn_plane_tool_source.find("finalize_drawn_plane_tool("),
            std::string::npos);
  EXPECT_EQ(drawn_plane_tool_source.find(
                "if (modeling_tool() == ModelingViewportTool::Cut) {"),
            std::string::npos);
  EXPECT_EQ(drawn_plane_tool_source.find(
                "if (modeling_tool() == ModelingViewportTool::Mirror) {"),
            std::string::npos);
  EXPECT_EQ(drawn_plane_tool_source.find(
                "context.modal_state->cut_drag_active = command.ok"),
            std::string::npos);
  EXPECT_EQ(drawn_plane_tool_source.find(
                "context.modal_state->mirror_drag_active = command.ok"),
            std::string::npos);
}

TEST(ViewportToolGuardrailTests, ViewportPanelProvidesPointToolContextToModalTools) {
  const std::filesystem::path root = source_root();
  const std::string viewport_panel =
      read_source_file(root / "src" / "ui" / "qt_viewport_panel.cpp");
  const std::string transform_gizmo = read_source_file(
      root / "src" / "editor" / "viewport" / "transform_gizmo_interaction.cpp");
  const std::string transform_drag_service = read_source_file(
      root / "src" / "editor" / "modeling" / "services" /
      "modeling_transform_drag_service.cpp");
  const std::string move_tool = read_source_file(
      root / "src" / "editor" / "interactions" / "move_tool.cpp");

  EXPECT_NE(viewport_panel.find("ViewportPointToolContext point_context"),
            std::string::npos);
  EXPECT_NE(viewport_panel.find("point_context.make_event_at"),
            std::string::npos);
  EXPECT_NE(viewport_panel.find("point_context.hit_drawn_plane_captured_point"),
            std::string::npos);
  EXPECT_NE(viewport_panel.find("point_context.hit_drawn_plane_line"),
            std::string::npos);
  EXPECT_NE(viewport_panel.find(".point = &point_context"),
            std::string::npos);
  EXPECT_NE(viewport_panel.find("make_tool_event_at"), std::string::npos);
  EXPECT_NE(viewport_panel.find("snap_enabled ="), std::string::npos);
  EXPECT_NE(viewport_panel.find("snap_enabled = resolved_grid_snap_enabled"),
            std::string::npos);
  EXPECT_NE(viewport_panel.find("vertex_radius ="), std::string::npos);
  EXPECT_NE(viewport_panel.find(
                "settings.float_value(ViewportFloatSettingId::PickVertexRadius)"),
            std::string::npos);
  EXPECT_NE(viewport_panel.find("constrain_axis = input.modifiers.shift"),
            std::string::npos);
  EXPECT_NE(transform_gizmo.find(
                "!frame.settings.bool_value(ViewportBoolSettingId::ShowGizmos)"),
            std::string::npos);
  EXPECT_NE(transform_gizmo.find(
                "const bool grid_snap_active = viewport_grid_snap_active("),
            std::string::npos);
  EXPECT_NE(transform_gizmo.find(
                "frame.settings, frame.input.modifiers.primary)"),
            std::string::npos);
  EXPECT_NE(transform_gizmo.find(".vertex_snap_radius ="), std::string::npos);
  EXPECT_NE(transform_gizmo.find(
                ".vertex_snap_radius = viewport_vertex_snap_radius(frame.settings)"),
            std::string::npos);
  EXPECT_NE(transform_gizmo.find(".grid_snap_enabled = grid_snap_active"),
            std::string::npos);
  EXPECT_NE(transform_gizmo.find("viewport_rotation_snap_active("),
            std::string::npos);
  EXPECT_NE(transform_gizmo.find(
                "ViewportFloatSettingId::RotationSnapDegrees"),
            std::string::npos);
  EXPECT_NE(transform_gizmo.find(
                ".rotation_snap_degrees = rotation_snap_degrees"),
            std::string::npos);
  EXPECT_NE(transform_drag_service.find(
                "input.rotation_snap_degrees = request.rotation_snap_degrees"),
            std::string::npos);
  EXPECT_NE(move_tool.find("input.rotation_snap_degrees"), std::string::npos);
  EXPECT_EQ(move_tool.find(
                "input.grid_enabled ? input.rotation_snap_degrees : 0.0"),
            std::string::npos);
  EXPECT_NE(transform_gizmo.find(".precision = frame.input.modifiers.primary"),
            std::string::npos);
  const std::size_t selection_center =
      transform_gizmo.find("editor.query().selection_transform_pivot()");
  EXPECT_EQ(transform_gizmo.find(
                "camera_navigation_frame && !dragging_ && "
                "!pending_drag_.has_value()"),
            std::string::npos);
  ASSERT_NE(selection_center, std::string::npos);
  const std::size_t suppress_hover =
      transform_gizmo.find("const bool suppress_hover");
  const std::size_t build_frame =
      transform_gizmo.find("build_transform_gizmo_frame");
  ASSERT_NE(suppress_hover, std::string::npos);
  ASSERT_NE(build_frame, std::string::npos);
  EXPECT_LT(selection_center, suppress_hover);
  EXPECT_LT(suppress_hover, build_frame);
  EXPECT_NE(transform_gizmo.find("frame.input_result.navigation_active ||"),
            std::string::npos);
  const std::size_t active_drag_pivot =
      transform_gizmo.find("pivot = active_drag_pivot_");
  ASSERT_NE(active_drag_pivot, std::string::npos);
  EXPECT_LT(active_drag_pivot, selection_center);
  EXPECT_NE(transform_gizmo.find("active_drag_pivot_ = preview.value().pivot"),
            std::string::npos);

  const std::string selection_interaction =
      read_source_file(root / "src" / "editor" / "viewport" /
                       "viewport_selection_interaction.cpp");
  EXPECT_NE(
      selection_interaction.find("mode == ViewportSelectionMode::Object ||"),
      std::string::npos);
  EXPECT_NE(
      selection_interaction.find("mode == ViewportSelectionMode::Group"),
      std::string::npos);
  EXPECT_EQ(
      selection_interaction.find(
          "editor.selection().summary().mode == QuaderSelectionMode::Group"),
      std::string::npos);
  EXPECT_EQ(viewport_panel.find("selectionOverlaySuppressedForInteraction()"),
            std::string::npos);
  EXPECT_EQ(viewport_panel.find("selection_overlay_cache_"),
            std::string::npos);
  EXPECT_NE(viewport_panel.find(".selection_frame_provider ="),
            std::string::npos);
}

TEST(ViewportToolGuardrailTests, UiShellHasNoDirectModalToolBranches) {
  const std::filesystem::path root = source_root();
  const std::string ui_shell =
      read_source_file(root / "src" / "ui" / "ui_system.cpp");
  EXPECT_EQ(ui_shell.find("active_tool == ViewportToolId::CreatePrimitive"),
            std::string::npos);
  EXPECT_EQ(ui_shell.find("active_tool == ViewportToolId::InsertEdgeLoop"),
            std::string::npos);
  EXPECT_EQ(ui_shell.find("active_tool == ViewportToolId::Knife"),
            std::string::npos);
  EXPECT_EQ(ui_shell.find("active_tool == ViewportToolId::Cut"),
            std::string::npos);
  EXPECT_EQ(ui_shell.find("active_tool == ViewportToolId::Mirror"),
            std::string::npos);
  EXPECT_EQ(ui_shell.find("active_tool == ViewportToolId::Poly"),
            std::string::npos);
  EXPECT_EQ(ui_shell.find("boxPreviewController_"), std::string::npos);
}

TEST(ViewportToolGuardrailTests,
     PersistedCameraDirtyObservationIsSignatureGatedInTheFrameLoop) {
  const std::string viewport_panel =
      read_source_file("src/ui/qt_viewport_panel.cpp");
  const std::string observation = source_slice(
      viewport_panel,
      "void QtViewportPanel::observe_persisted_camera_change(",
      "void QtViewportPanel::prepare_viewports(");

  const std::size_t signature_query =
      observation.find("persisted_perspective_camera_pose_signature(");
  const std::size_t unchanged_gate = observation.find(
      "current_signature == observed_persisted_camera_signature_");
  const std::size_t notify = observation.find(
      "QuaderViewportApiRuntimeAccess::observe_persisted_camera_change(");
  ASSERT_NE(signature_query, std::string::npos);
  ASSERT_NE(unchanged_gate, std::string::npos);
  ASSERT_NE(notify, std::string::npos);
  EXPECT_LT(signature_query, unchanged_gate);
  EXPECT_LT(unchanged_gate, notify);
  EXPECT_EQ(observation.find("open_scenes()"), std::string::npos);
  EXPECT_EQ(observation.find("commands().available()"), std::string::npos);
}

TEST(ViewportToolGuardrailTests, TransformGizmoEligibilityLivesInViewportToolMetadata) {
  const std::filesystem::path root = source_root();
  const std::string interaction = read_source_file(
      root / "src" / "editor" / "viewport" / "transform_gizmo_interaction.cpp");
  const std::string descriptor =
      read_source_file(root / "src" / "editor" / "tools" /
                       "viewport_tool_descriptor_catalog.hpp");
  const std::string catalog = read_source_file(
      root / "src" / "editor" / "tools" / "viewport_tool_catalog.hpp");

  EXPECT_NE(descriptor.find("supports_transform_gizmo"), std::string::npos);
  EXPECT_NE(catalog.find("viewport_tool_supports_transform_gizmo"),
            std::string::npos);
  EXPECT_NE(catalog.find("viewport_tool_interaction_tool"), std::string::npos);
  EXPECT_NE(interaction.find("viewport_tool_supports_transform_gizmo(tool)"),
            std::string::npos);
  EXPECT_NE(interaction.find("viewport_tool_interaction_tool(tool)"),
            std::string::npos);
  EXPECT_EQ(interaction.find("bool supports_transform_gizmo("),
            std::string::npos);
  EXPECT_EQ(interaction.find("return tool == ViewportToolId::Move"),
            std::string::npos);
  EXPECT_EQ(interaction.find("ViewportInteractionTool::move"),
            std::string::npos);
}

TEST(ViewportToolGuardrailTests,
     FaceTextureTransferDispatchKeepsExpensiveWorkBehindSemanticPress) {
  const std::string controller = read_source_file(
      "src/editor/viewport/viewport_interaction_controller.cpp");
  const std::string transfer = source_slice(
      controller,
      "void ViewportInteractionController::handle_material_interactions(",
      "const bool sample_face = frame.input.sample_face_material_pressed;");
  const auto input_gate = transfer.find(
      "if (frame.input.transfer_face_texture_pressed)");
  const auto target_pick = transfer.find("pick_material_face_target(");
  const auto mutation = transfer.find(
      "editor.modeling().transfer_face_texture_to_face(");
  ASSERT_NE(input_gate, std::string::npos);
  ASSERT_NE(target_pick, std::string::npos);
  ASSERT_NE(mutation, std::string::npos);
  EXPECT_LT(input_gate, target_pick);
  EXPECT_LT(target_pick, mutation);
  EXPECT_EQ(controller.find("commands().available()"), std::string::npos);
  EXPECT_EQ(controller.find("keybinding_catalog()"), std::string::npos);
  EXPECT_EQ(controller.find("face_texture_settings()"), std::string::npos);
  const std::string resolver = read_source_file(
      "src/editor/input/viewport_keybinding_resolver.cpp");
  const std::string commands =
      read_source_file("src/api/quader_commands_api.cpp");
  const std::string reserve_action = source_slice(
      resolver, "bool reserve_action_pointer(",
      "void reserve_deferred_action_pointer(");
  EXPECT_NE(reserve_action.find("transfer_face_texture_edges.push_back"),
            std::string::npos);
  EXPECT_NE(reserve_action.find("chord.mouse_button == QuaderMouseButton::None"),
            std::string::npos);
  EXPECT_EQ(reserve_action.find("chord.mouse_button != QuaderMouseButton::Left"),
            std::string::npos);
  const std::string pointer_owner = source_slice(
      controller,
      "ViewportInteractionController::reserve_face_texture_transfer(",
      "void ViewportInteractionController::handle_after_active_tool(");
  EXPECT_NE(pointer_owner.find("ViewportSemanticPointerEdgeKind::Trigger"),
            std::string::npos);
  EXPECT_NE(pointer_owner.find("pointer.press.button"), std::string::npos);
  EXPECT_NE(pointer_owner.find("exact_pointer_release_after("),
            std::string::npos);
  EXPECT_NE(controller.find("texture_transfer_owns_right"),
            std::string::npos);
  EXPECT_NE(commands.find(
                "face_texture_transfer_assignment_uses_unsupported_pointer_gesture"),
            std::string::npos);
  EXPECT_NE(commands.find(
                "drag and wheel gestures do not identify one exact"),
            std::string::npos);
  EXPECT_NE(commands.find("owned pointer edge."), std::string::npos);
  const std::string selection = read_source_file(
      "src/editor/viewport/viewport_selection_interaction.cpp");
  EXPECT_NE(selection.find("kFacePlaneCutGestureEnabled = false"),
            std::string::npos);
  EXPECT_NE(selection.find("if (kFacePlaneCutGestureEnabled &&"),
            std::string::npos);
  EXPECT_NE(selection.find("cut_selected_meshes_by_hovered_face_plane()"),
            std::string::npos);
}

TEST(ViewportToolGuardrailTests,
     HostViewportAdapterSourceUsesViewportToolDescriptors) {
  const std::filesystem::path root = source_root();
  const std::string adapter = read_source_file(
      root / "src" / "viewport" / "host_viewport_contract_adapter.cpp");
  EXPECT_NE(adapter.find("find_viewport_tool_descriptor(tool)"),
            std::string::npos);
  EXPECT_NE(adapter.find("find_viewport_tool_descriptor(id)"),
            std::string::npos);
  EXPECT_NE(
      adapter.find("quader_host_contract::find_action_descriptor(id)"),
      std::string::npos);
  EXPECT_EQ(adapter.find("case ViewportToolId::"), std::string::npos);
  EXPECT_NE(adapter.find("descriptor->editor_owned_native_lifecycle"),
            std::string::npos);
  EXPECT_EQ(adapter.find("tool == ViewportToolId::"), std::string::npos);
}

TEST(ViewportToolGuardrailTests,
     PivotHandleUsesSphereOverlayAndConsumesSelectionFallback) {
  const std::filesystem::path root = source_root();
  const std::string viewport_panel =
      read_source_file(root / "src" / "ui" / "qt_viewport_panel.cpp");
  const std::string native_tools = read_source_file(
      root / "src" / "editor" / "tools" /
      "viewport_native_tool_interaction.cpp");
  const std::string interaction_controller = read_source_file(
      root / "src" / "editor" / "viewport" /
      "viewport_interaction_controller.cpp");
  const std::string overlay_projection = read_source_file(
      root / "src" / "editor" / "viewport" /
      "viewport_tool_overlay_projection.cpp");

  const std::string pivot_overlay =
      source_slice(overlay_projection,
                   "void ViewportToolOverlayProjection::append_pivot_handle",
                   "void ViewportToolOverlayProjection::"
                   "append_linear_array_handle");
  EXPECT_NE(overlay_projection.find("void append_pivot_ring("),
            std::string::npos);
  EXPECT_NE(pivot_overlay.find("append_pivot_ring("), std::string::npos);
  EXPECT_NE(overlay_projection.find("frame.lines.push_back({"),
            std::string::npos);
  EXPECT_EQ(pivot_overlay.find("frame.points.push_back"), std::string::npos);
  EXPECT_NE(native_tools.find("blocked_by_navigation"),
            std::string::npos);
  EXPECT_NE(native_tools.find("pivot->clear_quick_override()"),
            std::string::npos);
  EXPECT_NE(native_tools.find("!blocked_by_navigation && "
                              "input.quick_pivot_down"),
            std::string::npos);

  const std::string pointer_lifecycle = source_slice(
      interaction_controller,
      "ViewportInteractionController::handle_tool_pointer_lifecycle(",
      "} // namespace quader");
  EXPECT_NE(pointer_lifecycle.find(
                "output.native_tool.blocks_transform_gizmo"),
            std::string::npos);
  EXPECT_NE(
      pointer_lifecycle.find(
          "ViewportBeforeToolInteractionResult{.gizmo_consumed = true}"),
      std::string::npos);
  EXPECT_NE(viewport_panel.find(
                "interaction_controller_.handle_tool_pointer_lifecycle("),
            std::string::npos);
  EXPECT_EQ(viewport_panel.find("const bool pivot_consumed"),
            std::string::npos);
  EXPECT_EQ(viewport_panel.find("pivot_blocks_gizmo ||"),
            std::string::npos);
}

TEST(ViewportToolGuardrailTests,
     QtDoesNotEnumerateEditorNativeToolLifecycleOrPointerPrecedence) {
  const std::string panel =
      read_source_file("src/ui/qt_viewport_panel.cpp");
  const std::string input = source_slice(
      panel, "void QtViewportPanel::process_viewport_input(",
      "bool QtViewportPanel::sync_active_tool_from_editor(");
  const std::string transform_gizmo = read_source_file(
      "src/editor/viewport/transform_gizmo_interaction.cpp");
  const std::string transform_gizmo_header = read_source_file(
      "src/editor/viewport/transform_gizmo_interaction.hpp");
  const std::string native_interaction = read_source_file(
      "src/editor/tools/viewport_native_tool_interaction.cpp");
  const std::string interaction_frame = read_source_file(
      "src/editor/viewport/viewport_interaction_frame.hpp");
  const std::string native_frame = read_source_file(
      "src/editor/tools/viewport_native_tool_interaction.hpp");
  const std::string curve_picking = read_source_file(
      "src/editor/viewport/viewport_curve_picking.cpp");
  const std::string selection = read_source_file(
      "src/editor/viewport/viewport_selection_interaction.cpp");
  const std::string selection_header = read_source_file(
      "src/editor/viewport/viewport_selection_interaction.hpp");
  const std::string panel_header =
      read_source_file("src/ui/qt_viewport_panel.hpp");
  const std::string ui_system_header = read_source_file("src/ui/ui_system.hpp");
  const std::string ui_system = read_source_file("src/ui/ui_system.cpp");
  const std::string interaction_controller_header = read_source_file(
      "src/editor/viewport/viewport_interaction_controller.hpp");
  const std::string interaction_controller = read_source_file(
      "src/editor/viewport/viewport_interaction_controller.cpp");
  const std::string native_coordinator = read_source_file(
      "src/editor/tools/native_viewport_tool_coordinator.cpp");
  const std::string curve_port = read_source_file(
      "src/editor/tools/viewport_curve_interaction_port.hpp");

  ASSERT_FALSE(input.empty());
  EXPECT_NE(input.find(
                "interaction_controller_.handle_tool_pointer_lifecycle("),
            std::string::npos);
  EXPECT_NE(input.find(
                "active_tool_uses_editor_native_lifecycle("),
            std::string::npos);
  for (const std::string_view native_tool : {
           "ViewportToolId::Curve", "ViewportToolId::CreatePrimitive",
           "ViewportToolId::LinearArray", "ViewportToolId::RadialArray",
           "ViewportToolId::Pivot"}) {
    EXPECT_EQ(input.find(native_tool), std::string::npos) << native_tool;
  }
  EXPECT_EQ(input.find("curve_gizmo_prepass"), std::string::npos);
  EXPECT_EQ(interaction_controller.find("ViewportToolId::Curve"),
            std::string::npos);
  EXPECT_NE(interaction_controller.find(
                "native_tools.curve_control_gizmo_start_allowed("),
            std::string::npos);
  EXPECT_NE(native_coordinator.find(
                "runtime_.native_lifecycle_active(viewport_tool_id(active_tool))"),
            std::string::npos);
  EXPECT_NE(curve_port.find("allows_control_gizmo_start("),
            std::string::npos);
  EXPECT_EQ(input.find("interaction_controller_.handle_native_tools("),
            std::string::npos);
  EXPECT_EQ(input.find("interaction_controller_.handle_before_active_tool("),
            std::string::npos);
  EXPECT_NE(input.find("const bool scene_surface_pick_requested"),
            std::string::npos);
  EXPECT_NE(input.find("point_placement->surface_snapshot(document, assets)"),
            std::string::npos);
  EXPECT_NE(input.find(".scene_surfaces = interaction_frame.scene_surfaces"),
            std::string::npos);
  EXPECT_NE(transform_gizmo.find("frame.scene_surfaces != nullptr"),
            std::string::npos);
  EXPECT_EQ(native_interaction.find("scene_surfaces"), std::string::npos);
  EXPECT_NE(native_frame.find("ViewportCurveControlPickingPort"),
            std::string::npos);
  EXPECT_NE(native_interaction.find("frame.curve_control_picker->pick("),
            std::string::npos);
  EXPECT_NE(curve_picking.find("scene_surfaces_ != nullptr"),
            std::string::npos);
  EXPECT_NE(interaction_controller.find(
                "ViewportCurveControlPickingAdapter curve_control_picker("),
            std::string::npos);
  EXPECT_NE(selection.find(".scene_surfaces = frame.scene_surfaces"),
            std::string::npos);
  EXPECT_EQ(input.find("scene_mesh_surface_distance"), std::string::npos);
  EXPECT_EQ(interaction_frame.find("scene_mesh_surface_distance"),
            std::string::npos);
  EXPECT_EQ(native_frame.find("scene_mesh_surface_distance"),
            std::string::npos);
  EXPECT_NE(selection.find("viewport_pointer_edge_after("),
            std::string::npos);
  EXPECT_NE(selection.find("viewport_pointer_deadline_reached("),
            std::string::npos);
  EXPECT_NE(selection.find("box_release_suppression_.consume("),
            std::string::npos);
  EXPECT_NE(panel_header.find("NativeViewportToolCoordinator"),
            std::string::npos);
  EXPECT_NE(interaction_controller_header.find(
                "ViewportCurveInteractionPort *curve"),
            std::string::npos);
  EXPECT_NE(transform_gizmo_header.find("ViewportCurveInteractionPort"),
            std::string::npos);
  EXPECT_NE(selection_header.find("ViewportCurveInteractionPort"),
            std::string::npos);
  for (const std::string_view concrete_service : {
           "CurveToolService", "PivotToolService", "LinearArrayToolService",
           "RadialArrayToolService"}) {
    EXPECT_EQ(panel_header.find(concrete_service), std::string::npos)
        << concrete_service;
    EXPECT_EQ(panel.find(concrete_service), std::string::npos)
        << concrete_service;
    EXPECT_EQ(ui_system_header.find(concrete_service), std::string::npos)
        << concrete_service;
    EXPECT_EQ(ui_system.find(concrete_service), std::string::npos)
        << concrete_service;
    EXPECT_EQ(interaction_controller_header.find(concrete_service),
              std::string::npos)
        << concrete_service;
    EXPECT_EQ(interaction_controller.find(concrete_service), std::string::npos)
        << concrete_service;
    EXPECT_EQ(transform_gizmo_header.find(concrete_service),
              std::string::npos)
        << concrete_service;
    EXPECT_EQ(transform_gizmo.find(concrete_service), std::string::npos)
        << concrete_service;
    EXPECT_EQ(selection_header.find(concrete_service), std::string::npos)
        << concrete_service;
    EXPECT_EQ(selection.find(concrete_service), std::string::npos)
        << concrete_service;
  }
}

TEST(ModelingSessionServiceSplitTests,
     RuntimeToolCommandConstructionLivesOutsideFacade) {
  const std::string facade = read_source_file(
      "src/editor/modeling/services/modeling_session_service.cpp");
  const std::string command_service = read_source_file(
      "src/editor/modeling/services/runtime_tool_command_service.cpp");

  const std::array<std::string, 9> settings_types = {
      "InsertEdgeLoopPickRuntimeCommandSettings",
      "CutRayRuntimeCommandSettings",
      "CutPointDragRuntimeCommandSettings",
      "SetCutModeRuntimeCommandSettings",
      "MirrorRayRuntimeCommandSettings",
      "MirrorPointDragRuntimeCommandSettings",
      "PolyHoverRuntimeCommandSettings",
      "BoxRayRuntimeCommandSettings",
      "BoxHeightRuntimeCommandSettings",
  };
  for (const std::string &settings_type : settings_types) {
    EXPECT_EQ(facade.find(settings_type), std::string::npos) << settings_type;
    EXPECT_NE(command_service.find(settings_type), std::string::npos)
        << settings_type;
  }

  EXPECT_NE(
      facade.find("RuntimeToolCommandService::update_insert_edge_loop_hover"),
      std::string::npos);
  EXPECT_NE(facade.find("RuntimeToolCommandService::update_cut_hover"),
            std::string::npos);
  EXPECT_NE(facade.find("RuntimeToolCommandService::update_mirror_hover"),
            std::string::npos);
  EXPECT_NE(facade.find("RuntimeToolCommandService::update_poly_hover"),
            std::string::npos);
  EXPECT_NE(facade.find("RuntimeToolCommandService::update_box_hover"),
            std::string::npos);
  EXPECT_EQ(command_service.find("create_box_from_bounds"), std::string::npos);
  EXPECT_EQ(command_service.find("commit_box"), std::string::npos);
  EXPECT_EQ(facade.find("viewport_grid_point_from_ray("), std::string::npos);
  EXPECT_EQ(facade.find("runtime_.update_poly_hover"), std::string::npos);
  EXPECT_EQ(facade.find("runtime_.accept_poly_current_point"),
            std::string::npos);
  EXPECT_EQ(facade.find("runtime_.commit_poly_tool"), std::string::npos);
}

TEST(ModelingSessionServiceSplitTests,
     SceneSessionDirtyPollingUsesConstantTimeModelingRevision) {
  const std::string scene_session =
      read_source_file("src/editor/session/scene_session.cpp");
  const std::string revision_stamp = source_slice(
      scene_session,
      "SceneContentRevisionStamp SceneSession::content_revision_stamp() const",
      "bool SceneSession::dirty() const");

  ASSERT_FALSE(revision_stamp.empty());
  EXPECT_NE(revision_stamp.find("modeling_->content_revision()"),
            std::string::npos);
  EXPECT_EQ(revision_stamp.find("selection_summary()"), std::string::npos);
}

TEST(ModelingSessionServiceSplitTests,
     ObjectBindingRegistryOwnsEveryLiveIdentityEdge) {
  const std::string cmake = read_source_file("CMakeLists.txt");
  const std::string services = read_source_file(
      "src/editor/modeling/services/modeling_session_services.cpp");
  const std::string service_aggregate = read_source_file(
      "src/editor/modeling/services/modeling_session_services.hpp");
  const std::string binding_registry = read_source_file(
      "src/editor/modeling/runtime/modeling_object_binding_registry.hpp");
  const std::string runtime_map_header = read_source_file(
      "src/editor/modeling/runtime/modeling_object_runtime_map.hpp");
  const std::string sdk_adapter = read_source_file(
      "src/editor/modeling/services/modeling_sdk_projection_adapter.cpp");

  EXPECT_NE(cmake.find(
                "src/editor/modeling/runtime/"
                "modeling_object_binding_registry.cpp"),
            std::string::npos);
  EXPECT_NE(
      cmake.find("src/editor/modeling/runtime/modeling_object_runtime_map.cpp"),
      std::string::npos);
  EXPECT_NE(cmake.find(
                "src/editor/modeling/services/"
                "modeling_host_publication_transaction.cpp"),
            std::string::npos);
  EXPECT_NE(service_aggregate.find(
                "editor/modeling/runtime/modeling_object_runtime_map.hpp"),
            std::string::npos);
  EXPECT_EQ(cmake.find("src/editor/modeling/services/ModelingObjectRegistry"),
            std::string::npos);
  EXPECT_EQ(service_aggregate.find("ModelingObjectRegistry"),
            std::string::npos);
  EXPECT_EQ(services.find("ModelingObjectRuntimeMap::"), std::string::npos);

  EXPECT_NE(binding_registry.find("entities_by_object_"), std::string::npos);
  EXPECT_NE(binding_registry.find("objects_by_entity_"), std::string::npos);
  EXPECT_NE(binding_registry.find("sdk_objects_by_object_"),
            std::string::npos);
  EXPECT_NE(binding_registry.find("objects_by_sdk_object_"),
            std::string::npos);
  EXPECT_NE(binding_registry.find("scene_identity_by_object_"),
            std::string::npos);
  EXPECT_NE(runtime_map_header.find(
                "using ModelingObjectRuntimeMap = "
                "ModelingObjectBindingRegistry;"),
            std::string::npos);
  EXPECT_EQ(runtime_map_header.find("public ModelingObjectBindingRegistry"),
            std::string::npos);
  EXPECT_EQ(runtime_map_header.find("entities_by_object_"), std::string::npos);
  EXPECT_EQ(runtime_map_header.find("sdk_objects_by_object_"),
            std::string::npos);

  EXPECT_NE(sdk_adapter.find("ModelingHostPublicationTransaction"),
            std::string::npos);
  EXPECT_NE(sdk_adapter.find("bind_sdk_object("), std::string::npos);
  EXPECT_NE(sdk_adapter.find("sdk_object_for_modeling_object("),
            std::string::npos);
  EXPECT_NE(sdk_adapter.find("modeling_object_for_sdk_object("),
            std::string::npos);
  EXPECT_EQ(sdk_adapter.find("std::map<int, ObjectId> object_ids_"),
            std::string::npos);
  EXPECT_EQ(sdk_adapter.find("native_object_ids_"), std::string::npos);
}

TEST(ModelingSessionServiceSplitTests,
     HostPublicationHasOneBoundedRollbackOwnerAndNoFullFallback) {
  const std::string transaction_header = read_source_file(
      "src/editor/modeling/services/"
      "modeling_host_publication_transaction.hpp");
  const std::string transaction_source = read_source_file(
      "src/editor/modeling/services/"
      "modeling_host_publication_transaction.cpp");
  const std::string sdk_adapter = read_source_file(
      "src/editor/modeling/services/modeling_sdk_projection_adapter.cpp");
  const std::string legacy_upgrade = read_source_file(
      "src/editor/modeling/services/"
      "modeling_legacy_local_frame_upgrade.cpp");
  const std::string mesh_sync = read_source_file(
      "src/editor/modeling/services/modeling_mesh_sync_service.cpp");
  const std::string asset_registry =
      read_source_file("src/assets/asset_registry.hpp");
  const std::string scene_document =
      read_source_file("src/scene/scene_document.hpp");
  const std::string binding_registry = read_source_file(
      "src/editor/modeling/runtime/modeling_object_binding_registry.hpp");
  const std::string pivot =
      read_source_file("src/editor/pivot/pivot_tool_service.cpp");

  for (const std::string *source :
       {&transaction_header, &transaction_source, &sdk_adapter,
        &legacy_upgrade, &pivot}) {
    EXPECT_EQ(source->find("ModelingHostPublicationCheckpointScope::Full"),
              std::string::npos);
    EXPECT_EQ(source->find("full_workspace"), std::string::npos);
    EXPECT_EQ(source->find("bounded_required"), std::string::npos);
  }
  EXPECT_EQ(transaction_header.find("AssetRegistryProjectionCheckpoint"),
            std::string::npos);
  EXPECT_EQ(transaction_header.find("SceneProjectionCheckpoint"),
            std::string::npos);
  EXPECT_EQ(transaction_source.find("capture_scene_entity_state_tree"),
            std::string::npos);
  EXPECT_EQ(transaction_source.find("capture_projection_checkpoint()"),
            std::string::npos);
  EXPECT_EQ(mesh_sync.find("restore_projected_asset_checkpoint"),
            std::string::npos);
  EXPECT_EQ(mesh_sync.find("restore_projected_scene_checkpoint"),
            std::string::npos);
  EXPECT_EQ(asset_registry.find("AssetRegistryProjectionCheckpoint"),
            std::string::npos);
  EXPECT_EQ(asset_registry.find("capture_projection_checkpoint()"),
            std::string::npos);
  EXPECT_EQ(asset_registry.find("MeshAssetProjectionCheckpoint"),
            std::string::npos);
  EXPECT_EQ(asset_registry.find("CurveAssetProjectionCheckpoint"),
            std::string::npos);
  EXPECT_EQ(scene_document.find("SceneProjectionCheckpoint"),
            std::string::npos);
  EXPECT_EQ(scene_document.find("SceneCurveProjectionCheckpoint"),
            std::string::npos);
  EXPECT_EQ(scene_document.find("SceneMeshProjectionCheckpoint"),
            std::string::npos);
  EXPECT_EQ(scene_document.find("capture_projection_checkpoint()"),
            std::string::npos);
  EXPECT_EQ(scene_document.find("capture_curve_projection_checkpoint"),
            std::string::npos);
  EXPECT_EQ(scene_document.find("capture_mesh_projection_checkpoint"),
            std::string::npos);

  EXPECT_NE(transaction_header.find("capture_mutation_fence"),
            std::string::npos);
  EXPECT_NE(transaction_header.find(
                "mutations_match_acknowledged_fence"),
            std::string::npos);
  EXPECT_NE(transaction_header.find("acknowledge_current_mutations"),
            std::string::npos);
  EXPECT_NE(transaction_header.find("preserve_concurrent_owner_changes"),
            std::string::npos);
  EXPECT_NE(sdk_adapter.find(
                ".preserve_concurrent_owner_changes = true"),
            std::string::npos);
  EXPECT_NE(asset_registry.find(
                "capture_modeling_projection_mutation_fence"),
            std::string::npos);
  EXPECT_NE(asset_registry.find(
                "preserving_concurrent_changes"),
            std::string::npos);
  EXPECT_NE(scene_document.find(
                "capture_modeling_projection_mutation_fence"),
            std::string::npos);
  EXPECT_NE(scene_document.find("preserving_concurrent_changes"),
            std::string::npos);
  EXPECT_NE(binding_registry.find("capture_mutation_fence"),
            std::string::npos);
  EXPECT_NE(binding_registry.find("preserving_concurrent_changes"),
            std::string::npos);

  constexpr std::array<std::string_view, 10> retired_adapter_state{
      "HostPublicationState",
      "preview_base_snapshot_",
      "workspace_update_base_snapshot_",
      "workspace_preview_active_",
      "workspace_preview_undo_mark_",
      "workspace_transform_participants_",
      "workspace_transform_commit_mutation_",
      "rollback_undo_since",
      "(void)publication.rollback",
      "static_cast<void>(publication.rollback",
  };
  for (const std::string_view retired : retired_adapter_state) {
    EXPECT_EQ(sdk_adapter.find(retired), std::string::npos) << retired;
  }

  EXPECT_NE(transaction_source.find("active_publication_scenes"),
            std::string::npos);
  EXPECT_NE(transaction_source.find(
                "active_publication_asset_registries"),
            std::string::npos);
  EXPECT_NE(transaction_source.find(
                "active_publication_binding_registries"),
            std::string::npos);
  EXPECT_NE(transaction_source.find(
                "active_publication_modeling_sessions"),
            std::string::npos);

  EXPECT_NE(pivot.find("ModelingHostPublicationTransaction"),
            std::string::npos);
  EXPECT_NE(pivot.find("modeling_.operation_preview_active()"),
            std::string::npos);
  EXPECT_NE(pivot.find(
                "ModelingHostPublicationCheckpointScope::MeshProjection"),
            std::string::npos);
  EXPECT_NE(pivot.find("pivot_publication_participants"),
            std::string::npos);
  EXPECT_NE(pivot.find("scene.descendants(root, true)"),
            std::string::npos);
  EXPECT_NE(pivot.find("pivot_plan_asset_participants"),
            std::string::npos);
  EXPECT_NE(pivot.find("publication.track_created_asset"),
            std::string::npos);
  EXPECT_NE(pivot.find(".capture_hierarchy = true"),
            std::string::npos);
  EXPECT_NE(pivot.find(".capture_scene_selection = true"),
            std::string::npos);
  EXPECT_NE(pivot.find("ModelingCommandResult restore_modeling_documents()"),
            std::string::npos);
  EXPECT_NE(pivot.find(
                "return modeling_.restore_mesh_documents_from_scene(entities)"),
            std::string::npos);
  EXPECT_NE(pivot.find("restore_documents_as_history_transition"),
            std::string::npos);
  EXPECT_NE(pivot.find("if (publication.rollback())"),
            std::string::npos);
  EXPECT_NE(pivot.find("failure.changed = true"), std::string::npos);
  EXPECT_NE(pivot.find("Host publication rollback failed."),
            std::string::npos);
  EXPECT_NE(pivot.find(".changed = !plans_.empty()"),
            std::string::npos);
  const std::string resolved_pivot_replay = source_slice(
      pivot, "CommandResult apply_resolved_plans(",
      "ModelingCommandResult restore_modeling_documents()");
  ASSERT_FALSE(resolved_pivot_replay.empty());
  EXPECT_NE(resolved_pivot_replay.find(".changed = true"),
            std::string::npos);

  constexpr std::array<std::string_view, 8> retired_pivot_fallbacks{
      "reset_from_scene(",
      "rollback_initial_apply",
      "for (auto rollback =",
      "void restore_modeling_documents()",
      "(void)apply_plan_before",
      "(void)apply_plan_after",
      "(void)publication.rollback",
      "static_cast<void>(publication.rollback",
  };
  for (const std::string_view retired : retired_pivot_fallbacks) {
    EXPECT_EQ(pivot.find(retired), std::string::npos) << retired;
  }
}

TEST(ModelingSessionServiceSplitTests,
     ObjectSelectionMappingLivesInSelectionProjectionService) {
  const std::string facade = read_source_file(
      "src/editor/modeling/services/modeling_session_service.cpp");
  const std::string sdk_adapter = read_source_file(
      "src/editor/modeling/services/modeling_sdk_projection_adapter.cpp");
  const std::string projection =
      read_source_file("src/editor/selection/selection_projection_service.cpp");
  const std::string selection_owner =
      read_source_file("src/editor/selection/scene_selection_application.cpp");

  EXPECT_NE(facade.find("sdk_projection_adapter_.set_object_selection("),
            std::string::npos);
  EXPECT_EQ(facade.find("const auto object = "
                        "object_registry_.modeling_object_for_entity(entity);"),
            std::string::npos);
  EXPECT_EQ(facade.find("const auto object = "
                        "object_registry_.modeling_object_for_entity(active);"),
            std::string::npos);
  EXPECT_NE(sdk_adapter.find("object_map_.modeling_object_for_entity(entity)"),
            std::string::npos);
  EXPECT_NE(sdk_adapter.find("session_.apply_object_selection("),
            std::string::npos);
  EXPECT_NE(projection.find("project_object_selection_for_scene_entities("),
            std::string::npos);
  EXPECT_NE(projection.find("runtime_object_selection_for_scene_entities("),
            std::string::npos);
  EXPECT_NE(projection.find("resolve_projected_active_selection("),
            std::string::npos);
  EXPECT_NE(projection.find("apply_runtime_object_selection("),
            std::string::npos);
  EXPECT_EQ(projection.find("apply_exact_scene_selection("), std::string::npos);
  EXPECT_NE(projection.find("SceneSelectionApplicationMode::RuntimeSync"),
            std::string::npos);
  EXPECT_NE(selection_owner.find("apply_runtime_sync_scene_selection("),
            std::string::npos);
  EXPECT_EQ(projection.find("ordered_selected.front()"), std::string::npos);
}

TEST(ModelingSessionServiceSplitTests,
     SelectionProjectionUsesImmutableValueWithoutRetainedSessionViews) {
  const std::array sources{
      read_source_file(
          "src/editor/modeling/services/modeling_sdk_projection_adapter.hpp"),
      read_source_file(
          "src/editor/modeling/services/modeling_sdk_projection_adapter.cpp"),
      read_source_file(
          "src/editor/modeling/services/modeling_session_service.cpp"),
      read_source_file(
          "src/editor/selection/selection_projection_service.hpp"),
      read_source_file(
          "src/editor/selection/selection_projection_service.cpp"),
  };
  constexpr std::array<std::string_view, 6> retired{
      "native_selection_projection_view",
      "selection_projection_view",
      "native_selection_projection(",
      "apply_native_selection_projection",
      "transient_selection_projection",
      "apply_selection_projection(",
  };
  for (const std::string &source : sources) {
    for (const std::string_view symbol : retired) {
      EXPECT_EQ(source.find(symbol), std::string::npos) << symbol;
    }
  }

  const std::string snapshot = read_source_file(
      "src/editor/selection/modeling_selection_projection_snapshot.hpp");
  EXPECT_NE(snapshot.find("class ModelingSelectionProjectionSnapshot final"),
            std::string::npos);
  EXPECT_NE(snapshot.find("private:"), std::string::npos);
  EXPECT_NE(snapshot.find("std::span<const quader_editor_core::PolygonObject>"),
            std::string::npos);
  EXPECT_EQ(snapshot.find("EditorSession &"), std::string::npos);
  EXPECT_EQ(snapshot.find("SessionState &"), std::string::npos);
}

TEST(ModelingSessionServiceSplitTests,
      SceneToModelingSelectionProjectionResultsAreNeverDiscarded) {
  constexpr std::array<std::string_view, 6> checked_calls{
      ".project_object_selection_from_scene(",
      "->project_object_selection_from_scene(",
      ".clear_object_selection_preserving_mode(",
      "->clear_object_selection_preserving_mode(",
      ".clear_runtime_object_selection_for_scene_owned_selection(",
      "->clear_runtime_object_selection_for_scene_owned_selection(",
  };
  for (const std::filesystem::path &path :
       test::guardrail::source_files_under(source_root() / "src")) {
    const std::string source = test::guardrail::read_file(path);
    const std::vector<std::string> lines =
        test::guardrail::split_lines(source);
    for (std::size_t index = 0u; index < lines.size(); ++index) {
      const std::string &line = lines[index];
      std::size_t call = std::string::npos;
      for (const std::string_view checked_call : checked_calls) {
        call = line.find(checked_call);
        if (call != std::string::npos) {
          break;
        }
      }
      if (call == std::string::npos) {
        continue;
      }

      std::string consumption_context = line.substr(0u, call);
      for (std::size_t lookback = 1u;
           lookback <= 3u && lookback <= index; ++lookback) {
        const std::string &previous = lines[index - lookback];
        consumption_context.insert(0u, previous + '\n');
        if (previous.find(';') != std::string::npos ||
            previous.find('{') != std::string::npos ||
            previous.find('}') != std::string::npos) {
          break;
        }
      }
      const bool consumed =
          consumption_context.find('=') != std::string::npos ||
          consumption_context.find("return ") != std::string::npos;
      EXPECT_TRUE(consumed)
          << test::guardrail::normalized_relative(path) << ':' << index + 1u
          << " discards the checked scene-to-modeling selection projection";
      EXPECT_EQ(consumption_context.find("(void)"), std::string::npos)
          << test::guardrail::normalized_relative(path) << ':' << index + 1u;
    }
  }
}

TEST(SelectionPublicationAuthorityGuardrailTests,
     FacadesCannotBypassCheckedSelectionPublicationOwner) {
  constexpr std::array<std::string_view, 2> direct_projection_owners{
      "src/editor/modeling/services/modeling_session_service.cpp",
      "src/editor/selection/editor_selection_service.cpp",
  };
  constexpr std::array<std::string_view, 8> raw_scene_selection_owners{
      "src/editor/commands/document/document_command_scene_helpers.hpp",
      "src/editor/commands/document/select_entities_command.hpp",
      "src/editor/commands/document/set_scene_layer_state_command.hpp",
      "src/editor/modeling/services/modeling_sdk_projection_adapter.cpp",
      "src/editor/selection/editor_selection_service.cpp",
      "src/editor/selection/scene_selection_application.cpp",
      "src/editor/selection/scene_selection_application.hpp",
      "src/editor/selection/selection_projection_service.cpp",
  };
  constexpr std::array<std::string_view, 8>
      scalar_compensation_owners{
          "src/api/quader_api_mutation_runner.cpp",
          "src/editor/commands/scene_structural_lifecycle_service.cpp",
          "src/editor/commands/scene_structural_lifecycle_service.hpp",
          "src/editor/instances/project_clipboard_service.cpp",
          "src/editor/modeling/services/modeling_transform_drag_service.cpp",
          "src/editor/selection/checked_selection_publication_command.hpp",
          "src/scene/scene_document.cpp",
          "src/scene/scene_document.hpp",
      };
  const auto allowed = [](std::string_view path, const auto &owners) {
    return std::find(owners.begin(), owners.end(), path) != owners.end();
  };

  for (const std::filesystem::path &path :
       test::guardrail::source_files_under(source_root() / "src")) {
    const std::string relative =
        test::guardrail::normalized_relative(path);
    const std::string source = test::guardrail::read_file(path);
    const bool directly_projects =
        source.find(".project_object_selection_from_scene(") !=
            std::string::npos ||
        source.find("->project_object_selection_from_scene(") !=
            std::string::npos;
    EXPECT_TRUE(!directly_projects ||
                allowed(relative, direct_projection_owners))
        << relative
        << " bypasses EditorSelectionService scene-selection publication";

    const bool directly_applies =
        source.find("apply_scene_selection(") != std::string::npos;
    EXPECT_TRUE(!directly_applies ||
                allowed(relative, raw_scene_selection_owners))
        << relative << " bypasses the selection application primitives";

    const bool directly_compensates_scalars =
        source.find("capture_scalar_compensation_checkpoint(") !=
            std::string::npos ||
        source.find("acknowledge_scalar_compensation_checkpoint(") !=
            std::string::npos ||
        source.find("begin_scalar_compensation_restore(") !=
            std::string::npos ||
        source.find("restore_scalar_compensation_checkpoint(") !=
            std::string::npos ||
        source.find("discard_scalar_compensation_checkpoint(") !=
            std::string::npos;
    EXPECT_TRUE(!directly_compensates_scalars ||
                allowed(relative, scalar_compensation_owners))
        << relative
        << " creates a parallel Scene scalar-compensation policy";
  }

  const std::string api_mutations =
      read_source_file("src/api/quader_api_mutation_runner.cpp");
  const std::string scene_api = read_source_file("src/api/quader_scene_api.cpp");
  const std::string layers_api =
      read_source_file("src/api/quader_layers_api.cpp");
  const std::string document_commands =
      read_source_file("src/editor/commands/document_command_service.cpp");
  const std::string instances =
      read_source_file("src/editor/instances/scene_instance_service.hpp");
  const std::string session =
      read_source_file("src/editor/session/scene_session.cpp");
  const std::string select_command = read_source_file(
      "src/editor/commands/document/select_entities_command.hpp");
  const std::string checked_publication = read_source_file(
      "src/editor/selection/checked_selection_publication_command.hpp");
  const std::string clipboard = read_source_file(
      "src/editor/instances/project_clipboard_service.cpp");
  const std::string structural_lifecycle = read_source_file(
      "src/editor/commands/scene_structural_lifecycle_service.cpp");
  const auto occurrence_count = [](std::string_view source,
                                   std::string_view needle) {
    std::size_t count = 0u;
    std::size_t offset = 0u;
    while ((offset = source.find(needle, offset)) !=
           std::string_view::npos) {
      ++count;
      offset += needle.size();
    }
    return count;
  };

  EXPECT_EQ(api_mutations.find("publish_scene_selection("), std::string::npos);
  EXPECT_EQ(scene_api.find("publish_scene_selection("), std::string::npos);
  EXPECT_EQ(layers_api.find("publish_scene_selection("), std::string::npos);
  EXPECT_NE(document_commands.find("with_checked_selection_publication("),
            std::string::npos);
  EXPECT_NE(instances.find("with_checked_selection_publication("),
            std::string::npos);
  EXPECT_NE(session.find("set_selection_publication("), std::string::npos);
  EXPECT_EQ(select_command.find("publish_scene_selection("),
            std::string::npos);
  EXPECT_EQ(select_command.find("capture_scene_selection_checkpoint("),
            std::string::npos);
  EXPECT_EQ(select_command.find("SceneProjectionCheckpoint"),
            std::string::npos);
  EXPECT_NE(checked_publication.find(
                "capture_scalar_compensation_checkpoint("),
            std::string::npos);
  EXPECT_NE(checked_publication.find(
                "acknowledge_scalar_compensation_checkpoint("),
            std::string::npos);
  EXPECT_NE(checked_publication.find("begin_scalar_compensation_restore("),
            std::string::npos);
  EXPECT_NE(checked_publication.find("same_scene_selection("),
            std::string::npos);
  EXPECT_NE(checked_publication.find(
                "restore_scalar_compensation_checkpoint("),
            std::string::npos);
  EXPECT_GE(occurrence_count(
                clipboard, "acknowledge_scalar_compensation_checkpoint("),
            2u);
  EXPECT_GE(occurrence_count(clipboard,
                             "begin_scalar_compensation_restore("),
            2u);
  EXPECT_NE(api_mutations.find(
                "acknowledge_scalar_compensation_checkpoint("),
            std::string::npos);
  EXPECT_NE(api_mutations.find("begin_scalar_compensation_restore("),
            std::string::npos);
  EXPECT_NE(structural_lifecycle.find(
                "acknowledge_scalar_compensation_checkpoint("),
            std::string::npos);
  EXPECT_NE(structural_lifecycle.find(
                "begin_scalar_compensation_restore("),
            std::string::npos);
}

TEST(ModelingSessionServiceSplitTests,
     ClickPickSelectionUsesSelectionServiceCachedInputs) {
  const std::string facade = read_source_file(
      "src/editor/modeling/services/modeling_session_service.cpp");
  const std::string sdk_adapter = read_source_file(
      "src/editor/modeling/services/modeling_sdk_projection_adapter.cpp");
  const std::string selection_service = read_source_file(
      "src/editor/modeling/services/modeling_session_services.cpp");
  const std::string runtime_picking =
      read_source_file("src/editor/modeling/runtime/session_picking.cpp");
  const std::string runtime_header =
      read_source_file("src/editor/modeling/runtime/session.hpp");

  EXPECT_NE(facade.find("sdk_projection_adapter_.pick_select("),
            std::string::npos);
  EXPECT_NE(facade.find("request, selection_service_"), std::string::npos);
  EXPECT_NE(sdk_adapter.find("selection.apply_pick_selection("),
            std::string::npos);
  EXPECT_NE(sdk_adapter.find(
                "selection.apply_pick_selection(projected, scene_, object_map_,"),
            std::string::npos);
  EXPECT_NE(sdk_adapter.find("effective_request);"), std::string::npos);
  EXPECT_EQ(facade.find("quader_editor_core::pick_select_all_faces(\n          "
                        "          picking_objects"),
            std::string::npos);
  EXPECT_EQ(facade.find("quader_editor_core::pick_select_edge_loop(\n          "
                        "          picking_objects"),
            std::string::npos);
  EXPECT_EQ(facade.find("quader_editor_core::pick_select_all_vertices(\n       "
                        "             picking_objects"),
            std::string::npos);
  EXPECT_NE(
      selection_service.find("ModelingSelectionService::apply_pick_selection("),
      std::string::npos);
  EXPECT_NE(selection_service.find(
                "cached_pick_object_bounds(session, scene, object_registry)"),
            std::string::npos);
  EXPECT_NE(selection_service.find("cached_local_pick_face_geometry("),
            std::string::npos);
  EXPECT_NE(selection_service.find("object_registry, pick_options)"),
            std::string::npos);
  EXPECT_NE(selection_service.find("cached_local_picking_transforms("),
            std::string::npos);
  EXPECT_NE(selection_service.find(".object_bounds = pick_bounds"),
            std::string::npos);
  EXPECT_NE(selection_service.find(".face_geometry = pick_face_geometry"),
            std::string::npos);
  EXPECT_NE(selection_service.find(".object_transforms = object_transforms"),
            std::string::npos);
  EXPECT_NE(selection_service.find("quader_editor_core::apply_pick_selection("),
            std::string::npos);
  EXPECT_EQ(selection_service.find("applyPickedSelectionToSession"),
            std::string::npos);
  EXPECT_EQ(selection_service.find("sessionHasComponentSelection"),
            std::string::npos);
  EXPECT_EQ(selection_service.find("pickSelectRequestHitsTarget"),
            std::string::npos);
  EXPECT_EQ(
      selection_service.find("quader_editor_core::pick_select_all_faces("),
      std::string::npos);
  EXPECT_EQ(
      selection_service.find("quader_editor_core::pick_select_edge_loop("),
      std::string::npos);
  EXPECT_EQ(
      selection_service.find("quader_editor_core::pick_select_all_vertices("),
      std::string::npos);
  EXPECT_NE(runtime_header.find("struct ApplyPickSelectionRequest"),
            std::string::npos);
  EXPECT_NE(runtime_header.find("apply_pick_selection(EditorSession &session"),
            std::string::npos);
  EXPECT_NE(
      runtime_picking.find("ApplyPickSelectionResult apply_pick_selection("),
      std::string::npos);
  EXPECT_NE(runtime_picking.find("apply_picked_selection_to_projection("),
            std::string::npos);
  EXPECT_NE(
      runtime_header.find("std::span<const PickObjectBounds> object_bounds"),
      std::string::npos);
  EXPECT_NE(runtime_header.find(
                "std::span<const PickObjectTransform> object_transforms"),
            std::string::npos);
  EXPECT_NE(runtime_picking.find("request.object_bounds"), std::string::npos);
  EXPECT_NE(runtime_picking.find("request.face_geometry"), std::string::npos);
  EXPECT_NE(runtime_picking.find("request.object_transforms"),
            std::string::npos);
}

TEST(ModelingSessionServiceSplitTests,
     SelectionApplicationLivesInModelingSelectionService) {
  const std::string facade = read_source_file(
      "src/editor/modeling/services/modeling_session_service.cpp");
  const std::string sdk_adapter = read_source_file(
      "src/editor/modeling/services/modeling_sdk_projection_adapter.cpp");
  const std::string selection_header = read_source_file(
      "src/editor/modeling/services/modeling_selection_service.hpp");
  const std::string selection_service = read_source_file(
      "src/editor/modeling/services/modeling_session_services.cpp");
  const std::string rectangle_resolution = source_slice(
      selection_service,
      "ModelingSelectionService::apply_resolved_rectangle_selection(",
      "ModelingSelectionService::set_group_selection_mode(");

  EXPECT_NE(selection_header.find("struct ModelingSelectionApplyResult"),
            std::string::npos);
  EXPECT_NE(selection_header.find("selection_summary("), std::string::npos);
  EXPECT_NE(selection_header.find("set_component_selection("),
            std::string::npos);
  EXPECT_NE(selection_header.find("set_group_selection_mode("),
            std::string::npos);

  EXPECT_NE(
      selection_service.find("ModelingSelectionService::selection_summary("),
      std::string::npos);
  EXPECT_NE(selection_service.find(
                "ModelingSelectionService::set_component_selection("),
            std::string::npos);
  EXPECT_NE(selection_service.find("validated_component_selection_edit("),
            std::string::npos);
  EXPECT_NE(selection_service.find(
                "ModelingSelectionService::apply_rectangle_selection("),
            std::string::npos);
  ASSERT_FALSE(rectangle_resolution.empty());
  const std::size_t operation_owner =
      rectangle_resolution.find("to_runtime_selection_edit(edit)");
  const std::size_t resolution_counter = rectangle_resolution.find(
      "++rectangle_selection_resolution_build_count_");
  const std::size_t request_delegate = rectangle_resolution.find(
      "return apply_resolved_rectangle_selection(");
  EXPECT_NE(operation_owner, std::string::npos);
  EXPECT_NE(resolution_counter, std::string::npos);
  EXPECT_NE(request_delegate, std::string::npos);
  EXPECT_LT(operation_owner, request_delegate);
  EXPECT_LT(resolution_counter, request_delegate);
  EXPECT_NE(rectangle_resolution.find(
                "rectangle_selection_hits(session, scene, object_registry, "
                "request)"),
            std::string::npos);
  EXPECT_EQ(rectangle_resolution.find(
                "to_runtime_selection_edit(request.edit)"),
            std::string::npos);

  EXPECT_NE(facade.find("selection_service_.selection_summary("),
            std::string::npos);
  EXPECT_NE(facade.find("sdk_projection_adapter_.set_component_selections("),
            std::string::npos);
  EXPECT_NE(facade.find("sdk_projection_adapter_.box_select("),
            std::string::npos);
  EXPECT_EQ(sdk_adapter.find("set_component_selection_without_preview("),
            std::string::npos);
  EXPECT_NE(sdk_adapter.find("document.apply_selection("), std::string::npos);
  EXPECT_NE(sdk_adapter.find("session_.commit_component_selection_batch("),
            std::string::npos);
  EXPECT_NE(sdk_adapter.find("selection.apply_rectangle_selection("),
            std::string::npos);
  EXPECT_NE(
      facade.find("selection_projection_service_.restore_scene_selection("),
      std::string::npos);
  EXPECT_EQ(facade.find("validated_component_selection_edit("),
            std::string::npos);
  EXPECT_EQ(facade.find("ComponentSelectionEditResult"), std::string::npos);
  EXPECT_EQ(facade.find("quader_editor_core::apply_rectangle_selection("
                        "rectangle_request)"),
            std::string::npos);
  EXPECT_EQ(facade.find("scene_.clear_selection()"), std::string::npos);
}

TEST(ModelingSessionServiceSplitTests,
     SelectionCenterWorldProjectionLivesInSelectionProjectionService) {
  const std::string facade = read_source_file(
      "src/editor/modeling/services/modeling_session_service.cpp");
  const std::string projection_header =
      read_source_file("src/editor/selection/selection_projection_service.hpp");
  const std::string projection =
      read_source_file("src/editor/selection/selection_projection_service.cpp");

  EXPECT_EQ(
      facade.find("selection_projection_service_.world_selection_center("),
      std::string::npos);
  EXPECT_NE(
      facade.find("selection_projection_service_.world_selection_bounds("),
      std::string::npos);
  EXPECT_NE(facade.find("sdk_projection_adapter_.selection_bounds()"),
            std::string::npos);
  EXPECT_NE(facade.find("projected, scene_, assets_, object_registry_)"),
            std::string::npos);
  EXPECT_EQ(facade.find("selected_world_bounds(scene_, assets_)"),
            std::string::npos);
  EXPECT_EQ(facade.find("quader_poly::selected_vertex_ids(object.document, "
                        "object.selection)"),
            std::string::npos);
  EXPECT_EQ(facade.find("transform_object_point("), std::string::npos);
  EXPECT_NE(projection_header.find("world_selection_center("),
            std::string::npos);
  EXPECT_NE(
      projection.find("SelectionProjectionService::world_selection_center("),
      std::string::npos);
  EXPECT_NE(projection.find("selected_world_bounds(scene, assets)"),
            std::string::npos);
  EXPECT_NE(projection.find("quader_poly::selected_vertex_ids(object.document, "
                            "object.selection)"),
            std::string::npos);
  EXPECT_NE(
      projection.find("vertices_by_id.reserve(object.document.vertices.size())"),
      std::string::npos);
  EXPECT_NE(
      projection.find("world_from_object = "
                      "scene.world_transform_matrix(*entity)"),
      std::string::npos);
  EXPECT_NE(projection.find("transform_point(*world_from_object, "
                            "vertex->second->position)"),
            std::string::npos);
  EXPECT_EQ(projection.find("world_point_for_modeling_object("),
            std::string::npos);
}

TEST(ModelingSessionServiceSplitTests,
     DuplicateSceneIdentityLivesInRuntimeMapAndMeshSync) {
  const std::string facade = read_source_file(
      "src/editor/modeling/services/modeling_session_service.cpp");
  const std::string runtime_map = read_source_file(
      "src/editor/modeling/runtime/modeling_object_binding_registry.cpp");
  const std::string mesh_sync = read_source_file(
      "src/editor/modeling/services/modeling_mesh_sync_service.cpp");

  EXPECT_EQ(facade.find("DuplicateSceneState"), std::string::npos);
  EXPECT_EQ(facade.find("scene_.set_transform(*duplicate"), std::string::npos);
  EXPECT_EQ(facade.find("scene_.set_material(*duplicate"), std::string::npos);
  EXPECT_NE(facade.find("capture_selected_duplicate_scene_identities()"),
            std::string::npos);
  EXPECT_NE(facade.find(
                "mesh_sync_service_.apply_scene_identity_to_duplicate_entity("),
            std::string::npos);
  EXPECT_NE(runtime_map.find("capture_scene_identities_for_modeling_objects("),
            std::string::npos);
  EXPECT_NE(runtime_map.find("object_entity_bindings_by_modeling_object("),
            std::string::npos);
  EXPECT_NE(runtime_map.find("std::sort("), std::string::npos);
  EXPECT_NE(runtime_map.find("bindings.begin(), bindings.end()"),
            std::string::npos);
  EXPECT_NE(mesh_sync.find("apply_scene_identity_to_duplicate_entity("),
            std::string::npos);
  EXPECT_NE(mesh_sync.find(
                "ModelingObjectSceneIdentity duplicate_identity = identity"),
            std::string::npos);
  EXPECT_NE(mesh_sync.find("apply_scene_entity_state("), std::string::npos);
  EXPECT_EQ(mesh_sync.find("object_entity_bindings_by_modeling_object()"),
            std::string::npos);
  EXPECT_EQ(
      mesh_sync.find(
          "std::sort(entities_by_object.begin(), entities_by_object.end()"),
      std::string::npos);
}

TEST(ModelingSessionServiceSplitTests,
     SelectedObjectDuplicationUsesOneSdkBatchTransition) {
  const std::string adapter = read_source_file(
      "src/editor/modeling/services/modeling_sdk_projection_adapter.cpp");
  const std::string duplicate_selected = source_slice(
      adapter, "duplicate_selected_objects(QVec3 geometry_offset,",
      "mirror_selected_objects_across_plane(");

  ASSERT_FALSE(duplicate_selected.empty());
  EXPECT_NE(duplicate_selected.find("session_.duplicate_objects("),
            std::string::npos);
  EXPECT_EQ(duplicate_selected.find("session_.duplicate_object("),
            std::string::npos);
  EXPECT_NE(duplicate_selected.find("session_.squash_undo_since(undo_mark)"),
            std::string::npos);
}

TEST(ModelingSessionServiceSplitTests,
     TransformDragPreviewRoutingLivesInController) {
  const std::string facade = read_source_file(
      "src/editor/modeling/services/modeling_session_service.cpp");
  const std::string service = read_source_file(
      "src/editor/modeling/services/modeling_transform_drag_service.cpp");
  const std::string controller = read_source_file(
      "src/editor/modeling/runtime_state/transform_drag_controller.cpp");

  EXPECT_EQ(facade.find("TransformDragController::preview_routing("),
            std::string::npos);
  EXPECT_NE(service.find("TransformDragController::preview_routing("),
            std::string::npos);
  EXPECT_EQ(facade.find("duplicate_scene_transform_preview"),
            std::string::npos);
  EXPECT_EQ(facade.find("needs_runtime_preview_transaction ="),
            std::string::npos);
  EXPECT_NE(controller.find("TransformDragController::preview_routing("),
            std::string::npos);
  EXPECT_NE(controller.find("scene_transform_preview_eligible"),
            std::string::npos);
  EXPECT_FALSE(std::filesystem::exists(
      source_root() /
      "src/editor/modeling/services/transform_drag_controller.hpp"));
  EXPECT_FALSE(std::filesystem::exists(
      source_root() /
      "src/editor/modeling/services/transform_preview_service.hpp"));
  EXPECT_FALSE(std::filesystem::exists(
      source_root() / "src/editor/modeling/services/modal_tool_runtime.hpp"));
}

TEST(ModelingSessionServiceSplitTests,
     TransformDragLifecycleLivesInFocusedService) {
  const std::string facade = read_source_file(
      "src/editor/modeling/services/modeling_session_service.cpp");
  const std::string service = read_source_file(
      "src/editor/modeling/services/modeling_transform_drag_service.cpp");
  const std::string service_header = read_source_file(
      "src/editor/modeling/services/modeling_transform_drag_service.hpp");
  const std::string transform_tool =
      read_source_file("src/editor/tools/base/transform_viewport_tool.hpp");

  EXPECT_NE(facade.find("transform_drag_service_.begin_transform_drag("),
            std::string::npos);
  EXPECT_NE(facade.find("transform_drag_service_.update_transform_drag("),
            std::string::npos);
  EXPECT_NE(facade.find("transform_drag_service_.commit_transform_drag("),
            std::string::npos);
  EXPECT_NE(facade.find("transform_drag_service_.cancel_transform_drag("),
            std::string::npos);
  EXPECT_EQ(facade.find("quader_editor_core::begin_move_drag("),
            std::string::npos);
  const std::size_t delegated_begin =
      facade.find("transform_drag_service_.begin_transform_drag(");
  const std::size_t delegated_cancel =
      facade.find("transform_drag_service_.cancel_transform_drag(",
                  delegated_begin);
  ASSERT_NE(delegated_begin, std::string::npos);
  ASSERT_NE(delegated_cancel, std::string::npos);
  EXPECT_EQ(facade.substr(delegated_begin,
                          delegated_cancel - delegated_begin)
                .find("SceneTransformDragService"),
            std::string::npos);
  EXPECT_NE(service.find("quader_editor_core::begin_move_drag("),
            std::string::npos);
  EXPECT_NE(service.find("quader_editor_core::begin_rotate_drag("),
            std::string::npos);
  EXPECT_NE(service.find("quader_editor_core::begin_scale_drag("),
            std::string::npos);
  EXPECT_NE(service_header.find("SceneTransformDragService"),
            std::string::npos);
  EXPECT_NE(service_header.find("SceneScalarCompensationCheckpoint"),
            std::string::npos);
  EXPECT_NE(service.find("capture_scalar_compensation_checkpoint()"),
            std::string::npos);
  EXPECT_NE(service.find("same_scene_selection("), std::string::npos);
  EXPECT_NE(transform_tool.find("TransformGizmoInteraction owns"),
            std::string::npos);
}

TEST(ModelingSessionServiceSplitTests,
     PickingAndOverlayProjectionHelpersStayOutOfFacade) {
  const std::string facade = read_source_file(
      "src/editor/modeling/services/modeling_session_service.cpp");
  const std::string services = read_source_file(
      "src/editor/modeling/services/modeling_session_services.cpp");
  const std::string picking_projection =
      read_source_file("src/editor/projection/picking_cache_projection.cpp");

  EXPECT_EQ(facade.find("objectPickFaceGeometrySignature("), std::string::npos);
  EXPECT_EQ(facade.find("mix_pick_geometry_hash("), std::string::npos);
  EXPECT_EQ(facade.find("build_object_pick_bounds("), std::string::npos);
  EXPECT_EQ(facade.find("include_pick_bounds_point("), std::string::npos);
  EXPECT_EQ(facade.find("append_selected_hover_filtered_base_frame("),
            std::string::npos);
  EXPECT_EQ(facade.find("hover_frame_suppresses_triangle("), std::string::npos);
  EXPECT_EQ(facade.find("hover_frame_suppresses_line("), std::string::npos);
  EXPECT_EQ(facade.find("hover_frame_suppresses_point("), std::string::npos);
  EXPECT_EQ(facade.find("append_overlay_frame("), std::string::npos);

  EXPECT_NE(services.find("append_selected_hover_filtered_base_frame("),
            std::string::npos);
  EXPECT_NE(services.find("append_overlay_frame("), std::string::npos);
  EXPECT_NE(services.find("PickingCacheProjection::build_object_pick_bounds("),
            std::string::npos);
  EXPECT_NE(
      services.find("PickingCacheProjection::object_face_geometry_signature("),
      std::string::npos);
  EXPECT_NE(picking_projection.find("object_face_geometry_signature("),
            std::string::npos);
  EXPECT_NE(picking_projection.find("build_object_pick_bounds("),
            std::string::npos);
}

TEST(ModelingSessionServiceSplitTests,
     ToolStatusProjectionLivesInOverlayService) {
  const std::string facade = read_source_file(
      "src/editor/modeling/services/modeling_session_service.cpp");
  const std::string services = read_source_file(
      "src/editor/modeling/services/modeling_session_services.cpp");
  const std::string overlay_header = read_source_file(
      "src/editor/modeling/services/modeling_overlay_service.hpp");
  const std::string tool_status = source_slice(
      facade, "[[nodiscard]] ModelingToolStatus tool_status() const",
      "[[nodiscard]] ModelingLiveStatus live_status() const");

  ASSERT_FALSE(tool_status.empty());
  EXPECT_NE(overlay_header.find("ModelingToolStatus tool_status("),
            std::string::npos);
  EXPECT_NE(services.find("ModelingOverlayService::tool_status("),
            std::string::npos);
  EXPECT_NE(facade.find("return overlay_service_.tool_status("
                        "runtime_, modal_tool_owns_viewport());"),
            std::string::npos);
  EXPECT_EQ(tool_status.find("runtime_.cut_state()"), std::string::npos);
  EXPECT_EQ(tool_status.find("runtime_.mirror_state()"), std::string::npos);
  EXPECT_EQ(tool_status.find("quader_editor_core::cut_overlay_points("),
            std::string::npos);
  EXPECT_EQ(tool_status.find("quader_editor_core::mirror_overlay_points("),
            std::string::npos);
  EXPECT_EQ(tool_status.find("quader_editor_core::cut_is_dragging("),
            std::string::npos);
  EXPECT_EQ(tool_status.find("quader_editor_core::mirror_is_dragging("),
            std::string::npos);
  EXPECT_NE(services.find("quader_editor_core::cut_overlay_points("),
            std::string::npos);
  EXPECT_NE(services.find("quader_editor_core::mirror_overlay_points("),
            std::string::npos);
}

TEST(ModelingSessionServiceSplitTests,
     HierarchyDuplicatePolicyLivesInFocusedService) {
  const std::string facade = read_source_file(
      "src/editor/modeling/services/modeling_session_service.cpp");
  const std::string service = read_source_file(
      "src/editor/modeling/services/modeling_hierarchy_duplicate_service.cpp");
  const std::string document_helpers = read_source_file(
      "src/editor/commands/document/document_command_scene_helpers.hpp");
  const std::string duplicate_command = read_source_file(
      "src/editor/commands/document/duplicate_selected_hierarchy_command.hpp");
  const std::string structural_lifecycle = read_source_file(
      "src/editor/commands/scene_structural_lifecycle_service.cpp");
  const std::string naming_header =
      read_source_file("src/scene/scene_entity_naming.hpp");
  const std::string cmake = read_source_file("CMakeLists.txt");

  EXPECT_NE(cmake.find("src/editor/modeling/services/"
                       "modeling_hierarchy_duplicate_service.cpp"),
            std::string::npos);
  EXPECT_NE(cmake.find("src/editor/commands/"
                       "scene_structural_lifecycle_service.cpp"),
            std::string::npos);
  EXPECT_NE(
      facade.find(
          "hierarchy_duplicate_service_.duplicate_entity_hierarchy("),
      std::string::npos);
  EXPECT_EQ(facade.find("duplicate_selected_hierarchy_for_group_drag("),
            std::string::npos);
  EXPECT_EQ(facade.find("duplicate_selected_entity_hierarchy("),
            std::string::npos);
  EXPECT_EQ(service.find("duplicate_selected_entity_hierarchy("),
            std::string::npos);
  EXPECT_EQ(
      facade.find("std::unordered_map<EntityId, EntityId> clone_by_source"),
      std::string::npos);
  EXPECT_EQ(facade.find("auto copyCommonEntityState"), std::string::npos);
  EXPECT_NE(service.find("pair_duplicate_meshes("), std::string::npos);
  EXPECT_NE(service.find("duplicate_hierarchy_with_existing_clones("),
            std::string::npos);
  EXPECT_NE(structural_lifecycle.find("modeling.duplicate_entity_hierarchy("),
            std::string::npos);
  EXPECT_NE(structural_lifecycle.find(
                "duplicate_hierarchy_in_transaction("),
            std::string::npos);
  EXPECT_EQ(service.find("copyCommonEntityState("), std::string::npos);
  EXPECT_EQ(service.find("cloneHierarchyEntity("), std::string::npos);
  EXPECT_NE(
      document_helpers.find("document_command_duplicate_entity_snapshot_tree("),
      std::string::npos);
  EXPECT_NE(document_helpers.find("SceneEntityNameAllocator *name_allocator"),
            std::string::npos);
  EXPECT_NE(duplicate_command.find(
                "SceneEntityNameAllocator name_allocator(scene())"),
            std::string::npos);
  EXPECT_NE(duplicate_command.find("&name_allocator"), std::string::npos);
  EXPECT_NE(naming_header.find("next_index_by_base_"), std::string::npos);
}

TEST(ModelingSessionServiceSplitTests,
     SceneAwareDeletePolicyLivesInFocusedCommand) {
  const std::string facade = read_source_file(
      "src/editor/modeling/services/modeling_session_service.cpp");
  const std::string command = read_source_file(
      "src/editor/commands/modeling/scene_aware_delete_selection_command.cpp");
  const std::string command_header = read_source_file(
      "src/editor/commands/modeling/scene_aware_delete_selection_command.hpp");
  const std::string cmake = read_source_file("CMakeLists.txt");

  EXPECT_NE(cmake.find("src/editor/commands/modeling/"
                       "scene_aware_delete_selection_command.cpp"),
            std::string::npos);
  EXPECT_NE(facade.find("create_scene_aware_delete_selection_command("),
            std::string::npos);
  EXPECT_NE(facade.find("SceneAwareDeleteRuntime{"), std::string::npos);
  EXPECT_NE(facade.find(".entity_for_modeling_object ="), std::string::npos);
  EXPECT_EQ(facade.find("struct SceneAwareDeleteState"), std::string::npos);
  EXPECT_EQ(facade.find("class SceneAwareDeleteSelectionCommand"),
            std::string::npos);
  EXPECT_EQ(facade.find("capture_scene_aware_delete_state"), std::string::npos);
  EXPECT_EQ(facade.find("redo_scene_aware_delete"), std::string::npos);
  EXPECT_EQ(facade.find("undo_scene_aware_delete"), std::string::npos);
  EXPECT_NE(command_header.find("entity_for_modeling_object"),
            std::string::npos);
  EXPECT_EQ(command.find("for (const EntityId entity : scene.entities())"),
            std::string::npos);
  EXPECT_NE(command.find(
                "std::vector<EntityId> destroyed = state_.current_roots"),
            std::string::npos);
  EXPECT_NE(command.find("state_.current_definition_roots"),
            std::string::npos);
  EXPECT_NE(command.find("runtime_.destroy_scene_entities"),
            std::string::npos);
  EXPECT_EQ(command.find("scene_.destroy_entity(root)"), std::string::npos);
  EXPECT_NE(command.find("runtime.entity_for_modeling_object(snapshot.modeling_"
                         "object->object_id)"),
            std::string::npos);
  EXPECT_NE(command.find("struct SceneAwareDeleteState"), std::string::npos);
  EXPECT_NE(command.find("class SceneAwareDeleteSelectionCommand"),
            std::string::npos);
  EXPECT_NE(command.find("capture_scene_aware_delete_state"),
            std::string::npos);
  EXPECT_NE(command.find("redo_scene_aware_delete"), std::string::npos);
  EXPECT_NE(command.find("undo_scene_aware_delete"), std::string::npos);
}

TEST(ModelingSessionServiceSplitTests,
     InstantOperationAvailabilityUsesActionCatalogEvaluator) {
  const std::string instant_header = read_source_file(
      "src/editor/modeling/services/modeling_instant_operation_catalog.hpp");
  const std::string instant_catalog = read_source_file(
      "src/editor/modeling/services/modeling_instant_operation_catalog.cpp");
  const std::string action_catalog = read_source_file(
      "src/editor/modeling/services/modeling_action_catalog.cpp");

  EXPECT_EQ(instant_header.find("ModelingInstantOperationEnablement"),
            std::string::npos);
  EXPECT_EQ(instant_catalog.find("switch (entry"), std::string::npos);
  EXPECT_EQ(instant_catalog.find("HostActionDescriptor"), std::string::npos);
  EXPECT_NE(instant_catalog.find(
                "ModelingActionCatalog::evaluate_operation_availability("),
            std::string::npos);
  EXPECT_NE(action_catalog.find("evaluate_host_operation_availability("),
            std::string::npos);
  EXPECT_NE(action_catalog.find("descriptor.availability.selection_mode"),
            std::string::npos);
  EXPECT_NE(action_catalog.find("evaluate_availability_rule("),
            std::string::npos);
}

TEST(ModelingSessionServiceSplitTests,
     InstantOperationSettingsMappingHasSingleOwner) {
  const std::string catalog_header = read_source_file(
      "src/editor/modeling/services/modeling_instant_operation_catalog.hpp");
  const std::string catalog = read_source_file(
      "src/editor/modeling/services/modeling_instant_operation_catalog.cpp");
  const std::string services = read_source_file(
      "src/editor/modeling/services/modeling_session_services.cpp");
  const std::string operation_settings = read_source_file(
      "src/editor/modeling/services/modeling_operation_settings.cpp");
  const std::string tools_api =
      read_source_file("src/api/quader_tools_api.cpp");

  EXPECT_EQ(catalog_header.find("RuntimeOperationSettingsKind"),
            std::string::npos);
  EXPECT_EQ(catalog.find("RuntimeOperationSettingsKind"), std::string::npos);
  EXPECT_EQ(catalog.find("settingsKind"), std::string::npos);
  EXPECT_EQ(services.find("settingsKind"), std::string::npos);
  EXPECT_NE(catalog.find("settings_for_operation(command.operation_id, "
                         "interaction_settings)"),
            std::string::npos);
  EXPECT_EQ(services.find("settings_for_operation(*entry->runtime_operation"),
            std::string::npos);
  EXPECT_NE(operation_settings.find("command_schema_for_operation(operation)"),
            std::string::npos);
  EXPECT_NE(
      operation_settings.find("operation_option_for_setting(tool_id, name)"),
      std::string::npos);
  EXPECT_NE(operation_settings.find("schema->settings_kind"),
            std::string::npos);
  EXPECT_EQ(operation_settings.find("bridge_operation_schema()"),
            std::string::npos);
  EXPECT_EQ(operation_settings.find("edge_bevel_operation_schema()"),
            std::string::npos);
  EXPECT_EQ(operation_settings.find("matchesAny("), std::string::npos);
  EXPECT_EQ(operation_settings.find("\"edge_bevel_width\""), std::string::npos);
  EXPECT_EQ(operation_settings.find("\"distance\""), std::string::npos);
  EXPECT_EQ(operation_settings.find(
                "case quader_editor_core::EditorOperationId::ConnectVertices"),
            std::string::npos);
  EXPECT_EQ(operation_settings.find(
                "case quader_editor_core::EditorOperationId::DeleteSelection"),
            std::string::npos);
  EXPECT_NE(
      tools_api.find(
          "apply_modeling_interaction_setting(settings, tool_id, name, value)"),
      std::string::npos);
  EXPECT_EQ(tools_api.find("edge_bevel_width = value"), std::string::npos);
  EXPECT_EQ(tools_api.find("merge_by_distance_tolerance = value"),
            std::string::npos);
}

TEST(ModelingSessionServiceSplitTests,
     HostOperationMetadataUsesSchemaOptionDescriptorCatalog) {
  const std::string operation_schema =
      read_source_file("src/editor/modeling/operations/operation_schema.hpp");
  const std::string host_contract =
      read_source_file("src/editor/metadata/host_contract.cpp");

  EXPECT_NE(operation_schema.find("struct EditorOperationOptionDescriptor"),
            std::string::npos);
  EXPECT_NE(operation_schema.find("operation_option_descriptor_catalog()"),
            std::string::npos);
  EXPECT_NE(operation_schema.find("operation_option_for_setting("),
            std::string::npos);
  EXPECT_NE(operation_schema.find("\"edge_bevel_width\""), std::string::npos);
  EXPECT_NE(operation_schema.find("\"distance\""), std::string::npos);
  EXPECT_NE(host_contract.find("operation_option_descriptor_catalog()"),
            std::string::npos);
  EXPECT_NE(host_contract.find("operation_preference("), std::string::npos);
  EXPECT_EQ(host_contract.find("static const std::array<HostOptionDescriptor"),
            std::string::npos);
  EXPECT_EQ(host_contract.find("operation_int_preference("), std::string::npos);
  EXPECT_EQ(host_contract.find("operation_float_preference("),
            std::string::npos);
  EXPECT_EQ(host_contract.find("operation_bool_preference("),
            std::string::npos);
}

TEST(ModelingContractHeaderGuardrailTests,
     GranularSdkHeadersOwnTheirPublicDeclarations) {
  /// Pairs one granular SDK header with the declaration it must own.
  struct HeaderContract final {
    std::string_view path;
    std::string_view declaration;
  };
  constexpr std::array<HeaderContract, 4> kContracts{{
      {"src/sdk/core/modeling/include/quader/modeling/api/mesh_handle.hpp",
       "class MeshHandle"},
      {"src/sdk/core/modeling/include/quader/modeling/api/selection.hpp",
       "class SelectionApi"},
      {"src/sdk/core/modeling/include/quader/modeling/api/previews.hpp",
       "class PreviewApi"},
      {"src/sdk/core/modeling/include/quader/modeling/api/tools.hpp",
       "class ToolsApi"},
  }};

  for (const HeaderContract &contract : kContracts) {
    SCOPED_TRACE(contract.path);
    const std::string header = read_source_file(contract.path);
    EXPECT_NE(header.find(contract.declaration), std::string::npos);
    EXPECT_EQ(header.find("api/modeling_api.hpp"), std::string::npos);
  }

  const std::string monolith = read_source_file(
      "src/sdk/core/modeling/include/quader/modeling/api/modeling_api.hpp");
  EXPECT_EQ(monolith.find("class MeshHandle {"), std::string::npos);
  EXPECT_EQ(monolith.find("class SelectionApi {"), std::string::npos);
  EXPECT_EQ(monolith.find("class PreviewApi {"), std::string::npos);
  EXPECT_EQ(monolith.find("class ToolsApi {"), std::string::npos);
}

TEST(ModelingContractHeaderGuardrailTests,
     NativeConsumersUseNarrowResultPreviewAndSelectionContracts) {
  const std::string command_tool =
      read_source_file("src/editor/commands/command_tool.hpp");
  const std::string executor = read_source_file(
      "src/editor/modeling/services/modeling_command_executor.hpp");
  const std::string cut_projection = read_source_file(
      "src/editor/projection/cut_preview_overlay_projection.hpp");
  const std::string selection_projection = read_source_file(
      "src/editor/projection/selection_overlay_projection.hpp");
  const std::string session =
      read_source_file("src/editor/modeling/runtime/session.hpp");

  EXPECT_NE(command_tool.find("modeling_result_types.hpp"), std::string::npos);
  EXPECT_NE(executor.find("modeling_result_types.hpp"), std::string::npos);
  EXPECT_NE(cut_projection.find("modeling_preview_types.hpp"),
            std::string::npos);
  EXPECT_NE(selection_projection.find("runtime/selection_types.hpp"),
            std::string::npos);
  EXPECT_NE(session.find("runtime/selection_types.hpp"), std::string::npos);
  EXPECT_EQ(command_tool.find("modeling_types.hpp"), std::string::npos);
  EXPECT_EQ(executor.find("modeling_types.hpp"), std::string::npos);
  EXPECT_EQ(cut_projection.find("modeling_types.hpp"), std::string::npos);
}

TEST(CommandAndKeybindingOwnershipGuardrailTests,
     RegistryExecutionAndKeybindingCatalogHaveSeparateOwners) {
  const std::string commands_api =
      read_source_file("src/api/quader_commands_api.cpp");
  const std::string target_catalog = read_source_file(
      "src/editor/input/quader_keybinding_target_catalog.cpp");
  const std::string target_catalog_header = read_source_file(
      "src/editor/input/quader_keybinding_target_catalog.hpp");

  EXPECT_NE(commands_api.find("EditorCommandRegistry registry_"),
            std::string::npos);
  EXPECT_NE(commands_api.find("QuaderKeybindingService keybindings_"),
            std::string::npos);
  EXPECT_NE(commands_api.find(
                "QuaderKeybindingTargetCatalog keybinding_target_catalog_"),
            std::string::npos);
  EXPECT_NE(target_catalog_header.find(
                "class QuaderKeybindingTargetCatalog final"),
            std::string::npos);
  EXPECT_NE(target_catalog.find("scope_for_context("), std::string::npos);
  EXPECT_NE(target_catalog.find("selection_mode_when_clause("),
            std::string::npos);
  EXPECT_EQ(target_catalog.find("EditorCommandRegistry"), std::string::npos);
  EXPECT_EQ(target_catalog.find("QuaderKeybindingService"), std::string::npos);
}

TEST(ConversionMappingGuardrailTests,
     NativeAsciiNormalizationUsesQStrOwner) {
  constexpr std::array<std::string_view, 7> kConsumers{{
      "src/io/project/project_manifest.cpp",
      "src/io/project/project_material_catalog.cpp",
      "src/io/project/qmat_codec.cpp",
      "src/io/mesh/obj_writer.cpp",
      "src/editor/material/project_image_decoder.cpp",
      "src/editor/material/standard_material_edit_session_service.cpp",
      "src/editor/session/project_session.cpp",
  }};

  for (const std::string_view path : kConsumers) {
    SCOPED_TRACE(path);
    const std::string source = read_source_file(path);
    EXPECT_NE(source.find("QStr::to_ascii_lower("), std::string::npos);
    EXPECT_EQ(source.find("std::tolower("), std::string::npos);
  }
}

TEST(ConversionMappingGuardrailTests,
     NativeModelingCommandResultsUseOneNamedAdapter) {
  const std::string adapter =
      read_source_file("src/editor/modeling/modeling_result_adapters.hpp");
  const std::string box_commit = read_source_file(
      "src/editor/commands/modeling/commit_box_preview_command.cpp");
  const std::string transform_drag = read_source_file(
      "src/editor/modeling/services/modeling_transform_drag_service.cpp");

  EXPECT_NE(adapter.find("command_result_from_modeling("), std::string::npos);
  EXPECT_NE(box_commit.find("command_result_from_modeling("),
            std::string::npos);
  EXPECT_NE(transform_drag.find("command_result_from_modeling("),
            std::string::npos);
  EXPECT_EQ(box_commit.find("CommandResult command_result("),
            std::string::npos);
  EXPECT_EQ(transform_drag.find("CommandResult command_result("),
            std::string::npos);
}

TEST(CurveControlSelectionPolicyGuardrailTests,
     CurveControlsReuseCanonicalSelectionPolicyOwners) {
  const std::string selection_edit =
      read_source_file("src/editor/interactions/selection_edit.hpp");
  const std::string viewport_selection = read_source_file(
      "src/editor/viewport/viewport_selection_interaction.cpp");
  const std::string curve_selection = source_slice(
      viewport_selection,
      "void ViewportSelectionInteraction::handle_frame(",
      "QuaderSelectionSummary ViewportSelectionInteraction::pick_context_menu_target(");
  const std::string native_curve_input = read_source_file(
      "src/editor/tools/viewport_native_tool_interaction.cpp");
  const std::string transform_gizmo = read_source_file(
      "src/editor/viewport/transform_gizmo_interaction.cpp");
  const std::string curve_service =
      read_source_file("src/editor/curve/curve_tool_service.cpp");
  const std::string curve_service_header =
      read_source_file("src/editor/curve/curve_tool_service.hpp");
  const std::string selection_policy = read_source_file(
      "src/editor/modeling/runtime/selection_policy.cpp");
  const std::string selection_owner = read_source_file(
      "src/editor/selection/editor_selection_service.cpp");

  ASSERT_FALSE(curve_selection.empty());
  EXPECT_NE(selection_edit.find("apply_selection_edit_operation("),
            std::string::npos);
  EXPECT_NE(selection_edit.find("resolve_selection_edit_operation("),
            std::string::npos);
  EXPECT_NE(viewport_selection.find(
                "#include \"editor/interactions/selection_edit.hpp\""),
            std::string::npos);
  EXPECT_NE(
      curve_selection.find("selection_edit_operation_from_modifiers("),
      std::string::npos);
  EXPECT_NE(curve_selection.find("begin_selection_box("), std::string::npos);
  EXPECT_NE(curve_selection.find("update_selection_box("), std::string::npos);
  EXPECT_NE(curve_selection.find("finish_selection_box("), std::string::npos);
  EXPECT_EQ(curve_selection.find("curve->edit_control_selection("),
            std::string::npos);
  EXPECT_NE(viewport_selection.find("selection.apply_resolved_click({"),
            std::string::npos);
  EXPECT_NE(viewport_selection.find("selection.apply_resolved_marquee({"),
            std::string::npos);
  EXPECT_NE(selection_owner.find("sub_selection_provider_->edit_sub_elements("),
            std::string::npos);
  EXPECT_NE(curve_selection.find("commit_pick_selection("), std::string::npos)
      << "A Curve-control miss must fall through the canonical scene pick.";
  EXPECT_NE(curve_selection.find("curve->active() || can_use_selection_fallback"),
            std::string::npos);
  EXPECT_NE(transform_gizmo.find(
                "frame.active_tool_allows_selection_fallback"),
            std::string::npos);
  EXPECT_NE(transform_gizmo.find("!frame.input.modal_tool_active"),
            std::string::npos)
      << "Passive Curve controls must yield to tools that suspend canonical "
         "selection fallback.";

  EXPECT_EQ(viewport_selection.find("selection_edit_from_modifiers("),
            std::string::npos)
      << "Viewport selection must not duplicate the canonical modifier map.";
  EXPECT_EQ(curve_selection.find("if (frame.input.modifiers.primary"),
            std::string::npos);
  EXPECT_EQ(curve_selection.find("if (frame.input.modifiers.shift"),
            std::string::npos);
  EXPECT_EQ(curve_selection.find("frame.input.modifiers.primary ?"),
            std::string::npos);
  EXPECT_EQ(curve_selection.find("frame.input.modifiers.shift ?"),
            std::string::npos);

  EXPECT_NE(curve_service_header.find(
                "quader_editor_core::SelectionEditOperation operation"),
            std::string::npos);
  EXPECT_NE(curve_service.find("resolve_selection_edit_operation("),
            std::string::npos);
  EXPECT_EQ(curve_service_header.find("CurveControlSelectionEdit"),
            std::string::npos);
  EXPECT_EQ(curve_service_header.find("CurveSelectionEditOperation"),
            std::string::npos);
  EXPECT_EQ(curve_service.find("CurveControlSelectionEdit"),
            std::string::npos);
  EXPECT_EQ(curve_service.find("CurveSelectionEditOperation"),
            std::string::npos);
  EXPECT_EQ(curve_service.find("modifiers.primary"), std::string::npos);
  EXPECT_EQ(curve_service.find("modifiers.shift"), std::string::npos);

  EXPECT_EQ(native_curve_input.find("curve.edit_control_selection("),
            std::string::npos);
  EXPECT_EQ(native_curve_input.find("curve.select_control("),
            std::string::npos)
      << "Native Curve input must leave click and box edits to the shared "
         "viewport selection interaction.";

  EXPECT_NE(selection_policy.find("apply_selection_edit_operation("),
            std::string::npos);
  EXPECT_EQ(selection_policy.find("apply_rectangle_key_edit("),
            std::string::npos)
      << "Rectangle selection and Curve controls must share one set algebra.";
}

TEST(ResolvedSelectionPolicyGuardrailTests,
     PublicAndViewportSelectionUseOneTypedHitAndCommitAuthority) {
  const std::string public_api =
      read_source_file("src/api/quader_selection_api.cpp");
  const std::string viewport = read_source_file(
      "src/editor/viewport/viewport_selection_interaction.cpp");
  const std::string selection_owner = read_source_file(
      "src/editor/selection/editor_selection_service.cpp");
  const std::string scene_hit_policy = read_source_file(
      "src/editor/viewport/viewport_scene_hit_policy.cpp");
  const std::string checked_scene_pick = source_slice(
      selection_owner,
      "DocumentCommandResult EditorSelectionService::pick_scene_entity(",
      "EditorSelectionService::box_select(const ModelingBoxSelectRequest &request)");

  ASSERT_FALSE(checked_scene_pick.empty());
  EXPECT_NE(public_api.find("collect_viewport_scene_ray_hits("),
            std::string::npos);
  EXPECT_NE(public_api.find("collect_viewport_scene_marquee_hits("),
            std::string::npos);
  EXPECT_NE(public_api.find("apply_resolved_click("), std::string::npos);
  EXPECT_NE(public_api.find("apply_resolved_marquee("), std::string::npos);
  EXPECT_NE(viewport.find("collect_viewport_scene_point_hits("),
            std::string::npos);
  EXPECT_NE(viewport.find("collect_viewport_scene_marquee_hits("),
            std::string::npos);
  EXPECT_NE(public_api.find("resolve_depth_arbitrated_target("),
            std::string::npos);
  EXPECT_NE(viewport.find("resolve_depth_arbitrated_target("),
            std::string::npos);
  EXPECT_NE(scene_hit_policy.find("SceneSurfaceEligibility::RenderVisible"),
            std::string::npos);
  EXPECT_NE(scene_hit_policy.find(
                "SceneSurfaceEligibility::SelectableUnlocked"),
            std::string::npos);

  EXPECT_EQ(public_api.find("session.selection().pick_select("),
            std::string::npos)
      << "Public picks must not bypass typed scene-hit arbitration.";
  EXPECT_EQ(public_api.find("session.selection().box_select("),
            std::string::npos)
      << "Public marquees must not bypass typed scene-hit arbitration.";
  EXPECT_EQ(viewport.find("selection.pick_select("), std::string::npos)
      << "Viewport picks must commit through the resolved authority.";
  EXPECT_EQ(viewport.find("selection.box_select("), std::string::npos)
      << "Viewport marquees must commit through the resolved authority.";
  EXPECT_NE(checked_scene_pick.find("scene_pick_selection_result("),
            std::string::npos);
  EXPECT_NE(checked_scene_pick.find("applied.command.ok"),
            std::string::npos);
  EXPECT_EQ(checked_scene_pick.find("(void)scene_pick_selection"),
            std::string::npos)
      << "A scene-to-modeling projection failure must reach the caller.";
}

TEST(ResolvedSelectionPolicyGuardrailTests,
     PublicHoverAndLocalVisibilityUseResolvedHoverAuthority) {
  const std::string public_api =
      read_source_file("src/api/quader_selection_api.cpp");
  const std::string selection_owner = read_source_file(
      "src/editor/selection/editor_selection_service.cpp");
  const std::string local_visibility = read_source_file(
      "src/editor/session/viewport_local_visibility_service.cpp");

  EXPECT_NE(public_api.find("selection().update_hover("), std::string::npos);
  EXPECT_NE(public_api.find("selection().set_hover("), std::string::npos);
  for (const std::string_view bypass : {
           "update_selection_hover(",
           "set_selection_hover(",
           "clear_selection_hover(",
       }) {
    EXPECT_EQ(public_api.find(bypass), std::string::npos)
        << "Public hover must not mutate the modeling-only hover owner: "
        << bypass;
  }
  EXPECT_NE(selection_owner.find("publish_resolved_hover("),
            std::string::npos);
  EXPECT_NE(selection_owner.find("resolved_modeling_hover_target("),
            std::string::npos);
  EXPECT_NE(local_visibility.find("clear_resolved_hover_();"),
            std::string::npos);
  EXPECT_EQ(local_visibility.find("clear_selection_hover("),
            std::string::npos)
      << "Local Visibility transitions must clear every retained hover owner.";
}

TEST(ResolvedSelectionPolicyGuardrailTests,
     OnlyEditorSelectionServiceOwnsTheProductionDepthCursor) {
  const std::string selection_header = read_source_file(
      "src/editor/selection/editor_selection_service.hpp");
  const std::string selection_source = read_source_file(
      "src/editor/selection/editor_selection_service.cpp");
  const std::string modeling_header = read_source_file(
      "src/editor/modeling/services/modeling_selection_service.hpp");
  const std::string modeling_source = read_source_file(
      "src/editor/modeling/services/modeling_session_services.cpp");

  EXPECT_NE(selection_header.find("resolved_depth_cycle_index_"),
            std::string::npos);
  for (const std::string_view legacy_cursor : {
           "depth_pick_cycle_",
           "reset_depth_pick_cycle",
           "synchronize_depth_pick_cycle",
           "advance_depth_pick_cycle(",
       }) {
    EXPECT_EQ(modeling_header.find(legacy_cursor), std::string::npos)
        << legacy_cursor;
    EXPECT_EQ(modeling_source.find(legacy_cursor), std::string::npos)
        << legacy_cursor;
  }
  EXPECT_EQ(selection_header.find(
                "pick_select(const ModelingPickSelectRequest"),
            std::string::npos);
  EXPECT_EQ(selection_source.find("EditorSelectionService::pick_select("),
            std::string::npos);
}

TEST(CurvePreviewPerformanceGuardrailTests,
     PointerSamplesDoNotInvalidatePolygonSelectionOverlays) {
  const std::string modeling_session = read_source_file(
      "src/editor/modeling/services/modeling_session_service.cpp");
  const std::string update = source_slice(
      modeling_session,
      "ModelingCommandResult update_curve_document_preview(",
      "[[nodiscard]] bool curve_document_preview_active() const");

  ASSERT_FALSE(update.empty());
  EXPECT_NE(update.find(
                "sdk_projection_adapter_.update_curve_document_preview("),
            std::string::npos);
  EXPECT_EQ(update.find("invalidate_selection_overlay_caches()"),
            std::string::npos)
      << "Curve geometry has its own overlay revision; per-sample edits must "
         "retain the unrelated polygon selection overlay.";
}

TEST(CurveAnchorSnapPolicyGuardrailTests,
     CurveAnchorsReuseMoveSnapOwnersAndFreezeCurveCandidatesPerGesture) {
  const std::string move_header =
      read_source_file("src/editor/interactions/move_tool.hpp");
  const std::string move_source =
      read_source_file("src/editor/interactions/move_tool.cpp");
  const std::string ordinary_move_snap_target = source_slice(
      move_source,
      "std::optional<MoveVertexSnapTarget> resolve_move_vertex_snap_target(",
      "std::optional<MoveVertexSnapTarget> "
      "resolve_move_vertex_snap_candidate(");
  const std::string bounded_move_snap_candidate = source_slice(
      move_source,
      "std::optional<MoveVertexSnapTarget> "
      "resolve_move_vertex_snap_candidate(",
      "const EditorTool& move_tool()");
  const std::string runtime_picking = read_source_file(
      "src/editor/modeling/runtime/session_picking.cpp");
  const std::string vertex_candidates_without_surface = source_slice(
      runtime_picking, "void pick_all_without_surface_into(",
      "PickAllWithSurfaceResult pick_all_with_surface(");
  const std::string modeling_session = read_source_file(
      "src/editor/modeling/services/modeling_session_service.cpp");
  const std::string mesh_query = source_slice(
      modeling_session,
      "pick_mesh_snap_query(const ModelingRay &ray",
      "pick_mesh_vertex_snap_target(const ModelingRay &ray");
  const std::string gizmo_snap_query = source_slice(
      read_source_file("src/editor/viewport/transform_gizmo_interaction.cpp"),
      "resolve_curve_control_snap_target(",
      "QuaderTransform3 curve_control_orientation(");
  const std::string mesh_target_wrapper = source_slice(
      modeling_session,
      "pick_mesh_vertex_snap_target(const ModelingRay &ray",
      "knife_pick_targets(const ModelingToolRayEvent &event");
  const std::string gizmo = read_source_file(
      "src/editor/viewport/transform_gizmo_interaction.cpp");
  const std::string gizmo_update = source_slice(
      gizmo, "if (dragging_) {", "bool TransformGizmoInteraction::matches_last_drag_update(");
  const std::string curve_picking = read_source_file(
      "src/editor/viewport/viewport_curve_picking.cpp");
  const std::string qt_panel =
      read_source_file("src/ui/qt_viewport_panel.cpp");
  const std::string curve_mesh_snap_binding = source_slice(
      qt_panel, "interaction_frame.scene_mesh_snap_query =",
      "return result;");
  const std::string curve_collection = source_slice(
      curve_picking, "collect_viewport_curve_anchor_snap_candidates(",
      "resolve_viewport_curve_anchor_snap_target(");

  EXPECT_NE(move_header.find("resolve_move_vertex_snap_target("),
            std::string::npos);
  EXPECT_NE(move_header.find("resolve_move_vertex_snap_candidate("),
            std::string::npos);
  EXPECT_NE(move_header.find("move_axis_supports_vertex_snap("),
            std::string::npos);
  EXPECT_NE(ordinary_move_snap_target.find(
                "resolve_move_vertex_snap_query("),
            std::string::npos)
      << "Ordinary modeling Move must retain its complete mesh-surface snap "
         "policy.";
  EXPECT_EQ(ordinary_move_snap_target.find(
                "pick_all_without_surface_into("),
            std::string::npos);
  EXPECT_NE(bounded_move_snap_candidate.find(
                "maximum_world_ray_distance"),
            std::string::npos);
  EXPECT_NE(bounded_move_snap_candidate.find(
                "pick_all_without_surface_into("),
            std::string::npos);
  EXPECT_NE(mesh_query.find("move_vertex_snap_pick_options("),
            std::string::npos);
  EXPECT_NE(mesh_query.find("resolve_move_vertex_snap_query("),
            std::string::npos);
  EXPECT_EQ(mesh_query.find("selection_pick_options_from_radii("),
            std::string::npos);
  EXPECT_EQ(mesh_query.find("pick_all("), std::string::npos)
      << "The host mesh query must delegate to the canonical Move picker.";
  EXPECT_NE(mesh_target_wrapper.find("resolve_move_vertex_snap_candidate("),
            std::string::npos);
  EXPECT_NE(mesh_target_wrapper.find("maximum_ray_distance"),
            std::string::npos)
      << "The canonical render-visible depth must reach per-object vertex "
         "enumeration before a raw winner is chosen.";
  EXPECT_EQ(mesh_target_wrapper.find("pick_mesh_snap_query("),
            std::string::npos)
      << "The production vertex candidate query must not repeat the legacy "
         "mesh-surface traversal.";
  EXPECT_EQ(mesh_target_wrapper.find("cached_local_pick_face_geometry("),
            std::string::npos)
      << "Vertex candidates must not build the legacy face geometry cache.";
  EXPECT_NE(vertex_candidates_without_surface.find(
                "object_space_surface_cutoff("),
            std::string::npos)
      << "A caller-owned world depth must be converted for each transformed "
         "mesh occurrence.";
  EXPECT_NE(vertex_candidates_without_surface.find("&surface_cutoff"),
            std::string::npos)
      << "The converted cutoff must participate during component enumeration "
         "so an occluded raw winner cannot mask a visible vertex.";
  EXPECT_EQ(vertex_candidates_without_surface.find(
                "pick_all_with_surface_into("),
            std::string::npos);
  EXPECT_NE(gizmo_snap_query.find("scene_mesh_snap_query"),
            std::string::npos);
  EXPECT_NE(curve_mesh_snap_binding.find(
                "editor.query().mesh_vertex_snap_target("),
            std::string::npos)
      << "Qt must route the read-only mesh landmark query through the public "
         "editor API.";
  EXPECT_EQ(curve_mesh_snap_binding.find("editor.query().mesh_snap_query("),
            std::string::npos)
      << "Curve snapping must leave the legacy combined mesh query outside "
         "the production pointer path.";
  EXPECT_EQ(curve_mesh_snap_binding.find("modeling.pick_mesh_snap_query("),
            std::string::npos);
  EXPECT_NE(curve_mesh_snap_binding.find(
                "false, maximum_ray_distance"),
            std::string::npos)
      << "Curve anchors move independently from the scene selection. A "
         "selected stationary mesh must remain eligible and receive the "
         "shared render-visible depth.";
  EXPECT_EQ(gizmo_snap_query.find("scene_mesh_surface_distance"),
            std::string::npos)
      << "Curve snapping must not retain the legacy mesh-depth callback.";
  EXPECT_EQ(gizmo_snap_query.find("nearest_surface_ray_distance"),
            std::string::npos)
      << "Both vertex domains use the frozen render-visible surface depth.";
  EXPECT_NE(gizmo_snap_query.find("frame.scene_surfaces->nearest_hit("),
            std::string::npos);
  EXPECT_NE(gizmo_snap_query.find("maximum_mesh_ray_distance"),
            std::string::npos);
  EXPECT_NE(gizmo_snap_query.find(
                ".scene_mesh_snap_query(ray, vertex_radius,"),
            std::string::npos)
      << "Mesh enumeration must receive the same surface depth used by Curve "
         "candidates and final typed arbitration.";
  EXPECT_NE(gizmo_snap_query.find(
                "aggregate_viewport_vertex_snap_targets("),
            std::string::npos);

  EXPECT_NE(gizmo.find("move_axis_supports_vertex_snap(pending.axis)"),
            std::string::npos);
  EXPECT_NE(gizmo_update.find("preview_move_to_axis_snap_target("),
            std::string::npos);
  EXPECT_NE(gizmo_update.find("fill_transform_snap_target_overlay_points("),
            std::string::npos);
  EXPECT_EQ(gizmo_update.find("snap_overlay_points_ ="), std::string::npos)
      << "Snapped pointer samples must reuse the retained two-point overlay ";
  EXPECT_NE(move_source.find("points.reserve(2u)"), std::string::npos);
  EXPECT_EQ(gizmo_update.find(
                "collect_viewport_curve_anchor_snap_candidates("),
            std::string::npos)
      << "Curve anchors must be flattened once at drag begin, not per pointer sample.";
  EXPECT_NE(gizmo.find("std::sort(selected_anchors.begin()"),
            std::string::npos);
  EXPECT_NE(gizmo.find("std::binary_search(selected_anchors.begin()"),
            std::string::npos)
      << "Large Curve control selections must not linearly rescan every "
         "selected point for every frozen candidate.";
  EXPECT_NE(gizmo.find("collect_viewport_curve_draft_anchor_snap_candidate("),
            std::string::npos)
      << "An active uncommitted draft needs its own typed frozen anchor group.";

  EXPECT_NE(curve_collection.find("point.position"), std::string::npos);
  EXPECT_EQ(curve_collection.find("incoming_handle"), std::string::npos);
  EXPECT_EQ(curve_collection.find("outgoing_handle"), std::string::npos)
      << "Handles must never enter the anchor snap candidate set.";
}

TEST(PathArraySourcePolicyGuardrailTests,
     SettingsAndUnchangedRefreshesDoNotRescanTheSourceHierarchy) {
  const std::string header = read_source_file(
      "src/editor/array/path_array_tool_service.hpp");
  const std::string source = read_source_file(
      "src/editor/array/path_array_tool_service.cpp");

  EXPECT_NE(header.find("std::uint64_t path_generation"), std::string::npos);
  EXPECT_NE(header.find("std::uint64_t source_generation"),
            std::string::npos);
  EXPECT_NE(header.find("std::uint64_t layout_revision"),
            std::string::npos);
  EXPECT_NE(header.find("cached_source_generation_"), std::string::npos);
  EXPECT_NE(header.find("cached_changed_source_"), std::string::npos);

  const std::string refresh = source_slice(
      source, "CommandResult PathArrayToolService::refresh()",
      "CommandResult PathArrayToolService::cancel()");
  const std::size_t validation =
      refresh.find("source_is_current(*session_->source)");
  const std::size_t changed_capture =
      refresh.find("std::move(cached_changed_source_)", validation);
  ASSERT_NE(validation, std::string::npos);
  ASSERT_NE(changed_capture, std::string::npos);
  EXPECT_LT(validation, changed_capture);
  EXPECT_EQ(refresh.find("capture_source("), std::string::npos)
      << "The continuously called refresh path must consume the revision-gated "
         "source validation cache.";

  const std::string validation_owner = source_slice(
      source, "bool PathArrayToolService::source_is_current(",
      "bool PathArrayToolService::same_source_snapshot(");
  const std::size_t generation_gate = validation_owner.find(
      "cached_source_generation_ == session_->source_generation");
  const std::size_t hierarchy_capture =
      validation_owner.find("capture_source(source.root)", generation_gate);
  ASSERT_NE(generation_gate, std::string::npos);
  ASSERT_NE(hierarchy_capture, std::string::npos);
  EXPECT_LT(generation_gate, hierarchy_capture);
  EXPECT_NE(validation_owner.find("cached_source_scene_revision_ =="),
            std::string::npos);
  EXPECT_NE(validation_owner.find("cached_source_asset_revision_ =="),
            std::string::npos);
  EXPECT_NE(validation_owner.find("cached_changed_source_ ="),
            std::string::npos);

  const std::array setting_slices{
      source_slice(source, "CommandResult PathArrayToolService::set_count(",
                   "CommandResult PathArrayToolService::set_distance("),
      source_slice(source, "CommandResult PathArrayToolService::set_distance(",
                   "CommandResult PathArrayToolService::set_follow_path("),
      source_slice(source,
                   "CommandResult PathArrayToolService::set_follow_path(",
                   "CommandResult\n"
                   "PathArrayToolService::set_forward_axis("),
      source_slice(source,
                   "CommandResult\n"
                   "PathArrayToolService::set_forward_axis(",
                   "CommandResult PathArrayToolService::reset_settings()"),
      source_slice(source,
                   "CommandResult PathArrayToolService::reset_settings()",
                   "std::optional<PathArrayToolService::SourceSnapshot>"),
  };
  for (const std::string &setting : setting_slices) {
    EXPECT_NE(setting.find("layout_revision = issue_revision()"),
              std::string::npos);
    EXPECT_EQ(setting.find("source_generation ="), std::string::npos)
        << "Layout settings must not invalidate cached hierarchy participants.";
    EXPECT_EQ(setting.find("capture_source("), std::string::npos);
  }

  const std::string texture_plan = source_slice(
      source, "PathArrayToolService::build_preview_mesh_mappings(",
      "std::uint64_t PathArrayToolService::issue_revision()");
  EXPECT_NE(texture_plan.find("cached_texture_plan_source_generation_"),
            std::string::npos);
  EXPECT_NE(texture_plan.find("cached_texture_mapping_plan_"),
            std::string::npos);
}

} // namespace
} // namespace quader
