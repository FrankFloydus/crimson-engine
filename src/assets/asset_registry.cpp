////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "assets/asset_registry.hpp"
#include "assets/mesh_asset_payload.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace quader {

quader_poly::Document &MeshAsset::edit_authored_polygon_document() {
  auto editable = authored_polygon_document
                      ? std::make_shared<quader_poly::Document>(
                            *authored_polygon_document)
                      : std::make_shared<quader_poly::Document>();
  authored_polygon_document = editable;
  has_authored_polygon_document = true;
  return *editable;
}

void MeshAsset::replace_authored_polygon_document(
    quader_poly::Document document) {
  authored_polygon_document =
      std::make_shared<const quader_poly::Document>(std::move(document));
  has_authored_polygon_document = true;
}

AssetId AssetRegistry::add_mesh(MeshAsset mesh) {
  if (next_id_ == kInvalidAssetId || !validate_mesh(mesh).ok) {
    return kInvalidAssetId;
  }
  const AssetId id = next_id_++;
  mesh = isolate_mesh_asset_payload(std::move(mesh));
  intern_mesh_payload(mesh);
  meshes_.add(id, std::move(mesh));
  mark_dirty();
  return id;
}

AssetId AssetRegistry::add_mesh(AssetId id, MeshAsset mesh) {
  if (id == kInvalidAssetId) {
    return add_mesh(std::move(mesh));
  }
  if (meshes_.contains(id) || !validate_mesh(mesh).ok) {
    return kInvalidAssetId;
  }
  next_id_ = std::max(next_id_, id + 1u);
  mesh = isolate_mesh_asset_payload(std::move(mesh));
  intern_mesh_payload(mesh);
  meshes_.add(id, std::move(mesh));
  mark_dirty();
  return id;
}

AssetId AssetRegistry::add_deferred_mesh(
    AssetId id, std::string name, bool has_stable_cpu_topology,
    bool has_authored_polygon_document, std::function<MeshAsset()> loader) {
  if (id == kInvalidAssetId || meshes_.contains(id) || !loader) {
    return kInvalidAssetId;
  }
  MeshAsset descriptor;
  descriptor.name = std::move(name);
  descriptor.has_stable_cpu_topology = has_stable_cpu_topology;
  if (meshes_.add(id, std::move(descriptor)) == kInvalidAssetId) {
    return kInvalidAssetId;
  }
  deferred_meshes_.emplace(
      id, DeferredMesh{
              .has_stable_cpu_topology = has_stable_cpu_topology,
              .has_authored_polygon_document =
                  has_authored_polygon_document,
              .loader = std::move(loader),
          });
  next_id_ = std::max(next_id_, id + 1u);
  mark_dirty();
  return id;
}

AssetId AssetRegistry::add_material(MaterialDocument material) {
  if (next_id_ == kInvalidAssetId || !validate_material(material).ok) {
    return kInvalidAssetId;
  }
  const AssetId id = next_id_++;
  normalize_material_texture_slots(material);
  materials_.add(id, std::move(material));
  mark_dirty();
  return id;
}

AssetId AssetRegistry::add_material(AssetId id, MaterialDocument material) {
  if (id == kInvalidAssetId) {
    return add_material(std::move(material));
  }
  if (materials_.contains(id) || !validate_material(material).ok) {
    return kInvalidAssetId;
  }
  next_id_ = std::max(next_id_, id + 1u);
  normalize_material_texture_slots(material);
  materials_.add(id, std::move(material));
  mark_dirty();
  return id;
}

AssetId AssetRegistry::add_texture(TextureAsset texture) {
  if (next_id_ == kInvalidAssetId) {
    return kInvalidAssetId;
  }
  const AssetId id = next_id_++;
  textures_.add(id, std::move(texture));
  mark_dirty();
  return id;
}

AssetId AssetRegistry::add_texture(AssetId id, TextureAsset texture) {
  if (id == kInvalidAssetId) {
    return add_texture(std::move(texture));
  }
  if (textures_.contains(id)) {
    return kInvalidAssetId;
  }
  next_id_ = std::max(next_id_, id + 1u);
  textures_.add(id, std::move(texture));
  mark_dirty();
  return id;
}

