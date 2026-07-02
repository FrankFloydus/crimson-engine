////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/i_undoable_command.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace quader {

class SceneDocument;

/**
 * Stores runtime callbacks required by the scene-aware delete command owner.
 */
struct SceneAwareDeleteRuntime {
  std::function<ModelingSessionMemento()> capture_modeling_memento;
  std::function<ModelingCommandResult(const ModelingSessionMemento &)>
      restore_modeling_memento;
  std::function<ModelingSelectionMode()> app_selection_mode;
  std::function<void(ModelingSelectionMode)> set_app_selection_mode;
  std::function<ModelingCommandResult(const std::vector<int> &)>
      delete_modeling_objects;
  std::function<std::optional<EntityId>(int)> entity_for_modeling_object;
  std::function<void(bool)> sync_object_selection_from_scene;
  std::function<void()> clear_runtime_object_selection_for_group_mode;
  std::function<void()> invalidate_selection_overlay_caches;
};

[[nodiscard]] bool
scene_aware_delete_has_selected_root(const SceneDocument &scene);
[[nodiscard]] bool
scene_aware_delete_uses_document_route(ModelingSelectionMode mode);
[[nodiscard]] bool
scene_aware_delete_handles_selection(ModelingSelectionSummary summary,
                                     bool has_selected_root);

std::unique_ptr<IUndoableCommand>
create_scene_aware_delete_selection_command(SceneDocument &scene,
                                            SceneAwareDeleteRuntime runtime);

} // namespace quader
