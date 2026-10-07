////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/commands/document_command_service.hpp"

#include "core/release_features.hpp"
#include "assets/asset_registry.hpp"
#include "diagnostics/profile.hpp"
#include "editor/commands/document/apply_entity_transform_command.hpp"
#include "editor/commands/document/apply_entity_transforms_command.hpp"
#include "editor/commands/document/batch_document_command.hpp"
#include "editor/commands/document/create_entity_command.hpp"
#include "editor/commands/document/create_light_command.hpp"
#include "editor/commands/document/destroy_entity_command.hpp"
#include "editor/commands/document/duplicate_entity_command.hpp"
#include "editor/commands/document/duplicate_selected_hierarchy_command.hpp"
#include "editor/commands/document/group_selection_command.hpp"
#include "editor/commands/document/move_entities_command.hpp"
#include "editor/commands/document/rename_entity_command.hpp"
#include "editor/commands/document/reparent_entity_command.hpp"
#include "editor/commands/document/select_entities_command.hpp"
#include "editor/commands/document/set_curve_component_command.hpp"
#include "editor/commands/document/set_lock_command.hpp"
#include "editor/commands/document/set_light_component_command.hpp"
#include "editor/commands/document/set_scene_layer_state_command.hpp"
#include "editor/commands/document/set_material_command.hpp"
#include "editor/commands/document/set_mesh_component_command.hpp"
#include "editor/commands/document/set_scene_settings_command.hpp"
#include "editor/commands/document/set_visibility_command.hpp"
#include "editor/commands/document/ungroup_selection_command.hpp"
#include "editor/selection/checked_selection_publication_command.hpp"
#include "scene/scene_document.hpp"
#include "scene/light_properties.hpp"
#include "scene/scene_transform.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace quader {
namespace {

/// Compares preview-owned RGB fields without introducing formatting tolerance.
bool same_preview_color(QVec3 a, QVec3 b) {
  return a.x == b.x && a.y == b.y && a.z == b.z;
}

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

DocumentCreateEntityResult invalid_create_result(std::string message) {
  DocumentCreateEntityResult result;
  assign_document_command_status(
      result, document_command_invalid_request_result(std::move(message)));
  return result;
}

bool document_command_entity_is_modeling_owned(const SceneDocument &scene,
                                               EntityId entity) {
  return scene.modeling_object(entity) != nullptr;
}

bool document_command_hierarchy_contains_modeling_owned_entity(
    const SceneDocument &scene, EntityId root) {
  if (root == kInvalidEntityId || !scene.exists(root)) {
    return false;
  }
  return std::ranges::any_of(scene.descendants(root, true),
                             [&scene](EntityId entity) {
                               return document_command_entity_is_modeling_owned(
                                   scene, entity);
                             });
}

DocumentCommandResult document_command_modeling_owned_entity_result() {
  return document_command_invalid_request_result(
      "Entity is owned by a modeling object; use modeling commands");
}

DocumentCommandResult document_command_active_tool_entity_result() {
  return document_command_invalid_request_result(
      "Apply or cancel the active tool before editing its transient entity");
}

bool document_command_selected_root_contains_modeling_owned_entity(
    const SceneDocument &scene) {
  for (const EntityId root : document_command_selected_root_entities(scene)) {
    if (document_command_hierarchy_contains_modeling_owned_entity(scene,
                                                                  root)) {
      return true;
    }
  }
  return false;
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
        edit.before == edit.after) {
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
        edit.before == edit.after) {
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
      scene, after.selected, after.active,
      SceneSelectionApplicationMode::UndoableCommand));
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

void DocumentCommandService::set_entity_mutation_blocker(
    EntityMutationBlocker blocker) {
  entity_mutation_blocker_ = std::move(blocker);
}

void DocumentCommandService::set_selection_publication(
    CheckedSelectionPublication publication) {
  selection_publication_ = std::move(publication);
}

void DocumentCommandService::set_texture_lock_settings_provider(
    std::function<TextureLockSettings()> provider) {
  texture_lock_settings_provider_ = std::move(provider);
}

TextureLockSettings DocumentCommandService::texture_lock_settings() const {
  return texture_lock_settings_provider_ ? texture_lock_settings_provider_()
                                         : TextureLockSettings{};
}

bool DocumentCommandService::mutation_blocked(EntityId entity) const {
  return entity != kInvalidEntityId && entity_mutation_blocker_ &&
         entity_mutation_blocker_(entity);
}

bool DocumentCommandService::selected_mutation_blocked() const {
  const std::vector<EntityId> selected = scene_.selected_entities();
  return std::any_of(selected.begin(), selected.end(),
                     [this](EntityId entity) {
                       return mutation_blocked(entity);
                     });
}

bool DocumentCommandService::entities_mutation_blocked(
    std::span<const EntityId> entities) const {
  return std::any_of(entities.begin(), entities.end(),
                     [this](EntityId entity) {
                       return mutation_blocked(entity);
                     });
}

DocumentCreateEntityResult
DocumentCommandService::create_entity(std::string name, EntityKind kind,
                                      EntityId parent,
                                      TransformComponent transform,
                                      bool select_created) {
  if (!object_mode_active()) {
    return invalid_create_result("Entity creation requires Object Mode.");
  }
  if ((kind == EntityKind::Light &&
       !release_has_feature(ReleaseFeature::Lights)) ||
      (kind == EntityKind::Curve &&
       !release_has_feature(ReleaseFeature::Curves)) ||
      (kind == EntityKind::Instance &&
       !release_has_feature(ReleaseFeature::Instancing))) {
    return invalid_create_result(release_feature_unavailable_message());
  }

  if (!scene_transform_is_finite(transform)) {
    return invalid_create_result("Entity transform must be finite");
  }
  if (parent != kInvalidEntityId && !scene_.exists(parent)) {
    DocumentCreateEntityResult result;
    assign_document_command_status(result,
                                   document_command_invalid_request_result(
                                       "Parent entity does not exist"));
    return result;
  }
  if (mutation_blocked(parent)) {
    return invalid_create_result(
        "Apply or cancel the active tool before parenting to its transient "
        "entity");
  }
  if (select_created) {
    const DocumentCommandResult accepted = accept_active_property_edit();
    if (!accepted.ok) {
      DocumentCreateEntityResult result;
      assign_document_command_status(result, accepted);
      return result;
    }
  }
  auto command = std::make_unique<CreateEntityCommand>(scene_, std::move(name),
                                                       kind, parent, transform,
                                                       select_created);
  CreateEntityCommand *created_command = command.get();
  DocumentCreateEntityResult result;
  assign_document_command_status(
      result, execute_document_command(
                  history_, with_checked_selection_publication(
                                scene_, std::move(command),
                                select_created
                                    ? selection_publication_
                                    : CheckedSelectionPublication{})));
  if (result.ok && result.changed) {
    result.entity = created_command->entity();
  }
  return result;
}

DocumentCommandResult DocumentCommandService::destroy_entity(EntityId entity) {
  if (!object_mode_active()) {
    return document_command_invalid_request_result("Whole-object editing requires Object Mode.");
  }
  if (!scene_.exists(entity)) {
    return document_command_missing_entity_result();
  }
  if (document_command_hierarchy_contains_modeling_owned_entity(scene_,
                                                                entity)) {
    return document_command_modeling_owned_entity_result();
  }
  const auto accepted_preview = accept_active_property_edit();
  if (!accepted_preview.ok) return accepted_preview;
  auto result = execute_document_command(
      history_, with_checked_selection_publication(
                    scene_, std::make_unique<DestroyEntityCommand>(scene_, entity),
                    selection_publication_));
  result.changed = result.changed || accepted_preview.changed;
  return result;
}

DocumentCommandResult
DocumentCommandService::reparent_entity(EntityId entity, EntityId parent,
                                        bool preserve_world_transform) {
  if (!object_mode_active()) {
    return document_command_invalid_request_result("Whole-object editing requires Object Mode.");
  }
  if (!scene_.exists(entity)) {
    return document_command_missing_entity_result();
  }
  if (parent != kInvalidEntityId && !scene_.exists(parent)) {
    return document_command_invalid_request_result(
        "Parent entity does not exist");
  }
  if (scene_.parent(entity) == parent) {
    return document_command_unchanged_result();
  }
  if (mutation_blocked(entity) || mutation_blocked(parent)) {
    return document_command_active_tool_entity_result();
  }
  return execute_document_command(
      history_, std::make_unique<ReparentEntityCommand>(
                    scene_, entity, parent, preserve_world_transform));
}

DocumentCommandResult DocumentCommandService::move_entities(
    std::span<const EntityId> entities, EntityId parent,
    EntityId before_sibling) {
  return move_entities_impl(entities, parent, before_sibling, true, false,
                            "Move Entities");
}

