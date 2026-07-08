////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <mesh/polygon/internal/quader_poly_document_bridge_surface_helpers.hpp>

#include <mesh/polygon/internal/quader_poly_document_hull_plane_helpers.hpp>
#include <mesh/polygon/internal/quader_poly_document_knife_helpers.hpp>
#include <mesh/polygon/internal/quader_poly_document_mesh_internal.hpp>
#include <mesh/polygon/internal/quader_poly_document_topology_helpers.hpp>

#include <diagnostics/profile.hpp>
#include <mesh/geometry/geometry.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <exception>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace quader_poly::document_internal {

std::vector<ElementId> selected_valid_vertices(const Document& document, const Selection& selection)
{
    std::vector<ElementId> selected_vertices;
    selected_vertices.reserve(selection.vertices.size());
    for (const ElementId vertex_id : selection.vertices) {
      if (vertex_id == kInvalidElementId ||
          contains_id(selected_vertices, vertex_id) ||
          find_vertex(document, vertex_id) == nullptr) {
        continue;
      }
        selected_vertices.push_back(vertex_id);
    }
    return selected_vertices;
}

std::vector<Edge> selected_valid_edges(const Document& document, const Selection& selection)
{
    std::vector<Edge> selected_edges;
    selected_edges.reserve(selection.edges.size());
    for (Edge edge : selection.edges) {
        edge = make_edge(edge.a, edge.b);
        if (edge.a == kInvalidElementId || edge.b == kInvalidElementId ||
            edge.a == edge.b || contains_edge(selected_edges, edge) ||
            find_vertex(document, edge.a) == nullptr ||
            find_vertex(document, edge.b) == nullptr) {
          continue;
        }
        selected_edges.push_back(edge);
    }
    return selected_edges;
}

quader::QVec3 document_vertex_centroid(const Document& document)
{
    quader::QVec3 center;
    std::size_t count = 0;
    for (const Vertex& vertex : document.vertices) {
      if (vertex.id == kInvalidElementId) {
        continue;
      }
        center += vertex.position;
        ++count;
    }
    return count == 0 ? quader::QVec3 {} : center / static_cast<float>(count);
}

std::vector<std::size_t> adjacent_face_indices_for_edge(const Document& document, Edge edge)
{
    edge = make_edge(edge.a, edge.b);
    if (edge.a == kInvalidElementId || edge.b == kInvalidElementId ||
        edge.a == edge.b) {
      return {};
    }
    const std::map<std::pair<ElementId, ElementId>, std::vector<std::size_t>> indices_by_edge = face_indices_by_edge(document);
    const auto adjacent_faces = indices_by_edge.find({ edge.a, edge.b });
    if (adjacent_faces == indices_by_edge.end()) {
        return {};
    }
    return adjacent_faces->second;
}

std::vector<ElementId> loop_between_indices(const std::vector<ElementId>& loop, std::size_t start_index, std::size_t end_index)
{
    std::vector<ElementId> vertices;
    if (loop.empty() || start_index >= loop.size() || end_index >= loop.size()) {
        return vertices;
    }

    std::size_t index = start_index;
    while (true) {
        vertices.push_back(loop[index]);
        if (index == end_index) {
            break;
        }
        index = (index + 1U) % loop.size();
        if (index == start_index) {
            break;
        }
    }
    return vertices;
}

std::optional<ConnectEdgeFacePath> shortest_connect_edge_face_path(
    const Document& document,
    const std::map<std::pair<ElementId, ElementId>, std::vector<std::size_t>>& indices_by_edge,
    Edge first_edge,
    Edge second_edge,
    const std::set<std::pair<ElementId, ElementId>>* blocked_edges = nullptr)
{
    first_edge = make_edge(first_edge.a, first_edge.b);
    second_edge = make_edge(second_edge.a, second_edge.b);
    const auto first_faces = indices_by_edge.find({ first_edge.a, first_edge.b });
    const auto second_faces = indices_by_edge.find({ second_edge.a, second_edge.b });
    if (first_faces == indices_by_edge.end() || second_faces == indices_by_edge.end()) {
        return std::nullopt;
    }

    std::set<std::size_t> target_faces(second_faces->second.begin(), second_faces->second.end());
    std::vector<int> previous_face(document.faces.size(), -1);
    std::vector<Edge> previous_edge(document.faces.size());
    std::vector<std::size_t> queue;
    queue.reserve(document.faces.size());
    for (const std::size_t face_index : first_faces->second) {
        if (face_index >= document.faces.size() || previous_face[face_index] != -1) {
            continue;
        }
        previous_face[face_index] = -2;
        queue.push_back(face_index);
    }

    std::optional<std::size_t> target_index;
    for (std::size_t queue_index = 0; queue_index < queue.size(); ++queue_index) {
        const std::size_t face_index = queue[queue_index];
        if (target_faces.contains(face_index)) {
            target_index = face_index;
            break;
        }

        const Face& face = document.faces[face_index];
        for (std::size_t vertex_index = 0; vertex_index < face.vertices.size(); ++vertex_index) {
            const Edge shared_edge = make_edge(face.vertices[vertex_index], face.vertices[(vertex_index + 1U) % face.vertices.size()]);
            if (shared_edge == first_edge || shared_edge == second_edge) {
                continue;
            }
            if (blocked_edges != nullptr && blocked_edges->contains(edge_key(shared_edge))) {
                continue;
            }
            const auto adjacent = indices_by_edge.find({ shared_edge.a, shared_edge.b });
            if (adjacent == indices_by_edge.end() || adjacent->second.size() != 2) {
                continue;
            }

            const std::size_t next_face_index = adjacent->second[0] == face_index ? adjacent->second[1] : adjacent->second[0];
            if (next_face_index >= document.faces.size() || previous_face[next_face_index] != -1) {
                continue;
            }

            previous_face[next_face_index] = static_cast<int>(face_index);
            previous_edge[next_face_index] = shared_edge;
            queue.push_back(next_face_index);
        }
    }

    if (!target_index.has_value()) {
        return std::nullopt;
    }

    ConnectEdgeFacePath path;
    std::vector<std::size_t> reversed_faces;
    std::vector<Edge> reversed_edges;
    for (std::size_t face_index = *target_index; face_index < previous_face.size();) {
        reversed_faces.push_back(face_index);
        const int previous = previous_face[face_index];
        if (previous == -2) {
            break;
        }
        if (previous < 0) {
            return std::nullopt;
        }
        reversed_edges.push_back(previous_edge[face_index]);
        face_index = static_cast<std::size_t>(previous);
    }

    path.face_indices.assign(reversed_faces.rbegin(), reversed_faces.rend());
    path.shared_edges.assign(reversed_edges.rbegin(), reversed_edges.rend());
    if (path.face_indices.empty()) {
        return std::nullopt;
    }
    return path;
}

void add_connect_path_edges(
    const Document& document,
    const ConnectEdgeFacePath& path,
    Edge first_edge,
    Edge second_edge,
    std::map<ElementId, std::vector<Edge>>& cut_edges_by_face_id)
{
    if (path.face_indices.empty()) {
        return;
    }

    auto add_face_cut_edge = [&](std::size_t face_index, Edge edge) {
        if (face_index >= document.faces.size()) {
            return;
        }
        add_unique_edge(cut_edges_by_face_id[document.faces[face_index].id], edge);
    };

    if (path.face_indices.size() == 1U) {
        add_face_cut_edge(path.face_indices.front(), first_edge);
        add_face_cut_edge(path.face_indices.front(), second_edge);
        return;
    }

    for (std::size_t index = 0; index < path.face_indices.size(); ++index) {
        const std::size_t face_index = path.face_indices[index];
        if (index == 0) {
            add_face_cut_edge(face_index, first_edge);
            add_face_cut_edge(face_index, path.shared_edges.front());
        } else if (index + 1U == path.face_indices.size()) {
            add_face_cut_edge(face_index, path.shared_edges[index - 1U]);
            add_face_cut_edge(face_index, second_edge);
        } else {
            add_face_cut_edge(face_index, path.shared_edges[index - 1U]);
            add_face_cut_edge(face_index, path.shared_edges[index]);
        }
    }
}

std::vector<ConnectEdgeFaceRegion> connect_edge_face_regions(
    const Document& document,
    const std::map<std::pair<ElementId, ElementId>, std::vector<std::size_t>>& indices_by_edge,
    const std::set<std::pair<ElementId, ElementId>>& selected_edge_keys)
{
    std::vector<ConnectEdgeFaceRegion> regions;
    std::vector<bool> visited(document.faces.size(), false);

    for (std::size_t start_index = 0; start_index < document.faces.size(); ++start_index) {
        if (visited[start_index]) {
            continue;
        }

        ConnectEdgeFaceRegion region;
        std::vector<std::size_t> queue;
        queue.push_back(start_index);
        visited[start_index] = true;

        for (std::size_t queue_index = 0; queue_index < queue.size(); ++queue_index) {
            const std::size_t face_index = queue[queue_index];
            if (face_index >= document.faces.size()) {
                continue;
            }

            region.face_indices.push_back(face_index);
            const Face& face = document.faces[face_index];
            for (std::size_t vertex_index = 0; vertex_index < face.vertices.size(); ++vertex_index) {
                const Edge edge = make_edge(face.vertices[vertex_index], face.vertices[(vertex_index + 1U) % face.vertices.size()]);
                if (selected_edge_keys.contains(edge_key(edge))) {
                    add_unique_edge(region.boundary_selected_edges, edge);
                    continue;
                }

                const auto adjacent = indices_by_edge.find({ edge.a, edge.b });
                if (adjacent == indices_by_edge.end() || adjacent->second.size() != 2) {
                    continue;
                }

                const std::size_t next_face_index = adjacent->second[0] == face_index ? adjacent->second[1] : adjacent->second[0];
                if (next_face_index >= document.faces.size() || visited[next_face_index]) {
                    continue;
                }

                visited[next_face_index] = true;
                queue.push_back(next_face_index);
            }
        }

        if (!region.boundary_selected_edges.empty()) {
            regions.push_back(std::move(region));
        }
    }

    return regions;
}

void add_unordered_connect_region_paths(
    const Document& document,
    const std::map<std::pair<ElementId, ElementId>, std::vector<std::size_t>>& indices_by_edge,
    const std::set<std::pair<ElementId, ElementId>>& selected_edge_keys,
    const ConnectEdgeFaceRegion& region,
    std::map<ElementId, std::vector<Edge>>& cut_edges_by_face_id)
{
    const std::vector<Edge>& edges = region.boundary_selected_edges;
    if (edges.size() < 2U) {
        return;
    }

    if (edges.size() == 2U) {
        const std::optional<ConnectEdgeFacePath> path = shortest_connect_edge_face_path(document, indices_by_edge, edges[0], edges[1], &selected_edge_keys);
        if (path.has_value()) {
            add_connect_path_edges(document, *path, edges[0], edges[1], cut_edges_by_face_id);
        }
        return;
    }

    std::vector<ConnectEdgePairPath> pair_paths;
    std::size_t shortest_face_count = std::numeric_limits<std::size_t>::max();
    for (std::size_t first_index = 0; first_index < edges.size(); ++first_index) {
        for (std::size_t second_index = first_index + 1U; second_index < edges.size(); ++second_index) {
            const std::optional<ConnectEdgeFacePath> path =
                shortest_connect_edge_face_path(document, indices_by_edge, edges[first_index], edges[second_index], &selected_edge_keys);
            if (!path.has_value()) {
                continue;
            }

            shortest_face_count = std::min(shortest_face_count, path->face_indices.size());
            pair_paths.push_back({ edges[first_index], edges[second_index], *path });
        }
    }

    std::set<std::pair<ElementId, ElementId>> connected_selected_edges;
    for (const ConnectEdgePairPath& pair_path : pair_paths) {
        if (pair_path.path.face_indices.size() != shortest_face_count) {
            continue;
        }

        add_connect_path_edges(document, pair_path.path, pair_path.first, pair_path.second, cut_edges_by_face_id);
        connected_selected_edges.insert(edge_key(pair_path.first));
        connected_selected_edges.insert(edge_key(pair_path.second));
    }

    for (const Edge& selected_edge : edges) {
        if (connected_selected_edges.contains(edge_key(selected_edge))) {
            continue;
        }

        const auto nearest = std::min_element(pair_paths.begin(), pair_paths.end(), [selected_edge](const ConnectEdgePairPath& left, const ConnectEdgePairPath& right) {
            const bool left_uses_edge = left.first == selected_edge || left.second == selected_edge;
            const bool right_uses_edge = right.first == selected_edge || right.second == selected_edge;
            if (left_uses_edge != right_uses_edge) {
                return left_uses_edge;
            }
            return left.path.face_indices.size() < right.path.face_indices.size();
        });
        if (nearest == pair_paths.end() || (nearest->first != selected_edge && nearest->second != selected_edge)) {
            continue;
        }

        add_connect_path_edges(document, nearest->path, nearest->first, nearest->second, cut_edges_by_face_id);
        connected_selected_edges.insert(edge_key(nearest->first));
        connected_selected_edges.insert(edge_key(nearest->second));
    }
}

ElementId duplicate_vertex_for_face(
    Document& document,
    std::map<std::pair<ElementId, ElementId>, ElementId>& duplicate_vertices,
    ElementId face_id,
    ElementId vertex_id)
{
    const std::pair<ElementId, ElementId> key { face_id, vertex_id };
    const auto existing = duplicate_vertices.find(key);
    if (existing != duplicate_vertices.end()) {
        return existing->second;
    }

    const Vertex* source = find_vertex(document, vertex_id);
    if (source == nullptr) {
      return kInvalidElementId;
    }

    const ElementId duplicate_id = add_vertex(document, source->position);
    duplicate_vertices[key] = duplicate_id;
    return duplicate_id;
}

std::optional<std::vector<ElementId>> closed_edge_loop_from_edges(std::span<const Edge> selected_edges)
{
    if (selected_edges.size() < 3) {
        return std::nullopt;
    }

    std::map<ElementId, int> degree_by_vertex;
    for (const Edge& edge : selected_edges) {
        ++degree_by_vertex[edge.a];
        ++degree_by_vertex[edge.b];
    }
    if (std::ranges::any_of(degree_by_vertex, [](const auto& entry) {
            return entry.second != 2;
        })) {
        return std::nullopt;
    }

    std::vector<Edge> remaining_edges(selected_edges.begin(), selected_edges.end());
    std::vector<ElementId> loop;
    loop.reserve(selected_edges.size());
    const Edge first_edge = remaining_edges.front();
    loop.push_back(first_edge.a);
    ElementId previous_id = first_edge.a;
    ElementId current_id = first_edge.b;
    remaining_edges.erase(remaining_edges.begin());

    while (current_id != loop.front()) {
        if (contains_id(loop, current_id)) {
            return std::nullopt;
        }
        loop.push_back(current_id);

        const auto next_edge = std::ranges::find_if(remaining_edges, [current_id, previous_id](const Edge& edge) {
            return (edge.a == current_id && edge.b != previous_id) || (edge.b == current_id && edge.a != previous_id);
        });
        if (next_edge == remaining_edges.end()) {
            return std::nullopt;
        }

        const ElementId next_id = next_edge->a == current_id ? next_edge->b : next_edge->a;
        previous_id = current_id;
        current_id = next_id;
        remaining_edges.erase(next_edge);
    }

    return remaining_edges.empty() && loop.size() >= 3 ? std::optional<std::vector<ElementId>> { std::move(loop) } : std::nullopt;
}

bool orient_face_against_adjacent_winding(const Document& document, Face& face)
{
    if (face.vertices.size() < 3U) {
        return false;
    }

    int score = 0;
    for (std::size_t index = 0; index < face.vertices.size(); ++index) {
        const ElementId from_id = face.vertices[index];
        const ElementId to_id = face.vertices[(index + 1U) % face.vertices.size()];
        for (const Face& adjacent_face : document.faces) {
            if (directed_face_edge_index(adjacent_face, from_id, to_id).has_value()) {
                --score;
            }
            if (directed_face_edge_index(adjacent_face, to_id, from_id).has_value()) {
                ++score;
            }
        }
    }

    if (score == 0) {
        return false;
    }
    if (score < 0) {
        reverse_face_winding(face);
    }
    return true;
}

std::uint32_t material_slot_for_open_edge(const Document& document, Edge edge)
{
    edge = make_edge(edge.a, edge.b);
    for (const Face& face : document.faces) {
        for (std::size_t vertex_index = 0; vertex_index < face.vertices.size(); ++vertex_index) {
            const Edge face_edge = make_edge(face.vertices[vertex_index], face.vertices[(vertex_index + 1U) % face.vertices.size()]);
            if (face_edge == edge) {
                return face.material_slot;
            }
        }
    }
    return 0U;
}

