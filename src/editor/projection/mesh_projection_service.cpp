////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/projection/mesh_projection_service.hpp"

#include "assets/mesh_asset_payload.hpp"
#include "diagnostics/profile.hpp"
#include "scene/scene_texture_mapping.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <quader/modeling/neutral/geometry/geometry.hpp>
#include <quader/modeling/neutral/polygon/document.hpp>
#include <quader/modeling/neutral/polygon/document_read_snapshot.hpp>
#include <quader/modeling/neutral/polygon/document_selection.hpp>
#include <quader/modeling/neutral/polygon/document_texture.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_bridge_surface_helpers.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_constants.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_mesh_internal.hpp>

namespace quader {

struct MeshProjectionPositionUpdatePlan {
  struct FaceRange {
    std::size_t face_index = 0U;
    std::size_t vertex_offset = 0U;
    bool dirty = false;
    std::vector<std::size_t> normal_corners;
  };

  std::shared_ptr<const quader_poly::Document> source_document;
  std::vector<std::uint8_t> movable_vertices;
  std::vector<FaceRange> faces;
  std::vector<MeshPrimitiveSpan> primitives;
  std::vector<std::uint32_t> compiled_source_ids;
  std::size_t index_count = 0U;
  bool eligible = false;
};

struct MeshRetainedPositionProjectionPlan {
  explicit MeshRetainedPositionProjectionPlan(
      modeling::PolygonDocumentReadSnapshot snapshot)
      : source(std::move(snapshot)) {}

  modeling::PolygonDocumentReadSnapshot source;
  std::shared_ptr<const MeshGeometryPayload> source_geometry;
  std::vector<std::size_t> compiled_vertex_sources;
  std::vector<std::size_t> face_vertex_offsets;
  std::vector<MeshProjectionPositionUpdatePlan::FaceRange> partial_ranges;
  std::vector<quader_poly::Face> partial_faces;
  std::vector<std::uint8_t> movable_vertices;
  std::vector<std::vector<std::size_t>> source_compiled_vertices;
  std::vector<std::vector<std::size_t>> source_faces;
  std::vector<std::vector<std::array<std::uint32_t, 3>>> face_triangle_identities;
  bool flat_faces = false;

  /** Private work arrays are never pinned by an accepted publication. */
  struct CoordinateWorkspace {
    explicit CoordinateWorkspace(std::span<const quader_poly::Vertex> source)
        : next_vertices(source.begin(), source.end()),
          previous_vertices(source.begin(), source.end()),
          next_lookup(next_vertices), previous_lookup(previous_vertices) {}

    std::vector<quader_poly::Vertex> next_vertices;
    std::vector<quader_poly::Vertex> previous_vertices;
    quader_poly::document_internal::VertexLookupIndex next_lookup;
    quader_poly::document_internal::VertexLookupIndex previous_lookup;
    std::vector<std::size_t> update_sources;
    std::vector<std::uint8_t> dirty_face_flags;
    std::vector<std::size_t> dirty_faces;
    std::vector<std::size_t> preserved_uv_faces;
    std::vector<QVec3> face_positions;
    std::vector<std::array<std::uint32_t, 3>> triangle_identities;
    quader_poly::CompiledMesh compilation;
    std::vector<std::weak_ptr<const std::vector<QVec3>>> position_buffers;
    bool bound = false;
    bool partial_eligible = false;
  };
  mutable std::mutex coordinate_mutex;
  mutable std::unique_ptr<CoordinateWorkspace> coordinate_workspace;
};

namespace {

std::atomic<std::uint64_t> retained_translation_count{0};
std::atomic<std::uint64_t> retained_flat_face_count{0};
std::atomic<std::uint64_t> retained_partial_count{0};
std::atomic<std::uint64_t> partial_compiled_face_count{0};
std::atomic<std::uint64_t> full_compile_count{0};
std::atomic<std::uint64_t> partial_plan_build_count{0};
std::atomic<std::uint64_t> partial_plan_face_visit_count{0};
std::atomic<std::uint64_t> partial_plan_reuse_count{0};
std::atomic<std::uint64_t> partial_plan_fallback_count{0};
std::atomic<std::uint64_t> retained_stream_projection_count{0};
std::atomic<std::uint64_t> retained_stream_fallback_count{0};

QVec3 from_runtime(quader::QVec3 value) { return {value.x, value.y, value.z}; }

quader_poly::CompiledMesh &canonical_compilation_scratch();
bool same_topology_layout(const quader_poly::Document &left,
                          const quader_poly::Document &right);

MeshVertex from_compiled_vertex(const quader_poly::CompiledVertex &vertex) {
  return MeshVertex{
      .position = from_runtime(vertex.position),
      .normal = from_runtime(vertex.normal),
      .uv = {vertex.uv0.x, vertex.uv0.y},
      .rgba = vertex.color,
      .source_vertex_id = vertex.source_vertex_id,
      .uv_carrier = vertex.uv_carrier,
      .uv_geometric_normal = vertex.uv_geometric_normal};
}

void replace_geometry_from_compiled(
    const quader_poly::CompiledMesh &compiled,
    MeshGeometryPayload &geometry);

bool same_position(quader::QVec3 left, quader::QVec3 right) {
  return left.x == right.x && left.y == right.y && left.z == right.z;
}

/** Checks stored attributes before attempting optional texture preservation. */
bool valid_rebase_face(const quader_poly::Face &face,
                       const quader_poly::document_internal::VertexLookupIndex &vertices) {
  if (face.vertices.size() < 3U ||
      (!face.uvs.empty() && face.uvs.size() != face.vertices.size()) ||
      !quader_poly::valid_face_texture_transform(face.texture_transform)) {
    return false;
  }
  for (const auto id : face.vertices) {
    const auto *vertex = vertices.find(id);
    if (vertex == nullptr || !std::isfinite(vertex->position.x) ||
        !std::isfinite(vertex->position.y) || !std::isfinite(vertex->position.z)) {
      return false;
    }
  }
  return std::ranges::all_of(face.uvs, [](QVec2 uv) {
    return std::isfinite(uv.x) && std::isfinite(uv.y);
  });
}

/** Bounds the exceptional identity check when an n-gon changes loop anchors. */
bool same_rebase_corner_ids(const quader_poly::Face &source,
                            const quader_poly::Face &destination) {
  if (source.vertices == destination.vertices) return true;
  auto source_ids = source.vertices;
  auto destination_ids = destination.vertices;
  std::ranges::sort(source_ids);
  std::ranges::sort(destination_ids);
  return source_ids == destination_ids;
}

double triangle_orientation(quader::QVec3 a, quader::QVec3 b,
                            quader::QVec3 c, quader::QVec3 normal) {
  const auto area = quader_geometry::triangle_area_vector(
      quader_geometry::QTriangle3<double>{
          {a.x, a.y, a.z}, {b.x, b.y, b.z}, {c.x, c.y, c.z}});
  return quader_geometry::dot(
      area, quader_geometry::QVec3d{normal.x, normal.y, normal.z});
}

/** Rejects rounded near-translations instead of assuming rigid geometry. */
template <typename DocumentView>
bool is_exact_document_translation(const DocumentView &previous,
                                   const DocumentView &next) {
  if (previous.vertices.empty() ||
      previous.vertices.size() != next.vertices.size()) {
    return false;
  }
  const auto difference = [](QVec3 left, QVec3 right) {
    return std::array<double, 3>{
        static_cast<double>(left.x) - right.x,
        static_cast<double>(left.y) - right.y,
        static_cast<double>(left.z) - right.z};
  };
  const auto translation = difference(next.vertices.front().position,
                                      previous.vertices.front().position);
  if (!std::ranges::all_of(translation,
                           [](double value) { return std::isfinite(value); })) {
    return false;
  }
  for (std::size_t index = 0; index < next.vertices.size(); ++index) {
    if (difference(next.vertices[index].position,
                   previous.vertices[index].position) != translation ||
        difference(next.vertices[index].position,
                   next.vertices.front().position) !=
            difference(previous.vertices[index].position,
                       previous.vertices.front().position)) {
      return false;
    }
  }
  return true;
}

/** Guarantees that the compiler never excludes this face from smoothing. */
bool has_valid_normal_fallback(
    const quader_poly::document_internal::VertexLookupIndex &vertex_lookup,
    const quader_poly::Face &face) {
  if (face.vertices.size() < 3U) return false;
  const auto *a = vertex_lookup.find(face.vertices[0]);
  const auto *b = vertex_lookup.find(face.vertices[1]);
  const auto *c = vertex_lookup.find(face.vertices[2]);
  if (a == nullptr || b == nullptr || c == nullptr) return false;
  const auto normal = quader_geometry::triangle_unit_normal(
      quader_geometry::QTriangle3<float>{
          {a->position.x, a->position.y, a->position.z},
          {b->position.x, b->position.y, b->position.z},
          {c->position.x, c->position.y, c->position.z}});
  return std::isfinite(normal.x) && std::isfinite(normal.y) &&
         std::isfinite(normal.z) &&
         quader_geometry::length_squared(normal) >
             quader_poly::document_internal::kEpsilon;
}

/** Detaches only genuinely changed attribute/index streams from their pins. */
class RetainedProjectionWriter final {
public:
  RetainedProjectionWriter(
      std::shared_ptr<const MeshGeometryPayload> source,
      const std::shared_ptr<const MeshGeometryProjection> &previous,
      std::shared_ptr<const std::vector<QVec3>> positions)
      : projection_(std::make_shared<MeshGeometryProjection>()) {
    projection_->source_geometry = std::move(source);
    projection_->positions = std::move(positions);
    if (previous) {
      projection_->vertex_attributes = previous->vertex_attributes;
      projection_->triangle_indices = previous->triangle_indices;
    }
  }

  [[nodiscard]] const MeshVertex &vertex(std::size_t index) const {
    return projection_->vertices()[index];
  }

  void set_vertex(std::size_t index, const MeshVertex &value) {
    const auto &current = vertex(index);
    if (same_position(current.normal, value.normal) &&
        current.uv.x == value.uv.x && current.uv.y == value.uv.y &&
        current.rgba == value.rgba &&
        current.source_vertex_id == value.source_vertex_id &&
        current.uv_carrier == value.uv_carrier &&
        same_position(current.uv_geometric_normal, value.uv_geometric_normal)) {
      return;
    }
    if (!vertices_) {
      const auto source = projection_->vertices();
      vertices_ = std::make_shared<std::vector<MeshVertex>>(source.begin(), source.end());
      projection_->vertex_attributes = vertices_;
    }
    (*vertices_)[index] = value;
  }

  void set_index(std::size_t index, std::uint32_t value) {
    if (projection_->indices()[index] == value) return;
    if (!indices_) {
      const auto source = projection_->indices();
      indices_ = std::make_shared<std::vector<std::uint32_t>>(source.begin(), source.end());
      projection_->triangle_indices = indices_;
    }
    (*indices_)[index] = value;
  }

