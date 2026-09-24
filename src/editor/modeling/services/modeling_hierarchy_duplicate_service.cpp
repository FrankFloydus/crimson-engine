////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/services/modeling_hierarchy_duplicate_service.hpp"

#include "diagnostics/profile.hpp"
#include "editor/commands/document_command_service.hpp"
#include "editor/commands/document/document_command_scene_helpers.hpp"
#include "editor/modeling/runtime/modeling_object_runtime_map.hpp"
#include "scene/components.hpp"
#include "scene/scene_document.hpp"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace quader {
namespace {

std::vector<EntityId>
source_mesh_entities_in_tree_order(const SceneDocument &scene,
                                   const std::vector<EntityId> &roots) {
  std::vector<EntityId> source_meshes;
  for (const EntityId root : roots) {
    for (const EntityId entity : scene.descendants(root, true)) {
      if (scene.modeling_object(entity) != nullptr) {
        source_meshes.push_back(entity);
      }
    }
  }
  return source_meshes;
}

std::vector<EntityId> source_mesh_entities_in_runtime_order(
    const ModelingSelectionProjectionSnapshot &selection,
    const SceneDocument &scene, const ModelingObjectRuntimeMap &object_registry,
    const std::vector<EntityId> &tree_order) {
  if (tree_order.empty()) {
    return {};
  }

  const std::unordered_set<EntityId> source_mesh_set(tree_order.begin(),
                                                     tree_order.end());
  std::vector<EntityId> source_meshes;
  source_meshes.reserve(tree_order.size());
  for (const quader_editor_core::PolygonObject &object : selection.objects()) {
    const std::optional<EntityId> entity =
        object_registry.entity_for_modeling_object(object.id);
    if (entity.has_value() && source_mesh_set.contains(*entity)) {
      source_meshes.push_back(*entity);
    }
  }
  // The legacy picking projection deliberately contains polygons only. Curves
  // remain SDK-owned objects and must participate in the same clone mapping.
  for (const EntityId entity : tree_order) {
    if (scene.curve(entity) != nullptr &&
        object_registry.modeling_object_for_entity(entity).has_value()) {
      source_meshes.push_back(entity);
    }
  }
  return source_meshes;
}

void restore_selection_baseline(
    SceneDocument &scene, const DocumentSelectionState &selection,
    const ModelingHierarchyDuplicateRuntime &runtime,
    ModelingSelectionMode selection_mode) {
  if (runtime.set_selection_mode) {
    (void)runtime.set_selection_mode(selection_mode);
  }
  document_command_apply_selection(scene, selection);
}

std::unordered_map<EntityId, EntityId>
pair_duplicate_meshes(const std::vector<EntityId> &source_meshes,
                      const std::vector<EntityId> &duplicate_meshes) {
  std::unordered_map<EntityId, EntityId> clone_by_source;
  for (std::size_t index = 0;
       index < source_meshes.size() && index < duplicate_meshes.size();
       ++index) {
    clone_by_source[source_meshes[index]] = duplicate_meshes[index];
  }
  return clone_by_source;
}

} // namespace

