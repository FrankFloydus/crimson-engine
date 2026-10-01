////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_project_api.hpp"
#include "core/user_config_paths.hpp"

#include "api/quader_api_status_internal.hpp"
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_events_api.hpp"
#include "api/quader_events_api_internal_access.hpp"
#include "api/quader_scene_documents_api.hpp"
#include "diagnostics/profile.hpp"
#include "editor/modeling/modeling_types.hpp"
#include "editor/commands/document_command_service.hpp"
#include "editor/instances/scene_instance_service.hpp"
#include "editor/material/standard_material_edit_session_service.hpp"
#include "editor/projection/mesh_projection_service.hpp"
#include "io/mesh/obj_writer.hpp"
#include "io/qdr/qdr_document_reader.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/session/project_session.hpp"
#include "editor/session/scene_viewport_camera_persistence.hpp"
#include "editor/tools/native_viewport_tool_coordinator.hpp"
#include "scene/components.hpp"
#include "scene/scene_texture_mapping.hpp"
#include "io/project/project_manifest.hpp"
#include "io/project/recent_project_store.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace quader {
namespace {

/**
 * Stores one selected entity and mesh resolved for OBJ export.
 */
struct SelectedObjExportMesh {
  EntityId entity = kInvalidEntityId;
  std::string name;
  MeshAsset mesh;
};

SceneSession &scene_session(QuaderApiSession session) {
  return QuaderApiSessionInternalAccess::scene_session(session);
}

const SceneSession &read_scene_session(QuaderApiSession session) {
  return QuaderApiSessionInternalAccess::read_scene_session(session);
}

QuaderApiVoidResult project_runtime_status(SceneSession &session,
                                           bool changed, std::string message) {
  return quader_api_void_status(session, true, changed, std::move(message),
                                QuaderMutationKind::Runtime);
}

QuaderApiResult<QuaderObjExportResult>
project_obj_export_status(SceneSession &session, bool ok,
                          std::string message,
                          QuaderObjExportResult value = {}) {
  const QuaderApiVoidResult status =
      quader_api_void_status(session, ok, false, std::move(message),
                             QuaderMutationKind::None);
  return quader_api_result_from_status(status, std::move(value));
}

std::optional<MeshAsset>
mesh_for_selected_entity(const SceneSessionReadView &view,
                         EntityId entity) {
  if (const ModelingObjectComponent *modeling_object =
          view.scene.modeling_object(entity);
      modeling_object != nullptr &&
      modeling_object->object_id != kInvalidModelingObjectId) {
    if (std::optional<MeshAsset> mesh =
            view.modeling.compile_object_mesh(modeling_object->object_id)) {
      return mesh;
    }
  }

  const MeshComponent *mesh_component = view.scene.mesh(entity);
  if (mesh_component == nullptr ||
      mesh_component->mesh == kInvalidAssetId) {
    return std::nullopt;
  }
  const MeshAsset *mesh = view.assets.mesh(mesh_component->mesh);
  if (mesh == nullptr) {
    return std::nullopt;
  }
  return *mesh;
}

std::string entity_export_name(const SceneDocument &scene, EntityId entity) {
  if (const NameComponent *name = scene.name(entity);
      name != nullptr && !name->name.empty()) {
    return name->name;
  }
  return "Entity_" + std::to_string(entity);
}

std::vector<SelectedObjExportMesh>
selected_obj_export_meshes(const SceneSession &session) {
  const SceneSessionReadView view = session.read_view();
  std::vector<SelectedObjExportMesh> result;
  if (view.modeling.selection_summary().mode !=
      ModelingSelectionMode::Object) {
    return result;
  }

  const std::vector<EntityId> selected = view.scene.selected_entities();
  const SceneInstanceEditState edit_state =
      session.instances().instance_edit_state();
  result.reserve(selected.size());
  for (const EntityId entity : selected) {
    std::optional<MeshAsset> mesh = mesh_for_selected_entity(view, entity);
    if (!mesh.has_value()) {
      continue;
    }
    const EntityId occurrence =
        edit_state.active && view.scene.containing_instance_definition(entity) ==
                                 edit_state.definition
            ? edit_state.occurrence
            : kInvalidEntityId;
    const std::optional<QMat4> mapping =
        effective_texture_sampling_from_local(view.scene, entity, occurrence);
    if (mapping.has_value()) {
      MeshProjectionService::apply_mesh_texture_mapping(*mesh, *mapping);
    }
    result.push_back(SelectedObjExportMesh{
        .entity = entity,
        .name = entity_export_name(view.scene, entity),
        .mesh = std::move(*mesh),
    });
  }
  return result;
}

bool safe_filename_character(char character) {
  return (character >= 'a' && character <= 'z') ||
         (character >= 'A' && character <= 'Z') ||
         (character >= '0' && character <= '9') || character == '_' ||
         character == '-' || character == '.';
}

std::string sanitized_filename_token(std::string_view text,
                                     std::string_view fallback) {
  std::string result;
  result.reserve(text.size());
  bool previous_separator = false;
  for (const char character : text) {
    if (safe_filename_character(character)) {
      result.push_back(character);
      previous_separator = false;
    } else if (!previous_separator) {
      result.push_back('_');
      previous_separator = true;
    }
  }

  const std::size_t first = result.find_first_not_of('_');
  if (first == std::string::npos) {
    return std::string(fallback);
  }
  const std::size_t last = result.find_last_not_of('_');
  result = result.substr(first, last - first + 1U);
  return result.empty() ? std::string(fallback) : result;
}

std::string path_collision_key(const std::filesystem::path &path) {
  std::string key = path.lexically_normal().generic_string();
#if defined(_WIN32)
  std::transform(key.begin(), key.end(), key.begin(), [](char character) {
    return character >= 'A' && character <= 'Z'
               ? static_cast<char>(character - 'A' + 'a')
               : character;
  });
#endif
  return key;
}

std::vector<std::filesystem::path> export_paths_for_selection(
    const std::filesystem::path &chosen_path,
    const std::vector<SelectedObjExportMesh> &meshes) {
  if (meshes.size() <= 1U) {
    return {chosen_path};
  }

  const std::filesystem::path parent = chosen_path.parent_path();
  const std::string base =
      sanitized_filename_token(chosen_path.stem().string(), "Selection");
  std::set<std::string> used;
  std::vector<std::filesystem::path> paths;
  paths.reserve(meshes.size());

  for (const SelectedObjExportMesh &mesh : meshes) {
    const std::string name = sanitized_filename_token(mesh.name, "Mesh");
    std::uint32_t suffix = 1U;
    while (true) {
      const std::string stem =
          suffix == 1U
              ? base + "_" + name
              : base + "_" + name + "_" + std::to_string(suffix);
      const std::filesystem::path candidate = parent / (stem + ".obj");
      if (used.insert(path_collision_key(candidate)).second) {
        paths.push_back(candidate);
        break;
      }
      ++suffix;
    }
  }
  return paths;
}

bool has_valid_obj_export_mesh(const SceneSession &session) {
  for (const SelectedObjExportMesh &mesh : selected_obj_export_meshes(session)) {
    if (ObjMeshWriter::validate_mesh(mesh.mesh).ok) {
      return true;
    }
  }
  return false;
}

std::filesystem::path default_recent_projects_path() {
  return quader_user_config_directory({}) / "recent-projects.json";
}

QuaderEvent project_lifecycle_event(std::string_view name,
                                    const ProjectSession &project) {
  QuaderEvent event{.name = std::string(name)};
  if (const ProjectManifest *manifest = project.manifest()) {
    event.project_id = manifest->id();
    event.project_name = manifest->name();
    const std::u8string root_utf8 = project.root().generic_u8string();
    event.project_root.emplace(root_utf8.begin(), root_utf8.end());
  }
  return event;
}

QuaderEvent scene_lifecycle_event(std::string_view name,
                                  const ProjectSession &project,
                                  const OpenSceneSession &scene) {
  QuaderEvent event = project_lifecycle_event(name, project);
  event.scene_session_id = scene.session_id;
  event.scene_id = scene.scene_id;
  event.scene_name = scene.display_name;
  if (scene.path.has_value()) {
    event.scene_path = scene.path->portable_string();
  }
  event.scene_dirty = scene.dirty();
  event.scene_active = scene.session_id == project.active_scene_id();
  return event;
}

/// Snapshots lifecycle events before a project releases its open scenes.
struct ProjectCloseLifecycleEvents final {
  std::vector<QuaderEvent> scenes;
  std::optional<QuaderEvent> active_changed;
  std::optional<QuaderEvent> project_closed;
};

ProjectCloseLifecycleEvents
capture_project_close_events(const ProjectSession &project) {
  ProjectCloseLifecycleEvents events;
  if (!project.has_project()) {
    return events;
  }
  events.scenes.reserve(project.open_scenes().size());
  for (const std::unique_ptr<OpenSceneSession> &scene : project.open_scenes()) {
    if (scene) {
      events.scenes.push_back(
          scene_lifecycle_event(quader_event::kSceneClosed, project, *scene));
    }
  }
  if (const OpenSceneSession *active = project.active_scene()) {
    events.active_changed = scene_lifecycle_event(
        quader_event::kActiveSceneChanged, project, *active);
    events.active_changed->scene_active = false;
  }
  events.project_closed =
      project_lifecycle_event(quader_event::kProjectClosed, project);
  return events;
}

void publish_project_close_events(QuaderEventsApi &publisher,
                                  ProjectCloseLifecycleEvents events) {
  for (QuaderEvent &event : events.scenes) {
    (void)QuaderEventsApiInternalAccess::publish(publisher, std::move(event));
  }
  if (events.active_changed.has_value()) {
    (void)QuaderEventsApiInternalAccess::publish(
        publisher, std::move(*events.active_changed));
  }
  if (events.project_closed.has_value()) {
    (void)QuaderEventsApiInternalAccess::publish(
        publisher, std::move(*events.project_closed));
  }
}

} // namespace

