#include "App.h"
#include "crimson-renderer/Diagnostics.h"
#include "crimson-renderer/Renderer.h"
#include "crimson-renderer/ImGuiBgfx.h"
#include "tools/Tools.h"
#include "assets/AssetLibrary.h"
#include "input/Navigation.h"
#include "platform/WindowIcon.h"
#include "ibl_smoke.h"

#include <SDL3/SDL.h>
#include <imgui_impl_sdl3.h>
#include <imgui.h>
#include <lodepng.h>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace crimson {
namespace {

uint32_t parseUnsigned(std::string_view text, uint32_t maximum) {
    uint32_t result{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || result > maximum) {
        throw std::runtime_error("Invalid numeric argument: " + std::string(text));
    }
    return result;
}

void parseSize(std::string_view text, uint32_t& width, uint32_t& height) {
    const auto split = text.find_first_of("xX");
    if (split == std::string_view::npos) throw std::runtime_error("Use WIDTHxHEIGHT for window size.");
    width = parseUnsigned(text.substr(0, split), 8192);
    height = parseUnsigned(text.substr(split + 1), 8192);
    if (width < 320 || height < 240) throw std::runtime_error("Minimum window size is 320x240.");
}

SceneSettings scenarioSettings(const std::string& scenario) {
    SceneSettings scene;
    if (scenario == "default") return scene;
    if (scenario == "rough") { scene.material.roughness = 1.0f; return scene; }
    if (scenario == "metal") { scene.material.metallic = 1.0f; scene.material.baseColor = {0.95f, 0.7f, 0.25f}; return scene; }
    if (scenario == "unlit") {
        scene.environment.enabled = false;
        scene.directional.enabled = false;
        for (auto& light : scene.points) light.enabled = false;
        return scene;
    }
    if (scenario.starts_with("ibl-")) {
        scene.directional.enabled = false;
        for (auto& light : scene.points) light.enabled = false;
        scene.material.baseColor = {.8f,.8f,.8f};
        scene.material.roughness = .12f;
        if (scenario == "ibl-zero-exposure") scene.exposure = 0;
        if (scenario == "ibl-metal" || scenario == "ibl-rough") scene.material.metallic = 1;
        if (scenario == "ibl-rough") scene.material.roughness = .85f;
        if (scenario == "ibl-extreme") {
            scene.material.metallic = 1; scene.material.roughness = 0;
            scene.material.normalStrength = 2; scene.environment.intensity = 10; scene.exposure = 100;
            scene.camera.position = {0,1.02f,1.02f};
        }
        if (scenario == "ibl-dielectric" || scenario == "ibl-metal" || scenario == "ibl-rough" ||
            scenario == "ibl-normal" || scenario == "ibl-neutral" || scenario == "ibl-zero-normal" ||
            scenario == "ibl-reopen" || scenario == "ibl-extreme" || scenario == "ibl-zero-exposure") return scene;
    }
    if (scenario == "near-light" || scenario == "far-light") {
        scene.directional.enabled = false;
        scene.points[1].enabled = false;
        scene.points[0].color = {1.0f, 1.0f, 1.0f};
        scene.points[0].intensity = 10.0f;
        scene.points[0].position = {0.0f, 0.0f, scenario == "near-light" ? 3.0f : 6.0f};
        return scene;
    }
    if (scenario == "extreme") {
        scene.material.metallic = 1.0f;
        scene.material.roughness = 0.0f;
        scene.points[0].position = {0.0f, 0.0f, 1.0f};
        scene.points[0].intensity = 100.0f;
        scene.directional.direction = {0.0f, 0.0f, 0.0f};
        return scene;
    }
    throw std::runtime_error("Unknown scenario: " + scenario);
}

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

void importSmokeTextures(AssetLibrary& assets, Material& material, const std::filesystem::path& root) {
    const auto folder = root / "build" / "smoke_sources";
    std::filesystem::create_directories(folder);
    const std::array<const char*, 3> names{"Prototype Checker.png", "Roughness Ramp.png", "Metalness Stripes.png"};
    for (size_t slot = 0; slot < names.size(); ++slot) {
        std::vector<uint8_t> pixels(64 * 64 * 4);
        for (uint32_t y = 0; y < 64; ++y) for (uint32_t x = 0; x < 64; ++x) {
            const size_t offset = (y * 64 + x) * 4;
            const bool checker = ((x / 8 + y / 8) % 2) != 0;
            if (slot == 0) {
                pixels[offset] = checker ? 210 : 240;
                pixels[offset + 1] = checker ? 28 : 215;
                pixels[offset + 2] = checker ? 20 : 175;
            } else {
                const auto value = static_cast<uint8_t>(slot == 1 ? 40 + x * 3 : checker ? 255 : 0);
                pixels[offset] = pixels[offset + 1] = pixels[offset + 2] = value;
            }
            pixels[offset + 3] = 255;
        }
        std::vector<uint8_t> png;
        const auto error = lodepng::encode(png, pixels, 64, 64);
        if (error) throw std::runtime_error("Could not create the texture smoke fixture.");
        const auto path = folder / names[slot];
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
        output.close();
        const auto* imported = assets.importTexture(path);
        if (!imported) throw std::runtime_error(assets.status());
        const auto kind = slot == 0 ? MaterialSlot::Albedo : slot == 1 ? MaterialSlot::Roughness : MaterialSlot::Metallic;
        if (!assets.assign(material, kind, imported->id)) throw std::runtime_error("Could not assign the smoke texture.");
    }
}

} // namespace

