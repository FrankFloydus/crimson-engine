#include "GameView.h"
#include "GameViewInteraction.h"
#include "editor/CollisionWire.h"
#include "editor/ActorLightRigOverlay.h"
#include "scene/SceneDocument.h"
#include "scene/SceneExtraction.h"
#include "crimson-renderer/Renderer.h"
#include "crimson-renderer/ImGuiBgfx.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>

namespace crimson {
void GameView::updateMetrics(Renderer& renderer,std::uint64_t textureAndProgramBytes) {
    if(!newFrameTiming_)return;
    newFrameTiming_=false;FrameMetricsSample sample;sample.timing=frameTiming_;
    sample.textureAndProgramBytes=textureAndProgramBytes;sample.modelBytes=renderer.modelResidentBytes();sample.shadowBytes=renderer.shadowResidentBytes();
    if(const auto* stats=bgfx::getStats()) {
        if(stats->gpuFrameNum)sample.gpuMilliseconds=gpuFrameMilliseconds(stats->gpuTimeBegin,stats->gpuTimeEnd,stats->gpuTimerFreq);
        sample.deviceMemory=validDeviceMemory(stats->gpuMemoryUsed,stats->gpuMemoryMax);sample.draws=stats->numDraw;
    }
    if(!metrics_.sample(sample))return;
    const auto& value=metrics_.readout();const auto mib=[](std::uint64_t bytes){return static_cast<double>(bytes)/(1024.0*1024.0);};
    char text[640],gpu[48],vram[96];
    if(value.gpuMilliseconds)std::snprintf(gpu,sizeof(gpu),"%.2f ms",*value.gpuMilliseconds);else std::snprintf(gpu,sizeof(gpu),"N/A");
    if(value.deviceMemory)std::snprintf(vram,sizeof(vram),"%.0f / %.0f MiB",mib(value.deviceMemory->used),mib(value.deviceMemory->available));
    else std::snprintf(vram,sizeof(vram),"N/A");
    std::snprintf(text,sizeof(text),"FPS: %.1f    Frame: %.2f ms\nCPU work (main): %.2f ms\nGPU frame: %s\nDevice VRAM: %s\nTextures/programs: %.1f MiB\nModels: %.1f MiB    Shadows: %.1f MiB\nDraws (engine): %llu",
        value.fps,value.timing.frameMilliseconds,value.timing.cpuWorkMilliseconds,gpu,vram,mib(value.textureAndProgramBytes),
        mib(value.modelBytes),mib(value.shadowBytes),static_cast<unsigned long long>(value.draws));
    metricsText_=text;
}
void GameView::drawMetrics(Vec2 origin,Vec2 size) const {
    const char* text=metricsText_.empty()?"FPS: N/A    Frame: N/A\nCPU work (main): N/A\nGPU frame: N/A\nDevice VRAM: N/A":metricsText_.c_str();
    const auto& style=ImGui::GetStyle();const auto extent=ImGui::CalcTextSize(text);const auto padding=style.WindowPadding;
    const ImVec2 minimum{std::max(origin[0]+padding.x,origin[0]+size[0]-extent.x-padding.x*3),origin[1]+padding.y};
    const ImVec2 maximum{minimum.x+extent.x+padding.x*2,minimum.y+extent.y+padding.y*2};
    auto* draw=ImGui::GetWindowDrawList();draw->PushClipRect({origin[0],origin[1]},{origin[0]+size[0],origin[1]+size[1]},true);
    draw->AddRectFilled(minimum,maximum,ImGui::GetColorU32(ImGuiCol_WindowBg),style.WindowRounding);
    if(style.WindowBorderSize>0)draw->AddRect(minimum,maximum,ImGui::GetColorU32(ImGuiCol_Border),style.WindowRounding,0,style.WindowBorderSize);
    draw->AddText({minimum.x+padding.x,minimum.y+padding.y},ImGui::GetColorU32(ImGuiCol_Text),text);draw->PopClipRect();
}
GameViewFrame GameView::draw(SceneDocument& document,Renderer& renderer,bool& open,bool playing,bool focus,std::uint64_t textureAndProgramBytes) {
    GameViewFrame frame;
    imageDrawList_=nullptr;
    if(playing&&!wasPlaying_){metrics_.reset();metricsText_.clear();}
    wasPlaying_=playing;
    if(!open)return frame;
    ImGui::SetNextWindowSize({960,600},ImGuiCond_FirstUseEver);
    if(focus) { ImGui::SetNextWindowFocus();ImGui::SetNextWindowCollapsed(false); }
    const bool visible=ImGui::Begin("Game",&open,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
    // The owner may release the target immediately when the close button is used.
    // Do not leave an Image command referencing that texture in this frame.
    if(!open) { ImGui::End();frame.closed=true;return frame; }
    if(visible) {
        if(gameMetricsToggleRequested(playing))metricsVisible_=!metricsVisible_;
        if(playing)updateMetrics(renderer,textureAndProgramBytes);
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
            if(playing&&metricsVisible_)drawMetrics({origin.x,origin.y},{size.x,size.y});
        }
    }
    ImGui::End();frame.closed=!open;
    return frame;
}
void GameView::drawActorLightRigs(const Camera& camera,const Scene& scene) {
    if(!imageDrawList_ || !imageWidth_ || !imageHeight_)return;
    const auto matrix=camera.projectionMatrix(static_cast<float>(imageWidth_)/imageHeight_,ClipDepth::ZeroToOne)*camera.viewMatrix();
    const Vec4 rectangle{imageRegion_.x,imageRegion_.y,imageRegion_.width,imageRegion_.height};
    drawSceneLightRigOverlays(*imageDrawList_,scene,matrix,rectangle,ClipDepth::ZeroToOne);
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
}
