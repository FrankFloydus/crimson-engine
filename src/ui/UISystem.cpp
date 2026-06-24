////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/UISystem.h"

#include "api/QuaderEditorApi.h"
#include "core/Logger.h"
#include "ui/DebugPaletteView.h"
#include "ui/UIUtilityTokens.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace quader {
namespace {

constexpr std::string_view kMainLayoutSidebarResizer = "sidebar";
constexpr std::string_view kMainLayoutPanelResizer = "panels";

std::optional<SDL_WindowID> eventWindowId(const SDL_Event& event) {
    switch (event.type) {
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        return event.window.windowID;
    case SDL_EVENT_MOUSE_MOTION:
        return event.motion.windowID;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        return event.button.windowID;
    case SDL_EVENT_MOUSE_WHEEL:
        return event.wheel.windowID;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        return event.key.windowID;
    case SDL_EVENT_TEXT_INPUT:
        return event.text.windowID;
    default:
        return std::nullopt;
    }
}

QuaderApiVoidResult runtimeStatus(bool changed, std::string message = {}) {
    return {
            .ok = true,
            .changed = changed,
            .message = std::move(message),
            .mutationKind = changed ? QuaderMutationKind::Runtime : QuaderMutationKind::None,
    };
}

QuaderApiVoidResult runtimeFailure(std::string message) {
    return {
            .ok = false,
            .changed = false,
            .message = std::move(message),
            .mutationKind = QuaderMutationKind::None,
    };
}

bool mainLayoutPanelVisibleInSidebar(const MainLayoutModel& model, std::string_view panelId) {
    const std::optional<DockLocation> location = dockPanelLocation(model.dockLayout, panelId);
    if (!location || *location != DockLocation::Sidebar) {
        return false;
    }
    if (panelId == kMainLayoutPanelOutliner) {
        return model.outlinerVisible;
    }
    if (panelId == kMainLayoutPanelInspector) {
        return model.inspectorVisible;
    }
    return false;
}

int mainLayoutVisibleSidebarDockPanelCount(const MainLayoutModel& model) {
    int count = 0;
    for (std::string_view panelId : mainLayoutDockablePanelIds()) {
        if (mainLayoutPanelVisibleInSidebar(model, panelId)) {
            ++count;
        }
    }
    return count;
}

std::string escapeRmlText(std::string_view text) {
    std::string escaped;
    escaped.reserve(text.size());
    for (const char character : text) {
        switch (character) {
        case '&':
            escaped += "&amp;";
            break;
        case '<':
            escaped += "&lt;";
            break;
        case '>':
            escaped += "&gt;";
            break;
        case '"':
            escaped += "&quot;";
            break;
        default:
            escaped.push_back(character);
            break;
        }
    }
    return escaped;
}

} // namespace

UISystem::UISystem()
    : mainLayoutController_(mainLayout_) {}

UISystem::~UISystem() {
    shutdown();
}

void UISystem::initialize(SDL_Window* window, RenderSystem& renderSystem, const std::filesystem::path& executableDir) {
    window_ = window;
    updateWindowMetrics();

    settingsPaths_ = UISettings::resolvePaths(executableDir);
    std::error_code error;
    std::filesystem::create_directories(settingsPaths_.directory, error);
    uiSettings_ = UISettings::load(settingsPaths_.settingsFile);
    mainLayout_ = uiSettings_.mainLayout();
    loadLocalization(executableDir);
    applyLocalizedMainLayoutLabels();
    viewportSettings_ = ViewportSettings::load(settingsPaths_.viewportSettingsFile);

    viewportHost_.initialize(window_, renderSystem, viewportSettings_);
    initializeMainLayoutCursors();

    const std::vector<DebugPaletteColorToken> debugPaletteColors =
            loadDebugPaletteColorTokens(appPaletteTokenPath(executableDir));
    const std::vector<UIUtilityToken> utilityTokens =
            loadUIUtilityTokens(appUtilityTokenPath(executableDir));
    const std::string utilityRcss = buildUIUtilityRcss(utilityTokens, debugPaletteColors);
    const std::string debugPaletteRml = buildDebugPalettePageRml(debugPaletteColors);

    if (!rmlHost_.initialize(window_,
                renderSystem,
                executableDir,
                utilityRcss,
                debugPaletteRml,
                viewportHost_.textureSource(),
                viewportHost_.textureHandle(),
                viewportHost_.textureWidth(),
                viewportHost_.textureHeight())) {
        shutdown();
        return;
    }
    updateRmlDensityScale();
    rebuildMainLayout();
    applyDebugPaletteVisibility();

    initialized_ = true;
    LOG_INFO("RmlUi app shell initialized");
}

