////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/picking/curve_picking_cache.hpp"

#include "assets/asset_registry.hpp"
#include "assets/curve_asset_payload.hpp"
#include "diagnostics/profile.hpp"
#include "editor/interactions/viewport_segment_projection.hpp"
#include "editor/interactions/viewport_projection.hpp"
#include "editor/modeling/modeling_viewport_camera_adapter.hpp"
#include "editor/selection/box_selection_rules.hpp"
#include "math/glm_bridge.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_hierarchy_visibility.hpp"
#include "scene/scene_transform.hpp"
#include "viewport/viewport_camera_clip_planes.hpp"
#include "viewport/viewport_local_visibility.hpp"

#include <quader/modeling/curves/curve_tube.hpp>
#include <quader/modeling/neutral/geometry/bvh.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <numeric>
#include <utility>

namespace quader {

namespace {

using NeutralRay = quader_geometry::QRay3<float>;
using NeutralTriangle = quader_geometry::QTriangle3<float>;
using NeutralVector = quader_geometry::QBasicVec3<float>;
using TubeTriangleBvh = quader_geometry::QTriangleBvh<float>;

constexpr float kTubeRayEpsilon = 0.000001f;
constexpr float kTubeDistanceTieTolerance = 0.000001f;
constexpr std::size_t kTubeProjectionVisibilitySubdivisions = 4u;
constexpr std::size_t kTubeProjectionBoundaryIterations = 8u;
constexpr std::size_t kTubePayloadCacheCapacity = 64u;
constexpr std::size_t kTubePayloadCacheByteCapacity = 128u * 1024u * 1024u;
// Retained native triangles, BVH-owned triangles/indices/nodes and container
// overhead must fit under this conservative lifetime reservation.
constexpr std::size_t kEstimatedTubePayloadBytesPerTriangle = 256u;
constexpr std::size_t kTubeProjectionCacheCapacity =
    kTubePayloadCacheCapacity * 4u;
constexpr std::size_t kTubeProjectionCacheByteCapacity = 32u * 1024u * 1024u;
constexpr std::size_t kTubeAggregateCacheByteCapacity =
    kTubePayloadCacheByteCapacity;
constexpr std::size_t kMaximumDetailedTubeProjectionTriangles = 16384u;

[[nodiscard]] bool finite_vector(QVec3 value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

[[nodiscard]] NeutralVector neutral_vector(QVec3 value) {
  return {value.x, value.y, value.z};
}

[[nodiscard]] QVec3 native_vector(NeutralVector value) {
  return {value.x, value.y, value.z};
}

[[nodiscard]] QVec3 interpolate_world_point(QVec3 start, QVec3 end,
                                             double parameter) {
  return {
      static_cast<float>(std::lerp(static_cast<double>(start.x), end.x,
                                   parameter)),
      static_cast<float>(std::lerp(static_cast<double>(start.y), end.y,
                                   parameter)),
      static_cast<float>(std::lerp(static_cast<double>(start.z), end.z,
                                   parameter)),
  };
}

[[nodiscard]] std::optional<double> projected_world_ray_distance(
    const quader_editor_core::ViewportCameraState &camera, QVec3 world,
    QVec2 screen) {
  quader_poly::Ray ray =
      quader_editor_core::ray_from_viewport_screen_point(camera, screen);
  const float direction_length = length(ray.direction);
  if (!std::isfinite(direction_length) ||
      direction_length <= kTubeRayEpsilon) {
    return std::nullopt;
  }
  ray.direction = ray.direction / direction_length;
  const double distance =
      static_cast<double>(dot(world - ray.origin, ray.direction));
  return std::isfinite(distance) && distance >= 0.0
             ? std::optional<double>{distance}
             : std::nullopt;
}

struct TubeProjectionSample final {
  double parameter = 0.0;
  QVec3 world{};
  QVec2 screen{};
  bool visible = false;
};

[[nodiscard]] bool same_vector(QVec3 left, QVec3 right) noexcept {
  return left.x == right.x && left.y == right.y && left.z == right.z;
}

[[nodiscard]] bool same_matrix(const QMat4 &left,
                               const QMat4 &right) noexcept {
  return left.values == right.values;
}

[[nodiscard]] bool same_camera(
    const quader_editor_core::ViewportCameraState &left,
    const quader_editor_core::ViewportCameraState &right) noexcept {
  return left.projection == right.projection &&
         left.affine_model_space == right.affine_model_space &&
         same_vector(left.transform.x_axis, right.transform.x_axis) &&
         same_vector(left.transform.y_axis, right.transform.y_axis) &&
         same_vector(left.transform.z_axis, right.transform.z_axis) &&
         same_vector(left.transform.origin, right.transform.origin) &&
         left.viewport_size.x == right.viewport_size.x &&
         left.viewport_size.y == right.viewport_size.y &&
         left.orthographic_size == right.orthographic_size &&
         left.fov_degrees == right.fov_degrees;
}

[[nodiscard]] std::size_t projection_byte_cost(
    const ViewportCurveTubeProjection &projection) noexcept {
  std::size_t polygon_vertices = 0u;
  for (const auto &polygon : projection.projected.screen_polygons) {
    polygon_vertices += polygon.capacity();
  }
  return projection.projected.screen_points.capacity() * sizeof(QVec2) +
         projection.projected.screen_segments.capacity() *
             sizeof(quader_editor_core::RectangleProjectedSegment) +
         polygon_vertices * sizeof(QVec2) +
         projection.projected.screen_polygons.capacity() *
             sizeof(std::vector<QVec2>);
}

[[nodiscard]] std::vector<ViewportCurveTubeTriangle>
build_tube_pick_triangles(const modeling::CurveDocument &document,
                          const CurveComponent &component) {
  if (!component.solidify || !curve_component_is_valid(component)) {
    return {};
  }
  const modeling::Result<modeling::MeshPayload> tessellated =
      modeling::tessellate_curve_tube(
          document,
          {.radius = component.solidify_radius,
           .radial_resolution =
               static_cast<std::uint32_t>(component.solidify_resolution)});
  if (!tessellated.ok()) {
    return {};
  }
  const modeling::MeshPayload &mesh = tessellated.value();
  if (mesh.indices.empty() || mesh.indices.size() % 3u != 0u) {
    return {};
  }
  std::vector<ViewportCurveTubeTriangle> triangles;
  triangles.reserve(mesh.indices.size() / 3u);
  for (std::size_t index = 0u; index < mesh.indices.size(); index += 3u) {
    const std::uint32_t first = mesh.indices[index];
    const std::uint32_t second = mesh.indices[index + 1u];
    const std::uint32_t third = mesh.indices[index + 2u];
    if (first >= mesh.vertices.size() || second >= mesh.vertices.size() ||
        third >= mesh.vertices.size()) {
      return {};
    }
    const auto native_point = [](modeling::Vec3 point) {
      return QVec3{point.x, point.y, point.z};
    };
    triangles.push_back({
        .a = native_point(mesh.vertices[first].position),
        .b = native_point(mesh.vertices[second].position),
        .c = native_point(mesh.vertices[third].position),
    });
  }
  return triangles;
}

} // namespace

class ViewportCurveTubeProjectionCache final {
public:
  explicit ViewportCurveTubeProjectionCache(std::size_t byte_capacity)
      : byte_capacity_(byte_capacity) {}

