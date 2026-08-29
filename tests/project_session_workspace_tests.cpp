////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/session/project_session.hpp"

#include "api/quader_command_ids.hpp"
#include "api/quader_editor_api.hpp"
#include "editor/commands/document/document_command_result.hpp"
#include "editor/curve/curve_tool_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/selection/editor_selection_service.hpp"
#include "io/qdr/qdr_document_reader.hpp"
#include "scene/scene_transform.hpp"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <Windows.h>
#endif

using namespace quader;

namespace {

std::filesystem::path unique_project_test_directory(std::string_view label) {
  const auto tick = static_cast<std::uint64_t>(
      std::chrono::steady_clock::now().time_since_epoch().count());
  return std::filesystem::temp_directory_path() /
         ("quader-project-session-" + std::string(label) + "-" +
          std::to_string(tick));
}

/// Owns an isolated temporary directory for project-session tests.
class ScopedProjectSessionDirectory final {
public:
  explicit ScopedProjectSessionDirectory(std::string_view label)
      : path_(unique_project_test_directory(label)) {
    std::error_code error;
    std::filesystem::create_directories(path_, error);
    EXPECT_FALSE(error) << error.message();
  }

  ~ScopedProjectSessionDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }

  [[nodiscard]] const std::filesystem::path &path() const noexcept {
    return path_;
  }

  [[nodiscard]] std::filesystem::path make_empty_child(
      std::string_view name) const {
    const std::filesystem::path child = path_ / std::string(name);
    std::error_code error;
    std::filesystem::create_directory(child, error);
    EXPECT_FALSE(error) << error.message();
    return child;
  }

private:
  std::filesystem::path path_;
};

ProjectRelativePath project_path(std::string_view text) {
  ProjectIoResult<ProjectRelativePath> parsed =
      ProjectRelativePath::parse_portable(text);
  EXPECT_TRUE(parsed.ok()) << parsed.error().message;
  return parsed.take_value();
}

std::vector<char> read_file_bytes(const std::filesystem::path &path) {
  std::ifstream stream(path, std::ios::binary);
  EXPECT_TRUE(stream.is_open()) << path.string();
  return {std::istreambuf_iterator<char>(stream),
          std::istreambuf_iterator<char>()};
}

void expect_vec3_eq(QVec3 actual, QVec3 expected) {
  EXPECT_FLOAT_EQ(actual.x, expected.x);
  EXPECT_FLOAT_EQ(actual.y, expected.y);
  EXPECT_FLOAT_EQ(actual.z, expected.z);
}

template <typename ActualVector3, typename ExpectedVector3>
void expect_vec3_near(const ActualVector3 &actual,
                      const ExpectedVector3 &expected,
                      float tolerance = 0.0001f) {
  EXPECT_NEAR(actual.x, expected.x, tolerance);
  EXPECT_NEAR(actual.y, expected.y, tolerance);
  EXPECT_NEAR(actual.z, expected.z, tolerance);
}

/// Temporarily replaces a manifest file with a directory to force write failure.
class ScopedManifestWriteFailure final {
public:
  explicit ScopedManifestWriteFailure(std::filesystem::path manifest)
      : manifest_(std::move(manifest)), backup_(manifest_) {
    backup_ += ".write-failure-backup";
    std::error_code error;
    std::filesystem::rename(manifest_, backup_, error);
    if (error) {
      message_ = error.message();
      return;
    }
    if (!std::filesystem::create_directory(manifest_, error) || error) {
      message_ = error ? error.message()
                       : "Could not create manifest blocker directory.";
      std::error_code ignored;
      std::filesystem::rename(backup_, manifest_, ignored);
      return;
    }
    ready_ = true;
  }

  ~ScopedManifestWriteFailure() {
    if (!ready_) {
      return;
    }
    std::error_code ignored;
    std::filesystem::remove_all(manifest_, ignored);
    std::filesystem::rename(backup_, manifest_, ignored);
  }

  [[nodiscard]] bool ready() const noexcept { return ready_; }
  [[nodiscard]] const std::string &message() const noexcept { return message_; }

private:
  std::filesystem::path manifest_;
  std::filesystem::path backup_;
  std::string message_;
  bool ready_ = false;
};

#if defined(_WIN32)
/// Keeps a manifest readable while denying the later replace/write boundary.
class ScopedManifestWriteLock final {
public:
  explicit ScopedManifestWriteLock(const std::filesystem::path &manifest)
      : handle_(::CreateFileW(manifest.c_str(), GENERIC_READ, FILE_SHARE_READ,
                              nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr)) {}

  ~ScopedManifestWriteLock() {
    if (ready()) {
      ::CloseHandle(handle_);
    }
  }

  [[nodiscard]] bool ready() const noexcept {
    return handle_ != INVALID_HANDLE_VALUE;
  }

private:
  HANDLE handle_ = INVALID_HANDLE_VALUE;
};
#endif

QuaderModelingObjectRef create_saved_transition_curve(
    ProjectSession &project, QuaderEditorApi &editor) {
  const auto curve = editor.modeling().create_curve({
      .points = {{.position = {-2.0f, 0.0f, 0.0f}},
                 {.position = {2.0f, 0.0f, 0.0f}}},
      .name = "Scene Transition Curve",
  });
  EXPECT_TRUE(curve.ok()) << curve.error_message();
  EXPECT_TRUE(editor.scene_documents().save_as(
      project.active_scene_id(), project_path("Scenes/Source.qdr")).ok());
  return curve.value();
}

ProjectRelativePath create_closed_transition_target(QuaderEditorApi &editor) {
  const auto target = editor.scene_documents().new_scene();
  EXPECT_TRUE(target.ok()) << target.error_message();
  const ProjectRelativePath path = project_path("Scenes/Target.qdr");
  EXPECT_TRUE(editor.scene_documents().save_as(target.value(), path).ok());
  EXPECT_TRUE(editor.scene_documents().close(target.value()).ok());
  return path;
}

bool select_and_begin_curve_control_drag(CurveToolService &curve,
                                         CurveControlTarget target) {
  const std::array hits{target};
  (void)curve.edit_control_selection(
      hits, quader_editor_core::SelectionEditOperation::Replace, nullptr,
      target);
  const CurveControlSelection &selection = curve.control_selection();
  return selection.controls.size() == 1u && selection.active == target &&
         curve.begin_selected_control_drag().ok;
}

TEST(ProjectSessionWorkspace,
     NewAndOpenedScenesCancelOutgoingCurveDragsBeforeRebinding) {
  for (const bool open_existing_file : {false, true}) {
    SCOPED_TRACE(open_existing_file ? "Open Scene" : "New Scene");
    ScopedProjectSessionDirectory temporary("curve-transition");
    ProjectSession project;
    const auto created = project.create(temporary.make_empty_child("Project"));
    ASSERT_TRUE(created.ok) << created.message;
    QuaderEditorApi editor(project);
    const auto object = create_saved_transition_curve(project, editor);
    OpenSceneSession *source = project.active_scene();
    ASSERT_NE(source, nullptr);
    const auto source_id = source->session_id;
    const auto target_path = create_closed_transition_target(editor);
    ASSERT_EQ(project.active_scene(), source);
    ASSERT_TRUE(editor.tools().activate(kCurveToolId).ok());
    CurveToolService &curve = source->session->curve();
    ASSERT_TRUE(curve.preview_state().document.has_value());
    const auto before = *curve.preview_state().document;
    const auto source_stamp = source->session->content_revision_stamp();
    ASSERT_NE(source->session->history().undo_label(), nullptr);
    const std::string undo_label = *source->session->history().undo_label();
    ASSERT_TRUE(select_and_begin_curve_control_drag(
        curve, {.point = before.points()[0].id}));
    ASSERT_TRUE(curve.update_drag({-5.0f, 1.0f, 2.0f}).ok);
    ASSERT_TRUE(curve.dragging());
    ASSERT_TRUE(source->session->modeling().curve_document_preview_active());

    const auto switched = open_existing_file
        ? editor.scene_documents().open(target_path)
        : editor.scene_documents().new_scene();
    ASSERT_TRUE(switched.ok()) << switched.error_message();
    EXPECT_NE(project.active_scene_id(), source_id);
    EXPECT_FALSE(curve.active());
    EXPECT_FALSE(curve.dragging());
    EXPECT_FALSE(source->session->modeling().curve_document_preview_active());
    EXPECT_EQ(source->session->content_revision_stamp(), source_stamp);
    EXPECT_FALSE(source->dirty());
    ASSERT_NE(source->session->history().undo_label(), nullptr);
    EXPECT_EQ(*source->session->history().undo_label(), undo_label);

    ASSERT_TRUE(editor.scene_documents().activate(source_id).ok());
    const auto restored = editor.modeling().curve_snapshot(object);
    ASSERT_TRUE(restored.ok()) << restored.error_message();
    EXPECT_TRUE(restored.value().document.has_same_geometry(before));
    ASSERT_TRUE(editor.tools().activate(kCurveToolId).ok());
    ASSERT_TRUE(select_and_begin_curve_control_drag(
        curve, {.point = before.points()[0].id}));
    EXPECT_TRUE(curve.cancel_drag().ok);
  }
}

TEST(ProjectSessionWorkspace,
     FailedNewAndOpenPersistencePreserveOutgoingCurveDragExactly) {
  for (const bool open_existing_file : {false, true}) {
    SCOPED_TRACE(open_existing_file ? "Open Scene" : "New Scene");
    ScopedProjectSessionDirectory temporary("curve-transition-rollback");
    ProjectSession project;
    const auto created = project.create(temporary.make_empty_child("Project"));
    ASSERT_TRUE(created.ok) << created.message;
    QuaderEditorApi editor(project);
    (void)create_saved_transition_curve(project, editor);
    OpenSceneSession *source = project.active_scene();
    ASSERT_NE(source, nullptr);
    const auto target_path = create_closed_transition_target(editor);
    ASSERT_TRUE(editor.tools().activate(kCurveToolId).ok());
    CurveToolService &curve = source->session->curve();
    ASSERT_TRUE(curve.preview_state().document.has_value());
    const auto point = curve.preview_state().document->points()[0].id;
    ASSERT_TRUE(select_and_begin_curve_control_drag(curve, {.point = point}));
    ASSERT_TRUE(curve.update_drag({-5.0f, 1.0f, 2.0f}).ok);
    const auto preview = *curve.preview_state().document;
    const auto stamp = source->session->content_revision_stamp();
    const auto active_id = project.active_scene_id();
    const auto generation = project.activation_generation();
    const auto tab_count = project.open_scenes().size();
    const std::string manifest = ProjectManifestStore::serialize(*project.manifest());

    {
      ScopedManifestWriteFailure blocked(project.manifest_path());
      ASSERT_TRUE(blocked.ready()) << blocked.message();
      const auto failed = open_existing_file
          ? editor.scene_documents().open(target_path)
          : editor.scene_documents().new_scene();
      EXPECT_FALSE(failed.ok());
    }
    EXPECT_EQ(project.active_scene_id(), active_id);
    EXPECT_EQ(project.activation_generation(), generation);
    EXPECT_EQ(project.open_scenes().size(), tab_count);
    EXPECT_EQ(ProjectManifestStore::serialize(*project.manifest()), manifest);
    EXPECT_EQ(source->session->content_revision_stamp(), stamp);
    EXPECT_TRUE(curve.active());
    EXPECT_TRUE(curve.dragging());
    EXPECT_TRUE(source->session->modeling().curve_document_preview_active());
    EXPECT_TRUE(curve.preview_state().document->has_same_geometry(preview));
    EXPECT_TRUE(curve.cancel_drag().ok);
  }
}

