////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/overlay_renderable_builder.hpp"

#include "core/color_space.hpp"
#include "diagnostics/profile.hpp"
#include "render/render_signature_builder.hpp"
#include "render/overlay/overlay_camera_projector.hpp"
#include "render/overlay/overlay_face_fill_builder.hpp"
#include "render/overlay/overlay_line_mesh_builder.hpp"
#include "render/overlay/overlay_point_mesh_builder.hpp"

#include <quader/modeling/neutral/geometry/bvh.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <optional>
#include <thread>
#include <unordered_map>
#include <utility>

namespace quader {
namespace {

constexpr float kOverlayVisibilityEpsilon = 0.000001f;

bool overlay_layer_uses_depth_stamp_point_visibility(
    ViewportOverlayLayer layer) {
  return layer == ViewportOverlayLayer::UnselectedVertex ||
         layer == ViewportOverlayLayer::SelectedVertex ||
         layer == ViewportOverlayLayer::HoverVertex;
}

std::size_t overlay_layer_index(ViewportOverlayLayer layer) {
  return static_cast<std::size_t>(layer);
}

bool overlay_layer_can_build_face_fill(ViewportOverlayLayer layer) {
  return layer == ViewportOverlayLayer::SelectedVisibleFace ||
         layer == ViewportOverlayLayer::SelectedXrayFace ||
         layer == ViewportOverlayLayer::SelectedFaceMask ||
         layer == ViewportOverlayLayer::HoverFace ||
         layer == ViewportOverlayLayer::ToolPlane ||
         layer == ViewportOverlayLayer::ToolPreviewFill ||
         layer == ViewportOverlayLayer::ToolHandle;
}

bool overlay_layer_can_build_lines(ViewportOverlayLayer layer) {
  return layer == ViewportOverlayLayer::WireframeBase ||
         layer == ViewportOverlayLayer::WireframeXray ||
         layer == ViewportOverlayLayer::SourceWire ||
         layer == ViewportOverlayLayer::OpenEdge ||
         layer == ViewportOverlayLayer::DiagnosticEdge ||
         layer == ViewportOverlayLayer::SelectedFaceEdge ||
         layer == ViewportOverlayLayer::SelectedEdge ||
         layer == ViewportOverlayLayer::GroupBounds ||
         layer == ViewportOverlayLayer::HoverFaceEdge ||
         layer == ViewportOverlayLayer::HoverEdge ||
         layer == ViewportOverlayLayer::ToolPlaneLine ||
         layer == ViewportOverlayLayer::ToolPreviewLine ||
         layer == ViewportOverlayLayer::ToolPreviewSeam ||
         layer == ViewportOverlayLayer::ToolHandle;
}

bool overlay_layer_can_build_points(ViewportOverlayLayer layer) {
  return layer == ViewportOverlayLayer::UnselectedVertex ||
         layer == ViewportOverlayLayer::SelectedVertex ||
         layer == ViewportOverlayLayer::HoverVertex ||
         layer == ViewportOverlayLayer::SnapTargetVertex ||
         layer == ViewportOverlayLayer::ToolHandle;
}

bool overlay_layer_can_build_icons(ViewportOverlayLayer layer) {
  return layer == ViewportOverlayLayer::PointLightGizmo;
}

OverlayRenderState make_state_with_depth_mode(
    ViewportOverlayLayer layer, ViewportOverlayGeometryKind kind,
    std::uint16_t blend_order, ViewportOverlayDepthMode depth_mode) {
  const bool draw_on_top = depth_mode == ViewportOverlayDepthMode::DrawOnTop;
  OverlayRenderState state;
  // Highlights consume scene depth without turning a culled face into an
  // occluder for the component wires and points drawn after it.
  state.depth_write = false;
  state.color_write = true;
  state.depth_culling = !draw_on_top;
  state.depth_func = draw_on_top ? OverlayDepthFunc::Always
                                : OverlayDepthFunc::GreaterOrEqual;
  const bool two_sided_triangle =
      kind == ViewportOverlayGeometryKind::Triangles &&
      viewport_overlay_triangle_is_two_sided(layer);
  state.culling_mode =
      kind == ViewportOverlayGeometryKind::Triangles && !two_sided_triangle
          ? OverlayCullingMode::Back
          : OverlayCullingMode::None;
  state.transparency_mode =
      kind == ViewportOverlayGeometryKind::Triangles && two_sided_triangle
          ? OverlayTransparencyMode::TwoPassesTwoSides
      : kind == ViewportOverlayGeometryKind::Triangles
          ? OverlayTransparencyMode::TwoPassesOneSide
          : OverlayTransparencyMode::Default;
  if (kind == ViewportOverlayGeometryKind::Triangles &&
      layer == ViewportOverlayLayer::CollisionFill) {
    state.transparency_mode = OverlayTransparencyMode::Default;
  } else if (kind == ViewportOverlayGeometryKind::Triangles) {
    state.polygon_offset_scale = -1.0f;
    state.polygon_offset_constant = -1.0f;
  }
  state.blend_order = blend_order;
  return state;
}

bool edit_wire_layer(ViewportOverlayLayer layer) {
  return layer == ViewportOverlayLayer::SourceWire ||
         layer == ViewportOverlayLayer::SelectedFaceEdge ||
         layer == ViewportOverlayLayer::SelectedEdge ||
         layer == ViewportOverlayLayer::HoverFaceEdge ||
         layer == ViewportOverlayLayer::HoverEdge;
}

bool line_uses_edit_wire_batch(const ViewportOverlayLine &line) {
  return edit_wire_layer(line.layer) &&
      (line.source == ViewportOverlaySource::ComponentSelection ||
       line.source == ViewportOverlaySource::ComponentHover);
}

quader_geometry::QVec3f neutral_point(QVec3 value) {
  return {value.x, value.y, value.z};
}

} // namespace

std::size_t OverlayWireSurfaceSupportCache::EdgeKeyHash::operator()(
    const EdgeKey &key) const {
  std::size_t hash = static_cast<std::uint32_t>(key.object);
  hash = hash * 16777619u ^ static_cast<std::uint32_t>(key.first);
  return hash * 16777619u ^ static_cast<std::uint32_t>(key.second);
}

void OverlayWireSurfaceSupportCache::prepare(
    const ViewportOverlayFrame &frame, const OverlayFrameSignatures &signatures) {
  const auto signature = signatures.layer_signatures[
      overlay_layer_index(ViewportOverlayLayer::SourceWireDepthStamp)];
  if (prepared_ && signature_ == signature) return;
  QDR_PROFILE_SCOPE("Overlay retain incident wire surfaces");
  supports_.clear();
  supports_.reserve(signatures.triangle_counts[
      overlay_layer_index(ViewportOverlayLayer::SourceWireDepthStamp)] * 3u);
  const auto same_position = [](QVec3 left, QVec3 right) {
    return left.x == right.x && left.y == right.y && left.z == right.z;
  };
  const auto finite_position = [](QVec3 position) {
    return std::isfinite(position.x) && std::isfinite(position.y) &&
           std::isfinite(position.z);
  };
  for (const auto &triangle : frame.triangles) {
    if (triangle.layer != ViewportOverlayLayer::SourceWireDepthStamp ||
        (triangle.source != ViewportOverlaySource::ComponentSelection &&
         triangle.source != ViewportOverlaySource::Selection)) continue;
    const auto &element = triangle.element;
    const std::array ids{element.vertex_id, element.edge_a, element.edge_b};
    if (element.object_id < 0 || element.face_id < 0 ||
        std::any_of(ids.begin(), ids.end(), [](int id) { return id < 0; }) ||
        ids[0] == ids[1] || ids[1] == ids[2] || ids[0] == ids[2]) continue;
    const std::array positions{triangle.a, triangle.b, triangle.c};
    for (std::size_t edge = 0u; edge < 3u; ++edge) {
      const auto next = (edge + 1u) % 3u;
      const auto opposite = (edge + 2u) % 3u;
      const bool ordered = ids[edge] < ids[next];
      const EdgeKey key{element.object_id, std::min(ids[edge], ids[next]),
                        std::max(ids[edge], ids[next])};
      const auto first = positions[ordered ? edge : next];
      const auto second = positions[ordered ? next : edge];
      const auto [entry, inserted] = supports_.try_emplace(key);
      auto &support = entry->second;
      if (inserted) {
        support.first = first;
        support.second = second;
      }
      if (!finite_position(first) || !finite_position(second) ||
          !finite_position(positions[opposite]) ||
          !same_position(support.first, first) ||
          !same_position(support.second, second)) {
        support.ambiguous = true;
      }
      if (support.ambiguous ||
          std::find(support.faces.begin(), support.faces.end(), element.face_id) !=
              support.faces.end()) continue;
      if (support.count == support.points.size()) {
        support.ambiguous = true;
        continue;
      }
      support.faces[support.count] = element.face_id;
      support.points[support.count++] = {positions[opposite], ids[opposite]};
    }
  }
  signature_ = signature;
  prepared_ = true;
  ++build_count_;
}

void OverlayWireSurfaceSupportCache::apply(ViewportOverlayLine &line) const {
  if (!line_uses_edit_wire_batch(line) ||
      line.surface_support[0].vertex_id >= 0 || line.surface_support[1].vertex_id >= 0)
    return;
  const auto &element = line.element;
  if (element.object_id < 0 || element.edge_a < 0 || element.edge_b < 0 ||
      element.edge_a == element.edge_b) return;
  const auto found = supports_.find({element.object_id,
      std::min(element.edge_a, element.edge_b), std::max(element.edge_a, element.edge_b)});
  if (found == supports_.end() || found->second.ambiguous) return;
  const auto &support = found->second;
  const bool ordered = element.edge_a < element.edge_b;
  const auto first = ordered ? line.start : line.end;
  const auto second = ordered ? line.end : line.start;
  if (first.x != support.first.x || first.y != support.first.y ||
      first.z != support.first.z || second.x != support.second.x ||
      second.y != support.second.y || second.z != support.second.z) return;
  line.surface_support = support.points;
}

/**
 * Resolves editable point visibility against source-wire depth metadata.
 *
 * Each object uses the shared neutral triangle BVH. Camera motion therefore
 * costs O(points * log(triangles)) queries instead of the former quadratic
 * point-by-triangle scan.
 */
struct OverlayDepthStampPointVisibilityFilter::Impl final {
  /** Owns one object's source triangles, acceleration data, and orientation. */
  struct StampSet final {
    std::vector<quader_geometry::QTriangle3<float>> triangles;
    quader_geometry::QTriangleBvh<float> bvh;
    mutable quader_geometry::QBvhTraversalScratch traversal_scratch;
    QVec3 centroid;
    std::size_t vertex_count = 0u;
    bool inside_out = false;
  };

