////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/pivot/pivot_tool_service.hpp"

#include "assets/asset_registry.hpp"
#include "assets/mesh_asset.hpp"
#include "core/i_undoable_command.hpp"
#include "editor/history/editor_history_service.hpp"
#include "editor/modeling/modeling_types.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/selection/editor_selection_service.hpp"
#include "editor/selection/scene_selection_application.hpp"
#include "math/glm_bridge.hpp"
#include "mesh/polygon/document_model.hpp"
#include "scene/scene_bounds.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_transform.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace quader {
namespace {

constexpr float kPivotPositionEpsilon = 0.00001f;

bool near_same_point(QVec3 left, QVec3 right) {
  return length_squared(left - right) <=
         kPivotPositionEpsilon * kPivotPositionEpsilon;
}

QVec3 world_origin_of(const SceneDocument &scene, EntityId entity) {
  return transform_point(scene.world_transform_matrix(entity), {});
}

QMat4 with_world_translation(QMat4 matrix, QVec3 translation) {
  glm::mat4 value = to_glm(matrix);
  value[3][0] = translation.x;
  value[3][1] = translation.y;
  value[3][2] = translation.z;
  return from_glm(value);
}

std::optional<TransformComponent>
transform_after_world_matrix(const SceneDocument &scene, EntityId entity,
                             QMat4 world_matrix) {
  const TransformComponent *before = scene.transform(entity);
  if (before == nullptr) {
    return std::nullopt;
  }
  const EntityId parent = scene.parent(entity);
  const QMat4 parent_world = parent == kInvalidEntityId
                                 ? scene_transform_identity_matrix()
                                 : scene.world_transform_matrix(parent);
  TransformComponent result = transform_component_from_matrix(
      scene_local_matrix_from_world_matrix(parent_world, world_matrix),
      before->rotation_euler);
  result.custom_pivot = before->custom_pivot;
  return result;
}

bool same_transform(const TransformComponent &left,
                    const TransformComponent &right) {
  return left.position.x == right.position.x &&
         left.position.y == right.position.y &&
         left.position.z == right.position.z &&
         left.rotation_euler.x == right.rotation_euler.x &&
         left.rotation_euler.y == right.rotation_euler.y &&
         left.rotation_euler.z == right.rotation_euler.z &&
         left.scale.x == right.scale.x && left.scale.y == right.scale.y &&
         left.scale.z == right.scale.z &&
         left.custom_pivot == right.custom_pivot;
}

/**
 * Stores before/after transforms changed by one pivot rebase command.
 */
struct PivotTransformEdit {
    EntityId entity = kInvalidEntityId;
    TransformComponent before;
    TransformComponent after;
};

/**
 * Stores mesh asset/component edits required to compensate a mesh origin move.
 */
struct PivotMeshEdit {
    bool active = false;
    bool clone_on_write = false;
    EntityId entity = kInvalidEntityId;
    AssetId before_mesh = kInvalidAssetId;
    AssetId after_mesh = kInvalidAssetId;
    MeshComponent before_component;
    MeshAsset before_asset;
    MeshAsset after_asset;
};

/**
 * Collects all transform and mesh edits that make one pivot apply undoable.
 */
struct PivotRebasePlan {
    std::vector<PivotTransformEdit> transforms;
    PivotMeshEdit mesh;
};

void offset_mesh_vertices(MeshAsset &mesh, QVec3 local_delta) {
  for (MeshVertex &vertex : mesh.vertices) {
    vertex.position = vertex.position - local_delta;
  }
  if (mesh.authored_polygon_document != nullptr) {
    mesh.authored_polygon_document =
        std::make_shared<quader_poly::Document>(
            *mesh.authored_polygon_document);
    for (quader_poly::Vertex &vertex :
         mesh.authored_polygon_document->vertices) {
      vertex.position = vertex.position - local_delta;
    }
  }
}

int mesh_usage_count(const SceneDocument &scene, AssetId mesh) {
  int count = 0;
  for (const EntityId entity : scene.entities()) {
    const MeshComponent *component = scene.mesh(entity);
    if (component != nullptr && component->mesh == mesh) {
      ++count;
    }
  }
  return count;
}

std::optional<std::string>
prepare_mesh_edit(const SceneDocument &scene, const AssetRegistry &assets,
                  EntityId target, QVec3 local_delta,
                  PivotMeshEdit &edit) {
  const MeshComponent *component = scene.mesh(target);
  if (component == nullptr) {
    return std::nullopt;
  }
  const MeshAsset *mesh = assets.mesh(component->mesh);
  if (mesh == nullptr) {
    return std::string("Pivot target mesh asset does not exist.");
  }
  if (!mesh->has_stable_cpu_topology || mesh->vertices.empty()) {
    return std::string("Pivot target mesh does not have editable CPU mesh data.");
  }

  edit.active = true;
  edit.entity = target;
  edit.before_mesh = component->mesh;
  edit.before_component = *component;
  edit.before_asset = *mesh;
  edit.after_asset = *mesh;
  offset_mesh_vertices(edit.after_asset, local_delta);
  edit.clone_on_write = mesh_usage_count(scene, component->mesh) > 1;
  edit.after_mesh = edit.clone_on_write ? kInvalidAssetId : component->mesh;
  return std::nullopt;
}

std::optional<PivotRebasePlan>
make_rebase_plan(const SceneDocument &scene, const AssetRegistry &assets,
                 EntityId target, QVec3 pivot,
                 std::string &error_message) {
  if (!scene.exists(target)) {
    error_message = "Pivot target no longer exists.";
    return std::nullopt;
  }
  const TransformComponent *target_before = scene.transform(target);
  if (target_before == nullptr) {
    error_message = "Pivot target has no transform.";
    return std::nullopt;
  }

  const QMat4 target_world_before = scene.world_transform_matrix(target);
  const QVec3 world_delta =
      pivot - transform_point(target_world_before, QVec3{});
  PivotRebasePlan plan;

  const QMat4 target_world_after =
      with_world_translation(target_world_before, pivot);
  const std::optional<TransformComponent> target_after =
      transform_after_world_matrix(scene, target, target_world_after);
  if (!target_after.has_value()) {
    error_message = "Pivot target transform could not be resolved.";
    return std::nullopt;
  }
  TransformComponent target_after_transform = *target_after;
  target_after_transform.custom_pivot = true;
  if (!same_transform(*target_before, target_after_transform)) {
    plan.transforms.push_back(PivotTransformEdit{
        .entity = target,
        .before = *target_before,
        .after = target_after_transform,
    });
  }

  for (const EntityId child : scene.children(target)) {
    const TransformComponent *child_before = scene.transform(child);
    if (child_before == nullptr) {
      continue;
    }
    const QMat4 child_world_before = scene.world_transform_matrix(child);
    const std::optional<TransformComponent> child_after =
        transform_after_world_matrix(scene, child, child_world_before);
    if (!child_after.has_value()) {
      continue;
    }
    const QMat4 child_local_after = scene_local_matrix_from_world_matrix(
        target_world_after, child_world_before);
    TransformComponent compensated =
        transform_component_from_matrix(child_local_after,
                                        child_before->rotation_euler);
    compensated.custom_pivot = child_before->custom_pivot;
    if (!same_transform(*child_before, compensated)) {
      plan.transforms.push_back(PivotTransformEdit{
          .entity = child,
          .before = *child_before,
          .after = compensated,
      });
    }
  }

  if (scene.mesh(target) != nullptr) {
    const QMat4 inverse_world = inverse(target_world_before);
    const QVec3 local_delta = transform_vector(inverse_world, world_delta);
    if (const std::optional<std::string> mesh_error =
            prepare_mesh_edit(scene, assets, target, local_delta, plan.mesh)) {
      error_message = *mesh_error;
      return std::nullopt;
    }
  }

  if (plan.transforms.empty() && !plan.mesh.active) {
    error_message.clear();
    return std::nullopt;
  }
  return plan;
}

/**
 * Applies and restores one origin rebase transaction through editor history.
 */
class PivotRebaseCommand final : public IUndoableCommand {
public:
  PivotRebaseCommand(SceneDocument &scene, AssetRegistry &assets,
                     ModelingSessionService &modeling, PivotRebasePlan plan)
      : scene_(scene), assets_(assets), modeling_(modeling),
        plan_(std::move(plan)) {}

