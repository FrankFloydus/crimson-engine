////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <quader/modeling/neutral/polygon/document.hpp>

#include <quader/modeling/neutral/polygon/internal/quader_poly_document_knife_helpers.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_topology_helpers.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_uv_helpers.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <utility>
#include <vector>

namespace quader_poly {

using namespace document_internal;

namespace {

int edge_incidence_count(
    const std::map<std::pair<ElementId, ElementId>, int>& incidence_counts,
    Edge edge)
{
    const Edge normalized = make_edge(edge.a, edge.b);
    const auto found = incidence_counts.find({ normalized.a, normalized.b });
    return found == incidence_counts.end() ? 0 : found->second;
}

const Face* face_at_index(const Document& document, std::size_t index)
{
    return index < document.faces.size() ? &document.faces[index] : nullptr;
}

const quader::QVec3* first_face_vertex_position(const Document& document,
                                                const Face& face)
{
    if (face.vertices.empty()) {
        return nullptr;
    }
    const Vertex* vertex = find_vertex(document, face.vertices.front());
    return vertex == nullptr ? nullptr : &vertex->position;
}

std::vector<const Face*> edge_faces(
    const Document& document,
    const std::map<std::pair<ElementId, ElementId>, std::vector<std::size_t>>&
        faces_by_edge,
    Edge edge)
{
    const Edge normalized = make_edge(edge.a, edge.b);
    const auto found = faces_by_edge.find({ normalized.a, normalized.b });
    if (found == faces_by_edge.end()) {
        return {};
    }

    std::vector<const Face*> faces;
    faces.reserve(found->second.size());
    for (const std::size_t face_index : found->second) {
        const Face* face = face_at_index(document, face_index);
        if (face != nullptr) {
            faces.push_back(face);
        }
    }
    return faces;
}

bool face_is_coplanar_with_context(const Document& document,
                                   const Face& context_face,
                                   quader::QVec3 context_normal,
                                   const Face& candidate)
{
    const quader::QVec3* context_point =
        first_face_vertex_position(document, context_face);
    if (context_point == nullptr) {
        return false;
    }

    const quader::QVec3 candidate_normal = face_normal(document, candidate);
    if (std::abs(quader::dot(context_normal, candidate_normal)) < 0.999F) {
        return false;
    }

    for (const ElementId vertex_id : candidate.vertices) {
        const Vertex* vertex = find_vertex(document, vertex_id);
        if (vertex == nullptr) {
            return false;
        }
        if (std::abs(quader::dot(vertex->position - *context_point,
                                 context_normal)) > 0.001F) {
            return false;
        }
    }

    return true;
}

std::optional<Edge> adjacent_face_edge_at_vertex(const Face& face,
                                                 Edge edge,
                                                 ElementId vertex)
{
    if (face.vertices.size() < 3U) {
        return std::nullopt;
    }

    const Edge normalized = make_edge(edge.a, edge.b);
    for (std::size_t index = 0; index < face.vertices.size(); ++index) {
        if (face.vertices[index] != vertex) {
            continue;
        }

        const ElementId previous =
            face.vertices[(index + face.vertices.size() - 1U) %
                          face.vertices.size()];
        const ElementId next =
            face.vertices[(index + 1U) % face.vertices.size()];
        const Edge previous_edge = make_edge(previous, vertex);
        const Edge next_edge = make_edge(vertex, next);
        if (previous_edge == normalized && next_edge != normalized) {
            return next_edge;
        }
        if (next_edge == normalized && previous_edge != normalized) {
            return previous_edge;
        }
        return std::nullopt;
    }

    return std::nullopt;
}

const Face* opposite_face_across_edge(
    const Document& document,
    const std::map<std::pair<ElementId, ElementId>, std::vector<std::size_t>>&
        faces_by_edge,
    Edge edge,
    ElementId source_face_id)
{
    for (const Face* face : edge_faces(document, faces_by_edge, edge)) {
        if (face != nullptr && face->id != source_face_id) {
            return face;
        }
    }
    return nullptr;
}

std::set<ElementId> connected_coplanar_face_region(
    const Document& document,
    const Face& context_face,
    const std::map<std::pair<ElementId, ElementId>, std::vector<std::size_t>>&
        faces_by_edge)
{
    std::set<ElementId> region;
    std::vector<ElementId> pending;
    const quader::QVec3 context_normal = face_normal(document, context_face);
    region.insert(context_face.id);
    pending.push_back(context_face.id);

    while (!pending.empty()) {
        const ElementId face_id = pending.back();
        pending.pop_back();
        const Face* face = find_face(document, face_id);
        if (face == nullptr) {
            continue;
        }

        for (const Edge& edge : face_edges(*face)) {
            for (const Face* candidate :
                 edge_faces(document, faces_by_edge, edge)) {
                if (candidate == nullptr ||
                    region.contains(candidate->id) ||
                    !face_is_coplanar_with_context(
                        document, context_face, context_normal, *candidate)) {
                    continue;
                }
                region.insert(candidate->id);
                pending.push_back(candidate->id);
            }
        }
    }

    return region;
}

std::vector<Edge> boundary_edges_for_face_region(
    const Document& document,
    const std::set<ElementId>& region_face_ids)
{
    std::map<std::pair<ElementId, ElementId>, int> region_edge_counts;
    for (const Face& face : document.faces) {
        if (!region_face_ids.contains(face.id)) {
            continue;
        }
        for (const Edge& edge : face_edges(face)) {
            const Edge normalized = make_edge(edge.a, edge.b);
            ++region_edge_counts[{ normalized.a, normalized.b }];
        }
    }

    std::vector<Edge> boundary_edges;
    for (const auto& [key, count] : region_edge_counts) {
        if (count == 1) {
            boundary_edges.push_back({ key.first, key.second });
        }
    }
    return boundary_edges;
}

std::vector<Edge> connected_edge_component(std::span<const Edge> edges,
                                           Edge seed)
{
    seed = make_edge(seed.a, seed.b);
    if (!contains_edge(edges, seed)) {
        return {};
    }

    std::vector<Edge> component;
    std::vector<Edge> pending { seed };
    while (!pending.empty()) {
        const Edge current = pending.back();
        pending.pop_back();
        if (contains_edge(component, current)) {
            continue;
        }
        component.push_back(current);

        for (const Edge& candidate : edges) {
            if (contains_edge(component, candidate) ||
                contains_edge(pending, candidate)) {
                continue;
            }
            if (candidate.a == current.a || candidate.a == current.b ||
                candidate.b == current.a || candidate.b == current.b) {
                pending.push_back(candidate);
            }
        }
    }

    return component;
}

std::vector<Edge> face_context_loop_edges(const Document& document,
                                          Edge seed,
                                          ElementId face_id)
{
    const Face* context_face = find_face(document, face_id);
    if (context_face == nullptr || !face_uses_edge(*context_face, seed)) {
        return {};
    }

    const std::map<std::pair<ElementId, ElementId>, std::vector<std::size_t>>
        faces_by_edge = face_indices_by_edge(document);
    const std::set<ElementId> region =
        connected_coplanar_face_region(document, *context_face, faces_by_edge);
    const std::vector<Edge> boundary_edges =
        boundary_edges_for_face_region(document, region);
    return connected_edge_component(boundary_edges, seed);
}

std::optional<Edge> next_boundary_loop_edge(
    const Document& document,
    const std::map<std::pair<ElementId, ElementId>, int>& incidence_counts,
    const std::map<std::pair<ElementId, ElementId>, std::vector<std::size_t>>&
        faces_by_edge,
    Edge current,
    ElementId through_vertex)
{
    current = make_edge(current.a, current.b);
    if (edge_incidence_count(incidence_counts, current) != 1 ||
        other_edge_vertex(current, through_vertex) == kInvalidElementId) {
        return std::nullopt;
    }

    std::vector<const Face*> current_faces =
        edge_faces(document, faces_by_edge, current);
    if (current_faces.size() != 1U || current_faces.front() == nullptr) {
        return std::nullopt;
    }

    const Face* face = current_faces.front();
    std::optional<Edge> fan_edge =
        adjacent_face_edge_at_vertex(*face, current, through_vertex);
    std::set<std::pair<ElementId, ElementId>> visited_edges;
    std::set<ElementId> visited_faces { face->id };
    const std::size_t max_steps =
        document_edges(document).size() + document.faces.size();
    for (std::size_t step = 0; step < max_steps && fan_edge.has_value();
         ++step) {
        Edge candidate = make_edge(fan_edge->a, fan_edge->b);
        if (other_edge_vertex(candidate, through_vertex) == kInvalidElementId) {
            return std::nullopt;
        }

        const int incidence =
            edge_incidence_count(incidence_counts, candidate);
        if (incidence == 1) {
            return candidate;
        }
        if (incidence != 2) {
            return std::nullopt;
        }

        const auto key = std::pair<ElementId, ElementId> {
            candidate.a, candidate.b
        };
        if (!visited_edges.insert(key).second) {
            return std::nullopt;
        }

        const Face* next_face = opposite_face_across_edge(
            document, faces_by_edge, candidate, face->id);
        if (next_face == nullptr ||
            !visited_faces.insert(next_face->id).second) {
            return std::nullopt;
        }

        fan_edge = adjacent_face_edge_at_vertex(*next_face, candidate,
                                                through_vertex);
        face = next_face;
    }

    return std::nullopt;
}

void append_boundary_loop_direction(
    const Document& document,
    const std::map<std::pair<ElementId, ElementId>, int>& incidence_counts,
    const std::map<std::pair<ElementId, ElementId>, std::vector<std::size_t>>&
        faces_by_edge,
    Edge seed,
    ElementId through_vertex,
    std::vector<Edge>& edges)
{
    Edge current = make_edge(seed.a, seed.b);
    ElementId vertex = through_vertex;
    const std::size_t max_steps = document_edges(document).size();
    for (std::size_t step = 0; step < max_steps; ++step) {
        const std::optional<Edge> next = next_boundary_loop_edge(
            document, incidence_counts, faces_by_edge, current, vertex);
        if (!next.has_value() || contains_edge(edges, *next)) {
            return;
        }

        edges.push_back(*next);
        const ElementId next_vertex = other_edge_vertex(*next, vertex);
        if (next_vertex == kInvalidElementId) {
            return;
        }

        current = *next;
        vertex = next_vertex;
    }
}

std::vector<Edge> boundary_loop_edges(const Document& document, Edge seed)
{
    const std::map<std::pair<ElementId, ElementId>, int> incidence_counts =
        edge_incidence_counts(document);
    if (edge_incidence_count(incidence_counts, seed) != 1) {
        return {};
    }

    const std::map<std::pair<ElementId, ElementId>, std::vector<std::size_t>>
        faces_by_edge = face_indices_by_edge(document);
    std::vector<Edge> edges;
    edges.push_back(seed);
    append_boundary_loop_direction(document, incidence_counts, faces_by_edge,
                                   seed, seed.a, edges);
    append_boundary_loop_direction(document, incidence_counts, faces_by_edge,
                                   seed, seed.b, edges);
    return edges;
}

quader::QVec3 transform_comparison_point(const Transform3& transform,
                                         quader::QVec3 point)
{
    return transform.origin + transform.x_axis * point.x +
           transform.y_axis * point.y + transform.z_axis * point.z;
}

std::optional<quader::QVec3> comparison_edge_direction(
    const Document& document,
    Edge edge,
    const Transform3& comparison_from_document)
{
    const Vertex* first = find_vertex(document, edge.a);
    const Vertex* second = find_vertex(document, edge.b);
    if (first == nullptr || second == nullptr) {
        return std::nullopt;
    }

    const quader::QVec3 direction =
        transform_comparison_point(comparison_from_document,
                                   second->position) -
        transform_comparison_point(comparison_from_document,
                                   first->position);
    const float direction_length_squared = quader::length_squared(direction);
    if (!std::isfinite(direction_length_squared) ||
        direction_length_squared <= 0.0F) {
        return std::nullopt;
    }
    return direction / std::sqrt(direction_length_squared);
}

std::optional<quader::QVec3> document_face_normal(
    const Document& document,
    const Face& face)
{
    if (face.vertices.size() < 3U) {
        return std::nullopt;
    }

    quader::QVec3 document_normal;
    for (std::size_t index = 0; index < face.vertices.size(); ++index) {
        const Vertex* current = find_vertex(document, face.vertices[index]);
        const Vertex* next = find_vertex(
            document, face.vertices[(index + 1U) % face.vertices.size()]);
        if (current == nullptr || next == nullptr) {
            return std::nullopt;
        }
        document_normal.x +=
            (current->position.y - next->position.y) *
            (current->position.z + next->position.z);
        document_normal.y +=
            (current->position.z - next->position.z) *
            (current->position.x + next->position.x);
        document_normal.z +=
            (current->position.x - next->position.x) *
            (current->position.y + next->position.y);
    }
    const float document_length_squared =
        quader::length_squared(document_normal);
    if (!std::isfinite(document_length_squared) ||
        document_length_squared <= 0.0F) {
        return std::nullopt;
    }

    return document_normal / std::sqrt(document_length_squared);
}

/// Stores one inverse-transpose normal before and after normalization.
struct ComparisonNormal {
    quader::QVec3 unnormalized;
    quader::QVec3 normalized;
    float length = 0.0F;
};

std::optional<ComparisonNormal> comparison_normal_from_document_normal(
    quader::QVec3 document_normal,
    const Transform3& comparison_from_document)
{
    const quader::QVec3 yz = quader::cross(
        comparison_from_document.y_axis,
        comparison_from_document.z_axis);
    const float determinant =
        quader::dot(comparison_from_document.x_axis, yz);
    if (!std::isfinite(determinant) || determinant == 0.0F) {
        return std::nullopt;
    }
    const quader::QVec3 comparison_normal =
        (yz * document_normal.x +
         quader::cross(comparison_from_document.z_axis,
                       comparison_from_document.x_axis) *
             document_normal.y +
         quader::cross(comparison_from_document.x_axis,
                       comparison_from_document.y_axis) *
             document_normal.z) /
        determinant;
    const float comparison_length_squared =
        quader::length_squared(comparison_normal);
    if (!std::isfinite(comparison_length_squared) ||
        comparison_length_squared <= 0.0F) {
        return std::nullopt;
    }
    const float comparison_length =
        std::sqrt(comparison_length_squared);
    return ComparisonNormal {
        .unnormalized = comparison_normal,
        .normalized = comparison_normal / comparison_length,
        .length = comparison_length,
    };
}

std::optional<quader::QVec3> comparison_face_normal(
    const Document& document,
    const Face& face,
    const Transform3& comparison_from_document)
{
    const std::optional<quader::QVec3> document_normal =
        document_face_normal(document, face);
    if (!document_normal.has_value()) {
        return std::nullopt;
    }
    const std::optional<ComparisonNormal> comparison_normal =
        comparison_normal_from_document_normal(
            *document_normal, comparison_from_document);
    return comparison_normal.has_value()
               ? std::optional<quader::QVec3>{
                     comparison_normal->normalized}
               : std::nullopt;
}

bool same_ids(std::span<const ElementId> left,
              std::span<const ElementId> right)
{
    if (left.size() != right.size()) {
        return false;
    }
    std::vector<ElementId> sorted_left(left.begin(), left.end());
    std::vector<ElementId> sorted_right(right.begin(), right.end());
    std::ranges::sort(sorted_left);
    std::ranges::sort(sorted_right);
    return sorted_left == sorted_right;
}

bool same_edges(std::span<const Edge> left, std::span<const Edge> right)
{
    if (left.size() != right.size()) {
        return false;
    }
    std::vector<Edge> sorted_left(left.begin(), left.end());
    std::vector<Edge> sorted_right(right.begin(), right.end());
    const auto normalize_and_sort = [](std::vector<Edge>& edges) {
        for (Edge& edge : edges) {
            edge = make_edge(edge.a, edge.b);
        }
        std::ranges::sort(edges, {}, [](Edge edge) {
            return std::pair { edge.a, edge.b };
        });
    };
    normalize_and_sort(sorted_left);
    normalize_and_sort(sorted_right);
    return sorted_left == sorted_right;
}

bool normals_match_same_facing(quader::QVec3 left,
                               quader::QVec3 right)
{
    const auto roundoff_zero = [](float left_first, float right_second,
                                  float left_second, float right_first) {
        const double first =
            static_cast<double>(left_first) * right_second;
        const double second =
            static_cast<double>(left_second) * right_first;
        const double value = first - second;
        constexpr double epsilon =
            std::numeric_limits<float>::epsilon();
        const double bound =
            4.0 * epsilon +
            2.0 * epsilon * (std::abs(first) + std::abs(second));
        return std::abs(value) <= bound;
    };
    const bool collinear =
        roundoff_zero(left.y, right.z, left.z, right.y) &&
        roundoff_zero(left.z, right.x, left.x, right.z) &&
        roundoff_zero(left.x, right.y, left.y, right.x);
    const double alignment =
        static_cast<double>(left.x) * right.x +
        static_cast<double>(left.y) * right.y +
        static_cast<double>(left.z) * right.z;
    return collinear && std::isfinite(alignment) && alignment > 0.0;
}

bool same_selection(const Selection& left, const Selection& right)
{
    return left.mode == right.mode &&
           same_ids(left.vertices, right.vertices) &&
           same_edges(left.edges, right.edges) &&
           same_ids(left.faces, right.faces) &&
           left.has_active == right.has_active &&
           left.active_kind == right.active_kind &&
           left.active_vertex == right.active_vertex &&
           make_edge(left.active_edge.a, left.active_edge.b) ==
               make_edge(right.active_edge.a, right.active_edge.b) &&
           left.active_face == right.active_face;
}

bool active_is_selected(const Selection& selection, const Selection& source)
{
    if (!source.has_active) {
        return false;
    }
    switch (selection.mode) {
    case SelectionMode::Vertex:
      return source.active_kind == ElementKind::Vertex &&
             contains_id(selection.vertices, source.active_vertex);
    case SelectionMode::Edge:
      return source.active_kind == ElementKind::Edge &&
             contains_edge(selection.edges, source.active_edge);
    case SelectionMode::Face:
      return source.active_kind == ElementKind::Face &&
             contains_id(selection.faces, source.active_face);
    }
    return false;
}

void restore_active_selection(Selection& selection, const Selection& source)
{
    if (!active_is_selected(selection, source)) {
        activate_last_selection(selection);
        return;
    }

    switch (selection.mode) {
    case SelectionMode::Vertex:
      activate_vertex_selection(selection, source.active_vertex);
      break;
    case SelectionMode::Edge:
      activate_edge_selection(selection, source.active_edge);
      break;
    case SelectionMode::Face:
      activate_face_selection(selection, source.active_face);
      break;
    }
}

bool finish_selection_change(Selection& selection, const Selection& before)
{
    restore_active_selection(selection, before);
    if (same_selection(selection, before)) {
        selection = before;
        return false;
    }
    return true;
}

std::optional<SelectionComparisonPlane> comparison_face_plane(
    const Document& document,
    const Face& face,
    const Transform3& comparison_from_document)
{
    if (face.vertices.size() < 3U) {
        return std::nullopt;
    }

    const std::optional<quader::QVec3> document_normal =
        document_face_normal(document, face);
    if (!document_normal.has_value()) {
        return std::nullopt;
    }
    const Vertex* first = find_vertex(document, face.vertices.front());
    if (first == nullptr) {
        return std::nullopt;
    }
    for (const ElementId vertex_id : face.vertices) {
        const Vertex* vertex = find_vertex(document, vertex_id);
        if (vertex == nullptr) {
            return std::nullopt;
        }
        const quader::QVec3 delta = vertex->position - first->position;
        const double distance =
            static_cast<double>(delta.x) * document_normal->x +
            static_cast<double>(delta.y) * document_normal->y +
            static_cast<double>(delta.z) * document_normal->z;
        const double magnitude =
            std::abs(static_cast<double>(delta.x) * document_normal->x) +
            std::abs(static_cast<double>(delta.y) * document_normal->y) +
            std::abs(static_cast<double>(delta.z) * document_normal->z);
        const double roundoff =
            4.0 * std::numeric_limits<double>::epsilon() * magnitude;
        if (!std::isfinite(distance) || std::abs(distance) > roundoff) {
            return std::nullopt;
        }
    }

    const std::optional<ComparisonNormal> comparison_normal =
        comparison_normal_from_document_normal(
            *document_normal, comparison_from_document);
    if (!comparison_normal.has_value()) {
        return std::nullopt;
    }
    const double document_offset =
        -(static_cast<double>(document_normal->x) * first->position.x +
          static_cast<double>(document_normal->y) * first->position.y +
          static_cast<double>(document_normal->z) * first->position.z);
    const quader::QVec3 origin = comparison_from_document.origin;
    const double comparison_offset =
        (document_offset -
         (static_cast<double>(comparison_normal->unnormalized.x) * origin.x +
          static_cast<double>(comparison_normal->unnormalized.y) * origin.y +
          static_cast<double>(comparison_normal->unnormalized.z) * origin.z)) /
        comparison_normal->length;
    return SelectionComparisonPlane {
        .point = transform_comparison_point(
            comparison_from_document, first->position),
        .normal = comparison_normal->normalized,
        .offset = comparison_offset,
    };
}

bool planes_are_exactly_coplanar(const SelectionComparisonPlane& left,
                                 const SelectionComparisonPlane& right)
{
    const float left_length_squared = quader::length_squared(left.normal);
    const float right_length_squared = quader::length_squared(right.normal);
    if (!std::isfinite(left_length_squared) ||
        !std::isfinite(right_length_squared) ||
        left_length_squared <= 0.0F || right_length_squared <= 0.0F) {
        return false;
    }
    const quader::QVec3 left_normal =
        left.normal / std::sqrt(left_length_squared);
    const quader::QVec3 right_normal =
        right.normal / std::sqrt(right_length_squared);
    const bool same_facing =
        normals_match_same_facing(left_normal, right_normal);
    const bool opposite_facing = normals_match_same_facing(
        left_normal, right_normal * -1.0F);
    if (!same_facing && !opposite_facing) {
        return false;
    }
    const double left_offset =
        left.offset / std::sqrt(static_cast<double>(left_length_squared));
    const double right_offset =
        (opposite_facing ? -right.offset : right.offset) /
        std::sqrt(static_cast<double>(right_length_squared));
    const double scale =
        std::max({1.0, std::abs(left_offset), std::abs(right_offset)});
    const double roundoff =
        32.0 * std::numeric_limits<double>::epsilon() * scale;
    return std::abs(left_offset - right_offset) <= roundoff;
}

std::vector<ElementId> selected_edge_vertex_ids(const Selection& selection)
{
    std::vector<ElementId> vertices;
    vertices.reserve(selection.edges.size() * 2U);
    for (const Edge& edge : selection.edges) {
        add_unique_id(vertices, edge.a);
        add_unique_id(vertices, edge.b);
    }
    return vertices;
}

bool face_all_vertices_selected(const Face& face,
                                const Selection& selection)
{
    return !face.vertices.empty() &&
           std::ranges::all_of(face.vertices, [&](ElementId vertex_id) {
               return contains_id(selection.vertices, vertex_id);
           });
}

bool face_all_edges_selected(const Face& face, const Selection& selection)
{
    const std::vector<Edge> edges = face_edges(face);
    return !edges.empty() && std::ranges::all_of(edges, [&](Edge edge) {
               return contains_edge(selection.edges, edge);
           });
}

bool face_shares_selected_vertex(const Face& face,
                                 std::span<const ElementId> selected_vertices)
{
    return std::ranges::any_of(face.vertices, [&](ElementId vertex_id) {
        return contains_id(selected_vertices, vertex_id);
    });
}

bool select_all_components(const Document& document, Selection& selection)
{
    const Selection before = selection;
    switch (selection.mode) {
    case SelectionMode::Vertex:
      selection.vertices.clear();
      selection.vertices.reserve(document.vertices.size());
      for (const Vertex& vertex : document.vertices) {
          add_unique_id(selection.vertices, vertex.id);
      }
      break;
    case SelectionMode::Edge:
      selection.edges = document_edges(document);
      break;
    case SelectionMode::Face:
      selection.faces.clear();
      selection.faces.reserve(document.faces.size());
      for (const Face& face : document.faces) {
          add_unique_id(selection.faces, face.id);
      }
      break;
    }
    return finish_selection_change(selection, before);
}

bool invert_component_selection(const Document& document,
                                Selection& selection)
{
    const Selection before = selection;
    switch (selection.mode) {
    case SelectionMode::Vertex: {
      std::vector<ElementId> inverse;
      inverse.reserve(document.vertices.size());
      for (const Vertex& vertex : document.vertices) {
          if (!contains_id(before.vertices, vertex.id)) {
              inverse.push_back(vertex.id);
          }
      }
      selection.vertices = std::move(inverse);
      break;
    }
    case SelectionMode::Edge: {
      std::vector<Edge> inverse;
      for (const Edge edge : document_edges(document)) {
          if (!contains_edge(before.edges, edge)) {
              inverse.push_back(edge);
          }
      }
      selection.edges = std::move(inverse);
      break;
    }
    case SelectionMode::Face: {
      std::vector<ElementId> inverse;
      inverse.reserve(document.faces.size());
      for (const Face& face : document.faces) {
          if (!contains_id(before.faces, face.id)) {
              inverse.push_back(face.id);
          }
      }
      selection.faces = std::move(inverse);
      break;
    }
    }
    return finish_selection_change(selection, before);
}

} // namespace

void select_only(Selection& selection, const PickResult& pick)
{
    selection.clear();
    if (!pick.hit) {
        return;
    }

    switch (pick.kind) {
    case ElementKind::Vertex:
      selection.mode = SelectionMode::Vertex;
      selection.vertices.push_back(pick.vertex_id);
      activate_pick_selection(selection, pick);
      break;
    case ElementKind::Edge:
      selection.mode = SelectionMode::Edge;
      selection.edges.push_back(make_edge(pick.edge.a, pick.edge.b));
      activate_pick_selection(selection, pick);
      break;
    case ElementKind::Face:
      selection.mode = SelectionMode::Face;
      selection.faces.push_back(pick.face_id);
      activate_pick_selection(selection, pick);
      break;
    }
}

void add_selection(Selection& selection, const PickResult& pick)
{
    if (!pick.hit) {
        return;
    }

    const SelectionMode pick_mode = selection_mode_for_kind(pick.kind);
    if (selection.mode != pick_mode) {
        set_selection_mode(selection, pick_mode);
    }

    switch (pick.kind) {
    case ElementKind::Vertex:
      if (!contains_id(selection.vertices, pick.vertex_id)) {
        selection.vertices.push_back(pick.vertex_id);
      }
      activate_pick_selection(selection, pick);
      break;
    case ElementKind::Edge:
      if (!contains_edge(selection.edges, pick.edge)) {
        selection.edges.push_back(make_edge(pick.edge.a, pick.edge.b));
      }
      activate_pick_selection(selection, pick);
      break;
    case ElementKind::Face:
      if (!contains_id(selection.faces, pick.face_id)) {
        selection.faces.push_back(pick.face_id);
      }
      activate_pick_selection(selection, pick);
      break;
    }
}

void remove_selection(Selection& selection, const PickResult& pick)
{
    if (!pick.hit || selection.mode != selection_mode_for_kind(pick.kind)) {
        return;
    }

    bool removed = false;
    switch (pick.kind) {
    case ElementKind::Vertex:
      removed = remove_id(selection.vertices, pick.vertex_id);
      break;
    case ElementKind::Edge:
      removed = remove_edge(selection.edges, pick.edge);
      break;
    case ElementKind::Face:
      removed = remove_id(selection.faces, pick.face_id);
      break;
    }
    if (removed) {
        activate_last_selection(selection);
    }
}

void set_selection_mode(Selection& selection, SelectionMode mode)
{
    selection.mode = mode;
    selection.clear();
}

void convert_selection_mode(const Document& document, Selection& selection, SelectionMode mode)
{
    if (selection.mode == mode) {
        return;
    }

    const Selection source = selection;
    const std::vector<ElementId> source_vertices = selected_vertex_ids(document, source);
    selection.clear();
    selection.mode = mode;

    switch (mode) {
    case SelectionMode::Vertex:
      for (const ElementId vertex_id : source_vertices) {
        if (find_vertex(document, vertex_id) != nullptr) {
          add_unique_id(selection.vertices, vertex_id);
        }
      }
      break;

    case SelectionMode::Edge:
      if (source.mode == SelectionMode::Face) {
        for (const ElementId face_id : source.faces) {
          const Face *face = find_face(document, face_id);
          if (face == nullptr) {
            continue;
          }
          for (const Edge &edge : face_edges(*face)) {
            add_unique_edge(selection.edges, edge);
          }
        }
      } else {
        for (const Edge &edge : document_edges(document)) {
          if (contains_id(source_vertices, edge.a) &&
              contains_id(source_vertices, edge.b)) {
            add_unique_edge(selection.edges, edge);
          }
        }
      }
      break;

    case SelectionMode::Face:
      if (source.mode == SelectionMode::Edge) {
        for (const Face &face : document.faces) {
          const std::vector<Edge> edges = face_edges(face);
          if (!edges.empty() &&
              std::ranges::all_of(edges, [&source](Edge edge) {
                return selection_contains(source, edge);
              })) {
            add_unique_id(selection.faces, face.id);
          }
        }
      } else {
        for (const Face &face : document.faces) {
          if (!face.vertices.empty() &&
              std::ranges::all_of(
                  face.vertices, [&source_vertices](ElementId vertex_id) {
                    return contains_id(source_vertices, vertex_id);
                  })) {
            add_unique_id(selection.faces, face.id);
          }
        }
      }
      break;
    }
    activate_last_selection(selection);
}

bool selection_contains(const Selection& selection, ElementId vertex_id)
{
    return contains_id(selection.vertices, vertex_id);
}

bool selection_contains(const Selection& selection, Edge edge)
{
    return contains_edge(selection.edges, edge);
}

bool selection_contains_face(const Selection& selection, ElementId face_id)
{
    return contains_id(selection.faces, face_id);
}

std::vector<ElementId> selected_vertex_ids(const Document& document, const Selection& selection)
{
    std::vector<ElementId> ids = selection.vertices;
    for (const Edge& edge : selection.edges) {
        ids.push_back(edge.a);
        ids.push_back(edge.b);
    }
    for (const ElementId face_id : selection.faces) {
        const Face* face = find_face(document, face_id);
        if (face == nullptr) {
            continue;
        }
        ids.insert(ids.end(), face->vertices.begin(), face->vertices.end());
    }
    return unique_vertex_ids(std::move(ids));
}

std::vector<Edge> edge_loop_edges(const Document& document, Edge seed_edge)
{
    return edge_loop_edges(document, seed_edge, kInvalidElementId);
}

std::vector<Edge> edge_loop_edges(const Document& document, Edge seed_edge, ElementId face_id)
{
    const Edge seed = make_edge(seed_edge.a, seed_edge.b);
    if (seed.a == kInvalidElementId || seed.b == kInvalidElementId ||
        seed.a == seed.b || !edge_exists(document, seed)) {
      return {};
    }

    // Boundary seeds use the same fan traversal Blender's edge-loop walker uses
    // before any picked-face surface-boundary context can turn a hole edge into
    // a plain face perimeter.
    const std::vector<Edge> boundary_edges = boundary_loop_edges(document, seed);
    if (boundary_edges.size() > 1U) {
        return boundary_edges;
    }

    const std::vector<Edge> face_context_edges =
        face_context_loop_edges(document, seed, face_id);
    if (face_context_edges.size() > 1U) {
        return face_context_edges;
    }

    std::vector<Edge> edges;
    edges.push_back(seed);
    append_edge_loop_direction(document, seed, seed.a, edges);
    append_edge_loop_direction(document, seed, seed.b, edges);
    return edges;
}

std::vector<Edge> edge_ring_edges(const Document& document, Edge seed_edge)
{
    const Edge seed = make_edge(seed_edge.a, seed_edge.b);
    if (seed.a == kInvalidElementId || seed.b == kInvalidElementId ||
        seed.a == seed.b || !edge_exists(document, seed)) {
        return {};
    }

    std::vector<Edge> ring { seed };
    const std::map<std::pair<ElementId, ElementId>, int> incidence_counts =
        edge_incidence_counts(document);
    if (edge_incidence_count(incidence_counts, seed) > 2) {
        return ring;
    }
    const std::map<std::pair<ElementId, ElementId>, std::vector<std::size_t>>
        faces_by_edge = face_indices_by_edge(document);

    std::vector<Edge> pending { seed };
    while (!pending.empty()) {
        const Edge current = pending.back();
        pending.pop_back();
        if (edge_incidence_count(incidence_counts, current) > 2) {
            continue;
        }

        const Edge normalized = make_edge(current.a, current.b);
        const auto found_faces =
            faces_by_edge.find({ normalized.a, normalized.b });
        if (found_faces == faces_by_edge.end()) {
            continue;
        }
        for (const std::size_t face_index : found_faces->second) {
            const Face* face = face_at_index(document, face_index);
            if (face == nullptr || face->vertices.size() != 4U) {
                continue;
            }

            for (std::size_t edge_index = 0U; edge_index < 4U;
                 ++edge_index) {
                const Edge face_edge = make_edge(
                    face->vertices[edge_index],
                    face->vertices[(edge_index + 1U) % 4U]);
                if (face_edge != normalized) {
                    continue;
                }

                const Edge opposite = make_edge(
                    face->vertices[(edge_index + 2U) % 4U],
                    face->vertices[(edge_index + 3U) % 4U]);
                const int opposite_incidence =
                    edge_incidence_count(incidence_counts, opposite);
                if (opposite_incidence < 1 || opposite_incidence > 2 ||
                    contains_edge(ring, opposite)) {
                    break;
                }
                ring.push_back(opposite);
                pending.push_back(opposite);
                break;
            }
        }
    }
    return ring;
}

void select_edge_loop(const Document& document, Selection& selection, Edge seed_edge, bool toggle)
{
    const std::vector<Edge> loop_edges = edge_loop_edges(document, seed_edge);
    if (loop_edges.empty()) {
        return;
    }

    if (selection.mode != SelectionMode::Edge) {
      selection.clear();
      selection.mode = SelectionMode::Edge;
    }

    if (!toggle) {
        selection.clear();
        selection.mode = SelectionMode::Edge;
        selection.edges = loop_edges;
        if (contains_edge(selection.edges, seed_edge)) {
            activate_edge_selection(selection, seed_edge);
        } else {
            activate_last_selection(selection);
        }
        return;
    }

    const bool all_selected = std::ranges::all_of(loop_edges, [&selection](Edge edge) {
        return selection_contains(selection, edge);
    });
    for (const Edge& edge : loop_edges) {
        if (all_selected) {
            [[maybe_unused]] const bool removed = remove_edge(selection.edges, edge);
        } else {
            add_unique_edge(selection.edges, edge);
        }
    }
    if (all_selected) {
        activate_last_selection(selection);
    } else if (contains_edge(selection.edges, seed_edge)) {
        activate_edge_selection(selection, seed_edge);
    } else {
        activate_last_selection(selection);
    }
}

std::vector<quader::QVec3> selected_edge_directions_in_comparison_space(
    const Document& document,
    const Selection& selection,
    const Transform3& comparison_from_document)
{
    if (selection.mode != SelectionMode::Edge) {
        return {};
    }

    std::vector<quader::QVec3> directions;
    directions.reserve(selection.edges.size());
    for (const Edge edge : selection.edges) {
        const std::optional<quader::QVec3> direction =
            comparison_edge_direction(document, edge,
                                      comparison_from_document);
        if (direction.has_value()) {
            directions.push_back(*direction);
        }
    }
    return directions;
}

std::vector<SelectionComparisonPlane>
selected_face_planes_in_comparison_space(
    const Document& document,
    const Selection& selection,
    const Transform3& comparison_from_document)
{
    if (selection.mode != SelectionMode::Face) {
        return {};
    }

    std::vector<SelectionComparisonPlane> planes;
    planes.reserve(selection.faces.size());
    for (const ElementId face_id : selection.faces) {
        const Face* face = find_face(document, face_id);
        if (face == nullptr) {
            continue;
        }
        const std::optional<SelectionComparisonPlane> plane =
            comparison_face_plane(document, *face,
                                  comparison_from_document);
        if (plane.has_value()) {
            planes.push_back(*plane);
        }
    }
    return planes;
}

std::vector<quader::QVec3> selected_face_normals_in_comparison_space(
    const Document& document,
    const Selection& selection,
    const Transform3& comparison_from_document)
{
    if (selection.mode != SelectionMode::Face) {
        return {};
    }

    std::vector<quader::QVec3> normals;
    normals.reserve(selection.faces.size());
    for (const ElementId face_id : selection.faces) {
        const Face* face = find_face(document, face_id);
        if (face == nullptr) {
            continue;
        }
        const std::optional<quader::QVec3> normal =
            comparison_face_normal(document, *face,
                                   comparison_from_document);
        if (normal.has_value()) {
            normals.push_back(*normal);
        }
    }
    return normals;
}

bool add_edges_matching_directions(
    const Document& document,
    Selection& selection,
    const Transform3& comparison_from_document,
    std::span<const quader::QVec3> comparison_directions,
    float angle_tolerance_radians)
{
    if (selection.mode != SelectionMode::Edge ||
        comparison_directions.empty() ||
        !std::isfinite(angle_tolerance_radians) ||
        angle_tolerance_radians < 0.0F) {
        return false;
    }

    std::vector<quader::QVec3> normalized_directions;
    normalized_directions.reserve(comparison_directions.size());
    for (const quader::QVec3 direction : comparison_directions) {
        const float direction_length_squared = quader::length_squared(direction);
        if (!std::isfinite(direction_length_squared) ||
            direction_length_squared <= 0.0F) {
            continue;
        }
        normalized_directions.push_back(
            direction / std::sqrt(direction_length_squared));
    }
    if (normalized_directions.empty()) {
        return false;
    }

    const Selection before = selection;
    const float tolerance = std::min(
        angle_tolerance_radians, std::numbers::pi_v<float>);
    const float minimum_alignment = std::cos(tolerance);
    for (const Edge candidate : document_edges(document)) {
        const std::optional<quader::QVec3> direction =
            comparison_edge_direction(document, candidate,
                                      comparison_from_document);
        if (!direction.has_value()) {
            continue;
        }
        const bool matches = std::ranges::any_of(
            normalized_directions, [&](quader::QVec3 source_direction) {
                return std::abs(quader::dot(*direction, source_direction)) >=
                       minimum_alignment;
            });
        if (matches) {
            add_unique_edge(selection.edges, candidate);
        }
    }
    return finish_selection_change(selection, before);
}

bool add_selected_edge_loops(const Document& document, Selection& selection)
{
    if (selection.mode != SelectionMode::Edge || selection.edges.empty()) {
        return false;
    }

    const Selection before = selection;
    const std::vector<Edge> original_edges = selection.edges;
    for (const Edge seed : original_edges) {
        for (const Edge edge : edge_loop_edges(document, seed)) {
            add_unique_edge(selection.edges, edge);
        }
    }
    return finish_selection_change(selection, before);
}

bool add_selected_edge_rings(const Document& document, Selection& selection)
{
    if (selection.mode != SelectionMode::Edge || selection.edges.empty()) {
        return false;
    }

    const Selection before = selection;
    const std::vector<Edge> original_edges = selection.edges;
    for (const Edge seed : original_edges) {
        for (const Edge edge : edge_ring_edges(document, seed)) {
            add_unique_edge(selection.edges, edge);
        }
    }
    return finish_selection_change(selection, before);
}

bool add_faces_with_selected_material_slots(const Document& document,
                                            Selection& selection)
{
    if (selection.mode != SelectionMode::Face || selection.faces.empty()) {
        return false;
    }

    std::set<std::uint32_t> selected_material_slots;
    for (const ElementId face_id : selection.faces) {
        const Face* face = find_face(document, face_id);
        if (face != nullptr) {
            selected_material_slots.insert(face->material_slot);
        }
    }
    if (selected_material_slots.empty()) {
        return false;
    }

    const std::vector<std::uint32_t> material_slots(
        selected_material_slots.begin(), selected_material_slots.end());
    return add_faces_with_material_slots(document, selection,
                                         material_slots);
}

bool add_faces_with_material_slots(
    const Document& document,
    Selection& selection,
    std::span<const std::uint32_t> material_slots)
{
    if (selection.mode != SelectionMode::Face || material_slots.empty()) {
        return false;
    }

    const Selection before = selection;
    for (const Face& face : document.faces) {
        if (std::ranges::find(material_slots, face.material_slot) !=
            material_slots.end()) {
            add_unique_id(selection.faces, face.id);
        }
    }
    return finish_selection_change(selection, before);
}

bool add_faces_coplanar_with_planes(
    const Document& document,
    Selection& selection,
    const Transform3& comparison_from_document,
    std::span<const SelectionComparisonPlane> comparison_planes)
{
    if (selection.mode != SelectionMode::Face || comparison_planes.empty()) {
        return false;
    }

    const Selection before = selection;
    for (const Face& face : document.faces) {
        const std::optional<SelectionComparisonPlane> candidate =
            comparison_face_plane(document, face,
                                  comparison_from_document);
        if (!candidate.has_value()) {
            continue;
        }
        const bool matches = std::ranges::any_of(
            comparison_planes,
            [&](const SelectionComparisonPlane& source_plane) {
                return planes_are_exactly_coplanar(source_plane, *candidate);
            });
        if (matches) {
            add_unique_id(selection.faces, face.id);
        }
    }
    return finish_selection_change(selection, before);
}

bool add_faces_matching_normals(
    const Document& document,
    Selection& selection,
    const Transform3& comparison_from_document,
    std::span<const quader::QVec3> comparison_normals)
{
    if (selection.mode != SelectionMode::Face ||
        comparison_normals.empty()) {
        return false;
    }

    const Selection before = selection;
    for (const Face& face : document.faces) {
        const std::optional<quader::QVec3> candidate =
            comparison_face_normal(document, face,
                                   comparison_from_document);
        if (!candidate.has_value()) {
            continue;
        }
        const bool matches = std::ranges::any_of(
            comparison_normals, [&](quader::QVec3 source) {
                return normals_match_same_facing(*candidate, source);
            });
        if (matches) {
            add_unique_id(selection.faces, face.id);
        }
    }
    return finish_selection_change(selection, before);
}

bool replace_selection_with_ngons(const Document& document,
                                  Selection& selection)
{
    if (selection.mode != SelectionMode::Face) {
        return false;
    }

    const Selection before = selection;
    selection.faces.clear();
    for (const Face& face : document.faces) {
        if (face.vertices.size() > 4U) {
            selection.faces.push_back(face.id);
        }
    }
    return finish_selection_change(selection, before);
}

bool grow_selection(const Document& document, Selection& selection)
{
    const Selection before = selection;
    const std::vector<Edge> all_edges = document_edges(document);
    const std::map<std::pair<ElementId, ElementId>, int> incidence_counts =
        edge_incidence_counts(document);

    switch (selection.mode) {
    case SelectionMode::Vertex:
      for (const Face& face : document.faces) {
          if (!face_shares_selected_vertex(face, before.vertices)) {
              continue;
          }
          for (const ElementId vertex_id : face.vertices) {
              add_unique_id(selection.vertices, vertex_id);
          }
      }
      for (const Edge edge : all_edges) {
          if (edge_incidence_count(incidence_counts, edge) != 0) {
              continue;
          }
          if (contains_id(before.vertices, edge.a)) {
              add_unique_id(selection.vertices, edge.b);
          }
          if (contains_id(before.vertices, edge.b)) {
              add_unique_id(selection.vertices, edge.a);
          }
      }
      break;
    case SelectionMode::Edge: {
      const std::vector<ElementId> original_vertices =
          selected_edge_vertex_ids(before);
      for (const Face& face : document.faces) {
          if (!face_shares_selected_vertex(face, original_vertices)) {
              continue;
          }
          for (const Edge edge : face_edges(face)) {
              add_unique_edge(selection.edges, edge);
          }
      }
      for (const Edge edge : all_edges) {
          if (edge_incidence_count(incidence_counts, edge) == 0 &&
              (contains_id(original_vertices, edge.a) ||
               contains_id(original_vertices, edge.b))) {
              add_unique_edge(selection.edges, edge);
          }
      }
      break;
    }
    case SelectionMode::Face:
      for (const Face& face : document.faces) {
          const bool shares_vertex = std::ranges::any_of(
              before.faces, [&](ElementId selected_face_id) {
                  const Face* selected_face =
                      find_face(document, selected_face_id);
                  return selected_face != nullptr &&
                         face_uses_any_vertex(face,
                                              selected_face->vertices);
              });
          if (shares_vertex) {
              add_unique_id(selection.faces, face.id);
          }
      }
      break;
    }
    return finish_selection_change(selection, before);
}

bool shrink_selection(const Document& document, Selection& selection)
{
    const Selection before = selection;
    const std::vector<Edge> all_edges = document_edges(document);
    const std::map<std::pair<ElementId, ElementId>, int> incidence_counts =
        edge_incidence_counts(document);

    switch (selection.mode) {
    case SelectionMode::Vertex: {
      std::vector<ElementId> retained;
      for (const ElementId vertex_id : before.vertices) {
          bool boundary = false;
          for (const Face& face : document.faces) {
              if (face_uses_vertex(face, vertex_id) &&
                  !face_all_vertices_selected(face, before)) {
                  boundary = true;
                  break;
              }
          }
          if (!boundary) {
              boundary = std::ranges::any_of(all_edges, [&](Edge edge) {
                  return edge_incidence_count(incidence_counts, edge) == 0 &&
                         (edge.a == vertex_id || edge.b == vertex_id) &&
                         (!contains_id(before.vertices, edge.a) ||
                          !contains_id(before.vertices, edge.b));
              });
          }
          if (!boundary) {
              add_unique_id(retained, vertex_id);
          }
      }
      selection.vertices = std::move(retained);
      break;
    }
    case SelectionMode::Edge: {
      std::set<ElementId> boundary_vertices;
      for (const ElementId vertex_id : selected_edge_vertex_ids(before)) {
          bool boundary = false;
          for (const Face& face : document.faces) {
              if (face_uses_vertex(face, vertex_id) &&
                  !face_all_edges_selected(face, before)) {
                  boundary = true;
                  break;
              }
          }
          if (!boundary) {
              boundary = std::ranges::any_of(all_edges, [&](Edge edge) {
                  return edge_incidence_count(incidence_counts, edge) == 0 &&
                         (edge.a == vertex_id || edge.b == vertex_id) &&
                         !contains_edge(before.edges, edge);
              });
          }
          if (boundary) {
              boundary_vertices.insert(vertex_id);
          }
      }

      std::vector<Edge> retained;
      for (const Edge edge : before.edges) {
          if (!boundary_vertices.contains(edge.a) &&
              !boundary_vertices.contains(edge.b)) {
              add_unique_edge(retained, edge);
          }
      }
      selection.edges = std::move(retained);
      break;
    }
    case SelectionMode::Face: {
      std::vector<ElementId> retained;
      for (const ElementId face_id : before.faces) {
          const Face* face = find_face(document, face_id);
          if (face == nullptr) {
              continue;
          }
          const bool boundary = std::ranges::any_of(
              document.faces, [&](const Face& candidate) {
                  return !contains_id(before.faces, candidate.id) &&
                         face_uses_any_vertex(candidate, face->vertices);
              });
          if (!boundary) {
              retained.push_back(face_id);
          }
      }
      selection.faces = std::move(retained);
      break;
    }
    }
    return finish_selection_change(selection, before);
}

bool apply_component_selection_command(
    const Document& document,
    Selection& selection,
    ComponentSelectionCommand command,
    const Transform3& comparison_from_document,
    float direction_angle_tolerance_radians)
{
    switch (command) {
    case ComponentSelectionCommand::SelectAll:
      return select_all_components(document, selection);
    case ComponentSelectionCommand::SelectInverse:
      return invert_component_selection(document, selection);
    case ComponentSelectionCommand::SelectDirection: {
      const std::vector<quader::QVec3> directions =
          selected_edge_directions_in_comparison_space(
              document, selection, comparison_from_document);
      return add_edges_matching_directions(
          document, selection, comparison_from_document, directions,
          direction_angle_tolerance_radians);
    }
    case ComponentSelectionCommand::SelectLoops:
      return add_selected_edge_loops(document, selection);
    case ComponentSelectionCommand::SelectEdgeRing:
      return add_selected_edge_rings(document, selection);
    case ComponentSelectionCommand::SelectFacesWithSameMaterial:
      return add_faces_with_selected_material_slots(document, selection);
    case ComponentSelectionCommand::SelectCoplanarFaces: {
      const std::vector<SelectionComparisonPlane> planes =
          selected_face_planes_in_comparison_space(
              document, selection, comparison_from_document);
      return add_faces_coplanar_with_planes(
          document, selection, comparison_from_document, planes);
    }
    case ComponentSelectionCommand::SelectFaceNormal: {
      const std::vector<quader::QVec3> normals =
          selected_face_normals_in_comparison_space(
              document, selection, comparison_from_document);
      return add_faces_matching_normals(
          document, selection, comparison_from_document, normals);
    }
    case ComponentSelectionCommand::SelectNGons:
      return replace_selection_with_ngons(document, selection);
    case ComponentSelectionCommand::Grow:
      return grow_selection(document, selection);
    case ComponentSelectionCommand::Shrink:
      return shrink_selection(document, selection);
    }
    return false;
}

std::optional<quader::QVec3> selection_center(const Document& document, const Selection& selection)
{
    const std::vector<ElementId> ids = selected_vertex_ids(document, selection);
    if (ids.empty()) {
        return std::nullopt;
    }

    quader::QVec3 center;
    std::size_t count = 0;
    for (const ElementId id : ids) {
        const Vertex* vertex = find_vertex(document, id);
        if (vertex == nullptr) {
            continue;
        }

        center += vertex->position;
        ++count;
    }

    if (count == 0) {
        return std::nullopt;
    }
    return center / static_cast<float>(count);
}

} // namespace quader_poly
