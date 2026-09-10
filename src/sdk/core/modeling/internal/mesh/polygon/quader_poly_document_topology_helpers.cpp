////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_topology_helpers.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_bridge_surface_helpers.hpp>

#include <quader/modeling/neutral/polygon/internal/quader_poly_document_knife_helpers.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_topology_backend.hpp>

#include <quader/modeling/neutral/geometry/geometry.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <exception>
#include <iterator>
#include <limits>
#include <map>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace quader_poly::document_internal {

namespace {

std::map<std::pair<ElementId, ElementId>, int> edge_incidence_counts_from_faces(const Document& document)
{
    std::map<std::pair<ElementId, ElementId>, int> counts;
    for (const Face& face : document.faces) {
        if (face.vertices.size() < 2) {
            continue;
        }
        for (std::size_t index = 0; index < face.vertices.size(); ++index) {
            Edge edge = make_edge(face.vertices[index], face.vertices[(index + 1U) % face.vertices.size()]);
            if (edge.a == kInvalidElementId || edge.b == kInvalidElementId ||
                edge.a == edge.b) {
              continue;
            }
            ++counts[{ edge.a, edge.b }];
        }
    }
    return counts;
}

std::map<std::pair<ElementId, ElementId>, std::vector<std::size_t>> face_indices_by_edge_from_faces(const Document& document)
{
    std::map<std::pair<ElementId, ElementId>, std::vector<std::size_t>> indices_by_edge;
    for (std::size_t face_index = 0; face_index < document.faces.size(); ++face_index) {
        const Face& face = document.faces[face_index];
        if (face.vertices.size() < 2) {
            continue;
        }
        for (std::size_t vertex_index = 0; vertex_index < face.vertices.size(); ++vertex_index) {
            const Edge edge = make_edge(face.vertices[vertex_index], face.vertices[(vertex_index + 1U) % face.vertices.size()]);
            if (edge.a == kInvalidElementId || edge.b == kInvalidElementId ||
                edge.a == edge.b) {
              continue;
            }
            indices_by_edge[{ edge.a, edge.b }].push_back(face_index);
        }
    }
    return indices_by_edge;
}

} // namespace

quader_geometry::QVec2d geometry_vec2(const std::array<double, 2>& value)
{
    return { value[0], value[1] };
}

std::array<double, 2> poly_vec2(quader_geometry::QVec2d value)
{
    return { value.x, value.y };
}

std::vector<quader_geometry::QVec2d> geometry_vec2_points(std::span<const std::array<double, 2>> points)
{
    std::vector<quader_geometry::QVec2d> geometry_points;
    geometry_points.reserve(points.size());
    for (const std::array<double, 2>& point : points) {
        geometry_points.push_back(geometry_vec2(point));
    }
    return geometry_points;
}

quader_geometry::QAxis geometry_axis_from_dropped_axis(int dropped_axis)
{
    switch (dropped_axis) {
    case 0:
      return quader_geometry::QAxis::X;
    case 1:
      return quader_geometry::QAxis::Y;
    default:
      return quader_geometry::QAxis::Z;
    }
}

quader_geometry::QVec3f geometry_vec3(const quader::QVec3& value)
{
    return { value.x, value.y, value.z };
}

quader::QVec3 poly_vec3(quader_geometry::QVec3f value)
{
    return { value.x, value.y, value.z };
}

quader_geometry::QRay3<float> geometry_ray(const Ray& ray)
{
    return { geometry_vec3(ray.origin), geometry_vec3(ray.direction) };
}


bool contains_id(std::span<const ElementId> ids, ElementId id)
{
    return std::ranges::find(ids, id) != ids.end();
}

bool contains_edge(std::span<const Edge> edges, Edge edge)
{
    return std::ranges::find(edges, make_edge(edge.a, edge.b)) != edges.end();
}

SelectionMode selection_mode_for_kind(ElementKind kind)
{
    switch (kind) {
    case ElementKind::Vertex:
      return SelectionMode::Vertex;
    case ElementKind::Edge:
      return SelectionMode::Edge;
    case ElementKind::Face:
      return SelectionMode::Face;
    }

    return SelectionMode::Vertex;
}

bool remove_id(std::vector<ElementId>& ids, ElementId id)
{
    const auto old_size = ids.size();
    std::erase(ids, id);
    return ids.size() != old_size;
}

