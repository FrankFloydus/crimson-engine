#include <catch2/catch_test_macros.hpp>

#include <quader_poly/document.h>

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
using quader_poly::Vec3;

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
	REQUIRE_FALSE(document.faces.empty());
	for (const auto& [edge, count] : edge_counts(document)) {
		(void)edge;
		CHECK(count == 2);
	}
	CHECK_FALSE(quader_poly::compile_document(document).indices.empty());
}

const Face* selected_cap_face(const Document& document, const quader_poly::Selection& selection)
{
	REQUIRE(selection.faces.size() == 1U);
	const Face* face = quader_poly::find_face(document, selection.faces.front());
	REQUIRE(face != nullptr);
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

TEST_CASE("plane cut splits a cube and keeps both closed halves", "[operation][plane_cut]")
{
	Document document = quader_poly::make_cube({});
	set_all_material_slots(document, 7);

	const PlaneCutResult result = quader_poly::plane_cut(document, center_x_cut(PlaneCutMode::keep_both));
	REQUIRE(result.changed);
	CHECK(result.split_count == 1);
	CHECK(result.removed_count == 0);
	CHECK(cap_face_count(result.front_selection) == 1);
	CHECK(cap_face_count(result.back_selection) == 1);
	check_closed_manifold(result.front_document);
	check_closed_manifold(result.back_document);

	const ElementId front_cap_id = result.front_selection.faces.front();
	const Face* front_cap = quader_poly::find_face(result.front_document, front_cap_id);
	REQUIRE(front_cap != nullptr);
	CHECK(front_cap->material_slot == 7);
	CHECK(dot(face_normal(result.front_document, *front_cap), Vec3 { -1.0F, 0.0F, 0.0F }) > 0.9F);

	const Face* back_cap = selected_cap_face(result.back_document, result.back_selection);
	CHECK(dot(face_normal(result.back_document, *back_cap), Vec3 { 1.0F, 0.0F, 0.0F }) > 0.9F);
}

TEST_CASE("plane cut supports destructive front and back modes", "[operation][plane_cut]")
{
	Document document = quader_poly::make_cube({});

	// PlaneCutOperation is still a translation-unit-local concrete type with no public factory,
	// so this pins the stable single-document replacement data consumed by its apply() path.
	const PlaneCutResult front = quader_poly::plane_cut(document, center_x_cut(PlaneCutMode::keep_front));
	REQUIRE(front.changed);
	CHECK(front.removed_count == 1);
	CHECK(front.front_selection.mode == quader_poly::SelectionMode::face);
	check_closed_manifold(front.front_document);
	check_closed_manifold(front.back_document);
	const Face* front_cap = selected_cap_face(front.front_document, front.front_selection);
	CHECK(dot(face_normal(front.front_document, *front_cap), Vec3 { -1.0F, 0.0F, 0.0F }) > 0.9F);

	const PlaneCutResult back = quader_poly::plane_cut(document, center_x_cut(PlaneCutMode::keep_back));
	REQUIRE(back.changed);
	CHECK(back.removed_count == 1);
	CHECK(back.back_selection.mode == quader_poly::SelectionMode::face);
	check_closed_manifold(back.front_document);
	check_closed_manifold(back.back_document);
	const Face* back_cap = selected_cap_face(back.back_document, back.back_selection);
	CHECK(dot(face_normal(back.back_document, *back_cap), Vec3 { 1.0F, 0.0F, 0.0F }) > 0.9F);
}

TEST_CASE("plane cut creates valid caps for tilted planes", "[operation][plane_cut]")
{
	Document document = quader_poly::make_cube({});

	const PlaneCutResult result = quader_poly::plane_cut(document, tilted_cut(PlaneCutMode::keep_both));
	REQUIRE(result.changed);
	REQUIRE(cap_face_count(result.front_selection) == 1);
	REQUIRE(cap_face_count(result.back_selection) == 1);
	check_closed_manifold(result.front_document);
	check_closed_manifold(result.back_document);
}

TEST_CASE("plane cut supports cuts through existing vertices", "[operation][plane_cut]")
{
	Document document = quader_poly::make_cube({});

	const PlaneCutResult result = quader_poly::plane_cut(document, corner_on_vertex_cut(PlaneCutMode::keep_both));
	REQUIRE(result.changed);
	CHECK(cap_face_count(result.front_selection) == 1);
	CHECK(cap_face_count(result.back_selection) == 1);
	check_closed_manifold(result.front_document);
	check_closed_manifold(result.back_document);

	const Face* front_cap = selected_cap_face(result.front_document, result.front_selection);
	const Face* back_cap = selected_cap_face(result.back_document, result.back_selection);
	CHECK(front_cap->vertices.size() == 3U);
	CHECK(back_cap->vertices.size() == 3U);
}

TEST_CASE("plane cut supports multiple simple cap loops in one closed document", "[operation][plane_cut]")
{
	Document document = two_cube_document();

	const PlaneCutResult result = quader_poly::plane_cut(document, center_x_cut(PlaneCutMode::keep_both));
	REQUIRE(result.changed);
	CHECK(cap_face_count(result.front_selection) == 2);
	CHECK(cap_face_count(result.back_selection) == 2);
	check_closed_manifold(result.front_document);
	check_closed_manifold(result.back_document);
}

TEST_CASE("plane cut rejects invalid inputs and unsupported meshes", "[operation][plane_cut]")
{
	SECTION("open mesh")
	{
		Document plane = quader_poly::make_plane({});
		const PlaneCutResult result = quader_poly::plane_cut(plane, center_x_cut(PlaneCutMode::keep_both));
		CHECK_FALSE(result.changed);
		CHECK(result.message.find("closed manifold") != std::string::npos);
	}

	SECTION("degenerate plane")
	{
		Document cube = quader_poly::make_cube({});
		const PlaneCutResult result = quader_poly::plane_cut(cube, {
			{ 0.0F, 0.0F, 0.0F },
			{ 1.0F, 0.0F, 0.0F },
			{ 2.0F, 0.0F, 0.0F },
			PlaneCutMode::keep_both,
		});
		CHECK_FALSE(result.changed);
		CHECK(result.message.find("non-collinear") != std::string::npos);
	}

	SECTION("no interior intersection")
	{
		Document cube = quader_poly::make_cube({});
		const PlaneCutResult result = quader_poly::plane_cut(cube, {
			{ 3.0F, -1.0F, -1.0F },
			{ 3.0F, 1.0F, -1.0F },
			{ 3.0F, -1.0F, 1.0F },
			PlaneCutMode::keep_both,
		});
		CHECK_FALSE(result.changed);
		CHECK(result.message.find("interior") != std::string::npos);
	}

	SECTION("cut plane lies on an existing face")
	{
		Document cube = quader_poly::make_cube({});
		const PlaneCutResult result = quader_poly::plane_cut(cube, {
			{ 1.0F, -1.0F, -1.0F },
			{ 1.0F, 1.0F, -1.0F },
			{ 1.0F, -1.0F, 1.0F },
			PlaneCutMode::keep_both,
		});
		CHECK_FALSE(result.changed);
		CHECK(result.message.find("existing face") != std::string::npos);
	}

	SECTION("cut plane follows existing edges")
	{
		Document cube = quader_poly::make_cube({});
		const PlaneCutResult result = quader_poly::plane_cut(cube, diagonal_on_existing_edges_cut(PlaneCutMode::keep_both));
		CHECK_FALSE(result.changed);
		CHECK_FALSE(result.message.empty());
	}
}
