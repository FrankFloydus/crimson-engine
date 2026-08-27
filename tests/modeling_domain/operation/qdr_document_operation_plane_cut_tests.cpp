////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_validation_test_helpers.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <utility>

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

} // namespace

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