  const std::string &label() const override { return label_; }

  CommandResult undo() override {
    apply_transforms_before();
    if (!apply_mesh_before()) {
      return command_invalid_result("Unable to restore pivot mesh asset.");
    }
    modeling_.reset_from_scene();
    return command_changed_result();
  }

  CommandResult redo() override {
    if (!apply_mesh_after()) {
      return command_invalid_result("Unable to apply pivot mesh asset.");
    }
    apply_transforms_after();
    modeling_.reset_from_scene();
    return command_changed_result();
  }

private:
  void apply_transforms_before() {
    std::vector<SceneTransformAssignment> assignments;
    assignments.reserve(plan_.transforms.size());
    for (const PivotTransformEdit &edit : plan_.transforms) {
      assignments.push_back(
          SceneTransformAssignment{.entity = edit.entity,
                                   .transform = edit.before});
    }
    scene_.set_transforms(assignments);
  }

  void apply_transforms_after() {
    std::vector<SceneTransformAssignment> assignments;
    assignments.reserve(plan_.transforms.size());
    for (const PivotTransformEdit &edit : plan_.transforms) {
      assignments.push_back(
          SceneTransformAssignment{.entity = edit.entity,
                                   .transform = edit.after});
    }
    scene_.set_transforms(assignments);
  }

