////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/material_document.hpp"
#include "assets/curve_asset.hpp"
#include "assets/mesh_asset.hpp"
#include "assets/texture_asset.hpp"
#include "assets/typed_asset_store.hpp"
#include "core/types.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace quader {

class AssetRegistry;

using MeshAssetStore = TypedAssetStore<MeshAsset, MeshAssetRevision>;
using CurveAssetStore = TypedAssetStore<CurveAsset, CurveAssetRevision>;
using MaterialAssetStore = TypedAssetStore<MaterialDocument, MaterialAssetRevision>;
using TextureAssetStore = TypedAssetStore<TextureAsset, TextureAssetRevision>;

/**
 * Owns one bounded, allocator-exact checkpoint for mixed modeling assets.
 *
 * Mesh and Curve stores share AssetRegistry's ID allocator and owner revision,
 * so a mixed publication must restore both stores before rewinding those shared
 * scalars exactly once.
 */
class ModelingAssetProjectionCheckpoint final {
public:
  ModelingAssetProjectionCheckpoint() = default;
  [[nodiscard]] bool valid() const noexcept { return valid_; }
  [[nodiscard]] std::size_t mesh_count() const noexcept {
    std::size_t count = 0u;
    for (const auto &[id, document] : meshes_.documents) {
      (void)id;
      count += document.has_value() ? 1u : 0u;
    }
    return count;
  }
  [[nodiscard]] std::size_t curve_count() const noexcept {
    std::size_t count = 0u;
    for (const auto &[id, document] : curves_.documents) {
      (void)id;
      count += document.has_value() ? 1u : 0u;
    }
    return count;
  }
  [[nodiscard]] std::size_t participant_count() const noexcept {
    return mutation_count_by_asset_.size();
  }

private:
  friend class AssetRegistry;

  bool valid_ = false;
  const AssetRegistry *origin_ = nullptr;
  AssetId next_id_ = 1;
  std::uint64_t revision_ = 0;
  std::uint64_t mesh_material_usage_revision_ = 0;
  std::uint64_t modeling_mutation_count_ = 0;
  std::uint64_t unscoped_mutation_count_ = 0;
  std::uint64_t allocation_serial_ = 0;
  std::unordered_map<AssetId, std::uint64_t> mutation_count_by_asset_;
  MeshAssetStore::TargetedCheckpoint meshes_;
  CurveAssetStore::TargetedCheckpoint curves_;
};

/** Bounded counter-only fence for long-lived modeling preview participants. */
struct ModelingAssetProjectionMutationFenceWork final {
  std::size_t participant_reference_count = 0u;
  std::size_t participant_record_count = 0u;
  std::size_t mutation_counter_lookup_count = 0u;
};

/**
 * Tracks acknowledged per-asset modeling mutations without copying payloads.
 *
 * The retained host projection checkpoint remains the sole rollback payload.
 * Preview owners compare this fence before each sample and acknowledge only
 * mutations they just published on the owner thread.
 */
class ModelingAssetProjectionMutationFence final {
public:
  ModelingAssetProjectionMutationFence() = default;
  ModelingAssetProjectionMutationFence(
      const ModelingAssetProjectionMutationFence &) = delete;
  ModelingAssetProjectionMutationFence &
  operator=(const ModelingAssetProjectionMutationFence &) = delete;
  ModelingAssetProjectionMutationFence(
      ModelingAssetProjectionMutationFence &&other) noexcept;
  ModelingAssetProjectionMutationFence &
  operator=(ModelingAssetProjectionMutationFence &&other) noexcept;

  [[nodiscard]] bool valid() const noexcept { return origin_ != nullptr; }
  [[nodiscard]] const ModelingAssetProjectionMutationFenceWork &
  work() const noexcept {
    return work_;
  }
  [[nodiscard]] bool matches_current(
      const AssetRegistry &assets,
      ModelingAssetProjectionMutationFenceWork *work = nullptr) const;
  [[nodiscard]] bool acknowledge_current(
      const AssetRegistry &assets,
      std::span<const AssetId> newly_created = {},
      ModelingAssetProjectionMutationFenceWork *work = nullptr);

private:
  friend class AssetRegistry;
  /** Captures one enrolled asset's mutation count for participant-fence validation. */
  struct Record final {
    AssetId asset = kInvalidAssetId;
    std::uint64_t mutation_count = 0u;
  };

