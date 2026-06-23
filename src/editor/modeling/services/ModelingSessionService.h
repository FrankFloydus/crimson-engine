////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/services/ModelingActionCatalog.h"
#include "editor/modeling/ModelingTypes.h"
#include "render/ViewportOverlayFrame.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace quader {

class AssetRegistry;
class DocumentCommandService;
class IUndoableCommand;
class SceneDocument;

/**
 * Stores an opaque Modeling Session Memento restore point for editor-owned history commands.
 */
class ModelingSessionMemento final {
public:
    ModelingSessionMemento();
    ~ModelingSessionMemento();
    ModelingSessionMemento(const ModelingSessionMemento&) noexcept = default;
    ModelingSessionMemento& operator=(const ModelingSessionMemento&) noexcept = default;
    ModelingSessionMemento(ModelingSessionMemento&&) noexcept = default;
    ModelingSessionMemento& operator=(ModelingSessionMemento&&) noexcept = default;

    [[nodiscard]] bool valid() const;

private:
    friend class ModelingSessionService;

    struct Impl;
    explicit ModelingSessionMemento(std::shared_ptr<const Impl> impl);

    std::shared_ptr<const Impl> impl_;
};

/**
 * Coordinates Modeling Session Service responsibilities for the modeling application service layer.
 */
class ModelingSessionService {
public:
    ModelingSessionService(SceneDocument& scene, AssetRegistry& assets);
    ~ModelingSessionService();

    ModelingSessionService(const ModelingSessionService&) = delete;
    ModelingSessionService& operator=(const ModelingSessionService&) = delete;
    ModelingSessionService(ModelingSessionService&&) noexcept;
    ModelingSessionService& operator=(ModelingSessionService&&) noexcept;

    [[nodiscard]] bool runtimeAvailable() const;
    [[nodiscard]] ModelingCommandDescriptor boxToolCommand() const;
    [[nodiscard]] ModelingCommandDescriptor createBoxFromBoundsCommand() const;
    [[nodiscard]] std::vector<ModelingObjectRecord> objects() const;
    [[nodiscard]] std::optional<EntityId> entityForModelingObject(int modelingObjectId) const;
    [[nodiscard]] std::optional<int> modelingObjectForEntity(EntityId entity) const;
    [[nodiscard]] std::optional<MeshAsset> compileObjectMesh(int modelingObjectId) const;
    [[nodiscard]] std::optional<QVec3> selectionCenter() const;
    [[nodiscard]] ModelingSelectionMode selectionMode() const;
    [[nodiscard]] ModelingSelectionSummary selectionSummary() const;
    [[nodiscard]] std::uint64_t selectionOverlaySignature() const;
    [[nodiscard]] ViewportOverlayFrame selectionOverlayFrame() const;
    [[nodiscard]] std::vector<ModelingToolPreviewMesh> toolPreviewMeshes() const;
    [[nodiscard]] ModelingSessionMemento captureHistoryMemento() const;
    ModelingCommandResult restoreHistoryMemento(const ModelingSessionMemento& memento);

