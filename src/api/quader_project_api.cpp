////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_project_api.hpp"

#include "api/quader_api_status_internal.hpp"
#include "api/quader_api_session_internal_access.hpp"
#include "diagnostics/profile.hpp"
#include "editor/modeling/modeling_types.hpp"
#include "io/mesh/obj_writer.hpp"
#include "io/qdr/qdr_document_reader.hpp"
#include "io/qdr/qdr_document_writer.hpp"
#include "editor/session/project_session.hpp"
#include "scene/components.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <string_view>
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

ProjectSession &project_session(QuaderApiSession session) {
  return QuaderApiSessionInternalAccess::project_session(session);
}

const ProjectSession &read_project_session(QuaderApiSession session) {
  return QuaderApiSessionInternalAccess::read_project_session(session);
}

QuaderApiVoidResult project_runtime_status(ProjectSession &session,
                                           bool changed, std::string message) {
  return quader_api_void_status(session, true, changed, std::move(message),
                                QuaderMutationKind::Runtime);
}

QuaderApiResult<QuaderObjExportResult>
project_obj_export_status(ProjectSession &session, bool ok,
                          std::string message,
                          QuaderObjExportResult value = {}) {
  const QuaderApiVoidResult status =
      quader_api_void_status(session, ok, false, std::move(message),
                             QuaderMutationKind::None);
  return quader_api_result_from_status(status, std::move(value));
}

