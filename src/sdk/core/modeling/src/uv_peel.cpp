////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <quader/modeling/mesh/uv_peel.hpp>

#include "polygon_document_native.hpp"
#include "uv_peel_solver.hpp"
#include "uv_peel_structured.hpp"
#include "uv_peel_circular.hpp"

#include <quader/modeling/neutral/geometry/geometry.hpp>
#include <quader/modeling/neutral/polygon/document_texture.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_constants.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_mesh_internal.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <queue>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace quader::modeling {
namespace {

using NativeId = quader_poly::ElementId;
using NativeEdge = std::pair<NativeId, NativeId>;
using NativeDocument = quader_poly::Document;
using NativeTransform = quader_poly::Transform3;
using VertexLookup = quader_poly::document_internal::VertexLookupIndex;
using detail::GuidedPeelChart;
using detail::PeelPoint2;
using detail::PeelPoint3;
using quader::QVec2;
using quader::QVec3;

constexpr std::size_t kNoIndex = std::numeric_limits<std::size_t>::max();
// Repeated native corner stores count too, bounding cyclic walk work rather
// than merely the number of unique faces eventually written.
constexpr std::size_t kUvPeelMaxCornerWrites = detail::kUvPeelMaxChartTriangles * 3U;

[[nodiscard]] NativeEdge edge_key(NativeId a, NativeId b) {
  return std::minmax(a, b);
}

[[nodiscard]] NativeTransform native_transform(const Transform3 &transform) {
  const auto vector = [](Vec3 value) { return QVec3{value.x, value.y, value.z}; };
  return {.x_axis = vector(transform.x_axis),
          .y_axis = vector(transform.y_axis),
          .z_axis = vector(transform.z_axis),
          .origin = vector(transform.origin)};
}

[[nodiscard]] bool finite(QVec3 value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

[[nodiscard]] bool finite(QVec2 value) {
  return std::isfinite(value.x) && std::isfinite(value.y);
}

[[nodiscard]] bool valid_transform(const NativeTransform &transform) {
  if (!finite(transform.x_axis) || !finite(transform.y_axis) ||
      !finite(transform.z_axis) || !finite(transform.origin)) {
    return false;
  }
  using quader_geometry::QVec3d;
  const QVec3d x{transform.x_axis};
  const QVec3d y{transform.y_axis};
  const QVec3d z{transform.z_axis};
  const double determinant = quader_geometry::dot(x, quader_geometry::cross(y, z));
  const double scale = std::sqrt(quader_geometry::dot(x, x) *
                                quader_geometry::dot(y, y) *
                                quader_geometry::dot(z, z));
  return std::isfinite(determinant) && std::isfinite(scale) && scale > 0.0 &&
         std::abs(determinant) > 1.0e-10 * scale;
}

[[nodiscard]] PeelPoint3 world_difference(const NativeTransform &transform,
                                          QVec3 point, QVec3 origin) {
  const double x = static_cast<double>(point.x) - origin.x;
  const double y = static_cast<double>(point.y) - origin.y;
  const double z = static_cast<double>(point.z) - origin.z;
  return {transform.x_axis.x * x + transform.y_axis.x * y + transform.z_axis.x * z,
          transform.x_axis.y * x + transform.y_axis.y * y + transform.z_axis.y * z,
          transform.x_axis.z * x + transform.y_axis.z * y + transform.z_axis.z * z};
}

[[nodiscard]] double world_distance(const NativeTransform &transform,
                                     QVec3 a, QVec3 b) {
  const auto delta = world_difference(transform, a, b);
  return std::hypot(delta.x, delta.y, delta.z);
}

[[nodiscard]] Error peel_error(std::string message) {
  return make_error(ErrorCode::ValidationFailed, std::move(message));
}

/// Identifies an authored edge occurrence without inventing mesh topology.
struct EdgeUse {
  std::size_t face = 0;
  std::size_t corner = 0;
};

using EdgeUses = std::map<NativeEdge, std::vector<EdgeUse>>;

/// Keeps each guide's original vertex order until transverse cuts are applied.
struct GuidePath {
  std::vector<NativeId> vertices;
  bool closed = false;
  bool reference = false;
};

/// Restricts chart construction to edge-connected faces incident to guides.
struct IncidentPatch {
  std::vector<std::size_t> faces;
  EdgeUses edges;
  std::set<NativeEdge> selected_edges;
  std::vector<GuidePath> guides;
};

/// Joins UV corner identities only across explicitly glued authored edges.
class CornerSets final {
public:
  explicit CornerSets(std::size_t count) : parents_(count) {
    std::iota(parents_.begin(), parents_.end(), std::size_t{0});
  }

  [[nodiscard]] std::size_t root(std::size_t index) {
    while (parents_[index] != index) {
      parents_[index] = parents_[parents_[index]];
      index = parents_[index];
    }
    return index;
  }

  void join(std::size_t a, std::size_t b) {
    a = root(a);
    b = root(b);
    if (a != b) {
      parents_[std::max(a, b)] = std::min(a, b);
    }
  }

private:
  std::vector<std::size_t> parents_;
};

/// Associates numerical chart nodes with unchanged authored face corners.
struct ChartWork {
  GuidedPeelChart chart;
  std::vector<std::vector<std::size_t>> face_nodes;
  std::vector<std::vector<std::array<std::size_t, 3>>> face_triangle_corners;
  std::map<std::size_t, std::size_t> face_slots;
  std::vector<NativeId> node_vertices;
  std::vector<std::vector<std::size_t>> boundaries;
  std::int64_t euler_characteristic = 0;
};

[[nodiscard]] EdgeUses authored_edges(const NativeDocument &document) {
  EdgeUses result;
  for (std::size_t face_index = 0; face_index < document.faces.size(); ++face_index) {
    const auto &face = document.faces[face_index];
    for (std::size_t corner = 0; corner < face.vertices.size(); ++corner) {
      result[edge_key(face.vertices[corner],
                      face.vertices[(corner + 1U) % face.vertices.size()])]
          .push_back({face_index, corner});
    }
  }
  return result;
}

[[nodiscard]] Result<std::vector<IncidentPatch>> incident_patches(
    const NativeDocument &document, const EdgeUses &all_edges,
    const std::set<NativeEdge> &selected_edges) {
  std::set<std::size_t> affected;
  for (const auto &edge : selected_edges) {
    const auto found = all_edges.find(edge);
    if (found == all_edges.end()) {
      return Result<std::vector<IncidentPatch>>::failure(
          make_error(ErrorCode::StaleId, "A UV Peel guide is not a live authored edge."));
    }
    for (const auto &use : found->second) {
      affected.insert(use.face);
    }
  }
  std::vector<std::size_t> ordered(affected.begin(), affected.end());
  std::ranges::sort(ordered, [&](std::size_t a, std::size_t b) {
    return document.faces[a].id < document.faces[b].id;
  });
  std::set<std::size_t> visited;
  std::vector<IncidentPatch> patches;
  for (const auto seed : ordered) {
    if (!visited.insert(seed).second) {
      continue;
    }
    IncidentPatch patch;
    patch.faces.push_back(seed);
    for (std::size_t next = 0; next < patch.faces.size(); ++next) {
      const auto face_index = patch.faces[next];
      const auto &face = document.faces[face_index];
      for (std::size_t corner = 0; corner < face.vertices.size(); ++corner) {
        const auto edge = edge_key(face.vertices[corner],
                                   face.vertices[(corner + 1U) % face.vertices.size()]);
        const auto &uses = all_edges.at(edge);
        if (uses.size() > 2U) {
          return Result<std::vector<IncidentPatch>>::failure(
              peel_error("UV Peel requires manifold authored edges."));
        }
        for (const auto &use : uses) {
          if (affected.contains(use.face) && visited.insert(use.face).second) {
            patch.faces.push_back(use.face);
          }
        }
      }
    }
    std::ranges::sort(patch.faces, [&](std::size_t a, std::size_t b) {
      return document.faces[a].id < document.faces[b].id;
    });
    for (const auto face_index : patch.faces) {
      const auto &face = document.faces[face_index];
      for (std::size_t corner = 0; corner < face.vertices.size(); ++corner) {
        const auto edge = edge_key(face.vertices[corner],
                                   face.vertices[(corner + 1U) % face.vertices.size()]);
        patch.edges[edge].push_back({face_index, corner});
        if (selected_edges.contains(edge)) {
          patch.selected_edges.insert(edge);
        }
      }
    }
    patches.push_back(std::move(patch));
  }
  return Result<std::vector<IncidentPatch>>::success(std::move(patches));
}

[[nodiscard]] std::optional<Error> order_guides(
    IncidentPatch &patch, const std::optional<NativeEdge> &active_edge) {
  std::map<NativeId, std::vector<NativeId>> neighbors;
  for (const auto &[a, b] : patch.selected_edges) {
    neighbors[a].push_back(b);
    neighbors[b].push_back(a);
  }
  for (auto &[vertex, adjacent] : neighbors) {
    static_cast<void>(vertex);
    std::ranges::sort(adjacent);
    if (adjacent.size() > 2U) {
      return peel_error("UV Peel guides must be nonbranching edge paths or loops.");
    }
  }
  const auto reference_edge = active_edge.has_value() &&
                                      patch.selected_edges.contains(*active_edge)
                                  ? *active_edge
                                  : *patch.selected_edges.begin();
  std::set<NativeEdge> remaining = patch.selected_edges;
  while (!remaining.empty()) {
    const auto first_edge = *remaining.begin();
    std::vector<NativeId> component{first_edge.first};
    std::set<NativeId> visited{first_edge.first};
    std::set<NativeEdge> component_edges;
    for (std::size_t next = 0; next < component.size(); ++next) {
      const auto vertex = component[next];
      for (const auto adjacent : neighbors.at(vertex)) {
        component_edges.insert(edge_key(vertex, adjacent));
        if (visited.insert(adjacent).second) {
          component.push_back(adjacent);
        }
      }
    }
    std::vector<NativeId> endpoints;
    for (const auto vertex : component) {
      if (neighbors.at(vertex).size() == 1U) {
        endpoints.push_back(vertex);
      }
    }
    if (!endpoints.empty() && endpoints.size() != 2U) {
      return peel_error("UV Peel could not order an ambiguous edge guide.");
    }
    GuidePath guide;
    guide.closed = endpoints.empty();
    guide.reference = component_edges.contains(reference_edge);
    const auto start_edge = guide.reference ? reference_edge : first_edge;
    const NativeId start = guide.closed ? start_edge.first
                                        : *std::ranges::min_element(endpoints);
    guide.vertices.push_back(start);
    NativeId previous = quader_poly::kInvalidElementId;
    NativeId current = start;
    std::set<NativeEdge> traversed;
    while (traversed.size() < component_edges.size()) {
      const auto &adjacent = neighbors.at(current);
      const auto next = previous == quader_poly::kInvalidElementId && guide.closed
                            ? start_edge.second
                            : (adjacent.front() != previous ? adjacent.front()
                                                            : adjacent.back());
      const auto edge = edge_key(current, next);
      if (!traversed.insert(edge).second) {
        return peel_error("UV Peel could not traverse an edge guide exactly once.");
      }
      guide.vertices.push_back(next);
      previous = current;
      current = next;
    }
    if ((guide.closed && current != start) ||
        (!guide.closed && neighbors.at(current).size() != 1U)) {
      return peel_error("UV Peel found an incomplete edge guide.");
    }
    for (const auto &edge : component_edges) {
      remaining.erase(edge);
    }
    patch.guides.push_back(std::move(guide));
  }
  return std::nullopt;
}

/// Copies the authored values consumed by the isolated structured solver.
struct StructuredPeelMesh {
  std::vector<detail::StructuredPeelPosition> positions;
  std::shared_ptr<const detail::StructuredPeelTopology> topology;
  std::map<NativeId, std::size_t> vertex_indices;
};

[[nodiscard]] bool guide_has_quad_faces(const NativeDocument &document,
                                        const EdgeUses &edges, const GuidePath &guide) {
  for (std::size_t endpoint = 1U; endpoint < guide.vertices.size(); ++endpoint) {
    for (const auto use : edges.at(edge_key(guide.vertices[endpoint - 1U],
                                           guide.vertices[endpoint]))) {
      if (document.faces[use.face].vertices.size() == 4U) {
        return true;
      }
    }
  }
  return false;
}

[[nodiscard]] Result<StructuredPeelMesh> structured_peel_mesh(const NativeDocument &document) {
  StructuredPeelMesh result;
  std::vector<detail::StructuredPeelFace> faces;
  result.positions.reserve(document.vertices.size());
  faces.reserve(document.faces.size());
  for (const auto &vertex : document.vertices) {
    result.vertex_indices.emplace(vertex.id, result.positions.size());
    result.positions.push_back({vertex.position.x, vertex.position.y, vertex.position.z});
  }
  for (const auto &face : document.faces) {
    detail::StructuredPeelFace copied;
    copied.vertices.reserve(face.vertices.size());
    for (const auto vertex : face.vertices) {
      copied.vertices.push_back(result.vertex_indices.at(vertex));
    }
    faces.push_back(std::move(copied));
  }
  auto topology = detail::make_structured_peel_topology(std::move(faces), result.positions.size());
  if (!topology.success) {
    return Result<StructuredPeelMesh>::failure(peel_error(std::move(topology.error_message)));
  }
  result.topology = std::move(topology.topology);
  return Result<StructuredPeelMesh>::success(std::move(result));
}

[[nodiscard]] detail::StructuredPeelResult compute_structured_guide(
    const StructuredPeelMesh &mesh, const NativeTransform &world,
    const GuidePath &guide, const UvPeelOptions &options,
    std::size_t &remaining_corner_writes) {
  std::vector<std::size_t> guide_indices;
  guide_indices.reserve(guide.vertices.size());
  for (const auto vertex : guide.vertices) {
    guide_indices.push_back(mesh.vertex_indices.at(vertex));
  }
  const auto basis = [](QVec3 axis) {
    return detail::StructuredPeelPosition{axis.x, axis.y, axis.z};
  };
  const detail::StructuredPeelInput input{
      .positions = mesh.positions,
      .topology = mesh.topology,
      .guide = guide_indices,
      .closed = guide.closed,
      .world_basis = {basis(world.x_axis), basis(world.y_axis), basis(world.z_axis)},
      .options = {.axis_u = options.axis == UvPeelAxis::U,
                  .world_space = options.world_space,
                  .u_repeat = options.u_repeat, .v_repeat = options.v_repeat,
                  .u_offset = options.u_offset, .v_offset = options.v_offset},
      .corner_budget_cap = kUvPeelMaxCornerWrites};
  return detail::compute_structured_uv_peel(input, remaining_corner_writes);
}

[[nodiscard]] Result<ChartWork> build_chart(
    const NativeDocument &document, const VertexLookup &vertices,
    const NativeTransform &world, const IncidentPatch &patch,
    const std::set<NativeEdge> &cuts) {
  const auto fail = [](std::string message) {
    return Result<ChartWork>::failure(peel_error(std::move(message)));
  };
  std::size_t corner_count = 0;
  std::size_t triangle_count = 0;
  std::map<std::size_t, std::size_t> offsets;
  NativeId anchor_id = std::numeric_limits<NativeId>::max();
  for (const auto face_index : patch.faces) {
    const auto &face = document.faces[face_index];
    if (face.vertices.size() < 3U) {
      return fail("UV Peel requires valid polygon face loops.");
    }
    offsets.emplace(face_index, corner_count);
    corner_count += face.vertices.size();
    triangle_count += face.vertices.size() - 2U;
    for (const auto id : face.vertices) {
      const auto *vertex = vertices.find(id);
      if (vertex == nullptr || !finite(vertex->position)) {
        return fail("UV Peel found an invalid source vertex.");
      }
      anchor_id = std::min(anchor_id, id);
    }
    if (triangle_count > detail::kUvPeelMaxChartTriangles ||
        corner_count > detail::kUvPeelMaxChartTriangles * 3U) {
      return fail("UV Peel chart exceeds the supported triangle budget.");
    }
  }
  CornerSets sets(corner_count);
  CornerSets face_sets(patch.faces.size());
  ChartWork result;
  for (std::size_t slot = 0; slot < patch.faces.size(); ++slot) {
    result.face_slots.emplace(patch.faces[slot], slot);
  }
  for (const auto &[edge, uses] : patch.edges) {
    if (uses.empty() || uses.size() > 2U) {
      return fail("UV Peel requires manifold authored edges.");
    }
    if (uses.size() != 2U) {
      continue;
    }
    const auto &first_face = document.faces[uses[0].face];
    const auto &second_face = document.faces[uses[1].face];
    if (first_face.vertices[uses[0].corner] ==
        second_face.vertices[uses[1].corner]) {
      return fail("UV Peel requires consistently oriented shared face edges.");
    }
    if (cuts.contains(edge)) {
      continue;
    }
    const auto corner_for = [&](const EdgeUse &use, NativeId id) {
      const auto &face = document.faces[use.face];
      return offsets.at(use.face) +
             (face.vertices[use.corner] == id
                  ? use.corner
                  : (use.corner + 1U) % face.vertices.size());
    };
    sets.join(corner_for(uses[0], edge.first), corner_for(uses[1], edge.first));
    sets.join(corner_for(uses[0], edge.second), corner_for(uses[1], edge.second));
    face_sets.join(result.face_slots.at(uses[0].face),
                   result.face_slots.at(uses[1].face));
  }
  for (std::size_t slot = 1; slot < patch.faces.size(); ++slot) {
    if (face_sets.root(slot) != face_sets.root(0)) {
      return fail("A transverse UV cut would disconnect the guided surface.");
    }
  }
  const auto origin = vertices.find(anchor_id)->position;
  std::map<std::size_t, std::size_t> node_for_root;
  std::map<NativeId, std::set<std::size_t>> roots_for_vertex;
  result.face_nodes.reserve(patch.faces.size());
  for (const auto face_index : patch.faces) {
    const auto &face = document.faces[face_index];
    std::vector<std::size_t> nodes;
    nodes.reserve(face.vertices.size());
    for (std::size_t corner = 0; corner < face.vertices.size(); ++corner) {
      const auto root = sets.root(offsets.at(face_index) + corner);
      const auto [entry, inserted] = node_for_root.try_emplace(
          root, result.chart.positions.size());
      if (inserted) {
        const auto position = world_difference(world,
            vertices.find(face.vertices[corner])->position, origin);
        if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
            !std::isfinite(position.z)) {
          return fail("UV Peel world-space geometry is non-finite.");
        }
        result.chart.positions.push_back(position);
        result.node_vertices.push_back(face.vertices[corner]);
      }
      nodes.push_back(entry->second);
      roots_for_vertex[face.vertices[corner]].insert(root);
    }
    result.face_nodes.push_back(std::move(nodes));
  }
  if (result.chart.positions.size() > detail::kUvPeelMaxChartVertices) {
    return fail("UV Peel chart exceeds the supported vertex budget.");
  }
  if (cuts.empty()) {
    for (const auto &[vertex, roots] : roots_for_vertex) {
      static_cast<void>(vertex);
      if (roots.size() != 1U) {
        return fail("UV Peel cannot flatten a pinched or nonmanifold vertex fan.");
      }
    }
  }
  std::map<std::pair<std::size_t, std::size_t>, std::vector<bool>> chart_edges;
  for (std::size_t slot = 0; slot < patch.faces.size(); ++slot) {
    const auto &face = document.faces[patch.faces[slot]];
    const auto &nodes = result.face_nodes[slot];
    if (nodes.size() == 4U) {
      // Retain authored quad boundaries, including any transverse UV copies.
      // Canonical triangle diagonals remain separate from strip recognition.
      result.chart.quads.push_back({nodes[0], nodes[1], nodes[2], nodes[3]});
    }
    for (std::size_t corner = 0; corner < nodes.size(); ++corner) {
      const auto a = nodes[corner];
      const auto b = nodes[(corner + 1U) % nodes.size()];
      if (a == b) {
        return fail("UV Peel produced a collapsed chart edge.");
      }
      chart_edges[std::minmax(a, b)].push_back(a < b);
    }
    const auto triangles = quader_poly::document_internal::triangulate_face_local_indices(
        document, face, vertices);
    if (triangles.size() != face.vertices.size() - 2U) {
      return fail("UV Peel could not preserve the canonical polygon triangulation.");
    }
    std::vector<std::array<std::size_t, 3>> triangle_corners;
    triangle_corners.reserve(triangles.size());
    for (const auto &triangle : triangles) {
      if (triangle.a >= nodes.size() || triangle.b >= nodes.size() ||
          triangle.c >= nodes.size()) {
        return fail("UV Peel found an invalid canonical polygon triangle.");
      }
      result.chart.triangles.push_back(
          {{nodes[triangle.a], nodes[triangle.b], nodes[triangle.c]}});
      triangle_corners.push_back({triangle.a, triangle.b, triangle.c});
    }
    result.face_triangle_corners.push_back(std::move(triangle_corners));
  }
  std::map<std::size_t, std::vector<std::size_t>> boundary_neighbors;
  for (const auto &[edge, orientations] : chart_edges) {
    if (orientations.size() > 2U ||
        (orientations.size() == 2U && orientations[0] == orientations[1])) {
      return fail("UV Peel chart has a nonmanifold or inconsistent shared edge.");
    }
    if (orientations.size() == 1U) {
      boundary_neighbors[edge.first].push_back(edge.second);
      boundary_neighbors[edge.second].push_back(edge.first);
    }
  }
  for (auto &[node, adjacent] : boundary_neighbors) {
    static_cast<void>(node);
    std::ranges::sort(adjacent);
    if (adjacent.size() != 2U) {
      return fail("UV Peel chart has an ambiguous boundary vertex.");
    }
  }
  std::set<std::size_t> visited;
  for (const auto &[start, adjacent] : boundary_neighbors) {
    static_cast<void>(adjacent);
    if (!visited.insert(start).second) {
      continue;
    }
    std::vector<std::size_t> boundary{start};
    std::size_t previous = kNoIndex;
    std::size_t current = start;
    do {
      const auto &next_nodes = boundary_neighbors.at(current);
      const auto next = next_nodes.front() != previous ? next_nodes.front()
                                                       : next_nodes.back();
      if (next == start) {
        break;
      }
      if (!visited.insert(next).second) {
        return fail("UV Peel chart boundary intersects itself topologically.");
      }
      boundary.push_back(next);
      previous = current;
      current = next;
    } while (boundary.size() <= boundary_neighbors.size());
    if (boundary.size() < 3U) {
      return fail("UV Peel chart boundary has fewer than three corners.");
    }
    result.boundaries.push_back(std::move(boundary));
  }
  result.euler_characteristic =
      static_cast<std::int64_t>(result.chart.positions.size()) -
      static_cast<std::int64_t>(chart_edges.size()) +
      static_cast<std::int64_t>(patch.faces.size());
  return Result<ChartWork>::success(std::move(result));
}

/// Stores an authored-edge shortest path used only to split UV corner identities.
struct TransversePath {
  std::vector<NativeId> vertices;
  double length = 0.0;
};

[[nodiscard]] std::optional<TransversePath> path_to_boundary(
    const NativeDocument &document, const VertexLookup &vertices,
    const NativeTransform &world, const IncidentPatch &patch, NativeId start,
    const std::set<NativeId> &destination, const std::set<NativeId> &all_boundary,
    const std::set<NativeId> &blocked) {
  static_cast<void>(document);
  std::map<NativeId, std::vector<std::pair<NativeId, double>>> neighbors;
  for (const auto &[edge, uses] : patch.edges) {
    if (uses.size() != 2U || patch.selected_edges.contains(edge)) {
      continue;
    }
    const double length = world_distance(world, vertices.find(edge.first)->position,
                                         vertices.find(edge.second)->position);
    if (!std::isfinite(length) || length <= 0.0) {
      return std::nullopt;
    }
    neighbors[edge.first].push_back({edge.second, length});
    neighbors[edge.second].push_back({edge.first, length});
  }
  using QueueEntry = std::pair<double, NativeId>;
  std::priority_queue<QueueEntry, std::vector<QueueEntry>, std::greater<QueueEntry>> queue;
  std::map<NativeId, double> distance{{start, 0.0}};
  std::map<NativeId, NativeId> previous;
  queue.push({0.0, start});
  while (!queue.empty()) {
    const auto [length, current] = queue.top();
    queue.pop();
    if (length != distance.at(current)) {
      continue;
    }
    if (destination.contains(current)) {
      TransversePath result{{current}, length};
      while (result.vertices.back() != start) {
        result.vertices.push_back(previous.at(result.vertices.back()));
      }
      std::ranges::reverse(result.vertices);
      return result;
    }
    const auto found = neighbors.find(current);
    if (found == neighbors.end()) {
      continue;
    }
    for (const auto &[next, cost] : found->second) {
      if (blocked.contains(next) ||
          (all_boundary.contains(next) && !destination.contains(next) && next != start)) {
        continue;
      }
      const double next_distance = length + cost;
      const auto old = distance.find(next);
      if (old == distance.end() || next_distance < old->second) {
        distance[next] = next_distance;
        previous[next] = current;
        queue.push({next_distance, next});
      }
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::vector<std::set<NativeEdge>> transverse_cut_candidates(
    const NativeDocument &document, const VertexLookup &vertices,
    const NativeTransform &world, const IncidentPatch &patch,
    const ChartWork &uncut) {
  std::array<std::set<NativeId>, 2> boundaries;
  std::set<NativeId> all_boundary;
  for (std::size_t loop = 0; loop < boundaries.size(); ++loop) {
    for (const auto node : uncut.boundaries[loop]) {
      boundaries[loop].insert(uncut.node_vertices[node]);
      all_boundary.insert(uncut.node_vertices[node]);
    }
  }
  const auto reference = std::ranges::find(patch.guides, true, &GuidePath::reference);
  const NativeId start = reference->vertices.front();
  std::vector<std::set<NativeEdge>> candidates;
  for (std::size_t first_loop = 0; first_loop < 2U; ++first_loop) {
    const auto first = path_to_boundary(document, vertices, world, patch, start,
                                        boundaries[first_loop], all_boundary, {});
    if (!first.has_value()) {
      continue;
    }
    std::set<NativeId> blocked(first->vertices.begin(), first->vertices.end());
    blocked.erase(start);
    const auto second = path_to_boundary(document, vertices, world, patch, start,
        boundaries[1U - first_loop], all_boundary, blocked);
    if (!second.has_value()) {
      continue;
    }
    std::set<NativeEdge> cuts;
    for (const auto *path : {&*first, &*second}) {
      for (std::size_t index = 1; index < path->vertices.size(); ++index) {
        cuts.insert(edge_key(path->vertices[index - 1U], path->vertices[index]));
      }
    }
    if (!cuts.empty() && std::ranges::find(candidates, cuts) == candidates.end()) {
      candidates.push_back(std::move(cuts));
    }
  }
  return candidates;
}

[[nodiscard]] std::optional<Error> map_guides(
    const NativeDocument &document, const IncidentPatch &patch, ChartWork &chart) {
  for (const auto &guide : patch.guides) {
    std::vector<std::pair<std::size_t, std::size_t>> edges;
    for (std::size_t index = 1; index < guide.vertices.size(); ++index) {
      const auto a = guide.vertices[index - 1U];
      const auto b = guide.vertices[index];
      const auto &uses = patch.edges.at(edge_key(a, b));
      const auto &use = uses.front();
      const auto &face = document.faces[use.face];
      const auto &nodes = chart.face_nodes[chart.face_slots.at(use.face)];
      const auto first = nodes[use.corner];
      const auto second = nodes[(use.corner + 1U) % nodes.size()];
      edges.push_back(face.vertices[use.corner] == a
                          ? std::pair{first, second}
                          : std::pair{second, first});
    }
    std::size_t start = 0;
    if (guide.closed) {
      std::size_t openings = 0;
      for (std::size_t index = 0; index < edges.size(); ++index) {
        if (edges[index].second != edges[(index + 1U) % edges.size()].first) {
          ++openings;
          start = (index + 1U) % edges.size();
        }
      }
      if (openings != 1U) {
        return peel_error("A closed UV Peel guide cannot be opened coherently by a transverse cut.");
      }
      if (guide.reference && chart.node_vertices[edges[start].first] != guide.vertices.front()) {
        return peel_error("The transverse UV cut cannot preserve the active guide's start.");
      }
    }
    detail::PeelGuide mapped;
    mapped.reference = guide.reference;
    mapped.vertices.push_back(edges[start].first);
    std::set<std::size_t> visited{edges[start].first};
    for (std::size_t offset = 0; offset < edges.size(); ++offset) {
      const auto &edge = edges[(start + offset) % edges.size()];
      if (mapped.vertices.back() != edge.first || !visited.insert(edge.second).second) {
        return peel_error("A transverse UV cut would split or repeat the interior of an open guide.");
      }
      mapped.vertices.push_back(edge.second);
    }
    chart.chart.guides.push_back(std::move(mapped));
  }
  return std::nullopt;
}

[[nodiscard]] Result<ChartWork> prepare_chart(
    const NativeDocument &document, const VertexLookup &vertices,
    const NativeTransform &world, IncidentPatch &patch,
    const std::optional<NativeEdge> &active_edge) {
  if (auto error = order_guides(patch, active_edge)) {
    return Result<ChartWork>::failure(std::move(*error));
  }
  auto uncut = build_chart(document, vertices, world, patch, {});
  if (!uncut.ok()) {
    return uncut;
  }
  if (uncut.value().boundaries.size() == 1U &&
      uncut.value().euler_characteristic == 1) {
    if (std::ranges::any_of(patch.guides, &GuidePath::closed)) {
      return Result<ChartWork>::failure(make_error(ErrorCode::UnsupportedOperation,
          "A closed guide on a disk cannot be straightened safely. Select an open flow path or an uncapped tube surface."));
    }
    if (auto error = map_guides(document, patch, uncut.value())) {
      return Result<ChartWork>::failure(std::move(*error));
    }
    return uncut;
  }
  if (uncut.value().boundaries.size() != 2U ||
      uncut.value().euler_characteristic != 0) {
    return Result<ChartWork>::failure(make_error(ErrorCode::UnsupportedOperation,
        "UV Peel supports disk patches and annular tube patches. Closed, handled, or multiply holed surfaces need an unambiguous guided patch."));
  }
  const auto candidates = transverse_cut_candidates(document, vertices, world, patch,
                                                     uncut.value());
  for (const auto &cuts : candidates) {
    auto cut = build_chart(document, vertices, world, patch, cuts);
    if (!cut.ok() || cut.value().boundaries.size() != 1U ||
        cut.value().euler_characteristic != 1) {
      continue;
    }
    if (map_guides(document, patch, cut.value()).has_value()) {
      continue;
    }
    return cut;
  }
  return Result<ChartWork>::failure(make_error(ErrorCode::UnsupportedOperation,
      "UV Peel could not find a safe authored-edge transverse cut that opens every closed guide. Add a transverse edge or use an open guided patch."));
}

[[nodiscard]] Result<std::vector<PeelPoint2>> solve_chart(
    const GuidedPeelChart &chart, const UvPeelOptions &options) {
  auto solved = detail::solve_guided_uv_peel(chart);
  if (!solved.ok()) {
    return solved;
  }
  auto &coordinates = solved.value();
  if (options.axis == UvPeelAxis::V) {
    for (auto &coordinate : coordinates) {
      coordinate = {-coordinate.v, coordinate.u};
    }
  }
  PeelPoint2 minimum{std::numeric_limits<double>::infinity(),
                      std::numeric_limits<double>::infinity()};
  PeelPoint2 maximum{-minimum.u, -minimum.v};
  for (const auto &coordinate : coordinates) {
    minimum.u = std::min(minimum.u, coordinate.u);
    minimum.v = std::min(minimum.v, coordinate.v);
    maximum.u = std::max(maximum.u, coordinate.u);
    maximum.v = std::max(maximum.v, coordinate.v);
  }
  const double width = maximum.u - minimum.u;
  const double height = maximum.v - minimum.v;
  if (!std::isfinite(width) || !std::isfinite(height) ||
      width <= 0.0 || height <= 0.0) {
    return Result<std::vector<PeelPoint2>>::failure(
        peel_error("UV Peel produced collapsed or non-finite chart bounds."));
  }
  constexpr double kTextureDensity =
      quader_poly::document_internal::kGeneratedUvTilesPerWorldUnit;
  for (auto &coordinate : coordinates) {
    if (options.world_space) {
      coordinate.u *= kTextureDensity;
      coordinate.v *= kTextureDensity;
    } else {
      coordinate.u = (coordinate.u - minimum.u) / width;
      coordinate.v = (coordinate.v - minimum.v) / height;
    }
    coordinate.u = coordinate.u * options.u_repeat + options.u_offset;
    coordinate.v = coordinate.v * options.v_repeat + options.v_offset;
  }
  if (auto error = detail::validate_uv_peel_chart(
          chart, coordinates, options.axis == UvPeelAxis::U)) {
    return Result<std::vector<PeelPoint2>>::failure(std::move(*error));
  }
  return solved;
}

[[nodiscard]] std::vector<QVec2> effective_uvs(
    std::span<const QVec3> positions, std::span<const QVec2> base_uvs,
    QVec3 normal, const quader_poly::FaceTextureTransform &transform,
    const NativeTransform &sampling) {
  const auto carrier = quader_poly::face_texture_carrier(
      positions, base_uvs, normal, transform);
  std::vector<QVec2> result;
  result.reserve(positions.size());
  for (std::size_t corner = 0; corner < positions.size(); ++corner) {
    const auto position = positions[corner];
    const auto base = base_uvs.empty()
                          ? quader_poly::generated_texture_uv(position, normal)
                          : base_uvs[corner];
    result.push_back(quader_poly::evaluate_texture_mapping(
        quader_poly::apply_face_texture_transform(base, transform), position,
        normal, carrier, sampling));
  }
  return result;
}

[[nodiscard]] bool same_uvs(std::span<const QVec2> a,
                            std::span<const QVec2> b, PeelPoint2 tolerance) {
  if (a.size() != b.size()) {
    return false;
  }
  for (std::size_t index = 0; index < a.size(); ++index) {
    if (!finite(a[index]) || !finite(b[index]) ||
        std::abs(static_cast<double>(a[index].x) - b[index].x) > tolerance.u ||
        std::abs(static_cast<double>(a[index].y) - b[index].y) > tolerance.v) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] PeelPoint2 chart_float_tolerances(std::span<const PeelPoint2> coordinates) {
  PeelPoint2 minimum = coordinates.front();
  PeelPoint2 maximum = minimum;
  for (const auto &coordinate : coordinates) {
    minimum.u = std::min(minimum.u, coordinate.u);
    minimum.v = std::min(minimum.v, coordinate.v);
    maximum.u = std::max(maximum.u, coordinate.u);
    maximum.v = std::max(maximum.v, coordinate.v);
  }
  constexpr double kRelativeRoundoff =
      8.0 * std::numeric_limits<float>::epsilon();
  return {kRelativeRoundoff * (maximum.u - minimum.u),
          kRelativeRoundoff * (maximum.v - minimum.v)};
}

/// Carries canonical storage certification back to each Peel output policy.
struct StoredFaceUvs {
  std::vector<QVec2> effective;
  std::vector<QVec2> desired;
  std::vector<std::array<double, 2>> precision;
};

[[nodiscard]] Result<StoredFaceUvs> prepare_face_storage(
    const NativeDocument &document, const VertexLookup &vertices,
    const NativeTransform &sampling, std::size_t face_index,
    std::span<const PeelPoint2> coordinates, PeelPoint2 axis_tolerances,
    std::map<std::size_t, std::vector<QVec2>> &updates) {
  const auto fail = [](std::string message) {
    return Result<StoredFaceUvs>::failure(peel_error(std::move(message)));
  };
  const auto &face = document.faces[face_index];
  if (!quader_poly::valid_face_texture_transform(face.texture_transform) ||
      coordinates.size() != face.vertices.size() ||
      (!face.uvs.empty() && face.uvs.size() != coordinates.size())) {
    return fail("UV Peel found invalid source face texture settings.");
  }
  StoredFaceUvs result;
  std::vector<QVec3> positions;
  positions.reserve(coordinates.size());
  result.desired.reserve(coordinates.size());
  for (std::size_t corner = 0U; corner < coordinates.size(); ++corner) {
    positions.push_back(vertices.find(face.vertices[corner])->position);
    const auto &coordinate = coordinates[corner];
    const QVec2 target{static_cast<float>(coordinate.u), static_cast<float>(coordinate.v)};
    if (!finite(target)) {
      return fail("UV Peel coordinates exceed the supported stored UV range.");
    }
    result.desired.push_back(target);
  }
  const auto normal = quader_poly::face_normal(document, face);
  if (!finite(normal) || quader::length_squared(normal) == 0.0F) {
    return fail("UV Peel found a face without a usable geometric normal.");
  }
  result.effective = effective_uvs(positions, face.uvs, normal,
                                   face.texture_transform, sampling);
  // Later guide components own their final corner writes, including restoring
  // the original values after an earlier component would have changed them.
  updates.erase(face_index);
  std::span<const QVec2> stored_base_uvs = face.uvs;
  // A long U span must not hide a material V change, or vice versa.
  if (!same_uvs(result.effective, result.desired, axis_tolerances) ||
      !quader_poly::face_texture_uvs_reconstruct(
          positions, stored_base_uvs, result.desired, normal,
          face.texture_transform, sampling)) {
    auto base = quader_poly::solve_face_texture_base_uvs(
        positions, result.desired, normal, face.texture_transform, sampling);
    if (!base.has_value()) {
      return fail("UV Peel cannot preserve the occurrence sampling transform for face " +
                  std::to_string(face.id) + ".");
    }
    // Identical geometry/options retain exact base storage on repeat, even
    // when occurrence compensation carries a certified float residual.
    if (!same_uvs(face.uvs, *base, {})) {
      result.effective = effective_uvs(positions, *base, normal,
                                       face.texture_transform, sampling);
      stored_base_uvs = updates.emplace(face_index, std::move(*base)).first->second;
    }
  }
  if (!quader_poly::face_texture_uvs_reconstruct(
          positions, stored_base_uvs, result.desired, normal,
          face.texture_transform, sampling)) {
    return fail("UV Peel cannot reconstruct its intended coordinates for face " +
                std::to_string(face.id) + ".");
  }
  result.precision = quader_poly::face_texture_precision_bounds(
      positions, stored_base_uvs, normal, face.texture_transform, sampling,
      quader_poly::TextureUvPrecision::Reconstruction);
  if (result.precision.size() != coordinates.size()) {
    return fail("UV Peel cannot determine stored coordinate precision for face " +
                std::to_string(face.id) + ".");
  }
  for (const auto coordinate : result.effective) {
    if (!finite(coordinate)) {
      return fail("UV Peel produced a non-finite effective stored UV.");
    }
  }
  return Result<StoredFaceUvs>::success(std::move(result));
}

[[nodiscard]] std::optional<Error> prepare_face_uvs(
    const NativeDocument &document, const VertexLookup &vertices,
    const NativeTransform &sampling, const IncidentPatch &patch,
    const ChartWork &chart, std::span<const PeelPoint2> desired_coordinates,
    const UvPeelOptions &options,
    std::map<std::size_t, std::vector<QVec2>> &updates) {
  const auto axis_tolerances = chart_float_tolerances(desired_coordinates);
  const double tolerance = std::max(axis_tolerances.u, axis_tolerances.v);
  double stored_contact_tolerance = tolerance;
  std::vector<PeelPoint2> stored_coordinates(chart.chart.positions.size());
  std::vector<PeelPoint2> stored_error_bounds(chart.chart.positions.size());
  std::vector<bool> populated(chart.chart.positions.size(), false);
  std::vector<std::array<PeelPoint2, 3>> stored_triangles;
  stored_triangles.reserve(chart.chart.triangles.size());
  for (std::size_t slot = 0; slot < patch.faces.size(); ++slot) {
    const auto face_index = patch.faces[slot];
    const auto &nodes = chart.face_nodes[slot];
    std::vector<PeelPoint2> coordinates;
    coordinates.reserve(nodes.size());
    for (const auto node : nodes) {
      coordinates.push_back(desired_coordinates[node]);
    }
    auto stored = prepare_face_storage(document, vertices, sampling, face_index,
                                        coordinates, axis_tolerances, updates);
    if (!stored.ok()) {
      return stored.error();
    }
    const auto &actual = stored.value().effective;
    const auto &desired = stored.value().desired;
    const auto &reconstruction_bounds = stored.value().precision;
    for (std::size_t corner = 0; corner < nodes.size(); ++corner) {
      const auto node = nodes[corner];
      const auto coordinate = actual[corner];
      if (!finite(coordinate)) {
        return peel_error("UV Peel produced a non-finite effective stored UV.");
      }
      // Each face was independently certified against the same desired node.
      // Include only its certified residual beyond the derived storage bound;
      // this retains the canonical validator's roundoff allowance without
      // introducing another precision formula or semantic no-op tolerance.
      const PeelPoint2 error_bound{
          std::max(reconstruction_bounds[corner][0],
              std::abs(static_cast<double>(coordinate.x) - desired[corner].x)),
          std::max(reconstruction_bounds[corner][1],
              std::abs(static_cast<double>(coordinate.y) - desired[corner].y))};
      if (populated[node]) {
        const double u_error = std::abs(stored_coordinates[node].u - coordinate.x);
        const double v_error = std::abs(stored_coordinates[node].v - coordinate.y);
        if (u_error > stored_error_bounds[node].u + error_bound.u ||
            v_error > stored_error_bounds[node].v + error_bound.v) {
          return peel_error("UV Peel cannot keep shared UV corners continuous after occurrence compensation.");
        }
        // The overlap owner admits contact only between identical chart nodes
        // and tests the actual displaced endpoints. Give it the measured
        // contact budget, rather than the larger reconstruction error bound.
        stored_contact_tolerance = std::max({stored_contact_tolerance, u_error, v_error});
      } else {
        stored_coordinates[node] = {coordinate.x, coordinate.y};
        stored_error_bounds[node] = error_bound;
        populated[node] = true;
      }
    }
    for (const auto &corners : chart.face_triangle_corners[slot]) {
      stored_triangles.push_back({PeelPoint2{actual[corners[0]].x, actual[corners[0]].y},
                                  PeelPoint2{actual[corners[1]].x, actual[corners[1]].y},
                                  PeelPoint2{actual[corners[2]].x, actual[corners[2]].y}});
    }
  }
  if (auto error = detail::validate_uv_peel_chart(
          chart.chart, stored_coordinates, options.axis == UvPeelAxis::U,
          stored_triangles, stored_contact_tolerance)) {
    error->message = "Stored effective UVs: " + error->message;
    return error;
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<Error> validate_corner_mapping(
    const NativeDocument &document, const VertexLookup &vertices,
    std::size_t face_index, std::span<const QVec2> coordinates) {
  const auto &face = document.faces[face_index];
  const auto triangles = quader_poly::document_internal::triangulate_face_local_indices(
      document, face, vertices);
  if (triangles.size() != face.vertices.size() - 2U) {
    return peel_error("UV Peel could not preserve the canonical polygon triangulation.");
  }
  double orientation = 0.0;
  for (const auto &triangle : triangles) {
    if (triangle.a >= coordinates.size() || triangle.b >= coordinates.size() ||
        triangle.c >= coordinates.size()) {
      return peel_error("UV Peel found an invalid canonical polygon triangle.");
    }
    const auto a = coordinates[triangle.a];
    const auto b = coordinates[triangle.b];
    const auto c = coordinates[triangle.c];
    const double ab_u = static_cast<double>(b.x) - a.x;
    const double ab_v = static_cast<double>(b.y) - a.y;
    const double ac_u = static_cast<double>(c.x) - a.x;
    const double ac_v = static_cast<double>(c.y) - a.y;
    const double area = ab_u * ac_v - ab_v * ac_u;
    if (!std::isfinite(area) || area == 0.0 ||
        (orientation != 0.0 && std::signbit(area) != std::signbit(orientation))) {
      return peel_error("UV Peel produced collapsed or folded texture coordinates for face " +
                         std::to_string(face.id) + ".");
    }
    orientation = area;
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<Error> prepare_structured_uvs(
    const NativeDocument &document, const VertexLookup &vertices,
    const NativeTransform &sampling,
    std::span<const detail::StructuredPeelFaceCoordinates> faces,
    std::map<std::size_t, std::vector<QVec2>> &updates) {
  for (const auto &face : faces) {
    std::array<PeelPoint2, 4> coordinates;
    for (std::size_t corner = 0U; corner < coordinates.size(); ++corner) {
      coordinates[corner] = {face.corners[corner].u, face.corners[corner].v};
    }
    auto stored = prepare_face_storage(document, vertices, sampling, face.face_index,
                                        coordinates, chart_float_tolerances(coordinates), updates);
    if (!stored.ok()) {
      return stored.error();
    }
    if (auto error = validate_corner_mapping(document, vertices, face.face_index,
                                              stored.value().desired)) {
      return error;
    }
    if (auto error = validate_corner_mapping(document, vertices, face.face_index,
                                              stored.value().effective)) {
      error->message = "Stored effective UVs: " + error->message;
      return error;
    }
  }
  return std::nullopt;
}

[[nodiscard]] Result<PreparedUvPeelDocument> prepare_target(
    const UvPeelTarget &target, const UvPeelOptions &options) {
  const auto fail = [&](Error error) {
    error.message = "UV Peel target " + std::to_string(target.key) + ": " + error.message;
    return Result<PreparedUvPeelDocument>::failure(std::move(error));
  };
  const auto world = native_transform(target.world_from_local);
  const auto sampling = native_transform(target.sampling_from_local);
  if (!valid_transform(world) || !valid_transform(sampling)) {
    return fail(make_error(ErrorCode::InvalidArgument,
        "UV Peel requires finite, invertible world and sampling transforms."));
  }
  const auto source_validation =
      PolygonDocumentNativeAccess::validate_authored_topology(*target.document);
  if (!source_validation.ok()) {
    return fail(source_validation.error());
  }
  const auto &document = PolygonDocumentNativeAccess::document(*target.document);
  const VertexLookup vertices(document);
  const auto all_edges = authored_edges(document);
  std::set<NativeEdge> selected_edges;
  const auto validate_edge = [&](const EdgeKey &edge) -> std::optional<Error> {
    if (!edge.valid() || edge.a != make_id<VertexTag>(edge.a.index) ||
        edge.b != make_id<VertexTag>(edge.b.index) ||
        !all_edges.contains(edge_key(edge.a.index, edge.b.index))) {
      return make_error(ErrorCode::StaleId,
          "A UV Peel guide has stale endpoint IDs or is not an authored edge.");
    }
    return std::nullopt;
  };
  for (const auto &edge : target.edges) {
    if (auto error = validate_edge(edge)) {
      return fail(std::move(*error));
    }
    if (!selected_edges.insert(edge_key(edge.a.index, edge.b.index)).second) {
      return fail(make_error(ErrorCode::InvalidArgument,
          "The UV Peel request contains a duplicate guide edge."));
    }
  }
  std::optional<NativeEdge> active_edge;
  if (target.active_edge.has_value()) {
    if (auto error = validate_edge(*target.active_edge)) {
      return fail(std::move(*error));
    }
    active_edge = edge_key(target.active_edge->a.index, target.active_edge->b.index);
    if (!selected_edges.contains(*active_edge)) {
      return fail(make_error(ErrorCode::StaleId,
          "The active UV Peel guide no longer belongs to the selection."));
    }
  }
  IncidentPatch selected;
  selected.selected_edges = selected_edges;
  if (auto error = order_guides(selected, active_edge)) {
    return fail(std::move(*error));
  }
  std::map<std::size_t, std::vector<QVec2>> updates;
  std::map<std::size_t, std::optional<quader_poly::CircularUvInterpolation>> circular_updates;
  const auto assign_circular = [&](std::size_t face_index,
      std::optional<quader_poly::CircularUvInterpolation> interpolation) {
    circular_updates.erase(face_index);
    if (document.faces[face_index].circular_uv_interpolation != interpolation) {
      circular_updates.emplace(face_index, std::move(interpolation));
    }
  };
  std::optional<StructuredPeelMesh> structured_mesh;
  std::size_t remaining_corner_writes = kUvPeelMaxCornerWrites;
  for (const auto &guide : selected.guides) {
    if (guide_has_quad_faces(document, all_edges, guide)) {
      if (!structured_mesh.has_value()) {
        auto prepared_mesh = structured_peel_mesh(document);
        if (!prepared_mesh.ok()) {
          return fail(prepared_mesh.error());
        }
        structured_mesh = std::move(prepared_mesh).value();
      }
      const auto peel = compute_structured_guide(*structured_mesh, world, guide, options,
                                                 remaining_corner_writes);
      if (!peel.success) {
        return fail(peel_error(peel.error_message));
      }
      auto desired_faces = peel.faces;
      for (std::size_t slot = 0U; slot < desired_faces.size(); ++slot) {
        assign_circular(desired_faces[slot].face_index, std::nullopt);
      }
      if (options.smooth_circular_flow) {
        std::map<std::size_t, std::size_t> face_slots;
        for (std::size_t slot = 0U; slot < desired_faces.size(); ++slot) {
          face_slots.emplace(desired_faces[slot].face_index, slot);
        }
        for (auto &fitted : detail::fit_circular_peel(document, guide.vertices, desired_faces)) {
          desired_faces[face_slots.at(fitted.coordinates.face_index)] = fitted.coordinates;
          assign_circular(fitted.coordinates.face_index, std::move(fitted.interpolation));
        }
      }
      if (auto error = prepare_structured_uvs(document, vertices, sampling,
                                               desired_faces, updates)) {
        return fail(std::move(*error));
      }
      continue;
    }
    // The conformal extension remains available for a guide that cannot be
    // represented by quad-side walks. It retains its own chart safeguards.
    std::set<NativeEdge> fallback_edges;
    for (std::size_t index = 1U; index < guide.vertices.size(); ++index) {
      fallback_edges.insert(edge_key(guide.vertices[index - 1U], guide.vertices[index]));
    }
    auto patches = incident_patches(document, all_edges, fallback_edges);
    if (!patches.ok()) {
      return fail(patches.error());
    }
    for (auto &patch : patches.value()) {
      auto chart = prepare_chart(document, vertices, world, patch, active_edge);
      if (!chart.ok()) {
        return fail(chart.error());
      }
      const auto coordinates = solve_chart(chart.value().chart, options);
      if (!coordinates.ok()) {
        return fail(coordinates.error());
      }
      if (auto error = prepare_face_uvs(document, vertices, sampling, patch,
                                         chart.value(), coordinates.value(), options, updates)) {
        return fail(std::move(*error));
      }
      for (const auto face_index : patch.faces) {
        assign_circular(face_index, std::nullopt);
      }
    }
  }
  PreparedUvPeelDocument result{.key = target.key, .document = *target.document};
  result.receipt.message = "UV Peel unchanged.";
  if (!updates.empty() || !circular_updates.empty()) {
    NativeDocument candidate = document;
    std::set<NativeId> modified_faces;
    for (auto &[face_index, uvs] : updates) {
      candidate.faces[face_index].uvs = std::move(uvs);
      modified_faces.insert(candidate.faces[face_index].id);
    }
    for (auto &[face_index, interpolation] : circular_updates) {
      candidate.faces[face_index].circular_uv_interpolation = std::move(interpolation);
      modified_faces.insert(candidate.faces[face_index].id);
    }
    PolygonDocumentNativeAccess::replace_document(result.document, std::move(candidate), true);
    ++PolygonDocumentNativeAccess::impl(result.document).content_revision;
    const auto validation =
        PolygonDocumentNativeAccess::validate_authored_topology(result.document);
    if (!validation.ok()) {
      return fail(validation.error());
    }
    result.receipt.changed = true;
    result.receipt.message = "Guided surface UVs peeled.";
    result.receipt.dirty.geometry = true;
    for (const auto id : modified_faces) {
      result.receipt.affected.faces.push_back(make_id<FaceTag>(id));
    }
    result.receipt.modified = result.receipt.affected;
  }
  return Result<PreparedUvPeelDocument>::success(std::move(result));
}

} // namespace

Result<UvPeelOptions> validate_uv_peel_options(const UvPeelOptions &options) {
  if ((options.axis != UvPeelAxis::U && options.axis != UvPeelAxis::V) ||
      !std::isfinite(options.u_repeat) || !std::isfinite(options.v_repeat) ||
      options.u_repeat <= 0.0 || options.v_repeat <= 0.0 ||
      !std::isfinite(options.u_offset) || !std::isfinite(options.v_offset)) {
    return Result<UvPeelOptions>::failure(make_error(ErrorCode::InvalidArgument,
        "UV Peel requires U or V flow, finite positive repetition factors, and finite offsets."));
  }
  return Result<UvPeelOptions>::success(options);
}

Result<PreparedUvPeel> prepare_uv_peel(
    std::span<const UvPeelTarget> targets, const UvPeelOptions &options) {
  const auto validated = validate_uv_peel_options(options);
  if (!validated.ok()) {
    return Result<PreparedUvPeel>::failure(validated.error());
  }
  if (targets.empty()) {
    return Result<PreparedUvPeel>::failure(make_error(ErrorCode::InvalidArgument,
        "Select one or more authored edge paths to peel their surface texture."));
  }
  std::map<std::uint64_t, const UvPeelTarget *> ordered;
  for (const auto &target : targets) {
    if (target.document == nullptr || target.edges.empty() ||
        !ordered.emplace(target.key, &target).second) {
      return Result<PreparedUvPeel>::failure(make_error(ErrorCode::InvalidArgument,
          "UV Peel requires unique targets with live documents and selected guide edges."));
    }
  }
  PreparedUvPeel result;
  result.documents.reserve(ordered.size());
  for (const auto &[key, target] : ordered) {
    static_cast<void>(key);
    auto prepared = prepare_target(*target, options);
    if (!prepared.ok()) {
      return Result<PreparedUvPeel>::failure(prepared.error());
    }
    result.changed = result.changed || prepared.value().receipt.changed;
    result.documents.push_back(std::move(prepared).value());
  }
  return Result<PreparedUvPeel>::success(std::move(result));
}

} // namespace quader::modeling
