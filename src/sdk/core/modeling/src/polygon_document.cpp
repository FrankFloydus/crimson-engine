////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <quader/modeling/mesh/polygon_document.hpp>

#include "polygon_document_native.hpp"

#include <diagnostics/profile.hpp>
#include <quader/modeling/neutral/polygon/document_builder.hpp>
#include <quader/modeling/neutral/polygon/document_mesh_compilation.hpp>
#include <quader/modeling/neutral/polygon/document_operations.hpp>
#include <quader/modeling/neutral/polygon/document_read_snapshot.hpp>
#include <quader/modeling/neutral/polygon/document_selection.hpp>
#include <quader/modeling/neutral/polygon/document_topology.hpp>
#include <quader/modeling/neutral/polygon/document_texture.hpp>
#include <quader/modeling/neutral/polygon/document_validation.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_bridge_surface_helpers.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_mesh_internal.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_uv_helpers.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <map>
#include <mutex>
#include <numeric>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace quader::modeling {

namespace {

[[nodiscard]] PolygonContentIdentity next_polygon_content_identity() noexcept {
  static std::atomic<std::uint64_t> next_identity{1U};
  return {next_identity.fetch_add(1U, std::memory_order_relaxed)};
}

} // namespace

/** Counts work across immutable candidates produced by one prepared gesture. */
struct PreparedSelectionTransformWorkState {
  std::atomic<std::uint64_t> coordinate_publication_count{0U};
  std::atomic<std::uint64_t> evaluated_vertex_count{0U};
  std::atomic<std::uint64_t> topology_materialization_count{0U};
  std::atomic<std::uint64_t> coordinate_snapshot_count{0U};
};

/**
 * Owns one immutable authored publication independently of its materialized
 * storage. Coordinate candidates pin a flat source, never another candidate.
 */
struct PolygonDocumentPublication {
  explicit PolygonDocumentPublication(
      std::shared_ptr<const quader_poly::Document> source_document,
      std::shared_ptr<const std::vector<std::size_t>> selected_indices = {},
      std::shared_ptr<const std::vector<VertexPositionUpdate>> selected_positions = {},
      std::shared_ptr<PreparedSelectionTransformWorkState> statistics = {},
      PolygonContentIdentity retained_topology_identity = {})
      : source(std::move(source_document)),
        vertex_indices(std::move(selected_indices)),
        positions(std::move(selected_positions)), work_state(std::move(statistics)),
        identity(next_polygon_content_identity()),
        topology_identity(retained_topology_identity.valid()
                              ? retained_topology_identity : identity) {}

  std::shared_ptr<const quader_poly::Document> source;
  std::shared_ptr<const std::vector<std::size_t>> vertex_indices;
  std::shared_ptr<const std::vector<VertexPositionUpdate>> positions;
  std::shared_ptr<PreparedSelectionTransformWorkState> work_state;
  PolygonContentIdentity identity;
  PolygonContentIdentity topology_identity;
  mutable std::once_flag coordinate_snapshot_once;
  mutable std::shared_ptr<const std::vector<quader_poly::Vertex>> coordinate_vertices;
  mutable std::once_flag materialization_once;
  mutable std::shared_ptr<const quader_poly::Document> materialized;
};

PolygonDocumentStorage::PolygonDocumentStorage()
    : PolygonDocumentStorage(std::make_shared<const quader_poly::Document>()) {}

PolygonDocumentStorage::PolygonDocumentStorage(
    std::shared_ptr<const quader_poly::Document> document)
    : publication_(std::make_shared<const PolygonDocumentPublication>(
          std::move(document))) {}

PolygonDocumentStorage::PolygonDocumentStorage(
    std::shared_ptr<const PolygonDocumentPublication> publication)
    : publication_(std::move(publication)) {}

PolygonDocumentStorage &PolygonDocumentStorage::operator=(
    std::shared_ptr<const quader_poly::Document> document) {
  publication_ = std::make_shared<const PolygonDocumentPublication>(
      std::move(document));
  return *this;
}

const quader_poly::Document &PolygonDocumentStorage::read_document() const {
  if (!publication_->positions) {
    return *publication_->source;
  }
  std::call_once(publication_->materialization_once, [this] {
    QDR_PROFILE_SCOPE("PolygonDocument materialize coordinate publication");
    auto materialized =
        std::make_shared<quader_poly::Document>(*publication_->source);
    for (std::size_t index = 0U; index < publication_->vertex_indices->size();
         ++index) {
      const auto &position = (*publication_->positions)[index].position;
      materialized->vertices[(*publication_->vertex_indices)[index]].position =
          {position.x, position.y, position.z};
    }
    publication_->materialized = std::move(materialized);
    if (publication_->work_state) {
      publication_->work_state->topology_materialization_count.fetch_add(
          1U, std::memory_order_relaxed);
    }
  });
  return *publication_->materialized;
}

std::shared_ptr<const quader_poly::Document>
PolygonDocumentStorage::materialized_document() const {
  (void)read_document();
  return publication_->positions ? publication_->materialized
                                 : publication_->source;
}

const quader_poly::Document &PolygonDocumentStorage::operator*() const {
  return read_document();
}

const quader_poly::Document *PolygonDocumentStorage::operator->() const {
  return &read_document();
}

PolygonContentIdentity PolygonDocumentStorage::identity() const noexcept {
  return publication_->identity;
}

PolygonContentIdentity PolygonDocumentStorage::topology_identity() const noexcept {
  return publication_->topology_identity;
}

PolygonDocumentReadSnapshot PolygonDocumentStorage::read_snapshot() const {
  if (publication_->positions) {
    std::call_once(publication_->coordinate_snapshot_once, [this] {
      QDR_PROFILE_SCOPE("PolygonDocument publish coordinate snapshot");
      auto vertices = std::make_shared<std::vector<quader_poly::Vertex>>(
          publication_->source->vertices);
      for (std::size_t index = 0U; index < publication_->vertex_indices->size();
           ++index) {
        const auto &position = (*publication_->positions)[index].position;
        (*vertices)[(*publication_->vertex_indices)[index]].position =
            {position.x, position.y, position.z};
      }
      publication_->coordinate_vertices = std::move(vertices);
      if (publication_->work_state) {
        publication_->work_state->coordinate_snapshot_count.fetch_add(
            1U, std::memory_order_relaxed);
      }
    });
    return {publication_->source, publication_->coordinate_vertices,
            publication_->identity, publication_->topology_identity};
  }
  return {publication_->source, {}, publication_->identity,
          publication_->topology_identity};
}

PolygonDocumentReadSnapshot
polygon_document_read_snapshot(const PolygonDocument &document) {
  return document.impl_->document.read_snapshot();
}

std::size_t PolygonDocumentStorage::vertex_count() const noexcept {
  return publication_->source->vertices.size();
}

std::size_t PolygonDocumentStorage::face_count() const noexcept {
  return publication_->source->faces.size();
}

quader_poly::ElementId PolygonDocumentStorage::next_vertex_id() const noexcept {
  return publication_->source->next_vertex_id;
}

quader_poly::ElementId PolygonDocumentStorage::next_face_id() const noexcept {
  return publication_->source->next_face_id;
}

PolygonDocumentStorage PolygonDocumentStorage::with_positions(
    std::shared_ptr<const quader_poly::Document> source,
    std::shared_ptr<const std::vector<std::size_t>> vertex_indices,
    std::shared_ptr<const std::vector<VertexPositionUpdate>> positions,
    std::shared_ptr<PreparedSelectionTransformWorkState> work_state,
    PolygonContentIdentity topology_identity) {
  return PolygonDocumentStorage(std::make_shared<const PolygonDocumentPublication>(
      std::move(source), std::move(vertex_indices), std::move(positions),
      std::move(work_state), topology_identity));
}

/** Owns the source snapshot and immutable neutral extrusion generation. */
struct PreparedFaceExtrusionImpl {
  PolygonDocument source;
  quader_poly::FaceExtrusionPreparation preparation;
};

/** Retains immutable authored state and dense transform targets for a gesture. */
struct PreparedSelectionTransformImpl {
  explicit PreparedSelectionTransformImpl(const PolygonDocument &document)
      : source(document) {}

  PolygonDocument source;
  std::shared_ptr<const quader_poly::Document> source_document;
  PolygonContentIdentity source_topology_identity;
  bool has_circular_interpolation = false;
  std::shared_ptr<const std::vector<std::size_t>> vertex_indices;
  std::shared_ptr<const std::vector<VertexPositionUpdate>> source_positions;
  std::shared_ptr<PreparedSelectionTransformWorkState> work_state =
      std::make_shared<PreparedSelectionTransformWorkState>();
};

