////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/projection/cut_preview_overlay_projection.hpp"

#include "math/glm_bridge.hpp"
#include "render/viewport_overlay_policy.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>

namespace quader {
namespace {

constexpr float kCutPreviewPlaneQuantization = 0.0001f;
constexpr float kCutPreviewFeatureEdgeCosine = 0.985f;

/**
 * Stores the Cut Preview Point Key data contract used by projection.
 */
struct CutPreviewPointKey {
    long long x = 0;
    long long y = 0;
    long long z = 0;
};

long long quantize_cut_preview_coordinate(float value) {
  return static_cast<long long>(
      std::llround(value / kCutPreviewPlaneQuantization));
}

CutPreviewPointKey cut_preview_point_key(QVec3 value) {
  return {
      .x = quantize_cut_preview_coordinate(value.x),
      .y = quantize_cut_preview_coordinate(value.y),
      .z = quantize_cut_preview_coordinate(value.z),
  };
}

bool cut_preview_point_key_less(const CutPreviewPointKey &left,
                                const CutPreviewPointKey &right) {
  if (left.x != right.x) {
    return left.x < right.x;
  }
  if (left.y != right.y) {
    return left.y < right.y;
  }
  return left.z < right.z;
}

/**
 * Stores the Cut Preview Edge Key data contract used by projection.
 */
struct CutPreviewEdgeKey {
    CutPreviewPointKey a;
    CutPreviewPointKey b;

    CutPreviewEdgeKey(QVec3 first, QVec3 second) {
      CutPreviewPointKey first_key = cut_preview_point_key(first);
      CutPreviewPointKey second_key = cut_preview_point_key(second);
      if (cut_preview_point_key_less(second_key, first_key)) {
        a = second_key;
        b = first_key;
      } else {
        a = first_key;
        b = second_key;
      }
    }

    [[nodiscard]] bool operator<(const CutPreviewEdgeKey& other) const {
      if (cut_preview_point_key_less(a, other.a)) {
        return true;
      }
      if (cut_preview_point_key_less(other.a, a)) {
        return false;
      }
      return cut_preview_point_key_less(b, other.b);
    }
};

/**
 * Represents a Cut Preview Edge Accumulator value used by projection.
 */
struct CutPreviewEdgeAccumulator {
    QVec3 a;
    QVec3 b;
    QVec3 normal;
    bool has_normal = false;
    bool has_cap_triangle = false;
    bool has_side_triangle = false;
    bool feature = false;
    int count = 0;

    void add(QVec3 first, QVec3 second, bool on_plane, bool cap_triangle,
             QVec3 triangle_normal) {
      if (length(first - second) <= 0.000001f) {
        return;
      }
      if (count == 0) {
        a = first;
        b = second;
        normal = triangle_normal;
        has_normal = true;
      } else if (has_normal && std::fabs(dot(normal, triangle_normal)) <
                                   kCutPreviewFeatureEdgeCosine) {
        feature = true;
      }

      ++count;
      if (on_plane && cap_triangle) {
        has_cap_triangle = true;
      }
      if (!cap_triangle) {
        has_side_triangle = true;
      }
    }

    [[nodiscard]] bool seam() const {
      return has_cap_triangle && has_side_triangle;
    }

