////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/session/project_session.hpp"

#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_command_ids.hpp"
#include "api/quader_editor_api.hpp"
#include "assets/default_material_catalog.hpp"
#include "editor/assets/project_asset_service.hpp"
#include "editor/commands/document/document_command_result.hpp"
#include "editor/curve/curve_tool_service.hpp"
#include "editor/history/editor_history_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/pivot/pivot_tool_service.hpp"
#include "editor/selection/editor_selection_service.hpp"
#include "editor/session/project_autosave_service.hpp"
#include "editor/session/quader_editor_api_session_access.hpp"
#include "io/project/qmat_codec.hpp"
#include "io/qdr/qdr_document_reader.hpp"
#include "io/qdr/qdr_document_writer.hpp"
#include "scene/scene_transform.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
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

using SceneScalarFingerprint = std::array<std::uint64_t, 12>;

[[nodiscard]] SceneScalarFingerprint
scene_scalar_fingerprint(const SceneDocument &scene) {
  return {
      scene.revision(),
      scene.content_revision(),
      scene.selection_revision(),
      scene.hierarchy_presentation_revision(),
      scene.visibility_revision(),
      scene.transform_revision(),
      scene.texture_mapping_revision(),
      scene.mesh_binding_revision(),
      scene.curve_binding_revision(),
      scene.material_binding_revision(),
      scene.render_component_revision(),
      scene.layer_revision(),
  };
}

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

/// Reads a completion record independently from the bridge's JSON decoder.
nlohmann::json read_sync_record(const std::filesystem::path &path) {
  const std::vector<char> bytes = read_file_bytes(path);
  return nlohmann::json::parse(bytes.begin(), bytes.end());
}

/// Checks the wire fingerprint against the actual committed document bytes.
void expect_sync_record(const nlohmann::json &record,
                        const std::filesystem::path &document,
                        std::string_view project_id,
                        std::string_view document_id,
                        std::string_view relative_path) {
  const std::vector<char> bytes = read_file_bytes(document);
  std::uint64_t hash = 14695981039346656037ull;
  for (const char byte : bytes) {
    hash ^= static_cast<unsigned char>(byte);
    hash *= 1099511628211ull;
  }
  std::ostringstream hex;
  hex << std::hex << std::setfill('0') << std::setw(16) << hash;
  EXPECT_EQ(record.at("version").get<int>(), 1);
  EXPECT_EQ(record.at("project_id").get<std::string>(), project_id);
  EXPECT_EQ(record.at("document_id").get<std::string>(), document_id);
  EXPECT_EQ(record.at("relative_path").get<std::string>(), relative_path);
  EXPECT_EQ(record.at("byte_count").get<std::string>(),
            std::to_string(bytes.size()));
  EXPECT_EQ(record.at("content_hash").get<std::string>(), hex.str());
  EXPECT_TRUE(SceneId::parse(record.at("save_token").get<std::string>()).ok());
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

/// Requires readers to share deletion while permitting atomic save replacement.
class ScopedSharedDeleteAccess final {
public:
  explicit ScopedSharedDeleteAccess(const std::filesystem::path &path)
      : handle_(::CreateFileW(path.c_str(), DELETE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                              nullptr)) {}

  ~ScopedSharedDeleteAccess() {
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
     SettingsOriginCapturesFocusBeforeRuntimeHookAndRejectsReboundScene) {
  quader::SceneSession first_scene;
  quader::SceneSession second_scene;
  auto binding =
      quader::QuaderApiSessionInternalAccess::from_scene_session(first_scene);
  auto second_binding =
      quader::QuaderApiSessionInternalAccess::from_scene_session(second_scene);
  quader::QuaderToolsApi tools(binding);
  ASSERT_TRUE(first_scene.viewport_workspace().set_layout(quader::ViewportLayoutId::FourQuad));
  ASSERT_TRUE(first_scene.viewport_workspace().focus_pane(quader::ViewportPaneId::Top));
  tools.set_runtime_hooks({.activate = [&](std::string_view id) {
    (void)first_scene.viewport_workspace().focus_pane(quader::ViewportPaneId::Front);
    return quader::QuaderToolsApi(binding).activate(id);
  }});
  ASSERT_TRUE(tools.activate(quader::ViewportToolId::Move).ok());
  EXPECT_EQ(tools.active_settings_presentation_origin().pane,
            quader::ViewportPaneId::Top);

  ASSERT_TRUE(second_scene.viewport_workspace().set_layout(quader::ViewportLayoutId::FourQuad));
  ASSERT_TRUE(second_scene.viewport_workspace().focus_pane(quader::ViewportPaneId::Side));
  tools.set_runtime_hooks({.activate = [&](std::string_view id) {
    quader::QuaderApiSessionInternalAccess::rebind(binding, second_binding);
    return quader::QuaderToolsApi(binding).activate(id);
  }});
  ASSERT_TRUE(tools.activate(quader::ViewportToolId::Move).ok());
  EXPECT_EQ(tools.active_settings_presentation_origin().serial, 1u);
  EXPECT_EQ(tools.active_settings_presentation_origin().pane,
            quader::ViewportPaneId::Side);

  second_scene.replace_document(quader::SceneContentDocument{});
  EXPECT_EQ(tools.active_settings_presentation_origin().serial, 0u);
  EXPECT_TRUE(tools.active_settings_presentation_origin().target_id.empty());

  ASSERT_TRUE(second_scene.viewport_workspace().focus_pane(quader::ViewportPaneId::Top));
  tools.set_runtime_hooks({.activate = [&](std::string_view id) {
    second_scene.replace_document(quader::SceneContentDocument{});
    (void)second_scene.viewport_workspace().focus_pane(quader::ViewportPaneId::Side);
    return quader::QuaderToolsApi(binding).activate(id);
  }});
  ASSERT_TRUE(tools.activate(quader::ViewportToolId::CreatePrimitive).ok());
  const auto replacement = tools.active_settings_presentation_origin();
  EXPECT_EQ(replacement.serial, 1u);
  EXPECT_EQ(replacement.target_id, "create_primitive");
  EXPECT_EQ(replacement.pane, quader::ViewportPaneId::Side);
}

TEST(ProjectSessionWorkspace,
     ToolActivationHookReceivesAcceptedPreviewAndInvalidRequestsNeverDispatch) {
  SceneSession scene;
  const auto binding = QuaderApiSessionInternalAccess::from_scene_session(scene);
  QuaderEditorApi editor(binding);
  const auto box = editor.modeling().create_box(
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      "Hook transition");
  ASSERT_TRUE(box.ok());
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::Move).ok());
  ASSERT_TRUE(scene.modeling().begin_transform_preview(false).ok);
  ASSERT_TRUE(scene.modeling().update_transform_translate_preview({2.0f, 0.0f, 0.0f}).ok);
  const auto history_before = scene.history().undo_count();
  int dispatch_count = 0;
  editor.tools().set_runtime_hooks({.activate = [&](std::string_view id) {
    ++dispatch_count;
    EXPECT_FALSE(scene.modeling().can_cancel_active_preview_for_undo());
    EXPECT_EQ(scene.history().undo_count(), history_before + 1U);
    return QuaderToolsApi(binding).activate(id);
  }});

  EXPECT_FALSE(editor.tools().activate("missing_tool").ok());
  EXPECT_EQ(dispatch_count, 0);
  EXPECT_TRUE(scene.modeling().can_cancel_active_preview_for_undo());
  EXPECT_EQ(scene.history().undo_count(), history_before);

  const auto switched = editor.tools().activate(ViewportToolId::Rotate);
  ASSERT_TRUE(switched.ok()) << switched.error_message();
  EXPECT_EQ(dispatch_count, 1);
  EXPECT_EQ(editor.tools().active_tool(), "rotate");
  EXPECT_EQ(scene.history().undo_count(), history_before + 1U);
  ASSERT_TRUE(editor.history().undo().ok());
  ASSERT_TRUE(editor.history().redo().ok());
}