void toggle_id(std::vector<ElementId>& ids, ElementId id)
{
  if (id == kInvalidElementId) {
    return;
  }

    if (!remove_id(ids, id)) {
        ids.push_back(id);
    }
}

void toggle_edge(std::vector<Edge>& edges, Edge edge)
{
    edge = make_edge(edge.a, edge.b);
    if (edge.a == kInvalidElementId || edge.b == kInvalidElementId ||
        edge.a == edge.b) {
      return;
    }

    const auto existing = std::ranges::find(edges, edge);
    if (existing == edges.end()) {
        edges.push_back(edge);
    } else {
        edges.erase(existing);
    }
}

bool remove_edge(std::vector<Edge>& edges, Edge edge)
{
    edge = make_edge(edge.a, edge.b);
    const auto existing = std::ranges::find(edges, edge);
    if (existing == edges.end()) {
        return false;
    }
    edges.erase(existing);
    return true;
}

void add_unique_id(std::vector<ElementId>& ids, ElementId id)
{
  if (id == kInvalidElementId || contains_id(ids, id)) {
    return;
  }
    ids.push_back(id);
}

void add_unique_edge(std::vector<Edge>& edges, Edge edge)
{
    edge = make_edge(edge.a, edge.b);
    if (edge.a == kInvalidElementId || edge.b == kInvalidElementId ||
        edge.a == edge.b || contains_edge(edges, edge)) {
      return;
    }
    edges.push_back(edge);
}

void clear_active_selection(Selection& selection)
{
    selection.has_active = false;
    selection.active_kind = ElementKind::Vertex;
    selection.active_vertex = kInvalidElementId;
    selection.active_edge = {};
    selection.active_face = kInvalidElementId;
}

void activate_vertex_selection(Selection& selection, ElementId vertex_id)
{
  if (vertex_id == kInvalidElementId) {
    clear_active_selection(selection);
    return;
  }
    selection.has_active = true;
    selection.active_kind = ElementKind::Vertex;
    selection.active_vertex = vertex_id;
    selection.active_edge = {};
    selection.active_face = kInvalidElementId;
}

void activate_edge_selection(Selection& selection, Edge edge)
{
    edge = make_edge(edge.a, edge.b);
    if (edge.a == kInvalidElementId || edge.b == kInvalidElementId ||
        edge.a == edge.b) {
      clear_active_selection(selection);
      return;
    }
    selection.has_active = true;
    selection.active_kind = ElementKind::Edge;
    selection.active_vertex = kInvalidElementId;
    selection.active_edge = edge;
    selection.active_face = kInvalidElementId;
}

void activate_face_selection(Selection& selection, ElementId face_id)
{
  if (face_id == kInvalidElementId) {
    clear_active_selection(selection);
    return;
  }
    selection.has_active = true;
    selection.active_kind = ElementKind::Face;
    selection.active_vertex = kInvalidElementId;
    selection.active_edge = {};
    selection.active_face = face_id;
}

void activate_pick_selection(Selection& selection, const PickResult& pick)
{
    switch (pick.kind) {
    case ElementKind::Vertex:
      activate_vertex_selection(selection, pick.vertex_id);
      break;
    case ElementKind::Edge:
      activate_edge_selection(selection, pick.edge);
      break;
    case ElementKind::Face:
      activate_face_selection(selection, pick.face_id);
      break;
    }
}

void activate_last_selection(Selection& selection)
{
    clear_active_selection(selection);
    switch (selection.mode) {
    case SelectionMode::Vertex:
      if (!selection.vertices.empty()) {
        activate_vertex_selection(selection, selection.vertices.back());
      }
      break;
    case SelectionMode::Edge:
      if (!selection.edges.empty()) {
        activate_edge_selection(selection, selection.edges.back());
      }
      break;
    case SelectionMode::Face:
      if (!selection.faces.empty()) {
        activate_face_selection(selection, selection.faces.back());
      }
      break;
    }
}

ElementId active_face_or_invalid(const Selection& selection)
{
  if (selection.has_active && selection.active_kind == ElementKind::Face &&
      contains_id(selection.faces, selection.active_face)) {
    return selection.active_face;
  }
  return kInvalidElementId;
}

