////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <mesh/polygon/internal/quader_poly_document_knife_helpers.hpp>

#include <mesh/polygon/internal/quader_poly_document_hull_plane_helpers.hpp>
#include <mesh/polygon/internal/quader_poly_document_topology_helpers.hpp>
#include <mesh/polygon/internal/quader_poly_document_uv_helpers.hpp>

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

ElementId other_edge_vertex(Edge edge, ElementId vertex)
{
    if (edge.a == vertex) {
        return edge.b;
    }
    if (edge.b == vertex) {
        return edge.a;
    }
    return kInvalidElementId;
}

bool edge_exists(const Document& document, Edge edge)
{
    const std::vector<Edge> edges = document_edges(document);
    return contains_edge(edges, edge);
}

bool edges_share_face(const Document& document, Edge left, Edge right)
{
    for (const Face& face : document.faces) {
        if (face_uses_edge(face, left) && face_uses_edge(face, right)) {
            return true;
        }
    }
    return false;
}

std::vector<Edge> incident_edges(const Document& document, ElementId vertex_id)
{
    std::vector<Edge> edges;
    for (const Edge& edge : document_edges(document)) {
        if (edge.a == vertex_id || edge.b == vertex_id) {
            edges.push_back(edge);
        }
    }
    return edges;
}

std::optional<Edge> next_edge_loop_edge(const Document& document, Edge current, ElementId through_vertex)
{
    std::vector<Edge> candidates;
    for (const Edge& edge : incident_edges(document, through_vertex)) {
        if (edge != make_edge(current.a, current.b)) {
            candidates.push_back(edge);
        }
    }
    if (candidates.empty()) {
        return std::nullopt;
    }

    std::vector<Edge> straight_candidates;
    for (const Edge& candidate : candidates) {
        if (!edges_share_face(document, current, candidate)) {
            straight_candidates.push_back(candidate);
        }
    }
    if (straight_candidates.size() == 1) {
        return straight_candidates.front();
    }

    if (candidates.size() == 1) {
        return candidates.front();
    }

    return std::nullopt;
}

