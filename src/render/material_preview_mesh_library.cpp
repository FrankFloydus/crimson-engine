////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/material_preview_mesh_library.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <numbers>

namespace quader {
namespace {

constexpr float kVectorEpsilon = 0.000001f;
constexpr float kUnitCubeHalfExtent = 0.57735026919f;
constexpr std::uint32_t kSphereSegments = 48u;
constexpr std::uint32_t kSphereRings = 24u;
constexpr std::uint32_t kCylinderSegments = 48u;
constexpr std::uint32_t kRoundedBoxSubdivisions = 12u;

bool finite(QVec3 value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

QVec3 fallback_tangent(QVec3 normal) {
  const QVec3 reference =
      std::abs(normal.y) < 0.9f ? QVec3{0.0f, 1.0f, 0.0f}
                               : QVec3{1.0f, 0.0f, 0.0f};
  const QVec3 tangent = normalize_or_zero(cross(reference, normal));
  return length_squared(tangent) > kVectorEpsilon * kVectorEpsilon
             ? tangent
             : QVec3{1.0f, 0.0f, 0.0f};
}

void generate_tangent_frames(MaterialPreviewMeshData &mesh) {
  std::vector<QVec3> tangent_accumulators(mesh.vertices.size());
  std::vector<QVec3> bitangent_accumulators(mesh.vertices.size());

  for (std::size_t index = 0u; index + 2u < mesh.indices.size(); index += 3u) {
    const std::uint32_t index_a = mesh.indices[index];
    const std::uint32_t index_b = mesh.indices[index + 1u];
    const std::uint32_t index_c = mesh.indices[index + 2u];
    assert(index_a < mesh.vertices.size());
    assert(index_b < mesh.vertices.size());
    assert(index_c < mesh.vertices.size());

    const MaterialPreviewVertex &a = mesh.vertices[index_a];
    const MaterialPreviewVertex &b = mesh.vertices[index_b];
    const MaterialPreviewVertex &c = mesh.vertices[index_c];
    const QVec3 edge_ab = b.position - a.position;
    const QVec3 edge_ac = c.position - a.position;
    const QVec2 uv_ab = b.uv - a.uv;
    const QVec2 uv_ac = c.uv - a.uv;
    const float determinant = uv_ab.x * uv_ac.y - uv_ab.y * uv_ac.x;
    if (std::abs(determinant) <= kVectorEpsilon ||
        length_squared(cross(edge_ab, edge_ac)) <=
            kVectorEpsilon * kVectorEpsilon) {
      continue;
    }

    const float inverse_determinant = 1.0f / determinant;
    const QVec3 tangent =
        (edge_ab * uv_ac.y - edge_ac * uv_ab.y) * inverse_determinant;
    const QVec3 bitangent =
        (edge_ac * uv_ab.x - edge_ab * uv_ac.x) * inverse_determinant;
    if (!finite(tangent) || !finite(bitangent)) {
      continue;
    }
    tangent_accumulators[index_a] += tangent;
    tangent_accumulators[index_b] += tangent;
    tangent_accumulators[index_c] += tangent;
    bitangent_accumulators[index_a] += bitangent;
    bitangent_accumulators[index_b] += bitangent;
    bitangent_accumulators[index_c] += bitangent;
  }

  for (std::size_t index = 0u; index < mesh.vertices.size(); ++index) {
    MaterialPreviewVertex &vertex = mesh.vertices[index];
    vertex.normal = normalize_or_zero(vertex.normal);
    QVec3 tangent = tangent_accumulators[index] -
                    vertex.normal *
                        dot(vertex.normal, tangent_accumulators[index]);
    tangent = normalize_or_zero(tangent);
    if (!finite(tangent) ||
        length_squared(tangent) <= kVectorEpsilon * kVectorEpsilon) {
      tangent = fallback_tangent(vertex.normal);
    }

    QVec3 canonical_bitangent =
        normalize_or_zero(cross(vertex.normal, tangent));
    if (!finite(canonical_bitangent) ||
        length_squared(canonical_bitangent) <=
            kVectorEpsilon * kVectorEpsilon) {
      tangent = fallback_tangent(vertex.normal);
      canonical_bitangent =
          normalize_or_zero(cross(vertex.normal, tangent));
    }
    const float handedness =
        dot(canonical_bitangent, bitangent_accumulators[index]) < 0.0f
            ? -1.0f
            : 1.0f;
    vertex.tangent_frame = {
        .tangent = tangent,
        .bitangent = canonical_bitangent * handedness,
    };
  }
}

void append_triangle(MaterialPreviewMeshData &mesh, std::uint32_t a,
                     std::uint32_t b, std::uint32_t c) {
  mesh.indices.push_back(a);
  mesh.indices.push_back(b);
  mesh.indices.push_back(c);
}

void append_quad(MaterialPreviewMeshData &mesh, QVec3 p00, QVec3 p10,
                 QVec3 p11, QVec3 p01, QVec3 normal) {
  const std::uint32_t base = static_cast<std::uint32_t>(mesh.vertices.size());
  mesh.vertices.push_back({.position = p00, .normal = normal, .uv = {0.0f, 0.0f}});
  mesh.vertices.push_back({.position = p10, .normal = normal, .uv = {1.0f, 0.0f}});
  mesh.vertices.push_back({.position = p11, .normal = normal, .uv = {1.0f, 1.0f}});
  mesh.vertices.push_back({.position = p01, .normal = normal, .uv = {0.0f, 1.0f}});
  append_triangle(mesh, base, base + 1u, base + 2u);
  append_triangle(mesh, base, base + 2u, base + 3u);
}

MaterialPreviewMeshData build_sphere() {
  MaterialPreviewMeshData mesh;
  mesh.vertices.reserve(
      (kSphereRings + 1u) * (kSphereSegments + 1u));
  mesh.indices.reserve(kSphereSegments * (kSphereRings - 1u) * 6u);

  for (std::uint32_t ring = 0u; ring <= kSphereRings; ++ring) {
    const float v = static_cast<float>(ring) /
                    static_cast<float>(kSphereRings);
    const float theta = v * std::numbers::pi_v<float>;
    const float sin_theta = std::sin(theta);
    const float cos_theta = std::cos(theta);
    for (std::uint32_t segment = 0u; segment <= kSphereSegments; ++segment) {
      const float u = static_cast<float>(segment) /
                      static_cast<float>(kSphereSegments);
      const float phi = u * 2.0f * std::numbers::pi_v<float>;
      const QVec3 position{
          sin_theta * std::cos(phi),
          cos_theta,
          sin_theta * std::sin(phi),
      };
      mesh.vertices.push_back({
          .position = position,
          .normal = position,
          .uv = {u, v},
      });
    }
  }

  const std::uint32_t row_size = kSphereSegments + 1u;
  for (std::uint32_t ring = 0u; ring < kSphereRings; ++ring) {
    for (std::uint32_t segment = 0u; segment < kSphereSegments; ++segment) {
      const std::uint32_t a = ring * row_size + segment;
      const std::uint32_t a_next = a + 1u;
      const std::uint32_t b = a + row_size;
      const std::uint32_t b_next = b + 1u;
      if (ring > 0u) {
        append_triangle(mesh, a, a_next, b_next);
      }
      if (ring + 1u < kSphereRings) {
        append_triangle(mesh, a, b_next, b);
      }
    }
  }

  generate_tangent_frames(mesh);
  return mesh;
}

MaterialPreviewMeshData build_box() {
  MaterialPreviewMeshData mesh;
  mesh.vertices.reserve(24u);
  mesh.indices.reserve(36u);
  constexpr float kH = kUnitCubeHalfExtent;

  append_quad(mesh, {-kH, -kH, kH}, {kH, -kH, kH}, {kH, kH, kH},
              {-kH, kH, kH}, {0.0f, 0.0f, 1.0f});
  append_quad(mesh, {kH, -kH, -kH}, {-kH, -kH, -kH},
              {-kH, kH, -kH}, {kH, kH, -kH}, {0.0f, 0.0f, -1.0f});
  append_quad(mesh, {kH, -kH, kH}, {kH, -kH, -kH}, {kH, kH, -kH},
              {kH, kH, kH}, {1.0f, 0.0f, 0.0f});
  append_quad(mesh, {-kH, -kH, -kH}, {-kH, -kH, kH}, {-kH, kH, kH},
              {-kH, kH, -kH}, {-1.0f, 0.0f, 0.0f});
  append_quad(mesh, {-kH, kH, kH}, {kH, kH, kH}, {kH, kH, -kH},
              {-kH, kH, -kH}, {0.0f, 1.0f, 0.0f});
  append_quad(mesh, {-kH, -kH, -kH}, {kH, -kH, -kH}, {kH, -kH, kH},
              {-kH, -kH, kH}, {0.0f, -1.0f, 0.0f});

  generate_tangent_frames(mesh);
  return mesh;
}

QVec3 component_clamp(QVec3 value, float minimum, float maximum) {
  return {
      std::clamp(value.x, minimum, maximum),
      std::clamp(value.y, minimum, maximum),
      std::clamp(value.z, minimum, maximum),
  };
}

void append_rounded_box_face(MaterialPreviewMeshData &mesh, QVec3 normal,
                             QVec3 tangent, QVec3 bitangent) {
  constexpr float kOuterHalfExtent = kUnitCubeHalfExtent;
  constexpr float kCornerRadius = kOuterHalfExtent * 0.32f;
  constexpr float kInnerHalfExtent = kOuterHalfExtent - kCornerRadius;
  const std::uint32_t row_size = kRoundedBoxSubdivisions + 1u;
  const std::uint32_t base = static_cast<std::uint32_t>(mesh.vertices.size());

  for (std::uint32_t row = 0u; row <= kRoundedBoxSubdivisions; ++row) {
    const float v = static_cast<float>(row) /
                    static_cast<float>(kRoundedBoxSubdivisions);
    for (std::uint32_t column = 0u; column <= kRoundedBoxSubdivisions;
         ++column) {
      const float u = static_cast<float>(column) /
                      static_cast<float>(kRoundedBoxSubdivisions);
      const QVec3 cube_point =
          normal * kOuterHalfExtent +
          tangent * ((u * 2.0f - 1.0f) * kOuterHalfExtent) +
          bitangent * ((v * 2.0f - 1.0f) * kOuterHalfExtent);
      const QVec3 core_point = component_clamp(
          cube_point, -kInnerHalfExtent, kInnerHalfExtent);
      const QVec3 rounded_normal =
          normalize_or_zero(cube_point - core_point);
      mesh.vertices.push_back({
          .position = core_point + rounded_normal * kCornerRadius,
          .normal = rounded_normal,
          .uv = {u, v},
      });
    }
  }

  for (std::uint32_t row = 0u; row < kRoundedBoxSubdivisions; ++row) {
    for (std::uint32_t column = 0u; column < kRoundedBoxSubdivisions;
         ++column) {
      const std::uint32_t a = base + row * row_size + column;
      const std::uint32_t b = a + row_size;
      append_triangle(mesh, a, a + 1u, b + 1u);
      append_triangle(mesh, a, b + 1u, b);
    }
  }
}

MaterialPreviewMeshData build_rounded_box() {
  MaterialPreviewMeshData mesh;
  constexpr std::size_t kFaceVertexCount =
      (kRoundedBoxSubdivisions + 1u) * (kRoundedBoxSubdivisions + 1u);
  mesh.vertices.reserve(kFaceVertexCount * 6u);
  mesh.indices.reserve(kRoundedBoxSubdivisions *
                       kRoundedBoxSubdivisions * 6u * 6u);

  append_rounded_box_face(mesh, {0.0f, 0.0f, 1.0f},
                          {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
  append_rounded_box_face(mesh, {0.0f, 0.0f, -1.0f},
                          {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
  append_rounded_box_face(mesh, {1.0f, 0.0f, 0.0f},
                          {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f});
  append_rounded_box_face(mesh, {-1.0f, 0.0f, 0.0f},
                          {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f});
  append_rounded_box_face(mesh, {0.0f, 1.0f, 0.0f},
                          {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f});
  append_rounded_box_face(mesh, {0.0f, -1.0f, 0.0f},
                          {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f});

  generate_tangent_frames(mesh);
  return mesh;
}

MaterialPreviewMeshData build_cylinder() {
  MaterialPreviewMeshData mesh;
  constexpr float kRadius = 0.70710678118f;
  constexpr float kHalfHeight = kRadius;
  mesh.vertices.reserve((kCylinderSegments + 1u) * 4u + 2u);
  mesh.indices.reserve(kCylinderSegments * 12u);

  for (std::uint32_t segment = 0u; segment <= kCylinderSegments; ++segment) {
    const float u = static_cast<float>(segment) /
                    static_cast<float>(kCylinderSegments);
    const float angle = u * 2.0f * std::numbers::pi_v<float>;
    const float x = std::cos(angle) * kRadius;
    const float z = std::sin(angle) * kRadius;
    const QVec3 normal = normalize_or_zero({x, 0.0f, z});
    mesh.vertices.push_back({
        .position = {x, -kHalfHeight, z},
        .normal = normal,
        .uv = {u, 1.0f},
    });
    mesh.vertices.push_back({
        .position = {x, kHalfHeight, z},
        .normal = normal,
        .uv = {u, 0.0f},
    });
  }
  for (std::uint32_t segment = 0u; segment < kCylinderSegments; ++segment) {
    const std::uint32_t bottom = segment * 2u;
    const std::uint32_t top = bottom + 1u;
    const std::uint32_t next_bottom = bottom + 2u;
    const std::uint32_t next_top = bottom + 3u;
    append_triangle(mesh, bottom, top, next_bottom);
    append_triangle(mesh, next_bottom, top, next_top);
  }

  const std::uint32_t top_center =
      static_cast<std::uint32_t>(mesh.vertices.size());
  mesh.vertices.push_back({
      .position = {0.0f, kHalfHeight, 0.0f},
      .normal = {0.0f, 1.0f, 0.0f},
      .uv = {0.5f, 0.5f},
  });
  for (std::uint32_t segment = 0u; segment <= kCylinderSegments; ++segment) {
    const float angle = static_cast<float>(segment) /
                        static_cast<float>(kCylinderSegments) * 2.0f *
                        std::numbers::pi_v<float>;
    const float x = std::cos(angle) * kRadius;
    const float z = std::sin(angle) * kRadius;
    mesh.vertices.push_back({
        .position = {x, kHalfHeight, z},
        .normal = {0.0f, 1.0f, 0.0f},
        .uv = {0.5f + x / (2.0f * kRadius),
               0.5f - z / (2.0f * kRadius)},
    });
  }
  for (std::uint32_t segment = 0u; segment < kCylinderSegments; ++segment) {
    append_triangle(mesh, top_center, top_center + segment + 2u,
                    top_center + segment + 1u);
  }

  const std::uint32_t bottom_center =
      static_cast<std::uint32_t>(mesh.vertices.size());
  mesh.vertices.push_back({
      .position = {0.0f, -kHalfHeight, 0.0f},
      .normal = {0.0f, -1.0f, 0.0f},
      .uv = {0.5f, 0.5f},
  });
  for (std::uint32_t segment = 0u; segment <= kCylinderSegments; ++segment) {
    const float angle = static_cast<float>(segment) /
                        static_cast<float>(kCylinderSegments) * 2.0f *
                        std::numbers::pi_v<float>;
    const float x = std::cos(angle) * kRadius;
    const float z = std::sin(angle) * kRadius;
    mesh.vertices.push_back({
        .position = {x, -kHalfHeight, z},
        .normal = {0.0f, -1.0f, 0.0f},
        .uv = {0.5f + x / (2.0f * kRadius),
               0.5f + z / (2.0f * kRadius)},
    });
  }
  for (std::uint32_t segment = 0u; segment < kCylinderSegments; ++segment) {
    append_triangle(mesh, bottom_center, bottom_center + segment + 1u,
                    bottom_center + segment + 2u);
  }

  generate_tangent_frames(mesh);
  return mesh;
}

MaterialPreviewMeshData build_plane() {
  MaterialPreviewMeshData mesh;
  constexpr float kHalfExtent = 0.70710678118f;
  append_quad(mesh, {-kHalfExtent, -kHalfExtent, 0.0f},
              {kHalfExtent, -kHalfExtent, 0.0f},
              {kHalfExtent, kHalfExtent, 0.0f},
              {-kHalfExtent, kHalfExtent, 0.0f}, {0.0f, 0.0f, 1.0f});
  generate_tangent_frames(mesh);
  return mesh;
}

std::size_t mesh_index(MaterialPreviewMeshKind kind) noexcept {
  switch (kind) {
  case MaterialPreviewMeshKind::Sphere:
    return 0u;
  case MaterialPreviewMeshKind::Box:
    return 1u;
  case MaterialPreviewMeshKind::RoundedBox:
    return 2u;
  case MaterialPreviewMeshKind::Cylinder:
    return 3u;
  case MaterialPreviewMeshKind::Plane:
    return 4u;
  }
  return 0u;
}

} // namespace

MaterialPreviewMeshLibrary::MaterialPreviewMeshLibrary()
    : meshes_{build_sphere(), build_box(), build_rounded_box(),
              build_cylinder(), build_plane()} {}

const MaterialPreviewMeshData &MaterialPreviewMeshLibrary::mesh(
    MaterialPreviewMeshKind kind) const noexcept {
  return meshes_[mesh_index(kind)];
}

} // namespace quader
