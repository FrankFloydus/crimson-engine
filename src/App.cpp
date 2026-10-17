#include "App.h"
#include "crimson-renderer/Diagnostics.h"
#include "crimson-renderer/ImGuiBgfx.h"
#include "crimson-renderer/Renderer.h"
#include "assets/AssetLibrary.h"
#include "input/Navigation.h"
#include "platform/WindowIcon.h"
#include "platform/WindowPlacement.h"
#include "tools/Tools.h"
#include "editor/EditorSession.h"
#include "editor/EditorPreferences.h"
#include "editor/ResourceRuntime.h"
#include "resources/ResourceManager.h"
#include "app/DiagnosticSession.h"
#include "app/FrameProfile.h"
#include "gameplay/Attributes.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <implot.h>
#include <imgui_impl_sdl3.h>
#include <filesystem>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <cstdint>
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
        ImPlot::CreateContext();
        auto& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_ViewportsEnable;
        io.ConfigDpiScaleFonts=true;
        io.ConfigDpiScaleViewports=true;
        const auto ini = (root / "crimson_ui.ini").u8string();
        iniFilename_.assign(reinterpret_cast<const char*>(ini.data()), ini.size());
        io.IniFilename = iniFilename_.c_str();
        if (!ImGui_ImplSDL3_InitForD3D(window)) {
            ImPlot::DestroyContext();
            ImGui::DestroyContext();
            throw std::runtime_error("Could not initialize ImGui SDL3 input.");
        }
        // Dock builders must see the saved nodes before the first editor frame.
        ImGui::LoadIniSettingsFromDisk(iniFilename_.c_str());
    }
    void saveLayout() {
        ImGui::SaveIniSettingsToDisk(iniFilename_.c_str());
        // The native viewports are destroyed before the ImGui context. Keep
        // their final live layout rather than saving a second shutdown state.
        ImGui::GetIO().IniFilename=nullptr;
    }
    ~ImGuiLifetime() {
        ImGui_ImplSDL3_Shutdown();
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
    }
private:
    std::string iniFilename_;
};

