////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <mesh/polygon/poly_operation.hpp>
#include <mesh/polygon/poly_operation_descriptor.hpp>

#include <mesh/polygon/internal/quader_poly_document_hull_plane_helpers.hpp>
#include <mesh/polygon/internal/quader_poly_document_knife_helpers.hpp>
#include <mesh/polygon/internal/quader_poly_document_topology_helpers.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace quader_poly {

using namespace document_internal;

namespace {

/**
 * Implements exact-position double vertex cleanup for the polygon document.
 */
class RemoveDoubleVerticesOperation final : public PolyOperation {
public:
  [[nodiscard]] std::string_view id() const override {
    return poly_operation_descriptor(PolyOperationKey::RemoveDoubleVertices).id;
  }

  [[nodiscard]] std::string_view label() const override {
    return poly_operation_descriptor(PolyOperationKey::RemoveDoubleVertices)
        .label;
  }

  [[nodiscard]] OperationResult apply(Document &document,
                                      Selection &selection) const override;
};

using PositionKey = std::tuple<float, float, float>;

PositionKey position_key(quader::QVec3 position) {
  return {position.x, position.y, position.z};
}

std::vector<ElementId> first_exact_duplicate_cluster(const Document &document) {
  std::map<PositionKey, std::vector<ElementId>> vertices_by_position;
  for (const Vertex &vertex : document.vertices) {
    if (!std::isfinite(vertex.position.x) ||
        !std::isfinite(vertex.position.y) ||
        !std::isfinite(vertex.position.z)) {
      continue;
    }
    vertices_by_position[position_key(vertex.position)].push_back(vertex.id);
  }

  for (auto &[_, vertex_ids] : vertices_by_position) {
    if (vertex_ids.size() < 2U) {
      continue;
    }
    std::ranges::sort(vertex_ids);
    return vertex_ids;
  }
  return {};
}

ElementId
remap_vertex_id(ElementId vertex_id,
                const std::map<ElementId, ElementId> &merged_vertex_ids) {
  const auto found = merged_vertex_ids.find(vertex_id);
  return found == merged_vertex_ids.end() ? vertex_id : found->second;
}

Edge remap_edge(Edge edge,
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

std::string removed_vertices_message(std::size_t removed_count) {
  if (removed_count == 0U) {
    return "No double vertices were found.";
  }
  if (removed_count == 1U) {
    return "Removed 1 vertex.";
  }
  return "Removed " + std::to_string(removed_count) + " vertices.";
}

OperationResult
RemoveDoubleVerticesOperation::apply(Document &document,
                                     Selection &selection) const {
  if (document.vertices.size() < 2U) {
    return {false, removed_vertices_message(0U)};
  }

  Document candidate = document;
  const Selection source_selection = selection;
  std::map<ElementId, ElementId> merged_vertex_ids;
  std::size_t removed_count = 0U;

  for (;;) {
    const std::vector<ElementId> cluster =
        first_exact_duplicate_cluster(candidate);
    if (cluster.size() < 2U) {
      break;
    }

    ElementId survivor_id = cluster.front();
    const Vertex *survivor = find_vertex(candidate, survivor_id);
    if (survivor == nullptr) {
      return {false, "Remove Doubles could not find a survivor vertex."};
    }

    std::set<ElementId> merge_vertex_ids;
    for (const ElementId vertex_id : cluster) {
      if (vertex_id != survivor_id &&
          find_vertex(candidate, vertex_id) != nullptr) {
        merge_vertex_ids.insert(vertex_id);
      }
    }
    if (merge_vertex_ids.empty()) {
      break;
    }

    Document next_candidate;
    const std::size_t before_vertices = candidate.vertices.size();
    const OperationResult merge_result = build_vertex_merge_candidate(
        candidate, next_candidate, merge_vertex_ids, survivor_id,
        survivor->position);
    if (!merge_result.changed) {
      return merge_result.message.empty()
                 ? OperationResult{false, "Remove Doubles would create invalid "
                                          "geometry."}
                 : merge_result;
    }

    if (!every_face_triangulates(next_candidate)) {
      return {false, "Remove Doubles would create invalid face geometry."};
    }

    for (const ElementId removed_id : merge_vertex_ids) {
      merged_vertex_ids[removed_id] = survivor_id;
    }
    removed_count += before_vertices - next_candidate.vertices.size();
    candidate = std::move(next_candidate);
  }

  if (removed_count == 0U) {
    return {false, removed_vertices_message(0U)};
  }

  document = std::move(candidate);
  restore_remapped_selection(document, source_selection, selection,
                             merged_vertex_ids);
  return {true, removed_vertices_message(removed_count)};
}

} // namespace

OperationResult remove_double_vertices(Document &document,
                                       Selection &selection) {
  return RemoveDoubleVerticesOperation().apply(document, selection);
}

} // namespace quader_poly
