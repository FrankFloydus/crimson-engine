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

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace quader {

class AssetRegistry;

/**
 * Owns an exact AssetRegistry restore point for one staged host projection.
 *
 * The checkpoint is intentionally owner-specific: editor adapters may use it
 * to make a cross-owner publication atomic, but they cannot inspect or mutate
 * registry internals through it.
 */
class AssetRegistryProjectionCheckpoint final {
public:
  AssetRegistryProjectionCheckpoint() = default;
  ~AssetRegistryProjectionCheckpoint() = default;
  AssetRegistryProjectionCheckpoint(
      const AssetRegistryProjectionCheckpoint &) noexcept = default;
  AssetRegistryProjectionCheckpoint &
  operator=(const AssetRegistryProjectionCheckpoint &) noexcept = default;
  AssetRegistryProjectionCheckpoint(
      AssetRegistryProjectionCheckpoint &&) noexcept = default;
  AssetRegistryProjectionCheckpoint &
  operator=(AssetRegistryProjectionCheckpoint &&) noexcept = default;

  [[nodiscard]] bool valid() const noexcept {
    return static_cast<bool>(snapshot_);
  }

private:
  friend class AssetRegistry;

  explicit AssetRegistryProjectionCheckpoint(
      std::shared_ptr<const AssetRegistry> snapshot)
      : snapshot_(std::move(snapshot)) {}

  std::shared_ptr<const AssetRegistry> snapshot_;
};

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
 * Pins either materialized mesh payloads or preserved encoded geometry for a
 * background writer. It is extracted on the registry owner thread.
 */
struct MeshAssetWriteSource final {
  MeshAssetDescriptor descriptor;
  std::shared_ptr<const MeshGeometryPayload> geometry;
  std::shared_ptr<const quader_poly::Document> authored_polygon_document;
  std::shared_ptr<const std::vector<std::byte>> encoded_geometry;
};

/// Reports an explicit failure instead of allowing save to omit an asset.
struct MeshAssetWriteSourceResult final {
  bool ok = false;
  MeshAssetWriteSource source;
  std::string message;
};

/**
 * Owns Asset Registry lookup and registration for the asset document and registry layer.
 */
class AssetRegistry {
public:
  AssetRegistry();
  AssetRegistry(const AssetRegistry &other);
  AssetRegistry &operator=(const AssetRegistry &other);
  AssetRegistry(AssetRegistry &&other) noexcept;
  AssetRegistry &operator=(AssetRegistry &&other) noexcept;

  AssetId add_mesh(MeshAsset mesh);
  AssetId add_mesh(AssetId id, MeshAsset mesh);
  AssetId add_deferred_mesh(AssetId id, std::string name,
                            bool has_stable_cpu_topology,
                            bool has_authored_polygon_document,
                            std::function<MeshAsset()> loader,
                            std::function<std::shared_ptr<const std::vector<
                                std::byte>>()> encoded_geometry_loader = {});
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
  [[nodiscard]] MeshAssetWriteSourceResult
  mesh_write_source(AssetId id) const;
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

  /** Captures all asset state before one staged editor projection. */
  [[nodiscard]] AssetRegistryProjectionCheckpoint
  capture_projection_checkpoint() const;
  /** Restores a staged editor projection without leaving revision or ID gaps. */
  [[nodiscard]] bool restore_projection_checkpoint(
      const AssetRegistryProjectionCheckpoint &checkpoint);
  /** Asset mutation/materialization is confined to this owning thread. */
  [[nodiscard]] bool on_owner_thread() const noexcept;

private:
  /**
   * Owns the immutable metadata and one-shot loader for a mesh whose geometry
   * has not been requested yet.
   */
  struct DeferredMesh final {
    bool has_stable_cpu_topology = true;
    bool has_authored_polygon_document = false;
    std::function<MeshAsset()> loader;
    std::function<std::shared_ptr<const std::vector<std::byte>>()>
        encoded_geometry_loader;
  };

  void assert_owner_thread() const;
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
  std::thread::id owner_thread_;
};

} // namespace quader
