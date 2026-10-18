////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_editor_api.hpp"
#include "api/quader_command_ids.hpp"
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_api_type_conversions.hpp"
#include "assets/asset_registry.hpp"
#include "editor/curve/curve_tool_service.hpp"
#include "editor/input/quader_input_binding.hpp"
#include "editor/interactions/tool_identity.hpp"
#include "api/quader_operation_input_context.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/modeling/services/primitive_creation_tool_service.hpp"
#include "editor/metadata/host_contract.hpp"
#include "scene/scene_document.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/tools/native_viewport_tool_coordinator.hpp"
#include "editor/tools/viewport_tool_catalog.hpp"
#include "editor/tools/viewport_tool_ports.hpp"
#include "editor/tools/viewport_tool_runtime.hpp"
#include "editor/transform/align_tool_service.hpp"
#include "editor/viewport/viewport_modeling_tool_commit_adapter.hpp"
#include "support/viewport_overlay_test_helpers.hpp"
#include "render/overlay/viewport_overlay_submission_coordinator.hpp"
#include "viewport/host_viewport_contract_adapter.hpp"
#include "viewport/viewport_host_interaction_policy.hpp"
#include "viewport/viewport_input_router.hpp"

#include <quader/modeling/neutral/polygon/document_topology.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace quader {
namespace {

/// Supplies the required commit boundary without mutating preview-only fixtures.
class RejectingToolCommitPort final : public ViewportToolCommitPort {
public:
  ModelingCommandResult commit_insert_edge_loop_drag() override {
    return rejected();
  }
  ModelingCommandResult commit_knife_tool() override { return rejected(); }
  ModelingCommandResult commit_poly_tool() override { return rejected(); }
  ModelingCommandResult finalize_drawn_plane_tool(ModelingViewportTool) override {
    return rejected();
  }

private:
  static ModelingCommandResult rejected() {
    return {.ok = false, .message = "Preview-only fixture cannot commit."};
  }
};

TEST(ViewportToolCatalogTests, HostViewportAdapterUsesViewportToolDescriptors) {
  ViewportToolId tool = ViewportToolId::Select;
  EXPECT_TRUE(viewport_tool_from_contract_id("insert_edge_loop_tool", tool));
  EXPECT_EQ(tool, ViewportToolId::InsertEdgeLoop);
  EXPECT_TRUE(viewport_tool_from_contract_id("mirror_tool", tool));
  EXPECT_EQ(tool, ViewportToolId::Mirror);
  EXPECT_EQ(viewport_tool_contract_id(ViewportToolId::Knife),
            std::string_view("knife"));
  EXPECT_TRUE(viewport_tool_allows_selection_fallback(ViewportToolId::Move));
  EXPECT_FALSE(
      viewport_tool_allows_selection_fallback(ViewportToolId::Knife));
}

/// Injects deterministic preview failures into viewport-tool tests.
class FailingToolPreviewPorts final : public ViewportToolHoverPort,
                                      public ViewportToolPreviewPort,
                                      public ViewportToolCancelPort {
public:
  bool fail_query = false;
  bool fail_point_hover = false;
  bool fail_edge_loop_begin = false;
  bool allow_hover = true;
  ModelingToolStatus status;
  mutable int status_queries = 0;
  int hover_updates = 0;
  int invalidations = 0;
  int cancellations = 0;
  int cut_mode_cycles = 0;
  bool accept_plane_drags = false;
  std::vector<ModelingToolRayEvent> plane_events;

  bool claim(ModelingBoxDragState) override { return allow_hover; }
  void invalidate() override { ++invalidations; }
  ModelingBoxPreview update_box_hover(const ModelingBoxToolEvent &) override {
    ++hover_updates;
    return {.ok = false, .message = "Forced Box hover failure."};
  }
  ModelingCommandResult
  update_insert_edge_loop_hover(const ModelingToolRayEvent &) override {
    return point_hover_result();
  }
  ModelingCommandResult
  update_knife_hover(const ModelingToolRayEvent &) override {
    return {.ok = true};
  }
  ModelingCommandResult clear_knife_hover() override { return {.ok = true}; }
  ModelingCommandResult
  update_poly_hover(const ModelingToolRayEvent &) override {
    return point_hover_result();
  }
  ModelingCommandResult
  update_drawn_plane_hover(ModelingViewportTool,
                           const ModelingToolRayEvent &event) override {
    plane_events.push_back(event);
    return point_hover_result();
  }

  ModelingToolStatus tool_status() const override {
    ++status_queries;
    return status;
  }
  ModelingBoxPreview box_preview(bool) const override {
    return fail_query
               ? ModelingBoxPreview{
                     .ok = false,
                     .message = "Forced Box preview query failure.",
                 }
               : ModelingBoxPreview{
                     .ok = true,
                     .drag_state = ModelingBoxDragState::Idle,
                 };
  }
  ModelingBoxPreview
  begin_box_footprint(const ModelingBoxToolEvent &) override {
    return {.ok = true};
  }
  ModelingBoxPreview
  update_box_footprint(const ModelingBoxToolEvent &) override {
    return {.ok = true};
  }
  ModelingBoxPreview begin_box_height(const ModelingBoxToolEvent &) override {
    return {.ok = true};
  }
  ModelingCommandResult
  begin_insert_edge_loop_drag(const ModelingToolRayEvent &) override {
    return {.ok = !fail_edge_loop_begin,
            .message = fail_edge_loop_begin ? "Forced edge loop begin failure."
                                            : ""};
  }
  ModelingCommandResult
  update_insert_edge_loop_drag(const ModelingToolRayEvent &) override {
    return {.ok = true};
  }
  ModelingCommandResult
  accept_knife_point(const ModelingToolRayEvent &) override {
    return {.ok = true};
  }
  ModelingCommandResult
  accept_poly_point(const ModelingToolRayEvent &) override {
    return {.ok = true};
  }
  ModelingCommandResult begin_drawn_plane_point_drag(ModelingViewportTool,
                                                       int) override {
    return {.ok = true};
  }
  ModelingCommandResult
  begin_drawn_plane_line_drag(ModelingViewportTool,
                              const ModelingToolRayEvent &event) override {
    plane_events.push_back(event);
    if (accept_plane_drags) status.cut_dragging = true;
    return {.ok = true};
  }
  ModelingCommandResult
  click_drawn_plane_point(ModelingViewportTool,
                          const ModelingToolRayEvent &) override {
    return {.ok = true};
  }
  ModelingCommandResult end_drawn_plane_drag(ModelingViewportTool) override {
    if (accept_plane_drags) status.cut_dragging = false;
    return {.ok = true};
  }
  ModelingCommandResult cycle_cut_mode() override {
    ++cut_mode_cycles;
    return {.ok = true};
  }
  ModelingCommandResult cancel_box() override { return {.ok = true}; }
  ModelingCommandResult cancel_active_modal_tool(bool exit_tool) override {
    ++cancellations;
    last_cancel_exited_tool = exit_tool;
    return {.ok = false, .message = "Forced modal cancel failure."};
  }

  bool last_cancel_exited_tool = false;

private:
  ModelingCommandResult point_hover_result() const {
    return {
        .ok = !fail_point_hover,
        .message = fail_point_hover ? "Forced tool hover failure." : "",
    };
  }
};

TEST(ViewportToolCatalogTests, CutModeCycleHonorsCapturedKeepModeAvailability) {
  ViewportToolRuntime runtime;
  ViewportToolState state;
  ASSERT_TRUE(runtime.set_active_tool(state, ViewportToolId::Cut));
  ViewportCameraController camera;
  FailingToolPreviewPorts ports;
  RejectingToolCommitPort commit;
  ViewportModalToolState modal_state;
  ViewportPointToolContext point;
  point.make_event_at = [](QVec2, bool) { return ModelingToolRayEvent{}; };
  ViewportToolContext context{
      .state = state, .camera = camera,
      .ports = {.hover = &ports, .preview = &ports, .commit = &commit,
                .cancel = &ports},
      .modal_state = &modal_state,
      .point = &point,
  };
  const ViewportToolEvent event{
      .kind = ViewportToolEventKind::Frame, .tool = ViewportToolId::Cut,
      .pointer = {.mode_cycle_pressed = true},
  };
  ports.status.cut_keep_mode_enabled = false;
  (void)runtime.handle_active_tool_event(state, context, event);
  EXPECT_EQ(ports.cut_mode_cycles, 0);
  ports.status.cut_keep_mode_enabled = true;
  (void)runtime.handle_active_tool_event(state, context, event);
  EXPECT_EQ(ports.cut_mode_cycles, 1);
}

TEST(ViewportToolCatalogTests, ModalPlaneHintsRetainAcceptedHoverWithoutRepeatingQueries) {
  ViewportToolRuntime runtime;
  ViewportToolState state;
  ASSERT_TRUE(runtime.set_active_tool(state, ViewportToolId::Mirror));
  ViewportCameraController camera;
  camera.set_viewport_size(640.0f, 480.0f);
  FailingToolPreviewPorts ports;
  ports.status.mirror_points = {{{-1.0f, 0.0f, 0.0f},
                                 {1.0f, 0.0f, 0.0f}, {}}};
  ports.status.mirror_captured_point_count = 2;
  ports.status.mirror_has_line = true;
  RejectingToolCommitPort commit;
  ViewportPointToolContext point;
  point.make_event_at = [](QVec2, bool) { return ModelingToolRayEvent{}; };
  ViewportToolContext context{
      .state = state, .camera = camera,
      .ports = {.hover = &ports, .preview = &ports, .commit = &commit,
                .cancel = &ports},
      .point = &point,
  };
  ViewportToolEvent event{
      .kind = ViewportToolEventKind::Frame, .tool = ViewportToolId::Mirror,
      .pointer = {.hovered = true, .can_interact = true,
                  .mouse_local = camera.project_world_point(
                      ports.status.mirror_points[0]).position},
  };
  (void)runtime.handle_active_tool_event(state, context, event);
  QuaderOperationInputContext hints;
  runtime.populate_operation_input_context(hints);
  EXPECT_TRUE(hints.plane_endpoint_hovered);
  EXPECT_FALSE(hints.plane_body_hovered);
  const int status_queries = ports.status_queries;
  ports.allow_hover = false;
  for (int frame = 0; frame < 20; ++frame) {
    (void)runtime.handle_active_tool_event(state, context, event);
    runtime.populate_operation_input_context(hints);
    EXPECT_TRUE(hints.plane_endpoint_hovered);
  }
  EXPECT_EQ(ports.status_queries, status_queries);

  ports.allow_hover = true;
  event.pointer.mouse_local = camera.project_world_point({}).position;
  (void)runtime.handle_active_tool_event(state, context, event);
  runtime.populate_operation_input_context(hints);
  EXPECT_FALSE(hints.plane_endpoint_hovered);
  EXPECT_TRUE(hints.plane_body_hovered);
  event.pointer.suppress_hover_work = true;
  (void)runtime.handle_active_tool_event(state, context, event);
  runtime.populate_operation_input_context(hints);
  EXPECT_FALSE(hints.plane_endpoint_hovered);
  EXPECT_FALSE(hints.plane_body_hovered);
}

TEST(ViewportToolCatalogTests, CutLineCursorAndTemporaryModifiersFollowSemanticPointerEvents) {
  ViewportToolRuntime runtime;
  ViewportToolState state;
  ASSERT_TRUE(runtime.set_active_tool(state, ViewportToolId::Cut));
  ViewportCameraController camera;
  camera.set_viewport_size(640.0F, 480.0F);
  FailingToolPreviewPorts ports;
  ports.accept_plane_drags = true;
  ports.status.cut_points = {{{-1.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, {}}};
  ports.status.cut_captured_point_count = 2;
  ports.status.cut_has_line = true;
  RejectingToolCommitPort commit;
  ViewportPointToolContext point;
  point.make_event_at = [](QVec2 local, bool dragging) {
    return ModelingToolRayEvent{.screen_position = local, .snap_enabled = true,
                                .constrain_axis = false, .dragging = dragging};
  };
  point.hit_drawn_plane_line = [](const auto&, int, bool) { return true; };
  ViewportToolContext context{
      .state = state, .camera = camera,
      .ports = {.hover = &ports, .preview = &ports, .commit = &commit, .cancel = &ports},
      .point = &point,
  };
  ViewportToolEvent event{
      .kind = ViewportToolEventKind::Frame, .tool = ViewportToolId::Cut,
      .pointer = {.hovered = true, .can_interact = true,
                  .mouse_local = camera.project_world_point({}).position},
  };
  (void)runtime.handle_active_tool_event(state, context, event);
  ASSERT_FALSE(ports.plane_events.empty());
  EXPECT_FALSE(ports.plane_events.back().snap_enabled);
  EXPECT_EQ(runtime.tool_pointer_cursor(ViewportToolId::Cut), ViewportPointerCursor::OpenHand);
  const int status_queries = ports.status_queries;
  ports.allow_hover = false;
  for (int frame = 0; frame < 10; ++frame) {
    (void)runtime.handle_active_tool_event(state, context, event);
    EXPECT_EQ(runtime.tool_pointer_cursor(ViewportToolId::Cut), ViewportPointerCursor::OpenHand);
  }
  EXPECT_EQ(ports.status_queries, status_queries);

  const std::array<ViewportPointerButtonEdge, 1> presses{{{
      .button = QuaderMouseButton::Left, .local = event.pointer.mouse_local, .sequence = 5U,
      .modifiers = ViewportModifiers{},
  }}};
  event.pointer.left_clicked = true;
  event.pointer.left_down = true;
  event.pointer.left_click_local = event.pointer.mouse_local;
  event.pointer.pointer_pressed_edges = presses;
  (void)runtime.handle_active_tool_event(state, context, event);
  EXPECT_EQ(runtime.tool_pointer_cursor(ViewportToolId::Cut), ViewportPointerCursor::ClosedHand);
  event.pointer.left_clicked = false;
  event.pointer.pointer_pressed_edges = {};
  for (const bool held : {true, false}) {
    event.pointer.modifiers = {.primary = held, .shift = held};
    (void)runtime.handle_active_tool_event(state, context, event);
    EXPECT_EQ(ports.plane_events.back().snap_enabled, held);
    EXPECT_EQ(ports.plane_events.back().constrain_axis, held);
    EXPECT_TRUE(ports.plane_events.back().dragging);
  }
  const std::array<ViewportPointerButtonEdge, 1> releases{{{
      .button = QuaderMouseButton::Left, .local = {345.0F, 251.0F}, .sequence = 6U,
      .modifiers = ViewportModifiers{.primary = true, .shift = true},
  }}};
  event.pointer.left_down = false;
  event.pointer.left_released = true;
  event.pointer.pointer_released_edges = releases;
  (void)runtime.handle_active_tool_event(state, context, event);
  EXPECT_TRUE(ports.plane_events.back().snap_enabled);
  EXPECT_TRUE(ports.plane_events.back().constrain_axis);
  EXPECT_FLOAT_EQ(ports.plane_events.back().screen_position.x, 345.0F);
  EXPECT_FLOAT_EQ(ports.plane_events.back().screen_position.y, 251.0F);
  EXPECT_EQ(runtime.tool_pointer_cursor(ViewportToolId::Cut), ViewportPointerCursor::BoxCutter);
}

TEST(ViewportToolCatalogTests, CutModifierPolicyDoesNotChangeMirrorEvents) {
  ViewportToolRuntime runtime;
  ViewportToolState state;
  ASSERT_TRUE(runtime.set_active_tool(state, ViewportToolId::Mirror));
  ViewportCameraController camera;
  FailingToolPreviewPorts ports;
  RejectingToolCommitPort commit;
  ViewportPointToolContext point;
  point.make_event_at = [](QVec2, bool) {
    return ModelingToolRayEvent{.snap_enabled = true, .constrain_axis = false};
  };
  ViewportToolContext context{
      .state = state, .camera = camera,
      .ports = {.hover = &ports, .preview = &ports, .commit = &commit, .cancel = &ports},
      .point = &point,
  };
  (void)runtime.handle_active_tool_event(state, context, {
      .kind = ViewportToolEventKind::Frame, .tool = ViewportToolId::Mirror,
      .pointer = {.hovered = true, .can_interact = true, .modifiers = {.shift = true}},
  });
  ASSERT_FALSE(ports.plane_events.empty());
  EXPECT_TRUE(ports.plane_events.back().snap_enabled);
  EXPECT_FALSE(ports.plane_events.back().constrain_axis);
}

TEST(ViewportToolCatalogTests, StrokeClosureHintsFollowOwnerHoverAndClearWhenInterrupted) {
  for (const ViewportToolId tool : {ViewportToolId::Knife, ViewportToolId::Poly}) {
    ViewportToolRuntime runtime;
    ViewportToolState state;
    ASSERT_TRUE(runtime.set_active_tool(state, tool));
    ViewportCameraController camera;
    FailingToolPreviewPorts ports;
    ports.status.knife_current_closes_stroke = true;
    ports.status.poly_current_closes_stroke = true;
    ViewportPointToolContext point;
    point.make_event_at = [](QVec2, bool) { return ModelingToolRayEvent{}; };
    ViewportToolContext context{
        .state = state, .camera = camera,
        .ports = {.hover = &ports, .preview = &ports, .cancel = &ports},
        .point = &point,
    };
    ViewportToolEvent event{
        .kind = ViewportToolEventKind::Frame, .tool = tool,
        .pointer = {.hovered = true, .can_interact = true},
    };
    (void)runtime.handle_active_tool_event(state, context, event);
    QuaderOperationInputContext hints;
    runtime.populate_operation_input_context(hints);
    EXPECT_TRUE(hints.stroke_close_hovered);
    const int status_queries = ports.status_queries;
    ports.allow_hover = false;
    for (int frame = 0; frame < 20; ++frame) {
      (void)runtime.handle_active_tool_event(state, context, event);
    }
    runtime.populate_operation_input_context(hints);
    EXPECT_TRUE(hints.stroke_close_hovered);
    EXPECT_EQ(ports.status_queries, status_queries);
    event.pointer.can_interact = false;
    (void)runtime.handle_active_tool_event(state, context, event);
    runtime.populate_operation_input_context(hints);
    EXPECT_FALSE(hints.stroke_close_hovered);
  }
}

/// Drives the real persistent primitive owner through deterministic rays.
class PrimitivePointerHarness final {
public:
  PrimitivePointerHarness()
      : editor(QuaderApiSessionInternalAccess::from_scene_session(session)),
        tool_ports(session.modeling()),
        context{
            .state = state,
            .camera = camera,
            .ports = tool_ports.ports(),
            .document = &session.scene(),
            .modal_state = &modal_state,
            .box = &box_context,
        } {
    state.active_tool = ViewportToolId::CreatePrimitive;
    tool = find_viewport_tool(ViewportToolId::CreatePrimitive);
    activated = editor.tools().activate(ViewportToolId::CreatePrimitive).ok();
    box_context.make_event_at = [](QVec2 local) {
      return ModelingBoxToolEvent{
          .screen_position = local,
          .ray = {.origin = {2.0f + local.x * 0.1f, 4.0f,
                             2.0f + local.y * 0.1f},
                  .direction = {0.0f, -1.0f, 0.0f}},
          .grid_size = 1.0,
      };
    };
    box_context.accept_preview =
        [&](std::string name, AssetId material, bool snap_enabled) {
          ++accept_count;
          const QuaderApiVoidResult result =
              editor.tools().accept_active_primitive(
                  std::move(name), material, snap_enabled);
          return ModelingCommandResult{
              .ok = result.ok,
              .changed = result.changed,
              .message = result.message,
          };
        };
    box_context.cancel_session = [&]() {
      const QuaderApiVoidResult result =
          editor.tools().cancel_active_tool_preview();
      return ModelingCommandResult{
          .ok = result.ok,
          .changed = result.changed,
          .message = result.message,
      };
    };
    box_context.session_active = [&]() {
      return editor.tools().has_active_primitive_creation();
    };
    box_context.next_mesh_name = [] {
      return std::string{"Pointer Gesture Primitive"};
    };
  }

  [[nodiscard]] ViewportToolResult
  dispatch_direct(ViewportPointerToolEvent pointer) {
    return tool->handle_event(
        context, ViewportToolEvent{
                     .kind = ViewportToolEventKind::Frame,
                     .tool = ViewportToolId::CreatePrimitive,
                     .pointer = pointer,
                 });
  }

  SceneSession session;
  QuaderEditorApi editor;
  ViewportToolState state;
  ViewportCameraController camera;
  ViewportModalToolState modal_state;
  ViewportBoxToolContext box_context;
  ViewportModelingToolPortAdapter tool_ports;
  ViewportToolContext context;
  const ViewportTool *tool = nullptr;
  bool activated = false;
  int accept_count = 0;
};

/// Counts viewport cancellations while forwarding them to the real owner.
class CountingPrimitiveCancelPort final : public ViewportToolCancelPort {
public:
  explicit CountingPrimitiveCancelPort(ViewportToolCancelPort &delegate)
      : delegate_(delegate) {}

  ModelingCommandResult cancel_box() override {
    ++box_cancellations;
    return delegate_.cancel_box();
  }
  ModelingCommandResult cancel_active_modal_tool(bool exit_tool) override {
    return delegate_.cancel_active_modal_tool(exit_tool);
  }

  int box_cancellations = 0;

private:
  ViewportToolCancelPort &delegate_;
};

void expect_point_tool_click_uses_queued_press_coordinate(
    ViewportToolId tool_id, bool expected_dragging) {
  SceneDocument document;
  AssetRegistry assets;
  ModelingSessionService modeling(document, assets);
  ViewportModelingToolPortAdapter tool_ports(modeling);
  RejectingToolCommitPort commit_port;
  ViewportToolState state;
  state.active_tool = tool_id;
  ViewportCameraController camera;
  ViewportModalToolState modal_state;

  constexpr float kDevicePixelRatio = 2.0f;
  const QVec2 queued_press_physical{
      75.0f * kDevicePixelRatio,
      50.0f * kDevicePixelRatio,
  };
  const QVec2 later_frame_mouse_physical{900.0f, 700.0f};
  QVec2 dispatched_position{};
  bool dispatched_dragging = false;
  int dispatch_count = 0;
  ViewportPointToolContext point_context;
  point_context.make_event_at = [&](QVec2 position, bool dragging) {
    ++dispatch_count;
    dispatched_position = position;
    dispatched_dragging = dragging;
    return ModelingToolRayEvent{
        .screen_position = position,
        .dragging = dragging,
    };
  };
  ViewportToolContext context{
      .state = state,
      .camera = camera,
      .ports = tool_ports.ports(&commit_port),
      .modal_state = &modal_state,
      .point = &point_context,
  };

  const ViewportTool *tool = find_viewport_tool(tool_id);
  ASSERT_NE(tool, nullptr);
  const ViewportToolResult result = tool->handle_event(
      context,
      ViewportToolEvent{
          .kind = ViewportToolEventKind::Frame,
          .tool = tool_id,
          .pointer =
              {
                  .left_clicked = true,
                  .can_interact = true,
                  .suppress_hover_work = true,
                  .mouse_local = later_frame_mouse_physical,
                  .left_click_local = queued_press_physical,
                  .viewport_size = {1600.0f, 1200.0f},
              },
      });

  EXPECT_TRUE(result.handled);
  ASSERT_EQ(dispatch_count, 1);
  EXPECT_FLOAT_EQ(dispatched_position.x, queued_press_physical.x);
  EXPECT_FLOAT_EQ(dispatched_position.y, queued_press_physical.y);
  EXPECT_EQ(dispatched_dragging, expected_dragging);
}

TEST(ViewportToolCatalogTests, CatalogContainsExpectedInteractiveToolContracts) {
  ASSERT_EQ(viewport_tool_catalog().size(), viewport_tool_descriptors().size());
  for (const ToolDescriptor &descriptor : viewport_tool_descriptors()) {
    SCOPED_TRACE(descriptor.id);
    EXPECT_EQ(std::string_view(viewport_tool_id(descriptor.tool)), descriptor.id);
    EXPECT_EQ(find_viewport_tool_descriptor(descriptor.id), &descriptor);
    EXPECT_EQ(find_viewport_tool_descriptor(descriptor.tool), &descriptor);
    if (!descriptor.editor_owned_native_lifecycle) {
      EXPECT_EQ(find_viewport_tool_descriptor(descriptor.modeling_tool),
                &descriptor);
    }

    const ViewportTool *tool = find_viewport_tool(descriptor.tool);
    ASSERT_NE(tool, nullptr) << static_cast<int>(descriptor.tool);
    EXPECT_EQ(find_viewport_tool(descriptor.id), tool);
    EXPECT_EQ(tool->viewport_tool(), descriptor.tool);
    EXPECT_EQ(tool->modeling_tool(), descriptor.modeling_tool);
    EXPECT_EQ(tool->id(), descriptor.id);
    EXPECT_EQ(tool->label(), descriptor.label);
    EXPECT_EQ(tool->kind(), descriptor.kind);
    EXPECT_EQ(tool->menu_group(), descriptor.menu_group);
    EXPECT_EQ(tool->owns_viewport(), descriptor.owns_viewport);
    EXPECT_EQ(tool->blocks_viewport_focus_shortcuts(),
              descriptor.blocks_viewport_focus_shortcuts);
    EXPECT_EQ(tool->supports_transform_gizmo(),
              descriptor.supports_transform_gizmo);
    EXPECT_EQ(viewport_tool_supports_transform_gizmo(descriptor.tool),
              descriptor.supports_transform_gizmo);
    if (!descriptor.editor_owned_native_lifecycle) {
      EXPECT_EQ(
          viewport_tool_interaction_tool(descriptor.tool),
          quader_editor_core::viewport_interaction_tool_from_id(descriptor.id));
    }
  }
}

TEST(ViewportToolCatalogTests,
     SelectionCapableToolsShareEscapeDispatchAndConflictScope) {
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  constexpr auto kClearSelection = quader_command::kEditClearSelection;
  const auto shortcut = editor.commands().shortcut_for(kClearSelection);
  ASSERT_EQ(shortcut, "Escape");
  const auto chords = quader_input_parse_shortcut(shortcut);
  ASSERT_TRUE(chords.has_value());
  ASSERT_EQ(chords->size(), 1u);
  for (const auto &descriptor : viewport_tool_descriptors()) {
    SCOPED_TRACE(descriptor.id);
    ViewportToolState state;
    state.active_tool = descriptor.tool;
    const auto dispatch = editor.commands().dispatch_viewport_input_binding(
        chords->front(), state, true, false);
    if (descriptor.tool == ViewportToolId::UvPeel ||
        descriptor.tool == ViewportToolId::Extrude ||
        descriptor.tool == ViewportToolId::Align) {
      EXPECT_EQ(dispatch.kind,
                QuaderViewportKeybindingDispatch::Kind::ViewportShortcut);
      EXPECT_EQ(dispatch.shortcut.kind, ViewportShortcutKind::CancelActiveTool);
      continue;
    }
    if (!descriptor.allows_selection_fallback) {
      EXPECT_NE(dispatch.command_id, kClearSelection.string());
      continue;
    }
    EXPECT_EQ(dispatch.kind, QuaderViewportKeybindingDispatch::Kind::Command);
    EXPECT_EQ(dispatch.command_id, kClearSelection.string());
    const auto conflicts = editor.commands().conflicts_for_shortcut(
        shortcut, "activeTool." + std::string(descriptor.id));
    EXPECT_NE(std::find_if(conflicts.begin(), conflicts.end(),
                          [&](const auto &conflict) {
                            return conflict.target_id == kClearSelection.string();
                          }),
              conflicts.end());
  }
}

TEST(ViewportToolCatalogTests,
     UvPeelLifecycleLeavesOrdinaryPointerFramesToEdgeSelection) {
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const ViewportTool *tool = find_viewport_tool(ViewportToolId::UvPeel);
  ASSERT_NE(tool, nullptr);
  EXPECT_EQ(tool->kind(), ToolKind::Modal);
  EXPECT_TRUE(tool->descriptor().editor_owned_native_lifecycle);
  EXPECT_TRUE(tool->allows_selection_fallback());
  EXPECT_FALSE(tool->owns_viewport());
  EXPECT_FALSE(tool->supports_transform_gizmo());
  EXPECT_FALSE(tool->blocks_viewport_focus_shortcuts());
  EXPECT_EQ(tool->modeling_tool(), ModelingViewportTool::Select);
  EXPECT_TRUE(tool->shortcut().empty());

  const auto mesh = editor.modeling().create_box(
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      "UV Peel participant");
  ASSERT_TRUE(mesh.ok()) << mesh.error_message();
  ASSERT_TRUE(editor.selection().select_object(mesh.value()).ok());
  ASSERT_TRUE(editor.mode().set(QuaderEditorMode::Edit).ok());
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::UvPeel).ok());
  EXPECT_EQ(session.modeling().selection_mode(), ModelingSelectionMode::Edge);
  EXPECT_EQ(session.modeling().tool_status().active_tool,
            ModelingViewportTool::Select);
  ViewportInputFrame input;
  input.viewport_hovered = true;
  input.viewport_focused = true;
  ViewportInputResult result;
  ViewportCameraController camera;
  const auto ordinary_frame = session.native_tools().handle_pointer_frame(
      session.scene(), session.assets(), &session.point_placement(), camera,
      {.input = input,
       .input_result = result,
       .left_pressed = true,
       .left_down = true});
  EXPECT_FALSE(ordinary_frame.consumed);
  EXPECT_FALSE(ordinary_frame.blocks_transform_gizmo);
  EXPECT_FALSE(ordinary_frame.command_attempted);
  EXPECT_FALSE(session.native_tools().pointer_dragging());
  EXPECT_EQ(session.native_tools().active_tool_id(), "uv_peel");

