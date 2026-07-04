////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/services/modeling_session_service.hpp"

#include "assets/asset_registry.hpp"
#include "core/i_undoable_command.hpp"
#include "editor/commands/document_command_service.hpp"
#include "editor/history/editor_history_service.hpp"
#include "editor/commands/modeling/scene_aware_delete_selection_command.hpp"
#include "editor/modeling/services/modeling_hierarchy_duplicate_service.hpp"
#include "editor/modeling/services/modeling_instant_operation_catalog.hpp"
#include "editor/modeling/services/modeling_session_services.hpp"
#include "editor/modeling/services/selection_mode_adapter.hpp"
#include "editor/modeling/services/modeling_operation_settings.hpp"
#include "editor/modeling/services/modeling_transform_drag_service.hpp"
#include "editor/tools/viewport_tool_descriptor_catalog.hpp"
#include "editor/selection/selection_projection_service.hpp"
#include "diagnostics/profile.hpp"
#include "math/glm_bridge.hpp"
#include "scene/scene_bounds.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_entity_naming.hpp"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <editor/modeling/operations/operation_command.hpp>
#include <editor/modeling/runtime/repeat_last_action.hpp>
#include <editor/modeling/runtime/runtime_command.hpp>
#include <editor/modeling/runtime/session.hpp>
#include <editor/interactions/box_tool.hpp>
#include <editor/interactions/cut_tool.hpp>
#include <editor/interactions/mirror_tool.hpp>
#include <editor/interactions/selection_box.hpp>
#include <editor/interactions/viewport_projection.hpp>
#include <editor/interactions/selection_edit.hpp>
#include <editor/metadata/host_contract.hpp>
#include <mesh/polygon/document.hpp>

namespace quader {
namespace {

quader::QVec3 to_runtime(QVec3 value) { return {value.x, value.y, value.z}; }

quader::QVec2 to_runtime(QVec2 value) { return {value.x, value.y}; }

quader_poly::Ray to_runtime(const ModelingRay &ray) {
  return {
      .origin = to_runtime(ray.origin),
      .direction = to_runtime(normalize(ray.direction)),
  };
}

quader_editor_core::ViewportCameraState
to_runtime(const ModelingViewportCamera &value) {
  quader_editor_core::ViewportCameraState camera;
  camera.projection = value.orthographic
                          ? quader_editor_core::CameraProjection::Orthographic
                          : quader_editor_core::CameraProjection::Perspective;
  camera.transform = {
      .x_axis = to_runtime(value.x_axis),
      .y_axis = to_runtime(value.y_axis),
      .z_axis = to_runtime(value.z_axis),
      .origin = to_runtime(value.origin),
  };
  camera.viewport_size = {
      std::max(value.viewport_size.x, 1.0f),
      std::max(value.viewport_size.y, 1.0f),
  };
  camera.orthographic_size = std::max(value.orthographic_size, 0.0001);
  camera.fov_degrees = value.fov_degrees;
  return camera;
}

quader_poly::Transform3 to_runtime(const ModelingTransform3 &transform) {
  return {
      .x_axis = to_runtime(transform.x_axis),
      .y_axis = to_runtime(transform.y_axis),
      .z_axis = to_runtime(transform.z_axis),
      .origin = to_runtime(transform.origin),
  };
}

quader_editor_core::ViewportInteractionTool
to_runtime(ModelingViewportTool tool) {
  switch (tool) {
  case ModelingViewportTool::Move:
    return quader_editor_core::ViewportInteractionTool::Move;
  case ModelingViewportTool::Extend:
    return quader_editor_core::ViewportInteractionTool::Extend;
  case ModelingViewportTool::Extrude:
    return quader_editor_core::ViewportInteractionTool::Extrude;
  case ModelingViewportTool::Rotate:
    return quader_editor_core::ViewportInteractionTool::Rotate;
  case ModelingViewportTool::Scale:
    return quader_editor_core::ViewportInteractionTool::Scale;
  case ModelingViewportTool::Box:
    return quader_editor_core::ViewportInteractionTool::Box;
  case ModelingViewportTool::InsertEdgeLoop:
    return quader_editor_core::ViewportInteractionTool::InsertEdgeLoop;
  case ModelingViewportTool::Knife:
    return quader_editor_core::ViewportInteractionTool::Knife;
  case ModelingViewportTool::Cut:
    return quader_editor_core::ViewportInteractionTool::Cut;
  case ModelingViewportTool::Poly:
    return quader_editor_core::ViewportInteractionTool::Poly;
  case ModelingViewportTool::Mirror:
    return quader_editor_core::ViewportInteractionTool::Mirror;
  case ModelingViewportTool::Select:
    return quader_editor_core::ViewportInteractionTool::Select;
  }
  return quader_editor_core::ViewportInteractionTool::Select;
}

ModelingViewportTool
from_runtime_viewport_tool(quader_editor_core::ViewportInteractionTool tool) {
  switch (tool) {
  case quader_editor_core::ViewportInteractionTool::Move:
    return ModelingViewportTool::Move;
  case quader_editor_core::ViewportInteractionTool::Extend:
    return ModelingViewportTool::Extend;
  case quader_editor_core::ViewportInteractionTool::Extrude:
    return ModelingViewportTool::Extrude;
  case quader_editor_core::ViewportInteractionTool::Rotate:
    return ModelingViewportTool::Rotate;
  case quader_editor_core::ViewportInteractionTool::Scale:
    return ModelingViewportTool::Scale;
  case quader_editor_core::ViewportInteractionTool::Box:
    return ModelingViewportTool::Box;
  case quader_editor_core::ViewportInteractionTool::InsertEdgeLoop:
    return ModelingViewportTool::InsertEdgeLoop;
  case quader_editor_core::ViewportInteractionTool::Knife:
    return ModelingViewportTool::Knife;
  case quader_editor_core::ViewportInteractionTool::Cut:
    return ModelingViewportTool::Cut;
  case quader_editor_core::ViewportInteractionTool::Poly:
    return ModelingViewportTool::Poly;
  case quader_editor_core::ViewportInteractionTool::Mirror:
    return ModelingViewportTool::Mirror;
  case quader_editor_core::ViewportInteractionTool::Select:
  case quader_editor_core::ViewportInteractionTool::Unknown:
    break;
  }
  return ModelingViewportTool::Select;
}

quader_editor_core::CutMode to_runtime(ModelingCutMode mode) {
  switch (mode) {
  case ModelingCutMode::DiscardLeft:
    return quader_editor_core::CutMode::KeepBack;
  case ModelingCutMode::DiscardRight:
    return quader_editor_core::CutMode::KeepFront;
  case ModelingCutMode::KeepBoth:
    return quader_editor_core::CutMode::KeepBoth;
  }
  return quader_editor_core::CutMode::KeepBoth;
}

ModelingCutMode from_runtime(quader_editor_core::CutMode mode) {
  switch (mode) {
  case quader_editor_core::CutMode::KeepBack:
    return ModelingCutMode::DiscardLeft;
  case quader_editor_core::CutMode::KeepFront:
    return ModelingCutMode::DiscardRight;
  case quader_editor_core::CutMode::KeepBoth:
    return ModelingCutMode::KeepBoth;
  }
  return ModelingCutMode::KeepBoth;
}

QVec3 from_runtime(quader::QVec3 value) { return {value.x, value.y, value.z}; }

ModelingTransform3 from_runtime(quader_poly::Transform3 value) {
  return {
      .x_axis = from_runtime(value.x_axis),
      .y_axis = from_runtime(value.y_axis),
      .z_axis = from_runtime(value.z_axis),
      .origin = from_runtime(value.origin),
  };
}

quader_editor_core::TransformAxis to_runtime(ModelingMirrorAxis axis) {
  switch (axis) {
  case ModelingMirrorAxis::X:
    return quader_editor_core::TransformAxis::X;
  case ModelingMirrorAxis::Y:
    return quader_editor_core::TransformAxis::Y;
  case ModelingMirrorAxis::Z:
    return quader_editor_core::TransformAxis::Z;
  }
  return quader_editor_core::TransformAxis::None;
}

quader_editor_core::TransformAxis to_runtime(ModelingTransformAxis axis) {
  switch (axis) {
  case ModelingTransformAxis::X:
    return quader_editor_core::TransformAxis::X;
  case ModelingTransformAxis::Y:
    return quader_editor_core::TransformAxis::Y;
  case ModelingTransformAxis::Z:
    return quader_editor_core::TransformAxis::Z;
  case ModelingTransformAxis::XY:
    return quader_editor_core::TransformAxis::Xy;
  case ModelingTransformAxis::XZ:
    return quader_editor_core::TransformAxis::Xz;
  case ModelingTransformAxis::YZ:
    return quader_editor_core::TransformAxis::Yz;
  case ModelingTransformAxis::All:
    return quader_editor_core::TransformAxis::All;
  case ModelingTransformAxis::None:
    return quader_editor_core::TransformAxis::None;
  }
  return quader_editor_core::TransformAxis::None;
}

quader_editor_core::ViewportInteractionTool
to_runtime(ModelingTransformTool tool) {
  switch (tool) {
  case ModelingTransformTool::Move:
    return quader_editor_core::ViewportInteractionTool::Move;
  case ModelingTransformTool::Rotate:
    return quader_editor_core::ViewportInteractionTool::Rotate;
  case ModelingTransformTool::Scale:
    return quader_editor_core::ViewportInteractionTool::Scale;
  }
  return quader_editor_core::ViewportInteractionTool::Unknown;
}

quader_editor_core::TransformDragOperation
to_runtime(ModelingTransformOperation operation) {
  switch (operation) {
  case ModelingTransformOperation::Transform:
    return quader_editor_core::TransformDragOperation::Transform;
  case ModelingTransformOperation::Extrude:
    return quader_editor_core::TransformDragOperation::Extrude;
  case ModelingTransformOperation::Inset:
    return quader_editor_core::TransformDragOperation::Inset;
  }
  return quader_editor_core::TransformDragOperation::Transform;
}

quader_editor_core::ViewportFlipOrientation
to_runtime(ModelingFlipOrientation orientation) {
  switch (orientation) {
  case ModelingFlipOrientation::Horizontal:
    return quader_editor_core::ViewportFlipOrientation::Horizontal;
  case ModelingFlipOrientation::Vertical:
    return quader_editor_core::ViewportFlipOrientation::Vertical;
  }
  return quader_editor_core::ViewportFlipOrientation::Horizontal;
}

ModelingHoverTarget
from_runtime(const quader_editor_core::SelectionHoverState &hover) {
  ModelingHoverTarget target;
  target.object_id = hover.object_id;
  switch (hover.kind) {
  case quader_editor_core::SelectionHoverKind::Object:
    target.kind = ModelingActiveSelectionKind::Object;
    break;
  case quader_editor_core::SelectionHoverKind::Vertex:
    target.kind = ModelingActiveSelectionKind::Vertex;
    target.vertex_id = static_cast<int>(hover.vertex_id);
    break;
  case quader_editor_core::SelectionHoverKind::Edge:
    target.kind = ModelingActiveSelectionKind::Edge;
    target.edge_a = static_cast<int>(hover.edge.a);
    target.edge_b = static_cast<int>(hover.edge.b);
    break;
  case quader_editor_core::SelectionHoverKind::Face:
    target.kind = ModelingActiveSelectionKind::Face;
    target.face_id = static_cast<int>(hover.face_id);
    break;
  case quader_editor_core::SelectionHoverKind::None:
    target.kind = ModelingActiveSelectionKind::None;
    target.object_id = -1;
    break;
  }
  return target;
}

ModelingHoverSummary
from_runtime_hover_summary(const quader_editor_core::EditorSession &session,
                           const quader_editor_core::SelectionHoverState &hover,
                           bool changed) {
  return {
      .changed = changed,
      .target = from_runtime(hover),
      .target_selected =
          quader_editor_core::selection_hover_target_matches_selection(
              session.objects(), session.selection_mode(), hover),
  };
}

ModelingBoxDragState from_runtime(quader_editor_core::BoxDragState state) {
  switch (state) {
  case quader_editor_core::BoxDragState::Idle:
    return ModelingBoxDragState::Idle;
  case quader_editor_core::BoxDragState::Footprint:
    return ModelingBoxDragState::Footprint;
  case quader_editor_core::BoxDragState::Height:
    return ModelingBoxDragState::Height;
  }
  return ModelingBoxDragState::Idle;
}

const quader_editor_core::PolygonObject *
find_object(const quader_editor_core::EditorSession &session,
            int modeling_object_id) {
  const auto &objects = session.objects();
  const auto it = std::find_if(objects.begin(), objects.end(),
                               [modeling_object_id](const auto &object) {
                                 return object.id == modeling_object_id;
                               });
  return it == objects.end() ? nullptr : &(*it);
}

quader_poly::PickOptions
pick_options_from_request(const ModelingHoverRequest &request) {
  return quader_editor_core::selection_pick_options_from_radii(
      request.vertex_radius, request.edge_radius);
}

ModelingBoxPreview
from_runtime(const quader_editor_core::BoxPreviewDescriptor &preview,
             const std::vector<quader::QVec3> &corners = {}) {
  ModelingBoxPreview result;
  result.ok = preview.ok;
  result.valid = preview.ok && preview.footprint.footprint.size() >= 4U &&
                 !preview.bounds.empty;
  result.drag_state = from_runtime(preview.drag_state);
  result.message = preview.message;
  result.normal = from_runtime(preview.normal);
  result.distance = preview.distance;
  result.bounds_empty = preview.bounds.empty;
  result.bounds = {
      .min = from_runtime(preview.bounds.min),
      .max = from_runtime(preview.bounds.max),
  };

  result.footprint_count = std::min<std::size_t>(
      result.footprint.size(), preview.footprint.footprint.size());
  for (std::size_t index = 0; index < result.footprint_count; ++index) {
    result.footprint[index] = from_runtime(preview.footprint.footprint[index]);
  }

  result.corner_count =
      std::min<std::size_t>(result.corners.size(), corners.size());
  for (std::size_t index = 0; index < result.corner_count; ++index) {
    result.corners[index] = from_runtime(corners[index]);
  }
  return result;
}

ModelingCommandDescriptor make_box_tool_descriptor() {
  const ToolDescriptor *tool =
      find_viewport_tool_descriptor(ModelingViewportTool::Box);
  return ModelingCommandDescriptor{
      .command = ModelingCommandId::ActivateBoxTool,
      .id = tool != nullptr ? std::string{tool->id} : std::string{},
      .label = tool != nullptr ? std::string{tool->label} : std::string{},
      .shortcut = tool != nullptr ? std::string{tool->shortcut} : std::string{},
      .viewport_scoped = tool != nullptr && tool->owns_viewport,
      .rebuilds_mesh_on_success = true,
  };
}

ModelingCommandDescriptor make_create_box_descriptor() {
  return ModelingCommandDescriptor{
      .command = ModelingCommandId::CreateBoxFromBounds,
      .id = "create_box_from_bounds",
      .label = "Create Box From Bounds",
      .shortcut = "B",
      .viewport_scoped = true,
      .rebuilds_mesh_on_success = true,
  };
}

} // namespace

/**
 * Stores the private runtime session state for a Modeling Session Memento.
 */
struct ModelingSessionMemento::Impl {
  Impl(quader_editor_core::SessionState snapshot,
       std::unordered_map<int, ModelingObjectSceneIdentity>
           scene_identity_snapshot)
      : state(std::move(snapshot)),
        scene_identities(std::move(scene_identity_snapshot)) {}

