////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/viewport/viewport_scene_hit_policy.hpp"

#include "editor/viewport/viewport_surface_occlusion_policy.hpp"

#include "assets/curve_asset_payload.hpp"
#include "diagnostics/profile.hpp"
#include "editor/interactions/viewport_segment_projection.hpp"
#include "editor/interactions/selection_lasso.hpp"
#include "editor/modeling/modeling_viewport_camera_adapter.hpp"
#include "editor/selection/box_selection_rules.hpp"
#include "editor/viewport/viewport_curve_picking.hpp"
#include "editor/viewport/viewport_interaction_frame.hpp"
#include "math/glm_bridge.hpp"
#include "viewport/viewport_camera_adapter.hpp"
#include "viewport/viewport_camera_controller.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <exception>
#include <limits>
#include <numeric>
#include <ranges>
#include <tuple>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace quader {

ViewportSceneProjectionCamera::ViewportSceneProjectionCamera(
    const ViewportCameraController &camera)
    : runtime_camera_(ViewportCameraAdapter::runtime_camera(
          camera, camera.viewport_size())),
      prepared_projection_(runtime_camera_),
      depth_forward_(normalize(runtime_camera_.transform.z_axis * -1.0f)),
      near_plane_(camera.state().near_plane),
      far_plane_(camera.state().far_plane) {
  if (length(depth_forward_) <= 1.0e-6f) {
    depth_forward_ = {0.0f, 0.0f, -1.0f};
  }
}

ViewportSceneProjectionCamera::ViewportSceneProjectionCamera(
    const ModelingViewportCamera &camera)
    : runtime_camera_(modeling_runtime_camera(camera)),
      prepared_projection_(runtime_camera_),
      depth_forward_(normalize(runtime_camera_.transform.z_axis * -1.0f)) {
  if (length(depth_forward_) <= 1.0e-6f) {
    depth_forward_ = {0.0f, 0.0f, -1.0f};
  }
}

ViewportRay ViewportSceneProjectionCamera::ray_from_viewport_point(
    QVec2 local_mouse) const {
  const quader_poly::Ray ray =
      prepared_projection_.ray_from_screen_point(local_mouse);
  return {.origin = ray.origin, .direction = normalize(ray.direction)};
}

ViewportSelectionDepthCamera ViewportSceneProjectionCamera::selection_depth_camera() const noexcept {
  return {.origin = runtime_camera_.transform.origin,
          .right = normalize(runtime_camera_.transform.x_axis),
          .up = normalize(runtime_camera_.transform.y_axis),
          .forward = normalize(runtime_camera_.transform.z_axis * -1.0f),
          .viewport_size = runtime_camera_.viewport_size,
          .vertical_fov_degrees = static_cast<float>(runtime_camera_.fov_degrees),
          .orthographic_size = static_cast<float>(runtime_camera_.orthographic_size),
          .orthographic = orthographic(),
          .supported = !runtime_camera_.affine_model_space};
}

ViewportScreenPoint ViewportSceneProjectionCamera::project_world_point(
    QVec3 world) const {
  QVec2 screen{};
  const bool projected = prepared_projection_.project_point(world, screen);
  const float depth =
      dot(world - runtime_camera_.transform.origin, depth_forward_);
  return {
      .position = screen,
      .depth = depth,
      .visible = projected && depth > near_plane_ && depth < far_plane_,
  };
}

std::optional<ViewportScreenSegment>
ViewportSceneProjectionCamera::project_visible_world_segment(
    QVec3 start, QVec3 end, bool clip_to_viewport) const {
  return project_visible_viewport_segment(runtime_camera_, start, end,
                                           near_plane_, far_plane_,
                                           clip_to_viewport);
}

