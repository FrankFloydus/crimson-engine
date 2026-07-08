////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/projection/mesh_projection_service.hpp"

#include "diagnostics/profile.hpp"

#include <memory>

#include <mesh/polygon/document.hpp>

namespace quader {
namespace {

QVec3 from_runtime(quader::QVec3 value) { return {value.x, value.y, value.z}; }

MeshAsset
mesh_asset_from_compiled_mesh(const quader_poly::CompiledMesh &compiled) {
  MeshAsset mesh;
  mesh.name = "Mesh";
  mesh.vertices.reserve(compiled.vertices.size());
  mesh.indices = compiled.indices;
  mesh.primitives.reserve(compiled.primitives.size());
  mesh.has_stable_cpu_topology = true;

  for (const quader_poly::CompiledVertex &vertex : compiled.vertices) {
    mesh.vertices.push_back(MeshVertex{
        .position = from_runtime(vertex.position),
        .normal = from_runtime(vertex.normal),
        .uv = {vertex.uv0.x, vertex.uv0.y},
        .rgba = vertex.color,
    });
  }
  for (const quader_poly::CompiledPrimitive &primitive : compiled.primitives) {
    mesh.primitives.push_back(MeshPrimitiveSpan{
        .index_offset = primitive.index_offset,
        .index_count = primitive.index_count,
        .material_slot = primitive.material_slot,
    });
  }

  return mesh;
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
  mesh.has_authored_polygon_document = true;
  mesh.authored_polygon_document =
      std::make_shared<quader_poly::Document>(object.document);
  return mesh;
}

} // namespace quader
