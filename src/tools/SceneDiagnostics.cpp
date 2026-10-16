#include "SceneDiagnostics.h"
#include "crimson-renderer/RenderTypes.h"
#include <imgui.h>
#include <array>

namespace crimson {
void drawSceneDiagnostics(std::string_view generalStatus,std::string_view pickingStatus,
                          std::string_view modelStatus,const PointShadowStats* shadows) {
    const bool keyShadow=shadows&&(shadows->dedicatedCharacterUpdates||shadows->dedicatedCharacterCacheHits);
    if (generalStatus.empty() && pickingStatus.empty() && modelStatus.empty() && (!shadows || (!shadows->activeLights&&!shadows->directionalLights&&!keyShadow))) return;
    if (ImGui::Begin("Scene diagnostics",nullptr,ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::PushTextWrapPos(0);
        for (const auto status:std::array{generalStatus,pickingStatus,modelStatus})
            if (!status.empty()) ImGui::TextUnformatted(status.data(),status.data()+status.size());
        ImGui::PopTextWrapPos();
        if(shadows && (shadows->activeLights||shadows->directionalLights||keyShadow)) {
            ImGui::SeparatorText("Shadows");
            ImGui::Text("Lights: %u / %u allocated; %u px per face",shadows->activeLights,static_cast<unsigned>(shadows->capacity),static_cast<unsigned>(shadows->resolution));
            ImGui::Text("Lights rendered: %u; cached: %u; faces: %u",shadows->renderedLights,shadows->cacheHits,shadows->renderedFaces);
            ImGui::Text("Deferred: %u; awaiting a valid map: %u",shadows->pointDeferred,shadows->pointCriticalDeferred);
            ImGui::Text("Casters: %u; draws: %u",shadows->casters,shadows->drawCalls);
            ImGui::Text("Directional contexts: %u; rendered: %u; cached: %u",shadows->directionalLights,shadows->directionalRendered,shadows->directionalCacheHits);
            ImGui::Text("World projections: %u; character projections: %u",shadows->directionalWorldProjections,shadows->characterProjections);
            ImGui::Text("Key silhouette updates: %u; cached: %u",shadows->dedicatedCharacterUpdates,shadows->dedicatedCharacterCacheHits);
            ImGui::Text("Actor silhouettes: %u / %u admitted; %u without a map",shadows->actorShadowAdmitted,shadows->actorShadowRequested,shadows->actorShadowUnadmitted);
            ImGui::Text("Atlas allocations: %.1f MiB",static_cast<double>(shadows->allocatedBytes)/(1024*1024));
        }
    }
    ImGui::End();
}
} // namespace crimson
