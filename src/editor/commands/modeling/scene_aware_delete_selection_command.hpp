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
    std::function<ModelingSessionMemento()> captureModelingMemento;
    std::function<ModelingCommandResult(const ModelingSessionMemento&)> restoreModelingMemento;
    std::function<ModelingSelectionMode()> appSelectionMode;
    std::function<void(ModelingSelectionMode)> setAppSelectionMode;
    std::function<ModelingCommandResult(const std::vector<int>&)> deleteModelingObjects;
    std::function<std::optional<EntityId>(int)> entityForModelingObject;
    std::function<void(bool)> syncObjectSelectionFromScene;
    std::function<void()> clearRuntimeObjectSelectionForGroupMode;
    std::function<void()> invalidateSelectionOverlayCaches;
};

[[nodiscard]] bool sceneAwareDeleteHasSelectedSceneGroupRoot(const SceneDocument& scene);

std::unique_ptr<IUndoableCommand> createSceneAwareDeleteSelectionCommand(
        SceneDocument& scene,
        SceneAwareDeleteRuntime runtime);

} // namespace quader
