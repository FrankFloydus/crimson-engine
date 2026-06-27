////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_object_handle.hpp"

#include "api/quader_api.hpp"
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_api_type_conversions.hpp"
#include "scene/document/project_session.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"

#include <utility>

namespace quader {

QuaderObjectHandle::QuaderObjectHandle(QuaderApi& api, QuaderModelingObjectRef ref)
    : api_(&api)
    , ref_(ref) {}

QuaderModelingObjectRef QuaderObjectHandle::ref() const {
    return ref_;
}

bool QuaderObjectHandle::valid() const {
    if (api_ == nullptr || ref_.entity == InvalidEntityId || ref_.modelingObjectId == InvalidModelingObjectId) {
        return false;
    }
    const ModelingObjectComponent* object =
            QuaderApiSessionInternalAccess::projectSession(api_->session()).scene().modelingObject(ref_.entity);
    return object != nullptr && object->objectId == ref_.modelingObjectId;
}

std::optional<QuaderEntitySnapshot> QuaderObjectHandle::entity() const {
    if (api_ == nullptr || ref_.entity == InvalidEntityId) {
        return std::nullopt;
    }
    return api_->query().entity(ref_.entity);
}

std::optional<QuaderMeshAsset> QuaderObjectHandle::compiledMesh() const {
    if (api_ == nullptr) {
        return std::nullopt;
    }
    const std::optional<MeshAsset> mesh =
            QuaderApiSessionInternalAccess::projectSession(api_->session()).modeling().compileObjectMesh(ref_.modelingObjectId);
    return mesh ? std::optional<QuaderMeshAsset>{toApiMeshAsset(*mesh)} : std::nullopt;
}

QuaderApiVoidResult QuaderObjectHandle::select() {
    return api_->selection().selectObject(ref_);
}

QuaderApiVoidResult QuaderObjectHandle::rename(std::string name) {
    return api_->scene().rename(ref_.entity, std::move(name));
}

QuaderApiVoidResult QuaderObjectHandle::destroy() {
    return api_->scene().destroy(ref_.entity);
}

QuaderApiResult<QuaderEntityRef> QuaderObjectHandle::duplicate() {
    return api_->scene().duplicate(ref_.entity);
}

QuaderApiVoidResult QuaderObjectHandle::setTransform(QuaderTransform transform) {
    return api_->scene().setTransform(ref_.entity, transform);
}

QuaderApiVoidResult QuaderObjectHandle::setVisible(bool visible) {
    return api_->scene().setVisible(ref_.entity, visible);
}

QuaderApiVoidResult QuaderObjectHandle::setLocked(bool locked) {
    return api_->scene().setLocked(ref_.entity, locked);
}

QuaderApiVoidResult QuaderObjectHandle::move(QVec3 delta) {
    QuaderApiVoidResult selected = select();
    if (!selected.ok) {
        return selected;
    }
    return api_->modeling().moveSelection(delta);
}

QuaderApiVoidResult QuaderObjectHandle::rotateEuler(QVec3 eulerRadians, QVec3 pivot) {
    QuaderApiVoidResult selected = select();
    if (!selected.ok) {
        return selected;
    }
    return api_->modeling().rotateSelection(eulerRadians, pivot);
}

QuaderApiVoidResult QuaderObjectHandle::scale(QVec3 scale, QVec3 pivot) {
    QuaderApiVoidResult selected = select();
    if (!selected.ok) {
        return selected;
    }
    return api_->modeling().scaleSelection(scale, pivot);
}

QuaderApiVoidResult QuaderObjectHandle::assignMaterial(AssetId material) {
    return api_->assets().assignMaterialToObject(ref_, material);
}

QuaderApiVoidResult QuaderObjectHandle::assignMaterialSlot(std::uint32_t materialSlot) {
    QuaderApiVoidResult selected = select();
    if (!selected.ok) {
        return selected;
    }
    return api_->modeling().assignMaterialSlotToSelection(materialSlot);
}

QuaderApiVoidResult QuaderObjectHandle::selectVertex(QuaderElementId vertex, QuaderSelectionEdit edit) {
    return api_->selection().selectVertex(ref_, vertex, edit);
}

QuaderApiVoidResult QuaderObjectHandle::selectEdge(
        QuaderElementId a,
        QuaderElementId b,
        QuaderSelectionEdit edit) {
    return api_->selection().selectEdge(ref_, a, b, edit);
}

QuaderApiVoidResult QuaderObjectHandle::selectFace(QuaderElementId face, QuaderSelectionEdit edit) {
    return api_->selection().selectFace(ref_, face, edit);
}

QuaderApiResult<QuaderHoverSummary> QuaderObjectHandle::hoverVertex(QuaderElementId vertex) {
    return api_->selection().hoverVertex(ref_, vertex);
}

QuaderApiResult<QuaderHoverSummary> QuaderObjectHandle::hoverEdge(QuaderElementId a, QuaderElementId b) {
    return api_->selection().hoverEdge(ref_, a, b);
}

QuaderApiResult<QuaderHoverSummary> QuaderObjectHandle::hoverFace(QuaderElementId face) {
    return api_->selection().hoverFace(ref_, face);
}

} // namespace quader