void append_edge_loop_direction(const Document& document, Edge seed, ElementId through_vertex, std::vector<Edge>& edges)
{
    Edge current = make_edge(seed.a, seed.b);
    ElementId vertex = through_vertex;
    const std::size_t max_steps = document_edges(document).size();
    for (std::size_t step = 0; step < max_steps; ++step) {
        const std::optional<Edge> next = next_edge_loop_edge(document, current, vertex);
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

std::optional<std::size_t> face_edge_index(const Face& face, Edge edge)
{
    if (face.vertices.size() < 2) {
        return std::nullopt;
    }

    const Edge normalized = make_edge(edge.a, edge.b);
    for (std::size_t index = 0; index < face.vertices.size(); ++index) {
        const Edge candidate = make_edge(face.vertices[index], face.vertices[(index + 1U) % face.vertices.size()]);
        if (candidate == normalized) {
            return index;
        }
    }
    return std::nullopt;
}

Edge directed_face_edge(const Face& face, std::size_t edge_index)
{
    return {
        face.vertices[edge_index % face.vertices.size()],
        face.vertices[(edge_index + 1U) % face.vertices.size()],
    };
}

bool same_directed_edge(Edge left, Edge right)
{
    return left.a == right.a && left.b == right.b;
}

Edge oriented_loop_opposite_edge(const Face& face, std::size_t entry_index, Edge entry_edge)
{
    const std::size_t i0 = entry_index;
    const std::size_t i1 = (i0 + 1U) % 4U;
    const std::size_t i2 = (i0 + 2U) % 4U;
    const std::size_t i3 = (i0 + 3U) % 4U;
    const Edge face_entry = directed_face_edge(face, i0);
    if (same_directed_edge(entry_edge, face_entry)) {
        return { face.vertices[i3], face.vertices[i2] };
    }
    return { face.vertices[i2], face.vertices[i3] };
}

const Face* find_face_copy(std::span<const Face> faces, ElementId id)
{
    const auto face = std::ranges::find_if(faces, [id](const Face& candidate) {
        return candidate.id == id;
    });
    return face == faces.end() ? nullptr : &(*face);
}

std::vector<EdgeLoopFaceSplit> collect_edge_loop_splits(const Document& document, Edge seed_edge)
{
    std::vector<EdgeLoopFaceSplit> queue;
    for (const Face& face : document.faces) {
        if (face.vertices.size() == 4 && face_edge_index(face, seed_edge).has_value()) {
            queue.push_back({ face.id, seed_edge });
        }
    }

    std::vector<EdgeLoopFaceSplit> splits;
    std::set<ElementId> visited_faces;
    for (std::size_t queue_index = 0; queue_index < queue.size(); ++queue_index) {
        const EdgeLoopFaceSplit current = queue[queue_index];
        if (visited_faces.contains(current.face_id)) {
            continue;
        }

        const Face* face = find_face_copy(document.faces, current.face_id);
        if (face == nullptr || face->vertices.size() != 4) {
            continue;
        }

        const std::optional<std::size_t> edge_index = face_edge_index(*face, current.entry_edge);
        if (!edge_index.has_value()) {
            continue;
        }

        visited_faces.insert(face->id);
        splits.push_back(current);

        const Edge opposite = oriented_loop_opposite_edge(*face, *edge_index, current.entry_edge);
        for (const Face& candidate : document.faces) {
            if (candidate.id == face->id ||
                candidate.vertices.size() != 4 ||
                visited_faces.contains(candidate.id) ||
                !face_edge_index(candidate, opposite).has_value()) {
                continue;
            }

            queue.push_back({ candidate.id, opposite });
        }
    }

    return splits;
}

std::optional<quader::QVec3> split_edge_position(const Document& document, Edge edge, float factor)
{
    const Vertex* a = find_vertex(document, edge.a);
    const Vertex* b = find_vertex(document, edge.b);
    if (a == nullptr || b == nullptr) {
        return std::nullopt;
    }

    return a->position + ((b->position - a->position) * factor);
}

std::pair<ElementId, ElementId> edge_key(Edge edge)
{
    const Edge normalized = make_edge(edge.a, edge.b);
    return { normalized.a, normalized.b };
}

ElementId split_vertex_for_edge(
    Document& document,
    std::map<std::pair<ElementId, ElementId>, ElementId>& split_vertices,
    Edge edge,
    float factor)
{
    const std::pair<ElementId, ElementId> key = edge_key(edge);
    const auto existing = split_vertices.find(key);
    if (existing != split_vertices.end()) {
        return existing->second;
    }

    const Vertex* a = find_vertex(document, edge.a);
    const Vertex* b = find_vertex(document, edge.b);
    if (a == nullptr || b == nullptr) {
      return kInvalidElementId;
    }

    const ElementId vertex_id = add_vertex(document, a->position + ((b->position - a->position) * factor));
    split_vertices[key] = vertex_id;
    return vertex_id;
}

bool knife_edge_splits_empty(const KnifeEdgeSplitMap& split_vertices)
{
    return split_vertices.empty();
}

std::size_t knife_edge_split_count(const KnifeEdgeSplitMap& split_vertices)
{
    std::size_t count = 0;
    for (const auto& [key, splits] : split_vertices) {
        (void)key;
        count += splits.size();
    }
    return count;
}

float knife_edge_factor_for_key(Edge edge, float factor)
{
    factor = std::clamp(std::isfinite(factor) ? factor : 0.5F, 0.01F, 0.99F);
    const Edge normalized = make_edge(edge.a, edge.b);
    if (edge.a == normalized.b && edge.b == normalized.a) {
        return 1.0F - factor;
    }
    return factor;
}

ElementId split_vertex_for_knife_edge(
    Document& document,
    KnifeEdgeSplitMap& split_vertices,
    Edge edge,
    float factor)
{
    const std::pair<ElementId, ElementId> key = edge_key(edge);
    if (key.first == kInvalidElementId || key.second == kInvalidElementId ||
        key.first == key.second) {
      return kInvalidElementId;
    }

    factor = knife_edge_factor_for_key(edge, factor);
    std::vector<KnifeEdgeSplit>& splits = split_vertices[key];
    for (const KnifeEdgeSplit& split : splits) {
        if (std::abs(split.factor - factor) <= 0.0001F) {
            return split.vertex_id;
        }
    }

    const Vertex* a = find_vertex(document, key.first);
    const Vertex* b = find_vertex(document, key.second);
    if (a == nullptr || b == nullptr) {
      return kInvalidElementId;
    }

    const ElementId vertex_id = add_vertex(document, a->position + ((b->position - a->position) * factor));
    splits.push_back({ factor, vertex_id });
    return vertex_id;
}

ElementId split_vertex_near_endpoint(
    Document& document,
    std::map<std::pair<ElementId, ElementId>, ElementId>& split_vertices,
    ElementId endpoint_id,
    ElementId neighbor_id,
    float distance)
{
  if (endpoint_id == kInvalidElementId || neighbor_id == kInvalidElementId ||
      endpoint_id == neighbor_id) {
    return kInvalidElementId;
  }

    const std::pair<ElementId, ElementId> key { endpoint_id, neighbor_id };
    const auto existing = split_vertices.find(key);
    if (existing != split_vertices.end()) {
        return existing->second;
    }

    const Vertex* endpoint = find_vertex(document, endpoint_id);
    const Vertex* neighbor = find_vertex(document, neighbor_id);
    if (endpoint == nullptr || neighbor == nullptr) {
      return kInvalidElementId;
    }

    const quader::QVec3 edge = neighbor->position - endpoint->position;
    const float edge_length = length(edge);
    if (edge_length <= kEpsilon) {
      return kInvalidElementId;
    }

    const float safe_distance =
        std::clamp(distance, kMinPrimitiveDimension, edge_length * 0.45F);
    const ElementId vertex_id = add_vertex(document, endpoint->position + ((edge / edge_length) * safe_distance));
    split_vertices[key] = vertex_id;
    return vertex_id;
}

bool knife_target_is_edge(const KnifePointTarget& target)
{
  return target.kind == KnifePointTargetKind::ExistingEdge;
}

bool knife_targets_use_same_edge(const KnifePointTarget& left, const KnifePointTarget& right)
{
    return knife_target_is_edge(left) && knife_target_is_edge(right) && edge_key(left.edge) == edge_key(right.edge);
}

std::optional<ResolvedKnifeTarget> resolve_knife_target(
    Document& candidate,
    std::map<std::pair<ElementId, ElementId>, ElementId>& split_vertices,
    const KnifePointTarget& target,
    std::string& message)
{
    switch (target.kind) {
    case KnifePointTargetKind::ExistingVertex:
    case KnifePointTargetKind::InsertedVertex: {
      const Vertex *vertex = find_vertex(candidate, target.vertex_id);
      if (vertex == nullptr) {
        message = target.kind == KnifePointTargetKind::InsertedVertex
                      ? "Knife segment previous inserted vertex was not found."
                      : "Knife segment vertex target was not found.";
        return std::nullopt;
      }
      return ResolvedKnifeTarget{vertex->id, vertex->position};
    }
    case KnifePointTargetKind::ExistingEdge: {
      const Edge edge = target.edge;
      const Edge normalized_edge = make_edge(edge.a, edge.b);
      if (normalized_edge.a == kInvalidElementId ||
          normalized_edge.b == kInvalidElementId ||
          normalized_edge.a == normalized_edge.b ||
          !edge_exists(candidate, normalized_edge)) {
        message = "Knife segment edge target was not found.";
        return std::nullopt;
      }
      const float factor = std::clamp(
          std::isfinite(target.edge_factor) ? target.edge_factor : 0.5F, 0.01F,
          0.99F);
      const ElementId vertex_id =
          split_vertex_for_edge(candidate, split_vertices, edge, factor);
      const Vertex *vertex = find_vertex(candidate, vertex_id);
      if (vertex == nullptr) {
        message = "Knife segment could not split the target edge.";
        return std::nullopt;
      }
      return ResolvedKnifeTarget{vertex_id, vertex->position};
    }
    }

    message = "Knife segment target was not valid.";
    return std::nullopt;
}

bool apply_knife_edge_splits_to_faces(Document& candidate, const std::map<std::pair<ElementId, ElementId>, ElementId>& split_vertices)
{
    bool changed = false;
    if (split_vertices.empty()) {
        return false;
    }

    for (Face& face : candidate.faces) {
        if (face.vertices.size() < 2) {
            continue;
        }

        std::vector<ElementId> expanded_loop;
        expanded_loop.reserve(face.vertices.size() + split_vertices.size());
        bool face_changed = false;
        for (std::size_t index = 0; index < face.vertices.size(); ++index) {
            const ElementId current = face.vertices[index];
            const ElementId next = face.vertices[(index + 1U) % face.vertices.size()];
            expanded_loop.push_back(current);

            const auto split = split_vertices.find(edge_key(make_edge(current, next)));
            if (split == split_vertices.end() || split->second == current || split->second == next) {
                continue;
            }

            expanded_loop.push_back(split->second);
            face_changed = true;
        }

        if (!face_changed) {
            continue;
        }
        if (expanded_loop.size() < 3 || has_repeated_vertex(expanded_loop)) {
            return false;
        }

        face.vertices = std::move(expanded_loop);
        face.uvs.clear();
        changed = true;
    }

    return changed;
}

bool apply_knife_edge_splits_to_faces(Document& candidate, const KnifeEdgeSplitMap& split_vertices)
{
    bool changed = false;
    if (knife_edge_splits_empty(split_vertices)) {
        return false;
    }

    for (Face& face : candidate.faces) {
        if (face.vertices.size() < 2) {
            continue;
        }

        std::vector<ElementId> expanded_loop;
        expanded_loop.reserve(face.vertices.size() + knife_edge_split_count(split_vertices));
        bool face_changed = false;
        for (std::size_t index = 0; index < face.vertices.size(); ++index) {
            const ElementId current = face.vertices[index];
            const ElementId next = face.vertices[(index + 1U) % face.vertices.size()];
            expanded_loop.push_back(current);

            const auto found = split_vertices.find(edge_key(make_edge(current, next)));
            if (found == split_vertices.end() || found->second.empty()) {
                continue;
            }

            std::vector<KnifeEdgeSplit> ordered_splits = found->second;
            const auto factor_less = [](const KnifeEdgeSplit& left, const KnifeEdgeSplit& right) {
                if (std::abs(left.factor - right.factor) > 0.0001F) {
                    return left.factor < right.factor;
                }
                return left.vertex_id < right.vertex_id;
            };
            std::ranges::sort(ordered_splits, factor_less);
            if (current > next) {
                std::ranges::reverse(ordered_splits);
            }

            for (const KnifeEdgeSplit& split : ordered_splits) {
              if (split.vertex_id == kInvalidElementId ||
                  split.vertex_id == current || split.vertex_id == next) {
                continue;
              }
                expanded_loop.push_back(split.vertex_id);
                face_changed = true;
            }
        }

        if (!face_changed) {
            continue;
        }
        if (expanded_loop.size() < 3 || has_repeated_vertex(expanded_loop)) {
            return false;
        }

        face.vertices = std::move(expanded_loop);
        face.uvs.clear();
        changed = true;
    }

    return changed;
}

std::vector<ElementId> loop_between_indices(const std::vector<ElementId>& loop, std::size_t start_index, std::size_t end_index);
std::vector<ElementId> unique_valid_face_loop(std::vector<ElementId> vertices);

KnifePoint2 knife_project_point(quader::QVec3 position, int dropped_axis)
{
    return projected_point_for_axis(position, dropped_axis);
}

KnifePoint2 knife_project_vertex(const Document& document, ElementId vertex_id, int dropped_axis)
{
    const Vertex* vertex = find_vertex(document, vertex_id);
    return vertex != nullptr ? knife_project_point(vertex->position, dropped_axis) : KnifePoint2 {};
}

double knife_distance_squared_2d(const KnifePoint2& a, const KnifePoint2& b)
{
    return quader_geometry::length_squared(geometry_vec2(a) - geometry_vec2(b));
}

bool knife_point_on_segment_2d(const KnifePoint2& point, const KnifePoint2& a, const KnifePoint2& b)
{
  constexpr double kTolerance = 0.000001;
  return quader_geometry::point_on_segment_2d(
      geometry_vec2(point), geometry_vec2(a), geometry_vec2(b), kTolerance);
}

bool knife_segments_intersect_2d(const KnifePoint2& a, const KnifePoint2& b, const KnifePoint2& c, const KnifePoint2& d)
{
  constexpr double kTolerance = 0.000001;
  return quader_geometry::intersect_segments_2d(
             geometry_vec2(a), geometry_vec2(b), geometry_vec2(c),
             geometry_vec2(d), kTolerance)
      .hit;
}

double knife_signed_area_2d(std::span<const KnifePoint2> points)
{
    const std::vector<quader_geometry::QVec2d> geometry_points = geometry_vec2_points(points);
    return quader_geometry::polygon_signed_area<double>(std::span<const quader_geometry::QVec2d>(geometry_points));
}

std::vector<KnifePoint2> knife_face_projected_loop(const Document& document, const Face& face, int dropped_axis)
{
    std::vector<KnifePoint2> points;
    points.reserve(face.vertices.size());
    for (const ElementId vertex_id : face.vertices) {
        const Vertex* vertex = find_vertex(document, vertex_id);
        if (vertex == nullptr) {
            points.clear();
            return points;
        }
        points.push_back(knife_project_point(vertex->position, dropped_axis));
    }
    return points;
}

bool knife_point_in_or_on_polygon_2d(const KnifePoint2& point, std::span<const KnifePoint2> polygon)
{
  constexpr double kTolerance = 0.000001;
  const std::vector<quader_geometry::QVec2d> geometry_polygon =
      geometry_vec2_points(polygon);
  return quader_geometry::point_in_or_on_polygon_2d(
      geometry_vec2(point),
      std::span<const quader_geometry::QVec2d>(geometry_polygon), kTolerance);
}

bool knife_vertex_is_on_face_boundary(const Face& face, ElementId vertex_id)
{
    return contains_id(face.vertices, vertex_id);
}

std::optional<KnifeBoundaryTarget> knife_face_boundary_target_at_position(
    const Document& document,
    const Face& face,
    quader::QVec3 position,
    int dropped_axis)
{
    if (face.vertices.size() < 2U) {
        return std::nullopt;
    }

    const KnifePoint2 point = knife_project_point(position, dropped_axis);
    for (std::size_t index = 0; index < face.vertices.size(); ++index) {
        const ElementId current = face.vertices[index];
        const ElementId next = face.vertices[(index + 1U) % face.vertices.size()];
        const Edge edge = make_edge(current, next);
        if (edge.a == kInvalidElementId || edge.b == kInvalidElementId ||
            edge.a == edge.b) {
          continue;
        }

        const KnifePoint2 a = knife_project_vertex(document, edge.a, dropped_axis);
        const KnifePoint2 b = knife_project_vertex(document, edge.b, dropped_axis);
        if (!knife_point_on_segment_2d(point, a, b)) {
            continue;
        }

        const std::optional<float> factor = edge_factor_from_position(document, edge, position);
        if (!factor.has_value()) {
            continue;
        }
        if (*factor <= 0.0001F) {
            return KnifeBoundaryTarget { edge.a, {}, 0.0F };
        }
        if (*factor >= 0.9999F) {
            return KnifeBoundaryTarget { edge.b, {}, 1.0F };
        }

        return KnifeBoundaryTarget{
            kInvalidElementId,
            edge,
            std::clamp(*factor, 0.01F, 0.99F),
        };
    }

    return std::nullopt;
}

bool knife_resolved_point_lies_on_face(const Face& face, const KnifeResolvedStrokePoint& point)
{
    return point.face_id == face.id || knife_vertex_is_on_face_boundary(face, point.vertex_id);
}

bool knife_segment_crosses_existing_edges(
    const Document& document,
    const Face& face,
    Edge segment,
    std::span<const Edge> cut_edges,
    int dropped_axis)
{
    const KnifePoint2 a = knife_project_vertex(document, segment.a, dropped_axis);
    const KnifePoint2 b = knife_project_vertex(document, segment.b, dropped_axis);
    if (knife_distance_squared_2d(a, b) <= 0.000000000001) {
        return true;
    }

    auto crosses_edge = [&](Edge edge) {
        edge = make_edge(edge.a, edge.b);
        if (edge.a == kInvalidElementId || edge.b == kInvalidElementId ||
            edge.a == edge.b) {
          return false;
        }
        if (edge.a == segment.a || edge.a == segment.b || edge.b == segment.a || edge.b == segment.b) {
            return false;
        }

        const KnifePoint2 c = knife_project_vertex(document, edge.a, dropped_axis);
        const KnifePoint2 d = knife_project_vertex(document, edge.b, dropped_axis);
        return knife_segments_intersect_2d(a, b, c, d);
    };

    for (const Edge& edge : face_edges(face)) {
        if (crosses_edge(edge)) {
            return true;
        }
    }
    for (const Edge& edge : cut_edges) {
        if (crosses_edge(edge)) {
            return true;
        }
    }
    return false;
}

bool knife_segment_stays_inside_face(
    const Document& document,
    const Face& face,
    Edge segment,
    int dropped_axis,
    std::span<const KnifePoint2> projected_face)
{
    const Vertex* a = find_vertex(document, segment.a);
    const Vertex* b = find_vertex(document, segment.b);
    if (a == nullptr || b == nullptr) {
        return false;
    }

    const quader::QVec3 mid = (a->position + b->position) * 0.5F;
    return knife_point_in_or_on_polygon_2d(knife_project_point(mid, dropped_axis), projected_face);
}

std::optional<Edge> knife_best_connector_to_boundary(
    const Document& document,
    const Face& face,
    ElementId interior_vertex,
    std::span<const Edge> cut_edges,
    int dropped_axis,
    const std::set<ElementId>& reserved_boundary_vertices = {})
{
    const Vertex* interior = find_vertex(document, interior_vertex);
    if (interior == nullptr || face.vertices.empty()) {
        return std::nullopt;
    }

    std::vector<ElementId> boundary_vertices = face.vertices;
    std::ranges::sort(boundary_vertices, [&document, interior, &reserved_boundary_vertices](ElementId left, ElementId right) {
        const Vertex* left_vertex = find_vertex(document, left);
        const Vertex* right_vertex = find_vertex(document, right);
        const bool left_reserved = reserved_boundary_vertices.contains(left);
        const bool right_reserved = reserved_boundary_vertices.contains(right);
        if (left_reserved != right_reserved) {
            return !left_reserved;
        }
        const float left_distance = left_vertex != nullptr ? length_squared(left_vertex->position - interior->position) : std::numeric_limits<float>::infinity();
        const float right_distance = right_vertex != nullptr ? length_squared(right_vertex->position - interior->position) : std::numeric_limits<float>::infinity();
        if (std::abs(left_distance - right_distance) > kEpsilon) {
          return left_distance < right_distance;
        }
        return left < right;
    });

    const std::vector<KnifePoint2> projected_face = knife_face_projected_loop(document, face, dropped_axis);
    for (const ElementId boundary_vertex : boundary_vertices) {
        if (boundary_vertex == interior_vertex || find_vertex(document, boundary_vertex) == nullptr) {
            continue;
        }

        const Edge connector = make_edge(interior_vertex, boundary_vertex);
        if (contains_edge(cut_edges, connector) || face_uses_edge(face, connector)) {
            continue;
        }
        if (!knife_segment_stays_inside_face(document, face, connector, dropped_axis, projected_face)) {
            continue;
        }
        return connector;
    }

    return std::nullopt;
}

bool knife_add_connector_to_boundary(
    const Document& document,
    const Face& face,
    ElementId interior_vertex,
    std::vector<Edge>& cut_edges,
    int dropped_axis,
    const std::set<ElementId>& reserved_boundary_vertices = {})
{
    const std::optional<Edge> connector =
        knife_best_connector_to_boundary(document, face, interior_vertex, cut_edges, dropped_axis, reserved_boundary_vertices);
    if (!connector.has_value()) {
        return false;
    }
    add_unique_edge(cut_edges, *connector);
    return true;
}

bool knife_add_component_connectors(
    const Document& document,
    const Face& face,
    std::vector<Edge>& cut_edges,
    int dropped_axis,
    std::string& message)
{
    std::map<ElementId, std::vector<ElementId>> adjacency;
    for (const Edge& edge : cut_edges) {
        adjacency[edge.a].push_back(edge.b);
        adjacency[edge.b].push_back(edge.a);
    }

    std::set<ElementId> visited;
    for (const auto& entry : adjacency) {
        const ElementId start = entry.first;
        if (visited.contains(start)) {
            continue;
        }

        std::vector<ElementId> stack { start };
        std::vector<ElementId> component;
        bool touches_boundary = false;
        while (!stack.empty()) {
            const ElementId vertex_id = stack.back();
            stack.pop_back();
            if (!visited.insert(vertex_id).second) {
                continue;
            }
            component.push_back(vertex_id);
            touches_boundary = touches_boundary || knife_vertex_is_on_face_boundary(face, vertex_id);
            for (const ElementId next : adjacency[vertex_id]) {
                if (!visited.contains(next)) {
                    stack.push_back(next);
                }
            }
        }

        bool added_connector = false;
        for (const ElementId vertex_id : component) {
            if (knife_vertex_is_on_face_boundary(face, vertex_id)) {
                continue;
            }
            const auto degree = adjacency.find(vertex_id);
            if (degree != adjacency.end() && degree->second.size() <= 1U) {
                if (!knife_add_connector_to_boundary(document, face, vertex_id, cut_edges, dropped_axis)) {
                    message = "Knife could not attach an open interior point to the face boundary.";
                    return false;
                }
                added_connector = true;
            }
        }

        if (!touches_boundary && !added_connector) {
            std::vector<ElementId> interior_vertices = component;
            std::ranges::sort(interior_vertices, [&document](ElementId left, ElementId right) {
                const Vertex* left_vertex = find_vertex(document, left);
                const Vertex* right_vertex = find_vertex(document, right);
                if (left_vertex != nullptr && right_vertex != nullptr) {
                  if (std::abs(left_vertex->position.x -
                               right_vertex->position.x) > kEpsilon) {
                    return left_vertex->position.x < right_vertex->position.x;
                  }
                  if (std::abs(left_vertex->position.y -
                               right_vertex->position.y) > kEpsilon) {
                    return left_vertex->position.y < right_vertex->position.y;
                  }
                  if (std::abs(left_vertex->position.z -
                               right_vertex->position.z) > kEpsilon) {
                    return left_vertex->position.z < right_vertex->position.z;
                  }
                }
                return left < right;
            });

            std::set<ElementId> reserved_boundary_vertices;
            int connectors_added = 0;
            for (const ElementId interior_vertex : interior_vertices) {
                const std::optional<Edge> connector =
                    knife_best_connector_to_boundary(document, face, interior_vertex, cut_edges, dropped_axis, reserved_boundary_vertices);
                if (!connector.has_value()) {
                    continue;
                }
                reserved_boundary_vertices.insert(connector->a == interior_vertex ? connector->b : connector->a);
                add_unique_edge(cut_edges, *connector);
                ++connectors_added;
                if (connectors_added >= 2) {
                    break;
                }
            }

            if (connectors_added < 2) {
                message = "Knife could not attach a closed interior cut loop to the face boundary.";
                return false;
            }
        }
    }

    return true;
}

bool knife_validate_cut_edges_for_face(
    const Document& document,
    const Face& face,
    std::span<const Edge> cut_edges,
    int dropped_axis,
    std::string& message)
{
    const std::vector<KnifePoint2> projected_face = knife_face_projected_loop(document, face, dropped_axis);
    if (projected_face.size() < 3U) {
        message = "Knife target face was not valid.";
        return false;
    }

    for (std::size_t index = 0; index < cut_edges.size(); ++index) {
        const Edge edge = cut_edges[index];
        if (edge.a == kInvalidElementId || edge.b == kInvalidElementId ||
            edge.a == edge.b) {
          message = "Knife cut contains an invalid segment.";
          return false;
        }
    }
    return true;
}

std::optional<KnifePoint2> knife_proper_segment_intersection_2d(
    const KnifePoint2& a,
    const KnifePoint2& b,
    const KnifePoint2& c,
    const KnifePoint2& d)
{
  constexpr double kTolerance = 0.000001;
  const quader_geometry::QSegmentIntersection2<double> intersection =
      quader_geometry::proper_segment_intersection_2d(
          geometry_vec2(a), geometry_vec2(b), geometry_vec2(c),
          geometry_vec2(d), kTolerance);
  if (!intersection.hit) {
    return std::nullopt;
  }
    return poly_vec2(intersection.point);
}

quader::QVec3 knife_unproject_face_point(const Document& document, const Face& face, int dropped_axis, const KnifePoint2& point)
{
    const Vertex* origin = !face.vertices.empty() ? find_vertex(document, face.vertices.front()) : nullptr;
    const quader::QVec3 normal = face_normal(document, face);
    if (origin == nullptr || length_squared(normal) <= kEpsilon) {
      return {};
    }

    const quader_geometry::QPlane3<float> plane =
        quader_geometry::plane_from_point_normal<float>(
            geometry_vec3(origin->position), geometry_vec3(normal), kEpsilon);
    if (quader_geometry::length_squared(plane.normal) <= kEpsilon) {
      return {};
    }

    const quader_geometry::QDominantAxisUnprojectResult<float> unprojected =
        quader_geometry::unproject_dominant_axis_point_to_plane<float>(
            {static_cast<float>(point[0]), static_cast<float>(point[1])}, plane,
            geometry_axis_from_dropped_axis(dropped_axis), kEpsilon);
    if (unprojected.valid) {
        return poly_vec3(unprojected.point);
    }

    const quader_geometry::QVec3f fallback_seed =
        dropped_axis == 0 ?
            quader_geometry::QVec3f { origin->position.x, static_cast<float>(point[0]), static_cast<float>(point[1]) } :
        dropped_axis == 1 ?
            quader_geometry::QVec3f { static_cast<float>(point[0]), origin->position.y, static_cast<float>(point[1]) } :
            quader_geometry::QVec3f { static_cast<float>(point[0]), static_cast<float>(point[1]), origin->position.z };
    return poly_vec3(quader_geometry::project_point_to_plane<float>(fallback_seed, plane));
}

ElementId knife_vertex_at_position(Document& document, quader::QVec3 position, float tolerance = 0.00001F)
{
    const float tolerance_squared = tolerance * tolerance;
    for (const Vertex& vertex : document.vertices) {
        if (length_squared(vertex.position - position) <= tolerance_squared) {
            return vertex.id;
        }
    }
    return add_vertex(document, position);
}

void knife_split_cut_edge_intersections(Document& document, const Face& face, std::vector<Edge>& cut_edges, int dropped_axis)
{
    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t first_index = 0; first_index < cut_edges.size() && !changed; ++first_index) {
            const Edge first = cut_edges[first_index];
            if (first.a == kInvalidElementId || first.b == kInvalidElementId ||
                first.a == first.b) {
              continue;
            }
            const KnifePoint2 first_a = knife_project_vertex(document, first.a, dropped_axis);
            const KnifePoint2 first_b = knife_project_vertex(document, first.b, dropped_axis);
            for (std::size_t second_index = first_index + 1U; second_index < cut_edges.size(); ++second_index) {
                const Edge second = cut_edges[second_index];
                if (second.a == kInvalidElementId ||
                    second.b == kInvalidElementId || second.a == second.b ||
                    first.a == second.a || first.a == second.b ||
                    first.b == second.a || first.b == second.b) {
                  continue;
                }

                const KnifePoint2 second_a = knife_project_vertex(document, second.a, dropped_axis);
                const KnifePoint2 second_b = knife_project_vertex(document, second.b, dropped_axis);
                const std::optional<KnifePoint2> intersection =
                    knife_proper_segment_intersection_2d(first_a, first_b, second_a, second_b);
                if (!intersection.has_value()) {
                    continue;
                }

                const ElementId intersection_vertex =
                    knife_vertex_at_position(document, knife_unproject_face_point(document, face, dropped_axis, *intersection));
                std::vector<Edge> next_edges;
                next_edges.reserve(cut_edges.size() + 2U);
                for (std::size_t edge_index = 0; edge_index < cut_edges.size(); ++edge_index) {
                    if (edge_index == first_index) {
                        add_unique_edge(next_edges, make_edge(first.a, intersection_vertex));
                        add_unique_edge(next_edges, make_edge(intersection_vertex, first.b));
                    } else if (edge_index == second_index) {
                        add_unique_edge(next_edges, make_edge(second.a, intersection_vertex));
                        add_unique_edge(next_edges, make_edge(intersection_vertex, second.b));
                    } else {
                        add_unique_edge(next_edges, cut_edges[edge_index]);
                    }
                }
                cut_edges = std::move(next_edges);
                changed = true;
                break;
            }
        }
    }
}