TEST(ProjectSessionWorkspace,
     NewAndOpenedScenesAcceptOutgoingCurveDragsBeforeRebinding) {
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
    const auto undo_steps = source->session->history().undo_count();
    ASSERT_TRUE(select_and_begin_curve_control_drag(
        curve, {.point = before.points()[0].id}));
    ASSERT_TRUE(curve.update_drag({-5.0f, 1.0f, 2.0f}).ok);
    ASSERT_TRUE(curve.dragging());
    ASSERT_TRUE(source->session->modeling().curve_document_preview_active());
    const auto pending = *curve.preview_state().document;

    const auto switched = open_existing_file
        ? editor.scene_documents().open(target_path)
        : editor.scene_documents().new_scene();
    ASSERT_TRUE(switched.ok()) << switched.error_message();
    EXPECT_NE(project.active_scene_id(), source_id);
    EXPECT_FALSE(curve.active());
    EXPECT_FALSE(curve.dragging());
    EXPECT_FALSE(source->session->modeling().curve_document_preview_active());
    EXPECT_NE(source->session->content_revision_stamp(), source_stamp);
    EXPECT_TRUE(source->dirty());
    EXPECT_EQ(source->session->history().undo_count(), undo_steps + 1U);

    ASSERT_TRUE(editor.scene_documents().activate(source_id).ok());
    const auto restored = editor.modeling().curve_snapshot(object);
    ASSERT_TRUE(restored.ok()) << restored.error_message();
    EXPECT_TRUE(restored.value().document.has_same_geometry(pending));
    ASSERT_TRUE(editor.history().undo().ok());
    ASSERT_TRUE(editor.modeling().curve_snapshot(object).ok());
    EXPECT_TRUE(editor.modeling().curve_snapshot(object).value().document.has_same_geometry(before));
    ASSERT_TRUE(editor.history().redo().ok());
    EXPECT_TRUE(editor.modeling().curve_snapshot(object).value().document.has_same_geometry(pending));
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
    int host_rebind_requests = 0;
    QuaderEditorApiSessionAccess::set_scene_binding_changed_callback(
        editor, [&](SceneSession &) { ++host_rebind_requests; });
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
    EXPECT_EQ(host_rebind_requests, 0)
        << "failed scene transitions must not ask the host to abandon and "
           "reinstall live viewport interactions";
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

TEST(ProjectSessionWorkspace,
     AlreadyActiveOpenDoesNotRebindOrDisturbLiveViewportInteraction) {
  ScopedProjectSessionDirectory temporary("scene-open-active-noop");
  ProjectSession project;
  const auto created = project.create(temporary.make_empty_child("Project"));
  ASSERT_TRUE(created.ok) << created.message;
  QuaderEditorApi editor(project);
  (void)create_saved_transition_curve(project, editor);
  OpenSceneSession *source = project.active_scene();
  ASSERT_NE(source, nullptr);
  ASSERT_TRUE(source->path.has_value());
  ASSERT_TRUE(editor.tools().activate(kCurveToolId).ok());
  CurveToolService &curve = source->session->curve();
  ASSERT_TRUE(curve.preview_state().document.has_value());
  const auto point = curve.preview_state().document->points()[0].id;
  ASSERT_TRUE(select_and_begin_curve_control_drag(curve, {.point = point}));
  ASSERT_TRUE(curve.update_drag({-5.0f, 1.0f, 2.0f}).ok);
  const auto preview = *curve.preview_state().document;

  int host_rebind_requests = 0;
  QuaderEditorApiSessionAccess::set_scene_binding_changed_callback(
      editor, [&](SceneSession &) { ++host_rebind_requests; });
  const QuaderApiResult<SceneSessionId> opened =
      editor.scene_documents().open(*source->path);

  ASSERT_TRUE(opened.ok()) << opened.error_message();
  EXPECT_FALSE(opened.changed());
  EXPECT_EQ(opened.value(), source->session_id);
  EXPECT_EQ(host_rebind_requests, 0)
      << "opening the already-active tab must preserve host interaction "
         "ownership";
  EXPECT_EQ(project.active_scene(), source);
  EXPECT_TRUE(curve.active());
  EXPECT_TRUE(curve.dragging());
  EXPECT_TRUE(source->session->modeling().curve_document_preview_active());
  ASSERT_TRUE(curve.preview_state().document.has_value());
  EXPECT_TRUE(curve.preview_state().document->has_same_geometry(preview));
  EXPECT_TRUE(curve.cancel_drag().ok);
}

TEST(ProjectSessionWorkspace,
     RepeatedProjectCloseDoesNotRebindOrCancelFallbackCurveDrag) {
  ScopedProjectSessionDirectory temporary("project-close-noop-rebind");
  ProjectSession project;
  const ProjectSessionResult created =
      project.create(temporary.make_empty_child("Project"));
  ASSERT_TRUE(created.ok) << created.message;
  QuaderEditorApi editor(project);

  int host_rebind_requests = 0;
  SceneSession *rebound_session = nullptr;
  QuaderEditorApiSessionAccess::set_scene_binding_changed_callback(
      editor, [&](SceneSession &replacement) {
        ++host_rebind_requests;
        rebound_session = &replacement;
      });
  int project_closed_events = 0;
  const QuaderEventSubscription closed_subscription = editor.events().subscribe(
      quader_event::kProjectClosed,
      [&](const QuaderEvent &) { ++project_closed_events; });
  ASSERT_TRUE(closed_subscription.valid());

  const QuaderApiVoidResult first_close = editor.project().close();
  ASSERT_TRUE(first_close.ok()) << first_close.error_message();
  ASSERT_TRUE(first_close.changed());
  EXPECT_FALSE(project.has_project());
  EXPECT_EQ(host_rebind_requests, 1);
  EXPECT_EQ(project_closed_events, 1);
  ASSERT_EQ(rebound_session, &project.fallback_scene());

  SceneSession &fallback = project.fallback_scene();
  const std::array<modeling::CurvePointSpec, 2> points{{
      {.position = {-2.0f, 0.0f, 0.0f}},
      {.position = {2.0f, 0.0f, 0.0f}},
  }};
  auto curve_document = modeling::CurveDocument::make_bezier(points);
  ASSERT_TRUE(curve_document.ok()) << curve_document.error_message();
  const ModelingCreateBoxResult curve_object = fallback.modeling().create_curve(
      std::move(curve_document).value(), "Fallback Close No-op Curve");
  ASSERT_TRUE(curve_object.ok) << curve_object.message;
  ASSERT_TRUE(editor.tools().activate(kCurveToolId).ok());
  CurveToolService &curve = fallback.curve();
  ASSERT_TRUE(curve.preview_state().document.has_value());
  const modeling::CurvePointId point =
      curve.preview_state().document->points()[0].id;
  ASSERT_TRUE(select_and_begin_curve_control_drag(curve, {.point = point}));
  ASSERT_TRUE(curve.update_drag({-5.0f, 1.0f, 2.0f}).ok);
  ASSERT_TRUE(curve.dragging());
  ASSERT_TRUE(
      project.fallback_scene().modeling().curve_document_preview_active());
  const modeling::CurveDocument dragged_preview =
      *curve.preview_state().document;
  const SceneContentRevisionStamp content_stamp =
      project.fallback_scene().content_revision_stamp();

  for (int repetition = 0; repetition < 2; ++repetition) {
    SCOPED_TRACE(repetition);
    const QuaderApiVoidResult no_op = editor.project().close();
    ASSERT_TRUE(no_op.ok()) << no_op.error_message();
    EXPECT_FALSE(no_op.changed());
    EXPECT_EQ(host_rebind_requests, 1);
    EXPECT_EQ(project_closed_events, 1);
    EXPECT_EQ(rebound_session, &project.fallback_scene());
    EXPECT_TRUE(curve.active());
    EXPECT_TRUE(curve.dragging());
    EXPECT_TRUE(
        project.fallback_scene().modeling().curve_document_preview_active());
    ASSERT_TRUE(curve.preview_state().document.has_value());
    EXPECT_TRUE(
        curve.preview_state().document->has_same_geometry(dragged_preview));
    EXPECT_EQ(project.fallback_scene().content_revision_stamp(),
              content_stamp);
  }
  EXPECT_TRUE(curve.cancel_drag().ok);
}

TEST(ProjectSessionWorkspace,
     ProjectReplacementCloseEventsObserveTheReplacementViewportBinding) {
  ScopedProjectSessionDirectory temporary("project-rebind-before-events");
  const std::filesystem::path original_root =
      temporary.make_empty_child("Original");
  const std::filesystem::path created_replacement_root =
      temporary.make_empty_child("CreatedReplacement");
  const std::filesystem::path opened_replacement_root =
      temporary.make_empty_child("OpenedReplacement");

  std::filesystem::path opened_replacement_manifest;
  {
    ProjectSession prepared_project;
    QuaderEditorApi prepared_editor(prepared_project);
    ASSERT_TRUE(
        prepared_editor.project().create(opened_replacement_root).ok());
    const std::optional<SceneSessionId> prepared_scene =
        prepared_editor.scene_documents().active_scene_id();
    ASSERT_TRUE(prepared_scene.has_value());
    ASSERT_TRUE(prepared_editor.scene_documents()
                    .save_as(*prepared_scene,
                             project_path("Scenes/Prepared.qdr"))
                    .ok());
    opened_replacement_manifest =
        prepared_editor.project().manifest_path();
    ASSERT_TRUE(prepared_editor.project().close().ok());
  }

  ProjectSession project;
  QuaderEditorApi editor(project);
  ASSERT_TRUE(editor.project().create(original_root).ok());
  ASSERT_TRUE(
      editor.viewports().set_layout(ViewportLayoutId::FourQuad).changed());
  ASSERT_TRUE(editor.viewports().set_grid_preset(9).changed());

  int host_rebind_count = 0;
  QuaderEditorApiSessionAccess::set_scene_binding_changed_callback(
      editor, [&](SceneSession &) { ++host_rebind_count; });
  std::vector<int> close_event_rebind_counts;
  std::vector<ViewportLayoutId> close_event_layouts;
  std::vector<int> close_event_grid_presets;
  std::vector<bool> close_event_local_visibility;
  const QuaderEventSubscription closed = editor.events().subscribe(
      quader_event::kSceneClosed, [&](const QuaderEvent &) {
        close_event_rebind_counts.push_back(host_rebind_count);
        close_event_layouts.push_back(editor.viewports().layout());
        close_event_grid_presets.push_back(editor.viewports().grid_preset());
        close_event_local_visibility.push_back(
            editor.viewports().local_visibility_active());
      });
  ASSERT_TRUE(closed.valid());

  ASSERT_TRUE(editor.project().create(created_replacement_root).ok());
  ASSERT_EQ(close_event_rebind_counts, (std::vector<int>{1}));
  ASSERT_EQ(close_event_layouts,
            (std::vector<ViewportLayoutId>{ViewportLayoutId::Single}));
  ASSERT_EQ(close_event_grid_presets, (std::vector<int>{6}));
  ASSERT_EQ(close_event_local_visibility, (std::vector<bool>{false}));

  ASSERT_TRUE(
      editor.viewports().set_layout(ViewportLayoutId::TwoVertical).changed());
  ASSERT_TRUE(editor.viewports().set_grid_preset(7).changed());
  ASSERT_TRUE(editor.project().open(opened_replacement_manifest).ok());
  EXPECT_EQ(close_event_rebind_counts, (std::vector<int>{1, 2}));
  EXPECT_EQ(close_event_layouts,
            (std::vector<ViewportLayoutId>{ViewportLayoutId::Single,
                                           ViewportLayoutId::Single}));
  EXPECT_EQ(close_event_grid_presets, (std::vector<int>{6, 6}));
  EXPECT_EQ(close_event_local_visibility,
            (std::vector<bool>{false, false}));
}

TEST(ProjectSessionWorkspace,
     ActiveSceneCloseEventObservesTheNextSceneViewportBinding) {
  ScopedProjectSessionDirectory temporary("scene-close-rebind-before-event");
  ProjectSession project;
  QuaderEditorApi editor(project);
  ASSERT_TRUE(
      editor.project().create(temporary.make_empty_child("Project")).ok());
  const std::optional<SceneSessionId> first_id =
      editor.scene_documents().active_scene_id();
  ASSERT_TRUE(first_id.has_value());
  ASSERT_TRUE(editor.viewports().set_grid_preset(3).changed());

  const QuaderApiResult<SceneSessionId> second =
      editor.scene_documents().new_scene();
  ASSERT_TRUE(second.ok()) << second.error_message();
  ASSERT_TRUE(editor.viewports().set_grid_preset(8).changed());
  ASSERT_TRUE(editor.scene_documents().activate(*first_id).changed());
  ASSERT_EQ(editor.viewports().grid_preset(), 3);

  int host_rebind_count = 0;
  QuaderEditorApiSessionAccess::set_scene_binding_changed_callback(
      editor, [&](SceneSession &) { ++host_rebind_count; });
  std::optional<SceneSessionId> active_during_close_event;
  int grid_during_close_event = 0;
  bool local_visibility_during_close_event = true;
  int rebind_count_during_close_event = 0;
  const QuaderEventSubscription closed = editor.events().subscribe(
      quader_event::kSceneClosed, [&](const QuaderEvent &) {
        active_during_close_event = editor.scene_documents().active_scene_id();
        grid_during_close_event = editor.viewports().grid_preset();
        local_visibility_during_close_event =
            editor.viewports().local_visibility_active();
        rebind_count_during_close_event = host_rebind_count;
      });
  ASSERT_TRUE(closed.valid());

  const QuaderApiVoidResult close_result =
      editor.scene_documents().close(*first_id);
  ASSERT_TRUE(close_result.ok()) << close_result.error_message();
  EXPECT_TRUE(close_result.changed());
  EXPECT_EQ(active_during_close_event,
            std::optional<SceneSessionId>(second.value()));
  EXPECT_EQ(grid_during_close_event, 8);
  EXPECT_FALSE(local_visibility_during_close_event);
  EXPECT_EQ(rebind_count_during_close_event, 1);
  EXPECT_EQ(host_rebind_count, 1);
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
     UvPeelOptionsAreSharedWhileSelectionTokensStayWithinSceneSessions) {
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
  ASSERT_TRUE(editor.mode().set(QuaderEditorMode::Edit).ok());
  ASSERT_TRUE(editor.selection().select_edge(first_box.value(), 1, 2).ok());
  const auto first_token = editor.modeling().uv_peel_settings().selection_token;

  const auto added = editor.scene_documents().new_scene();
  ASSERT_TRUE(added.ok()) << added.error_message();
  OpenSceneSession *second = project.find(added.value());
  ASSERT_NE(second, nullptr);
  ASSERT_NE(first->session.get(), second->session.get());
  EXPECT_EQ(editor.modeling().uv_peel_settings().options, first_options);
  const QuaderUvPeelOptions second_options{.u_repeat = 4.0, .v_offset = -2.0};
  ASSERT_TRUE(editor.modeling().set_uv_peel_options(second_options).changed());
  EXPECT_FALSE(second->dirty());
  EXPECT_FALSE(editor.history().can_undo());
  const auto second_box = editor.modeling().create_box(
      {.min = {-1, -1, -1}, .max = {1, 1, 1}}, "Second guide");
  ASSERT_TRUE(second_box.ok()) << second_box.error_message();
  ASSERT_TRUE(editor.mode().set(QuaderEditorMode::Edit).ok());
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
  EXPECT_EQ(editor.modeling().uv_peel_settings().options, second_options);
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
     CameraNavigationStaysCleanAndExplicitSavePreservesPerspectivePose) {
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
  EXPECT_FALSE(scene->dirty())
      << "viewport navigation is workspace state, not authored scene content";

  const std::array<ViewportLocalVisibilityEntityId, 1> members{1u};
  ASSERT_TRUE(workspace.enter_local_visibility(members, members));
  camera.focus_origin(91.0f);
  camera.set_view_direction({1.0f, 0.0f, 0.0f});
  EXPECT_FALSE(scene->dirty());
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

  restored_scene->session->viewport_workspace()
      .camera(ViewportPaneId::Perspective)
      .focus_origin(52.0f);
  EXPECT_FALSE(restored_scene->dirty())
      << "navigating an opened scene must not create a save prompt";

  QuaderEditorApi restored_editor(restored);
  int close_prompts = 0;
  restored_editor.scene_documents().set_runtime_hooks({
      .confirm_dirty_scene =
          [&close_prompts](const QuaderSceneDocumentInfo &,
                           std::string_view) {
            ++close_prompts;
            return QuaderDirtySceneDecision::Cancel;
          },
  });
  const QuaderApiVoidResult closed = restored_editor.project().close();
  ASSERT_TRUE(closed.ok()) << closed.error_message();
  EXPECT_TRUE(closed.changed());
  EXPECT_EQ(close_prompts, 0)
      << "closing after navigation alone must bypass dirty-scene confirmation";
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
     RejectedSaveAsDestinationsPreserveLivePreviewAndPublishedState) {
  ScopedProjectSessionDirectory temporary("scene-save-scope");
  const auto recents_path = temporary.path() / "recent-scenes.json";
  ProjectSession project(recents_path);
  QuaderEditorApi editor(project);
  ASSERT_TRUE(editor.project().create(temporary.make_empty_child("Project")).ok());
  const auto object = create_saved_transition_curve(project, editor);
  ASSERT_TRUE(editor.scene().rename(object.entity, "Unsaved Curve Name").ok());
  OpenSceneSession *scene = project.active_scene();
  ASSERT_NE(scene, nullptr);
  ASSERT_TRUE(scene->dirty());
  ASSERT_TRUE(editor.tools().activate(kCurveToolId).ok());
  CurveToolService &curve = scene->session->curve();
  ASSERT_TRUE(curve.preview_state().document.has_value());
  ASSERT_TRUE(select_and_begin_curve_control_drag(
      curve, {.point = curve.preview_state().document->points()[0].id}));
  ASSERT_TRUE(curve.update_drag({-5.0f, 1.0f, 2.0f}).ok);
  const auto preview = *curve.preview_state().document;
  const auto stamp = scene->session->content_revision_stamp();
  const auto identity = scene->scene_id;
  const auto path = scene->path;
  ASSERT_TRUE(path.has_value());
  const auto name = scene->display_name;
  const auto generation = project.activation_generation();
  const auto manifest = ProjectManifestStore::serialize(*project.manifest());
  const auto manifest_bytes = read_file_bytes(project.manifest_path());
  const auto scene_bytes = read_file_bytes(project.root() / path->native_path());
  const auto recent_bytes = read_file_bytes(recents_path);
  const auto bridge_path = project.root() / ".quader/bridge/scenes" /
                           (std::string(identity.str()) + ".json");
  const auto bridge_bytes = read_file_bytes(bridge_path);
  int saved_events = 0;
  const auto subscription = editor.events().subscribe(
      quader_event::kSceneSaved, [&](const QuaderEvent &) { ++saved_events; });
  ASSERT_TRUE(subscription.valid());
  const auto expect_unchanged = [&] {
    EXPECT_EQ(project.active_scene(), scene);
    EXPECT_EQ(project.activation_generation(), generation);
    EXPECT_EQ(scene->scene_id, identity);
    EXPECT_EQ(scene->path, path);
    EXPECT_EQ(scene->display_name, name);
    EXPECT_TRUE(scene->dirty());
    EXPECT_EQ(scene->session->content_revision_stamp(), stamp);
    EXPECT_TRUE(curve.active());
    EXPECT_TRUE(curve.dragging());
    EXPECT_TRUE(scene->session->modeling().curve_document_preview_active());
    ASSERT_TRUE(curve.preview_state().document.has_value());
    EXPECT_TRUE(curve.preview_state().document->has_same_geometry(preview));
    EXPECT_EQ(ProjectManifestStore::serialize(*project.manifest()), manifest);
    EXPECT_EQ(read_file_bytes(project.manifest_path()), manifest_bytes);
    EXPECT_EQ(read_file_bytes(project.root() / path->native_path()), scene_bytes);
    EXPECT_EQ(read_file_bytes(recents_path), recent_bytes);
    EXPECT_EQ(read_file_bytes(bridge_path), bridge_bytes);
    EXPECT_EQ(saved_events, 0);
  };

  for (const std::string_view destination :
       {".quader/Rejected.qdr", ".quader/Internal/Rejected.qdr"}) {
    SCOPED_TRACE(destination);
    const auto relative = project_path(destination);
    const auto absolute = project.root() / relative.native_path();
    std::filesystem::create_directories(absolute.parent_path());
    {
      std::ofstream existing(absolute, std::ios::binary);
      ASSERT_TRUE(existing.is_open());
      existing << "Preserve existing destination bytes";
    }
    const auto destination_bytes = read_file_bytes(absolute);
    editor.scene_documents().set_runtime_hooks({
        .choose_save_scene_as_path = [absolute](SceneSessionId) {
          return QuaderSceneSavePathResult{.path = absolute};
        },
    });
    for (int route = 0; route < 4; ++route) {
      SCOPED_TRACE(route);
      if (route == 0) {
        const auto rejected = project.save_as(scene->session_id, relative);
        EXPECT_FALSE(rejected.ok);
        EXPECT_FALSE(rejected.changed);
        EXPECT_NE(rejected.message.find("project"), std::string::npos);
      } else {
        const auto rejected = route == 1
            ? editor.scene_documents().save_as(scene->session_id, relative)
            : route == 2
                ? editor.scene_documents().save_as_native_path(
                      scene->session_id, absolute)
                : editor.scene_documents().save_as(scene->session_id);
        EXPECT_FALSE(rejected.ok());
        EXPECT_FALSE(rejected.changed());
        EXPECT_NE(rejected.error_message().find("project"), std::string::npos);
      }
      expect_unchanged();
      EXPECT_EQ(project.manifest()->find_scene(relative), nullptr);
      EXPECT_EQ(read_file_bytes(absolute), destination_bytes);
    }
  }

  const std::array native_destinations{
      project.root() / "Scenes/../Scenes/Rejected.qdr",
      temporary.make_empty_child("OtherProject") / "Scenes/Rejected.qdr"};
  for (const auto &absolute : native_destinations) {
    SCOPED_TRACE(absolute.string());
    std::filesystem::create_directories(absolute.parent_path());
    {
      std::ofstream existing(absolute, std::ios::binary);
      ASSERT_TRUE(existing.is_open());
      existing << "Preserve native destination bytes";
    }
    const auto destination_bytes = read_file_bytes(absolute);
    editor.scene_documents().set_runtime_hooks({
        .choose_save_scene_as_path = [absolute](SceneSessionId) {
          return QuaderSceneSavePathResult{.path = absolute};
        },
    });
    for (const bool use_dialog : {false, true}) {
      SCOPED_TRACE(use_dialog);
      const auto rejected = use_dialog
          ? editor.scene_documents().save_as(scene->session_id)
          : editor.scene_documents().save_as_native_path(scene->session_id, absolute);
      EXPECT_FALSE(rejected.ok());
      EXPECT_FALSE(rejected.changed());
      EXPECT_NE(rejected.error_message().find("project"), std::string::npos);
      expect_unchanged();
      EXPECT_EQ(read_file_bytes(absolute), destination_bytes);
    }
  }
}

TEST(ProjectSessionWorkspace, SaveAsAcceptsDeeplyNestedMixedAssetDestinations) {
  ScopedProjectSessionDirectory temporary("nested-scene-save");
  ProjectSession project;
  QuaderEditorApi editor(project);
  ASSERT_TRUE(editor.project().create(temporary.make_empty_child("Project")).ok());
  const auto tab = project.active_scene_id();
  const auto relative = project_path("Worlds/Chapter/Maps/Interior/Room.qdr");
  const auto absolute = project.root() / relative.native_path();
  std::filesystem::create_directories(absolute.parent_path());
  const auto saved = editor.scene_documents().save_as_native_path(tab, absolute);
  ASSERT_TRUE(saved.ok()) << saved.error_message();
  ASSERT_NE(project.find(tab), nullptr);
  EXPECT_EQ(project.find(tab)->path, relative);
  EXPECT_FALSE(project.find(tab)->dirty());
  ASSERT_NE(project.manifest()->find_scene(relative), nullptr);
  const auto document = QdrDocumentReader::read_project_document(absolute);
  ASSERT_TRUE(document.ok) << document.message;
  EXPECT_EQ(SceneId::from_bytes(document.scene_uuid), project.find(tab)->scene_id);
}

TEST(ProjectSessionWorkspace, SceneAssetCreationDoesNotReplaceOrCleanTheDraft) {
  ScopedProjectSessionDirectory temporary("detached-scene-asset");
  ProjectSession project;
  QuaderEditorApi editor(project);
  ASSERT_TRUE(editor.project().create(temporary.make_empty_child("Project")).ok());
  ASSERT_TRUE(editor.scene().create_entity({.name = "Resident Draft"}).ok());
  auto *resident = project.active_scene();
  const auto id = resident->scene_id;
  const auto generation = project.activation_generation();
  const auto stamp = resident->session->content_revision_stamp();
  const auto history = resident->session->history().undo_count();
  const auto path = project_path("New Scene.qdr");
  const auto created = project.create_scene_asset(path);
  ASSERT_TRUE(created.ok()) << created.error().message;
  EXPECT_EQ(project.open_scenes().size(), 1u);
  EXPECT_EQ(project.active_scene(), resident);
  EXPECT_EQ(project.activation_generation(), generation);
  EXPECT_EQ(resident->scene_id, id);
  EXPECT_FALSE(resident->saved());
  EXPECT_TRUE(resident->dirty());
  EXPECT_EQ(resident->session->content_revision_stamp(), stamp);
  EXPECT_EQ(resident->session->history().undo_count(), history);
  const auto read = QdrDocumentReader::read_project_document(project.root() / path.native_path());
  ASSERT_TRUE(read.ok) << read.message;
  EXPECT_EQ(SceneId::from_bytes(read.scene_uuid), created.value().id);
  const auto snapshot = qdr_project_write_snapshot_from_document(read.document);
  ASSERT_TRUE(snapshot.ok());
  EXPECT_TRUE(snapshot.value().entities.empty());
  EXPECT_NE(created.value().id, id);
  EXPECT_FALSE(project.create_scene_asset(path).ok());
}

TEST(ProjectSessionWorkspace, SavedSceneDuplicationAndDraftCopyHaveSeparateSources) {
  ScopedProjectSessionDirectory temporary("scene-copy-sources");
  ProjectSession project;
  QuaderEditorApi editor(project);
  ASSERT_TRUE(editor.project().create(temporary.make_empty_child("Project")).ok());
  ASSERT_TRUE(editor.scene().create_entity({.name = "Saved Entity"}).ok());
  const auto tab = project.active_scene_id();
  const auto source = project_path("Source.qdr");
  ASSERT_TRUE(editor.scene_documents().save_as(tab, source).ok());
  const auto identity = project.find(tab)->scene_id;
  const auto source_bytes = read_file_bytes(project.root() / source.native_path());
  ASSERT_TRUE(editor.scene().create_entity({.name = "Unsaved Entity"}).ok());
  const auto stamp = project.find(tab)->session->content_revision_stamp();
  const auto history = project.find(tab)->session->history().undo_count();
  const auto clone_id = SceneId::generate();
  const auto copy_path = project_path("Saved Copy.qdr");
  const auto copied = project.stage_scene_asset_copy(source,
      project.root() / copy_path.native_path(), clone_id);
  ASSERT_TRUE(copied.ok()) << copied.error().message;
  const auto saved_copy = QdrDocumentReader::read_project_document(project.root() / copy_path.native_path());
  ASSERT_TRUE(saved_copy.ok) << saved_copy.message;
  EXPECT_EQ(SceneId::from_bytes(saved_copy.scene_uuid), clone_id);
  const auto saved_snapshot = qdr_project_write_snapshot_from_document(saved_copy.document);
  ASSERT_TRUE(saved_snapshot.ok());
  EXPECT_EQ(saved_snapshot.value().entities.size(), 1u);
  const auto draft_path = project_path("Draft Copy.qdr");
  ASSERT_TRUE(editor.scene_documents().save_copy(tab, draft_path).ok());
  const auto draft_copy = QdrDocumentReader::read_project_document(project.root() / draft_path.native_path());
  ASSERT_TRUE(draft_copy.ok) << draft_copy.message;
  const auto draft_snapshot = qdr_project_write_snapshot_from_document(draft_copy.document);
  ASSERT_TRUE(draft_snapshot.ok());
  EXPECT_EQ(draft_snapshot.value().entities.size(), 2u);
  EXPECT_NE(SceneId::from_bytes(draft_copy.scene_uuid), identity);
  EXPECT_EQ(project.find(tab)->scene_id, identity);
  EXPECT_EQ(project.find(tab)->path, source);
  EXPECT_EQ(project.find(tab)->session->content_revision_stamp(), stamp);
  EXPECT_EQ(project.find(tab)->session->history().undo_count(), history);
  EXPECT_TRUE(project.find(tab)->dirty());
  EXPECT_EQ(read_file_bytes(project.root() / source.native_path()), source_bytes);
  EXPECT_EQ(project.open_scenes().size(), 1u);
}

TEST(ProjectSessionWorkspace, AssetRelocationKeepsResidentIdentityHistoryAndRecentOrder) {
  ScopedProjectSessionDirectory temporary("scene-asset-relocation");
  ProjectSession project(temporary.path() / "recent.json");
  QuaderEditorApi editor(project);
  ASSERT_TRUE(editor.project().create(temporary.make_empty_child("Project")).ok());
  const auto first = project.active_scene_id();
  const auto source = project_path("Scenes/First.qdr");
  ASSERT_TRUE(editor.scene_documents().save_as(first, source).ok());
  ASSERT_TRUE(editor.scene().create_entity({.name = "Retained Draft"}).ok());
  auto *resident = project.find(first);
  const auto identity = resident->scene_id;
  const auto stamp = resident->session->content_revision_stamp();
  const auto history = resident->session->history().undo_count();
  ASSERT_TRUE(editor.scene_documents().new_scene().ok());
  const auto second = project.active_scene_id();
  ASSERT_TRUE(editor.scene_documents().save_as(second, project_path("Second.qdr")).ok());
  const auto recent_before = project.recent_scenes();
  ASSERT_EQ(recent_before.size(), 2u);
  const auto generation = project.activation_generation();
  const auto destination = project_path("Renamed.qdr");
  auto candidate = *project.manifest();
  ASSERT_TRUE(candidate.unregister_scene(identity).ok());
  ASSERT_TRUE(candidate.register_scene({.id = identity, .path = destination}).ok());
  ASSERT_TRUE(project.prepare_asset_manifest(candidate).ok);
  std::filesystem::rename(project.root() / source.native_path(),
                          project.root() / destination.native_path());
  ASSERT_TRUE(project.install_asset_manifest(std::move(candidate)).ok);
  EXPECT_EQ(project.find(first), resident);
  EXPECT_EQ(resident->scene_id, identity);
  EXPECT_EQ(resident->path, destination);
  EXPECT_EQ(resident->display_name, "Renamed");
  EXPECT_TRUE(resident->dirty());
  EXPECT_EQ(resident->session->content_revision_stamp(), stamp);
  EXPECT_EQ(resident->session->history().undo_count(), history);
  EXPECT_EQ(project.active_scene_id(), second);
  EXPECT_EQ(project.activation_generation(), generation);
  EXPECT_EQ(project.open_scenes()[0]->session_id, first);
  const auto recent_after = project.recent_scenes();
  ASSERT_EQ(recent_after.size(), 2u);
  EXPECT_EQ(recent_after[0].scene_id, recent_before[0].scene_id);
  EXPECT_EQ(recent_after[1].scene_id, identity);
  EXPECT_EQ(recent_after[1].scene_path, destination);
  EXPECT_EQ(recent_after[1].scene_name, "Renamed");
}

TEST(ProjectSessionWorkspace, CaseOnlySceneAssetRenamePreservesTheOpenDraft) {
  ScopedProjectSessionDirectory temporary("case-only-scene-rename");
  ProjectSession project;
  QuaderEditorApi editor(project);
  ASSERT_TRUE(editor.project().create(temporary.make_empty_child("Project")).ok());
  const auto tab = project.active_scene_id();
  const auto source = project_path("Original.qdr");
  const auto destination = project_path("original.qdr");
  ASSERT_TRUE(editor.scene_documents().save_as(tab, source).ok());
  ASSERT_TRUE(editor.scene().create_entity({.name = "Retained Draft"}).ok());
  const auto identity = project.find(tab)->scene_id;
  const auto stamp = project.find(tab)->session->content_revision_stamp();
  auto candidate = *project.manifest();
  ASSERT_TRUE(candidate.unregister_scene(identity).ok());
  ASSERT_TRUE(candidate.register_scene({.id = identity, .path = destination}).ok());
  const auto prepared = project.prepare_asset_manifest(candidate);
  ASSERT_TRUE(prepared.ok) << prepared.message;
  const auto staging = project.root() / "case-rename-stage";
  std::filesystem::rename(project.root() / source.native_path(), staging);
  std::filesystem::rename(staging, project.root() / destination.native_path());
  const auto installed = project.install_asset_manifest(std::move(candidate));
  ASSERT_TRUE(installed.ok) << installed.message;
  EXPECT_EQ(project.find(tab)->scene_id, identity);
  EXPECT_EQ(project.find(tab)->path, destination);
  EXPECT_EQ(project.find(tab)->display_name, "original");
  EXPECT_EQ(project.find(tab)->session->content_revision_stamp(), stamp);
  EXPECT_TRUE(project.find(tab)->dirty());
}

TEST(ProjectSessionWorkspace, OpenRecoversCommittedCaseRenameBeforeValidatingScenePaths) {
  ScopedProjectSessionDirectory temporary("recover-case-scene-rename");
  ProjectSession project;
  ASSERT_TRUE(project.create(temporary.make_empty_child("Project")).ok);
  const auto source = project_path("Original.qdr");
  const auto destination = project_path("original.qdr");
  ASSERT_TRUE(project.save_as(project.active_scene_id(), source).ok);
  const auto identity = project.active_scene()->scene_id;
  const auto root = project.root();
  const auto manifest_path = project.manifest_path();
  auto candidate = *project.manifest();
  ASSERT_TRUE(candidate.unregister_scene(identity).ok());
  ASSERT_TRUE(candidate.register_scene({.id = identity, .path = destination}).ok());
  ASSERT_TRUE(project.prepare_asset_manifest(candidate).ok);
  ASSERT_TRUE(project.close().ok);
  const auto baseline_bytes = read_file_bytes(manifest_path);
  const std::string baseline(baseline_bytes.begin(), baseline_bytes.end());
  const auto staging = root / "case-rename-stage";
  std::filesystem::rename(root / source.native_path(), staging);
  std::filesystem::rename(staging, root / destination.native_path());

  // File-action compensation has separate service coverage. This journal isolates
  // the committed metadata publication needed before physical spelling checks.
  std::filesystem::create_directories(root / ".quader");
  const auto journal_path = root / ".quader" /
      ("asset-organization-" + std::string(ProjectAssetId::generate().str()) + ".json");
  const nlohmann::json journal{
      {"version", 1}, {"state", "committed"},
      {"project", std::string(candidate.id().str())},
      {"manifest", "Project.qproj"}, {"baseline", baseline},
      {"candidate", ProjectManifestStore::serialize(candidate)},
      {"actions", nlohmann::json::array()},
  };
  std::ofstream(journal_path, std::ios::binary) << journal.dump(2);
  const auto interrupted = ProjectManifestStore::open(manifest_path);
  ASSERT_FALSE(interrupted.ok());
  EXPECT_EQ(interrupted.error().code, ProjectIoErrorCode::PathCaseCollision);

  const auto reopened = project.open(manifest_path);
  ASSERT_TRUE(reopened.ok) << reopened.message;
  ASSERT_NE(project.active_scene(), nullptr);
  EXPECT_EQ(project.active_scene()->scene_id, identity);
  EXPECT_EQ(project.active_scene()->path, destination);
  ASSERT_NE(project.manifest()->find_scene(identity), nullptr);
  EXPECT_EQ(project.manifest()->find_scene(identity)->path, destination);
  EXPECT_FALSE(std::filesystem::exists(journal_path));
  const auto persisted = ProjectManifestStore::open(manifest_path);
  ASSERT_TRUE(persisted.ok()) << persisted.error().message;
  EXPECT_EQ(persisted.value().manifest.active_scene_path(), destination);
}

TEST(ProjectSessionWorkspace, ProjectGenerationIgnoresSceneTabTransitions) {
  ScopedProjectSessionDirectory temporary("project-generation");
  ProjectSession project;
  EXPECT_EQ(project.project_generation(), 0u);
  ASSERT_TRUE(project.create(temporary.make_empty_child("Project")).ok);
  const auto created = project.project_generation();
  EXPECT_EQ(created, 1u);
  const auto first = project.active_scene_id();
  ASSERT_TRUE(project.save_as(first, project_path("First.qdr")).ok);
  ASSERT_TRUE(project.new_scene().ok);
  const auto second = project.active_scene_id();
  ASSERT_TRUE(project.save_as(second, project_path("Second.qdr")).ok);
  ASSERT_TRUE(project.activate(first).ok);
  ASSERT_TRUE(project.close_scene(second).ok);
  EXPECT_EQ(project.project_generation(), created);
  const auto manifest = project.manifest_path();
  ASSERT_TRUE(project.open(manifest).ok);
  EXPECT_EQ(project.project_generation(), created + 1u);
  ASSERT_TRUE(project.close().ok);
  EXPECT_EQ(project.project_generation(), created + 2u);
  EXPECT_FALSE(project.close().changed);
  EXPECT_EQ(project.project_generation(), created + 2u);
}

TEST(ProjectSessionWorkspace, FailedAssetCatalogPreparationPreservesManifestAndOpenScenePaths) {
  ScopedProjectSessionDirectory temporary("asset-publication-failure");
  ProjectSession project;
  QuaderEditorApi editor(project);
  ASSERT_TRUE(editor.project().create(temporary.make_empty_child("Project")).ok());
  const auto tab = project.active_scene_id();
  const auto source = project_path("Before.qdr");
  ASSERT_TRUE(editor.scene_documents().save_as(tab, source).ok());
  ASSERT_TRUE(editor.scene().create_entity({.name = "Dirty Draft"}).ok());
  auto *scene = project.find(tab);
  const auto identity = scene->scene_id;
  const auto stamp = scene->session->content_revision_stamp();
  const auto history = scene->session->history().undo_count();
  const auto manifest = ProjectManifestStore::serialize(*project.manifest());
  const auto manifest_bytes = read_file_bytes(project.manifest_path());
  const auto source_bytes = read_file_bytes(project.root() / source.native_path());
  const auto duplicate_texture_id = ProjectAssetId::generate();
  for (const auto *name : {"ExternalA", "ExternalB"}) {
    const std::string image = std::string(name) + ".png";
    std::filesystem::copy_file(
        project.root() / project_path(kDefaultProjectAlbedoTexturePath).native_path(),
        project.root() / image);
    ProjectMaterialDocument material{
        .id = ProjectAssetId::generate(), .name = name};
    material.standard_material()->base_color_texture = ProjectMaterialTextureSlot{
        .id = duplicate_texture_id, .project_relative_path = image};
    ASSERT_TRUE(QmatCodec::write_atomic(project.workspace()->file_system,
        project_path(std::string(name) + ".qmat"), material).ok());
  }
  const auto rejected = project.project_assets().rename(source, "After");
  EXPECT_FALSE(rejected.ok);
  EXPECT_EQ(project.find(tab), scene);
  EXPECT_EQ(scene->scene_id, identity);
  EXPECT_EQ(scene->path, source);
  EXPECT_EQ(scene->display_name, "Before");
  EXPECT_TRUE(scene->dirty());
  EXPECT_EQ(scene->session->content_revision_stamp(), stamp);
  EXPECT_EQ(scene->session->history().undo_count(), history);
  EXPECT_EQ(ProjectManifestStore::serialize(*project.manifest()), manifest);
  EXPECT_EQ(read_file_bytes(project.manifest_path()), manifest_bytes);
  EXPECT_EQ(read_file_bytes(project.root() / source.native_path()), source_bytes);
  EXPECT_FALSE(std::filesystem::exists(project.root() / "After.qdr"));
}

TEST(ProjectSessionWorkspace, SceneDraftCopyPreflightsThenAcceptsPendingCurveEdits) {
  ScopedProjectSessionDirectory temporary("scene-copy-pending-curve");
  ProjectSession project;
  QuaderEditorApi editor(project);
  ASSERT_TRUE(editor.project().create(temporary.make_empty_child("Project")).ok());
  const auto object = create_saved_transition_curve(project, editor);
  auto *scene = project.active_scene();
  const auto identity = scene->scene_id;
  const auto source = *scene->path;
  const auto saved_bytes = read_file_bytes(project.root() / source.native_path());
  const auto history = scene->session->history().undo_count();
  ASSERT_TRUE(editor.tools().activate(kCurveToolId).ok());
  auto &curve = scene->session->curve();
  ASSERT_TRUE(curve.preview_state().document.has_value());
  ASSERT_TRUE(select_and_begin_curve_control_drag(
      curve, {.point = curve.preview_state().document->points()[0].id}));
  ASSERT_TRUE(curve.update_drag({-5.0f, 1.0f, 2.0f}).ok);
  const auto pending = *curve.preview_state().document;
  ASSERT_TRUE(project.scene_has_pending_edits(scene->session_id));
  EXPECT_FALSE(editor.scene_documents().save_copy(
      scene->session_id, project_path(".quader/Rejected.qdr")).ok());
  EXPECT_TRUE(curve.dragging());
  const auto destination = project_path("Curve Copy.qdr");
  const auto copied = editor.scene_documents().save_copy(scene->session_id, destination);
  ASSERT_TRUE(copied.ok()) << copied.error_message();
  EXPECT_EQ(project.active_scene(), scene);
  EXPECT_EQ(scene->scene_id, identity);
  EXPECT_EQ(scene->path, source);
  EXPECT_TRUE(scene->dirty());
  EXPECT_EQ(scene->session->history().undo_count(), history + 1u);
  EXPECT_FALSE(project.scene_has_pending_edits(scene->session_id));
  const auto read = QdrDocumentReader::read_project_document(project.root() / destination.native_path());
  ASSERT_TRUE(read.ok) << read.message;
  const auto snapshot = qdr_project_write_snapshot_from_document(read.document);
  ASSERT_TRUE(snapshot.ok());
  ASSERT_EQ(snapshot.value().curves.size(), 1u);
  EXPECT_TRUE(snapshot.value().curves[0].document->has_same_geometry(pending));
  EXPECT_EQ(read_file_bytes(project.root() / source.native_path()), saved_bytes);
  EXPECT_TRUE(editor.modeling().curve_snapshot(object).value().document.has_same_geometry(pending));
}

TEST(ProjectSessionWorkspace, SceneAssetRemovalClosesApprovedTabsOnlyAfterManifestCommit) {
  ScopedProjectSessionDirectory temporary("scene-removal-publication");
  ProjectSession project;
  QuaderEditorApi editor(project);
  ASSERT_TRUE(editor.project().create(temporary.make_empty_child("Project")).ok());
  const auto first = project.active_scene_id();
  ASSERT_TRUE(editor.scene_documents().save_as(first, project_path("First.qdr")).ok());
  ASSERT_TRUE(editor.scene_documents().new_scene().ok());
  const auto second = project.active_scene_id();
  ASSERT_TRUE(editor.scene_documents().save_as(second, project_path("Second.qdr")).ok());
  ASSERT_TRUE(editor.scene().create_entity({.name = "Dirty Removed Scene"}).ok());
  const auto removed_id = project.find(second)->scene_id;
  const auto approved = std::array{removed_id};
  auto candidate = *project.manifest();
  ASSERT_TRUE(candidate.unregister_scene(removed_id).ok());
  EXPECT_FALSE(project.prepare_asset_manifest(candidate).ok);
  ASSERT_TRUE(project.prepare_asset_manifest(candidate, approved).ok);
  const auto generation = project.activation_generation();
  {
    ScopedManifestWriteFailure blocked(project.manifest_path());
    ASSERT_TRUE(blocked.ready()) << blocked.message();
    EXPECT_FALSE(project.install_asset_manifest(candidate, approved).ok);
    ASSERT_NE(project.find(second), nullptr);
    EXPECT_TRUE(project.find(second)->dirty());
    EXPECT_EQ(project.active_scene_id(), second);
    EXPECT_EQ(project.activation_generation(), generation);
  }
  ASSERT_TRUE(project.install_asset_manifest(std::move(candidate), approved).ok);
  EXPECT_EQ(project.find(second), nullptr);
  EXPECT_EQ(project.open_scenes().size(), 1u);
  EXPECT_EQ(project.active_scene_id(), first);
  EXPECT_EQ(project.activation_generation(), generation + 1u);
  EXPECT_EQ(project.manifest()->open_scene_paths().size(), 1u);
}

TEST(ProjectSessionWorkspace, FailedSceneAssetCreationLeavesNoFileOrTab) {
  ScopedProjectSessionDirectory temporary("scene-asset-create-rollback");
  ProjectSession project;
  ASSERT_TRUE(project.create(temporary.make_empty_child("Project")).ok);
  const auto tab = project.active_scene_id();
  const auto destination = project_path("Failed.qdr");
  {
    ScopedManifestWriteFailure blocked(project.manifest_path());
    ASSERT_TRUE(blocked.ready()) << blocked.message();
    EXPECT_FALSE(project.create_scene_asset(destination).ok());
  }
  EXPECT_EQ(project.manifest()->find_scene(destination), nullptr);
  EXPECT_FALSE(std::filesystem::exists(project.root() / destination.native_path()));
  EXPECT_EQ(project.active_scene_id(), tab);
  EXPECT_EQ(project.open_scenes().size(), 1u);
}

TEST(ProjectSessionWorkspace, OpeningLegacyManifestDefersMigrationUntilSceneSave) {
  ScopedProjectSessionDirectory temporary("legacy-migration-deferred");
  ProjectSession project;
  ASSERT_TRUE(project.create(temporary.make_empty_child("Project")).ok);
  ASSERT_TRUE(project.save_as(project.active_scene_id(), project_path("Scene.qdr")).ok);
  const auto manifest_path = project.manifest_path();
  ASSERT_TRUE(project.close().ok);
  auto encoded = nlohmann::json::parse(read_file_bytes(manifest_path));
  encoded["version"] = 4u;
  encoded.erase("textures");
  encoded.erase("deletedTextureIds");
  {
    std::ofstream output(manifest_path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(output.is_open());
    output << encoded.dump(2);
  }
  const auto legacy_bytes = read_file_bytes(manifest_path);
  ASSERT_TRUE(project.open(manifest_path).ok);
  EXPECT_EQ(read_file_bytes(manifest_path), legacy_bytes);
  EXPECT_EQ(project.manifest()->loaded_version(), 4u);
  ASSERT_TRUE(project.save(project.active_scene_id()).ok);
  const auto migrated = nlohmann::json::parse(read_file_bytes(manifest_path));
  EXPECT_EQ(migrated["version"], kQuaderProjectManifestVersion);
  EXPECT_TRUE(migrated.contains("textures"));
  EXPECT_EQ(project.manifest()->loaded_version(), kQuaderProjectManifestVersion);
}

TEST(ProjectSessionWorkspace,
     SceneAtProjectRootSavesAndClosesThroughThePublicLifecycle) {
  ScopedProjectSessionDirectory temporary("root-scene-save");
  ProjectSession project;
  QuaderEditorApi editor(project);
  ASSERT_TRUE(editor.project().create(temporary.make_empty_child("Project")).ok());
  const auto tab = project.active_scene_id();
  const auto path = project_path("Root Scene.qdr");
  ASSERT_TRUE(editor.scene_documents().save_as(tab, path).ok());
  const auto identity = project.find(tab)->scene_id;
  ASSERT_TRUE(editor.scene().create_entity({.name = "Authored Edit"}).ok());
  ASSERT_TRUE(project.preflight_save(tab).ok);
  ASSERT_TRUE(editor.scene_documents().save(tab).ok());
  EXPECT_EQ(project.find(tab)->scene_id, identity);
  EXPECT_FALSE(project.find(tab)->dirty());
  const auto read = QdrDocumentReader::read_project_document(project.root() / path.native_path());
  ASSERT_TRUE(read.ok) << read.message;
  EXPECT_EQ(SceneId::from_bytes(read.scene_uuid), identity);
  ASSERT_TRUE(editor.scene().create_entity({.name = "Save On Close"}).ok());
  int prompts = 0;
  editor.scene_documents().set_runtime_hooks({
      .confirm_dirty_scene = [&](const QuaderSceneDocumentInfo &, std::string_view) {
        ++prompts;
        return QuaderDirtySceneDecision::Save;
      },
  });
  ASSERT_TRUE(editor.scene_documents().close(tab).ok());
  EXPECT_EQ(prompts, 1);
  EXPECT_EQ(project.find(tab), nullptr);
}

TEST(ProjectSessionWorkspace,
     SaveAllPreflightsReservedDestinationsBeforeEarlierWritesOrUnsavedPrompts) {
  for (int route = 0; route < 3; ++route) {
    SCOPED_TRACE(route);
    ScopedProjectSessionDirectory temporary("save-all-scene-scope");
    ProjectSession project;
    QuaderEditorApi editor(project);
    ASSERT_TRUE(editor.project().create(temporary.make_empty_child("Project")).ok());
    const auto first_id = project.active_scene_id();
    const auto first_path = project_path("Scenes/First.qdr");
    ASSERT_TRUE(project.save_as(first_id, first_path).ok);
    const auto first_absolute = project.root() / first_path.native_path();
    const auto legacy_path = project_path("Legacy.qdr");
    std::filesystem::copy_file(first_absolute,
                               project.root() / legacy_path.native_path());
    ASSERT_TRUE(editor.scene().create_entity({.name = "First Unsaved Edit"}).ok());
    if (route != 0) {
      ASSERT_TRUE(editor.scene_documents().new_scene().ok());
      ASSERT_TRUE(editor.scene().create_entity({.name = "Unsaved Tab"}).ok());
    }
    const auto legacy = editor.scene_documents().open(legacy_path);
    ASSERT_TRUE(legacy.ok()) << legacy.error_message();
    ASSERT_TRUE(editor.scene().create_entity({.name = "Legacy Unsaved Edit"}).ok());
    // A stale resident path must still be rejected before another tab is saved.
    project.find(legacy.value())->path = project_path(".quader/Invalid.qdr");
    const auto first_bytes = read_file_bytes(first_absolute);
    const auto legacy_bytes = read_file_bytes(project.root() / legacy_path.native_path());
    const auto manifest_bytes = read_file_bytes(project.manifest_path());
    const auto bridge_path = project.root() / ".quader/bridge/scenes" /
        (std::string(project.find(first_id)->scene_id.str()) + ".json");
    const auto bridge_bytes = read_file_bytes(bridge_path);
    int prompts = 0;
    editor.scene_documents().set_runtime_hooks({
        .choose_save_scene_as_path = [&](SceneSessionId) {
          ++prompts;
          return QuaderSceneSavePathResult{
              .path = project.root() / "Scenes/PreviouslyUnsaved.qdr"};
        },
    });
    if (route == 0) {
      const auto rejected = project.save_all();
      EXPECT_FALSE(rejected.ok);
      EXPECT_FALSE(rejected.changed);
    } else {
      const auto rejected = route == 1
          ? editor.scene_documents().save_all()
          : editor.commands().execute(quader_command::kSceneDocumentSaveAll);
      EXPECT_FALSE(rejected.ok());
      EXPECT_FALSE(rejected.changed());
    }
    EXPECT_EQ(prompts, 0);
    EXPECT_TRUE(project.find(first_id)->dirty());
    EXPECT_TRUE(project.find(legacy.value())->dirty());
    EXPECT_EQ(read_file_bytes(first_absolute), first_bytes);
    EXPECT_EQ(read_file_bytes(project.root() / legacy_path.native_path()), legacy_bytes);
    EXPECT_EQ(read_file_bytes(project.manifest_path()), manifest_bytes);
    EXPECT_EQ(read_file_bytes(bridge_path), bridge_bytes);
    EXPECT_FALSE(std::filesystem::exists(project.root() / "Scenes/PreviouslyUnsaved.qdr"));
  }
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
     SaveDialogSetupFailurePreservesUnsavedSceneAndLivePreview) {
  ScopedProjectSessionDirectory temporary("scene-save-dialog-failure");
  ProjectSession project;
  QuaderEditorApi editor(project);
  ASSERT_TRUE(editor.project().create(temporary.make_empty_child("Project")).ok());
  ASSERT_TRUE(editor.modeling().create_curve({
      .points = {{.position = {-2.0f, 0.0f, 0.0f}},
                 {.position = {2.0f, 0.0f, 0.0f}}},
      .name = "Unsaved Curve",
  }).ok());
  OpenSceneSession *scene = project.active_scene();
  ASSERT_NE(scene, nullptr);
  ASSERT_TRUE(scene->dirty());
  ASSERT_TRUE(editor.tools().activate(kCurveToolId).ok());
  CurveToolService &curve = scene->session->curve();
  ASSERT_TRUE(curve.preview_state().document.has_value());
  ASSERT_TRUE(select_and_begin_curve_control_drag(
      curve, {.point = curve.preview_state().document->points()[0].id}));
  ASSERT_TRUE(curve.update_drag({-5.0f, 1.0f, 2.0f}).ok);
  const auto preview = *curve.preview_state().document;
  const auto identity = scene->scene_id;
  const auto stamp = scene->session->content_revision_stamp();
  const auto generation = project.activation_generation();
  const auto manifest = ProjectManifestStore::serialize(*project.manifest());
  const auto manifest_bytes = read_file_bytes(project.manifest_path());
  constexpr std::string_view kDialogError =
      "Could not create the scene Save As dialog.";
  int path_requests = 0;
  int close_prompts = 0;
  editor.scene_documents().set_runtime_hooks({
      .choose_save_scene_as_path = [&](SceneSessionId id) {
        EXPECT_EQ(id, scene->session_id);
        ++path_requests;
        return QuaderSceneSavePathResult{.error_message = std::string(kDialogError)};
      },
      .confirm_dirty_scene = [&](const QuaderSceneDocumentInfo &,
                                 std::string_view action_id) {
        EXPECT_EQ(action_id, "scene.close");
        ++close_prompts;
        return QuaderDirtySceneDecision::Save;
      },
  });
  for (int route = 0; route < 4; ++route) {
    SCOPED_TRACE(route);
    const auto rejected = route == 0
        ? editor.scene_documents().save_as(scene->session_id)
        : route == 1
            ? editor.scene_documents().save(scene->session_id)
            : route == 2
                ? editor.scene_documents().close(scene->session_id)
                : editor.scene_documents().save_all();
    EXPECT_FALSE(rejected.ok());
    EXPECT_FALSE(rejected.changed());
    EXPECT_EQ(rejected.error_message(), kDialogError);
    EXPECT_EQ(path_requests, route + 1);
    EXPECT_EQ(project.active_scene(), scene);
    EXPECT_EQ(project.open_scenes().size(), 1u);
    EXPECT_EQ(project.activation_generation(), generation);
    EXPECT_EQ(scene->scene_id, identity);
    EXPECT_FALSE(scene->path.has_value());
    EXPECT_TRUE(scene->dirty());
    EXPECT_EQ(scene->session->content_revision_stamp(), stamp);
    EXPECT_TRUE(curve.active());
    EXPECT_TRUE(curve.dragging());
    EXPECT_TRUE(scene->session->modeling().curve_document_preview_active());
    ASSERT_TRUE(curve.preview_state().document.has_value());
    EXPECT_TRUE(curve.preview_state().document->has_same_geometry(preview));
    EXPECT_EQ(ProjectManifestStore::serialize(*project.manifest()), manifest);
    EXPECT_EQ(read_file_bytes(project.manifest_path()), manifest_bytes);
    EXPECT_TRUE(std::filesystem::is_empty(project.root() / "Scenes"));
  }
  EXPECT_EQ(close_prompts, 1);
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
              -> QuaderSceneSavePathResult {
            ++save_path_requests;
            return {};
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
              -> QuaderSceneSavePathResult {
            ++save_path_requests;
            return {};
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
  const std::filesystem::path root_before = editor.project().root();
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
              -> QuaderSceneSavePathResult {
            ++save_path_requests;
            return {};
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
  EXPECT_EQ(editor.project().root(), root_before);
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
                                     .location = {7.0f, 2.0f, -5.0f},
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
  expect_vec3_eq(pasted_transform->location, source_transform.location);
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
     LinkedInstancePastePublicationFailureRestoresOuterSceneAndAllocation) {
  ScopedProjectSessionDirectory temporary("linked-instance-paste-failure");
  ProjectSession project;
  ProjectSession control_project;
  ASSERT_TRUE(project
                  .create(temporary.make_empty_child("Failure"))
                  .ok);
  ASSERT_TRUE(control_project
                  .create(temporary.make_empty_child("Control"))
                  .ok);
  QuaderEditorApi editor(project);
  QuaderEditorApi control_editor(control_project);

  const auto prepare = [](ProjectSession &target_project,
                          QuaderEditorApi &target_editor) {
    const QuaderApiResult<QuaderModelingObjectRef> member =
        target_editor.modeling().create_box(
            QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                         .max = {1.0f, 1.0f, 1.0f}},
            "Clipboard Member");
    EXPECT_TRUE(member.ok()) << member.error_message();
    EXPECT_TRUE(target_editor.modeling().create_instance().ok());
    EXPECT_TRUE(target_editor.commands()
                    .execute(quader_command::kCopySelection)
                    .ok());
    const QuaderApiResult<QuaderModelingObjectRef> projection_seed =
        target_editor.modeling().create_box(
            QuaderBounds{.min = {3.0f, -1.0f, -1.0f},
                         .max = {5.0f, 1.0f, 1.0f}},
            "Projection Seed");
    EXPECT_TRUE(projection_seed.ok()) << projection_seed.error_message();
    if (projection_seed.ok()) {
      EXPECT_TRUE(target_editor.selection()
                      .select_object(projection_seed.value())
                      .ok());
    }
    OpenSceneSession *active = target_project.active_scene();
    EXPECT_NE(active, nullptr);
    if (active != nullptr && active->session != nullptr) {
      active->session->history().clear();
    }
    return active;
  };
  OpenSceneSession *active = prepare(project, editor);
  OpenSceneSession *control = prepare(control_project, control_editor);
  ASSERT_NE(active, nullptr);
  ASSERT_NE(active->session, nullptr);
  ASSERT_NE(control, nullptr);
  ASSERT_NE(control->session, nullptr);

  SceneSession &session = *active->session;
  const std::vector<EntityId> entities_before = session.scene().entities();
  const std::vector<EntityId> selection_before =
      session.scene().selected_entities();
  const EntityId active_before = session.scene().primary_selection();
  const SceneScalarFingerprint scalars_before =
      scene_scalar_fingerprint(session.scene());
  const ModelingSelectionSummary modeling_before =
      session.modeling().selection_summary();
  const QuaderHistorySnapshot history_before = editor.query().history();
  ASSERT_EQ(selection_before.size(), 1u);
  ASSERT_EQ(modeling_before.selected_object_count, 1u);
  ASSERT_NE(modeling_before.active_object_id, -1);

  session.modeling().set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::AfterSdkMutation);
  const QuaderApiVoidResult failed =
      editor.commands().execute(quader_command::kPasteSelection);
  EXPECT_FALSE(failed.ok());
  EXPECT_FALSE(failed.changed());
  EXPECT_NE(failed.error_message().find("Injected failure"),
            std::string::npos);
  EXPECT_EQ(session.scene().entities(), entities_before);
  EXPECT_EQ(session.scene().selected_entities(), selection_before);
  EXPECT_EQ(session.scene().primary_selection(), active_before);
  EXPECT_EQ(scene_scalar_fingerprint(session.scene()), scalars_before);
  const ModelingSelectionSummary modeling_after =
      session.modeling().selection_summary();
  EXPECT_EQ(modeling_after.mode, modeling_before.mode);
  EXPECT_EQ(modeling_after.active_object_id, modeling_before.active_object_id);
  EXPECT_EQ(modeling_after.selected_object_count,
            modeling_before.selected_object_count);
  EXPECT_EQ(modeling_after.selection_revision,
            modeling_before.selection_revision);
  EXPECT_EQ(modeling_after.content_revision, modeling_before.content_revision);
  const QuaderHistorySnapshot history_after = editor.query().history();
  EXPECT_EQ(history_after.can_undo, history_before.can_undo);
  EXPECT_EQ(history_after.can_redo, history_before.can_redo);
  EXPECT_EQ(history_after.undo_label, history_before.undo_label);
  EXPECT_EQ(history_after.redo_label, history_before.redo_label);

  session.modeling().set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::None);
  const QuaderApiResult<QuaderEntityRef> created_after_failure =
      editor.scene().create_entity({.name = "Allocator Probe"});
  const QuaderApiResult<QuaderEntityRef> control_created =
      control_editor.scene().create_entity({.name = "Allocator Probe"});
  ASSERT_TRUE(created_after_failure.ok())
      << created_after_failure.error_message();
  ASSERT_TRUE(control_created.ok()) << control_created.error_message();
  EXPECT_EQ(created_after_failure.value().id, control_created.value().id);
  EXPECT_EQ(created_after_failure.value().editor_object_id,
            control_created.value().editor_object_id);
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
                                     .location = {-4.0f, 3.0f, 9.0f},
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
  source_member_transform.location.x += 12.0f;
  ASSERT_TRUE(source_scene->session->document_commands()
                  .set_transform(source_member, source_member_transform)
                  .ok);
  const TransformComponent *destination_member_after =
      destination_scene->session->scene().transform(destination_member);
  ASSERT_NE(destination_member_after, nullptr);
  expect_vec3_eq(destination_member_after->location,
                 destination_member_transform.location);
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
     ProjectClipboardRemapsMissingLayerIdByNameAcrossNestedInstanceContent) {
  ScopedProjectSessionDirectory temporary("cross-scene-instance-layer-name");
  ProjectSession project;
  const ProjectSessionResult created = project.create(
      temporary.make_empty_child("CrossSceneInstanceLayerName"));
  ASSERT_TRUE(created.ok) << created.message;
  ASSERT_TRUE(created.scene_session_id.has_value());
  const SceneSessionId source_scene_id = *created.scene_session_id;

  QuaderEditorApi editor(project);
  const QuaderApiResult<QuaderModelingObjectRef> first =
      editor.modeling().create_box(
          QuaderBounds{.min = {-3.0f, -1.0f, -1.0f},
                       .max = {-1.0f, 1.0f, 1.0f}},
          "Nested Clipboard Left");
  const QuaderApiResult<QuaderModelingObjectRef> second =
      editor.modeling().create_box(
          QuaderBounds{.min = {1.0f, -1.0f, -1.0f},
                       .max = {3.0f, 1.0f, 1.0f}},
          "Nested Clipboard Right");
  ASSERT_TRUE(first.ok()) << first.error_message();
  ASSERT_TRUE(second.ok()) << second.error_message();
  ASSERT_TRUE(editor.selection()
                  .select_objects({first.value().entity, second.value().entity},
                                  first.value().entity)
                  .ok());
  const QuaderApiResult<QuaderEntityRef> group =
      editor.scene().group_selection("Nested Clipboard Group");
  ASSERT_TRUE(group.ok()) << group.error_message();
  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Object).ok());
  ASSERT_TRUE(editor.selection().select_object(group.value().id).ok());
  const QuaderApiResult<QuaderInstanceCreateResult> instance =
      editor.modeling().create_instance_from_selection();
  ASSERT_TRUE(instance.ok()) << instance.error_message();

  constexpr QVec3 kSourceColor = {0.8f, 0.2f, 0.1f};
  const QuaderApiResult<QuaderLayerSnapshot> source_layer =
      editor.layers().create("Shared Clipboard Layer", kSourceColor);
  ASSERT_TRUE(source_layer.ok()) << source_layer.error_message();
  ASSERT_TRUE(editor.layers().add_selection(source_layer.value().id).ok());
  const EntityId source_occurrence = instance.value().occurrence.root.id;
  OpenSceneSession *source_scene = project.find(source_scene_id);
  ASSERT_NE(source_scene, nullptr);
  ASSERT_EQ(source_scene->session->scene().entity_layer(source_occurrence),
            source_layer.value().id);
  ASSERT_TRUE(editor.modeling().copy_selection().ok());

  const QuaderApiResult<SceneSessionId> destination_created =
      editor.scene_documents().new_scene();
  ASSERT_TRUE(destination_created.ok()) << destination_created.error_message();
  OpenSceneSession *destination_scene =
      project.find(destination_created.value());
  ASSERT_NE(destination_scene, nullptr);
  constexpr SceneLayerId kDestinationLayerId = 41u;
  constexpr QVec3 kDestinationColor = {0.1f, 0.4f, 0.9f};
  ASSERT_EQ(destination_scene->session->scene().create_layer_with_id(
                kDestinationLayerId, "Shared Clipboard Layer", true,
                kDestinationColor, false),
            kDestinationLayerId);
  ASSERT_NE(kDestinationLayerId, source_layer.value().id);
  EXPECT_EQ(destination_scene->session->scene().layer(source_layer.value().id),
            nullptr);

  const QuaderApiVoidResult pasted = editor.modeling().paste_selection();
  ASSERT_TRUE(pasted.ok()) << pasted.error_message();
  ASSERT_TRUE(pasted.changed());
  const std::vector<EntityId> pasted_selection =
      destination_scene->session->scene().selected_entities();
  ASSERT_EQ(pasted_selection.size(), 1u);
  const EntityId destination_occurrence = pasted_selection.front();
  EXPECT_EQ(destination_scene->session->scene().entity_layer(
                destination_occurrence),
            kDestinationLayerId);
  const SceneLayerRecord *retained_layer =
      destination_scene->session->scene().layer(kDestinationLayerId);
  ASSERT_NE(retained_layer, nullptr);
  expect_vec3_eq(retained_layer->wireframe_color, kDestinationColor);
  EXPECT_EQ(destination_scene->session->scene().layers().size(), 1u);

  const InstanceOccurrenceComponent *destination_component =
      destination_scene->session->scene().instance_occurrence(
          destination_occurrence);
  ASSERT_NE(destination_component, nullptr);
  const EntityId destination_definition = destination_component->definition;
  const std::vector<EntityId> definition_children =
      destination_scene->session->scene().children(destination_definition);
  ASSERT_EQ(definition_children.size(), 1u);
  const EntityId destination_group = definition_children.front();
  const std::vector<EntityId> nested_members =
      destination_scene->session->scene().children(destination_group);
  ASSERT_EQ(nested_members.size(), 2u);
  EXPECT_EQ(destination_scene->session->scene().entity_layer(
                destination_definition),
            kInvalidSceneLayerId);
  EXPECT_EQ(destination_scene->session->scene().entity_layer(destination_group),
            kInvalidSceneLayerId);
  for (const EntityId member : nested_members) {
    EXPECT_EQ(destination_scene->session->scene().entity_layer(member),
              kInvalidSceneLayerId);
  }
}

