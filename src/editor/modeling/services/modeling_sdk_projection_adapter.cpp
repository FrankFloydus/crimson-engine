////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/services/modeling_sdk_projection_adapter.hpp"

#include "assets/asset_registry.hpp"
#include "assets/mesh_asset_payload.hpp"
#include "editor/modeling/runtime/modeling_object_runtime_map.hpp"
#include "editor/modeling/operations/portable_operation_mapping.hpp"
#include "editor/modeling/services/modeling_mesh_sync_service.hpp"
#include "editor/modeling/services/modeling_selection_service.hpp"
#include "editor/projection/mesh_projection_service.hpp"
#include "editor/selection/scene_selection_application.hpp"
#include "diagnostics/profile.hpp"
#include "math/glm_bridge.hpp"
#include "render/viewport_overlay_frame.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_transform.hpp"

#include <quader/modeling/modeling.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace quader {
namespace {

using quader::modeling::AuthoredTopology;
using quader::modeling::EdgeKey;
using quader::modeling::FaceId;
using quader::modeling::KnifeStrokeSegment;
using quader::modeling::KnifeTarget;
using quader::modeling::KnifeTargetKind;
using quader::modeling::ObjectId;
using quader::modeling::OperationResult;
using quader::modeling::PolygonDocument;
using quader::modeling::PolygonSelectionSnapshot;
using quader::modeling::PreviewId;
using quader::modeling::Result;
using quader::modeling::SelectionEdit;
using quader::modeling::SelectionKind;
using quader::modeling::SelectionMode;
using quader::modeling::SessionDocumentHistoryRestore;
using quader::modeling::SessionDocumentUpdate;
using quader::modeling::SessionWorkspaceDocument;
using quader::modeling::Vec3;
using quader::modeling::VertexId;
using quader::modeling::VertexPositionUpdate;

[[nodiscard]] constexpr std::uint64_t sdk_object_key(ObjectId object) {
  return (static_cast<std::uint64_t>(object.generation) << 32U) |
         static_cast<std::uint64_t>(object.index);
}

[[nodiscard]] std::uint64_t transform_mesh_layout_key(
    ObjectId object, std::span<const MeshVertex> vertices) {
  constexpr std::uint64_t kOffsetBasis = 14695981039346656037ULL;
  constexpr std::uint64_t kPrime = 1099511628211ULL;
  std::uint64_t key = kOffsetBasis;
  const auto mix = [&key](std::uint64_t value) {
    for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
      key ^= (value >> (byte * 8U)) & 0xffU;
      key *= kPrime;
    }
  };
  mix(sdk_object_key(object));
  mix(vertices.size());
  for (const MeshVertex &vertex : vertices) {
    mix(vertex.source_vertex_id);
  }
  return key;
}

/// Captures one SDK-owned object's durable native projection for history.
struct SdkProjectedObjectSnapshot {
  int modeling_object_id = kInvalidModelingObjectId;
  ModelingObjectSceneIdentity identity;
  std::uint32_t material_slot = 0;
  AssetId mesh_id = kInvalidAssetId;
  MeshAsset mesh;
};

[[nodiscard]] quader::modeling::Vec3 to_sdk(QVec3 value) {
  return {value.x, value.y, value.z};
}

[[nodiscard]] quader::modeling::Transform3
to_sdk(const ModelingTransform3 &value, QVec3 pivot = {}) {
  const QVec3 linear_pivot =
      value.x_axis * pivot.x + value.y_axis * pivot.y +
      value.z_axis * pivot.z;
  return {
      .x_axis = to_sdk(value.x_axis),
      .y_axis = to_sdk(value.y_axis),
      .z_axis = to_sdk(value.z_axis),
      .origin = to_sdk(value.origin + pivot - linear_pivot),
  };
}

[[nodiscard]] QVec3 transform_sdk_point(
    const quader::modeling::Transform3 &transform, QVec3 point) {
  return {
      transform.x_axis.x * point.x + transform.y_axis.x * point.y +
          transform.z_axis.x * point.z + transform.origin.x,
      transform.x_axis.y * point.x + transform.y_axis.y * point.y +
          transform.z_axis.y * point.z + transform.origin.y,
      transform.x_axis.z * point.x + transform.y_axis.z * point.y +
          transform.z_axis.z * point.z + transform.origin.z,
  };
}

[[nodiscard]] quader::modeling::Transform3 to_sdk(const QMat4 &value) {
  const glm::mat4 matrix = to_glm(value);
  return {
      .x_axis = to_sdk(from_glm(glm::vec3{matrix[0]})),
      .y_axis = to_sdk(from_glm(glm::vec3{matrix[1]})),
      .z_axis = to_sdk(from_glm(glm::vec3{matrix[2]})),
      .origin = to_sdk(from_glm(glm::vec3{matrix[3]})),
  };
}

[[nodiscard]] std::string_view
sdk_operation_id(quader_editor_core::EditorOperationId operation) {
  const quader::modeling::OperationDescriptor *descriptor =
      quader_editor_core::portable_operation_descriptor(operation);
  return descriptor != nullptr ? descriptor->dispatch_id : std::string_view{};
}

[[nodiscard]] SelectionEdit to_sdk(ModelingSelectionEdit edit) {
  switch (edit) {
  case ModelingSelectionEdit::Replace:
    return SelectionEdit::Replace;
  case ModelingSelectionEdit::Add:
    return SelectionEdit::Add;
  case ModelingSelectionEdit::Remove:
    return SelectionEdit::Remove;
  case ModelingSelectionEdit::Toggle:
    return SelectionEdit::Toggle;
  }
  return SelectionEdit::Replace;
}

[[nodiscard]] SelectionMode to_sdk_component_mode(ModelingSelectionMode mode) {
  switch (mode) {
  case ModelingSelectionMode::Edge:
    return SelectionMode::Edge;
  case ModelingSelectionMode::Face:
    return SelectionMode::Face;
  case ModelingSelectionMode::Object:
  case ModelingSelectionMode::Group:
  case ModelingSelectionMode::Vertex:
    return SelectionMode::Vertex;
  }
  return SelectionMode::Vertex;
}

[[nodiscard]] SelectionKind to_sdk_selection_kind(ModelingSelectionMode mode) {
  switch (mode) {
  case ModelingSelectionMode::Edge:
    return SelectionKind::Edge;
  case ModelingSelectionMode::Face:
    return SelectionKind::Face;
  case ModelingSelectionMode::Object:
  case ModelingSelectionMode::Group:
  case ModelingSelectionMode::Vertex:
    return SelectionKind::Vertex;
  }
  return SelectionKind::Vertex;
}

[[nodiscard]] bool component_selection_participates_in_mode(
    const PolygonSelectionSnapshot &selection, ModelingSelectionMode mode) {
  switch (mode) {
  case ModelingSelectionMode::Vertex:
    return !selection.vertices.empty();
  case ModelingSelectionMode::Edge:
    return !selection.edges.empty();
  case ModelingSelectionMode::Face:
    return !selection.faces.empty();
  case ModelingSelectionMode::Object:
  case ModelingSelectionMode::Group:
    return false;
  }
  return false;
}

[[nodiscard]] ModelingSelectionMode from_native_selection_mode(
    quader_editor_core::SelectionMode mode) {
  switch (mode) {
  case quader_editor_core::SelectionMode::Vertex:
    return ModelingSelectionMode::Vertex;
  case quader_editor_core::SelectionMode::Edge:
    return ModelingSelectionMode::Edge;
  case quader_editor_core::SelectionMode::Face:
    return ModelingSelectionMode::Face;
  case quader_editor_core::SelectionMode::Object:
    return ModelingSelectionMode::Object;
  }
  return ModelingSelectionMode::Object;
}

[[nodiscard]] bool finite(QVec3 value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

constexpr float kCreationFrameEpsilonSquared = 0.0000000001F;

/// Carries one rigid scene placement and its world-to-local point projection.
struct CreationFrame {
  ModelingTransform3 transform;

  [[nodiscard]] QVec3 local_point(QVec3 world_point) const {
    const QVec3 offset = world_point - transform.origin;
    return {
        dot(offset, transform.x_axis),
        dot(offset, transform.y_axis),
        dot(offset, transform.z_axis),
    };
  }
};

[[nodiscard]] std::optional<CreationFrame>
creation_frame_from_xy(QVec3 origin, QVec3 x_hint, QVec3 y_hint) {
  if (!finite(origin) || !finite(x_hint) || !finite(y_hint) ||
      length_squared(x_hint) <= kCreationFrameEpsilonSquared) {
    return std::nullopt;
  }
  const QVec3 x_axis = normalize(x_hint);
  const QVec3 rejected_y = y_hint - x_axis * dot(y_hint, x_axis);
  if (length_squared(rejected_y) <= kCreationFrameEpsilonSquared) {
    return std::nullopt;
  }
  const QVec3 y_axis = normalize(rejected_y);
  const QVec3 z_axis = normalize(cross(x_axis, y_axis));
  if (!finite(x_axis) || !finite(y_axis) || !finite(z_axis) ||
      length_squared(z_axis) <= kCreationFrameEpsilonSquared) {
    return std::nullopt;
  }
  return CreationFrame{.transform = {
                           .x_axis = x_axis,
                           .y_axis = y_axis,
                           .z_axis = z_axis,
                           .origin = origin,
                       }};
}

[[nodiscard]] std::optional<CreationFrame>
creation_frame_from_xz(QVec3 origin, QVec3 x_hint, QVec3 z_hint) {
  if (!finite(origin) || !finite(x_hint) || !finite(z_hint) ||
      length_squared(z_hint) <= kCreationFrameEpsilonSquared) {
    return std::nullopt;
  }
  const QVec3 z_axis = normalize(z_hint);
  const QVec3 rejected_x = x_hint - z_axis * dot(x_hint, z_axis);
  if (length_squared(rejected_x) <= kCreationFrameEpsilonSquared) {
    return std::nullopt;
  }
  const QVec3 x_axis = normalize(rejected_x);
  const QVec3 y_axis = normalize(cross(z_axis, x_axis));
  if (!finite(x_axis) || !finite(y_axis) || !finite(z_axis) ||
      length_squared(y_axis) <= kCreationFrameEpsilonSquared) {
    return std::nullopt;
  }
  return CreationFrame{.transform = {
                           .x_axis = x_axis,
                           .y_axis = y_axis,
                           .z_axis = z_axis,
                           .origin = origin,
                       }};
}

[[nodiscard]] std::optional<TransformComponent>
scene_transform_from_modeling_transform(const ModelingTransform3 &transform) {
  if (!finite(transform.x_axis) || !finite(transform.y_axis) ||
      !finite(transform.z_axis) || !finite(transform.origin)) {
    return std::nullopt;
  }
  const QMat4 matrix{.values = {
                        transform.x_axis.x,
                        transform.x_axis.y,
                        transform.x_axis.z,
                        0.0F,
                        transform.y_axis.x,
                        transform.y_axis.y,
                        transform.y_axis.z,
                        0.0F,
                        transform.z_axis.x,
                        transform.z_axis.y,
                        transform.z_axis.z,
                        0.0F,
                        transform.origin.x,
                        transform.origin.y,
                        transform.origin.z,
                        1.0F,
                    }};
  const std::optional<TransformComponent> component =
      try_transform_component_from_matrix(matrix);
  if (!component.has_value()) {
    return std::nullopt;
  }
  const QMat4 represented = compose_transform(component->spatial_transform());
  for (std::size_t index = 0; index < matrix.values.size(); ++index) {
    const float tolerance =
        0.0001F * std::max(1.0F, std::abs(matrix.values[index]));
    if (std::abs(represented.values[index] - matrix.values[index]) >
        tolerance) {
      return std::nullopt;
    }
  }
  return component;
}

[[nodiscard]] ModelingCommandResult failure(std::string message) {
  return {.ok = false, .changed = false, .message = std::move(message)};
}

[[nodiscard]] ModelingCommandResult from_sdk(
    const Result<OperationResult> &result, std::string fallback = {}) {
  if (!result.ok()) {
    return failure(result.error_message().empty() ? std::move(fallback)
                                                  : result.error_message());
  }
  return {
      .ok = result.value().success,
      .changed = result.value().changed,
      .message = result.value().message,
  };
}

[[nodiscard]] bool scene_mesh_is_shared(const SceneDocument &scene,
                                        EntityId owner, AssetId mesh) {
  if (mesh == kInvalidAssetId) {
    return false;
  }
  for (const EntityId entity : scene.entities()) {
    const MeshComponent *component = scene.mesh(entity);
    if (entity != owner && component != nullptr && component->mesh == mesh) {
      return true;
    }
  }
  return false;
}

} // namespace

/// Stores the opaque SDK and native mapping state carried by one memento.
struct ModelingSdkProjectionSnapshot::Impl {
  quader::modeling::ModelingSession session;
  std::map<int, ObjectId> object_ids;
  std::vector<SdkProjectedObjectSnapshot> projected_objects;
  int next_native_object_id = 1;
  int active_native_object_id = kInvalidModelingObjectId;
  ModelingSelectionMode selection_mode = ModelingSelectionMode::Object;
  std::uint64_t persistent_content_revision = 0;
  std::uint64_t mode_revision = 0;
};

ModelingSdkProjectionSnapshot::ModelingSdkProjectionSnapshot() = default;
ModelingSdkProjectionSnapshot::~ModelingSdkProjectionSnapshot() = default;

ModelingSdkProjectionSnapshot::ModelingSdkProjectionSnapshot(
    std::shared_ptr<const Impl> impl)
    : impl_(std::move(impl)) {}

bool ModelingSdkProjectionSnapshot::valid() const noexcept {
  return static_cast<bool>(impl_);
}

/// Stores immutable per-object authored-position deltas for sparse transform
/// undo and redo without cloning complete modeling sessions.
struct ModelingSdkTransformHistoryDelta::Impl {
  /// Pairs one SDK object with its exact sparse before and after positions.
  struct ObjectDelta {
    ObjectId object{};
    std::vector<VertexPositionUpdate> before;
    std::vector<VertexPositionUpdate> after;
  };

  std::vector<ObjectDelta> objects;
};

ModelingSdkTransformHistoryDelta::ModelingSdkTransformHistoryDelta() = default;
ModelingSdkTransformHistoryDelta::~ModelingSdkTransformHistoryDelta() = default;

ModelingSdkTransformHistoryDelta::ModelingSdkTransformHistoryDelta(
    std::shared_ptr<const Impl> impl)
    : impl_(std::move(impl)) {}

bool ModelingSdkTransformHistoryDelta::valid() const noexcept {
  return impl_ != nullptr && !impl_->objects.empty();
}

/// Implements one workspace-scoped SDK session and its host projection map.
class ModelingSdkProjectionAdapter::Impl final {
private:
  using DocumentMutation =
      std::function<Result<OperationResult>(PolygonDocument &)>;
  using DocumentMutationByObject =
      std::function<Result<OperationResult>(ObjectId, PolygonDocument &)>;
  using WorkspaceTransformResolver = std::function<
      std::optional<quader::modeling::Transform3>(ObjectId)>;

  /// Tracks one selected authored vertex and every compiled position derived
  /// from it during a topology-stable transform preview.
  struct WorkspaceTransformSelectedVertex {
    std::uint32_t source_vertex_id = 0u;
    QVec3 base_position;
    QVec3 preview_position;
    std::vector<std::size_t> compiled_vertex_indices;
  };

  /// Owns reusable preview buffers and retained topology state for one SDK
  /// object participating in a workspace transform.
  struct WorkspaceTransformParticipant {
    ObjectId object;
    int native_object_id = kInvalidModelingObjectId;
    EntityId entity = kInvalidEntityId;
    MaterialComponent material;
    QMat4 world_transform;
    bool double_sided = false;
    std::uint64_t layout_key = 0u;
    std::uint64_t position_revision = 0u;
    PolygonDocument preview_document;
    MeshAsset preview_mesh;
    std::vector<QVec3> base_mesh_positions;
    std::vector<WorkspaceTransformSelectedVertex> selected_vertices;
    std::unordered_map<std::uint32_t, std::size_t> selected_vertex_lookup;
    // The pool owns mutable packed position buffers. A buffer is reused only
    // when no immutable render snapshot still pins it (use_count == 1).
    std::vector<std::shared_ptr<std::vector<QVec3>>> position_buffers;
    std::size_t published_position_buffer = 0u;
    std::unordered_map<std::uint32_t, QVec3> base_positions;
    std::unordered_map<std::uint32_t, std::vector<std::uint32_t>>
        face_vertices;
  };

  [[nodiscard]] static Result<OperationResult>
  apply_with_whole_object_face_selection(
      ModelingSelectionMode mode, PolygonDocument &document,
      const DocumentMutation &operation) {
    if (mode != ModelingSelectionMode::Object &&
        mode != ModelingSelectionMode::Group) {
      return operation(document);
    }
    Result<OperationResult> selected = document.select_all_faces();
    if (!selected.ok()) {
      return selected;
    }
    Result<OperationResult> operated = operation(document);
    if (!operated.ok()) {
      return operated;
    }
    Result<OperationResult> cleared = document.clear_selection();
    if (!cleared.ok()) {
      return cleared;
    }
    operated.value().dirty.selection = false;
    return operated;
  }

  /// Retains enough portable operation state to rebuild a preview after a
  /// selection edit is applied to its committed base document.
  struct PreviewSelectionEditState {
    bool active = false;
    bool ok = true;
    std::string message;
    quader_editor_core::EditorOperationId operation{};
    quader_editor_core::EditorOperationSettings settings;
  };

  /// Keys the derived selection summary by every SDK and host owner revision.
  struct SelectionSummaryCacheKey {
    std::uint64_t persistent_content_revision = 0;
    std::uint64_t session_content_revision = 0;
    std::uint64_t session_selection_revision = 0;
    std::uint64_t mode_revision = 0;
    std::uint64_t object_map_revision = 0;
    int active_native_object_id = kInvalidModelingObjectId;
    ModelingSelectionMode mode = ModelingSelectionMode::Object;
    bool workspace_preview_active = false;
    bool sdk_preview_active = false;
    PreviewId preview_id{};
    ObjectId preview_object_id{};
    std::uint64_t preview_content_revision = 0;

    friend bool operator==(const SelectionSummaryCacheKey &,
                           const SelectionSummaryCacheKey &) = default;
  };

  /// Keys the immutable native projection used by read-only host queries.
  struct NativeSelectionProjectionCacheKey {
    std::uint64_t persistent_content_revision = 0;
    std::uint64_t session_content_revision = 0;
    std::uint64_t session_selection_revision = 0;
    std::uint64_t mode_revision = 0;
    std::uint64_t scene_mesh_binding_revision = 0;
    std::uint64_t asset_revision = 0;
    std::uint64_t object_map_revision = 0;
    int active_native_object_id = kInvalidModelingObjectId;
    ModelingSelectionMode mode = ModelingSelectionMode::Object;
    bool workspace_preview_active = false;
    bool sdk_preview_active = false;
    PreviewId preview_id{};
    ObjectId preview_object_id{};
    std::uint64_t preview_content_revision = 0;

    friend bool operator==(const NativeSelectionProjectionCacheKey &,
                           const NativeSelectionProjectionCacheKey &) = default;
  };

  friend class ModelingSdkProjectionAdapter;

public:
  Impl(SceneDocument &scene, AssetRegistry &assets,
       ModelingObjectRuntimeMap &object_map,
       ModelingMeshSyncService &mesh_sync)
      : scene_(scene), assets_(assets), object_map_(object_map),
        mesh_sync_(mesh_sync) {}

  void set_modeling_object_world_transform_resolver(
      std::function<std::optional<QMat4>(int)> resolver) {
    modeling_object_world_transform_resolver_ = std::move(resolver);
  }

  [[nodiscard]] Result<PolygonDocument> reconciled_sdk_document(
      ObjectId object, const quader_poly::Document &document) const {
    quader_poly::Document reconciled = document;
    Result<PolygonDocument> current = session_.document_copy(object);
    Result<AuthoredTopology> current_topology =
        current.ok()
            ? current.value().authored_topology()
            : Result<AuthoredTopology>::failure(current.error());
    if (!current_topology.ok()) {
      return Result<PolygonDocument>::failure(current_topology.error());
    }
    reconciled.next_vertex_id =
        std::max(reconciled.next_vertex_id,
                 current_topology.value().next_vertex_id);
    reconciled.next_face_id =
        std::max(reconciled.next_face_id,
                 current_topology.value().next_face_id);
    return MeshProjectionService::sdk_document_from_polygon_document(
        reconciled);
  }

  void reset_from_scene() {
    ++stats_.full_scene_reset_count;
    object_ids_.clear();
    preview_base_snapshot_.reset();
    workspace_update_base_snapshot_.reset();
    preview_asset_checkpoint_.reset();
    preview_scene_checkpoint_.reset();
    preview_changed_ = false;
    workspace_preview_active_ = false;
    workspace_preview_undo_mark_ = 0U;
    workspace_preview_world_from_object_.clear();
    workspace_transform_participants_.clear();
    workspace_transform_commit_mutation_ = {};
    workspace_transform_transient_active_ = false;
    workspace_transform_snapshotless_ = false;
    workspace_transform_preexisting_changed_ = false;
    committed_transform_history_delta_ = {};
    committed_transform_undo_mark_.reset();
    ++workspace_transform_preview_revision_;
    active_preview_operation_id_.clear();
    active_preview_operation_.reset();
    active_preview_settings_.reset();
    selection_mode_ = ModelingSelectionMode::Object;
    persistent_content_revision_ = 0;
    mode_revision_ = 0;
    active_native_object_id_ = kInvalidModelingObjectId;
    next_native_object_id_ = 1;

    /// Associates one hydrated SDK document with its native scene identity.
    struct SceneWorkspaceObject {
      int native_object_id = kInvalidModelingObjectId;
      EntityId entity = kInvalidEntityId;
      bool mark_sdk_owned = false;
    };
    std::vector<quader::modeling::SessionWorkspaceDocument>
        workspace_documents;
    std::vector<SceneWorkspaceObject> workspace_objects;
    const std::vector<EntityId> scene_entities = scene_.entity_tree_order();
    workspace_documents.reserve(scene_entities.size());
    workspace_objects.reserve(scene_entities.size());
    const EntityId active_entity = scene_.primary_selection();
    for (const EntityId entity : scene_entities) {
      const ModelingObjectComponent *component = scene_.modeling_object(entity);
      if (component == nullptr ||
          component->object_id == kInvalidModelingObjectId) {
        continue;
      }
      if (component->object_id < std::numeric_limits<int>::max()) {
        next_native_object_id_ =
            std::max(next_native_object_id_, component->object_id + 1);
      }
      const MeshComponent *mesh_component = scene_.mesh(entity);
      const MeshAsset *mesh = mesh_component != nullptr
                                  ? assets_.mesh(mesh_component->mesh)
                                  : nullptr;
      if (mesh == nullptr || !mesh->has_authored_polygon_document ||
          !mesh->authored_polygon_document) {
        continue;
      }
      Result<PolygonDocument> document =
          MeshProjectionService::sdk_document_from_polygon_document(
              *mesh->authored_polygon_document);
      if (!document.ok()) {
        continue;
      }
      const NameComponent *name = scene_.name(entity);
      if (const SelectionComponent *selection = scene_.selection(entity);
          selection != nullptr) {
        workspace_documents.push_back({
            .document = std::move(document).value(),
            .name = name != nullptr && !name->name.empty() ? name->name
                                                            : "Mesh",
            .selected = selection->selected,
        });
      } else {
        workspace_documents.push_back({
            .document = std::move(document).value(),
            .name = name != nullptr && !name->name.empty() ? name->name
                                                            : "Mesh",
        });
      }
      workspace_objects.push_back({
          .native_object_id = component->object_id,
          .entity = entity,
          .mark_sdk_owned = !component->sdk_owned,
      });
      if (entity == active_entity) {
        active_native_object_id_ = component->object_id;
      }
    }

    Result<std::vector<ObjectId>> hydrated =
        session_.replace_workspace_preserving_clipboard(
            std::move(workspace_documents));
    if (!hydrated.ok() ||
        hydrated.value().size() != workspace_objects.size()) {
      session_.reset_workspace_preserving_clipboard();
      active_native_object_id_ = kInvalidModelingObjectId;
      return;
    }
    for (std::size_t index = 0; index < workspace_objects.size(); ++index) {
      const SceneWorkspaceObject &workspace_object = workspace_objects[index];
      const ObjectId object_id = hydrated.value()[index];
      object_ids_.emplace(workspace_object.native_object_id, object_id);
      object_map_.bind(workspace_object.native_object_id,
                       workspace_object.entity);
      object_map_.remember_scene_entity(workspace_object.native_object_id,
                                        scene_, workspace_object.entity);
      if (workspace_object.mark_sdk_owned) {
        (void)mesh_sync_.mark_projected_object_sdk_owned(
            scene_, workspace_object.entity);
      }
    }
    stats_.scene_import_document_count += workspace_objects.size();
  }

  [[nodiscard]] ModelingCommandResult restore_mesh_documents_from_scene(
      std::span<const EntityId> entities) {
    if (entities.empty()) {
      return {.ok = true,
              .changed = false,
              .message = "No modeling mesh documents changed."};
    }
    if (preview_active()) {
      return failure("Commit or cancel the active preview first.");
    }

    std::unordered_set<int> restored_native_ids;
    restored_native_ids.reserve(entities.size());
    std::vector<SessionDocumentHistoryRestore> restores;
    restores.reserve(entities.size());
    for (const EntityId entity : entities) {
      const ModelingObjectComponent *component =
          scene_.modeling_object(entity);
      const MeshComponent *mesh_component = scene_.mesh(entity);
      const MeshAsset *mesh =
          mesh_component != nullptr ? assets_.mesh(mesh_component->mesh)
                                    : nullptr;
      if (component == nullptr ||
          component->object_id == kInvalidModelingObjectId ||
          mesh == nullptr || !mesh->has_authored_polygon_document ||
          mesh->authored_polygon_document == nullptr) {
        return failure(
            "Pivot mesh is not an SDK-owned authored modeling document.");
      }
      if (!restored_native_ids.emplace(component->object_id).second) {
        continue;
      }
      const std::optional<ObjectId> object = sdk_object(component->object_id);
      if (!object.has_value()) {
        return failure("Pivot modeling object does not exist.");
      }
      Result<PolygonDocument> document = reconciled_sdk_document(
          *object, *mesh->authored_polygon_document);
      if (!document.ok()) {
        return failure(document.error_message());
      }
      restores.push_back({
          .object = *object,
          .document = std::move(document).value(),
          .dirty = {.topology = true,
                    .geometry = true,
                    .materials = true,
                    .overlays = true},
      });
    }

    Result<OperationResult> restored =
        session_.restore_documents_as_history_transition(std::move(restores));
    if (!restored.ok()) {
      return from_sdk(restored,
                      "Pivot modeling documents could not be restored.");
    }
    ++persistent_content_revision_;
    ++stats_.targeted_scene_sync_count;
    stats_.targeted_scene_sync_document_count += restored_native_ids.size();
    return from_sdk(restored);
  }

  [[nodiscard]] bool owns_object(int modeling_object_id) const {
    return object_ids_.contains(modeling_object_id);
  }

  [[nodiscard]] bool owns_entity(EntityId entity) const {
    const std::optional<int> object = object_map_.modeling_object_for_entity(entity);
    return object.has_value() && owns_object(*object);
  }

  [[nodiscard]] std::size_t object_count() const noexcept {
    return object_ids_.size();
  }

  [[nodiscard]] bool has_selection() const {
    return selection_summary().selected_object_count != 0;
  }

  [[nodiscard]] const void *session_identity() const noexcept {
    return &session_;
  }

  [[nodiscard]] std::optional<MeshAsset>
  compile_object_mesh(int modeling_object_id) const {
    const std::optional<EntityId> entity =
        object_map_.entity_for_modeling_object(modeling_object_id);
    const MeshComponent *binding =
        entity.has_value() && scene_.exists(*entity) ? scene_.mesh(*entity)
                                                     : nullptr;
    const MeshAsset *projected =
        binding != nullptr ? assets_.mesh(binding->mesh) : nullptr;
    if (projected != nullptr) {
      return *projected;
    }
    return std::nullopt;
  }

  [[nodiscard]] ModelingSdkPlaneCutPreviewResult preview_plane_cut(
      std::span<const int> modeling_object_ids,
      const std::array<QVec3, 3> &world_plane_points,
      ModelingCutMode mode) const {
    ModelingSdkPlaneCutPreviewResult preview;
    preview.target_count = static_cast<int>(modeling_object_ids.size());
    if (modeling_object_ids.empty()) {
      preview.command = failure("Cut needs a selected mesh.");
      return preview;
    }

    for (const int native_id : modeling_object_ids) {
      const std::optional<ObjectId> object = sdk_object(native_id);
      const std::optional<EntityId> entity =
          object_map_.entity_for_modeling_object(native_id);
      if (!object.has_value() || !entity.has_value() ||
          !scene_.exists(*entity)) {
        preview.command = failure("Cut target mesh was not found.");
        preview.kept_meshes.clear();
        preview.discarded_meshes.clear();
        return preview;
      }

      const QMat4 world = scene_.world_transform_matrix(*entity);
      const QMat4 local_from_world = inverse(world);
      const std::array<QVec3, 3> local_points{
          transform_point(local_from_world, world_plane_points[0]),
          transform_point(local_from_world, world_plane_points[1]),
          transform_point(local_from_world, world_plane_points[2]),
      };
      Result<PolygonDocument> copied = session_.document_copy(*object);
      if (!copied.ok()) {
        preview.command = failure(copied.error_message());
        preview.kept_meshes.clear();
        preview.discarded_meshes.clear();
        return preview;
      }
      Result<quader::modeling::SplitPolygonDocuments> split =
          copied.value().split_by_plane({
              .a = to_sdk(local_points[0]),
              .b = to_sdk(local_points[1]),
              .c = to_sdk(local_points[2]),
              .cap = true,
          });
      if (!split.ok()) {
        preview.command = failure(split.error_message());
        preview.kept_meshes.clear();
        preview.discarded_meshes.clear();
        return preview;
      }

      const bool keep_front = mode != ModelingCutMode::DiscardLeft;
      const PolygonDocument &kept =
          keep_front ? split.value().front : split.value().back;
      const PolygonDocument &discarded =
          keep_front ? split.value().back : split.value().front;
      std::optional<MeshAsset> kept_mesh =
          MeshProjectionService::compile_sdk_document_mesh(kept);
      std::optional<MeshAsset> discarded_mesh =
          MeshProjectionService::compile_sdk_document_mesh(discarded);
      stats_.mesh_compile_count += 2U;
      if (!kept_mesh.has_value() || !discarded_mesh.has_value() ||
          kept_mesh->geometry().vertices.empty() ||
          kept_mesh->geometry().indices.empty() ||
          discarded_mesh->geometry().vertices.empty() ||
          discarded_mesh->geometry().indices.empty()) {
        preview.command = failure("Cut preview failed.");
        preview.kept_meshes.clear();
        preview.discarded_meshes.clear();
        return preview;
      }

      const MaterialComponent *material = scene_.material(*entity);
      const MaterialComponent material_component =
          material != nullptr ? *material : MaterialComponent{};
      kept_mesh->name = "Cut Preview Keep";
      discarded_mesh->name = "Cut Preview Discard";
      preview.kept_meshes.push_back({
          .source_entity = *entity,
          .material = material_component,
          .mesh = std::move(*kept_mesh),
          .transform = world,
      });
      preview.discarded_meshes.push_back({
          .source_entity = *entity,
          .material = material_component,
          .mesh = std::move(*discarded_mesh),
          .transform = world,
      });
      preview.split_count += split.value().split_count;
      preview.removed_count +=
          mode == ModelingCutMode::KeepBoth
              ? split.value().removed_count
              : std::max(1, split.value().removed_count);
    }

    preview.command = {.ok = true, .changed = false};
    return preview;
  }

  [[nodiscard]] ModelingSdkMirrorPreviewResult preview_mirror(
      std::span<const int> modeling_object_ids,
      const std::array<QVec3, 3> &world_plane_points) const {
    ModelingSdkMirrorPreviewResult preview;
    preview.target_count = static_cast<int>(modeling_object_ids.size());
    if (modeling_object_ids.empty()) {
      preview.command = failure("Mirror needs selected mesh objects.");
      return preview;
    }

    for (const int native_id : modeling_object_ids) {
      const std::optional<ObjectId> object = sdk_object(native_id);
      const std::optional<EntityId> entity =
          object_map_.entity_for_modeling_object(native_id);
      if (!object.has_value() || !entity.has_value() ||
          !scene_.exists(*entity)) {
        preview.command = failure("Mirror target mesh was not found.");
        preview.meshes.clear();
        return preview;
      }

      const std::optional<QMat4> world =
          modeling_object_world_transform_resolver_
              ? modeling_object_world_transform_resolver_(native_id)
              : std::optional<QMat4>{scene_.world_transform_matrix(*entity)};
      const std::optional<QMat4> local_from_world =
          world.has_value() ? try_scene_matrix_inverse(*world) : std::nullopt;
      if (!world.has_value() || !local_from_world.has_value()) {
        preview.command =
            failure("Mirror target mesh has a non-invertible world transform.");
        preview.meshes.clear();
        return preview;
      }
      const QVec3 first =
          transform_point(*local_from_world, world_plane_points[0]);
      const QVec3 second =
          transform_point(*local_from_world, world_plane_points[1]);
      const QVec3 third =
          transform_point(*local_from_world, world_plane_points[2]);
      const QVec3 normal = cross(second - first, third - first);
      if (!finite(first) || !finite(normal) ||
          length_squared(normal) <= 0.0000001F) {
        preview.command = failure("Mirror plane is degenerate.");
        preview.meshes.clear();
        return preview;
      }

      Result<PolygonDocument> copied = session_.document_copy(*object);
      if (!copied.ok()) {
        preview.command = failure(copied.error_message());
        preview.meshes.clear();
        return preview;
      }
      Result<OperationResult> reflected = copied.value().reflect_across_plane(
          to_sdk(first), to_sdk(normal));
      if (!reflected.ok()) {
        preview.command = failure(reflected.error_message());
        preview.meshes.clear();
        return preview;
      }
      std::optional<MeshAsset> mesh =
          MeshProjectionService::compile_sdk_document_mesh(copied.value());
      ++stats_.mesh_compile_count;
      if (!mesh.has_value() || mesh->geometry().vertices.empty() ||
          mesh->geometry().indices.empty()) {
        preview.command = failure("Mirror preview failed.");
        preview.meshes.clear();
        return preview;
      }
      const MaterialComponent *material = scene_.material(*entity);
      const MeshComponent *source_mesh = scene_.mesh(*entity);
      mesh->name = "Mirror Preview";
      preview.meshes.push_back({
          .source_entity = *entity,
          .material = material != nullptr ? *material : MaterialComponent{},
          .mesh = std::move(*mesh),
          .transform = *world,
          .double_sided = source_mesh != nullptr && source_mesh->double_sided,
      });
    }

    preview.command = {.ok = true, .changed = false};
    return preview;
  }

  [[nodiscard]] quader_editor_core::EditorSession
  build_native_selection_projection() const {
    QDR_PROFILE_SCOPE("Modeling SDK rebuild native selection projection");
    std::vector<quader_editor_core::PolygonObject> objects;
    objects.reserve(object_ids_.size());
    const std::vector<quader::modeling::SessionObjectSummary> summaries =
        session_.objects();
    for (const auto &[native_id, object_id] : object_ids_) {
      const std::optional<EntityId> entity =
          object_map_.entity_for_modeling_object(native_id);
      const MeshComponent *binding =
          entity.has_value() ? scene_.mesh(*entity) : nullptr;
      const MeshAsset *mesh = binding != nullptr
                                  ? assets_.mesh(binding->mesh)
                                  : nullptr;
      if (!entity.has_value() || !scene_.exists(*entity) || mesh == nullptr ||
          !mesh->has_authored_polygon_document ||
          !mesh->authored_polygon_document) {
        continue;
      }

      quader_editor_core::PolygonObject object;
      object.id = native_id;
      if (const ModelingObjectComponent *component =
              scene_.modeling_object(*entity)) {
        object.material_id = static_cast<int>(component->material_slot);
      }
      const auto summary = std::find_if(
          summaries.begin(), summaries.end(),
          [object_id](const auto &candidate) { return candidate.id == object_id; });
      object.selected = summary != summaries.end() && summary->selected;
      object.document.share(mesh->authored_polygon_document);

      Result<PolygonDocument> document = session_.document_copy(object_id);
      if (document.ok()) {
        const PolygonSelectionSnapshot selected = document.value().selection();
        switch (selected.kind) {
        case SelectionKind::Vertex:
          object.selection.mode = quader_poly::SelectionMode::Vertex;
          break;
        case SelectionKind::Edge:
          object.selection.mode = quader_poly::SelectionMode::Edge;
          break;
        case SelectionKind::Face:
          object.selection.mode = quader_poly::SelectionMode::Face;
          break;
        case SelectionKind::Object:
          object.selection.mode = quader_poly::SelectionMode::Vertex;
          break;
        }
        for (const VertexId id : selected.vertices) {
          object.selection.vertices.push_back(id.index);
        }
        for (const EdgeKey edge : selected.edges) {
          object.selection.edges.push_back(
              quader_poly::make_edge(edge.a.index, edge.b.index));
        }
        for (const FaceId id : selected.faces) {
          object.selection.faces.push_back(id.index);
        }
        object.selection.has_active = selected.has_active;
        object.selection.active_vertex = selected.active_vertex.index;
        object.selection.active_edge = quader_poly::make_edge(
            selected.active_edge.a.index, selected.active_edge.b.index);
        object.selection.active_face = selected.active_face.index;
        if (selected.kind == SelectionKind::Edge) {
          object.selection.active_kind = quader_poly::ElementKind::Edge;
        } else if (selected.kind == SelectionKind::Face) {
          object.selection.active_kind = quader_poly::ElementKind::Face;
        } else {
          object.selection.active_kind = quader_poly::ElementKind::Vertex;
        }
      }
      objects.push_back(std::move(object));
    }

    quader_editor_core::EditorSession projected(std::move(objects));
    projected.state().content_revision = content_revision();
    projected.state().selection_revision = selection_revision();
    projected.state().active_object_id = active_native_object_id_;
    switch (selection_mode_) {
    case ModelingSelectionMode::Object:
    case ModelingSelectionMode::Group:
      projected.state().selection_mode =
          quader_editor_core::SelectionMode::Object;
      break;
    case ModelingSelectionMode::Vertex:
      projected.state().selection_mode =
          quader_editor_core::SelectionMode::Vertex;
      break;
    case ModelingSelectionMode::Edge:
      projected.state().selection_mode = quader_editor_core::SelectionMode::Edge;
      break;
    case ModelingSelectionMode::Face:
      projected.state().selection_mode = quader_editor_core::SelectionMode::Face;
      break;
    }
    return projected;
  }

  [[nodiscard]] const quader_editor_core::EditorSession &
  native_selection_projection_view() const {
    QDR_PROFILE_FUNCTION();
    const quader::modeling::PreviewTransactionSummary preview =
        session_.preview_summary();
    const NativeSelectionProjectionCacheKey cache_key{
        .persistent_content_revision = persistent_content_revision_,
        .session_content_revision = session_.content_revision(),
        .session_selection_revision = session_.selection_revision(),
        .mode_revision = mode_revision_,
        .scene_mesh_binding_revision = scene_.mesh_binding_revision(),
        .asset_revision = assets_.revision(),
        .object_map_revision = object_map_.revision(),
        .active_native_object_id = active_native_object_id_,
        .mode = selection_mode_,
        .workspace_preview_active = workspace_preview_active_,
        .sdk_preview_active = preview.active,
        .preview_id = preview.id,
        .preview_object_id = preview.object_id,
        .preview_content_revision = preview.content_revision,
    };
    if (!native_selection_projection_cache_key_.has_value() ||
        *native_selection_projection_cache_key_ != cache_key ||
        !native_selection_projection_cache_.has_value()) {
      native_selection_projection_cache_ = build_native_selection_projection();
      native_selection_projection_cache_key_ = cache_key;
    }
    return *native_selection_projection_cache_;
  }

  [[nodiscard]] quader_editor_core::EditorSession
  native_selection_projection() const {
    return native_selection_projection_view();
  }

  [[nodiscard]] std::optional<EntityId> pick_object_entity(
      const ModelingPickSelectRequest &request,
      std::optional<EntityId> competing_native) const {
    quader_editor_core::EditorSession projected = native_selection_projection();
    ModelingSelectionService selection;
    const std::optional<int> object = selection.pick_object_id(
        projected, scene_, object_map_, request);
    if (!object.has_value()) {
      return competing_native;
    }
    return object_map_.entity_for_modeling_object(*object);
  }

  [[nodiscard]] std::vector<EntityId> box_pick_object_entities(
      const ModelingBoxSelectRequest &request) const {
    QDR_PROFILE_SCOPE("Modeling SDK box pick objects");
    quader_editor_core::EditorSession projected = [&]() {
      QDR_PROFILE_SCOPE("Box selection build SDK pick projection");
      return native_selection_projection();
    }();
    ModelingSelectionService selection;
    const std::vector<int> object_ids = [&]() {
      QDR_PROFILE_SCOPE("Box selection test SDK object candidates");
      return selection.box_pick_object_ids(projected, scene_, object_map_,
                                           request);
    }();
    std::vector<EntityId> entities;
    {
      QDR_PROFILE_SCOPE("Box selection map SDK object hits");
      entities.reserve(object_ids.size());
      for (const int object_id : object_ids) {
        const std::optional<EntityId> entity =
            object_map_.entity_for_modeling_object(object_id);
        if (entity.has_value()) {
          entities.push_back(*entity);
        }
      }
    }
    return entities;
  }

  [[nodiscard]] ModelingCreateBoxResult
  create_box(const ModelingCreateBoxRequest &request,
             const CreationFinalizer &finalize) {
    if (!finite(request.bounds.min) || !finite(request.bounds.max)) {
      return {.ok = false,
              .changed = false,
              .message = "Box bounds must be finite."};
    }

    std::optional<TransformComponent> initial_scene_transform;
    if (request.initial_scene_transform.has_value()) {
      initial_scene_transform = scene_transform_from_modeling_transform(
          *request.initial_scene_transform);
      if (!initial_scene_transform.has_value()) {
        return {.ok = false,
                .changed = false,
                .message = "Initial box scene transform is invalid."};
      }
    }

    const std::size_t undo_mark = session_.undo_depth();
    const quader::modeling::ModelingSession session_before = session_;
    const auto object_ids_before = object_ids_;
    const ModelingObjectRuntimeMap object_map_before = object_map_;
    const int next_native_before = next_native_object_id_;
    const int active_before = active_native_object_id_;
    const ModelingSelectionMode mode_before = selection_mode_;
    const std::uint64_t mode_revision_before = mode_revision_;
    const AssetRegistryProjectionCheckpoint assets_before =
        assets_.capture_projection_checkpoint();
    const SceneProjectionCheckpoint scene_before =
        scene_.capture_projection_checkpoint();
    std::vector<EntityId> created_entities;

    const auto rollback = [&](std::string message) {
      session_.restore_snapshot_preserving_identity_high_water(session_before);
      object_ids_ = object_ids_before;
      object_map_ = object_map_before;
      next_native_object_id_ = next_native_before;
      active_native_object_id_ = active_before;
      selection_mode_ = mode_before;
      mode_revision_ = mode_revision_before;
      (void)assets_.restore_projection_checkpoint(assets_before);
      (void)scene_.restore_projection_checkpoint(scene_before,
                                                 created_entities);
      return ModelingCreateBoxResult{
          .ok = false,
          .changed = false,
          .message = std::move(message),
      };
    };

    Result<PolygonDocument> document = PolygonDocument::make_box({
        .min = to_sdk(request.bounds.min),
        .max = to_sdk(request.bounds.max),
    });
    if (!document.ok()) {
      return rollback(document.error_message());
    }
    const std::string name = request.name.empty() ? "Box" : request.name;
    Result<ObjectId> added =
        session_.add_document(std::move(document).value(), name);
    if (!added.ok()) {
      return rollback(added.error_message());
    }
    Result<OperationResult> selected = session_.select_object(added.value());
    if (!selected.ok()) {
      return rollback(selected.error_message());
    }
    if (fails(ModelingSdkProjectionFailurePoint::AfterSdkMutation)) {
      return rollback("Injected failure after SDK box mutation.");
    }

    Result<PolygonDocument> committed = session_.document_copy(added.value());
    if (!committed.ok()) {
      return rollback(committed.error_message());
    }
    ++stats_.mesh_compile_count;
    std::optional<MeshAsset> projected =
        MeshProjectionService::compile_sdk_document_mesh(committed.value());
    if (!projected.has_value()) {
      return rollback("SDK box mesh projection failed.");
    }
    projected->name = name;
    const AssetRegistryValidationResult validation =
        assets_.validate_mesh(*projected);
    if (!validation.ok) {
      return rollback(validation.message.empty()
                          ? "SDK box mesh projection is invalid."
                          : validation.message);
    }
    const AssetId mesh_id =
        mesh_sync_.install_projected_mesh(assets_, std::move(*projected));
    if (mesh_id == kInvalidAssetId) {
      return rollback("SDK box mesh asset could not be installed.");
    }
    ++stats_.asset_publication_count;
    if (fails(ModelingSdkProjectionFailurePoint::AfterAssetInstall)) {
      return rollback("Injected failure after SDK mesh asset publication.");
    }

    const EntityId entity =
        mesh_sync_.create_projected_mesh_entity(scene_, name);
    if (entity == kInvalidEntityId) {
      return rollback("SDK box scene entity could not be created.");
    }
    created_entities.push_back(entity);
    if (fails(ModelingSdkProjectionFailurePoint::AfterEntityCreate)) {
      return rollback("Injected failure after SDK scene entity creation.");
    }

    const EntityId root = scene_.scene_root();
    if (root != kInvalidEntityId && scene_.exists(root) &&
        !mesh_sync_.parent_projected_entity(scene_, entity, root)) {
      return rollback("SDK box scene entity could not be parented.");
    }
    if (initial_scene_transform.has_value() &&
        !mesh_sync_.set_projected_transform(scene_, entity,
                                            *initial_scene_transform)) {
      return rollback("SDK box initial scene transform could not be stored.");
    }
    if (!mesh_sync_.set_projected_texture_mapping(
            scene_, entity,
            {.sampling_from_local = scene_.world_transform_matrix(entity)})) {
      return rollback("SDK box initial world texture mapping could not be stored.");
    }
    const int native_object_id = next_available_native_object_id();
    if (native_object_id == kInvalidModelingObjectId ||
        native_object_id == std::numeric_limits<int>::max()) {
      return rollback("Native modeling object IDs are exhausted.");
    }
    if (!mesh_sync_.bind_projected_object(
            scene_, entity, native_object_id, 0u, true, mesh_id)) {
      return rollback("SDK box scene binding could not be installed.");
    }
    if (request.material.valid()) {
      const MaterialComponent material{.material = request.material};
      if (!mesh_sync_.set_projected_material(scene_, entity, &material)) {
        return rollback("SDK box material binding could not be installed.");
      }
    }
    if (fails(ModelingSdkProjectionFailurePoint::AfterSceneBinding)) {
      return rollback("Injected failure after SDK scene binding publication.");
    }

    object_ids_.emplace(native_object_id, added.value());
    object_map_.bind(native_object_id, entity);
    object_map_.remember_scene_entity(native_object_id, scene_, entity);
    scene_.clear_selection();
    if (!scene_.set_selected(entity, true)) {
      return rollback("SDK box scene selection could not be updated.");
    }
    active_native_object_id_ = native_object_id;
    selection_mode_ = ModelingSelectionMode::Object;
    if (fails(ModelingSdkProjectionFailurePoint::AfterSelection)) {
      return rollback("Injected failure after SDK selection publication.");
    }

    ModelingObjectRecord record{
        .modeling_object_id = native_object_id,
        .entity = entity,
        .mesh = mesh_id,
        .name = name,
    };
    if (finalize && !finalize(record)) {
      return rollback("SDK box host finalization failed.");
    }
    object_map_.remember_scene_entity(native_object_id, scene_, entity);
    if (fails(ModelingSdkProjectionFailurePoint::BeforeCommit)) {
      return rollback("Injected failure before SDK projection commit.");
    }

    next_native_object_id_ = native_object_id + 1;
    session_.squash_undo_since(undo_mark);
    ++persistent_content_revision_;
    ++stats_.scene_publication_count;
    return {
        .ok = true,
        .changed = true,
        .message = "Created box.",
        .object = std::move(record),
    };
  }

  [[nodiscard]] ModelingCreateBoxResult
  create_mesh(const ModelingCreateMeshRequest &request,
              const CreationFinalizer &finalize) {
    if (request.document.vertices.empty() || request.document.faces.empty()) {
      return {.ok = false,
              .changed = false,
              .message = "Document mesh must contain vertices and faces."};
    }

    Result<PolygonDocument> document =
        MeshProjectionService::sdk_document_from_polygon_document(
            request.document);
    if (!document.ok()) {
      return {.ok = false,
              .changed = false,
              .message = document.error_message()};
    }
    return create_document(std::move(document).value(), request.name,
                           request.material_slot, request.material,
                           request.initial_scene_transform,
                           request.world_texture_projection, finalize);
  }

  [[nodiscard]] ModelingCreateBoxResult create_box_from_corners(
      std::span<const QVec3> corners, std::string_view requested_name,
      MaterialReference material, const CreationFinalizer &finalize) {
    if (corners.size() != 8U) {
      return {.ok = false,
              .changed = false,
              .message = "Box needs eight corner points."};
    }
    QVec3 center{};
    for (const QVec3 corner : corners) {
      if (!finite(corner)) {
        return {.ok = false,
                .changed = false,
                .message = "Box corners must be finite."};
      }
      center += corner;
    }
    center = center / static_cast<float>(corners.size());
    const std::optional<CreationFrame> frame = creation_frame_from_xy(
        center, corners[3] - corners[0], corners[4] - corners[0]);
    if (!frame.has_value()) {
      return {.ok = false,
              .changed = false,
              .message = "Box needs finite width, height, and depth."};
    }

    std::vector<Vec3> sdk_corners;
    sdk_corners.reserve(corners.size());
    for (const QVec3 corner : corners) {
      sdk_corners.push_back(to_sdk(frame->local_point(corner)));
    }
    Result<PolygonDocument> document =
        PolygonDocument::make_box_from_corners(sdk_corners);
    if (!document.ok()) {
      return {.ok = false,
              .changed = false,
              .message = document.error_message()};
    }
    return create_document(
        std::move(document).value(),
        requested_name.empty() ? std::string_view{"Box"} : requested_name,
        0U, material, frame->transform, true, finalize);
  }

  [[nodiscard]] ModelingCreateBoxResult
  create_face(std::span<const QVec3> points, QVec3 requested_normal,
              std::string_view requested_name, MaterialReference material,
              const CreationFinalizer &finalize) {
    constexpr float kEpsilonSquared = 0.00000001F;
    std::vector<QVec3> ordered_points;
    ordered_points.reserve(points.size());
    QVec3 polygon_normal{};
    QVec3 center{};
    for (std::size_t index = 0; index < points.size(); ++index) {
      const QVec3 point = points[index];
      if (!finite(point)) {
        return {.ok = false,
                .message = "Poly vertices must be finite."};
      }
      const QVec3 next = points[(index + 1U) % points.size()];
      polygon_normal = polygon_normal + cross(point, next);
      center += point;
      ordered_points.push_back(point);
    }
    if (ordered_points.size() < 3U ||
        length_squared(polygon_normal) <= kEpsilonSquared) {
      return {.ok = false,
              .message = "Poly needs a non-degenerate polygon."};
    }
    if (length_squared(requested_normal) > kEpsilonSquared &&
        dot(polygon_normal, requested_normal) < 0.0F) {
      std::reverse(ordered_points.begin(), ordered_points.end());
      polygon_normal = polygon_normal * -1.0F;
    }
    center = center / static_cast<float>(ordered_points.size());

    QVec3 x_hint{};
    for (std::size_t index = 0; index < ordered_points.size(); ++index) {
      const QVec3 edge =
          ordered_points[(index + 1U) % ordered_points.size()] -
          ordered_points[index];
      if (length_squared(edge) > kCreationFrameEpsilonSquared) {
        x_hint = edge;
        break;
      }
    }
    const std::optional<CreationFrame> frame =
        creation_frame_from_xz(center, x_hint, polygon_normal);
    if (!frame.has_value()) {
      return {.ok = false,
              .message = "Poly needs a non-degenerate local frame."};
    }

    std::vector<Vec3> sdk_points;
    sdk_points.reserve(ordered_points.size());
    for (const QVec3 point : ordered_points) {
      sdk_points.push_back(to_sdk(frame->local_point(point)));
    }
    Result<PolygonDocument> document = PolygonDocument::make_face(sdk_points);
    if (!document.ok()) {
      return {.ok = false,
              .message = document.error_message().empty()
                             ? "Could not create the poly face."
                             : document.error_message()};
    }
    return create_document(std::move(document).value(), requested_name, 0U,
                           material, frame->transform, true, finalize);
  }

  [[nodiscard]] ModelingCreateBoxResult
  create_document(PolygonDocument document, std::string_view requested_name,
                  std::uint32_t material_slot, MaterialReference material,
                  const std::optional<ModelingTransform3> &initial_transform,
                  bool world_texture_projection,
                  const CreationFinalizer &finalize) {

    std::optional<TransformComponent> initial_scene_transform;
    if (initial_transform.has_value()) {
      initial_scene_transform =
          scene_transform_from_modeling_transform(*initial_transform);
      if (!initial_scene_transform.has_value()) {
        return {.ok = false,
                .changed = false,
                .message = "Initial mesh scene transform is invalid."};
      }
    }

    const std::size_t undo_mark = session_.undo_depth();
    const quader::modeling::ModelingSession session_before = session_;
    const auto object_ids_before = object_ids_;
    const ModelingObjectRuntimeMap object_map_before = object_map_;
    const int next_native_before = next_native_object_id_;
    const int active_before = active_native_object_id_;
    const ModelingSelectionMode mode_before = selection_mode_;
    const std::uint64_t mode_revision_before = mode_revision_;
    const AssetRegistryProjectionCheckpoint assets_before =
        assets_.capture_projection_checkpoint();
    const SceneProjectionCheckpoint scene_before =
        scene_.capture_projection_checkpoint();
    std::vector<EntityId> created_entities;

    const auto rollback = [&](std::string message) {
      session_.restore_snapshot_preserving_identity_high_water(session_before);
      object_ids_ = object_ids_before;
      object_map_ = object_map_before;
      next_native_object_id_ = next_native_before;
      active_native_object_id_ = active_before;
      selection_mode_ = mode_before;
      mode_revision_ = mode_revision_before;
      (void)assets_.restore_projection_checkpoint(assets_before);
      (void)scene_.restore_projection_checkpoint(scene_before,
                                                 created_entities);
      return ModelingCreateBoxResult{
          .ok = false,
          .changed = false,
          .message = std::move(message),
      };
    };

    const std::string name =
        requested_name.empty() ? "Mesh" : std::string(requested_name);
    Result<ObjectId> added =
        session_.add_document(std::move(document), name);
    if (!added.ok()) {
      return rollback(added.error_message());
    }
    Result<OperationResult> selected = session_.select_object(added.value());
    if (!selected.ok()) {
      return rollback(selected.error_message());
    }
    if (fails(ModelingSdkProjectionFailurePoint::AfterSdkMutation)) {
      return rollback("Injected failure after SDK mesh mutation.");
    }

    Result<PolygonDocument> committed = session_.document_copy(added.value());
    if (!committed.ok()) {
      return rollback(committed.error_message());
    }
    ++stats_.mesh_compile_count;
    std::optional<MeshAsset> projected =
        MeshProjectionService::compile_sdk_document_mesh(committed.value());
    if (!projected.has_value()) {
      return rollback("SDK mesh projection failed.");
    }
    projected->name = name;
    const AssetRegistryValidationResult validation =
        assets_.validate_mesh(*projected);
    if (!validation.ok) {
      return rollback(validation.message.empty()
                          ? "SDK mesh projection is invalid."
                          : validation.message);
    }
    const AssetId mesh_id =
        mesh_sync_.install_projected_mesh(assets_, std::move(*projected));
    if (mesh_id == kInvalidAssetId) {
      return rollback("SDK mesh asset could not be installed.");
    }
    ++stats_.asset_publication_count;
    if (fails(ModelingSdkProjectionFailurePoint::AfterAssetInstall)) {
      return rollback("Injected failure after SDK mesh asset publication.");
    }

    const EntityId entity =
        mesh_sync_.create_projected_mesh_entity(scene_, name);
    if (entity == kInvalidEntityId) {
      return rollback("SDK mesh scene entity could not be created.");
    }
    created_entities.push_back(entity);
    if (fails(ModelingSdkProjectionFailurePoint::AfterEntityCreate)) {
      return rollback("Injected failure after SDK scene entity creation.");
    }

    const EntityId root = scene_.scene_root();
    if (root != kInvalidEntityId && scene_.exists(root) &&
        !mesh_sync_.parent_projected_entity(scene_, entity, root)) {
      return rollback("SDK mesh scene entity could not be parented.");
    }
    if (initial_scene_transform.has_value() &&
        !mesh_sync_.set_projected_transform(scene_, entity,
                                            *initial_scene_transform)) {
      return rollback("SDK mesh initial scene transform could not be stored.");
    }
    if (world_texture_projection &&
        !mesh_sync_.set_projected_texture_mapping(
            scene_, entity,
            {.sampling_from_local = scene_.world_transform_matrix(entity)})) {
      return rollback("SDK mesh initial world texture mapping could not be stored.");
    }
    const int native_object_id = next_available_native_object_id();
    if (native_object_id == kInvalidModelingObjectId ||
        native_object_id == std::numeric_limits<int>::max()) {
      return rollback("Native modeling object IDs are exhausted.");
    }
    if (!mesh_sync_.bind_projected_object(
            scene_, entity, native_object_id, material_slot, true,
            mesh_id)) {
      return rollback("SDK mesh scene binding could not be installed.");
    }
    if (material.valid()) {
      const MaterialComponent component{.material = material};
      if (!mesh_sync_.set_projected_material(scene_, entity, &component)) {
        return rollback("SDK mesh material binding could not be installed.");
      }
    }
    if (fails(ModelingSdkProjectionFailurePoint::AfterSceneBinding)) {
      return rollback("Injected failure after SDK scene binding publication.");
    }

    object_ids_.emplace(native_object_id, added.value());
    object_map_.bind(native_object_id, entity);
    object_map_.remember_scene_entity(native_object_id, scene_, entity);
    scene_.clear_selection();
    if (!scene_.set_selected(entity, true)) {
      return rollback("SDK mesh scene selection could not be updated.");
    }
    active_native_object_id_ = native_object_id;
    selection_mode_ = ModelingSelectionMode::Object;
    if (fails(ModelingSdkProjectionFailurePoint::AfterSelection)) {
      return rollback("Injected failure after SDK selection publication.");
    }

    ModelingObjectRecord record{
        .modeling_object_id = native_object_id,
        .entity = entity,
        .mesh = mesh_id,
        .name = name,
    };
    if (finalize && !finalize(record)) {
      return rollback("SDK mesh host finalization failed.");
    }
    object_map_.remember_scene_entity(native_object_id, scene_, entity);
    if (fails(ModelingSdkProjectionFailurePoint::BeforeCommit)) {
      return rollback("Injected failure before SDK projection commit.");
    }

    next_native_object_id_ = native_object_id + 1;
    (void)session_.squash_undo_since(undo_mark);
    ++persistent_content_revision_;
    ++stats_.scene_publication_count;
    return {
        .ok = true,
        .changed = true,
        .message = "Created mesh.",
        .object = std::move(record),
    };
  }

  [[nodiscard]] ModelingCommandResult
  replace_mesh_document(int modeling_object_id,
                        const quader_poly::Document &document) {
    const std::optional<ObjectId> object = sdk_object(modeling_object_id);
    if (!object.has_value()) {
      return failure("Modeling object does not exist.");
    }
    Result<PolygonDocument> converted =
        reconciled_sdk_document(*object, document);
    if (!converted.ok()) {
      return failure(converted.error_message());
    }
    PolygonDocument replacement = std::move(converted).value();
    const std::array<ObjectId, 1> objects{*object};
    return mutate_documents(
        objects,
        [replacement = std::move(replacement)](
            PolygonDocument &working) mutable -> Result<OperationResult> {
          working = replacement;
          OperationResult receipt;
          receipt.success = true;
          receipt.changed = true;
          receipt.message = "Replaced mesh document.";
          receipt.dirty.topology = true;
          receipt.dirty.geometry = true;
          receipt.dirty.materials = true;
          receipt.dirty.selection = true;
          receipt.dirty.overlays = true;
          return Result<OperationResult>::success(std::move(receipt));
        },
        "Modeling mesh document could not be replaced.");
  }

  [[nodiscard]] ModelingCommandResult execute_operation(
      quader_editor_core::EditorOperationId operation,
      const quader_editor_core::EditorOperationSettings &settings) {
    using Id = quader_editor_core::EditorOperationId;

    if (operation == Id::DuplicateMeshes) {
      const auto *value =
          std::get_if<quader_editor_core::DuplicateMeshesOperationSettings>(
              &settings);
      const QVec3 offset =
          value != nullptr
              ? quader_editor_core::duplicate_meshes_offset_from_settings(
                    *value)
              : QVec3{};
      return duplicate_selected_objects(offset, {},
                                        "Duplicated selected meshes.", false,
                                        "Mesh");
    }
    if (operation == Id::CombineMeshes) {
      return combine_selected_objects();
    }
    if (operation == Id::ExtractFaces) {
      return extract_selected_faces();
    }
    if (operation == Id::HalfCut) {
      const auto *value =
          std::get_if<quader_editor_core::HalfCutOperationSettings>(&settings);
      return half_cut_selected_objects(
          quader_editor_core::normalize_half_cut_operation_settings(
              value != nullptr
                  ? *value
                  : quader_editor_core::HalfCutOperationSettings{}));
    }
    if (operation == Id::Bridge &&
        selected_component_objects().size() > 1U) {
      const auto *value =
          std::get_if<quader_editor_core::BridgeOperationSettings>(&settings);
      const quader_editor_core::BridgeOperationSettings normalized =
          quader_editor_core::normalize_bridge_operation_settings(
              value != nullptr ? *value
                               : quader_editor_core::BridgeOperationSettings{});
      return bridge_selected_components(normalized.steps);
    }

    const ModelingSelectionMode mode_before = selection_mode_;
    const auto whole_object_faces = [mode_before](
                                        PolygonDocument &document,
                                        const DocumentMutation &operation_fn) {
      return apply_with_whole_object_face_selection(
          mode_before, document, operation_fn);
    };

    DocumentMutation mutation;
    switch (operation) {
    case Id::Bridge: {
      const auto *value =
          std::get_if<quader_editor_core::BridgeOperationSettings>(&settings);
      const auto normalized =
          quader_editor_core::normalize_bridge_operation_settings(
              value != nullptr ? *value
                               : quader_editor_core::BridgeOperationSettings{});
      mutation = [this, normalized](PolygonDocument &document) {
        return selection_mode_ == ModelingSelectionMode::Face
                   ? document.bridge_selected_faces(normalized.steps)
                   : document.bridge_selected_edges(normalized.steps);
      };
      break;
    }
    case Id::EdgeBevel: {
      const auto *value =
          std::get_if<quader_editor_core::EdgeBevelOperationSettings>(
              &settings);
      const auto normalized =
          quader_editor_core::normalize_edge_bevel_operation_settings(
              value != nullptr
                  ? *value
                  : quader_editor_core::EdgeBevelOperationSettings{});
      mutation = [normalized](PolygonDocument &document) {
        return document.bevel_selected_edges({
            .width = static_cast<float>(normalized.width),
            .profile = static_cast<float>(normalized.profile),
            .segments = normalized.segments,
        });
      };
      break;
    }
    case Id::Thicken: {
      const auto *value =
          std::get_if<quader_editor_core::ThickenOperationSettings>(&settings);
      const auto normalized =
          quader_editor_core::normalize_thicken_operation_settings(
              value != nullptr ? *value
                               : quader_editor_core::ThickenOperationSettings{});
      mutation = [normalized](PolygonDocument &document) {
        return document.thicken_selected_faces(
            static_cast<float>(normalized.thickness), normalized.from_center);
      };
      break;
    }
    case Id::SliceQuad: {
      const auto *value =
          std::get_if<quader_editor_core::SliceQuadOperationSettings>(&settings);
      const auto normalized =
          quader_editor_core::normalize_slice_quad_operation_settings(
              value != nullptr ? *value
                               : quader_editor_core::SliceQuadOperationSettings{});
      mutation = [normalized](PolygonDocument &document) {
        return document.slice_selected_quads(normalized.x_slices,
                                             normalized.y_slices);
      };
      break;
    }
    case Id::MergeByDistance: {
      const auto *value =
          std::get_if<quader_editor_core::MergeByDistanceOperationSettings>(
              &settings);
      const auto normalized =
          quader_editor_core::normalize_merge_by_distance_operation_settings(
              value != nullptr
                  ? *value
                  : quader_editor_core::MergeByDistanceOperationSettings{});
      mutation = [normalized](PolygonDocument &document) {
        return document.merge_selected_vertices_by_distance(
            static_cast<float>(normalized.tolerance));
      };
      break;
    }
    case Id::RemoveDoubles:
      mutation = [](PolygonDocument &document) {
        return document.remove_double_vertices();
      };
      break;
    case Id::BevelVertices: {
      const auto *value =
          std::get_if<quader_editor_core::BevelVerticesOperationSettings>(
              &settings);
      const float distance = static_cast<float>(
          quader_editor_core::bevel_vertices_distance_from_settings(
              value != nullptr
                  ? *value
                  : quader_editor_core::BevelVerticesOperationSettings{}));
      mutation = [distance](PolygonDocument &document) {
        return document.bevel_selected_vertices(distance);
      };
      break;
    }
    case Id::ConnectVertices:
      mutation = [](PolygonDocument &document) {
        return document.connect_selected_vertices();
      };
      break;
    case Id::DissolveVertices:
      mutation = [](PolygonDocument &document) {
        return document.dissolve_selected_vertices();
      };
      break;
    case Id::ConnectEdges:
      mutation = [](PolygonDocument &document) {
        return document.connect_selected_edges();
      };
      break;
    case Id::SplitEdges:
      mutation = [](PolygonDocument &document) {
        return document.split_selected_edges();
      };
      break;
    case Id::DissolveEdges:
      mutation = [](PolygonDocument &document) {
        return document.dissolve_selected_edges();
      };
      break;
    case Id::MergeEdges:
      mutation = [](PolygonDocument &document) {
        return document.merge_selected_edges();
      };
      break;
    case Id::CollapseEdges:
      mutation = [](PolygonDocument &document) {
        return document.collapse_selected_edges();
      };
      break;
    case Id::FillHole:
      mutation = [](PolygonDocument &document) {
        return document.fill_hole_from_selected_edges();
      };
      break;
    case Id::CombineFaces:
      mutation = [](PolygonDocument &document) {
        return document.combine_selected_faces();
      };
      break;
    case Id::CollapseFaces:
      mutation = [](PolygonDocument &document) {
        return document.collapse_selected_faces();
      };
      break;
    case Id::RadialAlign:
      mutation = [](PolygonDocument &document) {
        return document.radial_align_selection();
      };
      break;
    case Id::FlipFaceNormals:
      mutation = [whole_object_faces](PolygonDocument &document) {
        return whole_object_faces(
            document, [](PolygonDocument &selected_document) {
              return selected_document.flip_selected_face_normals();
            });
      };
      break;
    case Id::ShadeFacesSmooth:
      mutation = [whole_object_faces](PolygonDocument &document) {
        return whole_object_faces(
            document, [](PolygonDocument &selected_document) {
              return selected_document.shade_selected_faces_smooth();
            });
      };
      break;
    case Id::ShadeFacesFlat:
      mutation = [whole_object_faces](PolygonDocument &document) {
        return whole_object_faces(
            document, [](PolygonDocument &selected_document) {
              return selected_document.shade_selected_faces_flat();
            });
      };
      break;
    case Id::HardenEdgeNormals:
      mutation = [](PolygonDocument &document) {
        return document.harden_selected_edge_normals();
      };
      break;
    case Id::SoftenEdgeNormals:
      mutation = [](PolygonDocument &document) {
        return document.soften_selected_edge_normals();
      };
      break;
    case Id::DetachFaces:
      mutation = [](PolygonDocument &document) {
        return document.detach_selected_faces();
      };
      break;
    case Id::SnapVerticesToActive:
      mutation = [](PolygonDocument &document) {
        return document.snap_selected_vertices_to_active();
      };
      break;
    case Id::SnapEdgesToActive:
      mutation = [](PolygonDocument &document) {
        return document.snap_selected_edges_to_active();
      };
      break;
    case Id::MergeVerticesToActive:
      mutation = [](PolygonDocument &document) {
        return document.merge_selected_vertices_to_active();
      };
      break;
    case Id::MergeVerticesToCenter:
      mutation = [](PolygonDocument &document) {
        return document.merge_selected_vertices_to_center();
      };
      break;
    case Id::RecalculateFaceNormals: {
      const auto *value = std::get_if<
          quader_editor_core::RecalculateFaceNormalsOperationSettings>(
          &settings);
      const bool outside = value == nullptr || value->outside;
      mutation = [whole_object_faces, outside](PolygonDocument &document) {
        return whole_object_faces(
            document, [outside](PolygonDocument &selected_document) {
              return selected_document.recalculate_selected_face_normals(
                  outside);
            });
      };
      break;
    }
    case Id::AssignMaterial: {
      const auto *value =
          std::get_if<quader_editor_core::AssignMaterialOperationSettings>(
              &settings);
      if (value != nullptr && value->explicit_target) {
        return assign_material_slot_to_face(
            value->object_id, static_cast<int>(value->face_id),
            value->material_slot);
      }
      const std::uint32_t slot = value != nullptr ? value->material_slot : 0U;
      return assign_material_slot_to_selection(slot);
    }
    case Id::DeleteSelection:
      mutation = [](PolygonDocument &document) {
        return document.delete_selection();
      };
      break;
    case Id::DuplicateMeshes:
    case Id::CombineMeshes:
    case Id::ExtractFaces:
    case Id::HalfCut:
      break;
    }

    if (!mutation) {
      return failure("SDK operation mapping is incomplete.");
    }
    ModelingCommandResult result =
        mutate_selected_documents(mutation, sdk_operation_id(operation));
    if (result.ok && result.changed && operation == Id::FillHole) {
      selection_mode_ = ModelingSelectionMode::Face;
      ++mode_revision_;
    }
    return result;
  }

  [[nodiscard]] ModelingCommandResult begin_operation_preview(
      quader_editor_core::EditorOperationId operation,
      const quader_editor_core::EditorOperationSettings &settings) {
    if (preview_active()) {
      return failure("An SDK modeling preview is already active.");
    }
    active_preview_deleted_vertex_count_ = 0U;
    if (operation == quader_editor_core::EditorOperationId::HalfCut) {
      const auto *half_cut =
          std::get_if<quader_editor_core::HalfCutOperationSettings>(&settings);
      if (half_cut == nullptr) {
        return failure("Half-Cut preview settings are invalid.");
      }
      workspace_preview_undo_mark_ = session_.undo_depth();
      const ModelingSdkProjectionSnapshot base = capture_snapshot(true);
      preview_base_snapshot_ = base;
      preview_asset_checkpoint_ = assets_.capture_projection_checkpoint();
      preview_scene_checkpoint_ = scene_.capture_projection_checkpoint();
      workspace_preview_active_ = true;
      active_preview_operation_id_ = std::string(sdk_operation_id(operation));
      active_preview_operation_ = operation;
      ModelingCommandResult result = half_cut_selected_objects(*half_cut, true);
      if (!result.ok) {
        const std::string message = result.message;
        (void)restore_snapshot_exact(base);
        return failure(message);
      }
      preview_changed_ = result.changed;
      active_preview_settings_ = settings;
      return result;
    }
    if (operation == quader_editor_core::EditorOperationId::Bridge &&
        selected_component_objects().size() > 1U) {
      const auto *bridge =
          std::get_if<quader_editor_core::BridgeOperationSettings>(&settings);
      if (bridge == nullptr) {
        return failure("Bridge preview settings are invalid.");
      }
      workspace_preview_undo_mark_ = session_.undo_depth();
      const ModelingSdkProjectionSnapshot base = capture_snapshot(true);
      preview_base_snapshot_ = base;
      preview_asset_checkpoint_ = assets_.capture_projection_checkpoint();
      preview_scene_checkpoint_ = scene_.capture_projection_checkpoint();
      workspace_preview_active_ = true;
      active_preview_operation_id_ = std::string(sdk_operation_id(operation));
      active_preview_operation_ = operation;
      const auto normalized =
          quader_editor_core::normalize_bridge_operation_settings(*bridge);
      ModelingCommandResult result =
          bridge_selected_components(normalized.steps, true);
      if (!result.ok) {
        const std::string message = result.message;
        (void)restore_snapshot_exact(base);
        return failure(message);
      }
      preview_changed_ = result.changed;
      active_preview_settings_ = settings;
      return result;
    }
    const DocumentMutation mutation = preview_mutation_for(operation, settings);
    if (!mutation) {
      return failure("SDK operation does not support a document preview.");
    }
    if (selected_component_objects().size() > 1U) {
      workspace_preview_undo_mark_ = session_.undo_depth();
      const ModelingSdkProjectionSnapshot base = capture_snapshot(true);
      preview_base_snapshot_ = base;
      preview_asset_checkpoint_ = assets_.capture_projection_checkpoint();
      preview_scene_checkpoint_ = scene_.capture_projection_checkpoint();
      workspace_preview_active_ = true;
      active_preview_operation_id_ = std::string(sdk_operation_id(operation));
      active_preview_operation_ = operation;
      ModelingCommandResult result = mutate_selected_documents(
          mutation, sdk_operation_id(operation), true);
      if (!result.ok) {
        const std::string message = result.message;
        (void)restore_snapshot_exact(base);
        return failure(message);
      }
      preview_changed_ = result.changed;
      active_preview_settings_ = settings;
      return result;
    }
    const std::optional<ObjectId> object = active_sdk_object();
    if (!object.has_value()) {
      return failure("No active SDK modeling selection.");
    }
    Result<PolygonDocument> base = session_.document_copy(*object);
    if (!base.ok()) {
      return failure(base.error_message());
    }
    PolygonDocument working = std::move(base).value();
    Result<OperationResult> operated = mutation(working);
    if (!operated.ok() || !operated.value().success) {
      return failure(operated.ok() && !operated.value().message.empty()
                         ? operated.value().message
                         : operated.error_message());
    }

    const quader::modeling::ModelingSession session_before = session_;
    preview_base_snapshot_ = capture_snapshot(true);
    preview_asset_checkpoint_ = assets_.capture_projection_checkpoint();
    preview_scene_checkpoint_ = scene_.capture_projection_checkpoint();
    preview_changed_ = operated.value().changed;
    active_preview_deleted_vertex_count_ = operated.value().deleted.vertices.size();
    active_preview_operation_id_ = std::string(sdk_operation_id(operation));
    active_preview_operation_ = operation;
    Result<quader::modeling::PreviewId> begun = session_.begin_document_preview(
        *object, active_preview_operation_id_, std::move(working),
        std::move(operated).value());
    if (!begun.ok() || !project_preview_object()) {
      session_.restore_snapshot_preserving_identity_high_water(session_before);
      if (preview_asset_checkpoint_.has_value()) {
        (void)assets_.restore_projection_checkpoint(*preview_asset_checkpoint_);
      }
      if (preview_scene_checkpoint_.has_value()) {
        (void)scene_.restore_projection_checkpoint(*preview_scene_checkpoint_);
      }
      const std::string message =
          begun.ok() ? "SDK operation preview asset publication failed."
                     : begun.error_message();
      clear_preview_checkpoints();
      return failure(message);
    }
    ++stats_.preview_publication_count;
    active_preview_settings_ = settings;
    return {.ok = true,
            .changed = preview_changed_,
            .message = "SDK operation preview started."};
  }

  [[nodiscard]] ModelingCommandResult update_operation_preview(
      quader_editor_core::EditorOperationId operation,
      const quader_editor_core::EditorOperationSettings &settings) {
    if (active_preview_operation_id_ != sdk_operation_id(operation)) {
      return failure("Active SDK preview belongs to another operation.");
    }
    if (operation == quader_editor_core::EditorOperationId::HalfCut &&
        workspace_preview_active_) {
      const auto *half_cut =
          std::get_if<quader_editor_core::HalfCutOperationSettings>(&settings);
      if (half_cut == nullptr || !preview_base_snapshot_.has_value()) {
        return failure("Half-Cut preview settings are invalid.");
      }
      const ModelingSdkProjectionSnapshot base = *preview_base_snapshot_;
      const std::optional<AssetRegistryProjectionCheckpoint> asset_checkpoint =
          preview_asset_checkpoint_;
      const std::optional<SceneProjectionCheckpoint> scene_checkpoint =
          preview_scene_checkpoint_;
      const std::size_t undo_mark = workspace_preview_undo_mark_;
      ModelingCommandResult restored = restore_snapshot_exact(base);
      if (!restored.ok) {
        return restored;
      }
      preview_base_snapshot_ = base;
      preview_asset_checkpoint_ = asset_checkpoint;
      preview_scene_checkpoint_ = scene_checkpoint;
      workspace_preview_undo_mark_ = undo_mark;
      workspace_preview_active_ = true;
      active_preview_operation_id_ = std::string(sdk_operation_id(operation));
      active_preview_operation_ = operation;
      ModelingCommandResult result = half_cut_selected_objects(*half_cut, true);
      if (!result.ok) {
        const std::string message = result.message;
        (void)restore_snapshot_exact(base);
        return failure(message);
      }
      preview_changed_ = result.changed;
      if (result.ok) {
        active_preview_settings_ = settings;
      }
      return result;
    }
    if (operation == quader_editor_core::EditorOperationId::Bridge &&
        workspace_preview_active_) {
      const auto *bridge =
          std::get_if<quader_editor_core::BridgeOperationSettings>(&settings);
      if (bridge == nullptr || !preview_base_snapshot_.has_value()) {
        return failure("Bridge preview settings are invalid.");
      }
      const ModelingSdkProjectionSnapshot base = *preview_base_snapshot_;
      const std::optional<AssetRegistryProjectionCheckpoint> asset_checkpoint =
          preview_asset_checkpoint_;
      const std::optional<SceneProjectionCheckpoint> scene_checkpoint =
          preview_scene_checkpoint_;
      const std::size_t undo_mark = workspace_preview_undo_mark_;
      const auto world_from_object = workspace_preview_world_from_object_;
      ModelingCommandResult restored = restore_snapshot_exact(base);
      if (!restored.ok) {
        return restored;
      }
      preview_base_snapshot_ = base;
      preview_asset_checkpoint_ = asset_checkpoint;
      preview_scene_checkpoint_ = scene_checkpoint;
      workspace_preview_undo_mark_ = undo_mark;
      workspace_preview_world_from_object_ = world_from_object;
      workspace_preview_active_ = true;
      active_preview_operation_id_ = std::string(sdk_operation_id(operation));
      active_preview_operation_ = operation;
      const auto normalized =
          quader_editor_core::normalize_bridge_operation_settings(*bridge);
      ModelingCommandResult result =
          bridge_selected_components(normalized.steps, true);
      if (!result.ok) {
        const std::string message = result.message;
        (void)restore_snapshot_exact(base);
        return failure(message);
      }
      preview_changed_ = result.changed;
      if (result.ok) {
        active_preview_settings_ = settings;
      }
      return result;
    }
    const DocumentMutation mutation = preview_mutation_for(operation, settings);
    if (!mutation) {
      return failure("SDK operation does not support a document preview.");
    }
    if (workspace_preview_active_) {
      if (!preview_base_snapshot_.has_value()) {
        return failure("SDK workspace preview restore point is unavailable.");
      }
      const ModelingSdkProjectionSnapshot base = *preview_base_snapshot_;
      const std::optional<AssetRegistryProjectionCheckpoint> asset_checkpoint =
          preview_asset_checkpoint_;
      const std::optional<SceneProjectionCheckpoint> scene_checkpoint =
          preview_scene_checkpoint_;
      const std::size_t undo_mark = workspace_preview_undo_mark_;
      ModelingCommandResult restored = restore_snapshot_exact(base);
      if (!restored.ok) {
        return restored;
      }
      preview_base_snapshot_ = base;
      preview_asset_checkpoint_ = asset_checkpoint;
      preview_scene_checkpoint_ = scene_checkpoint;
      workspace_preview_undo_mark_ = undo_mark;
      workspace_preview_active_ = true;
      active_preview_operation_id_ = std::string(sdk_operation_id(operation));
      active_preview_operation_ = operation;
      ModelingCommandResult result = mutate_selected_documents(
          mutation, sdk_operation_id(operation), true);
      if (!result.ok) {
        const std::string message = result.message;
        (void)restore_snapshot_exact(base);
        return failure(message);
      }
      preview_changed_ = result.changed;
      if (result.ok) {
        active_preview_settings_ = settings;
      }
      return result;
    }
    ModelingCommandResult result =
        update_preview_from_base(mutation, "SDK operation preview failed.");
    if (result.ok) {
      active_preview_settings_ = settings;
    }
    return result;
  }

  [[nodiscard]] ModelingCommandResult commit_operation_preview() {
    return commit_translate_preview();
  }

  [[nodiscard]] ModelingCommandResult cancel_operation_preview() {
    return cancel_translate_preview();
  }

  [[nodiscard]] std::string_view active_preview_operation_id() const noexcept {
    return active_preview_operation_id_;
  }

  [[nodiscard]] std::optional<quader_editor_core::EditorOperationId>
  active_preview_operation() const noexcept {
    return active_preview_operation_;
  }

  [[nodiscard]] std::size_t
  active_preview_deleted_vertex_count() const noexcept {
    return active_preview_deleted_vertex_count_;
  }

  [[nodiscard]] ModelingCommandResult
  set_object_selection(std::span<const EntityId> entities, EntityId active,
                       bool publish_scene = true) {
    std::vector<ObjectId> selected;
    selected.reserve(entities.size());
    int active_native = kInvalidModelingObjectId;
    for (const EntityId entity : entities) {
      const std::optional<int> native =
          object_map_.modeling_object_for_entity(entity);
      if (!native.has_value()) {
        continue;
      }
      const std::optional<ObjectId> object = sdk_object(*native);
      if (!object.has_value()) {
        continue;
      }
      selected.push_back(*object);
      if (entity == active) {
        active_native = *native;
      }
    }
    if (selected.empty() && !entities.empty()) {
      return failure("Selection does not contain SDK modeling objects.");
    }
    if (active_native == kInvalidModelingObjectId && !selected.empty()) {
      active_native = native_object(selected.front()).value_or(
          kInvalidModelingObjectId);
    }
    if (active_native != kInvalidModelingObjectId) {
      const std::optional<ObjectId> active_object = sdk_object(active_native);
      const auto found = active_object.has_value()
                             ? std::find(selected.begin(), selected.end(),
                                         *active_object)
                             : selected.end();
      if (found != selected.end() && found != selected.begin()) {
        std::rotate(selected.begin(), found, std::next(found));
      }
    }

    const std::vector<quader::modeling::SessionObjectSummary> current_objects =
        session_.objects();
    std::unordered_set<std::uint64_t> selected_ids;
    selected_ids.reserve(selected.size());
    for (const ObjectId object : selected) {
      selected_ids.insert(sdk_object_key(object));
    }
    const std::size_t current_selected_count = static_cast<std::size_t>(
        std::ranges::count_if(current_objects,
                              [](const auto &object) {
                                return object.selected;
                              }));
    const bool same_selection =
        current_selected_count == selected_ids.size() &&
        std::ranges::all_of(current_objects, [&](const auto &object) {
          return object.selected ==
                 selected_ids.contains(sdk_object_key(object.id));
        });
    if (same_selection && active_native_object_id_ == active_native &&
        selection_mode_ == ModelingSelectionMode::Object) {
      return {.ok = true, .changed = false};
    }

    std::vector<EntityId> projected_entities;
    if (publish_scene) {
      projected_entities.reserve(entities.size());
      for (const EntityId entity : entities) {
        if (!owns_entity(entity)) {
          continue;
        }
        if (!scene_.exists(entity)) {
          return failure("SDK scene selection could not be projected.");
        }
        projected_entities.push_back(entity);
      }
    }
    const EntityId projected_active =
        projected_entities.empty() ? kInvalidEntityId
                                   : projected_entities.back();
    const SceneSelectionState desired_scene_selection =
        make_scene_selection(scene_, projected_entities, projected_active);

    const bool injected_rollback_required =
        fails(ModelingSdkProjectionFailurePoint::AfterSdkMutation) ||
        (publish_scene &&
         fails(ModelingSdkProjectionFailurePoint::SelectionScenePublication));
    const std::optional<quader::modeling::ModelingSession> session_before =
        injected_rollback_required
            ? std::optional<quader::modeling::ModelingSession>{session_}
            : std::nullopt;
    const std::optional<SceneProjectionCheckpoint> scene_before =
        injected_rollback_required
            ? std::optional<SceneProjectionCheckpoint>{
                  scene_.capture_projection_checkpoint()}
            : std::nullopt;
    const auto rollback_injected_failure = [&](std::string message) {
      if (session_before.has_value()) {
        session_.restore_snapshot_preserving_identity_high_water(
            *session_before);
      }
      if (scene_before.has_value()) {
        (void)scene_.restore_projection_checkpoint(*scene_before);
      }
      return failure(std::move(message));
    };
    Result<OperationResult> result =
        session_.apply_object_selection(selected, SelectionEdit::Replace);
    if (!result.ok()) {
      return failure(result.error_message());
    }
    if (fails(ModelingSdkProjectionFailurePoint::AfterSdkMutation)) {
      return rollback_injected_failure(
          "Injected failure after SDK selection mutation.");
    }

    if (publish_scene) {
      if (fails(
              ModelingSdkProjectionFailurePoint::SelectionScenePublication)) {
        return rollback_injected_failure(
            "Injected failure during SDK selection publication.");
      }
      QDR_PROFILE_SCOPE("Object selection atomic scene publication");
      (void)apply_scene_selection(
          scene_, desired_scene_selection,
          SceneSelectionApplicationMode::RuntimeSync);
    }
    active_native_object_id_ = active_native;
    if (selection_mode_ != ModelingSelectionMode::Object) {
      ++mode_revision_;
    }
    selection_mode_ = ModelingSelectionMode::Object;
    return from_sdk(result);
  }

  [[nodiscard]] ModelingCommandResult
  set_selection_mode(ModelingSelectionMode mode) {
    if (mode == selection_mode_) {
      return {.ok = true, .changed = false};
    }
    if (mode == ModelingSelectionMode::Object ||
        mode == ModelingSelectionMode::Group) {
      selection_mode_ = mode;
      ++mode_revision_;
      return {.ok = true, .changed = true};
    }
    if (active_native_object_id_ == kInvalidModelingObjectId) {
      selection_mode_ = mode;
      ++mode_revision_;
      return {.ok = true, .changed = true};
    }
    ModelingComponentSelectionRequest request;
    request.modeling_object_id = active_native_object_id_;
    request.mode = mode;
    request.edit = ModelingSelectionEdit::Add;
    ModelingCommandResult result = set_component_selection(request);
    if (result.ok && !result.changed) {
      result.changed = true;
    }
    return result;
  }

  [[nodiscard]] ModelingCommandResult set_component_selection(
      const ModelingComponentSelectionRequest &request) {
    const std::span<const ModelingComponentSelectionRequest> requests(
        &request, 1U);
    return set_component_selections(requests);
  }

  [[nodiscard]] ModelingCommandResult
  select_mesh_ngons(std::span<const int> modeling_object_ids) {
    if (selection_mode_ != ModelingSelectionMode::Object) {
      return failure("Select N-Gons is available only in Mesh mode.");
    }

    std::vector<ModelingComponentSelectionRequest> requests;
    requests.reserve(modeling_object_ids.size());
    std::unordered_set<int> visited;
    for (const int native_id : modeling_object_ids) {
      if (!visited.insert(native_id).second) {
        continue;
      }
      const std::optional<ObjectId> object = sdk_object(native_id);
      if (!object.has_value()) {
        return failure("SDK modeling object ID is not live.");
      }
      Result<PolygonDocument> copied = session_.document_copy(*object);
      if (!copied.ok()) {
        return failure(copied.error_message());
      }
      Result<AuthoredTopology> topology = copied.value().authored_topology();
      if (!topology.ok()) {
        return failure(topology.error_message());
      }

      ModelingComponentSelectionRequest request;
      request.modeling_object_id = native_id;
      request.mode = ModelingSelectionMode::Face;
      request.edit = ModelingSelectionEdit::Replace;
      for (const auto &face : topology.value().faces) {
        if (face.vertices.size() > 4U) {
          request.faces.push_back(static_cast<int>(face.id.index));
        }
      }
      if (!request.faces.empty()) {
        request.active_kind = ModelingActiveSelectionKind::Face;
        request.active_face = request.faces.back();
      }
      requests.push_back(std::move(request));
    }
    if (requests.empty()) {
      return {.ok = true,
              .changed = false,
              .message = "No directly editable mesh is selected."};
    }
    return set_component_selections(requests);
  }

  [[nodiscard]] ModelingCommandResult set_component_selections(
      std::span<const ModelingComponentSelectionRequest> requests) {
    const PreviewSelectionEditState preview_edit =
        suspend_operation_preview_for_selection_edit();
    if (!preview_edit.ok) {
      return failure(preview_edit.message);
    }
    if (requests.empty()) {
      return finish_operation_preview_selection_edit(
          preview_edit, {.ok = true, .changed = false});
    }

    /// Holds one validated selection replacement until the complete batch can
    /// be committed atomically.
    struct StagedSelectionDocument {
      ObjectId object{};
      std::optional<PolygonDocument> document;
      PolygonSelectionSnapshot selection;
    };

    // Box selection publishes one exact Replace snapshot per object. Let the
    // SDK validate and apply those snapshots once instead of first cloning and
    // selecting every document here, then repeating that same work in the SDK
    // atomic commit.
    bool exact_replace_batch = true;
    std::unordered_set<int> exact_replace_ids;
    exact_replace_ids.reserve(requests.size());
    for (const ModelingComponentSelectionRequest &request : requests) {
      if (request.edit != ModelingSelectionEdit::Replace ||
          !exact_replace_ids.emplace(request.modeling_object_id).second) {
        exact_replace_batch = false;
        break;
      }
    }

    const std::vector<quader::modeling::SessionObjectSummary> session_objects =
        session_.objects();
    std::unordered_map<int, std::size_t> session_index_by_native;
    session_index_by_native.reserve(session_objects.size());
    std::vector<bool> selected_by_index;
    selected_by_index.reserve(session_objects.size());
    for (std::size_t index = 0; index < session_objects.size(); ++index) {
      const std::optional<int> native = native_object(session_objects[index].id);
      if (native.has_value()) {
        session_index_by_native.emplace(*native, index);
      }
      selected_by_index.push_back(session_objects[index].selected);
    }

    std::vector<StagedSelectionDocument> staged_documents;
    staged_documents.reserve(requests.size());
    std::unordered_map<int, std::size_t> staged_index_by_native;
    staged_index_by_native.reserve(requests.size());
    std::vector<ObjectId> staged_objects;
    staged_objects.reserve(requests.size());
    for (const ModelingComponentSelectionRequest &request : requests) {
      const std::optional<ObjectId> object =
          sdk_object(request.modeling_object_id);
      if (!object.has_value() ||
          !session_index_by_native.contains(request.modeling_object_id)) {
        return finish_operation_preview_selection_edit(
            preview_edit, failure("SDK modeling object ID is not live."));
      }
      if (staged_index_by_native.contains(request.modeling_object_id)) {
        continue;
      }
      staged_index_by_native.emplace(request.modeling_object_id,
                                     staged_objects.size());
      staged_objects.push_back(*object);
    }
    if (exact_replace_batch) {
      for (const ObjectId object : staged_objects) {
        staged_documents.push_back({.object = object});
      }
    } else {
      Result<std::vector<PolygonDocument>> staged_copies =
          session_.document_copies(staged_objects);
      if (!staged_copies.ok()) {
        return finish_operation_preview_selection_edit(
            preview_edit, failure(staged_copies.error_message()));
      }
      for (std::size_t index = 0; index < staged_objects.size(); ++index) {
        staged_documents.push_back({
            .object = staged_objects[index],
            .document = std::move(staged_copies.value()[index]),
        });
      }
    }
    std::string result_message;
    for (std::size_t request_index = 0; request_index < requests.size();
         ++request_index) {
      const ModelingComponentSelectionRequest &request =
          requests[request_index];
      const std::optional<ObjectId> object =
          sdk_object(request.modeling_object_id);
      const auto session_index =
          session_index_by_native.find(request.modeling_object_id);
      if (!object.has_value() || session_index == session_index_by_native.end()) {
        return finish_operation_preview_selection_edit(
            preview_edit, failure("SDK modeling object ID is not live."));
      }

      const std::size_t staged_index =
          staged_index_by_native.at(request.modeling_object_id);
      PolygonSelectionSnapshot selection;
      selection.kind = to_sdk_selection_kind(request.mode);
      selection.vertices.reserve(request.vertices.size());
      for (const int id : request.vertices) {
        if (id > 0) {
          selection.vertices.push_back(
              quader::modeling::make_id<quader::modeling::VertexTag>(
                  static_cast<std::uint32_t>(id)));
        }
      }
      selection.edges.reserve(request.edges.size());
      for (const auto [a, b] : request.edges) {
        if (a > 0 && b > 0 && a != b) {
          selection.edges.push_back(quader::modeling::make_edge_key(
              quader::modeling::make_id<quader::modeling::VertexTag>(
                  static_cast<std::uint32_t>(a)),
              quader::modeling::make_id<quader::modeling::VertexTag>(
                  static_cast<std::uint32_t>(b))));
        }
      }
      selection.faces.reserve(request.faces.size());
      for (const int id : request.faces) {
        if (id > 0) {
          selection.faces.push_back(
              quader::modeling::make_id<quader::modeling::FaceTag>(
                  static_cast<std::uint32_t>(id)));
        }
      }
      selection.has_active = false;
      if (request.mode == ModelingSelectionMode::Vertex &&
          request.active_kind == ModelingActiveSelectionKind::Vertex &&
          request.active_vertex > 0) {
        selection.has_active = true;
        selection.active_vertex =
            quader::modeling::make_id<quader::modeling::VertexTag>(
                static_cast<std::uint32_t>(request.active_vertex));
      }
      if (request.mode == ModelingSelectionMode::Edge &&
          request.active_kind == ModelingActiveSelectionKind::Edge &&
          request.active_edge.first > 0 && request.active_edge.second > 0) {
        selection.has_active = true;
        selection.active_edge = quader::modeling::make_edge_key(
            quader::modeling::make_id<quader::modeling::VertexTag>(
                static_cast<std::uint32_t>(request.active_edge.first)),
            quader::modeling::make_id<quader::modeling::VertexTag>(
                static_cast<std::uint32_t>(request.active_edge.second)));
      }
      if (request.mode == ModelingSelectionMode::Face &&
          request.active_kind == ModelingActiveSelectionKind::Face &&
          request.active_face > 0) {
        selection.has_active = true;
        selection.active_face =
            quader::modeling::make_id<quader::modeling::FaceTag>(
                static_cast<std::uint32_t>(request.active_face));
      }

      StagedSelectionDocument &staged = staged_documents[staged_index];
      if (exact_replace_batch) {
        // Exact SDK snapshots require one canonical active component whenever
        // the selection is non-empty. Preserve an explicit caller choice and
        // synthesize only the missing per-object active value used by native
        // multi-object projections.
        if (!selection.has_active && selection.kind == SelectionKind::Vertex &&
            !selection.vertices.empty()) {
          selection.has_active = true;
          selection.active_vertex = selection.vertices.front();
        } else if (!selection.has_active &&
                   selection.kind == SelectionKind::Edge &&
                   !selection.edges.empty()) {
          selection.has_active = true;
          selection.active_edge = selection.edges.front();
        } else if (!selection.has_active &&
                   selection.kind == SelectionKind::Face &&
                   !selection.faces.empty()) {
          selection.has_active = true;
          selection.active_face = selection.faces.front();
        }
        staged.selection = std::move(selection);
      } else {
        PolygonDocument &document = *staged.document;
        std::string mode_message;
        if (request.edit != ModelingSelectionEdit::Replace) {
          Result<OperationResult> mode = document.set_selection_mode(
              to_sdk_component_mode(request.mode));
          if (!mode.ok()) {
            return finish_operation_preview_selection_edit(
                preview_edit, failure(mode.error_message()));
          }
          mode_message = mode.value().message;
        }

        Result<OperationResult> selected =
            document.apply_selection(selection, to_sdk(request.edit));
        if (!selected.ok()) {
          return finish_operation_preview_selection_edit(
              preview_edit, failure(selected.error_message()));
        }
        if (!selected.value().message.empty()) {
          result_message = selected.value().message;
        } else if (!mode_message.empty()) {
          result_message = std::move(mode_message);
        }
        staged.selection = document.selection();
      }

      const PolygonSelectionSnapshot &effective_selection = staged.selection;
      const bool target_has_selection =
          !effective_selection.vertices.empty() ||
          !effective_selection.edges.empty() ||
          !effective_selection.faces.empty();
      const bool target_was_selected =
          selected_by_index[session_index->second];
      if (request_index == 0U &&
          request.edit == ModelingSelectionEdit::Replace) {
        std::fill(selected_by_index.begin(), selected_by_index.end(), false);
      }
      selected_by_index[session_index->second] =
          target_has_selection ||
          (request.edit == ModelingSelectionEdit::Add && target_was_selected);
    }

    const ModelingComponentSelectionRequest &final_request = requests.back();
    const std::size_t final_session_index =
        session_index_by_native.at(final_request.modeling_object_id);
    const ObjectId final_object = session_objects[final_session_index].id;
    std::vector<ObjectId> selected_objects;
    selected_objects.reserve(session_objects.size());
    if (selected_by_index[final_session_index]) {
      selected_objects.push_back(final_object);
    }
    for (std::size_t index = 0; index < session_objects.size(); ++index) {
      if (index != final_session_index && selected_by_index[index]) {
        selected_objects.push_back(session_objects[index].id);
      }
    }
    const ObjectId sdk_active =
        selected_objects.empty() ? ObjectId{} : selected_objects.front();

    std::vector<quader::modeling::SessionComponentSelectionUpdate> updates;
    updates.reserve(staged_documents.size());
    for (const StagedSelectionDocument &staged : staged_documents) {
      updates.push_back({
          .object = staged.object,
          .selection = staged.selection,
      });
    }

    std::vector<EntityId> selected_entities;
    selected_entities.reserve(selected_objects.size());
    EntityId final_entity = kInvalidEntityId;
    for (const ObjectId selected_object : selected_objects) {
      const std::optional<int> native = native_object(selected_object);
      const std::optional<EntityId> entity =
          native.has_value()
              ? object_map_.entity_for_modeling_object(*native)
              : std::nullopt;
      if (!entity.has_value() || !scene_.exists(*entity)) {
        return finish_operation_preview_selection_edit(
            preview_edit,
            failure("SDK component selection scene projection failed."));
      }
      selected_entities.push_back(*entity);
      if (selected_object == final_object) {
        final_entity = *entity;
      }
    }
    const std::optional<EntityId> mapped_final_entity =
        object_map_.entity_for_modeling_object(
            final_request.modeling_object_id);
    if (!mapped_final_entity.has_value() ||
        !scene_.exists(*mapped_final_entity)) {
      return finish_operation_preview_selection_edit(
          preview_edit,
          failure("SDK component selection has no scene projection."));
    }
    const EntityId scene_active =
        final_entity != kInvalidEntityId
            ? final_entity
            : (selected_entities.empty() ? kInvalidEntityId
                                         : selected_entities.back());
    const SceneSelectionState desired_scene_selection =
        make_scene_selection(scene_, selected_entities, scene_active);

    const std::size_t undo_mark = session_.undo_depth();
    const int active_before = active_native_object_id_;
    const ModelingSelectionMode selection_mode_before = selection_mode_;
    const std::uint64_t mode_revision_before = mode_revision_;
    const bool injected_rollback_required =
        fails(ModelingSdkProjectionFailurePoint::AfterSdkMutation) ||
        fails(ModelingSdkProjectionFailurePoint::SelectionScenePublication);
    const std::optional<quader::modeling::ModelingSession> session_before =
        injected_rollback_required
            ? std::optional<quader::modeling::ModelingSession>{session_}
            : std::nullopt;
    const std::optional<SceneProjectionCheckpoint> scene_before =
        injected_rollback_required
            ? std::optional<SceneProjectionCheckpoint>{
                  scene_.capture_projection_checkpoint()}
            : std::nullopt;
    const auto rollback = [&](std::string message) {
      if (session_before.has_value()) {
        session_.restore_snapshot_preserving_identity_high_water(
            *session_before);
      }
      active_native_object_id_ = active_before;
      selection_mode_ = selection_mode_before;
      mode_revision_ = mode_revision_before;
      if (scene_before.has_value()) {
        (void)scene_.restore_projection_checkpoint(*scene_before);
      }
      return failure(std::move(message));
    };

    Result<OperationResult> committed = [&]() {
      QDR_PROFILE_SCOPE("Component selection atomic SDK publication");
      return session_.commit_component_selection_batch(
          updates, selected_objects, sdk_active);
    }();
    if (!committed.ok()) {
      return finish_operation_preview_selection_edit(
          preview_edit, rollback(committed.error_message()));
    }
    if (fails(ModelingSdkProjectionFailurePoint::AfterSdkMutation)) {
      return finish_operation_preview_selection_edit(
          preview_edit,
          rollback("Injected failure after SDK component selection."));
    }
    if (fails(
            ModelingSdkProjectionFailurePoint::SelectionScenePublication)) {
      return finish_operation_preview_selection_edit(
          preview_edit,
          rollback("Injected failure during SDK component selection publication."));
    }
    {
      QDR_PROFILE_SCOPE("Component selection atomic scene publication");
      (void)apply_scene_selection(
          scene_, desired_scene_selection,
          SceneSelectionApplicationMode::RuntimeSync);
    }

    active_native_object_id_ = final_request.modeling_object_id;
    selection_mode_ = final_request.mode;
    session_.squash_undo_since(undo_mark);
    if (!result_message.empty()) {
      committed.value().message = std::move(result_message);
    }
    return finish_operation_preview_selection_edit(preview_edit,
                                                   from_sdk(committed));
  }

  [[nodiscard]] ModelingCommandResult select_all() {
    if (selection_mode_ == ModelingSelectionMode::Object ||
        selection_mode_ == ModelingSelectionMode::Group) {
      std::vector<EntityId> entities;
      entities.reserve(object_ids_.size());
      for (const auto &[native_id, object_id] : object_ids_) {
        (void)object_id;
        const std::optional<EntityId> entity =
            object_map_.entity_for_modeling_object(native_id);
        if (entity.has_value() && scene_.exists(*entity)) {
          entities.push_back(*entity);
        }
      }
      return set_object_selection(
          entities, entities.empty() ? kInvalidEntityId : entities.back());
    }

    std::vector<int> targets;
    targets.reserve(object_ids_.size());
    for (const auto &[native_id, object_id] : object_ids_) {
      Result<PolygonDocument> copied = session_.document_copy(object_id);
      if (!copied.ok()) {
        return failure(copied.error_message());
      }
      const PolygonSelectionSnapshot selection = copied.value().selection();
      if (component_selection_participates_in_mode(selection,
                                                   selection_mode_)) {
        targets.push_back(native_id);
      }
    }
    const auto active =
        std::find(targets.begin(), targets.end(), active_native_object_id_);
    if (active != targets.end()) {
      std::rotate(active, std::next(active), targets.end());
    }
    if (targets.empty()) {
      return {.ok = true, .changed = false, .message = "Selection unchanged."};
    }
    std::vector<ModelingComponentSelectionRequest> requests;
    requests.reserve(targets.size());
    for (const int native_id : targets) {
      const std::optional<ObjectId> object = sdk_object(native_id);
      if (!object.has_value()) {
        return failure("SDK modeling object ID is not live.");
      }
      Result<PolygonDocument> copied = session_.document_copy(*object);
      if (!copied.ok()) {
        return failure(copied.error_message());
      }
      Result<AuthoredTopology> topology = copied.value().authored_topology();
      if (!topology.ok()) {
        return failure(topology.error_message());
      }
      ModelingComponentSelectionRequest request;
      request.modeling_object_id = native_id;
      request.mode = selection_mode_;
      request.edit = ModelingSelectionEdit::Replace;
      if (selection_mode_ == ModelingSelectionMode::Vertex) {
        for (const auto &vertex : topology.value().vertices) {
          request.vertices.push_back(static_cast<int>(vertex.id.index));
        }
        if (!request.vertices.empty()) {
          request.active_kind = ModelingActiveSelectionKind::Vertex;
          request.active_vertex = request.vertices.back();
        }
      } else if (selection_mode_ == ModelingSelectionMode::Edge) {
        for (const EdgeKey edge : copied.value().edge_ids()) {
          request.edges.emplace_back(static_cast<int>(edge.a.index),
                                     static_cast<int>(edge.b.index));
        }
        if (!request.edges.empty()) {
          request.active_kind = ModelingActiveSelectionKind::Edge;
          request.active_edge = request.edges.back();
        }
      } else {
        for (const auto &face : topology.value().faces) {
          request.faces.push_back(static_cast<int>(face.id.index));
        }
        if (!request.faces.empty()) {
          request.active_kind = ModelingActiveSelectionKind::Face;
          request.active_face = request.faces.back();
        }
      }
      requests.push_back(std::move(request));
    }
    return set_component_selections(requests);
  }

  [[nodiscard]] ModelingCommandResult invert_selection() {
    if (selection_mode_ == ModelingSelectionMode::Object ||
        selection_mode_ == ModelingSelectionMode::Group) {
      std::vector<EntityId> inverted;
      EntityId active = kInvalidEntityId;
      const std::vector<quader::modeling::SessionObjectSummary> objects =
          session_.objects();
      for (const auto &[native_id, object_id] : object_ids_) {
        const auto summary = std::find_if(
            objects.begin(), objects.end(),
            [object_id](const auto &candidate) {
              return candidate.id == object_id;
            });
        if (summary == objects.end() || summary->selected) {
          continue;
        }
        const std::optional<EntityId> entity =
            object_map_.entity_for_modeling_object(native_id);
        if (entity.has_value() && scene_.exists(*entity)) {
          inverted.push_back(*entity);
          active = *entity;
        }
      }
      return set_object_selection(inverted, active);
    }

    std::vector<int> targets;
    targets.reserve(object_ids_.size());
    for (const auto &[native_id, object_id] : object_ids_) {
      Result<PolygonDocument> copied = session_.document_copy(object_id);
      if (!copied.ok()) {
        return failure(copied.error_message());
      }
      const PolygonSelectionSnapshot selection = copied.value().selection();
      if (component_selection_participates_in_mode(selection,
                                                   selection_mode_)) {
        targets.push_back(native_id);
      }
    }
    const auto active =
        std::find(targets.begin(), targets.end(), active_native_object_id_);
    if (active != targets.end()) {
      std::rotate(active, std::next(active), targets.end());
    }
    if (targets.empty()) {
      return {.ok = true, .changed = false, .message = "Selection unchanged."};
    }
    std::vector<ModelingComponentSelectionRequest> requests;
    requests.reserve(targets.size());
    for (const int native_id : targets) {
      const std::optional<ObjectId> object = sdk_object(native_id);
      if (!object.has_value()) {
        return failure("SDK modeling object ID is not live.");
      }
      Result<PolygonDocument> copied = session_.document_copy(*object);
      if (!copied.ok()) {
        return failure(copied.error_message());
      }
      Result<AuthoredTopology> topology = copied.value().authored_topology();
      if (!topology.ok()) {
        return failure(topology.error_message());
      }
      const PolygonSelectionSnapshot current = copied.value().selection();
      ModelingComponentSelectionRequest request;
      request.modeling_object_id = native_id;
      request.mode = selection_mode_;
      request.edit = ModelingSelectionEdit::Replace;
      if (selection_mode_ == ModelingSelectionMode::Vertex) {
        for (const auto &vertex : topology.value().vertices) {
          if (std::find(current.vertices.begin(), current.vertices.end(),
                        vertex.id) == current.vertices.end()) {
            request.vertices.push_back(static_cast<int>(vertex.id.index));
          }
        }
        if (!request.vertices.empty()) {
          request.active_kind = ModelingActiveSelectionKind::Vertex;
          request.active_vertex = request.vertices.back();
        }
      } else if (selection_mode_ == ModelingSelectionMode::Edge) {
        for (const EdgeKey edge : copied.value().edge_ids()) {
          if (std::ranges::none_of(
                  current.edges,
                  [edge](const EdgeKey current_edge) {
                    return current_edge == edge;
                  })) {
            request.edges.emplace_back(static_cast<int>(edge.a.index),
                                       static_cast<int>(edge.b.index));
          }
        }
        if (!request.edges.empty()) {
          request.active_kind = ModelingActiveSelectionKind::Edge;
          request.active_edge = request.edges.back();
        }
      } else {
        for (const auto &face : topology.value().faces) {
          if (std::find(current.faces.begin(), current.faces.end(), face.id) ==
              current.faces.end()) {
            request.faces.push_back(static_cast<int>(face.id.index));
          }
        }
        if (!request.faces.empty()) {
          request.active_kind = ModelingActiveSelectionKind::Face;
          request.active_face = request.faces.back();
        }
      }
      requests.push_back(std::move(request));
    }
    return set_component_selections(requests);
  }

  [[nodiscard]] ModelingCommandResult clear_selection() {
    if (selection_mode_ == ModelingSelectionMode::Object ||
        selection_mode_ == ModelingSelectionMode::Group) {
      return set_object_selection({}, kInvalidEntityId);
    }
    std::vector<ModelingComponentSelectionRequest> requests;
    for (const quader::modeling::SessionObjectSummary &object :
         session_.objects()) {
      if (!object.selected) {
        continue;
      }
      const std::optional<int> native = native_object(object.id);
      if (native.has_value()) {
        requests.push_back({
            .modeling_object_id = *native,
            .mode = selection_mode_,
            .edit = ModelingSelectionEdit::Replace,
        });
      }
    }
    if (requests.empty()) {
      return {.ok = true, .changed = false};
    }
    return set_component_selections(requests);
  }

  [[nodiscard]] ModelingCommandResult
  remove_objects(std::span<const int> modeling_object_ids) {
    std::vector<std::pair<int, ObjectId>> removed;
    removed.reserve(modeling_object_ids.size());
    for (const int native_id : modeling_object_ids) {
      const std::optional<ObjectId> object = sdk_object(native_id);
      if (object.has_value()) {
        removed.emplace_back(native_id, *object);
      }
    }
    if (removed.empty()) {
      return {.ok = true, .changed = false};
    }

    const quader::modeling::ModelingSession before = session_;
    const auto object_ids_before = object_ids_;
    const ModelingObjectRuntimeMap object_map_before = object_map_;
    const int active_before = active_native_object_id_;
    const ModelingSelectionMode mode_before = selection_mode_;
    for (const auto &[native_id, object_id] : removed) {
      Result<OperationResult> result = session_.remove_object(object_id);
      if (!result.ok()) {
        session_.restore_snapshot_preserving_identity_high_water(before);
        object_ids_ = object_ids_before;
        object_map_ = object_map_before;
        active_native_object_id_ = active_before;
        selection_mode_ = mode_before;
        return failure(result.error_message());
      }
      object_ids_.erase(native_id);
      object_map_.unbind_object(native_id);
      if (active_native_object_id_ == native_id) {
        active_native_object_id_ = kInvalidModelingObjectId;
      }
    }
    if (object_ids_.empty()) {
      selection_mode_ = ModelingSelectionMode::Object;
    }
    ++persistent_content_revision_;
    clear_preview_checkpoints();
    return {.ok = true, .changed = true, .message = "Deleted SDK objects."};
  }

  [[nodiscard]] ModelingSelectionSummary apply_native_selection_projection(
      const quader_editor_core::EditorSession &projected) {
    const ModelingSelectionMode projected_mode =
        from_native_selection_mode(projected.selection_mode());
    if (projected_mode == ModelingSelectionMode::Object) {
      std::vector<EntityId> selected;
      EntityId active = kInvalidEntityId;
      for (const quader_editor_core::PolygonObject &object :
           projected.objects()) {
        if (!owns_object(object.id) || !object.selected) {
          continue;
        }
        const std::optional<EntityId> entity =
            object_map_.entity_for_modeling_object(object.id);
        if (!entity.has_value() || !scene_.exists(*entity)) {
          continue;
        }
        selected.push_back(*entity);
        if (object.id == projected.active_object_id()) {
          active = *entity;
        }
      }
      (void)set_object_selection(selected, active);
      return selection_summary();
    }

    std::vector<const quader_editor_core::PolygonObject *> desired_objects;
    std::unordered_set<int> desired_ids;
    for (const quader_editor_core::PolygonObject &object :
         projected.objects()) {
      if (!owns_object(object.id) || object.selection.empty()) {
        continue;
      }
      desired_objects.push_back(&object);
      desired_ids.insert(object.id);
    }
    const auto active = std::find_if(
        desired_objects.begin(), desired_objects.end(),
        [&](const quader_editor_core::PolygonObject *object) {
          return object->id == projected.active_object_id();
        });
    if (active != desired_objects.end()) {
      std::rotate(active, std::next(active), desired_objects.end());
    }

    std::vector<ModelingComponentSelectionRequest> requests;
    for (const quader::modeling::SessionObjectSummary &summary :
         session_.objects()) {
      const std::optional<int> native = native_object(summary.id);
      if (!summary.selected || !native.has_value() ||
          desired_ids.contains(*native)) {
        continue;
      }
      requests.push_back({
          .modeling_object_id = *native,
          .mode = projected_mode,
          .edit = ModelingSelectionEdit::Replace,
      });
    }

    const auto append_request = [&](
                                    const quader_editor_core::PolygonObject
                                        &object) {
      ModelingComponentSelectionRequest request;
      request.modeling_object_id = object.id;
      request.mode = projected_mode;
      request.edit = ModelingSelectionEdit::Replace;
      for (const quader_poly::ElementId vertex : object.selection.vertices) {
        request.vertices.push_back(static_cast<int>(vertex));
      }
      for (const quader_poly::Edge edge : object.selection.edges) {
        request.edges.emplace_back(static_cast<int>(edge.a),
                                   static_cast<int>(edge.b));
      }
      for (const quader_poly::ElementId face : object.selection.faces) {
        request.faces.push_back(static_cast<int>(face));
      }
      if (object.selection.has_active) {
        switch (object.selection.active_kind) {
        case quader_poly::ElementKind::Vertex:
          request.active_kind = ModelingActiveSelectionKind::Vertex;
          request.active_vertex =
              static_cast<int>(object.selection.active_vertex);
          break;
        case quader_poly::ElementKind::Edge:
          request.active_kind = ModelingActiveSelectionKind::Edge;
          request.active_edge = {
              static_cast<int>(object.selection.active_edge.a),
              static_cast<int>(object.selection.active_edge.b),
          };
          break;
        case quader_poly::ElementKind::Face:
          request.active_kind = ModelingActiveSelectionKind::Face;
          request.active_face = static_cast<int>(object.selection.active_face);
          break;
        }
      }
      requests.push_back(std::move(request));
    };
    for (const quader_editor_core::PolygonObject *object : desired_objects) {
      append_request(*object);
    }
    if (!requests.empty()) {
      (void)set_component_selections(requests);
    }
    return selection_summary();
  }

  [[nodiscard]] ModelingSelectionSummary
  pick_select(const ModelingPickSelectRequest &request,
              ModelingSelectionService &selection) {
    const PreviewSelectionEditState preview_edit =
        suspend_operation_preview_for_selection_edit();
    if (!preview_edit.ok) {
      return selection_summary();
    }
    quader_editor_core::EditorSession projected = native_selection_projection();
    ModelingPickSelectRequest effective_request = request;
    if (preview_edit.active) {
      effective_request.preserve_selection_on_miss = true;
    }
    const ModelingSelectionApplyResult applied = selection.apply_pick_selection(
        projected, scene_, object_map_, effective_request);
    if (applied.applied && applied.changed) {
      (void)apply_native_selection_projection(projected);
    }
    (void)finish_operation_preview_selection_edit(
        preview_edit,
        {.ok = true, .changed = applied.applied && applied.changed});
    return selection_summary();
  }

  [[nodiscard]] ModelingSelectionSummary
  box_select(const ModelingBoxSelectRequest &request,
             ModelingSelectionService &selection) {
    QDR_PROFILE_SCOPE("Modeling SDK box selection");
    const PreviewSelectionEditState preview_edit = [&]() {
      QDR_PROFILE_SCOPE("Box selection suspend operation preview");
      return suspend_operation_preview_for_selection_edit();
    }();
    if (!preview_edit.ok) {
      return selection_summary();
    }
    quader_editor_core::EditorSession projected = [&]() {
      QDR_PROFILE_SCOPE("Box selection build SDK selection projection");
      return native_selection_projection();
    }();
    const quader_editor_core::RectangleSelectionState baseline = [&]() {
      QDR_PROFILE_SCOPE("Box selection capture SDK runtime baseline");
      return selection.rectangle_selection_state(projected);
    }();
    return box_select(request, baseline, selection, projected, preview_edit);
  }

  [[nodiscard]] ModelingSelectionSummary box_select(
      const ModelingBoxSelectRequest &request,
      const quader_editor_core::RectangleSelectionState &baseline,
      ModelingSelectionService &selection) {
    QDR_PROFILE_SCOPE("Modeling SDK box selection from baseline");
    const PreviewSelectionEditState preview_edit = [&]() {
      QDR_PROFILE_SCOPE("Box selection suspend operation preview");
      return suspend_operation_preview_for_selection_edit();
    }();
    if (!preview_edit.ok) {
      return selection_summary();
    }
    quader_editor_core::EditorSession projected = [&]() {
      QDR_PROFILE_SCOPE("Box selection build SDK selection projection");
      return native_selection_projection();
    }();
    const quader_editor_core::RectangleSelectionState effective_baseline =
        [&]() {
          QDR_PROFILE_SCOPE("Box selection resolve SDK baseline");
          return preview_edit.active
                     ? selection.rectangle_selection_state(projected)
                     : baseline;
        }();
    return box_select(request, effective_baseline, selection, projected,
                      preview_edit);
  }

  [[nodiscard]] ModelingSelectionSummary box_select(
      const ModelingBoxSelectRequest &request,
      const quader_editor_core::RectangleSelectionState &baseline,
      ModelingSelectionService &selection,
      quader_editor_core::EditorSession &projected,
      const PreviewSelectionEditState &preview_edit) {
    QDR_PROFILE_SCOPE("Modeling SDK apply box selection");
    const ModelingSelectionApplyResult applied = [&]() {
      QDR_PROFILE_SCOPE("Box selection resolve SDK rectangle selection");
      return selection.apply_rectangle_selection(projected, scene_,
                                                  object_map_, request,
                                                  baseline);
    }();
    if (applied.changed) {
      QDR_PROFILE_SCOPE("Box selection publish SDK native selection");
      (void)apply_native_selection_projection(projected);
    }
    {
      QDR_PROFILE_SCOPE("Box selection finish operation preview edit");
      (void)finish_operation_preview_selection_edit(
          preview_edit, {.ok = true, .changed = applied.changed});
    }
    {
      QDR_PROFILE_SCOPE("Box selection summarize SDK selection");
      return selection_summary();
    }
  }

  [[nodiscard]] ModelingCommandResult move_selection(QVec3 delta) {
    if (!finite(delta)) {
      return failure("Translation delta must be finite.");
    }
    ModelingTransform3 transform;
    transform.origin = delta;
    return transform_selection(transform, {});
  }

  [[nodiscard]] ModelingCommandResult
  transform_selection(const ModelingTransform3 &transform, QVec3 pivot) {
    const quader::modeling::Transform3 sdk_transform = to_sdk(transform, pivot);
    const ModelingSelectionMode mode = selection_mode_;
    return mutate_selected_documents(
        [sdk_transform, mode](PolygonDocument &document) {
          if (mode != ModelingSelectionMode::Object &&
              mode != ModelingSelectionMode::Group) {
            return document.transform_selection(sdk_transform);
          }
          Result<OperationResult> selected = document.select_all_faces();
          if (!selected.ok()) {
            return selected;
          }
          Result<OperationResult> transformed =
              document.transform_selection(sdk_transform);
          if (!transformed.ok()) {
            return transformed;
          }
          Result<OperationResult> cleared = document.clear_selection();
          if (!cleared.ok()) {
            return cleared;
          }
          transformed.value().dirty.selection = false;
          return transformed;
        },
        "Transform selection failed.");
  }

  [[nodiscard]] ModelingCommandResult
  assign_material_slot_to_selection(std::uint32_t material_slot) {
    const ModelingSelectionMode mode = selection_mode_;
    return mutate_selected_documents(
        [material_slot, mode](PolygonDocument &document) {
          if (mode != ModelingSelectionMode::Object &&
              mode != ModelingSelectionMode::Group) {
            return document.assign_selected_face_material_slot(material_slot);
          }
          Result<OperationResult> selected = document.select_all_faces();
          if (!selected.ok()) {
            return selected;
          }
          Result<OperationResult> assigned =
              document.assign_selected_face_material_slot(material_slot);
          if (!assigned.ok()) {
            return assigned;
          }
          Result<OperationResult> cleared = document.clear_selection();
          if (!cleared.ok()) {
            return cleared;
          }
          assigned.value().dirty.selection = false;
          return assigned;
        },
        "Assign material slot failed.");
  }

  [[nodiscard]] ModelingSelectedFaceTextureSnapshot
  selected_face_texture_settings() const {
    ++stats_.face_texture_summary_build_count;
    ModelingSelectedFaceTextureSnapshot result;
    if (selection_mode_ != ModelingSelectionMode::Face) {
      return result;
    }
    const std::vector<ObjectId> selected = selected_sdk_objects();
    const Result<std::vector<PolygonDocument>> documents =
        session_.document_copies(selected);
    if (!documents.ok()) {
      result.command = failure(documents.error_message());
      return result;
    }
    for (std::size_t index = 0; index < selected.size(); ++index) {
      const auto &document = documents.value()[index];
      const PolygonSelectionSnapshot selection = document.selection();
      if (selection.faces.empty()) {
        continue;
      }
      const auto native = native_object(selected[index]);
      const auto entity = native.has_value()
                              ? object_map_.entity_for_modeling_object(*native)
                              : std::nullopt;
      if (!native.has_value() || !entity.has_value() || !scene_.exists(*entity)) {
        result.command = failure("A selected face mesh is no longer live.");
        return result;
      }
      auto properties = document.face_texture_settings(selection.faces);
      if (!properties.ok()) {
        result.command = failure(properties.error_message());
        return result;
      }
      std::ranges::sort(properties.value(), {},
                        &quader::modeling::FaceTextureSettings::face);
      result.objects.push_back({
          .modeling_object_id = *native,
          .entity = *entity,
          .faces = std::move(properties).value(),
      });
    }
    std::ranges::sort(result.objects, {},
                      &ModelingSelectedFaceTextureObject::modeling_object_id);
    return result;
  }

  [[nodiscard]] ModelingCommandResult apply_face_texture_updates(
      std::span<const ModelingFaceTextureUpdateBatch> batches) {
    if (selection_mode_ != ModelingSelectionMode::Face) {
      return failure("Texture settings require face selection mode.");
    }
    std::vector<ObjectId> objects;
    objects.reserve(batches.size());
    for (const auto &batch : batches) {
      const auto object = sdk_object(batch.modeling_object_id);
      if (!object.has_value() || batch.updates.empty() ||
          std::ranges::find(objects, *object) != objects.end()) {
        return failure("The selected texture targets are no longer valid.");
      }
      objects.push_back(*object);
    }
    return mutate_documents_by_object(
        objects,
        [&objects, batches](ObjectId object, PolygonDocument &document) {
          const std::size_t index = static_cast<std::size_t>(
              std::ranges::find(objects, object) - objects.begin());
          return document.set_face_texture_transforms(batches[index].updates);
        },
        "Edit face texture settings failed.", false, false, false, true);
  }

  template <typename PreparedDocument>
  [[nodiscard]] ModelingCommandResult publish_prepared_documents(
      std::span<const ObjectId> objects,
      const std::vector<PreparedDocument> &prepared_documents,
      std::string_view fallback_message) {
    return mutate_documents_by_object(
        objects,
        [&](ObjectId object, PolygonDocument &document) {
          const auto native = native_object(object);
          const auto entry = std::ranges::find(
              prepared_documents,
              static_cast<std::uint64_t>(native.value_or(-1)),
              &PreparedDocument::key);
          if (entry == prepared_documents.end()) {
            return Result<OperationResult>::failure(modeling::make_error(
                modeling::ErrorCode::InvalidId,
                "A prepared UV target is no longer live."));
          }
          document = entry->document;
          return Result<OperationResult>::success(entry->receipt);
        },
        fallback_message, false, false, false, true);
  }

  [[nodiscard]] ModelingCommandResult apply_face_texture_action(
      std::span<const ModelingFaceTextureActionTarget> targets,
      const modeling::FaceTextureActionOptions &options) {
    if (selection_mode_ != ModelingSelectionMode::Face) {
      return failure("Texture actions require face selection mode.");
    }
    if (targets.empty()) {
      return failure("Select one or more editable faces first.");
    }
    std::vector<ObjectId> objects;
    objects.reserve(targets.size());
    for (const auto &target : targets) {
      const auto object = sdk_object(target.modeling_object_id);
      if (!object.has_value() || target.faces.empty() ||
          std::ranges::find(objects, *object) != objects.end()) {
        return failure("The selected texture targets are no longer valid.");
      }
      objects.push_back(*object);
    }
    // These COW copies remain stable throughout the one selection-wide SDK
    // preparation. No SDK/native scene state is published until every target
    // has produced its validated replacement and receipt.
    auto documents = session_.document_copies(objects);
    if (!documents.ok()) {
      return failure(documents.error_message());
    }
    std::vector<modeling::FaceTextureActionTarget> portable_targets;
    portable_targets.reserve(targets.size());
    for (std::size_t index = 0; index < targets.size(); ++index) {
      const auto &target = targets[index];
      portable_targets.push_back({
          .key = static_cast<std::uint64_t>(target.modeling_object_id),
          .document = &documents.value()[index],
          .faces = target.faces,
          .world_from_local = to_sdk(target.world_from_local),
          .sampling_from_local = to_sdk(target.sampling_from_local),
      });
    }
    auto prepared = modeling::prepare_face_texture_action(portable_targets, options);
    if (!prepared.ok()) {
      return failure(prepared.error_message());
    }
    if (!prepared.value().changed) {
      return {.ok = true, .changed = false};
    }
    return publish_prepared_documents(objects, prepared.value().documents,
                                      "Edit face texture failed.");
  }

  [[nodiscard]] ModelingCommandResult transfer_face_texture(
      const ModelingFaceTextureTransferTarget &source,
      const ModelingFaceTextureTransferTarget &target,
      std::uint32_t target_material_slot) {
    if (selection_mode_ != ModelingSelectionMode::Face) {
      return failure("Texture transfer requires face selection mode.");
    }
    const auto source_object = sdk_object(source.modeling_object_id);
    const auto target_object = sdk_object(target.modeling_object_id);
    if (!source_object.has_value() || !target_object.has_value() ||
        !source.face.valid() || !target.face.valid()) {
      return failure("A texture transfer face is no longer live.");
    }
    std::vector<ObjectId> snapshot_objects{*source_object};
    if (*target_object != *source_object) {
      snapshot_objects.push_back(*target_object);
    }
    auto documents = session_.document_copies(snapshot_objects);
    if (!documents.ok()) {
      return failure(documents.error_message());
    }
    const modeling::FaceTextureTransferFace source_face{
        .key = static_cast<std::uint64_t>(source.modeling_object_id),
        .document = &documents.value().front(),
        .face = source.face,
        .world_from_local = to_sdk(source.world_from_local),
        .sampling_from_local = to_sdk(source.sampling_from_local),
    };
    const modeling::FaceTextureTransferFace target_face{
        .key = static_cast<std::uint64_t>(target.modeling_object_id),
        .document = &documents.value().back(),
        .face = target.face,
        .world_from_local = to_sdk(target.world_from_local),
        .sampling_from_local = to_sdk(target.sampling_from_local),
    };
    auto prepared = modeling::prepare_face_texture_transfer(
        source_face, target_face, target_material_slot);
    if (!prepared.ok()) {
      return failure(prepared.error_message());
    }
    if (!prepared.value().changed) {
      return {.ok = true, .changed = false};
    }
    const std::array<ObjectId, 1> targets{*target_object};
    return publish_prepared_documents(targets, prepared.value().documents,
                                      "Transfer face texture failed.");
  }

  [[nodiscard]] ModelingSelectedUvPeelSnapshot selected_uv_peel_edges() const {
    ++stats_.uv_peel_summary_build_count;
    ModelingSelectedUvPeelSnapshot result;
    if (selection_mode_ != ModelingSelectionMode::Edge) {
      return result;
    }
    const std::vector<ObjectId> selected = selected_sdk_objects();
    const Result<std::vector<PolygonDocument>> documents =
        session_.document_copies(selected);
    if (!documents.ok()) {
      result.command = failure(documents.error_message());
      return result;
    }
    for (std::size_t index = 0; index < selected.size(); ++index) {
      const PolygonSelectionSnapshot selection = documents.value()[index].selection();
      if (selection.edges.empty()) {
        continue;
      }
      const auto native = native_object(selected[index]);
      const auto entity = native.has_value()
                              ? object_map_.entity_for_modeling_object(*native)
                              : std::nullopt;
      if (!native.has_value() || !entity.has_value() || !scene_.exists(*entity)) {
        result.command = failure("A selected UV Peel mesh is no longer live.");
        return result;
      }
      ModelingSelectedUvPeelObject object{
          .modeling_object_id = *native,
          .entity = *entity,
          .edges = selection.edges,
      };
      std::ranges::sort(object.edges);
      if (active_native_object_id_ == *native) {
        result.active_modeling_object_id = *native;
        if (selection.active_edge.valid()) {
          object.active_edge = selection.active_edge;
        }
      }
      result.objects.push_back(std::move(object));
    }
    std::ranges::sort(result.objects, {},
                      &ModelingSelectedUvPeelObject::modeling_object_id);
    return result;
  }

  [[nodiscard]] ModelingCommandResult apply_uv_peel(
      std::span<const ModelingUvPeelTarget> targets,
      const ModelingUvPeelOptions &options) {
    if (selection_mode_ != ModelingSelectionMode::Edge) {
      return failure("UV Peel requires edge selection mode.");
    }
    if (targets.empty()) {
      return failure("Select one or more edge guides first.");
    }
    std::vector<ObjectId> objects;
    objects.reserve(targets.size());
    for (const auto &target : targets) {
      const auto object = sdk_object(target.modeling_object_id);
      if (!object.has_value() || target.edges.empty() ||
          std::ranges::find(objects, *object) != objects.end()) {
        return failure("The selected UV Peel targets are no longer valid.");
      }
      objects.push_back(*object);
    }
    Result<std::vector<PolygonDocument>> documents =
        session_.document_copies(objects);
    if (!documents.ok()) {
      return failure(documents.error_message());
    }
    std::vector<modeling::UvPeelTarget> portable_targets;
    portable_targets.reserve(targets.size());
    for (std::size_t index = 0; index < targets.size(); ++index) {
      const auto &target = targets[index];
      portable_targets.push_back({
          .key = static_cast<std::uint64_t>(target.modeling_object_id),
          .document = &documents.value()[index],
          .edges = target.edges,
          .active_edge = target.active_edge,
          .world_from_local = to_sdk(target.world_from_local),
          .sampling_from_local = to_sdk(target.sampling_from_local),
      });
    }
    Result<modeling::PreparedUvPeel> prepared =
        modeling::prepare_uv_peel(portable_targets, options);
    if (!prepared.ok()) {
      return failure(prepared.error_message());
    }
    if (!prepared.value().changed) {
      return {.ok = true, .changed = false};
    }
    return publish_prepared_documents(objects, prepared.value().documents,
                                      "UV Peel failed.");
  }

  [[nodiscard]] ModelingCommandResult assign_material_slot_to_objects(
      std::span<const int> modeling_object_ids,
      std::uint32_t material_slot, bool preserve_scene_selection = false) {
    if (modeling_object_ids.empty()) {
      return failure("Select one or more meshes before assigning a material.");
    }
    std::vector<ObjectId> objects;
    objects.reserve(modeling_object_ids.size());
    for (const int modeling_object_id : modeling_object_ids) {
      const std::optional<ObjectId> object = sdk_object(modeling_object_id);
      if (!object.has_value()) {
        return failure("Material target mesh is not live.");
      }
      if (std::ranges::find(objects, *object) == objects.end()) {
        objects.push_back(*object);
      }
    }
    return mutate_documents_by_object(
        objects,
        [material_slot](ObjectId, PolygonDocument &document) {
          return document.assign_all_face_material_slot(material_slot);
        },
        "Assign material slot failed.", false, false, false, true,
        preserve_scene_selection);
  }

  [[nodiscard]] ModelingCommandResult assign_material_slot_to_face(
      int modeling_object_id, int face_id, std::uint32_t material_slot) {
    const std::optional<ObjectId> object = sdk_object(modeling_object_id);
    if (!object.has_value() || face_id <= 0) {
      return failure("Material target face is not live.");
    }
    const std::array<ObjectId, 1> objects{*object};
    return mutate_documents_by_object(
        objects,
        [face_id, material_slot](ObjectId, PolygonDocument &document) {
          return document.assign_face_material_slot(
              quader::modeling::make_id<quader::modeling::FaceTag>(
                  static_cast<std::uint32_t>(face_id)),
              material_slot);
        },
        "Assign material slot failed.", false, false, false, true);
  }

  [[nodiscard]] ModelingCommandResult assign_material_slots_to_faces(
      std::span<const ModelingMaterialSlotAssignment> assignments) {
    if (assignments.empty()) {
      return failure("Select one or more faces before assigning a material.");
    }

    /// Stores one validated native-to-SDK material-slot assignment.
    struct SdkAssignment {
      ObjectId object;
      quader::modeling::FaceId face;
      std::uint32_t material_slot = 0;
    };
    std::vector<SdkAssignment> sdk_assignments;
    std::vector<ObjectId> objects;
    sdk_assignments.reserve(assignments.size());
    objects.reserve(assignments.size());
    for (const ModelingMaterialSlotAssignment &assignment : assignments) {
      const std::optional<ObjectId> object =
          sdk_object(assignment.modeling_object_id);
      if (!object.has_value() || assignment.face_id <= 0) {
        return failure("Material target face is not live.");
      }
      sdk_assignments.push_back({
          .object = *object,
          .face = quader::modeling::make_id<quader::modeling::FaceTag>(
              static_cast<std::uint32_t>(assignment.face_id)),
          .material_slot = assignment.material_slot,
      });
      if (std::ranges::find(objects, *object) == objects.end()) {
        objects.push_back(*object);
      }
    }

    return mutate_documents_by_object(
        objects,
        [sdk_assignments = std::move(sdk_assignments)](
            ObjectId object, PolygonDocument &document) {
          OperationResult combined;
          combined.message = "Material assigned.";
          for (const SdkAssignment &assignment : sdk_assignments) {
            if (assignment.object != object) {
              continue;
            }
            Result<OperationResult> assigned = document.assign_face_material_slot(
                assignment.face, assignment.material_slot);
            if (!assigned.ok()) {
              return assigned;
            }
            if (!assigned.value().success) {
              return assigned;
            }
            combined.changed = combined.changed || assigned.value().changed;
            combined.dirty.materials =
                combined.dirty.materials || assigned.value().dirty.materials;
            combined.dirty.topology =
                combined.dirty.topology || assigned.value().dirty.topology;
            combined.dirty.geometry =
                combined.dirty.geometry || assigned.value().dirty.geometry;
          }
          return Result<OperationResult>::success(std::move(combined));
        },
        "Assign material slot failed.");
  }

  [[nodiscard]] ModelingCommandResult insert_edge_loop(
      int modeling_object_id, int edge_a, int edge_b, float factor) {
    const std::optional<ObjectId> object = sdk_object(modeling_object_id);
    if (!object.has_value() || edge_a <= 0 || edge_b <= 0 ||
        edge_a == edge_b) {
      return failure("Edge loop target is not live.");
    }
    const std::size_t undo_mark = session_.undo_depth();
    const EdgeKey edge = quader::modeling::make_edge_key(
        quader::modeling::make_id<quader::modeling::VertexTag>(
            static_cast<std::uint32_t>(edge_a)),
        quader::modeling::make_id<quader::modeling::VertexTag>(
            static_cast<std::uint32_t>(edge_b)));
    const std::array<ObjectId, 1> objects{*object};
    ModelingCommandResult result = mutate_documents(
        objects,
        [edge, factor](PolygonDocument &document) {
          Result<OperationResult> mode =
              document.set_selection_mode(SelectionMode::Edge);
          if (!mode.ok()) {
            return mode;
          }
          const std::array<EdgeKey, 1> edges{edge};
          Result<OperationResult> selected =
              document.select_edges(edges, SelectionEdit::Replace);
          if (!selected.ok()) {
            return selected;
          }
          return document.insert_edge_loop(factor);
        },
        "Could not insert edge loop.");
    if (!result.ok || !result.changed) {
      return result;
    }
    Result<OperationResult> selected = session_.apply_object_selection(
        objects, SelectionEdit::Replace);
    if (!selected.ok()) {
      return failure(selected.error_message());
    }
    (void)session_.squash_undo_since(undo_mark);
    selection_mode_ = ModelingSelectionMode::Edge;
    active_native_object_id_ = modeling_object_id;
    add_selection_to_scene();
    return result;
  }

  [[nodiscard]] ModelingCommandResult knife_stroke(
      int modeling_object_id,
      std::span<const ModelingSdkKnifeTarget> points,
      std::span<const ModelingSdkKnifeStrokeSegment> segments) {
    const std::optional<ObjectId> object = sdk_object(modeling_object_id);
    if (!object.has_value()) {
      return failure("Knife stroke needs a locked target mesh.");
    }
    if (segments.empty()) {
      return {.ok = true, .changed = false};
    }

    const auto target_kind = [](ModelingSdkKnifeTargetKind kind) {
      switch (kind) {
      case ModelingSdkKnifeTargetKind::ExistingVertex:
        return KnifeTargetKind::ExistingVertex;
      case ModelingSdkKnifeTargetKind::ExistingEdge:
        return KnifeTargetKind::ExistingEdge;
      case ModelingSdkKnifeTargetKind::InsertedVertex:
        return KnifeTargetKind::InsertedVertex;
      case ModelingSdkKnifeTargetKind::FacePoint:
        return KnifeTargetKind::FacePoint;
      }
      return KnifeTargetKind::ExistingVertex;
    };

    std::vector<KnifeTarget> sdk_points;
    sdk_points.reserve(points.size());
    for (const ModelingSdkKnifeTarget &point : points) {
      sdk_points.push_back({
          .kind = target_kind(point.kind),
          .vertex = quader::modeling::make_id<quader::modeling::VertexTag>(
              static_cast<std::uint32_t>(std::max(point.vertex_id, 0))),
          .edge = quader::modeling::make_edge_key(
              quader::modeling::make_id<quader::modeling::VertexTag>(
                  static_cast<std::uint32_t>(std::max(point.edge_a, 0))),
              quader::modeling::make_id<quader::modeling::VertexTag>(
                  static_cast<std::uint32_t>(std::max(point.edge_b, 0)))),
          .edge_factor = point.edge_factor,
          .face = quader::modeling::make_id<quader::modeling::FaceTag>(
              static_cast<std::uint32_t>(std::max(point.face_id, 0))),
          .position = to_sdk(point.position),
      });
    }
    std::vector<KnifeStrokeSegment> sdk_segments;
    sdk_segments.reserve(segments.size());
    for (const ModelingSdkKnifeStrokeSegment segment : segments) {
      sdk_segments.push_back({
          .first_point = segment.first_point,
          .second_point = segment.second_point,
      });
    }

    const std::size_t undo_mark = session_.undo_depth();
    const ModelingSelectionMode selection_mode_before = selection_mode_;
    const std::array<ObjectId, 1> objects{*object};
    ModelingCommandResult result = mutate_documents(
        objects,
        [sdk_points = std::move(sdk_points),
         sdk_segments = std::move(sdk_segments),
         selection_mode_before](PolygonDocument &document) {
          Result<OperationResult> cut =
              document.knife_stroke(sdk_points, sdk_segments);
          if (!cut.ok() || !cut.value().success || !cut.value().changed ||
              (selection_mode_before != ModelingSelectionMode::Edge &&
               selection_mode_before != ModelingSelectionMode::Face)) {
            return cut;
          }
          Result<OperationResult> restored = document.set_selection_mode(
              to_sdk_component_mode(selection_mode_before));
          if (!restored.ok()) {
            return restored;
          }
          return cut;
        },
        "Could not apply the knife stroke.");
    if (!result.ok || !result.changed) {
      return result;
    }
    Result<OperationResult> selected = session_.apply_object_selection(
        objects, SelectionEdit::Replace);
    if (!selected.ok()) {
      return failure(selected.error_message());
    }
    (void)session_.squash_undo_since(undo_mark);
    selection_mode_ = selection_mode_before;
    active_native_object_id_ = modeling_object_id;
    add_selection_to_scene();
    return result;
  }

  [[nodiscard]] bool initialize_workspace_transform_participants() {
    QDR_PROFILE_FUNCTION();
    workspace_transform_participants_.clear();
    const std::vector<ObjectId> selected = selected_sdk_objects();
    Result<std::vector<PolygonDocument>> documents =
        session_.document_copies(selected);
    if (!documents.ok() || documents.value().size() != selected.size()) {
      return false;
    }
    workspace_transform_participants_.reserve(selected.size());
    for (std::size_t index = 0; index < selected.size(); ++index) {
      const ObjectId object = selected[index];
      const std::optional<int> native = native_object(object);
      const std::optional<EntityId> entity =
          native.has_value()
              ? object_map_.entity_for_modeling_object(*native)
              : std::nullopt;
      const MeshComponent *binding =
          entity.has_value() ? scene_.mesh(*entity) : nullptr;
      const MeshAsset *current =
          binding != nullptr ? assets_.mesh(binding->mesh) : nullptr;
      if (!native.has_value() || !entity.has_value() || current == nullptr) {
        workspace_transform_participants_.clear();
        return false;
      }

      PolygonDocument &document = documents.value()[index];
      Result<AuthoredTopology> authored = document.authored_topology();
      if (!authored.ok()) {
        workspace_transform_participants_.clear();
        return false;
      }
      MeshAsset base_mesh = *current;
      const bool has_source_mapping =
          !base_mesh.geometry().vertices.empty() &&
          std::ranges::all_of(base_mesh.geometry().vertices,
                              [](const MeshVertex &vertex) {
                                return vertex.source_vertex_id != 0u;
                              });
      if (!has_source_mapping) {
        std::optional<MeshAsset> compiled =
            MeshProjectionService::compile_sdk_document_mesh(document);
        if (!compiled.has_value()) {
          workspace_transform_participants_.clear();
          return false;
        }
        compiled->name = current->name;
        base_mesh = std::move(*compiled);
      }

      std::unordered_set<std::uint32_t> selected_vertices;
      if (selection_mode_ == ModelingSelectionMode::Object ||
          selection_mode_ == ModelingSelectionMode::Group) {
        selected_vertices.reserve(authored.value().vertices.size());
        for (const quader::modeling::AuthoredTopologyVertex &vertex :
             authored.value().vertices) {
          selected_vertices.insert(vertex.id.index);
        }
      } else {
        const PolygonSelectionSnapshot selection = document.selection();
        selected_vertices.reserve(selection.vertices.size() +
                                  selection.edges.size() * 2u);
        for (const quader::modeling::VertexId vertex : selection.vertices) {
          selected_vertices.insert(vertex.index);
        }
        for (const EdgeKey edge : selection.edges) {
          selected_vertices.insert(edge.a.index);
          selected_vertices.insert(edge.b.index);
        }
        for (const FaceId face_id : selection.faces) {
          const auto face = std::ranges::find(
              authored.value().faces, face_id,
              &quader::modeling::AuthoredTopologyFace::id);
          if (face == authored.value().faces.end()) {
            continue;
          }
          for (const quader::modeling::VertexId vertex : face->vertices) {
            selected_vertices.insert(vertex.index);
          }
        }
      }
      if (selected_vertices.empty()) {
        continue;
      }
      std::unordered_map<std::uint32_t, QVec3> base_positions;
      base_positions.reserve(authored.value().vertices.size());
      for (const quader::modeling::AuthoredTopologyVertex &vertex :
           authored.value().vertices) {
        base_positions.emplace(
            vertex.id.index,
            QVec3{vertex.position.x, vertex.position.y, vertex.position.z});
      }
      std::unordered_map<std::uint32_t, std::vector<std::uint32_t>>
          face_vertices;
      face_vertices.reserve(authored.value().faces.size());
      for (const quader::modeling::AuthoredTopologyFace &face :
           authored.value().faces) {
        std::vector<std::uint32_t> vertices;
        vertices.reserve(face.vertices.size());
        for (const quader::modeling::VertexId vertex : face.vertices) {
          vertices.push_back(vertex.index);
        }
        face_vertices.emplace(face.id.index, std::move(vertices));
      }
      std::vector<WorkspaceTransformSelectedVertex>
          selected_transform_vertices;
      std::unordered_map<std::uint32_t, std::size_t> selected_vertex_lookup;
      selected_transform_vertices.reserve(selected_vertices.size());
      selected_vertex_lookup.reserve(selected_vertices.size());
      for (const quader::modeling::AuthoredTopologyVertex &vertex :
           authored.value().vertices) {
        if (!selected_vertices.contains(vertex.id.index)) {
          continue;
        }
        const QVec3 position{vertex.position.x, vertex.position.y,
                             vertex.position.z};
        selected_vertex_lookup.emplace(vertex.id.index,
                                       selected_transform_vertices.size());
        selected_transform_vertices.push_back({
            .source_vertex_id = vertex.id.index,
            .base_position = position,
            .preview_position = position,
        });
      }
      std::vector<QVec3> base_mesh_positions;
      base_mesh_positions.reserve(base_mesh.geometry().vertices.size());
      for (std::size_t vertex_index = 0;
           vertex_index < base_mesh.geometry().vertices.size();
           ++vertex_index) {
        const MeshVertex &vertex = base_mesh.geometry().vertices[vertex_index];
        base_mesh_positions.push_back(vertex.position);
        const auto selected =
            selected_vertex_lookup.find(vertex.source_vertex_id);
        if (selected != selected_vertex_lookup.end()) {
          selected_transform_vertices[selected->second]
              .compiled_vertex_indices.push_back(vertex_index);
        }
      }
      auto initial_positions =
          std::make_shared<std::vector<QVec3>>(base_mesh_positions);
      const MaterialComponent *material = scene_.material(*entity);
      WorkspaceTransformParticipant participant{
          .object = object,
          .native_object_id = *native,
          .entity = *entity,
          .material = material != nullptr ? *material : MaterialComponent{},
          .world_transform =
              modeling_object_world_transform_resolver_
                  ? modeling_object_world_transform_resolver_(*native)
                        .value_or(scene_.world_transform_matrix(*entity))
                  : scene_.world_transform_matrix(*entity),
          .double_sided = binding->double_sided,
          .layout_key = transform_mesh_layout_key(
              object, base_mesh.geometry().vertices),
          .position_revision = workspace_transform_preview_revision_,
          .preview_document = document,
          .preview_mesh = std::move(base_mesh),
          .base_mesh_positions = std::move(base_mesh_positions),
          .selected_vertices = std::move(selected_transform_vertices),
          .selected_vertex_lookup = std::move(selected_vertex_lookup),
          .position_buffers = {std::move(initial_positions)},
          .base_positions = std::move(base_positions),
          .face_vertices = std::move(face_vertices),
      };
      workspace_transform_participants_.push_back(std::move(participant));
    }
    return !workspace_transform_participants_.empty();
  }

  [[nodiscard]] ModelingCommandResult update_workspace_transform_meshes(
      const WorkspaceTransformResolver &resolve_transform,
      DocumentMutationByObject commit_mutation,
      std::string_view fallback_message) {
    QDR_PROFILE_FUNCTION();
    if (!workspace_preview_active_ ||
        workspace_transform_participants_.empty()) {
      return failure("No active transient transform workspace.");
    }
    std::vector<std::optional<quader::modeling::Transform3>> transforms;
    transforms.reserve(workspace_transform_participants_.size());
    {
      QDR_PROFILE_SCOPE("Transform preview resolve participant transforms");
      for (const WorkspaceTransformParticipant &participant :
           workspace_transform_participants_) {
        transforms.push_back(resolve_transform(participant.object));
      }
    }

    bool changed = false;
    bool effective_changed = false;
    std::vector<std::uint8_t> participant_packed_positions_changed(
        workspace_transform_participants_.size(), 0u);
    {
      QDR_PROFILE_SCOPE("Transform preview update selected positions");
      for (std::size_t participant_index = 0u;
           participant_index < workspace_transform_participants_.size();
           ++participant_index) {
        WorkspaceTransformParticipant &participant =
            workspace_transform_participants_[participant_index];
        const std::optional<quader::modeling::Transform3> &transform =
            transforms[participant_index];
        const std::vector<QVec3> &published_positions =
            *participant
                 .position_buffers[participant.published_position_buffer];
        for (WorkspaceTransformSelectedVertex &vertex :
             participant.selected_vertices) {
          const QVec3 next = transform.has_value()
                                 ? transform_sdk_point(*transform,
                                                       vertex.base_position)
                                 : vertex.base_position;
          const bool vertex_changed =
              vertex.preview_position.x != next.x ||
              vertex.preview_position.y != next.y ||
              vertex.preview_position.z != next.z;
          changed = changed || vertex_changed;
          if (vertex_changed &&
              !participant_packed_positions_changed[participant_index]) {
            for (const std::size_t compiled_index :
                 vertex.compiled_vertex_indices) {
              const QVec3 &published = published_positions[compiled_index];
              if (published.x != next.x || published.y != next.y ||
                  published.z != next.z) {
                participant_packed_positions_changed[participant_index] = 1u;
                break;
              }
            }
          }
          effective_changed =
              effective_changed || vertex.base_position.x != next.x ||
              vertex.base_position.y != next.y ||
              vertex.base_position.z != next.z;
          vertex.preview_position = next;
        }
      }
    }
    bool packed_positions_changed = false;
    {
      QDR_PROFILE_SCOPE("Transform preview publish packed positions");
      for (std::size_t participant_index = 0u;
           participant_index < workspace_transform_participants_.size();
           ++participant_index) {
        if (!participant_packed_positions_changed[participant_index]) {
          continue;
        }
        packed_positions_changed = true;
        WorkspaceTransformParticipant &participant =
            workspace_transform_participants_[participant_index];
        std::size_t buffer_index = participant.position_buffers.size();
        for (std::size_t candidate = 0u;
             candidate < participant.position_buffers.size(); ++candidate) {
          if (participant.position_buffers[candidate].use_count() == 1) {
            buffer_index = candidate;
            break;
          }
        }
        if (buffer_index == participant.position_buffers.size()) {
          participant.position_buffers.push_back(
              std::make_shared<std::vector<QVec3>>());
        }

        std::vector<VertexPositionUpdate> updates;
        updates.reserve(participant.selected_vertices.size());
        for (const WorkspaceTransformSelectedVertex &vertex :
             participant.selected_vertices) {
          updates.push_back({
              .vertex = quader::modeling::make_id<
                  quader::modeling::VertexTag>(vertex.source_vertex_id),
              .position = to_sdk(vertex.preview_position),
          });
        }
        Result<OperationResult> positioned =
            participant.preview_document.set_vertex_positions(updates);
        if (!positioned.ok()) {
          return failure(positioned.error_message());
        }
        std::optional<MeshAsset> projected =
            MeshProjectionService::update_sdk_document_mesh_positions(
                participant.preview_document, participant.preview_mesh);
        if (!projected.has_value()) {
          projected = MeshProjectionService::compile_sdk_document_mesh(
              participant.preview_document);
        }
        if (!projected.has_value() ||
            projected->geometry().vertices.size() !=
                participant.base_mesh_positions.size()) {
          return failure(
              "Could not derive the transient transform mesh attributes.");
        }
        projected->name = participant.preview_mesh.name;

        std::vector<QVec3> &packed_positions =
            *participant.position_buffers[buffer_index];
        packed_positions.clear();
        packed_positions.reserve(projected->geometry().vertices.size());
        for (const MeshVertex &vertex : projected->geometry().vertices) {
          packed_positions.push_back(vertex.position);
        }
        participant.preview_mesh = std::move(*projected);
        participant.published_position_buffer = buffer_index;
      }
    }
    workspace_transform_commit_mutation_ = std::move(commit_mutation);
    workspace_transform_transient_active_ = true;
    if (packed_positions_changed) {
      ++workspace_transform_preview_revision_;
      for (std::size_t participant_index = 0u;
           participant_index < workspace_transform_participants_.size();
           ++participant_index) {
        if (participant_packed_positions_changed[participant_index]) {
          workspace_transform_participants_[participant_index]
              .position_revision = workspace_transform_preview_revision_;
        }
      }
    }
    preview_changed_ =
        workspace_transform_preexisting_changed_ || effective_changed;
    return {.ok = true,
            .changed = changed,
            .message = changed ? std::string(fallback_message)
                               : "Transform preview unchanged."};
  }

  [[nodiscard]] std::vector<ModelingToolPreviewMesh>
  workspace_transform_preview_meshes() const {
    QDR_PROFILE_FUNCTION();
    std::vector<ModelingToolPreviewMesh> meshes;
    if (!workspace_transform_transient_active_) {
      return meshes;
    }
    meshes.reserve(workspace_transform_participants_.size());
    for (const WorkspaceTransformParticipant &participant :
         workspace_transform_participants_) {
      meshes.push_back({
          .source_object_id = participant.native_object_id,
          .source_entity = participant.entity,
          .material = participant.material,
          .mesh = participant.preview_mesh,
          .transform = participant.world_transform,
          .double_sided = participant.double_sided,
          .dynamic_positions = true,
          .suppress_source = true,
          .layout_key = participant.layout_key,
          .position_revision = participant.position_revision,
          .dynamic_position_payload =
              participant.position_buffers[participant.published_position_buffer],
      });
    }
    return meshes;
  }

  [[nodiscard]] std::uint64_t workspace_transform_preview_revision() const {
    return workspace_transform_preview_revision_;
  }

  [[nodiscard]] bool workspace_transform_preview_active() const {
    return workspace_transform_transient_active_;
  }

  void apply_workspace_transform_preview_to_overlay(
      ViewportOverlayFrame &frame) const {
    if (!workspace_transform_transient_active_) {
      return;
    }
    std::unordered_map<int, const WorkspaceTransformParticipant *>
        participants;
    participants.reserve(workspace_transform_participants_.size());
    for (const WorkspaceTransformParticipant &participant :
         workspace_transform_participants_) {
      participants.emplace(participant.native_object_id, &participant);
    }
    const auto participant_for = [&participants](int object_id) {
      const auto found = participants.find(object_id);
      return found != participants.end() ? found->second : nullptr;
    };
    const auto preview_world = [](const WorkspaceTransformParticipant &entry,
                                  int vertex_id) -> std::optional<QVec3> {
      if (vertex_id < 0) {
        return std::nullopt;
      }
      const auto found =
          entry.selected_vertex_lookup.find(
              static_cast<std::uint32_t>(vertex_id));
      if (found == entry.selected_vertex_lookup.end()) {
        return std::nullopt;
      }
      return transform_point(
          entry.world_transform,
          entry.selected_vertices[found->second].preview_position);
    };
    for (ViewportOverlayPoint &point : frame.points) {
      const WorkspaceTransformParticipant *participant =
          participant_for(point.element.object_id);
      if (participant == nullptr) {
        continue;
      }
      if (const std::optional<QVec3> position =
              preview_world(*participant, point.element.vertex_id)) {
        point.position = *position;
      }
    }
    for (ViewportOverlayLine &line : frame.lines) {
      const WorkspaceTransformParticipant *participant =
          participant_for(line.element.object_id);
      if (participant == nullptr) {
        continue;
      }
      if (const std::optional<QVec3> position =
              preview_world(*participant, line.element.edge_a)) {
        line.start = *position;
      }
      if (const std::optional<QVec3> position =
              preview_world(*participant, line.element.edge_b)) {
        line.end = *position;
      }
    }
    const auto remap_face_point = [&preview_world](
                                      const WorkspaceTransformParticipant &entry,
                                      int face_id, QVec3 point) {
      if (face_id < 0) {
        return point;
      }
      const auto face =
          entry.face_vertices.find(static_cast<std::uint32_t>(face_id));
      if (face == entry.face_vertices.end()) {
        return point;
      }
      constexpr float kPositionToleranceSquared = 1.0e-8F;
      for (const std::uint32_t vertex_id : face->second) {
        const auto base = entry.base_positions.find(vertex_id);
        const std::optional<QVec3> preview =
            preview_world(entry, static_cast<int>(vertex_id));
        if (base == entry.base_positions.end() || !preview.has_value()) {
          continue;
        }
        const QVec3 base_world =
            transform_point(entry.world_transform, base->second);
        const QVec3 delta = point - base_world;
        if (delta.x * delta.x + delta.y * delta.y + delta.z * delta.z <=
            kPositionToleranceSquared) {
          return *preview;
        }
      }
      return point;
    };
    for (ViewportOverlayTriangle &triangle : frame.triangles) {
      const WorkspaceTransformParticipant *participant =
          participant_for(triangle.element.object_id);
      if (participant == nullptr) {
        continue;
      }
      triangle.a = remap_face_point(*participant, triangle.element.face_id,
                                    triangle.a);
      triangle.b = remap_face_point(*participant, triangle.element.face_id,
                                    triangle.b);
      triangle.c = remap_face_point(*participant, triangle.element.face_id,
                                    triangle.c);
    }
  }

  [[nodiscard]] ModelingCommandResult
  begin_translate_preview(bool duplicate_selected = false,
                          bool topology_stable_transform = false) {
    QDR_PROFILE_FUNCTION();
    if (preview_active()) {
      return failure("An SDK modeling preview is already active.");
    }
    if (selected_sdk_objects().empty()) {
      return failure("No active SDK modeling selection.");
    }
    workspace_preview_undo_mark_ = session_.undo_depth();
    committed_transform_history_delta_ = {};
    committed_transform_undo_mark_.reset();
    workspace_transform_snapshotless_ =
        topology_stable_transform && !duplicate_selected &&
        selection_mode_ != ModelingSelectionMode::Object &&
        selection_mode_ != ModelingSelectionMode::Group;
    if (!workspace_transform_snapshotless_) {
      QDR_PROFILE_SCOPE("Transform preview capture topology-changing base");
      preview_base_snapshot_ = capture_snapshot(true);
      preview_asset_checkpoint_ = assets_.capture_projection_checkpoint();
      preview_scene_checkpoint_ = scene_.capture_projection_checkpoint();
    }
    preview_changed_ = false;
    if (duplicate_selected &&
        (selection_mode_ == ModelingSelectionMode::Object ||
         selection_mode_ == ModelingSelectionMode::Group)) {
      ModelingCommandResult duplicated = duplicate_selected_objects(
          {}, {}, "Duplicated selected meshes.", true);
      if (!duplicated.ok) {
        clear_preview_checkpoints();
        return duplicated;
      }
      preview_changed_ = duplicated.changed;
    }
    if (!workspace_transform_snapshotless_) {
      QDR_PROFILE_SCOPE("Transform preview capture update base");
      workspace_update_base_snapshot_ = capture_snapshot(true);
    }
    workspace_transform_preexisting_changed_ = preview_changed_;
    {
      QDR_PROFILE_SCOPE("Transform preview initialize retained participants");
      if (!initialize_workspace_transform_participants()) {
        clear_preview_checkpoints();
        return failure("Transform preview participants could not be built.");
      }
    }
    workspace_preview_active_ = true;
    active_preview_operation_id_ = "translate_selection";
    active_preview_operation_.reset();
    active_preview_settings_.reset();
    return {.ok = true,
            .changed = preview_changed_,
            .message = "Transform preview started."};
  }

  [[nodiscard]] ModelingCommandResult
  update_workspace_transform_preview_by_object(
      const DocumentMutationByObject &mutation,
      std::string_view fallback_message,
      bool position_only_projection = false) {
    if (!workspace_preview_active_ ||
        !workspace_update_base_snapshot_.has_value() ||
        !preview_base_snapshot_.has_value()) {
      return failure("No active SDK workspace transform preview.");
    }
    const ModelingSdkProjectionSnapshot cancel_base = *preview_base_snapshot_;
    const ModelingSdkProjectionSnapshot update_base =
        *workspace_update_base_snapshot_;
    const std::optional<AssetRegistryProjectionCheckpoint> asset_checkpoint =
        preview_asset_checkpoint_;
    const std::optional<SceneProjectionCheckpoint> scene_checkpoint =
        preview_scene_checkpoint_;
    const std::size_t undo_mark = workspace_preview_undo_mark_;
    const bool changed_before = preview_changed_;
    const quader::modeling::ModelingSession session_before_attempt = session_;
    const AssetRegistryProjectionCheckpoint assets_before_attempt =
        assets_.capture_projection_checkpoint();
    const SceneProjectionCheckpoint scene_before_attempt =
        scene_.capture_projection_checkpoint();
    const ModelingSdkProjectionStats stats_before_attempt = stats_;
    const int active_before_attempt = active_native_object_id_;
    const ModelingSelectionMode mode_before_attempt = selection_mode_;
    const std::uint64_t persistent_revision_before_attempt =
        persistent_content_revision_;
    const std::uint64_t mode_revision_before_attempt = mode_revision_;
    const std::size_t deleted_before_attempt =
        active_preview_deleted_vertex_count_;
    const std::vector<ObjectId> selected = selected_sdk_objects();
    const bool sdk_only_reset =
        preview_targets_have_unique_mesh_bindings(selected);
    ModelingCommandResult restored;
    if (sdk_only_reset) {
      QDR_PROFILE_SCOPE("Modeling transform preview restore SDK base only");
      session_.restore_snapshot_preserving_identity_high_water(
          update_base.impl_->session);
      active_preview_deleted_vertex_count_ = 0U;
      restored = {.ok = true, .changed = true};
    } else {
      QDR_PROFILE_SCOPE("Modeling transform preview restore full projection");
      restored = restore_snapshot_exact(update_base);
    }
    if (!restored.ok) {
      return restored;
    }
    preview_base_snapshot_ = cancel_base;
    workspace_update_base_snapshot_ = update_base;
    preview_asset_checkpoint_ = asset_checkpoint;
    preview_scene_checkpoint_ = scene_checkpoint;
    workspace_preview_undo_mark_ = undo_mark;
    workspace_preview_active_ = true;
    active_preview_operation_id_ = "translate_selection";
    active_preview_operation_.reset();
    active_preview_settings_.reset();
    ModelingCommandResult result;
    {
      QDR_PROFILE_SCOPE("Modeling transform preview mutate selected documents");
      result = mutate_documents_by_object(selected, mutation, fallback_message,
                                          true, sdk_only_reset,
                                          position_only_projection &&
                                              sdk_only_reset);
    }
    if (!result.ok) {
      session_.restore_snapshot_preserving_identity_high_water(
          session_before_attempt);
      (void)assets_.restore_projection_checkpoint(assets_before_attempt);
      (void)scene_.restore_projection_checkpoint(scene_before_attempt);
      stats_ = stats_before_attempt;
      active_native_object_id_ = active_before_attempt;
      selection_mode_ = mode_before_attempt;
      persistent_content_revision_ = persistent_revision_before_attempt;
      mode_revision_ = mode_revision_before_attempt;
      active_preview_deleted_vertex_count_ = deleted_before_attempt;
      preview_base_snapshot_ = cancel_base;
      workspace_update_base_snapshot_ = update_base;
      preview_asset_checkpoint_ = asset_checkpoint;
      preview_scene_checkpoint_ = scene_checkpoint;
      workspace_preview_undo_mark_ = undo_mark;
      workspace_preview_active_ = true;
      active_preview_operation_id_ = "translate_selection";
      active_preview_operation_.reset();
      active_preview_settings_.reset();
      preview_changed_ = changed_before;
      return result;
    }
    preview_changed_ = changed_before || (result.ok && result.changed);
    if (result.ok && result.changed) {
      ++stats_.preview_publication_count;
    }
    return result;
  }

  [[nodiscard]] ModelingCommandResult update_workspace_transform_preview(
      const DocumentMutation &mutation, std::string_view fallback_message,
      bool position_only_projection = false) {
    return update_workspace_transform_preview_by_object(
        [&mutation](ObjectId, PolygonDocument &document) {
          return mutation(document);
        },
        fallback_message, position_only_projection);
  }

  [[nodiscard]] ModelingCommandResult update_translate_preview(QVec3 delta) {
    if (!preview_active()) {
      return failure("No active SDK translate preview.");
    }
    if (!finite(delta)) {
      return failure("Translation delta must be finite.");
    }
    const ModelingSelectionMode mode = selection_mode_;
    const DocumentMutation mutation =
        [delta, mode](PolygonDocument &document) {
          if (mode != ModelingSelectionMode::Object &&
              mode != ModelingSelectionMode::Group) {
            return document.translate_selection(to_sdk(delta));
          }
          Result<OperationResult> selected = document.select_all_faces();
          if (!selected.ok()) {
            return selected;
          }
          Result<OperationResult> translated =
              document.translate_selection(to_sdk(delta));
          if (!translated.ok()) {
            return translated;
          }
          Result<OperationResult> cleared = document.clear_selection();
          if (!cleared.ok()) {
            return cleared;
          }
          translated.value().dirty.selection = false;
          return translated;
        };
    if (workspace_preview_active_) {
      const quader::modeling::Transform3 preview_transform{
          .origin = to_sdk(delta),
      };
      return update_workspace_transform_meshes(
          [preview_transform](ObjectId) {
            return std::optional{preview_transform};
          },
          [mutation](ObjectId, PolygonDocument &document) {
            return mutation(document);
          },
          "Transform preview updated.");
    }
    return update_preview_from_base(mutation,
                                    "SDK translate preview failed.");
  }

  [[nodiscard]] ModelingCommandResult update_transform_preview(
      const ModelingTransform3 &transform, QVec3 pivot) {
    const quader::modeling::Transform3 sdk_transform = to_sdk(transform, pivot);
    const ModelingSelectionMode mode = selection_mode_;
    const DocumentMutation mutation =
        [sdk_transform, mode](PolygonDocument &document) {
          if (mode != ModelingSelectionMode::Object &&
              mode != ModelingSelectionMode::Group) {
            return document.transform_selection(sdk_transform);
          }
          Result<OperationResult> selected = document.select_all_faces();
          if (!selected.ok()) {
            return selected;
          }
          Result<OperationResult> transformed =
              document.transform_selection(sdk_transform);
          if (!transformed.ok()) {
            return transformed;
          }
          Result<OperationResult> cleared = document.clear_selection();
          if (!cleared.ok()) {
            return cleared;
          }
          transformed.value().dirty.selection = false;
          return transformed;
        };
    if (workspace_preview_active_) {
      return update_workspace_transform_meshes(
          [sdk_transform](ObjectId) {
            return std::optional{sdk_transform};
          },
          [mutation](ObjectId, PolygonDocument &document) {
            return mutation(document);
          },
          "Transform preview updated.");
    }
    return update_preview_from_base(mutation,
                                    "SDK transform preview failed.");
  }

  [[nodiscard]] ModelingCommandResult update_object_transform_preview(
      std::span<const ModelingSdkObjectTransformPreview> transforms,
      ModelingSdkTransformPreviewKind kind,
      float closed_edge_ledge_size) {
    /// Stores one resolved SDK object and its portable preview transform.
    struct SdkTransform {
      ObjectId object;
      quader::modeling::Transform3 transform;
    };
    std::vector<SdkTransform> sdk_transforms;
    sdk_transforms.reserve(transforms.size());
    for (const ModelingSdkObjectTransformPreview &transform : transforms) {
      const std::optional<ObjectId> object =
          sdk_object(transform.modeling_object_id);
      if (!object.has_value()) {
        return failure("Transform preview target is not live.");
      }
      sdk_transforms.push_back({
          .object = *object,
          .transform = to_sdk(transform.transform),
      });
    }
    std::ranges::sort(sdk_transforms, {}, &SdkTransform::object);
    const auto resolved_transforms =
        std::make_shared<const std::vector<SdkTransform>>(
            std::move(sdk_transforms));
    const DocumentMutationByObject mutation =
        [resolved_transforms, kind,
         closed_edge_ledge_size](ObjectId object, PolygonDocument &document) {
          const auto found = std::ranges::lower_bound(
              *resolved_transforms, object, {}, &SdkTransform::object);
          if (found == resolved_transforms->end() || found->object != object) {
            OperationResult unchanged;
            unchanged.message = "Transform preview unchanged.";
            return Result<OperationResult>::success(std::move(unchanged));
          }
          switch (kind) {
          case ModelingSdkTransformPreviewKind::Transform:
            return document.transform_selection(found->transform);
          case ModelingSdkTransformPreviewKind::ExtrudeOffset:
            return document.extrude_selected_elements(
                found->transform.origin, closed_edge_ledge_size);
          case ModelingSdkTransformPreviewKind::TransformExtrude:
            return document.transform_extrude_selected_elements(
                found->transform);
          case ModelingSdkTransformPreviewKind::Inset:
            return document.inset_selected_elements(found->transform, {});
          }
          return Result<OperationResult>::failure(
              quader::modeling::make_error(
                  quader::modeling::ErrorCode::UnsupportedOperation,
                  "Transform preview kind is unsupported."));
        };
    if (kind == ModelingSdkTransformPreviewKind::Transform &&
        workspace_preview_active_) {
      return update_workspace_transform_meshes(
          [resolved_transforms](ObjectId object)
              -> std::optional<quader::modeling::Transform3> {
            const auto found = std::ranges::lower_bound(
                *resolved_transforms, object, {}, &SdkTransform::object);
            return found != resolved_transforms->end() &&
                           found->object == object
                       ? std::optional{found->transform}
                       : std::nullopt;
          },
          mutation, "Transform preview updated.");
    }
    return update_workspace_transform_preview_by_object(
        mutation, "SDK object-local transform preview failed.");
  }

  [[nodiscard]] ModelingCommandResult
  update_extrude_preview(QVec3 offset, float closed_edge_ledge_size) {
    const DocumentMutation mutation =
        [offset, closed_edge_ledge_size](PolygonDocument &document) {
          return document.extrude_selected_elements(
              to_sdk(offset), closed_edge_ledge_size);
        };
    return workspace_preview_active_
               ? update_workspace_transform_preview(
                     mutation, "SDK extrude preview failed.")
               : update_preview_from_base(
                     mutation, "SDK extrude preview failed.");
  }

  [[nodiscard]] ModelingCommandResult
  update_inset_preview(const ModelingTransform3 &transform, QVec3 pivot) {
    const quader::modeling::Transform3 sdk_transform = to_sdk(transform);
    const DocumentMutation mutation =
        [sdk_transform, pivot](PolygonDocument &document) {
          return document.inset_selected_elements(sdk_transform,
                                                   to_sdk(pivot));
        };
    return workspace_preview_active_
               ? update_workspace_transform_preview(
                     mutation, "SDK inset preview failed.")
               : update_preview_from_base(
                     mutation, "SDK inset preview failed.");
  }

  [[nodiscard]] ModelingSdkTransformHistoryDelta
  build_transform_history_delta() const {
    QDR_PROFILE_SCOPE("Transform history build sparse authored positions");
    auto delta =
        std::make_shared<ModelingSdkTransformHistoryDelta::Impl>();
    delta->objects.reserve(workspace_transform_participants_.size());
    for (const WorkspaceTransformParticipant &participant :
         workspace_transform_participants_) {
      ModelingSdkTransformHistoryDelta::Impl::ObjectDelta object_delta;
      object_delta.object = participant.object;
      object_delta.before.reserve(participant.selected_vertices.size());
      object_delta.after.reserve(participant.selected_vertices.size());
      for (const WorkspaceTransformSelectedVertex &vertex :
           participant.selected_vertices) {
        if (vertex.base_position.x == vertex.preview_position.x &&
            vertex.base_position.y == vertex.preview_position.y &&
            vertex.base_position.z == vertex.preview_position.z) {
          continue;
        }
        const VertexId id =
            quader::modeling::make_id<quader::modeling::VertexTag>(
                vertex.source_vertex_id);
        object_delta.before.push_back(
            {.vertex = id, .position = to_sdk(vertex.base_position)});
        object_delta.after.push_back(
            {.vertex = id, .position = to_sdk(vertex.preview_position)});
      }
      if (!object_delta.before.empty()) {
        delta->objects.push_back(std::move(object_delta));
      }
    }
    return delta->objects.empty()
               ? ModelingSdkTransformHistoryDelta{}
               : ModelingSdkTransformHistoryDelta(std::move(delta));
  }

  [[nodiscard]] ModelingCommandResult commit_translate_preview() {
    QDR_PROFILE_FUNCTION();
    if (!preview_active()) {
      return failure("No active SDK translate preview.");
    }
    if (workspace_preview_active_) {
      if (workspace_transform_transient_active_ &&
          workspace_transform_commit_mutation_) {
        if (!preview_changed_) {
          (void)session_.rollback_undo_since(workspace_preview_undo_mark_);
          clear_preview_checkpoints();
          return {.ok = true,
                  .changed = false,
                  .message = "Transform preview unchanged."};
        }
        const std::size_t workspace_undo_mark = workspace_preview_undo_mark_;
        const ModelingSdkTransformHistoryDelta history_delta =
            build_transform_history_delta();
        if (!history_delta.valid()) {
          return failure(
              "Transform preview changed without authored position history.");
        }
        workspace_preview_active_ = false;
        ModelingCommandResult committed;
        {
          QDR_PROFILE_SCOPE("Transform commit position-only documents");
          const std::shared_ptr<
              const ModelingSdkTransformHistoryDelta::Impl>
              exact_positions = history_delta.impl_;
          committed = commit_sparse_transform_positions(exact_positions);
        }
        if (!committed.ok) {
          workspace_preview_active_ = true;
          return committed;
        }
        session_.squash_undo_since(workspace_undo_mark);
        committed_transform_history_delta_ = history_delta;
        committed_transform_undo_mark_ = workspace_undo_mark;
        clear_preview_checkpoints();
        return committed;
      }
      const bool changed = preview_changed_;
      session_.squash_undo_since(workspace_preview_undo_mark_);
      if (changed) {
        ++persistent_content_revision_;
      }
      clear_preview_checkpoints();
      return {.ok = true, .changed = changed, .message = "Preview committed."};
    }
    const quader::modeling::ModelingSession session_before = session_;
    Result<OperationResult> committed = session_.commit_preview();
    if (!committed.ok()) {
      return failure(committed.error_message());
    }
    if (fails(ModelingSdkProjectionFailurePoint::BeforeCommit)) {
      session_.restore_snapshot_preserving_identity_high_water(session_before);
      return failure("Injected failure before SDK preview commit.");
    }
    const bool changed = committed.value().changed || preview_changed_;
    if (changed) {
      ++persistent_content_revision_;
    }
    clear_preview_checkpoints();
    return {.ok = committed.value().success,
            .changed = changed,
            .message = committed.value().message};
  }

  [[nodiscard]] ModelingCommandResult cancel_translate_preview() {
    QDR_PROFILE_FUNCTION();
    if (!preview_active()) {
      return failure("No active SDK translate preview.");
    }
    if (workspace_preview_active_ && workspace_transform_snapshotless_) {
      QDR_PROFILE_SCOPE("Transform preview cancel transient positions");
      const bool changed = preview_changed_;
      (void)session_.rollback_undo_since(workspace_preview_undo_mark_);
      committed_transform_history_delta_ = {};
      committed_transform_undo_mark_.reset();
      clear_preview_checkpoints();
      return {.ok = true,
              .changed = changed,
              .message = "Preview canceled."};
    }
    if (!preview_base_snapshot_.has_value()) {
      return failure("SDK preview restore point is unavailable.");
    }
    const bool changed = preview_changed_;
    const ModelingSdkProjectionSnapshot base = *preview_base_snapshot_;
    if (session_.preview_active()) {
      Result<OperationResult> canceled = session_.cancel_preview();
      if (!canceled.ok()) {
        return failure(canceled.error_message());
      }
    }
    ModelingCommandResult restored = restore_snapshot_exact(base);
    if (!restored.ok) {
      return restored;
    }
    restored.changed = changed;
    restored.message = "Preview canceled.";
    return restored;
  }

  [[nodiscard]] ModelingSdkTransformHistoryDelta
  take_committed_transform_history_delta() {
    QDR_PROFILE_SCOPE("Transform history transfer sparse delta");
    ModelingSdkTransformHistoryDelta delta =
        std::move(committed_transform_history_delta_);
    committed_transform_history_delta_ = {};
    if (delta.valid() && committed_transform_undo_mark_.has_value()) {
      session_.discard_undo_since(*committed_transform_undo_mark_);
    }
    committed_transform_undo_mark_.reset();
    return delta;
  }

  [[nodiscard]] ModelingCommandResult apply_transform_history_delta(
      const ModelingSdkTransformHistoryDelta &delta, bool use_after) {
    QDR_PROFILE_FUNCTION();
    if (!delta.valid()) {
      return failure("Transform history delta is unavailable.");
    }
    const std::size_t undo_mark = session_.undo_depth();
    const std::shared_ptr<const ModelingSdkTransformHistoryDelta::Impl> data =
        delta.impl_;
    ModelingCommandResult applied;
    {
      QDR_PROFILE_SCOPE("Transform history apply sparse authored positions");
      applied = commit_sparse_transform_positions(data, use_after);
    }
    if (applied.ok) {
      session_.discard_undo_since(undo_mark);
    }
    return applied;
  }

  [[nodiscard]] bool preview_active() const noexcept {
    return workspace_preview_active_ || session_.preview_active();
  }

  [[nodiscard]] ModelingSelectionMode selection_mode() const noexcept {
    return selection_mode_;
  }

  [[nodiscard]] ModelingSelectionSummary selection_summary() const {
    const quader::modeling::PreviewTransactionSummary preview =
        session_.preview_summary();
    const SelectionSummaryCacheKey cache_key{
        .persistent_content_revision = persistent_content_revision_,
        .session_content_revision = session_.content_revision(),
        .session_selection_revision = session_.selection_revision(),
        .mode_revision = mode_revision_,
        .object_map_revision = object_map_.revision(),
        .active_native_object_id = active_native_object_id_,
        .mode = selection_mode_,
        .workspace_preview_active = workspace_preview_active_,
        .sdk_preview_active = preview.active,
        .preview_id = preview.id,
        .preview_object_id = preview.object_id,
        .preview_content_revision = preview.content_revision,
    };
    if (selection_summary_cache_key_.has_value() &&
        *selection_summary_cache_key_ == cache_key) {
      return selection_summary_cache_;
    }
    const auto cache_summary = [this, &cache_key](
                                   ModelingSelectionSummary summary) {
      selection_summary_cache_key_ = cache_key;
      selection_summary_cache_ = summary;
      return summary;
    };

    ++stats_.selection_summary_build_count;
    selected_component_entities_cache_.clear();
    ModelingSelectionSummary summary;
    summary.mode = selection_mode_;
    summary.content_revision = persistent_content_revision_;
    summary.selection_revision = session_.selection_revision();
    const std::vector<quader::modeling::SessionObjectSummary> objects =
        session_.objects();
    for (const auto &object : objects) {
      if (object.selected) {
        ++summary.selected_object_count;
      }
    }
    summary.active_object_id = active_native_object_id_;
    if (selection_mode_ == ModelingSelectionMode::Object) {
      summary.has_selection = summary.selected_object_count != 0;
      summary.active_kind = summary.has_selection
                                ? ModelingActiveSelectionKind::Object
                                : ModelingActiveSelectionKind::None;
      return cache_summary(std::move(summary));
    }

    const std::optional<ObjectId> active = active_sdk_object();
    if (!active.has_value()) {
      return cache_summary(std::move(summary));
    }
    PolygonSelectionSnapshot active_selection;
    bool active_selection_available = false;
    for (const auto &object : objects) {
      if (!object.selected && object.id != *active) {
        continue;
      }
      Result<PolygonDocument> document =
          preview.active && preview.object_id == object.id
              ? session_.preview_document_copy(preview.id)
              : session_.document_copy(object.id);
      if (!document.ok()) {
        continue;
      }
      PolygonSelectionSnapshot selection = document.value().selection();
      const bool preview_selection_empty =
          selection_mode_ == ModelingSelectionMode::Vertex
              ? selection.vertices.empty()
          : selection_mode_ == ModelingSelectionMode::Edge
              ? selection.edges.empty()
              : selection.faces.empty();
      if (preview.active && preview.object_id == object.id &&
          preview_selection_empty) {
        Result<PolygonDocument> committed = session_.document_copy(object.id);
        if (committed.ok()) {
          const PolygonSelectionSnapshot committed_selection =
              committed.value().selection();
          if (!committed_selection.vertices.empty() ||
              !committed_selection.edges.empty() ||
              !committed_selection.faces.empty()) {
            selection = committed_selection;
          }
        }
      }
      summary.selected_vertex_count +=
          static_cast<std::uint32_t>(selection.vertices.size());
      summary.selected_edge_count +=
          static_cast<std::uint32_t>(selection.edges.size());
      summary.selected_face_count +=
          static_cast<std::uint32_t>(selection.faces.size());
      if (!selection.vertices.empty() || !selection.edges.empty() ||
          !selection.faces.empty()) {
        const std::optional<int> native = native_object(object.id);
        const std::optional<EntityId> entity =
            native.has_value()
                ? object_map_.entity_for_modeling_object(*native)
                : std::nullopt;
        if (entity.has_value() && scene_.exists(*entity)) {
          selected_component_entities_cache_.push_back(*entity);
        }
      }
      if (object.id == *active) {
        active_selection = selection;
        active_selection_available = true;
      }
    }
    summary.has_selection = summary.selected_vertex_count != 0 ||
                            summary.selected_edge_count != 0 ||
                            summary.selected_face_count != 0;
    if (active_selection_available && active_selection.has_active) {
      if (selection_mode_ == ModelingSelectionMode::Vertex &&
          active_selection.active_vertex.valid()) {
        summary.active_kind = ModelingActiveSelectionKind::Vertex;
        summary.active_vertex_id =
            static_cast<int>(active_selection.active_vertex.index);
      } else if (selection_mode_ == ModelingSelectionMode::Edge &&
                 active_selection.active_edge.valid()) {
        summary.active_kind = ModelingActiveSelectionKind::Edge;
        summary.active_edge_a =
            static_cast<int>(active_selection.active_edge.a.index);
        summary.active_edge_b =
            static_cast<int>(active_selection.active_edge.b.index);
      } else if (selection_mode_ == ModelingSelectionMode::Face &&
                 active_selection.active_face.valid()) {
        summary.active_kind = ModelingActiveSelectionKind::Face;
        summary.active_face_id =
            static_cast<int>(active_selection.active_face.index);
      }
    }
    if (summary.active_kind == ModelingActiveSelectionKind::None &&
        summary.has_selection) {
      summary.active_kind = selection_mode_ == ModelingSelectionMode::Vertex
                                ? ModelingActiveSelectionKind::Vertex
                            : selection_mode_ == ModelingSelectionMode::Edge
                                ? ModelingActiveSelectionKind::Edge
                                : ModelingActiveSelectionKind::Face;
    }
    return cache_summary(std::move(summary));
  }

  [[nodiscard]] std::span<const EntityId>
  selected_component_entities() const {
    QDR_PROFILE_FUNCTION();
    (void)selection_summary();
    return selected_component_entities_cache_;
  }

  [[nodiscard]] ModelingSelectionExport export_selection() const {
    ModelingSelectionExport exported;
    exported.summary = selection_summary();
    const auto summaries = session_.objects();
    std::unordered_map<
        std::uint64_t, const quader::modeling::SessionObjectSummary *>
        summaries_by_id;
    summaries_by_id.reserve(summaries.size());
    for (const auto &summary : summaries) {
      summaries_by_id.emplace(sdk_object_key(summary.id), &summary);
    }
    for (const auto &[native_id, object_id] : object_ids_) {
      const auto summary = summaries_by_id.find(sdk_object_key(object_id));
      if (summary == summaries_by_id.end() ||
          (!summary->second->selected &&
           native_id != active_native_object_id_)) {
        continue;
      }
      Result<PolygonDocument> document = session_.document_copy(object_id);
      Result<AuthoredTopology> topology =
          document.ok() ? document.value().authored_topology()
                        : Result<AuthoredTopology>::failure(
                              quader::modeling::make_error(
                                  quader::modeling::ErrorCode::InvalidId,
                                  "SDK document is unavailable."));
      if (!document.ok() || !topology.ok()) {
        continue;
      }
      const PolygonSelectionSnapshot selection = document.value().selection();
      ModelingSelectionExportObject object;
      object.modeling_object_id = native_id;
      object.entity = object_map_.entity_for_modeling_object(native_id)
                          .value_or(kInvalidEntityId);
      object.selected = summary->second->selected;
      if (object.entity != kInvalidEntityId && scene_.exists(object.entity)) {
        object.entity_uuid = scene_.uuid(object.entity);
        if (const NameComponent *name = scene_.name(object.entity)) {
          object.name = name->name;
        }
      }
      for (const VertexId id : selection.vertices) {
        const auto vertex = std::find_if(
            topology.value().vertices.begin(), topology.value().vertices.end(),
            [id](const auto &candidate) { return candidate.id == id; });
        if (vertex != topology.value().vertices.end()) {
          object.selection.vertices.push_back(id.index);
          object.selected_vertices.push_back({
              .id = id.index,
              .position = {vertex->position.x, vertex->position.y,
                           vertex->position.z},
          });
        }
      }
      for (const EdgeKey edge : selection.edges) {
        object.selection.edges.push_back(
            quader_poly::make_edge(edge.a.index, edge.b.index));
        ModelingSelectionExportEdge exported_edge{
            .a = edge.a.index,
            .b = edge.b.index,
        };
        const auto a = std::find_if(
            topology.value().vertices.begin(), topology.value().vertices.end(),
            [edge](const auto &candidate) { return candidate.id == edge.a; });
        const auto b = std::find_if(
            topology.value().vertices.begin(), topology.value().vertices.end(),
            [edge](const auto &candidate) { return candidate.id == edge.b; });
        if (a != topology.value().vertices.end()) {
          exported_edge.a_position = {
              a->position.x, a->position.y, a->position.z};
        }
        if (b != topology.value().vertices.end()) {
          exported_edge.b_position = {
              b->position.x, b->position.y, b->position.z};
        }
        object.selected_edges.push_back(std::move(exported_edge));
      }
      for (const FaceId id : selection.faces) {
        const auto face = std::find_if(
            topology.value().faces.begin(), topology.value().faces.end(),
            [id](const auto &candidate) { return candidate.id == id; });
        if (face == topology.value().faces.end()) {
          continue;
        }
        object.selection.faces.push_back(id.index);
        ModelingSelectionExportFace exported_face;
        exported_face.id = id.index;
        for (const VertexId vertex : face->vertices) {
          exported_face.vertices.push_back(vertex.index);
        }
        object.selected_faces.push_back(std::move(exported_face));
      }
      exported.objects.push_back(std::move(object));
    }
    return exported;
  }

  [[nodiscard]] std::optional<SceneBounds> selection_bounds() const {
    if (selection_mode_ != ModelingSelectionMode::Object &&
        selection_mode_ != ModelingSelectionMode::Group) {
      const quader::modeling::PreviewTransactionSummary preview =
          session_.preview_summary();
      std::optional<SceneBounds> component_bounds;
      const std::optional<ObjectId> active = active_sdk_object();
      for (const quader::modeling::SessionObjectSummary &object :
           session_.objects()) {
        if (!object.selected &&
            (!active.has_value() || object.id != *active)) {
          continue;
        }
        const std::optional<int> native = native_object(object.id);
        const std::optional<EntityId> entity =
            native.has_value()
                ? object_map_.entity_for_modeling_object(*native)
                : std::nullopt;
        if (!entity.has_value() || !scene_.exists(*entity)) {
          continue;
        }
        Result<PolygonDocument> document =
            preview.active && preview.object_id == object.id
                ? session_.preview_document_copy(preview.id)
                : session_.document_copy(object.id);
        if (!document.ok()) {
          continue;
        }
        PolygonSelectionSnapshot selection = document.value().selection();
        const bool preview_selection_empty =
            selection_mode_ == ModelingSelectionMode::Vertex
                ? selection.vertices.empty()
            : selection_mode_ == ModelingSelectionMode::Edge
                ? selection.edges.empty()
                : selection.faces.empty();
        if (preview.active && preview.object_id == object.id &&
            preview_selection_empty) {
          Result<PolygonDocument> committed = session_.document_copy(object.id);
          if (committed.ok()) {
            selection = committed.value().selection();
            document = std::move(committed);
          }
        }
        Result<AuthoredTopology> topology = document.value().authored_topology();
        if (!topology.ok()) {
          continue;
        }
        const auto id_key = [](const auto id) {
          return (static_cast<std::uint64_t>(id.generation) << 32U) |
                 static_cast<std::uint64_t>(id.index);
        };
        const AuthoredTopology &authored_topology = topology.value();
        std::unordered_map<
            std::uint64_t,
            const quader::modeling::AuthoredTopologyVertex *>
            vertices_by_id;
        vertices_by_id.reserve(authored_topology.vertices.size());
        for (const quader::modeling::AuthoredTopologyVertex &vertex :
             authored_topology.vertices) {
          // Preserve the old find_if first-record behavior for malformed
          // duplicate IDs while making component projection linear.
          vertices_by_id.emplace(id_key(vertex.id), &vertex);
        }
        std::unordered_map<std::uint64_t,
                           const quader::modeling::AuthoredTopologyFace *>
            faces_by_id;
        if (!selection.faces.empty()) {
          faces_by_id.reserve(authored_topology.faces.size());
          for (const quader::modeling::AuthoredTopologyFace &face :
               authored_topology.faces) {
            faces_by_id.emplace(id_key(face.id), &face);
          }
        }
        const QMat4 world_from_object =
            scene_.world_transform_matrix(*entity);
        const auto include_vertex = [&](VertexId id) {
          const auto vertex = vertices_by_id.find(id_key(id));
          if (vertex == vertices_by_id.end() || vertex->second == nullptr) {
            return;
          }
          const QVec3 point = transform_point(
              world_from_object,
              {vertex->second->position.x, vertex->second->position.y,
               vertex->second->position.z});
          if (!component_bounds.has_value()) {
            component_bounds = {.min = point, .max = point};
          } else {
            include_point(*component_bounds, point);
          }
        };
        for (const VertexId id : selection.vertices) {
          include_vertex(id);
        }
        for (const EdgeKey edge : selection.edges) {
          include_vertex(edge.a);
          include_vertex(edge.b);
        }
        for (const FaceId id : selection.faces) {
          const auto face = faces_by_id.find(id_key(id));
          if (face != faces_by_id.end() && face->second != nullptr) {
            for (const VertexId vertex : face->second->vertices) {
              include_vertex(vertex);
            }
          }
        }
      }
      return component_bounds;
    }

    std::optional<SceneBounds> bounds;
    for (const EntityId entity : selected_entities()) {
      const std::optional<SceneBounds> current =
          entity_world_bounds(scene_, assets_, entity);
      if (!current.has_value()) {
        continue;
      }
      if (!bounds.has_value()) {
        bounds = current;
      } else {
        include_point(*bounds, current->min);
        include_point(*bounds, current->max);
      }
    }
    return bounds;
  }

  [[nodiscard]] std::vector<EntityId> selected_entities() const {
    std::vector<EntityId> entities;
    for (const auto &object : session_.objects()) {
      if (!object.selected) {
        continue;
      }
      const std::optional<int> native = native_object(object.id);
      const std::optional<EntityId> entity =
          native.has_value()
              ? object_map_.entity_for_modeling_object(*native)
              : std::nullopt;
      if (entity.has_value() && scene_.exists(*entity)) {
        entities.push_back(*entity);
      }
    }
    return entities;
  }

  [[nodiscard]] EntityId active_selected_entity() const {
    if (active_native_object_id_ == kInvalidModelingObjectId) {
      return kInvalidEntityId;
    }
    return object_map_.entity_for_modeling_object(active_native_object_id_)
        .value_or(kInvalidEntityId);
  }

  void add_selection_to_scene() {
    std::vector<EntityId> combined;
    for (const EntityId entity : scene_.selected_entities()) {
      if (!owns_entity(entity)) {
        combined.push_back(entity);
      }
    }
    const std::size_t preserved_count = combined.size();
    std::unordered_set<EntityId> membership(combined.begin(),
                                            combined.end());
    for (const EntityId entity : selected_entities()) {
      if (scene_.exists(entity) && membership.insert(entity).second) {
        combined.push_back(entity);
      }
    }
    EntityId active = active_selected_entity();
    if (active == kInvalidEntityId || !scene_.exists(active) ||
        !membership.contains(active)) {
      active = combined.size() > preserved_count
                   ? combined.back()
                   : (combined.empty() ? kInvalidEntityId
                                       : combined.front());
    }
    (void)scene_.replace_selection(combined, active);
  }

  [[nodiscard]] std::uint64_t content_revision() const {
    return persistent_content_revision_;
  }

  [[nodiscard]] std::uint64_t selection_revision() const {
    return session_.selection_revision() + mode_revision_;
  }

  // Internal preview/rollback snapshots can retain SDK-local history. The
  // public host memento path passes false so each history item owns only one
  // semantic workspace checkpoint.
  [[nodiscard]] ModelingSdkProjectionSnapshot
  capture_snapshot(bool include_sdk_history) const {
    auto snapshot = std::make_shared<ModelingSdkProjectionSnapshot::Impl>();
    if (include_sdk_history) {
      snapshot->session = session_;
    } else {
      snapshot->session = session_.current_workspace_checkpoint();
    }
    snapshot->object_ids = object_ids_;
    snapshot->next_native_object_id = next_native_object_id_;
    snapshot->active_native_object_id = active_native_object_id_;
    snapshot->selection_mode = selection_mode_;
    snapshot->persistent_content_revision = persistent_content_revision_;
    snapshot->mode_revision = mode_revision_;
    snapshot->projected_objects.reserve(object_ids_.size());
    std::unordered_set<int> projected_ids;
    projected_ids.reserve(object_ids_.size());
    for (const auto &[native_id, object_id] : object_ids_) {
      (void)object_id;
      projected_ids.insert(native_id);
    }
    const auto live_identities =
        object_map_.capture_scene_identities_for_modeling_objects(
            scene_, projected_ids);
    for (const auto &[native_id, object_id] : object_ids_) {
      (void)object_id;
      const std::optional<EntityId> entity =
          object_map_.entity_for_modeling_object(native_id);
      if (!entity.has_value() || !scene_.exists(*entity)) {
        continue;
      }
      const ModelingObjectComponent *component =
          scene_.modeling_object(*entity);
      const MeshComponent *mesh_component = scene_.mesh(*entity);
      const MeshAsset *mesh = mesh_component != nullptr
                                  ? assets_.mesh(mesh_component->mesh)
                                  : nullptr;
      const auto identity = live_identities.find(native_id);
      if (component == nullptr || mesh_component == nullptr || mesh == nullptr ||
          identity == live_identities.end()) {
        continue;
      }
      snapshot->projected_objects.push_back({
          .modeling_object_id = native_id,
          .identity = identity->second,
          .material_slot = component->material_slot,
          .mesh_id = mesh_component->mesh,
          .mesh = clone_mesh_asset_payload(*mesh),
      });
    }
    return ModelingSdkProjectionSnapshot(std::move(snapshot));
  }

  [[nodiscard]] static std::shared_ptr<ModelingSdkProjectionSnapshot::Impl>
  clone_host_checkpoint_impl(
      const ModelingSdkProjectionSnapshot &snapshot) {
    if (!snapshot.valid()) {
      return {};
    }
    auto checkpoint =
        std::make_shared<ModelingSdkProjectionSnapshot::Impl>();
    checkpoint->session =
        snapshot.impl_->session.current_workspace_checkpoint();
    checkpoint->object_ids = snapshot.impl_->object_ids;
    checkpoint->projected_objects = snapshot.impl_->projected_objects;
    checkpoint->next_native_object_id =
        snapshot.impl_->next_native_object_id;
    checkpoint->active_native_object_id =
        snapshot.impl_->active_native_object_id;
    checkpoint->selection_mode = snapshot.impl_->selection_mode;
    checkpoint->persistent_content_revision =
        snapshot.impl_->persistent_content_revision;
    checkpoint->mode_revision = snapshot.impl_->mode_revision;
    return checkpoint;
  }

  [[nodiscard]] ModelingSdkProjectionSnapshot rebase_snapshot_host_projection(
      const ModelingSdkProjectionSnapshot &snapshot) const {
    auto rebased = clone_host_checkpoint_impl(snapshot);
    if (!rebased) {
      return {};
    }

    std::unordered_set<int> projected_ids;
    projected_ids.reserve(rebased->projected_objects.size());
    for (const SdkProjectedObjectSnapshot &projected :
         rebased->projected_objects) {
      projected_ids.insert(projected.modeling_object_id);
    }
    const auto current_identities =
        object_map_.capture_scene_identities_for_modeling_objects(
            scene_, projected_ids);
    for (SdkProjectedObjectSnapshot &projected :
         rebased->projected_objects) {
      const auto identity =
          current_identities.find(projected.modeling_object_id);
      if (identity == current_identities.end() ||
          !scene_.exists(identity->second.entity)) {
        continue;
      }
      const ModelingObjectComponent *component =
          scene_.modeling_object(identity->second.entity);
      const MeshComponent *mesh_component =
          scene_.mesh(identity->second.entity);
      const MeshAsset *mesh = mesh_component != nullptr
                                  ? assets_.mesh(mesh_component->mesh)
                                  : nullptr;
      if (component == nullptr || mesh_component == nullptr || mesh == nullptr) {
        continue;
      }
      projected.identity = identity->second;
      projected.material_slot = component->material_slot;
      projected.mesh_id = mesh_component->mesh;
      projected.mesh = clone_mesh_asset_payload(*mesh);
    }
    return ModelingSdkProjectionSnapshot(std::move(rebased));
  }

  [[nodiscard]] ModelingSdkProjectionSnapshot
  capture_active_preview_base_snapshot() const {
    if (!preview_base_snapshot_.has_value()) {
      return {};
    }
    return ModelingSdkProjectionSnapshot(
        clone_host_checkpoint_impl(*preview_base_snapshot_));
  }

  [[nodiscard]] ModelingCommandResult
  restore_snapshot(const ModelingSdkProjectionSnapshot &snapshot) {
    return restore_snapshot(snapshot, true);
  }

  [[nodiscard]] ModelingCommandResult
  restore_snapshot_exact(const ModelingSdkProjectionSnapshot &snapshot) {
    return restore_snapshot(snapshot, false);
  }

  [[nodiscard]] ModelingCommandResult
  restore_snapshot(const ModelingSdkProjectionSnapshot &snapshot,
                   bool history_transition) {
    if (!snapshot.valid()) {
      return failure("SDK projection restore point is invalid.");
    }

    const int host_identity_high_water = next_available_native_object_id();
    const auto object_ids_before = object_ids_;
    const ModelingObjectRuntimeMap object_map_before = object_map_;
    const int next_native_before = next_native_object_id_;
    const int active_before = active_native_object_id_;
    const ModelingSelectionMode selection_mode_before = selection_mode_;
    const std::uint64_t persistent_content_revision_before =
        persistent_content_revision_;
    const std::uint64_t mode_revision_before = mode_revision_;
    const AssetRegistryProjectionCheckpoint assets_before =
        assets_.capture_projection_checkpoint();
    const SceneProjectionCheckpoint scene_before =
        scene_.capture_projection_checkpoint();
    std::vector<EntityId> created_entities;
    // The SDK checkpoint is installed only after every fallible host
    // projection step below. Rollback therefore keeps the still-live SDK
    // state in place instead of cloning its accumulated local history.
    const auto rollback = [&](std::string message) {
      object_ids_ = object_ids_before;
      object_map_ = object_map_before;
      next_native_object_id_ = next_native_before;
      active_native_object_id_ = active_before;
      selection_mode_ = selection_mode_before;
      persistent_content_revision_ = persistent_content_revision_before;
      mode_revision_ = mode_revision_before;
      (void)assets_.restore_projection_checkpoint(assets_before);
      (void)scene_.restore_projection_checkpoint(scene_before,
                                                 created_entities);
      return failure(std::move(message));
    };

    std::unordered_set<int> restored_ids;
    restored_ids.reserve(snapshot.impl_->object_ids.size());
    for (const auto &[native_id, object_id] : snapshot.impl_->object_ids) {
      (void)object_id;
      restored_ids.insert(native_id);
    }
    const std::unordered_map<int, ModelingObjectSceneIdentity>
        live_scene_identities =
            history_transition
                ? std::unordered_map<int, ModelingObjectSceneIdentity>{}
                : object_map_.capture_scene_identities_for_modeling_objects(
                      scene_, restored_ids);

    std::vector<AssetId> displaced_meshes;
    for (const auto &[native_id, object_id] : object_ids_) {
      (void)object_id;
      if (restored_ids.contains(native_id)) {
        continue;
      }
      const std::optional<EntityId> entity =
          object_map_.entity_for_modeling_object(native_id);
      if (entity.has_value() && scene_.exists(*entity)) {
        const ModelingObjectComponent *component =
            scene_.modeling_object(*entity);
        if (component != nullptr && component->sdk_owned) {
          if (const MeshComponent *mesh = scene_.mesh(*entity)) {
            displaced_meshes.push_back(mesh->mesh);
          }
          (void)mesh_sync_.destroy_projected_entity(scene_, *entity);
          object_map_.unbind_object(native_id);
        }
      } else {
        object_map_.unbind_object(native_id);
      }
    }

    std::unordered_map<AssetId, MeshAsset> restored_meshes;
    restored_meshes.reserve(snapshot.impl_->projected_objects.size());
    for (const SdkProjectedObjectSnapshot &projected :
         snapshot.impl_->projected_objects) {
      restored_meshes.emplace(projected.mesh_id,
                              clone_mesh_asset_payload(projected.mesh));
    }
    if (!mesh_sync_.restore_history_mesh_assets(assets_, restored_meshes)) {
      return rollback("SDK history mesh assets could not be restored.");
    }

    for (const SdkProjectedObjectSnapshot &projected :
         snapshot.impl_->projected_objects) {
      ModelingObjectSceneIdentity restored_identity = projected.identity;
      if (!history_transition) {
        const auto live =
            live_scene_identities.find(projected.modeling_object_id);
        if (live != live_scene_identities.end()) {
          const std::optional<MeshComponent> baseline_mesh =
              restored_identity.mesh;
          restored_identity = live->second;
          if (baseline_mesh.has_value()) {
            MeshComponent mesh = restored_identity.mesh.value_or(*baseline_mesh);
            mesh.mesh = projected.mesh_id;
            restored_identity.mesh = mesh;
          }
          restored_identity.material_slot = projected.material_slot;
        }
      }
      const std::optional<EntityId> current_entity =
          object_map_.entity_for_modeling_object(
              projected.modeling_object_id);
      if (current_entity.has_value() && scene_.exists(*current_entity)) {
        if (const MeshComponent *current_mesh = scene_.mesh(*current_entity);
            current_mesh != nullptr &&
            current_mesh->mesh != projected.mesh_id) {
          displaced_meshes.push_back(current_mesh->mesh);
        }
      }
      EntityId entity = restored_identity.entity;
      if (!scene_.exists(entity)) {
        entity = mesh_sync_.restore_projected_mesh_entity(
            scene_, restored_identity);
        if (entity == kInvalidEntityId) {
          return rollback("SDK history scene entity could not be recreated.");
        }
        created_entities.push_back(entity);
      }
      mesh_sync_.apply_scene_identity_to_entity(scene_, entity,
                                                restored_identity);
      if (!mesh_sync_.bind_projected_object(
              scene_, entity, projected.modeling_object_id,
              projected.material_slot, true, projected.mesh_id,
              restored_identity.local_frame_version)) {
        return rollback("SDK history scene binding could not be restored.");
      }
      (void)mesh_sync_.set_projected_material(
          scene_, entity,
          restored_identity.material.has_value()
              ? &*restored_identity.material
              : nullptr);
      object_map_.bind(projected.modeling_object_id, entity);
      object_map_.remember_scene_entity(projected.modeling_object_id, scene_,
                                        entity);
    }

    mesh_sync_.remove_unreferenced_history_mesh_assets(
        scene_, assets_, displaced_meshes);
    if (history_transition) {
      session_.restore_snapshot_as_history_transition(snapshot.impl_->session);
    } else {
      session_.restore_snapshot_preserving_identity_high_water(
          snapshot.impl_->session);
    }
    object_ids_ = snapshot.impl_->object_ids;
    next_native_object_id_ =
        std::max({next_native_object_id_, snapshot.impl_->next_native_object_id,
                  host_identity_high_water});
    active_native_object_id_ = snapshot.impl_->active_native_object_id;
    selection_mode_ = snapshot.impl_->selection_mode;
    if (history_transition) {
      if (persistent_content_revision_ !=
          snapshot.impl_->persistent_content_revision) {
        ++persistent_content_revision_;
      }
    } else {
      persistent_content_revision_ =
          snapshot.impl_->persistent_content_revision;
    }
    mode_revision_ = snapshot.impl_->mode_revision;
    clear_preview_checkpoints();
    add_selection_to_scene();
    return {.ok = true, .changed = true};
  }

  void set_failure_point_for_tests(ModelingSdkProjectionFailurePoint point) {
    failure_point_ = point;
  }

  [[nodiscard]] ModelingSdkProjectionStats stats() const noexcept {
    return stats_;
  }

private:
  [[nodiscard]] PreviewSelectionEditState
  suspend_operation_preview_for_selection_edit() {
    PreviewSelectionEditState state;
    if (!preview_active()) {
      return state;
    }
    state.active = true;
    if (!active_preview_operation_.has_value() ||
        !active_preview_settings_.has_value() ||
        !preview_base_snapshot_.has_value()) {
      state.ok = false;
      state.message =
          "Component selection cannot change during this SDK preview.";
      return state;
    }
    state.operation = *active_preview_operation_;
    state.settings = *active_preview_settings_;
    const ModelingSdkProjectionSnapshot base = *preview_base_snapshot_;
    ModelingCommandResult restored = restore_snapshot_exact(base);
    if (!restored.ok) {
      state.ok = false;
      state.message = restored.message;
    }
    return state;
  }

  [[nodiscard]] ModelingCommandResult finish_operation_preview_selection_edit(
      const PreviewSelectionEditState &state, ModelingCommandResult result) {
    if (!state.active) {
      return result;
    }
    const ModelingCommandResult rebuilt =
        begin_operation_preview(state.operation, state.settings);
    if (!rebuilt.ok) {
      return {.ok = false,
              .changed = result.changed,
              .message = rebuilt.message.empty()
                             ? "SDK operation preview could not be rebuilt."
                             : rebuilt.message};
    }
    return result;
  }

  [[nodiscard]] std::vector<ObjectId> selected_sdk_objects() const {
    std::vector<ObjectId> selected;
    const std::vector<quader::modeling::SessionObjectSummary> objects =
        session_.objects();
    for (const quader::modeling::SessionObjectSummary &object : objects) {
      if (object.selected) {
        selected.push_back(object.id);
      }
    }
    return selected;
  }

  [[nodiscard]] bool publish_new_object(
      ObjectId object_id, const ModelingObjectSceneIdentity &source_identity,
      std::uint32_t material_slot, int &native_id, EntityId &entity,
      bool share_source_mesh = false,
      std::optional<int> reserved_native_id = std::nullopt) {
    AssetId mesh_id = share_source_mesh && source_identity.mesh.has_value()
                          ? source_identity.mesh->mesh
                          : kInvalidAssetId;
    if (mesh_id == kInvalidAssetId || assets_.mesh(mesh_id) == nullptr) {
      Result<PolygonDocument> document = session_.document_copy(object_id);
      if (!document.ok()) {
        return false;
      }
      ++stats_.mesh_compile_count;
      std::optional<MeshAsset> projected =
          MeshProjectionService::compile_sdk_document_mesh(document.value());
      if (!projected.has_value()) {
        return false;
      }
      projected->name = source_identity.name;
      const AssetRegistryValidationResult validation =
          assets_.validate_mesh(*projected);
      if (!validation.ok) {
        return false;
      }
      mesh_id = mesh_sync_.install_projected_mesh(assets_, std::move(*projected));
      if (mesh_id == kInvalidAssetId) {
        return false;
      }
      ++stats_.asset_publication_count;
    }
    entity =
        mesh_sync_.create_projected_mesh_entity(scene_, source_identity.name);
    if (entity == kInvalidEntityId) {
      return false;
    }
    const std::optional<int> allocated_native_id =
        reserved_native_id.has_value()
            ? reserved_native_id
            : reserve_native_object_ids(1U);
    if (!allocated_native_id.has_value()) {
      return false;
    }
    native_id = *allocated_native_id;
    if (native_id == kInvalidModelingObjectId ||
        native_id >= std::numeric_limits<int>::max() ||
        !mesh_sync_.bind_projected_object(
            scene_, entity, native_id, material_slot, true, mesh_id,
            source_identity.local_frame_version)) {
      return false;
    }
    mesh_sync_.apply_scene_identity_to_duplicate_entity(scene_, entity,
                                                        source_identity);
    object_ids_.emplace(native_id, object_id);
    object_map_.bind(native_id, entity);
    object_map_.remember_scene_entity(native_id, scene_, entity);
    return true;
  }

  [[nodiscard]] ModelingSdkDuplicateBatchResult duplicate_objects(
      std::span<const int> source_modeling_object_ids,
      std::size_t copies_per_source, bool select_sources,
      bool preview_publication = false,
      bool share_source_mesh_assets = false) {
    const auto failure_result = [](std::string message, bool changed = false) {
      return ModelingSdkDuplicateBatchResult{
          .command = {.ok = false,
                      .changed = changed,
                      .message = std::move(message)},
      };
    };
    if (selection_mode_ != ModelingSelectionMode::Object &&
        selection_mode_ != ModelingSelectionMode::Group) {
      return failure_result("Duplicate Meshes requires object selection mode.");
    }
    if (source_modeling_object_ids.empty() || copies_per_source == 0U) {
      return failure_result("Duplicate Meshes needs source objects and copies.");
    }
    if (preview_active() && !preview_publication) {
      return failure_result("Commit or cancel the active preview first.");
    }

    std::vector<ObjectId> sources;
    sources.reserve(source_modeling_object_ids.size());
    std::unordered_set<std::uint64_t> unique_sources;
    unique_sources.reserve(source_modeling_object_ids.size());
    for (int native_id : source_modeling_object_ids) {
      const std::optional<ObjectId> source = sdk_object(native_id);
      if (!source.has_value() ||
          !unique_sources.emplace(sdk_object_key(*source)).second) {
        return failure_result("Duplicate source object is not live or unique.");
      }
      sources.push_back(*source);
    }
    if (copies_per_source >
        std::numeric_limits<std::size_t>::max() / sources.size()) {
      return failure_result("Duplicate object count is too large.");
    }

    const std::size_t undo_mark = session_.undo_depth();
    const quader::modeling::ModelingSession session_before = session_;
    const auto object_ids_before = object_ids_;
    const ModelingObjectRuntimeMap object_map_before = object_map_;
    const int next_native_before = next_native_object_id_;
    const int active_before = active_native_object_id_;
    const std::uint64_t revision_before = persistent_content_revision_;
    const ModelingSdkProjectionStats stats_before = stats_;
    const AssetRegistryProjectionCheckpoint assets_before =
        assets_.capture_projection_checkpoint();
    const SceneProjectionCheckpoint scene_before =
        scene_.capture_projection_checkpoint();
    std::vector<EntityId> created_entities;
    const auto rollback = [&](std::string message) {
      session_.restore_snapshot_preserving_identity_high_water(session_before);
      object_ids_ = object_ids_before;
      object_map_ = object_map_before;
      next_native_object_id_ = next_native_before;
      active_native_object_id_ = active_before;
      persistent_content_revision_ = revision_before;
      stats_ = stats_before;
      (void)assets_.restore_projection_checkpoint(assets_before);
      (void)scene_.restore_projection_checkpoint(scene_before,
                                                 created_entities);
      return failure_result(std::move(message));
    };

    /// Retains native scene facts needed to publish one duplicated SDK object.
    struct DuplicateSourceProjection {
      ModelingObjectSceneIdentity identity;
      std::uint32_t material_slot = 0U;
    };
    std::vector<DuplicateSourceProjection> source_projections;
    source_projections.reserve(sources.size());
    for (std::size_t source_index = 0; source_index < sources.size();
         ++source_index) {
      const int source_native = source_modeling_object_ids[source_index];
      const std::optional<EntityId> source_entity =
          object_map_.entity_for_modeling_object(source_native);
      if (!source_entity.has_value() || !scene_.exists(*source_entity)) {
        return rollback("Duplicate source scene identity is not live.");
      }
      object_map_.remember_scene_entity(source_native, scene_, *source_entity);
      const std::optional<ModelingObjectSceneIdentity> identity =
          object_map_.scene_identity_for_modeling_object(source_native);
      const ModelingObjectComponent *component =
          scene_.modeling_object(*source_entity);
      if (!identity.has_value() || component == nullptr) {
        return rollback("Duplicate source scene identity is incomplete.");
      }
      source_projections.push_back({
          .identity = *identity,
          .material_slot = component->material_slot,
      });
    }

    const std::size_t duplicate_count = sources.size() * copies_per_source;
    std::vector<ObjectId> expanded_sources;
    expanded_sources.reserve(duplicate_count);
    for (const ObjectId source : sources) {
      for (std::size_t copy = 0; copy < copies_per_source; ++copy) {
        expanded_sources.push_back(source);
      }
    }
    Result<std::vector<ObjectId>> duplicated_batch =
        session_.duplicate_objects(expanded_sources, {});
    if (!duplicated_batch.ok()) {
      return rollback(duplicated_batch.error_message());
    }
    std::vector<ObjectId> duplicated_objects =
        std::move(duplicated_batch).value();
    if (duplicated_objects.size() != duplicate_count) {
      return rollback("SDK duplicate batch returned incomplete results.");
    }
    ++stats_.sdk_duplicate_batch_mutation_count;
    stats_.sdk_duplicate_object_count += duplicated_objects.size();

    const std::optional<int> first_native_id =
        reserve_native_object_ids(duplicate_count);
    if (!first_native_id.has_value()) {
      return rollback("Native modeling object IDs are exhausted.");
    }

    ModelingSdkDuplicateBatchResult result{
        .command = {.ok = true,
                    .changed = true,
                    .message = "Duplicated selected meshes."},
    };
    result.duplicates.reserve(duplicate_count);
    std::vector<ObjectId> selection = select_sources ? sources
                                                     : std::vector<ObjectId>{};
    selection.reserve(selection.size() + duplicate_count);
    std::size_t duplicate_index = 0U;
    for (std::size_t source_index = 0; source_index < sources.size();
         ++source_index) {
      for (std::size_t copy = 0; copy < copies_per_source; ++copy) {
        const ObjectId duplicated = duplicated_objects[duplicate_index];
        int native_id = kInvalidModelingObjectId;
        EntityId entity = kInvalidEntityId;
        const bool published = publish_new_object(
            duplicated, source_projections[source_index].identity,
            source_projections[source_index].material_slot, native_id, entity,
            share_source_mesh_assets,
            *first_native_id + static_cast<int>(duplicate_index));
        if (entity != kInvalidEntityId) {
          created_entities.push_back(entity);
        }
        if (!published) {
          return rollback("SDK duplicate host projection failed.");
        }
        ModelingObjectRecord record{
            .modeling_object_id = native_id,
            .entity = entity,
            .name = source_projections[source_index].identity.name,
        };
        if (const MeshComponent *mesh = scene_.mesh(entity)) {
          record.mesh = mesh->mesh;
        }
        result.duplicates.push_back(std::move(record));
        selection.push_back(duplicated);
        active_native_object_id_ = native_id;
        ++duplicate_index;
      }
    }
    Result<OperationResult> selected =
        session_.apply_object_selection(selection, SelectionEdit::Replace);
    if (!selected.ok()) {
      return rollback(selected.error_message());
    }
    if (fails(ModelingSdkProjectionFailurePoint::AfterSdkMutation) ||
        fails(ModelingSdkProjectionFailurePoint::BeforeCommit)) {
      return rollback("Injected failure during SDK duplicate batch.");
    }
    session_.squash_undo_since(undo_mark);
    if (!preview_publication) {
      ++persistent_content_revision_;
    }
    ++stats_.scene_publication_count;
    add_selection_to_scene();
    return result;
  }

  [[nodiscard]] ModelingSdkClipboardCopyResult
  copy_selection_to_clipboard() {
    ModelingSdkClipboardCopyResult result;
    const std::vector<quader::modeling::SessionObjectSummary> objects =
        session_.objects();
    for (const quader::modeling::SessionObjectSummary &object : objects) {
      if (!object.selected) {
        continue;
      }
      const std::optional<int> native_id = native_object(object.id);
      if (!native_id.has_value()) {
        result.command = failure(
            "Selected SDK clipboard object has no native projection identity.");
        return result;
      }
      result.source_modeling_object_ids.push_back(*native_id);
    }

    Result<OperationResult> copied = session_.copy_selection_to_clipboard();
    if (!copied.ok()) {
      result.source_modeling_object_ids.clear();
      result.command = failure(copied.error_message());
      return result;
    }
    result.command = {
        .ok = true,
        .changed = copied.value().changed,
        .message = copied.value().message,
    };
    return result;
  }

  [[nodiscard]] ModelingSdkDuplicateBatchResult paste_clipboard(
      std::span<const ModelingObjectSceneIdentity> source_identities) {
    const auto failed = [](std::string message, bool changed = false) {
      return ModelingSdkDuplicateBatchResult{
          .command = {.ok = false,
                      .changed = changed,
                      .message = std::move(message)},
      };
    };
    if (!session_.clipboard_available()) {
      return failed("Copy one or more meshes before pasting.");
    }
    if (source_identities.size() != session_.clipboard_object_count()) {
      return failed("Clipboard host identity payload is incomplete.");
    }
    if (preview_active()) {
      return failed("Commit or cancel the active preview before pasting.");
    }

    const quader::modeling::ModelingSession session_before = session_;
    const auto object_ids_before = object_ids_;
    const ModelingObjectRuntimeMap object_map_before = object_map_;
    const int next_native_before = next_native_object_id_;
    const int active_before = active_native_object_id_;
    const ModelingSelectionMode mode_before = selection_mode_;
    const std::uint64_t revision_before = persistent_content_revision_;
    const ModelingSdkProjectionStats stats_before = stats_;
    const AssetRegistryProjectionCheckpoint assets_before =
        assets_.capture_projection_checkpoint();
    const SceneProjectionCheckpoint scene_before =
        scene_.capture_projection_checkpoint();
    std::vector<EntityId> created_entities;
    const auto rollback = [&](std::string message) {
      session_.restore_snapshot_preserving_identity_high_water(session_before);
      object_ids_ = object_ids_before;
      object_map_ = object_map_before;
      next_native_object_id_ = next_native_before;
      active_native_object_id_ = active_before;
      selection_mode_ = mode_before;
      persistent_content_revision_ = revision_before;
      stats_ = stats_before;
      (void)assets_.restore_projection_checkpoint(assets_before);
      (void)scene_.restore_projection_checkpoint(scene_before,
                                                 created_entities);
      return failed(std::move(message));
    };

    Result<quader::modeling::ClipboardPasteResult> pasted =
        session_.paste_clipboard({});
    if (!pasted.ok()) {
      return rollback(pasted.error_message());
    }
    if (pasted.value().objects.size() != source_identities.size()) {
      return rollback("SDK clipboard paste returned an unexpected object map.");
    }

    ModelingSdkDuplicateBatchResult result{
        .command = {.ok = true,
                    .changed = pasted.value().operation.changed,
                    .message = pasted.value().operation.message},
    };
    result.duplicates.reserve(pasted.value().objects.size());
    const std::vector<quader::modeling::SessionObjectSummary> summaries =
        session_.objects();
    std::unordered_map<std::uint64_t,
                       const quader::modeling::SessionObjectSummary *>
        summary_by_object;
    summary_by_object.reserve(summaries.size());
    for (const quader::modeling::SessionObjectSummary &summary : summaries) {
      summary_by_object.emplace(sdk_object_key(summary.id), &summary);
    }
    const std::optional<int> first_native_id =
        reserve_native_object_ids(pasted.value().objects.size());
    if (!first_native_id.has_value()) {
      return rollback("Native modeling object IDs are exhausted.");
    }
    for (std::size_t index = 0; index < pasted.value().objects.size(); ++index) {
      ModelingObjectSceneIdentity identity = source_identities[index];
      const auto summary =
          summary_by_object.find(sdk_object_key(pasted.value().objects[index]));
      if (summary == summary_by_object.end()) {
        return rollback("SDK clipboard object summary is unavailable.");
      }
      if (identity.name.empty()) {
        identity.name = summary->second->name;
      }
      const std::uint32_t material_slot =
          identity.material_slot;
      int native_id = kInvalidModelingObjectId;
      EntityId entity = kInvalidEntityId;
      const bool published = publish_new_object(
          pasted.value().objects[index], identity, material_slot, native_id,
          entity, false, *first_native_id + static_cast<int>(index));
      if (entity != kInvalidEntityId) {
        created_entities.push_back(entity);
      }
      if (!published) {
        return rollback("SDK clipboard host projection failed.");
      }
      ModelingObjectRecord record{
          .modeling_object_id = native_id,
          .entity = entity,
          .name = identity.name,
      };
      if (const MeshComponent *mesh = scene_.mesh(entity)) {
        record.mesh = mesh->mesh;
      }
      result.duplicates.push_back(std::move(record));
      active_native_object_id_ = native_id;
    }

    if (fails(ModelingSdkProjectionFailurePoint::AfterSdkMutation) ||
        fails(ModelingSdkProjectionFailurePoint::BeforeCommit)) {
      return rollback("Injected failure during SDK clipboard publication.");
    }
    if (selection_mode_ != ModelingSelectionMode::Object) {
      selection_mode_ = ModelingSelectionMode::Object;
      ++mode_revision_;
    }
    ++persistent_content_revision_;
    ++stats_.scene_publication_count;
    add_selection_to_scene();
    return result;
  }

  [[nodiscard]] bool clipboard_available() const {
    return session_.clipboard_available();
  }

  [[nodiscard]] std::size_t clipboard_object_count() const {
    return session_.clipboard_object_count();
  }

  [[nodiscard]] ModelingCommandResult
  duplicate_selected_objects(QVec3 geometry_offset,
                             const DocumentMutation &duplicate_mutation = {},
                             std::string_view success_message =
                                 "Duplicated selected meshes.",
                             bool preview_publication = false,
                             std::string_view duplicate_name = {}) {
    if (selection_mode_ != ModelingSelectionMode::Object &&
        selection_mode_ != ModelingSelectionMode::Group) {
      return failure("Duplicate Meshes requires object selection mode.");
    }
    if (!finite(geometry_offset)) {
      return failure("Duplicate offset must be finite.");
    }
    const std::vector<ObjectId> sources = selected_sdk_objects();
    if (sources.empty()) {
      return failure("Select one or more meshes to duplicate.");
    }
    if (preview_active() && !preview_publication) {
      return failure("Commit or cancel the active preview first.");
    }

    const std::size_t undo_mark = session_.undo_depth();
    const quader::modeling::ModelingSession session_before = session_;
    const auto object_ids_before = object_ids_;
    const ModelingObjectRuntimeMap object_map_before = object_map_;
    const int next_native_before = next_native_object_id_;
    const int active_before = active_native_object_id_;
    const ModelingSelectionMode mode_before = selection_mode_;
    const std::uint64_t revision_before = persistent_content_revision_;
    const ModelingSdkProjectionStats stats_before = stats_;
    const AssetRegistryProjectionCheckpoint assets_before =
        assets_.capture_projection_checkpoint();
    const SceneProjectionCheckpoint scene_before =
        scene_.capture_projection_checkpoint();
    std::vector<EntityId> created_entities;

    const auto rollback = [&](std::string message) {
      session_.restore_snapshot_preserving_identity_high_water(session_before);
      object_ids_ = object_ids_before;
      object_map_ = object_map_before;
      next_native_object_id_ = next_native_before;
      active_native_object_id_ = active_before;
      selection_mode_ = mode_before;
      persistent_content_revision_ = revision_before;
      stats_ = stats_before;
      (void)assets_.restore_projection_checkpoint(assets_before);
      (void)scene_.restore_projection_checkpoint(scene_before,
                                                 created_entities);
      return failure(std::move(message));
    };

    Result<std::vector<ObjectId>> duplicated_batch = [&]() {
      QDR_PROFILE_SCOPE("SDK duplicate selected objects atomic batch");
      return session_.duplicate_objects(sources, to_sdk(geometry_offset),
                                        std::string(duplicate_name));
    }();
    if (!duplicated_batch.ok()) {
      return rollback(duplicated_batch.error_message());
    }
    std::vector<ObjectId> duplicates =
        std::move(duplicated_batch).value();
    if (duplicates.size() != sources.size()) {
      return rollback("SDK duplicate batch returned incomplete results.");
    }
    ++stats_.sdk_duplicate_batch_mutation_count;
    stats_.sdk_duplicate_object_count += duplicates.size();
    const std::optional<int> first_native_id =
        reserve_native_object_ids(duplicates.size());
    if (!first_native_id.has_value()) {
      return rollback("Native modeling object IDs are exhausted.");
    }

    QDR_PROFILE_SCOPE("SDK duplicate publish host objects");
    for (std::size_t source_index = 0; source_index < sources.size();
         ++source_index) {
      const ObjectId source = sources[source_index];
      const ObjectId duplicated = duplicates[source_index];
      const std::optional<int> source_native = native_object(source);
      const std::optional<EntityId> source_entity =
          source_native.has_value()
              ? object_map_.entity_for_modeling_object(*source_native)
              : std::nullopt;
      if (!source_native.has_value() || !source_entity.has_value() ||
          !scene_.exists(*source_entity)) {
        return rollback("Duplicate source scene identity is not live.");
      }
      object_map_.remember_scene_entity(*source_native, scene_, *source_entity);
      const std::optional<ModelingObjectSceneIdentity> source_identity =
          object_map_.scene_identity_for_modeling_object(*source_native);
      const ModelingObjectComponent *source_component =
          scene_.modeling_object(*source_entity);
      if (!source_identity.has_value() || source_component == nullptr) {
        return rollback("Duplicate source scene identity is incomplete.");
      }
      ModelingObjectSceneIdentity duplicate_identity = *source_identity;
      if (!duplicate_name.empty()) {
        duplicate_identity.name = std::string(duplicate_name);
      }

      Result<PolygonDocument> document =
          session_.document_copy(duplicated);
      if (!document.ok()) {
        return rollback(document.error_message());
      }
      if (duplicate_mutation) {
        Result<OperationResult> mutated = duplicate_mutation(document.value());
        if (!mutated.ok()) {
          return rollback(mutated.error_message());
        }
        if (!mutated.value().success) {
          return rollback(mutated.value().message.empty()
                              ? "SDK duplicate mutation failed."
                              : mutated.value().message);
        }
        if (mutated.value().changed) {
          Result<OperationResult> committed = session_.commit_document(
              duplicated, std::move(document).value(),
              std::move(mutated).value());
          if (!committed.ok()) {
            return rollback(committed.error_message());
          }
          document = session_.document_copy(duplicated);
          if (!document.ok()) {
            return rollback(document.error_message());
          }
        }
      }
      AssetId mesh_id = kInvalidAssetId;
      if (mesh_id == kInvalidAssetId || assets_.mesh(mesh_id) == nullptr) {
        ++stats_.mesh_compile_count;
        std::optional<MeshAsset> projected =
            MeshProjectionService::compile_sdk_document_mesh(document.value());
        if (!projected.has_value()) {
          return rollback("SDK duplicate mesh projection failed.");
        }
        projected->name = duplicate_identity.name;
        const AssetRegistryValidationResult validation =
            assets_.validate_mesh(*projected);
        if (!validation.ok) {
          return rollback(validation.message.empty()
                              ? "SDK duplicate mesh projection is invalid."
                              : validation.message);
        }
        mesh_id =
            mesh_sync_.install_projected_mesh(assets_, std::move(*projected));
        if (mesh_id == kInvalidAssetId) {
          return rollback("SDK duplicate mesh asset could not be installed.");
        }
        ++stats_.asset_publication_count;
      }

      const EntityId entity = mesh_sync_.create_projected_mesh_entity(
          scene_, duplicate_identity.name);
      if (entity == kInvalidEntityId) {
        return rollback("SDK duplicate scene entity could not be created.");
      }
      created_entities.push_back(entity);
      const int native_id =
          *first_native_id + static_cast<int>(source_index);
      if (native_id == kInvalidModelingObjectId ||
          native_id >= std::numeric_limits<int>::max() ||
          !mesh_sync_.bind_projected_object(
              scene_, entity, native_id, source_component->material_slot, true,
              mesh_id, duplicate_identity.local_frame_version)) {
        return rollback("SDK duplicate scene binding could not be installed.");
      }
      mesh_sync_.apply_scene_identity_to_duplicate_entity(
          scene_, entity, duplicate_identity);
      object_ids_.emplace(native_id, duplicated);
      object_map_.bind(native_id, entity);
      object_map_.remember_scene_entity(native_id, scene_, entity);
      active_native_object_id_ = native_id;
    }

    Result<OperationResult> selected =
        session_.apply_object_selection(duplicates, SelectionEdit::Replace);
    if (!selected.ok()) {
      return rollback(selected.error_message());
    }
    if (fails(ModelingSdkProjectionFailurePoint::AfterSdkMutation) ||
        fails(ModelingSdkProjectionFailurePoint::BeforeCommit)) {
      return rollback("Injected failure during SDK duplicate publication.");
    }
    session_.squash_undo_since(undo_mark);
    if (!preview_publication) {
      ++persistent_content_revision_;
    }
    ++stats_.scene_publication_count;
    add_selection_to_scene();
    return {.ok = true,
            .changed = true,
            .message = std::string(success_message)};
  }

  [[nodiscard]] ModelingCommandResult mirror_selected_objects_across_plane(
      QVec3 origin, QVec3 normal) {
    if (!finite(origin) || !finite(normal)) {
      return failure("Mirror plane must be finite.");
    }
    return duplicate_selected_objects(
        {},
        [origin, normal](PolygonDocument &document) {
          return document.reflect_across_plane(to_sdk(origin), to_sdk(normal));
        },
        "Mirrored selected meshes.");
  }

  [[nodiscard]] ModelingCommandResult mirror_objects_across_planes(
      std::span<const ModelingSdkPlaneCutRequest> requests) {
    if (requests.empty()) {
      return failure("Mirror needs selected mesh objects.");
    }
    const std::size_t undo_mark = session_.undo_depth();
    const ModelingSdkProjectionSnapshot base = capture_snapshot(true);
    std::vector<int> sources;
    sources.reserve(requests.size());
    for (const ModelingSdkPlaneCutRequest &request : requests) {
      sources.push_back(request.modeling_object_id);
    }
    ModelingSdkDuplicateBatchResult duplicated =
        duplicate_objects(sources, 1U, false, true);
    if (!duplicated.command.ok ||
        duplicated.duplicates.size() != requests.size()) {
      return duplicated.command;
    }
    /// Stores one validated object-local reflection request.
    struct Reflection {
      ObjectId object;
      Vec3 origin;
      Vec3 normal;
    };
    std::vector<Reflection> reflections;
    std::vector<ObjectId> objects;
    reflections.reserve(requests.size());
    objects.reserve(requests.size());
    for (std::size_t index = 0; index < requests.size(); ++index) {
      const std::optional<ObjectId> object = sdk_object(
          duplicated.duplicates[index].modeling_object_id);
      if (!object.has_value()) {
        (void)restore_snapshot_exact(base);
        return failure("Mirror duplicate projection mapping was lost.");
      }
      const QVec3 first = requests[index].local_plane_points[0];
      const QVec3 second = requests[index].local_plane_points[1];
      const QVec3 third = requests[index].local_plane_points[2];
      const QVec3 normal = cross(second - first, third - first);
      if (!finite(first) || !finite(normal) ||
          length_squared(normal) <= 0.0000001F) {
        (void)restore_snapshot_exact(base);
        return failure("Mirror plane is degenerate.");
      }
      reflections.push_back({
          .object = *object,
          .origin = to_sdk(first),
          .normal = to_sdk(normal),
      });
      objects.push_back(*object);
    }
    ModelingCommandResult reflected = mutate_documents_by_object(
        objects,
        [reflections = std::move(reflections)](
            ObjectId object, PolygonDocument &document) {
          const auto found = std::ranges::find_if(
              reflections,
              [object](const Reflection &candidate) {
                return candidate.object == object;
              });
          if (found == reflections.end()) {
            return Result<OperationResult>::failure(
                quader::modeling::make_error(
                    quader::modeling::ErrorCode::InvalidId,
                    "Mirror duplicate target is not live."));
          }
          return document.reflect_across_plane(found->origin, found->normal);
        },
        "Mirror selected meshes failed.", true);
    if (!reflected.ok) {
      const std::string message = reflected.message;
      const ModelingCommandResult restored = restore_snapshot_exact(base);
      return {.ok = false,
              .changed = !restored.ok,
              .message = restored.ok
                             ? message
                             : message + " The pre-mirror workspace could "
                                         "not be restored."};
    }
    session_.squash_undo_since(undo_mark);
    ++persistent_content_revision_;
    add_selection_to_scene();
    return {.ok = true,
            .changed = true,
            .message = "Mirrored selected meshes."};
  }

  [[nodiscard]] ModelingCommandResult
  flip_selected_objects(ModelingFlipOrientation orientation) {
    if (selection_mode_ != ModelingSelectionMode::Object &&
        selection_mode_ != ModelingSelectionMode::Group) {
      return failure("Flip works only in object/mesh selection mode.");
    }
    const std::vector<ObjectId> selected = selected_sdk_objects();
    return mutate_documents(
        selected,
        [orientation](PolygonDocument &document) {
          Result<quader::modeling::AuthoredPolygonPayload> payload =
              document.authored_payload();
          if (!payload.ok()) {
            return Result<OperationResult>::failure(payload.error());
          }
          if (payload.value().positions.empty()) {
            return Result<OperationResult>::failure(quader::modeling::make_error(
                quader::modeling::ErrorCode::ValidationFailed,
                "Flip needs selected mesh bounds."));
          }
          Vec3 minimum = payload.value().positions.front();
          Vec3 maximum = minimum;
          for (Vec3 position : payload.value().positions) {
            minimum.x = std::min(minimum.x, position.x);
            minimum.y = std::min(minimum.y, position.y);
            minimum.z = std::min(minimum.z, position.z);
            maximum.x = std::max(maximum.x, position.x);
            maximum.y = std::max(maximum.y, position.y);
            maximum.z = std::max(maximum.z, position.z);
          }
          const Vec3 pivot{(minimum.x + maximum.x) * 0.5F,
                           (minimum.y + maximum.y) * 0.5F,
                           (minimum.z + maximum.z) * 0.5F};
          const Vec3 normal =
              orientation == ModelingFlipOrientation::Horizontal
                  ? Vec3{1.0F, 0.0F, 0.0F}
                  : Vec3{0.0F, 1.0F, 0.0F};
          return document.reflect_across_plane(pivot, normal);
        },
        "Flip selected meshes failed.");
  }

  [[nodiscard]] std::vector<ObjectId> selected_component_objects() const {
    std::vector<ObjectId> selected;
    for (ObjectId object : selected_sdk_objects()) {
      Result<PolygonDocument> document = session_.document_copy(object);
      if (!document.ok()) {
        continue;
      }
      const PolygonSelectionSnapshot selection = document.value().selection();
      const bool has_components =
          selection_mode_ == ModelingSelectionMode::Edge
              ? !selection.edges.empty()
              : selection_mode_ == ModelingSelectionMode::Face
                    ? !selection.faces.empty()
                    : !selection.vertices.empty();
      if (has_components) {
        selected.push_back(object);
      }
    }
    return selected;
  }

  [[nodiscard]] ModelingSelectedVerticesSnapshot
  selected_vertices_snapshot() const {
    if (selection_mode_ != ModelingSelectionMode::Vertex) {
      return {.message =
                  "Snap Vertices to Floor only works in vertex mode."};
    }
    if (preview_active()) {
      return {.message = "Commit or cancel the active preview first."};
    }

    const std::vector<ObjectId> selected = selected_sdk_objects();
    if (selected.empty()) {
      return {.message = "Select one or more vertices first."};
    }
    Result<std::vector<PolygonDocument>> documents =
        session_.document_copies(selected);
    if (!documents.ok() || documents.value().size() != selected.size()) {
      return {.message = documents.error_message().empty()
                             ? "Selected vertex documents are unavailable."
                             : documents.error_message()};
    }

    ModelingSelectedVerticesSnapshot result;
    result.objects.reserve(selected.size());
    for (std::size_t index = 0; index < selected.size(); ++index) {
      const ObjectId object = selected[index];
      const std::optional<int> native = native_object(object);
      const std::optional<EntityId> entity =
          native.has_value()
              ? object_map_.entity_for_modeling_object(*native)
              : std::nullopt;
      if (!native.has_value() || !entity.has_value() ||
          !scene_.exists(*entity)) {
        return {.message = "A selected modeling object is no longer live."};
      }

      PolygonDocument &document = documents.value()[index];
      const PolygonSelectionSnapshot selection = document.selection();
      if (selection.vertices.empty()) {
        continue;
      }
      Result<AuthoredTopology> authored = document.authored_topology();
      if (!authored.ok()) {
        return {.message = authored.error_message().empty()
                               ? "Selected vertex geometry is unavailable."
                               : authored.error_message()};
      }
      const std::optional<QMat4> world =
          modeling_object_world_transform_resolver_
              ? modeling_object_world_transform_resolver_(*native)
              : std::optional<QMat4>{scene_.world_transform_matrix(*entity)};
      const std::optional<QMat4> world_inverse =
          world.has_value() ? try_scene_matrix_inverse(*world) : std::nullopt;
      if (!world.has_value() || !world_inverse.has_value()) {
        return {.message =
                    "A selected mesh has a non-invertible world transform."};
      }

      ModelingSelectedVertexObjectSnapshot object_snapshot{
          .modeling_object_id = *native,
          .entity = *entity,
          .world_from_object = *world,
          .object_from_world = *world_inverse,
      };
      object_snapshot.vertices.reserve(selection.vertices.size());
      for (const VertexId selected_vertex : selection.vertices) {
        const auto authored_vertex = std::ranges::find(
            authored.value().vertices, selected_vertex,
            &quader::modeling::AuthoredTopologyVertex::id);
        if (authored_vertex == authored.value().vertices.end()) {
          return {.message = "A selected vertex is no longer live."};
        }
        object_snapshot.vertices.push_back({
            .vertex = selected_vertex,
            .local_position = {authored_vertex->position.x,
                               authored_vertex->position.y,
                               authored_vertex->position.z},
        });
      }
      result.objects.push_back(std::move(object_snapshot));
    }
    if (result.objects.empty()) {
      return {.message = "Select one or more vertices first."};
    }
    result.ok = true;
    return result;
  }

  [[nodiscard]] ModelingCommandResult apply_resolved_vertex_positions(
      std::span<const ModelingResolvedVertexPositionBatch> batches) {
    if (selection_mode_ != ModelingSelectionMode::Vertex) {
      return failure("Snap Vertices to Floor only works in vertex mode.");
    }
    if (batches.empty()) {
      return {.ok = true,
              .changed = false,
              .message = "Selected vertices are already on the floor."};
    }

    auto retained =
        std::make_shared<std::vector<ModelingResolvedVertexPositionBatch>>(
            batches.begin(), batches.end());
    auto batch_by_object =
        std::make_shared<std::unordered_map<std::uint64_t, std::size_t>>();
    std::vector<ObjectId> objects;
    objects.reserve(retained->size());
    batch_by_object->reserve(retained->size());
    for (std::size_t index = 0; index < retained->size(); ++index) {
      const ModelingResolvedVertexPositionBatch &batch = (*retained)[index];
      const std::optional<ObjectId> object =
          sdk_object(batch.modeling_object_id);
      if (!object.has_value() || batch.positions.empty() ||
          !batch_by_object->emplace(sdk_object_key(*object), index).second) {
        return failure(
            "Resolved vertex positions contain an invalid object batch.");
      }
      objects.push_back(*object);
    }

    return mutate_documents_by_object(
        objects,
        [retained, batch_by_object](ObjectId object,
                                    PolygonDocument &document) {
          const auto found = batch_by_object->find(sdk_object_key(object));
          if (found == batch_by_object->end()) {
            return Result<OperationResult>::failure(
                quader::modeling::make_error(
                    quader::modeling::ErrorCode::InvalidId,
                    "Resolved vertex position object is not live."));
          }
          const ModelingResolvedVertexPositionBatch &batch =
              (*retained)[found->second];
          const PolygonSelectionSnapshot selection = document.selection();
          for (const VertexPositionUpdate &update : batch.positions) {
            if (std::ranges::find(selection.vertices, update.vertex) ==
                selection.vertices.end()) {
              return Result<OperationResult>::failure(
                  quader::modeling::make_error(
                      quader::modeling::ErrorCode::ValidationFailed,
                      "Resolved vertex position is not currently selected."));
            }
          }
          return document.set_vertex_positions(batch.positions);
        },
        "Snap vertices to floor failed.", false, true, true, true);
  }

  [[nodiscard]] ModelingCommandResult bridge_selected_components(
      int steps, bool preview_publication = false) {
    if (selection_mode_ != ModelingSelectionMode::Edge &&
        selection_mode_ != ModelingSelectionMode::Face) {
      return failure("Bridge works only in edge or face mode.");
    }
    std::vector<ObjectId> sources = selected_component_objects();
    if (sources.size() != 2U) {
      return failure(selection_mode_ == ModelingSelectionMode::Edge
                         ? "Bridge needs equal selected open-edge counts on "
                           "exactly two meshes."
                         : "Bridge Faces needs selected faces from one mesh "
                           "or two meshes.");
    }
    const std::optional<ObjectId> active = active_sdk_object();
    if (active.has_value()) {
      const auto found = std::ranges::find(sources, *active);
      if (found != sources.end()) {
        std::rotate(sources.begin(), found, std::next(found));
      }
    }
    const ObjectId survivor = sources[0];
    const ObjectId merged = sources[1];
    const std::optional<int> survivor_native = native_object(survivor);
    const std::optional<int> merged_native = native_object(merged);
    const std::optional<EntityId> survivor_entity =
        survivor_native.has_value()
            ? object_map_.entity_for_modeling_object(*survivor_native)
            : std::nullopt;
    const std::optional<EntityId> merged_entity =
        merged_native.has_value()
            ? object_map_.entity_for_modeling_object(*merged_native)
            : std::nullopt;
    if (!survivor_native.has_value() || !merged_native.has_value() ||
        !survivor_entity.has_value() || !merged_entity.has_value() ||
        !scene_.exists(*survivor_entity) || !scene_.exists(*merged_entity)) {
      return failure("Bridge source scene identity is not live.");
    }
    if (preview_publication && workspace_preview_world_from_object_.empty()) {
      workspace_preview_world_from_object_.emplace(
          *survivor_native,
          scene_.world_transform_matrix(*survivor_entity));
      workspace_preview_world_from_object_.emplace(
          *merged_native, scene_.world_transform_matrix(*merged_entity));
    }
    const auto world_from_object = [&](int native_object,
                                       EntityId entity) -> QMat4 {
      if (preview_publication) {
        const auto found =
            workspace_preview_world_from_object_.find(native_object);
        if (found != workspace_preview_world_from_object_.end()) {
          return found->second;
        }
      }
      return scene_.world_transform_matrix(entity);
    };
    const QMat4 survivor_world =
        world_from_object(*survivor_native, *survivor_entity);
    const QMat4 merged_world =
        world_from_object(*merged_native, *merged_entity);
    const std::optional<QMat4> survivor_from_world =
        try_scene_matrix_inverse(survivor_world);
    if (!survivor_from_world.has_value()) {
      return failure("Bridge survivor transform is not invertible.");
    }
    const QMat4 survivor_from_merged = scene_transform_multiply(
        *survivor_from_world, merged_world);
    const std::size_t undo_mark = session_.undo_depth();
    const quader::modeling::ModelingSession session_before = session_;
    const auto object_ids_before = object_ids_;
    const ModelingObjectRuntimeMap object_map_before = object_map_;
    const int active_before = active_native_object_id_;
    const std::uint64_t revision_before = persistent_content_revision_;
    const ModelingSdkProjectionStats stats_before = stats_;
    const AssetRegistryProjectionCheckpoint assets_before =
        assets_.capture_projection_checkpoint();
    const SceneProjectionCheckpoint scene_before =
        scene_.capture_projection_checkpoint();
    const auto rollback = [&](std::string message) {
      session_.restore_snapshot_preserving_identity_high_water(session_before);
      object_ids_ = object_ids_before;
      object_map_ = object_map_before;
      active_native_object_id_ = active_before;
      persistent_content_revision_ = revision_before;
      stats_ = stats_before;
      (void)assets_.restore_projection_checkpoint(assets_before);
      (void)scene_.restore_projection_checkpoint(scene_before);
      return failure(std::move(message));
    };

    Result<OperationResult> bridged = session_.bridge_objects(
        survivor, merged, to_sdk(survivor_from_merged),
        selection_mode_ == ModelingSelectionMode::Face ? SelectionKind::Face
                                                        : SelectionKind::Edge,
        steps);
    if (!bridged.ok() || !bridged.value().success ||
        !bridged.value().changed) {
      return rollback(bridged.ok() ? bridged.value().message
                                   : bridged.error_message());
    }
    const ObjectId survivor_selection_id = survivor;
    Result<OperationResult> selected_survivor = session_.apply_object_selection(
        std::span<const ObjectId>(&survivor_selection_id, 1U),
        SelectionEdit::Replace);
    if (!selected_survivor.ok() || !project_committed_object(survivor)) {
      return rollback(selected_survivor.ok()
                          ? "Bridge survivor projection failed."
                          : selected_survivor.error_message());
    }
    std::vector<AssetId> displaced_meshes;
    if (const MeshComponent *mesh = scene_.mesh(*merged_entity);
        mesh != nullptr && mesh->mesh != kInvalidAssetId) {
      displaced_meshes.push_back(mesh->mesh);
    }
    if (!mesh_sync_.destroy_projected_entity(scene_, *merged_entity)) {
      return rollback("Bridge source scene entity could not be removed.");
    }
    object_map_.unbind_object(*merged_native);
    object_ids_.erase(*merged_native);
    if (fails(ModelingSdkProjectionFailurePoint::AfterSdkMutation) ||
        fails(ModelingSdkProjectionFailurePoint::BeforeCommit)) {
      return rollback("Injected failure during SDK Bridge publication.");
    }
    mesh_sync_.remove_unreferenced_history_mesh_assets(
        scene_, assets_, displaced_meshes);
    active_native_object_id_ = *survivor_native;
    session_.squash_undo_since(undo_mark);
    if (!preview_publication) {
      ++persistent_content_revision_;
    }
    add_selection_to_scene();
    return {.ok = true, .changed = true, .message = "Bridged selection."};
  }

  [[nodiscard]] ModelingCommandResult combine_selected_objects() {
    if (selection_mode_ != ModelingSelectionMode::Object &&
        selection_mode_ != ModelingSelectionMode::Group) {
      return failure("Combine Meshes requires object selection mode.");
    }
    std::vector<ObjectId> sources = selected_sdk_objects();
    if (sources.size() < 2U) {
      return failure("Select at least two meshes to combine.");
    }
    if (preview_active()) {
      return failure("Commit or cancel the active preview first.");
    }
    const std::optional<ObjectId> active = active_sdk_object();
    if (active.has_value()) {
      const auto found = std::ranges::find(sources, *active);
      if (found != sources.end()) {
        std::rotate(sources.begin(), found, std::next(found));
      }
    }
    const ObjectId survivor = sources.front();
    const std::optional<int> survivor_native = native_object(survivor);
    const std::optional<EntityId> survivor_entity =
        survivor_native.has_value()
            ? object_map_.entity_for_modeling_object(*survivor_native)
            : std::nullopt;
    if (!survivor_native.has_value() || !survivor_entity.has_value() ||
        !scene_.exists(*survivor_entity)) {
      return failure("Combine Mesh survivor scene identity is not live.");
    }
    const std::optional<QMat4> survivor_from_world =
        try_scene_matrix_inverse(scene_.world_transform_matrix(
            *survivor_entity));
    if (!survivor_from_world.has_value()) {
      return failure("Combine Mesh survivor transform is not invertible.");
    }

    const std::size_t undo_mark = session_.undo_depth();
    const quader::modeling::ModelingSession session_before = session_;
    const auto object_ids_before = object_ids_;
    const ModelingObjectRuntimeMap object_map_before = object_map_;
    const int active_before = active_native_object_id_;
    const ModelingSelectionMode mode_before = selection_mode_;
    const std::uint64_t revision_before = persistent_content_revision_;
    const ModelingSdkProjectionStats stats_before = stats_;
    const AssetRegistryProjectionCheckpoint assets_before =
        assets_.capture_projection_checkpoint();
    const SceneProjectionCheckpoint scene_before =
        scene_.capture_projection_checkpoint();

    const auto rollback = [&](std::string message) {
      session_.restore_snapshot_preserving_identity_high_water(session_before);
      object_ids_ = object_ids_before;
      object_map_ = object_map_before;
      active_native_object_id_ = active_before;
      selection_mode_ = mode_before;
      persistent_content_revision_ = revision_before;
      stats_ = stats_before;
      (void)assets_.restore_projection_checkpoint(assets_before);
      (void)scene_.restore_projection_checkpoint(scene_before);
      return failure(std::move(message));
    };

    for (ObjectId source : sources) {
      if (source == survivor) {
        continue;
      }
      const std::optional<int> source_native = native_object(source);
      const std::optional<EntityId> source_entity =
          source_native.has_value()
              ? object_map_.entity_for_modeling_object(*source_native)
              : std::nullopt;
      if (!source_native.has_value() || !source_entity.has_value() ||
          !scene_.exists(*source_entity)) {
        return rollback("Combine Mesh source scene identity is not live.");
      }
      const QMat4 survivor_from_object = scene_transform_multiply(
          *survivor_from_world,
          scene_.world_transform_matrix(*source_entity));
      Result<PolygonDocument> copied = session_.document_copy(source);
      if (!copied.ok()) {
        return rollback(copied.error_message());
      }
      PolygonDocument working = std::move(copied).value();
      Result<OperationResult> selected = working.select_all_faces();
      if (!selected.ok()) {
        return rollback(selected.error_message());
      }
      Result<OperationResult> transformed =
          working.transform_selection(to_sdk(survivor_from_object));
      if (!transformed.ok()) {
        return rollback(transformed.error_message());
      }
      if (glm::determinant(glm::mat3{to_glm(survivor_from_object)}) < 0.0F) {
        Result<OperationResult> reversed =
            working.flip_selected_face_normals();
        if (!reversed.ok()) {
          return rollback(reversed.error_message());
        }
      }
      Result<OperationResult> cleared = working.clear_selection();
      if (!cleared.ok()) {
        return rollback(cleared.error_message());
      }
      OperationResult receipt = std::move(transformed).value();
      receipt.success = true;
      receipt.changed = true;
      receipt.dirty.topology = true;
      receipt.dirty.geometry = true;
      receipt.dirty.selection = false;
      Result<OperationResult> committed = session_.commit_document(
          source, std::move(working), std::move(receipt));
      if (!committed.ok()) {
        return rollback(committed.error_message());
      }
    }

    Result<OperationResult> combined = session_.combine_objects(sources);
    if (!combined.ok() || !combined.value().success ||
        !combined.value().changed) {
      return rollback(combined.ok() ? combined.value().message
                                    : combined.error_message());
    }
    if (fails(ModelingSdkProjectionFailurePoint::AfterSdkMutation) ||
        !project_committed_object(survivor)) {
      return rollback("SDK Combine Mesh projection failed.");
    }

    std::vector<AssetId> displaced_meshes;
    for (ObjectId source : sources) {
      if (source == survivor) {
        continue;
      }
      const std::optional<int> native = native_object(source);
      const std::optional<EntityId> entity =
          native.has_value()
              ? object_map_.entity_for_modeling_object(*native)
              : std::nullopt;
      if (!native.has_value() || !entity.has_value()) {
        return rollback("Combined source projection mapping was lost.");
      }
      if (const MeshComponent *mesh = scene_.mesh(*entity);
          mesh != nullptr && mesh->mesh != kInvalidAssetId) {
        displaced_meshes.push_back(mesh->mesh);
      }
      if (!mesh_sync_.destroy_projected_entity(scene_, *entity)) {
        return rollback("Combined source scene entity could not be removed.");
      }
      object_map_.unbind_object(*native);
      object_ids_.erase(*native);
    }
    if (fails(ModelingSdkProjectionFailurePoint::BeforeCommit)) {
      return rollback("Injected failure before SDK Combine Mesh commit.");
    }
    mesh_sync_.remove_unreferenced_history_mesh_assets(
        scene_, assets_, displaced_meshes);
    active_native_object_id_ = *survivor_native;
    session_.squash_undo_since(undo_mark);
    ++persistent_content_revision_;
    add_selection_to_scene();
    return {.ok = true,
            .changed = true,
            .message = combined.value().message};
  }

  [[nodiscard]] ModelingCommandResult extract_selected_faces() {
    if (selection_mode_ != ModelingSelectionMode::Face) {
      return failure("Extract Faces only works in face mode.");
    }
    const std::optional<ObjectId> source = active_sdk_object();
    const std::optional<int> source_native =
        source.has_value() ? native_object(*source) : std::nullopt;
    const std::optional<EntityId> source_entity =
        source_native.has_value()
            ? object_map_.entity_for_modeling_object(*source_native)
            : std::nullopt;
    if (!source.has_value() || !source_native.has_value() ||
        !source_entity.has_value() || !scene_.exists(*source_entity)) {
      return failure("Select faces before extracting.");
    }
    if (preview_active()) {
      return failure("Commit or cancel the active preview first.");
    }
    object_map_.remember_scene_entity(*source_native, scene_, *source_entity);
    const std::optional<ModelingObjectSceneIdentity> source_identity =
        object_map_.scene_identity_for_modeling_object(*source_native);
    const ModelingObjectComponent *source_component =
        scene_.modeling_object(*source_entity);
    if (!source_identity.has_value() || source_component == nullptr) {
      return failure("Extract Faces source scene identity is incomplete.");
    }

    const std::size_t undo_mark = session_.undo_depth();
    const quader::modeling::ModelingSession session_before = session_;
    const auto object_ids_before = object_ids_;
    const ModelingObjectRuntimeMap object_map_before = object_map_;
    const int next_native_before = next_native_object_id_;
    const int active_before = active_native_object_id_;
    const std::uint64_t revision_before = persistent_content_revision_;
    const ModelingSdkProjectionStats stats_before = stats_;
    const AssetRegistryProjectionCheckpoint assets_before =
        assets_.capture_projection_checkpoint();
    const SceneProjectionCheckpoint scene_before =
        scene_.capture_projection_checkpoint();
    std::vector<EntityId> created_entities;

    const auto rollback = [&](std::string message) {
      session_.restore_snapshot_preserving_identity_high_water(session_before);
      object_ids_ = object_ids_before;
      object_map_ = object_map_before;
      next_native_object_id_ = next_native_before;
      active_native_object_id_ = active_before;
      persistent_content_revision_ = revision_before;
      stats_ = stats_before;
      (void)assets_.restore_projection_checkpoint(assets_before);
      (void)scene_.restore_projection_checkpoint(scene_before,
                                                 created_entities);
      return failure(std::move(message));
    };

    Result<PolygonDocument> copied = session_.document_copy(*source);
    if (!copied.ok()) {
      return rollback(copied.error_message());
    }
    PolygonDocument working = std::move(copied).value();
    Result<quader::modeling::ExtractedPolygonDocument> extracted =
        working.extract_selected_faces();
    if (!extracted.ok() || !extracted.value().receipt.success ||
        !extracted.value().receipt.changed) {
      return rollback(extracted.ok() ? extracted.value().receipt.message
                                     : extracted.error_message());
    }
    const bool remove_empty_source = working.face_count() == 0U;
    OperationResult source_receipt = extracted.value().receipt;
    Result<OperationResult> committed = session_.commit_document(
        *source, std::move(working), std::move(source_receipt));
    if (!committed.ok()) {
      return rollback(committed.error_message());
    }
    Result<ObjectId> added = session_.add_document(
        std::move(extracted).value().document, "Extracted Faces");
    if (!added.ok()) {
      return rollback(added.error_message());
    }
    if (remove_empty_source) {
      Result<OperationResult> removed = session_.remove_object(*source);
      if (!removed.ok()) {
        return rollback(removed.error_message());
      }
    }
    const ObjectId extracted_id = added.value();
    Result<OperationResult> selected =
        session_.select_object(extracted_id);
    if (!selected.ok()) {
      return rollback(selected.error_message());
    }

    Result<PolygonDocument> extracted_document =
        session_.document_copy(extracted_id);
    if (!extracted_document.ok()) {
      return rollback(extracted_document.error_message());
    }
    ++stats_.mesh_compile_count;
    std::optional<MeshAsset> projected =
        MeshProjectionService::compile_sdk_document_mesh(
            extracted_document.value());
    if (!projected.has_value()) {
      return rollback("SDK extracted mesh projection failed.");
    }
    projected->name = source_identity->name;
    const AssetId extracted_mesh =
        mesh_sync_.install_projected_mesh(assets_, std::move(*projected));
    if (extracted_mesh == kInvalidAssetId) {
      return rollback("SDK extracted mesh asset could not be installed.");
    }
    ++stats_.asset_publication_count;
    const EntityId extracted_entity = mesh_sync_.create_projected_mesh_entity(
        scene_, source_identity->name);
    if (extracted_entity == kInvalidEntityId) {
      return rollback("SDK extracted scene entity could not be created.");
    }
    created_entities.push_back(extracted_entity);
    const int extracted_native = next_available_native_object_id();
    if (extracted_native == kInvalidModelingObjectId ||
        extracted_native == std::numeric_limits<int>::max() ||
        !mesh_sync_.bind_projected_object(
            scene_, extracted_entity, extracted_native,
            source_component->material_slot, true, extracted_mesh,
            source_identity->local_frame_version)) {
      return rollback("SDK extracted scene binding could not be installed.");
    }
    mesh_sync_.apply_scene_identity_to_duplicate_entity(
        scene_, extracted_entity, *source_identity);
    object_ids_.emplace(extracted_native, extracted_id);
    object_map_.bind(extracted_native, extracted_entity);
    object_map_.remember_scene_entity(extracted_native, scene_,
                                      extracted_entity);
    next_native_object_id_ = extracted_native + 1;
    active_native_object_id_ = extracted_native;

    std::vector<AssetId> displaced_meshes;
    if (remove_empty_source) {
      if (const MeshComponent *mesh = scene_.mesh(*source_entity);
          mesh != nullptr && mesh->mesh != kInvalidAssetId) {
        displaced_meshes.push_back(mesh->mesh);
      }
      if (!mesh_sync_.destroy_projected_entity(scene_, *source_entity)) {
        return rollback("Empty extraction source entity could not be removed.");
      }
      object_map_.unbind_object(*source_native);
      object_ids_.erase(*source_native);
    } else if (!project_committed_object(*source)) {
      return rollback("SDK extraction source projection failed.");
    }
    if (fails(ModelingSdkProjectionFailurePoint::AfterSdkMutation) ||
        fails(ModelingSdkProjectionFailurePoint::BeforeCommit)) {
      return rollback("Injected failure during SDK Extract Faces commit.");
    }
    mesh_sync_.remove_unreferenced_history_mesh_assets(
        scene_, assets_, displaced_meshes);
    session_.squash_undo_since(undo_mark);
    ++persistent_content_revision_;
    ++stats_.scene_publication_count;
    add_selection_to_scene();
    return {.ok = true,
            .changed = true,
            .message = "Extracted selected faces."};
  }

  [[nodiscard]] ModelingCommandResult plane_cut(
      std::span<const ModelingSdkPlaneCutRequest> requests,
      ModelingCutMode mode, bool select_results) {
    if (requests.empty()) {
      return failure("Cut needs a selected mesh.");
    }
    if (preview_active()) {
      return failure("Commit or cancel the active preview first.");
    }
    const std::size_t undo_mark = session_.undo_depth();
    const quader::modeling::ModelingSession session_before = session_;
    const auto object_ids_before = object_ids_;
    const ModelingObjectRuntimeMap object_map_before = object_map_;
    const int next_native_before = next_native_object_id_;
    const int active_before = active_native_object_id_;
    const ModelingSelectionMode mode_before = selection_mode_;
    const std::uint64_t revision_before = persistent_content_revision_;
    const ModelingSdkProjectionStats stats_before = stats_;
    const AssetRegistryProjectionCheckpoint assets_before =
        assets_.capture_projection_checkpoint();
    const SceneProjectionCheckpoint scene_before =
        scene_.capture_projection_checkpoint();
    std::vector<EntityId> created_entities;
    const auto rollback = [&](std::string message) {
      session_.restore_snapshot_preserving_identity_high_water(session_before);
      object_ids_ = object_ids_before;
      object_map_ = object_map_before;
      next_native_object_id_ = next_native_before;
      active_native_object_id_ = active_before;
      selection_mode_ = mode_before;
      persistent_content_revision_ = revision_before;
      stats_ = stats_before;
      (void)assets_.restore_projection_checkpoint(assets_before);
      (void)scene_.restore_projection_checkpoint(scene_before,
                                                 created_entities);
      return failure(std::move(message));
    };

    std::vector<ObjectId> result_objects;
    result_objects.reserve(requests.size() *
                           (mode == ModelingCutMode::KeepBoth ? 2U : 1U));
    std::unordered_set<std::uint64_t> seen;
    seen.reserve(requests.size());
    std::optional<int> first_back_native_id;
    if (mode == ModelingCutMode::KeepBoth) {
      first_back_native_id = reserve_native_object_ids(requests.size());
      if (!first_back_native_id.has_value()) {
        return rollback("Native modeling object IDs are exhausted.");
      }
    }
    std::size_t published_back_count = 0U;
    for (const ModelingSdkPlaneCutRequest &request : requests) {
      const std::optional<ObjectId> source =
          sdk_object(request.modeling_object_id);
      const std::optional<EntityId> source_entity =
          object_map_.entity_for_modeling_object(request.modeling_object_id);
      if (!source.has_value() || !source_entity.has_value() ||
          !scene_.exists(*source_entity) ||
          !seen.emplace(sdk_object_key(*source)).second) {
        return rollback("Cut target mesh was not found or was duplicated.");
      }
      object_map_.remember_scene_entity(request.modeling_object_id, scene_,
                                        *source_entity);
      const std::optional<ModelingObjectSceneIdentity> identity =
          object_map_.scene_identity_for_modeling_object(
              request.modeling_object_id);
      const ModelingObjectComponent *component =
          scene_.modeling_object(*source_entity);
      if (!identity.has_value() || component == nullptr) {
        return rollback("Cut target scene identity is incomplete.");
      }
      const ModelingObjectSceneIdentity identity_copy = *identity;
      const std::uint32_t material_slot = component->material_slot;

      Result<PolygonDocument> copied = session_.document_copy(*source);
      if (!copied.ok()) {
        return rollback(copied.error_message());
      }
      quader::modeling::PlaneSplitSpec split{
          .a = to_sdk(request.local_plane_points[0]),
          .b = to_sdk(request.local_plane_points[1]),
          .c = to_sdk(request.local_plane_points[2]),
          .cap = true,
      };
      Result<quader::modeling::SplitPolygonDocuments> cut =
          copied.value().split_by_plane(split);
      if (!cut.ok()) {
        return rollback(cut.error_message());
      }
      if (!select_results) {
        Result<OperationResult> front_clear = cut.value().front.clear_selection();
        Result<OperationResult> back_clear = cut.value().back.clear_selection();
        if (!front_clear.ok() || !back_clear.ok()) {
          return rollback(!front_clear.ok() ? front_clear.error_message()
                                            : back_clear.error_message());
        }
      }

      PolygonDocument source_result =
          mode == ModelingCutMode::DiscardLeft
              ? std::move(cut.value().back)
              : std::move(cut.value().front);
      OperationResult source_receipt = cut.value().receipt;
      Result<OperationResult> committed = session_.commit_document(
          *source, std::move(source_result), std::move(source_receipt));
      if (!committed.ok() || !project_committed_object(*source)) {
        return rollback(committed.ok() ? "Cut source projection failed."
                                       : committed.error_message());
      }
      result_objects.push_back(*source);

      if (mode == ModelingCutMode::KeepBoth) {
        Result<ObjectId> back = session_.add_document(
            std::move(cut).value().back, identity_copy.name);
        if (!back.ok()) {
          return rollback(back.error_message());
        }
        int back_native = kInvalidModelingObjectId;
        EntityId back_entity = kInvalidEntityId;
        const bool published = publish_new_object(
            back.value(), identity_copy, material_slot, back_native,
            back_entity, false,
            *first_back_native_id +
                static_cast<int>(published_back_count));
        if (back_entity != kInvalidEntityId) {
          created_entities.push_back(back_entity);
        }
        if (!published) {
          return rollback("Cut back projection failed.");
        }
        ++published_back_count;
        result_objects.push_back(back.value());
      }
    }

    const std::span<const ObjectId> selection =
        select_results ? std::span<const ObjectId>(result_objects)
                       : std::span<const ObjectId>{};
    Result<OperationResult> selected =
        session_.apply_object_selection(selection, SelectionEdit::Replace);
    if (!selected.ok()) {
      return rollback(selected.error_message());
    }
    if (select_results) {
      selection_mode_ = ModelingSelectionMode::Face;
      const std::optional<int> active = native_object(result_objects.back());
      active_native_object_id_ =
          active.value_or(kInvalidModelingObjectId);
    } else {
      active_native_object_id_ = kInvalidModelingObjectId;
    }
    if (fails(ModelingSdkProjectionFailurePoint::AfterSdkMutation) ||
        fails(ModelingSdkProjectionFailurePoint::BeforeCommit)) {
      return rollback("Injected failure during SDK plane cut publication.");
    }
    session_.squash_undo_since(undo_mark);
    ++persistent_content_revision_;
    ++stats_.scene_publication_count;
    add_selection_to_scene();
    return {.ok = true, .changed = true, .message = "Cut selected meshes."};
  }

  [[nodiscard]] ModelingCommandResult half_cut_selected_objects(
      quader_editor_core::HalfCutOperationSettings settings,
      bool preview_publication = false) {
    if (selection_mode_ != ModelingSelectionMode::Object &&
        selection_mode_ != ModelingSelectionMode::Group) {
      return failure("Half-Cut requires object selection mode.");
    }
    const std::vector<ObjectId> sources = selected_sdk_objects();
    if (sources.empty()) {
      return failure("Half-Cut needs one or more selected meshes.");
    }
    if (preview_active() && !preview_publication) {
      return failure("Commit or cancel the active preview first.");
    }

    const std::size_t undo_mark = session_.undo_depth();
    const quader::modeling::ModelingSession session_before = session_;
    const auto object_ids_before = object_ids_;
    const ModelingObjectRuntimeMap object_map_before = object_map_;
    const int next_native_before = next_native_object_id_;
    const int active_before = active_native_object_id_;
    const ModelingSelectionMode mode_before = selection_mode_;
    const std::uint64_t revision_before = persistent_content_revision_;
    const ModelingSdkProjectionStats stats_before = stats_;
    const AssetRegistryProjectionCheckpoint assets_before =
        assets_.capture_projection_checkpoint();
    const SceneProjectionCheckpoint scene_before =
        scene_.capture_projection_checkpoint();
    std::vector<EntityId> created_entities;

    const auto rollback = [&](std::string message) {
      session_.restore_snapshot_preserving_identity_high_water(session_before);
      object_ids_ = object_ids_before;
      object_map_ = object_map_before;
      next_native_object_id_ = next_native_before;
      active_native_object_id_ = active_before;
      selection_mode_ = mode_before;
      persistent_content_revision_ = revision_before;
      stats_ = stats_before;
      (void)assets_.restore_projection_checkpoint(assets_before);
      (void)scene_.restore_projection_checkpoint(scene_before,
                                                 created_entities);
      return failure(std::move(message));
    };

    std::vector<ObjectId> results;
    results.reserve(sources.size() * (settings.separate_parts ? 2U : 1U));
    std::optional<int> first_back_native_id;
    if (settings.separate_parts) {
      first_back_native_id = reserve_native_object_ids(sources.size());
      if (!first_back_native_id.has_value()) {
        return rollback("Native modeling object IDs are exhausted.");
      }
    }
    std::size_t published_back_count = 0U;
    for (ObjectId source : sources) {
      const std::optional<int> native = native_object(source);
      const std::optional<EntityId> entity =
          native.has_value()
              ? object_map_.entity_for_modeling_object(*native)
              : std::nullopt;
      if (!native.has_value() || !entity.has_value() ||
          !scene_.exists(*entity)) {
        return rollback("Half-Cut source scene identity is not live.");
      }
      object_map_.remember_scene_entity(*native, scene_, *entity);
      const std::optional<ModelingObjectSceneIdentity> identity =
          object_map_.scene_identity_for_modeling_object(*native);
      const ModelingObjectComponent *component =
          scene_.modeling_object(*entity);
      if (!identity.has_value() || component == nullptr) {
        return rollback("Half-Cut source scene identity is incomplete.");
      }
      const ModelingObjectSceneIdentity identity_copy = *identity;
      const std::uint32_t material_slot = component->material_slot;

      Result<PolygonDocument> copied = session_.document_copy(source);
      if (!copied.ok()) {
        return rollback(copied.error_message());
      }
      PolygonDocument working = std::move(copied).value();
      Result<AuthoredTopology> topology = working.authored_topology();
      if (!topology.ok() || topology.value().vertices.empty()) {
        return rollback("Half-Cut target mesh has no vertices.");
      }
      quader::modeling::Vec3 minimum =
          topology.value().vertices.front().position;
      quader::modeling::Vec3 maximum = minimum;
      for (const quader::modeling::AuthoredTopologyVertex &vertex :
           topology.value().vertices) {
        minimum.x = std::min(minimum.x, vertex.position.x);
        minimum.y = std::min(minimum.y, vertex.position.y);
        minimum.z = std::min(minimum.z, vertex.position.z);
        maximum.x = std::max(maximum.x, vertex.position.x);
        maximum.y = std::max(maximum.y, vertex.position.y);
        maximum.z = std::max(maximum.z, vertex.position.z);
      }
      const quader::modeling::Vec3 center{
          (minimum.x + maximum.x) * 0.5F,
          (minimum.y + maximum.y) * 0.5F,
          (minimum.z + maximum.z) * 0.5F,
      };
      quader::modeling::PlaneSplitSpec split{
          .a = center,
          .cap = settings.fill_parts,
      };
      switch (settings.axis) {
      case quader_editor_core::HalfCutAxis::X:
        split.b = {center.x, center.y + 1.0F, center.z};
        split.c = {center.x, center.y, center.z + 1.0F};
        break;
      case quader_editor_core::HalfCutAxis::Y:
        split.b = {center.x, center.y, center.z + 1.0F};
        split.c = {center.x + 1.0F, center.y, center.z};
        break;
      case quader_editor_core::HalfCutAxis::Z:
        split.b = {center.x + 1.0F, center.y, center.z};
        split.c = {center.x, center.y + 1.0F, center.z};
        break;
      }

      if (!settings.separate_parts) {
        Result<OperationResult> cut = working.cut_surface_by_plane(split);
        if (!cut.ok() || !cut.value().success || !cut.value().changed) {
          return rollback(cut.ok() ? cut.value().message
                                   : cut.error_message());
        }
        Result<OperationResult> cleared = working.clear_selection();
        if (!cleared.ok()) {
          return rollback(cleared.error_message());
        }
        Result<OperationResult> committed = session_.commit_document(
            source, std::move(working), std::move(cut).value());
        if (!committed.ok()) {
          return rollback(committed.error_message());
        }
        if (!project_committed_object(source)) {
          return rollback("Half-Cut source projection failed.");
        }
        results.push_back(source);
        continue;
      }

      Result<quader::modeling::SplitPolygonDocuments> cut =
          working.split_by_plane(split);
      if (!cut.ok()) {
        return rollback(cut.error_message());
      }
      Result<OperationResult> cleared_front =
          cut.value().front.clear_selection();
      Result<OperationResult> cleared_back = cut.value().back.clear_selection();
      if (!cleared_front.ok() || !cleared_back.ok()) {
        return rollback(!cleared_front.ok() ? cleared_front.error_message()
                                            : cleared_back.error_message());
      }
      OperationResult front_receipt = cut.value().receipt;
      Result<OperationResult> front = session_.commit_document(
          source, std::move(cut.value().front), std::move(front_receipt));
      if (!front.ok()) {
        return rollback(front.error_message());
      }
      Result<ObjectId> back = session_.add_document(
          std::move(cut).value().back, identity_copy.name);
      if (!back.ok()) {
        return rollback(back.error_message());
      }
      if (!project_committed_object(source)) {
        return rollback("Half-Cut front projection failed.");
      }
      int back_native = kInvalidModelingObjectId;
      EntityId back_entity = kInvalidEntityId;
      const bool published = publish_new_object(
          back.value(), identity_copy, material_slot, back_native,
          back_entity, false,
          *first_back_native_id + static_cast<int>(published_back_count));
      if (back_entity != kInvalidEntityId) {
        created_entities.push_back(back_entity);
      }
      if (!published) {
        return rollback("Half-Cut back projection failed.");
      }
      ++published_back_count;
      results.push_back(source);
      results.push_back(back.value());
    }

    Result<OperationResult> selected =
        session_.apply_object_selection(results, SelectionEdit::Replace);
    if (!selected.ok()) {
      return rollback(selected.error_message());
    }
    if (const std::optional<ObjectId> active = active_sdk_object();
        !active.has_value() ||
        std::ranges::find(results, *active) == results.end()) {
      const std::optional<int> native = native_object(results.back());
      active_native_object_id_ =
          native.value_or(kInvalidModelingObjectId);
    }
    if (fails(ModelingSdkProjectionFailurePoint::AfterSdkMutation) ||
        fails(ModelingSdkProjectionFailurePoint::BeforeCommit)) {
      return rollback("Injected failure during SDK Half-Cut commit.");
    }
    session_.squash_undo_since(undo_mark);
    if (!preview_publication) {
      ++persistent_content_revision_;
    }
    add_selection_to_scene();
    return {.ok = true, .changed = true, .message = "Half-Cut"};
  }

  [[nodiscard]] ModelingCommandResult mutate_documents_by_object(
      std::span<const ObjectId> object_ids,
      const DocumentMutationByObject &mutation,
      std::string_view fallback_message,
      bool preview_publication = false,
      bool reproject_all_objects = false,
      bool position_only_projection = false,
      bool preserve_object_selection = false,
      bool preserve_scene_selection = false) {
    if (object_ids.empty()) {
      return failure("Select a modeling mesh or component first.");
    }
    if (preview_active() && !preview_publication) {
      return failure("Commit or cancel the active preview first.");
    }

    const std::size_t undo_mark = session_.undo_depth();
    const quader::modeling::ModelingSession session_before = session_;
    const int active_before = active_native_object_id_;
    const ModelingSelectionMode mode_before = selection_mode_;
    const std::uint64_t mode_revision_before = mode_revision_;
    const std::uint64_t persistent_revision_before =
        persistent_content_revision_;
    const ModelingSdkProjectionStats stats_before = stats_;
    const AssetRegistryProjectionCheckpoint assets_before =
        assets_.capture_projection_checkpoint();
    const SceneProjectionCheckpoint scene_before =
        scene_.capture_projection_checkpoint();

    const auto rollback = [&](std::string message) {
      session_.restore_snapshot_preserving_identity_high_water(session_before);
      active_native_object_id_ = active_before;
      selection_mode_ = mode_before;
      mode_revision_ = mode_revision_before;
      persistent_content_revision_ = persistent_revision_before;
      stats_ = stats_before;
      (void)assets_.restore_projection_checkpoint(assets_before);
      (void)scene_.restore_projection_checkpoint(scene_before);
      return failure(std::move(message));
    };

    bool changed = false;
    std::size_t deleted_vertex_count = 0U;
    std::string message;
    std::vector<SessionDocumentUpdate> updates;
    updates.reserve(object_ids.size());
    std::vector<std::pair<ObjectId, PolygonDocument>> projection_documents;
    projection_documents.reserve(reproject_all_objects ? object_ids.size()
                                                        : 0U);
    Result<std::vector<PolygonDocument>> copied_documents =
        session_.document_copies(object_ids);
    if (!copied_documents.ok()) {
      return rollback(copied_documents.error_message());
    }
    for (std::size_t object_index = 0; object_index < object_ids.size();
         ++object_index) {
      const ObjectId object_id = object_ids[object_index];
      PolygonDocument working =
          std::move(copied_documents.value()[object_index]);
      Result<OperationResult> operated = mutation(object_id, working);
      if (!operated.ok()) {
        return rollback(operated.error_message().empty()
                            ? std::string(fallback_message)
                            : operated.error_message());
      }
      if (!operated.value().success) {
        return rollback(operated.value().message.empty()
                            ? std::string(fallback_message)
                            : operated.value().message);
      }
      message = operated.value().message;
      deleted_vertex_count += operated.value().deleted.vertices.size();
      if (reproject_all_objects) {
        projection_documents.emplace_back(object_id, working);
      }
      if (!operated.value().changed) {
        continue;
      }
      if (!reproject_all_objects) {
        projection_documents.emplace_back(object_id, working);
      }
      updates.push_back({.object = object_id,
                         .document = std::move(working),
                         .receipt = std::move(operated).value()});
    }

    if (!updates.empty()) {
      QDR_PROFILE_SCOPE("Modeling SDK commit documents atomic batch");
      Result<OperationResult> committed =
          session_.commit_documents_batch(std::move(updates),
                                          preserve_object_selection);
      if (!committed.ok()) {
        return rollback(committed.error_message());
      }
      changed = committed.value().changed;
    }

    if (!changed) {
      (void)session_.rollback_undo_since(undo_mark);
      if (preview_publication && !reproject_all_objects) {
        active_preview_deleted_vertex_count_ = deleted_vertex_count;
      }
      if (!reproject_all_objects) {
        return {.ok = true,
                .changed = false,
                .message = message.empty() ? std::string(fallback_message)
                                           : std::move(message)};
      }
    }
    if (fails(ModelingSdkProjectionFailurePoint::AfterSdkMutation)) {
      return rollback("Injected failure after SDK operation mutation.");
    }
    {
      QDR_PROFILE_SCOPE("Modeling SDK project document batch");
      std::vector<std::optional<MeshAsset>> compiled_documents;
      if (reproject_all_objects &&
          (position_only_projection || projection_documents.size() >= 8u)) {
        QDR_PROFILE_SCOPE("Modeling SDK parallel compile document batch");
        compiled_documents.resize(projection_documents.size());
        std::vector<std::optional<MeshAsset>> retained_meshes;
        if (position_only_projection) {
          QDR_PROFILE_SCOPE(
              "Transform commit collect retained position-only meshes");
          retained_meshes.resize(projection_documents.size());
          for (std::size_t index = 0; index < projection_documents.size();
               ++index) {
            const std::optional<int> native =
                native_object(projection_documents[index].first);
            const std::optional<EntityId> entity =
                native.has_value()
                    ? object_map_.entity_for_modeling_object(*native)
                    : std::nullopt;
            const MeshComponent *binding =
                entity.has_value() ? scene_.mesh(*entity) : nullptr;
            const MeshAsset *current =
                binding != nullptr ? assets_.mesh(binding->mesh) : nullptr;
            if (current != nullptr) {
              retained_meshes[index] = *current;
            }
          }
        }
        std::atomic_size_t next_document{0u};
        std::atomic_size_t full_compile_fallbacks{0u};
        const std::size_t worker_count = std::min<std::size_t>(
            projection_documents.size(),
            std::clamp<std::size_t>(std::thread::hardware_concurrency(), 1u,
                                    8u));
        const auto compile_next = [&]() {
          for (;;) {
            const std::size_t index =
                next_document.fetch_add(1u, std::memory_order_relaxed);
            if (index >= projection_documents.size()) {
              return;
            }
            if (!retained_meshes.empty() &&
                retained_meshes[index].has_value()) {
              QDR_PROFILE_SCOPE(
                  "Transform commit update retained mesh positions");
              compiled_documents[index] =
                  MeshProjectionService::update_sdk_document_mesh_positions(
                      projection_documents[index].second,
                      *retained_meshes[index]);
            }
            if (!compiled_documents[index].has_value()) {
              QDR_PROFILE_SCOPE("Transform commit full mesh compile fallback");
              full_compile_fallbacks.fetch_add(1u,
                                               std::memory_order_relaxed);
              compiled_documents[index] =
                  MeshProjectionService::compile_sdk_document_mesh(
                      projection_documents[index].second);
            }
          }
        };
        std::vector<std::jthread> workers;
        workers.reserve(worker_count - 1u);
        for (std::size_t worker = 1u; worker < worker_count; ++worker) {
          workers.emplace_back(compile_next);
        }
        compile_next();
        for (std::jthread &worker : workers) {
          worker.join();
        }
        stats_.mesh_compile_count +=
            full_compile_fallbacks.load(std::memory_order_relaxed);
        if (std::ranges::any_of(compiled_documents,
                                [](const auto &compiled) {
                                  return !compiled.has_value();
                                })) {
          return rollback("SDK operation mesh compilation failed.");
        }
      }
      for (std::size_t index = 0; index < projection_documents.size();
           ++index) {
        const auto &[object_id, document] = projection_documents[index];
        std::optional<MeshAsset> compiled =
            compiled_documents.empty()
                ? std::optional<MeshAsset>{}
                : std::move(compiled_documents[index]);
        if (!project_document(object_id, document, preview_publication,
                              reproject_all_objects, std::move(compiled))) {
          return rollback("SDK operation host projection failed.");
        }
      }
    }
    if (projection_documents.empty() && changed) {
      for (ObjectId object_id : object_ids) {
        Result<PolygonDocument> projected = session_.document_copy(object_id);
        if (!projected.ok() ||
            !project_document(object_id, projected.value(),
                              preview_publication)) {
          return rollback("SDK operation host projection failed.");
        }
      }
    }
    if (fails(ModelingSdkProjectionFailurePoint::BeforeCommit)) {
      return rollback("Injected failure before SDK operation commit.");
    }
    session_.squash_undo_since(undo_mark);
    if (!preview_publication) {
      ++persistent_content_revision_;
    } else {
      active_preview_deleted_vertex_count_ = deleted_vertex_count;
    }
    if (!preserve_scene_selection) {
      add_selection_to_scene();
    }
    return {.ok = true,
            .changed = changed,
            .message = message.empty() ? std::string(fallback_message)
                                       : std::move(message)};
  }

  /**
   * Commits a topology-stable transform without cloning the complete SDK
   * session, asset registry, and scene for rollback. All document mutations,
   * canonical mesh compilation, host-target lookup, and injected failure
   * checks finish before the first durable publication. The fast path is used
   * only when every target owns its mesh binding, making publication two fixed
   * owner batches with no allocation of new scene/asset identities.
   */
  [[nodiscard]] ModelingCommandResult commit_sparse_transform_positions(
      const std::shared_ptr<const ModelingSdkTransformHistoryDelta::Impl>
          &exact_positions,
      bool use_after = true) {
    QDR_PROFILE_FUNCTION();
    if (!exact_positions || exact_positions->objects.empty()) {
      return failure("Transform commit has no authored position changes.");
    }

    std::vector<ObjectId> objects;
    objects.reserve(exact_positions->objects.size());
    auto exact_index =
        std::make_shared<std::unordered_map<std::uint64_t, std::size_t>>();
    exact_index->reserve(exact_positions->objects.size());
    for (std::size_t index = 0; index < exact_positions->objects.size();
         ++index) {
      const auto &entry = exact_positions->objects[index];
      objects.push_back(entry.object);
      exact_index->emplace(sdk_object_key(entry.object), index);
    }

    const auto exact_mutation =
        [exact_positions, exact_index,
         use_after](ObjectId object, PolygonDocument &document) {
          const auto found = exact_index->find(sdk_object_key(object));
          if (found == exact_index->end()) {
            return Result<OperationResult>::failure(
                quader::modeling::make_error(
                    quader::modeling::ErrorCode::InvalidId,
                    "Transform commit object is not live."));
          }
          const auto &entry = exact_positions->objects[found->second];
          return document.set_vertex_positions(use_after ? entry.after
                                                         : entry.before);
        };

    // Shared mesh bindings need the normal detach/rollback machinery. Keep
    // that uncommon path correct while avoiding whole-project checkpoints for
    // the common independently-authored modeling mesh case.
    if (!preview_targets_have_unique_mesh_bindings(objects)) {
      QDR_PROFILE_SCOPE("Transform commit shared-mesh safe fallback");
      return mutate_documents_by_object(
          objects, exact_mutation, "Transform commit failed.", false, false,
          true, true);
    }

    if (fails(ModelingSdkProjectionFailurePoint::AfterSdkMutation)) {
      return failure("Injected failure after SDK operation mutation.");
    }
    if (fails(ModelingSdkProjectionFailurePoint::BeforeCommit)) {
      return failure("Injected failure before SDK operation commit.");
    }
    if (fails(ModelingSdkProjectionFailurePoint::CommittedAssetReplace) ||
        fails(ModelingSdkProjectionFailurePoint::CommittedSceneNotify)) {
      return failure("SDK operation host projection failed.");
    }

    Result<std::vector<PolygonDocument>> copied_documents =
        session_.document_copies(objects);
    if (!copied_documents.ok() ||
        copied_documents.value().size() != objects.size()) {
      return failure(copied_documents.ok()
                         ? "Transform commit document batch is incomplete."
                         : copied_documents.error_message());
    }

    std::vector<SessionDocumentUpdate> updates;
    std::vector<std::pair<ObjectId, PolygonDocument>> projection_documents;
    updates.reserve(objects.size());
    projection_documents.reserve(objects.size());
    for (std::size_t index = 0; index < objects.size(); ++index) {
      PolygonDocument working =
          std::move(copied_documents.value()[index]);
      Result<OperationResult> operated =
          exact_mutation(objects[index], working);
      if (!operated.ok() || !operated.value().success) {
        return failure(operated.error_message().empty()
                           ? "Transform commit failed."
                           : operated.error_message());
      }
      if (!operated.value().changed) {
        continue;
      }
      projection_documents.emplace_back(objects[index], working);
      updates.push_back({.object = objects[index],
                         .document = std::move(working),
                         .receipt = std::move(operated).value()});
    }
    if (updates.empty()) {
      return {.ok = true,
              .changed = false,
              .message = "Transform commit unchanged."};
    }

    // Pin only affected immutable mesh payloads. This is O(changed objects)
    // in metadata and O(1) in geometry, unlike the former complete registry
    // checkpoint.
    std::vector<const MeshAsset *> retained_meshes;
    std::vector<EntityId> target_entities;
    std::vector<AssetId> target_mesh_ids;
    retained_meshes.reserve(projection_documents.size());
    target_entities.reserve(projection_documents.size());
    target_mesh_ids.reserve(projection_documents.size());
    for (const auto &[object, document] : projection_documents) {
      (void)document;
      const std::optional<int> native = native_object(object);
      const std::optional<EntityId> entity =
          native.has_value()
              ? object_map_.entity_for_modeling_object(*native)
              : std::nullopt;
      const MeshComponent *binding =
          entity.has_value() ? scene_.mesh(*entity) : nullptr;
      const MeshAsset *current =
          binding != nullptr ? assets_.mesh(binding->mesh) : nullptr;
      if (!entity.has_value() || !scene_.exists(*entity) ||
          binding == nullptr || current == nullptr) {
        return failure("Transform commit host target is not live.");
      }
      retained_meshes.push_back(current);
      target_entities.push_back(*entity);
      target_mesh_ids.push_back(binding->mesh);
    }

    std::vector<std::optional<MeshAsset>> compiled_documents(
        projection_documents.size());
    std::vector<std::size_t> projection_representative(
        projection_documents.size());
    {
      QDR_PROFILE_SCOPE("Transform commit group identical projection inputs");
      std::unordered_multimap<std::uint64_t, std::size_t>
          representative_by_signature;
      representative_by_signature.reserve(projection_documents.size());
      const auto positions_for = [&](std::size_t projection_index)
          -> const std::vector<VertexPositionUpdate> & {
        const ObjectId object = projection_documents[projection_index].first;
        const auto exact = exact_index->find(sdk_object_key(object));
        const auto &delta = exact_positions->objects[exact->second];
        return use_after ? delta.after : delta.before;
      };
      const auto same_updates = [](const auto &left, const auto &right) {
        if (left.size() != right.size()) {
          return false;
        }
        for (std::size_t index = 0U; index < left.size(); ++index) {
          if (left[index].vertex != right[index].vertex ||
              left[index].position.x != right[index].position.x ||
              left[index].position.y != right[index].position.y ||
              left[index].position.z != right[index].position.z) {
            return false;
          }
        }
        return true;
      };
      const auto signature_for = [&](std::size_t projection_index) {
        constexpr std::uint64_t kOffsetBasis = 14695981039346656037ULL;
        constexpr std::uint64_t kPrime = 1099511628211ULL;
        std::uint64_t signature = kOffsetBasis;
        const auto mix = [&signature](std::uint64_t value) {
          signature ^= value;
          signature *= kPrime;
        };
        const MeshAsset &mesh = *retained_meshes[projection_index];
        mix(static_cast<std::uint64_t>(mesh.geometry_token()));
        mix(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(
            mesh.authored_polygon_document.get())));
        const auto &positions = positions_for(projection_index);
        mix(positions.size());
        for (const VertexPositionUpdate &position : positions) {
          mix(static_cast<std::uint64_t>(position.vertex.index) |
              (static_cast<std::uint64_t>(position.vertex.generation) <<
               32U));
          mix(std::hash<float>{}(position.position.x));
          mix(std::hash<float>{}(position.position.y));
          mix(std::hash<float>{}(position.position.z));
        }
        return signature;
      };

      for (std::size_t index = 0U; index < projection_documents.size();
           ++index) {
        projection_representative[index] = index;
        const std::uint64_t signature = signature_for(index);
        const auto [first, last] =
            representative_by_signature.equal_range(signature);
        for (auto candidate = first; candidate != last; ++candidate) {
          const std::size_t representative = candidate->second;
          const MeshAsset &mesh = *retained_meshes[index];
          const MeshAsset &representative_mesh =
              *retained_meshes[representative];
          if (mesh.geometry_token() ==
                  representative_mesh.geometry_token() &&
              mesh.authored_polygon_document.get() ==
                  representative_mesh.authored_polygon_document.get() &&
              same_updates(positions_for(index),
                           positions_for(representative))) {
            projection_representative[index] = representative;
            break;
          }
        }
        if (projection_representative[index] == index) {
          representative_by_signature.emplace(signature, index);
        }
      }
    }
    std::atomic_size_t next_document{0u};
    std::atomic_size_t full_compile_fallbacks{0u};
    const std::size_t worker_count = std::min<std::size_t>(
        projection_documents.size(),
        std::clamp<std::size_t>(std::thread::hardware_concurrency(), 1u, 8u));
    const auto compile_next = [&]() {
      for (;;) {
        const std::size_t index =
            next_document.fetch_add(1u, std::memory_order_relaxed);
        if (index >= projection_documents.size()) {
          return;
        }
        if (projection_representative[index] != index) {
          continue;
        }
        {
          QDR_PROFILE_SCOPE("Transform commit canonical retained compile");
          compiled_documents[index] =
              MeshProjectionService::update_sdk_document_mesh_positions(
                  projection_documents[index].second,
                  *retained_meshes[index]);
        }
        if (!compiled_documents[index].has_value()) {
          QDR_PROFILE_SCOPE("Transform commit canonical full fallback");
          full_compile_fallbacks.fetch_add(1u, std::memory_order_relaxed);
          compiled_documents[index] =
              MeshProjectionService::compile_sdk_document_mesh(
                  projection_documents[index].second);
        }
      }
    };
    {
      QDR_PROFILE_SCOPE("Transform commit precompile changed documents");
      std::vector<std::jthread> workers;
      workers.reserve(worker_count - 1u);
      for (std::size_t worker = 1u; worker < worker_count; ++worker) {
        workers.emplace_back(compile_next);
      }
      compile_next();
      for (std::jthread &worker : workers) {
        worker.join();
      }
    }
    {
      QDR_PROFILE_SCOPE("Transform commit share identical projection outputs");
      for (std::size_t index = 0U; index < compiled_documents.size(); ++index) {
        const std::size_t representative = projection_representative[index];
        if (representative != index) {
          compiled_documents[index] = compiled_documents[representative];
        }
      }
    }
    for (std::size_t index = 0; index < compiled_documents.size(); ++index) {
      if (!compiled_documents[index].has_value()) {
        return failure("SDK transform mesh compilation failed.");
      }
      compiled_documents[index]->name = retained_meshes[index]->name;
      if (!assets_.validate_mesh(*compiled_documents[index]).ok) {
        return failure("SDK transform mesh validation failed.");
      }
    }

    if (!scene_.can_notify_mesh_payloads_changed(target_entities)) {
      return failure("Transform commit scene publication is not live.");
    }

    // Stage every replacement and owner precondition before the SDK document
    // commit. Host publication below is then two fixed owner-batch calls.
    std::vector<MeshAsset> original_meshes;
    std::vector<MeshAssetReplacement> mesh_replacements;
    original_meshes.reserve(retained_meshes.size());
    mesh_replacements.reserve(compiled_documents.size());
    for (std::size_t index = 0; index < compiled_documents.size(); ++index) {
      original_meshes.push_back(*retained_meshes[index]);
      mesh_replacements.push_back(
          {.id = target_mesh_ids[index],
           .mesh = std::move(*compiled_documents[index])});
    }

    const std::size_t undo_mark = session_.undo_depth();
    Result<OperationResult> committed =
        session_.commit_documents_batch(std::move(updates), true);
    if (!committed.ok() || !committed.value().success) {
      return failure(committed.error_message().empty()
                         ? "Transform document commit failed."
                          : committed.error_message());
    }

    const ModelingSdkProjectionStats stats_before_publication = stats_;
    if (!assets_.replace_meshes(mesh_replacements)) {
      (void)session_.rollback_undo_since(undo_mark);
      stats_ = stats_before_publication;
      return failure("SDK transform asset batch publication failed.");
    }
    if (!scene_.notify_mesh_payloads_changed(target_entities)) {
      QDR_PROFILE_SCOPE("Transform commit emergency sparse batch rollback");
      std::vector<MeshAssetReplacement> restore_replacements;
      restore_replacements.reserve(original_meshes.size());
      for (std::size_t index = 0; index < original_meshes.size(); ++index) {
        restore_replacements.push_back(
            {.id = target_mesh_ids[index],
             .mesh = std::move(original_meshes[index])});
      }
      if (assets_.replace_meshes(restore_replacements)) {
        (void)scene_.notify_mesh_payloads_changed(target_entities);
      }
      (void)session_.rollback_undo_since(undo_mark);
      stats_ = stats_before_publication;
      return failure("SDK transform scene batch publication failed.");
    }

    stats_.asset_publication_count += mesh_replacements.size();
    stats_.scene_publication_count += target_entities.size();
    stats_.mesh_compile_count +=
        full_compile_fallbacks.load(std::memory_order_relaxed);
    (void)session_.squash_undo_since(undo_mark);
    ++persistent_content_revision_;
    return {.ok = true,
            .changed = committed.value().changed,
            .message = "Transform committed."};
  }

  [[nodiscard]] bool preview_targets_have_unique_mesh_bindings(
      std::span<const ObjectId> object_ids) const {
    std::unordered_map<AssetId, std::size_t> reference_count;
    for (const EntityId entity : scene_.entities()) {
      if (const MeshComponent *mesh = scene_.mesh(entity);
          mesh != nullptr && mesh->mesh != kInvalidAssetId) {
        ++reference_count[mesh->mesh];
      }
    }
    for (const ObjectId object_id : object_ids) {
      const std::optional<int> native = native_object(object_id);
      const std::optional<EntityId> entity =
          native.has_value()
              ? object_map_.entity_for_modeling_object(*native)
              : std::nullopt;
      const MeshComponent *mesh =
          entity.has_value() && scene_.exists(*entity) ? scene_.mesh(*entity)
                                                       : nullptr;
      if (mesh == nullptr || reference_count[mesh->mesh] != 1U) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] ModelingCommandResult mutate_documents(
      std::span<const ObjectId> object_ids, const DocumentMutation &mutation,
      std::string_view fallback_message, bool preview_publication = false) {
    return mutate_documents_by_object(
        object_ids,
        [&mutation](ObjectId, PolygonDocument &document) {
          return mutation(document);
        },
        fallback_message, preview_publication);
  }

  [[nodiscard]] ModelingCommandResult mutate_selected_documents(
      const DocumentMutation &mutation, std::string_view fallback_message,
      bool preview_publication = false) {
    const std::vector<ObjectId> selected = selected_sdk_objects();
    return mutate_documents(selected, mutation, fallback_message,
                            preview_publication);
  }

  [[nodiscard]] DocumentMutation preview_mutation_for(
      quader_editor_core::EditorOperationId operation,
      const quader_editor_core::EditorOperationSettings &settings) const {
    using Id = quader_editor_core::EditorOperationId;
    switch (operation) {
    case Id::Bridge: {
      const auto *value =
          std::get_if<quader_editor_core::BridgeOperationSettings>(&settings);
      const auto normalized =
          quader_editor_core::normalize_bridge_operation_settings(
              value != nullptr ? *value
                               : quader_editor_core::BridgeOperationSettings{});
      const ModelingSelectionMode mode = selection_mode_;
      return [normalized, mode](PolygonDocument &document) {
        return mode == ModelingSelectionMode::Face
                   ? document.bridge_selected_faces(normalized.steps)
                   : document.bridge_selected_edges(normalized.steps);
      };
    }
    case Id::EdgeBevel: {
      const auto *value =
          std::get_if<quader_editor_core::EdgeBevelOperationSettings>(
              &settings);
      const auto normalized =
          quader_editor_core::normalize_edge_bevel_operation_settings(
              value != nullptr
                  ? *value
                  : quader_editor_core::EdgeBevelOperationSettings{});
      return [normalized](PolygonDocument &document) {
        return document.bevel_selected_edges({
            .width = static_cast<float>(normalized.width),
            .profile = static_cast<float>(normalized.profile),
            .segments = normalized.segments,
        });
      };
    }
    case Id::Thicken: {
      const auto *value =
          std::get_if<quader_editor_core::ThickenOperationSettings>(&settings);
      const auto normalized =
          quader_editor_core::normalize_thicken_operation_settings(
              value != nullptr ? *value
                               : quader_editor_core::ThickenOperationSettings{});
      const ModelingSelectionMode mode = selection_mode_;
      return [normalized, mode](PolygonDocument &document) {
        return apply_with_whole_object_face_selection(
            mode, document,
            [normalized](PolygonDocument &selected_document) {
              return selected_document.thicken_selected_faces(
                  static_cast<float>(normalized.thickness),
                  normalized.from_center);
            });
      };
    }
    case Id::SliceQuad: {
      const auto *value =
          std::get_if<quader_editor_core::SliceQuadOperationSettings>(&settings);
      const auto normalized =
          quader_editor_core::normalize_slice_quad_operation_settings(
              value != nullptr ? *value
                               : quader_editor_core::SliceQuadOperationSettings{});
      return [normalized](PolygonDocument &document) {
        return document.slice_selected_quads(normalized.x_slices,
                                             normalized.y_slices);
      };
    }
    case Id::MergeByDistance: {
      const auto *value =
          std::get_if<quader_editor_core::MergeByDistanceOperationSettings>(
              &settings);
      const auto normalized =
          quader_editor_core::normalize_merge_by_distance_operation_settings(
              value != nullptr
                  ? *value
                  : quader_editor_core::MergeByDistanceOperationSettings{});
      return [normalized](PolygonDocument &document) {
        return document.merge_selected_vertices_by_distance(
            static_cast<float>(normalized.tolerance));
      };
    }
    default:
      return {};
    }
  }

  [[nodiscard]] ModelingCommandResult update_preview_from_base(
      const DocumentMutation &mutation, std::string_view fallback_message) {
    if (!preview_active() || !preview_base_snapshot_.has_value() ||
        !preview_base_snapshot_->valid()) {
      return failure("No active SDK operation preview.");
    }
    const quader::modeling::PreviewTransactionSummary preview =
        session_.preview_summary();
    Result<PolygonDocument> base =
        preview_base_snapshot_->impl_->session.document_copy(preview.object_id);
    if (!base.ok()) {
      return failure(base.error_message());
    }
    PolygonDocument working = std::move(base).value();
    Result<OperationResult> operated = mutation(working);
    if (!operated.ok() || !operated.value().success) {
      return failure(operated.ok() && !operated.value().message.empty()
                         ? operated.value().message
                         : (operated.error_message().empty()
                                ? std::string(fallback_message)
                                : operated.error_message()));
    }
    active_preview_deleted_vertex_count_ =
        operated.value().deleted.vertices.size();

    const quader::modeling::ModelingSession session_before = session_;
    const AssetRegistryProjectionCheckpoint assets_before =
        assets_.capture_projection_checkpoint();
    const SceneProjectionCheckpoint scene_before =
        scene_.capture_projection_checkpoint();
    Result<OperationResult> updated = session_.update_document_preview(
        preview.id, std::move(working), std::move(operated).value());
    if (!updated.ok()) {
      return failure(updated.error_message());
    }
    if (!project_preview_object()) {
      session_.restore_snapshot_preserving_identity_high_water(session_before);
      (void)assets_.restore_projection_checkpoint(assets_before);
      (void)scene_.restore_projection_checkpoint(scene_before);
      return failure("SDK operation preview asset publication failed.");
    }
    preview_changed_ = preview_changed_ || updated.value().changed;
    ++stats_.preview_publication_count;
    return from_sdk(updated, std::string(fallback_message));
  }

  [[nodiscard]] bool fails(ModelingSdkProjectionFailurePoint point) const {
    return failure_point_ == point;
  }

  [[nodiscard]] std::optional<ObjectId> sdk_object(int native_id) const {
    const auto found = object_ids_.find(native_id);
    return found == object_ids_.end() ? std::nullopt
                                      : std::optional<ObjectId>{found->second};
  }

  [[nodiscard]] std::optional<int> native_object(ObjectId object_id) const {
    const std::uint64_t key = sdk_object_key(object_id);
    if (const auto cached = native_object_ids_.find(key);
        cached != native_object_ids_.end()) {
      const auto forward = object_ids_.find(cached->second);
      if (forward != object_ids_.end() && forward->second == object_id) {
        return cached->second;
      }
    }

    native_object_ids_.clear();
    native_object_ids_.reserve(object_ids_.size());
    for (const auto &[native_id, sdk_id] : object_ids_) {
      native_object_ids_.emplace(sdk_object_key(sdk_id), native_id);
    }
    const auto rebuilt = native_object_ids_.find(key);
    return rebuilt == native_object_ids_.end()
               ? std::nullopt
               : std::optional<int>{rebuilt->second};
  }

  [[nodiscard]] std::optional<ObjectId> active_sdk_object() const {
    return sdk_object(active_native_object_id_);
  }

  [[nodiscard]] int next_available_native_object_id() const {
    int candidate = next_native_object_id_;
    const std::vector<EntityId> entities = scene_.entities();
    ++stats_.native_object_id_scene_scan_count;
    stats_.native_object_id_scene_entity_visit_count += entities.size();
    for (const EntityId entity : entities) {
      const ModelingObjectComponent *component = scene_.modeling_object(entity);
      if (component != nullptr && component->object_id >= candidate &&
          component->object_id < std::numeric_limits<int>::max()) {
        candidate = component->object_id + 1;
      }
    }
    return candidate;
  }

  [[nodiscard]] std::optional<int>
  reserve_native_object_ids(std::size_t count) {
    if (count == 0U) {
      return std::nullopt;
    }
    const int first = next_available_native_object_id();
    if (first == kInvalidModelingObjectId ||
        first >= std::numeric_limits<int>::max()) {
      return std::nullopt;
    }
    const std::size_t available = static_cast<std::size_t>(
        std::numeric_limits<int>::max() - first);
    if (count > available) {
      return std::nullopt;
    }
    next_native_object_id_ = first + static_cast<int>(count);
    return first;
  }

  [[nodiscard]] bool project_document(ObjectId object_id,
                                      const PolygonDocument &document,
                                      bool preview,
                                      bool mesh_binding_known_unique = false,
                                      std::optional<MeshAsset> projected =
                                          std::nullopt) {
    const std::optional<int> native = native_object(object_id);
    const std::optional<EntityId> entity =
        native.has_value()
            ? object_map_.entity_for_modeling_object(*native)
            : std::nullopt;
    const MeshComponent *binding =
        entity.has_value() ? scene_.mesh(*entity) : nullptr;
    const MeshAsset *current = binding != nullptr
                                   ? assets_.mesh(binding->mesh)
                                   : nullptr;
    if (!entity.has_value() || !scene_.exists(*entity) || binding == nullptr ||
        current == nullptr) {
      return false;
    }
    const AssetId original_mesh = binding->mesh;
    bool changed_mesh_binding = false;
    const auto fail = [&]() {
      if (changed_mesh_binding && scene_.exists(*entity)) {
        (void)mesh_sync_.set_projected_mesh_binding(
            scene_, *entity, original_mesh);
      }
      return false;
    };
    if (!projected.has_value()) {
      ++stats_.mesh_compile_count;
      projected = MeshProjectionService::compile_sdk_document_mesh(document);
    }
    if (!projected.has_value()) {
      return false;
    }
    projected->name = current->name;
    const ModelingSdkProjectionFailurePoint asset_failure =
        preview ? ModelingSdkProjectionFailurePoint::PreviewAssetReplace
                : ModelingSdkProjectionFailurePoint::CommittedAssetReplace;
    if (fails(asset_failure)) {
      return false;
    }
    // Transform previews only use the reproject-all path after proving every
    // target owns a unique mesh binding. Preserve that proof here instead of
    // rescanning the full scene once for every projected object (O(N^2)).
    if (!mesh_binding_known_unique &&
        scene_mesh_is_shared(scene_, *entity, binding->mesh)) {
      const AssetId private_mesh =
          mesh_sync_.install_projected_mesh(assets_, std::move(*projected));
      if (private_mesh == kInvalidAssetId ||
          !mesh_sync_.set_projected_mesh_binding(scene_, *entity,
                                                 private_mesh)) {
        return fail();
      }
      changed_mesh_binding = true;
    } else if (!mesh_sync_.replace_projected_mesh(
                   assets_, binding->mesh, std::move(*projected))) {
      return false;
    }
    ++stats_.asset_publication_count;
    const ModelingSdkProjectionFailurePoint scene_failure =
        preview ? ModelingSdkProjectionFailurePoint::PreviewSceneNotify
                : ModelingSdkProjectionFailurePoint::CommittedSceneNotify;
    if (fails(scene_failure)) {
      return fail();
    }
    if (!mesh_sync_.notify_projected_mesh_changed(scene_, *entity)) {
      return fail();
    }
    ++stats_.scene_publication_count;
    return true;
  }

  [[nodiscard]] bool project_committed_object(ObjectId object_id) {
    Result<PolygonDocument> document = session_.document_copy(object_id);
    return document.ok() &&
           project_document(object_id, document.value(), false);
  }

  [[nodiscard]] bool project_preview_object() {
    const quader::modeling::PreviewTransactionSummary summary =
        session_.preview_summary();
    if (!summary.active) {
      return false;
    }
    Result<PolygonDocument> document =
        session_.preview_document_copy(summary.id);
    return document.ok() &&
           project_document(summary.object_id, document.value(), true);
  }

  void clear_preview_checkpoints() {
    preview_base_snapshot_.reset();
    workspace_update_base_snapshot_.reset();
    preview_asset_checkpoint_.reset();
    preview_scene_checkpoint_.reset();
    preview_changed_ = false;
    workspace_preview_active_ = false;
    workspace_preview_undo_mark_ = 0U;
    workspace_preview_world_from_object_.clear();
    workspace_transform_participants_.clear();
    workspace_transform_commit_mutation_ = {};
    workspace_transform_transient_active_ = false;
    workspace_transform_snapshotless_ = false;
    workspace_transform_preexisting_changed_ = false;
    ++workspace_transform_preview_revision_;
    active_preview_operation_id_.clear();
    active_preview_operation_.reset();
    active_preview_settings_.reset();
    active_preview_deleted_vertex_count_ = 0U;
  }

  SceneDocument &scene_;
  AssetRegistry &assets_;
  ModelingObjectRuntimeMap &object_map_;
  ModelingMeshSyncService &mesh_sync_;
  std::function<std::optional<QMat4>(int)>
      modeling_object_world_transform_resolver_;
  quader::modeling::ModelingSession session_;
  std::map<int, ObjectId> object_ids_;
  mutable std::unordered_map<std::uint64_t, int> native_object_ids_;
  int next_native_object_id_ = 1;
  int active_native_object_id_ = kInvalidModelingObjectId;
  ModelingSelectionMode selection_mode_ = ModelingSelectionMode::Object;
  std::uint64_t persistent_content_revision_ = 0;
  std::uint64_t mode_revision_ = 0;
  std::optional<ModelingSdkProjectionSnapshot> preview_base_snapshot_;
  std::optional<ModelingSdkProjectionSnapshot>
      workspace_update_base_snapshot_;
  std::optional<AssetRegistryProjectionCheckpoint> preview_asset_checkpoint_;
  std::optional<SceneProjectionCheckpoint> preview_scene_checkpoint_;
  bool preview_changed_ = false;
  bool workspace_preview_active_ = false;
  std::size_t workspace_preview_undo_mark_ = 0U;
  std::map<int, QMat4> workspace_preview_world_from_object_;
  std::vector<WorkspaceTransformParticipant>
      workspace_transform_participants_;
  DocumentMutationByObject workspace_transform_commit_mutation_;
  bool workspace_transform_transient_active_ = false;
  bool workspace_transform_snapshotless_ = false;
  bool workspace_transform_preexisting_changed_ = false;
  ModelingSdkTransformHistoryDelta committed_transform_history_delta_;
  std::optional<std::size_t> committed_transform_undo_mark_;
  std::uint64_t workspace_transform_preview_revision_ = 0U;
  std::string active_preview_operation_id_;
  std::optional<quader_editor_core::EditorOperationId>
      active_preview_operation_;
  std::optional<quader_editor_core::EditorOperationSettings>
      active_preview_settings_;
  std::size_t active_preview_deleted_vertex_count_ = 0U;
  mutable std::optional<SelectionSummaryCacheKey>
      selection_summary_cache_key_;
  mutable ModelingSelectionSummary selection_summary_cache_;
  mutable std::vector<EntityId> selected_component_entities_cache_;
  mutable std::optional<NativeSelectionProjectionCacheKey>
      native_selection_projection_cache_key_;
  mutable std::optional<quader_editor_core::EditorSession>
      native_selection_projection_cache_;
  ModelingSdkProjectionFailurePoint failure_point_ =
      ModelingSdkProjectionFailurePoint::None;
  mutable ModelingSdkProjectionStats stats_;
};

ModelingSdkProjectionAdapter::ModelingSdkProjectionAdapter(
    SceneDocument &scene, AssetRegistry &assets,
    ModelingObjectRuntimeMap &object_map,
    ModelingMeshSyncService &mesh_sync)
    : impl_(std::make_unique<Impl>(scene, assets, object_map, mesh_sync)) {}

ModelingSdkProjectionAdapter::~ModelingSdkProjectionAdapter() = default;

void ModelingSdkProjectionAdapter::set_modeling_object_world_transform_resolver(
    std::function<std::optional<QMat4>(int)> resolver) {
  impl_->set_modeling_object_world_transform_resolver(std::move(resolver));
}

void ModelingSdkProjectionAdapter::reset_from_scene() {
  impl_->reset_from_scene();
}

ModelingCommandResult
ModelingSdkProjectionAdapter::restore_mesh_documents_from_scene(
    std::span<const EntityId> entities) {
  return impl_->restore_mesh_documents_from_scene(entities);
}

bool ModelingSdkProjectionAdapter::owns_object(int modeling_object_id) const {
  return impl_->owns_object(modeling_object_id);
}

bool ModelingSdkProjectionAdapter::owns_entity(EntityId entity) const {
  return impl_->owns_entity(entity);
}

std::size_t ModelingSdkProjectionAdapter::object_count() const noexcept {
  return impl_->object_count();
}

bool ModelingSdkProjectionAdapter::has_selection() const {
  return impl_->has_selection();
}

const void *ModelingSdkProjectionAdapter::session_identity() const noexcept {
  return impl_->session_identity();
}

std::optional<MeshAsset> ModelingSdkProjectionAdapter::compile_object_mesh(
    int modeling_object_id) const {
  return impl_->compile_object_mesh(modeling_object_id);
}

ModelingSdkPlaneCutPreviewResult
ModelingSdkProjectionAdapter::preview_plane_cut(
    std::span<const int> modeling_object_ids,
    const std::array<QVec3, 3> &world_plane_points,
    ModelingCutMode mode) const {
  return impl_->preview_plane_cut(modeling_object_ids, world_plane_points,
                                  mode);
}

ModelingSdkMirrorPreviewResult ModelingSdkProjectionAdapter::preview_mirror(
    std::span<const int> modeling_object_ids,
    const std::array<QVec3, 3> &world_plane_points) const {
  return impl_->preview_mirror(modeling_object_ids, world_plane_points);
}

quader_editor_core::EditorSession
ModelingSdkProjectionAdapter::native_selection_projection() const {
  return impl_->native_selection_projection();
}

const quader_editor_core::EditorSession &
ModelingSdkProjectionAdapter::native_selection_projection_view() const {
  return impl_->native_selection_projection_view();
}

std::optional<EntityId> ModelingSdkProjectionAdapter::pick_object_entity(
    const ModelingPickSelectRequest &request,
    std::optional<EntityId> competing_native) const {
  return impl_->pick_object_entity(request, competing_native);
}

std::vector<EntityId>
ModelingSdkProjectionAdapter::box_pick_object_entities(
    const ModelingBoxSelectRequest &request) const {
  QDR_PROFILE_SCOPE("Modeling SDK box pick entry");
  return impl_->box_pick_object_entities(request);
}

ModelingCreateBoxResult ModelingSdkProjectionAdapter::create_box(
    const ModelingCreateBoxRequest &request, const CreationFinalizer &finalize) {
  return impl_->create_box(request, finalize);
}

ModelingCreateBoxResult ModelingSdkProjectionAdapter::create_box_from_corners(
    std::span<const QVec3> corners, std::string_view name,
    MaterialReference material, const CreationFinalizer &finalize) {
  return impl_->create_box_from_corners(corners, name, material, finalize);
}

ModelingCreateBoxResult ModelingSdkProjectionAdapter::create_mesh(
    const ModelingCreateMeshRequest &request,
    const CreationFinalizer &finalize) {
  return impl_->create_mesh(request, finalize);
}

ModelingCreateBoxResult ModelingSdkProjectionAdapter::create_face(
    std::span<const QVec3> points, QVec3 requested_normal,
    std::string_view name, MaterialReference material,
    const CreationFinalizer &finalize) {
  return impl_->create_face(points, requested_normal, name, material, finalize);
}

ModelingSdkDuplicateBatchResult ModelingSdkProjectionAdapter::duplicate_objects(
    std::span<const int> source_modeling_object_ids,
    std::size_t copies_per_source, bool select_sources,
    bool share_source_mesh_assets) {
  return impl_->duplicate_objects(source_modeling_object_ids,
                                  copies_per_source, select_sources, false,
                                  share_source_mesh_assets);
}

ModelingSdkClipboardCopyResult
ModelingSdkProjectionAdapter::copy_selection_to_clipboard() {
  return impl_->copy_selection_to_clipboard();
}

ModelingSdkDuplicateBatchResult ModelingSdkProjectionAdapter::paste_clipboard(
    std::span<const ModelingObjectSceneIdentity> source_identities) {
  return impl_->paste_clipboard(source_identities);
}

bool ModelingSdkProjectionAdapter::clipboard_available() const {
  return impl_->clipboard_available();
}

std::size_t ModelingSdkProjectionAdapter::clipboard_object_count() const {
  return impl_->clipboard_object_count();
}

ModelingCommandResult ModelingSdkProjectionAdapter::replace_mesh_document(
    int modeling_object_id, const quader_poly::Document &document) {
  return impl_->replace_mesh_document(modeling_object_id, document);
}

ModelingCommandResult ModelingSdkProjectionAdapter::execute_operation(
    quader_editor_core::EditorOperationId operation,
    const quader_editor_core::EditorOperationSettings &settings) {
  return impl_->execute_operation(operation, settings);
}

ModelingCommandResult ModelingSdkProjectionAdapter::plane_cut(
    std::span<const ModelingSdkPlaneCutRequest> requests,
    ModelingCutMode mode, bool select_results) {
  return impl_->plane_cut(requests, mode, select_results);
}

ModelingCommandResult ModelingSdkProjectionAdapter::begin_operation_preview(
    quader_editor_core::EditorOperationId operation,
    const quader_editor_core::EditorOperationSettings &settings) {
  return impl_->begin_operation_preview(operation, settings);
}

ModelingCommandResult ModelingSdkProjectionAdapter::update_operation_preview(
    quader_editor_core::EditorOperationId operation,
    const quader_editor_core::EditorOperationSettings &settings) {
  return impl_->update_operation_preview(operation, settings);
}

ModelingCommandResult
ModelingSdkProjectionAdapter::commit_operation_preview() {
  return impl_->commit_operation_preview();
}

ModelingCommandResult
ModelingSdkProjectionAdapter::cancel_operation_preview() {
  return impl_->cancel_operation_preview();
}

std::string_view
ModelingSdkProjectionAdapter::active_preview_operation_id() const noexcept {
  return impl_->active_preview_operation_id();
}

std::optional<quader_editor_core::EditorOperationId>
ModelingSdkProjectionAdapter::active_preview_operation() const noexcept {
  return impl_->active_preview_operation();
}

std::size_t
ModelingSdkProjectionAdapter::active_preview_deleted_vertex_count() const
    noexcept {
  return impl_->active_preview_deleted_vertex_count();
}

ModelingCommandResult ModelingSdkProjectionAdapter::set_object_selection(
    std::span<const EntityId> entities, EntityId active) {
  return impl_->set_object_selection(entities, active);
}

ModelingCommandResult
ModelingSdkProjectionAdapter::project_object_selection_from_scene(
    std::span<const EntityId> entities, EntityId active) {
  return impl_->set_object_selection(entities, active, false);
}

ModelingCommandResult ModelingSdkProjectionAdapter::set_selection_mode(
    ModelingSelectionMode mode) {
  return impl_->set_selection_mode(mode);
}

ModelingCommandResult ModelingSdkProjectionAdapter::set_component_selection(
    const ModelingComponentSelectionRequest &request) {
  return impl_->set_component_selection(request);
}

ModelingCommandResult ModelingSdkProjectionAdapter::select_mesh_ngons(
    std::span<const int> modeling_object_ids) {
  return impl_->select_mesh_ngons(modeling_object_ids);
}

ModelingCommandResult ModelingSdkProjectionAdapter::select_all() {
  return impl_->select_all();
}

ModelingCommandResult ModelingSdkProjectionAdapter::invert_selection() {
  return impl_->invert_selection();
}

ModelingCommandResult ModelingSdkProjectionAdapter::clear_selection() {
  return impl_->clear_selection();
}

ModelingCommandResult ModelingSdkProjectionAdapter::remove_objects(
    std::span<const int> modeling_object_ids) {
  return impl_->remove_objects(modeling_object_ids);
}

ModelingSelectionSummary
ModelingSdkProjectionAdapter::apply_native_selection_projection(
    const quader_editor_core::EditorSession &projection) {
  return impl_->apply_native_selection_projection(projection);
}

ModelingSelectionSummary ModelingSdkProjectionAdapter::pick_select(
    const ModelingPickSelectRequest &request,
    ModelingSelectionService &selection) {
  return impl_->pick_select(request, selection);
}

ModelingSelectionSummary ModelingSdkProjectionAdapter::box_select(
    const ModelingBoxSelectRequest &request,
    ModelingSelectionService &selection) {
  QDR_PROFILE_SCOPE("Modeling SDK box select entry");
  return impl_->box_select(request, selection);
}

ModelingSelectionSummary ModelingSdkProjectionAdapter::box_select(
    const ModelingBoxSelectRequest &request,
    const quader_editor_core::RectangleSelectionState &baseline,
    ModelingSelectionService &selection) {
  QDR_PROFILE_SCOPE("Modeling SDK baseline box select entry");
  return impl_->box_select(request, baseline, selection);
}

ModelingCommandResult
ModelingSdkProjectionAdapter::move_selection(QVec3 delta) {
  return impl_->move_selection(delta);
}

ModelingCommandResult ModelingSdkProjectionAdapter::transform_selection(
    const ModelingTransform3 &transform, QVec3 pivot) {
  return impl_->transform_selection(transform, pivot);
}

ModelingCommandResult
ModelingSdkProjectionAdapter::mirror_selected_objects_across_plane(
    QVec3 origin, QVec3 normal) {
  return impl_->mirror_selected_objects_across_plane(origin, normal);
}

ModelingCommandResult ModelingSdkProjectionAdapter::mirror_objects_across_planes(
    std::span<const ModelingSdkPlaneCutRequest> requests) {
  return impl_->mirror_objects_across_planes(requests);
}

ModelingCommandResult ModelingSdkProjectionAdapter::flip_selected_objects(
    ModelingFlipOrientation orientation) {
  return impl_->flip_selected_objects(orientation);
}

ModelingCommandResult
ModelingSdkProjectionAdapter::assign_material_slot_to_selection(
    std::uint32_t material_slot) {
  return impl_->assign_material_slot_to_selection(material_slot);
}

ModelingSelectedFaceTextureSnapshot
ModelingSdkProjectionAdapter::selected_face_texture_settings() const {
  return impl_->selected_face_texture_settings();
}

ModelingCommandResult ModelingSdkProjectionAdapter::apply_face_texture_updates(
    std::span<const ModelingFaceTextureUpdateBatch> batches) {
  return impl_->apply_face_texture_updates(batches);
}

ModelingCommandResult ModelingSdkProjectionAdapter::apply_face_texture_action(
    std::span<const ModelingFaceTextureActionTarget> targets,
    const modeling::FaceTextureActionOptions &options) {
  return impl_->apply_face_texture_action(targets, options);
}

ModelingCommandResult ModelingSdkProjectionAdapter::transfer_face_texture(
    const ModelingFaceTextureTransferTarget &source,
    const ModelingFaceTextureTransferTarget &target,
    std::uint32_t target_material_slot) {
  return impl_->transfer_face_texture(source, target, target_material_slot);
}

ModelingSelectedUvPeelSnapshot
ModelingSdkProjectionAdapter::selected_uv_peel_edges() const {
  return impl_->selected_uv_peel_edges();
}

ModelingCommandResult ModelingSdkProjectionAdapter::apply_uv_peel(
    std::span<const ModelingUvPeelTarget> targets,
    const ModelingUvPeelOptions &options) {
  return impl_->apply_uv_peel(targets, options);
}

ModelingCommandResult
ModelingSdkProjectionAdapter::assign_material_slot_to_objects(
    std::span<const int> modeling_object_ids, std::uint32_t material_slot,
    bool preserve_scene_selection) {
  return impl_->assign_material_slot_to_objects(modeling_object_ids,
                                                material_slot,
                                                preserve_scene_selection);
}

ModelingCommandResult ModelingSdkProjectionAdapter::assign_material_slot_to_face(
    int modeling_object_id, int face_id, std::uint32_t material_slot) {
  return impl_->assign_material_slot_to_face(modeling_object_id, face_id,
                                              material_slot);
}

ModelingCommandResult
ModelingSdkProjectionAdapter::assign_material_slots_to_faces(
    std::span<const ModelingMaterialSlotAssignment> assignments) {
  return impl_->assign_material_slots_to_faces(assignments);
}

ModelingSelectedVerticesSnapshot
ModelingSdkProjectionAdapter::selected_vertices_snapshot() const {
  return impl_->selected_vertices_snapshot();
}

ModelingCommandResult
ModelingSdkProjectionAdapter::apply_resolved_vertex_positions(
    std::span<const ModelingResolvedVertexPositionBatch> batches) {
  return impl_->apply_resolved_vertex_positions(batches);
}

ModelingCommandResult ModelingSdkProjectionAdapter::insert_edge_loop(
    int modeling_object_id, int edge_a, int edge_b, float factor) {
  return impl_->insert_edge_loop(modeling_object_id, edge_a, edge_b, factor);
}

ModelingCommandResult ModelingSdkProjectionAdapter::knife_stroke(
    int modeling_object_id,
    std::span<const ModelingSdkKnifeTarget> points,
    std::span<const ModelingSdkKnifeStrokeSegment> segments) {
  return impl_->knife_stroke(modeling_object_id, points, segments);
}

ModelingCommandResult
ModelingSdkProjectionAdapter::begin_translate_preview(
    bool duplicate_selected, bool topology_stable_transform) {
  return impl_->begin_translate_preview(duplicate_selected,
                                        topology_stable_transform);
}

ModelingCommandResult
ModelingSdkProjectionAdapter::update_translate_preview(QVec3 delta) {
  return impl_->update_translate_preview(delta);
}

ModelingCommandResult ModelingSdkProjectionAdapter::update_transform_preview(
    const ModelingTransform3 &transform, QVec3 pivot) {
  return impl_->update_transform_preview(transform, pivot);
}

ModelingCommandResult
ModelingSdkProjectionAdapter::update_object_transform_preview(
    std::span<const ModelingSdkObjectTransformPreview> transforms,
    ModelingSdkTransformPreviewKind kind, float closed_edge_ledge_size) {
  return impl_->update_object_transform_preview(transforms, kind,
                                                 closed_edge_ledge_size);
}

std::vector<ModelingToolPreviewMesh>
ModelingSdkProjectionAdapter::workspace_transform_preview_meshes() const {
  return impl_->workspace_transform_preview_meshes();
}

std::uint64_t
ModelingSdkProjectionAdapter::workspace_transform_preview_revision() const {
  return impl_->workspace_transform_preview_revision();
}

bool ModelingSdkProjectionAdapter::workspace_transform_preview_active() const {
  return impl_->workspace_transform_preview_active();
}

void ModelingSdkProjectionAdapter::apply_workspace_transform_preview_to_overlay(
    ViewportOverlayFrame &frame) const {
  impl_->apply_workspace_transform_preview_to_overlay(frame);
}

ModelingCommandResult ModelingSdkProjectionAdapter::update_extrude_preview(
    QVec3 offset, float closed_edge_ledge_size) {
  return impl_->update_extrude_preview(offset, closed_edge_ledge_size);
}

ModelingCommandResult ModelingSdkProjectionAdapter::update_inset_preview(
    const ModelingTransform3 &transform, QVec3 pivot) {
  return impl_->update_inset_preview(transform, pivot);
}

ModelingCommandResult
ModelingSdkProjectionAdapter::commit_translate_preview() {
  return impl_->commit_translate_preview();
}

ModelingCommandResult
ModelingSdkProjectionAdapter::cancel_translate_preview() {
  return impl_->cancel_translate_preview();
}

ModelingSdkTransformHistoryDelta
ModelingSdkProjectionAdapter::take_committed_transform_history_delta() {
  return impl_->take_committed_transform_history_delta();
}

ModelingCommandResult ModelingSdkProjectionAdapter::apply_transform_history_delta(
    const ModelingSdkTransformHistoryDelta &delta, bool use_after) {
  return impl_->apply_transform_history_delta(delta, use_after);
}

bool ModelingSdkProjectionAdapter::preview_active() const noexcept {
  return impl_->preview_active();
}

ModelingSelectionMode
ModelingSdkProjectionAdapter::selection_mode() const noexcept {
  return impl_->selection_mode();
}

ModelingSelectionSummary
ModelingSdkProjectionAdapter::selection_summary() const {
  return impl_->selection_summary();
}

std::span<const EntityId>
ModelingSdkProjectionAdapter::selected_component_entities() const {
  return impl_->selected_component_entities();
}

ModelingSelectionExport ModelingSdkProjectionAdapter::export_selection() const {
  return impl_->export_selection();
}

std::optional<SceneBounds>
ModelingSdkProjectionAdapter::selection_bounds() const {
  return impl_->selection_bounds();
}

std::vector<EntityId>
ModelingSdkProjectionAdapter::selected_entities() const {
  return impl_->selected_entities();
}

EntityId ModelingSdkProjectionAdapter::active_selected_entity() const {
  return impl_->active_selected_entity();
}

void ModelingSdkProjectionAdapter::add_selection_to_scene() {
  impl_->add_selection_to_scene();
}

std::uint64_t ModelingSdkProjectionAdapter::content_revision() const {
  return impl_->content_revision();
}

std::uint64_t ModelingSdkProjectionAdapter::selection_revision() const {
  return impl_->selection_revision();
}

ModelingSdkProjectionSnapshot
ModelingSdkProjectionAdapter::capture_snapshot() const {
  return impl_->capture_snapshot(false);
}

ModelingSdkProjectionSnapshot
ModelingSdkProjectionAdapter::capture_active_preview_base_snapshot() const {
  return impl_->capture_active_preview_base_snapshot();
}

ModelingSdkProjectionSnapshot
ModelingSdkProjectionAdapter::rebase_snapshot_host_projection(
    const ModelingSdkProjectionSnapshot &snapshot) const {
  return impl_->rebase_snapshot_host_projection(snapshot);
}

ModelingCommandResult ModelingSdkProjectionAdapter::restore_snapshot(
    const ModelingSdkProjectionSnapshot &snapshot) {
  return impl_->restore_snapshot(snapshot);
}

ModelingCommandResult ModelingSdkProjectionAdapter::restore_snapshot_exact(
    const ModelingSdkProjectionSnapshot &snapshot) {
  return impl_->restore_snapshot_exact(snapshot);
}

void ModelingSdkProjectionAdapter::set_failure_point_for_tests(
    ModelingSdkProjectionFailurePoint point) {
  impl_->set_failure_point_for_tests(point);
}

ModelingSdkProjectionStats ModelingSdkProjectionAdapter::stats() const noexcept {
  return impl_->stats();
}

} // namespace quader