    [[nodiscard]] bool outline() const {
        return count == 1 || feature;
    }
};

float signed_distance_to_plane(QVec3 point, QVec3 plane_point,
                               QVec3 plane_normal) {
  return dot(point - plane_point, plane_normal);
}

float preview_mesh_bounds_magnitude(
    const CutPreviewOverlayProjection::PreviewTriangleMesh &mesh) {
  if (mesh.vertices.empty()) {
    return 0.0f;
  }

  QVec3 min = mesh.vertices.front();
  QVec3 max = mesh.vertices.front();
  for (const QVec3 vertex : mesh.vertices) {
    min.x = std::min(min.x, vertex.x);
    min.y = std::min(min.y, vertex.y);
    min.z = std::min(min.z, vertex.z);
    max.x = std::max(max.x, vertex.x);
    max.y = std::max(max.y, vertex.y);
    max.z = std::max(max.z, vertex.z);
  }
  return length(max - min);
}

void add_cut_preview_edge(
    std::map<CutPreviewEdgeKey, CutPreviewEdgeAccumulator> &edges, QVec3 a,
    QVec3 b, bool on_plane, bool cap_triangle, QVec3 triangle_normal) {
  if (length(a - b) <= 0.000001f) {
    return;
  }
  edges[CutPreviewEdgeKey(a, b)].add(a, b, on_plane, cap_triangle,
                                     triangle_normal);
}

std::map<CutPreviewEdgeKey, CutPreviewEdgeAccumulator> collect_preview_edges(
    const CutPreviewOverlayProjection::PreviewTriangleMesh &mesh,
    std::optional<QVec3> plane_point, QVec3 plane_normal) {
  std::map<CutPreviewEdgeKey, CutPreviewEdgeAccumulator> edges;
  if (mesh.empty()) {
    return edges;
  }

  const bool has_plane =
      plane_point.has_value() && length(plane_normal) > 0.000001f;
  const QVec3 normalized_plane_normal =
      has_plane ? normalize(plane_normal) : QVec3{};
  const float plane_scale = std::max(1.0f, preview_mesh_bounds_magnitude(mesh));
  const float plane_epsilon =
      std::max(kCutPreviewPlaneQuantization * 8.0f, plane_scale * 0.00002f);

  for (std::size_t index = 0; index + 2U < mesh.indices.size(); index += 3U) {
    const std::uint32_t ia = mesh.indices[index];
    const std::uint32_t ib = mesh.indices[index + 1U];
    const std::uint32_t ic = mesh.indices[index + 2U];
    if (ia >= mesh.vertices.size() || ib >= mesh.vertices.size() ||
        ic >= mesh.vertices.size()) {
      continue;
    }

    const QVec3 a = mesh.vertices[ia];
    const QVec3 b = mesh.vertices[ib];
    const QVec3 c = mesh.vertices[ic];
    QVec3 triangle_normal = cross(b - a, c - a);
    if (length(triangle_normal) <= 0.000001f) {
      continue;
    }
    triangle_normal = normalize(triangle_normal);

    float da = 1.0f;
    float db = 1.0f;
    float dc = 1.0f;
    bool cap_triangle = false;
    if (has_plane) {
      da = signed_distance_to_plane(a, *plane_point, normalized_plane_normal);
      db = signed_distance_to_plane(b, *plane_point, normalized_plane_normal);
      dc = signed_distance_to_plane(c, *plane_point, normalized_plane_normal);
      cap_triangle = std::fabs(da) <= plane_epsilon &&
                     std::fabs(db) <= plane_epsilon &&
                     std::fabs(dc) <= plane_epsilon;
    }

    add_cut_preview_edge(edges, a, b,
                         has_plane && std::fabs(da) <= plane_epsilon &&
                             std::fabs(db) <= plane_epsilon,
                         cap_triangle, triangle_normal);
    add_cut_preview_edge(edges, b, c,
                         has_plane && std::fabs(db) <= plane_epsilon &&
                             std::fabs(dc) <= plane_epsilon,
                         cap_triangle, triangle_normal);
    add_cut_preview_edge(edges, c, a,
                         has_plane && std::fabs(dc) <= plane_epsilon &&
                             std::fabs(da) <= plane_epsilon,
                         cap_triangle, triangle_normal);
  }
  return edges;
}

void append_preview_line(ViewportOverlayFrame &frame,
                         ViewportOverlayLayer layer, QVec3 start, QVec3 end,
                         std::uint32_t color, float width_pixels,
                         ViewportOverlayStyleRole style_role =
                             ViewportOverlayStyleRole::ToolPreviewLine) {
  if (length(start - end) <= 0.000001f) {
    return;
  }
  frame.lines.push_back({
      .layer = layer,
      .start = start,
      .end = end,
      .color = color,
      .width_pixels = width_pixels,
      .style_role = style_role,
      .source = ViewportOverlaySource::Tool,
  });
}

} // namespace

CutPreviewOverlayProjection::PreviewTriangleMesh
CutPreviewOverlayProjection::mesh_from_previews(
    const std::vector<ModelingToolPreviewMesh> &previews) {
  PreviewTriangleMesh mesh;
  for (const ModelingToolPreviewMesh &preview : previews) {
    const MeshGeometryPayload &geometry = preview.mesh.geometry();
    const std::uint32_t vertex_offset =
        static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.reserve(mesh.vertices.size() + geometry.vertices.size());
    mesh.indices.reserve(mesh.indices.size() + geometry.indices.size());
    for (const MeshVertex &vertex : geometry.vertices) {
      mesh.vertices.push_back(transform_point(preview.transform,
                                              vertex.position));
    }
    for (const std::uint32_t index : geometry.indices) {
      if (index < geometry.vertices.size()) {
        mesh.indices.push_back(vertex_offset + index);
      }
    }
  }
  return mesh;
}

void CutPreviewOverlayProjection::append_mesh_fill(
    ViewportOverlayFrame &frame, const PreviewTriangleMesh &mesh,
    std::uint32_t color, ViewportOverlayStyleRole style_role) {
  if (mesh.empty()) {
    return;
  }

  for (std::size_t index = 0; index + 2U < mesh.indices.size(); index += 3U) {
    const std::uint32_t ia = mesh.indices[index];
    const std::uint32_t ib = mesh.indices[index + 1U];
    const std::uint32_t ic = mesh.indices[index + 2U];
    if (ia >= mesh.vertices.size() || ib >= mesh.vertices.size() ||
        ic >= mesh.vertices.size()) {
      continue;
    }
    frame.triangles.push_back({
        .layer = ViewportOverlayLayer::ToolPreviewFill,
        .a = mesh.vertices[ia],
        .b = mesh.vertices[ib],
        .c = mesh.vertices[ic],
        .color = color,
        .style_role = style_role,
        .source = ViewportOverlaySource::Tool,
    });
  }
}

void CutPreviewOverlayProjection::append_result_lines(
    ViewportOverlayFrame &frame, const PreviewTriangleMesh &mesh,
    QVec3 plane_point, QVec3 plane_normal, std::uint32_t outline_color) {
  const std::map<CutPreviewEdgeKey, CutPreviewEdgeAccumulator> edges =
      collect_preview_edges(mesh, plane_point, plane_normal);
  for (const auto &[key, edge] : edges) {
    (void)key;
    if (edge.outline()) {
      append_preview_line(
          frame, ViewportOverlayLayer::ToolPreviewLine, edge.a, edge.b,
          outline_color, 1.15f,
          outline_color == kOverlayCutPreviewKeepBothOutlineColor
              ? ViewportOverlayStyleRole::CutPreviewKeepBothOutline
              : ViewportOverlayStyleRole::CutPreviewDiscardOutline);
    }
    if (edge.seam()) {
      append_preview_line(frame, ViewportOverlayLayer::ToolPreviewSeam, edge.a,
                          edge.b, kOverlayCutPreviewSeamColor, 2.25f,
                          ViewportOverlayStyleRole::CutPreviewSeam);
    }
  }
}

} // namespace quader
