////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_editor_api.hpp"
#include "editor/input/quader_input_binding.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/tools/viewport_tool_catalog.hpp"
#include "support/command_behavior_contract.hpp"
#include "support/command_behavior_fixture.hpp"
#include "support/command_observable_state.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>

namespace quader {
namespace {

QuaderInputChord input_chord(std::string_view text) {
  const auto parsed = quader_input_parse_shortcut(text);
  EXPECT_TRUE(parsed.has_value());
  return parsed && !parsed->empty() ? parsed->front() : QuaderInputChord{};
}

TEST(SelectionToolSettingsTests, LassoIdentityShortcutAndUserOverrideRemainCoherent) {
  auto standalone = create_standalone_editor();
  auto &editor = standalone.editor();
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  constexpr auto kCommand = quader_command::kToolsActivateLassoSelect;
  const auto descriptor = editor.commands().descriptor(kCommand);
  ASSERT_TRUE(descriptor.has_value());
  EXPECT_EQ(descriptor->label, "Lasso Select");
  EXPECT_EQ(descriptor->shortcut, "Shift+Q");
  EXPECT_TRUE(descriptor->enabled);
  EXPECT_EQ(viewport_tool_for_modeling(ModelingViewportTool::LassoSelect),
            ViewportToolId::LassoSelect);
  EXPECT_EQ(viewport_tool_interaction_tool(ViewportToolId::LassoSelect),
            quader_editor_core::ViewportInteractionTool::LassoSelect);
  EXPECT_FALSE(viewport_tool_supports_transform_gizmo(ViewportToolId::LassoSelect));

  const auto catalog = editor.commands().keybinding_catalog();
  const auto row = std::ranges::find_if(catalog, [&](const auto &entry) {
    return std::ranges::find(entry.target_ids, kCommand.string()) !=
           entry.target_ids.end();
  });
  ASSERT_NE(row, catalog.end());
  EXPECT_EQ(row->default_shortcut, "Shift+Q");
  EXPECT_EQ(row->localization_key,
            "keybindings.target.tools.activate.lasso_select.label");

  ViewportToolState state;
  const auto chord = input_chord("Shift+Q");
  const auto dispatch = editor.commands().dispatch_viewport_input_binding(
      chord, state, true, false);
  ASSERT_EQ(dispatch.kind, QuaderViewportKeybindingDispatch::Kind::ViewportShortcut);
  EXPECT_EQ(dispatch.shortcut.tool, ViewportToolId::LassoSelect);
  EXPECT_EQ(editor.commands().dispatch_viewport_input_binding(
                chord, state, true, false, {.navigation_active = true}).kind,
            QuaderViewportKeybindingDispatch::Kind::NoMatch);
  EXPECT_EQ(editor.commands().dispatch_viewport_input_binding(
                chord, state, true, true).kind,
            QuaderViewportKeybindingDispatch::Kind::NoMatch);

  ASSERT_TRUE(editor.commands().execute(kCommand).ok());
  EXPECT_EQ(editor.tools().active_tool(), "lasso_select");
  EXPECT_EQ(editor.query().tool_status().active_tool, QuaderViewportTool::LassoSelect);
  EXPECT_FALSE(editor.history().can_undo());

  ASSERT_TRUE(editor.commands().set_shortcut(kCommand, "Ctrl+Alt+F22").ok());
  EXPECT_EQ(editor.commands().shortcut_for(kCommand), "Ctrl+Alt+F22");
  EXPECT_TRUE(editor.commands().shortcut_for(
      quader_command::kToolsActivateSelect).empty());
  ASSERT_TRUE(editor.commands().remove_shortcut(kCommand).ok());
  EXPECT_TRUE(editor.commands().shortcut_for(kCommand).empty());
  ASSERT_TRUE(editor.commands().reset_shortcut(kCommand).ok());
  EXPECT_EQ(editor.commands().shortcut_for(kCommand), "Shift+Q");
}

TEST(SelectionToolSettingsTests, MoveAndLassoCheckboxesRetainValuesAndResetIndependently) {
  auto standalone = create_standalone_editor();
  auto &editor = standalone.editor();
  auto &tools = editor.tools();
  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Vertex).ok());
  ASSERT_TRUE(tools.activate(ViewportToolId::Move).ok());
  auto target = tools.active_settings();
  ASSERT_TRUE(target.active);
  ASSERT_EQ(target.controls.size(), 1u);
  EXPECT_EQ(target.tool_id, "move");
  EXPECT_EQ(target.controls[0].id, "auto_merge_vertices");
  EXPECT_EQ(target.controls[0].kind, QuaderToolSettingControlKind::Toggle);
  EXPECT_TRUE(target.controls[0].bool_value);
  EXPECT_FALSE(target.apply_visible);
  const auto move_revision = tools.active_settings_revision();
  EXPECT_EQ(tools.active_settings_revision(), move_revision);
  ASSERT_TRUE(tools.set_setting("move", "auto_merge_vertices", false).ok());
  const auto changed_revision = tools.active_settings_revision();
  EXPECT_GT(changed_revision, move_revision);
  EXPECT_FALSE(tools.set_setting("move", "auto_merge_vertices", false).changed());
  EXPECT_EQ(tools.active_settings_revision(), changed_revision);

