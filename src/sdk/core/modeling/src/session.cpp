////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <quader/modeling/session/modeling_session.hpp>

#include "polygon_document_native.hpp"

#include <algorithm>
#include <mesh/polygon/document.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace quader::modeling {
namespace {

constexpr const char *kTranslateSelectionOperationId =
    "modeling.translate_selection";

/**
 * Stores one object in the headless SDK session.
 */
struct SessionObject {
  ObjectId id{};
  std::string name;
  PolygonDocument document;
  bool selected = false;
};

/**
 * Stores the copyable state snapshot used by SDK undo and redo.
 */
struct SessionState {
  std::vector<SessionObject> objects;
  ObjectId active_object{};
  std::uint32_t next_object_index = 1;
};

/**
 * Stores the active copy-on-write SDK preview transaction.
 */
struct PreviewTransaction {
  bool active = false;
  ObjectId object_id{};
  std::string operation_id;
  Vec3 delta{};
  PolygonDocument base_document;
  PolygonDocument preview_document;
  ToolPreviewPayload payload;
  DirtyFlags dirty;
};

[[nodiscard]] OperationResult session_result(bool changed,
                                             std::string message) {
  OperationResult result;
  result.changed = changed;
  result.message = std::move(message);
  result.dirty.geometry = changed;
  result.dirty.selection = changed;
  return result;
}

[[nodiscard]] Result<OperationResult> active_preview_error() {
  return Result<OperationResult>::failure(make_error(
      ErrorCode::InvalidArgument,
      "Commit or cancel the active preview before mutating the session."));
}

[[nodiscard]] Result<OperationResult> missing_preview_error() {
  return Result<OperationResult>::failure(
      make_error(ErrorCode::InvalidArgument, "No active preview transaction."));
}

[[nodiscard]] Result<ElementDelta> append_document_with_remapped_ids(
    quader_poly::Document &target, const quader_poly::Document &source) {
  ElementDelta created;
  std::unordered_map<quader_poly::ElementId, quader_poly::ElementId>
      vertex_id_map;
  for (const quader_poly::Vertex &vertex : source.vertices) {
    const quader_poly::ElementId id =
        quader_poly::add_vertex(target, vertex.position);
    if (id == quader_poly::kInvalidElementId) {
      return Result<ElementDelta>::failure(make_error(
          ErrorCode::InternalError, "Combine Mesh could not append a vertex."));
    }
    vertex_id_map.emplace(vertex.id, id);
    created.vertices.push_back(make_id<VertexTag>(id));
  }

  for (const quader_poly::Face &face : source.faces) {
    std::vector<quader_poly::ElementId> face_vertices;
    face_vertices.reserve(face.vertices.size());
    for (const quader_poly::ElementId vertex_id : face.vertices) {
      const auto mapped = vertex_id_map.find(vertex_id);
      if (mapped == vertex_id_map.end()) {
        return Result<ElementDelta>::failure(
            make_error(ErrorCode::InternalError,
                       "Combine Mesh could not remap a face vertex."));
      }
      face_vertices.push_back(mapped->second);
    }

    const quader_poly::ElementId face_id =
        quader_poly::add_face(target, face_vertices, face.material_slot);
    if (face_id == quader_poly::kInvalidElementId) {
      return Result<ElementDelta>::failure(make_error(
          ErrorCode::InternalError, "Combine Mesh could not append a face."));
    }
    created.faces.push_back(make_id<FaceTag>(face_id));
    if (quader_poly::Face *appended_face =
            quader_poly::find_face(target, face_id)) {
      appended_face->uvs = face.uvs;
      appended_face->normal_shading = face.normal_shading;
    }
  }

  for (const quader_poly::Edge edge : source.hard_normal_edges) {
    const auto a = vertex_id_map.find(edge.a);
    const auto b = vertex_id_map.find(edge.b);
    if (a != vertex_id_map.end() && b != vertex_id_map.end()) {
      target.hard_normal_edges.push_back(
          quader_poly::make_edge(a->second, b->second));
      created.edges.push_back(
          make_edge_key(make_id<VertexTag>(a->second),
                        make_id<VertexTag>(b->second)));
    }
  }
  return Result<ElementDelta>::success(std::move(created));
}

[[nodiscard]] auto find_object(SessionState &state, ObjectId object_id) {
  return std::ranges::find_if(state.objects, [&](const auto &object) {
    return object.id == object_id;
  });
}

[[nodiscard]] auto find_object(const SessionState &state, ObjectId object_id) {
  return std::ranges::find_if(state.objects, [&](const auto &object) {
    return object.id == object_id;
  });
}

[[nodiscard]] Result<SemanticOverlayPayload>
build_semantic_overlay_payload(const PolygonDocument &document) {
  Result<AuthoredPolygonPayload> authored_result = document.authored_payload();
  if (!authored_result.ok()) {
    return Result<SemanticOverlayPayload>::failure(authored_result.error());
  }

  const AuthoredPolygonPayload &authored = authored_result.value();
  auto position_for = [&](VertexId vertex_id) -> const Vec3 * {
    for (std::size_t index = 0; index < authored.vertices.size(); ++index) {
      if (authored.vertices[index] == vertex_id) {
        return &authored.positions[index];
      }
    }
    return nullptr;
  };

  SemanticOverlayPayload overlay;
  overlay.selection_revision = document.selection_revision();
  for (const AuthoredPolygonFacePayload &face : authored.faces) {
    SemanticOverlayRecord record;
    record.kind = SemanticOverlayKind::Wire;
    record.color_abgr = 0xffe8e8e8U;
    for (VertexId vertex_id : face.vertices) {
      if (const Vec3 *position = position_for(vertex_id)) {
        record.points.push_back(*position);
      }
    }
    if (!record.points.empty()) {
      record.points.push_back(record.points.front());
    }
    if (record.points.size() >= 2U) {
      overlay.records.push_back(std::move(record));
    }
  }
  return Result<SemanticOverlayPayload>::success(std::move(overlay));
}

[[nodiscard]] Result<ToolPreviewPayload>
build_preview_payload(const PolygonDocument &document) {
  Result<MeshPayload> mesh_result = document.compile_mesh();
  if (!mesh_result.ok()) {
    return Result<ToolPreviewPayload>::failure(mesh_result.error());
  }
  Result<SemanticOverlayPayload> overlay_result =
      build_semantic_overlay_payload(document);
  if (!overlay_result.ok()) {
    return Result<ToolPreviewPayload>::failure(overlay_result.error());
  }

  ToolPreviewPayload payload;
  payload.mesh = std::move(mesh_result).value();
  payload.overlay = std::move(overlay_result).value();
  payload.valid = true;
  return Result<ToolPreviewPayload>::success(std::move(payload));
}

[[nodiscard]] Result<OperationResult>
apply_preview_delta(PreviewTransaction &preview,
                    const PolygonDocument &base_document, Vec3 delta) {
  preview.preview_document = base_document;
  Result<OperationResult> operation =
      preview.preview_document.translate_selection(delta);
  if (!operation.ok()) {
    return operation;
  }

  Result<ToolPreviewPayload> payload =
      build_preview_payload(preview.preview_document);
  if (!payload.ok()) {
    return Result<OperationResult>::failure(payload.error());
  }

  preview.delta = delta;
  preview.payload = std::move(payload).value();
  preview.dirty = operation.value().dirty;
  preview.dirty.overlays = true;
  operation.value().dirty.overlays = true;
  return operation;
}

} // namespace

