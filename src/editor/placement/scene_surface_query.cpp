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
#include "scene/scene_mesh_occurrence_projection.hpp"
#include "scene/scene_transform.hpp"
#include "viewport/viewport_local_visibility.hpp"

#include <quader/modeling/neutral/geometry/bvh.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <numeric>
#include <ranges>
#include <unordered_map>
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
    const SceneSurfaceRaycastRequest &request) {
  const auto found = std::ranges::lower_bound(
      request.excluded_participants, id,
      [](const SceneSurfaceParticipantId &left,
         const SceneSurfaceParticipantId &right) {
        return scene_surface_participant_id_less(left, right);
      });
  if (found != request.excluded_participants.end() && *found == id) {
    return true;
  }

  const auto entity_excluded = [&request](EntityId entity) {
    return entity != kInvalidEntityId &&
           std::binary_search(request.excluded_entities.begin(),
                              request.excluded_entities.end(), entity);
  };
  if (entity_excluded(id.display_entity) ||
      entity_excluded(id.geometry_entity) ||
      entity_excluded(id.definition_entity)) {
    return true;
  }

  const auto occurrence_excluded = [&request, id](EntityId source_entity) {
    if (source_entity == kInvalidEntityId ||
        id.display_entity == kInvalidEntityId) {
      return false;
    }
    const SceneSurfaceOccurrenceExclusion key{
        .source_entity = source_entity,
        .occurrence = id.display_entity,
    };
    const auto occurrence = std::ranges::lower_bound(
        request.excluded_occurrences, key,
        scene_surface_occurrence_exclusion_less);
    return occurrence != request.excluded_occurrences.end() &&
           *occurrence == key;
  };
  return occurrence_excluded(id.geometry_entity) ||
         occurrence_excluded(id.definition_entity);
}

[[nodiscard]] bool participant_is_eligible(
    const SceneSurfaceParticipant &participant,
    SceneSurfaceEligibility eligibility) {
  switch (eligibility) {
  case SceneSurfaceEligibility::RenderVisible:
    return true;
  case SceneSurfaceEligibility::Selectable:
    return participant.selectable;
  case SceneSurfaceEligibility::SelectableUnlocked:
    return participant.selectable && !participant.effectively_locked;
  }
  return false;
}

[[nodiscard]] std::uint64_t surface_edge_key(std::uint32_t left,
                                             std::uint32_t right) {
  const std::uint32_t low = std::min(left, right);
  const std::uint32_t high = std::max(left, right);
  return (static_cast<std::uint64_t>(low) << 32u) |
         static_cast<std::uint64_t>(high);
}

struct SurfaceEdgeAccumulator final {
  std::uint32_t left = 0u;
  std::uint32_t right = 0u;
  QVec3 normal_sum{};
  std::uint64_t key = 0u;
};

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
  std::uint64_t hierarchy_revision = 0u;
  std::uint64_t visibility_revision = 0u;
  std::uint64_t transform_revision = 0u;
  std::uint64_t mesh_binding_revision = 0u;
  std::uint64_t mesh_asset_revision = 0u;
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
  std::uint64_t revision = 0u;
  std::vector<SceneSurfaceParticipant> public_participants;
  std::vector<SceneSurfaceLandmark> public_landmarks;
  std::vector<FrozenSurfaceParticipant> participants;
  BoundsBvh tlas;
};

/// Adapts scene, asset, instance, and local-visibility state into BVHs.
class SceneSurfaceQuery::Impl final {
public:
  Impl(const SceneDocument &scene, const AssetRegistry &assets,
       const ViewportLocalVisibilityService &local_visibility)
      : scene_(scene), assets_(assets),
        local_visibility_(&local_visibility) {}

  Impl(const SceneDocument &scene, const AssetRegistry &assets,
       std::function<bool(EntityId)> local_visibility_filter,
       std::function<std::uint64_t()> local_visibility_revision_provider)
      : scene_(scene), assets_(assets),
        local_visibility_filter_(std::move(local_visibility_filter)),
        local_visibility_revision_provider_(
            std::move(local_visibility_revision_provider)) {}

