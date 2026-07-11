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
#include "editor/metadata/host_contract.hpp"
#include "scene/scene_document.hpp"
#include "editor/session/project_session.hpp"
#include "editor/tools/viewport_tool_catalog.hpp"
#include "editor/tools/viewport_tool_ports.hpp"
#include "editor/tools/viewport_tool_runtime.hpp"
#include "support/guardrail_file_scan.hpp"
#include "viewport/host_viewport_contract_adapter.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

namespace quader {
namespace {

using test::guardrail::read_source_file;
using test::guardrail::source_root;
using test::guardrail::source_slice;

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
  const Tool *box = find_viewport_tool("box");
  ASSERT_NE(box, nullptr);
  EXPECT_EQ(box->viewport_tool(), ViewportToolId::Box);
  EXPECT_EQ(box->modeling_tool(), ModelingViewportTool::Box);
  EXPECT_EQ(box->kind(), ToolKind::Modal);
  EXPECT_EQ(box->label(), "Box");
  EXPECT_EQ(box->shortcut(), "B");
  EXPECT_TRUE(box->owns_viewport());
  EXPECT_FALSE(box->blocks_viewport_focus_shortcuts());

  EXPECT_EQ(viewport_tool_transform_tool(ViewportToolId::Move),
            ModelingTransformTool::Move);
  EXPECT_EQ(modeling_viewport_tool_for(ViewportToolId::Extend),
            ModelingViewportTool::Extend);
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
  EXPECT_EQ(viewport_tool_for_modeling(ModelingViewportTool::Extend),
            ViewportToolId::Extend);
  EXPECT_EQ(viewport_tool_for_modeling(ModelingViewportTool::Extrude),
            ViewportToolId::Extrude);
  EXPECT_EQ(viewport_tool_for_modeling(ModelingViewportTool::Mirror),
            ViewportToolId::Mirror);
  EXPECT_TRUE(viewport_tool_blocks_focus_shortcuts(ViewportToolId::Cut));
  EXPECT_TRUE(viewport_tool_owns_viewport(ViewportToolId::InsertEdgeLoop));
  EXPECT_TRUE(viewport_tool_supports_transform_gizmo(ViewportToolId::Move));
  EXPECT_TRUE(viewport_tool_supports_transform_gizmo(ViewportToolId::Rotate));
  EXPECT_TRUE(viewport_tool_supports_transform_gizmo(ViewportToolId::Scale));
  EXPECT_FALSE(viewport_tool_supports_transform_gizmo(ViewportToolId::Extend));
  EXPECT_FALSE(viewport_tool_supports_transform_gizmo(ViewportToolId::Extrude));
  EXPECT_FALSE(viewport_tool_supports_transform_gizmo(ViewportToolId::Mirror));
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

TEST(ViewportToolRuntimeTests, BoxToolCommitUsesCommandOwnedTransaction) {
    ProjectSession session;
    QuaderEditorApi editor(
        QuaderApiSessionInternalAccess::from_project_session(session));
    ViewportToolState state;
    ViewportCameraController camera;
    ViewportModalToolState modal_state;

    const QuaderApiResult<QuaderAssetRef> material =
        editor.assets().add_material(QuaderMaterialArgs{
            .name = "Viewport Box Material",
            .base_color = {0.2f, 0.4f, 0.8f},
        });
    ASSERT_TRUE(material.ok) << material.message;

    const ViewportTool *box_tool = find_viewport_tool(ViewportToolId::Box);
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
                                        .tool = ViewportToolId::Box,
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
      read_source_file(root / "src" / "ui" / "viewport_panel.cpp");
  const std::string transform_gizmo = read_source_file(
      root / "src" / "editor" / "viewport" / "transform_gizmo_interaction.cpp");

  EXPECT_NE(viewport_panel.find("ViewportPointToolContext point_tool_context"),
            std::string::npos);
  EXPECT_NE(viewport_panel.find("point_tool_context.make_event_at"),
            std::string::npos);
  EXPECT_NE(viewport_panel.find("point_tool_context.hit_drawn_plane_captured_point"),
            std::string::npos);
  EXPECT_NE(viewport_panel.find("point_tool_context.hit_drawn_plane_line"),
            std::string::npos);
  EXPECT_NE(viewport_panel.find(".point = &point_tool_context"),
            std::string::npos);
  EXPECT_NE(viewport_panel.find("make_modeling_tool_event_at"), std::string::npos);
  EXPECT_NE(viewport_panel.find("snap_enabled ="), std::string::npos);
  EXPECT_NE(
      viewport_panel.find(
          "viewport_grid_snap_active(settings, input.modifiers.primary)"),
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
                "const bool snap_active = viewport_grid_snap_active("),
            std::string::npos);
  EXPECT_NE(transform_gizmo.find(
                "frame.settings, frame.input.modifiers.primary)"),
            std::string::npos);
  EXPECT_NE(transform_gizmo.find(".vertex_snap_radius ="), std::string::npos);
  EXPECT_NE(transform_gizmo.find(
                "snap_active ? viewport_vertex_snap_radius(frame.settings) : 0.0"),
            std::string::npos);
  EXPECT_NE(transform_gizmo.find(".grid_snap_enabled = snap_active"),
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
  EXPECT_EQ(ui_shell.find("active_tool == ViewportToolId::Box"),
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
      read_source_file(root / "src" / "ui" / "viewport_panel.cpp");

  const std::string pivot_overlay =
      source_slice(viewport_panel, "void append_pivot_handle_overlay",
                   "bool linear_array_handle_hit");
  EXPECT_NE(viewport_panel.find("void append_pivot_ring("), std::string::npos);
  EXPECT_NE(pivot_overlay.find("append_pivot_ring("), std::string::npos);
  EXPECT_NE(viewport_panel.find("frame.lines.push_back(ViewportOverlayLine{"),
            std::string::npos);
  EXPECT_EQ(pivot_overlay.find("frame.points.push_back"), std::string::npos);
  EXPECT_NE(viewport_panel.find("quick_pivot_blocked_by_navigation"),
            std::string::npos);
  EXPECT_NE(viewport_panel.find("pivot_service->clear_quick_override()"),
            std::string::npos);
  EXPECT_NE(viewport_panel.find("!quick_pivot_blocked_by_navigation && "
                                "input.quick_pivot_down"),
            std::string::npos);

  const std::string pivot_input =
      source_slice(viewport_panel,
                   "const PivotHandleState pivot_handle_after_input",
                   "const ModelingToolStatus release_tool_status");
  EXPECT_NE(pivot_input.find("pivot_blocks_gizmo || pivot_consumed"),
            std::string::npos);
  EXPECT_NE(
      pivot_input.find(
          "ViewportBeforeToolInteractionResult{.gizmo_consumed = true}"),
      std::string::npos);
}

} // namespace
} // namespace quader