void activate_face_or_last_selection(Selection& selection, ElementId preferred_face_id)
{
  if (preferred_face_id != kInvalidElementId &&
      contains_id(selection.faces, preferred_face_id)) {
    activate_face_selection(selection, preferred_face_id);
    return;
  }
    activate_last_selection(selection);
}

bool face_uses_vertex(const Face& face, ElementId vertex_id)
{
    return contains_id(face.vertices, vertex_id);
}

bool face_uses_any_vertex(const Face& face, std::span<const ElementId> vertex_ids)
{
    return std::ranges::any_of(vertex_ids, [&face](ElementId vertex_id) {
        return face_uses_vertex(face, vertex_id);
    });
}

bool face_uses_any_vertex(const Face& face, const std::set<ElementId>& vertex_ids)
{
    return std::ranges::any_of(face.vertices, [&vertex_ids](ElementId vertex_id) {
        return vertex_ids.contains(vertex_id);
    });
}

bool face_uses_edge(const Face& face, Edge edge)
{
    if (face.vertices.size() < 2) {
        return false;
    }

    const Edge normalized = make_edge(edge.a, edge.b);
    for (std::size_t index = 0; index < face.vertices.size(); ++index) {
        const Edge candidate = make_edge(face.vertices[index], face.vertices[(index + 1U) % face.vertices.size()]);
        if (candidate == normalized) {
            return true;
        }
    }
    return false;
}

std::vector<Edge> face_edges(const Face& face)
{
    std::vector<Edge> edges;
    if (face.vertices.size() < 2) {
        return edges;
    }

    edges.reserve(face.vertices.size());
    for (std::size_t index = 0; index < face.vertices.size(); ++index) {
        add_unique_edge(edges, make_edge(face.vertices[index], face.vertices[(index + 1U) % face.vertices.size()]));
    }
    return edges;
}

bool face_uses_any_edge(const Face& face, std::span<const Edge> edges)
{
    return std::ranges::any_of(edges, [&face](Edge edge) {
        return face_uses_edge(face, edge);
    });
}

std::array<double, 2> projected_point_for_axis(const quader::QVec3& position, int dropped_axis)
{
    const quader_geometry::QVec3d geometry_position {
        static_cast<double>(position.x),
        static_cast<double>(position.y),
        static_cast<double>(position.z),
    };
    return poly_vec2(
        quader_geometry::project_dominant_axis<double>(geometry_position, geometry_axis_from_dropped_axis(dropped_axis)));
}

double signed_projected_area(std::span<const std::array<double, 2>> points)
{
    const std::vector<quader_geometry::QVec2d> geometry_points = geometry_vec2_points(points);
    return quader_geometry::polygon_signed_area<double>(std::span<const quader_geometry::QVec2d>(geometry_points));
}

bool has_repeated_vertex(std::span<const ElementId> vertex_ids)
{
    std::set<ElementId> unique_vertices;
    for (const ElementId vertex_id : vertex_ids) {
        if (!unique_vertices.insert(vertex_id).second) {
            return true;
        }
    }
    return false;
}

std::map<std::pair<ElementId, ElementId>, int> edge_incidence_counts(const Document& document)
{
    DocumentTopologyQuery query;
    std::string message;
    if (build_topology_query(document, query, message)) {
        return std::move(query.edge_incidence_counts);
    }
    return edge_incidence_counts_from_faces(document);
}

std::map<std::pair<ElementId, ElementId>, std::vector<std::size_t>> face_indices_by_edge(const Document& document)
{
    DocumentTopologyQuery query;
    std::string message;
    if (build_topology_query(document, query, message)) {
        return std::move(query.face_indices_by_edge);
    }
    return face_indices_by_edge_from_faces(document);
}

bool incidence_counts_are_closed_manifold(const std::map<std::pair<ElementId, ElementId>, int>& counts)
{
    return !counts.empty() && std::ranges::all_of(counts, [](const auto& entry) {
        return entry.second == 2;
    });
}

bool incidence_counts_have_nonmanifold_edges(const std::map<std::pair<ElementId, ElementId>, int>& counts)
{
    return std::ranges::any_of(counts, [](const auto& entry) {
        return entry.second > 2;
    });
}

bool document_is_closed_manifold(const Document& document)
{
    return incidence_counts_are_closed_manifold(edge_incidence_counts(document));
}