  ASSERT_TRUE(tools.activate(ViewportToolId::LassoSelect).ok());
  EXPECT_GT(tools.active_settings_revision(), changed_revision);
  target = tools.active_settings();
  ASSERT_EQ(target.controls.size(), 1u);
  EXPECT_EQ(target.tool_id, "lasso_select");
  EXPECT_EQ(target.controls[0].id, "select_partial_intersection");
  EXPECT_FALSE(target.controls[0].bool_value);
  ASSERT_TRUE(tools.set_setting("lasso_select", "select_partial_intersection", true).ok());
  ASSERT_TRUE(tools.activate(ViewportToolId::Move).ok());
  EXPECT_FALSE(tools.active_settings().controls[0].bool_value);
  ASSERT_TRUE(tools.reset_active_settings().ok());
  EXPECT_TRUE(tools.active_settings().controls[0].bool_value);
  ASSERT_TRUE(tools.activate(ViewportToolId::LassoSelect).ok());
  EXPECT_TRUE(tools.active_settings().controls[0].bool_value);
  ASSERT_TRUE(tools.reset_active_settings().ok());
  EXPECT_FALSE(tools.active_settings().controls[0].bool_value);
  EXPECT_FALSE(tools.reset_active_settings().changed());
  EXPECT_FALSE(editor.history().can_undo());
}

TEST(SelectionToolSettingsTests,
     AutoMergeControlOnlyAppearsForVertexMoveAndPreservesItsStoredValue) {
  auto standalone = create_standalone_editor();
  auto &editor = standalone.editor();
  auto &tools = editor.tools();
  ASSERT_TRUE(tools.activate(ViewportToolId::Move).ok());
  EXPECT_TRUE(tools.active_settings().controls.empty());
  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Vertex).ok());
  ASSERT_TRUE(tools.set_setting("move", "auto_merge_vertices", false).ok());
  const auto origin = tools.active_settings_presentation_origin();
  constexpr std::array kPanes{ViewportPaneId::Perspective, ViewportPaneId::Top,
                              ViewportPaneId::Front, ViewportPaneId::Side};

  for (const auto mode : {QuaderSelectionMode::Edge, QuaderSelectionMode::Face,
                          QuaderSelectionMode::Object, QuaderSelectionMode::Group}) {
    SCOPED_TRACE(static_cast<int>(mode));
    const auto visible_revision = tools.active_settings_revision();
    ASSERT_TRUE(editor.selection().set_mode(mode).ok());
    EXPECT_GT(tools.active_settings_revision(), visible_revision);
    EXPECT_TRUE(tools.active_settings().controls.empty());
    for (const auto pane : kPanes) {
      EXPECT_TRUE(tools.settings_for_pane(pane).controls.empty());
      const auto hidden_revision = tools.settings_revision(pane);
      for (int frame = 0; frame < 8; ++frame)
        EXPECT_EQ(tools.settings_revision(pane), hidden_revision);
    }
    const auto hidden_revision = tools.active_settings_revision();
    for (int frame = 0; frame < 8; ++frame)
      EXPECT_EQ(tools.active_settings_revision(), hidden_revision);
    ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Vertex).ok());
    EXPECT_GT(tools.active_settings_revision(), hidden_revision);
    for (const auto pane : kPanes) {
      const auto target = tools.settings_for_pane(pane);
      ASSERT_EQ(target.controls.size(), 1u);
      EXPECT_EQ(target.controls.front().id, "auto_merge_vertices");
      EXPECT_FALSE(target.controls.front().bool_value);
    }
    EXPECT_EQ(tools.active_settings_presentation_origin(), origin);
  }

  for (const auto tool : {ViewportToolId::Rotate, ViewportToolId::Scale,
                          ViewportToolId::Select}) {
    SCOPED_TRACE(static_cast<int>(tool));
    ASSERT_TRUE(tools.activate(tool).ok());
    EXPECT_TRUE(tools.active_settings().controls.empty());
    for (const auto pane : kPanes)
      EXPECT_TRUE(tools.settings_for_pane(pane).controls.empty());
  }
  ASSERT_TRUE(tools.activate(ViewportToolId::Move).ok());
  const auto restored = tools.active_settings();
  ASSERT_EQ(restored.controls.size(), 1u);
  EXPECT_FALSE(restored.controls.front().bool_value);
  EXPECT_FALSE(editor.history().can_undo());
}

