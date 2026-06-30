////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/render_system.hpp"

#include "core/logger.hpp"

#include <filament/Renderer.h>
#include <math/vec4.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstddef>
#include <string_view>
#include <utility>

namespace quader {
namespace {

filament::backend::Backend toFilamentBackend(RendererBackend backend) {
    switch (backend) {
    case RendererBackend::Vulkan:
        return filament::backend::Backend::VULKAN;
    case RendererBackend::OpenGL:
        return filament::backend::Backend::OPENGL;
    case RendererBackend::Metal:
        return filament::backend::Backend::METAL;
    }
    return filament::backend::Backend::VULKAN;
}

std::uint64_t swapChainFlagsFor(filament::Engine& engine, SwapChainPresentationColorSpace colorSpace) {
    switch (colorSpace) {
    case SwapChainPresentationColorSpace::Native:
        return 0u;
    case SwapChainPresentationColorSpace::Srgb:
        if (!filament::SwapChain::isSRGBSwapChainSupported(engine)) {
            LOG_WARN("sRGB swapchain presentation was requested but is not supported by this Filament backend");
        }
        return filament::SwapChain::CONFIG_SRGB_COLORSPACE;
    }
    return 0u;
}

bool isFrameInfoDurationAvailable(filament::Renderer::FrameInfo::duration_ns duration) {
    return duration > 0 &&
            duration != filament::Renderer::FrameInfo::INVALID &&
            duration != filament::Renderer::FrameInfo::PENDING;
}

double frameInfoDurationMs(filament::Renderer::FrameInfo::duration_ns duration) {
    constexpr double kNanosecondsPerMillisecond = 1000000.0;
    return static_cast<double>(duration) / kNanosecondsPerMillisecond;
}

} // namespace

RenderAdditionalSwapChain::RenderAdditionalSwapChain(
        RenderSystem& owner,
        filament::SwapChain* swapChain)
    : owner_(&owner)
    , swapChain_(swapChain) {}

RenderAdditionalSwapChain::RenderAdditionalSwapChain(RenderAdditionalSwapChain&& other) noexcept
    : owner_(std::exchange(other.owner_, nullptr))
    , swapChain_(std::exchange(other.swapChain_, nullptr)) {}

RenderAdditionalSwapChain& RenderAdditionalSwapChain::operator=(RenderAdditionalSwapChain&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    reset();
    owner_ = std::exchange(other.owner_, nullptr);
    swapChain_ = std::exchange(other.swapChain_, nullptr);
    return *this;
}

RenderAdditionalSwapChain::~RenderAdditionalSwapChain() {
    reset();
}

void RenderAdditionalSwapChain::reset() {
    if (owner_ != nullptr && swapChain_ != nullptr) {
        owner_->destroyAdditionalSwapChain(swapChain_);
    }
    owner_ = nullptr;
    swapChain_ = nullptr;
}

RenderSystem::~RenderSystem() {
    shutdown();
}

bool RenderSystem::initialize(void* nativeWindow, const std::filesystem::path& executableDir, RenderConfig config) {
    shutdown();
    LOG_INFO("Initializing Filament renderer with {} backend", toString(config.backend));
    const auto fail = [&](std::string_view message) {
        LOG_ERROR("{}", message);
        shutdown();
        return false;
    };

    engine_ = filament::Engine::create(toFilamentBackend(config.backend));
    if (!engine_) {
        return fail("Unable to create Filament engine");
    }
    LOG_INFO("Filament engine created");

    swapChain_ = engine_->createSwapChain(nativeWindow);
    if (!swapChain_) {
        return fail("Unable to create Filament swapchain");
    }
    LOG_INFO("Filament swapchain created for native window {}", nativeWindow);
    renderer_ = engine_->createRenderer();
    if (!renderer_) {
        return fail("Unable to create Filament renderer");
    }
    LOG_INFO("Filament renderer created");
    scene_ = engine_->createScene();
    if (!scene_) {
        return fail("Unable to create Filament scene");
    }
    LOG_INFO("Filament scene created");
    const EnvironmentDocument& environment = config.settings.environment;
    filament::Renderer::ClearOptions clear;
    clear.clear = true;
    clear.discard = true;
    clear.clearColor = {environment.clearColor.x, environment.clearColor.y, environment.clearColor.z, environment.clearAlpha};
    renderer_->setClearOptions(clear);

    if (!materialLibrary_.initialize(*engine_, executableDir)) {
        return fail("Unable to initialize Filament material library");
    }
    environmentLibrary_.initialize(*engine_, *scene_, executableDir);

    return true;
}

void RenderSystem::shutdown() {
    if (!engine_) {
        return;
    }

    textureRegistry_.clear();
    environmentLibrary_.shutdown();
    materialLibrary_.shutdown();
    if (scene_) {
        engine_->destroy(scene_);
        scene_ = nullptr;
    }
    if (renderer_) {
        engine_->destroy(renderer_);
        renderer_ = nullptr;
    }
    if (swapChain_) {
        engine_->destroy(swapChain_);
        swapChain_ = nullptr;
    }

    framePerformanceStats_ = {};
    frameCpuActive_ = false;
    filament::Engine::destroy(&engine_);
}

bool RenderSystem::beginFrame() {
    return beginFrameWithSwapChain(swapChain_);
}

bool RenderSystem::beginFrame(filament::SwapChain* swapChain) {
    return beginFrameWithSwapChain(swapChain);
}

void RenderSystem::endFrame() {
    if (renderer_) {
        renderer_->endFrame();
        updateFramePerformanceStatsAfterEndFrame();
    }
}

bool RenderSystem::beginFrameWithSwapChain(filament::SwapChain* swapChain) {
    if (renderer_ == nullptr || swapChain == nullptr || !renderer_->beginFrame(swapChain)) {
        frameCpuActive_ = false;
        return false;
    }

    frameCpuBegin_ = std::chrono::steady_clock::now();
    frameCpuActive_ = true;
    return true;
}

void RenderSystem::updateFramePerformanceStatsAfterEndFrame() {
    if (frameCpuActive_) {
        const std::chrono::steady_clock::time_point frameCpuEnd = std::chrono::steady_clock::now();
        framePerformanceStats_.cpuFrameMs =
                std::chrono::duration<double, std::milli>(frameCpuEnd - frameCpuBegin_).count();
        framePerformanceStats_.cpuFrameMsAvailable = true;
        frameCpuActive_ = false;
    }

    framePerformanceStats_.gpuFrameMsAvailable = false;
    const std::size_t historySize = std::min<std::size_t>(renderer_->getMaxFrameHistorySize(), 8u);
    const auto history = renderer_->getFrameInfoHistory(historySize);
    for (std::size_t index = history.size(); index > 0u; --index) {
        const filament::Renderer::FrameInfo& info = history[index - 1u];
        if (isFrameInfoDurationAvailable(info.denoisedGpuFrameDuration)) {
            framePerformanceStats_.gpuFrameMs = frameInfoDurationMs(info.denoisedGpuFrameDuration);
            framePerformanceStats_.gpuFrameMsAvailable = true;
            return;
        }
        if (isFrameInfoDurationAvailable(info.gpuFrameDuration)) {
            framePerformanceStats_.gpuFrameMs = frameInfoDurationMs(info.gpuFrameDuration);
            framePerformanceStats_.gpuFrameMsAvailable = true;
            return;
        }
    }
}

RenderAdditionalSwapChain RenderSystem::createAdditionalSwapChain(
        void* nativeWindow,
        SwapChainPresentationColorSpace colorSpace) {
    if (!engine_ || nativeWindow == nullptr) {
        return {};
    }

    const std::uint64_t flags = swapChainFlagsFor(*engine_, colorSpace);
    filament::SwapChain* swapChain = engine_->createSwapChain(nativeWindow, flags);
    LOG_INFO("Filament additional swapchain created for native window {} with flags {}", nativeWindow, flags);
    return RenderAdditionalSwapChain(*this, swapChain);
}

void RenderSystem::destroyAdditionalSwapChain(filament::SwapChain* swapChain) {
    if (!engine_ || !swapChain) {
        return;
    }

    engine_->destroy(swapChain);
}

void RenderSystem::applySceneSettings(const SceneSettingsComponent& settings) {
    if (!engine_ || !scene_) {
        return;
    }
    environmentLibrary_.apply(settings.environment);
}

} // namespace quader
