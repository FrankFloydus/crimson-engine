////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/commands/modeling/scene_aware_delete_selection_command.hpp"

#include "editor/commands/document/document_command_scene_helpers.hpp"
#include "scene/scene_document.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace quader {
namespace {

/**
 * Stores the Scene Aware Delete State data contract used by the modeling command layer.
 */
struct SceneAwareDeleteState {
    std::vector<DocumentEntitySnapshot> roots;
    DocumentSelectionState beforeSelection;
    ModelingSelectionMode beforeMode = ModelingSelectionMode::Object;
    std::vector<int> modelingObjectIds;
    std::vector<EntityId> currentRoots;
    ModelingSessionMemento beforeModeling;
    ModelingSessionMemento afterModeling;
    bool captured = false;
    bool runtimeDeleteExecuted = false;
};

CommandResult toCommandResult(ModelingCommandResult result) {
    return {
            .ok = result.ok,
            .changed = result.changed,
            .message = std::move(result.message),
    };
}

ModelingCommandResult missingRuntimeResult() {
    return {
            .ok = false,
            .changed = false,
            .message = "Scene-aware delete runtime is unavailable.",
    };
}

void appendModelingObjectIds(
        const DocumentEntitySnapshot& snapshot,
        std::vector<int>& objectIds) {
    if (snapshot.modelingObject.has_value() &&
            snapshot.modelingObject->objectId != InvalidModelingObjectId) {
        objectIds.push_back(snapshot.modelingObject->objectId);
    }
    for (const DocumentEntitySnapshot& child : snapshot.children) {
        appendModelingObjectIds(child, objectIds);
    }
}

std::vector<int> snapshotModelingObjectIds(
        const std::vector<DocumentEntitySnapshot>& snapshots) {
    std::vector<int> objectIds;
    for (const DocumentEntitySnapshot& snapshot : snapshots) {
        appendModelingObjectIds(snapshot, objectIds);
    }
    std::sort(objectIds.begin(), objectIds.end());
    objectIds.erase(std::unique(objectIds.begin(), objectIds.end()), objectIds.end());
    return objectIds;
}

EntityId entityForSnapshotModelingObject(
        const DocumentEntitySnapshot& snapshot,
        const SceneAwareDeleteRuntime& runtime) {
    if (!snapshot.modelingObject.has_value()) {
        return InvalidEntityId;
    }
    if (!runtime.entityForModelingObject) {
        return InvalidEntityId;
    }

    const std::optional<EntityId> entity = runtime.entityForModelingObject(snapshot.modelingObject->objectId);
    return entity.value_or(InvalidEntityId);
}

EntityId restoreSceneDeleteSnapshot(
        SceneDocument& scene,
        const SceneAwareDeleteRuntime& runtime,
        const DocumentEntitySnapshot& snapshot,
        EntityId parent,
        std::unordered_map<EntityId, EntityId>& remappedEntities) {
    EntityId restored = entityForSnapshotModelingObject(snapshot, runtime);
    if (restored == InvalidEntityId || !scene.exists(restored)) {
        restored = documentCommandCreateEntityFromSnapshot(
                scene,
                snapshot,
                InvalidEntityId,
                snapshot.name,
                true,
                true);
    } else {
        (void)scene.renameEntity(restored, snapshot.name);
        (void)scene.setEditorObjectId(restored, snapshot.editorObjectId);
        documentCommandApplyEntitySnapshotComponents(scene, restored, snapshot, true);
    }

    if (restored == InvalidEntityId || !scene.exists(restored)) {
        return InvalidEntityId;
    }
    if (!scene.setParent(restored, parent, false)) {
        return InvalidEntityId;
    }
    scene.setTransform(restored, snapshot.transform);
    remappedEntities[snapshot.originalId] = restored;

    for (const DocumentEntitySnapshot& child : snapshot.children) {
        if (restoreSceneDeleteSnapshot(scene, runtime, child, restored, remappedEntities) == InvalidEntityId) {
            return InvalidEntityId;
        }
    }
    return restored;
}

/**
 * Applies Scene Aware Delete Selection behavior as an undoable modeling command.
 */
class SceneAwareDeleteSelectionCommand final : public IUndoableCommand {
public:
    SceneAwareDeleteSelectionCommand(SceneDocument& scene, SceneAwareDeleteRuntime runtime)
        : scene_(scene)
        , runtime_(std::move(runtime)) {}

