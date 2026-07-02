////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/render_system.hpp"

#include "core/logger.hpp"
#include "diagnostics/profile.hpp"

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

filament::backend::Backend to_filament_backend(RendererBackend backend) {
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

std::uint64_t
swap_chain_flags_for(filament::Engine &engine,
                     SwapChainPresentationColorSpace color_space) {
  switch (color_space) {
  case SwapChainPresentationColorSpace::Native:
    return 0u;
  case SwapChainPresentationColorSpace::Srgb:
    if (!filament::SwapChain::isSRGBSwapChainSupported(engine)) {
      LOG_WARN("sRGB swapchain presentation was requested but is not supported "
               "by this Filament backend");
    }
    return filament::SwapChain::CONFIG_SRGB_COLORSPACE;
  }
  return 0u;
}

bool is_frame_info_duration_available(
    filament::Renderer::FrameInfo::duration_ns duration) {
  return duration > 0 && duration != filament::Renderer::FrameInfo::INVALID &&
         duration != filament::Renderer::FrameInfo::PENDING;
}

double
frame_info_duration_ms(filament::Renderer::FrameInfo::duration_ns duration) {
  constexpr double kNanosecondsPerMillisecond = 1000000.0;
  return static_cast<double>(duration) / kNanosecondsPerMillisecond;
}

} // namespace

RenderAdditionalSwapChain::RenderAdditionalSwapChain(
    RenderSystem &owner, filament::SwapChain *swap_chain)
    : owner_(&owner), swap_chain_(swap_chain) {}

RenderAdditionalSwapChain::RenderAdditionalSwapChain(
    RenderAdditionalSwapChain &&other) noexcept
    : owner_(std::exchange(other.owner_, nullptr)),
      swap_chain_(std::exchange(other.swap_chain_, nullptr)) {}

RenderAdditionalSwapChain& RenderAdditionalSwapChain::operator=(RenderAdditionalSwapChain&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    reset();
    owner_ = std::exchange(other.owner_, nullptr);
    swap_chain_ = std::exchange(other.swap_chain_, nullptr);
    return *this;
}

RenderAdditionalSwapChain::~RenderAdditionalSwapChain() {
    reset();
}

void RenderAdditionalSwapChain::reset() {
  if (owner_ != nullptr && swap_chain_ != nullptr) {
    owner_->destroy_additional_swap_chain(swap_chain_);
  }
    owner_ = nullptr;
    swap_chain_ = nullptr;
}

RenderSystem::~RenderSystem() {
    shutdown();
}

bool RenderSystem::initialize(void *native_window,
                              const std::filesystem::path &executable_dir,
                              RenderConfig config) {
  shutdown();
  LOG_INFO("Initializing Filament renderer with {} backend",
           to_string(config.backend));
  const auto fail = [&](std::string_view message) {
    LOG_ERROR("{}", message);
    shutdown();
    return false;
  };

  engine_ = filament::Engine::create(to_filament_backend(config.backend));
  if (!engine_) {
    return fail("Unable to create Filament engine");
  }
  LOG_INFO("Filament engine created");

  swap_chain_ = engine_->createSwapChain(native_window);
  if (!swap_chain_) {
    return fail("Unable to create Filament swapchain");
  }
  LOG_INFO("Filament swapchain created for native window {}", native_window);
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
  const EnvironmentDocument &environment = config.settings.environment;
  filament::Renderer::ClearOptions clear;
  clear.clear = true;
  clear.discard = false;
  clear.clearColor = {environment.clear_color.x, environment.clear_color.y,
                      environment.clear_color.z, environment.clear_alpha};
  renderer_->setClearOptions(clear);

  if (!material_library_.initialize(*engine_, executable_dir)) {
    return fail("Unable to initialize Filament material library");
  }
  environment_library_.initialize(*engine_, *scene_, executable_dir);

  return true;
}