  // Unlike viewport-owning modal tools, Peel reaches this native boundary for
  // Escape directly. It must cancel even before the first guide is selected.
  result.cancel_requested = true;
  const auto canceled = session.native_tools().handle_pointer_frame(
      session.scene(), session.assets(), &session.point_placement(), camera,
      {.input = input, .input_result = result});
  EXPECT_TRUE(canceled.consumed);
  EXPECT_TRUE(canceled.sync_active_tool);
  EXPECT_TRUE(canceled.command_attempted);
  EXPECT_TRUE(canceled.command_result.ok);
  EXPECT_TRUE(canceled.command_result.changed);
  EXPECT_FALSE(session.native_tools().has_pending_tool());
  EXPECT_EQ(editor.tools().active_tool(), "select");
  EXPECT_EQ(editor.selection().summary().mode, QuaderSelectionMode::Edge);
}

TEST(ViewportToolCatalogTests,
     AuthoredAndPrecisionCursorsBelongToTheirToolOwners) {
  ViewportToolRuntime runtime;
  for (const ToolDescriptor &descriptor : viewport_tool_descriptors()) {
    const ViewportPointerCursor expected =
        descriptor.tool == ViewportToolId::CreatePrimitive
            ? ViewportPointerCursor::PrecisionCross
            : ((descriptor.tool == ViewportToolId::Knife ||
                descriptor.tool == ViewportToolId::Cut)
                   ? ViewportPointerCursor::BoxCutter
                   : ViewportPointerCursor::Default);
    EXPECT_EQ(runtime.tool_pointer_cursor(descriptor.tool), expected)
        << descriptor.id;
  }

  EXPECT_EQ(viewport_tool_for_modeling(ModelingViewportTool::Box),
            ViewportToolId::CreatePrimitive);
  EXPECT_EQ(viewport_tool_for_modeling(ModelingViewportTool::Cylinder),
            ViewportToolId::CreatePrimitive);
  EXPECT_EQ(viewport_tool_for_modeling(ModelingViewportTool::Sphere),
            ViewportToolId::CreatePrimitive);
}

TEST(ViewportToolCatalogTests, ExposesToolMetadataAndConversions) {
  const Tool *primitive = find_viewport_tool("create_primitive");
  ASSERT_NE(primitive, nullptr);
  EXPECT_EQ(primitive->viewport_tool(), ViewportToolId::CreatePrimitive);
  EXPECT_EQ(primitive->modeling_tool(), ModelingViewportTool::Box);
  EXPECT_EQ(primitive->kind(), ToolKind::Modal);
  EXPECT_EQ(primitive->label(), "Create Primitive");
  EXPECT_TRUE(primitive->shortcut().empty());
  EXPECT_TRUE(primitive->owns_viewport());
  EXPECT_FALSE(primitive->blocks_viewport_focus_shortcuts());
  EXPECT_EQ(find_viewport_tool("box"), nullptr);
  EXPECT_EQ(find_viewport_tool("cylinder"), nullptr);
  EXPECT_EQ(find_viewport_tool("sphere"), nullptr);

  const Tool *cut = find_viewport_tool("cut");
  ASSERT_NE(cut, nullptr);
  EXPECT_TRUE(cut->shortcut().empty());
  const Tool *mirror = find_viewport_tool("mirror");
  ASSERT_NE(mirror, nullptr);
  EXPECT_EQ(mirror->shortcut(), "M");

  EXPECT_EQ(viewport_tool_transform_tool(ViewportToolId::Move),
            ModelingTransformTool::Move);
  EXPECT_EQ(modeling_viewport_tool_for(ViewportToolId::Extrude),
            ModelingViewportTool::Extrude);
  EXPECT_EQ(viewport_tool_transform_tool(ViewportToolId::Rotate),
            ModelingTransformTool::Rotate);
  EXPECT_EQ(viewport_tool_transform_tool(ViewportToolId::Scale),
            ModelingTransformTool::Scale);
  EXPECT_EQ(viewport_tool_interaction_tool(ViewportToolId::Move),
            quader_editor_core::ViewportInteractionTool::Move);
  EXPECT_EQ(viewport_tool_interaction_tool(ViewportToolId::Rotate),
            quader_editor_core::ViewportInteractionTool::Rotate);
  EXPECT_EQ(viewport_tool_interaction_tool(ViewportToolId::Scale),
            quader_editor_core::ViewportInteractionTool::Scale);
  EXPECT_EQ(modeling_viewport_tool_for(ViewportToolId::Cut),
            ModelingViewportTool::Cut);
  EXPECT_EQ(modeling_viewport_tool_for(ViewportToolId::CreatePrimitive),
            ModelingViewportTool::Box);
  EXPECT_EQ(viewport_tool_for_modeling(ModelingViewportTool::Extrude),
            ViewportToolId::Extrude);
  EXPECT_EQ(viewport_tool_for_modeling(ModelingViewportTool::Mirror),
            ViewportToolId::Mirror);
  EXPECT_EQ(viewport_tool_for_modeling(ModelingViewportTool::Cylinder),
            ViewportToolId::CreatePrimitive);
  EXPECT_EQ(viewport_tool_for_modeling(ModelingViewportTool::Sphere),
            ViewportToolId::CreatePrimitive);
  EXPECT_TRUE(viewport_tool_blocks_focus_shortcuts(ViewportToolId::Cut));
  EXPECT_TRUE(viewport_tool_owns_viewport(ViewportToolId::InsertEdgeLoop));
  EXPECT_TRUE(viewport_tool_supports_transform_gizmo(ViewportToolId::Move));
  EXPECT_TRUE(viewport_tool_supports_transform_gizmo(ViewportToolId::Rotate));
  EXPECT_TRUE(viewport_tool_supports_transform_gizmo(ViewportToolId::Scale));
  EXPECT_FALSE(viewport_tool_supports_transform_gizmo(ViewportToolId::Extrude));
  EXPECT_FALSE(viewport_tool_supports_transform_gizmo(ViewportToolId::Mirror));
}

TEST(ViewportToolCatalogTests, RemovedExtendToolIdIsRejected) {
  EXPECT_EQ(find_viewport_tool("extend"), nullptr);
  EXPECT_EQ(find_viewport_tool_descriptor("extend"), nullptr);
  EXPECT_EQ(quader_editor_core::viewport_interaction_tool_from_id("extend"),
            quader_editor_core::ViewportInteractionTool::Unknown);
}

TEST(ViewportToolCatalogTests, HostToolMetadataDerivesFromViewportToolCatalog) {
  ASSERT_EQ(quader_host_contract::tool_catalog().size(),
            viewport_tool_descriptors().size());
  for (const ToolDescriptor &tool : viewport_tool_descriptors()) {
    const quader_host_contract::HostToolDescriptor *descriptor =
        quader_host_contract::find_tool_descriptor(tool.id);
    ASSERT_NE(descriptor, nullptr) << tool.id;
    EXPECT_EQ(descriptor->label, tool.label);
    EXPECT_EQ(descriptor->primary_shortcut, tool.shortcut);
    EXPECT_EQ(descriptor->transform_tool, tool.kind == ToolKind::Transform);
    EXPECT_EQ(descriptor->modal_tool, tool.kind == ToolKind::Modal);
  }

    for (const quader_host_contract::HostToolDescriptor& descriptor : quader_host_contract::tool_catalog()) {
      EXPECT_NE(find_viewport_tool_descriptor(descriptor.id), nullptr)
          << descriptor.id;
    }
}

TEST(ViewportToolCatalogTests, HostToolPlacementsAndGroupsResolveToViewportCatalog) {
    for (const quader_host_contract::HostToolGroupDescriptor& group : quader_host_contract::tool_group_catalog()) {
      for (std::string_view tool_id : group.tool_ids) {
        EXPECT_NE(find_viewport_tool_descriptor(tool_id), nullptr)
            << group.id << " references " << tool_id;
      }
    }

    for (const quader_host_contract::HostUiPlacementDescriptor& placement : quader_host_contract::ui_placement_catalog()) {
      if (placement.target_kind !=
          quader_host_contract::HostUiPlacementTargetKind::Tool) {
        continue;
      }
      const ToolDescriptor *tool =
          find_viewport_tool_descriptor(placement.target_id);
      ASSERT_NE(tool, nullptr)
          << placement.id << " targets " << placement.target_id;
      EXPECT_NE(quader_host_contract::find_tool_descriptor(placement.target_id),
                nullptr)
          << placement.target_id;
      EXPECT_EQ(placement.label, tool->label) << placement.id;
    }
}

TEST(ViewportToolRuntimeTests, RegistryFindsConcreteToolObjects) {
    ViewportToolRegistry registry;

    ASSERT_EQ(registry.find(ViewportToolId::Knife),
              find_viewport_tool(ViewportToolId::Knife));
    ASSERT_EQ(registry.find("mirror"),
              find_viewport_tool(ViewportToolId::Mirror));
    EXPECT_TRUE(registry.available(ViewportToolId::Mirror));
}

TEST(ViewportToolRuntimeTests, RuntimeOwnsActiveToolState) {
    ViewportToolRuntime runtime;
    ViewportToolState state;
    ViewportInputResult result;

    ASSERT_NE(runtime.active_tool(), nullptr);
    EXPECT_EQ(runtime.active_tool()->viewport_tool(), ViewportToolId::Select);

    ASSERT_TRUE(
        runtime.set_active_tool(state, ViewportToolId::Mirror, &result));
    ASSERT_NE(runtime.active_tool(), nullptr);
    EXPECT_TRUE(result.tool_changed);
    EXPECT_EQ(state.active_tool, ViewportToolId::Mirror);
    EXPECT_EQ(runtime.active_tool()->viewport_tool(), ViewportToolId::Mirror);
    EXPECT_TRUE(runtime.active_tool_owns_viewport());
    EXPECT_TRUE(runtime.active_tool_blocks_viewport_focus_shortcuts());
}

TEST(ViewportToolRuntimeTests, RuntimeDispatchesShortcutStateChanges) {
    ViewportToolRuntime runtime;
    ViewportToolState state;
    ViewportCameraController camera;
    ViewportInputResult result;

    ViewportShortcut shortcut;
    shortcut.kind = ViewportShortcutKind::ActivateTool;
    shortcut.tool = ViewportToolId::Move;
    ASSERT_TRUE(runtime.handle_shortcut(shortcut, state, camera, result));
    EXPECT_TRUE(result.tool_changed);
    EXPECT_EQ(state.active_tool, ViewportToolId::Move);
    EXPECT_EQ(runtime.active_tool()->viewport_tool(), ViewportToolId::Move);

    result = {};
    shortcut = {};
    shortcut.kind = ViewportShortcutKind::FocusDocument;
    ASSERT_TRUE(runtime.handle_shortcut(shortcut, state, camera, result));
    EXPECT_TRUE(result.focus_document_requested);

    result = {};
    ASSERT_TRUE(runtime.set_active_tool(state, ViewportToolId::Mirror));
    EXPECT_FALSE(runtime.handle_shortcut(shortcut, state, camera, result));
    EXPECT_FALSE(result.focus_document_requested);
}

TEST(ViewportToolRuntimeTests,
     NativeCoordinatorRegistersEveryCatalogDeclaredLifecycleExactlyOnce) {
  SceneSession session;
  const ViewportToolRuntime &runtime = session.native_tools().runtime();
  const std::size_t declared = static_cast<std::size_t>(std::ranges::count_if(
      viewport_tool_descriptors(), [](const ToolDescriptor &descriptor) {
        return descriptor.editor_owned_native_lifecycle;
      }));

  EXPECT_TRUE(runtime.native_lifecycle_catalog_complete());
  EXPECT_EQ(runtime.native_lifecycle_count(), declared);
  for (const ToolDescriptor &descriptor : viewport_tool_descriptors()) {
    EXPECT_EQ(runtime.native_lifecycle_registered(descriptor.tool),
              descriptor.editor_owned_native_lifecycle)
        << descriptor.id;
    EXPECT_EQ(session.native_tools().handles(descriptor.id),
              descriptor.editor_owned_native_lifecycle)
        << descriptor.id;
    EXPECT_FALSE(session.native_tools().active(descriptor.id))
        << descriptor.id;
    EXPECT_FALSE(session.native_tools().pending(descriptor.id))
        << descriptor.id;
  }
}

TEST(ViewportToolRuntimeTests,
     NativeLocalHistoryRemainsOwnedWhenEmptyAndNeverCancelsTheLifecycle) {
  ViewportToolRuntime runtime;
  bool active = true;
  bool redo_available = false;
  bool fail_undo = false;
  int accepted = 1;
  int cancels = 0;
  ViewportNativeToolLifecycleAdapter adapter{
      .tool = ViewportToolId::CreatePrimitive,
      .commit_label = "Apply test lifecycle",
      .can_activate = [] { return true; },
      .active = [&] { return active; },
      .pending = [&] { return active; },
      .pending_history_preview = [&] { return active; },
      .activate = [](double) { return command_changed_result(); },
      .commit = [] { return command_changed_result(); },
      .cancel = [&] {
        ++cancels;
        active = false;
        return command_changed_result();
      },
      .reset = [] {},
      .local_history = ViewportNativeToolHistoryAdapter{
          .label = "Accept test shape",
          .can_undo = [&] { return accepted != 0; },
          .can_redo = [&] { return redo_available; },
          .undo = [&] {
            if (fail_undo) {
              return command_invalid_result("Forced local history failure.");
            }
            if (accepted == 0) {
              return command_unchanged_result();
            }
            --accepted;
            redo_available = true;
            return command_changed_result();
          },
          .redo = [&] {
            if (!redo_available) {
              return command_unchanged_result();
            }
            ++accepted;
            redo_available = false;
            return command_changed_result();
          },
      },
  };
  ASSERT_TRUE(runtime.register_native_lifecycle(std::move(adapter)));
  const auto initial = runtime.active_native_lifecycle_history_state();
  ASSERT_TRUE(initial.has_value());
  EXPECT_TRUE(initial->can_undo);
  EXPECT_FALSE(initial->can_redo);
  EXPECT_EQ(initial->label, "Accept test shape");

  fail_undo = true;
  const CommandResult failed = runtime.undo_active_native_lifecycle();
  EXPECT_FALSE(failed.ok);
  EXPECT_FALSE(failed.changed);
  EXPECT_EQ(failed.message, "Forced local history failure.");
  EXPECT_EQ(accepted, 1);
  fail_undo = false;

  EXPECT_TRUE(runtime.undo_active_native_lifecycle().changed);
  const auto undone = runtime.active_native_lifecycle_history_state();
  ASSERT_TRUE(undone.has_value());
  EXPECT_FALSE(undone->can_undo);
  EXPECT_TRUE(undone->can_redo);
  EXPECT_FALSE(runtime.undo_active_native_lifecycle().changed);
  EXPECT_EQ(cancels, 0);
  EXPECT_TRUE(active);

  EXPECT_TRUE(runtime.redo_active_native_lifecycle().changed);
  EXPECT_EQ(accepted, 1);
  EXPECT_FALSE(runtime.redo_active_native_lifecycle().changed);
  EXPECT_TRUE(runtime.cancel_native_lifecycles().changed);
  EXPECT_EQ(cancels, 1);
  EXPECT_FALSE(runtime.active_native_lifecycle_history_state().has_value());
  EXPECT_FALSE(runtime.undo_active_native_lifecycle().ok);
  EXPECT_FALSE(runtime.redo_active_native_lifecycle().ok);
}

