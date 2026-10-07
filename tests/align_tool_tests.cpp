////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "assets/asset_registry.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/tools/native_viewport_tool_coordinator.hpp"
#include "editor/transform/align_tool_service.hpp"
#include "render/overlay/render_overlay_snapshot.hpp"
#include "render/overlay/viewport_overlay_submission_coordinator.hpp"
#include "render/scene_render_extraction.hpp"
#include "scene/scene_bounds.hpp"
#include "scene/scene_transform.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <string>
#include <utility>
#include <vector>

namespace quader {
namespace {

constexpr float kAlignEpsilon = 0.0001f;

struct AlignHarness final {
  SceneSession session;
  QuaderEditorApi editor{
      QuaderApiSessionInternalAccess::from_scene_session(session)};
  std::array<QuaderModelingObjectRef, 3> objects;

  void seed() {
    constexpr std::array<QVec3, 3> kPositions{{{2, 3, 1}, {5, 9, 7}, {22, 15, 11}}};
    for (std::size_t index = 0; index < objects.size(); ++index) {
      const auto created = editor.modeling().create_box(
          {.min = {-1, -1, -1}, .max = {1, 1, 1}},
          "Align source " + std::to_string(index));
      ASSERT_TRUE(created.ok()) << created.error_message();
      objects[index] = created.value();
      ASSERT_TRUE(editor.scene().set_transform(
          objects[index].entity, {.position = kPositions[index]}).ok());
    }
    ASSERT_TRUE(editor.selection().select_objects(
        {objects[0].entity, objects[1].entity, objects[2].entity},
        objects[1].entity).ok());
    session.history().clear();
    session.mark_clean();
  }

  SceneBounds bounds(std::size_t index) const {
    const auto result = entity_world_bounds(
        session.scene(), session.assets(), objects[index].entity);
    EXPECT_TRUE(result.has_value());
    return result.value_or(SceneBounds{});
  }

