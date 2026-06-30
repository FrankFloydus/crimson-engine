////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/selection/editor_selection_service.hpp"

#include "editor/commands/document_command_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/modeling/services/selection_mode_adapter.hpp"
#include "scene/scene_document.hpp"

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

namespace quader {
namespace {

bool hasSceneOnlySelection(const SceneDocument& scene, const std::vector<EntityId>& selected) {
    return std::any_of(selected.begin(), selected.end(), [&](EntityId entity) {
        return scene.modelingObject(entity) == nullptr;
    });
}

DocumentCommandResult mergeModelingSelectionChange(
        DocumentCommandResult result,
        const ModelingSelectionSummary& before,
        const ModelingSelectionSummary& after) {
    if (result.ok &&
            (before.contentRevision != after.contentRevision ||
                    before.selectionRevision != after.selectionRevision)) {
        result.changed = true;
    }
    return result;
}

bool sceneEntitySelectableForUserSelection(const SceneDocument& scene, EntityId entity) {
    if (entity == InvalidEntityId ||
            !scene.exists(entity)) {
        return false;
    }
    if (scene.isSceneRoot(entity)) {
        return true;
    }
    if (scene.isEffectivelyLocked(entity)) {
        return false;
    }
    const VisibilityComponent* visibility = scene.visibility(entity);
    return visibility == nullptr || visibility->selectable;
}

std::vector<EntityId> userSelectableSceneEntities(
        const SceneDocument& scene,
        std::span<const EntityId> entities) {
    std::vector<EntityId> result;
    result.reserve(entities.size());
    for (const EntityId entity : entities) {
        if (!sceneEntitySelectableForUserSelection(scene, entity) ||
                std::find(result.begin(), result.end(), entity) != result.end()) {
            continue;
        }
        result.push_back(entity);
    }
    return result;
}

EntityId userSelectableActiveEntity(
        const SceneDocument& scene,
        const std::vector<EntityId>& entities,
        EntityId active) {
    if (active != InvalidEntityId &&
            sceneEntitySelectableForUserSelection(scene, active) &&
            std::find(entities.begin(), entities.end(), active) != entities.end()) {
        return active;
    }
    return entities.empty() ? InvalidEntityId : entities.back();
}

std::vector<EntityId> userSelectableSceneEntitiesForMode(
        const SceneDocument& scene,
        ModelingSelectionMode mode) {
    std::vector<EntityId> result;
    for (const EntityId entity : scene.entityTreeOrder()) {
        if (scene.isSceneRoot(entity) || !sceneEntitySelectableForUserSelection(scene, entity)) {
            continue;
        }
        if (mode == ModelingSelectionMode::Group && scene.kind(entity) != EntityKind::Empty) {
            continue;
        }
        result.push_back(entity);
    }
    return result;
}

ModelingCommandResult modelingCommandResultFromDocument(DocumentCommandResult result) {
    return {
            .ok = result.ok,
            .changed = result.changed,
            .message = std::move(result.message),
    };
}

/**
 * Stores normalized scene selection contents after applying an add/remove/toggle/replace edit.
 */
struct ResolvedSceneSelectionEdit {
    std::vector<EntityId> selected;
    EntityId active = InvalidEntityId;
};

ResolvedSceneSelectionEdit resolveSceneSelectionEdit(
        std::span<const EntityId> currentSelection,
        std::span<const EntityId> editedEntities,
        EditorSelectionEdit edit) {
    ResolvedSceneSelectionEdit result;
    if (edit == EditorSelectionEdit::Replace) {
        result.selected.reserve(editedEntities.size());
        for (const EntityId entity : editedEntities) {
            if (entity == InvalidEntityId ||
                    std::find(result.selected.begin(), result.selected.end(), entity) != result.selected.end()) {
                continue;
            }
            result.selected.push_back(entity);
            result.active = entity;
        }
        return result;
    }

    result.selected.assign(currentSelection.begin(), currentSelection.end());
    result.active = result.selected.empty() ? InvalidEntityId : result.selected.back();
    for (const EntityId entity : editedEntities) {
        const auto found = std::find(result.selected.begin(), result.selected.end(), entity);
        const bool alreadySelected = found != result.selected.end();
        if (edit == EditorSelectionEdit::Add || (edit == EditorSelectionEdit::Toggle && !alreadySelected)) {
            if (entity != InvalidEntityId && !alreadySelected) {
                result.selected.push_back(entity);
                result.active = entity;
            }
        } else if ((edit == EditorSelectionEdit::Remove || edit == EditorSelectionEdit::Toggle) && alreadySelected) {
            result.selected.erase(found);
            result.active = result.selected.empty() ? InvalidEntityId : result.selected.back();
        }
    }
    return result;
}

EditorSelectionEdit toEditorSelectionEdit(ModelingSelectionEdit edit) {
    switch (edit) {
    case ModelingSelectionEdit::Add:
        return EditorSelectionEdit::Add;
    case ModelingSelectionEdit::Remove:
        return EditorSelectionEdit::Remove;
    case ModelingSelectionEdit::Toggle:
        return EditorSelectionEdit::Toggle;
    case ModelingSelectionEdit::Replace:
        return EditorSelectionEdit::Replace;
    }
    return EditorSelectionEdit::Replace;
}

} // namespace

EditorSelectionService::EditorSelectionService(
        SceneDocument& scene,
        DocumentCommandService& documentCommands,
        ModelingSessionService& modeling)
    : scene_(scene)
    , documentCommands_(documentCommands)
    , modeling_(modeling) {}

EditorSelectionState EditorSelectionService::state() const {
    const ModelingSelectionSummary currentSummary = summary();
    return {
            .mode = currentSummary.mode,
            .summary = currentSummary,
            .target = targetFromSummary(currentSummary),
            .sceneSelection = scene_.selectedEntities(),
            .activeEntity = scene_.primarySelection(),
    };
}

EditorSelectionTarget EditorSelectionService::target() const {
    return targetFromSummary(summary());
}

ModelingSelectionSummary EditorSelectionService::summary() const {
    const ModelingSelectionSummary current = modeling_.selectionSummary();
    const std::vector<EntityId> selected = scene_.selectedEntities();
    if (!selected.empty() && hasSceneOnlySelection(scene_, selected)) {
        return sceneSummaryFor(current.mode == ModelingSelectionMode::Group
                ? ModelingSelectionMode::Group
                : ModelingSelectionMode::Object);
    }
    if (current.hasSelection && current.activeObjectId != InvalidModelingObjectId) {
        const std::optional<EntityId> activeEntity = modeling_.entityForModelingObject(current.activeObjectId);
        if (activeEntity.has_value() && scene_.isEffectivelyLocked(*activeEntity)) {
            return sceneSummaryFor(current.mode);
        }
    }
    return current;
}

ModelingHoverSummary EditorSelectionService::hoverSummary() const {
    return modeling_.selectionHoverSummary();
}

ModelingSelectionMode EditorSelectionService::mode() const {
    return modeling_.selectionMode();
}

DocumentCommandResult EditorSelectionService::setSceneSelection(std::span<const EntityId> entities, EntityId active) {
    const ModelingSelectionSummary before = modeling_.selectionSummary();
    std::vector<EntityId> selectable = userSelectableSceneEntities(scene_, entities);
    const EntityId selectableActive = userSelectableActiveEntity(scene_, selectable, active);
    DocumentCommandResult result =
            documentCommands_.selectEntities(std::move(selectable), selectableActive);
    if (result.ok && modeling_.selectionMode() != ModelingSelectionMode::Group) {
        modeling_.projectObjectSelectionFromScene(true);
    }
    return mergeModelingSelectionChange(result, before, modeling_.selectionSummary());
}

DocumentCommandResult EditorSelectionService::editSceneSelection(EntityId entity, EditorSelectionEdit edit) {
    const std::array<EntityId, 1> entities{{entity}};
    return editSceneSelection(entities, edit);
}

DocumentCommandResult EditorSelectionService::editSceneSelection(
        std::span<const EntityId> entities,
        EditorSelectionEdit edit) {
    const std::vector<EntityId> current = scene_.selectedEntities();
    const ResolvedSceneSelectionEdit resolved = resolveSceneSelectionEdit(current, entities, edit);
    return setSceneSelection(resolved.selected, resolved.active);
}

ModelingCommandResult EditorSelectionService::setObjectSelection(std::span<const EntityId> entities, EntityId active) {
    const DocumentCommandResult result = setSceneSelection(entities, active);
    return {
            .ok = result.ok,
            .changed = result.changed,
            .message = result.message,
    };
}

ModelingCommandResult EditorSelectionService::setComponentSelection(const ModelingComponentSelectionRequest& request) {
    return modeling_.setComponentSelection(request);
}

ModelingCommandResult EditorSelectionService::setMode(ModelingSelectionMode mode) {
    return modeling_.setSelectionMode(mode);
}

ModelingCommandResult EditorSelectionService::selectAll() {
    const ModelingSelectionMode selectionMode = modeling_.selectionMode();
    if (!selectionModeUsesSceneDocument(selectionMode)) {
        return modeling_.selectAll();
    }

    std::vector<EntityId> selected = userSelectableSceneEntitiesForMode(scene_, selectionMode);
    const EntityId active = selected.empty() ? InvalidEntityId : selected.back();
    return modelingCommandResultFromDocument(setSceneSelection(selected, active));
}

ModelingCommandResult EditorSelectionService::invert() {
    const ModelingSelectionMode selectionMode = modeling_.selectionMode();
    if (!selectionModeUsesSceneDocument(selectionMode)) {
        return modeling_.invertSelection();
    }

    const std::vector<EntityId> current = scene_.selectedEntities();
    std::vector<EntityId> selected;
    for (const EntityId entity : userSelectableSceneEntitiesForMode(scene_, selectionMode)) {
        if (std::find(current.begin(), current.end(), entity) == current.end()) {
            selected.push_back(entity);
        }
    }
    const EntityId active = selected.empty() ? InvalidEntityId : selected.back();
    return modelingCommandResultFromDocument(setSceneSelection(selected, active));
}

ModelingCommandResult EditorSelectionService::clear() {
    const ModelingSelectionMode selectionMode = modeling_.selectionMode();
    if (!selectionModeUsesSceneDocument(selectionMode)) {
        return modeling_.clearSelection();
    }

    return modelingCommandResultFromDocument(setSceneSelection(std::span<const EntityId>{}, InvalidEntityId));
}

EditorSelectionBoxBaseline EditorSelectionService::captureBoxSelectionBaseline() const {
    EditorSelectionBoxBaseline baseline;
    baseline.mode = modeling_.selectionMode();
    baseline.sceneSelection = scene_.selectedEntities();
    baseline.activeEntity = scene_.primarySelection();
    if (baseline.mode != ModelingSelectionMode::Group) {
        baseline.modelingSelection = modeling_.captureBoxSelectionBaseline();
    }
    return baseline;
}

ModelingSelectionSummary EditorSelectionService::pickSelect(const ModelingPickSelectRequest& request) {
    if (modeling_.selectionMode() == ModelingSelectionMode::Group) {
        const std::optional<EntityId> hitMesh = modeling_.pickObjectEntity(request);
        return scenePickSelection(
                nearestGroupAncestor(hitMesh.value_or(InvalidEntityId)),
                ModelingSelectionMode::Group,
                request);
    }

    return modeling_.pickSelect(request);
}

ModelingSelectionSummary EditorSelectionService::boxSelect(const ModelingBoxSelectRequest& request) {
    return boxSelect(request, nullptr);
}

ModelingSelectionSummary EditorSelectionService::boxSelect(
        const ModelingBoxSelectRequest& request,
        const EditorSelectionBoxBaseline& baseline) {
    return boxSelect(request, &baseline);
}

ModelingSelectionSummary EditorSelectionService::boxSelect(
        const ModelingBoxSelectRequest& request,
        const EditorSelectionBoxBaseline* baseline) {
    if (modeling_.selectionMode() == ModelingSelectionMode::Group) {
        std::vector<EntityId> groups;
        for (const EntityId meshEntity : modeling_.boxPickObjectEntities(request)) {
            const EntityId group = nearestGroupAncestor(meshEntity);
            if (group != InvalidEntityId &&
                    std::find(groups.begin(), groups.end(), group) == groups.end()) {
                groups.push_back(group);
            }
        }
        return sceneBoxSelection(groups, ModelingSelectionMode::Group, request.edit, baseline);
    }

    if (baseline != nullptr &&
            baseline->modelingSelection.has_value() &&
            baseline->modelingSelection->valid() &&
            baseline->mode == modeling_.selectionMode()) {
        return modeling_.boxSelect(request, *baseline->modelingSelection);
    }
    return modeling_.boxSelect(request);
}

ModelingCommandResult EditorSelectionService::updateHover(const ModelingHoverRequest& request) {
    return modeling_.updateSelectionHover(request);
}

ModelingCommandResult EditorSelectionService::setHover(const ModelingDirectHoverRequest& request) {
    return modeling_.setSelectionHover(request);
}

ModelingHoverSummary EditorSelectionService::previewHover(const ModelingHoverRequest& request) const {
    return modeling_.previewSelectionHover(request);
}

ModelingCommandResult EditorSelectionService::clearHover() {
    return modeling_.clearSelectionHover();
}

EditorSelectionTarget EditorSelectionService::targetFromSummary(const ModelingSelectionSummary& summary) const {
    auto activeSceneTarget = [&]() {
        const EntityId active = scene_.primarySelection();
        if (active == InvalidEntityId || !scene_.exists(active)) {
            return EditorSelectionTarget{};
        }
        return EditorSelectionTarget{
                .kind = scene_.kind(active) == EntityKind::Empty ? EditorSelectionTargetKind::Group : EditorSelectionTargetKind::Object,
                .entity = active,
                .modelingObjectId = scene_.modelingObject(active) != nullptr ? scene_.modelingObject(active)->objectId : InvalidModelingObjectId,
        };
    };

    if (!summary.hasSelection) {
        return {};
    }

    if (summary.mode == ModelingSelectionMode::Group) {
        EditorSelectionTarget target = activeSceneTarget();
        target.kind = target.entity == InvalidEntityId ? EditorSelectionTargetKind::None : EditorSelectionTargetKind::Group;
        return target;
    }

    const auto activeEntity = summary.activeObjectId > 0
            ? modeling_.entityForModelingObject(summary.activeObjectId)
            : std::optional<EntityId>{};
    const EntityId entity = activeEntity.value_or(scene_.primarySelection());

    switch (summary.activeKind) {
    case ModelingActiveSelectionKind::Object:
        return entity == InvalidEntityId ? activeSceneTarget() : EditorSelectionTarget{
                .kind = EditorSelectionTargetKind::Object,
                .entity = entity,
                .modelingObjectId = summary.activeObjectId,
        };
    case ModelingActiveSelectionKind::Vertex:
        return {
                .kind = EditorSelectionTargetKind::Vertex,
                .entity = entity,
                .modelingObjectId = summary.activeObjectId,
                .vertex = summary.activeVertexId,
        };
    case ModelingActiveSelectionKind::Edge:
        return {
                .kind = EditorSelectionTargetKind::Edge,
                .entity = entity,
                .modelingObjectId = summary.activeObjectId,
                .edgeA = summary.activeEdgeA,
                .edgeB = summary.activeEdgeB,
        };
    case ModelingActiveSelectionKind::Face:
        return {
                .kind = EditorSelectionTargetKind::Face,
                .entity = entity,
                .modelingObjectId = summary.activeObjectId,
                .face = summary.activeFaceId,
        };
    case ModelingActiveSelectionKind::Light:
    case ModelingActiveSelectionKind::None:
        break;
    }

    return activeSceneTarget();
}

ModelingSelectionSummary EditorSelectionService::scenePickSelection(
        EntityId targetEntity,
        ModelingSelectionMode mode,
        const ModelingPickSelectRequest& request) {
    if (!sceneEntitySelectableForUserSelection(scene_, targetEntity)) {
        targetEntity = InvalidEntityId;
    }
    const bool targetSelected =
            targetEntity != InvalidEntityId &&
            scene_.selection(targetEntity) != nullptr &&
            scene_.selection(targetEntity)->selected;
    const bool preserveSelectedHit = request.preserveSelectionOnSelectedHit && targetSelected;
    const std::vector<EntityId> previousSelected = scene_.selectedEntities();
    std::vector<EntityId> selected = previousSelected;

    const bool additiveToggle = request.controlPressed || request.commandPressed;
    if (!additiveToggle &&
            !request.shiftPressed &&
            !preserveSelectedHit &&
            (!request.preserveSelectionOnMiss || targetEntity != InvalidEntityId)) {
        selected.clear();
    }
    if (targetEntity != InvalidEntityId && !preserveSelectedHit) {
        const auto found = std::find(selected.begin(), selected.end(), targetEntity);
        const bool alreadySelected = found != selected.end();
        if (additiveToggle && alreadySelected) {
            selected.erase(found);
        } else if (!alreadySelected) {
            selected.push_back(targetEntity);
        }
    }

    if (selected != previousSelected) {
        const EntityId active = selected.empty() ? InvalidEntityId :
                (targetEntity != InvalidEntityId &&
                                std::find(selected.begin(), selected.end(), targetEntity) != selected.end()
                        ? targetEntity
                        : selected.back());
        (void)setSceneSelection(std::span<const EntityId>(selected.data(), selected.size()), active);
    }

    const ModelingSelectionSummary current = modeling_.selectionSummary();
    return current.mode == mode ? current : sceneSummaryFor(mode);
}

ModelingSelectionSummary EditorSelectionService::sceneBoxSelection(
        std::span<const EntityId> targetEntities,
        ModelingSelectionMode mode,
        ModelingSelectionEdit edit,
        const EditorSelectionBoxBaseline* baseline) {
    const std::vector<EntityId> previousSelected =
            (baseline != nullptr && baseline->mode == mode)
            ? baseline->sceneSelection
            : scene_.selectedEntities();
    const ResolvedSceneSelectionEdit resolved =
            resolveSceneSelectionEdit(previousSelected, targetEntities, toEditorSelectionEdit(edit));
    if (resolved.selected != previousSelected) {
        (void)setSceneSelection(
                std::span<const EntityId>(resolved.selected.data(), resolved.selected.size()),
                resolved.active);
    }

    const ModelingSelectionSummary current = modeling_.selectionSummary();
    return current.mode == mode ? current : sceneSummaryFor(mode);
}

EntityId EditorSelectionService::nearestGroupAncestor(EntityId entity) const {
    EntityId parent = scene_.parent(entity);
    while (parent != InvalidEntityId) {
        if (!scene_.isSceneRoot(parent) && scene_.kind(parent) == EntityKind::Empty) {
            return parent;
        }
        parent = scene_.parent(parent);
    }
    return InvalidEntityId;
}

ModelingSelectionSummary EditorSelectionService::sceneSummaryFor(ModelingSelectionMode mode) const {
    const std::vector<EntityId> selected = scene_.selectedEntities();
    const EntityId active = scene_.primarySelection();
    const bool activeIsLight = active != InvalidEntityId && scene_.light(active) != nullptr;
    return {
            .mode = mode,
            .hasSelection = !selected.empty(),
            .activeKind = selected.empty()
                    ? ModelingActiveSelectionKind::None
                    : (activeIsLight ? ModelingActiveSelectionKind::Light : ModelingActiveSelectionKind::Object),
            .selectedObjectCount = static_cast<std::uint32_t>(selected.size()),
            .contentRevision = modeling_.selectionSummary().contentRevision,
            .selectionRevision = scene_.revision(),
    };
}

} // namespace quader