  /// Each worker owns traversal storage and counters; source BVHs stay immutable.
  struct QueryWorker final {
    quader_geometry::QBvhTraversalScratch traversal_scratch;
    OverlayDepthStampPointVisibilityStats work;
    std::exception_ptr failure;
  };

  /// Resolves frame indices and object lookups only when the semantic frame changes.
  struct QueryPoint final {
    std::size_t frame_index = 0u;
    const StampSet *stamps = nullptr;
  };

  /// Stages only changed objects; accepted BVHs own their triangles across frames.
  struct SourceCandidate final {
    const StampSet *previous = nullptr;
    std::size_t triangle_count = 0u;
    bool matches_previous = true;
    std::unique_ptr<StampSet> replacement;
  };

  [[nodiscard]] static bool same_point(quader_geometry::QVec3f left,
                                       QVec3 right) {
    return left.x == right.x && left.y == right.y && left.z == right.z;
  }

  void reconcile_sources(const ViewportOverlayFrame &frame) {
    QDR_PROFILE_FUNCTION();
    std::unordered_map<int, SourceCandidate> candidates;
    candidates.reserve(stamps_by_object.size());
    for (const ViewportOverlayTriangle &triangle : frame.triangles) {
      if (triangle.layer != ViewportOverlayLayer::SourceWireDepthStamp ||
          triangle.element.object_id < 0) {
        continue;
      }
      auto [found, inserted] = candidates.try_emplace(triangle.element.object_id);
      SourceCandidate &candidate = found->second;
      if (inserted) {
        const auto previous = stamps_by_object.find(triangle.element.object_id);
        if (previous != stamps_by_object.end()) {
          candidate.previous = previous->second.get();
        }
      }
      if (candidate.matches_previous) {
        if (candidate.previous == nullptr ||
            candidate.triangle_count >= candidate.previous->triangles.size()) {
          candidate.matches_previous = false;
        } else {
          const auto &previous =
              candidate.previous->triangles[candidate.triangle_count];
          candidate.matches_previous = same_point(previous.a, triangle.a) &&
              same_point(previous.b, triangle.b) && same_point(previous.c, triangle.c);
        }
      }
      ++candidate.triangle_count;
    }

    std::size_t object_build_count = 0u;
    std::size_t triangle_count = 0u;
    for (auto &[object_id, candidate] : candidates) {
      (void)object_id;
      triangle_count += candidate.triangle_count;
      candidate.matches_previous = candidate.matches_previous &&
          candidate.previous != nullptr &&
          candidate.triangle_count == candidate.previous->triangles.size();
      if (!candidate.matches_previous) {
        candidate.replacement = std::make_unique<StampSet>();
        candidate.replacement->triangles.reserve(candidate.triangle_count);
        ++object_build_count;
      }
    }
    if (object_build_count != 0u) {
      for (const ViewportOverlayTriangle &triangle : frame.triangles) {
        if (triangle.layer != ViewportOverlayLayer::SourceWireDepthStamp ||
            triangle.element.object_id < 0) {
          continue;
        }
        SourceCandidate &candidate = candidates.at(triangle.element.object_id);
        if (!candidate.replacement) continue;
        StampSet &set = *candidate.replacement;
        set.triangles.push_back({neutral_point(triangle.a),
                                 neutral_point(triangle.b),
                                 neutral_point(triangle.c)});
        set.centroid += triangle.a + triangle.b + triangle.c;
        set.vertex_count += 3u;
      }
    }
    for (auto &[object_id, candidate] : candidates) {
      (void)object_id;
      if (!candidate.replacement) continue;
      StampSet &set = *candidate.replacement;
      set.centroid =
          set.centroid / static_cast<float>(set.vertex_count);
      set.inside_out = inside_out_score(set) < -kOverlayVisibilityEpsilon;
      if (!set.bvh.build(set.triangles)) {
        continue;
      }
      set.traversal_scratch.reserve(set.bvh.required_traversal_capacity());
    }

    const bool sources_changed = object_build_count != 0u ||
        candidates.size() != stamps_by_object.size();
    if (sources_changed) {
      // Query bindings never outlive a replaced or removed source. Objects
      // absent from this frame are evicted, without retaining historical BVHs.
      query_points.clear();
      query_frame_valid = false;
      visibility_valid = false;
      std::erase_if(stamps_by_object, [&](const auto &entry) {
        return !candidates.contains(entry.first);
      });
      for (auto &[object_id, candidate] : candidates) {
        if (candidate.replacement) {
          stamps_by_object.insert_or_assign(object_id,
                                            std::move(candidate.replacement));
        }
      }
    }
    ++work.build_count;
    work.object_build_count += object_build_count;
    work.object_reuse_count += candidates.size() - object_build_count;
    work.object_count = stamps_by_object.size();
    work.triangle_count = triangle_count;
  }

