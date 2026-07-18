////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/projection/mesh_projection_service.hpp"

#include "diagnostics/profile.hpp"

#include <memory>
#include <utility>

#include <quader/modeling/neutral/polygon/document.hpp>

namespace quader {
namespace {

QVec3 from_runtime(quader::QVec3 value) { return {value.x, value.y, value.z}; }

MeshAsset
mesh_asset_from_compiled_mesh(const quader_poly::CompiledMesh &compiled) {
  MeshAsset mesh;
  mesh.name = "Mesh";
  MeshGeometryPayload &geometry = mesh.edit_geometry();
  geometry.vertices.reserve(compiled.vertices.size());
  geometry.indices = compiled.indices;
  geometry.primitives.reserve(compiled.primitives.size());
  mesh.has_stable_cpu_topology = true;

  for (const quader_poly::CompiledVertex &vertex : compiled.vertices) {
    geometry.vertices.push_back(MeshVertex{
        .position = from_runtime(vertex.position),
        .normal = from_runtime(vertex.normal),
        .uv = {vertex.uv0.x, vertex.uv0.y},
        .rgba = vertex.color,
    });
  }
  for (const quader_poly::CompiledPrimitive &primitive : compiled.primitives) {
    geometry.primitives.push_back(MeshPrimitiveSpan{
        .index_offset = primitive.index_offset,
        .index_count = primitive.index_count,
        .material_slot = primitive.material_slot,
    });
  }

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

} // namespace

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
  quader::modeling::Result<quader::modeling::MeshPayload> compiled =
      document.compile_mesh();
  quader::modeling::Result<quader::modeling::AuthoredTopology> authored =
      document.authored_topology();
  if (!compiled.ok() || !authored.ok()) {
    return std::nullopt;
  }

  MeshAsset mesh;
  MeshGeometryPayload &geometry = mesh.edit_geometry();
  geometry.vertices.reserve(compiled.value().vertices.size());
  geometry.indices = compiled.value().indices;
  geometry.primitives.reserve(compiled.value().primitives.size());
  for (const quader::modeling::MeshVertexPayload &vertex :
       compiled.value().vertices) {
    geometry.vertices.push_back({
        .position = {vertex.position.x, vertex.position.y, vertex.position.z},
        .normal = {vertex.normal.x, vertex.normal.y, vertex.normal.z},
        .uv = {vertex.uv0.x, vertex.uv0.y},
        .rgba = vertex.color,
    });
  }
  for (const quader::modeling::MeshPrimitivePayload &primitive :
       compiled.value().primitives) {
    geometry.primitives.push_back({
        .index_offset = primitive.index_offset,
        .index_count = primitive.index_count,
        .material_slot = primitive.material.index,
    });
  }
  mesh.has_stable_cpu_topology = true;
  mesh.replace_authored_polygon_document(
      to_native_topology(authored.value()));
  return mesh;
}

} // namespace quader