  [[nodiscard]] std::shared_ptr<const MeshGeometryProjection> finish() const {
    return projection_;
  }

private:
  std::shared_ptr<MeshGeometryProjection> projection_;
  std::shared_ptr<std::vector<MeshVertex>> vertices_;
  std::shared_ptr<std::vector<std::uint32_t>> indices_;
};

/** Uses prepared face/corner ranges and canonical math over SDK-owned vertices. */
bool update_retained_projection_face_ranges(
    quader_poly::DocumentReadView previous, quader_poly::DocumentReadView next,
    const MeshRetainedPositionProjectionPlan &plan,
    RetainedProjectionWriter &writer, bool preserve_corner_normals,
    const quader_poly::document_internal::VertexLookupIndex &previous_lookup,
    const quader_poly::document_internal::VertexLookupIndex &next_lookup,
    std::vector<QVec3> &face_positions,
    std::span<const std::size_t> affected_faces = {},
    bool affected_only = false) {
  using namespace quader_poly::document_internal;
  const std::size_t face_count = affected_only ? affected_faces.size() : next.faces.size();
  for (std::size_t index = 0U; index < face_count; ++index) {
    const std::size_t face_index = affected_only ? affected_faces[index] : index;
    const auto &face = next.faces[face_index];
    const auto offset = plan.face_vertex_offsets[face_index];
    const auto &primitive = plan.source_geometry->primitives[face_index];
    if (preserve_corner_normals &&
        (!has_valid_normal_fallback(previous_lookup, face) ||
         !has_valid_normal_fallback(next_lookup, face))) return false;
    bool changed = false;
    face_positions.clear();
    face_positions.reserve(face.vertices.size());
    for (std::size_t corner = 0U; corner < face.vertices.size(); ++corner) {
      const auto source_index = plan.compiled_vertex_sources[offset + corner];
      const auto position = next.vertices[source_index].position;
      changed = changed || !same_position(position, previous.vertices[source_index].position);
      face_positions.push_back(position);
    }
    if (!changed) continue;
    const auto normal = face_normal_from_vertex_lookup(face, next_lookup);
    if (preserve_corner_normals &&
        !same_position(normal, writer.vertex(offset).uv_geometric_normal)) return false;
    const auto triangles = triangulate_face_local_indices(face, next_lookup, normal);
    if (triangles.empty() || triangles.size() * 3U != primitive.index_count) return false;
    const bool loop_uvs = face_has_loop_uvs(face);
    const auto uv_basis = loop_uvs ? FaceUvBasis{}
        : generated_face_uv_basis(face, normal, next_lookup);
    const auto dropped_axis = dropped_axis_for_normal(normal);
    const auto carrier = quader_poly::face_texture_carrier(
        loop_uvs ? std::span<const QVec3>(face_positions) : std::span<const QVec3>{},
        loop_uvs ? std::span<const QVec2>(face.uvs) : std::span<const QVec2>{},
        normal, face.texture_transform);
    for (std::size_t corner = 0U; corner < face.vertices.size(); ++corner) {
      const auto uv = loop_uvs ? face.uvs[corner]
          : generated_uv_for_position(face_positions[corner], uv_basis, dropped_axis);
      writer.set_vertex(offset + corner, MeshVertex{
          .position = face_positions[corner],
          .normal = preserve_corner_normals ? writer.vertex(offset + corner).normal : normal,
          .uv = quader_poly::apply_face_texture_transform(uv, face.texture_transform),
          .rgba = 0xff9f9f9fU, .source_vertex_id = face.vertices[corner],
          .uv_carrier = carrier, .uv_geometric_normal = normal});
    }
    std::size_t index_offset = primitive.index_offset;
    for (const auto &triangle : triangles) {
      writer.set_index(index_offset++, static_cast<std::uint32_t>(offset + triangle.a));
      const bool forward = triangle_orientation(face_positions[triangle.a],
          face_positions[triangle.b], face_positions[triangle.c], normal) >= 0.0;
      writer.set_index(index_offset++, static_cast<std::uint32_t>(offset +
          (forward ? triangle.b : triangle.c)));
      writer.set_index(index_offset++, static_cast<std::uint32_t>(offset +
          (forward ? triangle.c : triangle.b)));
    }
  }
  return true;
}

bool update_retained_projection_faces(
    quader_poly::DocumentReadView previous, quader_poly::DocumentReadView next,
    const MeshRetainedPositionProjectionPlan &plan,
    RetainedProjectionWriter &writer, bool preserve_corner_normals) {
  const quader_poly::document_internal::VertexLookupIndex previous_lookup(previous.vertices);
  const quader_poly::document_internal::VertexLookupIndex next_lookup(next.vertices);
  std::vector<QVec3> face_positions;
  return update_retained_projection_face_ranges(previous, next, plan, writer,
      preserve_corner_normals, previous_lookup, next_lookup, face_positions);
}

/** Copies canonical compiler output into the retained range of each dirty face. */
bool update_retained_projection_compilation(
    quader_poly::DocumentReadView next,
    const MeshRetainedPositionProjectionPlan &plan,
    RetainedProjectionWriter &writer, bool partial,
    quader_poly::CompiledMesh *scratch = nullptr) {
  auto input = next;
  if (partial) input.faces = plan.partial_faces;
  auto &compiled = scratch != nullptr ? *scratch : canonical_compilation_scratch();
  quader_poly::compile_document_into(input, compiled);
  if (compiled.primitives.size() != input.faces.size()) return false;
  std::size_t vertex_offset = 0U;
  for (std::size_t face_index = 0U; face_index < input.faces.size(); ++face_index) {
    const auto &face = input.faces[face_index];
    const MeshProjectionPositionUpdatePlan::FaceRange complete_range{
        .face_index = face_index, .vertex_offset = partial ? 0U : plan.face_vertex_offsets[face_index],
        .dirty = true};
    const auto &range = partial ? plan.partial_ranges[face_index] : complete_range;
    const auto &expected = plan.source_geometry->primitives[range.face_index];
    const auto &actual = compiled.primitives[face_index];
    if (actual.index_count != expected.index_count ||
        actual.material_slot != expected.material_slot ||
        vertex_offset + face.vertices.size() > compiled.vertices.size()) return false;
    for (std::size_t corner = 0U; corner < face.vertices.size(); ++corner) {
      const auto &vertex = compiled.vertices[vertex_offset + corner];
      if (vertex.source_vertex_id != face.vertices[corner]) return false;
      if (range.dirty) writer.set_vertex(range.vertex_offset + corner, from_compiled_vertex(vertex));
    }
    if (range.dirty) {
      for (std::size_t index = 0U; index < actual.index_count; ++index) {
        const auto compiled_index = compiled.indices[actual.index_offset + index];
        if (compiled_index < vertex_offset ||
            compiled_index >= vertex_offset + face.vertices.size()) return false;
        writer.set_index(expected.index_offset + index,
            static_cast<std::uint32_t>(range.vertex_offset + compiled_index - vertex_offset));
      }
    } else {
      for (const auto corner : range.normal_corners) {
        auto vertex = writer.vertex(range.vertex_offset + corner);
        vertex.normal = compiled.vertices[vertex_offset + corner].normal;
        writer.set_vertex(range.vertex_offset + corner, vertex);
      }
    }
    vertex_offset += face.vertices.size();
  }
  return vertex_offset == compiled.vertices.size();
}

/**
 * Applies a topology-retained transform directly to the already compiled
 * face ranges. Exact translations may also retain smooth corner normals when
 * every canonical face normal and its smoothing participation are unchanged.
 * Changed faces still receive canonical UVs, triangulation and winding.
 */
bool try_update_retained_document_mesh_positions(
    const quader_poly::Document &previous,
    const quader_poly::Document &next, MeshAsset &projected,
    bool preserve_corner_normals) {
  QDR_PROFILE_SCOPE("Mesh projection retained face update");
  using quader_poly::document_internal::Triangle;

  if (previous.faces.size() != next.faces.size()) {
    return false;
  }

  MeshGeometryPayload &geometry = projected.edit_geometry();
  if (geometry.primitives.size() != next.faces.size()) {
    return false;
  }

  const quader_poly::document_internal::VertexLookupIndex previous_lookup(previous);
  const quader_poly::document_internal::VertexLookupIndex next_lookup(next);
  std::vector<QVec3> face_positions;
  std::size_t vertex_offset = 0U;
  std::size_t index_offset = 0U;
  for (std::size_t face_index = 0U; face_index < next.faces.size();
       ++face_index) {
    const quader_poly::Face &previous_face = previous.faces[face_index];
    const quader_poly::Face &face = next.faces[face_index];
    if (previous_face.id != face.id ||
        previous_face.vertices != face.vertices ||
        vertex_offset + face.vertices.size() > geometry.vertices.size()) {
      return false;
    }

    if (preserve_corner_normals &&
        (!has_valid_normal_fallback(previous_lookup, previous_face) ||
         !has_valid_normal_fallback(next_lookup, face))) {
      return false;
    }

    MeshPrimitiveSpan &primitive = geometry.primitives[face_index];
    if (primitive.index_offset != index_offset ||
        primitive.material_slot != face.material_slot ||
        index_offset + primitive.index_count > geometry.indices.size()) {
      return false;
    }

    bool face_changed = false;
    face_positions.clear();
    face_positions.reserve(face.vertices.size());
    for (std::size_t local_index = 0U;
         local_index < face.vertices.size(); ++local_index) {
      const quader_poly::ElementId vertex_id = face.vertices[local_index];
      const quader_poly::Vertex *before = previous_lookup.find(vertex_id);
      const quader_poly::Vertex *after = next_lookup.find(vertex_id);
      if (before == nullptr || after == nullptr ||
          geometry.vertices[vertex_offset + local_index].source_vertex_id !=
              vertex_id) {
        return false;
      }
      face_changed = face_changed ||
                     !same_position(before->position, after->position);
      face_positions.push_back(after->position);
    }

    if (face_changed) {
      QDR_PROFILE_SCOPE("Mesh projection retained changed face");
      const quader::QVec3 normal = quader_poly::face_normal(next, face);
      if (preserve_corner_normals &&
          !same_position(normal,
                         geometry.vertices[vertex_offset].uv_geometric_normal)) {
        return false;
      }
      const std::vector<Triangle> triangles =
          quader_poly::document_internal::triangulate_face_local_indices(
              next, face, next_lookup, normal);
      if (triangles.empty() ||
          triangles.size() * 3U != primitive.index_count) {
        return false;
      }
      const int dropped_axis =
          quader_poly::document_internal::dropped_axis_for_normal(normal);
      const bool use_loop_uvs =
          quader_poly::document_internal::face_has_loop_uvs(face);
      const quader_poly::document_internal::FaceUvBasis uv_basis =
          use_loop_uvs
              ? quader_poly::document_internal::FaceUvBasis{}
              : quader_poly::document_internal::generated_face_uv_basis(
                    next, face, normal, next_lookup);
      const auto uv_carrier = quader_poly::face_texture_carrier(
          use_loop_uvs ? std::span<const QVec3>(face_positions)
                       : std::span<const QVec3>{},
          use_loop_uvs ? std::span<const QVec2>(face.uvs)
                       : std::span<const QVec2>{},
          normal, face.texture_transform);
      for (std::size_t local_index = 0U;
           local_index < face.vertices.size(); ++local_index) {
        const quader_poly::ElementId vertex_id = face.vertices[local_index];
        const QVec3 position = face_positions[local_index];
        const QVec2 base_uv =
            use_loop_uvs
                ? face.uvs[local_index]
                : quader_poly::document_internal::generated_uv_for_position(
                      position, uv_basis, dropped_axis);
        const QVec3 corner_normal = preserve_corner_normals
            ? geometry.vertices[vertex_offset + local_index].normal : normal;
        geometry.vertices[vertex_offset + local_index] = MeshVertex{
            .position = position,
            .normal = corner_normal,
            .uv = quader_poly::apply_face_texture_transform(
                base_uv, face.texture_transform),
            .rgba = 0xff9f9f9fU,
            .source_vertex_id = vertex_id,
            .uv_carrier = uv_carrier,
            .uv_geometric_normal = normal,
        };
      }

      std::size_t write_index = index_offset;
      for (const Triangle &triangle : triangles) {
        geometry.indices[write_index++] =
            static_cast<std::uint32_t>(vertex_offset + triangle.a);
        if (triangle_orientation(face_positions[triangle.a],
                                 face_positions[triangle.b],
                                 face_positions[triangle.c], normal) >= 0.0) {
          geometry.indices[write_index++] =
              static_cast<std::uint32_t>(vertex_offset + triangle.b);
          geometry.indices[write_index++] =
              static_cast<std::uint32_t>(vertex_offset + triangle.c);
        } else {
          geometry.indices[write_index++] =
              static_cast<std::uint32_t>(vertex_offset + triangle.c);
          geometry.indices[write_index++] =
              static_cast<std::uint32_t>(vertex_offset + triangle.b);
        }
      }
    }

    vertex_offset += face.vertices.size();
    index_offset += primitive.index_count;
  }

  return vertex_offset == geometry.vertices.size() &&
         index_offset == geometry.indices.size();
}

/**
 * Compiles the complete face neighborhood of affected normal corners through
 * the canonical owner. A fan connects only corners with the same authored
 * vertex ID, so every face incident to a dirty face's vertices is sufficient;
 * flat and collapsed faces remain present for their normal/edge contribution.
 */
bool try_update_prepared_document_mesh_positions(
    const quader_poly::Document &next, MeshAsset &projected,
    const MeshProjectionPositionUpdatePlan &plan) {
  QDR_PROFILE_SCOPE("Mesh projection retained partial face update");
  if (!plan.eligible || !plan.source_document ||
      !same_topology_layout(*plan.source_document, next)) {
    return false;
  }
  const auto &previous = *projected.authored_polygon_document;
  for (std::size_t index = 0U; index < next.vertices.size(); ++index) {
    if (!plan.movable_vertices[index] &&
        (!same_position(next.vertices[index].position,
                        plan.source_document->vertices[index].position) ||
         !same_position(previous.vertices[index].position,
                        plan.source_document->vertices[index].position))) {
      return false;
    }
  }
  const MeshGeometryPayload &current_geometry = projected.geometry();
  if (current_geometry.primitives.size() != plan.primitives.size() ||
      current_geometry.vertices.size() != plan.compiled_source_ids.size() ||
      current_geometry.indices.size() != plan.index_count) {
    return false;
  }
  for (std::size_t index = 0U; index < plan.primitives.size(); ++index) {
    const auto &actual = current_geometry.primitives[index];
    const auto &expected = plan.primitives[index];
    if (actual.index_offset != expected.index_offset ||
        actual.index_count != expected.index_count ||
        actual.material_slot != expected.material_slot) {
      return false;
    }
  }
  for (std::size_t index = 0U; index < plan.compiled_source_ids.size(); ++index) {
    if (current_geometry.vertices[index].source_vertex_id !=
        plan.compiled_source_ids[index]) {
      return false;
    }
  }

  // This is an immutable compiler input, not another editable document. Keep
  // the full vertex table and authored edge policy so ID resolution and local
  // non-manifold incidence match the authoritative source exactly.
  quader_poly::Document neighborhood;
  neighborhood.vertices = next.vertices;
  neighborhood.next_vertex_id = next.next_vertex_id;
  neighborhood.next_face_id = next.next_face_id;
  neighborhood.hard_normal_edges = next.hard_normal_edges;
  neighborhood.soft_normal_edges = next.soft_normal_edges;
  neighborhood.faces.reserve(plan.faces.size());
  for (const auto &range : plan.faces) {
    neighborhood.faces.push_back(next.faces[range.face_index]);
  }
  quader_poly::CompiledMesh &compiled = canonical_compilation_scratch();
  {
    QDR_PROFILE_SCOPE("Mesh projection compile affected normal neighborhood");
    quader_poly::compile_document_into(neighborhood, compiled);
  }
  partial_compiled_face_count.fetch_add(plan.faces.size(),
                                        std::memory_order_relaxed);
  if (compiled.primitives.size() != plan.faces.size()) return false;

  MeshGeometryPayload &geometry = projected.edit_geometry();
  std::size_t compiled_vertex_offset = 0U;
  for (std::size_t index = 0U; index < plan.faces.size(); ++index) {
    const auto &range = plan.faces[index];
    const auto &face = next.faces[range.face_index];
    const auto &primitive = plan.primitives[range.face_index];
    const auto &compiled_primitive = compiled.primitives[index];
    if (compiled_vertex_offset + face.vertices.size() > compiled.vertices.size() ||
        compiled_primitive.index_count != primitive.index_count ||
        compiled_primitive.material_slot != primitive.material_slot ||
        static_cast<std::size_t>(compiled_primitive.index_offset) +
                compiled_primitive.index_count > compiled.indices.size()) {
      return false;
    }
    for (std::size_t corner = 0U; corner < face.vertices.size(); ++corner) {
      const auto &compiled_vertex = compiled.vertices[compiled_vertex_offset + corner];
      if (compiled_vertex.source_vertex_id != face.vertices[corner]) {
        return false;
      }
      if (range.dirty) {
        geometry.vertices[range.vertex_offset + corner] =
            from_compiled_vertex(compiled_vertex);
      }
    }
    if (range.dirty) {
      for (std::size_t triangle_index = 0U; triangle_index < primitive.index_count;
           ++triangle_index) {
        const std::uint32_t compiled_index =
            compiled.indices[compiled_primitive.index_offset + triangle_index];
        if (compiled_index < compiled_vertex_offset ||
            compiled_index >= compiled_vertex_offset + face.vertices.size()) {
          return false;
        }
        geometry.indices[primitive.index_offset + triangle_index] =
            static_cast<std::uint32_t>(range.vertex_offset + compiled_index -
                                       compiled_vertex_offset);
      }
    } else {
      for (const std::size_t corner : range.normal_corners) {
        geometry.vertices[range.vertex_offset + corner].normal =
            from_runtime(compiled.vertices[compiled_vertex_offset + corner].normal);
      }
    }
    compiled_vertex_offset += face.vertices.size();
  }
  return compiled_vertex_offset == compiled.vertices.size();
}

bool try_update_partial_document_mesh_positions(
    const quader_poly::Document &previous,
    const quader_poly::Document &next, MeshAsset &projected) {
  std::vector<std::uint32_t> changed_vertices;
  for (std::size_t index = 0U; index < next.vertices.size(); ++index) {
    if (!same_position(previous.vertices[index].position,
                       next.vertices[index].position)) {
      changed_vertices.push_back(next.vertices[index].id);
    }
  }
  if (changed_vertices.empty()) return false;
  const auto plan = MeshProjectionService::prepare_position_update_plan(
      projected, changed_vertices);
  return plan && try_update_prepared_document_mesh_positions(next, projected, *plan);
}

MeshAsset
mesh_asset_from_compiled_mesh(const quader_poly::CompiledMesh &compiled) {
  MeshAsset mesh;
  mesh.name = "Mesh";
  replace_geometry_from_compiled(compiled, mesh.edit_geometry());
  mesh.has_stable_cpu_topology = true;
  return mesh;
}

quader::modeling::AuthoredSurfaceShading
to_sdk_shading(quader_poly::SurfaceShading shading) {
  switch (shading) {
  case quader_poly::SurfaceShading::Flat:
    return quader::modeling::AuthoredSurfaceShading::Flat;
  case quader_poly::SurfaceShading::Smooth:
    return quader::modeling::AuthoredSurfaceShading::Smooth;
  case quader_poly::SurfaceShading::Authored:
    return quader::modeling::AuthoredSurfaceShading::Authored;
  }
  return quader::modeling::AuthoredSurfaceShading::Flat;
}

quader_poly::SurfaceShading
to_native_shading(quader::modeling::AuthoredSurfaceShading shading) {
  switch (shading) {
  case quader::modeling::AuthoredSurfaceShading::Flat:
    return quader_poly::SurfaceShading::Flat;
  case quader::modeling::AuthoredSurfaceShading::Smooth:
    return quader_poly::SurfaceShading::Smooth;
  case quader::modeling::AuthoredSurfaceShading::Authored:
    return quader_poly::SurfaceShading::Authored;
  }
  return quader_poly::SurfaceShading::Flat;
}

quader::modeling::AuthoredTopology
to_sdk_topology(const quader_poly::Document &document) {
  quader::modeling::AuthoredTopology topology;
  topology.next_vertex_id = document.next_vertex_id;
  topology.next_face_id = document.next_face_id;
  topology.vertices.reserve(document.vertices.size());
  for (const quader_poly::Vertex &vertex : document.vertices) {
    topology.vertices.push_back({
        .id = quader::modeling::make_id<quader::modeling::VertexTag>(vertex.id),
        .position = {vertex.position.x, vertex.position.y, vertex.position.z},
    });
  }
  topology.faces.reserve(document.faces.size());
  for (const quader_poly::Face &face : document.faces) {
    quader::modeling::AuthoredTopologyFace converted;
    converted.id =
        quader::modeling::make_id<quader::modeling::FaceTag>(face.id);
    converted.material_slot = face.material_slot;
    converted.shading = to_sdk_shading(face.normal_shading);
    converted.texture_transform = {
        .scale = {face.texture_transform.scale.x,
                  face.texture_transform.scale.y},
        .shift_uv = {face.texture_transform.shift_uv.x,
                     face.texture_transform.shift_uv.y},
        .rotation_degrees = face.texture_transform.rotation_degrees,
    };
    converted.vertices.reserve(face.vertices.size());
    for (const quader_poly::ElementId vertex : face.vertices) {
      converted.vertices.push_back(
          quader::modeling::make_id<quader::modeling::VertexTag>(vertex));
    }
    converted.uvs.reserve(face.uvs.size());
    for (const QVec2 uv : face.uvs) {
      converted.uvs.push_back({uv.x, uv.y});
    }
    topology.faces.push_back(std::move(converted));
  }
  topology.hard_normal_edges.reserve(document.hard_normal_edges.size());
  for (const quader_poly::Edge edge : document.hard_normal_edges) {
    topology.hard_normal_edges.push_back(quader::modeling::make_edge_key(
        quader::modeling::make_id<quader::modeling::VertexTag>(edge.a),
        quader::modeling::make_id<quader::modeling::VertexTag>(edge.b)));
  }
  topology.soft_normal_edges.reserve(document.soft_normal_edges.size());
  for (const quader_poly::Edge edge : document.soft_normal_edges) {
    topology.soft_normal_edges.push_back(quader::modeling::make_edge_key(
        quader::modeling::make_id<quader::modeling::VertexTag>(edge.a),
        quader::modeling::make_id<quader::modeling::VertexTag>(edge.b)));
  }
  return topology;
}

quader_poly::Document
to_native_topology(const quader::modeling::AuthoredTopology &topology) {
  quader_poly::Document document;
  document.next_vertex_id = topology.next_vertex_id;
  document.next_face_id = topology.next_face_id;
  document.vertices.reserve(topology.vertices.size());
  for (const quader::modeling::AuthoredTopologyVertex &vertex :
       topology.vertices) {
    document.vertices.push_back({
        .id = vertex.id.index,
        .position = {vertex.position.x, vertex.position.y, vertex.position.z},
    });
  }
  document.faces.reserve(topology.faces.size());
  for (const quader::modeling::AuthoredTopologyFace &face : topology.faces) {
    quader_poly::Face converted;
    converted.id = face.id.index;
    converted.material_slot = face.material_slot;
    converted.normal_shading = to_native_shading(face.shading);
    converted.texture_transform = {
        .scale = {face.texture_transform.scale.x,
                  face.texture_transform.scale.y},
        .shift_uv = {face.texture_transform.shift_uv.x,
                     face.texture_transform.shift_uv.y},
        .rotation_degrees = face.texture_transform.rotation_degrees,
    };
    converted.vertices.reserve(face.vertices.size());
    for (const quader::modeling::VertexId vertex : face.vertices) {
      converted.vertices.push_back(vertex.index);
    }
    converted.uvs.reserve(face.uvs.size());
    for (const quader::modeling::Vec2 uv : face.uvs) {
      converted.uvs.push_back({uv.x, uv.y});
    }
    document.faces.push_back(std::move(converted));
  }
  document.hard_normal_edges.reserve(topology.hard_normal_edges.size());
  for (const quader::modeling::EdgeKey edge : topology.hard_normal_edges) {
    document.hard_normal_edges.push_back(
        quader_poly::make_edge(edge.a.index, edge.b.index));
  }
  document.soft_normal_edges.reserve(topology.soft_normal_edges.size());
  for (const quader::modeling::EdgeKey edge : topology.soft_normal_edges) {
    document.soft_normal_edges.push_back(
        quader_poly::make_edge(edge.a.index, edge.b.index));
  }
  return document;
}

bool same_topology_layout(const quader_poly::Document &left,
                          const quader_poly::Document &right) {
  if (left.vertices.size() != right.vertices.size() ||
      left.faces.size() != right.faces.size() ||
      left.hard_normal_edges != right.hard_normal_edges ||
      left.soft_normal_edges != right.soft_normal_edges ||
      left.next_vertex_id != right.next_vertex_id ||
      left.next_face_id != right.next_face_id) {
    return false;
  }
  for (std::size_t index = 0; index < left.vertices.size(); ++index) {
    if (left.vertices[index].id != right.vertices[index].id) {
      return false;
    }
  }
  for (std::size_t index = 0; index < left.faces.size(); ++index) {
    const quader_poly::Face &a = left.faces[index];
    const quader_poly::Face &b = right.faces[index];
    if (a.id != b.id || a.vertices != b.vertices ||
        a.uvs.size() != b.uvs.size() ||
        a.material_slot != b.material_slot ||
        a.normal_shading != b.normal_shading ||
        a.texture_transform.scale.x != b.texture_transform.scale.x ||
        a.texture_transform.scale.y != b.texture_transform.scale.y ||
        a.texture_transform.shift_uv.x != b.texture_transform.shift_uv.x ||
        a.texture_transform.shift_uv.y != b.texture_transform.shift_uv.y ||
        a.texture_transform.rotation_degrees !=
            b.texture_transform.rotation_degrees) {
      return false;
    }
    for (std::size_t uv = 0; uv < a.uvs.size(); ++uv) {
      if (a.uvs[uv].x != b.uvs[uv].x || a.uvs[uv].y != b.uvs[uv].y) {
        return false;
      }
    }
  }
  return true;
}

bool sdk_document_authored_topology(
    const quader::modeling::PolygonDocument &document,
    quader_poly::Document &authored) {
  quader::modeling::Result<quader::modeling::AuthoredTopology> result =
      document.authored_topology();
  if (!result.ok()) {
    return false;
  }
  authored = to_native_topology(result.value());
  return true;
}

quader_poly::CompiledMesh &canonical_compilation_scratch() {
  // Transform commits are already partitioned across worker threads. Keeping
  // one scratch payload per worker removes repeated output-vector allocation
  // without sharing mutable compiler state between workers.
  thread_local quader_poly::CompiledMesh scratch;
  return scratch;
}

void replace_geometry_from_compiled(
    const quader_poly::CompiledMesh &compiled,
    MeshGeometryPayload &geometry) {
  geometry.vertices.resize(compiled.vertices.size());
  for (std::size_t index = 0; index < compiled.vertices.size(); ++index) {
    geometry.vertices[index] = from_compiled_vertex(compiled.vertices[index]);
  }

  geometry.indices.resize(compiled.indices.size());
  std::copy(compiled.indices.begin(), compiled.indices.end(),
            geometry.indices.begin());

  geometry.primitives.resize(compiled.primitives.size());
  for (std::size_t index = 0; index < compiled.primitives.size(); ++index) {
    const quader_poly::CompiledPrimitive &primitive =
        compiled.primitives[index];
    geometry.primitives[index] = MeshPrimitiveSpan{
        .index_offset = primitive.index_offset,
        .index_count = primitive.index_count,
        .material_slot = primitive.material_slot,
    };
  }
}

MeshAsset project_document_mesh_positions(quader_poly::Document next_authored,
                                          const MeshAsset &current,
                                          bool topology_retained,
                                          const MeshProjectionPositionUpdatePlan *plan = nullptr) {
  MeshAsset projected = current;
  if (topology_retained) {
    // Smoothing depends on canonical face normals and authored connectivity.
    // Preserve it only for proven translations with exactly equal normals;
    // generated UVs and tessellation still use their usual owners.
    const bool flat_faces = std::ranges::all_of(
        next_authored.faces, [](const quader_poly::Face &face) {
          return face.normal_shading == quader_poly::SurfaceShading::Flat;
        });
    const bool exact_translation = !flat_faces && is_exact_document_translation(
        *current.authored_polygon_document, next_authored);
    if ((flat_faces || exact_translation) &&
        try_update_retained_document_mesh_positions(
            *current.authored_polygon_document, next_authored, projected,
            exact_translation)) {
      (exact_translation ? retained_translation_count : retained_flat_face_count)
          .fetch_add(1, std::memory_order_relaxed);
      projected.replace_authored_polygon_document(std::move(next_authored));
      return projected;
    }

    projected = current;
    const bool partial_updated = plan != nullptr
        ? try_update_prepared_document_mesh_positions(next_authored, projected, *plan)
        : try_update_partial_document_mesh_positions(
              *current.authored_polygon_document, next_authored, projected);
    if (partial_updated) {
      retained_partial_count.fetch_add(1, std::memory_order_relaxed);
      if (plan != nullptr) {
        partial_plan_reuse_count.fetch_add(1, std::memory_order_relaxed);
      }
      projected.replace_authored_polygon_document(std::move(next_authored));
      return projected;
    }
  }

  if (plan != nullptr) {
    partial_plan_fallback_count.fetch_add(1, std::memory_order_relaxed);
  }
  quader_poly::CompiledMesh &compiled = canonical_compilation_scratch();
  quader_poly::compile_document_into(next_authored, compiled);
  full_compile_count.fetch_add(1, std::memory_order_relaxed);

  projected = current;
  replace_geometry_from_compiled(compiled, projected.edit_geometry());
  projected.has_stable_cpu_topology = true;
  projected.replace_authored_polygon_document(std::move(next_authored));
  return projected;
}

} // namespace

std::shared_ptr<const MeshProjectionPositionUpdatePlan>
MeshProjectionService::prepare_position_update_plan(
    const MeshAsset &current,
    std::span<const std::uint32_t> movable_vertex_ids) {
  QDR_PROFILE_SCOPE("Mesh projection prepare position update plan");
  partial_plan_build_count.fetch_add(1, std::memory_order_relaxed);
  if (!current.has_stable_cpu_topology || !current.has_authored_polygon_document ||
      !current.authored_polygon_document || movable_vertex_ids.empty()) {
    return nullptr;
  }
  auto plan = std::make_shared<MeshProjectionPositionUpdatePlan>();
  plan->source_document = current.authored_polygon_document;
  const auto &source = *plan->source_document;
  const auto &geometry = current.geometry();
  std::uint64_t face_visits = 0U;
  const auto prepare = [&]() {
    if (source.faces.size() < 2U || geometry.primitives.size() != source.faces.size()) {
      return false;
    }
    const std::unordered_set<std::uint32_t> movable_ids(
        movable_vertex_ids.begin(), movable_vertex_ids.end());
    plan->movable_vertices.resize(source.vertices.size(), 0U);
    for (std::size_t index = 0U; index < source.vertices.size(); ++index) {
      plan->movable_vertices[index] = movable_ids.contains(source.vertices[index].id);
    }
    std::vector<std::uint8_t> dirty_faces(source.faces.size(), 0U);
    std::unordered_set<quader_poly::ElementId> normal_vertices;
    std::unordered_set<quader_poly::ElementId> face_ids;
    face_ids.reserve(source.faces.size());
    std::size_t dirty_face_count = 0U;
    for (std::size_t index = 0U; index < source.faces.size(); ++index) {
      ++face_visits;
      const auto &face = source.faces[index];
      // The canonical normal map resolves duplicate IDs globally. Never let
      // a subset change which malformed source face wins that resolution.
      if (!face_ids.insert(face.id).second) return false;
      if (std::ranges::any_of(face.vertices, [&](quader_poly::ElementId id) {
            return movable_ids.contains(id);
          })) {
        dirty_faces[index] = 1U;
        ++dirty_face_count;
        normal_vertices.insert(face.vertices.begin(), face.vertices.end());
      }
    }
    if (dirty_face_count == 0U || dirty_face_count == source.faces.size()) return false;

    std::size_t vertex_offset = 0U;
    std::size_t index_offset = 0U;
    for (std::size_t index = 0U; index < source.faces.size(); ++index) {
      ++face_visits;
      const auto &face = source.faces[index];
      const auto &primitive = geometry.primitives[index];
      if (vertex_offset + face.vertices.size() > geometry.vertices.size() ||
          primitive.index_offset != index_offset ||
          primitive.material_slot != face.material_slot ||
          index_offset + primitive.index_count > geometry.indices.size()) {
        return false;
      }
      MeshProjectionPositionUpdatePlan::FaceRange range{
          .face_index = index,
          .vertex_offset = vertex_offset,
          .dirty = dirty_faces[index] != 0U};
      bool included = range.dirty;
      for (std::size_t corner = 0U; corner < face.vertices.size(); ++corner) {
        if (geometry.vertices[vertex_offset + corner].source_vertex_id !=
            face.vertices[corner]) {
          return false;
        }
        if (normal_vertices.contains(face.vertices[corner])) {
          included = true;
          if (!range.dirty) range.normal_corners.push_back(corner);
        }
      }
      if (included) plan->faces.push_back(std::move(range));
      vertex_offset += face.vertices.size();
      index_offset += primitive.index_count;
    }
    if (plan->faces.size() == source.faces.size() ||
        vertex_offset != geometry.vertices.size() ||
        index_offset != geometry.indices.size()) {
      return false;
    }
    plan->primitives = geometry.primitives;
    plan->compiled_source_ids.reserve(geometry.vertices.size());
    for (const auto &vertex : geometry.vertices) {
      plan->compiled_source_ids.push_back(vertex.source_vertex_id);
    }
    plan->index_count = geometry.indices.size();
    return true;
  };
  plan->eligible = prepare();
  partial_plan_face_visit_count.fetch_add(face_visits, std::memory_order_relaxed);
  return plan;
}

std::shared_ptr<const MeshRetainedPositionProjectionPlan>
MeshProjectionService::prepare_retained_position_projection(
    const modeling::PolygonDocument &document, const MeshAsset &current,
    const MeshProjectionPositionUpdatePlan *partial_plan) {
  QDR_PROFILE_SCOPE("Mesh projection prepare retained coordinate mapping");
  if (!current.has_stable_cpu_topology || !current.authored_polygon_document) return {};
  auto plan = std::make_shared<MeshRetainedPositionProjectionPlan>(
      modeling::polygon_document_read_snapshot(document));
  const auto source = plan->source.view();
  const auto &authored = *current.authored_polygon_document;
  if (source.vertices.size() != authored.vertices.size() ||
      source.faces.size() != authored.faces.size()) return {};
  for (std::size_t index = 0U; index < source.vertices.size(); ++index) {
    if (source.vertices[index].id != authored.vertices[index].id ||
        !same_position(source.vertices[index].position, authored.vertices[index].position)) return {};
  }
  if (!std::ranges::equal(source.faces, authored.faces,
                         mesh_asset_payload_detail::same_polygon_face) ||
      !std::ranges::equal(source.hard_normal_edges, authored.hard_normal_edges) ||
      !std::ranges::equal(source.soft_normal_edges, authored.soft_normal_edges)) return {};
  plan->source_geometry = current.geometry_payload();
  const auto &geometry = *plan->source_geometry;
  if (geometry.primitives.size() != source.faces.size()) return {};
  const quader_poly::document_internal::VertexLookupIndex lookup(source.vertices);
  plan->compiled_vertex_sources.reserve(geometry.vertices.size());
  for (const auto &vertex : geometry.vertices) {
    const auto *authored_vertex = lookup.find(vertex.source_vertex_id);
    if (authored_vertex == nullptr || !same_position(vertex.position, authored_vertex->position)) return {};
    plan->compiled_vertex_sources.push_back(
        static_cast<std::size_t>(authored_vertex - source.vertices.data()));
  }
  std::size_t vertex_offset = 0U;
  std::size_t index_offset = 0U;
  plan->face_vertex_offsets.reserve(source.faces.size());
  for (std::size_t index = 0U; index < source.faces.size(); ++index) {
    const auto &face = source.faces[index];
    const auto &primitive = geometry.primitives[index];
    if (vertex_offset + face.vertices.size() > geometry.vertices.size() ||
        primitive.index_offset != index_offset || primitive.material_slot != face.material_slot ||
        index_offset + primitive.index_count > geometry.indices.size()) return {};
    plan->face_vertex_offsets.push_back(vertex_offset);
    for (std::size_t corner = 0U; corner < face.vertices.size(); ++corner) {
      if (geometry.vertices[vertex_offset + corner].source_vertex_id != face.vertices[corner]) return {};
    }
    vertex_offset += face.vertices.size();
    index_offset += primitive.index_count;
  }
  if (vertex_offset != geometry.vertices.size() || index_offset != geometry.indices.size()) return {};
  plan->source_compiled_vertices.resize(source.vertices.size());
  plan->source_faces.resize(source.vertices.size());
  plan->face_triangle_identities.resize(source.faces.size());
  for (std::size_t index = 0U; index < plan->compiled_vertex_sources.size(); ++index) {
    plan->source_compiled_vertices[plan->compiled_vertex_sources[index]].push_back(index);
  }
  for (std::size_t index = 0U; index < source.faces.size(); ++index) {
    const auto &face = source.faces[index];
    const auto offset = plan->face_vertex_offsets[index];
    for (std::size_t corner = 0U; corner < face.vertices.size(); ++corner) {
      plan->source_faces[plan->compiled_vertex_sources[offset + corner]].push_back(index);
    }
    if (face.vertices.size() <= 4U || face.uvs.size() != face.vertices.size()) continue;
    const auto &primitive = geometry.primitives[index];
    auto &identities = plan->face_triangle_identities[index];
    identities.reserve(primitive.index_count / 3U);
    for (std::size_t triangle = 0U; triangle + 2U < primitive.index_count; triangle += 3U) {
      std::array<std::uint32_t, 3> identity{};
      for (std::size_t corner = 0U; corner < identity.size(); ++corner) {
        const auto compiled_index = geometry.indices[primitive.index_offset + triangle + corner];
        if (compiled_index >= geometry.vertices.size()) return {};
        identity[corner] = geometry.vertices[compiled_index].source_vertex_id;
      }
      std::ranges::sort(identity);
      identities.push_back(identity);
    }
    std::ranges::sort(identities);
  }
  plan->flat_faces = std::ranges::all_of(source.faces, [](const auto &face) {
    return face.normal_shading == quader_poly::SurfaceShading::Flat;
  });
  if (partial_plan != nullptr && partial_plan->eligible &&
      partial_plan->source_document &&
      partial_plan->movable_vertices.size() == source.vertices.size() &&
      partial_plan->primitives == geometry.primitives &&
      partial_plan->index_count == geometry.indices.size() &&
      std::ranges::equal(partial_plan->compiled_source_ids, geometry.vertices,
          [](std::uint32_t id, const MeshVertex &vertex) { return id == vertex.source_vertex_id; }) &&
      same_topology_layout(*partial_plan->source_document, authored)) {
    plan->partial_ranges = partial_plan->faces;
    plan->movable_vertices = partial_plan->movable_vertices;
    plan->partial_faces.reserve(plan->partial_ranges.size());
    for (const auto &range : plan->partial_ranges) {
      plan->partial_faces.push_back(source.faces[range.face_index]);
    }
  }
  return plan;
}

std::shared_ptr<const MeshGeometryProjection>
MeshProjectionService::project_retained_mesh_position_updates(
    std::span<const modeling::VertexPositionUpdate> updates,
    const MeshRetainedPositionProjectionPlan &plan,
    const std::shared_ptr<const MeshGeometryProjection> &previous,
    std::shared_ptr<std::vector<QVec3>> positions,
    bool preserve_preview_uv_layout) {
  QDR_PROFILE_SCOPE("Mesh projection apply prepared coordinate ranges");
  if (updates.empty() || !plan.source_geometry ||
      (previous && (!previous->positions ||
          previous->positions->size() != plan.compiled_vertex_sources.size() ||
          (previous->source_geometry != plan.source_geometry &&
           (!previous->source_geometry || !mesh_geometry_payload_equal(
               *previous->source_geometry, *plan.source_geometry)))))) {
    retained_stream_fallback_count.fetch_add(1U, std::memory_order_relaxed);
    return {};
  }
  // The plan may be shared by readers, but its gesture work arrays never leave
  // this lock or back an immutable result. Rejected candidates cannot modify an
  // accepted coordinate, attribute or index stream.
  const std::lock_guard lock(plan.coordinate_mutex);
  const auto source = plan.source.view();
  if (!plan.coordinate_workspace) {
    plan.coordinate_workspace =
        std::make_unique<MeshRetainedPositionProjectionPlan::CoordinateWorkspace>(
            source.vertices);
  }
  auto &work = *plan.coordinate_workspace;
  if (!work.bound) {
    work.update_sources.clear();
    work.update_sources.reserve(updates.size());
    std::vector<std::uint8_t> selected(source.vertices.size(), 0U);
    work.partial_eligible = !plan.partial_faces.empty() &&
        plan.movable_vertices.size() == source.vertices.size();
    for (const auto &update : updates) {
      const auto *vertex = work.next_lookup.find(update.vertex.index);
      if (vertex == nullptr) return {};
      const auto index = static_cast<std::size_t>(vertex - work.next_vertices.data());
      if (selected[index] != 0U) return {};
      selected[index] = 1U;
      work.update_sources.push_back(index);
      work.partial_eligible = work.partial_eligible && plan.movable_vertices[index] != 0U;
    }
    work.preserved_uv_faces.clear();
    for (std::size_t index = 0U; index < source.faces.size(); ++index) {
      if (plan.face_triangle_identities[index].empty()) continue;
      const auto offset = plan.face_vertex_offsets[index];
      bool complete = true;
      for (std::size_t corner = 0U; complete && corner < source.faces[index].vertices.size(); ++corner) {
        complete = selected[plan.compiled_vertex_sources[offset + corner]] != 0U;
      }
      if (complete) work.preserved_uv_faces.push_back(index);
    }
    work.dirty_face_flags.resize(source.faces.size(), 0U);
    work.dirty_faces.reserve(source.faces.size());
    work.bound = true;
  }
  if (updates.size() != work.update_sources.size()) return {};
  for (std::size_t index = 0U; index < updates.size(); ++index) {
    const auto &update = updates[index];
    if (update.vertex.index != source.vertices[work.update_sources[index]].id ||
        !std::isfinite(update.position.x) || !std::isfinite(update.position.y) ||
        !std::isfinite(update.position.z)) return {};
  }
  for (const auto face : work.dirty_faces) work.dirty_face_flags[face] = 0U;
  work.dirty_faces.clear();
  const auto previous_position = [&](std::size_t source_index) {
    const auto &compiled = plan.source_compiled_vertices[source_index];
    return previous && !compiled.empty()
        ? (*previous->positions)[compiled.front()] : source.vertices[source_index].position;
  };
  const auto difference = [](QVec3 left, QVec3 right) {
    return std::array<double, 3>{static_cast<double>(left.x) - right.x,
        static_cast<double>(left.y) - right.y, static_cast<double>(left.z) - right.z};
  };
  const QVec3 next_origin{updates.front().position.x, updates.front().position.y,
                         updates.front().position.z};
  const auto previous_origin = previous_position(work.update_sources.front());
  const auto translation_delta = difference(next_origin, previous_origin);
  bool translation = !plan.flat_faces && updates.size() == source.vertices.size();
  for (std::size_t index = 0U; index < updates.size(); ++index) {
    const auto source_index = work.update_sources[index];
    const auto &update = updates[index];
    const QVec3 position{update.position.x, update.position.y, update.position.z};
    const auto before = previous_position(source_index);
    work.next_vertices[source_index].position = position;
    work.previous_vertices[source_index].position = before;
    translation = translation && difference(position, before) == translation_delta &&
        difference(position, next_origin) == difference(before, previous_origin);
    if (same_position(position, before)) continue;
    for (const auto face : plan.source_faces[source_index]) {
      if (work.dirty_face_flags[face] != 0U) continue;
      work.dirty_face_flags[face] = 1U;
      work.dirty_faces.push_back(face);
    }
  }
  auto next = source;
  next.vertices = work.next_vertices;
  auto before = source;
  before.vertices = work.previous_vertices;
  if (preserve_preview_uv_layout) {
    // The UV-preservation owner first tries the existing loop unchanged. Reuse
    // that exact case without copying/reimporting a complete authored document;
    // any changed triangulation still goes through its loop-rebasing fallback.
    for (const auto index : work.preserved_uv_faces) {
      if (work.dirty_face_flags[index] == 0U) continue;
      const auto &face = source.faces[index];
      const auto normal = quader_poly::document_internal::face_normal_from_vertex_lookup(
          face, work.next_lookup);
      const auto triangles = quader_poly::document_internal::triangulate_face_local_indices(
          face, work.next_lookup, normal);
      work.triangle_identities.clear();
      work.triangle_identities.reserve(triangles.size());
      for (const auto &triangle : triangles) {
        std::array<std::uint32_t, 3> identity{face.vertices[triangle.a],
            face.vertices[triangle.b], face.vertices[triangle.c]};
        std::ranges::sort(identity);
        work.triangle_identities.push_back(identity);
      }
      std::ranges::sort(work.triangle_identities);
      if (work.triangle_identities != plan.face_triangle_identities[index]) {
        retained_stream_fallback_count.fetch_add(1U, std::memory_order_relaxed);
        return {};
      }
    }
  }
  if (!positions || (previous && positions.get() == previous->positions.get())) {
    positions = std::make_shared<std::vector<QVec3>>();
  }
  std::erase_if(work.position_buffers, [](const auto &buffer) { return buffer.expired(); });
  const bool initialized = positions->size() == plan.compiled_vertex_sources.size() &&
      std::ranges::any_of(work.position_buffers, [&](const auto &buffer) {
        return buffer.lock().get() == positions.get();
      });
  if (!initialized) {
    positions->resize(plan.compiled_vertex_sources.size());
    for (std::size_t index = 0U; index < plan.compiled_vertex_sources.size(); ++index) {
      (*positions)[index] = source.vertices[plan.compiled_vertex_sources[index]].position;
    }
    work.position_buffers.push_back(positions);
  }
  // A recycled detached buffer may belong to an older sample. Refresh every
  // movable range, while its immutable source ranges remain initialized once.
  for (const auto source_index : work.update_sources) {
    const auto position = work.next_vertices[source_index].position;
    for (const auto compiled_index : plan.source_compiled_vertices[source_index]) {
      (*positions)[compiled_index] = position;
    }
  }
  if (work.dirty_faces.empty() && previous) return previous;
  auto writer = RetainedProjectionWriter(plan.source_geometry, previous, positions);
  if ((plan.flat_faces || translation) && update_retained_projection_face_ranges(
          before, next, plan, writer, translation, work.previous_lookup,
          work.next_lookup, work.face_positions, work.dirty_faces, true)) {
    (translation ? retained_translation_count : retained_flat_face_count)
        .fetch_add(1U, std::memory_order_relaxed);
  } else {
    writer = RetainedProjectionWriter(plan.source_geometry, previous, positions);
    bool compiled = false;
    if (work.partial_eligible) {
      compiled = update_retained_projection_compilation(next, plan, writer, true, &work.compilation);
      partial_compiled_face_count.fetch_add(plan.partial_faces.size(), std::memory_order_relaxed);
      if (compiled) {
        retained_partial_count.fetch_add(1U, std::memory_order_relaxed);
        partial_plan_reuse_count.fetch_add(1U, std::memory_order_relaxed);
      }
    }
    if (!compiled) {
      writer = RetainedProjectionWriter(plan.source_geometry, previous, positions);
      full_compile_count.fetch_add(1U, std::memory_order_relaxed);
      if (!update_retained_projection_compilation(next, plan, writer, false, &work.compilation)) {
        retained_stream_fallback_count.fetch_add(1U, std::memory_order_relaxed);
        return {};
      }
    }
  }
  retained_stream_projection_count.fetch_add(1U, std::memory_order_relaxed);
  return writer.finish();
}

std::shared_ptr<const MeshGeometryProjection>
MeshProjectionService::project_retained_mesh_positions(
    const modeling::PolygonDocument &document,
    const modeling::PolygonDocument &previous_document,
    const MeshRetainedPositionProjectionPlan &plan,
    const std::shared_ptr<const MeshGeometryProjection> &previous,
    std::shared_ptr<std::vector<QVec3>> positions) {
  QDR_PROFILE_SCOPE("Mesh projection retained coordinate streams");
  const auto next_snapshot = modeling::polygon_document_read_snapshot(document);
  const auto before_snapshot = modeling::polygon_document_read_snapshot(previous_document);
  if (next_snapshot.topology_identity() != plan.source.topology_identity() ||
      before_snapshot.topology_identity() != plan.source.topology_identity() ||
      (previous && previous->source_geometry != plan.source_geometry &&
       (!previous->source_geometry ||
        !mesh_geometry_payload_equal(*previous->source_geometry, *plan.source_geometry)))) {
    retained_stream_fallback_count.fetch_add(1U, std::memory_order_relaxed);
    return {};
  }
  if (previous && next_snapshot.content_identity() == before_snapshot.content_identity()) return previous;
  const auto next = next_snapshot.view();
  const auto before = before_snapshot.view();
  if (!positions) positions = std::make_shared<std::vector<QVec3>>();
  positions->resize(plan.compiled_vertex_sources.size());
  for (std::size_t index = 0U; index < plan.compiled_vertex_sources.size(); ++index) {
    (*positions)[index] = next.vertices[plan.compiled_vertex_sources[index]].position;
  }
  auto writer = RetainedProjectionWriter(plan.source_geometry, previous, positions);
  const bool translation = !plan.flat_faces && is_exact_document_translation(before, next);
  if ((plan.flat_faces || translation) &&
      update_retained_projection_faces(before, next, plan, writer, translation)) {
    (translation ? retained_translation_count : retained_flat_face_count)
        .fetch_add(1U, std::memory_order_relaxed);
  } else {
    writer = RetainedProjectionWriter(plan.source_geometry, previous, positions);
    bool compiled = false;
    bool partial_eligible = !plan.partial_faces.empty();
    const auto source = plan.source.view();
    for (std::size_t index = 0U; partial_eligible && index < next.vertices.size(); ++index) {
      if (!plan.movable_vertices[index] &&
          (!same_position(next.vertices[index].position, source.vertices[index].position) ||
           !same_position(before.vertices[index].position, source.vertices[index].position))) {
        partial_eligible = false;
      }
    }
    if (partial_eligible) {
      compiled = update_retained_projection_compilation(next, plan, writer, true);
      partial_compiled_face_count.fetch_add(plan.partial_faces.size(), std::memory_order_relaxed);
      if (compiled) {
        retained_partial_count.fetch_add(1U, std::memory_order_relaxed);
        partial_plan_reuse_count.fetch_add(1U, std::memory_order_relaxed);
      }
    }
    if (!compiled) {
      writer = RetainedProjectionWriter(plan.source_geometry, previous, positions);
      full_compile_count.fetch_add(1U, std::memory_order_relaxed);
      if (!update_retained_projection_compilation(next, plan, writer, false)) {
        retained_stream_fallback_count.fetch_add(1U, std::memory_order_relaxed);
        return {};
      }
    }
  }
  retained_stream_projection_count.fetch_add(1U, std::memory_order_relaxed);
  return writer.finish();
}

MeshAsset MeshProjectionService::materialize_retained_mesh_projection(
    const modeling::PolygonDocument &document, const MeshAsset &source,
    std::shared_ptr<const MeshGeometryProjection> projection) {
  QDR_PROFILE_SCOPE("Mesh projection pin accepted coordinate publication");
  MeshAsset mesh = source;
  mesh.share_geometry_projection(std::move(projection));
  mesh.share_authored_polygon_snapshot(modeling::polygon_document_read_snapshot(document));
  return mesh;
}

MeshAsset MeshProjectionService::materialize_retained_mesh_projection(
    const modeling::PolygonDocument &document, const MeshAsset &source,
    const MeshGeometryProjection &projection,
    const std::shared_ptr<const quader_poly::Document> &shared_authored) {
  QDR_PROFILE_SCOPE("Mesh projection materialize accepted coordinate streams");
  const auto snapshot = modeling::polygon_document_read_snapshot(document);
  const auto view = snapshot.view();
  MeshAsset mesh = source;
  mesh.share_geometry(projection.materialized_geometry());
  if (shared_authored &&
      std::ranges::equal(view.vertices, shared_authored->vertices,
                         mesh_asset_payload_detail::same_polygon_vertex) &&
      std::ranges::equal(view.faces, shared_authored->faces,
                         mesh_asset_payload_detail::same_polygon_face) &&
      std::ranges::equal(view.hard_normal_edges, shared_authored->hard_normal_edges) &&
      std::ranges::equal(view.soft_normal_edges, shared_authored->soft_normal_edges) &&
      view.next_vertex_id == shared_authored->next_vertex_id &&
      view.next_face_id == shared_authored->next_face_id) {
    mesh.share_authored_polygon_document(shared_authored);
    return mesh;
  }
  quader_poly::Document authored;
  authored.vertices.assign(view.vertices.begin(), view.vertices.end());
  authored.faces.assign(view.faces.begin(), view.faces.end());
  authored.hard_normal_edges.assign(view.hard_normal_edges.begin(), view.hard_normal_edges.end());
  authored.soft_normal_edges.assign(view.soft_normal_edges.begin(), view.soft_normal_edges.end());
  authored.next_vertex_id = view.next_vertex_id;
  authored.next_face_id = view.next_face_id;
  mesh.replace_authored_polygon_document(std::move(authored));
  return mesh;
}

quader_poly::Transform3 MeshProjectionService::texture_mapping_transform(
    const QMat4 &matrix) {
  const auto &value = matrix.values;
  return {
      .x_axis = {value[0], value[1], value[2]},
      .y_axis = {value[4], value[5], value[6]},
      .z_axis = {value[8], value[9], value[10]},
      .origin = {value[12], value[13], value[14]},
  };
}

MeshAsset MeshProjectionService::mesh_asset_from_compiled_mesh(
    const quader_poly::CompiledMesh &compiled) {
  QDR_PROFILE_FUNCTION();
  return quader::mesh_asset_from_compiled_mesh(compiled);
}

MeshAsset MeshProjectionService::compile_polygon_document_mesh(
    const quader_poly::Document &document) {
  QDR_PROFILE_FUNCTION();
  return mesh_asset_from_compiled_mesh(quader_poly::compile_document(document));
}

MeshAsset MeshProjectionService::compile_object_mesh(
    const quader_editor_core::PolygonObject &object) {
  QDR_PROFILE_FUNCTION();
  MeshAsset mesh = compile_polygon_document_mesh(object.document);
  // Keep the runtime's immutable authored pin. Geometry edits publish a new
  // CowPolygonDocument payload before this projection is reached.
  mesh.share_authored_polygon_document(object.document.topology_payload());
  return mesh;
}

quader::modeling::Result<quader::modeling::PolygonDocument>
MeshProjectionService::sdk_document_from_polygon_document(
    const quader_poly::Document &document) {
  return quader::modeling::PolygonDocument::from_authored_topology(
      to_sdk_topology(document));
}

std::optional<MeshAsset> MeshProjectionService::compile_sdk_document_mesh(
    const quader::modeling::PolygonDocument &document) {
  QDR_PROFILE_FUNCTION();
  quader_poly::Document authored;
  if (!sdk_document_authored_topology(document, authored)) {
    return std::nullopt;
  }
  quader_poly::CompiledMesh &compiled = canonical_compilation_scratch();
  quader_poly::compile_document_into(authored, compiled);

  MeshAsset mesh = quader::mesh_asset_from_compiled_mesh(compiled);
  mesh.replace_authored_polygon_document(std::move(authored));
  return mesh;
}

std::optional<MeshAsset>
MeshProjectionService::update_sdk_document_mesh_positions(
    const quader::modeling::PolygonDocument &document,
    const MeshAsset &current,
    const MeshProjectionPositionUpdatePlan *plan) {
  QDR_PROFILE_FUNCTION();
  if (!current.has_stable_cpu_topology ||
      !current.has_authored_polygon_document ||
      !current.authored_polygon_document) {
    return std::nullopt;
  }
  quader_poly::Document next_authored;
  if (!sdk_document_authored_topology(document, next_authored)) {
    return std::nullopt;
  }
  if (!same_topology_layout(*current.authored_polygon_document,
                            next_authored)) {
    return std::nullopt;
  }

  return project_document_mesh_positions(std::move(next_authored), current, true, plan);
}

MeshAsset MeshProjectionService::update_polygon_document_mesh_positions(
    quader_poly::Document document, const MeshAsset &current,
    const MeshProjectionPositionUpdatePlan *plan) {
  QDR_PROFILE_FUNCTION();
  const bool topology_retained = current.has_stable_cpu_topology &&
      current.has_authored_polygon_document && current.authored_polygon_document &&
      same_topology_layout(*current.authored_polygon_document, document);
  return project_document_mesh_positions(std::move(document), current,
                                         topology_retained, plan);
}

MeshProjectionPositionUpdateStats
MeshProjectionService::position_update_stats() noexcept {
  return {
      .retained_translation_count =
          retained_translation_count.load(std::memory_order_relaxed),
      .retained_flat_face_count =
          retained_flat_face_count.load(std::memory_order_relaxed),
      .retained_partial_count =
          retained_partial_count.load(std::memory_order_relaxed),
      .partial_compiled_face_count =
          partial_compiled_face_count.load(std::memory_order_relaxed),
      .full_compile_count = full_compile_count.load(std::memory_order_relaxed),
      .partial_plan_build_count = partial_plan_build_count.load(std::memory_order_relaxed),
      .partial_plan_face_visit_count =
          partial_plan_face_visit_count.load(std::memory_order_relaxed),
      .partial_plan_reuse_count = partial_plan_reuse_count.load(std::memory_order_relaxed),
      .partial_plan_fallback_count =
          partial_plan_fallback_count.load(std::memory_order_relaxed),
      .retained_stream_projection_count =
          retained_stream_projection_count.load(std::memory_order_relaxed),
      .retained_stream_fallback_count =
          retained_stream_fallback_count.load(std::memory_order_relaxed)};
}

void MeshProjectionService::apply_mesh_texture_mapping(
    MeshAsset &mesh, const QMat4 &sampling_from_local) {
  if (sampling_from_local.values == QMat4{}.values) {
    return;
  }
  if (!mesh.has_authored_polygon_document &&
      std::ranges::all_of(mesh.geometry().vertices, [](const MeshVertex &vertex) {
        return vertex.uv_carrier == std::array<float, 4>{};
      })) {
    if (!initialize_compiled_texture_mapping(mesh)) {
      return;
    }
  }
  const quader_poly::Transform3 mapping =
      texture_mapping_transform(sampling_from_local);
  for (MeshVertex &vertex : mesh.edit_geometry().vertices) {
    vertex.uv = quader_poly::evaluate_texture_mapping(
        vertex.uv, vertex.position, vertex.uv_geometric_normal,
        vertex.uv_carrier, mapping);
  }
}

std::optional<MeshAsset> MeshProjectionService::mesh_asset_from_sdk_payload(
    const quader::modeling::MeshPayload &payload,
    const quader::modeling::PolygonDocument &authored_document) {
  MeshAsset mesh;
  MeshGeometryPayload &geometry = mesh.edit_geometry();
  geometry.vertices.reserve(payload.vertices.size());
  for (const quader::modeling::MeshVertexPayload &vertex : payload.vertices) {
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
    });
  }
  geometry.indices = payload.indices;
  geometry.primitives.reserve(payload.primitives.size());
  for (const quader::modeling::MeshPrimitivePayload &primitive :
       payload.primitives) {
    if (primitive.index_offset > geometry.indices.size() ||
        primitive.index_count >
            geometry.indices.size() - primitive.index_offset) {
      return std::nullopt;
    }
    geometry.primitives.push_back({
        .index_offset = primitive.index_offset,
        .index_count = primitive.index_count,
        .material_slot = primitive.material.valid()
                             ? primitive.material.index - 1U
                             : 0U,
    });
  }
  if (std::ranges::any_of(geometry.indices, [&](std::uint32_t index) {
        return index >= geometry.vertices.size();
      })) {
    return std::nullopt;
  }
  mesh.has_stable_cpu_topology = true;
  quader_poly::Document authored;
  if (!sdk_document_authored_topology(authored_document, authored)) {
    return std::nullopt;
  }
  mesh.replace_authored_polygon_document(std::move(authored));
  return mesh;
}

