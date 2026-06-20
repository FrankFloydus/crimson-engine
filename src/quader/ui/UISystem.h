#pragma once

#include "quader/assets/AssetRegistry.h"
#include "quader/core/Types.h"
#include "quader/modeling/ModelingSessionService.h"
#include "quader/render/ImGuiFilamentRenderer.h"
#include "quader/render/RenderSystem.h"
#include "quader/render/SceneRenderBridge.h"
#include "quader/render/ViewportRenderer.h"
#include "quader/scene/SceneDocument.h"
#include "quader/ui/UIPanelManager.h"
#include "quader/ui/UISettings.h"
#include "quader/ui/UIStyleTokens.h"
#include "quader/ui/ViewportPanel.h"
#include "quader/viewport/ViewportCameraController.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>

#include <cstdint>
#include <filesystem>
#include <string>

namespace quader {

class UISystem {
public:
    ~UISystem();

    void initialize(SDL_Window* window, RenderSystem& renderSystem, const std::filesystem::path& executableDir);
    void shutdown();

    bool processEvent(const SDL_Event& event);
    void beginFrame();
    void draw(ViewportRenderer& viewport,
            ViewportCameraController& camera,
            SceneRenderBridge& renderBridge,
            SceneDocument& document,
            AssetRegistry& assets,
            ModelingSessionService& modeling,
            AssetId defaultMaterial,
            bool& quitRequested);
    void prepareRenderData(std::uint32_t framebufferWidth, std::uint32_t framebufferHeight);
    void render(RenderSystem& renderSystem);
    const ViewportToolState& viewportToolState() const { return viewportPanel_->toolState(); }

private:
    void registerPanels();
    void drawDockspace(bool& quitRequested);
    void drawMenuBar(bool& quitRequested);
    void buildDefaultDockLayout(ImGuiID dockspaceId, ImVec2 dockspaceSize);
    void saveSettings();

    SDL_Window* window_ = nullptr;
    ImGuiFilamentRenderer renderer_;
    UIStyleTokens style_;
    UIPanelManager panels_;
    ViewportPanel* viewportPanel_ = nullptr;
    UISettings settings_;
    UISettingsPaths settingsPaths_;
    std::string imguiIniPath_;
    bool initialized_ = false;
    bool dockLayoutBuilt_ = false;
    bool hasSavedDockLayout_ = false;
};

} // namespace quader
