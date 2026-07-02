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

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <limits>
#include <map>

#include <optional>
#include <set>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace quader_poly {

using namespace document_internal;

namespace {

/**
 * Stores the Bridge Pair Info data contract used by the polygon document and mesh editing core.
 */
struct BridgePairInfo {
    OpenBridgeEdgeInfo first;
    OpenBridgeEdgeInfo second;
};

OperationResult bridge_edge_pairs_impl(Document& document, Selection& selection, const std::vector<std::pair<Edge, Edge>>& edge_pairs, int steps);

quader::QVec3 edge_midpoint_or_zero(const Document& document, Edge edge)
{
    const Vertex* first = find_vertex(document, edge.a);
    const Vertex* second = find_vertex(document, edge.b);
    if (first == nullptr || second == nullptr) {
        return {};
    }
    return (first->position + second->position) * 0.5F;
}

std::vector<OpenBridgeEdgeInfo> bridge_edge_infos(
    const Document& document,
    Edge edge,
    const std::map<std::pair<ElementId, ElementId>, int>& incidence_counts)
{
    edge = make_edge(edge.a, edge.b);
    if (edge.a == kInvalidElementId || edge.b == kInvalidElementId ||
        edge.a == edge.b) {
      return {};
    }
    const auto count = incidence_counts.find({ edge.a, edge.b });
    if (count == incidence_counts.end() || count->second < 1) {
        return {};
    }

    std::vector<OpenBridgeEdgeInfo> infos;
    for (const Face& face : document.faces) {
        const std::optional<std::pair<ElementId, ElementId>> oriented = oriented_edge_in_face(face, edge);
        if (oriented.has_value()) {
            infos.push_back({ edge, *oriented, face_normal(document, face), face.material_slot });
        }
    }
    return infos;
}

float bridge_edge_info_gap_score(const Document& document, const OpenBridgeEdgeInfo& info, quader::QVec3 toward_other_edge)
{
    const quader::QVec3 toward = normalize_or_zero(toward_other_edge);
    if (length_squared(toward) <= kEpsilon) {
      return 0.0F;
    }

    float score = -std::numeric_limits<float>::infinity();
    const quader::QVec3 normal = normalize_or_zero(info.face_normal);
    if (length_squared(normal) > kEpsilon) {
      score = std::max(score, dot(normal, toward));
    }
    const quader::QVec3 edge_outward = bridge_edge_outward_direction(document, info);
    if (length_squared(edge_outward) > kEpsilon) {
      score = std::max(score, dot(edge_outward, toward));
    }
    return std::isfinite(score) ? score : 0.0F;
}

float bridge_loop_rail_score(const Document& document, std::span<const ElementId> loop, Edge first_edge, Edge second_edge)
{
    if (loop.size() < 3U) {
        return std::numeric_limits<float>::infinity();
    }

    float score = 0.0F;
    for (std::size_t index = 0; index < loop.size(); ++index) {
        const Edge loop_edge = make_edge(loop[index], loop[(index + 1U) % loop.size()]);
        if (loop_edge == first_edge || loop_edge == second_edge) {
            continue;
        }
        const Vertex* first = find_vertex(document, loop_edge.a);
        const Vertex* second = find_vertex(document, loop_edge.b);
        if (first == nullptr || second == nullptr) {
            return std::numeric_limits<float>::infinity();
        }
        score += length_squared(first->position - second->position);
    }
    return score;
}

std::optional<BridgePairInfo> bridge_edge_info_pair(
    const Document& document,
    Edge first_edge,
    Edge second_edge,
    const std::map<std::pair<ElementId, ElementId>, int>& incidence_counts)
{
    const std::vector<OpenBridgeEdgeInfo> first_infos = bridge_edge_infos(document, first_edge, incidence_counts);
    const std::vector<OpenBridgeEdgeInfo> second_infos = bridge_edge_infos(document, second_edge, incidence_counts);
    if (first_infos.empty() || second_infos.empty()) {
        return std::nullopt;
    }

    const quader::QVec3 first_to_second = edge_midpoint_or_zero(document, second_edge) - edge_midpoint_or_zero(document, first_edge);
    const quader::QVec3 second_to_first = first_to_second * -1.0F;
    std::optional<BridgePairInfo> best_pair;
    float best_rail_score = std::numeric_limits<float>::infinity();
    float best_gap_score = -std::numeric_limits<float>::infinity();

    for (const OpenBridgeEdgeInfo& first_info : first_infos) {
        for (const OpenBridgeEdgeInfo& second_info : second_infos) {
            const std::optional<std::vector<ElementId>> loop = bridge_face_loop_from_open_edges(document, first_info, second_info);
            if (!loop.has_value()) {
                continue;
            }

            const float rail_score = bridge_loop_rail_score(document, *loop, first_info.edge, second_info.edge);
            if (!std::isfinite(rail_score)) {
                continue;
            }
            const float gap_score =
                bridge_edge_info_gap_score(document, first_info, first_to_second) +
                bridge_edge_info_gap_score(document, second_info, second_to_first);
            if (!best_pair.has_value() ||
                rail_score < best_rail_score - kEpsilon ||
                (std::abs(rail_score - best_rail_score) <= kEpsilon &&
                 gap_score > best_gap_score + kEpsilon)) {
              best_pair = BridgePairInfo{first_info, second_info};
              best_rail_score = rail_score;
              best_gap_score = gap_score;
            }
        }
    }
    return best_pair;
}

/**
 * Implements the Bridge Edge Pairs Operation modeling operation for the polygon document and mesh editing core.
 */
class BridgeEdgePairsOperation final : public PolyOperation {
public:
    BridgeEdgePairsOperation(std::vector<std::pair<Edge, Edge>> edge_pairs, int steps = 1);