bool document_has_nonmanifold_edges(const Document& document)
{
    return incidence_counts_have_nonmanifold_edges(edge_incidence_counts(document));
}

FacePerimeterInfo perimeter_info_for_edges(const Document& document, std::span<const Edge> perimeter_edges)
{
    const std::map<std::pair<ElementId, ElementId>, int> counts = edge_incidence_counts(document);
    FacePerimeterInfo info;
    info.edges.reserve(perimeter_edges.size());
    info.open_edges.reserve(perimeter_edges.size());
    info.closed_edges.reserve(perimeter_edges.size());
    info.nonmanifold_edges.reserve(perimeter_edges.size());

    for (Edge edge : perimeter_edges) {
        edge = make_edge(edge.a, edge.b);
        if (edge.a == kInvalidElementId || edge.b == kInvalidElementId ||
            edge.a == edge.b || find_vertex(document, edge.a) == nullptr ||
            find_vertex(document, edge.b) == nullptr) {
          continue;
        }

        const auto count = counts.find({ edge.a, edge.b });
        if (count == counts.end()) {
            continue;
        }

        add_unique_edge(info.edges, edge);
    }

    for (const Edge& edge : info.edges) {
        const auto count = counts.find({ edge.a, edge.b });
        if (count == counts.end()) {
            continue;
        }

        if (count->second == 1) {
            add_unique_edge(info.open_edges, edge);
        } else if (count->second == 2) {
            add_unique_edge(info.closed_edges, edge);
        } else if (count->second > 2) {
            add_unique_edge(info.nonmanifold_edges, edge);
        }
    }

    return info;
}

bool document_has_unreferenced_vertices(const Document& document)
{
    std::set<ElementId> referenced_vertices;
    for (const Face& face : document.faces) {
        referenced_vertices.insert(face.vertices.begin(), face.vertices.end());
    }

    return std::ranges::any_of(
        document.vertices, [&referenced_vertices](const Vertex &vertex) {
          return vertex.id == kInvalidElementId ||
                 !referenced_vertices.contains(vertex.id);
        });
}

std::vector<ElementId> compact_face_vertices_for_merge(const Face& face, const std::set<ElementId>& merge_vertex_ids, ElementId active_vertex_id)
{
    std::vector<ElementId> merged_vertices;
    merged_vertices.reserve(face.vertices.size());
    for (const ElementId vertex_id : face.vertices) {
        const ElementId next_id = merge_vertex_ids.contains(vertex_id) ? active_vertex_id : vertex_id;
        if (!merged_vertices.empty() && merged_vertices.back() == next_id) {
            continue;
        }
        merged_vertices.push_back(next_id);
    }

    if (merged_vertices.size() > 1 && merged_vertices.front() == merged_vertices.back()) {
        merged_vertices.pop_back();
    }
    return merged_vertices;
}

void prune_unused_vertices(Document& document);
bool every_face_triangulates(const Document& document);
void orient_face_toward_normal(const Document& document, Face& face, quader::QVec3 expected_normal);

void restore_source_face_orientation(const Document& source, Document& candidate)
{
    for (Face& face : candidate.faces) {
        const Face* source_face = find_face(source, face.id);
        if (source_face == nullptr) {
            continue;
        }

        const quader::QVec3 source_normal = face_normal(source, *source_face);
        const quader::QVec3 candidate_normal = face_normal(candidate, face);
        if (length_squared(source_normal) <= kEpsilon ||
            length_squared(candidate_normal) <= kEpsilon) {
          continue;
        }
        if (dot(source_normal, candidate_normal) >= 0.0F) {
            continue;
        }

        reverse_face_winding(face);
    }
}

ElementId next_valid_face_id(Document& document)
{
    while (std::ranges::any_of(document.faces, [&document](const Face& face) {
        return face.id == document.next_face_id;
    })) {
        ++document.next_face_id;
    }
    return document.next_face_id++;
}