  std::array<TransformComponent, 3> transforms() const {
    return {*session.scene().transform(objects[0].entity),
            *session.scene().transform(objects[1].entity),
            *session.scene().transform(objects[2].entity)};
  }
};

void expect_same_transforms(const AlignHarness &harness,
                            const std::array<TransformComponent, 3> &expected) {
  for (std::size_t index = 0; index < expected.size(); ++index) {
    EXPECT_TRUE(document_command_same_transform(
        *harness.session.scene().transform(harness.objects[index].entity),
        expected[index]));
  }
}

TEST(AlignToolTests, EveryClickUsesCurrentBoundsAndOwnsOneHistoryStep) {
  AlignHarness harness;
  ASSERT_NO_FATAL_FAILURE(harness.seed());
  const auto before = harness.transforms();
  ASSERT_TRUE(harness.editor.tools().activate(ViewportToolId::Align).ok());
  auto &align = harness.session.align_tool();
  EXPECT_TRUE(align.active());
  expect_same_transforms(harness, before);
  EXPECT_EQ(harness.session.history().undo_count(), 0u);

  const auto distributed = align.set_distribution(MeshArrangementAxis::X);
  ASSERT_TRUE(distributed.ok) << distributed.message;
  EXPECT_TRUE(distributed.changed);
  EXPECT_NEAR(harness.bounds(0).min.x, 1.0f, kAlignEpsilon);
  EXPECT_NEAR(harness.bounds(1).min.x, 11.0f, kAlignEpsilon);
  EXPECT_NEAR(harness.bounds(2).min.x, 21.0f, kAlignEpsilon);
  EXPECT_EQ(harness.session.history().undo_count(), 1u);

  ASSERT_TRUE(align.set_alignment(MeshArrangementAxis::Y, MeshAlignmentMode::Maximum).ok);
  const auto before_x_alignment = harness.transforms();
  EXPECT_EQ(harness.session.history().undo_count(), 2u);
  ASSERT_TRUE(align.set_alignment(MeshArrangementAxis::X, MeshAlignmentMode::Minimum).ok);
  EXPECT_EQ(harness.session.history().undo_count(), 3u);
  for (std::size_t index = 0; index < 3u; ++index) {
    EXPECT_NEAR(harness.bounds(index).min.x, 1.0f, kAlignEpsilon);
    EXPECT_NEAR(bounds_center(harness.bounds(index)).y, 15.0f, kAlignEpsilon);
  }
  const auto after = harness.transforms();
  const auto already_aligned = align.set_alignment(
      MeshArrangementAxis::X, MeshAlignmentMode::Maximum);
  ASSERT_TRUE(already_aligned.ok) << already_aligned.message;
  EXPECT_FALSE(already_aligned.changed);
  expect_same_transforms(harness, after);
  EXPECT_EQ(harness.session.history().undo_count(), 3u);
  ASSERT_TRUE(harness.editor.history().undo().ok());
  EXPECT_TRUE(align.active());
  expect_same_transforms(harness, before_x_alignment);
  ASSERT_TRUE(harness.editor.history().redo().ok());
  expect_same_transforms(harness, after);
  EXPECT_TRUE(harness.session.dirty());
}

TEST(AlignToolTests, UndoThenAnotherAxisDoesNotReplayThePreviousAction) {
  AlignHarness harness;
  ASSERT_NO_FATAL_FAILURE(harness.seed());
  const auto before = harness.transforms();
  ASSERT_TRUE(harness.editor.tools().activate(ViewportToolId::Align).ok());
  auto &align = harness.session.align_tool();
  ASSERT_TRUE(align.set_alignment(MeshArrangementAxis::X, MeshAlignmentMode::Minimum).ok);
  ASSERT_TRUE(harness.editor.history().undo().ok());
  EXPECT_TRUE(align.active());
  expect_same_transforms(harness, before);
  ASSERT_TRUE(align.set_alignment(MeshArrangementAxis::Y, MeshAlignmentMode::Center).ok);
  for (std::size_t index = 0; index < 3u; ++index) {
    const auto current = *harness.session.scene().transform(harness.objects[index].entity);
    EXPECT_FLOAT_EQ(current.position.x, before[index].position.x);
    EXPECT_FLOAT_EQ(current.position.z, before[index].position.z);
    EXPECT_NEAR(bounds_center(harness.bounds(index)).y, 9.0f, kAlignEpsilon);
  }
  EXPECT_EQ(harness.session.history().undo_count(), 1u);
  EXPECT_EQ(harness.session.history().redo_count(), 0u);
  ASSERT_TRUE(harness.editor.history().undo().ok());
  expect_same_transforms(harness, before);
}

TEST(AlignToolTests, ValidSelectionChangesRetainActionsAndInvalidTargetsRetireThePanel) {
  AlignHarness harness;
  ASSERT_NO_FATAL_FAILURE(harness.seed());
  const auto before = harness.transforms();
  ASSERT_TRUE(harness.editor.tools().activate(ViewportToolId::Align).ok());
  auto &align = harness.session.align_tool();
  ASSERT_TRUE(harness.editor.selection().select_objects(
      {harness.objects[0].entity, harness.objects[1].entity},
      harness.objects[0].entity).ok());
  EXPECT_TRUE(align.active());
  EXPECT_FALSE(align.can_distribute());
  const auto before_action_history = harness.session.history().undo_count();
  ASSERT_TRUE(align.set_alignment(MeshArrangementAxis::Y, MeshAlignmentMode::Maximum).ok);
  EXPECT_EQ(harness.session.history().undo_count(), before_action_history + 1u);
  EXPECT_NEAR(bounds_center(harness.bounds(0)).y, 9.0f, kAlignEpsilon);
  EXPECT_NEAR(bounds_center(harness.bounds(1)).y, 9.0f, kAlignEpsilon);
  EXPECT_TRUE(document_command_same_transform(
      *harness.session.scene().transform(harness.objects[2].entity), before[2]));
  const auto accepted = harness.transforms();
  const auto accepted_history = harness.session.history().undo_count();
  EXPECT_FALSE(align.refresh().changed);
  // Other native owners first observe the changed scene and selection stamps.
  ASSERT_TRUE(harness.session.native_tools().refresh().ok);
  EXPECT_TRUE(align.active());
  expect_same_transforms(harness, accepted);
  EXPECT_EQ(harness.session.history().undo_count(), accepted_history);
  const auto revision = align.settings_revision();
  const auto availability_queries = align.work_counters().availability_query_count;
  const auto context_validations = align.work_counters().context_validation_count;
  for (int frame = 0; frame < 8; ++frame) {
    EXPECT_EQ(align.settings_revision(), revision);
    EXPECT_TRUE(align.active());
    EXPECT_TRUE(align.can_activate());
    EXPECT_FALSE(harness.session.native_tools().refresh().changed);
  }
  EXPECT_EQ(align.work_counters().availability_query_count, availability_queries);
  EXPECT_EQ(align.work_counters().context_validation_count, context_validations);
  ASSERT_TRUE(harness.session.scene().set_locked(harness.objects[1].entity, true));
  EXPECT_GT(align.settings_revision(), revision);
  EXPECT_FALSE(align.can_activate());
  EXPECT_FALSE(align.active());
  EXPECT_FALSE(align.set_alignment(MeshArrangementAxis::Z, MeshAlignmentMode::Center).ok);
  expect_same_transforms(harness, accepted);
  EXPECT_EQ(harness.session.history().undo_count(), accepted_history);
  EXPECT_TRUE(harness.editor.tools().active_settings().tool_id.empty());
  EXPECT_TRUE(harness.editor.tools().settings_for_pane(ViewportPaneId::Perspective).tool_id.empty());
  EXPECT_TRUE(harness.session.native_tools().refresh().changed);
  EXPECT_FALSE(harness.session.native_tools().refresh().changed);
  ASSERT_TRUE(harness.session.scene().set_locked(harness.objects[1].entity, false));
  ASSERT_TRUE(harness.session.scene().set_visible(harness.objects[1].entity, false));
  EXPECT_FALSE(align.set_alignment(MeshArrangementAxis::X, MeshAlignmentMode::Minimum).ok);
  expect_same_transforms(harness, accepted);
  EXPECT_EQ(harness.session.history().undo_count(), accepted_history);
  ASSERT_TRUE(harness.session.scene().set_visible(harness.objects[1].entity, true));
  EXPECT_FALSE(align.active());
  ASSERT_TRUE(harness.editor.tools().activate(ViewportToolId::Align).ok());
  ASSERT_TRUE(harness.editor.selection().select_object(harness.objects[0]).ok());
  const auto before_invalid_history = harness.session.history().undo_count();
  EXPECT_FALSE(align.can_activate());
  EXPECT_FALSE(align.set_distribution(MeshArrangementAxis::X).ok);
  EXPECT_EQ(harness.session.history().undo_count(), before_invalid_history);
  EXPECT_FALSE(align.active());
}

TEST(AlignToolTests, ImmediateActionsPublishTextureLocksAndUndoRestoresContent) {
  AlignHarness harness;
  ASSERT_NO_FATAL_FAILURE(harness.seed());
  harness.session.document_commands().set_texture_lock_settings_provider(
      [] { return TextureLockSettings{.texture_lock = true, .texture_lock_scale = true}; });
  std::array<TextureMappingComponent, 3> mappings;
  for (std::size_t index = 0; index < 3u; ++index) {
    const auto *mapping = harness.session.scene().texture_mapping(harness.objects[index].entity);
    if (mapping != nullptr) mappings[index] = *mapping;
    ASSERT_TRUE(harness.session.scene().set_texture_mapping(harness.objects[index].entity, mappings[index]));
  }
  harness.session.mark_clean();
  EXPECT_FALSE(harness.session.dirty());
  const auto before = harness.transforms();
  const auto selected_before = harness.session.scene().selected_entities();
  const auto assets_revision = harness.session.assets().revision();
  const auto modeling_revision = harness.session.modeling().content_revision();
  ASSERT_TRUE(harness.editor.tools().activate(ViewportToolId::Align).ok());
  ASSERT_TRUE(harness.session.align_tool().set_alignment(
      MeshArrangementAxis::X, MeshAlignmentMode::Center).ok);
  EXPECT_TRUE(harness.session.dirty());
  EXPECT_EQ(harness.session.history().undo_count(), 1u);
  EXPECT_EQ(harness.session.assets().revision(), assets_revision);
  EXPECT_EQ(harness.session.modeling().content_revision(), modeling_revision);
  const auto after = harness.transforms();
  std::array<TextureMappingComponent, 3> after_mappings;
  for (std::size_t index = 0; index < 3u; ++index) {
    after_mappings[index] = *harness.session.scene().texture_mapping(harness.objects[index].entity);
  }
  ASSERT_TRUE(harness.editor.history().undo().ok());
  expect_same_transforms(harness, before);
  for (std::size_t index = 0; index < 3u; ++index) {
    EXPECT_EQ(*harness.session.scene().texture_mapping(harness.objects[index].entity), mappings[index]);
  }
  EXPECT_EQ(harness.session.scene().selected_entities(), selected_before);
  EXPECT_EQ(harness.session.assets().revision(), assets_revision);
  EXPECT_EQ(harness.session.modeling().content_revision(), modeling_revision);
  EXPECT_EQ(harness.session.history().undo_count(), 0u);
  EXPECT_EQ(harness.session.history().redo_count(), 1u);
  // Ordinary undo advances content revisions under SceneSession::dirty's contract.
  EXPECT_TRUE(harness.session.dirty());
  EXPECT_TRUE(harness.session.align_tool().active());
  ASSERT_TRUE(harness.editor.history().redo().ok());
  expect_same_transforms(harness, after);
  for (std::size_t index = 0; index < 3u; ++index) {
    EXPECT_EQ(*harness.session.scene().texture_mapping(harness.objects[index].entity), after_mappings[index]);
  }
  EXPECT_EQ(harness.session.scene().selected_entities(), selected_before);
  EXPECT_EQ(harness.session.history().undo_count(), 1u);
  EXPECT_EQ(harness.session.history().redo_count(), 0u);
}

TEST(AlignToolTests, UnusableContextsRetireAllPublicSettingsWithoutChangingAuthoredHistory) {
  enum class ContextChange {
    FaceMode,
    EdgeMode,
    VertexMode,
    SingleRoot,
    EmptySelection,
    HiddenRoot,
    LockedRoot,
    HiddenLayer,
    LockedLayer,
    DeletedRoot,
    MissingMesh,
    SingularCompensation,
  };
  constexpr std::array kChanges{
      ContextChange::FaceMode, ContextChange::EdgeMode, ContextChange::VertexMode,
      ContextChange::SingleRoot, ContextChange::EmptySelection,
      ContextChange::HiddenRoot, ContextChange::LockedRoot,
      ContextChange::HiddenLayer, ContextChange::LockedLayer,
      ContextChange::DeletedRoot, ContextChange::MissingMesh,
      ContextChange::SingularCompensation,
  };
  for (const auto change : kChanges) {
    SCOPED_TRACE(static_cast<int>(change));
    AlignHarness harness;
    ASSERT_NO_FATAL_FAILURE(harness.seed());
    ASSERT_TRUE(harness.editor.viewports().set_layout(ViewportLayoutId::FourQuad).ok());
    ASSERT_TRUE(harness.editor.selection().select_objects(
        {harness.objects[0].entity, harness.objects[1].entity},
        harness.objects[0].entity).ok());
    harness.session.history().clear();
    ASSERT_TRUE(harness.editor.tools().activate(ViewportToolId::Align).ok());
    auto &align = harness.session.align_tool();
    ASSERT_TRUE(align.set_use_snapping(true).ok);
    ASSERT_TRUE(align.set_alignment(MeshArrangementAxis::Y, MeshAlignmentMode::Maximum).ok);
    const auto committed = harness.transforms();
    const auto settings_revision = harness.editor.tools().active_settings_revision();
    const auto tool_revision = harness.editor.tools().active_tool_revision();
    switch (change) {
    case ContextChange::FaceMode:
      ASSERT_TRUE(harness.editor.mode().set(QuaderEditorMode::Edit).ok());
      ASSERT_TRUE(harness.editor.selection().set_mode(QuaderSelectionMode::Face).ok());
      break;
    case ContextChange::EdgeMode:
      ASSERT_TRUE(harness.editor.mode().set(QuaderEditorMode::Edit).ok());
      ASSERT_TRUE(harness.editor.selection().set_mode(QuaderSelectionMode::Edge).ok());
      break;
    case ContextChange::VertexMode:
      ASSERT_TRUE(harness.editor.mode().set(QuaderEditorMode::Edit).ok());
      ASSERT_TRUE(harness.editor.selection().set_mode(QuaderSelectionMode::Vertex).ok());
      break;
    case ContextChange::SingleRoot:
      ASSERT_TRUE(harness.editor.selection().select_object(harness.objects[0]).ok());
      break;
    case ContextChange::EmptySelection:
      ASSERT_TRUE(harness.editor.selection().clear().ok());
      break;
    case ContextChange::HiddenRoot:
      ASSERT_TRUE(harness.session.scene().set_visible(harness.objects[0].entity, false));
      break;
    case ContextChange::LockedRoot:
      ASSERT_TRUE(harness.session.scene().set_locked(harness.objects[0].entity, true));
      break;
    case ContextChange::HiddenLayer:
    case ContextChange::LockedLayer: {
      const auto layer = harness.session.scene().create_layer("Align context layer");
      ASSERT_TRUE(harness.session.scene().set_entity_layer(harness.objects[0].entity, layer));
      if (change == ContextChange::HiddenLayer) {
        ASSERT_TRUE(harness.session.scene().set_layer_visible(layer, false));
      } else {
        ASSERT_TRUE(harness.session.scene().set_layer_locked(layer, true));
      }
      break;
    }
    case ContextChange::DeletedRoot:
      ASSERT_TRUE(harness.session.scene().destroy_entity(harness.objects[0].entity));
      break;
    case ContextChange::MissingMesh: {
      const auto *mesh = harness.session.scene().mesh(harness.objects[0].entity);
      ASSERT_NE(mesh, nullptr);
      ASSERT_TRUE(harness.session.assets().remove_mesh(mesh->mesh));
      break;
    }
    case ContextChange::SingularCompensation: {
      auto transform = *harness.session.scene().transform(harness.objects[0].entity);
      transform.parenting_offset.values[0] = 0.0f;
      ASSERT_TRUE(harness.session.scene().set_transform(harness.objects[0].entity, transform));
      break;
    }
    }
    const auto history_count = harness.session.history().undo_count();
    const auto scene_revision = harness.session.scene().revision();
    const auto acquired =
        harness.editor.tools().acquire_pane_tool(ViewportPaneId::Perspective);
    EXPECT_TRUE(acquired.ok()) << acquired.error_message();
    EXPECT_FALSE(acquired.changed());
    EXPECT_FALSE(align.active());
    EXPECT_FALSE(harness.session.native_tools().active("align"));
    EXPECT_NE(harness.editor.tools().active_tool(), "align");
    EXPECT_GT(harness.editor.tools().active_tool_revision(), tool_revision);
    EXPECT_GT(harness.editor.tools().active_settings_revision(), settings_revision);
    EXPECT_TRUE(harness.editor.tools().active_settings().tool_id.empty());
    for (const auto pane : {ViewportPaneId::Perspective, ViewportPaneId::Top,
                           ViewportPaneId::Front, ViewportPaneId::Side}) {
      EXPECT_EQ(harness.editor.viewports().selected_tool(pane), ViewportToolId::Select);
      const auto target = harness.editor.tools().settings_for_pane(pane);
      EXPECT_TRUE(target.tool_id.empty());
      EXPECT_TRUE(target.controls.empty());
    }
    EXPECT_EQ(harness.session.scene().revision(), scene_revision);
    EXPECT_EQ(harness.session.history().undo_count(), history_count);
    for (std::size_t index = 1; index < harness.objects.size(); ++index) {
      EXPECT_TRUE(document_command_same_transform(
          *harness.session.scene().transform(harness.objects[index].entity), committed[index]));
    }
    EXPECT_TRUE(align.settings().use_snapping);
    const bool component_mode_transition = change == ContextChange::FaceMode ||
        change == ContextChange::EdgeMode || change == ContextChange::VertexMode;
    EXPECT_EQ(harness.session.native_tools().refresh().changed,
              !component_mode_transition);
    const auto availability_queries = align.work_counters().availability_query_count;
    const auto context_validations = align.work_counters().context_validation_count;
    for (int frame = 0; frame < 8; ++frame) {
      EXPECT_FALSE(harness.session.native_tools().refresh().changed);
      EXPECT_FALSE(align.active());
      EXPECT_TRUE(harness.editor.tools().active_settings().tool_id.empty());
      EXPECT_TRUE(harness.editor.tools().settings_for_pane(ViewportPaneId::Perspective).tool_id.empty());
    }
    EXPECT_EQ(align.work_counters().availability_query_count, availability_queries);
    EXPECT_EQ(align.work_counters().context_validation_count, context_validations);
    if (component_mode_transition) {
      ASSERT_TRUE(harness.editor.mode().set(QuaderEditorMode::Object).ok());
      ASSERT_TRUE(harness.editor.selection().set_mode(QuaderSelectionMode::Object).ok());
      EXPECT_FALSE(align.active());
      EXPECT_TRUE(align.can_activate());
      const auto reacquired =
          harness.editor.tools().acquire_pane_tool(ViewportPaneId::Perspective);
      EXPECT_TRUE(reacquired.ok()) << reacquired.error_message();
      EXPECT_FALSE(reacquired.changed());
      EXPECT_FALSE(align.active());
      EXPECT_EQ(harness.editor.viewports().selected_tool(ViewportPaneId::Perspective),
                ViewportToolId::Select);
      EXPECT_TRUE(harness.editor.tools().active_settings().tool_id.empty());
      ASSERT_TRUE(harness.editor.tools().activate(ViewportToolId::Align).ok());
      EXPECT_TRUE(align.active());
      EXPECT_TRUE(align.settings().use_snapping);
      expect_same_transforms(harness, committed);
    }
  }
}

TEST(AlignToolTests, SharedPanesKeepOneOriginAndTransitionsNeverRevertActions) {
  AlignHarness harness;
  ASSERT_NO_FATAL_FAILURE(harness.seed());
  ASSERT_TRUE(harness.editor.viewports().set_layout(ViewportLayoutId::FourQuad).ok());
  ASSERT_TRUE(harness.editor.viewports().focus_pane(ViewportPaneId::Perspective).ok());
  ASSERT_TRUE(harness.editor.tools().activate(ViewportToolId::Align).ok());
  ASSERT_TRUE(harness.session.align_tool().set_alignment(
      MeshArrangementAxis::X, MeshAlignmentMode::Minimum).ok);
  const auto after = harness.transforms();
  const auto serial = harness.editor.tools().active_settings_presentation_origin().serial;
  EXPECT_FALSE(harness.session.native_tools().has_pending_tool());
  EXPECT_FALSE(harness.session.native_tools().has_pending_history_preview());
  EXPECT_FALSE(harness.session.native_tools().blocks_modeling_content_mutation());
  EXPECT_FALSE(harness.session.native_tools().blocks_document_mutation(harness.objects[0].entity));
  EXPECT_FALSE(harness.session.native_tools().active_commit_metadata().has_value());
  for (const auto pane : {ViewportPaneId::Perspective, ViewportPaneId::Top,
                         ViewportPaneId::Front, ViewportPaneId::Side}) {
    EXPECT_EQ(harness.editor.viewports().selected_tool(pane), ViewportToolId::Align);
    EXPECT_TRUE(harness.editor.tools().pane_owns_executing_tool(pane));
    ASSERT_TRUE(harness.editor.viewports().focus_pane(pane).ok());
    ASSERT_TRUE(harness.editor.tools().acquire_pane_tool(pane).ok());
    ASSERT_TRUE(harness.editor.tools().activate(ViewportToolId::Align).ok());
    EXPECT_TRUE(harness.session.align_tool().active());
    EXPECT_EQ(harness.session.history().undo_count(), 1u);
    EXPECT_EQ(harness.editor.tools().active_settings_presentation_origin().serial, serial);
    EXPECT_EQ(harness.editor.tools().active_settings_presentation_origin().pane,
              ViewportPaneId::Perspective);
    expect_same_transforms(harness, after);
  }
  ASSERT_TRUE(harness.session.native_tools().settle_transient_state().ok);
  EXPECT_TRUE(harness.session.align_tool().active());
  EXPECT_EQ(harness.session.history().undo_count(), 1u);
  ASSERT_TRUE(harness.editor.tools().activate(ViewportToolId::Move).ok());
  EXPECT_FALSE(harness.session.align_tool().active());
  EXPECT_EQ(harness.session.history().undo_count(), 1u);
  expect_same_transforms(harness, after);
}

TEST(AlignToolTests, InvalidIncomingTargetsDoNotSettleAnotherNativeOperation) {
  AlignHarness harness;
  ASSERT_NO_FATAL_FAILURE(harness.seed());
  ASSERT_TRUE(harness.editor.tools().activate(ViewportToolId::LinearArray).ok());
  ASSERT_TRUE(harness.session.scene().set_locked(harness.objects[1].entity, true));
  const auto before = harness.transforms();
  const auto history_count = harness.session.history().undo_count();
  const auto rejected = harness.editor.tools().activate(ViewportToolId::Align);
  EXPECT_FALSE(rejected.ok());
  EXPECT_FALSE(harness.session.align_tool().active());
  EXPECT_TRUE(harness.session.native_tools().pending("linear_array"));
  EXPECT_EQ(harness.session.history().undo_count(), history_count);
  expect_same_transforms(harness, before);
}

TEST(AlignToolTests, SnappingChangesOnlyTheNextActionAndSurvivesSceneReplacement) {
  AlignHarness harness;
  ASSERT_NO_FATAL_FAILURE(harness.seed());
  auto &align = harness.session.align_tool();
  EXPECT_FALSE(align.settings().use_snapping);
  ASSERT_TRUE(harness.editor.tools().activate(ViewportToolId::Align).ok());
  ASSERT_TRUE(align.set_distribution(MeshArrangementAxis::X).ok);
  const auto free = harness.transforms();
  const auto scene_revision = harness.session.scene().revision();
  ASSERT_TRUE(align.set_use_snapping(true).ok);
  expect_same_transforms(harness, free);
  EXPECT_EQ(harness.session.scene().revision(), scene_revision);
  EXPECT_EQ(harness.session.history().undo_count(), 1u);
  EXPECT_FALSE(align.snap_grid_size().has_value());
  EXPECT_TRUE(align.warning_message().empty());
  ASSERT_TRUE(harness.editor.history().undo().ok());
  ASSERT_TRUE(harness.editor.viewports().set_grid_preset(10).ok());
  ASSERT_TRUE(align.set_distribution(MeshArrangementAxis::X).ok);
  ASSERT_TRUE(align.snap_grid_size().has_value());
  EXPECT_DOUBLE_EQ(*align.snap_grid_size(), 1.0);
  expect_same_transforms(harness, free);
  ASSERT_TRUE(harness.editor.tools().activate(ViewportToolId::Move).ok());
  const auto before_reactivation = harness.transforms();
  ASSERT_TRUE(harness.editor.tools().activate(ViewportToolId::Align).ok());
  expect_same_transforms(harness, before_reactivation);
  EXPECT_EQ(harness.session.history().undo_count(), 1u);
  harness.session.replace_document(SceneContentDocument{});
  EXPECT_TRUE(harness.session.align_tool().settings().use_snapping);
  EXPECT_FALSE(harness.session.align_tool().active());

  SceneSessionService sessions;
  auto first = sessions.create_session();
  ASSERT_TRUE(first->align_tool().set_use_snapping(true).ok);
  auto second = sessions.create_session();
  EXPECT_TRUE(second->align_tool().settings().use_snapping);
  const auto revision = second->align_tool().settings_revision();
  ASSERT_TRUE(first->align_tool().reset_settings().ok);
  EXPECT_FALSE(second->align_tool().settings().use_snapping);
  EXPECT_GT(second->align_tool().settings_revision(), revision);
}

TEST(AlignToolTests, SnappingFallbackPreservesEqualSpacingAndTheNextActionClearsWarning) {
  AlignHarness harness;
  ASSERT_NO_FATAL_FAILURE(harness.seed());
  ASSERT_TRUE(harness.editor.scene().set_transform(
      harness.objects[1].entity, {.position = {5.01f, 9, 7}}).ok());
  harness.session.history().clear();
  harness.session.mark_clean();
  ASSERT_TRUE(harness.editor.tools().activate(ViewportToolId::Align).ok());
  auto &align = harness.session.align_tool();
  ASSERT_TRUE(align.set_use_snapping(true).ok);
  EXPECT_TRUE(align.warning_message().empty());
  const auto result = align.set_distribution(MeshArrangementAxis::X);
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_TRUE(result.changed);
  EXPECT_TRUE(align.grid_alignment_unavailable());
  EXPECT_FALSE(align.snap_grid_size().has_value());
  EXPECT_EQ(result.message, mesh_arrangement_grid_alignment_warning());
  EXPECT_NEAR(harness.bounds(1).min.x - harness.bounds(0).max.x,
              harness.bounds(2).min.x - harness.bounds(1).max.x, kAlignEpsilon);
  EXPECT_EQ(harness.session.history().undo_count(), 1u);
  EXPECT_TRUE(harness.session.dirty());
  ASSERT_TRUE(harness.editor.history().undo().ok());
  ASSERT_TRUE(align.set_alignment(MeshArrangementAxis::Y, MeshAlignmentMode::Maximum).ok);
  EXPECT_TRUE(align.warning_message().empty());
  EXPECT_FALSE(align.grid_alignment_unavailable());
  EXPECT_TRUE(align.active());
}

TEST(AlignToolTests, ContextRetirementClearsSnappingResultAndRetainsFutureDefaults) {
  AlignHarness harness;
  ASSERT_NO_FATAL_FAILURE(harness.seed());
  ASSERT_TRUE(harness.editor.scene().set_transform(
      harness.objects[1].entity, {.position = {5.01f, 9, 7}}).ok());
  harness.session.history().clear();
  ASSERT_TRUE(harness.editor.tools().activate(ViewportToolId::Align).ok());
  auto &align = harness.session.align_tool();
  ASSERT_TRUE(align.set_use_snapping(true).ok);
  ASSERT_TRUE(align.set_distribution(MeshArrangementAxis::X).ok);
  ASSERT_FALSE(align.warning_message().empty());
  ASSERT_TRUE(align.grid_alignment_unavailable());
  const auto committed = harness.transforms();
  ASSERT_TRUE(harness.editor.mode().set(QuaderEditorMode::Edit).ok());
  ASSERT_TRUE(harness.editor.selection().set_mode(QuaderSelectionMode::Face).ok());
  const auto history_count = harness.session.history().undo_count();
  EXPECT_TRUE(align.warning_message().empty());
  EXPECT_FALSE(align.grid_alignment_unavailable());
  EXPECT_FALSE(align.snap_grid_size().has_value());
  EXPECT_FALSE(align.active());
  EXPECT_TRUE(align.settings().use_snapping);
  EXPECT_EQ(harness.session.history().undo_count(), history_count);
  expect_same_transforms(harness, committed);
  EXPECT_TRUE(harness.editor.tools().active_settings().tool_id.empty());
}

TEST(AlignToolTests, NativeGroupModeUsesCurrentAggregateHierarchyBounds) {
  AlignHarness harness;
  ASSERT_NO_FATAL_FAILURE(harness.seed());
  const auto first_group = harness.editor.scene().create_empty();
  const auto second_group = harness.editor.scene().create_empty();
  ASSERT_TRUE(first_group.ok());
  ASSERT_TRUE(second_group.ok());
  ASSERT_TRUE(harness.editor.scene().reparent(
      harness.objects[0].entity, first_group.value().id, true).ok());
  ASSERT_TRUE(harness.editor.scene().reparent(
      harness.objects[1].entity, second_group.value().id, true).ok());
  ASSERT_TRUE(harness.editor.selection().set_mode(QuaderSelectionMode::Group).ok());
  ASSERT_TRUE(harness.editor.selection().select_objects(
      {first_group.value().id, second_group.value().id}, second_group.value().id).ok());
  harness.session.history().clear();
  ASSERT_TRUE(harness.editor.tools().activate(ViewportToolId::Align).ok());
  ASSERT_TRUE(harness.session.align_tool().set_alignment(
      MeshArrangementAxis::X, MeshAlignmentMode::Minimum).ok);
  EXPECT_NEAR(harness.bounds(0).min.x, 1.0f, kAlignEpsilon);
  EXPECT_NEAR(harness.bounds(1).min.x, 1.0f, kAlignEpsilon);
  EXPECT_NEAR(harness.bounds(2).min.x, 21.0f, kAlignEpsilon);
  EXPECT_EQ(harness.session.history().undo_count(), 1u);
  ASSERT_TRUE(harness.editor.history().undo().ok());
  EXPECT_TRUE(harness.session.align_tool().active());
  EXPECT_NEAR(harness.bounds(1).min.x, 4.0f, kAlignEpsilon);
}

TEST(AlignToolTests, MixedInstanceMeshAndLightActionsKeepInstanceDefinitionsUnchanged) {
  AlignHarness harness;
  ASSERT_NO_FATAL_FAILURE(harness.seed());
  ASSERT_TRUE(harness.editor.selection().select_object(harness.objects[0]).ok());
  ASSERT_TRUE(harness.editor.modeling().create_instance_from_selection().ok());
  const auto instance = harness.session.scene().primary_selection();
  ASSERT_NE(harness.session.scene().instance_occurrence(instance), nullptr);
  const auto occurrence = *harness.session.scene().instance_occurrence(instance);
  ASSERT_TRUE(harness.editor.scene().set_transform(instance, {.position = {5, 0, 0}}).ok());
  const auto light = harness.editor.scene().create_point_light(
      {.name = "Align light", .transform = {.position = {14, 0, 0}}});
  ASSERT_TRUE(light.ok()) << light.error_message();
  ASSERT_TRUE(harness.editor.selection().select_objects(
      {harness.objects[2].entity, instance, light.value().id}, light.value().id).ok());
  harness.session.history().clear();
  const auto instance_before = *harness.session.scene().transform(instance);
  const auto light_before = *harness.session.scene().transform(light.value().id);
  const auto mesh_before = *harness.session.scene().transform(harness.objects[2].entity);
  const auto assets_revision = harness.session.assets().revision();
  ASSERT_TRUE(harness.editor.tools().activate(ViewportToolId::Align).ok());
  ASSERT_TRUE(harness.session.align_tool().set_alignment(
      MeshArrangementAxis::Y, MeshAlignmentMode::Center).ok);
  EXPECT_EQ(harness.session.history().undo_count(), 1u);
  EXPECT_EQ(*harness.session.scene().instance_occurrence(instance), occurrence);
  EXPECT_EQ(harness.session.assets().revision(), assets_revision);
  const auto instance_bounds = entity_world_bounds(
      harness.session.scene(), harness.session.assets(), instance);
  ASSERT_TRUE(instance_bounds.has_value());
  const auto mesh_bounds = harness.bounds(2);
  EXPECT_NEAR(bounds_center(*instance_bounds).y, bounds_center(mesh_bounds).y, kAlignEpsilon);
  const auto light_bounds = entity_world_bounds(
      harness.session.scene(), harness.session.assets(), light.value().id);
  ASSERT_TRUE(light_bounds.has_value());
  EXPECT_NEAR(bounds_center(*light_bounds).y, bounds_center(mesh_bounds).y, kAlignEpsilon);
  ASSERT_TRUE(harness.editor.history().undo().ok());
  EXPECT_TRUE(document_command_same_transform(
      *harness.session.scene().transform(instance), instance_before));
  EXPECT_TRUE(document_command_same_transform(
      *harness.session.scene().transform(light.value().id), light_before));
  EXPECT_TRUE(document_command_same_transform(
      *harness.session.scene().transform(harness.objects[2].entity), mesh_before));
}

/// Queries the real semantic provider only after the retained outer stamp changes.
class AlignOverlayProvider final : public ViewportOverlaySelectionFrameProvider {
public:
  explicit AlignOverlayProvider(SceneSession &session) : session_(session) {}
  ViewportOverlayFrame build_selection_overlay_frame() const override {
    ++builds;
    return session_.modeling().selection_overlay_render_frame();
  }
  mutable std::uint64_t builds = 0;

private:
  SceneSession &session_;
};

/// Records both styled streams handed to the native renderer boundary.
class AlignOverlayTarget final : public ViewportOverlaySubmissionTarget {
public:
  ViewportOverlayCameraUpdate update_overlay_cameras(
      std::size_t, std::uint8_t, ViewportOverlayCamera) override {
    return {true, true};
  }
  void submit_persistent_overlay(std::size_t, std::uint8_t,
      ViewportOverlayFrame frame, ViewportOverlayCamera) override {
    persistent = std::move(frame);
    ++submissions;
  }
  void submit_dynamic_overlay(std::size_t, std::uint8_t,
      ViewportOverlayFrame frame, ViewportOverlayCamera) override {
    dynamic = std::move(frame);
    ++submissions;
  }
  ViewportOverlayFrame persistent;
  ViewportOverlayFrame dynamic;
  std::uint64_t submissions = 0;
};

TEST(AlignToolTests, RetainedSubmissionMovesOutlinesSelectionAndSceneDepthWithEveryAction) {
  AlignHarness harness;
  ASSERT_NO_FATAL_FAILURE(harness.seed());
  AlignOverlayProvider provider(harness.session);
  AlignOverlayTarget target;
  RenderOverlaySnapshotCache snapshots;
  ViewportOverlaySubmissionCoordinator coordinator;
  const auto settings = ViewportSettings::defaults();
  const auto before = harness.transforms();
  const auto asset_revision = harness.session.assets().revision();
  const auto selection_revision = harness.session.modeling().selection_revision();
  const auto modeling_revision = harness.session.modeling().content_revision();
  const auto submit = [&] {
    auto &modeling = harness.session.modeling();
    const auto updated = snapshots.update_persistent(harness.session.scene(),
        harness.session.assets(), true, 0u, {}, {}, nullptr, true, {}, {}, true);
    const auto &scene = snapshots.selection_snapshot(
        harness.session.scene(), harness.session.assets());
    return coordinator.submit({
        .viewport_index = 0u,
        .layer_mask = 1u,
        .camera = {.eye = {12, 9, 40}, .viewport_size = {800, 600},
                   .projection = ViewportOverlayProjection::Orthographic,
                   .orthographic_size = 40},
        .selection_source = {
            .source_identity = reinterpret_cast<std::uintptr_t>(&modeling),
            .content_revision = modeling.content_revision(),
            .selection_revision = modeling.selection_revision(),
            .scene_revision = harness.session.scene().revision(),
            .asset_revision = harness.session.assets().revision(),
            .overlay_signature = modeling.selection_overlay_signature(),
            .selection_context = ViewportOverlaySelectionContext::Object,
        },
        .selection_frame_provider = &provider,
        .scene_overlay_snapshot = &scene,
        .selected_mesh_outline_snapshot = updated.selected_object,
        .scene_selection_mode = RenderOverlaySelectionMode::Object,
        .style_selection_context = ViewportOverlaySelectionContext::Object,
        .overlays_visible = true,
        .scene_overlay_requested = true,
        .selection_dimensions_visible = false,
    }, settings, target);
  };
  const auto assert_visible = [&] {
    const auto main_scene = build_render_scene_snapshot(
        harness.session.scene(), harness.session.assets());
    const auto *selected = snapshots.selected_object_snapshot();
    ASSERT_NE(selected, nullptr);
    ASSERT_EQ(selected->meshes.size(), 3u);
    for (const auto &object : harness.objects) {
      const auto world = harness.session.scene().world_transform_matrix(object.entity);
      std::size_t render_occurrences = 0u;
      for (const auto &mesh : main_scene.meshes) {
        if (mesh.entity_id == object.entity) {
          ++render_occurrences;
          EXPECT_EQ(mesh.world_transform.values, world.values);
        }
      }
      // Compatible ordinary meshes share the renderer's instance batches.
      for (const auto &primitive : main_scene.instance_primitives) {
        for (const auto &occurrence : primitive.occurrences) {
          if (occurrence.entity_id == object.entity) {
            ++render_occurrences;
            EXPECT_EQ(occurrence.world_transform.values, world.values);
          }
        }
      }
      EXPECT_GT(render_occurrences, 0u);
      const auto selection = std::ranges::find(selected->meshes, object.entity,
                                                &RenderOverlayMeshSnapshot::entity);
      ASSERT_NE(selection, selected->meshes.end());
      EXPECT_EQ(selection->world_transform.values, world.values);
    }
    std::size_t outlines = 0u;
    for (const auto &line : target.dynamic.lines) {
      if (line.source != ViewportOverlaySource::MeshOutline) continue;
      ++outlines;
      const auto belongs_to_current_bounds = [&](QVec3 position) {
        for (std::size_t index = 0; index < 3u; ++index) {
          const auto bounds = harness.bounds(index);
          if (position.x >= bounds.min.x - kAlignEpsilon &&
              position.x <= bounds.max.x + kAlignEpsilon &&
              position.y >= bounds.min.y - kAlignEpsilon &&
              position.y <= bounds.max.y + kAlignEpsilon &&
              position.z >= bounds.min.z - kAlignEpsilon &&
              position.z <= bounds.max.z + kAlignEpsilon) return true;
        }
        return false;
      };
      EXPECT_TRUE(belongs_to_current_bounds(line.start));
      EXPECT_TRUE(belongs_to_current_bounds(line.end));
    }
    EXPECT_GT(outlines, 0u);
    const auto modeling_bounds = harness.session.modeling().selection_bounds();
    ASSERT_TRUE(modeling_bounds.has_value());
    SceneBounds expected = harness.bounds(0);
    for (std::size_t index = 1; index < 3u; ++index) {
      include_point(expected, harness.bounds(index).min);
      include_point(expected, harness.bounds(index).max);
    }
    EXPECT_NEAR(modeling_bounds->min.x, expected.min.x, kAlignEpsilon);
    EXPECT_NEAR(modeling_bounds->max.x, expected.max.x, kAlignEpsilon);
  };
  ASSERT_TRUE(submit().persistent_submitted);
  ASSERT_NO_FATAL_FAILURE(assert_visible());
  ASSERT_TRUE(harness.editor.tools().activate(ViewportToolId::Align).ok());
  constexpr std::array<std::pair<MeshArrangementAxis, MeshAlignmentMode>, 3> kActions{{
      {MeshArrangementAxis::X, MeshAlignmentMode::Minimum},
      {MeshArrangementAxis::Y, MeshAlignmentMode::Maximum},
      {MeshArrangementAxis::Z, MeshAlignmentMode::Center},
  }};
  for (const auto [axis, mode] : kActions) {
    ASSERT_TRUE(harness.session.align_tool().set_alignment(axis, mode).ok);
    ASSERT_TRUE(submit().persistent_submitted);
    ASSERT_NO_FATAL_FAILURE(assert_visible());
    EXPECT_EQ(harness.session.assets().revision(), asset_revision);
    EXPECT_EQ(harness.session.modeling().content_revision(), modeling_revision);
    EXPECT_EQ(harness.session.modeling().selection_revision(), selection_revision);
    const auto builds = provider.builds;
    const auto submissions = target.submissions;
    const auto bounds_builds = snapshots.stats().selection_world_bounds_build_count;
    const auto topology_builds = coordinator.work_counters().mesh_outline_topology_build_count;
    for (int frame = 0; frame < 3; ++frame) {
      const auto unchanged = submit();
      EXPECT_FALSE(unchanged.selection_source_refreshed);
      EXPECT_FALSE(unchanged.persistent_submitted);
      EXPECT_FALSE(unchanged.dynamic_submitted);
    }
    EXPECT_EQ(provider.builds, builds);
    EXPECT_EQ(target.submissions, submissions);
    EXPECT_EQ(snapshots.stats().selection_world_bounds_build_count, bounds_builds);
    EXPECT_EQ(coordinator.work_counters().mesh_outline_topology_build_count, topology_builds);
  }
  EXPECT_EQ(harness.session.history().undo_count(), 3u);
  ASSERT_TRUE(harness.editor.history().undo().ok());
  EXPECT_TRUE(harness.session.align_tool().active());
  ASSERT_TRUE(submit().persistent_submitted);
  ASSERT_NO_FATAL_FAILURE(assert_visible());
  ASSERT_TRUE(harness.editor.history().redo().ok());
  ASSERT_TRUE(submit().persistent_submitted);
  ASSERT_NO_FATAL_FAILURE(assert_visible());
  const auto after = harness.transforms();
  const auto scene_revision = harness.session.scene().revision();
  ASSERT_TRUE(harness.session.align_tool().set_use_snapping(true).ok);
  EXPECT_EQ(harness.session.scene().revision(), scene_revision);
  expect_same_transforms(harness, after);
  const auto unchanged = submit();
  EXPECT_FALSE(unchanged.persistent_submitted);
  EXPECT_FALSE(unchanged.dynamic_submitted);
  ASSERT_TRUE(harness.editor.tools().activate(ViewportToolId::Move).ok());
  expect_same_transforms(harness, after);
  EXPECT_EQ(harness.session.history().undo_count(), 3u);
  (void)submit();
  ASSERT_NO_FATAL_FAILURE(assert_visible());
  for (int action = 0; action < 3; ++action) {
    ASSERT_TRUE(harness.editor.history().undo().ok());
  }
  expect_same_transforms(harness, before);
  ASSERT_TRUE(submit().persistent_submitted);
  ASSERT_NO_FATAL_FAILURE(assert_visible());
}

} // namespace
} // namespace quader