namespace {

constexpr double kSceneHitDepthTieTolerance = 1.0e-6;
constexpr double kSceneHitPickTieTolerance = 1.0e-6;
constexpr std::size_t kCurveMarqueeVisibilitySubdivisions = 8u;
constexpr std::size_t kCurveMarqueeOcclusionBoundaryIterations = 10u;

[[nodiscard]] double canonical_tie_bucket(double value,
                                          double tolerance) noexcept {
  // Pairwise epsilon comparisons are not transitive and therefore cannot be
  // used by std::sort. Fixed buckets make near-tie equivalence deterministic.
  return std::floor(value / tolerance);
}

[[nodiscard]] bool finite_vector(QVec3 value) noexcept {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

[[nodiscard]] std::uint8_t exact_tie_priority(
    ViewportSceneHitKind kind) noexcept {
  switch (kind) {
  case ViewportSceneHitKind::CurveControl:
    return 0u;
  case ViewportSceneHitKind::ModelingComponent:
    return 1u;
  case ViewportSceneHitKind::Billboard:
    return 2u;
  case ViewportSceneHitKind::CurveEntity:
    return 3u;
  case ViewportSceneHitKind::ModelingObject:
    return 4u;
  case ViewportSceneHitKind::MeshSurface:
    return 5u;
  case ViewportSceneHitKind::FormalInstance:
    return 6u;
  }
  return 7u;
}

[[nodiscard]] std::array<std::uint64_t, 4> stable_source_key(
    const ViewportSceneHitCandidate &candidate) noexcept {
  std::array<std::uint64_t, 4> key{};
  if (const auto *mesh =
          std::get_if<ViewportSceneMeshHit>(&candidate.source)) {
    key = {mesh->participant.display_entity,
           mesh->participant.definition_entity,
           mesh->participant.geometry_entity, mesh->primitive_index};
  } else if (const auto *control =
                 std::get_if<ViewportSceneCurveControlHit>(
                      &candidate.source)) {
    key = {candidate.entity, control->control.point.index,
           control->control.point.generation,
           static_cast<std::uint64_t>(control->control.kind)};
  } else if (const auto *resolved =
                 std::get_if<ViewportResolvedSelectionHit>(
                     &candidate.source)) {
    key = resolved->stable_key;
  }
  return key;
}

struct SelectionIdentityKey final {
  ResolvedSelectionDomain domain = ResolvedSelectionDomain::SceneEntity;
  EntityId entity = kInvalidEntityId;
  int modeling_object_id = kInvalidModelingObjectId;
  ModelingActiveSelectionKind component_kind =
      ModelingActiveSelectionKind::None;
  int vertex = -1;
  int edge_a = -1;
  int edge_b = -1;
  int face = -1;
  EditorSubSelectionKind sub_selection_kind = EditorSubSelectionKind::None;
  std::uint32_t sub_index = 0u;
  std::uint32_t sub_generation = 0u;
  EditorSubSelectionElementRole sub_role =
      EditorSubSelectionElementRole::Primary;

  [[nodiscard]] auto operator<=>(const SelectionIdentityKey &) const =
      default;
};

/** Hashes the same provider-neutral identity used by marquee deduplication. */
struct SelectionIdentityHash final {
  [[nodiscard]] std::size_t operator()(const SelectionIdentityKey &key) const noexcept {
    const std::array values{
        static_cast<std::uint64_t>(key.domain), static_cast<std::uint64_t>(key.entity),
        static_cast<std::uint64_t>(key.modeling_object_id),
        static_cast<std::uint64_t>(key.component_kind),
        static_cast<std::uint64_t>(key.vertex), static_cast<std::uint64_t>(key.edge_a),
        static_cast<std::uint64_t>(key.edge_b), static_cast<std::uint64_t>(key.face),
        static_cast<std::uint64_t>(key.sub_selection_kind),
        static_cast<std::uint64_t>(key.sub_index),
        static_cast<std::uint64_t>(key.sub_generation),
        static_cast<std::uint64_t>(key.sub_role)};
    std::uint64_t result = 0u;
    constexpr std::uint64_t kMixingConstant = 0x9e3779b97f4a7c15ull;
    for (const std::uint64_t value : values) {
      result ^= value + kMixingConstant + (result << 6u) + (result >> 2u);
    }
    return static_cast<std::size_t>(result ^ (result >> 32u));
  }
};

/** Retains comparator facts without sorting or repeatedly decoding hit payloads. */
struct RankedMarqueeHit final {
  const ViewportSceneHitCandidate *candidate = nullptr;
  double depth_bucket = 0.0;
  std::uint8_t priority = 0u;
  double pick_bucket = 0.0;
  EntityId entity = kInvalidEntityId;
  std::array<std::uint64_t, 4> stable_key{};
};

[[nodiscard]] std::optional<SelectionIdentityKey> selection_identity_key(
    const ViewportSceneHitCandidate &candidate) {
  const std::optional<ResolvedSelectionTarget> target =
      resolved_selection_target(candidate);
  if (!target.has_value()) {
    return std::nullopt;
  }
  SelectionIdentityKey key{
      .domain = target->domain,
      .entity = target->entity,
  };
  switch (target->domain) {
  case ResolvedSelectionDomain::SceneEntity:
    break;
  case ResolvedSelectionDomain::ModelingComponent:
    key.entity = kInvalidEntityId;
    key.modeling_object_id = target->modeling_object_id;
    key.component_kind = target->component_kind;
    key.vertex = target->vertex;
    key.edge_a = target->edge_a;
    key.edge_b = target->edge_b;
    key.face = target->face;
    break;
  case ResolvedSelectionDomain::RegisteredSubElement:
    key.sub_selection_kind = target->sub_selection_kind;
    if (target->sub_element.has_value()) {
      key.sub_index = target->sub_element->index;
      key.sub_generation = target->sub_element->generation;
      key.sub_role = target->sub_element->role;
    }
    break;
  }
  return key;
}

[[nodiscard]] bool entity_is_excluded(
    std::span<const EntityId> excluded, EntityId entity) {
  return entity != kInvalidEntityId &&
         std::binary_search(excluded.begin(), excluded.end(), entity);
}

[[nodiscard]] bool occurrence_exclusion_less(
    const ViewportSceneOccurrenceExclusion &left,
    const ViewportSceneOccurrenceExclusion &right) noexcept {
  return scene_surface_occurrence_exclusion_less(left, right);
}

[[nodiscard]] bool occurrence_is_excluded(
    std::span<const ViewportSceneOccurrenceExclusion> excluded,
    EntityId source_entity, EntityId occurrence) {
  const ViewportSceneOccurrenceExclusion key{
      .source_entity = source_entity,
      .occurrence = occurrence,
  };
  const auto found = std::lower_bound(excluded.begin(), excluded.end(), key,
                                      occurrence_exclusion_less);
  return found != excluded.end() && *found == key;
}

/// Limits a normalized world ray to the camera's visible forward-depth slab.
struct SceneHitRayInterval final {
  float minimum = 0.0f;
  float maximum = std::numeric_limits<float>::infinity();
};

[[nodiscard]] std::optional<SceneHitRayInterval> camera_ray_interval(
    const ViewportSceneProjectionCamera &camera, const ViewportRay &ray) {
  const auto &transform = camera.runtime_camera().transform;
  QVec3 forward = normalize(transform.z_axis * -1.0f);
  if (length(forward) <= 1.0e-6f) {
    forward = {0.0f, 0.0f, -1.0f};
  }
  const double origin_depth = dot(ray.origin - transform.origin, forward);
  const double depth_per_distance = dot(ray.direction, forward);
  if (!std::isfinite(origin_depth) || !std::isfinite(depth_per_distance) ||
      depth_per_distance <= 0.0) {
    return std::nullopt;
  }
  // Perspective rays away from the viewport center travel farther than their
  // forward depth; orthographic rays may start away from the camera's eye.
  const double near_distance =
      (camera.near_plane() - origin_depth) / depth_per_distance;
  const double far_distance =
      (camera.far_plane() - origin_depth) / depth_per_distance;
  SceneHitRayInterval interval{
      .minimum = static_cast<float>(std::max(near_distance, 0.0)),
      .maximum = static_cast<float>(far_distance),
  };
  // Surface traversal uses inclusive bounds, while point projection excludes
  // the clip planes themselves. Round inward before traversing any primitive.
  if (interval.minimum <= near_distance) {
    interval.minimum = std::nextafter(
        interval.minimum, std::numeric_limits<float>::infinity());
  }
  if (interval.maximum >= far_distance) {
    interval.maximum = std::nextafter(
        interval.maximum, -std::numeric_limits<float>::infinity());
  }
  if (!std::isfinite(interval.minimum) ||
      interval.maximum < interval.minimum) {
    return std::nullopt;
  }
  return interval;
}

[[nodiscard]] std::optional<SceneSurfaceHit> render_surface_hit(
    const SceneSurfaceQuerySnapshot *scene_surfaces, const ViewportRay &ray,
    SceneSurfaceQueryScratch *scratch,
    SceneHitRayInterval interval = {},
    std::span<const EntityId> excluded_entities = {},
    std::span<const ViewportSceneOccurrenceExclusion>
        excluded_occurrences = {}) {
  if (scene_surfaces == nullptr) {
    return std::nullopt;
  }
  if (scratch == nullptr) {
    return std::nullopt;
  }
  return scene_surfaces->nearest_hit(
      {
          .world_origin = ray.origin,
          .world_direction = ray.direction,
          .min_distance = interval.minimum,
          .max_distance = interval.maximum,
          .include_backfaces = false,
          .eligibility = SceneSurfaceEligibility::RenderVisible,
          .excluded_entities = excluded_entities,
          .excluded_occurrences = excluded_occurrences,
      },
      *scratch);
}

[[nodiscard]] bool depth_is_visible(
    double ray_distance,
    std::optional<double> maximum_ray_distance) noexcept {
  return !maximum_ray_distance.has_value() ||
         ray_distance <= *maximum_ray_distance +
                             kViewportSurfaceOcclusionTolerance;
}

[[nodiscard]] bool world_point_is_render_visible(
    const ViewportSceneProjectionCamera &camera,
    const SceneSurfaceQuerySnapshot *scene_surfaces, QVec3 world_position,
    SceneSurfaceQueryScratch *scratch) {
  const ViewportScreenPoint projected =
      camera.project_world_point(world_position);
  if (!projected.visible) {
    return false;
  }
  ViewportRay ray =
      camera.ray_from_viewport_point(projected.position);
  const float direction_length = length(ray.direction);
  if (!std::isfinite(direction_length) || direction_length <= 1.0e-6f) {
    return false;
  }
  ray.direction = ray.direction / direction_length;
  const double ray_distance =
      static_cast<double>(dot(world_position - ray.origin, ray.direction));
  if (!std::isfinite(ray_distance) || ray_distance < 0.0) {
    return false;
  }
  if (scene_surfaces == nullptr) {
    return true;
  }
  if (scratch == nullptr) {
    return false;
  }
  const std::optional<SceneSurfaceHit> surface =
      scene_surfaces->nearest_hit(
          {
              .world_origin = ray.origin,
              .world_direction = ray.direction,
              .eligibility = SceneSurfaceEligibility::RenderVisible,
          },
          *scratch);
  return !surface.has_value() ||
         ray_distance <=
             surface->distance + kViewportSurfaceOcclusionTolerance;
}

[[nodiscard]] std::optional<double> world_point_ray_distance(
    const ViewportSceneProjectionCamera &camera, QVec3 world_position) {
  const ViewportScreenPoint projected =
      camera.project_world_point(world_position);
  if (!projected.visible) {
    return std::nullopt;
  }
  ViewportRay ray = camera.ray_from_viewport_point(projected.position);
  const float direction_length = length(ray.direction);
  if (!std::isfinite(direction_length) || direction_length <= 1.0e-6f) {
    return std::nullopt;
  }
  ray.direction = ray.direction / direction_length;
  const double distance =
      static_cast<double>(dot(world_position - ray.origin, ray.direction));
  return std::isfinite(distance) && distance >= 0.0
             ? std::optional<double>{distance}
             : std::nullopt;
}

/** Reuses identical surface rays only while one frozen marquee is collected. */
class MarqueeSurfaceDepthCache final {
public:
  MarqueeSurfaceDepthCache(std::size_t expected_rays,
      const SceneSurfaceQuerySnapshot *surfaces,
      std::shared_ptr<const ViewportSelectionDepthSnapshot> selection_depth)
      : surfaces_(surfaces), selection_depth_(std::move(selection_depth)) {
    if (expected_rays != 0u) {
      depths_.reserve(expected_rays);
    }
  }

  ~MarqueeSurfaceDepthCache() {
    if (surfaces_) surfaces_->record_selection_depth_work(stats_);
  }

  [[nodiscard]] ViewportSelectionDepthDecision visibility(
      ModelingActiveSelectionKind kind, QVec2 screen_position,
      const ViewportRay &ray, double depth) {
    if (!selection_depth_ || (kind != ModelingActiveSelectionKind::Vertex &&
                             kind != ModelingActiveSelectionKind::Face)) {
      ++stats_.cpu_fallback_witnesses;
      return ViewportSelectionDepthDecision::Unavailable;
    }
    const auto decision = classify_viewport_selection_depth(*selection_depth_,
        screen_position, ray.origin, ray.direction, depth,
        kViewportSurfaceOcclusionTolerance,
        {.owner = surfaces_, .intersect = [](const void *owner, std::uint32_t triangle_id,
                                             QVec3 origin, QVec3 direction) {
          return static_cast<const SceneSurfaceQuerySnapshot *>(owner)
              ->selection_depth_triangle_distance(triangle_id, origin, direction);
        }});
    switch (decision) {
    case ViewportSelectionDepthDecision::Accepted: ++stats_.gpu_accepted_witnesses; break;
    case ViewportSelectionDepthDecision::Rejected: ++stats_.gpu_rejected_witnesses; break;
    case ViewportSelectionDepthDecision::Overflow:
      ++stats_.gpu_overflow_witnesses;
      ++stats_.cpu_fallback_witnesses;
      break;
    case ViewportSelectionDepthDecision::Unavailable: ++stats_.cpu_fallback_witnesses; break;
    }
    return decision;
  }

  [[nodiscard]] std::optional<float> nearest_depth(
      const SceneSurfaceQuerySnapshot &surfaces, QVec2 screen_position,
      const ViewportRay &ray, SceneSurfaceQueryScratch &scratch) {
    const std::uint64_t key =
        (static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(screen_position.x)) << 32u) |
        std::bit_cast<std::uint32_t>(screen_position.y);
    const auto [entry, inserted] = depths_.try_emplace(key);
    if (inserted) {
      const auto surface = surfaces.nearest_hit(
          {.world_origin = ray.origin,
           .world_direction = ray.direction,
           .eligibility = SceneSurfaceEligibility::RenderVisible}, scratch);
      if (surface.has_value()) {
        entry->second = surface->distance;
      }
    }
    return entry->second;
  }

private:
  std::unordered_map<std::uint64_t, std::optional<float>> depths_;
  const SceneSurfaceQuerySnapshot *surfaces_ = nullptr;
  std::shared_ptr<const ViewportSelectionDepthSnapshot> selection_depth_;
  ViewportSelectionDepthQueryStats stats_;
};

/** Keeps one valid witness's exact ray and its original equal-depth priority. */
struct PreparedMarqueeWitness final {
  const ModelingSelectionVisibilityWitness *witness;
  ViewportRay ray;
  double depth;
  std::size_t original_index;
};

[[nodiscard]] std::optional<PreparedMarqueeWitness> prepare_marquee_witness(
    const ViewportSceneProjectionCamera &camera,
    const ModelingSelectionVisibilityWitness &witness,
    std::size_t original_index) {
  if (!finite_vector(witness.world_position) ||
      !std::isfinite(witness.screen_position.x) ||
      !std::isfinite(witness.screen_position.y)) {
    return std::nullopt;
  }
  ViewportRay ray = camera.ray_from_viewport_point(witness.screen_position);
  const float direction_length = length(ray.direction);
  if (!std::isfinite(direction_length) || direction_length <= 1.0e-6f) {
    return std::nullopt;
  }
  ray.direction = ray.direction / direction_length;
  const double depth = static_cast<double>(
      dot(witness.world_position - ray.origin, ray.direction));
  if (!std::isfinite(depth) || depth < 0.0) {
    return std::nullopt;
  }
  return PreparedMarqueeWitness{&witness, ray, depth, original_index};
}

[[nodiscard]] std::optional<ViewportSceneHitCandidate>
visible_resolved_marquee_candidate(
    const ViewportSceneMarqueeHitQuery &query,
    const ResolvedSelectionHit &hit,
    SceneSurfaceQueryScratch *scratch,
    MarqueeSurfaceDepthCache &surface_depths) {
  ViewportSceneHitCandidate candidate = viewport_scene_hit_candidate(hit);
  if (query.scene_surfaces == nullptr) {
    if (hit.world_position.has_value()) {
      candidate.world_position = *hit.world_position;
    } else if (!hit.visibility_witnesses.empty()) {
      candidate.world_position = hit.visibility_witnesses.front().world_position;
    }
    return candidate;
  }
  if (hit.visibility_witnesses.empty() ||
      (!query.include_occluded_resolved_modeling_hits && scratch == nullptr)) {
    return std::nullopt;
  }

  if (query.include_occluded_resolved_modeling_hits) {
    std::optional<PreparedMarqueeWitness> nearest;
    for (std::size_t index = 0u; index < hit.visibility_witnesses.size(); ++index) {
      const auto prepared = prepare_marquee_witness(
          query.camera, hit.visibility_witnesses[index], index);
      if (prepared.has_value() &&
          (!nearest.has_value() || prepared->depth < nearest->depth)) {
        nearest = prepared;
      }
    }
    if (!nearest.has_value()) {
      return std::nullopt;
    }
    candidate.world_position = nearest->witness->world_position;
    candidate.ray_distance = nearest->depth;
    return candidate;
  }

  // Native marquee witnesses fit in eighteen slots. Keep arbitrary provider
  // lists supported without allocating for the ordinary mesh case.
  constexpr std::size_t kInlineWitnessCount = 18u;
  std::array<PreparedMarqueeWitness, kInlineWitnessCount> inline_witnesses;
  std::vector<PreparedMarqueeWitness> overflow_witnesses;
  std::span<PreparedMarqueeWitness> prepared = inline_witnesses;
  if (hit.visibility_witnesses.size() > inline_witnesses.size()) {
    overflow_witnesses.resize(hit.visibility_witnesses.size());
    prepared = overflow_witnesses;
  }
  std::size_t count = 0u;
  for (std::size_t index = 0u; index < hit.visibility_witnesses.size(); ++index) {
    if (const auto witness = prepare_marquee_witness(
            query.camera, hit.visibility_witnesses[index], index)) {
      prepared[count++] = *witness;
    }
  }
  prepared = prepared.first(count);
  std::ranges::sort(prepared, [](const PreparedMarqueeWitness &left,
                                const PreparedMarqueeWitness &right) {
    return std::tie(left.depth, left.original_index) <
           std::tie(right.depth, right.original_index);
  });
  // The old strict-less-than minimum chose the first original witness on an
  // equal depth. Sorting that index second preserves its exact world position.
  for (const PreparedMarqueeWitness &witness : prepared) {
    const auto visibility = surface_depths.visibility(hit.target.component_kind,
        witness.witness->screen_position, witness.ray, witness.depth);
    if (visibility == ViewportSelectionDepthDecision::Rejected) continue;
    if (visibility == ViewportSelectionDepthDecision::Accepted) {
      candidate.world_position = witness.witness->world_position;
      candidate.ray_distance = witness.depth;
      return candidate;
    }
    const auto surface_depth = surface_depths.nearest_depth(
        *query.scene_surfaces, witness.witness->screen_position, witness.ray, *scratch);
    if (!surface_depth.has_value() ||
        witness.depth <= *surface_depth + kViewportSurfaceOcclusionTolerance) {
      candidate.world_position = witness.witness->world_position;
      candidate.ray_distance = witness.depth;
      return candidate;
    }
  }
  return std::nullopt;
}

void append_visible_marquee_modeling_hits(
    const ViewportSceneMarqueeHitQuery &query, SceneSurfaceQueryScratch *scratch,
    std::vector<ViewportSceneHitCandidate> &hits) {
  constexpr std::size_t kMaximumWorkers = 8u;
  constexpr std::size_t kMinimumParallelHits = 8192u;
  constexpr std::size_t kHitsPerWorker = 4096u;
  const std::size_t count = query.resolved_selection_hits.size();
  std::shared_ptr<const ViewportSelectionDepthSnapshot> selection_depth;
  if (query.scene_surfaces && !query.include_occluded_resolved_modeling_hits &&
      resolve_box_selection_rule(query.rule, query.start, query.end) == ModelingBoxSelectionRule::Crossing) {
    selection_depth = query.scene_surfaces->selection_depth_snapshot(query.camera.selection_depth_camera());
    if (!selection_depth) query.scene_surfaces->record_selection_depth_work({.stale_snapshot_queries = 1u});
  }
  std::size_t worker_count = 1u;
  // Orthographic depth layers often share identical screen rays. Retain that
  // cross-mesh reuse in one cache instead of duplicating it between workers.
  if (query.scene_surfaces != nullptr && !query.include_occluded_resolved_modeling_hits &&
      !query.camera.orthographic() && count >= kMinimumParallelHits) {
    worker_count = std::min(count / kHitsPerWorker,
        std::clamp<std::size_t>(std::thread::hardware_concurrency(), 1u, kMaximumWorkers));
  }
  if (worker_count == 1u) {
    MarqueeSurfaceDepthCache surface_depths(
        query.scene_surfaces != nullptr && !query.include_occluded_resolved_modeling_hits ? count : 0u,
        query.scene_surfaces, selection_depth);
    for (const ResolvedSelectionHit &hit : query.resolved_selection_hits) {
      if (auto visible = visible_resolved_marquee_candidate(query, hit, scratch, surface_depths)) {
        hits.push_back(std::move(*visible));
      }
    }
    return;
  }

  std::vector<std::optional<ViewportSceneHitCandidate>> visible_hits(count);
  std::array<std::exception_ptr, kMaximumWorkers> failures{};
  const auto collect_range = [&](std::size_t worker) {
    try {
      QDR_PROFILE_SCOPE("Marquee visibility worker");
      const std::size_t begin = (count / worker_count) * worker +
                                std::min(worker, count % worker_count);
      const std::size_t end = begin + count / worker_count +
                              (worker < count % worker_count ? 1u : 0u);
      SceneSurfaceQueryScratch worker_scratch;
      MarqueeSurfaceDepthCache surface_depths(end - begin, query.scene_surfaces, selection_depth);
      for (std::size_t index = begin; index < end; ++index) {
        visible_hits[index] = visible_resolved_marquee_candidate(
            query, query.resolved_selection_hits[index], &worker_scratch, surface_depths);
      }
    } catch (...) {
      failures[worker] = std::current_exception();
    }
  };
  std::vector<std::jthread> workers;
  workers.reserve(worker_count - 1u);
  std::size_t next_worker = 1u;
  for (; next_worker < worker_count; ++next_worker) {
    try {
      workers.emplace_back(collect_range, next_worker);
    } catch (const std::system_error &) {
      // Worker exhaustion does not change selection; finish unstarted ranges
      // on this thread while already-started readers finish their own slots.
      break;
    }
  }
  collect_range(0u);
  for (; next_worker < worker_count; ++next_worker) {
    collect_range(next_worker);
  }
  for (std::jthread &worker : workers) {
    worker.join();
  }
  for (const std::exception_ptr &failure : failures) {
    if (failure) {
      std::rethrow_exception(failure);
    }
  }
  for (auto &visible : visible_hits) {
    if (visible.has_value()) {
      hits.push_back(std::move(*visible));
    }
  }
}

void append_mesh_hits(const SceneSurfaceQuerySnapshot *scene_surfaces,
                      const ViewportRay &ray,
                      SceneHitRayInterval interval,
                      std::optional<double> visible_surface_distance,
                      bool collect_all, bool select_backfaces,
                      std::span<const EntityId> excluded_entities,
                      std::span<const ViewportSceneOccurrenceExclusion>
                          excluded_occurrences,
                      SceneSurfaceQueryScratch *scratch,
                      std::vector<ViewportSceneHitCandidate> &hits) {
  if (scene_surfaces == nullptr || scratch == nullptr) {
    return;
  }
  SceneSurfaceRaycastRequest request{
      .world_origin = ray.origin,
      .world_direction = ray.direction,
      .min_distance = interval.minimum,
      .max_distance = interval.maximum,
      .include_backfaces = select_backfaces,
      .eligibility = SceneSurfaceEligibility::SelectableUnlocked,
      .excluded_entities = excluded_entities,
      .excluded_occurrences = excluded_occurrences,
  };
  if (visible_surface_distance.has_value()) {
    request.max_distance = std::min(request.max_distance, static_cast<float>(
        *visible_surface_distance + kViewportSurfaceOcclusionTolerance));
  }
  std::vector<SceneSurfaceParticipantId> excluded;
  do {
    request.excluded_participants = excluded;
    const std::optional<SceneSurfaceHit> surface =
        scene_surfaces->nearest_hit(request, *scratch);
    if (!surface.has_value()) {
      break;
    }
    const bool formal =
        surface->participant.definition_entity != kInvalidEntityId;
    hits.push_back({
        .kind = formal ? ViewportSceneHitKind::FormalInstance
                       : ViewportSceneHitKind::MeshSurface,
        .entity = surface->participant.display_entity,
        .world_position = surface->world_position,
        .ray_distance = surface->distance,
        .pick_distance_pixels = 0.0,
        .source = ViewportSceneMeshHit{
            .participant = surface->participant,
            .primitive_index = surface->primitive_index,
        },
    });
    if (!collect_all) {
      break;
    }
    const auto position = std::ranges::lower_bound(
        excluded, surface->participant, scene_surface_participant_id_less);
    if (position != excluded.end() && *position == surface->participant) {
      break;
    }
    excluded.insert(position, surface->participant);
  } while (true);
}

void append_billboard_hits(const ViewportScenePointHitQuery &query,
                           const ViewportRay &ray,
                           std::optional<double> visible_surface_distance,
                           std::vector<ViewportSceneHitCandidate> &hits) {
  for (const ViewportSceneBillboardPickCandidate &candidate :
       query.billboards) {
    if (candidate.entity == kInvalidEntityId || !candidate.selectable ||
        candidate.effectively_locked || !finite_vector(candidate.position) ||
        entity_is_excluded(query.excluded_entities, candidate.entity)) {
      continue;
    }
    const ViewportScreenPoint projected =
        query.camera.project_world_point(candidate.position);
    if (!projected.visible || projected.depth < 0.0f) {
      continue;
    }
    const float half_size = std::max(candidate.size_pixels, 1.0f) * 0.5f;
    const float offset_x = query.local_mouse.x - projected.position.x;
    const float offset_y = query.local_mouse.y - projected.position.y;
    if (std::abs(offset_x) > half_size || std::abs(offset_y) > half_size) {
      continue;
    }
    const double ray_distance = static_cast<double>(
        dot(candidate.position - ray.origin, ray.direction));
    if (!std::isfinite(ray_distance) || ray_distance < 0.0 ||
        !depth_is_visible(ray_distance, visible_surface_distance)) {
      continue;
    }
    hits.push_back({
        .kind = ViewportSceneHitKind::Billboard,
        .entity = candidate.entity,
        .world_position = candidate.position,
        .ray_distance = ray_distance,
        .pick_distance_pixels =
            std::hypot(static_cast<double>(offset_x),
                       static_cast<double>(offset_y)),
        .source = ViewportSceneBillboardHit{},
    });
  }
}

void append_curve_entity_hits(
    const ViewportScenePointHitQuery &query,
    std::optional<double> visible_surface_distance,
    std::vector<ViewportSceneHitCandidate> &hits) {
  for (const ViewportSceneCurvePickCandidate &candidate : query.curves) {
    if (entity_is_excluded(query.excluded_entities, candidate.entity)) {
      continue;
    }
    const std::array one{candidate};
    const std::optional<ViewportCurvePickHit> hit = pick_viewport_curve(
        query.camera, query.local_mouse, one, query.curve_radius_pixels,
        visible_surface_distance, query.select_backfaces);
    if (!hit.has_value()) {
      continue;
    }
    hits.push_back({
        .kind = ViewportSceneHitKind::CurveEntity,
        .entity = hit->entity,
        .world_position = hit->world_position,
        .ray_distance = hit->ray_distance,
        .pick_distance_pixels = hit->distance_pixels,
        .source = ViewportSceneCurveEntityHit{},
    });
  }
}

void append_curve_control_hit(
    const ViewportScenePointHitQuery &query,
    std::optional<double> visible_surface_distance,
    std::vector<ViewportSceneHitCandidate> &hits) {
  // A Curve draft is a registered sub-selection scope before it owns a scene
  // entity. Its generational control IDs still provide the complete identity.
  if (!query.include_curve_controls || query.curve_controls == nullptr) {
    return;
  }
  if (entity_is_excluded(query.excluded_entities,
                         query.curve_controls->entity)) {
    return;
  }
  const std::optional<ViewportCurveControlPickHit> hit =
      pick_viewport_curve_control(
          query.camera, *query.curve_controls, query.local_mouse,
          query.anchors_only, query.curve_control_radius_pixels,
          visible_surface_distance);
  if (!hit.has_value()) {
    return;
  }
  hits.push_back({
      .kind = ViewportSceneHitKind::CurveControl,
      .entity = query.curve_controls->entity,
      .world_position = hit->world_position,
      .ray_distance = hit->ray_distance,
      .pick_distance_pixels = hit->distance_pixels,
      .source = ViewportSceneCurveControlHit{.control = hit->target},
  });
}

void append_resolved_selection_hits(
    std::span<const ResolvedSelectionHit> resolved_hits,
    const std::optional<SceneSurfaceHit> &visible_surface_hit,
    std::vector<ViewportSceneHitCandidate> &hits,
    std::span<const EntityId> excluded_entities = {},
    std::span<const ViewportSceneOccurrenceExclusion>
        excluded_occurrences = {},
    const ViewportSceneProjectionCamera *camera = nullptr,
    SceneHitRayInterval interval = {},
    const SceneSurfaceQuerySnapshot *scene_surfaces = nullptr,
    SceneSurfaceQueryScratch *surface_scratch = nullptr) {
  for (const ResolvedSelectionHit &hit : resolved_hits) {
    const std::optional<ViewportScreenPoint> projected =
        camera != nullptr && hit.world_position.has_value()
            ? std::optional{camera->project_world_point(*hit.world_position)}
            : std::nullopt;
    if (camera != nullptr &&
        (projected.has_value()
             ? !projected->visible
             : hit.ray_distance < interval.minimum ||
                   hit.ray_distance > interval.maximum)) {
      continue;
    }
    const EntityId occurrence =
        hit.target.source_occurrence != kInvalidEntityId
            ? hit.target.source_occurrence
            : hit.target.entity;
    if (entity_is_excluded(excluded_entities, hit.target.entity) ||
        entity_is_excluded(excluded_entities, occurrence) ||
        occurrence_is_excluded(excluded_occurrences, hit.target.entity,
                               occurrence)) {
      continue;
    }

    std::optional<SceneSurfaceHit> occluding_surface = visible_surface_hit;
    double candidate_distance = hit.ray_distance;
    const bool radius_component =
        hit.target.domain == ResolvedSelectionDomain::ModelingComponent &&
        (hit.target.component_kind == ModelingActiveSelectionKind::Vertex ||
         hit.target.component_kind == ModelingActiveSelectionKind::Edge);
    if (radius_component && projected.has_value() &&
        scene_surfaces != nullptr && surface_scratch != nullptr) {
      // Screen-radius candidates can lie beside the cursor's foreground
      // silhouette. Test all scene occluders along the candidate's own ray.
      const ViewportRay candidate_ray =
          camera->ray_from_viewport_point(projected->position);
      const auto candidate_interval = camera_ray_interval(*camera, candidate_ray);
      if (!candidate_interval.has_value()) {
        continue;
      }
      candidate_distance = dot(*hit.world_position - candidate_ray.origin,
                               candidate_ray.direction);
      occluding_surface = render_surface_hit(
          scene_surfaces, candidate_ray, surface_scratch, *candidate_interval,
          excluded_entities, excluded_occurrences);
    }
    const bool depth_visible =
        !occluding_surface.has_value() ||
        depth_is_visible(candidate_distance, occluding_surface->distance);
    if (!depth_visible) {
      // The modeling owner has already tested component visibility against
      // exact incident topology. A radius hit beside an edge can place its
      // closest point slightly behind the adjacent wall or ceiling, so do not
      // reject it a second time when that surface is the exact same geometry
      // occurrence. Distinct objects and distinct instances remain occluders.
      const SceneSurfaceParticipantId &surface =
          occluding_surface->participant;
      const bool same_modeling_participant =
          hit.target.domain == ResolvedSelectionDomain::ModelingComponent &&
          hit.target.entity != kInvalidEntityId &&
          occurrence == surface.display_entity &&
          hit.target.entity == surface.geometry_entity;
      if (!same_modeling_participant) {
        continue;
      }
    }
    hits.push_back(viewport_scene_hit_candidate(hit));
  }
}

} // namespace