namespace {

bool normal_edge_less(Edge left, Edge right)
{
    left = make_edge(left.a, left.b);
    right = make_edge(right.a, right.b);
    return std::tie(left.a, left.b) < std::tie(right.a, right.b);
}

struct NormalEdgeLess {
    bool operator()(Edge left, Edge right) const
    {
        return normal_edge_less(left, right);
    }
};

void sort_unique_normal_edges(std::vector<Edge>& edges)
{
    for (Edge& edge : edges) {
        edge = make_edge(edge.a, edge.b);
    }
    std::ranges::sort(edges, normal_edge_less);
    const auto duplicate_begin = std::ranges::unique(edges);
    edges.erase(duplicate_begin.begin(), edges.end());
}

bool normal_edge_vector_is_canonical(
    std::span<const Edge> edges,
    std::span<const Edge> live_edges)
{
    for (std::size_t index = 0U; index < edges.size(); ++index) {
        const Edge edge = make_edge(edges[index].a, edges[index].b);
        if (edges[index] != edge || edge.a == kInvalidElementId ||
            edge.a == edge.b ||
            !std::ranges::binary_search(live_edges, edge, normal_edge_less) ||
            (index > 0U && !normal_edge_less(edges[index - 1U], edge))) {
          return false;
        }
    }
    return true;
}

bool normal_edge_states_are_canonical(
    const Document& document,
    std::span<const Edge> live_edges)
{
    if (!normal_edge_vector_is_canonical(
            document.hard_normal_edges, live_edges) ||
        !normal_edge_vector_is_canonical(
            document.soft_normal_edges, live_edges)) {
      return false;
    }

    std::size_t hard_index = 0U;
    std::size_t soft_index = 0U;
    while (hard_index < document.hard_normal_edges.size() &&
           soft_index < document.soft_normal_edges.size()) {
        const Edge hard = document.hard_normal_edges[hard_index];
        const Edge soft = document.soft_normal_edges[soft_index];
        if (hard == soft) {
            return false;
        }
        if (normal_edge_less(hard, soft)) {
            ++hard_index;
        } else {
            ++soft_index;
        }
    }
    return true;
}

bool normalized_edge_vector_contains(std::span<const Edge> edges, Edge target)
{
    target = make_edge(target.a, target.b);
    return std::ranges::any_of(edges, [target](Edge edge) {
        return make_edge(edge.a, edge.b) == target;
    });
}

std::map<std::pair<ElementId, ElementId>, EdgeNormalMode>
build_normal_edge_mode_lookup(
    const Document& document)
{
    std::map<std::pair<ElementId, ElementId>, unsigned int> flags;
    for (Edge edge : document.hard_normal_edges) {
        edge = make_edge(edge.a, edge.b);
        if (edge.a != kInvalidElementId && edge.a != edge.b) {
            flags[{edge.a, edge.b}] |= 1U;
        }
    }
    for (Edge edge : document.soft_normal_edges) {
        edge = make_edge(edge.a, edge.b);
        if (edge.a != kInvalidElementId && edge.a != edge.b) {
            flags[{edge.a, edge.b}] |= 2U;
        }
    }

    std::map<std::pair<ElementId, ElementId>, EdgeNormalMode> modes;
    for (const auto& [edge, state_flags] : flags) {
        if (state_flags == 1U) {
            modes.emplace(edge, EdgeNormalMode::Hard);
        } else if (state_flags == 2U) {
            modes.emplace(edge, EdgeNormalMode::Soft);
        }
    }
    return modes;
}

void erase_sorted_normal_edge(std::vector<Edge>& edges, Edge target)
{
    const auto found =
        std::ranges::lower_bound(edges, target, normal_edge_less);
    if (found != edges.end() && *found == target) {
        edges.erase(found);
    }
}

void insert_sorted_normal_edge(std::vector<Edge>& edges, Edge target)
{
    const auto position =
        std::ranges::lower_bound(edges, target, normal_edge_less);
    if (position == edges.end() || *position != target) {
        edges.insert(position, target);
    }
}

std::vector<ElementId> verified_split_path(
    Edge source_edge,
    const Document& candidate,
    std::span<const ElementId> split_vertex_ids)
{
    const auto find_path = [&](std::span<const ElementId> ordered_splits) {
        std::vector<ElementId> nodes;
        nodes.reserve(ordered_splits.size() + 2U);
        nodes.push_back(source_edge.a);
        for (const ElementId vertex_id : ordered_splits) {
            if (vertex_id == kInvalidElementId ||
                vertex_id == source_edge.a || vertex_id == source_edge.b ||
                find_vertex(candidate, vertex_id) == nullptr ||
                std::ranges::find(nodes, vertex_id) != nodes.end()) {
              continue;
            }
            nodes.push_back(vertex_id);
        }
        nodes.push_back(source_edge.b);

        // Looking backward avoids choosing a surviving but disconnected
        // intermediate that would hide later live descendants.
        std::vector<std::size_t> next(nodes.size(), nodes.size());
        std::vector<bool> reaches_end(nodes.size(), false);
        reaches_end.back() = true;
        for (std::size_t index = nodes.size() - 1U; index-- > 0U;) {
            for (std::size_t candidate_index = index + 1U;
                 candidate_index < nodes.size(); ++candidate_index) {
                if (!reaches_end[candidate_index] ||
                    !edge_exists(
                        candidate,
                        make_edge(nodes[index], nodes[candidate_index]))) {
                  continue;
                }
                reaches_end[index] = true;
                next[index] = candidate_index;
                break;
            }
        }

        std::vector<ElementId> path;
        if (!reaches_end.front()) {
            return path;
        }
        for (std::size_t index = 0U; index < nodes.size(); index = next[index]) {
            path.push_back(nodes[index]);
            if (index == nodes.size() - 1U) {
                break;
            }
        }
        return path;
    };

    std::vector<ElementId> forward = find_path(split_vertex_ids);
    std::vector<ElementId> reversed_splits(
        split_vertex_ids.begin(), split_vertex_ids.end());
    std::ranges::reverse(reversed_splits);
    std::vector<ElementId> reverse = find_path(reversed_splits);
    return reverse.size() > forward.size() ? reverse : forward;
}

} // namespace

