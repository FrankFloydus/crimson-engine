#include "quader/ui/UISystem.h"

#include "quader/core/Logger.h"
#include "quader/ui/ConsolePanel.h"
#include "quader/ui/InspectorPanel.h"
#include "quader/ui/OutlinerPanel.h"
#include "quader/ui/UIContext.h"

#include <backend/PixelBufferDescriptor.h>
#include <backend/DriverEnums.h>
#include <filament/Texture.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_internal.h>
#include <lodepng.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_mouse.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
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

filament::backend::PixelBufferDescriptor makePixelBufferDescriptor(const unsigned char* pixels, std::size_t byteCount) {
    auto* bytes = new std::byte[byteCount];
    std::memcpy(bytes, pixels, byteCount);
    return {bytes, byteCount,
            filament::backend::PixelDataFormat::RGBA,
            filament::backend::PixelDataType::UBYTE,
            [](void* buffer, std::size_t, void*) {
                delete[] static_cast<std::byte*>(buffer);
            }};
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
    viewportSettings_ = ViewportSettings::load(settingsPaths_.viewportSettingsFile);
    imguiIniPath_ = settingsPaths_.imguiIniFile.string();
    const bool iniExists = std::filesystem::exists(settingsPaths_.imguiIniFile, error);
    const std::uintmax_t iniSize = iniExists ? std::filesystem::file_size(settingsPaths_.imguiIniFile, error) : 0u;
    hasSavedDockLayout_ = iniExists && !error && iniSize > 0u;

    registerPanels(renderSystem);
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

void UISystem::draw(SceneRenderBridge& renderBridge,
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
            .renderBridge = renderBridge,
            .document = document,
            .assets = assets,
            .undoRedo = undoRedo,
            .documentCommands = documentCommands,
            .modeling = modeling,
            .defaultMaterial = defaultMaterial,
            .quitRequested = quitRequested,
            .style = style_,
            .icons = icons_,
            .viewportSettings = viewportSettings_,
            .viewportRelativeMouseDelta = relativeMouseDelta_,
            .viewportCursorCaptureRequested = &viewportCursorCaptureRequested,
            .viewportCursorCapturePoint = &viewportCursorCapturePoint,
            .focusedPanel = focusedPanel_,
            .previousFocusedPanel = previousFocusedPanel,
            .focusedPanelStorage = &focusedPanel_,
            .panelFocusChanged = &panelFocusChanged,
    };
    panels_.draw(context);
    drawSettingsPanel();
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

void UISystem::prepareViewports(SceneRenderBridge& renderBridge) {
    if (viewportPanel_ != nullptr) {
        viewportPanel_->prepareViewports(renderBridge);
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

void UISystem::registerPanels(RenderSystem& renderSystem) {
    panels_.add(std::make_unique<OutlinerPanel>());

    auto viewport = std::make_unique<ViewportPanel>();
    viewport->initialize(renderSystem);
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

    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem("Settings")) {
            settingsPanelOpen_ = true;
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

    if (ImGui::BeginMenu("Layout")) {
        if (viewportPanel_ != nullptr) {
            for (const ViewportLayoutOption& option : viewportLayoutOptions()) {
                const bool selected = viewportPanel_->viewportLayout() == option.id;
                if (ImGui::MenuItem(option.label.data(), nullptr, selected)) {
                    viewportPanel_->setViewportLayout(option.id);
                }
            }
        }
        ImGui::EndMenu();
    }

    ImGui::EndMenuBar();
}

void UISystem::drawSettingsPanel() {
    if (!settingsPanelOpen_) {
        return;
    }

    bool changed = false;
    ImGui::SetNextWindowSize({420.0f, 560.0f}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Settings", &settingsPanelOpen_, ImGuiWindowFlags_NoDocking)) {
        if (ImGui::Button("Reset to Defaults")) {
            viewportSettings_.resetToDefaults();
            changed = true;
        }

        for (const ViewportSettingsSectionDescriptor& section : viewportSettingsSections()) {
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_None;
            if (section.defaultExpanded) {
                flags |= ImGuiTreeNodeFlags_DefaultOpen;
            }
            if (!ImGui::CollapsingHeader(section.label.data(), flags)) {
                continue;
            }

            for (const ViewportColorSettingDescriptor& descriptor : viewportColorSettings()) {
                if (descriptor.section != section.id) {
                    continue;
                }
                ViewportSettingColor color = viewportSettings_.color(descriptor.id);
                float values[4] = {color.r, color.g, color.b, color.a};
                ImGui::SetNextItemWidth(180.0f);
                if (ImGui::ColorEdit4(descriptor.label.data(),
                            values,
                            ImGuiColorEditFlags_NoInputs |
                                    ImGuiColorEditFlags_AlphaBar |
                                    ImGuiColorEditFlags_AlphaPreviewHalf)) {
                    changed = viewportSettings_.setColor(descriptor.id,
                                      ViewportSettingColor{values[0], values[1], values[2], values[3]}) ||
                            changed;
                }
            }

            for (const ViewportFloatSettingDescriptor& descriptor : viewportFloatSettings()) {
                if (descriptor.section != section.id) {
                    continue;
                }
                float value = viewportSettings_.floatValue(descriptor.id);
                ImGui::SetNextItemWidth(220.0f);
                if (ImGui::SliderFloat(descriptor.label.data(), &value, descriptor.minimum, descriptor.maximum, "%.3f")) {
                    changed = viewportSettings_.setFloatValue(descriptor.id, value) || changed;
                }
            }

            for (const ViewportBoolSettingDescriptor& descriptor : viewportBoolSettings()) {
                if (descriptor.section != section.id) {
                    continue;
                }
                bool value = viewportSettings_.boolValue(descriptor.id);
                if (ImGui::Checkbox(descriptor.label.data(), &value)) {
                    changed = viewportSettings_.setBoolValue(descriptor.id, value) || changed;
                }
            }
        }
    }
    ImGui::End();

    if (changed) {
        saveViewportSettings();
    }
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
        texture->setImage(engine, 0, makePixelBufferDescriptor(flipped.data(), flipped.size()));
        const ImTextureID id = textures.registerTexture(texture);
        iconTextures_.push_back(texture);
        iconTextureIds_.push_back(id);
        return id;
    };

    icons_.mesh = load("mesh_icon.png");
    icons_.empty = load("empty_icon.png");
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