  quader_editor_core::SessionState state;
  std::unordered_map<int, ModelingObjectSceneIdentity> scene_identities;
};

ModelingSessionMemento::ModelingSessionMemento() = default;
ModelingSessionMemento::~ModelingSessionMemento() = default;

ModelingSessionMemento::ModelingSessionMemento(std::shared_ptr<const Impl> impl)
    : impl_(std::move(impl)) {}

bool ModelingSessionMemento::valid() const {
    return static_cast<bool>(impl_);
}

/**
 * Stores private runtime selection state for a modeling box-selection baseline.
 */
struct ModelingSelectionBoxBaseline::Impl {
    explicit Impl(quader_editor_core::RectangleSelectionState selection)
        : selection(std::move(selection)) {}

    quader_editor_core::RectangleSelectionState selection;
};

ModelingSelectionBoxBaseline::ModelingSelectionBoxBaseline() = default;
ModelingSelectionBoxBaseline::~ModelingSelectionBoxBaseline() = default;

ModelingSelectionBoxBaseline::ModelingSelectionBoxBaseline(std::shared_ptr<const Impl> impl)
    : impl_(std::move(impl)) {}

bool ModelingSelectionBoxBaseline::valid() const {
    return static_cast<bool>(impl_);
}

/**
 * Owns Impl behavior within the modeling application service layer.
 */
class ModelingSessionService::Impl : private ModelingTransformDragPort {
public:
  Impl(SceneDocument &scene, AssetRegistry &assets)
      : scene_(scene), assets_(assets), scene_sync_service_(object_registry_) {}

  [[nodiscard]] ModelingCommandDescriptor box_tool_command() const {
    return make_box_tool_descriptor();
  }

  [[nodiscard]] ModelingCommandDescriptor
  create_box_from_bounds_command() const {
    return make_create_box_descriptor();
  }

    [[nodiscard]] std::vector<ModelingObjectRecord> objects() const {
      return object_registry_.records(scene_);
    }

    [[nodiscard]] std::optional<EntityId>
    entity_for_modeling_object(int modeling_object_id) const {
      return object_registry_.entity_for_modeling_object(modeling_object_id);
    }

    [[nodiscard]] std::optional<int>
    modeling_object_for_entity(EntityId entity) const {
      return object_registry_.modeling_object_for_entity(entity);
    }

    [[nodiscard]] std::optional<MeshAsset>
    compile_object_mesh(int modeling_object_id) const {
      const quader_editor_core::PolygonObject *object =
          find_object(session_, modeling_object_id);
      if (object == nullptr) {
        return std::nullopt;
      }
      return mesh_sync_service_.compile_object_mesh(*object);
    }

    [[nodiscard]] std::optional<EntityId>
    pick_object_entity(const ModelingPickSelectRequest &request) const {
      const std::optional<int> object_id = selection_service_.pick_object_id(
          session_, scene_, object_registry_, request);
      if (!object_id.has_value()) {
        return std::nullopt;
      }
      return object_registry_.entity_for_modeling_object(*object_id);
    }

    [[nodiscard]] std::vector<EntityId>
    box_pick_object_entities(const ModelingBoxSelectRequest &request) const {
      const std::vector<int> object_ids =
          selection_service_.box_pick_object_ids(session_, scene_,
                                                 object_registry_, request);
      std::vector<EntityId> entities;
      entities.reserve(object_ids.size());
      for (const int object_id : object_ids) {
        const std::optional<EntityId> entity =
            object_registry_.entity_for_modeling_object(object_id);
        if (!entity.has_value() || std::find(entities.begin(), entities.end(),
                                             *entity) != entities.end()) {
          continue;
        }
        entities.push_back(*entity);
      }
      return entities;
    }

    [[nodiscard]] std::optional<QVec3> selection_center() const {
      return selection_projection_service_.world_selection_center(
          session_, scene_, assets_, object_registry_);
    }

    [[nodiscard]] ModelingSelectionMode selection_mode() const {
      return selection_service_.selection_mode(session_, app_selection_mode_);
    }

    [[nodiscard]] ModelingSelectionSummary selection_summary() const {
      return selection_service_.selection_summary(session_, scene_,
                                                  app_selection_mode_);
    }

    [[nodiscard]] ModelingSelectionBoxBaseline
    capture_box_selection_baseline() const {
      return ModelingSelectionBoxBaseline(
          std::make_shared<ModelingSelectionBoxBaseline::Impl>(
              selection_service_.rectangle_selection_state(session_)));
    }

    [[nodiscard]] std::uint64_t selection_overlay_signature(
        ModelingSelectionOverlayOptions options = {}) const {
      return overlay_service_.selection_overlay_signature(runtime_, options);
    }

    [[nodiscard]] ViewportOverlayFrame selection_overlay_frame(
        ModelingSelectionOverlayOptions options = {}) const {
      return overlay_service_.selection_overlay_frame(
          session_, runtime_, scene_, object_registry_,
          cached_scene_transformed_objects_for_picking(), options);
    }

    [[nodiscard]] std::vector<ModelingToolPreviewMesh>
    tool_preview_meshes() const {
      return overlay_service_.tool_preview_meshes(
          runtime_, scene_, object_registry_,
          cached_scene_transformed_objects_for_picking());
    }

    [[nodiscard]] static std::unordered_set<int> live_modeling_object_ids(
        const std::vector<quader_editor_core::PolygonObject> &objects) {
      std::unordered_set<int> ids;
      ids.reserve(objects.size());
      for (const quader_editor_core::PolygonObject &object : objects) {
        ids.insert(object.id);
      }
      return ids;
    }

    [[nodiscard]] ModelingSessionMemento capture_history_memento_for_state(
        const quader_editor_core::SessionState &state) const {
      const std::unordered_set<int> live_ids =
          live_modeling_object_ids(state.objects);
      return ModelingSessionMemento(
          std::make_shared<ModelingSessionMemento::Impl>(
              state,
              object_registry_.capture_scene_identities_for_modeling_objects(
                  scene_, live_ids)));
    }

    void refresh_live_scene_identities() {
      for (const quader_editor_core::PolygonObject &object :
           session_.objects()) {
        const std::optional<EntityId> entity =
            object_registry_.entity_for_modeling_object(object.id);
        if (entity.has_value() && scene_.exists(*entity)) {
          object_registry_.remember_scene_entity(object.id, scene_, *entity);
        }
      }
    }

    [[nodiscard]] ModelingSessionMemento capture_history_memento() const {
      return capture_history_memento_for_state(session_.state());
    }

    [[nodiscard]] ModelingSessionMemento
    capture_active_preview_history_memento() const {
      const std::optional<quader_editor_core::SessionState> state =
          session_.preview_transaction_history_state();
      return state.has_value() ? capture_history_memento_for_state(*state)
                               : ModelingSessionMemento();
    }

    ModelingCommandResult
    restore_history_memento(const ModelingSessionMemento &memento) {
      if (!memento.valid()) {
        return {
            .ok = false,
            .changed = false,
            .message = "Modeling history restore point is invalid.",
        };
      }

      quader_editor_core::SessionState state = memento.impl_->state;
      const quader_editor_core::SessionState &current = session_.state();
      state.content_revision =
          std::max(state.content_revision, current.content_revision + 1U);
      state.selection_revision =
          std::max(state.selection_revision, current.selection_revision + 1U);
      object_registry_.restore_scene_identities(
          memento.impl_->scene_identities);
      session_.set_state(std::move(state));
      session_.clear_history();
      sync_scene_objects_from_modeling(true);
      object_registry_.restore_scene_identities(
          memento.impl_->scene_identities);
      refresh_live_scene_identities();
      sync_scene_selection_from_modeling();
      invalidate_selection_overlay_caches();
      return {
          .ok = true,
          .changed = true,
      };
    }

    void reset_from_scene() {
      object_registry_.clear();
      quader_editor_core::SessionState state;
      state.selection_mode = quader_editor_core::SelectionMode::Object;
      const EntityId active_entity = scene_.primary_selection();

      for (const EntityId entity : scene_.entity_tree_order()) {
        const ModelingObjectComponent *modeling_object =
            scene_.modeling_object(entity);
        const MeshComponent *mesh_component = scene_.mesh(entity);
        if (modeling_object == nullptr ||
            modeling_object->object_id == kInvalidModelingObjectId ||
            mesh_component == nullptr ||
            mesh_component->mesh == kInvalidAssetId) {
          continue;
        }

        const MeshAsset *mesh = assets_.mesh(mesh_component->mesh);
        if (mesh == nullptr || !mesh->has_authored_polygon_document ||
            !mesh->authored_polygon_document) {
          continue;
        }

        quader_editor_core::PolygonObject object;
        object.id = modeling_object->object_id;
        object.material_id = static_cast<int>(modeling_object->material_slot);
        object.selected = scene_.selection(entity) != nullptr &&
                          scene_.selection(entity)->selected;
        object.document = *mesh->authored_polygon_document;
        state.objects.push_back(std::move(object));
        object_registry_.bind(modeling_object->object_id, entity);
        object_registry_.remember_scene_entity(modeling_object->object_id,
                                               scene_, entity);
        if (entity == active_entity) {
          state.active_object_id = modeling_object->object_id;
        }
      }

      state.next_object_id =
          quader_editor_core::next_polygon_object_id(state.objects, 1);
      state.content_revision = state.objects.empty() ? 0U : 1U;
      state.selection_revision = 0U;
      session_.set_state(std::move(state));
      session_.mark_clean();
      session_.clear_history();
      runtime_.reset_document_state(session_);
      scene_sync_service_.mark_scene_objects_synced(
          session_.state().content_revision);
      (void)selection_projection_service_.project_object_selection_from_scene(
          session_, scene_, object_registry_, true);
      session_.mark_clean();
      invalidate_selection_overlay_caches();
    }

    void sync_object_selection_from_scene(bool force_object_mode = false) {
      QDR_PROFILE_FUNCTION();
      if (!selection_projection_service_
               .should_project_object_selection_from_scene(session_,
                                                           force_object_mode)) {
        return;
      }
      if (!scene_objects_synced_for_current_modeling_content()) {
        sync_scene_objects_from_modeling();
      }

      selection_projection_service_.project_object_selection_from_scene(
          session_, scene_, object_registry_, force_object_mode);
    }

    void promote_drawn_plane_tool_object_selection() {
      QDR_PROFILE_FUNCTION();
      if (!scene_objects_synced_for_current_modeling_content()) {
        sync_scene_objects_from_modeling();
      }

      if (selection_projection_service_.promote_object_selection_for_tool(
              session_, scene_, object_registry_)) {
        sync_scene_selection_from_modeling();
        invalidate_selection_overlay_caches();
      }
    }

    void sync_scene_selection_from_modeling() {
      QDR_PROFILE_FUNCTION();
      if (!scene_objects_synced_for_current_modeling_content()) {
        sync_scene_objects_from_modeling();
      }
      selection_projection_service_.project_scene_selection_from_modeling(
          session_, scene_, object_registry_);
    }