EdgeNormalModeLookup::EdgeNormalModeLookup(const Document& document)
    : modes_(build_normal_edge_mode_lookup(document))
{
}

EdgeNormalMode EdgeNormalModeLookup::mode(Edge edge) const
{
    edge = make_edge(edge.a, edge.b);
    const auto found = modes_.find({edge.a, edge.b});
    return found == modes_.end() ? EdgeNormalMode::Default : found->second;
}

EdgeNormalMode edge_normal_mode(const Document& document, Edge edge)
{
    edge = make_edge(edge.a, edge.b);
    const bool hard =
        normalized_edge_vector_contains(document.hard_normal_edges, edge);
    const bool soft =
        normalized_edge_vector_contains(document.soft_normal_edges, edge);
    if (hard == soft) {
        return EdgeNormalMode::Default;
    }
    return hard ? EdgeNormalMode::Hard : EdgeNormalMode::Soft;
}

bool set_edge_normal_modes(
    Document& document,
    std::span<const EdgeNormalModeAssignment> assignments)
{
    std::vector<Edge> live_edges = document_edges(document);
    sort_unique_normal_edges(live_edges);

    std::map<Edge, unsigned int, NormalEdgeLess> authored_flags;
    const auto collect_flags = [&](std::span<const Edge> edges,
                                   unsigned int flag) {
        for (Edge edge : edges) {
            edge = make_edge(edge.a, edge.b);
            if (edge.a == kInvalidElementId || edge.a == edge.b ||
                !std::ranges::binary_search(
                    live_edges, edge, normal_edge_less)) {
              continue;
            }
            authored_flags[edge] |= flag;
        }
    };
    collect_flags(document.hard_normal_edges, 1U);
    collect_flags(document.soft_normal_edges, 2U);

    std::map<Edge, EdgeNormalMode, NormalEdgeLess> modes;
    for (const auto& [edge, flags] : authored_flags) {
        if (flags == 1U) {
            modes.emplace(edge, EdgeNormalMode::Hard);
        } else if (flags == 2U) {
            modes.emplace(edge, EdgeNormalMode::Soft);
        }
    }

    // Ordered assignments are intentional: topology donor policies can rely
    // on the last valid donor winning without repeated vector rebuilding.
    for (const EdgeNormalModeAssignment& assignment : assignments) {
        const Edge edge = make_edge(assignment.edge.a, assignment.edge.b);
        if (edge.a == kInvalidElementId || edge.a == edge.b ||
            !std::ranges::binary_search(live_edges, edge, normal_edge_less)) {
          continue;
        }
        if (assignment.mode == EdgeNormalMode::Default) {
            modes.erase(edge);
        } else {
            modes[edge] = assignment.mode;
        }
    }

    std::vector<Edge> hard;
    std::vector<Edge> soft;
    hard.reserve(modes.size());
    soft.reserve(modes.size());
    for (const auto& [edge, mode] : modes) {
        if (mode == EdgeNormalMode::Hard) {
            hard.push_back(edge);
        } else if (mode == EdgeNormalMode::Soft) {
            soft.push_back(edge);
        }
    }

    const bool changed = hard != document.hard_normal_edges ||
                         soft != document.soft_normal_edges;
    document.hard_normal_edges = std::move(hard);
    document.soft_normal_edges = std::move(soft);
    return changed;
}