void UISystem::shutdown() {
    if (!initialized_ && !rmlHost_.initialized()) {
        return;
    }

    cancelMainLayoutResize();
    viewportHost_.setCursorCaptured(false);
    saveUISettings();
    saveViewportSettings();
    rmlHost_.shutdown();
    viewportHost_.shutdown();
    shutdownMainLayoutCursors();
    initialized_ = false;
    window_ = nullptr;
}

bool UISystem::processEvent(const SDL_Event& event) {
    if (!window_) {
        return false;
    }

    updateWindowMetrics();
    if (event.type == SDL_EVENT_KEY_DOWN &&
            event.key.key == SDLK_F1 &&
            !event.key.repeat &&
            (SDL_GetModState() & SDL_KMOD_SHIFT) != 0) {
        toggleDebugPalette();
        return true;
    }
    if (handleMainLayoutPointerEvent(event)) {
        return true;
    }

    const float scaleX = windowWidth_ > 0 ? static_cast<float>(framebufferWidth_) / static_cast<float>(windowWidth_) : 1.0f;
    const float scaleY = windowHeight_ > 0 ? static_cast<float>(framebufferHeight_) / static_cast<float>(windowHeight_) : 1.0f;
    const bool rmlConsumed = rmlHost_.processEvent(event, scaleX, scaleY);
    if (rmlConsumed && rmlConsumedEventBlocksViewport(event)) {
        return true;
    }
    if (viewportHost_.processEvent(event)) {
        return true;
    }
    return rmlConsumed;
}

void UISystem::beginFrame(const FrameTiming& frameTiming) {
    updateWindowMetrics();
    if (mainLayoutDirty_) {
        rebuildMainLayout();
    }
    viewportHost_.beginFrame(frameTiming);
}

void UISystem::draw(QuaderEditorApi& editor,
        SceneRenderBridge& renderBridge,
        SceneDocument& document,
        AssetRegistry& assets,
        ModelingSessionService& modeling,
        EditorSelectionService& selection,
        AssetId defaultMaterial,
        bool& quitRequested) {
    (void)renderBridge;
    (void)modeling;
    (void)selection;
    (void)defaultMaterial;
    (void)quitRequested;

    viewportHost_.handleFrame(editor, document, assets, debugPaletteVisible_);
}

void UISystem::prepareLayout(std::uint32_t framebufferWidth, std::uint32_t framebufferHeight) {
    framebufferWidth_ = std::max(1u, framebufferWidth);
    framebufferHeight_ = std::max(1u, framebufferHeight);
    viewportHost_.resize(framebufferWidth_, framebufferHeight_);
    if (mainLayoutDirty_) {
        rebuildMainLayout();
    }
    rmlHost_.updateLayout(framebufferWidth_, framebufferHeight_);
    updateMainViewportFrameFromRml();
    rebuildMainLayoutHitRegions();
    if (normalizeMainLayoutForMeasuredBounds()) {
        rebuildMainLayout();
        rmlHost_.updateLayout(framebufferWidth_, framebufferHeight_);
        updateMainViewportFrameFromRml();
        rebuildMainLayoutHitRegions();
    }
}