bool AssetRegistry::remove_mesh(AssetId id) {
  if (!meshes_.remove(id)) {
    return false;
  }
  deferred_meshes_.erase(id);
  mark_dirty();
  return true;
}

bool AssetRegistry::remove_material(AssetId id) {
  if (!materials_.remove(id)) {
    return false;
  }
  mark_dirty();
  return true;
}

bool AssetRegistry::remove_texture(AssetId id) {
  if (!textures_.remove(id)) {
    return false;
  }
  mark_dirty();
  return true;
}

bool AssetRegistry::replace_mesh(AssetId id, MeshAsset mesh) {
  if (!validate_mesh(mesh).ok) {
    return false;
  }
  mesh = isolate_mesh_asset_payload(std::move(mesh));
  intern_mesh_payload(mesh);
  if (!meshes_.replace(id, std::move(mesh))) {
    return false;
  }
  deferred_meshes_.erase(id);
  mark_dirty();
  return true;
}

bool AssetRegistry::replace_material(AssetId id, MaterialDocument material) {
  if (!validate_material(material).ok) {
    return false;
  }
  normalize_material_texture_slots(material);
  if (!materials_.replace(id, std::move(material))) {
    return false;
  }
  mark_dirty();
  return true;
}

bool AssetRegistry::replace_texture(AssetId id, TextureAsset texture) {
  if (!textures_.replace(id, std::move(texture))) {
    return false;
  }
  mark_dirty();
  return true;
}

bool AssetRegistry::update_mesh(
    AssetId id, const std::function<void(MeshAsset &)> &update) {
  const MeshAsset *existing = mesh(id);
  if (existing == nullptr) {
    return false;
  }
  MeshAsset updated = clone_mesh_asset_payload(*existing);
  update(updated);
  if (!validate_mesh(updated).ok) {
    return false;
  }
  updated = isolate_mesh_asset_payload(std::move(updated));
  intern_mesh_payload(updated);
  if (!meshes_.replace(id, std::move(updated))) {
    return false;
  }
  mark_dirty();
  return true;
}

bool AssetRegistry::update_material(
    AssetId id, const std::function<void(MaterialDocument &)> &update) {
  const MaterialDocument *existing = materials_.get(id);
  if (existing == nullptr) {
    return false;
  }
  MaterialDocument updated = *existing;
  update(updated);
  if (!validate_material(updated).ok) {
    return false;
  }
  normalize_material_texture_slots(updated);
  if (!materials_.replace(id, std::move(updated))) {
    return false;
  }
  mark_dirty();
  return true;
}

AssetRegistryMutationResult
AssetRegistry::set_material_texture(AssetId material, std::string_view slot,
                                    AssetId texture) {
  if (slot.empty()) {
    return {.ok = false,
            .changed = false,
            .message = "Material texture slot is empty"};
  }
  const std::optional<std::string> texture_slot =
      canonical_material_texture_slot(slot);
  if (!texture_slot.has_value()) {
    return {.ok = false,
            .changed = false,
            .message = "Unsupported material texture slot"};
  }
  if (texture == kInvalidAssetId || textures_.get(texture) == nullptr) {
    return {.ok = false,
            .changed = false,
            .message = "Texture asset does not exist"};
  }

  const MaterialDocument *existing = materials_.get(material);
  if (existing == nullptr) {
    return {.ok = false,
            .changed = false,
            .message = "Material asset does not exist"};
  }
  if (const auto current = existing->texture_slots.find(*texture_slot);
      current != existing->texture_slots.end() && current->second == texture) {
    return {.ok = true, .changed = false, .message = "Material unchanged"};
  }

  MaterialDocument updated = *existing;
  set_material_texture_slot(updated, *texture_slot, texture);
  const AssetRegistryValidationResult validation = validate_material(updated);
  if (!validation.ok) {
    return {.ok = false, .changed = false, .message = validation.message};
  }
  if (!replace_material(material, std::move(updated))) {
    return {.ok = false,
            .changed = false,
            .message = "Material asset does not exist"};
  }
  return {.ok = true, .changed = true, .message = "Material updated"};
}

