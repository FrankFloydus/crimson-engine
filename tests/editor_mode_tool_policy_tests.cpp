////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_editor_api.hpp"
#include "support/behavior_expect.hpp"
#include "support/behavior_scenario.hpp"

#include <gtest/gtest.h>

#include <array>
#include <algorithm>
#include <string_view>

namespace {

quader::QuaderModelingObjectRef create_policy_box(quader::QuaderEditorApi &editor) {
  const auto box = editor.modeling().create_box(
      quader::QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                           .max = {1.0f, 1.0f, 1.0f}}, "Policy Box");
  EXPECT_TRUE(box.ok()) << box.error_message();
  EXPECT_TRUE(editor.selection().select_object(box.value()).ok());
  return box.value();
}

TEST(EditorModeToolPolicyTests, ComponentToolsRejectObjectModeWithoutChangingTheActiveTool) {
  auto standalone = quader::create_standalone_editor();
  auto &editor = standalone.editor();
  (void)create_policy_box(editor);
  ASSERT_TRUE(editor.tools().activate("move").ok());
  const auto before = editor.revisions();
  const auto history = editor.history().undo_history().history_revision;
  const auto generation = editor.mode().generation();
  for (const std::string_view tool : {"knife", "insert_edge_loop", "extrude", "uv_peel"}) {
    SCOPED_TRACE(tool);
    EXPECT_FALSE(editor.tools().can_activate(tool));
    const auto result = editor.tools().activate(tool);
    EXPECT_FALSE(result.ok());
    EXPECT_FALSE(result.changed());
    EXPECT_EQ(editor.tools().active_tool(), "move");
    EXPECT_EQ(editor.mode().current(), quader::QuaderEditorMode::Object);
    EXPECT_EQ(editor.mode().generation(), generation);
    EXPECT_EQ(editor.history().undo_history().history_revision, history);
    EXPECT_EQ(editor.revisions().modeling_content_revision, before.modeling_content_revision);
    EXPECT_EQ(editor.revisions().modeling_selection_revision, before.modeling_selection_revision);
  }
  EXPECT_FALSE(editor.tools().activate_extrude(quader::QuaderExtrudeMode::Region).ok());
}

TEST(EditorModeToolPolicyTests, ObjectToolsAndCreationCannotSettleOrReplaceAnEditContext) {
  auto standalone = quader::create_standalone_editor();
  auto &editor = standalone.editor();
  const auto box = create_policy_box(editor);
  ASSERT_TRUE(editor.modeling().copy_selection().ok());
  ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Edit).ok());
  ASSERT_TRUE(editor.tools().activate("rotate").ok());
  const auto before = editor.revisions();
  const auto generation = editor.mode().generation();
  const auto history = editor.history().undo_history().history_revision;
  for (const std::string_view tool : {"create_primitive", "cut", "poly", "mirror", "pivot",
                                     "linear_array", "radial_array", "path_array", "curve", "align"}) {
    SCOPED_TRACE(tool);
    EXPECT_FALSE(editor.tools().can_activate(tool));
    const auto result = editor.tools().activate(tool);
    EXPECT_FALSE(result.ok());
    EXPECT_FALSE(result.changed());
  }
  EXPECT_FALSE(editor.tools().begin_primitive_creation(quader::QuaderPrimitiveCreationShape::Box).ok());
  EXPECT_FALSE(editor.tools().begin_new_curve_creation().ok());
  EXPECT_FALSE(editor.tools().begin_light_creation(quader::QuaderLightType::Point).ok());
  EXPECT_EQ(editor.tools().active_tool(), "rotate");
  EXPECT_EQ(editor.mode().current(), quader::QuaderEditorMode::Edit);
  EXPECT_EQ(editor.mode().generation(), generation);
  const auto mode = editor.mode().state();
  ASSERT_EQ(mode.participants.size(), 1u);
  EXPECT_EQ(mode.participants.front().id, box.entity);
  EXPECT_EQ(editor.history().undo_history().history_revision, history);
  EXPECT_EQ(editor.revisions().scene_revision, before.scene_revision);
  EXPECT_EQ(editor.revisions().asset_revision, before.asset_revision);
  EXPECT_EQ(editor.revisions().modeling_content_revision, before.modeling_content_revision);
  EXPECT_EQ(editor.revisions().modeling_selection_revision, before.modeling_selection_revision);
}

