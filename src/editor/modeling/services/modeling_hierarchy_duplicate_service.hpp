////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/modeling_types.hpp"
#include "core/types.hpp"

#include <functional>

namespace quader_editor_core {
class EditorSession;
}

namespace quader {

class DocumentCommandService;
class ModelingObjectRuntimeMap;
class SceneDocument;

/**
 * Stores runtime callbacks required to duplicate modeling-backed group hierarchies.
 */
struct ModelingHierarchyDuplicateRuntime {
    std::function<void()> ensureSceneObjectsSynced;
    std::function<ModelingCommandResult(ModelingSelectionMode)> setSelectionMode;
    std::function<void()> projectObjectSelectionFromScene;
    std::function<ModelingCommandResult()> duplicateSelectedMeshes;
};

/**
 * Owns modeling-backed hierarchy duplicate policy for group drag workflows.
 */
class ModelingHierarchyDuplicateService final {
public:
    ModelingCommandResult duplicateSelectedHierarchyForGroupDrag(
            quader_editor_core::EditorSession& session,
            SceneDocument& scene,
            ModelingObjectRuntimeMap& objectRegistry,
            DocumentCommandService& documentCommands,
            ModelingHierarchyDuplicateRuntime runtime) const;
    ModelingCommandResult duplicateSelectedEntityHierarchy(
            quader_editor_core::EditorSession& session,
            SceneDocument& scene,
            ModelingObjectRuntimeMap& objectRegistry,
            DocumentCommandService& documentCommands,
            ModelingHierarchyDuplicateRuntime runtime,
            ModelingSelectionMode resultSelectionMode) const;
};

} // namespace quader
