////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <mesh/polygon/document_builder.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <vector>

namespace {

using namespace quader_poly;

bool vec_nearly_equal(Vec3 left, Vec3 right, float epsilon = 0.0001F)
{
    return std::abs(left.x - right.x) <= epsilon &&
        std::abs(left.y - right.y) <= epsilon &&
        std::abs(left.z - right.z) <= epsilon;
}

/**
 * Represents a Bounds value used by the automated test suite.
 */
struct Bounds {
    Vec3 min;
    Vec3 max;
};

Bounds document_bounds(const Document& document)
{
    Bounds bounds {
        { std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max() },
        { std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest() },
    };
    for (const Vertex& vertex : document.vertices) {
        bounds.min.x = std::min(bounds.min.x, vertex.position.x);
        bounds.min.y = std::min(bounds.min.y, vertex.position.y);
        bounds.min.z = std::min(bounds.min.z, vertex.position.z);
        bounds.max.x = std::max(bounds.max.x, vertex.position.x);
        bounds.max.y = std::max(bounds.max.y, vertex.position.y);
        bounds.max.z = std::max(bounds.max.z, vertex.position.z);
    }
    return bounds;
}

const Vertex* require_vertex(const Document& document, ElementId id)
{
    const Vertex* vertex = find_vertex(document, id);
    REQUIRE(vertex != nullptr);
    return vertex;
}

} // namespace

TEST_CASE("document builder creates vertices, quads, and material slots", "[document-builder]")
{
    DocumentBuilder builder;
    const DocumentBuilder::VertexRef a = builder.vertex({ 0.0F, 0.0F, 0.0F });
    const DocumentBuilder::VertexRef b = builder.vertex({ 1.0F, 0.0F, 0.0F });
    const DocumentBuilder::VertexRef c = builder.vertex({ 1.0F, 1.0F, 0.0F });
    const DocumentBuilder::VertexRef d = builder.vertex({ 0.0F, 1.0F, 0.0F });

    const Document document = builder.quad(a, b, c, d, 3).assignMaterialSlot(7).build();

    REQUIRE(document.vertices.size() == 4U);
    REQUIRE(document.faces.size() == 1U);
    CHECK(document.next_vertex_id == 5U);
    CHECK(document.next_face_id == 2U);
    CHECK(document.faces.front().material_slot == 7U);
    CHECK(document.faces.front().vertices == std::vector<ElementId> { 1, 2, 3, 4 });
    CHECK(vec_nearly_equal(require_vertex(document, 3)->position, { 1.0F, 1.0F, 0.0F }));
    CHECK_FALSE(compile_document(document).indices.empty());
}

TEST_CASE("document builder creates faces from vertex arrays and indices", "[document-builder]")
{
    DocumentBuilder builder;
    const std::array<Vec3, 4> positions {{
        { 0.0F, 0.0F, 0.0F },
        { 1.0F, 0.0F, 0.0F },
        { 1.0F, 1.0F, 0.0F },
        { 0.0F, 1.0F, 0.0F },
    }};
    const std::array<std::size_t, 3> triangle_indices { 0U, 1U, 2U };

    const Document document = builder.vertices(positions).faceByIndex(triangle_indices, 2).build();

    REQUIRE(document.vertices.size() == 4U);
    REQUIRE(document.faces.size() == 1U);
    CHECK(document.faces.front().material_slot == 2U);
    CHECK(document.faces.front().vertices == std::vector<ElementId> { 1, 2, 3 });
}

TEST_CASE("document builder appends cubes from bounds", "[document-builder]")
{
    const Document document = DocumentBuilder {}
        .cube({ -2.0F, -3.0F, -4.0F }, { 2.0F, 3.0F, 4.0F }, 5)
        .build();

    REQUIRE(document.vertices.size() == 8U);
    REQUIRE(document.faces.size() == 6U);
    CHECK(std::ranges::all_of(document.faces, [](const Face& face) {
        return face.material_slot == 5U;
    }));

    const Bounds bounds = document_bounds(document);
    CHECK(vec_nearly_equal(bounds.min, { -2.0F, -3.0F, -4.0F }));
    CHECK(vec_nearly_equal(bounds.max, { 2.0F, 3.0F, 4.0F }));
    CHECK_FALSE(compile_document(document).indices.empty());

    const Document swapped_bounds = DocumentBuilder {}
        .cube({ 2.0F, 3.0F, 4.0F }, { -2.0F, -3.0F, -4.0F }, 5)
        .build();
    const Bounds swapped = document_bounds(swapped_bounds);
    CHECK(vec_nearly_equal(swapped.min, { -2.0F, -3.0F, -4.0F }));
    CHECK(vec_nearly_equal(swapped.max, { 2.0F, 3.0F, 4.0F }));

    const Document degenerate = DocumentBuilder {}
        .cube({ 0.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 1.0F })
        .build();
    CHECK(degenerate.vertices.empty());
    CHECK(degenerate.faces.empty());
}

