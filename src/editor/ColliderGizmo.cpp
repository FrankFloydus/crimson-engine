#include "ColliderGizmo.h"
#include <algorithm>
#include <cmath>

namespace crimson {
namespace {
constexpr float Minimum=.001f,Maximum=100000.f;
}
std::vector<ColliderGizmoHandle> colliderGizmoHandles(const SceneCollider& collider) {
    switch(collider.shape) {
    case ColliderShape::Box: {
        std::vector<ColliderGizmoHandle> result;result.reserve(6);
        constexpr const char* names[]{"Size X","Size Y","Size Z"};
        for(unsigned axis=0;axis<3;++axis)for(unsigned side=0;side<2;++side){Vec3 point;point[axis]=collider.size[axis]*(side?-.5f:.5f);result.push_back({axis*2+side,axis,point,names[axis]});}
        return result;
    }
    case ColliderShape::Sphere:return {{0,0,{collider.radius,0,0},"Radius"}};
    case ColliderShape::Cylinder:case ColliderShape::Capsule:return {
        {0,1,{0,collider.height*.5f,0},"Height"},{1,1,{0,-collider.height*.5f,0},"Height"},{2,0,{collider.radius,0,0},"Radius"}};
    default:return {};
    }
}
std::optional<float> colliderHandleCoordinate(const Ray& ray,const Mat4& world,unsigned axis) {
    if(axis>2 || !math::finite(ray.origin) || !math::finite(ray.direction))return {};
    const auto inverse=math::inverse(world);if(!inverse)return {};
    const auto origin=math::transformPoint(*inverse,ray.origin),direction=math::transformDirection(*inverse,ray.direction);
    const auto lengthSquared=math::dot(direction,direction);
    const auto perpendicular=lengthSquared-static_cast<double>(direction[axis])*direction[axis];
    if(!math::finite(origin) || !math::finite(direction) || lengthSquared<=1e-16 || perpendicular<=lengthSquared*1e-8)return {};
    const auto time=std::max(0.,-(math::dot(origin,direction)-static_cast<double>(origin[axis])*direction[axis])/perpendicular);
    const auto coordinate=static_cast<float>(origin[axis]+time*direction[axis]);return math::finite(coordinate)?std::optional<float>{coordinate}:std::nullopt;
}
std::optional<ColliderGizmoEdit> resizedColliderHandle(const SceneCollider& initial,const Mat4& local,unsigned handle,float coordinate,bool symmetric) {
    if(!math::finite(coordinate) || !math::isAffine(local))return {};
    const auto handles=colliderGizmoHandles(initial);if(handle>=handles.size())return {};
    ColliderGizmoEdit result{initial,local};Vec3 offset;
    if(initial.shape==ColliderShape::Box) {
        const auto axis=handle/2;const auto sign=handle%2?-1.f:1.f;
        result.collider.size[axis]=std::clamp(symmetric?2*sign*coordinate:initial.size[axis]*.5f+sign*coordinate,Minimum,Maximum);
        if(!symmetric)offset[axis]=sign*(result.collider.size[axis]-initial.size[axis])*.5f;
    } else if(initial.shape==ColliderShape::Sphere)result.collider.radius=std::clamp(coordinate,Minimum,Maximum);
    else if(handle<2) {
        const auto sign=handle?-1.f:1.f;
        result.collider.height=std::clamp(symmetric?2*sign*coordinate:initial.height*.5f+sign*coordinate,
            initial.shape==ColliderShape::Capsule?2*Minimum:Minimum,Maximum);
        if(!symmetric)offset[1]=sign*(result.collider.height-initial.height)*.5f;
        if(initial.shape==ColliderShape::Capsule)result.collider.radius=std::clamp(initial.radius,Minimum,result.collider.height*.5f);
    } else {
        result.collider.radius=std::clamp(coordinate,Minimum,initial.shape==ColliderShape::Capsule?Maximum*.5f:Maximum);
        if(initial.shape==ColliderShape::Capsule)result.collider.height=std::clamp(std::max(initial.height,2*result.collider.radius),2*Minimum,Maximum);
    }
    result.local=local*math::translation(offset);return result;
}
bool ColliderGizmoDrag::begin(SceneDocument& document,EntityId entity,unsigned handle,const Ray& ray) {
    if(active() || document.transactionActive() || document.selection().ids!=std::vector<EntityId>{entity})return false;
    const auto record=document.scene().find(entity);const auto world=document.scene().worldTransform(entity);
    if(!record || !record->collider || !world)return false;
    const auto handles=colliderGizmoHandles(*record->collider);if(handle>=handles.size())return false;
    const auto coordinate=colliderHandleCoordinate(ray,*world,handles[handle].axis);if(!coordinate)return false;
    const auto parent=record->parent?document.scene().worldTransform(record->parent):std::optional<Mat4>{Mat4{}};
    if(!parent || !math::inverse(*parent) || !document.beginTransform("Resize collider",{entity}))return false;
    owner_=&document;generation_=document.documentGeneration();entity_=entity;handle_=handle;axis_=handles[handle].axis;
    grabOffset_=*coordinate-handles[handle].point[axis_];initial_=*record;world_=*world;parentWorld_=*parent;selection_=document.selection().ids;return true;
}
bool ColliderGizmoDrag::valid(const SceneDocument& document) const {
    if(owner_!=&document || generation_!=document.documentGeneration() || !document.transactionActive() || selection_!=document.selection().ids)return false;
    const auto record=document.scene().find(entity_);if(!record || !record->collider || record->parent!=initial_.parent || record->collider->shape!=initial_.collider->shape)return false;
    const auto parent=record->parent?document.scene().worldTransform(record->parent):std::optional<Mat4>{Mat4{}};return parent && *parent==parentWorld_;
}
bool ColliderGizmoDrag::update(SceneDocument& document,const Ray& ray,bool symmetric) {
    if(!valid(document)){cancel(document);return false;}
    const auto coordinate=colliderHandleCoordinate(ray,world_,axis_);if(!coordinate)return true;
    const auto edit=resizedColliderHandle(*initial_.collider,initial_.local,handle_,*coordinate-grabOffset_,symmetric);if(!edit)return true;
    if(!document.update([&](auto& records,auto&){for(auto& record:records)if(record.id==entity_){record.collider=edit->collider;record.local=edit->local;break;}})) {cancel(document);return false;}
    return true;
}
bool ColliderGizmoDrag::finish(SceneDocument& document) {
    if(!valid(document)){cancel(document);return false;}
    const auto result=document.commit();reset();return result;
}
void ColliderGizmoDrag::reset(){owner_=nullptr;entity_=NoEntity;generation_=0;selection_.clear();initial_={};}
void ColliderGizmoDrag::cancel(SceneDocument& document) {
    if(owner_==&document && generation_==document.documentGeneration() && document.transactionActive())document.cancel();reset();
}
} // namespace crimson
