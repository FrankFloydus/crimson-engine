#include "GameView.h"
#include "GameViewInteraction.h"
#include "scene/SceneDocument.h"
#include "scene/SceneExtraction.h"
#include "crimson-renderer/Renderer.h"
#include "crimson-renderer/ImGuiBgfx.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace crimson {
GameViewFrame GameView::draw(SceneDocument& document,Renderer& renderer,bool& open,bool playing,bool focus) {
    GameViewFrame frame;
    imageDrawList_=nullptr;
    if(!open)return frame;
    ImGui::SetNextWindowSize({960,600},ImGuiCond_FirstUseEver);
    if(focus) { ImGui::SetNextWindowFocus();ImGui::SetNextWindowCollapsed(false); }
    const bool visible=ImGui::Begin("Game",&open,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
    // The owner may release the target immediately when the close button is used.
    // Do not leave an Image command referencing that texture in this frame.
    if(!open) { ImGui::End();frame.closed=true;return frame; }
    if(visible) {
        const auto active=document.scene().find(document.scene().properties().activeCamera);
        ImGui::SetNextItemWidth(220);
        if(ImGui::BeginCombo("Camera",active?active->name.c_str():"No active camera")) {
            if(ImGui::Selectable("None",!active))document.setActiveCamera(NoEntity);
            for(const auto& record:document.scene().records())if(record.camera) {
                ImGui::PushID(static_cast<int>(record.id>>32));ImGui::PushID(static_cast<int>(record.id));
                if(ImGui::Selectable(record.name.c_str(),active && record.id==active->id))document.setActiveCamera(record.id);
                ImGui::PopID();ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();ImGui::TextDisabled(playing?"Click or hold LMB to move | Esc to release":"Camera preview");
        ImGui::SameLine();ImGui::Checkbox("Click marker",&clickMarker_);
        if(ImGui::IsItemHovered())ImGui::SetTooltip("Debug: show a wire sphere at the latest clicked or held ground position, even when unreachable.");
        const auto size=ImGui::GetContentRegionAvail();
        if(size.x>0 && size.y>0) {
            const auto* host=ImGui::GetWindowViewport();const auto origin=ImGui::GetCursorScreenPos();
            frame.pixelWidth=static_cast<uint32_t>(std::max(1.f,std::round(size.x*host->FramebufferScale.x)));
            frame.pixelHeight=static_cast<uint32_t>(std::max(1.f,std::round(size.y*host->FramebufferScale.y)));
            frame.viewport={origin.x,origin.y,size.x,size.y,false,false,
                static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(host->PlatformHandle))};
            const bool cameraAvailable=resolveGameCamera(document.scene()).has_value();
            if(cameraAvailable) {
                renderer.resizeGameViewport(frame.pixelWidth,frame.pixelHeight);
                const auto interaction=drawGameViewImage(ImTextureRef(ImGuiBgfx::textureId(renderer.gameViewportTexture())),size);
                imageDrawList_=ImGui::GetWindowDrawList();imageRegion_=frame.viewport;
                imageWidth_=frame.pixelWidth;imageHeight_=frame.pixelHeight;
                frame.viewport.hovered=interaction.hovered;frame.clicked=interaction.clicked;
                frame.leftHeld=interaction.held;
                frame.viewport.inputBlocked=interaction.blocked;
                if(interaction.hovered && !interaction.blocked) {
                    const auto mouse=ImGui::GetIO().MousePos;frame.pointerPosition=Vec2{mouse.x,mouse.y};
                }
            } else {
                ImGui::TextUnformatted("No active camera");
                ImGui::TextWrapped("Add a Camera in the Outliner and choose Make Active to preview your game.");
                frame.pixelWidth=frame.pixelHeight=0;
                frame.viewport.inputBlocked=true;
            }
            frame.focused=ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        }
    }
    ImGui::End();frame.closed=!open;
    return frame;
}
void GameView::drawClickMarker(const Camera& camera,const Vec3& groundPoint) {
    if(!clickMarker_ || !imageDrawList_ || !imageWidth_ || !imageHeight_ || !math::finite(groundPoint))return;
    const auto matrix=camera.projectionMatrix(static_cast<float>(imageWidth_)/imageHeight_,ClipDepth::ZeroToOne)*camera.viewMatrix();
    const Vec4 rectangle{imageRegion_.x,imageRegion_.y,imageRegion_.width,imageRegion_.height};
    constexpr float radius=.15f;
    const Vec3 center=groundPoint+Vec3{0,radius,0};
    imageDrawList_->PushClipRect({rectangle[0],rectangle[1]},{rectangle[0]+rectangle[2],rectangle[1]+rectangle[3]},true);
    // Three world-space great circles form a small sphere whose bottom touches
    // the exact ray hit. This optional diagnostic is deliberately visible
    // through geometry; it never becomes a scene entity or affects collision.
    for(int plane=0;plane<3;++plane)for(int segment=0;segment<32;++segment) {
        const auto point=[&](int index) {
            const double angle=index*2*std::numbers::pi/32;
            auto world=center;world[plane]+=radius*static_cast<float>(std::cos(angle));
            world[(plane+1)%3]+=radius*static_cast<float>(std::sin(angle));
            return math::project(world,matrix,rectangle,ClipDepth::ZeroToOne);
        };
        const auto a=point(segment),b=point(segment+1);
        if(a && b && (*a)[2]>=0 && (*a)[2]<=1 && (*b)[2]>=0 && (*b)[2]<=1)
            imageDrawList_->AddLine({(*a)[0],(*a)[1]},{(*b)[0],(*b)[1]},IM_COL32(255,215,75,255),1.25f);
    }
    imageDrawList_->PopClipRect();
}
}
