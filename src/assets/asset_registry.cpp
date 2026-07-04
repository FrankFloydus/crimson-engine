////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "assets/asset_registry.hpp"

#include <algorithm>
#include <optional>
#include <utility>

namespace quader {

AssetId AssetRegistry::add_mesh(MeshAsset mesh) {
  const AssetId id = next_id_++;
  meshes_.add(id, std::move(mesh));
  mark_dirty();
  return id;
}

AssetId AssetRegistry::add_mesh(AssetId id, MeshAsset mesh) {
  if (id == kInvalidAssetId) {
    return add_mesh(std::move(mesh));
  }
  if (meshes_.contains(id)) {
    return kInvalidAssetId;
  }
  next_id_ = std::max(next_id_, id + 1u);
  meshes_.add(id, std::move(mesh));
  mark_dirty();
  return id;
}

AssetId AssetRegistry::add_material(MaterialDocument material) {
  if (!validate_material(material).ok) {
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
  if (!meshes_.replace(id, std::move(mesh))) {
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
  if (!meshes_.update(id, update)) {
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
AssetRegistry::validate_material(const MaterialDocument &material) const {
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

void AssetRegistry::mark_dirty() { ++revision_; }

} // namespace quader
