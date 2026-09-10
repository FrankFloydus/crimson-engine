////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <quader/modeling/neutral/polygon/document.hpp>
#include <quader/modeling/neutral/polygon/document_validation.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <ranges>
#include <vector>

namespace {

using namespace quader_poly;

bool has_code(const PolygonDocumentValidationReport& report, PolygonDocumentDiagnosticCode code)
{
    return std::ranges::any_of(report.diagnostics, [code](const PolygonDocumentDiagnostic& diagnostic) {
        return diagnostic.code == code;
    });
}

const PolygonDocumentDiagnostic* find_code(
    const PolygonDocumentValidationReport& report,
    PolygonDocumentDiagnosticCode code)
{
    const auto diagnostic = std::ranges::find_if(report.diagnostics, [code](const PolygonDocumentDiagnostic& candidate) {
        return candidate.code == code;
    });
    return diagnostic == report.diagnostics.end() ? nullptr : &(*diagnostic);
}

::testing::AssertionResult make_triangle_document(Document& document)
{
    const ElementId a = add_vertex(document, { 0.0F, 0.0F, 0.0F });
    const ElementId b = add_vertex(document, { 1.0F, 0.0F, 0.0F });
    const ElementId c = add_vertex(document, { 0.0F, 1.0F, 0.0F });
    return require_test(
        add_face(document, std::array<ElementId, 3> { a, b, c }) != kInvalidElementId,
        "triangle face should be created");
}

Document make_large_gapped_triangle_document(std::size_t retained_face_count)
{
    Document document;
    for (std::size_t triangle_index = 0;
         triangle_index <= retained_face_count;
         ++triangle_index) {
        const float x = static_cast<float>(triangle_index) * 2.0F;
        const ElementId a = add_vertex(document, { x, 0.0F, 0.0F });
        const ElementId b = add_vertex(document, { x + 1.0F, 0.0F, 0.0F });
        const ElementId c = add_vertex(document, { x, 1.0F, 0.0F });
        const ElementId face_id = add_face(
            document,
            std::array<ElementId, 3> { a, b, c },
            static_cast<std::uint32_t>(triangle_index % 11U));
        if (face_id == kInvalidElementId) {
            return {};
        }
        document.faces.back().uvs = {
            { 0.0F, 0.0F },
            { 1.0F, 0.0F },
            { 0.0F, 1.0F },
        };
    }

    document.faces.erase(document.faces.begin());
    document.vertices.erase(
        document.vertices.begin(),
        document.vertices.begin() + 3);
    return document;
}

Document make_large_gapped_quad_document(std::size_t retained_face_count)
{
    Document document;
    for (std::size_t quad_index = 0;
         quad_index <= retained_face_count;
         ++quad_index) {
        const float x = static_cast<float>(quad_index) * 2.0F;
        const ElementId a = add_vertex(document, { x, 0.0F, 0.0F });
        const ElementId b = add_vertex(document, { x + 1.0F, 0.0F, 0.0F });
        const ElementId c = add_vertex(document, { x + 1.0F, 1.0F, 0.0F });
        const ElementId d = add_vertex(document, { x, 1.0F, 0.0F });
        if (add_face(
                document,
                std::array<ElementId, 4> { a, b, c, d },
                static_cast<std::uint32_t>(quad_index % 13U)) ==
            kInvalidElementId) {
            return {};
        }
    }

    document.faces.erase(document.faces.begin());
    document.vertices.erase(
        document.vertices.begin(),
        document.vertices.begin() + 4);
    return document;
}

} // namespace

TEST(QdrDocumentValidationTests, ValidPrimitiveHasNoDiagnostics)
{
    const Document document = make_cube({});
    const PolygonDocumentValidationReport report = validate_polygon_document(document);

    EXPECT_TRUE(report.ok());
    EXPECT_FALSE(report.has_errors());
    EXPECT_TRUE(report.diagnostics.empty());
}

TEST(QdrDocumentValidationTests, ReportsDuplicateVertexAndFaceIds)
{
    Document document;
    document.vertices = {
        { 1, { 0.0F, 0.0F, 0.0F } },
        { 1, { 0.5F, 0.0F, 0.0F } },
        { 2, { 1.0F, 0.0F, 0.0F } },
        { 3, { 0.0F, 1.0F, 0.0F } },
    };
    document.faces = {
        { 7, { 1, 2, 3 } },
        { 7, { 1, 3, 2 } },
    };

    const PolygonDocumentValidationReport report = validate_polygon_document(document);

    EXPECT_FALSE(report.ok());
    const PolygonDocumentDiagnostic* vertex = find_code(report, PolygonDocumentDiagnosticCode::DuplicateVertexId);
    ASSERT_TRUE(vertex != nullptr);
    EXPECT_TRUE(vertex->severity == PolygonDocumentDiagnosticSeverity::Error);
    EXPECT_TRUE(vertex->vertex_id == 1U);

    const PolygonDocumentDiagnostic* face = find_code(report, PolygonDocumentDiagnosticCode::DuplicateFaceId);
    ASSERT_TRUE(face != nullptr);
    EXPECT_TRUE(face->severity == PolygonDocumentDiagnosticSeverity::Error);
    EXPECT_TRUE(face->face_id == 7U);
}