    [[nodiscard]] const std::string& label() const override {
        return label_;
    }

    CommandResult undo() override {
        if (!executed_) {
            return commandInvalidResult("Delete Selection has not been executed.");
        }
        return toCommandResult(undoSceneAwareDelete());
    }

    CommandResult redo() override {
        ModelingCommandResult result = redoSceneAwareDelete(!executed_);
        if (result.ok && result.changed) {
            executed_ = true;
        }
        return toCommandResult(std::move(result));
    }

private:
    ModelingCommandResult captureSceneAwareDeleteState() {
        if (!runtime_.appSelectionMode || !runtime_.captureModelingMemento) {
            return missingRuntimeResult();
        }

        state_ = {};
        state_.beforeMode = runtime_.appSelectionMode();
        state_.beforeSelection = documentCommandCaptureSelection(scene_);

        const std::vector<EntityId> roots = documentCommandSelectedRootEntities(scene_);
        if (roots.empty()) {
            return {.ok = false, .changed = false, .message = "Selection is empty."};
        }

        state_.roots.reserve(roots.size());
        for (const EntityId root : roots) {
            std::optional<DocumentEntitySnapshot> snapshot =
                    documentCommandCaptureEntitySnapshot(scene_, root);
            if (!snapshot.has_value()) {
                return {.ok = false, .changed = false, .message = "Selected entity is missing."};
            }
            state_.roots.push_back(std::move(*snapshot));
            state_.currentRoots.push_back(root);
        }
        state_.modelingObjectIds = snapshotModelingObjectIds(state_.roots);
        state_.beforeModeling = runtime_.captureModelingMemento();
        state_.captured = true;
        return {.ok = true, .changed = !state_.roots.empty()};
    }

    ModelingCommandResult executeSceneAwareDeleteModelingObjects() {
        if (state_.modelingObjectIds.empty()) {
            return {.ok = true, .changed = false};
        }
        if (!runtime_.deleteModelingObjects) {
            return missingRuntimeResult();
        }
        ModelingCommandResult result = runtime_.deleteModelingObjects(state_.modelingObjectIds);
        if (result.ok && result.changed) {
            state_.runtimeDeleteExecuted = true;
        }
        return result;
    }

    ModelingCommandResult destroySceneAwareDeleteRoots() {
        bool changed = false;
        for (const EntityId root : state_.currentRoots) {
            if (scene_.exists(root)) {
                changed = scene_.destroyEntity(root) || changed;
            }
        }
        state_.currentRoots.clear();
        invalidateSelectionOverlayCaches();
        return {.ok = true, .changed = changed};
    }

    ModelingCommandResult redoSceneAwareDelete(bool initialRedo) {
        if (initialRedo) {
            ModelingCommandResult captured = captureSceneAwareDeleteState();
            if (!captured.ok || !captured.changed) {
                return captured;
            }
            ModelingCommandResult runtimeDelete = executeSceneAwareDeleteModelingObjects();
            if (!runtimeDelete.ok) {
                return runtimeDelete;
            }
            ModelingCommandResult sceneDelete = destroySceneAwareDeleteRoots();
            if (runtimeDelete.changed) {
                if (!runtime_.captureModelingMemento) {
                    return missingRuntimeResult();
                }
                state_.afterModeling = runtime_.captureModelingMemento();
            }
            return {
                    .ok = true,
                    .changed = runtimeDelete.changed || sceneDelete.changed,
                    .message = "Delete Selection",
            };
        }

        ModelingCommandResult runtimeRedo{.ok = true, .changed = false};
        if (state_.runtimeDeleteExecuted) {
            if (!runtime_.restoreModelingMemento) {
                return missingRuntimeResult();
            }
            runtimeRedo = runtime_.restoreModelingMemento(state_.afterModeling);
            if (!runtimeRedo.ok) {
                return runtimeRedo;
            }
        }
        ModelingCommandResult sceneDelete = destroySceneAwareDeleteRoots();
        return {
                .ok = true,
                .changed = runtimeRedo.changed || sceneDelete.changed,
                .message = "Delete Selection",
        };
    }

