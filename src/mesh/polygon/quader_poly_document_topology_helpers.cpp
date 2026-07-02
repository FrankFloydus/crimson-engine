////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <mesh/polygon/internal/quader_poly_document_topology_helpers.hpp>

#include <mesh/polygon/internal/quader_poly_document_topology_backend.hpp>

#include <mesh/geometry/geometry.hpp>

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

        std::ranges::reverse(face.vertices);
        if (face.uvs.size() == face.vertices.size()) {
            std::ranges::reverse(face.uvs);
        } else {
            face.uvs.clear();
        }
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

} // namespace quader_poly::document_internal
