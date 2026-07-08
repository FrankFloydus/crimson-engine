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
#include <mesh/polygon/internal/quader_poly_document_mesh_internal.hpp>
#include <mesh/polygon/internal/quader_poly_document_topology_backend.hpp>
#include <mesh/polygon/internal/quader_poly_document_topology_helpers.hpp>
#include <mesh/polygon/internal/quader_poly_document_uv_helpers.hpp>

#include <array>
#include <cmath>
#include <limits>

#include <string_view>
#include <utility>

namespace quader_poly {

using namespace document_internal;

namespace {

OperationResult bridge_edge_boundaries_impl(
    Document& document,
    Selection& selection,
    std::span<const ElementId> first_vertices,
    std::span<const ElementId> second_vertices,
    bool closed,
    int steps);

std::vector<ElementId> bridge_reoriented_boundary_vertices(
    std::span<const ElementId> vertices,
    bool closed,
    bool reversed)
{
    std::vector<ElementId> result;
    result.reserve(vertices.size());
    if (!reversed) {
        result.assign(vertices.begin(), vertices.end());
        return result;
    }

    if (!closed) {
        result.assign(vertices.rbegin(), vertices.rend());
        return result;
    }

    if (vertices.empty()) {
        return result;
    }
    result.push_back(vertices.front());
    for (std::size_t index = vertices.size() - 1U; index > 0U; --index) {
        result.push_back(vertices[index]);
    }
    return result;
}

int bridge_boundary_winding_score(
    const Document& document,
    std::span<const ElementId> first_vertices,
    std::span<const ElementId> second_vertices,
    bool closed)
{
    if (first_vertices.size() != second_vertices.size()) {
        return std::numeric_limits<int>::min();
    }
    const std::size_t vertex_count = first_vertices.size();
    const std::size_t edge_count = closed ? vertex_count : (vertex_count > 0U ? vertex_count - 1U : 0U);
    if (edge_count == 0U) {
        return std::numeric_limits<int>::min();
    }

    int score = 0;
    for (std::size_t edge_index = 0; edge_index < edge_count; ++edge_index) {
        const std::size_t next_index = (edge_index + 1U) % vertex_count;
        const std::array<ElementId, 4> quad {
            first_vertices[edge_index],
            first_vertices[next_index],
            second_vertices[next_index],
            second_vertices[edge_index],
        };
        for (std::size_t vertex_index = 0; vertex_index < quad.size(); ++vertex_index) {
            const ElementId from_id = quad[vertex_index];
            const ElementId to_id = quad[(vertex_index + 1U) % quad.size()];
            if (from_id == kInvalidElementId || to_id == kInvalidElementId || from_id == to_id) {
                continue;
            }
            for (const Face& adjacent_face : document.faces) {
                if (directed_face_edge_index(adjacent_face, from_id, to_id).has_value()) {
                    --score;
                }
                if (directed_face_edge_index(adjacent_face, to_id, from_id).has_value()) {
                    ++score;
                }
            }
        }
    }
    return score;
}

std::pair<std::vector<ElementId>, std::vector<ElementId>>
bridge_winding_aligned_boundaries(
    const Document& document,
    std::span<const ElementId> first_vertices,
    std::span<const ElementId> second_vertices,
    bool closed)
{
    std::vector<ElementId> best_first(first_vertices.begin(), first_vertices.end());
    std::vector<ElementId> best_second(second_vertices.begin(), second_vertices.end());
    int best_score = bridge_boundary_winding_score(document, best_first, best_second, closed);
    for (const bool reverse_first : {false, true}) {
        for (const bool reverse_second : {false, true}) {
            if (!reverse_first && !reverse_second) {
                continue;
            }
            std::vector<ElementId> candidate_first =
                bridge_reoriented_boundary_vertices(first_vertices, closed, reverse_first);
            std::vector<ElementId> candidate_second =
                bridge_reoriented_boundary_vertices(second_vertices, closed, reverse_second);
            const int score = bridge_boundary_winding_score(
                document, candidate_first, candidate_second, closed);
            if (score > best_score) {
                best_score = score;
                best_first = std::move(candidate_first);
                best_second = std::move(candidate_second);
            }
        }
    }
    return {std::move(best_first), std::move(best_second)};
}

/**
 * Implements the Bridge Edge Boundaries Operation modeling operation for the polygon document and mesh editing core.
 */
class BridgeEdgeBoundariesOperation final : public PolyOperation {
public:
    BridgeEdgeBoundariesOperation(
        std::span<const ElementId> first_vertices,
        std::span<const ElementId> second_vertices,
        bool closed,
        int steps = 1);

