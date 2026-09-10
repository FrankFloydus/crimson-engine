////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_test_helpers.hpp"

#include <quader/modeling/neutral/polygon/internal/quader_poly_document_topology_helpers.hpp>

#include <cmath>

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

void set_bend_angle(BentCornerFixture& fixture, float degrees)
{
    constexpr float kPi = 3.14159265358979323846F;
    const float radians = degrees * kPi / 180.0F;
    const float outer_x = 1.0F + std::cos(radians);
    const float outer_z = -std::sin(radians);
    ASSERT_NE(find_vertex(fixture.document, fixture.back_bottom), nullptr);
    ASSERT_NE(find_vertex(fixture.document, fixture.back_top), nullptr);
    find_vertex(fixture.document, fixture.back_bottom)->position =
        { outer_x, 0.0F, outer_z };
    find_vertex(fixture.document, fixture.back_top)->position =
        { outer_x, 1.0F, outer_z };
}

bool shared_corner_normals_match(const BentCornerFixture& fixture)
{
    const CompiledMesh mesh = compile_document(fixture.document);
    return mesh.vertices.size() == 8U &&
           qdr_test::vec_nearly_equal(mesh.vertices[1].normal,
                                      mesh.vertices[4].normal) &&
           qdr_test::vec_nearly_equal(mesh.vertices[2].normal,
                                      mesh.vertices[7].normal);
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
    Selection edge_selection = shared_edge_selection(fixture);
    EXPECT_TRUE(
        soften_selected_edge_normals(fixture.document, edge_selection).changed);
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
    EXPECT_EQ(document_internal::edge_normal_mode(
                  fixture.document, fixture.shared_edge),
              EdgeNormalMode::Soft);
    const CompiledMesh flat_after = compile_document(fixture.document);
    EXPECT_TRUE(flat_after.vertices.size() == 8U);
    EXPECT_TRUE(qdr_test::vec_nearly_equal(flat_after.vertices[1].normal, flat_before.vertices[1].normal));
    EXPECT_TRUE(qdr_test::vec_nearly_equal(flat_after.vertices[4].normal, flat_before.vertices[4].normal));

    const OperationResult smooth_again =
        shade_selected_faces_smooth(fixture.document, selection);

    EXPECT_TRUE(smooth_again.changed);
    EXPECT_EQ(document_internal::edge_normal_mode(
                  fixture.document, fixture.shared_edge),
              EdgeNormalMode::Soft);
    EXPECT_TRUE(shared_corner_normals_match(fixture));
}

TEST(QdrDocumentOperationNormalShadingTests, HardAndSoftNormalEdgeOperationsSplitAndRejoinSmoothNormals) {
    BentCornerFixture fixture = make_bent_corner_document();
    Selection face_selection = all_faces(fixture);
    EXPECT_TRUE(shade_selected_faces_smooth(fixture.document, face_selection).changed);

    Selection edge_selection = shared_edge_selection(fixture);
    const CompiledMesh default_mesh = compile_document(fixture.document);
    EXPECT_FALSE(qdr_test::vec_nearly_equal(
        default_mesh.vertices[1].normal, default_mesh.vertices[4].normal));

    const OperationResult initial_soft =
        soften_selected_edge_normals(fixture.document, edge_selection);
    EXPECT_TRUE(initial_soft.changed);
    EXPECT_TRUE(fixture.document.soft_normal_edges.size() == 1U);
    EXPECT_TRUE(shared_corner_normals_match(fixture));

    const OperationResult hard = harden_selected_edge_normals(fixture.document, edge_selection);

    EXPECT_TRUE(hard.changed);
    EXPECT_TRUE(fixture.document.hard_normal_edges.size() == 1U);
    EXPECT_TRUE(fixture.document.soft_normal_edges.empty());
    const CompiledMesh hard_mesh = compile_document(fixture.document);
    EXPECT_TRUE(hard_mesh.vertices.size() == 8U);
    EXPECT_FALSE(qdr_test::vec_nearly_equal(hard_mesh.vertices[1].normal, hard_mesh.vertices[4].normal));
    EXPECT_TRUE(qdr_test::vec_nearly_equal(hard_mesh.vertices[1].normal, face_normal(fixture.document, fixture.document.faces[0])));
    EXPECT_TRUE(qdr_test::vec_nearly_equal(hard_mesh.vertices[4].normal, face_normal(fixture.document, fixture.document.faces[1])));

    const OperationResult soft = soften_selected_edge_normals(fixture.document, edge_selection);

    EXPECT_TRUE(soft.changed);
    EXPECT_TRUE(fixture.document.hard_normal_edges.empty());
    EXPECT_TRUE(fixture.document.soft_normal_edges.size() == 1U);
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
    EXPECT_TRUE(fixture.document.soft_normal_edges.size() == 1U);
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
    EXPECT_TRUE(fixture.document.soft_normal_edges.empty());
    const CompiledMesh hard_mesh = compile_document(fixture.document);
    EXPECT_TRUE(hard_mesh.vertices.size() == 8U);
    EXPECT_FALSE(qdr_test::vec_nearly_equal(hard_mesh.vertices[1].normal, hard_mesh.vertices[4].normal));
}

