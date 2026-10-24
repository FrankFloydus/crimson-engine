#include "RagdollBodyGizmo.h"
#include <imgui_internal.h>
#include <ImGuizmo.h>
#include <cmath>

namespace crimson {
namespace {
struct GizmoScope {
    ImGuizmo::Style style{ImGuizmo::GetStyle()};
    bool& idScoped;
    bool configured{};
    explicit GizmoScope(const void* owner,bool& scoped):idScoped(scoped){ImGuizmo::PushID(owner);idScoped=true;}
    ~GizmoScope(){
        ImGuizmo::GetStyle()=style;ImGuizmo::PopID();idScoped=false;
        if(configured){const auto* viewport=ImGui::GetMainViewport();ImGuizmo::SetRect(viewport->Pos.x,viewport->Pos.y,viewport->Size.x,viewport->Size.y);ImGuizmo::SetOrthographic(false);ImGuizmo::SetDrawlist();}
    }
};
// The image remains ImGui's real mouse owner. ImGuizmo's CanActivate rejects
// every active/hovered item, so mask only this image while it queries input.
// Restore all three IDs exactly before any other ImGui widget is submitted.
struct PreviewItemScope {
    ImGuiContext& context{*ImGui::GetCurrentContext()};
    ImGuiID active{context.ActiveId},hovered{context.HoveredId},previous{context.HoveredIdPreviousFrame};
    explicit PreviewItemScope(ImGuiID image){if(context.ActiveId==image)context.ActiveId=0;if(context.HoveredId==image)context.HoveredId=0;if(context.HoveredIdPreviousFrame==image)context.HoveredIdPreviousFrame=0;}
    ~PreviewItemScope(){context.ActiveId=active;context.HoveredId=hovered;context.HoveredIdPreviousFrame=previous;}
};
}
void RagdollBodyGizmo::stop(){
    if(active_){if(!idScoped_)ImGuizmo::PushID(this);if(ImGuizmo::IsUsing()){ImGuizmo::Enable(false);ImGuizmo::Enable(true);}if(!idScoped_)ImGuizmo::PopID();}
    active_=false;waitForRelease_=ImGui::IsMouseDown(ImGuiMouseButton_Left);asset_.clear();
}
bool RagdollBodyGizmo::finish(RagdollDocument& document){
    if(!active_)return true;
    if(document.transactionActive()&&!document.commit()){diagnostic_=document.status();return false;}
    stop();return true;
}
void RagdollBodyGizmo::cancel(RagdollDocument& document){if(active_&&document.transactionActive())document.cancel();stop();}
bool RagdollBodyGizmo::draw(RagdollDocument& document,std::size_t body,const Mat4& bone,const Camera& camera,
    const ViewportRegion& region,RagdollAuthoringOperation operation,bool lockProportions,bool inputBlocked) {
    const auto image=ImGui::GetItemID();const auto& io=ImGui::GetIO();
    if(!io.MouseDown[ImGuiMouseButton_Left])waitForRelease_=false;
    const bool focused=ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    const bool foreignWidget=ImGui::IsAnyItemActive()&&ImGui::GetActiveID()!=image;
    const bool blocked=inputBlocked||io.AppFocusLost||ImGui::IsKeyPressed(ImGuiKey_Escape,false)||
        ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopup)||ImGui::GetDragDropPayload()!=nullptr||foreignWidget;
    if(active_&&(!document.transactionActive()||document.assetId()!=asset_||body!=body_||bone!=bone_||!focused||blocked))cancel(document);
    if(!document.isOpen()||body>=document.definition().bodies.size()||region.width<=0||region.height<=0)return false;
    GizmoScope scope(this,idScoped_);
    if(ImGuizmo::IsUsingAny()&&!ImGuizmo::IsUsing())return false;
    if(!active_) {
        const auto matrix=ragdollBodyGizmoMatrix(document.definition().bodies[body],bone,operation,&diagnostic_);
        if(!matrix)return false;proxy_=*matrix;
    }
    const auto view=camera.viewMatrix(),projection=camera.projectionMatrix(region.width/region.height,ClipDepth::NegativeOneToOne);
    const auto center=math::project({proxy_[12],proxy_[13],proxy_[14]},projection*view,{region.x,region.y,region.width,region.height},ClipDepth::NegativeOneToOne);
    const auto clip=math::transformHomogeneous(projection*view,{proxy_[12],proxy_[13],proxy_[14],1});
    if(!active_&&(!center||(*center)[2]<0||(*center)[2]>1||clip[2]<.001f))return false;
    ImGuizmo::SetDrawlist();ImGuizmo::SetRect(region.x,region.y,region.width,region.height);ImGuizmo::SetOrthographic(false);
    scope.configured=true;
    auto& style=ImGuizmo::GetStyle();style.Colors[ImGuizmo::TEXT].w=style.Colors[ImGuizmo::TEXT_SHADOW].w=0;
    const bool allowed=!blocked&&!waitForRelease_&&(!document.transactionActive()||active_)&&!io.MouseDown[ImGuiMouseButton_Middle];
    if(!allowed)return false;
    ImGuizmo::Enable(true);
    const auto mode=active_?operation_:operation;
    const auto handles=mode==RagdollAuthoringOperation::Translate?ImGuizmo::TRANSLATE:
        mode==RagdollAuthoringOperation::Rotate?ImGuizmo::ROTATE:ImGuizmo::SCALE;
    const auto before=proxy_;
    {PreviewItemScope imageScope(image);ImGuizmo::Manipulate(view.data(),projection.data(),handles,ImGuizmo::LOCAL,proxy_.data());}
    // ImGuizmo's modified result compares a delta retained across gestures.
    // An identical first delta can still change the matrix after a new grab.
    bool changed=false;
    for(std::size_t i=0;i<proxy_.size();++i)if(!std::isfinite(proxy_[i])||std::abs(proxy_[i]-before[i])>1.e-6f){changed=true;break;}
    const bool usingNow=ImGuizmo::IsUsing(),over=ImGuizmo::IsOver();
    if(usingNow&&!active_) {
        baseline_=document.definition().bodies[body];bone_=bone;operation_=operation;lock_=lockProportions;body_=body;asset_=document.assetId();
        const char* label=operation==RagdollAuthoringOperation::Translate?"Move ragdoll collider":operation==RagdollAuthoringOperation::Rotate?"Rotate ragdoll collider":"Resize ragdoll collider";
        if(!document.begin(label)){proxy_=before;ImGuizmo::Enable(false);ImGuizmo::Enable(true);return over;}
        active_=true;ImGui::SetWindowFocus();
    }
    if(active_&&changed) {
        const auto value=applyRagdollBodyGizmoTransform(baseline_,bone_,proxy_,operation_,lock_,&diagnostic_);
        if(!value||!document.update([&](auto& asset){asset.bodies[body_]=*value;})){cancel(document);return true;}
    }
    if(active_&&(!usingNow||!io.MouseDown[ImGuiMouseButton_Left]))finish(document);
    return over||usingNow||active_;
}
}