std::optional<quader_poly::Document>
MeshProjectionService::polygon_document_from_sdk_document(
    const quader::modeling::PolygonDocument &document) {
  quader_poly::Document authored;
  if (!sdk_document_authored_topology(document, authored)) {
    return std::nullopt;
  }
  return authored;
}

modeling::Result<modeling::PolygonDocument>
MeshProjectionService::rebase_sdk_document_texture_mapping(
    const quader_poly::Document &before,
    const modeling::PolygonDocument &transformed,
    const QMat4 &source_sampling, const QMat4 &destination_sampling,
    bool reflected_geometry) {
  const auto rejected = [](std::string message) {
    return modeling::Result<modeling::PolygonDocument>::failure(
        {.code = modeling::ErrorCode::ValidationFailed,
         .message = std::move(message)});
  };
  if (!texture_mapping_is_valid({source_sampling}) ||
      !texture_mapping_is_valid({destination_sampling})) {
    return rejected("Combine Mesh requires finite nonsingular sampling transforms.");
  }
  auto after = MeshProjectionService::polygon_document_from_sdk_document(
      transformed);
  if (!after.has_value()) {
    return rejected(
        "Combine Mesh authored texture topology is unavailable.");
  }
  const quader_poly::document_internal::VertexLookupIndex before_lookup(before);
  const quader_poly::document_internal::VertexLookupIndex after_lookup(*after);
  std::unordered_map<quader_poly::ElementId, const quader_poly::Face *> faces;
  for (const auto &face : before.faces) faces.emplace(face.id, &face);
  const auto from_source =
      MeshProjectionService::texture_mapping_transform(source_sampling);
  const auto from_destination =
      MeshProjectionService::texture_mapping_transform(destination_sampling);
  for (auto &face : after->faces) {
    const auto found = faces.find(face.id);
    if (found == faces.end()) {
      return rejected(
          "Combine Mesh source texture face is unavailable.");
    }
    const auto &source_face = *found->second;
    if (source_face.vertices.size() < 3U ||
        source_face.vertices.size() != face.vertices.size() ||
        (!source_face.uvs.empty() && source_face.uvs.size() != source_face.vertices.size()) ||
        face.uvs.size() != source_face.uvs.size() ||
        !quader_poly::valid_face_texture_transform(source_face.texture_transform) ||
        !quader_poly::valid_face_texture_transform(face.texture_transform)) {
      return rejected("Combine Mesh found invalid face corner data.");
    }
    const auto winding = reflected_geometry
        ? quader_poly::document_internal::FaceRebaseWinding::Reverse
        : quader_poly::document_internal::FaceRebaseWinding::Preserve;
    if (!quader_poly::document_internal::preserve_rebased_face_texture_triangulation(
            before, source_face, *after, face, winding, before_lookup,
            after_lookup)) {
      if (!valid_rebase_face(source_face, before_lookup) ||
          !valid_rebase_face(face, after_lookup) ||
          !same_rebase_corner_ids(source_face, face)) {
        return rejected("Combine Mesh found invalid face corner data.");
      }
      if (reflected_geometry) {
        quader_poly::document_internal::reverse_face_winding(
            face, quader_poly::document_internal::FaceWindingChange::ReflectedGeometry);
      }
    }
    const QVec3 before_normal = quader_poly::face_normal(before, source_face);
    const QVec3 after_normal = quader_poly::face_normal(*after, face);
    const auto carrier =
        quader_poly::face_texture_carrier(before, source_face, before_normal);
    struct SourceCorner {
      QVec2 base;
      QVec2 displayed;
    };
    std::unordered_map<quader_poly::ElementId, SourceCorner> source_uvs;
    source_uvs.reserve(source_face.vertices.size());
    for (std::size_t corner = 0; corner < source_face.vertices.size(); ++corner) {
      const auto id = source_face.vertices[corner];
      const auto *vertex = before_lookup.find(id);
      if (vertex == nullptr) {
        return rejected(
            "Combine Mesh source texture corner is unavailable.");
      }
      const QVec2 base = source_face.uvs.empty()
                            ? quader_poly::generated_texture_uv(
                                  vertex->position, before_normal)
                            : source_face.uvs[corner];
      const QVec2 uv = quader_poly::apply_face_texture_transform(
          base, source_face.texture_transform);
      const QVec2 displayed = quader_poly::evaluate_texture_mapping(
          uv, vertex->position, before_normal, carrier, from_source);
      if (!std::isfinite(displayed.x) || !std::isfinite(displayed.y) ||
          !source_uvs.emplace(id, SourceCorner{base, displayed}).second) {
        return rejected("Combine Mesh found invalid source corner data.");
      }
    }
    std::vector<QVec3> positions;
    std::vector<QVec2> desired;
    positions.reserve(face.vertices.size());
    desired.reserve(face.vertices.size());
    for (const auto id : face.vertices) {
      const auto *vertex = after_lookup.find(id);
      const auto uv = source_uvs.find(id);
      if (vertex == nullptr || uv == source_uvs.end()) {
        return rejected(
            "Combine Mesh texture corner correspondence was lost.");
      }
      positions.push_back(vertex->position);
      desired.push_back(uv->second.displayed);
    }
    auto solved = quader_poly::solve_face_texture_base_uvs(
        positions, desired, after_normal, face.texture_transform,
        from_destination);
    if (!solved.has_value()) {
      // Keep the original authored chart when exact occurrence compensation
      // is unavailable. Geometry does not depend on reproducing its UV interior.
      std::vector<QVec2> retained;
      retained.reserve(face.vertices.size());
      for (const auto id : face.vertices) {
        retained.push_back(source_uvs.at(id).base);
      }
      const auto retained_carrier = quader_poly::face_texture_carrier(
          positions, retained, after_normal, face.texture_transform);
      for (std::size_t index = 0; index < positions.size(); ++index) {
        const QVec2 displayed = quader_poly::evaluate_texture_mapping(
            quader_poly::apply_face_texture_transform(retained[index], face.texture_transform),
            positions[index], after_normal, retained_carrier, from_destination);
        if (!std::isfinite(displayed.x) || !std::isfinite(displayed.y)) {
          return rejected("Combine Mesh would create non-finite texture coordinates.");
        }
      }
      solved = std::move(retained);
    }
    face.uvs = std::move(*solved);
  }
  return MeshProjectionService::sdk_document_from_polygon_document(*after);
}