QuaderProjectApi::QuaderProjectApi(QuaderApiSession session)
    : session_(session) {}

QuaderProjectApi::QuaderProjectApi(ProjectSession &project_session)
    : session_(QuaderApiSessionInternalAccess::from_project_session(
          project_session)),
      project_session_(&project_session) {}

QuaderApiVoidResult
QuaderProjectApi::create(const std::filesystem::path &root_path) {
  if (project_session_ == nullptr) {
    return new_project();
  }
  std::filesystem::path target = root_path;
  if (target.empty()) {
    if (!runtime_hooks_.choose_create_root) {
      return unsupported("Project creation requires an empty folder.");
    }
    const std::optional<std::filesystem::path> selected =
        runtime_hooks_.choose_create_root();
    if (!selected.has_value()) {
      return project_runtime_status(scene_session(session_), false,
                                    "Project creation canceled.");
    }
    target = *selected;
  }
  std::error_code folder_error;
  if (!std::filesystem::is_directory(target, folder_error) || folder_error) {
    return quader_api_failure_status(
        scene_session(session_),
        "A new project requires an existing empty folder.");
  }
  const bool folder_empty = std::filesystem::is_empty(target, folder_error);
  if (folder_error || !folder_empty) {
    return quader_api_failure_status(
        scene_session(session_),
        "A new project requires a completely empty folder.");
  }
  const auto closed = close_open_scenes("project.create");
  if (!closed.status.ok() || !closed.proceed) {
    return closed.status;
  }
  ProjectCloseLifecycleEvents close_events =
      capture_project_close_events(*project_session_);
  ProjectSessionResult created = project_session_->create(target, true);
  if (!created.ok) {
    return quader_api_failure_status(scene_session(session_), created.message);
  }
  if (project_session_->active_scene() != nullptr) {
    if (binding_changed_) {
      binding_changed_(session_);
    }
  }
  if (events_ != nullptr) {
    publish_project_close_events(*events_, std::move(close_events));
  }
  remember_open_project();
  if (events_ != nullptr) {
    (void)QuaderEventsApiInternalAccess::publish(
        *events_, project_lifecycle_event(quader_event::kProjectOpened,
                                          *project_session_));
    if (const OpenSceneSession *active = project_session_->active_scene()) {
      (void)QuaderEventsApiInternalAccess::publish(
          *events_, scene_lifecycle_event(quader_event::kSceneOpened,
                                          *project_session_, *active));
      (void)QuaderEventsApiInternalAccess::publish(
          *events_, scene_lifecycle_event(quader_event::kActiveSceneChanged,
                                          *project_session_, *active));
    }
  }
  return project_runtime_status(scene_session(session_), true,
                                std::move(created.message));
}