/**
 * Stores mutable SDK session implementation details.
 */
struct ModelingSessionImpl {
  SessionState state;
  PreviewTransaction preview;
  std::vector<SessionState> undo_stack;
  std::vector<SessionState> redo_stack;
};

ModelingSession::ModelingSession()
    : impl_(std::make_unique<ModelingSessionImpl>()) {}

ModelingSession::ModelingSession(const ModelingSession &other)
    : impl_(std::make_unique<ModelingSessionImpl>(*other.impl_)) {}

ModelingSession::ModelingSession(ModelingSession &&other) noexcept = default;

ModelingSession &
ModelingSession::operator=(const ModelingSession &other) {
  if (this != &other) {
    impl_ = std::make_unique<ModelingSessionImpl>(*other.impl_);
  }
  return *this;
}

ModelingSession &
ModelingSession::operator=(ModelingSession &&other) noexcept = default;

ModelingSession::~ModelingSession() = default;

Result<ObjectId> ModelingSession::add_document(PolygonDocument document,
                                               std::string name) {
  if (impl_->preview.active) {
    return Result<ObjectId>::failure(make_error(
        ErrorCode::InvalidArgument,
        "Commit or cancel the active preview before adding a document."));
  }

  impl_->undo_stack.push_back(impl_->state);
  impl_->redo_stack.clear();

  const ObjectId id = make_id<ObjectTag>(impl_->state.next_object_index++);
  impl_->state.objects.push_back({
      .id = id,
      .name = std::move(name),
      .document = std::move(document),
      .selected = false,
  });
  return Result<ObjectId>::success(id);
}

