////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <quader/modeling/neutral/polygon/document_operations.hpp>
#include <quader/modeling/neutral/polygon/document_selection.hpp>
#include <quader/modeling/neutral/polygon/document_validation.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_hull_plane_helpers.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_knife_helpers.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_topology_helpers.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <span>
#include <utility>
#include <vector>

namespace quader_poly {
namespace {

using document_internal::add_unique_edge;
using document_internal::add_unique_id;
using document_internal::edge_exists;

double coordinate(quader::QVec3 position, std::size_t axis) {
  return axis == 0U ? position.x : axis == 1U ? position.y : position.z;
}

double distance_squared(quader::QVec3 left, quader::QVec3 right) {
  const double x = double(left.x) - right.x;
  const double y = double(left.y) - right.y;
  const double z = double(left.z) - right.z;
  return x * x + y * y + z * z;
}

/// Bounds distance searches along the widest axis before exact 3D testing.
class VertexDistanceIndex final {
public:
  explicit VertexDistanceIndex(const Document &document) : document_(document) {
    std::array<double, 3> minimum;
    minimum.fill(std::numeric_limits<double>::infinity());
    std::array<double, 3> maximum;
    maximum.fill(-std::numeric_limits<double>::infinity());
    for (const Vertex &vertex : document.vertices) {
      for (std::size_t axis = 0; axis < 3U; ++axis) {
        minimum[axis] = std::min(minimum[axis], coordinate(vertex.position, axis));
        maximum[axis] = std::max(maximum[axis], coordinate(vertex.position, axis));
      }
    }
    for (std::size_t axis = 1; axis < 3U; ++axis) {
      if (maximum[axis] - minimum[axis] > maximum[axis_] - minimum[axis_]) {
        axis_ = axis;
      }
    }
    indices_.resize(document.vertices.size());
    std::iota(indices_.begin(), indices_.end(), std::size_t{0});
    std::ranges::sort(indices_, [&](std::size_t left, std::size_t right) {
      const double left_value = coordinate(document.vertices[left].position, axis_);
      const double right_value = coordinate(document.vertices[right].position, axis_);
      return left_value == right_value ? left < right : left_value < right_value;
    });
  }

