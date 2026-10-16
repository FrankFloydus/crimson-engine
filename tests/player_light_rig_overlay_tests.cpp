#include "editor/PlayerLightRigOverlay.h"

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
    DrawCounts draw(const PlayerLightRigSettings& rig,const CharacterRevealSettings& reveal,ClipDepth depth) {
        ImGui::NewFrame();auto* list=ImGui::GetForegroundDrawList();
        const auto vertices=list->VtxBuffer.Size,indices=list->IdxBuffer.Size;
        Mat4 projection;projection.at(0,0)=projection.at(1,1)=projection.at(2,2)=.05f;
        projection.at(2,3)=.5f;
        drawPlayerLightRigOverlay(*list,rig,reveal,{},2,projection,{20,20,860,560},depth);
        const DrawCounts result{list->VtxBuffer.Size-vertices,list->IdxBuffer.Size-indices};
        ImGui::Render();return result;
    }
private:
    ImGuiContext* context_{};
};
void showRigControlsEveryContribution() {
    OverlayUi ui;PlayerLightRigSettings rig;CharacterRevealSettings reveal;
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
}
int main() {
    try {showRigControlsEveryContribution();std::cout<<"Show Rig governs character and reveal overlays in both viewport depth conventions.\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
