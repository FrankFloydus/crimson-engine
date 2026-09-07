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
#include <optional>
#include <unordered_map>
#include <utility>

namespace quader {
namespace {

constexpr float kOverlayVisibilityEpsilon = 0.000001f;
constexpr float kOverlayVisibilityHitTolerance = 0.001f;

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

QVec3 overlay_camera_forward(const ViewportOverlayCamera &camera) {
  QVec3 forward = normalize(camera.forward);
  if (length_squared(forward) <= kOverlayVisibilityEpsilon) {
    forward = {0.0f, 0.0f, -1.0f};
  }
  return forward;
}

quader_geometry::QVec3f neutral_point(QVec3 value) {
  return {value.x, value.y, value.z};
}

} // namespace

/**
 * Resolves editable point visibility against source-wire depth metadata.
 *
 * Each object uses the shared neutral triangle BVH. Camera motion therefore
 * costs O(points * log(triangles)) queries instead of the former quadratic
 * point-by-triangle scan.
 */
class OverlayDepthStampPointVisibilityFilter final {
public:
  OverlayDepthStampPointVisibilityFilter(const ViewportOverlayFrame &frame,
                                         const ViewportOverlayCamera &camera) {
    QDR_PROFILE_FUNCTION();
    for (const ViewportOverlayTriangle &triangle : frame.triangles) {
      if (triangle.layer != ViewportOverlayLayer::SourceWireDepthStamp ||
          triangle.element.object_id < 0) {
        continue;
      }
      StampSet &set = stamps_by_object_[triangle.element.object_id];
      set.source_triangles.push_back(&triangle);
      set.triangles.push_back({neutral_point(triangle.a),
                               neutral_point(triangle.b),
                               neutral_point(triangle.c)});
      set.centroid += triangle.a + triangle.b + triangle.c;
      set.vertex_count += 3u;
    }

    for (auto &[object_id, set] : stamps_by_object_) {
      (void)object_id;
      if (set.vertex_count == 0u) {
        continue;
      }
      set.centroid =
          set.centroid / static_cast<float>(set.vertex_count);
      set.inside_out = inside_out_score(set) < -kOverlayVisibilityEpsilon;
      if (!set.bvh.build(set.triangles)) {
        continue;
      }
      set.traversal_scratch.reserve(set.bvh.required_traversal_capacity());
      set.camera_inside =
          set.inside_out && point_inside_stamp_volume(camera, set);
    }
  }

  [[nodiscard]] bool empty() const { return stamps_by_object_.empty(); }

  [[nodiscard]] bool point_visible(
      const ViewportOverlayPoint &point,
      const ViewportOverlayCamera &camera) const {
    const StampSet *stamps = stamps_for(point.element.object_id);
    return stamps == nullptr || stamps->inside_out ||
           point_visible(point.position, camera, *stamps);
  }

  [[nodiscard]] ViewportOverlayDepthMode
  point_depth_mode(const ViewportOverlayPoint &point) const {
    const StampSet *stamps = stamps_for(point.element.object_id);
    if (stamps != nullptr && stamps->inside_out && stamps->camera_inside) {
      return ViewportOverlayDepthMode::DrawOnTop;
    }
    return viewport_overlay_depth_mode(point);
  }

private:
  /** Owns one object's source triangles, acceleration data, and orientation. */
  struct StampSet final {
    std::vector<const ViewportOverlayTriangle *> source_triangles;
    std::vector<quader_geometry::QTriangle3<float>> triangles;
    quader_geometry::QTriangleBvh<float> bvh;
    mutable quader_geometry::QBvhTraversalScratch traversal_scratch;
    QVec3 centroid;
    std::size_t vertex_count = 0u;
    bool inside_out = false;
    bool camera_inside = false;
  };

  [[nodiscard]] const StampSet *stamps_for(int object_id) const {
    const auto found = stamps_by_object_.find(object_id);
    return found == stamps_by_object_.end() ? nullptr : &found->second;
  }

  [[nodiscard]] static float inside_out_score(const StampSet &stamps) {
    float score = 0.0f;
    for (const ViewportOverlayTriangle *triangle : stamps.source_triangles) {
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
    if (stamps.bvh.empty()) {
      return 0;
    }
    quader_geometry::QTriangleBvhQueryOptions<float> options;
    options.min_distance = 0.0001f;
    options.intersection_epsilon = kOverlayVisibilityEpsilon;
    options.tie_tolerance = kOverlayVisibilityHitTolerance;
    int count = 0;
    for (std::size_t iteration = 0u;
         iteration < stamps.bvh.primitive_count(); ++iteration) {
      const quader_geometry::QTriangleBvhHit<float> hit =
          stamps.bvh.nearest_hit(
              {neutral_point(origin), neutral_point(direction)}, options,
              stamps.traversal_scratch);
      if (!hit.hit) {
        break;
      }
      ++count;
      options.min_distance =
          hit.triangle_hit.distance + kOverlayVisibilityHitTolerance;
    }
    return count;
  }

  [[nodiscard]] static bool point_inside_stamp_volume(
      const ViewportOverlayCamera &raw_camera, const StampSet &stamps) {
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

  [[nodiscard]] static bool point_visible(
      QVec3 point, const ViewportOverlayCamera &raw_camera,
      const StampSet &stamps) {
    if (stamps.bvh.empty()) {
      return true;
    }
    const ViewportOverlayCamera camera =
        OverlayCameraProjector::sanitized_camera(raw_camera);
    const QVec3 forward = overlay_camera_forward(camera);
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
    quader_geometry::QTriangleBvhQueryOptions<float> options;
    options.min_distance = 0.0001f;
    options.max_distance = max_distance;
    options.intersection_epsilon = kOverlayVisibilityEpsilon;
    return !stamps.bvh.any_hit(
        {neutral_point(origin), neutral_point(direction)}, options,
        stamps.traversal_scratch);
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
    const ViewportOverlayCamera &camera, ViewportOverlayLayer layer) {
  RenderSignatureBuilder signature;
  signature.add_value(layer_signature(signatures, layer));
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
    // Compatibility metadata only. Scene and moving-preview overlay-depth
    // companions own visibility; this triangle stream neither renders nor
    // drives camera-dependent CPU filtering.
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
    std::optional<OverlayDepthStampPointVisibilityFilter> visibility;
    if (overlay_layer_uses_depth_stamp_point_visibility(layer) &&
        frame_signatures.has_source_wire_depth_stamp) {
      visibility.emplace(frame, sanitized_camera);
    }
    const std::vector<const ViewportOverlayPoint *> points = [&]() {
      QDR_PROFILE_SCOPE("Overlay collect layer points");
      return points_for_layer(frame, layer,
                              visibility ? &*visibility : nullptr,
                              sanitized_camera);
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
    const OverlayDepthStampPointVisibilityFilter *visibility,
    const ViewportOverlayCamera &camera) {
  std::vector<const ViewportOverlayPoint *> points;
  points.reserve(frame.points.size());
  for (const ViewportOverlayPoint &point : frame.points) {
    if (point.layer == layer &&
        (visibility == nullptr || visibility->point_visible(point, camera))) {
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
    const OverlayDepthStampPointVisibilityFilter *visibility) {
  const auto depth_mode_for =
      [visibility](const ViewportOverlayPoint *point) {
        return visibility == nullptr
                   ? viewport_overlay_depth_mode(*point)
                   : visibility->point_depth_mode(*point);
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