std::vector<ElementId> face_vertices_between(const Face& face, std::size_t start_index, std::size_t end_index)
{
    std::vector<ElementId> vertices;
    if (face.vertices.empty()) {
        return vertices;
    }

    std::size_t index = start_index;
    while (true) {
        vertices.push_back(face.vertices[index]);
        if (index == end_index) {
            break;
        }
        index = (index + 1U) % face.vertices.size();
        if (index == start_index) {
            break;
        }
    }
    return vertices;
}

std::vector<ElementId> unique_valid_face_loop(std::vector<ElementId> vertices)
{
    std::vector<ElementId> compact;
    compact.reserve(vertices.size());
    for (const ElementId vertex_id : vertices) {
      if (vertex_id == kInvalidElementId) {
        continue;
      }
        if (!compact.empty() && compact.back() == vertex_id) {
            continue;
        }
        compact.push_back(vertex_id);
    }
    if (compact.size() > 1 && compact.front() == compact.back()) {
        compact.pop_back();
    }
    if (has_repeated_vertex(compact)) {
        return {};
    }
    return compact;
}

std::optional<std::size_t> directed_face_edge_index(const Face& face, ElementId from_id, ElementId to_id)
{
  if (face.vertices.size() < 2 || from_id == kInvalidElementId ||
      to_id == kInvalidElementId || from_id == to_id) {
    return std::nullopt;
  }
    for (std::size_t index = 0; index < face.vertices.size(); ++index) {
        if (face.vertices[index] == from_id && face.vertices[(index + 1U) % face.vertices.size()] == to_id) {
            return index;
        }
    }
    return std::nullopt;
}

std::optional<std::pair<ElementId, ElementId>> oriented_edge_in_face(const Face& face, Edge edge)
{
    edge = make_edge(edge.a, edge.b);
    if (directed_face_edge_index(face, edge.a, edge.b).has_value()) {
        return std::pair<ElementId, ElementId> { edge.a, edge.b };
    }
    if (directed_face_edge_index(face, edge.b, edge.a).has_value()) {
        return std::pair<ElementId, ElementId> { edge.b, edge.a };
    }
    return std::nullopt;
}

std::optional<OpenBridgeEdgeInfo> open_bridge_edge_info(
    const Document& document,
    Edge edge,
    const std::map<std::pair<ElementId, ElementId>, int>& incidence_counts)
{
    edge = make_edge(edge.a, edge.b);
    if (edge.a == kInvalidElementId || edge.b == kInvalidElementId ||
        edge.a == edge.b) {
      return std::nullopt;
    }
    const auto count = incidence_counts.find({ edge.a, edge.b });
    if (count == incidence_counts.end() || count->second < 1) {
        return std::nullopt;
    }
    for (const Face& face : document.faces) {
        const std::optional<std::pair<ElementId, ElementId>> oriented = oriented_edge_in_face(face, edge);
        if (oriented.has_value()) {
            return OpenBridgeEdgeInfo { edge, *oriented, face_normal(document, face), face.material_slot };
        }
    }
    return std::nullopt;
}

quader::QVec3 bridge_edge_outward_direction(const Document& document, const OpenBridgeEdgeInfo& edge_info)
{
    const Vertex* start = find_vertex(document, edge_info.oriented_face_edge.first);
    const Vertex* end = find_vertex(document, edge_info.oriented_face_edge.second);
    if (start == nullptr || end == nullptr) {
        return {};
    }

    const quader::QVec3 edge_direction = normalize_or_zero(end->position - start->position);
    if (length_squared(edge_direction) <= kEpsilon ||
        length_squared(edge_info.face_normal) <= kEpsilon) {
      return {};
    }

    return normalize_or_zero(cross(edge_direction, edge_info.face_normal));
}

quader::QVec3 curved_bridge_position(
    quader::QVec3 start,
    quader::QVec3 end,
    quader::QVec3 start_outward,
    quader::QVec3 end_outward,
    int step_index,
    int step_count)
{
    const float t = static_cast<float>(step_index) / static_cast<float>(step_count);
    const quader::QVec3 linear = start + ((end - start) * t);
    const float rail_length = length(end - start);
    const quader::QVec3 start_tangent_direction = normalize_or_zero(start_outward);
    const quader::QVec3 end_tangent_direction = normalize_or_zero(end_outward);
    if (rail_length <= kEpsilon ||
        length_squared(start_tangent_direction) <= kEpsilon ||
        length_squared(end_tangent_direction) <= kEpsilon) {
      return linear;
    }

    const quader::QVec3 start_tangent = start_tangent_direction * rail_length;
    const quader::QVec3 end_tangent = end_tangent_direction * -rail_length;
    const float t2 = t * t;
    const float t3 = t2 * t;
    const float h00 = (2.0F * t3) - (3.0F * t2) + 1.0F;
    const float h10 = t3 - (2.0F * t2) + t;
    const float h01 = (-2.0F * t3) + (3.0F * t2);
    const float h11 = t3 - t2;
    return (start * h00) + (start_tangent * h10) + (end * h01) + (end_tangent * h11);
}

std::vector<ElementId> unique_bridge_vertices(Edge first_edge, Edge second_edge)
{
    std::vector<ElementId> vertices;
    vertices.reserve(4);
    auto append = [&vertices](ElementId vertex_id) {
      if (vertex_id != kInvalidElementId &&
          std::find(vertices.begin(), vertices.end(), vertex_id) ==
              vertices.end()) {
        vertices.push_back(vertex_id);
      }
    };
    append(first_edge.a);
    append(first_edge.b);
    append(second_edge.a);
    append(second_edge.b);
    return vertices;
}

bool face_contains_reversed_bridge_edge(const Face& face, const OpenBridgeEdgeInfo& edge_info)
{
    return directed_face_edge_index(face, edge_info.oriented_face_edge.second, edge_info.oriented_face_edge.first).has_value();
}

float bridge_open_edge_loop_rail_score(const Document& document, std::span<const ElementId> loop, Edge first_edge, Edge second_edge)
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

std::optional<std::vector<ElementId>> bridge_face_loop_from_open_edges(
    const Document& document,
    const OpenBridgeEdgeInfo& first_edge,
    const OpenBridgeEdgeInfo& second_edge)
{
    std::vector<ElementId> vertices = unique_bridge_vertices(first_edge.edge, second_edge.edge);
    if (vertices.size() != 3 && vertices.size() != 4) {
        return std::nullopt;
    }
    std::sort(vertices.begin(), vertices.end());

    std::optional<std::vector<ElementId>> best_loop;
    float best_rail_score = std::numeric_limits<float>::infinity();
    float best_area = -1.0F;
    do {
        Face candidate_face;
        candidate_face.vertices = vertices;
        if (!face_contains_reversed_bridge_edge(candidate_face, first_edge) ||
                !face_contains_reversed_bridge_edge(candidate_face, second_edge)) {
            continue;
        }
        if (triangulate_face_local_indices(document, candidate_face).empty()) {
            continue;
        }
        const float area = face_loop_area_score(document, candidate_face.vertices);
        const float rail_score = bridge_open_edge_loop_rail_score(document, candidate_face.vertices, first_edge.edge, second_edge.edge);
        if (!std::isfinite(rail_score)) {
            continue;
        }
        if (!best_loop.has_value() || rail_score < best_rail_score - kEpsilon ||
            (std::abs(rail_score - best_rail_score) <= kEpsilon &&
             area > best_area)) {
          best_loop = candidate_face.vertices;
          best_rail_score = rail_score;
          best_area = area;
        }
    } while (std::next_permutation(vertices.begin(), vertices.end()));

    return best_loop;
}

std::tuple<ElementId, ElementId, ElementId> edge_bevel_side_key(ElementId face_id, Edge edge)
{
    const Edge normalized = make_edge(edge.a, edge.b);
    return { face_id, normalized.a, normalized.b };
}

bool edge_bevel_edge_is_concave(const Document& document, Edge edge, std::span<const std::size_t> adjacent_faces)
{
    if (adjacent_faces.size() != 2U ||
        adjacent_faces[0] >= document.faces.size() ||
        adjacent_faces[1] >= document.faces.size()) {
        return false;
    }

    const Face& first_face = document.faces[adjacent_faces[0]];
    const Face& second_face = document.faces[adjacent_faces[1]];
    const std::optional<std::pair<ElementId, ElementId>> oriented_edge = oriented_edge_in_face(first_face, edge);
    if (!oriented_edge.has_value()) {
        return false;
    }

    const Vertex* from = find_vertex(document, oriented_edge->first);
    const Vertex* to = find_vertex(document, oriented_edge->second);
    if (from == nullptr || to == nullptr) {
        return false;
    }

    const quader::QVec3 edge_direction = normalize_or_zero(to->position - from->position);
    const quader::QVec3 first_normal = face_normal(document, first_face);
    const quader::QVec3 second_normal = face_normal(document, second_face);
    if (length_squared(edge_direction) <= kEpsilon ||
        length_squared(first_normal) <= kEpsilon ||
        length_squared(second_normal) <= kEpsilon) {
      return false;
    }

    return dot(cross(first_normal, second_normal), edge_direction) < -kEpsilon;
}

EdgeBevelSettings sanitized_edge_bevel_settings(EdgeBevelSettings settings)
{
    if (!std::isfinite(settings.width)) {
        settings.width = 1.0F;
    }
    if (!std::isfinite(settings.profile)) {
        settings.profile = 0.5F;
    }

    settings.width = std::max(settings.width, kMinPrimitiveDimension);
    settings.profile = std::clamp(settings.profile, kEdgeBevelMinProfile,
                                  kEdgeBevelMaxProfile);
    settings.segments = std::clamp(settings.segments, 1, 16);
    settings.profile_type = BevelProfileType::Offset;
    return settings;
}

float edge_bevel_shape_profile(float profile)
{
  return std::clamp(profile, kEdgeBevelMinProfile, kEdgeBevelMaxProfile);
}

float safe_edge_bevel_width_for_face_edge(const Document& document, const Face& face, std::size_t edge_index, float requested_width)
{
    if (face.vertices.size() < 3 || edge_index >= face.vertices.size()) {
        return 0.0F;
    }

    const std::size_t previous_index = (edge_index + face.vertices.size() - 1U) % face.vertices.size();
    const std::size_t next_index = (edge_index + 1U) % face.vertices.size();
    const std::size_t after_index = (edge_index + 2U) % face.vertices.size();
    const Vertex* previous = find_vertex(document, face.vertices[previous_index]);
    const Vertex* current = find_vertex(document, face.vertices[edge_index]);
    const Vertex* next = find_vertex(document, face.vertices[next_index]);
    const Vertex* after = find_vertex(document, face.vertices[after_index]);
    if (previous == nullptr || current == nullptr || next == nullptr || after == nullptr) {
        return 0.0F;
    }

    const quader::QVec3 normal = face_normal(document, face);
    const quader::QVec3 edge_direction = normalize_or_zero(next->position - current->position);
    const quader::QVec3 inward = normalize_or_zero(cross(normal, edge_direction));
    if (length_squared(normal) <= kEpsilon ||
        length_squared(edge_direction) <= kEpsilon ||
        length_squared(inward) <= kEpsilon) {
      return 0.0F;
    }

    float max_width = requested_width;
    const auto apply_support_limit = [&max_width, inward](quader::QVec3 support_delta) {
        const float inward_distance = dot(support_delta, inward);
        if (inward_distance > kEpsilon) {
          max_width = std::min(max_width, inward_distance * 0.999F);
        }
    };
    apply_support_limit(previous->position - current->position);
    apply_support_limit(after->position - next->position);
    return std::max(0.0F, max_width);
}

std::optional<EdgeBevelSide> edge_bevel_side_for_face(
    Document& candidate,
    const Document& source,
    Edge edge,
    std::size_t face_index,
    float width)
{
    if (face_index >= source.faces.size()) {
        return std::nullopt;
    }

    edge = make_edge(edge.a, edge.b);
    const Face& face = source.faces[face_index];
    std::optional<std::size_t> directed_index = directed_face_edge_index(face, edge.a, edge.b);
    if (!directed_index.has_value()) {
        directed_index = directed_face_edge_index(face, edge.b, edge.a);
    }
    if (!directed_index.has_value()) {
        return std::nullopt;
    }

    const ElementId from_id = face.vertices[*directed_index];
    const ElementId to_id = face.vertices[(*directed_index + 1U) % face.vertices.size()];
    const Vertex* from = find_vertex(source, from_id);
    const Vertex* to = find_vertex(source, to_id);
    if (from == nullptr || to == nullptr) {
        return std::nullopt;
    }

    const quader::QVec3 normal = face_normal(source, face);
    const quader::QVec3 edge_direction = normalize_or_zero(to->position - from->position);
    const quader::QVec3 inward = normalize_or_zero(cross(normal, edge_direction));
    const float safe_width = safe_edge_bevel_width_for_face_edge(source, face, *directed_index, width);
    if (length_squared(normal) <= kEpsilon ||
        length_squared(edge_direction) <= kEpsilon ||
        length_squared(inward) <= kEpsilon || safe_width <= kEpsilon) {
      return std::nullopt;
    }

    const quader::QVec3 from_position = from->position + (inward * safe_width);
    const quader::QVec3 to_position = to->position + (inward * safe_width);

    EdgeBevelSide side;
    side.face_id = face.id;
    side.material_slot = face.material_slot;
    side.normal = normal;
    side.endpoint_positions[from_id] = from_position;
    side.endpoint_positions[to_id] = to_position;
    side.endpoint_vertices[from_id] = add_vertex(candidate, from_position);
    side.endpoint_vertices[to_id] = add_vertex(candidate, to_position);
    return side;
}

ElementId edge_bevel_side_endpoint_vertex(const EdgeBevelSide& side, ElementId endpoint_id)
{
    const auto found = side.endpoint_vertices.find(endpoint_id);
    return found == side.endpoint_vertices.end() ? kInvalidElementId
                                                 : found->second;
}

quader::QVec3 edge_bevel_side_endpoint_position(const EdgeBevelSide& side, ElementId endpoint_id)
{
    const auto found = side.endpoint_positions.find(endpoint_id);
    return found == side.endpoint_positions.end() ? quader::QVec3 {} : found->second;
}

std::vector<ElementId> compact_edge_bevel_face_loop(std::vector<ElementId> vertices)
{
    std::vector<ElementId> compact;
    compact.reserve(vertices.size());
    for (const ElementId vertex_id : vertices) {
      if (vertex_id == kInvalidElementId) {
        continue;
      }
        if (!compact.empty() && compact.back() == vertex_id) {
            continue;
        }
        compact.push_back(vertex_id);
    }
    if (compact.size() > 1 && compact.front() == compact.back()) {
        compact.pop_back();
    }
    return compact;
}

bool edge_bevel_face_loop_has_repeated_vertices(std::span<const ElementId> vertices)
{
    std::set<ElementId> seen;
    for (const ElementId vertex_id : vertices) {
        if (!seen.insert(vertex_id).second) {
            return true;
        }
    }
    return false;
}

std::optional<EdgeBevelOffsetLine> edge_bevel_offset_line_for_face_edge(
    const Document& document,
    const Face& face,
    std::size_t edge_index,
    float width)
{
    if (face.vertices.size() < 3 || edge_index >= face.vertices.size()) {
        return std::nullopt;
    }

    const ElementId from_id = face.vertices[edge_index];
    const ElementId to_id = face.vertices[(edge_index + 1U) % face.vertices.size()];
    const Vertex* from = find_vertex(document, from_id);
    const Vertex* to = find_vertex(document, to_id);
    if (from == nullptr || to == nullptr) {
        return std::nullopt;
    }

    const quader::QVec3 normal = face_normal(document, face);
    const quader::QVec3 edge_direction = normalize_or_zero(to->position - from->position);
    const quader::QVec3 inward = normalize_or_zero(cross(normal, edge_direction));
    const float safe_width = safe_edge_bevel_width_for_face_edge(document, face, edge_index, width);
    if (length_squared(normal) <= kEpsilon ||
        length_squared(edge_direction) <= kEpsilon ||
        length_squared(inward) <= kEpsilon || safe_width <= kEpsilon) {
      return std::nullopt;
    }

    return EdgeBevelOffsetLine { from->position + (inward * safe_width), edge_direction };
}

