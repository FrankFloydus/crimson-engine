////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "assets/mesh_wire_geometry.hpp"

#include "assets/mesh_asset.hpp"
#include "quader/modeling/neutral/polygon/document_model.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <unordered_map>
#include <unordered_set>

namespace quader {
namespace {

[[nodiscard]] std::uint64_t edge_key(std::uint32_t a, std::uint32_t b) {
  const std::uint32_t low = std::min(a, b);
  const std::uint32_t high = std::max(a, b);
  return (static_cast<std::uint64_t>(low) << 32u) | high;
}

[[nodiscard]] double support_area_squared(QVec3 start, QVec3 end, QVec3 point) {
  const double ax = static_cast<double>(end.x) - start.x;
  const double ay = static_cast<double>(end.y) - start.y;
  const double az = static_cast<double>(end.z) - start.z;
  const double bx = static_cast<double>(point.x) - start.x;
  const double by = static_cast<double>(point.y) - start.y;
  const double bz = static_cast<double>(point.z) - start.z;
  const double nx = ay * bz - az * by;
  const double ny = az * bx - ax * bz;
  const double nz = ax * by - ay * bx;
  const double area = nx * nx + ny * ny + nz * nz;
  const double scale = (ax * ax + ay * ay + az * az) *
                       (bx * bx + by * by + bz * bz);
  return std::isfinite(area) && std::isfinite(scale) && area > scale * 1.0e-12
             ? area : 0.0;
}

void append_support(MeshWireSegment &segment, QVec3 point) {
  if (segment.surface_support_count >= segment.surface_support.size() ||
      support_area_squared(segment.start, segment.end, point) == 0.0) {
    return;
  }
  for (std::size_t index = 0u; index < segment.surface_support_count; ++index) {
    const QVec3 existing = segment.surface_support[index];
    if (existing.x == point.x && existing.y == point.y && existing.z == point.z) return;
  }
  segment.surface_support[segment.surface_support_count++] = point;
}

struct CompiledWireSupport final {
  quader_poly::ElementId vertex = quader_poly::kInvalidElementId;
  QVec3 position;
  double area_squared = 0.0;
};

void append_authored_segments(const MeshAsset &mesh,
                              MeshWireGeometryPayload &payload) {
  if (!mesh.has_authored_polygon_document ||
      mesh.authored_polygon_document == nullptr) {
    return;
  }

  const auto document = mesh.authored_polygon_document.read_view();
  std::unordered_map<quader_poly::ElementId, QVec3> positions;
  positions.reserve(document.vertices.size());
  for (const quader_poly::Vertex &vertex : document.vertices) {
    if (std::isfinite(vertex.position.x) && std::isfinite(vertex.position.y) &&
        std::isfinite(vertex.position.z)) {
      positions.emplace(vertex.id, vertex.position);
    }
  }
  // Effective positions are independent of the retained authored topology.
  const auto compiled_vertices = mesh.geometry_vertices();
  const auto effective_positions = mesh.geometry_positions();
  const bool has_effective_positions = effective_positions.size() == compiled_vertices.size();
  if (has_effective_positions) {
    for (std::size_t index = 0u; index < compiled_vertices.size(); ++index) {
      const auto found = positions.find(compiled_vertices[index].source_vertex_id);
      if (found != positions.end()) found->second = effective_positions[index];
    }
  }

  std::unordered_map<std::uint64_t, std::size_t> edge_indices;
  for (const quader_poly::Face &face : document.faces) {
    if (face.vertices.size() < 2u) continue;
    for (std::size_t index = 0u; index < face.vertices.size(); ++index) {
      const auto a_id = face.vertices[index];
      const auto b_id = face.vertices[(index + 1u) % face.vertices.size()];
      const auto a = positions.find(a_id);
      const auto b = positions.find(b_id);
      if (a_id != b_id && a != positions.end() && b != positions.end() &&
          edge_indices.emplace(edge_key(a_id, b_id), payload.segments.size()).second) {
        payload.segments.push_back({a->second, b->second});
      }
    }
  }

  // Only authored boundaries receive triangle supports; triangulation diagonals
  // never become wire segments. Keep the two largest incident candidates.
  std::vector<std::array<CompiledWireSupport, 2>> compiled_supports(payload.segments.size());
  const auto indices = mesh.geometry_indices();
  for (std::size_t triangle = 0u; triangle + 2u < indices.size(); triangle += 3u) {
    for (std::size_t corner = 0u; corner < 3u; ++corner) {
      const auto a = indices[triangle + corner];
      const auto b = indices[triangle + (corner + 1u) % 3u];
      const auto third = indices[triangle + (corner + 2u) % 3u];
      if (a >= compiled_vertices.size() || b >= compiled_vertices.size() ||
          third >= compiled_vertices.size()) continue;
      const auto edge = edge_indices.find(edge_key(compiled_vertices[a].source_vertex_id,
                                                   compiled_vertices[b].source_vertex_id));
      if (edge == edge_indices.end()) continue;
      const auto third_id = compiled_vertices[third].source_vertex_id;
      const auto point = has_effective_positions ? effective_positions[third]
                                                 : compiled_vertices[third].position;
      const auto &segment = payload.segments[edge->second];
      const double area = support_area_squared(segment.start, segment.end, point);
      if (area == 0.0 || !positions.contains(third_id)) continue;
      auto &candidates = compiled_supports[edge->second];
      auto &slot = candidates[0].area_squared <= candidates[1].area_squared
                       ? candidates[0] : candidates[1];
      if (area > slot.area_squared) slot = {third_id, point, area};
    }
  }

  for (const quader_poly::Face &face : document.faces) {
    if (face.vertices.size() < 3u) continue;
    const std::unordered_set<quader_poly::ElementId> face_vertices(
        face.vertices.begin(), face.vertices.end());
    for (std::size_t index = 0u; index < face.vertices.size(); ++index) {
      const auto edge = edge_indices.find(edge_key(face.vertices[index],
          face.vertices[(index + 1u) % face.vertices.size()]));
      if (edge == edge_indices.end()) continue;
      auto &segment = payload.segments[edge->second];
      if (segment.surface_support_count == segment.surface_support.size()) continue;
      QVec3 support;
      double best_area = 0.0;
      for (const auto &candidate : compiled_supports[edge->second]) {
        if (candidate.area_squared > best_area && face_vertices.contains(candidate.vertex)) {
          support = candidate.position;
          best_area = candidate.area_squared;
        }
      }
      // Missing provenance or a collinear compiled boundary retains a stable
      // incident-face plane without depending on an arbitrary polygon center.
      if (best_area == 0.0) {
        for (const auto vertex : face.vertices) {
          const auto position = positions.find(vertex);
          if (position == positions.end()) continue;
          const double area = support_area_squared(segment.start, segment.end, position->second);
          if (area > best_area) {
            best_area = area;
            support = position->second;
          }
        }
      }
      if (best_area > 0.0) append_support(segment, support);
    }
  }
  payload.authored = !payload.segments.empty();
}

void append_compiled_segments(const MeshAsset &mesh,
                              MeshWireGeometryPayload &payload) {
  const auto vertices = mesh.geometry_vertices();
  const auto positions = mesh.geometry_positions();
  const auto indices = mesh.geometry_indices();
  const bool has_positions = positions.size() == vertices.size();
  std::unordered_map<std::uint64_t, std::size_t> emitted;
  emitted.reserve(indices.size());
  const auto position = [&](std::uint32_t vertex) {
    return has_positions ? positions[vertex] : vertices[vertex].position;
  };
  const auto append_edge = [&](std::uint32_t a, std::uint32_t b, std::uint32_t third) {
    if (a >= vertices.size() || b >= vertices.size() || a == b) return;
    const auto [found, inserted] = emitted.emplace(edge_key(a, b), payload.segments.size());
    if (inserted) payload.segments.push_back({position(a), position(b)});
    if (third < vertices.size()) append_support(payload.segments[found->second], position(third));
  };
  for (std::size_t index = 0u; index + 2u < indices.size(); index += 3u) {
    append_edge(indices[index], indices[index + 1u], indices[index + 2u]);
    append_edge(indices[index + 1u], indices[index + 2u], indices[index]);
    append_edge(indices[index + 2u], indices[index], indices[index + 1u]);
  }
}

[[nodiscard]] std::uint64_t content_id(
    const MeshWireGeometryPayload &payload) {
  constexpr std::uint64_t kFnvOffset = 1469598103934665603ULL;
  constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
  std::uint64_t hash = kFnvOffset;
  const auto hash_u32 = [&hash](std::uint32_t value) {
    for (unsigned int byte = 0; byte < 4u; ++byte) {
      hash ^= (value >> (byte * 8u)) & 0xffu;
      hash *= kFnvPrime;
    }
  };
  const auto hash_float = [&hash_u32](float value) {
    std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
    if (bits == 0x80000000u) {
      bits = 0u;
    }
    hash_u32(bits);
  };
  hash_u32(payload.authored ? 1u : 0u);
  hash_u32(static_cast<std::uint32_t>(payload.segments.size()));
  for (const MeshWireSegment &segment : payload.segments) {
    hash_float(segment.start.x);
    hash_float(segment.start.y);
    hash_float(segment.start.z);
    hash_float(segment.end.x);
    hash_float(segment.end.y);
    hash_float(segment.end.z);
    hash_u32(segment.surface_support_count);
    for (std::size_t index = 0u; index < segment.surface_support_count; ++index) {
      hash_float(segment.surface_support[index].x);
      hash_float(segment.surface_support[index].y);
      hash_float(segment.surface_support[index].z);
    }
  }
  return hash == 0u ? 1u : hash;
}

std::shared_ptr<const MeshWireGeometryPayload>
compute_mesh_wire_geometry_payload(const MeshAsset &mesh) {
  auto payload = std::make_shared<MeshWireGeometryPayload>();
  append_authored_segments(mesh, *payload);
  if (payload->segments.empty()) {
    append_compiled_segments(mesh, *payload);
  }
  payload->content_id = content_id(*payload);
  return payload;
}

} // namespace

std::shared_ptr<const MeshWireGeometryPayload>
MeshAsset::wire_geometry_payload() const {
  const std::uintptr_t geometry_source = geometry_token();
  const std::uintptr_t authored_source = authored_polygon_document.identity_token();
  if (authored_source == 0u && !geometry_projection_) {
    if (geometry_->wire_geometry_payload_ == nullptr) {
      geometry_->wire_geometry_payload_ =
          compute_mesh_wire_geometry_payload(*this);
    }
    return geometry_->wire_geometry_payload_;
  }
  if (wire_geometry_payload_ != nullptr &&
      wire_geometry_source_ == geometry_source &&
      wire_geometry_authored_source_ == authored_source &&
      wire_geometry_authored_enabled_ == has_authored_polygon_document) {
    return wire_geometry_payload_;
  }
  wire_geometry_payload_ = compute_mesh_wire_geometry_payload(*this);
  wire_geometry_source_ = geometry_source;
  wire_geometry_authored_source_ = authored_source;
  wire_geometry_authored_enabled_ = has_authored_polygon_document;
  return wire_geometry_payload_;
}

std::shared_ptr<const MeshWireGeometryPayload>
build_mesh_wire_geometry_payload(const MeshAsset &mesh) {
  return mesh.wire_geometry_payload();
}

MeshTopologyCounts mesh_topology_counts(const MeshAsset &mesh) {
  MeshTopologyCounts counts;
  if (mesh.has_authored_polygon_document &&
      mesh.authored_polygon_document != nullptr) {
    const auto document = mesh.authored_polygon_document.read_view();
    counts.vertices = document.vertices.size();
    counts.faces = document.faces.size();
    for (const quader_poly::Face &face : document.faces) {
      if (face.vertices.size() >= 3u) {
        counts.triangles += face.vertices.size() - 2u;
      }
    }
  } else {
    counts.vertices = mesh.geometry_vertices().size();
    counts.triangles = mesh.geometry_indices().size() / 3u;
    counts.faces = counts.triangles;
  }
  if (const std::shared_ptr<const MeshWireGeometryPayload> wire =
          mesh.wire_geometry_payload()) {
    counts.edges = wire->segments.size();
  }
  return counts;
}

} // namespace quader
