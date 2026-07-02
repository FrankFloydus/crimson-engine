////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_object_handle.hpp"

#include "api/quader_api.hpp"

#include <utility>

namespace quader {

QuaderObjectHandle::QuaderObjectHandle(QuaderApi& api, QuaderModelingObjectRef ref)
    : api_(&api)
    , ref_(ref) {}

QuaderModelingObjectRef QuaderObjectHandle::ref() const {
    return ref_;
}

bool QuaderObjectHandle::valid() const {
  if (api_ == nullptr || ref_.entity == kInvalidEntityId ||
      ref_.modeling_object_id == kInvalidQuaderModelingObjectId) {
    return false;
  }
  const std::optional<QuaderModelingObjectRef> queried =
      api_->query().modeling_object_for_entity(ref_.entity);
  return queried.has_value() &&
         queried->modeling_object_id == ref_.modeling_object_id;
}

std::optional<QuaderEntitySnapshot> QuaderObjectHandle::entity() const {
  if (api_ == nullptr || ref_.entity == kInvalidEntityId) {
    return std::nullopt;
  }
    return api_->query().entity(ref_.entity);
}

std::optional<QuaderMeshAsset> QuaderObjectHandle::compiled_mesh() const {
  if (api_ == nullptr) {
    return std::nullopt;
  }
  return api_->query().compiled_object_mesh(ref_);
}

QuaderApiVoidResult QuaderObjectHandle::select() {
  return api_->selection().select_object(ref_);
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

QuaderApiVoidResult
QuaderObjectHandle::set_transform(QuaderTransform transform) {
  return api_->scene().set_transform(ref_.entity, transform);
}

QuaderApiVoidResult QuaderObjectHandle::set_visible(bool visible) {
  return api_->scene().set_visible(ref_.entity, visible);
}

QuaderApiVoidResult QuaderObjectHandle::set_locked(bool locked) {
  return api_->scene().set_locked(ref_.entity, locked);
}

QuaderApiVoidResult QuaderObjectHandle::move(QVec3 delta) {
    QuaderApiVoidResult selected = select();
    if (!selected.ok) {
        return selected;
    }
    return api_->modeling().move_selection(delta);
}

QuaderApiVoidResult QuaderObjectHandle::rotate_euler(QVec3 euler_radians,
                                                     QVec3 pivot) {
  QuaderApiVoidResult selected = select();
  if (!selected.ok) {
    return selected;
  }
  return api_->modeling().rotate_selection(euler_radians, pivot);
}

QuaderApiVoidResult QuaderObjectHandle::scale(QVec3 scale, QVec3 pivot) {
    QuaderApiVoidResult selected = select();
    if (!selected.ok) {
        return selected;
    }
    return api_->modeling().scale_selection(scale, pivot);
}

QuaderApiVoidResult QuaderObjectHandle::assign_material(AssetId material) {
  return api_->assets().assign_material_to_object(ref_, material);
}

QuaderApiVoidResult
QuaderObjectHandle::assign_material_slot(std::uint32_t material_slot) {
  QuaderApiVoidResult selected = select();
  if (!selected.ok) {
    return selected;
  }
  return api_->modeling().assign_material_slot_to_selection(material_slot);
}

QuaderApiVoidResult
QuaderObjectHandle::select_vertex(QuaderElementId vertex,
                                  QuaderSelectionEdit edit) {
  return api_->selection().select_vertex(ref_, vertex, edit);
}

QuaderApiVoidResult QuaderObjectHandle::select_edge(QuaderElementId a,
                                                    QuaderElementId b,
                                                    QuaderSelectionEdit edit) {
  return api_->selection().select_edge(ref_, a, b, edit);
}

QuaderApiVoidResult QuaderObjectHandle::select_face(QuaderElementId face,
                                                    QuaderSelectionEdit edit) {
  return api_->selection().select_face(ref_, face, edit);
}

QuaderApiResult<QuaderHoverSummary>
QuaderObjectHandle::hover_vertex(QuaderElementId vertex) {
  return api_->selection().hover_vertex(ref_, vertex);
}

QuaderApiResult<QuaderHoverSummary>
QuaderObjectHandle::hover_edge(QuaderElementId a, QuaderElementId b) {
  return api_->selection().hover_edge(ref_, a, b);
}

QuaderApiResult<QuaderHoverSummary>
QuaderObjectHandle::hover_face(QuaderElementId face) {
  return api_->selection().hover_face(ref_, face);
}

} // namespace quader