TEST(EditorModeToolPolicyTests, CuttingToolsChangeComponentKindOnlyInsideTheExistingEditSet) {
  for (const std::string_view tool : {"knife", "insert_edge_loop"}) {
    for (const auto kind : {quader::QuaderSelectionMode::Vertex,
                           quader::QuaderSelectionMode::Face}) {
      SCOPED_TRACE(tool);
      SCOPED_TRACE(static_cast<int>(kind));
      auto standalone = quader::create_standalone_editor();
      auto &editor = standalone.editor();
      const auto box = create_policy_box(editor);
      ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Edit).ok());
      ASSERT_TRUE(editor.selection().set_mode(kind).ok());
      const auto generation = editor.mode().generation();
      const auto history = editor.history().undo_history().history_revision;
      ASSERT_TRUE(editor.tools().can_activate(tool));
      const auto result = editor.tools().activate(tool);
      ASSERT_TRUE(result.ok()) << result.error_message();
      EXPECT_EQ(editor.selection().summary().mode, quader::QuaderSelectionMode::Edge);
      EXPECT_EQ(editor.mode().generation(), generation);
      const auto state = editor.mode().state();
      ASSERT_EQ(state.participants.size(), 1u);
      EXPECT_EQ(state.participants.front().id, box.entity);
      EXPECT_EQ(state.component_kind, quader::QuaderSelectionMode::Edge);
      EXPECT_EQ(editor.history().undo_history().history_revision, history);
      ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Object).ok());
      EXPECT_EQ(editor.tools().active_tool(), "select");
      ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Edit).ok());
      EXPECT_EQ(editor.tools().active_tool(), "select");
      EXPECT_EQ(editor.selection().summary().mode, quader::QuaderSelectionMode::Edge);
    }
  }
}

TEST(EditorModeToolPolicyTests, WholeObjectAuthoringAndClipboardRejectEditWithoutPartialBatchEffects) {
  auto standalone = quader::create_standalone_editor();
  auto &editor = standalone.editor();
  const auto parent = editor.scene().create_entity({.name = "Parent"});
  ASSERT_TRUE(parent.ok());
  const auto box = create_policy_box(editor);
  ASSERT_TRUE(editor.modeling().copy_selection().ok());
  ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Edit).ok());
  const auto before = editor.revisions();
  const auto history = editor.history().undo_history().history_revision;
  const auto original = editor.query().entity(box.entity);
  ASSERT_TRUE(original.has_value());
  quader::QuaderTransform moved;
  moved.location = {8.0f, 0.0f, 0.0f};
  EXPECT_FALSE(editor.scene().create_entity({.name = "Rejected"}).ok());
  EXPECT_FALSE(editor.scene().create_empty().ok());
  EXPECT_FALSE(editor.scene().destroy(box.entity).ok());
  EXPECT_FALSE(editor.scene().duplicate(box.entity).ok());
  EXPECT_FALSE(editor.scene().set_transform(box.entity, moved).ok());
  EXPECT_FALSE(editor.scene().reparent(box.entity, parent.value().id).ok());
  EXPECT_FALSE(editor.scene().can_group_selection());
  EXPECT_FALSE(editor.scene().group_selection().ok());
  EXPECT_FALSE(editor.modeling().copy_selection().ok());
  EXPECT_FALSE(editor.modeling().paste_selection().ok());
  EXPECT_FALSE(editor.modeling().can_execute_tool("delete_selection"));
  EXPECT_FALSE(editor.modeling().delete_selection().changed());
  EXPECT_FALSE(editor.scene().batch("Rejected Mixed Batch")
      .rename(box.entity, "Should Not Be Renamed")
      .set_transform(box.entity, moved).commit().ok());
  EXPECT_EQ(editor.query().entity(box.entity)->name, original->name);
  EXPECT_EQ(editor.history().undo_history().history_revision, history);
  EXPECT_EQ(editor.revisions().scene_revision, before.scene_revision);
  EXPECT_EQ(editor.revisions().asset_revision, before.asset_revision);
  EXPECT_EQ(editor.revisions().modeling_content_revision, before.modeling_content_revision);
  EXPECT_TRUE(editor.scene().rename(box.entity, "Renamed In Edit").ok());
  EXPECT_EQ(editor.query().entity(box.entity)->name, "Renamed In Edit");
}