TEST(ViewportToolRuntimeTests,
     NativeCoordinatorActivatesAndCancelsEveryCatalogDeclaredLifecycle) {
  for (const ToolDescriptor &descriptor : viewport_tool_descriptors()) {
    if (!descriptor.editor_owned_native_lifecycle) {
      continue;
    }
    SCOPED_TRACE(std::string(descriptor.id));

    SceneSession session;
    QuaderEditorApi editor(
        QuaderApiSessionInternalAccess::from_scene_session(session));
    if (descriptor.tool == ViewportToolId::PathArray) {
      const QuaderApiResult<QuaderModelingObjectRef> curve =
          editor.modeling().create_curve({
              .points = {{.position = {0.0f, 0.0f, 0.0f}},
                         {.position = {4.0f, 1.0f, 0.0f}}},
              .name = "Native lifecycle matrix path",
          });
      ASSERT_TRUE(curve.ok()) << curve.error_message();
      ASSERT_TRUE(editor.selection().select_object(curve.value()).ok());
    } else {
      const QuaderApiResult<QuaderModelingObjectRef> created =
          editor.modeling().create_box(
              QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                           .max = {1.0f, 1.0f, 1.0f}},
              "Native lifecycle matrix box");
      ASSERT_TRUE(created.ok()) << created.error_message();
      if (descriptor.tool == ViewportToolId::Align) {
        const auto companion = editor.modeling().create_box(
            {.min = {3.0f, -1.0f, -1.0f}, .max = {5.0f, 1.0f, 1.0f}},
            "Native lifecycle matrix alignment target");
        ASSERT_TRUE(companion.ok()) << companion.error_message();
        ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Object).ok());
        ASSERT_TRUE(editor.selection().select_objects(
            {created.value().entity, companion.value().entity},
            created.value().entity).ok());
      } else {
        ASSERT_TRUE(editor.selection().select_object(created.value()).ok());
      }
    }

    if (descriptor.supported_editor_modes == EditorModeSupport::Edit) {
      ASSERT_TRUE(editor.mode().set(QuaderEditorMode::Edit).ok());
    }
    NativeViewportToolCoordinator &coordinator = session.native_tools();
    ASSERT_TRUE(coordinator.handles(descriptor.id));
    EXPECT_TRUE(coordinator.can_activate(descriptor.id));
    EXPECT_FALSE(coordinator.has_pending_tool());

    const QuaderApiVoidResult activated =
        editor.tools().activate(descriptor.tool);
    ASSERT_TRUE(activated.ok()) << activated.error_message();
    EXPECT_TRUE(activated.changed());
    EXPECT_EQ(coordinator.active_tool_id(), descriptor.id);
    const bool has_pending_operation = descriptor.tool != ViewportToolId::Align;
    EXPECT_EQ(coordinator.has_pending_tool(), has_pending_operation);
    EXPECT_EQ(coordinator.has_pending_history_preview(),
              descriptor.tool != ViewportToolId::Curve &&
                  descriptor.tool != ViewportToolId::Extrude &&
                  has_pending_operation);
    if (descriptor.tool == ViewportToolId::Align) {
      const auto settings = editor.tools().active_settings();
      EXPECT_TRUE(settings.active);
      EXPECT_EQ(settings.tool_id, "align");
      EXPECT_FALSE(settings.preview_active);
      EXPECT_FALSE(settings.apply_visible);
      EXPECT_FALSE(settings.can_apply);
      EXPECT_FALSE(settings.can_cancel);
    }
    const auto local_history = coordinator.active_history_state();
    EXPECT_EQ(local_history.has_value(),
              descriptor.tool == ViewportToolId::CreatePrimitive);
    if (local_history) {
      EXPECT_FALSE(local_history->can_undo);
      EXPECT_FALSE(local_history->can_redo);
      const QuaderHistorySnapshot history = editor.history().snapshot();
      EXPECT_FALSE(history.can_undo);
      EXPECT_FALSE(history.can_redo);
      EXPECT_TRUE(history.undo_label.empty());
      EXPECT_TRUE(history.redo_label.empty());
    }

    for (const ToolDescriptor &candidate : viewport_tool_descriptors()) {
      if (!candidate.editor_owned_native_lifecycle) {
        continue;
      }
      const bool expected = candidate.tool == descriptor.tool;
      EXPECT_EQ(coordinator.active(candidate.id), expected) << candidate.id;
      EXPECT_EQ(coordinator.pending(candidate.id),
                expected && has_pending_operation) << candidate.id;
    }

    const auto metadata = coordinator.active_commit_metadata();
    ASSERT_EQ(metadata.has_value(), has_pending_operation);
    if (metadata) {
      EXPECT_FALSE(metadata->label.empty());
      EXPECT_EQ(metadata->domain,
                (descriptor.tool == ViewportToolId::Pivot ||
                 descriptor.tool == ViewportToolId::PathArray)
                    ? ViewportNativeToolCommitDomain::Document
                    : ViewportNativeToolCommitDomain::Modeling);
    }

    const QuaderApiVoidResult repeated =
        editor.tools().activate(descriptor.tool);
    ASSERT_TRUE(repeated.ok()) << repeated.error_message();
    EXPECT_FALSE(repeated.changed());

    const QuaderApiVoidResult canceled =
        editor.tools().cancel_active_tool_preview();
    ASSERT_TRUE(canceled.ok()) << canceled.error_message();
    EXPECT_TRUE(canceled.changed());
    EXPECT_FALSE(coordinator.has_pending_tool());
    EXPECT_TRUE(coordinator.active_tool_id().empty());
    EXPECT_FALSE(coordinator.active_commit_metadata().has_value());
    EXPECT_FALSE(coordinator.active_history_state().has_value());
    for (const ToolDescriptor &candidate : viewport_tool_descriptors()) {
      if (!candidate.editor_owned_native_lifecycle) {
        continue;
      }
      EXPECT_FALSE(coordinator.active(candidate.id)) << candidate.id;
      EXPECT_FALSE(coordinator.pending(candidate.id)) << candidate.id;
    }
  }
}

TEST(ViewportToolRuntimeTests,
     CatalogToolTransitionsRetireOutgoingOwnersAcrossSelectionAndTransforms) {
  const std::array incoming_tools{
      ViewportToolId::Select, ViewportToolId::LassoSelect,
      ViewportToolId::Move, ViewportToolId::Rotate, ViewportToolId::Scale};
  for (const ToolDescriptor &outgoing : viewport_tool_descriptors()) {
    for (const ViewportToolId incoming_id : incoming_tools) {
      SCOPED_TRACE(std::string(outgoing.id));
      SCOPED_TRACE(viewport_tool_id(incoming_id));
      SceneSession session;
      QuaderEditorApi editor(
          QuaderApiSessionInternalAccess::from_scene_session(session));
      ASSERT_TRUE(editor.viewports().set_layout(ViewportLayoutId::FourQuad).ok());
      if (outgoing.tool == ViewportToolId::PathArray) {
        const auto curve = editor.modeling().create_curve({
            .points = {{.position = {}}, {.position = {4.0f, 0.0f, 0.0f}}},
            .name = "Tool transition path"});
        ASSERT_TRUE(curve.ok()) << curve.error_message();
        ASSERT_TRUE(editor.selection().select_object(curve.value()).ok());
      } else {
        const auto box = editor.modeling().create_box(
            {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
            "Tool transition source");
        ASSERT_TRUE(box.ok()) << box.error_message();
        if (outgoing.tool == ViewportToolId::Align) {
          const auto companion = editor.modeling().create_box(
              {.min = {3.0f, -1.0f, -1.0f}, .max = {5.0f, 1.0f, 1.0f}},
              "Tool transition companion");
          ASSERT_TRUE(companion.ok()) << companion.error_message();
          ASSERT_TRUE(editor.selection().select_objects(
              {box.value().entity, companion.value().entity},
              box.value().entity).ok());
        } else {
          ASSERT_TRUE(editor.selection().select_object(box.value()).ok());
        }
      }
      if (outgoing.supported_editor_modes == EditorModeSupport::Edit) {
        ASSERT_TRUE(editor.mode().set(QuaderEditorMode::Edit).ok());
      }
      session.history().clear();
      const auto entity_count_before = editor.query().entities().size();
      ASSERT_TRUE(editor.tools().activate(ViewportToolId::Rotate).ok());
      const auto activated = editor.commands().execute(
          editor.commands().command_id_for_tool_activation(outgoing.id));
      ASSERT_TRUE(activated.ok()) << activated.error_message();
      ASSERT_EQ(editor.tools().active_tool(), outgoing.id);
      EXPECT_EQ(editor.tools().active_native_tool_lifecycle(outgoing.tool),
                outgoing.editor_owned_native_lifecycle);
      EXPECT_EQ(session.modeling().tool_status().active_tool,
                outgoing.editor_owned_native_lifecycle
                    ? ModelingViewportTool::Select : outgoing.modeling_tool);
      const auto origin = editor.tools().active_settings_presentation_origin();
      const auto repeated = editor.tools().activate(outgoing.tool);
      ASSERT_TRUE(repeated.ok()) << repeated.error_message();
      EXPECT_FALSE(repeated.changed());
      EXPECT_EQ(editor.tools().active_settings_presentation_origin(), origin);
      EXPECT_EQ(session.history().undo_count(), 0u);

      const auto switched = editor.commands().execute(
          editor.commands().command_id_for_tool_activation(
              viewport_tool_id(incoming_id)));
      ASSERT_TRUE(switched.ok()) << switched.error_message();
      EXPECT_EQ(editor.tools().active_tool(), viewport_tool_id(incoming_id));
      const ToolDescriptor *incoming = find_viewport_tool_descriptor(incoming_id);
      ASSERT_NE(incoming, nullptr);
      EXPECT_EQ(session.modeling().tool_status().active_tool,
                incoming->modeling_tool);
      const auto settings = editor.tools().active_settings();
      if (incoming_id == ViewportToolId::Move ||
          incoming_id == ViewportToolId::LassoSelect) {
        EXPECT_TRUE(settings.active);
        EXPECT_EQ(settings.tool_id, incoming->id);
        EXPECT_FALSE(settings.preview_active);
      } else if (incoming_id == ViewportToolId::Rotate ||
                 incoming_id == ViewportToolId::Scale) {
        EXPECT_FALSE(settings.active);
        EXPECT_TRUE(settings.controls.empty());
      }
      EXPECT_FALSE(session.native_tools().has_pending_tool());
      EXPECT_TRUE(session.native_tools().active_tool_id().empty());
      for (const ToolDescriptor &candidate : viewport_tool_descriptors()) {
        if (candidate.editor_owned_native_lifecycle) {
          EXPECT_FALSE(session.native_tools().active(candidate.id))
              << candidate.id;
          EXPECT_FALSE(session.native_tools().pending(candidate.id))
              << candidate.id;
        }
      }
      for (const ViewportPaneId pane :
           {ViewportPaneId::Perspective, ViewportPaneId::Top,
            ViewportPaneId::Front, ViewportPaneId::Side}) {
        EXPECT_EQ(editor.viewports().selected_tool(pane), incoming_id);
      }
      if (outgoing.tool == ViewportToolId::LinearArray) {
        EXPECT_GT(editor.query().entities().size(), entity_count_before);
        EXPECT_EQ(session.history().undo_count(), 1u);
        EXPECT_EQ(editor.history().snapshot().undo_label, "Linear Array");
        EXPECT_TRUE(editor.render().tool_preview_meshes().empty());
      } else {
        EXPECT_EQ(editor.query().entities().size(), entity_count_before);
        EXPECT_EQ(session.history().undo_count(), 0u);
      }
    }
  }
}

TEST(ViewportToolRuntimeTests,
     NativeLifecycleInterruptionMatrixPreservesPersistentStateExceptEscape) {
  enum class Interruption {
    FocusLoss,
    CaptureLoss,
    Navigation,
    Escape,
    OutsidePointer,
  };
  struct InterruptionCase final {
    Interruption interruption;
    std::string_view label;
    bool cancels_lifecycle = false;
  };
  const std::array cases{
      InterruptionCase{Interruption::FocusLoss, "focus loss"},
      InterruptionCase{Interruption::CaptureLoss, "capture loss"},
      InterruptionCase{Interruption::Navigation, "navigation"},
      InterruptionCase{Interruption::Escape, "Escape", true},
      InterruptionCase{Interruption::OutsidePointer, "outside pointer"},
  };

  for (const ToolDescriptor &descriptor : viewport_tool_descriptors()) {
    if (!descriptor.editor_owned_native_lifecycle) {
      continue;
    }
    for (const InterruptionCase &scenario : cases) {
      SCOPED_TRACE(std::string(descriptor.id));
      SCOPED_TRACE(std::string(scenario.label));

      SceneSession session;
      QuaderEditorApi editor(
          QuaderApiSessionInternalAccess::from_scene_session(session));
      if (descriptor.tool == ViewportToolId::PathArray) {
        const QuaderApiResult<QuaderModelingObjectRef> curve =
            editor.modeling().create_curve({
                .points = {{.position = {0.0f, 0.0f, 0.0f}},
                           {.position = {4.0f, 1.0f, 0.0f}}},
                .name = "Native interruption matrix path",
            });
        ASSERT_TRUE(curve.ok()) << curve.error_message();
        ASSERT_TRUE(editor.selection().select_object(curve.value()).ok());
      } else {
        const QuaderApiResult<QuaderModelingObjectRef> created =
            editor.modeling().create_box(
                QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                             .max = {1.0f, 1.0f, 1.0f}},
                "Native interruption matrix box");
        ASSERT_TRUE(created.ok()) << created.error_message();
        if (descriptor.tool == ViewportToolId::Align) {
          const auto companion = editor.modeling().create_box(
              {.min = {3.0f, -1.0f, -1.0f}, .max = {5.0f, 1.0f, 1.0f}},
              "Native interruption matrix alignment target");
          ASSERT_TRUE(companion.ok()) << companion.error_message();
          ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Object).ok());
          ASSERT_TRUE(editor.selection().select_objects(
              {created.value().entity, companion.value().entity},
              created.value().entity).ok());
        } else {
          ASSERT_TRUE(editor.selection().select_object(created.value()).ok());
        }
      }
      if (descriptor.supported_editor_modes == EditorModeSupport::Edit) {
        ASSERT_TRUE(editor.mode().set(QuaderEditorMode::Edit).ok());
      }
      ASSERT_TRUE(editor.tools().activate(descriptor.tool).ok());

      NativeViewportToolCoordinator &coordinator = session.native_tools();
      ASSERT_TRUE(coordinator.active(descriptor.id));
      std::vector<std::pair<EntityId, TransformComponent>> committed_align_transforms;
      if (descriptor.tool == ViewportToolId::Align) {
        session.history().clear();
        const auto aligned = session.align_tool().set_alignment(
            MeshArrangementAxis::X, MeshAlignmentMode::Minimum);
        ASSERT_TRUE(aligned.ok) << aligned.message;
        ASSERT_TRUE(aligned.changed);
        EXPECT_EQ(session.history().undo_count(), 1u);
        EXPECT_FALSE(coordinator.has_pending_tool());
        EXPECT_FALSE(coordinator.has_pending_history_preview());
        EXPECT_FALSE(coordinator.active_commit_metadata().has_value());
        for (const EntityId entity : session.scene().selected_entities()) {
          const auto *transform = session.scene().transform(entity);
          ASSERT_NE(transform, nullptr);
          committed_align_transforms.emplace_back(entity, *transform);
        }
      }
      ViewportInputFrame input;
      input.viewport_hovered = true;
      input.viewport_focused = true;
      input.viewport_size = {800.0f, 600.0f};
      input.mouse_local = {400.0f, 300.0f};
      input.mouse_screen = input.mouse_local;
      ViewportInputResult input_result;
      ViewportCameraController camera;
      camera.set_viewport_size(800.0f, 600.0f);
      camera.focus_origin(8.0f);

      const bool exercises_pointer_gesture =
          scenario.interruption != Interruption::OutsidePointer &&
          (descriptor.tool == ViewportToolId::Curve ||
           descriptor.tool == ViewportToolId::LinearArray ||
           descriptor.tool == ViewportToolId::Pivot);
      QVec2 gesture_local = input.mouse_local;
      std::optional<QVec3> handle_position_before;
      std::optional<QVec3> handle_position_authored;
      std::optional<std::size_t> curve_point_count_before;
      if (exercises_pointer_gesture) {
        const NativeViewportToolPresentationSnapshot before =
            coordinator.presentation_snapshot();
        if (descriptor.tool == ViewportToolId::LinearArray) {
          ASSERT_TRUE(before.linear_array_handle.visible);
          handle_position_before = before.linear_array_handle.endpoint_world;
          const ViewportScreenPoint screen = camera.project_world_point(
              before.linear_array_handle.endpoint_world);
          ASSERT_TRUE(screen.visible);
          gesture_local = screen.position;
        } else if (descriptor.tool == ViewportToolId::Pivot) {
          ASSERT_TRUE(before.pivot_handle.visible);
          handle_position_before = before.pivot_handle.world_position;
          const ViewportScreenPoint screen =
              camera.project_world_point(before.pivot_handle.world_position);
          ASSERT_TRUE(screen.visible);
          gesture_local = screen.position;
        } else {
          ASSERT_NE(before.curve_preview, nullptr);
          ASSERT_TRUE(before.curve_preview->document.has_value());
          curve_point_count_before =
              before.curve_preview->document->point_count();
        }

        constexpr std::uint64_t kPressSequence = 200u;
        input.mouse_local = gesture_local;
        input.mouse_screen = gesture_local;
        input.pointer_pressed_edges = {{
            .button = QuaderMouseButton::Left,
            .local = gesture_local,
            .sequence = kPressSequence,
        }};
        const ViewportNativeToolInteractionResult press =
            coordinator.handle_pointer_frame(
                session.scene(), session.assets(),
                &session.point_placement(), camera,
                {.input = input,
                 .input_result = input_result,
                 .left_pressed = true,
                 .left_down = true,
                 .left_click_local = gesture_local,
                 .pointer_drag_threshold_pixels = 4.0f});
        EXPECT_TRUE(press.consumed);
        ASSERT_TRUE(coordinator.pointer_dragging());

        gesture_local.x += 32.0f;
        input.mouse_local = gesture_local;
        input.mouse_screen = gesture_local;
        input.pointer_pressed_edges.clear();
        (void)coordinator.handle_pointer_frame(
            session.scene(), session.assets(),
            &session.point_placement(), camera,
            {.input = input,
             .input_result = input_result,
             .left_down = true,
             .pointer_drag_threshold_pixels = 4.0f});

        // An older release may be coalesced beside a newer press. It belongs
        // to the older gesture and must neither commit nor clear this one.
        input.pointer_released_edges = {{
            .button = QuaderMouseButton::Left,
            .local = gesture_local,
            .sequence = kPressSequence - 1u,
        }};
        (void)coordinator.handle_pointer_frame(
            session.scene(), session.assets(),
            &session.point_placement(), camera,
            {.input = input,
             .input_result = input_result,
             .left_down = true,
             .left_released = true,
             .pointer_drag_threshold_pixels = 4.0f});
        EXPECT_TRUE(coordinator.pointer_dragging());
        input.pointer_released_edges.clear();
        const NativeViewportToolPresentationSnapshot authored =
            coordinator.presentation_snapshot();
        if (descriptor.tool == ViewportToolId::LinearArray) {
          handle_position_authored = authored.linear_array_handle.endpoint_world;
        } else if (descriptor.tool == ViewportToolId::Pivot) {
          handle_position_authored = authored.pivot_handle.world_position;
        }
      }

      switch (scenario.interruption) {
      case Interruption::FocusLoss:
        input.viewport_focused = false;
        input.pointer_interaction_canceled = true;
        break;
      case Interruption::CaptureLoss:
        input.pointer_interaction_canceled = true;
        break;
      case Interruption::Navigation:
        input.navigation_active = true;
        input_result.navigation_active = true;
        break;
      case Interruption::Escape:
        input_result.cancel_requested = true;
        break;
      case Interruption::OutsidePointer: {
        // A panel click dispatches no tool event. Merely interacting with the
        // surrounding UI must retain the pending operation and its controls.
        break;
      }
      }

      if (scenario.interruption != Interruption::OutsidePointer) {
        (void)coordinator.handle_pointer_frame(
            session.scene(), session.assets(),
            &session.point_placement(), camera,
            {.input = input,
             .input_result = input_result,
             .left_down = exercises_pointer_gesture,
             .pointer_drag_threshold_pixels = 4.0f});
      }
      if (scenario.interruption == Interruption::Escape &&
          descriptor.tool != ViewportToolId::UvPeel &&
          descriptor.tool != ViewportToolId::Extrude &&
          descriptor.tool != ViewportToolId::Align) {
        const QuaderApiVoidResult canceled =
            editor.tools().cancel_active_tool_preview();
        ASSERT_TRUE(canceled.ok()) << canceled.error_message();
        EXPECT_TRUE(canceled.changed());
      }

      EXPECT_EQ(coordinator.active(descriptor.id),
                !scenario.cancels_lifecycle);
      const bool retains_pending_operation =
          !scenario.cancels_lifecycle && descriptor.tool != ViewportToolId::Align;
      EXPECT_EQ(coordinator.pending(descriptor.id),
                retains_pending_operation);
      EXPECT_EQ(coordinator.has_pending_tool(),
                retains_pending_operation);
      EXPECT_FALSE(coordinator.pointer_dragging());
      if (descriptor.tool == ViewportToolId::Align) {
        EXPECT_EQ(session.history().undo_count(), 1u);
        EXPECT_FALSE(coordinator.has_pending_history_preview());
        for (const auto &[entity, transform] : committed_align_transforms) {
          const auto *actual = session.scene().transform(entity);
          ASSERT_NE(actual, nullptr);
          EXPECT_TRUE(document_command_same_transform(*actual, transform));
        }
      }

      if (exercises_pointer_gesture && !scenario.cancels_lifecycle) {
        const NativeViewportToolPresentationSnapshot after =
            coordinator.presentation_snapshot();
        // Camera navigation releases the pointer gesture without reverting its
        // authored preview. Capture/focus interruption retains its old policy.
        const auto &expected_handle_position =
            scenario.interruption == Interruption::Navigation
                ? handle_position_authored : handle_position_before;
        if (descriptor.tool == ViewportToolId::LinearArray) {
          ASSERT_TRUE(expected_handle_position.has_value());
          EXPECT_FLOAT_EQ(after.linear_array_handle.endpoint_world.x,
                          expected_handle_position->x);
          EXPECT_FLOAT_EQ(after.linear_array_handle.endpoint_world.y,
                          expected_handle_position->y);
          EXPECT_FLOAT_EQ(after.linear_array_handle.endpoint_world.z,
                          expected_handle_position->z);
        } else if (descriptor.tool == ViewportToolId::Pivot) {
          ASSERT_TRUE(expected_handle_position.has_value());
          EXPECT_FLOAT_EQ(after.pivot_handle.world_position.x,
                          expected_handle_position->x);
          EXPECT_FLOAT_EQ(after.pivot_handle.world_position.y,
                          expected_handle_position->y);
          EXPECT_FLOAT_EQ(after.pivot_handle.world_position.z,
                          expected_handle_position->z);
        } else {
          ASSERT_NE(after.curve_preview, nullptr);
          ASSERT_TRUE(after.curve_preview->document.has_value());
          ASSERT_TRUE(curve_point_count_before.has_value());
          EXPECT_EQ(after.curve_preview->document->point_count(),
                    *curve_point_count_before + 1u);
        }
      }

      if (exercises_pointer_gesture) {
        input.viewport_focused = true;
        input.pointer_interaction_canceled = false;
        input.navigation_active = false;
        input_result = {};
        input.pointer_released_edges = {{
            .button = QuaderMouseButton::Left,
            .local = gesture_local,
            .sequence = 201u,
        }};
        const ViewportNativeToolInteractionResult canceled_release =
            coordinator.handle_pointer_frame(
                session.scene(), session.assets(),
                &session.point_placement(), camera,
                {.input = input,
                 .input_result = input_result,
                 .left_released = true,
                 .pointer_drag_threshold_pixels = 4.0f});
        EXPECT_TRUE(canceled_release.consumed);
        EXPECT_FALSE(coordinator.pointer_dragging());
      }
    }
  }
}

