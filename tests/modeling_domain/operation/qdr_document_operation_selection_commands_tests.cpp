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

/// Stores one dense face-selection shrink scenario and its ordered result.
struct BulkFaceShrinkFixture {
  Document document;
  std::vector<ElementId> selected_faces;
  std::vector<ElementId> retained_faces;
  ElementId active_face = kInvalidElementId;
};

BulkFaceShrinkFixture make_bulk_face_shrink_fixture(
    std::size_t selected_face_count)
{
    BulkFaceShrinkFixture fixture;
    std::vector<ElementId> document_selected_faces;
    document_selected_faces.reserve(selected_face_count);
    ElementId boundary_vertex = kInvalidElementId;
    ElementId boundary_face = kInvalidElementId;
    const std::size_t boundary_index = selected_face_count / 2U;

    for (std::size_t index = 0U; index < selected_face_count; ++index) {
        const AddedFace face = add_quad(
            fixture.document, static_cast<float>(index * 2U));
        document_selected_faces.push_back(face.id);
        if (index == boundary_index) {
            boundary_vertex = face.vertices.front();
            boundary_face = face.id;
        }
    }

    for (std::size_t index = 1U; index < selected_face_count; ++index) {
        add_quad(fixture.document,
                 10000.0F + static_cast<float>(index * 2U));
    }
    const ElementId boundary_b =
        add_vertex(fixture.document, { -2.0F, -1.0F, 1.0F });
    const ElementId boundary_c =
        add_vertex(fixture.document, { -1.0F, -2.0F, 1.0F });
    EXPECT_NE(add_face(
                  fixture.document,
                  std::array<ElementId, 3> {
                      boundary_vertex, boundary_b, boundary_c }),
              kInvalidElementId);

    fixture.selected_faces.assign(document_selected_faces.rbegin(),
                                  document_selected_faces.rend());
    fixture.retained_faces = fixture.selected_faces;
    std::erase(fixture.retained_faces, boundary_face);
    fixture.active_face =
        document_selected_faces[selected_face_count / 4U];
    return fixture;
}

TEST(QdrDocumentOperationSelectionCommandsTests,
     ConnectedPartSelectionKeepsDisconnectedPartsSeparateInEveryMode)
{
    Document document;
    const AddedFace first = add_quad(document, 0.0F);
    const AddedFace second = add_quad(document, 3.0F);
    const ElementId isolated = add_vertex(document, { 8.0F, 0.0F, 0.0F });

    const Selection vertices = connected_component_selection(
        document, PickResult { .hit = true,
                               .kind = ElementKind::Vertex,
                               .vertex_id = first.vertices.back() });
    EXPECT_EQ(vertices.mode, SelectionMode::Vertex);
    EXPECT_EQ(vertices.vertices, first.vertices);
    EXPECT_TRUE(vertices.edges.empty());
    EXPECT_TRUE(vertices.faces.empty());
    EXPECT_TRUE(vertices.has_active);
    EXPECT_EQ(vertices.active_kind, ElementKind::Vertex);
    EXPECT_EQ(vertices.active_vertex, first.vertices.back());
    EXPECT_FALSE(selection_contains(vertices, isolated));

    const Edge reversed_seed { first.vertices[1], first.vertices[0] };
    const Selection edges = connected_component_selection(
        document, PickResult { .hit = true,
                               .kind = ElementKind::Edge,
                               .edge = reversed_seed });
    EXPECT_EQ(edges.mode, SelectionMode::Edge);
    EXPECT_TRUE(edges.vertices.empty());
    ASSERT_EQ(edges.edges.size(), 4U);
    EXPECT_TRUE(edges.faces.empty());
    EXPECT_TRUE(edges.has_active);
    EXPECT_EQ(edges.active_kind, ElementKind::Edge);
    EXPECT_EQ(edges.active_edge,
              make_edge(reversed_seed.a, reversed_seed.b));
    for (std::size_t index = 0U; index < first.vertices.size(); ++index) {
        EXPECT_TRUE(selection_contains(
            edges, make_edge(first.vertices[index],
                             first.vertices[(index + 1U) % 4U])));
    }

    const Selection faces = connected_component_selection(
        document, PickResult { .hit = true,
                               .kind = ElementKind::Face,
                               .face_id = second.id });
    EXPECT_EQ(faces.mode, SelectionMode::Face);
    EXPECT_TRUE(faces.vertices.empty());
    EXPECT_TRUE(faces.edges.empty());
    EXPECT_EQ(faces.faces, (std::vector<ElementId> { second.id }));
    EXPECT_TRUE(faces.has_active);
    EXPECT_EQ(faces.active_kind, ElementKind::Face);
    EXPECT_EQ(faces.active_face, second.id);
}