AppOptions parseOptions(int argc, char** argv) {
    AppOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument(argv[i]);
        if (argument == "--hidden") { options.hidden = true; continue; }
        if (argument == "--window-cycle") { options.windowCycle = true; continue; }
        if (argument == "--editor-smoke") { options.editorSmoke = true; continue; }
        if (argument == "--ibl-smoke") { options.iblSmoke = true; continue; }
        if (i + 1 >= argc) throw std::runtime_error("Missing value for " + std::string(argument));
        const std::string_view value(argv[++i]);
        if (argument == "--frames") options.frameLimit = parseUnsigned(value, 1000000);
        else if (argument == "--capture") options.capture = std::string(value);
        else if (argument == "--scenario") options.scenario = std::string(value);
        else if (argument == "--project-root") options.projectRoot = std::string(value);
        else if (argument == "--ui-scale") {
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), options.uiScale);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
                !std::isfinite(options.uiScale) || options.uiScale < 0.75f || options.uiScale > 3.0f) {
                throw std::runtime_error("UI scale must be between 0.75 and 3.");
            }
        }
        else if (argument == "--size") parseSize(value, options.width, options.height);
        else if (argument == "--resize") parseSize(value, options.resizeWidth, options.resizeHeight);
        else throw std::runtime_error("Unknown argument: " + std::string(argument));
    }
    if (!options.capture.empty()) {
        if (!options.frameLimit) options.frameLimit = 60;
        options.frameLimit = std::max(options.frameLimit, 12u);
    }
    if (options.resizeWidth && options.frameLimit && options.frameLimit < 24) options.frameLimit = 24;
    if (options.windowCycle && options.frameLimit && options.frameLimit < 60) options.frameLimit = 60;
    if (options.editorSmoke) options.frameLimit = std::max(40u, options.frameLimit);
    if (options.iblSmoke && options.projectRoot.empty()) throw std::runtime_error("--ibl-smoke requires an isolated --project-root.");
    scenarioSettings(options.scenario); // Reject invalid QA arguments before opening a window.
    return options;
}

