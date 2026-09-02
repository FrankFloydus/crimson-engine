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
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <unordered_map>
#include <unordered_set>
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

using TopologyEdgeKey = std::pair<ElementId, ElementId>;
using EdgeIncidenceCounts = std::map<TopologyEdgeKey, int>;
using FaceIndicesByEdge =
    std::map<TopologyEdgeKey, std::vector<std::size_t>>;
using EdgesByVertex = std::map<ElementId, std::vector<Edge>>;

EdgesByVertex make_edges_by_vertex(const EdgeIncidenceCounts& incidence_counts,
                                   std::optional<int> required_incidence =
                                       std::nullopt)
{
    EdgesByVertex edges_by_vertex;
    for (const auto& [key, incidence] : incidence_counts) {
        if (required_incidence.has_value() &&
            incidence != *required_incidence) {
            continue;
        }
        const Edge edge { key.first, key.second };
        edges_by_vertex[edge.a].push_back(edge);
        edges_by_vertex[edge.b].push_back(edge);
    }
    return edges_by_vertex;
}

bool indexed_edges_share_face(const FaceIndicesByEdge& faces_by_edge,
                              Edge left,
                              Edge right)
{
    const auto left_faces = faces_by_edge.find(edge_key(left));
    const auto right_faces = faces_by_edge.find(edge_key(right));
    if (left_faces == faces_by_edge.end() ||
        right_faces == faces_by_edge.end()) {
        return false;
    }
    return std::ranges::any_of(left_faces->second, [&](std::size_t face_index) {
        return std::ranges::find(right_faces->second, face_index) !=
               right_faces->second.end();
    });
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

bool boundary_edge_is_single(const Document& document,
                             const EdgeIncidenceCounts& incidence_counts,
                             const FaceIndicesByEdge& faces_by_edge,
                             Edge edge)
{
    edge = make_edge(edge.a, edge.b);
    if (edge_incidence_count(incidence_counts, edge) != 1) {
        return false;
    }

    const auto found_faces = faces_by_edge.find(edge_key(edge));
    if (found_faces == faces_by_edge.end() ||
        found_faces->second.size() != 1U) {
        return false;
    }
    const Face* face = face_at_index(document, found_faces->second.front());
    if (face == nullptr || face->vertices.size() <= 4U) {
        return false;
    }

    for (std::size_t index = 0; index < face->vertices.size(); ++index) {
        if (make_edge(face->vertices[index],
                      face->vertices[(index + 1U) % face->vertices.size()]) !=
            edge) {
            continue;
        }
        const std::size_t previous_index =
            (index + face->vertices.size() - 1U) % face->vertices.size();
        const Edge previous = make_edge(
            face->vertices[previous_index], face->vertices[index]);
        const Edge next = make_edge(
            face->vertices[(index + 1U) % face->vertices.size()],
            face->vertices[(index + 2U) % face->vertices.size()]);
        return edge_incidence_count(incidence_counts, previous) == 1 ||
               edge_incidence_count(incidence_counts, next) == 1;
    }
    return false;
}

const Face* opposite_face_across_edge(const Document& document,
                                      const FaceIndicesByEdge& faces_by_edge,
                                      Edge edge,
                                      ElementId source_face_id)
{
    const auto found_faces = faces_by_edge.find(edge_key(edge));
    if (found_faces == faces_by_edge.end()) {
        return nullptr;
    }
    for (const std::size_t face_index : found_faces->second) {
        const Face* face = face_at_index(document, face_index);
        if (face != nullptr && face->id != source_face_id) {
            return face;
        }
    }
    return nullptr;
}

std::optional<Edge> next_boundary_loop_edge(
    const Document& document,
    const EdgeIncidenceCounts& incidence_counts,
    const FaceIndicesByEdge& faces_by_edge,
    const EdgesByVertex& edges_by_vertex,
    Edge current,
    ElementId through_vertex,
    bool seed_is_single)
{
    current = make_edge(current.a, current.b);
    if (edge_incidence_count(incidence_counts, current) != 1 ||
        other_edge_vertex(current, through_vertex) == kInvalidElementId) {
        return std::nullopt;
    }

    const auto incident_edges = edges_by_vertex.find(through_vertex);
    if (incident_edges == edges_by_vertex.end()) {
        return std::nullopt;
    }
    if (incident_edges->second.size() == 2U &&
        !boundary_edge_is_single(
            document, incidence_counts, faces_by_edge, current)) {
        return std::nullopt;
    }

    const auto current_faces = faces_by_edge.find(edge_key(current));
    if (current_faces == faces_by_edge.end() ||
        current_faces->second.size() != 1U) {
        return std::nullopt;
    }
    const Face* face = face_at_index(document, current_faces->second.front());
    if (face == nullptr) {
        return std::nullopt;
    }
    std::optional<Edge> fan_edge =
        adjacent_face_edge_at_vertex(*face, current, through_vertex);
    std::set<TopologyEdgeKey> visited_edges;
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
            if (boundary_edge_is_single(
                    document, incidence_counts, faces_by_edge, candidate) !=
                seed_is_single) {
                return std::nullopt;
            }
            return candidate;
        }
        if (incidence != 2) {
            return std::nullopt;
        }

        if (!visited_edges.insert(edge_key(candidate)).second) {
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
    const EdgeIncidenceCounts& incidence_counts,
    const FaceIndicesByEdge& faces_by_edge,
    const EdgesByVertex& edges_by_vertex,
    Edge seed,
    ElementId through_vertex,
    bool seed_is_single,
    std::vector<Edge>& edges)
{
    Edge current = make_edge(seed.a, seed.b);
    ElementId vertex = through_vertex;
    const std::size_t max_steps = document_edges(document).size();
    for (std::size_t step = 0; step < max_steps; ++step) {
        const std::optional<Edge> next = next_boundary_loop_edge(
            document, incidence_counts, faces_by_edge, edges_by_vertex,
            current, vertex, seed_is_single);
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
    const EdgeIncidenceCounts incidence_counts =
        edge_incidence_counts(document);
    if (edge_incidence_count(incidence_counts, seed) != 1) {
        return {};
    }

    const FaceIndicesByEdge faces_by_edge = face_indices_by_edge(document);
    const EdgesByVertex edges_by_vertex =
        make_edges_by_vertex(incidence_counts);
    const bool seed_is_single = boundary_edge_is_single(
        document, incidence_counts, faces_by_edge, seed);
    std::vector<Edge> edges;
    edges.push_back(seed);
    append_boundary_loop_direction(
        document, incidence_counts, faces_by_edge, edges_by_vertex, seed,
        seed.a, seed_is_single, edges);
    append_boundary_loop_direction(
        document, incidence_counts, faces_by_edge, edges_by_vertex, seed,
        seed.b, seed_is_single, edges);
    return edges;
}

std::optional<Edge> next_interior_loop_edge(
    const EdgeIncidenceCounts& incidence_counts,
    const FaceIndicesByEdge& faces_by_edge,
    const EdgesByVertex& edges_by_vertex,
    Edge current,
    ElementId through_vertex)
{
    current = make_edge(current.a, current.b);
    if (edge_incidence_count(incidence_counts, current) != 2) {
        return std::nullopt;
    }
    const auto incident_edges = edges_by_vertex.find(through_vertex);
    if (incident_edges == edges_by_vertex.end()) {
        return std::nullopt;
    }
    const std::vector<Edge>& candidates = incident_edges->second;
    if (candidates.size() != 2U && candidates.size() != 4U) {
        return std::nullopt;
    }
    if (std::ranges::any_of(candidates, [&](Edge edge) {
            return edge_incidence_count(incidence_counts, edge) != 2;
        })) {
        return std::nullopt;
    }

    if (candidates.size() == 2U) {
        const auto next = std::ranges::find_if(candidates, [&](Edge edge) {
            return edge != current;
        });
        return next == candidates.end() ? std::nullopt
                                        : std::optional<Edge> { *next };
    }

    std::optional<Edge> opposite;
    for (const Edge candidate : candidates) {
        if (candidate == current ||
            indexed_edges_share_face(faces_by_edge, current, candidate)) {
            continue;
        }
        if (opposite.has_value()) {
            return std::nullopt;
        }
        opposite = candidate;
    }
    return opposite;
}

void append_interior_loop_direction(
    const EdgeIncidenceCounts& incidence_counts,
    const FaceIndicesByEdge& faces_by_edge,
    const EdgesByVertex& edges_by_vertex,
    Edge seed,
    ElementId through_vertex,
    std::vector<Edge>& edges)
{
    Edge current = make_edge(seed.a, seed.b);
    ElementId vertex = through_vertex;
    const std::size_t max_steps = incidence_counts.size();
    for (std::size_t step = 0; step < max_steps; ++step) {
        const std::optional<Edge> next = next_interior_loop_edge(
            incidence_counts, faces_by_edge, edges_by_vertex, current,
            vertex);
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
        constexpr double kEpsilon =
            std::numeric_limits<float>::epsilon();
        const double bound =
            4.0 * kEpsilon +
            2.0 * kEpsilon * (std::abs(first) + std::abs(second));
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

using ElementIdIndex = std::unordered_set<ElementId>;
using EdgeIndex = std::unordered_set<std::uint64_t>;
using FaceByIdIndex = std::unordered_map<ElementId, const Face*>;

ElementIdIndex make_element_id_index(std::span<const ElementId> ids)
{
    ElementIdIndex index;
    index.reserve(ids.size());
    index.insert(ids.begin(), ids.end());
    return index;
}

std::uint64_t indexed_edge_key(Edge edge)
{
    edge = make_edge(edge.a, edge.b);
    return (static_cast<std::uint64_t>(edge.a) << 32U) |
           static_cast<std::uint64_t>(edge.b);
}

EdgeIndex make_edge_index(std::span<const Edge> edges)
{
    EdgeIndex index;
    index.reserve(edges.size());
    for (const Edge edge : edges) {
        index.insert(indexed_edge_key(edge));
    }
    return index;
}

FaceByIdIndex make_face_by_id_index(const Document& document)
{
    FaceByIdIndex index;
    index.reserve(document.faces.size());
    for (const Face& face : document.faces) {
        index.try_emplace(face.id, &face);
    }
    return index;
}

void add_indexed_id(std::vector<ElementId>& ids,
                    ElementIdIndex& index,
                    ElementId id)
{
    if (id != kInvalidElementId && index.insert(id).second) {
        ids.push_back(id);
    }
}

void add_indexed_edge(std::vector<Edge>& edges,
                      EdgeIndex& index,
                      Edge edge)
{
    edge = make_edge(edge.a, edge.b);
    if (edge.a == kInvalidElementId || edge.b == kInvalidElementId ||
        edge.a == edge.b || !index.insert(indexed_edge_key(edge)).second) {
        return;
    }
    edges.push_back(edge);
}

bool selection_universe_payload_matches_mode(
    const ComponentSelectionUniverse& universe)
{
    switch (universe.mode) {
    case SelectionMode::Vertex:
      return universe.edges.empty() && universe.faces.empty();
    case SelectionMode::Edge:
      return universe.vertices.empty() && universe.faces.empty();
    case SelectionMode::Face:
      return universe.vertices.empty() && universe.edges.empty();
    }
    return false;
}

bool selection_universe_is_live_and_unique(
    const Document& document,
    const ComponentSelectionUniverse& universe)
{
    if (!selection_universe_payload_matches_mode(universe)) {
        return false;
    }

    switch (universe.mode) {
    case SelectionMode::Vertex: {
      const ElementIdIndex requested =
          make_element_id_index(universe.vertices);
      if (requested.size() != universe.vertices.size()) {
          return false;
      }
      const ElementIdIndex live = [&]() {
          std::vector<ElementId> ids;
          ids.reserve(document.vertices.size());
          for (const Vertex& vertex : document.vertices) {
              ids.push_back(vertex.id);
          }
          return make_element_id_index(ids);
      }();
      return std::ranges::all_of(universe.vertices, [&](ElementId id) {
          return id != kInvalidElementId && live.contains(id);
      });
    }
    case SelectionMode::Edge: {
      const EdgeIndex requested = make_edge_index(universe.edges);
      if (requested.size() != universe.edges.size()) {
          return false;
      }
      const EdgeIndex live = make_edge_index(document_edges(document));
      return std::ranges::all_of(universe.edges, [&](Edge edge) {
          return edge.a != kInvalidElementId &&
                 edge.b != kInvalidElementId && edge.a != edge.b &&
                 edge == make_edge(edge.a, edge.b) &&
                 live.contains(indexed_edge_key(edge));
      });
    }
    case SelectionMode::Face: {
      const ElementIdIndex requested = make_element_id_index(universe.faces);
      if (requested.size() != universe.faces.size()) {
          return false;
      }
      const ElementIdIndex live = [&]() {
          std::vector<ElementId> ids;
          ids.reserve(document.faces.size());
          for (const Face& face : document.faces) {
              ids.push_back(face.id);
          }
          return make_element_id_index(ids);
      }();
      return std::ranges::all_of(universe.faces, [&](ElementId id) {
          return id != kInvalidElementId && live.contains(id);
      });
    }
    }
    return false;
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

bool face_all_vertices_selected(const Face& face,
                                const ElementIdIndex& selected_vertices)
{
    return !face.vertices.empty() &&
           std::ranges::all_of(face.vertices, [&](ElementId vertex_id) {
               return selected_vertices.contains(vertex_id);
           });
}

bool face_all_edges_selected(const Face& face,
                             const EdgeIndex& selected_edges)
{
    const std::vector<Edge> edges = face_edges(face);
    return !edges.empty() && std::ranges::all_of(edges, [&](Edge edge) {
               return selected_edges.contains(indexed_edge_key(edge));
           });
}

bool face_shares_selected_vertex(
    const Face& face,
    const ElementIdIndex& selected_vertices)
{
    return std::ranges::any_of(face.vertices, [&](ElementId vertex_id) {
        return selected_vertices.contains(vertex_id);
    });
}

bool select_all_components(const Document& document, Selection& selection)
{
    const Selection before = selection;
    switch (selection.mode) {
    case SelectionMode::Vertex: {
      selection.vertices.clear();
      selection.vertices.reserve(document.vertices.size());
      ElementIdIndex selected_vertices;
      selected_vertices.reserve(document.vertices.size());
      for (const Vertex& vertex : document.vertices) {
          add_indexed_id(selection.vertices, selected_vertices, vertex.id);
      }
      break;
    }
    case SelectionMode::Edge:
      selection.edges = document_edges(document);
      break;
    case SelectionMode::Face: {
      selection.faces.clear();
      selection.faces.reserve(document.faces.size());
      ElementIdIndex selected_faces;
      selected_faces.reserve(document.faces.size());
      for (const Face& face : document.faces) {
          add_indexed_id(selection.faces, selected_faces, face.id);
      }
      break;
    }
    }
    return finish_selection_change(selection, before);
}

} // namespace

ComponentSelectionUniverse all_component_selection_universe(
    const Document& document,
    SelectionMode mode)
{
    ComponentSelectionUniverse universe;
    universe.mode = mode;
    switch (mode) {
    case SelectionMode::Vertex:
      universe.vertices.reserve(document.vertices.size());
      for (const Vertex& vertex : document.vertices) {
          add_unique_id(universe.vertices, vertex.id);
      }
      break;
    case SelectionMode::Edge:
      universe.edges = document_edges(document);
      break;
    case SelectionMode::Face:
      universe.faces.reserve(document.faces.size());
      for (const Face& face : document.faces) {
          add_unique_id(universe.faces, face.id);
      }
      break;
    }
    return universe;
}

bool invert_component_selection(const Document& document,
                                Selection& selection)
{
    return invert_component_selection(
        document, selection,
        all_component_selection_universe(document, selection.mode));
}

bool invert_component_selection(const Document& document,
                                Selection& selection,
                                const ComponentSelectionUniverse& universe)
{
    if (selection.mode != universe.mode ||
        !selection_universe_is_live_and_unique(document, universe)) {
        return false;
    }

    const Selection before = selection;
    switch (universe.mode) {
    case SelectionMode::Vertex: {
      if (universe.vertices.empty()) {
          return false;
      }
      const ElementIdIndex selected =
          make_element_id_index(before.vertices);
      const ElementIdIndex eligible =
          make_element_id_index(universe.vertices);
      selection.vertices.clear();
      selection.vertices.reserve(document.vertices.size());
      for (const Vertex& vertex : document.vertices) {
          if (selected.contains(vertex.id) != eligible.contains(vertex.id)) {
              selection.vertices.push_back(vertex.id);
          }
      }
      break;
    }
    case SelectionMode::Edge: {
      if (universe.edges.empty()) {
          return false;
      }
      const EdgeIndex selected = make_edge_index(before.edges);
      const EdgeIndex eligible = make_edge_index(universe.edges);
      selection.edges.clear();
      const std::vector<Edge> live_edges = document_edges(document);
      selection.edges.reserve(live_edges.size());
      for (const Edge edge : live_edges) {
          const auto key = indexed_edge_key(edge);
          if (selected.contains(key) != eligible.contains(key)) {
              selection.edges.push_back(edge);
          }
      }
      break;
    }
    case SelectionMode::Face: {
      if (universe.faces.empty()) {
          return false;
      }
      const ElementIdIndex selected = make_element_id_index(before.faces);
      const ElementIdIndex eligible = make_element_id_index(universe.faces);
      selection.faces.clear();
      selection.faces.reserve(document.faces.size());
      for (const Face& face : document.faces) {
          if (selected.contains(face.id) != eligible.contains(face.id)) {
              selection.faces.push_back(face.id);
          }
      }
      break;
    }
    }
    return finish_selection_change(selection, before);
}

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
    selection.clear();
    selection.mode = mode;

    switch (mode) {
    case SelectionMode::Vertex: {
      const std::vector<ElementId> source_vertices =
          selected_vertex_ids(document, source);
      ElementIdIndex document_vertices;
      document_vertices.reserve(document.vertices.size());
      for (const Vertex& vertex : document.vertices) {
          document_vertices.insert(vertex.id);
      }
      for (const ElementId vertex_id : source_vertices) {
        if (document_vertices.contains(vertex_id)) {
          selection.vertices.push_back(vertex_id);
        }
      }
      break;
    }

    case SelectionMode::Edge:
      if (source.mode == SelectionMode::Face) {
        const FaceByIdIndex faces_by_id =
            make_face_by_id_index(document);
        EdgeIndex selected_edges;
        for (const ElementId face_id : source.faces) {
          const auto found = faces_by_id.find(face_id);
          if (found == faces_by_id.end() || found->second == nullptr) {
            continue;
          }
          for (const Edge &edge : face_edges(*found->second)) {
            add_indexed_edge(selection.edges, selected_edges, edge);
          }
        }
      } else {
        const std::vector<ElementId> source_vertices =
            selected_vertex_ids(document, source);
        const ElementIdIndex source_vertex_index =
            make_element_id_index(source_vertices);
        for (const Edge &edge : document_edges(document)) {
          if (source_vertex_index.contains(edge.a) &&
              source_vertex_index.contains(edge.b)) {
            selection.edges.push_back(edge);
          }
        }
      }
      break;

    case SelectionMode::Face:
      if (source.mode == SelectionMode::Edge) {
        const EdgeIndex source_edge_index =
            make_edge_index(source.edges);
        ElementIdIndex selected_faces;
        for (const Face &face : document.faces) {
          const std::vector<Edge> edges = face_edges(face);
          if (!edges.empty() &&
              std::ranges::all_of(edges, [&source_edge_index](Edge edge) {
                return source_edge_index.contains(indexed_edge_key(edge));
              })) {
            add_indexed_id(selection.faces, selected_faces, face.id);
          }
        }
      } else {
        const std::vector<ElementId> source_vertices =
            selected_vertex_ids(document, source);
        const ElementIdIndex source_vertex_index =
            make_element_id_index(source_vertices);
        ElementIdIndex selected_faces;
        for (const Face &face : document.faces) {
          if (!face.vertices.empty() &&
              std::ranges::all_of(
                  face.vertices, [&source_vertex_index](ElementId vertex_id) {
                    return source_vertex_index.contains(vertex_id);
                  })) {
            add_indexed_id(selection.faces, selected_faces, face.id);
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
    const FaceByIdIndex faces_by_id =
        selection.faces.empty() ? FaceByIdIndex {}
                                : make_face_by_id_index(document);
    for (const ElementId face_id : selection.faces) {
        const auto found = faces_by_id.find(face_id);
        if (found == faces_by_id.end() || found->second == nullptr) {
            continue;
        }
        ids.insert(ids.end(), found->second->vertices.begin(),
                   found->second->vertices.end());
    }
    return unique_vertex_ids(std::move(ids));
}

std::vector<Edge> edge_loop_edges(const Document& document, Edge seed_edge)
{
    return edge_loop_edges(document, seed_edge, kInvalidElementId);
}

std::vector<Edge> edge_loop_edges(const Document& document, Edge seed_edge, ElementId face_id)
{
    (void)face_id;
    const Edge seed = make_edge(seed_edge.a, seed_edge.b);
    if (seed.a == kInvalidElementId || seed.b == kInvalidElementId ||
        seed.a == seed.b || !edge_exists(document, seed)) {
      return {};
    }

    const EdgeIncidenceCounts incidence_counts =
        edge_incidence_counts(document);
    const int seed_incidence = edge_incidence_count(incidence_counts, seed);
    if (seed_incidence == 1) {
        return boundary_loop_edges(document, seed);
    }
    if (seed_incidence != 2) {
        return { seed };
    }

    const FaceIndicesByEdge faces_by_edge = face_indices_by_edge(document);
    const EdgesByVertex edges_by_vertex =
        make_edges_by_vertex(incidence_counts);
    std::vector<Edge> edges { seed };
    append_interior_loop_direction(
        incidence_counts, faces_by_edge, edges_by_vertex, seed, seed.a,
        edges);
    append_interior_loop_direction(
        incidence_counts, faces_by_edge, edges_by_vertex, seed, seed.b,
        edges);
    return edges;
}

std::vector<Edge> edge_boundary_component_edges(const Document& document,
                                                Edge seed_edge)
{
    const Edge seed = make_edge(seed_edge.a, seed_edge.b);
    if (seed.a == kInvalidElementId || seed.b == kInvalidElementId ||
        seed.a == seed.b) {
        return {};
    }

    const EdgeIncidenceCounts incidence_counts =
        edge_incidence_counts(document);
    if (edge_incidence_count(incidence_counts, seed) != 1) {
        return {};
    }
    const EdgesByVertex boundary_edges_by_vertex =
        make_edges_by_vertex(incidence_counts, 1);

    std::vector<Edge> component { seed };
    std::set<TopologyEdgeKey> visited { edge_key(seed) };
    for (std::size_t queue_index = 0; queue_index < component.size();
         ++queue_index) {
        const Edge current = component[queue_index];
        std::vector<Edge> neighbors;
        for (const ElementId vertex : { current.a, current.b }) {
            const auto found = boundary_edges_by_vertex.find(vertex);
            if (found != boundary_edges_by_vertex.end()) {
                neighbors.insert(neighbors.end(), found->second.begin(),
                                 found->second.end());
            }
        }
        std::ranges::sort(neighbors, {}, [](Edge edge) {
            return edge_key(edge);
        });
        neighbors.erase(std::ranges::unique(neighbors).begin(),
                        neighbors.end());
        for (const Edge neighbor : neighbors) {
            if (visited.insert(edge_key(neighbor)).second) {
                component.push_back(neighbor);
            }
        }
    }
    return component;
}

std::vector<Edge> edge_ring_edges(const Document& document, Edge seed_edge)
{
    const Edge seed = make_edge(seed_edge.a, seed_edge.b);
    if (seed.a == kInvalidElementId || seed.b == kInvalidElementId ||
        seed.a == seed.b || !edge_exists(document, seed)) {
        return {};
    }

    std::vector<Edge> ring { seed };
    const EdgeIncidenceCounts incidence_counts =
        edge_incidence_counts(document);
    const int seed_incidence = edge_incidence_count(incidence_counts, seed);
    if (seed_incidence != 1 && seed_incidence != 2) {
        return ring;
    }
    const FaceIndicesByEdge faces_by_edge = face_indices_by_edge(document);

    std::vector<Edge> pending { seed };
    while (!pending.empty()) {
        const Edge current = pending.back();
        pending.pop_back();
        const int current_incidence =
            edge_incidence_count(incidence_counts, current);
        if (current_incidence != 1 && current_incidence != 2) {
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
    case SelectionMode::Vertex: {
      const ElementIdIndex original_vertices =
          make_element_id_index(before.vertices);
      ElementIdIndex selected_vertices = original_vertices;
      for (const Face& face : document.faces) {
          if (!face_shares_selected_vertex(face, original_vertices)) {
              continue;
          }
          for (const ElementId vertex_id : face.vertices) {
              add_indexed_id(selection.vertices, selected_vertices,
                             vertex_id);
          }
      }
      for (const Edge edge : all_edges) {
          if (edge_incidence_count(incidence_counts, edge) != 0) {
              continue;
          }
          if (original_vertices.contains(edge.a)) {
              add_indexed_id(selection.vertices, selected_vertices, edge.b);
          }
          if (original_vertices.contains(edge.b)) {
              add_indexed_id(selection.vertices, selected_vertices, edge.a);
          }
      }
      break;
    }
    case SelectionMode::Edge: {
      ElementIdIndex original_vertices;
      original_vertices.reserve(before.edges.size() * 2U);
      for (const Edge edge : before.edges) {
          if (edge.a != kInvalidElementId) {
              original_vertices.insert(edge.a);
          }
          if (edge.b != kInvalidElementId) {
              original_vertices.insert(edge.b);
          }
      }
      EdgeIndex selected_edges = make_edge_index(before.edges);
      for (const Face& face : document.faces) {
          if (!face_shares_selected_vertex(face, original_vertices)) {
              continue;
          }
          for (const Edge edge : face_edges(face)) {
              add_indexed_edge(selection.edges, selected_edges, edge);
          }
      }
      for (const Edge edge : all_edges) {
          if (edge_incidence_count(incidence_counts, edge) == 0 &&
              (original_vertices.contains(edge.a) ||
               original_vertices.contains(edge.b))) {
              add_indexed_edge(selection.edges, selected_edges, edge);
          }
      }
      break;
    }
    case SelectionMode::Face: {
      const ElementIdIndex original_faces =
          make_element_id_index(before.faces);
      ElementIdIndex original_vertices;
      original_vertices.reserve(document.vertices.size());
      for (const Face& face : document.faces) {
          if (original_faces.contains(face.id)) {
              original_vertices.insert(face.vertices.begin(),
                                       face.vertices.end());
          }
      }
      ElementIdIndex selected_faces = original_faces;
      for (const Face& face : document.faces) {
          if (face_shares_selected_vertex(face, original_vertices)) {
              add_indexed_id(selection.faces, selected_faces, face.id);
          }
      }
      break;
    }
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
      const ElementIdIndex selected_vertices =
          make_element_id_index(before.vertices);
      ElementIdIndex boundary_vertices;
      for (const Face& face : document.faces) {
          if (face_all_vertices_selected(face, selected_vertices)) {
              continue;
          }
          for (const ElementId vertex_id : face.vertices) {
              if (selected_vertices.contains(vertex_id)) {
                  boundary_vertices.insert(vertex_id);
              }
          }
      }
      for (const Edge edge : all_edges) {
          if (edge_incidence_count(incidence_counts, edge) != 0 ||
              (selected_vertices.contains(edge.a) &&
               selected_vertices.contains(edge.b))) {
              continue;
          }
          if (selected_vertices.contains(edge.a)) {
              boundary_vertices.insert(edge.a);
          }
          if (selected_vertices.contains(edge.b)) {
              boundary_vertices.insert(edge.b);
          }
      }

      std::vector<ElementId> retained;
      retained.reserve(before.vertices.size());
      ElementIdIndex retained_vertices;
      retained_vertices.reserve(before.vertices.size());
      for (const ElementId vertex_id : before.vertices) {
          if (!boundary_vertices.contains(vertex_id)) {
              add_indexed_id(retained, retained_vertices, vertex_id);
          }
      }
      selection.vertices = std::move(retained);
      break;
    }
    case SelectionMode::Edge: {
      const EdgeIndex selected_edges = make_edge_index(before.edges);
      ElementIdIndex selected_vertices;
      selected_vertices.reserve(before.edges.size() * 2U);
      for (const Edge edge : before.edges) {
          if (edge.a != kInvalidElementId) {
              selected_vertices.insert(edge.a);
          }
          if (edge.b != kInvalidElementId) {
              selected_vertices.insert(edge.b);
          }
      }

      ElementIdIndex boundary_vertices;
      for (const Face& face : document.faces) {
          if (face_all_edges_selected(face, selected_edges)) {
              continue;
          }
          for (const ElementId vertex_id : face.vertices) {
              if (selected_vertices.contains(vertex_id)) {
                  boundary_vertices.insert(vertex_id);
              }
          }
      }
      for (const Edge edge : all_edges) {
          if (edge_incidence_count(incidence_counts, edge) != 0 ||
              selected_edges.contains(indexed_edge_key(edge))) {
              continue;
          }
          if (selected_vertices.contains(edge.a)) {
              boundary_vertices.insert(edge.a);
          }
          if (selected_vertices.contains(edge.b)) {
              boundary_vertices.insert(edge.b);
          }
      }

      std::vector<Edge> retained;
      retained.reserve(before.edges.size());
      EdgeIndex retained_edges;
      retained_edges.reserve(before.edges.size());
      for (const Edge edge : before.edges) {
          if (!boundary_vertices.contains(edge.a) &&
              !boundary_vertices.contains(edge.b)) {
              add_indexed_edge(retained, retained_edges, edge);
          }
      }
      selection.edges = std::move(retained);
      break;
    }
    case SelectionMode::Face: {
      const ElementIdIndex selected_faces =
          make_element_id_index(before.faces);
      const FaceByIdIndex faces_by_id = make_face_by_id_index(document);
      ElementIdIndex unselected_vertices;
      unselected_vertices.reserve(document.vertices.size());
      for (const Face& face : document.faces) {
          if (!selected_faces.contains(face.id)) {
              unselected_vertices.insert(face.vertices.begin(),
                                         face.vertices.end());
          }
      }

      std::vector<ElementId> retained;
      retained.reserve(before.faces.size());
      for (const ElementId face_id : before.faces) {
          const auto found = faces_by_id.find(face_id);
          if (found == faces_by_id.end() || found->second == nullptr) {
              continue;
          }
          const bool boundary = std::ranges::any_of(
              found->second->vertices, [&](ElementId vertex_id) {
                  return unselected_vertices.contains(vertex_id);
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
