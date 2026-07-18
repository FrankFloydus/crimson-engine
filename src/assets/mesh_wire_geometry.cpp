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

void append_authored_segments(const MeshAsset &mesh,
                              MeshWireGeometryPayload &payload) {
  if (!mesh.has_authored_polygon_document ||
      mesh.authored_polygon_document == nullptr) {
    return;
  }

  const quader_poly::Document &document = *mesh.authored_polygon_document;
  std::unordered_map<quader_poly::ElementId, QVec3> positions;
  positions.reserve(document.vertices.size());
  for (const quader_poly::Vertex &vertex : document.vertices) {
    positions.emplace(vertex.id, vertex.position);
  }

  std::unordered_set<std::uint64_t> emitted;
  for (const quader_poly::Face &face : document.faces) {
    if (face.vertices.size() < 2u) {
      continue;
    }
    for (std::size_t index = 0u; index < face.vertices.size(); ++index) {
      const quader_poly::ElementId a_id = face.vertices[index];
      const quader_poly::ElementId b_id =
          face.vertices[(index + 1u) % face.vertices.size()];
      if (a_id == b_id || !emitted.insert(edge_key(a_id, b_id)).second) {
        continue;
      }
      const auto a = positions.find(a_id);
      const auto b = positions.find(b_id);
      if (a != positions.end() && b != positions.end()) {
        payload.segments.push_back({a->second, b->second});
      }
    }
  }
  payload.authored = !payload.segments.empty();
}

void append_compiled_segments(const MeshAsset &mesh,
                              MeshWireGeometryPayload &payload) {
  const MeshGeometryPayload &geometry = mesh.geometry();
  std::unordered_set<std::uint64_t> emitted;
  emitted.reserve(geometry.indices.size());
  const auto append_edge = [&](std::uint32_t a, std::uint32_t b) {
    if (a >= geometry.vertices.size() || b >= geometry.vertices.size() ||
        a == b || !emitted.insert(edge_key(a, b)).second) {
      return;
    }
    payload.segments.push_back(
        {geometry.vertices[a].position, geometry.vertices[b].position});
  };
  for (std::size_t index = 0u; index + 2u < geometry.indices.size();
       index += 3u) {
    append_edge(geometry.indices[index], geometry.indices[index + 1u]);
    append_edge(geometry.indices[index + 1u], geometry.indices[index + 2u]);
    append_edge(geometry.indices[index + 2u], geometry.indices[index]);
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
  const MeshGeometryPayload *geometry_source = &geometry();
  const quader_poly::Document *authored_source =
      authored_polygon_document.get();
  if (authored_source == nullptr) {
    if (geometry_source->wire_geometry_payload_ == nullptr) {
      geometry_source->wire_geometry_payload_ =
          compute_mesh_wire_geometry_payload(*this);
    }
    return geometry_source->wire_geometry_payload_;
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

} // namespace quader
