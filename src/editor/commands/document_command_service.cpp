////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/commands/document_command_service.hpp"

#include "assets/asset_registry.hpp"
#include "diagnostics/profile.hpp"
#include "editor/commands/document/apply_entity_transform_command.hpp"
#include "editor/commands/document/batch_document_command.hpp"
#include "editor/commands/document/create_entity_command.hpp"
#include "editor/commands/document/create_light_command.hpp"
#include "editor/commands/document/destroy_entity_command.hpp"
#include "editor/commands/document/duplicate_entity_command.hpp"
#include "editor/commands/document/duplicate_selected_hierarchy_command.hpp"
#include "editor/commands/document/group_selection_command.hpp"
#include "editor/commands/document/rename_entity_command.hpp"
#include "editor/commands/document/reparent_entity_command.hpp"
#include "editor/commands/document/select_entities_command.hpp"
#include "editor/commands/document/set_lock_command.hpp"
#include "editor/commands/document/set_light_component_command.hpp"
#include "editor/commands/document/set_material_command.hpp"
#include "editor/commands/document/set_mesh_component_command.hpp"
#include "editor/commands/document/set_scene_settings_command.hpp"
#include "editor/commands/document/set_visibility_command.hpp"
#include "editor/commands/document/ungroup_selection_command.hpp"
#include "scene/scene_document.hpp"

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace quader {
namespace {

DocumentCommandResult executeDocumentCommand(
        EditorHistoryService& history,
        std::unique_ptr<IUndoableCommand> command) {
    return history.execute(std::move(command));
}

DocumentCommandResult executeDocumentCommand(
        EditorHistoryService& history,
        std::unique_ptr<IUndoableCommand> command,
        UndoRedoExecuteOptions options) {
    return history.execute(std::move(command), std::move(options));
}

void assignDocumentCommandStatus(DocumentCommandResult& target, DocumentCommandResult source) {
    target.ok = source.ok;
    target.changed = source.changed;
    target.message = std::move(source.message);
}

bool documentCommandEntityIsModelingOwned(const SceneDocument& scene, EntityId entity) {
    return scene.modelingObject(entity) != nullptr;
}

DocumentCommandResult documentCommandModelingOwnedEntityResult() {
    return documentCommandInvalidRequestResult("Entity is owned by a modeling object; use modeling commands");
}

DocumentCommandResult documentCommandSceneRootProtectedResult() {
    return documentCommandInvalidRequestResult("Scene root cannot be changed by this command");
}

bool documentCommandSelectionIncludesSceneRoot(const SceneDocument& scene) {
    for (const EntityId entity : scene.selectedEntities()) {
        if (scene.isSceneRoot(entity)) {
            return true;
        }
    }
    return false;
}

bool documentCommandSelectedRootContainsModelingOwnedEntity(const SceneDocument& scene) {
    for (const EntityId root : documentCommandSelectedRootEntities(scene)) {
        if (documentCommandEntityIsModelingOwned(scene, root)) {
            return true;
        }
    }
    return false;
}

EntityId normalizedCreateParent(const SceneDocument& scene, EntityId parent) {
    if (parent != InvalidEntityId) {
        return parent;
    }
    return scene.sceneRoot();
}

EntityId normalizedHierarchyParent(const SceneDocument& scene, EntityId entity, EntityId parent) {
    if (parent != InvalidEntityId || scene.isSceneRoot(entity)) {
        return parent;
    }
    return scene.sceneRoot();
}

std::vector<DocumentTransformEdit> normalizedTransformEdits(
        const SceneDocument& scene,
        std::vector<DocumentTransformEdit> edits) {
    std::vector<DocumentTransformEdit> result;
    result.reserve(edits.size());
    for (const DocumentTransformEdit& edit : edits) {
        if (edit.entity == InvalidEntityId ||
                !scene.exists(edit.entity) ||
                scene.isEffectivelyLocked(edit.entity) ||
                documentCommandSameTransform(edit.before, edit.after)) {
            continue;
        }
        result.push_back(edit);
    }
    std::sort(result.begin(), result.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.entity < rhs.entity;
    });
    return result;
}

std::vector<DocumentVisibilityEdit> normalizedVisibilityEdits(
        const SceneDocument& scene,
        std::vector<DocumentVisibilityEdit> edits) {
    std::vector<DocumentVisibilityEdit> result;
    result.reserve(edits.size());
    for (const DocumentVisibilityEdit& edit : edits) {
        if (edit.entity == InvalidEntityId ||
                !scene.exists(edit.entity) ||
                scene.isSceneRoot(edit.entity) ||
                edit.before == edit.after) {
            continue;
        }
        result.push_back(edit);
    }
    std::sort(result.begin(), result.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.entity < rhs.entity;
    });
    return result;
}