bool knife_face_loop_is_valid(const Document& document, std::span<const ElementId> loop)
{
  if (loop.size() < 3U || has_repeated_vertex(loop) ||
      face_loop_area_score(document, loop) <= kFaceAreaScoreEpsilon) {
    return false;
  }

    Face face;
    face.vertices.assign(loop.begin(), loop.end());
    return !triangulate_face_local_indices(document, face).empty();
}

void knife_orient_loop_like_source(const Document& document, const Face& source_face, std::vector<ElementId>& loop, int dropped_axis)
{
    const std::vector<KnifePoint2> source_projected = knife_face_projected_loop(document, source_face, dropped_axis);
    std::vector<KnifePoint2> loop_projected;
    loop_projected.reserve(loop.size());
    for (const ElementId vertex_id : loop) {
        loop_projected.push_back(knife_project_vertex(document, vertex_id, dropped_axis));
    }

    const double source_area = knife_signed_area_2d(source_projected);
    const double loop_area = knife_signed_area_2d(loop_projected);
    if (std::abs(source_area) > 0.000001 && std::abs(loop_area) > 0.000001 && (source_area > 0.0) != (loop_area > 0.0)) {
        std::ranges::reverse(loop);
    }
}

std::vector<std::vector<ElementId>> knife_partition_open_chain_face_loops(
    const Document& document,
    const Face& face,
    std::span<const Edge> cut_edges,
    int dropped_axis)
{
    if (cut_edges.empty()) {
        return {};
    }

    std::map<ElementId, std::vector<ElementId>> adjacency;
    for (Edge edge : cut_edges) {
        edge = make_edge(edge.a, edge.b);
        if (edge.a == kInvalidElementId || edge.b == kInvalidElementId ||
            edge.a == edge.b || face_uses_edge(face, edge)) {
          continue;
        }
        add_unique_id(adjacency[edge.a], edge.b);
        add_unique_id(adjacency[edge.b], edge.a);
    }

    if (adjacency.empty()) {
        return {};
    }

    std::vector<ElementId> endpoints;
    for (const auto& entry : adjacency) {
        if (entry.second.size() == 1U) {
            endpoints.push_back(entry.first);
        } else if (entry.second.size() != 2U) {
            return {};
        }
    }
    if (endpoints.size() != 2U ||
        !knife_vertex_is_on_face_boundary(face, endpoints[0]) ||
        !knife_vertex_is_on_face_boundary(face, endpoints[1])) {
        return {};
    }

    std::vector<ElementId> chain;
    chain.reserve(adjacency.size());
    ElementId previous = kInvalidElementId;
    ElementId current = endpoints[0];
    for (std::size_t guard = 0; guard <= adjacency.size(); ++guard) {
        chain.push_back(current);
        if (current == endpoints[1]) {
            break;
        }

        const auto found = adjacency.find(current);
        if (found == adjacency.end()) {
            return {};
        }

        ElementId next = kInvalidElementId;
        for (const ElementId candidate : found->second) {
            if (candidate != previous) {
                next = candidate;
                break;
            }
        }
        if (next == kInvalidElementId) {
          return {};
        }
        previous = current;
        current = next;
    }

    if (chain.size() != adjacency.size() || chain.back() != endpoints[1]) {
        return {};
    }

    const std::optional<std::size_t> first_index = face_vertex_index(face, chain.front());
    const std::optional<std::size_t> second_index = face_vertex_index(face, chain.back());
    if (!first_index.has_value() || !second_index.has_value() || *first_index == *second_index) {
        return {};
    }

    std::vector<ElementId> first_loop = loop_between_indices(face.vertices, *first_index, *second_index);
    for (std::size_t index = chain.size() - 1U; index-- > 1U;) {
        first_loop.push_back(chain[index]);
    }
    std::vector<ElementId> second_loop = loop_between_indices(face.vertices, *second_index, *first_index);
    for (std::size_t index = 1; index + 1U < chain.size(); ++index) {
        second_loop.push_back(chain[index]);
    }

    first_loop = unique_valid_face_loop(std::move(first_loop));
    second_loop = unique_valid_face_loop(std::move(second_loop));
    knife_orient_loop_like_source(document, face, first_loop, dropped_axis);
    knife_orient_loop_like_source(document, face, second_loop, dropped_axis);
    if (!knife_face_loop_is_valid(document, first_loop) || !knife_face_loop_is_valid(document, second_loop)) {
        return {};
    }

    return { std::move(first_loop), std::move(second_loop) };
}