  [[nodiscard]] SceneSurfaceQuerySnapshot snapshot() const {
    std::optional<ViewportLocalVisibilitySnapshot> local_snapshot;
    if (local_visibility_ != nullptr) {
      local_snapshot = local_visibility_->snapshot();
    }
    const std::uint64_t local_visibility_revision =
        local_snapshot.has_value()
            ? local_snapshot->revision()
            : (local_visibility_revision_provider_
                   ? local_visibility_revision_provider_()
                   : 0u);
    const SurfaceSnapshotFingerprint fingerprint{
        .scene = &scene_,
        .assets = &assets_,
        .hierarchy_revision = scene_.hierarchy_presentation_revision(),
        .visibility_revision = scene_.visibility_revision(),
        .transform_revision = scene_.transform_revision(),
        .mesh_binding_revision = scene_.mesh_binding_revision(),
        .mesh_asset_revision = assets_.mesh_material_usage_revision(),
        .local_visibility_revision = local_visibility_revision,
    };
    if (snapshot_ && fingerprint_valid_ && fingerprint == fingerprint_) {
      return SceneSurfaceQuery::snapshot_from_data(snapshot_);
    }

    QDR_PROFILE_SCOPE("Scene surface query build snapshot");
    std::shared_ptr<SceneSurfaceQuerySnapshot::Data> next =
        std::make_shared<SceneSurfaceQuerySnapshot::Data>();
    next->revision = ++snapshot_revision_;
    const auto local_allows = [&](EntityId entity) {
      if (local_snapshot.has_value()) {
        return local_snapshot->allows(entity);
      }
      return !local_visibility_filter_ || local_visibility_filter_(entity);
    };
    build_participants(local_allows, *next);
    build_tlas(*next);
    snapshot_ = std::move(next);
    fingerprint_ = fingerprint;
    fingerprint_valid_ = true;
    ++stats_.snapshot_build_count;
    ++stats_.tlas_build_count;
    stats_.participant_count = snapshot_->participants.size();
    stats_.landmark_count = snapshot_->public_landmarks.size();
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
    stats_.landmark_count = 0u;
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
      const MeshAsset &mesh, bool selectable,
      bool effectively_locked) const {
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
    const SceneSurfaceParticipant public_record{
        .id = id,
        .world_bounds = *bounds,
        .triangle_count = geometry->bvh.primitive_count(),
        .selectable = selectable,
        .effectively_locked = effectively_locked,
    };
    append_landmarks(target, public_record, *payload, world_from_local);
    target.participants.push_back({
        .public_record = public_record,
        .world_from_local = world_from_local,
        .local_from_world = *local_from_world,
        .pinned_payload = std::move(payload),
        .geometry = std::move(geometry),
    });
  }

  void append_landmarks(SceneSurfaceQuerySnapshot::Data &target,
                        const SceneSurfaceParticipant &participant,
                        const MeshGeometryPayload &geometry,
                        const QMat4 &world_from_local) const {
    std::vector<QVec3> world_vertices;
    world_vertices.reserve(geometry.vertices.size());
    for (const MeshVertex &vertex : geometry.vertices) {
      const QVec3 world = transform_point(world_from_local, vertex.position);
      if (!finite_vector(world)) {
        return;
      }
      world_vertices.push_back(world);
    }

    std::vector<QVec3> vertex_normal_sums(world_vertices.size());
    std::vector<SurfaceEdgeAccumulator> edges;
    std::unordered_map<std::uint64_t, std::size_t> edge_slots;
    edge_slots.reserve(geometry.indices.size());
    for (std::size_t offset = 0u; offset + 2u < geometry.indices.size();
         offset += 3u) {
      const std::uint32_t first = geometry.indices[offset];
      const std::uint32_t second = geometry.indices[offset + 1u];
      const std::uint32_t third = geometry.indices[offset + 2u];
      if (first >= world_vertices.size() || second >= world_vertices.size() ||
          third >= world_vertices.size()) {
        continue;
      }
      const QVec3 a = world_vertices[first];
      const QVec3 b = world_vertices[second];
      const QVec3 c = world_vertices[third];
      const QVec3 area_normal = cross(b - a, c - a);
      if (!finite_vector(area_normal) ||
          length_squared(area_normal) <= kRayEpsilon * kRayEpsilon) {
        continue;
      }
      const QVec3 normal = normalize_or_zero(area_normal);
      target.public_landmarks.push_back({
          .participant = participant.id,
          .world_position = (a + b + c) / 3.0f,
          .world_normal = normal,
          .kind = SceneSurfaceLandmarkKind::FaceCenter,
          .element_key = offset / 3u,
          .selectable = participant.selectable,
          .effectively_locked = participant.effectively_locked,
      });
      vertex_normal_sums[first] += area_normal;
      vertex_normal_sums[second] += area_normal;
      vertex_normal_sums[third] += area_normal;

      const std::array<std::pair<std::uint32_t, std::uint32_t>, 3u>
          triangle_edges{{{first, second}, {second, third}, {third, first}}};
      for (const auto &[left, right] : triangle_edges) {
        const std::uint64_t key = surface_edge_key(left, right);
        const auto found = edge_slots.find(key);
        if (found == edge_slots.end()) {
          edge_slots.emplace(key, edges.size());
          edges.push_back({
              .left = left,
              .right = right,
              .normal_sum = area_normal,
              .key = key,
          });
        } else {
          edges[found->second].normal_sum += area_normal;
        }
      }
    }

    for (std::size_t index = 0u; index < world_vertices.size(); ++index) {
      target.public_landmarks.push_back({
          .participant = participant.id,
          .world_position = world_vertices[index],
          .world_normal = normalize_or_zero(vertex_normal_sums[index]),
          .kind = SceneSurfaceLandmarkKind::Vertex,
          .element_key = index,
          .selectable = participant.selectable,
          .effectively_locked = participant.effectively_locked,
      });
    }
    for (const SurfaceEdgeAccumulator &edge : edges) {
      target.public_landmarks.push_back({
          .participant = participant.id,
          .world_position =
              (world_vertices[edge.left] + world_vertices[edge.right]) * 0.5f,
          .world_normal = normalize_or_zero(edge.normal_sum),
          .kind = SceneSurfaceLandmarkKind::EdgeMidpoint,
          .element_key = edge.key,
          .selectable = participant.selectable,
          .effectively_locked = participant.effectively_locked,
      });
    }
  }