  bool apply_mesh_before() {
    if (!plan_.mesh.active) {
      return true;
    }
    if (plan_.mesh.clone_on_write) {
      scene_.set_mesh(plan_.mesh.entity, plan_.mesh.before_component);
      if (plan_.mesh.after_mesh != kInvalidAssetId) {
        (void)assets_.remove_mesh(plan_.mesh.after_mesh);
      }
      return true;
    }
    return assets_.replace_mesh(plan_.mesh.before_mesh,
                                plan_.mesh.before_asset);
  }

  bool apply_mesh_after() {
    if (!plan_.mesh.active) {
      return true;
    }
    if (!plan_.mesh.clone_on_write) {
      return assets_.replace_mesh(plan_.mesh.before_mesh,
                                  plan_.mesh.after_asset);
    }

    if (plan_.mesh.after_mesh == kInvalidAssetId) {
      plan_.mesh.after_mesh = assets_.add_mesh(plan_.mesh.after_asset);
      if (plan_.mesh.after_mesh == kInvalidAssetId) {
        return false;
      }
    } else if (assets_.mesh(plan_.mesh.after_mesh) == nullptr) {
      if (assets_.add_mesh(plan_.mesh.after_mesh, plan_.mesh.after_asset) ==
          kInvalidAssetId) {
        return false;
      }
    } else if (!assets_.replace_mesh(plan_.mesh.after_mesh,
                                     plan_.mesh.after_asset)) {
      return false;
    }

    MeshComponent after_component = plan_.mesh.before_component;
    after_component.mesh = plan_.mesh.after_mesh;
    scene_.set_mesh(plan_.mesh.entity, after_component);
    return true;
  }

  SceneDocument &scene_;
  AssetRegistry &assets_;
  ModelingSessionService &modeling_;
  PivotRebasePlan plan_;
  std::string label_ = "Set Pivot";
};

/**
 * Stores one unique quick-align preset and its resolved world position.
 */
struct PivotAlignPreset {
    std::string_view id;
    QVec3 position;
};

std::vector<PivotAlignPreset> pivot_align_presets(const SceneBounds &bounds) {
  const QVec3 center = bounds_center(bounds);
  return {
      {.id = "pivot.align.center", .position = center},
      {.id = "pivot.align.bottom_center",
       .position = {center.x, bounds.min.y, center.z}},
      {.id = "pivot.align.top_center",
       .position = {center.x, bounds.max.y, center.z}},
      {.id = "pivot.align.bottom_left_front",
       .position = {bounds.min.x, bounds.min.y, bounds.min.z}},
      {.id = "pivot.align.bottom_left_back",
       .position = {bounds.min.x, bounds.min.y, bounds.max.z}},
      {.id = "pivot.align.bottom_right_front",
       .position = {bounds.max.x, bounds.min.y, bounds.min.z}},
      {.id = "pivot.align.bottom_right_back",
       .position = {bounds.max.x, bounds.min.y, bounds.max.z}},
      {.id = "pivot.align.top_left_front",
       .position = {bounds.min.x, bounds.max.y, bounds.min.z}},
      {.id = "pivot.align.top_left_back",
       .position = {bounds.min.x, bounds.max.y, bounds.max.z}},
      {.id = "pivot.align.top_right_front",
       .position = {bounds.max.x, bounds.max.y, bounds.min.z}},
      {.id = "pivot.align.top_right_back",
       .position = {bounds.max.x, bounds.max.y, bounds.max.z}},
  };
}

std::optional<QVec3> bounds_position_for_action(const SceneBounds &bounds,
                                                std::string_view action_id) {
  const std::vector<PivotAlignPreset> presets = pivot_align_presets(bounds);
  for (const PivotAlignPreset &preset : presets) {
    if (preset.id == action_id) {
      return preset.position;
    }
  }
  return std::nullopt;
}

std::size_t nearest_align_preset_index(
    const std::vector<PivotAlignPreset> &presets, QVec3 world_position) {
  std::size_t best_index = 0u;
  float best_distance = std::numeric_limits<float>::max();
  for (std::size_t index = 0u; index < presets.size(); ++index) {
    const float distance =
        length_squared(presets[index].position - world_position);
    if (distance < best_distance) {
      best_distance = distance;
      best_index = index;
    }
  }
  return best_index;
}

std::optional<QVec3> cycled_bounds_position_for_action(
    const SceneBounds &bounds, QVec3 current_position,
    std::string_view action_id) {
  if (action_id != "pivot.align.next" && action_id != "pivot.align.prev") {
    return std::nullopt;
  }
  const std::vector<PivotAlignPreset> presets = pivot_align_presets(bounds);
  if (presets.empty()) {
    return std::nullopt;
  }
  const std::size_t current =
      nearest_align_preset_index(presets, current_position);
  const std::size_t next =
      action_id == "pivot.align.next"
          ? (current + 1u) % presets.size()
          : (current + presets.size() - 1u) % presets.size();
  return presets[next].position;
}

} // namespace