TEST(EditorModeToolPolicyTests, CommonToolsSurviveModeTransitionsAndAvailabilityHasAnIdleGate) {
  for (const std::string_view tool : {"select", "lasso_select", "paint_select", "move", "rotate", "scale"}) {
    SCOPED_TRACE(tool);
    auto standalone = quader::create_standalone_editor();
    auto &editor = standalone.editor();
    (void)create_policy_box(editor);
    ASSERT_TRUE(editor.tools().activate(tool).ok());
    const auto object_revision = editor.tools().availability_revision();
    EXPECT_EQ(editor.tools().availability_revision(), object_revision);
    ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Edit).ok());
    EXPECT_EQ(editor.tools().active_tool(), tool);
    const auto edit_revision = editor.tools().availability_revision();
    EXPECT_GT(edit_revision, object_revision);
    EXPECT_EQ(editor.tools().availability_revision(), edit_revision);
    ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Object).ok());
    EXPECT_EQ(editor.tools().active_tool(), tool);
    EXPECT_GT(editor.tools().availability_revision(), edit_revision);
  }
}

TEST(EditorModeToolPolicyTests, EnteringEditRetiresPersistentNativeToolsWithoutResurrection) {
  auto standalone = quader::create_standalone_editor();
  auto &editor = standalone.editor();
  const auto first = create_policy_box(editor);
  const auto second = create_policy_box(editor);
  ASSERT_TRUE(editor.scene().select({first.entity, second.entity}, second.entity).ok());
  ASSERT_TRUE(editor.tools().activate("align").ok());
  EXPECT_EQ(editor.tools().active_tool(), "align");
  ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Edit).ok());
  EXPECT_EQ(editor.tools().active_tool(), "select");
  ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Object).ok());
  EXPECT_EQ(editor.tools().active_tool(), "select");
}

TEST(EditorModeToolPolicyTests, ModeRejectionPrecedesAcceptanceOfAnUnrelatedPropertyDraft) {
  for (const auto mode : {quader::QuaderEditorMode::Object, quader::QuaderEditorMode::Edit}) {
    SCOPED_TRACE(static_cast<int>(mode));
    auto standalone = quader::create_standalone_editor();
    auto &editor = standalone.editor();
    (void)create_policy_box(editor);
    ASSERT_TRUE(editor.mode().set(mode).ok());
    const auto draft = editor.scene().begin_environment_color_edit();
    ASSERT_TRUE(draft.ok());
    ASSERT_TRUE(editor.scene().update_environment_color_edit(draft.value(), {0.25f, 0.35f, 0.45f}).ok());
    const auto history = editor.history().undo_history().history_revision;
    const auto rejected = editor.tools().activate(
        mode == quader::QuaderEditorMode::Object ? "knife" : "create_primitive");
    EXPECT_FALSE(rejected.ok());
    EXPECT_FALSE(rejected.changed());
    EXPECT_EQ(editor.history().undo_history().history_revision, history);
    // A prematurely accepted draft would invalidate this exact preview handle.
    EXPECT_TRUE(editor.scene().finish_environment_color_edit(draft.value(), true).ok());
    EXPECT_EQ(editor.history().undo_history().history_revision, history);
  }
}