int run(const AppOptions& options) {
    const auto root = options.projectRoot.empty() ? std::filesystem::path(CC_PROJECT_ROOT) :
        std::filesystem::absolute(std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(options.projectRoot.data()), options.projectRoot.size())));
    std::filesystem::create_directories(root);
    if (options.iblSmoke && std::filesystem::equivalent(root, std::filesystem::path(CC_PROJECT_ROOT)))
        throw std::runtime_error("--ibl-smoke cannot use the authored project root. Choose a disposable directory.");
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
        Renderer renderer(window.get(), &diagnostics);
        ImGuiLifetime imgui(window.get(), root);
        ImGuiBgfx toolsRenderer;
        AssetLibrary assets(root);
        Navigation navigation(window.get());
        SceneSettings settings = scenarioSettings(options.scenario);
        if (options.scenario == "default" || options.scenario == "ibl-reopen") assets.loadMaterial(settings.material);
        if (options.scenario == "default" || options.scenario.starts_with("ibl-")) assets.loadEnvironment(settings.environment);
        if (options.editorSmoke) importSmokeTextures(assets, settings.material, root);
        if (options.iblSmoke) {
            verifyIblAssets(assets, settings, root);
            diagnostics.log("PASS: Normal/HDR import, material factors, assignments, legacy loading and persistence.");
        }
        if (options.scenario == "ibl-normal" || options.scenario == "ibl-zero-normal" || options.scenario == "ibl-extreme")
            settings.material.normalMap = "assets/textures/IBL Detail Normal.png";
        if (options.scenario == "ibl-neutral") settings.material.normalMap = "assets/textures/IBL Neutral Normal.png";
        if (options.scenario == "ibl-zero-normal") settings.material.normalStrength = 0;
        if (options.scenario == "ibl-extreme") settings.environment.intensity = 10;
        if (!renderer.prepareEnvironment(root, {})) throw std::runtime_error(renderer.environmentStatus());
        if (!settings.environment.map.empty()) {
            const bool prepared = renderer.prepareEnvironment(root, settings.environment.map);
            if (options.iblSmoke && !prepared) throw std::runtime_error(renderer.environmentStatus());
        }
        if (options.iblSmoke) {
            if (renderer.prepareEnvironment(root, "assets/textures/does-not-exist.hdr"))
                throw std::runtime_error("Missing environment unexpectedly prepared.");
            if (!renderer.prepareEnvironment(root, settings.environment.map) || renderer.environmentStatus().starts_with("Environment:"))
                throw std::runtime_error("Environment failed to recover after a rejected source.");
            diagnostics.log("PASS: Rejected environment source retained working resources and recovered its status.");
        }
        std::string preparedEnvironment = settings.environment.map;
        diagnostics.log(renderer.environmentStatus());
        float appliedScale = 0.0f;
        uint32_t frame = 0;
        bool running = true;
        bool captureRequested = false;
        bool cycleMinimized = false;
        uint64_t cycleRestoreTime = 0;

        while (running && (!options.frameLimit || frame < options.frameLimit)) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                ImGui_ImplSDL3_ProcessEvent(&event);
                navigation.handleEvent(event);
                if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) running = false;
            }
            if (!running) break;

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
            assets.processImports();

            const float dpiScale = options.uiScale > 0 ? options.uiScale : std::clamp(SDL_GetWindowDisplayScale(window.get()), 0.75f, 3.0f);
            if (std::abs(dpiScale - appliedScale) > 0.01f) {
                applyToolsStyle(dpiScale);
                appliedScale = dpiScale;
            }
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();
            const auto editor = drawEditor(settings, renderer, assets, window.get());
            navigation.update(settings.camera, editor.viewport, ImGui::GetIO().DeltaTime);
            if (editor.materialChanged) assets.saveMaterial(settings.material);
            if (editor.environmentChanged) assets.saveEnvironment(settings.environment);
            if (preparedEnvironment != settings.environment.map) {
                renderer.prepareEnvironment(root, settings.environment.map);
                preparedEnvironment = settings.environment.map;
                diagnostics.log(renderer.environmentStatus());
            }

            ImGui::Render();
            if (editor.viewport.width > 0 && editor.viewport.height > 0)
                renderer.render(settings, assets.bindings(settings.material));
            bgfx::setViewClear(ToolsView, BGFX_CLEAR_COLOR, 0x171a20ff);
            toolsRenderer.render(ImGui::GetDrawData());
            if (!captureRequested && !options.capture.empty() && frame + 5 >= options.frameLimit) {
                bgfx::requestScreenShot(BGFX_INVALID_HANDLE, options.capture.c_str());
                captureRequested = true;
            }
            bgfx::frame();
            ++frame;
        }
        // Let asynchronous screenshot requests and resource destruction finish before shutdown.
        if (captureRequested) {
            for (int flush = 0; flush < 8 && !diagnostics.captureSucceeded() && !diagnostics.captureFailed(); ++flush) bgfx::frame();
        }
        diagnostics.log("Rendered " + std::to_string(frame) + " frames at " + std::to_string(renderer.width()) +
                        "x" + std::to_string(renderer.height()) + ".");
        if (options.editorSmoke) diagnostics.log("PASS: Managed imports and all three material maps assigned.");
        // Destruction order: navigation/assets -> tools renderer -> ImGui platform/context -> bgfx -> SDL window.
    }
    const bool failed = diagnostics.hadRenderError() || diagnostics.captureFailed() ||
        (!options.capture.empty() && !diagnostics.captureSucceeded());
    diagnostics.log(failed ? "Renderer verification failed." : "Renderer shut down cleanly.");
    return failed ? 1 : 0;
}

} // namespace crimson