void UISystem::prepareRenderData(std::uint32_t framebufferWidth, std::uint32_t framebufferHeight) {
    framebufferWidth_ = std::max(1u, framebufferWidth);
    framebufferHeight_ = std::max(1u, framebufferHeight);
    rmlHost_.clearTextureSources();
    rmlHost_.registerTextureSource(
            viewportHost_.textureSource(),
            viewportHost_.textureHandle(),
            viewportHost_.textureWidth(),
            viewportHost_.textureHeight());
    rmlHost_.prepareRenderData(framebufferWidth_, framebufferHeight_);
}

void UISystem::prepareViewports(
        QuaderEditorApi& editor,
        SceneRenderBridge& renderBridge,
        const SceneDocument& document,
        const AssetRegistry& assets) {
    viewportHost_.prepareViewports(editor, renderBridge, document, assets);
}

void UISystem::renderViewports(RenderSystem& renderSystem) {
    viewportHost_.renderViewports(renderSystem);
}

void UISystem::renderWindowedViewports(RenderSystem& renderSystem) {
    viewportHost_.renderWindowedViewports(renderSystem);
}

void UISystem::render(RenderSystem& renderSystem) {
    rmlHost_.render(renderSystem);
}

void UISystem::installRuntimeHooks(QuaderEditorApi& editor) {
    editor.ui().setRuntimeHooks(QuaderUIRuntimeHooks{
            .isPanelVisible = [this](std::string_view panel) {
                return isMainLayoutPanelVisible(panel);
            },
            .setPanelVisible = [this](std::string_view panel, bool visible) {
                if (mainLayoutPanelIdIsViewport(panel)) {
                    return runtimeStatus(false, visible ? "Viewport is always visible." : "Viewport cannot be hidden.");
                }
                const bool changed = setMainLayoutPanelVisible(panel, visible);
                if (!mainLayoutPanelIdIsKnown(panel)) {
                    return runtimeFailure("Panel is not registered in the main layout.");
                }
                return runtimeStatus(changed);
            },
            .focusPanel = [this](std::string_view panel) {
                if (!isMainLayoutPanelVisible(panel)) {
                    return runtimeFailure("Panel is not visible in the main layout.");
                }
                return runtimeStatus(false);
            },
            .notify = [](QuaderNotification) {
                return runtimeStatus(false);
            },
    });
    editor.preferences().setRuntimeHooks(QuaderPreferencesRuntimeHooks{
            .locale = [this]() {
                return localization_.currentLocale();
            },
            .setLocale = [](std::string) {
                return runtimeFailure("Locale selection is not exposed in the RmlUi app shell yet.");
            },
            .save = [this]() {
                saveUISettings();
                saveViewportSettings();
                return runtimeStatus(false, "Preferences saved.");
            },
            .navigationGizmoVisible = []() {
                return false;
            },
            .setNavigationGizmoVisible = [](bool) {
                return runtimeFailure("Navigation gizmo is not available in the RmlUi app shell yet.");
            },
    });
}

void UISystem::updateWindowMetrics() {
    if (!window_) {
        return;
    }

    int framebufferW = 1;
    int framebufferH = 1;
    SDL_GetWindowSizeInPixels(window_, &framebufferW, &framebufferH);
    framebufferWidth_ = static_cast<std::uint32_t>(std::max(1, framebufferW));
    framebufferHeight_ = static_cast<std::uint32_t>(std::max(1, framebufferH));
    SDL_GetWindowSize(window_, &windowWidth_, &windowHeight_);
    windowWidth_ = std::max(1, windowWidth_);
    windowHeight_ = std::max(1, windowHeight_);
    updateRmlDensityScale();
}

void UISystem::updateRmlDensityScale() {
    if (!window_) {
        return;
    }

    const float displayScale = SDL_GetWindowDisplayScale(window_);
    if (!std::isfinite(displayScale) || displayScale <= 0.0f) {
        return;
    }

    const float dpRatio = std::clamp(displayScale, 0.75f, 4.0f);
    if (std::abs(dpRatio - rmlDpRatio_) <= 0.001f) {
        return;
    }

    rmlDpRatio_ = dpRatio;
    rmlHost_.setDensityIndependentPixelRatio(rmlDpRatio_);
}