    [[nodiscard]] std::string_view id() const override { return poly_operation_descriptor(PolyOperationKey::BridgeEdgePairs).id; }
    [[nodiscard]] std::string_view label() const override { return poly_operation_descriptor(PolyOperationKey::BridgeEdgePairs).label; }
    [[nodiscard]] OperationResult apply(Document& document, Selection& selection) const override;

private:
    std::vector<std::pair<Edge, Edge>> edge_pairs_;
    int steps_ = 1;
};

OperationResult bridge_edge_pairs_impl(Document& document, Selection& selection, const std::vector<std::pair<Edge, Edge>>& edge_pairs, int steps)
{
    if (edge_pairs.empty()) {
        return { false, "Bridge needs one or more selected edge pairs." };
    }

    const std::map<std::pair<ElementId, ElementId>, int> incidence_counts = edge_incidence_counts(document);
    std::vector<BridgePairInfo> pair_infos;
    pair_infos.reserve(edge_pairs.size());
    std::set<std::pair<ElementId, ElementId>> used_edges;
    for (const std::pair<Edge, Edge>& edge_pair : edge_pairs) {
        const Edge first = make_edge(edge_pair.first.a, edge_pair.first.b);
        const Edge second = make_edge(edge_pair.second.a, edge_pair.second.b);
        if (first.a == kInvalidElementId || first.b == kInvalidElementId ||
            first.a == first.b || second.a == kInvalidElementId ||
            second.b == kInvalidElementId || second.a == second.b ||
            find_vertex(document, first.a) == nullptr ||
            find_vertex(document, first.b) == nullptr ||
            find_vertex(document, second.a) == nullptr ||
            find_vertex(document, second.b) == nullptr || first == second) {
          return {false, "Bridge needs valid selected edge pairs."};
        }
        if (!used_edges.insert({ first.a, first.b }).second || !used_edges.insert({ second.a, second.b }).second) {
            return { false, "Bridge cannot reuse the same edge in multiple pairs." };
        }

        const std::optional<BridgePairInfo> pair_info = bridge_edge_info_pair(document, first, second, incidence_counts);
        if (!pair_info.has_value()) {
            return { false, "Bridge needs selected edges that belong to faces." };
        }
        pair_infos.push_back(*pair_info);
    }

    const int step_count = std::clamp(steps, 1, 64);
    Document candidate = document;
    std::vector<ElementId> bridged_face_ids;
    bridged_face_ids.reserve(edge_pairs.size() * static_cast<std::size_t>(step_count));
    std::map<BridgeRailStepKey, ElementId> rail_step_vertices;

    auto append_bridge_face = [&](std::vector<ElementId> vertices, std::uint32_t material_slot) -> bool {
        vertices = unique_valid_face_loop(std::move(vertices));
        if (vertices.size() < 3 || vertices.size() > 4) {
            return false;
        }
        Face bridge_face;
        bridge_face.id = candidate.next_face_id++;
        bridge_face.vertices = std::move(vertices);
        bridge_face.material_slot = material_slot;
        if (triangulate_face_local_indices(candidate, bridge_face).empty()) {
            return false;
        }
        bridged_face_ids.push_back(bridge_face.id);
        candidate.faces.push_back(std::move(bridge_face));
        return true;
    };

    auto interpolated_vertex = [&](ElementId start_id, ElementId end_id, quader::QVec3 first_outward, quader::QVec3 second_outward, int step_index) -> ElementId {
        if (step_index <= 0 || start_id == end_id) {
            return start_id;
        }
        if (step_index >= step_count) {
            return end_id;
        }

        ElementId key_first = start_id;
        ElementId key_second = end_id;
        int key_step = step_index;
        if (key_second < key_first) {
            std::swap(key_first, key_second);
            key_step = step_count - step_index;
        }
        const BridgeRailStepKey key { key_first, key_second, key_step };
        const auto found = rail_step_vertices.find(key);
        if (found != rail_step_vertices.end()) {
            return found->second;
        }

        const Vertex* start = find_vertex(document, start_id);
        const Vertex* end = find_vertex(document, end_id);
        if (start == nullptr || end == nullptr) {
          return kInvalidElementId;
        }
        const ElementId vertex_id = add_vertex(candidate, curved_bridge_position(start->position, end->position, first_outward, second_outward, step_index, step_count));
        rail_step_vertices.emplace(key, vertex_id);
        return vertex_id;
    };

    for (const BridgePairInfo& pair_info : pair_infos) {
        if (step_count == 1) {
            const std::optional<std::vector<ElementId>> bridge_loop = bridge_face_loop_from_open_edges(document, pair_info.first, pair_info.second);
            if (!bridge_loop.has_value() || !append_bridge_face(*bridge_loop, pair_info.first.material_slot)) {
                return { false, "Bridge could not create a valid triangle or quad from the selected edges." };
            }
        } else {
            const ElementId first_a = pair_info.first.oriented_face_edge.second;
            const ElementId first_b = pair_info.first.oriented_face_edge.first;
            const ElementId second_a = pair_info.second.oriented_face_edge.second;
            const ElementId second_b = pair_info.second.oriented_face_edge.first;
            const std::vector<ElementId> unique_vertices = unique_bridge_vertices(pair_info.first.edge, pair_info.second.edge);
            ElementId second_for_a = second_b;
            ElementId second_for_b = second_a;
            if (unique_vertices.size() == 4) {
                const float crossed_score =
                    length_squared(vertex_position_or_zero(document, first_a) - vertex_position_or_zero(document, second_b)) +
                    length_squared(vertex_position_or_zero(document, first_b) - vertex_position_or_zero(document, second_a));
                const float aligned_score =
                    length_squared(vertex_position_or_zero(document, first_a) - vertex_position_or_zero(document, second_a)) +
                    length_squared(vertex_position_or_zero(document, first_b) - vertex_position_or_zero(document, second_b));
                if (aligned_score < crossed_score) {
                    second_for_a = second_a;
                    second_for_b = second_b;
                }
            } else if (unique_vertices.size() == 3 && first_a != second_b && first_b != second_a) {
                if (first_a == second_a || first_b == second_b) {
                    second_for_a = second_a;
                    second_for_b = second_b;
                }
            }
            const bool rail_a_shared = first_a == second_for_a;
            const bool rail_b_shared = first_b == second_for_b;
            if (unique_vertices.size() == 3 && !rail_a_shared && !rail_b_shared) {
                return { false, "Bridge could not find a consistent triangle fan between the selected edges." };
            }
            if (unique_vertices.size() != 3 && unique_vertices.size() != 4) {
                return { false, "Bridge needs selected edges that share one vertex or no vertices." };
            }

            const quader::QVec3 first_outward = bridge_edge_outward_direction(document, pair_info.first);
            const quader::QVec3 second_outward = bridge_edge_outward_direction(document, pair_info.second);
            ElementId previous_a = first_a;
            ElementId previous_b = first_b;
            for (int step_index = 1; step_index <= step_count; ++step_index) {
                const ElementId current_a = rail_a_shared ? first_a : interpolated_vertex(first_a, second_for_a, first_outward, second_outward, step_index);
                const ElementId current_b = rail_b_shared ? first_b : interpolated_vertex(first_b, second_for_b, first_outward, second_outward, step_index);
                if (current_a == kInvalidElementId ||
                    current_b == kInvalidElementId) {
                  return {
                      false,
                      "Bridge could not create intermediate step vertices."};
                }

                if (rail_a_shared) {
                    if (!append_bridge_face({ first_a, previous_b, current_b }, pair_info.first.material_slot)) {
                        return { false, "Bridge could not create a valid stepped triangle." };
                    }
                } else if (rail_b_shared) {
                    if (!append_bridge_face({ previous_a, first_b, current_a }, pair_info.first.material_slot)) {
                        return { false, "Bridge could not create a valid stepped triangle." };
                    }
                } else {
                    if (!append_bridge_face({ previous_a, previous_b, current_b, current_a }, pair_info.first.material_slot)) {
                        return { false, "Bridge could not create a valid stepped quad." };
                    }
                }

                previous_a = current_a;
                previous_b = current_b;
            }
        }
    }

    if (bridged_face_ids.empty() || !every_face_triangulates(candidate)) {
        return { false, "Bridge would create invalid face geometry." };
    }

    document = std::move(candidate);
    selection.clear();
    selection.mode = SelectionMode::Face;
    selection.faces = std::move(bridged_face_ids);
    activate_face_selection(selection, selection.faces.back());
    return { true, {} };
}

BridgeEdgePairsOperation::BridgeEdgePairsOperation(std::vector<std::pair<Edge, Edge>> edge_pairs, int steps) :
    edge_pairs_(std::move(edge_pairs)),
    steps_(steps)
{}

OperationResult BridgeEdgePairsOperation::apply(Document& document, Selection& selection) const
{
    return bridge_edge_pairs_impl(document, selection, edge_pairs_, steps_);
}

} // namespace

OperationResult bridge_edge_pairs(Document& document, Selection& selection, const std::vector<std::pair<Edge, Edge>>& edge_pairs, int steps)
{
    return BridgeEdgePairsOperation { edge_pairs, steps }.apply(document, selection);
}
} // namespace quader_poly