TEST(QdrDocumentValidationTests, ReportsMissingRepeatedAndUvMismatchFaceData)
{
    Document document;
    document.vertices = {
        { 1, { 0.0F, 0.0F, 0.0F } },
        { 2, { 1.0F, 0.0F, 0.0F } },
        { 3, { 0.0F, 1.0F, 0.0F } },
    };
    document.faces.push_back({ 4, { 1, 2, 99, 2 } });
    document.faces.front().uvs = { { 0.0F, 0.0F }, { 1.0F, 0.0F } };

    const PolygonDocumentValidationReport report = validate_polygon_document(document);

    EXPECT_FALSE(report.ok());
    const PolygonDocumentDiagnostic* missing = find_code(report, PolygonDocumentDiagnosticCode::MissingVertexReference);
    ASSERT_TRUE(missing != nullptr);
    EXPECT_TRUE(missing->face_id == 4U);
    EXPECT_TRUE(missing->vertex_id == 99U);

    const PolygonDocumentDiagnostic* repeated = find_code(report, PolygonDocumentDiagnosticCode::RepeatedFaceVertex);
    ASSERT_TRUE(repeated != nullptr);
    EXPECT_TRUE(repeated->face_id == 4U);
    EXPECT_TRUE(repeated->vertex_id == 2U);

    const PolygonDocumentDiagnostic* uvs = find_code(report, PolygonDocumentDiagnosticCode::FaceUvCountMismatch);
    ASSERT_TRUE(uvs != nullptr);
    EXPECT_TRUE(uvs->face_id == 4U);
    EXPECT_FALSE(uvs->message.empty());
}

TEST(QdrDocumentValidationTests, ReportsDegenerateLoopAndTriangulationFailure)
{
    Document document;
    const ElementId a = add_vertex(document, { 0.0F, 0.0F, 0.0F });
    const ElementId b = add_vertex(document, { 1.0F, 0.0F, 0.0F });
    const ElementId c = add_vertex(document, { 2.0F, 0.0F, 0.0F });
    const ElementId d = add_vertex(document, { 3.0F, 0.0F, 0.0F });
    ASSERT_TRUE(require_test(add_face(document, std::array<ElementId, 4> { a, b, c, d }) != kInvalidElementId, "collinear quad should be authored"));

    const PolygonDocumentValidationReport report = validate_polygon_document(document);

    EXPECT_FALSE(report.ok());
    EXPECT_TRUE(has_code(report, PolygonDocumentDiagnosticCode::DegenerateFaceLoop));
    EXPECT_TRUE(has_code(report, PolygonDocumentDiagnosticCode::TriangulationFailure));
}

TEST(QdrDocumentValidationTests, ReportsNonmanifoldEdges)
{
    Document document;
    const ElementId a = add_vertex(document, { 0.0F, 0.0F, 0.0F });
    const ElementId b = add_vertex(document, { 1.0F, 0.0F, 0.0F });
    const ElementId c = add_vertex(document, { 0.0F, 1.0F, 0.0F });
    const ElementId d = add_vertex(document, { 0.0F, -1.0F, 0.0F });
    const ElementId e = add_vertex(document, { 0.0F, 0.0F, 1.0F });
    ASSERT_TRUE(require_test(add_face(document, std::array<ElementId, 3> { a, b, c }) != kInvalidElementId, "first face should be created"));
    ASSERT_TRUE(require_test(add_face(document, std::array<ElementId, 3> { b, a, d }) != kInvalidElementId, "second face should be created"));
    ASSERT_TRUE(require_test(add_face(document, std::array<ElementId, 3> { a, b, e }) != kInvalidElementId, "third face should be created"));

    const PolygonDocumentValidationReport report = validate_polygon_document(document);

    EXPECT_FALSE(report.ok());
    const PolygonDocumentDiagnostic* diagnostic = find_code(report, PolygonDocumentDiagnosticCode::NonmanifoldEdge);
    ASSERT_TRUE(diagnostic != nullptr);
    EXPECT_TRUE(diagnostic->edge == make_edge(a, b));
    EXPECT_TRUE((diagnostic->vertex_ids == std::vector<ElementId> { a, b }));
}

