////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/render_system.hpp"

#include "core/logger.hpp"
#include "diagnostics/profile.hpp"
#include "render/material_preview_renderer.hpp"
#include "render/scene_render_bridge.hpp"
#include "render/viewport_renderer.hpp"

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
    RenderSystem &owner, filament::SwapChain *swap_chain,
    filament::Renderer *renderer)
    : owner_(&owner), swap_chain_(swap_chain), renderer_(renderer) {}

RenderAdditionalSwapChain::RenderAdditionalSwapChain(
    RenderAdditionalSwapChain &&other) noexcept
    : owner_(std::exchange(other.owner_, nullptr)),
      swap_chain_(std::exchange(other.swap_chain_, nullptr)),
      renderer_(std::exchange(other.renderer_, nullptr)) {}

RenderAdditionalSwapChain& RenderAdditionalSwapChain::operator=(RenderAdditionalSwapChain&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    reset();
    owner_ = std::exchange(other.owner_, nullptr);
    swap_chain_ = std::exchange(other.swap_chain_, nullptr);
    renderer_ = std::exchange(other.renderer_, nullptr);
    return *this;
}

RenderAdditionalSwapChain::~RenderAdditionalSwapChain() {
    reset();
}

void RenderAdditionalSwapChain::reset() {
  if (owner_ != nullptr && (swap_chain_ != nullptr || renderer_ != nullptr)) {
    owner_->destroy_additional_surface(swap_chain_, renderer_);
  }
  owner_ = nullptr;
  swap_chain_ = nullptr;
  renderer_ = nullptr;
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
  resource_root_ = executable_dir;

  if (native_window != nullptr) {
    swap_chain_ = engine_->createSwapChain(native_window);
    if (!swap_chain_) {
      return fail("Unable to create Filament swapchain");
    }
    LOG_INFO("Filament swapchain created for native window {}", native_window);
  } else {
    LOG_INFO("Filament initialized without a primary swapchain");
  }
  renderer_ = engine_->createRenderer();
  if (!renderer_) {
    return fail("Unable to create Filament renderer");
  }
  LOG_INFO("Filament renderer created");
  performance_renderer_ = renderer_;
  scene_ = engine_->createScene();
  if (!scene_) {
    return fail("Unable to create Filament scene");
  }
  LOG_INFO("Filament scene created");
  const EnvironmentDocument &environment = config.settings.environment;
  renderer_clear_color_ = {environment.clear_color.x, environment.clear_color.y,
                           environment.clear_color.z,
                           environment.clear_alpha};
  configure_renderer(*renderer_);

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
    presentation_frame_tracker_.reset();
    performance_renderer_ = nullptr;
    renderer_clear_color_ = {};
    resource_root_.clear();
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

bool RenderSystem::begin_frame(RenderAdditionalSwapChain &surface) {
  QDR_PROFILE_FUNCTION();
  return begin_frame_with_renderer(surface.renderer_, surface.swap_chain_);
}

void RenderSystem::end_frame() {
  QDR_PROFILE_FUNCTION();
  end_frame_with_renderer(renderer_);
}

void RenderSystem::end_frame(RenderAdditionalSwapChain &surface) {
  QDR_PROFILE_FUNCTION();
  end_frame_with_renderer(surface.renderer_);
}

void RenderSystem::begin_presentation_frame() {
  presentation_frame_tracker_.begin_presentation_frame(
      RenderPresentationFrameTracker::Clock::now());
}

void RenderSystem::complete_presentation_frame(
    std::size_t swap_chain_submissions) {
  const bool presented =
      presentation_frame_tracker_.complete_presentation_frame(
          RenderPresentationFrameTracker::Clock::now(),
          swap_chain_submissions, frame_performance_stats_);
  if (presented && renderer_ != nullptr) {
    update_gpu_frame_performance_stats();
  }
}

bool RenderSystem::begin_frame_with_swap_chain(
    filament::SwapChain *swap_chain) {
  return begin_frame_with_renderer(renderer_, swap_chain);
}

bool RenderSystem::begin_frame_with_renderer(
    filament::Renderer *renderer, filament::SwapChain *swap_chain) {
  QDR_PROFILE_FUNCTION();
  if (renderer == nullptr || swap_chain == nullptr) {
    return false;
  }
  if (!renderer->beginFrame(swap_chain)) {
    return false;
  }
  performance_renderer_ = renderer;
  return true;
}

void RenderSystem::end_frame_with_renderer(filament::Renderer *renderer) {
  if (renderer != nullptr) {
    renderer->endFrame();
  }
}

void RenderSystem::configure_renderer(filament::Renderer &renderer) const {
  filament::Renderer::ClearOptions clear;
  clear.clear = true;
  clear.discard = false;
  clear.clearColor = {renderer_clear_color_[0], renderer_clear_color_[1],
                      renderer_clear_color_[2], renderer_clear_color_[3]};
  renderer.setClearOptions(clear);
}

void RenderSystem::update_gpu_frame_performance_stats() {
  frame_performance_stats_.gpu_frame_ms_available = false;
  filament::Renderer *renderer = performance_renderer_ != nullptr
                                     ? performance_renderer_
                                     : renderer_;
  if (renderer == nullptr) {
    return;
  }
  const std::size_t history_size =
      std::min<std::size_t>(renderer->getMaxFrameHistorySize(), 8u);
  const auto history = renderer->getFrameInfoHistory(history_size);
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

  filament::Renderer *renderer = engine_->createRenderer();
  if (renderer == nullptr) {
    LOG_ERROR("Unable to create Filament Renderer for additional native surface");
    return {};
  }
  configure_renderer(*renderer);

  const std::uint64_t flags = swap_chain_flags_for(*engine_, color_space);
  filament::SwapChain *swap_chain =
      engine_->createSwapChain(native_window, flags);
  if (swap_chain == nullptr) {
    LOG_ERROR("Unable to create Filament additional swapchain for native window {}",
              native_window);
    engine_->destroy(renderer);
    return {};
  }
  LOG_INFO("Filament additional swapchain created for native window {} with "
           "flags {}",
           native_window, flags);
  return RenderAdditionalSwapChain(*this, swap_chain, renderer);
}

void RenderSystem::destroy_additional_swap_chain(
    filament::SwapChain *swap_chain) {
  if (!engine_ || !swap_chain) {
    return;
  }

  engine_->destroy(swap_chain);
}

void RenderSystem::destroy_additional_surface(
    filament::SwapChain *swap_chain, filament::Renderer *renderer) {
  destroy_additional_swap_chain(swap_chain);
  if (!engine_ || renderer == nullptr) {
    return;
  }
  if (performance_renderer_ == renderer) {
    performance_renderer_ = renderer_;
  }
  engine_->destroy(renderer);
}

void RenderSystem::initialize_scene_render_bridge(
    SceneRenderBridge &render_bridge,
    const std::filesystem::path &executable_dir) {
  if (engine_ == nullptr || scene_ == nullptr) {
    return;
  }

  render_bridge.initialize(*engine_, *scene_, material_library_.quader_mesh(),
                           material_library_.quader_mesh_lit(),
                           material_library_.vertex_color_overlay(),
                           material_library_.overlay_line(),
                           material_library_.selected_object_line(),
                           material_library_.edit_wire_line(),
                           material_library_.overlay_point(),
                           material_library_.overlay_icon(), executable_dir);
}

void RenderSystem::initialize_viewport_renderer(
    ViewportRenderer &viewport_renderer, std::uint8_t grid_layer_mask) {
  initialize_viewport_renderer(viewport_renderer, grid_layer_mask,
                               ViewportRenderSurface::Texture);
}

bool RenderSystem::initialize_material_preview_renderer(
    MaterialPreviewRenderer &preview_renderer) {
  if (engine_ == nullptr || !material_library_.initialized()) {
    return false;
  }
  return preview_renderer.initialize(
      *engine_, material_library_.quader_mesh(),
      material_library_.quader_mesh_lit(), resource_root_);
}

void RenderSystem::initialize_viewport_renderer(
    ViewportRenderer &viewport_renderer, std::uint8_t grid_layer_mask,
    ViewportRenderSurface surface) {
  if (engine_ == nullptr || scene_ == nullptr) {
    return;
  }

  viewport_renderer.initialize(*engine_, *scene_, texture_registry_,
                               material_library_.ground_grid(),
                               grid_layer_mask, surface);
}

void RenderSystem::render_viewport(ViewportRenderer &viewport_renderer) {
  if (renderer_ == nullptr) {
    return;
  }

  viewport_renderer.render(*renderer_);
}

void RenderSystem::render_viewport(ViewportRenderer &viewport_renderer,
                                   RenderAdditionalSwapChain &surface) {
  if (surface.renderer_ == nullptr) {
    return;
  }
  viewport_renderer.render(*surface.renderer_);
}

void RenderSystem::render_material_preview(
    MaterialPreviewRenderer &preview_renderer,
    RenderAdditionalSwapChain &surface) {
  if (surface.renderer_ == nullptr || !preview_renderer.initialized()) {
    return;
  }
  preview_renderer.render(*surface.renderer_);
}

void RenderSystem::apply_scene_settings(
    const SceneSettingsComponent &settings) {
  if (!engine_ || !scene_) {
    return;
  }
  environment_library_.apply(settings.environment);
}

} // namespace quader
