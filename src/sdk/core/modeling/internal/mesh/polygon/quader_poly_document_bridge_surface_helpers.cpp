////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_bridge_surface_helpers.hpp>
#include <quader/modeling/neutral/polygon/document_texture.hpp>

#include <quader/modeling/neutral/polygon/internal/quader_poly_document_hull_plane_helpers.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_knife_helpers.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_mesh_internal.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_topology_helpers.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_uv_helpers.hpp>

#include <diagnostics/profile.hpp>
#include <quader/modeling/neutral/geometry/geometry.hpp>

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

const Face* face_for_open_edge(const Document& document, Edge edge)
{
    edge = make_edge(edge.a, edge.b);
    for (const Face& face : document.faces) {
        for (std::size_t vertex_index = 0; vertex_index < face.vertices.size(); ++vertex_index) {
            const Edge face_edge = make_edge(face.vertices[vertex_index], face.vertices[(vertex_index + 1U) % face.vertices.size()]);
            if (face_edge == edge) {
                return &face;
            }
        }
    }
    return nullptr;
}

std::uint32_t material_slot_for_open_edge(const Document& document, Edge edge)
{
    const Face* face = face_for_open_edge(document, edge);
    return face == nullptr ? 0U : face->material_slot;
}

ElementId add_face_with_texture_transform(
    Document& document,
    std::span<const ElementId> vertices,
    std::uint32_t material_slot,
    const FaceTextureTransform& texture_transform)
{
    const FaceTextureTransform inherited_transform = texture_transform;
    const ElementId id = add_face(document, vertices, material_slot);
    if (Face* face = find_face(document, id)) {
        face->texture_transform = inherited_transform;
    }
    return id;
}

