////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <mesh/polygon/document.hpp>
#include <mesh/polygon/poly_operation.hpp>
#include <mesh/polygon/poly_operation_descriptor.hpp>

#include <mesh/polygon/internal/quader_poly_document_hull_plane_helpers.hpp>
#include <mesh/polygon/internal/quader_poly_document_topology_helpers.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <utility>
#include <vector>

#include <string_view>

namespace quader_poly {

using namespace document_internal;

namespace {
/**
 * Implements the Radial Align Operation modeling operation for the polygon document and mesh editing core.
 */
class RadialAlignOperation final : public PolyOperation {
public:
    [[nodiscard]] std::string_view id() const override { return poly_operation_descriptor(PolyOperationKey::RadialAlignSelection).id; }
    [[nodiscard]] std::string_view label() const override { return poly_operation_descriptor(PolyOperationKey::RadialAlignSelection).label; }
    [[nodiscard]] OperationResult apply(Document& document, Selection& selection) const override;
};


using EdgeCountMap = std::map<std::pair<ElementId, ElementId>, int>;

/**
 * Represents a Radial Align Driver Vertices value used by the polygon document and mesh editing core.
 */
struct RadialAlignDriverVertices {
    std::vector<ElementId> ids;
    bool ordered_closed_loop = false;
};

void add_face_edges_to_counts(const Face& face, EdgeCountMap& edge_counts)
{
    for (const Edge& edge : face_edges(face)) {
        ++edge_counts[{ edge.a, edge.b }];
    }
}

std::vector<ElementId> boundary_vertex_ids_from_edge_counts(const EdgeCountMap& edge_counts)
{
    std::vector<ElementId> ids;
    for (const auto& [edge, count] : edge_counts) {
        if (count != 1) {
            continue;
        }
        add_unique_id(ids, edge.first);
        add_unique_id(ids, edge.second);
    }
    return ids;
}

std::vector<Edge> boundary_edges_from_edge_counts(const EdgeCountMap& edge_counts)
{
    std::vector<Edge> edges;
    for (const auto& [edge, count] : edge_counts) {
        if (count == 1) {
            edges.push_back(make_edge(edge.first, edge.second));
        }
    }
    return edges;
}

std::optional<std::vector<ElementId>> ordered_closed_loop_from_edges(const std::vector<Edge>& edges)
{
    if (edges.size() < 3U) {
        return std::nullopt;
    }

    std::map<ElementId, std::vector<ElementId>> adjacency;
    for (Edge edge : edges) {
        edge = make_edge(edge.a, edge.b);
        if (edge.a == kInvalidElementId || edge.b == kInvalidElementId ||
            edge.a == edge.b) {
          return std::nullopt;
        }
        adjacency[edge.a].push_back(edge.b);
        adjacency[edge.b].push_back(edge.a);
    }

    for (const auto& [vertex_id, neighbors] : adjacency) {
        (void)vertex_id;
        if (neighbors.size() != 2U) {
            return std::nullopt;
        }
    }
    if (adjacency.size() != edges.size()) {
        return std::nullopt;
    }

    const ElementId start = adjacency.begin()->first;
    ElementId previous = kInvalidElementId;
    ElementId current = start;
    std::vector<ElementId> loop;
    loop.reserve(adjacency.size());
    for (std::size_t step = 0; step < adjacency.size(); ++step) {
        if (std::ranges::find(loop, current) != loop.end()) {
            return std::nullopt;
        }
        loop.push_back(current);
        const std::vector<ElementId>& neighbors = adjacency[current];
        const ElementId next = neighbors[0] == previous ? neighbors[1] : neighbors[0];
        previous = current;
        current = next;
    }

    if (current != start || loop.size() != adjacency.size()) {
        return std::nullopt;
    }
    return loop;
}

RadialAlignDriverVertices radial_align_driver_vertex_ids(
    const Document& document,
    const Selection& selection,
    const std::vector<ElementId>& selected_vertex_ids)
{
    if (selected_vertex_ids.size() < 4U) {
        return { selected_vertex_ids, false };
    }

    if (selection.mode == SelectionMode::Edge && !selection.edges.empty()) {
      std::vector<Edge> selected_edges;
      selected_edges.reserve(selection.edges.size());
      for (Edge edge : selection.edges) {
        selected_edges.push_back(make_edge(edge.a, edge.b));
      }
      if (std::optional<std::vector<ElementId>> loop =
              ordered_closed_loop_from_edges(selected_edges)) {
        return {std::move(*loop), true};
      }
    }

    EdgeCountMap edge_counts;
    std::set<ElementId> region_vertices;
    if (selection.mode == SelectionMode::Face && !selection.faces.empty()) {
      for (const ElementId face_id : selection.faces) {
        const Face *face = find_face(document, face_id);
        if (face == nullptr) {
          continue;
        }
        region_vertices.insert(face->vertices.begin(), face->vertices.end());
        add_face_edges_to_counts(*face, edge_counts);
      }
    } else {
      const std::set<ElementId> selected_set(selected_vertex_ids.begin(),
                                             selected_vertex_ids.end());
      for (const Face &face : document.faces) {
        if (face.vertices.size() < 3U) {
          continue;
        }
        const bool face_is_inside_selection = std::ranges::all_of(
            face.vertices, [&selected_set](ElementId vertex_id) {
              return selected_set.contains(vertex_id);
            });
        if (!face_is_inside_selection) {
          continue;
        }
        region_vertices.insert(face.vertices.begin(), face.vertices.end());
        add_face_edges_to_counts(face, edge_counts);
      }
    }

    if (edge_counts.empty()) {
        return { selected_vertex_ids, false };
    }
    const bool selected_vertices_are_region_vertices = std::ranges::all_of(selected_vertex_ids, [&region_vertices](ElementId vertex_id) {
        return region_vertices.contains(vertex_id);
    });
    if (!selected_vertices_are_region_vertices) {
        return { selected_vertex_ids, false };
    }

    const std::vector<Edge> boundary_edges = boundary_edges_from_edge_counts(edge_counts);
    if (std::optional<std::vector<ElementId>> loop = ordered_closed_loop_from_edges(boundary_edges)) {
        return { std::move(*loop), true };
    }

    std::vector<ElementId> boundary_vertex_ids = boundary_vertex_ids_from_edge_counts(edge_counts);
    return { boundary_vertex_ids.size() >= 3U ? boundary_vertex_ids : selected_vertex_ids, false };
}

float normalized_angle_delta(float angle)
{
  const float tau_value = static_cast<float>(kTau);
  const float pi_value = tau_value * 0.5F;
  while (angle <= -pi_value) {
    angle += tau_value;
  }
    while (angle > pi_value) {
        angle -= tau_value;
    }
    return angle;
}

OperationResult radial_align_selection_impl(Document& document, Selection& selection)
{
    if (selection.empty()) {
        return { false, "Radial Align needs selected vertices, edges, or faces." };
    }

    Document candidate = document;
    const std::vector<ElementId> selected_ids = selected_vertex_ids(document, selection);
    if (selected_ids.size() < 3U) {
        return { false, "Radial Align needs at least three selected vertices." };
    }
    const RadialAlignDriverVertices driver_vertices = radial_align_driver_vertex_ids(document, selection, selected_ids);
    const std::vector<ElementId>& vertex_ids = driver_vertices.ids;

    quader::QVec3 center;
    std::vector<Vertex*> vertices;
    vertices.reserve(vertex_ids.size());
    for (const ElementId vertex_id : vertex_ids) {
        Vertex* vertex = find_vertex(candidate, vertex_id);
        if (vertex == nullptr) {
            continue;
        }
        vertices.push_back(vertex);
        center += vertex->position;
    }
    if (vertices.size() < 3U) {
        return { false, "Radial Align needs at least three valid selected vertices." };
    }
    center = center / static_cast<float>(vertices.size());

    quader::QVec3 normal;
    if (selection.mode == SelectionMode::Face) {
      for (const ElementId face_id : selection.faces) {
        const Face *face = find_face(candidate, face_id);
        if (face != nullptr) {
          normal += face_normal(candidate, *face);
        }
      }
      normal = normalize_or_zero(normal);
    }

    quader::QVec3 u_axis;
    quader::QVec3 v_axis;
    if (length_squared(normal) > kEpsilon) {
      u_axis = cross(normal, {0.0F, 1.0F, 0.0F});
      if (length_squared(u_axis) <= kEpsilon) {
        u_axis = cross(normal, {1.0F, 0.0F, 0.0F});
      }
      u_axis = normalize_or_zero(u_axis);
      v_axis = normalize_or_zero(cross(normal, u_axis));
    } else {
      quader::QVec3 min = vertices.front()->position;
      quader::QVec3 max = vertices.front()->position;
      for (const Vertex *vertex : vertices) {
        min.x = std::min(min.x, vertex->position.x);
        min.y = std::min(min.y, vertex->position.y);
        min.z = std::min(min.z, vertex->position.z);
        max.x = std::max(max.x, vertex->position.x);
        max.y = std::max(max.y, vertex->position.y);
        max.z = std::max(max.z, vertex->position.z);
      }
      const quader::QVec3 extents = max - min;
      if (extents.x <= extents.y && extents.x <= extents.z) {
        normal = {1.0F, 0.0F, 0.0F};
        u_axis = {0.0F, 1.0F, 0.0F};
        v_axis = {0.0F, 0.0F, 1.0F};
      } else if (extents.y <= extents.x && extents.y <= extents.z) {
        normal = {0.0F, 1.0F, 0.0F};
        u_axis = {1.0F, 0.0F, 0.0F};
        v_axis = {0.0F, 0.0F, 1.0F};
      } else {
        normal = {0.0F, 0.0F, 1.0F};
        u_axis = {1.0F, 0.0F, 0.0F};
        v_axis = {0.0F, 1.0F, 0.0F};
      }
    }

    /**
     * Represents a Radial Vertex value used by the polygon document and mesh editing core.
     */
    struct RadialVertex {
        Vertex* vertex = nullptr;
        float angle = 0.0F;
        float radius = 0.0F;
        float normal_offset = 0.0F;
    };

    std::vector<RadialVertex> radial_vertices;
    radial_vertices.reserve(vertices.size());
    float radius_sum = 0.0F;
    for (Vertex* vertex : vertices) {
        const quader::QVec3 delta = vertex->position - center;
        const float u = dot(delta, u_axis);
        const float v = dot(delta, v_axis);
        const float radius = std::sqrt((u * u) + (v * v));
        radius_sum += radius;
        radial_vertices.push_back({
            vertex,
            std::atan2(v, u),
            radius,
            dot(delta, normal),
        });
    }

    const float radius = radius_sum / static_cast<float>(radial_vertices.size());
    if (radius <= kEpsilon) {
      return {false, "Radial Align needs vertices with a visible radius."};
    }

    const float angle_step =
        static_cast<float>(kTau / static_cast<double>(radial_vertices.size()));
    float angle_direction = 1.0F;
    float start_angle = radial_vertices.front().angle;
    if (driver_vertices.ordered_closed_loop) {
        for (std::size_t index = 1; index < radial_vertices.size(); ++index) {
            const float winding_delta = normalized_angle_delta(radial_vertices[index].angle - radial_vertices[index - 1U].angle);
            if (std::abs(winding_delta) > kEpsilon) {
              angle_direction = winding_delta < 0.0F ? -1.0F : 1.0F;
              break;
            }
        }
        double phase_x = 0.0;
        double phase_y = 0.0;
        for (std::size_t index = 0; index < radial_vertices.size(); ++index) {
            const float phase = radial_vertices[index].angle - (angle_direction * angle_step * static_cast<float>(index));
            phase_x += std::cos(phase);
            phase_y += std::sin(phase);
        }
        if (std::abs(phase_x) > kEpsilon || std::abs(phase_y) > kEpsilon) {
          start_angle = static_cast<float>(std::atan2(phase_y, phase_x));
        }
    } else {
        std::ranges::sort(radial_vertices, [](const RadialVertex& left, const RadialVertex& right) {
            return left.angle < right.angle;
        });
        start_angle = radial_vertices.front().angle;
    }

    bool changed = false;
    for (std::size_t index = 0; index < radial_vertices.size(); ++index) {
        Vertex* vertex = radial_vertices[index].vertex;
        const float angle = start_angle + (angle_direction * angle_step * static_cast<float>(index));
        const quader::QVec3 desired =
            center +
            (u_axis * (std::cos(angle) * radius)) +
            (v_axis * (std::sin(angle) * radius)) +
            (normal * radial_vertices[index].normal_offset);
        if (length_squared(vertex->position - desired) > kEpsilon) {
          vertex->position = desired;
          changed = true;
        }
    }

    if (!changed) {
        return { false, "Selected vertices are already radially aligned." };
    }
    clear_face_uvs(candidate);
    if (!every_face_triangulates(candidate)) {
        return { false, "Radial Align would create invalid face geometry." };
    }
    document = std::move(candidate);
    return { true, {} };
}



OperationResult RadialAlignOperation::apply(Document& document, Selection& selection) const
{
    return radial_align_selection_impl(document, selection);
}

} // namespace

OperationResult radial_align_selection(Document& document, Selection& selection)
{
    return RadialAlignOperation {}.apply(document, selection);
}

} // namespace quader_poly
