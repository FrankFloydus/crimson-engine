////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <quader/modeling/neutral/polygon/document_validation.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_topology_helpers.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_uv_helpers.hpp>
#include <quader/modeling/neutral/polygon/poly_operation.hpp>
#include <quader/modeling/neutral/polygon/poly_operation_descriptor.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace quader_poly {

namespace {

using VertexExtrudeEdgeKey = std::pair<ElementId, ElementId>;

struct VertexExtrudeEdgeUse {
  ElementId from = kInvalidElementId;
  ElementId to = kInvalidElementId;
  ElementId face = kInvalidElementId;
};

bool vertex_extrude_finite(quader::QVec3 value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

ElementId append_vertex_extrude_vertex(Document &document,
                                       quader::QVec3 position) {
  if (!vertex_extrude_finite(position) ||
      document.next_vertex_id == kInvalidElementId ||
      document.next_vertex_id == std::numeric_limits<ElementId>::max()) {
    return kInvalidElementId;
  }
  return add_vertex(document, position);
}

ElementId allocate_vertex_extrude_face(Document &document) {
  if (document.next_face_id == kInvalidElementId ||
      document.next_face_id == std::numeric_limits<ElementId>::max()) {
    return kInvalidElementId;
  }
  return document.next_face_id++;
}

quader::QVec3 vertex_extrude_position(const Transform3 &transform,
                                      quader::QVec3 pivot,
                                      quader::QVec3 position) {
  const double x = static_cast<double>(position.x) - pivot.x;
  const double y = static_cast<double>(position.y) - pivot.y;
  const double z = static_cast<double>(position.z) - pivot.z;
  return {static_cast<float>(pivot.x + static_cast<double>(transform.origin.x) +
                             transform.x_axis.x * x + transform.y_axis.x * y +
                             transform.z_axis.x * z),
          static_cast<float>(pivot.y + static_cast<double>(transform.origin.y) +
                             transform.x_axis.y * x + transform.y_axis.y * y +
                             transform.z_axis.y * z),
          static_cast<float>(pivot.z + static_cast<double>(transform.origin.z) +
                             transform.x_axis.z * x + transform.y_axis.z * y +
                             transform.z_axis.z * z)};
}

bool vertex_extrude_identity(const Transform3 &transform) {
  return transform.x_axis.x == 1.0F && transform.x_axis.y == 0.0F &&
         transform.x_axis.z == 0.0F && transform.y_axis.x == 0.0F &&
         transform.y_axis.y == 1.0F && transform.y_axis.z == 0.0F &&
         transform.z_axis.x == 0.0F && transform.z_axis.y == 0.0F &&
         transform.z_axis.z == 1.0F && transform.origin.x == 0.0F &&
         transform.origin.y == 0.0F && transform.origin.z == 0.0F;
}

bool build_vertex_extrude_fan(Document &candidate, ElementId apex,
                              float setback, bool &changed) {
  using namespace document_internal;
  std::set<ElementId> neighbors;
  for (const Face &face : candidate.faces) {
    const auto found = std::ranges::find(face.vertices, apex);
    if (found == face.vertices.end()) {
      continue;
    }
    const auto index = static_cast<std::size_t>(found - face.vertices.begin());
    neighbors.insert(face.vertices[(index + 1U) % face.vertices.size()]);
    neighbors.insert(face.vertices[(index + face.vertices.size() - 1U) %
                                   face.vertices.size()]);
  }
  const quader::QVec3 origin = find_vertex(candidate, apex)->position;
  std::map<ElementId, ElementId> supports;
  std::vector<EdgeNormalModeAssignment> normal_modes;
  for (const ElementId neighbor : neighbors) {
    const quader::QVec3 endpoint = find_vertex(candidate, neighbor)->position;
    const double x = static_cast<double>(endpoint.x) - origin.x;
    const double y = static_cast<double>(endpoint.y) - origin.y;
    const double z = static_cast<double>(endpoint.z) - origin.z;
    const double distance = std::hypot(x, y, z);
    constexpr double kMinimumSpokeLength = 0.000001;
    if (distance <= kMinimumSpokeLength || setback >= distance) {
      supports.emplace(neighbor, neighbor);
      continue;
    }
    const double fraction = setback / distance;
    const ElementId support = append_vertex_extrude_vertex(
        candidate, {static_cast<float>(origin.x + fraction * x),
                    static_cast<float>(origin.y + fraction * y),
                    static_cast<float>(origin.z + fraction * z)});
    if (support == kInvalidElementId) {
      return false;
    }
    supports.emplace(neighbor, support);
    const auto mode = edge_normal_mode(candidate, make_edge(apex, neighbor));
    normal_modes.push_back({make_edge(apex, support), mode});
    normal_modes.push_back({make_edge(support, neighbor), mode});
    changed = true;
  }

  std::vector<Face> faces;
  faces.reserve(candidate.faces.size() + neighbors.size());
  for (const Face &source : candidate.faces) {
    const auto found = std::ranges::find(source.vertices, apex);
    if (found == source.vertices.end()) {
      faces.push_back(source);
      continue;
    }
    const auto index =
        static_cast<std::size_t>(found - source.vertices.begin());
    const std::size_t count = source.vertices.size();
    const ElementId previous = source.vertices[(index + count - 1U) % count];
    const ElementId next = source.vertices[(index + 1U) % count];
    const ElementId previous_support = supports.at(previous);
    const ElementId next_support = supports.at(next);

    Face outside = source;
    outside.vertices = {next_support};
    for (std::size_t offset = 1; offset < count; ++offset) {
      const ElementId vertex = source.vertices[(index + offset) % count];
      if (outside.vertices.back() != vertex) {
        outside.vertices.push_back(vertex);
      }
    }
    if (outside.vertices.back() != previous_support) {
      outside.vertices.push_back(previous_support);
    }
    if (outside.vertices.front() == outside.vertices.back()) {
      outside.vertices.pop_back();
    }
    if (outside.vertices.size() < 3U) {
      faces.push_back(source);
      continue;
    }

    Face tip = source;
    tip.id = allocate_vertex_extrude_face(candidate);
    tip.vertices = {previous_support, apex, next_support};
    if (tip.id == kInvalidElementId ||
        !preserve_face_uvs(candidate, source, candidate, outside) ||
        !preserve_face_uvs(candidate, source, candidate, tip)) {
      return false;
    }
    faces.push_back(std::move(outside));
    faces.push_back(std::move(tip));
    changed = true;
  }
  candidate.faces = std::move(faces);
  prune_normal_edge_states(candidate);
  static_cast<void>(set_edge_normal_modes(candidate, normal_modes));
  return true;
}

bool append_vertex_extrude_patch(Document &candidate, const Document &source,
                                 const Face &donor,
                                 std::vector<ElementId> vertices) {
  Face patch = donor;
  patch.id = allocate_vertex_extrude_face(candidate);
  patch.vertices = std::move(vertices);
  if (patch.id == kInvalidElementId ||
      !document_internal::preserve_face_uvs(source, donor, candidate, patch)) {
    return false;
  }
  candidate.faces.push_back(std::move(patch));
  return true;
}

bool vertex_extrude_face_changed(const Face &source, const Face &candidate) {
  return source.vertices != candidate.vertices ||
         source.material_slot != candidate.material_slot ||
         source.normal_shading != candidate.normal_shading ||
         source.texture_transform != candidate.texture_transform ||
         !std::ranges::equal(source.uvs, candidate.uvs,
                             [](quader::QVec2 left, quader::QVec2 right) {
                               return left.x == right.x && left.y == right.y;
                             });
}

OperationResult vertex_extrude_receipt(const Document &source,
                                       const Document &candidate) {
  OperationResult result;
  result.changed = true;
  const document_internal::VertexLookupIndex source_vertices(source);
  std::set<ElementId> affected_vertices;
  for (const Vertex &vertex : candidate.vertices) {
    const Vertex *before = source_vertices.find(vertex.id);
    if (before == nullptr) {
      result.created.vertices.push_back(vertex.id);
    } else if (before->position.x != vertex.position.x ||
               before->position.y != vertex.position.y ||
               before->position.z != vertex.position.z) {
      affected_vertices.insert(vertex.id);
    }
  }

  std::map<ElementId, const Face *> source_faces;
  for (const Face &face : source.faces) {
    source_faces.emplace(face.id, &face);
  }
  for (const Face &face : candidate.faces) {
    const auto before = source_faces.find(face.id);
    const bool created = before == source_faces.end();
    const bool face_changed =
        created || vertex_extrude_face_changed(*before->second, face);
    if (created) {
      result.created.faces.push_back(face.id);
    }
    if (face_changed) {
      for (const ElementId vertex : face.vertices) {
        if (source_vertices.find(vertex) != nullptr) {
          affected_vertices.insert(vertex);
        }
      }
    }
  }
  result.affected.vertices.assign(affected_vertices.begin(),
                                  affected_vertices.end());
  for (const Face &face : candidate.faces) {
    const auto before = source_faces.find(face.id);
    if (before != source_faces.end() &&
        (vertex_extrude_face_changed(*before->second, face) ||
         std::ranges::any_of(face.vertices, [&](ElementId vertex) {
           return affected_vertices.contains(vertex);
         }))) {
      result.affected.faces.push_back(face.id);
    }
  }

  std::set<VertexExtrudeEdgeKey> source_edges;
  for (const Edge edge : document_edges(source)) {
    source_edges.emplace(edge.a, edge.b);
  }
  for (const Edge edge : document_edges(candidate)) {
    if (source_edges.erase({edge.a, edge.b}) == 0U) {
      result.created.edges.push_back(edge);
    } else if (affected_vertices.contains(edge.a) ||
               affected_vertices.contains(edge.b)) {
      result.affected.edges.push_back(edge);
    }
  }
  for (const auto &[from, to] : source_edges) {
    result.deleted.edges.push_back(make_edge(from, to));
  }
  return result;
}

} // namespace

namespace document_internal {

OperationResult extrude_selected_vertices_impl(Document &document,
                                               Selection &selection,
                                               const Transform3 &transform,
                                               quader::QVec3 pivot,
                                               float setback) {
  if (selection.mode != SelectionMode::Vertex || selection.vertices.empty()) {
    return OperationResult::failure("Select one or more vertices to extrude.");
  }
  if (!std::isfinite(setback) || setback <= 0.0F ||
      !vertex_extrude_finite(pivot) ||
      !vertex_extrude_finite(transform.x_axis) ||
      !vertex_extrude_finite(transform.y_axis) ||
      !vertex_extrude_finite(transform.z_axis) ||
      !vertex_extrude_finite(transform.origin)) {
    return OperationResult::failure(
        "Vertex extrusion requires finite transforms and a positive setback.");
  }
  if (!validate_polygon_document(
           document, PolygonDocumentValidationPolicy::AuthoredTopology)
           .ok() ||
      std::ranges::any_of(document.vertices,
                          [](const Vertex &vertex) {
                            return vertex.id == kInvalidElementId ||
                                   !vertex_extrude_finite(vertex.position);
                          }) ||
      std::ranges::any_of(document.faces, [](const Face &face) {
        return face.id == kInvalidElementId;
      })) {
    return OperationResult::failure(
        "Vertex extrusion requires valid authored topology.");
  }
  std::vector<ElementId> selected;
  std::set<ElementId> selected_ids;
  for (const ElementId vertex : selection.vertices) {
    if (find_vertex(document, vertex) == nullptr) {
      return OperationResult::failure(
          "A selected extrusion vertex is missing.");
    }
    if (selected_ids.insert(vertex).second) {
      selected.push_back(vertex);
    }
  }
  if (selection.has_active &&
      (selection.active_kind != ElementKind::Vertex ||
       !selected_ids.contains(selection.active_vertex))) {
    return OperationResult::failure(
        "The active extrusion vertex is not selected.");
  }
  if (vertex_extrude_identity(transform)) {
    return {false, {}};
  }

  std::map<VertexExtrudeEdgeKey, std::vector<VertexExtrudeEdgeUse>> edge_uses;
  for (const Face &face : document.faces) {
    for (std::size_t index = 0; index < face.vertices.size(); ++index) {
      const ElementId from = face.vertices[index];
      const ElementId to = face.vertices[(index + 1U) % face.vertices.size()];
      const Edge edge = make_edge(from, to);
      edge_uses[{edge.a, edge.b}].push_back({from, to, face.id});
    }
  }
  std::map<ElementId, std::vector<VertexExtrudeEdgeUse>> boundary_uses;
  for (const auto &[edge, uses] : edge_uses) {
    if (uses.size() == 1U) {
      boundary_uses[edge.first].push_back(uses.front());
      boundary_uses[edge.second].push_back(uses.front());
    }
  }
  std::set<ElementId> boundary_selected;
  for (const ElementId vertex : selected) {
    const auto uses = boundary_uses.find(vertex);
    if (uses != boundary_uses.end() && uses->second.size() == 2U) {
      boundary_selected.insert(vertex);
    } else if (uses != boundary_uses.end()) {
      return OperationResult::failure(
          "Vertex extrusion requires a manifold boundary.");
    }
  }

  Document candidate = document;
  bool changed = false;
  for (const ElementId vertex : selected) {
    if (!boundary_selected.contains(vertex) &&
        !build_vertex_extrude_fan(candidate, vertex, setback, changed)) {
      return OperationResult::failure(
          "Vertex extrusion could not construct a valid support ring.");
    }
  }
  std::map<ElementId, ElementId> clones;
  for (const ElementId vertex : selected) {
    if (!boundary_selected.contains(vertex)) {
      continue;
    }
    const ElementId clone = append_vertex_extrude_vertex(
        candidate, find_vertex(document, vertex)->position);
    if (clone == kInvalidElementId) {
      return OperationResult::failure(
          "Vertex extrusion could not allocate a boundary vertex.");
    }
    clones.emplace(vertex, clone);
  }

  std::set<ElementId> chained;
  std::vector<EdgeNormalModeCopy> normal_copies;
  for (const auto &[edge, uses] : edge_uses) {
    if (uses.size() != 1U || !boundary_selected.contains(edge.first) ||
        !boundary_selected.contains(edge.second)) {
      continue;
    }
    const auto &use = uses.front();
    if (!append_vertex_extrude_patch(
            candidate, document, *find_face(document, use.face),
            {use.to, use.from, clones.at(use.from), clones.at(use.to)})) {
      return OperationResult::failure(
          "Vertex extrusion could not extend the boundary chain.");
    }
    chained.insert(use.from);
    chained.insert(use.to);
    normal_copies.push_back(
        {make_edge(use.from, use.to),
         make_edge(clones.at(use.from), clones.at(use.to))});
    changed = true;
  }
  for (const ElementId vertex : boundary_selected) {
    if (chained.contains(vertex)) {
      continue;
    }
    ElementId previous = kInvalidElementId;
    ElementId next = kInvalidElementId;
    ElementId donor = kInvalidElementId;
    // Boundary directions oppose the incident surface's perimeter winding.
    for (const auto &use : boundary_uses.at(vertex)) {
      if (use.from == vertex) {
        previous = use.to;
        donor = use.face;
      } else {
        next = use.from;
      }
    }
    if (previous == kInvalidElementId || next == kInvalidElementId ||
        !append_vertex_extrude_patch(
            candidate, document, *find_face(document, donor),
            {previous, vertex, next, clones.at(vertex)})) {
      return OperationResult::failure(
          "Vertex extrusion could not extend the boundary corner.");
    }
    normal_copies.push_back(
        {make_edge(vertex, previous), make_edge(clones.at(vertex), previous)});
    normal_copies.push_back(
        {make_edge(vertex, next), make_edge(clones.at(vertex), next)});
    changed = true;
  }
  static_cast<void>(copy_edge_normal_modes(document, candidate, normal_copies));

  Selection result_selection;
  result_selection.mode = SelectionMode::Vertex;
  for (const ElementId vertex : selected) {
    const auto clone = clones.find(vertex);
    const ElementId moved = clone == clones.end() ? vertex : clone->second;
    Vertex *target = find_vertex(candidate, moved);
    const quader::QVec3 position =
        vertex_extrude_position(transform, pivot, target->position);
    if (!vertex_extrude_finite(position)) {
      return OperationResult::failure(
          "Vertex extrusion produced nonfinite coordinates.");
    }
    changed = changed || target->position.x != position.x ||
              target->position.y != position.y ||
              target->position.z != position.z;
    target->position = position;
    result_selection.vertices.push_back(moved);
  }
  if (!validate_polygon_document(
           candidate, PolygonDocumentValidationPolicy::AuthoredTopology)
           .ok()) {
    return OperationResult::failure(
        "Vertex extrusion produced invalid authored topology.");
  }
  if (!changed) {
    return {false, {}};
  }
  if (selection.has_active) {
    const auto clone = clones.find(selection.active_vertex);
    activate_vertex_selection(result_selection, clone == clones.end()
                                                    ? selection.active_vertex
                                                    : clone->second);
  } else {
    activate_last_selection(result_selection);
  }
  OperationResult result = vertex_extrude_receipt(document, candidate);
  document = std::move(candidate);
  selection = std::move(result_selection);
  return result;
}

} // namespace document_internal

/**
 * Implements the Extrude Elements Operation modeling operation for the polygon
 * document and mesh editing core.
 */
class ExtrudeElementsOperation final : public PolyOperation {
public:
  ExtrudeElementsOperation(quader::QVec3 offset,
                           float closed_edge_ledge_size = 0.0F);