ViewportSceneHitCandidate
viewport_scene_hit_candidate(const ResolvedSelectionHit &hit) {
  ViewportSceneHitKind kind = ViewportSceneHitKind::ModelingObject;
  if (hit.target.domain == ResolvedSelectionDomain::ModelingComponent) {
    kind = ViewportSceneHitKind::ModelingComponent;
  } else if (hit.target.domain ==
             ResolvedSelectionDomain::RegisteredSubElement) {
    kind = ViewportSceneHitKind::CurveControl;
  }
  return {
      .kind = kind,
      .entity = hit.target.entity,
      .world_position = hit.world_position.value_or(QVec3{}),
      .ray_distance = hit.ray_distance,
      .pick_distance_pixels = hit.pick_distance_pixels,
      .source = ViewportResolvedSelectionHit{
          .target = hit.target,
          .stable_key = hit.stable_key,
      },
  };
}

std::optional<ResolvedSelectionTarget>
resolved_selection_target(const ViewportSceneHitCandidate &candidate) {
  if (const auto *resolved =
          std::get_if<ViewportResolvedSelectionHit>(&candidate.source)) {
    return resolved->target;
  }
  if (const auto *control =
          std::get_if<ViewportSceneCurveControlHit>(&candidate.source)) {
    EditorSubSelectionElementRole role =
        EditorSubSelectionElementRole::Primary;
    switch (control->control.kind) {
    case CurveControlKind::IncomingHandle:
      role = EditorSubSelectionElementRole::SecondaryA;
      break;
    case CurveControlKind::OutgoingHandle:
      role = EditorSubSelectionElementRole::SecondaryB;
      break;
    case CurveControlKind::Anchor:
      break;
    }
    return ResolvedSelectionTarget{
        .domain = ResolvedSelectionDomain::RegisteredSubElement,
        .entity = candidate.entity,
        .sub_selection_kind = EditorSubSelectionKind::CurveControl,
        .sub_element = EditorSubSelectionTarget{
            .index = control->control.point.index,
            .generation = control->control.point.generation,
            .role = role,
        },
    };
  }
  ResolvedSelectionTarget target{
      .domain = ResolvedSelectionDomain::SceneEntity,
      .entity = candidate.entity,
  };
  if (const auto *mesh =
          std::get_if<ViewportSceneMeshHit>(&candidate.source)) {
    target.source_occurrence = mesh->participant.display_entity;
    target.source_primitive = mesh->primitive_index;
  }
  return target;
}

