////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"
#include "math/types.hpp"
#include "scene/scene_bounds.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>

namespace quader {

class AssetRegistry;
class SceneDocument;
class ViewportLocalVisibilityService;

/// Identifies one concrete visible placement of one mesh geometry source.
struct SceneSurfaceParticipantId final {
  EntityId display_entity = kInvalidEntityId;
  EntityId geometry_entity = kInvalidEntityId;
  EntityId definition_entity = kInvalidEntityId;

  [[nodiscard]] bool
  operator==(const SceneSurfaceParticipantId &) const = default;
};

/** Defines the canonical ordering used by participants and exclusions. */
[[nodiscard]] inline constexpr bool scene_surface_participant_id_less(
    const SceneSurfaceParticipantId &left,
    const SceneSurfaceParticipantId &right) noexcept {
  if (left.display_entity != right.display_entity) {
    return left.display_entity < right.display_entity;
  }
  if (left.definition_entity != right.definition_entity) {
    return left.definition_entity < right.definition_entity;
  }
  return left.geometry_entity < right.geometry_entity;
}

/// Describes immutable world-space facts for one frozen surface participant.
struct SceneSurfaceParticipant final {
  SceneSurfaceParticipantId id;
  SceneBounds world_bounds{};
  std::size_t triangle_count = 0u;
};

/// Carries one downward or arbitrary world-space surface query.
struct SceneSurfaceRaycastRequest final {
  QVec3 world_origin{};
  QVec3 world_direction{0.0f, -1.0f, 0.0f};
  float min_distance = 0.0f;
  float max_distance = std::numeric_limits<float>::infinity();
  // Must be sorted by scene_surface_participant_id_less and contain no
  // duplicates. The query uses binary search and does not revalidate the span
  // on every ray.
  std::span<const SceneSurfaceParticipantId> excluded_participants;
};

/// Reports the closest deterministic triangle hit in one frozen snapshot.
struct SceneSurfaceHit final {
  SceneSurfaceParticipantId participant;
  QVec3 world_position{};
  QVec3 world_normal{};
  float distance = 0.0f;
  std::size_t primitive_index = 0u;
};

/// Reports query-cache work without affecting placement behavior.
struct SceneSurfaceQueryStats final {
  std::uint64_t snapshot_build_count = 0u;
  std::uint64_t tlas_build_count = 0u;
  std::uint64_t blas_build_count = 0u;
  std::size_t participant_count = 0u;
  std::size_t triangle_count = 0u;
  std::size_t unique_geometry_count = 0u;
};

/**
 * Retains neutral TLAS/BLAS traversal stacks across scene-surface raycasts.
 *
 * Use one scratch per sequential query batch. It is not thread-safe; callers
 * issuing concurrent queries must provide one scratch instance per thread.
 */
class SceneSurfaceQueryScratch final {
public:
  SceneSurfaceQueryScratch();
  ~SceneSurfaceQueryScratch();

  SceneSurfaceQueryScratch(const SceneSurfaceQueryScratch &) = delete;
  SceneSurfaceQueryScratch &
  operator=(const SceneSurfaceQueryScratch &) = delete;
  SceneSurfaceQueryScratch(SceneSurfaceQueryScratch &&) noexcept;
  SceneSurfaceQueryScratch &
  operator=(SceneSurfaceQueryScratch &&) noexcept;

private:
  friend class SceneSurfaceQuerySnapshot;
  class Impl;
  std::unique_ptr<Impl> impl_;
};

/**
 * Pins one immutable visible-surface index so every ray in an operation sees
 * the same receivers even when the operation resolves several sources.
 */
class SceneSurfaceQuerySnapshot final {
public:
  struct Data;

  SceneSurfaceQuerySnapshot() = default;

  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] std::span<const SceneSurfaceParticipant>
  participants() const noexcept;
  [[nodiscard]] std::optional<SceneSurfaceHit>
  nearest_hit(const SceneSurfaceRaycastRequest &request,
              SceneSurfaceQueryScratch &scratch) const;
  [[nodiscard]] std::optional<SceneSurfaceHit>
  nearest_hit(const SceneSurfaceRaycastRequest &request) const;

private:
  friend class SceneSurfaceQuery;

  explicit SceneSurfaceQuerySnapshot(std::shared_ptr<const Data> data);
  std::shared_ptr<const Data> data_;
};

/**
 * Owns the editor-side scene/asset adapter for neutral triangle BVHs.
 *
 * Receivers mirror render visibility: ordinary meshes and every visible
 * formal-instance occurrence participate, including locked/non-selectable
 * meshes. Viewport Local Visibility is the only additional filter.
 */
class SceneSurfaceQuery final {
public:
  SceneSurfaceQuery(const SceneDocument &scene, const AssetRegistry &assets,
                    const ViewportLocalVisibilityService &local_visibility);
  ~SceneSurfaceQuery();

  SceneSurfaceQuery(const SceneSurfaceQuery &) = delete;
  SceneSurfaceQuery &operator=(const SceneSurfaceQuery &) = delete;
  SceneSurfaceQuery(SceneSurfaceQuery &&) noexcept;
  SceneSurfaceQuery &operator=(SceneSurfaceQuery &&) noexcept;

  [[nodiscard]] SceneSurfaceQuerySnapshot snapshot() const;
  [[nodiscard]] SceneSurfaceQueryStats stats() const noexcept;
  void invalidate() noexcept;

private:
  [[nodiscard]] static SceneSurfaceQuerySnapshot snapshot_from_data(
      std::shared_ptr<const SceneSurfaceQuerySnapshot::Data> data);

  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace quader
