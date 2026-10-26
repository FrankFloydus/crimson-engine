#pragma once
#include <imgui.h>

namespace crimson {
// Native relative motion has no editor pointer. Event filtering alone leaves
// the last ImGui position hovering a widget even when there are no new events.
// Scope this around the complete UI frame; keyboard/commands remain enabled.
class NavigationUiMouseScope final {
public:
    explicit NavigationUiMouseScope(bool flying):active_(flying) {
        if(active_) {
            previous_=ImGui::GetIO().ConfigFlags&ImGuiConfigFlags_NoMouse;
            ImGui::GetIO().ConfigFlags|=ImGuiConfigFlags_NoMouse;
        }
    }
    ~NavigationUiMouseScope() {
        if(active_) {
            auto& flags=ImGui::GetIO().ConfigFlags;
            flags=(flags&~ImGuiConfigFlags_NoMouse)|previous_;
        }
    }
    NavigationUiMouseScope(const NavigationUiMouseScope&)=delete;
    NavigationUiMouseScope& operator=(const NavigationUiMouseScope&)=delete;
private:
    ImGuiConfigFlags previous_{};
    bool active_{};
};
}