std::optional<quader::QVec3> edge_bevel_intersect_offset_lines(const EdgeBevelOffsetLine& first, const EdgeBevelOffsetLine& second)
{
    const quader_geometry::QVec3f r = geometry_vec3(first.direction);
    const quader_geometry::QVec3f s = geometry_vec3(second.direction);
    if (quader_geometry::length_squared(quader_geometry::cross(r, s)) <=
            kEpsilon ||
        quader_geometry::length_squared(r) <= kEpsilon ||
        quader_geometry::length_squared(s) <= kEpsilon) {
      return std::nullopt;
    }

    const quader_geometry::QLineLineClosestPoints<float> closest =
        quader_geometry::closest_points_line_line<float>(
            {geometry_vec3(first.point), r}, {geometry_vec3(second.point), s},
            kEpsilon);
    return closest.valid ? std::optional<quader::QVec3>(poly_vec3(closest.midpoint)) : std::nullopt;
}

std::optional<std::pair<ElementId, quader::QVec3>> edge_bevel_face_vertex_miter(
    Document& candidate,
    const Document& source,
    const Face& face,
    ElementId vertex_id,
    float width,
    const std::set<std::pair<ElementId, ElementId>>& selected_edge_keys,
    std::map<std::pair<ElementId, ElementId>, std::pair<ElementId, quader::QVec3>>& miter_vertices)
{
    const auto vertex = std::ranges::find(face.vertices, vertex_id);
    if (vertex == face.vertices.end() || face.vertices.size() < 3) {
        return std::nullopt;
    }

    const std::size_t vertex_index = static_cast<std::size_t>(std::distance(face.vertices.begin(), vertex));
    const std::size_t previous_index = (vertex_index + face.vertices.size() - 1U) % face.vertices.size();
    const ElementId previous_id = face.vertices[previous_index];
    const ElementId next_id = face.vertices[(vertex_index + 1U) % face.vertices.size()];
    if (!selected_edge_keys.contains(edge_key(make_edge(previous_id, vertex_id))) ||
        !selected_edge_keys.contains(edge_key(make_edge(vertex_id, next_id)))) {
        return std::nullopt;
    }

    const std::pair key { face.id, vertex_id };
    if (const auto existing = miter_vertices.find(key); existing != miter_vertices.end()) {
        return existing->second;
    }

    const std::optional<EdgeBevelOffsetLine> previous_line =
        edge_bevel_offset_line_for_face_edge(source, face, previous_index, width);
    const std::optional<EdgeBevelOffsetLine> next_line =
        edge_bevel_offset_line_for_face_edge(source, face, vertex_index, width);
    if (!previous_line.has_value() || !next_line.has_value()) {
        return std::nullopt;
    }

    std::optional<quader::QVec3> miter_position = edge_bevel_intersect_offset_lines(*previous_line, *next_line);
    if (!miter_position.has_value()) {
        const Vertex* vertex_source = find_vertex(source, vertex_id);
        if (vertex_source == nullptr) {
            return std::nullopt;
        }
        const quader::QVec3 previous_delta = previous_line->point - vertex_source->position;
        const quader::QVec3 next_delta = next_line->point - vertex_source->position;
        miter_position = vertex_source->position + ((previous_delta + next_delta) * 0.5F);
    }

    const ElementId miter_vertex_id = add_vertex(candidate, *miter_position);
    miter_vertices.emplace(key, std::pair { miter_vertex_id, *miter_position });
    return std::pair { miter_vertex_id, *miter_position };
}

ElementId edge_bevel_profile_vertex_for_source(
    Document& candidate,
    ElementId source_vertex_id,
    quader::QVec3 position,
    std::map<ElementId, std::vector<std::pair<ElementId, quader::QVec3>>>& profile_vertices_by_source)
{
    auto& profile_vertices = profile_vertices_by_source[source_vertex_id];
    const auto reusable = std::ranges::find_if(profile_vertices, [position](const std::pair<ElementId, quader::QVec3>& existing) {
        return length_squared(existing.second - position) <= 0.000001F;
    });
    if (reusable != profile_vertices.end()) {
        return reusable->first;
    }

    const ElementId vertex_id = add_vertex(candidate, position);
    profile_vertices.emplace_back(vertex_id, position);
    return vertex_id;
}

float edge_bevel_superellipse_exponent(float profile)
{
    const float shape_profile = edge_bevel_shape_profile(profile);
    if (shape_profile >= kEdgeBevelSquareProfileThreshold) {
      return kEdgeBevelSquareExponent;
    }
    if (shape_profile <= kEdgeBevelMinProfile) {
      return kEdgeBevelSquareInExponent;
    }

    const float exponent = -std::log(2.0F) / std::log(std::sqrt(shape_profile));
    if (std::abs(exponent - kEdgeBevelCircleExponent) <=
        kEdgeBevelExponentEpsilon) {
      return kEdgeBevelCircleExponent;
    }
    if (std::abs(exponent - kEdgeBevelLineExponent) <=
        kEdgeBevelExponentEpsilon) {
      return kEdgeBevelLineExponent;
    }
    if (exponent < kEdgeBevelExponentEpsilon) {
      return kEdgeBevelSquareInExponent;
    }
    return std::clamp(exponent, kEdgeBevelExponentEpsilon,
                      kEdgeBevelSquareExponent);
}

double edge_bevel_superellipse_y(double x, float exponent)
{
    x = std::clamp(x, 0.0, 1.0);
    const double r = static_cast<double>(exponent);
    return std::pow(std::max(0.0, 1.0 - std::pow(x, r)), 1.0 / r);
}

std::pair<float, float> edge_bevel_square_profile_coordinates(int segment, int segments, bool inward)
{
    const int half_segments = segments / 2;
    if (half_segments <= 0) {
        return segment <= 0 ? std::pair { 0.0F, 1.0F } : std::pair { 1.0F, 0.0F };
    }

    const float step = (segments % 2) == 0 ?
        2.0F / static_cast<float>(segments) :
        1.0F / (static_cast<float>(half_segments) + std::sqrt(2.0F) * 0.5F);

    if (segment <= half_segments) {
        const float distance = std::clamp(static_cast<float>(segment) * step, 0.0F, 1.0F);
        return inward ? std::pair { 0.0F, 1.0F - distance } : std::pair { distance, 1.0F };
    }

    const float distance = std::clamp(static_cast<float>(segments - segment) * step, 0.0F, 1.0F);
    return inward ? std::pair { 1.0F - distance, 0.0F } : std::pair { 1.0F, distance };
}


std::vector<EdgeBevelProfilePolylinePoint> edge_bevel_build_superellipse_profile_polyline(int segments, float exponent)
{
    const int sample_count = std::clamp(segments * 96, 512, 8192);
    std::vector<EdgeBevelProfilePolylinePoint> samples;
    samples.reserve(static_cast<std::size_t>(sample_count + 1));

    for (int index = 0; index <= sample_count; ++index) {
        const double t = static_cast<double>(index) / static_cast<double>(sample_count);
        const double x = 0.5 - (std::cos(t * static_cast<double>(kPi)) * 0.5);
        const double y = edge_bevel_superellipse_y(x, exponent);
        double length = 0.0;
        if (!samples.empty()) {
            const EdgeBevelProfilePolylinePoint& previous = samples.back();
            length = previous.length + std::hypot(x - previous.x, y - previous.y);
        }
        samples.push_back({ x, y, length });
    }

    samples.front() = { 0.0, 1.0, 0.0 };
    samples.back().x = 1.0;
    samples.back().y = 0.0;
    return samples;
}

std::pair<float, float> edge_bevel_sample_profile_polyline(std::span<const EdgeBevelProfilePolylinePoint> samples, float fraction)
{
    if (samples.empty()) {
        return { 0.0F, 1.0F };
    }
    if (samples.size() == 1 || fraction <= 0.0F || samples.back().length <= 0.0) {
        return { static_cast<float>(samples.front().x), static_cast<float>(samples.front().y) };
    }
    if (fraction >= 1.0F) {
        return { static_cast<float>(samples.back().x), static_cast<float>(samples.back().y) };
    }

    const double target_length = samples.back().length * static_cast<double>(fraction);
    const auto upper = std::ranges::lower_bound(
        samples,
        target_length,
        {},
        [](const EdgeBevelProfilePolylinePoint& sample) {
            return sample.length;
        });
    if (upper == samples.begin()) {
        return { static_cast<float>(upper->x), static_cast<float>(upper->y) };
    }
    if (upper == samples.end()) {
        return { static_cast<float>(samples.back().x), static_cast<float>(samples.back().y) };
    }

    const EdgeBevelProfilePolylinePoint& b = *upper;
    const EdgeBevelProfilePolylinePoint& a = *(upper - 1);
    const double span_length = b.length - a.length;
    const double t = span_length <= 0.0 ? 0.0 : (target_length - a.length) / span_length;
    return {
        static_cast<float>(a.x + ((b.x - a.x) * t)),
        static_cast<float>(a.y + ((b.y - a.y) * t)),
    };
}

std::pair<float, float> edge_bevel_profile_coordinates(int segment, int segments, float profile, BevelProfileType profile_type)
{
    (void)profile_type;
    segments = std::max(segments, 1);
    segment = std::clamp(segment, 0, segments);
    if (segment == 0) {
        return { 1.0F, 0.0F };
    }
    if (segment == segments) {
        return { 0.0F, 1.0F };
    }

    const float exponent = edge_bevel_superellipse_exponent(profile);
    float x = 0.0F;
    float y = 1.0F;
    if (exponent == kEdgeBevelSquareExponent) {
      std::tie(x, y) =
          edge_bevel_square_profile_coordinates(segment, segments, false);
    } else if (exponent == kEdgeBevelSquareInExponent) {
      std::tie(x, y) =
          edge_bevel_square_profile_coordinates(segment, segments, true);
    } else if (std::abs(exponent - kEdgeBevelLineExponent) <=
               kEdgeBevelExponentEpsilon) {
      x = static_cast<float>(segment) / static_cast<float>(segments);
      y = 1.0F - x;
    } else if (std::abs(exponent - kEdgeBevelCircleExponent) <=
               kEdgeBevelExponentEpsilon) {
      const float theta = static_cast<float>(segment) * kPi * 0.5F /
                          static_cast<float>(segments);
      x = std::sin(theta);
      y = std::cos(theta);
    } else {
      const std::vector<EdgeBevelProfilePolylinePoint> samples =
          edge_bevel_build_superellipse_profile_polyline(segments, exponent);
      std::tie(x, y) = edge_bevel_sample_profile_polyline(
          samples, static_cast<float>(segment) / static_cast<float>(segments));
    }

    return { y, x };
}

quader::QVec3 edge_bevel_profile_point(quader::QVec3 source_position, quader::QVec3 first_boundary, quader::QVec3 second_boundary, int segment, int segments, float profile, BevelProfileType profile_type)
{
    const auto [first_weight, second_weight] = edge_bevel_profile_coordinates(segment, segments, profile, profile_type);
    const quader::QVec3 first_delta = first_boundary - source_position;
    const quader::QVec3 second_delta = second_boundary - source_position;
    return source_position +
        (first_delta * (1.0F - second_weight)) +
        (second_delta * (1.0F - first_weight));
}

quader::QVec3 edge_bevel_profile_middle_on_edge(quader::QVec3 edge_start, quader::QVec3 edge_end, quader::QVec3 first_boundary, quader::QVec3 second_boundary)
{
    const quader_geometry::QVec3f edge_delta = geometry_vec3(edge_end - edge_start);
    const quader_geometry::QVec3f boundary_delta = geometry_vec3(second_boundary - first_boundary);
    const float edge_length_squared = quader_geometry::length_squared(edge_delta);
    const float boundary_length_squared = quader_geometry::length_squared(boundary_delta);

    if (edge_length_squared <= kEpsilon) {
      return edge_start;
    }
    if (boundary_length_squared <= kEpsilon) {
      const float edge_parameter =
          quader_geometry::dot(geometry_vec3(first_boundary - edge_start),
                               edge_delta) /
          edge_length_squared;
      return edge_start + ((edge_end - edge_start) * edge_parameter);
    }

    const float edge_boundary_dot = quader_geometry::dot(edge_delta, boundary_delta);
    const float denominator = (edge_length_squared * boundary_length_squared) - (edge_boundary_dot * edge_boundary_dot);

    if (denominator <= kEpsilon) {
      return edge_start;
    }

    const quader_geometry::QLineLineClosestPoints<float> closest =
        quader_geometry::closest_points_line_line<float>(
            {geometry_vec3(edge_start), edge_delta},
            {geometry_vec3(first_boundary), boundary_delta}, kEpsilon);
    return closest.valid ? poly_vec3(closest.first_point) : edge_start;
}

void edge_bevel_set_component(quader::QVec3& value, int axis, float component)
{
    switch ((axis % 3 + 3) % 3) {
    case 0:
        value.x = component;
        break;
    case 1:
        value.y = component;
        break;
    default:
        value.z = component;
        break;
    }
}

quader::QVec3 edge_bevel_snap_to_superellipsoid(quader::QVec3 point, float exponent)
{
    quader::QVec3 positive {
        std::max(0.0F, point.x),
        std::max(0.0F, point.y),
        std::max(0.0F, point.z),
    };
    if (exponent == kEdgeBevelSquareExponent) {
      const float max_component =
          std::max(positive.x, std::max(positive.y, positive.z));
      return max_component > kEpsilon ? positive / max_component : positive;
    }
    if (exponent == kEdgeBevelSquareInExponent) {
      return positive;
    }
    if (std::abs(exponent - kEdgeBevelCircleExponent) <=
        kEdgeBevelExponentEpsilon) {
      return normalize_or_zero(positive);
    }

    const float inverse_exponent = 1.0F / exponent;
    if (positive.x <= kEpsilon) {
      if (positive.y <= kEpsilon) {
        return {0.0F, 0.0F, std::pow(positive.z, inverse_exponent)};
      }
      const float y =
          std::pow(1.0F / (1.0F + std::pow(positive.z / positive.y, exponent)),
                   inverse_exponent);
      return {0.0F, y, positive.z * y / positive.y};
    }

    const float x = std::pow(
        1.0F / (1.0F + std::pow(positive.y / positive.x, exponent) + std::pow(positive.z / positive.x, exponent)),
        inverse_exponent);
    return {
        x,
        positive.y * x / positive.x,
        positive.z * x / positive.x,
    };
}

quader::QVec3 edge_bevel_map_unit_cube_corner(quader::QVec3 axis0, quader::QVec3 axis1, quader::QVec3 axis2, quader::QVec3 source, quader::QVec3 point)
{
    const quader::QVec3 column0 = (axis0 - axis1 - axis2 + source) * 0.5F;
    const quader::QVec3 column1 = (axis1 - axis0 - axis2 + source) * 0.5F;
    const quader::QVec3 column2 = (axis2 - axis0 - axis1 + source) * 0.5F;
    const quader::QVec3 column3 = (axis0 + axis1 + axis2 - source) * 0.5F;
    return column3 + (column0 * point.x) + (column1 * point.y) + (column2 * point.z);
}

bool append_edge_bevel_face(
    Document& document,
    std::vector<Face>& faces,
    std::vector<ElementId>& generated_face_ids,
    std::vector<ElementId> vertices,
    std::uint32_t material_slot,
    quader::QVec3 expected_normal)
{
    vertices = compact_edge_bevel_face_loop(std::move(vertices));
    if (vertices.size() < 3 || edge_bevel_face_loop_has_repeated_vertices(vertices)) {
        return false;
    }

    Face face;
    face.id = document.next_face_id++;
    face.vertices = std::move(vertices);
    face.material_slot = material_slot;
    orient_face_toward_normal(document, face, expected_normal);

    generated_face_ids.push_back(face.id);
    faces.push_back(std::move(face));
    return true;
}


EdgeBevelTriCornerKey edge_bevel_tri_corner_canonical_key(int side, int ring, int segment, int segments)
{
  constexpr int kSideCount = 3;
  const int half_segments = segments / 2;
  const bool odd = (segments % 2) == 1;
  side = (side % kSideCount + kSideCount) % kSideCount;

  if (!odd && ring == half_segments && segment == half_segments) {
    return {0, ring, segment};
  }
    if (ring <= half_segments - 1 + (odd ? 1 : 0) && segment <= half_segments) {
        return { side, ring, segment };
    }
    if (segment <= half_segments) {
      return {(side + kSideCount - 1) % kSideCount, segment, segments - ring};
    }
    return {(side + 1) % kSideCount, segments - segment, ring};
}