  const AssetRegistry *origin_ = nullptr;
  std::vector<Record> records_;
  ModelingAssetProjectionMutationFenceWork work_;
};

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

/// Pairs one existing mesh asset ID with its fully prepared replacement.
struct MeshAssetReplacement final {
  AssetId id = kInvalidAssetId;
  MeshAsset mesh;
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
  AssetId add_curve(CurveAsset curve);
  AssetId add_curve(AssetId id, CurveAsset curve);
  /**
   * Adds one logical mesh by reusing an already validated registry-owned
   * immutable payload. Only logical name/topology metadata may differ.
   */
  AssetId add_mesh_payload_alias(AssetId id, AssetId payload_source,
                                 std::string name,
                                 bool has_stable_cpu_topology);
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
  bool remove_curve(AssetId id);
  bool remove_material(AssetId id);
  bool remove_texture(AssetId id);
  bool replace_mesh(AssetId id, MeshAsset mesh);
  bool replace_curve(AssetId id, CurveAsset curve);
  /**
   * Atomically validates and replaces existing mesh payloads, then advances
   * the registry owner revision once for the complete publication.
   */
  bool replace_meshes(std::span<MeshAssetReplacement> replacements);
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
  validate_curve(const CurveAsset &curve) const;
  [[nodiscard]] AssetRegistryValidationResult
  validate_material(const MaterialDocument &material) const;

  const MeshAsset *mesh(AssetId id) const;
  const CurveAsset *curve(AssetId id) const;
  [[nodiscard]] bool has_curve(AssetId id) const;
  [[nodiscard]] bool has_mesh(AssetId id) const;
  [[nodiscard]] bool mesh_materialized(AssetId id) const;
  [[nodiscard]] std::optional<MeshAssetDescriptor>
  mesh_descriptor(AssetId id) const;
  [[nodiscard]] MeshAssetWriteSourceResult
  mesh_write_source(AssetId id) const;
  const MaterialDocument *material(AssetId id) const;
  const TextureAsset *texture(AssetId id) const;
  MeshAssetRevision mesh_revision(AssetId id) const;
  CurveAssetRevision curve_revision(AssetId id) const;
  MaterialAssetRevision material_revision(AssetId id) const;
  TextureAssetRevision texture_revision(AssetId id) const;

  std::size_t mesh_count() const { return meshes_.size(); }
  std::size_t curve_count() const { return curves_.size(); }
  std::size_t material_count() const { return materials_.size(); }
  std::size_t texture_count() const { return textures_.size(); }
  std::vector<AssetId> mesh_ids() const { return meshes_.ids(); }
  std::vector<AssetId> curve_ids() const { return curves_.ids(); }
  std::vector<AssetId> material_ids() const { return materials_.ids(); }
  std::vector<AssetId> texture_ids() const { return textures_.ids(); }
  std::uint64_t revision() const { return revision_; }
  /// Advances only when the embedded-material catalog or payload changes.
  std::uint64_t material_catalog_revision() const {
    return material_catalog_revision_;
  }
  /// Advances when mesh payloads that can change resolved face slots change.
  std::uint64_t mesh_material_usage_revision() const {
    return mesh_material_usage_revision_;
  }
  void advance_revision_for_replacement(std::uint64_t previous_revision);