std::vector<DocumentLockEdit> normalizedLockEdits(
        const SceneDocument& scene,
        std::vector<DocumentLockEdit> edits) {
    std::vector<DocumentLockEdit> result;
    result.reserve(edits.size());
    for (const DocumentLockEdit& edit : edits) {
        if (edit.entity == InvalidEntityId ||
                !scene.exists(edit.entity) ||
                scene.isSceneRoot(edit.entity) ||
                edit.before == edit.after) {
            continue;
        }
        result.push_back(edit);
    }
    std::sort(result.begin(), result.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.entity < rhs.entity;
    });
    return result;
}

bool documentCommandEffectivelyLockedAfterEdits(
        const SceneDocument& scene,
        EntityId entity,
        const std::unordered_map<EntityId, bool>& lockedAfterByEntity) {
    while (entity != InvalidEntityId && scene.exists(entity)) {
        if (const auto edited = lockedAfterByEntity.find(entity); edited != lockedAfterByEntity.end()) {
            if (edited->second) {
                return true;
            }
        } else if (const LockComponent* lock = scene.lock(entity); lock != nullptr && lock->locked) {
            return true;
        }
        entity = scene.parent(entity);
    }
    return false;
}

DocumentSelectionState documentCommandSelectionAfterLockEdits(
        const SceneDocument& scene,
        const std::vector<DocumentLockEdit>& edits) {
    std::unordered_map<EntityId, bool> lockedAfterByEntity;
    lockedAfterByEntity.reserve(edits.size());
    for (const DocumentLockEdit& edit : edits) {
        lockedAfterByEntity[edit.entity] = edit.after;
    }

    DocumentSelectionState after;
    const std::vector<EntityId> beforeSelected = scene.selectedEntities();
    after.selected.reserve(beforeSelected.size());
    for (const EntityId selected : beforeSelected) {
        if (!documentCommandEffectivelyLockedAfterEdits(scene, selected, lockedAfterByEntity)) {
            after.selected.push_back(selected);
        }
    }

    const EntityId beforeActive = scene.primarySelection();
    after.active = std::find(after.selected.begin(), after.selected.end(), beforeActive) != after.selected.end()
            ? beforeActive
            : (after.selected.empty() ? InvalidEntityId : after.selected.back());
    return after;
}

void appendSelectionCleanupAfterLockEdits(
        SceneDocument& scene,
        const std::vector<DocumentLockEdit>& edits,
        std::vector<std::unique_ptr<IUndoableCommand>>& commands) {
    if (edits.empty()) {
        return;
    }

    const DocumentSelectionState before = documentCommandCaptureSelection(scene);
    const DocumentSelectionState after = documentCommandSelectionAfterLockEdits(scene, edits);
    if (documentCommandSameSelection(before, after)) {
        return;
    }

    commands.push_back(std::make_unique<SelectEntitiesCommand>(scene, after.selected, after.active));
}

/**
 * Stores the simulated batch entity state used while converting scene batch operations to commands.
 */