bool viewport_scene_hit_candidate_valid(
    const ViewportSceneHitCandidate &candidate) noexcept {
  const bool identity_valid =
      candidate.entity != kInvalidEntityId ||
      candidate.kind == ViewportSceneHitKind::CurveControl;
  return identity_valid &&
          finite_vector(candidate.world_position) &&
         std::isfinite(candidate.ray_distance) &&
         candidate.ray_distance >= 0.0 &&
         std::isfinite(candidate.pick_distance_pixels) &&
         candidate.pick_distance_pixels >= 0.0;
}

bool viewport_scene_hit_precedes(
    const ViewportSceneHitCandidate &candidate,
    const ViewportSceneHitCandidate &best) noexcept {
  const double candidate_depth_bucket = canonical_tie_bucket(
      candidate.ray_distance, kSceneHitDepthTieTolerance);
  const double best_depth_bucket =
      canonical_tie_bucket(best.ray_distance, kSceneHitDepthTieTolerance);
  if (candidate_depth_bucket != best_depth_bucket) {
    return candidate_depth_bucket < best_depth_bucket;
  }
  const std::uint8_t candidate_priority = exact_tie_priority(candidate.kind);
  const std::uint8_t best_priority = exact_tie_priority(best.kind);
  if (candidate_priority != best_priority) {
    return candidate_priority < best_priority;
  }
  const double candidate_pick_bucket = canonical_tie_bucket(
      candidate.pick_distance_pixels, kSceneHitPickTieTolerance);
  const double best_pick_bucket = canonical_tie_bucket(
      best.pick_distance_pixels, kSceneHitPickTieTolerance);
  if (candidate_pick_bucket != best_pick_bucket) {
    return candidate_pick_bucket < best_pick_bucket;
  }
  if (candidate.entity != best.entity) {
    return candidate.entity < best.entity;
  }
  return stable_source_key(candidate) < stable_source_key(best);
}