    void restore_scene_selection(const std::vector<EntityId> &selected,
                                 EntityId active) {
      selection_projection_service_.restore_scene_selection(scene_, selected,
                                                            active);
    }

    [[nodiscard]] std::vector<ModelingObjectSceneIdentity>
    capture_selected_duplicate_scene_identities() const {
      QDR_PROFILE_FUNCTION();
      const std::vector<int> selected_object_ids =
          session_.selected_object_ids();
      return object_registry_.capture_scene_identities_for_modeling_objects(
          scene_, std::span<const int>(selected_object_ids.data(),
                                       selected_object_ids.size()));
    }

    void apply_scene_identities_to_selected_duplicates(
        const std::vector<ModelingObjectSceneIdentity> &source_identities) {
      QDR_PROFILE_FUNCTION();
      if (source_identities.empty()) {
        return;
      }

      const std::vector<int> selected_duplicate_object_ids =
          session_.selected_object_ids();
      if (selected_duplicate_object_ids.size() != source_identities.size()) {
        return;
      }
      const std::unordered_set<int> duplicate_object_set(
          selected_duplicate_object_ids.begin(),
          selected_duplicate_object_ids.end());
      std::vector<int> duplicate_object_ids;
      duplicate_object_ids.reserve(selected_duplicate_object_ids.size());
      for (const quader_editor_core::PolygonObject &object :
           session_.objects()) {
        if (duplicate_object_set.contains(object.id)) {
          duplicate_object_ids.push_back(object.id);
        }
      }
      if (duplicate_object_ids.size() != source_identities.size()) {
        return;
      }

      for (std::size_t index = 0; index < source_identities.size(); ++index) {
        const std::optional<EntityId> duplicate =
            object_registry_.entity_for_modeling_object(
                duplicate_object_ids[index]);
        if (!duplicate.has_value() || !scene_.exists(*duplicate)) {
          continue;
        }

        mesh_sync_service_.apply_scene_identity_to_duplicate_entity(
            scene_, *duplicate, source_identities[index]);
        object_registry_.remember_scene_entity(duplicate_object_ids[index],
                                               scene_, *duplicate);
      }
    }

    ModelingCommandResult
    duplicate_selected_hierarchy(DocumentCommandService &document_commands,
                                 ModelingSelectionMode result_selection_mode) {
      return hierarchy_duplicate_service_.duplicate_selected_entity_hierarchy(
          session_, scene_, object_registry_, document_commands,
          ModelingHierarchyDuplicateRuntime{
              .ensure_scene_objects_synced =
                  [&]() {
                    if (!scene_objects_synced_for_current_modeling_content()) {
                      sync_scene_objects_from_modeling();
                    }
                  },
              .set_selection_mode =
                  [&](ModelingSelectionMode mode) {
                    return set_selection_mode(mode);
                  },
              .project_object_selection_from_scene =
                  [&]() { sync_object_selection_from_scene(); },
              .duplicate_selected_meshes =
                  [&]() {
                    return execute_instant_operation(
                        ModelingInstantOperationId::DuplicateMeshes, false);
                  },
          },
          result_selection_mode);
    }

    ModelingCommandResult duplicate_selected_hierarchy_for_group_drag(
        DocumentCommandService &document_commands) {
      return duplicate_selected_hierarchy(document_commands,
                                          ModelingSelectionMode::Group);
    }

    ModelingCommandResult duplicate_selected_entity_hierarchy(
        DocumentCommandService &document_commands) {
      return duplicate_selected_hierarchy(document_commands,
                                          ModelingSelectionMode::Object);
    }

    ModelingCommandResult set_selection_mode(ModelingSelectionMode mode) {
      if (mode == ModelingSelectionMode::Group) {
        const ModelingCommandResult result =
            selection_service_.set_group_selection_mode(session_,
                                                        app_selection_mode_);
        if (result.changed) {
          invalidate_selection_overlay_caches();
        }
        return result;
      }

      app_selection_mode_ = mode;
      std::vector<EntityId> scene_only_selection;
      scene_only_selection.reserve(scene_.selected_entities().size());
      for (const EntityId entity : scene_.selected_entities()) {
        if (scene_.modeling_object(entity) == nullptr) {
          scene_only_selection.push_back(entity);
        }
      }
      const EntityId active_scene_only_selection =
          scene_.modeling_object(scene_.primary_selection()) == nullptr
              ? scene_.primary_selection()
              : kInvalidEntityId;
      if (!scene_only_selection.empty()) {
        sync_object_selection_from_scene();
      }

      const ModelingCommandResult result =
          selection_service_.set_runtime_selection_mode(session_, runtime_,
                                                        mode);
      invalidate_selection_overlay_caches();
      if (mode == ModelingSelectionMode::Object &&
          !scene_only_selection.empty()) {
        sync_object_selection_from_scene();
      }
      sync_scene_selection_from_modeling();
      selection_projection_service_.add_scene_selection(
          scene_, scene_only_selection, active_scene_only_selection);
      return result;
    }

    ModelingCommandResult select_all() {
      return execute_runtime_no_settings(
          quader_editor_core::EditorRuntimeCommandId::SelectAll, false);
    }

    ModelingCommandResult invert_selection() {
      return execute_runtime_no_settings(
          quader_editor_core::EditorRuntimeCommandId::InvertSelection, false);
    }

    ModelingCommandResult clear_selection() {
      return execute_runtime_no_settings(
          quader_editor_core::EditorRuntimeCommandId::ClearSelection, false);
    }

    ModelingCommandResult delete_component_selection() {
      const ModelingSelectionSummary summary = selection_summary();
      if (scene_aware_delete_uses_document_route(summary.mode)) {
        return {
            .ok = false,
            .changed = false,
            .message =
                "Delete selection must use the scene-aware command route.",
        };
      }
      return execute_runtime_no_settings(
          quader_editor_core::EditorRuntimeCommandId::DeleteSelection, true);
    }

    [[nodiscard]] bool has_selected_scene_aware_delete_root() const {
      return scene_aware_delete_has_selected_root(scene_);
    }

    std::unique_ptr<IUndoableCommand> create_delete_selection_command() {
      return create_scene_aware_delete_selection_command(
          scene_,
          SceneAwareDeleteRuntime{
              .capture_modeling_memento =
                  [&]() { return capture_history_memento(); },
              .restore_modeling_memento =
                  [&](const ModelingSessionMemento &memento) {
                    return restore_history_memento(memento);
                  },
              .app_selection_mode = [&]() { return app_selection_mode_; },
              .set_app_selection_mode =
                  [&](ModelingSelectionMode mode) {
                    app_selection_mode_ = mode;
                  },
              .delete_modeling_objects =
                  [&](const std::vector<int> &modeling_object_ids) {
                    if (modeling_object_ids.empty()) {
                      return ModelingCommandResult{.ok = true,
                                                   .changed = false};
                    }
                    session_.set_selection_mode(
                        quader_editor_core::SelectionMode::Object);
                    session_.set_selected_object_ids(
                        modeling_object_ids, modeling_object_ids.front());
                    return execute_runtime_command(
                        RuntimeToolCommandService::no_settings(
                            quader_editor_core::EditorRuntimeCommandId::
                                DeleteSelection),
                        true);
                  },
              .entity_for_modeling_object =
                  [&](int modeling_object_id) {
                    return object_registry_.entity_for_modeling_object(
                        modeling_object_id);
                  },
              .sync_object_selection_from_scene =
                  [&](bool force_object_mode) {
                    sync_object_selection_from_scene(force_object_mode);
                  },
              .clear_runtime_object_selection_for_group_mode =
                  [&]() {
                    session_.set_selection_mode(
                        quader_editor_core::SelectionMode::Object);
                    session_.set_selected_object_ids({}, -1);
                  },
              .invalidate_selection_overlay_caches =
                  [&]() { invalidate_selection_overlay_caches(); },
          });
    }

    ModelingCommandResult copy_selection() {
      return execute_runtime_command(clipboard_service_.make_copy_command(),
                                     false);
    }

    ModelingCommandResult paste_selection() {
      const quader_editor_core::EditorRuntimeCommand command =
          clipboard_service_.make_paste_command();
      const std::uint64_t content_before = session_.state().content_revision;
      const quader_editor_core::EditorRuntimeCommandResult result =
          quader_editor_core::execute_runtime_command(session_, runtime_,
                                                      command);
      if (session_.state().content_revision != content_before) {
        sync_scene_objects_from_modeling();
      }
      sync_scene_selection_from_modeling();
      return {
          .ok = result.command.ok,
          .changed = result.command.changed,
          .message = result.command.message,
      };
    }

    ModelingCommandResult repeat_last_action() {
      const std::uint64_t content_before = session_.state().content_revision;
      const quader_editor_core::CommandResult result =
          history_bridge_.execute_repeat_last_action(session_);
      if (session_.state().content_revision != content_before) {
        sync_scene_objects_from_modeling();
      }
      sync_scene_selection_from_modeling();
      return {
          .ok = result.ok,
          .changed = result.changed,
          .message = result.message,
      };
    }

    ModelingCommandResult mirror_selection(ModelingMirrorAxis axis) {
      quader_editor_core::MirrorAxisRuntimeCommandSettings settings;
      settings.axis = to_runtime(axis);

      quader_editor_core::EditorRuntimeCommand command;
      command.command_id =
          quader_editor_core::EditorRuntimeCommandId::MirrorAxis;
      command.settings = settings;

      const std::uint64_t content_before = session_.state().content_revision;
      const quader_editor_core::EditorRuntimeCommandResult result =
          quader_editor_core::execute_runtime_command(session_, runtime_,
                                                      command);
      if (session_.state().content_revision != content_before) {
        sync_scene_objects_from_modeling();
      }
      sync_scene_selection_from_modeling();
      return {
          .ok = result.command.ok,
          .changed = result.command.changed,
          .message = result.command.message,
      };
    }

    ModelingCommandResult flip_selection(ModelingFlipOrientation orientation) {
      quader_editor_core::ViewFlipRuntimeCommandSettings settings;
      settings.orientation = to_runtime(orientation);

      quader_editor_core::EditorRuntimeCommand command;
      command.command_id = quader_editor_core::EditorRuntimeCommandId::FlipView;
      command.settings = settings;

      const std::uint64_t content_before = session_.state().content_revision;
      const quader_editor_core::EditorRuntimeCommandResult result =
          quader_editor_core::execute_runtime_command(session_, runtime_,
                                                      command);
      if (session_.state().content_revision != content_before) {
        sync_scene_objects_from_modeling();
      }
      sync_scene_selection_from_modeling();
      return {
          .ok = result.command.ok,
          .changed = result.command.changed,
          .message = result.command.message,
      };
    }

    ModelingCommandResult apply_active_material(std::uint32_t material_slot) {
      quader_editor_core::AssignMaterialOperationSettings settings;
      settings.material_slot = material_slot;

      quader_editor_core::EditorOperationCommand command;
      command.operation_id =
          quader_editor_core::EditorOperationId::AssignMaterial;
      command.phase = quader_editor_core::EditorOperationCommandPhase::Execute;
      command.settings = settings;

      const std::uint64_t content_before = session_.state().content_revision;
      const quader_editor_core::EditorOperationCommandResult result =
          quader_editor_core::execute_command(session_, command);
      if (session_.state().content_revision != content_before) {
        sync_scene_objects_from_modeling();
      }
      sync_scene_selection_from_modeling();
      history_bridge_.record_operation(command, result.command);
      return {
          .ok = result.command.ok,
          .changed = result.command.changed,
          .message = result.command.message,
      };
    }

    [[nodiscard]] ModelingToolStatus tool_status() const {
      return overlay_service_.tool_status(runtime_, modal_tool_owns_viewport());
    }

    [[nodiscard]] ModelingToolSettingsTarget tool_settings_target() const {
      const ModelingToolSettingsTarget preview_target =
          tool_settings_service_.active_preview_target(
              session_.preview_transaction_status());
      if (preview_target.active) {
        return preview_target;
      }
      return tool_settings_service_.active_runtime_tool_target(
          runtime_.status());
    }

    [[nodiscard]] ModelingInteractionSettings interaction_settings() const {
      return interaction_settings_service_.settings();
    }

    ModelingCommandResult
    set_interaction_settings(const ModelingInteractionSettings &settings) {
      const ModelingToolSettingsTarget active_target = tool_settings_target();
      const ModelingInteractionSettings previous =
          interaction_settings_service_.settings();
      const ModelingInteractionSettings normalized =
          interaction_settings_service_.normalize(settings);
      const bool cut_mode_changed = normalized.cut_mode != previous.cut_mode;
      const bool changed =
          interaction_settings_service_.set_normalized(normalized);
      if (cut_mode_changed) {
        return set_cut_mode(normalized.cut_mode);
      }
      if (changed && active_target.operation_preview_active) {
        ModelingCommandResult result =
            update_active_operation_preview_settings();
        result.changed = result.changed || changed;
        return result;
      }
      return {.ok = true, .changed = changed};
    }

    [[nodiscard]] bool modal_tool_owns_viewport() const {
      return modal_tool_runtime_.owns_viewport(
          from_runtime_viewport_tool(runtime_.active_tool()));
    }