  void resolve_query_points(const ViewportOverlayFrame &frame,
                            const OverlayFrameSignatures &signatures) {
    if (query_frame_valid && query_frame_signature == signatures.frame_signature) return;
    RenderSignatureBuilder point_signature;
    point_signature.add_size(frame.points.size());
    for (const auto &point : frame.points) {
      const bool eligible = overlay_layer_uses_depth_stamp_point_visibility(point.layer);
      point_signature.add_bool(eligible);
      if (!eligible) continue;
      // Color, style and equivalent selection layers do not change a center
      // ray. Effective extent still controls conservative viewport skipping.
      point_signature.add_value(static_cast<std::uint64_t>(point.element.object_id))
          .add_vec3(point.position)
          .add_float(point_radius(point));
    }
    query_frame_signature = signatures.frame_signature;
    if (query_frame_valid && query_point_signature == point_signature.value()) return;
    query_points.clear();
    query_points.reserve(frame.points.size());
    visibility.assign(frame.points.size(), 1u);
    for (std::size_t index = 0u; index < frame.points.size(); ++index) {
      const auto &point = frame.points[index];
      if (!overlay_layer_uses_depth_stamp_point_visibility(point.layer)) continue;
      const auto found = stamps_by_object.find(point.element.object_id);
      if (found == stamps_by_object.end() || found->second->inside_out) continue;
      query_points.push_back({.frame_index = index, .stamps = found->second.get()});
    }
    query_point_signature = point_signature.value();
    query_frame_valid = true;
    visibility_valid = false;
  }

  [[nodiscard]] static float inside_out_score(const StampSet &stamps) {
    float score = 0.0f;
    for (const auto &triangle : stamps.triangles) {
      const QVec3 a{triangle.a.x, triangle.a.y, triangle.a.z};
      const QVec3 b{triangle.b.x, triangle.b.y, triangle.b.z};
      const QVec3 c{triangle.c.x, triangle.c.y, triangle.c.z};
      const QVec3 normal = cross(b - a, c - a);
      const QVec3 triangle_centroid = (a + b + c) / 3.0f;
      score += dot(normal, triangle_centroid - stamps.centroid);
    }
    return score;
  }

  [[nodiscard]] static bool point_visible(
      QVec3 point, const ViewportOverlayCamera &camera,
      const StampSet &stamps,
      OverlayDepthStampPointVisibilityStats &work,
      quader_geometry::QBvhTraversalScratch &traversal_scratch) {
    if (stamps.bvh.empty()) {
      return true;
    }
    const QVec3 forward = camera.forward;
    QVec3 origin;
    QVec3 direction;
    float target_distance = 0.0f;
    float clip_distance = camera.near_plane;
    if (camera.projection == ViewportOverlayProjection::Orthographic) {
      target_distance = dot(point - camera.eye, forward);
      if (target_distance <= 0.0001f) {
        return true;
      }
      origin = point - (forward * target_distance);
      direction = forward;
    } else {
      const QVec3 to_point = point - camera.eye;
      target_distance = length(to_point);
      if (target_distance <= 0.0001f) {
        return true;
      }
      origin = camera.eye;
      direction = to_point / target_distance;
      const float forward_fraction = dot(direction, forward);
      if (forward_fraction <= kOverlayVisibilityEpsilon) {
        return true;
      }
      // The near plane is perpendicular to the camera, not to this pixel's
      // ray. At the viewport edge its ray distance exceeds near_plane.
      clip_distance /= forward_fraction;
    }

    const float occlusion_tolerance =
        std::max(0.015f, target_distance * 0.0005f);
    const float max_distance = target_distance - occlusion_tolerance;
    if (max_distance <= clip_distance) {
      return true;
    }
    quader_geometry::QTriangleBvhQueryOptions<float> options;
    options.min_distance = clip_distance;
    options.max_distance = max_distance;
    options.intersection_epsilon = kOverlayVisibilityEpsilon;
    ++work.ray_query_count;
    return !stamps.bvh.any_hit(
        {neutral_point(origin), neutral_point(direction)}, options,
        traversal_scratch);
  }