TEST(QdrDocumentOperationSelectionCommandsTests,
     ConnectedPartSelectionDoesNotJoinCoincidentVertexPositions)
{
    Document document;
    const AddedFace first = add_quad(document, 0.0F);
    const AddedFace coincident = add_quad(document, 0.0F);

    const Selection vertices = connected_component_selection(
        document, PickResult { .hit = true,
                               .kind = ElementKind::Vertex,
                               .vertex_id = coincident.vertices.front() });
    EXPECT_EQ(vertices.vertices, coincident.vertices);
    const Selection edges = connected_component_selection(
        document, PickResult {
                      .hit = true,
                      .kind = ElementKind::Edge,
                      .edge = make_edge(coincident.vertices[0],
                                        coincident.vertices[1]) });
    EXPECT_EQ(edges.edges.size(), 4U);
    EXPECT_TRUE(std::ranges::all_of(edges.edges, [&](Edge edge) {
        return std::ranges::find(coincident.vertices, edge.a) !=
                   coincident.vertices.end() &&
               std::ranges::find(coincident.vertices, edge.b) !=
                   coincident.vertices.end();
    }));
    const Selection faces = connected_component_selection(
        document, PickResult { .hit = true,
                               .kind = ElementKind::Face,
                               .face_id = first.id });
    EXPECT_EQ(faces.faces, (std::vector<ElementId> { first.id }));
}

TEST(QdrDocumentOperationSelectionCommandsTests,
     ConnectedPartSelectionTraversesSharedVertexChainsInEveryMode)
{
    Document document;
    const AddedFace first = add_quad(document, 0.0F);
    const ElementId second_b = add_vertex(document, { 2.0F, 1.0F, 0.0F });
    const ElementId second_c = add_vertex(document, { 1.0F, 2.0F, 0.0F });
    const ElementId second = add_face(
        document, std::array<ElementId, 3> {
                      first.vertices[2], second_b, second_c });
    const ElementId third_b = add_vertex(document, { 1.0F, 3.0F, 0.0F });
    const ElementId third_c = add_vertex(document, { 0.0F, 3.0F, 0.0F });
    const ElementId third = add_face(
        document, std::array<ElementId, 3> {
                      second_c, third_b, third_c });
    const AddedFace disconnected = add_quad(document, 4.0F);
    ASSERT_NE(second, kInvalidElementId);
    ASSERT_NE(third, kInvalidElementId);

    const Selection vertices = connected_component_selection(
        document, PickResult { .hit = true,
                               .kind = ElementKind::Vertex,
                               .vertex_id = first.vertices.front() });
    EXPECT_EQ(vertices.vertices,
              (std::vector<ElementId> {
                  first.vertices[0], first.vertices[1], first.vertices[2],
                  first.vertices[3], second_b, second_c, third_b, third_c }));

    const Selection edges = connected_component_selection(
        document, PickResult {
                      .hit = true,
                      .kind = ElementKind::Edge,
                      .edge = make_edge(first.vertices[0], first.vertices[1]) });
    EXPECT_EQ(edges.edges.size(), 10U);
    EXPECT_TRUE(selection_contains(edges, make_edge(third_b, third_c)));
    EXPECT_FALSE(selection_contains(
        edges, make_edge(disconnected.vertices[0], disconnected.vertices[1])));

    const Selection faces = connected_component_selection(
        document, PickResult { .hit = true,
                               .kind = ElementKind::Face,
                               .face_id = first.id });
    EXPECT_EQ(faces.faces,
              (std::vector<ElementId> { first.id, second, third }));
    EXPECT_EQ(faces.active_face, first.id);
}