    ModelingCommandResult undoSceneAwareDelete() {
        if (!state_.captured) {
            return {.ok = false, .changed = false, .message = "Delete selection was not captured."};
        }

        if (state_.runtimeDeleteExecuted) {
            if (!runtime_.restoreModelingMemento) {
                return missingRuntimeResult();
            }
            ModelingCommandResult runtimeUndo = runtime_.restoreModelingMemento(state_.beforeModeling);
            if (!runtimeUndo.ok) {
                return runtimeUndo;
            }
        }

        std::unordered_map<EntityId, EntityId> remappedEntities;
        std::vector<EntityId> restoredRoots;
        restoredRoots.reserve(state_.roots.size());
        for (const DocumentEntitySnapshot& snapshot : state_.roots) {
            const EntityId parent = snapshot.originalParent != InvalidEntityId && scene_.exists(snapshot.originalParent)
                    ? snapshot.originalParent
                    : InvalidEntityId;
            const EntityId restored = restoreSceneDeleteSnapshot(scene_, runtime_, snapshot, parent, remappedEntities);
            if (restored == InvalidEntityId) {
                return {.ok = false, .changed = false, .message = "Deleted hierarchy could not be restored."};
            }
            restoredRoots.push_back(restored);
        }

        state_.currentRoots = std::move(restoredRoots);
        if (runtime_.setAppSelectionMode) {
            runtime_.setAppSelectionMode(state_.beforeMode);
        }
        documentCommandApplySelection(
                scene_,
                documentCommandRemapSelection(state_.beforeSelection, remappedEntities));
        if (state_.beforeMode == ModelingSelectionMode::Object) {
            if (!runtime_.syncObjectSelectionFromScene) {
                return missingRuntimeResult();
            }
            runtime_.syncObjectSelectionFromScene(true);
        } else if (state_.beforeMode == ModelingSelectionMode::Group) {
            if (!runtime_.clearRuntimeObjectSelectionForGroupMode) {
                return missingRuntimeResult();
            }
            runtime_.clearRuntimeObjectSelectionForGroupMode();
        }
        invalidateSelectionOverlayCaches();
        return {.ok = true, .changed = true, .message = "Delete Selection"};
    }

    void invalidateSelectionOverlayCaches() const {
        if (runtime_.invalidateSelectionOverlayCaches) {
            runtime_.invalidateSelectionOverlayCaches();
        }
    }

    SceneDocument& scene_;
    SceneAwareDeleteRuntime runtime_;
    SceneAwareDeleteState state_;
    bool executed_ = false;
    std::string label_ = "Delete Selection";
};

} // namespace

bool sceneAwareDeleteHasSelectedSceneGroupRoot(const SceneDocument& scene) {
    for (const EntityId root : documentCommandSelectedRootEntities(scene)) {
        if (!scene.isSceneRoot(root) && scene.kind(root) == EntityKind::Empty && scene.hasChildren(root)) {
            return true;
        }
    }
    return false;
}

std::unique_ptr<IUndoableCommand> createSceneAwareDeleteSelectionCommand(
        SceneDocument& scene,
        SceneAwareDeleteRuntime runtime) {
    return std::make_unique<SceneAwareDeleteSelectionCommand>(scene, std::move(runtime));
}

} // namespace quader
