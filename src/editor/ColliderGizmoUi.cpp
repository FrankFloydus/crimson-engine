#include "ColliderGizmoUi.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace crimson {
ColliderGizmoResult drawColliderGizmo(ImDrawList& draw,SceneDocument& document,ColliderGizmoDrag& drag,const Mat4& vp,const Vec4& viewport,
                                     bool visible,bool allowStart,bool focused,bool cancelRequested) {
    ColliderGizmoResult result;const auto& io=ImGui::GetIO();
    visible=visible && document.selection().ids.size()==1 && document.scene().isVisible(document.selection().ids.front());
    if(drag.active() && (!visible || !focused || cancelRequested || io.AppFocusLost || !drag.valid(document))) {
        drag.cancel(document);result.canceled=true;
    }
    if(!visible || document.selection().ids.size()!=1)return result;
    const auto id=document.selection().ids.front();const auto record=document.scene().find(id);const auto world=document.scene().worldTransform(id);
    if(!record || !record->collider || !world)return result;
    auto handles=colliderGizmoHandles(*record->collider);if(handles.empty())return result;
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
    const auto cursor=ImGui::GetCursorScreenPos();ImGui::PushID("Collider shape handles");ImGui::PushID(static_cast<int>(id));
    for(const auto& handle:handles)if(projected[handle.id]) {
        const auto& point=*projected[handle.id];ImGui::PushID(static_cast<int>(handle.id));
        ImGui::SetCursorScreenPos({point[0]-8,point[1]-8});
        ImGui::BeginDisabled(!allowStart && !drag.active());
        ImGui::InvisibleButton("##handle",{16,16});
        if(static_cast<int>(handle.id)==hovered) {
            result.hovered=true;
            if(ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !drag.active() && !result.canceled)if(const auto ray=math::rayFromScreen(io.MousePos.x,io.MousePos.y,vp,viewport,ClipDepth::NegativeOneToOne))
                if(drag.begin(document,id,handle.id,*ray)){ImGui::SetWindowFocus();result.started=true;}
            if(!drag.active() && ImGui::IsItemHovered()) {
                if(record->collider->shape==ColliderShape::Box || (record->collider->shape!=ColliderShape::Sphere && handle.id<2))
                    ImGui::SetTooltip("%s\nAlt: resize from center",handle.name);
                else ImGui::SetTooltip("%s",handle.name);
            }
        }
        ImGui::EndDisabled();ImGui::PopID();
    }
    ImGui::PopID();ImGui::PopID();ImGui::SetCursorScreenPos(cursor);
    // Cursor restoration is still a layout operation in ImGui. Submit its
    // zero-sized item so overlay buttons do not leave an unconsumed cursor seek.
    ImGui::Dummy({0,0});
    if(drag.active()) {
        if(const auto ray=math::rayFromScreen(io.MousePos.x,io.MousePos.y,vp,viewport,ClipDepth::NegativeOneToOne))drag.update(document,*ray,io.KeyAlt);
        if(ImGui::IsMouseReleased(ImGuiMouseButton_Left) || !ImGui::IsMouseDown(ImGuiMouseButton_Left))drag.finish(document);
    }
    // Reproject after the mutation so the collision surface, Inspector, and
    // handles all describe the same dimensions on this frame.
    const auto updated=document.scene().find(id);const auto updatedWorld=document.scene().worldTransform(id);
    if(updated && updated->collider && updatedWorld)for(const auto& handle:colliderGizmoHandles(*updated->collider))
        if(const auto point=math::project(math::transformPoint(*updatedWorld,handle.point),vp,viewport,ClipDepth::NegativeOneToOne);point && (*point)[2]>=0 && (*point)[2]<=1) {
            const bool hot=(drag.active() && drag.handle()==handle.id) || static_cast<int>(handle.id)==hovered;
            const ImVec2 center{(*point)[0],(*point)[1]};draw.AddCircleFilled(center,hot?5.f:4.f,ImGui::GetColorU32(hot?ImGuiCol_ButtonHovered:ImGuiCol_Button),12);
            draw.AddCircle(center,hot?5.f:4.f,ImGui::GetColorU32(ImGuiCol_Text),12,1.f);
        }
    return result;
}
} // namespace crimson