Result<OperationResult> ModelingSession::apply_object_selection(
    std::span<const ObjectId> objects, SelectionEdit edit) {
  if (impl_->preview.active) {
    return active_preview_error();
  }

  for (ObjectId object_id : objects) {
    if (!object_id.valid() || object_id.generation != 1U ||
        find_object(impl_->state, object_id) == impl_->state.objects.end()) {
      return Result<OperationResult>::failure(
          make_error(ErrorCode::InvalidId, "Object ID is not live."));
    }
  }

  SessionState before = impl_->state;
  bool changed = false;
  for (SessionObject &object : impl_->state.objects) {
    const bool matched =
        std::ranges::find(objects, object.id) != objects.end();
    bool selected = object.selected;
    switch (edit) {
    case SelectionEdit::Replace:
      selected = matched;
      break;
    case SelectionEdit::Add:
      selected = selected || matched;
      break;
    case SelectionEdit::Remove:
      selected = selected && !matched;
      break;
    case SelectionEdit::Toggle:
      selected = matched ? !selected : selected;
      break;
    }
    changed = changed || selected != object.selected;
    object.selected = selected;
  }

  if (!objects.empty() && edit != SelectionEdit::Remove) {
    impl_->state.active_object = objects.front();
  } else if (edit == SelectionEdit::Replace && objects.empty()) {
    impl_->state.active_object = {};
  }

  if (changed) {
    impl_->undo_stack.push_back(std::move(before));
    impl_->redo_stack.clear();
  }
  return Result<OperationResult>::success(
      session_result(changed, "Object selection changed."));
}

Result<OperationResult> ModelingSession::select_object(ObjectId object_id) {
  const ObjectId objects[] = {object_id};
  return apply_object_selection(objects, SelectionEdit::Replace);
}

Result<OperationResult> ModelingSession::rename_object(ObjectId object_id,
                                                       std::string name) {
  if (impl_->preview.active) {
    return active_preview_error();
  }
  auto object = find_object(impl_->state, object_id);
  if (object == impl_->state.objects.end()) {
    return Result<OperationResult>::failure(
        make_error(ErrorCode::InvalidId, "Object ID is not live."));
  }

  const bool changed = object->name != name;
  if (changed) {
    impl_->undo_stack.push_back(impl_->state);
    impl_->redo_stack.clear();
    object->name = std::move(name);
  }
  OperationResult result = session_result(changed, "Object renamed.");
  result.dirty.geometry = false;
  result.dirty.selection = false;
  return Result<OperationResult>::success(std::move(result));
}

Result<OperationResult> ModelingSession::remove_object(ObjectId object_id) {
  if (impl_->preview.active) {
    return active_preview_error();
  }
  auto object = find_object(impl_->state, object_id);
  if (object == impl_->state.objects.end()) {
    return Result<OperationResult>::failure(
        make_error(ErrorCode::InvalidId, "Object ID is not live."));
  }

  impl_->undo_stack.push_back(impl_->state);
  impl_->redo_stack.clear();
  impl_->state.objects.erase(object);
  if (impl_->state.active_object == object_id) {
    impl_->state.active_object = impl_->state.objects.empty()
                                     ? ObjectId{}
                                     : impl_->state.objects.front().id;
  }
  OperationResult result = session_result(true, "Object removed.");
  result.dirty.topology = true;
  return Result<OperationResult>::success(std::move(result));
}

Result<ObjectId> ModelingSession::duplicate_object(ObjectId object_id,
                                                   Vec3 offset,
                                                   std::string name) {
  if (impl_->preview.active) {
    return Result<ObjectId>::failure(make_error(
        ErrorCode::InvalidArgument,
        "Commit or cancel the active preview before duplicating an object."));
  }
  const auto object = find_object(impl_->state, object_id);
  if (object == impl_->state.objects.end()) {
    return Result<ObjectId>::failure(
        make_error(ErrorCode::InvalidId, "Object ID is not live."));
  }

  PolygonDocument document = object->document;
  if (offset.x != 0.0F || offset.y != 0.0F || offset.z != 0.0F) {
    Result<OperationResult> selected = document.select_all_faces();
    if (!selected.ok()) {
      return Result<ObjectId>::failure(selected.error());
    }
    Result<OperationResult> translated = document.translate_selection(offset);
    if (!translated.ok()) {
      return Result<ObjectId>::failure(translated.error());
    }
  }

  impl_->undo_stack.push_back(impl_->state);
  impl_->redo_stack.clear();
  for (SessionObject &item : impl_->state.objects) {
    item.selected = false;
  }
  const ObjectId duplicated =
      make_id<ObjectTag>(impl_->state.next_object_index++);
  impl_->state.objects.push_back({
      .id = duplicated,
      .name = name.empty() ? object->name : std::move(name),
      .document = std::move(document),
      .selected = true,
  });
  impl_->state.active_object = duplicated;
  return Result<ObjectId>::success(duplicated);
}