TEST(ProjectSessionWorkspace,
     ProjectClipboardNeverAliasesCollidingNumericLayerIdentity) {
  ScopedProjectSessionDirectory temporary("cross-scene-instance-layer-collision");
  ProjectSession project;
  const ProjectSessionResult created = project.create(
      temporary.make_empty_child("CrossSceneInstanceLayerCollision"));
  ASSERT_TRUE(created.ok) << created.message;
  ASSERT_TRUE(created.scene_session_id.has_value());

  QuaderEditorApi editor(project);
  const QuaderApiResult<QuaderModelingObjectRef> mesh =
      editor.modeling().create_box(
          QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                       .max = {1.0f, 1.0f, 1.0f}},
          "Layer Collision Member");
  ASSERT_TRUE(mesh.ok()) << mesh.error_message();
  const QuaderApiResult<QuaderInstanceCreateResult> instance =
      editor.modeling().create_instance_from_selection();
  ASSERT_TRUE(instance.ok()) << instance.error_message();
  constexpr QVec3 kSourceColor = {0.7f, 0.3f, 0.2f};
  const QuaderApiResult<QuaderLayerSnapshot> source_layer =
      editor.layers().create("Imported Clipboard Layer", kSourceColor);
  ASSERT_TRUE(source_layer.ok()) << source_layer.error_message();
  ASSERT_TRUE(editor.layers().add_selection(source_layer.value().id).ok());
  ASSERT_TRUE(editor.modeling().copy_selection().ok());

  const QuaderApiResult<SceneSessionId> destination_created =
      editor.scene_documents().new_scene();
  ASSERT_TRUE(destination_created.ok()) << destination_created.error_message();
  OpenSceneSession *destination_scene =
      project.find(destination_created.value());
  ASSERT_NE(destination_scene, nullptr);
  constexpr QVec3 kUnrelatedColor = {0.1f, 0.8f, 0.4f};
  ASSERT_EQ(destination_scene->session->scene().create_layer_with_id(
                source_layer.value().id, "Unrelated Destination Layer", true,
                kUnrelatedColor, false),
            source_layer.value().id);

  const QuaderApiVoidResult pasted = editor.modeling().paste_selection();
  ASSERT_TRUE(pasted.ok()) << pasted.error_message();
  ASSERT_TRUE(pasted.changed());
  const std::vector<EntityId> selected =
      destination_scene->session->scene().selected_entities();
  ASSERT_EQ(selected.size(), 1u);
  const EntityId destination_occurrence = selected.front();
  const InstanceOccurrenceComponent *destination_component =
      destination_scene->session->scene().instance_occurrence(
          destination_occurrence);
  ASSERT_NE(destination_component, nullptr);
  const EntityId destination_definition = destination_component->definition;

  const std::vector<SceneLayerRecord> destination_layers =
      destination_scene->session->scene().layers();
  ASSERT_EQ(destination_layers.size(), 2u);
  const auto imported = std::find_if(
      destination_layers.begin(), destination_layers.end(),
      [](const SceneLayerRecord &record) {
        return record.name == "Imported Clipboard Layer";
      });
  ASSERT_NE(imported, destination_layers.end());
  EXPECT_NE(imported->id, source_layer.value().id);
  expect_vec3_eq(imported->wireframe_color, kSourceColor);
  EXPECT_EQ(destination_scene->session->scene().entity_layer(
                destination_occurrence),
            imported->id);
  EXPECT_TRUE(destination_scene->session->scene()
                  .layer_members(source_layer.value().id)
                  .empty());
  const SceneLayerRecord *unrelated =
      destination_scene->session->scene().layer(source_layer.value().id);
  ASSERT_NE(unrelated, nullptr);
  EXPECT_EQ(unrelated->name, "Unrelated Destination Layer");
  expect_vec3_eq(unrelated->wireframe_color, kUnrelatedColor);

  const SceneLayerId imported_id = imported->id;
  ASSERT_TRUE(editor.history().undo().ok());
  EXPECT_FALSE(destination_scene->session->scene().exists(
      destination_occurrence));
  EXPECT_FALSE(destination_scene->session->scene().exists(
      destination_definition));
  EXPECT_EQ(destination_scene->session->scene().layer(imported_id), nullptr);
  EXPECT_NE(destination_scene->session->scene().layer(source_layer.value().id),
            nullptr);

  ASSERT_TRUE(editor.history().redo().ok());
  EXPECT_TRUE(destination_scene->session->scene().exists(
      destination_occurrence));
  EXPECT_TRUE(destination_scene->session->scene().exists(
      destination_definition));
  ASSERT_NE(destination_scene->session->scene().layer(imported_id), nullptr);
  EXPECT_EQ(destination_scene->session->scene().entity_layer(
                destination_occurrence),
            imported_id);
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

