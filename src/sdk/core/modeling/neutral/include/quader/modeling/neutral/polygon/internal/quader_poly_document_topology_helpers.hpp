////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

// Source-only neutral modeling core contract shared by SDK and native owners.

#include <quader/modeling/neutral/geometry/geometry.hpp>
#include <quader/modeling/neutral/polygon/document.hpp>

#include "quader_poly_document_constants.hpp"

#include <array>
#include <map>
#include <set>
#include <span>
#include <utility>
#include <vector>

namespace quader_poly::document_internal {

quader_geometry::QVec2d geometry_vec2(const std::array<double, 2>& value);
std::array<double, 2> poly_vec2(quader_geometry::QVec2d value);
std::vector<quader_geometry::QVec2d> geometry_vec2_points(std::span<const std::array<double, 2>> points);
quader_geometry::QAxis geometry_axis_from_dropped_axis(int dropped_axis);
quader_geometry::QVec3f geometry_vec3(const quader::QVec3& value);
quader::QVec3 poly_vec3(quader_geometry::QVec3f value);
quader_geometry::QRay3<float> geometry_ray(const Ray& ray);
bool contains_id(std::span<const ElementId> ids, ElementId id);
bool contains_edge(std::span<const Edge> edges, Edge edge);
SelectionMode selection_mode_for_kind(ElementKind kind);
bool remove_id(std::vector<ElementId>& ids, ElementId id);
void toggle_id(std::vector<ElementId>& ids, ElementId id);
void toggle_edge(std::vector<Edge>& edges, Edge edge);
bool remove_edge(std::vector<Edge>& edges, Edge edge);
void add_unique_id(std::vector<ElementId>& ids, ElementId id);
void add_unique_edge(std::vector<Edge>& edges, Edge edge);
void clear_active_selection(Selection& selection);
void activate_vertex_selection(Selection& selection, ElementId vertex_id);
void activate_edge_selection(Selection& selection, Edge edge);
void activate_face_selection(Selection& selection, ElementId face_id);
void activate_pick_selection(Selection& selection, const PickResult& pick);
void activate_last_selection(Selection& selection);
/// Restores live component and active identities after a vertex weld.
void restore_remapped_selection(
    const Document &document, const Selection &source, Selection &selection,
    const std::map<ElementId, ElementId> &merged_vertex_ids);
ElementId active_face_or_invalid(const Selection& selection);
void activate_face_or_last_selection(Selection& selection, ElementId preferred_face_id);
bool face_uses_vertex(const Face& face, ElementId vertex_id);
bool face_uses_any_vertex(const Face& face, std::span<const ElementId> vertex_ids);
bool face_uses_any_vertex(const Face& face, const std::set<ElementId>& vertex_ids);
bool face_uses_edge(const Face& face, Edge edge);
bool edge_exists(const Document& document, Edge edge);
std::vector<Edge> face_edges(const Face& face);
bool face_uses_any_edge(const Face& face, std::span<const Edge> edges);
std::array<double, 2> projected_point_for_axis(const quader::QVec3& position, int dropped_axis);
double signed_projected_area(std::span<const std::array<double, 2>> points);
bool has_repeated_vertex(std::span<const ElementId> vertex_ids);
std::map<std::pair<ElementId, ElementId>, int> edge_incidence_counts(const Document& document);
std::map<std::pair<ElementId, ElementId>, std::vector<std::size_t>> face_indices_by_edge(const Document& document);
bool document_is_closed_manifold(const Document& document);
bool document_has_nonmanifold_edges(const Document& document);
FacePerimeterInfo perimeter_info_for_edges(const Document& document, std::span<const Edge> perimeter_edges);
bool document_has_unreferenced_vertices(const Document& document);
std::vector<ElementId> compact_face_vertices_for_merge(const Face& face, const std::set<ElementId>& merge_vertex_ids, ElementId active_vertex_id);
void restore_source_face_orientation(const Document& source, Document& candidate);
ElementId next_valid_face_id(Document& document);
/**
 * Assigns one exact authored edge-normal mode during a batch update.
 * Assignments are evaluated in order so the last live assignment for an edge
 * determines its final Hard, Soft, or Default state.
 */
struct EdgeNormalModeAssignment {
    Edge edge;
    EdgeNormalMode mode = EdgeNormalMode::Default;
};
/**
 * Captures an immutable tri-state lookup for repeated edge-normal queries.
 * Conflicting Hard and Soft source entries resolve to Default, matching the
 * canonical document conflict policy without rescanning both vectors.
 */
class EdgeNormalModeLookup {
public:
    explicit EdgeNormalModeLookup(const Document& document);
    [[nodiscard]] EdgeNormalMode mode(Edge edge) const;

private:
    std::map<std::pair<ElementId, ElementId>, EdgeNormalMode> modes_;
};
[[nodiscard]] EdgeNormalMode edge_normal_mode(const Document& document,
                                              Edge edge);
[[nodiscard]] bool set_edge_normal_modes(
    Document& document,
    std::span<const EdgeNormalModeAssignment> assignments);
[[nodiscard]] bool set_edge_normal_mode(Document& document, Edge edge,
                                        EdgeNormalMode mode);
/**
 * Describes one exact edge-normal transfer from a source edge to a target.
 * Batch copies resolve every source row from one immutable lookup before any
 * target state changes, including when source and target are the same document.
 */
struct EdgeNormalModeCopy {
    Edge source_edge;
    Edge target_edge;
};
[[nodiscard]] bool copy_edge_normal_modes(
    const Document& source,
    Document& target,
    std::span<const EdgeNormalModeCopy> copies);
[[nodiscard]] bool copy_edge_normal_mode(const Document& source,
                                         Edge source_edge,
                                         Document& target,
                                         Edge target_edge);
void prune_normal_edge_states(Document& document);
std::vector<Edge> remap_normal_edges_after_edge_splits(
    std::span<const Edge> source_edges,
    const Document& candidate,
    const std::map<std::pair<ElementId, ElementId>, ElementId>& split_vertices);
std::vector<Edge> remap_normal_edges_after_edge_splits(
    std::span<const Edge> source_edges,
    const Document& candidate,
    const std::map<std::pair<ElementId, ElementId>, std::vector<ElementId>>&
        split_vertex_ids_by_edge);
void remap_normal_edge_states_after_edge_splits(
    const Document& source,
    Document& candidate,
    const std::map<std::pair<ElementId, ElementId>, ElementId>& split_vertices);
void remap_normal_edge_states_after_edge_splits(
    const Document& source,
    Document& candidate,
    const std::map<std::pair<ElementId, ElementId>, std::vector<ElementId>>&
        split_vertex_ids_by_edge);
} // namespace quader_poly::document_internal
