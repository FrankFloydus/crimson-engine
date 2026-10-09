////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_command_ids.hpp"
#include "api/quader_editor_api.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/modes/editor_mode_service.hpp"
#include "editor/selection/editor_selection_service.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/transform/scene_transform_drag_service.hpp"

#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

namespace quader {
namespace {

class EditorModeServiceTests : public testing::Test {
protected:
  EditorModeServiceTests()
      : editor_(QuaderApiSessionInternalAccess::from_scene_session(session_)) {}

  QuaderModelingObjectRef create_box(std::string name = "Box") {
    const auto result = editor_.modeling().create_box(
        {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        std::move(name));
    EXPECT_TRUE(result.ok()) << result.error_message();
    return result.ok() ? result.value() : QuaderModelingObjectRef{};
  }

  SceneSession session_;
  QuaderEditorApi editor_;
};

/** Keeps project persistence and recent-project metadata inside a temporary profile. */
class ScopedModeProjectDirectory final {
public:
  ScopedModeProjectDirectory()
      : had_profile_(qEnvironmentVariableIsSet(kProfileVariable)),
        previous_profile_(qgetenv(kProfileVariable)) {
    if (directory_.isValid()) {
      active_ = qputenv(kProfileVariable, directory_.path().toUtf8());
    }
  }

  ~ScopedModeProjectDirectory() {
    if (active_) {
      if (had_profile_) {
        (void)qputenv(kProfileVariable, previous_profile_);
      } else {
        (void)qunsetenv(kProfileVariable);
      }
    }
  }

