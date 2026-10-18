#pragma once
#include <imgui.h>

namespace crimson {
struct GameViewInteraction {
    bool hovered{},clicked{},blocked{};
};
// An image needs its own input item: otherwise ImGui treats a held image click
// as a window move, which would cancel gameplay ownership on the next frame.
GameViewInteraction drawGameViewImage(ImTextureRef texture,const ImVec2& size);
// Evaluated in the current Game window after ImGui has resolved focus and text.
bool gameMetricsToggleRequested(bool playing);
// Tab belongs to target cycling only in the focused Game window, outside text,
// popups and other widgets. Shortcut routing also keeps it out of UI tabbing.
int gameAttributeDebugCycleRequested(bool enabled);
}
