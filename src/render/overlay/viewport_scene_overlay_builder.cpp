////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/viewport_scene_overlay_builder.hpp"

#include "render/overlay/curve_overlay_geometry.hpp"
#include "render/overlay/overlay_camera_projector.hpp"
#include "render/overlay/viewport_circular_handle.hpp"

#include "diagnostics/profile.hpp"
#include "core/color_space.hpp"
#include "math/glm_bridge.hpp"
#include "render/viewport_overlay_policy.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <optional>
#include <unordered_map>
#include <utility>

namespace quader {
namespace {

/**
 * Represents a Wire Edge Point value used by scene overlay extraction.
 */
struct WireEdgePoint {
    std::int64_t x = 0;
    std::int64_t y = 0;
    std::int64_t z = 0;
};

/**
 * Stores the Wire Edge Key data contract used by scene overlay extraction.
 */
struct WireEdgeKey {
    WireEdgePoint a;
    WireEdgePoint b;

    bool operator==(const WireEdgeKey& other) const {
        return a.x == other.a.x && a.y == other.a.y && a.z == other.a.z &&
                b.x == other.b.x && b.y == other.b.y && b.z == other.b.z;
    }
};

/**
 * Represents a Wire Edge Key Hash value used by scene overlay extraction.
 */
struct WireEdgeKeyHash {
    std::size_t operator()(const WireEdgeKey& key) const {
        auto mix = [](std::size_t hash, std::int64_t value) {
            hash ^= static_cast<std::size_t>(value) + 0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u);
            return hash;
        };
        std::size_t hash = 1469598103934665603ull;
        hash = mix(hash, key.a.x);
        hash = mix(hash, key.a.y);
        hash = mix(hash, key.a.z);
        hash = mix(hash, key.b.x);
        hash = mix(hash, key.b.y);
        return mix(hash, key.b.z);
    }
};

/**
 * Stores the Wire Edge Record data contract used by scene overlay extraction.
 */
struct WireEdgeRecord {
    QVec3 start = {};
    QVec3 end = {};
    QVec3 first_normal = {};
    std::uint32_t count = 0;
    bool coplanar_with_first = true;
};

WireEdgePoint quantized_wire_point(QVec3 value) {
  constexpr double kScale = 1000000.0;
  return {
      static_cast<std::int64_t>(
          std::llround(static_cast<double>(value.x) * kScale)),
      static_cast<std::int64_t>(
          std::llround(static_cast<double>(value.y) * kScale)),
      static_cast<std::int64_t>(
          std::llround(static_cast<double>(value.z) * kScale)),
  };
}

bool wire_point_less(const WireEdgePoint &left, const WireEdgePoint &right) {
  if (left.x != right.x) {
    return left.x < right.x;
  }
  if (left.y != right.y) {
    return left.y < right.y;
  }
  return left.z < right.z;
}

WireEdgeKey wire_edge_key(QVec3 a, QVec3 b) {
  WireEdgePoint qa = quantized_wire_point(a);
  WireEdgePoint qb = quantized_wire_point(b);
  if (wire_point_less(qb, qa)) {
    std::swap(qa, qb);
  }
  return {.a = qa, .b = qb};
}

void append_mesh_selection_wire(
    ViewportOverlayFrame &frame, const RenderOverlayMeshSnapshot &mesh,
    std::uint32_t color = kOverlaySelectionWireColor,
    ViewportOverlayLayer layer = ViewportOverlayLayer::SourceWire,
    ViewportOverlayStyleRole style_role =
        ViewportOverlayStyleRole::SelectedSourceWire,
    ViewportOverlaySource source = ViewportOverlaySource::Selection) {
  QDR_PROFILE_FUNCTION();
  if (mesh.has_wire_geometry()) {
    for (const MeshWireSegment &segment : mesh.wire_segments) {
      frame.lines.push_back(ViewportOverlayLine{
          .layer = layer,
          .start = transform_point(mesh.world_transform, segment.start),
          .end = transform_point(mesh.world_transform, segment.end),
          .color = color,
          .width_pixels = 0.0f,
          .style_role = style_role,
          .source = source,
      });
    }
    return;
  }
  if (!mesh.has_triangle_geometry()) {
    return;
  }

  std::unordered_map<WireEdgeKey, WireEdgeRecord, WireEdgeKeyHash> edges;
  edges.reserve(mesh.indices.size());

  auto append_edge = [&](std::uint32_t a, std::uint32_t b,
                         QVec3 triangle_normal) {
    if (a >= mesh.vertices.size() || b >= mesh.vertices.size() || a == b) {
      return;
    }
    const QVec3 start =
        transform_point(mesh.world_transform, mesh.vertices[a].position);
    const QVec3 end =
        transform_point(mesh.world_transform, mesh.vertices[b].position);
    const WireEdgeKey key = wire_edge_key(start, end);
    WireEdgeRecord &record = edges[key];
    if (record.count == 0u) {
      record.start = start;
      record.end = end;
      record.first_normal = triangle_normal;
    } else if (length(record.first_normal) > 0.000001f &&
               length(triangle_normal) > 0.000001f) {
      record.coplanar_with_first =
          record.coplanar_with_first &&
          std::abs(dot(normalize(record.first_normal),
                       normalize(triangle_normal))) > 0.999f;
    }
    ++record.count;
  };

  for (std::size_t index = 0; index + 2u < mesh.indices.size(); index += 3u) {
    const std::uint32_t a = mesh.indices[index];
    const std::uint32_t b = mesh.indices[index + 1u];
    const std::uint32_t c = mesh.indices[index + 2u];
    if (a >= mesh.vertices.size() || b >= mesh.vertices.size() ||
        c >= mesh.vertices.size()) {
      continue;
    }
    const QVec3 world_a =
        transform_point(mesh.world_transform, mesh.vertices[a].position);
    const QVec3 world_b =
        transform_point(mesh.world_transform, mesh.vertices[b].position);
    const QVec3 world_c =
        transform_point(mesh.world_transform, mesh.vertices[c].position);
    QVec3 normal = cross(world_b - world_a, world_c - world_a);
    if (length(normal) > 0.000001f) {
      normal = normalize(normal);
    }
    append_edge(a, b, normal);
    append_edge(b, c, normal);
    append_edge(c, a, normal);
  }

  for (const auto &[key, edge] : edges) {
    (void)key;
    if (edge.count == 2u && edge.coplanar_with_first) {
      continue;
    }
    frame.lines.push_back(ViewportOverlayLine{
        .layer = layer,
        .start = edge.start,
        .end = edge.end,
        .color = color,
        .width_pixels = 0.0f,
        .style_role = style_role,
        .source = source,
    });
  }
}

void append_semantic_wireframe(
    ViewportOverlayFrame &frame, const RenderOverlayMeshSnapshot &mesh,
    ViewportOverlayLayer layer, std::uint32_t color, float width_pixels,
    ViewportOverlayStyleRole style_role) {
  for (const MeshWireSegment &segment : mesh.wire_segments) {
    frame.lines.push_back(ViewportOverlayLine{
        .layer = layer,
        .start = transform_point(mesh.world_transform, segment.start),
        .end = transform_point(mesh.world_transform, segment.end),
        .color = color,
        .width_pixels = width_pixels,
        .style_role = style_role,
        .source = ViewportOverlaySource::SceneWireframe,
    });
  }
}

void append_hierarchy_selection_wires(ViewportOverlayFrame &frame,
                                      const RenderOverlaySelectionSnapshot
                                          &snapshot, bool active_feedback) {
  for (const RenderOverlayMeshSnapshot &mesh :
       snapshot.hierarchy_selection_wires) {
    append_mesh_selection_wire(
        frame, mesh, kOverlaySelectionWireColor,
        ViewportOverlayLayer::SelectedEdge,
        active_feedback && mesh.active_selected
            ? ViewportOverlayStyleRole::ActiveSelectionWire
            : ViewportOverlayStyleRole::SelectionWire,
        ViewportOverlaySource::HierarchySelection);
  }
}

void append_bounds_cage(ViewportOverlayFrame &frame,
                        const RenderOverlayBoundsSnapshot &bounds, bool active_feedback) {
  const std::array<QVec3, 8> corners{{
      {bounds.min.x, bounds.min.y, bounds.min.z},
      {bounds.max.x, bounds.min.y, bounds.min.z},
      {bounds.max.x, bounds.max.y, bounds.min.z},
      {bounds.min.x, bounds.max.y, bounds.min.z},
      {bounds.min.x, bounds.min.y, bounds.max.z},
      {bounds.max.x, bounds.min.y, bounds.max.z},
      {bounds.max.x, bounds.max.y, bounds.max.z},
      {bounds.min.x, bounds.max.y, bounds.max.z},
  }};
  constexpr std::array<std::array<int, 2>, 12> kEdges{{
      {{0, 1}},
      {{1, 2}},
      {{2, 3}},
      {{3, 0}},
      {{4, 5}},
      {{5, 6}},
      {{6, 7}},
      {{7, 4}},
      {{0, 4}},
      {{1, 5}},
      {{2, 6}},
      {{3, 7}},
  }};
  for (const auto edge : kEdges) {
    frame.lines.push_back(ViewportOverlayLine{
        .layer = ViewportOverlayLayer::GroupBounds,
        .start = corners[static_cast<std::size_t>(edge[0])],
        .end = corners[static_cast<std::size_t>(edge[1])],
        .color = kOverlayGroupBoundsColor,
        .width_pixels = kOverlayGroupBoundsLineWidthPixels,
        .style_role = active_feedback && bounds.active_selected
            ? ViewportOverlayStyleRole::ActiveGroupBounds : ViewportOverlayStyleRole::GroupBounds,
        .source = ViewportOverlaySource::GroupBounds,
    });
  }
}

bool selection_mode_uses_selected_mesh_bounds(RenderOverlaySelectionMode mode) {
  return mode == RenderOverlaySelectionMode::Object ||
         mode == RenderOverlaySelectionMode::Group;
}

std::optional<RenderOverlayBoundsSnapshot> combined_selected_mesh_bounds(
    const RenderOverlaySelectionSnapshot &snapshot,
    RenderOverlaySelectionMode selection_mode) {
  if (!selection_mode_uses_selected_mesh_bounds(selection_mode) ||
      snapshot.selected_mesh_bounds.empty()) {
    return std::nullopt;
  }

  RenderOverlayBoundsSnapshot combined = snapshot.selected_mesh_bounds.front();
  combined.active_selected = false;
  for (std::size_t index = 1u; index < snapshot.selected_mesh_bounds.size();
       ++index) {
    const RenderOverlayBoundsSnapshot &bounds =
        snapshot.selected_mesh_bounds[index];
    combined.min.x = std::min(combined.min.x, bounds.min.x);
    combined.min.y = std::min(combined.min.y, bounds.min.y);
    combined.min.z = std::min(combined.min.z, bounds.min.z);
    combined.max.x = std::max(combined.max.x, bounds.max.x);
    combined.max.y = std::max(combined.max.y, bounds.max.y);
    combined.max.z = std::max(combined.max.z, bounds.max.z);
  }
  return combined;
}

bool same_bounds(const RenderOverlayBoundsSnapshot &lhs,
                 const RenderOverlayBoundsSnapshot &rhs) {
  return lhs.min.x == rhs.min.x && lhs.min.y == rhs.min.y &&
         lhs.min.z == rhs.min.z && lhs.max.x == rhs.max.x &&
         lhs.max.y == rhs.max.y && lhs.max.z == rhs.max.z;
}

void append_group_selection_cages(
    ViewportOverlayFrame &frame,
    const RenderOverlaySelectionSnapshot &snapshot,
    RenderOverlaySelectionMode selection_mode) {
  const std::optional<RenderOverlayBoundsSnapshot> selected_mesh_bounds =
      combined_selected_mesh_bounds(snapshot, selection_mode);
  bool selected_mesh_bounds_merged = false;
  for (const RenderOverlayBoundsSnapshot &group_bounds :
       snapshot.group_bounds) {
    if (!selected_mesh_bounds_merged && selected_mesh_bounds.has_value() &&
        same_bounds(group_bounds, *selected_mesh_bounds)) {
      selected_mesh_bounds_merged = true;
    }
    append_bounds_cage(frame, group_bounds, selection_mode_uses_selected_mesh_bounds(selection_mode));
  }
  if (selected_mesh_bounds.has_value() && !selected_mesh_bounds_merged) {
    append_bounds_cage(frame, *selected_mesh_bounds, false);
  }
}

void append_selected_light_markers(ViewportOverlayFrame &frame,
                                   const RenderOverlaySelectionSnapshot
                                       &snapshot, bool active_feedback) {
  constexpr float kLightMarkerRadius = 0.28f;
  constexpr float kDirectionalLightLength = 0.85f;
  const std::array<QVec3, 3> axes{{
      {kLightMarkerRadius, 0.0f, 0.0f},
      {0.0f, kLightMarkerRadius, 0.0f},
      {0.0f, 0.0f, kLightMarkerRadius},
  }};

  for (const RenderOverlayLightMarkerSnapshot &light :
       snapshot.selected_lights) {
    const QMat4 world = light.world_transform;
    const QVec3 position = transform_point(world, {});
    frame.points.push_back(ViewportOverlayPoint{
        .layer = ViewportOverlayLayer::SelectedVertex,
        .position = position,
        .color = kOverlaySelectedVertexColor,
        .size_pixels = kOverlayLightMarkerPointSizePixels,
        .style_role = active_feedback && light.active_selected
            ? ViewportOverlayStyleRole::ActiveSelectedVertex : ViewportOverlayStyleRole::SelectedVertex,
        .source = ViewportOverlaySource::Selection,
        .outline_growth_pixels = kOverlayLightMarkerOutlineGrowthPixels,
    });

    if (light.type == LightType::Directional) {
      QVec3 direction = normalize(transform_vector(world, {0.0f, -1.0f, 0.0f}));
      if (length(direction) <= 0.000001f) {
        direction = {0.0f, -1.0f, 0.0f};
      }
      const QVec3 tip = position + direction * kDirectionalLightLength;
      frame.lines.push_back(ViewportOverlayLine{
          .layer = ViewportOverlayLayer::SelectedEdge,
          .start = position,
          .end = tip,
          .color = kOverlaySelectionWireColor,
          .width_pixels = 0.0f,
          .style_role = active_feedback && light.active_selected
              ? ViewportOverlayStyleRole::ActiveSelectionWire : ViewportOverlayStyleRole::SelectionWire,
          .source = ViewportOverlaySource::Selection,
      });
      const QVec3 right = normalize(cross(direction, {0.0f, 1.0f, 0.0f}));
      const QVec3 tick = length(right) <= 0.000001f
                             ? QVec3{kLightMarkerRadius, 0.0f, 0.0f}
                             : right * kLightMarkerRadius;
      frame.lines.push_back(ViewportOverlayLine{
          .layer = ViewportOverlayLayer::SelectedEdge,
          .start = tip - tick,
          .end = tip + tick,
          .color = kOverlaySelectionWireColor,
          .width_pixels = 0.0f,
          .style_role = active_feedback && light.active_selected
              ? ViewportOverlayStyleRole::ActiveSelectionWire : ViewportOverlayStyleRole::SelectionWire,
          .source = ViewportOverlaySource::Selection,
      });
      continue;
    }

    for (const QVec3 axis : axes) {
      frame.lines.push_back(ViewportOverlayLine{
          .layer = ViewportOverlayLayer::SelectedEdge,
          .start = position - axis,
          .end = position + axis,
          .color = kOverlaySelectionWireColor,
          .width_pixels = 0.0f,
          .style_role = active_feedback && light.active_selected
              ? ViewportOverlayStyleRole::ActiveSelectionWire : ViewportOverlayStyleRole::SelectionWire,
          .source = ViewportOverlaySource::Selection,
      });
    }
  }
}

void append_light_radius_circle(ViewportOverlayFrame &frame,
                                const RenderOverlayLightMarkerSnapshot &light,
                                QVec3 right, QVec3 up,
                                std::uint32_t color) {
  constexpr std::size_t kCircleSegments = 120u;
  const QVec3 center = transform_point(light.world_transform, {});
  QVec3 previous = center + right * light.radius;
  for (std::size_t segment = 1u; segment <= kCircleSegments; ++segment) {
    const float angle = 2.0f * std::numbers::pi_v<float> * static_cast<float>(segment) /
                        static_cast<float>(kCircleSegments);
    const QVec3 next = segment == kCircleSegments
                           ? center + right * light.radius
                           : center + (right * std::cos(angle) +
                                       up * std::sin(angle)) * light.radius;
    frame.lines.push_back({
        .layer = ViewportOverlayLayer::ToolPlaneLine,
        .start = previous,
        .end = next,
        .color = color,
        .width_pixels = kOverlayPointLightRadiusLineWidthPixels,
        .style_role = ViewportOverlayStyleRole::ExplicitColor,
        .source = ViewportOverlaySource::Gizmo,
        .element = {.object_id = static_cast<int>(light.entity)},
    });
    previous = next;
  }
}

void append_selected_light_radius_circles(
    ViewportOverlayFrame &frame,
    const RenderOverlaySelectionSnapshot &snapshot) {
  for (const RenderOverlayLightMarkerSnapshot &light :
       snapshot.point_light_gizmos) {
    if (!light.selected) continue;
    append_light_radius_circle(frame, light, {1.0f, 0.0f, 0.0f},
                               {0.0f, 1.0f, 0.0f},
                               kOverlayPointLightRadiusWireColor);
    append_light_radius_circle(frame, light, {1.0f, 0.0f, 0.0f},
                               {0.0f, 0.0f, 1.0f},
                               kOverlayPointLightRadiusWireColor);
    append_light_radius_circle(frame, light, {0.0f, 1.0f, 0.0f},
                               {0.0f, 0.0f, 1.0f},
                               kOverlayPointLightRadiusWireColor);
  }
}

void append_point_light_gizmos(ViewportOverlayFrame &frame,
                               const RenderOverlaySelectionSnapshot
                                   &snapshot, bool active_feedback, bool selection_visible) {
  for (const RenderOverlayLightMarkerSnapshot &light :
       snapshot.point_light_gizmos) {
    const QVec3 position = transform_point(light.world_transform, {});
    frame.icons.push_back(ViewportOverlayIcon{
        .layer = ViewportOverlayLayer::PointLightGizmo,
        .kind = ViewportOverlayIconKind::PointLight,
        .position = position,
        .color = rgba8(255, 255, 255, 255),
        .size_pixels = kOverlayPointLightGizmoSizePixels,
        .style_role = selection_visible && (light.selected || light.hierarchy_selected)
            ? (active_feedback && light.active_selected ? ViewportOverlayStyleRole::ActiveSelectionWire
                                                       : ViewportOverlayStyleRole::SelectionWire)
            : ViewportOverlayStyleRole::ExplicitColor,
        .source = ViewportOverlaySource::Gizmo,
        .element = {.object_id = static_cast<int>(light.entity)},
    });
  }
}

void append_scene_wireframe_overlay(ViewportOverlayFrame &frame,
                                    const RenderOverlaySceneWireframeSnapshot
                                        &snapshot) {
  QDR_PROFILE_FUNCTION();
  for (const RenderOverlayMeshSnapshot &mesh : snapshot.meshes) {
    QDR_PROFILE_SCOPE("Viewport scene wireframe entity");
    const ViewportOverlayLayer layer =
        mesh.selected ? ViewportOverlayLayer::SelectedEdge
                      : ViewportOverlayLayer::WireframeXray;
    constexpr std::uint8_t kWireframeAlpha = static_cast<std::uint8_t>(
        (kOverlayWireframeXrayColor >> 24u) & 0xffu);
    std::uint32_t color =
        mesh.selected ? (mesh.active_selected ? snapshot.style.active_selection_wire_color
                                            : snapshot.style.selection_wire_color)
                      : rgba8(normalized_to_byte(mesh.wireframe_tint.x),
                              normalized_to_byte(mesh.wireframe_tint.y),
                              normalized_to_byte(mesh.wireframe_tint.z),
                              kWireframeAlpha);
    if (is_instance_edit_context(snapshot.instance_edit_scope,
                                  mesh.occurrence, mesh.selected)) {
      const ViewportSettingColor context_color =
          snapshot.style.instance_edit_style.resolve_context_color(
              {mesh.wireframe_tint.x, mesh.wireframe_tint.y,
               mesh.wireframe_tint.z,
               static_cast<float>(kWireframeAlpha) / 255.0f},
              true);
      color = rgba8(normalized_to_byte(context_color.r),
                     normalized_to_byte(context_color.g),
                     normalized_to_byte(context_color.b),
                     normalized_to_byte(context_color.a));
    }
    const float width_pixels =
        mesh.selected ? (mesh.active_selected
                             ? snapshot.style.active_selection_line_width_pixels
                             : snapshot.style.selection_edge_line_width_pixels)
                      : 0.0f;
    const ViewportOverlayStyleRole style_role =
        mesh.selected ? (mesh.active_selected ? ViewportOverlayStyleRole::ActiveSelectionWire
                                            : ViewportOverlayStyleRole::SelectionWire)
                      : ViewportOverlayStyleRole::WireframeBase;
    append_semantic_wireframe(frame, mesh, layer, color, width_pixels,
                              style_role);
  }
}

bool wireframe_uses_wire_only_object_selection(RenderOverlaySelectionMode mode) {
  return mode == RenderOverlaySelectionMode::Object ||
         mode == RenderOverlaySelectionMode::Group;
}

bool wireframe_suppresses_triangle_layer(ViewportOverlayLayer layer) {
  return layer == ViewportOverlayLayer::SelectedVisibleFace ||
         layer == ViewportOverlayLayer::SelectedXrayFace ||
         layer == ViewportOverlayLayer::SelectedFaceMask ||
         layer == ViewportOverlayLayer::HoverFace;
}

bool persistent_object_selection_fill_triangle(
    const ViewportOverlayTriangle &triangle) {
  if (triangle.source != ViewportOverlaySource::Selection ||
      triangle.element.object_id < 0) {
    return false;
  }
  return triangle.layer == ViewportOverlayLayer::SelectedVisibleFace ||
         triangle.layer == ViewportOverlayLayer::SelectedXrayFace ||
         triangle.layer == ViewportOverlayLayer::SelectedFaceMask;
}

void strip_wireframe_object_face_fills(ViewportOverlayFrame &frame,
                                       RenderOverlaySelectionMode mode) {
  if (!wireframe_uses_wire_only_object_selection(mode)) {
    return;
  }

  frame.triangles.erase(
      std::remove_if(frame.triangles.begin(), frame.triangles.end(),
                     [](const ViewportOverlayTriangle &triangle) {
                       return wireframe_suppresses_triangle_layer(
                           triangle.layer);
                     }),
      frame.triangles.end());
}

} // namespace

