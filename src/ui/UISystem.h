////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/AssetRegistry.h"
#include "core/FrameTiming.h"
#include "core/LocalizationService.h"
#include "editor/modeling/ModelingTypes.h"
#include "render/RenderSystem.h"
#include "render/SceneRenderBridge.h"
#include "scene/SceneDocument.h"
#include "ui/backends/rml/MainLayoutRmlBuilder.h"
#include "ui/backends/rml/RmlUiContextHost.h"
#include "ui/components/organisms/ModelingModeShelfComponent.h"
#include "ui/controllers/MainLayoutController.h"
#include "ui/controllers/MainLayoutDockDragController.h"
#include "ui/controllers/MainLayoutSashRegistry.h"
#include "ui/controllers/ModelingModeShelfController.h"
#include "ui/controllers/ResizableSplitController.h"
#include "ui/controllers/ViewportHost.h"
#include "ui/UISettings.h"
#include "viewport/ViewportSettings.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_video.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace quader {

class QuaderEditorApi;
class EditorSelectionService;
class ModelingSessionService;

/**
 * Owns the RmlUi-backed app shell for the native app.
 */
class UISystem {
public:
    UISystem();
    ~UISystem();

    void initialize(SDL_Window* window, RenderSystem& renderSystem, const std::filesystem::path& executableDir);
    void shutdown();

    bool processEvent(const SDL_Event& event);
    void beginFrame(const FrameTiming& frameTiming);
    void draw(QuaderEditorApi& editor,
            SceneRenderBridge& renderBridge,
            SceneDocument& document,
            AssetRegistry& assets,
            ModelingSessionService& modeling,
            EditorSelectionService& selection,
            AssetId defaultMaterial,
            bool& quitRequested);
    void prepareLayout(std::uint32_t framebufferWidth, std::uint32_t framebufferHeight);
    void prepareRenderData(std::uint32_t framebufferWidth, std::uint32_t framebufferHeight);
    void prepareViewports(
            QuaderEditorApi& editor,
            SceneRenderBridge& renderBridge,
            const SceneDocument& document,
            const AssetRegistry& assets);
    void renderViewports(RenderSystem& renderSystem);
    void renderWindowedViewports(RenderSystem& renderSystem);
    void render(RenderSystem& renderSystem);
    void installRuntimeHooks(QuaderEditorApi& editor);
    [[nodiscard]] bool shouldBypassPlatformChrome(const SDL_Event& event) const;
    [[nodiscard]] std::optional<WindowChromeAction> takeWindowChromeAction();
    const ViewportToolState& viewportToolState() const { return viewportHost_.viewportToolState(); }

private:
    /**
     * Enumerates OS cursor states owned by main-layout pointer gestures.
     */
    enum class MainLayoutCursorKind {
        Default,
        ResizeHorizontal,
        ResizeVertical,
        DropForbidden,
    };

