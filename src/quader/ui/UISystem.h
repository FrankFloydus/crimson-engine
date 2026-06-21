#pragma once

#include "quader/assets/AssetRegistry.h"
#include "quader/core/UndoRedoManager.h"
#include "quader/core/Types.h"
#include "quader/document/DocumentCommandService.h"
#include "quader/modeling/ModelingSessionService.h"
#include "quader/render/ImGuiFilamentRenderer.h"
#include "quader/render/RenderSystem.h"
#include "quader/render/SceneRenderBridge.h"
#include "quader/render/ViewportRenderer.h"
#include "quader/scene/SceneDocument.h"
#include "quader/ui/UIPanelManager.h"
#include "quader/ui/UIIconSet.h"
#include "quader/ui/UISettings.h"
#include "quader/ui/UIStyleTokens.h"
#include "quader/ui/ViewportPanel.h"
#include "quader/viewport/ViewportSettings.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace quader {

class TextureRegistry;

class UISystem {
public:
    ~UISystem();

    void initialize(SDL_Window* window, RenderSystem& renderSystem, const std::filesystem::path& executableDir);
    void shutdown();

    bool processEvent(const SDL_Event& event);
    void beginFrame();
    void draw(SceneRenderBridge& renderBridge,
            SceneDocument& document,
            AssetRegistry& assets,
            UndoRedoManager& undoRedo,
            DocumentCommandService& documentCommands,
            ModelingSessionService& modeling,
            AssetId defaultMaterial,
            bool& quitRequested);
    void prepareRenderData(std::uint32_t framebufferWidth, std::uint32_t framebufferHeight);
    void prepareViewports(SceneRenderBridge& renderBridge);
    void renderViewports(RenderSystem& renderSystem);
    void render(RenderSystem& renderSystem);
    const ViewportToolState& viewportToolState() const { return viewportPanel_->toolState(); }

private:
    void registerPanels(RenderSystem& renderSystem);
    void drawDockspace(bool& quitRequested);
    void drawMenuBar(bool& quitRequested);
    void drawSettingsPanel();
    void buildDefaultDockLayout(ImGuiID dockspaceId, ImVec2 dockspaceSize);
    void enforceSidebarDockMinimums();
    void setViewportCursorCaptured(bool captured);
    void warpCapturedCursor(Vec2 point);
    void loadIcons(filament::Engine& engine, TextureRegistry& textures, const std::filesystem::path& executableDir);
    void destroyIcons();
    void saveSettings();
    void saveViewportSettings();

    SDL_Window* window_ = nullptr;
    ImGuiFilamentRenderer renderer_;
    UIStyleTokens style_;
    UIPanelManager panels_;
    ViewportPanel* viewportPanel_ = nullptr;
    UIIconSet icons_;
    std::vector<filament::Texture*> iconTextures_;
    std::vector<ImTextureID> iconTextureIds_;
    filament::Engine* iconEngine_ = nullptr;
    TextureRegistry* iconTextureRegistry_ = nullptr;
    UISettings settings_;
    UISettingsPaths settingsPaths_;
    ViewportSettings viewportSettings_;
    std::string imguiIniPath_;
    bool settingsPanelOpen_ = false;
    bool initialized_ = false;
    bool dockLayoutBuilt_ = false;
    bool hasSavedDockLayout_ = false;
    bool relativeMouseCaptured_ = false;
    bool relativeMouseCaptureWarningIssued_ = false;
    Vec2 relativeMouseDelta_ = {};
    UIPanelId focusedPanel_ = UIPanelId::Viewport;
};

} // namespace quader
