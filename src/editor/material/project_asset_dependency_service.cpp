////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/material/project_asset_dependency_service.hpp"

#include "editor/material/standard_material_edit_session_service.hpp"
#include "editor/session/project_session.hpp"
#include "io/project/project_material_reference_resolver.hpp"
#include "io/qdr/qdr_binary_container.hpp"
#include "io/qdr/qdr_document_reader.hpp"
#include "io/shared_file_read.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string_view>
#include <system_error>
#include <utility>

namespace quader {
namespace {

constexpr std::uint64_t kHashSeed = 14'695'981'039'346'656'037ull;

std::uint64_t append_hash(std::uint64_t hash, std::uint64_t value) {
  for (std::size_t offset = 0u; offset < sizeof(value); ++offset) {
    hash ^= static_cast<unsigned char>(value >> (offset * 8u));
    hash *= 1'099'511'628'211ull;
  }
  return hash;
}

std::uint64_t stamp_hash(SceneContentRevisionStamp stamp) {
  auto hash = append_hash(kHashSeed, stamp.project_revision);
  hash = append_hash(hash, stamp.scene_revision);
  hash = append_hash(hash, stamp.asset_revision);
  return append_hash(hash, stamp.modeling_content_revision);
}

template <typename T>
bool equal_values(std::span<const T> left, std::span<const T> right) {
  return std::ranges::equal(left, right);
}

void canonicalize_material_ids(std::vector<ProjectAssetId> &ids,
                               const ProjectManifest &manifest) {
  for (ProjectAssetId &id : ids) {
    id = ProjectMaterialReferenceResolver::canonical_material_id(manifest, id);
  }
  std::ranges::sort(ids);
  ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
}

std::vector<ProjectAssetId> stored_material_ids(const SceneDocument &scene) {
  std::set<ProjectAssetId> ids;
  const auto add_reference = [&](const MaterialReference &reference) {
    if (reference.project_owned()) {
      ids.insert(reference.project_asset());
    }
  };
  // This intentionally includes definitions, hidden objects and dormant slots.
  for (const EntityId entity : scene.entities()) {
    if (const MaterialComponent *material = scene.material(entity)) {
      add_reference(material->material);
      for (const auto &[slot, reference] : material->material_slots) {
        (void)slot;
        add_reference(reference);
      }
    }
  }
  return {ids.begin(), ids.end()};
}

bool same_snapshot(const ProjectAssetDependencySnapshot &left,
                   const ProjectAssetDependencySnapshot &right) {
  return left.project_id == right.project_id &&
         left.project_generation == right.project_generation &&
         left.catalog_revision == right.catalog_revision &&
         left.complete == right.complete && left.errors == right.errors &&
         left.scenes == right.scenes && left.materials == right.materials &&
         left.sources == right.sources;
}

/// A file stamp is only a cache hint; destructive refresh always hashes bytes.
struct FileStamp final {
  std::filesystem::path resolved_path;
  std::uintmax_t byte_count = 0u;
  std::filesystem::file_time_type modified;

  friend bool operator==(const FileStamp &, const FileStamp &) = default;
};

std::optional<FileStamp> inspect_stamp(const ProjectFileSystem &file_system,
                                      const ProjectRelativePath &path,
                                      std::string &error) {
  const auto resolved = file_system.resolve_existing_file(path);
  if (!resolved.ok()) {
    error = resolved.error().message;
    return std::nullopt;
  }
  std::error_code io_error;
  const auto byte_count = std::filesystem::file_size(resolved.value(), io_error);
  if (io_error) {
    error = io_error.message();
    return std::nullopt;
  }
  const auto modified =
      std::filesystem::last_write_time(resolved.value(), io_error);
  if (io_error) {
    error = io_error.message();
    return std::nullopt;
  }
  return FileStamp{resolved.value(), byte_count, modified};
}

ProjectMaterialAssetDependencies material_dependencies(
    const ProjectMaterialDocument &document,
    std::optional<ProjectRelativePath> path, std::uint64_t content_revision,
    bool draft, bool dirty,
    const std::array<std::optional<std::filesystem::path>, 4> *pending,
    const ProjectFileSystem &file_system, std::vector<std::string> &errors) {
  ProjectMaterialAssetDependencies result{
      .id = document.id,
      .path = std::move(path),
      .name = document.name,
      .draft = draft,
      .dirty = dirty,
      .content_revision = content_revision,
  };
  const auto *standard = document.standard_material();
  if (standard == nullptr) {
    errors.push_back("Cannot inspect dependencies of material '" +
                     document.name + "': unsupported material type.");
    return result;
  }
  constexpr std::array kSlots{
      StandardMaterialTextureSlot::Albedo,
      StandardMaterialTextureSlot::Roughness,
      StandardMaterialTextureSlot::Metallic,
      StandardMaterialTextureSlot::Normal,
  };
  for (std::size_t index = 0u; index < kSlots.size(); ++index) {
    const auto &saved = *standard->texture(kSlots[index]);
    const bool has_pending = pending != nullptr && (*pending)[index].has_value();
    if (!saved.has_value() && !has_pending) {
      continue;
    }
    ProjectMaterialTextureDependency dependency{.slot = kSlots[index]};
    if (saved.has_value()) {
      dependency.texture_id = saved->id;
      auto relative = ProjectRelativePath::parse_portable(
          saved->project_relative_path);
      if (!relative.ok()) {
        errors.push_back(document.name + ": " + relative.error().message);
      } else {
        dependency.path = relative.take_value();
        const auto resolved = file_system.resolve(*dependency.path);
        if (!resolved.ok()) {
          errors.push_back(document.name + ": " + resolved.error().message);
        }
      }
    }
    if (has_pending) {
      dependency.pending_source = (*pending)[index];
    }
    result.textures.push_back(std::move(dependency));
  }
  return result;
}

} // namespace

/// Keeps only derived edges and validated source versions between requests.
struct ProjectAssetDependencyService::State final {
  /// Owner revisions are checked before allocating or walking entity storage.
  struct LiveSceneCache final {
    SceneSessionId session_id;
    SceneId scene_id;
    const SceneSession *session = nullptr;
    std::optional<ProjectRelativePath> path;
    std::string display_name;
    SceneContentRevisionStamp stamp;
    bool dirty = false;
    std::vector<ProjectAssetId> material_ids;
  };

