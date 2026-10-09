////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_validation_test_helpers.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace {

using quader_poly::Document;
using quader_poly::ElementId;
using quader_poly::Face;
using quader_poly::PlaneCutMode;
using quader_poly::PlaneCutRequest;
using quader_poly::PlaneCutResult;
using quader_poly::OperationResult;
using quader::QVec3;

PlaneCutRequest center_x_cut(PlaneCutMode mode)
{
	return {
		{ 0.0F, -2.0F, -2.0F },
		{ 0.0F, 2.0F, -2.0F },
		{ 0.0F, -2.0F, 2.0F },
		mode,
	};
}

PlaneCutRequest tilted_cut(PlaneCutMode mode)
{
	return {
		{ -0.75F, -1.0F, -0.75F },
		{ 0.75F, 1.0F, -0.25F },
		{ -0.25F, -1.0F, 0.75F },
		mode,
	};
}

PlaneCutRequest subdivided_box_surface_cut(PlaneCutMode mode)
{
	return {
		{ -0.35F, -3.0F, -2.0F },
		{ 0.65F, 3.0F, -2.0F },
		{ -0.35F, -3.0F, 2.0F },
		mode,
	};
}

PlaneCutRequest corner_on_vertex_cut(PlaneCutMode mode)
{
	return {
		{ 1.0F, 1.0F, -1.0F },
		{ 1.0F, -1.0F, 1.0F },
		{ -1.0F, 1.0F, 1.0F },
		mode,
	};
}

PlaneCutRequest diagonal_on_existing_edges_cut(PlaneCutMode mode)
{
	return {
		{ 0.0F, 0.0F, -2.0F },
		{ 0.0F, 0.0F, 2.0F },
		{ -1.0F, 1.0F, 0.0F },
		mode,
	};
}

std::map<std::pair<ElementId, ElementId>, int> edge_counts(const Document& document)
{
	std::map<std::pair<ElementId, ElementId>, int> counts;
	for (const Face& face : document.faces) {
		for (std::size_t index = 0; index < face.vertices.size(); ++index) {
			quader_poly::Edge edge = quader_poly::make_edge(face.vertices[index], face.vertices[(index + 1U) % face.vertices.size()]);
			++counts[{ edge.a, edge.b }];
		}
	}
	return counts;
}

std::set<std::pair<ElementId, ElementId>> edge_set(const Document& document)
{
	std::set<std::pair<ElementId, ElementId>> edges;
	for (const quader_poly::Edge edge : quader_poly::document_edges(document)) {
		edges.emplace(edge.a, edge.b);
	}
	return edges;
}

bool point_lies_on_request_plane(const PlaneCutRequest& request, QVec3 point)
{
	const QVec3 first_delta = request.second_point - request.first_point;
	const QVec3 second_delta = request.third_point - request.first_point;
	const QVec3 normal = cross(first_delta, second_delta);
	const float normal_length = std::sqrt(length_squared(normal));
	if (normal_length <= 0.0001F) {
		return false;
	}
	return std::abs(dot(point - request.first_point, normal)) <=
	       0.0002F * normal_length;
}

bool point_lies_on_segment(QVec3 point, QVec3 first, QVec3 second)
{
	const QVec3 delta = second - first;
	const float length_squared_delta = length_squared(delta);
	if (length_squared_delta <= 0.00000001F) {
		return qdr_test::vec_nearly_equal(point, first, 0.0002F);
	}
	const float factor = dot(point - first, delta) / length_squared_delta;
	if (factor < -0.0002F || factor > 1.0002F) {
		return false;
	}
	const QVec3 closest = first + (delta * std::clamp(factor, 0.0F, 1.0F));
	return length_squared(point - closest) <= 0.0002F * 0.0002F;
}

bool edge_lies_on_source_edge(const Document& source, quader_poly::Edge edge,
                              const Document& candidate)
{
	const quader_poly::Vertex* first = quader_poly::find_vertex(candidate, edge.a);
	const quader_poly::Vertex* second = quader_poly::find_vertex(candidate, edge.b);
	if (first == nullptr || second == nullptr) {
		return false;
	}
	for (const quader_poly::Edge source_edge : quader_poly::document_edges(source)) {
		const quader_poly::Vertex* source_first =
			quader_poly::find_vertex(source, source_edge.a);
		const quader_poly::Vertex* source_second =
			quader_poly::find_vertex(source, source_edge.b);
		if (source_first == nullptr || source_second == nullptr) {
			continue;
		}
		if (point_lies_on_segment(
			    first->position, source_first->position, source_second->position) &&
		    point_lies_on_segment(
			    second->position, source_first->position, source_second->position)) {
			return true;
		}
	}
	return false;
}

