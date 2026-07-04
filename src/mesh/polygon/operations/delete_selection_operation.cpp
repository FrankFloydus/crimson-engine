////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <mesh/polygon/document.hpp>
#include <mesh/polygon/poly_operation.hpp>
#include <mesh/polygon/poly_operation_descriptor.hpp>

#include <mesh/polygon/internal/quader_poly_document_bridge_surface_helpers.hpp>
#include <mesh/polygon/internal/quader_poly_document_hull_plane_helpers.hpp>
#include <mesh/polygon/internal/quader_poly_document_knife_helpers.hpp>
#include <mesh/polygon/internal/quader_poly_document_topology_backend.hpp>
#include <mesh/polygon/internal/quader_poly_document_topology_helpers.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <string_view>

namespace quader_poly {

/**
 * Implements the Delete Selection Operation modeling operation for the polygon document and mesh editing core.
 */
class DeleteSelectionOperation final : public PolyOperation {
public:
    [[nodiscard]] std::string_view id() const override { return poly_operation_descriptor(PolyOperationKey::DeleteSelection).id; }
    [[nodiscard]] std::string_view label() const override { return poly_operation_descriptor(PolyOperationKey::DeleteSelection).label; }
    [[nodiscard]] OperationResult apply(Document& document, Selection& selection) const override;
};

using namespace document_internal;

namespace {
OperationElementDelta document_element_delta(const Document& document)
{
    OperationElementDelta elements;
    elements.vertices.reserve(document.vertices.size());
    elements.faces.reserve(document.faces.size());
    for (const Vertex& vertex : document.vertices) {
        elements.vertices.push_back(vertex.id);
    }
    for (const Face& face : document.faces) {
        elements.faces.push_back(face.id);
    }
    elements.edges = document_edges(document);
    return elements;
}

template <typename Element>
std::vector<Element> removed_elements(
    const std::vector<Element>& before,
    const std::vector<Element>& after)
{
    std::vector<Element> removed;
    for (const Element& element : before) {
        if (std::find(after.begin(), after.end(), element) == after.end()) {
            removed.push_back(element);
        }
    }
    return removed;
}

OperationElementDelta deleted_element_delta(
    const OperationElementDelta& before,
    const OperationElementDelta& after)
{
    OperationElementDelta deleted;
    deleted.vertices = removed_elements(before.vertices, after.vertices);
    deleted.edges = removed_elements(before.edges, after.edges);
    deleted.faces = removed_elements(before.faces, after.faces);
    return deleted;
}

OperationResult delete_selection_impl(Document& document, const Selection& selection)
{
    if (selection.empty()) {
        return { false, "No polygon elements are selected." };
    }

    const std::size_t original_face_count = document.faces.size();
    const std::size_t original_vertex_count = document.vertices.size();
    const OperationElementDelta before_elements = document_element_delta(document);

    switch (selection.mode) {
    case SelectionMode::Vertex:
      std::erase_if(document.faces, [&selection](const Face &face) {
        return face_uses_any_vertex(face, selection.vertices);
      });
      std::erase_if(document.vertices, [&selection](const Vertex &vertex) {
        return selection_contains(selection, vertex.id);
      });
      break;
    case SelectionMode::Edge:
      std::erase_if(document.faces, [&selection](const Face &face) {
        return face_uses_any_edge(face, selection.edges);
      });
      break;
    case SelectionMode::Face:
      std::erase_if(document.faces, [&selection](const Face &face) {
        return selection_contains_face(selection, face.id);
      });
      break;
    }

    prune_invalid_faces(document);
    prune_unused_vertices(document);
    const bool changed = original_face_count != document.faces.size() || original_vertex_count != document.vertices.size();
    OperationResult result;
    result.changed = changed;
    result.message = changed ? std::string {} : "No selected polygon elements were found.";
    if (changed) {
        result.deleted = deleted_element_delta(before_elements, document_element_delta(document));
    }
    return result;
}

} // namespace

OperationResult DeleteSelectionOperation::apply(Document& document, Selection& selection) const
{
    return delete_selection_impl(document, selection);
}

OperationResult delete_selection(Document& document, const Selection& selection)
{
    Selection selection_copy = selection;
    return DeleteSelectionOperation {}.apply(document, selection_copy);
}
} // namespace quader_poly