    [[nodiscard]] std::string_view id() const override { return poly_operation_descriptor(PolyOperationKey::BridgeEdgeBoundaries).id; }
    [[nodiscard]] std::string_view label() const override { return poly_operation_descriptor(PolyOperationKey::BridgeEdgeBoundaries).label; }
    [[nodiscard]] OperationResult apply(Document& document, Selection& selection) const override;

private:
    std::vector<ElementId> first_vertices_;
    std::vector<ElementId> second_vertices_;
    bool closed_ = false;
    int steps_ = 1;
};

OperationResult bridge_edge_boundaries_impl(
    Document& document,
    Selection& selection,
    std::span<const ElementId> first_vertices,
    std::span<const ElementId> second_vertices,
    bool closed,
    int steps)
{
    if (first_vertices.size() != second_vertices.size()) {
        return { false, "Bridge needs matching boundary vertex counts." };
    }
    const std::size_t vertex_count = first_vertices.size();
    const std::size_t edge_count = closed ? vertex_count : (vertex_count > 0U ? vertex_count - 1U : 0U);
    if (vertex_count < 2U || edge_count == 0U || (!closed && vertex_count < 2U)) {
        return { false, "Bridge needs two valid boundary edge chains." };
    }

    auto [oriented_first_vertices, oriented_second_vertices] =
        bridge_winding_aligned_boundaries(document, first_vertices, second_vertices, closed);

    const std::map<std::pair<ElementId, ElementId>, int> incidence_counts = edge_incidence_counts(document);
    std::vector<quader::QVec3> first_outwards(vertex_count);
    std::vector<quader::QVec3> second_outwards(vertex_count);
    std::vector<std::uint32_t> material_slots(edge_count, 0U);
    std::set<std::pair<ElementId, ElementId>> used_edges;

    auto append_outward = [](std::vector<quader::QVec3>& outwards, std::size_t index, quader::QVec3 outward) {
        if (index < outwards.size()) {
            outwards[index] += outward;
        }
    };

    for (std::size_t edge_index = 0; edge_index < edge_count; ++edge_index) {
        const std::size_t next_index = (edge_index + 1U) % vertex_count;
        const Edge first_edge = make_edge(oriented_first_vertices[edge_index], oriented_first_vertices[next_index]);
        const Edge second_edge = make_edge(oriented_second_vertices[edge_index], oriented_second_vertices[next_index]);
        if (first_edge.a == kInvalidElementId ||
            first_edge.b == kInvalidElementId ||
            second_edge.a == kInvalidElementId ||
            second_edge.b == kInvalidElementId ||
            first_edge.a == first_edge.b || second_edge.a == second_edge.b ||
            find_vertex(document, first_edge.a) == nullptr ||
            find_vertex(document, first_edge.b) == nullptr ||
            find_vertex(document, second_edge.a) == nullptr ||
            find_vertex(document, second_edge.b) == nullptr) {
          return {false, "Bridge needs valid boundary vertices."};
        }
        if (!used_edges.insert({ first_edge.a, first_edge.b }).second || !used_edges.insert({ second_edge.a, second_edge.b }).second) {
            return { false, "Bridge cannot reuse the same edge in multiple boundary segments." };
        }

        const std::optional<OpenBridgeEdgeInfo> first_info = open_bridge_edge_info(document, first_edge, incidence_counts);
        const std::optional<OpenBridgeEdgeInfo> second_info = open_bridge_edge_info(document, second_edge, incidence_counts);
        if (!first_info.has_value() || !second_info.has_value()) {
            return { false, "Bridge needs selected edges that belong to faces." };
        }

        const quader::QVec3 first_outward = bridge_edge_outward_direction(document, *first_info);
        const quader::QVec3 second_outward = bridge_edge_outward_direction(document, *second_info);
        append_outward(first_outwards, edge_index, first_outward);
        append_outward(first_outwards, next_index, first_outward);
        append_outward(second_outwards, edge_index, second_outward);
        append_outward(second_outwards, next_index, second_outward);
        material_slots[edge_index] = first_info->material_slot;
    }

    for (quader::QVec3& outward : first_outwards) {
        outward = normalize_or_zero(outward);
    }
    for (quader::QVec3& outward : second_outwards) {
        outward = normalize_or_zero(outward);
    }

    const int step_count = std::clamp(steps, 1, 64);
    Document candidate = document;
    std::vector<std::vector<ElementId>> rings;
    rings.reserve(static_cast<std::size_t>(step_count + 1));
    rings.push_back(oriented_first_vertices);
    for (int step_index = 1; step_index < step_count; ++step_index) {
        std::vector<ElementId> ring;
        ring.reserve(vertex_count);
        for (std::size_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
            const Vertex* first_vertex = find_vertex(document, oriented_first_vertices[vertex_index]);
            const Vertex* second_vertex = find_vertex(document, oriented_second_vertices[vertex_index]);
            if (first_vertex == nullptr || second_vertex == nullptr) {
                return { false, "Bridge could not find boundary vertices." };
            }
            ring.push_back(add_vertex(candidate, curved_bridge_position(first_vertex->position, second_vertex->position, first_outwards[vertex_index], second_outwards[vertex_index], step_index, step_count)));
        }
        rings.push_back(std::move(ring));
    }
    rings.push_back(oriented_second_vertices);

    std::vector<ElementId> bridge_face_ids;
    bridge_face_ids.reserve(edge_count * static_cast<std::size_t>(step_count));
    for (std::size_t ring_index = 0; ring_index + 1U < rings.size(); ++ring_index) {
        for (std::size_t edge_index = 0; edge_index < edge_count; ++edge_index) {
            const std::size_t next_index = (edge_index + 1U) % vertex_count;
            std::vector<ElementId> quad {
                rings[ring_index][edge_index],
                rings[ring_index][next_index],
                rings[ring_index + 1U][next_index],
                rings[ring_index + 1U][edge_index],
            };
            Face expected_face;
            expected_face.vertices = quad;
            const quader::QVec3 expected_normal = face_normal(candidate, expected_face);
            if (!append_bridge_face(candidate, std::move(quad), material_slots[edge_index], expected_normal, bridge_face_ids)) {
                return { false, "Bridge could not create a valid stepped boundary face." };
            }
        }
    }

    prune_invalid_faces(candidate);
    if (bridge_face_ids.empty() || !every_face_triangulates(candidate)) {
        return { false, "Bridge would create invalid boundary geometry." };
    }

    document = std::move(candidate);
    selection.clear();
    selection.mode = SelectionMode::Face;
    selection.faces = std::move(bridge_face_ids);
    activate_face_selection(selection, selection.faces.back());
    return { true, {} };
}

BridgeEdgeBoundariesOperation::BridgeEdgeBoundariesOperation(
    std::span<const ElementId> first_vertices,
    std::span<const ElementId> second_vertices,
    bool closed,
    int steps) :
    first_vertices_(first_vertices.begin(), first_vertices.end()),
    second_vertices_(second_vertices.begin(), second_vertices.end()),
    closed_(closed),
    steps_(steps)
{}

OperationResult BridgeEdgeBoundariesOperation::apply(Document& document, Selection& selection) const
{
    return bridge_edge_boundaries_impl(document, selection, first_vertices_, second_vertices_, closed_, steps_);
}

} // namespace

OperationResult bridge_edge_boundaries(
    Document& document,
    Selection& selection,
    std::span<const ElementId> first_vertices,
    std::span<const ElementId> second_vertices,
    bool closed,
    int steps)
{
    return BridgeEdgeBoundariesOperation { first_vertices, second_vertices, closed, steps }.apply(document, selection);
}
} // namespace quader_poly
