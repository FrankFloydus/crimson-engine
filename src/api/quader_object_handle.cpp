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
    , ref_(ref)
    , scene_session_id_(ref.scene_session_id) {}

bool QuaderObjectHandle::bound_to_active_scene() const {
  return api_ != nullptr && scene_session_id_.valid() &&
         api_->scene_binding_identity() == scene_session_id_.packed();
}

QuaderApiVoidResult QuaderObjectHandle::unavailable_status() const {
  QuaderApiVoidResult result{
      .ok = false,
      .changed = false,
      .message =
          "Object handle belongs to a closed, stale, or inactive scene.",
      .mutation_kind = QuaderMutationKind::None,
  };
  if (api_ != nullptr) {
    result.revisions = api_->revisions();
  }
  return result;
}

QuaderModelingObjectRef QuaderObjectHandle::ref() const {
    return ref_;
}

bool QuaderObjectHandle::valid() const {
  if (!bound_to_active_scene() || ref_.entity == kInvalidEntityId ||
      ref_.modeling_object_id == kInvalidQuaderModelingObjectId) {
    return false;
  }
  const std::optional<QuaderModelingObjectRef> queried =
      api_->query().modeling_object_for_entity(ref_.entity);
  return queried.has_value() &&
         queried->modeling_object_id == ref_.modeling_object_id;
}

std::optional<QuaderEntitySnapshot> QuaderObjectHandle::entity() const {
  if (!bound_to_active_scene() || ref_.entity == kInvalidEntityId) {
    return std::nullopt;
  }
    return api_->query().entity(ref_.entity);
}

std::optional<QuaderMeshAsset> QuaderObjectHandle::compiled_mesh() const {
  if (!bound_to_active_scene()) {
    return std::nullopt;
  }
  return api_->query().compiled_object_mesh(ref_);
}

QuaderApiVoidResult QuaderObjectHandle::select() {
  if (!bound_to_active_scene()) {
    return unavailable_status();
  }
  return api_->selection().select_object(ref_);
}

QuaderApiVoidResult QuaderObjectHandle::rename(std::string name) {
    if (!bound_to_active_scene()) {
      return unavailable_status();
    }
    return api_->scene().rename(ref_.entity, std::move(name));
}

QuaderApiVoidResult QuaderObjectHandle::destroy() {
    if (!bound_to_active_scene()) {
      return unavailable_status();
    }
    return api_->scene().destroy(ref_.entity);
}

QuaderApiResult<QuaderEntityRef> QuaderObjectHandle::duplicate() {
    if (!bound_to_active_scene()) {
      return quader_api_result_from_status<QuaderEntityRef>(
          unavailable_status());
    }
    return api_->scene().duplicate(ref_.entity);
}

QuaderApiVoidResult
QuaderObjectHandle::set_transform(QuaderTransform transform) {
  if (!bound_to_active_scene()) {
    return unavailable_status();
  }
  return api_->scene().set_transform(ref_.entity, transform);
}

QuaderApiVoidResult QuaderObjectHandle::set_visible(bool visible) {
  if (!bound_to_active_scene()) {
    return unavailable_status();
  }
  return api_->scene().set_visible(ref_.entity, visible);
}

QuaderApiVoidResult QuaderObjectHandle::set_selectable(bool selectable) {
  if (!bound_to_active_scene()) {
    return unavailable_status();
  }
  return api_->scene().set_selectable(ref_.entity, selectable);
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
  if (!bound_to_active_scene()) {
    return unavailable_status();
  }
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
  if (!bound_to_active_scene()) {
    return unavailable_status();
  }
  return api_->selection().select_vertex(ref_, vertex, edit);
}

QuaderApiVoidResult QuaderObjectHandle::select_edge(QuaderElementId a,
                                                    QuaderElementId b,
                                                    QuaderSelectionEdit edit) {
  if (!bound_to_active_scene()) {
    return unavailable_status();
  }
  return api_->selection().select_edge(ref_, a, b, edit);
}

QuaderApiVoidResult QuaderObjectHandle::select_face(QuaderElementId face,
                                                    QuaderSelectionEdit edit) {
  if (!bound_to_active_scene()) {
    return unavailable_status();
  }
  return api_->selection().select_face(ref_, face, edit);
}

QuaderApiResult<QuaderHoverSummary>
QuaderObjectHandle::hover_vertex(QuaderElementId vertex) {
  if (!bound_to_active_scene()) {
    return quader_api_result_from_status<QuaderHoverSummary>(
        unavailable_status());
  }
  return api_->selection().hover_vertex(ref_, vertex);
}

QuaderApiResult<QuaderHoverSummary>
QuaderObjectHandle::hover_edge(QuaderElementId a, QuaderElementId b) {
  if (!bound_to_active_scene()) {
    return quader_api_result_from_status<QuaderHoverSummary>(
        unavailable_status());
  }
  return api_->selection().hover_edge(ref_, a, b);
}

QuaderApiResult<QuaderHoverSummary>
QuaderObjectHandle::hover_face(QuaderElementId face) {
  if (!bound_to_active_scene()) {
    return quader_api_result_from_status<QuaderHoverSummary>(
        unavailable_status());
  }
  return api_->selection().hover_face(ref_, face);
}

} // namespace quader
