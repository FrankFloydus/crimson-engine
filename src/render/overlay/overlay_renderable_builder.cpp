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

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <utility>

namespace quader {
namespace {

constexpr float kOverlayVisibilityEpsilon = 0.000001f;
constexpr float kOverlayOwnershipTolerance = 0.001f;
constexpr int kOverlaySourceWireSegmentSamples = 17;

bool overlay_layer_uses_depth_stamp_visibility(ViewportOverlayLayer layer) {
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
    std::uint16_t blend_order, ViewportOverlayDepthMode depth_mode,
    bool depth_stamp = false, bool face_color_pass = false) {
  const bool draw_on_top = depth_mode == ViewportOverlayDepthMode::DrawOnTop;
  OverlayRenderState state;
  state.depth_write = depth_stamp;
  state.color_write = !depth_stamp;
  state.depth_culling = depth_stamp || face_color_pass || !draw_on_top;
  state.depth_func = depth_stamp       ? OverlayDepthFunc::GreaterOrEqual
                     : face_color_pass ? OverlayDepthFunc::Equal
                     : draw_on_top     ? OverlayDepthFunc::Always
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
  if (kind == ViewportOverlayGeometryKind::Triangles) {
    state.polygon_offset_scale = -1.0f;
    state.polygon_offset_constant = -1.0f;
  }
  state.blend_order = blend_order;
  return state;
}

QVec3 point_on_line(QVec3 a, QVec3 b, float t) { return a + ((b - a) * t); }

bool line_uses_edit_wire_batch(const ViewportOverlayLine &line) {
  if (line.source != ViewportOverlaySource::ComponentSelection &&
      line.source != ViewportOverlaySource::ComponentHover) {
    return false;
  }
  return line.layer == ViewportOverlayLayer::SourceWire ||
         line.layer == ViewportOverlayLayer::SelectedFaceEdge ||
         line.layer == ViewportOverlayLayer::SelectedEdge ||
         line.layer == ViewportOverlayLayer::HoverFaceEdge ||
         line.layer == ViewportOverlayLayer::HoverEdge;
}

QVec3 camera_forward(const ViewportOverlayCamera &camera) {
  QVec3 forward = normalize(camera.forward);
  if (length_squared(forward) <= kOverlayVisibilityEpsilon) {
    forward = {0.0f, 0.0f, -1.0f};
  }
  return forward;
}

bool intersect_ray_triangle(QVec3 origin, QVec3 direction,
                            const ViewportOverlayTriangle &triangle,
                            float &out_distance) {
  const QVec3 edge1 = triangle.b - triangle.a;
  const QVec3 edge2 = triangle.c - triangle.a;
  const QVec3 p = cross(direction, edge2);
  const float determinant = dot(edge1, p);
  if (std::abs(determinant) <= kOverlayVisibilityEpsilon) {
    return false;
  }
  const float inv_determinant = 1.0f / determinant;
  const QVec3 t = origin - triangle.a;
  const float u = dot(t, p) * inv_determinant;
  if (u < 0.0f || u > 1.0f) {
    return false;
  }
  const QVec3 q = cross(t, edge1);
  const float v = dot(direction, q) * inv_determinant;
  if (v < 0.0f || u + v > 1.0f) {
    return false;
  }
  const float distance = dot(edge2, q) * inv_determinant;
  if (distance <= 0.0001f) {
    return false;
  }
  out_distance = distance;
  return true;
}

bool point_lies_on_triangle_plane(QVec3 point,
                                  const ViewportOverlayTriangle &triangle,
                                  float tolerance) {
  const QVec3 normal = cross(triangle.b - triangle.a, triangle.c - triangle.a);
  const float normal_length = length(normal);
  if (normal_length <= kOverlayVisibilityEpsilon) {
    return false;
  }
  const float signed_distance = dot(point - triangle.a, normal) / normal_length;
  return std::abs(signed_distance) <= tolerance;
}

bool points_near(QVec3 a, QVec3 b, float tolerance) {
  const QVec3 delta = a - b;
  return length_squared(delta) <= tolerance * tolerance;
}

bool triangle_contains_point(QVec3 point,
                             const ViewportOverlayTriangle &triangle,
                             float tolerance) {
  return points_near(point, triangle.a, tolerance) ||
         points_near(point, triangle.b, tolerance) ||
         points_near(point, triangle.c, tolerance);
}

bool triangle_contains_line_endpoints(const ViewportOverlayLine &line,
                                      const ViewportOverlayTriangle &triangle,
                                      float tolerance) {
  return triangle_contains_point(line.start, triangle, tolerance) &&
         triangle_contains_point(line.end, triangle, tolerance);
}

bool line_lies_on_triangle_plane(const ViewportOverlayLine &line,
                                 const ViewportOverlayTriangle &triangle,
                                 float tolerance) {
  return point_lies_on_triangle_plane(line.start, triangle, tolerance) &&
         point_lies_on_triangle_plane(line.end, triangle, tolerance);
}

} // namespace

/**
 * Filters source-wire overlay primitives against depth-stamp triangles for object-local visibility.
 */
class OverlayDepthStampVisibilityFilter {
public:
    explicit OverlayDepthStampVisibilityFilter(const ViewportOverlayFrame& frame)
    {
        QDR_PROFILE_FUNCTION();
        for (const ViewportOverlayTriangle& triangle : frame.triangles) {
          if (triangle.layer == ViewportOverlayLayer::SourceWireDepthStamp &&
              triangle.element.object_id >= 0) {
            StampSet &set = stamps_by_object_[triangle.element.object_id];
            set.triangles.push_back(&triangle);
            set.centroid += triangle.a + triangle.b + triangle.c;
            set.vertex_count += 3;
          }
        }
        for (auto &entry : stamps_by_object_) {
          StampSet &set = entry.second;
          if (set.vertex_count <= 0) {
            continue;
          }
          set.centroid = set.centroid / static_cast<float>(set.vertex_count);
          set.inside_out = inside_out_score(set) < -kOverlayVisibilityEpsilon;
        }
    }

