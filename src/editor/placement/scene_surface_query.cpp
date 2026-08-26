////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/placement/scene_surface_query.hpp"

#include "assets/asset_registry.hpp"
#include "assets/mesh_asset_payload.hpp"
#include "diagnostics/profile.hpp"
#include "editor/session/viewport_local_visibility_service.hpp"
#include "math/glm_bridge.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_hierarchy_visibility.hpp"
#include "scene/scene_transform.hpp"
#include "viewport/viewport_local_visibility.hpp"

#include <quader/modeling/neutral/geometry/bvh.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <numeric>
#include <ranges>
#include <unordered_set>
#include <utility>
#include <vector>

namespace quader {
namespace {

using BoundsBvh = quader_geometry::QBoundsBvh<float>;
using NeutralBounds = quader_geometry::QAabb3<float>;
using NeutralRay = quader_geometry::QRay3<float>;
using NeutralTriangle = quader_geometry::QTriangle3<float>;
using NeutralVector = quader_geometry::QBasicVec3<float>;
using TriangleBvh = quader_geometry::QTriangleBvh<float>;

constexpr float kRayEpsilon = 0.000001f;
constexpr float kDistanceTieTolerance = 0.000001f;
// Retain enough recently used payloads for common hide/isolate workflows
// without allowing transient project geometry to grow the service cache
// without bound. Live snapshots pin any additional geometry they need.
constexpr std::size_t kGeometryCacheCapacity = 64u;

[[nodiscard]] bool finite_vector(QVec3 value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

[[nodiscard]] bool valid_bounds(const SceneBounds &bounds) {
  return finite_vector(bounds.min) && finite_vector(bounds.max) &&
         bounds.min.x <= bounds.max.x && bounds.min.y <= bounds.max.y &&
         bounds.min.z <= bounds.max.z;
}

[[nodiscard]] NeutralVector neutral_vector(QVec3 value) {
  return {value.x, value.y, value.z};
}

[[nodiscard]] NeutralBounds neutral_bounds(const SceneBounds &bounds) {
  return NeutralBounds{neutral_vector(bounds.min),
                       neutral_vector(bounds.max)};
}

[[nodiscard]] bool excluded_participant(
    SceneSurfaceParticipantId id,
    std::span<const SceneSurfaceParticipantId> exclusions) {
  const auto found = std::ranges::lower_bound(
      exclusions, id,
      [](const SceneSurfaceParticipantId &left,
         const SceneSurfaceParticipantId &right) {
        return scene_surface_participant_id_less(left, right);
      });
  return found != exclusions.end() && *found == id;
}

[[nodiscard]] std::optional<SceneBounds> transformed_payload_bounds(
    const MeshGeometryPayload &geometry, const QMat4 &world_from_local) {
  if (geometry.vertices.empty() || !scene_matrix_is_finite(world_from_local)) {
    return std::nullopt;
  }
  const QVec3 first =
      transform_point(world_from_local, geometry.vertices.front().position);
  if (!finite_vector(first)) {
    return std::nullopt;
  }
  SceneBounds bounds{.min = first, .max = first};
  for (std::size_t index = 1u; index < geometry.vertices.size(); ++index) {
    const QVec3 point =
        transform_point(world_from_local, geometry.vertices[index].position);
    if (!finite_vector(point)) {
      return std::nullopt;
    }
    include_point(bounds, point);
  }
  return bounds;
}

/// Pins one immutable mesh payload together with its local triangle BLAS.
struct CachedSurfaceGeometry final {
  std::shared_ptr<const MeshGeometryPayload> payload;
  std::vector<NeutralTriangle> triangles;
  TriangleBvh bvh;
};

/// Stores one participant's frozen transforms and shared local geometry.
struct FrozenSurfaceParticipant final {
  SceneSurfaceParticipant public_record;
  QMat4 world_from_local;
  QMat4 local_from_world;
  std::shared_ptr<const MeshGeometryPayload> pinned_payload;
  std::shared_ptr<const CachedSurfaceGeometry> geometry;
};

/// Identifies every owner revision that can change surface participation.
struct SurfaceSnapshotFingerprint final {
  const SceneDocument *scene = nullptr;
  const AssetRegistry *assets = nullptr;
  std::uint64_t scene_content_revision = 0u;
  std::uint64_t asset_revision = 0u;
  std::uint64_t local_visibility_revision = 0u;

  [[nodiscard]] bool
  operator==(const SurfaceSnapshotFingerprint &) const = default;
};

/// Stores one bounded strong-cache entry and deterministic LRU sequence.
struct GeometryCacheEntry final {
  MeshGeometryContentId content_id;
  std::shared_ptr<const CachedSurfaceGeometry> geometry;
  std::uint64_t last_use_sequence = 0u;
};

} // namespace

/// Owns the reusable neutral traversal stacks hidden behind the native API.
class SceneSurfaceQueryScratch::Impl final {
public:
  quader_geometry::QBvhTraversalScratch tlas;
  quader_geometry::QBvhTraversalScratch blas;
};

/// Owns the immutable participant records and world-space hierarchy.
struct SceneSurfaceQuerySnapshot::Data final {
  std::vector<SceneSurfaceParticipant> public_participants;
  std::vector<FrozenSurfaceParticipant> participants;
  BoundsBvh tlas;
};

/// Adapts scene, asset, instance, and local-visibility state into BVHs.
class SceneSurfaceQuery::Impl final {
public:
  Impl(const SceneDocument &scene, const AssetRegistry &assets,
       const ViewportLocalVisibilityService &local_visibility)
      : scene_(scene), assets_(assets), local_visibility_(local_visibility) {}

