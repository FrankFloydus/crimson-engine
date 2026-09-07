////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "io/bridge/quader_bridge_reader.hpp"

#include "assets/material_document.hpp"
#include "assets/mesh_collision_geometry_payload.hpp"
#include "core/qstr.hpp"
#include "editor/projection/mesh_projection_service.hpp"
#include "io/project/project_manifest.hpp"
#include "io/project/project_material_catalog.hpp"
#include "io/project/qmat_codec.hpp"
#include "io/qdr/qdr_document_reader.hpp"
#include "io/shared_file_read.hpp"
#include "scene/scene_mesh_occurrence_projection.hpp"
#include "scene/scene_texture_mapping.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using namespace quader;

/** Owns strings independently of native value representations. */
struct SceneEntryStorage final {
  std::string id;
  std::string path;
};

/** Owns the exported, detached geometry of one concrete placement. */
struct MeshStorage final {
  std::string name;
  SceneMeshOccurrenceProjectionRecord placement;
  std::vector<QuaderBridgeVertex> vertices;
  std::vector<uint32_t> indices;
  std::vector<QuaderBridgeSurface> surfaces;
  uint32_t collision_status = QUADER_BRIDGE_COLLISION_DISABLED;
  std::shared_ptr<const std::vector<QuaderBridgeCollisionPoint>> collision_points;
};

/** Retains the source descriptor without touching its image files. */
struct MaterialDescriptor final {
  std::string name;
  std::string source_relative_path;
  QuaderBridgeSourceFingerprint source_fingerprint{};
  ProjectStandardMaterialDefinition standard;
  std::array<std::string, QUADER_BRIDGE_TEXTURE_COUNT> texture_ids;
  std::array<std::string, QUADER_BRIDGE_TEXTURE_COUNT> texture_paths;
  std::array<uint32_t, QUADER_BRIDGE_TEXTURE_COUNT> texture_srgb{};
  std::array<bool, QUADER_BRIDGE_TEXTURE_COUNT> texture_present{};
};

/** Keeps identity available even if source material assets are unavailable. */
struct MaterialStorage final {
  MaterialReference reference;
  std::string id;
  std::unique_ptr<MaterialDescriptor> descriptor;
};

[[nodiscard]] std::string utf8_path(const std::filesystem::path &path) {
  const std::u8string value = path.generic_u8string();
  return {reinterpret_cast<const char *>(value.data()), value.size()};
}

[[nodiscard]] std::filesystem::path native_path(const char *value) {
  const auto *begin = reinterpret_cast<const char8_t *>(value);
  return std::filesystem::path(std::u8string(begin, begin + std::strlen(value)));
}

[[noreturn]] void fail(std::string message) {
  throw std::runtime_error(std::move(message));
}

[[noreturn]] void fail(const ProjectIoError &error) {
  fail(error.message + (error.path.empty() ? std::string{}
                                          : " [" + utf8_path(error.path) + "]"));
}

void require(bool condition, std::string_view message) {
  if (!condition) {
    fail(std::string(message));
  }
}

/** Catches every native exception before returning through the C ABI. */
template <typename Callback>
int guarded(QuaderBridgeError *error, Callback &&callback) noexcept {
  if (error != nullptr) {
    error->message[0] = '\0';
  }
  const auto assign_error = [error](const char *message) noexcept {
    if (error != nullptr) {
      const std::size_t available = std::strlen(message);
      std::size_t length =
          (std::min)(available, sizeof(error->message) - 1u);
      // Preserve a complete UTF-8 prefix when a path-rich error is truncated.
      if (length != available) {
        while (length > 0u &&
               (static_cast<unsigned char>(message[length]) & 0xc0u) == 0x80u) {
          --length;
        }
      }
      std::memcpy(error->message, message, length);
      error->message[length] = '\0';
    }
  };
  try {
    std::forward<Callback>(callback)();
    return 1;
  } catch (const std::exception &exception) {
    assign_error(exception.what());
  } catch (...) {
    assign_error("Unknown native bridge reader failure.");
  }
  return 0;
}

[[nodiscard]] uint32_t count32(std::size_t value) {
  require(value < UINT32_MAX, "Bridge snapshot exceeds its supported size.");
  return static_cast<uint32_t>(value);
}

} // namespace

