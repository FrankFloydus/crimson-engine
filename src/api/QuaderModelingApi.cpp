////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/QuaderModelingApi.h"

#include "api/QuaderApiMutationRunner.h"
#include "scene/document/ProjectSession.h"
#include "editor/history/ModelingMementoCommand.h"
#include "editor/selection/EditorSelectionService.h"
#include "editor/modeling/services/ModelingSessionService.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <memory>
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
    auto command = std::make_unique<CreateBoxFromBoundsHistoryCommand>(
            session_.modeling(),
            ModelingCreateBoxRequest{
                    .bounds = args.bounds,
                    .name = args.name,
            });
    CreateBoxFromBoundsHistoryCommand* createCommand = command.get();
    QuaderApiVoidResult status = mutations_.runModelingCommand(std::move(command), "Create Box");

    QuaderModelingObjectRef created;
    if (status.ok && status.changed) {
        created = refFromRecord(createCommand->created().object);
    }
    return resultFromCreate(std::move(status), created);
}

QuaderApiResult<QuaderModelingObjectRef> QuaderModelingApi::createBox(ModelingBounds bounds, std::string name) {
    return createBox(QuaderCreateBoxArgs{
            .bounds = bounds,
            .name = std::move(name),
    });
}

QuaderApiResult<QuaderModelingObjectRef> QuaderModelingApi::createMesh(
        QuaderPolygonDocument document,
        QuaderCreateMeshArgs args) {
    ModelingCreateMeshRequest request;
    request.document = std::move(document);
    request.name = std::move(args.name);
    request.materialSlot = args.materialSlot;
    auto command = std::make_unique<CreateMeshHistoryCommand>(session_.modeling(), std::move(request));
    CreateMeshHistoryCommand* createCommand = command.get();
    QuaderApiVoidResult status = mutations_.runModelingCommand(std::move(command), "Create Mesh");

    QuaderModelingObjectRef created;
    if (status.ok && status.changed) {
        created = refFromRecord(createCommand->created().object);
    }
    return resultFromCreate(std::move(status), created);
}

QuaderApiVoidResult QuaderModelingApi::setSelectionMode(ModelingSelectionMode mode) {
    return mutations_.runModelingResult(session_.selection().setMode(mode), "Set Selection Mode");
}

QuaderApiVoidResult QuaderModelingApi::executeTool(std::string_view toolId) {
    return mutations_.runTrackedModelingTool(toolId);
}

QuaderApiVoidResult QuaderModelingApi::beginPreview(std::string_view actionId) {
    return mutations_.runTrackedModelingAction(actionId);
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
    return mutations_.runModelingCommand(
            std::make_unique<CommitTransformPreviewHistoryCommand>(session_.modeling()),
            label.empty() ? "Commit Transform Preview" : label);
}

QuaderApiVoidResult QuaderModelingApi::cancelTransformPreview() {
    return mutations_.runModelingResult(
            session_.modeling().cancelTransformPreview(),
            "Cancel Transform Preview");
}

QuaderApiVoidResult QuaderModelingApi::moveSelection(Vec3 delta) {
    return mutations_.runModelingCommand(
            std::make_unique<MoveSelectionHistoryCommand>(session_.modeling(), delta),
            "Move Selection");
}

QuaderApiVoidResult QuaderModelingApi::rotateSelection(Vec3 eulerRadians, Vec3 pivot) {
    return transformSelection(rotationTransform(eulerRadians), pivot);
}

QuaderApiVoidResult QuaderModelingApi::scaleSelection(Vec3 scale, Vec3 pivot) {
    return transformSelection(scaleTransform(scale), pivot);
}

QuaderApiVoidResult QuaderModelingApi::transformSelection(const ModelingTransform3& transform, Vec3 pivot) {
    return mutations_.runModelingCommand(
            std::make_unique<TransformSelectionHistoryCommand>(session_.modeling(), transform, pivot),
            "Transform Selection");
}

QuaderApiVoidResult QuaderModelingApi::duplicateSelectedHierarchyForGroupDrag() {
    return mutations_.runModelingResult(
            session_.modeling().duplicateSelectedHierarchyForGroupDrag(session_.documentCommands()),
            "Duplicate Group Selection");
}

QuaderApiVoidResult QuaderModelingApi::applyActiveMaterial(std::uint32_t materialSlot) {
    return mutations_.runModelingCommand(
            std::make_unique<ApplyActiveMaterialHistoryCommand>(session_.modeling(), materialSlot),
            "Apply Active Material");
}

QuaderApiVoidResult QuaderModelingApi::assignMaterialSlotToSelection(std::uint32_t materialSlot) {
    return mutations_.runModelingCommand(
            std::make_unique<AssignMaterialSlotToSelectionHistoryCommand>(session_.modeling(), materialSlot),
            "Assign Material Slot");
}

QuaderApiVoidResult QuaderModelingApi::assignMaterialSlotToFace(
            QuaderModelingObjectRef object,
            QuaderElementId face,
            std::uint32_t materialSlot) {
    return mutations_.runModelingCommand(
            std::make_unique<AssignMaterialSlotToFaceHistoryCommand>(
                    session_.modeling(),
                    object.modelingObjectId,
                    face,
                    materialSlot),
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
    ref.editorObjectId = session_.scene().editorObjectId(record.entity);
    ref.modelingObjectId = record.modelingObjectId;
    ref.mesh = record.mesh;
    if (const MaterialComponent* material = session_.scene().material(record.entity)) {
        ref.material = material->material;
    }
    return ref;
}

} // namespace quader