  [[nodiscard]] static float point_radius(const ViewportOverlayPoint &point) {
    const float size = viewport_overlay_point_size_pixels(
        point.layer, point.size_pixels);
    const float outline_growth = point.outline_growth_pixels.value_or(
        viewport_overlay_point_outline_growth_pixels(point.layer));
    // Include the larger outline quad and the shader's half-pixel circular
    // coverage, plus one physical pixel for conservative raster rounding.
    return std::max(1.0f, size + std::max(0.0f, outline_growth)) * 0.5f + 1.5f;
  }

  [[nodiscard]] bool outside_viewport(const ViewportOverlayPoint &point) const {
    if (!projector.has_value()) {
      return false;
    }
    const OverlayViewPoint view = projector->view_point_for(point.position);
    if (projector->near_plane_distance(view) < 0.0f) {
      // The point shader discards the entire marker before its near plane.
      return true;
    }
    const OverlayProjectedPoint projected = projector->project(view);
    if (!projected.valid) {
      return false;
    }
    const float radius = point_radius(point);
    return projected.pixel.x < -radius || projected.pixel.y < -radius ||
           projected.pixel.x > camera.viewport_size.x + radius ||
           projected.pixel.y > camera.viewport_size.y + radius;
  }

  std::unordered_map<int, std::unique_ptr<StampSet>> stamps_by_object;
  ViewportOverlayCamera camera;
  std::optional<OverlayCameraProjector> projector;
  OverlayDepthStampPointVisibilityStats work;
  std::uint64_t source_signature = 0u;
  bool source_valid = false;
  bool allow_viewport_ray_skips = true;
  std::vector<QueryPoint> query_points;
  std::vector<QueryWorker> query_workers;
  std::vector<std::uint8_t> visibility;
  std::uint64_t query_frame_signature = 0u;
  std::uint64_t query_point_signature = 0u;
  std::uint64_t visibility_camera_signature = 0u;
  bool query_frame_valid = false;
  bool visibility_valid = false;
  bool visibility_allows_viewport_skips = true;
};

OverlayDepthStampPointVisibilityFilter::OverlayDepthStampPointVisibilityFilter() =
    default;

OverlayDepthStampPointVisibilityFilter::~OverlayDepthStampPointVisibilityFilter() =
    default;

void OverlayDepthStampPointVisibilityFilter::prepare(
    const ViewportOverlayFrame &frame, const OverlayFrameSignatures &signatures,
    const ViewportOverlayCamera &camera, bool allow_viewport_ray_skips) {
  if (!impl_) {
    impl_ = std::make_unique<Impl>();
  }
  const auto source_signature = signatures.layer_signatures[
      overlay_layer_index(ViewportOverlayLayer::SourceWireDepthStamp)];
  if (!impl_->source_valid || impl_->source_signature != source_signature) {
    impl_->reconcile_sources(frame);
    impl_->source_signature = source_signature;
    impl_->source_valid = true;
  }
  // Query rays use only this immutable layer camera. In particular, near-plane
  // clipping must not normalize the camera basis separately for every marker.
  impl_->camera = OverlayCameraProjector::sanitized_camera(camera);
  impl_->projector.emplace(impl_->camera);
  impl_->allow_viewport_ray_skips = allow_viewport_ray_skips;
}

void OverlayDepthStampPointVisibilityFilter::invalidate() {
  if (impl_) {
    impl_->source_valid = false;
    impl_->stamps_by_object.clear();
    impl_->query_points.clear();
    impl_->query_frame_valid = false;
    impl_->visibility_valid = false;
    impl_->work.object_count = 0u;
    impl_->work.triangle_count = 0u;
  }
}

bool OverlayDepthStampPointVisibilityFilter::point_visible(
    const ViewportOverlayPoint &point) const {
  if (!impl_ || !impl_->source_valid) {
    return true;
  }
  if (impl_->allow_viewport_ray_skips && impl_->outside_viewport(point)) {
    ++impl_->work.viewport_skip_count;
    // Keep the marker and its position-stream binding: a component preview
    // can move this offscreen base point onscreen without rebuilding topology.
    // Raster clipping handles it until then, so no CPU occlusion ray is needed.
    return true;
  }
  const auto found = impl_->stamps_by_object.find(point.element.object_id);
  return found == impl_->stamps_by_object.end() || found->second->inside_out ||
         Impl::point_visible(point.position, impl_->camera, *found->second,
                             impl_->work, found->second->traversal_scratch);
}

std::span<const std::uint8_t> OverlayDepthStampPointVisibilityFilter::evaluate(
    const ViewportOverlayFrame &frame, const OverlayFrameSignatures &signatures,
    const ViewportOverlayCamera &camera, bool allow_viewport_ray_skips,
    bool allow_parallel_queries) {
  QDR_PROFILE_SCOPE("Overlay evaluate retained point visibility");
  prepare(frame, signatures, camera, allow_viewport_ray_skips);
  impl_->resolve_query_points(frame, signatures);
  const auto camera_signature = OverlayRenderableBuilder::camera_signature(impl_->camera);
  if (impl_->visibility_valid &&
      impl_->visibility_camera_signature == camera_signature &&
      impl_->visibility_allows_viewport_skips == allow_viewport_ray_skips) {
    return impl_->visibility;
  }

  constexpr std::size_t kMinimumParallelPoints = 4096u;
  constexpr std::size_t kPointsPerWorker = 2048u;
  constexpr std::size_t kMaximumWorkers = 8u;
  const std::size_t available_workers = std::max(1u, std::thread::hardware_concurrency());
  const std::size_t worker_count = allow_parallel_queries &&
          impl_->query_points.size() >= kMinimumParallelPoints
      ? std::max<std::size_t>(1u, std::min({kMaximumWorkers, available_workers,
                                          impl_->query_points.size() / kPointsPerWorker}))
      : 1u;
  impl_->visibility_valid = false;
  impl_->query_workers.resize(worker_count);
  const auto query_range = [&](std::size_t worker_index) {
    auto &worker = impl_->query_workers[worker_index];
    worker.work = {};
    worker.failure = nullptr;
    try {
      const std::size_t first = impl_->query_points.size() * worker_index / worker_count;
      const std::size_t end = impl_->query_points.size() * (worker_index + 1u) / worker_count;
      for (std::size_t index = first; index < end; ++index) {
        const auto &query = impl_->query_points[index];
        const auto &point = frame.points[query.frame_index];
        bool visible = true;
        if (allow_viewport_ray_skips && impl_->outside_viewport(point)) {
          ++worker.work.viewport_skip_count;
        } else {
          visible = Impl::point_visible(point.position, impl_->camera, *query.stamps,
                                        worker.work, worker.traversal_scratch);
        }
        impl_->visibility[query.frame_index] = visible ? 1u : 0u;
      }
    } catch (...) {
      worker.failure = std::current_exception();
    }
  };
  {
    std::vector<std::jthread> workers;
    workers.reserve(worker_count - 1u);
    for (std::size_t index = 1u; index < worker_count; ++index) {
      workers.emplace_back(query_range, index);
    }
    query_range(0u);
  }
  for (const auto &worker : impl_->query_workers) {
    if (worker.failure) std::rethrow_exception(worker.failure);
    impl_->work.viewport_skip_count += worker.work.viewport_skip_count;
    impl_->work.ray_query_count += worker.work.ray_query_count;
  }
  ++impl_->work.query_batch_count;
  impl_->work.parallel_query_batch_count += worker_count > 1u ? 1u : 0u;
  impl_->work.queried_point_count += impl_->query_points.size();
  impl_->visibility_camera_signature = camera_signature;
  impl_->visibility_allows_viewport_skips = allow_viewport_ray_skips;
  impl_->visibility_valid = true;
  return impl_->visibility;
}

OverlayDepthStampPointVisibilityStats
OverlayDepthStampPointVisibilityFilter::stats() const {
  return impl_ ? impl_->work : OverlayDepthStampPointVisibilityStats{};
}

std::uint64_t
OverlayRenderableBuilder::frame_signature(const ViewportOverlayFrame &frame) {
  return RenderSignatureBuilder::overlay_frame_signature(frame);
}

OverlayFrameSignatures
OverlayRenderableBuilder::frame_signatures(const ViewportOverlayFrame &frame) {
  return RenderSignatureBuilder::overlay_frame_signatures(frame);
}

std::uint64_t OverlayRenderableBuilder::camera_signature(
    const ViewportOverlayCamera &camera) {
  return RenderSignatureBuilder::overlay_camera_signature(camera);
}

std::uint64_t
OverlayRenderableBuilder::layer_signature(const ViewportOverlayFrame &frame,
                                          ViewportOverlayLayer layer) {
  return RenderSignatureBuilder::overlay_layer_signature(frame, layer);
}

std::uint64_t OverlayRenderableBuilder::layer_signature(
    const OverlayFrameSignatures &signatures, ViewportOverlayLayer layer) {
  return signatures.layer_signatures[overlay_layer_index(layer)];
}

bool OverlayRenderableBuilder::layer_build_depends_on_camera(
    ViewportOverlayLayer layer) {
  // Lines retain world-space geometry but their projection and clip range are
  // part of the batch state. Points normally retain their world-space center;
  // source depth metadata additionally enables camera-dependent BVH culling.
  return overlay_layer_can_build_lines(layer) ||
         overlay_layer_can_build_points(layer) ||
         overlay_layer_can_build_icons(layer);
}

bool OverlayRenderableBuilder::layer_has_renderable_primitives(
    const OverlayFrameSignatures &signatures, ViewportOverlayLayer layer) {
  const std::size_t layer_index = overlay_layer_index(layer);
  return (overlay_layer_can_build_face_fill(layer) &&
          signatures.triangle_counts[layer_index] > 0u) ||
         (overlay_layer_can_build_lines(layer) &&
          signatures.line_counts[layer_index] > 0u) ||
         (overlay_layer_can_build_points(layer) &&
          signatures.point_counts[layer_index] > 0u) ||
         (overlay_layer_can_build_icons(layer) &&
          signatures.icon_counts[layer_index] > 0u);
}

std::uint64_t OverlayRenderableBuilder::layer_build_signature(
    const ViewportOverlayFrame &frame, const ViewportOverlayCamera &camera,
    ViewportOverlayLayer layer) {
  return layer_build_signature(frame_signatures(frame), camera, layer);
}

std::uint64_t OverlayRenderableBuilder::layer_build_signature(
    const OverlayFrameSignatures &signatures,
    const ViewportOverlayCamera &camera, ViewportOverlayLayer layer,
    bool retain_point_geometry) {
  RenderSignatureBuilder signature;
  signature.add_value(layer_signature(signatures, layer));
  if (edit_wire_layer(layer)) {
    // Incident face motion changes wire depth even when its endpoints stay put.
    signature.add_value(layer_signature(
        signatures, ViewportOverlayLayer::SourceWireDepthStamp));
  }
  if (retain_point_geometry && overlay_layer_can_build_points(layer)) {
    // Camera/source-face visibility lives in a separate flag stream. Point
    // topology, centers, colors and source bindings follow only their owner.
    return signature.add_bool(true).value();
  }
  if (overlay_layer_uses_depth_stamp_point_visibility(layer)) {
    // Filtered point geometry also depends on other faces of its source mesh.
    // Keep that dependency when stamps move, appear, or disappear while the
    // point positions and camera stay unchanged.
    signature.add_value(layer_signature(
        signatures, ViewportOverlayLayer::SourceWireDepthStamp));
  }
  if (layer_build_depends_on_camera(layer)) {
    const ViewportOverlayCamera sanitized =
        OverlayCameraProjector::sanitized_camera(camera);
    if (overlay_layer_can_build_icons(layer) ||
        (overlay_layer_uses_depth_stamp_point_visibility(layer) &&
         signatures.has_source_wire_depth_stamp)) {
      signature.add_value(camera_signature(sanitized));
    } else if (overlay_layer_can_build_lines(layer)) {
      // Line endpoints stay in retained world space. Only material projection
      // and clip state can invalidate their GPU batches; pose is patched by the
      // retained camera update path.
      signature
          .add_value(static_cast<std::uint64_t>(sanitized.projection))
          .add_float(sanitized.near_plane)
          .add_float(sanitized.far_plane);
    }
  }
  return signature.value();
}

OverlayRenderState OverlayRenderableBuilder::make_state(
    ViewportOverlayLayer layer, ViewportOverlayGeometryKind kind,
    std::uint16_t blend_order) {
  return make_state_with_depth_mode(
      layer, kind, blend_order, viewport_overlay_depth_mode(layer, kind));
}

OverlayEditWireBatch OverlayRenderableBuilder::edit_wire_presentation(
    ViewportOverlayLayer layer, float width_pixels,
    const ViewportOverlayCamera &camera, std::uint16_t blend_order) {
  const auto sanitized = OverlayCameraProjector::sanitized_camera(camera);
  return {.state = make_state_with_depth_mode(layer, ViewportOverlayGeometryKind::Lines,
              blend_order, ViewportOverlayDepthMode::DepthTested),
          .width_pixels = width_pixels,
          .near_plane = sanitized.near_plane,
          .far_plane = sanitized.far_plane,
          .orthographic_projection = sanitized.projection == ViewportOverlayProjection::Orthographic};
}

OverlayLayerBuildPlan OverlayRenderableBuilder::build_layer(
    const ViewportOverlayFrame &frame, const ViewportOverlayCamera &camera,
    ViewportOverlayLayer layer, const OverlayFrameSignatures *signatures,
    std::uint16_t blend_order_bias,
    OverlayDepthStampPointVisibilityFilter *point_visibility,
    bool allow_viewport_ray_skips, bool retain_point_geometry,
    const OverlayWireSurfaceSupportCache *wire_surface_support) {
  QDR_PROFILE_FUNCTION();
  OverlayLayerBuildPlan plan;
  const ViewportOverlayCamera sanitized_camera =
      OverlayCameraProjector::sanitized_camera(camera);
  if (layer == ViewportOverlayLayer::SourceWireDepthStamp) {
    // Point-visibility metadata only; this triangle stream adds no GPU pass.
    // Scene and moving-preview overlay-depth companions own GPU occlusion.
    return plan;
  }

  const OverlayFrameSignatures local_signatures =
      signatures == nullptr ? frame_signatures(frame)
                            : OverlayFrameSignatures{};
  const OverlayFrameSignatures &frame_signatures =
      signatures == nullptr ? local_signatures : *signatures;
  if (!layer_has_renderable_primitives(frame_signatures, layer)) {
    return plan;
  }

  std::uint16_t blend_order =
      static_cast<std::uint16_t>(viewport_overlay_draw_order(layer) * 64u +
                                 blend_order_bias);
  if (overlay_layer_can_build_face_fill(layer)) {
    QDR_PROFILE_SCOPE("Overlay build face fill mesh");
    constexpr std::size_t kClassCount =
        static_cast<std::size_t>(OverlayFaceFillClass::RemoveSelection) + 1u;
    std::array<std::size_t, kClassCount> triangle_counts{};
    for (const ViewportOverlayTriangle &triangle : frame.triangles) {
      if (triangle.layer == layer) {
        ++triangle_counts[static_cast<std::size_t>(
            overlay_face_fill_class(triangle))];
      }
    }
    std::array<OverlayFaceFillMesh, kClassCount> meshes;
    for (std::size_t index = 0u; index < kClassCount; ++index) {
      meshes[index].vertices.reserve(triangle_counts[index] * 3u);
      meshes[index].indices.reserve(triangle_counts[index] * 3u);
    }
    for (const ViewportOverlayTriangle &triangle : frame.triangles) {
      if (triangle.layer == layer) {
        OverlayFaceFillBuilder::append_triangle(
            meshes[static_cast<std::size_t>(overlay_face_fill_class(triangle))],
            triangle);
      }
    }
    for (std::size_t index = 0u; index < kClassCount; ++index) {
      OverlayFaceFillMesh &mesh = meshes[index];
      if (mesh.vertices.empty()) {
        continue;
      }
      for (std::uint32_t vertex = 0u; vertex < mesh.vertices.size(); ++vertex) {
        mesh.indices.push_back(vertex);
      }
      const auto fill_class = static_cast<OverlayFaceFillClass>(index);
      OverlayRenderState color_state = make_state(
          layer, ViewportOverlayGeometryKind::Triangles, blend_order++);
      color_state.face_fill_class = fill_class;
      if (fill_class != OverlayFaceFillClass::None) {
        OverlayRenderState mask_state = color_state;
        mask_state.color_write = false;
        mask_state.face_fill_pass = OverlayFaceFillPass::Mask;
        mask_state.blend_order =
            overlay_face_fill_mask_blend_order(blend_order_bias);
        plan.face_fill_batches.push_back({
            .mesh = mesh,
            .state = mask_state,
        });
      }
      plan.face_fill_batches.push_back({
          .mesh = std::move(mesh),
          .state = color_state,
      });
    }
  }

  if (overlay_layer_can_build_lines(layer)) {
    const std::vector<const ViewportOverlayLine *> lines = [&]() {
      QDR_PROFILE_SCOPE("Overlay collect layer lines");
      return lines_for_layer(frame, layer);
    }();
    std::vector<const ViewportOverlayLine *> edit_wire_lines;
    std::vector<const ViewportOverlayLine *> generic_lines;
    edit_wire_lines.reserve(lines.size());
    generic_lines.reserve(lines.size());
    for (const ViewportOverlayLine *line : lines) {
      if (line_uses_edit_wire_batch(*line)) {
        edit_wire_lines.push_back(line);
      } else {
        generic_lines.push_back(line);
      }
    }
    if (!edit_wire_lines.empty()) {
      QDR_PROFILE_SCOPE("Overlay build edit wire batch");
      OverlayWireSurfaceSupportCache local_support;
      if (!wire_surface_support) {
        local_support.prepare(frame, frame_signatures);
        wire_surface_support = &local_support;
      }
      std::vector<ViewportOverlayLine> supported_lines;
      supported_lines.reserve(edit_wire_lines.size());
      for (auto &line : edit_wire_lines) {
        supported_lines.push_back(*line);
        wire_surface_support->apply(supported_lines.back());
        line = &supported_lines.back();
      }
      std::vector<float> edit_wire_widths;
      for (const ViewportOverlayLine *line : edit_wire_lines) {
        const float resolved_width = viewport_overlay_line_width_pixels(
            layer, line->width_pixels);
        if (std::find(edit_wire_widths.begin(), edit_wire_widths.end(),
                      resolved_width) == edit_wire_widths.end()) {
          edit_wire_widths.push_back(resolved_width);
        }
      }
      for (const float width_pixels : edit_wire_widths) {
        std::vector<const ViewportOverlayLine *> batch_lines;
        batch_lines.reserve(edit_wire_lines.size());
        for (const ViewportOverlayLine *line : edit_wire_lines) {
          if (viewport_overlay_line_width_pixels(layer, line->width_pixels) ==
              width_pixels) {
            batch_lines.push_back(line);
          }
        }
        OverlayEditWireMesh mesh = OverlayLineMeshBuilder::build(
            batch_lines, camera, width_pixels, 0.0f);
        if (!mesh.empty()) {
          auto batch = edit_wire_presentation(layer, width_pixels, sanitized_camera, blend_order++);
          batch.mesh = std::move(mesh);
          plan.edit_wire_batches.push_back(std::move(batch));
        }
      }
    }
    const std::vector<LineBatchKey> line_keys =
        line_batch_keys(generic_lines, layer);
    // Visible contour draws claim stencil coverage before hidden contours,
    // including when authored widths partition them into multiple batches.
    std::uint16_t visible_blend_order = blend_order;
    std::uint16_t occluded_blend_order = static_cast<std::uint16_t>(
        blend_order + line_keys.size());
    blend_order = static_cast<std::uint16_t>(
        occluded_blend_order + std::count_if(
            line_keys.begin(), line_keys.end(), [](const LineBatchKey &key) {
              return key.object_silhouette;
            }));
    for (const LineBatchKey &key : line_keys) {
      QDR_PROFILE_SCOPE("Overlay build line batch");
      std::vector<const ViewportOverlayLine *> batch_lines;
      batch_lines.reserve(generic_lines.size());
      for (const ViewportOverlayLine *line : generic_lines) {
        if (viewport_overlay_line_width_pixels(layer, line->width_pixels) !=
                key.width_pixels ||
            viewport_overlay_depth_mode(*line) != key.depth_mode ||
            (line->source == ViewportOverlaySource::MeshOutline) !=
                key.object_silhouette) {
          continue;
        }
        batch_lines.push_back(line);
      }
      const float depth_bias = viewport_overlay_depth_bias(
          key.depth_mode, ViewportOverlayGeometryKind::Lines);
      OverlayLineMesh mesh = OverlayLineMeshBuilder::build(
          batch_lines, camera, key.width_pixels, depth_bias);
      if (mesh.empty()) {
        continue;
      }
      OverlayRenderState state = make_state_with_depth_mode(
          layer, ViewportOverlayGeometryKind::Lines, visible_blend_order++,
          key.depth_mode);
      std::optional<OverlayRenderState> occluded_state;
      if (key.object_silhouette) {
        state.silhouette_pass = OverlaySilhouettePass::Visible;
        occluded_state = state;
        occluded_state->depth_func = OverlayDepthFunc::Less;
        occluded_state->silhouette_pass = OverlaySilhouettePass::Occluded;
        occluded_state->blend_order = occluded_blend_order++;
      }
      plan.line_batches.push_back({
          .mesh = std::move(mesh),
          .state = state,
          .occluded_state = occluded_state,
          .width_pixels = key.width_pixels,
          .depth_bias = depth_bias,
          .near_plane = sanitized_camera.near_plane,
      });
    }
  }

  if (overlay_layer_can_build_points(layer)) {
    OverlayDepthStampPointVisibilityFilter local_visibility;
    OverlayDepthStampPointVisibilityFilter *visibility = nullptr;
    if (!retain_point_geometry && overlay_layer_uses_depth_stamp_point_visibility(layer) &&
        frame_signatures.has_source_wire_depth_stamp) {
      visibility = point_visibility != nullptr ? point_visibility
                                                : &local_visibility;
      visibility->prepare(frame, frame_signatures, sanitized_camera,
                          allow_viewport_ray_skips);
    }
    const std::vector<const ViewportOverlayPoint *> points = [&]() {
      QDR_PROFILE_SCOPE("Overlay collect layer points");
      return points_for_layer(frame, layer, visibility);
    }();
    {
      QDR_PROFILE_SCOPE("Overlay build point outline pass");
      append_point_pass(plan, points, camera, layer, blend_order, true,
                        retain_point_geometry ? frame.points.data() : nullptr);
    }
    {
      QDR_PROFILE_SCOPE("Overlay build point fill pass");
      append_point_pass(plan, points, camera, layer, blend_order, false,
                        retain_point_geometry ? frame.points.data() : nullptr);
    }
  }
  if (overlay_layer_can_build_icons(layer)) {
    const std::vector<const ViewportOverlayIcon *> icons = [&]() {
      QDR_PROFILE_SCOPE("Overlay collect layer icons");
      return icons_for_layer(frame, layer);
    }();
    {
      QDR_PROFILE_SCOPE("Overlay build icon pass");
      append_icon_pass(plan, icons, camera, layer, blend_order);
    }
  }
  return plan;
}

std::vector<const ViewportOverlayLine *>
OverlayRenderableBuilder::lines_for_layer(
    const ViewportOverlayFrame &frame, ViewportOverlayLayer layer) {
  std::vector<const ViewportOverlayLine *> lines;
  lines.reserve(frame.lines.size());
  for (const ViewportOverlayLine &line : frame.lines) {
    if (line.layer == layer) {
      lines.push_back(&line);
    }
  }
  return lines;
}

std::vector<const ViewportOverlayPoint *>
OverlayRenderableBuilder::points_for_layer(
    const ViewportOverlayFrame &frame, ViewportOverlayLayer layer,
    const OverlayDepthStampPointVisibilityFilter *visibility) {
  std::vector<const ViewportOverlayPoint *> points;
  points.reserve(frame.points.size());
  for (const ViewportOverlayPoint &point : frame.points) {
    if (point.layer == layer &&
        (visibility == nullptr || visibility->point_visible(point))) {
      points.push_back(&point);
    }
  }
  return points;
}

std::vector<const ViewportOverlayIcon *>
OverlayRenderableBuilder::icons_for_layer(const ViewportOverlayFrame &frame,
                                          ViewportOverlayLayer layer) {
  std::vector<const ViewportOverlayIcon *> icons;
  icons.reserve(frame.icons.size());
  for (const ViewportOverlayIcon &icon : frame.icons) {
    if (icon.layer == layer) {
      icons.push_back(&icon);
    }
  }
  return icons;
}

std::vector<OverlayRenderableBuilder::LineBatchKey>
OverlayRenderableBuilder::line_batch_keys(
    const std::vector<const ViewportOverlayLine *> &lines,
    ViewportOverlayLayer layer) {
  std::vector<LineBatchKey> keys;
  for (const ViewportOverlayLine *line : lines) {
    const LineBatchKey key{
        .width_pixels =
            viewport_overlay_line_width_pixels(layer, line->width_pixels),
        .depth_mode = viewport_overlay_depth_mode(*line),
        .object_silhouette =
            line->source == ViewportOverlaySource::MeshOutline,
    };
    const auto existing = std::find_if(
        keys.begin(), keys.end(), [&](const LineBatchKey &candidate) {
          return candidate.width_pixels == key.width_pixels &&
                 candidate.depth_mode == key.depth_mode &&
                 candidate.object_silhouette == key.object_silhouette;
        });
    if (existing == keys.end()) {
      keys.push_back(key);
    }
  }
  return keys;
}

void OverlayRenderableBuilder::append_point_pass(
    OverlayLayerBuildPlan &plan,
    const std::vector<const ViewportOverlayPoint *> &points,
    const ViewportOverlayCamera &camera, ViewportOverlayLayer layer,
    std::uint16_t &blend_order, bool outline,
    const ViewportOverlayPoint *frame_points) {
  // An inward-facing room needs the same scene-depth test as an outward mesh.
  // Camera containment must never turn editable vertices into X-ray handles;
  // the GPU retains visible corners while rejecting walls in front of them.
  const auto depth_mode_for = [](const ViewportOverlayPoint *point) {
    return viewport_overlay_depth_mode(*point);
  };
  const auto outline_growth_for = [layer](const ViewportOverlayPoint *point) {
    return point->outline_growth_pixels.value_or(
        viewport_overlay_point_outline_growth_pixels(layer));
  };
  const bool any_outline = std::any_of(
      points.begin(), points.end(), [&](const ViewportOverlayPoint *point) {
        return outline_growth_for(point) > 0.0f ||
               viewport_overlay_point_is_outline_only(layer);
      });
  if (outline && !any_outline) {
    return;
  }
  if (!outline && viewport_overlay_point_is_outline_only(layer)) {
    return;
  }

  std::vector<PointBatchKey> point_keys;
  for (const ViewportOverlayPoint *point : points) {
    const float base_size =
        viewport_overlay_point_size_pixels(layer, point->size_pixels);
    const bool outline_uses_native_color =
        layer == ViewportOverlayLayer::SnapTargetVertex;
    const float size_pixels = outline && !outline_uses_native_color
                                  ? base_size + outline_growth_for(point)
                                  : base_size;
    const PointBatchKey key{
        .size_pixels = size_pixels,
        .depth_mode = depth_mode_for(point),
    };
    const auto existing = std::find_if(
        point_keys.begin(), point_keys.end(),
        [&](const PointBatchKey &candidate) {
          return candidate.size_pixels == key.size_pixels &&
                 candidate.depth_mode == key.depth_mode;
        });
    if (existing == point_keys.end()) {
      point_keys.push_back(key);
    }
  }

  for (const PointBatchKey &key : point_keys) {
    const float depth_bias = viewport_overlay_depth_bias(
        key.depth_mode, ViewportOverlayGeometryKind::Points);
    std::vector<const ViewportOverlayPoint *> batch_points;
    batch_points.reserve(points.size());
    for (const ViewportOverlayPoint *point : points) {
      const float base_size =
          viewport_overlay_point_size_pixels(layer, point->size_pixels);
      const bool outline_uses_native_color =
          layer == ViewportOverlayLayer::SnapTargetVertex;
      const float point_size = outline && !outline_uses_native_color
                                   ? base_size + outline_growth_for(point)
                                   : base_size;
      if (point_size == key.size_pixels &&
          depth_mode_for(point) == key.depth_mode) {
        batch_points.push_back(point);
      }
    }

    const bool override_color =
        outline && layer != ViewportOverlayLayer::SnapTargetVertex;
    OverlayPointMesh mesh = OverlayPointMeshBuilder::build(
        batch_points, camera, key.size_pixels, depth_bias,
        kOverlayVertexOutlineColor, override_color);
    if (!mesh.empty()) {
      std::vector<std::size_t> visibility_point_indices;
      if (frame_points != nullptr) {
        visibility_point_indices.reserve(batch_points.size());
        for (const auto *point : batch_points) {
          visibility_point_indices.push_back(static_cast<std::size_t>(point - frame_points));
        }
      }
      plan.point_batches.push_back({
          .mesh = std::move(mesh),
          .state = make_state_with_depth_mode(
              layer, ViewportOverlayGeometryKind::Points, blend_order++,
              key.depth_mode),
          .size_pixels = key.size_pixels,
          .inner_radius =
              outline && viewport_overlay_point_is_outline_only(layer)
                  ? viewport_overlay_point_inner_radius(layer)
                  : 0.0f,
          .depth_bias = depth_bias,
          .near_plane = OverlayCameraProjector::sanitized_camera(camera).near_plane,
          .visibility_point_indices = std::move(visibility_point_indices),
      });
    }
  }
}

void OverlayRenderableBuilder::append_icon_pass(
    OverlayLayerBuildPlan &plan,
    const std::vector<const ViewportOverlayIcon *> &icons,
    const ViewportOverlayCamera &camera, ViewportOverlayLayer layer,
    std::uint16_t &blend_order) {
  std::vector<IconBatchKey> keys;
  for (const ViewportOverlayIcon *icon : icons) {
    const IconBatchKey key{
        .icon = icon->kind,
        .size_pixels = viewport_overlay_point_size_pixels(layer,
                                                          icon->size_pixels),
    };
    const auto existing = std::find_if(
        keys.begin(), keys.end(), [&](const IconBatchKey &candidate) {
          return candidate.icon == key.icon &&
                 candidate.size_pixels == key.size_pixels;
        });
    if (existing == keys.end()) {
      keys.push_back(key);
    }
  }

  const float depth_bias =
      viewport_overlay_depth_bias(layer, ViewportOverlayGeometryKind::Icons);
  for (const IconBatchKey &key : keys) {
    std::vector<const ViewportOverlayIcon *> batch_icons;
    batch_icons.reserve(icons.size());
    for (const ViewportOverlayIcon *icon : icons) {
      const float icon_size =
          viewport_overlay_point_size_pixels(layer, icon->size_pixels);
      if (icon->kind == key.icon && icon_size == key.size_pixels) {
        batch_icons.push_back(icon);
      }
    }

    OverlayIconMesh mesh = OverlayPointMeshBuilder::build_icons(
        batch_icons, camera, key.size_pixels, depth_bias);
    if (!mesh.empty()) {
      plan.icon_batches.push_back({
          .mesh = std::move(mesh),
          .state = make_state(layer, ViewportOverlayGeometryKind::Icons,
                              blend_order++),
          .icon = key.icon,
          .size_pixels = key.size_pixels,
          .depth_bias = depth_bias,
      });
    }
  }
}

} // namespace quader
