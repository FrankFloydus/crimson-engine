////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/material/project_material_usage_service.hpp"

#include "assets/asset_registry.hpp"
#include "diagnostics/profile.hpp"
#include "editor/session/project_session.hpp"
#include "io/qdr/qdr_document_reader.hpp"
#include "scene/scene_material_surface.hpp"

#include <quader/modeling/neutral/polygon/document.hpp>
#include <quader/modeling/curves/curve_document.hpp>

#include <algorithm>
#include <map>
#include <set>
#include <system_error>
#include <utility>

namespace quader {
namespace {

/// Excludes selection, camera and visibility changes from surface inspection.
struct UsageStamp final {
  std::uint64_t mesh_bindings = 0u;
  std::uint64_t curve_bindings = 0u;
  std::uint64_t material_bindings = 0u;
  std::uint64_t assets = 0u;

  friend bool operator==(const UsageStamp &, const UsageStamp &) = default;
};

UsageStamp usage_stamp(const SceneSession &session) {
  return {
      .mesh_bindings = session.scene().mesh_binding_revision(),
      .curve_bindings = session.scene().curve_binding_revision(),
      .material_bindings = session.scene().material_binding_revision(),
      .assets = session.assets().revision(),
  };
}

/// Pins the exact source checked, without retaining a mutable document owner.
struct GeometrySlots final {
  std::uint64_t inspection_pass = 0u;
  std::optional<MeshAsset> resident_source;
  MeshAssetWriteSource source;
  QdrMeshMaterialSlotsReadResult read;
};

bool same_material_slot_source(const MeshAsset &left, const MeshAsset &right) {
  const bool left_authored = left.has_authored_polygon_document &&
                            left.authored_polygon_document;
  const bool right_authored = right.has_authored_polygon_document &&
                             right.authored_polygon_document;
  if (left_authored != right_authored) return false;
  if (left_authored) {
    const auto left_faces = left.authored_polygon_document.read_view().faces;
    const auto right_faces = right.authored_polygon_document.read_view().faces;
    return left_faces.data() == right_faces.data() &&
           left_faces.size() == right_faces.size();
  }
  const auto left_primitives = left.geometry_primitives();
  const auto right_primitives = right.geometry_primitives();
  return left_primitives.data() == right_primitives.data() &&
         left_primitives.size() == right_primitives.size() &&
         left.geometry_indices().size() == right.geometry_indices().size();
}

QdrMeshMaterialSlotsReadResult inspect_material_slots(const MeshAsset &source) {
  QdrMeshMaterialSlotsReadResult result{.ok = true};
  if (source.has_authored_polygon_document && source.authored_polygon_document) {
    for (const auto &face : source.authored_polygon_document.read_view().faces) {
      result.material_slots.push_back(face.material_slot);
    }
  } else {
    for (const auto &primitive : normalized_mesh_primitive_spans(source)) {
      result.material_slots.push_back(primitive.material_slot);
    }
  }
  std::ranges::sort(result.material_slots);
  result.material_slots.erase(
      std::unique(result.material_slots.begin(), result.material_slots.end()),
      result.material_slots.end());
  return result;
}

QdrMeshMaterialSlotsReadResult inspect_material_slots(
    const MeshAssetWriteSource &source) {
  if (source.encoded_geometry) {
    return QdrDocumentReader::deserialize_mesh_material_slots(
        *source.encoded_geometry);
  }
  QdrMeshMaterialSlotsReadResult result{.ok = true};
  if (source.descriptor.has_authored_polygon_document &&
      source.authored_polygon_document) {
    for (const auto &face : source.authored_polygon_document->faces) {
      result.material_slots.push_back(face.material_slot);
    }
  } else if (source.geometry) {
    for (const auto &primitive :
         normalized_mesh_primitive_spans(*source.geometry)) {
      result.material_slots.push_back(primitive.material_slot);
    }
  } else {
    return {.message = "Mesh geometry is unavailable."};
  }
  std::ranges::sort(result.material_slots);
  result.material_slots.erase(
      std::unique(result.material_slots.begin(), result.material_slots.end()),
      result.material_slots.end());
  return result;
}

/// Used only when a source is first requested or file invalidation is explicit.
struct FileStamp final {
  std::filesystem::path resolved_path;
  std::uintmax_t byte_count = 0u;
  std::filesystem::file_time_type modified;