bool set_edge_normal_mode(Document& document, Edge edge, EdgeNormalMode mode)
{
    edge = make_edge(edge.a, edge.b);
    if (edge.a == kInvalidElementId || edge.b == kInvalidElementId ||
        edge.a == edge.b) {
      return false;
    }

    std::vector<Edge> live_edges = document_edges(document);
    sort_unique_normal_edges(live_edges);
    if (!std::ranges::binary_search(live_edges, edge, normal_edge_less)) {
        return false;
    }
    if (!normal_edge_states_are_canonical(document, live_edges)) {
        const EdgeNormalModeAssignment assignment {edge, mode};
        return set_edge_normal_modes(
            document,
            std::span<const EdgeNormalModeAssignment>(&assignment, 1U));
    }

    const EdgeNormalMode previous = edge_normal_mode(document, edge);
    if (previous == mode) {
        return false;
    }
    erase_sorted_normal_edge(document.hard_normal_edges, edge);
    erase_sorted_normal_edge(document.soft_normal_edges, edge);
    if (mode == EdgeNormalMode::Hard) {
        insert_sorted_normal_edge(document.hard_normal_edges, edge);
    } else if (mode == EdgeNormalMode::Soft) {
        insert_sorted_normal_edge(document.soft_normal_edges, edge);
    }
    return true;
}

bool copy_edge_normal_modes(
    const Document& source,
    Document& target,
    std::span<const EdgeNormalModeCopy> copies)
{
    const EdgeNormalModeLookup source_modes(source);
    std::vector<EdgeNormalModeAssignment> assignments;
    assignments.reserve(copies.size());
    for (const EdgeNormalModeCopy& copy : copies) {
        const Edge source_edge =
            make_edge(copy.source_edge.a, copy.source_edge.b);
        assignments.push_back({
            copy.target_edge,
            source_modes.mode(source_edge),
        });
    }
    return set_edge_normal_modes(target, assignments);
}

bool copy_edge_normal_mode(const Document& source, Edge source_edge,
                           Document& target, Edge target_edge)
{
    return set_edge_normal_mode(
        target, target_edge, edge_normal_mode(source, source_edge));
}

void prune_normal_edge_states(Document& document)
{
    static_cast<void>(set_edge_normal_modes(
        document, std::span<const EdgeNormalModeAssignment>{}));
}

std::vector<Edge> remap_normal_edges_after_edge_splits(
    std::span<const Edge> source_edges,
    const Document& candidate,
    const std::map<std::pair<ElementId, ElementId>, std::vector<ElementId>>&
        split_vertex_ids_by_edge)
{
    std::vector<Edge> remapped_edges;
    remapped_edges.reserve(source_edges.size() + split_vertex_ids_by_edge.size());
    for (Edge edge : source_edges) {
        edge = make_edge(edge.a, edge.b);
        if (edge.a == kInvalidElementId || edge.b == kInvalidElementId ||
            edge.a == edge.b) {
          continue;
        }

        if (edge_exists(candidate, edge)) {
            add_unique_edge(remapped_edges, edge);
        }

        auto found = split_vertex_ids_by_edge.find({edge.a, edge.b});
        if (found == split_vertex_ids_by_edge.end()) {
            found = split_vertex_ids_by_edge.find({edge.b, edge.a});
        }
        if (found == split_vertex_ids_by_edge.end()) {
            continue;
        }

        const std::vector<ElementId> path =
            verified_split_path(edge, candidate, found->second);
        for (std::size_t index = 1U; index < path.size(); ++index) {
            add_unique_edge(
                remapped_edges, make_edge(path[index - 1U], path[index]));
        }
    }
    sort_unique_normal_edges(remapped_edges);
    return remapped_edges;
}

