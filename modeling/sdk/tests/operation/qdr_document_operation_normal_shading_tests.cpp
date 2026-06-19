#include "operation_test_helpers.h"

using namespace quader_poly;

namespace {

struct BentCornerFixture {
    Document document;
    ElementId left_bottom = invalid_element_id;
    ElementId shared_bottom = invalid_element_id;
    ElementId shared_top = invalid_element_id;
    ElementId left_top = invalid_element_id;
    ElementId back_bottom = invalid_element_id;
    ElementId back_top = invalid_element_id;
    ElementId floor_face = invalid_element_id;
    ElementId wall_face = invalid_element_id;
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
    REQUIRE(fixture.floor_face != invalid_element_id);
    REQUIRE(fixture.wall_face != invalid_element_id);
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

} // namespace

TEST_CASE("shade smooth and flat control compiled face vertex normals", "[operation][normal_shading]")
{
    BentCornerFixture fixture = make_bent_corner_document();
    Selection selection = all_faces(fixture);

    const CompiledMesh flat_before = compile_document(fixture.document);
    REQUIRE(flat_before.vertices.size() == 8U);
    REQUIRE_FALSE(qdr_test::vec_nearly_equal(flat_before.vertices[1].normal, flat_before.vertices[4].normal));

    const OperationResult smooth = shade_selected_faces_smooth(fixture.document, selection);

    REQUIRE(smooth.changed);
    REQUIRE(fixture.document.faces[0].normal_shading == SurfaceShading::smooth);
    REQUIRE(fixture.document.faces[1].normal_shading == SurfaceShading::smooth);
    const CompiledMesh smooth_mesh = compile_document(fixture.document);
    REQUIRE(smooth_mesh.vertices.size() == 8U);
    CHECK(qdr_test::vec_nearly_equal(smooth_mesh.vertices[1].normal, smooth_mesh.vertices[4].normal));
    CHECK(qdr_test::vec_nearly_equal(smooth_mesh.vertices[2].normal, smooth_mesh.vertices[7].normal));

    const OperationResult flat = shade_selected_faces_flat(fixture.document, selection);

    REQUIRE(flat.changed);
    REQUIRE(fixture.document.faces[0].normal_shading == SurfaceShading::flat);
    REQUIRE(fixture.document.faces[1].normal_shading == SurfaceShading::flat);
    const CompiledMesh flat_after = compile_document(fixture.document);
    REQUIRE(flat_after.vertices.size() == 8U);
    CHECK(qdr_test::vec_nearly_equal(flat_after.vertices[1].normal, flat_before.vertices[1].normal));
    CHECK(qdr_test::vec_nearly_equal(flat_after.vertices[4].normal, flat_before.vertices[4].normal));
}

TEST_CASE("hard and soft normal edge operations split and rejoin smooth normals", "[operation][normal_shading]")
{
    BentCornerFixture fixture = make_bent_corner_document();
    Selection face_selection = all_faces(fixture);
    REQUIRE(shade_selected_faces_smooth(fixture.document, face_selection).changed);

    const CompiledMesh soft_before = compile_document(fixture.document);
    REQUIRE(qdr_test::vec_nearly_equal(soft_before.vertices[1].normal, soft_before.vertices[4].normal));

    Selection edge_selection = shared_edge_selection(fixture);
    const OperationResult hard = harden_selected_edge_normals(fixture.document, edge_selection);

    REQUIRE(hard.changed);
    REQUIRE(fixture.document.hard_normal_edges.size() == 1U);
    const CompiledMesh hard_mesh = compile_document(fixture.document);
    REQUIRE(hard_mesh.vertices.size() == 8U);
    CHECK_FALSE(qdr_test::vec_nearly_equal(hard_mesh.vertices[1].normal, hard_mesh.vertices[4].normal));
    CHECK(qdr_test::vec_nearly_equal(hard_mesh.vertices[1].normal, face_normal(fixture.document, fixture.document.faces[0])));
    CHECK(qdr_test::vec_nearly_equal(hard_mesh.vertices[4].normal, face_normal(fixture.document, fixture.document.faces[1])));

    const OperationResult soft = soften_selected_edge_normals(fixture.document, edge_selection);

    REQUIRE(soft.changed);
    REQUIRE(fixture.document.hard_normal_edges.empty());
    const CompiledMesh soft_after = compile_document(fixture.document);
    REQUIRE(soft_after.vertices.size() == 8U);
    CHECK(qdr_test::vec_nearly_equal(soft_after.vertices[1].normal, soft_after.vertices[4].normal));
}

TEST_CASE("soft normal edge operation softens adjacent flat faces", "[operation][normal_shading]")
{
    BentCornerFixture fixture = make_bent_corner_document();

    const CompiledMesh flat_before = compile_document(fixture.document);
    REQUIRE(flat_before.vertices.size() == 8U);
    REQUIRE_FALSE(qdr_test::vec_nearly_equal(flat_before.vertices[1].normal, flat_before.vertices[4].normal));

    Selection edge_selection = shared_edge_selection(fixture);
    const OperationResult soft = soften_selected_edge_normals(fixture.document, edge_selection);

    REQUIRE(soft.changed);
    REQUIRE(fixture.document.hard_normal_edges.empty());
    REQUIRE(fixture.document.faces[0].normal_shading == SurfaceShading::smooth);
    REQUIRE(fixture.document.faces[1].normal_shading == SurfaceShading::smooth);
    const CompiledMesh soft_mesh = compile_document(fixture.document);
    REQUIRE(soft_mesh.vertices.size() == 8U);
    CHECK(qdr_test::vec_nearly_equal(soft_mesh.vertices[1].normal, soft_mesh.vertices[4].normal));

    const OperationResult hard = harden_selected_edge_normals(fixture.document, edge_selection);

    REQUIRE(hard.changed);
    REQUIRE(fixture.document.hard_normal_edges.size() == 1U);
    const CompiledMesh hard_mesh = compile_document(fixture.document);
    REQUIRE(hard_mesh.vertices.size() == 8U);
    CHECK_FALSE(qdr_test::vec_nearly_equal(hard_mesh.vertices[1].normal, hard_mesh.vertices[4].normal));
}