TEST(QdrDocumentOperationNormalShadingTests,
     DefaultNormalEdgesJoinBelowAndSplitAboveHammerFortyDegreeThreshold)
{
    BentCornerFixture shallow = make_bent_corner_document();
    set_bend_angle(shallow, 30.0F);
    Selection shallow_faces = all_faces(shallow);
    ASSERT_TRUE(
        shade_selected_faces_smooth(shallow.document, shallow_faces).changed);
    EXPECT_TRUE(shared_corner_normals_match(shallow));

    BentCornerFixture steep = make_bent_corner_document();
    set_bend_angle(steep, 50.0F);
    Selection steep_faces = all_faces(steep);
    ASSERT_TRUE(shade_selected_faces_smooth(steep.document, steep_faces).changed);
    EXPECT_FALSE(shared_corner_normals_match(steep));

    BentCornerFixture immediately_below = make_bent_corner_document();
    set_bend_angle(immediately_below, 39.99F);
    Selection immediately_below_faces = all_faces(immediately_below);
    ASSERT_TRUE(shade_selected_faces_smooth(
                    immediately_below.document, immediately_below_faces)
                    .changed);
    EXPECT_TRUE(shared_corner_normals_match(immediately_below));

    BentCornerFixture exact_threshold = make_bent_corner_document();
    set_bend_angle(exact_threshold, 40.0F);
    Selection exact_threshold_faces = all_faces(exact_threshold);
    ASSERT_TRUE(shade_selected_faces_smooth(
                    exact_threshold.document, exact_threshold_faces)
                    .changed);
    EXPECT_FALSE(shared_corner_normals_match(exact_threshold));
}

TEST(QdrDocumentOperationNormalShadingTests,
     ExplicitNormalModesOverrideTheDefaultAngleWithoutReplacingFaceShading)
{
    BentCornerFixture shallow = make_bent_corner_document();
    set_bend_angle(shallow, 30.0F);
    Selection shallow_faces = all_faces(shallow);
    ASSERT_TRUE(
        shade_selected_faces_smooth(shallow.document, shallow_faces).changed);
    Selection shallow_edge = shared_edge_selection(shallow);
    ASSERT_TRUE(
        harden_selected_edge_normals(shallow.document, shallow_edge).changed);
    EXPECT_FALSE(shared_corner_normals_match(shallow));

    BentCornerFixture steep = make_bent_corner_document();
    set_bend_angle(steep, 50.0F);
    Selection steep_faces = all_faces(steep);
    ASSERT_TRUE(shade_selected_faces_smooth(steep.document, steep_faces).changed);
    Selection steep_edge = shared_edge_selection(steep);
    ASSERT_TRUE(
        soften_selected_edge_normals(steep.document, steep_edge).changed);
    EXPECT_TRUE(shared_corner_normals_match(steep));
}