namespace {

bool uv_values_match(quader::QVec2 left, quader::QVec2 right)
{
    return std::abs(left.x - right.x) <= 0.0001F &&
           std::abs(left.y - right.y) <= 0.0001F;
}

std::vector<std::pair<quader::QVec3, quader::QVec3>> chart_triangle_edges(
    const FaceUvSampler& sampler)
{
    std::vector<std::pair<quader::QVec3, quader::QVec3>> edges;
    for (const Triangle& triangle : sampler.triangles()) {
        const std::array<std::size_t, 3> indices { triangle.a, triangle.b, triangle.c };
        for (std::size_t index = 0; index < indices.size(); ++index) {
            edges.emplace_back(sampler.positions()[indices[index]],
                sampler.positions()[indices[(index + 1U) % indices.size()]]);
        }
    }
    return edges;
}

bool merged_chart_preserves_source(
    const Document& document, const Face& source, const Face& merged,
    const VertexLookupIndex& vertex_lookup, const FaceUvSampler& target_sampler)
{
    const FaceUvSampler source_sampler(document, source, vertex_lookup);
    const auto effective_uv = [](quader::QVec2 uv, const Face& face) {
        return apply_face_texture_transform(uv, face.texture_transform);
    };
    for (std::size_t index = 0; index < source.vertices.size(); ++index) {
        const Vertex* vertex = find_vertex(document, source.vertices[index]);
        if (vertex == nullptr) {
            return false;
        }
        const quader::QVec2 source_uv = face_has_loop_uvs(source)
            ? source.uvs[index]
            : generated_texture_uv(vertex->position, face_normal(document, source));
        const auto target_uv = target_sampler.sample(vertex->position);
        if (!target_uv.has_value() ||
            !uv_values_match(effective_uv(source_uv, source), effective_uv(*target_uv, merged))) {
            return false;
        }
    }
    const auto matches_at = [&](quader::QVec3 position) {
        const auto source_uv = source_sampler.sample(position);
        const auto target_uv = target_sampler.sample(position);
        return source_uv.has_value() && target_uv.has_value() &&
            uv_values_match(effective_uv(*source_uv, source), effective_uv(*target_uv, merged));
    };
    for (const ElementId vertex_id : merged.vertices) {
        const Vertex* vertex = find_vertex(document, vertex_id);
        if (vertex != nullptr && source_sampler.sample(vertex->position).has_value() &&
            !matches_at(vertex->position)) {
            return false;
        }
    }
    if (source_sampler.has_quad_interpolation() || target_sampler.has_quad_interpolation()) {
        // Nonlinear quad charts survive a merge exactly when each source is a
        // rectangular parameter subpatch of the resulting bilinear quad.
        // Triangles and arbitrary cuts cannot carry that nonlinear field.
        if (source.vertices.size() != 4U || merged.vertices.size() != 4U ||
            !source_sampler.has_quad_interpolation() || !target_sampler.has_quad_interpolation()) {
            return false;
        }
        Face parameter_face = merged;
        parameter_face.texture_transform = {};
        parameter_face.uvs = {{0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 1.0F}};
        const FaceUvSampler parameters(document, parameter_face, vertex_lookup);
        std::array<quader::QVec2, 4> corners;
        for (std::size_t index = 0; index < corners.size(); ++index) {
            const auto corner = parameters.sample(source_sampler.positions()[index]);
            if (!corner.has_value()) {
                return false;
            }
            corners[index] = *corner;
        }
        const quader::QVec2 first = corners[1] - corners[0];
        const quader::QVec2 second = corners[3] - corners[0];
        const bool coordinate_edges =
            (std::abs(first.x) <= 0.00001F && std::abs(second.y) <= 0.00001F) ||
            (std::abs(first.y) <= 0.00001F && std::abs(second.x) <= 0.00001F);
        return coordinate_edges && uv_values_match(corners[2], corners[0] + first + second);
    }
    // Each remaining chart is affine inside each rendered triangle. Their difference is
    // therefore affine on every triangle-overlap polygon: corners plus edge
    // intersections establish equality, including nonaffine authored charts.
    const auto source_edges = chart_triangle_edges(source_sampler);
    const auto target_edges = chart_triangle_edges(target_sampler);
    const quader::QVec3 normal = normalize_or_zero(face_normal(document, merged));
    for (const auto& [a, b] : source_edges) {
        const quader::QVec3 ab = b - a;
        for (const auto& [c, d] : target_edges) {
            const quader::QVec3 cd = d - c;
            const double denominator = dot(cross(ab, cd), normal);
            const double scale = std::sqrt(static_cast<double>(length_squared(ab)) * length_squared(cd));
            if (std::abs(denominator) <= std::numeric_limits<double>::epsilon() * scale) {
                continue;
            }
            const quader::QVec3 ac = c - a;
            const double source_factor = dot(cross(ac, cd), normal) / denominator;
            const double target_factor = dot(cross(ac, ab), normal) / denominator;
            if (source_factor >= 0.0 && source_factor <= 1.0 &&
                target_factor >= 0.0 && target_factor <= 1.0 &&
                !matches_at(a + ab * static_cast<float>(source_factor))) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

bool assign_merged_face_texture_boundary(
    const Document& source_document,
    std::span<const ElementId> source_face_ids,
    Face& merged_face)
{
    std::vector<const Face*> sources;
    sources.reserve(source_face_ids.size());
    for (const ElementId face_id : source_face_ids) {
        if (const Face* source = find_face(source_document, face_id)) {
            sources.push_back(source);
        }
    }
    if (sources.empty()) {
        return false;
    }

    merged_face.texture_transform = sources.front()->texture_transform;
    bool needs_corner_mapping = std::ranges::any_of(sources, [&merged_face](const Face* source) {
        return face_has_loop_uvs(*source) || source->texture_transform != merged_face.texture_transform;
    });
    if (!needs_corner_mapping) {
        const quader::QVec3 merged_normal = face_normal(source_document, merged_face);
        for (const Face* source : sources) {
            const quader::QVec3 source_normal = face_normal(source_document, *source);
            for (const ElementId vertex_id : source->vertices) {
                const Vertex* vertex = find_vertex(source_document, vertex_id);
                if (vertex == nullptr || !uv_values_match(
                    apply_face_texture_transform(generated_texture_uv(vertex->position, source_normal),
                                                  source->texture_transform),
                    apply_face_texture_transform(generated_texture_uv(vertex->position, merged_normal),
                                                  merged_face.texture_transform))) {
                    needs_corner_mapping = true;
                    break;
                }
            }
        }
    }
    if (!needs_corner_mapping) {
        return true;
    }

    // Preserve boundary corners in the first material donor's base space.
    // Removed interior seams may no longer fit in a single face chart; callers
    // that require exact interpolation check it separately. An identity source
    // transform does not imply procedural UVs.
    std::vector<quader::QVec2> inherited_uvs;
    inherited_uvs.reserve(merged_face.vertices.size());
    for (std::size_t index = 0; index < merged_face.vertices.size(); ++index) {
        const ElementId vertex_id = merged_face.vertices[index];
        const ElementId next_id = merged_face.vertices[(index + 1U) % merged_face.vertices.size()];
        const Face* source = nullptr;
        for (const Face* candidate : sources) {
            if (!contains_id(candidate->vertices, vertex_id)) {
                continue;
            }
            if (source == nullptr) {
                source = candidate;
            }
            if (face_uses_edge(*candidate, make_edge(vertex_id, next_id))) {
                source = candidate;
                break;
            }
        }
        const Vertex* vertex = find_vertex(source_document, vertex_id);
        if (source == nullptr || vertex == nullptr) {
            return false;
        }
        const auto source_corner = std::ranges::find(source->vertices, vertex_id);
        const std::size_t source_index = static_cast<std::size_t>(std::distance(source->vertices.begin(), source_corner));
        const quader::QVec2 base_uv = face_has_loop_uvs(*source)
            ? source->uvs[source_index]
            : generated_texture_uv(vertex->position, face_normal(source_document, *source));
        const quader::QVec2 uv = unapply_face_texture_transform(
            apply_face_texture_transform(base_uv, source->texture_transform),
            merged_face.texture_transform);
        if (!std::isfinite(uv.x) || !std::isfinite(uv.y)) {
            return false;
        }
        inherited_uvs.push_back(uv);
    }
    merged_face.uvs = std::move(inherited_uvs);
    return true;
}

bool merged_face_preserves_source_textures(const Document& source_document,
    std::span<const ElementId> source_face_ids, const Face& merged_face)
{
    if (source_face_ids.empty()) {
        return false;
    }
    const VertexLookupIndex vertex_lookup(source_document);
    const FaceUvSampler merged_sampler(source_document, merged_face, vertex_lookup);
    return std::ranges::all_of(source_face_ids, [&](ElementId face_id) {
        const Face* source = find_face(source_document, face_id);
        return source != nullptr && merged_chart_preserves_source(source_document, *source, merged_face,
                                             vertex_lookup, merged_sampler);
    });
}

bool inherit_merged_face_texture(const Document& source_document,
    std::span<const ElementId> source_face_ids, Face& merged_face)
{
    return assign_merged_face_texture_boundary(source_document, source_face_ids, merged_face) &&
        merged_face_preserves_source_textures(source_document, source_face_ids, merged_face);
}

bool face_triangulation_preserves_boundary(const Document& document, const Face& face,
    const VertexLookupIndex& vertex_lookup)
{
    return triangulation_preserves_face_boundary(
        face, vertex_lookup,
        triangulate_face_local_indices(document, face, vertex_lookup));
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
            return OpenBridgeEdgeInfo { edge, *oriented, face_normal(document, face), face.material_slot, face.texture_transform };
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

    const quader::QVec3 end_forward_direction = end_tangent_direction * -1.0F;
    const quader::QVec3 rail_direction = (end - start) / rail_length;
    const quader::QVec3 tangent_bisector = start_tangent_direction + end_forward_direction;
    float tangent_scale = 1.0F;
    if (length_squared(tangent_bisector) > kEpsilon &&
        dot(start_tangent_direction, rail_direction) > kEpsilon &&
        dot(end_forward_direction, rail_direction) > kEpsilon &&
        length_squared(cross(normalize_or_zero(tangent_bisector), rail_direction)) <= kEpsilon) {
        // Equal-radius circular joins have their tangent bisector along the chord.
        // Converting the cubic arc's 4/3*r*tan(turn/4) controls to Hermite tangents
        // gives sec^2(turn/4): one for straight rails and 4-2*sqrt(2) at 90 degrees.
        const float cos_turn = std::clamp(
            dot(start_tangent_direction, end_forward_direction), -1.0F, 1.0F);
        const float cos_half_turn = std::sqrt((1.0F + cos_turn) * 0.5F);
        tangent_scale = 2.0F / (1.0F + cos_half_turn);
    }
    const quader::QVec3 start_tangent = start_tangent_direction * (rail_length * tangent_scale);
    const quader::QVec3 end_tangent = end_forward_direction * (rail_length * tangent_scale);
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

float record_edge_bevel_applied_width(EdgeBevelWidthFeedback *feedback,
                                     float requested_width,
                                     float applied_width)
{
    if (feedback != nullptr) {
        feedback->applied_width = std::max(feedback->applied_width, applied_width);
        feedback->support_limited |= applied_width < requested_width;
    }
    return applied_width;
}

float safe_edge_bevel_width_for_face_edge(const Document& document, const Face& face, std::size_t edge_index, float requested_width, EdgeBevelWidthFeedback *feedback)
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
    return record_edge_bevel_applied_width(
        feedback, requested_width, std::max(0.0F, max_width));
}

std::optional<EdgeBevelSide> edge_bevel_side_for_face(
    Document& candidate,
    const Document& source,
    Edge edge,
    std::size_t face_index,
    float width,
    EdgeBevelWidthFeedback *feedback)
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
    const float safe_width = safe_edge_bevel_width_for_face_edge(source, face, *directed_index, width, feedback);
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
    side.texture_transform = face.texture_transform;
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
    float width,
    EdgeBevelWidthFeedback *feedback)
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
    const float safe_width = safe_edge_bevel_width_for_face_edge(document, face, edge_index, width, feedback);
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
    std::map<std::pair<ElementId, ElementId>, std::pair<ElementId, quader::QVec3>>& miter_vertices,
    EdgeBevelWidthFeedback *feedback)
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
        edge_bevel_offset_line_for_face_edge(source, face, previous_index, width, feedback);
    const std::optional<EdgeBevelOffsetLine> next_line =
        edge_bevel_offset_line_for_face_edge(source, face, vertex_index, width, feedback);
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

bool append_edge_bevel_face(
    Document& document,
    std::vector<Face>& faces,
    std::vector<ElementId>& generated_face_ids,
    std::vector<ElementId> vertices,
    std::uint32_t material_slot,
    quader::QVec3 expected_normal,
    const FaceTextureTransform& texture_transform)
{
    vertices = compact_edge_bevel_face_loop(std::move(vertices));
    if (vertices.size() < 3 || edge_bevel_face_loop_has_repeated_vertices(vertices)) {
        return false;
    }

    Face face;
    face.id = document.next_face_id++;
    face.vertices = std::move(vertices);
    face.material_slot = material_slot;
    face.texture_transform = texture_transform;
    orient_face_toward_normal(document, face, expected_normal);

    generated_face_ids.push_back(face.id);
    faces.push_back(std::move(face));
    return true;
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
    for (const Face& face : document.faces) {
        if (contains_id(face.vertices, vertex_id) &&
            !face_vertex_uv_is_redundant(document, face, vertex_id)) {
            return false;
        }
    }

    const EdgeNormalModeLookup source_modes(document);
    std::map<std::pair<ElementId, ElementId>, EdgeNormalMode>
        replacement_modes_by_edge;
    for (const Face& face : document.faces) {
        const std::optional<std::size_t> index =
            loop_vertex_index(face.vertices, vertex_id);
        if (!index.has_value()) {
            continue;
        }

        const ElementId previous = face.vertices[
            (*index + face.vertices.size() - 1U) % face.vertices.size()];
        const ElementId next = face.vertices[(*index + 1U) % face.vertices.size()];
        const Edge replacement = make_edge(previous, next);
        const auto replacement_key =
            std::pair{replacement.a, replacement.b};
        if (replacement_modes_by_edge.contains(replacement_key)) {
            continue;
        }

        EdgeNormalMode mode = EdgeNormalMode::Default;
        if (edge_exists(document, replacement)) {
            mode = source_modes.mode(replacement);
        } else {
            std::array<Edge, 2> donors {
                make_edge(previous, vertex_id),
                make_edge(vertex_id, next),
            };
            std::ranges::sort(donors, [](Edge left, Edge right) {
                return std::pair{left.a, left.b} <
                       std::pair{right.a, right.b};
            });
            // Resolve the replacement once from canonical donor order so
            // opposite face winding and face storage order cannot change it.
            // Default is a real donor state and can therefore clear an
            // earlier explicit assignment.
            for (const Edge donor : donors) {
                if (edge_exists(document, donor)) {
                    mode = source_modes.mode(donor);
                }
            }
        }
        replacement_modes_by_edge.emplace(replacement_key, mode);
    }

    std::vector<EdgeNormalModeAssignment> replacement_modes;
    replacement_modes.reserve(replacement_modes_by_edge.size());
    for (const auto& [edge, mode] : replacement_modes_by_edge) {
        replacement_modes.push_back({make_edge(edge.first, edge.second), mode});
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

        Face dissolved_face = face;
        dissolved_face.vertices = std::move(dissolved_loop);
        if (!preserve_face_uvs(document, face, document, dissolved_face)) {
            return false;
        }
        face = std::move(dissolved_face);
        changed = true;
    }
    if (changed) {
        static_cast<void>(
            set_edge_normal_modes(document, replacement_modes));
        prune_normal_edge_states(document);
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
    reverse_face_winding(face);
}

void reverse_face_winding(Face& face, FaceWindingChange change)
{
    if (face.vertices.empty()) {
        face.uvs.clear();
        return;
    }
    // Quads use the first-to-third diagonal; retain its anchor. Earcut changes
    // its traversal with projected winding, so a normal-only reversal needs
    // the old end anchor while already-reflected geometry keeps its first.
    const std::ptrdiff_t first = face.vertices.size() == 4U ||
        change == FaceWindingChange::ReflectedGeometry ? 1 : 0;
    std::reverse(face.vertices.begin() + first, face.vertices.end());
    if (face_has_loop_uvs(face)) {
        std::reverse(face.uvs.begin() + first, face.uvs.end());
    } else {
        face.uvs.clear();
    }
}

bool reverse_reflected_face_winding(const Document& source_document,
    const Face& source_face, const Document& result_document, Face& result_face)
{
    if (source_face.vertices.size() <= 4U || !face_has_loop_uvs(source_face)) {
        reverse_face_winding(result_face, FaceWindingChange::ReflectedGeometry);
        return true;
    }
    const VertexLookupIndex source_lookup(source_document);
    const VertexLookupIndex result_lookup(result_document);
    return reverse_reflected_face_winding(source_document, source_face, result_document,
                                          result_face, source_lookup, result_lookup);
}

bool reverse_reflected_face_winding(const Document& source_document,
    const Face& source_face, const Document& result_document, Face& result_face,
    const VertexLookupIndex& source_lookup, const VertexLookupIndex& result_lookup)
{
    return preserve_rebased_face_texture_triangulation(source_document, source_face,
        result_document, result_face, FaceRebaseWinding::Reverse, source_lookup,
        result_lookup);
}

bool preserve_rebased_face_texture_triangulation(const Document& source_document,
    const Face& source_face, const Document& result_document, Face& result_face,
    FaceRebaseWinding winding, const VertexLookupIndex& source_lookup,
    const VertexLookupIndex& result_lookup)
{
    if (source_face.vertices.size() <= 4U || !face_has_loop_uvs(source_face)) {
        if (winding == FaceRebaseWinding::Reverse) {
            reverse_face_winding(result_face, FaceWindingChange::ReflectedGeometry);
        }
        return true;
    }
    const FaceUvSampler source_sampler(source_document, source_face, source_lookup);
    const auto triangle_identities = [](const Face& face, std::span<const Triangle> triangles) {
        std::vector<std::array<ElementId, 3>> identities;
        identities.reserve(triangles.size());
        for (const Triangle& triangle : triangles) {
            std::array ids { face.vertices[triangle.a], face.vertices[triangle.b], face.vertices[triangle.c] };
            std::ranges::sort(ids);
            identities.push_back(ids);
        }
        std::ranges::sort(identities);
        return identities;
    };
    const auto source_triangles = triangle_identities(source_face, source_sampler.triangles());
    if (source_triangles.empty()) {
        return false;
    }
    Face preferred = result_face;
    if (winding == FaceRebaseWinding::Reverse) {
        reverse_face_winding(preferred, FaceWindingChange::ReflectedGeometry);
    }
    const auto matches_source = [&](const Face& candidate) {
        const auto triangles = triangulate_face_local_indices(result_document, candidate, result_lookup);
        return triangle_identities(candidate, triangles) == source_triangles;
    };
    if (matches_source(preferred)) {
        result_face = std::move(preferred);
        return true;
    }
    if (winding == FaceRebaseWinding::Reverse) {
        Face alternate = result_face;
        reverse_face_winding(alternate, FaceWindingChange::OrientationOnly);
        if (matches_source(alternate)) {
            result_face = std::move(alternate);
            return true;
        }
    } else {
        // Proper rotations can reverse the dominant-plane projection too.
        // Retain winding and try the adjacent starts in either direction;
        // moving each UV with its vertex preserves corner identity.
        for (const std::size_t offset : std::array<std::size_t, 2>{1U, preferred.vertices.size() - 1U}) {
            Face alternate = preferred;
            const auto shift = static_cast<std::ptrdiff_t>(offset);
            std::rotate(alternate.vertices.begin(), alternate.vertices.begin() + shift,
                        alternate.vertices.end());
            std::rotate(alternate.uvs.begin(), alternate.uvs.begin() + shift,
                        alternate.uvs.end());
            if (matches_source(alternate)) {
                result_face = std::move(alternate);
                return true;
            }
        }
    }
    // A planar affine UV field is independent of its triangulation, including
    // constant or one-axis-collapsed charts. Do not reject those valid rebases.
    const Triangle first = source_sampler.triangles().front();
    const auto positions = source_sampler.positions();
    const quader_geometry::QTriangle3<float> plane {
        geometry_vec3(positions[first.a]), geometry_vec3(positions[first.b]),
        geometry_vec3(positions[first.c]) };
    const auto normal = quader_geometry::triangle_unit_normal(plane);
    const float extent = std::max({1.0F, length(positions[first.b] - positions[first.a]),
                                        length(positions[first.c] - positions[first.a])});
    for (std::size_t corner = 0; corner < positions.size(); ++corner) {
        const auto point = geometry_vec3(positions[corner]);
        if (std::abs(quader_geometry::dot(point - plane.a, normal)) > extent * 0.00001F) {
            return false;
        }
        const auto weights = quader_geometry::barycentric_coordinates(point, plane);
        if (!weights.valid) {
            return false;
        }
        const quader::QVec2 expected = source_face.uvs[first.a] * weights.u +
            source_face.uvs[first.b] * weights.v + source_face.uvs[first.c] * weights.w;
        if (std::abs(expected.x - source_face.uvs[corner].x) > 0.00001F ||
            std::abs(expected.y - source_face.uvs[corner].y) > 0.00001F) {
            return false;
        }
    }
    result_face = std::move(preferred);
    return true;
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
    copied_face.normal_shading = source_face.normal_shading;
    copied_face.texture_transform = source_face.texture_transform;
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
                boundary.texture_transform = face->second.texture_transform;
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

bool append_bridge_face(Document& document, std::vector<ElementId> vertices, std::uint32_t material_slot, quader::QVec3 expected_normal, std::vector<ElementId>& bridge_face_ids, const FaceTextureTransform& texture_transform)
{
    vertices = unique_valid_face_loop(std::move(vertices));
    if (vertices.size() < 3 || vertices.size() > 4) {
        return false;
    }

    Face face;
    face.id = document.next_face_id++;
    face.vertices = std::move(vertices);
    face.material_slot = material_slot;
    face.texture_transform = texture_transform;
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
    bool skip_degenerate_faces,
    const FaceTextureTransform& texture_transform)
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
            if (!append_bridge_face(document, std::move(quad), material_slot, expected_normal, bridge_face_ids, texture_transform)) {
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