ModelingHierarchyDuplicateResult
ModelingHierarchyDuplicateService::duplicate_entity_hierarchy(
    const ModelingSelectionProjectionSnapshot &selection, SceneDocument &scene,
    ModelingObjectRuntimeMap &object_registry,
    DocumentCommandService &document_commands,
    ModelingHierarchyDuplicateRuntime runtime,
    std::span<const EntityId> explicit_roots,
    ModelingSelectionMode result_selection_mode) const {
  if (!document_command_hierarchy_available_in_release(scene, explicit_roots)) {
    return ModelingHierarchyDuplicateResult(ModelingCommandResult{
        .ok = false,
        .changed = false,
        .message = release_feature_unavailable_message()});
  }

  QDR_PROFILE_FUNCTION();
  std::vector<EntityId> roots(explicit_roots.begin(), explicit_roots.end());
  if (roots.empty()) {
    return ModelingHierarchyDuplicateResult(ModelingCommandResult{
        .ok = false,
        .changed = false,
        .message = "Select an entity to duplicate."});
  }
  const DocumentSelectionState selection_before =
      document_command_capture_selection(scene);
  const ModelingSelectionMode selection_mode_before =
      runtime.selection_mode ? runtime.selection_mode()
                             : result_selection_mode;

  std::vector<EntityId> source_meshes;
  {
    QDR_PROFILE_SCOPE("Duplicate collect source meshes");
    source_meshes = source_mesh_entities_in_tree_order(scene, roots);
  }
  if (!source_meshes.empty()) {
    if (runtime.ensure_scene_objects_synced) {
      QDR_PROFILE_SCOPE("Duplicate ensure scene objects synced");
      runtime.ensure_scene_objects_synced();
    }
    {
      QDR_PROFILE_SCOPE("Duplicate order source meshes by runtime");
      source_meshes = source_mesh_entities_in_runtime_order(
          selection, scene, object_registry, source_meshes);
    }
  }

  std::vector<EntityId> duplicate_meshes;
  if (!source_meshes.empty()) {
    if (!runtime.set_selection_mode ||
        !runtime.duplicate_modeling_objects || !runtime.detach_duplicate_mesh) {
      restore_selection_baseline(scene, selection_before, runtime,
                                 selection_mode_before);
      return ModelingHierarchyDuplicateResult(ModelingCommandResult{
          .ok = false,
          .changed = false,
          .message =
              "Modeling hierarchy duplicate runtime is unavailable."});
    }

    const ModelingCommandResult object_mode = [&]() {
      QDR_PROFILE_SCOPE("Duplicate switch to object mode");
      return runtime.set_selection_mode(ModelingSelectionMode::Object);
    }();
    if (!object_mode.ok) {
      restore_selection_baseline(scene, selection_before, runtime,
                                 selection_mode_before);
      return ModelingHierarchyDuplicateResult(object_mode);
    }
    const ModelingHierarchyDuplicateBatchResult duplicate = [&]() {
      QDR_PROFILE_SCOPE("Duplicate runtime meshes");
      return runtime.duplicate_modeling_objects(source_meshes);
    }();
    if (!duplicate.command.ok) {
      restore_selection_baseline(scene, selection_before, runtime,
                                 selection_mode_before);
      return ModelingHierarchyDuplicateResult(duplicate.command);
    }
    duplicate_meshes = duplicate.duplicate_entities;
    if (duplicate_meshes.size() != source_meshes.size()) {
      restore_selection_baseline(scene, selection_before, runtime,
                                 selection_mode_before);
      return ModelingHierarchyDuplicateResult(ModelingCommandResult{
          .ok = false,
          .changed = duplicate.command.changed,
          .message = "Group duplicate did not create matching mesh clones.",
      });
    }
    for (const EntityId duplicate_mesh : duplicate_meshes) {
      if (scene.parent(duplicate_mesh) != kInvalidEntityId &&
          !runtime.detach_duplicate_mesh(duplicate_mesh)) {
        restore_selection_baseline(scene, selection_before, runtime,
                                   selection_mode_before);
        return ModelingHierarchyDuplicateResult(ModelingCommandResult{
            .ok = false,
            .changed = true,
            .message =
                "Duplicated mesh could not be detached before hierarchy projection.",
        });
      }
    }
  }

  const std::unordered_map<EntityId, EntityId> clone_by_source =
      pair_duplicate_meshes(source_meshes, duplicate_meshes);
  // SDK duplication publishes its object selection while creating the native
  // clones. Structural duplication owns the scene selection transition, so
  // restore the caller's exact baseline before the document command captures
  // its undo state. This never selects an explicit target as a workaround.
  document_command_apply_selection(scene, selection_before);
  const DocumentDuplicateHierarchyResult duplicated = [&]() {
    QDR_PROFILE_SCOPE("Duplicate document hierarchy");
    return document_commands.duplicate_hierarchy_with_existing_clones(
        roots, clone_by_source);
  }();
  if (!duplicated.ok || duplicated.duplicate_roots.empty()) {
    restore_selection_baseline(scene, selection_before, runtime,
                               selection_mode_before);
    return ModelingHierarchyDuplicateResult(ModelingCommandResult{
        .ok = duplicated.ok,
        .changed = duplicated.changed,
        .message = duplicated.message.empty() ? "Group duplicate failed."
                                              : duplicated.message,
    });
  }

  if (!source_meshes.empty() && runtime.set_selection_mode) {
    (void)runtime.set_selection_mode(result_selection_mode);
  }
  return ModelingHierarchyDuplicateResult(
      ModelingCommandResult{.ok = true,
                            .changed = true,
                            .message = "Selection duplicated."},
      std::move(roots), duplicated.duplicate_roots, std::move(source_meshes),
      std::move(duplicate_meshes));
}

} // namespace quader