void RenderSystem::shutdown() {
    if (!engine_) {
        return;
    }

    texture_registry_.clear();
    environment_library_.shutdown();
    material_library_.shutdown();
    if (scene_) {
        engine_->destroy(scene_);
        scene_ = nullptr;
    }
    if (renderer_) {
        engine_->destroy(renderer_);
        renderer_ = nullptr;
    }
    if (swap_chain_) {
      engine_->destroy(swap_chain_);
      swap_chain_ = nullptr;
    }

    frame_performance_stats_ = {};
    frame_cpu_active_ = false;
    filament::Engine::destroy(&engine_);
}

bool RenderSystem::begin_frame() {
  QDR_PROFILE_FUNCTION();
  return begin_frame_with_swap_chain(swap_chain_);
}

bool RenderSystem::begin_frame(filament::SwapChain *swap_chain) {
  QDR_PROFILE_FUNCTION();
  return begin_frame_with_swap_chain(swap_chain);
}

void RenderSystem::end_frame() {
  QDR_PROFILE_FUNCTION();
  if (renderer_) {
    renderer_->endFrame();
    update_frame_performance_stats_after_end_frame();
  }
}

bool RenderSystem::begin_frame_with_swap_chain(
    filament::SwapChain *swap_chain) {
  QDR_PROFILE_FUNCTION();
  if (renderer_ == nullptr || swap_chain == nullptr) {
    frame_cpu_active_ = false;
    return false;
  }
  if (!renderer_->beginFrame(swap_chain)) {
    frame_cpu_active_ = false;
    return false;
  }

  frame_cpu_begin_ = std::chrono::steady_clock::now();
  frame_cpu_active_ = true;
  return true;
}

void RenderSystem::update_frame_performance_stats_after_end_frame() {
  if (frame_cpu_active_) {
    const std::chrono::steady_clock::time_point frame_cpu_end =
        std::chrono::steady_clock::now();
    frame_performance_stats_.cpu_frame_ms =
        std::chrono::duration<double, std::milli>(frame_cpu_end -
                                                  frame_cpu_begin_)
            .count();
    frame_performance_stats_.cpu_frame_ms_available = true;
    frame_cpu_active_ = false;
  }

  frame_performance_stats_.gpu_frame_ms_available = false;
  const std::size_t history_size =
      std::min<std::size_t>(renderer_->getMaxFrameHistorySize(), 8u);
  const auto history = renderer_->getFrameInfoHistory(history_size);
  for (std::size_t index = history.size(); index > 0u; --index) {
    const filament::Renderer::FrameInfo &info = history[index - 1u];
    if (is_frame_info_duration_available(info.denoisedGpuFrameDuration)) {
      frame_performance_stats_.gpu_frame_ms =
          frame_info_duration_ms(info.denoisedGpuFrameDuration);
      frame_performance_stats_.gpu_frame_ms_available = true;
      return;
    }
    if (is_frame_info_duration_available(info.gpuFrameDuration)) {
      frame_performance_stats_.gpu_frame_ms =
          frame_info_duration_ms(info.gpuFrameDuration);
      frame_performance_stats_.gpu_frame_ms_available = true;
      return;
    }
  }
}

RenderAdditionalSwapChain RenderSystem::create_additional_swap_chain(
    void *native_window, SwapChainPresentationColorSpace color_space) {
  if (!engine_ || native_window == nullptr) {
    return {};
  }

  const std::uint64_t flags = swap_chain_flags_for(*engine_, color_space);
  filament::SwapChain *swap_chain =
      engine_->createSwapChain(native_window, flags);
  LOG_INFO("Filament additional swapchain created for native window {} with "
           "flags {}",
           native_window, flags);
  return RenderAdditionalSwapChain(*this, swap_chain);
}

void RenderSystem::destroy_additional_swap_chain(
    filament::SwapChain *swap_chain) {
  if (!engine_ || !swap_chain) {
    return;
  }

  engine_->destroy(swap_chain);
}

void RenderSystem::apply_scene_settings(
    const SceneSettingsComponent &settings) {
  if (!engine_ || !scene_) {
    return;
  }
  environment_library_.apply(settings.environment);
}

} // namespace quader
