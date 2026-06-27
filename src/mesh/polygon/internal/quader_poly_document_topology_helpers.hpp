////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <mesh/geometry/geometry.hpp>
#include <mesh/polygon/document.hpp>

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
ElementId active_face_or_invalid(const Selection& selection);
void activate_face_or_last_selection(Selection& selection, ElementId preferred_face_id);
bool face_uses_vertex(const Face& face, ElementId vertex_id);
bool face_uses_any_vertex(const Face& face, std::span<const ElementId> vertex_ids);
bool face_uses_any_vertex(const Face& face, const std::set<ElementId>& vertex_ids);
bool face_uses_edge(const Face& face, Edge edge);
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
} // namespace quader_poly::document_internal
