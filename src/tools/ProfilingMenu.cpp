#include "ProfilingMenu.h"
#include "ProfilingStatus.h"
#include "profiling/CaptureController.h"
#include <crimson/renderer/core/Profiler.hpp>
#include <imgui.h>

namespace crimson {
namespace {
void statusTooltip(const profiling::CaptureStatus& status) {
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !status.detail.empty())
        ImGui::SetTooltip("%s",status.detail.c_str());
}
void copyTracePath(const profiling::CaptureStatus& status) {
    const auto path=status.trace.u8string();
    ImGui::SetClipboardText(reinterpret_cast<const char*>(path.c_str()));
}
std::string compactStatusLabel(const profiling::CaptureStatus& status) {
    switch(status.state) {
    case profiling::CaptureState::Connecting:return "Connecting";
    case profiling::CaptureState::Capturing:return "REC "+profilingCaptureTime(status.elapsedSeconds);
    case profiling::CaptureState::Saving:return "Saving";
    case profiling::CaptureState::Saved:return "Saved";
    case profiling::CaptureState::Failed:return "Failed";
    case profiling::CaptureState::Idle:return {};
    }
    return "Failed";
}
}

void drawProfilingMenu(profiling::CaptureController& captures,bool playing,float controlsRight) {
    ImGui::PushID(&captures);
    auto status=captures.status();
    if(ImGui::BeginMenu("Profiling")) {
        if(ImGui::MenuItem("Start recording",nullptr,false,CRIMSON_ENABLE_PROFILING && playing && !captures.active())) {
            captures.captureNow(profiling::CaptureKind::Gameplay,0);
            status=captures.status();
        }
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            if(!CRIMSON_ENABLE_PROFILING)ImGui::SetTooltip("Profiling is disabled in this build.");
            else if(!playing)ImGui::SetTooltip("Start Play to record gameplay.");
        }
        if(ImGui::MenuItem("Stop and save",nullptr,false,status.active() && status.state!=profiling::CaptureState::Saving))
            captures.requestStop();
        status=captures.status();
        if(status.state!=profiling::CaptureState::Idle) {
            ImGui::Separator();
            const auto text=profilingStatusLabel(status);
            ImGui::TextUnformatted(text.c_str());statusTooltip(status);
        }
        if(status.state==profiling::CaptureState::Saved && !status.trace.empty() && ImGui::MenuItem("Copy trace path"))
            copyTracePath(status);
        if(status.state==profiling::CaptureState::Failed && !status.detail.empty()) {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+ImGui::GetFontSize()*34.f);
            ImGui::TextWrapped("%s",status.detail.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::EndMenu();
    }

    // Re-read after actions, so the click which starts or stops a capture also
    // changes the visible feedback in this frame, outside the closed menu.
    status=captures.status();
    if(status.state!=profiling::CaptureState::Idle) {
        const auto text=profilingStatusLabel(status);
        const bool canStop=status.active() && status.state!=profiling::CaptureState::Saving;
        const bool saved=status.state==profiling::CaptureState::Saved;
        const bool failed=status.state==profiling::CaptureState::Failed;
        const auto& style=ImGui::GetStyle();
        const float limit=controlsRight>0.f?controlsRight:ImGui::GetWindowWidth()-style.WindowPadding.x;
        const float available=limit-ImGui::GetCursorPosX();
        const auto labelWidth=[&](const std::string& label) {
            return ImGui::CalcTextSize(label.c_str()).x+((saved||failed)?style.FramePadding.x*2:0.f);
        };
        const auto requiredWidth=[&](const std::string& label,const char* stop) {
            return labelWidth(label)+(stop?ImGui::CalcTextSize(stop).x+style.FramePadding.x*2+style.ItemSpacing.x:0.f);
        };
        auto displayText=text;
        const char* stopLabel=canStop?"Stop and save":nullptr;
        if(available<requiredWidth(displayText,stopLabel)) {
            displayText=compactStatusLabel(status);
            stopLabel=canStop?"Stop":nullptr;
            if(stopLabel && available<requiredWidth(displayText,stopLabel) &&
                status.state==profiling::CaptureState::Capturing && available>=requiredWidth("REC",stopLabel))
                displayText="REC";
            if(available<requiredWidth(displayText,stopLabel))stopLabel=nullptr;
            if(available<labelWidth(displayText) && status.state==profiling::CaptureState::Capturing)displayText="REC";
            else if(available<labelWidth(displayText) && status.active())displayText="...";
        }
        if(available>=requiredWidth(displayText,stopLabel)) {
            if(saved||failed) {
                if(ImGui::Button(displayText.c_str()))ImGui::OpenPopup("Capture details");
            } else ImGui::TextUnformatted(displayText.c_str());
            statusTooltip(status);
            if(stopLabel) {
                ImGui::SameLine();
                if(ImGui::Button(stopLabel))captures.requestStop();
                if(ImGui::IsItemHovered())ImGui::SetTooltip("Stop recording and save the trace.");
            }
        }
        if(ImGui::BeginPopup("Capture details")) {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+ImGui::GetFontSize()*38.f);
            ImGui::TextUnformatted(text.c_str());
            if(!status.detail.empty())ImGui::TextWrapped("%s",status.detail.c_str());
            if(saved && !status.trace.empty()) {
                const auto path=status.trace.u8string();
                ImGui::Separator();
                ImGui::TextWrapped("%s",reinterpret_cast<const char*>(path.c_str()));
                if(ImGui::Button("Copy trace path"))copyTracePath(status);
            }
            ImGui::PopTextWrapPos();
            ImGui::EndPopup();
        }
    }
    ImGui::PopID();
}
}
