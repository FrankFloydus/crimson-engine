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
#include "math/glm_bridge.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_transform.hpp"

#include <quader/modeling/modeling.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <string>
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
using quader::modeling::Result;
using quader::modeling::SelectionEdit;
using quader::modeling::SelectionKind;
using quader::modeling::SelectionMode;
using quader::modeling::Vec3;
using quader::modeling::VertexId;

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

/// Implements one workspace-scoped SDK session and its host projection map.
class ModelingSdkProjectionAdapter::Impl final {
private:
  using DocumentMutation =
      std::function<Result<OperationResult>(PolygonDocument &)>;
  using DocumentMutationByObject =
      std::function<Result<OperationResult>(ObjectId, PolygonDocument &)>;

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

  friend class ModelingSdkProjectionAdapter;

public:
  Impl(SceneDocument &scene, AssetRegistry &assets,
       ModelingObjectRuntimeMap &object_map,
       ModelingMeshSyncService &mesh_sync)
      : scene_(scene), assets_(assets), object_map_(object_map),
        mesh_sync_(mesh_sync) {}

  void reset_from_scene() {
    session_.reset_workspace_preserving_clipboard();
    object_ids_.clear();
    preview_base_snapshot_.reset();
    workspace_update_base_snapshot_.reset();
    preview_asset_checkpoint_.reset();
    preview_scene_checkpoint_.reset();
    preview_changed_ = false;
    workspace_preview_active_ = false;
    workspace_preview_undo_mark_ = 0U;
    workspace_preview_world_from_object_.clear();
    active_preview_operation_id_.clear();
    active_preview_operation_.reset();
    active_preview_settings_.reset();
    selection_mode_ = ModelingSelectionMode::Object;
    persistent_content_revision_ = 0;
    mode_revision_ = 0;
    active_native_object_id_ = kInvalidModelingObjectId;
    next_native_object_id_ = 1;

    std::vector<ObjectId> selected;
    const EntityId active_entity = scene_.primary_selection();
    for (const EntityId entity : scene_.entity_tree_order()) {
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
      Result<ObjectId> added = session_.add_document(
          std::move(document).value(),
          name != nullptr && !name->name.empty() ? name->name : "Mesh");
      if (!added.ok()) {
        continue;
      }
      object_ids_.emplace(component->object_id, added.value());
      object_map_.bind(component->object_id, entity);
      object_map_.remember_scene_entity(component->object_id, scene_, entity);
      if (!component->sdk_owned) {
        (void)mesh_sync_.mark_projected_object_sdk_owned(scene_, entity);
      }
      if (const SelectionComponent *selection = scene_.selection(entity);
          selection != nullptr && selection->selected) {
        selected.push_back(added.value());
      }
      if (entity == active_entity) {
        active_native_object_id_ = component->object_id;
      }
    }
    if (!selected.empty()) {
      (void)session_.apply_object_selection(selected, SelectionEdit::Replace);
    }
    session_.clear_history();
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
    return std::ranges::any_of(session_.objects(),
                               [](const auto &object) {
                                 return object.selected;
                               });
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
      const MaterialReference material_id =
          material != nullptr ? material->material
                              : MaterialReference{kInvalidAssetId};
      kept_mesh->name = "Cut Preview Keep";
      discarded_mesh->name = "Cut Preview Discard";
      preview.kept_meshes.push_back({
          .source_entity = *entity,
          .material = material_id,
          .mesh = std::move(*kept_mesh),
          .transform = world,
      });
      preview.discarded_meshes.push_back({
          .source_entity = *entity,
          .material = material_id,
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

      const QMat4 world = scene_.world_transform_matrix(*entity);
      const QMat4 local_from_world = inverse(world);
      const QVec3 first =
          transform_point(local_from_world, world_plane_points[0]);
      const QVec3 second =
          transform_point(local_from_world, world_plane_points[1]);
      const QVec3 third =
          transform_point(local_from_world, world_plane_points[2]);
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
      mesh->name = "Mirror Preview";
      preview.meshes.push_back({
          .source_entity = *entity,
          .material = material != nullptr
                          ? material->material
                          : MaterialReference{kInvalidAssetId},
          .mesh = std::move(*mesh),
          .transform = world,
      });
    }

    preview.command = {.ok = true, .changed = false};
    return preview;
  }

  [[nodiscard]] quader_editor_core::EditorSession
  native_selection_projection() const {
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
    quader_editor_core::EditorSession projected = native_selection_projection();
    ModelingSelectionService selection;
    const std::vector<int> object_ids =
        selection.box_pick_object_ids(projected, scene_, object_map_, request);
    std::vector<EntityId> entities;
    entities.reserve(object_ids.size());
    for (const int object_id : object_ids) {
      const std::optional<EntityId> entity =
          object_map_.entity_for_modeling_object(object_id);
      if (entity.has_value()) {
        entities.push_back(*entity);
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
                           request.material_slot, request.material, finalize);
  }

  [[nodiscard]] ModelingCreateBoxResult create_box_from_corners(
      std::span<const QVec3> corners, std::string_view requested_name,
      MaterialReference material, const CreationFinalizer &finalize) {
    std::vector<Vec3> sdk_corners;
    sdk_corners.reserve(corners.size());
    for (const QVec3 corner : corners) {
      sdk_corners.push_back(to_sdk(corner));
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
        0U, material, finalize);
  }

  [[nodiscard]] ModelingCreateBoxResult
  create_face(std::span<const QVec3> points, QVec3 requested_normal,
              std::string_view requested_name, MaterialReference material,
              const CreationFinalizer &finalize) {
    constexpr float kEpsilonSquared = 0.00000001F;
    std::vector<Vec3> sdk_points;
    sdk_points.reserve(points.size());
    QVec3 polygon_normal{};
    for (std::size_t index = 0; index < points.size(); ++index) {
      const QVec3 point = points[index];
      if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
          !std::isfinite(point.z)) {
        return {.ok = false,
                .message = "Poly vertices must be finite."};
      }
      const QVec3 next = points[(index + 1U) % points.size()];
      polygon_normal = polygon_normal + cross(point, next);
      sdk_points.push_back(to_sdk(point));
    }
    if (sdk_points.size() < 3U ||
        length_squared(polygon_normal) <= kEpsilonSquared) {
      return {.ok = false,
              .message = "Poly needs a non-degenerate polygon."};
    }
    if (length_squared(requested_normal) > kEpsilonSquared &&
        dot(polygon_normal, requested_normal) < 0.0F) {
      std::reverse(sdk_points.begin(), sdk_points.end());
    }
    Result<PolygonDocument> document = PolygonDocument::make_face(sdk_points);
    if (!document.ok()) {
      return {.ok = false,
              .message = document.error_message().empty()
                             ? "Could not create the poly face."
                             : document.error_message()};
    }
    return create_document(std::move(document).value(), requested_name, 0U,
                           material, finalize);
  }

  [[nodiscard]] ModelingCreateBoxResult
  create_document(PolygonDocument document, std::string_view requested_name,
                  std::uint32_t material_slot, MaterialReference material,
                  const CreationFinalizer &finalize) {

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
    quader_poly::Document reconciled = document;
    Result<PolygonDocument> current = session_.document_copy(*object);
    Result<AuthoredTopology> current_topology =
        current.ok()
            ? current.value().authored_topology()
            : Result<AuthoredTopology>::failure(current.error());
    if (!current_topology.ok()) {
      return failure(current_topology.error_message());
    }
    reconciled.next_vertex_id =
        std::max(reconciled.next_vertex_id,
                 current_topology.value().next_vertex_id);
    reconciled.next_face_id =
        std::max(reconciled.next_face_id,
                 current_topology.value().next_face_id);
    Result<PolygonDocument> converted =
        MeshProjectionService::sdk_document_from_polygon_document(reconciled);
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
      const ModelingSdkProjectionSnapshot base = capture_snapshot();
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
      const ModelingSdkProjectionSnapshot base = capture_snapshot();
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
      const ModelingSdkProjectionSnapshot base = capture_snapshot();
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
    preview_base_snapshot_ = capture_snapshot();
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
    const std::size_t current_selected_count = static_cast<std::size_t>(
        std::ranges::count_if(current_objects,
                              [](const auto &object) {
                                return object.selected;
                              }));
    const bool same_selection =
        current_selected_count == selected.size() &&
        std::ranges::all_of(current_objects, [&](const auto &object) {
          return object.selected ==
                 (std::ranges::find(selected, object.id) != selected.end());
        });
    if (same_selection && active_native_object_id_ == active_native &&
        selection_mode_ == ModelingSelectionMode::Object) {
      return {.ok = true, .changed = false};
    }

    const quader::modeling::ModelingSession session_before = session_;
    const SceneProjectionCheckpoint scene_before =
        scene_.capture_projection_checkpoint();
    Result<OperationResult> result =
        session_.apply_object_selection(selected, SelectionEdit::Replace);
    if (!result.ok()) {
      return failure(result.error_message());
    }
    if (fails(ModelingSdkProjectionFailurePoint::AfterSdkMutation)) {
      session_.restore_snapshot_preserving_identity_high_water(session_before);
      (void)scene_.restore_projection_checkpoint(scene_before);
      return failure("Injected failure after SDK selection mutation.");
    }

    if (publish_scene) {
      scene_.clear_selection();
      if (fails(
              ModelingSdkProjectionFailurePoint::SelectionScenePublication)) {
        session_.restore_snapshot_preserving_identity_high_water(
            session_before);
        (void)scene_.restore_projection_checkpoint(scene_before);
        return failure("Injected failure during SDK selection publication.");
      }
      for (const EntityId entity : entities) {
        if (owns_entity(entity) && !scene_.set_selected(entity, true)) {
          session_.restore_snapshot_preserving_identity_high_water(
              session_before);
          (void)scene_.restore_projection_checkpoint(scene_before);
          return failure("SDK scene selection could not be projected.");
        }
      }
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
    ModelingCommandResult result{.ok = true, .changed = false};
    for (std::size_t index = 0; index < requests.size(); ++index) {
      ModelingCommandResult current = set_component_selection_without_preview(
          requests[index], index != 0U);
      if (!current.ok) {
        result = std::move(current);
        break;
      }
      result.changed = result.changed || current.changed;
      if (!current.message.empty()) {
        result.message = std::move(current.message);
      }
    }
    return finish_operation_preview_selection_edit(preview_edit,
                                                   std::move(result));
  }

  [[nodiscard]] ModelingCommandResult set_component_selection_without_preview(
      const ModelingComponentSelectionRequest &request,
      bool preserve_other_objects = false) {
    const std::optional<ObjectId> object = sdk_object(request.modeling_object_id);
    if (!object.has_value()) {
      return failure("SDK modeling object ID is not live.");
    }

    const std::size_t undo_mark = session_.undo_depth();
    const quader::modeling::ModelingSession session_before = session_;
    const int active_before = active_native_object_id_;
    const ModelingSelectionMode selection_mode_before = selection_mode_;
    const std::uint64_t mode_revision_before = mode_revision_;
    const SceneProjectionCheckpoint scene_before =
        scene_.capture_projection_checkpoint();
    const auto rollback = [&](std::string message) {
      session_.restore_snapshot_preserving_identity_high_water(session_before);
      active_native_object_id_ = active_before;
      selection_mode_ = selection_mode_before;
      mode_revision_ = mode_revision_before;
      (void)scene_.restore_projection_checkpoint(scene_before);
      return failure(std::move(message));
    };
    Result<PolygonDocument> copied = session_.document_copy(*object);
    if (!copied.ok()) {
      return rollback(copied.error_message());
    }
    PolygonDocument document = std::move(copied).value();
    Result<OperationResult> mode =
        document.set_selection_mode(to_sdk_component_mode(request.mode));
    if (!mode.ok()) {
      return rollback(mode.error_message());
    }

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
    selection.has_active = request.active_kind !=
                           ModelingActiveSelectionKind::None;
    if (request.active_vertex > 0) {
      selection.active_vertex =
          quader::modeling::make_id<quader::modeling::VertexTag>(
              static_cast<std::uint32_t>(request.active_vertex));
    }
    if (request.active_edge.first > 0 && request.active_edge.second > 0) {
      selection.active_edge = quader::modeling::make_edge_key(
          quader::modeling::make_id<quader::modeling::VertexTag>(
              static_cast<std::uint32_t>(request.active_edge.first)),
          quader::modeling::make_id<quader::modeling::VertexTag>(
              static_cast<std::uint32_t>(request.active_edge.second)));
    }
    if (request.active_face > 0) {
      selection.active_face =
          quader::modeling::make_id<quader::modeling::FaceTag>(
              static_cast<std::uint32_t>(request.active_face));
    }

    Result<OperationResult> selected =
        document.apply_selection(selection, to_sdk(request.edit));
    if (!selected.ok()) {
      return rollback(selected.error_message());
    }
    const PolygonSelectionSnapshot effective_selection = document.selection();
    const bool target_has_selection =
        !effective_selection.vertices.empty() ||
        !effective_selection.edges.empty() || !effective_selection.faces.empty();
    const bool target_was_selected = std::ranges::any_of(
        session_.objects(), [&](const auto &summary) {
          return summary.id == *object && summary.selected;
        });
    std::vector<ObjectId> selected_objects;
    if (preserve_other_objects ||
        request.edit != ModelingSelectionEdit::Replace) {
      for (const quader::modeling::SessionObjectSummary &summary :
           session_.objects()) {
        if (summary.selected && summary.id != *object) {
          selected_objects.push_back(summary.id);
        }
      }
    }
    if (target_has_selection ||
        (request.edit == ModelingSelectionEdit::Add &&
         target_was_selected)) {
      selected_objects.insert(selected_objects.begin(), *object);
    }
    OperationResult receipt = selected.value();
    receipt.changed = receipt.changed || mode.value().changed;
    receipt.dirty.selection = receipt.changed;
    receipt.dirty.overlays = receipt.changed;
    Result<OperationResult> committed = session_.commit_document(
        *object, std::move(document), std::move(receipt));
    if (!committed.ok()) {
      return rollback(committed.error_message());
    }
    Result<OperationResult> object_selected = session_.apply_object_selection(
        selected_objects, SelectionEdit::Replace);
    if (!object_selected.ok()) {
      return rollback(object_selected.error_message());
    }
    committed.value().changed =
        committed.value().changed || object_selected.value().changed;
    committed.value().dirty.selection = committed.value().changed;
    committed.value().dirty.overlays = committed.value().changed;
    if (fails(ModelingSdkProjectionFailurePoint::AfterSdkMutation)) {
      return rollback("Injected failure after SDK component selection.");
    }

    const std::optional<EntityId> entity =
        object_map_.entity_for_modeling_object(request.modeling_object_id);
    if (!entity.has_value() || !scene_.exists(*entity)) {
      return rollback("SDK component selection has no scene projection.");
    }
    scene_.clear_selection();
    if (fails(ModelingSdkProjectionFailurePoint::
                  SelectionScenePublication)) {
      return rollback("Injected failure during SDK component selection publication.");
    }
    for (ObjectId selected_object : selected_objects) {
      const std::optional<int> selected_native = native_object(selected_object);
      const std::optional<EntityId> selected_entity =
          selected_native.has_value()
              ? object_map_.entity_for_modeling_object(*selected_native)
              : std::nullopt;
      if (!selected_entity.has_value() || !scene_.exists(*selected_entity) ||
          !scene_.set_selected(*selected_entity, true)) {
        return rollback("SDK component selection scene projection failed.");
      }
    }
    if (target_has_selection ||
        (request.edit == ModelingSelectionEdit::Add && target_was_selected)) {
      (void)scene_.set_active_selection(*entity);
    }
    active_native_object_id_ = request.modeling_object_id;
    selection_mode_ = request.mode;
    session_.squash_undo_since(undo_mark);
    return from_sdk(committed);
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
          if (std::find(current.edges.begin(), current.edges.end(), edge) ==
              current.edges.end()) {
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
    const PreviewSelectionEditState preview_edit =
        suspend_operation_preview_for_selection_edit();
    if (!preview_edit.ok) {
      return selection_summary();
    }
    quader_editor_core::EditorSession projected = native_selection_projection();
    const quader_editor_core::RectangleSelectionState baseline =
        selection.rectangle_selection_state(projected);
    return box_select(request, baseline, selection, projected, preview_edit);
  }

  [[nodiscard]] ModelingSelectionSummary box_select(
      const ModelingBoxSelectRequest &request,
      const quader_editor_core::RectangleSelectionState &baseline,
      ModelingSelectionService &selection) {
    const PreviewSelectionEditState preview_edit =
        suspend_operation_preview_for_selection_edit();
    if (!preview_edit.ok) {
      return selection_summary();
    }
    quader_editor_core::EditorSession projected = native_selection_projection();
    const quader_editor_core::RectangleSelectionState &effective_baseline =
        preview_edit.active ? selection.rectangle_selection_state(projected)
                            : baseline;
    return box_select(request, effective_baseline, selection, projected,
                      preview_edit);
  }

  [[nodiscard]] ModelingSelectionSummary box_select(
      const ModelingBoxSelectRequest &request,
      const quader_editor_core::RectangleSelectionState &baseline,
      ModelingSelectionService &selection,
      quader_editor_core::EditorSession &projected,
      const PreviewSelectionEditState &preview_edit) {
    const ModelingSelectionApplyResult applied =
        selection.apply_rectangle_selection(projected, scene_, object_map_,
                                             request, baseline);
    if (applied.changed) {
      (void)apply_native_selection_projection(projected);
    }
    (void)finish_operation_preview_selection_edit(
        preview_edit, {.ok = true, .changed = applied.changed});
    return selection_summary();
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

  [[nodiscard]] ModelingCommandResult assign_material_slot_to_face(
      int modeling_object_id, int face_id, std::uint32_t material_slot) {
    const std::optional<ObjectId> object = sdk_object(modeling_object_id);
    if (!object.has_value() || face_id <= 0) {
      return failure("Material target face is not live.");
    }
    const std::array<ObjectId, 1> objects{*object};
    return mutate_documents(
        objects,
        [face_id, material_slot](PolygonDocument &document) {
          return document.assign_face_material_slot(
              quader::modeling::make_id<quader::modeling::FaceTag>(
                  static_cast<std::uint32_t>(face_id)),
              material_slot);
        },
        "Assign material slot failed.");
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
    const std::array<ObjectId, 1> objects{*object};
    ModelingCommandResult result = mutate_documents(
        objects,
        [sdk_points = std::move(sdk_points),
         sdk_segments = std::move(sdk_segments)](PolygonDocument &document) {
          return document.knife_stroke(sdk_points, sdk_segments);
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
    selection_mode_ = ModelingSelectionMode::Vertex;
    active_native_object_id_ = modeling_object_id;
    add_selection_to_scene();
    return result;
  }

  [[nodiscard]] ModelingCommandResult
  begin_translate_preview(bool duplicate_selected = false) {
    if (preview_active()) {
      return failure("An SDK modeling preview is already active.");
    }
    if (selected_sdk_objects().empty()) {
      return failure("No active SDK modeling selection.");
    }
    workspace_preview_undo_mark_ = session_.undo_depth();
    preview_base_snapshot_ = capture_snapshot();
    preview_asset_checkpoint_ = assets_.capture_projection_checkpoint();
    preview_scene_checkpoint_ = scene_.capture_projection_checkpoint();
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
    workspace_update_base_snapshot_ = capture_snapshot();
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
      std::string_view fallback_message) {
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
    ModelingCommandResult restored = restore_snapshot_exact(update_base);
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
    const std::vector<ObjectId> selected = selected_sdk_objects();
    ModelingCommandResult result = mutate_documents_by_object(
        selected, mutation, fallback_message, true);
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
      const DocumentMutation &mutation, std::string_view fallback_message) {
    return update_workspace_transform_preview_by_object(
        [&mutation](ObjectId, PolygonDocument &document) {
          return mutation(document);
        },
        fallback_message);
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
    return workspace_preview_active_
               ? update_workspace_transform_preview(
                     mutation, "SDK translate preview failed.")
               : update_preview_from_base(
                     mutation, "SDK translate preview failed.");
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
    return workspace_preview_active_
               ? update_workspace_transform_preview(
                     mutation, "SDK transform preview failed.")
               : update_preview_from_base(
                     mutation, "SDK transform preview failed.");
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
    return update_workspace_transform_preview_by_object(
        [sdk_transforms = std::move(sdk_transforms), kind,
         closed_edge_ledge_size](ObjectId object, PolygonDocument &document) {
          const auto found = std::ranges::find_if(
              sdk_transforms,
              [object](const SdkTransform &candidate) {
                return candidate.object == object;
              });
          if (found == sdk_transforms.end()) {
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
        },
        "SDK object-local transform preview failed.");
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

  [[nodiscard]] ModelingCommandResult commit_translate_preview() {
    if (!preview_active()) {
      return failure("No active SDK translate preview.");
    }
    if (workspace_preview_active_) {
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
    if (!preview_active()) {
      return failure("No active SDK translate preview.");
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

  [[nodiscard]] bool preview_active() const noexcept {
    return workspace_preview_active_ || session_.preview_active();
  }

  [[nodiscard]] ModelingSelectionMode selection_mode() const noexcept {
    return selection_mode_;
  }

  [[nodiscard]] ModelingSelectionSummary selection_summary() const {
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
      return summary;
    }

    const std::optional<ObjectId> active = active_sdk_object();
    if (!active.has_value()) {
      return summary;
    }
    PolygonSelectionSnapshot active_selection;
    bool active_selection_available = false;
    const quader::modeling::PreviewTransactionSummary preview =
        session_.preview_summary();
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
    return summary;
  }

  [[nodiscard]] ModelingSelectionExport export_selection() const {
    ModelingSelectionExport exported;
    exported.summary = selection_summary();
    for (const auto &[native_id, object_id] : object_ids_) {
      const auto summaries = session_.objects();
      const auto summary = std::find_if(
          summaries.begin(), summaries.end(),
          [object_id](const auto &candidate) { return candidate.id == object_id; });
      if (summary == summaries.end() ||
          (!summary->selected && native_id != active_native_object_id_)) {
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
      object.selected = summary->selected;
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
        const QMat4 world_from_object =
            scene_.world_transform_matrix(*entity);
        const auto include_vertex = [&](VertexId id) {
          const auto vertex = std::find_if(
              topology.value().vertices.begin(),
              topology.value().vertices.end(),
              [id](const auto &candidate) { return candidate.id == id; });
          if (vertex == topology.value().vertices.end()) {
            return;
          }
          const QVec3 point = transform_point(
              world_from_object,
              {vertex->position.x, vertex->position.y, vertex->position.z});
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
          const auto face = std::find_if(
              topology.value().faces.begin(), topology.value().faces.end(),
              [id](const auto &candidate) { return candidate.id == id; });
          if (face != topology.value().faces.end()) {
            for (const VertexId vertex : face->vertices) {
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
    for (const EntityId entity : scene_.selected_entities()) {
      if (owns_entity(entity)) {
        (void)scene_.set_selected(entity, false);
      }
    }
    for (const EntityId entity : selected_entities()) {
      if (scene_.exists(entity)) {
        (void)scene_.set_selected(entity, true);
      }
    }
    const EntityId active = active_selected_entity();
    if (active != kInvalidEntityId && scene_.exists(active)) {
      (void)scene_.set_active_selection(active);
    }
  }

  [[nodiscard]] std::uint64_t content_revision() const {
    return persistent_content_revision_;
  }

  [[nodiscard]] std::uint64_t selection_revision() const {
    return session_.selection_revision() + mode_revision_;
  }

  [[nodiscard]] ModelingSdkProjectionSnapshot capture_snapshot() const {
    auto snapshot = std::make_shared<ModelingSdkProjectionSnapshot::Impl>();
    snapshot->session = session_;
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

  [[nodiscard]] ModelingSdkProjectionSnapshot rebase_snapshot_host_projection(
      const ModelingSdkProjectionSnapshot &snapshot) const {
    if (!snapshot.valid()) {
      return {};
    }

    auto rebased =
        std::make_shared<ModelingSdkProjectionSnapshot::Impl>(*snapshot.impl_);
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
    return preview_base_snapshot_.value_or(ModelingSdkProjectionSnapshot{});
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
    const quader::modeling::ModelingSession session_before = session_;
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
    const auto rollback = [&](std::string message) {
      session_.restore_snapshot_preserving_identity_high_water(session_before);
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
              projected.material_slot, true, projected.mesh_id)) {
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
      bool share_source_mesh = false) {
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
    native_id = next_available_native_object_id();
    if (native_id == kInvalidModelingObjectId ||
        native_id == std::numeric_limits<int>::max() ||
        !mesh_sync_.bind_projected_object(scene_, entity, native_id,
                                          material_slot, true, mesh_id)) {
      return false;
    }
    mesh_sync_.apply_scene_identity_to_duplicate_entity(scene_, entity,
                                                        source_identity);
    object_ids_.emplace(native_id, object_id);
    object_map_.bind(native_id, entity);
    object_map_.remember_scene_entity(native_id, scene_, entity);
    next_native_object_id_ = native_id + 1;
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
    for (int native_id : source_modeling_object_ids) {
      const std::optional<ObjectId> source = sdk_object(native_id);
      if (!source.has_value() ||
          std::ranges::find(sources, *source) != sources.end()) {
        return failure_result("Duplicate source object is not live or unique.");
      }
      sources.push_back(*source);
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

    ModelingSdkDuplicateBatchResult result{
        .command = {.ok = true,
                    .changed = true,
                    .message = "Duplicated selected meshes."},
    };
    result.duplicates.reserve(sources.size() * copies_per_source);
    std::vector<ObjectId> selection = select_sources ? sources
                                                     : std::vector<ObjectId>{};
    selection.reserve(selection.size() + sources.size() * copies_per_source);
    for (std::size_t source_index = 0; source_index < sources.size();
         ++source_index) {
      const ObjectId source = sources[source_index];
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
      for (std::size_t copy = 0; copy < copies_per_source; ++copy) {
        Result<ObjectId> duplicated = session_.duplicate_object(source, {});
        if (!duplicated.ok()) {
          return rollback(duplicated.error_message());
        }
        int native_id = kInvalidModelingObjectId;
        EntityId entity = kInvalidEntityId;
        const bool published = publish_new_object(
            duplicated.value(), *identity, component->material_slot, native_id,
            entity, share_source_mesh_assets);
        if (entity != kInvalidEntityId) {
          created_entities.push_back(entity);
        }
        if (!published) {
          return rollback("SDK duplicate host projection failed.");
        }
        ModelingObjectRecord record{
            .modeling_object_id = native_id,
            .entity = entity,
            .name = identity->name,
        };
        if (const MeshComponent *mesh = scene_.mesh(entity)) {
          record.mesh = mesh->mesh;
        }
        result.duplicates.push_back(std::move(record));
        selection.push_back(duplicated.value());
        active_native_object_id_ = native_id;
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
    for (std::size_t index = 0; index < pasted.value().objects.size(); ++index) {
      ModelingObjectSceneIdentity identity = source_identities[index];
      const auto summary = std::ranges::find_if(
          summaries, [&](const auto &candidate) {
            return candidate.id == pasted.value().objects[index];
          });
      if (summary == summaries.end()) {
        return rollback("SDK clipboard object summary is unavailable.");
      }
      if (identity.name.empty()) {
        identity.name = summary->name;
      }
      const std::uint32_t material_slot =
          identity.material_slot;
      int native_id = kInvalidModelingObjectId;
      EntityId entity = kInvalidEntityId;
      const bool published = publish_new_object(
          pasted.value().objects[index], identity, material_slot, native_id,
          entity);
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

    std::vector<ObjectId> duplicates;
    duplicates.reserve(sources.size());
    for (ObjectId source : sources) {
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

      Result<ObjectId> duplicated = session_.duplicate_object(
          source, to_sdk(geometry_offset), std::string(duplicate_name));
      if (!duplicated.ok()) {
        return rollback(duplicated.error_message());
      }
      Result<PolygonDocument> document =
          session_.document_copy(duplicated.value());
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
              duplicated.value(), std::move(document).value(),
              std::move(mutated).value());
          if (!committed.ok()) {
            return rollback(committed.error_message());
          }
          document = session_.document_copy(duplicated.value());
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
      const int native_id = next_available_native_object_id();
      if (native_id == kInvalidModelingObjectId ||
          native_id == std::numeric_limits<int>::max() ||
          !mesh_sync_.bind_projected_object(
              scene_, entity, native_id, source_component->material_slot, true,
              mesh_id)) {
        return rollback("SDK duplicate scene binding could not be installed.");
      }
      mesh_sync_.apply_scene_identity_to_duplicate_entity(
          scene_, entity, duplicate_identity);
      object_ids_.emplace(native_id, duplicated.value());
      object_map_.bind(native_id, entity);
      object_map_.remember_scene_entity(native_id, scene_, entity);
      next_native_object_id_ = native_id + 1;
      active_native_object_id_ = native_id;
      duplicates.push_back(duplicated.value());
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
    const ModelingSdkProjectionSnapshot base = capture_snapshot();
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
            source_component->material_slot, true, extracted_mesh)) {
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
    std::vector<ObjectId> seen;
    for (const ModelingSdkPlaneCutRequest &request : requests) {
      const std::optional<ObjectId> source =
          sdk_object(request.modeling_object_id);
      const std::optional<EntityId> source_entity =
          object_map_.entity_for_modeling_object(request.modeling_object_id);
      if (!source.has_value() || !source_entity.has_value() ||
          !scene_.exists(*source_entity) ||
          std::ranges::find(seen, *source) != seen.end()) {
        return rollback("Cut target mesh was not found or was duplicated.");
      }
      seen.push_back(*source);
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
            back_entity);
        if (back_entity != kInvalidEntityId) {
          created_entities.push_back(back_entity);
        }
        if (!published) {
          return rollback("Cut back projection failed.");
        }
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
          back_entity);
      if (back_entity != kInvalidEntityId) {
        created_entities.push_back(back_entity);
      }
      if (!published) {
        return rollback("Half-Cut back projection failed.");
      }
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
      bool preview_publication = false) {
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
    std::vector<ObjectId> changed_objects;
    for (ObjectId object_id : object_ids) {
      Result<PolygonDocument> copied = session_.document_copy(object_id);
      if (!copied.ok()) {
        return rollback(copied.error_message());
      }
      PolygonDocument working = std::move(copied).value();
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
      if (!operated.value().changed) {
        continue;
      }
      Result<OperationResult> committed = session_.commit_document(
          object_id, std::move(working), std::move(operated).value());
      if (!committed.ok()) {
        return rollback(committed.error_message());
      }
      changed = changed || committed.value().changed;
      if (committed.value().changed) {
        changed_objects.push_back(object_id);
      }
    }

    if (!changed) {
      (void)session_.rollback_undo_since(undo_mark);
      if (preview_publication) {
        active_preview_deleted_vertex_count_ = deleted_vertex_count;
      }
      return {.ok = true,
              .changed = false,
              .message = message.empty() ? std::string(fallback_message)
                                         : std::move(message)};
    }
    if (fails(ModelingSdkProjectionFailurePoint::AfterSdkMutation)) {
      return rollback("Injected failure after SDK operation mutation.");
    }
    for (ObjectId object_id : changed_objects) {
      Result<PolygonDocument> projected = session_.document_copy(object_id);
      if (!projected.ok() ||
          !project_document(object_id, projected.value(),
                            preview_publication)) {
        return rollback("SDK operation host projection failed.");
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
    add_selection_to_scene();
    return {.ok = true,
            .changed = true,
            .message = message.empty() ? std::string(fallback_message)
                                       : std::move(message)};
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
    const auto found = std::find_if(
        object_ids_.begin(), object_ids_.end(),
        [object_id](const auto &entry) { return entry.second == object_id; });
    return found == object_ids_.end() ? std::nullopt
                                      : std::optional<int>{found->first};
  }

  [[nodiscard]] std::optional<ObjectId> active_sdk_object() const {
    return sdk_object(active_native_object_id_);
  }

  [[nodiscard]] int next_available_native_object_id() const {
    int candidate = next_native_object_id_;
    for (const EntityId entity : scene_.entities()) {
      const ModelingObjectComponent *component = scene_.modeling_object(entity);
      if (component != nullptr && component->object_id >= candidate &&
          component->object_id < std::numeric_limits<int>::max()) {
        candidate = component->object_id + 1;
      }
    }
    return candidate;
  }

  [[nodiscard]] bool project_document(ObjectId object_id,
                                      const PolygonDocument &document,
                                      bool preview) {
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
    ++stats_.mesh_compile_count;
    std::optional<MeshAsset> projected =
        MeshProjectionService::compile_sdk_document_mesh(document);
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
    if (scene_mesh_is_shared(scene_, *entity, binding->mesh)) {
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
    active_preview_operation_id_.clear();
    active_preview_operation_.reset();
    active_preview_settings_.reset();
    active_preview_deleted_vertex_count_ = 0U;
  }

  SceneDocument &scene_;
  AssetRegistry &assets_;
  ModelingObjectRuntimeMap &object_map_;
  ModelingMeshSyncService &mesh_sync_;
  quader::modeling::ModelingSession session_;
  std::map<int, ObjectId> object_ids_;
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
  std::string active_preview_operation_id_;
  std::optional<quader_editor_core::EditorOperationId>
      active_preview_operation_;
  std::optional<quader_editor_core::EditorOperationSettings>
      active_preview_settings_;
  std::size_t active_preview_deleted_vertex_count_ = 0U;
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

void ModelingSdkProjectionAdapter::reset_from_scene() {
  impl_->reset_from_scene();
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

std::optional<EntityId> ModelingSdkProjectionAdapter::pick_object_entity(
    const ModelingPickSelectRequest &request,
    std::optional<EntityId> competing_native) const {
  return impl_->pick_object_entity(request, competing_native);
}

std::vector<EntityId>
ModelingSdkProjectionAdapter::box_pick_object_entities(
    const ModelingBoxSelectRequest &request) const {
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
  return impl_->box_select(request, selection);
}

ModelingSelectionSummary ModelingSdkProjectionAdapter::box_select(
    const ModelingBoxSelectRequest &request,
    const quader_editor_core::RectangleSelectionState &baseline,
    ModelingSelectionService &selection) {
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
    bool duplicate_selected) {
  return impl_->begin_translate_preview(duplicate_selected);
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
  return impl_->capture_snapshot();
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