TEST(ProjectSessionWorkspace, InvalidSceneReadDoesNotCancelOutgoingCurveDrag) {
  ScopedProjectSessionDirectory temporary("curve-read-rollback");
  ProjectSession project;
  const auto created = project.create(temporary.make_empty_child("Project"));
  ASSERT_TRUE(created.ok) << created.message;
  QuaderEditorApi editor(project);
  (void)create_saved_transition_curve(project, editor);
  OpenSceneSession *source = project.active_scene();
  ASSERT_NE(source, nullptr);
  ASSERT_TRUE(editor.tools().activate(kCurveToolId).ok());
  CurveToolService &curve = source->session->curve();
  ASSERT_TRUE(curve.preview_state().document.has_value());
  ASSERT_TRUE(select_and_begin_curve_control_drag(
      curve, {.point = curve.preview_state().document->points()[0].id}));
  ASSERT_TRUE(curve.update_drag({-5.0f, 1.0f, 2.0f}).ok);
  const auto preview = *curve.preview_state().document;
  const auto stamp = source->session->content_revision_stamp();
  const auto generation = project.activation_generation();
  const auto invalid_path = project_path("Scenes/Invalid.qdr");
  {
    std::ofstream invalid(project.root() / invalid_path.native_path(),
                           std::ios::binary);
    ASSERT_TRUE(invalid.is_open());
    invalid << "Invalid curve scene";
  }
  EXPECT_FALSE(editor.scene_documents().open(invalid_path).ok());
  EXPECT_EQ(project.active_scene(), source);
  EXPECT_EQ(project.activation_generation(), generation);
  EXPECT_EQ(source->session->content_revision_stamp(), stamp);
  EXPECT_TRUE(curve.dragging());
  EXPECT_TRUE(source->session->modeling().curve_document_preview_active());
  EXPECT_TRUE(curve.preview_state().document->has_same_geometry(preview));
  EXPECT_TRUE(curve.cancel_drag().ok);
}

TEST(ProjectSessionWorkspace, KeepsSceneHistoryAndSelectionIndependent) {
  ScopedProjectSessionDirectory temporary("independent-state");
  ProjectSession project;
  const ProjectSessionResult created =
      project.create(temporary.make_empty_child("Independent"));
  ASSERT_TRUE(created.ok) << created.message;
  ASSERT_TRUE(created.scene_session_id.has_value());

  OpenSceneSession *first = project.find(*created.scene_session_id);
  ASSERT_NE(first, nullptr);
  QuaderEditorApi editor(project);
  EXPECT_FALSE(editor.modeling().face_texture_treat_as_one());
  const DocumentCreateEntityResult first_entity =
      first->session->document_commands().create_entity("First Entity");
  ASSERT_TRUE(first_entity.ok) << first_entity.message;
  ASSERT_NE(first_entity.entity, kInvalidEntityId);
  ASSERT_TRUE(first->session->history().can_undo());
  first->session->mark_clean();

  const std::array<EntityId, 1> first_selection{first_entity.entity};
  const DocumentCommandResult selected =
      first->session->selection().set_inspection_scene_selection(
          first_selection, first_entity.entity);
  ASSERT_TRUE(selected.ok) << selected.message;
  EXPECT_FALSE(first->dirty())
      << "Inspection selection must not dirty persisted scene content.";

  const auto first_history_before_toggle = editor.history().snapshot();
  ASSERT_TRUE(editor.modeling().set_face_texture_treat_as_one(true).changed());
  EXPECT_TRUE(editor.modeling().face_texture_treat_as_one());
  EXPECT_FALSE(first->dirty());
  EXPECT_EQ(editor.history().snapshot().undo_label,
            first_history_before_toggle.undo_label);
  EXPECT_EQ(editor.history().snapshot().redo_label,
            first_history_before_toggle.redo_label);

  const auto added = editor.scene_documents().new_scene();
  ASSERT_TRUE(added.ok()) << added.error_message();
  OpenSceneSession *second = project.find(added.value());
  ASSERT_NE(second, nullptr);
  ASSERT_NE(first->session.get(), second->session.get());
  EXPECT_FALSE(editor.modeling().face_texture_treat_as_one());
  EXPECT_TRUE(second->session->selection().state().scene_selection.empty());
  EXPECT_FALSE(second->session->history().can_undo());
  ASSERT_TRUE(editor.modeling().set_face_texture_treat_as_one(true).changed());
  ASSERT_TRUE(editor.modeling().set_face_texture_treat_as_one(false).changed());
  EXPECT_FALSE(second->dirty());
  EXPECT_FALSE(second->session->history().can_undo());

  const DocumentCreateEntityResult second_entity =
      second->session->document_commands().create_entity("Second Entity");
  ASSERT_TRUE(second_entity.ok) << second_entity.message;
  EXPECT_TRUE(second->session->history().can_undo());
  EXPECT_TRUE(second->session->scene().exists(second_entity.entity));

  ASSERT_TRUE(editor.scene_documents().activate(first->session_id).ok());
  EXPECT_TRUE(editor.modeling().face_texture_treat_as_one());
  ASSERT_EQ(first->session->selection().state().scene_selection.size(), 1u);
  EXPECT_EQ(first->session->selection().state().active_entity,
            first_entity.entity);
  const CommandResult selection_undone = first->session->history().undo();
  ASSERT_TRUE(selection_undone.ok) << selection_undone.message;
  EXPECT_TRUE(first->session->scene().exists(first_entity.entity));
  const CommandResult entity_undone = first->session->history().undo();
  ASSERT_TRUE(entity_undone.ok) << entity_undone.message;
  EXPECT_FALSE(first->session->scene().exists(first_entity.entity));
  EXPECT_TRUE(second->session->scene().exists(second_entity.entity));
  EXPECT_TRUE(second->session->history().can_undo());
  ASSERT_TRUE(editor.scene_documents().activate(second->session_id).ok());
  EXPECT_FALSE(editor.modeling().face_texture_treat_as_one());
  ASSERT_TRUE(editor.scene_documents().activate(first->session_id).ok());
  EXPECT_TRUE(editor.modeling().face_texture_treat_as_one());
}

TEST(ProjectSessionWorkspace,
     UvPeelOptionsAndSelectionTokensStayWithinSceneSessions) {
  ScopedProjectSessionDirectory temporary("uv-peel-sessions");
  ProjectSession project;
  const auto created = project.create(temporary.make_empty_child("UvPeel"));
  ASSERT_TRUE(created.ok) << created.message;
  ASSERT_TRUE(created.scene_session_id.has_value());
  OpenSceneSession *first = project.find(*created.scene_session_id);
  ASSERT_NE(first, nullptr);
  QuaderEditorApi editor(project);
  const QuaderUvPeelOptions first_options{
      .axis = QuaderUvPeelAxis::V,
      .world_space = false,
      .u_repeat = 2.0,
      .v_repeat = 3.0,
      .u_offset = -0.25,
      .v_offset = 0.5,
  };
  const auto first_history = editor.history().snapshot();
  ASSERT_TRUE(editor.modeling().set_uv_peel_options(first_options).changed());
  EXPECT_FALSE(first->dirty());
  EXPECT_EQ(editor.history().snapshot().undo_label, first_history.undo_label);
  const auto first_box = editor.modeling().create_box(
      {.min = {-1, -1, -1}, .max = {1, 1, 1}}, "First guide");
  ASSERT_TRUE(first_box.ok()) << first_box.error_message();
  ASSERT_TRUE(editor.selection().select_edge(first_box.value(), 1, 2).ok());
  const auto first_token = editor.modeling().uv_peel_settings().selection_token;

  const auto added = editor.scene_documents().new_scene();
  ASSERT_TRUE(added.ok()) << added.error_message();
  OpenSceneSession *second = project.find(added.value());
  ASSERT_NE(second, nullptr);
  ASSERT_NE(first->session.get(), second->session.get());
  EXPECT_EQ(editor.modeling().uv_peel_settings().options, QuaderUvPeelOptions{});
  const QuaderUvPeelOptions second_options{.u_repeat = 4.0, .v_offset = -2.0};
  ASSERT_TRUE(editor.modeling().set_uv_peel_options(second_options).changed());
  EXPECT_FALSE(second->dirty());
  EXPECT_FALSE(editor.history().can_undo());
  const auto second_box = editor.modeling().create_box(
      {.min = {-1, -1, -1}, .max = {1, 1, 1}}, "Second guide");
  ASSERT_TRUE(second_box.ok()) << second_box.error_message();
  ASSERT_TRUE(editor.selection().select_edge(second_box.value(), 1, 2).ok());
  EXPECT_NE(editor.modeling().uv_peel_settings().selection_token, first_token);
  const auto before_content = second->session->modeling().content_revision();
  const auto before_scene = second->session->scene().revision();
  const auto before_assets = second->session->assets().revision();
  const auto before_history = editor.history().snapshot();
  const auto stale = editor.modeling().peel_uv({
      .options = first_options, .selection_token = first_token,
  });
  EXPECT_FALSE(stale.ok());
  EXPECT_FALSE(stale.changed());
  EXPECT_EQ(second->session->modeling().content_revision(), before_content);
  EXPECT_EQ(second->session->scene().revision(), before_scene);
  EXPECT_EQ(second->session->assets().revision(), before_assets);
  EXPECT_EQ(editor.history().snapshot().undo_label, before_history.undo_label);
  EXPECT_EQ(editor.history().snapshot().redo_label, before_history.redo_label);

  ASSERT_TRUE(editor.scene_documents().activate(first->session_id).ok());
  EXPECT_EQ(editor.modeling().uv_peel_settings().options, first_options);
  EXPECT_EQ(editor.modeling().uv_peel_settings().selected_edge_count, 1u);
  ASSERT_TRUE(editor.scene_documents().activate(second->session_id).ok());
  EXPECT_EQ(editor.modeling().uv_peel_settings().options, second_options);
  EXPECT_EQ(editor.modeling().uv_peel_settings().selected_edge_count, 1u);
}

TEST(ProjectSessionWorkspace,
     NewScenesStartWithIndependentCanonicalPerspectiveCameras) {
  ScopedProjectSessionDirectory temporary("independent-cameras");
  ProjectSession project;
  const ProjectSessionResult created =
      project.create(temporary.make_empty_child("IndependentCameras"));
  ASSERT_TRUE(created.ok) << created.message;
  ASSERT_TRUE(created.scene_session_id.has_value());

  OpenSceneSession *first = project.find(*created.scene_session_id);
  ASSERT_NE(first, nullptr);
  ViewportCameraController &first_camera =
      first->session->viewport_workspace().camera(
          ViewportPaneId::Perspective);
  const ViewportCameraController canonical_camera;
  EXPECT_EQ(first_camera.state().projection,
            canonical_camera.state().projection);
  EXPECT_FLOAT_EQ(first_camera.state().yaw_radians,
                  canonical_camera.state().yaw_radians);
  EXPECT_FLOAT_EQ(first_camera.state().pitch_radians,
                  canonical_camera.state().pitch_radians);
  EXPECT_FLOAT_EQ(first_camera.state().distance,
                  canonical_camera.state().distance);
  EXPECT_FLOAT_EQ(first_camera.pose().eye.x, canonical_camera.pose().eye.x);
  EXPECT_FLOAT_EQ(first_camera.pose().eye.y, canonical_camera.pose().eye.y);
  EXPECT_FLOAT_EQ(first_camera.pose().eye.z, canonical_camera.pose().eye.z);
  first_camera.set_viewport_size(1280.0f, 720.0f);
  EXPECT_TRUE(first_camera.project_world_point({}).visible);
  first_camera.focus_origin(23.0f);

  const ProjectSessionResult added = project.new_scene();
  ASSERT_TRUE(added.ok) << added.message;
  ASSERT_TRUE(added.scene_session_id.has_value());
  OpenSceneSession *second = project.find(*added.scene_session_id);
  ASSERT_NE(second, nullptr);
  ViewportCameraController &second_camera =
      second->session->viewport_workspace().camera(
          ViewportPaneId::Perspective);
  EXPECT_NE(&first_camera, &second_camera);
  EXPECT_EQ(second_camera.state().projection,
            canonical_camera.state().projection);
  EXPECT_FLOAT_EQ(second_camera.state().yaw_radians,
                  canonical_camera.state().yaw_radians);
  EXPECT_FLOAT_EQ(second_camera.state().pitch_radians,
                  canonical_camera.state().pitch_radians);
  EXPECT_FLOAT_EQ(second_camera.state().distance,
                  canonical_camera.state().distance);
  EXPECT_FLOAT_EQ(second_camera.pose().eye.x, canonical_camera.pose().eye.x);
  EXPECT_FLOAT_EQ(second_camera.pose().eye.y, canonical_camera.pose().eye.y);
  EXPECT_FLOAT_EQ(second_camera.pose().eye.z, canonical_camera.pose().eye.z);
  second_camera.set_viewport_size(1280.0f, 720.0f);
  EXPECT_TRUE(second_camera.project_world_point({}).visible);
  second_camera.focus_origin(91.0f);

  ASSERT_TRUE(project.activate(first->session_id).ok);
  ASSERT_EQ(project.active_scene(), first);
  EXPECT_FLOAT_EQ(first_camera.state().distance, 23.0f);
  EXPECT_FLOAT_EQ(second_camera.state().distance, 91.0f);

  ASSERT_TRUE(project.activate(second->session_id).ok);
  ASSERT_EQ(project.active_scene(), second);
  EXPECT_FLOAT_EQ(first_camera.state().distance, 23.0f);
  EXPECT_FLOAT_EQ(second_camera.state().distance, 91.0f);
}

