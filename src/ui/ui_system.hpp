////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/asset_registry.hpp"
#include "core/frame_timing.hpp"
#include "core/localization_service.hpp"
#include "core/types.hpp"
#include "render/imgui_filament_renderer.hpp"
#include "render/render_system.hpp"
#include "render/scene_render_bridge.hpp"
#include "render/viewport_renderer.hpp"
#include "scene/scene_document.hpp"
#include "ui/main_menu_bar.hpp"
#include "ui/tool_settings_panel.hpp"
#include "ui/ui_panel_manager.hpp"
#include "ui/ui_icon_set.hpp"
#include "ui/ui_settings.hpp"
#include "ui/ui_style_tokens.hpp"
#include "ui/viewport_settings_panel.hpp"
#include "ui/viewport_panel.hpp"
#include "viewport/viewport_settings.hpp"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace quader {

class QuaderEditorApi;
class EditorSelectionService;
class ModelingSessionService;
class TextureRegistry;

/**
 * Enumerates native window chrome actions requested by the ImGui shell.
 */
enum class WindowChromeAction {
    Minimize,
    ToggleMaximizeRestore,
    Close,
};

/**
 * Owns UI System behavior within the ImGui editor presentation layer.
 */
class UISystem {
public:
    ~UISystem();

    void initialize(
            SDL_Window* window,
            RenderSystem& renderSystem,
            const std::filesystem::path& executableDir,
            bool customChrome);
    void shutdown();

    bool processEvent(const SDL_Event& event);
    void beginFrame(const FrameTiming& frameTiming);
    void endFrameWithoutRender();
    void draw(QuaderEditorApi& editor,
            SceneRenderBridge& renderBridge,
            SceneDocument& document,
            AssetRegistry& assets,
            ModelingSessionService& modeling,
            EditorSelectionService& selection,
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
    void setRenderPerformanceStats(RenderFramePerformanceStats stats);
    void setViewportRenderResizeSuspended(bool suspended);
    [[nodiscard]] bool needsContinuousRendering() const;
    std::optional<WindowChromeAction> takeWindowChromeAction();
    const ViewportToolState& viewportToolState() const { return viewportPanel_->toolState(); }

private:
    void registerPanels(RenderSystem& renderSystem);
    MainMenuBarResult drawWindowChrome(
            QuaderEditorApi& editor,
            bool& quitRequested,
            SceneDocument& document,
            ModelingSessionService& modeling);
    void drawDockspace(
            QuaderEditorApi& editor,
            bool& quitRequested,
            SceneDocument& document,
            ModelingSessionService& modeling);
    void buildDefaultDockLayout(ImGuiID dockspaceId, ImVec2 dockspaceSize);
    void closeMenuPopupForPassThroughClick();
    void enforceSidebarDockMinimums();
    void setViewportCursorCaptured(bool captured);
    void warpCapturedCursor(QVec2 point);
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
    bool customChrome_ = false;
    bool dockLayoutBuilt_ = false;
    bool hasSavedDockLayout_ = false;
    bool mainMenuPopupOpen_ = false;
    bool relativeMouseCaptured_ = false;
    bool relativeMouseCaptureWarningIssued_ = false;
    QVec2 relativeMouseDelta_ = {};
    FrameTiming currentFrameTiming_ = {};
    RenderFramePerformanceStats renderPerformanceStats_;
    UIPanelId focusedPanel_ = UIPanelId::Viewport;
    std::optional<WindowChromeAction> pendingChromeAction_;
    bool viewportRenderResizeSuspended_ = false;
};

} // namespace quader