void UISystem::saveViewportSettings() {
    if (!settingsPaths_.viewportSettingsFile.empty() &&
            !viewportSettings_.save(settingsPaths_.viewportSettingsFile)) {
        LOG_WARN("Unable to save viewport settings to {}", settingsPaths_.viewportSettingsFile.string());
    }
}

void UISystem::saveUISettings() {
    if (settingsPaths_.settingsFile.empty()) {
        return;
    }

    uiSettings_.setLocale(localization_.currentLocale());
    uiSettings_.setMainLayout(mainLayout_);
    if (!uiSettings_.save(settingsPaths_.settingsFile)) {
        LOG_WARN("Unable to save UI settings to {}", settingsPaths_.settingsFile.string());
    }
}

void UISystem::loadLocalization(const std::filesystem::path& executableDir) {
    const std::filesystem::path localesDirectory = executableDir / "locales";
    if (!localization_.load(localesDirectory, uiSettings_.locale())) {
        LOG_WARN("Unable to load localization catalogs from {}", localesDirectory.string());
    }
    uiSettings_.setLocale(localization_.currentLocale());
}

void UISystem::applyLocalizedMainLayoutLabels() {
    mainLayoutLabels_.viewport = localization_.text("ui.panel.viewport", "Viewport");
    mainLayoutLabels_.outliner = localization_.text("ui.panel.outliner", "Outliner");
    mainLayoutLabels_.inspector = localization_.text("ui.panel.inspector", "Inspector");
}

void UISystem::rebuildMainLayout() {
    MainLayoutInteractionState interaction;
    if (mainLayoutResizeController_.activeHandleId() == kMainLayoutSidebarResizer) {
        interaction.activeResizer = MainLayoutActiveResizer::Sidebar;
    } else if (mainLayoutResizeController_.activeHandleId() == kMainLayoutPanelResizer) {
        interaction.activeResizer = MainLayoutActiveResizer::Panels;
    }

    rmlHost_.setElementInnerRml("toolbar-label", escapeRmlText(localization_.text("ui.shell.toolbar", "Toolbar")));
    rmlHost_.setElementInnerRml("statusbar-label", escapeRmlText(localization_.text("ui.shell.status", "Status")));
    rmlHost_.setElementInnerRml("main-layout-slot", buildMainLayoutRml(mainLayout_, mainLayoutLabels_, interaction));
    mainLayoutDirty_ = false;
}

void UISystem::rebuildMainLayoutHitRegions() {
    mainLayoutResizeController_.clearHitRegions();
    mainLayoutWidthDp_ = 0;
    mainLayoutSidebarHeightDp_ = 0;
    if (!rmlHost_.initialized()) {
        return;
    }

    std::vector<ResizableSplitHitRegion> regions;
    const int hitThicknessPx = std::max(
            8,
            static_cast<int>(std::lround(
                    static_cast<float>(kResizableSplitDefaultHitThicknessDp) * rmlDpRatio_)));
    if (const std::optional<RmlElementRect> rect = rmlHost_.measureElementById("main-layout-slot")) {
        mainLayoutWidthDp_ = static_cast<int>(
                std::lround(static_cast<float>(std::max(0, rect->width)) / std::max(0.001f, rmlDpRatio_)));
    }
    if (const std::optional<RmlElementRect> rect = rmlHost_.measureElementById("main-layout-sidebar")) {
        mainLayoutSidebarHeightDp_ = static_cast<int>(
                std::lround(static_cast<float>(std::max(0, rect->height)) / std::max(0.001f, rmlDpRatio_)));
    }
    if (const std::optional<RmlElementRect> rect = rmlHost_.measureElementById("main-layout-sidebar-resizer")) {
        regions.push_back({
                .id = std::string(kMainLayoutSidebarResizer),
                .axis = ResizableSplitAxis::Horizontal,
                .rect = ResizableSplitController::inflatedHitRect(
                        UIPixelRect{
                                .left = rect->left,
                                .top = rect->top,
                                .width = rect->width,
                                .height = rect->height,
                        },
                        ResizableSplitAxis::Horizontal,
                        hitThicknessPx),
        });
    }
    if (const std::optional<RmlElementRect> rect = rmlHost_.measureElementById("main-layout-panel-resizer")) {
        regions.push_back({
                .id = std::string(kMainLayoutPanelResizer),
                .axis = ResizableSplitAxis::Vertical,
                .rect = ResizableSplitController::inflatedHitRect(
                        UIPixelRect{
                                .left = rect->left,
                                .top = rect->top,
                                .width = rect->width,
                                .height = rect->height,
                        },
                        ResizableSplitAxis::Vertical,
                        hitThicknessPx),
        });
    }
    mainLayoutResizeController_.setHitRegions(std::move(regions));
}

