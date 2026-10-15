#pragma once
#include <imgui.h>

namespace crimson {
struct GameViewInteraction {
    bool hovered{},clicked{},held{},blocked{};
};
// An image needs its own input item: otherwise ImGui treats a held image click
// as a window move, which would cancel gameplay ownership on the next frame.
GameViewInteraction drawGameViewImage(ImTextureRef texture,const ImVec2& size);
// Evaluated in the current Game window after ImGui has resolved focus and text.
bool gameMetricsToggleRequested(bool playing);
}