  /// One metadata read can serve multiple unrelated live-document revisions.
  struct SavedSceneCache final {
    std::optional<FileStamp> stamp;
    QdrProjectDependencyReadResult dependencies;
  };

  explicit State(ProjectSession &owner) : project(owner) {}

  [[nodiscard]] bool owners_match() const {
    if (invalidated || !retained || project.root() != root ||
        project.activation_generation() != retained->project_generation ||
        project.material_catalog().content_revision() !=
            retained->catalog_revision) {
      return false;
    }
    const ProjectManifest *manifest = project.manifest();
    if (manifest == nullptr) {
      return !retained->project_id.valid();
    }
    if (manifest->id() != retained->project_id ||
        !equal_values<ProjectSceneCatalogEntry>(manifest->scenes(), scenes) ||
        !equal_values<ProjectAssetId>(manifest->deleted_material_ids(), deleted) ||
        project.open_scenes().size() != live.size()) {
      return false;
    }
    for (std::size_t index = 0u; index < live.size(); ++index) {
      const auto &open = *project.open_scenes()[index];
      const auto &cached = live[index];
      if (open.session_id != cached.session_id ||
          open.scene_id != cached.scene_id || open.session.get() != cached.session ||
          open.path != cached.path || open.display_name != cached.display_name ||
          open.dirty() != cached.dirty ||
          (open.session != nullptr &&
           open.session->content_revision_stamp() != cached.stamp)) {
        return false;
      }
    }
    const auto *draft = project.standard_material_editor().current();
    if (draft == nullptr) {
      return !draft_session.valid();
    }
    return draft->session_id == draft_session &&
           draft->draft_revision == draft_revision &&
           draft->path == draft_path && draft->dirty == draft_dirty;
  }

  void add_saved_scenes(ProjectAssetDependencySnapshot &candidate,
                        const ProjectFileSystem &file_system,
                        bool refresh_files) {
    for (const ProjectSceneCatalogEntry &entry : project.manifest()->scenes()) {
      std::string error;
      const auto stamp = inspect_stamp(file_system, entry.path, error);
      if (!stamp.has_value()) {
        candidate.errors.push_back(entry.path.portable_string() + ": " + error);
        saved.erase(entry.path);
        continue;
      }
      auto &cached = saved[entry.path];
      if (refresh_files || cached.stamp != stamp ||
          (!cached.dependencies.ok &&
           !cached.dependencies.dependency_data_available)) {
        ++work.saved_scene_read_count;
        ++work.fingerprint_read_count;
        cached.dependencies =
            QdrDocumentReader::read_project_dependencies(stamp->resolved_path);
        cached.stamp = stamp;
      }
      const auto &read = cached.dependencies;
      if (!read.ok) {
        candidate.errors.push_back(entry.path.portable_string() + ": " +
                                    read.message);
        if (!read.dependency_data_available) {
          continue;
        }
      }
      candidate.sources.push_back({
          .path = entry.path,
          .byte_count = read.source_byte_count,
          .content_hash = read.source_content_hash,
      });
      if (SceneId::from_bytes(read.scene_uuid) != entry.id) {
        candidate.errors.push_back(entry.path.portable_string() +
                                   ": scene identity differs from the project manifest.");
        continue;
      }
      ProjectSceneAssetDependencies dependency{
          .scene_id = entry.id,
          .path = entry.path,
          .display_name = read.scene_name,
          .content_revision = read.source_content_hash,
          .material_ids = read.material_ids,
      };
      canonicalize_material_ids(dependency.material_ids, *project.manifest());
      candidate.scenes.push_back(std::move(dependency));
    }
  }

