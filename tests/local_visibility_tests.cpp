////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_editor_api.hpp"
#include "editor/session/project_session.hpp"
#include "editor/viewport/quader_viewport_api_runtime_access.hpp"
#include "viewport/viewport_local_visibility.hpp"
#include "viewport/viewport_workspace_service.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

quader::QuaderModelingObjectRef create_box(quader::QuaderEditorApi &editor,
                                           std::string name) {
  const quader::QuaderApiResult<quader::QuaderModelingObjectRef> result =
      editor.modeling().create_box(
          {.min = {-1.0f, -1.0f, -1.0f},
           .max = {1.0f, 1.0f, 1.0f}},
          std::move(name));
  EXPECT_TRUE(result.ok()) << result.error_message();
  return result.ok() ? result.value() : quader::QuaderModelingObjectRef{};
}

std::optional<quader::QuaderEntitySnapshot>
entity_snapshot(const quader::QuaderEditorApi &editor,
                quader::EntityId entity) {
  return editor.query().entity(entity);
}

std::vector<quader::EntityId>
selected_entities(const quader::QuaderEditorApi &editor) {
  std::vector<quader::EntityId> selected;
  for (const quader::QuaderEntitySnapshot &entity :
       editor.query().entities()) {
    if (entity.selected) {
      selected.push_back(entity.id);
    }
  }
  std::sort(selected.begin(), selected.end());
  return selected;
}

void expect_revisions_equal(const quader::QuaderRevisionStamp &left,
                            const quader::QuaderRevisionStamp &right) {
  EXPECT_EQ(left.project_revision, right.project_revision);
  EXPECT_EQ(left.scene_revision, right.scene_revision);
  EXPECT_EQ(left.scene_layer_revision, right.scene_layer_revision);
  EXPECT_EQ(left.asset_revision, right.asset_revision);
  EXPECT_EQ(left.modeling_content_revision,
            right.modeling_content_revision);
  EXPECT_EQ(left.modeling_selection_revision,
            right.modeling_selection_revision);
}

void expect_camera_state_equal(const quader::ViewportCameraState &left,
                               const quader::ViewportCameraState &right) {
  EXPECT_FLOAT_EQ(left.target.x, right.target.x);
  EXPECT_FLOAT_EQ(left.target.y, right.target.y);
  EXPECT_FLOAT_EQ(left.target.z, right.target.z);
  EXPECT_FLOAT_EQ(left.yaw_radians, right.yaw_radians);
  EXPECT_FLOAT_EQ(left.pitch_radians, right.pitch_radians);
  EXPECT_FLOAT_EQ(left.distance, right.distance);
  EXPECT_EQ(left.projection, right.projection);
  EXPECT_FLOAT_EQ(left.orthographic_size, right.orthographic_size);
  EXPECT_FLOAT_EQ(left.fov_degrees, right.fov_degrees);
  EXPECT_FLOAT_EQ(left.near_plane, right.near_plane);
  EXPECT_FLOAT_EQ(left.far_plane, right.far_plane);
}

/// Owns an isolated temporary project directory for one behavior test.
class ScopedLocalVisibilityProjectDirectory final {
public:
  explicit ScopedLocalVisibilityProjectDirectory(std::string_view label) {
    const auto tick = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    path_ = std::filesystem::temp_directory_path() /
            ("quader-local-visibility-" + std::string(label) + "-" +
             std::to_string(tick));
    std::error_code error;
    std::filesystem::create_directories(path_, error);
    EXPECT_FALSE(error) << error.message();
  }

  ~ScopedLocalVisibilityProjectDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }

  [[nodiscard]] const std::filesystem::path &path() const noexcept {
    return path_;
  }

private:
  std::filesystem::path path_;
};