SDL_Window* platformWindow(const ImGuiViewport* viewport) {
    return viewport?SDL_GetWindowFromID(static_cast<SDL_WindowID>(reinterpret_cast<std::uintptr_t>(viewport->PlatformHandle))):nullptr;
}
bool editorHasFocus(SDL_Window* mainWindow) {
    auto* focused=SDL_GetKeyboardFocus();
    if(!focused)return false;
    if(focused==mainWindow)return true;
    return ImGui::FindViewportByPlatformHandle(reinterpret_cast<void*>(static_cast<std::uintptr_t>(SDL_GetWindowID(focused))))!=nullptr;
}
bool secondaryWindowDrawable() {
    const auto& viewports=ImGui::GetPlatformIO().Viewports;
    for(int index=1;index<viewports.Size;++index)if(auto* window=platformWindow(viewports[index])) {
        const auto flags=SDL_GetWindowFlags(window);int width{},height{};
        if(!(flags&(SDL_WINDOW_MINIMIZED|SDL_WINDOW_HIDDEN)) && SDL_GetWindowSizeInPixels(window,&width,&height) && width>0 && height>0)return true;
    }
    return false;
}

}
int run(const AppOptions& options) {
    // Validate the immutable native vocabulary before any scene is loaded.
    (void)nativeAttributeCatalog();
    const auto root = options.projectRoot.empty() ? std::filesystem::path(CC_PROJECT_ROOT) :
        std::filesystem::absolute(std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(options.projectRoot.data()), options.projectRoot.size())));
    std::filesystem::create_directories(root);
    if ((options.iblSmoke || options.modelSmoke || options.scenario == "model-sphere") &&
        std::filesystem::equivalent(root, std::filesystem::path(CC_PROJECT_ROOT)))
        throw std::runtime_error("Asset smoke checks cannot use the authored project root. Choose a disposable directory.");
    Diagnostics diagnostics;
    SdlLifetime sdl;
    const EditorPreferences startupPreferences;
    const auto graphicsBackend=options.rendererBackend.value_or(startupPreferences.values().graphicsBackend);
    diagnostics.log(std::string("crimson-renderer starting (Crimson Engine, ")+graphicsBackendName(graphicsBackend)+").");
    if(!startupPreferences.status().empty())diagnostics.log(startupPreferences.status());
    const bool diagnosticMode = options.scenario != "default" || options.editorSmoke || options.iblSmoke || options.modelSmoke;
    const bool persistWindow=!diagnosticMode && !options.hidden && !options.frameLimit && !options.windowCycle && !options.resizeWidth;
    const auto windowPlacement=root/"crimson_window.json";
    const auto windowFlags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
        (options.hidden || persistWindow ? SDL_WINDOW_HIDDEN : 0);
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
        SDL_CreateWindow("crimson-renderer | Crimson Engine", static_cast<int>(options.width),
                         static_cast<int>(options.height), windowFlags), &SDL_DestroyWindow);
    if (!window) throw std::runtime_error(std::string("SDL window creation failed: ") + SDL_GetError());
    SDL_SetWindowMinimumSize(window.get(), 640, 480);
    if (!applyWindowClassIcons(window.get())) {
        diagnostics.log("Window icon assignment incomplete; Windows class icon fallback remains available.");
    }
    if (persistWindow) {
        if (!restoreWindowPlacement(window.get(),windowPlacement)) SDL_ShowWindow(window.get());
        SDL_SyncWindow(window.get());
    }

    {
        ResourceManager resources;
        Renderer renderer(window.get(), &diagnostics, graphicsBackend);
        ImGuiLifetime imgui(window.get(), root);
        ImGuiBgfx toolsRenderer([&diagnostics](const std::string& message){diagnostics.log(message);});
        AssetLibrary assets(root,diagnosticMode?nullptr:&resources);
        Navigation navigation(window.get());
        navigation.setDesktopCoordinates((ImGui::GetIO().ConfigFlags&ImGuiConfigFlags_ViewportsEnable)!=0);
        applyToolsStyle();
        std::unique_ptr<ResourceRuntime> resourceRuntime;
        if(!diagnosticMode)resourceRuntime=std::make_unique<ResourceRuntime>(root,resources,assets,renderer,toolsRenderer);
        std::unique_ptr<EditorSession> editor;
        std::unique_ptr<DiagnosticSession> diagnostic;
        if (diagnosticMode) diagnostic=std::make_unique<DiagnosticSession>(options,root,assets,renderer,diagnostics);
        else editor=std::make_unique<EditorSession>(root,assets,renderer);
        uint32_t frame=0,bgfxFrame=0;
        std::unique_ptr<FrameProfile> frameProfile;
        if(!options.frameProfile.empty())frameProfile=std::make_unique<FrameProfile>(std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(options.frameProfile.data()),options.frameProfile.size())));
        bool running=true,captureRequested=false,cycleMinimized=false,pendingQuit=false;
        uint64_t cycleRestoreTime=0;
        while (running && (!options.frameLimit || frame < options.frameLimit)) {
            const auto frameStarted=std::chrono::steady_clock::now();
            SDL_Event event;
            bool focusLossEvent=false;
            while (SDL_PollEvent(&event)) {
                ImGui_ImplSDL3_ProcessEvent(&event);
                if(!editor || !editor->handleEvent(event))navigation.handleEvent(event);
                if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST || event.type == SDL_EVENT_WINDOW_HIDDEN) {
                    focusLossEvent=true;
                }
                if (event.type == SDL_EVENT_QUIT || (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                    event.window.windowID==SDL_GetWindowID(window.get()))) pendingQuit=true;
            }
            const bool focused=editorHasFocus(window.get());
            if(editor && focusLossEvent && !focused)editor->cancelInteraction(renderer);
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
            // Exit may need the scene/effect dirty-document dialogs. Restore
            // the host instead of leaving the request behind a minimized loop.
            if (pendingQuit && editor && (SDL_GetWindowFlags(window.get()) & SDL_WINDOW_MINIMIZED))
                SDL_RestoreWindow(window.get());
            int pixelWidth = 0, pixelHeight = 0;
            SDL_GetWindowSizeInPixels(window.get(), &pixelWidth, &pixelHeight);
            const bool minimized = (SDL_GetWindowFlags(window.get()) & SDL_WINDOW_MINIMIZED) != 0;
            const bool mainDrawable=!minimized && pixelWidth>0 && pixelHeight>0;
            const bool anyDrawable=mainDrawable || secondaryWindowDrawable();
            if(editor)editor->setSimulationSuspended(!anyDrawable || !focused);
            if (!anyDrawable) {
                SDL_Delay(16);
                continue;
            }
            renderer.resize(mainDrawable?static_cast<uint32_t>(pixelWidth):0,mainDrawable?static_cast<uint32_t>(pixelHeight):0);
            if(resourceRuntime)resourceRuntime->preFrame(editor.get(),bgfxFrame);
            else assets.processImports();

            toolsRenderer.newFrame(bgfxFrame);
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();
            if (editor) {
                editor->frame(assets,renderer,navigation,window.get(),bgfxFrame,pendingQuit);
                pendingQuit=false;
                running=!editor->exitAccepted();
            } else { renderer.advanceStreaming();diagnostic->frame(assets,renderer,navigation,window.get()); }
            ImGui::Render();
            bgfx::setViewClear(ToolsView,BGFX_CLEAR_COLOR,0x202020ff);
            toolsRenderer.render(ImGui::GetDrawData());
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            if (!captureRequested && !options.capture.empty() && frame+5 >= options.frameLimit) {
                bgfx::requestScreenShot(BGFX_INVALID_HANDLE,options.capture.c_str()); captureRequested=true;
            }
            const auto cpuSubmitted=std::chrono::steady_clock::now();
            bgfxFrame=bgfx::frame();
            const bool nativeSynchronized=toolsRenderer.postFrame(bgfxFrame,!mainDrawable);
            if(!mainDrawable && !nativeSynchronized)SDL_Delay(16);
            const FrameTiming frameTiming{
                std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-frameStarted).count(),
                std::chrono::duration<double,std::milli>(cpuSubmitted-frameStarted).count()};
            if(editor)editor->setFrameTiming(frameTiming);
            if(frameProfile)frameProfile->record(frameTiming,navigation.active(),static_cast<std::uint32_t>(pixelWidth),static_cast<std::uint32_t>(pixelHeight));
            ++frame;
        }
        if(resourceRuntime)resourceRuntime->stop();else resources.stop();
        if (persistWindow && !saveWindowPlacement(window.get(),windowPlacement))
            diagnostics.log("Could not save editor window placement.");
        imgui.saveLayout();
        bgfxFrame=renderer.drainPending(bgfxFrame);
        // Let asynchronous screenshot requests and resource destruction finish before shutdown.
        if (captureRequested) {
            for (int flush = 0; flush < 8 && !diagnostics.captureSucceeded() && !diagnostics.captureFailed(); ++flush) bgfx::frame();
        }
        toolsRenderer.shutdown();
        for (unsigned drain=0;drain<8 && toolsRenderer.hasPendingRetirements();++drain) {
            bgfxFrame=bgfx::frame();
            toolsRenderer.postFrame(bgfxFrame);
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