TEST(ViewportToolRuntimeTests,
     NativeLifecyclePreflightsBeforeCancelingTheActiveTool) {
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));

  ASSERT_TRUE(editor.tools().activate(ViewportToolId::Curve).ok());
  ASSERT_TRUE(session.curve().active());
  EXPECT_TRUE(session.native_tools().active("curve"));
  EXPECT_TRUE(session.native_tools().pending("curve"));
  EXPECT_FALSE(session.native_tools().active("create_primitive"));
  EXPECT_EQ(session.native_tools().active_tool_id(), std::string_view{"curve"});

  const auto metadata = session.native_tools().active_commit_metadata();
  ASSERT_TRUE(metadata.has_value());
  EXPECT_EQ(metadata->label, "Apply Curve");
  EXPECT_EQ(metadata->domain, ViewportNativeToolCommitDomain::Modeling);

  const QuaderApiVoidResult rejected =
      editor.tools().activate(ViewportToolId::LinearArray);
  EXPECT_FALSE(rejected.ok());
  EXPECT_TRUE(session.curve().active());
  EXPECT_EQ(session.native_tools().active_tool_id(), std::string_view{"curve"});

  EXPECT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  EXPECT_FALSE(session.native_tools().has_pending_tool());
}

TEST(ViewportToolRuntimeTests,
     ImplicitSettlementRetiresEmptyNativeToolsWithoutAuthoredHistory) {
  for (const ViewportToolId tool :
       {ViewportToolId::Curve, ViewportToolId::CreatePrimitive,
        ViewportToolId::RadialArray, ViewportToolId::PathArray,
        ViewportToolId::Pivot, ViewportToolId::UvPeel, ViewportToolId::Extrude}) {
    SCOPED_TRACE(static_cast<int>(tool));
    SceneSession session;
    QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
    if (tool == ViewportToolId::PathArray) {
      const auto curve = editor.modeling().create_curve({
          .points = {{.position = {}}, {.position = {4.0f, 0.0f, 0.0f}}},
          .name = "Empty lifecycle path"});
      ASSERT_TRUE(curve.ok()) << curve.error_message();
      ASSERT_TRUE(editor.selection().select_object(curve.value()).ok());
    } else {
      const auto box = editor.modeling().create_box(
          {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
          "Empty lifecycle source");
      ASSERT_TRUE(box.ok()) << box.error_message();
      ASSERT_TRUE(editor.selection().select_object(box.value()).ok());
    }
    if (tool == ViewportToolId::UvPeel || tool == ViewportToolId::Extrude) {
      ASSERT_TRUE(editor.mode().set(QuaderEditorMode::Edit).ok());
    }
    session.history().clear();
    const auto entities_before = session.scene().entities().size();
    ASSERT_TRUE(editor.tools().activate(tool).ok());
    const auto switched = editor.tools().activate(ViewportToolId::Move);
    EXPECT_TRUE(switched.ok()) << switched.error_message();
    EXPECT_FALSE(session.native_tools().has_pending_tool());
    EXPECT_EQ(session.scene().entities().size(), entities_before);
    EXPECT_FALSE(session.history().can_undo());
    EXPECT_EQ(editor.tools().active_tool(), "move");
  }
}

TEST(ViewportToolRuntimeTests,
     CurveToolSwitchRejectsIncompleteDraftThenAcceptsCompleteDraftWithHistory) {
  SceneSession session;
  QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::Curve).ok());
  ASSERT_TRUE(session.curve().begin_point({}).ok);
  ASSERT_TRUE(session.curve().commit_drag().ok);
  const auto rejected = editor.tools().activate(ViewportToolId::Move);
  EXPECT_FALSE(rejected.ok());
  EXPECT_FALSE(rejected.changed());
  EXPECT_EQ(session.curve().point_count(), 1u);
  EXPECT_TRUE(session.curve().active());
  EXPECT_TRUE(session.scene().entities().empty());
  EXPECT_FALSE(session.history().can_undo());

  ASSERT_TRUE(session.curve().begin_point({4.0f, 0.0f, 0.0f}).ok);
  const auto switched = editor.tools().activate(ViewportToolId::Move);
  ASSERT_TRUE(switched.ok()) << switched.error_message();
  EXPECT_FALSE(session.curve().active());
  EXPECT_FALSE(session.curve().dragging());
  ASSERT_EQ(session.scene().entities().size(), 1u);
  EXPECT_EQ(session.history().undo_count(), 1u);
  ASSERT_TRUE(editor.history().undo().ok());
  EXPECT_TRUE(session.scene().entities().empty());
  ASSERT_TRUE(editor.history().redo().ok());
  EXPECT_EQ(session.scene().entities().size(), 1u);
}

TEST(ViewportToolRuntimeTests,
     FailedLinearArraySettlementPreservesPreviewUntilRetryOrExplicitCancel) {
  SceneSession session;
  QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto box = editor.modeling().create_box(
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      "Array acceptance source");
  ASSERT_TRUE(box.ok()) << box.error_message();
  ASSERT_TRUE(editor.selection().select_object(box.value()).ok());
  session.history().clear();
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::LinearArray).ok());
  session.modeling().set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::AfterSdkMutation);
  const auto failed = editor.tools().activate(ViewportToolId::Move);
  EXPECT_FALSE(failed.ok());
  EXPECT_EQ(editor.tools().active_tool(), "linear_array");
  EXPECT_TRUE(session.native_tools().has_pending_tool());
  EXPECT_EQ(session.scene().entities().size(), 1u);
  EXPECT_FALSE(session.history().can_undo());
  session.modeling().set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::None);
  const auto retried = editor.tools().activate(ViewportToolId::Move);
  ASSERT_TRUE(retried.ok()) << retried.error_message();
  EXPECT_FALSE(session.native_tools().has_pending_tool());
  EXPECT_GT(session.scene().entities().size(), 1u);
  EXPECT_EQ(session.history().undo_count(), 1u);
  ASSERT_TRUE(editor.history().undo().ok());
  EXPECT_EQ(session.scene().entities().size(), 1u);
}

TEST(ViewportToolRuntimeTests,
     KnifeShortcutPreservesSelectionModeInRuntimeAndInputRouter) {
  const std::array modes{
      ViewportSelectionMode::Vertex,
      ViewportSelectionMode::Edge,
      ViewportSelectionMode::Face,
      ViewportSelectionMode::Object,
      ViewportSelectionMode::Group,
  };
  const ViewportShortcut shortcut{
      .kind = ViewportShortcutKind::ActivateTool,
      .tool = ViewportToolId::Knife,
  };

  for (const ViewportSelectionMode mode : modes) {
    SCOPED_TRACE(static_cast<int>(mode));

    ViewportToolRuntime runtime;
    ViewportToolState runtime_state;
    runtime_state.selection_mode = mode;
    ViewportCameraController runtime_camera;
    ViewportInputResult runtime_result;
    ASSERT_TRUE(runtime.handle_shortcut(shortcut, runtime_state,
                                        runtime_camera, runtime_result));
    EXPECT_EQ(runtime_state.active_tool, ViewportToolId::Knife);
    EXPECT_EQ(runtime_state.selection_mode, mode);
    EXPECT_FALSE(runtime_result.selection_mode_changed);

    ViewportInputRouter router;
    router.set_selection_mode(mode);
    ViewportCameraController router_camera;
    ViewportInputFrame input;
    input.viewport_hovered = true;
    input.viewport_focused = true;
    input.viewport_size = {800.0f, 600.0f};
    input.resolved_shortcuts.push_back(shortcut);
    const ViewportInputResult router_result =
        router.handle_frame(input, router_camera);
    EXPECT_TRUE(router_result.shortcut_handled);
    EXPECT_EQ(router.state().active_tool, ViewportToolId::Knife);
    EXPECT_EQ(router.state().selection_mode, mode);
    EXPECT_FALSE(router_result.selection_mode_changed);
  }
}

TEST(ViewportToolRuntimeTests,
     RepeatedPivotShortcutRequestsNativeToolActivation) {
  ViewportToolRuntime runtime;
  ViewportToolState state;
  ViewportCameraController camera;
  ViewportShortcut shortcut;
  shortcut.kind = ViewportShortcutKind::ActivateTool;
  shortcut.tool = ViewportToolId::Pivot;

  ViewportInputResult result;
  ASSERT_TRUE(runtime.handle_shortcut(shortcut, state, camera, result));
  EXPECT_TRUE(result.tool_activation_requested);
  EXPECT_TRUE(result.tool_changed);
  EXPECT_EQ(state.active_tool, ViewportToolId::Pivot);

  result = {};
  ASSERT_TRUE(runtime.handle_shortcut(shortcut, state, camera, result));
  EXPECT_TRUE(result.tool_activation_requested);
  EXPECT_FALSE(result.tool_changed);
  EXPECT_EQ(state.active_tool, ViewportToolId::Pivot);
}

TEST(ViewportToolRuntimeTests,
     RuntimeSynchronizesActualToolExitDespiteRetainIntent) {
    ViewportToolRuntime runtime;
    ViewportToolState state;
    ViewportCameraController camera;
    SceneDocument document;
    AssetRegistry assets;
    ModelingSessionService modeling(document, assets);
    ViewportModelingToolPortAdapter tool_ports(modeling);

    ASSERT_TRUE(runtime.set_active_tool(state, ViewportToolId::Knife));
    ASSERT_TRUE(runtime.activate_active_tool(modeling).ok);
    ViewportToolContext context{
            .state = state,
            .camera = camera,
            .ports = tool_ports.ports(),
    };
    bool changed_tool = false;
    context.set_active_tool = [&](ViewportToolId tool) {
      state.active_tool = tool;
      changed_tool = true;
    };
    const ViewportToolResult result = runtime.handle_active_tool_event(
        state, context,
        ViewportToolEvent{
            .kind = ViewportToolEventKind::Cancel,
            .tool = ViewportToolId::Knife,
            .exit_active_modal_tool = false,
        });

    EXPECT_TRUE(result.handled);
    EXPECT_TRUE(result.consumed);
    EXPECT_TRUE(result.sync_active_tool_from_modeling);
    EXPECT_FALSE(changed_tool);
    EXPECT_EQ(state.active_tool, ViewportToolId::Select);
    ASSERT_NE(runtime.active_tool(), nullptr);
    EXPECT_EQ(runtime.active_tool()->viewport_tool(), ViewportToolId::Select);
}

TEST(ViewportToolRuntimeTests,
     KnifeClickUsesQueuedPhysicalPressCoordinateWhenMouseMoveSharesFrame) {
  expect_point_tool_click_uses_queued_press_coordinate(ViewportToolId::Knife,
                                                       false);
}

TEST(ViewportToolRuntimeTests,
     PolyClickUsesQueuedPhysicalPressCoordinateWhenMouseMoveSharesFrame) {
  expect_point_tool_click_uses_queued_press_coordinate(ViewportToolId::Poly,
                                                       false);
}

TEST(ViewportToolRuntimeTests,
     InsertEdgeLoopClickUsesQueuedPhysicalPressCoordinateWhenMouseMoveSharesFrame) {
  expect_point_tool_click_uses_queued_press_coordinate(
      ViewportToolId::InsertEdgeLoop, true);
}

TEST(ViewportToolRuntimeTests,
     InsertEdgeLoopHoverPublishesPreviewBeforeANonQuadTermination) {
  SceneDocument document;
  AssetRegistry assets;
  ModelingSessionService modeling(document, assets);

  quader_poly::Document source;
  const quader_poly::ElementId bottom_left =
      quader_poly::add_vertex(source, {0.0F, 0.0F, 0.0F});
  const quader_poly::ElementId bottom_right =
      quader_poly::add_vertex(source, {1.0F, 0.0F, 0.0F});
  const quader_poly::ElementId shared_right =
      quader_poly::add_vertex(source, {1.0F, 1.0F, 0.0F});
  const quader_poly::ElementId shared_left =
      quader_poly::add_vertex(source, {0.0F, 1.0F, 0.0F});
  const quader_poly::ElementId triangle_tip =
      quader_poly::add_vertex(source, {0.5F, 2.0F, 0.0F});
  ASSERT_NE(quader_poly::add_face(
                source, std::array{bottom_left, bottom_right, shared_right,
                                   shared_left}),
            quader_poly::kInvalidElementId);
  ASSERT_NE(quader_poly::add_face(
                source, std::array{shared_left, shared_right, triangle_tip}),
            quader_poly::kInvalidElementId);

  const ModelingCreateBoxResult mesh = modeling.create_mesh({
      .document = source,
      .name = "Quad Strip With Non-Quad Termination",
  });
  ASSERT_TRUE(mesh.ok) << mesh.message;

  ViewportToolRuntime runtime;
  ViewportToolState state;
  ViewportCameraController camera;
  ViewportModelingToolPortAdapter tool_ports(modeling);
  RejectingToolCommitPort commit_port;
  ASSERT_TRUE(runtime.set_active_tool(state, ViewportToolId::InsertEdgeLoop));
  ASSERT_TRUE(runtime.activate_active_tool(modeling).ok);

  ViewportPointToolContext point_context;
  point_context.make_event_at = [](QVec2 position, bool dragging) {
    return ModelingToolRayEvent{
        .screen_position = position,
        .ray =
            {
                .origin = {0.5F, 0.0F, 5.0F},
                .direction = {0.0F, 0.0F, -1.0F},
            },
        .camera =
            {
                .orthographic = true,
                .origin = {0.5F, 0.0F, 5.0F},
                .x_axis = {1.0F, 0.0F, 0.0F},
                .y_axis = {0.0F, 1.0F, 0.0F},
                .z_axis = {0.0F, 0.0F, 1.0F},
                .viewport_size = {100.0F, 100.0F},
                .orthographic_size = 100.0,
                .fov_degrees = 60.0,
            },
        .grid_size = 1.0,
        .vertex_radius = 0.3,
        .edge_radius = 0.2,
        .snap_enabled = false,
        .dragging = dragging,
    };
  };
  ViewportToolContext context{
      .state = state,
      .camera = camera,
      .ports = tool_ports.ports(&commit_port),
      .point = &point_context,
  };

  const ViewportToolResult result = runtime.handle_active_tool_event(
      state, context,
      ViewportToolEvent{
          .kind = ViewportToolEventKind::Frame,
          .tool = ViewportToolId::InsertEdgeLoop,
          .pointer =
              {
                  .hovered = true,
                  .can_interact = true,
                  .mouse_local = {50.0F, 50.0F},
                  .viewport_size = {100.0F, 100.0F},
              },
      });

  EXPECT_TRUE(result.handled);
  EXPECT_TRUE(modeling.tool_status().insert_edge_loop_has_preview);
}

TEST(ViewportToolRuntimeTests,
     InsertEdgeLoopHoverFromInsideFlippedMeshPublishesPreview) {
  SceneDocument document;
  AssetRegistry assets;
  ModelingSessionService modeling(document, assets);

  quader_poly::Document source = quader_poly::make_cube({});
  for (quader_poly::Face &face : source.faces) {
    std::reverse(face.vertices.begin(), face.vertices.end());
    std::reverse(face.uvs.begin(), face.uvs.end());
  }
  const ModelingCreateBoxResult mesh = modeling.create_mesh({
      .document = source,
      .name = "Inside Flipped Edge Loop Source",
  });
  ASSERT_TRUE(mesh.ok) << mesh.message;

  ViewportToolRuntime runtime;
  ViewportToolState state;
  ViewportCameraController camera;
  ViewportModelingToolPortAdapter tool_ports(modeling);
  RejectingToolCommitPort commit_port;
  ASSERT_TRUE(runtime.set_active_tool(state, ViewportToolId::InsertEdgeLoop));
  ASSERT_TRUE(runtime.activate_active_tool(modeling).ok);

  constexpr float kInvSqrtTwo = 0.70710678118F;
  ViewportPointToolContext point_context;
  point_context.make_event_at = [](QVec2 position, bool dragging) {
    return ModelingToolRayEvent{
        .screen_position = position,
        .ray =
            {
                .origin = {0.0F, 0.0F, 0.0F},
                .direction = {kInvSqrtTwo, 0.0F, kInvSqrtTwo},
            },
        .camera =
            {
                .orthographic = false,
                .origin = {0.0F, 0.0F, 0.0F},
                .x_axis = {kInvSqrtTwo, 0.0F, -kInvSqrtTwo},
                .y_axis = {0.0F, 1.0F, 0.0F},
                .z_axis = {-kInvSqrtTwo, 0.0F, -kInvSqrtTwo},
                .viewport_size = {800.0F, 600.0F},
                .orthographic_size = 100.0,
                .fov_degrees = 60.0,
            },
        .grid_size = 1.0,
        .vertex_radius = 0.2,
        .edge_radius = 0.2,
        .snap_enabled = false,
        .dragging = dragging,
    };
  };
  ViewportToolContext context{
      .state = state,
      .camera = camera,
      .ports = tool_ports.ports(&commit_port),
      .point = &point_context,
  };

  const ViewportToolResult result = runtime.handle_active_tool_event(
      state, context,
      ViewportToolEvent{
          .kind = ViewportToolEventKind::Frame,
          .tool = ViewportToolId::InsertEdgeLoop,
          .pointer =
              {
                  .hovered = true,
                  .can_interact = true,
                  .mouse_local = {400.0F, 300.0F},
                  .viewport_size = {800.0F, 600.0F},
              },
      });

  EXPECT_TRUE(result.handled);
  EXPECT_TRUE(modeling.tool_status().insert_edge_loop_has_preview);
  const ViewportOverlayFrame overlay =
      test_support::visible_modeling_overlay_frame(modeling);
  EXPECT_TRUE(std::ranges::any_of(
      overlay.lines, [](const ViewportOverlayLine &line) {
        return line.source == ViewportOverlaySource::Tool ||
               line.layer == ViewportOverlayLayer::HoverEdge;
      }));
}