ViewportOverlayFrame ViewportSceneOverlayBuilder::build_selection_frame(
    ViewportOverlayFrame base_frame,
    const RenderOverlaySelectionSnapshot &snapshot,
    bool point_light_gizmos_visible,
    RenderOverlaySelectionMode selection_mode,
    bool include_tool_overlays,
    bool selection_overlays_visible) {
  QDR_PROFILE_FUNCTION();
  const bool active_feedback = selection_mode_uses_selected_mesh_bounds(selection_mode);
  for (const RenderOverlayCurveSnapshot &curve : snapshot.curves) {
    if (curve.sample_payload == nullptr) {
      continue;
    }
    const bool selected = selection_overlays_visible && curve.selected;
    append_curve_overlay_stroke(base_frame, *curve.sample_payload,
        curve.world_transform, ViewportOverlayLine{
            .layer = ViewportOverlayLayer::WireframeBase,
            .color = selected ? kOverlaySelectionWireColor : kOverlayCurveColor,
            .width_pixels = selected ? kOverlaySelectionWireLineWidthPixels
                                     : kOverlayCurveLineWidthPixels,
            .style_role = selected ? (active_feedback && curve.active_selected
                ? ViewportOverlayStyleRole::ActiveSelectionWire : ViewportOverlayStyleRole::SelectionWire)
                                  : ViewportOverlayStyleRole::ExplicitColor,
            .source = ViewportOverlaySource::SceneCurve,
            .element = {.object_id = curve.modeling_object_id},
        });
  }
  if (selection_overlays_visible) {
    if (selection_mode == RenderOverlaySelectionMode::Object &&
        snapshot.selected_mesh_bounds.size() > 1u) {
      base_frame = strip_persistent_object_selection_fills(
          std::move(base_frame), selection_mode);
    }
    append_hierarchy_selection_wires(base_frame, snapshot, active_feedback);
    append_group_selection_cages(base_frame, snapshot, selection_mode);
    append_selected_light_markers(base_frame, snapshot, active_feedback);
  }
  if (point_light_gizmos_visible) {
    append_point_light_gizmos(base_frame, snapshot, active_feedback, selection_overlays_visible);
    if (selection_overlays_visible) {
      append_selected_light_radius_circles(base_frame, snapshot);
    }
  }
  if (include_tool_overlays) {
    base_frame.lines.insert(base_frame.lines.end(),
                            snapshot.tool_overlays.lines.begin(),
                            snapshot.tool_overlays.lines.end());
    base_frame.triangles.insert(base_frame.triangles.end(),
                                snapshot.tool_overlays.triangles.begin(),
                                snapshot.tool_overlays.triangles.end());
    base_frame.points.insert(base_frame.points.end(),
                             snapshot.tool_overlays.points.begin(),
                             snapshot.tool_overlays.points.end());
    base_frame.icons.insert(base_frame.icons.end(),
                            snapshot.tool_overlays.icons.begin(),
                            snapshot.tool_overlays.icons.end());
    if (snapshot.tool_overlays.hover_edge_dimension_context.has_value()) {
      base_frame.hover_edge_dimension_context =
          snapshot.tool_overlays.hover_edge_dimension_context;
    }
    base_frame.hover_target_selected =
        base_frame.hover_target_selected ||
        snapshot.tool_overlays.hover_target_selected;
  }
  return base_frame;
}

