#include "ProfilingMenu.h"
#include "profiling/CaptureController.h"
#include <crimson/renderer/core/Profiler.hpp>
#include <imgui.h>

namespace crimson {
void drawProfilingMenu(profiling::CaptureController& captures,bool playing) {
    if(!ImGui::BeginMainMenuBar())return;
    if(ImGui::BeginMenu("Profiling")) {
        const auto status=captures.status();
        if(ImGui::MenuItem("Capture gameplay (10 s)",nullptr,false,
            CRIMSON_ENABLE_PROFILING && playing && !status.active()))captures.captureNow(profiling::CaptureKind::Gameplay,10);
        if(ImGui::MenuItem("Stop and save",nullptr,false,status.active() && status.state!=profiling::CaptureState::Saving))captures.requestStop();
        ImGui::Separator();
        ImGui::MenuItem(profiling::captureStateName(status.state),nullptr,false,false);
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !status.detail.empty())ImGui::SetTooltip("%s",status.detail.c_str());
        if(status.state==profiling::CaptureState::Saved && !status.trace.empty()) {
            const auto path=status.trace.u8string();
            if(ImGui::MenuItem("Copy trace path"))ImGui::SetClipboardText(reinterpret_cast<const char*>(path.c_str()));
        }
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}
}