TEST(ProjectSessionWorkspace,
     SaveAndWorkspaceRestorePreservePerspectivePoseFromBeforeLocalVisibility) {
  ScopedProjectSessionDirectory temporary("persisted-camera");
  const std::filesystem::path project_root =
      temporary.make_empty_child("PersistedCamera");
  ProjectSession project;
  const ProjectSessionResult created = project.create(project_root);
  ASSERT_TRUE(created.ok) << created.message;
  ASSERT_TRUE(created.scene_session_id.has_value());
  OpenSceneSession *scene = project.find(*created.scene_session_id);
  ASSERT_NE(scene, nullptr);

  ViewportWorkspaceService &workspace =
      scene->session->viewport_workspace();
  ViewportCameraController &camera =
      workspace.camera(ViewportPaneId::Perspective);
  camera.focus_origin(37.0f);
  camera.set_view_direction({-0.4f, -0.2f, -1.0f});
  const ViewportCameraPose expected = camera.pose();
  EXPECT_TRUE(scene->dirty());

  const std::array<ViewportLocalVisibilityEntityId, 1> members{1u};
  ASSERT_TRUE(workspace.enter_local_visibility(members, members));
  camera.focus_origin(91.0f);
  camera.set_view_direction({1.0f, 0.0f, 0.0f});
  expect_vec3_near(scene->session->persisted_viewport_camera_pose().eye,
                   expected.eye);

  const ProjectRelativePath scene_path =
      project_path("Scenes/PersistedCamera.qdr");
  const ProjectSessionResult saved =
      project.save_as(scene->session_id, scene_path);
  ASSERT_TRUE(saved.ok) << saved.message;
  EXPECT_FALSE(scene->dirty());

  const QdrProjectReadResult raw =
      QdrDocumentReader::read_project_document(
          project.root() / scene_path.native_path());
  ASSERT_TRUE(raw.ok) << raw.message;
  ASSERT_TRUE(raw.viewport_camera_pose.has_value());
  expect_vec3_near(raw.viewport_camera_pose->eye, expected.eye);
  expect_vec3_near(raw.viewport_camera_pose->target, expected.target);
  expect_vec3_near(raw.viewport_camera_pose->up, expected.up);

  ProjectSession restored;
  const ProjectSessionResult opened = restored.open(project.manifest_path());
  ASSERT_TRUE(opened.ok) << opened.message;
  const OpenSceneSession *restored_scene = restored.active_scene();
  ASSERT_NE(restored_scene, nullptr);
  const ViewportCameraPose actual =
      restored_scene->session->viewport_workspace()
          .camera(ViewportPaneId::Perspective)
          .pose();
  expect_vec3_near(actual.eye, expected.eye);
  expect_vec3_near(actual.target, expected.target);
  expect_vec3_near(actual.up, expected.up);
  EXPECT_FALSE(restored_scene->dirty());
}

TEST(ProjectSessionWorkspace, RejectsStaleIdsAcrossTabsAndProjects) {
  ScopedProjectSessionDirectory temporary("stale-ids");
  ProjectSession project;
  ProjectSessionResult created =
      project.create(temporary.make_empty_child("FirstProject"));
  ASSERT_TRUE(created.ok) << created.message;
  ASSERT_TRUE(created.scene_session_id.has_value());
  const SceneSessionId first_id = *created.scene_session_id;

  ASSERT_TRUE(project.close_scene(first_id).ok);
  EXPECT_TRUE(project.open_scenes().empty());
  EXPECT_FALSE(project.has_active_scene());
  const ProjectSessionResult replacement = project.new_scene();
  ASSERT_TRUE(replacement.ok) << replacement.message;
  ASSERT_TRUE(replacement.scene_session_id.has_value());
  EXPECT_EQ(replacement.scene_session_id->slot(), first_id.slot());
  EXPECT_NE(replacement.scene_session_id->generation(), first_id.generation());
  EXPECT_FALSE(project.activate(first_id).ok);
  EXPECT_EQ(project.find(first_id), nullptr);

  const SceneSessionId previous_project_id = *replacement.scene_session_id;
  created = project.create(temporary.make_empty_child("SecondProject"));
  ASSERT_TRUE(created.ok) << created.message;
  ASSERT_TRUE(created.scene_session_id.has_value());
  EXPECT_FALSE(project.activate(previous_project_id).ok);
  EXPECT_EQ(project.find(previous_project_id), nullptr);
  EXPECT_NE(*created.scene_session_id, previous_project_id);
}

TEST(ProjectSessionWorkspace,
     SaveAsChangesIdentityAndDuplicateOpenOnlyActivatesExistingTab) {
  ScopedProjectSessionDirectory temporary("save-identity");
  ProjectSession project;
  const ProjectSessionResult created =
      project.create(temporary.make_empty_child("Identity"));
  ASSERT_TRUE(created.ok) << created.message;
  ASSERT_TRUE(created.scene_session_id.has_value());
  const SceneSessionId tab_id = *created.scene_session_id;
  OpenSceneSession *scene = project.find(tab_id);
  ASSERT_NE(scene, nullptr);
  const SceneId unsaved_identity = scene->scene_id;

  const ProjectRelativePath first_path = project_path("Scenes/First.qdr");
  ProjectSessionResult saved_as = project.save_as(tab_id, first_path);
  ASSERT_TRUE(saved_as.ok) << saved_as.message;
  scene = project.find(tab_id);
  ASSERT_NE(scene, nullptr);
  const SceneId first_saved_identity = scene->scene_id;
  EXPECT_NE(first_saved_identity, unsaved_identity);
  EXPECT_FALSE(scene->dirty());
  ASSERT_NE(project.manifest()->find_scene(first_path), nullptr);
  EXPECT_EQ(project.manifest()->find_scene(first_path)->id,
            first_saved_identity);

  const std::filesystem::path first_absolute =
      project.root() / first_path.native_path();
  QdrProjectReadResult first_read =
      QdrDocumentReader::read_project_document(first_absolute);
  ASSERT_TRUE(first_read.ok) << first_read.message;
  EXPECT_EQ(SceneId::from_bytes(first_read.scene_uuid), first_saved_identity);

  const ProjectSessionResult extra = project.new_scene();
  ASSERT_TRUE(extra.ok) << extra.message;
  ASSERT_TRUE(extra.scene_session_id.has_value());
  ASSERT_NE(project.active_scene_id(), tab_id);
  const std::size_t tab_count = project.open_scenes().size();
  const ProjectSessionResult duplicate_open = project.open_scene(first_path);
  ASSERT_TRUE(duplicate_open.ok) << duplicate_open.message;
  EXPECT_EQ(project.open_scenes().size(), tab_count);
  EXPECT_EQ(project.active_scene_id(), tab_id);
  ASSERT_TRUE(project.close_scene(*extra.scene_session_id).ok);

  const DocumentCreateEntityResult changed =
      scene->session->document_commands().create_entity("Saved Change");
  ASSERT_TRUE(changed.ok) << changed.message;
  ASSERT_TRUE(scene->dirty());
  const ProjectSessionResult saved = project.save(tab_id);
  ASSERT_TRUE(saved.ok) << saved.message;
  EXPECT_EQ(scene->scene_id, first_saved_identity);
  EXPECT_FALSE(scene->dirty());
  const std::vector<char> first_file_before = read_file_bytes(first_absolute);

  const ProjectRelativePath second_path = project_path("Scenes/Second.qdr");
  saved_as = project.save_as(tab_id, second_path);
  ASSERT_TRUE(saved_as.ok) << saved_as.message;
  const SceneId second_saved_identity = scene->scene_id;
  EXPECT_NE(second_saved_identity, first_saved_identity);
  EXPECT_TRUE(std::filesystem::is_regular_file(first_absolute));
  EXPECT_TRUE(std::filesystem::is_regular_file(
      project.root() / second_path.native_path()));
  ASSERT_NE(project.manifest()->find_scene(first_path), nullptr);
  ASSERT_NE(project.manifest()->find_scene(second_path), nullptr);
  EXPECT_EQ(project.manifest()->find_scene(first_path)->id,
            first_saved_identity);
  EXPECT_EQ(project.manifest()->find_scene(second_path)->id,
            second_saved_identity);

  const ProjectRelativePath copied_path = project_path("Scenes/Copied.qdr");
  std::error_code copy_error;
  std::filesystem::copy_file(
      first_absolute, project.root() / copied_path.native_path(),
      std::filesystem::copy_options::none, copy_error);
  ASSERT_FALSE(copy_error) << copy_error.message();
  const ProjectSessionResult copied_open = project.open_scene(copied_path);
  ASSERT_TRUE(copied_open.ok) << copied_open.message;
  ASSERT_TRUE(copied_open.scene_session_id.has_value());
  ASSERT_FALSE(copied_open.warnings.empty());
  const OpenSceneSession *copied =
      project.find(*copied_open.scene_session_id);
  ASSERT_NE(copied, nullptr);
  EXPECT_TRUE(copied->identity_dirty);
  EXPECT_TRUE(copied->dirty());
  EXPECT_NE(copied->scene_id, first_saved_identity);
  EXPECT_NE(copied->scene_id, second_saved_identity);

  const ProjectSessionResult rejected = project.save_as(tab_id, first_path);
  EXPECT_FALSE(rejected.ok);
  EXPECT_EQ(read_file_bytes(first_absolute), first_file_before)
      << "Save As must reject a catalog collision before touching the file.";
}

TEST(ProjectSessionWorkspace,
     SavedSceneMeshesRemainPickSelectableAfterClosingAndReopeningTab) {
  ScopedProjectSessionDirectory temporary("reopened-mesh-selection");
  ProjectSession project;
  QuaderEditorApi editor(project);
  ASSERT_TRUE(editor.project()
                  .create(temporary.make_empty_child("ReopenedSelection"))
                  .ok());
  const std::optional<SceneSessionId> original_scene_id =
      editor.scene_documents().active_scene_id();
  ASSERT_TRUE(original_scene_id.has_value());

  const QuaderApiResult<QuaderModelingObjectRef> left =
      editor.modeling().create_box(
          QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                       .max = {1.0f, 1.0f, 1.0f}},
          "Reopened Left");
  ASSERT_TRUE(left.ok()) << left.error_message();
  const QuaderApiResult<QuaderModelingObjectRef> right =
      editor.modeling().create_box(
          QuaderBounds{.min = {4.0f, -1.0f, -1.0f},
                       .max = {6.0f, 1.0f, 1.0f}},
          "Reopened Right");
  ASSERT_TRUE(right.ok()) << right.error_message();

  const ProjectRelativePath path =
      project_path("Scenes/ReopenedSelection.qdr");
  ASSERT_TRUE(editor.scene_documents()
                  .save_as(*original_scene_id, path)
                  .ok());
  ASSERT_TRUE(editor.scene_documents().close(*original_scene_id).ok());
  ASSERT_FALSE(editor.scene_documents().has_active_scene());

  const QuaderApiResult<SceneSessionId> reopened =
      editor.scene_documents().open(path);
  ASSERT_TRUE(reopened.ok()) << reopened.error_message();
  ASSERT_NE(reopened.value(), *original_scene_id);
  const OpenSceneSession *reopened_scene = project.find(reopened.value());
  ASSERT_NE(reopened_scene, nullptr);
  EXPECT_FALSE(reopened_scene->dirty());
  EXPECT_EQ(reopened_scene->session->modeling()
                .modeling_sdk_object_count_for_tests(),
            2u);
  const ModelingObjectComponent *reopened_left =
      reopened_scene->session->scene().modeling_object(left.value().entity);
  const ModelingObjectComponent *reopened_right =
      reopened_scene->session->scene().modeling_object(right.value().entity);
  ASSERT_NE(reopened_left, nullptr);
  ASSERT_NE(reopened_right, nullptr);
  EXPECT_TRUE(reopened_left->sdk_owned);
  EXPECT_TRUE(reopened_right->sdk_owned);
  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Object).ok());

  QuaderPickSelectRequest pick_left;
  pick_left.ray = {
      .origin = {0.0f, 0.0f, 5.0f},
      .direction = {0.0f, 0.0f, -1.0f},
  };
  const QuaderApiResult<QuaderSelectionSummary> picked_left =
      editor.selection().pick_select(pick_left);
  ASSERT_TRUE(picked_left.ok()) << picked_left.error_message();
  EXPECT_TRUE(picked_left.changed());
  EXPECT_EQ(picked_left.value().active_object_id,
            left.value().modeling_object_id);
  EXPECT_FALSE(reopened_scene->dirty())
      << "Transient viewport selection must not dirty a reopened scene.";

  QuaderPickSelectRequest pick_right;
  pick_right.ray = {
      .origin = {5.0f, 0.0f, 5.0f},
      .direction = {0.0f, 0.0f, -1.0f},
  };
  const QuaderApiResult<QuaderSelectionSummary> picked_right =
      editor.selection().pick_select(pick_right);
  ASSERT_TRUE(picked_right.ok()) << picked_right.error_message();
  EXPECT_TRUE(picked_right.changed());
  EXPECT_EQ(picked_right.value().active_object_id,
            right.value().modeling_object_id);
  EXPECT_FALSE(reopened_scene->dirty())
      << "Cycling object selection must remain transient.";
}