bool edge_lies_on_specific_source_edge(
	const Document& source,
	quader_poly::Edge source_edge,
	const Document& candidate,
	quader_poly::Edge candidate_edge)
{
	const quader_poly::Vertex* source_first =
		quader_poly::find_vertex(source, source_edge.a);
	const quader_poly::Vertex* source_second =
		quader_poly::find_vertex(source, source_edge.b);
	const quader_poly::Vertex* candidate_first =
		quader_poly::find_vertex(candidate, candidate_edge.a);
	const quader_poly::Vertex* candidate_second =
		quader_poly::find_vertex(candidate, candidate_edge.b);
	return source_first != nullptr && source_second != nullptr &&
		candidate_first != nullptr && candidate_second != nullptr &&
		point_lies_on_segment(candidate_first->position,
		                      source_first->position,
		                      source_second->position) &&
		point_lies_on_segment(candidate_second->position,
		                      source_first->position,
		                      source_second->position);
}

bool edge_lies_on_plane(const Document& document, quader_poly::Edge edge,
                        const PlaneCutRequest& request)
{
	const quader_poly::Vertex* first = quader_poly::find_vertex(document, edge.a);
	const quader_poly::Vertex* second = quader_poly::find_vertex(document, edge.b);
	return first != nullptr && second != nullptr &&
	       point_lies_on_request_plane(request, first->position) &&
	       point_lies_on_request_plane(request, second->position);
}

void check_closed_manifold(const Document& document)
{
	EXPECT_FALSE(document.faces.empty());
	for (const auto& [edge, count] : edge_counts(document)) {
		(void)edge;
		EXPECT_TRUE(count == 2);
	}
	EXPECT_FALSE(quader_poly::compile_document(document).indices.empty());
}

const Face* selected_cap_face(const Document& document, const quader_poly::Selection& selection)
{
	EXPECT_TRUE(selection.faces.size() == 1U);
	const Face* face = quader_poly::find_face(document, selection.faces.front());
	EXPECT_TRUE(face != nullptr);
	return face;
}

int cap_face_count(const quader_poly::Selection& selection)
{
	return static_cast<int>(selection.faces.size());
}

void set_all_material_slots(Document& document, std::uint32_t slot)
{
	for (Face& face : document.faces) {
		face.material_slot = slot;
	}
}

Document two_cube_document()
{
	Document first = quader_poly::make_cube({});
	Document second = quader_poly::make_cube({});
	for (quader_poly::Vertex& vertex : second.vertices) {
		vertex.id += 1000;
		vertex.position.y += 4.0F;
	}
	for (Face& face : second.faces) {
		face.id += 1000;
		for (ElementId& vertex_id : face.vertices) {
			vertex_id += 1000;
		}
	}
	first.vertices.insert(first.vertices.end(), second.vertices.begin(), second.vertices.end());
	first.faces.insert(first.faces.end(), second.faces.begin(), second.faces.end());
	first.next_vertex_id = 2000;
	first.next_face_id = 2000;
	return first;
}

std::vector<ElementId> center_x_crossing_faces(const Document& document)
{
    std::vector<ElementId> faces;
    for (const Face& face : document.faces) {
        bool front = false;
        bool back = false;
        for (const ElementId vertex_id : face.vertices) {
            const auto* vertex = quader_poly::find_vertex(document, vertex_id);
            front = front || vertex->position.x > 0.0F;
            back = back || vertex->position.x < 0.0F;
        }
        if (front && back) {
            faces.push_back(face.id);
        }
    }
    return faces;
}

std::vector<quader_poly::Edge> center_x_crossing_edges(const Document& document)
{
    std::vector<quader_poly::Edge> edges;
    for (const auto edge : quader_poly::document_edges(document)) {
        if (quader_poly::find_vertex(document, edge.a)->position.x *
                quader_poly::find_vertex(document, edge.b)->position.x < 0.0F) {
            edges.push_back(edge);
        }
    }
    return edges;
}

void author_distinct_face_charts(Document& document)
{
    for (Face& face : document.faces) {
        face.material_slot = face.id;
        face.normal_shading = quader_poly::SurfaceShading::Smooth;
        face.texture_transform.rotation_degrees = 17.0F;
        face.uvs.clear();
        for (const ElementId vertex_id : face.vertices) {
            const auto position = quader_poly::find_vertex(document, vertex_id)->position;
            face.uvs.push_back({position.x + static_cast<float>(face.id) * 10.0F,
                               position.y + position.z * 2.0F});
        }
    }
}

void expect_preserved_face_chart(const Document& document, const Face& face,
                                 const Face& source)
{
    EXPECT_EQ(face.material_slot, source.material_slot);
    EXPECT_EQ(face.normal_shading, source.normal_shading);
    EXPECT_FLOAT_EQ(face.texture_transform.rotation_degrees,
                    source.texture_transform.rotation_degrees);
    ASSERT_EQ(face.uvs.size(), face.vertices.size());
    for (std::size_t index = 0; index < face.vertices.size(); ++index) {
        const auto position = quader_poly::find_vertex(document, face.vertices[index])->position;
        EXPECT_NEAR(face.uvs[index].x,
                    position.x + static_cast<float>(source.id) * 10.0F, 0.0001F);
        EXPECT_NEAR(face.uvs[index].y, position.y + position.z * 2.0F, 0.0001F);
    }
}

} // namespace

