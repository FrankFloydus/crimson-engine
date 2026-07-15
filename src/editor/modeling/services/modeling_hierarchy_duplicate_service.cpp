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

#include <editor/modeling/runtime/session.hpp>

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
    const quader_editor_core::EditorSession &session,
    const SceneDocument &scene, const ModelingObjectRuntimeMap &object_registry,
    const std::vector<EntityId> &tree_order) {
  if (tree_order.empty()) {
    return {};
  }

  const std::unordered_set<EntityId> source_mesh_set(tree_order.begin(),
                                                     tree_order.end());
  std::vector<EntityId> source_meshes;
  source_meshes.reserve(tree_order.size());
  for (const quader_editor_core::PolygonObject &object : session.objects()) {
    const std::optional<EntityId> entity =
        object_registry.entity_for_modeling_object(object.id);
    if (entity.has_value() && source_mesh_set.contains(*entity)) {
      source_meshes.push_back(*entity);
    }
  }
  return source_meshes;
}

std::vector<EntityId> selected_duplicate_mesh_entities(
    const quader_editor_core::EditorSession &session,
    const ModelingObjectRuntimeMap &object_registry) {
  const std::vector<int> selected_duplicate_object_ids =
      session.selected_object_ids();
  const std::unordered_set<int> duplicate_object_set(
      selected_duplicate_object_ids.begin(),
      selected_duplicate_object_ids.end());
  std::vector<EntityId> duplicate_meshes;
  duplicate_meshes.reserve(selected_duplicate_object_ids.size());
  for (const quader_editor_core::PolygonObject &object : session.objects()) {
    if (!duplicate_object_set.contains(object.id)) {
      continue;
    }
    const std::optional<EntityId> entity =
        object_registry.entity_for_modeling_object(object.id);
    if (entity.has_value()) {
      duplicate_meshes.push_back(*entity);
    }
  }
  return duplicate_meshes;
}

void restore_group_selection(DocumentCommandService &document_commands,
                             const ModelingHierarchyDuplicateRuntime &runtime,
                             const std::vector<EntityId> &roots,
                             ModelingSelectionMode result_selection_mode) {
  (void)document_commands.select_entities(
      roots, roots.empty() ? kInvalidEntityId : roots.front());
  if (runtime.set_selection_mode) {
    (void)runtime.set_selection_mode(result_selection_mode);
  }
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

ModelingCommandResult
ModelingHierarchyDuplicateService::duplicate_selected_hierarchy_for_group_drag(
    quader_editor_core::EditorSession &session, SceneDocument &scene,
    ModelingObjectRuntimeMap &object_registry,
    DocumentCommandService &document_commands,
    ModelingHierarchyDuplicateRuntime runtime) const {
  return duplicate_selected_entity_hierarchy(
      session, scene, object_registry, document_commands, std::move(runtime),
      ModelingSelectionMode::Group);
}

ModelingCommandResult
ModelingHierarchyDuplicateService::duplicate_selected_entity_hierarchy(
    quader_editor_core::EditorSession &session, SceneDocument &scene,
    ModelingObjectRuntimeMap &object_registry,
    DocumentCommandService &document_commands,
    ModelingHierarchyDuplicateRuntime runtime,
    ModelingSelectionMode result_selection_mode) const {
  QDR_PROFILE_FUNCTION();
  const std::vector<EntityId> roots =
      document_command_selected_root_entities(scene);
  if (roots.empty()) {
    return {.ok = false,
            .changed = false,
            .message = "Select an entity to duplicate."};
  }

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
          session, scene, object_registry, source_meshes);
    }
  }

  std::vector<EntityId> duplicate_meshes;
  if (!source_meshes.empty()) {
    if (!runtime.set_selection_mode ||
        !runtime.project_object_selection_from_scene ||
        !runtime.duplicate_selected_meshes) {
      restore_group_selection(document_commands, runtime, roots,
                              result_selection_mode);
      return {.ok = false,
              .changed = false,
              .message =
                  "Modeling hierarchy duplicate runtime is unavailable."};
    }

    const ModelingCommandResult object_mode = [&]() {
      QDR_PROFILE_SCOPE("Duplicate switch to object mode");
      return runtime.set_selection_mode(ModelingSelectionMode::Object);
    }();
    if (!object_mode.ok) {
      restore_group_selection(document_commands, runtime, roots,
                              result_selection_mode);
      return object_mode;
    }
    {
      QDR_PROFILE_SCOPE("Duplicate select source meshes");
      (void)document_commands.select_entities(source_meshes,
                                              source_meshes.front());
    }
    {
      QDR_PROFILE_SCOPE("Duplicate project object selection");
      runtime.project_object_selection_from_scene();
    }
    const ModelingCommandResult duplicate = [&]() {
      QDR_PROFILE_SCOPE("Duplicate runtime meshes");
      return runtime.duplicate_selected_meshes();
    }();
    if (!duplicate.ok) {
      restore_group_selection(document_commands, runtime, roots,
                              result_selection_mode);
      return duplicate;
    }

    {
      QDR_PROFILE_SCOPE("Duplicate collect runtime mesh clones");
      duplicate_meshes =
          selected_duplicate_mesh_entities(session, object_registry);
    }
    if (duplicate_meshes.size() != source_meshes.size()) {
      restore_group_selection(document_commands, runtime, roots,
                              result_selection_mode);
      return {
          .ok = false,
          .changed = duplicate.changed,
          .message = "Group duplicate did not create matching mesh clones.",
      };
    }
  }

  const std::unordered_map<EntityId, EntityId> clone_by_source =
      pair_duplicate_meshes(source_meshes, duplicate_meshes);
  const DocumentDuplicateHierarchyResult duplicated = [&]() {
    QDR_PROFILE_SCOPE("Duplicate document hierarchy");
    return document_commands.duplicate_hierarchy_with_existing_clones(
        roots, clone_by_source);
  }();
  if (!duplicated.ok || duplicated.duplicate_roots.empty()) {
    restore_group_selection(document_commands, runtime, roots,
                            result_selection_mode);
    return {
        .ok = duplicated.ok,
        .changed = duplicated.changed,
        .message = duplicated.message.empty() ? "Group duplicate failed."
                                              : duplicated.message,
    };
  }

  if (runtime.set_selection_mode) {
    (void)runtime.set_selection_mode(result_selection_mode);
  }
  {
    QDR_PROFILE_SCOPE("Duplicate select duplicate roots");
    (void)document_commands.select_entities(duplicated.duplicate_roots,
                                            duplicated.duplicate_roots.front());
  }
  return {.ok = true, .changed = true, .message = "Selection duplicated."};
}

} // namespace quader
