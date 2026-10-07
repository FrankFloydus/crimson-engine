////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_api_type_conversions.hpp"
#include "api/quader_editor_api.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/viewport/quader_viewport_api_runtime_access.hpp"

#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <filesystem>
#include <limits>

namespace quader {
namespace {

constexpr QVec3 kWireframeTestColor{0.25f, 0.5f, 0.75f};

class WireframeOnShadedTests : public testing::Test {
protected:
  WireframeOnShadedTests()
      : editor_(QuaderApiSessionInternalAccess::from_scene_session(session_)) {}

  QuaderModelingObjectRef create_box() {
    const auto created = editor_.modeling().create_box(
        {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        "Wireframe Mesh");
    EXPECT_TRUE(created.ok()) << created.error_message();
    return created.ok() ? created.value() : QuaderModelingObjectRef{};
  }

  void expect_color_override(EntityId entity) const {
    const auto *mesh = session_.scene().mesh(entity);
    ASSERT_NE(mesh, nullptr);
    ASSERT_TRUE(mesh->wireframe_on_shaded_color.has_value());
    EXPECT_EQ(mesh->wireframe_on_shaded_color->x, kWireframeTestColor.x);
    EXPECT_EQ(mesh->wireframe_on_shaded_color->y, kWireframeTestColor.y);
    EXPECT_EQ(mesh->wireframe_on_shaded_color->z, kWireframeTestColor.z);
  }

  SceneSession session_;
  QuaderEditorApi editor_;
};

TEST_F(WireframeOnShadedTests, MasterDefaultsOffAndChangesOnlyRequestedPane) {
  constexpr auto kControl = QuaderViewportControl::WireframeOnShaded;
  constexpr auto kFirst = ViewportPaneId::Perspective;
  constexpr auto kSecond = ViewportPaneId::Top;
  auto &viewports = editor_.viewports();
  EXPECT_FALSE(ViewportSettings::defaults().bool_value(
      ViewportBoolSettingId::WireframeOnShaded));
  EXPECT_FALSE(viewports.control(kFirst, kControl));
  EXPECT_FALSE(viewports.control(kSecond, kControl));
  const auto history_revision = session_.history().revision();
  const auto second_revision = viewports.control_revision(kSecond);
  ASSERT_TRUE(viewports.set_control(kFirst, kControl, true).changed());
  const auto first_revision = viewports.control_revision(kFirst);
  EXPECT_TRUE(viewports.control(kFirst, kControl));
  EXPECT_FALSE(viewports.control(kSecond, kControl));
  EXPECT_EQ(viewports.control_revision(kSecond), second_revision);
  EXPECT_FALSE(viewports.set_control(kFirst, kControl, true).changed());
  EXPECT_EQ(viewports.control_revision(kFirst), first_revision);
  ASSERT_TRUE(viewports.set_layout(ViewportLayoutId::FourQuad).ok());
  ASSERT_TRUE(viewports.focus_pane(kSecond).ok());
  EXPECT_FALSE(viewports.control(kControl));
  ASSERT_TRUE(viewports.set_layout(ViewportLayoutId::Single).ok());
  EXPECT_TRUE(viewports.control(kFirst, kControl));
  EXPECT_FALSE(viewports.control(kSecond, kControl));
  EXPECT_EQ(session_.history().revision(), history_revision);
}

TEST_F(WireframeOnShadedTests, PaneMasterSurvivesResidentSceneSwitches) {
  constexpr auto kControl = QuaderViewportControl::WireframeOnShaded;
  QuaderViewportApi viewports;
  ViewportWorkspaceService first;
  ViewportWorkspaceService second;
  QuaderViewportApiRuntimeAccess::bind_scene_workspace(viewports, first);
  ASSERT_TRUE(viewports.set_control(kControl, true).changed());
  const auto revision = viewports.control_revision();
  QuaderViewportApiRuntimeAccess::bind_scene_workspace(viewports, second);
  EXPECT_FALSE(viewports.control(kControl));
  QuaderViewportApiRuntimeAccess::bind_scene_workspace(viewports, first);
  EXPECT_TRUE(viewports.control(kControl));
  EXPECT_EQ(viewports.control_revision(), revision);
}

TEST_F(WireframeOnShadedTests, CommandTogglesFocusedPaneInObjectAndEditModes) {
  constexpr auto kControl = QuaderViewportControl::WireframeOnShaded;
  constexpr auto kCommand = quader_command::kViewportToggleWireframeOnShaded;
  constexpr auto kFirst = ViewportPaneId::Perspective;
  constexpr auto kSecond = ViewportPaneId::Top;
  const auto object = create_box();
  ASSERT_TRUE(editor_.selection().select_object(object).ok());
  ASSERT_TRUE(editor_.viewports().set_layout(ViewportLayoutId::FourQuad).ok());
  for (const auto mode : {QuaderEditorMode::Object, QuaderEditorMode::Edit}) {
    ASSERT_TRUE(editor_.mode().set(mode).ok());
    const auto generation = editor_.mode().state().generation;
    const auto history_revision = session_.history().revision();
    for (const auto pane : {kFirst, kSecond}) {
      SCOPED_TRACE(static_cast<int>(pane));
      ASSERT_TRUE(editor_.viewports().focus_pane(pane).ok());
      const auto other = pane == kFirst ? kSecond : kFirst;
      const bool before = editor_.viewports().control(pane, kControl);
      const bool other_before = editor_.viewports().control(other, kControl);
      const auto descriptor = editor_.commands().descriptor(kCommand);
      ASSERT_TRUE(descriptor.has_value());
      EXPECT_TRUE(descriptor->enabled);
      const auto toggled = editor_.commands().execute(kCommand);
      ASSERT_TRUE(toggled.ok()) << toggled.error_message();
      EXPECT_TRUE(toggled.changed());
      EXPECT_EQ(editor_.viewports().control(pane, kControl), !before);
      EXPECT_EQ(editor_.viewports().control(other, kControl), other_before);
      EXPECT_EQ(editor_.mode().current(), mode);
      EXPECT_EQ(editor_.mode().state().generation, generation);
      EXPECT_EQ(session_.history().revision(), history_revision);
    }
  }
}

TEST_F(WireframeOnShadedTests, CommandHasNoDefaultShortcutAndRetainsUserOverride) {
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  const std::filesystem::path path(
      directory.filePath(QStringLiteral("bindings.json")).toStdWString());
  constexpr auto kCommand = quader_command::kViewportToggleWireframeOnShaded;
  ASSERT_TRUE(editor_.commands().set_keybinding_storage_path(path).ok());
  EXPECT_TRUE(editor_.commands().shortcut_for(kCommand).empty());
  ASSERT_TRUE(editor_.commands().set_shortcut(kCommand, "Ctrl+Alt+F21").ok());
  {
    auto reloaded = create_standalone_editor();
    ASSERT_TRUE(reloaded.editor().commands().set_keybinding_storage_path(path).ok());
    EXPECT_EQ(reloaded.editor().commands().shortcut_for(kCommand), "Ctrl+Alt+F21");
    ASSERT_TRUE(reloaded.editor().commands().reset_shortcut(kCommand).ok());
  }
  auto reset = create_standalone_editor();
  ASSERT_TRUE(reset.editor().commands().set_keybinding_storage_path(path).ok());
  EXPECT_TRUE(reset.editor().commands().shortcut_for(kCommand).empty());
}

TEST_F(WireframeOnShadedTests, MeshDefaultsOnAndBatchEditsShareOneHistoryStep) {
  const auto first = create_box();
  const auto second = create_box();
  ASSERT_NE(session_.scene().mesh(first.entity), nullptr);
  ASSERT_NE(session_.scene().mesh(second.entity), nullptr);
  EXPECT_TRUE(MeshComponent{}.wireframe_on_shaded);
  EXPECT_TRUE(QuaderMeshComponent{}.wireframe_on_shaded);
  EXPECT_FALSE(MeshComponent{}.wireframe_on_shaded_color.has_value());
  EXPECT_FALSE(QuaderMeshComponent{}.wireframe_on_shaded_color.has_value());
  EXPECT_TRUE(session_.scene().mesh(first.entity)->wireframe_on_shaded);
  auto first_changed = to_api_mesh_component(*session_.scene().mesh(first.entity));
  auto second_changed = to_api_mesh_component(*session_.scene().mesh(second.entity));
  first_changed.wireframe_on_shaded = false;
  second_changed.wireframe_on_shaded = false;
  first_changed.wireframe_on_shaded_color = kWireframeTestColor;
  second_changed.wireframe_on_shaded_color = kWireframeTestColor;
  const auto undo_count_before = session_.history().undo_count();
  const auto geometry_before = editor_.object(first).compiled_mesh();
  ASSERT_TRUE(geometry_before.has_value());
  const auto changed = editor_.scene().batch("Wireframe on Shaded")
                           .set_mesh_component(first.entity, first_changed)
                           .set_mesh_component(second.entity, second_changed)
                           .commit();
  ASSERT_TRUE(changed.ok()) << changed.error_message();
  ASSERT_TRUE(changed.changed());
  EXPECT_FALSE(session_.scene().mesh(first.entity)->wireframe_on_shaded);
  EXPECT_FALSE(session_.scene().mesh(second.entity)->wireframe_on_shaded);
  expect_color_override(first.entity);
  expect_color_override(second.entity);
  EXPECT_EQ(session_.history().undo_count(), undo_count_before + 1u);
  EXPECT_FALSE(editor_.scene().set_mesh_component(first.entity, first_changed).changed());
  ASSERT_TRUE(editor_.history().undo().ok());
  EXPECT_TRUE(session_.scene().mesh(first.entity)->wireframe_on_shaded);
  EXPECT_TRUE(session_.scene().mesh(second.entity)->wireframe_on_shaded);
  EXPECT_FALSE(session_.scene().mesh(first.entity)->wireframe_on_shaded_color.has_value());
  EXPECT_FALSE(session_.scene().mesh(second.entity)->wireframe_on_shaded_color.has_value());
  ASSERT_TRUE(editor_.history().redo().ok());
  EXPECT_FALSE(session_.scene().mesh(first.entity)->wireframe_on_shaded);
  EXPECT_FALSE(session_.scene().mesh(second.entity)->wireframe_on_shaded);
  expect_color_override(first.entity);
  expect_color_override(second.entity);
  const auto geometry_after = editor_.object(first).compiled_mesh();
  ASSERT_TRUE(geometry_after.has_value());
  EXPECT_EQ(geometry_after->vertices.size(), geometry_before->vertices.size());
  EXPECT_EQ(geometry_after->indices, geometry_before->indices);
}

TEST_F(WireframeOnShadedTests, DuplicateClipboardAndTheirHistoryKeepMeshPreference) {
  const auto source = create_box();
  auto mesh = to_api_mesh_component(*session_.scene().mesh(source.entity));
  mesh.wireframe_on_shaded = false;
  mesh.wireframe_on_shaded_color = kWireframeTestColor;
  ASSERT_TRUE(editor_.scene().set_mesh_component(source.entity, mesh).ok());
  const auto duplicate = editor_.scene().duplicate(source.entity);
  ASSERT_TRUE(duplicate.ok()) << duplicate.error_message();
  ASSERT_NE(session_.scene().mesh(duplicate.value().id), nullptr);
  EXPECT_FALSE(session_.scene().mesh(duplicate.value().id)->wireframe_on_shaded);
  expect_color_override(duplicate.value().id);
  ASSERT_TRUE(editor_.history().undo().ok());
  EXPECT_EQ(session_.scene().mesh(duplicate.value().id), nullptr);
  ASSERT_TRUE(editor_.history().redo().ok());
  ASSERT_NE(session_.scene().mesh(duplicate.value().id), nullptr);
  EXPECT_FALSE(session_.scene().mesh(duplicate.value().id)->wireframe_on_shaded);
  expect_color_override(duplicate.value().id);

  ASSERT_TRUE(editor_.selection().select_object(source).ok());
  ASSERT_TRUE(editor_.modeling().copy_selection().ok());
  ASSERT_TRUE(editor_.modeling().paste_selection().ok());
  const auto selected = session_.scene().selected_entities();
  ASSERT_EQ(selected.size(), 1u);
  const EntityId pasted = selected.front();
  ASSERT_NE(pasted, source.entity);
  ASSERT_NE(pasted, duplicate.value().id);
  ASSERT_NE(session_.scene().mesh(pasted), nullptr);
  EXPECT_FALSE(session_.scene().mesh(pasted)->wireframe_on_shaded);
  expect_color_override(pasted);
  ASSERT_TRUE(editor_.history().undo().ok());
  EXPECT_EQ(session_.scene().mesh(pasted), nullptr);
  ASSERT_TRUE(editor_.history().redo().ok());
  ASSERT_NE(session_.scene().mesh(pasted), nullptr);
  EXPECT_FALSE(session_.scene().mesh(pasted)->wireframe_on_shaded);
  expect_color_override(pasted);
}

TEST_F(WireframeOnShadedTests, InstanceCreationAndFlattenKeepAuthoredMeshPreference) {
  const auto source = create_box();
  auto mesh = to_api_mesh_component(*session_.scene().mesh(source.entity));
  mesh.wireframe_on_shaded = false;
  mesh.wireframe_on_shaded_color = kWireframeTestColor;
  ASSERT_TRUE(editor_.scene().set_mesh_component(source.entity, mesh).ok());
  ASSERT_TRUE(editor_.selection().select_object(source).ok());
  const auto created = editor_.modeling().create_instance_from_selection();
  ASSERT_TRUE(created.ok()) << created.error_message();
  ASSERT_EQ(created.value().definition.members.size(), 1u);
  EXPECT_EQ(created.value().definition.members.front().id, source.entity);
  EXPECT_FALSE(session_.scene().mesh(source.entity)->wireframe_on_shaded);
  expect_color_override(source.entity);
  ASSERT_TRUE(editor_.history().undo().ok());
  ASSERT_NE(session_.scene().mesh(source.entity), nullptr);
  EXPECT_FALSE(session_.scene().mesh(source.entity)->wireframe_on_shaded);
  expect_color_override(source.entity);
  ASSERT_TRUE(editor_.history().redo().ok());
  const auto flattened = editor_.modeling().flatten_selected_instances();
  ASSERT_TRUE(flattened.ok()) << flattened.error_message();
  ASSERT_EQ(flattened.value().root_entities.size(), 1u);
  const EntityId flattened_mesh = flattened.value().root_entities.front().id;
  ASSERT_NE(session_.scene().mesh(flattened_mesh), nullptr);
  EXPECT_FALSE(session_.scene().mesh(flattened_mesh)->wireframe_on_shaded);
  expect_color_override(flattened_mesh);
  ASSERT_TRUE(editor_.history().undo().ok());
  ASSERT_TRUE(editor_.history().redo().ok());
  ASSERT_NE(session_.scene().mesh(flattened_mesh), nullptr);
  EXPECT_FALSE(session_.scene().mesh(flattened_mesh)->wireframe_on_shaded);
  expect_color_override(flattened_mesh);
}

TEST_F(WireframeOnShadedTests, PublicSceneSaveOpenPreservesMeshPreferenceAndResetsMaster) {
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  const std::filesystem::path path(
      directory.filePath(QStringLiteral("wireframe.qdr")).toStdWString());
  const auto source = create_box();
  auto mesh = to_api_mesh_component(*session_.scene().mesh(source.entity));
  mesh.wireframe_on_shaded = false;
  mesh.wireframe_on_shaded_color = kWireframeTestColor;
  ASSERT_TRUE(editor_.scene().set_mesh_component(source.entity, mesh).ok());
  ASSERT_TRUE(editor_.viewports().set_control(
      QuaderViewportControl::WireframeOnShaded, true).ok());
  const auto saved = editor_.scene_documents().save_as_native_path(
      *editor_.scene_documents().active_scene_id(), path);
  ASSERT_TRUE(saved.ok()) << saved.error_message();
  SceneSession reopened;
  QuaderEditorApi restored(QuaderApiSessionInternalAccess::from_scene_session(reopened));
  const auto opened = restored.scene_documents().open_native_path(path);
  ASSERT_TRUE(opened.ok()) << opened.error_message();
  ASSERT_NE(reopened.scene().mesh(source.entity), nullptr);
  EXPECT_FALSE(reopened.scene().mesh(source.entity)->wireframe_on_shaded);
  EXPECT_EQ(*reopened.scene().mesh(source.entity), to_scene_mesh_component(mesh));
  EXPECT_FALSE(restored.viewports().control(QuaderViewportControl::WireframeOnShaded));
  EXPECT_FALSE(restored.project().dirty());
}

TEST_F(WireframeOnShadedTests, ComponentPreviewCancelCommitAndHistoryKeepMeshPreference) {
  const auto source = create_box();
  auto mesh = to_api_mesh_component(*session_.scene().mesh(source.entity));
  mesh.wireframe_on_shaded = false;
  mesh.wireframe_on_shaded_color = kWireframeTestColor;
  ASSERT_TRUE(editor_.scene().set_mesh_component(source.entity, mesh).ok());
  ASSERT_TRUE(editor_.selection().select_object(source).ok());
  ASSERT_TRUE(editor_.mode().set(QuaderEditorMode::Edit).ok());
  ASSERT_TRUE(editor_.selection().select_vertex(source, 1).ok());
  const auto expect_preference = [&] {
    const auto *current = session_.scene().mesh(source.entity);
    ASSERT_NE(current, nullptr);
    EXPECT_FALSE(current->wireframe_on_shaded);
    expect_color_override(source.entity);
    EXPECT_EQ(editor_.mode().current(), QuaderEditorMode::Edit);
    ASSERT_EQ(editor_.mode().state().participants.size(), 1u);
  };
  const auto history_before = session_.history().revision();
  ASSERT_TRUE(editor_.modeling().begin_transform_preview().ok());
  ASSERT_TRUE(editor_.modeling().update_transform_translate({0.0f, 0.0f, 1.0f}).ok());
  expect_preference();
  const auto preview_revision = session_.modeling().transient_transform_preview_revision();
  ASSERT_TRUE(session_.modeling().transient_transform_preview_active());
  auto invalid = mesh;
  invalid.wireframe_on_shaded_color =
      QVec3{std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f};
  const auto rejected = editor_.scene().set_mesh_component(source.entity, invalid);
  EXPECT_FALSE(rejected.ok());
  EXPECT_FALSE(rejected.changed());
  EXPECT_TRUE(session_.modeling().transient_transform_preview_active());
  EXPECT_EQ(session_.modeling().transient_transform_preview_revision(), preview_revision);
  expect_preference();
  ASSERT_TRUE(editor_.modeling().cancel_transform_preview().ok());
  expect_preference();
  EXPECT_EQ(session_.history().revision(), history_before);
  ASSERT_TRUE(editor_.modeling().begin_transform_preview().ok());
  ASSERT_TRUE(editor_.modeling().update_transform_translate({0.0f, 0.0f, -1.0f}).ok());
  expect_preference();
  ASSERT_TRUE(editor_.modeling().commit_transform_preview("Move Vertex").ok());
  expect_preference();
  ASSERT_TRUE(editor_.history().undo().ok());
  expect_preference();
  ASSERT_TRUE(editor_.history().redo().ok());
  expect_preference();
}

TEST_F(WireframeOnShadedTests, ExplicitBlackAndResetToInheritedColorAreDistinctHistoryStates) {
  const auto source = create_box();
  const auto original = *session_.scene().mesh(source.entity);
  auto mesh = to_api_mesh_component(original);
  EXPECT_FALSE(mesh.wireframe_on_shaded_color.has_value());
  mesh.wireframe_on_shaded_color = QVec3{};
  ASSERT_TRUE(editor_.scene().set_mesh_component(source.entity, mesh).changed());
  ASSERT_TRUE(session_.scene().mesh(source.entity)->wireframe_on_shaded_color.has_value());
  const auto black = *session_.scene().mesh(source.entity);
  EXPECT_NE(black, original);
  const auto revision = session_.scene().render_component_revision();
  const auto history_revision = session_.history().revision();
  EXPECT_FALSE(editor_.scene().set_mesh_component(source.entity, mesh).changed());
  EXPECT_EQ(session_.scene().render_component_revision(), revision);
  EXPECT_EQ(session_.history().revision(), history_revision);
  mesh.wireframe_on_shaded_color.reset();
  ASSERT_TRUE(editor_.scene().set_mesh_component(source.entity, mesh).changed());
  EXPECT_EQ(*session_.scene().mesh(source.entity), original);
  ASSERT_TRUE(editor_.history().undo().ok());
  EXPECT_EQ(*session_.scene().mesh(source.entity), black);
  ASSERT_TRUE(editor_.history().redo().ok());
  EXPECT_EQ(*session_.scene().mesh(source.entity), original);
}

TEST_F(WireframeOnShadedTests, InvalidColorsRejectDirectAndBatchEditsAtomically) {
  const auto first = create_box();
  const auto second = create_box();
  const auto first_before = *session_.scene().mesh(first.entity);
  const auto second_before = *session_.scene().mesh(second.entity);
  auto valid = to_api_mesh_component(first_before);
  valid.wireframe_on_shaded_color = kWireframeTestColor;
  constexpr float kNan = std::numeric_limits<float>::quiet_NaN();
  constexpr float kInfinity = std::numeric_limits<float>::infinity();
  for (const QVec3 color : {QVec3{-0.01f, 0.0f, 0.0f}, QVec3{0.0f, 1.01f, 0.0f},
                           QVec3{0.0f, 0.0f, -0.01f}, QVec3{kNan, 0.0f, 0.0f},
                           QVec3{0.0f, kInfinity, 0.0f}, QVec3{0.0f, 0.0f, -kInfinity}}) {
    auto invalid = to_api_mesh_component(second_before);
    invalid.wireframe_on_shaded_color = color;
    const auto scene_revision = session_.scene().render_component_revision();
    const auto history_revision = session_.history().revision();
    const auto direct = editor_.scene().set_mesh_component(second.entity, invalid);
    EXPECT_FALSE(direct.ok());
    EXPECT_FALSE(direct.changed());
    EXPECT_NE(direct.error_message().find("finite RGB"), std::string::npos);
    const auto batch = editor_.scene().batch("Invalid Wire Color")
                           .set_mesh_component(first.entity, valid)
                           .set_mesh_component(second.entity, invalid).commit();
    EXPECT_FALSE(batch.ok());
    EXPECT_FALSE(batch.changed());
    EXPECT_FALSE(session_.scene().set_mesh(second.entity, to_scene_mesh_component(invalid)));
    EXPECT_EQ(*session_.scene().mesh(first.entity), first_before);
    EXPECT_EQ(*session_.scene().mesh(second.entity), second_before);
    EXPECT_EQ(session_.scene().render_component_revision(), scene_revision);
    EXPECT_EQ(session_.history().revision(), history_revision);
  }
}

} // namespace
} // namespace quader
