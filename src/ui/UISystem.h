#pragma once

#include "assets/AssetRegistry.h"
#include "core/LocalizationService.h"
#include "core/UndoRedoManager.h"
#include "core/Types.h"
#include "document/DocumentCommandService.h"
#include "modeling/ModelingSessionService.h"
#include "render/ImGuiFilamentRenderer.h"
#include "render/RenderSystem.h"
#include "render/SceneRenderBridge.h"
#include "render/ViewportRenderer.h"
#include "scene/SceneDocument.h"
#include "ui/MainMenuBar.h"
#include "ui/ToolSettingsPanel.h"
#include "ui/UIPanelManager.h"
#include "ui/UIIconSet.h"
#include "ui/UISettings.h"
#include "ui/UIStyleTokens.h"
#include "ui/ViewportSettingsPanel.h"
#include "ui/ViewportPanel.h"
#include "viewport/ViewportSettings.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace quader {

class QuaderEditorApi;
class TextureRegistry;

class UISystem {
public:
    ~UISystem();

    void initialize(SDL_Window* window, RenderSystem& renderSystem, const std::filesystem::path& executableDir);
    void shutdown();

    bool processEvent(const SDL_Event& event);
    void beginFrame();
    void draw(QuaderEditorApi& editor,
            SceneRenderBridge& renderBridge,
            SceneDocument& document,
            AssetRegistry& assets,
            UndoRedoManager& undoRedo,
            DocumentCommandService& documentCommands,
            ModelingSessionService& modeling,
            AssetId defaultMaterial,
            bool& quitRequested);
    void prepareRenderData(std::uint32_t framebufferWidth, std::uint32_t framebufferHeight);
    void prepareViewports(
            QuaderEditorApi& editor,
            SceneRenderBridge& renderBridge,
            const SceneDocument& document,
            const AssetRegistry& assets);
    void renderViewports(RenderSystem& renderSystem);
    void render(RenderSystem& renderSystem);
    const ViewportToolState& viewportToolState() const { return viewportPanel_->toolState(); }

private:
    void registerPanels(RenderSystem& renderSystem);
    void drawDockspace(
            QuaderEditorApi& editor,
            bool& quitRequested,
            SceneDocument& document,
            DocumentCommandService& documentCommands,
            ModelingSessionService& modeling);
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
    MainMenuBar mainMenuBar_;
    ViewportSettingsPanel viewportSettingsPanel_;
    ToolSettingsPanel toolSettingsPanel_;
    ViewportPanel* viewportPanel_ = nullptr;
    UIIconSet icons_;
    LocalizationService localization_;
    std::vector<filament::Texture*> iconTextures_;
    std::vector<ImTextureID> iconTextureIds_;
    filament::Engine* iconEngine_ = nullptr;
    TextureRegistry* iconTextureRegistry_ = nullptr;
    UISettings settings_;
    UISettingsPaths settingsPaths_;
    ViewportSettings viewportSettings_;
    std::string imguiIniPath_;
    bool settingsPanelOpen_ = false;
    bool toolSettingsPanelOpen_ = false;
    bool initialized_ = false;
    bool dockLayoutBuilt_ = false;
    bool hasSavedDockLayout_ = false;
    bool relativeMouseCaptured_ = false;
    bool relativeMouseCaptureWarningIssued_ = false;
    Vec2 relativeMouseDelta_ = {};
    UIPanelId focusedPanel_ = UIPanelId::Viewport;
};

} // namespace quader
