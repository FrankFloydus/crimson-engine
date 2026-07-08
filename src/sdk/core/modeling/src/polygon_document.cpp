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
#include <mesh/polygon/document_selection.hpp>
#include <mesh/polygon/document_validation.hpp>

#include <algorithm>
#include <memory>
#include <ranges>
#include <string>
#include <utility>

namespace quader::modeling {
namespace {

[[nodiscard]] quader::QVec2 to_native(Vec2 value) {
  return {value.x, value.y};
}

[[nodiscard]] quader::QVec3 to_native(Vec3 value) {
  return {value.x, value.y, value.z};
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

std::vector<FaceId> PolygonDocument::face_ids() const {
  std::vector<FaceId> ids;
  ids.reserve(impl_->document.faces.size());
  for (const quader_poly::Face &face : impl_->document.faces) {
    ids.push_back(face_id(face.id));
  }
  return ids;
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

Result<OperationResult> PolygonDocument::select_all_faces() {
  impl_->selection.clear();
  impl_->selection.mode = quader_poly::SelectionMode::Face;
  impl_->selection.faces.reserve(impl_->document.faces.size());
  for (const quader_poly::Face &face : impl_->document.faces) {
    impl_->selection.faces.push_back(face.id);
  }
  if (!impl_->selection.faces.empty()) {
    impl_->selection.has_active = true;
    impl_->selection.active_kind = quader_poly::ElementKind::Face;
    impl_->selection.active_face = impl_->selection.faces.front();
  }
  bump_selection(*impl_, true);
  return operation_success(selection_only_result(true, "Faces selected."));
}

Result<OperationResult> PolygonDocument::translate_selection(Vec3 delta) {
  quader_poly::OperationResult result =
      quader_poly::translate_selection(impl_->document, impl_->selection,
                                       to_native(delta));
  bump_content(*impl_, result.changed);
  bump_selection(*impl_, result.changed);
  return operation_success(adapt_operation_result(result));
}

} // namespace quader::modeling