TEST(QdrDocumentOperationPlaneCutTests,
     SelectedFaceCutPartitionsOnlyItsFaceAndExpandsSharedBoundaries) {
    Document document = quader_poly::make_cube({});
    author_distinct_face_charts(document);
    const Document source = document;
    const ElementId selected_face = center_x_crossing_faces(document).front();
    const Face source_face = *quader_poly::find_face(source, selected_face);
    quader_poly::Selection selection;
    selection.mode = quader_poly::SelectionMode::Face;
    selection.faces = {selected_face};
    selection.has_active = true;
    selection.active_kind = quader_poly::ElementKind::Face;
    selection.active_face = selected_face;

    const auto result = quader_poly::plane_cut_selected_faces(
        document, selection, center_x_cut(PlaneCutMode::KeepBoth));

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(result.changed);
    qdr_test::expect_valid_polygon_document_after_success(document, result);
    EXPECT_EQ(document.faces.size(), source.faces.size() + 1U);
    EXPECT_EQ(document.vertices.size(), source.vertices.size() + 2U);
    EXPECT_EQ(selection.mode, quader_poly::SelectionMode::Face);
    EXPECT_EQ(selection.faces.size(), 2U);
    EXPECT_EQ(selection.active_face, selected_face);
    EXPECT_EQ(result.created.faces.size(), 1U);
    check_closed_manifold(document);
    for (const Face& face : document.faces) {
        const Face* original = quader_poly::find_face(source, face.id);
        expect_preserved_face_chart(document, face, original ? *original : source_face);
        if (original != nullptr && original->id != selected_face) {
            EXPECT_GE(face.vertices.size(), original->vertices.size());
            for (const ElementId vertex_id : original->vertices) {
                EXPECT_NE(std::ranges::find(face.vertices, vertex_id), face.vertices.end());
            }
        }
    }
}

TEST(QdrDocumentOperationPlaneCutTests,
     AdjacentSelectedFacesShareOneIntersectionVertex) {
    Document document = quader_poly::make_cube({});
    const auto selected_edge = center_x_crossing_edges(document).front();
    quader_poly::Selection selection;
    selection.mode = quader_poly::SelectionMode::Face;
    for (const Face& face : document.faces) {
        const bool first = std::ranges::find(face.vertices, selected_edge.a) != face.vertices.end();
        const bool second = std::ranges::find(face.vertices, selected_edge.b) != face.vertices.end();
        if (first && second) {
            selection.faces.push_back(face.id);
        }
    }
    ASSERT_EQ(selection.faces.size(), 2U);
    const auto result = quader_poly::plane_cut_selected_faces(
        document, selection, center_x_cut(PlaneCutMode::KeepBoth));
    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(result.changed);
    qdr_test::expect_valid_polygon_document_after_success(document, result);
    EXPECT_EQ(document.faces.size(), 8U);
    EXPECT_EQ(document.vertices.size(), 11U);
    EXPECT_EQ(result.created.vertices.size(), 3U);
    check_closed_manifold(document);
}

TEST(QdrDocumentOperationPlaneCutTests,
     SelectedFaceDiscardKeepsOriginalIdentityAndUnselectedGeometry) {
    for (const auto mode : {PlaneCutMode::KeepFront, PlaneCutMode::KeepBack}) {
        Document document = quader_poly::make_cube({});
        author_distinct_face_charts(document);
        const Document source = document;
        const auto selected_face = center_x_crossing_faces(document).front();
        quader_poly::Selection selection;
        selection.mode = quader_poly::SelectionMode::Face;
        selection.faces = {selected_face};
        const auto result = quader_poly::plane_cut_selected_faces(
            document, selection, center_x_cut(mode));
        ASSERT_TRUE(result.success) << result.message;
        ASSERT_TRUE(result.changed);
        qdr_test::expect_valid_polygon_document_after_success(document, result);
        EXPECT_EQ(document.faces.size(), source.faces.size());
        EXPECT_TRUE(result.created.faces.empty());
        EXPECT_EQ(selection.faces, std::vector<ElementId>{selected_face});
        const Face* retained = quader_poly::find_face(document, selected_face);
        ASSERT_NE(retained, nullptr);
        for (const ElementId vertex_id : retained->vertices) {
            const float x = quader_poly::find_vertex(document, vertex_id)->position.x;
            EXPECT_GE(mode == PlaneCutMode::KeepFront ? x : -x, -0.0001F);
        }
        for (const Face& face : source.faces) {
            ASSERT_NE(quader_poly::find_face(document, face.id), nullptr);
            if (face.id != selected_face) {
                const auto& retained_loop = quader_poly::find_face(document, face.id)->vertices;
                for (const auto vertex_id : face.vertices) {
                    EXPECT_NE(std::ranges::find(retained_loop, vertex_id), retained_loop.end());
                }
            }
        }
    }
}

