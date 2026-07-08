////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "public_api_detail.hpp"

#include <cmath>

namespace quader::modeling {
using namespace detail;
namespace {

[[nodiscard]] Vec3 transform_point(Transform3 transform, Vec3 point) {
  return {
      transform.origin.x + transform.x_axis.x * point.x +
          transform.y_axis.x * point.y + transform.z_axis.x * point.z,
      transform.origin.y + transform.x_axis.y * point.x +
          transform.y_axis.y * point.y + transform.z_axis.y * point.z,
      transform.origin.z + transform.x_axis.z * point.x +
          transform.y_axis.z * point.y + transform.z_axis.z * point.z,
  };
}

[[nodiscard]] Transform3 rotation_transform(RotateOptions options) {
  const float sx = std::sin(options.radians.x);
  const float cx = std::cos(options.radians.x);
  const float sy = std::sin(options.radians.y);
  const float cy = std::cos(options.radians.y);
  const float sz = std::sin(options.radians.z);
  const float cz = std::cos(options.radians.z);

  Transform3 transform;
  transform.x_axis = {cz * cy, sz * cy, -sy};
  transform.y_axis = {cz * sy * sx - sz * cx, sz * sy * sx + cz * cx,
                      cy * sx};
  transform.z_axis = {cz * sy * cx + sz * sx, sz * sy * cx - cz * sx,
                      cy * cx};
  const Vec3 rotated_pivot = transform_point(transform, options.pivot);
  transform.origin = {
      options.pivot.x - rotated_pivot.x,
      options.pivot.y - rotated_pivot.y,
      options.pivot.z - rotated_pivot.z,
  };
  return transform;
}

} // namespace



MeshCollection::MeshCollection(std::shared_ptr<ModelingApiContext> context)
    : context_(std::move(context)) {}

std::vector<MeshHandle> MeshCollection::all() const {
  std::vector<MeshHandle> result;
  for (const SessionObjectSummary &summary : context_->session.objects()) {
    result.push_back(MeshHandle(context_, summary.id));
  }
  return result;
}

ObjectSelection MeshCollection::selected() const {
  std::vector<ObjectId> objects;
  for (const SessionObjectSummary &summary : context_->session.objects()) {
    if (summary.selected) {
      objects.push_back(summary.id);
    }
  }
  return only(objects);
}

ObjectSelection MeshCollection::only(std::span<const ObjectId> objects) const {
  SelectionUnion selection;
  selection.kind = SelectionKind::Object;
  selection.objects.assign(objects.begin(), objects.end());
  selection.object =
      selection.objects.empty() ? ObjectId{} : selection.objects.front();
  return ObjectSelection(context_, std::move(selection));
}

MeshHandle MeshCollection::first_selected() const {
  for (const SessionObjectSummary &summary : context_->session.objects()) {
    if (summary.selected) {
      return MeshHandle(context_, summary.id);
    }
  }
  return {};
}

OperationReceipt MeshCollection::delete_selected() {
  return selected().delete_objects();
}

MeshHandle MeshCollection::duplicate_selected(DuplicateOptions options) {
  return selected().duplicate(options);
}

OperationReceipt MeshCollection::combine_selected() {
  return selected().combine();
}

MeshHandle::MeshHandle(std::shared_ptr<ModelingApiContext> context,
                       ObjectId object)
    : context_(std::move(context)), object_(object) {}

ObjectId MeshHandle::id() const { return object_; }

bool MeshHandle::valid() const {
  return context_ && object_.valid() && object_live(*context_, object_);
}

MeshSummary MeshHandle::summary() const {
  if (!context_) {
    return {};
  }
  return find_summary(*context_, object_).value_or(MeshSummary{});
}

RevisionStamp MeshHandle::revisions() const {
  if (!context_) {
    return {};
  }
  RevisionStamp stamp;
  if (const std::optional<MeshSummary> item = find_summary(*context_, object_)) {
    stamp.content = item->content_revision;
    stamp.selection = item->selection_revision;
  }
  return stamp;
}

OperationReceipt MeshHandle::rename(std::string name) {
  Result<OperationResult> result =
      context_->session.rename_object(object_, std::move(name));
  if (!result.ok()) {
    return handle_failure(*context_, result.error());
  }
  return receipt_from_result(result.value(), current_revisions(*context_));
}

OperationReceipt MeshHandle::destroy() {
  Result<OperationResult> result = context_->session.remove_object(object_);
  if (!result.ok()) {
    return handle_failure(*context_, result.error());
  }
  return receipt_from_result(result.value(), current_revisions(*context_));
}

MeshHandle MeshHandle::duplicate(DuplicateOptions options) {
  Vec3 offset = options.offset;
  if (options.use_grid_default && offset.x == 0.0F && offset.y == 0.0F &&
      offset.z == 0.0F) {
    offset = {1.0F, 0.0F, 0.0F};
  }
  Result<ObjectId> object = context_->session.duplicate_object(object_, offset);
  if (!object.ok()) {
    return handle_value_failure(*context_, object.error(), MeshHandle{});
  }
  return MeshHandle(context_, object.value());
}

OperationReceipt MeshHandle::combine_with(std::span<const MeshHandle> meshes) {
  std::vector<ObjectId> objects{object_};
  objects.reserve(meshes.size() + 1U);
  for (const MeshHandle &mesh : meshes) {
    objects.push_back(mesh.id());
  }
  Result<OperationResult> result = context_->session.combine_objects(objects);
  if (!result.ok()) {
    return handle_failure(*context_, result.error());
  }
  return receipt_from_result(result.value(), current_revisions(*context_));
}

ObjectSelection MeshHandle::select() {
  std::vector<ObjectId> objects{object_};
  return MeshCollection(context_).only(objects);
}

VertexSelection MeshHandle::select_all_vertices() { return vertices().all(); }
EdgeSelection MeshHandle::select_all_edges() { return edges().all(); }
FaceSelection MeshHandle::select_all_faces() { return faces().all(); }
MeshVertices MeshHandle::vertices() { return MeshVertices(*this); }
MeshEdges MeshHandle::edges() { return MeshEdges(*this); }
MeshFaces MeshHandle::faces() { return MeshFaces(*this); }
MeshTransform MeshHandle::transform() { return MeshTransform(*this); }
MeshMaterials MeshHandle::materials() { return MeshMaterials(*this); }
MeshPayloads MeshHandle::payloads() const { return MeshPayloads(*this); }
MeshValidation MeshHandle::validation() const { return MeshValidation(*this); }

MeshVertices::MeshVertices(MeshHandle mesh) : mesh_(std::move(mesh)) {}

VertexSelection MeshVertices::all() const {
  std::vector<VertexId> vertices;
  if (Result<PolygonDocument> document =
          mesh_.context_->session.document_copy(mesh_.id())) {
    vertices = document.value().vertex_ids();
  }
  return only(vertices);
}

VertexSelection MeshVertices::selected() const {
  if (Result<PolygonDocument> document =
          mesh_.context_->session.document_copy(mesh_.id())) {
    const PolygonSelectionSnapshot snapshot = document.value().selection();
    if (snapshot.kind == SelectionKind::Vertex) {
      return only(snapshot.vertices);
    }
  }
  return only(std::span<const VertexId>{});
}

VertexSelection MeshVertices::only(std::span<const VertexId> vertices) const {
  SelectionUnion selection;
  selection.kind = SelectionKind::Vertex;
  selection.object = mesh_.id();
  selection.vertices.assign(vertices.begin(), vertices.end());
  return VertexSelection(mesh_.context_, std::move(selection));
}

VertexSelection MeshVertices::active() const {
  if (Result<PolygonDocument> document =
          mesh_.context_->session.document_copy(mesh_.id())) {
    const PolygonSelectionSnapshot snapshot = document.value().selection();
    if (snapshot.kind == SelectionKind::Vertex && snapshot.active_vertex.valid()) {
      std::vector<VertexId> ids{snapshot.active_vertex};
      return only(ids);
    }
  }
  return only(std::span<const VertexId>{});
}

MeshEdges::MeshEdges(MeshHandle mesh) : mesh_(std::move(mesh)) {}

EdgeSelection MeshEdges::all() const {
  std::vector<EdgeKey> edges;
  if (Result<PolygonDocument> document =
          mesh_.context_->session.document_copy(mesh_.id())) {
    edges = document.value().edge_ids();
  }
  return only(edges);
}

EdgeSelection MeshEdges::selected() const {
  if (Result<PolygonDocument> document =
          mesh_.context_->session.document_copy(mesh_.id())) {
    const PolygonSelectionSnapshot snapshot = document.value().selection();
    if (snapshot.kind == SelectionKind::Edge) {
      return only(snapshot.edges);
    }
  }
  return only(std::span<const EdgeKey>{});
}

EdgeSelection MeshEdges::only(std::span<const EdgeKey> edges) const {
  SelectionUnion selection;
  selection.kind = SelectionKind::Edge;
  selection.object = mesh_.id();
  selection.edges.assign(edges.begin(), edges.end());
  return EdgeSelection(mesh_.context_, std::move(selection));
}

EdgeSelection MeshEdges::boundary() const { return all(); }

EdgeSelection MeshEdges::hard() const {
  std::vector<EdgeKey> edges;
  if (Result<PolygonDocument> document =
          mesh_.context_->session.document_copy(mesh_.id())) {
    edges = document.value().hard_edge_ids();
  }
  return only(edges);
}

EdgeSelection MeshEdges::soft() const {
  std::vector<EdgeKey> edges;
  if (Result<PolygonDocument> document =
          mesh_.context_->session.document_copy(mesh_.id())) {
    edges = document.value().soft_edge_ids();
  }
  return only(edges);
}

MeshFaces::MeshFaces(MeshHandle mesh) : mesh_(std::move(mesh)) {}

FaceSelection MeshFaces::all() const {
  std::vector<FaceId> faces;
  if (Result<PolygonDocument> document =
          mesh_.context_->session.document_copy(mesh_.id())) {
    faces = document.value().face_ids();
  }
  return only(faces);
}

FaceSelection MeshFaces::selected() const {
  if (Result<PolygonDocument> document =
          mesh_.context_->session.document_copy(mesh_.id())) {
    const PolygonSelectionSnapshot snapshot = document.value().selection();
    if (snapshot.kind == SelectionKind::Face) {
      return only(snapshot.faces);
    }
  }
  return only(std::span<const FaceId>{});
}

FaceSelection MeshFaces::only(std::span<const FaceId> faces) const {
  SelectionUnion selection;
  selection.kind = SelectionKind::Face;
  selection.object = mesh_.id();
  selection.faces.assign(faces.begin(), faces.end());
  return FaceSelection(mesh_.context_, std::move(selection));
}

FaceSelection MeshFaces::by_material_slot(std::uint32_t slot) const {
  std::vector<FaceId> faces;
  if (Result<PolygonDocument> document =
          mesh_.context_->session.document_copy(mesh_.id())) {
    faces = document.value().face_ids_by_material_slot(slot);
  }
  return only(faces);
}

FaceSelection MeshFaces::by_normal(Vec3 direction, float tolerance) const {
  std::vector<FaceId> faces;
  if (Result<PolygonDocument> document =
          mesh_.context_->session.document_copy(mesh_.id())) {
    faces = document.value().face_ids_by_normal(direction, tolerance);
  }
  return only(faces);
}

MeshTransform::MeshTransform(MeshHandle mesh) : mesh_(std::move(mesh)) {}

OperationReceipt MeshTransform::translate(Vec3 delta) {
  return direct_mutate_selection(*mesh_.context_, all_face_selection(mesh_),
                                 [&](PolygonDocument &document) {
                                   return document.translate_selection(delta);
                                 });
}

OperationReceipt MeshTransform::rotate(RotateOptions options) {
  return apply({.transform = rotation_transform(options), .pivot = options.pivot});
}

OperationReceipt MeshTransform::scale(ScaleOptions options) {
  Transform3 transform;
  transform.x_axis.x = options.scale.x;
  transform.y_axis.y = options.scale.y;
  transform.z_axis.z = options.scale.z;
  transform.origin = {
      options.pivot.x - options.scale.x * options.pivot.x,
      options.pivot.y - options.scale.y * options.pivot.y,
      options.pivot.z - options.scale.z * options.pivot.z,
  };
  return apply({.transform = transform, .pivot = options.pivot});
}

OperationReceipt MeshTransform::apply(TransformOptions options) {
  return direct_mutate_selection(*mesh_.context_, all_face_selection(mesh_),
                                 [&](PolygonDocument &document) {
                                   return document.transform_selection(
                                       options.transform);
                                 });
}

PreviewHandle MeshTransform::preview(TransformOptions options) {
  auto state = std::make_shared<ModelingPreviewState>();
  state->context = mesh_.context_;
  state->id = make_id<PreviewTag>(1);
  state->kind = ModelingPreviewKind::Transform;
  state->selection = all_face_selection(mesh_);
  state->transform = options;
  return PreviewHandle(std::move(state));
}

MeshMaterials::MeshMaterials(MeshHandle mesh) : mesh_(std::move(mesh)) {}

OperationReceipt MeshMaterials::assign(MaterialId material) {
  return assign_slot(material.valid() ? material.index - 1U : 0U);
}

OperationReceipt MeshMaterials::assign_slot(std::uint32_t slot) {
  return direct_mutate_selection(*mesh_.context_, all_face_selection(mesh_),
                                 [slot](PolygonDocument &document) {
                                   return document
                                       .assign_selected_face_material_slot(slot);
                                 });
}

OperationReceipt MeshMaterials::assign_slot(FaceSelection faces,
                                            std::uint32_t slot) {
  return direct_mutate_selection(*mesh_.context_, faces.to_union(),
                                 [slot](PolygonDocument &document) {
                                   return document
                                       .assign_selected_face_material_slot(slot);
                                 });
}

MeshPayloads::MeshPayloads(MeshHandle mesh) : mesh_(std::move(mesh)) {}

AuthoredPolygonPayload MeshPayloads::authored_polygon() const {
  Result<PolygonDocument> document =
      mesh_.context_->session.document_copy(mesh_.id());
  if (!document.ok()) {
    return handle_value_failure(*mesh_.context_, document.error(),
                                AuthoredPolygonPayload{});
  }
  Result<AuthoredPolygonPayload> payload = document.value().authored_payload();
  if (!payload.ok()) {
    return handle_value_failure(*mesh_.context_, payload.error(),
                                AuthoredPolygonPayload{});
  }
  return std::move(payload).value();
}

MeshPayload MeshPayloads::compile_mesh(MeshCompileOptions) const {
  Result<PolygonDocument> document =
      mesh_.context_->session.document_copy(mesh_.id());
  if (!document.ok()) {
    return handle_value_failure(*mesh_.context_, document.error(),
                                MeshPayload{});
  }
  Result<MeshPayload> payload = document.value().compile_mesh();
  if (!payload.ok()) {
    return handle_value_failure(*mesh_.context_, payload.error(),
                                MeshPayload{});
  }
  return std::move(payload).value();
}

MeshCompileResult
MeshPayloads::compile_mesh_if_changed(std::uint64_t previous_revision,
                                      MeshCompileOptions options) const {
  const RevisionStamp stamp = mesh_.revisions();
  if (stamp.content <= previous_revision) {
    return {.changed = false, .reused_previous = true};
  }
  return {.mesh = compile_mesh(options), .changed = true};
}

MeshValidation::MeshValidation(MeshHandle mesh) : mesh_(std::move(mesh)) {}

OperationReceipt MeshValidation::validate() const {
  Result<PolygonDocument> document =
      mesh_.context_->session.document_copy(mesh_.id());
  if (!document.ok()) {
    return handle_failure(*mesh_.context_, document.error());
  }
  Result<OperationResult> result = document.value().validate();
  if (!result.ok()) {
    return handle_failure(*mesh_.context_, result.error());
  }
  return receipt_from_result(result.value(), mesh_.revisions());
}

} // namespace quader::modeling