std::optional<ViewportSceneHitCandidate> resolve_viewport_scene_hit(
    std::span<const ViewportSceneHitCandidate> candidates) {
  std::optional<ViewportSceneHitCandidate> best;
  for (const ViewportSceneHitCandidate &candidate : candidates) {
    if (!viewport_scene_hit_candidate_valid(candidate) ||
        (best.has_value() &&
         !viewport_scene_hit_precedes(candidate, *best))) {
      continue;
    }
    best = candidate;
  }
  return best;
}

std::vector<ViewportSceneHitCandidate>
collect_viewport_scene_point_hits(const ViewportScenePointHitQuery &query) {
  std::optional<SceneSurfaceQueryScratch> local_scratch;
  SceneSurfaceQueryScratch *surface_scratch = query.surface_scratch;
  if (surface_scratch == nullptr && query.scene_surfaces != nullptr) {
    local_scratch.emplace();
    surface_scratch = &*local_scratch;
  }
  std::vector<ViewportSceneHitCandidate> hits;
  hits.reserve(query.billboards.size() + query.curves.size() +
               (query.scene_surfaces != nullptr ? 1u : 0u) +
               (query.include_curve_controls ? 1u : 0u) +
               query.resolved_selection_hits.size());
  ViewportRay ray = query.camera.ray_from_viewport_point(query.local_mouse);
  const float direction_length = length(ray.direction);
  if (!std::isfinite(direction_length) || direction_length <= 1.0e-6f) {
    return hits;
  }
  ray.direction = ray.direction / direction_length;
  const ViewportSceneProjectionCamera projection_camera{query.camera};
  const auto interval = camera_ray_interval(projection_camera, ray);
  if (!interval.has_value()) {
    return hits;
  }
  const std::optional<SceneSurfaceHit> visible_surface_hit =
      query.include_occluded
          ? std::nullopt
          : render_surface_hit(query.scene_surfaces, ray, surface_scratch,
                               *interval,
                               query.excluded_entities,
                               query.excluded_occurrences);
  const std::optional<double> visible_surface_distance =
      visible_surface_hit.has_value()
          ? std::optional<double>{visible_surface_hit->distance}
          : std::nullopt;
  if (query.include_mesh_surfaces) {
    append_mesh_hits(query.scene_surfaces, ray, *interval,
                     visible_surface_distance,
                     query.collect_all_mesh_surfaces, query.select_backfaces,
                     query.excluded_entities, query.excluded_occurrences,
                     surface_scratch, hits);
  }
  append_billboard_hits(query, ray, visible_surface_distance, hits);
  append_curve_entity_hits(query, visible_surface_distance, hits);
  append_curve_control_hit(query, visible_surface_distance, hits);
  append_resolved_selection_hits(query.resolved_selection_hits,
                                 visible_surface_hit, hits,
                                 query.excluded_entities,
                                 query.excluded_occurrences,
                                 &projection_camera, *interval,
                                 query.include_occluded ? nullptr
                                                        : query.scene_surfaces,
                                 surface_scratch);
  return hits;
}