    [[nodiscard]] bool empty() const { return stamps_by_object_.empty(); }

    [[nodiscard]] bool line_visible(const ViewportOverlayLine &line,
                                    const ViewportOverlayCamera &camera) const {
      const StampSet *stamps = stamps_for(line.element.object_id);
      if (stamps == nullptr) {
        return true;
      }
      int visible_samples = 0;
      visible_samples += point_visible(point_on_line(line.start, line.end, 0.2f),
                                       camera, *stamps, &line)
                             ? 1
                             : 0;
      visible_samples += point_visible(point_on_line(line.start, line.end, 0.5f),
                                       camera, *stamps, &line)
                             ? 1
                             : 0;
      visible_samples += point_visible(point_on_line(line.start, line.end, 0.8f),
                                       camera, *stamps, &line)
                             ? 1
                             : 0;
      return visible_samples >= 2;
    }

    void append_visible_line_segments(
        const ViewportOverlayLine &line, const ViewportOverlayCamera &camera,
        std::vector<ViewportOverlayLine> &out_lines) const {
      const StampSet *stamps = stamps_for(line.element.object_id);
      if (stamps == nullptr) {
        out_lines.push_back(line);
        return;
      }

      bool previous_visible = false;
      float previous_t = 0.0f;
      for (int sample = 0; sample < kOverlaySourceWireSegmentSamples;
           ++sample) {
        const float t = static_cast<float>(sample) /
                        static_cast<float>(kOverlaySourceWireSegmentSamples - 1);
        const bool visible =
            point_visible(point_on_line(line.start, line.end, t), camera,
                          *stamps, &line);
        if (sample > 0) {
          const float midpoint_t = (previous_t + t) * 0.5f;
          const bool midpoint_visible =
              point_visible(point_on_line(line.start, line.end, midpoint_t),
                            camera, *stamps, &line);
          if (previous_visible && visible && midpoint_visible) {
            ViewportOverlayLine segment = line;
            segment.start = point_on_line(line.start, line.end, previous_t);
            segment.end = point_on_line(line.start, line.end, t);
            out_lines.push_back(segment);
          }
        }
        previous_visible = visible;
        previous_t = t;
      }
    }

    [[nodiscard]] bool
    point_visible(const ViewportOverlayPoint &point,
                  const ViewportOverlayCamera &camera) const {
      const StampSet *stamps = stamps_for(point.element.object_id);
      return stamps == nullptr ||
             point_visible(point.position, camera, *stamps, nullptr);
    }

