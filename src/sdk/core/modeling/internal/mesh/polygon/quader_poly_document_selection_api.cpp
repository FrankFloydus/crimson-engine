////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <mesh/polygon/document.hpp>

#include <mesh/polygon/internal/quader_poly_document_knife_helpers.hpp>
#include <mesh/polygon/internal/quader_poly_document_topology_helpers.hpp>
#include <mesh/polygon/internal/quader_poly_document_uv_helpers.hpp>

#include <cmath>
#include <map>
#include <optional>
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