TEST(QdrDocumentOperationSelectionCommandsTests,
     ConnectedPartSelectionSupportsIsolatedVerticesAndRejectsInvalidSeeds)
{
    Document document;
    const AddedFace face = add_quad(document, 0.0F);
    const ElementId isolated = add_vertex(document, { 3.0F, 0.0F, 0.0F });
    const Selection isolated_selection = connected_component_selection(
        document, PickResult { .hit = true,
                               .kind = ElementKind::Vertex,
                               .vertex_id = isolated });
    EXPECT_EQ(isolated_selection.vertices,
              (std::vector<ElementId> { isolated }));
    EXPECT_EQ(isolated_selection.active_vertex, isolated);
    EXPECT_TRUE(isolated_selection.has_active);

    const std::array<PickResult, 8> invalid_picks {{
        { .kind = ElementKind::Face, .face_id = face.id },
        { .hit = true, .kind = ElementKind::Vertex },
        { .hit = true, .kind = ElementKind::Vertex,
          .vertex_id = document.next_vertex_id },
        { .hit = true, .kind = ElementKind::Edge,
          .edge = make_edge(face.vertices[0], face.vertices[2]) },
        { .hit = true, .kind = ElementKind::Edge,
          .edge = make_edge(face.vertices[0], isolated) },
        { .hit = true, .kind = ElementKind::Edge,
          .edge = { face.vertices[0], face.vertices[0] } },
        { .hit = true, .kind = ElementKind::Face },
        { .hit = true, .kind = ElementKind::Face,
          .face_id = document.next_face_id },
    }};
    for (const PickResult& pick : invalid_picks) {
        const Selection selection = connected_component_selection(document, pick);
        EXPECT_TRUE(selection.empty());
        EXPECT_FALSE(selection.has_active);
    }
    EXPECT_TRUE(connected_component_selection(
                    Document {}, PickResult { .hit = true,
                                               .vertex_id = isolated }).empty());
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
     IsolatedOuterCornerLoopsStayAtTheirSeedsAndRingsStopAtNonQuads)
{
    Document loops_document;
    const AddedFace first = add_quad(loops_document, 0.0F);
    const AddedFace second = add_quad(loops_document, 3.0F);
    const Edge first_seed = make_edge(first.vertices[0], first.vertices[1]);
    const Edge second_seed =
        make_edge(second.vertices[0], second.vertices[1]);
    Selection loops = edge_selection({ first_seed, second_seed }, first_seed);

    EXPECT_FALSE(add_selected_edge_loops(loops_document, loops));
    EXPECT_EQ(loops.edges.size(), 2U);
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
     CoplanarityHandlesAuthoredRoundingWithoutAcceptingWarpedFaces)
{
    for (const float translation : {0.0F, 1000.0F}) {
        SCOPED_TRACE(translation);
        Document document;
        const auto add_patch = [&](float x_offset, float plane_offset,
                                   float warp) {
            std::array<quader::QVec3, 4> points {{
                {x_offset, 0.1F, 0.0F},
                {x_offset + 1.0F, 0.1F, 0.0F},
                {x_offset + 1.0F, 1.1F, 0.0F},
                {x_offset, 1.1F, 0.0F},
            }};
            for (quader::QVec3& point : points) {
                point.z = 0.37F * point.x + 0.61F * point.y + plane_offset;
                point = point + quader::QVec3{translation, translation, translation};
            }
            points.back().z += warp;
            return add_independent_face(document, points);
        };
        const AddedFace seed = add_patch(0.2F, 0.3F, 0.0F);
        const AddedFace matching = add_patch(2.2F, 0.3F, 0.0F);
        const AddedFace offset = add_patch(4.2F, 0.4F, 0.0F);
        const AddedFace warped = add_patch(6.2F, 0.3F, 0.1F);
        Selection selection = face_selection({seed.id}, seed.id);

        EXPECT_TRUE(apply_component_selection_command(
            document, selection, ComponentSelectionCommand::SelectCoplanarFaces));
        EXPECT_EQ(selection.faces, (std::vector<ElementId>{seed.id, matching.id}));
        EXPECT_FALSE(selection_contains_face(selection, offset.id));
        EXPECT_FALSE(selection_contains_face(selection, warped.id));
        EXPECT_EQ(selection.active_face, seed.id);
        EXPECT_FALSE(apply_component_selection_command(
            document, selection, ComponentSelectionCommand::SelectCoplanarFaces));

        Selection invalid_seed = face_selection({warped.id}, warped.id);
        EXPECT_TRUE(selected_face_planes_in_comparison_space(
                        document, invalid_seed, {}).empty());
        EXPECT_FALSE(apply_component_selection_command(
            document, invalid_seed, ComponentSelectionCommand::SelectCoplanarFaces));
        EXPECT_EQ(invalid_seed.faces, (std::vector<ElementId>{warped.id}));
    }
}