  void build_participants(
      const std::function<bool(EntityId)> &local_allows,
      SceneSurfaceQuerySnapshot::Data &target) const {
    QDR_PROFILE_SCOPE("Scene surface query collect participants");
    const std::shared_ptr<const SceneMeshOccurrenceProjectionSnapshot>
        occurrences = scene_.mesh_occurrence_projection();
    for (const SceneMeshOccurrenceProjectionRecord &occurrence :
         occurrences->records()) {
      if (local_allows && !local_allows(occurrence.display_entity)) {
        continue;
      }
      const MeshComponent *component = scene_.mesh(occurrence.geometry_entity);
      const MeshAsset *mesh =
          component != nullptr ? assets_.mesh(component->mesh) : nullptr;
      if (mesh == nullptr) {
        continue;
      }
      append_participant(
          target,
          {.display_entity = occurrence.display_entity,
           .geometry_entity = occurrence.geometry_entity,
           .definition_entity = occurrence.definition_entity},
          occurrence.world_from_geometry, *mesh, occurrence.selectable,
          occurrence.effectively_locked);
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
    std::ranges::sort(
        target.public_landmarks,
        [](const SceneSurfaceLandmark &left,
           const SceneSurfaceLandmark &right) {
          if (left.participant != right.participant) {
            return scene_surface_participant_id_less(left.participant,
                                                     right.participant);
          }
          if (left.kind != right.kind) {
            return left.kind < right.kind;
          }
          return left.element_key < right.element_key;
        });
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
  const ViewportLocalVisibilityService *local_visibility_ = nullptr;
  std::function<bool(EntityId)> local_visibility_filter_;
  std::function<std::uint64_t()> local_visibility_revision_provider_;
  mutable std::vector<GeometryCacheEntry> geometry_cache_;
  mutable std::uint64_t geometry_use_sequence_ = 0u;
  mutable std::uint64_t snapshot_revision_ = 0u;
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

std::span<const SceneSurfaceLandmark>
SceneSurfaceQuerySnapshot::landmarks() const noexcept {
  return data_ ? std::span<const SceneSurfaceLandmark>(data_->public_landmarks)
               : std::span<const SceneSurfaceLandmark>{};
}

std::uint64_t SceneSurfaceQuerySnapshot::revision() const noexcept {
  return data_ ? data_->revision : 0u;
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
        if (excluded_participant(participant.public_record.id, request) ||
            !participant_is_eligible(participant.public_record,
                                     request.eligibility) ||
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
                options, scratch.impl_->blas,
                [&request, &participant, &world_direction](
                    std::size_t, const NeutralTriangle &triangle) {
                  if (request.include_backfaces) {
                    return true;
                  }
                  const QVec3 world_a = transform_point(
                      participant.world_from_local,
                      {triangle.a.x, triangle.a.y, triangle.a.z});
                  const QVec3 world_b = transform_point(
                      participant.world_from_local,
                      {triangle.b.x, triangle.b.y, triangle.b.z});
                  const QVec3 world_c = transform_point(
                      participant.world_from_local,
                      {triangle.c.x, triangle.c.y, triangle.c.z});
                  const QVec3 area_normal =
                      cross(world_b - world_a, world_c - world_a);
                  return finite_vector(area_normal) &&
                         length_squared(area_normal) >
                             kRayEpsilon * kRayEpsilon &&
                         dot(area_normal, world_direction) < 0.0f;
                });
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

SceneSurfaceQuery::SceneSurfaceQuery(
    const SceneDocument &scene, const AssetRegistry &assets,
    std::function<bool(EntityId)> local_visibility_filter,
    std::function<std::uint64_t()> local_visibility_revision_provider)
    : impl_(std::make_unique<Impl>(
          scene, assets, std::move(local_visibility_filter),
          std::move(local_visibility_revision_provider))) {}

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