bool UISystem::handleMainLayoutPointerEvent(const SDL_Event& event) {
    if (debugPaletteVisible_) {
        cancelMainLayoutResize();
        setMainLayoutCursor(MainLayoutCursorKind::Default);
        return false;
    }

    if (mainLayoutResizeController_.active() && shouldCancelMainLayoutResize(event)) {
        cancelMainLayoutResize();
        return true;
    }

    const std::optional<std::pair<float, float>> pointer = mainLayoutPointerFramebufferPosition(event);
    if (!pointer) {
        if (event.type == SDL_EVENT_WINDOW_MOUSE_LEAVE) {
            setMainLayoutCursor(MainLayoutCursorKind::Default);
        }
        return false;
    }

    const float mouseX = pointer->first;
    const float mouseY = pointer->second;
    if (mainLayoutResizeController_.active()) {
        if (event.type == SDL_EVENT_MOUSE_MOTION) {
            if ((event.motion.state & SDL_BUTTON_LMASK) == 0u) {
                cancelMainLayoutResize();
                return true;
            }
            updateMainLayoutResize(mouseX, mouseY);
            return true;
        }
        if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT) {
            updateMainLayoutResize(mouseX, mouseY);
            endMainLayoutResize(mouseX, mouseY);
            return true;
        }
        return event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP;
    }

    if (event.type == SDL_EVENT_MOUSE_MOTION) {
        updateMainLayoutCursor(mouseX, mouseY);
        return false;
    }

    if (event.type != SDL_EVENT_MOUSE_BUTTON_DOWN || event.button.button != SDL_BUTTON_LEFT) {
        return false;
    }

    if (const std::optional<ResizableSplitHitRegion> region = mainLayoutResizeController_.hitTest(mouseX, mouseY)) {
        beginMainLayoutResize(region->id, mouseX, mouseY);
        return true;
    }

    return false;
}

std::optional<std::pair<float, float>> UISystem::mainLayoutPointerFramebufferPosition(const SDL_Event& event) const {
    if (!window_) {
        return std::nullopt;
    }

    const SDL_WindowID windowId = SDL_GetWindowID(window_);
    float windowX = 0.0f;
    float windowY = 0.0f;
    switch (event.type) {
    case SDL_EVENT_MOUSE_MOTION:
        if (event.motion.windowID != windowId) {
            return std::nullopt;
        }
        windowX = event.motion.x;
        windowY = event.motion.y;
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event.button.windowID != windowId) {
            return std::nullopt;
        }
        windowX = event.button.x;
        windowY = event.button.y;
        break;
    default:
        return std::nullopt;
    }

    const float scaleX = windowWidth_ > 0 ? static_cast<float>(framebufferWidth_) / static_cast<float>(windowWidth_) : 1.0f;
    const float scaleY = windowHeight_ > 0 ? static_cast<float>(framebufferHeight_) / static_cast<float>(windowHeight_) : 1.0f;
    return std::make_pair(windowX * scaleX, windowY * scaleY);
}