DocumentCommandResult DocumentCommandService::parent_selection_to_active() {
  if (!object_mode_active()) {
    return document_command_invalid_request_result("Whole-object editing requires Object Mode.");
  }
  const EntityId active = scene_.primary_selection();
  const std::vector<EntityId> selected = scene_.selected_entities();
  if (active == kInvalidEntityId ||
      std::ranges::find(selected, active) == selected.end()) {
    return document_command_invalid_request_result(
        "Select an active parent before parenting entities");
  }
  if (scene_.is_effectively_locked(active)) {
    return document_command_invalid_request_result("Parent entity is locked");
  }
  if (mutation_blocked(active)) {
    return document_command_active_tool_entity_result();
  }
  std::vector<EntityId> children;
  std::ranges::copy_if(selected, std::back_inserter(children),
                       [active](EntityId entity) { return entity != active; });
  return move_entities_impl(children, active, kInvalidEntityId, false, true,
                            "Parent to Active");
}

bool DocumentCommandService::can_parent_selection_to_active() const {
  if (!object_mode_active()) {
    return false;
  }
  const EntityId active = scene_.primary_selection();
  const SelectionComponent *selection = scene_.selection(active);
  if (selection == nullptr || !selection->selected ||
      scene_.is_effectively_locked(active) || mutation_blocked(active) ||
      scene_.instance_occurrence(active) != nullptr) {
    return false;
  }
  bool changes_parent = false;
  for (const EntityId entity : scene_.selected_entities()) {
    if (entity == active) {
      continue;
    }
    if (scene_.is_effectively_locked(entity) || mutation_blocked(entity) ||
        !scene_.can_set_parent(entity, active)) {
      return false;
    }
    changes_parent = changes_parent || scene_.parent(entity) != active;
  }
  return changes_parent;
}

bool DocumentCommandService::can_unparent_selection() const {
  if (!object_mode_active()) {
    return false;
  }
  bool changes_parent = false;
  for (const EntityId entity : scene_.selected_entities()) {
    if (scene_.parent(entity) == kInvalidEntityId) {
      continue;
    }
    if (scene_.is_effectively_locked(entity) || mutation_blocked(entity) ||
        !scene_.can_set_parent(entity, kInvalidEntityId)) {
      return false;
    }
    changes_parent = true;
  }
  return changes_parent;
}

bool DocumentCommandService::can_group_selection() const {
  if (!object_mode_active()) {
    return false;
  }
  const auto roots = document_command_selected_root_entities(scene_);
  return !roots.empty() && !selected_mutation_blocked() &&
         std::ranges::all_of(roots, [this](EntityId entity) {
           return !scene_.is_effectively_locked(entity);
         });
}

bool DocumentCommandService::can_ungroup_selection() const {
  if (!object_mode_active()) {
    return false;
  }
  if (selected_mutation_blocked()) {
    return false;
  }
  bool has_group = false;
  for (const EntityId entity : document_command_selected_root_entities(scene_)) {
    if (scene_.kind(entity) != EntityKind::Empty ||
        !scene_.has_children(entity)) {
      continue;
    }
    if (scene_.is_effectively_locked(entity)) {
      return false;
    }
    has_group = true;
  }
  return has_group;
}

DocumentCommandResult DocumentCommandService::unparent_selection() {
  if (!object_mode_active()) {
    return document_command_invalid_request_result("Whole-object editing requires Object Mode.");
  }
  std::vector<EntityId> children;
  for (const EntityId entity : scene_.selected_entities()) {
    if (scene_.parent(entity) != kInvalidEntityId) {
      children.push_back(entity);
    }
  }
  return move_entities_impl(children, kInvalidEntityId, kInvalidEntityId,
                            false, true, "Unparent");
}

DocumentCommandResult DocumentCommandService::move_entities_impl(
    std::span<const EntityId> entities, EntityId parent,
    EntityId before_sibling, bool retain_subtrees,
    bool leave_direct_children, std::string label) {
  if (!object_mode_active()) {
    return document_command_invalid_request_result("Whole-object editing requires Object Mode.");
  }
  if (entities.empty()) {
    return document_command_unchanged_result();
  }
  if (parent != kInvalidEntityId && !scene_.exists(parent)) {
    return document_command_invalid_request_result(
        "Parent entity does not exist");
  }
  if (scene_.is_effectively_locked(parent)) {
    return document_command_invalid_request_result("Parent entity is locked");
  }
  if (mutation_blocked(parent)) {
    return document_command_active_tool_entity_result();
  }
  if (!document_command_hierarchy_available_in_release(scene_, entities) ||
      (parent != kInvalidEntityId &&
       !document_command_hierarchy_available_in_release(
           scene_, std::span<const EntityId>(&parent, 1u)))) {
    return document_command_invalid_request_result(
        release_feature_unavailable_message());
  }
  if (before_sibling != kInvalidEntityId &&
      (!scene_.exists(before_sibling) ||
       scene_.parent(before_sibling) != parent)) {
    return document_command_invalid_request_result(
        "Insertion sibling does not belong to the destination parent");
  }

  std::unordered_set<EntityId> requested;
  requested.reserve(entities.size());
  for (const EntityId entity : entities) {
    if (!scene_.exists(entity)) {
      return document_command_missing_entity_result();
    }
    if (scene_.is_effectively_locked(entity)) {
      return document_command_invalid_request_result("Moved entity is locked");
    }
    if (entity == parent || scene_.is_descendant_of(parent, entity)) {
      return document_command_invalid_request_result(
          "An entity cannot be moved inside its own hierarchy");
    }
    requested.insert(entity);
  }

  std::vector<EntityId> roots;
  for (const EntityId entity : scene_.entity_tree_order()) {
    if (!requested.contains(entity)) {
      continue;
    }
    bool selected_ancestor = false;
    for (EntityId ancestor = scene_.parent(entity);
         ancestor != kInvalidEntityId; ancestor = scene_.parent(ancestor)) {
      if (requested.contains(ancestor)) {
        selected_ancestor = true;
        break;
      }
    }
    if (!retain_subtrees || !selected_ancestor) {
      const auto descendants = scene_.descendants(entity, true);
      if (std::ranges::any_of(descendants, [this](EntityId descendant) {
            return scene_.is_effectively_locked(descendant);
          })) {
        return document_command_invalid_request_result(
            "Moved hierarchy contains a locked entity");
      }
      if (entities_mutation_blocked(descendants)) {
        return document_command_active_tool_entity_result();
      }
      if (!leave_direct_children || scene_.parent(entity) != parent) {
        roots.push_back(entity);
      }
    }
  }
  if (roots.empty()) {
    return document_command_unchanged_result();
  }
  auto command = std::make_unique<MoveEntitiesCommand>(
      scene_, std::move(roots), parent, before_sibling, std::move(label));
  const DocumentCommandResult preflight = command->preflight();
  if (!preflight.ok || !preflight.changed) {
    return preflight;
  }
  return execute_document_command(history_, std::move(command));
}

DocumentGroupSelectionResult
DocumentCommandService::group_selection(std::string group_name,
                                        TransformComponent group_transform) {
  PreparedDocumentCommand prepared = prepare_group_selection_command(
      std::move(group_name), group_transform);
  if (!prepared.command) {
    DocumentGroupSelectionResult result;
    assign_document_command_status(result, std::move(prepared.status));
    return result;
  }
  const auto accepted = accept_active_property_edit();
  if (!accepted.ok) {
    DocumentGroupSelectionResult result;
    assign_document_command_status(result, accepted);
    return result;
  }
  DocumentGroupSelectionResult result;
  assign_document_command_status(
      result,
      execute_document_command(history_, std::move(prepared.command)));
  result.changed = result.changed || accepted.changed;
  if (result.ok && result.changed) {
    result.group_entity = scene_.primary_selection();
  }
  return result;
}

DocumentCommandResult DocumentCommandService::ungroup_selection() {
  PreparedDocumentCommand prepared = prepare_ungroup_selection_command();
  if (!prepared.command) {
    return std::move(prepared.status);
  }
  const auto accepted = accept_active_property_edit();
  if (!accepted.ok) return accepted;
  auto result = execute_document_command(history_, std::move(prepared.command));
  result.changed = result.changed || accepted.changed;
  return result;
}

PreparedDocumentCommand
DocumentCommandService::prepare_group_selection_command(
    std::string group_name, TransformComponent group_transform) {
  if (!object_mode_active()) {
    return {.status = document_command_invalid_request_result("Whole-object editing requires Object Mode.")};
  }
  if (selected_mutation_blocked()) {
    return {.status = document_command_active_tool_entity_result()};
  }
  auto command = std::make_unique<GroupSelectionCommand>(
      scene_, std::move(group_name), group_transform);
  const auto preflight = command->preflight();
  if (!preflight.ok || !preflight.changed) return {.status = preflight};
  return {
      .status = document_command_unchanged_result(),
      .command = with_checked_selection_publication(
          scene_, std::move(command), selection_publication_),
  };
}

