////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <quader/modeling/session/modeling_session.hpp>

#include "polygon_document_native.hpp"

#include <algorithm>
#include <iterator>
#include <limits>
#include <quader/modeling/neutral/polygon/document.hpp>
#include <quader/modeling/neutral/polygon/document_operations.hpp>
#include <memory>
#include <span>
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
};

/**
 * Stores one portable object clipboard payload outside undoable workspace
 * state. Object identities and selection are deliberately not copied.
 */
struct ClipboardObject {
  std::string name;
  PolygonDocument document;
};

/**
 * Stores the active copy-on-write SDK preview transaction.
 */
struct PreviewTransaction {
  PreviewId id{};
  bool active = false;
  ObjectId object_id{};
  std::string operation_id;
  Vec3 delta{};
  PolygonDocument base_document;
  PolygonContentIdentity base_content_identity{};
  std::uint64_t base_content_revision = 0;
  std::uint64_t base_session_revision = 0;
  PolygonDocument preview_document;
  OperationResult receipt;
  ToolPreviewPayload payload;
  bool payload_valid = false;
  DirtyFlags dirty;
  std::uint64_t payload_revision = 0;
};

/**
 * Stores the latest revision for every externally visible object change kind.
 */
struct SessionObjectChangeRecord {
  ObjectId id{};
  std::string name;
  std::uint64_t revision = 0;
  std::uint64_t topology_revision = 0;
  std::uint64_t geometry_revision = 0;
  std::uint64_t selection_revision = 0;
  std::uint64_t materials_revision = 0;
  std::uint64_t overlays_revision = 0;
  bool deleted = false;
};

/**
 * Carries one state-diff entry before it receives the shared session revision.
 */
struct PendingSessionObjectChange {
  ObjectId id{};
  std::string name;
  DirtyFlags dirty;
  bool deleted = false;
};

/**
 * Preserves operation-owned dirty metadata for one direct state transition.
 */
struct SessionObjectDirtyOverride {
  ObjectId id{};
  DirtyFlags dirty;
};

/**
 * Keeps topology allocation cursors outside undo snapshots.
 *
 * The cursor is scoped to one session object lineage. Document copies retain
 * their current cursor and then allocate independently; restoring session
 * history never rewinds the owning object's cursor.
 */
struct TopologyIdentityHighWater {
  std::uint32_t next_vertex_id = 1U;
  std::uint32_t next_face_id = 1U;
};

[[nodiscard]] bool same_polygon_selection(const PolygonDocument &left,
                                          const PolygonDocument &right) {
  const PolygonSelectionSnapshot left_selection = left.selection();
  const PolygonSelectionSnapshot right_selection = right.selection();
  return left_selection.kind == right_selection.kind &&
         left_selection.vertices == right_selection.vertices &&
         left_selection.edges == right_selection.edges &&
         left_selection.faces == right_selection.faces &&
         left_selection.has_active == right_selection.has_active &&
         left_selection.active_vertex == right_selection.active_vertex &&
         left_selection.active_edge == right_selection.active_edge &&
         left_selection.active_face == right_selection.active_face;
}

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

/**
 * Carries appended element identities and their source-to-survivor remaps.
 */
struct AppendedDocument {
  ElementDelta created;
  std::unordered_map<quader_poly::ElementId, quader_poly::ElementId>
      vertex_ids;
  std::unordered_map<quader_poly::ElementId, quader_poly::ElementId> face_ids;
};

[[nodiscard]] Result<AppendedDocument> append_document_with_remapped_ids(
    quader_poly::Document &target, const quader_poly::Document &source) {
  AppendedDocument appended;
  for (const quader_poly::Vertex &vertex : source.vertices) {
    const quader_poly::ElementId id =
        quader_poly::add_vertex(target, vertex.position);
    if (id == quader_poly::kInvalidElementId) {
      return Result<AppendedDocument>::failure(make_error(
          ErrorCode::InternalError, "Combine Mesh could not append a vertex."));
    }
    appended.vertex_ids.emplace(vertex.id, id);
    appended.created.vertices.push_back(make_id<VertexTag>(id));
  }

  for (const quader_poly::Face &face : source.faces) {
    std::vector<quader_poly::ElementId> face_vertices;
    face_vertices.reserve(face.vertices.size());
    for (const quader_poly::ElementId vertex_id : face.vertices) {
      const auto mapped = appended.vertex_ids.find(vertex_id);
      if (mapped == appended.vertex_ids.end()) {
        return Result<AppendedDocument>::failure(
            make_error(ErrorCode::InternalError,
                       "Combine Mesh could not remap a face vertex."));
      }
      face_vertices.push_back(mapped->second);
    }

    const quader_poly::ElementId face_id =
        quader_poly::add_face(target, face_vertices, face.material_slot);
    if (face_id == quader_poly::kInvalidElementId) {
      return Result<AppendedDocument>::failure(make_error(
          ErrorCode::InternalError, "Combine Mesh could not append a face."));
    }
    appended.face_ids.emplace(face.id, face_id);
    appended.created.faces.push_back(make_id<FaceTag>(face_id));
    if (quader_poly::Face *appended_face =
            quader_poly::find_face(target, face_id)) {
      appended_face->uvs = face.uvs;
      appended_face->normal_shading = face.normal_shading;
    }
  }

  for (const quader_poly::Edge edge : source.hard_normal_edges) {
    const auto a = appended.vertex_ids.find(edge.a);
    const auto b = appended.vertex_ids.find(edge.b);
    if (a != appended.vertex_ids.end() && b != appended.vertex_ids.end()) {
      target.hard_normal_edges.push_back(
          quader_poly::make_edge(a->second, b->second));
      appended.created.edges.push_back(
          make_edge_key(make_id<VertexTag>(a->second),
                        make_id<VertexTag>(b->second)));
    }
  }
  return Result<AppendedDocument>::success(std::move(appended));
}

void append_native_delta(ElementDelta &target,
                         const quader_poly::OperationElementDelta &source) {
  target.vertices.reserve(target.vertices.size() + source.vertices.size());
  for (quader_poly::ElementId id : source.vertices) {
    target.vertices.push_back(make_id<VertexTag>(id));
  }
  target.edges.reserve(target.edges.size() + source.edges.size());
  for (quader_poly::Edge edge : source.edges) {
    target.edges.push_back(make_edge_key(make_id<VertexTag>(edge.a),
                                         make_id<VertexTag>(edge.b)));
  }
  target.faces.reserve(target.faces.size() + source.faces.size());
  for (quader_poly::ElementId id : source.faces) {
    target.faces.push_back(make_id<FaceTag>(id));
  }
}

[[nodiscard]] quader::QVec3 transform_point(Transform3 transform,
                                             quader::QVec3 point) {
  return {
      transform.x_axis.x * point.x + transform.y_axis.x * point.y +
          transform.z_axis.x * point.z + transform.origin.x,
      transform.x_axis.y * point.x + transform.y_axis.y * point.y +
          transform.z_axis.y * point.z + transform.origin.y,
      transform.x_axis.z * point.x + transform.y_axis.z * point.y +
          transform.z_axis.z * point.z + transform.origin.z,
  };
}

