////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <mesh/polygon/document.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>

namespace {

using namespace quader_poly;

bool nearly_equal(float left, float right, float epsilon = 0.0001F)
{
    return std::abs(left - right) <= epsilon;
}

Document make_uv_quad_document(std::uint32_t material_slot = 7)
{
    Document document;
    const ElementId bottom_left = add_vertex(document, { -1.0F, -1.0F, 0.0F });
    const ElementId bottom_right = add_vertex(document, { 1.0F, -1.0F, 0.0F });
    const ElementId top_right = add_vertex(document, { 1.0F, 1.0F, 0.0F });
    const ElementId top_left = add_vertex(document, { -1.0F, 1.0F, 0.0F });
    const std::array<ElementId, 4> face_vertices { bottom_left, bottom_right, top_right, top_left };
    const ElementId face_id = add_face(document, face_vertices, material_slot);
    require_test(face_id != kInvalidElementId, "quad face should be created");
    return document;
}

void compile_document_preserves_loop_uvs_material_slots_and_normals()
{
    Document document = make_uv_quad_document(9);
    Face& face = document.faces.front();
    face.uvs = {
        { 0.125F, 0.25F },
        { 0.875F, 0.25F },
        { 0.875F, 0.75F },
        { 0.125F, 0.75F },
    };

    const CompiledMesh mesh = compile_document(document, SurfaceShading::Flat);
    require_test(mesh.vertices.size() == 4U, "quad compile should emit one vertex per loop");
    require_test(mesh.indices.size() == 6U, "quad compile should triangulate to two triangles");
    require_test(mesh.primitives.size() == 1U, "quad compile should emit one primitive");
    require_test(mesh.primitives.front().material_slot == 9U, "compiled primitive should preserve face material slot");
    require_test(mesh.primitives.front().index_offset == 0U, "first primitive should start at index zero");
    require_test(mesh.primitives.front().index_count == 6U, "quad primitive should own six indices");

    for (std::size_t index = 0; index < face.uvs.size(); ++index) {
        require_test(nearly_equal(mesh.vertices[index].uv0.x, face.uvs[index].x), "compiled vertex should preserve explicit loop UV x");
        require_test(nearly_equal(mesh.vertices[index].uv0.y, face.uvs[index].y), "compiled vertex should preserve explicit loop UV y");
        require_test(nearly_equal(mesh.vertices[index].normal.z, 1.0F), "flat compiled normal should follow face normal");
    }
}

} // namespace

TEST(QdrDocumentMeshUvTests, QdrDocumentMeshUvTests) {
    compile_document_preserves_loop_uvs_material_slots_and_normals();
}