  friend bool operator==(const FileStamp &, const FileStamp &) = default;
};

std::optional<FileStamp> inspect_file(const ProjectFileSystem &file_system,
                                    const ProjectRelativePath &path,
                                    std::string &message) {
  const auto resolved = file_system.resolve_existing_file(path);
  if (!resolved.ok()) {
    message = resolved.error().message;
    return std::nullopt;
  }
  std::error_code error;
  const auto size = std::filesystem::file_size(resolved.value(), error);
  if (error) {
    message = error.message();
    return std::nullopt;
  }
  const auto modified = std::filesystem::last_write_time(resolved.value(), error);
  if (error) {
    message = error.message();
    return std::nullopt;
  }
  return FileStamp{resolved.value(), size, modified};
}

template <typename T> void sort_unique(std::vector<T> &values) {
  std::ranges::sort(values);
  values.erase(std::unique(values.begin(), values.end()), values.end());
}

bool same_snapshot(const ProjectMaterialUsageSnapshot &left,
                   const ProjectMaterialUsageSnapshot &right) {
  return left.project_id == right.project_id &&
         left.project_generation == right.project_generation &&
         left.complete == right.complete &&
         left.material_ids == right.material_ids &&
         left.active_scene_embedded_material_ids ==
             right.active_scene_embedded_material_ids &&
         left.issues == right.issues;
}

} // namespace

/// Keeps derived source projections; project and scene owners remain authoritative.
struct ProjectMaterialUsageService::State final {
  /// Cached bindings and lifetime-pinned geometry sources for one resident scene.
  struct LiveScene final {
    SceneSessionId session_id;
    SceneId scene_id;
    const SceneSession *session = nullptr;
    std::optional<ProjectRelativePath> path;
    std::string name;
    UsageStamp stamp;
    std::vector<ProjectAssetId> material_ids;
    std::vector<AssetId> embedded_material_ids;
    std::vector<std::string> errors;
    std::map<AssetId, GeometrySlots> meshes;
  };

  /// One validated file projection, including stable inspection failures.
  struct SavedScene final {
    std::optional<FileStamp> stamp;
    QdrProjectMaterialUsageReadResult read;
    bool inspected = false;
  };

  explicit State(ProjectSession &owner) : project(owner) {}

  bool owners_match() const {
    if (invalidated || !retained || root != project.root() ||
        retained->project_generation != project.activation_generation()) {
      return false;
    }
    const auto *manifest = project.manifest();
    if (manifest == nullptr) return !retained->project_id.valid();
    if (retained->project_id != manifest->id() ||
        !std::ranges::equal(scenes, manifest->scenes()) ||
        !std::ranges::equal(deleted, manifest->deleted_material_ids()) ||
        project.open_scenes().size() != live.size()) {
      return false;
    }
    for (std::size_t index = 0u; index < live.size(); ++index) {
      const auto &open = *project.open_scenes()[index];
      const auto &cached = live[index];
      if (open.session_id != cached.session_id ||
          open.scene_id != cached.scene_id || open.session.get() != cached.session ||
          open.path != cached.path || open.display_name != cached.name ||
          (open.session && usage_stamp(*open.session) != cached.stamp)) {
        return false;
      }
    }
    return true;
  }

  const QdrMeshMaterialSlotsReadResult &slots_for(LiveScene &cached,
                                                const AssetRegistry &assets,
                                                AssetId mesh_id) {
    auto &mesh = cached.meshes[mesh_id];
    if (mesh.inspection_pass == work.live_scene_scan_count) return mesh.read;
    mesh.inspection_pass = work.live_scene_scan_count;
    if (assets.mesh_materialized(mesh_id)) {
      const auto *resident = assets.mesh(mesh_id);
      if (resident == nullptr) {
        mesh.read = {.message = "Mesh geometry is unavailable."};
        return mesh.read;
      }
      if (mesh.read.ok && mesh.resident_source &&
          (mesh.resident_source->revision == resident->revision ||
           same_material_slot_source(*mesh.resident_source, *resident))) {
        mesh.resident_source = *resident;
        return mesh.read;
      }
      mesh.resident_source = *resident;
      mesh.source = {};
      ++work.geometry_slot_scan_count;
      mesh.read = inspect_material_slots(*resident);
      return mesh.read;
    }
    mesh.resident_source.reset();
    const auto source = assets.mesh_write_source(mesh_id);
    if (!source.ok) {
      mesh.read = {.message = source.message};
      return mesh.read;
    }
    // A new logical asset revision can still share the same immutable payload.
    if (mesh.read.ok && mesh.source.descriptor.id != kInvalidAssetId &&
        mesh.source.geometry == source.source.geometry &&
        mesh.source.authored_polygon_document == source.source.authored_polygon_document &&
        mesh.source.encoded_geometry == source.source.encoded_geometry &&
        mesh.source.descriptor.has_authored_polygon_document ==
            source.source.descriptor.has_authored_polygon_document) {
      mesh.source = source.source;
      return mesh.read;
    }
    mesh.source = source.source;
    ++work.geometry_slot_scan_count;
    mesh.read = inspect_material_slots(mesh.source);
    return mesh.read;
  }

