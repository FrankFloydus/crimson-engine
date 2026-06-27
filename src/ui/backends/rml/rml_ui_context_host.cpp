////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/backends/rml/rml_ui_context_host.hpp"

#include "core/logger.hpp"
#include "platform/sdl_event_utils.hpp"
#include "render/render_system.hpp"
#include "ui/components/atoms/ui_chrome_button_component.hpp"
#include "ui/components/organisms/modeling_mode_shelf_component.hpp"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/Log.h>
#include <RmlUi/Core/SystemInterface.h>
#include <RmlUi/Debugger.h>

#include <SDL3/SDL_clipboard.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace quader {
namespace {

constexpr std::string_view kGeneratedUtilityRcssPath = "quader://ui/rcss/app-utility.generated.rcss";
constexpr std::string_view kGeneratedIconRcssPath = "quader://ui/rcss/app-icons.generated.rcss";
constexpr std::string_view kIconResourceScheme = "quader://icons/";
constexpr std::string_view kUiResourceScheme = "quader://ui/";

bool startsWith(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

bool loadFontFaceFromMemory(
        const std::filesystem::path& fontPath,
        std::string_view family,
        Rml::Style::FontStyle style = Rml::Style::FontStyle::Normal,
        Rml::Style::FontWeight weight = Rml::Style::FontWeight::Auto) {
    static std::unordered_map<std::string, std::vector<Rml::byte>> fontDataByPath;

    const std::string fontPathString = fontPath.string();
    std::vector<Rml::byte>& fontData = fontDataByPath[fontPathString];
    if (fontData.empty()) {
        std::ifstream file(fontPath, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }

        fontData.assign(
                std::istreambuf_iterator<char>(file),
                std::istreambuf_iterator<char>());
    }

    return Rml::LoadFontFace(
            Rml::Span<const Rml::byte>(fontData.data(), fontData.size()),
            std::string(family),
            style,
            weight);
}

/**
 * Stores one in-memory RmlUi file stream.
 */
struct RmlMemoryFile {
    std::string data;
    std::size_t offset = 0u;
};

Rml::FileHandle toFileHandle(RmlMemoryFile* file) {
    return reinterpret_cast<Rml::FileHandle>(file);
}

RmlMemoryFile* fromFileHandle(Rml::FileHandle file) {
    return reinterpret_cast<RmlMemoryFile*>(file);
}

const RmlMemoryFile* fromFileHandleConst(Rml::FileHandle file) {
    return reinterpret_cast<const RmlMemoryFile*>(file);
}

/**
 * Provides RmlUi with app resource files plus generated design-system RCSS.
 */
class RmlFileInterface final : public Rml::FileInterface {
public:
    explicit RmlFileInterface(std::filesystem::path executableDir)
        : executableDir_(std::move(executableDir)) {}

    void setVirtualFile(std::string path, std::string contents) {
        virtualFiles_[std::move(path)] = std::move(contents);
    }

    Rml::FileHandle Open(const Rml::String& path) override {
        if (const auto virtualFile = virtualFiles_.find(path); virtualFile != virtualFiles_.end()) {
            return toFileHandle(new RmlMemoryFile{.data = virtualFile->second});
        }

        const std::string_view pathView(path.data(), path.size());
        std::filesystem::path resolvedPath = path;
        if (startsWith(pathView, kIconResourceScheme)) {
            resolvedPath = executableDir_ / "icons" / std::string(pathView.substr(kIconResourceScheme.size()));
        } else if (startsWith(pathView, kUiResourceScheme)) {
            resolvedPath = executableDir_ / "ui" / std::string(pathView.substr(kUiResourceScheme.size()));
        }

        std::ifstream file(resolvedPath, std::ios::binary);
        if (!file.is_open()) {
            const std::filesystem::path fallbackPath = executableDir_ / path;
            file.open(fallbackPath, std::ios::binary);
        }
        if (!file.is_open()) {
            return 0;
        }

        return toFileHandle(new RmlMemoryFile{
                .data = {
                        std::istreambuf_iterator<char>(file),
                        std::istreambuf_iterator<char>()},
        });
    }

    void Close(Rml::FileHandle file) override {
        delete fromFileHandle(file);
    }

    size_t Read(void* buffer, size_t size, Rml::FileHandle file) override {
        auto* memoryFile = fromFileHandle(file);
        if (!memoryFile || !buffer || size == 0u || memoryFile->offset >= memoryFile->data.size()) {
            return 0u;
        }

        const std::size_t remaining = memoryFile->data.size() - memoryFile->offset;
        const std::size_t readSize = std::min(size, remaining);
        std::memcpy(buffer, memoryFile->data.data() + memoryFile->offset, readSize);
        memoryFile->offset += readSize;
        return readSize;
    }

    bool Seek(Rml::FileHandle file, long offset, int origin) override {
        auto* memoryFile = fromFileHandle(file);
        if (!memoryFile) {
            return false;
        }

        long long base = 0;
        if (origin == SEEK_SET) {
            base = 0;
        } else if (origin == SEEK_CUR) {
            base = static_cast<long long>(memoryFile->offset);
        } else if (origin == SEEK_END) {
            base = static_cast<long long>(memoryFile->data.size());
        } else {
            return false;
        }

        const long long next = base + static_cast<long long>(offset);
        if (next < 0 || next > static_cast<long long>(memoryFile->data.size())) {
            return false;
        }

        memoryFile->offset = static_cast<std::size_t>(next);
        return true;
    }

    size_t Tell(Rml::FileHandle file) override {
        const auto* memoryFile = fromFileHandleConst(file);
        return memoryFile ? memoryFile->offset : 0u;
    }

    size_t Length(Rml::FileHandle file) override {
        const auto* memoryFile = fromFileHandleConst(file);
        return memoryFile ? memoryFile->data.size() : 0u;
    }

private:
    std::filesystem::path executableDir_;
    std::unordered_map<std::string, std::string> virtualFiles_;
};

/**
 * Bridges RmlUi timing and log callbacks to the native platform services.
 */
class RmlSystemInterface final : public Rml::SystemInterface {
public:
    double GetElapsedTime() override {
        return static_cast<double>(SDL_GetTicksNS()) / 1000000000.0;
    }

    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override {
        switch (type) {
        case Rml::Log::LT_ERROR:
        case Rml::Log::LT_ASSERT:
            LOG_ERROR("RmlUi: {}", message);
            break;
        case Rml::Log::LT_WARNING:
            LOG_WARN("RmlUi: {}", message);
            break;
        default:
            LOG_INFO("RmlUi: {}", message);
            break;
        }
        return true;
    }
};

/**
 * Copies the clicked palette token hex value into the host clipboard.
 */
class DebugPaletteCopyListener final : public Rml::EventListener {
public:
    void ProcessEvent(Rml::Event& event) override {
        for (Rml::Element* element = event.GetTargetElement(); element != nullptr; element = element->GetParentNode()) {
            const Rml::String hex = element->GetAttribute<Rml::String>("data-copy-hex", "");
            if (hex.empty()) {
                continue;
            }

            if (!SDL_SetClipboardText(hex.c_str())) {
                LOG_WARN("Unable to copy debug palette color {} to clipboard: {}", hex, SDL_GetError());
            }
            event.StopPropagation();
            return;
        }
    }
};

/**
 * Captures native window chrome actions from RmlUi controls.
 */
class WindowChromeActionListener final : public Rml::EventListener {
public:
    explicit WindowChromeActionListener(std::optional<WindowChromeAction>& pendingAction)
        : pendingAction_(pendingAction) {}

    void ProcessEvent(Rml::Event& event) override {
        for (Rml::Element* element = event.GetTargetElement(); element != nullptr; element = element->GetParentNode()) {
            const Rml::String action = element->GetAttribute<Rml::String>(
                    kChromeWindowActionAttribute.data(),
                    "");
            if (action.empty()) {
                continue;
            }

            const std::optional<UIChromeButtonKind> kind = chromeButtonKindForActionName(action);
            if (!kind.has_value()) {
                return;
            }
            if (*kind == UIChromeButtonKind::Minimize) {
                pendingAction_ = WindowChromeAction::Minimize;
            } else if (*kind == UIChromeButtonKind::Restore) {
                pendingAction_ = WindowChromeAction::RestoreOrMaximize;
            } else if (*kind == UIChromeButtonKind::Close) {
                pendingAction_ = WindowChromeAction::Close;
            }

            event.StopPropagation();
            return;
        }
    }

private:
    std::optional<WindowChromeAction>& pendingAction_;
};

/**
 * Captures modeling shelf toolbar selections from RmlUi controls.
 */
class ModelingModeShelfControlSelectionListener final : public Rml::EventListener {
public:
    ModelingModeShelfControlSelectionListener(
            std::optional<std::string>& pendingControl,
            std::optional<std::string>& pendingCommand)
        : pendingControl_(pendingControl)
        , pendingCommand_(pendingCommand) {}

    void ProcessEvent(Rml::Event& event) override {
        for (Rml::Element* element = event.GetTargetElement(); element != nullptr; element = element->GetParentNode()) {
            const Rml::String command = element->GetAttribute<Rml::String>(
                    kModelingModeShelfCommandAttribute.data(),
                    "");
            if (!command.empty()) {
                pendingCommand_ = command;
                event.StopPropagation();
                return;
            }

            const Rml::String overflowToggle =
                    element->GetAttribute<Rml::String>(kModelingModeShelfOverflowToggleAttribute.data(), "");
            if (!overflowToggle.empty()) {
                pendingControl_ = std::string(modelingModeShelfOverflowToggleControlId());
                event.StopPropagation();
                return;
            }

            const Rml::String control = element->GetAttribute<Rml::String>(
                    kModelingModeShelfControlAttribute.data(),
                    "");
            if (!control.empty()) {
                pendingControl_ = control;
                event.StopPropagation();
                return;
            }
        }
    }

private:
    std::optional<std::string>& pendingControl_;
    std::optional<std::string>& pendingCommand_;
};

int rmlModifiersFromSdl() {
    const SDL_Keymod mod = SDL_GetModState();
    int result = 0;
    if ((mod & SDL_KMOD_CTRL) != 0) {
        result |= Rml::Input::KM_CTRL;
    }
    if ((mod & SDL_KMOD_SHIFT) != 0) {
        result |= Rml::Input::KM_SHIFT;
    }
    if ((mod & SDL_KMOD_ALT) != 0) {
        result |= Rml::Input::KM_ALT;
    }
    if ((mod & SDL_KMOD_GUI) != 0) {
        result |= Rml::Input::KM_META;
    }
    return result;
}

int rmlMouseButtonIndex(Uint8 button) {
    if (button == SDL_BUTTON_LEFT) {
        return 0;
    }
    if (button == SDL_BUTTON_RIGHT) {
        return 1;
    }
    return static_cast<int>(button);
}

bool isRmlDebuggerToggleShortcut(const SDL_Event& event) {
    if (event.type != SDL_EVENT_KEY_DOWN || event.key.repeat || event.key.key != SDLK_I) {
        return false;
    }

    const SDL_Keymod mod = SDL_GetModState();
    return (mod & SDL_KMOD_CTRL) != 0 &&
            (mod & SDL_KMOD_SHIFT) != 0 &&
            (mod & (SDL_KMOD_ALT | SDL_KMOD_GUI)) == 0;
}

bool processRmlContextEventConsumed(
        Rml::Context& context,
        const SDL_Event& event,
        float windowToFramebufferScaleX,
        float windowToFramebufferScaleY) {
    auto mousePixelX = [&](float x) {
        return static_cast<int>(std::lround(x * windowToFramebufferScaleX));
    };
    auto mousePixelY = [&](float y) {
        return static_cast<int>(std::lround(y * windowToFramebufferScaleY));
    };

    switch (event.type) {
    case SDL_EVENT_MOUSE_MOTION:
        return !context.ProcessMouseMove(mousePixelX(event.motion.x), mousePixelY(event.motion.y), rmlModifiersFromSdl());
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        context.ProcessMouseMove(mousePixelX(event.button.x), mousePixelY(event.button.y), rmlModifiersFromSdl());
        return !context.ProcessMouseButtonDown(rmlMouseButtonIndex(event.button.button), rmlModifiersFromSdl());
    case SDL_EVENT_MOUSE_BUTTON_UP:
        context.ProcessMouseMove(mousePixelX(event.button.x), mousePixelY(event.button.y), rmlModifiersFromSdl());
        return !context.ProcessMouseButtonUp(rmlMouseButtonIndex(event.button.button), rmlModifiersFromSdl());
    case SDL_EVENT_MOUSE_WHEEL: {
        const float direction = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;
        context.ProcessMouseMove(mousePixelX(event.wheel.mouse_x), mousePixelY(event.wheel.mouse_y), rmlModifiersFromSdl());
        return !context.ProcessMouseWheel(event.wheel.y * direction, rmlModifiersFromSdl());
    }
    case SDL_EVENT_TEXT_INPUT:
        if (event.text.text != nullptr) {
            return !context.ProcessTextInput(Rml::String(event.text.text));
        }
        break;
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        return !context.ProcessMouseLeave();
    default:
        break;
    }
    return false;
}

} // namespace

RmlUiContextHost::RmlUiContextHost() = default;

RmlUiContextHost::~RmlUiContextHost() {
    shutdown();
}

bool RmlUiContextHost::initialize(
        SDL_Window* window,
        RenderSystem& renderSystem,
        const std::filesystem::path& executableDir,
        std::string utilityRcss,
        std::string iconRcss,
        std::string debugPaletteRml,
        std::string initialTextureSource,
        RenderTextureHandle initialTextureHandle,
        std::uint32_t initialTextureWidth,
        std::uint32_t initialTextureHeight) {
    window_ = window;
    renderer_.initialize(renderSystem.engine(), renderSystem.rmlUiMaterial(), renderSystem.textures());
    renderer_.registerTextureSource(
            std::move(initialTextureSource),
            initialTextureHandle,
            initialTextureWidth,
            initialTextureHeight);

    systemInterface_ = std::make_unique<RmlSystemInterface>();
    auto fileInterface = std::make_unique<RmlFileInterface>(executableDir);
    fileInterface->setVirtualFile(std::string(kGeneratedUtilityRcssPath), std::move(utilityRcss));
    fileInterface->setVirtualFile(std::string(kGeneratedIconRcssPath), std::move(iconRcss));
    fileInterface_ = std::move(fileInterface);

    Rml::SetSystemInterface(systemInterface_.get());
    Rml::SetFileInterface(fileInterface_.get());
    Rml::SetRenderInterface(&renderer_);
    if (!Rml::Initialise()) {
        LOG_ERROR("RmlUi initialization failed");
        return false;
    }
    rmlInitialized_ = true;

    const std::filesystem::path interFontPath = executableDir / "fonts" / "Inter.ttf";
    if (!Rml::LoadFontFace(interFontPath.string())) {
        LOG_WARN("Unable to load RmlUi font face {}", interFontPath.string());
    }

    const std::filesystem::path novaMonoFontPath = executableDir / "fonts" / "NovaMono.ttf";
    if (!loadFontFaceFromMemory(novaMonoFontPath, "NovaMono")) {
        LOG_WARN("Unable to load RmlUi font face {}", novaMonoFontPath.string());
    }

    context_ = Rml::CreateContext("quader-main", Rml::Vector2i(1, 1));
    if (!context_) {
        LOG_ERROR("RmlUi context creation failed");
        shutdown();
        return false;
    }

    const std::filesystem::path documentPath = executableDir / "ui" / "rml" / "viewport-shell.rml";
    document_ = context_->LoadDocument(documentPath.string());
    if (!document_) {
        LOG_ERROR("RmlUi viewport shell document failed to load from {}", documentPath.string());
        shutdown();
        return false;
    }

    if (Rml::Element* debugPaletteSlot = document_->GetElementById("debug-palette-slot")) {
        debugPaletteSlot->SetInnerRML(debugPaletteRml);
    }

    windowChromeActionListener_ = std::make_unique<WindowChromeActionListener>(pendingWindowChromeAction_);
    document_->AddEventListener("click", windowChromeActionListener_.get());
    modelingModeShelfControlSelectionListener_ =
            std::make_unique<ModelingModeShelfControlSelectionListener>(
                    pendingModelingModeShelfControlSelection_,
                    pendingModelingModeShelfCommand_);
    document_->AddEventListener("click", modelingModeShelfControlSelectionListener_.get());
    debugPaletteCopyListener_ = std::make_unique<DebugPaletteCopyListener>();
    document_->AddEventListener("click", debugPaletteCopyListener_.get());
    document_->Show();

    debuggerContext_ = Rml::CreateContext("quader-debugger", Rml::Vector2i(1, 1));
    if (!debuggerContext_) {
        LOG_WARN("RmlUi debugger context creation failed");
    } else if (!Rml::Debugger::Initialise(debuggerContext_)) {
        LOG_WARN("RmlUi debugger initialization failed");
    } else {
        rmlDebuggerInitialized_ = true;
        Rml::Debugger::SetContext(context_);
        Rml::Debugger::SetVisible(false);
    }

    initialized_ = true;
    return true;
}

void RmlUiContextHost::shutdown() {
    if (!initialized_ && !rmlInitialized_) {
        return;
    }

    if (document_) {
        if (debugPaletteCopyListener_) {
            document_->RemoveEventListener("click", debugPaletteCopyListener_.get());
        }
        if (windowChromeActionListener_) {
            document_->RemoveEventListener("click", windowChromeActionListener_.get());
        }
        if (modelingModeShelfControlSelectionListener_) {
            document_->RemoveEventListener("click", modelingModeShelfControlSelectionListener_.get());
        }
    }
    debugPaletteCopyListener_.reset();
    windowChromeActionListener_.reset();
    modelingModeShelfControlSelectionListener_.reset();
    pendingWindowChromeAction_.reset();
    pendingModelingModeShelfControlSelection_.reset();
    pendingModelingModeShelfCommand_.reset();
    document_ = nullptr;
    if (rmlDebuggerInitialized_) {
        Rml::Debugger::Shutdown();
        rmlDebuggerInitialized_ = false;
    }
    debuggerContext_ = nullptr;
    context_ = nullptr;
    if (rmlInitialized_) {
        Rml::Shutdown();
        rmlInitialized_ = false;
    }
    Rml::SetRenderInterface(nullptr);
    Rml::SetFileInterface(nullptr);
    Rml::SetSystemInterface(nullptr);
    renderer_.shutdown();
    fileInterface_.reset();
    systemInterface_.reset();
    window_ = nullptr;
    initialized_ = false;
}

bool RmlUiContextHost::processEvent(
        const SDL_Event& event,
        float windowToFramebufferScaleX,
        float windowToFramebufferScaleY) {
    if (!context_) {
        return false;
    }
    const std::optional<SDL_WindowID> targetWindow = sdlEventWindowId(event);
    if (targetWindow && window_ && *targetWindow != SDL_GetWindowID(window_)) {
        return false;
    }
    if (isRmlDebuggerToggleShortcut(event)) {
        if (rmlDebuggerInitialized_) {
            Rml::Debugger::SetVisible(!Rml::Debugger::IsVisible());
            context_->Update();
            if (debuggerContext_) {
                debuggerContext_->Update();
            }
        }
        return true;
    }

    if (debuggerVisible() && debuggerContext_ &&
            processRmlContextEventConsumed(*debuggerContext_, event, windowToFramebufferScaleX, windowToFramebufferScaleY)) {
        return true;
    }
    return processRmlContextEventConsumed(*context_, event, windowToFramebufferScaleX, windowToFramebufferScaleY);
}

bool RmlUiContextHost::debuggerVisible() const {
    return rmlDebuggerInitialized_ && Rml::Debugger::IsVisible();
}

bool RmlUiContextHost::textInputActive() const {
    if (!context_) {
        return false;
    }

    for (Rml::Element* element = context_->GetFocusElement(); element != nullptr; element = element->GetParentNode()) {
        const Rml::String tagName = element->GetTagName();
        const std::string_view tag(tagName.data(), tagName.size());
        if (tag == "input" || tag == "textarea" || tag == "select") {
            return true;
        }

        const Rml::String contentEditable = element->GetAttribute<Rml::String>("contenteditable", "");
        if (contentEditable == "true" || contentEditable == "plaintext-only") {
            return true;
        }
    }

    return false;
}

void RmlUiContextHost::setDensityIndependentPixelRatio(float dpRatio) {
    if (!context_ || !std::isfinite(dpRatio) || dpRatio <= 0.0f || std::abs(dpRatio - dpRatio_) <= 0.001f) {
        return;
    }

    dpRatio_ = dpRatio;
    context_->SetDensityIndependentPixelRatio(dpRatio_);
    if (debuggerContext_) {
        debuggerContext_->SetDensityIndependentPixelRatio(dpRatio_);
    }
}

void RmlUiContextHost::updateLayout(std::uint32_t framebufferWidth, std::uint32_t framebufferHeight) {
    if (!context_) {
        return;
    }

    const std::uint32_t width = std::max(1u, framebufferWidth);
    const std::uint32_t height = std::max(1u, framebufferHeight);
    context_->SetDimensions(Rml::Vector2i(
            static_cast<int>(width),
            static_cast<int>(height)));
    context_->Update();
    if (debuggerContext_) {
        debuggerContext_->SetDimensions(Rml::Vector2i(
                static_cast<int>(width),
                static_cast<int>(height)));
        debuggerContext_->Update();
    }
}

void RmlUiContextHost::prepareRenderData(std::uint32_t framebufferWidth, std::uint32_t framebufferHeight) {
    if (!context_) {
        return;
    }

    renderer_.beginFrame(framebufferWidth, framebufferHeight);
    context_->Render();
    if (debuggerVisible() && debuggerContext_) {
        debuggerContext_->Render();
    }
}

void RmlUiContextHost::render(RenderSystem& renderSystem) {
    renderer_.render(renderSystem.renderer());
}

void RmlUiContextHost::registerTextureSource(
        std::string source,
        RenderTextureHandle textureHandle,
        std::uint32_t width,
        std::uint32_t height) {
    renderer_.registerTextureSource(std::move(source), textureHandle, width, height);
}

void RmlUiContextHost::clearTextureSources() {
    renderer_.clearTextureSources();
}

void RmlUiContextHost::setElementDisplay(const std::string& elementId, const std::string& display) {
    if (!document_) {
        return;
    }

    if (Rml::Element* element = document_->GetElementById(elementId)) {
        element->SetProperty("display", display);
    }
}

void RmlUiContextHost::setElementInnerRml(const std::string& elementId, const std::string& innerRml) {
    if (!document_) {
        return;
    }

    if (Rml::Element* element = document_->GetElementById(elementId)) {
        element->SetInnerRML(innerRml);
    }
}

std::optional<RmlElementRect> RmlUiContextHost::measureElementById(const std::string& elementId) const {
    if (!document_) {
        return std::nullopt;
    }

    Rml::Element* element = document_->GetElementById(elementId);
    if (!element) {
        return std::nullopt;
    }

    const int width = static_cast<int>(std::lround(element->GetOffsetWidth()));
    const int height = static_cast<int>(std::lround(element->GetOffsetHeight()));
    if (width <= 0 || height <= 0) {
        return std::nullopt;
    }

    return RmlElementRect{
            .left = static_cast<int>(std::lround(element->GetAbsoluteLeft())),
            .top = static_cast<int>(std::lround(element->GetAbsoluteTop())),
            .width = width,
            .height = height,
    };
}

std::optional<WindowChromeAction> RmlUiContextHost::takeWindowChromeAction() {
    std::optional<WindowChromeAction> action = pendingWindowChromeAction_;
    pendingWindowChromeAction_.reset();
    return action;
}

std::optional<std::string> RmlUiContextHost::takeModelingModeShelfControlSelection() {
    std::optional<std::string> control = std::move(pendingModelingModeShelfControlSelection_);
    pendingModelingModeShelfControlSelection_.reset();
    return control;
}

std::optional<std::string> RmlUiContextHost::takeModelingModeShelfCommand() {
    std::optional<std::string> command = std::move(pendingModelingModeShelfCommand_);
    pendingModelingModeShelfCommand_.reset();
    return command;
}

} // namespace quader