TEST(QdrDocumentOperationPlaneCutTests,
     SelectedEdgeCutInsertsOnceAcrossIncidentLoopsWithoutPartitioningFaces) {
    Document document = quader_poly::make_cube({});
    author_distinct_face_charts(document);
    const auto crossing = center_x_crossing_edges(document);
    const auto selected_edge = crossing.front();
    document.hard_normal_edges = {selected_edge};
    document.soft_normal_edges = {crossing.back()};
    const Document source = document;
    quader_poly::Selection selection;
    selection.mode = quader_poly::SelectionMode::Edge;
    selection.edges = {selected_edge, {selected_edge.b, selected_edge.a}};
    selection.has_active = true;
    selection.active_kind = quader_poly::ElementKind::Edge;
    selection.active_edge = selected_edge;
    const auto result = quader_poly::plane_cut_selected_edges(
        document, selection, center_x_cut(PlaneCutMode::KeepBack));
    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(result.changed);
    qdr_test::expect_valid_polygon_document_after_success(document, result);
    EXPECT_EQ(document.faces.size(), source.faces.size());
    EXPECT_EQ(document.vertices.size(), source.vertices.size() + 1U);
    EXPECT_EQ(result.created.vertices.size(), 1U);
    EXPECT_TRUE(result.created.faces.empty());
    EXPECT_EQ(selection.mode, quader_poly::SelectionMode::Edge);
    EXPECT_EQ(selection.edges.size(), 2U);
    EXPECT_EQ(document.hard_normal_edges.size(), 2U);
    EXPECT_EQ(document.soft_normal_edges, source.soft_normal_edges);
    EXPECT_EQ(result.affected.faces.size(), 2U);
    check_closed_manifold(document);
    const auto edges = quader_poly::document_edges(document);
    for (const auto edge : crossing) {
        EXPECT_EQ(std::ranges::find(edges, edge) != edges.end(), edge != selected_edge);
    }
    for (const Face& face : document.faces) {
        const Face* original = quader_poly::find_face(source, face.id);
        ASSERT_NE(original, nullptr);
        expect_preserved_face_chart(document, face, *original);
        const bool incident = std::ranges::find(original->vertices, selected_edge.a) != original->vertices.end() &&
                              std::ranges::find(original->vertices, selected_edge.b) != original->vertices.end();
        EXPECT_EQ(face.vertices.size(), original->vertices.size() + (incident ? 1U : 0U));
    }
}

TEST(QdrDocumentOperationPlaneCutTests,
     ScopedCutsPreserveNoOpsAndRejectStaleSelectionsAtomically) {
    const auto source = quader_poly::make_cube({});
    for (const bool faces : {false, true}) {
        Document document = source;
        quader_poly::Selection selection;
        selection.mode = faces ? quader_poly::SelectionMode::Face : quader_poly::SelectionMode::Edge;
        selection.faces = {center_x_crossing_faces(source).front()};
        selection.edges = {center_x_crossing_edges(source).front()};
        const auto operate = [&](const PlaneCutRequest& request) {
            return faces ? quader_poly::plane_cut_selected_faces(document, selection, request)
                         : quader_poly::plane_cut_selected_edges(document, selection, request);
        };
        auto missed = center_x_cut(PlaneCutMode::KeepBoth);
        missed.first_point.x = missed.second_point.x = missed.third_point.x = 4.0F;
        const auto no_op = operate(missed);
        EXPECT_TRUE(no_op.success);
        EXPECT_FALSE(no_op.changed);
        EXPECT_EQ(document.next_vertex_id, source.next_vertex_id);
        EXPECT_EQ(document.next_face_id, source.next_face_id);
        EXPECT_EQ(edge_set(document), edge_set(source));

        auto invalid = center_x_cut(PlaneCutMode::KeepBoth);
        invalid.first_point.x = std::numeric_limits<float>::quiet_NaN();
        EXPECT_FALSE(operate(invalid).success);
        selection.faces.push_back(99999U);
        selection.edges.push_back({99998U, 99999U});
        EXPECT_FALSE(operate(center_x_cut(PlaneCutMode::KeepBoth)).success);
        EXPECT_EQ(document.next_vertex_id, source.next_vertex_id);
        EXPECT_EQ(document.next_face_id, source.next_face_id);
        EXPECT_EQ(edge_set(document), edge_set(source));
    }
}