std::vector<std::vector<ElementId>> knife_partition_face_loops_for_turn(
    const Document& document,
    const Face& face,
    std::span<const Edge> cut_edges,
    int dropped_axis,
    bool previous_turn)
{
    std::map<ElementId, KnifePoint2> points;
    std::map<ElementId, std::vector<ElementId>> adjacency;
    const auto add_vertex = [&](ElementId vertex_id) {
        if (points.contains(vertex_id)) {
            return true;
        }
        const Vertex* vertex = find_vertex(document, vertex_id);
        if (vertex == nullptr) {
            return false;
        }
        points[vertex_id] = knife_project_point(vertex->position, dropped_axis);
        return true;
    };
    const auto add_graph_edge = [&](Edge edge) {
        edge = make_edge(edge.a, edge.b);
        if (edge.a == kInvalidElementId || edge.b == kInvalidElementId ||
            edge.a == edge.b) {
          return false;
        }
        if (!add_vertex(edge.a) || !add_vertex(edge.b)) {
            return false;
        }
        add_unique_id(adjacency[edge.a], edge.b);
        add_unique_id(adjacency[edge.b], edge.a);
        return true;
    };

    for (const Edge& edge : face_edges(face)) {
        if (!add_graph_edge(edge)) {
            return {};
        }
    }
    for (const Edge& edge : cut_edges) {
        if (!face_uses_edge(face, edge) && !add_graph_edge(edge)) {
            return {};
        }
    }

    for (auto& entry : adjacency) {
        const KnifePoint2 origin = points[entry.first];
        std::ranges::sort(entry.second, [&points, &origin](ElementId left, ElementId right) {
            const KnifePoint2& left_point = points[left];
            const KnifePoint2& right_point = points[right];
            const double left_angle = std::atan2(left_point[1] - origin[1], left_point[0] - origin[0]);
            const double right_angle = std::atan2(right_point[1] - origin[1], right_point[0] - origin[0]);
            if (std::abs(left_angle - right_angle) > 0.000000001) {
                return left_angle < right_angle;
            }
            return left < right;
        });
    }

    const std::vector<KnifePoint2> source_loop = knife_face_projected_loop(document, face, dropped_axis);
    const double source_area = knife_signed_area_2d(source_loop);
    if (std::abs(source_area) <= 0.000001) {
        return {};
    }

    std::set<std::pair<ElementId, ElementId>> visited;
    std::set<std::vector<ElementId>> emitted_keys;
    std::vector<std::vector<ElementId>> loops;
    for (const auto& entry : adjacency) {
        const ElementId start = entry.first;
        for (const ElementId next : entry.second) {
            if (visited.contains({ start, next })) {
                continue;
            }

            std::vector<ElementId> loop;
            ElementId from = start;
            ElementId to = next;
            for (std::size_t guard = 0; guard < adjacency.size() * adjacency.size() + 8U; ++guard) {
                if (visited.contains({ from, to })) {
                    break;
                }
                visited.insert({ from, to });
                loop.push_back(from);

                const auto neighbor_entry = adjacency.find(to);
                if (neighbor_entry == adjacency.end() || neighbor_entry->second.empty()) {
                    loop.clear();
                    break;
                }
                const std::vector<ElementId>& neighbors = neighbor_entry->second;
                const auto incoming = std::ranges::find(neighbors, from);
                if (incoming == neighbors.end()) {
                    loop.clear();
                    break;
                }
                const std::size_t incoming_index = static_cast<std::size_t>(std::distance(neighbors.begin(), incoming));
                const std::size_t next_index = previous_turn ?
                    (incoming_index == 0 ? neighbors.size() - 1U : incoming_index - 1U) :
                    ((incoming_index + 1U) % neighbors.size());
                from = to;
                to = neighbors[next_index];
                if (from == start && to == next) {
                    break;
                }
            }

            if (loop.size() < 3U || has_repeated_vertex(loop)) {
                continue;
            }

            std::vector<KnifePoint2> projected_loop;
            projected_loop.reserve(loop.size());
            for (const ElementId vertex_id : loop) {
                projected_loop.push_back(points[vertex_id]);
            }
            const double area = knife_signed_area_2d(projected_loop);
            if (std::abs(area) <= 0.000001 || (area > 0.0) != (source_area > 0.0)) {
                continue;
            }

            KnifePoint2 centroid {};
            for (const KnifePoint2& point : projected_loop) {
                centroid[0] += point[0];
                centroid[1] += point[1];
            }
            centroid[0] /= static_cast<double>(projected_loop.size());
            centroid[1] /= static_cast<double>(projected_loop.size());
            if (!knife_point_in_or_on_polygon_2d(centroid, source_loop)) {
                continue;
            }

            std::vector<ElementId> key = loop;
            std::ranges::sort(key);
            if (!emitted_keys.insert(std::move(key)).second) {
                continue;
            }
            loops.push_back(std::move(loop));
        }
    }

    return loops;
}

