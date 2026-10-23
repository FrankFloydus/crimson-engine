#pragma once

#include <imgui.h>

namespace crimson {
struct ViewportImageInteraction {
    bool hovered{},textInputBlocked{};
};

// An Inspector text/drag field can own ImGui's active item until this click.
// Release only its panel edit before deciding whether the image can be picked.
template<class FinishPanelEdit>
ViewportImageInteraction viewportImageInteraction(FinishPanelEdit&& finishPanelEdit) {
    const bool hovered=ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    const bool finished=hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !ImGui::GetDragDropPayload() && finishPanelEdit();
    // WantTextInput is computed at NewFrame and can remain true after the
    // Inspector has already processed this click's deactivation.
    return {hovered,ImGui::GetIO().WantTextInput && ImGui::IsAnyItemActive() && !finished};
}
} // namespace crimson