TEST(ProjectSessionWorkspace,
     SuccessfulSavesPublishCommittedFingerprintsAndSaveAsKeepsOldLink) {
  ScopedProjectSessionDirectory temporary("bridge-scene-saves");
  ProjectSession project;
  const auto created = project.create(temporary.make_empty_child("Project"));
  ASSERT_TRUE(created.ok) << created.message;
  ASSERT_TRUE(created.scene_session_id.has_value());
  const SceneSessionId tab = *created.scene_session_id;
  const ProjectRelativePath first_path = project_path("Scenes/First.qdr");
  ASSERT_TRUE(project.save_as(tab, first_path).ok);
  OpenSceneSession *scene = project.find(tab);
  ASSERT_NE(scene, nullptr);
  const SceneId first_id = scene->scene_id;
  const auto record_path = project.root() / ".quader/bridge/scenes" /
                           (std::string(first_id.str()) + ".json");
  ASSERT_TRUE(std::filesystem::is_regular_file(record_path));
  const auto first_record = read_sync_record(record_path);
  expect_sync_record(first_record, project.root() / first_path.native_path(),
                     project.manifest()->id().str(), first_id.str(),
                     first_path.portable_string());

  const auto first_bytes = read_file_bytes(project.root() / first_path.native_path());
  const auto decoded = QdrDocumentReader::read_project_document(
      project.root() / first_path.native_path());
  ASSERT_TRUE(decoded.ok) << decoded.message;
  EXPECT_EQ(decoded.source_byte_count, first_bytes.size());
  EXPECT_EQ(decoded.source_content_hash,
            std::stoull(first_record.at("content_hash").get<std::string>(),
                         nullptr, 16));
  ASSERT_TRUE(project.save(tab).ok);
  EXPECT_EQ(read_file_bytes(project.root() / first_path.native_path()), first_bytes)
      << "Completion tokens must not alter the authored QDR serialization.";
  const auto unchanged_record = read_sync_record(record_path);
  EXPECT_NE(unchanged_record.at("save_token"), first_record.at("save_token"));
  EXPECT_EQ(unchanged_record.at("content_hash"), first_record.at("content_hash"));
  EXPECT_EQ(unchanged_record.at("byte_count"), first_record.at("byte_count"));

  ASSERT_TRUE(scene->session->document_commands().create_entity("Saved Edit").ok);
  const auto saved = project.save(tab);
  ASSERT_TRUE(saved.ok) << saved.message;
  const auto second_record = read_sync_record(record_path);
  expect_sync_record(second_record, project.root() / first_path.native_path(),
                     project.manifest()->id().str(), first_id.str(),
                     first_path.portable_string());
  EXPECT_NE(second_record.at("save_token"), first_record.at("save_token"));
  EXPECT_NE(second_record.at("content_hash"), first_record.at("content_hash"));
  const auto preserved_record = read_file_bytes(record_path);

  const ProjectRelativePath second_path = project_path("Scenes/Second.qdr");
  ASSERT_TRUE(project.save_as(tab, second_path).ok);
  EXPECT_NE(scene->scene_id, first_id);
  EXPECT_EQ(read_file_bytes(record_path), preserved_record);
  const auto second_record_path = project.root() / ".quader/bridge/scenes" /
      (std::string(scene->scene_id.str()) + ".json");
  ASSERT_TRUE(std::filesystem::is_regular_file(second_record_path));
  expect_sync_record(read_sync_record(second_record_path),
                     project.root() / second_path.native_path(),
                     project.manifest()->id().str(), scene->scene_id.str(),
                     second_path.portable_string());
}