TEST(QdrDocumentOperationNormalShadingTests,
     DefaultNormalModeClearsEitherOverrideAndKeepsCurrentFaceShading)
{
    BentCornerFixture fixture = make_bent_corner_document();
    Selection edge_selection = shared_edge_selection(fixture);

    ASSERT_TRUE(
        harden_selected_edge_normals(fixture.document, edge_selection).changed);
    ASSERT_TRUE(reset_selected_edge_normals_to_default(
                    fixture.document, edge_selection)
                    .changed);
    EXPECT_TRUE(fixture.document.hard_normal_edges.empty());
    EXPECT_TRUE(fixture.document.soft_normal_edges.empty());
    EXPECT_TRUE(std::ranges::all_of(fixture.document.faces, [](const Face& face) {
        return face.normal_shading == SurfaceShading::Flat;
    }));

    ASSERT_TRUE(
        soften_selected_edge_normals(fixture.document, edge_selection).changed);
    ASSERT_TRUE(reset_selected_edge_normals_to_default(
                    fixture.document, edge_selection)
                    .changed);
    EXPECT_TRUE(fixture.document.hard_normal_edges.empty());
    EXPECT_TRUE(fixture.document.soft_normal_edges.empty());
    EXPECT_TRUE(std::ranges::all_of(fixture.document.faces, [](const Face& face) {
        return face.normal_shading == SurfaceShading::Smooth;
    }));
}

TEST(QdrDocumentOperationNormalShadingTests,
     EdgeNormalOperationsKeepCanonicalDisjointStateVectors)
{
    BentCornerFixture fixture = make_bent_corner_document();
    const Edge boundary = make_edge(fixture.left_bottom, fixture.shared_bottom);
    Selection selection = qdr_test::edge_selection(
        { Edge { fixture.shared_edge.b, fixture.shared_edge.a },
          Edge { boundary.b, boundary.a } },
        fixture.shared_edge);

    ASSERT_TRUE(soften_selected_edge_normals(fixture.document, selection).changed);
    ASSERT_EQ(fixture.document.soft_normal_edges.size(), 2U);
    EXPECT_TRUE(fixture.document.hard_normal_edges.empty());
    EXPECT_EQ(fixture.document.soft_normal_edges[0], boundary);
    EXPECT_EQ(fixture.document.soft_normal_edges[1], fixture.shared_edge);

    Selection shared = shared_edge_selection(fixture);
    ASSERT_TRUE(harden_selected_edge_normals(fixture.document, shared).changed);
    ASSERT_EQ(fixture.document.hard_normal_edges.size(), 1U);
    ASSERT_EQ(fixture.document.soft_normal_edges.size(), 1U);
    EXPECT_EQ(fixture.document.hard_normal_edges.front(), fixture.shared_edge);
    EXPECT_EQ(fixture.document.soft_normal_edges.front(), boundary);
}

TEST(QdrDocumentOperationNormalShadingTests,
     BoundarySoftStateRemainsAuthoredWithoutJoiningUnrelatedFaceCorners)
{
    BentCornerFixture fixture = make_bent_corner_document();
    Selection faces = all_faces(fixture);
    ASSERT_TRUE(shade_selected_faces_smooth(fixture.document, faces).changed);
    const Edge boundary = make_edge(fixture.left_bottom, fixture.shared_bottom);
    Selection boundary_selection =
        qdr_test::edge_selection({ boundary }, boundary);

    const OperationResult soft =
        soften_selected_edge_normals(fixture.document, boundary_selection);

    ASSERT_TRUE(soft.changed);
    EXPECT_EQ(document_internal::edge_normal_mode(fixture.document, boundary),
              EdgeNormalMode::Soft);
    const CompiledMesh mesh = compile_document(fixture.document);
    ASSERT_EQ(mesh.vertices.size(), 8U);
    EXPECT_FALSE(qdr_test::vec_nearly_equal(
        mesh.vertices[1].normal, mesh.vertices[4].normal));
    EXPECT_FALSE(qdr_test::vec_nearly_equal(
        mesh.vertices[2].normal, mesh.vertices[7].normal));
}