modeling::Result<modeling::OperationResult>
MeshProjectionService::preserve_transformed_sdk_document_uv_layout(
    const modeling::PolygonDocument &before,
    modeling::PolygonDocument &transformed, bool whole_object) {
  const auto rejected = [](std::string message) {
    return modeling::Result<modeling::OperationResult>::failure(
        {.code = modeling::ErrorCode::ValidationFailed,
         .message = std::move(message)});
  };
  modeling::OperationResult receipt;
  auto source = polygon_document_from_sdk_document(before);
  if (!source.has_value()) {
    return rejected("Transform source authored topology is unavailable.");
  }
  if (std::ranges::none_of(source->faces, [](const quader_poly::Face &face) {
        return face.vertices.size() > 4U &&
               face.uvs.size() == face.vertices.size();
      })) {
    return modeling::Result<modeling::OperationResult>::success(
        std::move(receipt));
  }
  quader_poly::Selection selected;
  if (whole_object) {
    selected.vertices.reserve(source->vertices.size());
    for (const auto &vertex : source->vertices) {
      selected.vertices.push_back(vertex.id);
    }
  } else {
    const auto selection = before.selection();
    for (const auto vertex : selection.vertices) {
      selected.vertices.push_back(vertex.index);
    }
    for (const auto edge : selection.edges) {
      selected.edges.push_back(quader_poly::make_edge(edge.a.index, edge.b.index));
    }
    for (const auto face : selection.faces) {
      selected.faces.push_back(face.index);
    }
  }
  const auto selected_vertices =
      quader_poly::selected_vertex_ids(*source, selected);
  const auto complete_face = [&](const quader_poly::Face &face) {
    return face.vertices.size() > 4U &&
           face.uvs.size() == face.vertices.size() &&
           std::ranges::all_of(face.vertices, [&](quader_poly::ElementId id) {
             return std::ranges::binary_search(selected_vertices, id);
           });
  };
  if (std::ranges::none_of(source->faces, complete_face)) {
    return modeling::Result<modeling::OperationResult>::success(
        std::move(receipt));
  }
  auto destination = polygon_document_from_sdk_document(transformed);
  if (!destination.has_value()) {
    return rejected("Transform candidate authored topology is unavailable.");
  }
  std::unordered_map<quader_poly::ElementId, quader_poly::Face *> faces;
  for (auto &face : destination->faces) faces.emplace(face.id, &face);
  const quader_poly::document_internal::VertexLookupIndex source_lookup(*source);
  const quader_poly::document_internal::VertexLookupIndex destination_lookup(
      *destination);
  for (const auto &face : source->faces) {
    if (!complete_face(face)) continue;
    const auto found = faces.find(face.id);
    if (found == faces.end()) {
      return rejected("Transform candidate face identity is unavailable.");
    }
    auto &candidate = *found->second;
    if (candidate.vertices.size() != face.vertices.size() ||
        candidate.uvs.size() != face.uvs.size()) {
      return rejected("Transform candidate face corner identities are invalid.");
    }
    const auto previous_loop = candidate.vertices;
    if (!quader_poly::document_internal::preserve_rebased_face_texture_triangulation(
            *source, face, *destination, candidate,
            quader_poly::document_internal::FaceRebaseWinding::Preserve,
            source_lookup, destination_lookup) &&
        (!valid_rebase_face(face, source_lookup) ||
         !valid_rebase_face(candidate, destination_lookup) ||
         !same_rebase_corner_ids(face, candidate))) {
      return rejected("Transform candidate face corner identities are invalid.");
    }
    if (candidate.vertices != previous_loop) {
      receipt.modified.faces.push_back(modeling::make_id<modeling::FaceTag>(face.id));
    }
  }
  if (receipt.modified.faces.empty()) {
    return modeling::Result<modeling::OperationResult>::success(
        std::move(receipt));
  }
  auto imported = sdk_document_from_polygon_document(*destination);
  if (!imported.ok()) return rejected(imported.error_message());
  const auto restored = imported.value().apply_selection(transformed.selection());
  if (!restored.ok()) return rejected(restored.error_message());
  imported.value().preserve_identity_high_water(transformed);
  transformed = std::move(imported).value();
  receipt.changed = true;
  receipt.dirty = {.topology = true,
                   .geometry = true,
                   .materials = true,
                   .overlays = true};
  receipt.message = "Transformed face texture layout preserved.";
  return modeling::Result<modeling::OperationResult>::success(std::move(receipt));
}