std::optional<ResizableSplitHandle> UISystem::mainLayoutSplitHandle(std::string_view handleId) const {
    if (handleId == kMainLayoutSidebarResizer) {
        return ResizableSplitHandle{
                .id = std::string(kMainLayoutSidebarResizer),
                .axis = ResizableSplitAxis::Horizontal,
                .growth = mainLayout_.sidebarSide == MainLayoutSidebarSide::Right ?
                        ResizableSplitGrowthDirection::Negative :
                        ResizableSplitGrowthDirection::Positive,
                .sizeDp = mainLayout_.sidebarCollapsed ? 0 : mainLayout_.sidebarWidthDp,
                .minSize = ResizableSplitSizeRule::dp(kMainLayoutSidebarMinWidthDp),
                .oppositeMinSize = ResizableSplitSizeRule::dp(kMainLayoutViewportMinWidthDp),
                .containerSizeDp = mainLayoutWidthDp_,
                .collapse = {
                        .enabled = true,
                        .collapsedSizeDp = 0,
                },
        };
    }
    if (handleId == kMainLayoutPanelResizer) {
        return ResizableSplitHandle{
                .id = std::string(kMainLayoutPanelResizer),
                .axis = ResizableSplitAxis::Vertical,
                .growth = ResizableSplitGrowthDirection::Positive,
                .sizeDp = mainLayout_.outlinerHeightDp,
                .minSize = ResizableSplitSizeRule::dp(kMainLayoutPanelMinHeightDp),
                .maxSize = ResizableSplitSizeRule::dp(kMainLayoutPanelMaxHeightDp),
                .oppositeMinSize = ResizableSplitSizeRule::dp(kMainLayoutPanelMinHeightDp),
                .containerSizeDp = mainLayoutSidebarHeightDp_,
        };
    }
    return std::nullopt;
}

void UISystem::beginMainLayoutResize(std::string_view handleId, float mouseX, float mouseY) {
    const std::optional<ResizableSplitHandle> handle = mainLayoutSplitHandle(handleId);
    if (!handle) {
        return;
    }

    mainLayoutResizeController_.beginDrag(*handle, mouseX, mouseY, rmlDpRatio_);
    if (!SDL_CaptureMouse(true)) {
        LOG_WARN("Unable to capture mouse for main-layout resize: {}", SDL_GetError());
    } else {
        mainLayoutMouseCaptured_ = true;
    }
    viewportHost_.setCursorCaptured(false);
    setMainLayoutCursor(mainLayoutCursorForAxis(handle->axis));
    mainLayoutDirty_ = true;
}

void UISystem::updateMainLayoutResize(float mouseX, float mouseY) {
    const std::optional<ResizableSplitResizeResult> resize =
            mainLayoutResizeController_.updateDrag(mouseX, mouseY, rmlDpRatio_);
    if (!resize) {
        return;
    }

    if (resize->id == kMainLayoutSidebarResizer) {
        const int restoredSidebarWidthDp = mainLayout_.sidebarCollapsed ?
                std::max(resize->sizeDp, mainLayout_.sidebarWidthDp) :
                resize->sizeDp;
        const bool changed = resize->collapsed ?
                mainLayoutController_.setSidebarCollapsed(true) :
                mainLayoutController_.resizeSidebar(restoredSidebarWidthDp);
        if (changed) {
            mainLayoutDirty_ = true;
        }
        return;
    }

    if (resize->id == kMainLayoutPanelResizer && mainLayoutController_.resizeOutlinerPanel(resize->sizeDp)) {
        mainLayoutDirty_ = true;
    }
}

void UISystem::endMainLayoutResize(float mouseX, float mouseY) {
    if (!mainLayoutResizeController_.active()) {
        return;
    }

    mainLayoutResizeController_.endDrag();
    releaseMainLayoutMouseCapture();
    mainLayoutDirty_ = true;
    updateMainLayoutCursor(mouseX, mouseY);
}