TEST(QdrDocumentOperationNormalShadingTests,
     SoftBoundaryEdgesDoNotJoinSmoothFansThatOnlyShareAVertex)
{
    Document document;
    const ElementId shared = add_vertex(document, { 0.0F, 0.0F, 0.0F });
    const ElementId first_a = add_vertex(document, { 1.0F, 0.0F, 0.0F });
    const ElementId first_b = add_vertex(document, { 0.0F, 1.0F, 0.0F });
    const ElementId second_a = add_vertex(document, { 0.0F, 0.0F, 1.0F });
    const ElementId second_b = add_vertex(document, { 0.0F, 1.0F, 0.0F });
    const ElementId first_face =
        add_face(document, std::array { shared, first_a, first_b });
    const ElementId second_face =
        add_face(document, std::array { shared, second_b, second_a });
    ASSERT_NE(first_face, kInvalidElementId);
    ASSERT_NE(second_face, kInvalidElementId);
    document.faces[0].normal_shading = SurfaceShading::Smooth;
    document.faces[1].normal_shading = SurfaceShading::Smooth;
    document.soft_normal_edges = {
        make_edge(shared, first_a),
        make_edge(shared, second_b),
    };

    const CompiledMesh mesh = compile_document(document);

    ASSERT_EQ(mesh.vertices.size(), 6U);
    EXPECT_FALSE(qdr_test::vec_nearly_equal(
        mesh.vertices[0].normal, mesh.vertices[3].normal));
    EXPECT_TRUE(qdr_test::vec_nearly_equal(
        mesh.vertices[0].normal, face_normal(document, document.faces[0])));
    EXPECT_TRUE(qdr_test::vec_nearly_equal(
        mesh.vertices[3].normal, face_normal(document, document.faces[1])));
}

TEST(QdrDocumentOperationNormalShadingTests,
     CollapsedSmoothFaceCannotBridgeOrContaminateVisibleNormalFans)
{
    Document document;
    const ElementId shared = add_vertex(document, { 0.0F, 0.0F, 0.0F });
    const ElementId right = add_vertex(document, { 1.0F, 0.0F, 0.0F });
    const ElementId left = add_vertex(document, { -1.0F, 0.0F, 0.0F });
    const ElementId upper = add_vertex(document, { 0.0F, 1.0F, 0.0F });
    const ElementId forward = add_vertex(document, { 0.0F, 0.0F, 1.0F });
    const ElementId first_face =
        add_face(document, std::array { shared, right, upper });
    const ElementId second_face =
        add_face(document, std::array { shared, forward, left });
    ASSERT_NE(first_face, kInvalidElementId);
    ASSERT_NE(second_face, kInvalidElementId);
    document.faces[0].normal_shading = SurfaceShading::Smooth;
    document.faces[1].normal_shading = SurfaceShading::Smooth;
    const ElementId collapsed_face = document.next_face_id++;
    document.faces.push_back({
        .id = collapsed_face,
        .vertices = { right, shared, left },
        .normal_shading = SurfaceShading::Smooth,
    });
    document.soft_normal_edges = {
        make_edge(shared, right),
        make_edge(shared, left),
    };

    const CompiledMesh mesh = compile_document(document);

    ASSERT_GE(mesh.vertices.size(), 6U);
    ASSERT_NE(find_face(document, collapsed_face), nullptr);
    EXPECT_FALSE(qdr_test::vec_nearly_equal(
        mesh.vertices[0].normal, mesh.vertices[3].normal));
    EXPECT_TRUE(qdr_test::vec_nearly_equal(
        mesh.vertices[0].normal, face_normal(document, document.faces[0])));
    EXPECT_TRUE(qdr_test::vec_nearly_equal(
        mesh.vertices[3].normal, face_normal(document, document.faces[1])));
}

TEST(QdrDocumentOperationNormalShadingTests,
     CollapsedThirdIncidenceKeepsVisibleSharedEdgeNonManifold)
{
    BentCornerFixture fixture = make_bent_corner_document();
    Selection faces = all_faces(fixture);
    ASSERT_TRUE(shade_selected_faces_smooth(fixture.document, faces).changed);
    const ElementId collapsed_middle =
        add_vertex(fixture.document, { 1.0F, 0.5F, 0.0F });
    const ElementId collapsed_face = fixture.document.next_face_id++;
    fixture.document.faces.push_back({
        .id = collapsed_face,
        .vertices = {
            fixture.shared_bottom,
            fixture.shared_top,
            collapsed_middle,
        },
        .normal_shading = SurfaceShading::Smooth,
    });
    fixture.document.soft_normal_edges = { fixture.shared_edge };

    const CompiledMesh mesh = compile_document(fixture.document);

    ASSERT_GE(mesh.vertices.size(), 8U);
    ASSERT_NE(find_face(fixture.document, collapsed_face), nullptr);
    EXPECT_FALSE(qdr_test::vec_nearly_equal(
        mesh.vertices[1].normal, mesh.vertices[4].normal));
    EXPECT_FALSE(qdr_test::vec_nearly_equal(
        mesh.vertices[2].normal, mesh.vertices[7].normal));
}