bool MeshProjectionService::initialize_compiled_texture_mapping(MeshAsset &mesh) {
  if (mesh.has_authored_polygon_document || mesh.geometry().indices.empty()) {
    return true;
  }
  const MeshGeometryPayload &original = mesh.geometry();
  std::vector<MeshVertex> vertices = original.vertices;
  std::vector<std::uint32_t> indices = original.indices;
  std::vector<bool> assigned(original.vertices.size(), false);
  quader_poly::Document triangle;
  triangle.vertices = {{.id = 1u}, {.id = 2u}, {.id = 3u}};
  quader_poly::Face face;
  face.vertices = {1u, 2u, 3u};
  for (std::size_t offset = 0; offset + 2u < indices.size(); offset += 3u) {
    std::array<QVec3, 3> positions;
    std::array<QVec2, 3> uvs;
    for (std::size_t corner = 0; corner < 3u; ++corner) {
      const std::uint32_t index = original.indices[offset + corner];
      if (index >= original.vertices.size()) {
        return false;
      }
      const MeshVertex &vertex = original.vertices[index];
      triangle.vertices[corner].position = positions[corner] = vertex.position;
      uvs[corner] = vertex.uv;
    }
    const QVec3 normal = quader_poly::face_normal(triangle, face);
    const auto carrier =
        quader_poly::face_texture_carrier(positions, uvs, normal, {});
    for (std::size_t corner = 0; corner < 3u; ++corner) {
      const std::uint32_t index = original.indices[offset + corner];
      if (!assigned[index]) {
        vertices[index].uv_carrier = carrier;
        vertices[index].uv_geometric_normal = normal;
        assigned[index] = true;
      } else if (vertices[index].uv_carrier != carrier ||
                 !same_position(vertices[index].uv_geometric_normal, normal)) {
        if (vertices.size() >= std::numeric_limits<std::uint32_t>::max()) {
          return false;
        }
        MeshVertex vertex = original.vertices[index];
        vertex.uv_carrier = carrier;
        vertex.uv_geometric_normal = normal;
        indices[offset + corner] = static_cast<std::uint32_t>(vertices.size());
        vertices.push_back(vertex);
      }
    }
  }
  MeshGeometryPayload &geometry = mesh.edit_geometry();
  geometry.vertices = std::move(vertices);
  geometry.indices = std::move(indices);
  return true;
}

bool MeshProjectionService::bake_polygon_texture_mapping(
    quader_poly::Document &document, const QMat4 &sampling_from_local) {
  return quader_poly::bake_document_texture_mapping(
      document, texture_mapping_transform(sampling_from_local));
}

} // namespace quader