TEST(QdrDocumentOperationPlaneCutTests,
     ScopedCutsIgnoreEndpointAndCoplanarContactsWithoutConsumingIds) {
    const Document source = quader_poly::make_cube({});
    for (const bool faces : {false, true}) {
        Document document = source;
        quader_poly::Selection selection;
        selection.mode = faces ? quader_poly::SelectionMode::Face : quader_poly::SelectionMode::Edge;
        for (const Face& face : source.faces) {
            selection.faces.push_back(face.id);
        }
        selection.edges = quader_poly::document_edges(source);
        auto request = center_x_cut(PlaneCutMode::KeepBoth);
        request.first_point.x = request.second_point.x = request.third_point.x = 1.0F;
        const auto result = faces
                                ? quader_poly::plane_cut_selected_faces(document, selection, request)
                                : quader_poly::plane_cut_selected_edges(document, selection, request);
        EXPECT_TRUE(result.success) << result.message;
        EXPECT_FALSE(result.changed);
        EXPECT_EQ(document.next_face_id, source.next_face_id);
        EXPECT_EQ(document.next_vertex_id, source.next_vertex_id);
        EXPECT_EQ(edge_set(document), edge_set(source));
    }
}

TEST(QdrDocumentOperationPlaneCutTests,
     SelectedFaceCutRejectsMultipleIntersectionIntervalsAtomically) {
    Document document;
    const std::vector<QVec3> positions{
        {-3.0F, -3.0F, 0.0F}, {3.0F, -3.0F, 0.0F},
        {3.0F, 3.0F, 0.0F}, {1.0F, 3.0F, 0.0F},
        {1.0F, -1.0F, 0.0F}, {-1.0F, -1.0F, 0.0F},
        {-1.0F, 3.0F, 0.0F}, {-3.0F, 3.0F, 0.0F}};
    Face face;
    face.id = document.next_face_id++;
    for (const auto position : positions) {
        const auto id = document.next_vertex_id++;
        document.vertices.push_back({id, position});
        face.vertices.push_back(id);
    }
    document.faces.push_back(face);
    const auto source = document;
    quader_poly::Selection selection;
    selection.mode = quader_poly::SelectionMode::Face;
    selection.faces = {face.id};
    const auto result = quader_poly::plane_cut_selected_faces(
        document, selection,
        {{-4.0F, 0.0F, 0.0F}, {4.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}});
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.changed);
    EXPECT_EQ(document.next_vertex_id, source.next_vertex_id);
    EXPECT_EQ(document.next_face_id, source.next_face_id);
    EXPECT_EQ(document.faces.front().vertices, source.faces.front().vertices);
    EXPECT_EQ(document.vertices.size(), source.vertices.size());
    EXPECT_EQ(selection.faces, std::vector<ElementId>{face.id});
}

TEST(QdrDocumentOperationPlaneCutTests,
     SelectedFaceDiscardPreservesUnrelatedLooseVertices) {
    Document document = quader_poly::make_cube({});
    const ElementId loose_vertex = document.next_vertex_id++;
    document.vertices.push_back({loose_vertex, {12.0F, 13.0F, 14.0F}});
    quader_poly::Selection selection;
    selection.mode = quader_poly::SelectionMode::Face;
    for (const Face& face : document.faces) {
        selection.faces.push_back(face.id);
    }
    auto request = center_x_cut(PlaneCutMode::KeepFront);
    request.first_point.x = request.second_point.x = request.third_point.x = 4.0F;
    const auto result = quader_poly::plane_cut_selected_faces(document, selection, request);
    ASSERT_TRUE(result.success) << result.message;
    EXPECT_TRUE(result.changed);
    EXPECT_TRUE(document.faces.empty());
    ASSERT_EQ(document.vertices.size(), 1U);
    EXPECT_EQ(document.vertices.front().id, loose_vertex);
    EXPECT_TRUE(selection.faces.empty());
}

TEST(QdrDocumentOperationPlaneCutTests, PlaneCutSplitsACubeAndKeepsBothClosedHalves) {
	Document document = quader_poly::make_cube({});
	set_all_material_slots(document, 7);
	const quader_poly::FaceTextureTransform texture{{-2.0F, 0.5F}, {0.25F, 0.75F}, -20.0F};
	for (Face& face : document.faces) {
		face.texture_transform = texture;
	}

        const PlaneCutResult result = quader_poly::plane_cut(
            document, center_x_cut(PlaneCutMode::KeepBoth));
        EXPECT_TRUE(result.changed);
	qdr_test::expect_valid_polygon_document_after_success(result);
	EXPECT_TRUE(result.split_count == 1);
	EXPECT_TRUE(result.removed_count == 0);
	EXPECT_TRUE(cap_face_count(result.front_selection) == 1);
	EXPECT_TRUE(cap_face_count(result.back_selection) == 1);
	check_closed_manifold(result.front_document);
	check_closed_manifold(result.back_document);
	for (const Document* half : {&result.front_document, &result.back_document}) {
		for (const Face& face : half->faces) {
			EXPECT_EQ(face.texture_transform, texture);
		}
	}

	const ElementId front_cap_id = result.front_selection.faces.front();
	const Face* front_cap = quader_poly::find_face(result.front_document, front_cap_id);
	EXPECT_TRUE(front_cap != nullptr);
	EXPECT_TRUE(front_cap->material_slot == 7);
	EXPECT_TRUE(dot(face_normal(result.front_document, *front_cap), quader::QVec3 { -1.0F, 0.0F, 0.0F }) > 0.9F);

	const Face* back_cap = selected_cap_face(result.back_document, result.back_selection);
	EXPECT_TRUE(dot(face_normal(result.back_document, *back_cap), quader::QVec3 { 1.0F, 0.0F, 0.0F }) > 0.9F);
}

