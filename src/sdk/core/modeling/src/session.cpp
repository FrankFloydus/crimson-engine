////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <quader/modeling/session/modeling_session.hpp>

#include "polygon_document_native.hpp"

#include <algorithm>
#include <diagnostics/profile.hpp>
#include <iterator>
#include <limits>
#include <quader/modeling/neutral/polygon/document.hpp>
#include <quader/modeling/neutral/polygon/document_operations.hpp>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace quader::modeling {
namespace {

constexpr const char *kTranslateSelectionOperationId =
    "modeling.translate_selection";

using SessionDocument = std::variant<PolygonDocument, CurveDocument>;
using SessionContentIdentity =
    std::variant<PolygonContentIdentity, CurveContentIdentity>;

[[nodiscard]] SessionObjectKind document_kind(
    const SessionDocument &document) noexcept {
  return std::holds_alternative<CurveDocument>(document)
             ? SessionObjectKind::Curve
             : SessionObjectKind::Mesh;
}

[[nodiscard]] std::uint64_t document_content_revision(
    const SessionDocument &document) {
  return std::visit([](const auto &value) { return value.content_revision(); },
                    document);
}

[[nodiscard]] SessionContentIdentity document_content_identity(
    const SessionDocument &document) {
  return std::visit(
      [](const auto &value) -> SessionContentIdentity {
        return value.content_identity();
      }, document);
}

[[nodiscard]] std::uint64_t document_selection_revision(
    const SessionDocument &document) {
  const auto *polygon = std::get_if<PolygonDocument>(&document);
  return polygon != nullptr ? polygon->selection_revision() : 0U;
}

[[nodiscard]] Error wrong_document_kind_error() {
  return make_error(ErrorCode::UnsupportedOperation,
                    "The operation requires a different document kind.");
}

/**
 * Stores one object in the headless SDK session.
 */
struct SessionObject {
  ObjectId id{};
  std::string name;
  SessionDocument document;
  bool selected = false;

  [[nodiscard]] SessionObjectKind kind() const noexcept {
    return document_kind(document);
  }

  [[nodiscard]] PolygonDocument &polygon_document() {
    return std::get<PolygonDocument>(document);
  }

  [[nodiscard]] const PolygonDocument &polygon_document() const {
    return std::get<PolygonDocument>(document);
  }

  [[nodiscard]] CurveDocument &curve_document() {
    return std::get<CurveDocument>(document);
  }

  [[nodiscard]] const CurveDocument &curve_document() const {
    return std::get<CurveDocument>(document);
  }
};

/**
 * Stores the copyable state snapshot used by SDK undo and redo.
 */
struct SessionState {
  std::vector<SessionObject> objects;
  ObjectId active_object{};
};

/**
 * Stores the reversible portion of one component-selection publication.
 *
 * The selection payload is an alternate rather than a before/after pair. Undo
 * and redo swap it with the live document while immutable topology stays
 * shared and untouched.
 */
struct ComponentSelectionHistoryObject {
  ObjectId id{};
  std::string name;
  std::optional<PolygonDocumentSelectionState> alternate_selection;
  bool alternate_selected = false;
  bool document_selection_changed = false;
  bool object_selection_changed = false;
  SessionObjectKind kind = SessionObjectKind::Mesh;
};

/// Stores one reversible sparse component-selection history entry.
struct ComponentSelectionHistoryDelta {
  std::vector<ComponentSelectionHistoryObject> objects;
  ObjectId alternate_active_object{};
  bool active_object_changed = false;
};

/**
 * Stores either the legacy whole-state checkpoint required by document and
 * topology mutations, or a sparse reversible component-selection delta.
 */
struct SessionHistoryEntry {
  std::variant<SessionState, ComponentSelectionHistoryDelta> value;

  SessionHistoryEntry(const SessionState &snapshot) : value(snapshot) {}
  SessionHistoryEntry(SessionState &&snapshot)
      : value(std::move(snapshot)) {}
  SessionHistoryEntry(ComponentSelectionHistoryDelta &&delta)
      : value(std::move(delta)) {}

  [[nodiscard]] bool is_state_snapshot() const noexcept {
    return std::holds_alternative<SessionState>(value);
  }

  [[nodiscard]] SessionState &state_snapshot() {
    return std::get<SessionState>(value);
  }

  [[nodiscard]] const SessionState &state_snapshot() const {
    return std::get<SessionState>(value);
  }

  [[nodiscard]] ComponentSelectionHistoryDelta &selection_delta() {
    return std::get<ComponentSelectionHistoryDelta>(value);
  }

  [[nodiscard]] const ComponentSelectionHistoryDelta &
  selection_delta() const {
    return std::get<ComponentSelectionHistoryDelta>(value);
  }

  // Existing full-state mutation call sites publish their freshly pushed
  // checkpoint directly. Keep that code compact while sparse entries remain
  // explicit in undo/redo and component-selection publication.
  [[nodiscard]] operator const SessionState &() const {
    return state_snapshot();
  }
};

/**
 * Stores one portable object clipboard payload outside undoable workspace
 * state. Object identities and selection are deliberately not copied.
 */
struct ClipboardObject {
  std::string name;
  SessionDocument document;
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
  SessionDocument base_document;
  SessionContentIdentity base_content_identity{};
  std::uint64_t base_content_revision = 0;
  std::uint64_t base_session_revision = 0;
  SessionDocument preview_document;
  OperationResult receipt;
  ToolPreviewPayload payload;
  bool payload_valid = false;
  DirtyFlags dirty;
  std::uint64_t payload_revision = 0;
};

/// Separates rollback tokens from copies and replacements of a session.
struct PreviewCheckpointOrigin {};

/// Copying session state starts a new token origin; moving its owner does not.
struct PreviewCheckpointOwnership {
  std::shared_ptr<const PreviewCheckpointOrigin> origin =
      std::make_shared<const PreviewCheckpointOrigin>();

