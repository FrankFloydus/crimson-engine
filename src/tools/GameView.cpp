#include "GameView.h"
#include "GameViewInteraction.h"
#include "RenderDebugControls.h"
#include "PlayControls.h"
#include "ProfilingStatus.h"
#include "editor/CollisionWire.h"
#include "editor/TraceOverlay.h"
#include "scene/SceneDocument.h"
#include "scene/SceneExtraction.h"
#include "crimson-renderer/Renderer.h"
#include "crimson-renderer/ImGuiBgfx.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace crimson {
namespace {
void drawCrosshair(ImDrawList& draw,ImVec2 origin,ImVec2 size) {
    const ImVec2 center{origin.x+size.x*.5f,origin.y+size.y*.5f};
    constexpr float gap=3.f,extent=8.f;
    draw.PushClipRect(origin,{origin.x+size.x,origin.y+size.y},true);
    const auto arm=[&](ImVec2 start,ImVec2 end) {
        draw.AddLine(start,end,IM_COL32(0,0,0,220),3.f);
        draw.AddLine(start,end,IM_COL32(255,255,255,255),1.f);
    };
    arm({center.x-extent,center.y},{center.x-gap,center.y});
    arm({center.x+gap,center.y},{center.x+extent,center.y});
    arm({center.x,center.y-extent},{center.x,center.y-gap});
    arm({center.x,center.y+gap},{center.x,center.y+extent});
    draw.PopClipRect();
}
}
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
GameViewFrame GameView::draw(SceneDocument& document,Renderer& renderer,bool& open,bool playing,bool focus,std::uint64_t textureAndProgramBytes,
                             bool attributeDebug,bool paused,bool playControlsBlocked) {
    GameViewFrame frame;
    imageDrawList_=nullptr;
    imageFont_=nullptr;
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
        frame.attributeDebugCycle=gameAttributeDebugCycleRequested(playing&&attributeDebug);
        if(playing)updateMetrics(renderer,textureAndProgramBytes);
        const PlayControlsContext playControls{playing,paused,playControlsBlocked};
        const float toolbarWidth=gamePlayToolbarWidth(playControls);
        const float toolbarAvailable=ImGui::GetContentRegionAvail().x;
        frame.playAction=drawGamePlayToolbar(playControls);
        const float debugWidth=ImGui::CalcTextSize("Indirect Lighting").x+ImGui::GetStyle().FramePadding.x*2+
            ImGui::GetFrameHeight()*2+ImGui::GetStyle().ItemInnerSpacing.x;
        if(playing&&toolbarAvailable>=toolbarWidth+debugWidth+ImGui::GetStyle().ItemSpacing.x)ImGui::SameLine();
        drawRenderDebugControls(renderer,RenderViewPurpose::Game);
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
                imageFont_=ImGui::GetFont();imageFontSize_=ImGui::GetFontSize();
                imageWidth_=frame.pixelWidth;imageHeight_=frame.pixelHeight;
                frame.viewport.hovered=interaction.hovered;frame.clicked=interaction.clicked;
                frame.viewport.inputBlocked=interaction.blocked;
                if(playing)drawCrosshair(*imageDrawList_,origin,size);
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
void GameView::drawCaptureStatus(const profiling::CaptureStatus& status) const {
    if(!imageDrawList_ || !imageFont_ || !imageWidth_ || !imageHeight_)return;
    const auto text=profilingStatusLabel(status);
    if(text.empty())return;
    const auto& style=ImGui::GetStyle();const auto padding=style.WindowPadding;
    const auto extent=imageFont_->CalcTextSizeA(imageFontSize_,std::numeric_limits<float>::max(),0,text.c_str());
    const ImVec2 origin{imageRegion_.x,imageRegion_.y},end{origin.x+imageRegion_.width,origin.y+imageRegion_.height};
    const ImVec2 minimum{origin.x+padding.x,std::max(origin.y+padding.y,end.y-extent.y-padding.y*3)};
    const ImVec2 maximum{minimum.x+extent.x+padding.x*2,minimum.y+extent.y+padding.y*2};
    const auto color=status.state==profiling::CaptureState::Capturing || status.state==profiling::CaptureState::Failed?
        IM_COL32(255,90,90,255):status.state==profiling::CaptureState::Saved?IM_COL32(100,220,130,255):ImGui::GetColorU32(ImGuiCol_Text);
    imageDrawList_->PushClipRect(origin,end,true);
    imageDrawList_->AddRectFilled(minimum,maximum,ImGui::GetColorU32(ImGuiCol_WindowBg),style.WindowRounding);
    if(style.WindowBorderSize>0)imageDrawList_->AddRect(minimum,maximum,ImGui::GetColorU32(ImGuiCol_Border),style.WindowRounding,0,style.WindowBorderSize);
    imageDrawList_->AddText(imageFont_,imageFontSize_,{minimum.x+padding.x,minimum.y+padding.y},color,text.c_str());
    imageDrawList_->PopClipRect();
}
void GameView::drawAttributeDebug(const Camera& camera,const Scene& scene,EntityId target,std::span<const AttributeDebugRow> rows,
                                const NavigationAgentProfileQuery& profiles,const ModelBoundsQuery& modelBounds) {
    if(!imageDrawList_ || !imageFont_ || !imageWidth_ || !imageHeight_)return;
    const Vec4 rectangle{imageRegion_.x,imageRegion_.y,imageRegion_.width,imageRegion_.height};
    imageDrawList_->PushClipRect({rectangle[0],rectangle[1]},{rectangle[0]+rectangle[2],rectangle[1]+rectangle[3]},true);
    const auto record=scene.find(target);
    if(record) {
        const auto bounds=attributeDebugActorBounds(scene,target,profiles,modelBounds);
        if(bounds.valid && math::finite(bounds.minimum) && math::finite(bounds.maximum)) {
            const auto matrix=camera.projectionMatrix(static_cast<float>(imageWidth_)/imageHeight_,ClipDepth::ZeroToOne)*camera.viewMatrix();
            const auto center=(bounds.minimum+bounds.maximum)*.5f;
            // Deliberately visible through scene geometry, like route diagnostics.
            boxWireSegments(bounds.maximum-bounds.minimum,[&](const Vec3& start,const Vec3& end) {
                if(const auto segment=projectClippedSegment(start+center,end+center,matrix,rectangle,ClipDepth::ZeroToOne))
                    imageDrawList_->AddLine({(*segment)[0][0],(*segment)[0][1]},{(*segment)[1][0],(*segment)[1][1]},IM_COL32(255,255,0,255),2.f);
            });
        }
    }
    std::string text=record?record->name:"No eligible actor";
    if(record)for(const auto& row:rows) {
        char number[64];
        if(std::isfinite(row.current))std::snprintf(number,sizeof(number),"%.6g",row.current);
        else std::snprintf(number,sizeof(number),"N/A");
        text+='\n';text+=row.name;text+=" | ";text+=number;
    }
    const auto& style=ImGui::GetStyle();const auto padding=style.WindowPadding;
    const auto extent=imageFont_->CalcTextSizeA(imageFontSize_,std::numeric_limits<float>::max(),0,text.c_str());
    const ImVec2 minimum{rectangle[0]+padding.x,rectangle[1]+padding.y};
    const ImVec2 maximum{minimum.x+extent.x+padding.x*2,minimum.y+extent.y+padding.y*2};
    imageDrawList_->AddRectFilled(minimum,maximum,ImGui::GetColorU32(ImGuiCol_WindowBg),style.WindowRounding);
    if(style.WindowBorderSize>0)imageDrawList_->AddRect(minimum,maximum,ImGui::GetColorU32(ImGuiCol_Border),style.WindowRounding,0,style.WindowBorderSize);
    imageDrawList_->AddText(imageFont_,imageFontSize_,{minimum.x+padding.x,minimum.y+padding.y},ImGui::GetColorU32(ImGuiCol_Text),text.c_str());
    imageDrawList_->PopClipRect();
}
void GameView::drawTraces(const Camera& camera,std::span<const TraceVisualRecord> traces) {
    if(!imageDrawList_)return;
    drawTraceOverlay(*imageDrawList_,camera,static_cast<float>(imageWidth_),static_cast<float>(imageHeight_),
        {imageRegion_.x,imageRegion_.y,imageRegion_.width,imageRegion_.height},traces);
}
void GameView::drawRoute(const Camera& camera,std::span<const Vec3> route,
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
    imageDrawList_->PopClipRect();
}
}
