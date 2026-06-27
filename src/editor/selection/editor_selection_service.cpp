////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/selection/editor_selection_service.hpp"

#include "assets/asset_registry.hpp"
#include "assets/mesh_asset.hpp"
#include "editor/commands/document_command_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/interactions/selection_box.hpp"
#include "editor/interactions/viewport_projection.hpp"
#include "editor/selection/box_selection_rules.hpp"
#include "math/glm_bridge.hpp"
#include "scene/scene_bounds.hpp"
#include "scene/scene_document.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace quader {
namespace {

bool rayIntersectsBounds(QVec3 origin, QVec3 direction, const SceneBounds& bounds, float& outDistance) {
    float tMin = 0.0f;
    float tMax = std::numeric_limits<float>::max();

    auto intersectAxis = [&](float originAxis, float directionAxis, float minAxis, float maxAxis) {
        if (std::abs(directionAxis) <= 0.000001f) {
            return originAxis >= minAxis && originAxis <= maxAxis;
        }

        float t1 = (minAxis - originAxis) / directionAxis;
        float t2 = (maxAxis - originAxis) / directionAxis;
        if (t1 > t2) {
            std::swap(t1, t2);
        }
        tMin = std::max(tMin, t1);
        tMax = std::min(tMax, t2);
        return tMin <= tMax;
    };

    if (!intersectAxis(origin.x, direction.x, bounds.min.x, bounds.max.x)) {
        return false;
    }
    if (!intersectAxis(origin.y, direction.y, bounds.min.y, bounds.max.y)) {
        return false;
    }
    if (!intersectAxis(origin.z, direction.z, bounds.min.z, bounds.max.z)) {
        return false;
    }

    outDistance = tMin;
    return tMax >= 0.0f;
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

quader_editor_core::ViewportCameraState toRuntimeCamera(const ModelingViewportCamera& value) {
    quader_editor_core::ViewportCameraState camera;
    camera.projection = value.orthographic
            ? quader_editor_core::CameraProjection::orthographic
            : quader_editor_core::CameraProjection::perspective;
    camera.transform = {
            .x_axis = value.xAxis,
            .y_axis = value.yAxis,
            .z_axis = value.zAxis,
            .origin = value.origin,
    };
    camera.viewport_size = {
            std::max(value.viewportSize.x, 1.0f),
            std::max(value.viewportSize.y, 1.0f),
    };
    camera.orthographic_size = std::max(value.orthographicSize, 0.0001);
    camera.fov_degrees = value.fovDegrees;
    return camera;
}

quader_editor_core::RectangleSelectionMatch rectangleMatch(const ModelingBoxSelectRequest& request) {
    switch (resolveBoxSelectionRule(request.rule, request.start, request.end)) {
    case ModelingBoxSelectionRule::Contain:
        return quader_editor_core::RectangleSelectionMatch::contain;
    case ModelingBoxSelectionRule::Crossing:
        return quader_editor_core::RectangleSelectionMatch::crossing;
    case ModelingBoxSelectionRule::DragDirection:
        break;
    }
    return quader_editor_core::RectangleSelectionMatch::crossing;
}

std::array<QVec3, 8> boundsCorners(const SceneBounds& bounds) {
    return {{
            {bounds.min.x, bounds.min.y, bounds.min.z},
            {bounds.max.x, bounds.min.y, bounds.min.z},
            {bounds.max.x, bounds.max.y, bounds.min.z},
            {bounds.min.x, bounds.max.y, bounds.min.z},
            {bounds.min.x, bounds.min.y, bounds.max.z},
            {bounds.max.x, bounds.min.y, bounds.max.z},
            {bounds.max.x, bounds.max.y, bounds.max.z},
            {bounds.min.x, bounds.max.y, bounds.max.z},
    }};
}

void includeProjectedObjectPoint(
        quader_editor_core::RectangleProjectedObject& projected,
        QVec2 screen) {
    projected.screen_points.push_back(screen);
    if (!projected.has_screen_bounds) {
        projected.screen_min = screen;
        projected.screen_max = screen;
        projected.has_screen_bounds = true;
    } else {
        projected.screen_min.x = std::min(projected.screen_min.x, screen.x);
        projected.screen_min.y = std::min(projected.screen_min.y, screen.y);
        projected.screen_max.x = std::max(projected.screen_max.x, screen.x);
        projected.screen_max.y = std::max(projected.screen_max.y, screen.y);
    }
}

} // namespace

EditorSelectionService::EditorSelectionService(
        SceneDocument& scene,
        DocumentCommandService& documentCommands,
        AssetRegistry& assets,
        ModelingSessionService& modeling)
    : scene_(scene)
    , documentCommands_(documentCommands)
    , assets_(assets)
    , modeling_(modeling) {}

EditorSelectionState EditorSelectionService::state() const {
    const ModelingSelectionSummary currentSummary = modeling_.selectionSummary();
    return {
            .mode = currentSummary.mode,
            .summary = currentSummary,
            .target = targetFromSummary(currentSummary),
            .sceneSelection = scene_.selectedEntities(),
            .activeEntity = scene_.primarySelection(),
    };
}

EditorSelectionTarget EditorSelectionService::target() const {
    return targetFromSummary(modeling_.selectionSummary());
}

ModelingSelectionSummary EditorSelectionService::summary() const {
    return modeling_.selectionSummary();
}

ModelingHoverSummary EditorSelectionService::hoverSummary() const {
    return modeling_.selectionHoverSummary();
}

ModelingSelectionMode EditorSelectionService::mode() const {
    return modeling_.selectionMode();
}

DocumentCommandResult EditorSelectionService::setSceneSelection(std::span<const EntityId> entities, EntityId active) {
    const ModelingSelectionSummary before = modeling_.selectionSummary();
    DocumentCommandResult result =
            documentCommands_.selectEntities(std::vector<EntityId>(entities.begin(), entities.end()), active);
    if (result.ok && modeling_.selectionMode() != ModelingSelectionMode::Group) {
        modeling_.projectObjectSelectionFromScene();
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
    return modeling_.setObjectSelection(entities, active);
}

ModelingCommandResult EditorSelectionService::setComponentSelection(const ModelingComponentSelectionRequest& request) {
    return modeling_.setComponentSelection(request);
}

ModelingCommandResult EditorSelectionService::setMode(ModelingSelectionMode mode) {
    return modeling_.setSelectionMode(mode);
}

ModelingCommandResult EditorSelectionService::selectAll() {
    return modeling_.selectAll();
}

ModelingCommandResult EditorSelectionService::invert() {
    return modeling_.invertSelection();
}

ModelingCommandResult EditorSelectionService::clear() {
    return modeling_.clearSelection();
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
        const EntityId hitMesh = pickMeshEntityByBounds(request.ray);
        return scenePickSelection(nearestGroupAncestor(hitMesh), ModelingSelectionMode::Group, request);
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
        for (const EntityId meshEntity : boxPickMeshEntitiesByBounds(request)) {
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

EntityId EditorSelectionService::pickMeshEntityByBounds(const ModelingRay& ray) const {
    EntityId bestEntity = InvalidEntityId;
    float bestDistance = std::numeric_limits<float>::max();

    for (const EntityId id : scene_.entities()) {
        const VisibilityComponent* visibility = scene_.visibility(id);
        if (!scene_.isEffectivelyVisible(id) ||
                (visibility != nullptr && !visibility->selectable)) {
            continue;
        }

        const MeshComponent* meshComponent = scene_.mesh(id);
        if (!meshComponent || scene_.transform(id) == nullptr) {
            continue;
        }

        const MeshAsset* mesh = assets_.mesh(meshComponent->mesh);
        if (!mesh) {
            continue;
        }

        const std::optional<SceneBounds> bounds = localMeshBounds(*mesh);
        if (!bounds) {
            continue;
        }

        const QMat4 worldFromLocal = scene_.worldTransformMatrix(id);
        const QMat4 localFromWorld = inverse(worldFromLocal);
        const QVec3 localOrigin = transformPoint(localFromWorld, ray.origin);
        const QVec3 unnormalizedLocalDirection = transformVector(localFromWorld, ray.direction);
        if (length(unnormalizedLocalDirection) <= 0.000001f) {
            continue;
        }
        const QVec3 localDirection = normalize(unnormalizedLocalDirection);

        float localDistance = 0.0f;
        if (!rayIntersectsBounds(localOrigin, localDirection, *bounds, localDistance)) {
            continue;
        }

        const QVec3 localHit = localOrigin + localDirection * localDistance;
        const QVec3 worldHit = transformPoint(worldFromLocal, localHit);
        const float worldDistance = length(worldHit - ray.origin);
        if (worldDistance < bestDistance) {
            bestDistance = worldDistance;
            bestEntity = id;
        }
    }

    return bestEntity;
}

std::vector<EntityId> EditorSelectionService::boxPickMeshEntitiesByBounds(const ModelingBoxSelectRequest& request) const {
    const quader_editor_core::ViewportCameraState camera = toRuntimeCamera(request.camera);
    quader_editor_core::RectangleProjectedHitsRequest hitsRequest;
    hitsRequest.mode = quader_editor_core::SelectionMode::object;
    hitsRequest.match = rectangleMatch(request);
    hitsRequest.rect = quader_editor_core::normalized_screen_rect(request.start, request.end);

    std::vector<EntityId> entitiesByCandidate;
    for (const EntityId id : scene_.entities()) {
        const VisibilityComponent* visibility = scene_.visibility(id);
        if (!scene_.isEffectivelyVisible(id) ||
                (visibility != nullptr && !visibility->selectable)) {
            continue;
        }

        const MeshComponent* meshComponent = scene_.mesh(id);
        if (!meshComponent || scene_.transform(id) == nullptr) {
            continue;
        }

        const MeshAsset* mesh = assets_.mesh(meshComponent->mesh);
        if (!mesh) {
            continue;
        }

        if (mesh->vertices.empty()) {
            continue;
        }

        const int candidateId = static_cast<int>(entitiesByCandidate.size());
        entitiesByCandidate.push_back(id);
        quader_editor_core::RectangleProjectedObject projected;
        projected.object_id = candidateId;
        const QMat4 worldFromLocal = scene_.worldTransformMatrix(id);

        std::vector<std::optional<QVec2>> projectedVertices(mesh->vertices.size());
        for (std::size_t vertexIndex = 0; vertexIndex < mesh->vertices.size(); ++vertexIndex) {
            QVec2 screen;
            if (!quader_editor_core::project_viewport_point(
                        camera,
                        transformPoint(worldFromLocal, mesh->vertices[vertexIndex].position),
                        screen)) {
                continue;
            }
            projectedVertices[vertexIndex] = screen;
            includeProjectedObjectPoint(projected, screen);
        }
        if (projected.has_screen_bounds) {
            projected.has_screen_center = true;
            projected.screen_center = {
                    (projected.screen_min.x + projected.screen_max.x) * 0.5f,
                    (projected.screen_min.y + projected.screen_max.y) * 0.5f,
            };
        }

        const std::vector<MeshPrimitiveSpan> primitiveSpans = normalizedMeshPrimitiveSpans(*mesh);
        for (const MeshPrimitiveSpan& span : primitiveSpans) {
            const std::uint64_t end = static_cast<std::uint64_t>(span.indexOffset) + span.indexCount;
            for (std::uint64_t offset = span.indexOffset; offset + 2U < end; offset += 3U) {
                const std::uint32_t ia = mesh->indices[static_cast<std::size_t>(offset)];
                const std::uint32_t ib = mesh->indices[static_cast<std::size_t>(offset + 1U)];
                const std::uint32_t ic = mesh->indices[static_cast<std::size_t>(offset + 2U)];
                if (ia >= projectedVertices.size() ||
                        ib >= projectedVertices.size() ||
                        ic >= projectedVertices.size()) {
                    continue;
                }
                const std::optional<QVec2>& a = projectedVertices[ia];
                const std::optional<QVec2>& b = projectedVertices[ib];
                const std::optional<QVec2>& c = projectedVertices[ic];
                if (!a || !b || !c) {
                    continue;
                }
                projected.screen_segments.push_back({*a, *b});
                projected.screen_segments.push_back({*b, *c});
                projected.screen_segments.push_back({*c, *a});
                projected.screen_polygons.push_back({*a, *b, *c});
            }
        }

        hitsRequest.objects.push_back(std::move(projected));
    }

    std::vector<EntityId> selected;
    const quader_editor_core::RectangleSelectionHits hits =
            quader_editor_core::rectangle_hits_from_projected_candidates(hitsRequest);
    selected.reserve(hits.object_ids.size());
    for (const int candidateId : hits.object_ids) {
        if (candidateId >= 0 && static_cast<std::size_t>(candidateId) < entitiesByCandidate.size()) {
            selected.push_back(entitiesByCandidate[static_cast<std::size_t>(candidateId)]);
        }
    }
    return selected;
}

EntityId EditorSelectionService::nearestGroupAncestor(EntityId entity) const {
    EntityId parent = scene_.parent(entity);
    while (parent != InvalidEntityId) {
        if (scene_.kind(parent) == EntityKind::Empty) {
            return parent;
        }
        parent = scene_.parent(parent);
    }
    return InvalidEntityId;
}

ModelingSelectionSummary EditorSelectionService::sceneSummaryFor(ModelingSelectionMode mode) const {
    const std::vector<EntityId> selected = scene_.selectedEntities();
    return {
            .mode = mode,
            .hasSelection = !selected.empty(),
            .activeKind = selected.empty() ? ModelingActiveSelectionKind::None : ModelingActiveSelectionKind::Object,
            .selectedObjectCount = static_cast<std::uint32_t>(selected.size()),
            .contentRevision = modeling_.selectionSummary().contentRevision,
            .selectionRevision = scene_.revision(),
    };
}

} // namespace quader
