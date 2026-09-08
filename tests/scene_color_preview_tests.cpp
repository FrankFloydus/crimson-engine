////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "editor/commands/document_command_service.hpp"
#include "editor/session/scene_session.hpp"

#include <gtest/gtest.h>

#include <limits>

namespace {

/// Exercises the same public preview contract for light RGB and environment RGB.
class SceneColorPreviewTests : public ::testing::TestWithParam<bool> {
protected:
  SceneColorPreviewTests()
      : editor_(quader::QuaderApiSessionInternalAccess::from_scene_session(session_)) {}

  void SetUp() override {
    const auto created = editor_.scene().create_point_light({.select_created = true});
    ASSERT_TRUE(created.ok()) << created.error_message();
    light_ = created.value().id;
    entity_ = GetParam() ? session_.scene().scene_root() : light_;
    ASSERT_TRUE(editor_.scene().select(entity_).ok());
    session_.mark_clean();
  }

  quader::QuaderApiResult<quader::QuaderScenePropertyEditRef> begin() {
    return GetParam() ? editor_.scene().begin_environment_color_edit(entity_)
                      : editor_.scene().begin_light_color_edit(entity_);
  }

  quader::QuaderApiVoidResult update(quader::QuaderScenePropertyEditRef ref, quader::QVec3 color) {
    return GetParam() ? editor_.scene().update_environment_color_edit(ref, color)
                      : editor_.scene().update_light_color_edit(ref, color);
  }

  quader::QuaderApiVoidResult finish(quader::QuaderScenePropertyEditRef ref, bool cancel = false) {
    return GetParam() ? editor_.scene().finish_environment_color_edit(ref, cancel)
                      : editor_.scene().finish_light_color_edit(ref, cancel);
  }

  quader::QVec3 color() const {
    return GetParam() ? session_.scene().scene_settings(entity_)->environment.clear_color
                      : session_.scene().light(entity_)->color;
  }

  void expect_color(quader::QVec3 expected) const {
    const auto actual = color();
    EXPECT_FLOAT_EQ(actual.x, expected.x);
    EXPECT_FLOAT_EQ(actual.y, expected.y);
    EXPECT_FLOAT_EQ(actual.z, expected.z);
  }

  void change_unrelated_field() {
    if (GetParam()) {
      auto settings = *session_.scene().scene_settings(entity_);
      settings.environment.intensity = 3.0f;
      ASSERT_TRUE(session_.scene().set_scene_settings(entity_, settings));
    } else {
      auto properties = *session_.scene().light(entity_);
      properties.intensity = 3.0f;
      ASSERT_TRUE(session_.scene().set_light(entity_, properties));
    }
  }

  void overwrite_color(quader::QVec3 replacement) {
    if (GetParam()) {
      auto settings = *session_.scene().scene_settings(entity_);
      settings.environment.clear_color = replacement;
      ASSERT_TRUE(session_.scene().set_scene_settings(entity_, settings));
    } else {
      auto properties = *session_.scene().light(entity_);
      properties.color = replacement;
      ASSERT_TRUE(session_.scene().set_light(entity_, properties));
    }
  }