void UISystem::cancelMainLayoutResize() {
    if (!mainLayoutResizeController_.active()) {
        releaseMainLayoutMouseCapture();
        return;
    }

    mainLayoutResizeController_.endDrag();
    releaseMainLayoutMouseCapture();
    mainLayoutDirty_ = true;
    setMainLayoutCursor(MainLayoutCursorKind::Default);
}

void UISystem::releaseMainLayoutMouseCapture() {
    if (!mainLayoutMouseCaptured_) {
        return;
    }

    if (!SDL_CaptureMouse(false)) {
        LOG_WARN("Unable to release mouse capture after main-layout resize: {}", SDL_GetError());
    }
    mainLayoutMouseCaptured_ = false;
}

bool UISystem::shouldCancelMainLayoutResize(const SDL_Event& event) const {
    const std::optional<SDL_WindowID> targetWindow = eventWindowId(event);
    if (targetWindow && window_ && *targetWindow != SDL_GetWindowID(window_)) {
        return false;
    }

    return event.type == SDL_EVENT_WINDOW_MOUSE_LEAVE ||
            event.type == SDL_EVENT_WINDOW_FOCUS_LOST ||
            event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED;
}

void UISystem::initializeMainLayoutCursors() {
    mainLayoutHorizontalResizeCursor_ = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_EW_RESIZE);
    mainLayoutVerticalResizeCursor_ = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NS_RESIZE);
}

void UISystem::shutdownMainLayoutCursors() {
    setMainLayoutCursor(MainLayoutCursorKind::Default);
    if (mainLayoutHorizontalResizeCursor_) {
        SDL_DestroyCursor(mainLayoutHorizontalResizeCursor_);
        mainLayoutHorizontalResizeCursor_ = nullptr;
    }
    if (mainLayoutVerticalResizeCursor_) {
        SDL_DestroyCursor(mainLayoutVerticalResizeCursor_);
        mainLayoutVerticalResizeCursor_ = nullptr;
    }
}

void UISystem::updateMainLayoutCursor(float mouseX, float mouseY) {
    const std::optional<ResizableSplitAxis> axis = mainLayoutResizeController_.axisAt(mouseX, mouseY);
    if (!axis) {
        setMainLayoutCursor(MainLayoutCursorKind::Default);
        return;
    }

    setMainLayoutCursor(mainLayoutCursorForAxis(*axis));
}

void UISystem::setMainLayoutCursor(MainLayoutCursorKind kind) {
    if (activeMainLayoutCursor_ == kind) {
        return;
    }

    SDL_Cursor* cursor = SDL_GetDefaultCursor();
    if (kind == MainLayoutCursorKind::ResizeHorizontal && mainLayoutHorizontalResizeCursor_) {
        cursor = mainLayoutHorizontalResizeCursor_;
    } else if (kind == MainLayoutCursorKind::ResizeVertical && mainLayoutVerticalResizeCursor_) {
        cursor = mainLayoutVerticalResizeCursor_;
    }
    if (cursor) {
        SDL_SetCursor(cursor);
    }
    activeMainLayoutCursor_ = kind;
}

UISystem::MainLayoutCursorKind UISystem::mainLayoutCursorForAxis(ResizableSplitAxis axis) const {
    return axis == ResizableSplitAxis::Horizontal ?
            MainLayoutCursorKind::ResizeHorizontal :
            MainLayoutCursorKind::ResizeVertical;
}

void UISystem::updateMainViewportFrameFromRml() {
    const std::optional<RmlElementRect> rect = rmlHost_.measureElementById("main-viewport");
    if (!rect) {
        mainViewportFrame_ = std::nullopt;
        viewportHost_.setMainViewportFrame(0, 0, framebufferWidth_, framebufferHeight_);
        return;
    }

    mainViewportFrame_ = *rect;
    viewportHost_.setMainViewportFrame(
            std::max(0, rect->left),
            std::max(0, rect->top),
            static_cast<std::uint32_t>(std::max(1, rect->width)),
            static_cast<std::uint32_t>(std::max(1, rect->height)));
}

