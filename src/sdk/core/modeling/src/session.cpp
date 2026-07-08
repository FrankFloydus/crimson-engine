////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <quader/modeling/session/modeling_session.hpp>

#include <algorithm>
#include <memory>
#include <string>
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

Result<OperationResult> ModelingSession::select_object(ObjectId object_id) {
  if (impl_->preview.active) {
    return active_preview_error();
  }
  if (!object_id.valid() || object_id.generation != 1U) {
    return Result<OperationResult>::failure(
        make_error(ErrorCode::InvalidId, "Object ID is invalid."));
  }

  SessionState before = impl_->state;
  bool found = false;
  bool changed = false;
  for (SessionObject &object : impl_->state.objects) {
    const bool selected = object.id == object_id;
    found = found || selected;
    changed = changed || object.selected != selected;
    object.selected = selected;
  }
  if (!found) {
    return Result<OperationResult>::failure(
        make_error(ErrorCode::InvalidId, "Object ID is not live."));
  }

  if (changed) {
    impl_->undo_stack.push_back(std::move(before));
    impl_->redo_stack.clear();
  }
  impl_->state.active_object = object_id;
  return Result<OperationResult>::success(
      session_result(changed, "Object selected."));
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
