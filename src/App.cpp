#include "App.h"
#include "crimson-renderer/Diagnostics.h"
#include "crimson-renderer/ImGuiBgfx.h"
#include "crimson-renderer/Renderer.h"
#include "assets/AssetLibrary.h"
#include "input/Navigation.h"
#include "platform/WindowIcon.h"
#include "tools/Tools.h"
#include "editor/EditorSession.h"
#include "editor/ResourceRuntime.h"
#include "resources/ResourceManager.h"
#include "app/DiagnosticSession.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <filesystem>
#include <memory>
#include <stdexcept>
namespace crimson {
namespace {
class SdlLifetime {
public:
    SdlLifetime() {
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) throw std::runtime_error(SDL_GetError());
    }
    ~SdlLifetime() { SDL_Quit(); }
};

class ImGuiLifetime {
public:
    explicit ImGuiLifetime(SDL_Window* window, const std::filesystem::path& root) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        const auto ini = (root / "crimson_ui.ini").u8string();
        iniFilename_.assign(reinterpret_cast<const char*>(ini.data()), ini.size());
        io.IniFilename = iniFilename_.c_str();
        if (!ImGui_ImplSDL3_InitForD3D(window)) {
            ImGui::DestroyContext();
            throw std::runtime_error("Could not initialize ImGui SDL3 input.");
        }
    }
    ~ImGuiLifetime() {
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
    }
private:
    std::string iniFilename_;
};

}
int run(const AppOptions& options) {
    const auto root = options.projectRoot.empty() ? std::filesystem::path(CC_PROJECT_ROOT) :
        std::filesystem::absolute(std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(options.projectRoot.data()), options.projectRoot.size())));
    std::filesystem::create_directories(root);
    if ((options.iblSmoke || options.modelSmoke || options.scenario == "model-sphere") &&
        std::filesystem::equivalent(root, std::filesystem::path(CC_PROJECT_ROOT)))
        throw std::runtime_error("Asset smoke checks cannot use the authored project root. Choose a disposable directory.");
    Diagnostics diagnostics;
    diagnostics.log("crimson-renderer starting (Crimson Engine, Direct3D 11).");
    SdlLifetime sdl;
    const auto windowFlags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
        (options.hidden ? SDL_WINDOW_HIDDEN : 0);
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
        SDL_CreateWindow("crimson-renderer | Crimson Engine", static_cast<int>(options.width),
                         static_cast<int>(options.height), windowFlags), &SDL_DestroyWindow);
    if (!window) throw std::runtime_error(std::string("SDL window creation failed: ") + SDL_GetError());
    SDL_SetWindowMinimumSize(window.get(), 640, 480);
    if (!applyWindowClassIcons(window.get())) {
        diagnostics.log("Window icon assignment incomplete; Windows class icon fallback remains available.");
    }

    {
        ResourceManager resources;
        Renderer renderer(window.get(), &diagnostics);
        ImGuiLifetime imgui(window.get(), root);
        ImGuiBgfx toolsRenderer;
        const bool diagnosticMode = options.scenario != "default" || options.editorSmoke || options.iblSmoke || options.modelSmoke;
        AssetLibrary assets(root,diagnosticMode?nullptr:&resources);
        Navigation navigation(window.get());
        applyToolsStyle();
        std::unique_ptr<ResourceRuntime> resourceRuntime;
        if(!diagnosticMode)resourceRuntime=std::make_unique<ResourceRuntime>(root,resources,assets,renderer,toolsRenderer);
        std::unique_ptr<EditorSession> editor;
        std::unique_ptr<DiagnosticSession> diagnostic;
        if (diagnosticMode) diagnostic=std::make_unique<DiagnosticSession>(options,root,assets,renderer,diagnostics);
        else editor=std::make_unique<EditorSession>(root,assets,renderer);
        uint32_t frame=0,bgfxFrame=0;
        bool running=true,captureRequested=false,cycleMinimized=false,pendingQuit=false;
        uint64_t cycleRestoreTime=0;
        while (running && (!options.frameLimit || frame < options.frameLimit)) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                ImGui_ImplSDL3_ProcessEvent(&event);
                navigation.handleEvent(event);
                if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST || event.type == SDL_EVENT_WINDOW_HIDDEN) {
                    if (editor) editor->cancelInteraction(renderer);
                }
                if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) pendingQuit=true;
            }
            if (pendingQuit && diagnosticMode) break;
            if (options.windowCycle && frame == 10 && !cycleMinimized) {
                SDL_MinimizeWindow(window.get());
                cycleMinimized = true;
                cycleRestoreTime = SDL_GetTicks() + 150;
            }
            if (cycleRestoreTime && SDL_GetTicks() >= cycleRestoreTime) {
                SDL_RestoreWindow(window.get());
                cycleRestoreTime = 0;
            }
            if (options.windowCycle && frame == 25) SDL_MaximizeWindow(window.get());
            if (options.windowCycle && frame == 40) SDL_RestoreWindow(window.get());

            if (options.resizeWidth && frame == 10) {
                SDL_SetWindowSize(window.get(), static_cast<int>(options.resizeWidth), static_cast<int>(options.resizeHeight));
            }
            int pixelWidth = 0, pixelHeight = 0;
            SDL_GetWindowSizeInPixels(window.get(), &pixelWidth, &pixelHeight);
            const bool minimized = (SDL_GetWindowFlags(window.get()) & SDL_WINDOW_MINIMIZED) != 0;
            if (minimized || pixelWidth <= 0 || pixelHeight <= 0) {
                SDL_Delay(16);
                continue;
            }
            renderer.resize(static_cast<uint32_t>(pixelWidth), static_cast<uint32_t>(pixelHeight));
            if(resourceRuntime)resourceRuntime->preFrame(editor.get(),bgfxFrame);
            else assets.processImports();

            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();
            if (editor) {
                editor->frame(assets,renderer,navigation,window.get(),bgfxFrame,pendingQuit);
                pendingQuit=false;
                running=!editor->exitAccepted();
            } else diagnostic->frame(assets,renderer,navigation,window.get());
            ImGui::Render();
            bgfx::setViewClear(ToolsView,BGFX_CLEAR_COLOR,0x202020ff);
            toolsRenderer.render(ImGui::GetDrawData());
            if (!captureRequested && !options.capture.empty() && frame+5 >= options.frameLimit) {
                bgfx::requestScreenShot(BGFX_INVALID_HANDLE,options.capture.c_str()); captureRequested=true;
            }
            bgfxFrame=bgfx::frame();
            ++frame;
        }
        if(resourceRuntime)resourceRuntime->stop();else resources.stop();
        bgfxFrame=renderer.drainPending(bgfxFrame);
        // Let asynchronous screenshot requests and resource destruction finish before shutdown.
        if (captureRequested) {
            for (int flush = 0; flush < 8 && !diagnostics.captureSucceeded() && !diagnostics.captureFailed(); ++flush) bgfx::frame();
        }
        diagnostics.log("Rendered " + std::to_string(frame) + " frames at " + std::to_string(renderer.width()) +
                        "x" + std::to_string(renderer.height()) + ".");
        // Destruction order: navigation/assets -> tools renderer -> ImGui platform/context -> bgfx -> SDL window.
    }
    const bool failed = diagnostics.hadRenderError() || diagnostics.captureFailed() ||
        (!options.capture.empty() && !diagnostics.captureSucceeded());
    diagnostics.log(failed ? "Renderer verification failed." : "Renderer shut down cleanly.");
    return failed ? 1 : 0;
}

} // namespace crimson