std::set<ElementId> knife_required_cut_vertices(const Face& face, std::span<const Edge> cut_edges)
{
    std::set<ElementId> vertices;
    for (Edge edge : cut_edges) {
        edge = make_edge(edge.a, edge.b);
        if (edge.a == kInvalidElementId || edge.b == kInvalidElementId ||
            edge.a == edge.b || face_uses_edge(face, edge)) {
          continue;
        }
        vertices.insert(edge.a);
        vertices.insert(edge.b);
    }
    return vertices;
}

std::size_t knife_loop_vertex_coverage(
    std::span<const std::vector<ElementId>> loops,
    const std::set<ElementId>& required_vertices)
{
    if (required_vertices.empty()) {
        return 0;
    }

    std::set<ElementId> covered;
    for (const std::vector<ElementId>& loop : loops) {
        for (const ElementId vertex_id : loop) {
            if (required_vertices.contains(vertex_id)) {
                covered.insert(vertex_id);
            }
        }
    }
    return covered.size();
}

bool knife_loops_cover_required_vertices(
    std::span<const std::vector<ElementId>> loops,
    const std::set<ElementId>& required_vertices)
{
    return knife_loop_vertex_coverage(loops, required_vertices) == required_vertices.size();
}

std::vector<ElementId> knife_uncovered_required_vertices(
    std::span<const std::vector<ElementId>> loops,
    const std::set<ElementId>& required_vertices)
{
    std::set<ElementId> covered;
    for (const std::vector<ElementId>& loop : loops) {
        for (const ElementId vertex_id : loop) {
            if (required_vertices.contains(vertex_id)) {
                covered.insert(vertex_id);
            }
        }
    }

    std::vector<ElementId> uncovered;
    for (const ElementId vertex_id : required_vertices) {
        if (!covered.contains(vertex_id)) {
            uncovered.push_back(vertex_id);
        }
    }
    return uncovered;
}