TEST(QdrDocumentValidationTests, ReportsInvalidHardNormalEdges)
{
    Document document;
    ASSERT_TRUE(make_triangle_document(document));
    const ElementId a = document.vertices[0].id;
    const ElementId b = document.vertices[1].id;
    document.hard_normal_edges = {
        make_edge(a, b),
        { b, b },
        make_edge(a, 99U),
    };

    const PolygonDocumentValidationReport report = validate_polygon_document(document);

    EXPECT_FALSE(report.ok());
    const std::size_t invalid_count = static_cast<std::size_t>(std::ranges::count_if(
        report.diagnostics,
        [](const PolygonDocumentDiagnostic& diagnostic) {
            return diagnostic.code == PolygonDocumentDiagnosticCode::InvalidHardNormalEdge;
        }));
    EXPECT_TRUE(invalid_count == 2U);
}

TEST(QdrDocumentValidationTests,
     ReportsInvalidAndConflictingSoftNormalEdgeStates)
{
    Document document;
    ASSERT_TRUE(make_triangle_document(document));
    const ElementId a = document.vertices[0].id;
    const ElementId b = document.vertices[1].id;
    const Edge edge = make_edge(a, b);
    document.hard_normal_edges = { edge };
    document.soft_normal_edges = {
        edge,
        { edge.b, edge.a },
        make_edge(a, 99U),
    };

    const PolygonDocumentValidationReport report =
        validate_polygon_document(document);

    EXPECT_FALSE(report.ok());
    EXPECT_EQ(std::ranges::count_if(
                  report.diagnostics,
                  [](const PolygonDocumentDiagnostic& diagnostic) {
                      return diagnostic.code ==
                             PolygonDocumentDiagnosticCode::InvalidSoftNormalEdge;
                  }),
              2);
    EXPECT_TRUE(has_code(
        report, PolygonDocumentDiagnosticCode::ConflictingNormalEdgeState));
}

TEST(QdrDocumentValidationTests, ReportsUnreferencedVerticesAsWarnings)
{
    Document document;
    ASSERT_TRUE(make_triangle_document(document));
    const ElementId unused = add_vertex(document, { 2.0F, 2.0F, 2.0F });

    const PolygonDocumentValidationReport report = validate_polygon_document(document);

    EXPECT_TRUE(report.ok());
    EXPECT_FALSE(report.has_errors());
    const PolygonDocumentDiagnostic* diagnostic = find_code(report, PolygonDocumentDiagnosticCode::UnreferencedVertex);
    ASSERT_TRUE(diagnostic != nullptr);
    EXPECT_TRUE(diagnostic->severity == PolygonDocumentDiagnosticSeverity::Warning);
    EXPECT_TRUE(diagnostic->vertex_id == unused);
}

TEST(
    QdrDocumentValidationTests,
    EarlyStableIdGapsCompileAndValidateLargeMeshesWithoutChangingOrdering)
{
    constexpr std::size_t kFaceCount = 4096U;
    const Document document = make_large_gapped_triangle_document(kFaceCount);
    ASSERT_EQ(document.faces.size(), kFaceCount);
    ASSERT_EQ(document.vertices.size(), kFaceCount * 3U);
    ASSERT_EQ(document.faces.front().id, 2U);
    ASSERT_EQ(document.vertices.front().id, 4U);
    ASSERT_EQ(
        document.next_face_id,
        static_cast<ElementId>(kFaceCount + 2U));
    ASSERT_EQ(
        document.next_vertex_id,
        static_cast<ElementId>((kFaceCount + 1U) * 3U + 1U));

    const PolygonDocumentValidationReport report =
        validate_polygon_document(document);
    EXPECT_TRUE(report.ok());
    EXPECT_TRUE(report.diagnostics.empty());

    const CompiledMesh mesh =
        compile_document(document, SurfaceShading::Smooth);
    ASSERT_EQ(mesh.vertices.size(), document.vertices.size());
    ASSERT_EQ(mesh.indices.size(), kFaceCount * 3U);
    ASSERT_EQ(mesh.primitives.size(), kFaceCount);

    std::vector<ElementId> expected_source_vertex_ids;
    expected_source_vertex_ids.reserve(document.vertices.size());
    for (const Face& face : document.faces) {
        expected_source_vertex_ids.insert(
            expected_source_vertex_ids.end(),
            face.vertices.begin(),
            face.vertices.end());
    }
    std::vector<ElementId> compiled_source_vertex_ids;
    compiled_source_vertex_ids.reserve(mesh.vertices.size());
    for (const CompiledVertex& vertex : mesh.vertices) {
        compiled_source_vertex_ids.push_back(vertex.source_vertex_id);
    }
    EXPECT_EQ(compiled_source_vertex_ids, expected_source_vertex_ids);
    EXPECT_TRUE(std::ranges::all_of(
        mesh.vertices,
        [](const CompiledVertex& vertex) {
            return vertex.normal.x == 0.0F &&
                   vertex.normal.y == 0.0F &&
                   vertex.normal.z == 1.0F;
        }));

    std::vector<std::uint32_t> expected_indices(mesh.indices.size());
    std::iota(expected_indices.begin(), expected_indices.end(), 0U);
    EXPECT_EQ(mesh.indices, expected_indices);
    bool primitives_preserve_face_order = true;
    for (std::size_t face_index = 0;
         face_index < document.faces.size();
         ++face_index) {
        const CompiledPrimitive& primitive = mesh.primitives[face_index];
        if (primitive.index_offset !=
                static_cast<std::uint32_t>(face_index * 3U) ||
            primitive.index_count != 3U ||
            primitive.material_slot !=
                document.faces[face_index].material_slot) {
            primitives_preserve_face_order = false;
            break;
        }
    }
    EXPECT_TRUE(primitives_preserve_face_order);
}