  PreviewCheckpointOwnership() = default;
  PreviewCheckpointOwnership(const PreviewCheckpointOwnership &)
      : PreviewCheckpointOwnership() {}
  PreviewCheckpointOwnership &operator=(const PreviewCheckpointOwnership &other) {
    if (this != &other) {
      origin = std::make_shared<const PreviewCheckpointOrigin>();
    }
    return *this;
  }
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
  SessionObjectKind kind = SessionObjectKind::Mesh;
};

/**
 * Carries one state-diff entry before it receives the shared session revision.
 */
struct PendingSessionObjectChange {
  ObjectId id{};
  std::string name;
  DirtyFlags dirty;
  bool deleted = false;
  SessionObjectKind kind = SessionObjectKind::Mesh;
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
  CurveIdentityHighWater curve{};
};

[[nodiscard]] constexpr std::uint64_t object_id_key(ObjectId id) noexcept {
  return (static_cast<std::uint64_t>(id.generation) << 32U) |
         static_cast<std::uint64_t>(id.index);
}

[[nodiscard]] constexpr std::uint64_t vertex_id_key(VertexId id) noexcept {
  return (static_cast<std::uint64_t>(id.generation) << 32U) |
         static_cast<std::uint64_t>(id.index);
}

[[nodiscard]] std::optional<SelectionMode>
selection_mode_for_kind(SelectionKind kind) {
  switch (kind) {
  case SelectionKind::Vertex:
    return SelectionMode::Vertex;
  case SelectionKind::Edge:
    return SelectionMode::Edge;
  case SelectionKind::Face:
    return SelectionMode::Face;
  case SelectionKind::Object:
    return std::nullopt;
  }
  return std::nullopt;
}

[[nodiscard]] bool same_polygon_selection(
    const PolygonSelectionSnapshot &left,
    const PolygonSelectionSnapshot &right) {
  if (left.kind != right.kind || left.vertices != right.vertices ||
      left.edges != right.edges || left.faces != right.faces ||
      left.has_active != right.has_active) {
    return false;
  }
  if (!left.has_active) {
    return true;
  }
  switch (left.kind) {
  case SelectionKind::Vertex:
    return left.active_vertex == right.active_vertex;
  case SelectionKind::Edge:
    return left.active_edge == right.active_edge;
  case SelectionKind::Face:
    return left.active_face == right.active_face;
  case SelectionKind::Object:
    return true;
  }
  return false;
}

[[nodiscard]] std::uint64_t polygon_selection_signature(
    const PolygonSelectionSnapshot &selection) noexcept {
  constexpr std::uint64_t kOffsetBasis = 14695981039346656037ULL;
  constexpr std::uint64_t kPrime = 1099511628211ULL;
  std::uint64_t hash = kOffsetBasis;
  const auto mix = [&hash](std::uint64_t value) {
    hash ^= value;
    hash *= kPrime;
  };
  mix(static_cast<std::uint64_t>(selection.kind));
  mix(selection.vertices.size());
  for (const VertexId id : selection.vertices) {
    mix((static_cast<std::uint64_t>(id.generation) << 32U) | id.index);
  }
  mix(selection.edges.size());
  for (const EdgeKey edge : selection.edges) {
    mix((static_cast<std::uint64_t>(edge.a.generation) << 32U) |
        edge.a.index);
    mix((static_cast<std::uint64_t>(edge.b.generation) << 32U) |
        edge.b.index);
  }
  mix(selection.faces.size());
  for (const FaceId id : selection.faces) {
    mix((static_cast<std::uint64_t>(id.generation) << 32U) | id.index);
  }
  mix(selection.has_active ? 1U : 0U);
  if (!selection.has_active) {
    return hash;
  }
  switch (selection.kind) {
  case SelectionKind::Vertex:
    mix((static_cast<std::uint64_t>(selection.active_vertex.generation)
         << 32U) |
        selection.active_vertex.index);
    break;
  case SelectionKind::Edge:
    mix((static_cast<std::uint64_t>(selection.active_edge.a.generation)
         << 32U) |
        selection.active_edge.a.index);
    mix((static_cast<std::uint64_t>(selection.active_edge.b.generation)
         << 32U) |
        selection.active_edge.b.index);
    break;
  case SelectionKind::Face:
    mix((static_cast<std::uint64_t>(selection.active_face.generation)
         << 32U) |
        selection.active_face.index);
    break;
  case SelectionKind::Object:
    break;
  }
  return hash;
}

[[nodiscard]] bool same_polygon_selection(const PolygonDocument &left,
                                          const PolygonDocument &right) {
  return same_polygon_selection(left.selection(), right.selection());
}

[[nodiscard]] bool same_document_selection(const SessionDocument &left,
                                            const SessionDocument &right) {
  const auto *left_polygon = std::get_if<PolygonDocument>(&left);
  const auto *right_polygon = std::get_if<PolygonDocument>(&right);
  if (left_polygon == nullptr || right_polygon == nullptr) {
    return left_polygon == nullptr && right_polygon == nullptr;
  }
  return left_polygon->shares_selection_with(*right_polygon) ||
         same_polygon_selection(*left_polygon, *right_polygon);
}

[[nodiscard]] Result<OperationResult> translate_document(
    SessionDocument &document, Vec3 delta, bool select_all_faces = false) {
  if (auto *curve = std::get_if<CurveDocument>(&document)) {
    return curve->transform(Transform3{.origin = delta});
  }
  auto &polygon = std::get<PolygonDocument>(document);
  if (select_all_faces) {
    Result<OperationResult> selected = polygon.select_all_faces();
    if (!selected.ok()) {
      return selected;
    }
  }
  return polygon.translate_selection(delta);
}

[[nodiscard]] Result<OperationResult> clear_document_selection(
    SessionDocument &document) {
  if (auto *polygon = std::get_if<PolygonDocument>(&document)) {
    return polygon->clear_selection();
  }
  return Result<OperationResult>::success(OperationResult{});
}

[[nodiscard]] Result<SessionDocument> workspace_document(
    PolygonDocument polygon, std::optional<CurveDocument> curve) {
  if (!curve.has_value()) {
    return Result<SessionDocument>::success(std::move(polygon));
  }
  if (polygon.vertex_count() != 0U || polygon.face_count() != 0U) {
    return Result<SessionDocument>::failure(make_error(
        ErrorCode::InvalidArgument,
        "A workspace object cannot contain both polygon and curve documents."));
  }
  Result<OperationResult> valid = curve->validate();
  if (!valid.ok()) {
    return Result<SessionDocument>::failure(valid.error());
  }
  return Result<SessionDocument>::success(std::move(*curve));
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

[[nodiscard]] OperationResult selection_session_result(std::string message) {
  OperationResult result;
  result.changed = true;
  result.message = std::move(message);
  result.dirty.selection = true;
  result.dirty.overlays = true;
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
      appended_face->texture_transform = face.texture_transform;
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

[[nodiscard]] bool toggle_component_selection_history(
    SessionState &state, ComponentSelectionHistoryDelta &delta) {
  std::unordered_map<std::uint64_t, std::size_t> object_index;
  object_index.reserve(state.objects.size());
  for (std::size_t index = 0; index < state.objects.size(); ++index) {
    object_index.emplace(object_id_key(state.objects[index].id), index);
  }
  for (const ComponentSelectionHistoryObject &change : delta.objects) {
    if (!object_index.contains(object_id_key(change.id))) {
      return false;
    }
  }

  for (ComponentSelectionHistoryObject &change : delta.objects) {
    SessionObject &object =
        state.objects[object_index.at(object_id_key(change.id))];
    if (change.alternate_selection.has_value()) {
      PolygonDocumentNativeAccess::swap_selection_state(
          object.polygon_document(), *change.alternate_selection);
    }
    std::swap(object.selected, change.alternate_selected);
  }
  std::swap(state.active_object, delta.alternate_active_object);
  return true;
}

[[nodiscard]] bool rewind_history_entry(SessionState &state,
                                        const SessionHistoryEntry &entry) {
  if (entry.is_state_snapshot()) {
    state = entry.state_snapshot();
    return true;
  }
  ComponentSelectionHistoryDelta delta = entry.selection_delta();
  return toggle_component_selection_history(state, delta);
}

[[nodiscard]] Result<SemanticOverlayPayload>
build_semantic_overlay_payload(const PolygonDocument &document) {
  Result<AuthoredPolygonPayload> authored_result = document.authored_payload();
  if (!authored_result.ok()) {
    return Result<SemanticOverlayPayload>::failure(authored_result.error());
  }

  const AuthoredPolygonPayload &authored = authored_result.value();
  std::unordered_map<std::uint64_t, std::size_t> position_indices;
  position_indices.reserve(authored.vertices.size());
  for (std::size_t index = 0; index < authored.vertices.size(); ++index) {
    position_indices.emplace(vertex_id_key(authored.vertices[index]), index);
  }
  auto position_for = [&](VertexId vertex_id) -> const Vec3 * {
    const auto found = position_indices.find(vertex_id_key(vertex_id));
    if (found != position_indices.end()) {
      return &authored.positions[found->second];
    }
    return nullptr;
  };

  SemanticOverlayPayload overlay;
  overlay.selection_revision = document.selection_revision();
  overlay.records.reserve(authored.faces.size());
  for (const AuthoredPolygonFacePayload &face : authored.faces) {
    SemanticOverlayRecord record;
    record.kind = SemanticOverlayKind::Wire;
    record.points.reserve(face.vertices.size() + 1U);
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
  const auto *document =
      std::get_if<PolygonDocument>(&preview.preview_document);
  if (document == nullptr) {
    return Result<ToolPreviewPayload>::failure(wrong_document_kind_error());
  }
  if (!preview.payload_valid) {
    Result<ToolPreviewPayload> payload =
        build_preview_payload(*document);
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
                    const SessionDocument &base_document, Vec3 delta) {
  SessionDocument candidate = base_document;
  Result<OperationResult> operation =
      translate_document(candidate, delta);
  if (!operation.ok()) {
    return operation;
  }

  preview.preview_document = std::move(candidate);
  preview.delta = delta;
  preview.payload = {};
  preview.payload_valid = false;
  preview.dirty = operation.value().dirty;
  preview.dirty.overlays = true;
  operation.value().dirty.overlays = true;
  return operation;
}

} // namespace

/// Retains only a preview candidate and the origin allowed to restore it.
struct SessionPreviewCheckpointImpl {
  std::shared_ptr<const PreviewCheckpointOrigin> origin;
  PreviewTransaction preview;
};

/**
 * Stores mutable SDK session implementation details.
 */
struct ModelingSessionImpl {
  SessionState state;
  std::vector<ClipboardObject> clipboard;
  PreviewTransaction preview;
  PreviewCheckpointOwnership preview_checkpoint_ownership;
  std::vector<SessionHistoryEntry> undo_stack;
  std::vector<SessionHistoryEntry> redo_stack;
  std::vector<SessionObjectChangeRecord> object_changes;
  std::unordered_map<std::uint64_t, std::size_t> object_change_index;
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
           document_content_identity(object->document) ==
               preview.base_content_identity &&
           document_content_revision(object->document) ==
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

  void record_topology_identity(ObjectId object_id,
                                const CurveDocument &document) {
    CurveIdentityHighWater &high_water =
        topology_identity_high_water[object_id.index].curve;
    const CurveIdentityHighWater observed = document.identity_high_water();
    high_water.next_point_index =
        std::max(high_water.next_point_index, observed.next_point_index);
    high_water.next_segment_index =
        std::max(high_water.next_segment_index, observed.next_segment_index);
  }

  void record_topology_identity(ObjectId object_id,
                                const SessionDocument &document) {
    std::visit([&](const auto &value) {
      record_topology_identity(object_id, value);
    }, document);
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

  void reconcile_topology_identity(ObjectId object_id,
                                   CurveDocument &document) const {
    const auto high_water = topology_identity_high_water.find(object_id.index);
    if (high_water != topology_identity_high_water.end()) {
      document.preserve_identity_high_water(high_water->second.curve);
    }
  }

  void reconcile_topology_identity(ObjectId object_id,
                                   SessionDocument &document) const {
    std::visit([&](auto &value) {
      reconcile_topology_identity(object_id, value);
    }, document);
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

  [[nodiscard]] Result<OperationResult>
  validate_commit_identity(ObjectId object_id,
                           const CurveDocument &document) const {
    Result<OperationResult> valid = document.validate();
    if (!valid.ok()) {
      return valid;
    }
    const auto recorded = topology_identity_high_water.find(object_id.index);
    if (recorded == topology_identity_high_water.end()) {
      return valid;
    }
    const CurveIdentityHighWater high_water = recorded->second.curve;
    const CurveIdentityHighWater candidate = document.identity_high_water();
    const auto stale = [] {
      return Result<OperationResult>::failure(make_error(
          ErrorCode::StaleId,
          "Curve document predates the current component identity high-water "
          "mark or reuses a retired component identity."));
    };
    if (candidate.next_point_index < high_water.next_point_index ||
        candidate.next_segment_index < high_water.next_segment_index) {
      return stale();
    }

    // A stale copy can advance its cursor while still allocating an already
    // retired ID. Only live or currently previewed IDs below the high-water
    // mark may survive a normal edit; history restoration is handled separately.
    const auto live = find_object(state, object_id);
    if (live == state.objects.end() || live->kind() != SessionObjectKind::Curve) {
      return Result<OperationResult>::failure(wrong_document_kind_error());
    }
    const CurveDocument &live_document = live->curve_document();
    const CurveDocument *preview_document =
        preview.active && preview.object_id == object_id
            ? std::get_if<CurveDocument>(&preview.preview_document)
            : nullptr;
    const auto has_point = [](const CurveDocument &source, CurvePointId id) {
      return std::ranges::any_of(source.points(),
                                [id](const auto &point) { return point.id == id; });
    };
    const auto has_segment = [](const CurveDocument &source, CurveSegmentId id) {
      return std::ranges::find(source.segments(), id) != source.segments().end();
    };
    for (const CurveControlPoint &point : document.points()) {
      if (point.id.index < high_water.next_point_index &&
          !has_point(live_document, point.id) &&
          (preview_document == nullptr || !has_point(*preview_document, point.id))) {
        return stale();
      }
    }
    for (const CurveSegmentId segment : document.segments()) {
      if (segment.index < high_water.next_segment_index &&
          !has_segment(live_document, segment) &&
          (preview_document == nullptr || !has_segment(*preview_document, segment))) {
        return stale();
      }
    }
    return valid;
  }

  [[nodiscard]] Result<OperationResult>
  validate_commit_identity(ObjectId object_id,
                           const SessionDocument &document) const {
    return std::visit([&](const auto &value) {
      return validate_commit_identity(object_id, value);
    }, document);
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

  void publish_pending_changes(
      std::span<const PendingSessionObjectChange> pending) {
    if (pending.empty()) {
      return;
    }

    ++content_revision;
    object_change_index.reserve(object_changes.size() + pending.size());
    for (const PendingSessionObjectChange &change : pending) {
      const auto [record_entry, inserted] = object_change_index.emplace(
          object_id_key(change.id), object_changes.size());
      if (inserted) {
        object_changes.push_back({.id = change.id});
      }
      SessionObjectChangeRecord &record =
          object_changes[record_entry->second];

      record.name = change.name;
      record.kind = change.kind;
      record.revision = content_revision;
      record.deleted = change.deleted;
      if (change.dirty.topology) {
        record.topology_revision = content_revision;
      }
      if (change.dirty.geometry) {
        record.geometry_revision = content_revision;
      }
      if (change.dirty.selection) {
        record.selection_revision = content_revision;
        selection_revision = content_revision;
      }
      if (change.dirty.materials) {
        record.materials_revision = content_revision;
      }
      if (change.dirty.overlays) {
        record.overlays_revision = content_revision;
      }
    }
  }

  void publish_component_selection_transition(
      const ComponentSelectionHistoryDelta &delta) {
    std::vector<PendingSessionObjectChange> pending;
    pending.reserve(delta.objects.size() + 2U);
    for (const ComponentSelectionHistoryObject &change : delta.objects) {
      if (!change.document_selection_changed &&
          !change.object_selection_changed) {
        continue;
      }
      pending.push_back({
          .id = change.id,
          .name = change.name,
          .dirty = {.selection = true, .overlays = true},
          .kind = change.kind,
      });
    }

    const auto mark_active_change = [&](ObjectId object_id) {
      if (!object_id.valid()) {
        return;
      }
      auto pending_change = std::ranges::find_if(
          pending, [&](const PendingSessionObjectChange &candidate) {
            return candidate.id == object_id;
          });
      if (pending_change == pending.end()) {
        const auto object = find_object(state, object_id);
        if (object == state.objects.end()) {
          return;
        }
        pending.push_back({.id = object->id, .name = object->name,
                           .kind = object->kind()});
        pending_change = std::prev(pending.end());
      }
      pending_change->dirty.selection = true;
      pending_change->dirty.overlays = true;
    };
    if (delta.active_object_changed) {
      mark_active_change(delta.alternate_active_object);
      mark_active_change(state.active_object);
    }
    publish_pending_changes(pending);
  }

  void publish_state_transition(
      const SessionState &before,
      std::span<const SessionObjectDirtyOverride> dirty_overrides = {}) {
    std::vector<PendingSessionObjectChange> pending;
    pending.reserve(before.objects.size() + state.objects.size());

    std::unordered_map<std::uint64_t, const SessionObject *> before_by_id;
    before_by_id.reserve(before.objects.size());
    for (const SessionObject &object : before.objects) {
      before_by_id.emplace(object_id_key(object.id), &object);
    }
    std::unordered_map<std::uint64_t, const SessionObject *> current_by_id;
    current_by_id.reserve(state.objects.size());
    for (const SessionObject &object : state.objects) {
      current_by_id.emplace(object_id_key(object.id), &object);
    }
    std::unordered_map<std::uint64_t, DirtyFlags> dirty_by_id;
    dirty_by_id.reserve(dirty_overrides.size());
    for (const SessionObjectDirtyOverride &override : dirty_overrides) {
      DirtyFlags &dirty = dirty_by_id[object_id_key(override.id)];
      dirty.topology = dirty.topology || override.dirty.topology;
      dirty.geometry = dirty.geometry || override.dirty.geometry;
      dirty.selection = dirty.selection || override.dirty.selection;
      dirty.materials = dirty.materials || override.dirty.materials;
      dirty.overlays = dirty.overlays || override.dirty.overlays;
    }

    for (const SessionObject &previous : before.objects) {
      const auto current_entry = current_by_id.find(object_id_key(previous.id));
      if (current_entry == current_by_id.end()) {
        pending.push_back({
            .id = previous.id,
            .name = previous.name,
            .dirty = {.topology = true,
                      .geometry = true,
                      .selection = true,
                      .materials = true,
                      .overlays = true},
            .deleted = true,
            .kind = previous.kind(),
        });
        continue;
      }
      const SessionObject &current = *current_entry->second;
      const auto dirty_override = dirty_by_id.find(object_id_key(current.id));

      const bool content_changed =
          document_content_identity(previous.document) !=
              document_content_identity(current.document) ||
          document_content_revision(previous.document) !=
              document_content_revision(current.document);
      const bool document_selection_revision_changed =
          document_selection_revision(previous.document) !=
          document_selection_revision(current.document);
      const bool exact_selection_dirty =
          dirty_override != dirty_by_id.end() &&
          dirty_override->second.selection;
      const bool component_selection_changed =
          exact_selection_dirty ||
          !same_document_selection(previous.document, current.document);
      const bool effective_selection_changed =
          previous.selected != current.selected ||
          component_selection_changed;
      const bool selection_changed =
          effective_selection_changed || document_selection_revision_changed;
      if (!content_changed && !selection_changed &&
          previous.name == current.name && dirty_override == dirty_by_id.end()) {
        continue;
      }

      DirtyFlags dirty;
      if (dirty_override != dirty_by_id.end()) {
        dirty = dirty_override->second;
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
          (dirty_override == dirty_by_id.end() ||
           effective_selection_changed)) {
        dirty.selection = true;
        dirty.overlays = true;
      }

      pending.push_back({
          .id = current.id,
          .name = current.name,
          .dirty = dirty,
          .kind = current.kind(),
      });
    }

    for (const SessionObject &current : state.objects) {
      if (before_by_id.contains(object_id_key(current.id))) {
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
          .kind = current.kind(),
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
        const auto current = current_by_id.find(object_id_key(object_id));
        if (current != current_by_id.end()) {
          pending.push_back(
              {.id = current->second->id, .name = current->second->name,
               .kind = current->second->kind()});
        } else {
          const auto previous = before_by_id.find(object_id_key(object_id));
          if (previous == before_by_id.end()) {
            return;
          }
          pending.push_back({
              .id = previous->second->id,
              .name = previous->second->name,
              .deleted = true,
              .kind = previous->second->kind(),
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

    publish_pending_changes(pending);
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

ModelingSession ModelingSession::current_workspace_checkpoint() const {
  ModelingSession checkpoint;
  // Assign durable fields directly: copying ModelingSessionImpl and clearing
  // its stacks afterward would already have paid the accumulated-history cost.
  checkpoint.impl_->state = impl_->state;
  checkpoint.impl_->clipboard = impl_->clipboard;
  checkpoint.impl_->object_changes = impl_->object_changes;
  checkpoint.impl_->object_change_index = impl_->object_change_index;
  checkpoint.impl_->content_revision = impl_->content_revision;
  checkpoint.impl_->selection_revision = impl_->selection_revision;
  checkpoint.impl_->preview_payload_revision =
      impl_->preview_payload_revision;
  checkpoint.impl_->next_preview_index = impl_->next_preview_index;
  checkpoint.impl_->topology_identity_high_water =
      impl_->topology_identity_high_water;
  checkpoint.impl_->next_object_index = impl_->next_object_index;
  return checkpoint;
}

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

Result<ObjectId> ModelingSession::add_curve_document(CurveDocument document,
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
  Result<OperationResult> valid = document.validate();
  if (!valid.ok()) {
    return Result<ObjectId>::failure(valid.error());
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
  return allocated_id;
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

  std::unordered_set<std::uint64_t> requested;
  requested.reserve(objects.size());
  for (const ObjectId object_id : objects) {
    if (!object_id.valid() || object_id.generation != 1U) {
      return Result<OperationResult>::failure(
          make_error(ErrorCode::InvalidId, "Object ID is not live."));
    }
    requested.emplace(object_id_key(object_id));
  }
  std::size_t live_requested_count = 0U;
  for (const SessionObject &object : impl_->state.objects) {
    live_requested_count += requested.contains(object_id_key(object.id)) ? 1U
                                                                         : 0U;
  }
  if (live_requested_count != requested.size()) {
    return Result<OperationResult>::failure(
        make_error(ErrorCode::InvalidId, "Object ID is not live."));
  }

  bool changed = false;
  for (const SessionObject &object : impl_->state.objects) {
    const bool matched = requested.contains(object_id_key(object.id));
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
  }

  ObjectId next_active = impl_->state.active_object;
  if (!objects.empty() && edit != SelectionEdit::Remove) {
    next_active = objects.front();
  } else if (edit == SelectionEdit::Replace && objects.empty()) {
    next_active = {};
  }
  changed = changed || next_active != impl_->state.active_object;

  if (changed) {
    SessionState before = impl_->state;
    for (SessionObject &object : impl_->state.objects) {
      const bool matched = requested.contains(object_id_key(object.id));
      switch (edit) {
      case SelectionEdit::Replace:
        object.selected = matched;
        break;
      case SelectionEdit::Add:
        object.selected = object.selected || matched;
        break;
      case SelectionEdit::Remove:
        object.selected = object.selected && !matched;
        break;
      case SelectionEdit::Toggle:
        object.selected = matched ? !object.selected : object.selected;
        break;
      }
    }
    impl_->state.active_object = next_active;
    impl_->undo_stack.push_back(std::move(before));
    impl_->redo_stack.clear();
    impl_->publish_state_transition(impl_->undo_stack.back());
  }
  return Result<OperationResult>::success(
      session_result(changed, "Object selection changed."));
}

Result<OperationResult> ModelingSession::commit_component_selection_batch(
    std::span<const SessionComponentSelectionUpdate> updates,
    std::span<const ObjectId> selected_objects, ObjectId active_object) {
  QDR_PROFILE_FUNCTION();
  if (impl_->batch_transaction_active) {
    impl_->batch_transaction_violated = true;
    return Result<OperationResult>::failure(make_error(
        ErrorCode::InvalidArgument,
        "A component selection batch cannot run inside another SDK batch."));
  }
  if (impl_->preview.active) {
    return active_preview_error();
  }

  constexpr std::size_t kMissingObjectIndex =
      (std::numeric_limits<std::size_t>::max)();
  std::vector<std::size_t> dense_object_index;
  std::unordered_map<std::uint64_t, std::size_t> sparse_object_index;
  {
    QDR_PROFILE_SCOPE("SDK component selection index objects");
    const std::size_t dense_span = impl_->next_object_index;
    constexpr std::size_t kMaximumDenseObjectIndexSpan = 1U << 20U;
    const std::size_t dense_budget =
        impl_->state.objects.size() * 4U + 1024U;
    const bool use_dense_index =
        dense_span <= kMaximumDenseObjectIndexSpan &&
        dense_span <= dense_budget;
    if (use_dense_index) {
      dense_object_index.assign(dense_span, kMissingObjectIndex);
      for (std::size_t index = 0; index < impl_->state.objects.size();
           ++index) {
        dense_object_index[impl_->state.objects[index].id.index] = index;
      }
    } else {
      sparse_object_index.reserve(impl_->state.objects.size());
      for (std::size_t index = 0; index < impl_->state.objects.size();
           ++index) {
        const SessionObject &object = impl_->state.objects[index];
        sparse_object_index.emplace(object_id_key(object.id), index);
      }
    }
  }
  const auto object_index_for = [&](ObjectId object)
      -> std::optional<std::size_t> {
    if (!object.valid()) {
      return std::nullopt;
    }
    if (!dense_object_index.empty()) {
      if (object.index >= dense_object_index.size()) {
        return std::nullopt;
      }
      const std::size_t index = dense_object_index[object.index];
      return index != kMissingObjectIndex &&
                     impl_->state.objects[index].id == object
                 ? std::optional{index}
                 : std::nullopt;
    }
    const auto found = sparse_object_index.find(object_id_key(object));
    return found != sparse_object_index.end()
               ? std::optional{found->second}
               : std::nullopt;
  };

  std::vector<bool> updated_by_index(impl_->state.objects.size(), false);
  std::vector<bool> selected_by_index(impl_->state.objects.size(), false);
  {
    QDR_PROFILE_SCOPE("SDK component selection validate batch structure");
    for (const SessionComponentSelectionUpdate &update : updates) {
      const std::optional<std::size_t> object_entry =
          object_index_for(update.object);
      if (!object_entry.has_value()) {
        return Result<OperationResult>::failure(
            make_error(ErrorCode::InvalidId, "Object ID is not live."));
      }
      if (updated_by_index[*object_entry]) {
        return Result<OperationResult>::failure(make_error(
            ErrorCode::InvalidArgument,
            "A component selection batch contains a duplicate object."));
      }
      if (impl_->state.objects[*object_entry].kind() != SessionObjectKind::Mesh) {
        return Result<OperationResult>::failure(wrong_document_kind_error());
      }
      updated_by_index[*object_entry] = true;
      if (!selection_mode_for_kind(update.selection.kind).has_value()) {
        return Result<OperationResult>::failure(make_error(
            ErrorCode::InvalidArgument,
            "A component selection batch requires component selections."));
      }
    }

    for (const ObjectId object : selected_objects) {
      const std::optional<std::size_t> object_entry =
          object_index_for(object);
      if (!object_entry.has_value()) {
        return Result<OperationResult>::failure(
            make_error(ErrorCode::InvalidId, "Object ID is not live."));
      }
      if (selected_by_index[*object_entry]) {
        return Result<OperationResult>::failure(make_error(
            ErrorCode::InvalidArgument,
            "A component selection batch contains duplicate selected objects."));
      }
      selected_by_index[*object_entry] = true;
    }
    if (active_object != ObjectId{} && !active_object.valid()) {
      return Result<OperationResult>::failure(
          make_error(ErrorCode::InvalidId, "Active object ID is invalid."));
    }
    const std::optional<std::size_t> active_object_index =
        object_index_for(active_object);
    if (active_object.valid() && !active_object_index.has_value()) {
      return Result<OperationResult>::failure(
          make_error(ErrorCode::InvalidId, "Active object ID is not live."));
    }
    if (active_object.valid() &&
        !selected_by_index[*active_object_index]) {
      return Result<OperationResult>::failure(make_error(
          ErrorCode::InvalidArgument,
          "The active component-selection object must also be selected."));
    }
    if (!active_object.valid() && !selected_objects.empty()) {
      return Result<OperationResult>::failure(make_error(
          ErrorCode::InvalidArgument,
          "A non-empty component selection batch requires an active object."));
    }
  }

  /// Pairs a session object slot with its validated immutable selection
  /// replacement before atomic publication.
  struct PreparedComponentSelection {
    std::size_t object_index = 0U;
    PolygonDocumentSelectionReplacement replacement;
  };
  /// Pins one already validated selection payload for reuse by documents with
  /// identical topology and requested component IDs.
  struct ReusablePreparedSelection {
    const PolygonSelectionSnapshot *snapshot = nullptr;
    const PolygonDocument *validated_document = nullptr;
    std::shared_ptr<const quader_poly::Selection> selection;
  };
  std::vector<PreparedComponentSelection> prepared_selections;
  prepared_selections.reserve(updates.size());
  const bool reuse_identical_selection_payloads = updates.size() >= 16U;
  std::vector<ReusablePreparedSelection> reusable_selections;
  std::unordered_multimap<std::uint64_t, std::size_t>
      reusable_selection_by_signature;
  if (reuse_identical_selection_payloads) {
    reusable_selections.reserve(updates.size());
    reusable_selection_by_signature.reserve(updates.size());
  }
  std::string message;
  {
    QDR_PROFILE_SCOPE("SDK component selection prepare replacements");
    for (const SessionComponentSelectionUpdate &update : updates) {
      const std::size_t index = *object_index_for(update.object);
      const SessionObject &object = impl_->state.objects[index];
      std::uint64_t selection_signature = 0U;
      const ReusablePreparedSelection *reusable = nullptr;
      if (reuse_identical_selection_payloads) {
        selection_signature = polygon_selection_signature(update.selection);
        const auto [first, last] =
            reusable_selection_by_signature.equal_range(selection_signature);
        for (auto candidate = first; candidate != last; ++candidate) {
          const ReusablePreparedSelection &entry =
              reusable_selections[candidate->second];
          if (same_polygon_selection(*entry.snapshot, update.selection)) {
            reusable = &entry;
            break;
          }
        }
      }
      Result<PolygonDocumentSelectionReplacement> replacement =
          reusable != nullptr
              ? PolygonDocumentNativeAccess::
                    prepare_selection_replacement_reusing(
                        object.polygon_document(), *reusable->validated_document,
                        update.selection,
                        reusable->selection)
              : PolygonDocumentNativeAccess::prepare_selection_replacement(
                    object.polygon_document(), update.selection);
      if (!replacement.ok()) {
        return Result<OperationResult>::failure(
            std::move(replacement).error());
      }
      PolygonDocumentSelectionReplacement prepared =
          std::move(replacement).value();
      if (reuse_identical_selection_payloads && reusable == nullptr &&
          prepared.selection != nullptr) {
        const std::size_t reusable_index = reusable_selections.size();
        reusable_selections.push_back({
            .snapshot = &update.selection,
            .validated_document = &object.polygon_document(),
            .selection = prepared.selection,
        });
        reusable_selection_by_signature.emplace(selection_signature,
                                                reusable_index);
      }
      if (!prepared.changed) {
        continue;
      }
      if (!prepared.message.empty()) {
        message = prepared.message;
      }
      prepared_selections.push_back({
          .object_index = index,
          .replacement = std::move(prepared),
      });
    }
  }

  constexpr std::size_t kMissingHistoryIndex =
      (std::numeric_limits<std::size_t>::max)();
  std::vector<std::size_t> history_index_by_object(
      impl_->state.objects.size(), kMissingHistoryIndex);
  ComponentSelectionHistoryDelta history;
  {
    QDR_PROFILE_SCOPE("SDK component selection build sparse history");
    std::size_t object_selection_change_count = 0U;
    for (std::size_t index = 0; index < impl_->state.objects.size(); ++index) {
      object_selection_change_count +=
          impl_->state.objects[index].selected != selected_by_index[index]
              ? 1U
              : 0U;
    }
    history.objects.reserve(std::min(
        impl_->state.objects.size(),
        prepared_selections.size() + object_selection_change_count));
    const auto ensure_history_object = [&](std::size_t index)
        -> ComponentSelectionHistoryObject & {
      std::size_t &history_index = history_index_by_object[index];
      if (history_index == kMissingHistoryIndex) {
        const SessionObject &object = impl_->state.objects[index];
        history_index = history.objects.size();
        history.objects.push_back({
            .id = object.id,
            .name = object.name,
            .alternate_selected = object.selected,
            .kind = object.kind(),
        });
      }
      return history.objects[history_index];
    };
    for (const PreparedComponentSelection &prepared : prepared_selections) {
      ComponentSelectionHistoryObject &history_object =
          ensure_history_object(prepared.object_index);
      history_object.alternate_selection.emplace(
          PolygonDocumentNativeAccess::capture_selection_state(
              impl_->state.objects[prepared.object_index].polygon_document()));
      history_object.document_selection_changed = true;
    }
    for (std::size_t index = 0; index < impl_->state.objects.size(); ++index) {
      const SessionObject &object = impl_->state.objects[index];
      const bool selected = selected_by_index[index];
      if (object.selected != selected) {
        ensure_history_object(index).object_selection_changed = true;
      }
    }
    history.alternate_active_object = impl_->state.active_object;
    history.active_object_changed =
        impl_->state.active_object != active_object;
  }
  const bool changed =
      !history.objects.empty() || history.active_object_changed;

  OperationResult result;
  result.changed = changed;
  result.message = message.empty()
                       ? (changed ? "Component selection changed."
                                  : "Component selection unchanged.")
                       : std::move(message);
  result.dirty.selection = changed;
  result.dirty.overlays = changed;
  if (!changed) {
    return Result<OperationResult>::success(std::move(result));
  }

  {
    QDR_PROFILE_SCOPE("SDK component selection commit sparse history");
    // Commit the fully allocated history entry before live mutation. Every
    // operation after this point is a no-fail pointer move, swap, or scalar
    // assignment, preserving all-or-nothing publication semantics.
    impl_->undo_stack.reserve(impl_->undo_stack.size() + 1U);
    impl_->undo_stack.emplace_back(std::move(history));
  }
  {
    QDR_PROFILE_SCOPE("SDK component selection mutate payloads");
    for (PreparedComponentSelection &prepared : prepared_selections) {
      PolygonDocumentNativeAccess::apply_selection_replacement(
          impl_->state.objects[prepared.object_index].polygon_document(),
          std::move(prepared.replacement));
    }
    for (std::size_t index = 0; index < impl_->state.objects.size(); ++index) {
      impl_->state.objects[index].selected = selected_by_index[index];
    }
    impl_->state.active_object = active_object;
    impl_->redo_stack.clear();
  }
  {
    QDR_PROFILE_SCOPE("SDK component selection publish transition");
    impl_->publish_component_selection_transition(
        impl_->undo_stack.back().selection_delta());
  }
  return Result<OperationResult>::success(std::move(result));
}

Result<OperationResult> ModelingSession::commit_documents_batch(
    std::vector<SessionDocumentUpdate> updates,
    bool preserve_object_selection) {
  if (impl_->batch_transaction_active) {
    impl_->batch_transaction_violated = true;
    return Result<OperationResult>::failure(make_error(
        ErrorCode::InvalidArgument,
        "A document batch cannot run inside another SDK batch."));
  }
  if (impl_->preview.active) {
    return active_preview_error();
  }

  std::unordered_map<std::uint64_t, std::size_t> object_index;
  object_index.reserve(impl_->state.objects.size());
  for (std::size_t index = 0; index < impl_->state.objects.size(); ++index) {
    const SessionObject &object = impl_->state.objects[index];
    object_index.emplace(object_id_key(object.id), index);
  }

  std::unordered_set<std::uint64_t> updated_ids;
  updated_ids.reserve(updates.size());
  for (const SessionDocumentUpdate &update : updates) {
    const std::uint64_t key = object_id_key(update.object);
    if (!update.object.valid() || !object_index.contains(key)) {
      return Result<OperationResult>::failure(
          make_error(ErrorCode::InvalidId, "Object ID is not live."));
    }
    if (!updated_ids.emplace(key).second) {
      return Result<OperationResult>::failure(make_error(
          ErrorCode::InvalidArgument,
          "A document batch contains a duplicate object."));
    }
    if (impl_->state.objects[object_index.at(key)].kind() != SessionObjectKind::Mesh) {
      return Result<OperationResult>::failure(wrong_document_kind_error());
    }
    if (!update.receipt.success) {
      return Result<OperationResult>::failure(make_error(
          update.receipt.error_code == ErrorCode::Ok
              ? ErrorCode::ValidationFailed
              : update.receipt.error_code,
          update.receipt.message.empty() ? "Document update failed."
                                         : update.receipt.message));
    }
    if (update.receipt.changed) {
      Result<OperationResult> identity =
          impl_->validate_commit_identity(update.object, update.document);
      if (!identity.ok()) {
        return identity;
      }
    }
  }

  OperationResult result;
  result.message = "Documents unchanged.";
  SessionState candidate = impl_->state;
  std::vector<SessionObjectDirtyOverride> dirty_overrides;
  dirty_overrides.reserve(updates.size());
  std::vector<ObjectId> changed_objects;
  changed_objects.reserve(updates.size());

  const auto append_delta = [](ElementDelta &destination,
                               ElementDelta &source) {
    destination.vertices.insert(destination.vertices.end(),
                                std::make_move_iterator(source.vertices.begin()),
                                std::make_move_iterator(source.vertices.end()));
    destination.edges.insert(destination.edges.end(),
                             std::make_move_iterator(source.edges.begin()),
                             std::make_move_iterator(source.edges.end()));
    destination.faces.insert(destination.faces.end(),
                             std::make_move_iterator(source.faces.begin()),
                             std::make_move_iterator(source.faces.end()));
  };
  const auto merge_dirty = [](DirtyFlags &destination,
                              const DirtyFlags &source) {
    destination.topology = destination.topology || source.topology;
    destination.geometry = destination.geometry || source.geometry;
    destination.selection = destination.selection || source.selection;
    destination.materials = destination.materials || source.materials;
    destination.overlays = destination.overlays || source.overlays;
  };

  for (SessionDocumentUpdate &update : updates) {
    OperationResult &receipt = update.receipt;
    if (!receipt.message.empty()) {
      result.message = receipt.message;
    }
    merge_dirty(result.dirty, receipt.dirty);
    result.diagnostics.insert(
        result.diagnostics.end(),
        std::make_move_iterator(receipt.diagnostics.begin()),
        std::make_move_iterator(receipt.diagnostics.end()));
    append_delta(result.created, receipt.created);
    append_delta(result.deleted, receipt.deleted);
    append_delta(result.affected, receipt.affected);
    append_delta(result.modified, receipt.modified);
    append_delta(result.selection_remap.source,
                 receipt.selection_remap.source);
    append_delta(result.selection_remap.destination,
                 receipt.selection_remap.destination);
    if (!receipt.changed) {
      continue;
    }

    SessionObject &object =
        candidate.objects[object_index.at(object_id_key(update.object))];
    object.document = std::move(update.document);
    if (!preserve_object_selection) {
      object.selected = true;
      candidate.active_object = object.id;
    }
    dirty_overrides.push_back({.id = update.object, .dirty = receipt.dirty});
    changed_objects.push_back(update.object);
    result.changed = true;
  }

  if (!result.changed) {
    return Result<OperationResult>::success(std::move(result));
  }

  impl_->undo_stack.push_back(std::move(impl_->state));
  impl_->redo_stack.clear();
  impl_->state = std::move(candidate);
  for (const ObjectId object : changed_objects) {
    const SessionObject &committed =
        impl_->state.objects[object_index.at(object_id_key(object))];
    impl_->record_topology_identity(object, committed.document);
  }
  impl_->publish_state_transition(impl_->undo_stack.back(), dirty_overrides);
  return Result<OperationResult>::success(std::move(result));
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

  SessionDocument document = object->document;
  if (offset.x != 0.0F || offset.y != 0.0F || offset.z != 0.0F) {
    Result<OperationResult> translated =
        translate_document(document, offset, true);
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

Result<std::vector<ObjectId>> ModelingSession::duplicate_objects(
    std::span<const ObjectId> object_ids, Vec3 offset, std::string name) {
  QDR_PROFILE_FUNCTION();
  if (impl_->batch_transaction_active) {
    impl_->batch_transaction_violated = true;
    return Result<std::vector<ObjectId>>::failure(make_error(
        ErrorCode::InvalidArgument,
        "Batch callback cannot duplicate objects outside scheduled steps."));
  }
  if (impl_->preview.active) {
    return Result<std::vector<ObjectId>>::failure(make_error(
        ErrorCode::InvalidArgument,
        "Commit or cancel the active preview before duplicating objects."));
  }
  if (object_ids.empty()) {
    return Result<std::vector<ObjectId>>::failure(make_error(
        ErrorCode::InvalidArgument, "No objects were provided to duplicate."));
  }

  /// Stages one complete SDK object duplicate before the atomic session state
  /// transition publishes any of them.
  struct PreparedDuplicate {
    ObjectId id{};
    std::string name;
    SessionDocument document;
  };
  std::vector<PreparedDuplicate> prepared;
  prepared.reserve(object_ids.size());
  {
    QDR_PROFILE_SCOPE("SDK prepare atomic object duplicates");
    std::unordered_map<std::uint64_t, const SessionObject *> object_by_id;
    object_by_id.reserve(impl_->state.objects.size());
    for (const SessionObject &object : impl_->state.objects) {
      object_by_id.emplace(object_id_key(object.id), &object);
    }
    for (const ObjectId object_id : object_ids) {
      const auto object = object_by_id.find(object_id_key(object_id));
      if (!object_id.valid() || object == object_by_id.end()) {
        return Result<std::vector<ObjectId>>::failure(
            make_error(ErrorCode::InvalidId, "Object ID is not live."));
      }

      SessionDocument document = object->second->document;
      if (offset.x != 0.0F || offset.y != 0.0F || offset.z != 0.0F) {
        Result<OperationResult> translated =
            translate_document(document, offset, true);
        if (!translated.ok()) {
          return Result<std::vector<ObjectId>>::failure(translated.error());
        }
      }

      Result<ObjectId> allocated_id = impl_->allocate_object_id();
      if (!allocated_id.ok()) {
        return Result<std::vector<ObjectId>>::failure(allocated_id.error());
      }
      prepared.push_back({
          .id = allocated_id.value(),
          .name = name.empty() ? object->second->name : name,
          .document = std::move(document),
      });
    }
  }

  std::vector<ObjectId> duplicated;
  duplicated.reserve(prepared.size());
  {
    QDR_PROFILE_SCOPE("SDK publish atomic object duplicates");
    SessionState before = impl_->state;
    impl_->state.objects.reserve(impl_->state.objects.size() + prepared.size());
    for (SessionObject &object : impl_->state.objects) {
      object.selected = false;
    }
    for (PreparedDuplicate &duplicate : prepared) {
      duplicated.push_back(duplicate.id);
      impl_->state.objects.push_back({
          .id = duplicate.id,
          .name = std::move(duplicate.name),
          .document = std::move(duplicate.document),
          .selected = true,
      });
      impl_->record_topology_identity(
          duplicate.id, impl_->state.objects.back().document);
    }
    impl_->state.active_object = duplicated.front();
    impl_->undo_stack.push_back(std::move(before));
    impl_->redo_stack.clear();
    impl_->publish_state_transition(impl_->undo_stack.back());
  }
  return Result<std::vector<ObjectId>>::success(std::move(duplicated));
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
    SessionDocument document = object.document;
    Result<OperationResult> cleared = clear_document_selection(document);
    if (!cleared.ok()) {
      return cleared;
    }
    copied.push_back({.name = object.name, .document = std::move(document)});
  }
  if (copied.empty()) {
    return Result<OperationResult>::failure(make_error(
        ErrorCode::InvalidArgument,
        "Select one or more objects before copying."));
  }

  impl_->clipboard = std::move(copied);
  OperationResult result;
  result.message = "Copied selected objects.";
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
        "Copy one or more objects before pasting."));
  }

  const ModelingSession before = *this;
  const std::size_t undo_mark = undo_depth();
  std::vector<ObjectId> pasted;
  pasted.reserve(impl_->clipboard.size());
  for (const ClipboardObject &object : impl_->clipboard) {
    SessionDocument document = object.document;
    if (offset.x != 0.0F || offset.y != 0.0F || offset.z != 0.0F) {
      Result<OperationResult> translated =
          translate_document(document, offset, true);
      if (!translated.ok()) {
        restore_snapshot_preserving_identity_high_water(before);
        return Result<ClipboardPasteResult>::failure(translated.error());
      }
      Result<OperationResult> cleared = clear_document_selection(document);
      if (!cleared.ok()) {
        restore_snapshot_preserving_identity_high_water(before);
        return Result<ClipboardPasteResult>::failure(cleared.error());
      }
    }
    Result<ObjectId> added =
        std::holds_alternative<CurveDocument>(document)
            ? add_curve_document(std::get<CurveDocument>(std::move(document)),
                                 object.name)
            : add_document(std::get<PolygonDocument>(std::move(document)),
                           object.name);
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
  operation.message = "Pasted copied objects.";
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

Result<std::vector<ObjectId>>
ModelingSession::replace_workspace_preserving_clipboard(
    std::vector<SessionWorkspaceDocument> documents) {
  auto replacement = std::make_unique<ModelingSessionImpl>();
  replacement->state.objects.reserve(documents.size());

  std::vector<ObjectId> object_ids;
  object_ids.reserve(documents.size());
  for (SessionWorkspaceDocument &document : documents) {
    Result<SessionDocument> authored = workspace_document(
        std::move(document.document), std::move(document.curve_document));
    if (!authored.ok()) {
      return Result<std::vector<ObjectId>>::failure(authored.error());
    }
    Result<ObjectId> allocated = replacement->allocate_object_id();
    if (!allocated.ok()) {
      return Result<std::vector<ObjectId>>::failure(allocated.error());
    }
    const ObjectId object_id = allocated.value();
    replacement->state.objects.push_back({
        .id = object_id,
        .name = std::move(document.name),
        .document = std::move(authored).value(),
        .selected = document.selected,
    });
    replacement->record_topology_identity(
        object_id, replacement->state.objects.back().document);
    if (document.selected && !replacement->state.active_object.valid()) {
      replacement->state.active_object = object_id;
    }
    object_ids.push_back(object_id);
  }

  const SessionState empty;
  replacement->publish_state_transition(empty);
  replacement->clipboard = std::move(impl_->clipboard);
  impl_ = std::move(replacement);
  return Result<std::vector<ObjectId>>::success(std::move(object_ids));
}

Result<OperationResult>
ModelingSession::restore_documents_as_history_transition(
    std::vector<SessionDocumentHistoryRestore> documents) {
  if (impl_->batch_transaction_active) {
    impl_->batch_transaction_violated = true;
    return Result<OperationResult>::failure(make_error(
        ErrorCode::InvalidArgument,
        "A document history restore cannot run inside an SDK batch."));
  }
  if (impl_->preview.active) {
    return active_preview_error();
  }
  if (documents.empty()) {
    return Result<OperationResult>::success(
        session_result(false, "Documents unchanged."));
  }

  std::unordered_map<std::uint64_t, std::size_t> object_index;
  object_index.reserve(impl_->state.objects.size());
  for (std::size_t index = 0; index < impl_->state.objects.size(); ++index) {
    object_index.emplace(object_id_key(impl_->state.objects[index].id), index);
  }

  std::unordered_set<std::uint64_t> restored_ids;
  restored_ids.reserve(documents.size());
  std::vector<SessionDocument> prepared_documents;
  prepared_documents.reserve(documents.size());
  for (SessionDocumentHistoryRestore &restore : documents) {
    const std::uint64_t key = object_id_key(restore.object);
    if (!restore.object.valid() || !object_index.contains(key)) {
      return Result<OperationResult>::failure(
          make_error(ErrorCode::InvalidId, "Object ID is not live."));
    }
    if (!restored_ids.emplace(key).second) {
      return Result<OperationResult>::failure(make_error(
          ErrorCode::InvalidArgument,
          "A document history restore contains a duplicate object."));
    }
    Result<SessionDocument> authored = workspace_document(
        std::move(restore.document), std::move(restore.curve_document));
    if (!authored.ok()) {
      return Result<OperationResult>::failure(authored.error());
    }
    SessionDocument prepared = std::move(authored).value();
    if (document_kind(prepared) != impl_->state.objects[object_index.at(key)].kind()) {
      return Result<OperationResult>::failure(wrong_document_kind_error());
    }
    // Host history can legitimately restore retired IDs. Retain its exact
    // component data while merging the live allocator cursor before publication.
    Result<OperationResult> identity;
    if (auto *curve = std::get_if<CurveDocument>(&prepared)) {
      impl_->reconcile_topology_identity(restore.object, *curve);
      identity = curve->validate();
    } else {
      identity = impl_->validate_commit_identity(restore.object, prepared);
    }
    if (!identity.ok()) {
      return identity;
    }
    prepared_documents.push_back(std::move(prepared));
  }

  const auto merge_dirty = [](DirtyFlags &destination,
                              const DirtyFlags &source) {
    destination.topology = destination.topology || source.topology;
    destination.geometry = destination.geometry || source.geometry;
    destination.selection = destination.selection || source.selection;
    destination.materials = destination.materials || source.materials;
    destination.overlays = destination.overlays || source.overlays;
  };

  OperationResult result =
      session_result(true, "Documents restored from host history.");
  std::vector<PendingSessionObjectChange> pending;
  pending.reserve(documents.size());
  for (std::size_t index = 0; index < documents.size(); ++index) {
    const SessionDocumentHistoryRestore &restore = documents[index];
    SessionObject &object = impl_->state.objects[
        object_index.at(object_id_key(restore.object))];
    object.document = std::move(prepared_documents[index]);
    impl_->record_topology_identity(restore.object, object.document);
    merge_dirty(result.dirty, restore.dirty);
    pending.push_back({
        .id = object.id,
        .name = object.name,
        .dirty = restore.dirty,
        .kind = object.kind(),
    });
  }

  impl_->undo_stack.clear();
  impl_->redo_stack.clear();
  impl_->publish_pending_changes(pending);
  return Result<OperationResult>::success(std::move(result));
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
  if (survivor->kind() != SessionObjectKind::Mesh) {
    return Result<OperationResult>::failure(wrong_document_kind_error());
  }

  OperationResult result = session_result(true, "Meshes combined.");
  result.dirty.topology = true;
  result.dirty.geometry = true;
  result.dirty.selection = true;

  PolygonDocument survivor_working = survivor->polygon_document();
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
    if (source->kind() != SessionObjectKind::Mesh) {
      return Result<OperationResult>::failure(wrong_document_kind_error());
    }
    Result<AppendedDocument> appended = append_document_with_remapped_ids(
        survivor_document,
        PolygonDocumentNativeAccess::document(source->polygon_document()));
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
      survivor->polygon_document(), std::move(survivor_document));
  quader_poly::Selection survivor_selection =
      PolygonDocumentNativeAccess::selection(survivor->polygon_document());
  survivor_selection.clear();
  PolygonDocumentNativeAccess::replace_selection(
      survivor->polygon_document(), std::move(survivor_selection));
  PolygonDocumentImpl &survivor_impl =
      PolygonDocumentNativeAccess::impl(survivor->polygon_document());
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
  if (survivor->kind() != SessionObjectKind::Mesh ||
      source->kind() != SessionObjectKind::Mesh) {
    return Result<OperationResult>::failure(wrong_document_kind_error());
  }

  const quader_poly::Selection survivor_selection =
      PolygonDocumentNativeAccess::selection(survivor->polygon_document());
  const quader_poly::Selection source_selection =
      PolygonDocumentNativeAccess::selection(source->polygon_document());
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

  PolygonDocument survivor_working = survivor->polygon_document();
  impl_->reconcile_topology_identity(survivor_id, survivor_working);
  quader_poly::Document survivor_document =
      PolygonDocumentNativeAccess::document(survivor_working);
  quader_poly::Document source_document =
      PolygonDocumentNativeAccess::document(source->polygon_document());
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
      survivor->polygon_document(), std::move(survivor_document));
  PolygonDocumentNativeAccess::replace_selection(
      survivor->polygon_document(), std::move(bridge_selection));
  PolygonDocumentImpl &survivor_impl =
      PolygonDocumentNativeAccess::impl(survivor->polygon_document());
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

  if (object->kind() != SessionObjectKind::Mesh) {
    return Result<OperationResult>::failure(wrong_document_kind_error());
  }
  Result<OperationResult> result = object->polygon_document().select_face(face_id);
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

  Result<OperationResult> result = translate_document(found->document, delta);
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
  preview.base_content_identity = document_content_identity(object->document);
  preview.base_content_revision = document_content_revision(object->document);
  preview.base_session_revision = impl_->content_revision;
  Result<OperationResult> result =
      apply_preview_delta(preview, object->document, delta);
  if (!result.ok()) {
    return result;
  }

  preview.payload_revision = impl_->advance_preview_payload_revision(
      document_content_revision(preview.preview_document));
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
        document_content_revision(impl_->preview.preview_document));
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
  if (object->kind() != SessionObjectKind::Mesh) {
    return Result<PreviewId>::failure(wrong_document_kind_error());
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
  preview.base_content_identity = document_content_identity(object->document);
  preview.base_content_revision = document_content_revision(object->document);
  preview.base_session_revision = impl_->content_revision;
  preview.preview_document = std::move(preview_document);
  preview.receipt = std::move(receipt);
  preview.receipt.dirty.overlays = true;
  preview.dirty = preview.receipt.dirty;
  preview.payload = std::move(payload).value();
  preview.payload_valid = true;
  preview.payload_revision = impl_->advance_preview_payload_revision(
      document_content_revision(preview.preview_document));
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
  if (document_kind(impl_->preview.preview_document) != SessionObjectKind::Mesh) {
    return Result<OperationResult>::failure(wrong_document_kind_error());
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
      document_content_revision(impl_->preview.preview_document));
  impl_->preview.payload.mesh.content_revision =
      impl_->preview.payload_revision;
  return Result<OperationResult>::success(std::move(receipt));
}

Result<PreviewId> ModelingSession::begin_curve_document_preview(
    ObjectId object_id, std::string operation_id,
    CurveDocument preview_document, OperationResult receipt) {
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
  const auto object = find_object(impl_->state, object_id);
  if (object == impl_->state.objects.end()) {
    return Result<PreviewId>::failure(
        make_error(ErrorCode::InvalidId, "Preview object ID is not live."));
  }
  if (object->kind() != SessionObjectKind::Curve) {
    return Result<PreviewId>::failure(wrong_document_kind_error());
  }
  if (!receipt.success) {
    return Result<PreviewId>::failure(make_error(
        receipt.error_code == ErrorCode::Ok ? ErrorCode::ValidationFailed
                                            : receipt.error_code,
        receipt.message.empty() ? "Preview operation failed." : receipt.message));
  }
  Result<OperationResult> identity =
      impl_->validate_commit_identity(object_id, preview_document);
  if (!identity.ok()) {
    return Result<PreviewId>::failure(identity.error());
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
  preview.base_content_identity = document_content_identity(object->document);
  preview.base_content_revision = document_content_revision(object->document);
  preview.base_session_revision = impl_->content_revision;
  receipt.changed = !object->curve_document().has_same_geometry(preview_document);
  receipt.dirty.overlays = true;
  preview.preview_document = std::move(preview_document);
  preview.receipt = std::move(receipt);
  preview.dirty = preview.receipt.dirty;
  preview.payload_revision = impl_->advance_preview_payload_revision(
      document_content_revision(preview.preview_document));
  impl_->record_topology_identity(object_id, preview.preview_document);
  impl_->preview = std::move(preview);
  return allocated;
}

Result<OperationResult> ModelingSession::update_curve_document_preview(
    PreviewId preview_id, CurveDocument preview_document,
    OperationResult receipt) {
  if (!impl_->preview.active || impl_->preview.id != preview_id) {
    return Result<OperationResult>::failure(
        make_error(ErrorCode::StaleId, "Preview ID is not active."));
  }
  if (document_kind(impl_->preview.preview_document) != SessionObjectKind::Curve) {
    return Result<OperationResult>::failure(wrong_document_kind_error());
  }
  if (!impl_->preview_matches_base()) {
    return Result<OperationResult>::failure(make_error(
        ErrorCode::StaleId,
        "Preview base changed before the preview could be updated."));
  }
  if (!receipt.success) {
    return Result<OperationResult>::failure(make_error(
        receipt.error_code == ErrorCode::Ok ? ErrorCode::ValidationFailed
                                            : receipt.error_code,
        receipt.message.empty() ? "Preview operation failed." : receipt.message));
  }
  Result<OperationResult> identity = impl_->validate_commit_identity(
      impl_->preview.object_id, preview_document);
  if (!identity.ok()) {
    return identity;
  }
  const auto &current =
      std::get<CurveDocument>(impl_->preview.preview_document);
  if (current.has_same_geometry(preview_document)) {
    OperationResult unchanged;
    unchanged.message = "Curve preview unchanged.";
    return Result<OperationResult>::success(std::move(unchanged));
  }
  const auto &base = std::get<CurveDocument>(impl_->preview.base_document);
  receipt.changed = !base.has_same_geometry(preview_document);
  receipt.dirty.overlays = true;
  impl_->preview.preview_document = std::move(preview_document);
  impl_->preview.receipt = receipt;
  impl_->preview.dirty = receipt.dirty;
  impl_->preview.payload_revision = impl_->advance_preview_payload_revision(
      document_content_revision(impl_->preview.preview_document));
  impl_->record_topology_identity(impl_->preview.object_id,
                                   impl_->preview.preview_document);
  return Result<OperationResult>::success(std::move(receipt));
}

Result<SessionPreviewCheckpoint>
ModelingSession::capture_preview_checkpoint(PreviewId preview_id) const {
  if (!impl_->preview.active || impl_->preview.id != preview_id ||
      !impl_->preview_matches_base()) {
    return Result<SessionPreviewCheckpoint>::failure(make_error(
        ErrorCode::StaleId, "Preview ID or its base is no longer active."));
  }
  SessionPreviewCheckpoint checkpoint;
  checkpoint.impl_ = std::make_shared<const SessionPreviewCheckpointImpl>(
      SessionPreviewCheckpointImpl{
          .origin = impl_->preview_checkpoint_ownership.origin,
          .preview = impl_->preview});
  return Result<SessionPreviewCheckpoint>::success(std::move(checkpoint));
}

Result<OperationResult> ModelingSession::restore_preview_checkpoint(
    const SessionPreviewCheckpoint &checkpoint) {
  if (!checkpoint.valid()) {
    return Result<OperationResult>::failure(make_error(
        ErrorCode::InvalidArgument, "Preview checkpoint is empty."));
  }
  const PreviewTransaction &saved = checkpoint.impl_->preview;
  const PreviewTransaction &current = impl_->preview;
  if (checkpoint.impl_->origin != impl_->preview_checkpoint_ownership.origin ||
      !current.active || current.id != saved.id ||
      current.object_id != saved.object_id ||
      current.base_content_identity != saved.base_content_identity ||
      current.base_content_revision != saved.base_content_revision ||
      current.base_session_revision != saved.base_session_revision ||
      !impl_->preview_matches_base()) {
    return Result<OperationResult>::failure(make_error(
        ErrorCode::StaleId,
        "Preview checkpoint does not belong to this active session preview."));
  }
  OperationResult restored;
  // payload_revision is the authored preview publication identity. Restoring
  // allocator high-water can replace only a polygon backing allocation while
  // retaining that publication, so content_identity is not a no-op criterion.
  if (current.payload_revision == saved.payload_revision &&
      current.payload_valid == saved.payload_valid &&
      document_selection_revision(current.preview_document) ==
          document_selection_revision(saved.preview_document)) {
    restored.message = "Preview checkpoint unchanged.";
    return Result<OperationResult>::success(std::move(restored));
  }

  // The candidate may use identities retired by a later failed publication.
  // Restore it directly, leaving the session's allocator high-water intact.
  impl_->preview = saved;
  impl_->reconcile_topology_identity(saved.object_id,
                                      impl_->preview.preview_document);
  restored.changed = true;
  restored.dirty = saved.dirty;
  restored.message = "Preview checkpoint restored.";
  return Result<OperationResult>::success(std::move(restored));
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
  if (object->kind() == SessionObjectKind::Curve &&
      object->curve_document().has_same_geometry(
          std::get<CurveDocument>(impl_->preview.preview_document))) {
    result.changed = false;
    result.dirty = {};
  }
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
  if (object->kind() == SessionObjectKind::Mesh) {
    object->selected = true;
    impl_->state.active_object = object->id;
  }
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
  if (!impl_->undo_stack.back().is_state_snapshot()) {
    impl_->redo_stack.reserve(impl_->redo_stack.size() + 1U);
    SessionHistoryEntry &entry = impl_->undo_stack.back();
    if (!toggle_component_selection_history(impl_->state,
                                            entry.selection_delta())) {
      return Result<OperationResult>::failure(make_error(
          ErrorCode::InternalError,
          "Component selection history references a missing object."));
    }
    impl_->redo_stack.push_back(std::move(entry));
    impl_->undo_stack.pop_back();
    impl_->publish_component_selection_transition(
        impl_->redo_stack.back().selection_delta());
    return Result<OperationResult>::success(
        selection_session_result("Undo."));
  }
  impl_->redo_stack.push_back(impl_->state);
  impl_->state =
      std::move(impl_->undo_stack.back().state_snapshot());
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
  if (!impl_->redo_stack.back().is_state_snapshot()) {
    impl_->undo_stack.reserve(impl_->undo_stack.size() + 1U);
    SessionHistoryEntry &entry = impl_->redo_stack.back();
    if (!toggle_component_selection_history(impl_->state,
                                            entry.selection_delta())) {
      return Result<OperationResult>::failure(make_error(
          ErrorCode::InternalError,
          "Component selection history references a missing object."));
    }
    impl_->undo_stack.push_back(std::move(entry));
    impl_->redo_stack.pop_back();
    impl_->publish_component_selection_transition(
        impl_->undo_stack.back().selection_delta());
    return Result<OperationResult>::success(
        selection_session_result("Redo."));
  }
  impl_->undo_stack.push_back(impl_->state);
  impl_->state =
      std::move(impl_->redo_stack.back().state_snapshot());
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
  if (mark + 1U == impl_->undo_stack.size()) {
    return;
  }
  SessionState first_batch_snapshot = impl_->state;
  for (std::size_t index = impl_->undo_stack.size(); index > mark; --index) {
    if (!rewind_history_entry(first_batch_snapshot,
                              impl_->undo_stack[index - 1U])) {
      return;
    }
  }
  impl_->undo_stack.erase(impl_->undo_stack.begin() + mark,
                          impl_->undo_stack.end());
  impl_->undo_stack.push_back(std::move(first_batch_snapshot));
}

void ModelingSession::discard_undo_since(std::size_t mark) {
  if (mark >= impl_->undo_stack.size()) {
    return;
  }
  impl_->undo_stack.erase(impl_->undo_stack.begin() + mark,
                          impl_->undo_stack.end());
  impl_->redo_stack.clear();
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
  for (std::size_t index = impl_->undo_stack.size(); index > mark; --index) {
    if (!rewind_history_entry(impl_->state,
                              impl_->undo_stack[index - 1U])) {
      impl_->state = std::move(before);
      return false;
    }
  }
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
    restored.curve.next_point_index =
        std::max(restored.curve.next_point_index, high_water.curve.next_point_index);
    restored.curve.next_segment_index =
        std::max(restored.curve.next_segment_index, high_water.curve.next_segment_index);
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
  auto object_change_index = impl_->object_change_index;
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
  impl_->object_change_index = std::move(object_change_index);
  for (const auto &[object_index, high_water] :
       topology_identity_high_water) {
    auto &restored = impl_->topology_identity_high_water[object_index];
    restored.next_vertex_id =
        std::max(restored.next_vertex_id, high_water.next_vertex_id);
    restored.next_face_id =
        std::max(restored.next_face_id, high_water.next_face_id);
    restored.curve.next_point_index =
        std::max(restored.curve.next_point_index, high_water.curve.next_point_index);
    restored.curve.next_segment_index =
        std::max(restored.curve.next_segment_index, high_water.curve.next_segment_index);
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
        .content_revision = document_content_revision(object.document),
        .selection_revision = document_selection_revision(object.document),
        .kind = object.kind(),
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
        .kind = record.kind,
    });
  }
  return snapshot;
}

const PolygonDocument *ModelingSession::document(ObjectId object_id) const {
  const auto found = find_object(impl_->state, object_id);
  return found == impl_->state.objects.end()
             ? nullptr
             : std::get_if<PolygonDocument>(&found->document);
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
  if (found->kind() != SessionObjectKind::Mesh) {
    return Result<PolygonDocument>::failure(wrong_document_kind_error());
  }
  PolygonDocument copy = found->polygon_document();
  impl_->reconcile_topology_identity(object_id, copy);
  return Result<PolygonDocument>::success(std::move(copy));
}

Result<std::shared_ptr<const CurveDocument>>
ModelingSession::curve_document_snapshot(ObjectId object_id) const {
  Result<CurveDocument> copy = curve_document_copy(object_id);
  if (!copy.ok()) {
    return Result<std::shared_ptr<const CurveDocument>>::failure(copy.error());
  }
  return Result<std::shared_ptr<const CurveDocument>>::success(
      std::make_shared<const CurveDocument>(std::move(copy).value()));
}

Result<CurveDocument>
ModelingSession::curve_document_copy(ObjectId object_id) const {
  const auto found = find_object(impl_->state, object_id);
  if (found == impl_->state.objects.end()) {
    return Result<CurveDocument>::failure(
        make_error(ErrorCode::InvalidId, "Object ID is not live."));
  }
  if (found->kind() != SessionObjectKind::Curve) {
    return Result<CurveDocument>::failure(wrong_document_kind_error());
  }
  CurveDocument copy = found->curve_document();
  impl_->reconcile_topology_identity(object_id, copy);
  return Result<CurveDocument>::success(std::move(copy));
}

Result<std::vector<PolygonDocument>>
ModelingSession::document_copies(std::span<const ObjectId> object_ids) const {
  std::unordered_map<std::uint64_t, const SessionObject *> object_by_id;
  object_by_id.reserve(impl_->state.objects.size());
  for (const SessionObject &object : impl_->state.objects) {
    object_by_id.emplace(object_id_key(object.id), &object);
  }

  std::vector<PolygonDocument> copies;
  copies.reserve(object_ids.size());
  for (const ObjectId object_id : object_ids) {
    const auto found = object_by_id.find(object_id_key(object_id));
    if (!object_id.valid() || found == object_by_id.end()) {
      return Result<std::vector<PolygonDocument>>::failure(
          make_error(ErrorCode::InvalidId, "Object ID is not live."));
    }
    if (found->second->kind() != SessionObjectKind::Mesh) {
      return Result<std::vector<PolygonDocument>>::failure(wrong_document_kind_error());
    }
    PolygonDocument copy = found->second->polygon_document();
    impl_->reconcile_topology_identity(object_id, copy);
    copies.push_back(std::move(copy));
  }
  return Result<std::vector<PolygonDocument>>::success(std::move(copies));
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
  if (found->kind() != SessionObjectKind::Mesh) {
    return Result<OperationResult>::failure(wrong_document_kind_error());
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

Result<OperationResult> ModelingSession::commit_curve_document(
    ObjectId object_id, CurveDocument document, OperationResult receipt) {
  if (!impl_->allow_batch_object(object_id)) {
    return Result<OperationResult>::failure(make_error(
        ErrorCode::InvalidArgument,
        "Batch callback cannot mutate an unrelated document."));
  }
  if (impl_->preview.active) {
    return active_preview_error();
  }
  const auto found = find_object(impl_->state, object_id);
  if (found == impl_->state.objects.end()) {
    return Result<OperationResult>::failure(
        make_error(ErrorCode::InvalidId, "Object ID is not live."));
  }
  if (found->kind() != SessionObjectKind::Curve) {
    return Result<OperationResult>::failure(wrong_document_kind_error());
  }
  if (!receipt.success || !receipt.changed) {
    return Result<OperationResult>::success(std::move(receipt));
  }
  Result<OperationResult> identity =
      impl_->validate_commit_identity(object_id, document);
  if (!identity.ok()) {
    return identity;
  }
  if (found->curve_document().has_same_geometry(document)) {
    receipt.changed = false;
    receipt.dirty = {};
    receipt.message = "Curve unchanged.";
    return Result<OperationResult>::success(std::move(receipt));
  }

  impl_->undo_stack.push_back(impl_->state);
  impl_->redo_stack.clear();
  found->document = std::move(document);
  impl_->record_topology_identity(object_id, found->document);
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
  const auto *document =
      std::get_if<PolygonDocument>(&impl_->preview.preview_document);
  if (document == nullptr) {
    return Result<PolygonDocument>::failure(wrong_document_kind_error());
  }
  return Result<PolygonDocument>::success(*document);
}

Result<CurveDocument>
ModelingSession::preview_curve_document_copy(PreviewId preview_id) const {
  if (!impl_->preview.active || impl_->preview.id != preview_id) {
    return Result<CurveDocument>::failure(
        make_error(ErrorCode::StaleId, "Preview ID is not active."));
  }
  const auto *document =
      std::get_if<CurveDocument>(&impl_->preview.preview_document);
  if (document == nullptr) {
    return Result<CurveDocument>::failure(wrong_document_kind_error());
  }
  CurveDocument copy = *document;
  impl_->reconcile_topology_identity(impl_->preview.object_id, copy);
  return Result<CurveDocument>::success(std::move(copy));
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
      .kind = document_kind(impl_->preview.preview_document),
  };
}

Result<SemanticOverlayPayload>
ModelingSession::semantic_overlay(ObjectId object_id) const {
  const auto object = find_object(impl_->state, object_id);
  if (object == impl_->state.objects.end()) {
    return Result<SemanticOverlayPayload>::failure(
        make_error(ErrorCode::InvalidId, "Object ID is not live."));
  }
  if (object->kind() != SessionObjectKind::Mesh) {
    return Result<SemanticOverlayPayload>::failure(wrong_document_kind_error());
  }
  return build_semantic_overlay_payload(object->polygon_document());
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
