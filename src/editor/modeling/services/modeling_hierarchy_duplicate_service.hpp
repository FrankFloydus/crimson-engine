////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/modeling_types.hpp"
#include "editor/modeling/runtime/modeling_object_runtime_map.hpp"
#include "editor/selection/modeling_selection_projection_snapshot.hpp"
#include "core/types.hpp"
#include "scene/scene_entity_naming.hpp"

#include <functional>
#include <span>
#include <utility>
#include <vector>

namespace quader {

class DocumentCommandService;
class SceneDocument;

/**
 * Reports one explicit modeling duplicate batch to the hierarchy owner.
 */
struct ModelingHierarchyDuplicateBatchResult final {
  ModelingCommandResult command;
  std::vector<EntityId> duplicate_entities;
};

/**
 * Reports the deterministic source-root to duplicate-root mapping produced by
 * one scene-aware hierarchy duplicate.
 */
struct ModelingHierarchyDuplicateResult final : ModelingCommandResult {
  ModelingHierarchyDuplicateResult() = default;
  explicit ModelingHierarchyDuplicateResult(ModelingCommandResult status)
      : ModelingCommandResult(std::move(status)) {}
  ModelingHierarchyDuplicateResult(ModelingCommandResult status,
                                   std::vector<EntityId> sources,
                                   std::vector<EntityId> duplicates,
                                   std::vector<EntityId> modeled_sources,
                                   std::vector<EntityId> modeled_duplicates)
      : ModelingCommandResult(std::move(status)),
        source_roots(std::move(sources)),
        duplicate_roots(std::move(duplicates)),
        source_modeling_entities(std::move(modeled_sources)),
        duplicate_modeling_entities(std::move(modeled_duplicates)) {}

  std::vector<EntityId> source_roots;
  std::vector<EntityId> duplicate_roots;
  std::vector<EntityId> source_modeling_entities;
  std::vector<EntityId> duplicate_modeling_entities;
};

/**
 * Stores runtime callbacks required to duplicate modeling-backed group hierarchies.
 */
struct ModelingHierarchyDuplicateRuntime {
  std::function<void()> ensure_scene_objects_synced;
  std::function<ModelingSelectionMode()> selection_mode;
  std::function<ModelingCommandResult(ModelingSelectionMode)>
      set_selection_mode;
  std::function<ModelingHierarchyDuplicateBatchResult(
      std::span<const EntityId>)>
      duplicate_modeling_objects;
  std::function<bool(EntityId)> detach_duplicate_mesh;
};

/**
 * Owns modeling-backed hierarchy duplicate policy for group drag workflows.
 */
class ModelingHierarchyDuplicateService final {
public:
  ModelingHierarchyDuplicateResult duplicate_entity_hierarchy(
      const ModelingSelectionProjectionSnapshot &selection,
      SceneDocument &scene,
      ModelingObjectRuntimeMap &object_registry,
      DocumentCommandService &document_commands,
      ModelingHierarchyDuplicateRuntime runtime,
      std::span<const EntityId> explicit_roots,
      ModelingSelectionMode result_selection_mode,
      SceneEntityNamePolicy name_policy = SceneEntityNamePolicy::DuplicateFamily) const;
};

} // namespace quader
