////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/Types.h"
#include "math/Types.h"

#include <optional>
#include <span>

namespace quader {

class AssetRegistry;
class ModelingObjectRuntimeMap;
class SceneDocument;

} // namespace quader

namespace quader_editor_core {

class EditorSession;

} // namespace quader_editor_core

namespace quader {

/**
 * Stores the outcome of projecting scene object selection into the modeling runtime.
 */
struct RuntimeObjectSelectionProjectionResult {
    bool ok = true;
    bool changed = false;
    bool hasModelingObjects = false;
};

/**
 * Owns projection policy between scene selection state and runtime modeling selection state.
 */
class SelectionProjectionService final {
public:
    [[nodiscard]] bool shouldProjectObjectSelectionFromScene(
            const quader_editor_core::EditorSession& session,
            bool forceObjectMode) const;

    bool projectObjectSelectionFromScene(
            quader_editor_core::EditorSession& session,
            const SceneDocument& scene,
            const ModelingObjectRuntimeMap& objectRegistry,
            bool forceObjectMode = false) const;

    RuntimeObjectSelectionProjectionResult projectObjectSelectionForSceneEntities(
            quader_editor_core::EditorSession& session,
            std::span<const EntityId> entities,
            EntityId active,
            const ModelingObjectRuntimeMap& objectRegistry) const;

    bool promoteObjectSelectionForTool(
            quader_editor_core::EditorSession& session,
            const SceneDocument& scene,
            const ModelingObjectRuntimeMap& objectRegistry) const;

    bool projectSceneSelectionFromModeling(
            const quader_editor_core::EditorSession& session,
            SceneDocument& scene,
            const ModelingObjectRuntimeMap& objectRegistry) const;

    bool restoreSceneSelection(
            SceneDocument& scene,
            std::span<const EntityId> selected,
            EntityId active = InvalidEntityId) const;

    bool addSceneSelection(
            SceneDocument& scene,
            std::span<const EntityId> selected,
            EntityId active = InvalidEntityId) const;

    [[nodiscard]] std::optional<QVec3> worldSelectionCenter(
            const quader_editor_core::EditorSession& session,
            const SceneDocument& scene,
            const AssetRegistry& assets,
            const ModelingObjectRuntimeMap& objectRegistry) const;
};

} // namespace quader
