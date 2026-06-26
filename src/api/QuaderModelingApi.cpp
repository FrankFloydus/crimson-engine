////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/QuaderModelingApi.h"
#include "api/QuaderApiSessionInternalAccess.h"

#include "api/QuaderApiTypeConversions.h"
#include "api/QuaderApiMutationRunner.h"
#include "api/QuaderPolygonDocumentInternalAccess.h"
#include "scene/document/ProjectSession.h"
#include "editor/commands/modeling/CommitBoxPreviewCommand.h"
#include "editor/history/ModelingMementoCommand.h"
#include "editor/selection/EditorSelectionService.h"
#include "editor/modeling/services/ModelingSessionService.h"
#include "math/GlmBridge.h"

#include <glm/gtc/quaternion.hpp>

#include <memory>
#include <utility>

namespace quader {
namespace {

ProjectSession& projectSession(QuaderApiSession session) {
    return QuaderApiSessionInternalAccess::projectSession(session);
}


QuaderApiResult<QuaderModelingObjectRef> resultFromCreate(
        QuaderApiVoidResult status,
        QuaderModelingObjectRef value) {
    return quaderApiResultFromStatus(status, value);
}

QuaderApiResult<QuaderTransformDragPreview> resultFromTransformDragPreview(
        const ProjectSession& session,
        ModelingTransformDragPreview preview) {
    QuaderApiResult<QuaderTransformDragPreview> result;
    result.ok = preview.ok;
    result.changed = preview.changed;
    result.message = preview.message;
    result.mutationKind =
            preview.changed ? QuaderMutationKind::Modeling :
            preview.active ? QuaderMutationKind::Runtime :
            QuaderMutationKind::None;
    const ModelingSelectionSummary summary = session.modeling().selectionSummary();
    result.revisions = {
            .projectRevision = session.document().revision(),
            .sceneRevision = session.scene().revision(),
            .assetRevision = session.assets().revision(),
            .modelingContentRevision = summary.contentRevision,
            .modelingSelectionRevision = summary.selectionRevision,
    };
    result.value = toApiTransformDragPreview(preview);
    return result;
}

QuaderTransform3 rotationTransform(QVec3 eulerRadians) {
    const glm::mat3 rotation = glm::mat3_cast(toGlm(quatFromEulerRadians(eulerRadians)));
    return {
            .xAxis = fromGlm(glm::vec3(rotation[0])),
            .yAxis = fromGlm(glm::vec3(rotation[1])),
            .zAxis = fromGlm(glm::vec3(rotation[2])),
            .origin = {},
    };
}

QuaderTransform3 scaleTransform(QVec3 scale) {
    return {
            .xAxis = {scale.x, 0.0f, 0.0f},
            .yAxis = {0.0f, scale.y, 0.0f},
            .zAxis = {0.0f, 0.0f, scale.z},
            .origin = {},
    };
}

} // namespace

QuaderModelingApi::QuaderModelingApi(QuaderApiSession session, QuaderApiMutationRunner& mutations)
    : session_(session)
    , mutations_(mutations) {}

QuaderApiResult<QuaderModelingObjectRef> QuaderModelingApi::createBox(const QuaderCreateBoxArgs& args) {
    auto command = std::make_unique<CreateBoxFromBoundsHistoryCommand>(
            projectSession(session_).modeling(),
            ModelingCreateBoxRequest{
                    .bounds = toModelingBounds(args.bounds),
                    .name = args.name,
            });
    CreateBoxFromBoundsHistoryCommand* createCommand = command.get();
    QuaderApiVoidResult status = mutations_.runModelingCommand(std::move(command), "Create Box");

    QuaderModelingObjectRef created;
    if (status.ok && status.changed) {
        created = refFromRecord(toApiModelingObjectRecord(createCommand->created().object));
    }
    return resultFromCreate(std::move(status), created);
}

QuaderApiResult<QuaderModelingObjectRef> QuaderModelingApi::createBox(QuaderBounds bounds, std::string name) {
    return createBox(QuaderCreateBoxArgs{
            .bounds = bounds,
            .name = std::move(name),
    });
}

QuaderApiResult<QuaderModelingObjectRef> QuaderModelingApi::commitBoxPreview(
        std::string name,
        AssetId defaultMaterial) {
    auto command = std::make_unique<CommitBoxPreviewCommand>(
            projectSession(session_).modeling(),
            projectSession(session_).scene(),
            std::move(name),
            defaultMaterial);
    CommitBoxPreviewCommand* commitCommand = command.get();
    QuaderApiVoidResult status = mutations_.runModelingUndoableCommand(
            std::move(command),
            "Create Box");

    QuaderModelingObjectRef created;
    if (status.ok && status.changed) {
        created = refFromRecord(toApiModelingObjectRecord(commitCommand->committed().object));
    }
    return resultFromCreate(std::move(status), created);
}

QuaderApiResult<QuaderModelingObjectRef> QuaderModelingApi::createMesh(
        QuaderPolygonDocument document,
        QuaderCreateMeshArgs args) {
    ModelingCreateMeshRequest request;
    request.document = QuaderPolygonDocumentInternalAccess::toBackend(document);
    request.name = std::move(args.name);
    request.materialSlot = args.materialSlot;
    auto command = std::make_unique<CreateMeshHistoryCommand>(projectSession(session_).modeling(), std::move(request));
    CreateMeshHistoryCommand* createCommand = command.get();
    QuaderApiVoidResult status = mutations_.runModelingCommand(std::move(command), "Create Mesh");

    QuaderModelingObjectRef created;
    if (status.ok && status.changed) {
        created = refFromRecord(toApiModelingObjectRecord(createCommand->created().object));
    }
    return resultFromCreate(std::move(status), created);
}

QuaderApiVoidResult QuaderModelingApi::setSelectionMode(QuaderSelectionMode mode) {
    return mutations_.runModelingResult(
            projectSession(session_).selection().setMode(toModelingSelectionMode(mode)),
            "Set Selection Mode");
}

bool QuaderModelingApi::canExecuteTool(std::string_view toolId) const {
    if (toolId == "delete_selection" && projectSession(session_).modeling().hasSelectedSceneGroupRoot()) {
        return true;
    }
    return mutations_.canRunTrackedModelingTool(toolId);
}

QuaderApiVoidResult QuaderModelingApi::executeTool(std::string_view toolId) {
    return mutations_.runTrackedModelingTool(toolId);
}

QuaderApiVoidResult QuaderModelingApi::beginPreview(std::string_view actionId) {
    return mutations_.beginTrackedModelingPreview(actionId);
}

QuaderApiVoidResult QuaderModelingApi::updatePreview(const QuaderPreviewSettings& settings) {
    return mutations_.runModelingResult(
            projectSession(session_).modeling().setInteractionSettings(toModelingInteractionSettings(settings.interaction)),
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
            projectSession(session_).modeling().beginTransformPreview(duplicateSelected),
            "Begin Transform Preview");
}

QuaderApiVoidResult QuaderModelingApi::updateTransformTranslate(QVec3 delta) {
    return mutations_.runModelingResult(
            projectSession(session_).modeling().updateTransformTranslatePreview(delta),
            "Update Transform Preview");
}

QuaderApiVoidResult QuaderModelingApi::updateTransformMatrix(const QuaderTransform3& transform, QVec3 pivot) {
    return mutations_.runModelingResult(
            projectSession(session_).modeling().updateTransformMatrixPreview(toModelingTransform3(transform), pivot),
            "Update Transform Preview");
}

QuaderApiVoidResult QuaderModelingApi::updateTransformExtrude(QVec3 offset, float closedEdgeLedgeSize) {
    return mutations_.runModelingResult(
            projectSession(session_).modeling().updateTransformExtrudePreview(offset, closedEdgeLedgeSize),
            "Update Transform Preview");
}

QuaderApiVoidResult QuaderModelingApi::updateTransformInset(const QuaderTransform3& transform, QVec3 pivot) {
    return mutations_.runModelingResult(
            projectSession(session_).modeling().updateTransformInsetPreview(toModelingTransform3(transform), pivot),
            "Update Transform Preview");
}

QuaderApiVoidResult QuaderModelingApi::commitTransformPreview(std::string label) {
    return mutations_.runModelingCommand(
            std::make_unique<CommitTransformPreviewHistoryCommand>(projectSession(session_).modeling()),
            label.empty() ? "Commit Transform Preview" : label);
}

QuaderApiVoidResult QuaderModelingApi::cancelTransformPreview() {
    return mutations_.runModelingResult(
            projectSession(session_).modeling().cancelTransformPreview(),
            "Cancel Transform Preview");
}

QuaderApiResult<QuaderTransformDragPreview> QuaderModelingApi::beginTransformDrag(
        const QuaderTransformDragBeginRequest& request) {
    return resultFromTransformDragPreview(
            projectSession(session_),
            projectSession(session_).modeling().beginTransformDrag(toModelingTransformDragBeginRequest(request)));
}

QuaderApiResult<QuaderTransformDragPreview> QuaderModelingApi::updateTransformDrag(
        const QuaderTransformDragUpdateRequest& request) {
    return resultFromTransformDragPreview(
            projectSession(session_),
            projectSession(session_).modeling().updateTransformDrag(toModelingTransformDragUpdateRequest(request)));
}

QuaderApiResult<QuaderTransformDragPreview> QuaderModelingApi::commitTransformDrag() {
    return resultFromTransformDragPreview(
            projectSession(session_),
            projectSession(session_).modeling().commitTransformDrag(projectSession(session_).history()));
}

QuaderApiVoidResult QuaderModelingApi::cancelTransformDrag() {
    return mutations_.runModelingResult(
            projectSession(session_).modeling().cancelTransformDrag(),
            "Cancel Transform Drag");
}

QuaderApiVoidResult QuaderModelingApi::moveSelection(QVec3 delta) {
    return mutations_.runModelingCommand(
            std::make_unique<MoveSelectionHistoryCommand>(projectSession(session_).modeling(), delta),
            "Move Selection");
}

QuaderApiVoidResult QuaderModelingApi::rotateSelection(QVec3 eulerRadians, QVec3 pivot) {
    return transformSelection(rotationTransform(eulerRadians), pivot);
}

QuaderApiVoidResult QuaderModelingApi::scaleSelection(QVec3 scale, QVec3 pivot) {
    return transformSelection(scaleTransform(scale), pivot);
}

QuaderApiVoidResult QuaderModelingApi::transformSelection(const QuaderTransform3& transform, QVec3 pivot) {
    return mutations_.runModelingCommand(
            std::make_unique<TransformSelectionHistoryCommand>(projectSession(session_).modeling(), toModelingTransform3(transform), pivot),
            "Transform Selection");
}

QuaderApiVoidResult QuaderModelingApi::duplicateSelectedHierarchyForGroupDrag() {
    return mutations_.runModelingResult(
            projectSession(session_).modeling().duplicateSelectedHierarchyForGroupDrag(projectSession(session_).documentCommands()),
            "Duplicate Group Selection");
}

QuaderApiVoidResult QuaderModelingApi::applyActiveMaterial(std::uint32_t materialSlot) {
    return mutations_.runModelingCommand(
            std::make_unique<ApplyActiveMaterialHistoryCommand>(projectSession(session_).modeling(), materialSlot),
            "Apply Active Material");
}

QuaderApiVoidResult QuaderModelingApi::assignMaterialSlotToSelection(std::uint32_t materialSlot) {
    return mutations_.runModelingCommand(
            std::make_unique<AssignMaterialSlotToSelectionHistoryCommand>(projectSession(session_).modeling(), materialSlot),
            "Assign Material Slot");
}

QuaderApiVoidResult QuaderModelingApi::assignMaterialSlotToFace(
            QuaderModelingObjectRef object,
            QuaderElementId face,
            std::uint32_t materialSlot) {
    return mutations_.runModelingCommand(
            std::make_unique<AssignMaterialSlotToFaceHistoryCommand>(
                    projectSession(session_).modeling(),
                    object.modelingObjectId,
                    face,
                    materialSlot),
            "Assign Face Material Slot");
}

QuaderActionSet QuaderModelingApi::availableActionSet(const QuaderActionQuery& query) const {
    return toApiActionSet(projectSession(session_).modeling().contextActionSet(toModelingActionQuery(query)));
}

std::vector<QuaderActionItem> QuaderModelingApi::availableActions(const QuaderActionQuery& query) const {
    const std::vector<ModelingActionItem> actions = projectSession(session_).modeling().contextActions(toModelingActionQuery(query));
    std::vector<QuaderActionItem> result;
    result.reserve(actions.size());
    for (const ModelingActionItem& action : actions) {
        result.push_back(toApiActionItem(action));
    }
    return result;
}

std::vector<QuaderInstantOperationDescriptor> QuaderModelingApi::instantOperations() const {
    const std::vector<ModelingInstantOperationDescriptor> operations = projectSession(session_).modeling().instantOperations();
    std::vector<QuaderInstantOperationDescriptor> result;
    result.reserve(operations.size());
    for (const ModelingInstantOperationDescriptor& operation : operations) {
        result.push_back(toApiInstantOperationDescriptor(operation));
    }
    return result;
}

QuaderSelectionSummary QuaderModelingApi::selectionSummary() const {
    return toApiSelectionSummary(projectSession(session_).modeling().selectionSummary());
}

std::vector<QuaderModelingObjectRecord> QuaderModelingApi::objects() const {
    const std::vector<ModelingObjectRecord> records = projectSession(session_).modeling().objects();
    std::vector<QuaderModelingObjectRecord> result;
    result.reserve(records.size());
    for (const ModelingObjectRecord& record : records) {
        result.push_back(toApiModelingObjectRecord(record));
    }
    return result;
}

QuaderModelingObjectRef QuaderModelingApi::refFromRecord(const QuaderModelingObjectRecord& record) const {
    QuaderModelingObjectRef ref;
    ref.entity = record.entity;
    ref.editorObjectId = projectSession(session_).scene().editorObjectId(record.entity);
    ref.modelingObjectId = record.modelingObjectId;
    ref.mesh = record.mesh;
    if (const MaterialComponent* material = projectSession(session_).scene().material(record.entity)) {
        ref.material = material->material;
    }
    return ref;
}

} // namespace quader