TEST(ProjectSessionWorkspace,
     SceneSaveEstablishesAndRepairsReferencedMaterialCompletionRecords) {
  ScopedProjectSessionDirectory temporary("bridge-material-baseline");
  ProjectSession project;
  project.set_resource_root(std::filesystem::path(QUADER_SOURCE_DIR) /
                            "resources/modeling_assets");
  QuaderEditorApi editor(project);
  ASSERT_TRUE(editor.project().create(temporary.make_empty_child("Project")).ok());
  ASSERT_TRUE(editor.modeling()
                  .create_box(QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                                          .max = {1.0f, 1.0f, 1.0f}},
                              "Material User")
                  .ok());
  const auto scene_path = project_path("Scenes/MaterialUser.qdr");
  const auto tab = project.active_scene_id();
  ASSERT_TRUE(project.save_as(tab, scene_path).ok);
  const auto material_record_path = project.root() / ".quader/bridge/materials" /
      (std::string(kDefaultProjectMaterialId) + ".json");
  ASSERT_TRUE(std::filesystem::is_regular_file(material_record_path));
  const auto material_record = read_sync_record(material_record_path);
  expect_sync_record(material_record,
                     project.root() / kDefaultProjectMaterialPath,
                     project.manifest()->id().str(), kDefaultProjectMaterialId,
                     kDefaultProjectMaterialPath);

  ASSERT_TRUE(project.active_scene()->session->document_commands()
                  .create_entity("Unrelated Scene Edit").ok);
  ASSERT_TRUE(project.save(tab).ok);
  EXPECT_EQ(read_sync_record(material_record_path), material_record)
      << "A matching saved-material baseline must retain its token.";
  {
    std::ofstream corrupt(material_record_path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(corrupt.is_open());
    corrupt << "incomplete record";
  }
  ASSERT_TRUE(project.active_scene()->session->document_commands()
                  .create_entity("Repair Trigger").ok);
  ASSERT_TRUE(project.save(tab).ok);
  const auto repaired_record = read_sync_record(material_record_path);
  expect_sync_record(repaired_record,
                     project.root() / kDefaultProjectMaterialPath,
                     project.manifest()->id().str(), kDefaultProjectMaterialId,
                     kDefaultProjectMaterialPath);
  EXPECT_NE(repaired_record.at("save_token"), material_record.at("save_token"));
}