  [[nodiscard]] std::string_view id() const override {
    return poly_operation_descriptor(PolyOperationKey::ExtrudeSelectedElements)
        .id;
  }
  [[nodiscard]] std::string_view label() const override {
    return poly_operation_descriptor(PolyOperationKey::ExtrudeSelectedElements)
        .label;
  }
  [[nodiscard]] OperationResult apply(Document &document,
                                      Selection &selection) const override;

private:
  quader::QVec3 offset_;
  float closed_edge_ledge_size_ = 0.0F;
};

/**
 * Extrudes selected elements by applying an affine transform around a pivot.
 */
class TransformExtrudeElementsOperation final : public PolyOperation {
public:
  TransformExtrudeElementsOperation(Transform3 transform, quader::QVec3 pivot,
                                    float vertex_extrude_size);

  [[nodiscard]] std::string_view id() const override {
    return poly_operation_descriptor(PolyOperationKey::ExtrudeSelectedElements)
        .id;
  }
  [[nodiscard]] std::string_view label() const override {
    return poly_operation_descriptor(PolyOperationKey::ExtrudeSelectedElements)
        .label;
  }
  [[nodiscard]] OperationResult apply(Document &document,
                                      Selection &selection) const override;

private:
  Transform3 transform_;
  quader::QVec3 pivot_;
  float vertex_extrude_size_ = 1.0F;
};

namespace document_internal {
[[nodiscard]] OperationResult
extrude_selected_elements_impl(Document &document, Selection &selection,
                               quader::QVec3 offset,
                               float closed_edge_ledge_size);
[[nodiscard]] OperationResult transform_extrude_selected_elements_impl(
    Document &document, Selection &selection, const Transform3 &transform,
    quader::QVec3 pivot, float vertex_extrude_size);
} // namespace document_internal

ExtrudeElementsOperation::ExtrudeElementsOperation(quader::QVec3 offset,
                                                   float closed_edge_ledge_size)
    : offset_(offset), closed_edge_ledge_size_(closed_edge_ledge_size) {}

OperationResult ExtrudeElementsOperation::apply(Document &document,
                                                Selection &selection) const {
  return document_internal::extrude_selected_elements_impl(
      document, selection, offset_, closed_edge_ledge_size_);
}

TransformExtrudeElementsOperation::TransformExtrudeElementsOperation(
    Transform3 transform, quader::QVec3 pivot, float vertex_extrude_size)
    : transform_(transform), pivot_(pivot),
      vertex_extrude_size_(vertex_extrude_size) {}

OperationResult
TransformExtrudeElementsOperation::apply(Document &document,
                                         Selection &selection) const {
  return document_internal::transform_extrude_selected_elements_impl(
      document, selection, transform_, pivot_, vertex_extrude_size_);
}

OperationResult extrude_selected_elements(Document &document,
                                          Selection &selection,
                                          quader::QVec3 offset,
                                          float closed_edge_ledge_size) {
  return ExtrudeElementsOperation(offset, closed_edge_ledge_size)
      .apply(document, selection);
}

OperationResult transform_extrude_selected_elements(Document &document,
                                                    Selection &selection,
                                                    const Transform3 &transform,
                                                    quader::QVec3 pivot,
                                                    float vertex_extrude_size) {
  return TransformExtrudeElementsOperation(transform, pivot,
                                           vertex_extrude_size)
      .apply(document, selection);
}

} // namespace quader_poly
