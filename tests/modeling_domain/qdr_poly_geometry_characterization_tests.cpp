////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.h"

#include <mesh/polygon/document.h>

#include <array>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <optional>
#include <set>

namespace {

using namespace quader_poly;

bool nearly_equal(float left, float right, float epsilon = 0.0001F)
{
    return std::abs(left - right) <= epsilon;
}

bool vec3_nearly_equal(quader::QVec3 left, quader::QVec3 right, float epsilon = 0.0001F)
{
    return nearly_equal(left.x, right.x, epsilon) && nearly_equal(left.y, right.y, epsilon) && nearly_equal(left.z, right.z, epsilon);
}

quader::QVec3 face_centroid_for_test(const Document& document, const Face& face)
{
    quader::QVec3 centroid;
    for (const ElementId vertex_id : face.vertices) {
        const Vertex* vertex = find_vertex(document, vertex_id);
        require_test(vertex != nullptr, "test face centroid should resolve every face vertex");
        centroid += vertex->position;
    }
    return face.vertices.empty() ? centroid : centroid / static_cast<float>(face.vertices.size());
}

quader::QVec3 document_centroid_for_test(const Document& document)
{
    quader::QVec3 centroid;
    for (const Vertex& vertex : document.vertices) {
        centroid += vertex.position;
    }
    return document.vertices.empty() ? centroid : centroid / static_cast<float>(document.vertices.size());
}

bool face_has_repeated_vertices(const Face& face)
{
    std::set<ElementId> seen;
    return std::ranges::any_of(face.vertices, [&seen](ElementId vertex_id) {
        return !seen.insert(vertex_id).second;
    });
}

bool document_has_edge(const Document& document, Edge edge)
{
    const Edge normalized = make_edge(edge.a, edge.b);
    const std::vector<Edge> edges = document_edges(document);
    return std::ranges::find(edges, normalized) != edges.end();
}

bool every_edge_is_closed(const Document& document)
{
    std::map<std::pair<ElementId, ElementId>, int> edge_counts;
    for (const Face& face : document.faces) {
        for (std::size_t index = 0; index < face.vertices.size(); ++index) {
            const Edge edge = make_edge(face.vertices[index], face.vertices[(index + 1U) % face.vertices.size()]);
            ++edge_counts[{ edge.a, edge.b }];
        }
    }
    return !edge_counts.empty() && std::ranges::all_of(edge_counts, [](const auto& entry) {
        return entry.second == 2;
    });
}

bool every_face_compiles_to_triangles(const Document& document)
{
    for (const Face& face : document.faces) {
        Document single_face;
        for (const ElementId vertex_id : face.vertices) {
            const Vertex* vertex = find_vertex(document, vertex_id);
            require_test(vertex != nullptr, "test renderability should resolve every face vertex");
            single_face.vertices.push_back(*vertex);
            single_face.next_vertex_id = std::max(single_face.next_vertex_id, vertex->id + 1);
        }
        single_face.faces.push_back(face);
        single_face.next_face_id = face.id + 1;
        if (compile_document(single_face).indices.empty()) {
            return false;
        }
    }
    return true;
}

Document make_single_quad_document()
{
    Document document;
    const ElementId bottom_left = add_vertex(document, { -1.0F, -1.0F, 0.0F });
    const ElementId bottom_right = add_vertex(document, { 1.0F, -1.0F, 0.0F });
    const ElementId top_right = add_vertex(document, { 1.0F, 1.0F, 0.0F });
    const ElementId top_left = add_vertex(document, { -1.0F, 1.0F, 0.0F });
    const std::array<ElementId, 4> face_vertices { bottom_left, bottom_right, top_right, top_left };
    require_test(add_face(document, face_vertices) != invalid_element_id, "single quad should create a face");
    return document;
}

/**
 * Stores the Occlusion Fixture data contract used by the automated test suite.
 */
struct OcclusionFixture {
    Document document;
    ElementId front_face = invalid_element_id;
    Edge back_edge;
    ElementId back_vertex = invalid_element_id;
};

OcclusionFixture make_occlusion_fixture()
{
    OcclusionFixture fixture;
    const ElementId front_bottom_left = add_vertex(fixture.document, { -2.0F, -2.0F, 0.0F });
    const ElementId front_top_left = add_vertex(fixture.document, { -2.0F, 2.0F, 0.0F });
    const ElementId front_top_right = add_vertex(fixture.document, { 2.0F, 2.0F, 0.0F });
    const ElementId front_bottom_right = add_vertex(fixture.document, { 2.0F, -2.0F, 0.0F });
    fixture.front_face = add_face(fixture.document, std::array<ElementId, 4> {
        front_bottom_left,
        front_top_left,
        front_top_right,
        front_bottom_right,
    });
    require_test(fixture.front_face != invalid_element_id, "front occluder face should be created");

    const ElementId back_bottom_left = add_vertex(fixture.document, { -0.5F, -0.5F, 1.0F });
    const ElementId back_top_left = add_vertex(fixture.document, { -0.5F, 0.5F, 1.0F });
    const ElementId back_top_right = add_vertex(fixture.document, { 0.5F, 0.5F, 1.0F });
    const ElementId back_bottom_right = add_vertex(fixture.document, { 0.5F, -0.5F, 1.0F });
    require_test(add_face(fixture.document, std::array<ElementId, 4> {
        back_bottom_left,
        back_top_left,
        back_top_right,
        back_bottom_right,
    }) != invalid_element_id,
        "back component face should be created");
    fixture.back_edge = make_edge(back_bottom_right, back_top_right);
    fixture.back_vertex = back_top_right;
    return fixture;
}

Document make_parallel_vertical_edge_faces()
{
    Document document;

    const ElementId near_bottom_left = add_vertex(document, { 0.0F, -1.0F, 1.0F });
    const ElementId near_bottom_right = add_vertex(document, { 0.0F, 1.0F, 1.0F });
    const ElementId near_top_right = add_vertex(document, { 0.0F, 1.0F, 1.5F });
    const ElementId near_top_left = add_vertex(document, { 0.0F, -1.0F, 1.5F });
    require_test(add_face(document, std::array<ElementId, 4> {
        near_bottom_left,
        near_bottom_right,
        near_top_right,
        near_top_left,
    }) != invalid_element_id,
        "near vertical face should be created");

    const ElementId far_bottom_left = add_vertex(document, { 0.0F, -1.0F, 2.0F });
    const ElementId far_bottom_right = add_vertex(document, { 0.0F, 1.0F, 2.0F });
    const ElementId far_top_right = add_vertex(document, { 0.0F, 1.0F, 2.5F });
    const ElementId far_top_left = add_vertex(document, { 0.0F, -1.0F, 2.5F });
    require_test(add_face(document, std::array<ElementId, 4> {
        far_bottom_left,
        far_bottom_right,
        far_top_right,
        far_top_left,
    }) != invalid_element_id,
        "far vertical face should be created");

    return document;
}

Document make_concave_ngon_document()
{
    Document document;
    const ElementId bottom_left = add_vertex(document, { -1.0F, -1.0F, 0.0F });
    const ElementId bottom_right = add_vertex(document, { 1.0F, -1.0F, 0.0F });
    const ElementId inner_right = add_vertex(document, { 1.0F, 0.0F, 0.0F });
    const ElementId inner_corner = add_vertex(document, { 0.0F, 0.0F, 0.0F });
    const ElementId top_inner = add_vertex(document, { 0.0F, 1.0F, 0.0F });
    const ElementId top_left = add_vertex(document, { -1.0F, 1.0F, 0.0F });
    require_test(add_face(document, std::array<ElementId, 6> {
        bottom_left,
        bottom_right,
        inner_right,
        inner_corner,
        top_inner,
        top_left,
    }) != invalid_element_id,
        "concave n-gon should be accepted as a face");
    return document;
}

void characterizes_face_pick_on_quad()
{
    const Document document = make_single_quad_document();
    PickOptions options;
    options.mode = SelectionMode::face;

    const PickResult pick = pick_element(document, { { 0.25F, -0.25F, 2.0F }, { 0.0F, 0.0F, -1.0F } }, options);
    require_test(pick.hit, "face ray should hit the quad front side");
    require_test(pick.kind == ElementKind::face, "quad face pick should return a face hit");
    require_test(pick.face_id == document.faces.front().id, "quad face pick should return the quad face id");
    require_test(nearly_equal(pick.distance, 2.0F), "quad face pick distance should be measured along the ray");
}

void characterizes_face_pick_with_near_parallel_bounds_axis()
{
    const Document document = make_single_quad_document();
    PickOptions options;
    options.mode = SelectionMode::face;

    const PickResult pick = pick_element(
        document,
        { { -1.0009F, 0.0F, 2000.0F }, { 0.0000005F, 0.0F, -1.0F } },
        options);
    require_test(pick.hit, "face picking should preserve old triangle hits when a bounded AABB axis is nearly parallel");
    require_test(pick.kind == ElementKind::face, "near-parallel bounds-axis pick should return a face hit");
    require_test(pick.face_id == document.faces.front().id, "near-parallel bounds-axis pick should preserve the hit face id");
}

void characterizes_edge_pick_radius_and_tie_break()
{
    const Document document = make_parallel_vertical_edge_faces();
    PickOptions options;
    options.mode = SelectionMode::edge;
    options.edge_radius = 0.04F;

    const Ray ray { { 0.05F, 0.0F, -2.0F }, { 0.0F, 0.0F, 1.0F } };
    PickResult pick = pick_element(document, ray, options);
    require_test(!pick.hit, "edge pick should reject an edge outside the configured radius");

    options.edge_radius = 0.06F;
    pick = pick_element(document, ray, options);
    require_test(pick.hit && pick.kind == ElementKind::edge, "edge pick should hit when inside the configured radius");
    require_test(pick.edge == make_edge(1, 2), "equally close edge pick should choose the nearer ray-depth edge");
    require_test(nearly_equal(pick.distance, 3.0F), "edge pick distance should use the nearest edge depth along the ray");
}

void characterizes_component_occlusion_by_front_face()
{
    const OcclusionFixture fixture = make_occlusion_fixture();

    PickOptions edge_options;
    edge_options.mode = SelectionMode::edge;
    edge_options.edge_radius = 0.1F;
    const PickResult edge_pick = pick_element(
        fixture.document,
        { { 0.5F, 0.0F, -2.0F }, { 0.0F, 0.0F, 1.0F } },
        edge_options);
    require_test(!edge_pick.hit, "edge pick should be occluded by a closer front face it does not belong to");

    PickOptions vertex_options;
    vertex_options.mode = SelectionMode::vertex;
    vertex_options.vertex_radius = 0.1F;
    const PickResult vertex_pick = pick_element(
        fixture.document,
        { { 0.5F, 0.5F, -2.0F }, { 0.0F, 0.0F, 1.0F } },
        vertex_options);
    require_test(!vertex_pick.hit, "vertex pick should be occluded by a closer front face it does not belong to");
}

void characterizes_concave_ngon_renderability()
{
    const CompiledMesh mesh = compile_document(make_concave_ngon_document());
    require_test(mesh.vertices.size() == 6U, "concave n-gon should compile one vertex per face loop");
    require_test(mesh.indices.size() == 12U, "concave six-sided n-gon should compile into four triangles");
    require_test(mesh.primitives.size() == 1U, "concave n-gon should compile into one primitive");
    require_test(mesh.primitives.front().index_count == 12U, "concave n-gon primitive should cover all triangulated indices");
}

void characterizes_edge_factor_from_ray()
{
    const Document document = make_single_quad_document();
    const std::optional<float> factor = edge_factor_from_ray(
        document,
        make_edge(1, 2),
        { { 0.25F, -1.0F, 2.0F }, { 0.0F, 0.0F, -1.0F } });

    require_test(factor.has_value(), "edge factor should be available for a known edge and valid ray");
    require_test(nearly_equal(*factor, 0.625F), "edge factor should match the closest point projected onto the edge");
}

void characterizes_convex_hull_rebuild_and_normal_recalculation()
{
    Document cube = make_cube({});
    const Edge merge_edge = document_edges(cube).front();
    Selection merge_selection;
    merge_selection.mode = SelectionMode::vertex;
    merge_selection.vertices = { merge_edge.a, merge_edge.b };
    merge_selection.has_active = true;
    merge_selection.active_kind = ElementKind::vertex;
    merge_selection.active_vertex = merge_edge.a;

    const Vertex* merge_a = find_vertex(cube, merge_edge.a);
    const Vertex* merge_b = find_vertex(cube, merge_edge.b);
    require_test(merge_a != nullptr && merge_b != nullptr, "hull rebuild test should resolve the merge edge");
    const quader::QVec3 expected_center = (merge_a->position + merge_b->position) / 2.0F;

    const OperationResult merge_result = merge_selected_vertices_to_center(cube, merge_selection);
    require_test(merge_result.changed, "center vertex merge should rebuild a convex cube hull");
    require_test(cube.vertices.size() == 7U, "center vertex merge should collapse exactly one cube edge endpoint");
    require_test(find_vertex(cube, merge_edge.a) != nullptr && find_vertex(cube, merge_edge.b) == nullptr,
        "center vertex merge should preserve the active survivor id");
    require_test(vec3_nearly_equal(find_vertex(cube, merge_edge.a)->position, expected_center),
        "center vertex merge should place the survivor at the selected edge center");
    require_test(std::none_of(cube.faces.begin(), cube.faces.end(), face_has_repeated_vertices),
        "rebuilt convex hull faces should not repeat vertex ids");
    require_test(every_edge_is_closed(cube), "rebuilt convex hull should remain closed after center merge");
    require_test(every_face_compiles_to_triangles(cube), "rebuilt convex hull should keep every face renderable");

    Face& face = cube.faces.front();
    const ElementId face_id = face.id;
    const quader::QVec3 center = document_centroid_for_test(cube);
    std::ranges::reverse(face.vertices);
    const quader::QVec3 inverted_normal = face_normal(cube, face);
    require_test(dot(inverted_normal, face_centroid_for_test(cube, face) - center) < 0.0F,
        "normal recalculation test should start with an inward face");

    Selection face_selection;
    face_selection.mode = SelectionMode::face;
    face_selection.faces = { face_id };
    const OperationResult recalc_result = recalculate_selected_face_normals(cube, face_selection, true);
    require_test(recalc_result.changed, "normal recalculation should flip an inward selected hull face outward");
    const Face* recalculated_face = find_face(cube, face_id);
    require_test(recalculated_face != nullptr, "normal recalculation should preserve the selected face id");
    require_test(dot(face_normal(cube, *recalculated_face), face_centroid_for_test(cube, *recalculated_face) - center) > 0.0F,
        "normal recalculation should orient the selected hull face away from the document center");
}

void characterizes_knife_rejects_segments_crossing_existing_cuts()
{
    Document quad = make_single_quad_document();
    const Face source_face = quad.faces.front();
    const KnifePointTarget bottom_cut {
        KnifePointTargetKind::existing_edge,
        invalid_element_id,
        make_edge(source_face.vertices[0], source_face.vertices[1]),
        0.25F,
        source_face.id,
        {},
    };
    const KnifePointTarget top_cut {
        KnifePointTargetKind::existing_edge,
        invalid_element_id,
        make_edge(source_face.vertices[2], source_face.vertices[3]),
        0.75F,
        source_face.id,
        {},
    };
    Selection selection;
    const OperationResult first_cut = knife_segment(quad, selection, bottom_cut, top_cut);
    require_test(first_cut.changed, "knife should split the quad with an initial boundary-to-boundary segment");
    require_test(quad.faces.size() == 2U && selection.vertices.size() == 2U,
        "initial knife segment should create two split faces and select its endpoints");
    require_test(document_has_edge(quad, make_edge(selection.vertices[0], selection.vertices[1])),
        "initial knife segment should commit the inserted cut edge");

    const std::size_t vertex_count_before_crossing_cut = quad.vertices.size();
    const std::size_t face_count_before_crossing_cut = quad.faces.size();
    const KnifePointTarget left_crossing_cut {
        KnifePointTargetKind::existing_edge,
        invalid_element_id,
        make_edge(source_face.vertices[3], source_face.vertices[0]),
        0.5F,
        source_face.id,
        {},
    };
    const KnifePointTarget right_crossing_cut {
        KnifePointTargetKind::existing_edge,
        invalid_element_id,
        make_edge(source_face.vertices[1], source_face.vertices[2]),
        0.5F,
        source_face.id,
        {},
    };
    const KnifeSegmentPreview crossing_preview = build_knife_segment_preview(quad, left_crossing_cut, right_crossing_cut);
    require_test(!crossing_preview.valid, "knife preview should reject a segment that would cross an existing inserted cut edge");

    const OperationResult crossing_cut = knife_segment(quad, selection, left_crossing_cut, right_crossing_cut);
    require_test(!crossing_cut.changed, "knife should reject a segment whose endpoints no longer resolve to one unsplit face");
    require_test(quad.vertices.size() == vertex_count_before_crossing_cut && quad.faces.size() == face_count_before_crossing_cut,
        "rejected crossing knife segment should not mutate the split document");
}

void characterizes_adjacent_edge_bevel_miter_geometry()
{
    Document cube = make_cube({});
    const Face source_face = cube.faces.front();
    const std::size_t vertex_count_before = cube.vertices.size();
    const std::size_t face_count_before = cube.faces.size();
    Selection selection;
    selection.mode = SelectionMode::edge;
    selection.edges = {
        make_edge(source_face.vertices[0], source_face.vertices[1]),
        make_edge(source_face.vertices[1], source_face.vertices[2]),
    };
    selection.has_active = true;
    selection.active_kind = ElementKind::edge;
    selection.active_edge = selection.edges.back();

    EdgeBevelSettings settings;
    settings.width = 0.25F;
    settings.segments = 1;
    settings.profile = 0.5F;
    const OperationResult bevel_result = bevel_selected_edges(cube, selection, settings);

    require_test(bevel_result.changed, "adjacent edge bevel should create offset geometry");
    require_test(cube.vertices.size() > vertex_count_before, "adjacent edge bevel should add offset vertices");
    require_test(cube.faces.size() == face_count_before + 2U, "adjacent edge bevel should add strip faces while terminal profiles are absorbed by source faces");
    require_test(selection.mode == SelectionMode::face && selection.faces.size() == 2U,
        "adjacent edge bevel should select strip faces without adding a generated two-edge corner patch");
    require_test(std::none_of(cube.faces.begin(), cube.faces.end(), face_has_repeated_vertices),
        "adjacent edge bevel should not leave repeated face vertices at the miter");
    require_test(every_edge_is_closed(cube), "adjacent edge bevel should keep the cube hull closed");
    require_test(every_face_compiles_to_triangles(cube), "adjacent edge bevel should keep every generated face renderable");
}

} // namespace

TEST_CASE("qdr_poly_geometry_characterization_tests", "[poly-geometry-characterization]")
{
    characterizes_face_pick_on_quad();
    characterizes_face_pick_with_near_parallel_bounds_axis();
    characterizes_edge_pick_radius_and_tie_break();
    characterizes_component_occlusion_by_front_face();
    characterizes_concave_ngon_renderability();
    characterizes_edge_factor_from_ray();
    characterizes_convex_hull_rebuild_and_normal_recalculation();
    characterizes_knife_rejects_segments_crossing_existing_cuts();
    characterizes_adjacent_edge_bevel_miter_geometry();
}

TEST_CASE("poly overlay emits selected face primitives for a quad", "[poly-geometry-characterization][overlay]")
{
    const Document document = make_single_quad_document();
    Selection selection;
    selection.mode = SelectionMode::face;
    selection.faces = { document.faces.front().id };

    const OverlayData overlay = build_overlay(document, selection);

    REQUIRE(overlay.triangles.size() == 2U);
    CHECK(overlay.lines.size() >= 8U);
    CHECK(overlay.points.empty());
}