  [[nodiscard]] std::optional<std::uint64_t>
  try_register_payload(std::size_t bytes) noexcept {
    if (bytes > byte_capacity_) {
      return std::nullopt;
    }
    while (!entries_.empty() &&
           bytes > byte_capacity_ - payload_cached_bytes_ - cached_bytes_) {
      evict_lru();
    }
    if (bytes > byte_capacity_ - payload_cached_bytes_) {
      return std::nullopt;
    }
    payload_cached_bytes_ += bytes;
    return next_payload_identity_++;
  }

  [[nodiscard]] std::optional<ViewportCurveTubeProjection> find(
      std::uint64_t payload_identity, const QMat4 &world_transform,
      const ViewportCurveTubeProjectionRequest &request) {
    for (ProjectionCacheEntry &entry : entries_) {
      if (entry.payload_identity == payload_identity &&
          entry.near_plane == request.near_plane &&
          entry.far_plane == request.far_plane &&
          entry.visibility_revision == request.visibility_revision &&
          same_camera(entry.camera, request.camera) &&
          same_matrix(entry.world_transform, world_transform)) {
        entry.last_use_sequence = ++use_sequence_;
        return entry.projection;
      }
    }
    return std::nullopt;
  }

  void insert(std::uint64_t payload_identity,
              const QMat4 &world_transform,
              const ViewportCurveTubeProjectionRequest &request,
              const ViewportCurveTubeProjection &projection) {
    const std::size_t dynamic_bytes = projection_byte_cost(projection);
    const std::size_t entry_bytes =
        dynamic_bytes > std::numeric_limits<std::size_t>::max() -
                            sizeof(ProjectionCacheEntry)
            ? std::numeric_limits<std::size_t>::max()
            : dynamic_bytes + sizeof(ProjectionCacheEntry);
    const std::size_t available = available_projection_bytes();
    if (entry_bytes > available) {
      return;
    }
    while (!entries_.empty() &&
           (entries_.size() >= kTubeProjectionCacheCapacity ||
            entry_bytes > available_projection_bytes() - cached_bytes_)) {
      evict_lru();
    }
    entries_.push_back({
        .payload_identity = payload_identity,
        .camera = request.camera,
        .world_transform = world_transform,
        .near_plane = request.near_plane,
        .far_plane = request.far_plane,
        .visibility_revision = request.visibility_revision,
        .projection = projection,
        .bytes = entry_bytes,
        .last_use_sequence = ++use_sequence_,
    });
    cached_bytes_ += entry_bytes;
  }