TEST_CASE("document builder creates oriented boxes from corners", "[document-builder]")
{
    const std::array<Vec3, 8> corners {{
        { 2.0F, -2.0F, -1.0F },
        { 2.0F, 2.0F, -1.0F },
        { 2.0F, 2.0F, 1.0F },
        { 2.0F, -2.0F, 1.0F },
        { 4.0F, -2.0F, -1.0F },
        { 4.0F, 2.0F, -1.0F },
        { 4.0F, 2.0F, 1.0F },
        { 4.0F, -2.0F, 1.0F },
    }};

    const Document document = DocumentBuilder {}.boxFromCorners(corners, 9).build();

    REQUIRE(document.vertices.size() == 8U);
    REQUIRE(document.faces.size() == 6U);
    CHECK(std::ranges::all_of(document.faces, [](const Face& face) {
        return face.material_slot == 9U;
    }));
    CHECK_FALSE(compile_document(document).indices.empty());

    bool found_left = false;
    bool found_right = false;
    for (const Face& face : document.faces) {
        bool all_left = true;
        bool all_right = true;
        for (const ElementId vertex_id : face.vertices) {
            const Vertex* vertex = require_vertex(document, vertex_id);
            all_left = all_left && vertex->position.x == Catch::Approx(2.0F);
            all_right = all_right && vertex->position.x == Catch::Approx(4.0F);
        }
        const Vec3 normal = face_normal(document, face);
        if (all_left) {
            found_left = true;
            CHECK(normal.x == Catch::Approx(-1.0F));
        }
        if (all_right) {
            found_right = true;
            CHECK(normal.x == Catch::Approx(1.0F));
        }
    }
    CHECK(found_left);
    CHECK(found_right);
}

TEST_CASE("document builder transforms all detached vertices", "[document-builder]")
{
    DocumentBuilder builder;
    const DocumentBuilder::VertexRef a = builder.vertex({ 1.0F, 1.0F, 1.0F });
    builder.vertex({ 2.0F, 0.0F, 0.0F });
    builder.triangle(a, { 1U }, { 0U });

    Document translated = builder.translate({ 1.0F, 2.0F, 3.0F }).build();
    CHECK(vec_nearly_equal(require_vertex(translated, 1)->position, { 2.0F, 3.0F, 4.0F }));
    CHECK(vec_nearly_equal(require_vertex(translated, 2)->position, { 3.0F, 2.0F, 3.0F }));

    Document scaled = builder.scale({ 2.0F, 3.0F, 4.0F }, { 1.0F, 2.0F, 3.0F }).build();
    CHECK(vec_nearly_equal(require_vertex(scaled, 1)->position, { 3.0F, 5.0F, 7.0F }));
    CHECK(vec_nearly_equal(require_vertex(scaled, 2)->position, { 5.0F, 2.0F, 3.0F }));

    Transform3 transform;
    transform.x_axis = { 0.0F, 1.0F, 0.0F };
    transform.y_axis = { 1.0F, 0.0F, 0.0F };
    transform.z_axis = { 0.0F, 0.0F, 1.0F };
    transform.origin = { 5.0F, 0.0F, 0.0F };

    Document transformed = builder.transform(transform, { 1.0F, 2.0F, 3.0F }).build();
    CHECK(vec_nearly_equal(require_vertex(transformed, 1)->position, { 9.0F, 4.0F, 7.0F }));
    CHECK(vec_nearly_equal(require_vertex(transformed, 2)->position, { 6.0F, 6.0F, 3.0F }));
}

TEST_CASE("document builder build copies and take resets", "[document-builder]")
{
    DocumentBuilder builder;
    const DocumentBuilder::VertexRef a = builder.vertex({ 0.0F, 0.0F, 0.0F });
    const DocumentBuilder::VertexRef b = builder.vertex({ 1.0F, 0.0F, 0.0F });
    const DocumentBuilder::VertexRef c = builder.vertex({ 0.0F, 1.0F, 0.0F });
    builder.triangle(a, b, c, 4);

    Document built = builder.build();
    built.vertices.front().position.x = 99.0F;

    REQUIRE(builder.preview().vertices.size() == 3U);
    CHECK(builder.preview().vertices.front().position.x == Catch::Approx(0.0F));

    Document taken = builder.take();
    REQUIRE(taken.vertices.size() == 3U);
    REQUIRE(taken.faces.size() == 1U);
    CHECK(taken.faces.front().material_slot == 4U);
    CHECK(builder.preview().vertices.empty());
    CHECK(builder.preview().faces.empty());

    static_cast<void>(builder.vertex({ 7.0F, 8.0F, 9.0F }));
    const Document rebuilt = builder.build();
    REQUIRE(rebuilt.vertices.size() == 1U);
    CHECK(rebuilt.vertices.front().id == 1U);
    CHECK(vec_nearly_equal(rebuilt.vertices.front().position, { 7.0F, 8.0F, 9.0F }));
}