    [[nodiscard]] bool can_cancel_active_tool_or_preview() const {
      return tool_settings_target().operation_preview_active ||
             runtime_.transform_drag_active() || has_transform_preview() ||
             transform_drag_service_.scene_preview_active() ||
             modal_tool_owns_viewport();
    }

    [[nodiscard]] bool can_cancel_active_preview_for_undo() const {
      return tool_settings_target().operation_preview_active ||
             runtime_.transform_drag_active() || has_transform_preview() ||
             transform_drag_service_.scene_preview_active();
    }

    [[nodiscard]] ModelingActionQuery
    effective_action_query(ModelingActionQuery query) const {
      return context_action_service_.effective_action_query(
          query, modal_tool_owns_viewport());
    }

    [[nodiscard]] ModelingActionSet
    context_action_set(ModelingActionQuery query) const {
      return context_action_service_.action_set(selection_summary(), query,
                                                modal_tool_owns_viewport());
    }

    [[nodiscard]] std::vector<ModelingActionItem>
    context_actions(ModelingActionQuery query) const {
      return context_action_service_.actions(selection_summary(), query,
                                             modal_tool_owns_viewport());
    }

    [[nodiscard]] std::optional<ModelingActionItem>
    context_action(std::string_view action_id,
                   ModelingActionQuery query) const {
      return context_action_service_.action(selection_summary(), action_id,
                                            query, modal_tool_owns_viewport());
    }

    void
    reset_settings_for_preview_action(ModelingActionPreviewKind preview_kind) {
      interaction_settings_service_.reset_for_preview_action(preview_kind);
    }

    void clear_inactive_operation_preview_metadata() {
      if (!session_.preview_transaction_status().active) {
        tool_settings_service_.clear_preview_metadata();
      }
    }

    ModelingCommandResult execute_runtime_operation_command(
        quader_editor_core::EditorOperationId runtime_operation,
        quader_editor_core::EditorOperationCommandPhase phase,
        quader_editor_core::EditorOperationSettings settings) {
      RuntimeOperationExecutor::OperationCommand operation_command;
      operation_command.operation = runtime_operation;
      operation_command.phase = phase;
      operation_command.settings = std::move(settings);
      const quader_editor_core::EditorOperationCommand command =
          RuntimeOperationExecutor::make_command(operation_command);

      const quader_editor_core::PreviewTransactionStatus preview_before =
          session_.preview_transaction_status();
      const std::uint64_t content_before = session_.state().content_revision;
      const quader_editor_core::EditorOperationCommandResult result =
          runtime_operation_executor_.execute(session_, operation_command);

      history_bridge_.record_operation(command, result.command);

      ModelingCommandExecutor::SyncPlan sync_plan =
          command_executor_.operation_sync_plan(
              phase, preview_before, result, content_before,
              session_.state().content_revision);
      if (runtime_operation ==
              quader_editor_core::EditorOperationId::DuplicateMeshes &&
          phase == quader_editor_core::EditorOperationCommandPhase::Execute) {
        sync_plan.force_mesh_sync = false;
      }
      if (sync_plan.invalidate_overlay) {
        invalidate_selection_overlay_caches();
      }
      if (sync_plan.sync_meshes) {
        sync_scene_objects_from_modeling(sync_plan.force_mesh_sync);
      }
      if (sync_plan.sync_selection) {
        sync_scene_selection_from_modeling();
      }
      clear_inactive_operation_preview_metadata();
      return command_executor_.from_runtime(result.command);
    }

    ModelingCommandResult begin_operation_preview(
        const ModelingActionItem &action,
        const quader_editor_core::EditorOperation &operation) {
      if (!operation.supports_preview()) {
        return {.ok = false,
                .message = "Action does not support a live settings preview."};
      }

      reset_settings_for_preview_action(action.preview_kind);
      ModelingCommandResult result = execute_runtime_operation_command(
          operation.operation_id(),
          quader_editor_core::EditorOperationCommandPhase::BeginPreview,
          settings_for_operation(operation.operation_id(),
                                 interaction_settings_service_.settings()));
      if (result.ok) {
        tool_settings_service_.set_preview_metadata(
            action.action_id, action.operation_id,
            action.label.empty()
                ? fallback_tool_settings_label(
                      tool_settings_target_kind_for_preview(
                          session_.preview_transaction_status().kind))
                : action.label);
      }
      return result;
    }

    ModelingCommandResult update_active_operation_preview_settings() {
      const quader_editor_core::PreviewTransactionStatus preview =
          session_.preview_transaction_status();
      if (!preview.active) {
        return {.ok = false, .message = "No active operation preview."};
      }
      const std::optional<quader_editor_core::EditorOperationId> operation =
          operation_for_preview_kind(preview.kind);
      if (!operation.has_value()) {
        return {
            .ok = false,
            .message =
                "Active preview does not have editable operation settings."};
      }
      return execute_runtime_operation_command(
          *operation,
          quader_editor_core::EditorOperationCommandPhase::UpdatePreview,
          settings_for_operation(*operation,
                                 interaction_settings_service_.settings()));
    }

    ModelingCommandResult finish_active_operation_preview(bool commit) {
      const quader_editor_core::PreviewTransactionStatus preview =
          session_.preview_transaction_status();
      if (!preview.active) {
        return {.ok = false, .message = "No active operation preview."};
      }
      const std::optional<quader_editor_core::EditorOperationId> operation =
          operation_for_preview_kind(preview.kind);
      if (!operation.has_value()) {
        return {.ok = false,
                .message =
                    "Active preview does not belong to a settings operation."};
      }
      return execute_runtime_operation_command(
          *operation,
          commit
              ? quader_editor_core::EditorOperationCommandPhase::CommitPreview
              : quader_editor_core::EditorOperationCommandPhase::CancelPreview,
          settings_for_operation(*operation,
                                 interaction_settings_service_.settings()));
    }

    [[nodiscard]] std::optional<quader_editor_core::PreviewTransactionKind>
    editable_operation_preview_kind() const {
      const quader_editor_core::PreviewTransactionStatus preview =
          session_.preview_transaction_status();
      if (!preview.active ||
          !operation_for_preview_kind(preview.kind).has_value()) {
        return std::nullopt;
      }
      if (preview.kind == quader_editor_core::PreviewTransactionKind::Thicken &&
          session_.selection_mode() ==
              quader_editor_core::SelectionMode::Object) {
        return std::nullopt;
      }
      return preview.kind;
    }

    [[nodiscard]] bool
    restore_active_operation_preview_base_for_selection_edit() {
      const std::optional<quader_editor_core::PreviewTransactionKind>
          preview_kind = editable_operation_preview_kind();
      if (!preview_kind.has_value()) {
        return false;
      }
      const bool restored =
          session_.restore_preview_transaction_base_for_edit(*preview_kind);
      if (restored) {
        selection_service_.invalidate_picking_cache();
      }
      return restored;
    }

    void refresh_active_operation_preview_after_selection_edit(
        bool restored_preview_base, bool selection_changed) {
      if (!restored_preview_base) {
        return;
      }
      const std::optional<quader_editor_core::PreviewTransactionKind>
          preview_kind = editable_operation_preview_kind();
      if (!preview_kind.has_value()) {
        sync_scene_selection_from_modeling();
        return;
      }
      if (selection_changed) {
        (void)session_.capture_preview_transaction_base_after_edit(
            *preview_kind);
      }
      (void)update_active_operation_preview_settings();
    }

    ModelingCommandResult execute_context_action(std::string_view action_id) {
      if (action_id == std::string_view("delete_selection")) {
        return {.ok = false,
                .message = "Delete selection must use the command route."};
      }

      const ModelingContextActionResolution action =
          context_action_service_.resolve_executable_action(
              selection_summary(), action_id, modal_tool_owns_viewport());
      if (!action.ok) {
        return {.ok = false, .message = action.message};
      }
      for (const ModelingInstantOperationCatalogEntry &entry :
           modeling_instant_operation_catalog()) {
        if (entry.id == action.action.action_id &&
            (action.action.operation_id.empty() ||
             entry.operation_id == action.action.operation_id) &&
            action.action.preview_kind == ModelingActionPreviewKind::None) {
          return execute_instant_operation(entry.operation);
        }
      }
      if (action.action.kind != ModelingActionKind::Operation ||
          action.action.operation_id.empty()) {
        return {.ok = false,
                .message = "Action is not an executable modeling operation."};
      }

      const quader_editor_core::EditorOperation *operation =
          quader_editor_core::editor_operation_for_id(
              action.action.operation_id);
      if (operation == nullptr) {
        return {.ok = false,
                .message = "Action references an unknown modeling operation."};
      }

      if (action.action.preview_kind != ModelingActionPreviewKind::None) {
        return begin_operation_preview(action.action, *operation);
      }

      return execute_runtime_operation(
          operation->operation_id(),
          settings_for_operation(operation->operation_id(),
                                 interaction_settings_service_.settings()));
    }

    ModelingCommandResult commit_active_operation_preview() {
      return finish_active_operation_preview(true);
    }

    ModelingCommandResult cancel_active_operation_preview() {
      return finish_active_operation_preview(false);
    }

    [[nodiscard]] std::vector<ModelingInstantOperationDescriptor>
    instant_operations() const {
      const ModelingSelectionSummary summary = selection_summary();
      std::vector<ModelingInstantOperationDescriptor> operations;
      for (const ModelingInstantOperationCatalogEntry &entry :
           modeling_instant_operation_catalog()) {
        operations.push_back({
            .operation = entry.operation,
            .id = std::string(entry.id),
            .label = std::string(entry.live_label),
            .enabled = modeling_instant_operation_enabled(entry, summary),
            .rebuilds_mesh_on_success = entry.rebuilds_mesh_on_success,
        });
      }
      return operations;
    }

    ModelingCommandResult execute_runtime_operation(
        quader_editor_core::EditorOperationId runtime_operation,
        quader_editor_core::EditorOperationSettings settings) {
      return execute_runtime_operation_command(
          runtime_operation,
          quader_editor_core::EditorOperationCommandPhase::Execute,
          std::move(settings));
    }

    ModelingCommandResult
    execute_instant_operation(ModelingInstantOperationId operation,
                              bool preserve_duplicate_scene_identity = true) {
      const std::vector<ModelingObjectSceneIdentity>
          duplicate_scene_identities =
              operation == ModelingInstantOperationId::DuplicateMeshes &&
                      preserve_duplicate_scene_identity
                  ? capture_selected_duplicate_scene_identities()
                  : std::vector<ModelingObjectSceneIdentity>{};

      const std::optional<RuntimeOperationExecutor::OperationCommand> command =
          runtime_operation_executor_.operation_for_instant_command(
              operation, interaction_settings_service_.settings());
      if (!command.has_value()) {
        return {.ok = false,
                .message =
                    "Instant operation is not mapped to a runtime operation."};
      }

      ModelingCommandResult result =
          execute_runtime_operation(command->operation, command->settings);
      if (result.ok && result.changed &&
          operation == ModelingInstantOperationId::DuplicateMeshes) {
        apply_scene_identities_to_selected_duplicates(
            duplicate_scene_identities);
      }
      return result;
    }

    ModelingCommandResult begin_transform_preview(bool duplicate_selected) {
      const std::vector<ModelingObjectSceneIdentity>
          duplicate_scene_identities =
              duplicate_selected ? capture_selected_duplicate_scene_identities()
                                 : std::vector<ModelingObjectSceneIdentity>{};
      const quader_editor_core::CommandResult result =
          session_.begin_transform_preview(duplicate_selected);
      if (result.ok) {
        invalidate_selection_overlay_caches();
      }
      if (result.changed) {
        sync_scene_objects_from_modeling(true);
        sync_scene_selection_from_modeling();
        if (duplicate_selected) {
          apply_scene_identities_to_selected_duplicates(
              duplicate_scene_identities);
        }
      }
      return {
          .ok = result.ok,
          .changed = result.changed,
          .message = result.message,
      };
    }

    ModelingCommandResult update_transform_translate_preview(QVec3 delta) {
      return update_transform_preview_result(
          session_.update_transform_translate_preview(to_runtime(delta)));
    }

    ModelingCommandResult
    update_transform_matrix_preview(const ModelingTransform3 &transform,
                                    QVec3 pivot) {
      return update_transform_preview_result(
          session_.update_transform_matrix_preview(to_runtime(transform),
                                                   to_runtime(pivot)));
    }

    ModelingCommandResult
    update_transform_extrude_preview(QVec3 offset,
                                     float closed_edge_ledge_size) {
      return update_transform_preview_result(
          session_.update_transform_extrude_preview(to_runtime(offset),
                                                    closed_edge_ledge_size));
    }

    ModelingCommandResult
    update_transform_inset_preview(const ModelingTransform3 &transform,
                                   QVec3 pivot) {
      return update_transform_preview_result(
          session_.update_transform_inset_preview(to_runtime(transform),
                                                  to_runtime(pivot)));
    }

    ModelingCommandResult commit_transform_preview() {
      const quader_editor_core::PreviewTransactionFinishResult finish =
          session_.commit_preview_transaction(
              quader_editor_core::PreviewTransactionKind::Transform);
      if (finish.before.active) {
        invalidate_selection_overlay_caches();
      }
      sync_scene_objects_from_modeling(finish.before.active);
      sync_scene_selection_from_modeling();
      return transform_preview_service_.finish_result(finish, false);
    }