    [[nodiscard]] ViewportOverlayDepthMode
    point_depth_mode(const ViewportOverlayPoint &point,
                     const ViewportOverlayCamera &camera) const {
      const StampSet *stamps = stamps_for(point.element.object_id);
      if (stamps != nullptr && stamps->inside_out &&
          point_inside_stamp_volume(camera, *stamps)) {
        return ViewportOverlayDepthMode::DrawOnTop;
      }
      return viewport_overlay_depth_mode(point);
    }

private:
    /**
     * Caches depth-stamp triangles and aggregate orientation for one modeling object.
     */
    struct StampSet {
        std::vector<const ViewportOverlayTriangle*> triangles;
        QVec3 centroid{};
        int vertex_count = 0;
        bool inside_out = false;
    };

    [[nodiscard]] const StampSet *stamps_for(int object_id) const {
      const auto found = stamps_by_object_.find(object_id);
      return found == stamps_by_object_.end() ? nullptr : &found->second;
    }

    [[nodiscard]] static float inside_out_score(const StampSet &stamps) {
      float score = 0.0f;
      for (const ViewportOverlayTriangle *triangle : stamps.triangles) {
        const QVec3 normal =
            cross(triangle->b - triangle->a, triangle->c - triangle->a);
        const QVec3 triangle_centroid =
            (triangle->a + triangle->b + triangle->c) / 3.0f;
        score += dot(normal, triangle_centroid - stamps.centroid);
      }
      return score;
    }

    [[nodiscard]] static int distinct_intersection_count(
        QVec3 origin, QVec3 direction, const StampSet &stamps) {
      std::vector<float> distances;
      distances.reserve(stamps.triangles.size());
      for (const ViewportOverlayTriangle *triangle : stamps.triangles) {
        float distance = 0.0f;
        if (intersect_ray_triangle(origin, direction, *triangle, distance)) {
          distances.push_back(distance);
        }
      }
      if (distances.empty()) {
        return 0;
      }

      std::sort(distances.begin(), distances.end());
      int distinct_count = 0;
      float previous = -1.0f;
      for (const float distance : distances) {
        if (distinct_count == 0 ||
            std::abs(distance - previous) > kOverlayOwnershipTolerance) {
          ++distinct_count;
          previous = distance;
        }
      }
      return distinct_count;
    }

    [[nodiscard]] static bool
    point_inside_stamp_volume(const ViewportOverlayCamera &raw_camera,
                              const StampSet &stamps) {
      const ViewportOverlayCamera camera =
          OverlayCameraProjector::sanitized_camera(raw_camera);
      const std::array<QVec3, 3u> directions{{
          normalize(QVec3{0.331f, 0.521f, 0.787f}),
          normalize(QVec3{-0.613f, 0.247f, 0.751f}),
          normalize(QVec3{0.419f, -0.733f, 0.537f}),
      }};
      int samples = 0;
      int inside_votes = 0;
      for (const QVec3 direction : directions) {
        const int intersections =
            distinct_intersection_count(camera.eye, direction, stamps);
        if (intersections == 0) {
          continue;
        }
        ++samples;
        if ((intersections % 2) == 1) {
          ++inside_votes;
        }
      }
      return samples > 0 && inside_votes * 2 > samples;
    }

    [[nodiscard]] static bool
    point_visible(QVec3 point, const ViewportOverlayCamera &raw_camera,
                  const StampSet &stamps,
                  const ViewportOverlayLine *source_line) {
      if (stamps.inside_out) {
        return true;
      }
      const ViewportOverlayCamera camera =
          OverlayCameraProjector::sanitized_camera(raw_camera);
      const QVec3 forward = camera_forward(camera);
      QVec3 origin;
      QVec3 direction;
      float target_distance = 0.0f;
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
      }

