////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/mesh_asset.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <quader/modeling/neutral/polygon/document_model.hpp>

namespace quader {
namespace mesh_asset_payload_detail {

inline bool same_vec2(QVec2 left, QVec2 right) {
  return left.x == right.x && left.y == right.y;
}

inline bool same_vec3(QVec3 left, QVec3 right) {
  return left.x == right.x && left.y == right.y && left.z == right.z;
}

inline bool same_vertex(const MeshVertex &left, const MeshVertex &right) {
  return same_vec3(left.position, right.position) &&
         same_vec3(left.normal, right.normal) && same_vec2(left.uv, right.uv) &&
         left.rgba == right.rgba &&
         left.source_vertex_id == right.source_vertex_id &&
         left.uv_carrier == right.uv_carrier &&
         same_vec3(left.uv_geometric_normal, right.uv_geometric_normal);
}

inline bool same_polygon_vertex(const quader_poly::Vertex &left,
                                const quader_poly::Vertex &right) {
  return left.id == right.id && same_vec3(left.position, right.position);
}

inline bool same_polygon_face(const quader_poly::Face &left,
                              const quader_poly::Face &right) {
  if (left.id != right.id || left.vertices != right.vertices ||
      left.material_slot != right.material_slot ||
      left.normal_shading != right.normal_shading ||
      !same_vec2(left.texture_transform.scale, right.texture_transform.scale) ||
      !same_vec2(left.texture_transform.shift_uv,
                 right.texture_transform.shift_uv) ||
      left.texture_transform.rotation_degrees !=
          right.texture_transform.rotation_degrees ||
      left.uvs.size() != right.uvs.size()) {
    return false;
  }
  for (std::size_t index = 0; index < left.uvs.size(); ++index) {
    if (!same_vec2(left.uvs[index], right.uvs[index])) {
      return false;
    }
  }
  return true;
}

inline bool same_polygon_document(const quader_poly::Document &left,
                                  const quader_poly::Document &right) {
  return left.vertices.size() == right.vertices.size() &&
         std::equal(left.vertices.begin(), left.vertices.end(),
                    right.vertices.begin(), same_polygon_vertex) &&
         left.faces.size() == right.faces.size() &&
         std::equal(left.faces.begin(), left.faces.end(), right.faces.begin(),
                    same_polygon_face) &&
         left.hard_normal_edges == right.hard_normal_edges &&
         left.soft_normal_edges == right.soft_normal_edges &&
         left.next_vertex_id == right.next_vertex_id &&
         left.next_face_id == right.next_face_id;
}

inline std::uint64_t append_authored_hash(std::uint64_t hash,
                                         std::uint64_t value) {
  constexpr std::uint64_t kPrime = 1'099'511'628'211ull;
  for (std::size_t offset = 0u; offset < sizeof(value); ++offset) {
    hash ^= static_cast<unsigned char>(value >> (offset * 8u));
    hash *= kPrime;
  }
  return hash;
}

inline std::uint64_t authored_float_hash_value(float value) {
  return value == 0.0f ? 0u : std::bit_cast<std::uint32_t>(value);
}

inline std::uint64_t
polygon_document_hash(const quader_poly::Document &document) {
  std::uint64_t hash = 14'695'981'039'346'656'037ull;
  hash = append_authored_hash(hash, document.vertices.size());
  for (const quader_poly::Vertex &vertex : document.vertices) {
    hash = append_authored_hash(hash, vertex.id);
    hash = append_authored_hash(
        hash, authored_float_hash_value(vertex.position.x));
    hash = append_authored_hash(
        hash, authored_float_hash_value(vertex.position.y));
    hash = append_authored_hash(
        hash, authored_float_hash_value(vertex.position.z));
  }
  hash = append_authored_hash(hash, document.faces.size());
  for (const quader_poly::Face &face : document.faces) {
    hash = append_authored_hash(hash, face.id);
    hash = append_authored_hash(hash, face.vertices.size());
    for (const quader_poly::ElementId vertex : face.vertices) {
      hash = append_authored_hash(hash, vertex);
    }
    hash = append_authored_hash(hash, face.uvs.size());
    for (const QVec2 uv : face.uvs) {
      hash = append_authored_hash(hash, authored_float_hash_value(uv.x));
      hash = append_authored_hash(hash, authored_float_hash_value(uv.y));
    }
    hash = append_authored_hash(hash, face.material_slot);
    hash = append_authored_hash(
        hash, static_cast<std::uint64_t>(face.normal_shading));
    hash = append_authored_hash(
        hash, authored_float_hash_value(face.texture_transform.scale.x));
    hash = append_authored_hash(
        hash, authored_float_hash_value(face.texture_transform.scale.y));
    hash = append_authored_hash(
        hash, authored_float_hash_value(face.texture_transform.shift_uv.x));
    hash = append_authored_hash(
        hash, authored_float_hash_value(face.texture_transform.shift_uv.y));
    hash = append_authored_hash(
        hash, authored_float_hash_value(face.texture_transform.rotation_degrees));
  }
  hash = append_authored_hash(hash, document.hard_normal_edges.size());
  for (const quader_poly::Edge edge : document.hard_normal_edges) {
    hash = append_authored_hash(hash, edge.a);
    hash = append_authored_hash(hash, edge.b);
  }
  hash = append_authored_hash(hash, document.soft_normal_edges.size());
  for (const quader_poly::Edge edge : document.soft_normal_edges) {
    hash = append_authored_hash(hash, edge.a);
    hash = append_authored_hash(hash, edge.b);
  }
  hash = append_authored_hash(hash, document.next_vertex_id);
  hash = append_authored_hash(hash, document.next_face_id);
  return hash;
}

inline bool same_authored_document(const MeshAsset &left,
                                   const MeshAsset &right) {
  if (left.has_authored_polygon_document !=
      right.has_authored_polygon_document) {
    return false;
  }
  if (!left.has_authored_polygon_document) {
    return true;
  }
  return left.authored_polygon_document && right.authored_polygon_document &&
         same_polygon_document(*left.authored_polygon_document,
                               *right.authored_polygon_document);
}

inline bool same_primitives(const MeshAsset &left, const MeshAsset &right) {
  const std::vector<MeshPrimitiveSpan> left_spans =
      normalized_mesh_primitive_spans(left);
  const std::vector<MeshPrimitiveSpan> right_spans =
      normalized_mesh_primitive_spans(right);
  return left_spans.size() == right_spans.size() &&
         std::equal(left_spans.begin(), left_spans.end(), right_spans.begin(),
                    [](const MeshPrimitiveSpan &left_span,
                       const MeshPrimitiveSpan &right_span) {
                      return left_span.index_offset == right_span.index_offset &&
                             left_span.index_count == right_span.index_count &&
                             left_span.material_slot == right_span.material_slot;
                    });
}

} // namespace mesh_asset_payload_detail

/** Compares compiled geometry independently of logical asset metadata. */
inline bool mesh_geometry_payload_equal(const MeshGeometryPayload &left,
                                        const MeshGeometryPayload &right) {
  using namespace mesh_asset_payload_detail;
  return left.vertices.size() == right.vertices.size() &&
         std::equal(left.vertices.begin(), left.vertices.end(),
                    right.vertices.begin(), same_vertex) &&
         left.indices == right.indices &&
         left.primitives == right.primitives;
}

/** Returns a deterministic in-process content hash for geometry interning. */
inline std::uint64_t
mesh_geometry_payload_hash(const MeshGeometryPayload &geometry) {
  return geometry.content_id().value;
}

/**
 * Returns an O(1) mesh snapshot. Compiled and authored topology are immutable
 * shared state; either payload detaches only through its explicit edit method.
 */
inline MeshAsset clone_mesh_asset_payload(const MeshAsset &source) {
  return source;
}

/**
 * Transfers a mesh payload into isolated ownership without copying its CPU
 * vertex and index buffers.
 */
inline MeshAsset isolate_mesh_asset_payload(MeshAsset source) {
  (void)source.geometry_payload();
  // A caller can assign a shared_ptr<Document> to the public const pointer and
  // retain a mutable alias. Publish an immutable snapshot at registry mutation
  // boundaries; the registry can still intern equal snapshots immediately so
  // retained assets/history share one canonical topology payload.
  if (source.authored_polygon_document) {
    source.authored_polygon_document =
        std::make_shared<const quader_poly::Document>(
            *source.authored_polygon_document);
  }
  return source;
}

/** Compares the authored content of two mesh assets, excluding store stamps. */
inline bool mesh_asset_payload_equal(const MeshAsset &left,
                                     const MeshAsset &right) {
  using namespace mesh_asset_payload_detail;
  return left.name == right.name &&
         mesh_geometry_payload_equal(left.geometry(), right.geometry()) &&
         same_primitives(left, right) &&
         left.has_stable_cpu_topology == right.has_stable_cpu_topology &&
         same_authored_document(left, right);
}

} // namespace quader
