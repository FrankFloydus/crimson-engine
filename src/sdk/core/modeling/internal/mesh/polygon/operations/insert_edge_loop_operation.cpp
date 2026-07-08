////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <mesh/polygon/document.hpp>
#include <mesh/polygon/poly_operation.hpp>
#include <mesh/polygon/poly_operation_descriptor.hpp>

#include <diagnostics/profile.hpp>

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

namespace quader_poly {

/**
 * Implements the Insert Edge Loop Operation modeling operation for the polygon document and mesh editing core.
 */
class InsertEdgeLoopOperation final : public PolyOperation {
public:
    explicit InsertEdgeLoopOperation(float factor = 0.5F);

    [[nodiscard]] std::string_view id() const override { return poly_operation_descriptor(PolyOperationKey::InsertEdgeLoop).id; }
    [[nodiscard]] std::string_view label() const override { return poly_operation_descriptor(PolyOperationKey::InsertEdgeLoop).label; }
    [[nodiscard]] OperationResult apply(Document& document, Selection& selection) const override;

private:
    float factor_ = 0.5F;
};

using namespace document_internal;

InsertEdgeLoopOperation::InsertEdgeLoopOperation(float factor)
    : factor_(factor)
{
}

OperationResult InsertEdgeLoopOperation::apply(Document& document, Selection& selection) const
{
    QDR_PROFILE_SCOPE("qdr_document.InsertEdgeLoopOperation.apply");
    if (selection.mode != SelectionMode::Edge || selection.edges.empty()) {
      return {false, "Select an edge to insert an edge loop."};
    }

    float factor = factor_;
    factor = std::clamp(factor, 0.01F, 0.99F);
    const Edge active_edge = make_edge(selection.active_edge.a, selection.active_edge.b);
    const Edge seed = edge_exists(document, active_edge) ? active_edge : make_edge(selection.edges.front().a, selection.edges.front().b);
    const std::vector<EdgeLoopFaceSplit> splits = collect_edge_loop_splits(document, seed);
    if (splits.empty()) {
        return { false, "Edge loop insertion needs at least one quad face." };
    }

    Document candidate = document;
    const std::vector<Face> original_faces = candidate.faces;
    std::map<std::pair<ElementId, ElementId>, ElementId> split_vertices;
    std::vector<Face> split_faces;
    std::vector<Edge> inserted_edges;
    split_faces.reserve(splits.size() * 2U);
    inserted_edges.reserve(splits.size());

    for (const EdgeLoopFaceSplit& split : splits) {
        const Face* face = find_face_copy(original_faces, split.face_id);
        if (face == nullptr || face->vertices.size() != 4) {
            continue;
        }

        const std::optional<std::size_t> edge_index = face_edge_index(*face, split.entry_edge);
        if (!edge_index.has_value()) {
            continue;
        }

        const std::size_t i0 = *edge_index;
        const std::size_t i1 = (i0 + 1U) % 4U;
        const std::size_t i2 = (i0 + 2U) % 4U;
        const std::size_t i3 = (i0 + 3U) % 4U;
        const Edge face_entry = directed_face_edge(*face, i0);
        const Edge entry_edge = same_directed_edge(split.entry_edge, face_entry) ? face_entry : Edge { face->vertices[i1], face->vertices[i0] };
        const Edge opposite_edge = oriented_loop_opposite_edge(*face, i0, entry_edge);
        const ElementId entry_vertex = split_vertex_for_edge(candidate, split_vertices, entry_edge, factor);
        const ElementId opposite_vertex = split_vertex_for_edge(candidate, split_vertices, opposite_edge, factor);
        if (entry_vertex == kInvalidElementId ||
            opposite_vertex == kInvalidElementId) {
          continue;
        }

        Face first_split_face;
        first_split_face.id = face->id;
        first_split_face.vertices = { face->vertices[i1], face->vertices[i2], opposite_vertex, entry_vertex };
        first_split_face.material_slot = face->material_slot;
        first_split_face.uvs.clear();
        split_faces.push_back(std::move(first_split_face));

        Face second_split_face;
        second_split_face.id = candidate.next_face_id++;
        second_split_face.vertices = { face->vertices[i0], entry_vertex, opposite_vertex, face->vertices[i3] };
        second_split_face.material_slot = face->material_slot;
        second_split_face.uvs.clear();
        split_faces.push_back(std::move(second_split_face));
        inserted_edges.push_back(make_edge(entry_vertex, opposite_vertex));
    }

    if (split_faces.empty()) {
        return { false, "No valid quad faces were found for edge loop insertion." };
    }

    const std::set<ElementId> split_face_ids = [&splits]() {
        std::set<ElementId> ids;
        for (const EdgeLoopFaceSplit& split : splits) {
            ids.insert(split.face_id);
        }
        return ids;
    }();

    std::erase_if(candidate.faces, [&split_face_ids](const Face& face) {
        return split_face_ids.contains(face.id);
    });
    candidate.faces.insert(candidate.faces.end(), split_faces.begin(), split_faces.end());
    prune_invalid_faces(candidate);
    prune_unused_vertices(candidate);
    restore_source_face_orientation(document, candidate);
    if (!every_face_triangulates(candidate)) {
        return { false, "Edge loop insertion would create invalid face geometry." };
    }

    document = std::move(candidate);
    selection.clear();
    selection.mode = SelectionMode::Edge;
    selection.edges = std::move(inserted_edges);
    activate_last_selection(selection);
    return { true, {} };
}

OperationResult insert_edge_loop(Document& document, Selection& selection, float factor)
{
    QDR_PROFILE_SCOPE("qdr_document.insert_edge_loop");
    return InsertEdgeLoopOperation(factor).apply(document, selection);
}

} // namespace quader_poly