TEST(SelectionToolSettingsTests, InvalidBooleanSettingsPreserveValuesAndRevision) {
  auto standalone = create_standalone_editor();
  auto &tools = standalone.editor().tools();
  ASSERT_TRUE(standalone.editor().selection().set_mode(QuaderSelectionMode::Vertex).ok());
  ASSERT_TRUE(tools.activate(ViewportToolId::Move).ok());
  const auto revision = tools.active_settings_revision();
  for (const double value : {-1.0, 0.5, 2.0,
                            std::numeric_limits<double>::quiet_NaN()}) {
    EXPECT_FALSE(tools.set_setting("move", "auto_merge_vertices", value).ok());
  }
  EXPECT_FALSE(tools.set_setting("lasso_select", "auto_merge_vertices", false).ok());
  EXPECT_TRUE(tools.active_settings().controls[0].bool_value);
  EXPECT_EQ(tools.active_settings_revision(), revision);
}

TEST(SelectionToolSettingsTests, SharedToolSettingsRemainPinnedAndEditableAcrossFocus) {
  auto standalone = create_standalone_editor();
  auto &editor = standalone.editor();
  auto &tools = editor.tools();
  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Vertex).ok());
  ASSERT_TRUE(tools.activate(ViewportToolId::Move).ok());
  ASSERT_TRUE(editor.viewports().focus_pane(ViewportPaneId::Top).ok());
  ASSERT_TRUE(tools.activate(ViewportToolId::Rotate).ok());
  EXPECT_EQ(editor.viewports().selected_tool(ViewportPaneId::Perspective), ViewportToolId::Rotate);
  EXPECT_EQ(editor.viewports().selected_tool(ViewportPaneId::Top), ViewportToolId::Rotate);
  ASSERT_TRUE(tools.activate(ViewportToolId::Move).ok());
  const auto origin = tools.active_settings_presentation_origin();
  ASSERT_EQ(origin.pane, ViewportPaneId::Top);
  const auto move_revision = tools.settings_revision(ViewportPaneId::Perspective);
  const auto tool_revision = tools.active_tool_revision();
  const auto history = editor.history().can_undo();

  ASSERT_TRUE(editor.viewports().focus_pane(ViewportPaneId::Perspective).ok());
  const auto move = tools.settings_for_pane(ViewportPaneId::Perspective);
  ASSERT_EQ(move.tool_id, "move");
  ASSERT_EQ(move.controls.size(), 1U);
  EXPECT_EQ(move.controls.front().id, "auto_merge_vertices");
  EXPECT_FALSE(move.apply_visible);
  EXPECT_EQ(tools.settings_for_pane(ViewportPaneId::Top).tool_id, "move");
  EXPECT_EQ(tools.active_tool(), "move");
  EXPECT_EQ(tools.active_tool_revision(), tool_revision);
  EXPECT_EQ(tools.active_settings_presentation_origin().serial, origin.serial);
  EXPECT_EQ(tools.active_settings_presentation_origin().pane, origin.pane);
  EXPECT_EQ(editor.history().can_undo(), history);
  for (int frame = 0; frame < 10; ++frame) {
    EXPECT_EQ(tools.settings_revision(ViewportPaneId::Perspective), move_revision);
  }

  ASSERT_TRUE(tools.set_setting(ViewportPaneId::Top, "move",
                                "auto_merge_vertices", false).ok());
  EXPECT_EQ(tools.active_tool(), "move");
  EXPECT_TRUE(tools.pane_owns_executing_tool(ViewportPaneId::Perspective));
  EXPECT_EQ(editor.viewports().selected_tool(ViewportPaneId::Top), ViewportToolId::Move);
  EXPECT_EQ(tools.active_settings_presentation_origin(), origin);
  EXPECT_FALSE(tools.settings_for_pane(ViewportPaneId::Perspective).controls.front().bool_value);
  EXPECT_GT(tools.settings_revision(ViewportPaneId::Perspective), move_revision);
  EXPECT_FALSE(editor.history().can_undo());
}