std::vector<std::vector<ElementId>> knife_partition_face_loops(
    const Document& document,
    const Face& face,
    std::span<const Edge> cut_edges,
    int dropped_axis)
{
    std::vector<std::vector<ElementId>> previous_loops =
        knife_partition_face_loops_for_turn(document, face, cut_edges, dropped_axis, true);
    std::vector<std::vector<ElementId>> next_loops =
        knife_partition_face_loops_for_turn(document, face, cut_edges, dropped_axis, false);
    if (previous_loops.empty()) {
        return next_loops;
    }
    if (next_loops.empty()) {
        return previous_loops;
    }

    const std::set<ElementId> required_vertices = knife_required_cut_vertices(face, cut_edges);
    const std::size_t previous_coverage = knife_loop_vertex_coverage(previous_loops, required_vertices);
    const std::size_t next_coverage = knife_loop_vertex_coverage(next_loops, required_vertices);
    if (next_coverage > previous_coverage) {
        return next_loops;
    }
    return previous_loops;
}

std::vector<std::vector<ElementId>> knife_partition_face_loops_with_repairs(
    Document& document,
    const Face& face,
    std::vector<Edge>& cut_edges,
    int dropped_axis,
    std::string& message)
{
  constexpr int kMaxRepairPasses = 4;
  std::vector<std::vector<ElementId>> loops;
  for (int pass = 0; pass < kMaxRepairPasses; ++pass) {
    loops = knife_partition_open_chain_face_loops(document, face, cut_edges,
                                                  dropped_axis);
    if (loops.empty()) {
      loops =
          knife_partition_face_loops(document, face, cut_edges, dropped_axis);
    }

    const std::set<ElementId> required_vertices =
        knife_required_cut_vertices(face, cut_edges);
    if (!loops.empty() &&
        knife_loops_cover_required_vertices(loops, required_vertices)) {
      return loops;
    }

    bool repaired = false;
    const std::vector<ElementId> uncovered_vertices =
        knife_uncovered_required_vertices(loops, required_vertices);
    for (const ElementId vertex_id : uncovered_vertices) {
      const std::optional<Edge> connector = knife_best_connector_to_boundary(
          document, face, vertex_id, cut_edges, dropped_axis);
      if (!connector.has_value()) {
        continue;
      }
      const std::size_t before_size = cut_edges.size();
      add_unique_edge(cut_edges, *connector);
      repaired = cut_edges.size() != before_size || repaired;
    }

    if (!repaired) {
      break;
    }

    knife_split_cut_edge_intersections(document, face, cut_edges, dropped_axis);
    if (!knife_add_component_connectors(document, face, cut_edges, dropped_axis,
                                        message)) {
      return {};
    }
    knife_split_cut_edge_intersections(document, face, cut_edges, dropped_axis);
  }

    return loops;
}

std::optional<ElementId> knife_segment_face_id(
    const Document& document,
    const std::vector<KnifeResolvedStrokePoint>& points,
    const KnifeStrokeSegment& segment)
{
    if (segment.first_point >= points.size() || segment.second_point >= points.size()) {
        return std::nullopt;
    }
    const KnifeResolvedStrokePoint& first = points[segment.first_point];
    const KnifeResolvedStrokePoint& second = points[segment.second_point];
    if (first.vertex_id == kInvalidElementId ||
        second.vertex_id == kInvalidElementId ||
        first.vertex_id == second.vertex_id) {
      return std::nullopt;
    }

    if (first.face_id != kInvalidElementId &&
        second.face_id != kInvalidElementId &&
        first.face_id == second.face_id) {
      const Face *face = find_face(document, first.face_id);
      if (face != nullptr && knife_resolved_point_lies_on_face(*face, first) &&
          knife_resolved_point_lies_on_face(*face, second)) {
        return first.face_id;
      }
      return std::nullopt;
    }

    std::vector<ElementId> matching_faces;
    for (const Face& face : document.faces) {
        if (knife_resolved_point_lies_on_face(face, first) && knife_resolved_point_lies_on_face(face, second)) {
            matching_faces.push_back(face.id);
        }
    }
    if (matching_faces.size() != 1U) {
        return std::nullopt;
    }
    return matching_faces.front();
}

std::vector<Edge> knife_shared_face_edges(const Face& first, const Face& second)
{
    std::vector<Edge> shared_edges;
    for (const Edge& edge : face_edges(first)) {
        if (face_uses_edge(second, edge)) {
            add_unique_edge(shared_edges, edge);
        }
    }
    return shared_edges;
}

bool knife_vertex_is_on_shared_face_boundary(const Face& first, const Face& second, ElementId vertex_id)
{
    return knife_vertex_is_on_face_boundary(first, vertex_id) && knife_vertex_is_on_face_boundary(second, vertex_id);
}

float knife_edge_factor_nearest_segment(
    const Document& document,
    Edge edge,
    quader::QVec3 segment_start,
    quader::QVec3 segment_end)
{
    edge = make_edge(edge.a, edge.b);
    const Vertex* edge_start = find_vertex(document, edge.a);
    const Vertex* edge_end = find_vertex(document, edge.b);
    if (edge_start == nullptr || edge_end == nullptr) {
        return 0.5F;
    }

    quader_geometry::QSegmentSegmentFactorOptions<float> options;
    options.length_squared_epsilon = kEpsilon;
    options.closest_epsilon = kEpsilon;
    options.parameter_epsilon = kEpsilon;
    options.min_factor = 0.01F;
    options.max_factor = 0.99F;

    const quader_geometry::QSegmentFactorClosestPoint3<float> closest =
        quader_geometry::closest_factor_on_segment_to_segment<float>(
            { geometry_vec3(edge_start->position), geometry_vec3(edge_end->position) },
            { geometry_vec3(segment_start), geometry_vec3(segment_end) },
            options);
    return closest.valid ? closest.segment_factor : 0.5F;
}

float knife_distance_squared_to_segment(quader::QVec3 point, quader::QVec3 segment_start, quader::QVec3 segment_end)
{
  return quader_geometry::point_segment_distance_squared<float>(
      geometry_vec3(point),
      {geometry_vec3(segment_start), geometry_vec3(segment_end)},
      std::sqrt(kEpsilon));
}

bool knife_add_face_graph_edge(
    KnifeStrokeCandidate& build,
    std::map<ElementId, KnifeFaceGraph>& face_graphs,
    ElementId face_id,
    Edge edge)
{
    const Face* face = find_face(build.document, face_id);
    if (face == nullptr) {
        return false;
    }

    edge = make_edge(edge.a, edge.b);
    if (edge.a == kInvalidElementId || edge.b == kInvalidElementId ||
        edge.a == edge.b || face_uses_edge(*face, edge)) {
      return false;
    }

    KnifeFaceGraph& graph = face_graphs[face_id];
    graph.face_id = face_id;
    add_unique_edge(graph.cut_edges, edge);
    return true;
}

bool knife_face_graph_uses_vertex(const KnifeFaceGraph& graph, ElementId vertex_id)
{
    return std::ranges::any_of(graph.cut_edges, [vertex_id](Edge edge) {
        return edge.a == vertex_id || edge.b == vertex_id;
    });
}

bool knife_any_face_graph_uses_vertex(const std::map<ElementId, KnifeFaceGraph>& face_graphs, ElementId vertex_id)
{
    return std::ranges::any_of(face_graphs, [vertex_id](const auto& entry) {
        return knife_face_graph_uses_vertex(entry.second, vertex_id);
    });
}

bool knife_ensure_point_connected_to_face_graph(
    Document& document,
    std::map<ElementId, KnifeFaceGraph>& face_graphs,
    const KnifeResolvedStrokePoint& point,
    std::string& message)
{
  if (point.vertex_id == kInvalidElementId ||
      knife_any_face_graph_uses_vertex(face_graphs, point.vertex_id)) {
    return true;
  }
  if (point.face_id == kInvalidElementId) {
    return true;
  }

    const Face* face = find_face(document, point.face_id);
    if (face == nullptr) {
        return true;
    }

    KnifeFaceGraph& graph = face_graphs[face->id];
    graph.face_id = face->id;
    if (knife_face_graph_uses_vertex(graph, point.vertex_id)) {
        return true;
    }

    const int dropped_axis = dropped_axis_for_normal(face_normal(document, *face));
    const std::optional<Edge> connector =
        knife_best_connector_to_boundary(document, *face, point.vertex_id, graph.cut_edges, dropped_axis);
    if (!connector.has_value()) {
        message = "Knife stroke could not connect every placed cut point.";
        return false;
    }

    add_unique_edge(graph.cut_edges, *connector);
    return true;
}

