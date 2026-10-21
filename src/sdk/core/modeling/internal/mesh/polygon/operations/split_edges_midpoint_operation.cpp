////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <quader/modeling/neutral/polygon/poly_operation.hpp>
#include <quader/modeling/neutral/polygon/poly_operation_descriptor.hpp>

#include <quader/modeling/neutral/polygon/internal/quader_poly_document_bridge_surface_helpers.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_hull_plane_helpers.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_topology_helpers.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_uv_helpers.hpp>

#include <algorithm>
#include <cstddef>
#include <map>
#include <numeric>
#include <set>
#include <utility>
#include <vector>

namespace quader_poly {
namespace {

using EdgeKey = std::pair<ElementId, ElementId>;

class SplitEdgesMidpointOperation final : public PolyOperation {
public:
  [[nodiscard]] std::string_view id() const override {
    return poly_operation_descriptor(PolyOperationKey::SplitSelectedEdgesMidpoint).id;
  }
  [[nodiscard]] std::string_view label() const override {
    return poly_operation_descriptor(PolyOperationKey::SplitSelectedEdgesMidpoint).label;
  }
  [[nodiscard]] OperationResult apply(Document &document, Selection &selection) const override;
};

OperationResult SplitEdgesMidpointOperation::apply(Document &document,
                                                 Selection &selection) const {
  using namespace document_internal;
  if (selection.mode != SelectionMode::Edge || selection.edges.empty())
    return OperationResult::failure("Select one or more edges to split at their midpoints.");
  const auto source_validation = validate_authored_operation_candidate(document, document);
  if (!source_validation.success) return source_validation;

  const auto incidence = face_indices_by_edge(document);
  std::set<EdgeKey> selected;
  std::set<std::size_t> affected_faces;
  for (const auto edge : selection.edges) {
    const auto normalized = make_edge(edge.a, edge.b);
    const EdgeKey key{normalized.a, normalized.b};
    if (normalized.a == normalized.b || !incidence.contains(key))
      return OperationResult::failure("Edge Split needs valid edges belonging to mesh faces.");
    selected.insert(key);
    const auto &faces = incidence.at(key);
    affected_faces.insert(faces.begin(), faces.end());
  }

  Document candidate = document;
  OperationResult result;
  std::map<EdgeKey, ElementId> midpoints;
  std::vector<EdgeNormalModeCopy> normal_copies;
  std::vector<Edge> halves;
  for (const auto &[first, second] : selected) {
    const auto *a = find_vertex(document, first);
    const auto *b = find_vertex(document, second);
    if (a == nullptr || b == nullptr)
      return OperationResult::failure("Edge Split could not resolve a selected edge endpoint.");
    const quader::QVec3 position{std::midpoint(a->position.x, b->position.x),
        std::midpoint(a->position.y, b->position.y),
        std::midpoint(a->position.z, b->position.z)};
    const auto same_position = [position](quader::QVec3 endpoint) {
      return position.x == endpoint.x && position.y == endpoint.y && position.z == endpoint.z;
    };
    if (same_position(a->position) || same_position(b->position))
      return OperationResult::failure("Edge Split needs an edge with a representable interior midpoint.");
    const auto midpoint = add_vertex(candidate, position);
    midpoints.emplace(EdgeKey{first, second}, midpoint);
    result.created.vertices.push_back(midpoint);
    const auto source = make_edge(first, second);
    result.deleted.edges.push_back(source);
    for (const auto half : {make_edge(first, midpoint), make_edge(midpoint, second)}) {
      halves.push_back(half);
      result.created.edges.push_back(half);
      normal_copies.push_back({source, half});
    }
  }

  for (const auto index : affected_faces) {
    const auto &source = document.faces[index];
    std::vector<ElementId> loop;
    loop.reserve(source.vertices.size() * 2u);
    for (std::size_t corner = 0u; corner < source.vertices.size(); ++corner) {
      const auto first = source.vertices[corner];
      const auto second = source.vertices[(corner + 1u) % source.vertices.size()];
      loop.push_back(first);
      const auto edge = make_edge(first, second);
      if (const auto found = midpoints.find({edge.a, edge.b}); found != midpoints.end()) {
        loop.push_back(found->second);
      }
    }
    auto &face = candidate.faces[index];
    face.vertices = std::move(loop);
    if (!preserve_face_uvs(document, source, candidate, face))
      return OperationResult::failure("Edge Split encountered invalid face texture data.");
    result.affected.faces.push_back(face.id);
  }
  static_cast<void>(copy_edge_normal_modes(document, candidate, normal_copies));
  prune_normal_edge_states(candidate);
  const auto validation = validate_authored_operation_candidate(document, candidate);
  if (!validation.success) return validation;

  Selection next;
  next.mode = SelectionMode::Edge;
  next.edges = std::move(halves);
  activate_last_selection(next);
  document = std::move(candidate);
  selection = std::move(next);
  result.changed = true;
  return result;
}

} // namespace

OperationResult split_selected_edges_midpoint(Document &document, Selection &selection) {
  return SplitEdgesMidpointOperation().apply(document, selection);
}

} // namespace quader_poly