QuaderApiVoidResult QuaderProjectApi::new_project() {
  if (project_session_ != nullptr) {
    return create({});
  }
  SceneSession &session = scene_session(session_);
  if (std::optional<QuaderApiVoidResult> blocked =
          dirty_replacement_preflight("project.new")) {
    return std::move(*blocked);
  }
  session.replace_document(SceneContentDocument{});
  path_.clear();
  return project_runtime_status(session, true, "New project created.");
}

QuaderApiVoidResult QuaderProjectApi::open(const std::filesystem::path& path) {
    std::filesystem::path target = path;
    if (target.empty()) {
      const auto &chooser = project_session_ != nullptr
                                ? runtime_hooks_.choose_open_manifest_path
                                : runtime_hooks_.choose_open_path;
      if (!chooser) {
        return unsupported("Project open requires a path.");
      }
      const std::optional<std::filesystem::path> selected = chooser();
      if (!selected.has_value()) {
        return project_runtime_status(scene_session(session_), false,
                                      "Project open canceled.");
      }
        target = *selected;
    }
    if (target.empty()) {
        return unsupported("Project open requires a path.");
    }

    if (project_session_ != nullptr) {
      const ProjectSessionResult validated = ProjectSession::preflight_open(target);
      if (!validated.ok) {
        return quader_api_failure_status(scene_session(session_), validated.message);
      }
      const auto closed = close_open_scenes("project.open");
      if (!closed.status.ok() || !closed.proceed) {
        return closed.status;
      }
      ProjectCloseLifecycleEvents close_events =
          capture_project_close_events(*project_session_);
      ProjectSessionResult opened = project_session_->open(target, true);
      if (!opened.ok) {
        return quader_api_failure_status(scene_session(session_),
                                         opened.message);
      }
      if (binding_changed_) {
        binding_changed_(session_);
      }
      if (events_ != nullptr) {
        publish_project_close_events(*events_, std::move(close_events));
      }
      remember_open_project();
      if (events_ != nullptr) {
        (void)QuaderEventsApiInternalAccess::publish(
            *events_, project_lifecycle_event(quader_event::kProjectOpened,
                                              *project_session_));
        for (const std::unique_ptr<OpenSceneSession> &scene :
             project_session_->open_scenes()) {
          if (scene) {
            (void)QuaderEventsApiInternalAccess::publish(
                *events_, scene_lifecycle_event(quader_event::kSceneOpened,
                                                *project_session_, *scene));
          }
        }
        if (const OpenSceneSession *active = project_session_->active_scene()) {
          (void)QuaderEventsApiInternalAccess::publish(
              *events_, scene_lifecycle_event(
                            quader_event::kActiveSceneChanged,
                            *project_session_, *active));
        }
      }
      std::string message = std::move(opened.message);
      if (!opened.warnings.empty()) {
        message += " Some restored scenes were skipped: ";
        for (std::size_t index = 0; index < opened.warnings.size(); ++index) {
          if (index != 0u) {
            message += "; ";
          }
          message += opened.warnings[index];
        }
      }
      return project_runtime_status(scene_session(session_), true,
                                    std::move(message));
    }

    // Invalid candidates must not settle the outgoing preview or open a prompt.
    // Read again after the decision because Save may update this exact target.
    const auto compatible = QdrDocumentReader::read_project_document(
        target, current_product_release());
    if (!compatible.ok) {
      return quader_api_failure_status(scene_session(session_), compatible.message);
    }
    if (std::optional<QuaderApiVoidResult> blocked =
            dirty_replacement_preflight("project.open")) {
      return std::move(*blocked);
    }
    QdrProjectReadResult read =
        QdrDocumentReader::read_project_document(target, current_product_release());
    if (!read.ok) {
      return quader_api_failure_status(scene_session(session_),
                                       read.message.empty()
                                           ? "Project open failed."
                                           : std::move(read.message));
    }

    SceneSession &session = scene_session(session_);
    if (!session.replace_document_with_persisted_viewport_camera(
            std::move(read.document),
            viewport_camera_pose_from_qdr(read.viewport_camera_pose))) {
      return quader_api_failure_status(
          session, "Scene viewport camera pose could not be restored.");
    }
    session.set_minimum_product_release(read.minimum_product_release);
    path_ = std::move(target);
    return project_runtime_status(session, true, "Scene opened.");
}