  [[nodiscard]] bool valid() const { return directory_.isValid() && active_; }
  [[nodiscard]] std::filesystem::path project_root() const {
    return std::filesystem::path(directory_.path().toStdWString()) / "Project";
  }

private:
#ifdef _WIN32
  static constexpr const char *kProfileVariable = "LOCALAPPDATA";
#else
  static constexpr const char *kProfileVariable = "XDG_CONFIG_HOME";
#endif
  QTemporaryDir directory_;
  bool had_profile_ = false;
  QByteArray previous_profile_;
  bool active_ = false;
};

TEST_F(EditorModeServiceTests, InspectorTransformsUseFixedEditParticipantsWithoutComponents) {
  const auto outside = create_box("Outside");
  const auto first = create_box("First");
  const auto second = create_box("Second");
  ASSERT_TRUE(editor_.scene().select({first.entity, second.entity}, first.entity).ok());
  ASSERT_TRUE(editor_.mode().set(QuaderEditorMode::Edit).ok());
  EXPECT_EQ(editor_.selection().summary().selected_vertex_count, 0u);
  const auto context = editor_.scene().inspector_object_edit_context();
  ASSERT_EQ(context.entities.size(), 2u);
  const QuaderTransform before;
  QuaderTransform moved;
  moved.location = {4.0f, 2.0f, -1.0f};
  EXPECT_FALSE(editor_.scene().set_transform(first.entity, moved).ok());
  const std::array outside_edit{QuaderSceneTransformEdit{
      .entity = outside.entity, .before = before, .after = moved}};
  EXPECT_FALSE(editor_.scene().set_inspector_transforms(context, outside_edit).ok());
  const std::array edits{
      QuaderSceneTransformEdit{.entity = first.entity, .before = before, .after = moved},
      QuaderSceneTransformEdit{.entity = second.entity, .before = before, .after = moved}};
  ASSERT_TRUE(editor_.scene().set_inspector_transforms(context, edits).ok());
  EXPECT_FLOAT_EQ(session_.scene().transform(first.entity)->location.x, 4.0f);
  EXPECT_FLOAT_EQ(session_.scene().transform(second.entity)->location.x, 4.0f);
  EXPECT_FLOAT_EQ(session_.scene().transform(outside.entity)->location.x, 0.0f);
  EXPECT_EQ(editor_.selection().summary().selected_vertex_count, 0u);
  ASSERT_TRUE(editor_.commands().execute(quader_command::kEditUndo).ok());
  EXPECT_FLOAT_EQ(session_.scene().transform(first.entity)->location.x, 0.0f);
  ASSERT_TRUE(editor_.commands().execute(quader_command::kEditRedo).ok());
  EXPECT_FLOAT_EQ(session_.scene().transform(second.entity)->location.x, 4.0f);
  EXPECT_EQ(session_.editor_mode().snapshot().participants.size(), 2u);
}

TEST_F(EditorModeServiceTests, InspectorPreviewRetainsInvisibleUnselectableEditParticipants) {
  const auto box = create_box();
  ASSERT_TRUE(editor_.mode().set(QuaderEditorMode::Edit).ok());
  ASSERT_TRUE(editor_.scene().set_visible(box.entity, false).ok());
  ASSERT_TRUE(editor_.scene().set_selectable(box.entity, false).ok());
  ASSERT_TRUE(session_.editor_mode().contains(box.entity));
  SceneTransformDragService preview;
  ASSERT_TRUE(preview.begin_inspector_transform_preview(
      editor_, session_.scene(), session_.assets(), box.entity,
      SceneTextureTransformKind::Translation, "Inspector Location"));
  ASSERT_TRUE(preview.preview_selected_to_primary_location(
      session_.scene(), box.entity, {3.0f, 0.0f, 0.0f}).ok);
  EXPECT_FLOAT_EQ(session_.scene().transform(box.entity)->location.x, 3.0f);
  ASSERT_TRUE(preview.cancel().ok);
  EXPECT_FLOAT_EQ(session_.scene().transform(box.entity)->location.x, 0.0f);
  ASSERT_TRUE(preview.begin_inspector_transform_preview(
      editor_, session_.scene(), session_.assets(), box.entity,
      SceneTextureTransformKind::Translation, "Inspector Location"));
  ASSERT_TRUE(preview.preview_selected_to_primary_location(
      session_.scene(), box.entity, {5.0f, 0.0f, 0.0f}).ok);
  ASSERT_TRUE(preview.commit(editor_).ok());
  ASSERT_TRUE(editor_.commands().execute(quader_command::kEditUndo).ok());
  EXPECT_FLOAT_EQ(session_.scene().transform(box.entity)->location.x, 0.0f);
  ASSERT_TRUE(editor_.commands().execute(quader_command::kEditRedo).ok());
  EXPECT_FLOAT_EQ(session_.scene().transform(box.entity)->location.x, 5.0f);
  EXPECT_FALSE(session_.scene().selectable(box.entity)->selectable);
  EXPECT_FALSE(session_.scene().visibility(box.entity)->visible);
  EXPECT_EQ(editor_.selection().summary().selected_vertex_count, 0u);
  SceneTransformPreviewTransaction generic(session_.scene(), {box.entity});
  EXPECT_TRUE(generic.empty());
}

TEST_F(EditorModeServiceTests, SelectabilityPublicationFailureRestoresAuthoredStateAndSelection) {
  const auto first = create_box("First");
  const auto second = create_box("Second");
  ASSERT_TRUE(editor_.scene().select({first.entity, second.entity}, first.entity).ok());
  const auto selected = session_.scene().selected_entities();
  const auto history_revision = session_.history().revision();
  session_.document_commands().set_selection_publication([] {
    return command_invalid_result("Injected selection publication failure");
  });
  const auto result = editor_.scene().set_selectable(first.entity, false);
  EXPECT_FALSE(result.ok());
  EXPECT_TRUE(session_.scene().selectable(first.entity)->selectable);
  EXPECT_EQ(session_.scene().selected_entities(), selected);
  EXPECT_EQ(session_.scene().primary_selection(), first.entity);
  EXPECT_EQ(session_.history().revision(), history_revision);
}

TEST_F(EditorModeServiceTests, InspectorRotationAndScaleUseAllFixedParticipants) {
  const auto first = create_box("First");
  const auto second = create_box("Second");
  QuaderTransform offset;
  offset.location = {4.0f, 0.0f, 0.0f};
  ASSERT_TRUE(editor_.scene().set_transform(second.entity, offset).ok());
  ASSERT_TRUE(editor_.scene().select({first.entity, second.entity}, first.entity).ok());
  ASSERT_TRUE(editor_.mode().set(QuaderEditorMode::Edit).ok());
  SceneTransformDragService preview;
  ASSERT_TRUE(preview.begin_inspector_transform_preview(
      editor_, session_.scene(), session_.assets(), first.entity,
      SceneTextureTransformKind::Rotation, "Inspector Rotation"));
  ASSERT_TRUE(preview.preview_selected_to_primary_euler(
      session_.scene(), first.entity, {0.0f, 0.0f, 1.57079632679f}).ok);
  EXPECT_NEAR(session_.scene().transform(first.entity)->location.x, 2.0f, 0.0001f);
  EXPECT_NEAR(session_.scene().transform(first.entity)->location.y, -2.0f, 0.0001f);
  EXPECT_NEAR(session_.scene().transform(second.entity)->location.x, 2.0f, 0.0001f);
  EXPECT_NEAR(session_.scene().transform(second.entity)->location.y, 2.0f, 0.0001f);
  ASSERT_TRUE(preview.cancel().ok);
  ASSERT_TRUE(preview.begin_inspector_transform_preview(
      editor_, session_.scene(), session_.assets(), first.entity,
      SceneTextureTransformKind::Scale, "Inspector Scale"));
  ASSERT_TRUE(preview.preview_selected_to_primary_scale(
      session_.scene(), first.entity, {2.0f, 2.0f, 2.0f}).ok);
  EXPECT_FLOAT_EQ(session_.scene().transform(first.entity)->location.x, -2.0f);
  EXPECT_FLOAT_EQ(session_.scene().transform(second.entity)->location.x, 6.0f);
  ASSERT_TRUE(preview.commit(editor_).ok());
  EXPECT_EQ(editor_.selection().summary().selected_vertex_count, 0u);
  ASSERT_TRUE(editor_.commands().execute(quader_command::kEditUndo).ok());
  EXPECT_FLOAT_EQ(session_.scene().transform(second.entity)->location.x, 4.0f);
}

TEST_F(EditorModeServiceTests, SelectableHistoryPreservesIndependentEligibilityGate) {
  for (const bool eligible : {false, true}) {
    const auto box = create_box();
    ASSERT_TRUE(editor_.scene().select(std::span<const EntityId>{}).ok());
    ASSERT_TRUE(session_.scene().set_selectability(box.entity, eligible));
    const auto unchanged = editor_.scene().set_selectable(box.entity, true);
    EXPECT_TRUE(unchanged.ok());
    EXPECT_FALSE(unchanged.changed());
    ASSERT_TRUE(editor_.scene().set_selectable(box.entity, false).ok());
    auto snapshot = editor_.query().entity(box.entity);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_FALSE(snapshot->selectable);
    EXPECT_EQ(snapshot->selection_eligible, eligible);
    ASSERT_TRUE(editor_.commands().execute(quader_command::kEditUndo).ok());
    snapshot = editor_.query().entity(box.entity);
    EXPECT_TRUE(snapshot->selectable);
    EXPECT_EQ(snapshot->selection_eligible, eligible);
    ASSERT_TRUE(editor_.commands().execute(quader_command::kEditRedo).ok());
    snapshot = editor_.query().entity(box.entity);
    EXPECT_FALSE(snapshot->selectable);
    EXPECT_EQ(snapshot->selection_eligible, eligible);
  }
}

TEST_F(EditorModeServiceTests, InspectorObjectDraftSettlesCapturedTargetsAfterSelectionChanges) {
  const auto first = create_box("First");
  const auto second = create_box("Second");
  ASSERT_TRUE(editor_.scene().select(first.entity).ok());
  SceneTransformDragService preview;
  ASSERT_TRUE(preview.begin_inspector_transform_preview(
      editor_, session_.scene(), session_.assets(), first.entity,
      SceneTextureTransformKind::Translation, "Inspector Location"));
  ASSERT_TRUE(preview.preview_selected_to_primary_location(
      session_.scene(), first.entity, {3.0f, 0.0f, 0.0f}).ok);
  ASSERT_TRUE(editor_.scene().select(second.entity).ok());
  EXPECT_FALSE(preview.preview_selected_to_primary_location(
      session_.scene(), first.entity, {8.0f, 0.0f, 0.0f}).ok);
  ASSERT_TRUE(preview.commit(editor_).ok());
  EXPECT_FLOAT_EQ(session_.scene().transform(first.entity)->location.x, 3.0f);
  EXPECT_FLOAT_EQ(session_.scene().transform(second.entity)->location.x, 0.0f);
  EXPECT_EQ(session_.scene().primary_selection(), second.entity);
}

TEST_F(EditorModeServiceTests, InspectorContextUsesIdentityAndFixedParticipantsBeyondSelectionRevisions) {
  const auto first = create_box("First");
  const auto second = create_box("Second");
  ASSERT_TRUE(editor_.scene().select({first.entity, second.entity}, first.entity).ok());
  ASSERT_TRUE(editor_.mode().set(QuaderEditorMode::Edit).ok());
  const auto context = editor_.scene().inspector_object_edit_context();
  ASSERT_TRUE(editor_.selection().select_vertex(first, 1).ok());
  for (int sample = 0; sample < 10; ++sample) {
    EXPECT_TRUE(editor_.scene().inspector_object_edit_context_valid(context));
  }
  const auto second_identity = session_.scene().uuid(second.entity);
  ASSERT_TRUE(session_.scene().set_entity_uuid(second.entity, "8fad8c01-0161-47ac-9d2c-0371da41a6fd"));
  EXPECT_FALSE(editor_.scene().inspector_object_edit_context_valid(context));
  ASSERT_TRUE(session_.scene().set_entity_uuid(second.entity, second_identity));
  EXPECT_TRUE(editor_.scene().inspector_object_edit_context_valid(context));
}

TEST_F(EditorModeServiceTests, InspectorContextRejectsReplacedDocument) {
  const auto box = create_box();
  ASSERT_TRUE(editor_.mode().set(QuaderEditorMode::Edit).ok());
  const auto context = editor_.scene().inspector_object_edit_context();
  session_.replace_document(SceneContentDocument{});
  EXPECT_FALSE(editor_.scene().inspector_object_edit_context_valid(context));
  const std::array edits{QuaderSceneTransformEdit{
      .entity = box.entity, .before = {}, .after = {.location = {3.0f, 0.0f, 0.0f}}}};
  EXPECT_FALSE(editor_.scene().set_inspector_transforms(context, edits).ok());
}

TEST_F(EditorModeServiceTests, RetiredInspectorPreviewNeverRestoresIntoAReplacementEntity) {
  for (int finish_kind = 0; finish_kind < 3; ++finish_kind) {
    session_.replace_document(SceneContentDocument{});
    const auto original = create_box("Original");
    ASSERT_TRUE(editor_.mode().set(QuaderEditorMode::Edit).ok());
    auto preview = std::make_unique<SceneTransformDragService>();
    ASSERT_TRUE(preview->begin_inspector_transform_preview(
        editor_, session_.scene(), session_.assets(), original.entity,
        SceneTextureTransformKind::Translation, "Inspector Location"));
    ASSERT_TRUE(preview->preview_selected_to_primary_location(
        session_.scene(), original.entity, {3.0f, 0.0f, 0.0f}).ok);
    session_.replace_document(SceneContentDocument{});
    const auto replacement = create_box("Replacement");
    ASSERT_EQ(replacement.entity, original.entity);
    QuaderTransform replacement_transform;
    replacement_transform.location = {17.0f, 5.0f, 2.0f};
    ASSERT_TRUE(editor_.scene().set_transform(replacement.entity, replacement_transform).ok());
    if (finish_kind == 0) {
      EXPECT_FALSE(preview->cancel().ok);
    } else if (finish_kind == 1) {
      preview->reset();
    } else {
      preview.reset();
    }
    EXPECT_FLOAT_EQ(session_.scene().transform(replacement.entity)->location.x, 17.0f);
    EXPECT_FLOAT_EQ(session_.scene().transform(replacement.entity)->location.y, 5.0f);
    EXPECT_FLOAT_EQ(session_.scene().transform(replacement.entity)->location.z, 2.0f);
  }
}

TEST_F(EditorModeServiceTests, InspectorContextRejectsRetiredModeGenerationAndRestoresPreview) {
  const auto box = create_box();
  ASSERT_TRUE(editor_.mode().set(QuaderEditorMode::Edit).ok());
  const auto context = editor_.scene().inspector_object_edit_context();
  SceneTransformDragService preview;
  ASSERT_TRUE(preview.begin_inspector_transform_preview(
      editor_, session_.scene(), session_.assets(), box.entity,
      SceneTextureTransformKind::Translation, "Inspector Location"));
  ASSERT_TRUE(preview.preview_selected_to_primary_location(
      session_.scene(), box.entity, {3.0f, 0.0f, 0.0f}).ok);
  ASSERT_TRUE(editor_.mode().set(QuaderEditorMode::Object).ok());
  ASSERT_TRUE(editor_.mode().set(QuaderEditorMode::Edit).ok());
  EXPECT_FALSE(editor_.scene().inspector_object_edit_context_valid(context));
  EXPECT_FALSE(preview.preview_selected_to_primary_location(
      session_.scene(), box.entity, {8.0f, 0.0f, 0.0f}).ok);
  EXPECT_FALSE(preview.commit(editor_).ok());
  ASSERT_TRUE(preview.cancel().ok);
  EXPECT_FLOAT_EQ(session_.scene().transform(box.entity)->location.x, 0.0f);
}

TEST_F(EditorModeServiceTests, NewSceneStartsInObjectWithoutEditableComponents) {
  EXPECT_EQ(editor_.mode().current(), QuaderEditorMode::Object);
  EXPECT_EQ(session_.modeling().selection_mode(), ModelingSelectionMode::Object);
  EXPECT_TRUE(session_.editor_mode().snapshot().participants.empty());
  EXPECT_FALSE(editor_.mode().can_set(QuaderEditorMode::Edit));
  EXPECT_FALSE(editor_.selection().set_mode(QuaderSelectionMode::Vertex).ok());
  EXPECT_FALSE(editor_.selection().set_mode(QuaderSelectionMode::Edge).ok());
  EXPECT_FALSE(editor_.selection().set_mode(QuaderSelectionMode::Face).ok());
  EXPECT_TRUE(editor_.selection().set_mode(QuaderSelectionMode::Group).ok());
}

TEST_F(EditorModeServiceTests, EmptyEditRequestsAreSilentWithoutSettlingOrChangingRevisions) {
  int settlement_count = 0;
  session_.editor_mode().set_transition_settlement([&] {
    ++settlement_count;
    return command_changed_result();
  });
  const auto before = session_.editor_mode().snapshot();
  const auto history_revision = session_.history().revision();
  const auto content = session_.content_revision_stamp();
  EXPECT_TRUE(editor_.commands().descriptor(quader_command::kEditorModeEdit)->enabled);
  EXPECT_TRUE(editor_.commands().descriptor(quader_command::kEditorModeToggleEdit)->enabled);
  const std::array results{
      editor_.mode().set(QuaderEditorMode::Edit),
      editor_.mode().toggle_edit(),
      editor_.commands().execute(quader_command::kEditorModeEdit),
      editor_.commands().execute(quader_command::kEditorModeToggleEdit)};
  for (const auto &result : results) {
    EXPECT_TRUE(result.ok()) << result.error_message();
    EXPECT_FALSE(result.changed());
    EXPECT_TRUE(result.error_message().empty());
  }
  EXPECT_EQ(settlement_count, 0);
  EXPECT_EQ(session_.editor_mode().snapshot(), before);
  EXPECT_EQ(session_.history().revision(), history_revision);
  EXPECT_EQ(session_.content_revision_stamp(), content);
}

TEST_F(EditorModeServiceTests, ObjectDeselectionRetainsActiveMeshForExplicitEditEntry) {
  const auto mesh = create_box("Last Active");
  const auto outside = create_box("Outside");
  ASSERT_TRUE(editor_.object(outside).set_transform(
      {.location = {5.0f, 0.0f, 0.0f}}).ok());
  const QuaderRay outside_ray{.origin = {5.0f, 0.0f, 5.0f},
                             .direction = {0.0f, 0.0f, -1.0f}};
  for (int deselection_path = 0; deselection_path < 5; ++deselection_path) {
    SCOPED_TRACE(deselection_path);
    ASSERT_TRUE(editor_.selection().select_object(mesh).ok());
    ASSERT_EQ(editor_.mode().state().active_entity.id, mesh.entity);
    const auto active_revision = editor_.mode().revision();
    const auto generation = editor_.mode().generation();
    switch (deselection_path) {
    case 0:
      ASSERT_TRUE(editor_.selection().clear().ok());
      break;
    case 1:
      ASSERT_TRUE(editor_.selection().select_object(mesh, QuaderSelectionEdit::Remove).ok());
      break;
    case 2:
      ASSERT_TRUE(editor_.selection().select_objects(std::span<const EntityId>{}).ok());
      break;
    case 3:
      ASSERT_TRUE(editor_.selection().pick_select(
          {.ray = {.origin = {20.0f, 20.0f, 20.0f},
                   .direction = {0.0f, 0.0f, 1.0f}}}).ok());
      break;
    case 4:
      ASSERT_TRUE(editor_.selection().select_object(mesh, QuaderSelectionEdit::Toggle).ok());
      break;
    }
    EXPECT_TRUE(session_.scene().selected_entities().empty());
    EXPECT_EQ(session_.scene().primary_selection(), kInvalidEntityId);
    EXPECT_EQ(editor_.selection().summary().selected_object_count, 0u);
    EXPECT_EQ(editor_.mode().state().active_entity.id, mesh.entity);
    EXPECT_EQ(editor_.mode().state().active_entity.uuid, mesh.entity_uuid);
    EXPECT_EQ(editor_.mode().revision(), active_revision);
    EXPECT_EQ(editor_.mode().generation(), generation);
    EXPECT_TRUE(editor_.mode().can_set(QuaderEditorMode::Edit));
    const auto before_hover = session_.editor_mode().snapshot();
    const auto outside_preview = editor_.selection().preview_hover(
        {.ray = outside_ray, .face_plane_hover = true});
    ASSERT_EQ(outside_preview.target.object_id, outside.modeling_object_id);
    EXPECT_EQ(outside_preview.target.kind, QuaderActiveSelectionKind::Face);
    ASSERT_TRUE(editor_.selection().hover({.ray = outside_ray}).ok());
    EXPECT_FALSE(editor_.selection().hover_face(outside, 1).ok());
    EXPECT_EQ(session_.editor_mode().snapshot(), before_hover);
    EXPECT_TRUE(session_.scene().selected_entities().empty());
    const auto history_count = session_.history().undo_count();
    const auto entered = editor_.commands().execute(quader_command::kEditorModeToggleEdit);
    ASSERT_TRUE(entered.ok()) << entered.error_message();
    EXPECT_TRUE(entered.changed());
    ASSERT_EQ(editor_.mode().state().participants.size(), 1u);
    EXPECT_EQ(editor_.mode().state().participants.front().id, mesh.entity);
    EXPECT_FALSE(session_.editor_mode().contains(outside.entity));
    EXPECT_EQ(session_.selection().summary().selected_vertex_count, 0u);
    EXPECT_EQ(session_.history().undo_count(), history_count);
    ASSERT_TRUE(editor_.mode().set(QuaderEditorMode::Object).ok());
  }
}

TEST_F(EditorModeServiceTests, NewObjectSelectionReplacesRetainedActiveWithoutChangingEditGeneration) {
  const auto first = create_box("First");
  const auto second = create_box("Second");
  ASSERT_TRUE(editor_.selection().select_object(first).ok());
  ASSERT_TRUE(editor_.selection().clear().ok());
  const auto revision = editor_.mode().revision();
  const auto generation = editor_.mode().generation();
  ASSERT_TRUE(editor_.selection().select_object(second).ok());
  EXPECT_GT(editor_.mode().revision(), revision);
  EXPECT_EQ(editor_.mode().generation(), generation);
  EXPECT_EQ(editor_.mode().state().active_entity.id, second.entity);
  EXPECT_EQ(session_.scene().selected_entities(), std::vector<EntityId>{second.entity});
  ASSERT_TRUE(editor_.selection().clear().ok());
  const auto before = session_.editor_mode().snapshot();
  ASSERT_TRUE(editor_.selection().clear().ok());
  EXPECT_EQ(session_.editor_mode().snapshot(), before);
  ASSERT_TRUE(editor_.mode().toggle_edit().ok());
  EXPECT_TRUE(session_.editor_mode().contains(second.entity));
  EXPECT_FALSE(session_.editor_mode().contains(first.entity));
}

TEST_F(EditorModeServiceTests, ObjectSelectionHistoryPreservesDeselectedActiveWithoutReselectingIt) {
  const auto first = create_box("First");
  const auto second = create_box("Second");
  ASSERT_TRUE(editor_.selection().select_object(first).ok());
  ASSERT_TRUE(editor_.selection().select_object(second).ok());
  ASSERT_TRUE(editor_.selection().clear().ok());
  const auto generation = editor_.mode().generation();
  ASSERT_TRUE(editor_.history().undo().ok());
  EXPECT_EQ(session_.scene().selected_entities(), std::vector<EntityId>{second.entity});
  EXPECT_EQ(editor_.mode().state().active_entity.id, second.entity);
  ASSERT_TRUE(editor_.history().redo().ok());
  EXPECT_TRUE(session_.scene().selected_entities().empty());
  EXPECT_EQ(editor_.selection().summary().selected_object_count, 0u);
  EXPECT_EQ(editor_.mode().state().active_entity.id, second.entity);
  EXPECT_EQ(editor_.mode().generation(), generation);
  const auto unchanged = session_.editor_mode().snapshot();
  session_.editor_mode().reconcile_scene_lifecycle();
  session_.editor_mode().reconcile_scene_lifecycle();
  EXPECT_EQ(session_.editor_mode().snapshot(), unchanged);
}

TEST_F(EditorModeServiceTests, FailedActiveFallbackEntryPreservesDeselectionAndDraft) {
  const auto mesh = create_box();
  ASSERT_TRUE(editor_.selection().clear().ok());
  int settlement_count = 0;
  session_.editor_mode().set_transition_settlement([&] {
    ++settlement_count;
    return command_invalid_result("Draft cannot be accepted.");
  });
  const auto before = session_.editor_mode().snapshot();
  const auto history_revision = session_.history().revision();
  const auto entered = editor_.commands().execute(quader_command::kEditorModeToggleEdit);
  EXPECT_FALSE(entered.ok());
  EXPECT_EQ(entered.error_message(), "Draft cannot be accepted.");
  EXPECT_EQ(settlement_count, 1);
  EXPECT_EQ(session_.editor_mode().snapshot(), before);
  EXPECT_EQ(session_.history().revision(), history_revision);
  EXPECT_EQ(editor_.mode().state().active_entity.id, mesh.entity);
  EXPECT_TRUE(session_.scene().selected_entities().empty());
}

TEST_F(EditorModeServiceTests, IneligibleRetainedActiveNeverFallsBackToAnotherMeshOrSettlesDrafts) {
  const auto eligible = create_box("Unselected Eligible Mesh");
  for (int ineligible_kind = 0; ineligible_kind < 4; ++ineligible_kind) {
    SCOPED_TRACE(ineligible_kind);
    EntityId active = kInvalidEntityId;
    if (ineligible_kind < 2) {
      const auto mesh = create_box("Ineligible Mesh");
      active = mesh.entity;
      ASSERT_TRUE(editor_.selection().select_object(mesh).ok());
      ASSERT_TRUE(editor_.selection().clear().ok());
      if (ineligible_kind == 0) {
        ASSERT_TRUE(editor_.scene().set_visible(active, false).ok());
      } else {
        ASSERT_TRUE(editor_.scene().set_selectable(active, false).ok());
      }
    } else {
      const auto created = editor_.scene().create_entity(
          {.name = "Ineligible Object",
           .kind = ineligible_kind == 2 ? QuaderEntityKind::Light : QuaderEntityKind::Empty});
      ASSERT_TRUE(created.ok());
      active = created.value().id;
      if (ineligible_kind == 3) {
        ASSERT_TRUE(editor_.scene().reparent(eligible.entity, active).ok());
      }
      ASSERT_TRUE(editor_.selection().select_object(active).ok());
      ASSERT_TRUE(editor_.selection().clear().ok());
    }
    ASSERT_EQ(editor_.mode().state().active_entity.id, active);
    EXPECT_FALSE(editor_.mode().can_set(QuaderEditorMode::Edit));
    int settlement_count = 0;
    session_.editor_mode().set_transition_settlement([&] {
      ++settlement_count;
      return command_invalid_result("An unrelated draft is pending.");
    });
    const auto before = session_.editor_mode().snapshot();
    const auto content = session_.content_revision_stamp();
    const auto history_revision = session_.history().revision();
    const auto entered = editor_.commands().execute(quader_command::kEditorModeToggleEdit);
    EXPECT_TRUE(entered.ok()) << entered.error_message();
    EXPECT_FALSE(entered.changed());
    EXPECT_EQ(settlement_count, 0);
    EXPECT_EQ(session_.editor_mode().snapshot(), before);
    EXPECT_EQ(session_.content_revision_stamp(), content);
    EXPECT_EQ(session_.history().revision(), history_revision);
    EXPECT_TRUE(session_.scene().selected_entities().empty());
    session_.editor_mode().set_transition_settlement({});
  }
}

TEST_F(EditorModeServiceTests, SelectedIneligibleObjectDoesNotUsePreviousMeshAndGroupFallbackDoesNotExpand) {
  const auto mesh = create_box();
  ASSERT_TRUE(editor_.selection().clear().ok());
  const auto group = editor_.scene().create_entity(
      {.name = "Group", .kind = QuaderEntityKind::Empty});
  ASSERT_TRUE(group.ok());
  ASSERT_TRUE(editor_.scene().reparent(mesh.entity, group.value().id).ok());
  ASSERT_TRUE(editor_.selection().select_object(group.value().id).ok());
  ASSERT_TRUE(editor_.selection().clear().ok());
  const auto inactive_group = editor_.mode().toggle_edit();
  EXPECT_TRUE(inactive_group.ok());
  EXPECT_FALSE(inactive_group.changed());
  EXPECT_EQ(editor_.mode().current(), QuaderEditorMode::Object);
  ASSERT_TRUE(editor_.selection().select_object(group.value().id).ok());
  ASSERT_TRUE(editor_.mode().toggle_edit().ok());
  ASSERT_TRUE(session_.editor_mode().contains(mesh.entity));
  ASSERT_TRUE(editor_.mode().set(QuaderEditorMode::Object).ok());
  const auto light = editor_.scene().create_entity(
      {.name = "Light", .kind = QuaderEntityKind::Light});
  ASSERT_TRUE(light.ok());
  ASSERT_TRUE(editor_.selection().select_object(light.value().id).ok());
  const auto entered = editor_.mode().toggle_edit();
  EXPECT_TRUE(entered.ok());
  EXPECT_FALSE(entered.changed());
  EXPECT_EQ(editor_.mode().current(), QuaderEditorMode::Object);
  EXPECT_EQ(session_.scene().selected_entities(), std::vector<EntityId>{light.value().id});
}

TEST_F(EditorModeServiceTests, RetainedActiveIdentityIsClearedWhenDestroyedAndOnDocumentReplacement) {
  const auto outside = create_box("Outside");
  const auto active = create_box("Active");
  ASSERT_TRUE(editor_.selection().select_object(active).ok());
  ASSERT_TRUE(editor_.selection().clear().ok());
  const auto revision = editor_.mode().revision();
  ASSERT_TRUE(editor_.scene().destroy(active.entity).ok());
  EXPECT_GT(editor_.mode().revision(), revision);
  EXPECT_EQ(editor_.mode().state().active_entity.id, kInvalidEntityId);
  EXPECT_TRUE(editor_.mode().state().active_entity.uuid.empty());
  EXPECT_TRUE(session_.scene().selected_entities().empty());
  EXPECT_TRUE(session_.scene().exists(outside.entity));
  EXPECT_FALSE(editor_.mode().can_set(QuaderEditorMode::Edit));
  const auto no_active = editor_.mode().toggle_edit();
  EXPECT_TRUE(no_active.ok());
  EXPECT_FALSE(no_active.changed());
  ASSERT_TRUE(editor_.selection().select_object(outside).ok());
  ASSERT_TRUE(editor_.selection().clear().ok());
  session_.replace_document(SceneContentDocument{});
  EXPECT_EQ(editor_.mode().state().active_entity.id, kInvalidEntityId);
  EXPECT_FALSE(editor_.mode().can_set(QuaderEditorMode::Edit));
}

TEST_F(EditorModeServiceTests, EntryFailureRetainsObjectSelectionAndModeContext) {
  const auto mesh = create_box();
  ASSERT_TRUE(editor_.selection().select_object(mesh).ok());
  session_.editor_mode().set_transition_settlement([] {
    return command_invalid_result("Draft cannot be accepted.");
  });
  const auto before = session_.editor_mode().snapshot();
  const auto history_count = session_.history().undo_count();
  EXPECT_FALSE(editor_.mode().set(QuaderEditorMode::Edit).ok());
  EXPECT_EQ(session_.editor_mode().snapshot(), before);
  EXPECT_EQ(session_.scene().selected_entities(), std::vector<EntityId>{mesh.entity});
  EXPECT_EQ(session_.history().undo_count(), history_count);
}

TEST_F(EditorModeServiceTests, EntryUsesAllSelectedMeshesWithoutHistoryOrDirtyChanges) {
  const auto first = create_box("First");
  const auto second = create_box("Second");
  ASSERT_TRUE(editor_.selection().select_objects({first.entity, second.entity},
                                                second.entity).ok());
  session_.mark_clean();
  const auto history_count = session_.history().undo_count();
  const auto content = session_.content_revision_stamp();
  const auto generation = editor_.mode().generation();
  ASSERT_TRUE(editor_.mode().toggle_edit().ok());
  EXPECT_EQ(editor_.mode().current(), QuaderEditorMode::Edit);
  EXPECT_GT(editor_.mode().generation(), generation);
  EXPECT_EQ(session_.editor_mode().snapshot().participants.size(), 2u);
  EXPECT_TRUE(session_.editor_mode().contains(first.entity));
  EXPECT_TRUE(session_.editor_mode().contains(second.entity));
  EXPECT_EQ(session_.editor_mode().snapshot().active_entity, second.entity);
  EXPECT_EQ(session_.modeling().selection_mode(), ModelingSelectionMode::Vertex);
  EXPECT_EQ(session_.selection().summary().selected_vertex_count, 0u);
  EXPECT_EQ(session_.history().undo_count(), history_count);
  EXPECT_EQ(session_.content_revision_stamp(), content);
  EXPECT_FALSE(session_.dirty());
  ASSERT_TRUE(editor_.mode().toggle_edit().ok());
  EXPECT_EQ(session_.scene().selected_entities(),
            (std::vector<EntityId>{first.entity, second.entity}));
  EXPECT_EQ(session_.scene().primary_selection(), second.entity);
  EXPECT_EQ(session_.modeling().selection_mode(), ModelingSelectionMode::Object);
  EXPECT_EQ(session_.history().undo_count(), history_count);
  EXPECT_FALSE(session_.dirty());
}

TEST_F(EditorModeServiceTests, FailedExitRetainsComponentsMembershipAndInputGeneration) {
  const auto mesh = create_box();
  ASSERT_TRUE(editor_.mode().set(QuaderEditorMode::Edit).ok());
  ASSERT_TRUE(editor_.selection().select_vertex(mesh, 1).ok());
  ASSERT_EQ(editor_.selection().summary().selected_vertex_count, 1u);
  const auto before = session_.editor_mode().snapshot();
  const auto history_count = session_.history().undo_count();
  session_.editor_mode().set_transition_settlement([] {
    return command_invalid_result("Component draft cannot be accepted.");
  });
  EXPECT_FALSE(editor_.mode().set(QuaderEditorMode::Object).ok());
  EXPECT_EQ(session_.editor_mode().snapshot(), before);
  EXPECT_EQ(session_.selection().summary().selected_vertex_count, 1u);
  EXPECT_EQ(session_.selection().summary().active_vertex_id, 1);
  EXPECT_EQ(session_.modeling().selection_mode(), ModelingSelectionMode::Vertex);
  EXPECT_EQ(session_.history().undo_count(), history_count);
}

TEST_F(EditorModeServiceTests, SelectingMeshParentDoesNotEnrollItsUnselectedMeshChild) {
  const auto parent = create_box("Parent");
  const auto child = create_box("Child");
  ASSERT_TRUE(editor_.scene().reparent(child.entity, parent.entity).ok());
  ASSERT_TRUE(editor_.selection().select_object(parent).ok());
  ASSERT_TRUE(editor_.mode().set(QuaderEditorMode::Edit).ok());
  EXPECT_TRUE(session_.editor_mode().contains(parent.entity));
  EXPECT_FALSE(session_.editor_mode().contains(child.entity));
}

TEST_F(EditorModeServiceTests, GroupEntryRecursesDeduplicatesAndSkipsIneligibleEntities) {
  const auto group = editor_.scene().create_entity(
      {.name = "Group", .kind = QuaderEntityKind::Empty});
  ASSERT_TRUE(group.ok());
  const auto nested = editor_.scene().create_entity(
      {.name = "Nested", .kind = QuaderEntityKind::Empty,
       .parent = group.value().id});
  ASSERT_TRUE(nested.ok());
  const auto first = create_box("First");
  const auto second = create_box("Second");
  const auto hidden = create_box("Hidden");
  const auto locked = create_box("Locked");
  const auto light = editor_.scene().create_entity(
      {.name = "Light", .kind = QuaderEntityKind::Light,
       .parent = group.value().id});
  ASSERT_TRUE(light.ok());
  ASSERT_TRUE(editor_.scene().batch("Arrange")
                  .reparent(first.entity, group.value().id)
                  .reparent(second.entity, nested.value().id)
                  .reparent(hidden.entity, group.value().id)
                  .set_visible(hidden.entity, false)
                  .reparent(locked.entity, group.value().id)
                  .set_selectable(locked.entity, false)
                  .commit().ok());
  ASSERT_TRUE(editor_.selection().select_objects(
      {group.value().id, second.entity}, second.entity).ok());
  ASSERT_TRUE(editor_.mode().set(QuaderEditorMode::Edit).ok());
  EXPECT_EQ(session_.editor_mode().snapshot().participants.size(), 2u);
  EXPECT_TRUE(session_.editor_mode().contains(first.entity));
  EXPECT_TRUE(session_.editor_mode().contains(second.entity));
  EXPECT_FALSE(session_.editor_mode().contains(group.value().id));
  EXPECT_FALSE(session_.editor_mode().contains(hidden.entity));
  EXPECT_FALSE(session_.editor_mode().contains(locked.entity));
  EXPECT_FALSE(session_.editor_mode().contains(light.value().id));
}

TEST_F(EditorModeServiceTests, ReentryRemembersComponentsAndLastComponentKindPerScene) {
  const auto first = create_box("First");
  const auto second = create_box("Second");
  ASSERT_TRUE(editor_.selection().select_object(first).ok());
  ASSERT_TRUE(editor_.mode().set(QuaderEditorMode::Edit).ok());
  ASSERT_TRUE(editor_.selection().select_vertex(first, 1).ok());
  ASSERT_EQ(session_.selection().summary().selected_vertex_count, 1u);
  ASSERT_TRUE(editor_.mode().set(QuaderEditorMode::Object).ok());
  ASSERT_TRUE(editor_.selection().select_object(second).ok());
  ASSERT_TRUE(editor_.mode().set(QuaderEditorMode::Edit).ok());
  EXPECT_EQ(session_.selection().summary().selected_vertex_count, 0u);
  ASSERT_TRUE(editor_.selection().set_mode(QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor_.mode().set(QuaderEditorMode::Object).ok());
  ASSERT_TRUE(editor_.mode().set(QuaderEditorMode::Edit).ok());
  EXPECT_EQ(session_.modeling().selection_mode(), ModelingSelectionMode::Face);
  ASSERT_TRUE(editor_.selection().set_mode(QuaderSelectionMode::Vertex).ok());
  ASSERT_TRUE(editor_.mode().set(QuaderEditorMode::Object).ok());
  ASSERT_TRUE(editor_.selection().select_object(first).ok());
  ASSERT_TRUE(editor_.mode().set(QuaderEditorMode::Edit).ok());
  EXPECT_EQ(session_.selection().summary().selected_vertex_count, 1u);
  EXPECT_EQ(session_.selection().summary().active_vertex_id, 1);
}

TEST_F(EditorModeServiceTests, InstanceEntryUsesVisibleOccurrenceAndAuthoredMemberEligibility) {
  const auto visible = create_box("Visible Instance Member");
  const auto hidden = create_box("Hidden Instance Member");
  const auto locked = create_box("Locked Instance Member");
  const auto outside = create_box("Outside Instance");
  ASSERT_TRUE(editor_.selection().select_objects(
      {visible.entity, hidden.entity, locked.entity}, visible.entity).ok());
  const auto instance = editor_.modeling().create_instance_from_selection();
  ASSERT_TRUE(instance.ok()) << instance.error_message();
  const EntityId occurrence = instance.value().occurrence.root.id;
  EXPECT_FALSE(session_.scene().is_effectively_visible(visible.entity));
  ASSERT_TRUE(editor_.modeling().enter_instance_edit(occurrence).ok());
  ASSERT_TRUE(editor_.scene().set_visible(hidden.entity, false).ok());
  ASSERT_TRUE(editor_.scene().set_selectable(locked.entity, false).ok());
  ASSERT_TRUE(editor_.selection().select_objects(
      {visible.entity, hidden.entity, locked.entity, outside.entity},
      visible.entity).ok());
  EXPECT_TRUE(editor_.mode().can_set(QuaderEditorMode::Edit));
  ASSERT_TRUE(editor_.mode().set(QuaderEditorMode::Edit).ok());
  EXPECT_EQ(session_.editor_mode().snapshot().participants.size(), 1u);
  EXPECT_TRUE(session_.editor_mode().contains(visible.entity));
  EXPECT_FALSE(session_.editor_mode().contains(hidden.entity));
  EXPECT_FALSE(session_.editor_mode().contains(locked.entity));
  EXPECT_FALSE(session_.editor_mode().contains(outside.entity));
}

TEST_F(EditorModeServiceTests, HiddenInstanceOccurrenceCannotAdmitItsSelectedCanonicalMember) {
  const auto member = create_box("Instance Member");
  const auto instance = editor_.modeling().create_instance_from_selection();
  ASSERT_TRUE(instance.ok()) << instance.error_message();
  const EntityId occurrence = instance.value().occurrence.root.id;
  ASSERT_TRUE(editor_.modeling().enter_instance_edit(occurrence).ok());
  ASSERT_TRUE(editor_.selection().select_object(member).ok());
  ASSERT_TRUE(editor_.scene().set_visible(occurrence, false).ok());
  const auto before = session_.editor_mode().snapshot();
  EXPECT_FALSE(editor_.mode().can_set(QuaderEditorMode::Edit));
  const auto entered = editor_.mode().set(QuaderEditorMode::Edit);
  EXPECT_TRUE(entered.ok());
  EXPECT_FALSE(entered.changed());
  EXPECT_EQ(session_.editor_mode().snapshot(), before);
}

TEST_F(EditorModeServiceTests, ComponentClearAndSceneClicksNeverReplaceEditMembership) {
  const auto first = create_box("First");
  const auto second = create_box("Second");
  const auto outside = create_box("Outside");
  ASSERT_TRUE(editor_.selection().select_objects({first.entity, second.entity}).ok());
  ASSERT_TRUE(editor_.mode().set(QuaderEditorMode::Edit).ok());
  constexpr std::array kKinds{QuaderSelectionMode::Vertex,
                               QuaderSelectionMode::Edge,
                               QuaderSelectionMode::Face};
  for (const auto kind : kKinds) {
    SCOPED_TRACE(static_cast<int>(kind));
    ASSERT_TRUE(editor_.selection().set_mode(kind).ok());
    const auto participants = session_.editor_mode().snapshot().participants;
    ASSERT_TRUE(editor_.selection().select_all().ok());
    ASSERT_TRUE(editor_.selection().clear().ok());
    EXPECT_EQ(session_.editor_mode().snapshot().participants, participants);
    EXPECT_FALSE(editor_.selection().select_object(outside).ok());
    EXPECT_FALSE(editor_.selection().select_objects_for_inspection(
        {outside.entity}).ok());
    EXPECT_EQ(session_.editor_mode().snapshot().participants, participants);
    EXPECT_FALSE(editor_.selection().set_mode(QuaderSelectionMode::Object).ok());
    EXPECT_FALSE(editor_.selection().set_mode(QuaderSelectionMode::Group).ok());
  }
}

TEST_F(EditorModeServiceTests, UnchangedModeRequestsDoNotSettleOrInvalidateInput) {
  create_box();
  ASSERT_TRUE(editor_.mode().set(QuaderEditorMode::Edit).ok());
  int settlement_count = 0;
  session_.editor_mode().set_transition_settlement([&] {
    ++settlement_count;
    return command_changed_result();
  });
  const auto before = session_.editor_mode().snapshot();
  const auto result = editor_.mode().set(QuaderEditorMode::Edit);
  EXPECT_TRUE(result.ok());
  EXPECT_FALSE(result.changed());
  EXPECT_EQ(settlement_count, 0);
  EXPECT_EQ(session_.editor_mode().snapshot(), before);
}

TEST_F(EditorModeServiceTests, DocumentReplacementResetsModeAndInvalidatesOldGeneration) {
  const auto mesh = create_box();
  ASSERT_TRUE(editor_.mode().set(QuaderEditorMode::Edit).ok());
  ASSERT_TRUE(editor_.selection().select_vertex(mesh, 1).ok());
  const auto generation = session_.editor_mode().generation();
  session_.replace_document(SceneContentDocument{});
  EXPECT_EQ(session_.editor_mode().mode(), EditorMode::Object);
  EXPECT_GT(session_.editor_mode().generation(), generation);
  EXPECT_TRUE(session_.editor_mode().snapshot().participants.empty());
  EXPECT_EQ(session_.editor_mode().snapshot().component_kind,
            ModelingSelectionMode::Vertex);
  EXPECT_EQ(session_.modeling().selection_mode(), ModelingSelectionMode::Object);
}

TEST_F(EditorModeServiceTests, StructuralHistoryCannotEnrollMeshesFromAnEarlierEditGeneration) {
  const auto first = create_box("First");
  const auto second = create_box("Second");
  ASSERT_TRUE(editor_.selection().select_object(first).ok());
  ASSERT_TRUE(editor_.mode().set(QuaderEditorMode::Edit).ok());
  const auto older_context = session_.editor_mode().capture_history_state();
  ASSERT_TRUE(editor_.mode().set(QuaderEditorMode::Object).ok());
  ASSERT_TRUE(editor_.selection().select_object(second).ok());
  ASSERT_TRUE(editor_.mode().set(QuaderEditorMode::Edit).ok());
  const auto current_context = session_.editor_mode().snapshot();
  session_.editor_mode().restore_history_state(older_context);
  EXPECT_EQ(session_.editor_mode().snapshot(), current_context);
  EXPECT_FALSE(session_.editor_mode().contains(first.entity));
  EXPECT_TRUE(session_.editor_mode().contains(second.entity));
}

TEST_F(EditorModeServiceTests, UnchangedHistoryContextDoesNotInvalidateSelectionOrModeRevision) {
  create_box();
  ASSERT_TRUE(editor_.mode().set(QuaderEditorMode::Edit).ok());
  const auto historical = session_.editor_mode().capture_history_state();
  session_.editor_mode().restore_history_state(historical);
  EXPECT_EQ(session_.editor_mode().snapshot(), historical);

  ASSERT_TRUE(editor_.selection().set_mode(QuaderSelectionMode::Edge).ok());
  ASSERT_TRUE(editor_.selection().set_mode(QuaderSelectionMode::Vertex).ok());
  const auto current = session_.editor_mode().snapshot();
  ASSERT_GT(current.revision, historical.revision);
  ASSERT_EQ(current.generation, historical.generation);
  const auto selection_revision = editor_.revisions().modeling_selection_revision;
  session_.editor_mode().restore_history_state(historical);
  EXPECT_EQ(session_.editor_mode().snapshot(), current);
  EXPECT_EQ(editor_.revisions().modeling_selection_revision, selection_revision);
}

TEST_F(EditorModeServiceTests, HistoryContextSemanticChangesAdvanceRevisionWithoutChangingGeneration) {
  const auto first = create_box("First");
  const auto second = create_box("Second");
  const auto output = create_box("Structural Result");
  ASSERT_TRUE(editor_.selection().select_objects(
      {first.entity, second.entity}, first.entity).ok());
  ASSERT_TRUE(editor_.mode().set(QuaderEditorMode::Edit).ok());
  const auto original = session_.editor_mode().capture_history_state();
  for (int changed_field = 0; changed_field < 4; ++changed_field) {
    SCOPED_TRACE(changed_field);
    auto historical = original;
    switch (changed_field) {
    case 0:
      historical.active_entity = second.entity;
      break;
    case 1:
      historical.component_kind = ModelingSelectionMode::Edge;
      break;
    case 2:
      historical.participants.pop_back();
      historical.active_entity = historical.participants.front().entity;
      break;
    case 3:
      historical.object_results.push_back(output.entity);
      break;
    }
    const auto previous_revision = session_.editor_mode().revision();
    session_.editor_mode().restore_history_state(historical);
    EXPECT_GT(session_.editor_mode().revision(), previous_revision);
    EXPECT_EQ(session_.editor_mode().generation(), original.generation);
    EXPECT_EQ(session_.editor_mode().snapshot().active_entity, historical.active_entity);
    EXPECT_EQ(session_.editor_mode().snapshot().component_kind, historical.component_kind);
    EXPECT_EQ(session_.editor_mode().snapshot().participants, historical.participants);
    EXPECT_EQ(session_.editor_mode().snapshot().object_results, historical.object_results);
    session_.editor_mode().restore_history_state(original);
  }
}

TEST_F(EditorModeServiceTests, StructuralRemapPreservesUnrelatedParticipantsAndRollbackIsExact) {
  const auto first = create_box("First");
  const auto second = create_box("Second");
  const auto third = create_box("Third");
  ASSERT_TRUE(editor_.selection().select_objects(
      {first.entity, second.entity, third.entity}, second.entity).ok());
  ASSERT_TRUE(editor_.mode().set(QuaderEditorMode::Edit).ok());
  const auto before = session_.editor_mode().capture_history_state();
  const std::array consumed{first.entity, second.entity};
  session_.editor_mode().remap_participants(consumed, first.entity);
  EXPECT_TRUE(session_.editor_mode().contains(first.entity));
  EXPECT_FALSE(session_.editor_mode().contains(second.entity));
  EXPECT_TRUE(session_.editor_mode().contains(third.entity));
  EXPECT_EQ(session_.editor_mode().snapshot().active_entity, first.entity);
  EXPECT_EQ(session_.editor_mode().generation(), before.generation);
  session_.editor_mode().restore_history_state(before);
  EXPECT_EQ(session_.editor_mode().snapshot().participants, before.participants);
  EXPECT_GT(session_.editor_mode().revision(), before.revision);
  auto compensation = before;
  session_.editor_mode().restore_exact_state(std::move(compensation));
  EXPECT_EQ(session_.editor_mode().snapshot(), before);
}

TEST_F(EditorModeServiceTests, StructuralResultsRemainOutsideEditAndJoinObjectSelectionOnExit) {
  const auto source = create_box("Source");
  const auto output = create_box("Extracted Result");
  ASSERT_TRUE(editor_.selection().select_object(source).ok());
  ASSERT_TRUE(editor_.mode().set(QuaderEditorMode::Edit).ok());
  session_.editor_mode().register_object_result(output.entity);
  EXPECT_TRUE(session_.editor_mode().contains(source.entity));
  EXPECT_FALSE(session_.editor_mode().contains(output.entity));
  ASSERT_TRUE(editor_.mode().set(QuaderEditorMode::Object).ok());
  EXPECT_EQ(session_.scene().selected_entities(),
            (std::vector<EntityId>{source.entity, output.entity}));
}

TEST_F(EditorModeServiceTests, FinalParticipantRemovalExitsAndOldHistoryCannotReenterEdit) {
  const auto mesh = create_box();
  ASSERT_TRUE(editor_.mode().set(QuaderEditorMode::Edit).ok());
  const auto before = session_.editor_mode().capture_history_state();
  ASSERT_TRUE(session_.scene().destroy_entity(mesh.entity));
  session_.editor_mode().reconcile_scene_lifecycle();
  EXPECT_EQ(session_.editor_mode().mode(), EditorMode::Object);
  EXPECT_GT(session_.editor_mode().generation(), before.generation);
  session_.editor_mode().restore_history_state(before);
  EXPECT_EQ(session_.editor_mode().mode(), EditorMode::Object);
  EXPECT_TRUE(session_.editor_mode().snapshot().participants.empty());
}

TEST_F(EditorModeServiceTests, DeletingFinalMeshRetiresComponentToolsInEveryPaneAndThroughUndo) {
  for (const auto tool : {ViewportToolId::Knife, ViewportToolId::InsertEdgeLoop}) {
    SCOPED_TRACE(static_cast<int>(tool));
    const auto mesh = create_box();
    ASSERT_TRUE(editor_.mode().set(QuaderEditorMode::Edit).ok());
    ASSERT_TRUE(editor_.tools().activate(tool).ok());
    ASSERT_EQ(editor_.selection().summary().mode, QuaderSelectionMode::Edge);
    for (std::size_t index = 0u; index < kViewportPaneCount; ++index) {
      EXPECT_EQ(session_.viewport_workspace().selected_tool(
                    static_cast<ViewportPaneId>(index)), tool);
    }
    // Exercise an actual structural publication. Component Delete intentionally
    // retains its now-empty mesh document and therefore cannot remove membership.
    const auto deleted = session_.history().execute(
        session_.modeling().create_destroy_entities_command(
            {mesh.entity}, "Remove Final Edit Participant"));
    ASSERT_TRUE(deleted.ok) << deleted.message;
    ASSERT_TRUE(deleted.changed);
    EXPECT_TRUE(editor_.query().modeling_objects().empty());
    EXPECT_EQ(editor_.mode().current(), QuaderEditorMode::Object);
    EXPECT_EQ(editor_.tools().active_tool(), "select");
    for (std::size_t index = 0u; index < kViewportPaneCount; ++index) {
      EXPECT_EQ(session_.viewport_workspace().selected_tool(
                    static_cast<ViewportPaneId>(index)), ViewportToolId::Select);
    }
    ASSERT_TRUE(editor_.history().undo().ok());
    EXPECT_EQ(editor_.query().modeling_objects().size(), 1u);
    EXPECT_EQ(editor_.mode().current(), QuaderEditorMode::Object);
    EXPECT_EQ(editor_.tools().active_tool(), "select");
    for (std::size_t index = 0u; index < kViewportPaneCount; ++index) {
      EXPECT_EQ(session_.viewport_workspace().selected_tool(
                    static_cast<ViewportPaneId>(index)), ViewportToolId::Select);
    }
    ASSERT_TRUE(editor_.scene().destroy(mesh.entity).ok());
  }
}

TEST_F(EditorModeServiceTests, ComponentKindChangesRetireToolsBeforePublishingNewVertexSelection) {
  for (const auto tool : {ViewportToolId::Knife, ViewportToolId::InsertEdgeLoop,
                          ViewportToolId::UvPeel}) {
    for (const bool explicit_kind_change : {false, true}) {
      SCOPED_TRACE(static_cast<int>(tool));
      SCOPED_TRACE(explicit_kind_change);
      const auto mesh = create_box();
      ASSERT_TRUE(editor_.mode().set(QuaderEditorMode::Edit).ok());
      ASSERT_TRUE(editor_.tools().activate(tool).ok());
      ASSERT_EQ(editor_.selection().summary().mode, QuaderSelectionMode::Edge);
      session_.record_settings_presentation_origin(
          editor_.tools().active_tool(), ViewportPaneId::Top);
      const auto generation = editor_.mode().generation();
      const auto history = editor_.history().undo_history().history_revision;
      if (explicit_kind_change) {
        ASSERT_TRUE(editor_.selection().set_mode(QuaderSelectionMode::Vertex).ok());
      }
      ASSERT_TRUE(editor_.selection().select_vertices(
          mesh, {1, 2, 3, 4, 5, 6, 7, 8}).ok());
      EXPECT_EQ(editor_.selection().summary().mode, QuaderSelectionMode::Vertex);
      ASSERT_EQ(editor_.selection().summary().selected_vertex_count, 8u);
      EXPECT_EQ(editor_.tools().active_tool(), "select");
      EXPECT_EQ(session_.settings_presentation_origin().target_id, "select");
      EXPECT_EQ(editor_.mode().generation(), generation);
      EXPECT_EQ(editor_.history().undo_history().history_revision, history);
      for (std::size_t index = 0u; index < kViewportPaneCount; ++index) {
        EXPECT_EQ(session_.viewport_workspace().selected_tool(
                      static_cast<ViewportPaneId>(index)), ViewportToolId::Select);
      }
      const auto deleted = editor_.modeling().delete_selection();
      ASSERT_TRUE(deleted.ok()) << deleted.error_message();
      EXPECT_TRUE(deleted.changed());
      const auto empty_mesh = editor_.query().compiled_object_mesh(mesh);
      ASSERT_TRUE(empty_mesh.has_value());
      EXPECT_TRUE(empty_mesh->vertices.empty());
      EXPECT_TRUE(empty_mesh->indices.empty());
      EXPECT_TRUE(empty_mesh->face_textures.empty());
      const auto selection = editor_.selection().summary();
      EXPECT_EQ(selection.selected_vertex_count, 0u);
      EXPECT_EQ(selection.selected_edge_count, 0u);
      EXPECT_EQ(selection.selected_face_count, 0u);
      EXPECT_EQ(editor_.query().modeling_objects().size(), 1u);
      EXPECT_EQ(editor_.mode().current(), QuaderEditorMode::Edit);
      EXPECT_EQ(editor_.mode().generation(), generation);
      ASSERT_EQ(editor_.mode().state().participants.size(), 1u);
      EXPECT_EQ(editor_.mode().state().participants.front().id, mesh.entity);
      ASSERT_TRUE(editor_.history().undo().ok());
      EXPECT_EQ(editor_.selection().summary().selected_vertex_count, 8u);
      const auto restored_mesh = editor_.query().compiled_object_mesh(mesh);
      ASSERT_TRUE(restored_mesh.has_value());
      EXPECT_FALSE(restored_mesh->vertices.empty());
      EXPECT_FALSE(restored_mesh->indices.empty());
      EXPECT_EQ(editor_.mode().current(), QuaderEditorMode::Edit);
      EXPECT_EQ(editor_.mode().generation(), generation);
      ASSERT_TRUE(editor_.mode().set(QuaderEditorMode::Object).ok());
      ASSERT_TRUE(editor_.scene().destroy(mesh.entity).ok());
    }
  }
}

TEST(EditorModeProjectLifecycleTests, PublicSaveCloseReopenResetsModeAndComponentMemory) {
  ScopedModeProjectDirectory directory;
  ASSERT_TRUE(directory.valid());
  ASSERT_TRUE(std::filesystem::create_directory(directory.project_root()));
  ProjectSession project;
  project.set_resource_root(std::filesystem::path(QUADER_SOURCE_DIR) /
                            "resources/modeling_assets");
  QuaderEditorApi editor(project);
  ASSERT_TRUE(editor.project().create(directory.project_root()).ok());
  const auto scene_id = editor.scene_documents().active_scene_id();
  ASSERT_TRUE(scene_id.has_value());
  const auto created = editor.modeling().create_box(
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}}, "Saved Mesh");
  ASSERT_TRUE(created.ok()) << created.error_message();
  ASSERT_TRUE(editor.mode().set(QuaderEditorMode::Edit).ok());
  ASSERT_TRUE(editor.selection().select_vertex(created.value(), 1).ok());
  ASSERT_EQ(editor.selection().summary().selected_vertex_count, 1u);
  const auto path = ProjectRelativePath::parse_portable("Scenes/Mode Memory.qdr");
  ASSERT_TRUE(path.ok());
  const auto saved = editor.scene_documents().save_as(*scene_id, path.value());
  ASSERT_TRUE(saved.ok()) << saved.error_message();
  EXPECT_EQ(editor.mode().current(), QuaderEditorMode::Edit);
  ASSERT_TRUE(editor.scene_documents().close(*scene_id).ok());
  const auto reopened = editor.scene_documents().open(path.value());
  ASSERT_TRUE(reopened.ok()) << reopened.error_message();
  EXPECT_NE(reopened.value(), *scene_id);
  EXPECT_EQ(editor.mode().current(), QuaderEditorMode::Object);
  EXPECT_EQ(editor.selection().summary().mode, QuaderSelectionMode::Object);
  EXPECT_TRUE(editor.mode().state().participants.empty());
  const auto objects = editor.query().modeling_objects();
  ASSERT_EQ(objects.size(), 1u);
  const auto mesh = editor.query().modeling_object_for_entity(objects.front().entity);
  ASSERT_TRUE(mesh.has_value());
  EXPECT_EQ(mesh->entity_uuid, created.value().entity_uuid);
  ASSERT_TRUE(editor.selection().select_object(*mesh).ok());
  ASSERT_TRUE(editor.mode().set(QuaderEditorMode::Edit).ok());
  EXPECT_EQ(editor.selection().summary().mode, QuaderSelectionMode::Vertex);
  EXPECT_EQ(editor.selection().summary().selected_vertex_count, 0u);
  EXPECT_EQ(editor.selection().summary().selected_edge_count, 0u);
  EXPECT_EQ(editor.selection().summary().selected_face_count, 0u);
}

TEST(EditorModeProjectLifecycleTests, ResidentScenesRetainIndependentModeKindsAndSelections) {
  ScopedModeProjectDirectory directory;
  ASSERT_TRUE(directory.valid());
  ASSERT_TRUE(std::filesystem::create_directory(directory.project_root()));
  ProjectSession project;
  project.set_resource_root(std::filesystem::path(QUADER_SOURCE_DIR) /
                            "resources/modeling_assets");
  QuaderEditorApi editor(project);
  ASSERT_TRUE(editor.project().create(directory.project_root()).ok());
  const auto first_scene = editor.scene_documents().active_scene_id();
  ASSERT_TRUE(first_scene.has_value());
  const auto first_mesh = editor.modeling().create_box(
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}}, "First Mesh");
  ASSERT_TRUE(first_mesh.ok());
  ASSERT_TRUE(editor.mode().set(QuaderEditorMode::Edit).ok());
  ASSERT_TRUE(editor.selection().select_vertex(first_mesh.value(), 1).ok());
  ASSERT_EQ(editor.selection().summary().selected_vertex_count, 1u);
  const auto first_generation = editor.mode().generation();
  const auto second_scene = editor.scene_documents().new_scene();
  ASSERT_TRUE(second_scene.ok());
  EXPECT_EQ(editor.mode().current(), QuaderEditorMode::Object);
  const auto second_mesh = editor.modeling().create_box(
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}}, "Second Mesh");
  ASSERT_TRUE(second_mesh.ok());
  ASSERT_TRUE(editor.scene_documents().activate(*first_scene).ok());
  EXPECT_EQ(editor.mode().current(), QuaderEditorMode::Edit);
  EXPECT_EQ(editor.mode().generation(), first_generation);
  EXPECT_EQ(editor.selection().summary().selected_vertex_count, 1u);
  EXPECT_EQ(editor.selection().summary().active_vertex_id, 1);
  ASSERT_TRUE(editor.scene_documents().activate(second_scene.value()).ok());
  EXPECT_EQ(editor.mode().current(), QuaderEditorMode::Object);
  ASSERT_TRUE(editor.mode().set(QuaderEditorMode::Edit).ok());
  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection().select_face(second_mesh.value(), 1).ok());
  const auto second_generation = editor.mode().generation();
  ASSERT_TRUE(editor.scene_documents().activate(*first_scene).ok());
  EXPECT_EQ(editor.selection().summary().mode, QuaderSelectionMode::Vertex);
  EXPECT_EQ(editor.selection().summary().selected_vertex_count, 1u);
  EXPECT_EQ(editor.mode().generation(), first_generation);
  ASSERT_TRUE(editor.mode().set(QuaderEditorMode::Object).ok());
  ASSERT_TRUE(editor.scene_documents().activate(second_scene.value()).ok());
  EXPECT_EQ(editor.mode().current(), QuaderEditorMode::Edit);
  EXPECT_EQ(editor.mode().generation(), second_generation);
  EXPECT_EQ(editor.selection().summary().mode, QuaderSelectionMode::Face);
  EXPECT_EQ(editor.selection().summary().selected_face_count, 1u);
  EXPECT_EQ(editor.selection().summary().active_face_id, 1);
  ASSERT_TRUE(editor.scene_documents().activate(*first_scene).ok());
  EXPECT_EQ(editor.mode().current(), QuaderEditorMode::Object);
  ASSERT_TRUE(editor.mode().set(QuaderEditorMode::Edit).ok());
  EXPECT_EQ(editor.selection().summary().mode, QuaderSelectionMode::Vertex);
  EXPECT_EQ(editor.selection().summary().selected_vertex_count, 1u);
  ASSERT_EQ(editor.mode().state().participants.size(), 1u);
  EXPECT_EQ(editor.mode().state().participants.front().uuid,
            first_mesh.value().entity_uuid);
}

