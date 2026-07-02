////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/overlay_renderable_builder.hpp"

#include "diagnostics/profile.hpp"
#include "render/render_signature_builder.hpp"
#include "render/overlay/overlay_camera_projector.hpp"
#include "render/overlay/overlay_face_fill_builder.hpp"
#include "render/overlay/overlay_line_mesh_builder.hpp"
#include "render/overlay/overlay_point_mesh_builder.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <utility>

namespace quader {
namespace {

constexpr float kOverlayVisibilityEpsilon = 0.000001f;

bool overlay_layer_uses_depth_stamp_visibility(ViewportOverlayLayer layer) {
  return layer == ViewportOverlayLayer::SourceWire ||
         layer == ViewportOverlayLayer::SelectedFaceEdge ||
         layer == ViewportOverlayLayer::SelectedEdge ||
         layer == ViewportOverlayLayer::HoverFaceEdge ||
         layer == ViewportOverlayLayer::HoverEdge ||
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

QVec3 point_on_line(QVec3 a, QVec3 b, float t) { return a + ((b - a) * t); }

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
      if (stamps == nullptr || stamps->inside_out) {
        return true;
      }
      return point_visible(point_on_line(line.start, line.end, 0.2f), camera,
                           *stamps) &&
             point_visible(point_on_line(line.start, line.end, 0.5f), camera,
                           *stamps) &&
             point_visible(point_on_line(line.start, line.end, 0.8f), camera,
                           *stamps);
    }