  void add_live_scenes(ProjectAssetDependencySnapshot &candidate) {
    std::vector<LiveSceneCache> next;
    next.reserve(project.open_scenes().size());
    for (const auto &open_pointer : project.open_scenes()) {
      const auto &open = *open_pointer;
      if (open.session == nullptr) {
        candidate.errors.push_back("Cannot inspect scene '" + open.display_name +
                                   "': its document is unavailable.");
        next.push_back({
            .session_id = open.session_id,
            .scene_id = open.scene_id,
            .session = nullptr,
            .path = open.path,
            .display_name = open.display_name,
            .dirty = open.dirty(),
        });
        continue;
      }
      const auto stamp = open.session->content_revision_stamp();
      const auto existing = std::ranges::find(live, open.session_id,
                                               &LiveSceneCache::session_id);
      LiveSceneCache cached{
          .session_id = open.session_id,
          .scene_id = open.scene_id,
          .session = open.session.get(),
          .path = open.path,
          .display_name = open.display_name,
          .stamp = stamp,
          .dirty = open.dirty(),
      };
      if (existing != live.end() && existing->session == cached.session &&
          existing->stamp == stamp) {
        cached.material_ids = existing->material_ids;
      } else {
        ++work.live_scene_scan_count;
        cached.material_ids = stored_material_ids(open.session->scene());
      }
      ProjectSceneAssetDependencies dependency{
          .scene_id = open.scene_id,
          .session_id = open.session_id,
          .path = open.path,
          .display_name = open.display_name,
          .live = true,
          .dirty = cached.dirty,
          .content_revision = stamp_hash(stamp),
          .material_ids = cached.material_ids,
      };
      canonicalize_material_ids(dependency.material_ids, *project.manifest());
      candidate.scenes.push_back(std::move(dependency));
      next.push_back(std::move(cached));
    }
    live = std::move(next);
  }

  void add_materials(ProjectAssetDependencySnapshot &candidate,
                     const ProjectFileSystem &file_system, bool refresh_files) {
    if (refresh_files || !inspected_catalog.has_value() ||
        inspected_catalog->content_revision() != candidate.catalog_revision) {
      ++work.material_catalog_scan_count;
      auto scanned = ProjectMaterialCatalog::scan(file_system);
      if (!scanned.ok()) {
        inspected_catalog.reset();
        candidate.errors.push_back(scanned.error().message);
      } else {
        inspected_catalog = scanned.take_value();
      }
    }
    const auto &catalog = inspected_catalog.has_value()
                              ? *inspected_catalog
                              : project.material_catalog();
    if (catalog.content_revision() != candidate.catalog_revision) {
      candidate.errors.push_back(
          "Material files changed outside the installed catalog. Refresh the "
          "material catalog before deleting a folder.");
    }
    for (const auto &warning : catalog.warnings()) {
      candidate.errors.push_back(warning);
    }
    for (const auto &entry : catalog.entries()) {
      candidate.sources.push_back({
          .path = entry.path,
          .byte_count = static_cast<std::uint64_t>(entry.file_size),
          .content_hash = entry.content_revision,
      });
      candidate.materials.push_back(material_dependencies(
          entry.material, entry.path, entry.content_revision, false, false,
          nullptr, file_system, candidate.errors));
    }
    const auto *draft = project.standard_material_editor().current();
    if (draft != nullptr) {
      candidate.materials.push_back(material_dependencies(
          draft->document, draft->path, draft->draft_revision, true, draft->dirty,
          &draft->pending_texture_sources, file_system, candidate.errors));
      draft_session = draft->session_id;
      draft_revision = draft->draft_revision;
      draft_path = draft->path;
      draft_dirty = draft->dirty;
    } else {
      draft_session = {};
      draft_revision = 0u;
      draft_path.reset();
      draft_dirty = false;
    }
  }