TEST(EditorModeProjectLifecycleTests, DeselectedActiveObjectIsSceneLocalAndNotPersisted) {
  ScopedModeProjectDirectory directory;
  ASSERT_TRUE(directory.valid());
  ASSERT_TRUE(std::filesystem::create_directory(directory.project_root()));
  ProjectSession project;
  project.set_resource_root(std::filesystem::path(QUADER_SOURCE_DIR) /
                            "resources/modeling_assets");
  QuaderEditorApi editor(project);
  ASSERT_TRUE(editor.project().create(directory.project_root()).ok());
  const auto first_scene = editor.scene_documents().active_scene_id();
  ASSERT_TRUE(first_scene.has_value());
  const auto first_mesh = editor.modeling().create_box(
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}}, "First Active");
  ASSERT_TRUE(first_mesh.ok());
  ASSERT_TRUE(editor.selection().clear().ok());
  const auto first_active = editor.mode().state().active_entity;
  const auto path = ProjectRelativePath::parse_portable("Scenes/Retained Active.qdr");
  ASSERT_TRUE(path.ok());
  ASSERT_TRUE(editor.scene_documents().save_as(*first_scene, path.value()).ok());

  const auto second_scene = editor.scene_documents().new_scene();
  ASSERT_TRUE(second_scene.ok());
  EXPECT_EQ(editor.mode().state().active_entity.id, kInvalidEntityId);
  const auto second_mesh = editor.modeling().create_box(
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}}, "Second Active");
  ASSERT_TRUE(second_mesh.ok());
  ASSERT_TRUE(editor.selection().clear().ok());
  const auto second_active = editor.mode().state().active_entity;
  EXPECT_NE(first_active.uuid, second_active.uuid);
  ASSERT_TRUE(editor.scene_documents().activate(*first_scene).ok());
  EXPECT_EQ(editor.mode().state().active_entity.uuid, first_active.uuid);
  EXPECT_EQ(editor.selection().summary().selected_object_count, 0u);
  ASSERT_TRUE(editor.mode().toggle_edit().ok());
  ASSERT_EQ(editor.mode().state().participants.size(), 1u);
  EXPECT_EQ(editor.mode().state().participants.front().uuid, first_active.uuid);
  ASSERT_TRUE(editor.mode().set(QuaderEditorMode::Object).ok());
  ASSERT_TRUE(editor.selection().clear().ok());
  ASSERT_TRUE(editor.scene_documents().activate(second_scene.value()).ok());
  EXPECT_EQ(editor.mode().state().active_entity.uuid, second_active.uuid);
  EXPECT_EQ(editor.selection().summary().selected_object_count, 0u);
  ASSERT_TRUE(editor.scene_documents().close(*first_scene).ok());
  const auto reopened = editor.scene_documents().open(path.value());
  ASSERT_TRUE(reopened.ok());
  EXPECT_EQ(editor.mode().current(), QuaderEditorMode::Object);
  EXPECT_EQ(editor.selection().summary().selected_object_count, 0u);
  EXPECT_EQ(editor.mode().state().active_entity.id, kInvalidEntityId);
  EXPECT_FALSE(editor.mode().can_set(QuaderEditorMode::Edit));
  const auto entered = editor.mode().toggle_edit();
  EXPECT_TRUE(entered.ok());
  EXPECT_FALSE(entered.changed());
}

} // namespace
} // namespace quader