TEST(SelectionToolSettingsTests, PaneAcquisitionRejectsStaleTargetsAndPreservesSharedOrigin) {
  auto standalone = create_standalone_editor();
  auto &editor = standalone.editor();
  auto &tools = editor.tools();
  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Vertex).ok());
  ASSERT_TRUE(tools.activate(ViewportToolId::Move).ok());
  ASSERT_TRUE(editor.viewports().focus_pane(ViewportPaneId::Top).ok());
  const auto origin = tools.active_settings_presentation_origin();
  EXPECT_FALSE(tools.activate(ViewportToolId::Move).changed());
  EXPECT_FALSE(tools.acquire_pane_tool(static_cast<ViewportPaneId>(99)).ok());
  EXPECT_FALSE(tools.set_setting(ViewportPaneId::Top, "lasso_select",
                                 "select_partial_intersection", true).ok());
  EXPECT_FALSE(tools.set_setting(ViewportPaneId::Side, "move",
                                 "auto_merge_vertices", false).ok());
  EXPECT_EQ(tools.active_settings_presentation_origin().serial, origin.serial);
  EXPECT_TRUE(tools.settings_for_pane(ViewportPaneId::Perspective).controls.front().bool_value);
  EXPECT_FALSE(tools.acquire_pane_tool(ViewportPaneId::Perspective).ok());
  const auto acquired = tools.acquire_pane_tool(ViewportPaneId::Top);
  ASSERT_TRUE(acquired.ok());
  EXPECT_FALSE(acquired.changed());
  constexpr std::array kPanes{ViewportPaneId::Perspective, ViewportPaneId::Top,
                              ViewportPaneId::Front, ViewportPaneId::Side};
  for (const auto pane : kPanes) {
    EXPECT_TRUE(tools.pane_owns_executing_tool(pane));
  }
  EXPECT_EQ(tools.active_settings_presentation_origin(), origin);
}