  void inspect_live(LiveScene &cached, const SceneSession &session) {
    QDR_PROFILE_SCOPE("Project material usage inspect live scene");
    ++work.live_scene_scan_count;
    cached.material_ids.clear();
    cached.embedded_material_ids.clear();
    cached.errors.clear();
    const auto add = [&](MaterialReference reference) {
      if (reference.project_owned()) cached.material_ids.push_back(reference.project_asset());
      else if (reference.embedded()) cached.embedded_material_ids.push_back(reference.embedded_asset());
    };
    std::set<AssetId> used_meshes;
    for (const EntityId entity : session.scene().entities()) {
      const auto *material = session.scene().material(entity);
      if (material == nullptr) continue;
      if (scene_entity_has_solid_curve_surface(session.scene(), entity)) {
        const auto *curve = session.assets().curve(session.scene().curve(entity)->curve);
        if (curve == nullptr || !curve->document) {
          cached.errors.push_back("A solid curve asset is unavailable.");
        } else if (curve->document->segment_count() != 0u) {
          add(material->material_for_slot(0u));
        }
        continue;
      }
      const auto *binding = session.scene().mesh(entity);
      if (binding == nullptr) continue;
      used_meshes.insert(binding->mesh);
      const auto &slots = slots_for(cached, session.assets(), binding->mesh);
      if (!slots.ok) {
        cached.errors.push_back(slots.message);
        continue;
      }
      for (const auto slot : slots.material_slots) add(material->material_for_slot(slot));
    }
    std::erase_if(cached.meshes, [&](const auto &entry) {
      return !used_meshes.contains(entry.first);
    });
    sort_unique(cached.material_ids);
    sort_unique(cached.embedded_material_ids);
    sort_unique(cached.errors);
  }

  void add_live(ProjectMaterialUsageSnapshot &candidate, bool retry_failed) {
    std::vector<LiveScene> next;
    next.reserve(project.open_scenes().size());
    for (const auto &open_pointer : project.open_scenes()) {
      const auto &open = *open_pointer;
      const auto existing = std::ranges::find(live, open.session_id,
                                              &LiveScene::session_id);
      LiveScene cached;
      if (existing != live.end() && existing->session == open.session.get()) {
        cached = std::move(*existing);
      }
      const auto stamp = open.session ? usage_stamp(*open.session) : UsageStamp{};
      const bool reuse = cached.session != nullptr && cached.stamp == stamp &&
                         (!retry_failed || cached.errors.empty());
      cached.session_id = open.session_id;
      cached.scene_id = open.scene_id;
      cached.session = open.session.get();
      cached.path = open.path;
      cached.name = open.display_name;
      cached.stamp = stamp;
      if (!open.session) {
        cached.errors = {"The live scene document is unavailable."};
      } else if (!reuse) {
        inspect_live(cached, *open.session);
      }
      candidate.material_ids.insert(candidate.material_ids.end(),
                                     cached.material_ids.begin(), cached.material_ids.end());
      if (open.session_id == project.active_scene_id()) {
        candidate.active_scene_embedded_material_ids = cached.embedded_material_ids;
      }
      for (const auto &error : cached.errors) {
        candidate.issues.push_back({cached.scene_id, cached.session_id,
            cached.path ? cached.path->portable_string() : std::string{}, cached.name, error});
      }
      next.push_back(std::move(cached));
    }
    live = std::move(next);
  }