std::optional<ViewportSceneHitCandidate>
resolve_viewport_scene_point_hit(const ViewportScenePointHitQuery &query) {
  const std::vector<ViewportSceneHitCandidate> hits =
      collect_viewport_scene_point_hits(query);
  return resolve_viewport_scene_hit(hits);
}

std::vector<ViewportSceneHitCandidate>
collect_viewport_scene_ray_hits(const ViewportSceneRayHitQuery &query) {
  std::optional<SceneSurfaceQueryScratch> local_scratch;
  SceneSurfaceQueryScratch *surface_scratch = query.surface_scratch;
  if (surface_scratch == nullptr && query.scene_surfaces != nullptr) {
    local_scratch.emplace();
    surface_scratch = &*local_scratch;
  }
  std::vector<ViewportSceneHitCandidate> hits;
  hits.reserve((query.scene_surfaces != nullptr ? 1u : 0u) +
               query.resolved_selection_hits.size());
  ViewportRay ray{.origin = query.ray.origin,
                  .direction = query.ray.direction};
  const float direction_length = length(ray.direction);
  if (!std::isfinite(direction_length) || direction_length <= 1.0e-6f) {
    return hits;
  }
  ray.direction = ray.direction / direction_length;
  const std::optional<SceneSurfaceHit> visible_surface_hit =
      query.include_occluded
          ? std::nullopt
          : render_surface_hit(query.scene_surfaces, ray, surface_scratch);
  const std::optional<double> visible_surface_distance =
      visible_surface_hit.has_value()
          ? std::optional<double>{visible_surface_hit->distance}
          : std::nullopt;
  if (query.include_mesh_surfaces) {
    append_mesh_hits(query.scene_surfaces, ray, {}, visible_surface_distance,
                     query.collect_all_mesh_surfaces, query.select_backfaces,
                     {}, {}, surface_scratch, hits);
  }
  append_resolved_selection_hits(query.resolved_selection_hits,
                                 visible_surface_hit, hits);
  return hits;
}

std::optional<ViewportSceneHitCandidate>
resolve_viewport_scene_ray_hit(const ViewportSceneRayHitQuery &query) {
  const std::vector<ViewportSceneHitCandidate> hits =
      collect_viewport_scene_ray_hits(query);
  return resolve_viewport_scene_hit(hits);
}