TEST(LocalVisibilityTests,
     PublicToggleIsRuntimeOnlyAndPreservesSelectionAndPersistentVisibility) {
  quader::QuaderStandaloneEditor standalone =
      quader::create_standalone_editor();
  quader::QuaderEditorApi &editor = standalone.editor();
  const quader::QuaderModelingObjectRef isolated =
      create_box(editor, "Local Isolated");
  const quader::QuaderModelingObjectRef hidden =
      create_box(editor, "Persistently Hidden");
  const quader::QuaderModelingObjectRef other =
      create_box(editor, "Local Other");
  ASSERT_NE(isolated.entity, quader::kInvalidEntityId);
  ASSERT_TRUE(editor.scene().set_visible(hidden.entity, false).ok());
  ASSERT_TRUE(editor.selection().select_object(isolated).ok());

  const bool dirty_before = editor.project().dirty();
  const quader::QuaderRevisionStamp revisions_before = editor.revisions();
  const quader::QuaderHistorySnapshot history_before =
      editor.query().history();
  const std::vector<quader::EntityId> selection_before =
      selected_entities(editor);

  const quader::QuaderApiVoidResult entered =
      editor.viewports().toggle_local_visibility();
  ASSERT_TRUE(entered.ok()) << entered.error_message();
  EXPECT_TRUE(entered.changed());
  EXPECT_EQ(entered.mutation_kind, quader::QuaderMutationKind::Runtime);
  EXPECT_TRUE(editor.viewports().local_visibility_active());

  const quader::ViewportLocalVisibilitySnapshot local =
      quader::QuaderViewportApiRuntimeAccess::workspace(editor.viewports())
          .local_visibility_snapshot();
  EXPECT_TRUE(local.allows(isolated.entity));
  EXPECT_FALSE(local.allows(hidden.entity));
  EXPECT_FALSE(local.allows(other.entity));
  EXPECT_EQ(selected_entities(editor), selection_before);
  EXPECT_EQ(editor.project().dirty(), dirty_before);
  expect_revisions_equal(editor.revisions(), revisions_before);
  EXPECT_EQ(editor.query().history().can_undo, history_before.can_undo);
  EXPECT_EQ(editor.query().history().can_redo, history_before.can_redo);
  EXPECT_EQ(editor.query().history().undo_label, history_before.undo_label);
  EXPECT_EQ(editor.query().history().redo_label, history_before.redo_label);

  const quader::QuaderApiVoidResult exited =
      editor.viewports().toggle_local_visibility();
  ASSERT_TRUE(exited.ok()) << exited.error_message();
  EXPECT_TRUE(exited.changed());
  EXPECT_EQ(exited.mutation_kind, quader::QuaderMutationKind::Runtime);
  EXPECT_FALSE(editor.viewports().local_visibility_active());
  EXPECT_EQ(selected_entities(editor), selection_before);
  const std::optional<quader::QuaderEntitySnapshot> hidden_after =
      entity_snapshot(editor, hidden.entity);
  ASSERT_TRUE(hidden_after.has_value());
  EXPECT_FALSE(hidden_after->visible);
  EXPECT_TRUE(entity_snapshot(editor, other.entity)->visible);
  EXPECT_EQ(editor.project().dirty(), dirty_before);
  expect_revisions_equal(editor.revisions(), revisions_before);
}

TEST(LocalVisibilityTests,
     EmptyComponentSelectionCannotEnterOutsideInstanceEditing) {
  quader::QuaderStandaloneEditor standalone =
      quader::create_standalone_editor();
  quader::QuaderEditorApi &editor = standalone.editor();
  (void)create_box(editor, "Unselected Component Owner");
  ASSERT_TRUE(editor.selection().clear().ok());
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Vertex)
                  .ok());
  ASSERT_FALSE(editor.selection().summary().has_selection);

  EXPECT_FALSE(editor.viewports().can_enter_local_visibility());
  const quader::QuaderApiVoidResult rejected =
      editor.viewports().toggle_local_visibility();
  EXPECT_FALSE(rejected.ok());
  EXPECT_FALSE(rejected.changed());
  EXPECT_FALSE(editor.viewports().local_visibility_active());
}