  /** Captures named mesh and Curve assets against one shared allocator state. */
  [[nodiscard]] ModelingAssetProjectionCheckpoint
  capture_modeling_projection_checkpoint(
      std::span<const AssetId> modeling_asset_ids) const;
  /** Captures counter-only participant state for an acknowledged preview. */
  [[nodiscard]] ModelingAssetProjectionMutationFence
  capture_modeling_projection_mutation_fence(
      std::span<const AssetId> modeling_asset_ids) const;
  /** Validates an explicit exclusive allocation lease without mutation. */
  [[nodiscard]] bool can_restore_modeling_projection_checkpoint(
      const ModelingAssetProjectionCheckpoint &checkpoint,
      std::span<const AssetId> created_modeling_asset_ids) const;
  /** Restores only explicitly enrolled assets created during a long preview. */
  [[nodiscard]] bool restore_modeling_projection_checkpoint(
      const ModelingAssetProjectionCheckpoint &checkpoint,
      std::span<const AssetId> created_modeling_asset_ids);
  /**
   * Long-preview preflight that requires an acknowledged participant fence
   * and permits unrelated asset publications to survive the rollback.
   */
  [[nodiscard]] bool
  can_restore_modeling_projection_checkpoint_preserving_concurrent_changes(
      const ModelingAssetProjectionCheckpoint &checkpoint,
      const ModelingAssetProjectionMutationFence &acknowledged_fence,
      std::span<const AssetId> created_modeling_asset_ids) const;
  /**
   * Restores enrolled payloads only. Exact scalar rewind is retained when no
   * unrelated work occurred; otherwise owner revisions remain monotonic.
   */
  [[nodiscard]] bool
  restore_modeling_projection_checkpoint_preserving_concurrent_changes(
      const ModelingAssetProjectionCheckpoint &checkpoint,
      const ModelingAssetProjectionMutationFence &acknowledged_fence,
      std::span<const AssetId> created_modeling_asset_ids);
  /** Asset mutation/materialization is confined to this owning thread. */
  [[nodiscard]] bool on_owner_thread() const noexcept;

private:
  friend class ModelingAssetProjectionMutationFence;
  /** Lists asset identities affected by an exact modeling-projection rollback. */
  struct ModelingAssetProjectionRestorePlan final {
    std::vector<AssetId> created_asset_ids;
    std::vector<AssetId> created_mesh_ids;
    std::vector<AssetId> created_curve_ids;
    std::vector<AssetId> enrolled_asset_ids;
  };
  /** Partitions participant assets for rollback while preserving unrelated mutations. */
  struct ConcurrentModelingAssetProjectionRestorePlan final {
    std::vector<AssetId> created_mesh_ids;
    std::vector<AssetId> created_curve_ids;
    std::vector<AssetId> restore_asset_ids;
    std::vector<AssetId> changed_asset_ids;
    bool mesh_material_usage_dirty = false;
  };
  [[nodiscard]] std::optional<ModelingAssetProjectionRestorePlan>
  prepare_modeling_projection_restore(
      const ModelingAssetProjectionCheckpoint &checkpoint,
      std::span<const AssetId> created_modeling_asset_ids) const;
  void apply_modeling_projection_restore(
      const ModelingAssetProjectionCheckpoint &checkpoint,
      const ModelingAssetProjectionRestorePlan &plan);
  [[nodiscard]] std::optional<ConcurrentModelingAssetProjectionRestorePlan>
  prepare_modeling_projection_restore_preserving_concurrent_changes(
      const ModelingAssetProjectionCheckpoint &checkpoint,
      const ModelingAssetProjectionMutationFence &acknowledged_fence,
      std::span<const AssetId> created_modeling_asset_ids) const;
  void apply_modeling_projection_restore_preserving_concurrent_changes(
      const ModelingAssetProjectionCheckpoint &checkpoint,
      const ConcurrentModelingAssetProjectionRestorePlan &plan);
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
  void mark_material_dirty();
  void mark_mesh_material_usage_dirty();
  void mark_modeling_assets_dirty(std::span<const AssetId> asset_ids,
                                  bool mesh_material_usage_dirty);
  void note_asset_allocation(AssetId asset_id);
  [[nodiscard]] bool material_references_texture(AssetId texture) const;

  AssetId next_id_ = 1;
  std::uint64_t revision_ = 0;
  std::uint64_t modeling_mutation_count_ = 0;
  std::uint64_t unscoped_mutation_count_ = 0;
  std::unordered_map<AssetId, std::uint64_t> mutation_count_by_asset_;
  // Successful allocations receive a monotonic receipt. This serial and the
  // retained per-ID receipts deliberately never rewind during bounded
  // rollback; together they distinguish a transaction-created asset from a
  // later explicit reuse of the same numeric ID.
  std::uint64_t asset_allocation_serial_ = 0;
  std::unordered_map<AssetId, std::uint64_t>
      last_asset_allocation_serial_;
  std::uint64_t material_catalog_revision_ = 0;
  std::uint64_t mesh_material_usage_revision_ = 0;
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
  CurveAssetStore curves_;
  TextureAssetStore textures_;
  std::thread::id owner_thread_;
};

} // namespace quader