  [[nodiscard]] SceneSurfaceQuerySnapshot snapshot() const {
    const ViewportLocalVisibilitySnapshot local_snapshot =
        local_visibility_.snapshot();
    const SurfaceSnapshotFingerprint fingerprint{
        .scene = &scene_,
        .assets = &assets_,
        .scene_content_revision = scene_.content_revision(),
        .asset_revision = assets_.revision(),
        .local_visibility_revision = local_snapshot.revision(),
    };
    if (snapshot_ && fingerprint_valid_ && fingerprint == fingerprint_) {
      return SceneSurfaceQuery::snapshot_from_data(snapshot_);
    }

    QDR_PROFILE_SCOPE("Scene surface query build snapshot");
    std::shared_ptr<SceneSurfaceQuerySnapshot::Data> next =
        std::make_shared<SceneSurfaceQuerySnapshot::Data>();
    build_participants(local_snapshot, *next);
    build_tlas(*next);
    snapshot_ = std::move(next);
    fingerprint_ = fingerprint;
    fingerprint_valid_ = true;
    ++stats_.snapshot_build_count;
    ++stats_.tlas_build_count;
    stats_.participant_count = snapshot_->participants.size();
    stats_.triangle_count = 0u;
    std::unordered_set<const CachedSurfaceGeometry *> unique_geometries;
    unique_geometries.reserve(snapshot_->participants.size());
    for (const FrozenSurfaceParticipant &participant :
         snapshot_->participants) {
      stats_.triangle_count += participant.public_record.triangle_count;
      unique_geometries.insert(participant.geometry.get());
    }
    stats_.unique_geometry_count = unique_geometries.size();
    trim_geometry_cache();
    return SceneSurfaceQuery::snapshot_from_data(snapshot_);
  }

  [[nodiscard]] SceneSurfaceQueryStats stats() const noexcept {
    return stats_;
  }

  void invalidate() noexcept {
    snapshot_.reset();
    fingerprint_valid_ = false;
    stats_.participant_count = 0u;
    stats_.triangle_count = 0u;
    stats_.unique_geometry_count = 0u;
  }

private:
  [[nodiscard]] std::shared_ptr<const CachedSurfaceGeometry>
  geometry_for(std::shared_ptr<const MeshGeometryPayload> payload) const {
    const MeshGeometryContentId content_id = payload->content_id();
    for (GeometryCacheEntry &entry : geometry_cache_) {
      if (!(entry.content_id == content_id)) {
        continue;
      }
      const std::shared_ptr<const CachedSurfaceGeometry> &candidate =
          entry.geometry;
      if (candidate &&
          (candidate->payload.get() == payload.get() ||
           mesh_geometry_payload_equal(*candidate->payload, *payload))) {
        entry.last_use_sequence = ++geometry_use_sequence_;
        return candidate;
      }
    }

    auto geometry = std::make_shared<CachedSurfaceGeometry>();
    geometry->payload = std::move(payload);
    const MeshGeometryPayload &source = *geometry->payload;
    geometry->triangles.reserve(source.indices.size() / 3u);
    std::vector<std::size_t> primitive_indices;
    primitive_indices.reserve(source.indices.size() / 3u);
    for (std::size_t offset = 0u; offset + 2u < source.indices.size();
         offset += 3u) {
      const std::uint32_t first = source.indices[offset];
      const std::uint32_t second = source.indices[offset + 1u];
      const std::uint32_t third = source.indices[offset + 2u];
      if (first >= source.vertices.size() ||
          second >= source.vertices.size() || third >= source.vertices.size()) {
        continue;
      }
      geometry->triangles.push_back({
          neutral_vector(source.vertices[first].position),
          neutral_vector(source.vertices[second].position),
          neutral_vector(source.vertices[third].position),
      });
      primitive_indices.push_back(offset / 3u);
    }
    if (!geometry->bvh.build(geometry->triangles, primitive_indices)) {
      geometry->triangles.clear();
      geometry->bvh.clear();
    }
    geometry_cache_.push_back({
        .content_id = content_id,
        .geometry = geometry,
        .last_use_sequence = ++geometry_use_sequence_,
    });
    ++stats_.blas_build_count;
    return geometry;
  }