AssetRegistryValidationResult
AssetRegistry::validate_mesh(const MeshAsset &mesh) const {
  const auto finite_vec2 = [](QVec2 value) {
    return std::isfinite(value.x) && std::isfinite(value.y);
  };
  const auto finite_vec3 = [](QVec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
  };

  const MeshGeometryPayload &geometry = mesh.geometry();
  for (const MeshVertex &vertex : geometry.vertices) {
    if (!finite_vec3(vertex.position) || !finite_vec3(vertex.normal) ||
        !finite_vec2(vertex.uv)) {
      return {.ok = false, .message = "Mesh vertex data must be finite"};
    }
  }
  if (geometry.indices.size() % 3u != 0u) {
    return {.ok = false,
            .message = "Mesh indices must describe complete triangles"};
  }
  for (const std::uint32_t index : geometry.indices) {
    if (index >= geometry.vertices.size()) {
      return {.ok = false,
              .message = "Mesh index references a missing vertex"};
    }
  }

  if (mesh.has_authored_polygon_document !=
      static_cast<bool>(mesh.authored_polygon_document)) {
    return {.ok = false,
            .message = "Mesh authored polygon document state is inconsistent"};
  }
  if (mesh.authored_polygon_document) {
    for (const quader_poly::Vertex &vertex :
         mesh.authored_polygon_document->vertices) {
      if (!finite_vec3(vertex.position)) {
        return {.ok = false,
                .message =
                    "Mesh authored polygon vertex data must be finite"};
      }
    }
    for (const quader_poly::Face &face :
         mesh.authored_polygon_document->faces) {
      for (const QVec2 uv : face.uvs) {
        if (!finite_vec2(uv)) {
          return {.ok = false,
                  .message = "Mesh authored polygon UV data must be finite"};
        }
      }
    }
  }
  return {};
}

AssetRegistryValidationResult
AssetRegistry::validate_material(const MaterialDocument &material) const {
  if (!std::isfinite(material.base_color.x) ||
      !std::isfinite(material.base_color.y) ||
      !std::isfinite(material.base_color.z) ||
      !std::isfinite(material.roughness) ||
      !std::isfinite(material.metallic)) {
    return {.ok = false, .message = "Material values must be finite"};
  }
  for (const auto &[slot, texture] : material.texture_slots) {
    if (slot.empty()) {
      return {.ok = false, .message = "Material texture slot is empty"};
    }
    if (!material_texture_slot_supported(slot)) {
      return {.ok = false, .message = "Unsupported material texture slot"};
    }
    if (texture == kInvalidAssetId || textures_.get(texture) == nullptr) {
      return {.ok = false, .message = "Texture asset does not exist"};
    }
  }
  return {};
}

bool AssetRegistry::update_texture(
    AssetId id, const std::function<void(TextureAsset &)> &update) {
  if (!textures_.update(id, update)) {
    return false;
  }
  mark_dirty();
  return true;
}

const MeshAsset* AssetRegistry::mesh(AssetId id) const {
  const auto deferred = deferred_meshes_.find(id);
  if (deferred == deferred_meshes_.end()) {
    return meshes_.get(id);
  }
  const MeshAsset *descriptor = meshes_.get(id);
  if (descriptor == nullptr) {
    deferred_meshes_.erase(deferred);
    return nullptr;
  }

  MeshAsset materialized = deferred->second.loader();
  materialized.name = descriptor->name;
  materialized.has_stable_cpu_topology =
      deferred->second.has_stable_cpu_topology;
  if (materialized.has_authored_polygon_document !=
      deferred->second.has_authored_polygon_document) {
    return nullptr;
  }
  if (!validate_mesh(materialized).ok) {
    return nullptr;
  }
  materialized = isolate_mesh_asset_payload(std::move(materialized));
  intern_mesh_payload(materialized);
  const MeshAssetRevision revision = descriptor->revision;
  MeshAsset *stored = const_cast<MeshAsset *>(descriptor);
  *stored = std::move(materialized);
  stored->id = id;
  stored->revision = revision;
  deferred_meshes_.erase(deferred);
  return stored;
}

bool AssetRegistry::has_mesh(AssetId id) const { return meshes_.contains(id); }

bool AssetRegistry::mesh_materialized(AssetId id) const {
  return meshes_.contains(id) && !deferred_meshes_.contains(id);
}

