////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/material_document.hpp"
#include "assets/mesh_asset.hpp"
#include "assets/texture_asset.hpp"
#include "assets/typed_asset_store.hpp"
#include "core/types.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace quader {

using MeshAssetStore = TypedAssetStore<MeshAsset, MeshAssetRevision>;
using MaterialAssetStore = TypedAssetStore<MaterialDocument, MaterialAssetRevision>;
using TextureAssetStore = TypedAssetStore<TextureAsset, TextureAssetRevision>;

/**
 * Stores asset-registry validation status for caller-facing asset mutations.
 */
struct AssetRegistryValidationResult {
    bool ok = true;
    std::string message;
};

/**
 * Stores asset-registry mutation status for owner-level asset edits.
 */
struct AssetRegistryMutationResult {
    bool ok = true;
    bool changed = false;
    std::string message;
};

/**
 * Describes one mesh asset without forcing its deferred geometry payload to
 * hydrate.
 */
struct MeshAssetDescriptor final {
  AssetId id = kInvalidAssetId;
  MeshAssetRevision revision;
  std::string name;
  bool has_stable_cpu_topology = true;
  bool has_authored_polygon_document = false;
  bool materialized = true;
};

/**
 * Owns Asset Registry lookup and registration for the asset document and registry layer.
 */
class AssetRegistry {
public:
  AssetId add_mesh(MeshAsset mesh);
  AssetId add_mesh(AssetId id, MeshAsset mesh);
  AssetId add_deferred_mesh(AssetId id, std::string name,
                            bool has_stable_cpu_topology,
                            bool has_authored_polygon_document,
                            std::function<MeshAsset()> loader);
  AssetId add_material(MaterialDocument material);
  AssetId add_material(AssetId id, MaterialDocument material);
  AssetId add_texture(TextureAsset texture);
  AssetId add_texture(AssetId id, TextureAsset texture);
  bool remove_mesh(AssetId id);
  bool remove_material(AssetId id);
  bool remove_texture(AssetId id);
  bool replace_mesh(AssetId id, MeshAsset mesh);
  bool replace_material(AssetId id, MaterialDocument material);
  bool replace_texture(AssetId id, TextureAsset texture);
  bool update_mesh(AssetId id, const std::function<void(MeshAsset &)> &update);
  bool update_material(AssetId id,
                       const std::function<void(MaterialDocument &)> &update);
  bool update_texture(AssetId id,
                      const std::function<void(TextureAsset &)> &update);
  [[nodiscard]] AssetRegistryMutationResult
  set_material_texture(AssetId material, std::string_view slot,
                       AssetId texture);
  [[nodiscard]] AssetRegistryValidationResult
  validate_mesh(const MeshAsset &mesh) const;
  [[nodiscard]] AssetRegistryValidationResult
  validate_material(const MaterialDocument &material) const;

  const MeshAsset *mesh(AssetId id) const;
  [[nodiscard]] bool has_mesh(AssetId id) const;
  [[nodiscard]] bool mesh_materialized(AssetId id) const;
  [[nodiscard]] std::optional<MeshAssetDescriptor>
  mesh_descriptor(AssetId id) const;
  const MaterialDocument *material(AssetId id) const;
  const TextureAsset *texture(AssetId id) const;
  MeshAssetRevision mesh_revision(AssetId id) const;
  MaterialAssetRevision material_revision(AssetId id) const;
  TextureAssetRevision texture_revision(AssetId id) const;

  std::size_t mesh_count() const { return meshes_.size(); }
  std::size_t material_count() const { return materials_.size(); }
  std::size_t texture_count() const { return textures_.size(); }
  std::vector<AssetId> mesh_ids() const { return meshes_.ids(); }
  std::vector<AssetId> material_ids() const { return materials_.ids(); }
  std::vector<AssetId> texture_ids() const { return textures_.ids(); }
  std::uint64_t revision() const { return revision_; }
  void advance_revision_for_replacement(std::uint64_t previous_revision);

private:
  /**
   * Owns the immutable metadata and one-shot loader for a mesh whose geometry
   * has not been requested yet.
   */
  struct DeferredMesh final {
    bool has_stable_cpu_topology = true;
    bool has_authored_polygon_document = false;
    std::function<MeshAsset()> loader;
  };

  void intern_mesh_payload(MeshAsset &mesh) const;
  void mark_dirty();

  AssetId next_id_ = 1;
  std::uint64_t revision_ = 0;
  mutable MeshAssetStore meshes_;
  mutable std::unordered_map<AssetId, DeferredMesh> deferred_meshes_;
  mutable std::unordered_map<
      std::uint64_t,
      std::vector<std::weak_ptr<const MeshGeometryPayload>>>
      mesh_geometry_pool_;
  mutable std::unordered_map<
      std::uint64_t,
      std::vector<std::weak_ptr<const quader_poly::Document>>>
      authored_topology_pool_;
  MaterialAssetStore materials_;
  TextureAssetStore textures_;
};

} // namespace quader