KnifeStrokeCandidate build_knife_stroke_candidate(
    const Document& document,
    std::span<const KnifePointTarget> targets,
    std::span<const KnifeStrokeSegment> stroke_segments)
{
    KnifeStrokeCandidate build;
    build.document = document;
    if (targets.size() < 2U || stroke_segments.empty()) {
        build.message = "Knife needs at least two cut points.";
        return build;
    }

    KnifeEdgeSplitMap split_vertices;
    for (const KnifePointTarget& target : targets) {
      if (target.kind == KnifePointTargetKind::ExistingEdge) {
        const Edge edge = target.edge;
        const Edge normalized_edge = make_edge(edge.a, edge.b);
        const float factor = std::clamp(
            std::isfinite(target.edge_factor) ? target.edge_factor : 0.5F,
            0.01F, 0.99F);
        if (normalized_edge.a == kInvalidElementId ||
            normalized_edge.b == kInvalidElementId ||
            normalized_edge.a == normalized_edge.b ||
            !edge_exists(build.document, normalized_edge)) {
          build.message = "Knife edge target was not found.";
          return build;
        }
        const ElementId vertex_id = split_vertex_for_knife_edge(
            build.document, split_vertices, edge, factor);
        if (vertex_id == kInvalidElementId) {
          build.message = "Knife could not split the target edge.";
          return build;
        }
      } else if (target.kind == KnifePointTargetKind::FacePoint) {
        const Face *face = find_face(build.document, target.face_id);
        if (face == nullptr) {
          build.message = "Knife face point target was not found.";
          return build;
        }

        const quader::QVec3 normal = face_normal(build.document, *face);
        const int dropped_axis = dropped_axis_for_normal(normal);
        const std::optional<KnifeBoundaryTarget> boundary =
            knife_face_boundary_target_at_position(
                build.document, *face, target.position, dropped_axis);
        if (boundary.has_value() && boundary->vertex_id == kInvalidElementId) {
          const ElementId vertex_id = split_vertex_for_knife_edge(
              build.document, split_vertices, boundary->edge,
              boundary->edge_factor);
          if (vertex_id == kInvalidElementId) {
            build.message = "Knife could not split the target edge.";
            return build;
          }
        }
      }
    }

    if (!knife_edge_splits_empty(split_vertices) && !apply_knife_edge_splits_to_faces(build.document, split_vertices)) {
        build.message = "Knife could not resolve the target edge boundaries.";
        return build;
    }

    build.points.reserve(targets.size());
    for (const KnifePointTarget& target : targets) {
        KnifeResolvedStrokePoint resolved;
        resolved.face_id = target.face_id;
        switch (target.kind) {
        case KnifePointTargetKind::ExistingVertex:
        case KnifePointTargetKind::InsertedVertex: {
          const Vertex *vertex = find_vertex(build.document, target.vertex_id);
          if (vertex == nullptr) {
            build.message = "Knife vertex target was not found.";
            return build;
          }
          resolved.vertex_id = vertex->id;
          resolved.position = vertex->position;
          break;
        }
        case KnifePointTargetKind::ExistingEdge: {
          const Edge edge = target.edge;
          const float factor = std::clamp(
              std::isfinite(target.edge_factor) ? target.edge_factor : 0.5F,
              0.01F, 0.99F);
          const ElementId vertex_id = split_vertex_for_knife_edge(
              build.document, split_vertices, edge, factor);
          const Vertex *vertex = find_vertex(build.document, vertex_id);
          if (vertex == nullptr) {
            build.message = "Knife split vertex was not found.";
            return build;
          }
          resolved.vertex_id = vertex->id;
          resolved.position = vertex->position;
          break;
        }
        case KnifePointTargetKind::FacePoint: {
          const Face *face = find_face(build.document, target.face_id);
          if (face == nullptr) {
            build.message = "Knife face point target was not found.";
            return build;
          }
          const quader::QVec3 normal = face_normal(build.document, *face);
          const int dropped_axis = dropped_axis_for_normal(normal);
          const std::vector<KnifePoint2> projected_face =
              knife_face_projected_loop(build.document, *face, dropped_axis);
          if (!knife_point_in_or_on_polygon_2d(
                  knife_project_point(target.position, dropped_axis),
                  projected_face)) {
            build.message =
                "Knife face point must stay inside the target face.";
            return build;
          }
          const std::optional<KnifeBoundaryTarget> boundary =
              knife_face_boundary_target_at_position(
                  build.document, *face, target.position, dropped_axis);
          if (boundary.has_value() &&
              boundary->vertex_id != kInvalidElementId) {
            const Vertex *vertex =
                find_vertex(build.document, boundary->vertex_id);
            if (vertex == nullptr) {
              build.message = "Knife face boundary vertex was not found.";
              return build;
            }
            resolved.vertex_id = vertex->id;
            resolved.position = vertex->position;
          } else if (boundary.has_value()) {
            const ElementId vertex_id = split_vertex_for_knife_edge(
                build.document, split_vertices, boundary->edge,
                boundary->edge_factor);
            const Vertex *vertex = find_vertex(build.document, vertex_id);
            if (vertex == nullptr) {
              build.message = "Knife split vertex was not found.";
              return build;
            }
            resolved.vertex_id = vertex->id;
            resolved.position = vertex->position;
          } else {
            resolved.vertex_id = add_vertex(build.document, target.position);
            resolved.position = target.position;
          }
          resolved.face_id = target.face_id;
          break;
        }
        }

        add_unique_id(build.selected_vertices, resolved.vertex_id);
        build.points.push_back(resolved);
    }

    std::map<ElementId, KnifeFaceGraph> face_graphs;
    const std::size_t applied_split_vertex_count = knife_edge_split_count(split_vertices);
    for (const KnifeStrokeSegment& segment : stroke_segments) {
        if (segment.first_point >= build.points.size() || segment.second_point >= build.points.size()) {
            build.message = "Knife stroke segment referenced a missing point.";
            return build;
        }
        if (segment.first_point == segment.second_point) {
            continue;
        }

        const std::optional<ElementId> face_id = knife_segment_face_id(build.document, build.points, segment);
        const KnifeResolvedStrokePoint& first_point = build.points[segment.first_point];
        const KnifeResolvedStrokePoint& second_point = build.points[segment.second_point];
        if (face_id.has_value()) {
            if (knife_add_face_graph_edge(
                    build,
                    face_graphs,
                    *face_id,
                    make_edge(first_point.vertex_id, second_point.vertex_id))) {
                build.segments.push_back({ segment.first_point, segment.second_point, *face_id });
            }
            continue;
        }

        const Face *first_face =
            first_point.face_id != kInvalidElementId
                ? find_face(build.document, first_point.face_id)
                : nullptr;
        const Face *second_face =
            second_point.face_id != kInvalidElementId
                ? find_face(build.document, second_point.face_id)
                : nullptr;
        if (first_face == nullptr || second_face == nullptr || first_face->id == second_face->id) {
            continue;
        }

        const std::vector<Edge> shared_edges = knife_shared_face_edges(*first_face, *second_face);
        if (shared_edges.empty()) {
            continue;
        }

        ElementId boundary_vertex = kInvalidElementId;
        if (knife_vertex_is_on_shared_face_boundary(*first_face, *second_face, first_point.vertex_id)) {
            boundary_vertex = first_point.vertex_id;
        } else if (knife_vertex_is_on_shared_face_boundary(*first_face, *second_face, second_point.vertex_id)) {
            boundary_vertex = second_point.vertex_id;
        } else {
            float best_distance_squared = std::numeric_limits<float>::infinity();
            Edge best_edge {};
            float best_factor = 0.5F;
            for (const Edge& shared_edge : shared_edges) {
                const float factor = knife_edge_factor_nearest_segment(
                    build.document,
                    shared_edge,
                    first_point.position,
                    second_point.position);
                const std::optional<quader::QVec3> edge_position = split_edge_position(build.document, shared_edge, factor);
                if (!edge_position.has_value()) {
                    continue;
                }
                const float distance_squared =
                    knife_distance_squared_to_segment(*edge_position, first_point.position, second_point.position);
                if (distance_squared < best_distance_squared) {
                    best_distance_squared = distance_squared;
                    best_edge = shared_edge;
                    best_factor = factor;
                }
            }
            if (best_distance_squared < std::numeric_limits<float>::infinity()) {
                boundary_vertex = split_vertex_for_knife_edge(build.document, split_vertices, best_edge, best_factor);
            }
        }
        if (boundary_vertex == kInvalidElementId ||
            find_vertex(build.document, boundary_vertex) == nullptr) {
          continue;
        }

        bool routed = false;
        routed = knife_add_face_graph_edge(build, face_graphs, first_face->id, make_edge(first_point.vertex_id, boundary_vertex)) || routed;
        routed = knife_add_face_graph_edge(build, face_graphs, second_face->id, make_edge(boundary_vertex, second_point.vertex_id)) || routed;
        if (routed) {
            build.segments.push_back({ segment.first_point, segment.second_point, first_face->id });
        }
    }

    if (knife_edge_split_count(split_vertices) > applied_split_vertex_count && !apply_knife_edge_splits_to_faces(build.document, split_vertices)) {
        build.message = "Knife could not route the cut through a shared face boundary.";
        return build;
    }

    for (const KnifeResolvedStrokePoint& point : build.points) {
        if (!knife_ensure_point_connected_to_face_graph(build.document, face_graphs, point, build.message)) {
            return build;
        }
    }

    if (face_graphs.empty()) {
        build.message = "Knife stroke did not create any face cuts.";
        return build;
    }

    std::set<ElementId> used_face_ids;
    for (const Face& face : build.document.faces) {
        used_face_ids.insert(face.id);
    }
    auto next_face_id = [&build, &used_face_ids]() {
      while (build.document.next_face_id == kInvalidElementId ||
             used_face_ids.contains(build.document.next_face_id)) {
        ++build.document.next_face_id;
      }
      const ElementId face_id = build.document.next_face_id++;
      used_face_ids.insert(face_id);
      return face_id;
    };

    std::vector<Face> rebuilt_faces;
    rebuilt_faces.reserve(build.document.faces.size() + stroke_segments.size());
    std::size_t split_face_count = 0;
    for (const Face& face : build.document.faces) {
        const auto graph = face_graphs.find(face.id);
        if (graph == face_graphs.end()) {
            rebuilt_faces.push_back(face);
            continue;
        }

        std::vector<Edge> cut_edges = graph->second.cut_edges;
        const int dropped_axis = dropped_axis_for_normal(face_normal(build.document, face));
        knife_split_cut_edge_intersections(build.document, face, cut_edges, dropped_axis);
        if (!knife_add_component_connectors(build.document, face, cut_edges, dropped_axis, build.message)) {
            return build;
        }
        knife_split_cut_edge_intersections(build.document, face, cut_edges, dropped_axis);
        if (!knife_validate_cut_edges_for_face(build.document, face, cut_edges, dropped_axis, build.message)) {
            return build;
        }

        std::vector<std::vector<ElementId>> loops =
            knife_partition_face_loops_with_repairs(build.document, face, cut_edges, dropped_axis, build.message);
        if (loops.size() < 2U) {
            rebuilt_faces.push_back(face);
            continue;
        }

        ++split_face_count;
        for (std::size_t loop_index = 0; loop_index < loops.size(); ++loop_index) {
            std::vector<ElementId> loop = unique_valid_face_loop(loops[loop_index]);
            if (loop.size() < 3U || has_repeated_vertex(loop)) {
                continue;
            }

            Face rebuilt_face = face;
            rebuilt_face.id = loop_index == 0 ? face.id : next_face_id();
            rebuilt_face.vertices = std::move(loop);
            rebuilt_face.uvs.clear();
            rebuilt_faces.push_back(std::move(rebuilt_face));
        }
    }

    if (split_face_count == 0U) {
        build.message = "Knife cut did not split the target face.";
        return build;
    }

    build.document.faces = std::move(rebuilt_faces);
    prune_invalid_faces(build.document);
    prune_unused_vertices(build.document);
    restore_source_face_orientation(document, build.document);
    for (const ElementId vertex_id : build.selected_vertices) {
        if (find_vertex(build.document, vertex_id) == nullptr ||
            !std::ranges::any_of(build.document.faces, [vertex_id](const Face& face) {
                return contains_id(face.vertices, vertex_id);
            })) {
            build.message = "Knife stroke could not keep every placed cut point.";
            return build;
        }
    }
    if (!every_face_triangulates(build.document)) {
        build.message = "Knife stroke would create non-renderable face geometry.";
        return build;
    }

    build.changed = true;
    return build;
}