std::optional<std::vector<ElementId>> ordered_tri_corner_boundary_ring(
    const Document& document,
    ElementId source_vertex_id,
    std::vector<ElementId> patch_vertices,
    int segments)
{
    patch_vertices = compact_edge_bevel_face_loop(std::move(patch_vertices));
    if (segments < 2 || patch_vertices.size() != static_cast<std::size_t>(segments * 3)) {
        return std::nullopt;
    }

    const Vertex* source_vertex = find_vertex(document, source_vertex_id);
    if (source_vertex == nullptr) {
        return std::nullopt;
    }

    std::vector<std::pair<float, std::size_t>> distances;
    distances.reserve(patch_vertices.size());
    for (std::size_t index = 0; index < patch_vertices.size(); ++index) {
        const Vertex* vertex = find_vertex(document, patch_vertices[index]);
        if (vertex == nullptr) {
            return std::nullopt;
        }
        distances.emplace_back(length_squared(vertex->position - source_vertex->position), index);
    }
    std::ranges::sort(distances, [](const auto& left, const auto& right) {
        return left.first > right.first;
    });

    std::set<std::size_t> corner_indices;
    for (std::size_t index = 0; index < 3U && index < distances.size(); ++index) {
        corner_indices.insert(distances[index].second);
    }
    if (corner_indices.size() != 3U) {
        return std::nullopt;
    }

    auto build_ring = [&patch_vertices](std::size_t start, bool reverse) {
        std::vector<ElementId> ring;
        ring.reserve(patch_vertices.size());
        const std::size_t count = patch_vertices.size();
        for (std::size_t offset = 0; offset < count; ++offset) {
            const std::size_t index = reverse ? (start + count - offset) % count : (start + offset) % count;
            ring.push_back(patch_vertices[index]);
        }
        return ring;
    };
    auto is_corner_id = [&patch_vertices, &corner_indices](ElementId vertex_id) {
        return std::ranges::any_of(corner_indices, [&patch_vertices, vertex_id](std::size_t index) {
            return patch_vertices[index] == vertex_id;
        });
    };
    auto is_valid_ring = [segments, &is_corner_id](const std::vector<ElementId>& ring) {
        return ring.size() == static_cast<std::size_t>(segments * 3) &&
            is_corner_id(ring[0]) &&
            is_corner_id(ring[static_cast<std::size_t>(segments)]) &&
            is_corner_id(ring[static_cast<std::size_t>(segments * 2)]);
    };

    for (const std::size_t corner_index : corner_indices) {
        std::vector<ElementId> ring = build_ring(corner_index, false);
        if (is_valid_ring(ring)) {
            return ring;
        }
        ring = build_ring(corner_index, true);
        if (is_valid_ring(ring)) {
            return ring;
        }
    }
    return std::nullopt;
}

bool append_edge_bevel_tri_corner_patch_faces(
    Document& document,
    std::vector<Face>& faces,
    std::vector<ElementId>& generated_face_ids,
    const std::vector<ElementId>& patch_vertices,
    ElementId source_vertex_id,
    std::uint32_t material_slot,
    quader::QVec3 expected_normal,
    int segments,
    float profile,
    BevelProfileType profile_type)
{
    (void)profile_type;
    const std::optional<std::vector<ElementId>> boundary_ring =
        ordered_tri_corner_boundary_ring(document, source_vertex_id, patch_vertices, segments);
    if (!boundary_ring.has_value()) {
        return false;
    }

    const Vertex* source_vertex = find_vertex(document, source_vertex_id);
    if (source_vertex == nullptr) {
        return false;
    }
    const Vertex* corner0 = find_vertex(document, (*boundary_ring)[0]);
    const Vertex* corner1 = find_vertex(document, (*boundary_ring)[static_cast<std::size_t>(segments)]);
    const Vertex* corner2 = find_vertex(document, (*boundary_ring)[static_cast<std::size_t>(segments * 2)]);
    if (corner0 == nullptr || corner1 == nullptr || corner2 == nullptr) {
        return false;
    }

    const int half_segments = segments / 2;
    const bool odd = (segments % 2) == 1;
    const float exponent = edge_bevel_superellipse_exponent(profile);
    std::map<EdgeBevelTriCornerKey, ElementId> generated_vertices;

    auto boundary_vertex = [&boundary_ring, segments](int side, int segment) {
      constexpr int kSideCount = 3;
      side = (side % kSideCount + kSideCount) % kSideCount;
      segment = std::clamp(segment, 0, segments);
      return (*boundary_ring)[static_cast<std::size_t>(
          (side * segments + segment) % (segments * kSideCount))];
    };

    auto vertex_at = [&](int side, int ring, int segment) -> ElementId {
        EdgeBevelTriCornerKey key = edge_bevel_tri_corner_canonical_key(side, ring, segment, segments);
        if (key.ring == 0) {
            return boundary_vertex(key.side, key.segment);
        }

        const auto existing = generated_vertices.find(key);
        if (existing != generated_vertices.end()) {
            return existing->second;
        }

        quader::QVec3 unit;
        edge_bevel_set_component(unit, key.side, 1.0F);
        edge_bevel_set_component(unit, key.side + 1, static_cast<float>(key.segment) * 2.0F / static_cast<float>(segments));
        edge_bevel_set_component(unit, key.side + 2, static_cast<float>(key.ring) * 2.0F / static_cast<float>(segments));
        unit = edge_bevel_snap_to_superellipsoid(unit, exponent);

        const ElementId vertex_id = add_vertex(
            document,
            edge_bevel_map_unit_cube_corner(
                corner0->position,
                corner1->position,
                corner2->position,
                source_vertex->position,
                unit));
        generated_vertices.emplace(key, vertex_id);
        return vertex_id;
    };

    bool appended = false;
    for (int side = 0; side < 3; ++side) {
        for (int ring = 0; ring < half_segments; ++ring) {
            for (int segment = 0; segment < half_segments + (odd ? 1 : 0); ++segment) {
                appended |= append_edge_bevel_face(
                    document,
                    faces,
                    generated_face_ids,
                    {
                        vertex_at(side, ring, segment),
                        vertex_at(side, ring, segment + 1),
                        vertex_at(side, ring + 1, segment + 1),
                        vertex_at(side, ring + 1, segment),
                    },
                    material_slot,
                    expected_normal);
            }
        }
    }

    if (odd) {
        std::vector<ElementId> center_vertices;
        center_vertices.reserve(3U);
        for (int side = 0; side < 3; ++side) {
            center_vertices.push_back(vertex_at(side, half_segments, half_segments));
        }
        appended |= append_edge_bevel_face(
            document,
            faces,
            generated_face_ids,
            std::move(center_vertices),
            material_slot,
            expected_normal);
    }

    return appended;
}





int edge_bevel_positive_mod(int value, int divisor)
{
    if (divisor <= 0) {
        return 0;
    }
    return (value % divisor + divisor) % divisor;
}

EdgeBevelVMeshKey edge_bevel_vmesh_canonical_key(int side, int ring, int segment, int side_count, int segments)
{
    const int half_segments = segments / 2;
    const bool odd = (segments % 2) == 1;
    side = edge_bevel_positive_mod(side, side_count);

    if (!odd && ring == half_segments && segment == half_segments) {
        return { 0, ring, segment };
    }
    if (ring <= half_segments - 1 + (odd ? 1 : 0) && segment <= half_segments) {
        return { side, ring, segment };
    }
    if (segment <= half_segments) {
        return { edge_bevel_positive_mod(side - 1, side_count), segment, segments - ring };
    }
    return { edge_bevel_positive_mod(side + 1, side_count), segments - segment, ring };
}

bool edge_bevel_vmesh_is_canonical(const EdgeBevelVMesh& vmesh, int side, int ring, int segment)
{
    const int half_segments = vmesh.segments / 2;
    if ((vmesh.segments % 2) == 1) {
        return ring <= half_segments && segment <= half_segments;
    }
    return (ring < half_segments && segment <= half_segments) ||
        (ring == half_segments && segment == half_segments && edge_bevel_positive_mod(side, vmesh.side_count) == 0);
}

std::size_t edge_bevel_vmesh_slot_index(const EdgeBevelVMesh& vmesh, int side, int ring, int segment)
{
    side = edge_bevel_positive_mod(side, vmesh.side_count);
    const std::size_t ring_count = static_cast<std::size_t>(vmesh.segments / 2 + 1);
    const std::size_t segment_count = static_cast<std::size_t>(vmesh.segments + 1);
    return ((static_cast<std::size_t>(side) * ring_count) + static_cast<std::size_t>(ring)) * segment_count +
        static_cast<std::size_t>(segment);
}

EdgeBevelVMeshSlot& edge_bevel_vmesh_slot(EdgeBevelVMesh& vmesh, int side, int ring, int segment)
{
    return vmesh.slots[edge_bevel_vmesh_slot_index(vmesh, side, ring, segment)];
}

const EdgeBevelVMeshSlot& edge_bevel_vmesh_slot(const EdgeBevelVMesh& vmesh, int side, int ring, int segment)
{
    return vmesh.slots[edge_bevel_vmesh_slot_index(vmesh, side, ring, segment)];
}

EdgeBevelVMeshSlot& edge_bevel_vmesh_canonical_slot(EdgeBevelVMesh& vmesh, int side, int ring, int segment)
{
    const EdgeBevelVMeshKey key = edge_bevel_vmesh_canonical_key(side, ring, segment, vmesh.side_count, vmesh.segments);
    return edge_bevel_vmesh_slot(vmesh, key.side, key.ring, key.segment);
}

const EdgeBevelVMeshSlot& edge_bevel_vmesh_canonical_slot(const EdgeBevelVMesh& vmesh, int side, int ring, int segment)
{
    const EdgeBevelVMeshKey key = edge_bevel_vmesh_canonical_key(side, ring, segment, vmesh.side_count, vmesh.segments);
    return edge_bevel_vmesh_slot(vmesh, key.side, key.ring, key.segment);
}

EdgeBevelVMesh edge_bevel_make_vmesh(int side_count, int segments, std::vector<EdgeBevelVMeshProfile> profiles)
{
    EdgeBevelVMesh vmesh;
    vmesh.side_count = side_count;
    vmesh.segments = segments;
    vmesh.profiles = std::move(profiles);
    vmesh.slots.resize(static_cast<std::size_t>(side_count) *
        static_cast<std::size_t>(segments / 2 + 1) *
        static_cast<std::size_t>(segments + 1));
    return vmesh;
}

quader::QVec3 edge_bevel_vmesh_profile_point(
    const EdgeBevelVMesh& vmesh,
    int side,
    int segment,
    int segment_count,
    float profile,
    BevelProfileType profile_type)
{
    const EdgeBevelVMeshProfile& vprofile =
        vmesh.profiles[static_cast<std::size_t>(edge_bevel_positive_mod(side, vmesh.side_count))];
    const float effective_profile = vprofile.use_global_profile ? profile : vprofile.profile;
    const BevelProfileType effective_profile_type = vprofile.use_global_profile ? profile_type : vprofile.profile_type;
    quader::QVec3 point = edge_bevel_profile_point(
        vprofile.source,
        vprofile.start,
        vprofile.end,
        segment,
        segment_count,
        effective_profile,
        effective_profile_type);
    if (!vprofile.has_projection) {
        return point;
    }

    const float denominator = dot(vprofile.plane_normal, vprofile.projection_direction);
    if (std::abs(denominator) <= kEpsilon) {
      return point;
    }

    const float t = dot(vprofile.plane_normal, vprofile.plane_co - point) / denominator;
    return point + (vprofile.projection_direction * t);
}

void edge_bevel_vmesh_copy_equivalent_slots(EdgeBevelVMesh& vmesh)
{
    const int half_segments = vmesh.segments / 2;
    for (int side = 0; side < vmesh.side_count; ++side) {
        for (int ring = 0; ring <= half_segments; ++ring) {
            for (int segment = 0; segment <= vmesh.segments; ++segment) {
                if (edge_bevel_vmesh_is_canonical(vmesh, side, ring, segment)) {
                    continue;
                }
                edge_bevel_vmesh_slot(vmesh, side, ring, segment) =
                    edge_bevel_vmesh_canonical_slot(vmesh, side, ring, segment);
            }
        }
    }
}

quader::QVec3 edge_bevel_average4(quader::QVec3 a, quader::QVec3 b, quader::QVec3 c, quader::QVec3 d)
{
    return (a + b + c + d) * 0.25F;
}

float edge_bevel_sabin_gamma(int side_count)
{
    if (side_count < 3) {
        return 0.0F;
    }
    if (side_count == 3) {
        return 0.065247584F;
    }
    if (side_count == 4) {
        return 0.25F;
    }
    if (side_count == 5) {
        return 0.401983447F;
    }
    if (side_count == 6) {
        return 0.523423277F;
    }

    const double cosine = std::cos(kPi / static_cast<double>(side_count));
    const double cosine2 = cosine * cosine;
    const double cosine4 = cosine2 * cosine2;
    const double cosine6 = cosine4 * cosine2;
    const double y = std::pow(
        std::sqrt(3.0) * std::sqrt((64.0 * cosine6) - (144.0 * cosine4) + (135.0 * cosine2) - 27.0) +
            (9.0 * cosine),
        1.0 / 3.0);
    const double x = (0.480749856769136 * y) - ((0.231120424783545 * ((12.0 * cosine2) - 9.0)) / y);
    return static_cast<float>(((cosine * x) + (2.0 * cosine2) - 1.0) / ((x * x) * ((cosine * x) + 1.0)));
}

float edge_bevel_profile_fullness(int segments, float profile)
{
    const float exponent = edge_bevel_superellipse_exponent(profile);
    if (std::abs(exponent - kEdgeBevelLineExponent) <=
        kEdgeBevelExponentEpsilon) {
      return 0.0F;
    }

    static constexpr std::array<float, 11> kCircleFullness{
        0.0F,   0.559F, 0.642F, 0.551F, 0.646F, 0.624F,
        0.646F, 0.619F, 0.647F, 0.639F, 0.647F,
    };
    if (std::abs(exponent - kEdgeBevelCircleExponent) <=
            kEdgeBevelExponentEpsilon &&
        segments > 0 && segments <= static_cast<int>(kCircleFullness.size())) {
      return kCircleFullness[static_cast<std::size_t>(segments - 1)];
    }
    if ((segments % 2) == 0) {
        return (2.4506F * profile) - (0.00000300F * static_cast<float>(segments)) - 0.6266F;
    }
    return (2.3635F * profile) + (0.000152F * static_cast<float>(segments)) - 0.6060F;
}

std::vector<float> edge_bevel_vmesh_boundary_fractions(const EdgeBevelVMesh& vmesh, int side)
{
    std::vector<float> fractions(static_cast<std::size_t>(vmesh.segments + 1), 0.0F);
    float total = 0.0F;
    for (int segment = 0; segment < vmesh.segments; ++segment) {
        total += length(
            edge_bevel_vmesh_slot(vmesh, side, 0, segment + 1).position -
            edge_bevel_vmesh_slot(vmesh, side, 0, segment).position);
        fractions[static_cast<std::size_t>(segment + 1)] = total;
    }
    if (total > 0.0F) {
        for (int segment = 1; segment <= vmesh.segments; ++segment) {
            fractions[static_cast<std::size_t>(segment)] /= total;
        }
    } else {
        fractions.back() = 1.0F;
    }
    return fractions;
}

std::vector<float> edge_bevel_vmesh_profile_fractions(
    const EdgeBevelVMesh& vmesh,
    int side,
    int segments,
    float profile,
    BevelProfileType profile_type)
{
    std::vector<float> fractions(static_cast<std::size_t>(segments + 1), 0.0F);
    float total = 0.0F;
    quader::QVec3 previous = edge_bevel_vmesh_profile_point(vmesh, side, 0, segments, profile, profile_type);
    for (int segment = 0; segment < segments; ++segment) {
        const quader::QVec3 next = edge_bevel_vmesh_profile_point(vmesh, side, segment + 1, segments, profile, profile_type);
        total += length(next - previous);
        fractions[static_cast<std::size_t>(segment + 1)] = total;
        previous = next;
    }
    if (total > 0.0F) {
        for (int segment = 1; segment <= segments; ++segment) {
            fractions[static_cast<std::size_t>(segment)] /= total;
        }
    } else {
        fractions.back() = 1.0F;
    }
    return fractions;
}

int edge_bevel_vmesh_interp_range(std::span<const float> fractions, float value, float& rest)
{
    const int count = static_cast<int>(fractions.size()) - 1;
    for (int index = 0; index < count; ++index) {
        if (value <= fractions[static_cast<std::size_t>(index + 1)]) {
            const float span = fractions[static_cast<std::size_t>(index + 1)] - fractions[static_cast<std::size_t>(index)];
            rest = span == 0.0F ? 0.0F : (value - fractions[static_cast<std::size_t>(index)]) / span;
            if (index == count - 1 && rest == 1.0F) {
                rest = 0.0F;
                return count;
            }
            return index;
        }
    }
    rest = 0.0F;
    return count;
}

