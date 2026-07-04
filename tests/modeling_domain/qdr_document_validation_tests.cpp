////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <mesh/polygon/document.hpp>
#include <mesh/polygon/document_validation.hpp>

#include <algorithm>
#include <array>
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

Document make_triangle_document()
{
    Document document;
    const ElementId a = add_vertex(document, { 0.0F, 0.0F, 0.0F });
    const ElementId b = add_vertex(document, { 1.0F, 0.0F, 0.0F });
    const ElementId c = add_vertex(document, { 0.0F, 1.0F, 0.0F });
    require_test(add_face(document, std::array<ElementId, 3> { a, b, c }) != kInvalidElementId, "triangle face should be created");
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
    require_test(add_face(document, std::array<ElementId, 4> { a, b, c, d }) != kInvalidElementId, "collinear quad should be authored");

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
    require_test(add_face(document, std::array<ElementId, 3> { a, b, c }) != kInvalidElementId, "first face should be created");
    require_test(add_face(document, std::array<ElementId, 3> { b, a, d }) != kInvalidElementId, "second face should be created");
    require_test(add_face(document, std::array<ElementId, 3> { a, b, e }) != kInvalidElementId, "third face should be created");

    const PolygonDocumentValidationReport report = validate_polygon_document(document);

    EXPECT_FALSE(report.ok());
    const PolygonDocumentDiagnostic* diagnostic = find_code(report, PolygonDocumentDiagnosticCode::NonmanifoldEdge);
    ASSERT_TRUE(diagnostic != nullptr);
    EXPECT_TRUE(diagnostic->edge == make_edge(a, b));
    EXPECT_TRUE((diagnostic->vertex_ids == std::vector<ElementId> { a, b }));
}

TEST(QdrDocumentValidationTests, ReportsInvalidHardNormalEdges)
{
    Document document = make_triangle_document();
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

TEST(QdrDocumentValidationTests, ReportsUnreferencedVerticesAsWarnings)
{
    Document document = make_triangle_document();
    const ElementId unused = add_vertex(document, { 2.0F, 2.0F, 2.0F });

    const PolygonDocumentValidationReport report = validate_polygon_document(document);

    EXPECT_TRUE(report.ok());
    EXPECT_FALSE(report.has_errors());
    const PolygonDocumentDiagnostic* diagnostic = find_code(report, PolygonDocumentDiagnosticCode::UnreferencedVertex);
    ASSERT_TRUE(diagnostic != nullptr);
    EXPECT_TRUE(diagnostic->severity == PolygonDocumentDiagnosticSeverity::Warning);
    EXPECT_TRUE(diagnostic->vertex_id == unused);
}
