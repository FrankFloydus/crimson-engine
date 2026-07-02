////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <mesh/polygon/document.hpp>

#include <algorithm>
#include <map>
#include <utility>

namespace {

using quader_poly::Document;
using quader_poly::ElementId;
using quader_poly::Face;
using quader_poly::PlaneCutMode;
using quader_poly::PlaneCutRequest;
using quader_poly::PlaneCutResult;
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

        const PlaneCutResult result = quader_poly::plane_cut(
            document, center_x_cut(PlaneCutMode::KeepBoth));
        EXPECT_TRUE(result.changed);
	EXPECT_TRUE(result.split_count == 1);
	EXPECT_TRUE(result.removed_count == 0);
	EXPECT_TRUE(cap_face_count(result.front_selection) == 1);
	EXPECT_TRUE(cap_face_count(result.back_selection) == 1);
	check_closed_manifold(result.front_document);
	check_closed_manifold(result.back_document);

	const ElementId front_cap_id = result.front_selection.faces.front();
	const Face* front_cap = quader_poly::find_face(result.front_document, front_cap_id);
	EXPECT_TRUE(front_cap != nullptr);
	EXPECT_TRUE(front_cap->material_slot == 7);
	EXPECT_TRUE(dot(face_normal(result.front_document, *front_cap), quader::QVec3 { -1.0F, 0.0F, 0.0F }) > 0.9F);

	const Face* back_cap = selected_cap_face(result.back_document, result.back_selection);
	EXPECT_TRUE(dot(face_normal(result.back_document, *back_cap), quader::QVec3 { 1.0F, 0.0F, 0.0F }) > 0.9F);
}

TEST(QdrDocumentOperationPlaneCutTests, PlaneCutSupportsDestructiveFrontAndBackModes) {
	Document document = quader_poly::make_cube({});

	// PlaneCutOperation is still a translation-unit-local concrete type with no public factory,
	// so this pins the stable single-document replacement data consumed by its apply() path.
        const PlaneCutResult front = quader_poly::plane_cut(
            document, center_x_cut(PlaneCutMode::KeepFront));
        EXPECT_TRUE(front.changed);
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
	EXPECT_TRUE(cap_face_count(result.front_selection) == 2);
	EXPECT_TRUE(cap_face_count(result.back_selection) == 2);
	check_closed_manifold(result.front_document);
	check_closed_manifold(result.back_document);
}

TEST(QdrDocumentOperationPlaneCutTests, PlaneCutRejectsInvalidInputsAndUnsupportedMeshesOpenMesh) {
	{
		Document plane = quader_poly::make_plane({});
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