QuaderApiResult<QuaderObjExportResult>
QuaderProjectApi::export_selected_meshes_as_obj(
    const std::filesystem::path &path) {
  QDR_PROFILE_FUNCTION();
  SceneSession &session = scene_session(session_);
  const std::vector<SelectedObjExportMesh> meshes =
      selected_obj_export_meshes(session);
  if (meshes.empty()) {
    return project_obj_export_status(
        session, false,
        "OBJ export requires an object-mode mesh selection.");
  }

  std::filesystem::path target = path;
  if (target.empty()) {
    if (!runtime_hooks_.choose_export_obj_path) {
      return project_obj_export_status(session, false,
                                       "OBJ export requires a path.");
    }
    const std::optional<std::filesystem::path> selected =
        choose_export_obj_path();
    if (!selected.has_value()) {
      return project_obj_export_status(session, true, "OBJ export canceled.");
    }
    target = *selected;
  }
  if (target.empty()) {
    return project_obj_export_status(session, false,
                                     "OBJ export requires a path.");
  }

  QuaderObjExportResult result{
      .paths = export_paths_for_selection(target, meshes),
  };
  std::vector<ObjMeshWriteRequest> requests;
  requests.reserve(meshes.size());
  for (std::size_t index = 0; index < meshes.size(); ++index) {
    requests.push_back(ObjMeshWriteRequest{
        .path = result.paths[index],
        .mesh = &meshes[index].mesh,
        .object_name = meshes[index].name,
    });
  }
  const ObjMeshWriteResult write =
      ObjMeshWriter::write_meshes_atomic(requests);
  if (!write.ok) {
    return project_obj_export_status(session, false,
                                     write.message.empty()
                                         ? "OBJ export failed."
                                         : write.message);
  }

  return project_obj_export_status(
      session, true,
      meshes.size() == 1U ? "OBJ mesh exported." : "OBJ meshes exported.",
      std::move(result));
}