PivotToolService::PivotToolService(SceneDocument &scene, AssetRegistry &assets,
                                   EditorHistoryService &history,
                                   ModelingSessionService &modeling)
    : scene_(scene), assets_(assets), history_(history), modeling_(modeling) {}

PivotHandleState PivotToolService::handle_state() const {
  if (persistent_.has_value()) {
    return {
        .visible = true,
        .editing = true,
        .persistent = true,
        .target = persistent_->target,
        .world_position = persistent_->preview_world_position,
    };
  }
  if (quick_edit_active()) {
    return {
        .visible = true,
        .editing = true,
        .persistent = false,
        .target = kInvalidEntityId,
        .world_position = quick_.world_position,
    };
  }
  return {};
}

SelectionPivotOverride PivotToolService::selection_pivot_override() const {
  if (!quick_.override_valid || scene_.selected_entities().empty() ||
      quick_.selection_revision != scene_.selection_revision()) {
    return {};
  }
  return {
      .valid = true,
      .world_position = quick_.world_position,
  };
}

std::optional<QVec3> PivotToolService::persistent_preview_position() const {
  return persistent_.has_value()
             ? std::optional<QVec3>(persistent_->preview_world_position)
             : std::nullopt;
}

CommandResult
PivotToolService::activate_persistent(EditorSelectionService &selection) {
  const std::optional<EntityId> target = active_selection_target(selection);
  if (!target.has_value()) {
    return command_invalid_result("Select one mesh or group to edit its pivot.");
  }
  if (!scene_.exists(*target)) {
    return command_invalid_result("Pivot target no longer exists.");
  }

  const std::vector<EntityId> selected = scene_.selected_entities();
  if (selected.size() != 1u || selected.front() != *target ||
      scene_.primary_selection() != *target) {
    apply_scene_selection(scene_,
                          SceneSelectionState{
                              .selected = {*target},
                              .active = *target,
                          },
                          SceneSelectionApplicationMode::RuntimeSync);
    modeling_.project_object_selection_from_scene(true);
  }

  const QVec3 origin = world_origin_of(scene_, *target);
  persistent_ = PersistentSession{
      .target = *target,
      .kind = target_kind(*target, selection),
      .original_world_position = origin,
      .preview_world_position = origin,
      .selection_revision = scene_.selection_revision(),
  };
  return command_changed_result();
}

CommandResult PivotToolService::cancel_persistent() {
  if (!persistent_.has_value()) {
    return command_unchanged_result();
  }
  persistent_.reset();
  return command_changed_result();
}

CommandResult PivotToolService::apply_persistent() {
  if (!persistent_.has_value()) {
    return command_unchanged_result();
  }
  const PersistentSession session = *persistent_;
  persistent_.reset();
  if (near_same_point(session.original_world_position,
                      session.preview_world_position)) {
    return command_changed_result();
  }

  std::string error_message;
  std::optional<PivotRebasePlan> plan = make_rebase_plan(
      scene_, assets_, session.target, session.preview_world_position,
      error_message);
  if (!plan.has_value()) {
    if (error_message.empty()) {
      return command_changed_result();
    }
    return command_invalid_result(std::move(error_message));
  }
  return history_.execute(std::make_unique<PivotRebaseCommand>(
      scene_, assets_, modeling_, std::move(*plan)));
}