    ModelingCommandResult cancel_transform_preview() {
      const quader_editor_core::PreviewTransactionFinishResult finish =
          session_.cancel_preview_transaction(
              quader_editor_core::PreviewTransactionKind::Transform);
      if (finish.before.active) {
        invalidate_selection_overlay_caches();
      }
      sync_scene_objects_from_modeling(finish.before.active);
      sync_scene_selection_from_modeling();
      return transform_preview_service_.finish_result(finish, true);
    }

    ModelingCommandResult
    record_repeatable_transform(const ModelingRepeatTransform &transform) {
      if (!transform.active) {
        return {.ok = false, .message = "No active transform to record."};
      }

      quader_editor_core::TransformDragState drag;
      drag.active = true;
      drag.tool = to_runtime(transform.tool);
      drag.operation = to_runtime(transform.operation);
      drag.axis = to_runtime(transform.axis);
      drag.move.active = transform.tool == ModelingTransformTool::Move;
      drag.move.axis = drag.axis;
      drag.move.preview_delta = to_runtime(transform.move_delta);
      drag.rotate.active = transform.tool == ModelingTransformTool::Rotate;
      drag.rotate.axis = drag.axis;
      drag.rotate.preview_angle_degrees = transform.rotate_angle_degrees;
      drag.scale.active = transform.tool == ModelingTransformTool::Scale;
      drag.scale.axis = drag.axis;
      drag.scale.preview_scale = to_runtime(transform.scale);

      history_bridge_.record_transform(session_, drag,
                                       transform.duplicate_selected);
      return {.ok = true};
    }

    ModelingTransformDragPreview
    begin_transform_drag(const ModelingTransformDragBeginRequest &request) {
      return transform_drag_service_.begin_transform_drag(
          transform_drag_context(), request);
    }

    ModelingTransformDragPreview
    update_transform_drag(const ModelingTransformDragUpdateRequest &request) {
      return transform_drag_service_.update_transform_drag(
          transform_drag_context(), request);
    }

    ModelingTransformDragPreview
    commit_transform_drag(EditorHistoryService *history = nullptr) {
      return transform_drag_service_.commit_transform_drag(
          transform_drag_context(), history);
    }

    ModelingCommandResult cancel_transform_drag() {
      return transform_drag_service_.cancel_transform_drag(
          transform_drag_context());
    }

    ModelingSelectionSummary
    pick_select(const ModelingPickSelectRequest &request) {
      const bool restored_preview_base =
          restore_active_operation_preview_base_for_selection_edit();
      ModelingPickSelectRequest effective_request = request;
      if (restored_preview_base) {
        effective_request.preserve_selection_on_miss = true;
      }
      const ModelingSelectionApplyResult selection =
          selection_service_.apply_pick_selection(
          session_, scene_, object_registry_, effective_request);
      if (restored_preview_base) {
        refresh_active_operation_preview_after_selection_edit(
            restored_preview_base, selection.changed);
        return selection_summary();
      }
      if (selection.applied) {
        sync_scene_selection_from_modeling();
      }
      return selection_summary();
    }

    ModelingSelectionSummary
    box_select(const ModelingBoxSelectRequest &request) {
      const bool restored_preview_base =
          restore_active_operation_preview_base_for_selection_edit();
      return box_select_from_baseline(
          request, selection_service_.rectangle_selection_state(session_),
          restored_preview_base);
    }

    ModelingSelectionSummary box_select(
        const ModelingBoxSelectRequest &request,
        const quader_editor_core::RectangleSelectionState &baseline_selection) {
      const bool restored_preview_base =
          restore_active_operation_preview_base_for_selection_edit();
      const quader_editor_core::RectangleSelectionState effective_baseline =
          restored_preview_base
              ? selection_service_.rectangle_selection_state(session_)
              : baseline_selection;
      return box_select_from_baseline(request, effective_baseline,
                                      restored_preview_base);
    }

    ModelingSelectionSummary box_select_from_baseline(
        const ModelingBoxSelectRequest &request,
        const quader_editor_core::RectangleSelectionState &baseline_selection,
        bool restored_preview_base) {
      const ModelingSelectionApplyResult selection =
          selection_service_.apply_rectangle_selection(
              session_, scene_, object_registry_, request, baseline_selection);
      if (restored_preview_base) {
        refresh_active_operation_preview_after_selection_edit(
            restored_preview_base, selection.changed);
        return selection_summary();
      }
      sync_scene_selection_from_modeling();
      invalidate_selection_overlay_caches();
      return selection_summary();
    }

    ModelingCommandResult
    update_selection_hover(const ModelingHoverRequest &request) {
      const std::vector<quader_editor_core::PolygonObject> &picking_objects =
          cached_scene_transformed_objects_for_picking();
      const quader_poly::PickOptions pick_options =
          pick_options_from_request(request);
      const quader_editor_core::SelectionHoverUpdateResult hover =
          quader_editor_core::update_selection_hover(
              picking_objects, session_.selection_mode(), runtime_.hover(),
              to_runtime(request.ray), request.selected_modifier_pressed,
              pick_options, cached_pick_object_bounds(),
              cached_pick_face_geometry(pick_options));
      runtime_.apply_hover_update(hover);
      return {
          .ok = true,
          .changed = hover.changed,
      };
    }

    [[nodiscard]] ModelingHoverSummary selection_hover_summary() const {
      return from_runtime_hover_summary(session_, runtime_.hover(), false);
    }

    [[nodiscard]] ModelingHoverSummary
    preview_selection_hover(const ModelingHoverRequest &request) const {
      const std::vector<quader_editor_core::PolygonObject> &picking_objects =
          cached_scene_transformed_objects_for_picking();
      const quader_poly::PickOptions pick_options =
          pick_options_from_request(request);
      const quader_editor_core::SelectionHoverUpdateResult hover =
          quader_editor_core::update_selection_hover(
              picking_objects, session_.selection_mode(), runtime_.hover(),
              to_runtime(request.ray), request.selected_modifier_pressed,
              pick_options, cached_pick_object_bounds(),
              cached_pick_face_geometry(pick_options));
      return from_runtime_hover_summary(session_, hover.hover, hover.changed);
    }

    ModelingCommandResult clear_selection_hover() {
      quader_editor_core::EditorRuntimeCommand command;
      command.command_id =
          quader_editor_core::EditorRuntimeCommandId::ClearHover;
      const quader_editor_core::EditorRuntimeCommandResult result =
          quader_editor_core::execute_runtime_command(session_, runtime_,
                                                      command);
      return {
          .ok = result.command.ok,
          .changed = result.command.changed,
          .message = result.command.message,
      };
    }

    ModelingCommandResult execute_runtime_command(
        const quader_editor_core::EditorRuntimeCommand &command,
        bool may_change_content) {
      const std::uint64_t content_before = session_.state().content_revision;
      const quader_editor_core::EditorRuntimeCommandResult result =
          quader_editor_core::execute_runtime_command(session_, runtime_,
                                                      command);
      const ModelingCommandExecutor::SyncPlan sync_plan =
          command_executor_.runtime_sync_plan(
              may_change_content, content_before,
              session_.state().content_revision, result.command);
      if (sync_plan.sync_meshes) {
        sync_scene_objects_from_modeling(sync_plan.force_mesh_sync);
      }
      if (sync_plan.sync_selection) {
        sync_scene_selection_from_modeling();
      }
      if (sync_plan.invalidate_overlay) {
        invalidate_selection_overlay_caches();
      }
      return command_executor_.from_runtime(result.command);
    }

    ModelingCommandResult
    set_active_runtime_tool(quader_editor_core::ViewportInteractionTool tool) {
      const bool preserve_group_selection =
          app_selection_mode_ == ModelingSelectionMode::Group;
      const bool preserve_scene_transform_selection =
          !preserve_group_selection && !session_.has_selection() &&
          has_scene_transform_selection();
      const bool preserve_scene_selection =
          preserve_group_selection || preserve_scene_transform_selection;
      const std::vector<EntityId> selected_before =
          preserve_scene_selection ? scene_.selected_entities()
                                   : std::vector<EntityId>{};
      const EntityId active_before = preserve_scene_selection
                                         ? scene_.primary_selection()
                                         : kInvalidEntityId;

      ModelingCommandResult result = execute_runtime_command(
          RuntimeToolCommandService::set_active_tool(
              from_runtime_viewport_tool(tool)),
          false);
      if (preserve_scene_selection) {
        restore_scene_selection(selected_before, active_before);
        if (preserve_group_selection) {
          app_selection_mode_ = ModelingSelectionMode::Group;
        }
        invalidate_selection_overlay_caches();
      }
      return result;
    }

    ModelingCommandResult activate_viewport_tool(ModelingViewportTool tool) {
      const bool preserve_group_selection =
          app_selection_mode_ == ModelingSelectionMode::Group;
      const bool preserve_scene_transform_selection =
          !preserve_group_selection && !session_.has_selection() &&
          has_scene_transform_selection();
      const bool preserve_scene_selection =
          preserve_group_selection || preserve_scene_transform_selection;
      const std::vector<EntityId> selected_before =
          preserve_scene_selection ? scene_.selected_entities()
                                   : std::vector<EntityId>{};
      const EntityId active_before = preserve_scene_selection
                                         ? scene_.primary_selection()
                                         : kInvalidEntityId;
      const auto finish = [&](ModelingCommandResult result) {
        if (preserve_scene_selection) {
          restore_scene_selection(selected_before, active_before);
          if (preserve_group_selection) {
            app_selection_mode_ = ModelingSelectionMode::Group;
          }
          invalidate_selection_overlay_caches();
        }
        return result;
      };
      if (tool == ModelingViewportTool::Cut ||
          tool == ModelingViewportTool::Mirror) {
        promote_drawn_plane_tool_object_selection();
      }
      if (tool == ModelingViewportTool::Cut) {
        ModelingCommandResult activated = execute_runtime_command(
            RuntimeToolCommandService::activate_cut(), false);
        if (activated.ok) {
          const ModelingCommandResult mode_result =
              set_cut_mode(interaction_settings_service_.settings().cut_mode);
          activated.ok = mode_result.ok;
          activated.changed = activated.changed || mode_result.changed;
          if (!mode_result.message.empty()) {
            activated.message = mode_result.message;
          }
        }
        return finish(activated);
      }
      if (tool == ModelingViewportTool::Mirror) {
        return finish(execute_runtime_command(
            RuntimeToolCommandService::activate_mirror(), false));
      }
      return finish(set_active_runtime_tool(to_runtime(tool)));
    }

    ModelingCommandResult cancel_active_modal_tool() {
      switch (runtime_.active_tool()) {
      case quader_editor_core::ViewportInteractionTool::Box:
        return cancel_box();
      case quader_editor_core::ViewportInteractionTool::InsertEdgeLoop:
        (void)execute_runtime_command(
            RuntimeToolCommandService::cancel_insert_edge_loop(), false);
        return set_active_runtime_tool(
            quader_editor_core::ViewportInteractionTool::Select);
      case quader_editor_core::ViewportInteractionTool::Knife:
        runtime_.reset_knife_state();
        invalidate_selection_overlay_caches();
        return set_active_runtime_tool(
            quader_editor_core::ViewportInteractionTool::Select);
      case quader_editor_core::ViewportInteractionTool::Cut:
        (void)execute_runtime_command(RuntimeToolCommandService::cancel_cut(),
                                      false);
        return set_active_runtime_tool(
            quader_editor_core::ViewportInteractionTool::Select);
      case quader_editor_core::ViewportInteractionTool::Mirror:
        (void)execute_runtime_command(
            RuntimeToolCommandService::cancel_mirror(), false);
        return set_active_runtime_tool(
            quader_editor_core::ViewportInteractionTool::Select);
      case quader_editor_core::ViewportInteractionTool::Poly:
        (void)execute_runtime_command(RuntimeToolCommandService::cancel_poly(),
                                      false);
        return set_active_runtime_tool(
            quader_editor_core::ViewportInteractionTool::Select);
      case quader_editor_core::ViewportInteractionTool::Select:
      case quader_editor_core::ViewportInteractionTool::Move:
      case quader_editor_core::ViewportInteractionTool::Extend:
      case quader_editor_core::ViewportInteractionTool::Extrude:
      case quader_editor_core::ViewportInteractionTool::Rotate:
      case quader_editor_core::ViewportInteractionTool::Scale:
      case quader_editor_core::ViewportInteractionTool::Unknown:
        break;
      }
      return {.ok = true};
    }

    ModelingCommandResult
    update_insert_edge_loop_hover(const ModelingToolRayEvent &event) {
      return execute_runtime_command(
          RuntimeToolCommandService::update_insert_edge_loop_hover(event),
          false);
    }

    ModelingCommandResult
    begin_insert_edge_loop_drag(const ModelingToolRayEvent &event) {
      return execute_runtime_command(
          RuntimeToolCommandService::begin_insert_edge_loop_drag(event), false);
    }

    ModelingCommandResult
    update_insert_edge_loop_drag(const ModelingToolRayEvent &event) {
      return execute_runtime_command(
          RuntimeToolCommandService::update_insert_edge_loop_drag(event),
          false);
    }

    ModelingCommandResult commit_insert_edge_loop_drag() {
      return execute_runtime_command(
          RuntimeToolCommandService::commit_insert_edge_loop_drag(), true);
    }