std::optional<MeshAsset>
mesh_for_selected_entity(const ProjectSessionReadView &view,
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
selected_obj_export_meshes(const ProjectSessionReadView &view) {
  std::vector<SelectedObjExportMesh> result;
  if (view.modeling.selection_summary().mode !=
      ModelingSelectionMode::Object) {
    return result;
  }

  const std::vector<EntityId> selected = view.scene.selected_entities();
  result.reserve(selected.size());
  for (const EntityId entity : selected) {
    std::optional<MeshAsset> mesh = mesh_for_selected_entity(view, entity);
    if (!mesh.has_value()) {
      continue;
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

bool has_valid_obj_export_mesh(const ProjectSessionReadView &view) {
  for (const SelectedObjExportMesh &mesh : selected_obj_export_meshes(view)) {
    if (ObjMeshWriter::validate_mesh(mesh.mesh).ok) {
      return true;
    }
  }
  return false;
}

} // namespace

QuaderProjectApi::QuaderProjectApi(QuaderApiSession session)
    : session_(session) {}

QuaderApiVoidResult QuaderProjectApi::new_project() {
  ProjectSession &session = project_session(session_);
  session.replace_document(ProjectDocument{});
  path_.clear();
  return project_runtime_status(session, true, "New project created.");
}

QuaderApiVoidResult QuaderProjectApi::open(const std::filesystem::path& path) {
    std::filesystem::path target = path;
    if (target.empty()) {
      if (!runtime_hooks_.choose_open_path) {
        return unsupported("Project open requires a path.");
      }
      const std::optional<std::filesystem::path> selected = choose_open_path();
      if (!selected.has_value()) {
        return project_runtime_status(project_session(session_), false,
                                      "Project open canceled.");
      }
        target = *selected;
    }
    if (target.empty()) {
        return unsupported("Project open requires a path.");
    }

    QdrProjectReadResult read =
        QdrDocumentReader::read_project_document(target);
    if (!read.ok) {
      return quader_api_failure_status(project_session(session_),
                                       read.message.empty()
                                           ? "Project open failed."
                                           : std::move(read.message));
    }

    ProjectSession &session = project_session(session_);
    session.replace_document(std::move(read.document));
    path_ = std::move(target);
    return project_runtime_status(session, true,
                                  read.migrated_legacy_session
                                      ? "Legacy QDR project opened."
                                      : "Project opened.");
}

QuaderApiVoidResult QuaderProjectApi::save() {
    QDR_PROFILE_FUNCTION();
    if (path_.empty()) {
      return save_as({});
    }
    return save_to_path(path_, false);
}

QuaderApiVoidResult
QuaderProjectApi::save_as(const std::filesystem::path &path) {
  QDR_PROFILE_FUNCTION();
  std::filesystem::path target = path;
  if (target.empty()) {
    if (!runtime_hooks_.choose_save_as_path) {
      return unsupported("Project save-as requires a path.");
    }
    const std::optional<std::filesystem::path> selected = choose_save_as_path();
    if (!selected.has_value()) {
      return project_runtime_status(project_session(session_), false,
                                    "Project save-as canceled.");
    }
    target = *selected;
  }
  if (target.empty()) {
    return unsupported("Project save-as requires a path.");
  }

  return save_to_path(target, true);
}

QuaderApiResult<QuaderObjExportResult>
QuaderProjectApi::export_selected_meshes_as_obj(
    const std::filesystem::path &path) {
  QDR_PROFILE_FUNCTION();
  ProjectSession &session = project_session(session_);
  const std::vector<SelectedObjExportMesh> meshes =
      selected_obj_export_meshes(session.read_view());
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

  for (const SelectedObjExportMesh &mesh : meshes) {
    const ObjMeshWriteResult validation =
        ObjMeshWriter::validate_mesh(mesh.mesh);
    if (!validation.ok) {
      return project_obj_export_status(session, false,
                                       validation.message.empty()
                                           ? "OBJ export failed."
                                           : validation.message);
    }
  }

  QuaderObjExportResult result{
      .paths = export_paths_for_selection(target, meshes),
  };
  for (std::size_t index = 0; index < meshes.size(); ++index) {
    const ObjMeshWriteResult write =
        ObjMeshWriter::write_mesh(result.paths[index], meshes[index].mesh,
                                  meshes[index].name);
    if (!write.ok) {
      return project_obj_export_status(session, false,
                                       write.message.empty()
                                           ? "OBJ export failed."
                                           : write.message);
    }
  }

  return project_obj_export_status(
      session, true,
      meshes.size() == 1U ? "OBJ mesh exported." : "OBJ meshes exported.",
      std::move(result));
}

QuaderApiVoidResult QuaderProjectApi::close() { return new_project(); }

void QuaderProjectApi::set_runtime_hooks(QuaderProjectRuntimeHooks hooks) {
  runtime_hooks_ = std::move(hooks);
}

bool QuaderProjectApi::can_export_selected_meshes_as_obj() const {
  const ProjectSession &session = read_project_session(session_);
  return has_valid_obj_export_mesh(session.read_view());
}

bool QuaderProjectApi::dirty() const {
  return read_project_session(session_).dirty();
}

const std::filesystem::path& QuaderProjectApi::path() const {
    return path_;
}

QuaderProjectMetadata QuaderProjectApi::metadata() const {
  const ProjectSession &session = read_project_session(session_);
  return {
      .id = session.document().id(),
      .name = session.document().name(),
  };
}

QuaderApiVoidResult QuaderProjectApi::unsupported(std::string message) const {
  return quader_api_failure_status(read_project_session(session_),
                                   std::move(message));
}

QuaderApiVoidResult
QuaderProjectApi::save_to_path(const std::filesystem::path &path,
                               bool update_path) {
  QDR_PROFILE_FUNCTION();
  if (path.empty()) {
    return unsupported("Project save requires a path.");
  }

  ProjectSession &session = project_session(session_);
  QdrProjectWriteResult write = [&]() {
    QDR_PROFILE_SCOPE("Project write QDR document");
    return QdrDocumentWriter::write_project_document_atomic(path,
                                                            session.document());
  }();
  if (!write.ok) {
    return quader_api_failure_status(session, write.message.empty()
                                                  ? "Project save failed."
                                                  : std::move(write.message));
  }

  if (update_path) {
    path_ = path;
  }
  session.mark_clean();
  return project_runtime_status(session, true, "Project saved.");
}

std::optional<std::filesystem::path>
QuaderProjectApi::choose_open_path() const {
  return runtime_hooks_.choose_open_path ? runtime_hooks_.choose_open_path()
                                         : std::nullopt;
}

std::optional<std::filesystem::path>
QuaderProjectApi::choose_save_as_path() const {
  return runtime_hooks_.choose_save_as_path
             ? runtime_hooks_.choose_save_as_path()
             : std::nullopt;
}

std::optional<std::filesystem::path>
QuaderProjectApi::choose_export_obj_path() const {
  return runtime_hooks_.choose_export_obj_path
             ? runtime_hooks_.choose_export_obj_path()
             : std::nullopt;
}

} // namespace quader
