////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/RenderSystem.h"

#include "core/Logger.h"

#include <filament/Renderer.h>
#include <math/vec4.h>

#include <cstdint>
#include <string_view>

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

} // namespace

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

    return true;
}

void RenderSystem::shutdown() {
    if (!engine_) {
        return;
    }

    textureRegistry_.clear();
    materialLibrary_.shutdown();
    if (skybox_) {
        scene_->setSkybox(nullptr);
        engine_->destroy(skybox_);
        skybox_ = nullptr;
    }
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

    filament::Engine::destroy(&engine_);
}

bool RenderSystem::beginFrame() {
    return renderer_ != nullptr && swapChain_ != nullptr && renderer_->beginFrame(swapChain_);
}

bool RenderSystem::beginFrame(filament::SwapChain* swapChain) {
    return renderer_ != nullptr && swapChain != nullptr && renderer_->beginFrame(swapChain);
}

void RenderSystem::endFrame() {
    if (renderer_) {
        renderer_->endFrame();
    }
}

filament::SwapChain* RenderSystem::createAdditionalSwapChain(
        void* nativeWindow,
        SwapChainPresentationColorSpace colorSpace) {
    if (!engine_ || nativeWindow == nullptr) {
        return nullptr;
    }

    const std::uint64_t flags = swapChainFlagsFor(*engine_, colorSpace);
    filament::SwapChain* swapChain = engine_->createSwapChain(nativeWindow, flags);
    LOG_INFO("Filament additional swapchain created for native window {} with flags {}", nativeWindow, flags);
    return swapChain;
}

void RenderSystem::destroyAdditionalSwapChain(filament::SwapChain* swapChain) {
    if (!engine_ || !swapChain) {
        return;
    }

    engine_->destroy(swapChain);
}

} // namespace quader
