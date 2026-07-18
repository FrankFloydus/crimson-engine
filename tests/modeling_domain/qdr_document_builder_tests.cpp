////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <quader/modeling/neutral/polygon/document_builder.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <vector>

namespace {

using namespace quader_poly;

bool vec_nearly_equal(quader::QVec3 left, quader::QVec3 right, float epsilon = 0.0001F)
{
    return std::abs(left.x - right.x) <= epsilon &&
        std::abs(left.y - right.y) <= epsilon &&
        std::abs(left.z - right.z) <= epsilon;
}

/**
 * Represents a Bounds value used by the automated test suite.
 */
struct Bounds {
    quader::QVec3 min;
    quader::QVec3 max;
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
    if (vertex == nullptr) {
        ADD_FAILURE() << "Missing document vertex " << id;
        static const Vertex kMissingVertex{};
        return &kMissingVertex;
    }
    return vertex;
}

} // namespace

TEST(QdrDocumentBuilderTests, DocumentBuilderCreatesVerticesQuadsAndMaterialSlots) {
    DocumentBuilder builder;
    const DocumentBuilder::VertexRef a = builder.vertex({ 0.0F, 0.0F, 0.0F });
    const DocumentBuilder::VertexRef b = builder.vertex({ 1.0F, 0.0F, 0.0F });
    const DocumentBuilder::VertexRef c = builder.vertex({ 1.0F, 1.0F, 0.0F });
    const DocumentBuilder::VertexRef d = builder.vertex({ 0.0F, 1.0F, 0.0F });

    const Document document =
        builder.quad(a, b, c, d, 3).assign_material_slot(7).build();

    EXPECT_TRUE(document.vertices.size() == 4U);
    EXPECT_TRUE(document.faces.size() == 1U);
    EXPECT_TRUE(document.next_vertex_id == 5U);
    EXPECT_TRUE(document.next_face_id == 2U);
    EXPECT_TRUE(document.faces.front().material_slot == 7U);
    EXPECT_TRUE((document.faces.front().vertices == std::vector<ElementId> { 1, 2, 3, 4 }));
    EXPECT_TRUE(vec_nearly_equal(require_vertex(document, 3)->position, { 1.0F, 1.0F, 0.0F }));
    EXPECT_FALSE(compile_document(document).indices.empty());
}

TEST(QdrDocumentBuilderTests, DocumentBuilderCreatesFacesFromVertexArraysAndIndices) {
    DocumentBuilder builder;
    const std::array<quader::QVec3, 4> positions {{
        { 0.0F, 0.0F, 0.0F },
        { 1.0F, 0.0F, 0.0F },
        { 1.0F, 1.0F, 0.0F },
        { 0.0F, 1.0F, 0.0F },
    }};
    const std::array<std::size_t, 3> triangle_indices { 0U, 1U, 2U };

    const Document document =
        builder.vertices(positions).face_by_index(triangle_indices, 2).build();

    EXPECT_TRUE(document.vertices.size() == 4U);
    EXPECT_TRUE(document.faces.size() == 1U);
    EXPECT_TRUE(document.faces.front().material_slot == 2U);
    EXPECT_TRUE((document.faces.front().vertices == std::vector<ElementId> { 1, 2, 3 }));
}

TEST(QdrDocumentBuilderTests, DocumentBuilderAppendsCubesFromBounds) {
    const Document document = DocumentBuilder {}
        .cube({ -2.0F, -3.0F, -4.0F }, { 2.0F, 3.0F, 4.0F }, 5)
        .build();

    EXPECT_TRUE(document.vertices.size() == 8U);
    EXPECT_TRUE(document.faces.size() == 6U);
    EXPECT_TRUE(std::ranges::all_of(document.faces, [](const Face& face) {
        return face.material_slot == 5U;
    }));

    const Bounds bounds = document_bounds(document);
    EXPECT_TRUE(vec_nearly_equal(bounds.min, { -2.0F, -3.0F, -4.0F }));
    EXPECT_TRUE(vec_nearly_equal(bounds.max, { 2.0F, 3.0F, 4.0F }));
    EXPECT_FALSE(compile_document(document).indices.empty());

    const Document swapped_bounds = DocumentBuilder {}
        .cube({ 2.0F, 3.0F, 4.0F }, { -2.0F, -3.0F, -4.0F }, 5)
        .build();
    const Bounds swapped = document_bounds(swapped_bounds);
    EXPECT_TRUE(vec_nearly_equal(swapped.min, { -2.0F, -3.0F, -4.0F }));
    EXPECT_TRUE(vec_nearly_equal(swapped.max, { 2.0F, 3.0F, 4.0F }));

    const Document degenerate = DocumentBuilder {}
        .cube({ 0.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 1.0F })
        .build();
    EXPECT_TRUE(degenerate.vertices.empty());
    EXPECT_TRUE(degenerate.faces.empty());
}

