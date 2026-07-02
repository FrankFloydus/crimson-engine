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
#include <iostream>
#include <optional>

namespace {

bool nearly_equal(float left, float right, float epsilon = 0.0001F)
{
	return std::abs(left - right) <= epsilon;
}

void selection_clear_resets_active_state()
{
	quader_poly::Selection selection;
        selection.mode = quader_poly::SelectionMode::Edge;
        selection.vertices = { 1, 2 };
	selection.edges = { quader_poly::make_edge(4, 3) };
	selection.faces = { 9 };
	selection.has_active = true;
        selection.active_kind = quader_poly::ElementKind::Edge;
        selection.active_vertex = 1;
	selection.active_edge = quader_poly::make_edge(5, 6);
	selection.active_face = 9;

	require_test(!selection.empty(), "populated selection should not be empty before clear");
	selection.clear();

	require_test(selection.empty(), "clear should remove all component selections");
	require_test(!selection.has_active, "clear should remove active selection");
        require_test(selection.active_kind == quader_poly::ElementKind::Vertex,
                     "clear should reset active kind to vertex");
        require_test(selection.active_vertex == quader_poly::kInvalidElementId,
                     "clear should reset active vertex");
        require_test(selection.active_edge == quader_poly::Edge {}, "clear should reset active edge");
        require_test(selection.active_face == quader_poly::kInvalidElementId,
                     "clear should reset active face");
}

void face_perimeter_predicates_remain_data_driven()
{
	quader_poly::FacePerimeterInfo info;
	require_test(info.empty(), "empty perimeter info should report empty");
	require_test(!info.has_open_edges(), "empty perimeter info should not report open edges");
	require_test(!info.has_only_open_edges(), "empty perimeter info should not report only-open edges");
	require_test(!info.has_only_closed_edges(), "empty perimeter info should not report only-closed edges");

	info.edges = { quader_poly::make_edge(1, 2), quader_poly::make_edge(2, 3) };
	info.open_edges = info.edges;
	require_test(!info.empty(), "perimeter info with edges should not be empty");
	require_test(info.has_open_edges(), "open edge list should drive has_open_edges");
	require_test(info.has_only_open_edges(), "all edges in open list should drive only-open");
	require_test(!info.has_closed_edges(), "no closed edges should report no closed edges");

	info.open_edges.clear();
	info.closed_edges = info.edges;
	require_test(info.has_closed_edges(), "closed edge list should drive has_closed_edges");
	require_test(info.has_only_closed_edges(), "all edges in closed list should drive only-closed");
	require_test(!info.has_only_open_edges(), "closed edges should not report only-open");

	info.nonmanifold_edges = { info.edges.front() };
	require_test(info.has_nonmanifold_edges(), "non-manifold edge list should drive non-manifold predicate");
}

void vec3_math_and_edge_normalization_stay_linkable_after_split()
{
	const quader::QVec3 a { 1.0F, 2.0F, 3.0F };
	const quader::QVec3 b { -4.0F, 5.0F, -6.0F };

	const quader::QVec3 sum = a + b;
	require_test(nearly_equal(sum.x, -3.0F) && nearly_equal(sum.y, 7.0F) && nearly_equal(sum.z, -3.0F),
		"quader::QVec3 addition should stay available from qdr_document");
	const quader::QVec3 difference = a - b;
	require_test(nearly_equal(difference.x, 5.0F) && nearly_equal(difference.y, -3.0F) && nearly_equal(difference.z, 9.0F),
		"quader::QVec3 subtraction should stay available from qdr_document");
	const quader::QVec3 scaled = a * 2.0F;
	require_test(nearly_equal(scaled.x, 2.0F) && nearly_equal(scaled.y, 4.0F) && nearly_equal(scaled.z, 6.0F),
		"quader::QVec3 scalar multiply should stay available from qdr_document");
	require_test(nearly_equal(quader::dot(a, b), -12.0F), "quader::QVec3 dot product should route through document model API");

	const quader::QVec3 cross = quader::cross({ 1.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F });
	require_test(nearly_equal(cross.x, 0.0F) && nearly_equal(cross.y, 0.0F) && nearly_equal(cross.z, 1.0F),
		"quader::QVec3 cross product should stay right-handed");
	const quader::QVec3 normalized = quader::normalize_or_zero({ 0.0F, 3.0F, 4.0F });
	require_test(nearly_equal(normalized.x, 0.0F) && nearly_equal(normalized.y, 0.6F) && nearly_equal(normalized.z, 0.8F),
		"quader::QVec3 normalization should keep model API behavior");

	require_test(quader_poly::make_edge(7, 3) == quader_poly::Edge { 3, 7 },
		"make_edge should normalize edge ids after model API split");
}

void edge_factor_helpers_are_stable_after_model_api_split()
{
	quader_poly::Document document;
	const quader_poly::ElementId a = quader_poly::add_vertex(document, { 0.0F, 0.0F, 0.0F });
	const quader_poly::ElementId b = quader_poly::add_vertex(document, { 10.0F, 0.0F, 0.0F });
	(void)quader_poly::add_face(document, std::array<quader_poly::ElementId, 3> {
		a,
		b,
		quader_poly::add_vertex(document, { 0.0F, 1.0F, 0.0F }),
	});

	const std::optional<float> midpoint =
		quader_poly::edge_factor_from_position(document, quader_poly::make_edge(b, a), { 5.0F, 3.0F, 0.0F });
	require_test(midpoint.has_value() && nearly_equal(*midpoint, 0.5F),
		"edge factor from position should normalize reversed edges and project to the segment");
	const std::optional<float> before_start =
		quader_poly::edge_factor_from_position(document, quader_poly::make_edge(a, b), { -100.0F, 0.0F, 0.0F });
	require_test(before_start.has_value() && nearly_equal(*before_start, 0.0F),
		"edge factor from position should clamp below the segment");
	const std::optional<float> after_end =
		quader_poly::edge_factor_from_position(document, quader_poly::make_edge(a, b), { 100.0F, 0.0F, 0.0F });
	require_test(after_end.has_value() && nearly_equal(*after_end, 1.0F),
		"edge factor from position should clamp above the segment");

	const std::optional<float> from_ray =
		quader_poly::edge_factor_from_ray(document, quader_poly::make_edge(a, b), { { 7.5F, 2.0F, 0.0F }, { 0.0F, -1.0F, 0.0F } });
	require_test(from_ray.has_value() && nearly_equal(*from_ray, 0.75F),
		"edge factor from ray should use closest segment point semantics");
	require_test(!quader_poly::edge_factor_from_ray(document, quader_poly::make_edge(a, b), { { 0.0F, 0.0F, 0.0F }, {} }).has_value(),
		"edge factor from ray should reject zero-length ray directions");
	require_test(!quader_poly::edge_factor_from_position(document, quader_poly::make_edge(a, 999), { 0.0F, 0.0F, 0.0F }).has_value(),
		"edge factor from position should reject missing vertices");

	const quader_poly::ElementId tiny_a = quader_poly::add_vertex(document, { 20.0F, 0.0F, 0.0F });
	const quader_poly::ElementId tiny_b = quader_poly::add_vertex(document, { 20.0005F, 0.0F, 0.0F });
	require_test(!quader_poly::edge_factor_from_position(document, quader_poly::make_edge(tiny_a, tiny_b), { 20.0F, 0.0F, 0.0F }).has_value(),
		"edge factor from position should preserve the tiny-edge rejection threshold");
}

} // namespace

TEST(QdrDocumentModelApiTests, QdrDocumentModelApiTests) {
	selection_clear_resets_active_state();
	face_perimeter_predicates_remain_data_driven();
	vec3_math_and_edge_normalization_stay_linkable_after_split();
	edge_factor_helpers_are_stable_after_model_api_split();
}
