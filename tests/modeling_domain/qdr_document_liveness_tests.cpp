////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <mesh/polygon/document.hpp>

#include <array>

namespace {

using namespace quader_poly;

Document make_quad_document()
{
    Document document;
    const ElementId a = add_vertex(document, { 0.0F, 0.0F, 0.0F });
    const ElementId b = add_vertex(document, { 1.0F, 0.0F, 0.0F });
    const ElementId c = add_vertex(document, { 1.0F, 1.0F, 0.0F });
    const ElementId d = add_vertex(document, { 0.0F, 1.0F, 0.0F });
    static_cast<void>(add_face(document, std::array<ElementId, 4> { a, b, c, d }));
    return document;
}

bool has_issue(
    const PolygonSelectionLivenessReport& report,
    PolygonSelectionLivenessIssueCode code,
    ElementId element_id = kInvalidElementId,
    Edge edge = {})
{
    for (const PolygonSelectionLivenessIssue& issue : report.issues) {
        if (issue.code != code) {
            continue;
        }
        if (element_id != kInvalidElementId && issue.element_id != element_id) {
            continue;
        }
        if (edge != Edge {} && issue.edge != make_edge(edge.a, edge.b)) {
            continue;
        }
        return true;
    }
    return false;
}

void liveness_snapshot_reports_live_vertices_faces_edges_and_revision()
{
    Document document = make_quad_document();
    const PolygonDocumentLiveness liveness = build_polygon_document_liveness(document);
    require_test(liveness.revision != 0U, "liveness snapshot should expose a non-zero document revision");
    require_test(liveness.contains_vertex(1), "liveness snapshot should include live vertices");
    require_test(liveness.contains_face(1), "liveness snapshot should include live faces");
    require_test(liveness.contains_edge(make_edge(1, 2)), "liveness snapshot should include face boundary edges");
    require_test(!liveness.contains_edge(make_edge(1, 3)), "liveness snapshot should reject non-boundary edges");

    document.faces.clear();
    const PolygonDocumentLiveness after_face_delete = build_polygon_document_liveness(document);
    require_test(after_face_delete.revision != liveness.revision, "topology deletion should change liveness revision");
    require_test(!after_face_delete.contains_face(1), "deleted faces should stop being live");
    require_test(!after_face_delete.contains_edge(make_edge(1, 2)), "deleting the face should remove boundary edge liveness");
}

void valid_selection_liveness_report_stays_empty()
{
    const Document document = make_quad_document();
    Selection selection;
    selection.mode = SelectionMode::Edge;
    selection.vertices = { 1 };
    selection.edges = { make_edge(1, 2) };
    selection.faces = { 1 };
    selection.has_active = true;
    selection.active_kind = ElementKind::Edge;
    selection.active_edge = make_edge(1, 2);

    const PolygonSelectionLivenessReport report = validate_polygon_selection_liveness(document, selection);
    require_test(report.ok(), "valid selected and active element IDs should pass liveness validation");
}

void selection_liveness_detects_deleted_selected_ids()
{
    Document document = make_quad_document();
    Selection stale_selection;
    stale_selection.mode = SelectionMode::Face;
    stale_selection.vertices = { 4 };
    stale_selection.edges = { make_edge(1, 2) };
    stale_selection.faces = { 1 };
    stale_selection.has_active = true;
    stale_selection.active_kind = ElementKind::Face;
    stale_selection.active_face = 1;

    document.vertices.erase(document.vertices.begin() + 3);
    document.faces.clear();

    const PolygonSelectionLivenessReport report =
        validate_polygon_selection_liveness(document, stale_selection);
    require_test(!report.ok(), "deleted selected IDs should fail liveness validation");
    require_test(
        has_issue(report, PolygonSelectionLivenessIssueCode::MissingSelectedVertex, 4),
        "deleted selected vertex should be diagnosed");
    require_test(
        has_issue(report, PolygonSelectionLivenessIssueCode::MissingSelectedEdge, kInvalidElementId, make_edge(1, 2)),
        "selected edge removed from topology should be diagnosed");
    require_test(
        has_issue(report, PolygonSelectionLivenessIssueCode::MissingSelectedFace, 1),
        "deleted selected face should be diagnosed");
    require_test(
        has_issue(report, PolygonSelectionLivenessIssueCode::MissingActiveFace, 1),
        "deleted active face should be diagnosed");
}

void selection_liveness_detects_missing_edge_endpoints_and_active_ids()
{
    const Document document = make_quad_document();
    Selection stale_selection;
    stale_selection.mode = SelectionMode::Edge;
    stale_selection.edges = { make_edge(2, 999), make_edge(1, 3) };
    stale_selection.faces = { 999 };
    stale_selection.has_active = true;
    stale_selection.active_kind = ElementKind::Edge;
    stale_selection.active_edge = make_edge(3, 999);

    const PolygonSelectionLivenessReport report =
        validate_polygon_selection_liveness(build_polygon_document_liveness(document), stale_selection);
    require_test(!report.ok(), "stale edges and active IDs should fail liveness validation");
    require_test(
        has_issue(report, PolygonSelectionLivenessIssueCode::MissingSelectedEdgeEndpoint, 999, make_edge(2, 999)),
        "selected edge with a missing endpoint should be diagnosed");
    require_test(
        has_issue(report, PolygonSelectionLivenessIssueCode::MissingSelectedEdge, kInvalidElementId, make_edge(1, 3)),
        "selected edge with live endpoints but no boundary edge should be diagnosed");
    require_test(
        has_issue(report, PolygonSelectionLivenessIssueCode::MissingSelectedFace, 999),
        "missing selected face should be diagnosed");
    require_test(
        has_issue(report, PolygonSelectionLivenessIssueCode::MissingActiveEdgeEndpoint, 999, make_edge(3, 999)),
        "active edge with a missing endpoint should be diagnosed");
}

} // namespace

TEST(QdrDocumentLivenessTests, QdrDocumentLivenessTests)
{
    liveness_snapshot_reports_live_vertices_faces_edges_and_revision();
    valid_selection_liveness_report_stays_empty();
    selection_liveness_detects_deleted_selected_ids();
    selection_liveness_detects_missing_edge_endpoints_and_active_ids();
}
