////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"

#include <span>
#include <vector>

namespace quader {

class SceneDocument;
class SceneModelingProjectionCheckpoint;

/**
 * Distinguishes undoable command selection writes from runtime projection sync.
 */
enum class SceneSelectionApplicationMode {
  UndoableCommand,
  RuntimeSync,
};

/**
 * Enumerates scene selection edit operations.
 */
enum class SceneSelectionEdit {
  Replace,
  Add,
  Remove,
  Toggle,
};

/**
 * Restricts bulk user selection collection for scene-owned modes.
 */
enum class SceneSelectionBulkMode {
  Object,
  Group,
};

/**
 * Stores normalized scene selection contents.
 */
struct SceneSelectionState {
  std::vector<EntityId> selected;
  EntityId active = kInvalidEntityId;
};

[[nodiscard]] SceneSelectionState
capture_scene_selection(const SceneDocument &scene);

[[nodiscard]] SceneSelectionState
make_scene_selection(const SceneDocument &scene,
                     std::span<const EntityId> entities,
                     EntityId active = kInvalidEntityId);

[[nodiscard]] bool same_scene_selection(const SceneSelectionState &left,
                                        const SceneSelectionState &right);

/**
 * Captures only Selection-domain records for the entities touched while
 * moving between two scene selections, plus the exact scene revision scalars.
 */
[[nodiscard]] SceneModelingProjectionCheckpoint
capture_scene_selection_checkpoint(const SceneDocument &scene,
                                   const SceneSelectionState &before,
                                   const SceneSelectionState &after);

/** Restores one bounded Selection-domain checkpoint without a scene scan. */
[[nodiscard]] bool restore_scene_selection_checkpoint(
    SceneDocument &scene,
    const SceneModelingProjectionCheckpoint &checkpoint);

[[nodiscard]] bool scene_entity_selectable_for_user_selection(
    const SceneDocument &scene, EntityId entity);

[[nodiscard]] std::vector<EntityId>
user_selectable_scene_entities(const SceneDocument &scene,
                               std::span<const EntityId> entities);

[[nodiscard]] EntityId
user_selectable_active_entity(const SceneDocument &scene,
                              const std::vector<EntityId> &entities,
                              EntityId active);

[[nodiscard]] SceneSelectionState
make_user_scene_selection(const SceneDocument &scene,
                          std::span<const EntityId> entities,
                          EntityId active = kInvalidEntityId);

[[nodiscard]] std::vector<EntityId>
user_selectable_scene_entities_for_mode(const SceneDocument &scene,
                                        SceneSelectionBulkMode mode);

[[nodiscard]] SceneSelectionState
resolve_scene_selection_edit(std::span<const EntityId> current_selection,
                             EntityId current_active,
                             std::span<const EntityId> edited_entities,
                             SceneSelectionEdit edit);

bool apply_scene_selection(SceneDocument &scene,
                           const SceneSelectionState &state,
                           SceneSelectionApplicationMode mode);

bool add_scene_selection(SceneDocument &scene,
                         std::span<const EntityId> selected,
                         EntityId active,
                         SceneSelectionApplicationMode mode);

} // namespace quader