Result<OperationResult>
ModelingSession::combine_objects(std::span<const ObjectId> object_ids,
                                 std::string name) {
  if (impl_->preview.active) {
    return active_preview_error();
  }

  std::vector<ObjectId> unique_ids;
  unique_ids.reserve(object_ids.size());
  for (ObjectId object_id : object_ids) {
    if (std::ranges::find(unique_ids, object_id) == unique_ids.end()) {
      unique_ids.push_back(object_id);
    }
  }
  if (unique_ids.size() < 2U) {
    return Result<OperationResult>::failure(make_error(
        ErrorCode::InvalidArgument,
        "Select at least two meshes to combine."));
  }

  SessionState candidate = impl_->state;
  const ObjectId survivor_id = unique_ids.front();
  auto survivor = find_object(candidate, survivor_id);
  if (survivor == candidate.objects.end()) {
    return Result<OperationResult>::failure(
        make_error(ErrorCode::InvalidId, "Combine Mesh survivor is not live."));
  }

  OperationResult result = session_result(true, "Meshes combined.");
  result.dirty.topology = true;
  result.dirty.geometry = true;
  result.dirty.selection = true;

  quader_poly::Document &survivor_document =
      PolygonDocumentNativeAccess::document(survivor->document);
  for (ObjectId source_id : unique_ids) {
    if (source_id == survivor_id) {
      continue;
    }
    const auto source = find_object(candidate, source_id);
    if (source == candidate.objects.end()) {
      return Result<OperationResult>::failure(
          make_error(ErrorCode::InvalidId, "Combine Mesh source is not live."));
    }
    Result<ElementDelta> appended = append_document_with_remapped_ids(
        survivor_document, PolygonDocumentNativeAccess::document(source->document));
    if (!appended.ok()) {
      return Result<OperationResult>::failure(appended.error());
    }
    result.created.vertices.insert(result.created.vertices.end(),
                                   appended.value().vertices.begin(),
                                   appended.value().vertices.end());
    result.created.edges.insert(result.created.edges.end(),
                                appended.value().edges.begin(),
                                appended.value().edges.end());
    result.created.faces.insert(result.created.faces.end(),
                                appended.value().faces.begin(),
                                appended.value().faces.end());
  }
  result.affected = result.created;
  result.modified = result.created;

  candidate.objects.erase(
      std::remove_if(candidate.objects.begin(), candidate.objects.end(),
                     [&](const SessionObject &object) {
                       return object.id != survivor_id &&
                              std::ranges::find(unique_ids, object.id) !=
                                  unique_ids.end();
                     }),
      candidate.objects.end());

  survivor = find_object(candidate, survivor_id);
  if (survivor == candidate.objects.end()) {
    return Result<OperationResult>::failure(make_error(
        ErrorCode::InternalError, "Combine Mesh lost the survivor mesh."));
  }
  for (SessionObject &object : candidate.objects) {
    object.selected = object.id == survivor_id;
  }
  if (!name.empty()) {
    survivor->name = std::move(name);
  }
  PolygonDocumentNativeAccess::selection(survivor->document).clear();
  PolygonDocumentImpl &survivor_impl =
      PolygonDocumentNativeAccess::impl(survivor->document);
  ++survivor_impl.content_revision;
  ++survivor_impl.selection_revision;
  candidate.active_object = survivor_id;

  impl_->undo_stack.push_back(impl_->state);
  impl_->redo_stack.clear();
  impl_->state = std::move(candidate);
  return Result<OperationResult>::success(std::move(result));
}