TEST(ProjectSessionWorkspace, SupportsZeroTabsDirtyGuardsAndAtomicSaveAllPreflight) {
  ScopedProjectSessionDirectory temporary("close-save-all");
  ProjectSession project;
  const ProjectSessionResult created =
      project.create(temporary.make_empty_child("SaveAll"));
  ASSERT_TRUE(created.ok) << created.message;
  ASSERT_TRUE(created.scene_session_id.has_value());
  const SceneSessionId first_id = *created.scene_session_id;
  ASSERT_TRUE(project.save_as(first_id, project_path("Scenes/One.qdr")).ok);

  const ProjectSessionResult second_created = project.new_scene();
  ASSERT_TRUE(second_created.ok) << second_created.message;
  ASSERT_TRUE(second_created.scene_session_id.has_value());
  const SceneSessionId second_id = *second_created.scene_session_id;
  ASSERT_TRUE(project.save_as(second_id, project_path("Scenes/Two.qdr")).ok);

  OpenSceneSession *first = project.find(first_id);
  OpenSceneSession *second = project.find(second_id);
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  ASSERT_TRUE(first->session->document_commands().create_entity("One").ok);
  ASSERT_TRUE(second->session->document_commands().create_entity("Two").ok);
  ASSERT_TRUE(project.has_dirty_scenes());
  ProjectSessionResult saved_all = project.save_all();
  ASSERT_TRUE(saved_all.ok) << saved_all.message;
  EXPECT_TRUE(saved_all.changed);
  EXPECT_FALSE(first->dirty());
  EXPECT_FALSE(second->dirty());

  const ProjectSessionResult unsaved_created = project.new_scene();
  ASSERT_TRUE(unsaved_created.ok) << unsaved_created.message;
  ASSERT_TRUE(unsaved_created.scene_session_id.has_value());
  OpenSceneSession *unsaved = project.find(*unsaved_created.scene_session_id);
  ASSERT_NE(unsaved, nullptr);
  ASSERT_TRUE(unsaved->session->document_commands()
                  .create_entity("Unsaved Dirty")
                  .ok);
  ASSERT_TRUE(first->session->document_commands()
                  .create_entity("Must Stay Dirty")
                  .ok);
  saved_all = project.save_all();
  EXPECT_FALSE(saved_all.ok);
  EXPECT_TRUE(first->dirty())
      << "Save All must preflight unsaved dirty tabs before writing any scene.";

  EXPECT_FALSE(project.close_scene(*unsaved_created.scene_session_id).ok);
  ASSERT_TRUE(
      project.close_scene(*unsaved_created.scene_session_id, true).ok);
  ASSERT_TRUE(project.save_all().ok);
  EXPECT_FALSE(project.has_dirty_scenes());

  ASSERT_TRUE(project.close_scene(second_id).ok);
  ASSERT_TRUE(project.close_scene(first_id).ok);
  EXPECT_TRUE(project.open_scenes().empty());
  EXPECT_FALSE(project.has_active_scene());
  const ProjectSessionResult reopened_empty = project.new_scene();
  ASSERT_TRUE(reopened_empty.ok) << reopened_empty.message;
  ASSERT_TRUE(reopened_empty.scene_session_id.has_value());
  ASSERT_NE(project.active_scene(), nullptr);

  OpenSceneSession *last = project.active_scene();
  ASSERT_TRUE(last->session->document_commands().create_entity("Dirty").ok);
  const std::filesystem::path blocked_root =
      temporary.make_empty_child("BlockedByDirty");
  const ProjectSessionResult blocked = project.create(blocked_root);
  EXPECT_FALSE(blocked.ok);
  EXPECT_TRUE(std::filesystem::is_empty(blocked_root));
  EXPECT_EQ(project.active_scene(), last);
}

TEST(ProjectSessionWorkspace,
     ManifestWriteFailuresRollBackWorkspaceOwnerMutations) {
  ScopedProjectSessionDirectory temporary("manifest-rollback");
  ProjectSession project;
  const ProjectSessionResult created =
      project.create(temporary.make_empty_child("ManifestRollback"));
  ASSERT_TRUE(created.ok) << created.message;
  ASSERT_TRUE(created.scene_session_id.has_value());
  const SceneSessionId first_id = *created.scene_session_id;
  const ProjectRelativePath first_path = project_path("Scenes/First.qdr");
  ASSERT_TRUE(project.save_as(first_id, first_path).ok);

  const ProjectSessionResult second_created = project.new_scene();
  ASSERT_TRUE(second_created.ok) << second_created.message;
  ASSERT_TRUE(second_created.scene_session_id.has_value());
  const SceneSessionId second_id = *second_created.scene_session_id;
  ASSERT_TRUE(
      project.save_as(second_id, project_path("Scenes/Second.qdr")).ok);
  ASSERT_TRUE(project.activate(first_id).ok);

  const auto expect_unchanged_after_manifest_failure =
      [&](auto &&operation) {
        const SceneSessionId active_before = project.active_scene_id();
        const std::uint64_t generation_before = project.activation_generation();
        const std::size_t tabs_before = project.open_scenes().size();
        const std::string manifest_before =
            ProjectManifestStore::serialize(*project.manifest());
        ProjectSessionResult result;
        {
          ScopedManifestWriteFailure blocked(project.manifest_path());
          EXPECT_TRUE(blocked.ready()) << blocked.message();
          if (!blocked.ready()) {
            return ProjectSessionResult{};
          }
          result = operation();
          EXPECT_FALSE(result.ok);
          EXPECT_EQ(project.active_scene_id(), active_before);
          EXPECT_EQ(project.activation_generation(), generation_before);
          EXPECT_EQ(project.open_scenes().size(), tabs_before);
          EXPECT_EQ(ProjectManifestStore::serialize(*project.manifest()),
                    manifest_before);
        }
        return result;
      };

  (void)expect_unchanged_after_manifest_failure(
      [&]() { return project.activate(second_id); });
  (void)expect_unchanged_after_manifest_failure(
      [&]() { return project.new_scene(); });
  (void)expect_unchanged_after_manifest_failure(
      [&]() { return project.close_scene(first_id); });

  const ProjectRelativePath imported_path =
      project_path("Scenes/Imported.qdr");
  std::error_code copy_error;
  std::filesystem::copy_file(project.root() / first_path.native_path(),
                             project.root() / imported_path.native_path(),
                             std::filesystem::copy_options::none, copy_error);
  ASSERT_FALSE(copy_error) << copy_error.message();
  (void)expect_unchanged_after_manifest_failure(
      [&]() { return project.open_scene(imported_path); });
  EXPECT_EQ(project.manifest()->find_scene(imported_path), nullptr);

  const ProjectSessionResult unsaved_created = project.new_scene();
  ASSERT_TRUE(unsaved_created.ok) << unsaved_created.message;
  ASSERT_TRUE(unsaved_created.scene_session_id.has_value());
  OpenSceneSession *unsaved = project.find(*unsaved_created.scene_session_id);
  ASSERT_NE(unsaved, nullptr);
  const SceneId unsaved_id = unsaved->scene_id;
  const ProjectRelativePath save_as_path =
      project_path("Scenes/ManifestFailureSaveAs.qdr");
  (void)expect_unchanged_after_manifest_failure([&]() {
    return project.save_as(unsaved->session_id, save_as_path);
  });
  EXPECT_FALSE(
      std::filesystem::exists(project.root() / save_as_path.native_path()));
  EXPECT_EQ(unsaved->scene_id, unsaved_id);
  EXPECT_FALSE(unsaved->path.has_value());
  EXPECT_EQ(project.manifest()->find_scene(save_as_path), nullptr);

  const ProjectRelativePath occupied_save_as_path =
      project_path("Scenes/ManifestFailureOccupied.qdr");
  const std::filesystem::path occupied_destination =
      project.root() / occupied_save_as_path.native_path();
  const std::array<char, 12> original_bytes{
      'n', 'o', 't', '-', 'a', '-', 'q', 'd', 'r', '\0', '\r', '\n'};
  {
    std::ofstream stream(occupied_destination,
                         std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(stream.is_open());
    stream.write(original_bytes.data(),
                 static_cast<std::streamsize>(original_bytes.size()));
    ASSERT_TRUE(stream.good());
  }
  const std::vector<char> occupied_before =
      read_file_bytes(occupied_destination);
  const std::filesystem::path occupied_alias =
      project.root() / "Scenes" / "ManifestFailureOccupiedAlias.qdr";
  std::error_code occupied_link_error;
  std::filesystem::create_hard_link(occupied_destination, occupied_alias,
                                    occupied_link_error);
  (void)expect_unchanged_after_manifest_failure([&]() {
    return project.save_as(unsaved->session_id, occupied_save_as_path);
  });
  EXPECT_EQ(read_file_bytes(occupied_destination), occupied_before);
  if (!occupied_link_error) {
    std::error_code equivalent_error;
    EXPECT_TRUE(std::filesystem::equivalent(occupied_destination,
                                            occupied_alias,
                                            equivalent_error));
    EXPECT_FALSE(equivalent_error) << equivalent_error.message();
  }
  EXPECT_EQ(unsaved->scene_id, unsaved_id);
  EXPECT_FALSE(unsaved->path.has_value());
  EXPECT_EQ(project.manifest()->find_scene(occupied_save_as_path), nullptr);
}

TEST(ProjectSessionWorkspace,
     PhysicalPathAliasesFocusOpenTabsAndBlockSaveAsCollisions) {
  ScopedProjectSessionDirectory temporary("physical-aliases");
  ProjectSession project;
  const ProjectSessionResult created =
      project.create(temporary.make_empty_child("PhysicalAliases"));
  ASSERT_TRUE(created.ok) << created.message;
  ASSERT_TRUE(created.scene_session_id.has_value());
  const SceneSessionId first_id = *created.scene_session_id;
  const ProjectRelativePath first_path = project_path("Scenes/First.qdr");
  ASSERT_TRUE(project.save_as(first_id, first_path).ok);

  const ProjectSessionResult second_created = project.new_scene();
  ASSERT_TRUE(second_created.ok) << second_created.message;
  ASSERT_TRUE(second_created.scene_session_id.has_value());
  const SceneSessionId second_id = *second_created.scene_session_id;
  const std::size_t tab_count = project.open_scenes().size();

  const std::filesystem::path link = project.root() / "SceneAlias";
  std::error_code link_error;
  std::filesystem::create_directory_symlink(project.root() / "Scenes", link,
                                            link_error);
  if (!link_error) {
    const ProjectRelativePath alias_path =
        project_path("SceneAlias/First.qdr");
    const ProjectSessionResult opened = project.open_scene(alias_path);
    ASSERT_TRUE(opened.ok) << opened.message;
    EXPECT_EQ(project.open_scenes().size(), tab_count);
    EXPECT_EQ(project.active_scene_id(), first_id);

    const ProjectSessionResult rejected =
        project.save_as(second_id, alias_path);
    EXPECT_FALSE(rejected.ok);
    const OpenSceneSession *second = project.find(second_id);
    ASSERT_NE(second, nullptr);
    EXPECT_FALSE(second->path.has_value());
  }

  const std::filesystem::path hard_link =
      project.root() / "Scenes" / "FirstHardLink.qdr";
  std::error_code hard_link_error;
  std::filesystem::create_hard_link(project.root() / first_path.native_path(),
                                    hard_link, hard_link_error);
  if (!hard_link_error) {
    ASSERT_TRUE(project.activate(second_id).ok);
    const ProjectRelativePath hard_link_path =
        project_path("Scenes/FirstHardLink.qdr");
    const ProjectSessionResult opened = project.open_scene(hard_link_path);
    ASSERT_TRUE(opened.ok) << opened.message;
    EXPECT_EQ(project.open_scenes().size(), tab_count);
    EXPECT_EQ(project.active_scene_id(), first_id);
    EXPECT_FALSE(project.save_as(second_id, hard_link_path).ok);
  }
}

TEST(ProjectSessionWorkspace, ClosingActiveSceneClearsNextSceneHover) {
  ScopedProjectSessionDirectory temporary("close-clears-hover");
  ProjectSession project;
  const ProjectSessionResult created =
      project.create(temporary.make_empty_child("CloseClearsHover"));
  ASSERT_TRUE(created.ok) << created.message;
  ASSERT_TRUE(created.scene_session_id.has_value());
  const SceneSessionId first_id = *created.scene_session_id;

  QuaderEditorApi editor(project);
  const QuaderApiResult<QuaderModelingObjectRef> box =
      editor.modeling().create_box(
          QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                       .max = {1.0f, 1.0f, 1.0f}},
          "Hover Target");
  ASSERT_TRUE(box.ok()) << box.error_message();
  ASSERT_TRUE(
      project.save_as(first_id, project_path("Scenes/First.qdr")).ok);

  const ProjectSessionResult second_created = project.new_scene();
  ASSERT_TRUE(second_created.ok) << second_created.message;
  ASSERT_TRUE(second_created.scene_session_id.has_value());
  const SceneSessionId second_id = *second_created.scene_session_id;

  OpenSceneSession *first = project.find(first_id);
  ASSERT_NE(first, nullptr);
  const ModelingCommandResult hover_set = first->session->selection().set_hover(
      {.modeling_object_id = box.value().modeling_object_id,
       .kind = ModelingActiveSelectionKind::Object});
  ASSERT_TRUE(hover_set.ok) << hover_set.message;
  ASSERT_EQ(first->session->selection().hover_summary().target.kind,
            ModelingActiveSelectionKind::Object);

  const ProjectSessionResult closed = project.close_scene(second_id);
  ASSERT_TRUE(closed.ok) << closed.message;
  EXPECT_EQ(project.active_scene_id(), first_id);
  EXPECT_EQ(first->session->selection().hover_summary().target.kind,
            ModelingActiveSelectionKind::None);
}

