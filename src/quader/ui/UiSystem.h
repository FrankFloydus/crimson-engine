#pragma once

#include "quader/assets/AssetRegistry.h"
#include "quader/core/Types.h"
#include "quader/modeling/ModelingSessionService.h"
#include "quader/render/ImGuiFilamentRenderer.h"
#include "quader/render/RenderSystem.h"
#include "quader/render/SceneRenderBridge.h"
#include "quader/render/ViewportRenderer.h"
#include "quader/scene/SceneDocument.h"
#include "quader/ui/ConsolePanel.h"
#include "quader/ui/ViewportPanel.h"
#include "quader/viewport/ViewportCameraController.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>

#include <cstdint>
#include <filesystem>

namespace quader {

class UiSystem {
public:
    ~UiSystem();

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
    const ViewportToolState& viewportToolState() const { return viewportPanel_.toolState(); }

private:
    void drawDockspace(bool& quitRequested);
    void drawOutliner(SceneDocument& document);
    void drawInspector(SceneDocument& document, ModelingSessionService& modeling);
    void applyTheme();

    SDL_Window* window_ = nullptr;
    ImGuiFilamentRenderer renderer_;
    ViewportPanel viewportPanel_;
    ConsolePanel consolePanel_;
    bool initialized_ = false;
    bool dockLayoutBuilt_ = false;
    EntityId outlinerAnchor_ = InvalidEntityId;
};

} // namespace quader