Result<OperationResult> ModelingSession::select_face(ObjectId object_id,
                                                     FaceId face_id) {
  if (impl_->preview.active) {
    return active_preview_error();
  }
  for (SessionObject &object : impl_->state.objects) {
    if (object.id != object_id) {
      continue;
    }
    impl_->undo_stack.push_back(impl_->state);
    impl_->redo_stack.clear();
    impl_->state.active_object = object_id;
    object.selected = true;
    return object.document.select_face(face_id);
  }
  return Result<OperationResult>::failure(
      make_error(ErrorCode::InvalidId, "Object ID is not live."));
}

Result<OperationResult> ModelingSession::translate_selection(Vec3 delta) {
  if (impl_->preview.active) {
    return active_preview_error();
  }

  auto found =
      std::ranges::find_if(impl_->state.objects, [&](const auto &object) {
        return object.id == impl_->state.active_object && object.selected;
      });
  if (found == impl_->state.objects.end()) {
    return Result<OperationResult>::failure(
        make_error(ErrorCode::InvalidId, "No active selected object."));
  }

  impl_->undo_stack.push_back(impl_->state);
  impl_->redo_stack.clear();
  return found->document.translate_selection(delta);
}

Result<OperationResult>
ModelingSession::begin_translate_preview(ObjectId object_id, Vec3 delta) {
  if (impl_->preview.active) {
    return active_preview_error();
  }

  auto object = find_object(impl_->state, object_id);
  if (object == impl_->state.objects.end()) {
    return Result<OperationResult>::failure(
        make_error(ErrorCode::InvalidId, "Object ID is not live."));
  }

  PreviewTransaction preview;
  preview.active = true;
  preview.object_id = object_id;
  preview.operation_id = kTranslateSelectionOperationId;
  preview.base_document = object->document;
  Result<OperationResult> result =
      apply_preview_delta(preview, object->document, delta);
  if (!result.ok()) {
    return result;
  }

  impl_->preview = std::move(preview);
  result.value().message = "Translate preview started.";
  return result;
}

Result<OperationResult> ModelingSession::update_translate_preview(Vec3 delta) {
  if (!impl_->preview.active) {
    return missing_preview_error();
  }
  if (impl_->preview.operation_id != kTranslateSelectionOperationId) {
    return Result<OperationResult>::failure(make_error(
        ErrorCode::UnsupportedOperation,
        "Active preview transaction is not a translate operation."));
  }

  Result<OperationResult> result =
      apply_preview_delta(impl_->preview, impl_->preview.base_document, delta);
  if (result.ok()) {
    result.value().message = "Translate preview updated.";
  }
  return result;
}

Result<OperationResult> ModelingSession::commit_preview() {
  if (!impl_->preview.active) {
    return missing_preview_error();
  }

  auto object = find_object(impl_->state, impl_->preview.object_id);
  if (object == impl_->state.objects.end()) {
    impl_->preview = PreviewTransaction{};
    return Result<OperationResult>::failure(
        make_error(ErrorCode::InvalidId, "Preview object ID is not live."));
  }

  impl_->undo_stack.push_back(impl_->state);
  impl_->redo_stack.clear();
  object->document = impl_->preview.preview_document;
  object->selected = true;
  impl_->state.active_object = object->id;
  impl_->preview = PreviewTransaction{};

  OperationResult result =
      session_result(true, "Preview committed.");
  result.dirty.overlays = true;
  return Result<OperationResult>::success(std::move(result));
}

Result<OperationResult> ModelingSession::cancel_preview() {
  if (!impl_->preview.active) {
    return missing_preview_error();
  }
  impl_->preview = PreviewTransaction{};
  OperationResult result =
      session_result(true, "Preview canceled.");
  result.dirty.geometry = false;
  result.dirty.selection = false;
  result.dirty.overlays = true;
  return Result<OperationResult>::success(std::move(result));
}

Result<OperationResult> ModelingSession::undo() {
  if (impl_->preview.active) {
    return active_preview_error();
  }
  if (impl_->undo_stack.empty()) {
    return Result<OperationResult>::failure(
        make_error(ErrorCode::InvalidArgument, "Nothing to undo."));
  }
  impl_->redo_stack.push_back(impl_->state);
  impl_->state = std::move(impl_->undo_stack.back());
  impl_->undo_stack.pop_back();
  return Result<OperationResult>::success(session_result(true, "Undo."));
}

Result<OperationResult> ModelingSession::redo() {
  if (impl_->preview.active) {
    return active_preview_error();
  }
  if (impl_->redo_stack.empty()) {
    return Result<OperationResult>::failure(
        make_error(ErrorCode::InvalidArgument, "Nothing to redo."));
  }
  impl_->undo_stack.push_back(impl_->state);
  impl_->state = std::move(impl_->redo_stack.back());
  impl_->redo_stack.pop_back();
  return Result<OperationResult>::success(session_result(true, "Redo."));
}