  void append_participant(
      SceneSurfaceQuerySnapshot::Data &target,
      SceneSurfaceParticipantId id, const QMat4 &world_from_local,
      const MeshAsset &mesh) const {
    const std::optional<QMat4> local_from_world =
        try_scene_matrix_inverse(world_from_local);
    std::shared_ptr<const MeshGeometryPayload> payload =
        mesh.geometry_payload();
    const std::optional<SceneBounds> bounds =
        local_from_world.has_value()
            ? transformed_payload_bounds(*payload, world_from_local)
            : std::nullopt;
    if (!local_from_world.has_value() || !bounds.has_value() ||
        !valid_bounds(*bounds)) {
      return;
    }
    std::shared_ptr<const CachedSurfaceGeometry> geometry =
        geometry_for(payload);
    target.participants.push_back({
        .public_record =
            {.id = id,
             .world_bounds = *bounds,
             .triangle_count = geometry->bvh.primitive_count()},
        .world_from_local = world_from_local,
        .local_from_world = *local_from_world,
        .pinned_payload = std::move(payload),
        .geometry = std::move(geometry),
    });
  }

  void build_participants(
      const ViewportLocalVisibilitySnapshot &local_snapshot,
      SceneSurfaceQuerySnapshot::Data &target) const {
    QDR_PROFILE_SCOPE("Scene surface query collect participants");
    for (const EntityId entity : scene_.entities()) {
      if (!scene_.is_effectively_visible(entity) ||
          !local_snapshot.allows(entity) ||
          scene_.is_instance_definition_entity(entity) ||
          scene_.is_instance_definition_member(entity) ||
          scene_.instance_occurrence(entity) != nullptr) {
        continue;
      }
      const MeshComponent *component = scene_.mesh(entity);
      const MeshAsset *mesh =
          component != nullptr ? assets_.mesh(component->mesh) : nullptr;
      if (mesh == nullptr) {
        continue;
      }
      append_participant(
          target,
          {.display_entity = entity,
           .geometry_entity = entity,
           .definition_entity = kInvalidEntityId},
          scene_.world_transform_matrix(entity), *mesh);
    }

    for (const EntityId definition : scene_.entities()) {
      if (!scene_.is_instance_definition_entity(definition)) {
        continue;
      }
      const std::optional<QMat4> definition_inverse =
          try_scene_matrix_inverse(scene_.world_transform_matrix(definition));
      if (!definition_inverse.has_value()) {
        continue;
      }
      std::vector<EntityId> visible_occurrences;
      for (const EntityId occurrence :
           scene_.instance_occurrences_for_definition(definition)) {
        if (scene_.is_effectively_visible(occurrence) &&
            local_snapshot.allows(occurrence)) {
          visible_occurrences.push_back(occurrence);
        }
      }
      if (visible_occurrences.empty()) {
        continue;
      }
      for (const EntityId member : scene_.descendants(definition, false)) {
        if (scene_.containing_instance_definition(member) != definition ||
            !scene_hierarchy_segment_is_visible(scene_, member, definition)) {
          continue;
        }
        const MeshComponent *component = scene_.mesh(member);
        const MeshAsset *mesh =
            component != nullptr ? assets_.mesh(component->mesh) : nullptr;
        if (mesh == nullptr) {
          continue;
        }
        const QMat4 member_from_definition = scene_transform_multiply(
            *definition_inverse, scene_.world_transform_matrix(member));
        for (const EntityId occurrence : visible_occurrences) {
          append_participant(
              target,
              {.display_entity = occurrence,
               .geometry_entity = member,
               .definition_entity = definition},
              scene_transform_multiply(
                  scene_.world_transform_matrix(occurrence),
                  member_from_definition),
              *mesh);
        }
      }
    }

    std::ranges::sort(
        target.participants,
        [](const FrozenSurfaceParticipant &left,
           const FrozenSurfaceParticipant &right) {
          return scene_surface_participant_id_less(left.public_record.id,
                                                   right.public_record.id);
        });
    target.public_participants.reserve(target.participants.size());
    for (const FrozenSurfaceParticipant &participant : target.participants) {
      target.public_participants.push_back(participant.public_record);
    }
  }

