#include "quader/ui/UISystem.h"

#include "quader/core/Logger.h"
#include "quader/ui/ConsolePanel.h"
#include "quader/ui/InspectorPanel.h"
#include "quader/ui/OutlinerPanel.h"
#include "quader/ui/UIContext.h"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_internal.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_mouse.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <system_error>

namespace quader {
namespace {

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

} // namespace

UISystem::~UISystem() {
    shutdown();
}

void UISystem::initialize(SDL_Window* window, RenderSystem& renderSystem, const std::filesystem::path& executableDir) {
    window_ = window;
    settingsPaths_ = UISettings::resolvePaths(executableDir);
    std::error_code error;
    std::filesystem::create_directories(settingsPaths_.directory, error);
    settings_ = UISettings::load(settingsPaths_.settingsFile);
    imguiIniPath_ = settingsPaths_.imguiIniFile.string();
    const bool iniExists = std::filesystem::exists(settingsPaths_.imguiIniFile, error);
    const std::uintmax_t iniSize = iniExists ? std::filesystem::file_size(settingsPaths_.imguiIniFile, error) : 0u;
    hasSavedDockLayout_ = iniExists && !error && iniSize > 0u;

    registerPanels();
    panels_.applySettings(settings_);

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
    initialized_ = true;
    LOG_INFO("ImGui docking UI initialized");
}

void UISystem::shutdown() {
    if (!initialized_) {
        return;
    }

    setViewportCursorCaptured(false);
    saveSettings();
    if (!imguiIniPath_.empty()) {
        ImGui::SaveIniSettingsToDisk(imguiIniPath_.c_str());
    }

    renderer_.shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    initialized_ = false;
    window_ = nullptr;
    viewportPanel_ = nullptr;
}

bool UISystem::processEvent(const SDL_Event& event) {
    return ImGui_ImplSDL3_ProcessEvent(&event);
}

void UISystem::beginFrame() {
    renderer_.releaseFrameResources();
    relativeMouseDelta_ = {};
    if (relativeMouseCaptured_) {
        float deltaX = 0.0f;
        float deltaY = 0.0f;
        (void)SDL_GetRelativeMouseState(&deltaX, &deltaY);
        relativeMouseDelta_ = {deltaX, deltaY};
    }
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void UISystem::draw(ViewportRenderer& viewport,
        ViewportCameraController& camera,
        SceneRenderBridge& renderBridge,
        SceneDocument& document,
        AssetRegistry& assets,
        UndoRedoManager& undoRedo,
        DocumentCommandService& documentCommands,
        ModelingSessionService& modeling,
        AssetId defaultMaterial,
        bool& quitRequested) {
    drawDockspace(quitRequested);

    bool viewportCursorCaptureRequested = false;
    Vec2 viewportCursorCapturePoint = {};
    const UIPanelId previousFocusedPanel = focusedPanel_;
    bool panelFocusChanged = false;
    UIContext context{
            .viewport = viewport,
            .camera = camera,
            .renderBridge = renderBridge,
            .document = document,
            .assets = assets,
            .undoRedo = undoRedo,
            .documentCommands = documentCommands,
            .modeling = modeling,
            .defaultMaterial = defaultMaterial,
            .quitRequested = quitRequested,
            .style = style_,
            .viewportRelativeMouseDelta = relativeMouseDelta_,
            .viewportCursorCaptureRequested = &viewportCursorCaptureRequested,
            .viewportCursorCapturePoint = &viewportCursorCapturePoint,
            .focusedPanel = focusedPanel_,
            .previousFocusedPanel = previousFocusedPanel,
            .focusedPanelStorage = &focusedPanel_,
            .panelFocusChanged = &panelFocusChanged,
    };
    panels_.draw(context);
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

void UISystem::render(RenderSystem& renderSystem) {
    renderer_.render(renderSystem.renderer());
}

void UISystem::registerPanels() {
    panels_.add(std::make_unique<OutlinerPanel>());

    auto viewport = std::make_unique<ViewportPanel>();
    viewportPanel_ = viewport.get();
    panels_.add(std::move(viewport));

    panels_.add(std::make_unique<InspectorPanel>());
    panels_.add(std::make_unique<ConsolePanel>());
}

void UISystem::drawDockspace(bool& quitRequested) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("Main Dockspace", nullptr, flags);
    ImGui::PopStyleVar(2);

    drawMenuBar(quitRequested);

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
    ImGui::DockSpace(dockspaceId, {0.0f, 0.0f}, ImGuiDockNodeFlags_AutoHideTabBar);
    ImGui::PopStyleVar();
    enforceSidebarDockMinimums();

    ImGui::End();
}

void UISystem::drawMenuBar(bool& quitRequested) {
    if (!ImGui::BeginMenuBar()) {
        return;
    }

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Exit")) {
            quitRequested = true;
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        if (ImGui::BeginMenu("Toolbars")) {
            for (const std::unique_ptr<UIPanel>& panel : panels_.panels()) {
                if (!panel->canToggle()) {
                    continue;
                }
                bool visible = panels_.isVisible(panel->id());
                if (ImGui::MenuItem(panel->title(), nullptr, &visible)) {
                    panels_.setVisible(panel->id(), visible);
                    saveSettings();
                }
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }

    ImGui::EndMenuBar();
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

void UISystem::warpCapturedCursor(Vec2 point) {
    if (window_ == nullptr || !relativeMouseCaptured_) {
        return;
    }
    SDL_WarpMouseInWindow(window_, point.x, point.y);
    float discardX = 0.0f;
    float discardY = 0.0f;
    (void)SDL_GetRelativeMouseState(&discardX, &discardY);
    relativeMouseDelta_ = {};
}

void UISystem::saveSettings() {
    panels_.writeSettings(settings_);
    if (!settings_.save(settingsPaths_.settingsFile)) {
        LOG_WARN("Unable to save UI settings to {}", settingsPaths_.settingsFile.string());
    }
}

} // namespace quader
