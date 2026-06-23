////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <mesh/polygon/document.h>

#include <mesh/polygon/internal/quader_poly_document_knife_helpers.h>
#include <mesh/polygon/internal/quader_poly_document_topology_helpers.h>
#include <mesh/polygon/internal/quader_poly_document_uv_helpers.h>

#include <vector>

namespace quader_poly {

using namespace document_internal;

void select_only(Selection& selection, const PickResult& pick)
{
    selection.clear();
    if (!pick.hit) {
        return;
    }

    switch (pick.kind) {
    case ElementKind::vertex:
        selection.mode = SelectionMode::vertex;
        selection.vertices.push_back(pick.vertex_id);
        activate_pick_selection(selection, pick);
        break;
    case ElementKind::edge:
        selection.mode = SelectionMode::edge;
        selection.edges.push_back(make_edge(pick.edge.a, pick.edge.b));
        activate_pick_selection(selection, pick);
        break;
    case ElementKind::face:
        selection.mode = SelectionMode::face;
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
    case ElementKind::vertex:
        if (!contains_id(selection.vertices, pick.vertex_id)) {
            selection.vertices.push_back(pick.vertex_id);
        }
        activate_pick_selection(selection, pick);
        break;
    case ElementKind::edge:
        if (!contains_edge(selection.edges, pick.edge)) {
            selection.edges.push_back(make_edge(pick.edge.a, pick.edge.b));
        }
        activate_pick_selection(selection, pick);
        break;
    case ElementKind::face:
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
    case ElementKind::vertex:
        removed = remove_id(selection.vertices, pick.vertex_id);
        break;
    case ElementKind::edge:
        removed = remove_edge(selection.edges, pick.edge);
        break;
    case ElementKind::face:
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
    case SelectionMode::vertex:
        for (const ElementId vertex_id : source_vertices) {
            if (find_vertex(document, vertex_id) != nullptr) {
                add_unique_id(selection.vertices, vertex_id);
            }
        }
        break;

    case SelectionMode::edge:
        if (source.mode == SelectionMode::face) {
            for (const ElementId face_id : source.faces) {
                const Face* face = find_face(document, face_id);
                if (face == nullptr) {
                    continue;
                }
                for (const Edge& edge : face_edges(*face)) {
                    add_unique_edge(selection.edges, edge);
                }
            }
        } else {
            for (const Edge& edge : document_edges(document)) {
                if (contains_id(source_vertices, edge.a) && contains_id(source_vertices, edge.b)) {
                    add_unique_edge(selection.edges, edge);
                }
            }
        }
        break;

    case SelectionMode::face:
        if (source.mode == SelectionMode::edge) {
            for (const Face& face : document.faces) {
                const std::vector<Edge> edges = face_edges(face);
                if (!edges.empty() && std::ranges::all_of(edges, [&source](Edge edge) {
                        return selection_contains(source, edge);
                    })) {
                    add_unique_id(selection.faces, face.id);
                }
            }
        } else {
            for (const Face& face : document.faces) {
                if (!face.vertices.empty() && std::ranges::all_of(face.vertices, [&source_vertices](ElementId vertex_id) {
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
    const Edge seed = make_edge(seed_edge.a, seed_edge.b);
    if (seed.a == invalid_element_id || seed.b == invalid_element_id || seed.a == seed.b || !edge_exists(document, seed)) {
        return {};
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

    if (selection.mode != SelectionMode::edge) {
        selection.clear();
        selection.mode = SelectionMode::edge;
    }

    if (!toggle) {
        selection.clear();
        selection.mode = SelectionMode::edge;
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