  void unregister_payload(std::uint64_t payload_identity,
                          std::size_t payload_bytes) noexcept {
    for (auto entry = entries_.begin(); entry != entries_.end();) {
      if (entry->payload_identity != payload_identity) {
        ++entry;
        continue;
      }
      cached_bytes_ -= entry->bytes;
      entry = entries_.erase(entry);
    }
    payload_cached_bytes_ -=
        std::min(payload_bytes, payload_cached_bytes_);
  }

  void record_build() noexcept { ++build_count_; }

  void reset() noexcept {
    entries_.clear();
    cached_bytes_ = 0u;
    use_sequence_ = 0u;
    build_count_ = 0u;
    eviction_count_ = 0u;
  }

  [[nodiscard]] std::size_t cached_bytes() const noexcept {
    return cached_bytes_;
  }
  [[nodiscard]] std::size_t payload_cached_bytes() const noexcept {
    return payload_cached_bytes_;
  }
  [[nodiscard]] std::size_t byte_capacity() const noexcept {
    return byte_capacity_;
  }
  [[nodiscard]] std::size_t entry_count() const noexcept {
    return entries_.size();
  }
  [[nodiscard]] std::uint64_t build_count() const noexcept {
    return build_count_;
  }
  [[nodiscard]] std::uint64_t eviction_count() const noexcept {
    return eviction_count_;
  }

private:
  struct ProjectionCacheEntry final {
    std::uint64_t payload_identity = 0u;
    quader_editor_core::ViewportCameraState camera;
    QMat4 world_transform;
    double near_plane = 0.0;
    double far_plane = 0.0;
    std::optional<std::uint64_t> visibility_revision;
    ViewportCurveTubeProjection projection;
    std::size_t bytes = 0u;
    std::uint64_t last_use_sequence = 0u;
  };

  [[nodiscard]] std::size_t available_projection_bytes() const noexcept {
    return byte_capacity_ - payload_cached_bytes_;
  }

  void evict_lru() noexcept {
    const auto victim = std::ranges::min_element(
        entries_, {}, &ProjectionCacheEntry::last_use_sequence);
    cached_bytes_ -= victim->bytes;
    entries_.erase(victim);
    ++eviction_count_;
  }