quader::QVec3 edge_bevel_bilinear(quader::QVec3 lower_left, quader::QVec3 lower_right, quader::QVec3 upper_right, quader::QVec3 upper_left, float x, float y)
{
    return (lower_left * ((1.0F - x) * (1.0F - y))) +
        (lower_right * (x * (1.0F - y))) +
        (upper_right * (x * y)) +
        (upper_left * ((1.0F - x) * y));
}

EdgeBevelVMesh edge_bevel_vmesh_cubic_subdiv(
    EdgeBevelVMesh vmesh_in,
    float profile,
    BevelProfileType profile_type)
{
    QDR_PROFILE_SCOPE("qdr_document.edge_bevel_vmesh_cubic_subdiv");
    const int side_count = vmesh_in.side_count;
    const int input_segments = vmesh_in.segments;
    const int input_half_segments = input_segments / 2;
    const int output_segments = input_segments * 2;
    EdgeBevelVMesh vmesh_out = edge_bevel_make_vmesh(side_count, output_segments, vmesh_in.profiles);

    for (int side = 0; side < side_count; ++side) {
        edge_bevel_vmesh_slot(vmesh_out, side, 0, 0).position =
            edge_bevel_vmesh_slot(vmesh_in, side, 0, 0).position;
        for (int segment = 1; segment < input_segments; ++segment) {
            quader::QVec3 position = edge_bevel_vmesh_slot(vmesh_in, side, 0, segment).position;
            const quader::QVec3 previous = edge_bevel_vmesh_slot(vmesh_in, side, 0, segment - 1).position;
            const quader::QVec3 next = edge_bevel_vmesh_slot(vmesh_in, side, 0, segment + 1).position;
            position += ((previous + next) - (position * 2.0F)) * (-1.0F / 6.0F);
            edge_bevel_vmesh_canonical_slot(vmesh_out, side, 0, segment * 2).position = position;
        }
    }

    for (int side = 0; side < side_count; ++side) {
        for (int segment = 1; segment < output_segments; segment += 2) {
            quader::QVec3 position = edge_bevel_vmesh_profile_point(vmesh_out, side, segment, output_segments, profile, profile_type);
            const quader::QVec3 previous = edge_bevel_vmesh_canonical_slot(vmesh_out, side, 0, segment - 1).position;
            const quader::QVec3 next = edge_bevel_vmesh_canonical_slot(vmesh_out, side, 0, segment + 1).position;
            position += ((previous + next) - (position * 2.0F)) * (-1.0F / 6.0F);
            edge_bevel_vmesh_canonical_slot(vmesh_out, side, 0, segment).position = position;
        }
    }
    edge_bevel_vmesh_copy_equivalent_slots(vmesh_out);

    for (int side = 0; side < side_count; ++side) {
        for (int segment = 0; segment < input_segments; ++segment) {
            edge_bevel_vmesh_slot(vmesh_in, side, 0, segment).position =
                edge_bevel_vmesh_slot(vmesh_out, side, 0, segment * 2).position;
        }
    }
    edge_bevel_vmesh_copy_equivalent_slots(vmesh_in);

    for (int side = 0; side < side_count; ++side) {
        for (int ring = 0; ring < input_half_segments; ++ring) {
            for (int segment = 0; segment < input_half_segments; ++segment) {
                edge_bevel_vmesh_slot(vmesh_out, side, (ring * 2) + 1, (segment * 2) + 1).position =
                    edge_bevel_average4(
                        edge_bevel_vmesh_slot(vmesh_in, side, ring, segment).position,
                        edge_bevel_vmesh_slot(vmesh_in, side, ring, segment + 1).position,
                        edge_bevel_vmesh_slot(vmesh_in, side, ring + 1, segment).position,
                        edge_bevel_vmesh_slot(vmesh_in, side, ring + 1, segment + 1).position);
            }
        }
    }

    for (int side = 0; side < side_count; ++side) {
        for (int ring = 0; ring < input_half_segments; ++ring) {
            for (int segment = 1; segment <= input_half_segments; ++segment) {
                edge_bevel_vmesh_slot(vmesh_out, side, (ring * 2) + 1, segment * 2).position =
                    edge_bevel_average4(
                        edge_bevel_vmesh_slot(vmesh_in, side, ring, segment).position,
                        edge_bevel_vmesh_slot(vmesh_in, side, ring + 1, segment).position,
                        edge_bevel_vmesh_canonical_slot(vmesh_out, side, (ring * 2) + 1, (segment * 2) - 1).position,
                        edge_bevel_vmesh_canonical_slot(vmesh_out, side, (ring * 2) + 1, (segment * 2) + 1).position);
            }
        }
    }

    for (int side = 0; side < side_count; ++side) {
        for (int ring = 1; ring < input_half_segments; ++ring) {
            for (int segment = 0; segment < input_half_segments; ++segment) {
                edge_bevel_vmesh_slot(vmesh_out, side, ring * 2, (segment * 2) + 1).position =
                    edge_bevel_average4(
                        edge_bevel_vmesh_slot(vmesh_in, side, ring, segment).position,
                        edge_bevel_vmesh_slot(vmesh_in, side, ring, segment + 1).position,
                        edge_bevel_vmesh_canonical_slot(vmesh_out, side, (ring * 2) - 1, (segment * 2) + 1).position,
                        edge_bevel_vmesh_canonical_slot(vmesh_out, side, (ring * 2) + 1, (segment * 2) + 1).position);
            }
        }
    }

    constexpr float kOrdinaryGamma = 0.25F;
    constexpr float kOrdinaryBeta = -kOrdinaryGamma;
    for (int side = 0; side < side_count; ++side) {
        for (int ring = 1; ring < input_half_segments; ++ring) {
            for (int segment = 1; segment <= input_half_segments; ++segment) {
                const quader::QVec3 edge_centroid = edge_bevel_average4(
                    edge_bevel_vmesh_canonical_slot(vmesh_out, side, ring * 2, (segment * 2) - 1).position,
                    edge_bevel_vmesh_canonical_slot(vmesh_out, side, ring * 2, (segment * 2) + 1).position,
                    edge_bevel_vmesh_canonical_slot(vmesh_out, side, (ring * 2) - 1, segment * 2).position,
                    edge_bevel_vmesh_canonical_slot(vmesh_out, side, (ring * 2) + 1, segment * 2).position);
                const quader::QVec3 face_centroid = edge_bevel_average4(
                    edge_bevel_vmesh_canonical_slot(vmesh_out, side, (ring * 2) - 1, (segment * 2) - 1).position,
                    edge_bevel_vmesh_canonical_slot(vmesh_out, side, (ring * 2) + 1, (segment * 2) - 1).position,
                    edge_bevel_vmesh_canonical_slot(vmesh_out, side, (ring * 2) - 1, (segment * 2) + 1).position,
                    edge_bevel_vmesh_canonical_slot(vmesh_out, side, (ring * 2) + 1, (segment * 2) + 1).position);
                edge_bevel_vmesh_slot(vmesh_out, side, ring * 2, segment * 2)
                    .position =
                    edge_centroid + (face_centroid * kOrdinaryBeta) +
                    (edge_bevel_vmesh_slot(vmesh_in, side, ring, segment)
                         .position *
                     kOrdinaryGamma);
            }
        }
    }
    edge_bevel_vmesh_copy_equivalent_slots(vmesh_out);

    const float center_gamma = edge_bevel_sabin_gamma(side_count);
    const float center_beta = -center_gamma;
    quader::QVec3 edge_accumulator;
    quader::QVec3 face_accumulator;
    for (int side = 0; side < side_count; ++side) {
        edge_accumulator += edge_bevel_vmesh_slot(vmesh_out, side, input_segments, input_segments - 1).position;
        face_accumulator += edge_bevel_vmesh_slot(vmesh_out, side, input_segments - 1, input_segments - 1).position;
        face_accumulator += edge_bevel_vmesh_slot(vmesh_out, side, input_segments - 1, input_segments + 1).position;
    }
    const quader::QVec3 center_position =
        (edge_accumulator / static_cast<float>(side_count)) +
        (face_accumulator * (center_beta / (2.0F * static_cast<float>(side_count)))) +
        (edge_bevel_vmesh_slot(vmesh_in, 0, input_half_segments, input_half_segments).position * center_gamma);
    for (int side = 0; side < side_count; ++side) {
        edge_bevel_vmesh_slot(vmesh_out, side, input_segments, input_segments).position = center_position;
    }

    for (int side = 0; side < side_count; ++side) {
        for (int segment = 0; segment <= output_segments; ++segment) {
            edge_bevel_vmesh_canonical_slot(vmesh_out, side, 0, segment).position =
                edge_bevel_vmesh_profile_point(vmesh_out, side, segment, output_segments, profile, profile_type);
        }
    }
    edge_bevel_vmesh_copy_equivalent_slots(vmesh_out);
    return vmesh_out;
}

quader::QVec3 edge_bevel_vmesh_center(const EdgeBevelVMesh& vmesh)
{
    const int half_segments = vmesh.segments / 2;
    if ((vmesh.segments % 2) == 0) {
        return edge_bevel_vmesh_slot(vmesh, 0, half_segments, half_segments).position;
    }

    quader::QVec3 center;
    for (int side = 0; side < vmesh.side_count; ++side) {
        center += edge_bevel_vmesh_slot(vmesh, side, half_segments, half_segments).position;
    }
    return center / static_cast<float>(vmesh.side_count);
}

EdgeBevelVMesh edge_bevel_vmesh_interpolate(
    const EdgeBevelVMesh& vmesh_in,
    int target_segments,
    float profile,
    BevelProfileType profile_type)
{
    QDR_PROFILE_SCOPE("qdr_document.edge_bevel_vmesh_interpolate");
    const int side_count = vmesh_in.side_count;
    const int target_half_segments = target_segments / 2;
    const bool odd = (target_segments % 2) == 1;
    EdgeBevelVMesh vmesh_out = edge_bevel_make_vmesh(side_count, target_segments, vmesh_in.profiles);

    std::vector<float> previous_fractions = edge_bevel_vmesh_boundary_fractions(vmesh_in, side_count - 1);
    std::vector<float> previous_new_fractions =
        edge_bevel_vmesh_profile_fractions(vmesh_out, side_count - 1, target_segments, profile, profile_type);
    for (int side = 0; side < side_count; ++side) {
        std::vector<float> fractions = edge_bevel_vmesh_boundary_fractions(vmesh_in, side);
        std::vector<float> new_fractions =
            edge_bevel_vmesh_profile_fractions(vmesh_out, side, target_segments, profile, profile_type);

        for (int ring = 0; ring <= target_half_segments - 1 + (odd ? 1 : 0); ++ring) {
            for (int segment = 0; segment <= target_half_segments; ++segment) {
                float rest_segment = 0.0F;
                float rest_previous = 0.0F;
                const int source_segment =
                    edge_bevel_vmesh_interp_range(fractions, new_fractions[static_cast<std::size_t>(segment)], rest_segment);
                const int previous_source_segment = edge_bevel_vmesh_interp_range(
                    previous_fractions,
                    previous_new_fractions[static_cast<std::size_t>(target_segments - ring)],
                    rest_previous);

                int source_ring = vmesh_in.segments - previous_source_segment;
                float rest_ring = -rest_previous;
                if (rest_ring > -kEpsilon) {
                  rest_ring = 0.0F;
                } else {
                  --source_ring;
                  rest_ring = 1.0F + rest_ring;
                }

                quader::QVec3 position;
                if (rest_ring < kEpsilon && rest_segment < kEpsilon) {
                  position = edge_bevel_vmesh_canonical_slot(
                                 vmesh_in, side, source_ring, source_segment)
                                 .position;
                } else {
                  const int ring_increment =
                      (rest_ring < kEpsilon || source_ring == vmesh_in.segments)
                          ? 0
                          : 1;
                  const int segment_increment =
                      (rest_segment < kEpsilon ||
                       source_segment == vmesh_in.segments)
                          ? 0
                          : 1;
                  position = edge_bevel_bilinear(
                      edge_bevel_vmesh_canonical_slot(
                          vmesh_in, side, source_ring, source_segment)
                          .position,
                      edge_bevel_vmesh_canonical_slot(
                          vmesh_in, side, source_ring,
                          source_segment + segment_increment)
                          .position,
                      edge_bevel_vmesh_canonical_slot(
                          vmesh_in, side, source_ring + ring_increment,
                          source_segment + segment_increment)
                          .position,
                      edge_bevel_vmesh_canonical_slot(
                          vmesh_in, side, source_ring + ring_increment,
                          source_segment)
                          .position,
                      rest_segment, rest_ring);
                }
                edge_bevel_vmesh_slot(vmesh_out, side, ring, segment).position = position;
            }
        }

        previous_fractions = std::move(fractions);
        previous_new_fractions = std::move(new_fractions);
    }

    if (!odd) {
        edge_bevel_vmesh_slot(vmesh_out, 0, target_half_segments, target_half_segments).position =
            edge_bevel_vmesh_center(vmesh_in);
    }
    edge_bevel_vmesh_copy_equivalent_slots(vmesh_out);
    return vmesh_out;
}

std::vector<EdgeBevelVMeshProfile> edge_bevel_tri_cube_corner_profiles()
{
    return {
        { { 1.0F, 1.0F, 0.0F }, { 1.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F } },
        { { 0.0F, 1.0F, 1.0F }, { 0.0F, 1.0F, 0.0F }, { 0.0F, 0.0F, 1.0F } },
        { { 1.0F, 0.0F, 1.0F }, { 0.0F, 0.0F, 1.0F }, { 1.0F, 0.0F, 0.0F } },
    };
}

EdgeBevelVMesh edge_bevel_make_tri_cube_corner_square_vmesh(int segments)
{
    EdgeBevelVMesh vmesh = edge_bevel_make_vmesh(3, segments, edge_bevel_tri_cube_corner_profiles());
    const int half_segments = segments / 2;
    for (int side = 0; side < vmesh.side_count; ++side) {
        for (int ring = 0; ring <= half_segments; ++ring) {
            for (int segment = 0; segment <= half_segments; ++segment) {
                if (!edge_bevel_vmesh_is_canonical(vmesh, side, ring, segment)) {
                    continue;
                }
                quader::QVec3 position;
                edge_bevel_set_component(position, side, 1.0F);
                edge_bevel_set_component(position, side + 1, static_cast<float>(segment) * 2.0F / static_cast<float>(segments));
                edge_bevel_set_component(position, side + 2, static_cast<float>(ring) * 2.0F / static_cast<float>(segments));
                edge_bevel_vmesh_slot(vmesh, side, ring, segment).position = position;
            }
        }
    }
    edge_bevel_vmesh_copy_equivalent_slots(vmesh);
    return vmesh;
}

EdgeBevelVMesh edge_bevel_make_tri_cube_corner_vmesh(int segments, float profile, BevelProfileType profile_type)
{
    QDR_PROFILE_SCOPE("qdr_document.edge_bevel_make_tri_cube_corner_vmesh");
    if (edge_bevel_superellipse_exponent(profile) == kEdgeBevelSquareExponent) {
      return edge_bevel_make_tri_cube_corner_square_vmesh(segments);
    }

    std::vector<EdgeBevelVMeshProfile> profiles = edge_bevel_tri_cube_corner_profiles();

    EdgeBevelVMesh vmesh = edge_bevel_make_vmesh(3, 2, std::move(profiles));
    for (int side = 0; side < vmesh.side_count; ++side) {
        edge_bevel_vmesh_slot(vmesh, side, 0, 0).position =
            vmesh.profiles[static_cast<std::size_t>(side)].start;
        edge_bevel_vmesh_slot(vmesh, side, 0, 1).position =
            edge_bevel_vmesh_profile_point(vmesh, side, 1, 2, profile, profile_type);
    }

    const float exponent = edge_bevel_superellipse_exponent(profile);
    quader::QVec3 center { std::sqrt(1.0F / 3.0F), std::sqrt(1.0F / 3.0F), std::sqrt(1.0F / 3.0F) };
    if (segments > 2) {
        if (exponent > 1.5F) {
            center = center * 1.4F;
        } else if (exponent < 0.75F) {
            center = center * 0.6F;
        }
    }
    edge_bevel_vmesh_slot(vmesh, 0, 1, 1).position = center;
    edge_bevel_vmesh_copy_equivalent_slots(vmesh);

    while (vmesh.segments < segments) {
        vmesh = edge_bevel_vmesh_cubic_subdiv(std::move(vmesh), profile, profile_type);
    }
    if (vmesh.segments != segments) {
        vmesh = edge_bevel_vmesh_interpolate(vmesh, segments, profile, profile_type);
    }

    const int half_segments = segments / 2;
    for (int side = 0; side < vmesh.side_count; ++side) {
        for (int ring = 0; ring <= half_segments; ++ring) {
            for (int segment = 0; segment <= segments; ++segment) {
                edge_bevel_vmesh_slot(vmesh, side, ring, segment).position =
                    edge_bevel_snap_to_superellipsoid(
                        edge_bevel_vmesh_slot(vmesh, side, ring, segment).position,
                        exponent);
            }
        }
    }
    edge_bevel_vmesh_copy_equivalent_slots(vmesh);
    return vmesh;
}