QuaderApiVoidResult QuaderProjectApi::close() {
  if (project_session_ == nullptr) {
    return new_project();
  }
  const auto scenes_closed = close_open_scenes("project.close");
  if (!scenes_closed.status.ok() || !scenes_closed.proceed) {
    return scenes_closed.status;
  }
  ProjectCloseLifecycleEvents close_events =
      capture_project_close_events(*project_session_);
  const ProjectSessionResult closed = project_session_->close(true);
  if (!closed.ok) {
    return quader_api_failure_status(scene_session(session_), closed.message);
  }
  if (closed.changed) {
    if (binding_changed_) {
      binding_changed_(session_);
    }
    if (events_ != nullptr) {
      publish_project_close_events(*events_, std::move(close_events));
    }
  }
  return project_runtime_status(scene_session(session_), closed.changed,
                                closed.message);
}

void QuaderProjectApi::set_runtime_hooks(QuaderProjectRuntimeHooks hooks) {
  runtime_hooks_ = std::move(hooks);
}

bool QuaderProjectApi::can_export_selected_meshes_as_obj() const {
  if (project_session_ != nullptr && !project_session_->has_active_scene()) {
    return false;
  }
  const SceneSession &session = read_scene_session(session_);
  return has_valid_obj_export_mesh(session);
}

bool QuaderProjectApi::has_project() const {
  return project_session_ != nullptr ? project_session_->has_project() : true;
}