std::optional<MeshAssetDescriptor>
AssetRegistry::mesh_descriptor(AssetId id) const {
  const MeshAsset *stored = meshes_.get(id);
  if (stored == nullptr) {
    return std::nullopt;
  }
  const auto deferred = deferred_meshes_.find(id);
  return MeshAssetDescriptor{
      .id = id,
      .revision = stored->revision,
      .name = stored->name,
      .has_stable_cpu_topology =
          deferred == deferred_meshes_.end()
              ? stored->has_stable_cpu_topology
              : deferred->second.has_stable_cpu_topology,
      .has_authored_polygon_document =
          deferred == deferred_meshes_.end()
              ? stored->has_authored_polygon_document
              : deferred->second.has_authored_polygon_document,
      .materialized = deferred == deferred_meshes_.end(),
  };
}

const MaterialDocument* AssetRegistry::material(AssetId id) const {
    return materials_.get(id);
}

const TextureAsset* AssetRegistry::texture(AssetId id) const {
    return textures_.get(id);
}

MeshAssetRevision AssetRegistry::mesh_revision(AssetId id) const {
  return meshes_.revision(id);
}

MaterialAssetRevision AssetRegistry::material_revision(AssetId id) const {
  return materials_.revision(id);
}

TextureAssetRevision AssetRegistry::texture_revision(AssetId id) const {
  return textures_.revision(id);
}

void AssetRegistry::intern_mesh_payload(MeshAsset &mesh) const {
  std::shared_ptr<const MeshGeometryPayload> geometry = mesh.geometry_payload();
  const std::uint64_t content_id = geometry->content_id().value;
  auto &geometry_candidates = mesh_geometry_pool_[content_id];
  for (auto candidate = geometry_candidates.begin();
       candidate != geometry_candidates.end();) {
    std::shared_ptr<const MeshGeometryPayload> existing = candidate->lock();
    if (!existing) {
      candidate = geometry_candidates.erase(candidate);
      continue;
    }
    if (existing.get() != geometry.get() &&
        mesh_geometry_payload_equal(*existing, *geometry)) {
      mesh.share_geometry(existing);
      geometry = std::move(existing);
      break;
    }
    if (existing.get() == geometry.get()) {
      break;
    }
    ++candidate;
  }
  if (std::none_of(
          geometry_candidates.begin(), geometry_candidates.end(),
          [&geometry](const auto &candidate) {
            const std::shared_ptr<const MeshGeometryPayload> existing =
                candidate.lock();
            return existing && existing.get() == geometry.get();
          })) {
    geometry_candidates.emplace_back(geometry);
  }

  if (!mesh.has_authored_polygon_document ||
      !mesh.authored_polygon_document) {
    return;
  }
  auto &authored_candidates = authored_topology_pool_[content_id];
  std::shared_ptr<const quader_poly::Document> authored =
      mesh.authored_polygon_document;
  for (auto candidate = authored_candidates.begin();
       candidate != authored_candidates.end();) {
    std::shared_ptr<const quader_poly::Document> existing = candidate->lock();
    if (!existing) {
      candidate = authored_candidates.erase(candidate);
      continue;
    }
    if (existing.get() == authored.get()) {
      break;
    }
    if (mesh_asset_payload_detail::same_polygon_document(*existing,
                                                         *authored)) {
      mesh.share_authored_polygon_document(existing);
      authored = std::move(existing);
      break;
    }
    ++candidate;
  }
  if (std::none_of(
          authored_candidates.begin(), authored_candidates.end(),
          [&authored](const auto &candidate) {
            const std::shared_ptr<const quader_poly::Document> existing =
                candidate.lock();
            return existing && existing.get() == authored.get();
          })) {
    authored_candidates.emplace_back(authored);
  }
}

void AssetRegistry::advance_revision_for_replacement(
    std::uint64_t previous_revision) {
  meshes_.restamp_after(MeshAssetRevision{previous_revision});
  materials_.restamp_after(MaterialAssetRevision{previous_revision});
  textures_.restamp_after(TextureAssetRevision{previous_revision});
  revision_ = std::max(revision_, previous_revision) + 1u;
}

void AssetRegistry::mark_dirty() { ++revision_; }

} // namespace quader
