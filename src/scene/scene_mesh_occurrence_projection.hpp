////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"
#include "math/types/qmat4.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace quader {

class SceneDocument;

/** Stable semantic identity for one concrete scene mesh placement. */
struct SceneMeshOccurrenceIdentity final {
  EntityId display_entity = kInvalidEntityId;
  EntityId definition_entity = kInvalidEntityId;
  EntityId geometry_entity = kInvalidEntityId;

  friend bool operator==(const SceneMeshOccurrenceIdentity &,
                         const SceneMeshOccurrenceIdentity &) = default;
  friend bool operator<(const SceneMeshOccurrenceIdentity &left,
                        const SceneMeshOccurrenceIdentity &right) noexcept {
    return std::tie(left.display_entity, left.definition_entity,
                    left.geometry_entity) <
           std::tie(right.display_entity, right.definition_entity,
                    right.geometry_entity);
  }
};

/** Selects whether hidden concrete mesh placements remain in a snapshot. */
enum class SceneMeshOccurrenceVisibility : std::uint8_t {
  VisibleOnly,
  IncludeHidden,
};

/** One ordinary mesh or expanded formal-instance placement. */
struct SceneMeshOccurrenceProjectionRecord final {
  EntityId display_entity = kInvalidEntityId;
  EntityId geometry_entity = kInvalidEntityId;
  EntityId definition_entity = kInvalidEntityId;
  QMat4 world_from_geometry;
  bool selectable = true;
  bool effectively_locked = false;
  /** Occurrence hierarchy and member segment visibility, excluding storage. */
  bool effectively_visible = true;

  [[nodiscard]] bool formal_instance() const noexcept {
    return definition_entity != kInvalidEntityId;
  }
  [[nodiscard]] SceneMeshOccurrenceIdentity identity() const noexcept {
    return {
        .display_entity = display_entity,
        .definition_entity = definition_entity,
        .geometry_entity = geometry_entity,
    };
  }
};

/**
 * Groups the included placements of one canonical definition mesh.
 *
 * Groups retain definition/member authored order and participant indices retain
 * occurrence entity order. Render extraction can therefore keep its existing
 * instancing and draw ordering without rebuilding hierarchy facts.
 */
struct SceneMeshOccurrenceGeometryGroup final {
  EntityId definition_entity = kInvalidEntityId;
  EntityId geometry_entity = kInvalidEntityId;
  QMat4 definition_from_geometry;
  std::vector<std::size_t> participant_indices;
};

/** Counts deterministic work performed by one mesh occurrence projection. */
struct SceneMeshOccurrenceProjectionWorkStats final {
  std::uint64_t ordinary_placement_count = 0u;
  std::uint64_t formal_geometry_group_count = 0u;
  std::uint64_t formal_placement_count = 0u;
  std::uint64_t world_transform_multiply_count = 0u;
  /** Singular definitions that omit at least one included mesh placement. */
  std::uint64_t singular_definition_count = 0u;
};

/**
 * Immutable scene-owned projection of concrete mesh placements.
 *
 * `records()` uses display-entity order, then authored member order. This is
 * the stable spatial/picking traversal. `formal_placement_indices()` preserves
 * definition -> occurrence -> member order for consumers whose retained output
 * historically used that order. Local Visibility is deliberately absent and
 * must be applied to each record's `display_entity` by the consumer.
 */
class SceneMeshOccurrenceProjectionSnapshot final {
public:
  [[nodiscard]] std::span<const SceneMeshOccurrenceProjectionRecord>
  records() const noexcept {
    return records_;
  }
  [[nodiscard]] std::span<const std::size_t>
  ordinary_placement_indices() const noexcept {
    return ordinary_placement_indices_;
  }
  [[nodiscard]] std::span<const std::size_t>
  formal_placement_indices() const noexcept {
    return formal_placement_indices_;
  }
  [[nodiscard]] std::span<const SceneMeshOccurrenceGeometryGroup>
  formal_geometry_groups() const noexcept {
    return formal_geometry_groups_;
  }
  [[nodiscard]] std::span<const SceneMeshOccurrenceProjectionRecord>
  records_for_display(EntityId display_entity) const noexcept;
  [[nodiscard]] const SceneMeshOccurrenceProjectionRecord *
  find(SceneMeshOccurrenceIdentity identity) const noexcept;
  [[nodiscard]] const SceneMeshOccurrenceProjectionRecord *
  record(std::size_t index) const noexcept;

private:
  friend SceneMeshOccurrenceProjectionSnapshot
  build_scene_mesh_occurrence_projection(
      const SceneDocument &, SceneMeshOccurrenceProjectionWorkStats *,
      SceneMeshOccurrenceVisibility);

  /** Maps one display entity to its contiguous occurrence records. */
  struct DisplayRange final {
    std::size_t begin = 0u;
    std::size_t count = 0u;
  };

  /** Hashes stable occurrence identities for deterministic lookup. */
  struct IdentityHash final {
    [[nodiscard]] std::size_t
    operator()(const SceneMeshOccurrenceIdentity &identity) const noexcept;
  };

  std::vector<SceneMeshOccurrenceProjectionRecord> records_;
  std::vector<std::size_t> ordinary_placement_indices_;
  std::vector<std::size_t> formal_placement_indices_;
  std::vector<SceneMeshOccurrenceGeometryGroup> formal_geometry_groups_;
  std::unordered_map<EntityId, DisplayRange> display_ranges_;
  std::unordered_map<SceneMeshOccurrenceIdentity, std::size_t, IdentityHash>
      index_by_identity_;
};

/**
 * Builds one scene-only projection without AssetRegistry or viewport policy.
 * Hidden placements can be retained for export without changing scene visibility
 * or the renderer's default visible-only scene-owned cache.
 */
[[nodiscard]] SceneMeshOccurrenceProjectionSnapshot
build_scene_mesh_occurrence_projection(
    const SceneDocument &document,
    SceneMeshOccurrenceProjectionWorkStats *work_stats = nullptr,
    SceneMeshOccurrenceVisibility visibility =
        SceneMeshOccurrenceVisibility::VisibleOnly);

} // namespace quader