bool QuaderProjectApi::dirty() const {
  if (project_session_ != nullptr) {
    return project_session_->has_dirty_scenes() ||
           project_session_->has_dirty_material();
  }
  return read_scene_session(session_).dirty();
}

const std::filesystem::path& QuaderProjectApi::path() const {
    return project_session_ != nullptr ? project_session_->manifest_path()
                                       : path_;
}

const std::filesystem::path &QuaderProjectApi::root() const {
  return project_session_ != nullptr ? project_session_->root() : empty_path_;
}

const std::filesystem::path &QuaderProjectApi::manifest_path() const {
  return project_session_ != nullptr ? project_session_->manifest_path()
                                     : empty_path_;
}

QuaderProjectMetadata QuaderProjectApi::metadata() const {
  if (project_session_ != nullptr) {
    const ProjectManifest *project_manifest = project_session_->manifest();
    return project_manifest != nullptr
               ? QuaderProjectMetadata{
                     .id = 0u,
                     .uuid = std::string(project_manifest->id().str()),
                     .name = project_manifest->name(),
                 }
               : QuaderProjectMetadata{};
  }
  const SceneSession &session = read_scene_session(session_);
  return {
      .id = session.document().id(),
      .uuid = {},
      .name = session.document().name(),
  };
}

std::vector<QuaderRecentProject> QuaderProjectApi::recent_projects() const {
  const ProjectIoResult<std::vector<RecentProjectEntry>> loaded =
      RecentProjectStore(default_recent_projects_path()).load();
  if (!loaded.ok()) {
    return {};
  }
  std::vector<QuaderRecentProject> result;
  result.reserve(loaded.value().size());
  for (const RecentProjectEntry &entry : loaded.value()) {
    result.push_back({
        .uuid = std::string(entry.project_id.str()),
        .name = entry.name,
        .manifest_path = entry.manifest_path,
        .available = entry.available,
        .manifest_last_modified_unix_ms =
            entry.manifest_last_modified_unix_ms,
        .manifest_size_bytes = entry.manifest_size_bytes,
    });
  }
  return result;
}

std::vector<QuaderRecentScene> QuaderProjectApi::recent_scenes() const {
  std::vector<QuaderRecentScene> result;
  if (project_session_ == nullptr) {
    return result;
  }
  for (const RecentSceneEntry &entry : project_session_->recent_scenes()) {
    result.push_back({
        .scene_id = entry.scene_id,
        .scene_name = entry.scene_name,
        .project_name = entry.project_name,
        .manifest_path = entry.manifest_path,
        .scene_path = entry.scene_path,
        .available = entry.available,
    });
  }
  return result;
}

QuaderApiVoidResult QuaderProjectApi::open_recent_scene(
    const QuaderRecentScene &entry) {
  if (project_session_ == nullptr || scene_documents_ == nullptr ||
      !project_session_->has_project()) {
    return unsupported("Open Recent requires an open project.");
  }
  std::error_code path_error;
  if (!std::filesystem::equivalent(entry.manifest_path, manifest_path(),
                                   path_error) || path_error) {
    return unsupported("The recent scene does not belong to the open project.");
  }
  return quader_api_status_from_result(scene_documents_->open(entry.scene_path));
}

QuaderApiVoidResult QuaderProjectApi::remove_recent_project(
    const std::filesystem::path &manifest) {
  const ProjectIoResult<void> removed =
      RecentProjectStore(default_recent_projects_path()).remove(manifest);
  return removed.ok()
             ? project_runtime_status(scene_session(session_), true,
                                      "Recent project removed.")
             : quader_api_failure_status(scene_session(session_),
                                         removed.error().message);
}

void QuaderProjectApi::attach_scene_documents(
    QuaderSceneDocumentsApi &documents) {
  scene_documents_ = &documents;
}

void QuaderProjectApi::attach_events(QuaderEventsApi &events) {
  events_ = &events;
}

void QuaderProjectApi::set_binding_changed_callback(
    std::function<void(QuaderApiSession)> callback) {
  binding_changed_ = std::move(callback);
}