  void add_saved(ProjectMaterialUsageSnapshot &candidate, bool inspect_files,
                 bool force_read) {
    std::set<ProjectRelativePath> retained_paths;
    for (const auto &entry : project.manifest()->scenes()) {
      retained_paths.insert(entry.path);
      if (std::ranges::any_of(project.open_scenes(), [&](const auto &open) {
            return open->scene_id == entry.id || open->path == entry.path;
          })) {
        // Never union stale saved assignments with a resident document.
        saved.erase(entry.path);
        continue;
      }
      auto &cached = saved[entry.path];
      if (inspect_files || !cached.inspected) {
        ++work.file_stamp_read_count;
        std::string message;
        const auto stamp = inspect_file(project.workspace()->file_system, entry.path, message);
        if (!stamp) {
          cached.read = {.message = std::move(message)};
          cached.stamp.reset();
        } else if (force_read || !cached.inspected || cached.stamp != stamp) {
          ++work.saved_scene_read_count;
          cached.read = QdrDocumentReader::read_project_material_usage(stamp->resolved_path);
          cached.stamp = stamp;
        }
        cached.inspected = true;
      }
      const auto &read = cached.read;
      std::string error;
      if (!read.ok || !read.complete) error = read.message;
      else if (SceneId::from_bytes(read.scene_uuid) != entry.id) {
        error = "Scene identity differs from the project manifest.";
      } else {
        candidate.material_ids.insert(candidate.material_ids.end(),
                                       read.material_ids.begin(), read.material_ids.end());
      }
      if (!error.empty() || !read.ok || !read.complete) {
        candidate.issues.push_back({entry.id, std::nullopt,
            entry.path.portable_string(), read.scene_name,
            error.empty() ? "The scene could not be completely inspected." : std::move(error)});
      }
    }
    std::erase_if(saved, [&](const auto &entry) {
      return !retained_paths.contains(entry.first);
    });
  }

  std::shared_ptr<const ProjectMaterialUsageSnapshot> build(bool refresh_files) {
    if (!refresh_files && owners_match()) return retained;
    QDR_PROFILE_SCOPE("Project material usage rebuild snapshot");
    ++work.snapshot_build_count;
    ProjectMaterialUsageSnapshot candidate{
        .project_generation = project.activation_generation(),
    };
    const auto *manifest = project.manifest();
    if (manifest == nullptr || project.workspace() == nullptr) {
      live.clear();
      saved.clear();
      scenes.clear();
      deleted.clear();
    } else {
      candidate.project_id = manifest->id();
      if (!retained || retained->project_id != candidate.project_id || root != project.root()) {
        live.clear();
        saved.clear();
      }
      add_saved(candidate, refresh_files || invalidated, refresh_files);
      add_live(candidate, refresh_files || invalidated);
      for (auto &id : candidate.material_ids) id = project.canonical_material_id(id);
      sort_unique(candidate.material_ids);
      candidate.complete = candidate.issues.empty();
      scenes.assign(manifest->scenes().begin(), manifest->scenes().end());
      deleted.assign(manifest->deleted_material_ids().begin(), manifest->deleted_material_ids().end());
    }
    root = project.root();
    invalidated = false;
    if (retained && same_snapshot(candidate, *retained)) return retained;
    candidate.revision = ++revision;
    retained = std::make_shared<const ProjectMaterialUsageSnapshot>(std::move(candidate));
    return retained;
  }

  ProjectSession &project;
  ProjectMaterialUsageWorkStats work;
  std::shared_ptr<const ProjectMaterialUsageSnapshot> retained;
  std::filesystem::path root;
  std::vector<ProjectSceneCatalogEntry> scenes;
  std::vector<ProjectAssetId> deleted;
  std::vector<LiveScene> live;
  std::map<ProjectRelativePath, SavedScene> saved;
  bool invalidated = true;
  std::uint64_t revision = 0u;
};

ProjectMaterialUsageService::ProjectMaterialUsageService(ProjectSession &project)
    : state_(std::make_unique<State>(project)) {}
ProjectMaterialUsageService::~ProjectMaterialUsageService() = default;

std::shared_ptr<const ProjectMaterialUsageSnapshot>
ProjectMaterialUsageService::snapshot(bool refresh_files) const {
  QDR_PROFILE_SCOPE("Project material usage snapshot");
  return state_->build(refresh_files);
}

void ProjectMaterialUsageService::invalidate() noexcept { state_->invalidated = true; }

const ProjectMaterialUsageWorkStats &ProjectMaterialUsageService::work_stats() const noexcept {
  return state_->work;
}

} // namespace quader