TEST(ProjectSessionWorkspace, FailedReplacementPreservesExistingWorkspace) {
  ScopedProjectSessionDirectory temporary("replacement-rollback");
  ProjectSession project;
  const ProjectSessionResult created =
      project.create(temporary.make_empty_child("Existing"));
  ASSERT_TRUE(created.ok) << created.message;
  ASSERT_TRUE(created.scene_session_id.has_value());
  const std::filesystem::path existing_root = project.root();
  const ProjectId existing_id = project.manifest()->id();
  OpenSceneSession *existing_scene = project.active_scene();
  ASSERT_NE(existing_scene, nullptr);

  const std::filesystem::path occupied =
      temporary.make_empty_child("OccupiedReplacement");
  {
    std::ofstream marker(occupied / "keep.txt");
    marker << "keep";
  }
  const ProjectSessionResult failed_create = project.create(occupied, true);
  EXPECT_FALSE(failed_create.ok);
  EXPECT_EQ(project.root(), existing_root);
  EXPECT_EQ(project.manifest()->id(), existing_id);
  EXPECT_EQ(project.active_scene(), existing_scene);

  const std::filesystem::path malformed = temporary.path() / "bad.qproj";
  {
    std::ofstream stream(malformed, std::ios::binary);
    stream << "not a project";
  }
  const ProjectSessionResult failed_open = project.open(malformed, true);
  EXPECT_FALSE(failed_open.ok);
  EXPECT_EQ(project.root(), existing_root);
  EXPECT_EQ(project.manifest()->id(), existing_id);
  EXPECT_EQ(project.active_scene(), existing_scene);
}

TEST(ProjectSessionWorkspace,
     LateOpenPersistenceFailurePreservesActiveSdkPreviewExactly) {
#if !defined(_WIN32)
  GTEST_SKIP() << "The late manifest replacement lock is Windows-specific.";
#else
  ScopedProjectSessionDirectory temporary("late-open-preview-rollback");
  ProjectSession project;
  const ProjectSessionResult created =
      project.create(temporary.make_empty_child("Active"));
  ASSERT_TRUE(created.ok) << created.message;
  OpenSceneSession *active = project.active_scene();
  ASSERT_NE(active, nullptr);

  const ModelingCreateBoxResult box =
      active->session->modeling().create_box_from_bounds({
          .bounds = {.min = {-1.0f, -1.0f, -1.0f},
                     .max = {1.0f, 1.0f, 1.0f}},
          .name = "Preview Must Survive",
      });
  ASSERT_TRUE(box.ok) << box.message;
  ASSERT_TRUE(active->session->modeling().begin_transform_preview(false).ok);
  ASSERT_TRUE(active->session->modeling()
                  .update_transform_translate_preview({2.0f, 0.0f, 0.0f})
                  .ok);
  ASSERT_TRUE(
      active->session->modeling().can_cancel_active_preview_for_undo());
  const ModelingSelectionSummary selection_before =
      active->session->modeling().selection_summary();
  const std::vector<EntityId> entities_before =
      active->session->scene().entities();
  const std::vector<AssetId> meshes_before =
      active->session->assets().mesh_ids();
  const std::filesystem::path active_root = project.root();

  ProjectSession candidate;
  const ProjectSessionResult candidate_created =
      candidate.create(temporary.make_empty_child("Candidate"));
  ASSERT_TRUE(candidate_created.ok) << candidate_created.message;
  const std::filesystem::path candidate_manifest = candidate.manifest_path();
  ASSERT_TRUE(candidate.close(true).ok);

  ScopedManifestWriteLock locked(candidate_manifest);
  ASSERT_TRUE(locked.ready());
  const ProjectSessionResult failed = project.open(candidate_manifest, true);
  EXPECT_FALSE(failed.ok);
  EXPECT_EQ(project.root(), active_root);
  EXPECT_EQ(project.active_scene(), active);
  EXPECT_TRUE(
      active->session->modeling().can_cancel_active_preview_for_undo());
  EXPECT_EQ(active->session->modeling().selection_summary().content_revision,
            selection_before.content_revision);
  EXPECT_EQ(active->session->modeling().selection_summary().selection_revision,
            selection_before.selection_revision);
  EXPECT_EQ(active->session->scene().entities(), entities_before);
  EXPECT_EQ(active->session->assets().mesh_ids(), meshes_before);
#endif
}

TEST(ProjectSessionWorkspace,
     ObjectHandlesRejectWrongActiveAndClosedSceneGenerations) {
  ScopedProjectSessionDirectory temporary("qualified-handles");
  ProjectSession project;
  const ProjectSessionResult created =
      project.create(temporary.make_empty_child("QualifiedHandles"));
  ASSERT_TRUE(created.ok) << created.message;
  ASSERT_TRUE(created.scene_session_id.has_value());
  const SceneSessionId first_id = *created.scene_session_id;

  QuaderEditorApi editor(project);
  editor.scene_documents().set_runtime_hooks({
      .confirm_dirty_scene =
          [](const QuaderSceneDocumentInfo &, std::string_view) {
            return QuaderDirtySceneDecision::Discard;
          },
  });
  const QuaderApiResult<QuaderModelingObjectRef> created_box =
      editor.modeling().create_box(
          QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                       .max = {1.0f, 1.0f, 1.0f}},
          "First Scene Box");
  ASSERT_TRUE(created_box.ok()) << created_box.error_message();
  QuaderObjectHandle handle = editor.object(created_box.value());
  ASSERT_TRUE(handle.valid());

  const QuaderApiResult<SceneSessionId> second =
      editor.scene_documents().new_scene();
  ASSERT_TRUE(second.ok()) << second.error_message();
  ASSERT_NE(second.value(), first_id);
  EXPECT_FALSE(handle.valid());
  const QuaderApiVoidResult wrong_scene = handle.rename("Wrong Scene");
  EXPECT_FALSE(wrong_scene.ok());
  EXPECT_NE(wrong_scene.error_message().find("inactive scene"),
            std::string::npos);

  ASSERT_TRUE(editor.scene_documents().activate(first_id).ok());
  ASSERT_TRUE(handle.valid());
  ASSERT_TRUE(handle.rename("First Scene Box Renamed").ok());

  ASSERT_TRUE(editor.scene_documents().close(first_id).ok());
  EXPECT_FALSE(handle.valid());
  EXPECT_FALSE(handle.destroy().ok());
}

TEST(ProjectSessionWorkspace,
     CachedFacadesResolveActiveSceneAndRejectSameNumberReferences) {
  ScopedProjectSessionDirectory temporary("cached-active-facades");
  ProjectSession project;
  const ProjectSessionResult created =
      project.create(temporary.make_empty_child("CachedActiveFacades"));
  ASSERT_TRUE(created.ok) << created.message;
  ASSERT_TRUE(created.scene_session_id.has_value());
  const SceneSessionId first_id = *created.scene_session_id;

  QuaderEditorApi editor(project);
  QuaderSceneApi &cached_scene = editor.scene();
  QuaderModelingApi &cached_modeling = editor.modeling();
  QuaderSelectionApi &cached_selection = editor.selection();

  const QuaderApiResult<QuaderEntityRef> first_entity =
      cached_scene.create_entity({.name = "First Entity"});
  ASSERT_TRUE(first_entity.ok()) << first_entity.error_message();
  EXPECT_EQ(first_entity.value().scene_session_id, first_id);
  const QuaderApiResult<QuaderModelingObjectRef> first_box =
      cached_modeling.create_box(
          QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                       .max = {1.0f, 1.0f, 1.0f}},
          "First Box");
  ASSERT_TRUE(first_box.ok()) << first_box.error_message();
  EXPECT_EQ(first_box.value().scene_session_id, first_id);
  OpenSceneSession *first_workspace = project.find(first_id);
  ASSERT_NE(first_workspace, nullptr);
  const void *first_sdk_session = first_workspace->session->modeling()
                                      .modeling_sdk_session_identity_for_tests();
  EXPECT_EQ(first_workspace->session->modeling()
                .modeling_sdk_object_count_for_tests(),
            1u);

  const QuaderApiResult<SceneSessionId> second_scene =
      editor.scene_documents().new_scene();
  ASSERT_TRUE(second_scene.ok()) << second_scene.error_message();
  const SceneSessionId second_id = second_scene.value();
  ASSERT_NE(second_id, first_id);
  OpenSceneSession *second_workspace = project.find(second_id);
  ASSERT_NE(second_workspace, nullptr);
  EXPECT_NE(second_workspace->session->modeling()
                .modeling_sdk_session_identity_for_tests(),
            first_sdk_session);
  EXPECT_EQ(&editor.scene(), &cached_scene);
  EXPECT_EQ(&editor.modeling(), &cached_modeling);
  EXPECT_EQ(&editor.selection(), &cached_selection);

  const QuaderApiResult<QuaderEntityRef> second_entity =
      cached_scene.create_entity({.name = "Second Entity"});
  ASSERT_TRUE(second_entity.ok()) << second_entity.error_message();
  EXPECT_EQ(second_entity.value().scene_session_id, second_id);
  EXPECT_EQ(second_entity.value().id, first_entity.value().id);
  const QuaderApiResult<QuaderModelingObjectRef> second_box =
      cached_modeling.create_box(
          QuaderBounds{.min = {-2.0f, -2.0f, -2.0f},
                       .max = {2.0f, 2.0f, 2.0f}},
          "Second Box");
  ASSERT_TRUE(second_box.ok()) << second_box.error_message();
  EXPECT_EQ(second_box.value().scene_session_id, second_id);
  EXPECT_EQ(second_box.value().entity, first_box.value().entity);
  EXPECT_EQ(second_box.value().modeling_object_id,
            first_box.value().modeling_object_id);
  EXPECT_EQ(first_workspace->session->modeling()
                .modeling_sdk_object_count_for_tests(),
            1u);
  EXPECT_EQ(second_workspace->session->modeling()
                .modeling_sdk_object_count_for_tests(),
            1u);

  const QuaderApiVoidResult stale_selection =
      cached_selection.select_object(first_box.value());
  EXPECT_FALSE(stale_selection.ok());
  EXPECT_NE(stale_selection.error_message().find("inactive scene"),
            std::string::npos);
  EXPECT_FALSE(cached_modeling.mesh(first_box.value()).valid());
  ASSERT_TRUE(cached_selection.select_object(second_box.value()).ok());

  ASSERT_TRUE(editor.scene_documents().activate(first_id).ok());
  ASSERT_TRUE(cached_scene.rename(first_entity.value().id,
                                  "First Entity Renamed")
                  .ok());
  ASSERT_TRUE(editor.query().entity(first_entity.value().id).has_value());
  EXPECT_EQ(editor.query().entity(first_entity.value().id)->name,
            "First Entity Renamed");

  ASSERT_TRUE(editor.scene_documents().activate(second_id).ok());
  ASSERT_TRUE(editor.query().entity(second_entity.value().id).has_value());
  EXPECT_EQ(editor.query().entity(second_entity.value().id)->name,
            "Second Entity");
}