PreparedDocumentCommand
DocumentCommandService::prepare_ungroup_selection_command() {
  if (!object_mode_active()) {
    return {.status = document_command_invalid_request_result("Whole-object editing requires Object Mode.")};
  }
  if (selected_mutation_blocked()) {
    return {.status = document_command_active_tool_entity_result()};
  }
  auto command = std::make_unique<UngroupSelectionCommand>(scene_);
  const auto preflight = command->preflight();
  if (!preflight.ok || !preflight.changed) return {.status = preflight};
  return {
      .status = document_command_unchanged_result(),
      .command = with_checked_selection_publication(
          scene_, std::move(command), selection_publication_),
  };
}

DocumentDuplicateEntityResult
DocumentCommandService::duplicate_entity(EntityId entity) {
  if (!object_mode_active()) {
    DocumentDuplicateEntityResult result;
    assign_document_command_status(result, document_command_invalid_request_result("Whole-object editing requires Object Mode."));
    return result;
  }
  if (!document_command_hierarchy_available_in_release(
          scene_, std::span<const EntityId>(&entity, 1u))) {
    DocumentDuplicateEntityResult result;
    assign_document_command_status(result, document_command_invalid_request_result(
        release_feature_unavailable_message()));
    return result;
  }

  if (!scene_.exists(entity)) {
    DocumentDuplicateEntityResult result;
    assign_document_command_status(result,
                                   document_command_missing_entity_result());
    return result;
  }
  if (document_command_hierarchy_contains_modeling_owned_entity(scene_,
                                                                entity)) {
    DocumentDuplicateEntityResult result;
    assign_document_command_status(
        result, document_command_modeling_owned_entity_result());
    return result;
  }

  const auto accepted = accept_active_property_edit();
  if (!accepted.ok) {
    DocumentDuplicateEntityResult result;
    assign_document_command_status(result, accepted);
    return result;
  }
  auto command = std::make_unique<DuplicateEntityCommand>(scene_, entity);
  DuplicateEntityCommand *duplicate_command = command.get();
  DocumentDuplicateEntityResult result;
  assign_document_command_status(
      result, execute_document_command(
                  history_, with_checked_selection_publication(
                                scene_, std::move(command), selection_publication_)));
  if (result.ok && result.changed) {
    result.duplicate_entity = duplicate_command->duplicate_entity();
  }
  return result;
}