    ModelingCommandResult duplicateSelectedHierarchyForGroupDrag(DocumentCommandService& documentCommands);
    ModelingCommandResult setSelectionMode(ModelingSelectionMode mode);
    ModelingCommandResult selectAll();
    ModelingCommandResult invertSelection();
    ModelingCommandResult clearSelection();
    std::unique_ptr<IUndoableCommand> createDeleteSelectionCommand();
    [[nodiscard]] bool hasSelectedSceneGroupRoot() const;
    ModelingCommandResult deleteSelection();
    ModelingCommandResult copySelection();
    ModelingCommandResult pasteSelection();
    ModelingCommandResult repeatLastAction();
    ModelingCommandResult mirrorSelection(ModelingMirrorAxis axis);
    ModelingCommandResult flipSelection(ModelingFlipOrientation orientation);
    ModelingCommandResult applyActiveMaterial(std::uint32_t materialSlot = 0);
    [[nodiscard]] bool modalToolOwnsViewport() const;
    [[nodiscard]] ModelingToolStatus toolStatus() const;
    [[nodiscard]] ModelingToolSettingsTarget toolSettingsTarget() const;
    [[nodiscard]] ModelingInteractionSettings interactionSettings() const;
    ModelingCommandResult setInteractionSettings(const ModelingInteractionSettings& settings);
    [[nodiscard]] ModelingActionSet contextActionSet(const ModelingActionQuery& query = {}) const;
    [[nodiscard]] std::vector<ModelingActionItem> contextActions(const ModelingActionQuery& query = {}) const;
    ModelingCommandResult executeContextAction(std::string_view actionId);
    ModelingCommandResult commitActiveOperationPreview();
    ModelingCommandResult cancelActiveOperationPreview();
    [[nodiscard]] std::vector<ModelingInstantOperationDescriptor> instantOperations() const;
    ModelingCommandResult executeInstantOperation(ModelingInstantOperationId operation);
    ModelingCommandResult beginTransformPreview(bool duplicateSelected);
    ModelingCommandResult updateTransformTranslatePreview(QVec3 delta);
    ModelingCommandResult updateTransformMatrixPreview(const ModelingTransform3& transform, QVec3 pivot);
    ModelingCommandResult updateTransformExtrudePreview(QVec3 offset, float closedEdgeLedgeSize = 0.0f);
    ModelingCommandResult updateTransformInsetPreview(const ModelingTransform3& transform, QVec3 pivot);
    ModelingCommandResult commitTransformPreview();
    ModelingCommandResult cancelTransformPreview();
    ModelingCommandResult recordRepeatableTransform(const ModelingRepeatTransform& transform);
    ModelingTransformDragPreview beginTransformDrag(const ModelingTransformDragBeginRequest& request);
    ModelingTransformDragPreview updateTransformDrag(const ModelingTransformDragUpdateRequest& request);
    ModelingTransformDragPreview commitTransformDrag();
    ModelingCommandResult cancelTransformDrag();
    ModelingSelectionSummary pickSelect(const ModelingPickSelectRequest& request);
    ModelingCommandResult updateSelectionHover(const ModelingHoverRequest& request);
    [[nodiscard]] ModelingHoverSummary selectionHoverSummary() const;
    [[nodiscard]] ModelingHoverSummary previewSelectionHover(const ModelingHoverRequest& request) const;
    ModelingCommandResult clearSelectionHover();
    ModelingCommandResult activateViewportTool(ModelingViewportTool tool);
    ModelingCommandResult cancelActiveModalTool();
    ModelingCommandResult updateInsertEdgeLoopHover(const ModelingToolRayEvent& event);
    ModelingCommandResult beginInsertEdgeLoopDrag(const ModelingToolRayEvent& event);
    ModelingCommandResult updateInsertEdgeLoopDrag(const ModelingToolRayEvent& event);
    ModelingCommandResult commitInsertEdgeLoopDrag();
    ModelingCommandResult updateKnifeHover(const ModelingToolRayEvent& event);
    ModelingCommandResult acceptKnifePoint(const ModelingToolRayEvent& event);
    ModelingCommandResult commitKnifeTool();
    ModelingCommandResult updateCutHover(const ModelingToolRayEvent& event);
    ModelingCommandResult clickCut(const ModelingToolRayEvent& event);
    ModelingCommandResult beginCutPointDrag(int pointIndex);
    ModelingCommandResult beginCutLineDrag(const ModelingToolRayEvent& event);
    ModelingCommandResult endCutDrag();
    ModelingCommandResult setCutMode(ModelingCutMode mode);
    ModelingCommandResult cycleCutMode();
    ModelingCommandResult finalizeCutTool();
    ModelingCommandResult updateMirrorHover(const ModelingToolRayEvent& event);
    ModelingCommandResult clickMirror(const ModelingToolRayEvent& event);
    ModelingCommandResult beginMirrorPointDrag(int pointIndex);
    ModelingCommandResult beginMirrorLineDrag(const ModelingToolRayEvent& event);
    ModelingCommandResult endMirrorDrag();
    ModelingCommandResult finalizeMirrorTool();
    ModelingCommandResult updatePolyHover(const ModelingToolRayEvent& event);
    ModelingCommandResult acceptPolyPoint();
    ModelingCommandResult commitPolyTool();
    ModelingCommandResult activateBoxTool();
    ModelingCreateBoxResult createBoxFromBounds(const ModelingCreateBoxRequest& request);
    ModelingBoxPreview updateBoxHover(const ModelingBoxToolEvent& event);
    ModelingBoxPreview beginBoxFootprint(const ModelingBoxToolEvent& event);
    ModelingBoxPreview updateBoxFootprint(const ModelingBoxToolEvent& event);
    ModelingBoxPreview beginBoxHeight(const ModelingBoxToolEvent& event);
    ModelingBoxPreview updateBoxHeight(const ModelingBoxToolEvent& event);
    ModelingCreateBoxResult commitBox(std::string name);
    ModelingCommandResult cancelBox();
    ModelingCreateBoxResult createMesh(const ModelingCreateMeshRequest& request);
    void projectObjectSelectionFromScene();
    ModelingCommandResult setObjectSelection(std::span<const EntityId> entities, EntityId active = InvalidEntityId);
    ModelingCommandResult setComponentSelection(const ModelingComponentSelectionRequest& request);
    ModelingCommandResult moveSelection(QVec3 delta);
    ModelingCommandResult transformSelection(const ModelingTransform3& transform, QVec3 pivot);
    ModelingCommandResult assignMaterialSlotToSelection(std::uint32_t materialSlot);
    ModelingCommandResult assignMaterialSlotToFace(int modelingObjectId, int faceId, std::uint32_t materialSlot);
    ModelingCommandResult setSelectionHover(const ModelingDirectHoverRequest& request);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace quader