TEST(ViewportToolRuntimeTests, BoxToolAcceptUsesCommandOwnedTransaction) {
    SceneSession session;
    QuaderEditorApi editor(
        QuaderApiSessionInternalAccess::from_scene_session(session));
    ViewportToolState state;
    ViewportCameraController camera;
    ViewportModalToolState modal_state;

    const QuaderApiResult<QuaderAssetRef> material =
        editor.assets().add_material(QuaderMaterialArgs{
            .name = "Viewport Box Material",
            .base_color = {0.2f, 0.4f, 0.8f},
        });
    ASSERT_TRUE(material.ok) << material.message;

    const ViewportTool *box_tool =
        find_viewport_tool(ViewportToolId::CreatePrimitive);
    ASSERT_NE(box_tool, nullptr);
    ASSERT_TRUE(
        editor.tools().activate(ViewportToolId::CreatePrimitive).ok());

    const ModelingBoxToolEvent start{
        .screen_position = {400.0f, 300.0f},
        .ray =
            {
                .origin = {4.0f, 4.0f, 4.0f},
                .direction = {0.0f, -1.0f, 0.0f},
            },
        .grid_size = 1.0,
    };
    const ModelingBoxToolEvent footprint_end{
        .screen_position = {400.0f, 300.0f},
        .ray =
            {
                .origin = {6.0f, 4.0f, 7.0f},
                .direction = {0.0f, -1.0f, 0.0f},
            },
        .grid_size = 1.0,
    };
    ModelingBoxToolEvent current_event = start;
    EntityId committed_entity = kInvalidEntityId;
    int commit_count = 0;

    ViewportBoxToolContext box_context{
        .default_material = material.value.id,
    };
    box_context.make_event_at = [&](QVec2) { return current_event; };
    box_context.accept_preview = [&](std::string name,
                                     AssetId default_material,
                                     bool snap_enabled) {
      ++commit_count;
      const QuaderApiVoidResult result =
          editor.tools().accept_active_primitive(
              std::move(name), default_material, snap_enabled);
      if (result.ok && result.changed) {
        committed_entity = session.scene().primary_selection();
      }
      return ModelingCommandResult{
          .ok = result.ok,
          .changed = result.changed,
          .message = result.message,
      };
    };
    box_context.session_active = [&]() {
      return editor.tools().has_active_primitive_creation();
    };
    box_context.next_mesh_name = [] { return std::string{"Viewport Box"}; };

    ViewportModelingToolPortCallbacks callbacks;
    callbacks.set_selection_mode = [&](ViewportSelectionMode mode) {
      state.selection_mode = mode;
    };
    callbacks.set_app_selection_mode = [&](ModelingSelectionMode mode) {
      (void)editor.selection().set_mode(to_api_selection_mode(mode));
    };
    ViewportModelingToolPortAdapter tool_ports(session.modeling(),
                                               std::move(callbacks));
    ViewportToolContext context{
        .state = state,
        .camera = camera,
        .ports = tool_ports.ports(),
        .document = &session.scene(),
        .modal_state = &modal_state,
        .box = &box_context,
    };

    auto dispatch_frame = [&](ViewportPointerToolEvent pointer) {
      return box_tool->handle_event(context,
                                    ViewportToolEvent{
                                        .kind = ViewportToolEventKind::Frame,
                                        .tool = ViewportToolId::CreatePrimitive,
                                        .pointer = pointer,
                                    });
    };

    ViewportToolResult result = dispatch_frame(ViewportPointerToolEvent{
        .hovered = true,
        .left_clicked = true,
        .left_down = true,
        .mouse_local = {400.0f, 300.0f},
        .left_click_local = {400.0f, 300.0f},
    });
    ASSERT_TRUE(result.handled);
    const ModelingBoxPreview pressed_preview =
        session.modeling().box_preview();
    ASSERT_EQ(pressed_preview.drag_state, ModelingBoxDragState::Footprint);
    EXPECT_FALSE(pressed_preview.valid);
    EXPECT_EQ(pressed_preview.footprint_count, 0U);

    current_event = footprint_end;
    result = dispatch_frame(ViewportPointerToolEvent{
        .hovered = true,
        .left_down = true,
        .mouse_local = {420.0f, 300.0f},
    });
    ASSERT_TRUE(result.handled);
    const ModelingBoxPreview footprint_preview =
        session.modeling().box_preview();
    ASSERT_TRUE(footprint_preview.valid);
    ASSERT_EQ(footprint_preview.drag_state,
              ModelingBoxDragState::Footprint);
    ASSERT_EQ(footprint_preview.footprint_count, 4U);
    EXPECT_FLOAT_EQ(footprint_preview.footprint_size.x, 2.0f);
    EXPECT_FLOAT_EQ(footprint_preview.footprint_size.y, 3.0f);
    EXPECT_EQ(commit_count, 0);

    current_event = footprint_end;
    result = dispatch_frame(ViewportPointerToolEvent{
        .hovered = true,
        .left_released = true,
        .mouse_local = {420.0f, 300.0f},
    });
    ASSERT_TRUE(result.handled);
    EXPECT_FALSE(result.active_tool_changed);
    EXPECT_EQ(session.modeling().box_preview().drag_state,
              ModelingBoxDragState::Idle);
    EXPECT_EQ(commit_count, 1);
    ASSERT_NE(committed_entity, kInvalidEntityId);
    ASSERT_TRUE(session.scene().exists(committed_entity));
    ASSERT_NE(session.scene().material(committed_entity), nullptr);
    EXPECT_EQ(session.scene().material(committed_entity)->material,
              material.value.id);
    EXPECT_EQ(session.scene().primary_selection(), committed_entity);

    const QuaderApiVoidResult applied =
        editor.tools().commit_active_tool_preview();
    ASSERT_TRUE(applied.ok) << applied.message;
    ASSERT_TRUE(applied.changed);
    EXPECT_FALSE(editor.tools().has_active_primitive_creation());

    ASSERT_TRUE(editor.history().can_undo());
    const QuaderApiVoidResult undo = editor.history().undo();
    ASSERT_TRUE(undo.ok) << undo.message;
    ASSERT_TRUE(undo.changed);
    EXPECT_FALSE(session.scene().exists(committed_entity));
    EXPECT_EQ(session.scene().primary_selection(), kInvalidEntityId);
    ASSERT_TRUE(editor.history().can_redo());

    const QuaderApiVoidResult redo = editor.history().redo();
    ASSERT_TRUE(redo.ok) << redo.message;
    ASSERT_TRUE(redo.changed);
    ASSERT_TRUE(session.scene().exists(committed_entity));
    ASSERT_NE(session.scene().material(committed_entity), nullptr);
    EXPECT_EQ(session.scene().material(committed_entity)->material,
              material.value.id);
    EXPECT_EQ(session.scene().primary_selection(), committed_entity);
}

TEST(ViewportToolRuntimeTests,
     PrimitivePointerGestureRejectsStaleReleaseAndCommitsMatchingRelease) {
  PrimitivePointerHarness harness;
  ASSERT_NE(harness.tool, nullptr);
  ASSERT_TRUE(harness.activated);

  constexpr QVec2 kPressLocal{20.0f, 20.0f};
  constexpr QVec2 kReleaseLocal{48.0f, 48.0f};
  const std::array press_edges{ViewportPointerButtonEdge{
      .button = QuaderMouseButton::Left,
      .local = kPressLocal,
      .sequence = 100u,
  }};
  const ViewportToolResult started = harness.dispatch_direct({
      .hovered = true,
      .left_clicked = true,
      .left_down = true,
      .viewport_focused = true,
      .mouse_local = kPressLocal,
      .mouse_screen = kPressLocal,
      .left_click_local = kPressLocal,
      .pointer_pressed_edges = press_edges,
  });
  ASSERT_TRUE(started.consumed);
  EXPECT_TRUE(started.settings_operation_started);
  ASSERT_TRUE(harness.modal_state.primary_pointer_gesture.has_value());

  const ViewportToolResult updated = harness.dispatch_direct({
      .hovered = true,
      .left_down = true,
      .viewport_focused = true,
      .mouse_local = kReleaseLocal,
      .mouse_screen = kReleaseLocal,
  });
  ASSERT_TRUE(updated.handled);
  EXPECT_FALSE(updated.settings_operation_started);
  ASSERT_EQ(harness.session.modeling().box_preview().drag_state,
            ModelingBoxDragState::Footprint);

  const std::array stale_release{ViewportPointerButtonEdge{
      .button = QuaderMouseButton::Left,
      .local = kReleaseLocal,
      .sequence = 99u,
  }};
  (void)harness.dispatch_direct({
      .hovered = true,
      .left_down = true,
      .left_released = true,
      .viewport_focused = true,
      .mouse_local = kReleaseLocal,
      .mouse_screen = kReleaseLocal,
      .pointer_released_edges = stale_release,
  });
  EXPECT_TRUE(harness.modal_state.primary_pointer_gesture.has_value());
  EXPECT_EQ(harness.accept_count, 0);

  const std::array matching_release{ViewportPointerButtonEdge{
      .button = QuaderMouseButton::Left,
      .local = kReleaseLocal,
      .sequence = 101u,
  }};
  const ViewportToolResult committed = harness.dispatch_direct({
      .hovered = true,
      .left_released = true,
      .viewport_focused = true,
      .mouse_local = kReleaseLocal,
      .mouse_screen = kReleaseLocal,
      .pointer_released_edges = matching_release,
  });
  EXPECT_TRUE(committed.consumed);
  EXPECT_FALSE(committed.settings_operation_started);
  EXPECT_FALSE(harness.modal_state.primary_pointer_gesture.has_value());
  EXPECT_EQ(harness.accept_count, 1);
  EXPECT_EQ(harness.session.primitive_creation().shape_count(), 1u);
}

TEST(ViewportToolRuntimeTests,
     PrimitiveOneAxisReleaseCancelsOnlyTheCurrentPointerGesture) {
  PrimitivePointerHarness harness;
  ASSERT_NE(harness.tool, nullptr);
  ASSERT_TRUE(harness.activated);

  constexpr QVec2 kPressLocal{20.0f, 20.0f};
  constexpr QVec2 kLineEndLocal{50.0f, 20.0f};
  const std::array press_edges{ViewportPointerButtonEdge{
      .button = QuaderMouseButton::Left,
      .local = kPressLocal,
      .sequence = 500u,
  }};
  ASSERT_TRUE(harness.dispatch_direct({
      .hovered = true,
      .left_clicked = true,
      .left_down = true,
      .viewport_focused = true,
      .mouse_local = kPressLocal,
      .mouse_screen = kPressLocal,
      .left_click_local = kPressLocal,
      .pointer_pressed_edges = press_edges,
  }).consumed);
  ASSERT_TRUE(harness.modal_state.primary_pointer_gesture.has_value());
  const ModelingBoxPreview pressed = harness.session.modeling().box_preview();
  EXPECT_FALSE(pressed.valid);
  EXPECT_EQ(pressed.footprint_count, 0U);

  ASSERT_TRUE(harness.dispatch_direct({
      .hovered = true,
      .left_down = true,
      .viewport_focused = true,
      .mouse_local = kLineEndLocal,
      .mouse_screen = kLineEndLocal,
  }).handled);
  const ModelingBoxPreview line = harness.session.modeling().box_preview();
  EXPECT_EQ(line.drag_state, ModelingBoxDragState::Footprint);
  EXPECT_FALSE(line.valid);
  ASSERT_EQ(line.footprint_count, 2U);
  EXPECT_FLOAT_EQ(line.footprint_size.x, 3.0f);
  EXPECT_FLOAT_EQ(line.footprint_size.y, 0.0f);

  const std::array release_edges{ViewportPointerButtonEdge{
      .button = QuaderMouseButton::Left,
      .local = kLineEndLocal,
      .sequence = 501u,
  }};
  const ViewportToolResult released = harness.dispatch_direct({
      .hovered = true,
      .left_released = true,
      .viewport_focused = true,
      .mouse_local = kLineEndLocal,
      .mouse_screen = kLineEndLocal,
      .pointer_released_edges = release_edges,
  });
  EXPECT_TRUE(released.consumed);
  EXPECT_FALSE(harness.modal_state.primary_pointer_gesture.has_value());
  EXPECT_FALSE(harness.modal_state.primary_release_suppression.active());
  EXPECT_EQ(harness.accept_count, 0);
  EXPECT_TRUE(harness.session.primitive_creation().active());
  EXPECT_EQ(harness.session.primitive_creation().shape_count(), 0U);
  EXPECT_EQ(harness.session.modeling().box_preview().drag_state,
            ModelingBoxDragState::Idle);
}

TEST(ViewportToolRuntimeTests,
     PrimitiveUndoRetiresTheCurrentGestureAndAllowsTheNextDrag) {
  enum class NextFrame { HeldThenRelease, Release, FreshPress };
  for (const NextFrame next_frame : {NextFrame::HeldThenRelease,
                                    NextFrame::Release,
                                    NextFrame::FreshPress}) {
    SCOPED_TRACE(static_cast<int>(next_frame));
    PrimitivePointerHarness harness;
    ASSERT_TRUE(harness.activated);
    CountingPrimitiveCancelPort cancel_port(harness.tool_ports);
    harness.context.ports.cancel = &cancel_port;
    ViewportToolRuntime runtime;
    runtime.sync_active_tool(ViewportToolId::CreatePrimitive);
    std::uint64_t sequence = 600u;
    const auto frame = [&](ViewportPointerToolEvent pointer) {
      return runtime.handle_active_tool_event(
          harness.state, harness.context,
          ViewportToolEvent{
              .kind = ViewportToolEventKind::Frame,
              .tool = ViewportToolId::CreatePrimitive,
              .pointer = pointer,
          });
    };
    const auto press = [&](QVec2 local) {
      const std::array edges{ViewportPointerButtonEdge{
          .button = QuaderMouseButton::Left,
          .local = local,
          .sequence = sequence++,
      }};
      return frame({
          .hovered = true,
          .left_clicked = true,
          .left_down = true,
          .viewport_focused = true,
          .mouse_local = local,
          .mouse_screen = local,
          .left_click_local = local,
          .pointer_pressed_edges = edges,
      });
    };
    const auto release = [&](QVec2 local) {
      const std::array edges{ViewportPointerButtonEdge{
          .button = QuaderMouseButton::Left,
          .local = local,
          .sequence = sequence++,
      }};
      return frame({
          .hovered = true,
          .left_released = true,
          .viewport_focused = true,
          .mouse_local = local,
          .mouse_screen = local,
          .pointer_released_edges = edges,
      });
    };

    ASSERT_TRUE(press({20.0f, 20.0f}).consumed);
    ASSERT_TRUE(release({48.0f, 48.0f}).consumed);
    ASSERT_EQ(harness.accept_count, 1);
    ASSERT_EQ(harness.session.primitive_creation().shape_count(), 1U);
    const EntityId first_entity = harness.session.scene().primary_selection();

    ASSERT_TRUE(press({60.0f, 60.0f}).consumed);
    ASSERT_TRUE(frame({.hovered = true,
                       .left_down = true,
                       .viewport_focused = true,
                       .mouse_local = {90.0f, 90.0f},
                       .mouse_screen = {90.0f, 90.0f}}).handled);
    ASSERT_EQ(harness.session.modeling().box_preview().drag_state,
              ModelingBoxDragState::Footprint);
    const QuaderApiVoidResult undone = harness.editor.history().undo();
    ASSERT_TRUE(undone.ok()) << undone.error_message();
    ASSERT_TRUE(undone.changed());
    ASSERT_EQ(harness.session.modeling().box_preview().drag_state,
              ModelingBoxDragState::Idle);
    ASSERT_TRUE(harness.session.primitive_creation().active());
    ASSERT_TRUE(harness.context.modal_state->primary_pointer_gesture);

    if (next_frame == NextFrame::HeldThenRelease) {
      EXPECT_TRUE(frame({.hovered = true,
                         .left_down = true,
                         .viewport_focused = true,
                       .mouse_local = {90.0f, 90.0f},
                       .mouse_screen = {90.0f, 90.0f}}).consumed);
      EXPECT_FALSE(harness.context.modal_state->primary_pointer_gesture);
      EXPECT_TRUE(
          harness.context.modal_state->primary_release_suppression.active());
    }
    if (next_frame != NextFrame::FreshPress) {
      EXPECT_TRUE(release({90.0f, 90.0f}).consumed);
      EXPECT_FALSE(harness.context.modal_state->primary_pointer_gesture);
      EXPECT_FALSE(
          harness.context.modal_state->primary_release_suppression.active());
      (void)frame({.hovered = true,
                   .viewport_focused = true,
                   .mouse_local = {90.0f, 90.0f},
                   .mouse_screen = {90.0f, 90.0f}});
      EXPECT_EQ(harness.editor.tools().active_tool(), "create_primitive");
      EXPECT_EQ(cancel_port.box_cancellations, 0);
    }
    EXPECT_EQ(harness.accept_count, 1);
    EXPECT_EQ(harness.session.primitive_creation().shape_count(), 1U);
    EXPECT_TRUE(harness.session.scene().exists(first_entity));

    ASSERT_TRUE(press({100.0f, 100.0f}).consumed);
    ASSERT_TRUE(harness.context.modal_state->primary_pointer_gesture);
    EXPECT_FALSE(
        harness.context.modal_state->primary_release_suppression.active());
    EXPECT_FLOAT_EQ(
        harness.context.modal_state->primary_pointer_gesture->press.local.x,
        100.0f);
    EXPECT_FLOAT_EQ(
        harness.context.modal_state->primary_pointer_gesture->press.local.y,
        100.0f);
    ASSERT_EQ(harness.session.modeling().box_preview().drag_state,
              ModelingBoxDragState::Footprint);
    ASSERT_TRUE(release({130.0f, 130.0f}).consumed);
    EXPECT_EQ(harness.accept_count, 2);
    EXPECT_EQ(harness.session.primitive_creation().shape_count(), 2U);
    EXPECT_TRUE(harness.session.primitive_creation().active());
    EXPECT_TRUE(harness.session.scene().exists(first_entity));
    EXPECT_EQ(harness.editor.tools().active_tool(), "create_primitive");
    EXPECT_EQ(runtime.active_tool()->viewport_tool(),
              ViewportToolId::CreatePrimitive);
    EXPECT_FALSE(harness.context.modal_state->primary_pointer_gesture);
    EXPECT_FALSE(
        harness.context.modal_state->primary_release_suppression.active());
    EXPECT_EQ(cancel_port.box_cancellations, 0);
  }
}

TEST(ViewportToolRuntimeTests,
     PrimitivePointerInterruptionPreservesFootprintForMatchingReleaseOrRetry) {
  for (int interruption = 0; interruption < 5; ++interruption) {
    SCOPED_TRACE(interruption);
    PrimitivePointerHarness harness;
    ASSERT_NE(harness.tool, nullptr);
    ASSERT_TRUE(harness.activated);
    constexpr QVec2 kPressLocal{20.0f, 20.0f};
    constexpr QVec2 kDragLocal{48.0f, 48.0f};
    const std::array press_edges{ViewportPointerButtonEdge{
        .button = QuaderMouseButton::Left,
        .local = kPressLocal,
        .sequence = 300u,
    }};
    ASSERT_TRUE(harness.dispatch_direct({
        .hovered = true,
        .left_clicked = true,
        .left_down = true,
        .viewport_focused = true,
        .mouse_local = kPressLocal,
        .mouse_screen = kPressLocal,
        .left_click_local = kPressLocal,
        .pointer_pressed_edges = press_edges,
    }).consumed);
    (void)harness.dispatch_direct({
        .hovered = true,
        .left_down = true,
        .viewport_focused = true,
        .mouse_local = kDragLocal,
        .mouse_screen = kDragLocal,
    });
    ASSERT_TRUE(harness.modal_state.primary_pointer_gesture.has_value());
    const ModelingBoxPreview authored = harness.session.modeling().box_preview();
    ASSERT_TRUE(authored.valid);
    const bool pointer_lost = interruption == 1 || interruption == 3;

    const ViewportPointerToolEvent interrupted{
        .hovered = true,
        .left_down = interruption != 3,
        .navigation_active = interruption == 2,
        .text_input_active = interruption == 4,
        .viewport_focused = interruption != 0,
        .pointer_interaction_canceled = interruption == 1,
        .mouse_local = {-9000.0f, -9000.0f},
        .mouse_screen = {-9000.0f, -9000.0f},
    };
    EXPECT_TRUE(harness.dispatch_direct(interrupted).consumed);
    EXPECT_EQ(harness.modal_state.primary_pointer_gesture.has_value(), !pointer_lost);
    EXPECT_EQ(harness.modal_state.primary_release_suppression.active(), interruption == 1);
    EXPECT_EQ(harness.session.modeling().box_preview().drag_state,
              ModelingBoxDragState::Footprint);
    const auto expect_authored_footprint = [&]() {
      const ModelingBoxPreview retained = harness.session.modeling().box_preview();
      ASSERT_EQ(retained.footprint_count, authored.footprint_count);
      for (std::size_t index = 0; index < authored.footprint_count; ++index) {
        EXPECT_FLOAT_EQ(retained.footprint[index].x, authored.footprint[index].x);
        EXPECT_FLOAT_EQ(retained.footprint[index].y, authored.footprint[index].y);
        EXPECT_FLOAT_EQ(retained.footprint[index].z, authored.footprint[index].z);
      }
    };
    expect_authored_footprint();
    EXPECT_TRUE(harness.session.primitive_creation().active());
    EXPECT_EQ(harness.accept_count, 0);
    EXPECT_FALSE(harness.session.history().can_undo());

    const std::array release_edges{ViewportPointerButtonEdge{
        .button = QuaderMouseButton::Left,
        .local = kDragLocal,
        .sequence = 301u,
    }};
    const ViewportToolResult release = harness.dispatch_direct({
        .hovered = true,
        .left_released = true,
        .viewport_focused = true,
        .mouse_local = kDragLocal,
        .mouse_screen = kDragLocal,
        .pointer_released_edges = release_edges,
    });
    EXPECT_FALSE(harness.modal_state.primary_release_suppression.active());
    if (!pointer_lost) {
      EXPECT_TRUE(release.consumed);
      EXPECT_EQ(harness.accept_count, 1);
      EXPECT_EQ(harness.session.primitive_creation().shape_count(), 1U);
      continue;
    }
    EXPECT_EQ(harness.accept_count, 0);
    expect_authored_footprint();
    const auto rejected = harness.editor.tools().activate(ViewportToolId::Move);
    EXPECT_FALSE(rejected.ok());
    EXPECT_TRUE(harness.session.primitive_creation().active());
    expect_authored_footprint();

    constexpr QVec2 kRetryPress{80.0f, 80.0f};
    const auto retry = [&](bool released, QVec2 point) {
      return harness.dispatch_direct({
          .hovered = true,
          .left_clicked = true,
          .left_down = !released,
          .left_released = released,
          .viewport_focused = true,
          .mouse_local = point,
          .mouse_screen = point,
          .left_click_local = kRetryPress,
      });
    };
    EXPECT_TRUE(retry(true, kRetryPress).consumed);
    EXPECT_FALSE(harness.modal_state.primary_pointer_gesture);
    EXPECT_EQ(harness.accept_count, 0);
    expect_authored_footprint();
    ASSERT_TRUE(retry(false, kRetryPress).consumed);
    expect_authored_footprint();
    EXPECT_TRUE(harness.dispatch_direct({
        .hovered = true,
        .left_released = true,
        .viewport_focused = true,
        .mouse_local = kDragLocal,
    }).consumed);
    EXPECT_EQ(harness.accept_count, 1);
    EXPECT_EQ(harness.session.primitive_creation().shape_count(), 1U);
  }
}

TEST(ViewportToolRuntimeTests,
     PrimitiveIncompleteFootprintSurvivesLostCaptureUntilExplicitCancel) {
  PrimitivePointerHarness harness;
  ASSERT_TRUE(harness.dispatch_direct({
      .hovered = true, .left_clicked = true, .left_down = true,
      .mouse_local = {20.0f, 20.0f}, .left_click_local = {20.0f, 20.0f},
  }).consumed);
  ASSERT_TRUE(harness.dispatch_direct({
      .hovered = true, .left_down = true, .mouse_local = {50.0f, 20.0f},
  }).handled);
  const ModelingBoxPreview authored = harness.session.modeling().box_preview();
  ASSERT_FALSE(authored.valid);
  ASSERT_EQ(authored.footprint_count, 2U);
  ASSERT_TRUE(harness.dispatch_direct({
      .pointer_interaction_canceled = true,
      .mouse_local = {-9000.0f, -9000.0f},
  }).consumed);
  EXPECT_EQ(harness.session.modeling().box_preview().drag_state,
            ModelingBoxDragState::Footprint);
  EXPECT_FLOAT_EQ(harness.session.modeling().box_preview().footprint_size.x,
                  authored.footprint_size.x);
  EXPECT_EQ(harness.accept_count, 0);
  EXPECT_FALSE(harness.editor.tools().activate(ViewportToolId::Move).ok());
  EXPECT_TRUE(harness.session.primitive_creation().active());
  const auto canceled = harness.tool->handle_event(harness.context, {
      .kind = ViewportToolEventKind::Cancel,
      .tool = ViewportToolId::CreatePrimitive,
      .exit_active_modal_tool = true,
  });
  EXPECT_TRUE(canceled.consumed);
  EXPECT_FALSE(harness.session.primitive_creation().active());
  EXPECT_EQ(harness.session.modeling().box_preview().drag_state,
            ModelingBoxDragState::Idle);
  EXPECT_TRUE(harness.session.scene().entities().empty());
  EXPECT_FALSE(harness.session.history().can_undo());
}

