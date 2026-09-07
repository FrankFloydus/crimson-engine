////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "assets/mesh_collision_geometry_payload.hpp"

#include "assets/mesh_asset.hpp"

#include <atomic>
#include <bit>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

namespace quader {
namespace {

[[nodiscard]] float snapped_collision_coordinate(float value) noexcept {
  // Godot's Vector3::snappedf passes real_t(0.0001) to Math::snapped's
  // double-precision calculation, including its rounding toward +infinity at ties.
  constexpr double kStep = static_cast<double>(0.0001f);
  return static_cast<float>(
      std::floor(static_cast<double>(value) / kStep + 0.5) * kStep);
}

[[nodiscard]] bool finite_position(QVec3 value) noexcept {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

[[nodiscard]] std::uint64_t
collision_content_id(const std::vector<QVec3> &faces) noexcept {
  constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
  constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
  std::uint64_t hash = kFnvOffset;
  const auto append_float = [&hash](float value) {
    const std::uint32_t bits =
        value == 0.0f ? 0u : std::bit_cast<std::uint32_t>(value);
    for (unsigned int byte = 0u; byte < 4u; ++byte) {
      hash ^= (bits >> (byte * 8u)) & 0xffu;
      hash *= kFnvPrime;
    }
  };
  for (const QVec3 face : faces) {
    append_float(face.x);
    append_float(face.y);
    append_float(face.z);
  }
  return hash == 0u ? 1u : hash;
}

[[nodiscard]] MeshCollisionGeometryPayload
generate_collision_geometry(const MeshGeometryPayload &geometry) {
  MeshCollisionGeometryPayload result;
  if (geometry.indices.size() >
      (std::numeric_limits<std::uint32_t>::max)()) {
    result.error_message =
        "Mesh collision index count exceeds the supported range.";
    return result;
  }

  const std::vector<MeshPrimitiveSpan> spans =
      normalized_mesh_primitive_spans(geometry);
  std::size_t face_vertex_count = 0u;
  for (const MeshPrimitiveSpan &span : spans) {
    if (span.index_count % 3u != 0u) {
      result.error_message = "Mesh collision primitive is not a triangle list.";
      return result;
    }
    if (span.index_count > result.faces.max_size() - face_vertex_count) {
      result.error_message =
          "Mesh collision triangle count exceeds the supported range.";
      return result;
    }
    face_vertex_count += span.index_count;
  }

  result.faces.reserve(face_vertex_count);
  for (const MeshPrimitiveSpan &span : spans) {
    const std::size_t end =
        static_cast<std::size_t>(span.index_offset) + span.index_count;
    for (std::size_t index = span.index_offset; index < end; ++index) {
      const std::uint32_t vertex_index = geometry.indices[index];
      if (vertex_index >= geometry.vertices.size()) {
        result.faces.clear();
        result.error_message = "Mesh collision references an out-of-range vertex.";
        return result;
      }
      const QVec3 position = geometry.vertices[vertex_index].position;
      if (!finite_position(position)) {
        result.faces.clear();
        result.error_message = "Mesh collision contains a non-finite position.";
        return result;
      }
      result.faces.push_back({snapped_collision_coordinate(position.x),
                              snapped_collision_coordinate(position.y),
                              snapped_collision_coordinate(position.z)});
    }
  }
  result.content_id = collision_content_id(result.faces);
  return result;
}

} // namespace

std::shared_ptr<const MeshCollisionGeometryPayload>
mesh_collision_geometry_payload(const MeshGeometryPayload &geometry) {
  auto cached = std::atomic_load_explicit(&geometry.collision_geometry_payload_,
                                         std::memory_order_acquire);
  if (cached) {
    return cached;
  }

  const std::shared_ptr<const MeshCollisionGeometryPayload> generated =
      std::make_shared<MeshCollisionGeometryPayload>(
          generate_collision_geometry(geometry));
  if (std::atomic_compare_exchange_strong_explicit(
          &geometry.collision_geometry_payload_, &cached, generated,
          std::memory_order_acq_rel, std::memory_order_acquire)) {
    return generated;
  }
  return cached;
}

std::shared_ptr<const MeshCollisionGeometryPayload>
mesh_collision_geometry_payload(const MeshAsset &mesh) {
  return mesh_collision_geometry_payload(mesh.geometry());
}

} // namespace quader