bool UISystem::normalizeMainLayoutForMeasuredBounds() {
    bool changed = false;
    if (mainLayoutWidthDp_ > 0 && !mainLayout_.sidebarCollapsed) {
        const int measuredMaxSidebarWidthDp = std::max(
                mainLayoutWidthDp_ - kMainLayoutViewportMinWidthDp,
                kMainLayoutSidebarMinWidthDp);
        if (mainLayout_.sidebarWidthDp > measuredMaxSidebarWidthDp) {
            mainLayout_.sidebarWidthDp = measuredMaxSidebarWidthDp;
            changed = true;
        }
    }

    if (mainLayoutSidebarHeightDp_ > 0 && mainLayoutVisibleSidebarDockPanelCount(mainLayout_) > 1) {
        const int measuredMaxOutlinerHeightDp = std::clamp(
                mainLayoutSidebarHeightDp_ - kMainLayoutPanelMinHeightDp - kResizableSplitDefaultHitThicknessDp,
                kMainLayoutPanelMinHeightDp,
                kMainLayoutPanelMaxHeightDp);
        if (mainLayout_.outlinerHeightDp > measuredMaxOutlinerHeightDp) {
            mainLayout_.outlinerHeightDp = measuredMaxOutlinerHeightDp;
            changed = true;
        }
    }

    mainLayoutDirty_ = mainLayoutDirty_ || changed;
    return changed;
}

bool UISystem::rmlConsumedEventBlocksViewport(const SDL_Event& event) const {
    switch (event.type) {
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_WHEEL:
        return !pointerEventInsideMainViewport(event);
    default:
        return false;
    }
}

bool UISystem::pointerEventInsideMainViewport(const SDL_Event& event) const {
    if (!mainViewportFrame_) {
        return true;
    }

    float windowX = 0.0f;
    float windowY = 0.0f;
    switch (event.type) {
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        windowX = event.button.x;
        windowY = event.button.y;
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        windowX = event.wheel.mouse_x;
        windowY = event.wheel.mouse_y;
        break;
    default:
        return true;
    }

    const float scaleX = windowWidth_ > 0 ? static_cast<float>(framebufferWidth_) / static_cast<float>(windowWidth_) : 1.0f;
    const float scaleY = windowHeight_ > 0 ? static_cast<float>(framebufferHeight_) / static_cast<float>(windowHeight_) : 1.0f;
    const float x = windowX * scaleX;
    const float y = windowY * scaleY;
    return x >= static_cast<float>(mainViewportFrame_->left) &&
            y >= static_cast<float>(mainViewportFrame_->top) &&
            x < static_cast<float>(mainViewportFrame_->left + mainViewportFrame_->width) &&
            y < static_cast<float>(mainViewportFrame_->top + mainViewportFrame_->height);
}

bool UISystem::isMainLayoutPanelVisible(std::string_view panel) const {
    if (mainLayoutPanelIdIsViewport(panel)) {
        return true;
    }
    return mainLayoutController_.isPanelVisible(panel);
}

bool UISystem::setMainLayoutPanelVisible(std::string_view panel, bool visible) {
    const bool changed = mainLayoutController_.setPanelVisible(panel, visible);
    mainLayoutDirty_ = mainLayoutDirty_ || changed;
    return changed;
}

void UISystem::toggleDebugPalette() {
    debugPaletteVisible_ = !debugPaletteVisible_;
    cancelMainLayoutResize();
    applyDebugPaletteVisibility();
    if (debugPaletteVisible_) {
        viewportHost_.setCursorCaptured(false);
    }
}

void UISystem::applyDebugPaletteVisibility() {
    rmlHost_.setElementDisplay("viewport-panel", debugPaletteVisible_ ? "none" : "flex");
    rmlHost_.setElementDisplay("debug-palette-page", debugPaletteVisible_ ? "block" : "none");
}

} // namespace quader