    ModelingCommandResult
    update_knife_hover(const ModelingToolRayEvent &event) {
      const bool changed = runtime_.update_knife_preview_from_pick(
          session_, to_runtime(event.ray), to_runtime(event.camera),
          event.grid_size, event.snap_enabled, event.constrain_axis);
      if (changed) {
        invalidate_selection_overlay_caches();
      }
      return {.ok = true, .changed = changed};
    }

    ModelingCommandResult
    accept_knife_point(const ModelingToolRayEvent &event) {
      const bool changed = runtime_.accept_knife_current_point(
          session_, to_runtime(event.camera), event.grid_size);
      if (changed) {
        invalidate_selection_overlay_caches();
      }
      return {.ok = true, .changed = changed};
    }

    ModelingCommandResult commit_knife_tool() {
      const std::uint64_t content_before = session_.state().content_revision;
      const quader_editor_core::CommandResult result =
          runtime_.commit_knife_tool(session_);
      if (session_.state().content_revision != content_before ||
          result.changed) {
        sync_scene_objects_from_modeling(result.changed);
      }
      sync_scene_selection_from_modeling();
      invalidate_selection_overlay_caches();
      if (result.ok) {
        const ModelingCommandResult select_tool = set_active_runtime_tool(
            quader_editor_core::ViewportInteractionTool::Select);
        return {
            .ok = select_tool.ok,
            .changed = result.changed || select_tool.changed,
            .message =
                !result.message.empty() ? result.message : select_tool.message,
        };
      }
      return {.ok = result.ok,
              .changed = result.changed,
              .message = result.message};
    }

    ModelingCommandResult update_cut_hover(const ModelingToolRayEvent &event) {
      return execute_runtime_command(
          RuntimeToolCommandService::update_cut_hover(event), false);
    }

    ModelingCommandResult click_cut(const ModelingToolRayEvent &event) {
      return execute_runtime_command(RuntimeToolCommandService::click_cut(event),
                                     false);
    }

    ModelingCommandResult begin_cut_point_drag(int point_index) {
      return execute_runtime_command(
          RuntimeToolCommandService::begin_cut_point_drag(point_index), false);
    }

    ModelingCommandResult
    begin_cut_line_drag(const ModelingToolRayEvent &event) {
      return execute_runtime_command(
          RuntimeToolCommandService::begin_cut_line_drag(event), false);
    }

    ModelingCommandResult end_cut_drag() {
      return execute_runtime_command(RuntimeToolCommandService::end_cut_drag(),
                                     false);
    }

    ModelingCommandResult set_cut_mode(ModelingCutMode mode) {
      interaction_settings_service_.set_cut_mode(mode);
      return execute_runtime_command(RuntimeToolCommandService::set_cut_mode(mode),
                                     false);
    }

    ModelingCommandResult cycle_cut_mode() {
      return set_cut_mode(from_runtime(quader_editor_core::next_cut_mode(
          to_runtime(interaction_settings_service_.settings().cut_mode))));
    }

    ModelingCommandResult finalize_cut_tool() {
      return execute_runtime_command(RuntimeToolCommandService::finalize_cut(),
                                     true);
    }

    ModelingCommandResult
    update_mirror_hover(const ModelingToolRayEvent &event) {
      return execute_runtime_command(
          RuntimeToolCommandService::update_mirror_hover(event), false);
    }

    ModelingCommandResult click_mirror(const ModelingToolRayEvent &event) {
      return execute_runtime_command(
          RuntimeToolCommandService::click_mirror(event), false);
    }

    ModelingCommandResult begin_mirror_point_drag(int point_index) {
      return execute_runtime_command(
          RuntimeToolCommandService::begin_mirror_point_drag(point_index),
          false);
    }

    ModelingCommandResult
    begin_mirror_line_drag(const ModelingToolRayEvent &event) {
      return execute_runtime_command(
          RuntimeToolCommandService::begin_mirror_line_drag(event), false);
    }

    ModelingCommandResult end_mirror_drag() {
      return execute_runtime_command(
          RuntimeToolCommandService::end_mirror_drag(), false);
    }

    ModelingCommandResult finalize_mirror_tool() {
      return execute_runtime_command(
          RuntimeToolCommandService::finalize_mirror(), true);
    }

    ModelingCommandResult update_poly_hover(const ModelingToolRayEvent &event) {
      return execute_runtime_command(
          RuntimeToolCommandService::update_poly_hover(event), false);
    }

    ModelingCommandResult accept_poly_point() {
      return execute_runtime_command(
          RuntimeToolCommandService::accept_poly_point(), false);
    }

    ModelingCommandResult commit_poly_tool() {
      const std::uint64_t content_before = session_.state().content_revision;
      const quader_editor_core::EditorRuntimeCommandResult runtime_result =
          quader_editor_core::execute_runtime_command(
              session_, runtime_, RuntimeToolCommandService::commit_poly());
      if (session_.state().content_revision != content_before ||
          runtime_result.command.changed) {
        sync_scene_objects_from_modeling(runtime_result.command.changed);
      }
      sync_scene_selection_from_modeling();
      invalidate_selection_overlay_caches();
      if (runtime_result.command.ok) {
        const ModelingCommandResult select_tool = set_active_runtime_tool(
            quader_editor_core::ViewportInteractionTool::Select);
        return {
            .ok = select_tool.ok,
            .changed = runtime_result.command.changed || select_tool.changed,
            .message = !runtime_result.command.message.empty()
                           ? runtime_result.command.message
                           : select_tool.message,
        };
      }
      return {.ok = runtime_result.command.ok,
              .changed = runtime_result.command.changed,
              .message = runtime_result.command.message};
    }

    ModelingCommandResult activate_box_tool() {
      if (quader_host_contract::find_tool_descriptor("box") == nullptr) {
        return {.ok = false,
                .message =
                    "Box tool is not present in the modeling host contract."};
      }

      return set_active_runtime_tool(
          quader_editor_core::ViewportInteractionTool::Box);
    }

    ModelingCreateBoxResult
    create_box_from_bounds(const ModelingCreateBoxRequest &request) {
      const std::size_t object_count_before = session_.objects().size();
      const quader_editor_core::EditorRuntimeCommandResult runtime_result =
          quader_editor_core::execute_runtime_command(
              session_, runtime_,
              RuntimeToolCommandService::create_box_from_bounds(request));
      if (!runtime_result.command.ok || !runtime_result.command.changed ||
          session_.objects().size() <= object_count_before) {
        return {
            .ok = runtime_result.command.ok,
            .changed = runtime_result.command.changed,
            .message = runtime_result.command.message.empty()
                           ? "Modeling runtime did not create a box object."
                           : runtime_result.command.message,
        };
      }

      return create_scene_object_from_latest(
          request.name.empty() ? "Box" : request.name,
          runtime_result.command.message);
    }

    ModelingBoxPreview update_box_hover(const ModelingBoxToolEvent &event) {
      return execute_box_preview_command(
          RuntimeToolCommandService::update_box_hover(event),
          event.snap_enabled);
    }

    ModelingBoxPreview begin_box_footprint(const ModelingBoxToolEvent &event) {
      ModelingBoxPreview preview = execute_box_preview_command(
          RuntimeToolCommandService::begin_box_footprint(event),
          event.snap_enabled);
      if (preview.ok) {
        selection_projection_service_.restore_scene_selection(
            scene_, std::span<const EntityId>{}, kInvalidEntityId);
      }
      return preview;
    }

    ModelingBoxPreview update_box_footprint(const ModelingBoxToolEvent &event) {
      return execute_box_preview_command(
          RuntimeToolCommandService::update_box_footprint(event),
          event.snap_enabled);
    }

    ModelingBoxPreview begin_box_height(const ModelingBoxToolEvent &event) {
      return execute_box_preview_command(
          RuntimeToolCommandService::begin_box_height(event),
          event.snap_enabled);
    }

    ModelingBoxPreview update_box_height(const ModelingBoxToolEvent &event) {
      return execute_box_preview_command(
          RuntimeToolCommandService::update_box_height(event),
          event.snap_enabled);
    }

    [[nodiscard]] ModelingBoxPreview
    box_preview(bool snap_enabled = true) const {
      return current_box_preview(snap_enabled);
    }

    ModelingCreateBoxResult commit_box(std::string name) {
      const std::size_t object_count_before = session_.objects().size();
      const quader_editor_core::EditorRuntimeCommandResult runtime_result =
          quader_editor_core::execute_runtime_command(
              session_, runtime_, RuntimeToolCommandService::commit_box());
      if (!runtime_result.command.ok || !runtime_result.command.changed ||
          session_.objects().size() <= object_count_before) {
        return {
            .ok = runtime_result.command.ok,
            .changed = runtime_result.command.changed,
            .message = runtime_result.command.message.empty()
                           ? "Modeling runtime did not commit a box object."
                           : runtime_result.command.message,
        };
      }

      ModelingCreateBoxResult created = create_scene_object_from_latest(
          name.empty() ? "Box" : name, runtime_result.command.message);
      const ModelingCommandResult select_tool = set_active_runtime_tool(
          quader_editor_core::ViewportInteractionTool::Select);
      created.changed = created.changed || select_tool.changed;
      if (!select_tool.ok) {
        created.ok = false;
        created.message =
            select_tool.message.empty()
                ? "Box committed, but Modeling runtime tool reset failed."
                : select_tool.message;
      }
      return created;
    }

    ModelingCommandResult cancel_box() {
      const quader_editor_core::EditorRuntimeCommandResult result =
          quader_editor_core::execute_runtime_command(
              session_, runtime_, RuntimeToolCommandService::cancel_box());
      const ModelingCommandResult select_tool = set_active_runtime_tool(
          quader_editor_core::ViewportInteractionTool::Select);
      return {
          .ok = result.command.ok && select_tool.ok,
          .changed = result.command.changed || select_tool.changed,
          .message = !result.command.message.empty() ? result.command.message
                                                     : select_tool.message,
      };
    }

    ModelingCreateBoxResult
    create_mesh(const ModelingCreateMeshRequest &request) {
      const std::size_t object_count_before = session_.objects().size();
      const quader_editor_core::CommandResult runtime_result =
          session_.create_object_from_document(request.document, request.name,
                                               request.material_slot);
      if (!runtime_result.ok || !runtime_result.changed ||
          session_.objects().size() <= object_count_before) {
        return {
            .ok = runtime_result.ok,
            .changed = runtime_result.changed,
            .message = runtime_result.message.empty()
                           ? "Modeling runtime did not create a mesh object."
                           : runtime_result.message,
        };
      }

      return create_scene_object_from_latest(
          request.name.empty() ? "Mesh" : request.name, runtime_result.message);
    }

    ModelingCommandResult
    set_object_selection(std::span<const EntityId> entities, EntityId active) {
      if (!scene_objects_synced_for_current_modeling_content()) {
        sync_scene_objects_from_modeling();
      }

      const RuntimeObjectSelectionProjectionResult projection =
          selection_projection_service_
              .project_object_selection_for_scene_entities(
                  session_, entities, active, object_registry_);
      if (!projection.ok) {
        return {.ok = false,
                .message = "Selection does not contain modeling objects."};
      }

      if (!projection.changed) {
        sync_scene_selection_from_modeling();
        return {.ok = true, .changed = false};
      }

      sync_scene_selection_from_modeling();
      invalidate_selection_overlay_caches();
      return {.ok = true, .changed = true};
    }

    ModelingCommandResult
    set_component_selection(const ModelingComponentSelectionRequest &request) {
      const bool restored_preview_base =
          restore_active_operation_preview_base_for_selection_edit();
      auto finish_after_preview_restore =
          [&](const ModelingCommandResult &result) {
        if (restored_preview_base) {
          refresh_active_operation_preview_after_selection_edit(
              restored_preview_base, result.ok && result.changed);
        }
        return result;
      };

      const ModelingCommandResult selection =
          selection_service_.set_component_selection(session_, request);
      if (restored_preview_base) {
        return finish_after_preview_restore(selection);
      }
      if (!selection.ok) {
        return selection;
      }
      sync_scene_selection_from_modeling();
      invalidate_selection_overlay_caches();
      return selection;
    }

    ModelingCommandResult move_selection(QVec3 delta) {
      const quader_editor_core::CommandResult result =
          session_.translate_selected(to_runtime(delta));
      if (result.ok) {
        if (result.changed) {
          sync_scene_objects_from_modeling(true);
        }
        sync_scene_selection_from_modeling();
        invalidate_selection_overlay_caches();
      }
      return {.ok = result.ok,
              .changed = result.changed,
              .message = result.message};
    }

    ModelingCommandResult
    transform_selection(const ModelingTransform3 &transform, QVec3 pivot) {
      const quader_editor_core::CommandResult result =
          session_.transform_selected(to_runtime(transform), to_runtime(pivot));
      if (result.ok) {
        if (result.changed) {
          sync_scene_objects_from_modeling(true);
        }
        sync_scene_selection_from_modeling();
        invalidate_selection_overlay_caches();
      }
      return {.ok = result.ok,
              .changed = result.changed,
              .message = result.message};
    }