TEST(LocalVisibilityTests,
     ActiveToggleAlwaysExitsWithEmptyOrChangedSelection) {
  quader::QuaderStandaloneEditor standalone =
      quader::create_standalone_editor();
  quader::QuaderEditorApi &editor = standalone.editor();
  const quader::QuaderModelingObjectRef first =
      create_box(editor, "Toggle First");
  const quader::QuaderModelingObjectRef second =
      create_box(editor, "Toggle Second");

  ASSERT_TRUE(editor.selection().select_objects({first, second}).ok());
  ASSERT_TRUE(editor.viewports().toggle_local_visibility().ok());
  ASSERT_TRUE(editor.selection().clear().ok());
  const quader::QuaderApiVoidResult empty_exit =
      editor.viewports().toggle_local_visibility();
  ASSERT_TRUE(empty_exit.ok()) << empty_exit.error_message();
  EXPECT_TRUE(empty_exit.changed());
  EXPECT_FALSE(editor.viewports().local_visibility_active());
  EXPECT_TRUE(selected_entities(editor).empty());

  ASSERT_TRUE(editor.selection().select_objects({first, second}).ok());
  ASSERT_TRUE(editor.viewports().toggle_local_visibility().ok());
  ASSERT_TRUE(editor.selection().select_object(second).ok());
  EXPECT_EQ(selected_entities(editor),
            std::vector<quader::EntityId>{second.entity});
  const quader::QuaderApiVoidResult changed_exit =
      editor.viewports().toggle_local_visibility();
  ASSERT_TRUE(changed_exit.ok()) << changed_exit.error_message();
  EXPECT_TRUE(changed_exit.changed());
  EXPECT_FALSE(editor.viewports().local_visibility_active());
  EXPECT_EQ(selected_entities(editor),
            std::vector<quader::EntityId>{second.entity});
}

TEST(LocalVisibilityTests,
     NewEntitiesImmediatelyJoinAndAreSelectableWithoutAStateQuery) {
  quader::QuaderStandaloneEditor standalone =
      quader::create_standalone_editor();
  quader::QuaderEditorApi &editor = standalone.editor();
  const quader::QuaderModelingObjectRef isolated =
      create_box(editor, "Lifecycle Isolated");
  (void)create_box(editor, "Lifecycle Other");
  ASSERT_TRUE(editor.selection().select_object(isolated).ok());
  ASSERT_TRUE(editor.viewports().toggle_local_visibility().ok());

  const quader::QuaderApiResult<quader::QuaderEntityRef> created =
      editor.scene().create_entity({.name = "Created While Local"});
  ASSERT_TRUE(created.ok()) << created.error_message();
  const quader::QuaderApiVoidResult selected =
      editor.selection().select_object(created.value().id);
  ASSERT_TRUE(selected.ok()) << selected.error_message();
  EXPECT_EQ(selected_entities(editor),
            std::vector<quader::EntityId>{created.value().id});

  const quader::ViewportLocalVisibilitySnapshot local =
      quader::QuaderViewportApiRuntimeAccess::workspace(editor.viewports())
          .local_visibility_snapshot();
  EXPECT_TRUE(local.active());
  EXPECT_TRUE(local.allows(created.value().id));
}

TEST(LocalVisibilityTests,
     DeletingLastMemberSynchronouslyExitsRestoresCameraAndUndoStaysGlobal) {
  quader::QuaderStandaloneEditor standalone =
      quader::create_standalone_editor();
  quader::QuaderEditorApi &editor = standalone.editor();
  const quader::QuaderModelingObjectRef isolated =
      create_box(editor, "Deleted Local Member");
  (void)create_box(editor, "Persistent Global Context");
  ASSERT_TRUE(editor.selection().select_object(isolated).ok());

  quader::ViewportWorkspaceService &workspace =
      quader::QuaderViewportApiRuntimeAccess::workspace(editor.viewports());
  workspace.clear_pane_visibility();
  workspace.set_pane_visible(quader::ViewportPaneId::Perspective, true);
  workspace.camera(quader::ViewportPaneId::Perspective).focus_origin(73.0f);
  const quader::ViewportCameraState camera_before =
      workspace.camera(quader::ViewportPaneId::Perspective).state();

  ASSERT_TRUE(editor.viewports().toggle_local_visibility().ok());
  ASSERT_TRUE(workspace.local_visibility_active());
  ASSERT_NE(workspace.camera(quader::ViewportPaneId::Perspective)
                .state()
                .distance,
            camera_before.distance);

  const quader::QuaderApiVoidResult deleted_isolated =
      editor.modeling().delete_selection();
  ASSERT_TRUE(deleted_isolated.ok()) << deleted_isolated.error_message();
  EXPECT_FALSE(workspace.local_visibility_active());
  expect_camera_state_equal(
      workspace.camera(quader::ViewportPaneId::Perspective).state(),
      camera_before);

  const quader::QuaderApiVoidResult undone = editor.history().undo();
  ASSERT_TRUE(undone.ok()) << undone.error_message();
  EXPECT_FALSE(workspace.local_visibility_active());
  EXPECT_TRUE(editor.query().entity(isolated.entity).has_value());
  expect_camera_state_equal(
      workspace.camera(quader::ViewportPaneId::Perspective).state(),
      camera_before);
}