TEST(SelectionToolSettingsTests, OtherPaneKeepsPendingExtrusionUntilExplicitToolChange) {
  auto standalone = create_standalone_editor();
  auto &editor = standalone.editor();
  auto &tools = editor.tools();
  const auto created = editor.modeling().create_box(
      {.min = {-1.0F, -1.0F, -1.0F}, .max = {1.0F, 1.0F, 1.0F}}, "Pane extrusion");
  ASSERT_TRUE(created.ok());
  const auto original = editor.query().compiled_object_mesh(created.value());
  ASSERT_TRUE(original.has_value());
  ASSERT_TRUE(editor.viewports().focus_pane(ViewportPaneId::Top).ok());
  ASSERT_TRUE(tools.activate(ViewportToolId::Move).ok());
  ASSERT_TRUE(editor.viewports().focus_pane(ViewportPaneId::Perspective).ok());
  ASSERT_TRUE(tools.activate(ViewportToolId::Extrude).ok());
  ASSERT_TRUE(editor.selection().select_face(created.value(), 1).ok());
  ASSERT_TRUE(tools.set_setting("extrude", "extrude.amount", 1.0).ok());
  ASSERT_TRUE(tools.active_settings().preview_active);
  const auto origin = tools.active_settings_presentation_origin();
  const auto revision = tools.active_settings_revision();
  const auto before = editor.query().compiled_object_mesh(created.value());
  ASSERT_TRUE(before.has_value());
  ASSERT_TRUE(editor.viewports().focus_pane(ViewportPaneId::Top).ok());
  const auto passive = tools.settings_for_pane(ViewportPaneId::Top);
  EXPECT_EQ(passive.tool_id, "extrude");
  EXPECT_FALSE(passive.preview_active);
  EXPECT_FALSE(passive.apply_visible);
  EXPECT_FALSE(passive.can_apply);
  EXPECT_FALSE(passive.can_cancel);
  EXPECT_TRUE(tools.settings_for_pane(ViewportPaneId::Perspective).preview_active);
  EXPECT_EQ(tools.active_tool(), "extrude");
  EXPECT_EQ(tools.active_settings_revision(), revision);
  EXPECT_EQ(tools.active_settings_presentation_origin().serial, origin.serial);
  const auto acquired = tools.acquire_pane_tool(ViewportPaneId::Top);
  ASSERT_TRUE(acquired.ok());
  EXPECT_FALSE(acquired.changed());
  EXPECT_EQ(tools.active_tool(), "extrude");
  EXPECT_TRUE(tools.active_settings().preview_active);
  EXPECT_EQ(tools.active_settings_revision(), revision);
  EXPECT_EQ(tools.active_settings_presentation_origin(), origin);
  ASSERT_TRUE(tools.activate(ViewportToolId::Move).ok());
  EXPECT_TRUE(tools.set_setting(ViewportPaneId::Top, "move", "auto_merge_vertices", false).ok());
  EXPECT_EQ(tools.active_tool(), "move");
  EXPECT_FALSE(tools.active_settings().preview_active);
  EXPECT_GT(tools.active_settings_revision(), revision);
  EXPECT_GT(tools.active_settings_presentation_origin().serial, origin.serial);
  EXPECT_EQ(editor.history().snapshot().undo_label, "Extrude Faces");
  const auto after = editor.query().compiled_object_mesh(created.value());
  ASSERT_TRUE(after.has_value());
  EXPECT_EQ(after->indices, before->indices);
  ASSERT_EQ(after->vertices.size(), before->vertices.size());
  for (std::size_t index = 0; index < before->vertices.size(); ++index) {
    EXPECT_FLOAT_EQ(after->vertices[index].position.x, before->vertices[index].position.x);
    EXPECT_FLOAT_EQ(after->vertices[index].position.y, before->vertices[index].position.y);
    EXPECT_FLOAT_EQ(after->vertices[index].position.z, before->vertices[index].position.z);
  }
  ASSERT_TRUE(editor.history().undo().ok());
  const auto undone = editor.query().compiled_object_mesh(created.value());
  ASSERT_TRUE(undone.has_value());
  EXPECT_EQ(undone->indices, original->indices);
  EXPECT_EQ(undone->vertices.size(), original->vertices.size());
  ASSERT_TRUE(editor.history().redo().ok());
  const auto redone = editor.query().compiled_object_mesh(created.value());
  ASSERT_TRUE(redone.has_value());
  EXPECT_EQ(redone->indices, before->indices);
}

TEST(SelectionToolSettingsTests, CompletingOrCancelingUpdatesEveryPaneChoice) {
  auto standalone = create_standalone_editor();
  auto &editor = standalone.editor();
  auto &tools = editor.tools();
  ASSERT_TRUE(editor.viewports().focus_pane(ViewportPaneId::Top).ok());
  ASSERT_TRUE(tools.activate(ViewportToolId::Rotate).ok());
  ASSERT_TRUE(editor.viewports().focus_pane(ViewportPaneId::Perspective).ok());
  ASSERT_TRUE(tools.begin_primitive_creation(QuaderPrimitiveCreationShape::Box).ok());
  EXPECT_EQ(editor.viewports().selected_tool(ViewportPaneId::Perspective), ViewportToolId::CreatePrimitive);
  ASSERT_TRUE(editor.viewports().focus_pane(ViewportPaneId::Top).ok());
  ASSERT_TRUE(tools.cancel_modal_tool().ok());
  const auto *finished_tool = find_viewport_tool(tools.active_tool());
  ASSERT_NE(finished_tool, nullptr);
  EXPECT_NE(finished_tool->viewport_tool(), ViewportToolId::CreatePrimitive);
  constexpr std::array kPanes{ViewportPaneId::Perspective, ViewportPaneId::Top,
                              ViewportPaneId::Front, ViewportPaneId::Side};
  for (const auto pane : kPanes) {
    EXPECT_EQ(editor.viewports().selected_tool(pane), finished_tool->viewport_tool());
    EXPECT_TRUE(tools.pane_owns_executing_tool(pane));
  }
}