bool append_edge_bevel_vmesh_faces(
    Document& document,
    std::vector<Face>& faces,
    std::vector<ElementId>& generated_face_ids,
    EdgeBevelVMesh& vmesh,
    std::uint32_t material_slot,
    quader::QVec3 expected_normal,
    std::optional<quader::QVec3> orientation_source,
    bool orient_away_from_source)
{
    QDR_PROFILE_SCOPE("qdr_document.append_edge_bevel_vmesh_faces");
    auto vertex_at = [&](int side, int ring, int segment) -> ElementId {
        EdgeBevelVMeshSlot& slot = edge_bevel_vmesh_canonical_slot(vmesh, side, ring, segment);
        if (slot.vertex_id == kInvalidElementId) {
          slot.vertex_id = add_vertex(document, slot.position);
        }
        return slot.vertex_id;
    };

    auto append_vmesh_face = [&](std::vector<ElementId> vertices) {
        quader::QVec3 face_expected_normal = expected_normal;
        if (orientation_source.has_value()) {
            Face probe;
            probe.vertices = vertices;
            const quader::QVec3 from_source = face_centroid(document, probe) - *orientation_source;
            if (length_squared(from_source) > kEpsilon) {
              face_expected_normal =
                  orient_away_from_source ? from_source : from_source * -1.0F;
            }
        }

        return append_edge_bevel_face(
            document,
            faces,
            generated_face_ids,
            std::move(vertices),
            material_slot,
            face_expected_normal);
    };

    bool appended = false;
    const int half_segments = vmesh.segments / 2;
    const bool odd = (vmesh.segments % 2) == 1;
    for (int side = 0; side < vmesh.side_count; ++side) {
        for (int ring = 0; ring < half_segments; ++ring) {
            for (int segment = 0; segment < half_segments + (odd ? 1 : 0); ++segment) {
                appended |= append_vmesh_face({
                        vertex_at(side, ring, segment),
                        vertex_at(side, ring, segment + 1),
                        vertex_at(side, ring + 1, segment + 1),
                        vertex_at(side, ring + 1, segment),
                    });
            }
        }
    }

    if (odd) {
        std::vector<ElementId> center_vertices;
        center_vertices.reserve(static_cast<std::size_t>(vmesh.side_count));
        for (int side = 0; side < vmesh.side_count; ++side) {
            center_vertices.push_back(vertex_at(side, half_segments, half_segments));
        }
        appended |= append_vmesh_face(std::move(center_vertices));
    }

    return appended;
}

bool edge_bevel_vertices_match(const Document& document, ElementId first_id, ElementId second_id)
{
    if (first_id == second_id) {
        return true;
    }
    const Vertex* first = find_vertex(document, first_id);
    const Vertex* second = find_vertex(document, second_id);
    return first != nullptr && second != nullptr && length_squared(first->position - second->position) <= 0.000001F;
}

std::optional<std::vector<EdgeBevelCornerArc>> ordered_edge_bevel_corner_arcs(
    const Document& document,
    std::vector<EdgeBevelCornerArc> arcs,
    int segments)
{
    if (arcs.size() < 3U) {
        return std::nullopt;
    }
    for (const EdgeBevelCornerArc& arc : arcs) {
        if (arc.vertices.size() != static_cast<std::size_t>(segments + 1)) {
            return std::nullopt;
        }
    }

    auto reversed_arc = [](EdgeBevelCornerArc arc) {
        std::ranges::reverse(arc.vertices);
        return arc;
    };

    for (std::size_t start_index = 0; start_index < arcs.size(); ++start_index) {
        for (const bool reverse_start : { false, true }) {
            std::vector<bool> used(arcs.size(), false);
            std::vector<EdgeBevelCornerArc> ordered;
            ordered.reserve(arcs.size());
            ordered.push_back(reverse_start ? reversed_arc(arcs[start_index]) : arcs[start_index]);
            used[start_index] = true;

            while (ordered.size() < arcs.size()) {
                const ElementId end_id = ordered.back().vertices.back();
                bool found_next = false;
                for (std::size_t index = 0; index < arcs.size(); ++index) {
                    if (used[index]) {
                        continue;
                    }
                    if (edge_bevel_vertices_match(document, end_id, arcs[index].vertices.front())) {
                        ordered.push_back(arcs[index]);
                        used[index] = true;
                        found_next = true;
                        break;
                    }
                    if (edge_bevel_vertices_match(document, end_id, arcs[index].vertices.back())) {
                        ordered.push_back(reversed_arc(arcs[index]));
                        used[index] = true;
                        found_next = true;
                        break;
                    }
                }
                if (!found_next) {
                    break;
                }
            }

            if (ordered.size() == arcs.size() &&
                edge_bevel_vertices_match(document, ordered.back().vertices.back(), ordered.front().vertices.front())) {
                return ordered;
            }
        }
    }
    return std::nullopt;
}

bool append_edge_bevel_tri_corner_arc_patch_faces(
    Document& document,
    std::vector<Face>& faces,
    std::vector<ElementId>& generated_face_ids,
    const std::vector<EdgeBevelCornerArc>& ordered_arcs,
    ElementId source_vertex_id,
    std::uint32_t material_slot,
    quader::QVec3 expected_normal,
    int segments,
    float profile)
{
    QDR_PROFILE_SCOPE("qdr_document.append_edge_bevel_tri_corner_arc_patch_faces");
    if (ordered_arcs.size() != 3U || segments <= 1) {
        return false;
    }

    const Vertex* source_vertex = find_vertex(document, source_vertex_id);
    const Vertex* corner0 = find_vertex(document, ordered_arcs[0].vertices.front());
    const Vertex* corner1 = find_vertex(document, ordered_arcs[1].vertices.front());
    const Vertex* corner2 = find_vertex(document, ordered_arcs[2].vertices.front());
    if (source_vertex == nullptr || corner0 == nullptr || corner1 == nullptr || corner2 == nullptr) {
        return false;
    }
    for (const EdgeBevelCornerArc& arc : ordered_arcs) {
        if (arc.vertices.size() != static_cast<std::size_t>(segments + 1)) {
            return false;
        }
    }

    const float exponent = edge_bevel_superellipse_exponent(profile);
    if (segments == 2 && exponent == kEdgeBevelSquareInExponent) {
      const ElementId pole_id = ordered_arcs[0].vertices[1];
      // Blender's square-in tri-corner path collapses the middle row into a
      // shared pole and leaves the edge strips to close the corner, avoiding a
      // crossed fallback patch.
      if (std::ranges::all_of(
              ordered_arcs,
              [&document, pole_id](const EdgeBevelCornerArc &arc) {
                return arc.vertices.size() == 3U && edge_bevel_vertices_match(document, pole_id, arc.vertices[1]);
              })) {
        return true;
      }
    }

    EdgeBevelVMesh vmesh = edge_bevel_make_tri_cube_corner_vmesh(
        segments, profile, BevelProfileType::Offset);
    const int half_segments = segments / 2;
    for (int side = 0; side < vmesh.side_count; ++side) {
        for (int ring = 0; ring <= half_segments; ++ring) {
            for (int segment = 0; segment <= segments; ++segment) {
                EdgeBevelVMeshSlot& slot = edge_bevel_vmesh_slot(vmesh, side, ring, segment);
                slot.position = edge_bevel_map_unit_cube_corner(
                    corner0->position,
                    corner1->position,
                    corner2->position,
                    source_vertex->position,
                    slot.position);
                slot.vertex_id = kInvalidElementId;
            }
        }
    }

    for (int side = 0; side < vmesh.side_count; ++side) {
        for (int segment = 0; segment <= segments; ++segment) {
            const ElementId vertex_id =
                ordered_arcs[static_cast<std::size_t>(side)].vertices[static_cast<std::size_t>(segment)];
            const Vertex* vertex = find_vertex(document, vertex_id);
            if (vertex == nullptr) {
                return false;
            }
            EdgeBevelVMeshSlot& slot = edge_bevel_vmesh_canonical_slot(vmesh, side, 0, segment);
            slot.position = vertex->position;
            slot.vertex_id = vertex_id;
        }
    }
    edge_bevel_vmesh_copy_equivalent_slots(vmesh);

    const quader::QVec3 reference_center = (corner0->position + corner1->position + corner2->position) / 3.0F;
    const bool orient_away_from_source =
        dot(expected_normal, reference_center - source_vertex->position) >= 0.0F;
    return append_edge_bevel_vmesh_faces(
        document,
        faces,
        generated_face_ids,
        vmesh,
        material_slot,
        expected_normal,
        source_vertex->position,
        orient_away_from_source);
}

bool append_edge_bevel_vmesh_corner_patch_faces(
    Document& document,
    std::vector<Face>& faces,
    std::vector<ElementId>& generated_face_ids,
    std::vector<EdgeBevelCornerArc> arcs,
    ElementId source_vertex_id,
    std::uint32_t material_slot,
    quader::QVec3 expected_normal,
    int segments,
    float profile,
    BevelProfileType profile_type)
{
    QDR_PROFILE_SCOPE("qdr_document.append_edge_bevel_vmesh_corner_patch_faces");
    if (segments <= 1) {
        return false;
    }
    std::optional<std::vector<EdgeBevelCornerArc>> ordered_arcs =
        ordered_edge_bevel_corner_arcs(document, std::move(arcs), segments);
    if (!ordered_arcs.has_value()) {
        return false;
    }

    const Vertex* source_vertex = find_vertex(document, source_vertex_id);
    if (source_vertex == nullptr) {
        return false;
    }

    if (ordered_arcs->size() == 3U) {
        return append_edge_bevel_tri_corner_arc_patch_faces(
            document,
            faces,
            generated_face_ids,
            *ordered_arcs,
            source_vertex_id,
            material_slot,
            expected_normal,
            segments,
            profile);
    }

    std::vector<EdgeBevelVMeshProfile> profiles;
    profiles.reserve(ordered_arcs->size());
    quader::QVec3 bound_center;
    for (const EdgeBevelCornerArc& arc : *ordered_arcs) {
        const Vertex* start = find_vertex(document, arc.vertices.front());
        const Vertex* end = find_vertex(document, arc.vertices.back());
        if (start == nullptr || end == nullptr) {
            return false;
        }
        EdgeBevelVMeshProfile vmesh_profile;
        vmesh_profile.source = arc.has_profile_middle ? arc.profile_middle : source_vertex->position;
        vmesh_profile.start = start->position;
        vmesh_profile.end = end->position;
        vmesh_profile.use_global_profile = arc.use_global_profile;
        vmesh_profile.profile = arc.profile;
        vmesh_profile.profile_type = arc.profile_type;
        if (arc.edge.a != kInvalidElementId &&
            arc.edge.b != kInvalidElementId) {
          const Vertex *edge_a = find_vertex(document, arc.edge.a);
          const Vertex *edge_b = find_vertex(document, arc.edge.b);
          const quader::QVec3 first_delta =
              normalize_or_zero(vmesh_profile.source - vmesh_profile.start);
          const quader::QVec3 second_delta =
              normalize_or_zero(vmesh_profile.source - vmesh_profile.end);
          const quader::QVec3 plane_normal =
              normalize_or_zero(cross(first_delta, second_delta));
          const quader::QVec3 projection_direction =
              edge_a != nullptr && edge_b != nullptr
                  ? normalize_or_zero(edge_a->position - edge_b->position)
                  : quader::QVec3{};
          if (length_squared(plane_normal) > kEpsilon &&
              length_squared(projection_direction) > kEpsilon &&
              std::abs(dot(plane_normal, projection_direction)) > kEpsilon) {
            vmesh_profile.plane_co = vmesh_profile.start;
            vmesh_profile.plane_normal = plane_normal;
            vmesh_profile.projection_direction = projection_direction;
            vmesh_profile.has_projection = true;
          }
        }
        profiles.push_back(vmesh_profile);
        bound_center += start->position;
    }
    bound_center = bound_center / static_cast<float>(profiles.size());

    EdgeBevelVMesh vmesh = edge_bevel_make_vmesh(static_cast<int>(profiles.size()), 2, profiles);
    for (int side = 0; side < vmesh.side_count; ++side) {
        edge_bevel_vmesh_slot(vmesh, side, 0, 0).position = profiles[static_cast<std::size_t>(side)].start;
        edge_bevel_vmesh_slot(vmesh, side, 0, 1).position =
            edge_bevel_vmesh_profile_point(vmesh, side, 1, 2, profile, profile_type);
    }

    const quader::QVec3 center_direction = source_vertex->position - bound_center;
    const float fullness = edge_bevel_profile_fullness(segments, profile);
    edge_bevel_vmesh_slot(vmesh, 0, 1, 1).position =
        length_squared(center_direction) > kEpsilon
            ? bound_center + (center_direction * fullness)
            : bound_center;
    edge_bevel_vmesh_copy_equivalent_slots(vmesh);

    do {
        vmesh = edge_bevel_vmesh_cubic_subdiv(std::move(vmesh), profile, profile_type);
    } while (vmesh.segments < segments);
    if (vmesh.segments != segments) {
        vmesh = edge_bevel_vmesh_interpolate(vmesh, segments, profile, profile_type);
    }

    for (int side = 0; side < vmesh.side_count; ++side) {
        const EdgeBevelCornerArc& arc = (*ordered_arcs)[static_cast<std::size_t>(side)];
        for (int segment = 0; segment <= segments; ++segment) {
            const ElementId vertex_id = arc.vertices[static_cast<std::size_t>(segment)];
            const Vertex* vertex = find_vertex(document, vertex_id);
            if (vertex == nullptr) {
                return false;
            }
            EdgeBevelVMeshSlot& slot = edge_bevel_vmesh_canonical_slot(vmesh, side, 0, segment);
            slot.position = vertex->position;
            slot.vertex_id = vertex_id;
        }
    }
    edge_bevel_vmesh_copy_equivalent_slots(vmesh);
    const bool orient_away_from_source =
        dot(expected_normal, bound_center - source_vertex->position) >= 0.0F;
    return append_edge_bevel_vmesh_faces(
        document,
        faces,
        generated_face_ids,
        vmesh,
        material_slot,
        expected_normal,
        source_vertex->position,
        orient_away_from_source);
}

