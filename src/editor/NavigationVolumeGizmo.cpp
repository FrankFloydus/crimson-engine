#include "NavigationVolumeGizmo.h"
#include <imgui.h>
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
NavigationVolumeGizmoResult drawNavigationVolumeGizmo(ImDrawList& draw,SceneDocument& document,NavigationVolumeGizmoDrag& drag,
    const Mat4& vp,const Vec4& viewport,bool allowStart,bool focused,bool cancelRequested) {
    NavigationVolumeGizmoResult result;const auto& io=ImGui::GetIO();
    const bool visible=document.selection().ids.size()==1 && document.scene().isVisible(document.selection().ids.front());
    if(drag.active() && (!visible || !focused || cancelRequested || io.AppFocusLost || !drag.valid(document))) {drag.cancel(document);result.canceled=true;}
    if(!visible)return result;
    const auto id=document.selection().ids.front();const auto record=document.scene().find(id);const auto world=document.scene().worldTransform(id);
    if(!record || !record->navigationVolume || !world)return result;
    const auto handles=colliderGizmoHandles(boundsCollider(*record->navigationVolume));
    std::vector<std::optional<Vec3>> projected;projected.reserve(handles.size());
    int hovered=-1;float distance=8.f,depth=std::numeric_limits<float>::max();
    const bool pointer=io.MousePos.x>=viewport[0] && io.MousePos.y>=viewport[1] && io.MousePos.x<viewport[0]+viewport[2] && io.MousePos.y<viewport[1]+viewport[3];
    for(const auto& handle:handles) {
        auto point=math::project(math::transformPoint(*world,handle.point),vp,viewport,ClipDepth::NegativeOneToOne);
        if(point && ((*point)[2]<0 || (*point)[2]>1))point.reset();projected.push_back(point);
        if(!point || !pointer || !allowStart)continue;
        const auto candidate=std::hypot((*point)[0]-io.MousePos.x,(*point)[1]-io.MousePos.y);
        if(candidate<distance || (candidate==distance && (*point)[2]<depth)){distance=candidate;depth=(*point)[2];hovered=static_cast<int>(handle.id);}
    }
    const auto cursor=ImGui::GetCursorScreenPos();ImGui::PushID("Navigation volume handles");ImGui::PushID(static_cast<int>(id));
    for(const auto& handle:handles)if(projected[handle.id]) {
        const auto& point=*projected[handle.id];ImGui::PushID(static_cast<int>(handle.id));ImGui::SetCursorScreenPos({point[0]-8,point[1]-8});
        ImGui::BeginDisabled(!allowStart && !drag.active());ImGui::InvisibleButton("##handle",{16,16});
        if(static_cast<int>(handle.id)==hovered) {
            result.hovered=true;
            if(ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !drag.active() && !result.canceled)
                if(const auto ray=math::rayFromScreen(io.MousePos.x,io.MousePos.y,vp,viewport,ClipDepth::NegativeOneToOne))
                    if(drag.begin(document,id,handle.id,*ray)){ImGui::SetWindowFocus();result.started=true;}
            if(!drag.active() && ImGui::IsItemHovered())ImGui::SetTooltip("%s\nAlt: resize from center",handle.name);
        }
        ImGui::EndDisabled();ImGui::PopID();
    }
    ImGui::PopID();ImGui::PopID();ImGui::SetCursorScreenPos(cursor);ImGui::Dummy({0,0});
    if(drag.active()) {
        if(const auto ray=math::rayFromScreen(io.MousePos.x,io.MousePos.y,vp,viewport,ClipDepth::NegativeOneToOne))drag.update(document,*ray,io.KeyAlt);
        if(ImGui::IsMouseReleased(ImGuiMouseButton_Left) || !ImGui::IsMouseDown(ImGuiMouseButton_Left))drag.finish(document);
    }
    const auto updated=document.scene().find(id);const auto updatedWorld=document.scene().worldTransform(id);
    if(updated && updated->navigationVolume && updatedWorld)for(const auto& handle:colliderGizmoHandles(boundsCollider(*updated->navigationVolume)))
        if(const auto point=math::project(math::transformPoint(*updatedWorld,handle.point),vp,viewport,ClipDepth::NegativeOneToOne);point && (*point)[2]>=0 && (*point)[2]<=1) {
            const bool hot=(drag.active() && drag.handle()==handle.id) || static_cast<int>(handle.id)==hovered;
            const ImVec2 center{(*point)[0],(*point)[1]};draw.AddCircleFilled(center,hot?5.f:4.f,ImGui::GetColorU32(hot?ImGuiCol_ButtonHovered:ImGuiCol_Button),12);
            draw.AddCircle(center,hot?5.f:4.f,ImGui::GetColorU32(ImGuiCol_Text),12,1.f);
        }
    return result;
}
} // namespace crimson