  template <typename Visitor>
  void visit(quader::QVec3 position, double tolerance, Visitor visitor) const {
    const double center = coordinate(position, axis_);
    auto begin = std::lower_bound(
        indices_.begin(), indices_.end(), center - tolerance,
        [&](std::size_t index, double value) {
          return coordinate(document_.vertices[index].position, axis_) < value;
        });
    for (; begin != indices_.end(); ++begin) {
      const Vertex &vertex = document_.vertices[*begin];
      if (coordinate(vertex.position, axis_) > center + tolerance) {
        break;
      }
      const double distance = distance_squared(position, vertex.position);
      if (distance <= tolerance * tolerance) {
        visitor(*begin, distance);
      }
    }
  }

private:
  const Document &document_;
  std::vector<std::size_t> indices_;
  std::size_t axis_ = 0;
};

std::size_t cluster_survivor(const Document &document,
                             std::span<const std::size_t> cluster) {
  if (cluster.size() == 2U) {
    return std::min(cluster[0], cluster[1]);
  }
  std::array<double, 3> centroid{};
  for (const std::size_t index : cluster) {
    for (std::size_t axis = 0; axis < 3U; ++axis) {
      centroid[axis] += coordinate(document.vertices[index].position, axis);
    }
  }
  for (double &value : centroid) {
    value /= static_cast<double>(cluster.size());
  }
  std::size_t survivor = cluster.front();
  double best_distance = std::numeric_limits<double>::infinity();
  for (const std::size_t index : cluster) {
    double distance = 0.0;
    for (std::size_t axis = 0; axis < 3U; ++axis) {
      const double delta = coordinate(document.vertices[index].position, axis) - centroid[axis];
      distance += delta * delta;
    }
    if (distance < best_distance ||
        (distance == best_distance && index < survivor)) {
      survivor = index;
      best_distance = distance;
    }
  }
  return survivor;
}

std::map<ElementId, ElementId> merge_targets(
    const Document &document, const std::set<ElementId> &selected,
    float tolerance) {
  const VertexDistanceIndex index(document);
  const std::size_t invalid_index = document.vertices.size();
  std::vector<std::size_t> targets(document.vertices.size(), invalid_index);
  for (std::size_t vertex_index = 0; vertex_index < document.vertices.size();
       ++vertex_index) {
    const Vertex &vertex = document.vertices[vertex_index];
    if (!selected.contains(vertex.id)) {
      targets[vertex_index] = vertex_index;
      continue;
    }
    double best_distance = std::numeric_limits<double>::infinity();
    index.visit(vertex.position, tolerance, [&](std::size_t candidate, double distance) {
      if (selected.contains(document.vertices[candidate].id)) {
        return;
      }
      if (distance < best_distance ||
          (distance == best_distance && candidate < targets[vertex_index])) {
        best_distance = distance;
        targets[vertex_index] = candidate;
      }
    });
  }

  // Reserve unselected targets first. Remaining clusters use a seed radius,
  // never transitive connectivity, and retain an existing central vertex.
  std::vector<std::size_t> cluster;
  for (std::size_t seed = 0; seed < document.vertices.size(); ++seed) {
    if (targets[seed] != invalid_index) {
      continue;
    }
    cluster.clear();
    index.visit(document.vertices[seed].position, tolerance,
                [&](std::size_t candidate, double) {
                  if (targets[candidate] == invalid_index) {
                    cluster.push_back(candidate);
                  }
                });
    const std::size_t survivor = cluster_survivor(document, cluster);
    for (const std::size_t candidate : cluster) {
      targets[candidate] = survivor;
    }
  }

  std::map<ElementId, ElementId> result;
  for (std::size_t source = 0; source < targets.size(); ++source) {
    if (targets[source] != source) {
      result.emplace(document.vertices[source].id,
                     document.vertices[targets[source]].id);
    }
  }
  return result;
}

OperationElementDelta elements_in(const Document &document) {
  OperationElementDelta result;
  for (const Vertex &vertex : document.vertices) {
    result.vertices.push_back(vertex.id);
  }
  for (const Face &face : document.faces) {
    result.faces.push_back(face.id);
  }
  result.edges = document_edges(document);
  return result;
}

OperationElementDelta difference(OperationElementDelta left,
                                  const OperationElementDelta &right) {
  const std::set<ElementId> right_vertices(right.vertices.begin(), right.vertices.end());
  const std::set<ElementId> right_faces(right.faces.begin(), right.faces.end());
  std::set<std::pair<ElementId, ElementId>> right_edges;
  for (const Edge edge : right.edges) {
    right_edges.emplace(edge.a, edge.b);
  }
  std::erase_if(left.vertices, [&](ElementId id) { return right_vertices.contains(id); });
  std::erase_if(left.faces, [&](ElementId id) { return right_faces.contains(id); });
  std::erase_if(left.edges, [&](Edge edge) { return right_edges.contains({edge.a, edge.b}); });
  return left;
}

} // namespace

OperationResult auto_merge_selected_vertices(Document &document,
                                              Selection &selection,
                                              float tolerance) {
  if (!std::isfinite(tolerance) || tolerance < 0.0F) {
    return OperationResult::failure("Auto Merge needs a finite non-negative distance.");
  }
  const std::vector<ElementId> selected_ids = selected_vertex_ids(document, selection);
  if (selected_ids.empty() || document.vertices.size() < 2U) {
    return {};
  }
  for (const Vertex &vertex : document.vertices) {
    if (!std::isfinite(vertex.position.x) || !std::isfinite(vertex.position.y) ||
        !std::isfinite(vertex.position.z)) {
      return OperationResult::failure("Auto Merge requires finite vertex positions.");
    }
  }
  const std::set<ElementId> selected(selected_ids.begin(), selected_ids.end());
  const std::map<ElementId, ElementId> remap = merge_targets(document, selected, tolerance);
  if (remap.empty()) {
    return {};
  }

  std::map<ElementId, std::set<ElementId>> clusters;
  for (const auto &[source, target] : remap) {
    clusters[target].insert(source);
  }
  Document candidate = document;
  ElementId preferred_active_face = selection.active_face;
  std::map<ElementId, std::set<ElementId>> descendant_sources;
  for (const Face &face : document.faces) {
    descendant_sources[face.id].insert(face.id);
  }
  for (const auto &[target, sources] : clusters) {
    const Vertex *survivor = find_vertex(candidate, target);
    if (survivor == nullptr) {
      return OperationResult::failure("Auto Merge could not retain its target vertex.");
    }
    Document next;
    std::map<ElementId, ElementId> created_face_sources;
    std::map<ElementId, ElementId> merged_face_ids;
    const OperationResult result = document_internal::build_topology_vertex_merge_candidate(
        candidate, next, sources, target, survivor->position, true,
        &created_face_sources, &merged_face_ids);
    if (!result.success || !result.changed) {
      return OperationResult::failure(result.message.empty()
          ? "Auto Merge would create invalid geometry." : result.message);
    }
    for (const auto &[created, source] : created_face_sources) {
      const auto ancestor = descendant_sources.find(source);
      descendant_sources.emplace(created, ancestor == descendant_sources.end()
          ? std::set<ElementId>{source} : ancestor->second);
    }
    for (const auto &[removed, survivor] : merged_face_ids) {
      ElementId final_survivor = survivor;
      for (;;) {
        const auto next_survivor = merged_face_ids.find(final_survivor);
        if (next_survivor == merged_face_ids.end()) {
          break;
        }
        final_survivor = next_survivor->second;
      }
      const auto source = descendant_sources.find(removed);
      if (source != descendant_sources.end()) {
        auto &ancestors = descendant_sources[final_survivor];
        ancestors.insert(source->second.begin(), source->second.end());
      }
    }
    for (;;) {
      const auto survivor = merged_face_ids.find(preferred_active_face);
      if (survivor == merged_face_ids.end()) {
        break;
      }
      preferred_active_face = survivor->second;
    }
    candidate = std::move(next);
  }

  candidate.hard_normal_edges.clear();
  for (Edge edge : document.hard_normal_edges) {
    if (const auto found = remap.find(edge.a); found != remap.end()) {
      edge.a = found->second;
    }
    if (const auto found = remap.find(edge.b); found != remap.end()) {
      edge.b = found->second;
    }
    edge = make_edge(edge.a, edge.b);
    if (edge.a != edge.b && edge_exists(candidate, edge)) {
      add_unique_edge(candidate.hard_normal_edges, edge);
    }
  }
  // A weld changes vertex identity without changing the authored corner UV.
  // Keep projection-based repair only for faces actually split or collapsed.
  std::map<ElementId, const Face *> source_faces;
  for (const Face &face : document.faces) {
    source_faces.emplace(face.id, &face);
  }
  for (Face &face : candidate.faces) {
    const auto source = source_faces.find(face.id);
    if (source == source_faces.end() ||
        source->second->vertices.size() != face.vertices.size()) {
      continue;
    }
    const Face &original = *source->second;
    bool same_corners = true;
    for (std::size_t corner = 0; corner < face.vertices.size(); ++corner) {
      const auto mapped = remap.find(original.vertices[corner]);
      const ElementId expected = mapped == remap.end()
          ? original.vertices[corner] : mapped->second;
      same_corners = same_corners && face.vertices[corner] == expected;
    }
    if (same_corners) {
      face.uvs = original.uvs;
    }
  }
  Selection remapped_selection;
  const auto validation = validate_polygon_document(
      candidate, PolygonDocumentValidationPolicy::AuthoredTopology);
  const bool malformed = std::ranges::any_of(validation.diagnostics,
      [](const PolygonDocumentDiagnostic &diagnostic) {
        return diagnostic.severity == PolygonDocumentDiagnosticSeverity::Error &&
               diagnostic.code != PolygonDocumentDiagnosticCode::NonmanifoldEdge;
      });
  if (malformed) {
    return OperationResult::failure("Auto Merge would create invalid authored topology.");
  }
  document_internal::restore_remapped_selection(candidate, selection, remapped_selection, remap);
  const std::set<ElementId> selected_faces(selection.faces.begin(), selection.faces.end());
  for (const Face &face : candidate.faces) {
    const auto source = descendant_sources.find(face.id);
    if (source != descendant_sources.end() && std::ranges::any_of(
          source->second, [&](ElementId id) { return selected_faces.contains(id); })) {
      add_unique_id(remapped_selection.faces, face.id);
    }
  }
  if (selection.has_active && selection.active_kind == ElementKind::Face &&
      std::ranges::find(remapped_selection.faces, preferred_active_face) !=
          remapped_selection.faces.end()) {
    document_internal::activate_face_selection(remapped_selection, preferred_active_face);
  }
  if (!remapped_selection.has_active) {
    document_internal::activate_last_selection(remapped_selection);
  }
  OperationResult result;
  result.changed = true;
  const OperationElementDelta before = elements_in(document);
  const OperationElementDelta after = elements_in(candidate);
  result.deleted = difference(before, after);
  result.created = difference(after, before);
  for (const auto &[source, target] : remap) {
    add_unique_id(result.affected.vertices, target);
    for (const Face &face : candidate.faces) {
      if (document_internal::face_uses_vertex(face, target)) {
        add_unique_id(result.affected.faces, face.id);
      }
    }
    static_cast<void>(source);
  }
  document = std::move(candidate);
  selection = std::move(remapped_selection);
  return result;
}

} // namespace quader_poly