TEST(EditorModeToolPolicyTests, ObjectMirrorAndFlipRejectEveryComponentKindBeforeAcceptingDrafts) {
  constexpr std::array<std::string_view, 5> kActions{
      "mirror_selection_x", "mirror_selection_y", "mirror_selection_z",
      "flip_horizontal", "flip_vertical"};
  constexpr std::array kCommands{
      quader::quader_command::kModelingMirrorSelectionX,
      quader::quader_command::kModelingMirrorSelectionY,
      quader::quader_command::kModelingMirrorSelectionZ,
      quader::quader_command::kModelingFlipHorizontal,
      quader::quader_command::kModelingFlipVertical};
  for (const auto kind : {quader::QuaderSelectionMode::Vertex,
                          quader::QuaderSelectionMode::Edge,
                          quader::QuaderSelectionMode::Face}) {
    SCOPED_TRACE(static_cast<int>(kind));
    auto standalone = quader::create_standalone_editor();
    auto &editor = standalone.editor();
    const auto box = create_policy_box(editor);
    for (const auto action : kActions) {
      EXPECT_TRUE(editor.modeling().can_execute_tool(action));
    }
    ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Edit).ok());
    ASSERT_TRUE(editor.selection().set_mode(kind).ok());
    ASSERT_TRUE(editor.selection().select_all().ok());
    const auto selection = editor.selection().summary();
    ASSERT_TRUE(selection.has_selection);
    const auto draft = editor.scene().begin_environment_color_edit();
    ASSERT_TRUE(draft.ok());
    ASSERT_TRUE(editor.scene().update_environment_color_edit(
        draft.value(), {0.25f, 0.35f, 0.45f}).ok());
    const auto before = editor.revisions();
    const auto history = editor.history().undo_history().history_revision;
    const auto mode = editor.mode().state();
    for (std::size_t action_index = 0; action_index < kActions.size(); ++action_index) {
      SCOPED_TRACE(kActions[action_index]);
      EXPECT_FALSE(editor.modeling().can_execute_tool(kActions[action_index]));
      const auto descriptor = editor.commands().descriptor(kCommands[action_index]);
      ASSERT_TRUE(descriptor.has_value());
      EXPECT_FALSE(descriptor->enabled);
      const auto direct = editor.modeling().execute_tool(kActions[action_index]);
      EXPECT_FALSE(direct.ok());
      EXPECT_FALSE(direct.changed());
      const auto command = editor.commands().execute(kCommands[action_index]);
      EXPECT_FALSE(command.ok());
      EXPECT_FALSE(command.changed());
      EXPECT_EQ(editor.history().undo_history().history_revision, history);
      EXPECT_EQ(editor.revisions().scene_revision, before.scene_revision);
      EXPECT_EQ(editor.revisions().asset_revision, before.asset_revision);
      EXPECT_EQ(editor.revisions().modeling_content_revision,
                before.modeling_content_revision);
      EXPECT_EQ(editor.revisions().modeling_selection_revision,
                before.modeling_selection_revision);
      EXPECT_EQ(editor.mode().revision(), mode.revision);
      EXPECT_EQ(editor.mode().generation(), mode.generation);
      const auto after = editor.selection().summary();
      EXPECT_EQ(after.mode, kind);
      EXPECT_EQ(after.selected_vertex_count, selection.selected_vertex_count);
      EXPECT_EQ(after.selected_edge_count, selection.selected_edge_count);
      EXPECT_EQ(after.selected_face_count, selection.selected_face_count);
      ASSERT_EQ(editor.mode().state().participants.size(), 1u);
      EXPECT_EQ(editor.mode().state().participants.front().id, box.entity);
      EXPECT_EQ(editor.query().modeling_objects().size(), 1u);
    }
    // Early settlement would consume this exact preview handle and add history.
    EXPECT_TRUE(editor.scene().finish_environment_color_edit(draft.value(), true).ok());
    EXPECT_EQ(editor.history().undo_history().history_revision, history);
  }
}

TEST(EditorModeToolPolicyTests, ExtractCountsMeshesWithSelectedFacesInsteadOfAllEditParticipants) {
  auto standalone = quader::create_standalone_editor();
  auto &editor = standalone.editor();
  const auto source = create_policy_box(editor);
  const auto other = create_policy_box(editor);
  const auto empty = create_policy_box(editor);
  ASSERT_TRUE(editor.scene().select({source.entity, other.entity, empty.entity}, source.entity).ok());
  ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Edit).ok());
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection().select_face(source, 1).ok());
  ASSERT_EQ(editor.mode().state().participants.size(), 3u);
  EXPECT_TRUE(editor.modeling().can_execute_tool("extract_faces"));
  const auto operations = editor.modeling().instant_operations();
  const auto extract = std::ranges::find_if(operations, [](const auto &operation) {
    return operation.id == "extract_faces";
  });
  ASSERT_NE(extract, operations.end());
  EXPECT_TRUE(extract->enabled);

  ASSERT_TRUE(editor.selection().select_face(other, 1, quader::QuaderSelectionEdit::Add).ok());
  EXPECT_FALSE(editor.modeling().can_execute_tool("extract_faces"));
  const auto before = editor.revisions();
  const auto history = editor.history().undo_history().history_revision;
  const auto rejected = editor.modeling().execute_tool("extract_faces");
  EXPECT_FALSE(rejected.ok());
  EXPECT_FALSE(rejected.changed());
  EXPECT_EQ(editor.revisions().modeling_content_revision, before.modeling_content_revision);
  EXPECT_EQ(editor.revisions().modeling_selection_revision, before.modeling_selection_revision);
  EXPECT_EQ(editor.history().undo_history().history_revision, history);
  EXPECT_EQ(editor.mode().state().participants.size(), 3u);

  ASSERT_TRUE(editor.selection().select_face(other, 1, quader::QuaderSelectionEdit::Remove).ok());
  EXPECT_TRUE(editor.modeling().can_execute_tool("extract_faces"));
}

