////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_editor_api.hpp"
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_api_type_conversions.hpp"
#include "assets/asset_registry.hpp"
#include "editor/interactions/tool_identity.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/modeling/services/primitive_creation_tool_service.hpp"
#include "editor/metadata/host_contract.hpp"
#include "scene/scene_document.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/tools/viewport_tool_catalog.hpp"
#include "editor/tools/viewport_tool_ports.hpp"
#include "editor/tools/viewport_tool_runtime.hpp"
#include "support/guardrail_file_scan.hpp"
#include "viewport/host_viewport_contract_adapter.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

namespace quader {
namespace {

using test::guardrail::read_source_file;
using test::guardrail::source_root;
using test::guardrail::source_slice;

/// Injects deterministic Box preview failures into viewport-tool tests.
class FailingBoxPreviewPorts final : public ViewportToolHoverPort,
                                     public ViewportToolPreviewPort {
public:
  bool fail_query = false;
  int hover_updates = 0;
  int invalidations = 0;

  bool claim(ModelingBoxDragState) override { return true; }
  void invalidate() override { ++invalidations; }
  ModelingBoxPreview update_box_hover(const ModelingBoxToolEvent &) override {
    ++hover_updates;
    return {.ok = false, .message = "Forced Box hover failure."};
  }
  ModelingCommandResult
  update_insert_edge_loop_hover(const ModelingToolRayEvent &) override {
    return {.ok = true};
  }
  ModelingCommandResult
  update_knife_hover(const ModelingToolRayEvent &) override {
    return {.ok = true};
  }
  ModelingCommandResult clear_knife_hover() override { return {.ok = true}; }
  ModelingCommandResult
  update_poly_hover(const ModelingToolRayEvent &) override {
    return {.ok = true};
  }
  ModelingCommandResult
  update_drawn_plane_hover(ModelingViewportTool,
                           const ModelingToolRayEvent &) override {
    return {.ok = true};
  }

  ModelingToolStatus tool_status() const override { return {}; }
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
    return {.ok = true};
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
                              const ModelingToolRayEvent &) override {
    return {.ok = true};
  }
  ModelingCommandResult
  click_drawn_plane_point(ModelingViewportTool,
                          const ModelingToolRayEvent &) override {
    return {.ok = true};
  }
  ModelingCommandResult end_drawn_plane_drag(ModelingViewportTool) override {
    return {.ok = true};
  }
  ModelingCommandResult cycle_cut_mode() override { return {.ok = true}; }
};

void expect_point_tool_click_uses_queued_press_coordinate(
    ViewportToolId tool_id, bool expected_dragging) {
  SceneDocument document;
  AssetRegistry assets;
  ModelingSessionService modeling(document, assets);
  ViewportModelingToolPortAdapter tool_ports(modeling);
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
      .ports = tool_ports.ports(),
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

TEST(ViewportToolCatalogTests, ExposesToolMetadataAndConversions) {
  const Tool *primitive = find_viewport_tool("create_primitive");
  ASSERT_NE(primitive, nullptr);
  EXPECT_EQ(primitive->viewport_tool(), ViewportToolId::CreatePrimitive);
  EXPECT_EQ(primitive->modeling_tool(), ModelingViewportTool::Box);
  EXPECT_EQ(primitive->kind(), ToolKind::Modal);
  EXPECT_EQ(primitive->label(), "Create Primitive");
  EXPECT_EQ(primitive->shortcut(), "Shift+B");
  EXPECT_TRUE(primitive->owns_viewport());
  EXPECT_FALSE(primitive->blocks_viewport_focus_shortcuts());
  EXPECT_EQ(find_viewport_tool("box"), nullptr);
  EXPECT_EQ(find_viewport_tool("cylinder"), nullptr);
  EXPECT_EQ(find_viewport_tool("sphere"), nullptr);

  const Tool *cut = find_viewport_tool("cut");
  ASSERT_NE(cut, nullptr);
  EXPECT_TRUE(cut->shortcut().empty());

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

TEST(ViewportToolRuntimeTests, RuntimeRoutesToolEventsToActiveTool) {
    ViewportToolRuntime runtime;
    ViewportToolState state;
    ViewportCameraController camera;
    SceneDocument document;
    AssetRegistry assets;
    ModelingSessionService modeling(document, assets);
    ViewportModelingToolPortAdapter tool_ports(modeling);

    ASSERT_TRUE(runtime.set_active_tool(state, ViewportToolId::Mirror));
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
            .tool = ViewportToolId::Mirror,
            .exit_active_modal_tool = true,
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
      .ports = tool_ports.ports(),
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
      .ports = tool_ports.ports(),
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
  const ViewportOverlayFrame overlay = modeling.selection_overlay_frame();
  EXPECT_TRUE(std::ranges::any_of(
      overlay.lines, [](const ViewportOverlayLine &line) {
        return line.source == ViewportOverlaySource::Tool ||
               line.layer == ViewportOverlayLayer::HoverEdge;
      }));
}

TEST(ViewportToolRuntimeTests, BoxToolCommitUsesCommandOwnedTransaction) {
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
    ASSERT_TRUE(session.modeling().activate_box_tool().ok);

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
    box_context.make_event = [&]() { return current_event; };
    box_context.commit_preview = [&](std::string name,
                                     AssetId default_material) {
      ++commit_count;
      const QuaderApiResult<QuaderModelingObjectRef> result =
          editor.modeling().commit_box_preview(std::move(name),
                                               default_material);
      ModelingCreateBoxResult committed{
          .ok = result.ok,
          .changed = result.changed,
          .message = result.message,
      };
      if (result.ok && result.changed) {
        committed_entity = result.value.entity;
        committed.object = {
            .modeling_object_id = result.value.modeling_object_id,
            .entity = result.value.entity,
            .mesh = result.value.mesh,
            .name = "Viewport Box",
        };
      }
      return committed;
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
    });
    ASSERT_TRUE(result.handled);
    ASSERT_EQ(session.modeling().box_preview().drag_state,
              ModelingBoxDragState::Footprint);

    current_event = footprint_end;
    result = dispatch_frame(ViewportPointerToolEvent{
        .hovered = true,
    });
    ASSERT_TRUE(result.handled);
    ASSERT_TRUE(session.modeling().box_preview().valid);
    ASSERT_EQ(session.modeling().box_preview().drag_state,
              ModelingBoxDragState::Footprint);
    EXPECT_EQ(commit_count, 0);

    current_event = footprint_end;
    result = dispatch_frame(ViewportPointerToolEvent{
        .hovered = true,
        .left_released = true,
    });
    ASSERT_TRUE(result.handled);
    ASSERT_TRUE(result.active_tool_changed);
    EXPECT_EQ(session.modeling().box_preview().drag_state,
              ModelingBoxDragState::Idle);
    EXPECT_EQ(commit_count, 1);
    ASSERT_NE(committed_entity, kInvalidEntityId);
    ASSERT_TRUE(session.scene().exists(committed_entity));
    ASSERT_NE(session.scene().material(committed_entity), nullptr);
    EXPECT_EQ(session.scene().material(committed_entity)->material,
              material.value.id);
    EXPECT_EQ(session.scene().primary_selection(), committed_entity);

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
     PersistentPrimitivePreservesSnapModeAndRollsBackAfterPreviewFailure) {
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
  box_context.make_event = [&]() { return current_event; };
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

  ASSERT_TRUE(dispatch_frame({.hovered = true, .left_clicked = true}).handled);
  current_event.ray.origin = {6.4f, 4.0f, 7.6f};
  ASSERT_TRUE(dispatch_frame({.hovered = true, .left_released = true}).handled);
  EXPECT_FALSE(accepted_snap_enabled);
  EXPECT_TRUE(session.primitive_creation().active());
  EXPECT_EQ(session.primitive_creation().shape_count(), 1U);
  EXPECT_EQ(session.modeling().objects().size(), 1U);
  // Public Undo remains available so it can cancel the pending native tool,
  // even though the accepted live shape has not entered document history.
  EXPECT_TRUE(editor.history().can_undo());

  current_event.ray.direction = {};
  const ViewportToolResult failed =
      dispatch_frame({.hovered = true, .left_clicked = true});
  EXPECT_TRUE(failed.active_tool_changed);
  EXPECT_TRUE(failed.sync_active_tool_from_modeling);
  EXPECT_FALSE(session.primitive_creation().active());
  EXPECT_EQ(editor.tools().active_tool(), "select");
  EXPECT_TRUE(session.modeling().objects().empty());
  EXPECT_FALSE(editor.history().can_undo());
}

TEST(ViewportToolRuntimeTests,
     PersistentPrimitiveRollsBackOnQueryAndHoverPreviewFailures) {
  const ViewportTool *box_tool =
      find_viewport_tool(ViewportToolId::CreatePrimitive);
  ASSERT_NE(box_tool, nullptr);

  for (const bool fail_query : {true, false}) {
    SCOPED_TRACE(fail_query ? "query failure" : "hover failure");
    FailingBoxPreviewPorts ports;
    ports.fail_query = fail_query;
    ViewportToolState state{
        .active_tool = ViewportToolId::CreatePrimitive};
    ViewportCameraController camera;
    ViewportModalToolState modal_state;
    int cancel_count = 0;
    ViewportBoxToolContext box_context;
    box_context.make_event = [] { return ModelingBoxToolEvent{}; };
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
    EXPECT_TRUE(result.active_tool_changed);
    EXPECT_TRUE(result.sync_active_tool_from_modeling);
    EXPECT_EQ(cancel_count, 1);
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
    FailingBoxPreviewPorts ports;
    ViewportToolState state{
        .active_tool = ViewportToolId::CreatePrimitive};
    ViewportCameraController camera;
    ViewportModalToolState modal_state;
    bool session_active = true;
    ViewportBoxToolContext box_context;
    box_context.make_event = [] { return ModelingBoxToolEvent{}; };
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

TEST(ViewportToolGuardrailTests, RemovedLegacyViewportToolBase) {
  const std::filesystem::path root = source_root();
  const std::string tool_header =
      read_source_file(root / "src" / "editor" / "tools" / "viewport_tool.hpp");
  const std::string catalog_header = read_source_file(
      root / "src" / "editor" / "tools" / "viewport_tool_catalog.hpp");
  EXPECT_EQ(tool_header.find("ViewportToolBase"), std::string::npos);
  EXPECT_EQ(catalog_header.find("ViewportToolBase"), std::string::npos);
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

  EXPECT_EQ(modal_state.find("ModelingBoxPreview"), std::string::npos);
  EXPECT_EQ(box_tool_source.find("context.modal_state->box_preview"),
            std::string::npos);
  EXPECT_NE(box_tool_source.find("preview_port.box_preview("),
            std::string::npos);
  EXPECT_NE(router_source.find("tool_runtime_->box_preview(preview"),
            std::string::npos);
  EXPECT_EQ(router_header.find("ModelingSessionService"), std::string::npos);
  EXPECT_EQ(router_source.find("box_preview(const ModelingSessionService"),
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
      selection_interaction.find("frame.input_router.state().selection_mode == "
                                 "ViewportSelectionMode::Group"),
      std::string::npos);
  EXPECT_EQ(
      selection_interaction.find(
          "editor.selection().summary().mode == QuaderSelectionMode::Group"),
      std::string::npos);
  EXPECT_EQ(viewport_panel.find("selectionOverlaySuppressedForInteraction()"),
            std::string::npos);
  EXPECT_NE(
      viewport_panel.find("overlays_visible && selection_overlay_cache_.valid()"),
      std::string::npos);
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

TEST(ViewportToolGuardrailTests, HostViewportAdapterUsesViewportToolDescriptors) {
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

    const std::filesystem::path root = source_root();
    const std::string adapter = read_source_file(
        root / "src" / "viewport" / "host_viewport_contract_adapter.cpp");
    EXPECT_NE(adapter.find("find_viewport_tool_descriptor(tool)"), std::string::npos);
    EXPECT_NE(adapter.find("find_viewport_tool_descriptor(id)"), std::string::npos);
    EXPECT_NE(adapter.find("quader_host_contract::find_action_descriptor(id)"), std::string::npos);
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

  const std::string pivot_overlay =
      source_slice(viewport_panel, "void append_pivot_handle_overlay",
                   "std::uint32_t linear_array_axis_color");
  EXPECT_NE(viewport_panel.find("void append_pivot_ring("), std::string::npos);
  EXPECT_NE(pivot_overlay.find("append_pivot_ring("), std::string::npos);
  EXPECT_NE(viewport_panel.find("frame.lines.push_back({"),
            std::string::npos);
  EXPECT_EQ(pivot_overlay.find("frame.points.push_back"), std::string::npos);
  EXPECT_NE(viewport_panel.find("blocked_by_navigation"),
            std::string::npos);
  EXPECT_NE(viewport_panel.find("pivot->clear_quick_override()"),
            std::string::npos);
  EXPECT_NE(viewport_panel.find("!blocked_by_navigation && "
                                "input.quick_pivot_down"),
            std::string::npos);

  const std::string pivot_input =
      source_slice(viewport_panel,
                   "const PivotHandleState pivot_after_input",
                   "const ModelingSelectionSummary hover_selection");
  EXPECT_NE(pivot_input.find("pivot_blocks_gizmo ||"), std::string::npos);
  EXPECT_NE(pivot_input.find("pivot_consumed)"), std::string::npos);
  EXPECT_NE(
      pivot_input.find(
          "ViewportBeforeToolInteractionResult{.gizmo_consumed = true}"),
      std::string::npos);
}

} // namespace
} // namespace quader
