////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <quader/modeling/mesh/polygon_document.hpp>

#include "polygon_document_native.hpp"

#include <mesh/polygon/document_builder.hpp>
#include <mesh/polygon/document_mesh_compilation.hpp>
#include <mesh/polygon/document_operations.hpp>
#include <mesh/polygon/document_selection.hpp>
#include <mesh/polygon/document_topology.hpp>
#include <mesh/polygon/document_validation.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <ranges>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>

namespace quader::modeling {
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
  sdk_result.success = true;
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

[[nodiscard]] Result<OperationResult>
validate_vertex_id(const quader_poly::Document &document, VertexId id) {
  if (!id.valid()) {
    return Result<OperationResult>::failure(invalid_id_error("Vertex ID is invalid."));
  }
  if (stale_generation(id)) {
    return Result<OperationResult>::failure(stale_id_error("Vertex ID is stale."));
  }
  if (quader_poly::find_vertex(document, id.index) == nullptr) {
    return Result<OperationResult>::failure(invalid_id_error("Vertex ID is not live."));
  }
  return operation_success(selection_only_result(false, {}));
}

[[nodiscard]] Result<OperationResult>
validate_face_id(const quader_poly::Document &document, FaceId id) {
  if (!id.valid()) {
    return Result<OperationResult>::failure(invalid_id_error("Face ID is invalid."));
  }
  if (stale_generation(id)) {
    return Result<OperationResult>::failure(stale_id_error("Face ID is stale."));
  }
  if (quader_poly::find_face(document, id.index) == nullptr) {
    return Result<OperationResult>::failure(invalid_id_error("Face ID is not live."));
  }
  return operation_success(selection_only_result(false, {}));
}

[[nodiscard]] Result<OperationResult>
validate_edge_key(const quader_poly::Document &document, EdgeKey edge) {
  if (!edge.valid()) {
    return Result<OperationResult>::failure(invalid_id_error("Edge key is invalid."));
  }
  if (stale_generation(edge)) {
    return Result<OperationResult>::failure(stale_id_error("Edge key is stale."));
  }
  if (quader_poly::find_vertex(document, edge.a.index) == nullptr ||
      quader_poly::find_vertex(document, edge.b.index) == nullptr) {
    return Result<OperationResult>::failure(
        invalid_id_error("Edge endpoint is not live."));
  }
  const std::vector<quader_poly::Edge> live_edges =
      quader_poly::document_edges(document);
  const quader_poly::Edge native_edge =
      quader_poly::make_edge(edge.a.index, edge.b.index);
  const bool live = std::ranges::any_of(live_edges, [&](quader_poly::Edge item) {
    return item == native_edge;
  });
  if (!live) {
    return Result<OperationResult>::failure(invalid_id_error("Edge key is not live."));
  }
  return operation_success(selection_only_result(false, {}));
}

template <typename T>
void apply_edit(std::vector<T> &target, std::span<const T> values,
                SelectionEdit edit) {
  if (edit == SelectionEdit::Replace) {
    target.assign(values.begin(), values.end());
    return;
  }

  for (const T &value : values) {
    const auto found = std::ranges::find(target, value);
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

void update_active_selection(quader_poly::Selection &selection) {
  selection.has_active = false;
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

[[nodiscard]] quader_poly::Edge native_edge(EdgeKey edge) {
  return quader_poly::make_edge(edge.a.index, edge.b.index);
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

template <typename Operation>
[[nodiscard]] Result<OperationResult>
apply_operation(PolygonDocumentImpl &impl, Operation operation,
                bool material_change = false) {
  quader_poly::OperationResult native_result =
      operation(impl.document, impl.selection);
  OperationResult result = adapt_operation_result(native_result);
  if (material_change && result.changed) {
    result.dirty.materials = true;
  }
  bump_content(impl, result.changed);
  bump_selection(impl, result.changed);
  return operation_success(std::move(result));
}

} // namespace

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

Result<PolygonDocument> PolygonDocument::make_box(const BoxSpec &spec) {
  if (spec.min.x >= spec.max.x || spec.min.y >= spec.max.y ||
      spec.min.z >= spec.max.z) {
    return Result<PolygonDocument>::failure(
        make_error(ErrorCode::InvalidArgument,
                   "Box bounds must have positive width, height, and depth."));
  }

  quader_poly::DocumentBuilder builder;
  builder.cube(to_native(spec.min), to_native(spec.max),
               native_material_slot(spec.material));
  return Result<PolygonDocument>::success(
      PolygonDocumentNativeAccess::from_native(builder.take()));
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

std::size_t PolygonDocument::vertex_count() const {
  return impl_->document.vertices.size();
}

std::size_t PolygonDocument::face_count() const {
  return impl_->document.faces.size();
}

std::uint64_t PolygonDocument::content_revision() const {
  return impl_->content_revision;
}

std::uint64_t PolygonDocument::selection_revision() const {
  return impl_->selection_revision;
}

std::vector<VertexId> PolygonDocument::vertex_ids() const {
  std::vector<VertexId> ids;
  ids.reserve(impl_->document.vertices.size());
  for (const quader_poly::Vertex &vertex : impl_->document.vertices) {
    ids.push_back(vertex_id(vertex.id));
  }
  return ids;
}

std::vector<EdgeKey> PolygonDocument::edge_ids() const {
  std::vector<EdgeKey> ids;
  const std::vector<quader_poly::Edge> native_edges =
      quader_poly::document_edges(impl_->document);
  ids.reserve(native_edges.size());
  for (const quader_poly::Edge edge : native_edges) {
    ids.push_back(edge_id(edge));
  }
  return ids;
}

std::vector<FaceId> PolygonDocument::face_ids() const {
  std::vector<FaceId> ids;
  ids.reserve(impl_->document.faces.size());
  for (const quader_poly::Face &face : impl_->document.faces) {
    ids.push_back(face_id(face.id));
  }
  return ids;
}

std::vector<EdgeKey> PolygonDocument::hard_edge_ids() const {
  std::vector<EdgeKey> ids;
  ids.reserve(impl_->document.hard_normal_edges.size());
  for (const quader_poly::Edge edge : impl_->document.hard_normal_edges) {
    ids.push_back(edge_id(edge));
  }
  return ids;
}

std::vector<EdgeKey> PolygonDocument::soft_edge_ids() const {
  std::vector<EdgeKey> ids = edge_ids();
  const std::vector<EdgeKey> hard = hard_edge_ids();
  std::erase_if(ids, [&](const EdgeKey edge) {
    return std::ranges::find(hard, edge) != hard.end();
  });
  return ids;
}

std::vector<FaceId>
PolygonDocument::face_ids_by_material_slot(std::uint32_t material_slot) const {
  std::vector<FaceId> ids;
  for (const quader_poly::Face &face : impl_->document.faces) {
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
  for (const quader_poly::Face &face : impl_->document.faces) {
    const quader::QVec3 normal =
        quader_poly::face_normal(impl_->document, face);
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
  snapshot.kind = selection_kind_from_native(impl_->selection.mode);
  snapshot.has_active = impl_->selection.has_active;
  snapshot.vertices.reserve(impl_->selection.vertices.size());
  for (const quader_poly::ElementId id : impl_->selection.vertices) {
    snapshot.vertices.push_back(vertex_id(id));
  }
  snapshot.edges.reserve(impl_->selection.edges.size());
  for (const quader_poly::Edge edge : impl_->selection.edges) {
    snapshot.edges.push_back(edge_id(edge));
  }
  snapshot.faces.reserve(impl_->selection.faces.size());
  for (const quader_poly::ElementId id : impl_->selection.faces) {
    snapshot.faces.push_back(face_id(id));
  }
  if (impl_->selection.has_active) {
    snapshot.active_vertex = vertex_id(impl_->selection.active_vertex);
    snapshot.active_edge = edge_id(impl_->selection.active_edge);
    snapshot.active_face = face_id(impl_->selection.active_face);
  }
  return snapshot;
}

Result<AuthoredPolygonPayload> PolygonDocument::authored_payload() const {
  AuthoredPolygonPayload payload;
  payload.content_revision = impl_->content_revision;
  payload.vertices.reserve(impl_->document.vertices.size());
  payload.positions.reserve(impl_->document.vertices.size());
  for (const quader_poly::Vertex &vertex : impl_->document.vertices) {
    payload.vertices.push_back(vertex_id(vertex.id));
    payload.positions.push_back(from_native(vertex.position));
  }

  payload.faces.reserve(impl_->document.faces.size());
  for (const quader_poly::Face &face : impl_->document.faces) {
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

Result<MeshPayload> PolygonDocument::compile_mesh() const {
  const quader_poly::CompiledMesh compiled =
      quader_poly::compile_document(impl_->document);
  MeshPayload payload;
  payload.content_revision = impl_->content_revision;
  payload.vertices.reserve(compiled.vertices.size());
  for (const quader_poly::CompiledVertex &vertex : compiled.vertices) {
    payload.vertices.push_back({
        .position = from_native(vertex.position),
        .normal = from_native(vertex.normal),
        .uv0 = from_native(vertex.uv0),
        .color = vertex.color,
    });
  }
  payload.indices = compiled.indices;
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
  const quader_poly::PolygonDocumentValidationReport document_report =
      quader_poly::validate_polygon_document(impl_->document);
  const quader_poly::PolygonSelectionLivenessReport selection_report =
      quader_poly::validate_polygon_selection_liveness(impl_->document,
                                                       impl_->selection);

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

Result<OperationResult>
PolygonDocument::set_selection_mode(SelectionMode mode) {
  auto &selection = impl_->selection;
  const quader_poly::SelectionMode previous = selection.mode;
  switch (mode) {
  case SelectionMode::Vertex:
    quader_poly::convert_selection_mode(impl_->document, selection,
                                        quader_poly::SelectionMode::Vertex);
    break;
  case SelectionMode::Edge:
    quader_poly::convert_selection_mode(impl_->document, selection,
                                        quader_poly::SelectionMode::Edge);
    break;
  case SelectionMode::Face:
    quader_poly::convert_selection_mode(impl_->document, selection,
                                        quader_poly::SelectionMode::Face);
    break;
  }

  const bool changed = selection.mode != previous;
  bump_selection(*impl_, changed);
  return operation_success(selection_only_result(changed, "Selection mode updated."));
}

Result<OperationResult> PolygonDocument::clear_selection() {
  const bool changed = !impl_->selection.empty() || impl_->selection.has_active;
  impl_->selection.clear();
  bump_selection(*impl_, changed);
  return operation_success(selection_only_result(changed, "Selection cleared."));
}

Result<OperationResult>
PolygonDocument::select_vertices(std::span<const VertexId> ids,
                                 SelectionEdit edit) {
  for (const VertexId id : ids) {
    Result<OperationResult> validation = validate_vertex_id(impl_->document, id);
    if (!validation.ok()) {
      return validation;
    }
  }
  if (edit == SelectionEdit::Replace ||
      impl_->selection.mode != quader_poly::SelectionMode::Vertex) {
    impl_->selection.clear();
    impl_->selection.mode = quader_poly::SelectionMode::Vertex;
  }
  std::vector<quader_poly::ElementId> native_ids;
  native_ids.reserve(ids.size());
  for (const VertexId id : ids) {
    native_ids.push_back(id.index);
  }
  apply_edit(impl_->selection.vertices,
             std::span<const quader_poly::ElementId>(native_ids.data(),
                                                     native_ids.size()),
             edit);
  update_active_selection(impl_->selection);
  bump_selection(*impl_, true);
  return operation_success(selection_only_result(true, "Vertices selected."));
}

Result<OperationResult>
PolygonDocument::select_edges(std::span<const EdgeKey> edges,
                              SelectionEdit edit) {
  for (const EdgeKey edge : edges) {
    Result<OperationResult> validation =
        validate_edge_key(impl_->document, edge);
    if (!validation.ok()) {
      return validation;
    }
  }
  if (edit == SelectionEdit::Replace ||
      impl_->selection.mode != quader_poly::SelectionMode::Edge) {
    impl_->selection.clear();
    impl_->selection.mode = quader_poly::SelectionMode::Edge;
  }
  std::vector<quader_poly::Edge> native_edges;
  native_edges.reserve(edges.size());
  for (const EdgeKey edge : edges) {
    native_edges.push_back(native_edge(edge));
  }
  apply_edit(impl_->selection.edges,
             std::span<const quader_poly::Edge>(native_edges.data(),
                                                native_edges.size()),
             edit);
  update_active_selection(impl_->selection);
  bump_selection(*impl_, true);
  return operation_success(selection_only_result(true, "Edges selected."));
}

Result<OperationResult>
PolygonDocument::select_faces(std::span<const FaceId> ids,
                              SelectionEdit edit) {
  for (const FaceId id : ids) {
    Result<OperationResult> validation = validate_face_id(impl_->document, id);
    if (!validation.ok()) {
      return validation;
    }
  }
  if (edit == SelectionEdit::Replace ||
      impl_->selection.mode != quader_poly::SelectionMode::Face) {
    impl_->selection.clear();
    impl_->selection.mode = quader_poly::SelectionMode::Face;
  }
  std::vector<quader_poly::ElementId> native_ids;
  native_ids.reserve(ids.size());
  for (const FaceId id : ids) {
    native_ids.push_back(id.index);
  }
  apply_edit(impl_->selection.faces,
             std::span<const quader_poly::ElementId>(native_ids.data(),
                                                     native_ids.size()),
             edit);
  update_active_selection(impl_->selection);
  bump_selection(*impl_, true);
  return operation_success(selection_only_result(true, "Faces selected."));
}

Result<OperationResult> PolygonDocument::select_vertex(VertexId id) {
  Result<OperationResult> validation = validate_vertex_id(impl_->document, id);
  if (!validation.ok()) {
    return validation;
  }
  impl_->selection.clear();
  impl_->selection.mode = quader_poly::SelectionMode::Vertex;
  impl_->selection.vertices = {id.index};
  impl_->selection.has_active = true;
  impl_->selection.active_kind = quader_poly::ElementKind::Vertex;
  impl_->selection.active_vertex = id.index;
  bump_selection(*impl_, true);
  return operation_success(selection_only_result(true, "Vertex selected."));
}

Result<OperationResult> PolygonDocument::select_edge(EdgeKey edge) {
  Result<OperationResult> validation = validate_edge_key(impl_->document, edge);
  if (!validation.ok()) {
    return validation;
  }
  impl_->selection.clear();
  impl_->selection.mode = quader_poly::SelectionMode::Edge;
  impl_->selection.edges = {quader_poly::make_edge(edge.a.index, edge.b.index)};
  impl_->selection.has_active = true;
  impl_->selection.active_kind = quader_poly::ElementKind::Edge;
  impl_->selection.active_edge = impl_->selection.edges.front();
  bump_selection(*impl_, true);
  return operation_success(selection_only_result(true, "Edge selected."));
}

Result<OperationResult> PolygonDocument::select_face(FaceId id) {
  Result<OperationResult> validation = validate_face_id(impl_->document, id);
  if (!validation.ok()) {
    return validation;
  }
  impl_->selection.clear();
  impl_->selection.mode = quader_poly::SelectionMode::Face;
  impl_->selection.faces = {id.index};
  impl_->selection.has_active = true;
  impl_->selection.active_kind = quader_poly::ElementKind::Face;
  impl_->selection.active_face = id.index;
  bump_selection(*impl_, true);
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
PolygonDocument::apply_selection(const PolygonSelectionSnapshot &selection,
                                 SelectionEdit edit) {
  if (selection.kind == SelectionKind::Vertex) {
    return select_vertices(selection.vertices, edit);
  }
  if (selection.kind == SelectionKind::Edge) {
    return select_edges(selection.edges, edit);
  }
  if (selection.kind == SelectionKind::Face) {
    return select_faces(selection.faces, edit);
  }
  return Result<OperationResult>::failure(
      make_error(ErrorCode::InvalidArgument,
                 "Polygon documents do not own object selections."));
}

Result<OperationResult> PolygonDocument::translate_selection(Vec3 delta) {
  quader_poly::OperationResult result =
      quader_poly::translate_selection(impl_->document, impl_->selection,
                                       to_native(delta));
  bump_content(*impl_, result.changed);
  bump_selection(*impl_, result.changed);
  return operation_success(adapt_operation_result(result));
}

Result<OperationResult>
PolygonDocument::transform_selection(Transform3 transform) {
  return apply_operation(*impl_, [&](quader_poly::Document &document,
                                     quader_poly::Selection &selection) {
    return quader_poly::transform_selection(document, selection,
                                            to_native(transform));
  });
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
PolygonDocument::assign_face_material_slot(FaceId id,
                                           std::uint32_t material_slot) {
  Result<OperationResult> validation = validate_face_id(impl_->document, id);
  if (!validation.ok()) {
    return validation;
  }
  const quader_poly::OperationResult native_result =
      quader_poly::assign_face_material_slot(impl_->document, id.index,
                                             material_slot);
  OperationResult result = adapt_operation_result(native_result);
  if (result.changed) {
    result.dirty.materials = true;
  }
  bump_content(*impl_, result.changed);
  bump_selection(*impl_, result.changed);
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
  quader_poly::Document &document = impl_->document;
  quader_poly::Selection &selection = impl_->selection;
  if (selection.mode != quader_poly::SelectionMode::Edge ||
      selection.edges.empty()) {
    return operation_success(
        selection_only_result(false, "Select edges before snapping."));
  }
  if (!selection.has_active ||
      selection.active_kind != quader_poly::ElementKind::Edge) {
    return operation_success(
        selection_only_result(false, "Edge Snap needs an active selected edge."));
  }

  const quader_poly::Edge target_edge =
      quader_poly::make_edge(selection.active_edge.a, selection.active_edge.b);
  const bool active_selected =
      std::ranges::any_of(selection.edges, [&](quader_poly::Edge edge) {
        return quader_poly::make_edge(edge.a, edge.b) == target_edge;
      });
  if (!active_selected) {
    return operation_success(
        selection_only_result(false, "Edge Snap needs an active selected edge."));
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
    return operation_success(selection_only_result(
        false, "Select at least one other edge to snap."));
  }

  OperationResult result;
  result.message = "Selected edges snapped to active edge.";
  for (const auto &[vertex_id, desired] : targets) {
    quader_poly::Vertex *vertex = quader_poly::find_vertex(document, vertex_id);
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

  quader_poly::clear_face_uvs(document);
  result.modified = result.affected;
  result.dirty.geometry = true;
  result.dirty.selection = true;
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
  return apply_operation(
      *impl_,
      [steps](quader_poly::Document &document,
              quader_poly::Selection &selection) {
        return quader_poly::bridge_selected_edges(document, selection, steps);
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

Result<OperationResult> PolygonDocument::delete_selection() {
  return apply_operation(
      *impl_,
      [](quader_poly::Document &document, quader_poly::Selection &selection) {
        return quader_poly::delete_selection(document, selection);
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

Result<OperationResult> PolygonDocument::detach_selected_faces() {
  return apply_operation(*impl_, quader_poly::detach_selected_faces);
}

Result<ExtractedPolygonDocument> PolygonDocument::extract_selected_faces() {
  quader_poly::ExtractFacesResult native_result =
      quader_poly::extract_selected_faces(impl_->document, impl_->selection);
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

Result<OperationResult> PolygonDocument::bridge_selected_faces(int steps) {
  return apply_operation(
      *impl_,
      [steps](quader_poly::Document &document,
              quader_poly::Selection &selection) {
        return quader_poly::bridge_selected_faces(document, selection, steps);
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
