////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_result.hpp"
#include "api/quader_api_session.hpp"
#include "api/quader_api_types.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace quader {

class QuaderApiMutationRunner;

/**
 * Exposes Quader Modeling API operations through the public Quader API surface.
 */
class QuaderModelingApi final {
public:
    QuaderModelingApi(QuaderApiSession session, QuaderApiMutationRunner& mutations);

    QuaderApiResult<QuaderModelingObjectRef> createBox(const QuaderCreateBoxArgs& args);
    QuaderApiResult<QuaderModelingObjectRef> createBox(QuaderBounds bounds, std::string name = "Box");
    QuaderApiResult<QuaderModelingObjectRef> commitBoxPreview(
            std::string name = "Box",
            AssetId defaultMaterial = InvalidAssetId);

    QuaderApiResult<QuaderModelingObjectRef> createMesh(
            QuaderPolygonDocument document,
            QuaderCreateMeshArgs args = {});

    QuaderApiVoidResult setSelectionMode(QuaderSelectionMode mode);
    [[nodiscard]] bool canExecuteTool(std::string_view toolId) const;
    QuaderApiVoidResult executeTool(std::string_view toolId);

    QuaderApiVoidResult beginPreview(std::string_view actionId);
    QuaderApiVoidResult updatePreview(const QuaderPreviewSettings& settings);
    QuaderApiVoidResult commitPreview(std::string label = {});
    QuaderApiVoidResult cancelPreview();
    [[nodiscard]] bool canCancelActiveToolOrPreview() const;

    QuaderApiVoidResult beginTransformPreview(bool duplicateSelected = false);
    QuaderApiVoidResult updateTransformTranslate(QVec3 delta);
    QuaderApiVoidResult updateTransformMatrix(const QuaderTransform3& transform, QVec3 pivot);
    QuaderApiVoidResult updateTransformExtrude(QVec3 offset, float closedEdgeLedgeSize = 0.0f);
    QuaderApiVoidResult updateTransformInset(const QuaderTransform3& transform, QVec3 pivot);
    QuaderApiVoidResult commitTransformPreview(std::string label = {});
    QuaderApiVoidResult cancelTransformPreview();

    QuaderApiResult<QuaderTransformDragPreview> beginTransformDrag(const QuaderTransformDragBeginRequest& request);
    QuaderApiResult<QuaderTransformDragPreview> updateTransformDrag(const QuaderTransformDragUpdateRequest& request);
    QuaderApiResult<QuaderTransformDragPreview> commitTransformDrag();
    QuaderApiVoidResult cancelTransformDrag();

    QuaderApiVoidResult moveSelection(QVec3 delta);
    QuaderApiVoidResult rotateSelection(QVec3 eulerRadians, QVec3 pivot = {});
    QuaderApiVoidResult scaleSelection(QVec3 scale, QVec3 pivot = {});
    QuaderApiVoidResult transformSelection(const QuaderTransform3& transform, QVec3 pivot = {});
    QuaderApiVoidResult duplicateSelectedHierarchyForGroupDrag();

    QuaderApiVoidResult applyActiveMaterial(std::uint32_t materialSlot = 0);
    QuaderApiVoidResult assignMaterialSlotToSelection(std::uint32_t materialSlot);
    QuaderApiVoidResult assignMaterialSlotToFace(
            QuaderModelingObjectRef object,
            QuaderElementId face,
            std::uint32_t materialSlot);

    [[nodiscard]] QuaderActionSet availableActionSet(const QuaderActionQuery& query = {}) const;
    [[nodiscard]] std::vector<QuaderActionItem> availableActions(const QuaderActionQuery& query = {}) const;
    [[nodiscard]] std::vector<QuaderInstantOperationDescriptor> instantOperations() const;
    [[nodiscard]] QuaderSelectionSummary selectionSummary() const;
    [[nodiscard]] std::vector<QuaderModelingObjectRecord> objects() const;

private:
    [[nodiscard]] QuaderModelingObjectRef refFromRecord(const QuaderModelingObjectRecord& record) const;

    QuaderApiSession session_;
    QuaderApiMutationRunner& mutations_;
};

} // namespace quader