    ModelingCommandResult
    assign_material_slot_to_selection(std::uint32_t material_slot) {
      const quader_editor_core::CommandResult result =
          session_.assign_material_to_selection(material_slot);
      if (result.ok) {
        if (result.changed) {
          sync_scene_objects_from_modeling(true);
        }
        sync_scene_selection_from_modeling();
        invalidate_selection_overlay_caches();
      }
      return {.ok = result.ok,
              .changed = result.changed,
              .message = result.message};
    }

    ModelingCommandResult
    assign_material_slot_to_face(int modeling_object_id, int face_id,
                                 std::uint32_t material_slot) {
      const quader_editor_core::CommandResult result =
          session_.assign_material_to_face(
              modeling_object_id, static_cast<quader_poly::ElementId>(face_id),
              material_slot);
      if (result.ok) {
        if (result.changed) {
          sync_scene_objects_from_modeling(true);
        }
        sync_scene_selection_from_modeling();
        invalidate_selection_overlay_caches();
      }
      return {.ok = result.ok,
              .changed = result.changed,
              .message = result.message};
    }

    ModelingCommandResult
    set_selection_hover(const ModelingDirectHoverRequest &request) {
      quader_editor_core::SelectionHoverState hover;
      hover.object_id = request.modeling_object_id;
      hover.selected_modifier_pressed = request.selected_modifier_pressed;
      hover.candidate_object_id =
          request.kind == ModelingActiveSelectionKind::Object
              ? -1
              : request.modeling_object_id;
      switch (request.kind) {
      case ModelingActiveSelectionKind::Object:
        hover.kind = quader_editor_core::SelectionHoverKind::Object;
        break;
      case ModelingActiveSelectionKind::Vertex:
        hover.kind = quader_editor_core::SelectionHoverKind::Vertex;
        hover.vertex_id =
            static_cast<quader_poly::ElementId>(request.vertex_id);
        break;
      case ModelingActiveSelectionKind::Edge:
        hover.kind = quader_editor_core::SelectionHoverKind::Edge;
        hover.edge = quader_poly::make_edge(
            static_cast<quader_poly::ElementId>(request.edge.first),
            static_cast<quader_poly::ElementId>(request.edge.second));
        break;
      case ModelingActiveSelectionKind::Face:
        hover.kind = quader_editor_core::SelectionHoverKind::Face;
        hover.face_id = static_cast<quader_poly::ElementId>(request.face_id);
        break;
      case ModelingActiveSelectionKind::None:
      case ModelingActiveSelectionKind::Light:
        hover = {};
        break;
      }

      const bool changed =
          !SelectionOverlayCache::hover_matches(runtime_.hover(), hover);
      runtime_.apply_hover_update(
          quader_editor_core::SelectionHoverUpdateResult{
              .changed = changed,
              .hover = hover,
          });
      if (changed) {
        invalidate_selection_overlay_caches();
      }
      return {.ok = true, .changed = changed};
    }

private:
  void invalidate_selection_overlay_caches() const override {
    overlay_service_.invalidate();
  }

  [[nodiscard]] bool scene_objects_synced_for_current_modeling_content() const {
    return scene_sync_service_
        .scene_objects_synced_for_current_modeling_content(session_, scene_);
  }

  ModelingCommandResult update_transform_preview_result(
      const quader_editor_core::CommandResult &result) {
    if (result.ok) {
      invalidate_selection_overlay_caches();
      sync_scene_objects_from_modeling(true);
      sync_scene_selection_from_modeling();
    }
    return {
        .ok = result.ok,
        .changed = result.changed,
        .message = result.message,
    };
  }

  [[nodiscard]] bool has_transform_preview() const {
    return transform_preview_service_.has_transform_preview(session_);
  }

  [[nodiscard]] ModelingTransformDragServiceContext transform_drag_context() {
    return {
        .scene = scene_,
        .assets = assets_,
        .session = session_,
        .runtime = runtime_,
        .history_bridge = history_bridge_,
        .port = *this,
    };
  }

  [[nodiscard]] bool has_scene_transform_selection() const override {
    for (const EntityId entity : scene_.selected_entities()) {
      if (scene_.transform(entity) != nullptr &&
          !scene_.is_effectively_locked(entity)) {
        return true;
      }
    }
    return false;
  }

  [[nodiscard]] quader_editor_core::Bounds3 transform_drag_bounds()
      const override {
    quader_editor_core::Bounds3 bounds = session_.selection_bounds();
    if (!bounds.empty) {
      return bounds;
    }
    if (const std::optional<SceneBounds> scene_bounds =
            selected_transform_world_bounds(scene_, assets_)) {
      bounds.empty = false;
      bounds.min = scene_bounds->min;
      bounds.max = scene_bounds->max;
    }
    return bounds;
  }

  void sync_scene_objects_from_modeling(bool force = false) override {
    QDR_PROFILE_FUNCTION();
    mesh_sync_service_.sync_scene_objects_from_modeling(
        session_, scene_, assets_, scene_sync_service_, force);
  }

  [[nodiscard]] const std::vector<quader_editor_core::PolygonObject> &
  cached_scene_transformed_objects_for_picking() const {
    QDR_PROFILE_FUNCTION();
    return selection_service_.cached_scene_transformed_objects_for_picking(
        session_, scene_, object_registry_);
  }

  [[nodiscard]] std::vector<quader_editor_core::PolygonObject>
  scene_transformed_objects_for_picking() const {
    QDR_PROFILE_FUNCTION();
    return selection_service_.scene_transformed_objects_for_picking(
        session_, scene_, object_registry_);
  }

  [[nodiscard]] const std::vector<quader_editor_core::PickObjectBounds> &
  cached_pick_object_bounds() const override {
    QDR_PROFILE_FUNCTION();
    return selection_service_.cached_pick_object_bounds(session_, scene_,
                                                        object_registry_);
  }

  [[nodiscard]] const std::vector<quader_editor_core::PickObjectFaceGeometry> &
  cached_pick_face_geometry(quader_poly::PickOptions pick_options = {})
      const override {
    QDR_PROFILE_FUNCTION();
    return selection_service_.cached_pick_face_geometry(
        session_, scene_, object_registry_, pick_options);
  }

  ModelingCommandResult execute_runtime_no_settings(
      quader_editor_core::EditorRuntimeCommandId command_id,
      bool may_change_content) {
    return execute_runtime_command(RuntimeToolCommandService::no_settings(
                                       command_id),
                                   may_change_content);
  }

  ModelingCreateBoxResult
  create_scene_object_from_latest(const std::string &name,
                                  const std::string &message) {
    return mesh_sync_service_.create_scene_object_from_latest(
        session_, scene_, assets_, scene_sync_service_, name, message);
  }

  [[nodiscard]] ModelingBoxPreview
  current_box_preview(bool snap_enabled) const {
    const quader_editor_core::BoxPreviewDescriptor preview =
        runtime_.box_preview_descriptor(snap_enabled);
    if (preview.drag_state == quader_editor_core::BoxDragState::Idle &&
        from_runtime_viewport_tool(runtime_.active_tool()) !=
            ModelingViewportTool::Box) {
      return {
          .ok = preview.ok,
          .message = preview.message,
      };
    }
    std::vector<quader::QVec3> corners;
    if (preview.drag_state == quader_editor_core::BoxDragState::Height) {
      corners = quader_editor_core::box_preview_corners(preview);
    }
    return from_runtime(preview, corners);
  }

  ModelingBoxPreview execute_box_preview_command(
      const quader_editor_core::EditorRuntimeCommand &command,
      bool snap_enabled) {
    const quader_editor_core::EditorRuntimeCommandResult runtime_result =
        quader_editor_core::execute_runtime_command(session_, runtime_,
                                                    command);
    if (!runtime_result.command.ok) {
      return {
          .ok = false,
          .message = runtime_result.command.message,
      };
    }

    ModelingBoxPreview result = current_box_preview(snap_enabled);
    result.ok = runtime_result.command.ok;
    if (!runtime_result.command.message.empty()) {
      result.message = runtime_result.command.message;
    }
    return result;
  }

    SceneDocument& scene_;
    AssetRegistry& assets_;
    ModelingObjectRuntimeMap object_registry_;
    SceneModelingSyncService scene_sync_service_;
    ModelingMeshSyncService mesh_sync_service_;
    ModelingHierarchyDuplicateService hierarchy_duplicate_service_;
    ModelingSelectionService selection_service_;
    SelectionProjectionService selection_projection_service_;
    mutable ModelingOverlayService overlay_service_;
    ModelingCommandExecutor command_executor_;
    ModelingContextActionService context_action_service_;
    RuntimeOperationExecutor runtime_operation_executor_;
    TransformPreviewService transform_preview_service_;
    ModelingInteractionSettingsService interaction_settings_service_;
    ModelingToolSettingsService tool_settings_service_;
    ModelingTransformDragService transform_drag_service_;
    ModalToolRuntime modal_tool_runtime_;
    ModelingClipboardService clipboard_service_;
    ModelingHistoryBridge history_bridge_;
    ModelingSelectionMode app_selection_mode_ = ModelingSelectionMode::Object;