    [[nodiscard]] bool
    point_visible(const ViewportOverlayPoint &point,
                  const ViewportOverlayCamera &camera) const {
      const StampSet *stamps = stamps_for(point.element.object_id);
      return stamps == nullptr || stamps->inside_out ||
             point_visible(point.position, camera, *stamps);
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

    [[nodiscard]] static bool
    point_visible(QVec3 point, const ViewportOverlayCamera &raw_camera,
                  const StampSet &stamps) {
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

      const float tolerance = std::max(0.001f, target_distance * 0.00025f);
      const float max_distance = target_distance - tolerance;
      if (max_distance <= 0.0001f) {
        return true;
      }
      for (const ViewportOverlayTriangle *triangle : stamps.triangles) {
        float hit_distance = 0.0f;
        if (intersect_ray_triangle(origin, direction, *triangle,
                                   hit_distance) &&
            hit_distance < max_distance) {
          return false;
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
         overlay_layer_can_build_points(layer);
}

bool OverlayRenderableBuilder::layer_has_renderable_primitives(
    const OverlayFrameSignatures &signatures, ViewportOverlayLayer layer) {
  const std::size_t layer_index = overlay_layer_index(layer);
  return (overlay_layer_can_build_face_fill(layer) &&
          signatures.triangle_counts[layer_index] > 0u) ||
         (overlay_layer_can_build_lines(layer) &&
          signatures.line_counts[layer_index] > 0u) ||
         (overlay_layer_can_build_points(layer) &&
          signatures.point_counts[layer_index] > 0u);
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
    signature.add_value(
        camera_signature(OverlayCameraProjector::sanitized_camera(camera)));
  }
  return signature.value();
}

OverlayRenderState OverlayRenderableBuilder::make_state(
    ViewportOverlayLayer layer, ViewportOverlayGeometryKind kind,
    std::uint16_t blend_order, bool depth_stamp, bool face_color_pass) {
  const bool draw_on_top = viewport_overlay_depth_mode(layer, kind) ==
                           ViewportOverlayDepthMode::DrawOnTop;
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

OverlayLayerBuildPlan OverlayRenderableBuilder::build_layer(
    const ViewportOverlayFrame &frame, const ViewportOverlayCamera &camera,
    ViewportOverlayLayer layer, const OverlayFrameSignatures *signatures) {
  QDR_PROFILE_FUNCTION();
  OverlayLayerBuildPlan plan;
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
      static_cast<std::uint16_t>(viewport_overlay_draw_order(layer) * 64);
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
    const std::vector<const ViewportOverlayLine *> lines = [&]() {
      QDR_PROFILE_SCOPE("Overlay collect layer lines");
      return lines_for_layer(frame, layer, camera,
                             visibility ? &*visibility : nullptr);
    }();
    const std::vector<LineBatchKey> line_keys = line_batch_keys(lines, layer);
    for (const LineBatchKey &key : line_keys) {
      QDR_PROFILE_SCOPE("Overlay build line batch");
      std::vector<const ViewportOverlayLine *> batch_lines;
      batch_lines.reserve(lines.size());
      for (const ViewportOverlayLine *line : lines) {
        if (viewport_overlay_line_width_pixels(layer, line->width_pixels) !=
            key.width_pixels) {
          continue;
        }
        batch_lines.push_back(line);
      }
      const float depth_bias = viewport_overlay_depth_bias(
          layer, ViewportOverlayGeometryKind::Lines);
      OverlayLineMesh mesh = OverlayLineMeshBuilder::build(
          batch_lines, camera, key.width_pixels, depth_bias);
      if (mesh.empty()) {
        continue;
      }
      plan.line_batches.push_back({
          .mesh = std::move(mesh),
          .state = make_state(layer, ViewportOverlayGeometryKind::Lines,
                              blend_order++),
          .width_pixels = key.width_pixels,
          .depth_bias = depth_bias,
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
      append_point_pass(plan, points, camera, layer, blend_order, true);
    }
    {
      QDR_PROFILE_SCOPE("Overlay build point fill pass");
      append_point_pass(plan, points, camera, layer, blend_order, false);
    }
  }
  return plan;
}

std::vector<const ViewportOverlayLine *>
OverlayRenderableBuilder::lines_for_layer(
    const ViewportOverlayFrame &frame, ViewportOverlayLayer layer,
    const ViewportOverlayCamera &camera,
    const OverlayDepthStampVisibilityFilter *visibility) {
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

std::vector<OverlayRenderableBuilder::LineBatchKey>
OverlayRenderableBuilder::line_batch_keys(
    const std::vector<const ViewportOverlayLine *> &lines,
    ViewportOverlayLayer layer) {
  std::vector<LineBatchKey> keys;
  for (const ViewportOverlayLine *line : lines) {
    const LineBatchKey key{
        .width_pixels =
            viewport_overlay_line_width_pixels(layer, line->width_pixels),
    };
    const auto existing = std::find_if(
        keys.begin(), keys.end(), [&](const LineBatchKey &candidate) {
          return candidate.width_pixels == key.width_pixels;
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
    std::uint16_t &blend_order, bool outline) {
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

  std::vector<float> point_sizes;
  for (const ViewportOverlayPoint *point : points) {
    const float base_size =
        viewport_overlay_point_size_pixels(layer, point->size_pixels);
    const bool outline_uses_native_color =
        layer == ViewportOverlayLayer::SnapTargetVertex;
    const float size_pixels = outline && !outline_uses_native_color
                                  ? base_size + outline_growth_for(point)
                                  : base_size;
    if (std::find(point_sizes.begin(), point_sizes.end(), size_pixels) ==
        point_sizes.end()) {
      point_sizes.push_back(size_pixels);
    }
  }

  const float depth_bias =
      viewport_overlay_depth_bias(layer, ViewportOverlayGeometryKind::Points);
  for (const float size_pixels : point_sizes) {
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
      if (point_size == size_pixels) {
        batch_points.push_back(point);
      }
    }

    const bool override_color =
        outline && layer != ViewportOverlayLayer::SnapTargetVertex;
    OverlayPointMesh mesh = OverlayPointMeshBuilder::build(
        batch_points, camera, size_pixels, depth_bias,
        kOverlayVertexOutlineColor, override_color);
    if (!mesh.empty()) {
      plan.point_batches.push_back({
          .mesh = std::move(mesh),
          .state = make_state(layer, ViewportOverlayGeometryKind::Points,
                              blend_order++),
          .size_pixels = size_pixels,
          .inner_radius =
              outline && viewport_overlay_point_is_outline_only(layer)
                  ? viewport_overlay_point_inner_radius(layer)
                  : 0.0f,
          .depth_bias = depth_bias,
      });
    }
  }
}

} // namespace quader
