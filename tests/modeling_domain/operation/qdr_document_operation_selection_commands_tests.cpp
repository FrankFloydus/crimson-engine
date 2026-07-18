////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_test_helpers.hpp"

#include <quader/modeling/neutral/polygon/document.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <ranges>
#include <span>
#include <vector>

namespace {

using namespace quader_poly;
using namespace qdr_test;

/// Stores a test face together with its newly allocated vertex IDs.
struct AddedFace {
  ElementId id = kInvalidElementId;
  std::vector<ElementId> vertices;
};

AddedFace add_independent_face(Document& document,
                               std::span<const quader::QVec3> positions,
                               std::uint32_t material_slot = 0)
{
    AddedFace result;
    result.vertices.reserve(positions.size());
    for (const quader::QVec3 position : positions) {
        result.vertices.push_back(add_vertex(document, position));
    }
    result.id = add_face(document, result.vertices, material_slot);
    return result;
}

AddedFace add_quad(Document& document, float x_offset)
{
    const std::array<quader::QVec3, 4> points {{
        { x_offset, 0.0F, 0.0F },
        { x_offset + 1.0F, 0.0F, 0.0F },
        { x_offset + 1.0F, 1.0F, 0.0F },
        { x_offset, 1.0F, 0.0F },
    }};
    return add_independent_face(document, points);
}

TEST(QdrDocumentOperationSelectionCommandsTests,
     SelectAllAndInversePreserveActiveAndReportNoOps)
{
    const QuadFixture fixture = make_quad_document();

    Selection vertices = vertex_selection(
        { fixture.bottom_left }, fixture.bottom_left);
    EXPECT_TRUE(apply_component_selection_command(
        fixture.document, vertices, ComponentSelectionCommand::SelectAll));
    EXPECT_EQ(vertices.vertices.size(), fixture.document.vertices.size());
    EXPECT_EQ(vertices.active_vertex, fixture.bottom_left);
    EXPECT_FALSE(apply_component_selection_command(
        fixture.document, vertices, ComponentSelectionCommand::SelectAll));
    EXPECT_TRUE(apply_component_selection_command(
        fixture.document, vertices,
        ComponentSelectionCommand::SelectInverse));
    EXPECT_TRUE(vertices.empty());
    EXPECT_FALSE(vertices.has_active);

    const std::vector<Edge> edges = document_edges(fixture.document);
    ASSERT_FALSE(edges.empty());
    Selection edge_selection_state = edge_selection({ edges.front() },
                                                     edges.front());
    EXPECT_TRUE(apply_component_selection_command(
        fixture.document, edge_selection_state,
        ComponentSelectionCommand::SelectAll));
    EXPECT_EQ(edge_selection_state.edges.size(), edges.size());
    EXPECT_EQ(edge_selection_state.active_edge, edges.front());

    Selection faces = face_selection({ fixture.face }, fixture.face);
    EXPECT_FALSE(apply_component_selection_command(
        fixture.document, faces, ComponentSelectionCommand::SelectAll));
    EXPECT_EQ(faces.active_face, fixture.face);
}

TEST(QdrDocumentOperationSelectionCommandsTests,
     DirectionUsesComparisonSpaceAndDefaultAngularTolerance)
{
    Document document;
    const std::array<quader::QVec3, 3> seed_points {{
        { 0.0F, 0.0F, 0.0F },
        { 1.0F, 0.1F, 0.0F },
        { 0.2F, 2.0F, 0.0F },
    }};
    const std::array<quader::QVec3, 3> candidate_points {{
        { 3.0F, 0.0F, 0.0F },
        { 4.0F, -0.1F, 0.0F },
        { 3.2F, 2.0F, 0.0F },
    }};
    const AddedFace seed_face = add_independent_face(document, seed_points);
    const AddedFace candidate_face =
        add_independent_face(document, candidate_points);
    const Edge seed = make_edge(seed_face.vertices[0], seed_face.vertices[1]);
    const Edge candidate =
        make_edge(candidate_face.vertices[0], candidate_face.vertices[1]);

    Selection local = edge_selection({ seed }, seed);
    const std::vector<quader::QVec3> local_directions =
        selected_edge_directions_in_comparison_space(document, local, {});
    EXPECT_FALSE(add_edges_matching_directions(document, local, {},
                                               local_directions));
    EXPECT_FALSE(selection_contains(local, candidate));

    Transform3 flattened;
    flattened.y_axis = { 0.0F, 0.01F, 0.0F };
    Selection comparison = edge_selection({ seed }, seed);
    const std::vector<quader::QVec3> comparison_directions =
        selected_edge_directions_in_comparison_space(
            document, comparison, flattened);
    EXPECT_TRUE(add_edges_matching_directions(
        document, comparison, flattened, comparison_directions));
    EXPECT_TRUE(selection_contains(comparison, candidate));
    EXPECT_EQ(comparison.active_edge, seed);
    EXPECT_FALSE(add_edges_matching_directions(
        document, comparison, flattened, comparison_directions));
}

TEST(QdrDocumentOperationSelectionCommandsTests,
     LoopsUnionAllOriginalSeedsAndRingsStopAtNonQuads)
{
    Document loops_document;
    const AddedFace first = add_quad(loops_document, 0.0F);
    const AddedFace second = add_quad(loops_document, 3.0F);
    const Edge first_seed = make_edge(first.vertices[0], first.vertices[1]);
    const Edge second_seed =
        make_edge(second.vertices[0], second.vertices[1]);
    Selection loops = edge_selection({ first_seed, second_seed }, first_seed);

    EXPECT_TRUE(add_selected_edge_loops(loops_document, loops));
    EXPECT_EQ(loops.edges.size(), 8U);
    EXPECT_EQ(loops.active_edge, first_seed);
    EXPECT_FALSE(add_selected_edge_loops(loops_document, loops));

    Document ring_document;
    const ElementId a = add_vertex(ring_document, { 0.0F, 0.0F, 0.0F });
    const ElementId b = add_vertex(ring_document, { 1.0F, 0.0F, 0.0F });
    const ElementId c = add_vertex(ring_document, { 1.0F, 1.0F, 0.0F });
    const ElementId d = add_vertex(ring_document, { 0.0F, 1.0F, 0.0F });
    ASSERT_NE(add_face(ring_document,
                       std::array<ElementId, 4> { a, b, c, d }),
              kInvalidElementId);
    const ElementId e = add_vertex(ring_document, { 2.0F, -0.5F, 0.0F });
    const ElementId f = add_vertex(ring_document, { 2.5F, 0.5F, 0.0F });
    const ElementId g = add_vertex(ring_document, { 2.0F, 1.5F, 0.0F });
    ASSERT_NE(add_face(
                  ring_document,
                  std::array<ElementId, 5> { b, e, f, g, c }),
              kInvalidElementId);

    const Edge ring_seed = make_edge(a, d);
    const Edge quad_opposite = make_edge(b, c);
    Selection ring = edge_selection({ ring_seed }, ring_seed);
    EXPECT_TRUE(add_selected_edge_rings(ring_document, ring));
    EXPECT_EQ(ring.edges.size(), 2U);
    EXPECT_TRUE(selection_contains(ring, quad_opposite));
    EXPECT_TRUE(std::ranges::none_of(
        ring.edges, [&](Edge edge) {
            return edge != ring_seed && edge != quad_opposite;
        }));
}

TEST(QdrDocumentOperationSelectionCommandsTests,
     FaceMatchingIsAdditiveAndCoplanarityIsExact)
{
    Document document;
    const std::array<quader::QVec3, 4> base_points {{
        { 0.0F, 0.0F, 0.0F },
        { 1.0F, 0.0F, 0.0F },
        { 1.0F, 1.0F, 0.0F },
        { 0.0F, 1.0F, 0.0F },
    }};
    const std::array<quader::QVec3, 4> opposite_points {{
        { 3.0F, 1.0F, 0.0F },
        { 4.0F, 1.0F, 0.0F },
        { 4.0F, 0.0F, 0.0F },
        { 3.0F, 0.0F, 0.0F },
    }};
    const std::array<quader::QVec3, 4> offset_points {{
        { 0.0F, 0.0F, 0.000001F },
        { 1.0F, 0.0F, 0.000001F },
        { 1.0F, 1.0F, 0.000001F },
        { 0.0F, 1.0F, 0.000001F },
    }};
    const AddedFace base = add_independent_face(document, base_points, 7U);
    const AddedFace opposite =
        add_independent_face(document, opposite_points, 3U);
    const AddedFace offset = add_independent_face(document, offset_points, 7U);

    Selection material = face_selection({ base.id }, base.id);
    EXPECT_TRUE(add_faces_with_selected_material_slots(document, material));
    EXPECT_TRUE(selection_contains_face(material, offset.id));
    EXPECT_FALSE(selection_contains_face(material, opposite.id));
    EXPECT_EQ(material.active_face, base.id);

    Selection coplanar = face_selection({ base.id }, base.id);
    const std::vector<SelectionComparisonPlane> planes =
        selected_face_planes_in_comparison_space(document, coplanar, {});
    ASSERT_EQ(planes.size(), 1U);
    EXPECT_TRUE(add_faces_coplanar_with_planes(document, coplanar, {},
                                               planes));
    EXPECT_TRUE(selection_contains_face(coplanar, opposite.id));
    EXPECT_FALSE(selection_contains_face(coplanar, offset.id));
    EXPECT_EQ(coplanar.active_face, base.id);
    EXPECT_FALSE(add_faces_coplanar_with_planes(document, coplanar, {},
                                                planes));

    const float x = 0.37F;
    const float y = -0.61F;
    const float z = 0.83F;
    const float sx = std::sin(x);
    const float cx = std::cos(x);
    const float sy = std::sin(y);
    const float cy = std::cos(y);
    const float sz = std::sin(z);
    const float cz = std::cos(z);
    Transform3 rotated {
        .x_axis = { cz * cy, sz * cy, -sy },
        .y_axis = {
            cz * sy * sx - sz * cx,
            sz * sy * sx + cz * cx,
            cy * sx,
        },
        .z_axis = {
            cz * sy * cx + sz * sx,
            sz * sy * cx - cz * sx,
            cy * cx,
        },
        .origin = { 7.25F, -3.5F, 2.0F },
    };
    Selection rotated_coplanar = face_selection({ base.id }, base.id);
    const std::vector<SelectionComparisonPlane> rotated_planes =
        selected_face_planes_in_comparison_space(
            document, rotated_coplanar, rotated);
    ASSERT_EQ(rotated_planes.size(), 1U);
    EXPECT_TRUE(add_faces_coplanar_with_planes(
        document, rotated_coplanar, rotated, rotated_planes));
    EXPECT_TRUE(selection_contains_face(rotated_coplanar, opposite.id));
    EXPECT_FALSE(selection_contains_face(rotated_coplanar, offset.id));
}

TEST(QdrDocumentOperationSelectionCommandsTests,
     FaceNormalMatchingIsExactFacingSensitiveAndRejectsDegenerates)
{
    Document document;
    const AddedFace seed = add_independent_face(
        document,
        std::array<quader::QVec3, 3> {{
            { 0.0F, 0.0F, 0.0F },
            { 1.0F, 0.0F, 0.0F },
            { 0.0F, 1.0F, 0.0F },
        }});
    const AddedFace same_facing = add_independent_face(
        document,
        std::array<quader::QVec3, 3> {{
            { 3.0F, 0.0F, 2.0F },
            { 4.0F, 0.0F, 2.0F },
            { 3.0F, 1.0F, 2.0F },
        }});
    const AddedFace opposite_facing = add_independent_face(
        document,
        std::array<quader::QVec3, 3> {{
            { 6.0F, 0.0F, 0.0F },
            { 6.0F, 1.0F, 0.0F },
            { 7.0F, 0.0F, 0.0F },
        }});
    const AddedFace tilted = add_independent_face(
        document,
        std::array<quader::QVec3, 3> {{
            { 9.0F, 0.0F, 0.0F },
            { 10.0F, 0.0F, 0.0F },
            { 9.0F, 1.0F, 0.000001F },
        }});
    const ElementId degenerate_a =
        add_vertex(document, { 12.0F, 0.0F, 0.0F });
    const ElementId degenerate_b =
        add_vertex(document, { 13.0F, 0.0F, 0.0F });
    const ElementId degenerate_c =
        add_vertex(document, { 14.0F, 0.0F, 0.0F });
    const ElementId degenerate_id = document.next_face_id++;
    document.faces.push_back(Face {
        .id = degenerate_id,
        .vertices = { degenerate_a, degenerate_b, degenerate_c },
    });

    Selection selection = face_selection({ seed.id }, seed.id);
    const std::vector<quader::QVec3> normals =
        selected_face_normals_in_comparison_space(document, selection, {});
    ASSERT_EQ(normals.size(), 1U);
    EXPECT_TRUE(add_faces_matching_normals(
        document, selection, {}, normals));
    EXPECT_TRUE(selection_contains_face(selection, same_facing.id));
    EXPECT_FALSE(selection_contains_face(selection, opposite_facing.id));
    EXPECT_FALSE(selection_contains_face(selection, tilted.id));
    EXPECT_FALSE(selection_contains_face(selection, degenerate_id));
    EXPECT_EQ(selection.active_face, seed.id);

    Transform3 mirrored_nonuniform;
    mirrored_nonuniform.x_axis = { -2.0F, 0.0F, 0.0F };
    mirrored_nonuniform.y_axis = { 0.0F, 3.0F, 0.0F };
    mirrored_nonuniform.z_axis = { 0.0F, 0.0F, 0.5F };
    const std::vector<quader::QVec3> mirrored =
        selected_face_normals_in_comparison_space(
            document, face_selection({ seed.id }, seed.id),
            mirrored_nonuniform);
    ASSERT_EQ(mirrored.size(), 1U);
    EXPECT_FLOAT_EQ(mirrored.front().x, 0.0F);
    EXPECT_FLOAT_EQ(mirrored.front().y, 0.0F);
    EXPECT_FLOAT_EQ(mirrored.front().z, 1.0F);
    Selection cross_transform = face_selection({ seed.id }, seed.id);
    EXPECT_TRUE(add_faces_matching_normals(
        document, cross_transform, mirrored_nonuniform, normals));
    EXPECT_TRUE(selection_contains_face(cross_transform, same_facing.id));
}

TEST(QdrDocumentOperationSelectionCommandsTests,
     NGonsReplaceSelectionAndPreserveActiveWhenPossible)
{
    Document document;
    const AddedFace triangle = add_independent_face(
        document,
        std::array<quader::QVec3, 3> {{
            { 0.0F, 0.0F, 0.0F },
            { 1.0F, 0.0F, 0.0F },
            { 0.0F, 1.0F, 0.0F },
        }});
    const AddedFace pentagon = add_independent_face(
        document,
        std::array<quader::QVec3, 5> {{
            { 2.0F, 0.0F, 0.0F },
            { 3.0F, 0.0F, 0.0F },
            { 3.5F, 0.8F, 0.0F },
            { 2.5F, 1.5F, 0.0F },
            { 1.5F, 0.8F, 0.0F },
        }});
    const AddedFace hexagon = add_independent_face(
        document,
        std::array<quader::QVec3, 6> {{
            { 5.0F, 0.0F, 0.0F },
            { 6.0F, 0.0F, 0.0F },
            { 6.5F, 0.8F, 0.0F },
            { 6.0F, 1.6F, 0.0F },
            { 5.0F, 1.6F, 0.0F },
            { 4.5F, 0.8F, 0.0F },
        }});

    Selection selection =
        face_selection({ triangle.id, pentagon.id }, pentagon.id);
    EXPECT_TRUE(replace_selection_with_ngons(document, selection));
    EXPECT_EQ(selection.faces,
              (std::vector<ElementId> { pentagon.id, hexagon.id }));
    EXPECT_EQ(selection.active_face, pentagon.id);
    EXPECT_FALSE(replace_selection_with_ngons(document, selection));
}

TEST(QdrDocumentOperationSelectionCommandsTests,
     GrowAndShrinkUseOneFaceStepInEveryComponentMode)
{
    const QuadFixture quad = make_quad_document();
    Selection vertices = vertex_selection({ quad.bottom_left },
                                          quad.bottom_left);
    EXPECT_TRUE(grow_selection(quad.document, vertices));
    EXPECT_EQ(vertices.vertices.size(), 4U);
    EXPECT_EQ(vertices.active_vertex, quad.bottom_left);

    Selection vertex_boundary = vertex_selection(
        { quad.bottom_left, quad.bottom_right, quad.top_left },
        quad.bottom_left);
    EXPECT_TRUE(shrink_selection(quad.document, vertex_boundary));
    EXPECT_TRUE(vertex_boundary.empty());

    const std::vector<Edge> quad_edges = document_edges(quad.document);
    ASSERT_EQ(quad_edges.size(), 4U);
    Selection edges = edge_selection({ quad_edges.front() },
                                     quad_edges.front());
    EXPECT_TRUE(grow_selection(quad.document, edges));
    EXPECT_EQ(edges.edges.size(), 4U);
    EXPECT_EQ(edges.active_edge, quad_edges.front());

    Selection edge_boundary = edge_selection(
        { quad_edges[0], quad_edges[1], quad_edges[2] }, quad_edges[0]);
    EXPECT_TRUE(shrink_selection(quad.document, edge_boundary));
    EXPECT_TRUE(edge_boundary.empty());

    Document face_document;
    const ElementId a = add_vertex(face_document, { 0.0F, 0.0F, 0.0F });
    const ElementId b = add_vertex(face_document, { 1.0F, 0.0F, 0.0F });
    const ElementId c = add_vertex(face_document, { 0.0F, 1.0F, 0.0F });
    const ElementId d = add_vertex(face_document, { 2.0F, 0.0F, 0.0F });
    const ElementId e = add_vertex(face_document, { 2.0F, 1.0F, 0.0F });
    const ElementId f = add_vertex(face_document, { 3.0F, 0.0F, 0.0F });
    const ElementId g = add_vertex(face_document, { 3.0F, 1.0F, 0.0F });
    const ElementId first = add_face(
        face_document, std::array<ElementId, 3> { a, b, c });
    const ElementId second = add_face(
        face_document, std::array<ElementId, 3> { b, d, e });
    const ElementId third = add_face(
        face_document, std::array<ElementId, 3> { d, f, g });
    ASSERT_NE(first, kInvalidElementId);
    ASSERT_NE(second, kInvalidElementId);
    ASSERT_NE(third, kInvalidElementId);

    Selection faces = face_selection({ first }, first);
    EXPECT_TRUE(grow_selection(face_document, faces));
    EXPECT_TRUE(selection_contains_face(faces, second));
    EXPECT_FALSE(selection_contains_face(faces, third));
    EXPECT_TRUE(shrink_selection(face_document, faces));
    EXPECT_EQ(faces.faces, std::vector<ElementId> { first });
    EXPECT_EQ(faces.active_face, first);
}

TEST(QdrDocumentOperationSelectionCommandsTests,
     ExactGeometryIgnoresIdsOrderMaterialsAndWinding)
{
    Document left;
    const ElementId a = add_vertex(left, { 0.0F, 0.0F, 0.0F });
    const ElementId b = add_vertex(left, { 1.0F, 0.0F, 0.0F });
    const ElementId c = add_vertex(left, { 1.0F, 1.0F, 0.0F });
    const ElementId d = add_vertex(left, { 0.0F, 1.0F, 0.0F });
    ASSERT_NE(add_face(left, std::array<ElementId, 3> { a, b, c }, 1U),
              kInvalidElementId);
    ASSERT_NE(add_face(left, std::array<ElementId, 3> { a, c, d }, 2U),
              kInvalidElementId);

    Document reordered;
    reordered.vertices = {
        { 40U, { 0.0F, 1.0F, 0.0F } },
        { 20U, { 1.0F, 0.0F, 0.0F } },
        { 10U, { 0.0F, 0.0F, 0.0F } },
        { 30U, { 1.0F, 1.0F, 0.0F } },
    };
    reordered.faces = {
        { 90U, { 40U, 30U, 10U }, {}, 99U,
          SurfaceShading::Smooth },
        { 80U, { 30U, 20U, 10U }, {}, 77U,
          SurfaceShading::Authored },
    };
    reordered.hard_normal_edges = { make_edge(10U, 20U) };
    EXPECT_TRUE(has_same_geometry(left, reordered));

    Document moved = reordered;
    moved.vertices.front().position.x = 0.25F;
    EXPECT_FALSE(has_same_geometry(left, moved));

    Document other_diagonal = reordered;
    other_diagonal.faces = {
        { 80U, { 10U, 20U, 40U } },
        { 90U, { 20U, 30U, 40U } },
    };
    EXPECT_FALSE(has_same_geometry(left, other_diagonal));
}

TEST(QdrDocumentOperationSelectionCommandsTests,
     ExactGeometryDistinguishesCoincidentVertexIncidence)
{
    const auto make_vertices = [](Document& document) {
        std::array<std::array<ElementId, 4>, 3> groups;
        const std::array<quader::QVec3, 3> positions {{
            { 0.0F, 0.0F, 0.0F },
            { 1.0F, 0.0F, 0.0F },
            { 0.0F, 1.0F, 0.0F },
        }};
        for (std::size_t group = 0U; group < groups.size(); ++group) {
            for (ElementId& vertex : groups[group]) {
                vertex = add_vertex(document, positions[group]);
            }
        }
        return groups;
    };
    const auto add_triangle = [](Document& document, ElementId a,
                                 ElementId b, ElementId c) {
        ASSERT_NE(add_face(document,
                           std::array<ElementId, 3> { a, b, c }),
                  kInvalidElementId);
    };

    Document disconnected;
    const auto disconnected_vertices = make_vertices(disconnected);
    for (std::size_t component = 0U; component < 2U; ++component) {
        const std::size_t first = component * 2U;
        const std::size_t second = first + 1U;
        add_triangle(disconnected, disconnected_vertices[0][first],
                     disconnected_vertices[1][first],
                     disconnected_vertices[2][first]);
        add_triangle(disconnected, disconnected_vertices[0][second],
                     disconnected_vertices[1][second],
                     disconnected_vertices[2][second]);
        add_triangle(disconnected, disconnected_vertices[0][first],
                     disconnected_vertices[1][second],
                     disconnected_vertices[2][second]);
        add_triangle(disconnected, disconnected_vertices[0][second],
                     disconnected_vertices[1][first],
                     disconnected_vertices[2][first]);
    }

    Document connected;
    const auto connected_vertices = make_vertices(connected);
    for (std::size_t index = 0U; index < 4U; ++index) {
        add_triangle(connected, connected_vertices[0][index],
                     connected_vertices[1][index],
                     connected_vertices[2][index]);
        add_triangle(connected, connected_vertices[0][index],
                     connected_vertices[1][(index + 1U) % 4U],
                     connected_vertices[2][(index + 2U) % 4U]);
    }

    // Every vertex has the same position, incident-face count, neighboring
    // position multiset, and incident face-position loops in both documents.
    // Only the actual vertex-to-face incidence distinguishes the meshes.
    EXPECT_FALSE(has_same_geometry(disconnected, connected));
}

} // namespace