    quader_editor_core::EditorSession session_;
    quader_editor_core::EditorRuntime runtime_;
};

ModelingSessionService::ModelingSessionService(SceneDocument& scene, AssetRegistry& assets)
    : impl_(std::make_unique<Impl>(scene, assets)) {}

ModelingSessionService::~ModelingSessionService() = default;
ModelingSessionService::ModelingSessionService(ModelingSessionService&&) noexcept = default;
ModelingSessionService& ModelingSessionService::operator=(ModelingSessionService&&) noexcept = default;

ModelingCommandDescriptor ModelingSessionService::box_tool_command() const {
  return impl_->box_tool_command();
}

ModelingCommandDescriptor
ModelingSessionService::create_box_from_bounds_command() const {
  return impl_->create_box_from_bounds_command();
}

std::vector<ModelingObjectRecord> ModelingSessionService::objects() const {
    return impl_->objects();
}

std::optional<EntityId> ModelingSessionService::entity_for_modeling_object(
    int modeling_object_id) const {
  return impl_->entity_for_modeling_object(modeling_object_id);
}

std::optional<int>
ModelingSessionService::modeling_object_for_entity(EntityId entity) const {
  return impl_->modeling_object_for_entity(entity);
}

std::optional<MeshAsset>
ModelingSessionService::compile_object_mesh(int modeling_object_id) const {
  return impl_->compile_object_mesh(modeling_object_id);
}

std::optional<EntityId> ModelingSessionService::pick_object_entity(
    const ModelingPickSelectRequest &request) const {
  return impl_->pick_object_entity(request);
}

std::vector<EntityId> ModelingSessionService::box_pick_object_entities(
    const ModelingBoxSelectRequest &request) const {
  return impl_->box_pick_object_entities(request);
}

std::optional<QVec3> ModelingSessionService::selection_center() const {
  return impl_->selection_center();
}

ModelingSelectionMode ModelingSessionService::selection_mode() const {
  return impl_->selection_mode();
}

ModelingSelectionSummary ModelingSessionService::selection_summary() const {
  return impl_->selection_summary();
}

ModelingSelectionBoxBaseline
ModelingSessionService::capture_box_selection_baseline() const {
  return impl_->capture_box_selection_baseline();
}

std::uint64_t ModelingSessionService::selection_overlay_signature(
    ModelingSelectionOverlayOptions options) const {
  return impl_->selection_overlay_signature(options);
}

ViewportOverlayFrame ModelingSessionService::selection_overlay_frame(
    ModelingSelectionOverlayOptions options) const {
  return impl_->selection_overlay_frame(options);
}

std::vector<ModelingToolPreviewMesh>
ModelingSessionService::tool_preview_meshes() const {
  return impl_->tool_preview_meshes();
}

ModelingSessionMemento ModelingSessionService::capture_history_memento() const {
  return impl_->capture_history_memento();
}

ModelingSessionMemento
ModelingSessionService::capture_active_preview_history_memento() const {
  return impl_->capture_active_preview_history_memento();
}

ModelingCommandResult ModelingSessionService::restore_history_memento(
    const ModelingSessionMemento &memento) {
  return impl_->restore_history_memento(memento);
}

void ModelingSessionService::reset_from_scene() { impl_->reset_from_scene(); }

void ModelingSessionService::project_object_selection_from_scene(
    bool force_object_mode) {
  impl_->sync_object_selection_from_scene(force_object_mode);
}

ModelingCommandResult
ModelingSessionService::duplicate_selected_hierarchy_for_group_drag(
    DocumentCommandService &document_commands) {
  return impl_->duplicate_selected_hierarchy_for_group_drag(document_commands);
}

ModelingCommandResult
ModelingSessionService::duplicate_selected_entity_hierarchy(
    DocumentCommandService &document_commands) {
  return impl_->duplicate_selected_entity_hierarchy(document_commands);
}

ModelingCommandResult
ModelingSessionService::set_selection_mode(ModelingSelectionMode mode) {
  return impl_->set_selection_mode(mode);
}

ModelingCommandResult ModelingSessionService::select_all() {
  return impl_->select_all();
}

ModelingCommandResult ModelingSessionService::invert_selection() {
  return impl_->invert_selection();
}

ModelingCommandResult ModelingSessionService::clear_selection() {
  return impl_->clear_selection();
}

std::unique_ptr<IUndoableCommand>
ModelingSessionService::create_delete_selection_command() {
  return impl_->create_delete_selection_command();
}

bool ModelingSessionService::has_selected_scene_aware_delete_root() const {
  return impl_->has_selected_scene_aware_delete_root();
}

ModelingCommandResult ModelingSessionService::delete_component_selection() {
  return impl_->delete_component_selection();
}

ModelingCommandResult ModelingSessionService::copy_selection() {
  return impl_->copy_selection();
}

ModelingCommandResult ModelingSessionService::paste_selection() {
  return impl_->paste_selection();
}

ModelingCommandResult ModelingSessionService::repeat_last_action() {
  return impl_->repeat_last_action();
}

ModelingCommandResult
ModelingSessionService::mirror_selection(ModelingMirrorAxis axis) {
  return impl_->mirror_selection(axis);
}

ModelingCommandResult
ModelingSessionService::flip_selection(ModelingFlipOrientation orientation) {
  return impl_->flip_selection(orientation);
}

ModelingCommandResult
ModelingSessionService::apply_active_material(std::uint32_t material_slot) {
  return impl_->apply_active_material(material_slot);
}

bool ModelingSessionService::modal_tool_owns_viewport() const {
  return impl_->modal_tool_owns_viewport();
}

bool ModelingSessionService::can_cancel_active_tool_or_preview() const {
  return impl_->can_cancel_active_tool_or_preview();
}

bool ModelingSessionService::can_cancel_active_preview_for_undo() const {
  return impl_->can_cancel_active_preview_for_undo();
}

ModelingToolStatus ModelingSessionService::tool_status() const {
  return impl_->tool_status();
}

ModelingToolSettingsTarget
ModelingSessionService::tool_settings_target() const {
  return impl_->tool_settings_target();
}

ModelingInteractionSettings
ModelingSessionService::interaction_settings() const {
  return impl_->interaction_settings();
}

ModelingCommandResult ModelingSessionService::set_interaction_settings(
    const ModelingInteractionSettings &settings) {
  return impl_->set_interaction_settings(settings);
}

ModelingActionSet ModelingSessionService::context_action_set(
    const ModelingActionQuery &query) const {
  return impl_->context_action_set(query);
}

std::vector<ModelingActionItem> ModelingSessionService::context_actions(
    const ModelingActionQuery &query) const {
  return impl_->context_actions(query);
}

std::optional<ModelingActionItem>
ModelingSessionService::context_action(std::string_view action_id,
                                       const ModelingActionQuery &query) const {
  return impl_->context_action(action_id, query);
}

ModelingCommandResult
ModelingSessionService::execute_context_action(std::string_view action_id) {
  return impl_->execute_context_action(action_id);
}

ModelingCommandResult
ModelingSessionService::commit_active_operation_preview() {
  return impl_->commit_active_operation_preview();
}

ModelingCommandResult
ModelingSessionService::cancel_active_operation_preview() {
  return impl_->cancel_active_operation_preview();
}

std::vector<ModelingInstantOperationDescriptor>
ModelingSessionService::instant_operations() const {
  return impl_->instant_operations();
}

ModelingCommandResult ModelingSessionService::execute_instant_operation(
    ModelingInstantOperationId operation) {
  return impl_->execute_instant_operation(operation);
}

ModelingCommandResult
ModelingSessionService::begin_transform_preview(bool duplicate_selected) {
  return impl_->begin_transform_preview(duplicate_selected);
}

ModelingCommandResult
ModelingSessionService::update_transform_translate_preview(QVec3 delta) {
  return impl_->update_transform_translate_preview(delta);
}

ModelingCommandResult ModelingSessionService::update_transform_matrix_preview(
    const ModelingTransform3 &transform, QVec3 pivot) {
  return impl_->update_transform_matrix_preview(transform, pivot);
}

ModelingCommandResult ModelingSessionService::update_transform_extrude_preview(
    QVec3 offset, float closed_edge_ledge_size) {
  return impl_->update_transform_extrude_preview(offset,
                                                 closed_edge_ledge_size);
}

ModelingCommandResult ModelingSessionService::update_transform_inset_preview(
    const ModelingTransform3 &transform, QVec3 pivot) {
  return impl_->update_transform_inset_preview(transform, pivot);
}

ModelingCommandResult ModelingSessionService::commit_transform_preview() {
  return impl_->commit_transform_preview();
}

ModelingCommandResult ModelingSessionService::cancel_transform_preview() {
  return impl_->cancel_transform_preview();
}

ModelingCommandResult ModelingSessionService::record_repeatable_transform(
    const ModelingRepeatTransform &transform) {
  return impl_->record_repeatable_transform(transform);
}

ModelingTransformDragPreview ModelingSessionService::begin_transform_drag(
    const ModelingTransformDragBeginRequest &request) {
  QDR_PROFILE_FUNCTION();
  return impl_->begin_transform_drag(request);
}

ModelingTransformDragPreview ModelingSessionService::update_transform_drag(
    const ModelingTransformDragUpdateRequest &request) {
  QDR_PROFILE_FUNCTION();
  return impl_->update_transform_drag(request);
}

ModelingTransformDragPreview ModelingSessionService::commit_transform_drag() {
  QDR_PROFILE_FUNCTION();
  return impl_->commit_transform_drag();
}

ModelingTransformDragPreview
ModelingSessionService::commit_transform_drag(EditorHistoryService &history) {
  QDR_PROFILE_FUNCTION();
  return impl_->commit_transform_drag(&history);
}

ModelingCommandResult ModelingSessionService::cancel_transform_drag() {
  QDR_PROFILE_FUNCTION();
  return impl_->cancel_transform_drag();
}

ModelingSelectionSummary
ModelingSessionService::pick_select(const ModelingPickSelectRequest &request) {
  return impl_->pick_select(request);
}

ModelingSelectionSummary
ModelingSessionService::box_select(const ModelingBoxSelectRequest &request) {
  return impl_->box_select(request);
}

ModelingSelectionSummary ModelingSessionService::box_select(
    const ModelingBoxSelectRequest &request,
    const ModelingSelectionBoxBaseline &baseline) {
  return baseline.impl_ ? impl_->box_select(request, baseline.impl_->selection)
                        : impl_->box_select(request);
}

ModelingCommandResult ModelingSessionService::update_selection_hover(
    const ModelingHoverRequest &request) {
  return impl_->update_selection_hover(request);
}

ModelingHoverSummary ModelingSessionService::selection_hover_summary() const {
  return impl_->selection_hover_summary();
}

ModelingHoverSummary ModelingSessionService::preview_selection_hover(
    const ModelingHoverRequest &request) const {
  return impl_->preview_selection_hover(request);
}

ModelingCommandResult ModelingSessionService::clear_selection_hover() {
  return impl_->clear_selection_hover();
}

ModelingCommandResult
ModelingSessionService::activate_viewport_tool(ModelingViewportTool tool) {
  return impl_->activate_viewport_tool(tool);
}

ModelingCommandResult ModelingSessionService::cancel_active_modal_tool() {
  return impl_->cancel_active_modal_tool();
}

ModelingCommandResult ModelingSessionService::update_insert_edge_loop_hover(
    const ModelingToolRayEvent &event) {
  return impl_->update_insert_edge_loop_hover(event);
}

ModelingCommandResult ModelingSessionService::begin_insert_edge_loop_drag(
    const ModelingToolRayEvent &event) {
  return impl_->begin_insert_edge_loop_drag(event);
}

ModelingCommandResult ModelingSessionService::update_insert_edge_loop_drag(
    const ModelingToolRayEvent &event) {
  return impl_->update_insert_edge_loop_drag(event);
}

ModelingCommandResult ModelingSessionService::commit_insert_edge_loop_drag() {
  return impl_->commit_insert_edge_loop_drag();
}

ModelingCommandResult
ModelingSessionService::update_knife_hover(const ModelingToolRayEvent &event) {
  return impl_->update_knife_hover(event);
}

ModelingCommandResult
ModelingSessionService::accept_knife_point(const ModelingToolRayEvent &event) {
  return impl_->accept_knife_point(event);
}

ModelingCommandResult ModelingSessionService::commit_knife_tool() {
  return impl_->commit_knife_tool();
}

ModelingCommandResult
ModelingSessionService::update_cut_hover(const ModelingToolRayEvent &event) {
  return impl_->update_cut_hover(event);
}

ModelingCommandResult
ModelingSessionService::click_cut(const ModelingToolRayEvent &event) {
  return impl_->click_cut(event);
}

ModelingCommandResult
ModelingSessionService::begin_cut_point_drag(int point_index) {
  return impl_->begin_cut_point_drag(point_index);
}

ModelingCommandResult
ModelingSessionService::begin_cut_line_drag(const ModelingToolRayEvent &event) {
  return impl_->begin_cut_line_drag(event);
}

ModelingCommandResult ModelingSessionService::end_cut_drag() {
  return impl_->end_cut_drag();
}

ModelingCommandResult
ModelingSessionService::set_cut_mode(ModelingCutMode mode) {
  return impl_->set_cut_mode(mode);
}

ModelingCommandResult ModelingSessionService::cycle_cut_mode() {
  return impl_->cycle_cut_mode();
}

ModelingCommandResult ModelingSessionService::finalize_cut_tool() {
  return impl_->finalize_cut_tool();
}

ModelingCommandResult
ModelingSessionService::update_mirror_hover(const ModelingToolRayEvent &event) {
  return impl_->update_mirror_hover(event);
}

ModelingCommandResult
ModelingSessionService::click_mirror(const ModelingToolRayEvent &event) {
  return impl_->click_mirror(event);
}

ModelingCommandResult
ModelingSessionService::begin_mirror_point_drag(int point_index) {
  return impl_->begin_mirror_point_drag(point_index);
}

ModelingCommandResult ModelingSessionService::begin_mirror_line_drag(
    const ModelingToolRayEvent &event) {
  return impl_->begin_mirror_line_drag(event);
}

ModelingCommandResult ModelingSessionService::end_mirror_drag() {
  return impl_->end_mirror_drag();
}

ModelingCommandResult ModelingSessionService::finalize_mirror_tool() {
  return impl_->finalize_mirror_tool();
}

ModelingCommandResult
ModelingSessionService::update_poly_hover(const ModelingToolRayEvent &event) {
  return impl_->update_poly_hover(event);
}

ModelingCommandResult ModelingSessionService::accept_poly_point() {
  return impl_->accept_poly_point();
}

ModelingCommandResult ModelingSessionService::commit_poly_tool() {
  return impl_->commit_poly_tool();
}

ModelingCommandResult ModelingSessionService::activate_box_tool() {
  return impl_->activate_box_tool();
}

ModelingCreateBoxResult ModelingSessionService::create_box_from_bounds(
    const ModelingCreateBoxRequest &request) {
  return impl_->create_box_from_bounds(request);
}

ModelingBoxPreview
ModelingSessionService::update_box_hover(const ModelingBoxToolEvent &event) {
  return impl_->update_box_hover(event);
}

ModelingBoxPreview
ModelingSessionService::begin_box_footprint(const ModelingBoxToolEvent &event) {
  return impl_->begin_box_footprint(event);
}

ModelingBoxPreview ModelingSessionService::update_box_footprint(
    const ModelingBoxToolEvent &event) {
  return impl_->update_box_footprint(event);
}

ModelingBoxPreview
ModelingSessionService::begin_box_height(const ModelingBoxToolEvent &event) {
  return impl_->begin_box_height(event);
}

ModelingBoxPreview
ModelingSessionService::update_box_height(const ModelingBoxToolEvent &event) {
  return impl_->update_box_height(event);
}

ModelingBoxPreview
ModelingSessionService::box_preview(bool snap_enabled) const {
  return impl_->box_preview(snap_enabled);
}

ModelingCreateBoxResult ModelingSessionService::commit_box(std::string name) {
  return impl_->commit_box(std::move(name));
}

ModelingCommandResult ModelingSessionService::cancel_box() {
  return impl_->cancel_box();
}

ModelingCreateBoxResult
ModelingSessionService::create_mesh(const ModelingCreateMeshRequest &request) {
  return impl_->create_mesh(request);
}

ModelingCommandResult
ModelingSessionService::set_object_selection(std::span<const EntityId> entities,
                                             EntityId active) {
  return impl_->set_object_selection(entities, active);
}

ModelingCommandResult ModelingSessionService::set_component_selection(
    const ModelingComponentSelectionRequest &request) {
  return impl_->set_component_selection(request);
}

ModelingCommandResult ModelingSessionService::move_selection(QVec3 delta) {
  return impl_->move_selection(delta);
}

ModelingCommandResult
ModelingSessionService::transform_selection(const ModelingTransform3 &transform,
                                            QVec3 pivot) {
  return impl_->transform_selection(transform, pivot);
}

ModelingCommandResult ModelingSessionService::assign_material_slot_to_selection(
    std::uint32_t material_slot) {
  return impl_->assign_material_slot_to_selection(material_slot);
}

ModelingCommandResult ModelingSessionService::assign_material_slot_to_face(
    int modeling_object_id, int face_id, std::uint32_t material_slot) {
  return impl_->assign_material_slot_to_face(modeling_object_id, face_id,
                                             material_slot);
}

ModelingCommandResult ModelingSessionService::set_selection_hover(
    const ModelingDirectHoverRequest &request) {
  return impl_->set_selection_hover(request);
}

} // namespace quader