  void build_tlas(SceneSurfaceQuerySnapshot::Data &target) const {
    QDR_PROFILE_SCOPE("Scene surface query build TLAS");
    std::vector<NeutralBounds> bounds;
    std::vector<NeutralVector> centroids;
    std::vector<std::size_t> participant_indices(target.participants.size());
    bounds.reserve(target.participants.size());
    centroids.reserve(target.participants.size());
    std::iota(participant_indices.begin(), participant_indices.end(), 0u);
    for (const FrozenSurfaceParticipant &participant : target.participants) {
      bounds.push_back(neutral_bounds(participant.public_record.world_bounds));
      centroids.push_back(neutral_vector(
          bounds_center(participant.public_record.world_bounds)));
    }
    if (!target.tlas.build(bounds, centroids, participant_indices)) {
      target.tlas.clear();
    }
  }

  void trim_geometry_cache() const {
    while (geometry_cache_.size() > kGeometryCacheCapacity) {
      const auto victim = std::min_element(
          geometry_cache_.begin(), geometry_cache_.end(),
          [](const GeometryCacheEntry &left,
             const GeometryCacheEntry &right) {
            return left.last_use_sequence < right.last_use_sequence;
          });
      geometry_cache_.erase(victim);
    }
  }

  const SceneDocument &scene_;
  const AssetRegistry &assets_;
  const ViewportLocalVisibilityService &local_visibility_;
  mutable std::vector<GeometryCacheEntry> geometry_cache_;
  mutable std::uint64_t geometry_use_sequence_ = 0u;
  mutable std::shared_ptr<const SceneSurfaceQuerySnapshot::Data> snapshot_;
  mutable SurfaceSnapshotFingerprint fingerprint_;
  mutable bool fingerprint_valid_ = false;
  mutable SceneSurfaceQueryStats stats_;
};

SceneSurfaceQuerySnapshot::SceneSurfaceQuerySnapshot(
    std::shared_ptr<const Data> data)
    : data_(std::move(data)) {}

SceneSurfaceQueryScratch::SceneSurfaceQueryScratch()
    : impl_(std::make_unique<Impl>()) {}

SceneSurfaceQueryScratch::~SceneSurfaceQueryScratch() = default;
SceneSurfaceQueryScratch::SceneSurfaceQueryScratch(
    SceneSurfaceQueryScratch &&) noexcept = default;
SceneSurfaceQueryScratch &SceneSurfaceQueryScratch::operator=(
    SceneSurfaceQueryScratch &&) noexcept = default;

bool SceneSurfaceQuerySnapshot::valid() const noexcept {
  return static_cast<bool>(data_);
}

std::span<const SceneSurfaceParticipant>
SceneSurfaceQuerySnapshot::participants() const noexcept {
  return data_ ? std::span<const SceneSurfaceParticipant>(
                     data_->public_participants)
               : std::span<const SceneSurfaceParticipant>{};
}

std::optional<SceneSurfaceHit> SceneSurfaceQuerySnapshot::nearest_hit(
    const SceneSurfaceRaycastRequest &request) const {
  SceneSurfaceQueryScratch scratch;
  return nearest_hit(request, scratch);
}

std::optional<SceneSurfaceHit> SceneSurfaceQuerySnapshot::nearest_hit(
    const SceneSurfaceRaycastRequest &request,
    SceneSurfaceQueryScratch &scratch) const {
  if (!data_ || !finite_vector(request.world_origin) ||
      !finite_vector(request.world_direction) ||
      !std::isfinite(request.min_distance) ||
      std::isnan(request.max_distance) || request.min_distance < 0.0f ||
      request.max_distance < request.min_distance) {
    return std::nullopt;
  }
  const QVec3 world_direction = normalize_or_zero(request.world_direction);
  if (length_squared(world_direction) <= kRayEpsilon * kRayEpsilon) {
    return std::nullopt;
  }

  const NeutralRay world_ray{neutral_vector(request.world_origin),
                             neutral_vector(world_direction)};
  quader_geometry::QBvhRayQueryOptions<float> tlas_options;
  tlas_options.min_distance = request.min_distance;
  tlas_options.max_distance = request.max_distance;
  tlas_options.intersection_epsilon = kRayEpsilon;
  if (!scratch.impl_) {
    scratch.impl_ = std::make_unique<SceneSurfaceQueryScratch::Impl>();
  }
  std::optional<SceneSurfaceHit> best;
  (void)data_->tlas.for_each_ray_candidate(
      world_ray, tlas_options, scratch.impl_->tlas,
      [this, &request, &world_direction, &best, &scratch](
          std::size_t, std::size_t participant_index,
          const quader_geometry::QRayAabbHit<float> &,
          float &traversal_max_distance) {
        const FrozenSurfaceParticipant &participant =
            data_->participants[participant_index];
        if (excluded_participant(participant.public_record.id,
                                 request.excluded_participants) ||
            participant.geometry->bvh.empty()) {
          return true;
        }
        const QVec3 local_origin = transform_point(
            participant.local_from_world, request.world_origin);
        const QVec3 local_direction = transform_vector(
            participant.local_from_world, world_direction);
        if (!finite_vector(local_origin) || !finite_vector(local_direction) ||
            length_squared(local_direction) <= kRayEpsilon * kRayEpsilon) {
          return true;
        }
        quader_geometry::QTriangleBvhQueryOptions<float> options;
        options.min_distance = request.min_distance;
        options.max_distance = traversal_max_distance;
        options.intersection_epsilon = kRayEpsilon;
        options.tie_tolerance = kDistanceTieTolerance;
        const quader_geometry::QTriangleBvhHit<float> local_hit =
            participant.geometry->bvh.nearest_hit(
                NeutralRay{neutral_vector(local_origin),
                           neutral_vector(local_direction)},
                options, scratch.impl_->blas);
        if (!local_hit.hit ||
            local_hit.triangle_index >= participant.geometry->triangles.size()) {
          return true;
        }
        const float distance = std::max(local_hit.triangle_hit.distance, 0.0f);
        const bool distance_better =
            !best.has_value() ||
            distance < best->distance - kDistanceTieTolerance;
        const bool distance_tied =
            best.has_value() &&
            std::abs(distance - best->distance) <= kDistanceTieTolerance;
        const bool stable_id_better =
            distance_tied &&
            (scene_surface_participant_id_less(
                 participant.public_record.id, best->participant) ||
             (participant.public_record.id == best->participant &&
              local_hit.primitive_index < best->primitive_index));
        if (!distance_better && !stable_id_better) {
          return true;
        }
        const NeutralTriangle &local_triangle =
            participant.geometry->triangles[local_hit.triangle_index];
        const QVec3 world_a = transform_point(
            participant.world_from_local,
            {local_triangle.a.x, local_triangle.a.y, local_triangle.a.z});
        const QVec3 world_b = transform_point(
            participant.world_from_local,
            {local_triangle.b.x, local_triangle.b.y, local_triangle.b.z});
        const QVec3 world_c = transform_point(
            participant.world_from_local,
            {local_triangle.c.x, local_triangle.c.y, local_triangle.c.z});
        QVec3 world_normal = normalize_or_zero(
            cross(world_b - world_a, world_c - world_a));
        if (!finite_vector(world_normal)) {
          world_normal = {};
        }
        best = SceneSurfaceHit{
            .participant = participant.public_record.id,
            .world_position = request.world_origin + world_direction * distance,
            .world_normal = world_normal,
            .distance = distance,
            .primitive_index = local_hit.primitive_index,
        };
        traversal_max_distance =
            std::min(traversal_max_distance,
                     distance + kDistanceTieTolerance);
        return true;
      });
  return best;
}

SceneSurfaceQuery::SceneSurfaceQuery(
    const SceneDocument &scene, const AssetRegistry &assets,
    const ViewportLocalVisibilityService &local_visibility)
    : impl_(std::make_unique<Impl>(scene, assets, local_visibility)) {}

SceneSurfaceQuery::~SceneSurfaceQuery() = default;
SceneSurfaceQuery::SceneSurfaceQuery(SceneSurfaceQuery &&) noexcept = default;
SceneSurfaceQuery &
SceneSurfaceQuery::operator=(SceneSurfaceQuery &&) noexcept = default;

SceneSurfaceQuerySnapshot SceneSurfaceQuery::snapshot_from_data(
    std::shared_ptr<const SceneSurfaceQuerySnapshot::Data> data) {
  return SceneSurfaceQuerySnapshot(std::move(data));
}

SceneSurfaceQuerySnapshot SceneSurfaceQuery::snapshot() const {
  return impl_->snapshot();
}

SceneSurfaceQueryStats SceneSurfaceQuery::stats() const noexcept {
  return impl_->stats();
}

void SceneSurfaceQuery::invalidate() noexcept { impl_->invalidate(); }

} // namespace quader