bool append_edge_bevel_corner_patch_faces(
    Document& document,
    std::vector<Face>& faces,
    std::vector<ElementId>& generated_face_ids,
    std::vector<ElementId> patch_vertices,
    ElementId source_vertex_id,
    int selected_edge_count,
    std::uint32_t material_slot,
    quader::QVec3 expected_normal,
    int segments,
    float profile,
    BevelProfileType profile_type)
{
    QDR_PROFILE_SCOPE("qdr_document.append_edge_bevel_corner_patch_faces");
    patch_vertices = compact_edge_bevel_face_loop(std::move(patch_vertices));
    if (patch_vertices.size() < 3) {
        return false;
    }
    if (segments <= 1) {
        return append_edge_bevel_face(
            document,
            faces,
            generated_face_ids,
            std::move(patch_vertices),
            material_slot,
            expected_normal);
    }

    if (selected_edge_count == 3 &&
        append_edge_bevel_tri_corner_patch_faces(
            document,
            faces,
            generated_face_ids,
            patch_vertices,
            source_vertex_id,
            material_slot,
            expected_normal,
            segments,
            profile,
            profile_type)) {
        return true;
    }

    const Vertex* source_vertex = find_vertex(document, source_vertex_id);
    if (source_vertex == nullptr || selected_edge_count < 2) {
        return false;
    }

    std::vector<ElementId> inner_vertices = patch_vertices;
    std::ranges::sort(inner_vertices, [&document, source_position = source_vertex->position](ElementId left_id, ElementId right_id) {
        const Vertex* left = find_vertex(document, left_id);
        const Vertex* right = find_vertex(document, right_id);
        const float left_distance = left != nullptr ? length_squared(left->position - source_position) : std::numeric_limits<float>::max();
        const float right_distance = right != nullptr ? length_squared(right->position - source_position) : std::numeric_limits<float>::max();
        return left_distance < right_distance;
    });
    inner_vertices.resize(std::min<std::size_t>(inner_vertices.size(), static_cast<std::size_t>(selected_edge_count)));

    std::set<ElementId> inner_vertex_set(inner_vertices.begin(), inner_vertices.end());
    std::vector<ElementId> ordered_inner_vertices;
    ordered_inner_vertices.reserve(inner_vertices.size());
    for (const ElementId vertex_id : patch_vertices) {
        if (inner_vertex_set.contains(vertex_id)) {
            ordered_inner_vertices.push_back(vertex_id);
        }
    }

    if (segments == 2 && ordered_inner_vertices.size() >= 3) {
        bool appended = append_edge_bevel_face(
            document,
            faces,
            generated_face_ids,
            ordered_inner_vertices,
            material_slot,
            expected_normal);
        const std::size_t vertex_count = patch_vertices.size();
        for (std::size_t index = 0; index < vertex_count; ++index) {
            const ElementId outer_id = patch_vertices[index];
            if (inner_vertex_set.contains(outer_id)) {
                continue;
            }
            const ElementId previous_id = patch_vertices[(index + vertex_count - 1U) % vertex_count];
            const ElementId next_id = patch_vertices[(index + 1U) % vertex_count];
            if (!inner_vertex_set.contains(previous_id) || !inner_vertex_set.contains(next_id)) {
                continue;
            }
            appended |= append_edge_bevel_face(
                document,
                faces,
                generated_face_ids,
                { previous_id, outer_id, next_id },
                material_slot,
                expected_normal);
        }
        return appended;
    }

    quader::QVec3 center_position;
    std::size_t center_count = 0;
    const std::vector<ElementId>& center_source_vertices = ordered_inner_vertices.empty() ? patch_vertices : ordered_inner_vertices;
    for (const ElementId vertex_id : center_source_vertices) {
        const Vertex* vertex = find_vertex(document, vertex_id);
        if (vertex == nullptr) {
            continue;
        }
        center_position += vertex->position;
        ++center_count;
    }
    if (center_count == 0) {
        return false;
    }
    center_position = center_position / static_cast<float>(center_count);
    const ElementId center_id = add_vertex(document, center_position);

    bool appended = false;
    const std::size_t vertex_count = patch_vertices.size();
    for (std::size_t index = 0; index < vertex_count; ++index) {
        appended |= append_edge_bevel_face(
            document,
            faces,
            generated_face_ids,
            {
                patch_vertices[index],
                patch_vertices[(index + 1U) % vertex_count],
                center_id,
            },
            material_slot,
            expected_normal);
    }
    return appended;
}

void add_unique_edge_bevel_patch_vertex(std::vector<std::pair<ElementId, quader::QVec3>>& points, ElementId vertex_id, quader::QVec3 position)
{
  if (vertex_id == kInvalidElementId) {
    return;
  }
    const auto existing = std::ranges::find_if(points, [vertex_id, position](const std::pair<ElementId, quader::QVec3>& point) {
        return point.first == vertex_id || length_squared(point.second - position) <= 0.000001F;
    });
    if (existing == points.end()) {
        points.emplace_back(vertex_id, position);
    }
}

std::vector<ElementId> ordered_edge_bevel_patch_vertices(
    const Document& document,
    ElementId source_vertex_id,
    std::vector<std::pair<ElementId, quader::QVec3>> points)
{
    if (points.size() < 3) {
        std::vector<ElementId> ids;
        ids.reserve(points.size());
        for (const auto& point : points) {
            ids.push_back(point.first);
        }
        return ids;
    }

    const Vertex* source_vertex = find_vertex(document, source_vertex_id);
    if (source_vertex == nullptr) {
        std::vector<ElementId> ids;
        ids.reserve(points.size());
        for (const auto& point : points) {
            ids.push_back(point.first);
        }
        return ids;
    }

    quader::QVec3 centroid;
    for (const auto& point : points) {
        centroid += point.second;
    }
    centroid = centroid / static_cast<float>(points.size());

    quader::QVec3 axis = normalize_or_zero(centroid - source_vertex->position);
    if (length_squared(axis) <= kEpsilon) {
      axis = {0.0F, 1.0F, 0.0F};
    }

    quader::QVec3 reference = normalize_or_zero(points.front().second - source_vertex->position);
    reference = normalize_or_zero(reference - (axis * dot(reference, axis)));
    if (length_squared(reference) <= kEpsilon) {
      reference = std::abs(axis.y) < 0.9F
                      ? normalize_or_zero(cross(axis, {0.0F, 1.0F, 0.0F}))
                      : normalize_or_zero(cross(axis, {1.0F, 0.0F, 0.0F}));
    }
    const quader::QVec3 bitangent = normalize_or_zero(cross(axis, reference));

    std::ranges::sort(points, [source_position = source_vertex->position, reference, bitangent](const auto& left, const auto& right) {
        const quader::QVec3 left_direction = normalize_or_zero(left.second - source_position);
        const quader::QVec3 right_direction = normalize_or_zero(right.second - source_position);
        const float left_angle = std::atan2(dot(left_direction, bitangent), dot(left_direction, reference));
        const float right_angle = std::atan2(dot(right_direction, bitangent), dot(right_direction, reference));
        return left_angle < right_angle;
    });

    std::vector<ElementId> ids;
    ids.reserve(points.size());
    for (const auto& point : points) {
        ids.push_back(point.first);
    }
    return ids;
}

bool edge_bevel_point_lies_on_face_plane(const Document& document, const Face& face, quader::QVec3 position)
{
  const quader_geometry::QVec3f normal = quader_geometry::normalize_or_zero(
      geometry_vec3(face_normal(document, face)), kEpsilon);
  if (quader_geometry::length_squared(normal) <= kEpsilon) {
    return false;
  }

    for (const ElementId vertex_id : face.vertices) {
        const Vertex* vertex = find_vertex(document, vertex_id);
        if (vertex == nullptr) {
            continue;
        }
        const quader_geometry::QPlane3<float> plane =
            quader_geometry::plane_from_point_normal<float>(
                geometry_vec3(vertex->position), normal, kEpsilon);
        return quader_geometry::plane_side<float>(geometry_vec3(position),
                                                  plane, 0.001F) ==
               quader_geometry::QPlaneSide::On;
    }
    return false;
}

bool edge_bevel_point_lies_in_face_corner(
    const Document& document,
    const Face& face,
    ElementId vertex_id,
    ElementId previous_id,
    ElementId next_id,
    quader::QVec3 position)
{
    const Vertex* vertex = find_vertex(document, vertex_id);
    const Vertex* previous = find_vertex(document, previous_id);
    const Vertex* next = find_vertex(document, next_id);
    if (vertex == nullptr || previous == nullptr || next == nullptr) {
        return false;
    }

    const quader::QVec3 normal = normalize_or_zero(face_normal(document, face));
    const quader::QVec3 offset_direction = position - vertex->position;
    if (length_squared(normal) <= kEpsilon ||
        length_squared(offset_direction) <= kEpsilon) {
      return false;
    }

    const quader::QVec3 previous_direction = previous->position - vertex->position;
    const quader::QVec3 next_direction = next->position - vertex->position;
    if (length_squared(previous_direction) <= kEpsilon ||
        length_squared(next_direction) <= kEpsilon) {
      return false;
    }

    constexpr float kCornerEpsilon = -0.001F;
    const float after_previous_edge = dot(cross(previous_direction * -1.0F, offset_direction), normal);
    const float before_next_edge = dot(cross(next_direction, offset_direction), normal);
    return after_previous_edge >= kCornerEpsilon &&
           before_next_edge >= kCornerEpsilon;
}

std::vector<ElementId> edge_bevel_endpoint_profile_vertices(const EdgeBevelBuild& build, ElementId endpoint_id)
{
    int endpoint_index = -1;
    if (build.edge.a == endpoint_id) {
        endpoint_index = 0;
    } else if (build.edge.b == endpoint_id) {
        endpoint_index = 1;
    }
    if (endpoint_index < 0) {
        return {};
    }

    std::vector<ElementId> vertices;
    vertices.reserve(build.rows.size());
    for (const std::array<ElementId, 2>& row : build.rows) {
        vertices.push_back(row[static_cast<std::size_t>(endpoint_index)]);
    }
    return compact_edge_bevel_face_loop(std::move(vertices));
}

bool edge_bevel_endpoint_profile_lies_in_face_corner(
    const Document& document,
    const Face& face,
    ElementId vertex_id,
    ElementId previous_id,
    ElementId next_id,
    std::span<const ElementId> profile_vertices)
{
    if (profile_vertices.size() < 2U) {
        return false;
    }

    return std::ranges::all_of(profile_vertices, [&document, &face, vertex_id, previous_id, next_id](ElementId profile_vertex_id) {
        const Vertex* profile_vertex = find_vertex(document, profile_vertex_id);
        return profile_vertex != nullptr &&
            edge_bevel_point_lies_on_face_plane(document, face, profile_vertex->position) &&
            edge_bevel_point_lies_in_face_corner(document, face, vertex_id, previous_id, next_id, profile_vertex->position);
    });
}

void append_edge_bevel_terminal_profile_vertices(
    const Document& document,
    ElementId vertex_id,
    ElementId previous_id,
    ElementId next_id,
    std::span<const ElementId> profile_vertices,
    std::vector<ElementId>& rebuilt_loop)
{
    const Vertex* vertex = find_vertex(document, vertex_id);
    const Vertex* previous = find_vertex(document, previous_id);
    const Vertex* next = find_vertex(document, next_id);
    if (vertex == nullptr || previous == nullptr || next == nullptr || profile_vertices.size() < 2U) {
        rebuilt_loop.push_back(vertex_id);
        return;
    }

    std::vector<ElementId> ordered(profile_vertices.begin(), profile_vertices.end());
    const auto direction_from_source = [&document, source_position = vertex->position](ElementId profile_vertex_id) {
        const Vertex* profile_vertex = find_vertex(document, profile_vertex_id);
        return profile_vertex != nullptr ? normalize_or_zero(profile_vertex->position - source_position) : quader::QVec3 {};
    };

    const quader::QVec3 previous_direction = normalize_or_zero(previous->position - vertex->position);
    const quader::QVec3 next_direction = normalize_or_zero(next->position - vertex->position);
    const quader::QVec3 first_direction = direction_from_source(ordered.front());
    const quader::QVec3 last_direction = direction_from_source(ordered.back());
    const float forward_score = dot(first_direction, previous_direction) + dot(last_direction, next_direction);
    const float reverse_score = dot(last_direction, previous_direction) + dot(first_direction, next_direction);
    if (reverse_score > forward_score) {
        std::ranges::reverse(ordered);
    }

    for (const ElementId profile_vertex_id : ordered) {
      if (profile_vertex_id == kInvalidElementId) {
        continue;
      }
        if (!rebuilt_loop.empty() && rebuilt_loop.back() == profile_vertex_id) {
            continue;
        }
        rebuilt_loop.push_back(profile_vertex_id);
    }
}

void append_edge_bevel_unselected_vertex_offsets(
    const Document& document,
    const Face& face,
    ElementId vertex_id,
    ElementId previous_id,
    ElementId next_id,
    std::span<const EdgeBevelFaceVertexOffset> offsets,
    std::vector<ElementId>& rebuilt_loop)
{
    const Vertex* vertex = find_vertex(document, vertex_id);
    const Vertex* previous = find_vertex(document, previous_id);
    const Vertex* next = find_vertex(document, next_id);
    if (vertex == nullptr || previous == nullptr || next == nullptr) {
        rebuilt_loop.push_back(vertex_id);
        return;
    }

    /**
     * Represents an Ordered Offset value used by the polygon document and mesh editing core.
     */
    struct OrderedOffset {
        EdgeBevelFaceVertexOffset offset;
        float angle = 0.0F;
        float distance = 0.0F;
    };

    const quader::QVec3 previous_direction = normalize_or_zero(previous->position - vertex->position);
    const quader::QVec3 next_direction = normalize_or_zero(next->position - vertex->position);
    const quader::QVec3 normal = normalize_or_zero(face_normal(document, face));
    const float orientation = dot(cross(previous_direction, next_direction), normal);
    const float orientation_sign = orientation < 0.0F ? -1.0F : 1.0F;
    std::vector<OrderedOffset> ordered_offsets;

    for (const EdgeBevelFaceVertexOffset& offset : offsets) {
      if (offset.source_vertex_id != vertex_id ||
          offset.offset_vertex_id == kInvalidElementId ||
          !edge_bevel_point_lies_on_face_plane(document, face,
                                               offset.position) ||
          !edge_bevel_point_lies_in_face_corner(document, face, vertex_id,
                                                previous_id, next_id,
                                                offset.position)) {
        continue;
      }
        if (contains_id(rebuilt_loop, offset.offset_vertex_id)) {
            continue;
        }

        const quader::QVec3 offset_delta = offset.position - vertex->position;
        const quader::QVec3 offset_direction = normalize_or_zero(offset_delta);
        if (length_squared(offset_direction) <= kEpsilon) {
          continue;
        }
        float angle = std::atan2(
            dot(cross(previous_direction, offset_direction), normal),
            dot(previous_direction, offset_direction)) * orientation_sign;
        if (angle < -0.0001F) {
            angle += 6.28318530717958647692F;
        }
        ordered_offsets.push_back({ offset, angle, length_squared(offset_delta) });
    }

    if (ordered_offsets.empty()) {
        rebuilt_loop.push_back(vertex_id);
        return;
    }

    std::ranges::sort(ordered_offsets, [](const OrderedOffset& left, const OrderedOffset& right) {
        if (std::abs(left.angle - right.angle) > 0.0001F) {
            return left.angle < right.angle;
        }
        return left.distance < right.distance;
    });

    for (const OrderedOffset& offset : ordered_offsets) {
        rebuilt_loop.push_back(offset.offset.offset_vertex_id);
    }
}

std::vector<ElementId> merged_face_loop_for_dissolved_edge(const Face& first_face, const Face& second_face, ElementId first_from, ElementId first_to)
{
    const auto first_start = std::ranges::find(first_face.vertices, first_to);
    const auto first_end = std::ranges::find(first_face.vertices, first_from);
    if (first_start == first_face.vertices.end() || first_end == first_face.vertices.end()) {
        return {};
    }
    std::vector<ElementId> first_path = face_vertices_between(
        first_face,
        static_cast<std::size_t>(std::distance(first_face.vertices.begin(), first_start)),
        static_cast<std::size_t>(std::distance(first_face.vertices.begin(), first_end)));

    const auto second_start = std::ranges::find(second_face.vertices, first_from);
    const auto second_end = std::ranges::find(second_face.vertices, first_to);
    if (second_start == second_face.vertices.end() || second_end == second_face.vertices.end()) {
        return {};
    }
    std::vector<ElementId> second_path = face_vertices_between(
        second_face,
        static_cast<std::size_t>(std::distance(second_face.vertices.begin(), second_start)),
        static_cast<std::size_t>(std::distance(second_face.vertices.begin(), second_end)));

    if (first_path.size() < 2 || second_path.size() < 2) {
        return {};
    }

    std::vector<ElementId> merged_loop = first_path;
    merged_loop.reserve(first_path.size() + second_path.size());
    for (std::size_t index = 1; index + 1U < second_path.size(); ++index) {
        merged_loop.push_back(second_path[index]);
    }
    return unique_valid_face_loop(std::move(merged_loop));
}

std::optional<std::size_t> loop_vertex_index(std::span<const ElementId> loop, ElementId vertex_id)
{
    const auto found = std::ranges::find(loop, vertex_id);
    if (found == loop.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::distance(loop.begin(), found));
}

bool loop_vertex_is_redundant(const Document& document, std::span<const ElementId> loop, std::size_t index)
{
    if (loop.size() <= 3U || index >= loop.size()) {
        return false;
    }

    const Vertex* previous = find_vertex(document, loop[(index + loop.size() - 1U) % loop.size()]);
    const Vertex* current = find_vertex(document, loop[index]);
    const Vertex* next = find_vertex(document, loop[(index + 1U) % loop.size()]);
    if (previous == nullptr || current == nullptr || next == nullptr) {
        return false;
    }

    const quader::QVec3 previous_delta = current->position - previous->position;
    const quader::QVec3 next_delta = next->position - current->position;
    const float previous_length_squared = length_squared(previous_delta);
    const float next_length_squared = length_squared(next_delta);
    if (previous_length_squared <= kEpsilon ||
        next_length_squared <= kEpsilon ||
        dot(previous_delta, next_delta) <= 0.0F) {
      return false;
    }

    const float scale = std::max(previous_length_squared * next_length_squared, 1.0F);
    return length_squared(cross(previous_delta, next_delta)) <= scale * 0.000001F;
}