TEST(QdrDocumentOperationPlaneCutTests,
	 PlaneCutAndSurfaceCutPreserveHardAndSoftClippedEdgeDescendants) {
	Document source = quader_poly::make_cube({});
	std::vector<quader_poly::Edge> crossing_edges;
	for (const quader_poly::Edge edge : quader_poly::document_edges(source)) {
		const quader_poly::Vertex* first =
			quader_poly::find_vertex(source, edge.a);
		const quader_poly::Vertex* second =
			quader_poly::find_vertex(source, edge.b);
		if (first != nullptr && second != nullptr &&
		    first->position.x * second->position.x < 0.0F) {
			crossing_edges.push_back(edge);
		}
	}
	ASSERT_GE(crossing_edges.size(), 3U);
	const quader_poly::Edge hard_source = crossing_edges[0];
	const quader_poly::Edge soft_source = crossing_edges[1];
	source.hard_normal_edges = {hard_source};
	source.soft_normal_edges = {soft_source};

	const PlaneCutRequest request = center_x_cut(PlaneCutMode::KeepBoth);
	const PlaneCutResult solid = quader_poly::plane_cut(source, request);
	ASSERT_TRUE(solid.changed) << solid.message;
	for (const Document* half : {&solid.front_document,
	                             &solid.back_document}) {
		ASSERT_EQ(half->hard_normal_edges.size(), 1U);
		EXPECT_TRUE(edge_lies_on_specific_source_edge(
			source, hard_source, *half, half->hard_normal_edges.front()));
		ASSERT_EQ(half->soft_normal_edges.size(), 1U);
		EXPECT_TRUE(edge_lies_on_specific_source_edge(
			source, soft_source, *half, half->soft_normal_edges.front()));
		for (const quader_poly::Edge edge : quader_poly::document_edges(*half)) {
			if (!edge_lies_on_plane(*half, edge, request)) {
				continue;
			}
			EXPECT_FALSE(qdr_test::contains_edge(
				half->hard_normal_edges, edge));
			EXPECT_FALSE(qdr_test::contains_edge(
				half->soft_normal_edges, edge));
		}
	}

	Document surface = source;
	quader_poly::Selection selection;
	const OperationResult surface_result = quader_poly::plane_surface_cut(
		surface, selection, request);
	ASSERT_TRUE(surface_result.changed) << surface_result.message;
	ASSERT_EQ(surface.hard_normal_edges.size(), 2U);
	for (const quader_poly::Edge edge : surface.hard_normal_edges) {
		EXPECT_TRUE(edge_lies_on_specific_source_edge(
			source, hard_source, surface, edge));
	}
	ASSERT_EQ(surface.soft_normal_edges.size(), 2U);
	for (const quader_poly::Edge edge : surface.soft_normal_edges) {
		EXPECT_TRUE(edge_lies_on_specific_source_edge(
			source, soft_source, surface, edge));
	}
}

TEST(QdrDocumentOperationPlaneCutTests,
     PlaneSurfaceCutBisectsCubeWithoutRemovingOrSplittingParts) {
  Document document = quader_poly::make_cube({});
  quader_poly::Selection selection;
  const std::size_t vertex_count_before = document.vertices.size();
  const std::size_t face_count_before = document.faces.size();

  const OperationResult result = quader_poly::plane_surface_cut(
      document, selection, center_x_cut(PlaneCutMode::KeepBoth));

  EXPECT_TRUE(result.changed) << result.message;
  EXPECT_GT(document.vertices.size(), vertex_count_before);
  EXPECT_GT(document.faces.size(), face_count_before);
  EXPECT_EQ(selection.mode, quader_poly::SelectionMode::Vertex);
  EXPECT_FALSE(selection.vertices.empty());
  check_closed_manifold(document);
}