[[nodiscard]] float determinant(Transform3 transform) {
  return transform.x_axis.x *
             (transform.y_axis.y * transform.z_axis.z -
              transform.y_axis.z * transform.z_axis.y) -
         transform.y_axis.x *
             (transform.x_axis.y * transform.z_axis.z -
              transform.x_axis.z * transform.z_axis.y) +
         transform.z_axis.x *
             (transform.x_axis.y * transform.y_axis.z -
              transform.x_axis.z * transform.y_axis.y);
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

[[nodiscard]] Result<ToolPreviewPayload>
materialize_preview_payload(PreviewTransaction &preview) {
  if (!preview.payload_valid) {
    Result<ToolPreviewPayload> payload =
        build_preview_payload(preview.preview_document);
    if (!payload.ok()) {
      return payload;
    }
    preview.payload = std::move(payload).value();
    preview.payload.mesh.content_revision = preview.payload_revision;
    preview.payload_valid = true;
  }
  return Result<ToolPreviewPayload>::success(preview.payload);
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

  preview.delta = delta;
  preview.payload = {};
  preview.payload_valid = false;
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
  std::vector<ClipboardObject> clipboard;
  PreviewTransaction preview;
  std::vector<SessionState> undo_stack;
  std::vector<SessionState> redo_stack;
  std::vector<SessionObjectChangeRecord> object_changes;
  std::uint64_t content_revision = 0;
  std::uint64_t selection_revision = 0;
  std::uint64_t preview_payload_revision = 0;
  std::uint32_t next_preview_index = 1U;
  std::unordered_map<std::uint32_t, TopologyIdentityHighWater>
      topology_identity_high_water;
  bool batch_transaction_active = false;
  bool batch_transaction_violated = false;
  bool batch_creation_authorized = false;
  ObjectId batch_target{};

  // Object identities belong to the session lifetime, not an undo snapshot.
  // Rewinding this counter would let a stale handle alias a replacement object
  // created after branching from undo history.
  std::uint32_t next_object_index = 1;

  [[nodiscard]] Result<ObjectId> allocate_object_id() {
    if (next_object_index == 0U) {
      return Result<ObjectId>::failure(make_error(
          ErrorCode::InternalError, "The session exhausted object identities."));
    }
    return Result<ObjectId>::success(
        make_id<ObjectTag>(next_object_index++));
  }

  [[nodiscard]] Result<PreviewId> allocate_preview_id() {
    if (next_preview_index == 0U) {
      return Result<PreviewId>::failure(make_error(
          ErrorCode::InternalError, "The session exhausted preview identities."));
    }
    return Result<PreviewId>::success(
        make_id<PreviewTag>(next_preview_index++));
  }

  [[nodiscard]] bool allow_batch_creation() {
    if (!batch_transaction_active) {
      return true;
    }
    if (!batch_creation_authorized) {
      batch_transaction_violated = true;
      return false;
    }
    batch_creation_authorized = false;
    return true;
  }

  [[nodiscard]] bool allow_batch_object(ObjectId object_id) {
    if (!batch_transaction_active) {
      return true;
    }
    if (batch_target.valid() && batch_target == object_id) {
      return true;
    }
    batch_transaction_violated = true;
    return false;
  }

  [[nodiscard]] bool allow_batch_objects(std::span<const ObjectId> objects) {
    if (!batch_transaction_active) {
      return true;
    }
    if (batch_target.valid() && !objects.empty() &&
        std::ranges::all_of(objects,
                            [&](ObjectId id) { return id == batch_target; })) {
      return true;
    }
    batch_transaction_violated = true;
    return false;
  }

  [[nodiscard]] bool preview_matches_base() const {
    if (!preview.active) {
      return false;
    }
    const auto object = find_object(state, preview.object_id);
    return object != state.objects.end() &&
           object->document.content_identity() ==
               preview.base_content_identity &&
           object->document.content_revision() ==
               preview.base_content_revision &&
           content_revision == preview.base_session_revision;
  }

  void record_topology_identity(ObjectId object_id,
                                const PolygonDocument &document) {
    const quader_poly::Document &native =
        PolygonDocumentNativeAccess::document(document);
    TopologyIdentityHighWater &high_water =
        topology_identity_high_water[object_id.index];
    high_water.next_vertex_id =
        std::max(high_water.next_vertex_id, native.next_vertex_id);
    high_water.next_face_id =
        std::max(high_water.next_face_id, native.next_face_id);
  }

  void reconcile_topology_identity(ObjectId object_id,
                                   PolygonDocument &document) const {
    const auto high_water = topology_identity_high_water.find(object_id.index);
    if (high_water == topology_identity_high_water.end()) {
      return;
    }
    const quader_poly::Document &current =
        PolygonDocumentNativeAccess::document(document);
    if (current.next_vertex_id >= high_water->second.next_vertex_id &&
        current.next_face_id >= high_water->second.next_face_id) {
      return;
    }
    quader_poly::Document reconciled = current;
    reconciled.next_vertex_id =
        std::max(reconciled.next_vertex_id,
                 high_water->second.next_vertex_id);
    reconciled.next_face_id =
        std::max(reconciled.next_face_id, high_water->second.next_face_id);
    PolygonDocumentNativeAccess::replace_document(document,
                                                  std::move(reconciled));
  }

  void reconcile_restored_topology_identities() {
    for (SessionObject &object : state.objects) {
      reconcile_topology_identity(object.id, object.document);
      record_topology_identity(object.id, object.document);
    }
  }

  [[nodiscard]] Result<OperationResult>
  validate_commit_identity(ObjectId object_id,
                           const PolygonDocument &document) const {
    const auto high_water = topology_identity_high_water.find(object_id.index);
    if (high_water == topology_identity_high_water.end()) {
      return Result<OperationResult>::success(OperationResult{});
    }
    const quader_poly::Document &native =
        PolygonDocumentNativeAccess::document(document);
    if (native.next_vertex_id < high_water->second.next_vertex_id ||
        native.next_face_id < high_water->second.next_face_id) {
      return Result<OperationResult>::failure(make_error(
          ErrorCode::StaleId,
          "Polygon document was copied before the current topology identity "
          "high-water mark."));
    }
    return Result<OperationResult>::success(OperationResult{});
  }

  [[nodiscard]] std::uint64_t advance_preview_payload_revision(
      std::uint64_t document_revision) {
    if (preview_payload_revision <
        (std::numeric_limits<std::uint64_t>::max)()) {
      ++preview_payload_revision;
    }
    preview_payload_revision =
        std::max(preview_payload_revision, document_revision);
    return preview_payload_revision;
  }

  void publish_state_transition(
      const SessionState &before,
      std::span<const SessionObjectDirtyOverride> dirty_overrides = {}) {
    std::vector<PendingSessionObjectChange> pending;
    pending.reserve(before.objects.size() + state.objects.size());

    for (const SessionObject &previous : before.objects) {
      const auto current = find_object(state, previous.id);
      if (current == state.objects.end()) {
        pending.push_back({
            .id = previous.id,
            .name = previous.name,
            .dirty = {.topology = true,
                      .geometry = true,
                      .selection = true,
                      .materials = true,
                      .overlays = true},
            .deleted = true,
        });
        continue;
      }

      const bool content_changed =
          previous.document.content_identity() !=
              current->document.content_identity() ||
          previous.document.content_revision() !=
              current->document.content_revision();
      const bool effective_selection_changed =
          previous.selected != current->selected ||
          !same_polygon_selection(previous.document, current->document);
      const bool selection_changed =
          effective_selection_changed ||
          previous.document.selection_revision() !=
              current->document.selection_revision() ||
          (content_changed &&
           !same_polygon_selection(previous.document, current->document));
      const auto dirty_override = std::ranges::find_if(
          dirty_overrides,
          [&](const SessionObjectDirtyOverride &candidate) {
            return candidate.id == current->id;
          });
      if (!content_changed && !selection_changed &&
          previous.name == current->name &&
          dirty_override == dirty_overrides.end()) {
        continue;
      }

      DirtyFlags dirty;
      if (dirty_override != dirty_overrides.end()) {
        dirty = dirty_override->dirty;
      } else if (content_changed) {
        // Undo, redo, and rollback currently retain state snapshots without the
        // originating receipt. Conservatively invalidate every content domain
        // rather than losing material-only changes.
        dirty.topology = true;
        dirty.geometry = true;
        dirty.materials = true;
        dirty.overlays = true;
      }
      // A direct document commit carries the authoritative dirty-domain
      // receipt. Some document mutations advance an internal selection
      // revision while preserving the effective selection; do not widen an
      // exact override into a selection invalidation in that case. Object and
      // active-object transitions are still published below.
      if (selection_changed &&
          (dirty_override == dirty_overrides.end() ||
           effective_selection_changed)) {
        dirty.selection = true;
        dirty.overlays = true;
      }

      pending.push_back({
          .id = current->id,
          .name = current->name,
          .dirty = dirty,
      });
    }

    for (const SessionObject &current : state.objects) {
      if (find_object(before, current.id) != before.objects.end()) {
        continue;
      }
      pending.push_back({
          .id = current.id,
          .name = current.name,
          .dirty = {.topology = true,
                    .geometry = true,
                    .selection = true,
                    .materials = true,
                    .overlays = true},
      });
    }

    auto mark_active_change = [&](ObjectId object_id) {
      if (!object_id.valid()) {
        return;
      }
      auto change = std::ranges::find_if(
          pending, [&](const PendingSessionObjectChange &candidate) {
            return candidate.id == object_id;
          });
      if (change == pending.end()) {
        const auto current = find_object(state, object_id);
        if (current != state.objects.end()) {
          pending.push_back({.id = current->id, .name = current->name});
        } else {
          const auto previous = find_object(before, object_id);
          if (previous == before.objects.end()) {
            return;
          }
          pending.push_back({
              .id = previous->id,
              .name = previous->name,
              .deleted = true,
          });
        }
        change = std::prev(pending.end());
      }
      change->dirty.selection = true;
      change->dirty.overlays = true;
    };
    if (before.active_object != state.active_object) {
      mark_active_change(before.active_object);
      mark_active_change(state.active_object);
    }

    if (pending.empty()) {
      return;
    }

    ++content_revision;
    for (const PendingSessionObjectChange &change : pending) {
      auto record = std::ranges::find_if(
          object_changes, [&](const SessionObjectChangeRecord &candidate) {
            return candidate.id == change.id;
          });
      if (record == object_changes.end()) {
        object_changes.push_back({.id = change.id});
        record = std::prev(object_changes.end());
      }

      record->name = change.name;
      record->revision = content_revision;
      record->deleted = change.deleted;
      if (change.dirty.topology) {
        record->topology_revision = content_revision;
      }
      if (change.dirty.geometry) {
        record->geometry_revision = content_revision;
      }
      if (change.dirty.selection) {
        record->selection_revision = content_revision;
        selection_revision = content_revision;
      }
      if (change.dirty.materials) {
        record->materials_revision = content_revision;
      }
      if (change.dirty.overlays) {
        record->overlays_revision = content_revision;
      }
    }
  }
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
  if (!impl_->allow_batch_creation()) {
    return Result<ObjectId>::failure(make_error(
        ErrorCode::InvalidArgument,
        "Batch callback cannot create an unrelated document."));
  }
  if (impl_->preview.active) {
    return Result<ObjectId>::failure(make_error(
        ErrorCode::InvalidArgument,
        "Commit or cancel the active preview before adding a document."));
  }

  Result<ObjectId> allocated_id = impl_->allocate_object_id();
  if (!allocated_id.ok()) {
    return allocated_id;
  }

  impl_->undo_stack.push_back(impl_->state);
  impl_->redo_stack.clear();

  const ObjectId id = allocated_id.value();
  impl_->state.objects.push_back({
      .id = id,
      .name = std::move(name),
      .document = std::move(document),
      .selected = false,
  });
  impl_->record_topology_identity(id, impl_->state.objects.back().document);
  impl_->publish_state_transition(impl_->undo_stack.back());
  return Result<ObjectId>::success(id);
}

Result<OperationResult> ModelingSession::apply_object_selection(
    std::span<const ObjectId> objects, SelectionEdit edit) {
  if (!impl_->allow_batch_objects(objects)) {
    return Result<OperationResult>::failure(make_error(
        ErrorCode::InvalidArgument,
        "Batch callback cannot change unrelated object selection."));
  }
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
  changed = changed || before.active_object != impl_->state.active_object;

  if (changed) {
    impl_->undo_stack.push_back(std::move(before));
    impl_->redo_stack.clear();
    impl_->publish_state_transition(impl_->undo_stack.back());
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
  if (!impl_->allow_batch_object(object_id)) {
    return Result<OperationResult>::failure(make_error(
        ErrorCode::InvalidArgument,
        "Batch callback cannot rename an unrelated object."));
  }
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
    impl_->publish_state_transition(impl_->undo_stack.back());
  }
  OperationResult result = session_result(changed, "Object renamed.");
  result.dirty.geometry = false;
  result.dirty.selection = false;
  return Result<OperationResult>::success(std::move(result));
}

Result<OperationResult> ModelingSession::remove_object(ObjectId object_id) {
  if (!impl_->allow_batch_object(object_id)) {
    return Result<OperationResult>::failure(make_error(
        ErrorCode::InvalidArgument,
        "Batch callback cannot remove an unrelated object."));
  }
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
  impl_->publish_state_transition(impl_->undo_stack.back());
  OperationResult result = session_result(true, "Object removed.");
  result.dirty.topology = true;
  return Result<OperationResult>::success(std::move(result));
}

Result<ObjectId> ModelingSession::duplicate_object(ObjectId object_id,
                                                   Vec3 offset,
                                                   std::string name) {
  if (impl_->batch_transaction_active) {
    impl_->batch_transaction_violated = true;
    return Result<ObjectId>::failure(make_error(
        ErrorCode::InvalidArgument,
        "Batch callback cannot duplicate objects outside scheduled steps."));
  }
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

  Result<ObjectId> allocated_id = impl_->allocate_object_id();
  if (!allocated_id.ok()) {
    return allocated_id;
  }

  impl_->undo_stack.push_back(impl_->state);
  impl_->redo_stack.clear();
  for (SessionObject &item : impl_->state.objects) {
    item.selected = false;
  }
  const ObjectId duplicated = allocated_id.value();
  impl_->state.objects.push_back({
      .id = duplicated,
      .name = name.empty() ? object->name : std::move(name),
      .document = std::move(document),
      .selected = true,
  });
  impl_->record_topology_identity(
      duplicated, impl_->state.objects.back().document);
  impl_->state.active_object = duplicated;
  impl_->publish_state_transition(impl_->undo_stack.back());
  return Result<ObjectId>::success(duplicated);
}

Result<OperationResult> ModelingSession::copy_selection_to_clipboard() {
  if (impl_->preview.active) {
    return active_preview_error();
  }

  std::vector<ClipboardObject> copied;
  for (const SessionObject &object : impl_->state.objects) {
    if (!object.selected) {
      continue;
    }
    PolygonDocument document = object.document;
    Result<OperationResult> cleared = document.clear_selection();
    if (!cleared.ok()) {
      return cleared;
    }
    copied.push_back({.name = object.name, .document = std::move(document)});
  }
  if (copied.empty()) {
    return Result<OperationResult>::failure(make_error(
        ErrorCode::InvalidArgument,
        "Select one or more meshes before copying."));
  }

  impl_->clipboard = std::move(copied);
  OperationResult result;
  result.message = "Copied selected meshes.";
  return Result<OperationResult>::success(std::move(result));
}

Result<ClipboardPasteResult> ModelingSession::paste_clipboard(Vec3 offset) {
  if (impl_->batch_transaction_active) {
    impl_->batch_transaction_violated = true;
    return Result<ClipboardPasteResult>::failure(make_error(
        ErrorCode::InvalidArgument,
        "Clipboard paste is forbidden inside an atomic batch callback."));
  }
  if (impl_->preview.active) {
    return Result<ClipboardPasteResult>::failure(make_error(
        ErrorCode::InvalidArgument,
        "Commit or cancel the active preview before pasting."));
  }
  if (impl_->clipboard.empty()) {
    return Result<ClipboardPasteResult>::failure(make_error(
        ErrorCode::InvalidArgument,
        "Copy one or more meshes before pasting."));
  }

  const ModelingSession before = *this;
  const std::size_t undo_mark = undo_depth();
  std::vector<ObjectId> pasted;
  pasted.reserve(impl_->clipboard.size());
  for (const ClipboardObject &object : impl_->clipboard) {
    PolygonDocument document = object.document;
    if (offset.x != 0.0F || offset.y != 0.0F || offset.z != 0.0F) {
      Result<OperationResult> selected = document.select_all_faces();
      if (!selected.ok()) {
        restore_snapshot_preserving_identity_high_water(before);
        return Result<ClipboardPasteResult>::failure(selected.error());
      }
      Result<OperationResult> translated = document.translate_selection(offset);
      if (!translated.ok()) {
        restore_snapshot_preserving_identity_high_water(before);
        return Result<ClipboardPasteResult>::failure(translated.error());
      }
      Result<OperationResult> cleared = document.clear_selection();
      if (!cleared.ok()) {
        restore_snapshot_preserving_identity_high_water(before);
        return Result<ClipboardPasteResult>::failure(cleared.error());
      }
    }
    Result<ObjectId> added = add_document(std::move(document), object.name);
    if (!added.ok()) {
      restore_snapshot_preserving_identity_high_water(before);
      return Result<ClipboardPasteResult>::failure(added.error());
    }
    pasted.push_back(added.value());
  }

  Result<OperationResult> selected =
      apply_object_selection(pasted, SelectionEdit::Replace);
  if (!selected.ok()) {
    restore_snapshot_preserving_identity_high_water(before);
    return Result<ClipboardPasteResult>::failure(selected.error());
  }
  squash_undo_since(undo_mark);

  OperationResult operation = std::move(selected).value();
  operation.changed = true;
  operation.message = "Pasted copied meshes.";
  operation.dirty.topology = true;
  operation.dirty.geometry = true;
  operation.dirty.selection = true;
  operation.dirty.overlays = true;
  return Result<ClipboardPasteResult>::success({
      .operation = std::move(operation),
      .objects = std::move(pasted),
  });
}

bool ModelingSession::clipboard_available() const {
  return !impl_->clipboard.empty();
}

std::size_t ModelingSession::clipboard_object_count() const {
  return impl_->clipboard.size();
}

void ModelingSession::reset_workspace_preserving_clipboard() {
  std::vector<ClipboardObject> clipboard = std::move(impl_->clipboard);
  impl_ = std::make_unique<ModelingSessionImpl>();
  impl_->clipboard = std::move(clipboard);
}

Result<OperationResult>
ModelingSession::combine_objects(std::span<const ObjectId> object_ids,
                                  std::string name) {
  if (impl_->batch_transaction_active) {
    impl_->batch_transaction_violated = true;
    return Result<OperationResult>::failure(make_error(
        ErrorCode::InvalidArgument,
        "Batch callback cannot combine objects outside scheduled steps."));
  }
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

  PolygonDocument survivor_working = survivor->document;
  impl_->reconcile_topology_identity(survivor_id, survivor_working);
  quader_poly::Document survivor_document =
      PolygonDocumentNativeAccess::document(survivor_working);
  for (ObjectId source_id : unique_ids) {
    if (source_id == survivor_id) {
      continue;
    }
    const auto source = find_object(candidate, source_id);
    if (source == candidate.objects.end()) {
      return Result<OperationResult>::failure(
          make_error(ErrorCode::InvalidId, "Combine Mesh source is not live."));
    }
    Result<AppendedDocument> appended = append_document_with_remapped_ids(
        survivor_document, PolygonDocumentNativeAccess::document(source->document));
    if (!appended.ok()) {
      return Result<OperationResult>::failure(appended.error());
    }
    result.created.vertices.insert(result.created.vertices.end(),
                                   appended.value().created.vertices.begin(),
                                   appended.value().created.vertices.end());
    result.created.edges.insert(result.created.edges.end(),
                                appended.value().created.edges.begin(),
                                appended.value().created.edges.end());
    result.created.faces.insert(result.created.faces.end(),
                                appended.value().created.faces.begin(),
                                appended.value().created.faces.end());
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
  PolygonDocumentNativeAccess::replace_document(
      survivor->document, std::move(survivor_document));
  quader_poly::Selection survivor_selection =
      PolygonDocumentNativeAccess::selection(survivor->document);
  survivor_selection.clear();
  PolygonDocumentNativeAccess::replace_selection(
      survivor->document, std::move(survivor_selection));
  PolygonDocumentImpl &survivor_impl =
      PolygonDocumentNativeAccess::impl(survivor->document);
  ++survivor_impl.content_revision;
  ++survivor_impl.selection_revision;
  candidate.active_object = survivor_id;

  impl_->undo_stack.push_back(impl_->state);
  impl_->redo_stack.clear();
  impl_->state = std::move(candidate);
  const auto committed_survivor = find_object(impl_->state, survivor_id);
  if (committed_survivor != impl_->state.objects.end()) {
    impl_->record_topology_identity(survivor_id,
                                    committed_survivor->document);
  }
  const SessionObjectDirtyOverride dirty_overrides[] = {
      {.id = survivor_id, .dirty = result.dirty},
  };
  impl_->publish_state_transition(impl_->undo_stack.back(), dirty_overrides);
  return Result<OperationResult>::success(std::move(result));
}

Result<OperationResult>
ModelingSession::bridge_objects(ObjectId survivor_id, ObjectId source_id,
                                Transform3 survivor_from_source,
                                SelectionKind selection_kind, int steps) {
  if (impl_->batch_transaction_active) {
    impl_->batch_transaction_violated = true;
    return Result<OperationResult>::failure(make_error(
        ErrorCode::InvalidArgument,
        "Batch callback cannot bridge objects outside scheduled steps."));
  }
  if (impl_->preview.active) {
    return active_preview_error();
  }
  if (survivor_id == source_id ||
      (selection_kind != SelectionKind::Edge &&
       selection_kind != SelectionKind::Face)) {
    return Result<OperationResult>::failure(make_error(
        ErrorCode::InvalidArgument,
        "Bridge needs two distinct objects with edge or face selections."));
  }

  SessionState candidate = impl_->state;
  auto survivor = find_object(candidate, survivor_id);
  const auto source = find_object(candidate, source_id);
  if (survivor == candidate.objects.end() || source == candidate.objects.end()) {
    return Result<OperationResult>::failure(
        make_error(ErrorCode::InvalidId, "Bridge source object is not live."));
  }

  const quader_poly::Selection survivor_selection =
      PolygonDocumentNativeAccess::selection(survivor->document);
  const quader_poly::Selection source_selection =
      PolygonDocumentNativeAccess::selection(source->document);
  const bool face_bridge = selection_kind == SelectionKind::Face;
  if ((face_bridge && (survivor_selection.faces.empty() ||
                       source_selection.faces.empty())) ||
      (!face_bridge && (survivor_selection.edges.empty() ||
                        source_selection.edges.empty()))) {
    return Result<OperationResult>::failure(make_error(
        ErrorCode::InvalidArgument,
        face_bridge ? "Bridge Faces needs selected faces on both objects."
                    : "Bridge needs selected edges on both objects."));
  }
  if (!face_bridge &&
      survivor_selection.edges.size() != source_selection.edges.size()) {
    return Result<OperationResult>::failure(make_error(
        ErrorCode::InvalidArgument,
        "Bridge needs equal selected open-edge counts on both objects."));
  }

  PolygonDocument survivor_working = survivor->document;
  impl_->reconcile_topology_identity(survivor_id, survivor_working);
  quader_poly::Document survivor_document =
      PolygonDocumentNativeAccess::document(survivor_working);
  quader_poly::Document source_document =
      PolygonDocumentNativeAccess::document(source->document);
  for (quader_poly::Vertex &vertex : source_document.vertices) {
    vertex.position = transform_point(survivor_from_source, vertex.position);
  }
  if (determinant(survivor_from_source) < 0.0F) {
    for (quader_poly::Face &face : source_document.faces) {
      std::ranges::reverse(face.vertices);
      if (face.uvs.size() == face.vertices.size()) {
        std::ranges::reverse(face.uvs);
      } else {
        face.uvs.clear();
      }
    }
  }

  Result<AppendedDocument> appended =
      append_document_with_remapped_ids(survivor_document, source_document);
  if (!appended.ok()) {
    return Result<OperationResult>::failure(appended.error());
  }

  quader_poly::Selection bridge_selection;
  if (face_bridge) {
    bridge_selection.mode = quader_poly::SelectionMode::Face;
    bridge_selection.faces = survivor_selection.faces;
    for (quader_poly::ElementId id : source_selection.faces) {
      const auto mapped = appended.value().face_ids.find(id);
      if (mapped == appended.value().face_ids.end()) {
        return Result<OperationResult>::failure(make_error(
            ErrorCode::InternalError,
            "Bridge Faces could not remap a source face."));
      }
      bridge_selection.faces.push_back(mapped->second);
    }
    if (survivor_selection.has_active &&
        survivor_selection.active_kind == quader_poly::ElementKind::Face) {
      bridge_selection.has_active = true;
      bridge_selection.active_kind = quader_poly::ElementKind::Face;
      bridge_selection.active_face = survivor_selection.active_face;
    } else if (source_selection.has_active &&
               source_selection.active_kind == quader_poly::ElementKind::Face) {
      const auto mapped =
          appended.value().face_ids.find(source_selection.active_face);
      if (mapped != appended.value().face_ids.end()) {
        bridge_selection.has_active = true;
        bridge_selection.active_kind = quader_poly::ElementKind::Face;
        bridge_selection.active_face = mapped->second;
      }
    }
    if (!bridge_selection.has_active && !bridge_selection.faces.empty()) {
      bridge_selection.has_active = true;
      bridge_selection.active_kind = quader_poly::ElementKind::Face;
      bridge_selection.active_face = bridge_selection.faces.front();
    }
  } else {
    bridge_selection.mode = quader_poly::SelectionMode::Edge;
    bridge_selection.edges = survivor_selection.edges;
    for (quader_poly::Edge edge : source_selection.edges) {
      const auto a = appended.value().vertex_ids.find(edge.a);
      const auto b = appended.value().vertex_ids.find(edge.b);
      if (a == appended.value().vertex_ids.end() ||
          b == appended.value().vertex_ids.end()) {
        return Result<OperationResult>::failure(make_error(
            ErrorCode::InternalError,
            "Bridge could not remap a source edge."));
      }
      bridge_selection.edges.push_back(
          quader_poly::make_edge(a->second, b->second));
    }
    if (survivor_selection.has_active &&
        survivor_selection.active_kind == quader_poly::ElementKind::Edge) {
      bridge_selection.has_active = true;
      bridge_selection.active_kind = quader_poly::ElementKind::Edge;
      bridge_selection.active_edge = survivor_selection.active_edge;
    } else if (source_selection.has_active &&
               source_selection.active_kind == quader_poly::ElementKind::Edge) {
      const auto a =
          appended.value().vertex_ids.find(source_selection.active_edge.a);
      const auto b =
          appended.value().vertex_ids.find(source_selection.active_edge.b);
      if (a != appended.value().vertex_ids.end() &&
          b != appended.value().vertex_ids.end()) {
        bridge_selection.has_active = true;
        bridge_selection.active_kind = quader_poly::ElementKind::Edge;
        bridge_selection.active_edge =
            quader_poly::make_edge(a->second, b->second);
      }
    }
    if (!bridge_selection.has_active && !bridge_selection.edges.empty()) {
      bridge_selection.has_active = true;
      bridge_selection.active_kind = quader_poly::ElementKind::Edge;
      bridge_selection.active_edge = bridge_selection.edges.front();
    }
  }

  const quader_poly::OperationResult native_result =
      face_bridge
          ? quader_poly::bridge_selected_faces(survivor_document,
                                               bridge_selection, steps)
          : quader_poly::bridge_selected_edges(survivor_document,
                                               bridge_selection, steps);
  if (!native_result.changed) {
    OperationResult unchanged;
    unchanged.message = native_result.message;
    return Result<OperationResult>::success(std::move(unchanged));
  }

  OperationResult result = session_result(
      true, face_bridge ? "Faces bridged." : "Edges bridged.");
  result.dirty.topology = true;
  result.created = appended.value().created;
  append_native_delta(result.created, native_result.created);
  append_native_delta(result.deleted, native_result.deleted);
  append_native_delta(result.affected, native_result.affected);
  result.modified = result.affected;

  PolygonDocumentNativeAccess::replace_document(
      survivor->document, std::move(survivor_document));
  PolygonDocumentNativeAccess::replace_selection(
      survivor->document, std::move(bridge_selection));
  PolygonDocumentImpl &survivor_impl =
      PolygonDocumentNativeAccess::impl(survivor->document);
  ++survivor_impl.content_revision;
  ++survivor_impl.selection_revision;

  candidate.objects.erase(
      std::remove_if(candidate.objects.begin(), candidate.objects.end(),
                     [source_id](const SessionObject &object) {
                       return object.id == source_id;
                     }),
      candidate.objects.end());
  for (SessionObject &object : candidate.objects) {
    object.selected = object.id == survivor_id;
  }
  candidate.active_object = survivor_id;

  impl_->undo_stack.push_back(impl_->state);
  impl_->redo_stack.clear();
  impl_->state = std::move(candidate);
  const auto committed_survivor = find_object(impl_->state, survivor_id);
  if (committed_survivor != impl_->state.objects.end()) {
    impl_->record_topology_identity(survivor_id,
                                    committed_survivor->document);
  }
  const SessionObjectDirtyOverride dirty_overrides[] = {
      {.id = survivor_id, .dirty = result.dirty},
  };
  impl_->publish_state_transition(impl_->undo_stack.back(), dirty_overrides);
  return Result<OperationResult>::success(std::move(result));
}

Result<OperationResult> ModelingSession::select_face(ObjectId object_id,
                                                     FaceId face_id) {
  if (!impl_->allow_batch_object(object_id)) {
    return Result<OperationResult>::failure(make_error(
        ErrorCode::InvalidArgument,
        "Batch callback cannot select an unrelated object."));
  }
  if (impl_->preview.active) {
    return active_preview_error();
  }
  SessionState candidate = impl_->state;
  auto object = find_object(candidate, object_id);
  if (object == candidate.objects.end()) {
    return Result<OperationResult>::failure(
        make_error(ErrorCode::InvalidId, "Object ID is not live."));
  }

  Result<OperationResult> result = object->document.select_face(face_id);
  if (!result.ok() || !result.value().success || !result.value().changed) {
    return result;
  }

  candidate.active_object = object_id;
  object->selected = true;
  impl_->undo_stack.push_back(impl_->state);
  impl_->redo_stack.clear();
  impl_->state = std::move(candidate);
  const SessionObjectDirtyOverride dirty_overrides[] = {
      {.id = object_id, .dirty = result.value().dirty},
  };
  impl_->publish_state_transition(impl_->undo_stack.back(), dirty_overrides);
  return result;
}

Result<OperationResult> ModelingSession::translate_selection(Vec3 delta) {
  if (impl_->batch_transaction_active &&
      !impl_->allow_batch_object(impl_->state.active_object)) {
    return Result<OperationResult>::failure(make_error(
        ErrorCode::InvalidArgument,
        "Batch callback cannot transform an unrelated selection."));
  }
  if (impl_->preview.active) {
    return active_preview_error();
  }

  SessionState candidate = impl_->state;
  auto found = std::ranges::find_if(
      candidate.objects, [&](const SessionObject &object) {
        return object.id == candidate.active_object && object.selected;
      });
  if (found == candidate.objects.end()) {
    return Result<OperationResult>::failure(
        make_error(ErrorCode::InvalidId, "No active selected object."));
  }

  Result<OperationResult> result = found->document.translate_selection(delta);
  if (!result.ok() || !result.value().success || !result.value().changed) {
    return result;
  }

  const ObjectId object_id = found->id;
  impl_->undo_stack.push_back(impl_->state);
  impl_->redo_stack.clear();
  impl_->state = std::move(candidate);
  const SessionObjectDirtyOverride dirty_overrides[] = {
      {.id = object_id, .dirty = result.value().dirty},
  };
  impl_->publish_state_transition(impl_->undo_stack.back(), dirty_overrides);
  return result;
}

Result<OperationResult>
ModelingSession::begin_translate_preview(ObjectId object_id, Vec3 delta) {
  if (impl_->batch_transaction_active) {
    impl_->batch_transaction_violated = true;
    return Result<OperationResult>::failure(make_error(
        ErrorCode::InvalidArgument,
        "Batch callback cannot start a preview transaction."));
  }
  if (impl_->preview.active) {
    return active_preview_error();
  }

  auto object = find_object(impl_->state, object_id);
  if (object == impl_->state.objects.end()) {
    return Result<OperationResult>::failure(
        make_error(ErrorCode::InvalidId, "Object ID is not live."));
  }

  PreviewTransaction preview;
  Result<PreviewId> preview_id = impl_->allocate_preview_id();
  if (!preview_id.ok()) {
    return Result<OperationResult>::failure(preview_id.error());
  }
  preview.id = preview_id.value();
  preview.active = true;
  preview.object_id = object_id;
  preview.operation_id = kTranslateSelectionOperationId;
  preview.base_document = object->document;
  preview.base_content_identity = object->document.content_identity();
  preview.base_content_revision = object->document.content_revision();
  preview.base_session_revision = impl_->content_revision;
  Result<OperationResult> result =
      apply_preview_delta(preview, object->document, delta);
  if (!result.ok()) {
    return result;
  }

  preview.payload_revision = impl_->advance_preview_payload_revision(
      preview.preview_document.content_revision());
  preview.receipt = result.value();
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
    impl_->preview.payload_revision = impl_->advance_preview_payload_revision(
        impl_->preview.preview_document.content_revision());
    impl_->preview.receipt = result.value();
    result.value().message = "Translate preview updated.";
  }
  return result;
}

Result<PreviewId> ModelingSession::begin_document_preview(
    ObjectId object_id, std::string operation_id,
    PolygonDocument preview_document, OperationResult receipt) {
  if (impl_->batch_transaction_active) {
    impl_->batch_transaction_violated = true;
    return Result<PreviewId>::failure(make_error(
        ErrorCode::InvalidArgument,
        "Batch callback cannot start a preview transaction."));
  }
  if (impl_->preview.active) {
    return Result<PreviewId>::failure(make_error(
        ErrorCode::InvalidArgument,
        "Commit or cancel the active preview before starting another preview."));
  }
  auto object = find_object(impl_->state, object_id);
  if (object == impl_->state.objects.end()) {
    return Result<PreviewId>::failure(
        make_error(ErrorCode::InvalidId, "Preview object ID is not live."));
  }
  Result<OperationResult> identity =
      impl_->validate_commit_identity(object_id, preview_document);
  if (!identity.ok()) {
    return Result<PreviewId>::failure(identity.error());
  }
  if (!receipt.success) {
    return Result<PreviewId>::failure(make_error(
        receipt.error_code == ErrorCode::Ok ? ErrorCode::ValidationFailed
                                            : receipt.error_code,
        receipt.message.empty() ? "Preview operation failed." : receipt.message));
  }
  Result<ToolPreviewPayload> payload = build_preview_payload(preview_document);
  if (!payload.ok()) {
    return Result<PreviewId>::failure(payload.error());
  }
  Result<PreviewId> allocated = impl_->allocate_preview_id();
  if (!allocated.ok()) {
    return allocated;
  }

  PreviewTransaction preview;
  preview.id = allocated.value();
  preview.active = true;
  preview.object_id = object_id;
  preview.operation_id = std::move(operation_id);
  preview.base_document = object->document;
  preview.base_content_identity = object->document.content_identity();
  preview.base_content_revision = object->document.content_revision();
  preview.base_session_revision = impl_->content_revision;
  preview.preview_document = std::move(preview_document);
  preview.receipt = std::move(receipt);
  preview.receipt.dirty.overlays = true;
  preview.dirty = preview.receipt.dirty;
  preview.payload = std::move(payload).value();
  preview.payload_valid = true;
  preview.payload_revision = impl_->advance_preview_payload_revision(
      preview.preview_document.content_revision());
  preview.payload.mesh.content_revision = preview.payload_revision;
  impl_->preview = std::move(preview);
  return allocated;
}

Result<OperationResult> ModelingSession::update_document_preview(
    PreviewId preview_id, PolygonDocument preview_document,
    OperationResult receipt) {
  if (!impl_->preview.active || impl_->preview.id != preview_id) {
    return Result<OperationResult>::failure(
        make_error(ErrorCode::StaleId, "Preview ID is not active."));
  }
  if (!impl_->preview_matches_base()) {
    return Result<OperationResult>::failure(make_error(
        ErrorCode::StaleId,
        "Preview base changed before the preview could be updated."));
  }
  Result<OperationResult> identity = impl_->validate_commit_identity(
      impl_->preview.object_id, preview_document);
  if (!identity.ok()) {
    return identity;
  }
  if (!receipt.success) {
    return Result<OperationResult>::failure(make_error(
        receipt.error_code == ErrorCode::Ok ? ErrorCode::ValidationFailed
                                            : receipt.error_code,
        receipt.message.empty() ? "Preview operation failed." : receipt.message));
  }
  Result<ToolPreviewPayload> payload = build_preview_payload(preview_document);
  if (!payload.ok()) {
    return Result<OperationResult>::failure(payload.error());
  }
  receipt.dirty.overlays = true;
  impl_->preview.preview_document = std::move(preview_document);
  impl_->preview.receipt = receipt;
  impl_->preview.dirty = receipt.dirty;
  impl_->preview.payload = std::move(payload).value();
  impl_->preview.payload_valid = true;
  impl_->preview.payload_revision = impl_->advance_preview_payload_revision(
      impl_->preview.preview_document.content_revision());
  impl_->preview.payload.mesh.content_revision =
      impl_->preview.payload_revision;
  return Result<OperationResult>::success(std::move(receipt));
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

  if (!impl_->preview_matches_base()) {
    return Result<OperationResult>::failure(make_error(
        ErrorCode::StaleId,
        "Preview base changed before the preview could be committed."));
  }
  Result<OperationResult> identity = impl_->validate_commit_identity(
      impl_->preview.object_id, impl_->preview.preview_document);
  if (!identity.ok()) {
    return identity;
  }

  OperationResult result = impl_->preview.receipt;
  result.dirty.overlays = true;
  if (!result.changed) {
    impl_->preview = PreviewTransaction{};
    result.message = "Preview completed without changes.";
    return Result<OperationResult>::success(std::move(result));
  }

  impl_->undo_stack.push_back(impl_->state);
  impl_->redo_stack.clear();
  const ObjectId preview_object = object->id;
  const DirtyFlags preview_dirty = impl_->preview.dirty;
  object->document = impl_->preview.preview_document;
  impl_->record_topology_identity(object->id, object->document);
  object->selected = true;
  impl_->state.active_object = object->id;
  impl_->preview = PreviewTransaction{};
  const SessionObjectDirtyOverride dirty_overrides[] = {
      {.id = preview_object, .dirty = preview_dirty},
  };
  impl_->publish_state_transition(impl_->undo_stack.back(), dirty_overrides);

  result.message = "Preview committed.";
  result.dirty = preview_dirty;
  return Result<OperationResult>::success(std::move(result));
}

Result<OperationResult> ModelingSession::commit_preview(PreviewId preview_id) {
  if (!impl_->preview.active || impl_->preview.id != preview_id) {
    return Result<OperationResult>::failure(
        make_error(ErrorCode::StaleId, "Preview ID is not active."));
  }
  return commit_preview();
}

Result<OperationResult> ModelingSession::cancel_preview() {
  if (!impl_->preview.active) {
    return missing_preview_error();
  }
  impl_->preview = PreviewTransaction{};
  OperationResult result = session_result(false, "Preview canceled.");
  result.dirty.geometry = false;
  result.dirty.selection = false;
  result.dirty.overlays = true;
  return Result<OperationResult>::success(std::move(result));
}

Result<OperationResult> ModelingSession::cancel_preview(PreviewId preview_id) {
  if (!impl_->preview.active || impl_->preview.id != preview_id) {
    return Result<OperationResult>::failure(
        make_error(ErrorCode::StaleId, "Preview ID is not active."));
  }
  return cancel_preview();
}

Result<OperationResult> ModelingSession::undo() {
  if (impl_->batch_transaction_active) {
    impl_->batch_transaction_violated = true;
    return Result<OperationResult>::failure(make_error(
        ErrorCode::InvalidArgument,
        "Undo is forbidden inside an atomic batch callback."));
  }
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
  impl_->reconcile_restored_topology_identities();
  impl_->publish_state_transition(impl_->redo_stack.back());
  return Result<OperationResult>::success(session_result(true, "Undo."));
}

Result<OperationResult> ModelingSession::redo() {
  if (impl_->batch_transaction_active) {
    impl_->batch_transaction_violated = true;
    return Result<OperationResult>::failure(make_error(
        ErrorCode::InvalidArgument,
        "Redo is forbidden inside an atomic batch callback."));
  }
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
  impl_->reconcile_restored_topology_identities();
  impl_->publish_state_transition(impl_->undo_stack.back());
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
  if (impl_->batch_transaction_active) {
    impl_->batch_transaction_violated = true;
    return;
  }
  if (mark >= impl_->undo_stack.size()) {
    return;
  }
  SessionState first_batch_snapshot = impl_->undo_stack[mark];
  impl_->undo_stack.erase(impl_->undo_stack.begin() + mark,
                          impl_->undo_stack.end());
  impl_->undo_stack.push_back(std::move(first_batch_snapshot));
}

bool ModelingSession::rollback_undo_since(std::size_t mark) {
  if (impl_->batch_transaction_active) {
    impl_->batch_transaction_violated = true;
    return false;
  }
  if (mark >= impl_->undo_stack.size()) {
    return false;
  }
  SessionState before = impl_->state;
  impl_->state = impl_->undo_stack[mark];
  impl_->undo_stack.erase(impl_->undo_stack.begin() + mark,
                          impl_->undo_stack.end());
  impl_->redo_stack.clear();
  impl_->preview = PreviewTransaction{};
  impl_->reconcile_restored_topology_identities();
  impl_->publish_state_transition(before);
  return true;
}

bool ModelingSession::preview_active() const {
  return impl_->preview.active;
}

void ModelingSession::restore_snapshot_preserving_identity_high_water(
    const ModelingSession &snapshot) {
  if (this == &snapshot) {
    return;
  }

  const std::uint32_t next_object_index = impl_->next_object_index;
  const std::uint32_t next_preview_index = impl_->next_preview_index;
  const std::uint64_t preview_payload_revision =
      impl_->preview_payload_revision;
  const auto topology_identity_high_water =
      impl_->topology_identity_high_water;
  std::vector<ClipboardObject> clipboard = std::move(impl_->clipboard);

  *impl_ = *snapshot.impl_;
  impl_->clipboard = std::move(clipboard);
  impl_->next_object_index =
      std::max(impl_->next_object_index, next_object_index);
  impl_->next_preview_index =
      std::max(impl_->next_preview_index, next_preview_index);
  impl_->preview_payload_revision =
      std::max(impl_->preview_payload_revision, preview_payload_revision);
  for (const auto &[object_index, high_water] :
       topology_identity_high_water) {
    auto &restored = impl_->topology_identity_high_water[object_index];
    restored.next_vertex_id =
        std::max(restored.next_vertex_id, high_water.next_vertex_id);
    restored.next_face_id =
        std::max(restored.next_face_id, high_water.next_face_id);
  }
}

void ModelingSession::restore_snapshot_as_history_transition(
    const ModelingSession &snapshot) {
  if (this == &snapshot) {
    return;
  }

  const SessionState before = impl_->state;
  const std::uint32_t next_object_index = impl_->next_object_index;
  const std::uint32_t next_preview_index = impl_->next_preview_index;
  const std::uint64_t content_revision = impl_->content_revision;
  const std::uint64_t selection_revision = impl_->selection_revision;
  const std::uint64_t preview_payload_revision =
      impl_->preview_payload_revision;
  auto object_changes = impl_->object_changes;
  const auto topology_identity_high_water =
      impl_->topology_identity_high_water;
  std::vector<ClipboardObject> clipboard = std::move(impl_->clipboard);

  *impl_ = *snapshot.impl_;
  impl_->clipboard = std::move(clipboard);
  impl_->next_object_index =
      std::max(impl_->next_object_index, next_object_index);
  impl_->next_preview_index =
      std::max(impl_->next_preview_index, next_preview_index);
  impl_->content_revision =
      std::max(impl_->content_revision, content_revision);
  impl_->selection_revision =
      std::max(impl_->selection_revision, selection_revision);
  impl_->preview_payload_revision =
      std::max(impl_->preview_payload_revision, preview_payload_revision);
  impl_->object_changes = std::move(object_changes);
  for (const auto &[object_index, high_water] :
       topology_identity_high_water) {
    auto &restored = impl_->topology_identity_high_water[object_index];
    restored.next_vertex_id =
        std::max(restored.next_vertex_id, high_water.next_vertex_id);
    restored.next_face_id =
        std::max(restored.next_face_id, high_water.next_face_id);
  }
  impl_->reconcile_restored_topology_identities();
  impl_->publish_state_transition(before);
}

void ModelingSession::clear_history() {
  impl_->undo_stack.clear();
  impl_->redo_stack.clear();
}

bool ModelingSession::preview_active(PreviewId preview_id) const {
  return impl_->preview.active && impl_->preview.id == preview_id;
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

std::uint64_t ModelingSession::content_revision() const {
  return impl_->content_revision;
}

std::uint64_t ModelingSession::selection_revision() const {
  return impl_->selection_revision;
}

SessionChangeSnapshot ModelingSession::changes_since(
    std::uint64_t previous_content_revision) const {
  SessionChangeSnapshot snapshot{
      .content_revision = impl_->content_revision,
      .selection_revision = impl_->selection_revision,
  };
  for (const SessionObjectChangeRecord &record : impl_->object_changes) {
    if (record.revision <= previous_content_revision) {
      continue;
    }
    snapshot.objects.push_back({
        .id = record.id,
        .name = record.name,
        .dirty = {
            .topology =
                record.topology_revision > previous_content_revision,
            .geometry =
                record.geometry_revision > previous_content_revision,
            .selection =
                record.selection_revision > previous_content_revision,
            .materials =
                record.materials_revision > previous_content_revision,
            .overlays =
                record.overlays_revision > previous_content_revision,
        },
        .deleted = record.deleted,
    });
  }
  return snapshot;
}

const PolygonDocument *ModelingSession::document(ObjectId object_id) const {
  const auto found = find_object(impl_->state, object_id);
  return found == impl_->state.objects.end() ? nullptr : &found->document;
}

Result<std::shared_ptr<const PolygonDocument>>
ModelingSession::document_snapshot(ObjectId object_id) const {
  Result<PolygonDocument> copy = document_copy(object_id);
  if (!copy.ok()) {
    return Result<std::shared_ptr<const PolygonDocument>>::failure(copy.error());
  }
  return Result<std::shared_ptr<const PolygonDocument>>::success(
      std::make_shared<const PolygonDocument>(std::move(copy).value()));
}

Result<PolygonDocument>
ModelingSession::document_copy(ObjectId object_id) const {
  const auto found = find_object(impl_->state, object_id);
  if (found == impl_->state.objects.end()) {
    return Result<PolygonDocument>::failure(
        make_error(ErrorCode::InvalidId, "Object ID is not live."));
  }
  PolygonDocument copy = found->document;
  impl_->reconcile_topology_identity(object_id, copy);
  return Result<PolygonDocument>::success(std::move(copy));
}

Result<OperationResult>
ModelingSession::commit_document(ObjectId object_id, PolygonDocument document,
                                  OperationResult receipt) {
  if (!impl_->allow_batch_object(object_id)) {
    return Result<OperationResult>::failure(make_error(
        ErrorCode::InvalidArgument,
        "Batch callback cannot mutate an unrelated document."));
  }
  if (impl_->preview.active) {
    return active_preview_error();
  }
  auto found = find_object(impl_->state, object_id);
  if (found == impl_->state.objects.end()) {
    return Result<OperationResult>::failure(
        make_error(ErrorCode::InvalidId, "Object ID is not live."));
  }
  if (!receipt.success || !receipt.changed) {
    return Result<OperationResult>::success(std::move(receipt));
  }

  Result<OperationResult> identity =
      impl_->validate_commit_identity(object_id, document);
  if (!identity.ok()) {
    return identity;
  }

  impl_->undo_stack.push_back(impl_->state);
  impl_->redo_stack.clear();
  found->document = std::move(document);
  impl_->record_topology_identity(object_id, found->document);
  found->selected = true;
  impl_->state.active_object = found->id;
  const SessionObjectDirtyOverride dirty_overrides[] = {
      {.id = object_id, .dirty = receipt.dirty},
  };
  impl_->publish_state_transition(impl_->undo_stack.back(), dirty_overrides);
  return Result<OperationResult>::success(std::move(receipt));
}

Result<ToolPreviewPayload> ModelingSession::preview_payload() const {
  if (!impl_->preview.active) {
    return Result<ToolPreviewPayload>::failure(
        make_error(ErrorCode::InvalidArgument, "No active preview transaction."));
  }
  return materialize_preview_payload(impl_->preview);
}

Result<ToolPreviewPayload>
ModelingSession::preview_payload(PreviewId preview_id) const {
  if (!impl_->preview.active || impl_->preview.id != preview_id) {
    return Result<ToolPreviewPayload>::failure(
        make_error(ErrorCode::StaleId, "Preview ID is not active."));
  }
  return materialize_preview_payload(impl_->preview);
}

Result<PolygonDocument>
ModelingSession::preview_document_copy(PreviewId preview_id) const {
  if (!impl_->preview.active || impl_->preview.id != preview_id) {
    return Result<PolygonDocument>::failure(
        make_error(ErrorCode::StaleId, "Preview ID is not active."));
  }
  return Result<PolygonDocument>::success(impl_->preview.preview_document);
}

PreviewTransactionSummary ModelingSession::preview_summary() const {
  if (!impl_->preview.active) {
    return {};
  }
  return {
      .id = impl_->preview.id,
      .active = true,
      .object_id = impl_->preview.object_id,
      .operation_id = impl_->preview.operation_id,
      .delta = impl_->preview.delta,
      .content_revision = impl_->preview.payload_revision,
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

bool ModelingSession::begin_batch_transaction() {
  if (impl_->batch_transaction_active) {
    impl_->batch_transaction_violated = true;
    return false;
  }
  if (impl_->preview.active) {
    return false;
  }
  impl_->batch_transaction_active = true;
  impl_->batch_transaction_violated = false;
  impl_->batch_creation_authorized = false;
  impl_->batch_target = {};
  return true;
}

void ModelingSession::authorize_batch_creation() {
  if (!impl_->batch_transaction_active || impl_->batch_target.valid()) {
    impl_->batch_transaction_violated = true;
    return;
  }
  impl_->batch_creation_authorized = true;
}

void ModelingSession::target_batch_object(ObjectId object_id) {
  if (!impl_->batch_transaction_active ||
      find_object(impl_->state, object_id) == impl_->state.objects.end()) {
    impl_->batch_transaction_violated = true;
    return;
  }
  impl_->batch_creation_authorized = false;
  impl_->batch_target = object_id;
}

void ModelingSession::clear_batch_target() {
  if (!impl_->batch_transaction_active) {
    return;
  }
  impl_->batch_target = {};
}

bool ModelingSession::finish_batch_transaction() {
  if (!impl_->batch_transaction_active) {
    return false;
  }
  const bool valid = !impl_->batch_transaction_violated;
  impl_->batch_transaction_active = false;
  impl_->batch_transaction_violated = false;
  impl_->batch_creation_authorized = false;
  impl_->batch_target = {};
  return valid;
}

} // namespace quader::modeling