QuaderProjectApi::SceneClosePreparationResult
QuaderProjectApi::close_open_scenes(std::string_view action_id) {
  if (project_session_ == nullptr || !project_session_->has_project()) {
    return {
        .status = project_runtime_status(scene_session(session_), false, {}),
        .proceed = true,
    };
  }
  if (scene_documents_ == nullptr) {
    const bool dirty = project_session_->has_dirty_scenes() ||
                       project_session_->has_dirty_material();
    return {
        .status = dirty
                      ? quader_api_failure_status(
                            scene_session(session_),
                            "Dirty scenes require the scene-document close "
                            "flow.")
                      : project_runtime_status(scene_session(session_), false,
                                               {}),
        .proceed = !dirty,
    };
  }
  const auto prepared = scene_documents_->prepare_project_close(action_id);
  if (prepared.value().proceed) {
    const auto material = project_session_->standard_material_editor()
                              .accept_base_color_preview();
    if (!material.ok) {
      return {
          .status = quader_api_failure_status(scene_session(session_),
                                              material.message),
          .proceed = false,
      };
    }
  }
  if (prepared.value().proceed && project_session_->has_dirty_material()) {
    if (runtime_hooks_.prepare_material_document_close &&
        runtime_hooks_.prepare_material_document_close(action_id) &&
        !project_session_->has_dirty_material()) {
      return {
          .status = quader_api_status_from_result(prepared),
          .proceed = true,
      };
    }
    return {
        .status = quader_api_failure_status(
            scene_session(session_),
            runtime_hooks_.prepare_material_document_close
                ? "Project replacement was canceled by the Standard "
                  "Material editor."
                : "A dirty Standard Material draft must be saved or "
                  "discarded before replacing the project."),
        .proceed = false,
    };
  }
  return {
      .status = quader_api_status_from_result(prepared),
      .proceed = prepared.value().proceed,
  };
}

void QuaderProjectApi::remember_open_project() {
  if (project_session_ == nullptr || project_session_->manifest() == nullptr) {
    return;
  }
  (void)RecentProjectStore(default_recent_projects_path())
      .remember(*project_session_->manifest(), project_session_->manifest_path());
}

QuaderApiVoidResult QuaderProjectApi::unsupported(std::string message) const {
  return quader_api_failure_status(read_scene_session(session_),
                                   std::move(message));
}

std::optional<std::filesystem::path>
QuaderProjectApi::choose_export_obj_path() const {
  return runtime_hooks_.choose_export_obj_path
             ? runtime_hooks_.choose_export_obj_path()
             : std::nullopt;
}

std::optional<QuaderApiVoidResult>
QuaderProjectApi::dirty_replacement_preflight(std::string_view action_id) {
  SceneSession &session = scene_session(session_);
  const CommandResult property =
      session.document_commands().accept_active_property_edit();
  if (!property.ok) return quader_api_failure_status(session, property.message);
  const CommandResult settled = session.native_tools().settle_transient_state();
  if (!settled.ok) return quader_api_failure_status(session, settled.message);
  if (!session.dirty()) {
    return std::nullopt;
  }
  if (!runtime_hooks_.confirm_dirty_project_replacement) {
    return quader_api_failure_status(
        session, "Unsaved project changes require Save, Discard, or Cancel.");
  }

  switch (runtime_hooks_.confirm_dirty_project_replacement(action_id)) {
  case QuaderDirtyProjectDecision::Discard:
    return std::nullopt;
  case QuaderDirtyProjectDecision::Cancel:
    return project_runtime_status(session, false,
                                  "Project replacement canceled.");
  case QuaderDirtyProjectDecision::Save: {
    if (scene_documents_ == nullptr) {
      return quader_api_failure_status(
          session, "Scene document saving is unavailable.");
    }
    QuaderApiVoidResult saved = scene_documents_->save_active();
    if (!saved.ok()) {
      return saved;
    }
    if (session.dirty()) {
      return project_runtime_status(session, false,
                                    "Project replacement canceled.");
    }
    return std::nullopt;
  }
  }
  return project_runtime_status(session, false,
                                "Project replacement canceled.");
}

} // namespace quader
