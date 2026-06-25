////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/services/ModelingSessionServices.h"

#include "editor/modeling/services/ModelingInstantOperationCatalog.h"
#include "editor/modeling/services/ModelingOperationSettings.h"
#include "editor/projection/PickingCacheProjection.h"
#include "scene/SceneDocument.h"

#include <algorithm>
#include <iterator>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>

#include <editor/interactions/transform_drag.h>
#include <mesh/polygon/document.h>

namespace quader {
namespace {

void appendOverlayFrame(
        quader_editor_core::SelectionOverlayFrame& target,
        const quader_editor_core::SelectionOverlayFrame& source) {
    target.triangles.reserve(target.triangles.size() + source.triangles.size());
    target.lines.reserve(target.lines.size() + source.lines.size());
    target.points.reserve(target.points.size() + source.points.size());
    target.triangles.insert(target.triangles.end(), source.triangles.begin(), source.triangles.end());
    target.lines.insert(target.lines.end(), source.lines.begin(), source.lines.end());
    target.points.insert(target.points.end(), source.points.begin(), source.points.end());
    target.hover_target_selected = target.hover_target_selected || source.hover_target_selected;
}

bool edgesMatchUnordered(quader_poly::Edge left, quader_poly::Edge right) {
    return (left.a == right.a && left.b == right.b) || (left.a == right.b && left.b == right.a);
}

bool hoverFrameSuppressesTriangle(
        bool suppressBaseUnderHover,
        const quader_editor_core::SelectionOverlayFrame& hoverFrame,
        const quader_editor_core::SelectionOverlayTriangle& triangle) {
    if (!suppressBaseUnderHover) {
        return false;
    }
    if (triangle.layer != quader_editor_core::SelectionOverlayLayer::selected_visible_face &&
            triangle.layer != quader_editor_core::SelectionOverlayLayer::selected_xray_face &&
            triangle.layer != quader_editor_core::SelectionOverlayLayer::selected_face_mask) {
        return false;
    }
    return std::any_of(
            hoverFrame.triangles.begin(),
            hoverFrame.triangles.end(),
            [&](const quader_editor_core::SelectionOverlayTriangle& hoverTriangle) {
                return hoverTriangle.layer == quader_editor_core::SelectionOverlayLayer::hover_face &&
                        hoverTriangle.object_id == triangle.object_id &&
                        hoverTriangle.face_id == triangle.face_id;
            });
}

bool hoverFrameSuppressesLine(
        bool suppressBaseUnderHover,
        const quader_editor_core::SelectionOverlayFrame& hoverFrame,
        const quader_editor_core::SelectionOverlayLine& line) {
    if (!suppressBaseUnderHover) {
        return false;
    }
    if (line.layer != quader_editor_core::SelectionOverlayLayer::selected_face_edge &&
            line.layer != quader_editor_core::SelectionOverlayLayer::selected_edge) {
        return false;
    }
    return std::any_of(
            hoverFrame.lines.begin(),
            hoverFrame.lines.end(),
            [&](const quader_editor_core::SelectionOverlayLine& hoverLine) {
                if (hoverLine.object_id != line.object_id) {
                    return false;
                }
                if (hoverLine.layer == quader_editor_core::SelectionOverlayLayer::hover_face_edge) {
                    return line.face_id == hoverLine.face_id && edgesMatchUnordered(line.edge, hoverLine.edge);
                }
                return hoverLine.layer == quader_editor_core::SelectionOverlayLayer::hover_edge &&
                        edgesMatchUnordered(line.edge, hoverLine.edge);
            });
}

bool hoverFrameSuppressesPoint(
        bool suppressBaseUnderHover,
        const quader_editor_core::SelectionOverlayFrame& hoverFrame,
        const quader_editor_core::SelectionOverlayPoint& point) {
    if (!suppressBaseUnderHover || point.layer != quader_editor_core::SelectionOverlayLayer::selected_vertex) {
        return false;
    }
    return std::any_of(
            hoverFrame.points.begin(),
            hoverFrame.points.end(),
            [&](const quader_editor_core::SelectionOverlayPoint& hoverPoint) {
                return hoverPoint.layer == quader_editor_core::SelectionOverlayLayer::hover_vertex &&
                        hoverPoint.object_id == point.object_id &&
                        hoverPoint.vertex_id == point.vertex_id;
            });
}

void appendSelectedHoverFilteredBaseFrame(
        quader_editor_core::SelectionOverlayFrame& target,
        const quader_editor_core::SelectionOverlayFrame& base,
        const quader_editor_core::SelectionOverlayFrame& hoverFrame,
        bool suppressBaseUnderHover) {
    target.triangles.reserve(target.triangles.size() + base.triangles.size());
    target.lines.reserve(target.lines.size() + base.lines.size());
    target.points.reserve(target.points.size() + base.points.size());
    for (const quader_editor_core::SelectionOverlayTriangle& triangle : base.triangles) {
        if (!hoverFrameSuppressesTriangle(suppressBaseUnderHover, hoverFrame, triangle)) {
            target.triangles.push_back(triangle);
        }
    }
    for (const quader_editor_core::SelectionOverlayLine& line : base.lines) {
        if (!hoverFrameSuppressesLine(suppressBaseUnderHover, hoverFrame, line)) {
            target.lines.push_back(line);
        }
    }
    for (const quader_editor_core::SelectionOverlayPoint& point : base.points) {
        if (!hoverFrameSuppressesPoint(suppressBaseUnderHover, hoverFrame, point)) {
            target.points.push_back(point);
        }
    }
}

} // namespace

bool ModelingSelectionService::hasComponentSelection(const ModelingSelectionSummary& summary) {
    return summary.selectedVertexCount > 0 || summary.selectedEdgeCount > 0 || summary.selectedFaceCount > 0;
}

void ModelingSelectionService::invalidatePickingCache() const {
    pickingCache_ = {};
}

const std::vector<quader_editor_core::PolygonObject>&
ModelingSelectionService::cachedSceneTransformedObjectsForPicking(
        const quader_editor_core::EditorSession& session,
        const SceneDocument& scene,
        const ModelingObjectRuntimeMap& objectRegistry) const {
    const std::uint64_t contentRevision = session.state().content_revision;
    const std::uint64_t selectionRevision = session.state().selection_revision;
    const std::uint64_t sceneRevision = scene.revision();
    const bool geometryChanged =
            !pickingCache_.objectsValid ||
            pickingCache_.contentRevision != contentRevision ||
            pickingCache_.sceneRevision != sceneRevision;
    if (pickingCache_.objectsValid &&
            pickingCache_.contentRevision == contentRevision &&
            pickingCache_.selectionRevision == selectionRevision &&
            pickingCache_.sceneRevision == sceneRevision) {
        return pickingCache_.objects;
    }

    const std::vector<quader_editor_core::PolygonObject> runtimeObjects = session.objects();
    pickingCache_.objects = PickingCacheProjection::sceneTransformedObjectsForPicking(
            runtimeObjects,
            scene,
            objectRegistry);
    pickingCache_.contentRevision = contentRevision;
    pickingCache_.selectionRevision = selectionRevision;
    pickingCache_.sceneRevision = sceneRevision;
    pickingCache_.objectsValid = true;
    if (geometryChanged) {
        pickingCache_.boundsValid = false;
        pickingCache_.faceGeometryValid = false;
    }
    return pickingCache_.objects;
}

std::vector<quader_editor_core::PolygonObject> ModelingSelectionService::sceneTransformedObjectsForPicking(
        const quader_editor_core::EditorSession& session,
        const SceneDocument& scene,
        const ModelingObjectRuntimeMap& objectRegistry) const {
    return cachedSceneTransformedObjectsForPicking(session, scene, objectRegistry);
}

const std::vector<quader_editor_core::PickObjectBounds>& ModelingSelectionService::cachedPickObjectBounds(
        const quader_editor_core::EditorSession& session,
        const SceneDocument& scene,
        const ModelingObjectRuntimeMap& objectRegistry) const {
    const std::vector<quader_editor_core::PolygonObject>& objects =
            cachedSceneTransformedObjectsForPicking(session, scene, objectRegistry);
    if (pickingCache_.boundsValid) {
        return pickingCache_.bounds;
    }

    pickingCache_.bounds.clear();
    pickingCache_.bounds.reserve(objects.size());
    for (const quader_editor_core::PolygonObject& object : objects) {
        pickingCache_.bounds.push_back(PickingCacheProjection::buildObjectPickBounds(object));
    }
    pickingCache_.boundsValid = true;
    return pickingCache_.bounds;
}

const std::vector<quader_editor_core::PickObjectFaceGeometry>& ModelingSelectionService::cachedPickFaceGeometry(
        const quader_editor_core::EditorSession& session,
        const SceneDocument& scene,
        const ModelingObjectRuntimeMap& objectRegistry,
        quader_poly::PickOptions pickOptions) const {
    const std::vector<quader_editor_core::PolygonObject>& objects =
            cachedSceneTransformedObjectsForPicking(session, scene, objectRegistry);
    if (pickingCache_.faceGeometryValid) {
        bool signaturesMatch = pickingCache_.faceGeometryEntries.size() == objects.size();
        for (std::size_t index = 0; signaturesMatch && index < objects.size(); ++index) {
            signaturesMatch =
                    pickingCache_.faceGeometryEntries[index].objectId == objects[index].id &&
                    pickingCache_.faceGeometryEntries[index].geometrySignature ==
                            PickingCacheProjection::objectFaceGeometrySignature(objects[index], pickOptions);
        }
        if (signaturesMatch) {
            return pickingCache_.faceGeometry;
        }
    }

    std::vector<ModelingPickFaceGeometryCacheEntry> previousEntries = std::move(pickingCache_.faceGeometryEntries);
    std::vector<quader_editor_core::PickObjectFaceGeometry> previousGeometry = std::move(pickingCache_.faceGeometry);
    std::vector<ModelingPickFaceGeometryCacheEntry> nextEntries;
    std::vector<quader_editor_core::PickObjectFaceGeometry> nextGeometry;
    nextEntries.reserve(objects.size());
    nextGeometry.reserve(objects.size());

    for (const quader_editor_core::PolygonObject& object : objects) {
        const std::uint64_t signature = PickingCacheProjection::objectFaceGeometrySignature(object, pickOptions);
        auto reusable = previousEntries.end();
        for (auto it = previousEntries.begin(); it != previousEntries.end(); ++it) {
            if (it->objectId == object.id && it->geometrySignature == signature) {
                reusable = it;
                break;
            }
        }
        if (reusable != previousEntries.end()) {
            const std::size_t index = static_cast<std::size_t>(std::distance(previousEntries.begin(), reusable));
            nextGeometry.push_back(std::move(previousGeometry[index]));
            reusable->objectId = -1;
        } else {
            nextGeometry.push_back({
                    object.id,
                    quader_poly::build_face_pick_geometry(object.document, pickOptions),
            });
        }
        nextEntries.push_back({
                .objectId = object.id,
                .geometrySignature = signature,
        });
    }

    pickingCache_.faceGeometry = std::move(nextGeometry);
    pickingCache_.faceGeometryEntries = std::move(nextEntries);
    pickingCache_.faceGeometryValid = true;
    return pickingCache_.faceGeometry;
}

bool ModelingSelectionService::selectionMatches(
        const quader_poly::Selection& left,
        const quader_poly::Selection& right) {
    return left.mode == right.mode &&
            left.vertices == right.vertices &&
            left.edges == right.edges &&
            left.faces == right.faces &&
            left.has_active == right.has_active &&
            left.active_kind == right.active_kind &&
            left.active_vertex == right.active_vertex &&
            left.active_edge == right.active_edge &&
            left.active_face == right.active_face;
}

SceneModelingSyncService::SceneModelingSyncService(ModelingObjectRuntimeMap& objectMap)
    : objectMap_(objectMap) {}

ModelingObjectRuntimeMap& SceneModelingSyncService::objectMap() {
    return objectMap_;
}

const ModelingObjectRuntimeMap& SceneModelingSyncService::objectMap() const {
    return objectMap_;
}

bool SceneModelingSyncService::sceneObjectsSyncValid() const {
    return sceneObjectsSyncValid_;
}

std::uint64_t SceneModelingSyncService::syncedModelingContentRevision() const {
    return syncedModelingContentRevision_;
}

void SceneModelingSyncService::invalidateSceneObjects() {
    sceneObjectsSyncValid_ = false;
}

void SceneModelingSyncService::markSceneObjectsSynced(std::uint64_t contentRevision) {
    syncedModelingContentRevision_ = contentRevision;
    sceneObjectsSyncValid_ = true;
}

bool SceneModelingSyncService::sceneObjectsSyncedForCurrentModelingContent(
        const quader_editor_core::EditorSession& session,
        const SceneDocument& scene) const {
    if (!sceneObjectsSyncValid_ || syncedModelingContentRevision_ != session.state().content_revision) {
        return false;
    }
    if (objectMap_.size() != session.objects().size()) {
        return false;
    }
    for (const quader_editor_core::PolygonObject& object : session.objects()) {
        const auto entity = objectMap_.entitiesByObject().find(object.id);
        if (entity == objectMap_.entitiesByObject().end() || !scene.exists(entity->second)) {
            return false;
        }
        const ModelingObjectComponent* modelingObject = scene.modelingObject(entity->second);
        const MeshComponent* mesh = scene.mesh(entity->second);
        if (modelingObject == nullptr || mesh == nullptr || mesh->mesh == InvalidAssetId) {
            return false;
        }
    }
    return true;
}

bool ModelingOverlayService::hasModalOverlay(const ModelingToolStatus& status) {
    return status.insertEdgeLoopHasPreview ||
            status.knifeHasCurrent ||
            status.cutHasLine ||
            status.mirrorHasLine ||
            status.polyHasCurrent;
}

void ModelingOverlayService::invalidate() const {
    selectionOverlayCache_.invalidate();
    overlayBaseCache_.valid = false;
}

void SelectionOverlayCache::invalidate() const {
    valid_ = false;
}

bool SelectionOverlayCache::matches(
        std::uint64_t contentRevision,
        std::uint64_t selectionRevision,
        std::uint64_t sceneRevision,
        quader_editor_core::SelectionMode selectionMode,
        std::uint64_t overlaySignature,
        const quader_editor_core::SelectionHoverState& hover) const {
    return valid_ &&
            contentRevision_ == contentRevision &&
            selectionRevision_ == selectionRevision &&
            sceneRevision_ == sceneRevision &&
            selectionMode_ == selectionMode &&
            overlaySignature_ == overlaySignature &&
            hoverMatches(hover_, hover);
}

const ViewportOverlayFrame& SelectionOverlayCache::frame() const {
    return frame_;
}

void SelectionOverlayCache::store(
        ViewportOverlayFrame frame,
        std::uint64_t contentRevision,
        std::uint64_t selectionRevision,
        std::uint64_t sceneRevision,
        quader_editor_core::SelectionMode selectionMode,
        std::uint64_t overlaySignature,
        const quader_editor_core::SelectionHoverState& hover) const {
    frame_ = std::move(frame);
    contentRevision_ = contentRevision;
    selectionRevision_ = selectionRevision;
    sceneRevision_ = sceneRevision;
    selectionMode_ = selectionMode;
    overlaySignature_ = overlaySignature;
    hover_ = hover;
    valid_ = true;
}

bool SelectionOverlayCache::hoverMatches(
        const quader_editor_core::SelectionHoverState& left,
        const quader_editor_core::SelectionHoverState& right) {
    return left.object_id == right.object_id &&
            left.kind == right.kind &&
            left.vertex_id == right.vertex_id &&
            left.edge == right.edge &&
            left.face_id == right.face_id &&
            left.selected_modifier_pressed == right.selected_modifier_pressed &&
            left.candidate_object_id == right.candidate_object_id;
}

bool ModelingOverlayService::selectionFrameMatches(
        std::uint64_t contentRevision,
        std::uint64_t selectionRevision,
        std::uint64_t sceneRevision,
        quader_editor_core::SelectionMode selectionMode,
        std::uint64_t overlaySignature,
        const quader_editor_core::SelectionHoverState& hover) const {
    return selectionOverlayCache_.matches(
            contentRevision,
            selectionRevision,
            sceneRevision,
            selectionMode,
            overlaySignature,
            hover);
}

const ViewportOverlayFrame& ModelingOverlayService::selectionFrame() const {
    return selectionOverlayCache_.frame();
}

void ModelingOverlayService::storeSelectionFrame(
        ViewportOverlayFrame frame,
        std::uint64_t contentRevision,
        std::uint64_t selectionRevision,
        std::uint64_t sceneRevision,
        quader_editor_core::SelectionMode selectionMode,
        std::uint64_t overlaySignature,
        const quader_editor_core::SelectionHoverState& hover) const {
    selectionOverlayCache_.store(
            std::move(frame),
            contentRevision,
            selectionRevision,
            sceneRevision,
            selectionMode,
            overlaySignature,
            hover);
}

const quader_editor_core::SelectionOverlayFrame& ModelingOverlayService::selectionOverlayBaseFrame(
        const quader_editor_core::EditorSession& session) const {
    const std::uint64_t contentRevision = session.state().content_revision;
    const std::uint64_t selectionRevision = session.state().selection_revision;
    const quader_editor_core::SelectionMode selectionMode = session.selection_mode();
    if (overlayBaseCache_.valid &&
            overlayBaseCache_.contentRevision == contentRevision &&
            overlayBaseCache_.selectionRevision == selectionRevision &&
            overlayBaseCache_.selectionMode == selectionMode) {
        return overlayBaseCache_.frame;
    }

    std::vector<OverlayBaseObjectCacheEntry> previousEntries = std::move(overlayBaseCache_.objectEntries);
    OverlayBaseCache next;
    next.valid = true;
    next.contentRevision = contentRevision;
    next.selectionRevision = selectionRevision;
    next.selectionMode = selectionMode;
    next.objectEntries.reserve(session.objects().size());

    const quader_editor_core::SelectionHoverState emptyHover;
    for (const quader_editor_core::PolygonObject& object : session.objects()) {
        const std::uint64_t signature = PickingCacheProjection::objectFaceGeometrySignature(object);
        auto reusable = previousEntries.end();
        for (auto it = previousEntries.begin(); it != previousEntries.end(); ++it) {
            if (it->objectId == object.id &&
                    it->geometrySignature == signature &&
                    it->selectionMode == selectionMode &&
                    it->selected == object.selected &&
                    ModelingSelectionService::selectionMatches(it->selection, object.selection)) {
                reusable = it;
                break;
            }
        }

        OverlayBaseObjectCacheEntry entry;
        if (reusable != previousEntries.end()) {
            entry = std::move(*reusable);
            reusable->objectId = -1;
        } else {
            entry.objectId = object.id;
            entry.geometrySignature = signature;
            entry.selectionMode = selectionMode;
            entry.selected = object.selected;
            entry.selection = object.selection;
            entry.frame = quader_editor_core::build_selection_overlay_frame(
                    std::span<const quader_editor_core::PolygonObject>(&object, 1U),
                    selectionMode,
                    emptyHover,
                    {});
        }
        appendOverlayFrame(next.frame, entry.frame);
        next.objectEntries.push_back(std::move(entry));
    }

    overlayBaseCache_ = std::move(next);
    return overlayBaseCache_.frame;
}

quader_editor_core::SelectionOverlayFrame ModelingOverlayService::selectionOverlayCurrentFrame(
        const quader_editor_core::EditorSession& session,
        const quader_editor_core::EditorRuntime& runtime,
        const quader_editor_core::SelectionHoverState& hover) const {
    const quader_editor_core::SelectionOverlayFrame& base = selectionOverlayBaseFrame(session);
    quader_editor_core::SelectionOverlayFrame hoverFrame =
            quader_editor_core::build_selection_hover_overlay_frame(
                    session.objects(),
                    session.selection_mode(),
                    hover,
                    {});
    const auto appendRuntimeToolOverlay = [&](quader_editor_core::SelectionOverlayFrame& frame) {
        const quader_editor_core::InsertEdgeLoopToolState& insertLoop = runtime.insert_edge_loop_state();
        frame.lines.insert(frame.lines.end(), insertLoop.preview_lines.begin(), insertLoop.preview_lines.end());

        const quader_editor_core::KnifeToolState& knife = runtime.knife_state();
        frame.points.insert(frame.points.end(), knife.preview_points.begin(), knife.preview_points.end());

        const quader_editor_core::PolyToolState& poly = runtime.poly_state();
        frame.lines.insert(frame.lines.end(), poly.preview_lines.begin(), poly.preview_lines.end());
        frame.points.insert(frame.points.end(), poly.preview_points.begin(), poly.preview_points.end());
    };
    const bool suppressBaseUnderHover =
            quader_editor_core::selection_hover_target_is_selected(session.objects(), session.selection_mode(), hover);
    if (suppressBaseUnderHover) {
        quader_editor_core::SelectionOverlayFrame frame;
        frame.hover_target_selected = hoverFrame.hover_target_selected;
        appendSelectedHoverFilteredBaseFrame(frame, base, hoverFrame, suppressBaseUnderHover);
        appendOverlayFrame(frame, hoverFrame);
        appendRuntimeToolOverlay(frame);
        return frame;
    }

    quader_editor_core::SelectionOverlayFrame frame = base;
    appendOverlayFrame(frame, hoverFrame);
    appendRuntimeToolOverlay(frame);
    return frame;
}

ModelingCommandResult ModelingCommandExecutor::runtimeUnavailable() {
    return {.ok = false, .message = "Modeling runtime is not available in this build."};
}

ModelingCommandResult ModelingCommandExecutor::fromRuntime(const quader_editor_core::CommandResult& result) {
    return {
            .ok = result.ok,
            .changed = result.changed,
            .message = result.message,
    };
}

ModelingCommandExecutor::SyncPlan ModelingCommandExecutor::runtimeSyncPlan(
        bool mayChangeContent,
        std::uint64_t contentBefore,
        std::uint64_t contentAfter,
        const quader_editor_core::CommandResult& result) {
    SyncPlan plan;
    plan.syncMeshes = mayChangeContent && (contentAfter != contentBefore || result.changed);
    plan.forceMeshSync = result.changed;
    plan.syncSelection = true;
    plan.invalidateOverlay = result.changed;
    return plan;
}

ModelingCommandExecutor::SyncPlan ModelingCommandExecutor::operationSyncPlan(
        quader_editor_core::EditorOperationCommandPhase phase,
        const quader_editor_core::PreviewTransactionStatus& previewBefore,
        const quader_editor_core::EditorOperationCommandResult& result,
        std::uint64_t contentBefore,
        std::uint64_t contentAfter) {
    const bool previewPhase = phase != quader_editor_core::EditorOperationCommandPhase::execute;
    const bool previewFinished = result.preview_finish.has_value() && result.preview_finish->before.active;
    const bool sceneChanged = contentAfter != contentBefore;

    SyncPlan plan;
    plan.syncMeshes = sceneChanged || previewPhase || previewBefore.active || previewFinished;
    plan.forceMeshSync = ModelingMeshSyncService::shouldForceRefresh(
            previewPhase || previewBefore.active,
            previewFinished);
    plan.syncSelection = result.command.ok || sceneChanged || previewPhase || previewBefore.active || previewFinished;
    plan.invalidateOverlay = plan.syncMeshes;
    return plan;
}

std::optional<RuntimeOperationExecutor::OperationCommand> RuntimeOperationExecutor::operationForInstantCommand(
        ModelingInstantOperationId operation,
        const ModelingInteractionSettings& interactionSettings) const {
    const ModelingInstantOperationCatalogEntry* entry = findModelingInstantOperationCatalogEntry(operation);
    if (entry == nullptr || !entry->runtimeOperation.has_value()) {
        return std::nullopt;
    }

    OperationCommand command;
    command.operation = *entry->runtimeOperation;
    command.settings = quader_editor_core::SelectionOperationSettings{};

    switch (entry->settingsKind) {
    case RuntimeOperationSettingsKind::Selection:
        break;
    case RuntimeOperationSettingsKind::DuplicateMeshes:
        command.settings = quader_editor_core::DuplicateMeshesOperationSettings{};
        break;
    case RuntimeOperationSettingsKind::Bridge:
        command.settings = quader_editor_core::BridgeOperationSettings{.steps = interactionSettings.bridgeSteps};
        break;
    case RuntimeOperationSettingsKind::BevelVertices:
        command.settings = quader_editor_core::BevelVerticesOperationSettings{};
        break;
    case RuntimeOperationSettingsKind::RecalculateFaceNormals:
        command.settings = quader_editor_core::RecalculateFaceNormalsOperationSettings{};
        break;
    }

    return command;
}

quader_editor_core::EditorOperationCommand RuntimeOperationExecutor::makeCommand(
        const OperationCommand& operation) {
    quader_editor_core::EditorOperationCommand command;
    command.operation_id = operation.operation;
    command.phase = operation.phase;
    command.settings = operation.settings;
    return command;
}

quader_editor_core::EditorOperationCommandResult RuntimeOperationExecutor::execute(
        quader_editor_core::EditorSession& session,
        const OperationCommand& operation) {
    return quader_editor_core::execute_command(session, makeCommand(operation));
}

bool TransformPreviewService::isTransform(ModelingTransformOperation operation) {
    return operation == ModelingTransformOperation::Transform;
}

bool TransformPreviewService::hasTransformPreview(const quader_editor_core::EditorSession& session) {
    const quader_editor_core::PreviewTransactionStatus status = session.preview_transaction_status();
    return status.active && status.kind == quader_editor_core::PreviewTransactionKind::transform;
}

ModelingCommandResult TransformPreviewService::finishResult(
        const quader_editor_core::PreviewTransactionFinishResult& finish,
        bool cancel) {
    return {
            .ok = !finish.before.active || finish.before.kind == quader_editor_core::PreviewTransactionKind::transform,
            .changed = cancel ? finish.restored : finish.before.changed,
    };
}

TransformDragPreviewRouting TransformDragController::previewRouting(
        bool duplicateSelected,
        bool objectSelectionMode,
        bool transformOperation) {
    const bool duplicateSceneTransformPreview =
            duplicateSelected &&
            objectSelectionMode &&
            transformOperation;
    TransformDragPreviewRouting routing;
    routing.previewDriven = !duplicateSceneTransformPreview && (duplicateSelected || !objectSelectionMode);
    routing.needsRuntimePreviewTransaction = routing.previewDriven || duplicateSelected;
    routing.sceneTransformPreviewEligible =
            (duplicateSceneTransformPreview || !routing.previewDriven) && transformOperation;
    return routing;
}

void TransformDragController::setPreviewDriven(bool previewDriven, bool duplicateSelected) {
    previewDriven_ = previewDriven;
    duplicateSelected_ = duplicateSelected;
}

void TransformDragController::clear() {
    previewDriven_ = false;
    duplicateSelected_ = false;
}

bool TransformDragController::previewDriven() const {
    return previewDriven_;
}

bool TransformDragController::duplicateSelected() const {
    return duplicateSelected_;
}

bool ModalToolRuntime::ownsViewport(ModelingViewportTool tool) {
    switch (tool) {
    case ModelingViewportTool::Box:
    case ModelingViewportTool::InsertEdgeLoop:
    case ModelingViewportTool::Knife:
    case ModelingViewportTool::Cut:
    case ModelingViewportTool::Poly:
    case ModelingViewportTool::Mirror:
        return true;
    case ModelingViewportTool::Select:
    case ModelingViewportTool::Move:
    case ModelingViewportTool::Extend:
    case ModelingViewportTool::Extrude:
    case ModelingViewportTool::Rotate:
    case ModelingViewportTool::Scale:
        return false;
    }
    return false;
}

quader_editor_core::EditorRuntimeCommandId ModelingClipboardService::copyCommand() {
    return quader_editor_core::EditorRuntimeCommandId::copy_selected_meshes;
}

quader_editor_core::EditorRuntimeCommandId ModelingClipboardService::pasteCommand() {
    return quader_editor_core::EditorRuntimeCommandId::paste_copied_meshes;
}

quader_editor_core::EditorRuntimeCommand ModelingClipboardService::makeCopyCommand() {
    quader_editor_core::EditorRuntimeCommand command;
    command.command_id = copyCommand();
    return command;
}

quader_editor_core::EditorRuntimeCommand ModelingClipboardService::makePasteCommand() {
    quader_editor_core::EditorRuntimeCommand command;
    command.command_id = pasteCommand();
    command.settings = quader_editor_core::PasteCopiedMeshesRuntimeCommandSettings {};
    return command;
}

quader_editor_core::EditorRuntimeCommandId ModelingHistoryBridge::undoCommand() {
    return quader_editor_core::EditorRuntimeCommandId::undo;
}

quader_editor_core::EditorRuntimeCommandId ModelingHistoryBridge::redoCommand() {
    return quader_editor_core::EditorRuntimeCommandId::redo;
}

quader_editor_core::RepeatLastActionState& ModelingHistoryBridge::repeatLastAction() {
    return repeatLastAction_;
}

const quader_editor_core::RepeatLastActionState& ModelingHistoryBridge::repeatLastAction() const {
    return repeatLastAction_;
}

quader_editor_core::CommandResult ModelingHistoryBridge::executeRepeatLastAction(
        quader_editor_core::EditorSession& session) {
    return quader_editor_core::execute_repeat_last_action(session, repeatLastAction_);
}

void ModelingHistoryBridge::recordOperation(
        const quader_editor_core::EditorOperationCommand& command,
        const quader_editor_core::CommandResult& result) {
    quader_editor_core::record_repeatable_operation(repeatLastAction_, command, result);
}

void ModelingHistoryBridge::recordTransform(
        const quader_editor_core::EditorSession& session,
        const quader_editor_core::TransformDragState& drag,
        bool duplicateSelected) {
    quader_editor_core::record_repeatable_transform(
            repeatLastAction_,
            session,
            drag,
            duplicateSelected);
}

ModelingInteractionSettingsService::ModelingInteractionSettingsService()
    : settings_(normalize(ModelingInteractionSettings{})) {}

ModelingInteractionSettings ModelingInteractionSettingsService::settings() const {
    return settings_;
}

ModelingInteractionSettings ModelingInteractionSettingsService::normalize(
        const ModelingInteractionSettings& settings) const {
    ModelingInteractionSettings normalized = settings;
    normalized.bridgeSteps = quader_editor_core::normalize_bridge_operation_settings(
            quader_editor_core::BridgeOperationSettings{.steps = normalized.bridgeSteps}).steps;
    const quader_editor_core::EdgeBevelOperationSettings edgeBevel =
            quader_editor_core::normalize_edge_bevel_operation_settings({
                    .width = normalized.edgeBevelWidth,
                    .profile = normalized.edgeBevelProfile,
                    .segments = normalized.edgeBevelSegments,
            });
    normalized.edgeBevelWidth = edgeBevel.width;
    normalized.edgeBevelProfile = edgeBevel.profile;
    normalized.edgeBevelSegments = edgeBevel.segments;
    const quader_editor_core::ThickenOperationSettings thicken =
            quader_editor_core::normalize_thicken_operation_settings({
                    .thickness = normalized.thickenThickness,
                    .from_center = normalized.thickenFromCenter,
            });
    normalized.thickenThickness = thicken.thickness;
    normalized.thickenFromCenter = thicken.from_center;
    const quader_editor_core::SliceQuadOperationSettings slice =
            quader_editor_core::normalize_slice_quad_operation_settings({
                    .x_slices = normalized.sliceQuadXSlices,
                    .y_slices = normalized.sliceQuadYSlices,
            });
    normalized.sliceQuadXSlices = slice.x_slices;
    normalized.sliceQuadYSlices = slice.y_slices;
    normalized.mergeByDistanceTolerance = quader_editor_core::normalize_merge_by_distance_operation_settings(
            quader_editor_core::MergeByDistanceOperationSettings{.tolerance = normalized.mergeByDistanceTolerance}).tolerance;
    return normalized;
}

bool ModelingInteractionSettingsService::setNormalized(ModelingInteractionSettings settings) {
    settings = normalize(settings);
    const bool changed = settings.bridgeSteps != settings_.bridgeSteps ||
            settings.edgeBevelWidth != settings_.edgeBevelWidth ||
            settings.edgeBevelProfile != settings_.edgeBevelProfile ||
            settings.edgeBevelSegments != settings_.edgeBevelSegments ||
            settings.thickenThickness != settings_.thickenThickness ||
            settings.thickenFromCenter != settings_.thickenFromCenter ||
            settings.sliceQuadXSlices != settings_.sliceQuadXSlices ||
            settings.sliceQuadYSlices != settings_.sliceQuadYSlices ||
            settings.mergeByDistanceTolerance != settings_.mergeByDistanceTolerance ||
            settings.cutMode != settings_.cutMode;
    settings_ = settings;
    return changed;
}

void ModelingInteractionSettingsService::resetForPreviewAction(ModelingActionPreviewKind previewKind) {
    const ModelingInteractionSettings defaults = normalize(ModelingInteractionSettings{});
    switch (previewKind) {
    case ModelingActionPreviewKind::EdgeBevel:
        settings_.edgeBevelWidth = defaults.edgeBevelWidth;
        settings_.edgeBevelProfile = defaults.edgeBevelProfile;
        settings_.edgeBevelSegments = defaults.edgeBevelSegments;
        return;
    case ModelingActionPreviewKind::Thicken:
        settings_.thickenThickness = defaults.thickenThickness;
        settings_.thickenFromCenter = defaults.thickenFromCenter;
        return;
    case ModelingActionPreviewKind::MergeByDistance:
        settings_.mergeByDistanceTolerance = defaults.mergeByDistanceTolerance;
        return;
    case ModelingActionPreviewKind::Bridge:
    case ModelingActionPreviewKind::SliceQuad:
    case ModelingActionPreviewKind::None:
        return;
    }
}

void ModelingInteractionSettingsService::setCutMode(ModelingCutMode mode) {
    settings_.cutMode = mode;
}

void ModelingInteractionSettingsService::cycleCutModeWithoutRuntime() {
    settings_.cutMode =
            settings_.cutMode == ModelingCutMode::DiscardLeft ? ModelingCutMode::DiscardRight :
            settings_.cutMode == ModelingCutMode::DiscardRight ? ModelingCutMode::KeepBoth :
            ModelingCutMode::DiscardLeft;
}

const std::string& ModelingToolSettingsService::activePreviewActionId() const {
    return activePreviewActionId_;
}

const std::string& ModelingToolSettingsService::activePreviewOperationId() const {
    return activePreviewOperationId_;
}

const std::string& ModelingToolSettingsService::activePreviewLabel() const {
    return activePreviewLabel_;
}

void ModelingToolSettingsService::setPreviewMetadata(std::string actionId, std::string operationId, std::string label) {
    activePreviewActionId_ = std::move(actionId);
    activePreviewOperationId_ = std::move(operationId);
    activePreviewLabel_ = std::move(label);
}

void ModelingToolSettingsService::clearPreviewMetadata() {
    activePreviewActionId_.clear();
    activePreviewOperationId_.clear();
    activePreviewLabel_.clear();
}

ModelingToolSettingsTarget ModelingToolSettingsService::operationPreviewTarget(
        ModelingToolSettingsTargetKind kind,
        std::string_view fallbackLabel,
        std::string_view fallbackOperationId) const {
    ModelingToolSettingsTarget target;
    target.kind = kind;
    target.active = true;
    target.operationPreviewActive = true;
    target.label = activePreviewLabel_.empty() ? std::string(fallbackLabel) : activePreviewLabel_;
    target.actionId = activePreviewActionId_;
    target.operationId = activePreviewOperationId_.empty() ? std::string(fallbackOperationId) : activePreviewOperationId_;
    return target;
}

ModelingToolSettingsTarget ModelingToolSettingsService::activePreviewTarget(
        const quader_editor_core::PreviewTransactionStatus& preview) const {
    ModelingToolSettingsTarget target;
    const ModelingToolSettingsTargetKind previewKind = preview.active ?
            toolSettingsTargetKindForPreview(preview.kind) :
            ModelingToolSettingsTargetKind::None;
    if (previewKind == ModelingToolSettingsTargetKind::None) {
        return target;
    }

    std::string fallbackOperationId;
    if (const std::optional<quader_editor_core::EditorOperationId> operation =
                operationForPreviewKind(preview.kind)) {
        if (const quader_editor_core::EditorOperation* schema =
                    quader_editor_core::editor_operation_for_id(*operation)) {
            fallbackOperationId = std::string(schema->id());
        }
    }
    return operationPreviewTarget(
            previewKind,
            fallbackToolSettingsLabel(previewKind),
            fallbackOperationId);
}

ModelingToolSettingsTarget ModelingToolSettingsService::activeRuntimeToolTarget(
        const quader_editor_core::EditorRuntimeStatus& runtimeStatus) const {
    ModelingToolSettingsTarget target;
    if (runtimeStatus.cut_active) {
        target.kind = ModelingToolSettingsTargetKind::Cut;
        target.active = true;
        target.label = fallbackToolSettingsLabel(ModelingToolSettingsTargetKind::Cut);
        return target;
    }
    return target;
}

ModelingActionQuery ModelingContextActionService::effectiveActionQuery(
        ModelingActionQuery query,
        bool modalToolOwnsViewport) const {
    if (query.suppressWhenModalToolOwnsViewport) {
        query.modalToolOwnsViewport = query.modalToolOwnsViewport || modalToolOwnsViewport;
    }
    return query;
}

ModelingActionSet ModelingContextActionService::actionSet(
        const ModelingSelectionSummary& summary,
        ModelingActionQuery query,
        bool modalToolOwnsViewport) const {
    return ModelingActionCatalog::actionSetFor(summary, effectiveActionQuery(query, modalToolOwnsViewport));
}

std::vector<ModelingActionItem> ModelingContextActionService::actions(
        const ModelingSelectionSummary& summary,
        ModelingActionQuery query,
        bool modalToolOwnsViewport) const {
    return ModelingActionCatalog::actionsFor(summary, effectiveActionQuery(query, modalToolOwnsViewport));
}

ModelingContextActionResolution ModelingContextActionService::resolveExecutableAction(
        const ModelingSelectionSummary& summary,
        std::string_view actionId,
        bool modalToolOwnsViewport) const {
    ModelingContextActionResolution resolution;
    if (modalToolOwnsViewport) {
        resolution.message = "Viewport is owned by an active modal tool.";
        return resolution;
    }

    ModelingActionQuery query;
    query.surface = ModelingActionSurface::ViewportContextMenu;
    query.modalToolOwnsViewport = false;
    query.includeDisabled = true;
    query.includeHidden = false;

    const std::vector<ModelingActionItem> availableActions = ModelingActionCatalog::actionsFor(summary, query);
    const auto found = std::find_if(
            availableActions.begin(),
            availableActions.end(),
            [&](const ModelingActionItem& action) {
                return action.actionId == actionId;
            });
    if (found == availableActions.end()) {
        resolution.message = "Action is not available in the current mode.";
        return resolution;
    }
    if (found->visibility != ModelingActionVisibility::Enabled) {
        resolution.message = found->disabledReason.empty()
                ? "Action is disabled for the current selection."
                : found->disabledReason;
        return resolution;
    }

    resolution.ok = true;
    resolution.action = *found;
    return resolution;
}

} // namespace quader