TEST(ProjectSessionWorkspace,
     FailedSceneSaveAndRecoveryAutosaveKeepLastCompletionRecord) {
  ScopedProjectSessionDirectory temporary("bridge-failure-autosave");
  ProjectSession project;
  const auto created = project.create(temporary.make_empty_child("Project"));
  ASSERT_TRUE(created.ok) << created.message;
  ASSERT_TRUE(created.scene_session_id.has_value());
  const auto tab = *created.scene_session_id;
  const auto path = project_path("Scenes/Authored.qdr");
  ASSERT_TRUE(project.save_as(tab, path).ok);
  OpenSceneSession *scene = project.find(tab);
  ASSERT_NE(scene, nullptr);
  const auto record_path = project.root() / ".quader/bridge/scenes" /
                           (std::string(scene->scene_id.str()) + ".json");
  const auto before = read_file_bytes(record_path);
  ASSERT_TRUE(scene->session->document_commands().create_entity("Unsaved Edit").ok);
  {
    ScopedManifestWriteFailure blocked(project.root() / path.native_path());
    ASSERT_TRUE(blocked.ready()) << blocked.message();
    const auto failed = project.save(tab);
    EXPECT_FALSE(failed.ok);
    EXPECT_TRUE(scene->dirty());
    EXPECT_EQ(read_file_bytes(record_path), before);
  }
  const SessionPreferences settings;
  const std::chrono::steady_clock::time_point epoch{};
  EXPECT_EQ(project.autosave().poll(project, settings, epoch).written, 0u);
  const auto recovery = project.autosave().poll(
      project, settings, epoch + std::chrono::minutes(2));
  ASSERT_TRUE(recovery.ok);
  ASSERT_EQ(recovery.written, 1u);
  EXPECT_TRUE(scene->dirty());
  EXPECT_EQ(read_file_bytes(record_path), before);
}

TEST(ProjectSessionWorkspace,
     NotificationFailureDoesNotFailSaveAndSaveAllPreservesEachWarning) {
  ScopedProjectSessionDirectory temporary("bridge-nonfatal");
  ProjectSession project;
  const auto created = project.create(temporary.make_empty_child("Project"));
  ASSERT_TRUE(created.ok) << created.message;
  ASSERT_TRUE(created.scene_session_id.has_value());
  const auto first = *created.scene_session_id;
  const auto bridge_root = project.root() / ".quader/bridge";
  std::filesystem::create_directories(bridge_root.parent_path());
  {
    std::ofstream blocker(bridge_root, std::ios::binary);
    ASSERT_TRUE(blocker.is_open());
    blocker << "Keep this unrelated file";
  }
  const auto saved = project.save_as(first, project_path("Scenes/First.qdr"));
  ASSERT_TRUE(saved.ok) << saved.message;
  EXPECT_FALSE(project.find(first)->dirty());
  ASSERT_FALSE(saved.warnings.empty());
  EXPECT_TRUE(std::ranges::any_of(saved.warnings, [](const std::string &warning) {
    return warning.find("Godot synchronization") != std::string::npos;
  }));
  const auto second_created = project.new_scene();
  ASSERT_TRUE(second_created.ok) << second_created.message;
  ASSERT_TRUE(second_created.scene_session_id.has_value());
  const auto second = *second_created.scene_session_id;
  ASSERT_TRUE(project.save_as(second, project_path("Scenes/Second.qdr")).ok);
  ASSERT_TRUE(project.find(first)->session->document_commands()
                  .create_entity("First Edit").ok);
  ASSERT_TRUE(project.find(second)->session->document_commands()
                  .create_entity("Second Edit").ok);
  const auto all = project.save_all();
  ASSERT_TRUE(all.ok) << all.message;
  EXPECT_FALSE(project.has_dirty_scenes());
  EXPECT_EQ(std::ranges::count_if(all.warnings, [](const std::string &warning) {
    return warning.find("Godot synchronization") != std::string::npos;
  }), 2);
  const auto blocker_bytes = read_file_bytes(bridge_root);
  EXPECT_EQ(std::string(blocker_bytes.begin(), blocker_bytes.end()),
            "Keep this unrelated file");
}

TEST(ProjectSessionWorkspace,
     SceneReadSharesDeletionAndPreservesDecodedSnapshotAcrossAtomicSave) {
#if !defined(_WIN32)
  GTEST_SKIP() << "Reciprocal file sharing is a Windows-specific contract.";
#else
  ScopedProjectSessionDirectory temporary("bridge-scene-shared-delete");
  ProjectSession project;
  const auto created = project.create(temporary.make_empty_child("Project"));
  ASSERT_TRUE(created.ok) << created.message;
  ASSERT_TRUE(created.scene_session_id.has_value());
  const auto tab = *created.scene_session_id;
  auto *scene = project.find(tab);
  ASSERT_NE(scene, nullptr);
  const auto original_entity =
      scene->session->document_commands().create_entity("Original Saved Entity");
  ASSERT_TRUE(original_entity.ok) << original_entity.message;
  const auto relative = project_path("Scenes/SharedRead.qdr");
  ASSERT_TRUE(project.save_as(tab, relative).ok);
  const auto absolute = project.root() / relative.native_path();
  const auto record_path = project.root() / ".quader/bridge/scenes" /
      (std::string(scene->scene_id.str()) + ".json");
  const auto original_record = read_sync_record(record_path);

  // No sleeps or competing thread are needed: Windows checks sharing in both
  // directions, so a reader that omits FILE_SHARE_DELETE fails this open.
  ScopedSharedDeleteAccess held(absolute);
  ASSERT_TRUE(held.ready()) << ::GetLastError();
  const auto original = QdrDocumentReader::read_project_document(absolute);
  ASSERT_TRUE(original.ok) << original.message;
  EXPECT_EQ(original.source_byte_count,
            std::stoull(original_record.at("byte_count").get<std::string>()));
  EXPECT_EQ(original.source_content_hash,
            std::stoull(original_record.at("content_hash").get<std::string>(),
                         nullptr, 16));
  ASSERT_NE(original.document.scene().name(original_entity.entity), nullptr);
  EXPECT_EQ(original.document.scene().name(original_entity.entity)->name,
            "Original Saved Entity");

  const auto added =
      scene->session->document_commands().create_entity("Saved During Read");
  ASSERT_TRUE(added.ok) << added.message;
  const auto saved = project.save(tab);
  ASSERT_TRUE(saved.ok) << saved.message;
  const auto replacement = QdrDocumentReader::read_project_document(absolute);
  ASSERT_TRUE(replacement.ok) << replacement.message;
  const auto replacement_record = read_sync_record(record_path);
  expect_sync_record(replacement_record, absolute, project.manifest()->id().str(),
                     scene->scene_id.str(), relative.portable_string());
  EXPECT_EQ(replacement.source_byte_count,
            std::stoull(replacement_record.at("byte_count").get<std::string>()));
  EXPECT_EQ(replacement.source_content_hash,
            std::stoull(replacement_record.at("content_hash").get<std::string>(),
                         nullptr, 16));
  EXPECT_NE(replacement.source_content_hash, original.source_content_hash);
  ASSERT_NE(replacement.document.scene().name(added.entity), nullptr);
  EXPECT_EQ(replacement.document.scene().name(added.entity)->name,
            "Saved During Read");
  EXPECT_EQ(original.document.scene().name(added.entity), nullptr);
  EXPECT_EQ(original.document.scene().name(original_entity.entity)->name,
            "Original Saved Entity");
  EXPECT_EQ(original.source_content_hash,
            std::stoull(original_record.at("content_hash").get<std::string>(),
                         nullptr, 16));
#endif
}

TEST(ProjectSessionWorkspace,
     MaterialReadSharesDeletionAndPreservesDecodedSnapshotAcrossAtomicSave) {
#if !defined(_WIN32)
  GTEST_SKIP() << "Reciprocal file sharing is a Windows-specific contract.";
#else
  ScopedProjectSessionDirectory temporary("bridge-material-shared-delete");
  ProjectSession project;
  project.set_resource_root(std::filesystem::path(QUADER_SOURCE_DIR) /
                            "resources/modeling_assets");
  QuaderEditorApi editor(project);
  ASSERT_TRUE(editor.project().create(temporary.make_empty_child("Project")).ok());
  const auto default_id = ProjectAssetId::parse(kDefaultProjectMaterialId);
  ASSERT_TRUE(default_id.has_value());
  auto &documents = editor.standard_material_documents();
  ASSERT_TRUE(documents.open(*default_id).ok());
  ASSERT_TRUE(documents.set_name("Original Saved Material").ok());
  ASSERT_TRUE(documents.save().ok());
  const auto relative = project_path(kDefaultProjectMaterialPath);
  const auto absolute = project.root() / relative.native_path();
  const auto record_path = project.root() / ".quader/bridge/materials" /
      (std::string(kDefaultProjectMaterialId) + ".json");
  const auto original_record = read_sync_record(record_path);
  const auto &file_system = project.workspace()->file_system;

  ScopedSharedDeleteAccess held(absolute);
  ASSERT_TRUE(held.ready()) << ::GetLastError();
  const auto original = QmatCodec::read(file_system, relative);
  ASSERT_TRUE(original.ok()) << original.error().message;
  EXPECT_EQ(original.value().material.name, "Original Saved Material");
  EXPECT_EQ(original.value().content_revision,
            std::stoull(original_record.at("content_hash").get<std::string>(),
                         nullptr, 16));

  ASSERT_TRUE(documents.set_name("Material Saved During Read").ok());
  const auto saved = documents.save();
  ASSERT_TRUE(saved.ok()) << saved.error_message();
  const auto replacement = QmatCodec::read(file_system, relative);
  ASSERT_TRUE(replacement.ok()) << replacement.error().message;
  const auto replacement_record = read_sync_record(record_path);
  expect_sync_record(replacement_record, absolute, project.manifest()->id().str(),
                     kDefaultProjectMaterialId, relative.portable_string());
  EXPECT_EQ(replacement.value().file_size,
            std::stoull(replacement_record.at("byte_count").get<std::string>()));
  EXPECT_EQ(replacement.value().content_revision,
            std::stoull(replacement_record.at("content_hash").get<std::string>(),
                         nullptr, 16));
  EXPECT_NE(replacement.value().content_revision, original.value().content_revision);
  EXPECT_EQ(replacement.value().material.name, "Material Saved During Read");
  EXPECT_EQ(original.value().material.name, "Original Saved Material");
  EXPECT_EQ(original.value().content_revision,
            std::stoull(original_record.at("content_hash").get<std::string>(),
                         nullptr, 16));
#endif
}

TEST(ProjectSessionWorkspace,
     ManifestReadSharesDeletionAndKeepsSnapshotAcrossWorkspaceChanges) {
#if !defined(_WIN32)
  GTEST_SKIP() << "Reciprocal file sharing is a Windows-specific contract.";
#else
  ScopedProjectSessionDirectory temporary("bridge-manifest-shared-delete");
  ProjectSession project;
  const auto created = project.create(temporary.make_empty_child("Project"));
  ASSERT_TRUE(created.ok) << created.message;
  ASSERT_TRUE(created.scene_session_id.has_value());
  const auto first_path = project_path("Scenes/First.qdr");
  ASSERT_TRUE(project.save_as(*created.scene_session_id, first_path).ok);

  ScopedSharedDeleteAccess held(project.manifest_path());
  ASSERT_TRUE(held.ready()) << ::GetLastError();
  const auto original = ProjectManifestStore::open(project.manifest_path());
  ASSERT_TRUE(original.ok()) << original.error().message;
  ASSERT_EQ(original.value().manifest.open_scene_paths().size(), 1u);
  EXPECT_EQ(original.value().manifest.open_scene_paths().front(), first_path);
  EXPECT_EQ(original.value().manifest.active_scene_path(), first_path);
  ASSERT_EQ(original.value().manifest.scenes().size(), 1u);
  const auto first_catalog_entry = original.value().manifest.scenes().front();

  const auto added = project.new_scene();
  ASSERT_TRUE(added.ok) << added.message;
  ASSERT_TRUE(added.scene_session_id.has_value());
  const auto after_new = ProjectManifestStore::open(project.manifest_path());
  ASSERT_TRUE(after_new.ok()) << after_new.error().message;
  ASSERT_EQ(after_new.value().manifest.open_scene_paths().size(), 1u);
  EXPECT_EQ(after_new.value().manifest.open_scene_paths().front(), first_path);
  EXPECT_FALSE(after_new.value().manifest.active_scene_path().has_value());
  ASSERT_EQ(after_new.value().manifest.scenes().size(), 1u);
  EXPECT_EQ(after_new.value().manifest.scenes().front(), first_catalog_entry);

  const auto second_path = project_path("Scenes/Second.qdr");
  const auto saved = project.save_as(*added.scene_session_id, second_path);
  ASSERT_TRUE(saved.ok) << saved.message;
  const auto replacement = ProjectManifestStore::open(project.manifest_path());
  ASSERT_TRUE(replacement.ok()) << replacement.error().message;
  ASSERT_EQ(replacement.value().manifest.open_scene_paths().size(), 2u);
  EXPECT_EQ(replacement.value().manifest.open_scene_paths()[0], first_path);
  EXPECT_EQ(replacement.value().manifest.open_scene_paths()[1], second_path);
  EXPECT_EQ(replacement.value().manifest.active_scene_path(), second_path);
  ASSERT_EQ(replacement.value().manifest.scenes().size(), 2u);
  const auto *second_entry = replacement.value().manifest.find_scene(second_path);
  ASSERT_NE(second_entry, nullptr);
  EXPECT_EQ(second_entry->id, project.find(*added.scene_session_id)->scene_id);
  ASSERT_EQ(original.value().manifest.open_scene_paths().size(), 1u);
  EXPECT_EQ(original.value().manifest.open_scene_paths().front(), first_path);
  EXPECT_EQ(original.value().manifest.active_scene_path(), first_path);
  ASSERT_EQ(original.value().manifest.scenes().size(), 1u);
  EXPECT_EQ(original.value().manifest.scenes().front(), first_catalog_entry);
  EXPECT_EQ(original.value().manifest.find_scene(second_path), nullptr);
#endif
}

TEST(ProjectSessionWorkspace,
     FullOpeningPublicProjectAndSavingWorkspacePreservesItsRelease) {
  ASSERT_EQ(current_product_release(), kInternalFullRelease);
  ScopedProjectSessionDirectory temporary("public-release-bookkeeping");
  auto storage = ProjectManifestStore::create_in_empty_directory(
      temporary.make_empty_child("PublicProject"));
  ASSERT_TRUE(storage.ok()) << storage.error().message;
  auto public_manifest = ProjectManifest::hydrate(
      storage.value().manifest.id(), storage.value().manifest.name(), {}, {},
      std::nullopt, {}, kPublicRelease01);
  ASSERT_TRUE(public_manifest.ok());
  storage.value().manifest = public_manifest.take_value();
  ASSERT_TRUE(ProjectManifestStore::save_atomic(storage.value()).ok());

  ProjectSession project;
  const auto opened = project.open(storage.value().manifest_path);
  ASSERT_TRUE(opened.ok) << opened.message;
  ASSERT_NE(project.manifest(), nullptr);
  EXPECT_EQ(project.manifest()->minimum_product_release(), kPublicRelease01);
  EXPECT_NE(project.material_catalog().find(
                *ProjectAssetId::parse(kDefaultProjectMaterialId)),
            nullptr);

  QuaderEditorApi editor(project);
  const auto added = editor.scene_documents().new_scene();
  ASSERT_TRUE(added.ok()) << added.error_message();
  ASSERT_TRUE(editor.scene_documents().close(added.value()).ok());
  EXPECT_EQ(project.manifest()->minimum_product_release(), kPublicRelease01);
  const auto persisted = ProjectManifestStore::open(project.manifest_path());
  ASSERT_TRUE(persisted.ok()) << persisted.error().message;
  EXPECT_EQ(persisted.value().manifest.minimum_product_release(),
            kPublicRelease01);
  ASSERT_TRUE(project.close().ok);
  const auto after_close =
      ProjectManifestStore::open(storage.value().manifest_path);
  ASSERT_TRUE(after_close.ok()) << after_close.error().message;
  EXPECT_EQ(after_close.value().manifest.minimum_product_release(),
            kPublicRelease01);
}