  void add_manifest(ProjectAssetDependencySnapshot &candidate,
                     const ProjectFileSystem &file_system) {
    const auto relative =
        file_system.relative_from_absolute(project.manifest_path());
    if (!relative.ok()) {
      candidate.errors.push_back(relative.error().message);
      return;
    }
    const auto resolved = file_system.resolve_existing_file(relative.value());
    if (!resolved.ok()) {
      candidate.errors.push_back(resolved.error().message);
      return;
    }
    ++work.fingerprint_read_count;
    const auto source = quader_io::read_file_bytes_shared(resolved.value());
    if (!source.ok) {
      candidate.errors.push_back("Cannot inspect the project manifest: " +
                                 source.message);
      return;
    }
    candidate.sources.push_back({
        .path = relative.value(),
        .byte_count = static_cast<std::uint64_t>(source.bytes.size()),
        .content_hash = quader_io::qdr_binary_content_hash(source.bytes),
    });
    const std::string_view text(
        reinterpret_cast<const char *>(source.bytes.data()), source.bytes.size());
    const auto decoded = ProjectManifestStore::deserialize(text);
    if (!decoded.ok()) {
      candidate.errors.push_back(decoded.error().message);
      return;
    }
    const auto &manifest = *project.manifest();
    if (decoded.value().id() != manifest.id() ||
        !equal_values<ProjectSceneCatalogEntry>(decoded.value().scenes(),
                                                manifest.scenes()) ||
        !equal_values<ProjectAssetId>(decoded.value().deleted_material_ids(),
                                     manifest.deleted_material_ids())) {
      candidate.errors.push_back(
          "The project manifest changed on disk. Reopen the project before "
          "deleting a material folder.");
    }
  }

  std::shared_ptr<const ProjectAssetDependencySnapshot> build(bool refresh_files) {
    if (!refresh_files && owners_match()) {
      return retained;
    }
    ++work.snapshot_build_count;
    const ProjectManifest *manifest = project.manifest();
    ProjectAssetDependencySnapshot candidate{
        .project_generation = project.activation_generation(),
        .catalog_revision = project.material_catalog().content_revision(),
    };
    if (manifest == nullptr || project.workspace() == nullptr) {
      candidate.errors.push_back("No project is open.");
      live.clear();
      saved.clear();
      inspected_catalog.reset();
      scenes.clear();
      deleted.clear();
    } else {
      candidate.project_id = manifest->id();
      if (!retained || candidate.project_id != retained->project_id ||
          project.root() != root) {
        live.clear();
        saved.clear();
        inspected_catalog.reset();
      }
      const auto file_system =
          project.workspace()->file_system.begin_resolution_batch();
      add_saved_scenes(candidate, file_system, refresh_files || invalidated);
      add_live_scenes(candidate);
      add_materials(candidate, file_system, refresh_files || invalidated);
      add_manifest(candidate, file_system);
      scenes.assign(manifest->scenes().begin(), manifest->scenes().end());
      deleted.assign(manifest->deleted_material_ids().begin(),
                     manifest->deleted_material_ids().end());
      std::ranges::sort(candidate.sources, {},
                        &ProjectAssetDependencySource::path);
      std::ranges::sort(candidate.errors);
      candidate.errors.erase(
          std::unique(candidate.errors.begin(), candidate.errors.end()),
          candidate.errors.end());
      candidate.complete = candidate.errors.empty();
    }
    root = project.root();
    invalidated = false;
    if (retained && same_snapshot(candidate, *retained)) {
      return retained;
    }
    candidate.revision = ++revision;
    retained = std::make_shared<const ProjectAssetDependencySnapshot>(
        std::move(candidate));
    return retained;
  }

  ProjectSession &project;
  ProjectAssetDependencyWorkStats work;
  std::shared_ptr<const ProjectAssetDependencySnapshot> retained;
  std::filesystem::path root;
  std::vector<ProjectSceneCatalogEntry> scenes;
  std::vector<ProjectAssetId> deleted;
  std::vector<LiveSceneCache> live;
  std::map<ProjectRelativePath, SavedSceneCache> saved;
  std::optional<ProjectMaterialCatalog> inspected_catalog;
  MaterialEditSessionId draft_session;
  std::uint64_t draft_revision = 0u;
  std::optional<ProjectRelativePath> draft_path;
  bool draft_dirty = false;
  bool invalidated = true;
  std::uint64_t revision = 0u;
};

ProjectAssetDependencyService::ProjectAssetDependencyService(ProjectSession &project)
    : state_(std::make_unique<State>(project)) {}

ProjectAssetDependencyService::~ProjectAssetDependencyService() = default;

std::shared_ptr<const ProjectAssetDependencySnapshot>
ProjectAssetDependencyService::snapshot(bool refresh_files) const {
  return state_->build(refresh_files);
}

void ProjectAssetDependencyService::invalidate() noexcept {
  state_->invalidated = true;
}

const ProjectAssetDependencyWorkStats &
ProjectAssetDependencyService::work_stats() const noexcept {
  return state_->work;
}

} // namespace quader