namespace {

[[nodiscard]] quader::QVec2 to_native(Vec2 value) {
  return {value.x, value.y};
}

[[nodiscard]] quader::QVec3 to_native(Vec3 value) {
  return {value.x, value.y, value.z};
}

[[nodiscard]] quader_poly::Transform3 to_native(Transform3 value) {
  return {
      .x_axis = to_native(value.x_axis),
      .y_axis = to_native(value.y_axis),
      .z_axis = to_native(value.z_axis),
      .origin = to_native(value.origin),
  };
}

[[nodiscard]] Vec2 from_native(quader::QVec2 value) {
  return {value.x, value.y};
}

[[nodiscard]] Vec3 from_native(quader::QVec3 value) {
  return {value.x, value.y, value.z};
}

[[nodiscard]] VertexId vertex_id(quader_poly::ElementId id) {
  return make_id<VertexTag>(id);
}

[[nodiscard]] FaceId face_id(quader_poly::ElementId id) {
  return make_id<FaceTag>(id);
}

[[nodiscard]] EdgeKey edge_id(quader_poly::Edge edge) {
  return make_edge_key(vertex_id(edge.a), vertex_id(edge.b));
}

[[nodiscard]] MaterialId material_id(std::uint32_t id) {
  return make_id<MaterialTag>(id + 1U);
}

[[nodiscard]] std::uint32_t native_material_slot(MaterialId id) {
  return id.valid() ? id.index - 1U : 0U;
}

[[nodiscard]] quader_poly::SurfaceShading
native_shading(AuthoredSurfaceShading shading) {
  switch (shading) {
  case AuthoredSurfaceShading::Flat:
    return quader_poly::SurfaceShading::Flat;
  case AuthoredSurfaceShading::Smooth:
    return quader_poly::SurfaceShading::Smooth;
  case AuthoredSurfaceShading::Authored:
    return quader_poly::SurfaceShading::Authored;
  }
  return quader_poly::SurfaceShading::Flat;
}

[[nodiscard]] bool positive_finite(float value) {
  return std::isfinite(value) && value > 0.0F;
}

[[nodiscard]] bool valid_subdivision_count(int value, int minimum) {
  return value >= minimum && value <= 128;
}

void prepare_primitive_document(quader_poly::Document &document,
                                quader::QVec3 translation,
                                std::uint32_t material_slot) {
  for (quader_poly::Vertex &vertex : document.vertices) {
    vertex.position = vertex.position + translation;
  }
  for (quader_poly::Face &face : document.faces) {
    face.material_slot = material_slot;
  }
}

[[nodiscard]] bool stale_generation(VertexId id) {
  return id.valid() && id.generation != 1U;
}

[[nodiscard]] bool stale_generation(FaceId id) {
  return id.valid() && id.generation != 1U;
}

[[nodiscard]] bool stale_generation(EdgeKey edge) {
  return stale_generation(edge.a) || stale_generation(edge.b);
}

[[nodiscard]] Error invalid_id_error(std::string message) {
  return make_error(ErrorCode::InvalidId, std::move(message));
}

[[nodiscard]] Error stale_id_error(std::string message) {
  return make_error(ErrorCode::StaleId, std::move(message));
}

[[nodiscard]] bool vertex_id_was_issued(
    const quader_poly::Document &document, std::uint32_t index) {
  return index != 0U &&
         (document.next_vertex_id == 0U || index < document.next_vertex_id);
}

[[nodiscard]] bool face_id_was_issued(
    const quader_poly::Document &document, std::uint32_t index) {
  return index != 0U &&
         (document.next_face_id == 0U || index < document.next_face_id);
}

[[nodiscard]] ElementDelta
adapt_delta(const quader_poly::OperationElementDelta &delta) {
  ElementDelta result;
  result.vertices.reserve(delta.vertices.size());
  for (const quader_poly::ElementId id : delta.vertices) {
    result.vertices.push_back(vertex_id(id));
  }
  result.edges.reserve(delta.edges.size());
  for (const quader_poly::Edge edge : delta.edges) {
    result.edges.push_back(make_edge_key(vertex_id(edge.a), vertex_id(edge.b)));
  }
  result.faces.reserve(delta.faces.size());
  for (const quader_poly::ElementId id : delta.faces) {
    result.faces.push_back(face_id(id));
  }
  return result;
}

[[nodiscard]] OperationResult
adapt_operation_result(const quader_poly::OperationResult &result) {
  OperationResult sdk_result;
  sdk_result.success = result.success;
  sdk_result.changed = result.changed;
  sdk_result.message = result.message;
  sdk_result.created = adapt_delta(result.created);
  sdk_result.deleted = adapt_delta(result.deleted);
  sdk_result.affected = adapt_delta(result.affected);
  sdk_result.modified = sdk_result.affected;
  sdk_result.dirty.geometry = result.changed;
  sdk_result.dirty.topology =
      result.changed && (!sdk_result.created.empty() || !sdk_result.deleted.empty());
  sdk_result.dirty.selection = result.changed;
  if (result.edge_bevel_width.has_value()) {
    sdk_result.edge_bevel_width = EdgeBevelWidthFeedback{
        .requested_width = result.edge_bevel_width->requested_width,
        .applied_width = result.edge_bevel_width->applied_width,
        .support_limited = result.edge_bevel_width->support_limited,
    };
  }
  if (result.bridge_shape.has_value()) {
    BridgeShapeFeedback feedback;
    for (std::size_t index = 0; index < feedback.controls.size(); ++index) {
      feedback.controls[index] = from_native(result.bridge_shape->controls[index]);
    }
    feedback.start_handle_axis = from_native(result.bridge_shape->start_handle_axis);
    feedback.end_handle_axis = from_native(result.bridge_shape->end_handle_axis);
    feedback.start_tension = result.bridge_shape->start_tension;
    feedback.end_tension = result.bridge_shape->end_tension;
    feedback.representative_rail = result.bridge_shape->representative_rail;
    sdk_result.bridge_shape = feedback;
  }
  return sdk_result;
}

[[nodiscard]] Result<OperationResult> operation_success(OperationResult result) {
  return Result<OperationResult>::success(std::move(result));
}

[[nodiscard]] SelectionKind
selection_kind_from_native(quader_poly::SelectionMode mode) {
  switch (mode) {
  case quader_poly::SelectionMode::Vertex:
    return SelectionKind::Vertex;
  case quader_poly::SelectionMode::Edge:
    return SelectionKind::Edge;
  case quader_poly::SelectionMode::Face:
    return SelectionKind::Face;
  }
  return SelectionKind::Vertex;
}

[[nodiscard]] quader_poly::SelectionMode native_selection_mode(
    SelectionMode mode) {
  switch (mode) {
  case SelectionMode::Vertex:
    return quader_poly::SelectionMode::Vertex;
  case SelectionMode::Edge:
    return quader_poly::SelectionMode::Edge;
  case SelectionMode::Face:
    return quader_poly::SelectionMode::Face;
  }
  return quader_poly::SelectionMode::Vertex;
}

[[nodiscard]] bool valid_selection_mode(SelectionMode mode) {
  switch (mode) {
  case SelectionMode::Vertex:
  case SelectionMode::Edge:
  case SelectionMode::Face:
    return true;
  }
  return false;
}

[[nodiscard]] bool
same_native_selection(const quader_poly::Selection &left,
                      const quader_poly::Selection &right) {
  return left.mode == right.mode && left.vertices == right.vertices &&
         left.edges == right.edges && left.faces == right.faces &&
         left.has_active == right.has_active &&
         left.active_kind == right.active_kind &&
         left.active_vertex == right.active_vertex &&
         left.active_edge == right.active_edge &&
         left.active_face == right.active_face;
}

[[nodiscard]] quader_poly::FaceTextureTransform
to_native(const FaceTextureTransform &value) {
  return {.scale = to_native(value.scale),
          .shift_uv = to_native(value.shift_uv),
          .rotation_degrees = value.rotation_degrees};
}

[[nodiscard]] FaceTextureTransform
from_native(const quader_poly::FaceTextureTransform &value) {
  return {.scale = from_native(value.scale),
          .shift_uv = from_native(value.shift_uv),
          .rotation_degrees = value.rotation_degrees};
}

[[nodiscard]] quader_poly::CircularUvInterpolation
to_native(const CircularUvInterpolation &value) {
  return {.center = to_native(value.center),
          .axis_u = to_native(value.axis_u),
          .axis_v = to_native(value.axis_v),
          .uv_from_polar = value.uv_from_polar};
}

[[nodiscard]] CircularUvInterpolation
from_native(const quader_poly::CircularUvInterpolation &value) {
  return {.center = from_native(value.center),
          .axis_u = from_native(value.axis_u),
          .axis_v = from_native(value.axis_v),
          .uv_from_polar = value.uv_from_polar};
}

[[nodiscard]] bool native_active_selection_matches_snapshot(
    const quader_poly::Selection &selection,
    const PolygonSelectionSnapshot &snapshot) {
  if (selection.has_active != snapshot.has_active) {
    return false;
  }
  if (!selection.has_active) {
    return true;
  }
  switch (snapshot.kind) {
  case SelectionKind::Vertex:
    return selection.active_kind == quader_poly::ElementKind::Vertex &&
           vertex_id(selection.active_vertex) == snapshot.active_vertex;
  case SelectionKind::Edge:
    return selection.active_kind == quader_poly::ElementKind::Edge &&
           edge_id(selection.active_edge) == snapshot.active_edge;
  case SelectionKind::Face:
    return selection.active_kind == quader_poly::ElementKind::Face &&
           face_id(selection.active_face) == snapshot.active_face;
  case SelectionKind::Object:
    return true;
  }
  return false;
}

[[nodiscard]] bool native_selection_matches_snapshot(
    const quader_poly::Selection &selection,
    const PolygonSelectionSnapshot &snapshot) {
  if (selection_kind_from_native(selection.mode) != snapshot.kind ||
      selection.vertices.size() != snapshot.vertices.size() ||
      selection.edges.size() != snapshot.edges.size() ||
      selection.faces.size() != snapshot.faces.size() ||
      !native_active_selection_matches_snapshot(selection, snapshot)) {
    return false;
  }
  for (std::size_t index = 0; index < selection.vertices.size(); ++index) {
    if (vertex_id(selection.vertices[index]) != snapshot.vertices[index]) {
      return false;
    }
  }
  for (std::size_t index = 0; index < selection.edges.size(); ++index) {
    if (edge_id(selection.edges[index]) != snapshot.edges[index]) {
      return false;
    }
  }
  for (std::size_t index = 0; index < selection.faces.size(); ++index) {
    if (face_id(selection.faces[index]) != snapshot.faces[index]) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] OperationResult selection_only_result(bool changed,
                                                    std::string message) {
  OperationResult result;
  result.changed = changed;
  result.message = std::move(message);
  result.dirty.selection = changed;
  return result;
}

void bump_content(PolygonDocumentImpl &impl, bool changed) {
  if (changed) {
    ++impl.content_revision;
  }
}

void bump_selection(PolygonDocumentImpl &impl, bool changed) {
  if (changed) {
    ++impl.selection_revision;
  }
}

void commit_selection(PolygonDocumentImpl &impl,
                      quader_poly::Selection selection, bool changed) {
  if (!changed) {
    return;
  }
  impl.selection = std::make_shared<const quader_poly::Selection>(
      std::move(selection));
  bump_selection(impl, true);
}

[[nodiscard]] bool selection_validation_uses_index(
    std::size_t requested_count, std::size_t live_count) {
  // Hash indexes pay for one allocation per live element. Small edit-mode
  // meshes (notably dense selections of many boxes) are substantially faster
  // with their cache-local linear scan; build an index only when the avoided
  // comparison work is large enough to amortize those allocations.
  constexpr std::size_t kLinearComparisonBudget = 4096U;
  return requested_count != 0U &&
         live_count > kLinearComparisonBudget / requested_count;
}

[[nodiscard]] Result<OperationResult>
validate_vertex_id(
    const quader_poly::Document &document, VertexId id,
    const std::unordered_set<quader_poly::ElementId> *live_ids = nullptr) {
  if (!id.valid()) {
    return Result<OperationResult>::failure(invalid_id_error("Vertex ID is invalid."));
  }
  if (stale_generation(id)) {
    return Result<OperationResult>::failure(stale_id_error("Vertex ID is stale."));
  }
  const bool live = live_ids != nullptr
                        ? live_ids->contains(id.index)
                        : quader_poly::find_vertex(document, id.index) !=
                              nullptr;
  if (!live) {
    return Result<OperationResult>::failure(
        vertex_id_was_issued(document, id.index)
            ? stale_id_error("Vertex ID is stale.")
            : invalid_id_error("Vertex ID was never issued by this document."));
  }
  return operation_success(selection_only_result(false, {}));
}

[[nodiscard]] Result<OperationResult>
validate_face_id(
    const quader_poly::Document &document, FaceId id,
    const std::unordered_set<quader_poly::ElementId> *live_ids = nullptr) {
  if (!id.valid()) {
    return Result<OperationResult>::failure(invalid_id_error("Face ID is invalid."));
  }
  if (stale_generation(id)) {
    return Result<OperationResult>::failure(stale_id_error("Face ID is stale."));
  }
  const bool live = live_ids != nullptr
                        ? live_ids->contains(id.index)
                        : quader_poly::find_face(document, id.index) != nullptr;
  if (!live) {
    return Result<OperationResult>::failure(
        face_id_was_issued(document, id.index)
            ? stale_id_error("Face ID is stale.")
            : invalid_id_error("Face ID was never issued by this document."));
  }
  return operation_success(selection_only_result(false, {}));
}

[[nodiscard]] Result<OperationResult>
validate_edge_key(const quader_poly::Document &document, EdgeKey edge,
                  std::span<const quader_poly::Edge> live_edges,
                  const std::unordered_set<quader_poly::ElementId>
                      *live_vertex_ids = nullptr) {
  if (!edge.valid()) {
    return Result<OperationResult>::failure(invalid_id_error("Edge key is invalid."));
  }
  if (stale_generation(edge)) {
    return Result<OperationResult>::failure(stale_id_error("Edge key is stale."));
  }
  const bool a_live =
      live_vertex_ids != nullptr
          ? live_vertex_ids->contains(edge.a.index)
          : quader_poly::find_vertex(document, edge.a.index) != nullptr;
  const bool b_live =
      live_vertex_ids != nullptr
          ? live_vertex_ids->contains(edge.b.index)
          : quader_poly::find_vertex(document, edge.b.index) != nullptr;
  if (!a_live || !b_live) {
    const bool stale_endpoint =
        (!a_live && vertex_id_was_issued(document, edge.a.index)) ||
        (!b_live && vertex_id_was_issued(document, edge.b.index));
    return Result<OperationResult>::failure(
        stale_endpoint
            ? stale_id_error("Edge key references a stale endpoint.")
            : invalid_id_error(
                  "Edge key references an endpoint that was never issued."));
  }
  const quader_poly::Edge native_edge =
      quader_poly::make_edge(edge.a.index, edge.b.index);
  const auto edge_less = [](quader_poly::Edge left,
                            quader_poly::Edge right) {
    return left.a < right.a || (left.a == right.a && left.b < right.b);
  };
  const bool live =
      std::ranges::binary_search(live_edges, native_edge, edge_less);
  if (!live) {
    // Edge identity is derived from two issued vertex identities. Once both
    // endpoints belong to this document lineage, an absent pair is a stale
    // topology reference rather than an unissued allocator value.
    return Result<OperationResult>::failure(
        stale_id_error("Edge key is stale in the current topology."));
  }
  return operation_success(selection_only_result(false, {}));
}

[[nodiscard]] Result<OperationResult>
validate_edge_key(const quader_poly::Document &document, EdgeKey edge) {
  const std::vector<quader_poly::Edge> live_edges =
      quader_poly::document_edges(document);
  return validate_edge_key(document, edge, live_edges);
}

template <typename T>
[[nodiscard]] typename std::vector<T>::iterator find_value(std::vector<T> &target,
                                                           const T &value) {
  for (auto item = target.begin(); item != target.end(); ++item) {
    if (*item == value) {
      return item;
    }
  }
  return target.end();
}

template <typename T>
void apply_edit(std::vector<T> &target, std::span<const T> values,
                SelectionEdit edit) {
  if (edit == SelectionEdit::Replace) {
    target.assign(values.begin(), values.end());
    return;
  }

  for (const T &value : values) {
    const auto found = find_value(target, value);
    if (edit == SelectionEdit::Add) {
      if (found == target.end()) {
        target.push_back(value);
      }
      continue;
    }
    if (edit == SelectionEdit::Remove) {
      if (found != target.end()) {
        target.erase(found);
      }
      continue;
    }
    if (found == target.end()) {
      target.push_back(value);
    } else {
      target.erase(found);
    }
  }
}

[[nodiscard]] bool active_selection_is_selected(
    const quader_poly::Selection &selection,
    const quader_poly::Selection &candidate) {
  if (!candidate.has_active || selection.mode != candidate.mode) {
    return false;
  }
  switch (selection.mode) {
  case quader_poly::SelectionMode::Vertex:
    return candidate.active_kind == quader_poly::ElementKind::Vertex &&
           std::ranges::find(selection.vertices, candidate.active_vertex) !=
               selection.vertices.end();
  case quader_poly::SelectionMode::Edge:
    return candidate.active_kind == quader_poly::ElementKind::Edge &&
           std::ranges::find(selection.edges,
                             quader_poly::make_edge(candidate.active_edge.a,
                                                    candidate.active_edge.b)) !=
               selection.edges.end();
  case quader_poly::SelectionMode::Face:
    return candidate.active_kind == quader_poly::ElementKind::Face &&
           std::ranges::find(selection.faces, candidate.active_face) !=
               selection.faces.end();
  }
  return false;
}

void copy_active_selection(quader_poly::Selection &selection,
                           const quader_poly::Selection &source) {
  selection.has_active = true;
  selection.active_kind = source.active_kind;
  switch (source.active_kind) {
  case quader_poly::ElementKind::Vertex:
    selection.active_vertex = source.active_vertex;
    break;
  case quader_poly::ElementKind::Edge:
    selection.active_edge = quader_poly::make_edge(source.active_edge.a,
                                                   source.active_edge.b);
    break;
  case quader_poly::ElementKind::Face:
    selection.active_face = source.active_face;
    break;
  }
}

void update_active_selection(
    quader_poly::Selection &selection,
    const quader_poly::Selection *requested = nullptr,
    const quader_poly::Selection *previous = nullptr) {
  selection.has_active = false;
  selection.active_kind = quader_poly::ElementKind::Vertex;
  selection.active_vertex = quader_poly::kInvalidElementId;
  selection.active_edge = {};
  selection.active_face = quader_poly::kInvalidElementId;
  if (requested != nullptr &&
      active_selection_is_selected(selection, *requested)) {
    copy_active_selection(selection, *requested);
    return;
  }
  if (previous != nullptr &&
      active_selection_is_selected(selection, *previous)) {
    copy_active_selection(selection, *previous);
    return;
  }
  if (selection.mode == quader_poly::SelectionMode::Vertex &&
      !selection.vertices.empty()) {
    selection.has_active = true;
    selection.active_kind = quader_poly::ElementKind::Vertex;
    selection.active_vertex = selection.vertices.front();
    return;
  }
  if (selection.mode == quader_poly::SelectionMode::Edge &&
      !selection.edges.empty()) {
    selection.has_active = true;
    selection.active_kind = quader_poly::ElementKind::Edge;
    selection.active_edge = selection.edges.front();
    return;
  }
  if (selection.mode == quader_poly::SelectionMode::Face &&
      !selection.faces.empty()) {
    selection.has_active = true;
    selection.active_kind = quader_poly::ElementKind::Face;
    selection.active_face = selection.faces.front();
  }
}

[[nodiscard]] quader_poly::Selection active_selection_from_snapshot(
    const PolygonSelectionSnapshot &snapshot) {
  quader_poly::Selection active;
  active.has_active = snapshot.has_active;
  switch (snapshot.kind) {
  case SelectionKind::Vertex:
    active.mode = quader_poly::SelectionMode::Vertex;
    active.active_kind = quader_poly::ElementKind::Vertex;
    active.active_vertex = snapshot.active_vertex.index;
    break;
  case SelectionKind::Edge:
    active.mode = quader_poly::SelectionMode::Edge;
    active.active_kind = quader_poly::ElementKind::Edge;
    active.active_edge = quader_poly::make_edge(snapshot.active_edge.a.index,
                                                snapshot.active_edge.b.index);
    break;
  case SelectionKind::Face:
    active.mode = quader_poly::SelectionMode::Face;
    active.active_kind = quader_poly::ElementKind::Face;
    active.active_face = snapshot.active_face.index;
    break;
  case SelectionKind::Object:
    break;
  }
  return active;
}

[[nodiscard]] bool snapshot_active_is_member(
    const PolygonSelectionSnapshot &snapshot) {
  if (!snapshot.has_active) {
    return false;
  }
  switch (snapshot.kind) {
  case SelectionKind::Vertex:
    return std::ranges::find(snapshot.vertices, snapshot.active_vertex) !=
           snapshot.vertices.end();
  case SelectionKind::Edge:
    return std::ranges::find(snapshot.edges, snapshot.active_edge) !=
           snapshot.edges.end();
  case SelectionKind::Face:
    return std::ranges::find(snapshot.faces, snapshot.active_face) !=
           snapshot.faces.end();
  case SelectionKind::Object:
    return false;
  }
  return false;
}

[[nodiscard]] quader_poly::Edge native_edge(EdgeKey edge) {
  return quader_poly::make_edge(edge.a.index, edge.b.index);
}

[[nodiscard]] Result<quader_poly::ComponentSelectionUniverse>
native_selection_universe(
    const PolygonDocumentImpl &impl,
    const PolygonSelectionUniverse &eligible) {
  if (!valid_selection_mode(eligible.mode)) {
    return Result<quader_poly::ComponentSelectionUniverse>::failure(
        make_error(ErrorCode::InvalidArgument,
                   "Selection inversion requires a valid component mode."));
  }
  const bool payload_matches_mode =
      (eligible.mode == SelectionMode::Vertex && eligible.edges.empty() &&
       eligible.faces.empty()) ||
      (eligible.mode == SelectionMode::Edge && eligible.vertices.empty() &&
       eligible.faces.empty()) ||
      (eligible.mode == SelectionMode::Face && eligible.vertices.empty() &&
       eligible.edges.empty());
  if (!payload_matches_mode) {
    return Result<quader_poly::ComponentSelectionUniverse>::failure(
        make_error(ErrorCode::InvalidArgument,
                   "Selection inversion universe payload does not match its mode."));
  }

  quader_poly::ComponentSelectionUniverse universe;
  universe.mode = native_selection_mode(eligible.mode);
  if (eligible.mode == SelectionMode::Vertex) {
    std::unordered_set<quader_poly::ElementId> live_ids;
    const bool use_live_index = selection_validation_uses_index(
        eligible.vertices.size(), impl.document->vertices.size());
    if (use_live_index) {
      live_ids.reserve(impl.document->vertices.size());
      for (const quader_poly::Vertex &vertex : impl.document->vertices) {
        live_ids.emplace(vertex.id);
      }
    }
    universe.vertices.reserve(eligible.vertices.size());
    std::set<VertexId> unique_ids;
    for (const VertexId id : eligible.vertices) {
      if (!unique_ids.insert(id).second) {
        return Result<quader_poly::ComponentSelectionUniverse>::failure(
            make_error(ErrorCode::InvalidArgument,
                       "Selection inversion universe contains a duplicate vertex."));
      }
      Result<OperationResult> validation = validate_vertex_id(
          *impl.document, id, use_live_index ? &live_ids : nullptr);
      if (!validation.ok()) {
        return Result<quader_poly::ComponentSelectionUniverse>::failure(
            validation.error());
      }
      universe.vertices.push_back(id.index);
    }
    return Result<quader_poly::ComponentSelectionUniverse>::success(
        std::move(universe));
  }
  if (eligible.mode == SelectionMode::Edge) {
    const std::vector<quader_poly::Edge> live_edges =
        quader_poly::document_edges(*impl.document);
    std::unordered_set<quader_poly::ElementId> live_vertex_ids;
    const bool use_live_index = selection_validation_uses_index(
        eligible.edges.size(), impl.document->vertices.size());
    if (use_live_index) {
      live_vertex_ids.reserve(impl.document->vertices.size());
      for (const quader_poly::Vertex &vertex : impl.document->vertices) {
        live_vertex_ids.emplace(vertex.id);
      }
    }
    universe.edges.reserve(eligible.edges.size());
    std::set<EdgeKey> unique_edges;
    for (const EdgeKey edge : eligible.edges) {
      if (edge != make_edge_key(edge.a, edge.b)) {
        return Result<quader_poly::ComponentSelectionUniverse>::failure(
            make_error(ErrorCode::InvalidArgument,
                       "Selection inversion requires canonical edge keys."));
      }
      if (!unique_edges.insert(edge).second) {
        return Result<quader_poly::ComponentSelectionUniverse>::failure(
            make_error(ErrorCode::InvalidArgument,
                       "Selection inversion universe contains a duplicate edge."));
      }
      Result<OperationResult> validation = validate_edge_key(
          *impl.document, edge, live_edges,
          use_live_index ? &live_vertex_ids : nullptr);
      if (!validation.ok()) {
        return Result<quader_poly::ComponentSelectionUniverse>::failure(
            validation.error());
      }
      universe.edges.push_back(native_edge(edge));
    }
    return Result<quader_poly::ComponentSelectionUniverse>::success(
        std::move(universe));
  }

  if (eligible.mode != SelectionMode::Face) {
    return Result<quader_poly::ComponentSelectionUniverse>::failure(
        make_error(ErrorCode::InvalidArgument,
                   "Selection inversion requires a valid component mode."));
  }
  std::unordered_set<quader_poly::ElementId> live_ids;
  const bool use_live_index = selection_validation_uses_index(
      eligible.faces.size(), impl.document->faces.size());
  if (use_live_index) {
    live_ids.reserve(impl.document->faces.size());
    for (const quader_poly::Face &face : impl.document->faces) {
      live_ids.emplace(face.id);
    }
  }
  universe.faces.reserve(eligible.faces.size());
  std::set<FaceId> unique_ids;
  for (const FaceId id : eligible.faces) {
    if (!unique_ids.insert(id).second) {
      return Result<quader_poly::ComponentSelectionUniverse>::failure(
          make_error(ErrorCode::InvalidArgument,
                     "Selection inversion universe contains a duplicate face."));
    }
    Result<OperationResult> validation = validate_face_id(
        *impl.document, id, use_live_index ? &live_ids : nullptr);
    if (!validation.ok()) {
      return Result<quader_poly::ComponentSelectionUniverse>::failure(
          validation.error());
    }
    universe.faces.push_back(id.index);
  }
  return Result<quader_poly::ComponentSelectionUniverse>::success(
      std::move(universe));
}

[[nodiscard]] quader_poly::KnifePointTarget
native_knife_target(KnifeTarget target) {
  quader_poly::KnifePointTarget native;
  switch (target.kind) {
  case KnifeTargetKind::ExistingVertex:
    native.kind = quader_poly::KnifePointTargetKind::ExistingVertex;
    break;
  case KnifeTargetKind::ExistingEdge:
    native.kind = quader_poly::KnifePointTargetKind::ExistingEdge;
    break;
  case KnifeTargetKind::InsertedVertex:
    native.kind = quader_poly::KnifePointTargetKind::InsertedVertex;
    break;
  case KnifeTargetKind::FacePoint:
    native.kind = quader_poly::KnifePointTargetKind::FacePoint;
    break;
  }
  native.vertex_id = target.vertex.index;
  native.edge = native_edge(target.edge);
  native.edge_factor = target.edge_factor;
  native.face_id = target.face.index;
  native.position = to_native(target.position);
  return native;
}

[[nodiscard]] quader_poly::KnifeStrokeSegment
native_knife_segment(KnifeStrokeSegment segment) {
  return {
      .first_point = segment.first_point,
      .second_point = segment.second_point,
  };
}

[[nodiscard]] float distance_squared(quader::QVec3 left,
                                     quader::QVec3 right) {
  const float x = left.x - right.x;
  const float y = left.y - right.y;
  const float z = left.z - right.z;
  return x * x + y * y + z * z;
}

[[nodiscard]] Result<OperationResult> validate_document(
    const PolygonDocumentImpl &impl,
    quader_poly::PolygonDocumentValidationPolicy policy) {
  const quader_poly::PolygonDocumentValidationReport document_report =
      quader_poly::validate_polygon_document(*impl.document, policy);
  const quader_poly::PolygonSelectionLivenessReport selection_report =
      quader_poly::validate_polygon_selection_liveness(*impl.document,
                                                       *impl.selection);

  OperationResult result;
  result.success = document_report.ok() && selection_report.ok();
  result.error_code = result.success ? ErrorCode::Ok : ErrorCode::ValidationFailed;
  result.message = result.success ? "Document is valid." : "Document validation failed.";

  for (const quader_poly::PolygonDocumentDiagnostic &diagnostic :
       document_report.diagnostics) {
    result.diagnostics.push_back({
        .code = "polygon.document",
        .severity = diagnostic.severity ==
                            quader_poly::PolygonDocumentDiagnosticSeverity::Error
                        ? DiagnosticSeverity::Error
                        : DiagnosticSeverity::Warning,
        .message = diagnostic.message,
    });
  }
  for (const quader_poly::PolygonSelectionLivenessIssue &issue :
       selection_report.issues) {
    result.diagnostics.push_back({
        .code = "polygon.selection_liveness",
        .severity = DiagnosticSeverity::Error,
        .message = "Selection references stale polygon topology.",
    });
    static_cast<void>(issue);
  }

  if (!result.success) {
    Error error = make_error(ErrorCode::ValidationFailed, result.message);
    error.diagnostics = result.diagnostics;
    return Result<OperationResult>::failure(std::move(error));
  }
  return operation_success(std::move(result));
}

template <typename Operation>
[[nodiscard]] Result<OperationResult>
apply_operation(PolygonDocumentImpl &impl, Operation operation,
                bool material_change = false) {
  quader_poly::Document document = *impl.document;
  quader_poly::Selection selection = *impl.selection;
  const quader_poly::Selection selection_before = selection;
  quader_poly::OperationResult native_result =
      operation(document, selection);
  if (!native_result.success) {
    return Result<OperationResult>::failure(
        make_error(ErrorCode::ValidationFailed, std::move(native_result.message)));
  }
  OperationResult result = adapt_operation_result(native_result);
  const bool selection_changed =
      result.changed && !same_native_selection(selection_before, selection);
  result.dirty.selection = selection_changed;
  if (material_change && result.changed) {
    result.dirty.materials = true;
  }
  if (result.changed) {
    PolygonDocumentNativeAccess::reconcile_circular_uv_interpolations(*impl.document, document);
    impl.document = std::make_shared<const quader_poly::Document>(
        std::move(document));
    impl.selection = std::make_shared<const quader_poly::Selection>(
        std::move(selection));
  }
  bump_content(impl, result.changed);
  bump_selection(impl, selection_changed);
  return operation_success(std::move(result));
}

[[nodiscard]] bool finite_position(quader::QVec3 position) {
  return std::isfinite(position.x) && std::isfinite(position.y) &&
         std::isfinite(position.z);
}

template <typename TransformPosition>
[[nodiscard]] Result<SelectionTransformEvaluation>
evaluate_prepared_selection_transform(
    const PreparedSelectionTransformImpl &prepared,
    TransformPosition transform_position) {
  QDR_PROFILE_SCOPE("PolygonDocument evaluate prepared selection transform");
  const auto &source = *prepared.source_document;
  std::shared_ptr<std::vector<VertexPositionUpdate>> positions;
  if (!prepared.vertex_indices->empty()) {
    positions = std::make_shared<std::vector<VertexPositionUpdate>>();
    positions->reserve(prepared.vertex_indices->size());
  }
  bool changed = false;
  for (const std::size_t index : *prepared.vertex_indices) {
    const auto &vertex = source.vertices[index];
    const auto position = vertex.position;
    const auto desired = transform_position(position);
    if (!finite_position(desired)) {
      return Result<SelectionTransformEvaluation>::failure(make_error(
          ErrorCode::InvalidArgument,
          "Vertex position updates must contain finite coordinates."));
    }
    const bool vertex_changed = desired.x != position.x ||
                                desired.y != position.y || desired.z != position.z;
    changed = changed || vertex_changed;
    positions->push_back({.vertex = vertex_id(vertex.id),
                          .position = from_native(vertex_changed ? desired : position)});
  }
  prepared.work_state->evaluated_vertex_count.fetch_add(
      prepared.vertex_indices->size(), std::memory_order_relaxed);

  SelectionTransformEvaluation evaluation{.document = prepared.source};
  evaluation.receipt.changed = changed;
  evaluation.receipt.dirty.geometry = changed;
  if (prepared.vertex_indices->empty()) {
    evaluation.receipt.message = "No polygon elements are selected.";
  }
  if (changed) {
    // The SDK candidate and native preview share the same immutable coordinates.
    // Faces, UVs and connectivity remain pinned until a legacy consumer asks for
    // a complete document; validation precedes any candidate publication.
    evaluation.positions = std::move(positions);
    auto &candidate = PolygonDocumentNativeAccess::impl(evaluation.document);
    if (prepared.has_circular_interpolation) {
      auto document = source;
      for (std::size_t i = 0U; i < prepared.vertex_indices->size(); ++i) {
        document.vertices[(*prepared.vertex_indices)[i]].position =
            to_native((*evaluation.positions)[i].position);
      }
      PolygonDocumentNativeAccess::reconcile_circular_uv_interpolations(source, document);
      candidate.document = std::make_shared<const quader_poly::Document>(std::move(document));
    } else {
      candidate.document = PolygonDocumentStorage::with_positions(
          prepared.source_document, prepared.vertex_indices, evaluation.positions,
          prepared.work_state, prepared.source_topology_identity);
    }
    bump_content(candidate, true);
    prepared.work_state->coordinate_publication_count.fetch_add(
        1U, std::memory_order_relaxed);
  } else {
    evaluation.positions = prepared.source_positions;
  }
  return Result<SelectionTransformEvaluation>::success(std::move(evaluation));
}

} // namespace

void PolygonDocumentNativeAccess::reconcile_circular_uv_interpolations(
    const quader_poly::Document &source, quader_poly::Document &replacement) {
  if (std::none_of(replacement.faces.begin(), replacement.faces.end(),
      [](const auto &face) { return face.circular_uv_interpolation.has_value(); })) {
    return;
  }
  std::unordered_map<quader_poly::ElementId, const quader_poly::Face *> source_faces;
  for (const auto &face : source.faces) { source_faces.emplace(face.id, &face); }
  const quader_poly::document_internal::VertexLookupIndex before(source), after(replacement);
  for (auto &face : replacement.faces) {
    if (!face.circular_uv_interpolation.has_value()) { continue; }
    const auto found = source_faces.find(face.id);
    bool unchanged = found != source_faces.end();
    if (unchanged) {
      const auto &old = *found->second;
      unchanged = old.vertices == face.vertices && old.uvs.size() == face.uvs.size() &&
          old.circular_uv_interpolation == face.circular_uv_interpolation;
      for (std::size_t i = 0U; unchanged && i < face.uvs.size(); ++i) {
        unchanged = old.uvs[i].x == face.uvs[i].x && old.uvs[i].y == face.uvs[i].y;
      }
      for (auto id : face.vertices) {
        if (!unchanged) { break; }
        const auto *first = before.find(id), *second = after.find(id);
        unchanged = first != nullptr && second != nullptr &&
            first->position.x == second->position.x && first->position.y == second->position.y &&
            first->position.z == second->position.z;
      }
    }
    if (!unchanged) { face.circular_uv_interpolation.reset(); }
  }
}

PolygonDocument::PolygonDocument()
    : impl_(std::make_unique<PolygonDocumentImpl>()) {}

PolygonDocument::PolygonDocument(std::unique_ptr<PolygonDocumentImpl> impl)
    : impl_(std::move(impl)) {
  if (!impl_) {
    impl_ = std::make_unique<PolygonDocumentImpl>();
  }
}

PolygonDocument::PolygonDocument(const PolygonDocument &other)
    : impl_(std::make_unique<PolygonDocumentImpl>(*other.impl_)) {}

PolygonDocument::PolygonDocument(PolygonDocument &&other) noexcept = default;

PolygonDocument &
PolygonDocument::operator=(const PolygonDocument &other) {
  if (this != &other) {
    impl_ = std::make_unique<PolygonDocumentImpl>(*other.impl_);
  }
  return *this;
}

PolygonDocument &
PolygonDocument::operator=(PolygonDocument &&other) noexcept = default;

PolygonDocument::~PolygonDocument() = default;

void PolygonDocument::preserve_identity_high_water(const PolygonDocument &other) {
  const auto next_vertex_id = std::max(impl_->document.next_vertex_id(),
                                       other.impl_->document.next_vertex_id());
  const auto next_face_id = std::max(impl_->document.next_face_id(),
                                     other.impl_->document.next_face_id());
  if (next_vertex_id == impl_->document.next_vertex_id() &&
      next_face_id == impl_->document.next_face_id()) {
    return;
  }
  quader_poly::Document reconciled = *impl_->document;
  reconciled.next_vertex_id = next_vertex_id;
  reconciled.next_face_id = next_face_id;
  impl_->document = std::make_shared<const quader_poly::Document>(std::move(reconciled));
}

Result<PolygonDocument> PolygonDocument::make_box(const BoxSpec &spec) {
  const Vec3 dimensions{
      spec.max.x - spec.min.x,
      spec.max.y - spec.min.y,
      spec.max.z - spec.min.z,
  };
  if (!positive_finite(dimensions.x) || !positive_finite(dimensions.y) ||
      !positive_finite(dimensions.z)) {
    return Result<PolygonDocument>::failure(
        make_error(ErrorCode::InvalidArgument,
                   "Box bounds must have positive width, height, and depth."));
  }
  if (!valid_subdivision_count(spec.x_subdivisions, 1) ||
      !valid_subdivision_count(spec.y_subdivisions, 1) ||
      !valid_subdivision_count(spec.z_subdivisions, 1)) {
    return Result<PolygonDocument>::failure(make_error(
        ErrorCode::InvalidArgument,
        "Box subdivision counts must be between 1 and 128."));
  }

  quader_poly::Document document = quader_poly::make_cube({
      .width = dimensions.x,
      .height = dimensions.y,
      .depth = dimensions.z,
      .width_segments = spec.x_subdivisions,
      .height_segments = spec.y_subdivisions,
      .depth_segments = spec.z_subdivisions,
      .flip_faces = spec.flip_faces,
  });
  prepare_primitive_document(
      document,
      to_native(Vec3{
          std::midpoint(spec.min.x, spec.max.x),
          std::midpoint(spec.min.y, spec.max.y),
          std::midpoint(spec.min.z, spec.max.z),
      }),
      native_material_slot(spec.material));
  return Result<PolygonDocument>::success(
      PolygonDocumentNativeAccess::from_native(std::move(document)));
}

Result<PolygonDocument>
PolygonDocument::make_box_from_corners(std::span<const Vec3> corners,
                                       MaterialId material) {
  if (corners.size() != 8U) {
    return Result<PolygonDocument>::failure(make_error(
        ErrorCode::InvalidArgument, "Box needs eight corner points."));
  }

  std::array<quader::QVec3, 8> native_corners{};
  for (std::size_t index = 0; index < corners.size(); ++index) {
    const Vec3 corner = corners[index];
    if (!std::isfinite(corner.x) || !std::isfinite(corner.y) ||
        !std::isfinite(corner.z)) {
      return Result<PolygonDocument>::failure(make_error(
          ErrorCode::InvalidArgument, "Box corners must be finite."));
    }
    native_corners[index] = to_native(corner);
  }

  quader_poly::DocumentBuilder builder;
  quader_poly::Document document =
      builder
          .box_from_corners(native_corners, native_material_slot(material))
          .take();
  if (document.vertices.size() != 8U || document.faces.size() != 6U ||
      quader_poly::compile_document(document).indices.empty()) {
    return Result<PolygonDocument>::failure(make_error(
        ErrorCode::InvalidArgument,
        "Box needs finite width, height, and depth."));
  }
  return Result<PolygonDocument>::success(
      PolygonDocumentNativeAccess::from_native(std::move(document)));
}

Result<PolygonDocument>
PolygonDocument::make_cylinder(const CylinderSpec &spec) {
  if (!positive_finite(spec.width) || !positive_finite(spec.height) ||
      !positive_finite(spec.depth)) {
    return Result<PolygonDocument>::failure(make_error(
        ErrorCode::InvalidArgument,
        "Cylinder dimensions must have positive width, height, and depth."));
  }
  if (!valid_subdivision_count(spec.side_count, 3)) {
    return Result<PolygonDocument>::failure(make_error(
        ErrorCode::InvalidArgument,
        "Cylinder side count must be between 3 and 128."));
  }

  quader_poly::Document document = quader_poly::make_cylinder({
      .width = spec.width,
      .height = spec.height,
      .depth = spec.depth,
      .side_count = spec.side_count,
      .capped = spec.capped,
      .flip_faces = spec.flip_faces,
  });
  prepare_primitive_document(document, {}, native_material_slot(spec.material));
  return Result<PolygonDocument>::success(
      PolygonDocumentNativeAccess::from_native(std::move(document)));
}

Result<PolygonDocument> PolygonDocument::make_cone(const ConeSpec &spec) {
  if (!positive_finite(spec.width) || !positive_finite(spec.height) ||
      !positive_finite(spec.depth)) {
    return Result<PolygonDocument>::failure(make_error(
        ErrorCode::InvalidArgument,
        "Cone dimensions must have positive width, height, and depth."));
  }
  if (!valid_subdivision_count(spec.side_count, 3)) {
    return Result<PolygonDocument>::failure(make_error(
        ErrorCode::InvalidArgument,
        "Cone side count must be between 3 and 128."));
  }

  quader_poly::Document document = quader_poly::make_cone({
      .width = spec.width,
      .height = spec.height,
      .depth = spec.depth,
      .side_count = spec.side_count,
      .capped = spec.capped,
      .flip_faces = spec.flip_faces,
  });
  prepare_primitive_document(document, {}, native_material_slot(spec.material));
  return Result<PolygonDocument>::success(
      PolygonDocumentNativeAccess::from_native(std::move(document)));
}

Result<PolygonDocument> PolygonDocument::make_sphere(const SphereSpec &spec) {
  if (!positive_finite(spec.width) || !positive_finite(spec.height) ||
      !positive_finite(spec.depth)) {
    return Result<PolygonDocument>::failure(make_error(
        ErrorCode::InvalidArgument,
        "Sphere dimensions must have positive width, height, and depth."));
  }
  quader_poly::SphereType native_type;
  switch (spec.type) {
  case SphereType::Uv:
    native_type = quader_poly::SphereType::Uv;
    if (!valid_subdivision_count(spec.radial_segments, 3)) {
      return Result<PolygonDocument>::failure(make_error(
          ErrorCode::InvalidArgument,
          "Sphere radial segment count must be between 3 and 128."));
    }
    if (!valid_subdivision_count(spec.rings, 2)) {
      return Result<PolygonDocument>::failure(
          make_error(ErrorCode::InvalidArgument,
                     "Sphere ring count must be between 2 and 128."));
    }
    break;
  case SphereType::Quad:
    native_type = quader_poly::SphereType::Quad;
    if (spec.subdivision < 1 || spec.subdivision > 6) {
      return Result<PolygonDocument>::failure(
          make_error(ErrorCode::InvalidArgument,
                     "Quad sphere subdivision must be between 1 and 6."));
    }
    break;
  default:
    return Result<PolygonDocument>::failure(
        make_error(ErrorCode::InvalidArgument, "Sphere type is invalid."));
  }

  quader_poly::Document document = quader_poly::make_sphere({
      .width = spec.width,
      .height = spec.height,
      .depth = spec.depth,
      .radial_segments = spec.radial_segments,
      .rings = spec.rings,
      .hemisphere = spec.hemisphere,
      .flip_faces = spec.flip_faces,
      .type = native_type,
      .subdivision = spec.subdivision,
  });
  prepare_primitive_document(document, {}, native_material_slot(spec.material));
  return Result<PolygonDocument>::success(
      PolygonDocumentNativeAccess::from_native(std::move(document)));
}

Result<PolygonDocument> PolygonDocument::make_torus(const TorusSpec &spec) {
  if (!positive_finite(spec.width) || !positive_finite(spec.height) ||
      !positive_finite(spec.depth)) {
    return Result<PolygonDocument>::failure(make_error(
        ErrorCode::InvalidArgument,
        "Torus dimensions must have positive width, height, and depth."));
  }
  if (!valid_subdivision_count(spec.major_segments, 3)) {
    return Result<PolygonDocument>::failure(make_error(
        ErrorCode::InvalidArgument,
        "Torus major segment count must be between 3 and 128."));
  }
  if (!valid_subdivision_count(spec.minor_segments, 3)) {
    return Result<PolygonDocument>::failure(make_error(
        ErrorCode::InvalidArgument,
        "Torus minor segment count must be between 3 and 128."));
  }
  if (!std::isfinite(spec.tube_radius_ratio) ||
      spec.tube_radius_ratio < 0.01F || spec.tube_radius_ratio > 0.49F) {
    return Result<PolygonDocument>::failure(make_error(
        ErrorCode::InvalidArgument,
        "Torus tube radius ratio must be between 0.01 and 0.49."));
  }

  quader_poly::Document document = quader_poly::make_torus({
      .width = spec.width,
      .height = spec.height,
      .depth = spec.depth,
      .major_segments = spec.major_segments,
      .minor_segments = spec.minor_segments,
      .tube_radius_ratio = spec.tube_radius_ratio,
      .flip_faces = spec.flip_faces,
  });
  prepare_primitive_document(document, {}, native_material_slot(spec.material));
  return Result<PolygonDocument>::success(
      PolygonDocumentNativeAccess::from_native(std::move(document)));
}

Result<PolygonDocument> PolygonDocument::make_face(std::span<const Vec3> points,
                                                   MaterialId material) {
  if (points.size() < 3U) {
    return Result<PolygonDocument>::failure(make_error(
        ErrorCode::InvalidArgument, "A polygon face needs at least three points."));
  }

  quader_poly::DocumentBuilder builder;
  std::vector<quader_poly::DocumentBuilder::VertexRef> vertices;
  vertices.reserve(points.size());
  for (Vec3 point : points) {
    vertices.push_back(builder.vertex(to_native(point)));
  }
  builder.face(vertices, native_material_slot(material));
  PolygonDocument document = PolygonDocumentNativeAccess::from_native(builder.take());
  Result<OperationResult> validation = document.validate();
  if (!validation.ok()) {
    return Result<PolygonDocument>::failure(validation.error());
  }
  if (!validation.value().success) {
    return Result<PolygonDocument>::failure(make_error(
        ErrorCode::ValidationFailed, "Polygon face validation failed."));
  }
  return Result<PolygonDocument>::success(std::move(document));
}

Result<PolygonDocument>
PolygonDocument::from_authored_topology(const AuthoredTopology &topology) {
  return import_authored_topology(topology, false);
}

Result<PolygonDocument>
PolygonDocument::from_persisted_topology(const AuthoredTopology &topology) {
  return import_authored_topology(topology, true);
}

Result<PolygonDocument> PolygonDocument::import_authored_topology(
    const AuthoredTopology &topology, bool preserve_storage) {
  if (!preserve_storage &&
      (topology.next_vertex_id == 0U || topology.next_face_id == 0U)) {
    return Result<PolygonDocument>::failure(make_error(
        ErrorCode::ValidationFailed,
        "Authored topology allocation cursors must be nonzero."));
  }

  quader_poly::Document document;
  document.next_vertex_id = topology.next_vertex_id;
  document.next_face_id = topology.next_face_id;
  document.vertices.reserve(topology.vertices.size());
  for (const AuthoredTopologyVertex &vertex : topology.vertices) {
    if (vertex.id.generation != 1U ||
        (!preserve_storage &&
         (!vertex.id.valid() || vertex.id.index >= topology.next_vertex_id ||
          !std::isfinite(vertex.position.x) ||
          !std::isfinite(vertex.position.y) ||
          !std::isfinite(vertex.position.z)))) {
      return Result<PolygonDocument>::failure(make_error(
          ErrorCode::ValidationFailed,
          "Authored topology contains an invalid vertex record."));
    }
    document.vertices.push_back({
        .id = vertex.id.index,
        .position = to_native(vertex.position),
    });
  }

  document.faces.reserve(topology.faces.size());
  for (const AuthoredTopologyFace &face : topology.faces) {
    if (face.id.generation != 1U ||
        (!preserve_storage &&
         (!face.id.valid() || face.id.index >= topology.next_face_id))) {
      return Result<PolygonDocument>::failure(make_error(
          ErrorCode::ValidationFailed,
          "Authored topology contains an invalid face record."));
    }
    quader_poly::Face native_face;
    native_face.id = face.id.index;
    native_face.material_slot = face.material_slot;
    native_face.normal_shading = preserve_storage
        ? static_cast<quader_poly::SurfaceShading>(face.shading)
        : native_shading(face.shading);
    native_face.texture_transform = to_native(face.texture_transform);
    if (face.circular_uv_interpolation.has_value()) {
      native_face.circular_uv_interpolation = to_native(*face.circular_uv_interpolation);
      if (!preserve_storage && !quader_poly::valid_circular_uv_interpolation(
                                   *native_face.circular_uv_interpolation)) {
        return Result<PolygonDocument>::failure(make_error(
            ErrorCode::ValidationFailed,
            "Authored topology contains invalid circular UV interpolation."));
      }
    }
    native_face.vertices.reserve(face.vertices.size());
    for (VertexId id : face.vertices) {
      if (id.generation != 1U || (!preserve_storage && !id.valid())) {
        return Result<PolygonDocument>::failure(make_error(
            ErrorCode::ValidationFailed,
            "Authored topology face contains an invalid vertex ID."));
      }
      native_face.vertices.push_back(id.index);
    }
    native_face.uvs.reserve(face.uvs.size());
    for (Vec2 uv : face.uvs) {
      if (!preserve_storage &&
          (!std::isfinite(uv.x) || !std::isfinite(uv.y))) {
        return Result<PolygonDocument>::failure(make_error(
            ErrorCode::ValidationFailed,
            "Authored topology contains a non-finite face UV."));
      }
      native_face.uvs.push_back(to_native(uv));
    }
    document.faces.push_back(std::move(native_face));
  }

  document.hard_normal_edges.reserve(topology.hard_normal_edges.size());
  for (EdgeKey edge : topology.hard_normal_edges) {
    if ((!preserve_storage && !edge.valid()) || edge.a.generation != 1U ||
        edge.b.generation != 1U) {
      return Result<PolygonDocument>::failure(make_error(
          ErrorCode::ValidationFailed,
          "Authored topology contains an invalid hard edge."));
    }
    document.hard_normal_edges.push_back(preserve_storage
        ? quader_poly::Edge{edge.a.index, edge.b.index}
        : quader_poly::make_edge(edge.a.index, edge.b.index));
  }

  document.soft_normal_edges.reserve(topology.soft_normal_edges.size());
  for (EdgeKey edge : topology.soft_normal_edges) {
    if ((!preserve_storage && !edge.valid()) || edge.a.generation != 1U ||
        edge.b.generation != 1U) {
      return Result<PolygonDocument>::failure(make_error(
          ErrorCode::ValidationFailed,
          "Authored topology contains an invalid soft edge."));
    }
    document.soft_normal_edges.push_back(preserve_storage
        ? quader_poly::Edge{edge.a.index, edge.b.index}
        : quader_poly::make_edge(edge.a.index, edge.b.index));
  }
  if (preserve_storage) {
    return Result<PolygonDocument>::success(
        PolygonDocumentNativeAccess::from_native(std::move(document)));
  }
  const auto edge_less = [](quader_poly::Edge left,
                            quader_poly::Edge right) noexcept {
    return left.a < right.a || (left.a == right.a && left.b < right.b);
  };
  std::ranges::sort(document.hard_normal_edges, edge_less);
  std::ranges::sort(document.soft_normal_edges, edge_less);

  PolygonDocument result = PolygonDocumentNativeAccess::from_native(
      std::move(document));
  // Imports are lossless authored-state transfers, including finite collapsed
  // faces produced by component edits. Geometric diagnostics remain available
  // through the stricter public validate() operation.
  Result<OperationResult> validation = validate_document(
      *result.impl_, quader_poly::PolygonDocumentValidationPolicy::AuthoredTopology);
  if (!validation.ok()) {
    return Result<PolygonDocument>::failure(validation.error());
  }
  return Result<PolygonDocument>::success(std::move(result));
}

std::size_t PolygonDocument::vertex_count() const {
  return impl_->document.vertex_count();
}

std::size_t PolygonDocument::face_count() const {
  return impl_->document.face_count();
}

bool PolygonDocument::selected_faces_have_open_perimeter() const {
  const quader_poly::FacePerimeterInfo perimeter =
      quader_poly::selected_faces_perimeter_info(*impl_->document,
                                                *impl_->selection);
  return !perimeter.empty() && perimeter.has_only_open_edges();
}

PolygonSelectionTopologyEligibility
PolygonDocument::selection_topology_eligibility() const {
  PolygonSelectionTopologyEligibility result;
  const quader_poly::Document &document = *impl_->document;
  const quader_poly::Selection &selection = *impl_->selection;
  if (selection.mode == quader_poly::SelectionMode::Face) {
    result.open_face_perimeter = selected_faces_have_open_perimeter();
    const std::unordered_set<quader_poly::ElementId> selected_face_ids(
        selection.faces.begin(), selection.faces.end());
    std::vector<const quader_poly::Face *> selected_faces;
    for (const auto &face : document.faces) {
      if (!selected_face_ids.contains(face.id)) {
        continue;
      }
      selected_faces.push_back(&face);
    }
    if (selected_faces.size() >= 2U) {
      const auto selected =
          quader_poly::document_internal::selected_face_copies(document, selection);
      const auto islands =
          quader_poly::document_internal::selected_face_island_boundaries(document, selected);
      const quader_poly::document_internal::VertexLookupIndex vertices(document);
      bool valid_boundaries = true;
      bool has_merged_island = false;
      for (const auto &island : islands) {
        if (island.face_ids.size() < 2U) {
          continue;
        }
        has_merged_island = true;
        quader_poly::Face merged;
        merged.vertices = island.vertices;
        quader_poly::document_internal::orient_face_toward_normal(document, merged, island.normal);
        if (!quader_poly::document_internal::face_triangulation_preserves_boundary(
                document, merged, vertices)) {
          valid_boundaries = false;
          break;
        }
      }
      result.combinable_selected_faces = has_merged_island && valid_boundaries;
      if (selected_faces.size() > 2U) {
        result.bridge_face_pair =
            islands.size() == 2U &&
            islands[0].edges.size() == islands[1].edges.size() &&
            islands[0].vertices.size() >= 3U &&
            islands[1].vertices.size() >= 3U;
      }
    }
    if (selected_faces.size() == 2U) {
      result.bridge_face_pair =
          selected_faces[0]->vertices.size() ==
          selected_faces[1]->vertices.size();
    } else if (selected_faces.size() == 1U) {
      result.single_selected_face_edge_count = static_cast<std::uint32_t>(
          selected_faces.front()->vertices.size());
    }
    return result;
  }

  std::map<std::pair<quader_poly::ElementId, quader_poly::ElementId>, int>
      face_edge_counts;
  std::set<quader_poly::ElementId> face_vertices;
  const std::unordered_set<quader_poly::ElementId> selected_vertex_ids(
      selection.vertices.begin(), selection.vertices.end());
  for (const auto &face : document.faces) {
    std::size_t selected_vertex_count = 0U;
    for (std::size_t i = 0; i < face.vertices.size(); ++i) {
      const auto vertex = face.vertices[i];
      if (selection.mode == quader_poly::SelectionMode::Vertex) {
        face_vertices.insert(vertex);
        selected_vertex_count += selected_vertex_ids.contains(vertex);
      } else if (selection.mode == quader_poly::SelectionMode::Edge) {
        const auto edge = quader_poly::make_edge(
            vertex, face.vertices[(i + 1U) % face.vertices.size()]);
        ++face_edge_counts[{edge.a, edge.b}];
      }
    }
    if (selected_vertex_count >= 2U) {
      result.selected_vertices_share_face = true;
    }
  }
  result.selected_vertices_belong_to_faces =
      !selection.vertices.empty() &&
      std::ranges::all_of(selection.vertices, [&face_vertices](auto vertex) {
        return face_vertices.contains(vertex);
      });

  if (selection.mode == quader_poly::SelectionMode::Edge &&
      !selection.edges.empty()) {
    result.bridge_edge_pair = selection.edges.size() == 2U;
    result.selected_edges_belong_to_faces = true;
    result.selected_edges_are_interior = true;
    result.selected_edges_are_open_boundary = true;
    std::uint32_t open_boundary_count = 0U;
    for (const auto &selected : selection.edges) {
      const auto edge = quader_poly::make_edge(selected.a, selected.b);
      const auto found = face_edge_counts.find({edge.a, edge.b});
      const int incidence = found == face_edge_counts.end() ? 0 : found->second;
      result.selected_edges_belong_to_faces &= incidence >= 1;
      result.selected_edges_are_interior &= incidence == 2;
      result.selected_edges_are_open_boundary &= incidence == 1;
      open_boundary_count += incidence == 1;
    }
    result.three_selected_open_boundary_edges = open_boundary_count >= 3U;
    result.bridge_edge_pair &= result.selected_edges_are_open_boundary;
  }
  return result;
}

PolygonSelectionTopologyEligibility
PolygonDocument::whole_mesh_topology_eligibility() const {
  PolygonSelectionTopologyEligibility result;
  if (impl_->document->faces.empty()) {
    return result;
  }
  std::map<std::pair<quader_poly::ElementId, quader_poly::ElementId>, int>
      incidence;
  for (const auto &face : impl_->document->faces) {
    for (std::size_t i = 0; i < face.vertices.size(); ++i) {
      const auto edge = quader_poly::make_edge(
          face.vertices[i], face.vertices[(i + 1U) % face.vertices.size()]);
      ++incidence[{edge.a, edge.b}];
    }
  }
  bool has_open_edge = false;
  bool has_nonmanifold_edge = false;
  result.whole_mesh_closed = true;
  for (const auto &[edge, count] : incidence) {
    (void)edge;
    result.whole_mesh_closed &= count == 2;
    has_open_edge |= count == 1;
    has_nonmanifold_edge |= count > 2;
  }
  result.whole_mesh_has_open_perimeter =
      has_open_edge && !has_nonmanifold_edge;
  return result;
}

PolygonContentIdentity PolygonDocument::content_identity() const noexcept {
  return impl_->document.identity();
}

std::uint64_t PolygonDocument::content_revision() const {
  return impl_->content_revision;
}

std::uint64_t PolygonDocument::selection_revision() const {
  return impl_->selection_revision;
}

bool PolygonDocument::shares_topology_with(
    const PolygonDocument &other) const noexcept {
  return impl_ && other.impl_ && impl_->document == other.impl_->document;
}

[[nodiscard]] const std::vector<quader_poly::Edge> &
cached_document_edges(const PolygonDocumentImpl &impl) {
  if (impl.edge_cache == nullptr ||
      impl.edge_cache_document != impl.document.identity()) {
    impl.edge_cache =
        std::make_shared<const std::vector<quader_poly::Edge>>(
            quader_poly::document_edges(*impl.document));
    impl.edge_cache_document = impl.document.identity();
  }
  return *impl.edge_cache;
}

[[nodiscard]] bool has_same_selection_domain(
    const PolygonDocumentImpl &left, const PolygonDocumentImpl &right,
    SelectionKind kind) {
  if (left.document == right.document) {
    return true;
  }

  switch (kind) {
  case SelectionKind::Vertex: {
    if (left.document->vertices.size() != right.document->vertices.size()) {
      return false;
    }
    for (std::size_t index = 0U; index < left.document->vertices.size();
         ++index) {
      if (left.document->vertices[index].id !=
          right.document->vertices[index].id) {
        return false;
      }
    }
    return true;
  }
  case SelectionKind::Edge: {
    if (left.document->vertices.size() != right.document->vertices.size() ||
        left.document->faces.size() != right.document->faces.size()) {
      return false;
    }
    for (std::size_t index = 0U; index < left.document->vertices.size();
         ++index) {
      if (left.document->vertices[index].id !=
          right.document->vertices[index].id) {
        return false;
      }
    }
    for (std::size_t index = 0U; index < left.document->faces.size(); ++index) {
      if (left.document->faces[index].vertices !=
          right.document->faces[index].vertices) {
        return false;
      }
    }
    return true;
  }
  case SelectionKind::Face: {
    if (left.document->faces.size() != right.document->faces.size()) {
      return false;
    }
    for (std::size_t index = 0U; index < left.document->faces.size(); ++index) {
      if (left.document->faces[index].id != right.document->faces[index].id) {
        return false;
      }
    }
    return true;
  }
  case SelectionKind::Object:
    return false;
  }
  return false;
}

bool PolygonDocument::shares_selection_with(
    const PolygonDocument &other) const noexcept {
  return impl_ && other.impl_ && impl_->selection == other.impl_->selection;
}

Result<PolygonDocumentSelectionReplacement>
PolygonDocumentNativeAccess::prepare_selection_replacement(
    const PolygonDocument &document,
    const PolygonSelectionSnapshot &snapshot) {
  QDR_PROFILE_SCOPE("SDK polygon prepare direct selection replacement");
  if (snapshot.kind == SelectionKind::Object) {
    return Result<PolygonDocumentSelectionReplacement>::failure(make_error(
        ErrorCode::InvalidArgument,
        "A component selection batch requires component selections."));
  }
  if (snapshot.has_active && !snapshot_active_is_member(snapshot)) {
    return Result<PolygonDocumentSelectionReplacement>::failure(make_error(
        ErrorCode::InvalidArgument,
        "The active component must belong to the replacement selection."));
  }
  if (native_selection_matches_snapshot(*document.impl_->selection,
                                        snapshot)) {
    return Result<PolygonDocumentSelectionReplacement>::success({
        .selection = document.impl_->selection,
        .selection_revision = document.impl_->selection_revision,
        .changed = false,
    });
  }
  quader_poly::Selection replacement;
  std::string message;
  {
    QDR_PROFILE_SCOPE("SDK polygon direct selection validate");
    switch (snapshot.kind) {
    case SelectionKind::Vertex: {
      replacement.mode = quader_poly::SelectionMode::Vertex;
      std::unordered_set<quader_poly::ElementId> live_ids;
      const bool use_live_index = selection_validation_uses_index(
          snapshot.vertices.size(), document.impl_->document->vertices.size());
      if (use_live_index) {
        live_ids.reserve(document.impl_->document->vertices.size());
        for (const quader_poly::Vertex &vertex :
             document.impl_->document->vertices) {
          live_ids.emplace(vertex.id);
        }
      }
      replacement.vertices.reserve(snapshot.vertices.size());
      for (const VertexId id : snapshot.vertices) {
        Result<OperationResult> validation = validate_vertex_id(
            *document.impl_->document, id,
            use_live_index ? &live_ids : nullptr);
        if (!validation.ok()) {
          return Result<PolygonDocumentSelectionReplacement>::failure(
              std::move(validation).error());
        }
        replacement.vertices.push_back(id.index);
      }
      message = "Vertices selected.";
      break;
    }
    case SelectionKind::Edge: {
      replacement.mode = quader_poly::SelectionMode::Edge;
      const std::vector<quader_poly::Edge> &live_edges =
          cached_document_edges(*document.impl_);
      std::unordered_set<quader_poly::ElementId> live_vertex_ids;
      const bool use_live_index = selection_validation_uses_index(
          snapshot.edges.size(), document.impl_->document->vertices.size());
      if (use_live_index) {
        live_vertex_ids.reserve(document.impl_->document->vertices.size());
        for (const quader_poly::Vertex &vertex :
             document.impl_->document->vertices) {
          live_vertex_ids.emplace(vertex.id);
        }
      }
      replacement.edges.reserve(snapshot.edges.size());
      for (const EdgeKey edge : snapshot.edges) {
        Result<OperationResult> validation = validate_edge_key(
            *document.impl_->document, edge, live_edges,
            use_live_index ? &live_vertex_ids : nullptr);
        if (!validation.ok()) {
          return Result<PolygonDocumentSelectionReplacement>::failure(
              std::move(validation).error());
        }
        replacement.edges.push_back(native_edge(edge));
      }
      message = "Edges selected.";
      break;
    }
    case SelectionKind::Face: {
      replacement.mode = quader_poly::SelectionMode::Face;
      std::unordered_set<quader_poly::ElementId> live_ids;
      const bool use_live_index = selection_validation_uses_index(
          snapshot.faces.size(), document.impl_->document->faces.size());
      if (use_live_index) {
        live_ids.reserve(document.impl_->document->faces.size());
        for (const quader_poly::Face &face : document.impl_->document->faces) {
          live_ids.emplace(face.id);
        }
      }
      replacement.faces.reserve(snapshot.faces.size());
      for (const FaceId id : snapshot.faces) {
        Result<OperationResult> validation = validate_face_id(
            *document.impl_->document, id,
            use_live_index ? &live_ids : nullptr);
        if (!validation.ok()) {
          return Result<PolygonDocumentSelectionReplacement>::failure(
              std::move(validation).error());
        }
        replacement.faces.push_back(id.index);
      }
      message = "Faces selected.";
      break;
    }
    case SelectionKind::Object:
      break;
    }
    const quader_poly::Selection requested_active =
        active_selection_from_snapshot(snapshot);
    if (snapshot.has_active &&
        !active_selection_is_selected(replacement, requested_active)) {
      return Result<PolygonDocumentSelectionReplacement>::failure(make_error(
          ErrorCode::InvalidArgument,
          "The active component must belong to the replacement selection."));
    }
    update_active_selection(
        replacement, snapshot.has_active ? &requested_active : nullptr);
    if (!native_selection_matches_snapshot(replacement, snapshot)) {
      return Result<PolygonDocumentSelectionReplacement>::failure(make_error(
          ErrorCode::InvalidArgument,
          "A component selection batch requires canonical exact snapshots."));
    }
  }

  std::shared_ptr<const quader_poly::Selection> immutable_selection;
  {
    QDR_PROFILE_SCOPE("SDK polygon direct selection allocate payload");
    immutable_selection = std::make_shared<const quader_poly::Selection>(
        std::move(replacement));
  }
  return Result<PolygonDocumentSelectionReplacement>::success({
      .selection = std::move(immutable_selection),
      .selection_revision = document.impl_->selection_revision + 1U,
      .changed = true,
      .message = std::move(message),
  });
}

Result<PolygonDocumentSelectionReplacement>
PolygonDocumentNativeAccess::prepare_selection_replacement_reusing(
    const PolygonDocument &document,
    const PolygonDocument &validated_document,
    const PolygonSelectionSnapshot &snapshot,
    std::shared_ptr<const quader_poly::Selection> canonical_selection) {
  QDR_PROFILE_SCOPE("SDK polygon reuse validated selection payload");
  if (native_selection_matches_snapshot(*document.impl_->selection,
                                        snapshot)) {
    return Result<PolygonDocumentSelectionReplacement>::success({
        .selection = document.impl_->selection,
        .selection_revision = document.impl_->selection_revision,
        .changed = false,
    });
  }
  if (snapshot.kind == SelectionKind::Object ||
      canonical_selection == nullptr ||
      selection_kind_from_native(canonical_selection->mode) != snapshot.kind ||
      canonical_selection->vertices.size() != snapshot.vertices.size() ||
      canonical_selection->edges.size() != snapshot.edges.size() ||
      canonical_selection->faces.size() != snapshot.faces.size() ||
      !native_active_selection_matches_snapshot(*canonical_selection,
                                                snapshot)) {
    return Result<PolygonDocumentSelectionReplacement>::failure(make_error(
        ErrorCode::InvalidArgument,
        "A component selection batch requires canonical exact snapshots."));
  }

  // The payload was already validated against validated_document. Equal live
  // identifier domains make that proof transferable without rebuilding hash
  // indexes or revalidating every selected element for every identical mesh.
  // The comparison is exact (not hash based), so stale-ID rejection remains
  // unchanged for documents with a different live domain.
  if (has_same_selection_domain(*document.impl_, *validated_document.impl_,
                                snapshot.kind)) {
    const char *message = snapshot.kind == SelectionKind::Vertex
                              ? "Vertices selected."
                          : snapshot.kind == SelectionKind::Edge
                              ? "Edges selected."
                              : "Faces selected.";
    return Result<PolygonDocumentSelectionReplacement>::success({
        .selection = std::move(canonical_selection),
        .selection_revision = document.impl_->selection_revision + 1U,
        .changed = true,
        .message = message,
    });
  }

  std::string message;
  switch (snapshot.kind) {
  case SelectionKind::Vertex: {
    std::unordered_set<quader_poly::ElementId> live_ids;
    const bool use_live_index = selection_validation_uses_index(
        snapshot.vertices.size(), document.impl_->document->vertices.size());
    if (use_live_index) {
      live_ids.reserve(document.impl_->document->vertices.size());
      for (const quader_poly::Vertex &vertex :
           document.impl_->document->vertices) {
        live_ids.emplace(vertex.id);
      }
    }
    for (const VertexId id : snapshot.vertices) {
      Result<OperationResult> validation = validate_vertex_id(
          *document.impl_->document, id,
          use_live_index ? &live_ids : nullptr);
      if (!validation.ok()) {
        return Result<PolygonDocumentSelectionReplacement>::failure(
            std::move(validation).error());
      }
    }
    message = "Vertices selected.";
    break;
  }
  case SelectionKind::Edge: {
    const std::vector<quader_poly::Edge> &live_edges =
        cached_document_edges(*document.impl_);
    std::unordered_set<quader_poly::ElementId> live_vertex_ids;
    const bool use_live_index = selection_validation_uses_index(
        snapshot.edges.size(), document.impl_->document->vertices.size());
    if (use_live_index) {
      live_vertex_ids.reserve(document.impl_->document->vertices.size());
      for (const quader_poly::Vertex &vertex :
           document.impl_->document->vertices) {
        live_vertex_ids.emplace(vertex.id);
      }
    }
    for (const EdgeKey edge : snapshot.edges) {
      Result<OperationResult> validation = validate_edge_key(
          *document.impl_->document, edge, live_edges,
          use_live_index ? &live_vertex_ids : nullptr);
      if (!validation.ok()) {
        return Result<PolygonDocumentSelectionReplacement>::failure(
            std::move(validation).error());
      }
    }
    message = "Edges selected.";
    break;
  }
  case SelectionKind::Face: {
    std::unordered_set<quader_poly::ElementId> live_ids;
    const bool use_live_index = selection_validation_uses_index(
        snapshot.faces.size(), document.impl_->document->faces.size());
    if (use_live_index) {
      live_ids.reserve(document.impl_->document->faces.size());
      for (const quader_poly::Face &face : document.impl_->document->faces) {
        live_ids.emplace(face.id);
      }
    }
    for (const FaceId id : snapshot.faces) {
      Result<OperationResult> validation = validate_face_id(
          *document.impl_->document, id,
          use_live_index ? &live_ids : nullptr);
      if (!validation.ok()) {
        return Result<PolygonDocumentSelectionReplacement>::failure(
            std::move(validation).error());
      }
    }
    message = "Faces selected.";
    break;
  }
  case SelectionKind::Object:
    break;
  }

  return Result<PolygonDocumentSelectionReplacement>::success({
      .selection = std::move(canonical_selection),
      .selection_revision = document.impl_->selection_revision + 1U,
      .changed = true,
      .message = std::move(message),
  });
}

bool PolygonDocument::has_same_geometry(
    const PolygonDocument &other) const {
  return impl_ && other.impl_ &&
         quader_poly::has_same_geometry(*impl_->document,
                                        *other.impl_->document);
}

std::vector<VertexId> PolygonDocument::vertex_ids() const {
  std::vector<VertexId> ids;
  ids.reserve(impl_->document->vertices.size());
  for (const quader_poly::Vertex &vertex : impl_->document->vertices) {
    ids.push_back(vertex_id(vertex.id));
  }
  return ids;
}

std::vector<EdgeKey> PolygonDocument::edge_ids() const {
  std::vector<EdgeKey> ids;
  const std::vector<quader_poly::Edge> &native_edges =
      cached_document_edges(*impl_);
  ids.reserve(native_edges.size());
  for (const quader_poly::Edge edge : native_edges) {
    ids.push_back(edge_id(edge));
  }
  return ids;
}

std::vector<FaceId> PolygonDocument::face_ids() const {
  std::vector<FaceId> ids;
  ids.reserve(impl_->document->faces.size());
  for (const quader_poly::Face &face : impl_->document->faces) {
    ids.push_back(face_id(face.id));
  }
  return ids;
}

std::vector<EdgeKey> PolygonDocument::hard_edge_ids() const {
  std::vector<EdgeKey> ids;
  ids.reserve(impl_->document->hard_normal_edges.size());
  for (const quader_poly::Edge edge : impl_->document->hard_normal_edges) {
    ids.push_back(edge_id(edge));
  }
  return ids;
}

std::vector<EdgeKey> PolygonDocument::soft_edge_ids() const {
  std::vector<EdgeKey> ids;
  ids.reserve(impl_->document->soft_normal_edges.size());
  for (const quader_poly::Edge edge : impl_->document->soft_normal_edges) {
    ids.push_back(edge_id(edge));
  }
  return ids;
}

std::vector<EdgeKey> PolygonDocument::default_edge_ids() const {
  std::vector<EdgeKey> ids = edge_ids();
  const std::vector<EdgeKey> hard = hard_edge_ids();
  const std::vector<EdgeKey> soft = soft_edge_ids();
  std::erase_if(ids, [&](const EdgeKey edge) {
    return std::ranges::find(hard, edge) != hard.end() ||
           std::ranges::find(soft, edge) != soft.end();
  });
  return ids;
}

std::vector<FaceId>
PolygonDocument::face_ids_by_material_slot(std::uint32_t material_slot) const {
  std::vector<FaceId> ids;
  for (const quader_poly::Face &face : impl_->document->faces) {
    if (face.material_slot == material_slot) {
      ids.push_back(face_id(face.id));
    }
  }
  return ids;
}

std::vector<FaceId>
PolygonDocument::face_ids_by_normal(Vec3 direction, float tolerance) const {
  const quader::QVec3 native_direction = to_native(direction);
  const float length = std::sqrt((native_direction.x * native_direction.x) +
                                 (native_direction.y * native_direction.y) +
                                 (native_direction.z * native_direction.z));
  if (length <= 0.000001F) {
    return {};
  }
  const quader::QVec3 normalized{
      native_direction.x / length,
      native_direction.y / length,
      native_direction.z / length,
  };
  std::vector<FaceId> ids;
  for (const quader_poly::Face &face : impl_->document->faces) {
    const quader::QVec3 normal =
        quader_poly::face_normal(*impl_->document, face);
    const float facing = (normal.x * normalized.x) +
                         (normal.y * normalized.y) +
                         (normal.z * normalized.z);
    if (facing >= 1.0F - tolerance) {
      ids.push_back(face_id(face.id));
    }
  }
  return ids;
}

PolygonSelectionSnapshot PolygonDocument::selection() const {
  PolygonSelectionSnapshot snapshot;
  snapshot.kind = selection_kind_from_native(impl_->selection->mode);
  snapshot.has_active = impl_->selection->has_active;
  snapshot.vertices.reserve(impl_->selection->vertices.size());
  for (const quader_poly::ElementId id : impl_->selection->vertices) {
    snapshot.vertices.push_back(vertex_id(id));
  }
  snapshot.edges.reserve(impl_->selection->edges.size());
  for (const quader_poly::Edge edge : impl_->selection->edges) {
    snapshot.edges.push_back(edge_id(edge));
  }
  snapshot.faces.reserve(impl_->selection->faces.size());
  for (const quader_poly::ElementId id : impl_->selection->faces) {
    snapshot.faces.push_back(face_id(id));
  }
  if (impl_->selection->has_active) {
    snapshot.active_vertex = vertex_id(impl_->selection->active_vertex);
    snapshot.active_edge = edge_id(impl_->selection->active_edge);
    snapshot.active_face = face_id(impl_->selection->active_face);
  }
  return snapshot;
}

Result<AuthoredPolygonPayload> PolygonDocument::authored_payload() const {
  AuthoredPolygonPayload payload;
  payload.content_revision = impl_->content_revision;
  payload.vertices.reserve(impl_->document->vertices.size());
  payload.positions.reserve(impl_->document->vertices.size());
  for (const quader_poly::Vertex &vertex : impl_->document->vertices) {
    payload.vertices.push_back(vertex_id(vertex.id));
    payload.positions.push_back(from_native(vertex.position));
  }

  payload.faces.reserve(impl_->document->faces.size());
  for (const quader_poly::Face &face : impl_->document->faces) {
    AuthoredPolygonFacePayload face_payload;
    face_payload.id = face_id(face.id);
    face_payload.material = material_id(face.material_slot);
    face_payload.vertices.reserve(face.vertices.size());
    for (const quader_poly::ElementId id : face.vertices) {
      face_payload.vertices.push_back(vertex_id(id));
    }
    payload.faces.push_back(std::move(face_payload));
  }
  return Result<AuthoredPolygonPayload>::success(std::move(payload));
}

Result<AuthoredTopology> PolygonDocument::authored_topology() const {
  AuthoredTopology topology;
  topology.next_vertex_id = impl_->document->next_vertex_id;
  topology.next_face_id = impl_->document->next_face_id;
  topology.vertices.reserve(impl_->document->vertices.size());
  for (const quader_poly::Vertex &vertex : impl_->document->vertices) {
    topology.vertices.push_back({
        .id = vertex_id(vertex.id),
        .position = from_native(vertex.position),
    });
  }
  topology.faces.reserve(impl_->document->faces.size());
  for (const quader_poly::Face &face : impl_->document->faces) {
    AuthoredTopologyFace authored_face;
    authored_face.id = face_id(face.id);
    authored_face.material_slot = face.material_slot;
    authored_face.shading = static_cast<AuthoredSurfaceShading>(face.normal_shading);
    authored_face.texture_transform = from_native(face.texture_transform);
    if (face.circular_uv_interpolation.has_value()) {
      authored_face.circular_uv_interpolation = from_native(*face.circular_uv_interpolation);
    }
    authored_face.vertices.reserve(face.vertices.size());
    for (quader_poly::ElementId id : face.vertices) {
      authored_face.vertices.push_back(vertex_id(id));
    }
    authored_face.uvs.reserve(face.uvs.size());
    for (quader::QVec2 uv : face.uvs) {
      authored_face.uvs.push_back(from_native(uv));
    }
    topology.faces.push_back(std::move(authored_face));
  }
  topology.hard_normal_edges.reserve(impl_->document->hard_normal_edges.size());
  for (quader_poly::Edge edge : impl_->document->hard_normal_edges) {
    topology.hard_normal_edges.push_back(
        {vertex_id(edge.a), vertex_id(edge.b)});
  }
  topology.soft_normal_edges.reserve(impl_->document->soft_normal_edges.size());
  for (quader_poly::Edge edge : impl_->document->soft_normal_edges) {
    topology.soft_normal_edges.push_back(
        {vertex_id(edge.a), vertex_id(edge.b)});
  }
  return Result<AuthoredTopology>::success(std::move(topology));
}

Result<std::vector<FaceTextureSettings>> PolygonDocument::face_texture_settings(
    std::span<const FaceId> faces) const {
  std::vector<FaceTextureSettings> settings;
  settings.reserve(faces.size());
  for (const FaceId id : faces) {
    const auto validation = validate_face_id(*impl_->document, id);
    if (!validation.ok()) {
      return Result<std::vector<FaceTextureSettings>>::failure(
          validation.error());
    }
    const auto *face = quader_poly::find_face(*impl_->document, id.index);
    settings.push_back({.face = id,
                        .texture_transform = from_native(face->texture_transform),
                        .material_slot = face->material_slot});
  }
  return Result<std::vector<FaceTextureSettings>>::success(std::move(settings));
}

Result<MeshPayload> PolygonDocument::compile_mesh() const {
  const auto snapshot = impl_->document.read_snapshot();
  const quader_poly::CompiledMesh compiled =
      quader_poly::compile_document(snapshot.view());
  MeshPayload payload;
  payload.content_revision = impl_->content_revision;
  payload.vertices.reserve(compiled.vertices.size());
  for (const quader_poly::CompiledVertex &vertex : compiled.vertices) {
    payload.vertices.push_back({
        .position = from_native(vertex.position),
        .normal = from_native(vertex.normal),
        .uv0 = from_native(vertex.uv0),
        .color = vertex.color,
        .source_vertex = vertex_id(vertex.source_vertex_id),
        .uv_carrier = vertex.uv_carrier,
        .uv_geometric_normal = from_native(vertex.uv_geometric_normal),
        .circular_uv_index = vertex.circular_uv_index,
    });
  }
  payload.indices = compiled.indices;
  payload.circular_uv_interpolations.reserve(compiled.circular_uv_interpolations.size());
  for (const auto &interpolation : compiled.circular_uv_interpolations) {
    payload.circular_uv_interpolations.push_back(from_native(interpolation));
  }
  payload.primitives.reserve(compiled.primitives.size());
  for (const quader_poly::CompiledPrimitive &primitive : compiled.primitives) {
    payload.primitives.push_back({
        .index_offset = primitive.index_offset,
        .index_count = primitive.index_count,
        .material = material_id(primitive.material_slot),
    });
  }
  return Result<MeshPayload>::success(std::move(payload));
}

Result<OperationResult> PolygonDocument::validate() const {
  return validate_document(
      *impl_, quader_poly::PolygonDocumentValidationPolicy::Strict);
}

Result<OperationResult> PolygonDocumentNativeAccess::validate_authored_topology(
    const PolygonDocument &document) {
  return validate_document(
      *document.impl_, quader_poly::PolygonDocumentValidationPolicy::AuthoredTopology);
}

Result<OperationResult>
PolygonDocument::set_selection_mode(SelectionMode mode) {
  quader_poly::Selection selection = *impl_->selection;
  const quader_poly::SelectionMode previous = selection.mode;
  switch (mode) {
  case SelectionMode::Vertex:
    quader_poly::convert_selection_mode(*impl_->document, selection,
                                        quader_poly::SelectionMode::Vertex);
    break;
  case SelectionMode::Edge:
    quader_poly::convert_selection_mode(*impl_->document, selection,
                                        quader_poly::SelectionMode::Edge);
    break;
  case SelectionMode::Face:
    quader_poly::convert_selection_mode(*impl_->document, selection,
                                        quader_poly::SelectionMode::Face);
    break;
  }

  const bool changed = selection.mode != previous;
  commit_selection(*impl_, std::move(selection), changed);
  return operation_success(selection_only_result(changed, "Selection mode updated."));
}

Result<OperationResult> PolygonDocument::clear_selection() {
  quader_poly::Selection selection = *impl_->selection;
  const bool changed = !selection.empty() || selection.has_active;
  selection.clear();
  commit_selection(*impl_, std::move(selection), changed);
  return operation_success(selection_only_result(changed, "Selection cleared."));
}

namespace {

[[nodiscard]] Result<OperationResult> apply_vertex_selection_edit(
    PolygonDocumentImpl &impl, std::span<const VertexId> ids,
    SelectionEdit edit, std::optional<VertexId> requested_active) {
  std::unordered_set<quader_poly::ElementId> live_ids;
  const bool use_live_index = selection_validation_uses_index(
      ids.size() + (requested_active.has_value() ? 1U : 0U),
      impl.document->vertices.size());
  if (use_live_index) {
    live_ids.reserve(impl.document->vertices.size());
    for (const quader_poly::Vertex &vertex : impl.document->vertices) {
      live_ids.emplace(vertex.id);
    }
  }
  for (const VertexId id : ids) {
    Result<OperationResult> validation = validate_vertex_id(
        *impl.document, id, use_live_index ? &live_ids : nullptr);
    if (!validation.ok()) {
      return validation;
    }
  }
  if (requested_active.has_value()) {
    Result<OperationResult> validation = validate_vertex_id(
        *impl.document, *requested_active,
        use_live_index ? &live_ids : nullptr);
    if (!validation.ok()) {
      return validation;
    }
    if (edit == SelectionEdit::Replace &&
        std::ranges::find(ids, *requested_active) == ids.end()) {
      return Result<OperationResult>::failure(make_error(
          ErrorCode::InvalidArgument,
          "The active vertex must belong to the replacement selection."));
    }
  }

  const quader_poly::Selection previous = *impl.selection;
  quader_poly::Selection selection = previous;
  if (edit == SelectionEdit::Replace ||
      selection.mode != quader_poly::SelectionMode::Vertex) {
    selection.clear();
    selection.mode = quader_poly::SelectionMode::Vertex;
  }
  std::vector<quader_poly::ElementId> native_ids;
  native_ids.reserve(ids.size());
  for (const VertexId id : ids) {
    native_ids.push_back(id.index);
  }
  apply_edit(selection.vertices,
             std::span<const quader_poly::ElementId>(native_ids.data(),
                                                     native_ids.size()),
             edit);

  quader_poly::Selection requested;
  if (requested_active.has_value()) {
    requested.mode = quader_poly::SelectionMode::Vertex;
    requested.has_active = true;
    requested.active_kind = quader_poly::ElementKind::Vertex;
    requested.active_vertex = requested_active->index;
  }
  update_active_selection(
      selection, requested_active.has_value() ? &requested : nullptr,
      edit == SelectionEdit::Replace ? nullptr : &previous);
  commit_selection(impl, std::move(selection), true);
  return operation_success(selection_only_result(true, "Vertices selected."));
}

[[nodiscard]] Result<OperationResult> apply_edge_selection_edit(
    PolygonDocumentImpl &impl, std::span<const EdgeKey> edges,
    SelectionEdit edit, std::optional<EdgeKey> requested_active) {
  const std::vector<quader_poly::Edge> &live_edges =
      cached_document_edges(impl);
  std::unordered_set<quader_poly::ElementId> live_vertex_ids;
  const bool use_live_index = selection_validation_uses_index(
      edges.size() + (requested_active.has_value() ? 1U : 0U),
      impl.document->vertices.size());
  if (use_live_index) {
    live_vertex_ids.reserve(impl.document->vertices.size());
    for (const quader_poly::Vertex &vertex : impl.document->vertices) {
      live_vertex_ids.emplace(vertex.id);
    }
  }
  for (const EdgeKey edge : edges) {
    Result<OperationResult> validation = validate_edge_key(
        *impl.document, edge, live_edges,
        use_live_index ? &live_vertex_ids : nullptr);
    if (!validation.ok()) {
      return validation;
    }
  }
  if (requested_active.has_value()) {
    Result<OperationResult> validation = validate_edge_key(
        *impl.document, *requested_active, live_edges,
        use_live_index ? &live_vertex_ids : nullptr);
    if (!validation.ok()) {
      return validation;
    }
    if (edit == SelectionEdit::Replace &&
        std::ranges::find(edges, *requested_active) == edges.end()) {
      return Result<OperationResult>::failure(make_error(
          ErrorCode::InvalidArgument,
          "The active edge must belong to the replacement selection."));
    }
  }

  const quader_poly::Selection previous = *impl.selection;
  quader_poly::Selection selection = previous;
  if (edit == SelectionEdit::Replace ||
      selection.mode != quader_poly::SelectionMode::Edge) {
    selection.clear();
    selection.mode = quader_poly::SelectionMode::Edge;
  }
  std::vector<quader_poly::Edge> native_edges;
  native_edges.reserve(edges.size());
  for (const EdgeKey edge : edges) {
    native_edges.push_back(native_edge(edge));
  }
  apply_edit(selection.edges,
             std::span<const quader_poly::Edge>(native_edges.data(),
                                                native_edges.size()),
             edit);

  quader_poly::Selection requested;
  if (requested_active.has_value()) {
    requested.mode = quader_poly::SelectionMode::Edge;
    requested.has_active = true;
    requested.active_kind = quader_poly::ElementKind::Edge;
    requested.active_edge = native_edge(*requested_active);
  }
  update_active_selection(
      selection, requested_active.has_value() ? &requested : nullptr,
      edit == SelectionEdit::Replace ? nullptr : &previous);
  commit_selection(impl, std::move(selection), true);
  return operation_success(selection_only_result(true, "Edges selected."));
}

[[nodiscard]] Result<OperationResult> apply_face_selection_edit(
    PolygonDocumentImpl &impl, std::span<const FaceId> ids,
    SelectionEdit edit, std::optional<FaceId> requested_active) {
  std::unordered_set<quader_poly::ElementId> live_ids;
  const bool use_live_index = selection_validation_uses_index(
      ids.size() + (requested_active.has_value() ? 1U : 0U),
      impl.document->faces.size());
  if (use_live_index) {
    live_ids.reserve(impl.document->faces.size());
    for (const quader_poly::Face &face : impl.document->faces) {
      live_ids.emplace(face.id);
    }
  }
  for (const FaceId id : ids) {
    Result<OperationResult> validation = validate_face_id(
        *impl.document, id, use_live_index ? &live_ids : nullptr);
    if (!validation.ok()) {
      return validation;
    }
  }
  if (requested_active.has_value()) {
    Result<OperationResult> validation = validate_face_id(
        *impl.document, *requested_active,
        use_live_index ? &live_ids : nullptr);
    if (!validation.ok()) {
      return validation;
    }
    if (edit == SelectionEdit::Replace &&
        std::ranges::find(ids, *requested_active) == ids.end()) {
      return Result<OperationResult>::failure(make_error(
          ErrorCode::InvalidArgument,
          "The active face must belong to the replacement selection."));
    }
  }

  const quader_poly::Selection previous = *impl.selection;
  quader_poly::Selection selection = previous;
  if (edit == SelectionEdit::Replace ||
      selection.mode != quader_poly::SelectionMode::Face) {
    selection.clear();
    selection.mode = quader_poly::SelectionMode::Face;
  }
  std::vector<quader_poly::ElementId> native_ids;
  native_ids.reserve(ids.size());
  for (const FaceId id : ids) {
    native_ids.push_back(id.index);
  }
  apply_edit(selection.faces,
             std::span<const quader_poly::ElementId>(native_ids.data(),
                                                     native_ids.size()),
             edit);

  quader_poly::Selection requested;
  if (requested_active.has_value()) {
    requested.mode = quader_poly::SelectionMode::Face;
    requested.has_active = true;
    requested.active_kind = quader_poly::ElementKind::Face;
    requested.active_face = requested_active->index;
  }
  update_active_selection(
      selection, requested_active.has_value() ? &requested : nullptr,
      edit == SelectionEdit::Replace ? nullptr : &previous);
  commit_selection(impl, std::move(selection), true);
  return operation_success(selection_only_result(true, "Faces selected."));
}

} // namespace

Result<OperationResult>
PolygonDocument::select_vertices(std::span<const VertexId> ids,
                                 SelectionEdit edit) {
  return apply_vertex_selection_edit(*impl_, ids, edit, std::nullopt);
}

Result<OperationResult>
PolygonDocument::select_edges(std::span<const EdgeKey> edges,
                              SelectionEdit edit) {
  return apply_edge_selection_edit(*impl_, edges, edit, std::nullopt);
}

Result<OperationResult>
PolygonDocument::select_faces(std::span<const FaceId> ids,
                              SelectionEdit edit) {
  return apply_face_selection_edit(*impl_, ids, edit, std::nullopt);
}

Result<OperationResult> PolygonDocument::select_vertex(VertexId id) {
  Result<OperationResult> validation = validate_vertex_id(*impl_->document, id);
  if (!validation.ok()) {
    return validation;
  }
  quader_poly::Selection selection = *impl_->selection;
  selection.clear();
  selection.mode = quader_poly::SelectionMode::Vertex;
  selection.vertices = {id.index};
  selection.has_active = true;
  selection.active_kind = quader_poly::ElementKind::Vertex;
  selection.active_vertex = id.index;
  commit_selection(*impl_, std::move(selection), true);
  return operation_success(selection_only_result(true, "Vertex selected."));
}

Result<OperationResult> PolygonDocument::select_edge(EdgeKey edge) {
  Result<OperationResult> validation = validate_edge_key(*impl_->document, edge);
  if (!validation.ok()) {
    return validation;
  }
  quader_poly::Selection selection = *impl_->selection;
  selection.clear();
  selection.mode = quader_poly::SelectionMode::Edge;
  selection.edges = {quader_poly::make_edge(edge.a.index, edge.b.index)};
  selection.has_active = true;
  selection.active_kind = quader_poly::ElementKind::Edge;
  selection.active_edge = selection.edges.front();
  commit_selection(*impl_, std::move(selection), true);
  return operation_success(selection_only_result(true, "Edge selected."));
}

Result<OperationResult> PolygonDocument::select_face(FaceId id) {
  Result<OperationResult> validation = validate_face_id(*impl_->document, id);
  if (!validation.ok()) {
    return validation;
  }
  quader_poly::Selection selection = *impl_->selection;
  selection.clear();
  selection.mode = quader_poly::SelectionMode::Face;
  selection.faces = {id.index};
  selection.has_active = true;
  selection.active_kind = quader_poly::ElementKind::Face;
  selection.active_face = id.index;
  commit_selection(*impl_, std::move(selection), true);
  return operation_success(selection_only_result(true, "Face selected."));
}

Result<OperationResult> PolygonDocument::select_all_vertices() {
  const std::vector<VertexId> ids = vertex_ids();
  return select_vertices(ids, SelectionEdit::Replace);
}

Result<OperationResult> PolygonDocument::select_all_edges() {
  const std::vector<EdgeKey> ids = edge_ids();
  return select_edges(ids, SelectionEdit::Replace);
}

Result<OperationResult> PolygonDocument::select_all_faces() {
  const std::vector<FaceId> ids = face_ids();
  return select_faces(ids, SelectionEdit::Replace);
}

Result<OperationResult>
PolygonDocument::invert_selection(const PolygonSelectionUniverse &eligible) {
  if (!valid_selection_mode(eligible.mode)) {
    return Result<OperationResult>::failure(make_error(
        ErrorCode::InvalidArgument,
        "Selection inversion requires a valid component mode."));
  }
  const quader_poly::SelectionMode requested_mode =
      native_selection_mode(eligible.mode);
  if (impl_->selection->mode != requested_mode) {
    return Result<OperationResult>::failure(make_error(
        ErrorCode::InvalidArgument,
        "Selection inversion universe mode must match the current selection mode."));
  }
  Result<quader_poly::ComponentSelectionUniverse> native_universe =
      native_selection_universe(*impl_, eligible);
  if (!native_universe.ok()) {
    return Result<OperationResult>::failure(native_universe.error());
  }

  quader_poly::Selection selection = *impl_->selection;
  const bool changed = quader_poly::invert_component_selection(
      *impl_->document, selection, native_universe.value());
  commit_selection(*impl_, std::move(selection), changed);
  return operation_success(
      selection_only_result(changed, changed ? "Selection inverted."
                                             : "Selection unchanged."));
}

Result<OperationResult>
PolygonDocument::apply_selection(const PolygonSelectionSnapshot &selection,
                                 SelectionEdit edit) {
  if (selection.kind == SelectionKind::Vertex) {
    return apply_vertex_selection_edit(
        *impl_, selection.vertices, edit,
        selection.has_active ? std::optional{selection.active_vertex}
                             : std::nullopt);
  }
  if (selection.kind == SelectionKind::Edge) {
    return apply_edge_selection_edit(
        *impl_, selection.edges, edit,
        selection.has_active ? std::optional{selection.active_edge}
                             : std::nullopt);
  }
  if (selection.kind == SelectionKind::Face) {
    return apply_face_selection_edit(
        *impl_, selection.faces, edit,
        selection.has_active ? std::optional{selection.active_face}
                             : std::nullopt);
  }
  return Result<OperationResult>::failure(
      make_error(ErrorCode::InvalidArgument,
                 "Polygon documents do not own object selections."));
}

Result<OperationResult> PolygonDocument::translate_selection(Vec3 delta) {
  return apply_operation(
      *impl_, [delta](quader_poly::Document &document,
                      quader_poly::Selection &selection) {
        quader_poly::OperationResult result =
            quader_poly::translate_selection(document, selection,
                                             to_native(delta));
        return result;
      });
}

Result<OperationResult>
PolygonDocument::transform_selection(Transform3 transform) {
  return apply_operation(*impl_, [&](quader_poly::Document &document,
                                     quader_poly::Selection &selection) {
    quader_poly::OperationResult result =
        quader_poly::transform_selection(document, selection,
                                         to_native(transform));
    return result;
  });
}

Result<PreparedSelectionTransform>
PolygonDocument::prepare_selection_transform() const {
  QDR_PROFILE_SCOPE("PolygonDocument prepare selection transform");
  auto prepared = std::make_shared<PreparedSelectionTransformImpl>(*this);
  // Flatten at gesture preparation, never by chaining another coordinate
  // publication into a per-sample candidate.
  prepared->source_document = impl_->document.materialized_document();
  prepared->source_topology_identity = impl_->document.topology_identity();
  const auto &source = *prepared->source_document;
  prepared->has_circular_interpolation = std::any_of(source.faces.begin(), source.faces.end(),
      [](const auto &face) { return face.circular_uv_interpolation.has_value(); });
  auto selected_ids =
      quader_poly::selected_vertex_ids(source, *impl_->selection);
  std::ranges::sort(selected_ids);
  auto vertex_indices = std::make_shared<std::vector<std::size_t>>();
  auto positions = std::make_shared<std::vector<VertexPositionUpdate>>();
  vertex_indices->reserve(selected_ids.size());
  positions->reserve(selected_ids.size());
  for (std::size_t index = 0U; index < source.vertices.size(); ++index) {
    const auto &vertex = source.vertices[index];
    if (!finite_position(vertex.position)) {
      return Result<PreparedSelectionTransform>::failure(make_error(
          ErrorCode::InvalidArgument,
          "Transform source must contain finite coordinates."));
    }
    if (std::ranges::binary_search(selected_ids, vertex.id)) {
      vertex_indices->push_back(index);
      positions->push_back({.vertex = vertex_id(vertex.id),
                            .position = from_native(vertex.position)});
    }
  }
  prepared->vertex_indices = std::move(vertex_indices);
  if (!positions->empty()) {
    prepared->source_positions = std::move(positions);
  }
  PreparedSelectionTransform value;
  value.impl_ = std::move(prepared);
  return Result<PreparedSelectionTransform>::success(std::move(value));
}

Result<SelectionTransformEvaluation>
PreparedSelectionTransform::evaluate_translation(Vec3 delta) const {
  if (!valid() || !finite_position(to_native(delta))) {
    return Result<SelectionTransformEvaluation>::failure(make_error(
        ErrorCode::InvalidArgument,
        "Translation requires a preparation and a finite delta."));
  }
  return evaluate_prepared_selection_transform(
      *impl_, [native_delta = to_native(delta)](quader::QVec3 position) {
        return position + native_delta;
      });
}

Result<SelectionTransformEvaluation>
PreparedSelectionTransform::evaluate_transform(Transform3 transform) const {
  const auto native_transform = to_native(transform);
  if (!valid() || !finite_position(native_transform.x_axis) ||
      !finite_position(native_transform.y_axis) ||
      !finite_position(native_transform.z_axis) ||
      !finite_position(native_transform.origin)) {
    return Result<SelectionTransformEvaluation>::failure(make_error(
        ErrorCode::InvalidArgument,
        "Transform requires a preparation and finite coefficients."));
  }
  return evaluate_prepared_selection_transform(
      *impl_, [native_transform](quader::QVec3 position) {
        return quader_poly::document_internal::transform_point(native_transform,
                                                                position);
      });
}

PreparedSelectionTransformWorkStats
PreparedSelectionTransform::work_stats() const noexcept {
  if (!impl_) {
    return {};
  }
  return {
      .coordinate_publication_count =
          impl_->work_state->coordinate_publication_count.load(std::memory_order_relaxed),
      .evaluated_vertex_count =
          impl_->work_state->evaluated_vertex_count.load(std::memory_order_relaxed),
      .topology_materialization_count =
          impl_->work_state->topology_materialization_count.load(std::memory_order_relaxed),
      .coordinate_snapshot_count =
          impl_->work_state->coordinate_snapshot_count.load(std::memory_order_relaxed),
  };
}

Result<OperationResult> PolygonDocument::set_vertex_positions(
    std::span<const VertexPositionUpdate> updates) {
  QDR_PROFILE_FUNCTION();
  OperationResult result;
  result.message = "Vertex positions unchanged.";
  if (updates.empty()) {
    return operation_success(std::move(result));
  }

  bool strictly_increasing_ids = true;
  quader_poly::ElementId previous_id = quader_poly::kInvalidElementId;
  for (const VertexPositionUpdate &update : updates) {
    if (!update.vertex.valid() || update.vertex.generation != 1U) {
      return Result<OperationResult>::failure(
          invalid_id_error("Vertex position update ID is invalid."));
    }
    if (!std::isfinite(update.position.x) ||
        !std::isfinite(update.position.y) ||
        !std::isfinite(update.position.z)) {
      return Result<OperationResult>::failure(make_error(
          ErrorCode::InvalidArgument,
          "Vertex position updates must contain finite coordinates."));
    }
    strictly_increasing_ids =
        strictly_increasing_ids &&
        (previous_id == quader_poly::kInvalidElementId ||
         previous_id < update.vertex.index);
    previous_id = update.vertex.index;
  }

  quader_poly::Document document = *impl_->document;
  result.affected.vertices.reserve(updates.size());
  if (strictly_increasing_ids) {
    QDR_PROFILE_SCOPE("PolygonDocument set ordered vertex positions");
    for (const VertexPositionUpdate &update : updates) {
      quader_poly::Vertex *vertex =
          quader_poly::find_vertex(document, update.vertex.index);
      if (vertex == nullptr) {
        return Result<OperationResult>::failure(
            invalid_id_error("Vertex position update ID is not live."));
      }
      const quader::QVec3 desired = to_native(update.position);
      if (vertex->position.x == desired.x &&
          vertex->position.y == desired.y &&
          vertex->position.z == desired.z) {
        continue;
      }
      vertex->position = desired;
      result.affected.vertices.push_back(update.vertex);
    }
  } else {
    QDR_PROFILE_SCOPE("PolygonDocument set unordered vertex positions");
    std::unordered_map<quader_poly::ElementId, quader::QVec3> positions;
    positions.reserve(updates.size());
    for (const VertexPositionUpdate &update : updates) {
      if (!positions.emplace(update.vertex.index, to_native(update.position))
               .second) {
        return Result<OperationResult>::failure(make_error(
            ErrorCode::InvalidArgument,
            "Vertex position updates contain a duplicate vertex ID."));
      }
    }
    std::size_t matched = 0U;
    for (quader_poly::Vertex &vertex : document.vertices) {
      const auto position = positions.find(vertex.id);
      if (position == positions.end()) {
        continue;
      }
      ++matched;
      const quader::QVec3 desired = position->second;
      if (vertex.position.x == desired.x && vertex.position.y == desired.y &&
          vertex.position.z == desired.z) {
        continue;
      }
      vertex.position = desired;
      result.affected.vertices.push_back(vertex_id(vertex.id));
    }
    if (matched != positions.size()) {
      return Result<OperationResult>::failure(
          invalid_id_error("Vertex position update ID is not live."));
    }
  }
  if (result.affected.vertices.empty()) {
    return operation_success(std::move(result));
  }

  result.changed = true;
  result.message = "Vertex positions changed.";
  result.modified = result.affected;
  result.dirty.geometry = true;
  // Position edits keep authored UVs attached to their corners. Faces without
  // authored corners continue to evaluate their procedural mapping.
  PolygonDocumentNativeAccess::reconcile_circular_uv_interpolations(*impl_->document, document);
  impl_->document =
      std::make_shared<const quader_poly::Document>(std::move(document));
  bump_content(*impl_, true);
  return operation_success(std::move(result));
}

Result<OperationResult>
PolygonDocument::assign_selected_face_material_slot(std::uint32_t material_slot) {
  return apply_operation(
      *impl_,
      [&](quader_poly::Document &document, quader_poly::Selection &selection) {
        return quader_poly::assign_selected_face_material_slot(
            document, selection, material_slot);
      },
      true);
}

Result<OperationResult>
PolygonDocument::assign_all_face_material_slot(std::uint32_t material_slot) {
  return apply_operation(
      *impl_,
      [material_slot](quader_poly::Document &document,
                      quader_poly::Selection &) {
        bool changed = false;
        for (quader_poly::Face &face : document.faces) {
          if (face.material_slot == material_slot) {
            continue;
          }
          face.material_slot = material_slot;
          changed = true;
        }
        return quader_poly::OperationResult{
            .changed = changed,
            .message = changed ? "Material assigned."
                               : "All faces already use that material slot.",
        };
      },
      true);
}

Result<OperationResult>
PolygonDocument::assign_face_material_slot(FaceId id,
                                           std::uint32_t material_slot) {
  Result<OperationResult> validation = validate_face_id(*impl_->document, id);
  if (!validation.ok()) {
    return validation;
  }
  return apply_operation(
      *impl_,
      [id, material_slot](quader_poly::Document &document,
                          quader_poly::Selection &) {
        return quader_poly::assign_face_material_slot(document, id.index,
                                                      material_slot);
      },
      true);
}

Result<OperationResult> PolygonDocument::set_face_texture_transforms(
    std::span<const FaceTextureTransformUpdate> updates) {
  OperationResult result;
  result.message = "Face texture settings unchanged.";
  std::unordered_set<std::uint32_t> updated_ids;
  updated_ids.reserve(updates.size());
  std::vector<std::pair<std::size_t, quader_poly::FaceTextureTransform>> changes;
  changes.reserve(updates.size());

  for (const FaceTextureTransformUpdate &update : updates) {
    const auto validation = validate_face_id(*impl_->document, update.face);
    if (!validation.ok()) {
      return validation;
    }
    if (!updated_ids.insert(update.face.index).second) {
      return Result<OperationResult>::failure(make_error(
          ErrorCode::InvalidArgument,
          "Face texture updates contain a duplicate face ID."));
    }
    const auto *face = quader_poly::find_face(*impl_->document, update.face.index);
    auto desired = face->texture_transform;
    if (update.scale_x.has_value()) {
      desired.scale.x = *update.scale_x;
    }
    if (update.scale_y.has_value()) {
      desired.scale.y = *update.scale_y;
    }
    if (update.shift_x.has_value()) {
      desired.shift_uv.x = *update.shift_x;
    }
    if (update.shift_y.has_value()) {
      desired.shift_uv.y = *update.shift_y;
    }
    if (update.rotation_degrees.has_value()) {
      desired.rotation_degrees = *update.rotation_degrees;
    }
    if (!quader_poly::valid_face_texture_transform(desired)) {
      return Result<OperationResult>::failure(make_error(
          ErrorCode::InvalidArgument,
          "Face texture settings require finite values and nonzero scale."));
    }
    if (desired == face->texture_transform) {
      continue;
    }
    const auto normal = quader_poly::face_normal(*impl_->document, *face);
    for (std::size_t corner = 0; corner < face->vertices.size(); ++corner) {
      const auto *vertex =
          quader_poly::find_vertex(*impl_->document, face->vertices[corner]);
      if (vertex == nullptr) {
        return Result<OperationResult>::failure(make_error(
            ErrorCode::ValidationFailed,
            "Face texture update references an invalid vertex."));
      }
      const auto base = face->uvs.size() == face->vertices.size()
                            ? face->uvs[corner]
                            : quader_poly::generated_texture_uv(
                                  vertex->position, normal);
      const auto uv = quader_poly::apply_face_texture_transform(base, desired);
      if (!std::isfinite(uv.x) || !std::isfinite(uv.y)) {
        return Result<OperationResult>::failure(make_error(
            ErrorCode::InvalidArgument,
            "Face texture settings would produce non-finite coordinates."));
      }
    }
    changes.emplace_back(
        static_cast<std::size_t>(face - impl_->document->faces.data()), desired);
    result.affected.faces.push_back(update.face);
  }
  if (changes.empty()) {
    return operation_success(std::move(result));
  }
  quader_poly::Document document = *impl_->document;
  for (const auto &[index, transform] : changes) {
    document.faces[index].texture_transform = transform;
  }
  result.changed = true;
  result.message = "Face texture settings changed.";
  result.modified = result.affected;
  result.dirty.geometry = true;
  impl_->document =
      std::make_shared<const quader_poly::Document>(std::move(document));
  bump_content(*impl_, true);
  return operation_success(std::move(result));
}

Result<OperationResult>
PolygonDocument::snap_selected_vertices_to_active() {
  return apply_operation(*impl_, quader_poly::snap_selected_vertices_to_active);
}

Result<OperationResult>
PolygonDocument::merge_selected_vertices_to_active() {
  return apply_operation(*impl_, quader_poly::merge_selected_vertices_to_active);
}

Result<OperationResult>
PolygonDocument::merge_selected_vertices_to_center() {
  return apply_operation(*impl_, quader_poly::merge_selected_vertices_to_center);
}

Result<OperationResult>
PolygonDocument::merge_selected_vertices_by_distance(float tolerance) {
  return apply_operation(
      *impl_,
      [tolerance](quader_poly::Document &document,
                  quader_poly::Selection &selection) {
        return quader_poly::merge_selected_vertices_by_distance(
            document, selection, tolerance);
      });
}

Result<OperationResult> PolygonDocument::remove_double_vertices() {
  return apply_operation(*impl_, quader_poly::remove_double_vertices);
}

Result<OperationResult>
PolygonDocument::auto_merge_selected_vertices(float tolerance) {
  const PolygonSelectionSnapshot source = selection();
  Result<OperationResult> result = apply_operation(
      *impl_, [tolerance](quader_poly::Document &document,
                          quader_poly::Selection &selection) {
        return quader_poly::auto_merge_selected_vertices(document, selection,
                                                         tolerance);
      });
  if (result.ok() && result.value().changed) {
    const PolygonSelectionSnapshot destination = selection();
    result.value().selection_remap.source = {
        .vertices = source.vertices,
        .edges = source.edges,
        .faces = source.faces,
    };
    result.value().selection_remap.destination = {
        .vertices = destination.vertices,
        .edges = destination.edges,
        .faces = destination.faces,
    };
  }
  return result;
}

Result<OperationResult>
PolygonDocument::bevel_selected_vertices(float distance) {
  return apply_operation(
      *impl_,
      [distance](quader_poly::Document &document,
                 quader_poly::Selection &selection) {
        return quader_poly::bevel_selected_vertices(document, selection,
                                                    distance);
      });
}

Result<OperationResult> PolygonDocument::connect_selected_vertices() {
  return apply_operation(*impl_, quader_poly::connect_selected_vertices);
}

Result<OperationResult> PolygonDocument::dissolve_selected_vertices() {
  return apply_operation(*impl_, quader_poly::dissolve_selected_vertices);
}

Result<OperationResult> PolygonDocument::connect_selected_edges() {
  return apply_operation(*impl_, quader_poly::connect_selected_edges);
}

Result<OperationResult> PolygonDocument::snap_selected_edges_to_active() {
  const quader_poly::Document &document = *impl_->document;
  const quader_poly::Selection &selection = *impl_->selection;
  if (selection.mode != quader_poly::SelectionMode::Edge ||
      selection.edges.empty()) {
    return Result<OperationResult>::failure(make_error(
        ErrorCode::InvalidArgument, "Select edges before snapping."));
  }
  if (!selection.has_active ||
      selection.active_kind != quader_poly::ElementKind::Edge) {
    return Result<OperationResult>::failure(make_error(
        ErrorCode::InvalidArgument, "Edge Snap needs an active selected edge."));
  }

  const quader_poly::Edge target_edge =
      quader_poly::make_edge(selection.active_edge.a, selection.active_edge.b);
  const bool active_selected =
      std::ranges::any_of(selection.edges, [&](quader_poly::Edge edge) {
        return quader_poly::make_edge(edge.a, edge.b) == target_edge;
      });
  if (!active_selected) {
    return Result<OperationResult>::failure(make_error(
        ErrorCode::InvalidArgument, "Edge Snap needs an active selected edge."));
  }

  const quader_poly::Vertex *target_a =
      quader_poly::find_vertex(document, target_edge.a);
  const quader_poly::Vertex *target_b =
      quader_poly::find_vertex(document, target_edge.b);
  if (target_a == nullptr || target_b == nullptr) {
    return Result<OperationResult>::failure(
        invalid_id_error("Active edge vertices were not found."));
  }

  std::unordered_map<quader_poly::ElementId, quader::QVec3> targets;
  bool has_other_edge = false;
  for (quader_poly::Edge edge : selection.edges) {
    edge = quader_poly::make_edge(edge.a, edge.b);
    if (edge.a == quader_poly::kInvalidElementId ||
        edge.b == quader_poly::kInvalidElementId || edge.a == edge.b ||
        edge == target_edge) {
      continue;
    }

    const quader_poly::Vertex *source_a =
        quader_poly::find_vertex(document, edge.a);
    const quader_poly::Vertex *source_b =
        quader_poly::find_vertex(document, edge.b);
    if (source_a == nullptr || source_b == nullptr) {
      continue;
    }

    has_other_edge = true;
    const float direct_cost =
        distance_squared(source_a->position, target_a->position) +
        distance_squared(source_b->position, target_b->position);
    const float flipped_cost =
        distance_squared(source_a->position, target_b->position) +
        distance_squared(source_b->position, target_a->position);
    const quader::QVec3 desired_a =
        direct_cost <= flipped_cost ? target_a->position : target_b->position;
    const quader::QVec3 desired_b =
        direct_cost <= flipped_cost ? target_b->position : target_a->position;

    const auto add_target =
        [&targets](quader_poly::ElementId vertex_id,
                   quader::QVec3 desired) -> Result<OperationResult> {
      const auto existing = targets.find(vertex_id);
      if (existing == targets.end()) {
        targets[vertex_id] = desired;
        return operation_success(selection_only_result(false, {}));
      }
      if (distance_squared(existing->second, desired) > 0.000001F) {
        return Result<OperationResult>::failure(make_error(
            ErrorCode::InvalidArgument,
            "Edge Snap has conflicting selected-edge endpoint targets."));
      }
      return operation_success(selection_only_result(false, {}));
    };

    if (edge.a != target_edge.a && edge.a != target_edge.b) {
      Result<OperationResult> added = add_target(edge.a, desired_a);
      if (!added.ok()) {
        return added;
      }
    }
    if (edge.b != target_edge.a && edge.b != target_edge.b) {
      Result<OperationResult> added = add_target(edge.b, desired_b);
      if (!added.ok()) {
        return added;
      }
    }
  }

  if (!has_other_edge) {
    return Result<OperationResult>::failure(make_error(
        ErrorCode::InvalidArgument, "Select at least one other edge to snap."));
  }

  OperationResult result;
  result.message = "Selected edges snapped to active edge.";
  quader_poly::Document updated_document = document;
  for (const auto &[vertex_id, desired] : targets) {
    quader_poly::Vertex *vertex =
        quader_poly::find_vertex(updated_document, vertex_id);
    if (vertex == nullptr) {
      continue;
    }
    if (distance_squared(vertex->position, desired) <= 0.000001F) {
      continue;
    }
    vertex->position = desired;
    result.changed = true;
    result.affected.vertices.push_back(make_id<VertexTag>(vertex_id));
  }

  if (!result.changed) {
    result.message = "Selected edges are already at the active edge.";
    return operation_success(std::move(result));
  }

  result.modified = result.affected;
  result.dirty.geometry = true;
  result.dirty.selection = true;
  impl_->document = std::make_shared<const quader_poly::Document>(
      std::move(updated_document));
  bump_content(*impl_, true);
  bump_selection(*impl_, true);
  return operation_success(std::move(result));
}

Result<OperationResult> PolygonDocument::split_selected_edges() {
  return apply_operation(*impl_, quader_poly::split_selected_edges);
}

Result<OperationResult> PolygonDocument::dissolve_selected_edges() {
  return apply_operation(*impl_, quader_poly::dissolve_selected_edges);
}

Result<OperationResult> PolygonDocument::merge_selected_edges() {
  return apply_operation(*impl_, quader_poly::merge_selected_edges);
}

Result<OperationResult> PolygonDocument::collapse_selected_edges() {
  return apply_operation(*impl_, quader_poly::collapse_selected_edges);
}

Result<OperationResult>
PolygonDocument::fill_hole_from_selected_edges() {
  return apply_operation(*impl_, quader_poly::fill_hole_from_selected_edges);
}

Result<OperationResult>
PolygonDocument::bevel_selected_edges(EdgeBevelSpec settings) {
  quader_poly::EdgeBevelSettings native_settings;
  native_settings.width = settings.width;
  native_settings.profile = settings.profile;
  native_settings.segments = settings.segments;
  return apply_operation(
      *impl_,
      [native_settings](quader_poly::Document &document,
                        quader_poly::Selection &selection) {
        return quader_poly::bevel_selected_edges(document, selection,
                                                 native_settings);
      });
}

Result<OperationResult> PolygonDocument::bridge_selected_edges(int steps) {
  return bridge_selected_edges(BridgeOptions{.steps = steps});
}

Result<OperationResult>
PolygonDocument::bridge_selected_edges(BridgeOptions options) {
  if (!std::isfinite(options.start_tension) ||
      !std::isfinite(options.end_tension) || options.start_tension < 0.0F ||
      options.start_tension > 3.0F || options.end_tension < 0.0F ||
      options.end_tension > 3.0F) {
    return Result<OperationResult>::failure(make_error(
        ErrorCode::InvalidArgument,
        "Bridge tensions must be finite and between zero and three."));
  }
  const quader_poly::BridgeOptions native_options{
      .steps = options.steps,
      .start_tension = options.start_tension,
      .end_tension = options.end_tension};
  return apply_operation(*impl_,
                         [native_options](quader_poly::Document &document,
                                          quader_poly::Selection &selection) {
                           return quader_poly::bridge_selected_edges(
                               document, selection, native_options);
                         });
}

Result<OperationResult> PolygonDocument::combine_selected_faces() {
  return apply_operation(*impl_, quader_poly::combine_selected_faces);
}

Result<OperationResult> PolygonDocument::collapse_selected_faces() {
  return apply_operation(*impl_, quader_poly::collapse_selected_faces);
}

Result<OperationResult> PolygonDocument::radial_align_selection() {
  return apply_operation(*impl_, quader_poly::radial_align_selection);
}

Result<OperationResult> PolygonDocument::flip_selected_face_normals() {
  return apply_operation(
      *impl_,
      [](quader_poly::Document &document, quader_poly::Selection &selection) {
        return quader_poly::flip_selected_face_normals(document, selection);
      });
}

Result<OperationResult>
PolygonDocument::recalculate_selected_face_normals(bool outside) {
  return apply_operation(
      *impl_,
      [outside](quader_poly::Document &document,
                quader_poly::Selection &selection) {
        return quader_poly::recalculate_selected_face_normals(
            document, selection, outside);
      });
}

Result<OperationResult> PolygonDocument::shade_selected_faces_smooth() {
  return apply_operation(
      *impl_,
      [](quader_poly::Document &document, quader_poly::Selection &selection) {
        return quader_poly::shade_selected_faces_smooth(document, selection);
      });
}

Result<OperationResult> PolygonDocument::shade_selected_faces_flat() {
  return apply_operation(
      *impl_,
      [](quader_poly::Document &document, quader_poly::Selection &selection) {
        return quader_poly::shade_selected_faces_flat(document, selection);
      });
}

Result<OperationResult> PolygonDocument::harden_selected_edge_normals() {
  return apply_operation(
      *impl_,
      [](quader_poly::Document &document, quader_poly::Selection &selection) {
        return quader_poly::harden_selected_edge_normals(document, selection);
      });
}

Result<OperationResult> PolygonDocument::soften_selected_edge_normals() {
  return apply_operation(
      *impl_,
      [](quader_poly::Document &document, quader_poly::Selection &selection) {
        return quader_poly::soften_selected_edge_normals(document, selection);
      });
}

Result<OperationResult>
PolygonDocument::reset_selected_edge_normals_to_default() {
  return apply_operation(
      *impl_,
      [](quader_poly::Document &document, quader_poly::Selection &selection) {
        return quader_poly::reset_selected_edge_normals_to_default(document,
                                                                   selection);
      });
}

Result<OperationResult> PolygonDocument::delete_selection() {
  return apply_operation(
      *impl_,
      [](quader_poly::Document &document, quader_poly::Selection &selection) {
        auto result = quader_poly::delete_selection(document, selection);
        if (result.success && result.changed) {
          // Delete removes every selected operand of the active kind. The
          // geometry helper borrows selection, so publish its cleared state
          // here with the topology and the selection dirty receipt.
          selection.clear();
        }
        return result;
      });
}

Result<OperationResult> PolygonDocument::insert_edge_loop(float factor) {
  return apply_operation(
      *impl_,
      [factor](quader_poly::Document &document,
               quader_poly::Selection &selection) {
        return quader_poly::insert_edge_loop(document, selection, factor);
      });
}

Result<OperationResult> PolygonDocument::knife_segment(KnifeTarget previous,
                                                       KnifeTarget current) {
  const quader_poly::KnifePointTarget native_previous =
      native_knife_target(previous);
  const quader_poly::KnifePointTarget native_current =
      native_knife_target(current);
  return apply_operation(
      *impl_,
      [native_previous, native_current](
          quader_poly::Document &document, quader_poly::Selection &selection) {
        return quader_poly::knife_segment(document, selection, native_previous,
                                          native_current);
      });
}

Result<OperationResult>
PolygonDocument::knife_stroke(std::span<const KnifeTarget> points,
                              std::span<const KnifeStrokeSegment> segments) {
  std::vector<quader_poly::KnifePointTarget> native_points;
  native_points.reserve(points.size());
  for (KnifeTarget point : points) {
    native_points.push_back(native_knife_target(point));
  }

  std::vector<quader_poly::KnifeStrokeSegment> native_segments;
  native_segments.reserve(segments.size());
  for (KnifeStrokeSegment segment : segments) {
    native_segments.push_back(native_knife_segment(segment));
  }

  return apply_operation(
      *impl_, [native_points = std::move(native_points),
               native_segments = std::move(native_segments)](
                  quader_poly::Document &document,
                  quader_poly::Selection &selection) {
        return quader_poly::knife_stroke(document, selection, native_points,
                                         native_segments);
      });
}

Result<OperationResult>
PolygonDocument::slice_selected_quads(int x_slices, int y_slices) {
  return apply_operation(
      *impl_,
      [x_slices, y_slices](quader_poly::Document &document,
                           quader_poly::Selection &selection) {
        return quader_poly::slice_selected_quads(document, selection, x_slices,
                                                y_slices);
      });
}

Result<OperationResult>
PolygonDocument::extrude_selected_elements(Vec3 offset,
                                           float closed_edge_ledge_size) {
  return apply_operation(
      *impl_,
      [offset, closed_edge_ledge_size](quader_poly::Document &document,
                                       quader_poly::Selection &selection) {
        return quader_poly::extrude_selected_elements(
            document, selection, to_native(offset), closed_edge_ledge_size);
      });
}

Result<OperationResult>
PolygonDocument::transform_extrude_selected_elements(Transform3 transform,
                                                      Vec3 pivot,
                                                      float extrusion_grid_size) {
  return apply_operation(
      *impl_,
      [transform, pivot, extrusion_grid_size](quader_poly::Document &document,
                         quader_poly::Selection &selection) {
        return quader_poly::transform_extrude_selected_elements(
            document, selection, to_native(transform), to_native(pivot),
            extrusion_grid_size);
      });
}

Result<OperationResult>
PolygonDocument::inset_selected_elements(Transform3 transform, Vec3 pivot) {
  return apply_operation(
      *impl_,
      [transform, pivot](quader_poly::Document &document,
                         quader_poly::Selection &selection) {
        return quader_poly::inset_selected_elements(
            document, selection, to_native(transform), to_native(pivot));
      });
}

Result<OperationResult> PolygonDocument::inset_selected_faces(float amount) {
  return apply_operation(
      *impl_,
      [amount](quader_poly::Document &document,
               quader_poly::Selection &selection) {
        return quader_poly::inset_selected_faces(document, selection, amount);
      });
}

Result<OperationResult> PolygonDocument::extrude_selected_faces(float distance) {
  return apply_operation(
      *impl_,
      [distance](quader_poly::Document &document,
                 quader_poly::Selection &selection) {
        return quader_poly::extrude_selected_faces(document, selection,
                                                   distance);
      });
}

Result<OperationResult> PolygonDocument::extrude_selected_faces(
    const FaceExtrudeOptions &options) {
  quader_poly::FaceExtrudeOptions native_options;
  native_options.mode = static_cast<quader_poly::FaceExtrudeMode>(options.mode);
  native_options.amount = options.amount;
  native_options.extrusion_from_local = to_native(options.extrusion_from_local);
  if (options.region_direction) {
    native_options.region_direction = to_native(*options.region_direction);
  }
  return apply_operation(
      *impl_, [&native_options](quader_poly::Document &document,
                               quader_poly::Selection &selection) {
        return quader_poly::extrude_selected_faces(document, selection,
                                                  native_options);
      });
}

Result<PreparedFaceExtrusion> PolygonDocument::prepare_face_extrusion(
    FaceExtrudeMode mode, const Transform3 &extrusion_from_local) const {
  auto prepared = std::make_shared<PreparedFaceExtrusionImpl>();
  const auto result = quader_poly::prepare_face_extrusion(
      *impl_->document, *impl_->selection,
      static_cast<quader_poly::FaceExtrudeMode>(mode),
      to_native(extrusion_from_local), prepared->preparation);
  if (!result.success) {
    return Result<PreparedFaceExtrusion>::failure(
        make_error(ErrorCode::ValidationFailed, result.message));
  }
  prepared->source = *this;
  PreparedFaceExtrusion value;
  value.impl_ = std::move(prepared);
  return Result<PreparedFaceExtrusion>::success(std::move(value));
}

Result<FaceExtrusionEvaluation> PreparedFaceExtrusion::evaluate(
    float amount, std::optional<Vec3> region_direction) const {
  if (!valid()) {
    return Result<FaceExtrusionEvaluation>::failure(
        make_error(ErrorCode::InvalidArgument, "Extrusion preparation is unavailable."));
  }
  std::optional<quader::QVec3> direction;
  if (region_direction) {
    direction = to_native(*region_direction);
  }
  quader_poly::Document document;
  quader_poly::Selection selection;
  const auto operated = quader_poly::evaluate_face_extrusion(
      impl_->preparation, amount, direction, document, selection);
  if (!operated.success) {
    return Result<FaceExtrusionEvaluation>::failure(
        make_error(ErrorCode::ValidationFailed, operated.message));
  }
  FaceExtrusionEvaluation value{.document = impl_->source,
                                .receipt = adapt_operation_result(operated)};
  if (operated.changed) {
    auto &candidate = *value.document.impl_;
    const bool selection_changed = !same_native_selection(*candidate.selection, selection);
    value.receipt.dirty.selection = selection_changed;
    candidate.document = std::make_shared<const quader_poly::Document>(std::move(document));
    candidate.selection = std::make_shared<const quader_poly::Selection>(std::move(selection));
    bump_content(candidate, true);
    bump_selection(candidate, selection_changed);
  }
  return Result<FaceExtrusionEvaluation>::success(std::move(value));
}

Result<PolygonFaceExtrudeFrame> PolygonDocument::selected_face_extrude_frame(
    const Transform3 &extrusion_from_local) const {
  quader_poly::FaceExtrudeFrame frame;
  const auto result = quader_poly::selected_face_extrude_frame(
      *impl_->document, *impl_->selection, to_native(extrusion_from_local), frame);
  if (!result.success) {
    return Result<PolygonFaceExtrudeFrame>::failure(
        make_error(ErrorCode::ValidationFailed, result.message));
  }
  return Result<PolygonFaceExtrudeFrame>::success({
      .center = from_native(frame.center),
      .summed_normal = from_native(frame.summed_normal),
      .active_normal = from_native(frame.active_normal),
      .face_count = frame.face_count,
      .vertex_count = frame.vertex_count,
  });
}

Result<OperationResult> PolygonDocument::detach_selected_faces() {
  return apply_operation(*impl_, quader_poly::detach_selected_faces);
}

Result<ExtractedPolygonDocument> PolygonDocument::extract_selected_faces() {
  quader_poly::Document source_document = *impl_->document;
  quader_poly::Selection source_selection = *impl_->selection;
  quader_poly::ExtractFacesResult native_result =
      quader_poly::extract_selected_faces(source_document, source_selection);
  if (!native_result.success) {
    return Result<ExtractedPolygonDocument>::failure(
        make_error(ErrorCode::ValidationFailed, std::move(native_result.message)));
  }
  OperationResult receipt;
  receipt.changed = native_result.changed;
  receipt.message = native_result.message;
  receipt.created = adapt_delta(native_result.created);
  receipt.deleted = adapt_delta(native_result.deleted);
  receipt.affected = adapt_delta(native_result.affected);
  receipt.modified = receipt.affected;
  receipt.dirty.geometry = native_result.changed;
  receipt.dirty.topology = native_result.changed;
  receipt.dirty.selection = native_result.changed;
  if (receipt.changed) {
    impl_->document = std::make_shared<const quader_poly::Document>(
        std::move(source_document));
    impl_->selection = std::make_shared<const quader_poly::Selection>(
        std::move(source_selection));
  }
  bump_content(*impl_, receipt.changed);
  bump_selection(*impl_, receipt.changed);
  ExtractedPolygonDocument result{
      .receipt = std::move(receipt),
      .document = PolygonDocumentNativeAccess::from_native(
          std::move(native_result.extracted_document),
          std::move(native_result.extracted_selection)),
  };
  return Result<ExtractedPolygonDocument>::success(std::move(result));
}

Result<SplitPolygonDocuments>
PolygonDocument::split_by_plane(const PlaneSplitSpec &spec) const {
  const quader_poly::PlaneCutResult native_result = quader_poly::plane_cut(
      *impl_->document,
      {
          .first_point = to_native(spec.a),
          .second_point = to_native(spec.b),
          .third_point = to_native(spec.c),
          .mode = quader_poly::PlaneCutMode::KeepBoth,
          .cap = spec.cap,
      });
  if (!native_result.changed) {
    return Result<SplitPolygonDocuments>::failure(make_error(
        ErrorCode::InvalidArgument,
        native_result.message.empty() ? "Plane cut could not split the mesh."
                                      : native_result.message));
  }
  OperationResult receipt;
  receipt.success = true;
  receipt.changed = true;
  receipt.message = native_result.message;
  receipt.created = adapt_delta(native_result.created);
  receipt.deleted = adapt_delta(native_result.deleted);
  receipt.affected = adapt_delta(native_result.affected);
  receipt.modified = receipt.affected;
  receipt.dirty.topology = true;
  receipt.dirty.geometry = true;
  receipt.dirty.selection = true;
  return Result<SplitPolygonDocuments>::success({
      .receipt = std::move(receipt),
      .front = PolygonDocumentNativeAccess::from_native(
          native_result.front_document, native_result.front_selection),
      .back = PolygonDocumentNativeAccess::from_native(
          native_result.back_document, native_result.back_selection),
      .split_count = native_result.split_count,
      .removed_count = native_result.removed_count,
  });
}

Result<OperationResult>
PolygonDocument::reflect_across_plane(Vec3 origin, Vec3 normal) {
  const float normal_length_squared =
      normal.x * normal.x + normal.y * normal.y + normal.z * normal.z;
  if (!std::isfinite(origin.x) || !std::isfinite(origin.y) ||
      !std::isfinite(origin.z) || !std::isfinite(normal_length_squared) ||
      normal_length_squared <= 0.0000001F) {
    return Result<OperationResult>::failure(make_error(
        ErrorCode::InvalidArgument,
        "Reflection requires a finite origin and non-zero plane normal."));
  }
  const float inverse_length = 1.0F / std::sqrt(normal_length_squared);
  const quader::QVec3 native_origin = to_native(origin);
  const quader::QVec3 native_normal = to_native(Vec3{
      normal.x * inverse_length, normal.y * inverse_length,
      normal.z * inverse_length});
  const auto source_document = impl_->document;
  return apply_operation(
      *impl_,
      [native_origin, native_normal, source_document](quader_poly::Document &document,
                                     quader_poly::Selection &) {
        for (quader_poly::Vertex &vertex : document.vertices) {
          const quader::QVec3 offset = vertex.position - native_origin;
          const float distance = offset.x * native_normal.x +
                                 offset.y * native_normal.y +
                                 offset.z * native_normal.z;
          vertex.position =
              vertex.position - native_normal * (2.0F * distance);
          if (!std::isfinite(vertex.position.x) ||
              !std::isfinite(vertex.position.y) ||
              !std::isfinite(vertex.position.z)) {
            return quader_poly::OperationResult::failure(
                "Reflection would create non-finite vertex positions.");
          }
        }
        const quader_poly::document_internal::VertexLookupIndex source_lookup(*source_document);
        const quader_poly::document_internal::VertexLookupIndex result_lookup(document);
        for (quader_poly::Face &face : document.faces) {
          const auto* source_face = quader_poly::find_face(*source_document, face.id);
          if (source_face == nullptr) {
            return quader_poly::OperationResult::failure(
                "Reflection source face identity is unavailable.");
          }
          if (!quader_poly::document_internal::reverse_reflected_face_winding(
                  *source_document, *source_face, document, face, source_lookup, result_lookup)) {
            // Keep each stored UV with its vertex when the reflected n-gon
            // requires a different interior triangulation.
            quader_poly::document_internal::reverse_face_winding(
                face, quader_poly::document_internal::FaceWindingChange::ReflectedGeometry);
          }
        }
        return quader_poly::OperationResult{
            !document.vertices.empty() || !document.faces.empty(), {}};
      });
}

Result<OperationResult>
PolygonDocument::cut_surface_by_plane(const PlaneSplitSpec &spec) {
  return apply_operation(
      *impl_, [&spec](quader_poly::Document &document,
                     quader_poly::Selection &selection) {
        return quader_poly::plane_surface_cut(
            document, selection,
            {
                .first_point = to_native(spec.a),
                .second_point = to_native(spec.b),
                .third_point = to_native(spec.c),
                .mode = quader_poly::PlaneCutMode::KeepBoth,
                .cap = spec.cap,
            });
      });
}

Result<OperationResult> PolygonDocument::cut_selected_faces_by_plane(
    const PlaneSplitSpec &spec, CutKeepMode keep) {
  if (keep != CutKeepMode::DiscardLeft && keep != CutKeepMode::DiscardRight &&
      keep != CutKeepMode::KeepBoth) {
    return Result<OperationResult>::failure(
        make_error(ErrorCode::InvalidArgument, "Cut keep mode is invalid."));
  }
  const auto source_selection = selection();
  auto result = apply_operation(
      *impl_, [&spec, keep](quader_poly::Document &document,
                           quader_poly::Selection &selection) {
        return quader_poly::plane_cut_selected_faces(
            document, selection,
            {.first_point = to_native(spec.a),
             .second_point = to_native(spec.b),
             .third_point = to_native(spec.c),
             .mode = keep == CutKeepMode::DiscardLeft
                         ? quader_poly::PlaneCutMode::KeepBack
                         : keep == CutKeepMode::DiscardRight
                               ? quader_poly::PlaneCutMode::KeepFront
                               : quader_poly::PlaneCutMode::KeepBoth,
             .cap = spec.cap});
      });
  if (result.ok() && result.value().changed) {
    result.value().selection_remap.source.faces = source_selection.faces;
    result.value().selection_remap.destination.faces = selection().faces;
  }
  return result;
}

Result<OperationResult>
PolygonDocument::cut_selected_edges_by_plane(const PlaneSplitSpec &spec) {
  const auto source_selection = selection();
  auto result = apply_operation(
      *impl_, [&spec](quader_poly::Document &document,
                     quader_poly::Selection &selection) {
        return quader_poly::plane_cut_selected_edges(
            document, selection,
            {.first_point = to_native(spec.a),
             .second_point = to_native(spec.b),
             .third_point = to_native(spec.c),
             .mode = quader_poly::PlaneCutMode::KeepBoth,
             .cap = false});
      });
  if (result.ok() && result.value().changed) {
    result.value().selection_remap.source.edges = source_selection.edges;
    result.value().selection_remap.destination.edges = selection().edges;
  }
  return result;
}

Result<OperationResult> PolygonDocument::bridge_selected_faces(int steps) {
  return bridge_selected_faces(BridgeOptions{.steps = steps});
}

Result<OperationResult>
PolygonDocument::bridge_selected_faces(BridgeOptions options) {
  if (!std::isfinite(options.start_tension) ||
      !std::isfinite(options.end_tension) || options.start_tension < 0.0F ||
      options.start_tension > 3.0F || options.end_tension < 0.0F ||
      options.end_tension > 3.0F) {
    return Result<OperationResult>::failure(make_error(
        ErrorCode::InvalidArgument,
        "Bridge tensions must be finite and between zero and three."));
  }
  const quader_poly::BridgeOptions native_options{
      .steps = options.steps,
      .start_tension = options.start_tension,
      .end_tension = options.end_tension};
  return apply_operation(*impl_,
                         [native_options](quader_poly::Document &document,
                                          quader_poly::Selection &selection) {
                           return quader_poly::bridge_selected_faces(
                               document, selection, native_options);
                         });
}

Result<OperationResult>
PolygonDocument::thicken_selected_faces(float thickness, bool from_center) {
  return apply_operation(
      *impl_,
      [thickness, from_center](quader_poly::Document &document,
                               quader_poly::Selection &selection) {
        return quader_poly::thicken_selected_faces(document, selection,
                                                   thickness, from_center);
      });
}

} // namespace quader::modeling
