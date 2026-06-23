////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/projection/MeshProjectionService.h"

#include <memory>

#include <mesh/polygon/document.h>

namespace quader {
namespace {

QVec3 fromRuntime(quader::QVec3 value) {
    return {value.x, value.y, value.z};
}

MeshAsset meshAssetFromCompiledMesh(const quader_poly::CompiledMesh& compiled) {
    MeshAsset mesh;
    mesh.name = "Mesh";
    mesh.vertices.reserve(compiled.vertices.size());
    mesh.indices = compiled.indices;
    mesh.primitives.reserve(compiled.primitives.size());
    mesh.hasStableCpuTopology = true;

    for (const quader_poly::CompiledVertex& vertex : compiled.vertices) {
        mesh.vertices.push_back(MeshVertex{
                .position = fromRuntime(vertex.position),
                .normal = fromRuntime(vertex.normal),
                .uv = {vertex.uv0.x, vertex.uv0.y},
                .rgba = vertex.color,
        });
    }
    for (const quader_poly::CompiledPrimitive& primitive : compiled.primitives) {
        mesh.primitives.push_back(MeshPrimitiveSpan{
                .indexOffset = primitive.index_offset,
                .indexCount = primitive.index_count,
                .materialSlot = primitive.material_slot,
        });
    }

    return mesh;
}

} // namespace

MeshAsset MeshProjectionService::meshAssetFromCompiledMesh(const quader_poly::CompiledMesh& compiled) {
    return quader::meshAssetFromCompiledMesh(compiled);
}

MeshAsset MeshProjectionService::compilePolygonDocumentMesh(const quader_poly::Document& document) {
    return meshAssetFromCompiledMesh(quader_poly::compile_document(document));
}

MeshAsset MeshProjectionService::compileObjectMesh(const quader_editor_core::PolygonObject& object) {
    MeshAsset mesh = compilePolygonDocumentMesh(object.document);
    mesh.hasAuthoredPolygonDocument = true;
    mesh.authoredPolygonDocument = std::make_shared<quader_poly::Document>(object.document);
    return mesh;
}

} // namespace quader