struct DocumentSceneBatchEntityState {
    std::string name;
    EntityId parent = InvalidEntityId;
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

DocumentCreateEntityResult DocumentCommandService::createEntity(
        std::string name,
        EntityKind kind,
        EntityId parent,
        TransformComponent transform) {
    parent = normalizedCreateParent(scene_, parent);
    if (parent != InvalidEntityId && !scene_.exists(parent)) {
        DocumentCreateEntityResult result;
        assignDocumentCommandStatus(result, documentCommandInvalidRequestResult("Parent entity does not exist"));
        return result;
    }
    auto command = std::make_unique<CreateEntityCommand>(
            scene_,
            std::move(name),
            kind,
            parent,
            transform);
    CreateEntityCommand* createdCommand = command.get();
    DocumentCreateEntityResult result;
    assignDocumentCommandStatus(result, executeDocumentCommand(history_, std::move(command)));
    if (result.ok && result.changed) {
        result.entity = createdCommand->entity();
    }
    return result;
}

DocumentCommandResult DocumentCommandService::destroyEntity(EntityId entity) {
    if (!scene_.exists(entity)) {
        return documentCommandMissingEntityResult();
    }
    if (scene_.isSceneRoot(entity)) {
        return documentCommandSceneRootProtectedResult();
    }
    if (documentCommandEntityIsModelingOwned(scene_, entity)) {
        return documentCommandModelingOwnedEntityResult();
    }
    return executeDocumentCommand(
            history_,
            std::make_unique<DestroyEntityCommand>(scene_, entity));
}

DocumentCommandResult DocumentCommandService::reparentEntity(
        EntityId entity,
        EntityId parent,
        bool preserveWorldTransform) {
    if (!scene_.exists(entity)) {
        return documentCommandMissingEntityResult();
    }
    if (scene_.isSceneRoot(entity)) {
        return documentCommandSceneRootProtectedResult();
    }
    parent = normalizedHierarchyParent(scene_, entity, parent);
    if (parent != InvalidEntityId && !scene_.exists(parent)) {
        return documentCommandInvalidRequestResult("Parent entity does not exist");
    }
    if (scene_.parent(entity) == parent) {
        return documentCommandUnchangedResult();
    }
    return executeDocumentCommand(
            history_,
            std::make_unique<ReparentEntityCommand>(scene_, entity, parent, preserveWorldTransform));
}

DocumentGroupSelectionResult DocumentCommandService::groupSelection(
        std::string groupName,
        TransformComponent groupTransform) {
    auto command = std::make_unique<GroupSelectionCommand>(
            scene_,
            std::move(groupName),
            groupTransform);
    GroupSelectionCommand* groupCommand = command.get();
    DocumentGroupSelectionResult result;
    assignDocumentCommandStatus(result, executeDocumentCommand(history_, std::move(command)));
    if (result.ok && result.changed) {
        result.groupEntity = groupCommand->groupEntity();
    }
    return result;
}

DocumentCommandResult DocumentCommandService::ungroupSelection() {
    return executeDocumentCommand(
            history_,
            std::make_unique<UngroupSelectionCommand>(scene_));
}

DocumentDuplicateEntityResult DocumentCommandService::duplicateEntity(EntityId entity) {
    if (!scene_.exists(entity)) {
        DocumentDuplicateEntityResult result;
        assignDocumentCommandStatus(result, documentCommandMissingEntityResult());
        return result;
    }
    if (scene_.isSceneRoot(entity)) {
        DocumentDuplicateEntityResult result;
        assignDocumentCommandStatus(result, documentCommandSceneRootProtectedResult());
        return result;
    }
    if (documentCommandEntityIsModelingOwned(scene_, entity)) {
        DocumentDuplicateEntityResult result;
        assignDocumentCommandStatus(result, documentCommandModelingOwnedEntityResult());
        return result;
    }

    auto command = std::make_unique<DuplicateEntityCommand>(scene_, entity);
    DuplicateEntityCommand* duplicateCommand = command.get();
    DocumentDuplicateEntityResult result;
    assignDocumentCommandStatus(result, executeDocumentCommand(history_, std::move(command)));
    if (result.ok && result.changed) {
        result.duplicateEntity = duplicateCommand->duplicateEntity();
    }
    return result;
}

DocumentDuplicateHierarchyResult DocumentCommandService::duplicateSelectedHierarchy() {
    QDR_PROFILE_FUNCTION();
    if (documentCommandSelectionIncludesSceneRoot(scene_)) {
        DocumentDuplicateHierarchyResult result;
        assignDocumentCommandStatus(result, documentCommandSceneRootProtectedResult());
        return result;
    }
    if (documentCommandSelectedRootContainsModelingOwnedEntity(scene_)) {
        DocumentDuplicateHierarchyResult result;
        assignDocumentCommandStatus(result, documentCommandModelingOwnedEntityResult());
        return result;
    }

    auto command = std::make_unique<DuplicateSelectedHierarchyCommand>(scene_);
    DuplicateSelectedHierarchyCommand* duplicateCommand = command.get();
    DocumentDuplicateHierarchyResult result;
    {
        QDR_PROFILE_SCOPE("Duplicate execute selected hierarchy command");
        assignDocumentCommandStatus(result, executeDocumentCommand(history_, std::move(command)));
    }
    if (result.ok && result.changed) {
        result.duplicateRoots = duplicateCommand->duplicateRoots();
    }
    return result;
}

DocumentDuplicateHierarchyResult DocumentCommandService::duplicateHierarchyWithExistingClones(
        std::vector<EntityId> roots,
        std::unordered_map<EntityId, EntityId> existingClonesBySource) {
    QDR_PROFILE_FUNCTION();
    std::unordered_set<EntityId> seenRoots;
    std::vector<EntityId> existingRoots;
    existingRoots.reserve(roots.size());
    for (const EntityId root : roots) {
        if (root != InvalidEntityId && scene_.exists(root) && !scene_.isSceneRoot(root) && seenRoots.insert(root).second) {
            existingRoots.push_back(root);
        }
    }
    roots = std::move(existingRoots);
    if (roots.empty()) {
        DocumentDuplicateHierarchyResult result;
        assignDocumentCommandStatus(result, documentCommandUnchangedResult());
        return result;
    }

    std::vector<DocumentEntitySnapshot> sourceSnapshots;
    sourceSnapshots.reserve(roots.size());
    {
        QDR_PROFILE_SCOPE("Duplicate capture hierarchy snapshots");
        for (const EntityId root : roots) {
            std::optional<DocumentEntitySnapshot> snapshot = documentCommandCaptureEntitySnapshot(scene_, root);
            if (!snapshot.has_value()) {
                DocumentDuplicateHierarchyResult result;
                assignDocumentCommandStatus(result, documentCommandMissingEntityResult());
                return result;
            }
            sourceSnapshots.push_back(std::move(*snapshot));
        }
    }

    auto command = std::make_unique<DuplicateSelectedHierarchyCommand>(
            scene_,
            std::move(sourceSnapshots),
            std::move(existingClonesBySource));
    DuplicateSelectedHierarchyCommand* duplicateCommand = command.get();
    DocumentDuplicateHierarchyResult result;
    assignDocumentCommandStatus(result, executeDocumentCommand(history_, std::move(command)));
    if (result.ok && result.changed) {
        result.duplicateRoots = duplicateCommand->duplicateRoots();
    }
    return result;
}

DocumentCreateEntityResult DocumentCommandService::createPointLight(
        std::string name,
        LightComponent light,
        EntityId parent,
        TransformComponent transform,
        bool selectCreated) {
    light.type = LightType::Point;
    parent = normalizedCreateParent(scene_, parent);
    if (parent != InvalidEntityId && !scene_.exists(parent)) {
        DocumentCreateEntityResult result;
        assignDocumentCommandStatus(result, documentCommandInvalidRequestResult("Parent entity does not exist"));
        return result;
    }
    auto command = std::make_unique<CreateLightCommand>(
            scene_,
            "Create Point Light",
            std::move(name),
            light,
            parent,
            transform,
            selectCreated);
    CreateLightCommand* createdCommand = command.get();
    DocumentCreateEntityResult result;
    {
        QDR_PROFILE_SCOPE("Duplicate execute hierarchy command");
        assignDocumentCommandStatus(result, executeDocumentCommand(history_, std::move(command)));
    }
    if (result.ok && result.changed) {
        result.entity = createdCommand->entity();
    }
    return result;
}

DocumentCreateEntityResult DocumentCommandService::createDirectionalLight(
        std::string name,
        LightComponent light,
        EntityId parent,
        TransformComponent transform,
        bool selectCreated) {
    light.type = LightType::Directional;
    parent = normalizedCreateParent(scene_, parent);
    if (parent != InvalidEntityId && !scene_.exists(parent)) {
        DocumentCreateEntityResult result;
        assignDocumentCommandStatus(result, documentCommandInvalidRequestResult("Parent entity does not exist"));
        return result;
    }
    auto command = std::make_unique<CreateLightCommand>(
            scene_,
            "Create Directional Light",
            std::move(name),
            light,
            parent,
            transform,
            selectCreated);
    CreateLightCommand* createdCommand = command.get();
    DocumentCreateEntityResult result;
    assignDocumentCommandStatus(result, executeDocumentCommand(history_, std::move(command)));
    if (result.ok && result.changed) {
        result.entity = createdCommand->entity();
    }
    return result;
}

DocumentCommandResult DocumentCommandService::selectEntities(std::vector<EntityId> entities, EntityId active) {
    return executeDocumentCommand(
            history_,
            std::make_unique<SelectEntitiesCommand>(scene_, std::move(entities), active));
}

DocumentCommandResult DocumentCommandService::applyEntityTransform(EntityId entity, TransformComponent transform) {
    const TransformComponent* currentTransform = scene_.transform(entity);
    if (!currentTransform) {
        return documentCommandMissingEntityResult();
    }
    if (documentCommandSameTransform(*currentTransform, transform)) {
        return documentCommandUnchangedResult();
    }

    return executeDocumentCommand(
            history_,
            std::make_unique<ApplyEntityTransformCommand>(scene_, entity, *currentTransform, transform));
}

DocumentCommandResult DocumentCommandService::renameEntity(EntityId entity, std::string name) {
    const NameComponent* currentName = scene_.name(entity);
    if (!currentName) {
        return documentCommandMissingEntityResult();
    }
    if (currentName->name == name) {
        return documentCommandUnchangedResult();
    }
    if (scene_.isSceneRoot(entity)) {
        return documentCommandSceneRootProtectedResult();
    }

    const std::string before = currentName->name;
    return executeDocumentCommand(
            history_,
            std::make_unique<RenameEntityCommand>(scene_, entity, before, std::move(name)));
}

DocumentCommandResult DocumentCommandService::setVisible(EntityId entity, bool visible) {
    const VisibilityComponent* currentVisibility = scene_.visibility(entity);
    if (!currentVisibility) {
        return documentCommandMissingEntityResult();
    }
    if (currentVisibility->visible == visible) {
        return documentCommandUnchangedResult();
    }

    return setVisibility({DocumentVisibilityEdit{
            .entity = entity,
            .before = currentVisibility->visible,
            .after = visible,
    }});
}

DocumentCommandResult DocumentCommandService::setVisibility(std::vector<DocumentVisibilityEdit> edits) {
    edits = normalizedVisibilityEdits(scene_, std::move(edits));
    if (edits.empty()) {
        return documentCommandUnchangedResult();
    }

    std::vector<std::unique_ptr<IUndoableCommand>> commands;
    commands.reserve(edits.size());
    for (const DocumentVisibilityEdit& edit : edits) {
        commands.push_back(std::make_unique<SetVisibilityCommand>(scene_, edit.entity, edit.before, edit.after));
    }

    return executeDocumentCommand(
            history_,
            std::make_unique<BatchDocumentCommand>("Set Visibility", std::move(commands)));
}

DocumentCommandResult DocumentCommandService::setLocked(EntityId entity, bool locked) {
    const LockComponent* currentLock = scene_.lock(entity);
    if (!currentLock) {
        return documentCommandMissingEntityResult();
    }
    if (currentLock->locked == locked) {
        return documentCommandUnchangedResult();
    }

    return setLocks({DocumentLockEdit{
            .entity = entity,
            .before = currentLock->locked,
            .after = locked,
    }});
}

DocumentCommandResult DocumentCommandService::setLocks(std::vector<DocumentLockEdit> edits) {
    edits = normalizedLockEdits(scene_, std::move(edits));
    if (edits.empty()) {
        return documentCommandUnchangedResult();
    }

    std::vector<std::unique_ptr<IUndoableCommand>> commands;
    commands.reserve(edits.size() + 1u);
    for (const DocumentLockEdit& edit : edits) {
        commands.push_back(std::make_unique<SetLockCommand>(scene_, edit.entity, edit.before, edit.after));
    }
    appendSelectionCleanupAfterLockEdits(scene_, edits, commands);

    return executeDocumentCommand(
            history_,
            std::make_unique<BatchDocumentCommand>("Set Lock", std::move(commands)));
}

DocumentCommandResult DocumentCommandService::setMaterial(EntityId entity, AssetId material) {
    if (material == InvalidAssetId) {
        return documentCommandInvalidRequestResult("Material asset is invalid");
    }
    if (assets_ != nullptr && assets_->material(material) == nullptr) {
        return documentCommandInvalidRequestResult("Material asset does not exist");
    }
    if (!scene_.exists(entity)) {
        return documentCommandMissingEntityResult();
    }
    if (scene_.isSceneRoot(entity)) {
        return documentCommandSceneRootProtectedResult();
    }

    const MaterialComponent after{.material = material};
    std::optional<MaterialComponent> before;
    if (const MaterialComponent* currentMaterial = scene_.material(entity)) {
        if (*currentMaterial == after) {
            return documentCommandUnchangedResult();
        }
        before = *currentMaterial;
    }

    return executeDocumentCommand(
            history_,
            std::make_unique<SetMaterialCommand>(
                    scene_,
                    entity,
                    before,
                    after));
}

DocumentCommandResult DocumentCommandService::setMeshComponent(EntityId entity, MeshComponent mesh) {
    if (mesh.mesh == InvalidAssetId) {
        return documentCommandInvalidRequestResult("Mesh asset is invalid");
    }
    if (assets_ != nullptr && assets_->mesh(mesh.mesh) == nullptr) {
        return documentCommandInvalidRequestResult("Mesh asset does not exist");
    }
    const MeshComponent* currentMesh = scene_.mesh(entity);
    if (!currentMesh) {
        return documentCommandMissingEntityResult();
    }
    if (documentCommandEntityIsModelingOwned(scene_, entity) && currentMesh->mesh != mesh.mesh) {
        return documentCommandModelingOwnedEntityResult();
    }
    if (documentCommandSameMeshComponent(*currentMesh, mesh)) {
        return documentCommandUnchangedResult();
    }

    return executeDocumentCommand(
            history_,
            std::make_unique<SetMeshComponentCommand>(scene_, entity, *currentMesh, mesh));
}

DocumentCommandResult DocumentCommandService::setLightComponent(
        EntityId entity,
        LightComponent light,
        UndoRedoExecuteOptions options) {
    const LightComponent* currentLight = scene_.light(entity);
    if (!currentLight) {
        if (!scene_.exists(entity)) {
            return documentCommandMissingEntityResult();
        }
        return documentCommandInvalidRequestResult("Entity does not have a light component");
    }
    if (documentCommandSameLightComponent(*currentLight, light)) {
        return documentCommandUnchangedResult();
    }

    return executeDocumentCommand(
            history_,
            std::make_unique<SetLightComponentCommand>(scene_, entity, *currentLight, light),
            std::move(options));
}

DocumentCommandResult DocumentCommandService::setSceneSettings(
        EntityId entity,
        SceneSettingsComponent settings,
        UndoRedoExecuteOptions options) {
    const SceneSettingsComponent* currentSettings = scene_.sceneSettings(entity);
    if (!currentSettings) {
        if (!scene_.exists(entity)) {
            return documentCommandMissingEntityResult();
        }
        return documentCommandInvalidRequestResult("Entity does not have scene settings");
    }
    if (documentCommandSameSceneSettings(*currentSettings, settings)) {
        return documentCommandUnchangedResult();
    }

    return executeDocumentCommand(
            history_,
            std::make_unique<SetSceneSettingsCommand>(scene_, entity, *currentSettings, std::move(settings)),
            std::move(options));
}

DocumentCommandResult DocumentCommandService::setTransform(EntityId entity, TransformComponent transform) {
    const TransformComponent* currentTransform = scene_.transform(entity);
    if (!currentTransform) {
        return documentCommandMissingEntityResult();
    }
    if (documentCommandSameTransform(*currentTransform, transform)) {
        return documentCommandUnchangedResult();
    }

    return setTransforms({DocumentTransformEdit{
            .entity = entity,
            .before = *currentTransform,
            .after = transform,
    }});
}

DocumentCommandResult DocumentCommandService::setTransforms(
        std::vector<DocumentTransformEdit> edits,
        std::string label) {
    if (label.empty()) {
        label = "Set Transform";
    }
    edits = normalizedTransformEdits(scene_, std::move(edits));
    if (edits.empty()) {
        return documentCommandUnchangedResult();
    }

    std::vector<std::unique_ptr<IUndoableCommand>> commands;
    commands.reserve(edits.size());
    for (const DocumentTransformEdit& edit : edits) {
        commands.push_back(std::make_unique<ApplyEntityTransformCommand>(
                scene_,
                edit.entity,
                edit.before,
                edit.after,
                label));
    }

    return executeDocumentCommand(
            history_,
            std::make_unique<BatchDocumentCommand>(std::move(label), std::move(commands)));
}

DocumentCommandResult DocumentCommandService::applySceneBatch(
        std::string label,
        std::vector<DocumentSceneBatchOperation> operations) {
    if (label.empty()) {
        label = "Scene Batch";
    }

    std::vector<std::unique_ptr<IUndoableCommand>> commands;
    commands.reserve(operations.size() + 1u);
    std::vector<DocumentLockEdit> lockEdits;
    std::unordered_map<EntityId, DocumentSceneBatchEntityState> simulated;

    auto failure = [](std::string message) {
        return documentCommandInvalidRequestResult(std::move(message));
    };

    auto simulatedState = [&](EntityId entity)
            -> std::optional<std::reference_wrapper<DocumentSceneBatchEntityState>> {
        if (const auto existing = simulated.find(entity); existing != simulated.end()) {
            return existing->second;
        }
        const NameComponent* name = scene_.name(entity);
        const TransformComponent* transform = scene_.transform(entity);
        const VisibilityComponent* visibility = scene_.visibility(entity);
        const LockComponent* lock = scene_.lock(entity);
        if (name == nullptr || transform == nullptr || visibility == nullptr || lock == nullptr) {
            return std::nullopt;
        }
        auto [inserted, _] = simulated.emplace(entity, DocumentSceneBatchEntityState{
                .name = name->name,
                .parent = scene_.parent(entity),
                .transform = *transform,
                .visible = visibility->visible,
                .locked = lock->locked,
        });
        return inserted->second;
    };

    auto simulatedParent = [&](EntityId entity) {
        if (std::optional<std::reference_wrapper<DocumentSceneBatchEntityState>> state = simulatedState(entity)) {
            return state->get().parent;
        }
        return scene_.parent(entity);
    };

    auto simulatedLocked = [&](EntityId entity) {
        if (std::optional<std::reference_wrapper<DocumentSceneBatchEntityState>> state = simulatedState(entity)) {
            return state->get().locked;
        }
        const LockComponent* lock = scene_.lock(entity);
        return lock != nullptr && lock->locked;
    };

    auto simulatedEffectivelyLocked = [&](EntityId entity) {
        std::unordered_set<EntityId> visited;
        while (entity != InvalidEntityId && scene_.exists(entity)) {
            if (!visited.insert(entity).second) {
                return true;
            }
            if (simulatedLocked(entity)) {
                return true;
            }
            entity = simulatedParent(entity);
        }
        return false;
    };

    for (const DocumentSceneBatchOperation& operation : operations) {
        if (operation.entity == InvalidEntityId || !scene_.exists(operation.entity)) {
            return failure("Batch entity does not exist.");
        }

        switch (operation.kind) {
        case DocumentSceneBatchOperationKind::Rename: {
            if (scene_.isSceneRoot(operation.entity)) {
                return failure("Scene root cannot be renamed.");
            }
            std::optional<std::reference_wrapper<DocumentSceneBatchEntityState>> state =
                    simulatedState(operation.entity);
            if (!state.has_value()) {
                return failure("Batch entity cannot be renamed.");
            }
            if (state->get().name == operation.name) {
                break;
            }
            commands.push_back(std::make_unique<RenameEntityCommand>(
                    scene_,
                    operation.entity,
                    state->get().name,
                    operation.name));
            state->get().name = operation.name;
            break;
        }
        case DocumentSceneBatchOperationKind::Reparent: {
            if (scene_.isSceneRoot(operation.entity)) {
                return failure("Scene root cannot be reparented.");
            }
            const EntityId parent = normalizedHierarchyParent(scene_, operation.entity, operation.parent);
            if (parent != InvalidEntityId && !scene_.exists(parent)) {
                return failure("Batch parent entity does not exist.");
            }
            std::optional<std::reference_wrapper<DocumentSceneBatchEntityState>> state =
                    simulatedState(operation.entity);
            if (!state.has_value()) {
                return failure("Batch entity cannot be reparented.");
            }
            if (state->get().parent == parent) {
                break;
            }

            commands.push_back(std::make_unique<ReparentEntityCommand>(
                    scene_,
                    operation.entity,
                    parent,
                    operation.preserveWorldTransform));
            state->get().parent = parent;
            break;
        }
        case DocumentSceneBatchOperationKind::SetTransform: {
            std::optional<std::reference_wrapper<DocumentSceneBatchEntityState>> state =
                    simulatedState(operation.entity);
            if (!state.has_value()) {
                return failure("Batch entity has no transform.");
            }
            if (simulatedEffectivelyLocked(operation.entity) ||
                    documentCommandSameTransform(state->get().transform, operation.transform)) {
                break;
            }
            commands.push_back(std::make_unique<ApplyEntityTransformCommand>(
                    scene_,
                    operation.entity,
                    state->get().transform,
                    operation.transform,
                    label));
            state->get().transform = operation.transform;
            break;
        }
        case DocumentSceneBatchOperationKind::SetVisible: {
            if (scene_.isSceneRoot(operation.entity)) {
                return failure("Scene root visibility cannot be changed.");
            }
            std::optional<std::reference_wrapper<DocumentSceneBatchEntityState>> state =
                    simulatedState(operation.entity);
            if (!state.has_value()) {
                return failure("Batch entity has no visibility component.");
            }
            if (state->get().visible == operation.flag) {
                break;
            }
            commands.push_back(std::make_unique<SetVisibilityCommand>(
                    scene_,
                    operation.entity,
                    state->get().visible,
                    operation.flag));
            state->get().visible = operation.flag;
            break;
        }
        case DocumentSceneBatchOperationKind::SetLocked: {
            if (scene_.isSceneRoot(operation.entity)) {
                return failure("Scene root lock cannot be changed.");
            }
            std::optional<std::reference_wrapper<DocumentSceneBatchEntityState>> state =
                    simulatedState(operation.entity);
            if (!state.has_value()) {
                return failure("Batch entity has no lock component.");
            }
            if (state->get().locked == operation.flag) {
                break;
            }
            commands.push_back(std::make_unique<SetLockCommand>(
                    scene_,
                    operation.entity,
                    state->get().locked,
                    operation.flag));
            lockEdits.push_back(DocumentLockEdit{
                    .entity = operation.entity,
                    .before = state->get().locked,
                    .after = operation.flag,
            });
            state->get().locked = operation.flag;
            break;
        }
        }
    }

    appendSelectionCleanupAfterLockEdits(scene_, lockEdits, commands);

    if (commands.empty()) {
        return documentCommandUnchangedResult();
    }

    return executeDocumentCommand(
            history_,
            std::make_unique<BatchDocumentCommand>(std::move(label), std::move(commands)));
}

} // namespace quader