TEST(ProjectSessionWorkspace,
     SceneBatchesAndRawReferencesRejectInactiveAndClosedScenes) {
  ScopedProjectSessionDirectory temporary("qualified-scene-batches");
  ProjectSession project;
  const ProjectSessionResult created =
      project.create(temporary.make_empty_child("QualifiedSceneBatches"));
  ASSERT_TRUE(created.ok) << created.message;
  ASSERT_TRUE(created.scene_session_id.has_value());
  const SceneSessionId first_id = *created.scene_session_id;

  QuaderEditorApi editor(project);
  editor.scene_documents().set_runtime_hooks({
      .confirm_dirty_scene =
          [](const QuaderSceneDocumentInfo &, std::string_view) {
            return QuaderDirtySceneDecision::Discard;
          },
  });
  QuaderSceneApi &cached_scene = editor.scene();
  const QuaderApiResult<QuaderModelingObjectRef> first_box =
      editor.modeling().create_box(
          QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                       .max = {1.0f, 1.0f, 1.0f}},
          "First Batch Box");
  ASSERT_TRUE(first_box.ok()) << first_box.error_message();
  QuaderSceneBatch first_batch = cached_scene.batch("First Scene Batch");
  first_batch.rename(first_box.value().entity, "Must Not Rename Second");

  const QuaderApiResult<SceneSessionId> second_scene =
      editor.scene_documents().new_scene();
  ASSERT_TRUE(second_scene.ok()) << second_scene.error_message();
  const SceneSessionId second_id = second_scene.value();
  const QuaderApiResult<QuaderModelingObjectRef> second_box =
      editor.modeling().create_box(
          QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                       .max = {1.0f, 1.0f, 1.0f}},
          "Second Batch Box");
  ASSERT_TRUE(second_box.ok()) << second_box.error_message();
  ASSERT_EQ(first_box.value().entity, second_box.value().entity);

  const QuaderApiVoidResult wrong_scene_batch = first_batch.commit();
  EXPECT_FALSE(wrong_scene_batch.ok());
  EXPECT_NE(wrong_scene_batch.error_message().find("inactive scene"),
            std::string::npos);
  ASSERT_TRUE(editor.query().entity(second_box.value().entity).has_value());
  EXPECT_EQ(editor.query().entity(second_box.value().entity)->name,
            "Second Batch Box");

  QuaderSceneBatch second_batch = cached_scene.batch("Second Scene Batch");
  second_batch.rename(second_box.value().entity,
                      "Second Batch Box Renamed");
  ASSERT_TRUE(second_batch.commit().ok());
  EXPECT_EQ(editor.query().entity(second_box.value().entity)->name,
            "Second Batch Box Renamed");

  ASSERT_TRUE(editor.scene_documents().activate(first_id).ok());
  ASSERT_TRUE(editor.scene_documents().close(first_id).ok());
  ASSERT_EQ(editor.scene_documents().active_scene_id(), second_id);
  const QuaderApiVoidResult closed_reference =
      editor.selection().select_object(first_box.value());
  EXPECT_FALSE(closed_reference.ok());
  EXPECT_NE(closed_reference.error_message().find("stale"),
            std::string::npos);
}

TEST(ProjectSessionWorkspace,
     CanceledSaveAsAfterSaveDecisionKeepsDirtySceneTabOpen) {
  ScopedProjectSessionDirectory temporary("scene-close-save-as-cancel");
  ProjectSession project;
  QuaderEditorApi editor(project);
  ASSERT_TRUE(editor.project()
                  .create(temporary.make_empty_child("SceneCloseCancel"))
                  .ok());
  const std::optional<SceneSessionId> scene_id =
      editor.scene_documents().active_scene_id();
  ASSERT_TRUE(scene_id.has_value());
  ASSERT_TRUE(editor.scene()
                  .create_entity(
                      QuaderCreateEntityArgs{.name = "Unsaved Close Entity"})
                  .ok());

  int save_path_requests = 0;
  editor.scene_documents().set_runtime_hooks({
      .choose_save_scene_as_path =
          [&save_path_requests](SceneSessionId)
              -> std::optional<std::filesystem::path> {
            ++save_path_requests;
            return std::nullopt;
          },
      .confirm_dirty_scene =
          [](const QuaderSceneDocumentInfo &, std::string_view action_id) {
            EXPECT_EQ(action_id, "scene.close");
            return QuaderDirtySceneDecision::Save;
          },
  });

  const QuaderApiVoidResult closed = editor.scene_documents().close(*scene_id);
  ASSERT_TRUE(closed.ok()) << closed.error_message();
  EXPECT_FALSE(closed.changed());
  EXPECT_EQ(save_path_requests, 1);
  ASSERT_EQ(editor.scene_documents().open_scenes().size(), 1u);
  ASSERT_TRUE(editor.scene_documents().active_scene().has_value());
  EXPECT_EQ(editor.scene_documents().active_scene()->session_id, *scene_id);
  EXPECT_TRUE(editor.scene_documents().active_scene()->dirty);
  EXPECT_FALSE(editor.scene_documents().active_scene()->path.has_value());
}

TEST(ProjectSessionWorkspace,
     CanceledSaveAsAbortsSaveAllThroughApiAndCommand) {
  ScopedProjectSessionDirectory temporary("save-all-save-as-cancel");
  const std::filesystem::path root =
      temporary.make_empty_child("SaveAllCancel");
  ProjectSession project;
  QuaderEditorApi editor(project);
  ASSERT_TRUE(editor.project().create(root).ok());
  const std::optional<SceneSessionId> scene_id =
      editor.scene_documents().active_scene_id();
  ASSERT_TRUE(scene_id.has_value());
  ASSERT_TRUE(editor.scene()
                  .create_entity(
                      QuaderCreateEntityArgs{.name = "Unsaved Save All Entity"})
                  .ok());

  int save_path_requests = 0;
  editor.scene_documents().set_runtime_hooks({
      .choose_save_scene_as_path =
          [&save_path_requests](SceneSessionId)
              -> std::optional<std::filesystem::path> {
            ++save_path_requests;
            return std::nullopt;
          },
  });

  const QuaderApiVoidResult api_result =
      editor.scene_documents().save_all();
  ASSERT_TRUE(api_result.ok()) << api_result.error_message();
  EXPECT_FALSE(api_result.changed());
  EXPECT_EQ(api_result.message, "Save All canceled.");
  EXPECT_EQ(save_path_requests, 1);
  ASSERT_TRUE(editor.scene_documents().active_scene().has_value());
  EXPECT_EQ(editor.scene_documents().active_scene()->session_id, *scene_id);
  EXPECT_TRUE(editor.scene_documents().active_scene()->dirty);
  EXPECT_FALSE(editor.scene_documents().active_scene()->path.has_value());

  const QuaderApiVoidResult command_result = editor.commands().execute(
      quader_command::kSceneDocumentSaveAll);
  ASSERT_TRUE(command_result.ok()) << command_result.error_message();
  EXPECT_FALSE(command_result.changed());
  EXPECT_EQ(command_result.message, "Save All canceled.");
  EXPECT_EQ(save_path_requests, 2);
  ASSERT_TRUE(editor.scene_documents().active_scene().has_value());
  EXPECT_EQ(editor.scene_documents().active_scene()->session_id, *scene_id);
  EXPECT_TRUE(editor.scene_documents().active_scene()->dirty);
  EXPECT_FALSE(editor.scene_documents().active_scene()->path.has_value());
  EXPECT_TRUE(std::filesystem::is_empty(root / "Scenes"));
}

TEST(ProjectSessionWorkspace,
     CanceledSaveAsAfterSaveDecisionAbortsProjectReplacement) {
  ScopedProjectSessionDirectory temporary("project-replace-save-as-cancel");
  const std::filesystem::path original_root =
      temporary.make_empty_child("OriginalProject");
  const std::filesystem::path replacement_root =
      temporary.make_empty_child("ReplacementProject");
  ProjectSession project;
  QuaderEditorApi editor(project);
  ASSERT_TRUE(editor.project().create(original_root).ok());
  const std::optional<SceneSessionId> scene_id =
      editor.scene_documents().active_scene_id();
  ASSERT_TRUE(scene_id.has_value());
  ASSERT_TRUE(editor.scene()
                  .create_entity(QuaderCreateEntityArgs{
                      .name = "Preserved Replacement Entity"})
                  .ok());

  int save_path_requests = 0;
  editor.scene_documents().set_runtime_hooks({
      .choose_save_scene_as_path =
          [&save_path_requests](SceneSessionId)
              -> std::optional<std::filesystem::path> {
            ++save_path_requests;
            return std::nullopt;
          },
      .confirm_dirty_scene =
          [](const QuaderSceneDocumentInfo &, std::string_view action_id) {
            EXPECT_EQ(action_id, "project.create");
            return QuaderDirtySceneDecision::Save;
          },
  });

  const QuaderApiVoidResult replaced = editor.project().create(replacement_root);
  ASSERT_TRUE(replaced.ok()) << replaced.error_message();
  EXPECT_FALSE(replaced.changed());
  EXPECT_EQ(save_path_requests, 1);
  EXPECT_EQ(editor.project().root(), original_root);
  EXPECT_TRUE(editor.project().dirty());
  ASSERT_EQ(editor.scene_documents().open_scenes().size(), 1u);
  EXPECT_EQ(editor.scene_documents().open_scenes().front().session_id,
            *scene_id);
  EXPECT_TRUE(std::filesystem::is_empty(replacement_root));
}