TEST(LocalVisibilityTests,
     GroupAndComponentSelectionsResolveToPublicOwningEntities) {
  {
    quader::QuaderStandaloneEditor standalone =
        quader::create_standalone_editor();
    quader::QuaderEditorApi &editor = standalone.editor();
    const quader::QuaderModelingObjectRef left =
        create_box(editor, "Grouped Left");
    const quader::QuaderModelingObjectRef right =
        create_box(editor, "Grouped Right");
    ASSERT_TRUE(editor.selection().select_objects({left, right}).ok());
    const quader::QuaderApiResult<quader::QuaderEntityRef> grouped =
        editor.scene().group_selection("Local Group");
    ASSERT_TRUE(grouped.ok()) << grouped.error_message();
    ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Group)
                    .ok());
    ASSERT_TRUE(editor.viewports().toggle_local_visibility().ok());
    const quader::ViewportLocalVisibilitySnapshot local =
        quader::QuaderViewportApiRuntimeAccess::workspace(editor.viewports())
            .local_visibility_snapshot();
    EXPECT_TRUE(local.allows(grouped.value().id));
    EXPECT_TRUE(local.allows(left.entity));
    EXPECT_TRUE(local.allows(right.entity));
  }

  {
    quader::QuaderStandaloneEditor standalone =
        quader::create_standalone_editor();
    quader::QuaderEditorApi &editor = standalone.editor();
    const quader::QuaderModelingObjectRef component_owner =
        create_box(editor, "Component Owner");
    const quader::QuaderModelingObjectRef other =
        create_box(editor, "Component Other");
    ASSERT_TRUE(editor.selection().select_vertex(component_owner, 1).ok());
    ASSERT_TRUE(editor.viewports().toggle_local_visibility().ok());
    const quader::ViewportLocalVisibilitySnapshot local =
        quader::QuaderViewportApiRuntimeAccess::workspace(editor.viewports())
            .local_visibility_snapshot();
    EXPECT_TRUE(local.allows(component_owner.entity));
    EXPECT_FALSE(local.allows(other.entity));
  }
}