/** Validated immutable project catalog owned entirely by the reader DLL. */
struct QuaderBridgeProject final {
  quader::ProjectWorkspaceStorage storage;
  std::string id;
  std::vector<SceneEntryStorage> entries;
  uint32_t default_scene_index = QUADER_BRIDGE_NO_SCENE;
};

/** Independent read-only scene and all borrowed export views. */
struct QuaderBridgeScene final {
  quader::ProjectWorkspaceStorage storage;
  quader::QdrProjectReadResult source;
  std::string id;
  std::string relative_path;
  std::vector<MeshStorage> meshes;
  std::vector<MaterialStorage> materials;
  std::unordered_map<std::string, uint32_t> material_indices;
  std::optional<quader::ProjectMaterialCatalog> legacy_material_catalog;
};

/** Independent encoded texture allocation with its portable image extension. */
struct QuaderBridgeBlob final {
  std::vector<std::byte> bytes;
  std::string extension;
};

namespace {

/** Registers an encountered binding using only its persisted source identity. */
[[nodiscard]] uint32_t material_index(QuaderBridgeScene &scene,
                                    MaterialReference reference) {
  if (!reference.valid()) {
    return QUADER_BRIDGE_NO_MATERIAL;
  }
  const std::string id = reference.project_owned()
                             ? std::string(reference.project_asset().str())
                             : std::to_string(reference.embedded_asset());
  const std::string key = (reference.project_owned() ? "project:" : "embedded:") + id;
  const auto found = scene.material_indices.find(key);
  if (found != scene.material_indices.end()) {
    return found->second;
  }
  const uint32_t index = count32(scene.materials.size());
  scene.materials.push_back({.reference = reference, .id = id});
  scene.material_indices.emplace(key, index);
  return index;
}

void build_mesh_snapshot(QuaderBridgeScene &target) {
  const SceneDocument &scene = target.source.document.scene();
  SceneMeshOccurrenceProjectionWorkStats work;
  const SceneMeshOccurrenceProjectionSnapshot occurrences =
      build_scene_mesh_occurrence_projection(
          scene, &work, SceneMeshOccurrenceVisibility::IncludeHidden);
  require(work.singular_definition_count == 0u,
          "Cannot export an instance whose definition has a singular transform.");
  target.meshes.reserve(occurrences.records().size());
  std::unordered_map<const MeshGeometryPayload *,
                     std::shared_ptr<const std::vector<QuaderBridgeCollisionPoint>>>
      collision_points_by_geometry;
  for (const SceneMeshOccurrenceProjectionRecord &placement : occurrences.records()) {
    const MeshComponent *component = scene.mesh(placement.geometry_entity);
    require(component != nullptr, "Mesh occurrence has no geometry binding.");
    const MeshAsset *source = target.source.document.assets().mesh(component->mesh);
    require(source != nullptr, "Cannot decode the source mesh geometry.");
    const std::optional<QMat4> sampling = effective_texture_sampling_from_local(
        scene, placement.geometry_entity,
        placement.formal_instance() ? placement.display_entity : kInvalidEntityId);
    require(sampling.has_value(), "Cannot resolve mesh occurrence texture mapping.");
    MeshAsset exported = *source;
    if (sampling->values != QMat4{}.values) {
      require(MeshProjectionService::initialize_compiled_texture_mapping(exported),
              "Cannot reconstruct the source mesh texture mapping.");
      MeshProjectionService::apply_mesh_texture_mapping(exported, *sampling);
    }
    const MeshGeometryPayload &geometry = exported.geometry();
    (void)count32(geometry.vertices.size());
    (void)count32(geometry.indices.size());
    require(scene_matrix_is_finite(placement.world_from_geometry),
            "Source mesh transform contains non-finite values.");
    MeshStorage mesh;
    const NameComponent *display_name = scene.name(placement.display_entity);
    const NameComponent *geometry_name = scene.name(placement.geometry_entity);
    mesh.name = display_name != nullptr ? display_name->name : "Mesh";
    if (placement.formal_instance() && geometry_name != nullptr) {
      mesh.name += " / " + geometry_name->name;
    }
    mesh.placement = placement;
    if (component->generate_collisions) {
      mesh.collision_status = mesh_collision_supported(*component)
                                  ? QUADER_BRIDGE_COLLISION_TRIMESH_STATIC
                                  : QUADER_BRIDGE_COLLISION_UNSUPPORTED;
      if (mesh.collision_status == QUADER_BRIDGE_COLLISION_TRIMESH_STATIC) {
        // Derive from the original geometry, before detached UV/material
        // projection. Instances borrow one immutable local collision buffer.
        const MeshGeometryPayload &collision_source = source->geometry();
        auto found = collision_points_by_geometry.find(&collision_source);
        if (found == collision_points_by_geometry.end()) {
          const auto collision = mesh_collision_geometry_payload(collision_source);
          require(collision != nullptr, "Cannot generate the source mesh collision.");
          require(collision->error_message.empty(), collision->error_message);
          (void)count32(collision->faces.size());
          auto points = std::make_shared<std::vector<QuaderBridgeCollisionPoint>>();
          points->reserve(collision->faces.size());
          for (const QVec3 &point : collision->faces) {
            points->push_back({.position = {point.x, point.y, point.z}});
          }
          found = collision_points_by_geometry.emplace(&collision_source,
                                                       std::move(points)).first;
        }
        mesh.collision_points = found->second;
      }
    }
    mesh.vertices.reserve(geometry.vertices.size());
    for (const MeshVertex &vertex : geometry.vertices) {
      QuaderBridgeVertex output{
          .position = {vertex.position.x, vertex.position.y, vertex.position.z},
          .normal = {vertex.normal.x, vertex.normal.y, vertex.normal.z},
          .uv = {vertex.uv.x, vertex.uv.y}};
      const auto finite = [](float value) { return std::isfinite(value); };
      require(std::ranges::all_of(output.position, finite) &&
                  std::ranges::all_of(output.normal, finite) &&
                  std::ranges::all_of(output.uv, finite),
              "Source mesh has non-finite vertex attributes.");
      mesh.vertices.push_back(output);
    }
    const MaterialComponent *binding = scene.material(placement.geometry_entity);
    const std::vector<MeshPrimitiveSpan> spans = normalized_mesh_primitive_spans(exported);
    std::unordered_map<uint32_t, std::size_t> surface_by_material;
    std::vector<std::vector<uint32_t>> surface_indices;
    for (const MeshPrimitiveSpan &span : spans) {
      require(span.index_count % 3u == 0u,
              "Source material surface does not contain complete triangles.");
      const uint32_t material = material_index(
          target, binding != nullptr ? binding->material_for_slot(span.material_slot)
                                     : MaterialReference{});
      auto [found, inserted] = surface_by_material.emplace(material, surface_indices.size());
      if (inserted) {
        surface_indices.emplace_back();
        mesh.surfaces.push_back({.material_index = material});
      }
      std::vector<uint32_t> &indices = surface_indices[found->second];
      for (uint32_t offset = 0u; offset < span.index_count; ++offset) {
        const uint32_t index = geometry.indices[span.index_offset + offset];
        require(index < geometry.vertices.size(), "Source mesh index is out of bounds.");
        indices.push_back(index);
      }
    }
    mesh.indices.reserve(geometry.indices.size());
    for (std::size_t index = 0u; index < mesh.surfaces.size(); ++index) {
      mesh.surfaces[index].index_offset = count32(mesh.indices.size());
      mesh.surfaces[index].index_count = count32(surface_indices[index].size());
      mesh.indices.insert(mesh.indices.end(), surface_indices[index].begin(),
                          surface_indices[index].end());
    }
    target.meshes.push_back(std::move(mesh));
  }
  (void)count32(target.meshes.size());
  (void)count32(target.materials.size());
}

[[nodiscard]] ProjectMaterialCatalogEntry read_project_material(
    QuaderBridgeScene &scene, ProjectAssetId id) {
  for (const ProjectAssetReference &dependency : scene.source.project_asset_dependencies) {
    if (dependency.stable_id != id) {
      continue;
    }
    require(dependency.kind == ProjectAssetKind::Material,
            "Source material dependency has an incompatible asset kind.");
    auto decoded = QmatCodec::read(scene.storage.file_system, dependency.path);
    if (!decoded.ok()) {
      fail(decoded.error());
    }
    require(decoded.value().material.id == id,
            "Source material identity does not match its scene dependency.");
    return {.material = std::move(decoded.value().material),
            .path = dependency.path,
            .file_size = decoded.value().file_size,
            .content_revision = decoded.value().content_revision};
  }
  // Older QDR files may contain stable material IDs without dependency paths.
  if (!scene.legacy_material_catalog.has_value()) {
    auto catalog = ProjectMaterialCatalog::scan(scene.storage.file_system);
    if (!catalog.ok()) {
      fail(catalog.error());
    }
    scene.legacy_material_catalog = catalog.take_value();
  }
  const ProjectMaterialCatalogEntry *entry = scene.legacy_material_catalog->find(id);
  if (entry == nullptr) {
    fail("Source project material is unavailable: " + std::string(id.str()));
  }
  return *entry;
}

[[nodiscard]] MaterialDescriptor &material_descriptor(QuaderBridgeScene &scene,
                                                      uint32_t index) {
  require(index < scene.materials.size(), "Material index is out of bounds.");
  MaterialStorage &storage = scene.materials[index];
  if (storage.descriptor) {
    return *storage.descriptor;
  }
  auto descriptor = std::make_unique<MaterialDescriptor>();
  if (storage.reference.project_owned()) {
    const ProjectMaterialCatalogEntry decoded =
        read_project_material(scene, storage.reference.project_asset());
    const ProjectMaterialDocument &material = decoded.material;
    const ProjectStandardMaterialDefinition *standard = material.standard_material();
    require(standard != nullptr, "Only Standard source materials are supported.");
    descriptor->name = material.name;
    descriptor->source_relative_path = decoded.path.portable_string();
    descriptor->source_fingerprint = {
        .byte_count = static_cast<uint64_t>(decoded.file_size),
        .content_hash = decoded.content_revision};
    descriptor->standard = *standard;
    for (uint32_t slot = 0u; slot < QUADER_BRIDGE_TEXTURE_COUNT; ++slot) {
      const auto *texture = standard->texture(static_cast<StandardMaterialTextureSlot>(slot));
      if (texture != nullptr && texture->has_value()) {
        descriptor->texture_present[slot] = true;
        descriptor->texture_ids[slot] = std::string((*texture)->id.str());
        descriptor->texture_paths[slot] = (*texture)->project_relative_path;
        descriptor->texture_srgb[slot] =
            (*texture)->color_space == ProjectTextureColorSpace::Srgb ? 1u : 0u;
      }
    }
  } else {
    const AssetRegistry &assets = scene.source.document.assets();
    const MaterialDocument *material = assets.material(storage.reference.embedded_asset());
    require(material != nullptr, "Source embedded material is unavailable.");
    require(unsupported_material_texture_slots(*material).empty(),
            "Source embedded material has unsupported texture inputs.");
    descriptor->name = material->name;
    descriptor->source_relative_path = scene.relative_path;
    descriptor->source_fingerprint = {
        .byte_count = scene.source.source_byte_count,
        .content_hash = scene.source.source_content_hash};
    descriptor->standard.base_color =
        {material->base_color.x, material->base_color.y, material->base_color.z};
    descriptor->standard.roughness = material->roughness;
    descriptor->standard.metallic = material->metallic;
    constexpr std::array<const char *, QUADER_BRIDGE_TEXTURE_COUNT> kSlots{
        material_texture_slot::kBaseColor, material_texture_slot::kRoughness,
        material_texture_slot::kMetallic, material_texture_slot::kNormal};
    for (uint32_t slot = 0u; slot < QUADER_BRIDGE_TEXTURE_COUNT; ++slot) {
      const std::optional<AssetId> id = material_texture_slot_asset(*material, kSlots[slot]);
      if (!id.has_value()) {
        continue;
      }
      const TextureAsset *texture = assets.texture(*id);
      require(texture != nullptr, "Source embedded texture is unavailable.");
      descriptor->texture_present[slot] = true;
      descriptor->texture_ids[slot] = std::to_string(*id);
      descriptor->texture_paths[slot] = utf8_path(texture->source_path);
      descriptor->texture_srgb[slot] =
          texture->color_space == TextureColorSpace::Srgb ? 1u : 0u;
    }
  }
  storage.descriptor = std::move(descriptor);
  return *storage.descriptor;
}

int open_project(const char *path, QuaderBridgeProject **out,
                 QuaderBridgeError *error) noexcept {
  if (out != nullptr) {
    *out = nullptr;
  }
  return guarded(error, [&] {
    require(path != nullptr && *path != '\0' && out != nullptr,
            "A project manifest path and output handle are required.");
    auto opened = ProjectManifestStore::open(native_path(path));
    if (!opened.ok()) {
      fail(opened.error());
    }
    auto project = std::make_unique<QuaderBridgeProject>();
    project->storage = opened.take_value();
    project->id = std::string(project->storage.manifest.id().str());
    const auto entries = project->storage.manifest.scenes();
    (void)count32(entries.size());
    project->entries.reserve(entries.size());
    const auto &active = project->storage.manifest.active_scene_path();
    for (const ProjectSceneCatalogEntry &entry : entries) {
      if (active.has_value() && entry.path == *active) {
        project->default_scene_index = count32(project->entries.size());
      }
      project->entries.push_back({.id = std::string(entry.id.str()),
                                  .path = entry.path.portable_string()});
    }
    if (!entries.empty() && project->default_scene_index == QUADER_BRIDGE_NO_SCENE) {
      project->default_scene_index = 0u;
    }
    *out = project.release();
  });
}

void release_project(QuaderBridgeProject *project) noexcept { delete project; }

int project_view(const QuaderBridgeProject *project, QuaderBridgeProjectView *out,
                 QuaderBridgeError *error) noexcept {
  return guarded(error, [&] {
    require(project != nullptr && out != nullptr, "Project and output view are required.");
    *out = {.id = project->id.c_str(),
            .name = project->storage.manifest.name().c_str(),
            .scene_count = count32(project->entries.size()),
            .default_scene_index = project->default_scene_index};
  });
}

int scene_entry(const QuaderBridgeProject *project, uint32_t index,
                QuaderBridgeSceneEntry *out, QuaderBridgeError *error) noexcept {
  return guarded(error, [&] {
    require(project != nullptr && out != nullptr, "Project and output entry are required.");
    require(index < project->entries.size(), "Scene index is out of bounds.");
    *out = {.id = project->entries[index].id.c_str(),
            .relative_path = project->entries[index].path.c_str()};
  });
}

int open_scene(const QuaderBridgeProject *project, const char *scene_id,
               QuaderBridgeScene **out, QuaderBridgeError *error) noexcept {
  if (out != nullptr) {
    *out = nullptr;
  }
  return guarded(error, [&] {
    require(project != nullptr && scene_id != nullptr && out != nullptr,
            "Project, scene identity, and output handle are required.");
    const auto id = SceneId::parse(scene_id);
    if (!id.ok()) {
      fail(id.error());
    }
    require(project->storage.manifest.find_scene(id.value()) != nullptr,
            "Selected scene is not in the validated project catalog.");
    auto reopened = ProjectManifestStore::open(project->storage.manifest_path);
    if (!reopened.ok()) {
      fail(reopened.error());
    }
    require(reopened.value().manifest.id() == project->storage.manifest.id(),
            "Project identity changed since validation.");
    const ProjectSceneCatalogEntry *entry = reopened.value().manifest.find_scene(id.value());
    require(entry != nullptr, "Selected scene is no longer in the project catalog.");
    const auto path = reopened.value().file_system.resolve_existing_file(entry->path);
    if (!path.ok()) {
      fail(path.error());
    }
    auto scene = std::make_unique<QuaderBridgeScene>();
    scene->relative_path = entry->path.portable_string();
    scene->storage = reopened.take_value();
    scene->source = QdrDocumentReader::read_project_document(path.value());
    if (!scene->source.ok) {
      fail("Cannot read scene " + utf8_path(path.value()) + ": " + scene->source.message);
    }
    require(SceneId::from_bytes(scene->source.scene_uuid) == id.value(),
            "Scene identity does not match its project catalog entry.");
    scene->id = std::string(id.value().str());
    build_mesh_snapshot(*scene);
    *out = scene.release();
  });
}

void release_scene(QuaderBridgeScene *scene) noexcept { delete scene; }

int scene_view(const QuaderBridgeScene *scene, QuaderBridgeSceneView *out,
               QuaderBridgeError *error) noexcept {
  return guarded(error, [&] {
    require(scene != nullptr && out != nullptr, "Scene and output view are required.");
    *out = {.id = scene->id.c_str(), .relative_path = scene->relative_path.c_str(),
            .mesh_count = count32(scene->meshes.size()),
            .material_count = count32(scene->materials.size())};
  });
}

int mesh_view(const QuaderBridgeScene *scene, uint32_t index,
              QuaderBridgeMeshView *out, QuaderBridgeError *error) noexcept {
  return guarded(error, [&] {
    require(scene != nullptr && out != nullptr, "Scene and output mesh are required.");
    require(index < scene->meshes.size(), "Mesh index is out of bounds.");
    const MeshStorage &mesh = scene->meshes[index];
    *out = {.name = mesh.name.c_str(),
            .display_entity = mesh.placement.display_entity,
            .geometry_entity = mesh.placement.geometry_entity,
            .definition_entity = mesh.placement.definition_entity,
            .visible = mesh.placement.effectively_visible ? 1u : 0u,
            .vertices = mesh.vertices.data(), .vertex_count = count32(mesh.vertices.size()),
            .indices = mesh.indices.data(), .index_count = count32(mesh.indices.size()),
            .surfaces = mesh.surfaces.data(), .surface_count = count32(mesh.surfaces.size()),
            .collision_status = mesh.collision_status,
            .collision_points = mesh.collision_points ? mesh.collision_points->data() : nullptr,
            .collision_point_count = mesh.collision_points
                                         ? count32(mesh.collision_points->size()) : 0u};
    std::ranges::copy(mesh.placement.world_from_geometry.values, out->transform);
  });
}

int material_identity(const QuaderBridgeScene *scene, uint32_t index,
                      QuaderBridgeMaterialIdentity *out,
                      QuaderBridgeError *error) noexcept {
  return guarded(error, [&] {
    require(scene != nullptr && out != nullptr, "Scene and material identity are required.");
    require(index < scene->materials.size(), "Material index is out of bounds.");
    const MaterialStorage &material = scene->materials[index];
    *out = {.id = material.id.c_str(),
            .kind = static_cast<uint32_t>(material.reference.project_owned()
                                              ? QUADER_BRIDGE_MATERIAL_PROJECT
                                              : QUADER_BRIDGE_MATERIAL_EMBEDDED)};
  });
}

int material_view(QuaderBridgeScene *scene, uint32_t index,
                  QuaderBridgeMaterialView *out, QuaderBridgeError *error) noexcept {
  return guarded(error, [&] {
    require(scene != nullptr && out != nullptr, "Scene and material view are required.");
    const MaterialDescriptor &material = material_descriptor(*scene, index);
    const ProjectStandardMaterialDefinition &standard = material.standard;
    *out = {.name = material.name.c_str(),
            .source_relative_path = material.source_relative_path.c_str(),
            .roughness = standard.roughness, .metallic = standard.metallic,
            .normal_strength = standard.normal_strength,
            .normal_directx = standard.normal_convention == NormalMapConvention::DirectX ? 1u : 0u,
            .uv_rotation_degrees = standard.texture_transform.rotation_degrees};
    std::ranges::copy(standard.base_color, out->base_color);
    std::ranges::copy(standard.texture_transform.center, out->uv_center);
    std::ranges::copy(standard.texture_transform.scale, out->uv_scale);
    std::ranges::copy(standard.texture_transform.offset, out->uv_offset);
    for (uint32_t slot = 0u; slot < QUADER_BRIDGE_TEXTURE_COUNT; ++slot) {
      out->textures[slot] = {.present = material.texture_present[slot] ? 1u : 0u,
                             .id = material.texture_ids[slot].c_str(),
                             .source_path = material.texture_paths[slot].c_str(),
                             .srgb = material.texture_srgb[slot]};
    }
  });
}

int read_texture(QuaderBridgeScene *scene, uint32_t material_index_value,
                 uint32_t slot, QuaderBridgeBlob **out,
                 QuaderBridgeError *error) noexcept {
  if (out != nullptr) {
    *out = nullptr;
  }
  return guarded(error, [&] {
    require(scene != nullptr && out != nullptr, "Scene and texture output are required.");
    require(slot < QUADER_BRIDGE_TEXTURE_COUNT, "Texture slot is out of bounds.");
    const MaterialDescriptor &material = material_descriptor(*scene, material_index_value);
    require(material.texture_present[slot], "Source material has no texture in this slot.");
    const std::string &source_path = material.texture_paths[slot];
    require(!source_path.empty(), "Source texture has no encoded file (generated textures are unsupported).");
    const std::filesystem::path source = native_path(source_path.c_str());
    auto relative = source.is_absolute()
                        ? scene->storage.file_system.relative_from_absolute(source)
                        : ProjectRelativePath::parse(source);
    if (!relative.ok()) {
      fail(relative.error());
    }
    auto resolved = scene->storage.file_system.resolve_existing_file(relative.value());
    if (!resolved.ok()) {
      fail(resolved.error());
    }
    const std::filesystem::path &path = resolved.value();
    auto blob = std::make_unique<QuaderBridgeBlob>();
    blob->extension = QStr::to_ascii_lower(utf8_path(path.extension()));
    require(!blob->extension.empty(), "Source texture has no image file extension.");
    constexpr uint64_t kMaximumBytes = 64u * 1024u * 1024u;
    auto read = quader_io::read_file_bytes_shared(path, kMaximumBytes);
    require(read.error != quader_io::SharedFileReadError::Limit,
            "Source texture is empty or exceeds the 64 MiB supported size.");
    if (!read.ok) {
      fail("Cannot read source texture " + utf8_path(path) + ": " + read.message);
    }
    require(!read.bytes.empty(),
            "Source texture is empty or exceeds the 64 MiB supported size.");
    blob->bytes = std::move(read.bytes);
    *out = blob.release();
  });
}

int blob_view(const QuaderBridgeBlob *blob, QuaderBridgeBlobView *out,
              QuaderBridgeError *error) noexcept {
  return guarded(error, [&] {
    require(blob != nullptr && out != nullptr, "Texture blob and output view are required.");
    *out = {.bytes = reinterpret_cast<const uint8_t *>(blob->bytes.data()),
            .byte_count = blob->bytes.size(),
            .extension = blob->extension.c_str()};
  });
}

void release_blob(QuaderBridgeBlob *blob) noexcept { delete blob; }

int scene_source_fingerprint(const QuaderBridgeScene *scene,
                             QuaderBridgeSourceFingerprint *out,
                             QuaderBridgeError *error) noexcept {
  return guarded(error, [&] {
    require(scene != nullptr && out != nullptr,
            "Scene and source fingerprint output are required.");
    *out = {.byte_count = scene->source.source_byte_count,
            .content_hash = scene->source.source_content_hash};
  });
}

int material_source_fingerprint(QuaderBridgeScene *scene, uint32_t index,
                                QuaderBridgeSourceFingerprint *out,
                                QuaderBridgeError *error) noexcept {
  return guarded(error, [&] {
    require(scene != nullptr && out != nullptr,
            "Scene and material fingerprint output are required.");
    *out = material_descriptor(*scene, index).source_fingerprint;
  });
}

constexpr QuaderBridgeApi kApi{
    .abi_version = QUADER_BRIDGE_ABI_VERSION,
    .struct_size = sizeof(QuaderBridgeApi),
    .open_project = open_project, .release_project = release_project,
    .project_view = project_view, .scene_entry = scene_entry,
    .open_scene = open_scene, .release_scene = release_scene,
    .scene_view = scene_view, .mesh_view = mesh_view,
    .material_identity = material_identity, .material_view = material_view,
    .read_texture = read_texture, .blob_view = blob_view, .release_blob = release_blob,
    .scene_source_fingerprint = scene_source_fingerprint,
    .material_source_fingerprint = material_source_fingerprint};

} // namespace

extern "C" QUADER_BRIDGE_EXPORT const QuaderBridgeApi *
quader_bridge_get_api(uint32_t requested_version) {
  return requested_version == QUADER_BRIDGE_ABI_VERSION ? &kApi : nullptr;
}
