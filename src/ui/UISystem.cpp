////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/UISystem.h"

#include "api/QuaderApiTypeConversions.h"
#include "api/QuaderEditorApi.h"
#include "core/Logger.h"
#include "platform/SdlEventUtils.h"
#include "ui/DebugPaletteView.h"
#include "ui/UIUtilityTokens.h"
#include "ui/backends/rml/RmlMarkupWriter.h"
#include "ui/components/organisms/ModelingModeShelfComponent.h"
#include "ui/components/organisms/WindowChromeControlsComponent.h"
#include "ui/controllers/MainLayoutDockHitRegionBuilder.h"
#include "ui/core/UIIconRegistry.h"

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

constexpr float kMainLayoutDockDragPreviewHotspotOffsetPx = 10.0f;

bool stringListContains(const std::vector<std::string>& values, std::string_view value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

QuaderApiVoidResult uiRuntimeResult(bool ok, bool changed, std::string message) {
    return {
            .ok = ok,
            .changed = changed,
            .message = std::move(message),
            .mutationKind = ok && changed ? QuaderMutationKind::Runtime : QuaderMutationKind::None,
    };
}

QuaderApiVoidResult uiRuntimeStatus(bool changed, std::string message = {}) {
    return uiRuntimeResult(true, changed, std::move(message));
}

QuaderApiVoidResult uiRuntimeFailure(std::string message) {
    return uiRuntimeResult(false, false, std::move(message));
}

std::string uiRuntimeMessage(
        const LocalizationService& localization,
        std::string_view key,
        std::string_view fallback) {
    return localization.text(key, fallback);
}

QuaderApiVoidResult uiRuntimeStatus(
        const LocalizationService& localization,
        bool changed,
        std::string_view key,
        std::string_view fallback) {
    return uiRuntimeStatus(changed, uiRuntimeMessage(localization, key, fallback));
}

QuaderApiVoidResult uiRuntimeFailure(
        const LocalizationService& localization,
        std::string_view key,
        std::string_view fallback) {
    return uiRuntimeFailure(uiRuntimeMessage(localization, key, fallback));
}

bool mainLayoutDockDropTargetsEqual(
        const std::optional<MainLayoutDockDropTarget>& lhs,
        const std::optional<MainLayoutDockDropTarget>& rhs) {
    if (lhs.has_value() != rhs.has_value()) {
        return false;
    }
    if (!lhs) {
        return true;
    }
    return lhs->location == rhs->location &&
            lhs->targetPanelId == rhs->targetPanelId &&
            lhs->position == rhs->position &&
            lhs->action == rhs->action &&
            lhs->feedbackKind == rhs->feedbackKind;
}

UIPixelRect uiPixelRectFromRmlRect(const RmlElementRect& rect) {
    return {
            .left = rect.left,
            .top = rect.top,
            .width = rect.width,
            .height = rect.height,
    };
}

UIPixelRect bottomSplitResizerHitRectFromPane(
        const RmlElementRect& paneRect,
        const RmlElementRect& contentRect) {
    return {
            .left = paneRect.left + std::max(0, paneRect.width) - 1,
            .top = contentRect.top,
            .width = 1,
            .height = contentRect.height,
    };
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
    viewportHost_.setWindowedViewportTitleFormat(
            localization_.text("ui.viewport.window.title", "Quader Viewport {index}"));
    applyLocalizedMainLayoutLabels();
    viewportSettings_ = ViewportSettings::load(settingsPaths_.viewportSettingsFile);

    viewportHost_.initialize(window_, renderSystem, viewportSettings_);
    initializeMainLayoutCursors();

    const UITokenConfig uiTokens = loadUITokenConfig(appUITokenConfigPath(executableDir));
    const std::string utilityRcss = buildUIUtilityRcss(uiTokens.utilities, uiTokens.colors);
    const std::string iconRcss = UIIconRegistry::instance().buildRcss();
    const std::string debugPaletteRml = buildDebugPalettePageRml(uiTokens.colors);

    if (!rmlHost_.initialize(window_,
                renderSystem,
                executableDir,
                utilityRcss,
                iconRcss,
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
    cancelMainLayoutDockDrag();
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
    refreshMeasuredViewportFrameForPointerEvent(event);
    if (event.type == SDL_EVENT_KEY_DOWN &&
            event.key.key == SDLK_F1 &&
            !event.key.repeat &&
            (SDL_GetModState() & SDL_KMOD_SHIFT) != 0) {
        toggleDebugPalette();
        return true;
    }
    const float scaleX = windowWidth_ > 0 ? static_cast<float>(framebufferWidth_) / static_cast<float>(windowWidth_) : 1.0f;
    const float scaleY = windowHeight_ > 0 ? static_cast<float>(framebufferHeight_) / static_cast<float>(windowHeight_) : 1.0f;
    if (rmlHost_.debuggerVisible()) {
        const bool rmlConsumed = rmlHost_.processEvent(event, scaleX, scaleY);
        return rmlConsumed || rmlConsumedEventBlocksViewport(event);
    }

    if (handleMainLayoutPointerEvent(event)) {
        return true;
    }

    const bool rmlConsumed = rmlHost_.processEvent(event, scaleX, scaleY);
    if (const std::optional<std::string> selectedControl = rmlHost_.takeModelingModeShelfControlSelection()) {
        activateModelingModeShelfControl(*selectedControl);
        return true;
    }
    if (const std::optional<std::string> selectedCommand = rmlHost_.takeModelingModeShelfCommand()) {
        modelingModeShelfController_.activateCommand(*selectedCommand);
        rebuildModelingModeShelf();
        return true;
    }
    if (pointerEventShouldBypassViewport(event)) {
        return true;
    }
    if (rmlConsumed && rmlConsumedEventBlocksViewport(event)) {
        return true;
    }
    if (viewportHost_.processEvent(event)) {
        return true;
    }
    return rmlConsumed;
}

bool UISystem::shouldBypassPlatformChrome(const SDL_Event& event) const {
    if (!window_ || !rmlHost_.debuggerVisible()) {
        return false;
    }

    const SDL_WindowID windowId = SDL_GetWindowID(window_);
    switch (event.type) {
    case SDL_EVENT_MOUSE_MOTION:
        return sdlEventTargetsWindow(event, windowId);
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        return sdlEventTargetsWindow(event, windowId);
    case SDL_EVENT_MOUSE_WHEEL:
        return sdlEventTargetsWindow(event, windowId);
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        return sdlEventTargetsWindow(event, windowId);
    default:
        return false;
    }
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
    (void)selection;
    (void)quitRequested;

    applyPendingModelingModeShelfIntents(editor);
    if (modelingModeShelfController_.syncEditorState(editor, localization_)) {
        rebuildModelingModeShelf();
    }
    viewportHost_.setTextInputActive(rmlHost_.textInputActive());
    viewportHost_.handleFrame(editor, document, assets, modeling, defaultMaterial, debugPaletteVisible_);
}

void UISystem::prepareLayout(std::uint32_t framebufferWidth, std::uint32_t framebufferHeight) {
    framebufferWidth_ = std::max(1u, framebufferWidth);
    framebufferHeight_ = std::max(1u, framebufferHeight);
    viewportHost_.resize(framebufferWidth_, framebufferHeight_);
    if (mainLayoutDirty_) {
        rebuildMainLayout();
    }
    auto updateRmlLayoutAndShelfOverflow = [&]() {
        rmlHost_.updateLayout(framebufferWidth_, framebufferHeight_);
        if (updateModelingModeShelfOverflowFromRml()) {
            rmlHost_.updateLayout(framebufferWidth_, framebufferHeight_);
        }
    };

    updateRmlLayoutAndShelfOverflow();
    updateMainViewportFrameFromRml();
    rebuildMainLayoutHitRegions();
    if (normalizeMainLayoutForMeasuredBounds()) {
        rebuildMainLayout();
        updateRmlLayoutAndShelfOverflow();
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
                    return visible ?
                            uiRuntimeStatus(
                                    localization_,
                                    false,
                                    "ui.runtime.viewport_always_visible",
                                    "Viewport is always visible.") :
                            uiRuntimeStatus(
                                    localization_,
                                    false,
                                    "ui.runtime.viewport_cannot_hide",
                                    "Viewport cannot be hidden.");
                }
                const bool changed = setMainLayoutPanelVisible(panel, visible);
                if (!mainLayoutPanelIdIsKnown(panel)) {
                    return uiRuntimeFailure(
                            localization_,
                            "ui.runtime.panel_not_registered",
                            "Panel is not registered in the main layout.");
                }
                return uiRuntimeStatus(changed);
            },
            .focusPanel = [this](std::string_view panel) {
                if (!isMainLayoutPanelVisible(panel)) {
                    return uiRuntimeFailure(
                            localization_,
                            "ui.runtime.panel_not_visible",
                            "Panel is not visible in the main layout.");
                }
                return uiRuntimeStatus(false);
            },
            .notify = [](QuaderNotification) {
                return uiRuntimeStatus(false);
            },
    });
    editor.preferences().setRuntimeHooks(QuaderPreferencesRuntimeHooks{
            .locale = [this]() {
                return localization_.currentLocale();
            },
            .setLocale = [this](std::string) {
                return uiRuntimeFailure(
                        localization_,
                        "ui.runtime.locale_selection_unavailable",
                        "Locale selection is not exposed in the RmlUi app shell yet.");
            },
            .save = [this]() {
                saveUISettings();
                saveViewportSettings();
                return uiRuntimeStatus(
                        localization_,
                        false,
                        "ui.runtime.preferences_saved",
                        "Preferences saved.");
            },
            .navigationGizmoVisible = []() {
                return false;
            },
            .setNavigationGizmoVisible = [this](bool) {
                return uiRuntimeFailure(
                        localization_,
                        "ui.runtime.navigation_gizmo_unavailable",
                        "Navigation gizmo is not available in the RmlUi app shell yet.");
            },
    });
}

