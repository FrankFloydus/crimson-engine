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

AssetId AssetRegistry::add_mesh(MeshAsset mesh) {
  if (next_id_ == kInvalidAssetId || !validate_mesh(mesh).ok) {
    return kInvalidAssetId;
  }
  const AssetId id = next_id_++;
  meshes_.add(id, isolate_mesh_asset_payload(std::move(mesh)));
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
  meshes_.add(id, isolate_mesh_asset_payload(std::move(mesh)));
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
  if (!validate_mesh(mesh).ok ||
      !meshes_.replace(id, isolate_mesh_asset_payload(std::move(mesh)))) {
    return false;
  }
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
  const MeshAsset *existing = meshes_.get(id);
  if (existing == nullptr) {
    return false;
  }
  MeshAsset updated = clone_mesh_asset_payload(*existing);
  update(updated);
  if (!validate_mesh(updated).ok ||
      !meshes_.replace(id, isolate_mesh_asset_payload(std::move(updated)))) {
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

  for (const MeshVertex &vertex : mesh.vertices) {
    if (!finite_vec3(vertex.position) || !finite_vec3(vertex.normal) ||
        !finite_vec2(vertex.uv)) {
      return {.ok = false, .message = "Mesh vertex data must be finite"};
    }
  }
  if (mesh.indices.size() % 3u != 0u) {
    return {.ok = false,
            .message = "Mesh indices must describe complete triangles"};
  }
  for (const std::uint32_t index : mesh.indices) {
    if (index >= mesh.vertices.size()) {
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
    return meshes_.get(id);
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

void AssetRegistry::advance_revision_for_replacement(
    std::uint64_t previous_revision) {
  meshes_.restamp_after(MeshAssetRevision{previous_revision});
  materials_.restamp_after(MaterialAssetRevision{previous_revision});
  textures_.restamp_after(TextureAssetRevision{previous_revision});
  revision_ = std::max(revision_, previous_revision) + 1u;
}

void AssetRegistry::mark_dirty() { ++revision_; }

} // namespace quader
