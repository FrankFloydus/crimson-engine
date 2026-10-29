#include "NavigationVolumeGizmo.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace crimson {
namespace {
SceneCollider boundsCollider(const SceneNavigationVolume& volume) { SceneCollider box;box.size=volume.size;return box; }
}
bool NavigationVolumeGizmoDrag::begin(SceneDocument& document,EntityId entity,unsigned handle,const Ray& ray) {
    if(active() || document.transactionActive() || document.selection().ids!=std::vector<EntityId>{entity})return false;
    const auto record=document.scene().find(entity);const auto world=document.scene().worldTransform(entity);
    if(!record || !record->navigationVolume || !world)return false;
    const auto handles=colliderGizmoHandles(boundsCollider(*record->navigationVolume));if(handle>=handles.size())return false;
    const auto coordinate=colliderHandleCoordinate(ray,*world,handles[handle].axis);if(!coordinate)return false;
    const auto parent=record->parent?document.scene().worldTransform(record->parent):std::optional<Mat4>{Mat4{}};
    if(!parent || !math::inverse(*parent) || !document.beginTransform("Resize navigation volume",{entity}))return false;
    owner_=&document;generation_=document.documentGeneration();transaction_=document.transactionId();entity_=entity;handle_=handle;axis_=handles[handle].axis;
    grabOffset_=*coordinate-handles[handle].point[axis_];initial_=*record;world_=*world;parentWorld_=*parent;selection_=document.selection().ids;return true;
}
bool NavigationVolumeGizmoDrag::valid(const SceneDocument& document) const {
    if(owner_!=&document || generation_!=document.documentGeneration() || transaction_!=document.transactionId() || !document.transactionActive() || selection_!=document.selection().ids)return false;
    const auto record=document.scene().find(entity_);if(!record || !record->navigationVolume || record->parent!=initial_.parent)return false;
    const auto parent=record->parent?document.scene().worldTransform(record->parent):std::optional<Mat4>{Mat4{}};return parent && *parent==parentWorld_;
}
bool NavigationVolumeGizmoDrag::update(SceneDocument& document,const Ray& ray,bool symmetric) {
    if(!valid(document)){cancel(document);return false;}
    const auto coordinate=colliderHandleCoordinate(ray,world_,axis_);if(!coordinate)return true;
    const auto edit=resizedColliderHandle(boundsCollider(*initial_.navigationVolume),initial_.local,handle_,*coordinate-grabOffset_,symmetric);if(!edit)return true;
    if(!document.update([&](auto& records,auto&){for(auto& record:records)if(record.id==entity_){record.navigationVolume->size=edit->collider.size;record.local=edit->local;break;}})) {cancel(document);return false;}
    return true;
}
bool NavigationVolumeGizmoDrag::finish(SceneDocument& document) {
    if(!valid(document)){cancel(document);return false;}const auto result=document.commit();reset();return result;
}
void NavigationVolumeGizmoDrag::reset(){owner_=nullptr;entity_=NoEntity;generation_=transaction_=0;selection_.clear();initial_={};}
void NavigationVolumeGizmoDrag::cancel(SceneDocument& document) {
    if(owner_==&document && generation_==document.documentGeneration() && transaction_==document.transactionId() && document.transactionActive())document.cancel();reset();
}
} // namespace crimson