    void updateWindowMetrics();
    void updateRmlDensityScale();
    void saveViewportSettings();
    void saveUISettings();
    void loadLocalization(const std::filesystem::path& executableDir);
    void applyLocalizedMainLayoutLabels();
    void rebuildMainLayout();
    void rebuildModelingModeShelf();
    bool updateModelingModeShelfOverflowFromRml();
    void rebuildMainLayoutHitRegions();
    bool activateModelingModeShelfControl(std::string_view controlId);
    void applyPendingModelingModeShelfIntents(QuaderEditorApi& editor);
    bool handleMainLayoutPointerEvent(const SDL_Event& event);
    std::optional<std::pair<float, float>> mainLayoutPointerFramebufferPosition(const SDL_Event& event) const;
    std::optional<ResizableSplitHandle> mainLayoutSplitHandle(std::string_view handleId) const;
    void beginMainLayoutResize(std::string_view handleId, float mouseX, float mouseY);
    void updateMainLayoutResize(float mouseX, float mouseY);
    void endMainLayoutResize(float mouseX, float mouseY);
    void cancelMainLayoutResize();
    bool beginMainLayoutDockDragCandidate(float mouseX, float mouseY);
    void updateMainLayoutDockDrag(float mouseX, float mouseY);
    void endMainLayoutDockDrag(float mouseX, float mouseY);
    void cancelMainLayoutDockDrag();
    void updateMainLayoutDockDragPreviewPosition(float mouseX, float mouseY);
    void releaseMainLayoutMouseCapture();
    [[nodiscard]] bool shouldCancelMainLayoutPointerGesture(const SDL_Event& event) const;
    void initializeMainLayoutCursors();
    void shutdownMainLayoutCursors();
    void updateMainLayoutCursor(float mouseX, float mouseY);
    void setMainLayoutCursor(MainLayoutCursorKind kind);
    [[nodiscard]] MainLayoutCursorKind mainLayoutCursorForAxis(ResizableSplitAxis axis) const;
    void updateMainViewportFrameFromRml();
    [[nodiscard]] bool normalizeMainLayoutForMeasuredBounds();
    [[nodiscard]] bool rmlConsumedEventBlocksViewport(const SDL_Event& event) const;
    [[nodiscard]] bool pointerEventInsideMainViewport(const SDL_Event& event) const;
    [[nodiscard]] bool pointerEventInsideRmlElement(const SDL_Event& event, std::string_view elementId) const;
    [[nodiscard]] bool pointerEventInsideModelingModeShelf(const SDL_Event& event) const;
    [[nodiscard]] bool pointerEventShouldBypassViewport(const SDL_Event& event) const;
    bool isMainLayoutPanelVisible(std::string_view panel) const;
    bool setMainLayoutPanelVisible(std::string_view panel, bool visible);
    void toggleDebugPalette();
    void applyDebugPaletteVisibility();

    SDL_Window* window_ = nullptr;
    RmlUiContextHost rmlHost_;
    MainLayoutModel mainLayout_ = MainLayoutModel::defaults();
    MainLayoutController mainLayoutController_;
    ModelingModeShelfController modelingModeShelfController_;
    MainLayoutLabels mainLayoutLabels_;
    ViewportHost viewportHost_;
    UISettingsPaths settingsPaths_;
    UISettings uiSettings_;
    LocalizationService localization_;
    ViewportSettings viewportSettings_;
    std::uint32_t framebufferWidth_ = 1u;
    std::uint32_t framebufferHeight_ = 1u;
    int windowWidth_ = 1;
    int windowHeight_ = 1;
    int mainLayoutLeftPx_ = 0;
    int mainLayoutTopPx_ = 0;
    int mainLayoutWidthDp_ = 0;
    int mainLayoutHeightDp_ = 0;
    std::optional<RmlElementRect> mainViewportFrame_;
    float rmlDpRatio_ = 1.0f;
    bool initialized_ = false;
    bool debugPaletteVisible_ = false;
    bool mainLayoutDirty_ = true;
    MainLayoutSashRegistry mainLayoutSashRegistry_;
    ResizableSplitController mainLayoutResizeController_;
    MainLayoutDockDragController mainLayoutDockDragController_;
    int mainLayoutDockDragPreviewLeftDp_ = 0;
    int mainLayoutDockDragPreviewTopDp_ = 0;
    int mainLayoutDockDragPreviewWidthDp_ = 0;
    int mainLayoutDockDragPreviewHeightDp_ = 0;
    bool mainLayoutMouseCaptured_ = false;
    SDL_Cursor* mainLayoutHorizontalResizeCursor_ = nullptr;
    SDL_Cursor* mainLayoutVerticalResizeCursor_ = nullptr;
    SDL_Cursor* mainLayoutDropForbiddenCursor_ = nullptr;
    MainLayoutCursorKind activeMainLayoutCursor_ = MainLayoutCursorKind::Default;
};

} // namespace quader
