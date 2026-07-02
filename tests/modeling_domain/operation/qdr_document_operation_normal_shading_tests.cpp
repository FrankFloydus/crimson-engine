////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_test_helpers.hpp"

using namespace quader_poly;

namespace {

/**
 * Stores the Bent Corner Fixture data contract used by the automated test suite.
 */
struct BentCornerFixture {
    Document document;
    ElementId left_bottom = kInvalidElementId;
    ElementId shared_bottom = kInvalidElementId;
    ElementId shared_top = kInvalidElementId;
    ElementId left_top = kInvalidElementId;
    ElementId back_bottom = kInvalidElementId;
    ElementId back_top = kInvalidElementId;
    ElementId floor_face = kInvalidElementId;
    ElementId wall_face = kInvalidElementId;
    Edge shared_edge;
};

BentCornerFixture make_bent_corner_document()
{
    BentCornerFixture fixture;
    fixture.left_bottom = add_vertex(fixture.document, { 0.0F, 0.0F, 0.0F });
    fixture.shared_bottom = add_vertex(fixture.document, { 1.0F, 0.0F, 0.0F });
    fixture.shared_top = add_vertex(fixture.document, { 1.0F, 1.0F, 0.0F });
    fixture.left_top = add_vertex(fixture.document, { 0.0F, 1.0F, 0.0F });
    fixture.back_bottom = add_vertex(fixture.document, { 1.0F, 0.0F, 1.0F });
    fixture.back_top = add_vertex(fixture.document, { 1.0F, 1.0F, 1.0F });

    const std::array<ElementId, 4> floor {
        fixture.left_bottom,
        fixture.shared_bottom,
        fixture.shared_top,
        fixture.left_top,
    };
    const std::array<ElementId, 4> wall {
        fixture.shared_bottom,
        fixture.back_bottom,
        fixture.back_top,
        fixture.shared_top,
    };
    fixture.floor_face = add_face(fixture.document, floor);
    fixture.wall_face = add_face(fixture.document, wall);
    fixture.shared_edge = make_edge(fixture.shared_bottom, fixture.shared_top);
    EXPECT_TRUE(fixture.floor_face != kInvalidElementId);
    EXPECT_TRUE(fixture.wall_face != kInvalidElementId);
    return fixture;
}

Selection all_faces(const BentCornerFixture& fixture)
{
    return qdr_test::face_selection({ fixture.floor_face, fixture.wall_face }, fixture.wall_face);
}

Selection shared_edge_selection(const BentCornerFixture& fixture)
{
    return qdr_test::edge_selection({ fixture.shared_edge }, fixture.shared_edge);
}

} // namespace

TEST(QdrDocumentOperationNormalShadingTests, ShadeSmoothAndFlatControlCompiledFaceVertexNormals) {
    BentCornerFixture fixture = make_bent_corner_document();
    Selection selection = all_faces(fixture);

    const CompiledMesh flat_before = compile_document(fixture.document);
    EXPECT_TRUE(flat_before.vertices.size() == 8U);
    EXPECT_FALSE(qdr_test::vec_nearly_equal(flat_before.vertices[1].normal, flat_before.vertices[4].normal));

    const OperationResult smooth = shade_selected_faces_smooth(fixture.document, selection);

    EXPECT_TRUE(smooth.changed);
    EXPECT_TRUE(fixture.document.faces[0].normal_shading ==
                SurfaceShading::Smooth);
    EXPECT_TRUE(fixture.document.faces[1].normal_shading ==
                SurfaceShading::Smooth);
    const CompiledMesh smooth_mesh = compile_document(fixture.document);
    EXPECT_TRUE(smooth_mesh.vertices.size() == 8U);
    EXPECT_TRUE(qdr_test::vec_nearly_equal(smooth_mesh.vertices[1].normal, smooth_mesh.vertices[4].normal));
    EXPECT_TRUE(qdr_test::vec_nearly_equal(smooth_mesh.vertices[2].normal, smooth_mesh.vertices[7].normal));

    const OperationResult flat = shade_selected_faces_flat(fixture.document, selection);

    EXPECT_TRUE(flat.changed);
    EXPECT_TRUE(fixture.document.faces[0].normal_shading ==
                SurfaceShading::Flat);
    EXPECT_TRUE(fixture.document.faces[1].normal_shading ==
                SurfaceShading::Flat);
    const CompiledMesh flat_after = compile_document(fixture.document);
    EXPECT_TRUE(flat_after.vertices.size() == 8U);
    EXPECT_TRUE(qdr_test::vec_nearly_equal(flat_after.vertices[1].normal, flat_before.vertices[1].normal));
    EXPECT_TRUE(qdr_test::vec_nearly_equal(flat_after.vertices[4].normal, flat_before.vertices[4].normal));
}