TEST(SelectionToolSettingsTests, ActivationSharesToolWithoutReplacingOtherPaneSelectionMemory) {
  auto standalone = create_standalone_editor();
  auto &editor = standalone.editor();
  auto &viewports = editor.viewports();
  ASSERT_TRUE(viewports.set_layout(ViewportLayoutId::FourQuad).ok());
  ASSERT_TRUE(viewports.record_selected_tool(ViewportPaneId::Top,
                                             ViewportToolId::LassoSelect).ok());
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::Move).ok());
  ASSERT_TRUE(viewports.focus_pane(ViewportPaneId::Front).ok());
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::LassoSelect).ok());
  constexpr std::array kPanes{ViewportPaneId::Perspective, ViewportPaneId::Top,
                              ViewportPaneId::Front, ViewportPaneId::Side};
  for (const auto pane : kPanes) {
    EXPECT_EQ(viewports.selected_tool(pane), ViewportToolId::LassoSelect);
  }
  EXPECT_EQ(viewports.preferred_selection_tool(ViewportPaneId::Perspective),
            ViewportToolId::Select);
  EXPECT_EQ(viewports.preferred_selection_tool(ViewportPaneId::Top),
            ViewportToolId::LassoSelect);
  EXPECT_EQ(viewports.preferred_selection_tool(ViewportPaneId::Front),
            ViewportToolId::LassoSelect);
  EXPECT_EQ(viewports.preferred_selection_tool(ViewportPaneId::Side),
            ViewportToolId::Select);
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::Select).ok());
  EXPECT_EQ(viewports.preferred_selection_tool(ViewportPaneId::Front),
            ViewportToolId::Select);
  EXPECT_EQ(viewports.preferred_selection_tool(ViewportPaneId::Top),
            ViewportToolId::LassoSelect);
}

TEST(SelectionToolSettingsTests, LassoSettingsTakePriorityOverPassiveSelectedCurve) {
  auto standalone = create_standalone_editor();
  auto &editor = standalone.editor();
  const auto curve = editor.modeling().create_curve({
      .points = {{.position = {0.0f, 0.0f, 0.0f}},
                 {.position = {3.0f, 0.0f, 0.0f}}},
      .name = "Lasso Settings Curve",
  });
  ASSERT_TRUE(curve.ok());
  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Object).ok());
  ASSERT_TRUE(editor.selection().select_object(curve.value()).ok());
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::Curve).ok());
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  EXPECT_EQ(editor.tools().active_settings().tool_id, "curve");
  const auto curve_revision = editor.tools().active_settings_revision();
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::LassoSelect).ok());
  EXPECT_GT(editor.tools().active_settings_revision(), curve_revision);
  auto target = editor.tools().active_settings();
  ASSERT_EQ(target.tool_id, "lasso_select");
  ASSERT_EQ(target.controls.size(), 1u);
  EXPECT_EQ(target.controls[0].id, "select_partial_intersection");
  ASSERT_TRUE(editor.tools().set_setting({}, "select_partial_intersection", true).ok());
  EXPECT_TRUE(editor.tools().active_settings().controls[0].bool_value);
  ASSERT_TRUE(editor.tools().reset_active_settings().ok());
  EXPECT_FALSE(editor.tools().active_settings().controls[0].bool_value);
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::Move).ok());
  target = editor.tools().active_settings();
  ASSERT_EQ(target.tool_id, "move");
  EXPECT_TRUE(target.controls.empty());
  EXPECT_FALSE(target.preview_active);
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::Select).ok());
  EXPECT_EQ(editor.tools().active_tool(), "select");
  EXPECT_FALSE(editor.tools().active_native_tool_lifecycle(ViewportToolId::Curve));
  EXPECT_EQ(editor.tools().active_settings().tool_id, "curve");
}