ViewportOverlayFrame ViewportSceneOverlayBuilder::build_point_light_radius_frame(
    std::span<const RenderOverlayLightMarkerSnapshot> selected_lights,
    const ViewportOverlayCamera &raw_camera, float raster_scale) {
  ViewportOverlayFrame frame;
  const ViewportOverlayCamera camera =
      OverlayCameraProjector::sanitized_camera(raw_camera);
  frame.lines.reserve(selected_lights.size() * 120u);
  frame.triangles.reserve(selected_lights.size() *
                          kViewportCircularHandleSegments * 3u);
  const float perspective_span_per_depth =
      2.0f * std::tan(camera.fov_degrees * std::numbers::pi_v<float> / 360.0f);
  const float handle_radius_pixels =
      kOverlayPointLightRadiusHandleSizePixels * 0.5f *
      viewport_overlay_sanitized_raster_scale(raster_scale);
  for (const RenderOverlayLightMarkerSnapshot &light : selected_lights) {
    if (!light.selected) continue;
    append_light_radius_circle(frame, light, camera.right, camera.up,
                               kOverlayPointLightRadiusSilhouetteColor);
    if (!light.radius_editable) continue;
    const QVec3 handle = transform_point(light.world_transform, {}) +
                         camera.right * light.radius;
    const float depth = dot(handle - camera.eye, camera.forward);
    if (!std::isfinite(depth) || depth < camera.near_plane) continue;
    const float world_radius =
        (camera.projection == ViewportOverlayProjection::Orthographic
             ? camera.orthographic_size
             : depth * perspective_span_per_depth) *
        handle_radius_pixels / camera.viewport_size.y;
    append_viewport_circular_handle(
        frame, handle, camera.right * world_radius, camera.up * -world_radius, {
            .source = ViewportOverlaySource::Gizmo,
            .element = {.object_id = static_cast<int>(light.entity)},
        });
  }
  return frame;
}

ViewportOverlayFrame
ViewportSceneOverlayBuilder::strip_persistent_object_selection_fills(
    ViewportOverlayFrame frame, RenderOverlaySelectionMode selection_mode) {
  if (selection_mode != RenderOverlaySelectionMode::Object) {
    return frame;
  }

  frame.triangles.erase(
      std::remove_if(frame.triangles.begin(), frame.triangles.end(),
                     persistent_object_selection_fill_triangle),
      frame.triangles.end());
  return frame;
}

ViewportOverlayFrame
ViewportSceneOverlayBuilder::strip_wireframe_selection_fills(
    ViewportOverlayFrame frame, RenderOverlaySelectionMode selection_mode) {
  strip_wireframe_object_face_fills(frame, selection_mode);
  return frame;
}

ViewportOverlayFrame ViewportSceneOverlayBuilder::build_wireframe_frame(
    ViewportOverlayFrame base_frame, RenderOverlaySelectionMode selection_mode,
    const RenderOverlaySceneWireframeSnapshot &snapshot) {
  QDR_PROFILE_FUNCTION();
  strip_wireframe_object_face_fills(base_frame, selection_mode);
  append_scene_wireframe_overlay(base_frame, snapshot);
  return base_frame;
}

} // namespace quader