TEST(ViewportToolRuntimeTests,
     PrimitiveEscapeAndToolSwitchRetainReleaseSuppression) {
  for (const bool escape : {true, false}) {
    SCOPED_TRACE(escape ? "Escape" : "tool switch");
    PrimitivePointerHarness harness;
    ASSERT_NE(harness.tool, nullptr);
    ASSERT_TRUE(harness.activated);
    ViewportToolRuntime runtime;
    runtime.sync_active_tool(ViewportToolId::CreatePrimitive);
    constexpr QVec2 kPressLocal{20.0f, 20.0f};
    const std::array press_edges{ViewportPointerButtonEdge{
        .button = QuaderMouseButton::Left,
        .local = kPressLocal,
        .sequence = 400u,
    }};
    ASSERT_TRUE(runtime.handle_active_tool_event(
        harness.state, harness.context,
        ViewportToolEvent{
            .kind = ViewportToolEventKind::Frame,
            .tool = ViewportToolId::CreatePrimitive,
            .pointer = {.hovered = true,
                        .left_clicked = true,
                        .left_down = true,
                        .viewport_focused = true,
                        .mouse_local = kPressLocal,
                        .mouse_screen = kPressLocal,
                        .left_click_local = kPressLocal,
                        .pointer_pressed_edges = press_edges},
        }).consumed);

    if (escape) {
      const ViewportToolResult canceled = runtime.handle_active_tool_event(
          harness.state, harness.context,
          ViewportToolEvent{.kind = ViewportToolEventKind::Cancel,
                            .tool = ViewportToolId::CreatePrimitive,
                            .exit_active_modal_tool = true});
      EXPECT_TRUE(canceled.consumed);
    } else {
      ASSERT_TRUE(harness.editor.tools().cancel_active_tool_preview().ok());
      runtime.clear_modal_tool_state();
    }
    harness.state.active_tool = ViewportToolId::Select;
    runtime.sync_active_tool(ViewportToolId::Select);

    const std::array release_edges{ViewportPointerButtonEdge{
        .button = QuaderMouseButton::Left,
        .local = kPressLocal,
        .sequence = 401u,
    }};
    const ViewportToolResult release = runtime.handle_active_tool_event(
        harness.state, harness.context,
        ViewportToolEvent{
            .kind = ViewportToolEventKind::Frame,
            .tool = ViewportToolId::Select,
            .pointer = {.hovered = true,
                        .left_released = true,
                        .viewport_focused = true,
                        .mouse_local = kPressLocal,
                        .mouse_screen = kPressLocal,
                        .pointer_released_edges = release_edges},
        });
    EXPECT_TRUE(release.handled);
    EXPECT_TRUE(release.consumed);
    EXPECT_EQ(harness.accept_count, 0);
  }
}

TEST(ViewportToolRuntimeTests,
     PersistentPrimitivePreservesSnapModeAndAcceptedShapesAfterPreviewFailure) {
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  ASSERT_TRUE(
      editor.tools().activate(ViewportToolId::CreatePrimitive).ok());

  ViewportToolState state{.active_tool = ViewportToolId::CreatePrimitive};
  ViewportCameraController camera;
  ViewportModalToolState modal_state;
  const ViewportTool *box_tool =
      find_viewport_tool(ViewportToolId::CreatePrimitive);
  ASSERT_NE(box_tool, nullptr);

  ModelingBoxToolEvent current_event{
      .screen_position = {400.0f, 300.0f},
      .ray = {.origin = {4.25f, 4.0f, 4.25f},
              .direction = {0.0f, -1.0f, 0.0f}},
      .grid_size = 1.0,
      .snap_enabled = false,
  };
  bool accepted_snap_enabled = true;
  ViewportBoxToolContext box_context;
  box_context.make_event_at = [&](QVec2) { return current_event; };
  box_context.accept_preview = [&](std::string name, AssetId material,
                                   bool snap_enabled) {
    accepted_snap_enabled = snap_enabled;
    const QuaderApiVoidResult result = editor.tools().accept_active_primitive(
        std::move(name), material, snap_enabled);
    return ModelingCommandResult{
        .ok = result.ok,
        .changed = result.changed,
        .message = result.message,
    };
  };
  box_context.cancel_session = [&]() {
    const QuaderApiVoidResult result =
        editor.tools().cancel_active_tool_preview();
    return ModelingCommandResult{
        .ok = result.ok,
        .changed = result.changed,
        .message = result.message,
    };
  };
  box_context.session_active = [&]() {
    return session.primitive_creation().active();
  };
  box_context.next_mesh_name = [] { return std::string{"Persistent Box"}; };

  ViewportModelingToolPortAdapter tool_ports(session.modeling());
  ViewportToolContext context{
      .state = state,
      .camera = camera,
      .ports = tool_ports.ports(),
      .document = &session.scene(),
      .modal_state = &modal_state,
      .box = &box_context,
  };
  const auto dispatch_frame = [&](ViewportPointerToolEvent pointer) {
    return box_tool->handle_event(
        context, ViewportToolEvent{
                     .kind = ViewportToolEventKind::Frame,
                     .tool = ViewportToolId::CreatePrimitive,
                     .pointer = pointer,
                 });
  };

  ASSERT_TRUE(dispatch_frame({.hovered = true,
                              .left_clicked = true,
                              .left_down = true,
                              .left_click_local = {20.0f, 20.0f}})
                  .handled);
  current_event.ray.origin = {6.4f, 4.0f, 7.6f};
  ASSERT_TRUE(dispatch_frame({.hovered = true,
                              .left_released = true,
                              .mouse_local = {40.0f, 20.0f}})
                  .handled);
  EXPECT_FALSE(accepted_snap_enabled);
  EXPECT_TRUE(session.primitive_creation().active());
  EXPECT_EQ(session.primitive_creation().shape_count(), 1U);
  EXPECT_EQ(session.modeling().objects().size(), 1U);
  // Public Undo removes the accepted shape from the active lifecycle, even
  // though that live shape has not entered document history.
  EXPECT_TRUE(editor.history().can_undo());

  current_event.ray.direction = {};
  const ViewportToolResult failed =
      dispatch_frame({.hovered = true,
                      .left_clicked = true,
                      .left_down = true,
                      .left_click_local = {20.0f, 20.0f}});
  EXPECT_FALSE(failed.active_tool_changed);
  EXPECT_FALSE(failed.sync_active_tool_from_modeling);
  EXPECT_TRUE(session.primitive_creation().active());
  EXPECT_EQ(editor.tools().active_tool(), "create_primitive");
  EXPECT_EQ(session.modeling().objects().size(), 1U);
  EXPECT_EQ(session.primitive_creation().shape_count(), 1U);
  EXPECT_TRUE(editor.history().can_undo());
  current_event.ray.direction = {0.0f, -1.0f, 0.0f};
  ASSERT_TRUE(dispatch_frame({.hovered = true,
                              .left_clicked = true,
                              .left_down = true,
                              .left_click_local = {20.0f, 20.0f}}).handled);
  current_event.ray.origin = {9.0f, 4.0f, 10.0f};
  ASSERT_TRUE(dispatch_frame({.hovered = true,
                              .left_released = true,
                              .mouse_local = {40.0f, 40.0f}}).handled);
  EXPECT_EQ(session.primitive_creation().shape_count(), 2U);
  EXPECT_EQ(session.modeling().objects().size(), 2U);
}