TEST(QdrDocumentOperationPlaneCutTests,
     PlaneSurfaceCutSubdividedBoxDoesNotCreateOffPlaneConnectorEdges) {
  quader_poly::CubePrimitiveSpec spec;
  spec.width = 4.0F;
  spec.height = 6.0F;
  spec.depth = 2.0F;
  spec.width_segments = 4;
  spec.height_segments = 6;
  spec.depth_segments = 2;
  const Document source = quader_poly::make_cube(spec);
  Document document = source;
  quader_poly::Selection selection;
  const std::set<std::pair<ElementId, ElementId>> source_edges =
      edge_set(source);
  const PlaneCutRequest request =
      subdivided_box_surface_cut(PlaneCutMode::KeepBoth);

  const OperationResult result =
      quader_poly::plane_surface_cut(document, selection, request);

  EXPECT_TRUE(result.changed) << result.message;
  qdr_test::expect_valid_polygon_document_after_success(document, result);
  EXPECT_EQ(selection.mode, quader_poly::SelectionMode::Vertex);
  EXPECT_FALSE(selection.vertices.empty());
  check_closed_manifold(document);
  for (const quader_poly::Edge edge : quader_poly::document_edges(document)) {
    if (source_edges.contains({edge.a, edge.b})) {
      continue;
    }
    EXPECT_TRUE(edge_lies_on_plane(document, edge, request) ||
                edge_lies_on_source_edge(source, edge, document))
        << "new edge " << edge.a << "-" << edge.b
        << " is neither on the cut plane nor on a split source edge";
  }
}

TEST(QdrDocumentOperationPlaneCutTests,
     PlaneCutCanLeaveSeparatedPartsOpenWhenCapsAreDisabled) {
  const Document document = quader_poly::make_cube({});
  PlaneCutRequest request = center_x_cut(PlaneCutMode::KeepBoth);
  request.cap = false;

  const PlaneCutResult result = quader_poly::plane_cut(document, request);

  ASSERT_TRUE(result.changed) << result.message;
  EXPECT_TRUE(result.front_selection.faces.empty());
  EXPECT_TRUE(result.back_selection.faces.empty());
  EXPECT_GT(qdr_test::open_edge_count(result.front_document), 0U);
  EXPECT_GT(qdr_test::open_edge_count(result.back_document), 0U);
  EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(result.front_document));
  EXPECT_TRUE(qdr_test::every_face_compiles_to_triangles(result.back_document));
}

TEST(QdrDocumentOperationPlaneCutTests, PlaneCutSupportsDestructiveFrontAndBackModes) {
	Document document = quader_poly::make_cube({});

	// PlaneCutOperation is still a translation-unit-local concrete type with no public factory,
	// so this pins the stable single-document replacement data consumed by its apply() path.
        const PlaneCutResult front = quader_poly::plane_cut(
            document, center_x_cut(PlaneCutMode::KeepFront));
        EXPECT_TRUE(front.changed);
	qdr_test::expect_valid_polygon_document_after_success(front, "successful front plane cut");
	EXPECT_TRUE(front.removed_count == 1);
        EXPECT_TRUE(front.front_selection.mode ==
                    quader_poly::SelectionMode::Face);
        check_closed_manifold(front.front_document);
	check_closed_manifold(front.back_document);
	const Face* front_cap = selected_cap_face(front.front_document, front.front_selection);
	EXPECT_TRUE(dot(face_normal(front.front_document, *front_cap), quader::QVec3 { -1.0F, 0.0F, 0.0F }) > 0.9F);

        const PlaneCutResult back = quader_poly::plane_cut(
            document, center_x_cut(PlaneCutMode::KeepBack));
        EXPECT_TRUE(back.changed);
	qdr_test::expect_valid_polygon_document_after_success(back, "successful back plane cut");
	EXPECT_TRUE(back.removed_count == 1);
        EXPECT_TRUE(back.back_selection.mode ==
                    quader_poly::SelectionMode::Face);
        check_closed_manifold(back.front_document);
	check_closed_manifold(back.back_document);
	const Face* back_cap = selected_cap_face(back.back_document, back.back_selection);
	EXPECT_TRUE(dot(face_normal(back.back_document, *back_cap), quader::QVec3 { 1.0F, 0.0F, 0.0F }) > 0.9F);
}

TEST(QdrDocumentOperationPlaneCutTests, PlaneCutCreatesValidCapsForTiltedPlanes) {
	Document document = quader_poly::make_cube({});

        const PlaneCutResult result = quader_poly::plane_cut(
            document, tilted_cut(PlaneCutMode::KeepBoth));
        EXPECT_TRUE(result.changed);
	qdr_test::expect_valid_polygon_document_after_success(result, "successful tilted plane cut");
	EXPECT_TRUE(cap_face_count(result.front_selection) == 1);
	EXPECT_TRUE(cap_face_count(result.back_selection) == 1);
	check_closed_manifold(result.front_document);
	check_closed_manifold(result.back_document);
}