TEST(LocalVisibilityTests,
     ComponentAndGroupBulkSelectionRemainInsideLocalVisibility) {
  {
    quader::QuaderStandaloneEditor standalone =
        quader::create_standalone_editor();
    quader::QuaderEditorApi &editor = standalone.editor();
    const quader::QuaderModelingObjectRef local =
        create_box(editor, "Bulk Component Local");
    const quader::QuaderModelingObjectRef outside =
        create_box(editor, "Bulk Component Outside");
    ASSERT_TRUE(editor.selection().select_vertex(local, 1).ok());
    ASSERT_TRUE(editor.viewports().toggle_local_visibility().ok());

    ASSERT_TRUE(editor.selection().select_all().ok());
    EXPECT_EQ(editor.selection().summary().selected_vertex_count, 8u);
    ASSERT_TRUE(editor.selection().select_inverse().ok());
    EXPECT_EQ(editor.selection().summary().selected_vertex_count, 0u);
    const quader::QuaderApiVoidResult inverse_without_component_scope =
        editor.selection().select_inverse();
    ASSERT_TRUE(inverse_without_component_scope.ok())
        << inverse_without_component_scope.error_message();
    EXPECT_FALSE(inverse_without_component_scope.changed());
    EXPECT_EQ(editor.selection().summary().selected_vertex_count, 0u);

    ASSERT_TRUE(editor.selection().hover_vertex(outside, 1).ok());
    const quader::QuaderApiVoidResult outside_sticky_select_all =
        editor.selection().select_all();
    ASSERT_TRUE(outside_sticky_select_all.ok())
        << outside_sticky_select_all.error_message();
    EXPECT_FALSE(outside_sticky_select_all.changed());
    EXPECT_EQ(editor.selection().summary().selected_vertex_count, 0u);

    ASSERT_TRUE(editor.selection().hover_vertex(local, 1).ok());
    const quader::QuaderApiVoidResult local_sticky_select_all =
        editor.selection().select_all();
    ASSERT_TRUE(local_sticky_select_all.ok())
        << local_sticky_select_all.error_message();
    EXPECT_TRUE(local_sticky_select_all.changed());
    EXPECT_EQ(editor.selection().summary().selected_vertex_count, 8u);
  }

  {
    quader::QuaderStandaloneEditor standalone =
        quader::create_standalone_editor();
    quader::QuaderEditorApi &editor = standalone.editor();
    const quader::QuaderModelingObjectRef local_child =
        create_box(editor, "Bulk Group Local Child");
    ASSERT_TRUE(editor.selection().select_object(local_child).ok());
    const quader::QuaderApiResult<quader::QuaderEntityRef> local_group =
        editor.scene().group_selection("Bulk Local Group");
    ASSERT_TRUE(local_group.ok()) << local_group.error_message();

    const quader::QuaderModelingObjectRef outside_child =
        create_box(editor, "Bulk Group Outside Child");
    ASSERT_TRUE(editor.selection().select_object(outside_child).ok());
    const quader::QuaderApiResult<quader::QuaderEntityRef> outside_group =
        editor.scene().group_selection("Bulk Outside Group");
    ASSERT_TRUE(outside_group.ok()) << outside_group.error_message();

    ASSERT_TRUE(editor.selection().select_object(local_group.value().id).ok());
    ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Group)
                    .ok());
    ASSERT_TRUE(editor.viewports().toggle_local_visibility().ok());
    ASSERT_TRUE(editor.selection().clear().ok());

    ASSERT_TRUE(editor.selection().select_all().ok());
    EXPECT_EQ(selected_entities(editor),
              std::vector<quader::EntityId>{local_group.value().id});
    ASSERT_TRUE(editor.selection().select_inverse().ok());
    EXPECT_TRUE(selected_entities(editor).empty());
    ASSERT_TRUE(editor.selection().select_inverse().ok());
    EXPECT_EQ(selected_entities(editor),
              std::vector<quader::EntityId>{local_group.value().id});
    EXPECT_NE(local_group.value().id, outside_group.value().id);
  }
}

TEST(LocalVisibilityTests,
     DocumentReplacementResetsLocalVisibilityAndRestoresCamera) {
  quader::QuaderStandaloneEditor standalone =
      quader::create_standalone_editor();
  quader::QuaderEditorApi &editor = standalone.editor();
  const quader::QuaderModelingObjectRef isolated =
      create_box(editor, "Replacement Local Member");
  (void)create_box(editor, "Replacement Other");
  ASSERT_TRUE(editor.selection().select_object(isolated).ok());

  quader::ViewportWorkspaceService &workspace =
      quader::QuaderViewportApiRuntimeAccess::workspace(editor.viewports());
  workspace.clear_pane_visibility();
  workspace.set_pane_visible(quader::ViewportPaneId::Perspective, true);
  workspace.camera(quader::ViewportPaneId::Perspective).focus_origin(81.0f);
  const quader::ViewportCameraState camera_before =
      workspace.camera(quader::ViewportPaneId::Perspective).state();
  ASSERT_TRUE(editor.viewports().toggle_local_visibility().ok());

  editor.project().set_runtime_hooks({
      .confirm_dirty_project_replacement =
          [](std::string_view) {
            return quader::QuaderDirtyProjectDecision::Discard;
          },
  });
  const quader::QuaderApiVoidResult replaced = editor.project().new_project();
  ASSERT_TRUE(replaced.ok()) << replaced.error_message();
  EXPECT_FALSE(workspace.local_visibility_active());
  EXPECT_FALSE(editor.viewports().local_visibility_active());
  EXPECT_FALSE(editor.query().entity(isolated.entity).has_value());
  expect_camera_state_equal(
      workspace.camera(quader::ViewportPaneId::Perspective).state(),
      camera_before);
}