TEST(ProjectSessionWorkspace,
     FullAuthoringSavePromotesPublicProjectOnlyAfterSuccessfulCommit) {
  ASSERT_EQ(current_product_release(), kInternalFullRelease);
  ScopedProjectSessionDirectory temporary("public-release-authoring");
  auto storage = ProjectManifestStore::create_in_empty_directory(
      temporary.make_empty_child("PublicProject"));
  ASSERT_TRUE(storage.ok()) << storage.error().message;
  auto public_manifest = ProjectManifest::hydrate(
      storage.value().manifest.id(), storage.value().manifest.name(), {}, {},
      std::nullopt, {}, kPublicRelease01);
  ASSERT_TRUE(public_manifest.ok());
  storage.value().manifest = public_manifest.take_value();
  ASSERT_TRUE(ProjectManifestStore::save_atomic(storage.value()).ok());

  ProjectSession project;
  ASSERT_TRUE(project.open(storage.value().manifest_path).ok);
  QuaderEditorApi editor(project);
  const auto added = editor.scene_documents().new_scene();
  ASSERT_TRUE(added.ok()) << added.error_message();
  ASSERT_TRUE(editor.modeling().create_box(
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      "Authored in Full").ok());
  EXPECT_EQ(project.manifest()->minimum_product_release(), kPublicRelease01);
  const auto scene_path = project_path("Scenes/Authored.qdr");
  {
    ScopedManifestWriteFailure blocked(project.manifest_path());
    ASSERT_TRUE(blocked.ready()) << blocked.message();
    EXPECT_FALSE(editor.scene_documents().save_as(added.value(), scene_path).ok());
    EXPECT_EQ(project.manifest()->minimum_product_release(), kPublicRelease01);
    EXPECT_FALSE(std::filesystem::exists(
        project.root() / scene_path.native_path()));
  }

  const auto saved = editor.scene_documents().save_as(added.value(), scene_path);
  ASSERT_TRUE(saved.ok()) << saved.error_message();
  EXPECT_EQ(project.manifest()->minimum_product_release(), kInternalFullRelease);
  const auto persisted = ProjectManifestStore::open(project.manifest_path());
  ASSERT_TRUE(persisted.ok()) << persisted.error().message;
  EXPECT_EQ(persisted.value().manifest.minimum_product_release(),
            kInternalFullRelease);
  const auto scene = QdrDocumentReader::read_project_document(
      project.root() / scene_path.native_path());
  ASSERT_TRUE(scene.ok) << scene.message;
  EXPECT_EQ(scene.minimum_product_release, kInternalFullRelease);
}

TEST(ProjectSessionWorkspace,
     FullSaveOfLoadedPublicScenePreservesOriginalOnManifestFailure) {
  ScopedProjectSessionDirectory temporary("public-scene-save");
  auto storage = ProjectManifestStore::create_in_empty_directory(
      temporary.make_empty_child("PublicProject"));
  ASSERT_TRUE(storage.ok()) << storage.error().message;
  const auto scene_path = project_path("Scenes/Public.qdr");
  const auto scene_id = SceneId::generate();
  auto public_manifest = ProjectManifest::hydrate(
      storage.value().manifest.id(), storage.value().manifest.name(),
      {{.id = scene_id, .path = scene_path}}, {scene_path}, scene_path, {},
      kPublicRelease01);
  ASSERT_TRUE(public_manifest.ok());
  storage.value().manifest = public_manifest.take_value();
  auto extracted = qdr_project_write_snapshot_from_document(SceneContentDocument{});
  ASSERT_TRUE(extracted.ok()) << extracted.error().message;
  extracted.value().minimum_product_release = kPublicRelease01;
  const auto absolute = storage.value().file_system.root() / scene_path.native_path();
  ASSERT_TRUE(QdrDocumentWriter::write_project_document_atomic(
      absolute, extracted.value(), scene_id.bytes()).ok);
  ASSERT_TRUE(ProjectManifestStore::save_atomic(storage.value()).ok());
  const auto original_bytes = read_file_bytes(absolute);

  ProjectSession project;
  ASSERT_TRUE(project.open(storage.value().manifest_path).ok);
  ASSERT_NE(project.active_scene(), nullptr);
  EXPECT_EQ(project.active_scene()->session->minimum_product_release(), kPublicRelease01);
  QuaderEditorApi editor(project);
  ASSERT_TRUE(editor.modeling().create_box(
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      "New geometry").ok());
  const auto active_id = project.active_scene_id();
  {
    ScopedManifestWriteFailure blocked(project.manifest_path());
    ASSERT_TRUE(blocked.ready()) << blocked.message();
    EXPECT_FALSE(project.save(active_id).ok);
    EXPECT_EQ(read_file_bytes(absolute), original_bytes);
    EXPECT_TRUE(project.active_scene()->dirty());
    EXPECT_EQ(project.manifest()->minimum_product_release(), kPublicRelease01);
    EXPECT_EQ(project.active_scene()->session->minimum_product_release(), kPublicRelease01);
  }
  const auto saved = project.save(active_id);
  ASSERT_TRUE(saved.ok) << saved.message;
  EXPECT_EQ(project.manifest()->minimum_product_release(), kInternalFullRelease);
  EXPECT_EQ(project.active_scene()->session->minimum_product_release(), kInternalFullRelease);
  const auto read = QdrDocumentReader::read_project_document(absolute);
  ASSERT_TRUE(read.ok) << read.message;
  EXPECT_EQ(read.minimum_product_release, kInternalFullRelease);
  const auto persisted = ProjectManifestStore::open(project.manifest_path());
  ASSERT_TRUE(persisted.ok());
  EXPECT_EQ(persisted.value().manifest.minimum_product_release(), kInternalFullRelease);
}

TEST(ProjectSessionWorkspace, SceneSaveRoutesAcceptPendingEdits) {
  for (const int route : {0, 1, 2, 3}) {
    SCOPED_TRACE(route);
    ScopedProjectSessionDirectory temporary("save-accepts-preview");
    ProjectSession project;
    ASSERT_TRUE(project.create(temporary.make_empty_child("Project")).ok);
    QuaderEditorApi editor(project);
    const auto object = create_saved_transition_curve(project, editor);
    OpenSceneSession *scene = project.active_scene();
    ASSERT_NE(scene, nullptr);
    ASSERT_TRUE(editor.tools().activate(kCurveToolId).ok());
    CurveToolService &curve = scene->session->curve();
    ASSERT_TRUE(curve.preview_state().document.has_value());
    const auto before = *curve.preview_state().document;
    const auto history_count = scene->session->history().undo_count();
    ASSERT_TRUE(select_and_begin_curve_control_drag(
        curve, {.point = before.points()[0].id}));
    ASSERT_TRUE(curve.update_drag({-5.0f, 1.0f, 2.0f}).ok);
    const auto expected = *curve.preview_state().document;
    ASSERT_TRUE(scene->dirty());

    const ProjectRelativePath destination = route == 1
        ? project_path("Scenes/Nested/Accepted.qdr")
        : project_path("Scenes/Source.qdr");
    if (route == 0) {
      const auto saved = editor.scene_documents().save_active();
      ASSERT_TRUE(saved.ok()) << saved.error_message();
    } else if (route == 1) {
      ASSERT_TRUE(std::filesystem::create_directories(
          project.root() / destination.native_path().parent_path()));
      const auto saved = editor.scene_documents().save_as(scene->session_id, destination);
      ASSERT_TRUE(saved.ok()) << saved.error_message();
    } else if (route == 2) {
      const auto saved = editor.scene_documents().save_all();
      ASSERT_TRUE(saved.ok()) << saved.error_message();
    } else {
      const auto saved = project.save_all();
      ASSERT_TRUE(saved.ok) << saved.message;
    }

    EXPECT_FALSE(curve.dragging());
    EXPECT_FALSE(scene->session->modeling().curve_document_preview_active());
    EXPECT_FALSE(scene->dirty());
    EXPECT_EQ(scene->session->history().undo_count(), history_count + 1U);
    const auto read = QdrDocumentReader::read_project_document(
        project.root() / destination.native_path());
    ASSERT_TRUE(read.ok) << read.message;
    const auto ids = read.document.assets().curve_ids();
    ASSERT_EQ(ids.size(), 1U);
    const auto *saved_curve = read.document.assets().curve(ids.front());
    ASSERT_NE(saved_curve, nullptr);
    ASSERT_NE(saved_curve->document, nullptr);
    EXPECT_TRUE(saved_curve->document->has_same_geometry(expected));
    ASSERT_TRUE(editor.history().undo().ok());
    const auto undone = editor.modeling().curve_snapshot(object);
    ASSERT_TRUE(undone.ok());
    EXPECT_TRUE(undone.value().document.has_same_geometry(before));
    ASSERT_TRUE(editor.history().redo().ok());
    const auto redone = editor.modeling().curve_snapshot(object);
    ASSERT_TRUE(redone.ok());
    EXPECT_TRUE(redone.value().document.has_same_geometry(expected));
  }
}

TEST(ProjectSessionWorkspace, ClosePromptsIncludeCleanPendingChangesWithoutAccepting) {
  for (const bool close_project : {false, true}) {
    SCOPED_TRACE(close_project);
    ScopedProjectSessionDirectory temporary("close-accepts-preview");
    ProjectSession project;
    ASSERT_TRUE(project.create(temporary.make_empty_child("Project")).ok);
    QuaderEditorApi editor(project);
    const auto object = editor.modeling().create_box(
        {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        "Pending Pivot");
    ASSERT_TRUE(object.ok()) << object.error_message();
    OpenSceneSession *scene = project.active_scene();
    ASSERT_NE(scene, nullptr);
    const auto id = scene->session_id;
    const auto path = project_path("Scenes/Source.qdr");
    ASSERT_TRUE(editor.scene_documents().save_as(id, path).ok());
    ASSERT_TRUE(editor.tools().activate(ViewportToolId::Pivot).ok());
    ASSERT_TRUE(editor.tools().execute_action("pivot", "pivot.align.bottom_center").ok());
    const auto expected = scene->session->pivot().persistent_preview_position();
    ASSERT_TRUE(expected.has_value());
    expect_vec3_eq(*expected, {0.0f, -1.0f, 0.0f});
    const auto history_count = scene->session->history().undo_count();
    const auto stamp = scene->session->content_revision_stamp();
    ASSERT_FALSE(scene->dirty());
    int prompt_count = 0;
    QuaderDirtySceneDecision decision = QuaderDirtySceneDecision::Cancel;
    editor.scene_documents().set_runtime_hooks({
        .confirm_dirty_scene = [&](const QuaderSceneDocumentInfo &info,
                                   std::string_view) {
          ++prompt_count;
          EXPECT_EQ(info.session_id, id);
          EXPECT_TRUE(info.dirty);
          EXPECT_FALSE(scene->dirty());
          EXPECT_EQ(scene->session->history().undo_count(), history_count);
          return decision;
        }});

    const auto closed = close_project ? editor.project().close()
                                     : editor.scene_documents().close(id);
    ASSERT_TRUE(closed.ok()) << closed.error_message();
    EXPECT_EQ(prompt_count, 1);
    EXPECT_EQ(project.active_scene(), scene);
    EXPECT_FALSE(scene->dirty());
    EXPECT_EQ(scene->session->content_revision_stamp(), stamp);
    EXPECT_EQ(scene->session->history().undo_count(), history_count);
    const auto retained = scene->session->pivot().persistent_preview_position();
    ASSERT_TRUE(retained.has_value());
    expect_vec3_eq(*retained, *expected);
    const auto saved_path = project.root() / path.native_path();
    decision = QuaderDirtySceneDecision::Save;
    const auto saved_and_closed = close_project ? editor.project().close()
        : editor.scene_documents().close(id);
    ASSERT_TRUE(saved_and_closed.ok()) << saved_and_closed.error_message();
    EXPECT_EQ(prompt_count, 2);
    EXPECT_EQ(project.find(id), nullptr);
    const auto read = QdrDocumentReader::read_project_document(saved_path);
    ASSERT_TRUE(read.ok) << read.message;
    const auto *saved_transform = read.document.scene().transform(object.value().entity);
    ASSERT_NE(saved_transform, nullptr);
    expect_vec3_eq(saved_transform->location, *expected);
  }
}

TEST(ProjectSessionWorkspace, DirectCloseCannotLoseCleanPendingChanges) {
  for (const bool close_project : {false, true}) {
    SCOPED_TRACE(close_project);
    ScopedProjectSessionDirectory temporary("direct-close-preview");
    ProjectSession project;
    ASSERT_TRUE(project.create(temporary.make_empty_child("Project")).ok);
    QuaderEditorApi editor(project);
    ASSERT_TRUE(editor.modeling().create_box(
        {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        "Pending Pivot").ok());
    OpenSceneSession *scene = project.active_scene();
    ASSERT_NE(scene, nullptr);
    const auto id = scene->session_id;
    ASSERT_TRUE(editor.scene_documents().save_as(id, project_path("Scenes/Source.qdr")).ok());
    ASSERT_TRUE(editor.tools().activate(ViewportToolId::Pivot).ok());
    ASSERT_TRUE(editor.tools().execute_action("pivot", "pivot.align.bottom_center").ok());
    const auto expected = scene->session->pivot().persistent_preview_position();
    ASSERT_TRUE(expected.has_value());
    expect_vec3_eq(*expected, {0.0f, -1.0f, 0.0f});
    const auto history_count = scene->session->history().undo_count();
    ASSERT_FALSE(scene->dirty());
    const auto closed = close_project ? project.close() : project.close_scene(id);
    EXPECT_FALSE(closed.ok);
    EXPECT_EQ(project.active_scene(), scene);
    EXPECT_TRUE(scene->dirty());
    EXPECT_EQ(scene->session->history().undo_count(), history_count + 1U);
    const auto retained = editor.query().selection_transform_pivot();
    ASSERT_TRUE(retained.has_value());
    expect_vec3_eq(*retained, *expected);
    const auto discarded = close_project ? project.close(true)
                                        : project.close_scene(id, true);
    EXPECT_TRUE(discarded.ok) << discarded.message;
  }
}

TEST(ProjectSessionWorkspace, DirectSaveAllAcceptsModalPolygonAndProjectMaterial) {
  for (const bool reopen : {false, true}) {
    SCOPED_TRACE(reopen);
    ScopedProjectSessionDirectory temporary("save-all-polygon-preview");
    ProjectSession project;
    ASSERT_TRUE(project.create(temporary.make_empty_child("Project")).ok);
    const auto path = project_path("Scenes/Polygon.qdr");
    ASSERT_TRUE(project.save_as(project.active_scene_id(), path).ok);
    if (reopen) {
      const auto manifest = project.manifest_path();
      ASSERT_TRUE(project.close().ok);
      ASSERT_TRUE(project.open(manifest).ok);
    }
    QuaderEditorApi editor(project);
    ASSERT_TRUE(editor.tools().activate(ViewportToolId::Poly).ok());
    OpenSceneSession *scene = project.active_scene();
    ASSERT_NE(scene, nullptr);
    auto &modeling = scene->session->modeling();
    for (const QVec3 point : {QVec3{-2.0f, 0.0f, -2.0f},
                             QVec3{2.0f, 0.0f, -2.0f},
                             QVec3{2.0f, 0.0f, 2.0f}}) {
      ModelingToolRayEvent event;
      event.ray = {.origin = point + QVec3{0.0f, 10.0f, 0.0f},
                   .direction = {0.0f, -1.0f, 0.0f}};
      event.camera = {.orthographic = true,
                      .origin = event.ray.origin,
                      .x_axis = {1.0f, 0.0f, 0.0f},
                      .y_axis = {0.0f, 0.0f, -1.0f},
                      .z_axis = {0.0f, 1.0f, 0.0f},
                      .viewport_size = {800.0f, 600.0f}};
      event.snap_enabled = false;
      const auto accepted = modeling.accept_poly_point(event);
      ASSERT_TRUE(accepted.ok) << accepted.message;
    }
    ASSERT_EQ(modeling.tool_status().poly_point_count, 3);
    ASSERT_TRUE(modeling.pending_operation_active());
    ASSERT_FALSE(scene->dirty());
    const auto saved = project.save_all();
    ASSERT_TRUE(saved.ok) << saved.message;
    EXPECT_TRUE(saved.changed);
    EXPECT_FALSE(scene->dirty());
    EXPECT_FALSE(modeling.pending_operation_active());
    EXPECT_EQ(modeling.objects().size(), 1U);
    const auto read = QdrDocumentReader::read_project_document(
        project.root() / path.native_path());
    ASSERT_TRUE(read.ok) << read.message;
    EXPECT_EQ(read.document.assets().mesh_count(), 1U);
    ASSERT_TRUE(editor.history().undo().ok());
    EXPECT_TRUE(modeling.objects().empty());
    ASSERT_TRUE(editor.history().redo().ok());
    EXPECT_EQ(modeling.objects().size(), 1U);
  }
}

TEST(ProjectSessionWorkspace, StandaloneProjectReplacementAcceptsBeforeDirtyPrompt) {
  for (const bool open_scene : {false, true}) {
    SCOPED_TRACE(open_scene);
    ScopedProjectSessionDirectory temporary("standalone-project-preview");
    SceneSession scene;
    QuaderEditorApi editor(
        QuaderApiSessionInternalAccess::from_scene_session(scene));
    const auto object = editor.modeling().create_box(
        {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        "Pending Pivot");
    ASSERT_TRUE(object.ok()) << object.error_message();
    const auto path = temporary.path() / "Source.qdr";
    ASSERT_TRUE(editor.scene_documents().save_as_native_path(
        *editor.scene_documents().active_scene_id(), path).ok());
    const auto original_pivot = editor.query().selection_transform_pivot();
    ASSERT_TRUE(original_pivot.has_value());
    ASSERT_TRUE(editor.tools().activate(ViewportToolId::Pivot).ok());
    ASSERT_TRUE(editor.tools().execute_action("pivot", "pivot.align.bottom_center").ok());
    // Persistent Pivot previews do not replace the committed transform pivot
    // until Apply or implicit settlement publishes the rebase.
    const auto preview = scene.pivot().persistent_preview_position();
    ASSERT_TRUE(preview.has_value());
    expect_vec3_eq(*preview, {0.0f, -1.0f, 0.0f});
    ASSERT_FALSE(scene.dirty());
    const auto history_count = scene.history().undo_count();
    int prompts = 0;
    editor.project().set_runtime_hooks({
        .confirm_dirty_project_replacement = [&](std::string_view) {
          ++prompts;
          EXPECT_TRUE(scene.dirty());
          EXPECT_EQ(scene.history().undo_count(), history_count + 1U);
          return QuaderDirtyProjectDecision::Cancel;
        }});
    const auto result = open_scene ? editor.project().open(path)
                                   : editor.project().new_project();
    ASSERT_TRUE(result.ok()) << result.error_message();
    EXPECT_FALSE(result.changed());
    EXPECT_EQ(prompts, 1);
    const auto retained = editor.query().selection_transform_pivot();
    ASSERT_TRUE(retained.has_value());
    expect_vec3_eq(*retained, *preview);
    EXPECT_TRUE(scene.dirty());
    ASSERT_TRUE(editor.history().undo().ok());
    const auto undone = editor.query().selection_transform_pivot();
    ASSERT_TRUE(undone.has_value());
    expect_vec3_eq(*undone, *original_pivot);
    ASSERT_TRUE(editor.history().redo().ok());
    const auto redone = editor.query().selection_transform_pivot();
    ASSERT_TRUE(redone.has_value());
    expect_vec3_eq(*redone, *preview);
  }
}

TEST(ProjectSessionWorkspace, InvalidStandaloneProjectOpenPreservesPendingEdit) {
  ScopedProjectSessionDirectory temporary("invalid-standalone-preview");
  SceneSession scene;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(scene));
  ASSERT_TRUE(editor.modeling().create_box(
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      "Pending Pivot").ok());
  ASSERT_TRUE(editor.scene_documents().save_as_native_path(
      *editor.scene_documents().active_scene_id(),
      temporary.path() / "Source.qdr").ok());
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::Pivot).ok());
  ASSERT_TRUE(editor.tools().execute_action("pivot", "pivot.align.bottom_center").ok());
  const auto stamp = scene.content_revision_stamp();
  const auto history_count = scene.history().undo_count();
  ASSERT_FALSE(scene.dirty());
  int prompts = 0;
  editor.project().set_runtime_hooks({
      .confirm_dirty_project_replacement = [&](std::string_view) {
        ++prompts;
        return QuaderDirtyProjectDecision::Discard;
      }});
  EXPECT_FALSE(editor.project().open(temporary.path() / "Missing.qdr").ok());
  EXPECT_EQ(prompts, 0);
  EXPECT_EQ(scene.content_revision_stamp(), stamp);
  EXPECT_EQ(scene.history().undo_count(), history_count);
  EXPECT_TRUE(editor.tools().active_settings().active);
  EXPECT_FALSE(scene.dirty());
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
}

