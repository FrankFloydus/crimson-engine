#pragma once

#include "crimson-renderer/RenderTypes.h"
#include <cstdint>
#include <string>
#include <vector>

struct SDL_Window;

namespace crimson {

class Renderer;
class AssetLibrary;
struct ModelInstance;

struct EditorFrame {
    ViewportRegion viewport;
    bool materialChanged{false};
    bool environmentChanged{false};
    std::string modelDropId;
    std::vector<std::string> modelDropIds;
    bool showSphere{false};
    bool frameModel{false};
    Vec2 modelDropPosition{};
    bool documentReplaced{false};
};

struct BrowserActions {
    bool newScene{false};
    std::string openScene, deleteScene;
};

BrowserActions drawAssetBrowser(AssetLibrary& assets, SDL_Window* window);
bool drawEnvironmentControls(EnvironmentSettings& environment, AssetLibrary& assets, const Renderer& renderer);

// Call once after creating the ImGui context. DPI is supplied by the host;
// this does not change window/framebuffer coordinates or SDL input scaling.
void applyToolsStyle(float dpiScale = 1.0f);
EditorFrame drawEditor(SceneSettings& settings, Renderer& renderer, AssetLibrary& assets, SDL_Window* window,
                       const ModelInstance* model = nullptr, const std::string& previewStatus = {});

} // namespace crimson