TEST(LocalVisibilityTests,
     OpenSceneSessionsKeepIndependentLocalVisibilityAcrossActivation) {
  ScopedLocalVisibilityProjectDirectory temporary("scene-isolation");
  quader::ProjectSession project;
  const quader::ProjectSessionResult created =
      project.create(temporary.path());
  ASSERT_TRUE(created.ok) << created.message;
  ASSERT_TRUE(created.scene_session_id.has_value());
  const quader::SceneSessionId first_scene = *created.scene_session_id;
  quader::QuaderEditorApi editor(project);

  const quader::QuaderModelingObjectRef first_local =
      create_box(editor, "First Scene Local");
  (void)create_box(editor, "First Scene Outside");
  ASSERT_TRUE(editor.selection().select_object(first_local).ok());
  ASSERT_TRUE(editor.viewports().toggle_local_visibility().ok());
  ASSERT_TRUE(editor.viewports().local_visibility_active());

  const quader::QuaderApiResult<quader::SceneSessionId> second_created =
      editor.scene_documents().new_scene();
  ASSERT_TRUE(second_created.ok()) << second_created.error_message();
  const quader::SceneSessionId second_scene = second_created.value();
  EXPECT_FALSE(editor.viewports().local_visibility_active());

  ASSERT_TRUE(editor.scene_documents().activate(first_scene).ok());
  EXPECT_TRUE(editor.viewports().local_visibility_active());
  ASSERT_TRUE(editor.scene_documents().activate(second_scene).ok());
  EXPECT_FALSE(editor.viewports().local_visibility_active());
  ASSERT_TRUE(editor.scene_documents().activate(first_scene).ok());
  EXPECT_TRUE(editor.viewports().local_visibility_active());
}

TEST(LocalVisibilityTests,
     InstanceEditIsolationUsesThePublicOccurrenceAsItsMember) {
  quader::QuaderStandaloneEditor standalone =
      quader::create_standalone_editor();
  quader::QuaderEditorApi &editor = standalone.editor();
  (void)create_box(editor, "Instance Definition Member");
  const quader::QuaderApiResult<quader::QuaderInstanceCreateResult> instance =
      editor.modeling().create_instance_from_selection();
  ASSERT_TRUE(instance.ok()) << instance.error_message();
  const quader::EntityId occurrence = instance.value().occurrence.root.id;
  const quader::EntityId definition = instance.value().definition.root.id;
  const quader::QuaderModelingObjectRef outside =
      create_box(editor, "Instance Edit Outside");

  ASSERT_TRUE(editor.selection().select_object(occurrence).ok());
  ASSERT_TRUE(editor.modeling().enter_instance_edit(occurrence).ok());
  ASSERT_TRUE(editor.viewports().can_enter_local_visibility());
  ASSERT_TRUE(editor.viewports().toggle_local_visibility().ok());

  const quader::ViewportLocalVisibilitySnapshot local =
      quader::QuaderViewportApiRuntimeAccess::workspace(editor.viewports())
          .local_visibility_snapshot();
  EXPECT_TRUE(local.contains_member(occurrence));
  EXPECT_TRUE(local.allows(occurrence));
  EXPECT_FALSE(local.contains_member(definition));
  EXPECT_FALSE(local.allows(outside.entity));
}

} // namespace