bool loop_vertex_is_redundant(const Document& document, std::span<const ElementId> loop, ElementId vertex_id)
{
    const std::optional<std::size_t> index = loop_vertex_index(loop, vertex_id);
    return index.has_value() && loop_vertex_is_redundant(document, loop, *index);
}

void append_loop_neighbors_for_vertex(std::span<const ElementId> loop, ElementId vertex_id, std::vector<ElementId>& neighbors)
{
    const std::optional<std::size_t> index = loop_vertex_index(loop, vertex_id);
    if (!index.has_value() || loop.size() < 2U) {
        return;
    }

    add_unique_id(neighbors, loop[(*index + loop.size() - 1U) % loop.size()]);
    add_unique_id(neighbors, loop[(*index + 1U) % loop.size()]);
}

bool vertex_is_redundant_in_every_face_that_uses_it(const Document& document, ElementId vertex_id)
{
    bool used_by_face = false;
    for (const Face& face : document.faces) {
        if (!contains_id(face.vertices, vertex_id)) {
            continue;
        }

        used_by_face = true;
        if (!loop_vertex_is_redundant(document, face.vertices, vertex_id)) {
            return false;
        }
    }
    return used_by_face;
}

std::vector<ElementId> remove_redundant_vertices_from_loop(
    const Document& document,
    std::vector<ElementId> loop,
    const std::set<ElementId>& vertices_to_remove,
    std::vector<ElementId>& removed_vertices)
{
    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t index = 0; index < loop.size(); ++index) {
            const ElementId vertex_id = loop[index];
            if (!vertices_to_remove.contains(vertex_id) || !loop_vertex_is_redundant(document, loop, index)) {
                continue;
            }

            add_unique_id(removed_vertices, vertex_id);
            loop.erase(loop.begin() + static_cast<std::ptrdiff_t>(index));
            changed = true;
            break;
        }
    }
    return unique_valid_face_loop(std::move(loop));
}

bool remove_redundant_vertex_from_all_face_loops(Document& document, ElementId vertex_id)
{
    if (!vertex_is_redundant_in_every_face_that_uses_it(document, vertex_id)) {
        return false;
    }

    bool changed = false;
    const std::set<ElementId> vertex_to_remove { vertex_id };
    for (Face& face : document.faces) {
        if (!contains_id(face.vertices, vertex_id)) {
            continue;
        }

        std::vector<ElementId> removed_vertices;
        std::vector<ElementId> dissolved_loop = remove_redundant_vertices_from_loop(document, face.vertices, vertex_to_remove, removed_vertices);
        if (removed_vertices.empty()) {
            continue;
        }
        if (dissolved_loop.size() < 3U) {
            return false;
        }

        face.vertices = std::move(dissolved_loop);
        face.uvs.clear();
        changed = true;
    }
    return changed;
}

void orient_face_toward_normal(const Document& document, Face& face, quader::QVec3 expected_normal)
{
  if (length_squared(expected_normal) <= kEpsilon) {
    return;
  }
    const quader::QVec3 normal = face_normal(document, face);
    if (length_squared(normal) <= kEpsilon ||
        dot(normal, expected_normal) >= 0.0F) {
      return;
    }
    std::ranges::reverse(face.vertices);
    if (face.uvs.size() == face.vertices.size()) {
        std::ranges::reverse(face.uvs);
    } else {
        face.uvs.clear();
    }
}

void reverse_face_winding(Face& face)
{
    std::ranges::reverse(face.vertices);
    if (face_has_loop_uvs(face)) {
        std::ranges::reverse(face.uvs);
    } else {
        face.uvs.clear();
    }
}

std::vector<Face> selected_face_copies(const Document& document, const Selection& selection)
{
    std::vector<Face> faces;
    std::set<ElementId> visited;
    for (const ElementId face_id : selection.faces) {
        if (visited.contains(face_id)) {
            continue;
        }

        const Face* face = find_face(document, face_id);
        if (face == nullptr || face->vertices.size() < 3) {
            continue;
        }

        visited.insert(face_id);
        faces.push_back(*face);
    }
    return faces;
}

ElementId copied_vertex_id(
    const Document& source,
    Document& target,
    std::map<ElementId, ElementId>& vertex_map,
    ElementId source_vertex_id)
{
    const auto existing = vertex_map.find(source_vertex_id);
    if (existing != vertex_map.end()) {
        return existing->second;
    }

    const Vertex* source_vertex = find_vertex(source, source_vertex_id);
    if (source_vertex == nullptr) {
      return kInvalidElementId;
    }

    const ElementId target_vertex_id = add_vertex(target, source_vertex->position);
    vertex_map.emplace(source_vertex_id, target_vertex_id);
    return target_vertex_id;
}

bool append_copied_face(
    const Document& source,
    Document& target,
    const Face& source_face,
    std::map<ElementId, ElementId>& vertex_map,
    std::map<ElementId, ElementId>& face_map,
    Selection& target_selection)
{
    std::vector<ElementId> mapped_vertices;
    mapped_vertices.reserve(source_face.vertices.size());
    for (const ElementId source_vertex_id : source_face.vertices) {
        const ElementId target_vertex_id = copied_vertex_id(source, target, vertex_map, source_vertex_id);
        if (target_vertex_id == kInvalidElementId) {
          return false;
        }
        mapped_vertices.push_back(target_vertex_id);
    }

    Face copied_face;
    copied_face.id = target.next_face_id++;
    copied_face.vertices = std::move(mapped_vertices);
    if (face_has_loop_uvs(source_face)) {
        copied_face.uvs = source_face.uvs;
    }
    copied_face.material_slot = source_face.material_slot;
    if (triangulate_face_local_indices(target, copied_face).empty()) {
        return false;
    }

    const ElementId copied_face_id = copied_face.id;
    target.faces.push_back(std::move(copied_face));
    face_map.emplace(source_face.id, copied_face_id);
    add_unique_id(target_selection.faces, copied_face_id);
    return true;
}


std::vector<FaceIslandBoundary> selected_face_island_boundaries(const Document& document, const std::vector<Face>& selected_faces)
{
    std::map<ElementId, Face> faces_by_id;
    std::map<std::pair<ElementId, ElementId>, std::vector<ElementId>> edge_faces;
    for (const Face& face : selected_faces) {
        faces_by_id.emplace(face.id, face);
        for (const Edge& edge : face_edges(face)) {
            edge_faces[{ edge.a, edge.b }].push_back(face.id);
        }
    }

    std::map<ElementId, std::set<ElementId>> adjacency;
    for (const Face& face : selected_faces) {
        adjacency[face.id];
    }
    for (const auto& [edge, face_ids] : edge_faces) {
        (void)edge;
        if (face_ids.size() != 2U) {
            continue;
        }
        adjacency[face_ids[0]].insert(face_ids[1]);
        adjacency[face_ids[1]].insert(face_ids[0]);
    }

    std::set<ElementId> remaining;
    for (const Face& face : selected_faces) {
        remaining.insert(face.id);
    }

    std::vector<FaceIslandBoundary> boundaries;
    while (!remaining.empty()) {
        std::vector<ElementId> stack { *remaining.begin() };
        remaining.erase(remaining.begin());
        std::vector<ElementId> component_face_ids;

        while (!stack.empty()) {
            const ElementId face_id = stack.back();
            stack.pop_back();
            component_face_ids.push_back(face_id);

            const auto adjacent = adjacency.find(face_id);
            if (adjacent == adjacency.end()) {
                continue;
            }
            for (const ElementId neighbor_id : adjacent->second) {
                const auto removed = remaining.find(neighbor_id);
                if (removed == remaining.end()) {
                    continue;
                }
                remaining.erase(removed);
                stack.push_back(neighbor_id);
            }
        }

        std::map<std::pair<ElementId, ElementId>, int> component_edge_counts;
        FaceIslandBoundary boundary;
        boundary.face_ids = std::move(component_face_ids);
        for (const ElementId face_id : boundary.face_ids) {
            const auto face = faces_by_id.find(face_id);
            if (face == faces_by_id.end()) {
                continue;
            }
            for (const Edge& edge : face_edges(face->second)) {
                ++component_edge_counts[{ edge.a, edge.b }];
            }
            boundary.normal += face_normal(document, face->second);
            if (!boundary.has_material_slot) {
                boundary.material_slot = face->second.material_slot;
                boundary.has_material_slot = true;
            }
        }

        for (const auto& [edge, count] : component_edge_counts) {
            if (count == 1) {
                boundary.edges.push_back({ edge.first, edge.second });
            }
        }

        std::optional<std::vector<ElementId>> loop = closed_edge_loop_from_edges(boundary.edges);
        if (!loop.has_value()) {
            boundary.vertices.clear();
        } else {
            boundary.vertices = std::move(*loop);
        }
        boundary.normal = normalize_or_zero(boundary.normal);
        const FacePerimeterInfo perimeter = perimeter_info_for_edges(document, boundary.edges);
        boundary.all_open = perimeter.has_only_open_edges();
        boundaries.push_back(std::move(boundary));
    }

    return boundaries;
}

float loop_alignment_score(
    const Document& document,
    std::span<const ElementId> first_vertices,
    std::span<const ElementId> second_vertices)
{
    if (first_vertices.size() != second_vertices.size()) {
        return std::numeric_limits<float>::infinity();
    }

    float score = 0.0F;
    float edge_midpoint_distance_sum = 0.0F;
    float closest_edge_midpoint_distance = std::numeric_limits<float>::infinity();
    float edge_length_sum = 0.0F;
    std::size_t valid_edges = 0;
    for (std::size_t index = 0; index < first_vertices.size(); ++index) {
        const Vertex* first = find_vertex(document, first_vertices[index]);
        const Vertex* second = find_vertex(document, second_vertices[index]);
        if (first == nullptr || second == nullptr) {
            return std::numeric_limits<float>::infinity();
        }
        score += length_squared(first->position - second->position);

        const Vertex* first_next = find_vertex(document, first_vertices[(index + 1U) % first_vertices.size()]);
        const Vertex* second_next = find_vertex(document, second_vertices[(index + 1U) % second_vertices.size()]);
        if (first_next == nullptr || second_next == nullptr) {
            continue;
        }
        const quader::QVec3 first_midpoint = (first->position + first_next->position) * 0.5F;
        const quader::QVec3 second_midpoint = (second->position + second_next->position) * 0.5F;
        const float edge_midpoint_distance = length_squared(first_midpoint - second_midpoint);
        edge_midpoint_distance_sum += edge_midpoint_distance;
        closest_edge_midpoint_distance = std::min(closest_edge_midpoint_distance, edge_midpoint_distance);

        const quader::QVec3 first_edge = first_next->position - first->position;
        const quader::QVec3 second_edge = second_next->position - second->position;
        const float first_edge_length = length(first_edge);
        const float second_edge_length = length(second_edge);
        if (first_edge_length <= kEpsilon || second_edge_length <= kEpsilon) {
          continue;
        }
        edge_length_sum += (first_edge_length + second_edge_length) * 0.5F;
        ++valid_edges;
    }
    score += edge_midpoint_distance_sum;
    if (std::isfinite(closest_edge_midpoint_distance)) {
        score += closest_edge_midpoint_distance * static_cast<float>(first_vertices.size()) * 4.0F;
    }
    if (valid_edges > 0U) {
        score += edge_length_sum / static_cast<float>(valid_edges) * 0.0001F;
    }
    return score;
}

std::vector<ElementId> aligned_loop_vertices(
    const Document& document,
    std::span<const ElementId> first_vertices,
    std::span<const ElementId> second_vertices)
{
    if (first_vertices.empty() || first_vertices.size() != second_vertices.size()) {
        return {};
    }

    float best_score = std::numeric_limits<float>::infinity();
    std::vector<ElementId> best_vertices;
    const std::size_t count = second_vertices.size();
    for (std::size_t offset = 0; offset < count; ++offset) {
        for (const bool reversed : { false, true }) {
            std::vector<ElementId> candidate;
            candidate.reserve(count);
            for (std::size_t index = 0; index < count; ++index) {
                const std::size_t source_index = reversed ?
                    (offset + count - (index % count)) % count :
                    (offset + index) % count;
                candidate.push_back(second_vertices[source_index]);
            }

            const float score = loop_alignment_score(document, first_vertices, candidate);
            if (score < best_score) {
                best_score = score;
                best_vertices = std::move(candidate);
            }
        }
    }

    return std::isfinite(best_score) ? best_vertices : std::vector<ElementId> {};
}

quader::QVec3 vertex_position_or_zero(const Document& document, ElementId vertex_id)
{
    const Vertex* vertex = find_vertex(document, vertex_id);
    return vertex != nullptr ? vertex->position : quader::QVec3 {};
}

quader::QVec3 vertex_loop_centroid(const Document& document, std::span<const ElementId> vertex_ids)
{
    quader::QVec3 centroid;
    std::size_t count = 0;
    for (const ElementId vertex_id : vertex_ids) {
        const Vertex* vertex = find_vertex(document, vertex_id);
        if (vertex == nullptr) {
            continue;
        }
        centroid += vertex->position;
        ++count;
    }
    return count == 0 ? quader::QVec3 {} : centroid / static_cast<float>(count);
}

bool append_bridge_face(Document& document, std::vector<ElementId> vertices, std::uint32_t material_slot, quader::QVec3 expected_normal, std::vector<ElementId>& bridge_face_ids)
{
    vertices = unique_valid_face_loop(std::move(vertices));
    if (vertices.size() < 3 || vertices.size() > 4) {
        return false;
    }

    Face face;
    face.id = document.next_face_id++;
    face.vertices = std::move(vertices);
    face.material_slot = material_slot;
    if (triangulate_face_local_indices(document, face).empty()) {
        std::ranges::reverse(face.vertices);
        if (triangulate_face_local_indices(document, face).empty()) {
            --document.next_face_id;
            return false;
        }
    }
    if (!orient_face_against_adjacent_winding(document, face)) {
        orient_face_toward_normal(document, face, expected_normal);
    }
    bridge_face_ids.push_back(face.id);
    document.faces.push_back(std::move(face));
    return true;
}

bool append_bridge_faces_between_loops(
    Document& document,
    std::span<const ElementId> first_vertices,
    std::span<const ElementId> second_vertices,
    quader::QVec3 first_normal,
    quader::QVec3 second_normal,
    std::uint32_t material_slot,
    int steps,
    std::vector<ElementId>& bridge_face_ids,
    bool skip_degenerate_faces)
{
    if (first_vertices.size() < 3 || first_vertices.size() != second_vertices.size()) {
        return false;
    }

    const int step_count = std::clamp(steps, 1, 64);
    const std::size_t vertex_count = first_vertices.size();
    std::vector<std::vector<ElementId>> rings;
    rings.reserve(static_cast<std::size_t>(step_count + 1));
    rings.emplace_back(first_vertices.begin(), first_vertices.end());

    for (int step_index = 1; step_index < step_count; ++step_index) {
        std::vector<ElementId> ring;
        ring.reserve(vertex_count);
        for (std::size_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
            const quader::QVec3 start = vertex_position_or_zero(document, first_vertices[vertex_index]);
            const quader::QVec3 end = vertex_position_or_zero(document, second_vertices[vertex_index]);
            ring.push_back(add_vertex(document, curved_bridge_position(start, end, first_normal, second_normal, step_index, step_count)));
        }
        rings.push_back(std::move(ring));
    }

    rings.emplace_back(second_vertices.begin(), second_vertices.end());
    for (std::size_t ring_index = 0; ring_index + 1U < rings.size(); ++ring_index) {
        for (std::size_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
            const std::size_t next_index = (vertex_index + 1U) % vertex_count;
            std::vector<ElementId> quad {
                rings[ring_index][vertex_index],
                rings[ring_index][next_index],
                rings[ring_index + 1U][next_index],
                rings[ring_index + 1U][vertex_index],
            };
            Face expected_face;
            expected_face.vertices = quad;
            const quader::QVec3 expected_normal = face_normal(document, expected_face);
            if (!append_bridge_face(document, std::move(quad), material_slot, expected_normal, bridge_face_ids)) {
                if (!skip_degenerate_faces) {
                    return false;
                }
            }
        }
    }

    return true;
}

quader::QVec3 face_centroid(const Document& document, const Face& face)
{
    quader::QVec3 centroid;
    std::size_t count = 0;
    for (const ElementId vertex_id : face.vertices) {
        const Vertex* vertex = find_vertex(document, vertex_id);
        if (vertex == nullptr) {
            continue;
        }

        centroid += vertex->position;
        ++count;
    }

    return count == 0 ? quader::QVec3 {} : centroid / static_cast<float>(count);
}

} // namespace quader_poly::document_internal