TEST(QdrDocumentOperationSelectionCommandsTests,
     CoplanarityMatchesDifferentLocalPlanesInAffineComparisonSpace)
{
    Document seed_document;
    const AddedFace seed = add_quad(seed_document, 0.0F);
    const Selection selected = face_selection({seed.id}, seed.id);
    Transform3 seed_transform{
        .x_axis = {1.0F, 0.0F, 2.0F},
        .y_axis = {0.0F, 2.0F, 6.0F},
        .z_axis = {0.0F, 0.0F, -0.5F},
        .origin = {7000.0F, -3500.0F, 2000.0F},
    };
    const auto planes = selected_face_planes_in_comparison_space(
        seed_document, selected, seed_transform);
    ASSERT_EQ(planes.size(), 1U);

    Document candidate_document;
    const AddedFace matching = add_independent_face(candidate_document,
        std::array<quader::QVec3, 4>{{
            {3.0F, 0.0F, 6.0F}, {4.0F, 0.0F, 8.0F},
            {4.0F, 1.0F, 11.0F}, {3.0F, 1.0F, 9.0F},
        }});
    Transform3 candidate_transform{.origin = seed_transform.origin};
    Selection candidates;
    candidates.mode = SelectionMode::Face;
    EXPECT_TRUE(add_faces_coplanar_with_planes(
        candidate_document, candidates, candidate_transform, planes));
    EXPECT_EQ(candidates.faces, (std::vector<ElementId>{matching.id}));

    candidate_transform.origin.z += 0.01F;
    candidates = {};
    candidates.mode = SelectionMode::Face;
    EXPECT_FALSE(add_faces_coplanar_with_planes(
        candidate_document, candidates, candidate_transform, planes));
    EXPECT_TRUE(candidates.empty());

    seed_transform.z_axis = {};
    EXPECT_TRUE(selected_face_planes_in_comparison_space(
                    seed_document, selected, seed_transform).empty());
}

TEST(QdrDocumentOperationSelectionCommandsTests,
     CoplanarityRetainsFloatAffineNormalRoundoffWithoutMergingOffsets)
{
    Document document;
    const AddedFace face = add_quad(document, 0.0F);
    const Selection seed = face_selection({face.id}, face.id);
    const Transform3 seed_transform{
        .x_axis = {1.0F, 0.0F, 0.37F},
        .y_axis = {0.0F, 1.0F, 0.61F},
    };
    const auto planes = selected_face_planes_in_comparison_space(
        document, seed, seed_transform);
    ASSERT_EQ(planes.size(), 1U);
    Transform3 candidate_transform = seed_transform;
    candidate_transform.x_axis = candidate_transform.x_axis * 3.0F;
    candidate_transform.y_axis = candidate_transform.y_axis * 5.0F;
    Selection candidate;
    candidate.mode = SelectionMode::Face;
    EXPECT_TRUE(add_faces_coplanar_with_planes(
        document, candidate, candidate_transform, planes));
    EXPECT_EQ(candidate.faces, (std::vector<ElementId>{face.id}));

    candidate = {};
    candidate.mode = SelectionMode::Face;
    candidate_transform.origin.z = 0.000001F;
    EXPECT_FALSE(add_faces_coplanar_with_planes(
        document, candidate, candidate_transform, planes));
    EXPECT_TRUE(candidate.empty());
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
     FaceShrinkRetainsOrderedInteriorAtNAndTwoNScale)
{
    constexpr std::size_t kFaceCountN = 256U;
    for (const std::size_t selected_face_count :
         std::array<std::size_t, 2> {
             kFaceCountN, kFaceCountN * 2U }) {
        BulkFaceShrinkFixture fixture =
            make_bulk_face_shrink_fixture(selected_face_count);
        ASSERT_EQ(fixture.document.faces.size(),
                  selected_face_count * 2U);
        ASSERT_EQ(fixture.selected_faces.size(), selected_face_count);
        ASSERT_EQ(fixture.retained_faces.size(),
                  selected_face_count - 1U);
        Selection selection =
            face_selection({ fixture.active_face }, fixture.active_face);
        selection.faces = fixture.selected_faces;

        ASSERT_TRUE(shrink_selection(fixture.document, selection))
            << "selected face count: " << selected_face_count;
        EXPECT_EQ(selection.faces, fixture.retained_faces)
            << "selected face count: " << selected_face_count;
        EXPECT_TRUE(selection.has_active);
        EXPECT_EQ(selection.active_kind, ElementKind::Face);
        EXPECT_EQ(selection.active_face, fixture.active_face);
        EXPECT_FALSE(shrink_selection(fixture.document, selection));
        EXPECT_EQ(selection.faces, fixture.retained_faces);
        EXPECT_EQ(selection.active_face, fixture.active_face);
    }
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