TEST(QdrDocumentOperationNormalShadingTests,
     SplitStateRemapSkipsPrunedIntermediatesWithoutLosingLaterDescendants)
{
    Document source;
    const ElementId a = add_vertex(source, { 0.0F, 0.0F, 0.0F });
    const ElementId b = add_vertex(source, { 2.0F, 0.0F, 0.0F });
    const ElementId c = add_vertex(source, { 0.0F, 1.0F, 0.0F });
    const std::array<ElementId, 3> triangle { a, b, c };
    ASSERT_NE(add_face(source, triangle), kInvalidElementId);
    source.soft_normal_edges = { make_edge(a, b) };

    Document candidate = source;
    const ElementId pruned =
        add_vertex(candidate, { 0.5F, 0.0F, 0.0F });
    const ElementId retained =
        add_vertex(candidate, { 1.0F, 0.0F, 0.0F });
    candidate.vertices.erase(
        std::ranges::find_if(candidate.vertices, [pruned](const Vertex& vertex) {
            return vertex.id == pruned;
        }));
    candidate.faces.front().vertices = { a, retained, b, c };

    const std::map<std::pair<ElementId, ElementId>, std::vector<ElementId>>
        splits { { { a, b }, { pruned, retained } } };
    document_internal::remap_normal_edge_states_after_edge_splits(
        source, candidate, splits);

    EXPECT_TRUE(candidate.hard_normal_edges.empty());
    ASSERT_EQ(candidate.soft_normal_edges.size(), 2U);
    EXPECT_EQ(candidate.soft_normal_edges[0], make_edge(a, retained));
    EXPECT_EQ(candidate.soft_normal_edges[1], make_edge(b, retained));
}

TEST(QdrDocumentOperationNormalShadingTests,
     BatchNormalModeAssignmentsCanonicalizeAndUseLastValidAssignment)
{
    BentCornerFixture fixture = make_bent_corner_document();
    const Edge boundary = make_edge(fixture.left_bottom, fixture.shared_bottom);
    fixture.document.hard_normal_edges = {
        {boundary.b, boundary.a},
        fixture.shared_edge,
    };
    fixture.document.soft_normal_edges = {fixture.shared_edge};
    const std::array<document_internal::EdgeNormalModeAssignment, 4>
        assignments {{
            {fixture.shared_edge, EdgeNormalMode::Soft},
            {fixture.shared_edge, EdgeNormalMode::Default},
            {fixture.shared_edge, EdgeNormalMode::Hard},
            {make_edge(fixture.shared_bottom, 999U), EdgeNormalMode::Soft},
        }};

    ASSERT_TRUE(document_internal::set_edge_normal_modes(
        fixture.document, assignments));

    ASSERT_EQ(fixture.document.hard_normal_edges.size(), 2U);
    EXPECT_EQ(fixture.document.hard_normal_edges[0], boundary);
    EXPECT_EQ(fixture.document.hard_normal_edges[1], fixture.shared_edge);
    EXPECT_TRUE(fixture.document.soft_normal_edges.empty());
}

TEST(QdrDocumentOperationNormalShadingTests,
     BatchNormalModeCopyUsesOneSourceSnapshotWhenSourceIsTarget)
{
    BentCornerFixture fixture = make_bent_corner_document();
    const Edge boundary = make_edge(fixture.left_bottom, fixture.shared_bottom);
    fixture.document.hard_normal_edges = {boundary};
    fixture.document.soft_normal_edges = {fixture.shared_edge};
    const std::array<document_internal::EdgeNormalModeCopy, 2> copies {{
        {boundary, fixture.shared_edge},
        {fixture.shared_edge, boundary},
    }};

    ASSERT_TRUE(document_internal::copy_edge_normal_modes(
        fixture.document, fixture.document, copies));

    ASSERT_EQ(fixture.document.hard_normal_edges.size(), 1U);
    ASSERT_EQ(fixture.document.soft_normal_edges.size(), 1U);
    EXPECT_EQ(fixture.document.hard_normal_edges.front(), fixture.shared_edge);
    EXPECT_EQ(fixture.document.soft_normal_edges.front(), boundary);
}
