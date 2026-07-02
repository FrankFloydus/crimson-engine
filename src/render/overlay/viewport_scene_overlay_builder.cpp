////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/viewport_scene_overlay_builder.hpp"

#include "assets/asset_registry.hpp"
#include "diagnostics/profile.hpp"
#include "math/glm_bridge.hpp"
#include "mesh/polygon/document.hpp"
#include "render/viewport_overlay_policy.hpp"
#include "scene/scene_bounds.hpp"
#include "scene/scene_document.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <utility>

namespace quader {
namespace {

bool entity_selected(const SceneDocument &document, EntityId entity) {
  const SelectionComponent *selection = document.selection(entity);
  return selection != nullptr && selection->selected;
}

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
    ViewportOverlayFrame &frame, const SceneDocument &document,
    const AssetRegistry &assets, EntityId entity,
    std::uint32_t color = kOverlaySelectionWireColor,
    ViewportOverlayLayer layer = ViewportOverlayLayer::SourceWire,
    ViewportOverlayStyleRole style_role =
        ViewportOverlayStyleRole::SelectedSourceWire,
    ViewportOverlaySource source = ViewportOverlaySource::Selection) {
  QDR_PROFILE_FUNCTION();
  if (!document.is_effectively_visible(entity)) {
    return;
  }

  const MeshComponent *mesh_component = document.mesh(entity);
  if (mesh_component == nullptr) {
    return;
  }

  const MeshAsset *mesh = assets.mesh(mesh_component->mesh);
  if (mesh == nullptr || mesh->vertices.empty() || mesh->indices.size() < 3u) {
    return;
  }

  const QMat4 world_from_local = document.world_transform_matrix(entity);
  std::unordered_map<WireEdgeKey, WireEdgeRecord, WireEdgeKeyHash> edges;
  edges.reserve(mesh->indices.size());

  auto append_edge = [&](std::uint32_t a, std::uint32_t b,
                         QVec3 triangle_normal) {
    if (a >= mesh->vertices.size() || b >= mesh->vertices.size() || a == b) {
      return;
    }
    const QVec3 start =
        transform_point(world_from_local, mesh->vertices[a].position);
    const QVec3 end =
        transform_point(world_from_local, mesh->vertices[b].position);
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

  for (std::size_t index = 0; index + 2u < mesh->indices.size(); index += 3u) {
    const std::uint32_t a = mesh->indices[index];
    const std::uint32_t b = mesh->indices[index + 1u];
    const std::uint32_t c = mesh->indices[index + 2u];
    if (a >= mesh->vertices.size() || b >= mesh->vertices.size() ||
        c >= mesh->vertices.size()) {
      continue;
    }
    const QVec3 world_a =
        transform_point(world_from_local, mesh->vertices[a].position);
    const QVec3 world_b =
        transform_point(world_from_local, mesh->vertices[b].position);
    const QVec3 world_c =
        transform_point(world_from_local, mesh->vertices[c].position);
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

void append_wire_edge(
    std::unordered_map<WireEdgeKey, WireEdgeRecord, WireEdgeKeyHash> &edges,
    QVec3 start, QVec3 end) {
  const WireEdgeKey key = wire_edge_key(start, end);
  WireEdgeRecord &record = edges[key];
  if (record.count == 0u) {
    record.start = start;
    record.end = end;
  }
  ++record.count;
}

void emit_wireframe_edges(ViewportOverlayFrame &frame,
                          const std::unordered_map<WireEdgeKey, WireEdgeRecord,
                                                   WireEdgeKeyHash> &edges) {
  for (const auto &[key, edge] : edges) {
    (void)key;
    frame.lines.push_back(ViewportOverlayLine{
        .layer = ViewportOverlayLayer::WireframeXray,
        .start = edge.start,
        .end = edge.end,
        .color = kOverlayWireframeXrayColor,
        .width_pixels = 0.0f,
        .style_role = ViewportOverlayStyleRole::WireframeBase,
        .source = ViewportOverlaySource::SceneWireframe,
    });
  }
}

bool append_authored_polygon_wireframe(ViewportOverlayFrame &frame,
                                       const MeshAsset &mesh,
                                       const QMat4 &world_from_local) {
  if (!mesh.has_authored_polygon_document || !mesh.authored_polygon_document) {
    return false;
  }

  const quader_poly::Document &document = *mesh.authored_polygon_document;
  std::unordered_map<quader_poly::ElementId, QVec3> vertices;
  vertices.reserve(document.vertices.size());
  for (const quader_poly::Vertex &vertex : document.vertices) {
    vertices.emplace(vertex.id,
                     transform_point(world_from_local, vertex.position));
  }

  std::unordered_map<WireEdgeKey, WireEdgeRecord, WireEdgeKeyHash> edges;
  for (const quader_poly::Face &face : document.faces) {
    if (face.vertices.size() < 2u) {
      continue;
    }
    for (std::size_t index = 0; index < face.vertices.size(); ++index) {
      const quader_poly::ElementId a_id = face.vertices[index];
      const quader_poly::ElementId b_id =
          face.vertices[(index + 1u) % face.vertices.size()];
      const auto a = vertices.find(a_id);
      const auto b = vertices.find(b_id);
      if (a == vertices.end() || b == vertices.end()) {
        continue;
      }
      append_wire_edge(edges, a->second, b->second);
    }
  }
  if (edges.empty()) {
    return false;
  }

  emit_wireframe_edges(frame, edges);
  return true;
}

void append_triangle_mesh_wireframe(ViewportOverlayFrame &frame,
                                    const MeshAsset &mesh,
                                    const QMat4 &world_from_local) {
  std::unordered_map<WireEdgeKey, WireEdgeRecord, WireEdgeKeyHash> edges;
  edges.reserve(mesh.indices.size());
  auto append_triangle_edge = [&](std::uint32_t a, std::uint32_t b) {
    if (a >= mesh.vertices.size() || b >= mesh.vertices.size() || a == b) {
      return;
    }
    append_wire_edge(
        edges, transform_point(world_from_local, mesh.vertices[a].position),
        transform_point(world_from_local, mesh.vertices[b].position));
  };

  for (std::size_t index = 0; index + 2u < mesh.indices.size(); index += 3u) {
    append_triangle_edge(mesh.indices[index], mesh.indices[index + 1u]);
    append_triangle_edge(mesh.indices[index + 1u], mesh.indices[index + 2u]);
    append_triangle_edge(mesh.indices[index + 2u], mesh.indices[index]);
  }
  emit_wireframe_edges(frame, edges);
}

void append_hierarchy_selection_wires(ViewportOverlayFrame &frame,
                                      const SceneDocument &document,
                                      const AssetRegistry &assets) {
  for (const EntityId selected : document.selected_entities()) {
    if (!document.has_children(selected)) {
      continue;
    }
    for (const EntityId descendant : document.descendants(selected, false)) {
      if (entity_selected(document, descendant)) {
        continue;
      }
      append_mesh_selection_wire(frame, document, assets, descendant,
                                 kOverlaySelectionWireColor,
                                 ViewportOverlayLayer::SourceWire,
                                 ViewportOverlayStyleRole::SelectedSourceWire,
                                 ViewportOverlaySource::HierarchySelection);
    }
  }
}

void append_bounds_cage(ViewportOverlayFrame &frame,
                        const SceneBounds &bounds) {
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
        .style_role = ViewportOverlayStyleRole::GroupBounds,
        .source = ViewportOverlaySource::GroupBounds,
    });
  }
}

void append_group_selection_cages(ViewportOverlayFrame &frame,
                                  const SceneDocument &document,
                                  const AssetRegistry &assets) {
  for (const EntityId selected : document.selected_entities()) {
    if (document.kind(selected) != EntityKind::Empty ||
        !document.has_children(selected)) {
      continue;
    }
    if (const std::optional<SceneBounds> bounds =
            entity_hierarchy_world_bounds(document, assets, selected)) {
      append_bounds_cage(frame, *bounds);
    }
  }
}

void append_selected_light_markers(ViewportOverlayFrame &frame,
                                   const SceneDocument &document) {
  constexpr float kLightMarkerRadius = 0.28f;
  constexpr float kDirectionalLightLength = 0.85f;
  const std::array<QVec3, 3> axes{{
      {kLightMarkerRadius, 0.0f, 0.0f},
      {0.0f, kLightMarkerRadius, 0.0f},
      {0.0f, 0.0f, kLightMarkerRadius},
  }};

  for (const EntityId selected : document.selected_entities()) {
    const LightComponent *light = document.light(selected);
    if (light == nullptr || !document.is_effectively_visible(selected)) {
      continue;
    }
    const QMat4 world = document.world_transform_matrix(selected);
    const QVec3 position = transform_point(world, {});
    frame.points.push_back(ViewportOverlayPoint{
        .layer = ViewportOverlayLayer::SelectedVertex,
        .position = position,
        .color = kOverlaySelectedVertexColor,
        .size_pixels = kOverlayLightMarkerPointSizePixels,
        .style_role = ViewportOverlayStyleRole::SelectedVertex,
        .source = ViewportOverlaySource::Selection,
        .outline_growth_pixels = kOverlayLightMarkerOutlineGrowthPixels,
    });

    if (light->type == LightType::Directional) {
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
          .style_role = ViewportOverlayStyleRole::SelectionWire,
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
          .style_role = ViewportOverlayStyleRole::SelectionWire,
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
          .style_role = ViewportOverlayStyleRole::SelectionWire,
          .source = ViewportOverlaySource::Selection,
      });
    }
  }
}

