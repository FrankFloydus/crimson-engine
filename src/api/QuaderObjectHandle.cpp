#include "api/QuaderObjectHandle.h"

#include "api/QuaderApi.h"
#include "document/ProjectSession.h"

#include <utility>

namespace quader {

QuaderObjectHandle::QuaderObjectHandle(QuaderApi& api, QuaderModelingObjectRef ref)
    : api_(&api)
    , ref_(ref) {}

QuaderModelingObjectRef QuaderObjectHandle::ref() const {
    return ref_;
}

bool QuaderObjectHandle::valid() const {
    if (api_ == nullptr || ref_.entity == InvalidEntityId || ref_.sdkObjectId == InvalidModelingObjectId) {
        return false;
    }
    const ModelingObjectComponent* object = api_->session().scene().modelingObject(ref_.entity);
    return object != nullptr && object->objectId == ref_.sdkObjectId;
}

std::optional<QuaderEntitySnapshot> QuaderObjectHandle::entity() const {
    if (api_ == nullptr || ref_.entity == InvalidEntityId) {
        return std::nullopt;
    }
    return api_->query().entity(ref_.entity);
}

std::optional<MeshAsset> QuaderObjectHandle::compiledMesh() const {
    if (api_ == nullptr) {
        return std::nullopt;
    }
    return api_->session().modeling().compileObjectMesh(ref_.sdkObjectId);
}

QuaderApiVoidResult QuaderObjectHandle::select() {
    return api_->selection().selectObject(ref_);
}

QuaderApiVoidResult QuaderObjectHandle::rename(std::string name) {
    return api_->scene().rename(ref_.entity, std::move(name));
}

QuaderApiVoidResult QuaderObjectHandle::setTransform(TransformComponent transform) {
    return api_->scene().setTransform(ref_.entity, transform);
}

QuaderApiVoidResult QuaderObjectHandle::move(Vec3 delta) {
    QuaderApiVoidResult selected = select();
    if (!selected.ok) {
        return selected;
    }
    return api_->modeling().moveSelection(delta);
}

QuaderApiVoidResult QuaderObjectHandle::rotateEuler(Vec3 eulerRadians, Vec3 pivot) {
    QuaderApiVoidResult selected = select();
    if (!selected.ok) {
        return selected;
    }
    return api_->modeling().rotateSelection(eulerRadians, pivot);
}

QuaderApiVoidResult QuaderObjectHandle::scale(Vec3 scale, Vec3 pivot) {
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

QuaderApiResult<ModelingHoverSummary> QuaderObjectHandle::hoverVertex(QuaderElementId vertex) {
    return api_->selection().hoverVertex(ref_, vertex);
}

QuaderApiResult<ModelingHoverSummary> QuaderObjectHandle::hoverEdge(QuaderElementId a, QuaderElementId b) {
    return api_->selection().hoverEdge(ref_, a, b);
}

QuaderApiResult<ModelingHoverSummary> QuaderObjectHandle::hoverFace(QuaderElementId face) {
    return api_->selection().hoverFace(ref_, face);
}

} // namespace quader