TEST(SelectionToolSettingsTests, NewSceneSessionUsesDefaultCheckboxValues) {
  auto first = create_standalone_editor();
  auto &first_tools = first.editor().tools();
  ASSERT_TRUE(first_tools.activate(ViewportToolId::Move).ok());
  ASSERT_TRUE(first_tools.set_setting("move", "auto_merge_vertices", false).ok());
  ASSERT_TRUE(first_tools.activate(ViewportToolId::LassoSelect).ok());
  ASSERT_TRUE(first_tools.set_setting("lasso_select", "select_partial_intersection", true).ok());
  auto next_session = create_standalone_editor();
  auto &next_tools = next_session.editor().tools();
  ASSERT_TRUE(next_session.editor().selection().set_mode(QuaderSelectionMode::Vertex).ok());
  ASSERT_TRUE(next_tools.activate(ViewportToolId::Move).ok());
  EXPECT_TRUE(next_tools.active_settings().controls[0].bool_value);
  ASSERT_TRUE(next_tools.activate(ViewportToolId::LassoSelect).ok());
  EXPECT_FALSE(next_tools.active_settings().controls[0].bool_value);
  EXPECT_TRUE(first_tools.active_settings().controls[0].bool_value);
}

TEST(SelectionToolSettingsTests,
     EveryModelingPreviewSettingsTargetAppliesOrCancelsThroughCanonicalKeys) {
  struct PreviewCase {
    QuaderCommandId command;
    std::string_view tool;
    std::string_view setting;
    double value;
  };
  constexpr std::array kCases{
      PreviewCase{quader_command::kModelingInterpolatedBridgeEdges, "bridge",
                  "operation.bridge.steps", 3.0},
      PreviewCase{quader_command::kModelingInterpolatedBridgeFaces, "bridge",
                  "operation.bridge.steps", 3.0},
      PreviewCase{quader_command::kModelingBevelEdges, "edge_bevel",
                  "operation.edge_bevel.width", 0.2},
      PreviewCase{quader_command::kModelingThickenFaces, "thicken",
                  "operation.thicken.thickness", 0.25},
      PreviewCase{quader_command::kModelingSliceQuad, "slice_quad",
                  "operation.slice_quad.x_slices", 3.0},
      PreviewCase{quader_command::kModelingMergeByDistance, "merge_by_distance",
                  "operation.merge_by_distance.tolerance", 0.2},
      PreviewCase{quader_command::kModelingHalfCut, "half_cut",
                  "operation.half_cut.axis", 2.0},
  };
  using Domain = test::CommandObservableDomain;
  for (const auto &preview_case : kCases) {
    SCOPED_TRACE(preview_case.command.string());
    const auto *contract =
        test::find_command_behavior_contract(preview_case.command.string());
    ASSERT_NE(contract, nullptr);
    for (const bool apply : {false, true}) {
      SCOPED_TRACE(apply ? "Space applies" : "Escape cancels");
      auto fixture = test::CommandBehaviorFixture::create(*contract);
      auto &editor = fixture->editor();
      const auto undo_before = fixture->native_scene().history().undo_count();
      const auto redo_before = fixture->native_scene().history().redo_count();
      const auto before = test::capture_command_observable_state(editor);
      const auto started = editor.commands().execute(preview_case.command);
      ASSERT_TRUE(started.ok()) << started.error_message();
      ASSERT_TRUE(editor.tools().active_settings().preview_active);
      const auto initial_preview = test::capture_command_observable_state(editor);
      const auto updated = editor.tools().set_setting(
          preview_case.tool, preview_case.setting, preview_case.value);
      ASSERT_TRUE(updated.ok()) << updated.error_message();
      const auto settings = editor.tools().active_settings();
      ASSERT_EQ(settings.tool_id, preview_case.tool);
      ASSERT_TRUE(settings.can_apply);
      ASSERT_TRUE(settings.can_cancel);
      const auto preview = test::capture_command_observable_state(editor);
      ASSERT_NE(preview.domain(Domain::Geometry), before.domain(Domain::Geometry));
      if (preview_case.tool == "bridge" || preview_case.tool == "edge_bevel") {
        EXPECT_NE(preview.domain(Domain::Geometry),
                  initial_preview.domain(Domain::Geometry));
      }
      EXPECT_EQ(fixture->native_scene().history().undo_count(), undo_before);

      ASSERT_EQ(editor.tools().active_tool(), "select");
      const ViewportToolState state{.active_tool = ViewportToolId::Select};
      ASSERT_FALSE(editor.tools().active_native_tool_lifecycle(state.active_tool));
      const auto dispatch =
          editor.commands().dispatch_native_tool_settings_input_binding(
              input_chord(apply ? "Space" : "Escape"), state);
      ASSERT_EQ(dispatch.kind, QuaderNativeToolSettingsDispatch::Kind::Intent);
      EXPECT_EQ(dispatch.intent, apply ? QuaderNativeToolSettingsIntent::Apply
                                       : QuaderNativeToolSettingsIntent::Cancel);
      const auto completed =
          editor.commands().execute_native_tool_settings_intent(dispatch.intent);
      ASSERT_TRUE(completed.ok()) << completed.error_message();
      EXPECT_TRUE(completed.changed());
      EXPECT_FALSE(editor.tools().active_settings().preview_active);
      const auto after = test::capture_command_observable_state(editor);
      if (!apply) {
        const auto restored = test::diff_command_observable_state(
            before, after, Domain::Geometry | Domain::Selection);
        EXPECT_TRUE(restored.empty()) << restored.summary;
        EXPECT_EQ(fixture->native_scene().history().undo_count(), undo_before);
        EXPECT_EQ(fixture->native_scene().history().redo_count(), redo_before);
        continue;
      }

      EXPECT_EQ(after.domain(Domain::Geometry), preview.domain(Domain::Geometry));
      EXPECT_EQ(fixture->native_scene().history().undo_count(), undo_before + 1U);
      ASSERT_TRUE(editor.history().undo().ok());
      const auto undone = test::capture_command_observable_state(editor);
      const auto restored = test::diff_command_observable_state(
          before, undone, Domain::Geometry | Domain::Selection);
      EXPECT_TRUE(restored.empty()) << restored.summary;
      EXPECT_EQ(fixture->native_scene().history().undo_count(), undo_before);
      ASSERT_TRUE(editor.history().redo().ok());
      const auto redone = test::capture_command_observable_state(editor);
      const auto reapplied = test::diff_command_observable_state(
          after, redone, Domain::Geometry | Domain::Selection);
      EXPECT_TRUE(reapplied.empty()) << reapplied.summary;
    }
  }
}

