////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/projection/mesh_projection_service.hpp"

#include "diagnostics/profile.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <utility>

#include <quader/modeling/neutral/polygon/document.hpp>
#include <quader/modeling/neutral/polygon/document_texture.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_mesh_internal.hpp>

namespace quader {
namespace {

QVec3 from_runtime(quader::QVec3 value) { return {value.x, value.y, value.z}; }

void replace_geometry_from_compiled(
    const quader_poly::CompiledMesh &compiled,
    MeshGeometryPayload &geometry);

bool same_position(quader::QVec3 left, quader::QVec3 right) {
  return left.x == right.x && left.y == right.y && left.z == right.z;
}

float triangle_orientation(quader::QVec3 a, quader::QVec3 b,
                           quader::QVec3 c, quader::QVec3 normal) {
  const quader::QVec3 ab{b.x - a.x, b.y - a.y, b.z - a.z};
  const quader::QVec3 ac{c.x - a.x, c.y - a.y, c.z - a.z};
  const quader::QVec3 area{
      ab.y * ac.z - ab.z * ac.y,
      ab.z * ac.x - ab.x * ac.z,
      ab.x * ac.y - ab.y * ac.x,
  };
  return area.x * normal.x + area.y * normal.y + area.z * normal.z;
}

/**
 * Applies a topology-retained transform directly to the already compiled
 * flat-shaded face ranges. This is the Quader equivalent of Blender's edit
 * mesh partial tessellation/normal update: unchanged faces retain their
 * derived buffers, while every affected face still receives canonical
 * normals, generated UVs, triangulation, winding, IDs, and primitive spans.
 * Smooth islands fall back to the full compiler because their normals can
 * propagate beyond the directly affected faces.
 */
bool try_update_flat_document_mesh_positions(
    const quader_poly::Document &previous,
    const quader_poly::Document &next, MeshAsset &projected) {
  QDR_PROFILE_SCOPE("Mesh projection retained flat-face update");
  using quader_poly::document_internal::Triangle;

  if (previous.faces.size() != next.faces.size() ||
      std::ranges::any_of(next.faces, [](const quader_poly::Face &face) {
        return face.normal_shading != quader_poly::SurfaceShading::Flat;
      })) {
    return false;
  }

  MeshGeometryPayload &geometry = projected.edit_geometry();
  if (geometry.primitives.size() != next.faces.size()) {
    return false;
  }

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

    MeshPrimitiveSpan &primitive = geometry.primitives[face_index];
    if (primitive.index_offset != index_offset ||
        primitive.material_slot != face.material_slot ||
        index_offset + primitive.index_count > geometry.indices.size()) {
      return false;
    }

    bool face_changed = false;
    for (std::size_t local_index = 0U;
         local_index < face.vertices.size(); ++local_index) {
      const quader_poly::ElementId vertex_id = face.vertices[local_index];
      const quader_poly::Vertex *before =
          quader_poly::find_vertex(previous, vertex_id);
      const quader_poly::Vertex *after = quader_poly::find_vertex(next, vertex_id);
      if (before == nullptr || after == nullptr ||
          geometry.vertices[vertex_offset + local_index].source_vertex_id !=
              vertex_id) {
        return false;
      }
      face_changed = face_changed ||
                     !same_position(before->position, after->position);
    }

    if (face_changed) {
      QDR_PROFILE_SCOPE("Mesh projection retained flat-face changed face");
      const std::vector<Triangle> triangles =
          quader_poly::document_internal::triangulate_face_local_indices(next,
                                                                          face);
      if (triangles.empty() ||
          triangles.size() * 3U != primitive.index_count) {
        return false;
      }

      const quader::QVec3 normal = quader_poly::face_normal(next, face);
      const int dropped_axis =
          quader_poly::document_internal::dropped_axis_for_normal(normal);
      const quader_poly::document_internal::FaceUvBasis uv_basis =
          quader_poly::document_internal::generated_face_uv_basis(next, face,
                                                                   normal);
      const bool use_loop_uvs =
          quader_poly::document_internal::face_has_loop_uvs(face);
      const auto uv_carrier =
          quader_poly::face_texture_carrier(next, face, normal);
      for (std::size_t local_index = 0U;
           local_index < face.vertices.size(); ++local_index) {
        const quader_poly::ElementId vertex_id = face.vertices[local_index];
        const quader_poly::Vertex *vertex =
            quader_poly::find_vertex(next, vertex_id);
        if (vertex == nullptr) {
          return false;
        }
        const QVec2 base_uv =
            use_loop_uvs
                ? face.uvs[local_index]
                : quader_poly::document_internal::generated_uv_for_position(
                      vertex->position, uv_basis, dropped_axis);
        geometry.vertices[vertex_offset + local_index] = MeshVertex{
            .position = vertex->position,
            .normal = normal,
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
        const quader_poly::Vertex *a =
            quader_poly::find_vertex(next, face.vertices[triangle.a]);
        const quader_poly::Vertex *b =
            quader_poly::find_vertex(next, face.vertices[triangle.b]);
        const quader_poly::Vertex *c =
            quader_poly::find_vertex(next, face.vertices[triangle.c]);
        if (a == nullptr || b == nullptr || c == nullptr) {
          return false;
        }
        geometry.indices[write_index++] =
            static_cast<std::uint32_t>(vertex_offset + triangle.a);
        if (triangle_orientation(a->position, b->position, c->position,
                                 normal) >= 0.0F) {
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
  return document;
}

bool same_topology_layout(const quader_poly::Document &left,
                          const quader_poly::Document &right) {
  if (left.vertices.size() != right.vertices.size() ||
      left.faces.size() != right.faces.size() ||
      left.hard_normal_edges != right.hard_normal_edges ||
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
    const quader_poly::CompiledVertex &vertex = compiled.vertices[index];
    geometry.vertices[index] = MeshVertex{
        .position = from_runtime(vertex.position),
        .normal = from_runtime(vertex.normal),
        .uv = {vertex.uv0.x, vertex.uv0.y},
        .rgba = vertex.color,
        .source_vertex_id = vertex.source_vertex_id,
        .uv_carrier = vertex.uv_carrier,
        .uv_geometric_normal = vertex.uv_geometric_normal,
    };
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

} // namespace

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
    const MeshAsset &current) {
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

  // Flat editable meshes can update only the face ranges touched by the
  // transformed vertices. This preserves the canonical compiler result while
  // avoiding a full derived-buffer rebuild for every object in a large edit
  // selection. Any layout, degeneracy, or smooth-normal case drops to the
  // complete compiler below.
  MeshAsset projected = current;
  if (try_update_flat_document_mesh_positions(
          *current.authored_polygon_document, next_authored, projected)) {
    projected.replace_authored_polygon_document(std::move(next_authored));
    return projected;
  }

  quader_poly::CompiledMesh &compiled = canonical_compilation_scratch();
  quader_poly::compile_document_into(next_authored, compiled);

  projected = current;
  MeshGeometryPayload &geometry = projected.edit_geometry();
  replace_geometry_from_compiled(compiled, geometry);
  projected.replace_authored_polygon_document(std::move(next_authored));
  return projected;
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
