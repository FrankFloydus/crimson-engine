#include "SceneDiagnostics.h"
#include "crimson-renderer/RenderTypes.h"
#include <imgui.h>
#include <array>

namespace crimson {
void drawSceneDiagnostics(std::string_view generalStatus,std::string_view pickingStatus,
                          std::string_view modelStatus,const PointShadowStats* shadows) {
    if (generalStatus.empty() && pickingStatus.empty() && modelStatus.empty() && (!shadows || !shadows->activeLights)) return;
    if (ImGui::Begin("Scene diagnostics",nullptr,ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::PushTextWrapPos(0);
        for (const auto status:std::array{generalStatus,pickingStatus,modelStatus})
            if (!status.empty()) ImGui::TextUnformatted(status.data(),status.data()+status.size());
        ImGui::PopTextWrapPos();
        if(shadows && shadows->activeLights) {
            ImGui::SeparatorText("Point shadows");
            ImGui::Text("Lights: %u / %u allocated; %u px per face",shadows->activeLights,static_cast<unsigned>(shadows->capacity),static_cast<unsigned>(shadows->resolution));
            ImGui::Text("Lights rendered: %u; cached: %u; faces: %u",shadows->renderedLights,shadows->cacheHits,shadows->renderedFaces);
            ImGui::Text("Casters: %u; draws: %u",shadows->casters,shadows->drawCalls);
            ImGui::Text("Atlas allocations: %.1f MiB",static_cast<double>(shadows->allocatedBytes)/(1024*1024));
        }
    }
    ImGui::End();
}
} // namespace crimson
