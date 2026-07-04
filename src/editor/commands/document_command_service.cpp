////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/commands/document_command_service.hpp"

#include "assets/asset_registry.hpp"
#include "diagnostics/profile.hpp"
#include "editor/commands/document/apply_entity_transform_command.hpp"
#include "editor/commands/document/batch_document_command.hpp"
#include "editor/commands/document/create_entity_command.hpp"
#include "editor/commands/document/create_light_command.hpp"
#include "editor/commands/document/destroy_entity_command.hpp"
#include "editor/commands/document/duplicate_entity_command.hpp"
#include "editor/commands/document/duplicate_selected_hierarchy_command.hpp"
#include "editor/commands/document/group_selection_command.hpp"
#include "editor/commands/document/rename_entity_command.hpp"
#include "editor/commands/document/reparent_entity_command.hpp"
#include "editor/commands/document/select_entities_command.hpp"
#include "editor/commands/document/set_lock_command.hpp"
#include "editor/commands/document/set_light_component_command.hpp"
#include "editor/commands/document/set_material_command.hpp"
#include "editor/commands/document/set_mesh_component_command.hpp"
#include "editor/commands/document/set_scene_settings_command.hpp"
#include "editor/commands/document/set_visibility_command.hpp"
#include "editor/commands/document/ungroup_selection_command.hpp"
#include "scene/scene_document.hpp"

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace quader {
namespace {

DocumentCommandResult
execute_document_command(EditorHistoryService &history,
                         std::unique_ptr<IUndoableCommand> command) {
  return history.execute(std::move(command));
}

DocumentCommandResult
execute_document_command(EditorHistoryService &history,
                         std::unique_ptr<IUndoableCommand> command,
                         UndoRedoExecuteOptions options) {
  return history.execute(std::move(command), std::move(options));
}

void assign_document_command_status(DocumentCommandResult &target,
                                    DocumentCommandResult source) {
  target.ok = source.ok;
  target.changed = source.changed;
  target.message = std::move(source.message);
}

bool document_command_entity_is_modeling_owned(const SceneDocument &scene,
                                               EntityId entity) {
  return scene.modeling_object(entity) != nullptr;
}

DocumentCommandResult document_command_modeling_owned_entity_result() {
  return document_command_invalid_request_result(
      "Entity is owned by a modeling object; use modeling commands");
}

DocumentCommandResult document_command_scene_root_protected_result() {
  return document_command_invalid_request_result(
      "Scene root cannot be changed by this command");
}

bool document_command_selection_includes_scene_root(
    const SceneDocument &scene) {
  for (const EntityId entity : scene.selected_entities()) {
    if (scene.is_scene_root(entity)) {
      return true;
    }
  }
  return false;
}

bool document_command_selected_root_contains_modeling_owned_entity(
    const SceneDocument &scene) {
  for (const EntityId root : document_command_selected_root_entities(scene)) {
    if (document_command_entity_is_modeling_owned(scene, root)) {
      return true;
    }
  }
  return false;
}

EntityId normalized_create_parent(const SceneDocument &scene, EntityId parent) {
  if (parent != kInvalidEntityId) {
    return parent;
  }
  return scene.scene_root();
}

EntityId normalized_hierarchy_parent(const SceneDocument &scene,
                                     EntityId entity, EntityId parent) {
  if (parent != kInvalidEntityId || scene.is_scene_root(entity)) {
    return parent;
  }
  return scene.scene_root();
}

std::vector<DocumentTransformEdit>
normalized_transform_edits(const SceneDocument &scene,
                           std::vector<DocumentTransformEdit> edits) {
  std::vector<DocumentTransformEdit> result;
  result.reserve(edits.size());
  for (const DocumentTransformEdit &edit : edits) {
    if (edit.entity == kInvalidEntityId || !scene.exists(edit.entity) ||
        scene.is_effectively_locked(edit.entity) ||
        document_command_same_transform(edit.before, edit.after)) {
      continue;
    }
    result.push_back(edit);
  }
  std::sort(result.begin(), result.end(), [](const auto &lhs, const auto &rhs) {
    return lhs.entity < rhs.entity;
  });
  return result;
}

std::vector<DocumentVisibilityEdit>
normalized_visibility_edits(const SceneDocument &scene,
                            std::vector<DocumentVisibilityEdit> edits) {
  std::vector<DocumentVisibilityEdit> result;
  result.reserve(edits.size());
  for (const DocumentVisibilityEdit &edit : edits) {
    if (edit.entity == kInvalidEntityId || !scene.exists(edit.entity) ||
        scene.is_scene_root(edit.entity) || edit.before == edit.after) {
      continue;
    }
    result.push_back(edit);
  }
  std::sort(result.begin(), result.end(), [](const auto &lhs, const auto &rhs) {
    return lhs.entity < rhs.entity;
  });
  return result;
}

std::vector<DocumentLockEdit>
normalized_lock_edits(const SceneDocument &scene,
                      std::vector<DocumentLockEdit> edits) {
  std::vector<DocumentLockEdit> result;
  result.reserve(edits.size());
  for (const DocumentLockEdit &edit : edits) {
    if (edit.entity == kInvalidEntityId || !scene.exists(edit.entity) ||
        scene.is_scene_root(edit.entity) || edit.before == edit.after) {
      continue;
    }
    result.push_back(edit);
  }
  std::sort(result.begin(), result.end(), [](const auto &lhs, const auto &rhs) {
    return lhs.entity < rhs.entity;
  });
  return result;
}

bool document_command_effectively_locked_after_edits(
    const SceneDocument &scene, EntityId entity,
    const std::unordered_map<EntityId, bool> &locked_after_by_entity) {
  while (entity != kInvalidEntityId && scene.exists(entity)) {
    if (const auto edited = locked_after_by_entity.find(entity);
        edited != locked_after_by_entity.end()) {
      if (edited->second) {
        return true;
      }
    } else if (const LockComponent *lock = scene.lock(entity);
               lock != nullptr && lock->locked) {
      return true;
    }
    entity = scene.parent(entity);
  }
  return false;
}

DocumentSelectionState document_command_selection_after_lock_edits(
    const SceneDocument &scene, const std::vector<DocumentLockEdit> &edits) {
  std::unordered_map<EntityId, bool> locked_after_by_entity;
  locked_after_by_entity.reserve(edits.size());
  for (const DocumentLockEdit &edit : edits) {
    locked_after_by_entity[edit.entity] = edit.after;
  }

  DocumentSelectionState after;
  const std::vector<EntityId> before_selected = scene.selected_entities();
  after.selected.reserve(before_selected.size());
  for (const EntityId selected : before_selected) {
    if (!document_command_effectively_locked_after_edits(
            scene, selected, locked_after_by_entity)) {
      after.selected.push_back(selected);
    }
  }

  const EntityId before_active = scene.primary_selection();
  after.active =
      std::find(after.selected.begin(), after.selected.end(), before_active) !=
              after.selected.end()
          ? before_active
          : (after.selected.empty() ? kInvalidEntityId : after.selected.back());
  return after;
}

void append_selection_cleanup_after_lock_edits(
    SceneDocument &scene, const std::vector<DocumentLockEdit> &edits,
    std::vector<std::unique_ptr<IUndoableCommand>> &commands) {
  if (edits.empty()) {
    return;
  }

  const DocumentSelectionState before =
      document_command_capture_selection(scene);
  const DocumentSelectionState after =
      document_command_selection_after_lock_edits(scene, edits);
  if (document_command_same_selection(before, after)) {
    return;
  }

  commands.push_back(std::make_unique<SelectEntitiesCommand>(
      scene, after.selected, after.active));
}

/**
 * Stores the simulated batch entity state used while converting scene batch operations to commands.
 */
struct DocumentSceneBatchEntityState {
    std::string name;
    EntityId parent = kInvalidEntityId;
    TransformComponent transform;
    bool visible = true;
    bool locked = false;
};

} // namespace

DocumentCommandService::DocumentCommandService(
        SceneDocument& scene,
        EditorHistoryService& history,
        const AssetRegistry* assets)
    : scene_(scene)
    , history_(history)
    , assets_(assets) {}

DocumentCreateEntityResult
DocumentCommandService::create_entity(std::string name, EntityKind kind,
                                      EntityId parent,
                                      TransformComponent transform) {
  parent = normalized_create_parent(scene_, parent);
  if (parent != kInvalidEntityId && !scene_.exists(parent)) {
    DocumentCreateEntityResult result;
    assign_document_command_status(result,
                                   document_command_invalid_request_result(
                                       "Parent entity does not exist"));
    return result;
  }
  auto command = std::make_unique<CreateEntityCommand>(scene_, std::move(name),
                                                       kind, parent, transform);
  CreateEntityCommand *created_command = command.get();
  DocumentCreateEntityResult result;
  assign_document_command_status(
      result, execute_document_command(history_, std::move(command)));
  if (result.ok && result.changed) {
    result.entity = created_command->entity();
  }
  return result;
}

DocumentCommandResult DocumentCommandService::destroy_entity(EntityId entity) {
  if (!scene_.exists(entity)) {
    return document_command_missing_entity_result();
  }
  if (scene_.is_scene_root(entity)) {
    return document_command_scene_root_protected_result();
  }
  if (document_command_entity_is_modeling_owned(scene_, entity)) {
    return document_command_modeling_owned_entity_result();
  }
  return execute_document_command(
      history_, std::make_unique<DestroyEntityCommand>(scene_, entity));
}

DocumentCommandResult
DocumentCommandService::reparent_entity(EntityId entity, EntityId parent,
                                        bool preserve_world_transform) {
  if (!scene_.exists(entity)) {
    return document_command_missing_entity_result();
  }
  if (scene_.is_scene_root(entity)) {
    return document_command_scene_root_protected_result();
  }
  parent = normalized_hierarchy_parent(scene_, entity, parent);
  if (parent != kInvalidEntityId && !scene_.exists(parent)) {
    return document_command_invalid_request_result(
        "Parent entity does not exist");
  }
  if (scene_.parent(entity) == parent) {
    return document_command_unchanged_result();
  }
  return execute_document_command(
      history_, std::make_unique<ReparentEntityCommand>(
                    scene_, entity, parent, preserve_world_transform));
}

DocumentGroupSelectionResult
DocumentCommandService::group_selection(std::string group_name,
                                        TransformComponent group_transform) {
  auto command = std::make_unique<GroupSelectionCommand>(
      scene_, std::move(group_name), group_transform);
  GroupSelectionCommand *group_command = command.get();
  DocumentGroupSelectionResult result;
  assign_document_command_status(
      result, execute_document_command(history_, std::move(command)));
  if (result.ok && result.changed) {
    result.group_entity = group_command->group_entity();
  }
  return result;
}

DocumentCommandResult DocumentCommandService::ungroup_selection() {
  return execute_document_command(
      history_, std::make_unique<UngroupSelectionCommand>(scene_));
}

DocumentDuplicateEntityResult
DocumentCommandService::duplicate_entity(EntityId entity) {
  if (!scene_.exists(entity)) {
    DocumentDuplicateEntityResult result;
    assign_document_command_status(result,
                                   document_command_missing_entity_result());
    return result;
  }
  if (scene_.is_scene_root(entity)) {
    DocumentDuplicateEntityResult result;
    assign_document_command_status(
        result, document_command_scene_root_protected_result());
    return result;
  }
  if (document_command_entity_is_modeling_owned(scene_, entity)) {
    DocumentDuplicateEntityResult result;
    assign_document_command_status(
        result, document_command_modeling_owned_entity_result());
    return result;
  }

  auto command = std::make_unique<DuplicateEntityCommand>(scene_, entity);
  DuplicateEntityCommand *duplicate_command = command.get();
  DocumentDuplicateEntityResult result;
  assign_document_command_status(
      result, execute_document_command(history_, std::move(command)));
  if (result.ok && result.changed) {
    result.duplicate_entity = duplicate_command->duplicate_entity();
  }
  return result;
}

DocumentDuplicateHierarchyResult
DocumentCommandService::duplicate_selected_hierarchy() {
  QDR_PROFILE_FUNCTION();
  if (document_command_selection_includes_scene_root(scene_)) {
    DocumentDuplicateHierarchyResult result;
    assign_document_command_status(
        result, document_command_scene_root_protected_result());
    return result;
  }
  if (document_command_selected_root_contains_modeling_owned_entity(scene_)) {
    DocumentDuplicateHierarchyResult result;
    assign_document_command_status(
        result, document_command_modeling_owned_entity_result());
    return result;
  }

  auto command = std::make_unique<DuplicateSelectedHierarchyCommand>(scene_);
  DuplicateSelectedHierarchyCommand *duplicate_command = command.get();
  DocumentDuplicateHierarchyResult result;
  {
    QDR_PROFILE_SCOPE("Duplicate execute selected hierarchy command");
    assign_document_command_status(
        result, execute_document_command(history_, std::move(command)));
  }
  if (result.ok && result.changed) {
    result.duplicate_roots = duplicate_command->duplicate_roots();
  }
  return result;
}

DocumentDuplicateHierarchyResult
DocumentCommandService::duplicate_hierarchy_with_existing_clones(
    std::vector<EntityId> roots,
    std::unordered_map<EntityId, EntityId> existing_clones_by_source) {
  QDR_PROFILE_FUNCTION();
  std::unordered_set<EntityId> seen_roots;
  std::vector<EntityId> existing_roots;
  existing_roots.reserve(roots.size());
  for (const EntityId root : roots) {
    if (root != kInvalidEntityId && scene_.exists(root) &&
        !scene_.is_scene_root(root) && seen_roots.insert(root).second) {
      existing_roots.push_back(root);
    }
  }
  roots = std::move(existing_roots);
  if (roots.empty()) {
    DocumentDuplicateHierarchyResult result;
    assign_document_command_status(result, document_command_unchanged_result());
    return result;
  }

  std::vector<DocumentEntitySnapshot> source_snapshots;
  source_snapshots.reserve(roots.size());
  {
    QDR_PROFILE_SCOPE("Duplicate capture hierarchy snapshots");
    for (const EntityId root : roots) {
      std::optional<DocumentEntitySnapshot> snapshot =
          document_command_capture_entity_snapshot(scene_, root);
      if (!snapshot.has_value()) {
        DocumentDuplicateHierarchyResult result;
        assign_document_command_status(
            result, document_command_missing_entity_result());
        return result;
      }
      source_snapshots.push_back(std::move(*snapshot));
    }
  }

  auto command = std::make_unique<DuplicateSelectedHierarchyCommand>(
      scene_, std::move(source_snapshots),
      std::move(existing_clones_by_source));
  DuplicateSelectedHierarchyCommand *duplicate_command = command.get();
  DocumentDuplicateHierarchyResult result;
  assign_document_command_status(
      result, execute_document_command(history_, std::move(command)));
  if (result.ok && result.changed) {
    result.duplicate_roots = duplicate_command->duplicate_roots();
  }
  return result;
}

DocumentCreateEntityResult DocumentCommandService::create_point_light(
    std::string name, LightComponent light, EntityId parent,
    TransformComponent transform, bool select_created) {
  light.type = LightType::Point;
  parent = normalized_create_parent(scene_, parent);
  if (parent != kInvalidEntityId && !scene_.exists(parent)) {
    DocumentCreateEntityResult result;
    assign_document_command_status(result,
                                   document_command_invalid_request_result(
                                       "Parent entity does not exist"));
    return result;
  }
  auto command = std::make_unique<CreateLightCommand>(
      scene_, "Create Point Light", std::move(name), light, parent, transform,
      select_created);
  CreateLightCommand *created_command = command.get();
  DocumentCreateEntityResult result;
  {
    QDR_PROFILE_SCOPE("Duplicate execute hierarchy command");
    assign_document_command_status(
        result, execute_document_command(history_, std::move(command)));
  }
  if (result.ok && result.changed) {
    result.entity = created_command->entity();
  }
  return result;
}

DocumentCreateEntityResult DocumentCommandService::create_directional_light(
    std::string name, LightComponent light, EntityId parent,
    TransformComponent transform, bool select_created) {
  light.type = LightType::Directional;
  parent = normalized_create_parent(scene_, parent);
  if (parent != kInvalidEntityId && !scene_.exists(parent)) {
    DocumentCreateEntityResult result;
    assign_document_command_status(result,
                                   document_command_invalid_request_result(
                                       "Parent entity does not exist"));
    return result;
  }
  auto command = std::make_unique<CreateLightCommand>(
      scene_, "Create Directional Light", std::move(name), light, parent,
      transform, select_created);
  CreateLightCommand *created_command = command.get();
  DocumentCreateEntityResult result;
  assign_document_command_status(
      result, execute_document_command(history_, std::move(command)));
  if (result.ok && result.changed) {
    result.entity = created_command->entity();
  }
  return result;
}

DocumentCommandResult
DocumentCommandService::select_entities(std::vector<EntityId> entities,
                                        EntityId active,
                                        SceneSelectionApplicationMode mode) {
  return execute_document_command(history_,
                                  std::make_unique<SelectEntitiesCommand>(
                                      scene_, std::move(entities), active,
                                      mode));
}

DocumentCommandResult
DocumentCommandService::apply_entity_transform(EntityId entity,
                                               TransformComponent transform) {
  const TransformComponent *current_transform = scene_.transform(entity);
  if (!current_transform) {
    return document_command_missing_entity_result();
  }
  if (document_command_same_transform(*current_transform, transform)) {
    return document_command_unchanged_result();
  }

  return execute_document_command(
      history_, std::make_unique<ApplyEntityTransformCommand>(
                    scene_, entity, *current_transform, transform));
}

DocumentCommandResult DocumentCommandService::rename_entity(EntityId entity,
                                                            std::string name) {
  const NameComponent *current_name = scene_.name(entity);
  if (!current_name) {
    return document_command_missing_entity_result();
  }
  if (current_name->name == name) {
    return document_command_unchanged_result();
  }
  if (scene_.is_scene_root(entity)) {
    return document_command_scene_root_protected_result();
  }

  const std::string before = current_name->name;
  return execute_document_command(
      history_, std::make_unique<RenameEntityCommand>(scene_, entity, before,
                                                      std::move(name)));
}

DocumentCommandResult DocumentCommandService::set_visible(EntityId entity,
                                                          bool visible) {
  const VisibilityComponent *current_visibility = scene_.visibility(entity);
  if (!current_visibility) {
    return document_command_missing_entity_result();
  }
  if (current_visibility->visible == visible) {
    return document_command_unchanged_result();
  }

  return set_visibility({DocumentVisibilityEdit{
      .entity = entity,
      .before = current_visibility->visible,
      .after = visible,
  }});
}

DocumentCommandResult DocumentCommandService::set_visibility(
    std::vector<DocumentVisibilityEdit> edits) {
  edits = normalized_visibility_edits(scene_, std::move(edits));
  if (edits.empty()) {
    return document_command_unchanged_result();
  }

  std::vector<std::unique_ptr<IUndoableCommand>> commands;
  commands.reserve(edits.size());
  for (const DocumentVisibilityEdit &edit : edits) {
    commands.push_back(std::make_unique<SetVisibilityCommand>(
        scene_, edit.entity, edit.before, edit.after));
  }

  return execute_document_command(
      history_, std::make_unique<BatchDocumentCommand>("Set Visibility",
                                                       std::move(commands)));
}

DocumentCommandResult DocumentCommandService::set_locked(EntityId entity,
                                                         bool locked) {
  const LockComponent *current_lock = scene_.lock(entity);
  if (!current_lock) {
    return document_command_missing_entity_result();
  }
  if (current_lock->locked == locked) {
    return document_command_unchanged_result();
  }

  return set_locks({DocumentLockEdit{
      .entity = entity,
      .before = current_lock->locked,
      .after = locked,
  }});
}

DocumentCommandResult
DocumentCommandService::set_locks(std::vector<DocumentLockEdit> edits) {
  edits = normalized_lock_edits(scene_, std::move(edits));
  if (edits.empty()) {
    return document_command_unchanged_result();
  }

  std::vector<std::unique_ptr<IUndoableCommand>> commands;
  commands.reserve(edits.size() + 1u);
  for (const DocumentLockEdit &edit : edits) {
    commands.push_back(std::make_unique<SetLockCommand>(
        scene_, edit.entity, edit.before, edit.after));
  }
  append_selection_cleanup_after_lock_edits(scene_, edits, commands);

  return execute_document_command(
      history_,
      std::make_unique<BatchDocumentCommand>("Set Lock", std::move(commands)));
}

DocumentCommandResult DocumentCommandService::set_material(EntityId entity,
                                                           AssetId material) {
  if (material == kInvalidAssetId) {
    return document_command_invalid_request_result("Material asset is invalid");
  }
  if (assets_ != nullptr && assets_->material(material) == nullptr) {
    return document_command_invalid_request_result(
        "Material asset does not exist");
  }
  if (!scene_.exists(entity)) {
    return document_command_missing_entity_result();
  }
  if (scene_.is_scene_root(entity)) {
    return document_command_scene_root_protected_result();
  }

  const MaterialComponent after{.material = material};
  std::optional<MaterialComponent> before;
  if (const MaterialComponent *current_material = scene_.material(entity)) {
    if (*current_material == after) {
      return document_command_unchanged_result();
    }
    before = *current_material;
  }

  return execute_document_command(
      history_,
      std::make_unique<SetMaterialCommand>(scene_, entity, before, after));
}

DocumentCommandResult
DocumentCommandService::set_mesh_component(EntityId entity,
                                           MeshComponent mesh) {
  if (mesh.mesh == kInvalidAssetId) {
    return document_command_invalid_request_result("Mesh asset is invalid");
  }
  if (assets_ != nullptr && assets_->mesh(mesh.mesh) == nullptr) {
    return document_command_invalid_request_result("Mesh asset does not exist");
  }
  const MeshComponent *current_mesh = scene_.mesh(entity);
  if (!current_mesh) {
    return document_command_missing_entity_result();
  }
  if (document_command_entity_is_modeling_owned(scene_, entity) &&
      current_mesh->mesh != mesh.mesh) {
    return document_command_modeling_owned_entity_result();
  }
  if (document_command_same_mesh_component(*current_mesh, mesh)) {
    return document_command_unchanged_result();
  }

  return execute_document_command(
      history_, std::make_unique<SetMeshComponentCommand>(scene_, entity,
                                                          *current_mesh, mesh));
}

DocumentCommandResult DocumentCommandService::set_light_component(
    EntityId entity, LightComponent light, UndoRedoExecuteOptions options) {
  const LightComponent *current_light = scene_.light(entity);
  if (!current_light) {
    if (!scene_.exists(entity)) {
      return document_command_missing_entity_result();
    }
    return document_command_invalid_request_result(
        "Entity does not have a light component");
  }
  if (document_command_same_light_component(*current_light, light)) {
    return document_command_unchanged_result();
  }

  return execute_document_command(history_,
                                  std::make_unique<SetLightComponentCommand>(
                                      scene_, entity, *current_light, light),
                                  std::move(options));
}

DocumentCommandResult
DocumentCommandService::set_scene_settings(EntityId entity,
                                           SceneSettingsComponent settings,
                                           UndoRedoExecuteOptions options) {
  const SceneSettingsComponent *current_settings =
      scene_.scene_settings(entity);
  if (!current_settings) {
    if (!scene_.exists(entity)) {
      return document_command_missing_entity_result();
    }
    return document_command_invalid_request_result(
        "Entity does not have scene settings");
  }
  if (document_command_same_scene_settings(*current_settings, settings)) {
    return document_command_unchanged_result();
  }

  return execute_document_command(
      history_,
      std::make_unique<SetSceneSettingsCommand>(
          scene_, entity, *current_settings, std::move(settings)),
      std::move(options));
}

DocumentCommandResult
DocumentCommandService::set_transform(EntityId entity,
                                      TransformComponent transform) {
  const TransformComponent *current_transform = scene_.transform(entity);
  if (!current_transform) {
    return document_command_missing_entity_result();
  }
  if (document_command_same_transform(*current_transform, transform)) {
    return document_command_unchanged_result();
  }

  return set_transforms({DocumentTransformEdit{
      .entity = entity,
      .before = *current_transform,
      .after = transform,
  }});
}

DocumentCommandResult
DocumentCommandService::set_transforms(std::vector<DocumentTransformEdit> edits,
                                       std::string label) {
  if (label.empty()) {
    label = "Set Transform";
  }
  edits = normalized_transform_edits(scene_, std::move(edits));
  if (edits.empty()) {
    return document_command_unchanged_result();
  }

  std::vector<std::unique_ptr<IUndoableCommand>> commands;
  commands.reserve(edits.size());
  for (const DocumentTransformEdit &edit : edits) {
    commands.push_back(std::make_unique<ApplyEntityTransformCommand>(
        scene_, edit.entity, edit.before, edit.after, label));
  }

  return execute_document_command(
      history_, std::make_unique<BatchDocumentCommand>(std::move(label),
                                                       std::move(commands)));
}

DocumentCommandResult DocumentCommandService::apply_scene_batch(
    std::string label, std::vector<DocumentSceneBatchOperation> operations) {
  if (label.empty()) {
    label = "Scene Batch";
  }

  std::vector<std::unique_ptr<IUndoableCommand>> commands;
  commands.reserve(operations.size() + 1u);
  std::vector<DocumentLockEdit> lock_edits;
  std::unordered_map<EntityId, DocumentSceneBatchEntityState> simulated;

  auto failure = [](std::string message) {
    return document_command_invalid_request_result(std::move(message));
  };

  auto simulated_state = [&](EntityId entity)
      -> std::optional<std::reference_wrapper<DocumentSceneBatchEntityState>> {
    if (const auto existing = simulated.find(entity);
        existing != simulated.end()) {
      return existing->second;
    }
    const NameComponent *name = scene_.name(entity);
    const TransformComponent *transform = scene_.transform(entity);
    const VisibilityComponent *visibility = scene_.visibility(entity);
    const LockComponent *lock = scene_.lock(entity);
    if (name == nullptr || transform == nullptr || visibility == nullptr ||
        lock == nullptr) {
      return std::nullopt;
    }
    auto [inserted, _] =
        simulated.emplace(entity, DocumentSceneBatchEntityState{
                                      .name = name->name,
                                      .parent = scene_.parent(entity),
                                      .transform = *transform,
                                      .visible = visibility->visible,
                                      .locked = lock->locked,
                                  });
    return inserted->second;
  };

  auto simulated_parent = [&](EntityId entity) {
    if (std::optional<std::reference_wrapper<DocumentSceneBatchEntityState>>
            state = simulated_state(entity)) {
      return state->get().parent;
    }
    return scene_.parent(entity);
  };

  auto simulated_locked = [&](EntityId entity) {
    if (std::optional<std::reference_wrapper<DocumentSceneBatchEntityState>>
            state = simulated_state(entity)) {
      return state->get().locked;
    }
    const LockComponent *lock = scene_.lock(entity);
    return lock != nullptr && lock->locked;
  };

  auto simulated_effectively_locked = [&](EntityId entity) {
    std::unordered_set<EntityId> visited;
    while (entity != kInvalidEntityId && scene_.exists(entity)) {
      if (!visited.insert(entity).second) {
        return true;
      }
      if (simulated_locked(entity)) {
        return true;
      }
      entity = simulated_parent(entity);
    }
    return false;
  };

  for (const DocumentSceneBatchOperation &operation : operations) {
    if (operation.entity == kInvalidEntityId ||
        !scene_.exists(operation.entity)) {
      return failure("Batch entity does not exist.");
    }

    switch (operation.kind) {
    case DocumentSceneBatchOperationKind::Rename: {
      if (scene_.is_scene_root(operation.entity)) {
        return failure("Scene root cannot be renamed.");
      }
      std::optional<std::reference_wrapper<DocumentSceneBatchEntityState>>
          state = simulated_state(operation.entity);
      if (!state.has_value()) {
        return failure("Batch entity cannot be renamed.");
      }
      if (state->get().name == operation.name) {
        break;
      }
      commands.push_back(std::make_unique<RenameEntityCommand>(
          scene_, operation.entity, state->get().name, operation.name));
      state->get().name = operation.name;
      break;
    }
    case DocumentSceneBatchOperationKind::Reparent: {
      if (scene_.is_scene_root(operation.entity)) {
        return failure("Scene root cannot be reparented.");
      }
      const EntityId parent = normalized_hierarchy_parent(
          scene_, operation.entity, operation.parent);
      if (parent != kInvalidEntityId && !scene_.exists(parent)) {
        return failure("Batch parent entity does not exist.");
      }
      std::optional<std::reference_wrapper<DocumentSceneBatchEntityState>>
          state = simulated_state(operation.entity);
      if (!state.has_value()) {
        return failure("Batch entity cannot be reparented.");
      }
      if (state->get().parent == parent) {
        break;
      }

      commands.push_back(std::make_unique<ReparentEntityCommand>(
          scene_, operation.entity, parent,
          operation.preserve_world_transform));
      state->get().parent = parent;
      break;
    }
    case DocumentSceneBatchOperationKind::SetTransform: {
      std::optional<std::reference_wrapper<DocumentSceneBatchEntityState>>
          state = simulated_state(operation.entity);
      if (!state.has_value()) {
        return failure("Batch entity has no transform.");
      }
      if (simulated_effectively_locked(operation.entity) ||
          document_command_same_transform(state->get().transform,
                                          operation.transform)) {
        break;
      }
      commands.push_back(std::make_unique<ApplyEntityTransformCommand>(
          scene_, operation.entity, state->get().transform, operation.transform,
          label));
      state->get().transform = operation.transform;
      break;
    }
    case DocumentSceneBatchOperationKind::SetVisible: {
      if (scene_.is_scene_root(operation.entity)) {
        return failure("Scene root visibility cannot be changed.");
      }
      std::optional<std::reference_wrapper<DocumentSceneBatchEntityState>>
          state = simulated_state(operation.entity);
      if (!state.has_value()) {
        return failure("Batch entity has no visibility component.");
      }
      if (state->get().visible == operation.flag) {
        break;
      }
      commands.push_back(std::make_unique<SetVisibilityCommand>(
          scene_, operation.entity, state->get().visible, operation.flag));
      state->get().visible = operation.flag;
      break;
    }
    case DocumentSceneBatchOperationKind::SetLocked: {
      if (scene_.is_scene_root(operation.entity)) {
        return failure("Scene root lock cannot be changed.");
      }
      std::optional<std::reference_wrapper<DocumentSceneBatchEntityState>>
          state = simulated_state(operation.entity);
      if (!state.has_value()) {
        return failure("Batch entity has no lock component.");
      }
      if (state->get().locked == operation.flag) {
        break;
      }
      commands.push_back(std::make_unique<SetLockCommand>(
          scene_, operation.entity, state->get().locked, operation.flag));
      lock_edits.push_back(DocumentLockEdit{
          .entity = operation.entity,
          .before = state->get().locked,
          .after = operation.flag,
      });
      state->get().locked = operation.flag;
      break;
    }
    }
  }

  append_selection_cleanup_after_lock_edits(scene_, lock_edits, commands);

  if (commands.empty()) {
    return document_command_unchanged_result();
  }

  return execute_document_command(
      history_, std::make_unique<BatchDocumentCommand>(std::move(label),
                                                       std::move(commands)));
}

} // namespace quader