  quader::SceneSession session_;
  quader::QuaderEditorApi editor_;
  quader::EntityId light_ = quader::kInvalidEntityId;
  quader::EntityId entity_ = quader::kInvalidEntityId;
};

TEST_P(SceneColorPreviewTests, PublishesBeforeAcceptanceWithOneUndoStep) {
  const auto before = color();
  const auto undo_count = session_.history().undo_count();
  const auto begun = begin();
  ASSERT_TRUE(begun.ok());
  const quader::QVec3 preview{0.125f, 0.25f, 0.5f};
  ASSERT_TRUE(update(begun.value(), preview).ok());
  expect_color(preview);
  EXPECT_EQ(session_.history().undo_count(), undo_count);
  EXPECT_FALSE(session_.dirty());
  const auto revision = session_.scene().revision();
  const auto unchanged = update(begun.value(), preview);
  ASSERT_TRUE(unchanged.ok());
  EXPECT_FALSE(unchanged.changed());
  EXPECT_EQ(session_.scene().revision(), revision);
  ASSERT_TRUE(finish(begun.value()).ok());
  EXPECT_EQ(session_.history().undo_count(), undo_count + 1u);
  EXPECT_TRUE(session_.dirty());
  ASSERT_TRUE(editor_.history().undo().ok());
  expect_color(before);
  ASSERT_TRUE(editor_.history().redo().ok());
  expect_color(preview);
}

TEST_P(SceneColorPreviewTests, CancelRestoresCleanStateAndExistingRedo) {
  ASSERT_TRUE(editor_.scene().rename(light_, "Renamed light").ok());
  ASSERT_TRUE(editor_.history().undo().ok());
  session_.mark_clean();
  const auto before = color();
  const auto undo_count = session_.history().undo_count();
  const auto redo_count = session_.history().redo_count();
  ASSERT_GT(redo_count, 0u);
  const auto begun = begin();
  ASSERT_TRUE(begun.ok());
  ASSERT_TRUE(update(begun.value(), {0.125f, 0.25f, 0.5f}).ok());
  EXPECT_FALSE(editor_.history().can_redo());
  ASSERT_TRUE(finish(begun.value(), true).ok());
  expect_color(before);
  EXPECT_FALSE(session_.dirty());
  EXPECT_EQ(session_.history().undo_count(), undo_count);
  EXPECT_EQ(session_.history().redo_count(), redo_count);
  ASSERT_TRUE(editor_.history().redo().ok());
  EXPECT_EQ(editor_.query().entity(light_)->name, "Renamed light");
}

TEST_P(SceneColorPreviewTests, RoundTripDoesNotCreateHistoryOrDirtyContent) {
  const auto before = color();
  const auto undo_count = session_.history().undo_count();
  const auto begun = begin();
  ASSERT_TRUE(begun.ok());
  ASSERT_TRUE(update(begun.value(), {0.125f, 0.25f, 0.5f}).ok());
  ASSERT_TRUE(update(begun.value(), before).ok());
  const auto accepted = finish(begun.value());
  ASSERT_TRUE(accepted.ok());
  EXPECT_FALSE(accepted.changed());
  EXPECT_EQ(session_.history().undo_count(), undo_count);
  EXPECT_FALSE(session_.dirty());
}

TEST_P(SceneColorPreviewTests, CancelPreservesConcurrentFieldsAndTheirDirtyState) {
  const auto before = color();
  const auto begun = begin();
  ASSERT_TRUE(begun.ok());
  ASSERT_TRUE(update(begun.value(), {0.125f, 0.25f, 0.5f}).ok());
  change_unrelated_field();
  ASSERT_TRUE(finish(begun.value(), true).ok());
  expect_color(before);
  const float intensity = GetParam()
      ? session_.scene().scene_settings(entity_)->environment.intensity
      : session_.scene().light(entity_)->intensity;
  EXPECT_FLOAT_EQ(intensity, 3.0f);
  EXPECT_TRUE(session_.dirty());
}

TEST_P(SceneColorPreviewTests, OwnedFieldConflictRetiresPreviewWithoutOverwriting) {
  const auto begun = begin();
  ASSERT_TRUE(begun.ok());
  ASSERT_TRUE(update(begun.value(), {0.125f, 0.25f, 0.5f}).ok());
  const quader::QVec3 competing{0.75f, 0.5f, 0.25f};
  overwrite_color(competing);
  EXPECT_FALSE(update(begun.value(), {0.25f, 0.25f, 0.25f}).ok());
  EXPECT_FALSE(session_.document_commands().property_edit_active());
  expect_color(competing);
  EXPECT_TRUE(session_.dirty());
}

TEST_P(SceneColorPreviewTests, UndoCancelsPreviewBeforeConsumingExistingHistory) {
  const auto before = color();
  const auto undo_count = session_.history().undo_count();
  const auto begun = begin();
  ASSERT_TRUE(begun.ok());
  ASSERT_TRUE(update(begun.value(), {0.125f, 0.25f, 0.5f}).ok());
  ASSERT_TRUE(editor_.history().undo().ok());
  EXPECT_FALSE(session_.document_commands().property_edit_active());
  expect_color(before);
  EXPECT_EQ(session_.history().undo_count(), undo_count);
  EXPECT_FALSE(session_.dirty());
}

TEST_P(SceneColorPreviewTests, SelectionChangeCancelsOwnedColor) {
  const auto before = color();
  const auto begun = begin();
  ASSERT_TRUE(begun.ok());
  ASSERT_TRUE(update(begun.value(), {0.125f, 0.25f, 0.5f}).ok());
  ASSERT_TRUE(editor_.scene().select(GetParam() ? light_ : session_.scene().scene_root()).ok());
  EXPECT_FALSE(session_.document_commands().property_edit_active());
  expect_color(before);
}

TEST_P(SceneColorPreviewTests, ReplacementRejectsStaleCallbacksAndKeepsNewPreview) {
  const auto begun = begin();
  ASSERT_TRUE(begun.ok());
  ASSERT_TRUE(update(begun.value(), {0.125f, 0.25f, 0.5f}).ok());
  session_.replace_document(quader::SceneContentDocument{});
  EXPECT_FALSE(session_.document_commands().property_edit_active());
  if (GetParam()) {
    entity_ = session_.scene().scene_root();
  } else {
    const auto created = editor_.scene().create_point_light();
    ASSERT_TRUE(created.ok());
    entity_ = created.value().id;
  }
  const auto fresh = begin();
  ASSERT_TRUE(fresh.ok());
  const quader::QVec3 replacement{0.75f, 0.5f, 0.25f};
  ASSERT_TRUE(update(fresh.value(), replacement).ok());
  EXPECT_FALSE(update(begun.value(), {0.25f, 0.25f, 0.25f}).ok());
  EXPECT_FALSE(finish(begun.value(), true).ok());
  EXPECT_TRUE(session_.document_commands().property_edit_active());
  expect_color(replacement);
  ASSERT_TRUE(finish(fresh.value(), true).ok());
}

TEST_P(SceneColorPreviewTests, InvalidValuesAndCompetingToolsPreservePreview) {
  const auto begun = begin();
  ASSERT_TRUE(begun.ok());
  EXPECT_FALSE(begin().ok());
  EXPECT_FALSE(editor_.tools().activate(quader::ViewportToolId::CreatePrimitive).ok());
  const auto before = color();
  EXPECT_FALSE(update(begun.value(), {std::numeric_limits<float>::quiet_NaN(), 0, 0}).ok());
  expect_color(before);
  EXPECT_TRUE(session_.document_commands().property_edit_active());
  ASSERT_TRUE(finish(begun.value(), true).ok());
}

TEST_P(SceneColorPreviewTests, EqualComponentAndSelectionWritesKeepPreviewOpen) {
  const auto begun = begin();
  ASSERT_TRUE(begun.ok());
  ASSERT_TRUE(update(begun.value(), {0.125f, 0.25f, 0.5f}).ok());
  const auto undo_count = session_.history().undo_count();
  const auto snapshot = editor_.query().entity(entity_);
  ASSERT_TRUE(snapshot.has_value());
  const auto unchanged = GetParam()
      ? editor_.scene().set_scene_settings(entity_, snapshot->scene_settings)
      : editor_.scene().set_light_component(entity_, snapshot->light);
  ASSERT_TRUE(unchanged.ok());
  EXPECT_FALSE(unchanged.changed());
  ASSERT_TRUE(editor_.scene().select(entity_).ok());
  EXPECT_TRUE(session_.document_commands().property_edit_active());
  EXPECT_EQ(session_.history().undo_count(), undo_count);
  ASSERT_TRUE(finish(begun.value(), true).ok());
}

TEST_P(SceneColorPreviewTests, AcceptedUndoKeepsUnrelatedFields) {
  const auto before = color();
  const auto begun = begin();
  ASSERT_TRUE(begun.ok());
  ASSERT_TRUE(update(begun.value(), {0.125f, 0.25f, 0.5f}).ok());
  change_unrelated_field();
  ASSERT_TRUE(finish(begun.value()).ok());
  ASSERT_TRUE(editor_.history().undo().ok());
  expect_color(before);
  const float intensity = GetParam()
      ? session_.scene().scene_settings(entity_)->environment.intensity
      : session_.scene().light(entity_)->intensity;
  EXPECT_FLOAT_EQ(intensity, 3.0f);
}

TEST_P(SceneColorPreviewTests, ReopenedPreviewRejectsCallbacksFromThePreviousEdit) {
  const auto previous = begin();
  ASSERT_TRUE(previous.ok());
  ASSERT_TRUE(update(previous.value(), {0.125f, 0.25f, 0.5f}).ok());
  ASSERT_TRUE(finish(previous.value(), true).ok());
  const auto reopened = begin();
  ASSERT_TRUE(reopened.ok());
  EXPECT_NE(previous.value().generation, reopened.value().generation);
  const quader::QVec3 current{0.75f, 0.5f, 0.25f};
  ASSERT_TRUE(update(reopened.value(), current).ok());
  const auto revision = session_.scene().revision();
  const auto undo_count = session_.history().undo_count();
  EXPECT_FALSE(update(previous.value(), {0.25f, 0.25f, 0.25f}).ok());
  EXPECT_FALSE(finish(previous.value(), true).ok());
  EXPECT_FALSE(finish(previous.value()).ok());
  EXPECT_TRUE(session_.document_commands().property_edit_active());
  expect_color(current);
  EXPECT_EQ(session_.scene().revision(), revision);
  EXPECT_EQ(session_.history().undo_count(), undo_count);
  ASSERT_TRUE(finish(reopened.value(), true).ok());
}

TEST_P(SceneColorPreviewTests, SelectedCreationCancelsPreviewBeforeChangingSelection) {
  const auto before = color();
  const auto begun = begin();
  ASSERT_TRUE(begun.ok());
  ASSERT_TRUE(update(begun.value(), {0.125f, 0.25f, 0.5f}).ok());
  const auto created = editor_.scene().create_point_light({.select_created = true});
  ASSERT_TRUE(created.ok());
  EXPECT_FALSE(session_.document_commands().property_edit_active());
  EXPECT_EQ(session_.scene().primary_selection(), created.value().id);
  expect_color(before);
  ASSERT_TRUE(editor_.history().undo().ok());
  expect_color(before);
}

TEST_P(SceneColorPreviewTests, DuplicateCancelsPreviewBeforeCapturingTheClone) {
  const auto before = color();
  const auto light_before = *session_.scene().light(light_);
  const auto begun = begin();
  ASSERT_TRUE(begun.ok());
  ASSERT_TRUE(update(begun.value(), {0.125f, 0.25f, 0.5f}).ok());
  const auto duplicated = editor_.scene().duplicate(light_);
  ASSERT_TRUE(duplicated.ok()) << duplicated.error_message();
  EXPECT_FALSE(session_.document_commands().property_edit_active());
  expect_color(before);
  ASSERT_NE(session_.scene().light(duplicated.value().id), nullptr);
  EXPECT_TRUE(*session_.scene().light(duplicated.value().id) == light_before);
  ASSERT_TRUE(editor_.history().undo().ok());
  expect_color(before);
  ASSERT_TRUE(editor_.history().redo().ok());
  EXPECT_TRUE(*session_.scene().light(duplicated.value().id) == light_before);
}

TEST_P(SceneColorPreviewTests, InvalidStructuralRequestsKeepThePreviewAndHistory) {
  const auto begun = begin();
  ASSERT_TRUE(begun.ok());
  const quader::QVec3 preview{0.125f, 0.25f, 0.5f};
  ASSERT_TRUE(update(begun.value(), preview).ok());
  const auto revision = session_.scene().revision();
  const auto undo_count = session_.history().undo_count();
  const auto missing = std::numeric_limits<quader::EntityId>::max();
  EXPECT_FALSE(editor_.scene().duplicate(missing).ok());
  EXPECT_FALSE(editor_.scene().create_point_light(
      {.parent = missing, .select_created = true}).ok());
  EXPECT_FALSE(editor_.scene().create_directional_light(
      {.parent = missing, .select_created = true}).ok());
  EXPECT_TRUE(session_.document_commands().property_edit_active());
  expect_color(preview);
  EXPECT_EQ(session_.scene().revision(), revision);
  EXPECT_EQ(session_.history().undo_count(), undo_count);
  ASSERT_TRUE(finish(begun.value(), true).ok());
}

TEST_P(SceneColorPreviewTests, GroupAndUngroupCancelOnlySuccessfulStructuralEdits) {
  ASSERT_TRUE(editor_.scene().select(light_).ok());
  const auto before = color();
  const auto begun = begin();
  ASSERT_TRUE(begun.ok());
  const quader::QVec3 preview{0.125f, 0.25f, 0.5f};
  ASSERT_TRUE(update(begun.value(), preview).ok());
  quader::QuaderTransform invalid;
  invalid.scale = {0.0f, 0.0f, 0.0f};
  EXPECT_FALSE(editor_.scene().group_selection("Invalid group", invalid).ok());
  EXPECT_TRUE(session_.document_commands().property_edit_active());
  expect_color(preview);
  const auto grouped = editor_.scene().group_selection("Light group");
  ASSERT_TRUE(grouped.ok()) << grouped.error_message();
  EXPECT_FALSE(session_.document_commands().property_edit_active());
  expect_color(before);
  const auto ungroup_edit = begin();
  ASSERT_TRUE(ungroup_edit.ok());
  ASSERT_TRUE(update(ungroup_edit.value(), preview).ok());
  ASSERT_TRUE(editor_.scene().ungroup_selection().ok());
  EXPECT_FALSE(session_.document_commands().property_edit_active());
  expect_color(before);
  ASSERT_TRUE(editor_.history().undo().ok());
  expect_color(before);
}

TEST_P(SceneColorPreviewTests, FailedGroupPublicationKeepsCancelOutsideScalarRollback) {
  ASSERT_TRUE(editor_.scene().select(light_).ok());
  session_.mark_clean();
  const auto before = color();
  const auto parent = session_.scene().parent(light_);
  const auto begun = begin();
  ASSERT_TRUE(begun.ok());
  ASSERT_TRUE(update(begun.value(), {0.125f, 0.25f, 0.5f}).ok());
  const auto preview_revision = session_.scene().revision();
  const auto undo_count = session_.history().undo_count();
  session_.document_commands().set_selection_publication([]() {
    return quader::document_command_invalid_request_result("Injected selection failure");
  });
  const auto failed = editor_.scene().group_selection("Rejected group");
  EXPECT_FALSE(failed.ok());
  EXPECT_TRUE(failed.changed());
  EXPECT_FALSE(session_.document_commands().property_edit_active());
  expect_color(before);
  EXPECT_EQ(session_.scene().parent(light_), parent);
  EXPECT_EQ(session_.scene().primary_selection(), light_);
  EXPECT_GT(session_.scene().revision(), preview_revision);
  EXPECT_EQ(session_.history().undo_count(), undo_count);
  EXPECT_FALSE(session_.dirty());
}

TEST_P(SceneColorPreviewTests, FailedUngroupPublicationKeepsCancelOutsideScalarRollback) {
  ASSERT_TRUE(editor_.scene().select(light_).ok());
  const auto grouped = editor_.scene().group_selection("Existing group");
  ASSERT_TRUE(grouped.ok());
  session_.mark_clean();
  const auto before = color();
  const auto parent = session_.scene().parent(light_);
  const auto begun = begin();
  ASSERT_TRUE(begun.ok());
  ASSERT_TRUE(update(begun.value(), {0.125f, 0.25f, 0.5f}).ok());
  const auto preview_revision = session_.scene().revision();
  const auto undo_count = session_.history().undo_count();
  session_.document_commands().set_selection_publication([]() {
    return quader::document_command_invalid_request_result("Injected selection failure");
  });
  const auto failed = editor_.scene().ungroup_selection();
  EXPECT_FALSE(failed.ok());
  EXPECT_EQ(failed.error_message(), "Injected selection failure");
  EXPECT_TRUE(failed.changed());
  EXPECT_FALSE(session_.document_commands().property_edit_active());
  expect_color(before);
  EXPECT_EQ(session_.scene().parent(light_), parent);
  EXPECT_EQ(session_.scene().primary_selection(), parent);
  EXPECT_GT(session_.scene().revision(), preview_revision);
  EXPECT_EQ(session_.history().undo_count(), undo_count);
  EXPECT_FALSE(session_.dirty());
}

INSTANTIATE_TEST_SUITE_P(LightAndEnvironment, SceneColorPreviewTests,
                        ::testing::Bool());

TEST(SceneLightColorPreviewTests, CancelRestoresModeAndPresetWhileRetainingKelvin) {
  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto created = editor.scene().create_point_light();
  ASSERT_TRUE(created.ok());
  const auto entity = created.value().id;
  auto light = editor.query().entity(entity)->light;
  light.color_mode = quader::QuaderLightColorMode::ColorTemperature;
  light.source_preset = quader::QuaderLightSourcePreset::Candle;
  light.color_temperature_kelvin = 1900.0f;
  ASSERT_TRUE(editor.scene().set_light_component(entity, light).ok());
  session.mark_clean();
  const auto begun = editor.scene().begin_light_color_edit(entity);
  ASSERT_TRUE(begun.ok());
  ASSERT_TRUE(editor.scene().update_light_color_edit(begun.value(), {0.125f, 0.25f, 0.5f}).ok());
  auto preview = editor.query().entity(entity)->light;
  EXPECT_EQ(preview.color_mode, quader::QuaderLightColorMode::Color);
  EXPECT_EQ(preview.source_preset, quader::QuaderLightSourcePreset::Custom);
  EXPECT_FLOAT_EQ(preview.color_temperature_kelvin, 1900.0f);
  ASSERT_TRUE(editor.scene().finish_light_color_edit(begun.value(), true).ok());
  const auto restored = editor.query().entity(entity)->light;
  EXPECT_EQ(restored.color_mode, light.color_mode);
  EXPECT_EQ(restored.source_preset, light.source_preset);
  EXPECT_FLOAT_EQ(restored.color_temperature_kelvin, 1900.0f);
  EXPECT_FALSE(session.dirty());
}

TEST(SceneLightColorPreviewTests, InvalidLightPropertiesDoNotCommitTheColorPreview) {
  for (const bool invalid_metadata : {false, true}) {
    quader::SceneSession session;
    quader::QuaderEditorApi editor(
        quader::QuaderApiSessionInternalAccess::from_scene_session(session));
    const auto created = editor.scene().create_point_light();
    ASSERT_TRUE(created.ok());
    const auto entity = created.value().id;
    ASSERT_TRUE(editor.scene().rename(entity, "Renamed").ok());
    ASSERT_TRUE(editor.history().undo().ok());
    session.mark_clean();
    const auto before = editor.query().entity(entity)->light;
    const auto begun = editor.scene().begin_light_color_edit(entity);
    ASSERT_TRUE(begun.ok());
    ASSERT_TRUE(editor.scene().update_light_color_edit(
        begun.value(), {0.125f, 0.25f, 0.5f}).ok());
    const auto revision = session.scene().revision();
    const auto undo_count = session.history().undo_count();
    const auto redo_count = session.history().redo_count();
    ASSERT_GT(redo_count, 0u);
    auto invalid = editor.query().entity(entity)->light;
    if (invalid_metadata) {
      invalid.color_temperature_kelvin = std::numeric_limits<float>::quiet_NaN();
    } else {
      invalid.distance_attenuation = std::numeric_limits<float>::quiet_NaN();
    }
    EXPECT_FALSE(editor.scene().set_light_component(entity, invalid).ok());
    EXPECT_TRUE(session.document_commands().property_edit_active());
    EXPECT_EQ(session.scene().revision(), revision);
    EXPECT_EQ(session.history().undo_count(), undo_count);
    EXPECT_EQ(session.history().redo_count(), redo_count);
    EXPECT_FALSE(session.dirty());
    ASSERT_TRUE(editor.scene().finish_light_color_edit(begun.value(), true).ok());
    const auto restored = editor.query().entity(entity)->light;
    EXPECT_FLOAT_EQ(restored.color.x, before.color.x);
    EXPECT_FLOAT_EQ(restored.color.y, before.color.y);
    EXPECT_FLOAT_EQ(restored.color.z, before.color.z);
    EXPECT_TRUE(editor.history().can_redo());
  }
}

} // namespace
