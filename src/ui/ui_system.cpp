////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/ui_system.hpp"

#include "api/quader_editor_api.hpp"
#include "core/logger.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/viewport/quader_viewport_api_runtime_access.hpp"
#include "render/filament_buffer_descriptor_factory.hpp"
#include "ui/console_panel.hpp"
#include "ui/inspector_panel.hpp"
#include "ui/outliner_panel.hpp"
#include "ui/ui_context.hpp"

#include <filament/Texture.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_internal.h>
#include <lodepng.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_mouse.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace quader {
namespace {

constexpr float kWindowChromeHeight = 48.0f;
constexpr float kWindowChromeControlsWidth = 120.0f;
constexpr float kWindowChromeButtonWidth = 40.0f;
constexpr float kStaticBottomBarHeight = 28.0f;
constexpr float kStaticBottomBarAccentHeight = 3.0f;

ImVec4 uiColor(int red, int green, int blue, float alpha = 1.0f) {
    return {
            static_cast<float>(red) / 255.0f,
            static_cast<float>(green) / 255.0f,
            static_cast<float>(blue) / 255.0f,
            alpha,
    };
}

ImU32 uiPackedColor(int red, int green, int blue, int alpha = 255) {
    return IM_COL32(red, green, blue, alpha);
}

void clampDockLeafNodeWidth(ImGuiDockNode* node, float minimumWidth) {
    if (node == nullptr || node->ParentNode == nullptr || node->ParentNode->SplitAxis != ImGuiAxis_X) {
        return;
    }

    ImGuiDockNode* parent = node->ParentNode;
    ImGuiDockNode* sibling = parent->ChildNodes[0] == node ? parent->ChildNodes[1] : parent->ChildNodes[0];
    if (sibling == nullptr || parent->Size.x <= minimumWidth + 1.0f || node->Size.x >= minimumWidth) {
        return;
    }

    const bool nodeIsLeftChild = parent->ChildNodes[0] == node;
    const float clampedWidth = std::min(minimumWidth, parent->Size.x - 1.0f);
    const float siblingWidth = std::max(1.0f, parent->Size.x - clampedWidth);

    node->Size.x = clampedWidth;
    node->SizeRef.x = clampedWidth;
    sibling->Size.x = siblingWidth;
    sibling->SizeRef.x = siblingWidth;

    if (nodeIsLeftChild) {
        node->Pos.x = parent->Pos.x;
        sibling->Pos.x = parent->Pos.x + clampedWidth;
    } else {
        sibling->Pos.x = parent->Pos.x;
        node->Pos.x = parent->Pos.x + siblingWidth;
    }

    node->WantLockSizeOnce = true;
    sibling->WantLockSizeOnce = true;
}

void clampDockedWindowWidth(const char* windowName, float minimumWidth) {
    ImGuiWindow* window = ImGui::FindWindowByName(windowName);
    if (window == nullptr || window->DockNode == nullptr) {
        return;
    }
    clampDockLeafNodeWidth(window->DockNode, minimumWidth);
}

std::optional<UIPanelId> panelIdFromName(std::string_view name) {
    for (const UIPanelId id : {UIPanelId::Viewport, UIPanelId::Outliner, UIPanelId::Inspector, UIPanelId::Console}) {
        if (name == uiPanelKey(id)) {
            return id;
        }
    }
    return std::nullopt;
}

bool chromeIconButton(const char* stableId, WindowChromeAction action, const ImVec2& size) {
    ImGui::PushID(stableId);
    const bool clicked = ImGui::InvisibleButton("##button", size);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const ImVec2 center{(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f};
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    if (hovered || active) {
        ImU32 background = uiPackedColor(46, 46, 46);
        if (active) {
            background = uiPackedColor(60, 60, 60);
        }
        if (action == WindowChromeAction::Close) {
            background = active ? uiPackedColor(155, 32, 32) : uiPackedColor(206, 59, 59);
        }
        drawList->AddRectFilled(min, max, background);
    }

    const ImU32 iconColor = hovered || active ? uiPackedColor(247, 247, 247) : uiPackedColor(180, 180, 180);
    const float thickness = 1.5f;
    if (action == WindowChromeAction::Minimize) {
        drawList->AddLine({center.x - 5.0f, center.y + 4.0f}, {center.x + 5.0f, center.y + 4.0f}, iconColor, thickness);
    } else if (action == WindowChromeAction::ToggleMaximizeRestore) {
        drawList->AddRect({center.x - 5.0f, center.y - 5.0f}, {center.x + 5.0f, center.y + 5.0f}, iconColor, 0.0f, 0, thickness);
    } else {
        drawList->AddLine({center.x - 5.0f, center.y - 5.0f}, {center.x + 5.0f, center.y + 5.0f}, iconColor, thickness);
        drawList->AddLine({center.x + 5.0f, center.y - 5.0f}, {center.x - 5.0f, center.y + 5.0f}, iconColor, thickness);
    }

    ImGui::PopID();
    return clicked;
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

void drawStaticBottomBar(const ImGuiViewport& viewport) {
    ImGui::SetNextWindowPos({viewport.Pos.x, viewport.Pos.y + std::max(0.0f, viewport.Size.y - kStaticBottomBarHeight)});
    ImGui::SetNextWindowSize({std::max(1.0f, viewport.Size.x), kStaticBottomBarHeight});
    ImGui::SetNextWindowViewport(viewport.ID);

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoInputs |
            ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoNavFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, uiColor(18, 19, 22));
    ImGui::Begin("Quader Static Bottom Bar", nullptr, flags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);

    const ImVec2 min = ImGui::GetWindowPos();
    const ImVec2 max{min.x + ImGui::GetWindowWidth(), min.y + kStaticBottomBarHeight};
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    drawList->AddRectFilled(min, max, uiPackedColor(18, 19, 22));
    drawList->AddLine(min, {max.x, min.y}, uiPackedColor(72, 74, 82));
    drawList->AddRectFilled(
            {min.x, max.y - kStaticBottomBarAccentHeight},
            max,
            uiPackedColor(118, 92, 168));

    const float textY = min.y + std::floor((kStaticBottomBarHeight - ImGui::GetFontSize()) * 0.5f);
    drawList->AddText({min.x + 12.0f, textY}, uiPackedColor(216, 216, 216), "Ready");

    const char* rightLabel = "Quader";
    const ImVec2 rightLabelSize = ImGui::CalcTextSize(rightLabel);
    drawList->AddText(
            {std::max(min.x + 72.0f, max.x - rightLabelSize.x - 12.0f), textY},
            uiPackedColor(150, 154, 163),
            rightLabel);

    ImGui::End();
}

} // namespace

UISystem::~UISystem() {
    shutdown();
}

void UISystem::initialize(
        SDL_Window* window,
        RenderSystem& renderSystem,
        const std::filesystem::path& executableDir,
        bool customChrome) {
    window_ = window;
    customChrome_ = customChrome;
    settingsPaths_ = UISettings::resolvePaths(executableDir);
    std::error_code error;
    std::filesystem::create_directories(settingsPaths_.directory, error);
    settings_ = UISettings::load(settingsPaths_.settingsFile);
    localization_.load(executableDir / "locales", settings_.locale());
    if (settings_.locale() != localization_.currentLocale()) {
        settings_.setLocale(localization_.currentLocale());
    }
    viewportSettings_ = ViewportSettings::load(settingsPaths_.viewportSettingsFile);
    imguiIniPath_ = settingsPaths_.imguiIniFile.string();
    const bool iniExists = std::filesystem::exists(settingsPaths_.imguiIniFile, error);
    const std::uintmax_t iniSize = iniExists ? std::filesystem::file_size(settingsPaths_.imguiIniFile, error) : 0u;
    hasSavedDockLayout_ = iniExists && !error && iniSize > 0u;

    registerPanels(renderSystem);
    panels_.applySettings(settings_);
    if (viewportPanel_ != nullptr) {
        viewportPanel_->setNavigationGizmoVisible(settings_.navigationGizmoVisible());
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = imguiIniPath_.c_str();

    loadUIDefaultFont(executableDir, style_);
    applyUIStyle(style_);

    LOG_INFO("Initializing ImGui SDL3 backend");
    ImGui_ImplSDL3_InitForOther(window_);
    LOG_INFO("Initializing ImGui Filament renderer");
    renderer_.initialize(renderSystem.engine(), renderSystem.imguiMaterial(), renderSystem.textures());
    loadIcons(renderSystem.engine(), renderSystem.textures(), executableDir);
    initialized_ = true;
    LOG_INFO("ImGui docking UI initialized");
}

void UISystem::shutdown() {
    if (!initialized_) {
        return;
    }

    setViewportCursorCaptured(false);
    saveSettings();
    saveViewportSettings();
    if (!imguiIniPath_.empty()) {
        ImGui::SaveIniSettingsToDisk(imguiIniPath_.c_str());
    }

    if (viewportPanel_ != nullptr) {
        viewportPanel_->shutdown();
    }
    renderer_.shutdown();
    destroyIcons();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    initialized_ = false;
    window_ = nullptr;
    customChrome_ = false;
    pendingChromeAction_.reset();
    viewportPanel_ = nullptr;
}

bool UISystem::processEvent(const SDL_Event& event) {
    return ImGui_ImplSDL3_ProcessEvent(&event);
}

void UISystem::beginFrame(const FrameTiming& frameTiming) {
    currentFrameTiming_ = frameTiming;
    renderer_.releaseFrameResources();
    relativeMouseDelta_ = {};
    if (relativeMouseCaptured_) {
        float deltaX = 0.0f;
        float deltaY = 0.0f;
        (void)SDL_GetRelativeMouseState(&deltaX, &deltaY);
        relativeMouseDelta_ = {deltaX, deltaY};
    }
    ImGui_ImplSDL3_NewFrame();
    ImGui::GetIO().DeltaTime = static_cast<float>(std::max(currentFrameTiming_.deltaSeconds, 0.0));
    ImGui::NewFrame();
}

void UISystem::draw(QuaderEditorApi& editor,
        SceneRenderBridge& renderBridge,
        SceneDocument& document,
        AssetRegistry& assets,
        ModelingSessionService& modeling,
        EditorSelectionService& selection,
        AssetId defaultMaterial,
        bool& quitRequested) {
    editor.ui().setRuntimeHooks(QuaderUIRuntimeHooks{
            .isPanelVisible = [this](std::string_view panel) {
                const std::optional<UIPanelId> id = panelIdFromName(panel);
                return id.has_value() && panels_.isVisible(*id);
            },
            .setPanelVisible = [this](std::string_view panel, bool visible) {
                const std::optional<UIPanelId> id = panelIdFromName(panel);
                if (!id.has_value()) {
                    return runtimeFailure("Unknown UI panel.");
                }
                const bool before = panels_.isVisible(*id);
                panels_.setVisible(*id, visible);
                if (before != visible) {
                    saveSettings();
                }
                return runtimeStatus(before != visible);
            },
            .focusPanel = [this](std::string_view panel) {
                const std::optional<UIPanelId> id = panelIdFromName(panel);
                if (!id.has_value()) {
                    return runtimeFailure("Unknown UI panel.");
                }
                focusedPanel_ = *id;
                return runtimeStatus(true);
            },
    });
    editor.preferences().setRuntimeHooks(QuaderPreferencesRuntimeHooks{
            .locale = [this]() {
                return settings_.locale();
            },
            .setLocale = [this](std::string locale) {
                if (!localization_.selectLocale(locale)) {
                    return runtimeFailure("Unknown locale.");
                }
                const bool changed = settings_.locale() != localization_.currentLocale();
                settings_.setLocale(localization_.currentLocale());
                if (changed) {
                    saveSettings();
                }
                return runtimeStatus(changed);
            },
            .save = [this]() {
                saveSettings();
                saveViewportSettings();
                return runtimeStatus(false, "Preferences saved.");
            },
            .navigationGizmoVisible = [this]() {
                return viewportPanel_ != nullptr && viewportPanel_->navigationGizmoVisible();
            },
            .setNavigationGizmoVisible = [this](bool visible) {
                if (viewportPanel_ == nullptr) {
                    return runtimeFailure("Viewport panel is not available.");
                }
                const bool before = viewportPanel_->navigationGizmoVisible();
                viewportPanel_->setNavigationGizmoVisible(visible);
                settings_.setNavigationGizmoVisible(visible);
                saveSettings();
                return runtimeStatus(before != visible);
            },
    });
    drawWindowChrome();
    drawDockspace(editor, quitRequested, document, modeling);

    bool viewportCursorCaptureRequested = false;
    QVec2 viewportCursorCapturePoint = {};
    const UIPanelId previousFocusedPanel = focusedPanel_;
    bool panelFocusChanged = false;
    bool settingsChanged = false;
    UIContext context{
            .editor = editor,
            .renderBridge = renderBridge,
            .document = document,
            .assets = assets,
            .modeling = modeling,
            .selection = selection,
            .defaultMaterial = defaultMaterial,
            .quitRequested = quitRequested,
            .style = style_,
            .icons = icons_,
            .localization = localization_,
            .settings = settings_,
            .viewportSettings = viewportSettings_,
            .frameTiming = currentFrameTiming_,
            .viewportRelativeMouseDelta = relativeMouseDelta_,
            .viewportRenderResizeSuspended = viewportRenderResizeSuspended_,
            .viewportCursorCaptureRequested = &viewportCursorCaptureRequested,
            .viewportCursorCapturePoint = &viewportCursorCapturePoint,
            .focusedPanel = focusedPanel_,
            .previousFocusedPanel = previousFocusedPanel,
            .focusedPanelStorage = &focusedPanel_,
            .panelFocusChanged = &panelFocusChanged,
            .settingsChanged = &settingsChanged,
    };
    panels_.draw(context);
    if (viewportSettingsPanel_.draw(context, settingsPanelOpen_)) {
        saveViewportSettings();
    }
    if (settingsChanged) {
        saveSettings();
    }
    toolSettingsPanel_.draw(context, toolSettingsPanelOpen_);
    drawStaticBottomBar(*ImGui::GetMainViewport());
    std::vector<ModelingToolPreviewMesh> modelingPreviewMeshes = modeling.toolPreviewMeshes();
    std::vector<SceneRenderBridge::PreviewMesh> renderPreviewMeshes;
    renderPreviewMeshes.reserve(modelingPreviewMeshes.size());
    for (ModelingToolPreviewMesh& previewMesh : modelingPreviewMeshes) {
        const AssetId material = previewMesh.material != InvalidAssetId ? previewMesh.material : defaultMaterial;
        renderPreviewMeshes.push_back(SceneRenderBridge::PreviewMesh{
                .sourceEntity = previewMesh.sourceEntity,
                .material = material,
                .mesh = std::move(previewMesh.mesh),
                .transform = {},
        });
    }
    renderBridge.setToolPreviewMeshes(std::move(renderPreviewMeshes), assets);
    enforceSidebarDockMinimums();
    setViewportCursorCaptured(viewportCursorCaptureRequested);
    if (viewportCursorCaptureRequested) {
        warpCapturedCursor(viewportCursorCapturePoint);
    }
}

void UISystem::prepareRenderData(std::uint32_t framebufferWidth, std::uint32_t framebufferHeight) {
    ImGui::Render();
    renderer_.prepare(ImGui::GetDrawData(), framebufferWidth, framebufferHeight);
}

void UISystem::prepareViewports(
        QuaderEditorApi& editor,
        SceneRenderBridge& renderBridge,
        const SceneDocument& document,
        const AssetRegistry& assets) {
    if (viewportPanel_ != nullptr) {
        viewportPanel_->prepareViewports(
                QuaderViewportApiRuntimeAccess::workspace(editor.viewports()),
                renderBridge,
                document,
                assets);
    }
}

void UISystem::renderViewports(RenderSystem& renderSystem) {
    if (viewportPanel_ != nullptr) {
        viewportPanel_->renderViewports(renderSystem.renderer());
    }
}

void UISystem::render(RenderSystem& renderSystem) {
    renderer_.render(renderSystem.renderer());
}

void UISystem::setViewportRenderResizeSuspended(bool suspended) {
    viewportRenderResizeSuspended_ = suspended;
}

std::optional<WindowChromeAction> UISystem::takeWindowChromeAction() {
    std::optional<WindowChromeAction> action = pendingChromeAction_;
    pendingChromeAction_.reset();
    return action;
}

void UISystem::registerPanels(RenderSystem& renderSystem) {
    panels_.add(std::make_unique<OutlinerPanel>());

    auto viewport = std::make_unique<ViewportPanel>();
    viewport->initialize(renderSystem);
    viewportPanel_ = viewport.get();
    panels_.add(std::move(viewport));

    panels_.add(std::make_unique<InspectorPanel>());
    panels_.add(std::make_unique<ConsolePanel>());
}

void UISystem::drawWindowChrome() {
    if (!customChrome_) {
        return;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize({viewport->Size.x, kWindowChromeHeight});
    ImGui::SetNextWindowViewport(viewport->ID);

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoNavFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, uiColor(25, 25, 25));
    ImGui::Begin("Quader Window Chrome", nullptr, flags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetWindowPos();
    const ImVec2 max{min.x + ImGui::GetWindowWidth(), min.y + kWindowChromeHeight};
    drawList->AddLine({min.x, max.y - 1.0f}, {max.x, max.y - 1.0f}, uiPackedColor(46, 46, 46));
    const float titleY = min.y + std::floor((kWindowChromeHeight - ImGui::GetFontSize()) * 0.5f);
    drawList->AddText({min.x + 12.0f, titleY}, uiPackedColor(216, 216, 216), "Quader");

    const float controlsLeft = std::max(0.0f, ImGui::GetWindowWidth() - kWindowChromeControlsWidth);
    ImGui::SetCursorPos({controlsLeft, 0.0f});
    if (chromeIconButton("minimize", WindowChromeAction::Minimize, {kWindowChromeButtonWidth, kWindowChromeHeight})) {
        pendingChromeAction_ = WindowChromeAction::Minimize;
    }
    ImGui::SameLine(0.0f, 0.0f);
    if (chromeIconButton("restore", WindowChromeAction::ToggleMaximizeRestore, {kWindowChromeButtonWidth, kWindowChromeHeight})) {
        pendingChromeAction_ = WindowChromeAction::ToggleMaximizeRestore;
    }
    ImGui::SameLine(0.0f, 0.0f);
    if (chromeIconButton("close", WindowChromeAction::Close, {kWindowChromeButtonWidth, kWindowChromeHeight})) {
        pendingChromeAction_ = WindowChromeAction::Close;
    }

    ImGui::End();
}

void UISystem::drawDockspace(
        QuaderEditorApi& editor,
        bool& quitRequested,
        SceneDocument& document,
        ModelingSessionService& modeling) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float chromeHeight = customChrome_ ? kWindowChromeHeight : 0.0f;
    ImGui::SetNextWindowPos({viewport->Pos.x, viewport->Pos.y + chromeHeight});
    ImGui::SetNextWindowSize({viewport->Size.x, std::max(1.0f, viewport->Size.y - chromeHeight - kStaticBottomBarHeight)});
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("Main Dockspace", nullptr, flags);
    ImGui::PopStyleVar(3);

    const MainMenuBarResult menuResult = mainMenuBar_.draw({
            .quitRequested = quitRequested,
            .editor = editor,
            .document = document,
            .modeling = modeling,
            .viewportPanel = viewportPanel_,
            .panels = panels_,
            .settings = settings_,
            .localization = localization_,
            .viewportSettingsPanelOpen = settingsPanelOpen_,
            .toolSettingsPanelOpen = toolSettingsPanelOpen_,
    });
    if (menuResult.settingsChanged) {
        saveSettings();
    }

    const ImGuiID dockspaceId = ImGui::GetID("QuaderDockspace");
    const ImVec2 dockspaceSize = ImGui::GetContentRegionAvail();
    if (!hasSavedDockLayout_ && !dockLayoutBuilt_ && dockspaceSize.x > 1.0f && dockspaceSize.y > 1.0f) {
        buildDefaultDockLayout(dockspaceId, dockspaceSize);
        dockLayoutBuilt_ = true;
    }

    enforceSidebarDockMinimums();
    const ImVec2 savedWindowMinSize = ImGui::GetStyle().WindowMinSize;
    ImGui::PushStyleVar(
            ImGuiStyleVar_WindowMinSize,
            ImVec2(std::max(savedWindowMinSize.x, style_.sidebarMinWidth), savedWindowMinSize.y));
    ImGui::DockSpace(dockspaceId, dockspaceSize, ImGuiDockNodeFlags_AutoHideTabBar);
    ImGui::PopStyleVar();
    enforceSidebarDockMinimums();

    ImGui::End();
}

void UISystem::buildDefaultDockLayout(ImGuiID dockspaceId, ImVec2 dockspaceSize) {
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_AutoHideTabBar);
    ImGui::DockBuilderSetNodeSize(dockspaceId, dockspaceSize);

    ImGuiID mainId = dockspaceId;
    ImGuiID consoleId = 0;
    if (panels_.isVisible(UIPanelId::Console)) {
        consoleId = ImGui::DockBuilderSplitNode(mainId, ImGuiDir_Down, 0.24f, nullptr, &mainId);
    }
    const float sidebarMinimumRatio = style_.sidebarMinWidth / std::max(dockspaceSize.x, 1.0f);
    const float outlinerRatio = std::clamp(std::max(0.18f, sidebarMinimumRatio), 0.18f, 0.35f);
    const float inspectorRatio = std::clamp(std::max(0.24f, sidebarMinimumRatio), 0.24f, 0.35f);
    const ImGuiID outlinerId = ImGui::DockBuilderSplitNode(mainId, ImGuiDir_Left, outlinerRatio, nullptr, &mainId);
    const ImGuiID inspectorId = ImGui::DockBuilderSplitNode(mainId, ImGuiDir_Right, inspectorRatio, nullptr, &mainId);

    ImGui::DockBuilderDockWindow(uiPanelKey(UIPanelId::Outliner), outlinerId);
    ImGui::DockBuilderDockWindow(uiPanelKey(UIPanelId::Viewport), mainId);
    ImGui::DockBuilderDockWindow(uiPanelKey(UIPanelId::Inspector), inspectorId);
    if (consoleId != 0) {
        ImGui::DockBuilderDockWindow(uiPanelKey(UIPanelId::Console), consoleId);
    }
    ImGui::DockBuilderFinish(dockspaceId);
}

void UISystem::enforceSidebarDockMinimums() {
    clampDockedWindowWidth(uiPanelKey(UIPanelId::Outliner), style_.sidebarMinWidth);
    clampDockedWindowWidth(uiPanelKey(UIPanelId::Inspector), style_.sidebarMinWidth);
}

void UISystem::setViewportCursorCaptured(bool captured) {
    if (window_ == nullptr || relativeMouseCaptured_ == captured) {
        return;
    }

    if (!SDL_SetWindowRelativeMouseMode(window_, captured)) {
        if (!relativeMouseCaptureWarningIssued_) {
            LOG_WARN("Unable to {} viewport relative mouse mode: {}",
                    captured ? "enable" : "disable",
                    SDL_GetError());
            relativeMouseCaptureWarningIssued_ = true;
        }
        return;
    }

    relativeMouseCaptured_ = captured;
    relativeMouseCaptureWarningIssued_ = false;
    if (captured) {
        float discardX = 0.0f;
        float discardY = 0.0f;
        (void)SDL_GetRelativeMouseState(&discardX, &discardY);
        relativeMouseDelta_ = {};
    }
}

void UISystem::warpCapturedCursor(QVec2 point) {
    if (window_ == nullptr || !relativeMouseCaptured_) {
        return;
    }
    SDL_WarpMouseInWindow(window_, point.x, point.y);
    float discardX = 0.0f;
    float discardY = 0.0f;
    (void)SDL_GetRelativeMouseState(&discardX, &discardY);
    relativeMouseDelta_ = {};
}

void UISystem::loadIcons(filament::Engine& engine, TextureRegistry& textures, const std::filesystem::path& executableDir) {
    iconEngine_ = &engine;
    iconTextureRegistry_ = &textures;

    auto load = [&](const char* filename) {
        const std::filesystem::path iconPath = executableDir / "icons" / filename;
        std::vector<unsigned char> pixels;
        unsigned width = 0;
        unsigned height = 0;
        const unsigned error = lodepng::decode(pixels, width, height, iconPath.string());
        if (error != 0u || pixels.empty() || width == 0u || height == 0u) {
            LOG_WARN("Unable to load UI icon {}: {}", iconPath.string(), lodepng_error_text(error));
            return ImTextureID_Invalid;
        }

        const std::size_t rowByteCount = static_cast<std::size_t>(width) * 4u;
        std::vector<unsigned char> flipped(pixels.size());
        for (unsigned y = 0; y < height; ++y) {
            const std::size_t sourceOffset = static_cast<std::size_t>(y) * rowByteCount;
            const std::size_t targetOffset = static_cast<std::size_t>(height - 1u - y) * rowByteCount;
            std::memcpy(flipped.data() + targetOffset, pixels.data() + sourceOffset, rowByteCount);
        }

        filament::Texture* texture = filament::Texture::Builder()
                .width(width)
                .height(height)
                .levels(1)
                .format(filament::Texture::InternalFormat::RGBA8)
                .usage(filament::Texture::Usage::UPLOADABLE | filament::Texture::Usage::SAMPLEABLE)
                .build(engine);
        texture->setImage(engine, 0, FilamentBufferDescriptorFactory::rgbaPixels(flipped.data(), flipped.size()));
        const ImTextureID id = textures.registerTexture(texture);
        iconTextures_.push_back(texture);
        iconTextureIds_.push_back(id);
        return id;
    };

    icons_.mesh = load("mesh_icon.png");
    icons_.empty = load("empty_icon.png");
    icons_.lock = load("lock_icon.png");
    icons_.hide = load("hide_icon.png");
}

void UISystem::destroyIcons() {
    if (iconTextureRegistry_ != nullptr) {
        for (const ImTextureID id : iconTextureIds_) {
            if (id != ImTextureID_Invalid) {
                iconTextureRegistry_->unregisterTexture(id);
            }
        }
    }
    iconTextureIds_.clear();

    if (iconEngine_ != nullptr) {
        for (filament::Texture* texture : iconTextures_) {
            if (texture != nullptr) {
                iconEngine_->destroy(texture);
            }
        }
    }
    iconTextures_.clear();
    icons_ = UIIconSet{};
    iconEngine_ = nullptr;
    iconTextureRegistry_ = nullptr;
}

void UISystem::saveSettings() {
    panels_.writeSettings(settings_);
    if (viewportPanel_ != nullptr) {
        settings_.setNavigationGizmoVisible(viewportPanel_->navigationGizmoVisible());
    }
    if (!settings_.save(settingsPaths_.settingsFile)) {
        LOG_WARN("Unable to save UI settings to {}", settingsPaths_.settingsFile.string());
    }
}

void UISystem::saveViewportSettings() {
    if (!viewportSettings_.save(settingsPaths_.viewportSettingsFile)) {
        LOG_WARN("Unable to save viewport settings to {}", settingsPaths_.viewportSettingsFile.string());
    }
}

} // namespace quader
