////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_scene_documents_api.hpp"

#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_api_status_internal.hpp"
#include "api/quader_events_api.hpp"
#include "api/quader_events_api_internal_access.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/session/project_session.hpp"
#include "editor/session/scene_viewport_camera_persistence.hpp"
#include "editor/tools/native_viewport_tool_coordinator.hpp"
#include "io/qdr/qdr_document_reader.hpp"
#include "io/qdr/qdr_document_writer.hpp"

#include <algorithm>
#include <utility>

namespace quader {
namespace {

QuaderApiVoidResult status_for(SceneSession &session, bool ok, bool changed,
                               std::string message) {
  return quader_api_void_status(
      session, ok, changed, std::move(message),
      changed ? QuaderMutationKind::Runtime : QuaderMutationKind::None);
}

QuaderApiVoidResult result_status(SceneSession &session,
                                  const ProjectSessionResult &result) {
  return status_for(session, result.ok, result.changed, result.message);
}

QuaderApiResult<SceneSessionId>
scene_result(SceneSession &session, const ProjectSessionResult &result) {
  const QuaderApiVoidResult status = result_status(session, result);
  return quader_api_result_from_status(
      status, result.scene_session_id.value_or(SceneSessionId{}));
}

bool has_uncommitted_preview(const SceneSession &session) {
  return session.native_tools().has_pending_tool() ||
         session.modeling().can_cancel_active_tool_or_preview();
}

} // namespace

/// Owns project-backed and standalone scene-document API state.
class QuaderSceneDocumentsApi::Impl final {
public:
  explicit Impl(QuaderApiSession session)
      : standalone_session(session), standalone_scene_id(SceneId::generate()) {}

  explicit Impl(ProjectSession &session) : project_session(&session) {}

  [[nodiscard]] SceneSession &status_session() const {
    if (project_session != nullptr) {
      if (OpenSceneSession *active = project_session->active_scene()) {
        return *active->session;
      }
      return project_session->fallback_scene();
    }
    return QuaderApiSessionInternalAccess::scene_session(standalone_session);
  }

  [[nodiscard]] SceneSession *active_session() const {
    if (project_session != nullptr) {
      OpenSceneSession *active = project_session->active_scene();
      return active != nullptr ? active->session.get() : nullptr;
    }
    return &QuaderApiSessionInternalAccess::scene_session(standalone_session);
  }

  void notify_binding_changed() {
    if (binding_changed) {
      const QuaderApiSession session =
          project_session != nullptr
              ? QuaderApiSessionInternalAccess::from_project_session(
                    *project_session)
              : standalone_session;
      binding_changed(session);
    }
  }

  [[nodiscard]] QuaderEvent
  make_event(std::string_view event_name,
             std::optional<SceneSessionId> scene_id = std::nullopt) const {
    QuaderEvent event{.name = std::string(event_name)};
    if (project_session != nullptr) {
      if (const ProjectManifest *manifest = project_session->manifest()) {
        event.project_id = manifest->id();
        event.project_name = manifest->name();
        event.project_root = project_session->root().generic_string();
      }
      const SceneSessionId target =
          scene_id.value_or(project_session->active_scene_id());
      if (const OpenSceneSession *scene = project_session->find(target)) {
        event.scene_session_id = scene->session_id;
        event.scene_id = scene->scene_id;
        event.scene_name = scene->display_name;
        if (scene->path.has_value()) {
          event.scene_path = scene->path->portable_string();
        }
        event.scene_dirty = scene->dirty();
        event.scene_active =
            scene->session_id == project_session->active_scene_id();
      }
      return event;
    }
    event.scene_session_id = standalone_session_id;
    event.scene_id = standalone_scene_id;
    event.scene_name = status_session().document().name();
    if (!standalone_path.empty()) {
      event.scene_path = standalone_path.generic_string();
    }
    event.scene_dirty = status_session().dirty();
    event.scene_active = true;
    return event;
  }

  void publish(QuaderEvent event) const {
    if (events != nullptr) {
      (void)QuaderEventsApiInternalAccess::publish(*events, std::move(event));
    }
  }

  void publish(std::string_view event_name,
               std::optional<SceneSessionId> scene_id = std::nullopt) const {
    publish(make_event(event_name, scene_id));
  }

