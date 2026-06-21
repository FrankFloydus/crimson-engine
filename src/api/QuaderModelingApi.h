#pragma once

#include "api/QuaderApiResult.h"
#include "api/QuaderApiTypes.h"

#if QUADER_HAS_MODELING_SDK
#include <quader_poly/document.h>
#endif

#include <string>
#include <string_view>
#include <vector>

namespace quader {

class ProjectSession;
class QuaderApiMutationRunner;

class QuaderModelingApi final {
public:
    QuaderModelingApi(ProjectSession& session, QuaderApiMutationRunner& mutations);

    QuaderApiResult<QuaderModelingObjectRef> createBox(const QuaderCreateBoxArgs& args);
    QuaderApiResult<QuaderModelingObjectRef> createBox(ModelingBounds bounds, std::string name = "Box");

#if QUADER_HAS_MODELING_SDK
    QuaderApiResult<QuaderModelingObjectRef> createMesh(
            quader_poly::Document document,
            QuaderCreateMeshArgs args = {});
#endif

    QuaderApiVoidResult setSelectionMode(ModelingSelectionMode mode);
    QuaderApiVoidResult executeTool(std::string_view toolId);
    QuaderApiVoidResult executeAction(std::string_view actionId);

    QuaderApiVoidResult beginPreview(std::string_view actionId);
    QuaderApiVoidResult updatePreview(const QuaderPreviewSettings& settings);
    QuaderApiVoidResult commitPreview(std::string label = {});
    QuaderApiVoidResult cancelPreview();

    QuaderApiVoidResult beginTransformPreview(bool duplicateSelected = false);
    QuaderApiVoidResult updateTransformTranslate(Vec3 delta);
    QuaderApiVoidResult updateTransformMatrix(const ModelingTransform3& transform, Vec3 pivot);
    QuaderApiVoidResult updateTransformExtrude(Vec3 offset, float closedEdgeLedgeSize = 0.0f);
    QuaderApiVoidResult updateTransformInset(const ModelingTransform3& transform, Vec3 pivot);
    QuaderApiVoidResult commitTransformPreview(std::string label = {});
    QuaderApiVoidResult cancelTransformPreview();

    QuaderApiVoidResult moveSelection(Vec3 delta);
    QuaderApiVoidResult rotateSelection(Vec3 eulerRadians, Vec3 pivot = {});
    QuaderApiVoidResult scaleSelection(Vec3 scale, Vec3 pivot = {});
    QuaderApiVoidResult transformSelection(const ModelingTransform3& transform, Vec3 pivot = {});

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
