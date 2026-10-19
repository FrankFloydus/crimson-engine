////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "assets/modeling_mesh_payload_adapter.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace quader {

std::optional<MeshGeometryPayload> mesh_geometry_payload_from_modeling_payload(
    const modeling::MeshPayload &source) {
  if (source.vertices.empty() || source.indices.empty() ||
      source.vertices.size() > std::numeric_limits<std::uint32_t>::max() ||
      std::ranges::any_of(source.indices, [&](std::uint32_t index) {
        return index >= source.vertices.size();
      })) {
    return std::nullopt;
  }

  MeshGeometryPayload geometry;
  for (const auto &field : source.circular_uv_interpolations) {
    geometry.circular_uv_interpolations.push_back(circular_uv_interpolation_from_modeling(field));
  }
  geometry.vertices.reserve(source.vertices.size());
  for (const modeling::MeshVertexPayload &vertex : source.vertices) {
    geometry.vertices.push_back({
        .position = {vertex.position.x, vertex.position.y, vertex.position.z},
        .normal = {vertex.normal.x, vertex.normal.y, vertex.normal.z},
        .uv = {vertex.uv0.x, vertex.uv0.y},
        .rgba = vertex.color,
        .source_vertex_id = vertex.source_vertex.index,
        .uv_carrier = vertex.uv_carrier,
        .uv_geometric_normal = {vertex.uv_geometric_normal.x,
                                vertex.uv_geometric_normal.y,
                                vertex.uv_geometric_normal.z},
        .circular_uv_index = vertex.circular_uv_index,
    });
  }
  if (std::ranges::any_of(geometry.vertices, [&](const MeshVertex &vertex) {
        return vertex.circular_uv_index > geometry.circular_uv_interpolations.size();
      })) return std::nullopt;
  geometry.indices = source.indices;
  geometry.primitives.reserve(source.primitives.size());
  for (const modeling::MeshPrimitivePayload &primitive : source.primitives) {
    const std::size_t offset =
        static_cast<std::size_t>(primitive.index_offset);
    const std::size_t count =
        static_cast<std::size_t>(primitive.index_count);
    if (offset > geometry.indices.size() ||
        count > geometry.indices.size() - offset) {
      return std::nullopt;
    }
    geometry.primitives.push_back({
        .index_offset = primitive.index_offset,
        .index_count = primitive.index_count,
        .material_slot = primitive.material.valid()
                             ? primitive.material.index - 1u
                             : 0u,
    });
  }
  (void)geometry.content_id();
  return geometry;
}

std::optional<MeshAsset>
mesh_asset_from_modeling_payload(const modeling::MeshPayload &source,
                                 std::string name) {
  std::optional<MeshGeometryPayload> geometry =
      mesh_geometry_payload_from_modeling_payload(source);
  if (!geometry.has_value()) {
    return std::nullopt;
  }
  MeshAsset mesh;
  mesh.name = std::move(name);
  mesh.has_stable_cpu_topology = true;
  mesh.has_authored_polygon_document = false;
  mesh.replace_geometry(std::move(*geometry));
  return mesh;
}

} // namespace quader