void append_scene_wireframe_overlay(ViewportOverlayFrame &frame,
                                    const SceneDocument &document,
                                    const AssetRegistry &assets) {
  QDR_PROFILE_FUNCTION();
  for (const EntityId entity : document.entities()) {
    QDR_PROFILE_SCOPE("Viewport scene wireframe entity");
    if (!document.is_effectively_visible(entity)) {
      continue;
    }
    const MeshComponent *mesh_component = document.mesh(entity);
    if (mesh_component == nullptr) {
      continue;
    }
    const MeshAsset *mesh = assets.mesh(mesh_component->mesh);
    if (mesh == nullptr || mesh->vertices.empty() ||
        mesh->indices.size() < 3u) {
      continue;
    }
    const QMat4 world_from_local = document.world_transform_matrix(entity);
    if (!append_authored_polygon_wireframe(frame, *mesh, world_from_local)) {
      append_triangle_mesh_wireframe(frame, *mesh, world_from_local);
    }
  }
}

bool wireframe_uses_wire_only_object_selection(ModelingSelectionMode mode) {
  return mode == ModelingSelectionMode::Object ||
         mode == ModelingSelectionMode::Group;
}

bool wireframe_suppresses_triangle_layer(ViewportOverlayLayer layer) {
  return layer == ViewportOverlayLayer::SelectedVisibleFace ||
         layer == ViewportOverlayLayer::SelectedXrayFace ||
         layer == ViewportOverlayLayer::SelectedFaceMask ||
         layer == ViewportOverlayLayer::HoverFace;
}

