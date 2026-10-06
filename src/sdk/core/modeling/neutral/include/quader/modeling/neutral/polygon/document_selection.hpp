////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

// Source-only neutral modeling core contract shared by SDK and native owners.

#include <quader/modeling/neutral/polygon/document_model.hpp>
#include <quader/modeling/neutral/geometry/geometry.hpp>

#include <cstdint>
#include <numbers>
#include <optional>
#include <span>
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
 * Enumerates selection-only component commands owned by the portable polygon
 * document.
 */
enum class ComponentSelectionCommand {
  SelectAll,
  SelectInverse,
  SelectDirection,
  SelectLoops,
  SelectEdgeRing,
  SelectFacesWithSameMaterial,
  SelectCoplanarFaces,
  SelectFaceNormal,
  SelectNGons,
  Grow,
  Shrink,
};

inline constexpr float kDefaultSelectionDirectionToleranceRadians =
    0.01F * std::numbers::pi_v<float>;

/**
 * Stores a comparison-space plane and projected float-authoring roundoff.
 * Opposite normal orientation represents the same plane. Distance roundoff
 * uses the same scale as the normal and offset.
 */
struct SelectionComparisonPlane {
  quader_geometry::QVec3d point;
  quader_geometry::QVec3d normal;
  double offset = 0.0;
  double distance_roundoff = 0.0;
  double angular_roundoff = 0.0;
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
 * Defines the component universe that a bulk selection command may affect.
 * Components outside this set keep their current selection state, matching
 * hidden/pickability-aware editor behavior.
 */
struct ComponentSelectionUniverse {
  SelectionMode mode = SelectionMode::Vertex;
  std::vector<ElementId> vertices;
  std::vector<Edge> edges;
  std::vector<ElementId> faces;
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
/**
 * Selects the picked loose part through shared authored vertex identities.
 * Only the picked component kind is returned, with the seed active. Coincident
 * positions do not connect parts; a missing or invalid seed returns no selection.
 */
[[nodiscard]] Selection connected_component_selection(
    const Document& document,
    const PickResult& pick);
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
[[nodiscard]] std::vector<quader::QVec3>
selected_edge_directions_in_comparison_space(
    const Document& document,
    const Selection& selection,
    const Transform3& comparison_from_document);
[[nodiscard]] std::vector<SelectionComparisonPlane>
selected_face_planes_in_comparison_space(
    const Document& document,
    const Selection& selection,
    const Transform3& comparison_from_document);
[[nodiscard]] std::vector<quader::QVec3>
selected_face_normals_in_comparison_space(
    const Document& document,
    const Selection& selection,
    const Transform3& comparison_from_document);
[[nodiscard]] bool add_edges_matching_directions(
    const Document& document,
    Selection& selection,
    const Transform3& comparison_from_document,
    std::span<const quader::QVec3> comparison_directions,
    float angle_tolerance_radians =
        kDefaultSelectionDirectionToleranceRadians);
[[nodiscard]] bool add_selected_edge_loops(
    const Document& document,
    Selection& selection);
[[nodiscard]] bool add_selected_edge_rings(
    const Document& document,
    Selection& selection);
[[nodiscard]] bool add_faces_with_selected_material_slots(
    const Document& document,
    Selection& selection);
[[nodiscard]] bool add_faces_with_material_slots(
    const Document& document,
    Selection& selection,
    std::span<const std::uint32_t> material_slots);
[[nodiscard]] bool add_faces_coplanar_with_planes(
    const Document& document,
    Selection& selection,
    const Transform3& comparison_from_document,
    std::span<const SelectionComparisonPlane> comparison_planes);
[[nodiscard]] bool add_faces_matching_normals(
    const Document& document,
    Selection& selection,
    const Transform3& comparison_from_document,
    std::span<const quader::QVec3> comparison_normals);
[[nodiscard]] bool replace_selection_with_ngons(
    const Document& document,
    Selection& selection);
[[nodiscard]] bool grow_selection(
    const Document& document,
    Selection& selection);
[[nodiscard]] bool shrink_selection(
    const Document& document,
    Selection& selection);
[[nodiscard]] ComponentSelectionUniverse all_component_selection_universe(
    const Document& document,
    SelectionMode mode);
[[nodiscard]] bool invert_component_selection(
    const Document& document,
    Selection& selection);
[[nodiscard]] bool invert_component_selection(
    const Document& document,
    Selection& selection,
    const ComponentSelectionUniverse& universe);
[[nodiscard]] bool apply_component_selection_command(
    const Document& document,
    Selection& selection,
    ComponentSelectionCommand command,
    const Transform3& comparison_from_document = {},
    float direction_angle_tolerance_radians =
        kDefaultSelectionDirectionToleranceRadians);
[[nodiscard]] std::optional<quader::QVec3> selection_center(const Document& document, const Selection& selection);

} // namespace quader_poly