TEST(QdrDocumentOperationNormalShadingTests, HardAndSoftNormalEdgeOperationsSplitAndRejoinSmoothNormals) {
    BentCornerFixture fixture = make_bent_corner_document();
    Selection face_selection = all_faces(fixture);
    EXPECT_TRUE(shade_selected_faces_smooth(fixture.document, face_selection).changed);

    const CompiledMesh soft_before = compile_document(fixture.document);
    EXPECT_TRUE(qdr_test::vec_nearly_equal(soft_before.vertices[1].normal, soft_before.vertices[4].normal));

    Selection edge_selection = shared_edge_selection(fixture);
    const OperationResult hard = harden_selected_edge_normals(fixture.document, edge_selection);

    EXPECT_TRUE(hard.changed);
    EXPECT_TRUE(fixture.document.hard_normal_edges.size() == 1U);
    const CompiledMesh hard_mesh = compile_document(fixture.document);
    EXPECT_TRUE(hard_mesh.vertices.size() == 8U);
    EXPECT_FALSE(qdr_test::vec_nearly_equal(hard_mesh.vertices[1].normal, hard_mesh.vertices[4].normal));
    EXPECT_TRUE(qdr_test::vec_nearly_equal(hard_mesh.vertices[1].normal, face_normal(fixture.document, fixture.document.faces[0])));
    EXPECT_TRUE(qdr_test::vec_nearly_equal(hard_mesh.vertices[4].normal, face_normal(fixture.document, fixture.document.faces[1])));

    const OperationResult soft = soften_selected_edge_normals(fixture.document, edge_selection);

    EXPECT_TRUE(soft.changed);
    EXPECT_TRUE(fixture.document.hard_normal_edges.empty());
    const CompiledMesh soft_after = compile_document(fixture.document);
    EXPECT_TRUE(soft_after.vertices.size() == 8U);
    EXPECT_TRUE(qdr_test::vec_nearly_equal(soft_after.vertices[1].normal, soft_after.vertices[4].normal));
}

TEST(QdrDocumentOperationNormalShadingTests, SoftNormalEdgeOperationSoftensAdjacentFlatFaces) {
    BentCornerFixture fixture = make_bent_corner_document();

    const CompiledMesh flat_before = compile_document(fixture.document);
    EXPECT_TRUE(flat_before.vertices.size() == 8U);
    EXPECT_FALSE(qdr_test::vec_nearly_equal(flat_before.vertices[1].normal, flat_before.vertices[4].normal));

    Selection edge_selection = shared_edge_selection(fixture);
    const OperationResult soft = soften_selected_edge_normals(fixture.document, edge_selection);

    EXPECT_TRUE(soft.changed);
    EXPECT_TRUE(fixture.document.hard_normal_edges.empty());
    EXPECT_TRUE(fixture.document.faces[0].normal_shading ==
                SurfaceShading::Smooth);
    EXPECT_TRUE(fixture.document.faces[1].normal_shading ==
                SurfaceShading::Smooth);
    const CompiledMesh soft_mesh = compile_document(fixture.document);
    EXPECT_TRUE(soft_mesh.vertices.size() == 8U);
    EXPECT_TRUE(qdr_test::vec_nearly_equal(soft_mesh.vertices[1].normal, soft_mesh.vertices[4].normal));

    const OperationResult hard = harden_selected_edge_normals(fixture.document, edge_selection);

    EXPECT_TRUE(hard.changed);
    EXPECT_TRUE(fixture.document.hard_normal_edges.size() == 1U);
    const CompiledMesh hard_mesh = compile_document(fixture.document);
    EXPECT_TRUE(hard_mesh.vertices.size() == 8U);
    EXPECT_FALSE(qdr_test::vec_nearly_equal(hard_mesh.vertices[1].normal, hard_mesh.vertices[4].normal));
}