TEST(SelectionToolSettingsTests,
     BevelNumericSettingsEachUpdateTheAcceptedPreviewGeometry) {
  const auto *contract = test::find_command_behavior_contract(
      quader_command::kModelingBevelEdges.string());
  ASSERT_NE(contract, nullptr);
  auto fixture = test::CommandBehaviorFixture::create(*contract);
  auto &editor = fixture->editor();
  const auto started =
      editor.commands().execute(quader_command::kModelingBevelEdges);
  ASSERT_TRUE(started.ok()) << started.error_message();
  ASSERT_TRUE(editor.tools().active_settings().preview_active);
  using Domain = test::CommandObservableDomain;
  auto previous = test::capture_command_observable_state(editor);
  struct SettingChange {
    std::string_view id;
    double value;
  };
  constexpr std::array kChanges{
      SettingChange{"operation.edge_bevel.width", 0.15},
      SettingChange{"operation.edge_bevel.width", 0.3},
      SettingChange{"operation.edge_bevel.segments", 3.0},
      SettingChange{"operation.edge_bevel.profile", 0.2},
      SettingChange{"operation.edge_bevel.profile", 0.8},
      SettingChange{"operation.edge_bevel.width", 0.2},
  };
  for (const auto &change : kChanges) {
    SCOPED_TRACE(change.id);
    SCOPED_TRACE(change.value);
    const auto updated =
        editor.tools().set_setting("edge_bevel", change.id, change.value);
    ASSERT_TRUE(updated.ok()) << updated.error_message();
    EXPECT_TRUE(updated.changed());
    const auto current = test::capture_command_observable_state(editor);
    EXPECT_NE(current.domain(Domain::Geometry), previous.domain(Domain::Geometry));
    const auto unchanged =
        editor.tools().set_setting("edge_bevel", change.id, change.value);
    ASSERT_TRUE(unchanged.ok()) << unchanged.error_message();
    EXPECT_FALSE(unchanged.changed());
    EXPECT_EQ(test::capture_command_observable_state(editor).domain(Domain::Geometry),
              current.domain(Domain::Geometry));
    previous = current;
  }
  ASSERT_TRUE(editor.tools().reset_active_settings().ok());
  EXPECT_NE(test::capture_command_observable_state(editor).domain(Domain::Geometry),
            previous.domain(Domain::Geometry));
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
}

} // namespace
} // namespace quader