std::vector<ViewportSceneHitCandidate> collect_viewport_scene_marquee_hits(
    const ViewportSceneMarqueeHitQuery &query) {
  QDR_PROFILE_FUNCTION();
  std::optional<SceneSurfaceQueryScratch> local_scratch;
  SceneSurfaceQueryScratch *surface_scratch = query.surface_scratch;
  if (surface_scratch == nullptr && query.scene_surfaces != nullptr) {
    local_scratch.emplace();
    surface_scratch = &*local_scratch;
  }
  // The modeling rectangle query emits identity-only component targets for
  // through-selection. Its exact projected geometry has already matched the
  // rectangle, and component publication orders the set by its universe. No
  // surface witness, depth ordering or duplicate membership pass is needed.
  if (query.include_occluded_resolved_modeling_hits &&
      query.billboards.empty() && query.curves.empty() &&
      !query.include_curve_controls &&
      std::ranges::all_of(query.resolved_selection_hits, [](const auto &hit) {
        return hit.target.domain == ResolvedSelectionDomain::ModelingComponent;
      }) &&
      std::ranges::any_of(query.resolved_selection_hits, [](const auto &hit) {
        return hit.rectangle_target_only;
      })) {
    QDR_PROFILE_SCOPE("Marquee collect through component targets");
    std::vector<ViewportSceneHitCandidate> hits;
    hits.reserve(query.resolved_selection_hits.size());
    MarqueeSurfaceDepthCache surface_depths(0u, query.scene_surfaces, {});
    for (const ResolvedSelectionHit &hit : query.resolved_selection_hits) {
      if (!hit.rectangle_target_only) {
        if (auto visible = visible_resolved_marquee_candidate(
                query, hit, surface_scratch, surface_depths)) {
          hits.push_back(std::move(*visible));
        }
        continue;
      }
      ViewportSceneHitCandidate candidate = viewport_scene_hit_candidate(hit);
      if (viewport_scene_hit_candidate_valid(candidate)) {
        hits.push_back(std::move(candidate));
      }
    }
    return hits;
  }
  struct ProjectedCandidate final {
    ViewportSceneHitCandidate hit;
    quader_editor_core::RectangleProjectedObject projected;
  };

  std::vector<ProjectedCandidate> projected_candidates;
  projected_candidates.reserve(
      query.billboards.size() + query.curves.size() +
      (query.include_curve_controls && query.curve_controls != nullptr
           ? query.curve_controls->document.has_value()
                 ? query.curve_controls->document->point_count() * 3u
                 : 0u
           : 0u));
  const auto append_projected = [&](ViewportSceneHitCandidate hit,
                                    quader_editor_core::RectangleProjectedObject
                                        projected) {
    projected.object_id = static_cast<int>(projected_candidates.size());
    projected_candidates.push_back(
        {.hit = std::move(hit), .projected = std::move(projected)});
  };

  for (const ViewportSceneBillboardPickCandidate &candidate :
       query.billboards) {
    if (candidate.entity == kInvalidEntityId || !candidate.selectable ||
        candidate.effectively_locked || !finite_vector(candidate.position)) {
      continue;
    }
    const ViewportScreenPoint screen =
        query.camera.project_world_point(candidate.position);
    const std::optional<double> ray_distance =
        world_point_ray_distance(query.camera, candidate.position);
    if (!screen.visible || !ray_distance.has_value() ||
        !world_point_is_render_visible(query.camera, query.scene_surfaces,
                                       candidate.position,
                                       surface_scratch)) {
      continue;
    }
    const float half_size = std::max(candidate.size_pixels, 1.0f) * 0.5f;
    quader_editor_core::RectangleProjectedObject projected;
    projected.has_screen_center = true;
    projected.screen_center = screen.position;
    projected.has_screen_bounds = true;
    projected.screen_min = screen.position - QVec2{half_size, half_size};
    projected.screen_max = screen.position + QVec2{half_size, half_size};
    append_projected(
        {
            .kind = ViewportSceneHitKind::Billboard,
            .entity = candidate.entity,
            .world_position = candidate.position,
            .ray_distance = *ray_distance,
            .pick_distance_pixels = 0.0,
            .source = ViewportSceneBillboardHit{},
        },
        std::move(projected));
  }

  for (const ViewportSceneCurvePickCandidate &candidate : query.curves) {
    if (candidate.entity == kInvalidEntityId) {
      continue;
    }
    if (candidate.solidified) {
      ViewportCurveTubeProjection tube = project_viewport_curve_tube(
          candidate,
          {.camera = query.camera.runtime_camera(),
           .near_plane = query.camera.near_plane(),
           .far_plane = query.camera.far_plane(),
           .world_point_visible = [&](QVec3 world) {
             return world_point_is_render_visible(
                 query.camera, query.scene_surfaces, world,
                 surface_scratch);
           },
           .visibility_revision =
                query.scene_surfaces != nullptr
                    ? std::optional<std::uint64_t>{
                          query.scene_surfaces->revision()}
                    : std::optional<std::uint64_t>{0u}});
      if (!tube.valid) {
        continue;
      }
      append_projected(
          {
              .kind = ViewportSceneHitKind::CurveEntity,
              .entity = candidate.entity,
              .world_position = tube.nearest_world_position,
              .ray_distance = tube.nearest_ray_distance,
              .pick_distance_pixels = 0.0,
              .source = ViewportSceneCurveEntityHit{},
          },
          std::move(tube.projected));
      continue;
    }
    if (candidate.sample_payload == nullptr) {
      continue;
    }
    quader_editor_core::RectangleProjectedObject projected;
    std::optional<QVec3> nearest_world;
    double nearest_depth = std::numeric_limits<double>::max();
    const auto &samples = candidate.sample_payload->samples;
    for (std::size_t index = 1u; index < samples.size(); ++index) {
      const auto &a = samples[index - 1u].position;
      const auto &b = samples[index].position;
      const QVec3 start = transform_point(
          candidate.world_transform, {a.x, a.y, a.z});
      const QVec3 end = transform_point(
          candidate.world_transform, {b.x, b.y, b.z});
      const auto segment = query.camera.project_visible_world_segment(
          start, end, false);
      if (!segment.has_value()) {
        continue;
      }
      const auto world_at = [&](double parameter) {
        return QVec3{
            static_cast<float>(std::lerp(static_cast<double>(start.x), end.x,
                                         parameter)),
            static_cast<float>(std::lerp(static_cast<double>(start.y), end.y,
                                         parameter)),
            static_cast<float>(std::lerp(static_cast<double>(start.z), end.z,
                                         parameter)),
        };
      };
      struct CurveVisibilitySample final {
        double parameter = 0.0;
        QVec3 world{};
        QVec2 screen{};
        bool visible = false;
      };
      const auto visibility_sample = [&](double parameter) {
        CurveVisibilitySample sample{
            .parameter = parameter,
            .world = world_at(parameter),
        };
        const ViewportScreenPoint screen =
            query.camera.project_world_point(sample.world);
        sample.screen = screen.position;
        sample.visible =
            screen.visible &&
            world_point_is_render_visible(query.camera, query.scene_surfaces,
                                          sample.world, surface_scratch);
        return sample;
      };
      const auto include_visible_point =
          [&](const CurveVisibilitySample &sample) {
            quader_editor_core::include_projected_object_point(
                projected, sample.screen);
            const std::optional<double> ray_distance =
                world_point_ray_distance(query.camera, sample.world);
            if (ray_distance.has_value() && *ray_distance < nearest_depth) {
              nearest_depth = *ray_distance;
              nearest_world = sample.world;
            }
          };
      const auto refined_visible_boundary =
          [&](CurveVisibilitySample first, CurveVisibilitySample second) {
            CurveVisibilitySample visible = first.visible ? first : second;
            CurveVisibilitySample hidden = first.visible ? second : first;
            for (std::size_t iteration = 0u;
                 iteration < kCurveMarqueeOcclusionBoundaryIterations;
                 ++iteration) {
              CurveVisibilitySample midpoint = visibility_sample(
                  std::midpoint(visible.parameter, hidden.parameter));
              if (midpoint.visible) {
                visible = midpoint;
              } else {
                hidden = midpoint;
              }
            }
            return visible;
          };

      std::optional<QVec2> previous_visible_screen;
      CurveVisibilitySample previous =
          visibility_sample(segment->start_parameter);
      if (previous.visible) {
        include_visible_point(previous);
        projected.screen_segments.push_back(
            {.start = previous.screen, .end = previous.screen});
        previous_visible_screen = previous.screen;
      }
      for (std::size_t sample_index = 1u;
           sample_index <= kCurveMarqueeVisibilitySubdivisions;
           ++sample_index) {
        const double sample_fraction = static_cast<double>(sample_index) /
                                       static_cast<double>(
                                           kCurveMarqueeVisibilitySubdivisions);
        CurveVisibilitySample current = visibility_sample(std::lerp(
            segment->start_parameter, segment->end_parameter,
            sample_fraction));
        if (previous.visible != current.visible) {
          // Retain the visible side of each occlusion boundary. Otherwise a
          // short visible sliver between a coarse sample and the blocker is
          // absent from crossing-selection geometry.
          const CurveVisibilitySample boundary =
              refined_visible_boundary(previous, current);
          include_visible_point(boundary);
          if (previous.visible) {
            projected.screen_segments.push_back(
                {.start = previous_visible_screen.value_or(previous.screen),
                 .end = boundary.screen});
            previous_visible_screen.reset();
          } else {
            projected.screen_segments.push_back(
                {.start = boundary.screen, .end = boundary.screen});
            previous_visible_screen = boundary.screen;
          }
        }
        if (current.visible) {
          include_visible_point(current);
          projected.screen_segments.push_back(
              {.start = previous_visible_screen.value_or(current.screen),
               .end = current.screen});
          previous_visible_screen = current.screen;
        } else if (previous.visible == current.visible) {
          previous_visible_screen.reset();
        }
        previous = current;
      }
    }
    if (projected.screen_segments.empty() || !nearest_world.has_value()) {
      continue;
    }
    append_projected(
        {
            .kind = ViewportSceneHitKind::CurveEntity,
            .entity = candidate.entity,
            .world_position = *nearest_world,
            .ray_distance = nearest_depth,
            .pick_distance_pixels = 0.0,
            .source = ViewportSceneCurveEntityHit{},
        },
        std::move(projected));
  }

  // Match point picking: draft controls participate before publication assigns
  // their Curve a scene entity.
  if (query.include_curve_controls && query.curve_controls != nullptr &&
      query.curve_controls->document.has_value()) {
    const auto &document = *query.curve_controls->document;
    constexpr std::array kKinds{
        CurveControlKind::Anchor, CurveControlKind::IncomingHandle,
        CurveControlKind::OutgoingHandle};
    for (std::size_t point_index = 0u;
         point_index < document.point_count(); ++point_index) {
      const modeling::CurveControlPoint &point =
          document.points()[point_index];
      for (const CurveControlKind kind : kKinds) {
        if (!curve_control_is_visible(*query.curve_controls, point_index,
                                      kind)) {
          continue;
        }
        const QVec3 world = transform_point(
            query.curve_controls->world_transform,
            curve_control_local_position(point, kind));
        const ViewportScreenPoint screen =
            query.camera.project_world_point(world);
        const std::optional<double> ray_distance =
            world_point_ray_distance(query.camera, world);
        if (!screen.visible || !ray_distance.has_value() ||
            !world_point_is_render_visible(query.camera,
                                           query.scene_surfaces, world,
                                           surface_scratch)) {
          continue;
        }
        quader_editor_core::RectangleProjectedObject projected;
        projected.has_screen_center = true;
        projected.screen_center = screen.position;
        append_projected(
            {
                .kind = ViewportSceneHitKind::CurveControl,
                .entity = query.curve_controls->entity,
                .world_position = world,
                .ray_distance = *ray_distance,
                .pick_distance_pixels = 0.0,
                .source = ViewportSceneCurveControlHit{
                    .control = {.point = point.id, .kind = kind},
                },
            },
            std::move(projected));
      }
    }
  }

  quader_editor_core::RectangleProjectedHitsRequest request;
  request.rect =
      quader_editor_core::normalized_screen_rect(query.start, query.end);
  request.match =
      resolve_box_selection_rule(query.rule, query.start, query.end) ==
              ModelingBoxSelectionRule::Contain
          ? quader_editor_core::RectangleSelectionMatch::Contain
          : quader_editor_core::RectangleSelectionMatch::Crossing;
  request.objects.reserve(projected_candidates.size());
  for (const ProjectedCandidate &candidate : projected_candidates) {
    request.objects.push_back(candidate.projected);
  }
  const quader_editor_core::RectangleSelectionHits rectangle_hits =
      quader_editor_core::rectangle_hits_from_projected_candidates(request);
  std::vector<ViewportSceneHitCandidate> hits;
  hits.reserve(rectangle_hits.object_ids.size());
  for (const int id : rectangle_hits.object_ids) {
    if (id >= 0 &&
        static_cast<std::size_t>(id) < projected_candidates.size()) {
      hits.push_back(projected_candidates[static_cast<std::size_t>(id)].hit);
    }
  }
  {
    QDR_PROFILE_SCOPE("Marquee test modeling witness visibility");
    append_visible_marquee_modeling_hits(query, surface_scratch, hits);
  }
  return resolve_viewport_scene_marquee_hits(hits);
}

