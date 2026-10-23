#include "App.h"
#include "crimson-renderer/RenderTypes.h"
#include <algorithm>
#include <charconv>
#include <cmath>
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

}
SceneSettings diagnosticSettings(const std::string& scenario) {
    SceneSettings scene;
    if (scenario == "default" || scenario == "model-reopen" || scenario == "model-sphere" || scenario == "mesh-instancing") return scene;
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

AppOptions parseOptions(int argc, char** argv) {
    AppOptions options;
    bool profileDurationSpecified=false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument(argv[i]);
        if (argument == "--hidden") { options.hidden = true; continue; }
        if (argument == "--window-cycle") { options.windowCycle = true; continue; }
        if (argument == "--editor-smoke") { options.editorSmoke = true; continue; }
        if (argument == "--ibl-smoke") { options.iblSmoke = true; continue; }
        if (argument == "--model-smoke") { options.modelSmoke = true; continue; }
        if (i + 1 >= argc) throw std::runtime_error("Missing value for " + std::string(argument));
        const std::string_view value(argv[++i]);
        if (argument == "--frames") options.frameLimit = parseUnsigned(value, 1000000);
        else if (argument == "--capture") options.capture = std::string(value);
        else if (argument == "--scenario") options.scenario = std::string(value);
        else if (argument == "--project-root") options.projectRoot = std::string(value);
        else if (argument == "--frame-profile") options.frameProfile = std::string(value);
        else if (argument == "--profile") {
            if(value!="startup" && value!="gameplay" && value!="editor")throw std::runtime_error("Profile mode must be startup, gameplay or editor.");
            options.profileMode=std::string(value);
        }
        else if (argument == "--profile-duration") {
            options.profileDuration=parseUnsigned(value,3600);
            if(!options.profileDuration)throw std::runtime_error("Profile duration must be between 1 and 3600 seconds.");
            profileDurationSpecified=true;
        }
        else if (argument == "--profile-output") {
            if(value.empty())throw std::runtime_error("Profile output directory cannot be empty.");
            options.profileOutput=std::string(value);
        }
        else if (argument == "--renderer") {
            options.rendererBackend=parseGraphicsBackend(value);
            if(!options.rendererBackend)throw std::runtime_error("Renderer must be dx11 or dx12.");
        }
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
    if (options.modelSmoke && options.projectRoot.empty()) throw std::runtime_error("--model-smoke requires an isolated --project-root.");
    if (options.modelSmoke && (options.iblSmoke || options.editorSmoke || options.scenario != "default"))
        throw std::runtime_error("--model-smoke uses its own default scene and cannot be combined with other smoke modes.");
    if(profileDurationSpecified && options.profileMode.empty())throw std::runtime_error("--profile-duration requires --profile startup, gameplay or editor.");
    if((options.profileMode=="gameplay" || options.profileMode=="editor") && !profileDurationSpecified)options.profileDuration=10;
    diagnosticSettings(options.scenario); // Reject invalid QA arguments before opening a window.
    return options;
}

}