TEST(ViewportToolRuntimeTests,
     FailedHoverStaysSilentAndExplicitRejectionAnnouncesEachAttempt) {
  for (const ViewportToolId tool_id :
       {ViewportToolId::Poly, ViewportToolId::InsertEdgeLoop,
        ViewportToolId::Cut, ViewportToolId::Mirror}) {
    SCOPED_TRACE(static_cast<int>(tool_id));
    SceneSession session;
    QuaderEditorApi editor(
        QuaderApiSessionInternalAccess::from_scene_session(session));
    const auto mesh = editor.modeling().create_box(
        {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        "Tool Error Target");
    ASSERT_TRUE(mesh.ok()) << mesh.error_message();
    ASSERT_TRUE(editor.selection().select_object(mesh.value()).ok());
    if (tool_id == ViewportToolId::InsertEdgeLoop) {
      ASSERT_TRUE(editor.mode().set(QuaderEditorMode::Edit).ok());
    }

    struct ReportedResult {
      ModelingCommandResult command;
      bool announce_failure = false;
    };
    std::vector<ReportedResult> results;
    ViewportModelingToolPortAdapter tool_ports(
        session.modeling(),
        {.handle_tool_command_result =
             [&](std::string_view, const ModelingCommandResult &command,
                 bool announce_failure) {
               results.push_back({command, announce_failure});
             }});
    FailingToolPreviewPorts failing_hover;
    failing_hover.fail_point_hover = true;
    ViewportModelingToolCommitAdapter commit_adapter(editor.tools());
    ViewportToolPorts ports = tool_ports.ports(&commit_adapter);
    ports.hover = &failing_hover;
    if (tool_id == ViewportToolId::InsertEdgeLoop) {
      // Empty-space begin can legitimately succeed without a preview. Inject
      // rejection at the preview port to exercise its error presentation.
      failing_hover.fail_edge_loop_begin = true;
      ports.preview = &failing_hover;
    }
    ViewportToolRuntime runtime;
    ViewportToolState state;
    ViewportCameraController camera;
    ASSERT_TRUE(runtime.set_active_tool(state, tool_id));
    const ModelingCommandResult activated =
        runtime.activate_active_tool(session.modeling());
    ASSERT_TRUE(activated.ok) << activated.message;

    ViewportPointToolContext point_context;
    point_context.make_event_at = [](QVec2, bool dragging) {
      return ModelingToolRayEvent{
          .ray = {.origin = {100.0f, 100.0f, 100.0f},
                  .direction = {0.0f, 0.0f, -1.0f}},
          .dragging = dragging,
      };
    };
    ViewportToolContext context{
        .state = state,
        .camera = camera,
        .ports = ports,
        .point = &point_context,
    };
    const auto dispatch = [&](ViewportPointerToolEvent pointer) {
      return runtime.handle_active_tool_event(
          state, context,
          {.kind = ViewportToolEventKind::Frame,
           .tool = tool_id,
           .pointer = pointer});
    };

    for (int attempt = 0; attempt < 2; ++attempt) {
      results.clear();
      EXPECT_TRUE(dispatch({.can_interact = true}).handled);
      ASSERT_EQ(results.size(), 1U);
      EXPECT_FALSE(results.back().command.ok);
      EXPECT_FALSE(results.back().announce_failure);
      EXPECT_EQ(results.back().command.message, "Forced tool hover failure.");

      results.clear();
      const bool begin_drag = tool_id == ViewportToolId::InsertEdgeLoop;
      const ViewportToolResult rejected = dispatch({
          .left_clicked = begin_drag,
          .can_interact = true,
          .finalize_pressed = !begin_drag,
          .suppress_hover_work = true,
      });
      EXPECT_TRUE(rejected.handled);
      EXPECT_FALSE(rejected.active_tool_changed);
      ASSERT_EQ(results.size(), 1U);
      EXPECT_FALSE(results.back().command.ok);
      EXPECT_FALSE(results.back().command.changed);
      EXPECT_TRUE(results.back().announce_failure);
      EXPECT_FALSE(results.back().command.message.empty());
      EXPECT_EQ(state.active_tool, tool_id);
    }

    results.clear();
    context.ports.cancel = &failing_hover;
    const int invalidations_before = failing_hover.invalidations;
    const ViewportToolResult canceled = runtime.handle_active_tool_event(
        state, context,
        {.kind = ViewportToolEventKind::Cancel,
         .tool = tool_id,
         .exit_active_modal_tool = true});
    EXPECT_TRUE(canceled.handled);
    EXPECT_TRUE(canceled.consumed);
    EXPECT_TRUE(canceled.modal_state_changed);
    EXPECT_FALSE(canceled.active_tool_changed);
    EXPECT_FALSE(canceled.sync_active_tool_from_modeling);
    EXPECT_EQ(failing_hover.cancellations, 1);
    EXPECT_TRUE(failing_hover.last_cancel_exited_tool);
    EXPECT_EQ(failing_hover.invalidations, invalidations_before + 1);
    ASSERT_EQ(results.size(), 1U);
    EXPECT_FALSE(results.back().command.ok);
    EXPECT_TRUE(results.back().announce_failure);
    EXPECT_EQ(results.back().command.message, "Forced modal cancel failure.");
  }
}

TEST(ViewportToolRuntimeTests,
     PersistentPrimitiveQueryAndHoverFailuresNeverCancelTheSession) {
  const ViewportTool *box_tool =
      find_viewport_tool(ViewportToolId::CreatePrimitive);
  ASSERT_NE(box_tool, nullptr);

  for (const bool fail_query : {true, false}) {
    SCOPED_TRACE(fail_query ? "query failure" : "hover failure");
    FailingToolPreviewPorts ports;
    ports.fail_query = fail_query;
    ViewportToolState state{
        .active_tool = ViewportToolId::CreatePrimitive};
    ViewportCameraController camera;
    ViewportModalToolState modal_state;
    int cancel_count = 0;
    ViewportBoxToolContext box_context;
    box_context.make_event_at = [](QVec2) { return ModelingBoxToolEvent{}; };
    box_context.accept_preview =
        [](std::string, AssetId, bool) -> ModelingCommandResult {
      return {.ok = true, .changed = true};
    };
    box_context.cancel_session = [&]() -> ModelingCommandResult {
      ++cancel_count;
      return {.ok = true, .changed = true};
    };
    ViewportToolContext context{
        .state = state,
        .camera = camera,
        .ports = {.hover = &ports, .preview = &ports},
        .modal_state = &modal_state,
        .box = &box_context,
    };

    const ViewportToolResult result = box_tool->handle_event(
        context,
        ViewportToolEvent{
            .kind = ViewportToolEventKind::Frame,
            .tool = ViewportToolId::CreatePrimitive,
            .pointer = {.hovered = true},
        });

    EXPECT_TRUE(result.handled);
    EXPECT_TRUE(result.consumed);
    EXPECT_FALSE(result.active_tool_changed);
    EXPECT_FALSE(result.sync_active_tool_from_modeling);
    EXPECT_EQ(cancel_count, 0);
    EXPECT_EQ(ports.hover_updates, fail_query ? 0 : 1);
    EXPECT_GT(ports.invalidations, 0);
  }
}

TEST(ViewportToolRuntimeTests,
     FailedPrimitiveFinalizeStillSynchronizesAfterSessionExit) {
  const ViewportTool *box_tool =
      find_viewport_tool(ViewportToolId::CreatePrimitive);
  ASSERT_NE(box_tool, nullptr);

  for (const bool apply : {true, false}) {
    SCOPED_TRACE(apply ? "apply failure" : "cancel failure");
    FailingToolPreviewPorts ports;
    ViewportToolState state{
        .active_tool = ViewportToolId::CreatePrimitive};
    ViewportCameraController camera;
    ViewportModalToolState modal_state;
    bool session_active = true;
    ViewportBoxToolContext box_context;
    box_context.make_event_at = [](QVec2) { return ModelingBoxToolEvent{}; };
    box_context.accept_preview =
        [](std::string, AssetId, bool) -> ModelingCommandResult {
      return {.ok = true, .changed = true};
    };
    box_context.apply_session = [&]() -> ModelingCommandResult {
      session_active = false;
      return {.ok = false, .message = "Forced primitive apply failure."};
    };
    box_context.cancel_session = [&]() -> ModelingCommandResult {
      session_active = false;
      return {.ok = false, .message = "Forced primitive cancel failure."};
    };
    box_context.session_active = [&]() { return session_active; };
    ViewportToolContext context{
        .state = state,
        .camera = camera,
        .ports = {.hover = &ports, .preview = &ports},
        .modal_state = &modal_state,
        .box = &box_context,
    };

    const ViewportToolResult result = box_tool->handle_event(
        context, apply ? ViewportToolEvent{
                             .kind = ViewportToolEventKind::Frame,
                             .tool = ViewportToolId::CreatePrimitive,
                             .pointer = {.finalize_pressed = true},
                         }
                       : ViewportToolEvent{
                             .kind = ViewportToolEventKind::Cancel,
                             .tool = ViewportToolId::CreatePrimitive,
                             .exit_active_modal_tool = true,
                         });

    EXPECT_TRUE(result.handled);
    EXPECT_TRUE(result.consumed);
    EXPECT_TRUE(result.active_tool_changed);
    EXPECT_TRUE(result.sync_active_tool_from_modeling);
  }
}












/// Drives modal mesh commits through the production viewport runtime and ports.
class ModelingPointerHistoryHarness final {
public:
  ModelingPointerHistoryHarness()
      : editor(QuaderApiSessionInternalAccess::from_scene_session(session)),
        commit_adapter(editor.tools()),
        tool_ports(session.modeling(),
                   {.handle_tool_command_result =
                        [this](std::string_view, const ModelingCommandResult &command,
                               bool) { commands.push_back(command); }}),
        context{.state = state,
                .camera = camera,
                .ports = tool_ports.ports(&commit_adapter),
                .document = &session.scene(),
                .point = &point} {
    point.make_event_at = [this](QVec2 local, bool dragging) {
      ModelingViewportCamera projection{
          .orthographic = tool_id == ViewportToolId::InsertEdgeLoop,
          .origin = {0.0f, 0.0f, 10.0f},
          .x_axis = {1.0f, 0.0f, 0.0f},
          .y_axis = {0.0f, 1.0f, 0.0f},
          .z_axis = {0.0f, 0.0f, 1.0f},
          .viewport_size = {100.0f, 100.0f},
          .orthographic_size = 100.0,
          .fov_degrees = 60.0,
      };
      return ModelingToolRayEvent{
          .screen_position = local,
          .ray = tool_id == ViewportToolId::InsertEdgeLoop
              ? ModelingRay{.origin = {1.0f, local.x, 5.0f},
                            .direction = {0.0f, 0.0f, -1.0f}}
              : ModelingRay{.origin = {local.x, 2.0f, 0.0f},
                            .direction = {0.0f, -1.0f, 0.0f}},
          .camera = projection,
          .grid_size = 0.5,
          .vertex_radius = 0.3,
          .edge_radius = 0.2,
          .snap_enabled = tool_id == ViewportToolId::Cut,
          .dragging = dragging,
      };
    };
  }

  void initialize(ViewportToolId requested_tool) {
    tool_id = requested_tool;
    const auto created = editor.modeling().create_box(
        {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        "Pointer History Source");
    ASSERT_TRUE(created.ok()) << created.error_message();
    source = created.value();
    ASSERT_TRUE(editor.selection().select_object(source).ok());
    if (tool_id == ViewportToolId::Knife ||
        tool_id == ViewportToolId::InsertEdgeLoop) {
      ASSERT_TRUE(editor.mode().set(QuaderEditorMode::Edit).ok());
    }
    ASSERT_TRUE(editor.tools().activate(tool_id).ok());
    ASSERT_TRUE(runtime.set_active_tool(state, tool_id));
    session.history().clear();
  }

  ViewportToolResult dispatch(ViewportPointerToolEvent pointer) {
    return runtime.handle_active_tool_event(
        state, context,
        {.kind = ViewportToolEventKind::Frame,
         .tool = tool_id,
         .pointer = pointer});
  }

  void begin_preview(QVec2 local = {}) {
    if (tool_id == ViewportToolId::InsertEdgeLoop) {
      const auto begun = context.ports.preview->begin_insert_edge_loop_drag(
          point.make_event_at(local, true));
      ASSERT_TRUE(begun.ok) << begun.message;
      ASSERT_TRUE(session.modeling().tool_status().insert_edge_loop_dragging);
    } else {
      ASSERT_TRUE(dispatch({.left_clicked = true,
                            .left_down = true,
                            .can_interact = true,
                            .suppress_hover_work = true}).handled);
      ASSERT_TRUE(dispatch({.left_down = true,
                            .can_interact = true,
                            .mouse_local = {1.0f, 0.0f}}).handled);
      ASSERT_TRUE(dispatch({.left_released = true,
                            .can_interact = true,
                            .suppress_hover_work = true,
                            .mouse_local = {1.0f, 0.0f}}).handled);
      ASSERT_TRUE(session.modeling().tool_status().cut_preview_valid);
    }
  }

  ViewportToolResult commit_pointer() {
    if (tool_id == ViewportToolId::Cut) {
      begin_preview();
      return dispatch({.can_interact = true,
                       .finalize_pressed = true,
                       .suppress_hover_work = true});
    }
    return dispatch({.left_clicked = true,
                     .can_interact = true,
                     .suppress_hover_work = true});
  }

  using Documents = std::vector<std::pair<EntityId, quader_poly::Document>>;

  Documents documents() const {
    Documents result;
    for (const auto &object : editor.query().modeling_objects()) {
      const auto *mesh = session.assets().mesh(object.mesh);
      EXPECT_NE(mesh, nullptr);
      if (mesh == nullptr) continue;
      EXPECT_NE(mesh->authored_polygon_document, nullptr);
      if (mesh->authored_polygon_document != nullptr) {
        result.emplace_back(object.entity, *mesh->authored_polygon_document);
      }
    }
    std::sort(result.begin(), result.end(), [](const auto &left, const auto &right) {
      return left.first < right.first;
    });
    return result;
  }

  void expect_documents(const Documents &expected) const {
    const auto actual = documents();
    ASSERT_EQ(actual.size(), expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index) {
      SCOPED_TRACE(index);
      EXPECT_EQ(actual[index].first, expected[index].first);
      const auto &current = actual[index].second;
      const auto &original = expected[index].second;
      EXPECT_TRUE(quader_poly::has_same_geometry(current, original));
      EXPECT_EQ(current.hard_normal_edges, original.hard_normal_edges);
      EXPECT_EQ(current.soft_normal_edges, original.soft_normal_edges);
      ASSERT_EQ(current.vertices.size(), original.vertices.size());
      for (std::size_t vertex = 0; vertex < original.vertices.size(); ++vertex) {
        EXPECT_EQ(current.vertices[vertex].id, original.vertices[vertex].id);
        EXPECT_EQ(current.vertices[vertex].position.x, original.vertices[vertex].position.x);
        EXPECT_EQ(current.vertices[vertex].position.y, original.vertices[vertex].position.y);
        EXPECT_EQ(current.vertices[vertex].position.z, original.vertices[vertex].position.z);
      }
      ASSERT_EQ(current.faces.size(), original.faces.size());
      for (std::size_t face = 0; face < original.faces.size(); ++face) {
        const auto &value = current.faces[face];
        const auto &wanted = original.faces[face];
        EXPECT_EQ(value.id, wanted.id);
        EXPECT_EQ(value.vertices, wanted.vertices);
        EXPECT_EQ(value.material_slot, wanted.material_slot);
        EXPECT_EQ(value.normal_shading, wanted.normal_shading);
        EXPECT_EQ(value.texture_transform, wanted.texture_transform);
        ASSERT_EQ(value.uvs.size(), wanted.uvs.size());
        for (std::size_t uv = 0; uv < wanted.uvs.size(); ++uv) {
          EXPECT_EQ(value.uvs[uv].x, wanted.uvs[uv].x);
          EXPECT_EQ(value.uvs[uv].y, wanted.uvs[uv].y);
        }
      }
    }
  }

  SceneSession session;
  QuaderEditorApi editor;
  ViewportModelingToolCommitAdapter commit_adapter;
  ViewportToolRuntime runtime;
  ViewportToolState state;
  ViewportCameraController camera;
  ViewportPointToolContext point;
  std::vector<ModelingCommandResult> commands;
  ViewportModelingToolPortAdapter tool_ports;
  ViewportToolContext context;
  ViewportToolId tool_id = ViewportToolId::Select;
  QuaderModelingObjectRef source;
};

TEST(ViewportToolRuntimeTests,
     InsertEdgeLoopUsesCtrlOnlySnapAndPublishesEveryPreviewTransition) {
  /// Records the actual retained stream produced by the viewport coordinator.
  class LoopPreviewSubmissionTarget final : public ViewportOverlaySubmissionTarget {
  public:
    ViewportOverlayCameraUpdate update_overlay_cameras(
        std::size_t, std::uint8_t, ViewportOverlayCamera) override {
      return {true, true};
    }
    void submit_persistent_overlay(std::size_t, std::uint8_t,
                                  ViewportOverlayFrame, ViewportOverlayCamera) override {}
    void submit_dynamic_overlay(std::size_t, std::uint8_t,
                                ViewportOverlayFrame submitted,
                                ViewportOverlayCamera) override {
      ++submissions;
      frame = std::move(submitted);
    }
    int submissions = 0;
    ViewportOverlayFrame frame;
  };
  for (const bool global_snap : {false, true}) {
    SCOPED_TRACE(global_snap);
    ModelingPointerHistoryHarness harness;
    harness.initialize(ViewportToolId::InsertEdgeLoop);
    const auto before = harness.documents();
    const auto producer = harness.point.make_event_at;
    harness.point.make_event_at = [=](QVec2 local, bool dragging) {
      auto event = producer(local, dragging);
      event.snap_enabled = global_snap;
      return event;
    };
    ViewportOverlaySubmissionCoordinator coordinator;
    LoopPreviewSubmissionTarget target;
    const auto settings = ViewportSettings::defaults();
    const auto submit = [&] {
      const auto snapshot = harness.session.modeling().volatile_overlay_snapshot();
      return coordinator.submit({
          .volatile_modeling_overlay_frame = snapshot.frame.get(),
          .volatile_modeling_source = {
              .source_identity = snapshot.source.source_identity,
              .publication_revision = snapshot.source.publication_revision},
          .scene_selection_mode = RenderOverlaySelectionMode::Component,
          .style_selection_context = ViewportOverlaySelectionContext::Edge,
          .overlays_visible = true,
          .selection_dimensions_visible = false,
      }, settings, target);
    };
    const auto expect_submitted_height = [&](float y) {
      ASSERT_EQ(target.frame.points.size(), 4u);
      for (const auto &point : target.frame.points) {
        EXPECT_NEAR(point.position.y, y, 0.0001f);
      }
      const int submitted = target.submissions;
      for (int frame = 0; frame < 8; ++frame) {
        EXPECT_FALSE(submit().dynamic_submitted);
      }
      EXPECT_EQ(target.submissions, submitted);
    };
    for (const bool control : {false, true, false}) {
      ASSERT_TRUE(harness.dispatch({.can_interact = true,
          .mouse_local = {0.37f, 0.0f}, .modifiers = {.primary = control}}).handled);
      ASSERT_TRUE(harness.session.modeling().tool_status().insert_edge_loop_has_preview);
      ASSERT_TRUE(submit().dynamic_submitted);
      expect_submitted_height(control ? 0.5f : 0.37f);
      harness.expect_documents(before);
      EXPECT_EQ(harness.session.history().undo_count(), 0u);
    }
    harness.begin_preview({0.37f, 0.0f});
    for (const bool control : {false, true, false}) {
      ASSERT_TRUE(harness.dispatch({.left_down = true, .can_interact = true,
          .mouse_local = {0.43f, 0.0f}, .modifiers = {.primary = control}}).handled);
      ASSERT_TRUE(submit().dynamic_submitted);
      expect_submitted_height(control ? 0.5f : 0.43f);
    }
    ASSERT_TRUE(harness.dispatch({.left_released = true,
                                 .can_interact = true}).handled);
    ASSERT_TRUE(submit().dynamic_submitted);
    EXPECT_TRUE(target.frame.points.empty());
    EXPECT_EQ(harness.session.history().undo_count(), 1u);
    const auto after = harness.documents();
    const auto &inserted = after.front().second.vertices;
    for (std::size_t index = before.front().second.vertices.size();
         index < inserted.size(); ++index) {
      EXPECT_NEAR(inserted[index].position.y, 0.43f, 0.0001f);
    }
    ASSERT_TRUE(harness.editor.history().undo().ok());
    harness.expect_documents(before);
    ASSERT_TRUE(harness.editor.history().redo().ok());
    harness.expect_documents(after);
    harness.begin_preview({0.2f, 0.0f});
    ASSERT_TRUE(harness.dispatch({.left_down = true, .can_interact = true,
        .mouse_local = {0.2f, 0.0f}, .modifiers = {.primary = true}}).handled);
    ASSERT_TRUE(submit().dynamic_submitted);
    ASSERT_TRUE(harness.runtime.handle_active_tool_event(harness.state,
        harness.context, {.kind = ViewportToolEventKind::Cancel,
                          .tool = ViewportToolId::InsertEdgeLoop}).handled);
    ASSERT_TRUE(submit().dynamic_submitted);
    EXPECT_TRUE(target.frame.points.empty());
    EXPECT_EQ(harness.session.history().undo_count(), 1u);
    harness.expect_documents(after);
  }
}

TEST(ViewportToolRuntimeTests,
     CutEndpointPressSamplesIdleCursorWithoutRebuildingTheAcceptedPreview) {
  ModelingPointerHistoryHarness harness;
  harness.initialize(ViewportToolId::Cut);
  harness.begin_preview();
  ASSERT_FALSE(::testing::Test::HasFatalFailure());
  const auto before = harness.session.modeling().tool_status();
  ASSERT_EQ(before.cut_captured_point_count, 2);
  ASSERT_FALSE(before.cut_dragging);
  const auto overlay = harness.session.modeling().volatile_overlay_snapshot();
  for (const float x : {0.4F, 0.6F, 0.8F}) {
    const auto hovered = harness.session.modeling().update_cut_hover(
        harness.point.make_event_at({x, 0.0F}, false));
    ASSERT_TRUE(hovered.ok) << hovered.message;
    EXPECT_FALSE(hovered.changed);
    const auto retained = harness.session.modeling().volatile_overlay_snapshot();
    EXPECT_EQ(retained.frame, overlay.frame);
    EXPECT_EQ(retained.source.publication_revision, overlay.source.publication_revision);
  }
  harness.point.hit_drawn_plane_captured_point = [](const auto&, int) {
    return std::optional<int>{0};
  };
  // The press is offset from endpoint zero, after the last idle sample at 0.8.
  // Its explicit sample must bypass hover throttling and retain -0.1 exactly.
  ASSERT_TRUE(harness.dispatch({.left_clicked = true, .left_down = true,
      .can_interact = true, .suppress_hover_work = true,
      .mouse_local = {-0.1F, 0.0F}, .left_click_local = {-0.1F, 0.0F}}).handled);
  ASSERT_TRUE(harness.session.modeling().tool_status().cut_dragging);
  ASSERT_TRUE(harness.dispatch({.left_down = true, .can_interact = true,
      .mouse_local = {-0.1F, 0.0F}}).handled);
  auto status = harness.session.modeling().tool_status();
  EXPECT_NEAR(status.cut_points[0].x, before.cut_points[0].x, 0.00001F);
  EXPECT_NEAR(status.cut_points[1].x, before.cut_points[1].x, 0.00001F);
  ASSERT_TRUE(harness.dispatch({.left_down = true, .can_interact = true,
      .mouse_local = {0.15F, 0.0F}}).handled);
  status = harness.session.modeling().tool_status();
  EXPECT_NEAR(status.cut_points[0].x, before.cut_points[0].x + 0.25F, 0.00001F);
  EXPECT_NEAR(status.cut_points[1].x, before.cut_points[1].x, 0.00001F);
  EXPECT_EQ(harness.session.history().undo_count(), 0U);
}

TEST(ViewportToolRuntimeTests,
     CutAndInsertEdgeLoopPointerCommitsHaveOneUndoStepAndExactRedo) {
  for (const auto tool : {ViewportToolId::Cut, ViewportToolId::InsertEdgeLoop}) {
    SCOPED_TRACE(static_cast<int>(tool));
    ModelingPointerHistoryHarness harness;
    harness.initialize(tool);
    const auto before = harness.documents();
    ASSERT_EQ(before.size(), 1u);
    const auto committed = harness.commit_pointer();
    ASSERT_TRUE(committed.handled);
    ASSERT_FALSE(harness.commands.empty());
    ASSERT_TRUE(harness.commands.back().ok) << harness.commands.back().message;
    ASSERT_TRUE(harness.commands.back().changed);
    const auto after = harness.documents();
    if (tool == ViewportToolId::Cut) {
      ASSERT_GT(after.size(), before.size());
    } else {
      ASSERT_EQ(after.size(), 1u);
      EXPECT_GT(after.front().second.vertices.size(),
                before.front().second.vertices.size());
    }
    EXPECT_EQ(harness.session.history().undo_count(), 1u);
    EXPECT_EQ(harness.state.active_tool, tool == ViewportToolId::InsertEdgeLoop
                                             ? tool : ViewportToolId::Select);
    EXPECT_EQ(committed.active_tool_changed, tool == ViewportToolId::Cut);
    ASSERT_TRUE(harness.editor.history().undo().ok());
    harness.expect_documents(before);
    EXPECT_EQ(harness.session.history().undo_count(), 0u);
    EXPECT_EQ(harness.session.history().redo_count(), 1u);
    ASSERT_TRUE(harness.editor.history().redo().ok());
    harness.expect_documents(after);
    EXPECT_EQ(harness.session.history().undo_count(), 1u);
    EXPECT_EQ(harness.session.history().redo_count(), 0u);
  }
}

TEST(ViewportToolRuntimeTests,
     PlaneAndPolyPreviewsSuspendAcrossNavigationAndResumeAtTheNextPointerSample) {
  for (const auto tool : {ViewportToolId::Cut, ViewportToolId::Mirror,
                          ViewportToolId::Poly}) {
    SCOPED_TRACE(static_cast<int>(tool));
    ModelingPointerHistoryHarness harness;
    harness.initialize(tool);
    const auto before = harness.documents();
    const bool plane_tool = tool != ViewportToolId::Poly;
    ASSERT_TRUE(harness.dispatch({.left_clicked = true,
                                  .left_down = plane_tool,
                                  .can_interact = true,
                                  .suppress_hover_work = true}).handled);
    ASSERT_TRUE(harness.dispatch({.left_down = plane_tool,
                                  .can_interact = true,
                                  .mouse_local = {1.0f, 0.0f}}).handled);
    const auto status = harness.session.modeling().tool_status();
    if (tool == ViewportToolId::Cut) {
      ASSERT_TRUE(status.cut_dragging);
      ASSERT_TRUE(status.cut_preview_valid);
    } else if (tool == ViewportToolId::Mirror) {
      ASSERT_TRUE(status.mirror_dragging);
      ASSERT_TRUE(status.mirror_preview_valid);
    } else {
      ASSERT_EQ(status.poly_point_count, 1);
      ASSERT_TRUE(status.poly_has_current);
    }
    const auto authored = harness.session.modeling().volatile_overlay_snapshot();
    ASSERT_NE(authored.frame, nullptr);
    ASSERT_FALSE(authored.frame->lines.empty());
    const auto preview_revision = harness.session.modeling().tool_preview_source_revision();
    harness.commands.clear();
    for (int frame = 0; frame < 8; ++frame) {
      ASSERT_TRUE(harness.dispatch({.left_down = plane_tool,
                                    .suppress_hover_work = true,
                                    .navigation_active = true,
                                    .mouse_local = {100.0f + frame, -100.0f},
                                    .camera_changed = true}).handled);
      const auto retained = harness.session.modeling().volatile_overlay_snapshot();
      // Retaining the immutable publication proves every world-space preview
      // primitive is unchanged and avoids rebuilding it for camera movement.
      EXPECT_EQ(retained.frame, authored.frame);
      EXPECT_EQ(retained.source.publication_revision,
                authored.source.publication_revision);
      EXPECT_EQ(harness.session.modeling().tool_preview_source_revision(), preview_revision);
      EXPECT_EQ(harness.session.history().undo_count(), 0u);
    }
    EXPECT_TRUE(harness.commands.empty());
    harness.expect_documents(before);

    // RMB release has no LMB edge. Suppress its recentered cursor sample just
    // as the host does, then resume authoring on the next real pointer sample.
    ASSERT_TRUE(harness.dispatch({.left_down = plane_tool,
                                  .suppress_hover_work = true}).handled);
    EXPECT_EQ(harness.session.modeling().volatile_overlay_snapshot().frame, authored.frame);
    ASSERT_TRUE(harness.dispatch({.left_down = plane_tool,
                                  .can_interact = true,
                                  .mouse_local = {0.5f, 0.0f}}).handled);
    ASSERT_FALSE(harness.commands.empty());
    EXPECT_TRUE(harness.commands.back().ok) << harness.commands.back().message;
    EXPECT_NE(harness.session.modeling().volatile_overlay_snapshot().frame, authored.frame);
    EXPECT_EQ(harness.session.history().undo_count(), 0u);
    harness.expect_documents(before);
  }
}

TEST(ViewportToolRuntimeTests,
     InsertEdgeLoopNavigationKeepsAuthoredDragUntilItsOwnLeftRelease) {
  ModelingPointerHistoryHarness harness;
  harness.initialize(ViewportToolId::InsertEdgeLoop);
  const auto before = harness.documents();
  harness.begin_preview();
  const auto authored = harness.session.modeling().volatile_overlay_snapshot();
  ASSERT_NE(authored.frame, nullptr);
  ASSERT_FALSE(authored.frame->lines.empty());
  harness.commands.clear();
  for (int frame = 0; frame < 8; ++frame) {
    ASSERT_TRUE(harness.dispatch({.left_down = true,
                                  .suppress_hover_work = true,
                                  .navigation_active = true,
                                  .mouse_local = {100.0f + frame, -100.0f},
                                  .camera_changed = true}).handled);
    EXPECT_TRUE(harness.session.modeling().tool_status().insert_edge_loop_dragging);
    EXPECT_TRUE(harness.session.modeling().tool_status().insert_edge_loop_has_preview);
    const auto retained = harness.session.modeling().volatile_overlay_snapshot();
    EXPECT_EQ(retained.frame, authored.frame);
    EXPECT_EQ(retained.source.publication_revision, authored.source.publication_revision);
    EXPECT_EQ(harness.session.history().undo_count(), 0u);
  }
  EXPECT_TRUE(harness.commands.empty());
  harness.expect_documents(before);
  ASSERT_TRUE(harness.dispatch({.left_released = true,
                                .suppress_hover_work = true,
                                .navigation_active = true,
                                .mouse_local = {100.0f, -100.0f}}).handled);
  ASSERT_FALSE(harness.commands.empty());
  EXPECT_TRUE(harness.commands.back().ok) << harness.commands.back().message;
  EXPECT_FALSE(harness.session.modeling().tool_status().insert_edge_loop_dragging);
  EXPECT_EQ(harness.session.history().undo_count(), 1u);
  EXPECT_EQ(harness.state.active_tool, ViewportToolId::InsertEdgeLoop);
  const auto after = harness.documents();
  ASSERT_EQ(after.size(), 1u);
  EXPECT_GT(after.front().second.vertices.size(), before.front().second.vertices.size());
  ASSERT_TRUE(harness.editor.history().undo().ok());
  harness.expect_documents(before);
}

TEST(ViewportToolRuntimeTests,
     InsertEdgeLoopReleaseCommitPreservesActiveToolAndRecordsHistory) {
  ModelingPointerHistoryHarness harness;
  harness.initialize(ViewportToolId::InsertEdgeLoop);
  const auto before = harness.documents();
  harness.begin_preview();
  EXPECT_EQ(harness.session.history().undo_count(), 0u);
  const auto committed = harness.dispatch({.left_released = true,
                                            .can_interact = true,
                                            .suppress_hover_work = true});
  ASSERT_TRUE(committed.handled);
  ASSERT_FALSE(harness.commands.empty());
  ASSERT_TRUE(harness.commands.back().ok) << harness.commands.back().message;
  ASSERT_TRUE(harness.commands.back().changed);
  EXPECT_FALSE(committed.sync_active_tool_from_modeling);
  EXPECT_FALSE(committed.active_tool_changed);
  EXPECT_EQ(harness.state.active_tool, ViewportToolId::InsertEdgeLoop);
  ASSERT_NE(harness.runtime.active_tool(), nullptr);
  EXPECT_EQ(harness.runtime.active_tool()->viewport_tool(),
            ViewportToolId::InsertEdgeLoop);
  EXPECT_EQ(harness.session.modeling().tool_status().active_tool,
            ModelingViewportTool::InsertEdgeLoop);
  EXPECT_EQ(harness.session.history().undo_count(), 1u);
  const auto after = harness.documents();
  ASSERT_TRUE(harness.editor.history().undo().ok());
  harness.expect_documents(before);
  ASSERT_TRUE(harness.editor.history().redo().ok());
  harness.expect_documents(after);
}

TEST(ViewportToolRuntimeTests,
     PersistentInsertEdgeLoopReplaysEachCutAndRefreshesPreviewAfterHistory) {
  ModelingPointerHistoryHarness harness;
  harness.initialize(ViewportToolId::InsertEdgeLoop);
  std::vector<ModelingPointerHistoryHarness::Documents> snapshots{
      harness.documents()};
  const auto expect_active_without_preview = [&] {
    EXPECT_EQ(harness.state.active_tool, ViewportToolId::InsertEdgeLoop);
    EXPECT_EQ(harness.editor.tools().active_tool(), "insert_edge_loop");
    const auto status = harness.session.modeling().tool_status();
    EXPECT_EQ(status.active_tool, ModelingViewportTool::InsertEdgeLoop);
    EXPECT_FALSE(status.insert_edge_loop_dragging);
    EXPECT_FALSE(status.insert_edge_loop_has_preview);
  };

  for (const float position : {-0.5f, 0.0f, 0.5f}) {
    SCOPED_TRACE(position);
    // Press and release can arrive together; each physical click inserts once.
    const auto committed = harness.dispatch({
        .left_clicked = true,
        .left_released = true,
        .can_interact = true,
        .suppress_hover_work = true,
        .left_click_local = {position, 0.0f},
    });
    ASSERT_TRUE(committed.handled);
    EXPECT_FALSE(committed.active_tool_changed);
    EXPECT_FALSE(committed.sync_active_tool_from_modeling);
    ASSERT_FALSE(harness.commands.empty());
    ASSERT_TRUE(harness.commands.back().ok) << harness.commands.back().message;
    ASSERT_TRUE(harness.commands.back().changed);
    const auto after = harness.documents();
    ASSERT_EQ(after.size(), 1u);
    EXPECT_GT(after.front().second.vertices.size(),
              snapshots.back().front().second.vertices.size());
    EXPECT_EQ(harness.session.history().undo_count(), snapshots.size());
    snapshots.push_back(after);
    expect_active_without_preview();
    harness.commands.clear();
    EXPECT_TRUE(harness.dispatch({.left_released = true,
                                  .can_interact = true,
                                  .suppress_hover_work = true}).handled);
    EXPECT_TRUE(harness.commands.empty());
    EXPECT_EQ(harness.session.history().undo_count(), snapshots.size() - 1u);
    harness.expect_documents(after);
  }

  const auto hover_before_history = [&] {
    ASSERT_TRUE(harness.dispatch({.can_interact = true,
                                  .mouse_local = {0.75f, 0.0f}}).handled);
    const auto status = harness.session.modeling().tool_status();
    ASSERT_TRUE(status.insert_edge_loop_has_preview);
    EXPECT_FALSE(status.insert_edge_loop_dragging);
  };
  for (std::size_t remaining = snapshots.size() - 1u; remaining > 0u; --remaining) {
    hover_before_history();
    ASSERT_TRUE(harness.editor.history().undo().ok());
    harness.expect_documents(snapshots[remaining - 1u]);
    EXPECT_EQ(harness.session.history().undo_count(), remaining - 1u);
    expect_active_without_preview();
  }
  for (std::size_t index = 1u; index < snapshots.size(); ++index) {
    hover_before_history();
    ASSERT_TRUE(harness.editor.history().redo().ok());
    harness.expect_documents(snapshots[index]);
    EXPECT_EQ(harness.session.history().undo_count(), index);
    expect_active_without_preview();
  }

  harness.commands.clear();
  ASSERT_TRUE(harness.dispatch({.can_interact = true,
                                .mouse_local = {0.75f, 0.0f}}).handled);
  ASSERT_TRUE(harness.session.modeling().tool_status().insert_edge_loop_has_preview);
  ASSERT_TRUE(harness.dispatch({.left_clicked = true,
                                .can_interact = true,
                                .suppress_hover_work = true,
                                .left_click_local = {0.75f, 0.0f}}).handled);
  ASSERT_FALSE(harness.commands.empty());
  ASSERT_TRUE(harness.commands.back().ok) << harness.commands.back().message;
  ASSERT_TRUE(harness.commands.back().changed);
  EXPECT_EQ(harness.session.history().undo_count(), 4u);
  expect_active_without_preview();
  ASSERT_TRUE(harness.editor.history().undo().ok());
  harness.expect_documents(snapshots.back());
}

TEST(ViewportToolRuntimeTests,
     InsertEdgeLoopInterruptedReleaseAcceptsPreviewAndPreservesActivation) {
  for (int interruption = 0; interruption < 4; ++interruption) {
    SCOPED_TRACE(interruption);
    ModelingPointerHistoryHarness harness;
    harness.initialize(ViewportToolId::InsertEdgeLoop);
    const auto before = harness.documents();
    harness.begin_preview();
    const auto interrupted = harness.dispatch({
        .left_released = true,
        .can_interact = interruption != 0,
        .suppress_hover_work = true,
        .navigation_active = interruption == 1,
        .viewport_focused = interruption != 2,
        .pointer_interaction_canceled = interruption == 3,
    });
    EXPECT_TRUE(interrupted.handled);
    EXPECT_FALSE(interrupted.active_tool_changed);
    EXPECT_FALSE(interrupted.sync_active_tool_from_modeling);
    EXPECT_EQ(harness.state.active_tool, ViewportToolId::InsertEdgeLoop);
    EXPECT_EQ(harness.editor.tools().active_tool(), "insert_edge_loop");
    EXPECT_FALSE(harness.session.modeling().tool_status().insert_edge_loop_dragging);
    EXPECT_FALSE(harness.session.modeling().tool_status().insert_edge_loop_has_preview);
    EXPECT_EQ(harness.session.history().undo_count(), 1u);
    const auto after = harness.documents();
    ASSERT_EQ(after.size(), before.size());
    EXPECT_GT(after.front().second.vertices.size(), before.front().second.vertices.size());
    EXPECT_TRUE(harness.dispatch({.left_released = true,
                                  .can_interact = true,
                                  .suppress_hover_work = true}).handled);
    EXPECT_EQ(harness.session.history().undo_count(), 1u);
    harness.expect_documents(after);
    ASSERT_TRUE(harness.editor.history().undo().ok());
    harness.expect_documents(before);
    ASSERT_TRUE(harness.editor.history().redo().ok());
    harness.expect_documents(after);
  }
}

TEST(ViewportToolRuntimeTests,
     InsertEdgeLoopExplicitToolChangeAcceptsPreviewAndSuppressesOldRelease) {
  ModelingPointerHistoryHarness harness;
  harness.initialize(ViewportToolId::InsertEdgeLoop);
  const auto before = harness.documents();
  harness.begin_preview();
  ASSERT_TRUE(harness.editor.tools().activate(ViewportToolId::Move).ok());
  ASSERT_TRUE(harness.runtime.set_active_tool(harness.state, ViewportToolId::Move));
  harness.runtime.clear_modal_tool_state();
  EXPECT_EQ(harness.editor.tools().active_tool(), "move");
  EXPECT_FALSE(harness.session.modeling().tool_status().insert_edge_loop_dragging);
  EXPECT_FALSE(harness.session.modeling().tool_status().insert_edge_loop_has_preview);
  harness.dispatch({.left_released = true, .can_interact = true});
  EXPECT_EQ(harness.session.history().undo_count(), 1u);
  const auto after = harness.documents();
  ASSERT_EQ(after.size(), before.size());
  EXPECT_GT(after.front().second.vertices.size(), before.front().second.vertices.size());
  ASSERT_TRUE(harness.editor.history().undo().ok());
  harness.expect_documents(before);
  ASSERT_TRUE(harness.editor.tools().activate(ViewportToolId::InsertEdgeLoop).ok());
  ASSERT_TRUE(harness.runtime.set_active_tool(harness.state,
                                            ViewportToolId::InsertEdgeLoop));
  ASSERT_TRUE(harness.commit_pointer().handled);
  EXPECT_EQ(harness.session.history().undo_count(), 1u);
}

TEST(ViewportToolRuntimeTests,
     InsertEdgeLoopFocusAloneRetainsDragAndFailedInterruptionPreservesPreview) {
  ModelingPointerHistoryHarness harness;
  harness.initialize(ViewportToolId::InsertEdgeLoop);
  const auto before = harness.documents();
  harness.begin_preview();
  EXPECT_TRUE(harness.dispatch({.left_down = true,
                                .viewport_focused = false}).handled);
  EXPECT_TRUE(harness.session.modeling().tool_status().insert_edge_loop_dragging);
  EXPECT_EQ(harness.session.history().undo_count(), 0u);
  harness.expect_documents(before);
  RejectingToolCommitPort rejected;
  harness.context.ports.commit = &rejected;
  EXPECT_TRUE(harness.dispatch({.left_released = true,
                                .pointer_interaction_canceled = true}).handled);
  ASSERT_FALSE(harness.commands.empty());
  EXPECT_FALSE(harness.commands.back().ok);
  EXPECT_TRUE(harness.session.modeling().tool_status().insert_edge_loop_dragging);
  EXPECT_TRUE(harness.session.modeling().tool_status().insert_edge_loop_has_preview);
  EXPECT_EQ(harness.session.history().undo_count(), 0u);
  harness.expect_documents(before);
  harness.context.ports.commit = &harness.commit_adapter;
  EXPECT_TRUE(harness.dispatch({.left_released = true,
                                .pointer_interaction_canceled = true}).handled);
  EXPECT_FALSE(harness.session.modeling().tool_status().insert_edge_loop_dragging);
  EXPECT_EQ(harness.session.history().undo_count(), 1u);
  ASSERT_TRUE(harness.editor.history().undo().ok());
  harness.expect_documents(before);
}

TEST(ViewportToolRuntimeTests,
     CutAndInsertEdgeLoopHistoryInterleavesWithOrdinarySceneEdits) {
  for (const auto tool : {ViewportToolId::Cut, ViewportToolId::InsertEdgeLoop}) {
    SCOPED_TRACE(static_cast<int>(tool));
    ModelingPointerHistoryHarness harness;
    harness.initialize(tool);
    const auto source = harness.source.entity;
    ASSERT_TRUE(harness.editor.scene().rename(source, "Before Pointer Commit").ok());
    const auto before = harness.documents();
    ASSERT_TRUE(harness.commit_pointer().handled);
    const auto after = harness.documents();
    ASSERT_TRUE(harness.editor.scene().rename(source, "After Pointer Commit").ok());
    EXPECT_EQ(harness.session.history().undo_count(), 3u);
    ASSERT_TRUE(harness.editor.history().undo().ok());
    harness.expect_documents(after);
    EXPECT_EQ(harness.editor.query().entity(source)->name, "Before Pointer Commit");
    ASSERT_TRUE(harness.editor.history().undo().ok());
    harness.expect_documents(before);
    EXPECT_EQ(harness.editor.query().entity(source)->name, "Before Pointer Commit");
    ASSERT_TRUE(harness.editor.history().undo().ok());
    EXPECT_EQ(harness.editor.query().entity(source)->name, "Pointer History Source");
    EXPECT_FALSE(harness.editor.history().can_undo());
    ASSERT_TRUE(harness.editor.history().redo().ok());
    harness.expect_documents(before);
    ASSERT_TRUE(harness.editor.history().redo().ok());
    harness.expect_documents(after);
    ASSERT_TRUE(harness.editor.history().redo().ok());
    harness.expect_documents(after);
    EXPECT_EQ(harness.editor.query().entity(source)->name, "After Pointer Commit");
    EXPECT_FALSE(harness.editor.history().can_redo());
  }
}

TEST(ViewportToolRuntimeTests,
     CutAndInsertEdgeLoopCanceledOrMissingPreviewsDoNotRecordHistory) {
  for (const auto tool : {ViewportToolId::Cut, ViewportToolId::InsertEdgeLoop}) {
    for (const bool valid_preview : {false, true}) {
      SCOPED_TRACE(static_cast<int>(tool));
      SCOPED_TRACE(valid_preview);
      ModelingPointerHistoryHarness harness;
      harness.initialize(tool);
      ASSERT_TRUE(harness.editor.scene()
                      .rename(harness.source.entity,
                              "Redo Survives Modal Cancellation")
                      .ok());
      ASSERT_TRUE(harness.editor.history().undo().ok());
      ASSERT_TRUE(harness.editor.tools().activate(tool).ok());
      ASSERT_TRUE(harness.runtime.set_active_tool(harness.state, tool));
      const auto before = harness.documents();
      if (valid_preview) {
        harness.begin_preview();
      } else {
        EXPECT_TRUE(harness.dispatch({
            .left_clicked = tool == ViewportToolId::InsertEdgeLoop,
            .can_interact = true,
            .finalize_pressed = tool == ViewportToolId::Cut,
            .suppress_hover_work = true,
            .left_click_local = {100.0f, 100.0f},
        }).handled);
      }
      EXPECT_EQ(harness.session.history().undo_count(), 0u);
      EXPECT_EQ(harness.session.history().redo_count(), 1u);
      const auto canceled = harness.runtime.handle_active_tool_event(
          harness.state, harness.context,
          {.kind = ViewportToolEventKind::Cancel,
           .tool = tool,
           .exit_active_modal_tool = true});
      EXPECT_TRUE(canceled.handled);
      EXPECT_TRUE(canceled.active_tool_changed);
      EXPECT_TRUE(canceled.sync_active_tool_from_modeling);
      EXPECT_EQ(harness.state.active_tool, ViewportToolId::Select);
      ASSERT_NE(harness.runtime.active_tool(), nullptr);
      EXPECT_EQ(harness.runtime.active_tool()->viewport_tool(),
                ViewportToolId::Select);
      EXPECT_EQ(harness.editor.tools().active_tool(), "select");
      harness.expect_documents(before);
      EXPECT_EQ(harness.session.history().undo_count(), 0u);
      EXPECT_EQ(harness.session.history().redo_count(), 1u);
      EXPECT_FALSE(harness.editor.history().can_undo());
      EXPECT_TRUE(harness.editor.history().can_redo());
      if (tool == ViewportToolId::InsertEdgeLoop) {
        const ModelingToolStatus status =
            harness.session.modeling().tool_status();
        EXPECT_FALSE(status.insert_edge_loop_dragging);
        EXPECT_FALSE(status.insert_edge_loop_has_preview);
      }
      ASSERT_TRUE(harness.editor.history().redo().ok());
      EXPECT_EQ(harness.editor.query().entity(harness.source.entity)->name,
                "Redo Survives Modal Cancellation");
      harness.expect_documents(before);
    }
  }
}

TEST(ViewportToolRuntimeTests,
     MissingCommitPortCannotAcceptCutOrInsertEdgeLoopPreview) {
  for (const auto tool : {ViewportToolId::Cut, ViewportToolId::InsertEdgeLoop}) {
    SCOPED_TRACE(static_cast<int>(tool));
    ModelingPointerHistoryHarness harness;
    harness.initialize(tool);
    const auto before = harness.documents();
    harness.begin_preview();
    harness.commands.clear();
    harness.context.ports = harness.tool_ports.ports();
    ASSERT_EQ(harness.context.ports.commit, nullptr);

    const auto blocked = harness.dispatch({
        .left_released = tool == ViewportToolId::InsertEdgeLoop,
        .can_interact = true,
        .finalize_pressed = tool == ViewportToolId::Cut,
        .suppress_hover_work = true,
    });
    EXPECT_FALSE(blocked.active_tool_changed);
    EXPECT_FALSE(blocked.sync_active_tool_from_modeling);
    EXPECT_TRUE(harness.commands.empty());
    EXPECT_EQ(harness.state.active_tool, tool);
    EXPECT_EQ(harness.session.history().undo_count(), 0u);
    harness.expect_documents(before);

    const auto canceled = harness.runtime.handle_active_tool_event(
        harness.state, harness.context,
        {.kind = ViewportToolEventKind::Cancel,
         .tool = tool,
         .exit_active_modal_tool = true});
    EXPECT_TRUE(canceled.handled);
    harness.expect_documents(before);
    EXPECT_FALSE(harness.editor.history().can_undo());
  }
}

TEST(ViewportToolRuntimeTests, EmptyKnifeCommitPreservesExistingRedo) {
  ModelingPointerHistoryHarness harness;
  harness.initialize(ViewportToolId::Knife);
  ASSERT_TRUE(harness.editor.scene()
                  .rename(harness.source.entity, "Redo Survives Empty Knife")
                  .ok());
  ASSERT_TRUE(harness.editor.history().undo().ok());
  ASSERT_TRUE(harness.editor.tools().activate(ViewportToolId::Knife).ok());
  const auto before = harness.documents();
  const auto content_revision = harness.session.modeling().content_revision();
  const auto no_op = harness.context.ports.commit->commit_knife_tool();
  EXPECT_TRUE(no_op.ok) << no_op.message;
  EXPECT_FALSE(no_op.changed);
  EXPECT_EQ(harness.session.modeling().content_revision(), content_revision);
  harness.expect_documents(before);
  EXPECT_EQ(harness.session.history().undo_count(), 0u);
  EXPECT_EQ(harness.session.history().redo_count(), 1u);

  ASSERT_TRUE(harness.editor.tools().cancel_modal_tool().ok());
  ASSERT_TRUE(harness.editor.history().redo().ok());
  EXPECT_EQ(harness.editor.query().entity(harness.source.entity)->name,
            "Redo Survives Empty Knife");
  harness.expect_documents(before);
}

TEST(ViewportToolRuntimeTests,
     UnsupportedOrMismatchedCommitCannotConsumeAnActiveCutPreview) {
  ModelingPointerHistoryHarness harness;
  harness.initialize(ViewportToolId::Cut);
  harness.begin_preview();
  const auto before = harness.documents();
  const auto content_revision = harness.session.modeling().content_revision();
  for (const auto requested : {ViewportToolId::Select, ViewportToolId::LassoSelect,
                               ViewportToolId::InsertEdgeLoop,
                               ViewportToolId::Mirror}) {
    SCOPED_TRACE(static_cast<int>(requested));
    const auto rejected = harness.editor.tools().commit_modeling_tool(requested);
    EXPECT_FALSE(rejected.ok());
    EXPECT_FALSE(rejected.changed());
    EXPECT_FALSE(rejected.error_message().empty());
    EXPECT_TRUE(harness.session.modeling().tool_status().cut_preview_valid);
    EXPECT_EQ(harness.editor.tools().active_tool(),
              viewport_tool_id(ViewportToolId::Cut));
    EXPECT_EQ(harness.session.modeling().content_revision(), content_revision);
    EXPECT_EQ(harness.session.history().undo_count(), 0u);
    harness.expect_documents(before);
  }
  const auto committed = harness.dispatch({.can_interact = true,
                                            .finalize_pressed = true,
                                            .suppress_hover_work = true});
  ASSERT_TRUE(committed.handled);
  ASSERT_FALSE(harness.commands.empty());
  EXPECT_TRUE(harness.commands.back().ok) << harness.commands.back().message;
  EXPECT_EQ(harness.session.history().undo_count(), 1u);
}

TEST(ViewportToolRuntimeTests,
     StaleCutAndInsertEdgeLoopCommitPreservesHistoryAndTopology) {
  for (const auto tool : {ViewportToolId::Cut, ViewportToolId::InsertEdgeLoop}) {
    SCOPED_TRACE(static_cast<int>(tool));
    ModelingPointerHistoryHarness harness;
    harness.initialize(tool);
    const auto before = harness.documents();
    ASSERT_TRUE(harness.commit_pointer().handled);
    ASSERT_FALSE(harness.commands.empty());
    ASSERT_TRUE(harness.commands.back().ok) << harness.commands.back().message;
    const auto after = harness.documents();
    const auto rejected = harness.editor.tools().commit_modeling_tool(tool);
    EXPECT_FALSE(rejected.ok());
    EXPECT_FALSE(rejected.changed());
    EXPECT_EQ(harness.session.history().undo_count(), 1u);
    harness.expect_documents(after);

    ASSERT_TRUE(harness.editor.history().undo().ok());
    harness.expect_documents(before);
    const auto rejected_after_undo =
        harness.editor.tools().commit_modeling_tool(tool);
    EXPECT_FALSE(rejected_after_undo.ok());
    EXPECT_FALSE(rejected_after_undo.changed());
    EXPECT_EQ(harness.session.history().undo_count(), 0u);
    EXPECT_EQ(harness.session.history().redo_count(), 1u);
    ASSERT_TRUE(harness.editor.history().redo().ok());
    harness.expect_documents(after);
  }
}

TEST(ViewportToolRuntimeTests,
     CutUndoPreservesSelectedGroupAndRedoRestoresExactResultSelection) {
  ModelingPointerHistoryHarness harness;
  harness.initialize(ViewportToolId::Cut);
  const auto grouped = harness.editor.scene().group_selection("Cut Group");
  ASSERT_TRUE(grouped.ok()) << grouped.error_message();
  const auto group = grouped.value().id;
  const auto target = harness.editor.modeling().create_box(
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      "Selected Cut Target");
  ASSERT_TRUE(target.ok()) << target.error_message();
  ASSERT_TRUE(harness.editor.selection().set_mode(QuaderSelectionMode::Object).ok());
  // Object mode projects the direct mesh into Cut's SDK selection. The
  // accompanying group remains scene-owned throughout the history transaction.
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects({group, target.value().entity},
                                  target.value().entity)
                  .ok());
  const auto activated = harness.editor.tools().activate(ViewportToolId::Cut);
  ASSERT_TRUE(activated.ok()) << activated.error_message();
  harness.session.history().clear();
  ASSERT_EQ(harness.editor.selection().summary().mode, QuaderSelectionMode::Object);
  const auto before = harness.documents();
  const auto selected_before = harness.session.scene().selected_entities();
  const auto active_before = harness.session.scene().primary_selection();
  ASSERT_EQ(selected_before.size(), 2u);
  ASSERT_NE(std::find(selected_before.begin(), selected_before.end(), group),
            selected_before.end());
  ASSERT_NE(std::find(selected_before.begin(), selected_before.end(),
                      target.value().entity),
            selected_before.end());
  ASSERT_EQ(active_before, target.value().entity);
  ASSERT_TRUE(harness.commit_pointer().handled);
  ASSERT_FALSE(harness.commands.empty());
  ASSERT_TRUE(harness.commands.back().ok) << harness.commands.back().message;
  ASSERT_TRUE(harness.commands.back().changed);
  const auto after = harness.documents();
  const auto selected_after = harness.session.scene().selected_entities();
  const auto active_after = harness.session.scene().primary_selection();
  const auto mode_after = harness.editor.selection().summary().mode;
  EXPECT_EQ(harness.session.history().undo_count(), 1u);

  ASSERT_TRUE(harness.editor.history().undo().ok());
  harness.expect_documents(before);
  ASSERT_TRUE(harness.editor.query().entity(group).has_value());
  EXPECT_EQ(harness.editor.query().entity(group)->name, "Cut Group");
  EXPECT_EQ(harness.session.scene().selected_entities(), selected_before);
  EXPECT_EQ(harness.session.scene().primary_selection(), active_before);
  EXPECT_EQ(harness.editor.selection().summary().mode, QuaderSelectionMode::Object);

  ASSERT_TRUE(harness.editor.history().redo().ok());
  harness.expect_documents(after);
  EXPECT_EQ(harness.session.scene().selected_entities(), selected_after);
  EXPECT_EQ(harness.session.scene().primary_selection(), active_after);
  EXPECT_EQ(harness.editor.selection().summary().mode, mode_after);
}

TEST(ViewportToolRuntimeTests,
     FailedCutAndInsertEdgeLoopPublicationPreservesTopologySelectionAndRedo) {
  for (const auto tool : {ViewportToolId::Cut, ViewportToolId::InsertEdgeLoop}) {
    for (const auto failure : {ModelingSdkProjectionFailurePoint::AfterSdkMutation,
                              ModelingSdkProjectionFailurePoint::BeforeCommit}) {
      SCOPED_TRACE(static_cast<int>(tool));
      SCOPED_TRACE(static_cast<int>(failure));
      ModelingPointerHistoryHarness harness;
      harness.initialize(tool);
      ASSERT_TRUE(harness.editor.scene()
                      .rename(harness.source.entity, "Redo After Failed Commit")
                      .ok());
      ASSERT_TRUE(harness.editor.history().undo().ok());
      ASSERT_TRUE(harness.editor.tools().activate(tool).ok());
      harness.begin_preview();
      harness.commands.clear();
      const auto before = harness.documents();
      const auto selected = harness.session.scene().selected_entities();
      const auto active = harness.session.scene().primary_selection();
      const auto content_revision = harness.session.modeling().content_revision();
      const auto assets = harness.session.assets().mesh_ids();
      harness.session.modeling().set_modeling_sdk_projection_failure_for_tests(failure);
      const auto failed = harness.dispatch({
          .left_released = tool == ViewportToolId::InsertEdgeLoop,
          .can_interact = true,
          .finalize_pressed = tool == ViewportToolId::Cut,
          .suppress_hover_work = true,
      });
      harness.session.modeling().set_modeling_sdk_projection_failure_for_tests(
          ModelingSdkProjectionFailurePoint::None);
      EXPECT_TRUE(failed.handled);
      EXPECT_FALSE(failed.active_tool_changed);
      ASSERT_FALSE(harness.commands.empty());
      EXPECT_FALSE(harness.commands.back().ok);
      EXPECT_FALSE(harness.commands.back().changed);
      harness.expect_documents(before);
      EXPECT_EQ(harness.session.scene().selected_entities(), selected);
      EXPECT_EQ(harness.session.scene().primary_selection(), active);
      EXPECT_EQ(harness.session.assets().mesh_ids(), assets);
      EXPECT_EQ(harness.session.modeling().content_revision(), content_revision);
      EXPECT_EQ(harness.session.history().undo_count(), 0u);
      EXPECT_EQ(harness.session.history().redo_count(), 1u);
      ASSERT_TRUE(harness.editor.tools().cancel_modal_tool().ok());
      ASSERT_TRUE(harness.editor.history().redo().ok());
      EXPECT_EQ(harness.editor.query().entity(harness.source.entity)->name,
                "Redo After Failed Commit");
      harness.expect_documents(before);
    }
  }
}

} // namespace
} // namespace quader