std::optional<WindowChromeAction> UISystem::takeWindowChromeAction() {
    return rmlHost_.takeWindowChromeAction();
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

void UISystem::refreshMeasuredViewportFrameForPointerEvent(const SDL_Event& event) {
    switch (event.type) {
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_WHEEL:
        break;
    default:
        return;
    }

    if (!rmlHost_.initialized()) {
        return;
    }

    if (mainLayoutDirty_) {
        rebuildMainLayout();
    }
    rmlHost_.updateLayout(framebufferWidth_, framebufferHeight_);
    if (updateModelingModeShelfOverflowFromRml()) {
        rmlHost_.updateLayout(framebufferWidth_, framebufferHeight_);
    }
    updateMainViewportFrameFromRml();
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
    MainLayoutLabels labels;
    labels.panels.reserve(mainLayoutPanelDescriptors().size());
    for (const MainLayoutPanelDescriptor& descriptor : mainLayoutPanelDescriptors()) {
        labels.panels.push_back({
                .panelId = std::string(descriptor.id),
                .label = localization_.text(descriptor.localeKey, descriptor.fallbackLabel),
        });
    }
    mainLayoutLabels_ = std::move(labels);
}

void UISystem::rebuildMainLayout() {
    MainLayoutInteractionState interaction;
    if (const std::optional<MainLayoutSashRegistry::ActiveSash> activeSash =
                mainLayoutSashRegistry_.activeSash(mainLayoutResizeController_.activeHandleId())) {
        switch (activeSash->kind) {
        case MainLayoutSashRegistry::SashKind::SideDockWidth:
            interaction.activeResizer = activeSash->location == DockLocation::AuxiliaryBar ?
                    MainLayoutActiveResizer::AuxiliarySidebar :
                    MainLayoutActiveResizer::Sidebar;
            break;
        case MainLayoutSashRegistry::SashKind::SidePanelStack:
            interaction.activeResizer = MainLayoutActiveResizer::Panels;
            break;
        case MainLayoutSashRegistry::SashKind::BottomDockHeight:
            interaction.activeResizer = MainLayoutActiveResizer::BottomDock;
            break;
        case MainLayoutSashRegistry::SashKind::BottomPanelSplit:
            interaction.activeResizer = MainLayoutActiveResizer::BottomSplitPanel;
            interaction.activeBottomSplitPanelId = activeSash->panelId;
            break;
        }
    }
    if (const std::optional<MainLayoutDockDropTarget> target = mainLayoutDockDragController_.target()) {
        if (target->targetPanelId.empty()) {
            interaction.dockDropLocation = target->location;
        } else {
            interaction.panelDrop = MainLayoutPanelDropPresentation{
                    .location = target->location,
                    .panelId = target->targetPanelId,
                    .position = target->position,
                    .feedbackKind = target->feedbackKind,
            };
        }
    }
    if (mainLayoutDockDragController_.active() && !mainLayoutDockDragController_.activePanelId().empty()) {
        interaction.dockDrag = MainLayoutDockDragPresentation{
                .panelId = std::string(mainLayoutDockDragController_.activePanelId()),
                .previewLeftDp = mainLayoutDockDragPreviewLeftDp_,
                .previewTopDp = mainLayoutDockDragPreviewTopDp_,
                .previewWidthDp = mainLayoutDockDragPreviewWidthDp_,
                .previewHeightDp = mainLayoutDockDragPreviewHeightDp_,
                .dragging = mainLayoutDockDragController_.dragging(),
        };
    }

    rmlHost_.setElementInnerRml("chrome-window-controls", buildWindowChromeControlsInnerRml());
    rmlHost_.setElementInnerRml("toolbar-label", escapeRmlText(localization_.text("ui.shell.toolbar", "Toolbar")));
    rmlHost_.setElementInnerRml("statusbar-label", escapeRmlText(localization_.text("ui.shell.status", "Status")));
    rmlHost_.setElementInnerRml("main-layout-slot", buildMainLayoutRml(mainLayout_, mainLayoutLabels_, interaction));
    rebuildModelingModeShelf();
    mainLayoutDirty_ = false;
}

void UISystem::rebuildModelingModeShelf() {
    rmlHost_.setElementInnerRml(
            mainLayoutModelingModeShelfSlotElementId(),
            buildModelingModeShelfRml(modelingModeShelfController_.buildState(localization_)));
    if (framebufferWidth_ > 0 && framebufferHeight_ > 0) {
        rmlHost_.updateLayout(framebufferWidth_, framebufferHeight_);
    }
}

bool UISystem::updateModelingModeShelfOverflowFromRml() {
    std::vector<ModelingModeShelfControl> controls = modelingModeShelfController_.controls(localization_);
    if (controls.empty()) {
        if (modelingModeShelfController_.setOverflowControlIds({})) {
            rebuildModelingModeShelf();
            return true;
        }
        return false;
    }

    const std::optional<RmlElementRect> toolbarRect = rmlHost_.measureElementById("modeling-mode-shelf-toolbar");
    const std::optional<RmlElementRect> moreRect =
            rmlHost_.measureElementById(std::string(modelingModeShelfOverflowMeasureElementId()));
    if (!toolbarRect || !moreRect) {
        return false;
    }

    /**
     * Stores one measured toolbar control and its layout advance, including
     * the horizontal gap before the next measured item.
     */
    struct ControlMeasurement {
        std::string id;
        int left = 0;
        int width = 0;
        int advanceWidth = 0;
    };

    std::vector<ControlMeasurement> measurements;
    measurements.reserve(controls.size());
    for (const ModelingModeShelfControl& control : controls) {
        const std::optional<RmlElementRect> rect =
                rmlHost_.measureElementById(modelingModeShelfControlMeasureElementId(control.id));
        if (!rect) {
            return false;
        }
        measurements.push_back(ControlMeasurement{
                .id = control.id,
                .left = rect->left,
                .width = rect->width,
                .advanceWidth = rect->width,
        });
    }

    for (std::size_t index = 0; index < measurements.size(); ++index) {
        const int nextLeft = index + 1u < measurements.size() ?
                measurements[index + 1u].left :
                moreRect->left;
        measurements[index].advanceWidth = std::max(
                measurements[index].width,
                nextLeft - measurements[index].left);
    }

    int leadingInset = 0;
    for (const ModelingModeShelfControl& control : controls) {
        if (stringListContains(modelingModeShelfController_.overflowControlIds(), control.id)) {
            continue;
        }
        if (const std::optional<RmlElementRect> rect =
                    rmlHost_.measureElementById(modelingModeShelfControlButtonElementId(control.id))) {
            leadingInset = std::max(0, rect->left - toolbarRect->left);
            break;
        }
    }
    if (leadingInset == 0) {
        if (const std::optional<RmlElementRect> rect =
                    rmlHost_.measureElementById(std::string(modelingModeShelfOverflowButtonElementId()))) {
            leadingInset = std::max(0, rect->left - toolbarRect->left);
        }
    }

    const int availableWidth = std::max(0, toolbarRect->width - leadingInset);
    std::vector<bool> visible(measurements.size(), true);
    const std::optional<std::string> activeTabId = modelingModeShelfController_.activeControlId();

    auto hiddenCount = [&]() {
        return static_cast<std::size_t>(std::count(visible.begin(), visible.end(), false));
    };
    auto totalVisibleWidth = [&]() {
        int width = 0;
        for (std::size_t index = 0; index < measurements.size(); ++index) {
            if (visible[index]) {
                width += measurements[index].advanceWidth;
            }
        }
        if (hiddenCount() > 0u) {
            width += moreRect->width;
        }
        return width;
    };
    auto nextHideCandidate = [&]() -> std::optional<std::size_t> {
        for (std::size_t reverseIndex = measurements.size(); reverseIndex > 0u; --reverseIndex) {
            const std::size_t index = reverseIndex - 1u;
            if (!visible[index]) {
                continue;
            }
            if (activeTabId && measurements[index].id == *activeTabId) {
                continue;
            }
            return index;
        }
        for (std::size_t reverseIndex = measurements.size(); reverseIndex > 0u; --reverseIndex) {
            const std::size_t index = reverseIndex - 1u;
            if (visible[index]) {
                return index;
            }
        }
        return std::nullopt;
    };

    while (totalVisibleWidth() > availableWidth) {
        const std::optional<std::size_t> index = nextHideCandidate();
        if (!index) {
            break;
        }
        visible[*index] = false;
    }

    std::vector<std::string> overflowControlIds;
    for (std::size_t index = 0; index < measurements.size(); ++index) {
        if (!visible[index]) {
            overflowControlIds.push_back(measurements[index].id);
        }
    }

    if (!modelingModeShelfController_.setOverflowControlIds(std::move(overflowControlIds))) {
        return false;
    }
    rebuildModelingModeShelf();
    return true;
}

bool UISystem::activateModelingModeShelfControl(std::string_view controlId) {
    if (!modelingModeShelfController_.activateControl(controlId)) {
        return false;
    }
    if (controlId != modelingModeShelfOverflowToggleControlId()) {
        return true;
    }
    rebuildModelingModeShelf();
    return true;
}

void UISystem::applyPendingModelingModeShelfIntents(QuaderEditorApi& editor) {
    if (const std::optional<ModelingSelectionMode> selectionMode =
                modelingModeShelfController_.takePendingSelectionMode()) {
        const QuaderApiVoidResult result = editor.selection().setMode(toApiSelectionMode(*selectionMode));
        if (!result.ok()) {
            LOG_WARN("Unable to apply modeling shelf selection mode: {}", result.errorMessage());
        }
    }
    if (const std::optional<std::string> commandId = modelingModeShelfController_.takePendingCommandId()) {
        const QuaderApiVoidResult result = editor.commands().execute(*commandId);
        if (!result.ok()) {
            LOG_WARN("Unable to execute modeling shelf command {}: {}", *commandId, result.errorMessage());
        }
    }
}

void UISystem::rebuildMainLayoutHitRegions() {
    mainLayoutResizeController_.clearHitRegions();
    mainLayoutDockDragController_.clearHitRegions();
    mainLayoutDockDragController_.clearDockDropZones();
    mainLayoutDockDragController_.clearPanelDropZones();
    mainLayoutLeftPx_ = 0;
    mainLayoutTopPx_ = 0;
    mainLayoutWidthDp_ = 0;
    mainLayoutHeightDp_ = 0;
    mainLayoutSashRegistry_.clear();
    if (!rmlHost_.initialized()) {
        return;
    }

    const auto measureMainLayoutRect = [&](std::string_view elementId) -> std::optional<UIPixelRect> {
        const std::optional<RmlElementRect> rect = rmlHost_.measureElementById(std::string(elementId));
        if (!rect) {
            return std::nullopt;
        }
        return uiPixelRectFromRmlRect(*rect);
    };

    MainLayoutSashMeasuredLayout measuredLayout;
    const int hitThicknessPx = std::max(
            8,
            static_cast<int>(std::lround(
                    static_cast<float>(kResizableSplitDefaultHitThicknessDp) * rmlDpRatio_)));
    if (const std::optional<RmlElementRect> rect = rmlHost_.measureElementById("main-layout-slot")) {
        mainLayoutLeftPx_ = rect->left;
        mainLayoutTopPx_ = rect->top;
        mainLayoutWidthDp_ = static_cast<int>(
                std::lround(static_cast<float>(std::max(0, rect->width)) / std::max(0.001f, rmlDpRatio_)));
        mainLayoutHeightDp_ = static_cast<int>(
                std::lround(static_cast<float>(std::max(0, rect->height)) / std::max(0.001f, rmlDpRatio_)));
        measuredLayout.mainWidthDp = mainLayoutWidthDp_;
        measuredLayout.mainHeightDp = mainLayoutHeightDp_;
    }
    if (const std::optional<RmlElementRect> editorColumnRect =
                rmlHost_.measureElementById(mainLayoutEditorColumnElementId())) {
        measuredLayout.bottomDockTrackWidthDp = static_cast<int>(std::lround(
                static_cast<float>(std::max(0, editorColumnRect->width)) / std::max(0.001f, rmlDpRatio_)));
    }
    for (DockLocation location : {DockLocation::Sidebar, DockLocation::AuxiliaryBar}) {
        if (const std::optional<RmlElementRect> rect =
                        rmlHost_.measureElementById(mainLayoutSideDockElementId(location))) {
            measuredLayout.sideDockHeightDp = std::max(
                    measuredLayout.sideDockHeightDp,
                    static_cast<int>(std::lround(
                            static_cast<float>(std::max(0, rect->height)) / std::max(0.001f, rmlDpRatio_))));
        }
    }
    if (const std::optional<RmlElementRect> rect = rmlHost_.measureElementById(mainLayoutBottomDockElementId())) {
        const int bottomDockWidthDp = static_cast<int>(std::lround(
                static_cast<float>(std::max(0, rect->width)) / std::max(0.001f, rmlDpRatio_)));
        measuredLayout.bottomDockTrackWidthDp =
                std::max(measuredLayout.bottomDockTrackWidthDp, bottomDockWidthDp);
    }
    mainLayoutSashRegistry_.setMeasuredLayout(measuredLayout);
    for (DockLocation location : {DockLocation::Sidebar, DockLocation::AuxiliaryBar}) {
        if (const std::optional<RmlElementRect> rect =
                        rmlHost_.measureElementById(mainLayoutSideDockResizerElementId(location))) {
            const std::optional<MainLayoutSidebarSide> side =
                    mainLayoutSidebarSideForDockLocation(mainLayout_, location);
            if (side) {
                mainLayoutSashRegistry_.addSideDockWidthSash(
                        mainLayout_,
                        location,
                        *side,
                        ResizableSplitController::inflatedHitRect(
                                uiPixelRectFromRmlRect(*rect),
                                ResizableSplitAxis::Horizontal,
                                hitThicknessPx));
            }
        }
    }
    if (const std::optional<RmlElementRect> rect = rmlHost_.measureElementById("main-layout-panel-resizer")) {
        mainLayoutSashRegistry_.addSidePanelStackSash(
                mainLayout_,
                ResizableSplitController::inflatedHitRect(
                        uiPixelRectFromRmlRect(*rect),
                        ResizableSplitAxis::Vertical,
                        hitThicknessPx));
    }
    if (const std::optional<RmlElementRect> rect = rmlHost_.measureElementById(mainLayoutBottomDockResizerElementId())) {
        mainLayoutSashRegistry_.addBottomDockHeightSash(
                mainLayout_,
                ResizableSplitController::inflatedHitRect(
                        uiPixelRectFromRmlRect(*rect),
                        ResizableSplitAxis::Vertical,
                        hitThicknessPx));
    }
    if (mainLayoutBottomDockVisibleForLocation(mainLayout_)) {
        const std::optional<RmlElementRect> contentRect =
                rmlHost_.measureElementById(mainLayoutBottomDockContentElementId());
        if (contentRect) {
            const int bottomDockContentWidthDp = static_cast<int>(std::lround(
                    static_cast<float>(std::max(0, contentRect->width)) / std::max(0.001f, rmlDpRatio_)));
            const int bottomSplitTrackWidthDp = std::max(
                    bottomDockContentWidthDp,
                    measuredLayout.bottomDockTrackWidthDp);
            const int contentRightPx = contentRect->left + contentRect->width;
            const std::vector<std::string_view> splitPanels =
                    dockSplitPanelsInLocation(mainLayout_.dockLayout, DockLocation::Panel);
            for (std::size_t index = 0; index < splitPanels.size(); ++index) {
                const std::string_view panelId = splitPanels[index];
                if (!mainLayoutPanelVisibleInRenderedDock(mainLayout_, panelId)) {
                    continue;
                }
                if (const std::optional<RmlElementRect> panelRect =
                                rmlHost_.measureElementById(mainLayoutPanelElementId(panelId))) {
                    const int widthDp = static_cast<int>(std::lround(
                            static_cast<float>(std::max(0, panelRect->width)) / std::max(0.001f, rmlDpRatio_)));
                    const int availableWidthDp = static_cast<int>(std::lround(
                            static_cast<float>(std::max(0, contentRightPx - panelRect->left)) /
                            std::max(0.001f, rmlDpRatio_)));
                    MainLayoutBottomSplitPaneMeasurement measurement{
                            .panelId = std::string(panelId),
                            .widthDp = widthDp,
                            .availableWidthDp = availableWidthDp,
                            .trackWidthDp = bottomSplitTrackWidthDp,
                            .splitIndex = index,
                            .splitCount = splitPanels.size(),
                    };
                    if (index + 1 < splitPanels.size()) {
                        mainLayoutSashRegistry_.addBottomPanelSplitSash(
                                mainLayout_,
                                std::move(measurement),
                                ResizableSplitController::inflatedHitRect(
                                        bottomSplitResizerHitRectFromPane(*panelRect, *contentRect),
                                        ResizableSplitAxis::Horizontal,
                                        hitThicknessPx));
                    }
                }
            }
        }
    }
    MainLayoutDockHitRegions dockHitRegions =
            buildMainLayoutDockHitRegions(mainLayout_, measureMainLayoutRect, rmlDpRatio_);
    mainLayoutResizeController_.setHitRegions(mainLayoutSashRegistry_.hitRegions());
    mainLayoutDockDragController_.setHitRegions(std::move(dockHitRegions.panelDragRegions));
    mainLayoutDockDragController_.setDockDropZones(std::move(dockHitRegions.dockDropZones));
    mainLayoutDockDragController_.setPanelDropZones(std::move(dockHitRegions.panelDropZones));
}

bool UISystem::handleMainLayoutPointerEvent(const SDL_Event& event) {
    if (debugPaletteVisible_) {
        cancelMainLayoutDockDrag();
        cancelMainLayoutResize();
        setMainLayoutCursor(MainLayoutCursorKind::Default);
        return false;
    }

    if (mainLayoutResizeController_.active() && shouldCancelMainLayoutPointerGesture(event)) {
        cancelMainLayoutResize();
        return true;
    }
    if (mainLayoutDockDragController_.active() && shouldCancelMainLayoutPointerGesture(event)) {
        cancelMainLayoutDockDrag();
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

    if (mainLayoutDockDragController_.active()) {
        if (event.type == SDL_EVENT_MOUSE_MOTION) {
            if ((event.motion.state & SDL_BUTTON_LMASK) == 0u) {
                cancelMainLayoutDockDrag();
                return true;
            }
            updateMainLayoutDockDrag(mouseX, mouseY);
            return true;
        }
        if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT) {
            endMainLayoutDockDrag(mouseX, mouseY);
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
    if (beginMainLayoutDockDragCandidate(mouseX, mouseY)) {
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
        if (!sdlEventTargetsWindow(event, windowId)) {
            return std::nullopt;
        }
        windowX = event.motion.x;
        windowY = event.motion.y;
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (!sdlEventTargetsWindow(event, windowId)) {
            return std::nullopt;
        }
        windowX = event.button.x;
        windowY = event.button.y;
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        if (!sdlEventTargetsWindow(event, windowId)) {
            return std::nullopt;
        }
        windowX = event.wheel.mouse_x;
        windowY = event.wheel.mouse_y;
        break;
    default:
        return std::nullopt;
    }

    const float scaleX = windowWidth_ > 0 ? static_cast<float>(framebufferWidth_) / static_cast<float>(windowWidth_) : 1.0f;
    const float scaleY = windowHeight_ > 0 ? static_cast<float>(framebufferHeight_) / static_cast<float>(windowHeight_) : 1.0f;
    return std::make_pair(windowX * scaleX, windowY * scaleY);
}

std::optional<ResizableSplitHandle> UISystem::mainLayoutSplitHandle(std::string_view handleId) const {
    return mainLayoutSashRegistry_.handle(handleId);
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

    if (mainLayoutSashRegistry_.applyResize(mainLayout_, mainLayoutController_, *resize)) {
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

bool UISystem::beginMainLayoutDockDragCandidate(float mouseX, float mouseY) {
    if (!mainLayoutDockDragController_.beginDragCandidate(mouseX, mouseY)) {
        return false;
    }

    updateMainLayoutDockDragPreviewPosition(mouseX, mouseY);
    if (!SDL_CaptureMouse(true)) {
        LOG_WARN("Unable to capture mouse for main-layout panel drag: {}", SDL_GetError());
    } else {
        mainLayoutMouseCaptured_ = true;
    }
    viewportHost_.setCursorCaptured(false);
    setMainLayoutCursor(MainLayoutCursorKind::Default);
    mainLayoutDirty_ = true;
    return true;
}

void UISystem::updateMainLayoutDockDrag(float mouseX, float mouseY) {
    const bool wasDragging = mainLayoutDockDragController_.dragging();
    const std::optional<MainLayoutDockDropTarget> previousTarget = mainLayoutDockDragController_.target();
    const int previousPreviewLeftDp = mainLayoutDockDragPreviewLeftDp_;
    const int previousPreviewTopDp = mainLayoutDockDragPreviewTopDp_;
    const int previousPreviewWidthDp = mainLayoutDockDragPreviewWidthDp_;
    const int previousPreviewHeightDp = mainLayoutDockDragPreviewHeightDp_;
    updateMainLayoutDockDragPreviewPosition(mouseX, mouseY);
    const std::optional<MainLayoutDockDropTarget> target =
            mainLayoutDockDragController_.updateDrag(mouseX, mouseY);
    if (mainLayoutDockDragController_.dragging()) {
        setMainLayoutCursor(target ? MainLayoutCursorKind::Default : MainLayoutCursorKind::DropForbidden);
    }
    if (wasDragging != mainLayoutDockDragController_.dragging() ||
            !mainLayoutDockDropTargetsEqual(previousTarget, target) ||
            (mainLayoutDockDragController_.dragging() &&
                    (previousPreviewLeftDp != mainLayoutDockDragPreviewLeftDp_ ||
                            previousPreviewTopDp != mainLayoutDockDragPreviewTopDp_ ||
                            previousPreviewWidthDp != mainLayoutDockDragPreviewWidthDp_ ||
                            previousPreviewHeightDp != mainLayoutDockDragPreviewHeightDp_))) {
        mainLayoutDirty_ = true;
    }
}

void UISystem::endMainLayoutDockDrag(float mouseX, float mouseY) {
    if (!mainLayoutDockDragController_.active()) {
        return;
    }

    const bool hadPresentationState = mainLayoutDockDragController_.active();
    const bool wasDragging = mainLayoutDockDragController_.dragging();
    const std::string activePanelId(mainLayoutDockDragController_.activePanelId());
    const std::optional<MainLayoutDockDropResult> drop =
            mainLayoutDockDragController_.endDrag(mouseX, mouseY);
    mainLayoutDockDragPreviewWidthDp_ = 0;
    mainLayoutDockDragPreviewHeightDp_ = 0;
    releaseMainLayoutMouseCapture();
    if (drop) {
        bool moved = false;
        switch (drop->target.action) {
        case MainLayoutDockDropAction::MoveToDock:
            moved = mainLayoutController_.movePanelToDockLocation(drop->panelId, drop->target.location);
            break;
        case MainLayoutDockDropAction::InsertPanel:
            moved = mainLayoutController_.movePanelToDockLocation(
                    drop->panelId,
                    drop->target.location,
                    drop->target.targetPanelId,
                    drop->target.position);
            break;
        case MainLayoutDockDropAction::SplitPanel:
            moved = mainLayoutController_.splitPanelWithDockPanel(
                    drop->panelId,
                    drop->target.location,
                    drop->target.targetPanelId,
                    drop->target.position);
            break;
        }
        const bool expandedDock = drop->target.location == DockLocation::Panel ?
                mainLayoutController_.setBottomDockCollapsed(false) :
                mainLayoutController_.setSidebarCollapsed(drop->target.location, false);
        mainLayoutDirty_ = mainLayoutDirty_ || moved || expandedDock;
    } else if (!wasDragging && !activePanelId.empty()) {
        if (const std::optional<DockLocation> location = dockPanelLocation(mainLayout_.dockLayout, activePanelId);
                location && *location == DockLocation::Panel &&
                mainLayoutController_.setActiveDockPanel(DockLocation::Panel, activePanelId)) {
            mainLayoutDirty_ = true;
        }
    }
    if (hadPresentationState) {
        mainLayoutDirty_ = true;
    }
    updateMainLayoutCursor(mouseX, mouseY);
}

void UISystem::cancelMainLayoutDockDrag() {
    if (!mainLayoutDockDragController_.active()) {
        return;
    }

    const bool hadPresentationState = mainLayoutDockDragController_.active();
    mainLayoutDockDragController_.cancel();
    mainLayoutDockDragPreviewWidthDp_ = 0;
    mainLayoutDockDragPreviewHeightDp_ = 0;
    releaseMainLayoutMouseCapture();
    if (hadPresentationState) {
        mainLayoutDirty_ = true;
    }
    setMainLayoutCursor(MainLayoutCursorKind::Default);
}

void UISystem::updateMainLayoutDockDragPreviewPosition(float mouseX, float mouseY) {
    const float dpRatio = std::max(0.001f, rmlDpRatio_);
    mainLayoutDockDragPreviewLeftDp_ = std::max(0,
            static_cast<int>(std::lround(
                    (mouseX - static_cast<float>(mainLayoutLeftPx_) + kMainLayoutDockDragPreviewHotspotOffsetPx) /
                    dpRatio)));
    mainLayoutDockDragPreviewTopDp_ = std::max(0,
            static_cast<int>(std::lround(
                    (mouseY - static_cast<float>(mainLayoutTopPx_) + kMainLayoutDockDragPreviewHotspotOffsetPx) /
                    dpRatio)));

    if (!mainLayoutDockDragController_.activePanelId().empty()) {
        const std::string_view activePanelId = mainLayoutDockDragController_.activePanelId();
        const std::optional<DockLocation> location = dockPanelLocation(mainLayout_.dockLayout, activePanelId);
        const std::string dragElementId = location && *location == DockLocation::Panel ?
                mainLayoutPanelTabElementId(activePanelId) :
                mainLayoutPanelHeaderElementId(activePanelId);
        if (const std::optional<RmlElementRect> rect = rmlHost_.measureElementById(dragElementId)) {
            mainLayoutDockDragPreviewWidthDp_ = std::max(1,
                    static_cast<int>(std::lround(
                            static_cast<float>(std::max(0, rect->width)) / dpRatio)));
            mainLayoutDockDragPreviewHeightDp_ = std::max(1,
                    static_cast<int>(std::lround(
                            static_cast<float>(std::max(0, rect->height)) / dpRatio)));
        }
    }
}

void UISystem::releaseMainLayoutMouseCapture() {
    if (!mainLayoutMouseCaptured_) {
        return;
    }

    if (!SDL_CaptureMouse(false)) {
        LOG_WARN("Unable to release mouse capture after main-layout pointer gesture: {}", SDL_GetError());
    }
    mainLayoutMouseCaptured_ = false;
}

bool UISystem::shouldCancelMainLayoutPointerGesture(const SDL_Event& event) const {
    const std::optional<SDL_WindowID> targetWindow = sdlEventWindowId(event);
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
    mainLayoutDropForbiddenCursor_ = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NOT_ALLOWED);
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
    if (mainLayoutDropForbiddenCursor_) {
        SDL_DestroyCursor(mainLayoutDropForbiddenCursor_);
        mainLayoutDropForbiddenCursor_ = nullptr;
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
    } else if (kind == MainLayoutCursorKind::DropForbidden && mainLayoutDropForbiddenCursor_) {
        cursor = mainLayoutDropForbiddenCursor_;
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
    const bool changed = mainLayoutSashRegistry_.normalizeMeasuredBounds(mainLayout_, mainLayoutController_);
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

    const std::optional<std::pair<float, float>> pointer = mainLayoutPointerFramebufferPosition(event);
    if (!pointer) {
        return true;
    }

    const float x = pointer->first;
    const float y = pointer->second;
    return x >= static_cast<float>(mainViewportFrame_->left) &&
            y >= static_cast<float>(mainViewportFrame_->top) &&
            x < static_cast<float>(mainViewportFrame_->left + mainViewportFrame_->width) &&
            y < static_cast<float>(mainViewportFrame_->top + mainViewportFrame_->height);
}

bool UISystem::pointerEventInsideRmlElement(const SDL_Event& event, std::string_view elementId) const {
    const std::optional<std::pair<float, float>> pointer = mainLayoutPointerFramebufferPosition(event);
    if (!pointer) {
        return false;
    }
    const std::optional<RmlElementRect> rect = rmlHost_.measureElementById(std::string(elementId));
    if (!rect) {
        return false;
    }

    const float x = pointer->first;
    const float y = pointer->second;
    return x >= static_cast<float>(rect->left) &&
            y >= static_cast<float>(rect->top) &&
            x < static_cast<float>(rect->left + rect->width) &&
            y < static_cast<float>(rect->top + rect->height);
}

bool UISystem::pointerEventInsideModelingModeShelf(const SDL_Event& event) const {
    for (const std::string_view elementId : modelingModeShelfPointerBypassElementIds()) {
        if (pointerEventInsideRmlElement(event, elementId)) {
            return true;
        }
    }
    return false;
}

bool UISystem::pointerEventShouldBypassViewport(const SDL_Event& event) const {
    switch (event.type) {
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (viewportHost_.hasActivePointerGesture()) {
            return false;
        }
        return pointerEventInsideModelingModeShelf(event) || !pointerEventInsideMainViewport(event);
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_WHEEL:
        return pointerEventInsideModelingModeShelf(event) || !pointerEventInsideMainViewport(event);
    default:
        return false;
    }
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
    cancelMainLayoutDockDrag();
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
