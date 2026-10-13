#pragma once

#include "crimson-renderer/RenderTypes.h"
#include <cstdint>

struct SDL_Window;

namespace crimson {

class Renderer;
class AssetLibrary;

struct EditorFrame {
    ViewportRegion viewport;
    bool materialChanged{false};
    bool environmentChanged{false};
};

// Call once after creating the ImGui context. DPI is supplied by the host;
// this does not change window/framebuffer coordinates or SDL input scaling.
void applyToolsStyle(float dpiScale = 1.0f);
EditorFrame drawEditor(SceneSettings& settings, Renderer& renderer, AssetLibrary& assets, SDL_Window* window);

} // namespace crimson
