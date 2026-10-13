#include "SceneDiagnostics.h"
#include <imgui.h>
#include <array>

namespace crimson {
void drawSceneDiagnostics(std::string_view generalStatus,std::string_view pickingStatus,
                          std::string_view modelStatus) {
    if (generalStatus.empty() && pickingStatus.empty() && modelStatus.empty()) return;
    if (ImGui::Begin("Scene diagnostics",nullptr,ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::PushTextWrapPos(0);
        for (const auto status:std::array{generalStatus,pickingStatus,modelStatus})
            if (!status.empty()) ImGui::TextUnformatted(status.data(),status.data()+status.size());
        ImGui::PopTextWrapPos();
    }
    ImGui::End();
}
} // namespace crimson
