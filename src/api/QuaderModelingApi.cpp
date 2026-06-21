#include "api/QuaderModelingApi.h"

#include "api/QuaderApiMutationRunner.h"
#include "document/ProjectSession.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <utility>

namespace quader {
namespace {

QuaderApiResult<QuaderModelingObjectRef> resultFromCreate(
        QuaderApiVoidResult status,
        QuaderModelingObjectRef value) {
    QuaderApiResult<QuaderModelingObjectRef> result;
    result.ok = status.ok;
    result.changed = status.changed;
    result.message = std::move(status.message);
    result.mutationKind = status.mutationKind;
    result.revisions = status.revisions;
    result.value = value;
    return result;
}

ModelingTransform3 rotationTransform(Vec3 eulerRadians) {
    const glm::mat3 rotation = glm::mat3_cast(toGlm(quatFromEulerRadians(eulerRadians)));
    return {
            .xAxis = fromGlm(glm::vec3(rotation[0])),
            .yAxis = fromGlm(glm::vec3(rotation[1])),
            .zAxis = fromGlm(glm::vec3(rotation[2])),
            .origin = {},
    };
}

ModelingTransform3 scaleTransform(Vec3 scale) {
    return {
            .xAxis = {scale.x, 0.0f, 0.0f},
            .yAxis = {0.0f, scale.y, 0.0f},
            .zAxis = {0.0f, 0.0f, scale.z},
            .origin = {},
    };
}

} // namespace

QuaderModelingApi::QuaderModelingApi(ProjectSession& session, QuaderApiMutationRunner& mutations)
    : session_(session)
    , mutations_(mutations) {}

QuaderApiResult<QuaderModelingObjectRef> QuaderModelingApi::createBox(const QuaderCreateBoxArgs& args) {
    const ModelingCreateBoxResult created = session_.modeling().createBoxFromBounds(ModelingCreateBoxRequest{
            .bounds = args.bounds,
            .name = args.name,
    });
    QuaderApiVoidResult status = mutations_.runModelingResult(
            ModelingCommandResult{
                    .ok = created.ok,
                    .changed = created.changed,
                    .message = created.message,
            },
            "Create Box");
    return resultFromCreate(std::move(status), refFromRecord(created.object));
}

QuaderApiResult<QuaderModelingObjectRef> QuaderModelingApi::createBox(ModelingBounds bounds, std::string name) {
    return createBox(QuaderCreateBoxArgs{
            .bounds = bounds,
            .name = std::move(name),
    });
}

#if QUADER_HAS_MODELING_SDK
QuaderApiResult<QuaderModelingObjectRef> QuaderModelingApi::createMesh(
        quader_poly::Document document,
        QuaderCreateMeshArgs args) {
    ModelingCreateMeshRequest request;
    request.document = std::move(document);
    request.name = std::move(args.name);
    request.materialSlot = args.materialSlot;
    const ModelingCreateBoxResult created = session_.modeling().createMesh(request);
    QuaderApiVoidResult status = mutations_.runModelingResult(
            ModelingCommandResult{
                    .ok = created.ok,
                    .changed = created.changed,
                    .message = created.message,
            },
            "Create Mesh");
    return resultFromCreate(std::move(status), refFromRecord(created.object));
}
#endif

QuaderApiVoidResult QuaderModelingApi::setSelectionMode(ModelingSelectionMode mode) {
    return mutations_.runModelingResult(session_.modeling().setSelectionMode(mode), "Set Selection Mode");
}

QuaderApiVoidResult QuaderModelingApi::executeTool(std::string_view toolId) {
    return mutations_.runTrackedModelingTool(toolId);
}

QuaderApiVoidResult QuaderModelingApi::executeAction(std::string_view actionId) {
    return mutations_.runTrackedModelingAction(actionId);
}

QuaderApiVoidResult QuaderModelingApi::beginPreview(std::string_view actionId) {
    return executeAction(actionId);
}

QuaderApiVoidResult QuaderModelingApi::updatePreview(const QuaderPreviewSettings& settings) {
    return mutations_.runModelingResult(
            session_.modeling().setInteractionSettings(settings.interaction),
            "Update Operation Preview");
}

QuaderApiVoidResult QuaderModelingApi::commitPreview(std::string label) {
    return mutations_.commitModelingPreview(std::move(label));
}

QuaderApiVoidResult QuaderModelingApi::cancelPreview() {
    return mutations_.cancelModelingPreview();
}

QuaderApiVoidResult QuaderModelingApi::beginTransformPreview(bool duplicateSelected) {
    return mutations_.runModelingResult(
            session_.modeling().beginTransformPreview(duplicateSelected),
            "Begin Transform Preview");
}

QuaderApiVoidResult QuaderModelingApi::updateTransformTranslate(Vec3 delta) {
    return mutations_.runModelingResult(
            session_.modeling().updateTransformTranslatePreview(delta),
            "Update Transform Preview");
}

QuaderApiVoidResult QuaderModelingApi::updateTransformMatrix(const ModelingTransform3& transform, Vec3 pivot) {
    return mutations_.runModelingResult(
            session_.modeling().updateTransformMatrixPreview(transform, pivot),
            "Update Transform Preview");
}

QuaderApiVoidResult QuaderModelingApi::updateTransformExtrude(Vec3 offset, float closedEdgeLedgeSize) {
    return mutations_.runModelingResult(
            session_.modeling().updateTransformExtrudePreview(offset, closedEdgeLedgeSize),
            "Update Transform Preview");
}

QuaderApiVoidResult QuaderModelingApi::updateTransformInset(const ModelingTransform3& transform, Vec3 pivot) {
    return mutations_.runModelingResult(
            session_.modeling().updateTransformInsetPreview(transform, pivot),
            "Update Transform Preview");
}

QuaderApiVoidResult QuaderModelingApi::commitTransformPreview(std::string label) {
    return mutations_.runModelingResult(
            session_.modeling().commitTransformPreview(),
            label.empty() ? "Commit Transform Preview" : label);
}

QuaderApiVoidResult QuaderModelingApi::cancelTransformPreview() {
    return mutations_.runModelingResult(
            session_.modeling().cancelTransformPreview(),
            "Cancel Transform Preview");
}

QuaderApiVoidResult QuaderModelingApi::moveSelection(Vec3 delta) {
    return mutations_.runModelingResult(session_.modeling().moveSelection(delta), "Move Selection");
}

QuaderApiVoidResult QuaderModelingApi::rotateSelection(Vec3 eulerRadians, Vec3 pivot) {
    return transformSelection(rotationTransform(eulerRadians), pivot);
}

QuaderApiVoidResult QuaderModelingApi::scaleSelection(Vec3 scale, Vec3 pivot) {
    return transformSelection(scaleTransform(scale), pivot);
}

QuaderApiVoidResult QuaderModelingApi::transformSelection(const ModelingTransform3& transform, Vec3 pivot) {
    return mutations_.runModelingResult(
            session_.modeling().transformSelection(transform, pivot),
            "Transform Selection");
}

QuaderApiVoidResult QuaderModelingApi::applyActiveMaterial(std::uint32_t materialSlot) {
    return mutations_.runModelingResult(
            session_.modeling().applyActiveMaterial(materialSlot),
            "Apply Active Material");
}

QuaderApiVoidResult QuaderModelingApi::assignMaterialSlotToSelection(std::uint32_t materialSlot) {
    return mutations_.runModelingResult(
            session_.modeling().assignMaterialSlotToSelection(materialSlot),
            "Assign Material Slot");
}

QuaderApiVoidResult QuaderModelingApi::assignMaterialSlotToFace(
        QuaderModelingObjectRef object,
        QuaderElementId face,
        std::uint32_t materialSlot) {
    return mutations_.runModelingResult(
            session_.modeling().assignMaterialSlotToFace(object.sdkObjectId, face, materialSlot),
            "Assign Face Material Slot");
}

std::vector<ModelingActionItem> QuaderModelingApi::availableActions(const ModelingActionQuery& query) const {
    return session_.modeling().contextActions(query);
}

std::vector<ModelingInstantOperationDescriptor> QuaderModelingApi::instantOperations() const {
    return session_.modeling().instantOperations();
}

ModelingSelectionSummary QuaderModelingApi::selectionSummary() const {
    return session_.modeling().selectionSummary();
}

std::vector<ModelingObjectRecord> QuaderModelingApi::objects() const {
    return session_.modeling().objects();
}

QuaderModelingObjectRef QuaderModelingApi::refFromRecord(const ModelingObjectRecord& record) const {
    QuaderModelingObjectRef ref;
    ref.entity = record.entity;
    ref.sdkObjectId = record.sdkObjectId;
    ref.mesh = record.mesh;
    if (const MaterialComponent* material = session_.scene().material(record.entity)) {
        ref.material = material->material;
    }
    return ref;
}

} // namespace quader