std::vector<ViewportSceneHitCandidate> collect_viewport_scene_lasso_hits(
    const ViewportSceneLassoHitQuery &query) {
  const quader_editor_core::SelectionLassoRegion region(query.points);
  std::vector<ViewportSceneHitCandidate> hits;
  ViewportSceneLassoProjectionCache local_cache;
  auto &cache = query.projection_cache != nullptr ? *query.projection_cache : local_cache;
  std::vector<std::uint64_t> key;
  const auto scalar = [&](double value) { key.push_back(std::bit_cast<std::uint64_t>(value)); };
  const auto vector = [&](QVec3 value) { scalar(value.x); scalar(value.y); scalar(value.z); };
  const auto &camera = query.camera.runtime_camera();
  key.push_back(static_cast<std::uint64_t>(camera.projection));
  key.push_back(camera.affine_model_space);
  vector(camera.transform.origin);
  vector(camera.transform.x_axis);
  vector(camera.transform.y_axis);
  vector(camera.transform.z_axis);
  scalar(camera.viewport_size.x);
  scalar(camera.viewport_size.y);
  scalar(camera.orthographic_size);
  scalar(camera.fov_degrees);
  scalar(query.camera.near_plane());
  scalar(query.camera.far_plane());
  key.push_back(query.billboards.size());
  for (const auto &candidate : query.billboards) {
    key.push_back(candidate.entity);
    key.push_back(candidate.selectable);
    key.push_back(candidate.effectively_locked);
    vector(candidate.position);
    scalar(candidate.size_pixels);
  }
  key.push_back(query.curves.size());
  for (const auto &candidate : query.curves) {
    key.push_back(candidate.entity);
    key.push_back(candidate.solidified);
    key.push_back(reinterpret_cast<std::uintptr_t>(candidate.sample_payload.get()));
    key.push_back(reinterpret_cast<std::uintptr_t>(candidate.tube_payload.get()));
    for (const auto value : candidate.world_transform.values) {
      scalar(value);
    }
  }
  key.push_back(query.include_curve_controls);
  if (query.include_curve_controls && query.curve_controls != nullptr) {
    key.push_back(reinterpret_cast<std::uintptr_t>(query.curve_controls));
    key.push_back(query.curve_controls->revision);
    key.push_back(query.curve_controls->entity);
    key.push_back(query.curve_controls->active);
    key.push_back(query.curve_controls->drawing);
    for (const auto value : query.curve_controls->world_transform.values) {
      scalar(value);
    }
  }
  if (cache.key != key) {
    cache.key = std::move(key);
    cache.candidates.clear();
    cache.eligible_entities.clear();
    cache.retained_curves.assign(query.curves.begin(), query.curves.end());
    ++cache.projection_build_count;
  const auto eligible = [&](EntityId entity) {
    if (entity != kInvalidEntityId) {
      cache.eligible_entities.push_back(entity);
    }
  };
  const auto append = [&](ViewportSceneHitCandidate hit,
                          quader_editor_core::RectangleProjectedObject projected) {
    cache.candidates.push_back({std::move(hit), std::move(projected)});
  };
  for (const auto &candidate : query.billboards) {
    if (candidate.entity == kInvalidEntityId || !candidate.selectable ||
        candidate.effectively_locked || !finite_vector(candidate.position)) {
      continue;
    }
    eligible(candidate.entity);
    const auto screen = query.camera.project_world_point(candidate.position);
    if (!screen.visible) {
      continue;
    }
    const float half_size = std::max(candidate.size_pixels, 1.0f) * 0.5f;
    quader_editor_core::RectangleProjectedObject projected;
    projected.has_screen_bounds = true;
    projected.screen_min = screen.position - QVec2{half_size, half_size};
    projected.screen_max = screen.position + QVec2{half_size, half_size};
    append({.kind = ViewportSceneHitKind::Billboard,
            .entity = candidate.entity, .world_position = candidate.position,
            .ray_distance = world_point_ray_distance(query.camera, candidate.position).value_or(0.0),
            .source = ViewportSceneBillboardHit{}}, projected);
  }
  for (const auto &candidate : query.curves) {
    if (candidate.entity == kInvalidEntityId) {
      continue;
    }
    eligible(candidate.entity);
    quader_editor_core::RectangleProjectedObject projected;
    QVec3 witness{};
    double depth = 0.0;
    if (candidate.solidified && candidate.tube_payload != nullptr) {
      for (const auto &triangle : candidate.tube_payload->triangles()) {
        std::vector<QVec3> world_polygon;
        world_polygon.reserve(3);
        for (const auto point : {triangle.a, triangle.b, triangle.c}) {
          const auto world = transform_point(candidate.world_transform, point);
          world_polygon.push_back(world);
          if (!query.camera.project_world_point(world).visible) {
            projected.complete_geometry = false;
          }
        }
        auto polygon = quader_editor_core::project_lasso_polygon(
            query.camera.runtime_camera(), world_polygon,
            query.camera.near_plane(), query.camera.far_plane());
        if (!polygon.empty()) {
          projected.screen_polygons.push_back(std::move(polygon));
        }
      }
    } else if (candidate.sample_payload != nullptr) {
      const auto &samples = candidate.sample_payload->samples;
      bool has_witness = false;
      for (std::size_t index = 0; index < samples.size(); ++index) {
        const auto local = samples[index].position;
        const auto world = transform_point(candidate.world_transform,
                                            {local.x, local.y, local.z});
        const auto screen = query.camera.project_world_point(world);
        projected.complete_geometry = projected.complete_geometry && screen.visible;
        if (screen.visible) {
          quader_editor_core::include_projected_object_point(projected, screen.position);
          if (!has_witness) {
            witness = world;
            depth = world_point_ray_distance(query.camera, world).value_or(0.0);
            has_witness = true;
          }
        }
        if (index == 0) {
          continue;
        }
        const auto previous = samples[index - 1].position;
        const auto start = transform_point(candidate.world_transform,
                                            {previous.x, previous.y, previous.z});
        const auto segment = query.camera.project_visible_world_segment(start, world, false);
        if (segment.has_value()) {
          projected.screen_segments.push_back({segment->start, segment->end});
          if (!has_witness) {
            witness = start + (world - start) *
                static_cast<float>((segment->start_parameter + segment->end_parameter) * 0.5);
            depth = world_point_ray_distance(query.camera, witness).value_or(0.0);
            has_witness = true;
          }
        }
      }
    }
    append({.kind = ViewportSceneHitKind::CurveEntity,
            .entity = candidate.entity, .world_position = witness,
            .ray_distance = depth, .source = ViewportSceneCurveEntityHit{}}, projected);
  }
  if (query.include_curve_controls && query.curve_controls != nullptr &&
      query.curve_controls->document.has_value()) {
    const auto &document = *query.curve_controls->document;
    constexpr std::array kKinds{CurveControlKind::Anchor,
        CurveControlKind::IncomingHandle, CurveControlKind::OutgoingHandle};
    for (std::size_t index = 0; index < document.point_count(); ++index) {
      const auto &point = document.points()[index];
      for (const auto kind : kKinds) {
        if (!curve_control_is_visible(*query.curve_controls, index, kind)) {
          continue;
        }
        const auto world = transform_point(query.curve_controls->world_transform,
                                            curve_control_local_position(point, kind));
        const auto screen = query.camera.project_world_point(world);
        if (screen.visible) {
          quader_editor_core::RectangleProjectedObject projected;
          projected.has_screen_center = true;
          projected.screen_center = screen.position;
          append({.kind = ViewportSceneHitKind::CurveControl,
              .entity = query.curve_controls->entity, .world_position = world,
              .ray_distance = world_point_ray_distance(query.camera, world).value_or(0.0),
              .source = ViewportSceneCurveControlHit{.control = {.point = point.id, .kind = kind}}},
              std::move(projected));
        }
      }
    }
  }
  }
  if (query.eligible_entities != nullptr) {
    query.eligible_entities->insert(query.eligible_entities->end(),
        cache.eligible_entities.begin(), cache.eligible_entities.end());
  }
  for (const auto &candidate : cache.candidates) {
    if (region.matches_object(candidate.projected, query.partial_intersection)) {
      hits.push_back(candidate.hit);
    }
  }
  for (const auto &hit : query.resolved_selection_hits) {
    hits.push_back(viewport_scene_hit_candidate(hit));
  }
  return resolve_viewport_scene_marquee_hits(hits);
}

std::vector<ViewportSceneHitCandidate> resolve_viewport_scene_marquee_hits(
    std::span<const ViewportSceneHitCandidate> candidates) {
  QDR_PROFILE_FUNCTION();
  std::vector<RankedMarqueeHit> ranked;
  ranked.reserve(candidates.size());
  for (const ViewportSceneHitCandidate &candidate : candidates) {
    if (viewport_scene_hit_candidate_valid(candidate)) {
      ranked.push_back({
          .candidate = &candidate,
          .depth_bucket = canonical_tie_bucket(candidate.ray_distance, kSceneHitDepthTieTolerance),
          .priority = exact_tie_priority(candidate.kind),
          .pick_bucket = canonical_tie_bucket(candidate.pick_distance_pixels, kSceneHitPickTieTolerance),
          .entity = candidate.entity,
          .stable_key = stable_source_key(candidate)});
    }
  }
  // Match viewport_scene_hit_precedes exactly, including its equivalence
  // classes. Input position is deliberately not an additional tie-breaker.
  std::ranges::sort(ranked, [](const RankedMarqueeHit &left, const RankedMarqueeHit &right) {
    return std::tie(left.depth_bucket, left.priority, left.pick_bucket, left.entity, left.stable_key) <
           std::tie(right.depth_bucket, right.priority, right.pick_bucket, right.entity, right.stable_key);
  });
  std::vector<ViewportSceneHitCandidate> unique;
  unique.reserve(ranked.size());
  std::unordered_set<SelectionIdentityKey, SelectionIdentityHash> membership;
  membership.reserve(ranked.size());
  for (const RankedMarqueeHit &rank : ranked) {
    const ViewportSceneHitCandidate &candidate = *rank.candidate;
    const std::optional<SelectionIdentityKey> identity =
        selection_identity_key(candidate);
    if (!identity.has_value() || !membership.insert(*identity).second) {
      continue;
    }
    unique.push_back(candidate);
  }
  return unique;
}

} // namespace quader
