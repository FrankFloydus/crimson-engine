#include "editor/ActorLightRigOverlay.h"
#include "scene/Scene.h"

#include <imgui.h>
#include <iostream>
#include <stdexcept>

namespace {
using namespace crimson;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct DrawCounts {
    int vertices{},indices{};
    bool empty() const{return vertices==0&&indices==0;}
};
// Private CPU draw lists only: this test creates no renderer, native window or
// desktop input and cannot change the user's editor session.
class OverlayUi {
public:
    OverlayUi() {
        context_=ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;
        io.DisplaySize={900,600};io.DeltaTime=1.f/60;
        unsigned char* pixels{};int width{},height{};
        io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);io.Fonts->SetTexID(static_cast<ImTextureID>(1));
    }
    ~OverlayUi(){ImGui::DestroyContext(context_);}
    DrawCounts draw(const ActorLightRigSettings& rig,const DarknessRevealSettings& reveal,ClipDepth depth) {
        ImGui::NewFrame();auto* list=ImGui::GetForegroundDrawList();
        const auto vertices=list->VtxBuffer.Size,indices=list->IdxBuffer.Size;
        Mat4 projection;projection.at(0,0)=projection.at(1,1)=projection.at(2,2)=.05f;
        projection.at(2,3)=.5f;
        drawActorLightRigOverlay(*list,rig,reveal,{},2,projection,{20,20,860,560},depth);
        const DrawCounts result{list->VtxBuffer.Size-vertices,list->IdxBuffer.Size-indices};
        ImGui::Render();return result;
    }
    DrawCounts drawScene(const Scene& scene) {
        ImGui::NewFrame();auto* list=ImGui::GetForegroundDrawList();
        const auto vertices=list->VtxBuffer.Size,indices=list->IdxBuffer.Size;
        Mat4 projection;projection.at(0,0)=projection.at(1,1)=projection.at(2,2)=.05f;projection.at(2,3)=.5f;
        drawSceneLightRigOverlays(*list,scene,projection,{20,20,860,560},ClipDepth::ZeroToOne);
        const DrawCounts result{list->VtxBuffer.Size-vertices,list->IdxBuffer.Size-indices};ImGui::Render();return result;
    }
private:
    ImGuiContext* context_{};
};
void showRigControlsEveryContribution() {
    OverlayUi ui;ActorLightRigSettings rig;DarknessRevealSettings reveal;
    rig.enabled=true;reveal.enabled=true;
    for(const auto depth:{ClipDepth::NegativeOneToOne,ClipDepth::ZeroToOne}) {
        rig.showGizmos=false;
        require(ui.draw(rig,reveal,depth).empty(),"Show Rig off still drew character or reveal overlays.");
        rig.showGizmos=true;
        require(!ui.draw(rig,reveal,depth).empty(),"Show Rig on did not draw the active character rig.");
        rig.enabled=false;
        require(!ui.draw(rig,reveal,depth).empty(),"Show Rig failed to display independent Darkness Reveal.");
        rig.showGizmos=false;
        require(ui.draw(rig,reveal,depth).empty(),"Show Rig off still drew independent Darkness Reveal.");
        rig.showGizmos=true;reveal.enabled=false;
        require(ui.draw(rig,reveal,depth).empty(),"Disabled rig and reveal still drew overlays.");
        rig.enabled=true;reveal.enabled=true;
    }
}
void independentActorOverlays() {
    OverlayUi ui;Scene scene;EntityRecord first;first.id=1;first.name="Actor A";first.actor.emplace();first.lightRig.emplace();
    first.lightRig->enabled=true;first.lightRig->showGizmos=true;
    auto second=first;second.id=2;second.name="Actor B";second.local=math::translation({3,0,0});
    scene.apply({first},{});const auto one=ui.drawScene(scene);
    scene.apply({first,second},{});const auto two=ui.drawScene(scene);
    require(!one.empty()&&two.vertices>one.vertices&&two.indices>one.indices,"Scene overlay still selected one player rig.");
    second.visible=false;scene.apply({first,second},{});const auto hidden=ui.drawScene(scene);
    require(hidden.vertices==one.vertices&&hidden.indices==one.indices,"Hidden actor still drew rig overlays.");
    second.visible=true;first.lightRig->showGizmos=second.lightRig->showGizmos=false;scene.apply({first,second},{});
    require(ui.drawScene(scene).empty(),"Independent Show Rig toggles emitted geometry while off.");
}
}
int main() {
    try {showRigControlsEveryContribution();independentActorOverlays();std::cout<<"Show Rig governs independent actor and reveal overlays in both viewport depth conventions.\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