CommandResult
PivotToolService::set_snap_enabled(std::string_view setting_id, bool enabled) {
  bool *target = nullptr;
  if (setting_id == kPivotSnapVertexSetting) {
    target = &snap_settings_.vertex;
  } else if (setting_id == kPivotSnapEdgeMidpointSetting) {
    target = &snap_settings_.edge_midpoint;
  } else if (setting_id == kPivotSnapFaceCenterSetting) {
    target = &snap_settings_.face_center;
  } else if (setting_id == kPivotSnapGridSetting) {
    target = &snap_settings_.grid;
  }
  if (target == nullptr) {
    return command_invalid_result("Unknown pivot snap setting.");
  }
  if (*target == enabled) {
    return command_unchanged_result();
  }
  *target = enabled;
  return command_changed_result();
}

CommandResult PivotToolService::execute_align_action(std::string_view action_id) {
  if (!persistent_.has_value()) {
    return command_invalid_result("Pivot tool is not active.");
  }
  const std::optional<SceneBounds> bounds = target_bounds();
  if (!bounds.has_value()) {
    return command_invalid_result("Pivot target has no bounds.");
  }
  std::optional<QVec3> world_position = cycled_bounds_position_for_action(
      *bounds, persistent_->preview_world_position, action_id);
  if (!world_position.has_value()) {
    world_position = bounds_position_for_action(*bounds, action_id);
  }
  if (!world_position.has_value()) {
    return command_invalid_result("Unknown pivot align action.");
  }
  return align_to_world_position(*world_position);
}

void PivotToolService::set_persistent_preview_position(QVec3 world_position) {
  if (persistent_.has_value()) {
    persistent_->preview_world_position = world_position;
  }
}

void PivotToolService::begin_quick(QVec3 initial_world_position) {
  if (scene_.selected_entities().empty()) {
    clear_quick_override();
    return;
  }
  quick_ = QuickState{
      .override_valid = true,
      .editing = true,
      .selection_revision = scene_.selection_revision(),
      .world_position = initial_world_position,
  };
}

void PivotToolService::set_quick_preview_position(QVec3 world_position) {
  if (!quick_edit_active()) {
    return;
  }
  quick_.world_position = world_position;
  quick_.override_valid = true;
}

void PivotToolService::end_quick_edit() {
  if (quick_edit_active()) {
    quick_.editing = false;
  }
}

void PivotToolService::clear_quick_override() { quick_ = QuickState{}; }

bool PivotToolService::quick_edit_active() const {
  return quick_.editing && quick_.override_valid &&
         quick_.selection_revision == scene_.selection_revision() &&
         !scene_.selected_entities().empty();
}

std::optional<EntityId> PivotToolService::active_selection_target(
    const EditorSelectionService &selection) const {
  const EditorSelectionState state = selection.state();
  if (state.target.entity != kInvalidEntityId &&
      scene_.exists(state.target.entity)) {
    return state.target.entity;
  }
  if (state.active_entity != kInvalidEntityId &&
      scene_.exists(state.active_entity)) {
    return state.active_entity;
  }
  const std::vector<EntityId> selected = scene_.selected_entities();
  if (!selected.empty()) {
    return selected.back();
  }
  return std::nullopt;
}

PivotToolService::PivotTargetKind PivotToolService::target_kind(
    EntityId target, const EditorSelectionService &selection) const {
  const EditorSelectionState state = selection.state();
  if (state.mode == ModelingSelectionMode::Group || scene_.has_children(target)) {
    return PivotTargetKind::Group;
  }
  return PivotTargetKind::Mesh;
}

std::optional<SceneBounds> PivotToolService::target_bounds() const {
  if (!persistent_.has_value()) {
    return std::nullopt;
  }
  return persistent_->kind == PivotTargetKind::Group
             ? entity_hierarchy_world_bounds(scene_, assets_,
                                             persistent_->target)
             : entity_world_bounds(scene_, assets_, persistent_->target);
}

CommandResult PivotToolService::align_to_world_position(QVec3 world_position) {
  if (!persistent_.has_value()) {
    return command_invalid_result("Pivot tool is not active.");
  }
  if (near_same_point(persistent_->preview_world_position, world_position)) {
    return command_unchanged_result();
  }
  persistent_->preview_world_position = world_position;
  return command_changed_result();
}

} // namespace quader
