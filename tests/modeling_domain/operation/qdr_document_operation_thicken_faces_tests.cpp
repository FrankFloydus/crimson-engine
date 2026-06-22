////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "operation_test_helpers.h"

#include <algorithm>

using namespace quader_poly;

TEST_CASE("thicken faces turns one open face into a closed thin volume", "[operation][thicken_faces]")
{
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	Selection selection = qdr_test::face_selection({ fixture.face }, fixture.face);

	const OperationResult result = thicken_selected_faces(fixture.document, selection, 0.5F, false);

	REQUIRE(result.changed);
	REQUIRE(fixture.document.faces.size() == 6U);
	REQUIRE(selection.mode == SelectionMode::face);
	REQUIRE(selection.faces.size() == 6U);
	REQUIRE(selection.has_active);
	REQUIRE(selection.active_face == fixture.face);
	REQUIRE(qdr_test::every_edge_is_closed(fixture.document));
	REQUIRE(qdr_test::every_face_compiles_to_triangles(fixture.document));
}

TEST_CASE("negative thicken grows from the fixed source cap and winds inward", "[operation][thicken_faces]")
{
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	Selection selection = qdr_test::face_selection({ fixture.face }, fixture.face);
	const ElementId original_face_id = selection.active_face;
	const Face* source_before = find_face(fixture.document, original_face_id);
	REQUIRE(source_before != nullptr);
	const Vec3 source_normal = face_normal(fixture.document, *source_before);

	const OperationResult result = thicken_selected_faces(fixture.document, selection, -0.5F, false);

	REQUIRE(result.changed);
	REQUIRE(selection.mode == SelectionMode::face);
	REQUIRE(selection.faces.size() == 6U);
	REQUIRE(selection.has_active);
	REQUIRE(selection.active_kind == ElementKind::face);
	REQUIRE(selection.active_face == original_face_id);
	const Face* source_cap = find_face(fixture.document, original_face_id);
	const auto offset_cap = std::find_if(fixture.document.faces.begin(), fixture.document.faces.end(), [&fixture](const Face& face) {
		return qdr_test::face_centroid(fixture.document, face).z > 0.49F;
	});
	REQUIRE(source_cap != nullptr);
	REQUIRE(offset_cap != fixture.document.faces.end());
	REQUIRE(dot(face_normal(fixture.document, *source_cap), source_normal) > 0.5F);
	REQUIRE(dot(face_normal(fixture.document, *offset_cap), source_normal) < -0.5F);
	REQUIRE(qdr_test::face_centroid(fixture.document, *source_cap).z < 0.01F);
	REQUIRE(qdr_test::face_centroid(fixture.document, *offset_cap).z > 0.49F);
	const auto front_side_face = std::find_if(fixture.document.faces.begin(), fixture.document.faces.end(), [&fixture](const Face& face) {
		const Vec3 center = qdr_test::face_centroid(fixture.document, face);
		return center.y < -0.9F && center.z > 0.01F && center.z < 0.49F;
	});
	REQUIRE(front_side_face != fixture.document.faces.end());
	REQUIRE(dot(face_normal(fixture.document, *front_side_face), { 0.0F, 1.0F, 0.0F }) > 0.5F);
	REQUIRE(qdr_test::every_edge_is_closed(fixture.document));
	REQUIRE(qdr_test::every_face_compiles_to_triangles(fixture.document));
}

TEST_CASE("negative thicken ignores legacy from-center movement", "[operation][thicken_faces]")
{
	qdr_test::QuadFixture fixture = qdr_test::make_quad_document();
	Selection selection = qdr_test::face_selection({ fixture.face }, fixture.face);

	const OperationResult result = thicken_selected_faces(fixture.document, selection, -0.5F, true);

	REQUIRE(result.changed);
	const auto minmax = std::minmax_element(fixture.document.vertices.begin(), fixture.document.vertices.end(), [](const Vertex& left, const Vertex& right) {
		return left.position.z < right.position.z;
	});
	REQUIRE(minmax.first != fixture.document.vertices.end());
	REQUIRE(minmax.first->position.z < 0.01F);
	REQUIRE(minmax.second->position.z > 0.49F);
	REQUIRE(qdr_test::every_edge_is_closed(fixture.document));
}