TEST(EditorModeToolPolicyTests, RepeatingAnEditOnlyOperationInObjectModeRejectsBeforeMutation) {
  auto standalone = quader::create_standalone_editor();
  auto &editor = standalone.editor();
  const auto source = create_policy_box(editor);
  const auto empty = create_policy_box(editor);
  ASSERT_TRUE(editor.scene().select({source.entity, empty.entity}, source.entity).ok());
  ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Edit).ok());
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection().select_face(source, 1).ok());
  const auto extracted = editor.modeling().execute_tool("extract_faces");
  ASSERT_TRUE(extracted.ok()) << extracted.error_message();
  ASSERT_TRUE(extracted.changed());
  ASSERT_EQ(editor.query().modeling_objects().size(), 3u);
  ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Object).ok());
  const auto before = editor.revisions();
  const auto history = editor.history().undo_history().history_revision;
  const auto repeated = editor.history().repeat_last_action();
  EXPECT_FALSE(repeated.ok());
  EXPECT_FALSE(repeated.changed());
  EXPECT_EQ(editor.revisions().scene_revision, before.scene_revision);
  EXPECT_EQ(editor.revisions().asset_revision, before.asset_revision);
  EXPECT_EQ(editor.revisions().modeling_content_revision, before.modeling_content_revision);
  EXPECT_EQ(editor.revisions().modeling_selection_revision, before.modeling_selection_revision);
  EXPECT_EQ(editor.history().undo_history().history_revision, history);
  EXPECT_EQ(editor.query().modeling_objects().size(), 3u);
  EXPECT_EQ(editor.mode().current(), quader::QuaderEditorMode::Object);
}

TEST(EditorModeToolPolicyTests, RepeatingAnObjectOperationInEditModePreservesItsMembership) {
  auto standalone = quader::create_standalone_editor();
  auto &editor = standalone.editor();
  const auto source = create_policy_box(editor);
  ASSERT_TRUE(editor.modeling().execute_tool("half_cut").ok());
  ASSERT_TRUE(editor.modeling().commit_preview("Half-Cut Meshes").ok());
  ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Edit).ok());
  const auto before = editor.revisions();
  const auto history = editor.history().undo_history().history_revision;
  const auto generation = editor.mode().generation();
  const auto repeated = editor.history().repeat_last_action();
  EXPECT_FALSE(repeated.ok());
  EXPECT_FALSE(repeated.changed());
  EXPECT_EQ(editor.revisions().scene_revision, before.scene_revision);
  EXPECT_EQ(editor.revisions().asset_revision, before.asset_revision);
  EXPECT_EQ(editor.revisions().modeling_content_revision, before.modeling_content_revision);
  EXPECT_EQ(editor.revisions().modeling_selection_revision, before.modeling_selection_revision);
  EXPECT_EQ(editor.history().undo_history().history_revision, history);
  EXPECT_EQ(editor.mode().current(), quader::QuaderEditorMode::Edit);
  EXPECT_EQ(editor.mode().generation(), generation);
  const auto state = editor.mode().state();
  ASSERT_EQ(state.participants.size(), 1u);
  EXPECT_EQ(state.participants.front().id, source.entity);
}

TEST(EditorModeToolPolicyTests, RepeatingComponentExtrusionCannotBecomeAnObjectMove) {
  auto scenario = quader::test::BehaviorScenario::new_project();
  scenario.given().unit_box("box").edit_mode()
      .selection_mode(quader::QuaderSelectionMode::Face);
  scenario.when().pick({0.0f, 0.0f, 5.0f});
  scenario.when().begin_transform_drag({
      .tool = quader::ModelingTransformTool::Move,
      .operation = quader::ModelingTransformOperation::Extrude,
  }).update_transform_drag(120.0f).commit_drag();
  scenario.then().command().ok_changed();
  auto &editor = scenario.editor();
  ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Object).ok());
  const auto before = editor.revisions();
  const auto history = editor.history().undo_history().history_revision;
  const auto transform = editor.query().entity(scenario.entity("box"))->transform;
  const auto repeated = editor.history().repeat_last_action();
  EXPECT_FALSE(repeated.ok());
  EXPECT_FALSE(repeated.changed());
  EXPECT_EQ(editor.revisions().scene_revision, before.scene_revision);
  EXPECT_EQ(editor.revisions().asset_revision, before.asset_revision);
  EXPECT_EQ(editor.revisions().modeling_content_revision, before.modeling_content_revision);
  EXPECT_EQ(editor.history().undo_history().history_revision, history);
  const auto after = editor.query().entity(scenario.entity("box"))->transform;
  EXPECT_FLOAT_EQ(after.location.x, transform.location.x);
  EXPECT_FLOAT_EQ(after.location.y, transform.location.y);
  EXPECT_FLOAT_EQ(after.location.z, transform.location.z);
}

} // namespace