TEST(QdrDocumentOperationPlaneCutTests, PlaneCutSupportsCutsThroughExistingVertices) {
	Document document = quader_poly::make_cube({});

        const PlaneCutResult result = quader_poly::plane_cut(
            document, corner_on_vertex_cut(PlaneCutMode::KeepBoth));
        EXPECT_TRUE(result.changed);
	qdr_test::expect_valid_polygon_document_after_success(result, "successful vertex plane cut");
	EXPECT_TRUE(cap_face_count(result.front_selection) == 1);
	EXPECT_TRUE(cap_face_count(result.back_selection) == 1);
	check_closed_manifold(result.front_document);
	check_closed_manifold(result.back_document);

	const Face* front_cap = selected_cap_face(result.front_document, result.front_selection);
	const Face* back_cap = selected_cap_face(result.back_document, result.back_selection);
	EXPECT_TRUE(front_cap->vertices.size() == 3U);
	EXPECT_TRUE(back_cap->vertices.size() == 3U);
}

TEST(QdrDocumentOperationPlaneCutTests, PlaneCutSupportsMultipleSimpleCapLoopsInOneClosedDocument) {
	Document document = two_cube_document();

        const PlaneCutResult result = quader_poly::plane_cut(
            document, center_x_cut(PlaneCutMode::KeepBoth));
        EXPECT_TRUE(result.changed);
	qdr_test::expect_valid_polygon_document_after_success(result, "successful multi-loop plane cut");
	EXPECT_TRUE(cap_face_count(result.front_selection) == 2);
	EXPECT_TRUE(cap_face_count(result.back_selection) == 2);
	check_closed_manifold(result.front_document);
	check_closed_manifold(result.back_document);
}

TEST(QdrDocumentOperationPlaneCutTests, PlaneCutRejectsInvalidInputsAndUnsupportedMeshesOpenMesh) {
	{
		Document plane = qdr_test::make_quad_document().document;
                const PlaneCutResult result = quader_poly::plane_cut(
                    plane, center_x_cut(PlaneCutMode::KeepBoth));
                EXPECT_FALSE(result.changed);
		EXPECT_TRUE(result.message.find("closed manifold") != std::string::npos);
	}

	

	

	

	
}

TEST(QdrDocumentOperationPlaneCutTests, PlaneCutRejectsInvalidInputsAndUnsupportedMeshesDegeneratePlane) {
	

	{
		Document cube = quader_poly::make_cube({});
                const PlaneCutResult result =
                    quader_poly::plane_cut(cube, {
                                                     {0.0F, 0.0F, 0.0F},
                                                     {1.0F, 0.0F, 0.0F},
                                                     {2.0F, 0.0F, 0.0F},
                                                     PlaneCutMode::KeepBoth,
                                                 });
                EXPECT_FALSE(result.changed);
		EXPECT_TRUE(result.message.find("non-collinear") != std::string::npos);
	}

	

	

	
}

TEST(QdrDocumentOperationPlaneCutTests, PlaneCutRejectsInvalidInputsAndUnsupportedMeshesNoInteriorIntersection) {
	

	

	{
		Document cube = quader_poly::make_cube({});
                const PlaneCutResult result =
                    quader_poly::plane_cut(cube, {
                                                     {3.0F, -1.0F, -1.0F},
                                                     {3.0F, 1.0F, -1.0F},
                                                     {3.0F, -1.0F, 1.0F},
                                                     PlaneCutMode::KeepBoth,
                                                 });
                EXPECT_FALSE(result.changed);
		EXPECT_TRUE(result.message.find("interior") != std::string::npos);
	}

	

	
}

TEST(QdrDocumentOperationPlaneCutTests, PlaneCutRejectsInvalidInputsAndUnsupportedMeshesCutPlaneLiesOnAnExistingFace) {
	

	

	

	{
		Document cube = quader_poly::make_cube({});
                const PlaneCutResult result =
                    quader_poly::plane_cut(cube, {
                                                     {1.0F, -1.0F, -1.0F},
                                                     {1.0F, 1.0F, -1.0F},
                                                     {1.0F, -1.0F, 1.0F},
                                                     PlaneCutMode::KeepBoth,
                                                 });
                EXPECT_FALSE(result.changed);
		EXPECT_TRUE(result.message.find("existing face") != std::string::npos);
	}

	
}

TEST(QdrDocumentOperationPlaneCutTests, PlaneCutRejectsInvalidInputsAndUnsupportedMeshesCutPlaneFollowsExistingEdges) {
	

	

	

	

	{
		Document cube = quader_poly::make_cube({});
                const PlaneCutResult result = quader_poly::plane_cut(
                    cube,
                    diagonal_on_existing_edges_cut(PlaneCutMode::KeepBoth));
                EXPECT_FALSE(result.changed);
		EXPECT_FALSE(result.message.empty());
	}
}