TEST(ProjectSessionWorkspace,
     ProjectLoadingEndsAfterBindingAndLifecyclePublication) {
  ScopedProjectSessionDirectory temporary("project-loading-order");
  ProjectSession project;
  QuaderEditorApi editor(project);
  std::vector<std::string> order;
  bool loading = false;
  editor.project().set_runtime_hooks({
      .begin_loading = [&]() {
        EXPECT_FALSE(loading);
        loading = true;
        order.push_back("begin");
      },
      .end_loading = [&](bool succeeded) {
        EXPECT_TRUE(loading);
        loading = false;
        order.push_back(succeeded ? "end-installed" : "end-uninstalled");
      },
  });
  QuaderEditorApiSessionAccess::set_scene_binding_changed_callback(
      editor, [&](SceneSession &scene) {
        EXPECT_TRUE(loading);
        EXPECT_EQ(&scene, project.active_scene()->session.get());
        order.push_back("binding");
      });
  const auto opened = editor.events().subscribe(
      quader_event::kActiveSceneChanged, [&](const QuaderEvent &) {
        EXPECT_TRUE(loading);
        EXPECT_EQ(order.back(), "binding");
        order.push_back("active");
      });
  ASSERT_TRUE(opened.valid());
  ASSERT_TRUE(editor.project().create(temporary.make_empty_child("Project")).ok());
  EXPECT_EQ(order, (std::vector<std::string>{
                       "begin", "binding", "active", "end-installed"}));
  ASSERT_TRUE(editor.scene_documents()
                  .save_as(*editor.scene_documents().active_scene_id(),
                           project_path("Scenes/Loading.qdr"))
                  .ok());
  const auto manifest = editor.project().manifest_path();
  // Closing replaces the binding without a loading presentation.
  QuaderEditorApiSessionAccess::set_scene_binding_changed_callback(
      editor, [&](SceneSession &) { EXPECT_FALSE(loading); });
  ASSERT_TRUE(editor.events().unsubscribe(opened));
  ASSERT_TRUE(editor.project().close().ok());
  order.clear();

  QuaderEditorApiSessionAccess::set_scene_binding_changed_callback(
      editor, [&](SceneSession &scene) {
        EXPECT_TRUE(loading);
        EXPECT_EQ(&scene, project.active_scene()->session.get());
        order.push_back("binding");
      });
  const auto reopened = editor.events().subscribe(
      quader_event::kActiveSceneChanged, [&](const QuaderEvent &) {
        EXPECT_TRUE(loading);
        EXPECT_EQ(order.back(), "binding");
        order.push_back("active");
      });
  ASSERT_TRUE(reopened.valid());
  ASSERT_TRUE(editor.project().open(manifest).ok());
  EXPECT_FALSE(loading);
  EXPECT_EQ(order, (std::vector<std::string>{
                       "begin", "end-uninstalled", "begin", "binding",
                       "active", "end-installed"}));
}

TEST(ProjectSessionWorkspace,
     ProjectLoadingRestoresBeforeDirtyPromptsAndBalancesFailedReads) {
  ScopedProjectSessionDirectory temporary("project-loading-failures");
  ProjectSession candidate;
  ASSERT_TRUE(candidate.create(temporary.make_empty_child("Candidate")).ok);
  const auto candidate_manifest = candidate.manifest_path();
  ASSERT_TRUE(candidate.close(true).ok);
  ProjectSession project;
  QuaderEditorApi editor(project);
  ASSERT_TRUE(editor.project().create(temporary.make_empty_child("Original")).ok());
  ASSERT_TRUE(editor.scene().create_entity({.name = "Retained edit"}).ok());
  const auto original_root = project.root();
  const auto original_scene = project.active_scene_id();
  std::vector<std::string> order;
  bool loading = false;
  bool cancel_dirty = true;
  bool fail_install = false;
  int begins = 0;
  editor.project().set_runtime_hooks({
      .choose_open_manifest_path = [&]() -> std::optional<std::filesystem::path> {
        EXPECT_FALSE(loading);
        order.push_back("picker");
        return std::nullopt;
      },
      .begin_loading = [&]() {
        EXPECT_FALSE(loading);
        loading = true;
        order.push_back("begin");
        if (++begins == 2 && fail_install) {
          std::error_code error;
          EXPECT_TRUE(std::filesystem::remove(candidate_manifest, error));
          EXPECT_FALSE(error);
        }
      },
      .end_loading = [&](bool succeeded) {
        EXPECT_TRUE(loading);
        EXPECT_FALSE(succeeded);
        loading = false;
        order.push_back("end");
      },
  });
  editor.scene_documents().set_runtime_hooks({
      .confirm_dirty_scene = [&](const QuaderSceneDocumentInfo &, std::string_view) {
        EXPECT_FALSE(loading);
        order.push_back("prompt");
        return cancel_dirty ? QuaderDirtySceneDecision::Cancel
                            : QuaderDirtySceneDecision::Discard;
      },
  });
  const auto picker_canceled = editor.project().open({});
  ASSERT_TRUE(picker_canceled.ok());
  EXPECT_FALSE(picker_canceled.changed());
  EXPECT_EQ(order, (std::vector<std::string>{"picker"}));

  order.clear();
  EXPECT_FALSE(editor.project().open(temporary.path() / "Missing.qproj").ok());
  EXPECT_EQ(order, (std::vector<std::string>{"begin", "end"}));
  EXPECT_FALSE(loading);

  order.clear();
  const auto dirty_canceled = editor.project().open(candidate_manifest);
  ASSERT_TRUE(dirty_canceled.ok());
  EXPECT_FALSE(dirty_canceled.changed());
  EXPECT_EQ(order, (std::vector<std::string>{"begin", "end", "prompt"}));
  EXPECT_EQ(project.active_scene_id(), original_scene);
  EXPECT_EQ(project.root(), original_root);
  EXPECT_TRUE(project.active_scene()->dirty());

  order.clear();
  begins = 0;
  cancel_dirty = false;
  fail_install = true;
  EXPECT_FALSE(editor.project().open(candidate_manifest).ok());
  EXPECT_EQ(order, (std::vector<std::string>{
                       "begin", "end", "prompt", "begin", "end"}));
  EXPECT_FALSE(loading);
  EXPECT_EQ(project.active_scene_id(), original_scene);
  EXPECT_EQ(project.root(), original_root);
  EXPECT_TRUE(project.active_scene()->dirty());
}

TEST(ProjectSessionWorkspace,
     SceneLoadingCoversOpenRoutesAndRetainsHooksAcrossRebinding) {
  ScopedProjectSessionDirectory temporary("scene-loading-routes");
  ProjectSession project;
  QuaderEditorApi editor(project);
  ASSERT_TRUE(editor.project().create(temporary.make_empty_child("Project")).ok());
  const auto path = project_path("Scenes/Loading.qdr");
  ASSERT_TRUE(editor.scene_documents()
                  .save_as(*editor.scene_documents().active_scene_id(), path)
                  .ok());
  const auto native_path = project.root() / path.native_path();
  const auto recents = editor.project().recent_scenes();
  ASSERT_EQ(recents.size(), 1u);
  std::vector<std::string> order;
  bool loading = false;
  for (int route = 0; route < 4; ++route) {
    SCOPED_TRACE(route);
    ASSERT_TRUE(editor.scene_documents().close_active().ok());
    order.clear();
    editor.scene_documents().set_runtime_hooks({
        .choose_open_scene_path = [&]() -> std::optional<std::filesystem::path> {
          EXPECT_FALSE(loading);
          order.push_back("picker");
          return native_path;
        },
        .begin_loading = [&]() {
          EXPECT_FALSE(loading);
          loading = true;
          order.push_back("begin");
          // A host may replace its callbacks while presentation is starting.
          editor.scene_documents().set_runtime_hooks({});
        },
        .end_loading = [&](bool succeeded) {
          EXPECT_TRUE(loading);
          EXPECT_TRUE(succeeded);
          loading = false;
          order.push_back("end");
        },
    });
    QuaderEditorApiSessionAccess::set_scene_binding_changed_callback(
        editor, [&](SceneSession &scene) {
          EXPECT_TRUE(loading);
          EXPECT_EQ(&scene, project.active_scene()->session.get());
          order.push_back("binding");
        });
    const auto active = editor.events().subscribe(
        quader_event::kActiveSceneChanged, [&](const QuaderEvent &) {
          EXPECT_TRUE(loading);
          EXPECT_EQ(order.back(), "binding");
          order.push_back("active");
        });
    ASSERT_TRUE(active.valid());
    switch (route) {
    case 0:
      ASSERT_TRUE(editor.scene_documents().open(path).ok());
      break;
    case 1:
      ASSERT_TRUE(editor.scene_documents().open_native_path(native_path).ok());
      break;
    case 2:
      ASSERT_TRUE(editor.project().open_recent_scene(recents.front()).ok());
      break;
    case 3:
      ASSERT_TRUE(editor.scene_documents().open().ok());
      break;
    }
    EXPECT_FALSE(loading);
    std::vector<std::string> expected{"begin", "binding", "active", "end"};
    if (route == 3) {
      expected.insert(expected.begin(), "picker");
    }
    EXPECT_EQ(order, expected);
    QuaderEditorApiSessionAccess::set_scene_binding_changed_callback(
        editor, [&](SceneSession &) { EXPECT_FALSE(loading); });
    ASSERT_TRUE(editor.events().unsubscribe(active));
  }

  order.clear();
  editor.scene_documents().set_runtime_hooks({
      .choose_open_scene_path = [&]() -> std::optional<std::filesystem::path> {
        order.push_back("picker");
        return std::nullopt;
      },
      .begin_loading = [&]() { order.push_back("begin"); },
      .end_loading = [&](bool succeeded) {
        EXPECT_FALSE(succeeded);
        order.push_back("end");
      },
  });
  const auto canceled = editor.scene_documents().open();
  EXPECT_TRUE(canceled.ok());
  EXPECT_FALSE(canceled.changed());
  EXPECT_EQ(order, (std::vector<std::string>{"picker"}));
  order.clear();
  const auto unchanged = editor.scene_documents().open(path);
  EXPECT_TRUE(unchanged.ok());
  EXPECT_FALSE(unchanged.changed());
  EXPECT_EQ(order, (std::vector<std::string>{"begin", "end"}));
  order.clear();
  const auto active_before = project.active_scene_id();
  EXPECT_FALSE(editor.scene_documents().open(project_path("Scenes/Missing.qdr")).ok());
  EXPECT_EQ(order, (std::vector<std::string>{"begin", "end"}));
  EXPECT_EQ(project.active_scene_id(), active_before);
}

TEST(ProjectSessionWorkspace,
     StandaloneLoadingBalancesCompatibilityOpenFailureAndCancellation) {
  ScopedProjectSessionDirectory temporary("standalone-loading");
  SceneSession scene;
  QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(scene));
  const auto path = temporary.path() / "Scene.qdr";
  ASSERT_TRUE(editor.scene_documents()
                  .save_as_native_path(*editor.scene_documents().active_scene_id(), path)
                  .ok());
  std::vector<bool> completions;
  bool loading = false;
  const auto begin = [&]() {
    EXPECT_FALSE(loading);
    loading = true;
  };
  const auto end = [&](bool succeeded) {
    EXPECT_TRUE(loading);
    loading = false;
    completions.push_back(succeeded);
  };
  editor.scene_documents().set_runtime_hooks({
      .begin_loading = begin,
      .end_loading = end,
  });
  editor.project().set_runtime_hooks({
      .confirm_dirty_project_replacement = [&](std::string_view) {
        EXPECT_FALSE(loading);
        return QuaderDirtyProjectDecision::Cancel;
      },
      .begin_loading = begin,
      .end_loading = end,
  });
  ASSERT_TRUE(editor.scene_documents().open_native_path(path).ok());
  EXPECT_EQ(completions, (std::vector<bool>{true}));
  completions.clear();
  EXPECT_FALSE(editor.scene_documents().open_native_path(temporary.path() / "Missing.qdr").ok());
  EXPECT_EQ(completions, (std::vector<bool>{false}));
  completions.clear();
  ASSERT_TRUE(editor.project().open(path).ok());
  EXPECT_EQ(completions, (std::vector<bool>{false, true}));
  completions.clear();
  ASSERT_TRUE(editor.scene().create_entity({.name = "Unsaved"}).ok());
  const auto canceled = editor.project().open(path);
  EXPECT_TRUE(canceled.ok());
  EXPECT_FALSE(canceled.changed());
  EXPECT_EQ(completions, (std::vector<bool>{false}));
  EXPECT_TRUE(scene.dirty());
  EXPECT_FALSE(loading);
}

} // namespace