std::vector<ElementId> loop_between_indices(const std::vector<ElementId>& loop, std::size_t start_index, std::size_t end_index);
std::vector<ElementId> unique_valid_face_loop(std::vector<ElementId> vertices);

bool face_indices_are_adjacent(const Face& face, std::size_t first, std::size_t second)
{
    if (face.vertices.size() < 2) {
        return false;
    }
    const std::size_t count = face.vertices.size();
    return ((first + 1U) % count) == second || ((second + 1U) % count) == first;
}

std::vector<std::size_t> splittable_knife_face_indices(const Document& document, ElementId previous_vertex, ElementId current_vertex)
{
    std::vector<std::size_t> indices;
    for (std::size_t face_index = 0; face_index < document.faces.size(); ++face_index) {
        const Face& face = document.faces[face_index];
        const std::optional<std::size_t> previous_index = face_vertex_index(face, previous_vertex);
        const std::optional<std::size_t> current_index = face_vertex_index(face, current_vertex);
        if (!previous_index.has_value() || !current_index.has_value() || *previous_index == *current_index) {
            continue;
        }
        if (face_indices_are_adjacent(face, *previous_index, *current_index)) {
            continue;
        }
        indices.push_back(face_index);
    }
    return indices;
}

KnifeSegmentCandidate build_knife_segment_candidate(
    const Document& document,
    const KnifePointTarget& previous,
    const KnifePointTarget& current)
{
    KnifeSegmentCandidate build;
    build.document = document;
    if (knife_targets_use_same_edge(previous, current)) {
        build.message = "Knife segment needs targets on different boundary points.";
        return build;
    }

    std::map<std::pair<ElementId, ElementId>, ElementId> split_vertices;
    std::string message;
    const std::optional<ResolvedKnifeTarget> previous_resolved = resolve_knife_target(build.document, split_vertices, previous, message);
    if (!previous_resolved.has_value()) {
        build.message = std::move(message);
        return build;
    }
    const std::optional<ResolvedKnifeTarget> current_resolved = resolve_knife_target(build.document, split_vertices, current, message);
    if (!current_resolved.has_value()) {
        build.message = std::move(message);
        return build;
    }

    build.previous_vertex = previous_resolved->vertex_id;
    build.current_vertex = current_resolved->vertex_id;
    build.previous_position = previous_resolved->position;
    build.current_position = current_resolved->position;
    if (build.previous_vertex == build.current_vertex ||
        length_squared(build.current_position - build.previous_position) <=
            kEpsilon) {
      build.message = "Knife segment needs two distinct target points.";
      return build;
    }

    if (!split_vertices.empty() && !apply_knife_edge_splits_to_faces(build.document, split_vertices)) {
        build.message = "Knife segment could not resolve the target edge boundaries.";
        return build;
    }

    const std::vector<std::size_t> split_face_indices = splittable_knife_face_indices(build.document, build.previous_vertex, build.current_vertex);
    if (split_face_indices.empty()) {
        build.message = "Knife segment needs two targets on the same face boundary.";
        return build;
    }
    if (split_face_indices.size() > 1U) {
        build.message = "Knife segment is ambiguous across multiple faces.";
        return build;
    }

    const std::size_t face_index = split_face_indices.front();
    Face source_face = build.document.faces[face_index];
    const std::optional<std::size_t> previous_index = face_vertex_index(source_face, build.previous_vertex);
    const std::optional<std::size_t> current_index = face_vertex_index(source_face, build.current_vertex);
    if (!previous_index.has_value() || !current_index.has_value()) {
        build.message = "Knife segment target face was not valid.";
        return build;
    }

    std::vector<ElementId> first_loop = unique_valid_face_loop(loop_between_indices(source_face.vertices, *previous_index, *current_index));
    std::vector<ElementId> second_loop = unique_valid_face_loop(loop_between_indices(source_face.vertices, *current_index, *previous_index));
    if (first_loop.size() < 3U || second_loop.size() < 3U) {
        build.message = "Knife segment would create invalid face geometry.";
        return build;
    }

    Face first_face = source_face;
    first_face.vertices = std::move(first_loop);
    first_face.uvs.clear();

    Face second_face;
    second_face.id = build.document.next_face_id++;
    second_face.vertices = std::move(second_loop);
    second_face.material_slot = source_face.material_slot;
    second_face.uvs.clear();

    build.document.faces[face_index] = std::move(first_face);
    build.document.faces.push_back(std::move(second_face));
    prune_invalid_faces(build.document);
    prune_unused_vertices(build.document);
    restore_source_face_orientation(document, build.document);
    if (!every_face_triangulates(build.document)) {
        build.message = "Knife segment would create invalid face geometry.";
        return build;
    }

    build.changed = true;
    return build;
}

} // namespace quader_poly::document_internal