TEST(QdrDocumentBuilderTests, DocumentBuilderCreatesOrientedBoxesFromCorners) {
    const std::array<quader::QVec3, 8> corners {{
        { 2.0F, -2.0F, -1.0F },
        { 2.0F, 2.0F, -1.0F },
        { 2.0F, 2.0F, 1.0F },
        { 2.0F, -2.0F, 1.0F },
        { 4.0F, -2.0F, -1.0F },
        { 4.0F, 2.0F, -1.0F },
        { 4.0F, 2.0F, 1.0F },
        { 4.0F, -2.0F, 1.0F },
    }};

    const Document document =
        DocumentBuilder{}.box_from_corners(corners, 9).build();

    EXPECT_TRUE(document.vertices.size() == 8U);
    EXPECT_TRUE(document.faces.size() == 6U);
    EXPECT_TRUE(std::ranges::all_of(document.faces, [](const Face& face) {
        return face.material_slot == 9U;
    }));
    EXPECT_FALSE(compile_document(document).indices.empty());

    bool found_left = false;
    bool found_right = false;
    for (const Face& face : document.faces) {
        bool all_left = true;
        bool all_right = true;
        for (const ElementId vertex_id : face.vertices) {
            const Vertex* vertex = require_vertex(document, vertex_id);
            all_left = all_left &&qdr_test::nearly_equal(vertex->position.x, 2.0F, 0.0001);
            all_right = all_right &&qdr_test::nearly_equal(vertex->position.x, 4.0F, 0.0001);
        }
        const quader::QVec3 normal = face_normal(document, face);
        if (all_left) {
            found_left = true;
            EXPECT_NEAR(normal.x, -1.0F, 0.0001);
        }
        if (all_right) {
            found_right = true;
            EXPECT_NEAR(normal.x, 1.0F, 0.0001);
        }
    }
    EXPECT_TRUE(found_left);
    EXPECT_TRUE(found_right);
}

TEST(QdrDocumentBuilderTests, DocumentBuilderTransformsAllDetachedVertices) {
    DocumentBuilder builder;
    const DocumentBuilder::VertexRef a = builder.vertex({ 1.0F, 1.0F, 1.0F });
    builder.vertex({ 2.0F, 0.0F, 0.0F });
    builder.triangle(a, { 1U }, { 0U });

    Document translated = builder.translate({ 1.0F, 2.0F, 3.0F }).build();
    EXPECT_TRUE(vec_nearly_equal(require_vertex(translated, 1)->position, { 2.0F, 3.0F, 4.0F }));
    EXPECT_TRUE(vec_nearly_equal(require_vertex(translated, 2)->position, { 3.0F, 2.0F, 3.0F }));

    Document scaled = builder.scale({ 2.0F, 3.0F, 4.0F }, { 1.0F, 2.0F, 3.0F }).build();
    EXPECT_TRUE(vec_nearly_equal(require_vertex(scaled, 1)->position, { 3.0F, 5.0F, 7.0F }));
    EXPECT_TRUE(vec_nearly_equal(require_vertex(scaled, 2)->position, { 5.0F, 2.0F, 3.0F }));

    Transform3 transform;
    transform.x_axis = { 0.0F, 1.0F, 0.0F };
    transform.y_axis = { 1.0F, 0.0F, 0.0F };
    transform.z_axis = { 0.0F, 0.0F, 1.0F };
    transform.origin = { 5.0F, 0.0F, 0.0F };

    Document transformed = builder.transform(transform, { 1.0F, 2.0F, 3.0F }).build();
    EXPECT_TRUE(vec_nearly_equal(require_vertex(transformed, 1)->position, { 9.0F, 4.0F, 7.0F }));
    EXPECT_TRUE(vec_nearly_equal(require_vertex(transformed, 2)->position, { 6.0F, 6.0F, 3.0F }));
}

TEST(QdrDocumentBuilderTests, DocumentBuilderBuildCopiesAndTakeResets) {
    DocumentBuilder builder;
    const DocumentBuilder::VertexRef a = builder.vertex({ 0.0F, 0.0F, 0.0F });
    const DocumentBuilder::VertexRef b = builder.vertex({ 1.0F, 0.0F, 0.0F });
    const DocumentBuilder::VertexRef c = builder.vertex({ 0.0F, 1.0F, 0.0F });
    builder.triangle(a, b, c, 4);

    Document built = builder.build();
    built.vertices.front().position.x = 99.0F;

    EXPECT_TRUE(builder.preview().vertices.size() == 3U);
    EXPECT_NEAR(builder.preview().vertices.front().position.x, 0.0F, 0.0001);

    Document taken = builder.take();
    EXPECT_TRUE(taken.vertices.size() == 3U);
    EXPECT_TRUE(taken.faces.size() == 1U);
    EXPECT_TRUE(taken.faces.front().material_slot == 4U);
    EXPECT_TRUE(builder.preview().vertices.empty());
    EXPECT_TRUE(builder.preview().faces.empty());

    static_cast<void>(builder.vertex({ 7.0F, 8.0F, 9.0F }));
    const Document rebuilt = builder.build();
    EXPECT_TRUE(rebuilt.vertices.size() == 1U);
    EXPECT_TRUE(rebuilt.vertices.front().id == 1U);
    EXPECT_TRUE(vec_nearly_equal(rebuilt.vertices.front().position, { 7.0F, 8.0F, 9.0F }));
}