DocumentDuplicateHierarchyResult
DocumentCommandService::duplicate_selected_hierarchy() {
  if (!object_mode_active()) {
    DocumentDuplicateHierarchyResult result;
    assign_document_command_status(result, document_command_invalid_request_result("Whole-object editing requires Object Mode."));
    return result;
  }
  if (!document_command_hierarchy_available_in_release(
          scene_, document_command_selected_root_entities(scene_))) {
    DocumentDuplicateHierarchyResult result;
    assign_document_command_status(result, document_command_invalid_request_result(
        release_feature_unavailable_message()));
    return result;
  }

  QDR_PROFILE_FUNCTION();
  if (document_command_selected_root_contains_modeling_owned_entity(scene_)) {
    DocumentDuplicateHierarchyResult result;
    assign_document_command_status(
        result, document_command_modeling_owned_entity_result());
    return result;
  }

  if (!document_command_selected_root_entities(scene_).empty()) {
    const auto accepted = accept_active_property_edit();
    if (!accepted.ok) {
      DocumentDuplicateHierarchyResult result;
      assign_document_command_status(result, accepted);
      return result;
    }
  }
  auto command = std::make_unique<DuplicateSelectedHierarchyCommand>(scene_);
  DuplicateSelectedHierarchyCommand *duplicate_command = command.get();
  DocumentDuplicateHierarchyResult result;
  {
    QDR_PROFILE_SCOPE("Duplicate execute selected hierarchy command");
    assign_document_command_status(
        result, execute_document_command(
                    history_, with_checked_selection_publication(
                                  scene_, std::move(command), selection_publication_)));
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
  if (!object_mode_active()) {
    DocumentDuplicateHierarchyResult result;
    assign_document_command_status(result, document_command_invalid_request_result("Whole-object editing requires Object Mode."));
    return result;
  }
  if (!document_command_hierarchy_available_in_release(
          scene_, roots)) {
    DocumentDuplicateHierarchyResult result;
    assign_document_command_status(result, document_command_invalid_request_result(
        release_feature_unavailable_message()));
    return result;
  }

  QDR_PROFILE_FUNCTION();
  std::unordered_set<EntityId> seen_roots;
  std::vector<EntityId> existing_roots;
  existing_roots.reserve(roots.size());
  for (const EntityId root : roots) {
    if (root != kInvalidEntityId && scene_.exists(root) &&
        seen_roots.insert(root).second) {
      existing_roots.push_back(root);
    }
  }
  roots = std::move(existing_roots);
  if (roots.empty()) {
    DocumentDuplicateHierarchyResult result;
    assign_document_command_status(result, document_command_unchanged_result());
    return result;
  }
  if (std::any_of(roots.begin(), roots.end(), [this](EntityId entity) {
        return mutation_blocked(entity);
      })) {
    DocumentDuplicateHierarchyResult result;
    assign_document_command_status(
        result, document_command_active_tool_entity_result());
    return result;
  }

  for (const EntityId root : roots) {
    for (const EntityId entity : scene_.descendants(root, true)) {
      if (!document_command_entity_is_modeling_owned(scene_, entity)) {
        continue;
      }
      const auto clone = existing_clones_by_source.find(entity);
      if (clone == existing_clones_by_source.end() ||
          clone->second == kInvalidEntityId ||
          !scene_.exists(clone->second)) {
        DocumentDuplicateHierarchyResult result;
        assign_document_command_status(
            result, document_command_invalid_request_result(
                        "Modeling hierarchy clone mapping is incomplete"));
        return result;
      }
    }
  }

  const auto accepted = accept_active_property_edit();
  if (!accepted.ok) {
    DocumentDuplicateHierarchyResult result;
    assign_document_command_status(result, accepted);
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
      result, execute_document_command(
                  history_, with_checked_selection_publication(
                                scene_, std::move(command), selection_publication_)));
  if (result.ok && result.changed) {
    result.duplicate_roots = duplicate_command->duplicate_roots();
  }
  return result;
}

DocumentCreateEntityResult DocumentCommandService::create_point_light(
    std::string name, LightComponent light, EntityId parent,
    TransformComponent transform, bool select_created) {
  if (!object_mode_active()) {
    return invalid_create_result("Light creation requires Object Mode.");
  }
  if (!release_has_feature(ReleaseFeature::Lights)) {
    return invalid_create_result(release_feature_unavailable_message());
  }

  if (!scene_transform_is_finite(transform)) {
    return invalid_create_result("Light transform must be finite");
  }
  light.type = LightType::Point;
  if (!std::isfinite(light.distance_attenuation) ||
      !light_component_metadata_is_valid(light)) {
    return invalid_create_result("Light properties are invalid");
  }
  if (parent != kInvalidEntityId && !scene_.exists(parent)) {
    DocumentCreateEntityResult result;
    assign_document_command_status(result,
                                   document_command_invalid_request_result(
                                       "Parent entity does not exist"));
    return result;
  }
  if (mutation_blocked(parent)) {
    return invalid_create_result(
        "Apply or cancel the active tool before parenting to its transient "
        "entity");
  }
  if (select_created) {
    const auto accepted = accept_active_property_edit();
    if (!accepted.ok) {
      DocumentCreateEntityResult result;
      assign_document_command_status(result, accepted);
      return result;
    }
  }
  auto command = std::make_unique<CreateLightCommand>(
      scene_, "Create Point Light", std::move(name), light, parent, transform,
      select_created);
  CreateLightCommand *created_command = command.get();
  DocumentCreateEntityResult result;
  {
    QDR_PROFILE_SCOPE("Duplicate execute hierarchy command");
    assign_document_command_status(
        result, execute_document_command(
                    history_, with_checked_selection_publication(
                                  scene_, std::move(command),
                                  select_created
                                      ? selection_publication_
                                      : CheckedSelectionPublication{})));
  }
  if (result.ok && result.changed) {
    result.entity = created_command->entity();
  }
  return result;
}

DocumentCreateEntityResult DocumentCommandService::create_directional_light(
    std::string name, LightComponent light, EntityId parent,
    TransformComponent transform, bool select_created) {
  if (!object_mode_active()) {
    return invalid_create_result("Light creation requires Object Mode.");
  }
  if (!release_has_feature(ReleaseFeature::Lights)) {
    return invalid_create_result(release_feature_unavailable_message());
  }

  if (!scene_transform_is_finite(transform)) {
    return invalid_create_result("Light transform must be finite");
  }
  light.type = LightType::Directional;
  if (!std::isfinite(light.distance_attenuation) ||
      !light_component_metadata_is_valid(light)) {
    return invalid_create_result("Light properties are invalid");
  }
  if (parent != kInvalidEntityId && !scene_.exists(parent)) {
    DocumentCreateEntityResult result;
    assign_document_command_status(result,
                                   document_command_invalid_request_result(
                                       "Parent entity does not exist"));
    return result;
  }
  if (mutation_blocked(parent)) {
    return invalid_create_result(
        "Apply or cancel the active tool before parenting to its transient "
        "entity");
  }
  if (select_created) {
    const auto accepted = accept_active_property_edit();
    if (!accepted.ok) {
      DocumentCreateEntityResult result;
      assign_document_command_status(result, accepted);
      return result;
    }
  }
  auto command = std::make_unique<CreateLightCommand>(
      scene_, "Create Directional Light", std::move(name), light, parent,
      transform, select_created);
  CreateLightCommand *created_command = command.get();
  DocumentCreateEntityResult result;
  assign_document_command_status(
      result, execute_document_command(
                  history_, with_checked_selection_publication(
                                scene_, std::move(command),
                                select_created
                                    ? selection_publication_
                                    : CheckedSelectionPublication{})));
  if (result.ok && result.changed) {
    result.entity = created_command->entity();
  }
  return result;
}

DocumentCommandResult
DocumentCommandService::select_entities(std::vector<EntityId> entities,
                                         EntityId active,
                                         SceneSelectionApplicationMode mode,
                                         CheckedSelectionPublication
                                             publication,
                                         UndoRedoExecuteOptions options,
                                         std::optional<std::uint64_t>
                                             expected_history_revision) {
  if (!publication) {
    publication = selection_publication_;
  }
  DocumentCommandResult accepted = document_command_unchanged_result();
  if (entity_property_edit_active() &&
      !same_scene_selection(capture_scene_selection(scene_),
                            make_scene_selection(scene_, entities, active))) {
    accepted = accept_active_property_edit();
    if (!accepted.ok) return accepted;
  }
  if (expected_history_revision.has_value() &&
      !history_.can_merge_with_latest(options.merge_key,
                                      *expected_history_revision)) {
    options.merge_key.clear();
  }
  const std::string merge_key = options.merge_key;
  auto result = execute_document_command(history_,
                                  with_checked_selection_publication(
                                      scene_,
                                      std::make_unique<SelectEntitiesCommand>(
                                          scene_, std::move(entities), active,
                                          mode, merge_key),
                                      std::move(publication)),
                                  std::move(options));
  result.changed = result.changed || accepted.changed;
  return result;
}

std::uint64_t DocumentCommandService::history_revision() const {
  return history_.revision();
}

DocumentCommandResult
DocumentCommandService::apply_entity_transform(EntityId entity,
                                               TransformComponent transform) {
  if (!object_mode_active()) {
    return document_command_invalid_request_result("Whole-object editing requires Object Mode.");
  }
  if (mutation_blocked(entity)) {
    return document_command_active_tool_entity_result();
  }
  const TransformComponent *current_transform = scene_.transform(entity);
  if (!current_transform) {
    return document_command_missing_entity_result();
  }
  if (document_command_same_transform(*current_transform, transform)) {
    return document_command_unchanged_result();
  }

  return execute_document_command(
      history_, std::make_unique<ApplyEntityTransformCommand>(
                    scene_, entity, *current_transform, transform,
                    "Apply Entity Transform", texture_lock_settings()));
}

DocumentCommandResult DocumentCommandService::rename_entity(EntityId entity,
                                                            std::string name) {
  if (mutation_blocked(entity)) {
    return document_command_active_tool_entity_result();
  }
  const NameComponent *current_name = scene_.name(entity);
  if (!current_name) {
    return document_command_missing_entity_result();
  }
  if (current_name->name == name) {
    return document_command_unchanged_result();
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
  if (std::any_of(edits.begin(), edits.end(), [this](const auto &edit) {
        return mutation_blocked(edit.entity);
      })) {
    return document_command_active_tool_entity_result();
  }

  std::vector<std::unique_ptr<IUndoableCommand>> commands;
  commands.reserve(edits.size());
  for (const DocumentVisibilityEdit &edit : edits) {
    commands.push_back(std::make_unique<SetVisibilityCommand>(
        scene_, edit.entity, edit.before, edit.after));
  }
  if (entity_property_edit_active()) {
    const auto accepted = accept_active_property_edit();
    if (!accepted.ok) return accepted;
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
  if (std::any_of(edits.begin(), edits.end(), [this](const auto &edit) {
        return mutation_blocked(edit.entity);
      })) {
    return document_command_active_tool_entity_result();
  }

  std::vector<std::unique_ptr<IUndoableCommand>> commands;
  commands.reserve(edits.size() + 1u);
  for (const DocumentLockEdit &edit : edits) {
    commands.push_back(std::make_unique<SetLockCommand>(
        scene_, edit.entity, edit.before, edit.after));
  }
  append_selection_cleanup_after_lock_edits(scene_, edits, commands);
  if (entity_property_edit_active()) {
    const auto accepted = accept_active_property_edit();
    if (!accepted.ok) return accepted;
  }
  return execute_document_command(
      history_, with_checked_selection_publication(
                    scene_, std::make_unique<BatchDocumentCommand>(
                                "Set Lock", std::move(commands)),
                    selection_publication_));
}

DocumentCommandResult DocumentCommandService::set_material(EntityId entity,
                                                           AssetId material) {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return document_command_invalid_request_result(
        release_feature_unavailable_message());
  }

  if (mutation_blocked(entity)) {
    return document_command_active_tool_entity_result();
  }
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
  if (!mesh_wireframe_color_is_valid(mesh.wireframe_on_shaded_color)) {
    return document_command_invalid_request_result("Wireframe color must contain finite RGB values from 0 to 1");
  }
  if (mutation_blocked(entity)) {
    return document_command_active_tool_entity_result();
  }
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

DocumentCommandResult DocumentCommandService::set_curve_component(
    EntityId entity, CurveComponent curve, UndoRedoExecuteOptions options) {
  if (!release_has_feature(ReleaseFeature::Curves)) {
    return document_command_invalid_request_result(
        release_feature_unavailable_message());
  }

  if (mutation_blocked(entity)) {
    return document_command_active_tool_entity_result();
  }
  if (!curve_component_is_valid(curve)) {
    return document_command_invalid_request_result(
        "Curve component is invalid");
  }
  if (assets_ != nullptr && assets_->curve(curve.curve) == nullptr) {
    return document_command_invalid_request_result(
        "Curve asset does not exist");
  }
  const CurveComponent *current_curve = scene_.curve(entity);
  if (current_curve == nullptr) {
    if (!scene_.exists(entity)) {
      return document_command_missing_entity_result();
    }
    return document_command_invalid_request_result(
        "Entity does not have a curve component");
  }
  if (scene_.modeling_object(entity) != nullptr &&
      current_curve->curve != curve.curve) {
    return document_command_modeling_owned_entity_result();
  }
  if (document_command_same_curve_component(*current_curve, curve)) {
    return document_command_unchanged_result();
  }

  return execute_document_command(
      history_,
      std::make_unique<SetCurveComponentCommand>(scene_, entity,
                                                 *current_curve, curve),
      std::move(options));
}

DocumentCommandResult DocumentCommandService::set_light_component(
    EntityId entity, LightComponent light, UndoRedoExecuteOptions options) {
  if (!release_has_feature(ReleaseFeature::Lights)) {
    return document_command_invalid_request_result(
        release_feature_unavailable_message());
  }

  if (!std::isfinite(light.distance_attenuation) ||
      !light_component_metadata_is_valid(light)) {
    return document_command_invalid_request_result("Light properties are invalid");
  }
  if (mutation_blocked(entity)) {
    return document_command_active_tool_entity_result();
  }
  const LightComponent *current_light = scene_.light(entity);
  if (!current_light) {
    if (!scene_.exists(entity)) {
      return document_command_missing_entity_result();
    }
    return document_command_invalid_request_result(
        "Entity does not have a light component");
  }
  if (scene_.instance_occurrence(entity) != nullptr ||
      scene_.is_instance_definition_entity(entity) ||
      scene_.is_instance_definition_member(entity) || scene_.curve(entity) != nullptr) {
    return document_command_invalid_request_result("Light component cannot be edited here");
  }
  if (document_command_same_light_component(*current_light, light)) {
    return document_command_unchanged_result();
  }
  DocumentCommandResult finished = document_command_unchanged_result();
  if (property_edit_ && property_edit_->entity == entity) {
    finished = finish_property_edit(false);
    if (!finished.ok) return finished;
  }
  auto result = execute_document_command(history_,
                                  std::make_unique<SetLightComponentCommand>(
                                      scene_, entity, *current_light, light),
                                  std::move(options));
  result.changed = result.changed || finished.changed;
  return result;
}

void DocumentCommandService::advance_property_edit_generation() {
  ++property_edit_generation_;
  if (property_edit_generation_ == 0u) ++property_edit_generation_;
}

bool DocumentCommandService::point_light_range_edit_active() const {
  return property_edit_ &&
      std::holds_alternative<PointLightRangeEdit>(property_edit_->value);
}

DocumentCommandResult
DocumentCommandService::begin_point_light_range_edit(EntityId entity) {
  if (!release_has_feature(ReleaseFeature::Lights)) {
    return document_command_invalid_request_result(
        release_feature_unavailable_message());
  }

  if (point_light_range_edit_active() && property_edit_->entity == entity) {
    return document_command_invalid_request_result("A document property edit is already active");
  }
  const auto *light = scene_.light(entity);
  if (!light || light->type != LightType::Point ||
      !std::isfinite(light->range) || scene_.is_effectively_locked(entity) ||
      mutation_blocked(entity)) {
    return document_command_invalid_request_result("Point light cannot be edited");
  }
  const auto accepted = accept_active_property_edit();
  if (!accepted.ok) return accepted;
  property_edit_ = PropertyEdit{
      .entity = entity, .uuid = scene_.uuid(entity),
      .value = PointLightRangeEdit{light->range, light->range}};
  advance_property_edit_generation();
  if (property_preview_hooks_.begin) property_preview_hooks_.begin();
  return document_command_unchanged_result();
}

DocumentCommandResult DocumentCommandService::update_point_light_range_edit(
    EntityId entity, float range) {
  if (!release_has_feature(ReleaseFeature::Lights)) {
    return document_command_invalid_request_result(
        release_feature_unavailable_message());
  }

  if (!point_light_range_edit_active() || property_edit_->entity != entity ||
      !std::isfinite(range) || range < 0.001f) {
    return document_command_invalid_request_result("Invalid point light range edit");
  }
  auto &edit = std::get<PointLightRangeEdit>(property_edit_->value);
  const auto *current = scene_.light(entity);
  if (!current || current->type != LightType::Point ||
      scene_.uuid(entity) != property_edit_->uuid || current->range != edit.expected ||
      scene_.is_effectively_locked(entity) || mutation_blocked(entity)) {
    return document_command_invalid_request_result("Point light changed during range edit");
  }
  if (range == edit.expected) return document_command_unchanged_result();
  LightComponent light = *current;
  light.range = range;
  if (property_preview_hooks_.observe_before_projection)
    property_preview_hooks_.observe_before_projection();
  if (!scene_.set_light(entity, light)) {
    return document_command_invalid_request_result("Point light range could not be applied");
  }
  edit.expected = range;
  if (property_preview_hooks_.record_after_projection)
    property_preview_hooks_.record_after_projection();
  return document_command_changed_result();
}

DocumentCommandResult DocumentCommandService::finish_point_light_range_edit(
    EntityId entity, bool cancel) {
  if (!release_has_feature(ReleaseFeature::Lights)) {
    return document_command_invalid_request_result(
        release_feature_unavailable_message());
  }

  return point_light_range_edit_active() && property_edit_->entity == entity
      ? finish_property_edit(cancel) : document_command_unchanged_result();
}

DocumentCommandResult DocumentCommandService::begin_light_color_edit(EntityId entity) {
  if (!release_has_feature(ReleaseFeature::Lights)) {
    return document_command_invalid_request_result(
        release_feature_unavailable_message());
  }

  if (property_edit_ && property_edit_->entity == entity &&
      std::holds_alternative<LightColorEdit>(property_edit_->value)) {
    return document_command_invalid_request_result("A document property edit is already active");
  }
  const auto *light = scene_.light(entity);
  if (!light || scene_.is_effectively_locked(entity) || mutation_blocked(entity)) {
    return document_command_invalid_request_result("Light color cannot be edited");
  }
  const auto accepted = accept_active_property_edit();
  if (!accepted.ok) return accepted;
  const LightColorValue color{light->color, light->color_mode, light->source_preset};
  property_edit_ = PropertyEdit{.entity = entity, .uuid = scene_.uuid(entity),
                               .value = LightColorEdit{color, color}};
  advance_property_edit_generation();
  if (property_preview_hooks_.begin) property_preview_hooks_.begin();
  return document_command_unchanged_result();
}

DocumentCommandResult DocumentCommandService::update_light_color_edit(
    EntityId entity, std::uint64_t generation, QVec3 color) {
  if (!release_has_feature(ReleaseFeature::Lights)) {
    return document_command_invalid_request_result(
        release_feature_unavailable_message());
  }

  if (generation == 0u || generation != property_edit_generation_ ||
      !property_edit_ || property_edit_->entity != entity ||
      !std::holds_alternative<LightColorEdit>(property_edit_->value) ||
      !std::isfinite(color.x) || !std::isfinite(color.y) || !std::isfinite(color.z) ||
      color.x < 0.0f || color.y < 0.0f || color.z < 0.0f) {
    return document_command_invalid_request_result("Invalid light color edit");
  }
  auto &edit = std::get<LightColorEdit>(property_edit_->value);
  const auto *current = scene_.light(entity);
  if (!current || scene_.uuid(entity) != property_edit_->uuid ||
      !same_preview_color(current->color, edit.expected.color) || current->color_mode != edit.expected.mode ||
      current->source_preset != edit.expected.preset || scene_.is_effectively_locked(entity) ||
      mutation_blocked(entity)) {
    return document_command_invalid_request_result("Light changed during color edit");
  }
  if (same_preview_color(color, edit.expected.color)) return document_command_unchanged_result();
  LightComponent light = *current;
  light.color = color;
  light.color_mode = LightColorMode::Color;
  light.source_preset = LightSourcePreset::Custom;
  if (property_preview_hooks_.observe_before_projection)
    property_preview_hooks_.observe_before_projection();
  if (!scene_.set_light(entity, light)) {
    return document_command_invalid_request_result("Light color could not be applied");
  }
  edit.expected = {color, light.color_mode, light.source_preset};
  if (property_preview_hooks_.record_after_projection)
    property_preview_hooks_.record_after_projection();
  return document_command_changed_result();
}

DocumentCommandResult DocumentCommandService::finish_light_color_edit(
    EntityId entity, std::uint64_t generation, bool cancel) {
  if (!release_has_feature(ReleaseFeature::Lights)) {
    return document_command_invalid_request_result(
        release_feature_unavailable_message());
  }

  if (generation == 0u || generation != property_edit_generation_ ||
      (property_edit_ && (property_edit_->entity != entity ||
                         !std::holds_alternative<LightColorEdit>(property_edit_->value)))) {
    return document_command_invalid_request_result("Color edit is no longer active");
  }
  return finish_property_edit(cancel);
}

DocumentCommandResult DocumentCommandService::begin_environment_color_edit() {
  if (!release_has_feature(ReleaseFeature::Environment)) {
    return document_command_invalid_request_result(
        release_feature_unavailable_message());
  }

  if (property_edit_ && std::holds_alternative<EnvironmentColorEdit>(property_edit_->value)) {
    return document_command_invalid_request_result("A document property edit is already active");
  }
  const auto accepted = accept_active_property_edit();
  if (!accepted.ok) return accepted;
  const auto &settings = scene_.scene_settings();
  const QVec3 color = settings.environment.clear_color;
  property_edit_ = PropertyEdit{.value = EnvironmentColorEdit{color, color}};
  advance_property_edit_generation();
  if (property_preview_hooks_.begin) property_preview_hooks_.begin();
  return document_command_unchanged_result();
}

DocumentCommandResult DocumentCommandService::update_environment_color_edit(
    std::uint64_t generation, QVec3 color) {
  if (!release_has_feature(ReleaseFeature::Environment)) {
    return document_command_invalid_request_result(
        release_feature_unavailable_message());
  }

  if (generation == 0u || generation != property_edit_generation_ ||
      !property_edit_ ||
      !std::holds_alternative<EnvironmentColorEdit>(property_edit_->value) ||
      !std::isfinite(color.x) || !std::isfinite(color.y) || !std::isfinite(color.z) ||
      color.x < 0.0f || color.y < 0.0f || color.z < 0.0f) {
    return document_command_invalid_request_result("Invalid environment color edit");
  }
  auto &edit = std::get<EnvironmentColorEdit>(property_edit_->value);
  const auto &current = scene_.scene_settings();
  if (!same_preview_color(current.environment.clear_color, edit.expected)) {
    return document_command_invalid_request_result("Environment changed during color edit");
  }
  if (same_preview_color(color, edit.expected)) return document_command_unchanged_result();
  SceneSettings settings = current;
  settings.environment.clear_color = color;
  if (property_preview_hooks_.observe_before_projection)
    property_preview_hooks_.observe_before_projection();
  if (!scene_.set_scene_settings(settings)) {
    return document_command_invalid_request_result("Environment color could not be applied");
  }
  edit.expected = color;
  if (property_preview_hooks_.record_after_projection)
    property_preview_hooks_.record_after_projection();
  return document_command_changed_result();
}

DocumentCommandResult DocumentCommandService::finish_environment_color_edit(
    std::uint64_t generation, bool cancel) {
  if (!release_has_feature(ReleaseFeature::Environment)) {
    return document_command_invalid_request_result(
        release_feature_unavailable_message());
  }

  if (generation == 0u || generation != property_edit_generation_ ||
      (property_edit_ && !std::holds_alternative<EnvironmentColorEdit>(property_edit_->value))) {
    return document_command_invalid_request_result("Color edit is no longer active");
  }
  return finish_property_edit(cancel);
}

DocumentCommandResult DocumentCommandService::begin_fog_color_edit() {
  if (!release_has_feature(ReleaseFeature::Environment)) {
    return document_command_invalid_request_result(
        release_feature_unavailable_message());
  }

  if (property_edit_ && std::holds_alternative<FogColorEdit>(property_edit_->value)) {
    return document_command_invalid_request_result("A document property edit is already active");
  }
  const auto accepted = accept_active_property_edit();
  if (!accepted.ok) return accepted;
  const auto &settings = scene_.scene_settings();
  const QVec3 color = settings.fog.color;
  property_edit_ = PropertyEdit{.value = FogColorEdit{color, color}};
  advance_property_edit_generation();
  if (property_preview_hooks_.begin) property_preview_hooks_.begin();
  return document_command_unchanged_result();
}

DocumentCommandResult DocumentCommandService::update_fog_color_edit(
    std::uint64_t generation, QVec3 color) {
  if (!release_has_feature(ReleaseFeature::Environment)) {
    return document_command_invalid_request_result(
        release_feature_unavailable_message());
  }

  if (generation == 0u || generation != property_edit_generation_ ||
      !property_edit_ ||
      !std::holds_alternative<FogColorEdit>(property_edit_->value) ||
      !std::isfinite(color.x) || !std::isfinite(color.y) || !std::isfinite(color.z) ||
      color.x < 0.0f || color.y < 0.0f || color.z < 0.0f) {
    return document_command_invalid_request_result("Invalid fog color edit");
  }
  auto &edit = std::get<FogColorEdit>(property_edit_->value);
  const auto &current = scene_.scene_settings();
  if (!same_preview_color(current.fog.color, edit.expected)) {
    return document_command_invalid_request_result("Fog changed during color edit");
  }
  if (same_preview_color(color, edit.expected)) return document_command_unchanged_result();
  SceneSettings settings = current;
  settings.fog.color = color;
  if (property_preview_hooks_.observe_before_projection)
    property_preview_hooks_.observe_before_projection();
  if (!scene_.set_scene_settings(settings)) {
    return document_command_invalid_request_result("Fog color could not be applied");
  }
  edit.expected = color;
  if (property_preview_hooks_.record_after_projection)
    property_preview_hooks_.record_after_projection();
  return document_command_changed_result();
}

DocumentCommandResult DocumentCommandService::finish_fog_color_edit(
    std::uint64_t generation, bool cancel) {
  if (!release_has_feature(ReleaseFeature::Environment)) {
    return document_command_invalid_request_result(
        release_feature_unavailable_message());
  }

  if (generation == 0u || generation != property_edit_generation_ ||
      (property_edit_ && !std::holds_alternative<FogColorEdit>(property_edit_->value))) {
    return document_command_invalid_request_result("Color edit is no longer active");
  }
  return finish_property_edit(cancel);
}

DocumentCommandResult DocumentCommandService::settle_property_edit(
    DocumentCommandResult result, bool restored) {
  if (!result.ok) return result;
  property_edit_.reset();
  if (property_preview_hooks_.record_after_projection)
    property_preview_hooks_.record_after_projection();
  if (property_preview_hooks_.finish) property_preview_hooks_.finish(restored);
  return result;
}

DocumentCommandResult DocumentCommandService::finish_property_edit(bool cancel) {
  if (!property_edit_) return document_command_unchanged_result();
  const PropertyEdit edit = *property_edit_;
  const auto stale_preview = [this, cancel](const char *message) {
    // Explicit cancellation retires invalidated ownership without overwriting
    // replacement data. Acceptance keeps the draft available for recovery.
    return cancel
        ? settle_property_edit(document_command_changed_result(), false)
        : document_command_invalid_request_result(message);
  };
  if (property_preview_hooks_.observe_before_projection)
    property_preview_hooks_.observe_before_projection();
  const auto *environment_color = std::get_if<EnvironmentColorEdit>(&edit.value);
  const auto *fog_color = std::get_if<FogColorEdit>(&edit.value);
  if (environment_color || fog_color) {
    const QVec3 initial = fog_color ? fog_color->before : environment_color->before;
    const QVec3 expected = fog_color ? fog_color->expected : environment_color->expected;
    const auto &current = scene_.scene_settings();
    if (!same_preview_color(
            fog_color ? current.fog.color : current.environment.clear_color, expected)) {
      return stale_preview(fog_color ? "Fog changed during color edit"
                                     : "Environment changed during color edit");
    }
    if (same_preview_color(initial, expected))
      return settle_property_edit(document_command_unchanged_result(), true);
    SceneSettings before = current;
    if (fog_color) before.fog.color = initial;
    else before.environment.clear_color = initial;
    if (cancel) {
      const bool restored = scene_.set_scene_settings(before);
      return settle_property_edit(restored ? document_command_changed_result()
          : document_command_invalid_request_result(
                fog_color ? "Fog color could not be restored"
                          : "Environment color could not be restored"), restored);
    }
    return settle_property_edit(history_.record_executed(
        std::make_unique<SetSceneSettingsCommand>(scene_, before, current)), false);
  }
  if (!scene_.exists(edit.entity) || scene_.uuid(edit.entity) != edit.uuid) {
    return stale_preview("Entity changed during property edit");
  }
  if (!cancel && scene_.is_effectively_locked(edit.entity)) {
    return document_command_invalid_request_result(
        "Unlock the entity before accepting its property edit");
  }
  const auto *current = scene_.light(edit.entity);
  if (!current) {
    return stale_preview("Light changed during property edit");
  }
  LightComponent before = *current;
  if (const auto *range = std::get_if<PointLightRangeEdit>(&edit.value)) {
    if (current->type != LightType::Point || current->range != range->expected) {
      return stale_preview("Point light changed during range edit");
    }
    before.range = range->before;
  } else {
    const auto &color = std::get<LightColorEdit>(edit.value);
    if (!same_preview_color(current->color, color.expected.color) || current->color_mode != color.expected.mode ||
        current->source_preset != color.expected.preset) {
      return stale_preview("Light changed during color edit");
    }
    before.color = color.before.color;
    before.color_mode = color.before.mode;
    before.source_preset = color.before.preset;
  }
  if (document_command_same_light_component(before, *current))
    return settle_property_edit(document_command_unchanged_result(), true);
  if (cancel) {
    const bool restored = scene_.set_light(edit.entity, before);
    return settle_property_edit(restored ? document_command_changed_result()
        : document_command_invalid_request_result("Light property could not be restored"), restored);
  }
  return settle_property_edit(history_.record_executed(
      std::make_unique<SetLightComponentCommand>(scene_, edit.entity, before, *current)), false);
}

DocumentCommandResult DocumentCommandService::accept_active_property_edit() {
  return finish_property_edit(false);
}

DocumentCommandResult DocumentCommandService::cancel_active_property_edit() {
  return finish_property_edit(true);
}

DocumentCommandResult DocumentCommandService::cancel_active_point_light_range_edit() {
  return point_light_range_edit_active() ? finish_property_edit(true)
                                        : document_command_unchanged_result();
}

DocumentCommandResult
DocumentCommandService::set_scene_settings(SceneSettings settings,
                                           UndoRedoExecuteOptions options) {
  if (!release_has_feature(ReleaseFeature::Environment)) {
    SceneSettings permitted = scene_.scene_settings();
    permitted.camera.near_plane = settings.camera.near_plane;
    permitted.camera.far_plane = settings.camera.far_plane;
    if (settings != permitted) {
      return document_command_invalid_request_result(
          release_feature_unavailable_message());
    }
  }

  if (!scene_fog_settings_is_valid(settings.fog)) {
    return document_command_invalid_request_result("Fog settings contain invalid values");
  }
  const SceneSettings current_settings = scene_.scene_settings();
  if (document_command_same_scene_settings(current_settings, settings)) {
    return document_command_unchanged_result();
  }

  DocumentCommandResult finished = document_command_unchanged_result();
  if (property_edit_ && !entity_property_edit_active()) {
    finished = finish_property_edit(false);
    if (!finished.ok) return finished;
  }
  auto result = execute_document_command(
      history_,
      std::make_unique<SetSceneSettingsCommand>(
          scene_, current_settings, std::move(settings)),
      std::move(options));
  result.changed = result.changed || finished.changed;
  return result;
}

DocumentCommandResult
DocumentCommandService::set_transform(EntityId entity,
                                      TransformComponent transform) {
  if (!object_mode_active()) {
    return document_command_invalid_request_result("Whole-object editing requires Object Mode.");
  }
  if (!scene_transform_is_finite(transform)) {
    return document_command_invalid_request_result(
        "Transform contains non-finite values");
  }
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
                                       std::string label,
                                       SceneTextureTransformKind kind) {
  if (!object_mode_active()) {
    return document_command_invalid_request_result("Whole-object editing requires Object Mode.");
  }
  if (label.empty()) {
    label = "Set Transform";
  }
  if (std::any_of(edits.begin(), edits.end(), [](const auto &edit) {
        return !scene_transform_is_finite(edit.before) ||
               !scene_transform_is_finite(edit.after);
      })) {
    return document_command_invalid_request_result(
        "Transform contains non-finite values");
  }
  edits = normalized_transform_edits(scene_, std::move(edits));
  if (edits.empty()) {
    return document_command_unchanged_result();
  }
  if (std::any_of(edits.begin(), edits.end(), [this](const auto &edit) {
        return mutation_blocked(edit.entity);
      })) {
    return document_command_active_tool_entity_result();
  }

  std::vector<EntityId> targets;
  targets.reserve(edits.size());
  for (const DocumentTransformEdit &edit : edits) {
    targets.push_back(edit.entity);
  }
  const std::optional<SceneTextureMappingPlan> plan =
      SceneTextureMappingPlan::capture(scene_, targets, texture_lock_settings());
  if (!plan.has_value()) {
    return document_command_invalid_request_result(
        "Texture mapping cannot be captured for this transform hierarchy");
  }
  auto mappings = plan->evaluate(edits, kind);
  if (!mappings.has_value()) {
    return document_command_invalid_request_result(
        "Transform would produce an invalid texture mapping");
  }

  return execute_document_command(
      history_, std::make_unique<ApplyEntityTransformsCommand>(
                    scene_, std::move(edits), std::move(label), std::move(*mappings)));
}

DocumentCreateLayerResult DocumentCommandService::create_layer(
    std::string name, QVec3 wireframe_color, bool add_selection) {
  if (!release_has_feature(ReleaseFeature::Layers)) {
    DocumentCreateLayerResult result;
    assign_document_command_status(result, document_command_invalid_request_result(
        release_feature_unavailable_message()));
    return result;
  }

  DocumentCreateLayerResult result;
  const std::vector<EntityId> eligible = add_selection
      ? scene_.layer_eligible_entities(scene_.selected_entities())
      : std::vector<EntityId>{};
  if (add_selection && eligible.empty()) {
    assign_document_command_status(
        result, document_command_invalid_request_result(
                    "No layer-eligible scene entities are selected"));
    return result;
  }
  if (entities_mutation_blocked(eligible)) {
    assign_document_command_status(result,
                                   document_command_active_tool_entity_result());
    return result;
  }
  if (name.empty()) {
    name = scene_.next_layer_name();
  }

  const SceneLayerId layer_id = scene_.next_layer_id();
  SceneLayerState before = scene_.layer_state();
  SceneLayerState after = before;
  after.layers.push_back(SceneLayerRecord{
      .id = layer_id,
      .name = std::move(name),
      .visible = true,
      .wireframe_color = wireframe_color,
      .locked = false,
  });
  if (add_selection) {
    const std::unordered_set<EntityId> assigned(eligible.begin(),
                                                eligible.end());
    std::erase_if(after.assignments,
                  [&assigned](const SceneLayerAssignment &entry) {
                    return assigned.contains(entry.entity);
                  });
    for (const EntityId entity : eligible) {
      after.assignments.push_back(
          SceneLayerAssignment{.entity = entity, .layer_id = layer_id});
    }
  }

  assign_document_command_status(
      result, execute_document_command(
                  history_, with_checked_selection_publication(
                                scene_, std::make_unique<SetSceneLayerStateCommand>(
                                    scene_, add_selection
                                                ? "Create Layer From Selection"
                                                : "Create Layer",
                                    std::move(before), std::move(after)),
                                selection_publication_)));
  if (result.ok && result.changed) {
    result.layer_id = layer_id;
  }
  return result;
}

DocumentCommandResult DocumentCommandService::rename_layer(
    SceneLayerId layer_id, std::string name) {
  if (!release_has_feature(ReleaseFeature::Layers)) {
    return document_command_invalid_request_result(
        release_feature_unavailable_message());
  }

  const SceneLayerRecord *current = scene_.layer(layer_id);
  if (current == nullptr) {
    return document_command_invalid_request_result("Layer does not exist");
  }
  if (current->name == name) {
    return document_command_unchanged_result();
  }
  SceneLayerState before = scene_.layer_state();
  SceneLayerState after = before;
  for (SceneLayerRecord &record : after.layers) {
    if (record.id == layer_id) {
      record.name = std::move(name);
      break;
    }
  }
  return execute_document_command(
      history_, with_checked_selection_publication(
                    scene_, std::make_unique<SetSceneLayerStateCommand>(
                        scene_, "Rename Layer", std::move(before),
                        std::move(after)),
                    selection_publication_));
}

DocumentCommandResult DocumentCommandService::set_layer_visible(
    SceneLayerId layer_id, bool visible) {
  if (!release_has_feature(ReleaseFeature::Layers)) {
    return document_command_invalid_request_result(
        release_feature_unavailable_message());
  }

  const SceneLayerRecord *current = scene_.layer(layer_id);
  if (current == nullptr) {
    return document_command_invalid_request_result("Layer does not exist");
  }
  if (current->visible == visible) {
    return document_command_unchanged_result();
  }
  const std::vector<EntityId> members = scene_.layer_members(layer_id);
  if (entities_mutation_blocked(members)) {
    return document_command_active_tool_entity_result();
  }
  SceneLayerState before = scene_.layer_state();
  SceneLayerState after = before;
  for (SceneLayerRecord &record : after.layers) {
    if (record.id == layer_id) {
      record.visible = visible;
      break;
    }
  }
  return execute_document_command(
      history_, with_checked_selection_publication(
                    scene_, std::make_unique<SetSceneLayerStateCommand>(
                        scene_, "Set Layer Visibility", std::move(before),
                        std::move(after)),
                    selection_publication_));
}

DocumentCommandResult DocumentCommandService::set_layer_wireframe_color(
    SceneLayerId layer_id, QVec3 color) {
  if (!release_has_feature(ReleaseFeature::Layers)) {
    return document_command_invalid_request_result(
        release_feature_unavailable_message());
  }

  const SceneLayerRecord *current = scene_.layer(layer_id);
  if (current == nullptr) {
    return document_command_invalid_request_result("Layer does not exist");
  }
  if (current->wireframe_color.x == color.x &&
      current->wireframe_color.y == color.y &&
      current->wireframe_color.z == color.z) {
    return document_command_unchanged_result();
  }
  const std::vector<EntityId> members = scene_.layer_members(layer_id);
  if (entities_mutation_blocked(members)) {
    return document_command_active_tool_entity_result();
  }
  SceneLayerState before = scene_.layer_state();
  SceneLayerState after = before;
  for (SceneLayerRecord &record : after.layers) {
    if (record.id == layer_id) {
      record.wireframe_color = color;
      break;
    }
  }
  return execute_document_command(
      history_, with_checked_selection_publication(
                    scene_, std::make_unique<SetSceneLayerStateCommand>(
                        scene_, "Set Layer Color", std::move(before),
                        std::move(after)),
                    selection_publication_));
}

DocumentCommandResult DocumentCommandService::set_layer_locked(
    SceneLayerId layer_id, bool locked) {
  if (!release_has_feature(ReleaseFeature::Layers)) {
    return document_command_invalid_request_result(
        release_feature_unavailable_message());
  }

  const SceneLayerRecord *current = scene_.layer(layer_id);
  if (current == nullptr) {
    return document_command_invalid_request_result("Layer does not exist");
  }
  if (current->locked == locked) {
    return document_command_unchanged_result();
  }
  const std::vector<EntityId> members = scene_.layer_members(layer_id);
  if (entities_mutation_blocked(members)) {
    return document_command_active_tool_entity_result();
  }
  SceneLayerState before = scene_.layer_state();
  SceneLayerState after = before;
  for (SceneLayerRecord &record : after.layers) {
    if (record.id == layer_id) {
      record.locked = locked;
      break;
    }
  }
  return execute_document_command(
      history_, with_checked_selection_publication(
                    scene_, std::make_unique<SetSceneLayerStateCommand>(
                        scene_, "Set Layer Lock", std::move(before),
                        std::move(after)),
                    selection_publication_));
}

DocumentCommandResult DocumentCommandService::add_selection_to_layer(
    SceneLayerId layer_id) {
  if (!release_has_feature(ReleaseFeature::Layers)) {
    return document_command_invalid_request_result(
        release_feature_unavailable_message());
  }

  if (scene_.layer(layer_id) == nullptr) {
    return document_command_invalid_request_result("Layer does not exist");
  }
  const std::vector<EntityId> eligible =
      scene_.layer_eligible_entities(scene_.selected_entities());
  if (eligible.empty()) {
    return document_command_invalid_request_result(
        "No layer-eligible scene entities are selected");
  }
  if (entities_mutation_blocked(eligible)) {
    return document_command_active_tool_entity_result();
  }
  SceneLayerState before = scene_.layer_state();
  SceneLayerState after = before;
  const std::unordered_set<EntityId> assigned(eligible.begin(), eligible.end());
  std::erase_if(after.assignments,
                [&assigned](const SceneLayerAssignment &entry) {
                  return assigned.contains(entry.entity);
                });
  for (const EntityId entity : eligible) {
    after.assignments.push_back(
        SceneLayerAssignment{.entity = entity, .layer_id = layer_id});
  }
  if (before == after) {
    return document_command_unchanged_result();
  }
  return execute_document_command(
      history_, with_checked_selection_publication(
                    scene_, std::make_unique<SetSceneLayerStateCommand>(
                        scene_, "Add Selection to Layer", std::move(before),
                        std::move(after)),
                    selection_publication_));
}

DocumentCommandResult DocumentCommandService::remove_selection_from_layer(
    SceneLayerId layer_id) {
  if (!release_has_feature(ReleaseFeature::Layers)) {
    return document_command_invalid_request_result(
        release_feature_unavailable_message());
  }

  if (scene_.layer(layer_id) == nullptr) {
    return document_command_invalid_request_result("Layer does not exist");
  }
  const std::vector<EntityId> eligible =
      scene_.layer_eligible_entities(scene_.selected_entities());
  if (entities_mutation_blocked(eligible)) {
    return document_command_active_tool_entity_result();
  }
  const std::unordered_set<EntityId> removed(eligible.begin(), eligible.end());
  SceneLayerState before = scene_.layer_state();
  SceneLayerState after = before;
  std::erase_if(after.assignments,
                [&removed, layer_id](const SceneLayerAssignment &entry) {
                  return entry.layer_id == layer_id &&
                         removed.contains(entry.entity);
                });
  if (before == after) {
    return document_command_unchanged_result();
  }
  return execute_document_command(
      history_, with_checked_selection_publication(
                    scene_, std::make_unique<SetSceneLayerStateCommand>(
                        scene_, "Remove Selection From Layer",
                        std::move(before), std::move(after)),
                    selection_publication_));
}

DocumentCommandResult
DocumentCommandService::remove_selection_from_all_layers() {
  if (!release_has_feature(ReleaseFeature::Layers)) {
    return document_command_invalid_request_result(
        release_feature_unavailable_message());
  }

  const std::vector<EntityId> eligible =
      scene_.layer_eligible_entities(scene_.selected_entities());
  if (entities_mutation_blocked(eligible)) {
    return document_command_active_tool_entity_result();
  }
  const std::unordered_set<EntityId> removed(eligible.begin(), eligible.end());
  SceneLayerState before = scene_.layer_state();
  SceneLayerState after = before;
  std::erase_if(after.assignments,
                [&removed](const SceneLayerAssignment &entry) {
                  return removed.contains(entry.entity);
                });
  if (before == after) {
    return document_command_unchanged_result();
  }
  return execute_document_command(
      history_, with_checked_selection_publication(
                    scene_, std::make_unique<SetSceneLayerStateCommand>(
                        scene_, "Remove Selection From Layers",
                        std::move(before), std::move(after)),
                    selection_publication_));
}

DocumentCommandResult
DocumentCommandService::empty_layer(SceneLayerId layer_id) {
  if (!release_has_feature(ReleaseFeature::Layers)) {
    return document_command_invalid_request_result(
        release_feature_unavailable_message());
  }

  if (scene_.layer(layer_id) == nullptr) {
    return document_command_invalid_request_result("Layer does not exist");
  }
  const std::vector<EntityId> members = scene_.layer_members(layer_id);
  if (entities_mutation_blocked(members)) {
    return document_command_active_tool_entity_result();
  }
  SceneLayerState before = scene_.layer_state();
  SceneLayerState after = before;
  std::erase_if(after.assignments,
                [layer_id](const SceneLayerAssignment &entry) {
                  return entry.layer_id == layer_id;
                });
  if (before == after) {
    return document_command_unchanged_result();
  }
  return execute_document_command(
      history_, with_checked_selection_publication(
                    scene_, std::make_unique<SetSceneLayerStateCommand>(
                        scene_, "Empty Layer", std::move(before),
                        std::move(after)),
                    selection_publication_));
}

DocumentCommandResult
DocumentCommandService::delete_layers(std::vector<SceneLayerId> layer_ids) {
  if (!release_has_feature(ReleaseFeature::Layers)) {
    return document_command_invalid_request_result(
        release_feature_unavailable_message());
  }

  std::sort(layer_ids.begin(), layer_ids.end());
  layer_ids.erase(std::unique(layer_ids.begin(), layer_ids.end()),
                  layer_ids.end());
  if (layer_ids.empty()) {
    return document_command_unchanged_result();
  }
  std::unordered_set<SceneLayerId> removed;
  removed.reserve(layer_ids.size());
  for (const SceneLayerId layer_id : layer_ids) {
    if (scene_.layer(layer_id) == nullptr) {
      return document_command_invalid_request_result("Layer does not exist");
    }
    removed.insert(layer_id);
  }
  SceneLayerState before = scene_.layer_state();
  std::vector<EntityId> affected;
  affected.reserve(before.assignments.size());
  for (const SceneLayerAssignment &assignment : before.assignments) {
    if (removed.contains(assignment.layer_id)) {
      affected.push_back(assignment.entity);
    }
  }
  if (entities_mutation_blocked(affected)) {
    return document_command_active_tool_entity_result();
  }
  SceneLayerState after = before;
  std::erase_if(after.layers, [&removed](const SceneLayerRecord &record) {
    return removed.contains(record.id);
  });
  std::erase_if(after.assignments,
                [&removed](const SceneLayerAssignment &entry) {
                  return removed.contains(entry.layer_id);
                });
  return execute_document_command(
      history_, with_checked_selection_publication(
                    scene_, std::make_unique<SetSceneLayerStateCommand>(
                        scene_, layer_ids.size() == 1u ? "Delete Layer"
                                                      : "Delete Layers",
                        std::move(before), std::move(after)),
                    selection_publication_));
}

DocumentCommandResult DocumentCommandService::apply_scene_batch(
    std::string label, std::vector<DocumentSceneBatchOperation> operations) {
  if (!object_mode_active() && std::ranges::any_of(operations, [](const auto &operation) {
        return operation.kind == DocumentSceneBatchOperationKind::Reparent ||
               operation.kind == DocumentSceneBatchOperationKind::SetTransform;
      })) {
    return document_command_invalid_request_result("Whole-object editing requires Object Mode.");
  }
  if (label.empty()) {
    label = "Scene Batch";
  }
  if (std::any_of(operations.begin(), operations.end(),
                  [this](const auto &operation) {
                    return mutation_blocked(operation.entity) ||
                           mutation_blocked(operation.parent);
                  })) {
    return document_command_active_tool_entity_result();
  }

  std::vector<std::unique_ptr<IUndoableCommand>> commands;
  commands.reserve(operations.size() + 1u);
  std::vector<DocumentLockEdit> lock_edits;
  std::unordered_map<EntityId, DocumentSceneBatchEntityState> simulated;
  std::unordered_map<EntityId, MeshComponent> simulated_meshes;

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
      const SceneLayerId layer_id = scene_.entity_layer(entity);
      if (layer_id != kInvalidSceneLayerId) {
        const SceneLayerRecord *layer = scene_.layer(layer_id);
        if (layer == nullptr || layer->locked) {
          return true;
        }
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
      const EntityId parent = operation.parent;
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
      if (!scene_transform_is_finite(operation.transform)) {
        return failure("Batch transform contains non-finite values.");
      }
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
          label, texture_lock_settings()));
      state->get().transform = operation.transform;
      break;
    }
    case DocumentSceneBatchOperationKind::SetVisible: {
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
    case DocumentSceneBatchOperationKind::SetMeshComponent: {
      if (!mesh_wireframe_color_is_valid(operation.mesh.wireframe_on_shaded_color)) {
        return failure("Wireframe color must contain finite RGB values from 0 to 1");
      }
      if (operation.mesh.mesh == kInvalidAssetId) {
        return failure("Mesh asset is invalid");
      }
      if (assets_ != nullptr && assets_->mesh(operation.mesh.mesh) == nullptr) {
        return failure("Mesh asset does not exist");
      }
      const auto pending = simulated_meshes.find(operation.entity);
      const MeshComponent *current_mesh =
          pending != simulated_meshes.end() ? &pending->second
                                           : scene_.mesh(operation.entity);
      if (current_mesh == nullptr) {
        return failure("Batch entity has no mesh component.");
      }
      if (document_command_entity_is_modeling_owned(scene_, operation.entity) &&
          current_mesh->mesh != operation.mesh.mesh) {
        return document_command_modeling_owned_entity_result();
      }
      if (document_command_same_mesh_component(*current_mesh, operation.mesh)) {
        break;
      }
      commands.push_back(std::make_unique<SetMeshComponentCommand>(
          scene_, operation.entity, *current_mesh, operation.mesh));
      simulated_meshes.insert_or_assign(operation.entity, operation.mesh);
      break;
    }
    case DocumentSceneBatchOperationKind::SetLocked: {
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
      history_, with_checked_selection_publication(
                    scene_, std::make_unique<BatchDocumentCommand>(
                                std::move(label), std::move(commands)),
                    selection_publication_));
}

} // namespace quader