TEST(
    QdrDocumentValidationTests,
    EarlyStableIdGapsCompileGeneratedUvQuadsWithLinearLookupReuse)
{
    constexpr std::size_t kFaceCount = 4096U;
    const Document document = make_large_gapped_quad_document(kFaceCount);
    ASSERT_EQ(document.faces.size(), kFaceCount);
    ASSERT_EQ(document.vertices.size(), kFaceCount * 4U);
    ASSERT_EQ(document.faces.front().id, 2U);
    ASSERT_EQ(document.vertices.front().id, 5U);
    ASSERT_TRUE(std::ranges::all_of(
        document.faces,
        [](const Face& face) {
            return face.uvs.empty();
        }));

    const PolygonDocumentValidationReport report =
        validate_polygon_document(document);
    EXPECT_TRUE(report.ok());
    EXPECT_TRUE(report.diagnostics.empty());

    const CompiledMesh mesh =
        compile_document(document, SurfaceShading::Smooth);
    ASSERT_EQ(mesh.vertices.size(), kFaceCount * 4U);
    ASSERT_EQ(mesh.indices.size(), kFaceCount * 6U);
    ASSERT_EQ(mesh.primitives.size(), kFaceCount);

    bool output_preserves_face_order = true;
    for (std::size_t face_index = 0;
         face_index < document.faces.size();
         ++face_index) {
        const Face& face = document.faces[face_index];
        const std::size_t vertex_offset = face_index * 4U;
        const std::size_t index_offset = face_index * 6U;
        for (std::size_t corner = 0; corner < 4U; ++corner) {
            const CompiledVertex& vertex =
                mesh.vertices[vertex_offset + corner];
            if (vertex.source_vertex_id != face.vertices[corner] ||
                vertex.normal.x != 0.0F ||
                vertex.normal.y != 0.0F ||
                vertex.normal.z != 1.0F) {
                output_preserves_face_order = false;
                break;
            }
        }
        if (!output_preserves_face_order) {
            break;
        }

        const std::array<std::uint32_t, 6> expected_indices {
            static_cast<std::uint32_t>(vertex_offset),
            static_cast<std::uint32_t>(vertex_offset + 1U),
            static_cast<std::uint32_t>(vertex_offset + 2U),
            static_cast<std::uint32_t>(vertex_offset),
            static_cast<std::uint32_t>(vertex_offset + 2U),
            static_cast<std::uint32_t>(vertex_offset + 3U),
        };
        if (!std::equal(
                expected_indices.begin(),
                expected_indices.end(),
                mesh.indices.begin() +
                    static_cast<std::ptrdiff_t>(index_offset))) {
            output_preserves_face_order = false;
            break;
        }

        const CompiledPrimitive& primitive = mesh.primitives[face_index];
        if (primitive.index_offset !=
                static_cast<std::uint32_t>(index_offset) ||
            primitive.index_count != 6U ||
            primitive.material_slot != face.material_slot) {
            output_preserves_face_order = false;
            break;
        }
    }
    EXPECT_TRUE(output_preserves_face_order);

    ASSERT_GE(mesh.vertices.size(), 4U);
    EXPECT_NE(mesh.vertices[0].uv0.x, mesh.vertices[1].uv0.x);
    EXPECT_NE(mesh.vertices[1].uv0.y, mesh.vertices[2].uv0.y);
}
