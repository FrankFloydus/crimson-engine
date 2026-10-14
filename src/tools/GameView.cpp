#include "GameView.h"
#include "GameViewInteraction.h"
#include "editor/CollisionWire.h"
#include "editor/CollisionOverlay.h"
#include "scene/SceneDocument.h"
#include "scene/SceneExtraction.h"
#include "crimson-renderer/Renderer.h"
#include "crimson-renderer/ImGuiBgfx.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace crimson {
GameViewFrame GameView::draw(SceneDocument& document,Renderer& renderer,bool& open,bool /*playing*/,bool focus) {
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
                frame.pixelWidth=frame.pixelHeight=0;
                frame.viewport.inputBlocked=true;
            }
            frame.focused=ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        }
    }
    ImGui::End();frame.closed=!open;
    return frame;
}
void GameView::drawRoute(const Camera& camera,const std::optional<Vec3>& clickedGround,std::span<const Vec3> route,
                         const std::optional<Vec3>& characterPosition) {
    if(!imageDrawList_ || !imageWidth_ || !imageHeight_)return;
    const auto matrix=camera.projectionMatrix(static_cast<float>(imageWidth_)/imageHeight_,ClipDepth::ZeroToOne)*camera.viewMatrix();
    const Vec4 rectangle{imageRegion_.x,imageRegion_.y,imageRegion_.width,imageRegion_.height};
    imageDrawList_->PushClipRect({rectangle[0],rectangle[1]},{rectangle[0]+rectangle[2],rectangle[1]+rectangle[3]},true);
    const auto line=[&](const Vec3& start,const Vec3& end,float width) {
        if(const auto segment=projectClippedSegment(start,end,matrix,rectangle,ClipDepth::ZeroToOne))
            imageDrawList_->AddLine({(*segment)[0][0],(*segment)[0][1]},{(*segment)[1][0],(*segment)[1][1]},IM_COL32(255,166,26,255),width);
    };
    auto previous=characterPosition;
    for(const auto& corner:route) {
        if(previous)line(*previous+Vec3{0,.03f,0},corner+Vec3{0,.03f,0},2.f);
        previous=corner;
    }
    // Three world-space great circles form a small sphere whose bottom touches
    // the exact ray hit. This optional diagnostic is deliberately visible
    // through geometry; it never becomes a scene entity or affects collision.
    if(clickedGround && math::finite(*clickedGround))for(int plane=0;plane<3;++plane)for(int segment=0;segment<32;++segment) {
        const auto point=[&](int index) {
            constexpr float radius=.15f;
            const double angle=index*2*std::numbers::pi/32;
            auto world=*clickedGround+Vec3{0,radius,0};world[plane]+=radius*static_cast<float>(std::cos(angle));
            world[(plane+1)%3]+=radius*static_cast<float>(std::sin(angle));
            return world;
        };
        line(point(segment),point(segment+1),1.25f);
    }
    imageDrawList_->PopClipRect();
}
void GameView::drawCollisions(const Camera& camera,const Scene& scene,bool enabled) {
    if(!enabled || !imageDrawList_ || !imageWidth_ || !imageHeight_)return;
    const auto matrix=camera.projectionMatrix(static_cast<float>(imageWidth_)/imageHeight_,ClipDepth::ZeroToOne)*camera.viewMatrix();
    const Vec4 rectangle{imageRegion_.x,imageRegion_.y,imageRegion_.width,imageRegion_.height};
    imageDrawList_->PushClipRect({rectangle[0],rectangle[1]},{rectangle[0]+rectangle[2],rectangle[1]+rectangle[3]},true);
    for(const auto& record:scene.records()) {
        const bool character=record.character && record.character->showCollisionInGame;
        const bool collider=record.collider && record.collider->showCollisionInGame;
        if(!character && !collider)continue;
        const auto world=scene.worldTransform(record.id);if(!world)continue;
        const auto line=[](ImVec2,ImVec2) {};
        if(collider)drawCollisionOverlay(*imageDrawList_,boxCollisionMesh(),[&](const Vec3& point) {
            const auto& size=record.collider->size;return Vec3{point[0]*size[0],point[1]*size[1],point[2]*size[2]};
        },*world,matrix,rectangle,record.collider->collisionColor,line,ClipDepth::ZeroToOne);
        if(character)drawCollisionOverlay(*imageDrawList_,capsuleCollisionMesh(record.character->height,record.character->radius),[&](const Vec3& point) {
            return capsuleCollisionPoint(point,record.character->height,record.character->radius);
        },*world,matrix,rectangle,record.character->collisionColor,line,ClipDepth::ZeroToOne);
    }
    imageDrawList_->PopClipRect();
}
}