std::vector<Edge> remap_normal_edges_after_edge_splits(
    std::span<const Edge> source_edges,
    const Document& candidate,
    const std::map<std::pair<ElementId, ElementId>, ElementId>& split_vertices)
{
    std::map<std::pair<ElementId, ElementId>, std::vector<ElementId>>
        split_vertex_ids_by_edge;
    for (const auto& [edge, vertex_id] : split_vertices) {
        if (vertex_id != kInvalidElementId) {
            split_vertex_ids_by_edge[edge].push_back(vertex_id);
        }
    }
    return remap_normal_edges_after_edge_splits(
        source_edges, candidate, split_vertex_ids_by_edge);
}

void remap_normal_edge_states_after_edge_splits(
    const Document& source,
    Document& candidate,
    const std::map<std::pair<ElementId, ElementId>, std::vector<ElementId>>&
        split_vertex_ids_by_edge)
{
    candidate.hard_normal_edges = remap_normal_edges_after_edge_splits(
        source.hard_normal_edges, candidate, split_vertex_ids_by_edge);
    candidate.soft_normal_edges = remap_normal_edges_after_edge_splits(
        source.soft_normal_edges, candidate, split_vertex_ids_by_edge);
    prune_normal_edge_states(candidate);
}

void remap_normal_edge_states_after_edge_splits(
    const Document& source,
    Document& candidate,
    const std::map<std::pair<ElementId, ElementId>, ElementId>& split_vertices)
{
    candidate.hard_normal_edges = remap_normal_edges_after_edge_splits(
        source.hard_normal_edges, candidate, split_vertices);
    candidate.soft_normal_edges = remap_normal_edges_after_edge_splits(
        source.soft_normal_edges, candidate, split_vertices);
    prune_normal_edge_states(candidate);
}

static ElementId
remap_vertex_id(ElementId vertex_id,
                const std::map<ElementId, ElementId> &merged_vertex_ids) {
  const auto found = merged_vertex_ids.find(vertex_id);
  return found == merged_vertex_ids.end() ? vertex_id : found->second;
}

static Edge remap_edge(Edge edge,
                const std::map<ElementId, ElementId> &merged_vertex_ids) {
  return make_edge(remap_vertex_id(edge.a, merged_vertex_ids),
                   remap_vertex_id(edge.b, merged_vertex_ids));
}

void restore_remapped_selection(
    const Document &document, const Selection &source, Selection &selection,
    const std::map<ElementId, ElementId> &merged_vertex_ids) {
  Selection remapped;
  remapped.mode = source.mode;

  for (const ElementId vertex_id : source.vertices) {
    const ElementId mapped_id = remap_vertex_id(vertex_id, merged_vertex_ids);
    if (find_vertex(document, mapped_id) != nullptr) {
      add_unique_id(remapped.vertices, mapped_id);
    }
  }

  for (Edge edge : source.edges) {
    edge = remap_edge(edge, merged_vertex_ids);
    if (edge.a != edge.b && edge_exists(document, edge)) {
      add_unique_edge(remapped.edges, edge);
    }
  }

  for (const ElementId face_id : source.faces) {
    if (find_face(document, face_id) != nullptr) {
      add_unique_id(remapped.faces, face_id);
    }
  }

  if (source.has_active) {
    switch (source.active_kind) {
    case ElementKind::Vertex: {
      const ElementId mapped_id =
          remap_vertex_id(source.active_vertex, merged_vertex_ids);
      if (find_vertex(document, mapped_id) != nullptr) {
        add_unique_id(remapped.vertices, mapped_id);
        activate_vertex_selection(remapped, mapped_id);
      }
      break;
    }
    case ElementKind::Edge: {
      const Edge mapped_edge =
          remap_edge(source.active_edge, merged_vertex_ids);
      if (mapped_edge.a != mapped_edge.b &&
          edge_exists(document, mapped_edge)) {
        add_unique_edge(remapped.edges, mapped_edge);
        activate_edge_selection(remapped, mapped_edge);
      }
      break;
    }
    case ElementKind::Face:
      if (find_face(document, source.active_face) != nullptr) {
        add_unique_id(remapped.faces, source.active_face);
        activate_face_selection(remapped, source.active_face);
      }
      break;
    }
  }

  if (!remapped.has_active) {
    activate_last_selection(remapped);
  }
  selection = std::move(remapped);
}

} // namespace quader_poly::document_internal
