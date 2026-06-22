////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <mesh/polygon/document.h>
#include <mesh/polygon/poly_operation.h>

#include <mesh/polygon/internal/quader_poly_document_bridge_surface_helpers.h>
#include <mesh/polygon/internal/quader_poly_document_hull_plane_helpers.h>
#include <mesh/polygon/internal/quader_poly_document_knife_helpers.h>
#include <mesh/polygon/internal/quader_poly_document_topology_backend.h>
#include <mesh/polygon/internal/quader_poly_document_topology_helpers.h>

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
    [[nodiscard]] std::string_view id() const override { return "delete_selection"; }
    [[nodiscard]] std::string_view label() const override { return "Delete Selection"; }
    [[nodiscard]] OperationResult apply(Document& document, Selection& selection) const override;
};

using namespace document_internal;

namespace {
OperationResult delete_selection_impl(Document& document, const Selection& selection)
{
    if (selection.empty()) {
        return { false, "No polygon elements are selected." };
    }

    const std::size_t original_face_count = document.faces.size();
    const std::size_t original_vertex_count = document.vertices.size();

    switch (selection.mode) {
    case SelectionMode::vertex:
        std::erase_if(document.faces, [&selection](const Face& face) {
            return face_uses_any_vertex(face, selection.vertices);
        });
        std::erase_if(document.vertices, [&selection](const Vertex& vertex) {
            return selection_contains(selection, vertex.id);
        });
        break;
    case SelectionMode::edge:
        std::erase_if(document.faces, [&selection](const Face& face) {
            return face_uses_any_edge(face, selection.edges);
        });
        break;
    case SelectionMode::face:
        std::erase_if(document.faces, [&selection](const Face& face) {
            return selection_contains_face(selection, face.id);
        });
        break;
    }

    prune_invalid_faces(document);
    prune_unused_vertices(document);
    const bool changed = original_face_count != document.faces.size() || original_vertex_count != document.vertices.size();
    return { changed, changed ? std::string {} : "No selected polygon elements were found." };
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