  [[nodiscard]] std::optional<ProjectRelativePath>
  relative_dialog_path(const std::filesystem::path &absolute) const {
    if (project_session == nullptr || project_session->workspace() == nullptr) {
      return std::nullopt;
    }
    ProjectIoResult<ProjectRelativePath> relative =
        project_session->workspace()->file_system.relative_from_absolute(
            absolute);
    return relative.ok()
               ? std::optional<ProjectRelativePath>(relative.take_value())
               : std::nullopt;
  }

  ProjectSession *project_session = nullptr;
  QuaderApiSession standalone_session;
  SceneSessionId standalone_session_id = SceneSessionId::from_parts(0u, 1u);
  SceneId standalone_scene_id;
  std::filesystem::path standalone_path;
  QuaderSceneDocumentsRuntimeHooks hooks;
  std::function<void(QuaderApiSession)> binding_changed;
  QuaderEventsApi *events = nullptr;
};

QuaderSceneDocumentsApi::QuaderSceneDocumentsApi(
    QuaderApiSession standalone_session)
    : impl_(std::make_unique<Impl>(standalone_session)) {}

QuaderSceneDocumentsApi::QuaderSceneDocumentsApi(ProjectSession &project_session)
    : impl_(std::make_unique<Impl>(project_session)) {}

QuaderSceneDocumentsApi::~QuaderSceneDocumentsApi() = default;

QuaderApiResult<SceneSessionId> QuaderSceneDocumentsApi::new_scene() {
  if (impl_->project_session == nullptr) {
    SceneSession &session = impl_->status_session();
    session.replace_document(SceneContentDocument(SceneContentDocumentRecord{
        .id = 0u,
        .name = "Untitled Scene",
    }));
    session.mark_clean();
    impl_->standalone_scene_id = SceneId::generate();
    impl_->standalone_path.clear();
    impl_->notify_binding_changed();
    impl_->publish(quader_event::kSceneOpened);
    return quader_api_result_from_status(
        status_for(session, true, true, "New scene created."),
        impl_->standalone_session_id);
  }
  ProjectSessionResult result = impl_->project_session->new_scene();
  impl_->notify_binding_changed();
  if (result.ok) {
    impl_->publish(quader_event::kSceneOpened);
    impl_->publish(quader_event::kActiveSceneChanged);
  }
  return scene_result(impl_->status_session(), result);
}

QuaderApiResult<SceneSessionId>
QuaderSceneDocumentsApi::open(ProjectRelativePath path) {
  if (impl_->project_session == nullptr) {
    return quader_api_result_from_status(
        status_for(impl_->status_session(), false, false,
                   "Standalone scene open requires an absolute compatibility "
                   "path."),
        SceneSessionId{});
  }
  const std::size_t open_count_before =
      impl_->project_session->open_scenes().size();
  const SceneSessionId active_before =
      impl_->project_session->active_scene_id();
  ProjectSessionResult result =
      impl_->project_session->open_scene(std::move(path));
  impl_->notify_binding_changed();
  if (result.ok) {
    if (impl_->project_session->open_scenes().size() > open_count_before) {
      impl_->publish(quader_event::kSceneOpened);
    }
    if (impl_->project_session->active_scene_id() != active_before) {
      impl_->publish(quader_event::kActiveSceneChanged);
    }
  }
  return scene_result(impl_->status_session(), result);
}

QuaderApiResult<SceneSessionId> QuaderSceneDocumentsApi::open() {
  if (!impl_->hooks.choose_open_scene_path) {
    return quader_api_result_from_status(
        status_for(impl_->status_session(), false, false,
                   "Scene open dialog is unavailable."),
        SceneSessionId{});
  }
  const std::optional<std::filesystem::path> selected =
      impl_->hooks.choose_open_scene_path();
  if (!selected.has_value()) {
    return quader_api_result_from_status(
        status_for(impl_->status_session(), true, false,
                   "Scene open canceled."),
        SceneSessionId{});
  }
  const std::optional<ProjectRelativePath> relative =
      impl_->relative_dialog_path(*selected);
  if (!relative.has_value()) {
    return quader_api_result_from_status(
        status_for(impl_->status_session(), false, false,
                   "Scene files must remain inside the project."),
        SceneSessionId{});
  }
  return open(*relative);
}

QuaderApiResult<SceneSessionId> QuaderSceneDocumentsApi::open_native_path(
    const std::filesystem::path &path) {
  if (path.empty()) {
    return quader_api_result_from_status(
        status_for(impl_->status_session(), false, false,
                   "Scene open requires a path."),
        SceneSessionId{});
  }
  if (impl_->project_session != nullptr) {
    const std::optional<ProjectRelativePath> relative =
        impl_->relative_dialog_path(path);
    if (!relative.has_value()) {
      return quader_api_result_from_status(
          status_for(impl_->status_session(), false, false,
                     "Scene files must remain inside the project."),
          SceneSessionId{});
    }
    return open(*relative);
  }

  SceneSession &session = impl_->status_session();
  if (session.dirty()) {
    return quader_api_result_from_status(
        status_for(session, false, false,
                   "Save or discard the current standalone scene before "
                   "opening another scene."),
        SceneSessionId{});
  }
  QdrProjectReadResult read =
      QdrDocumentReader::read_project_document(path);
  if (!read.ok) {
    return quader_api_result_from_status(
        status_for(session, false, false,
                   read.message.empty() ? "Scene open failed."
                                        : std::move(read.message)),
        SceneSessionId{});
  }
  if (!session.replace_document_with_persisted_viewport_camera(
          std::move(read.document),
          viewport_camera_pose_from_qdr(read.viewport_camera_pose))) {
    return quader_api_result_from_status(
        status_for(session, false, false,
                   "Scene viewport camera pose could not be restored."),
        SceneSessionId{});
  }
  impl_->standalone_path = path;
  impl_->standalone_scene_id = SceneId::from_bytes(read.scene_uuid);
  impl_->notify_binding_changed();
  impl_->publish(quader_event::kSceneOpened, impl_->standalone_session_id);
  impl_->publish(quader_event::kActiveSceneChanged,
                 impl_->standalone_session_id);
  return quader_api_result_from_status(
      status_for(session, true, true, "Scene opened."),
      impl_->standalone_session_id);
}

QuaderApiVoidResult QuaderSceneDocumentsApi::activate(SceneSessionId id) {
  if (impl_->project_session == nullptr) {
    return status_for(impl_->status_session(),
                      id == impl_->standalone_session_id, false,
                      id == impl_->standalone_session_id
                          ? "Scene is already active."
                          : "Scene tab is closed or stale.");
  }
  ProjectSessionResult result = impl_->project_session->activate(id);
  if (result.ok && result.changed) {
    impl_->notify_binding_changed();
    impl_->publish(quader_event::kActiveSceneChanged);
  }
  return result_status(impl_->status_session(), result);
}

QuaderApiVoidResult QuaderSceneDocumentsApi::save(SceneSessionId id) {
  if (impl_->project_session == nullptr) {
    if (id != impl_->standalone_session_id) {
      return status_for(impl_->status_session(), false, false,
                        "Scene tab is closed or stale.");
    }
    if (impl_->standalone_path.empty()) {
      return save_as(id);
    }
    SceneSession &session = impl_->status_session();
    if (has_uncommitted_preview(session)) {
      return status_for(
          session, false, false,
          "Commit or cancel the active preview before saving the scene.");
    }
    const bool was_dirty = session.dirty();
    const QdrProjectWriteResult written =
        QdrDocumentWriter::write_project_document_atomic(
            impl_->standalone_path, session.document(),
            qdr_viewport_camera_pose_for_scene(session));
    if (!written.ok) {
      return status_for(session, false, false, written.message);
    }
    session.mark_clean();
    impl_->publish(quader_event::kSceneSaved, id);
    if (was_dirty != session.dirty()) {
      impl_->publish(quader_event::kSceneDirtyChanged, id);
    }
    return status_for(session, true, true, "Scene saved.");
  }

  OpenSceneSession *scene = impl_->project_session->find(id);
  if (scene != nullptr && !scene->path.has_value()) {
    return save_as(id);
  }
  const bool was_dirty = scene != nullptr && scene->dirty();
  ProjectSessionResult result = impl_->project_session->save(id);
  if (result.ok) {
    impl_->publish(quader_event::kSceneSaved, id);
    const OpenSceneSession *saved_scene = impl_->project_session->find(id);
    if (saved_scene != nullptr && was_dirty != saved_scene->dirty()) {
      impl_->publish(quader_event::kSceneDirtyChanged, id);
    }
  }
  return result_status(impl_->status_session(), result);
}

QuaderApiVoidResult QuaderSceneDocumentsApi::save_active() {
  const std::optional<SceneSessionId> id = active_scene_id();
  return id.has_value()
             ? save(*id)
             : status_for(impl_->status_session(), false, false,
                          "No scene is active.");
}

QuaderApiVoidResult
QuaderSceneDocumentsApi::save_as(SceneSessionId id, ProjectRelativePath path) {
  if (impl_->project_session == nullptr) {
    return status_for(impl_->status_session(), false, false,
                      "Standalone Save As requires a native dialog path.");
  }
  const OpenSceneSession *scene = impl_->project_session->find(id);
  const bool was_dirty = scene != nullptr && scene->dirty();
  ProjectSessionResult result =
      impl_->project_session->save_as(id, std::move(path));
  if (result.ok) {
    impl_->publish(quader_event::kSceneSaved, id);
    const OpenSceneSession *saved_scene = impl_->project_session->find(id);
    if (saved_scene != nullptr && was_dirty != saved_scene->dirty()) {
      impl_->publish(quader_event::kSceneDirtyChanged, id);
    }
  }
  return result_status(impl_->status_session(), result);
}

QuaderApiVoidResult QuaderSceneDocumentsApi::save_as(SceneSessionId id) {
  if (!impl_->hooks.choose_save_scene_as_path) {
    return status_for(impl_->status_session(), false, false,
                      "Scene Save As dialog is unavailable.");
  }
  const std::optional<std::filesystem::path> selected =
      impl_->hooks.choose_save_scene_as_path(id);
  if (!selected.has_value()) {
    return status_for(impl_->status_session(), true, false,
                      "Scene Save As canceled.");
  }
  return save_as_native_path(id, *selected);
}

QuaderApiVoidResult QuaderSceneDocumentsApi::save_as_native_path(
    SceneSessionId id, const std::filesystem::path &path) {
  if (path.empty()) {
    return status_for(impl_->status_session(), false, false,
                      "Scene Save As requires a path.");
  }
  if (impl_->project_session == nullptr) {
    if (id != impl_->standalone_session_id) {
      return status_for(impl_->status_session(), false, false,
                        "Scene tab is closed or stale.");
    }
    SceneSession &session = impl_->status_session();
    if (has_uncommitted_preview(session)) {
      return status_for(
          session, false, false,
          "Commit or cancel the active preview before saving the scene.");
    }
    const bool was_dirty = session.dirty();
    const QdrProjectWriteResult written =
        QdrDocumentWriter::write_project_document_atomic(
            path, session.document(),
            qdr_viewport_camera_pose_for_scene(session));
    if (!written.ok) {
      return status_for(session, false, false, written.message);
    }
    impl_->standalone_path = path;
    impl_->standalone_scene_id = SceneId::generate();
    session.mark_clean();
    impl_->publish(quader_event::kSceneSaved, id);
    if (was_dirty != session.dirty()) {
      impl_->publish(quader_event::kSceneDirtyChanged, id);
    }
    return status_for(session, true, true, "Scene saved.");
  }
  const std::optional<ProjectRelativePath> relative =
      impl_->relative_dialog_path(path);
  if (!relative.has_value()) {
    return status_for(impl_->status_session(), false, false,
                      "Scene files must remain inside the project.");
  }
  return save_as(id, *relative);
}

QuaderApiVoidResult QuaderSceneDocumentsApi::save_all() {
  if (impl_->project_session == nullptr) {
    return save_active();
  }
  bool saved_any = false;
  for (const QuaderSceneDocumentInfo &scene : open_scenes()) {
    if (!scene.dirty) {
      continue;
    }
    QuaderApiVoidResult saved = scene.path.has_value()
                                    ? save(scene.session_id)
                                    : save_as(scene.session_id);
    if (!saved.ok) {
      return saved;
    }
    saved_any = saved_any || saved.changed();
    const OpenSceneSession *saved_scene =
        impl_->project_session->find(scene.session_id);
    if (saved_scene == nullptr || saved_scene->dirty()) {
      return status_for(impl_->status_session(), true, saved_any,
                        "Save All canceled.");
    }
  }
  return status_for(impl_->status_session(), true, saved_any,
                    "All dirty scenes saved.");
}

QuaderApiVoidResult QuaderSceneDocumentsApi::close(SceneSessionId id) {
  if (impl_->project_session == nullptr) {
    return status_for(impl_->status_session(), false, false,
                      "The standalone scene cannot be closed.");
  }
  const OpenSceneSession *scene = impl_->project_session->find(id);
  if (scene == nullptr) {
    return status_for(impl_->status_session(), false, false,
                      "Scene tab is closed or stale.");
  }
  bool discard = false;
  if (scene->dirty()) {
    if (!impl_->hooks.confirm_dirty_scene) {
      return status_for(impl_->status_session(), false, false,
                        "Scene contains unsaved changes.");
    }
    QuaderSceneDocumentInfo info;
    for (const QuaderSceneDocumentInfo &candidate : open_scenes()) {
      if (candidate.session_id == id) {
        info = candidate;
        break;
      }
    }
    switch (impl_->hooks.confirm_dirty_scene(info, "scene.close")) {
    case QuaderDirtySceneDecision::Cancel:
      return status_for(impl_->status_session(), true, false,
                        "Scene close canceled.");
    case QuaderDirtySceneDecision::Save: {
      QuaderApiVoidResult saved = save(id);
      if (!saved.ok) {
        return saved;
      }
      const OpenSceneSession *saved_scene = impl_->project_session->find(id);
      if (saved_scene == nullptr || saved_scene->dirty()) {
        return status_for(impl_->status_session(), true, false,
                          "Scene close canceled.");
      }
      break;
    }
    case QuaderDirtySceneDecision::Discard:
      discard = true;
      break;
    }
  }
  const SceneSessionId active_before = impl_->project_session->active_scene_id();
  QuaderEvent closed_event =
      impl_->make_event(quader_event::kSceneClosed, id);
  ProjectSessionResult result =
      impl_->project_session->close_scene(id, discard);
  if (result.ok) {
    impl_->publish(std::move(closed_event));
    if (active_before == id) {
      impl_->notify_binding_changed();
      impl_->publish(quader_event::kActiveSceneChanged);
    }
  }
  return result_status(impl_->status_session(), result);
}

QuaderApiVoidResult QuaderSceneDocumentsApi::close_active() {
  const std::optional<SceneSessionId> id = active_scene_id();
  return id.has_value()
             ? close(*id)
             : status_for(impl_->status_session(), false, false,
                          "No scene is active.");
}

QuaderApiResult<QuaderSceneDocumentsApi::ProjectClosePreparation>
QuaderSceneDocumentsApi::prepare_project_close(std::string_view action_id) {
  if (impl_->project_session == nullptr) {
    return quader_api_result_from_status(
        status_for(impl_->status_session(), true, false, {}),
        ProjectClosePreparation{.proceed = true});
  }

  std::vector<QuaderSceneDocumentInfo> ordered = open_scenes();
  std::stable_sort(ordered.begin(), ordered.end(),
                   [](const QuaderSceneDocumentInfo &left,
                      const QuaderSceneDocumentInfo &right) {
                     return left.active && !right.active;
                   });
  bool saved_any = false;
  for (const QuaderSceneDocumentInfo &scene : ordered) {
    if (!scene.dirty) {
      continue;
    }
    if (!impl_->hooks.confirm_dirty_scene) {
      return quader_api_result_from_status(
          status_for(impl_->status_session(), false, false,
                     "Unsaved scene changes require Save, Discard, or "
                     "Cancel."),
          ProjectClosePreparation{});
    }
    switch (impl_->hooks.confirm_dirty_scene(scene, action_id)) {
    case QuaderDirtySceneDecision::Cancel:
      return quader_api_result_from_status(
          status_for(impl_->status_session(), true, saved_any,
                     "Project close canceled."),
          ProjectClosePreparation{});
    case QuaderDirtySceneDecision::Save: {
      QuaderApiVoidResult saved = save(scene.session_id);
      if (!saved.ok()) {
        return quader_api_result_from_status<ProjectClosePreparation>(
            saved, ProjectClosePreparation{});
      }
      saved_any = saved_any || saved.changed();
      const OpenSceneSession *saved_scene =
          impl_->project_session->find(scene.session_id);
      if (saved_scene == nullptr || saved_scene->dirty()) {
        return quader_api_result_from_status(
            status_for(impl_->status_session(), true, saved_any,
                       "Project close canceled."),
            ProjectClosePreparation{});
      }
      break;
    }
    case QuaderDirtySceneDecision::Discard:
      break;
    }
  }
  return quader_api_result_from_status(
      status_for(impl_->status_session(), true, saved_any,
                 "Project scenes are ready to close."),
      ProjectClosePreparation{.proceed = true});
}

QuaderApiVoidResult QuaderSceneDocumentsApi::activate_next() {
  const std::vector<QuaderSceneDocumentInfo> scenes = open_scenes();
  if (scenes.empty()) {
    return status_for(impl_->status_session(), false, false,
                      "No scene is active.");
  }
  const auto active = std::find_if(scenes.begin(), scenes.end(),
                                   [](const QuaderSceneDocumentInfo &scene) {
                                     return scene.active;
                                   });
  const std::size_t index = active == scenes.end()
                                ? 0u
                                : (static_cast<std::size_t>(
                                       std::distance(scenes.begin(), active)) +
                                   1u) %
                                      scenes.size();
  return activate(scenes[index].session_id);
}

QuaderApiVoidResult QuaderSceneDocumentsApi::activate_previous() {
  const std::vector<QuaderSceneDocumentInfo> scenes = open_scenes();
  if (scenes.empty()) {
    return status_for(impl_->status_session(), false, false,
                      "No scene is active.");
  }
  const auto active = std::find_if(scenes.begin(), scenes.end(),
                                   [](const QuaderSceneDocumentInfo &scene) {
                                     return scene.active;
                                   });
  const std::size_t index =
      active == scenes.end()
          ? 0u
          : (static_cast<std::size_t>(
                 std::distance(scenes.begin(), active)) +
             scenes.size() - 1u) %
                scenes.size();
  return activate(scenes[index].session_id);
}

std::vector<QuaderSceneDocumentInfo>
QuaderSceneDocumentsApi::open_scenes() const {
  if (impl_->project_session == nullptr) {
    SceneSession &session = impl_->status_session();
    return {{
        .session_id = impl_->standalone_session_id,
        .scene_id = impl_->standalone_scene_id,
        .name = session.document().name(),
        .path = std::nullopt,
        .dirty = session.dirty(),
        .active = true,
    }};
  }
  std::vector<QuaderSceneDocumentInfo> result;
  result.reserve(impl_->project_session->open_scenes().size());
  for (const std::unique_ptr<OpenSceneSession> &scene :
       impl_->project_session->open_scenes()) {
    if (!scene) {
      continue;
    }
    result.push_back({
        .session_id = scene->session_id,
        .scene_id = scene->scene_id,
        .name = scene->display_name,
        .path = scene->path,
        .dirty = scene->dirty(),
        .active =
            scene->session_id == impl_->project_session->active_scene_id(),
    });
  }
  return result;
}

std::optional<QuaderSceneDocumentInfo>
QuaderSceneDocumentsApi::active_scene() const {
  const std::vector<QuaderSceneDocumentInfo> scenes = open_scenes();
  const auto active = std::find_if(scenes.begin(), scenes.end(),
                                   [](const QuaderSceneDocumentInfo &scene) {
                                     return scene.active;
                                   });
  return active == scenes.end()
             ? std::nullopt
             : std::optional<QuaderSceneDocumentInfo>(*active);
}

std::optional<SceneSessionId>
QuaderSceneDocumentsApi::active_scene_id() const {
  const std::optional<QuaderSceneDocumentInfo> active = active_scene();
  return active.has_value() ? std::optional(active->session_id) : std::nullopt;
}

bool QuaderSceneDocumentsApi::has_active_scene() const {
  return active_scene_id().has_value();
}

bool QuaderSceneDocumentsApi::has_dirty_scenes() const {
  const std::vector<QuaderSceneDocumentInfo> scenes = open_scenes();
  return std::any_of(scenes.begin(), scenes.end(),
                     [](const QuaderSceneDocumentInfo &scene) {
                       return scene.dirty;
                     });
}

void QuaderSceneDocumentsApi::set_runtime_hooks(
    QuaderSceneDocumentsRuntimeHooks hooks) {
  impl_->hooks = std::move(hooks);
}

void QuaderSceneDocumentsApi::set_binding_changed_callback(
    std::function<void(QuaderApiSession)> callback) {
  impl_->binding_changed = std::move(callback);
}

void QuaderSceneDocumentsApi::attach_events(QuaderEventsApi &events) {
  impl_->events = &events;
}

void QuaderSceneDocumentsApi::publish_dirty_changed() {
  impl_->publish(quader_event::kSceneDirtyChanged);
}

} // namespace quader