  std::vector<ProjectionCacheEntry> entries_;
  std::size_t byte_capacity_ = 0u;
  std::size_t payload_cached_bytes_ = 0u;
  std::size_t cached_bytes_ = 0u;
  std::uint64_t next_payload_identity_ = 1u;
  std::uint64_t use_sequence_ = 0u;
  std::uint64_t build_count_ = 0u;
  std::uint64_t eviction_count_ = 0u;
};

class ViewportCurveTubePickPayload::Impl final {
public:
  std::vector<NeutralTriangle> triangles;
  TubeTriangleBvh bvh;
  std::shared_ptr<ViewportCurveTubeProjectionCache> projection_cache;
  std::uint64_t projection_cache_identity = 0u;
  std::size_t registered_payload_bytes = 0u;
  mutable std::uint64_t projection_build_count = 0u;
};

std::optional<std::size_t>
ViewportCurveTubePickPayload::estimated_byte_cost(
    std::size_t triangle_count) noexcept {
  const std::size_t fixed_bytes =
      sizeof(ViewportCurveTubePickPayload) + sizeof(Impl);
  if (triangle_count >
      (std::numeric_limits<std::size_t>::max() - fixed_bytes) /
          kEstimatedTubePayloadBytesPerTriangle) {
    return std::nullopt;
  }
  return fixed_bytes +
         triangle_count * kEstimatedTubePayloadBytesPerTriangle;
}

ViewportCurveTubePickPayload::ViewportCurveTubePickPayload(
    std::vector<ViewportCurveTubeTriangle> triangles,
    std::shared_ptr<ViewportCurveTubeProjectionCache> projection_cache,
    std::uint64_t projection_cache_identity,
    std::size_t registered_payload_bytes)
    : triangles_(std::move(triangles)), impl_(std::make_unique<Impl>()) {
  impl_->projection_cache = std::move(projection_cache);
  impl_->projection_cache_identity = projection_cache_identity;
  impl_->registered_payload_bytes = registered_payload_bytes;
  impl_->triangles.reserve(triangles_.size());
  for (const ViewportCurveTubeTriangle &triangle : triangles_) {
    impl_->triangles.push_back({neutral_vector(triangle.a),
                                neutral_vector(triangle.b),
                                neutral_vector(triangle.c)});
  }
  std::vector<std::size_t> primitive_indices(impl_->triangles.size());
  std::iota(primitive_indices.begin(), primitive_indices.end(), 0u);
  if (!impl_->bvh.build(impl_->triangles, primitive_indices)) {
    impl_->triangles.clear();
  }
  impl_->triangles.clear();
  impl_->triangles.shrink_to_fit();
}

ViewportCurveTubePickPayload::~ViewportCurveTubePickPayload() {
  if (impl_ != nullptr && impl_->projection_cache != nullptr) {
    impl_->projection_cache->unregister_payload(
        impl_->projection_cache_identity, impl_->registered_payload_bytes);
  }
}

std::size_t ViewportCurveTubePickPayload::estimated_bytes() const noexcept {
  return impl_ != nullptr ? impl_->registered_payload_bytes : 0u;
}

std::uint64_t
ViewportCurveTubePickPayload::projection_build_count() const noexcept {
  return impl_ != nullptr ? impl_->projection_build_count : 0u;
}

std::shared_ptr<const ViewportCurveTubePickPayload>
ViewportCurveTubePickPayload::build(
    std::vector<ViewportCurveTubeTriangle> triangles) {
  return build(std::move(triangles),
               std::make_shared<ViewportCurveTubeProjectionCache>(
                   kTubeProjectionCacheByteCapacity));
}

std::shared_ptr<const ViewportCurveTubePickPayload>
ViewportCurveTubePickPayload::build(
    std::vector<ViewportCurveTubeTriangle> triangles,
    std::shared_ptr<ViewportCurveTubeProjectionCache> projection_cache) {
  if (projection_cache == nullptr || triangles.empty() ||
      std::ranges::any_of(triangles, [](const auto &triangle) {
        return !finite_vector(triangle.a) || !finite_vector(triangle.b) ||
               !finite_vector(triangle.c);
      })) {
    return {};
  }
  const std::optional<std::size_t> estimated_bytes =
      estimated_byte_cost(triangles.size());
  if (!estimated_bytes.has_value()) {
    return {};
  }
  const std::optional<std::uint64_t> projection_cache_identity =
      projection_cache->try_register_payload(*estimated_bytes);
  if (!projection_cache_identity.has_value()) {
    return {};
  }
  std::shared_ptr<ViewportCurveTubePickPayload> payload;
  try {
    payload.reset(new ViewportCurveTubePickPayload(
        std::move(triangles), projection_cache, *projection_cache_identity,
        *estimated_bytes));
  } catch (...) {
    projection_cache->unregister_payload(*projection_cache_identity,
                                         *estimated_bytes);
    throw;
  }
  return payload->impl_ == nullptr || payload->impl_->bvh.empty()
             ? std::shared_ptr<const ViewportCurveTubePickPayload>{}
             : std::shared_ptr<const ViewportCurveTubePickPayload>{
                   std::move(payload)};
}

std::optional<ViewportCurveTubeRayHit>
ViewportCurveTubePickPayload::nearest_hit(
    const QMat4 &world_transform, QVec3 normalized_world_origin,
    QVec3 normalized_world_direction, bool include_backfaces,
    double maximum_ray_distance) const {
  if (impl_ == nullptr || impl_->bvh.empty() ||
      !finite_vector(normalized_world_origin) ||
      !finite_vector(normalized_world_direction) ||
      std::isnan(maximum_ray_distance) || maximum_ray_distance < 0.0) {
    return std::nullopt;
  }
  const float direction_length = length(normalized_world_direction);
  if (!std::isfinite(direction_length) ||
      direction_length <= kTubeRayEpsilon) {
    return std::nullopt;
  }
  normalized_world_direction =
      normalized_world_direction / direction_length;
  const std::optional<QMat4> local_from_world =
      try_scene_matrix_inverse(world_transform);
  if (!local_from_world.has_value()) {
    return std::nullopt;
  }
  const QVec3 local_origin =
      transform_point(*local_from_world, normalized_world_origin);
  const QVec3 local_direction =
      transform_vector(*local_from_world, normalized_world_direction);
  if (!finite_vector(local_origin) || !finite_vector(local_direction) ||
      length_squared(local_direction) <= kTubeRayEpsilon * kTubeRayEpsilon) {
    return std::nullopt;
  }

  quader_geometry::QTriangleBvhQueryOptions<float> options;
  options.min_distance = 0.0f;
  options.max_distance = std::isfinite(maximum_ray_distance)
                             ? static_cast<float>(std::min(
                                   maximum_ray_distance,
                                   static_cast<double>(
                                       std::numeric_limits<float>::max())))
                             : std::numeric_limits<float>::infinity();
  options.intersection_epsilon = kTubeRayEpsilon;
  options.tie_tolerance = kTubeDistanceTieTolerance;
  quader_geometry::QBvhTraversalScratch scratch;
  const auto local_hit = impl_->bvh.nearest_hit(
      NeutralRay{neutral_vector(local_origin),
                 neutral_vector(local_direction)},
      options, scratch,
      [include_backfaces, &world_transform,
       normalized_world_direction](std::size_t,
                                   const NeutralTriangle &triangle) {
        if (include_backfaces) {
          return true;
        }
        const QVec3 world_a =
            transform_point(world_transform, native_vector(triangle.a));
        const QVec3 world_b =
            transform_point(world_transform, native_vector(triangle.b));
        const QVec3 world_c =
            transform_point(world_transform, native_vector(triangle.c));
        const QVec3 world_normal =
            cross(world_b - world_a, world_c - world_a);
        return finite_vector(world_normal) &&
               length_squared(world_normal) >
                   kTubeRayEpsilon * kTubeRayEpsilon &&
               dot(world_normal, normalized_world_direction) < 0.0f;
      });
  if (!local_hit.hit || !std::isfinite(local_hit.triangle_hit.distance) ||
      local_hit.triangle_hit.distance < 0.0f) {
    return std::nullopt;
  }
  const double ray_distance = local_hit.triangle_hit.distance;
  if (ray_distance > maximum_ray_distance + kTubeDistanceTieTolerance) {
    return std::nullopt;
  }
  const QVec3 world_position =
      normalized_world_origin +
      normalized_world_direction * static_cast<float>(ray_distance);
  if (!finite_vector(world_position)) {
    return std::nullopt;
  }
  return ViewportCurveTubeRayHit{
      .world_position = world_position,
      .ray_distance = ray_distance,
      .primitive_index = local_hit.primitive_index,
  };
}

ViewportCurveTubeProjection ViewportCurveTubePickPayload::project(
    const QMat4 &world_transform,
    const ViewportCurveTubeProjectionRequest &request) const {
  ViewportCurveTubeProjection result;
  if (impl_ == nullptr || !std::isfinite(request.near_plane) ||
      !std::isfinite(request.far_plane) ||
      request.near_plane > request.far_plane ||
      !scene_matrix_is_finite(world_transform)) {
    return result;
  }

  const bool cacheable = !request.world_point_visible ||
                         request.visibility_revision.has_value();
  if (cacheable && impl_->projection_cache != nullptr) {
    const auto cached = impl_->projection_cache->find(
        impl_->projection_cache_identity, world_transform, request);
    if (cached.has_value()) {
      return *cached;
    }
  }
  ++impl_->projection_build_count;
  if (impl_->projection_cache != nullptr) {
    impl_->projection_cache->record_build();
  }

  const auto point_visible = [&](QVec3 world) {
    return !request.world_point_visible ||
           request.world_point_visible(world);
  };
  const auto include_sample = [&](const TubeProjectionSample &sample) {
    quader_editor_core::include_projected_object_point(result.projected,
                                                        sample.screen);
    const std::optional<double> distance = projected_world_ray_distance(
        request.camera, sample.world, sample.screen);
    if (distance.has_value() && *distance < result.nearest_ray_distance) {
      result.nearest_ray_distance = *distance;
      result.nearest_world_position = sample.world;
    }
  };
  const auto sample_world = [&](QVec3 world, double parameter) {
    TubeProjectionSample sample{.parameter = parameter, .world = world};
    const std::optional<ViewportScreenSegment> projected =
        project_visible_viewport_segment(
            request.camera, world, world, request.near_plane,
            request.far_plane, false);
    if (projected.has_value()) {
      sample.screen = projected->start;
    }
    sample.visible = projected.has_value() && point_visible(world);
    return sample;
  };
  const auto append_edge = [&](QVec3 start, QVec3 end) {
    const std::optional<ViewportScreenSegment> clipped =
        project_visible_viewport_segment(
            request.camera, start, end, request.near_plane,
            request.far_plane, false);
    if (!clipped.has_value()) {
      return;
    }
    const auto sample_at = [&](double parameter) {
      return sample_world(interpolate_world_point(start, end, parameter),
                          parameter);
    };
    if (!request.world_point_visible) {
      const TubeProjectionSample first =
          sample_at(clipped->start_parameter);
      const TubeProjectionSample last = sample_at(clipped->end_parameter);
      if (first.visible && last.visible) {
        include_sample(first);
        include_sample(last);
        result.projected.screen_segments.push_back(
            {.start = first.screen, .end = last.screen});
      }
      return;
    }
    const auto refine_boundary = [&](TubeProjectionSample first,
                                     TubeProjectionSample second) {
      TubeProjectionSample visible = first.visible ? first : second;
      TubeProjectionSample hidden = first.visible ? second : first;
      for (std::size_t iteration = 0u;
           iteration < kTubeProjectionBoundaryIterations; ++iteration) {
        TubeProjectionSample midpoint =
            sample_at(std::midpoint(visible.parameter, hidden.parameter));
        if (midpoint.visible) {
          visible = midpoint;
        } else {
          hidden = midpoint;
        }
      }
      return visible;
    };

    std::optional<QVec2> previous_visible_screen;
    TubeProjectionSample previous = sample_at(clipped->start_parameter);
    if (previous.visible) {
      include_sample(previous);
      previous_visible_screen = previous.screen;
    }
    for (std::size_t index = 1u;
         index <= kTubeProjectionVisibilitySubdivisions; ++index) {
      const double fraction = static_cast<double>(index) /
                              static_cast<double>(
                                  kTubeProjectionVisibilitySubdivisions);
      TubeProjectionSample current = sample_at(std::lerp(
          clipped->start_parameter, clipped->end_parameter, fraction));
      if (previous.visible != current.visible) {
        const TubeProjectionSample boundary =
            refine_boundary(previous, current);
        include_sample(boundary);
        if (previous.visible) {
          result.projected.screen_segments.push_back(
              {.start = previous_visible_screen.value_or(previous.screen),
               .end = boundary.screen});
          previous_visible_screen.reset();
        } else {
          previous_visible_screen = boundary.screen;
        }
      }
      if (current.visible) {
        include_sample(current);
        if (previous_visible_screen.has_value()) {
          result.projected.screen_segments.push_back(
              {.start = *previous_visible_screen, .end = current.screen});
        }
        previous_visible_screen = current.screen;
      } else if (previous.visible == current.visible) {
        previous_visible_screen.reset();
      }
      previous = current;
    }
  };

  const std::size_t projection_stride = std::max<std::size_t>(
      1u, (triangles_.size() + kMaximumDetailedTubeProjectionTriangles - 1u) /
              kMaximumDetailedTubeProjectionTriangles);
  // Pathological authored Curves retain a bounded, uniformly sampled tube
  // proxy for marquee projection. Exact pointer picking still traverses the
  // complete BLAS, while ordinary tubes (up to the guard) use every triangle.
  for (std::size_t triangle_index = 0u;
       triangle_index < triangles_.size(); triangle_index += projection_stride) {
    const ViewportCurveTubeTriangle &triangle = triangles_[triangle_index];
    const std::array world{
        transform_point(world_transform, triangle.a),
        transform_point(world_transform, triangle.b),
        transform_point(world_transform, triangle.c),
    };
    if (std::ranges::any_of(world,
                            [](QVec3 point) { return !finite_vector(point); })) {
      continue;
    }

    std::array<TubeProjectionSample, 3u> vertices{
        sample_world(world[0], 0.0), sample_world(world[1], 0.0),
        sample_world(world[2], 0.0)};
    const QVec3 centroid = (world[0] + world[1] + world[2]) / 3.0f;
    TubeProjectionSample centroid_sample = sample_world(centroid, 0.0);
    const bool all_vertices_visible = std::ranges::all_of(
        vertices, [](const TubeProjectionSample &sample) {
          return sample.visible;
        });
    if (all_vertices_visible && centroid_sample.visible) {
      std::vector<QVec2> polygon;
      polygon.reserve(vertices.size());
      for (const TubeProjectionSample &vertex : vertices) {
        include_sample(vertex);
        polygon.push_back(vertex.screen);
      }
      include_sample(centroid_sample);
      result.projected.screen_polygons.push_back(std::move(polygon));
    } else if (centroid_sample.visible) {
      include_sample(centroid_sample);
    }

    append_edge(world[0], world[1]);
    append_edge(world[1], world[2]);
    append_edge(world[2], world[0]);
  }

  result.valid = result.projected.has_screen_bounds &&
                 std::isfinite(result.nearest_ray_distance);
  if (result.valid) {
    result.projected.has_screen_center = true;
    result.projected.screen_center =
        (result.projected.screen_min + result.projected.screen_max) * 0.5f;
  }
  if (!request.world_point_visible &&
      (!result.projected.screen_segments.empty() ||
       !result.projected.screen_polygons.empty())) {
    result.projected.screen_points.clear();
    result.projected.screen_points.shrink_to_fit();
  }
  if (cacheable && impl_->projection_cache != nullptr) {
    impl_->projection_cache->insert(impl_->projection_cache_identity,
                                    world_transform, request, result);
  }
  return result;
}

ViewportCurveTubeProjection project_viewport_curve_tube(
    const ViewportSceneCurvePickCandidate &candidate,
    const ViewportCurveTubeProjectionRequest &request) {
  if (!candidate.solidified || candidate.tube_payload == nullptr) {
    return {};
  }
  return candidate.tube_payload->project(candidate.world_transform, request);
}

ViewportCurvePickCache::ViewportCurvePickCache()
    : tube_projection_cache_(
          std::make_shared<ViewportCurveTubeProjectionCache>(
              kTubeAggregateCacheByteCapacity)) {}

std::size_t ViewportCurvePickCache::tube_payload_cached_bytes() const noexcept {
  return tube_projection_cache_ != nullptr
             ? tube_projection_cache_->payload_cached_bytes()
             : 0u;
}

std::size_t ViewportCurvePickCache::tube_projection_cached_bytes() const
    noexcept {
  return tube_projection_cache_ != nullptr
             ? tube_projection_cache_->cached_bytes()
             : 0u;
}

std::size_t ViewportCurvePickCache::tube_cached_bytes() const noexcept {
  return tube_payload_cached_bytes() + tube_projection_cached_bytes();
}

std::size_t ViewportCurvePickCache::tube_cache_byte_capacity() const noexcept {
  return tube_projection_cache_ != nullptr
             ? tube_projection_cache_->byte_capacity()
             : 0u;
}

std::size_t
ViewportCurvePickCache::tube_projection_cache_entry_count() const noexcept {
  return tube_projection_cache_ != nullptr
             ? tube_projection_cache_->entry_count()
             : 0u;
}

std::uint64_t
ViewportCurvePickCache::tube_projection_build_count() const noexcept {
  return tube_projection_cache_ != nullptr
             ? tube_projection_cache_->build_count()
             : 0u;
}

std::uint64_t
ViewportCurvePickCache::tube_projection_eviction_count() const noexcept {
  return tube_projection_cache_ != nullptr
             ? tube_projection_cache_->eviction_count()
             : 0u;
}

std::shared_ptr<const ViewportCurveTubePickPayload>
ViewportCurvePickCache::tube_payload_for(
    const modeling::CurveDocument &document,
    const CurveComponent &component) {
  if (!component.solidify || !curve_component_is_valid(component)) {
    return {};
  }
  const std::uint64_t content_identity = document.content_identity().value;
  const std::uint64_t content_revision = document.content_revision();
  const std::uint32_t radius_bits =
      std::bit_cast<std::uint32_t>(component.solidify_radius);
  for (TubePayloadCacheEntry &entry : tube_payload_cache_) {
    if (entry.content_identity == content_identity &&
        entry.content_revision == content_revision &&
        entry.radius_bits == radius_bits &&
        entry.resolution == component.solidify_resolution) {
      entry.last_use_sequence = ++tube_payload_use_sequence_;
      return entry.payload;
    }
  }
  std::vector<ViewportCurveTubeTriangle> triangles =
      build_tube_pick_triangles(document, component);
  const std::optional<std::size_t> prospective_bytes =
      triangles.empty()
          ? std::optional<std::size_t>{0u}
          : ViewportCurveTubePickPayload::estimated_byte_cost(
                triangles.size());
  ++tube_payload_build_count_;
  while (!tube_payload_cache_.empty() &&
         (tube_payload_cache_.size() >= kTubePayloadCacheCapacity ||
          (prospective_bytes.has_value() &&
           *prospective_bytes <= kTubePayloadCacheByteCapacity &&
           *prospective_bytes > kTubePayloadCacheByteCapacity -
                                    tube_payload_cached_bytes_))) {
    const auto victim = std::ranges::min_element(
        tube_payload_cache_, {},
        &TubePayloadCacheEntry::last_use_sequence);
    tube_payload_cached_bytes_ -= victim->estimated_bytes;
    tube_payload_cache_.erase(victim);
  }
  std::shared_ptr<const ViewportCurveTubePickPayload> payload =
      ViewportCurveTubePickPayload::build(
          std::move(triangles), tube_projection_cache_);
  const std::size_t estimated_bytes =
      payload != nullptr ? payload->estimated_bytes() : 0u;
  tube_payload_cache_.push_back({
      .content_identity = content_identity,
      .content_revision = content_revision,
      .radius_bits = radius_bits,
      .resolution = component.solidify_resolution,
      .payload = payload,
      .estimated_bytes = estimated_bytes,
      .last_use_sequence = ++tube_payload_use_sequence_,
  });
  tube_payload_cached_bytes_ += estimated_bytes;
  return payload;
}

std::span<const ViewportSceneCurvePickCandidate> ViewportCurvePickCache::candidates(
    const SceneDocument &document, const AssetRegistry &assets,
    const ViewportLocalVisibilitySnapshot &local_visibility) {
  return candidates(document, assets, local_visibility.revision(),
                    [&local_visibility](EntityId entity) {
                      return local_visibility.allows(entity);
                    });
}

std::span<const ViewportSceneCurvePickCandidate> ViewportCurvePickCache::candidates(
    const SceneDocument &document, const AssetRegistry &assets,
    std::uint64_t visibility_revision,
    const std::function<bool(EntityId)> &entity_visible,
    bool instance_edit_active) {
  if (valid_ && document_ == &document && assets_ == &assets &&
      scene_revision_ == document.revision() &&
      asset_revision_ == assets.revision() &&
      visibility_revision_ == visibility_revision &&
      instance_edit_active_ == instance_edit_active) {
    return candidates_;
  }
  QDR_PROFILE_SCOPE("Viewport rebuild curve pick candidates");
  document_ = &document;
  assets_ = &assets;
  scene_revision_ = document.revision();
  asset_revision_ = assets.revision();
  visibility_revision_ = visibility_revision;
  instance_edit_active_ = instance_edit_active;
  valid_ = true;
  ++build_count_;
  candidates_.clear();
  if (instance_edit_active || assets.curve_count() == 0u) {
    return candidates_;
  }
  candidates_.reserve(assets.curve_count());
  const auto hierarchy = document.hierarchy_projection();
  for (const auto &entity : hierarchy->records()) {
    const CurveComponent *binding = document.curve(entity.entity);
    if (binding == nullptr || !entity.effectively_visible ||
        !entity.effectively_selectable ||
        entity.containing_instance_definition != kInvalidEntityId ||
        (entity_visible && !entity_visible(entity.entity)) ||
        !scene_hierarchy_segment_is_selectable_ignoring_visibility(
            document, entity.entity)) {
      continue;
    }
    const CurveAsset *curve = assets.curve(binding->curve);
    if (curve == nullptr || curve->document == nullptr) {
      continue;
    }
    const auto samples = curve_sample_payload(*curve);
    const std::shared_ptr<const ViewportCurveTubePickPayload> tube_payload =
        tube_payload_for(*curve->document, *binding);
    candidates_.push_back({
        .entity = entity.entity,
        .world_transform = entity.world_transform,
        .document = curve->document,
        .sample_payload = samples,
        .solidified = tube_payload != nullptr,
        .tube_payload = tube_payload,
    });
  }
  return candidates_;
}

std::vector<EntityId> ViewportCurvePickCache::box_pick_entities(
    const SceneDocument &document, const AssetRegistry &assets,
    const ModelingBoxSelectRequest &request, std::uint64_t visibility_revision,
    const std::function<bool(EntityId)> &entity_visible,
    bool instance_edit_active) {
  const auto source = candidates(document, assets, visibility_revision,
                                 entity_visible, instance_edit_active);
  if (!rectangle_valid_ || rectangle_source_revision_ != build_count_ ||
      rectangle_settings_revision_ != document.scene_settings_revision() ||
      !modeling_viewport_camera_matches(rectangle_camera_, request.camera)) {
    QDR_PROFILE_SCOPE("Viewport rebuild curve rectangle projection");
    rectangle_request_.objects.clear();
    rectangle_entities_.clear();
    rectangle_request_.objects.reserve(source.size());
    rectangle_entities_.reserve(source.size());
    const SceneCameraSettings &settings = document.scene_settings().camera;
    const auto clips = sanitize_viewport_camera_clip_planes(
        settings.near_plane, settings.far_plane);
    const auto camera = modeling_runtime_camera(request.camera);
    for (const auto &candidate : source) {
      if (rectangle_entities_.size() >=
          static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        break;
      }
      quader_editor_core::RectangleProjectedObject projected;
      projected.object_id = static_cast<int>(rectangle_entities_.size());
      if (candidate.solidified) {
        ViewportCurveTubeProjection tube = project_viewport_curve_tube(
            candidate,
            {.camera = camera,
             .near_plane = clips.near_plane,
             .far_plane = clips.far_plane});
        if (!tube.valid) {
          continue;
        }
        projected = std::move(tube.projected);
        projected.object_id = static_cast<int>(rectangle_entities_.size());
      } else {
        if (candidate.sample_payload == nullptr ||
            candidate.sample_payload->samples.size() < 2u) {
          continue;
        }
        const auto &samples = candidate.sample_payload->samples;
        projected.screen_points.reserve(samples.size() * 2u);
        projected.screen_segments.reserve(samples.size() - 1u);
        for (std::size_t index = 1; index < samples.size(); ++index) {
          const auto &a = samples[index - 1u].position;
          const auto &b = samples[index].position;
          const QVec3 start = transform_point(
              candidate.world_transform, {a.x, a.y, a.z});
          const QVec3 end = transform_point(
              candidate.world_transform, {b.x, b.y, b.z});
          if (start.x == end.x && start.y == end.y && start.z == end.z) {
            continue;
          }
          // Keep offscreen endpoints: clipping them to the viewport would make
          // a partly visible curve incorrectly satisfy whole-object containment.
          const auto segment = project_visible_viewport_segment(
              camera, start, end, clips.near_plane, clips.far_plane, false);
          if (!segment.has_value()) {
            continue;
          }
          quader_editor_core::include_projected_object_point(projected,
                                                              segment->start);
          quader_editor_core::include_projected_object_point(projected,
                                                              segment->end);
          projected.screen_segments.push_back(
              {segment->start, segment->end});
        }
        if (projected.screen_segments.empty()) {
          continue;
        }
      }
      rectangle_entities_.push_back(candidate.entity);
      rectangle_request_.objects.push_back(std::move(projected));
    }
    rectangle_source_revision_ = build_count_;
    rectangle_settings_revision_ = document.scene_settings_revision();
    rectangle_camera_ = request.camera;
    rectangle_valid_ = true;
    ++rectangle_build_count_;
  }
  rectangle_request_.rect =
      quader_editor_core::normalized_screen_rect(request.start, request.end);
  rectangle_request_.match =
      resolve_box_selection_rule(request.rule, request.start, request.end) ==
              ModelingBoxSelectionRule::Contain
          ? quader_editor_core::RectangleSelectionMatch::Contain
          : quader_editor_core::RectangleSelectionMatch::Crossing;
  const auto hits = quader_editor_core::rectangle_hits_from_projected_candidates(
      rectangle_request_);
  std::vector<EntityId> entities;
  entities.reserve(hits.object_ids.size());
  for (const int id : hits.object_ids) {
    entities.push_back(rectangle_entities_[static_cast<std::size_t>(id)]);
  }
  return entities;
}

void ViewportCurvePickCache::reset() {
  document_ = nullptr;
  assets_ = nullptr;
  candidates_.clear();
  build_count_ = 0;
  valid_ = false;
  rectangle_request_.objects.clear();
  rectangle_entities_.clear();
  rectangle_build_count_ = 0;
  rectangle_valid_ = false;
  tube_payload_cache_.clear();
  tube_payload_cached_bytes_ = 0u;
  tube_payload_use_sequence_ = 0u;
  tube_payload_build_count_ = 0u;
  if (tube_projection_cache_ != nullptr) {
    tube_projection_cache_->reset();
  }
}

} // namespace quader
