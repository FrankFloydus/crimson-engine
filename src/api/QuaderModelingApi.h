////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/QuaderApiResult.h"
#include "api/QuaderApiTypes.h"
#include "editor/modeling/ModelingTypes.h"
#include "editor/modeling/services/ModelingActionCatalog.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace quader {

class ProjectSession;
class QuaderApiMutationRunner;

/**
 * Exposes Quader Modeling API operations through the public Quader API surface.
 */
class QuaderModelingApi final {
public:
    QuaderModelingApi(ProjectSession& session, QuaderApiMutationRunner& mutations);

    QuaderApiResult<QuaderModelingObjectRef> createBox(const QuaderCreateBoxArgs& args);
    QuaderApiResult<QuaderModelingObjectRef> createBox(QuaderBounds bounds, std::string name = "Box");
    QuaderApiResult<QuaderModelingObjectRef> createBox(ModelingBounds bounds, std::string name = "Box");
    QuaderApiResult<QuaderModelingObjectRef> commitBoxPreview(
            std::string name = "Box",
            AssetId defaultMaterial = InvalidAssetId);

    QuaderApiResult<QuaderModelingObjectRef> createMesh(
            QuaderPolygonDocument document,
            QuaderCreateMeshArgs args = {});

    QuaderApiVoidResult setSelectionMode(ModelingSelectionMode mode);
    QuaderApiVoidResult executeTool(std::string_view toolId);

    QuaderApiVoidResult beginPreview(std::string_view actionId);
    QuaderApiVoidResult updatePreview(const QuaderPreviewSettings& settings);
    QuaderApiVoidResult commitPreview(std::string label = {});
    QuaderApiVoidResult cancelPreview();

    QuaderApiVoidResult beginTransformPreview(bool duplicateSelected = false);
    QuaderApiVoidResult updateTransformTranslate(QVec3 delta);
    QuaderApiVoidResult updateTransformMatrix(const ModelingTransform3& transform, QVec3 pivot);
    QuaderApiVoidResult updateTransformExtrude(QVec3 offset, float closedEdgeLedgeSize = 0.0f);
    QuaderApiVoidResult updateTransformInset(const ModelingTransform3& transform, QVec3 pivot);
    QuaderApiVoidResult commitTransformPreview(std::string label = {});
    QuaderApiVoidResult cancelTransformPreview();

    QuaderApiResult<ModelingTransformDragPreview> beginTransformDrag(const ModelingTransformDragBeginRequest& request);
    QuaderApiResult<ModelingTransformDragPreview> updateTransformDrag(const ModelingTransformDragUpdateRequest& request);
    QuaderApiResult<ModelingTransformDragPreview> commitTransformDrag();
    QuaderApiVoidResult cancelTransformDrag();

    QuaderApiVoidResult moveSelection(QVec3 delta);
    QuaderApiVoidResult rotateSelection(QVec3 eulerRadians, QVec3 pivot = {});
    QuaderApiVoidResult scaleSelection(QVec3 scale, QVec3 pivot = {});
    QuaderApiVoidResult transformSelection(const ModelingTransform3& transform, QVec3 pivot = {});
    QuaderApiVoidResult duplicateSelectedHierarchyForGroupDrag();

    QuaderApiVoidResult applyActiveMaterial(std::uint32_t materialSlot = 0);
    QuaderApiVoidResult assignMaterialSlotToSelection(std::uint32_t materialSlot);
    QuaderApiVoidResult assignMaterialSlotToFace(
            QuaderModelingObjectRef object,
            QuaderElementId face,
            std::uint32_t materialSlot);

    [[nodiscard]] std::vector<ModelingActionItem> availableActions(const ModelingActionQuery& query = {}) const;
    [[nodiscard]] std::vector<ModelingInstantOperationDescriptor> instantOperations() const;
    [[nodiscard]] ModelingSelectionSummary selectionSummary() const;
    [[nodiscard]] std::vector<ModelingObjectRecord> objects() const;

private:
    [[nodiscard]] QuaderModelingObjectRef refFromRecord(const ModelingObjectRecord& record) const;

    ProjectSession& session_;
    QuaderApiMutationRunner& mutations_;
};

} // namespace quader