bool ModelingSession::can_undo() const {
  return !impl_->undo_stack.empty();
}

bool ModelingSession::can_redo() const {
  return !impl_->redo_stack.empty();
}

std::size_t ModelingSession::undo_depth() const {
  return impl_->undo_stack.size();
}

void ModelingSession::squash_undo_since(std::size_t mark) {
  if (mark >= impl_->undo_stack.size()) {
    return;
  }
  SessionState first_batch_snapshot = impl_->undo_stack[mark];
  impl_->undo_stack.erase(impl_->undo_stack.begin() + mark,
                          impl_->undo_stack.end());
  impl_->undo_stack.push_back(std::move(first_batch_snapshot));
}

bool ModelingSession::rollback_undo_since(std::size_t mark) {
  if (mark >= impl_->undo_stack.size()) {
    return false;
  }
  impl_->state = impl_->undo_stack[mark];
  impl_->undo_stack.erase(impl_->undo_stack.begin() + mark,
                          impl_->undo_stack.end());
  impl_->redo_stack.clear();
  impl_->preview = PreviewTransaction{};
  return true;
}

bool ModelingSession::preview_active() const {
  return impl_->preview.active;
}

std::vector<SessionObjectSummary> ModelingSession::objects() const {
  std::vector<SessionObjectSummary> result;
  result.reserve(impl_->state.objects.size());
  for (const SessionObject &object : impl_->state.objects) {
    result.push_back({
        .id = object.id,
        .name = object.name,
        .selected = object.selected,
        .content_revision = object.document.content_revision(),
        .selection_revision = object.document.selection_revision(),
    });
  }
  return result;
}

const PolygonDocument *ModelingSession::document(ObjectId object_id) const {
  const auto found = find_object(impl_->state, object_id);
  return found == impl_->state.objects.end() ? nullptr : &found->document;
}

Result<PolygonDocument>
ModelingSession::document_copy(ObjectId object_id) const {
  const auto found = find_object(impl_->state, object_id);
  if (found == impl_->state.objects.end()) {
    return Result<PolygonDocument>::failure(
        make_error(ErrorCode::InvalidId, "Object ID is not live."));
  }
  return Result<PolygonDocument>::success(found->document);
}

Result<OperationResult>
ModelingSession::commit_document(ObjectId object_id, PolygonDocument document,
                                 OperationResult receipt) {
  if (impl_->preview.active) {
    return active_preview_error();
  }
  auto found = find_object(impl_->state, object_id);
  if (found == impl_->state.objects.end()) {
    return Result<OperationResult>::failure(
        make_error(ErrorCode::InvalidId, "Object ID is not live."));
  }
  if (receipt.changed) {
    impl_->undo_stack.push_back(impl_->state);
    impl_->redo_stack.clear();
  }
  found->document = std::move(document);
  found->selected = true;
  impl_->state.active_object = found->id;
  return Result<OperationResult>::success(std::move(receipt));
}

Result<ToolPreviewPayload> ModelingSession::preview_payload() const {
  if (!impl_->preview.active) {
    return Result<ToolPreviewPayload>::failure(
        make_error(ErrorCode::InvalidArgument, "No active preview transaction."));
  }
  return Result<ToolPreviewPayload>::success(impl_->preview.payload);
}

PreviewTransactionSummary ModelingSession::preview_summary() const {
  if (!impl_->preview.active) {
    return {};
  }
  return {
      .active = true,
      .object_id = impl_->preview.object_id,
      .operation_id = impl_->preview.operation_id,
      .delta = impl_->preview.delta,
      .content_revision =
          impl_->preview.preview_document.content_revision(),
      .dirty = impl_->preview.dirty,
  };
}

Result<SemanticOverlayPayload>
ModelingSession::semantic_overlay(ObjectId object_id) const {
  const auto object = find_object(impl_->state, object_id);
  if (object == impl_->state.objects.end()) {
    return Result<SemanticOverlayPayload>::failure(
        make_error(ErrorCode::InvalidId, "Object ID is not live."));
  }
  return build_semantic_overlay_payload(object->document);
}

std::vector<CommandDescriptor> ModelingSession::operation_registry() const {
  return {{
      .id = kTranslateSelectionOperationId,
      .label = "Translate Selection",
  }};
}

} // namespace quader::modeling