bool persistent_object_selection_triangle(
    const ViewportOverlayTriangle &triangle) {
  if (triangle.source != ViewportOverlaySource::Selection ||
      triangle.element.object_id < 0) {
    return false;
  }
  return triangle.layer == ViewportOverlayLayer::SelectedVisibleFace ||
         triangle.layer == ViewportOverlayLayer::SelectedXrayFace ||
         triangle.layer == ViewportOverlayLayer::SelectedFaceMask;
}

bool persistent_object_selection_line(const ViewportOverlayLine &line) {
  if (line.source != ViewportOverlaySource::Selection ||
      line.element.object_id < 0) {
    return false;
  }
  return line.layer == ViewportOverlayLayer::SelectedFaceEdge ||
         line.layer == ViewportOverlayLayer::SelectedEdge;
}

void strip_wireframe_object_face_fills(ViewportOverlayFrame &frame,
                                       ModelingSelectionMode mode) {
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
    ViewportOverlayFrame base_frame, const SceneDocument &document,
    const AssetRegistry &assets) {
  QDR_PROFILE_FUNCTION();
  append_hierarchy_selection_wires(base_frame, document, assets);
  append_group_selection_cages(base_frame, document, assets);
  append_selected_light_markers(base_frame, document);
  return base_frame;
}

ViewportOverlayFrame
ViewportSceneOverlayBuilder::strip_persistent_object_selection(
    ViewportOverlayFrame frame, ModelingSelectionMode selection_mode) {
  if (selection_mode != ModelingSelectionMode::Object) {
    return frame;
  }

  frame.triangles.erase(std::remove_if(frame.triangles.begin(),
                                       frame.triangles.end(),
                                       persistent_object_selection_triangle),
                        frame.triangles.end());
  frame.lines.erase(std::remove_if(frame.lines.begin(), frame.lines.end(),
                                   persistent_object_selection_line),
                    frame.lines.end());
  return frame;
}

ViewportOverlayFrame
ViewportSceneOverlayBuilder::strip_wireframe_selection_fills(
    ViewportOverlayFrame frame, ModelingSelectionMode selection_mode) {
  strip_wireframe_object_face_fills(frame, selection_mode);
  return frame;
}

ViewportOverlayFrame ViewportSceneOverlayBuilder::build_wireframe_frame(
    ViewportOverlayFrame base_frame, ModelingSelectionMode selection_mode,
    const SceneDocument &document, const AssetRegistry &assets) {
  QDR_PROFILE_FUNCTION();
  strip_wireframe_object_face_fills(base_frame, selection_mode);
  append_scene_wireframe_overlay(base_frame, document, assets);
  return base_frame;
}

} // namespace quader