      const float occlusion_tolerance =
          std::max(0.015f, target_distance * 0.0005f);
      const float max_distance = target_distance - occlusion_tolerance;
      if (max_distance <= 0.0001f) {
        return true;
      }
      for (const ViewportOverlayTriangle *triangle : stamps.triangles) {
        float hit_distance = 0.0f;
        if (intersect_ray_triangle(origin, direction, *triangle,
                                   hit_distance)) {
          if (source_line != nullptr &&
              source_line->element.face_id > 0 &&
              source_line->element.face_id == triangle->element.face_id) {
            continue;
          }
          if (source_line != nullptr &&
              triangle_contains_line_endpoints(*source_line, *triangle,
                                               kOverlayOwnershipTolerance)) {
            continue;
          }
          if (hit_distance < max_distance) {
            return false;
          }
        }
      }
      return true;
    }

    std::unordered_map<int, StampSet> stamps_by_object_;
};

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
    const ViewportOverlayCamera &camera, ViewportOverlayLayer layer) {
  RenderSignatureBuilder signature;
  signature.add_value(layer_signature(signatures, layer));
  if (layer_build_depends_on_camera(layer)) {
    const ViewportOverlayCamera sanitized =
        OverlayCameraProjector::sanitized_camera(camera);
    if (overlay_layer_can_build_points(layer) ||
        overlay_layer_can_build_icons(layer)) {
      signature.add_value(camera_signature(sanitized));
    } else if (overlay_layer_can_build_lines(layer)) {
      // Line endpoints stay in world space. Only material clip/depth state can
      // invalidate their GPU batches; camera pose and viewport size cannot.
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
    std::uint16_t blend_order, bool depth_stamp, bool face_color_pass) {
  return make_state_with_depth_mode(
      layer, kind, blend_order, viewport_overlay_depth_mode(layer, kind),
      depth_stamp, face_color_pass);
}

OverlayLayerBuildPlan OverlayRenderableBuilder::build_layer(
    const ViewportOverlayFrame &frame, const ViewportOverlayCamera &camera,
    ViewportOverlayLayer layer, const OverlayFrameSignatures *signatures,
    std::uint16_t blend_order_bias) {
  QDR_PROFILE_FUNCTION();
  OverlayLayerBuildPlan plan;
  const ViewportOverlayCamera sanitized_camera =
      OverlayCameraProjector::sanitized_camera(camera);
  if (layer == ViewportOverlayLayer::SourceWireDepthStamp) {
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
    OverlayFaceFillMesh face_fill_mesh = [&]() {
      QDR_PROFILE_SCOPE("Overlay build face fill mesh");
      return OverlayFaceFillBuilder::build_layer(frame, layer);
    }();
    if (!face_fill_mesh.empty()) {
      if (viewport_overlay_layer_uses_depth_only_face_fill(layer)) {
        plan.face_fill_batches.push_back({
            .mesh = std::move(face_fill_mesh),
            .state = make_state(layer, ViewportOverlayGeometryKind::Triangles,
                                blend_order++, true, false),
        });
      } else if (viewport_overlay_layer_uses_depth_stamped_face_fill(layer)) {
        plan.face_fill_batches.push_back({
            .mesh = face_fill_mesh,
            .state = make_state(layer, ViewportOverlayGeometryKind::Triangles,
                                blend_order++, true, false),
        });
        plan.face_fill_batches.push_back({
            .mesh = std::move(face_fill_mesh),
            .state = make_state(layer, ViewportOverlayGeometryKind::Triangles,
                                blend_order++, false, true),
        });
      } else {
        plan.face_fill_batches.push_back({
            .mesh = std::move(face_fill_mesh),
            .state = make_state(layer, ViewportOverlayGeometryKind::Triangles,
                                blend_order++),
        });
      }
    }
  }

  std::optional<OverlayDepthStampVisibilityFilter> visibility;
  if (overlay_layer_uses_depth_stamp_visibility(layer) &&
      frame_signatures.has_source_wire_depth_stamp &&
      (overlay_layer_can_build_lines(layer) ||
       overlay_layer_can_build_points(layer))) {
    visibility.emplace(frame);
  }

  if (overlay_layer_can_build_lines(layer)) {
    std::vector<ViewportOverlayLine> line_storage;
    const std::vector<const ViewportOverlayLine *> lines = [&]() {
      QDR_PROFILE_SCOPE("Overlay collect layer lines");
      return lines_for_layer(frame, layer, camera,
                             visibility ? &*visibility : nullptr,
                             line_storage);
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
      const float width_pixels =
          viewport_overlay_line_width_pixels(layer, 0.0f);
      OverlayEditWireMesh mesh = OverlayLineMeshBuilder::build(
          edit_wire_lines, camera, width_pixels, 0.0f);
      if (!mesh.empty()) {
        plan.edit_wire_batches.push_back({
            .mesh = std::move(mesh),
            .state = make_state_with_depth_mode(
                layer, ViewportOverlayGeometryKind::Lines, blend_order++,
                ViewportOverlayDepthMode::DepthTested),
            .width_pixels = width_pixels,
            .near_plane = sanitized_camera.near_plane,
            .far_plane = sanitized_camera.far_plane,
            .orthographic_projection =
                sanitized_camera.projection ==
                ViewportOverlayProjection::Orthographic,
        });
      }
    }
    const std::vector<LineBatchKey> line_keys =
        line_batch_keys(generic_lines, layer);
    for (const LineBatchKey &key : line_keys) {
      QDR_PROFILE_SCOPE("Overlay build line batch");
      std::vector<const ViewportOverlayLine *> batch_lines;
      batch_lines.reserve(generic_lines.size());
      for (const ViewportOverlayLine *line : generic_lines) {
        if (viewport_overlay_line_width_pixels(layer, line->width_pixels) !=
                key.width_pixels ||
            viewport_overlay_depth_mode(*line) != key.depth_mode) {
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
      plan.line_batches.push_back({
          .mesh = std::move(mesh),
          .state = make_state_with_depth_mode(
              layer, ViewportOverlayGeometryKind::Lines, blend_order++,
              key.depth_mode),
          .width_pixels = key.width_pixels,
          .depth_bias = depth_bias,
          .near_plane = sanitized_camera.near_plane,
      });
    }
  }

  if (overlay_layer_can_build_points(layer)) {
    const std::vector<const ViewportOverlayPoint *> points = [&]() {
      QDR_PROFILE_SCOPE("Overlay collect layer points");
      return points_for_layer(frame, layer, camera,
                              visibility ? &*visibility : nullptr);
    }();
    {
      QDR_PROFILE_SCOPE("Overlay build point outline pass");
      append_point_pass(plan, points, camera, layer, blend_order, true,
                        visibility ? &*visibility : nullptr);
    }
    {
      QDR_PROFILE_SCOPE("Overlay build point fill pass");
      append_point_pass(plan, points, camera, layer, blend_order, false,
                        visibility ? &*visibility : nullptr);
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
    const ViewportOverlayFrame &frame, ViewportOverlayLayer layer,
    const ViewportOverlayCamera &camera,
    const OverlayDepthStampVisibilityFilter *visibility,
    std::vector<ViewportOverlayLine> &line_storage) {
  (void)line_storage;
  std::vector<const ViewportOverlayLine *> lines;
  lines.reserve(frame.lines.size());
  for (const ViewportOverlayLine &line : frame.lines) {
    if (line.layer == layer &&
        (!overlay_layer_uses_depth_stamp_visibility(layer) ||
         visibility == nullptr || visibility->line_visible(line, camera))) {
      lines.push_back(&line);
    }
  }
  return lines;
}

std::vector<const ViewportOverlayPoint *>
OverlayRenderableBuilder::points_for_layer(
    const ViewportOverlayFrame &frame, ViewportOverlayLayer layer,
    const ViewportOverlayCamera &camera,
    const OverlayDepthStampVisibilityFilter *visibility) {
  std::vector<const ViewportOverlayPoint *> points;
  points.reserve(frame.points.size());
  for (const ViewportOverlayPoint &point : frame.points) {
    if (point.layer == layer &&
        (!overlay_layer_uses_depth_stamp_visibility(layer) ||
         visibility == nullptr || visibility->point_visible(point, camera))) {
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
    };
    const auto existing = std::find_if(
        keys.begin(), keys.end(), [&](const LineBatchKey &candidate) {
          return candidate.width_pixels == key.width_pixels &&
                 candidate.depth_mode == key.depth_mode;
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
    const OverlayDepthStampVisibilityFilter *visibility) {
  const auto depth_mode_for = [&](const ViewportOverlayPoint *point) {
    return visibility == nullptr ? viewport_overlay_depth_mode(*point)
                                 : visibility->point_depth_mode(*point,
                                                                camera);
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
