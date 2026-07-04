////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <mesh/polygon/document_model.hpp>

#include <cstdint>
#include <optional>
#include <vector>

namespace quader_poly {

struct PickResult;

/**
 * Enumerates ElementKind values used by the modeling layer.
 */
enum class ElementKind {
  Vertex,
  Edge,
  Face,
};

/**
 * Enumerates SelectionMode values used by the modeling layer.
 */
enum class SelectionMode {
  Vertex,
  Edge,
  Face,
};

/**
 * Represents a Selection value used by the polygon document and mesh editing core.
 */
struct Selection {
  SelectionMode mode = SelectionMode::Vertex;
  std::vector<ElementId> vertices;
  std::vector<Edge> edges;
  std::vector<ElementId> faces;
  bool has_active = false;
  ElementKind active_kind = ElementKind::Vertex;
  ElementId active_vertex = kInvalidElementId;
  Edge active_edge;
  ElementId active_face = kInvalidElementId;

  [[nodiscard]] bool empty() const;
  void clear();
};

/**
 * Enumerates stale polygon selection liveness issues detected from a document snapshot.
 */
enum class PolygonSelectionLivenessIssueCode {
  MissingSelectedVertex,
  MissingSelectedEdgeEndpoint,
  MissingSelectedEdge,
  MissingSelectedFace,
  MissingActiveVertex,
  MissingActiveEdgeEndpoint,
  MissingActiveEdge,
  MissingActiveFace,
};

/**
 * Stores one stale selected or active polygon element reference.
 */
struct PolygonSelectionLivenessIssue {
    PolygonSelectionLivenessIssueCode code = PolygonSelectionLivenessIssueCode::MissingSelectedVertex;
    ElementKind kind = ElementKind::Vertex;
    ElementId element_id = kInvalidElementId;
    Edge edge;
};

/**
 * Stores all liveness issues found for one polygon selection.
 */
struct PolygonSelectionLivenessReport {
    std::vector<PolygonSelectionLivenessIssue> issues;

    [[nodiscard]] bool ok() const;
};

/**
 * Captures live polygon element IDs and a deterministic topology revision.
 */
struct PolygonDocumentLiveness {
    std::uint64_t revision = 0;
    std::vector<ElementId> live_vertices;
    std::vector<ElementId> live_faces;
    std::vector<Edge> live_edges;

    [[nodiscard]] bool contains_vertex(ElementId id) const;
    [[nodiscard]] bool contains_face(ElementId id) const;
    [[nodiscard]] bool contains_edge(Edge edge) const;
};

void select_only(Selection& selection, const PickResult& pick);
void add_selection(Selection& selection, const PickResult& pick);
void remove_selection(Selection& selection, const PickResult& pick);
void set_selection_mode(Selection& selection, SelectionMode mode);
void convert_selection_mode(const Document& document, Selection& selection, SelectionMode mode);
[[nodiscard]] bool selection_contains(const Selection& selection, ElementId vertex_id);
[[nodiscard]] bool selection_contains(const Selection& selection, Edge edge);
[[nodiscard]] bool selection_contains_face(const Selection& selection, ElementId face_id);
[[nodiscard]] std::vector<ElementId> selected_vertex_ids(const Document& document, const Selection& selection);
[[nodiscard]] PolygonDocumentLiveness build_polygon_document_liveness(const Document& document);
[[nodiscard]] PolygonSelectionLivenessReport validate_polygon_selection_liveness(
    const Document& document,
    const Selection& selection);
[[nodiscard]] PolygonSelectionLivenessReport validate_polygon_selection_liveness(
    const PolygonDocumentLiveness& liveness,
    const Selection& selection);
void select_edge_loop(const Document& document, Selection& selection, Edge seed_edge, bool toggle = false);
[[nodiscard]] std::optional<quader::QVec3> selection_center(const Document& document, const Selection& selection);

} // namespace quader_poly