TEST(ProjectSessionWorkspace,
     ProjectClosePromptsActiveFirstAndPreservesSavedWorkspaceTabs) {
  ScopedProjectSessionDirectory temporary("project-close-workspace");
  const std::filesystem::path root =
      temporary.make_empty_child("ProjectCloseWorkspace");
  ProjectSession project;
  QuaderEditorApi editor(project);

  const QuaderApiVoidResult created = editor.project().create(root);
  ASSERT_TRUE(created.ok()) << created.error_message();
  const std::optional<SceneSessionId> first_id =
      editor.scene_documents().active_scene_id();
  ASSERT_TRUE(first_id.has_value());
  ASSERT_TRUE(editor.scene_documents()
                  .save_as(*first_id, project_path("Scenes/First.qdr"))
                  .ok());

  const QuaderApiResult<SceneSessionId> second =
      editor.scene_documents().new_scene();
  ASSERT_TRUE(second.ok()) << second.error_message();
  ASSERT_TRUE(editor.scene_documents()
                  .save_as(second.value(),
                           project_path("Scenes/Second.qdr"))
                  .ok());
  ASSERT_TRUE(editor.scene_documents().activate(*first_id).ok());
  OpenSceneSession *first = project.find(*first_id);
  ASSERT_NE(first, nullptr);
  ASSERT_TRUE(
      first->session->document_commands().create_entity("Discard Me").ok);

  std::vector<SceneSessionId> prompted;
  editor.scene_documents().set_runtime_hooks({
      .confirm_dirty_scene =
          [&prompted](const QuaderSceneDocumentInfo &scene,
                      std::string_view) {
            prompted.push_back(scene.session_id);
            return QuaderDirtySceneDecision::Discard;
          },
  });
  const std::filesystem::path manifest = editor.project().manifest_path();
  const QuaderApiVoidResult closed = editor.project().close();
  ASSERT_TRUE(closed.ok()) << closed.error_message();
  ASSERT_EQ(prompted.size(), 1u);
  EXPECT_EQ(prompted.front(), *first_id);
  EXPECT_FALSE(editor.project().has_project());

  const QuaderApiVoidResult reopened = editor.project().open(manifest);
  ASSERT_TRUE(reopened.ok()) << reopened.error_message();
  const std::vector<QuaderSceneDocumentInfo> scenes =
      editor.scene_documents().open_scenes();
  ASSERT_EQ(scenes.size(), 2u);
  ASSERT_TRUE(editor.scene_documents().active_scene().has_value());
  EXPECT_EQ(editor.scene_documents().active_scene()->path,
            project_path("Scenes/First.qdr"));
  EXPECT_EQ(scenes[0].path, project_path("Scenes/First.qdr"));
  EXPECT_EQ(scenes[1].path, project_path("Scenes/Second.qdr"));
}

TEST(ProjectSessionWorkspace,
     RecentProjectsStayInsideTheDisposableTestProfile) {
  const char *local_app_data = std::getenv("LOCALAPPDATA");
  ASSERT_NE(local_app_data, nullptr);
  const std::filesystem::path test_profile(local_app_data);
  EXPECT_EQ(test_profile.filename().string().rfind(
                "quader-tests-local-app-data-", 0u),
            0u);

  ScopedProjectSessionDirectory temporary("recent-project-isolation");
  const std::filesystem::path root =
      temporary.make_empty_child("RecentProjectIsolation");
  ProjectSession project;
  QuaderEditorApi editor(project);
  ASSERT_TRUE(editor.project().create(root).ok());

  const std::filesystem::path isolated_store =
      test_profile / "Quader" / "recent-projects.json";
  EXPECT_TRUE(std::filesystem::is_regular_file(isolated_store));
  const std::vector<QuaderRecentProject> recent =
      editor.project().recent_projects();
  ASSERT_FALSE(recent.empty());
  EXPECT_EQ(recent.front().manifest_path, editor.project().manifest_path());
  EXPECT_TRUE(recent.front().available);
  EXPECT_TRUE(recent.front().manifest_last_modified_unix_ms.has_value());
  ASSERT_TRUE(recent.front().manifest_size_bytes.has_value());
  EXPECT_EQ(*recent.front().manifest_size_bytes,
            std::filesystem::file_size(editor.project().manifest_path()));
}

TEST(ProjectSessionWorkspace, ActiveSceneApisReportUnavailableWithZeroTabs) {
  ScopedProjectSessionDirectory temporary("zero-tab-api");
  ProjectSession project;
  QuaderEditorApi editor(project);
  ASSERT_TRUE(
      editor.project()
          .create(temporary.make_empty_child("ZeroTabApi"))
          .ok());
  const std::optional<SceneSessionId> active =
      editor.scene_documents().active_scene_id();
  ASSERT_TRUE(active.has_value());
  ASSERT_TRUE(editor.scene_documents().close(*active).ok());
  ASSERT_FALSE(editor.scene_documents().has_active_scene());

  const QuaderApiResult<QuaderEntityRef> entity =
      editor.scene().create_entity({.name = "Unavailable"});
  EXPECT_FALSE(entity.ok());
  EXPECT_EQ(entity.error_message(), "No scene is active.");
  const QuaderApiResult<QuaderModelingObjectRef> box =
      editor.modeling().create_box(
          QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                       .max = {1.0f, 1.0f, 1.0f}},
          "Unavailable Box");
  EXPECT_FALSE(box.ok());
  EXPECT_EQ(box.error_message(), "No scene is active.");
  const QuaderApiVoidResult selection =
      editor.selection().set_mode(QuaderSelectionMode::Object);
  EXPECT_FALSE(selection.ok());
  EXPECT_EQ(selection.error_message(), "No scene is active.");
  const QuaderApiVoidResult undo = editor.history().undo();
  EXPECT_FALSE(undo.ok());
  EXPECT_EQ(undo.error_message(), "No scene is active.");

  SceneSession &fallback = project.fallback_scene();
  const std::uint64_t fallback_layer_revision =
      fallback.scene().layer_revision();
  const bool fallback_can_undo = fallback.history().can_undo();
  const QuaderLayerId missing_layer = 1u;
  const std::array<QuaderLayerId, 2> missing_layers = {1u, 2u};
  auto expect_layer_unavailable = [](const QuaderApiStatus &status) {
    EXPECT_FALSE(status.ok());
    EXPECT_FALSE(status.changed());
    EXPECT_EQ(status.error_message(), "No scene is active.");
  };

  expect_layer_unavailable(editor.layers().create());
  expect_layer_unavailable(editor.layers().create_from_selection());
  expect_layer_unavailable(
      editor.layers().rename(missing_layer, "Unavailable"));
  expect_layer_unavailable(editor.layers().set_visible(missing_layer, false));
  expect_layer_unavailable(
      editor.layers().set_wireframe_color(missing_layer, {1.0f, 0.0f, 0.0f}));
  expect_layer_unavailable(editor.layers().set_locked(missing_layer, true));
  expect_layer_unavailable(editor.layers().add_selection(missing_layer));
  expect_layer_unavailable(editor.layers().remove_selection(missing_layer));
  expect_layer_unavailable(editor.layers().remove_selection_from_all_layers());
  expect_layer_unavailable(editor.layers().empty(missing_layer));
  expect_layer_unavailable(editor.layers().destroy(missing_layer));
  expect_layer_unavailable(editor.layers().destroy(missing_layers));

  EXPECT_TRUE(fallback.scene().layers().empty());
  EXPECT_EQ(fallback.scene().layer_revision(), fallback_layer_revision);
  EXPECT_EQ(fallback.history().can_undo(), fallback_can_undo);
}

TEST(ProjectSessionWorkspace,
     ProjectClipboardPastesLinkedInstancesInSourceSceneWithUndoRedo) {
  ScopedProjectSessionDirectory temporary("linked-instance-clipboard");
  ProjectSession project;
  const ProjectSessionResult created =
      project.create(temporary.make_empty_child("LinkedInstanceClipboard"));
  ASSERT_TRUE(created.ok) << created.message;
  ASSERT_TRUE(created.scene_session_id.has_value());

  QuaderEditorApi editor(project);
  const QuaderApiResult<QuaderModelingObjectRef> mesh =
      editor.modeling().create_box(
          QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                       .max = {1.0f, 1.0f, 1.0f}},
          "Clipboard Instance Member");
  ASSERT_TRUE(mesh.ok()) << mesh.error_message();
  ASSERT_TRUE(editor.modeling().create_instance().ok());

  OpenSceneSession *scene = project.active_scene();
  ASSERT_NE(scene, nullptr);
  std::vector<EntityId> selected = scene->session->scene().selected_entities();
  ASSERT_EQ(selected.size(), 1u);
  const EntityId source_occurrence = selected.front();
  const InstanceOccurrenceComponent *source_component =
      scene->session->scene().instance_occurrence(source_occurrence);
  ASSERT_NE(source_component, nullptr);
  const EntityId source_definition = source_component->definition;

  ASSERT_TRUE(editor.scene()
                  .set_transform(source_occurrence,
                                 QuaderTransform{
                                     .position = {7.0f, 2.0f, -5.0f},
                                     .rotation_euler = {0.0f, 0.4f, 0.0f},
                                 })
                  .ok());
  const TransformComponent source_transform =
      *scene->session->scene().transform(source_occurrence);

  const QuaderApiVoidResult copied =
      editor.commands().execute(quader_command::kCopySelection);
  ASSERT_TRUE(copied.ok()) << copied.error_message();
  EXPECT_FALSE(copied.changed());
  const QuaderApiVoidResult pasted =
      editor.commands().execute(quader_command::kPasteSelection);
  ASSERT_TRUE(pasted.ok()) << pasted.error_message();
  ASSERT_TRUE(pasted.changed());

  selected = scene->session->scene().selected_entities();
  ASSERT_EQ(selected.size(), 1u);
  const EntityId pasted_occurrence = selected.front();
  ASSERT_NE(pasted_occurrence, source_occurrence);
  const InstanceOccurrenceComponent *pasted_component =
      scene->session->scene().instance_occurrence(pasted_occurrence);
  ASSERT_NE(pasted_component, nullptr);
  EXPECT_EQ(pasted_component->definition, source_definition);
  const TransformComponent *pasted_transform =
      scene->session->scene().transform(pasted_occurrence);
  ASSERT_NE(pasted_transform, nullptr);
  expect_vec3_eq(pasted_transform->position, source_transform.position);
  expect_vec3_eq(pasted_transform->rotation_euler,
                 source_transform.rotation_euler);
  EXPECT_EQ(editor.query().history().undo_label, "Paste Instances");

  ASSERT_TRUE(editor.history().undo().ok());
  EXPECT_TRUE(scene->session->scene().exists(source_occurrence));
  EXPECT_FALSE(scene->session->scene().exists(pasted_occurrence));
  EXPECT_EQ(scene->session->scene().selected_entities(),
            std::vector<EntityId>{source_occurrence});
  ASSERT_TRUE(editor.history().redo().ok());
  EXPECT_TRUE(scene->session->scene().exists(pasted_occurrence));
  EXPECT_EQ(scene->session->scene()
                .instance_occurrence(pasted_occurrence)
                ->definition,
            source_definition);
  EXPECT_EQ(scene->session->scene().selected_entities(),
            std::vector<EntityId>{pasted_occurrence});
}

