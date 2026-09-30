////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <quader/modeling/neutral/polygon/document.hpp>
#include <quader/modeling/neutral/polygon/poly_operation.hpp>
#include <quader/modeling/neutral/polygon/poly_operation_descriptor.hpp>
#include <quader/modeling/neutral/polygon/document_texture.hpp>

#include <quader/modeling/neutral/polygon/internal/quader_poly_document_bridge_surface_helpers.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_hull_plane_helpers.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_knife_helpers.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_mesh_internal.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_topology_backend.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_topology_helpers.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_uv_helpers.hpp>

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
            infos.push_back({ edge, *oriented, face_normal(document, face), face.material_slot, face.texture_transform });
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
        return OperationResult::failure("Bridge needs one or more selected edge pairs.");
    }

    const OperationResult source_validation = validate_authored_operation_candidate(document, document);
    if (!source_validation.success) {
        return source_validation;
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
          return OperationResult::failure("Bridge needs valid selected edge pairs.");
        }
        if (!used_edges.insert({ first.a, first.b }).second || !used_edges.insert({ second.a, second.b }).second) {
            return OperationResult::failure("Bridge cannot reuse the same edge in multiple pairs.");
        }
        const auto first_incidence = incidence_counts.find({first.a, first.b});
        const auto second_incidence = incidence_counts.find({second.a, second.b});
        if (first_incidence == incidence_counts.end() || first_incidence->second != 1 ||
            second_incidence == incidence_counts.end() || second_incidence->second != 1) {
            return OperationResult::failure("Bridge needs open boundary edges with exactly one adjacent face.");
        }

        const std::optional<BridgePairInfo> pair_info = bridge_edge_info_pair(document, first, second, incidence_counts);
        if (!pair_info.has_value()) {
            return OperationResult::failure("Bridge needs selected edges that belong to faces.");
        }
        pair_infos.push_back(*pair_info);
    }

    const int step_count = std::clamp(steps, 1, 64);
    Document candidate = document;
    std::vector<ElementId> bridged_face_ids;
    bridged_face_ids.reserve(edge_pairs.size() * static_cast<std::size_t>(step_count));
    std::map<BridgeRailStepKey, ElementId> rail_step_vertices;

    auto append_bridge_face = [&](std::vector<ElementId> vertices, const OpenBridgeEdgeInfo& source, const OpenBridgeEdgeInfo& other,
                                  const std::map<ElementId, quader::QVec2>* rail_uvs = nullptr) -> bool {
        vertices = unique_valid_face_loop(std::move(vertices));
        if (vertices.size() < 3 || vertices.size() > 4) {
            return false;
        }
        Face bridge_face;
        bridge_face.id = candidate.next_face_id++;
        bridge_face.vertices = std::move(vertices);
        bridge_face.material_slot = source.material_slot;
        bridge_face.texture_transform = source.texture_transform;
        const auto source_face = std::ranges::find_if(document.faces, [&](const Face& face) {
            return oriented_edge_in_face(face, source.edge).has_value();
        });
        const auto other_face = std::ranges::find_if(document.faces, [&](const Face& face) {
            return oriented_edge_in_face(face, other.edge).has_value();
        });
        if (source_face == document.faces.end() || other_face == document.faces.end()) {
            return false;
        }
        bridge_face.normal_shading = source_face->normal_shading;
        if (rail_uvs != nullptr) {
            bridge_face.uvs.reserve(bridge_face.vertices.size());
            for (const ElementId vertex_id : bridge_face.vertices) {
                const auto uv = rail_uvs->find(vertex_id);
                if (uv == rail_uvs->end()) {
                    return false;
                }
                bridge_face.uvs.push_back(uv->second);
            }
        } else {
            if (!preserve_face_uvs(document, *source_face, candidate, bridge_face)) {
                return false;
            }
            if (bridge_face.uvs.empty() && face_has_loop_uvs(*other_face)) {
                if (!assign_face_local_uvs(candidate, bridge_face)) {
                    return false;
                }
            }
            if (face_has_loop_uvs(bridge_face)) {
                for (std::size_t index = 0; index < bridge_face.vertices.size(); ++index) {
                    if (!face_uv_for_vertex_id(*source_face, bridge_face.vertices[index]).has_value()) {
                        const auto uv = face_uv_for_vertex_id(*other_face, bridge_face.vertices[index]);
                        if (uv.has_value()) {
                            bridge_face.uvs[index] = unapply_face_texture_transform(
                                apply_face_texture_transform(*uv, other_face->texture_transform),
                                bridge_face.texture_transform);
                        }
                    }
                }
            }
        }
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
            if (!bridge_loop.has_value() || !append_bridge_face(*bridge_loop, pair_info.first, pair_info.second)) {
                return OperationResult::failure("Bridge could not create a valid triangle or quad from the selected edges.");
            }
        } else {
            const ElementId first_a = pair_info.first.oriented_face_edge.second;
            const ElementId first_b = pair_info.first.oriented_face_edge.first;
            const std::vector<ElementId> unique_vertices = unique_bridge_vertices(pair_info.first.edge, pair_info.second.edge);
            const auto bridge_loop = bridge_face_loop_from_open_edges(document, pair_info.first, pair_info.second);
            if (!bridge_loop.has_value()) {
                return OperationResult::failure("Bridge could not find consistently oriented boundary rails.");
            }
            const auto first_corner = std::ranges::find(*bridge_loop, first_a);
            const std::size_t first_index = static_cast<std::size_t>(std::distance(bridge_loop->begin(), first_corner));
            if ((*bridge_loop)[(first_index + 1U) % bridge_loop->size()] != first_b) {
                return OperationResult::failure("Bridge could not orient its first boundary edge.");
            }
            ElementId second_for_a = first_a;
            ElementId second_for_b = first_b;
            if (unique_vertices.size() == 4U) {
                second_for_b = (*bridge_loop)[(first_index + 2U) % bridge_loop->size()];
                second_for_a = (*bridge_loop)[(first_index + 3U) % bridge_loop->size()];
            } else if (unique_vertices.size() == 3U) {
                const ElementId third = (*bridge_loop)[(first_index + 2U) % bridge_loop->size()];
                if (pair_info.second.edge.a == first_a || pair_info.second.edge.b == first_a) {
                    second_for_b = third;
                } else {
                    second_for_a = third;
                }
            }
            const bool rail_a_shared = first_a == second_for_a;
            const bool rail_b_shared = first_b == second_for_b;
            if (unique_vertices.size() == 3 && !rail_a_shared && !rail_b_shared) {
                return OperationResult::failure("Bridge could not find a consistent triangle fan between the selected edges.");
            }
            if (unique_vertices.size() != 3 && unique_vertices.size() != 4) {
                return OperationResult::failure("Bridge needs selected edges that share one vertex or no vertices.");
            }

            const quader::QVec3 first_outward = bridge_edge_outward_direction(document, pair_info.first);
            const quader::QVec3 second_outward = bridge_edge_outward_direction(document, pair_info.second);
            const auto first_source = std::ranges::find_if(document.faces, [&](const Face& face) {
                return oriented_edge_in_face(face, pair_info.first.edge).has_value();
            });
            const auto second_source = std::ranges::find_if(document.faces, [&](const Face& face) {
                return oriented_edge_in_face(face, pair_info.second.edge).has_value();
            });
            if (first_source == document.faces.end() || second_source == document.faces.end()) {
                return OperationResult::failure("Bridge could not find its boundary texture charts.");
            }
            Face first_chart = *first_source;
            Face second_chart = *second_source;
            if (!ensure_face_loop_uvs(candidate, first_chart) ||
                !ensure_face_loop_uvs(candidate, second_chart)) {
                return OperationResult::failure("Bridge could not resolve its boundary texture charts.");
            }
            // Geometry can share cached rails; each pair retains its own chart seam.
            std::map<ElementId, quader::QVec2> rail_uvs;
            const auto assign_rail_uv = [&](ElementId vertex_id, ElementId start_id,
                                            ElementId end_id, int step_index) -> bool {
                const auto start_uv = face_uv_for_vertex_id(first_chart, start_id);
                const auto end_uv = face_uv_for_vertex_id(second_chart, end_id);
                if (!start_uv.has_value() || !end_uv.has_value()) {
                    return false;
                }
                if (step_index == 0 || start_id == end_id) {
                    rail_uvs.insert_or_assign(vertex_id, *start_uv);
                    return true;
                }
                const quader::QVec2 resolved_start = apply_face_texture_transform(
                    *start_uv, first_chart.texture_transform);
                const quader::QVec2 resolved_end = apply_face_texture_transform(
                    *end_uv, second_chart.texture_transform);
                const float factor = static_cast<float>(step_index) / static_cast<float>(step_count);
                const quader::QVec2 resolved_uv = step_index == step_count ? resolved_end :
                    quader::QVec2{resolved_start.x + (resolved_end.x - resolved_start.x) * factor,
                                 resolved_start.y + (resolved_end.y - resolved_start.y) * factor};
                rail_uvs.insert_or_assign(vertex_id, unapply_face_texture_transform(
                    resolved_uv, first_chart.texture_transform));
                return true;
            };
            if (!assign_rail_uv(first_a, first_a, second_for_a, 0) ||
                !assign_rail_uv(first_b, first_b, second_for_b, 0)) {
                return OperationResult::failure("Bridge could not resolve its boundary texture corners.");
            }
            ElementId previous_a = first_a;
            ElementId previous_b = first_b;
            for (int step_index = 1; step_index <= step_count; ++step_index) {
                const ElementId current_a = rail_a_shared ? first_a : interpolated_vertex(first_a, second_for_a, first_outward, second_outward, step_index);
                const ElementId current_b = rail_b_shared ? first_b : interpolated_vertex(first_b, second_for_b, first_outward, second_outward, step_index);
                if (current_a == kInvalidElementId ||
                    current_b == kInvalidElementId) {
                  return OperationResult::failure("Bridge could not create intermediate step vertices.");
                }
                if (!assign_rail_uv(current_a, first_a, second_for_a, step_index) ||
                    !assign_rail_uv(current_b, first_b, second_for_b, step_index)) {
                    return OperationResult::failure("Bridge could not interpolate its rail texture corners.");
                }

                if (rail_a_shared) {
                    if (!append_bridge_face({ first_a, previous_b, current_b }, pair_info.first, pair_info.second, &rail_uvs)) {
                        return OperationResult::failure("Bridge could not create a valid stepped triangle.");
                    }
                } else if (rail_b_shared) {
                    if (!append_bridge_face({ previous_a, first_b, current_a }, pair_info.first, pair_info.second, &rail_uvs)) {
                        return OperationResult::failure("Bridge could not create a valid stepped triangle.");
                    }
                } else {
                    if (!append_bridge_face({ previous_a, previous_b, current_b, current_a }, pair_info.first, pair_info.second, &rail_uvs)) {
                        return OperationResult::failure("Bridge could not create a valid stepped quad.");
                    }
                }

                previous_a = current_a;
                previous_b = current_b;
            }
        }
    }

    prune_normal_edge_states(candidate);
    if (bridged_face_ids.empty()) {
        return OperationResult::failure("Bridge would create invalid face geometry.");
    }

    const OperationResult validation = validate_authored_operation_candidate(document, candidate);
    if (!validation.success) {
        return validation;
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