TEST(ProjectSessionWorkspace,
     ProjectClipboardImportsIndependentCrossSceneDefinitionWithSharedPayload) {
  ScopedProjectSessionDirectory temporary("cross-scene-instance-clipboard");
  ProjectSession project;
  const ProjectSessionResult created = project.create(
      temporary.make_empty_child("CrossSceneInstanceClipboard"));
  ASSERT_TRUE(created.ok) << created.message;
  ASSERT_TRUE(created.scene_session_id.has_value());
  const SceneSessionId source_scene_id = *created.scene_session_id;

  QuaderEditorApi editor(project);
  const QuaderApiResult<QuaderModelingObjectRef> mesh =
      editor.modeling().create_box(
          QuaderBounds{.min = {-2.0f, -1.0f, -3.0f},
                       .max = {2.0f, 1.0f, 3.0f}},
          "Cross Scene Canonical Member");
  ASSERT_TRUE(mesh.ok()) << mesh.error_message();
  ASSERT_TRUE(editor.modeling().create_instance().ok());

  OpenSceneSession *source_scene = project.find(source_scene_id);
  ASSERT_NE(source_scene, nullptr);
  const EntityId source_occurrence =
      source_scene->session->scene().selected_entities().front();
  const EntityId source_definition =
      source_scene->session->scene()
          .instance_occurrence(source_occurrence)
          ->definition;
  const EntityId source_member =
      source_scene->session->scene().children(source_definition).front();
  const AssetId source_mesh_id =
      source_scene->session->scene().mesh(source_member)->mesh;
  const std::shared_ptr<const MeshGeometryPayload> source_payload =
      source_scene->session->assets().mesh(source_mesh_id)->geometry_payload();
  ASSERT_TRUE(editor.scene()
                  .set_transform(source_occurrence,
                                 QuaderTransform{
                                     .position = {-4.0f, 3.0f, 9.0f},
                                     .rotation_euler = {0.2f, 0.5f, -0.1f},
                                     .scale = {1.5f, 0.75f, 2.0f},
                                 })
                  .ok());
  const QMat4 source_world =
      source_scene->session->scene().world_transform_matrix(source_occurrence);
  ASSERT_TRUE(editor.modeling().copy_selection().ok());

  const QuaderApiResult<SceneSessionId> destination_created =
      editor.scene_documents().new_scene();
  ASSERT_TRUE(destination_created.ok()) << destination_created.error_message();
  const SceneSessionId destination_scene_id = destination_created.value();
  ASSERT_NE(destination_scene_id, source_scene_id);

  const QuaderApiVoidResult pasted = editor.modeling().paste_selection();
  ASSERT_TRUE(pasted.ok()) << pasted.error_message();
  ASSERT_TRUE(pasted.changed());
  OpenSceneSession *destination_scene = project.find(destination_scene_id);
  ASSERT_NE(destination_scene, nullptr);
  std::vector<EntityId> selected =
      destination_scene->session->scene().selected_entities();
  ASSERT_EQ(selected.size(), 1u);
  const EntityId destination_occurrence = selected.front();
  const InstanceOccurrenceComponent *destination_component =
      destination_scene->session->scene().instance_occurrence(
          destination_occurrence);
  ASSERT_NE(destination_component, nullptr);
  const EntityId destination_definition = destination_component->definition;
  EXPECT_TRUE(destination_scene->session->scene()
                  .is_instance_definition_entity(destination_definition));
  const std::vector<EntityId> destination_members =
      destination_scene->session->scene().children(destination_definition);
  ASSERT_EQ(destination_members.size(), 1u);
  const EntityId destination_member = destination_members.front();
  const AssetId destination_mesh_id =
      destination_scene->session->scene().mesh(destination_member)->mesh;
  const MeshAsset *destination_mesh =
      destination_scene->session->assets().mesh(destination_mesh_id);
  ASSERT_NE(destination_mesh, nullptr);
  EXPECT_EQ(destination_mesh->geometry_payload().get(), source_payload.get());
  expect_vec3_eq(
      transform_point(destination_scene->session->scene()
                          .world_transform_matrix(destination_occurrence),
                      {}),
      transform_point(source_world, {}));

  // Cross-scene definitions are independent editor state even though their
  // immutable geometry payload is shared until a topology edit detaches it.
  const TransformComponent destination_member_transform =
      *destination_scene->session->scene().transform(destination_member);
  TransformComponent source_member_transform =
      *source_scene->session->scene().transform(source_member);
  source_member_transform.position.x += 12.0f;
  ASSERT_TRUE(source_scene->session->document_commands()
                  .set_transform(source_member, source_member_transform)
                  .ok);
  const TransformComponent *destination_member_after =
      destination_scene->session->scene().transform(destination_member);
  ASSERT_NE(destination_member_after, nullptr);
  expect_vec3_eq(destination_member_after->position,
                 destination_member_transform.position);
  expect_vec3_eq(destination_member_after->rotation_euler,
                 destination_member_transform.rotation_euler);
  expect_vec3_eq(destination_member_after->scale,
                 destination_member_transform.scale);

  EXPECT_EQ(editor.query().history().undo_label, "Paste Instances");
  ASSERT_TRUE(editor.history().undo().ok());
  EXPECT_FALSE(destination_scene->session->scene().exists(
      destination_occurrence));
  EXPECT_FALSE(destination_scene->session->scene().exists(
      destination_definition));
  EXPECT_TRUE(source_scene->session->scene().exists(source_occurrence));
  EXPECT_TRUE(source_scene->session->scene().exists(source_definition));
  ASSERT_TRUE(editor.history().redo().ok());
  EXPECT_TRUE(destination_scene->session->scene().exists(
      destination_occurrence));
  EXPECT_TRUE(destination_scene->session->scene().exists(
      destination_definition));
  EXPECT_EQ(destination_scene->session->scene()
                .instance_occurrence(destination_occurrence)
                ->definition,
            destination_definition);
}

TEST(ProjectSessionWorkspace,
     ProjectClipboardKeepsOrdinarySameSceneMeshCopyPasteWorking) {
  ScopedProjectSessionDirectory temporary("ordinary-modeling-clipboard");
  ProjectSession project;
  const ProjectSessionResult created = project.create(
      temporary.make_empty_child("OrdinaryModelingClipboard"));
  ASSERT_TRUE(created.ok) << created.message;

  QuaderEditorApi editor(project);
  ASSERT_TRUE(editor.modeling()
                  .create_box(QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                                           .max = {1.0f, 1.0f, 1.0f}},
                              "Ordinary Clipboard Mesh")
                  .ok());
  OpenSceneSession *scene = project.active_scene();
  ASSERT_NE(scene, nullptr);
  ASSERT_EQ(scene->session->modeling().objects().size(), 1u);
  ASSERT_TRUE(editor.commands()
                  .execute(quader_command::kCopySelection)
                  .ok());
  ASSERT_TRUE(editor.commands()
                  .execute(quader_command::kPasteSelection)
                  .ok());
  EXPECT_EQ(scene->session->modeling().objects().size(), 2u);
  ASSERT_TRUE(editor.history().undo().ok());
  EXPECT_EQ(scene->session->modeling().objects().size(), 1u);
}

TEST(ProjectSessionWorkspace,
     ProjectClipboardCutDeletesOnceAndSurvivesUndoRedoForPaste) {
  ScopedProjectSessionDirectory temporary("ordinary-modeling-cut");
  ProjectSession project;
  const ProjectSessionResult created =
      project.create(temporary.make_empty_child("OrdinaryModelingCut"));
  ASSERT_TRUE(created.ok) << created.message;

  QuaderEditorApi editor(project);
  ASSERT_TRUE(editor.modeling()
                  .create_box(QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                                           .max = {1.0f, 1.0f, 1.0f}},
                              "Ordinary Cut Mesh")
                  .ok());
  OpenSceneSession *scene = project.active_scene();
  ASSERT_NE(scene, nullptr);
  ASSERT_EQ(scene->session->modeling().objects().size(), 1u);

  const QuaderApiVoidResult cut =
      editor.commands().execute(quader_command::kCutSelection);
  ASSERT_TRUE(cut.ok()) << cut.error_message();
  ASSERT_TRUE(cut.changed());
  EXPECT_EQ(scene->session->modeling().objects().size(), 0u);

  ASSERT_TRUE(editor.history().undo().ok());
  EXPECT_EQ(scene->session->modeling().objects().size(), 1u);
  ASSERT_TRUE(editor.history().redo().ok());
  EXPECT_EQ(scene->session->modeling().objects().size(), 0u);
  ASSERT_TRUE(editor.history().undo().ok());
  EXPECT_EQ(scene->session->modeling().objects().size(), 1u);

  const QuaderApiVoidResult pasted =
      editor.commands().execute(quader_command::kPasteSelection);
  ASSERT_TRUE(pasted.ok()) << pasted.error_message();
  ASSERT_TRUE(pasted.changed());
  EXPECT_EQ(scene->session->modeling().objects().size(), 2u);
}

TEST(ProjectSessionWorkspace,
     SceneInstanceEditContextsPersistPerTabAndCleanUpWithTheirScene) {
  ScopedProjectSessionDirectory temporary("instance-edit-tab-context");
  const std::filesystem::path original_project =
      temporary.make_empty_child("InstanceEditTabs");
  const std::filesystem::path replacement_project =
      temporary.make_empty_child("InstanceEditTabsReplacement");
  ProjectSession project;
  QuaderEditorApi editor(project);
  ASSERT_TRUE(editor.project().create(original_project).ok());
  editor.scene_documents().set_runtime_hooks({
      .confirm_dirty_scene =
          [](const QuaderSceneDocumentInfo &, std::string_view) {
            return QuaderDirtySceneDecision::Discard;
          },
  });
  const std::optional<SceneSessionId> first_id =
      editor.scene_documents().active_scene_id();
  ASSERT_TRUE(first_id.has_value());

  const auto create_and_enter =
      [&](std::string name) -> std::pair<EntityId, EntityId> {
    const QuaderApiResult<QuaderModelingObjectRef> mesh =
        editor.modeling().create_box(
            QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                         .max = {1.0f, 1.0f, 1.0f}},
            std::move(name));
    EXPECT_TRUE(mesh.ok()) << mesh.error_message();
    const QuaderApiResult<QuaderInstanceCreateResult> created =
        editor.modeling().create_instance_from_selection();
    EXPECT_TRUE(created.ok()) << created.error_message();
    const EntityId occurrence = created.value().occurrence.root.id;
    const EntityId definition = created.value().definition.root.id;
    const QuaderApiVoidResult entered =
        editor.modeling().enter_instance_edit(occurrence);
    EXPECT_TRUE(entered.ok()) << entered.error_message();
    return {occurrence, definition};
  };

  const auto [first_occurrence, first_definition] =
      create_and_enter("First Tab Instance Member");
  const QuaderInstanceEditState first_state =
      editor.modeling().instance_edit_state();
  ASSERT_TRUE(first_state.active);
  EXPECT_EQ(first_state.occurrence.scene_session_id, *first_id);

  const QuaderApiResult<SceneSessionId> second_created =
      editor.scene_documents().new_scene();
  ASSERT_TRUE(second_created.ok()) << second_created.error_message();
  const SceneSessionId second_id = second_created.value();
  EXPECT_FALSE(editor.modeling().instance_edit_state().active);
  const auto [second_occurrence, second_definition] =
      create_and_enter("Second Tab Instance Member");
  const QuaderInstanceEditState second_state =
      editor.modeling().instance_edit_state();
  ASSERT_TRUE(second_state.active);
  EXPECT_EQ(second_state.occurrence.scene_session_id, second_id);

  ASSERT_TRUE(editor.scene_documents().activate(*first_id).ok());
  const QuaderInstanceEditState restored_first =
      editor.modeling().instance_edit_state();
  ASSERT_TRUE(restored_first.active);
  EXPECT_EQ(restored_first.occurrence.scene_session_id, *first_id);
  EXPECT_EQ(restored_first.occurrence.id, first_occurrence);
  EXPECT_EQ(restored_first.definition.id, first_definition);

  ASSERT_TRUE(editor.scene_documents().activate(second_id).ok());
  const QuaderInstanceEditState restored_second =
      editor.modeling().instance_edit_state();
  ASSERT_TRUE(restored_second.active);
  EXPECT_EQ(restored_second.occurrence.id, second_occurrence);
  EXPECT_EQ(restored_second.definition.id, second_definition);
  ASSERT_TRUE(editor.scene_documents().close(second_id).ok());
  ASSERT_EQ(editor.scene_documents().active_scene_id(), first_id);
  const QuaderInstanceEditState first_after_sibling_close =
      editor.modeling().instance_edit_state();
  ASSERT_TRUE(first_after_sibling_close.active);
  EXPECT_EQ(first_after_sibling_close.occurrence.id, first_occurrence);

  // Removing the occurrence invalidates and lazily clears only this scene's
  // transient edit context.
  ASSERT_TRUE(editor.scene().destroy(first_occurrence).ok());
  EXPECT_FALSE(editor.modeling().instance_edit_state().active);
  EXPECT_FALSE(editor.query().entity(first_occurrence).has_value());
  EXPECT_FALSE(editor.query().entity(first_definition).has_value());

  (void)create_and_enter("Replacement Cleanup Instance Member");
  ASSERT_TRUE(editor.modeling().instance_edit_state().active);
  const QuaderApiVoidResult replaced =
      editor.project().create(replacement_project);
  ASSERT_TRUE(replaced.ok()) << replaced.error_message();
  ASSERT_TRUE(replaced.changed());
  EXPECT_FALSE(editor.modeling().instance_edit_state().active);
}

} // namespace
