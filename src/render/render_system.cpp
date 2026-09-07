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

#include <backend/platforms/VulkanPlatformWindows.h>
#include <filament/Renderer.h>
#include <math/vec4.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <utility>

static_assert(FILAMENT_QUADER_VSYNC_PATCH_VERSION == 2,
              "Quader requires the matching Filament VSync patch revision");

namespace quader {

// Handles can move in the UI while these owner records remain stable. Shutdown
// invalidates the record before a surviving handle can attempt to release it.
struct RenderSurfaceState {
  RenderSystem *owner = nullptr;
  void *native_window = nullptr;
  filament::SwapChain *swap_chain = nullptr;
  filament::Renderer *renderer = nullptr;
  SwapChainPresentationColorSpace color_space =
      SwapChainPresentationColorSpace::Native;
  std::atomic<bool> unsynchronized = false;
  std::atomic<bool> unsynchronized_requested = false;
  bool renderer_synchronized = true;
  bool submitted_this_frame = false;
  bool submitted_last_frame = false;
  bool initial_frame_pending = false;
};

// Filament owns Vulkan surface creation and recreation. This hook only publishes
// the mode it actually selected; the backend thread never enters Qt or app code.
class RenderVulkanPlatform final
    : public filament::backend::VulkanPlatformWindows {
public:
  void register_surface(const std::shared_ptr<RenderSurfaceState> &surface) {
    std::lock_guard lock(mutex_);
    surfaces_[surface->native_window] = surface;
  }

  void unregister_surface(void *native_window) {
    std::lock_guard lock(mutex_);
    surfaces_.erase(native_window);
  }

  void onSwapChainPresentModeSelected(void *native_window,
                                      bool unsynchronized) noexcept override {
    std::lock_guard lock(mutex_);
    if (const auto found = surfaces_.find(native_window);
        found != surfaces_.end()) {
      found->second->unsynchronized.store(unsynchronized,
                                          std::memory_order_relaxed);
      if (!unsynchronized && found->second->unsynchronized_requested.load(
                                 std::memory_order_relaxed)) {
        LOG_WARN("VSync off is unavailable for native window {}; using FIFO",
                 native_window);
      }
    }
    LOG_INFO("Vulkan presentation for native window {}: {}", native_window,
             unsynchronized ? "Immediate (VSync off)" : "FIFO (VSync on)");
  }

private:
  std::mutex mutex_;
  std::unordered_map<void *, std::shared_ptr<RenderSurfaceState>> surfaces_;
};

namespace {

constexpr float kFallbackDisplayRefreshRateHz = 60.0f;
constexpr float kMinimumDisplayRefreshRateHz = 1.0f;
constexpr float kMaximumDisplayRefreshRateHz = 1000.0f;
constexpr float kDisplayRefreshRateComparisonEpsilonHz = 0.01f;

float normalized_display_refresh_rate_hz(float refresh_rate_hz) {
  if (!std::isfinite(refresh_rate_hz) || refresh_rate_hz <= 0.0f) {
    return kFallbackDisplayRefreshRateHz;
  }
  return std::clamp(refresh_rate_hz, kMinimumDisplayRefreshRateHz,
                    kMaximumDisplayRefreshRateHz);
}

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
                     SwapChainPresentationColorSpace color_space,
                     bool vsync_enabled) {
  const std::uint64_t presentation_flags =
      vsync_enabled ? 0u : filament::SwapChain::CONFIG_UNSYNCHRONIZED;
  switch (color_space) {
  case SwapChainPresentationColorSpace::Native:
    return presentation_flags;
  case SwapChainPresentationColorSpace::Srgb:
    if (!filament::SwapChain::isSRGBSwapChainSupported(engine)) {
      LOG_WARN("sRGB swapchain presentation was requested but is not supported "
               "by this Filament backend");
    }
    return presentation_flags | filament::SwapChain::CONFIG_SRGB_COLORSPACE;
  }
  return presentation_flags;
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
    std::shared_ptr<RenderSurfaceState> state)
    : state_(std::move(state)) {}

RenderAdditionalSwapChain::RenderAdditionalSwapChain(
    RenderAdditionalSwapChain &&other) noexcept
    : state_(std::move(other.state_)) {}

RenderAdditionalSwapChain& RenderAdditionalSwapChain::operator=(RenderAdditionalSwapChain&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    reset();
    state_ = std::move(other.state_);
    return *this;
}

RenderAdditionalSwapChain::~RenderAdditionalSwapChain() {
    reset();
}

void RenderAdditionalSwapChain::reset() {
  if (state_ != nullptr && state_->owner != nullptr) {
    state_->owner->destroy_additional_surface(*state_);
  }
  state_.reset();
}

filament::SwapChain *RenderAdditionalSwapChain::get() const {
  return state_ != nullptr ? state_->swap_chain : nullptr;
}

RenderAdditionalSwapChain::operator bool() const {
  return state_ != nullptr && state_->swap_chain != nullptr &&
         state_->renderer != nullptr;
}

bool RenderAdditionalSwapChain::needs_initial_frame() const {
  return static_cast<bool>(*this) && state_->initial_frame_pending;
}

RenderSystem::RenderSystem() = default;

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

  if (config.backend == RendererBackend::Vulkan) {
    vulkan_platform_ = std::make_unique<RenderVulkanPlatform>();
  }
  engine_ = filament::Engine::create(to_filament_backend(config.backend),
                                    vulkan_platform_.get());
  if (!engine_) {
    return fail("Unable to create Filament engine");
  }
  LOG_INFO("Filament engine created");
  // The display-latency heuristic assumes a refresh-cadenced application loop.
  // GPU frame fences remain enabled independently when VSync is disabled.
  engine_->setFeatureFlag("engine.skip_frame_when_cpu_ahead_of_display",
                          vsync_enabled_);
  resource_root_ = executable_dir;

  if (native_window != nullptr) {
    primary_surface_ = std::make_shared<RenderSurfaceState>();
    primary_surface_->owner = this;
    primary_surface_->native_window = native_window;
    if (vulkan_platform_ != nullptr) {
      vulkan_platform_->register_surface(primary_surface_);
    }
    create_surface_swap_chain(*primary_surface_);
    if (!primary_surface_->swap_chain) {
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
  if (primary_surface_ != nullptr) {
    primary_surface_->renderer = renderer_;
  }
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
        vulkan_platform_.reset();
        return;
    }

    // Complete all queued creation/presentation work before invalidating native
    // surfaces or their backend callback records.
    engine_->flushAndWait();
    while (!additional_surfaces_.empty()) {
      destroy_additional_surface(*additional_surfaces_.back());
    }
    if (primary_surface_ != nullptr) {
      destroy_surface_swap_chain(*primary_surface_);
      primary_surface_->owner = nullptr;
      primary_surface_->renderer = nullptr;
      primary_surface_.reset();
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
    frame_performance_stats_ = {};
    presentation_frame_tracker_.reset();
    performance_renderer_ = nullptr;
    renderer_clear_color_ = {};
    resource_root_.clear();
    filament::Engine::destroy(&engine_);
    vulkan_platform_.reset();
}

bool RenderSystem::begin_frame() {
  QDR_PROFILE_FUNCTION();
  return begin_frame_with_swap_chain(
      primary_surface_ != nullptr ? primary_surface_->swap_chain : nullptr);
}

bool RenderSystem::begin_frame(filament::SwapChain *swap_chain) {
  QDR_PROFILE_FUNCTION();
  return begin_frame_with_swap_chain(swap_chain);
}

bool RenderSystem::begin_frame(RenderAdditionalSwapChain &surface) {
  QDR_PROFILE_FUNCTION();
  if (surface.state_ == nullptr || surface.state_->owner != this) {
    return false;
  }
  return begin_frame_with_renderer(surface.state_->renderer,
                                   surface.state_->swap_chain,
                                   surface.state_.get());
}

void RenderSystem::end_frame() {
  QDR_PROFILE_FUNCTION();
  end_frame_with_renderer(renderer_);
  if (primary_surface_ != nullptr && renderer_ != nullptr) {
    primary_surface_->initial_frame_pending = false;
  }
}

void RenderSystem::end_frame(RenderAdditionalSwapChain &surface) {
  QDR_PROFILE_FUNCTION();
  if (surface.state_ != nullptr && surface.state_->owner == this) {
    end_frame_with_renderer(surface.state_->renderer);
    if (surface.state_->renderer != nullptr) {
      surface.state_->initial_frame_pending = false;
    }
  }
}

void RenderSystem::begin_presentation_frame() {
  if (primary_surface_ != nullptr) {
    primary_surface_->submitted_this_frame = false;
  }
  for (const auto &surface : additional_surfaces_) {
    surface->submitted_this_frame = false;
  }
  presentation_frame_tracker_.begin_presentation_frame(
      RenderPresentationFrameTracker::Clock::now());
}

void RenderSystem::complete_presentation_frame(
    std::size_t swap_chain_submissions) {
  if (primary_surface_ != nullptr) {
    primary_surface_->submitted_last_frame =
        primary_surface_->submitted_this_frame;
  }
  for (const auto &surface : additional_surfaces_) {
    surface->submitted_last_frame = surface->submitted_this_frame;
  }
  const bool presented =
      presentation_frame_tracker_.complete_presentation_frame(
          RenderPresentationFrameTracker::Clock::now(),
          swap_chain_submissions, frame_performance_stats_);
  if (presented && renderer_ != nullptr) {
    update_gpu_frame_performance_stats();
  }
}

void RenderSystem::set_display_refresh_rate_hz(float refresh_rate_hz) {
  const float normalized =
      normalized_display_refresh_rate_hz(refresh_rate_hz);
  if (std::abs(display_refresh_rate_hz_ - normalized) <=
      kDisplayRefreshRateComparisonEpsilonHz) {
    return;
  }

  display_refresh_rate_hz_ = normalized;
  const filament::Renderer::DisplayInfo display_info{
      .refreshRate = display_refresh_rate_hz_};
  if (renderer_ != nullptr &&
      (primary_surface_ == nullptr ||
       primary_surface_->renderer_synchronized)) {
    renderer_->setDisplayInfo(display_info);
  }
  for (const auto &surface : additional_surfaces_) {
    if (surface->renderer != nullptr && surface->renderer_synchronized) {
      surface->renderer->setDisplayInfo(display_info);
    }
  }
}

void RenderSystem::set_vsync_enabled(bool enabled) {
  if (vsync_enabled_ == enabled) {
    return;
  }
  vsync_enabled_ = enabled;
  if (engine_ == nullptr) {
    return;
  }
  engine_->setFeatureFlag("engine.skip_frame_when_cpu_ahead_of_display",
                          enabled);

  // Vulkan permits only one live swapchain per native window. Finish queued
  // frames and retire every old swapchain before creating its replacement.
  // The paired Renderer, native handle, color space, and UI handle stay stable.
  engine_->flushAndWait();
  if (primary_surface_ != nullptr) {
    destroy_surface_swap_chain(*primary_surface_);
  }
  for (const auto &surface : additional_surfaces_) {
    destroy_surface_swap_chain(*surface);
  }
  engine_->flushAndWait();
  if (primary_surface_ != nullptr) {
    if (vulkan_platform_ != nullptr) {
      vulkan_platform_->register_surface(primary_surface_);
    }
    create_surface_swap_chain(*primary_surface_);
  }
  for (const auto &surface : additional_surfaces_) {
    if (vulkan_platform_ != nullptr) {
      vulkan_platform_->register_surface(surface);
    }
    create_surface_swap_chain(*surface);
  }
  engine_->flushAndWait();
  if (primary_surface_ != nullptr) {
    synchronize_surface_presentation(*primary_surface_);
  }
  for (const auto &surface : additional_surfaces_) {
    synchronize_surface_presentation(*surface);
  }
  LOG_INFO("VSync {} requested; native surfaces recreated", enabled ? "on" : "off");
}

bool RenderSystem::presentation_is_synchronized() const {
  if (vsync_enabled_) {
    return true;
  }
  const bool has_recent_submission =
      (primary_surface_ != nullptr && primary_surface_->submitted_last_frame) ||
      std::any_of(additional_surfaces_.begin(), additional_surfaces_.end(),
                   [](const auto &surface) {
                     return surface->submitted_last_frame;
                   });
  bool has_surface = false;
  const auto requires_synchronization =
      [&has_surface, has_recent_submission](const auto &surface) {
        if (surface == nullptr || surface->swap_chain == nullptr ||
            (has_recent_submission && !surface->submitted_last_frame)) {
          return false;
        }
        has_surface = true;
        return !surface->unsynchronized.load(std::memory_order_relaxed);
      };
  const bool synchronized =
      requires_synchronization(primary_surface_) ||
      std::any_of(additional_surfaces_.begin(), additional_surfaces_.end(),
                   requires_synchronization);
  return !has_surface || synchronized;
}

bool RenderSystem::ready_for_next_frame() const {
  const auto ready = [](const std::shared_ptr<RenderSurfaceState> &surface) {
    return surface == nullptr || !surface->submitted_last_frame ||
           surface->renderer == nullptr ||
           surface->renderer->shouldRenderFrame();
  };
  return ready(primary_surface_) &&
         std::all_of(additional_surfaces_.begin(), additional_surfaces_.end(),
                     ready);
}

bool RenderSystem::begin_frame_with_swap_chain(
    filament::SwapChain *swap_chain) {
  return begin_frame_with_renderer(renderer_, swap_chain,
                                   primary_surface_.get());
}

bool RenderSystem::begin_frame_with_renderer(
    filament::Renderer *renderer, filament::SwapChain *swap_chain,
    RenderSurfaceState *surface) {
  QDR_PROFILE_FUNCTION();
  if (renderer == nullptr || swap_chain == nullptr) {
    return false;
  }
  if (surface != nullptr) {
    synchronize_surface_presentation(*surface);
  }
  if (!renderer->beginFrame(swap_chain)) {
    return false;
  }
  performance_renderer_ = renderer;
  if (surface != nullptr) {
    surface->submitted_this_frame = true;
  }
  return true;
}

void RenderSystem::end_frame_with_renderer(filament::Renderer *renderer) {
  if (renderer != nullptr) {
    renderer->endFrame();
  }
}

void RenderSystem::configure_renderer(filament::Renderer &renderer) const {
  renderer.setDisplayInfo(
      filament::Renderer::DisplayInfo{.refreshRate =
                                          display_refresh_rate_hz_});
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

  auto surface = std::make_shared<RenderSurfaceState>();
  surface->owner = this;
  surface->native_window = native_window;
  surface->renderer = renderer;
  surface->color_space = color_space;
  if (vulkan_platform_ != nullptr) {
    vulkan_platform_->register_surface(surface);
  }
  create_surface_swap_chain(*surface);
  if (surface->swap_chain == nullptr) {
    LOG_ERROR("Unable to create Filament additional swapchain for native window {}",
              native_window);
    engine_->destroy(renderer);
    if (vulkan_platform_ != nullptr) {
      vulkan_platform_->unregister_surface(native_window);
    }
    return {};
  }
  additional_surfaces_.push_back(surface);
  return RenderAdditionalSwapChain(std::move(surface));
}

void RenderSystem::create_surface_swap_chain(RenderSurfaceState &surface) {
  surface.unsynchronized.store(false, std::memory_order_relaxed);
  surface.unsynchronized_requested.store(!vsync_enabled_,
                                          std::memory_order_relaxed);
  const std::uint64_t flags =
      swap_chain_flags_for(*engine_, surface.color_space, vsync_enabled_);
  surface.swap_chain = engine_->createSwapChain(surface.native_window, flags);
  if (surface.swap_chain == nullptr) {
    LOG_ERROR("Unable to create Filament swapchain for native window {}",
              surface.native_window);
  } else {
    LOG_INFO("Filament swapchain created for native window {} with flags {}",
             surface.native_window, flags);
  }
  surface.submitted_this_frame = false;
  surface.submitted_last_frame = false;
  surface.initial_frame_pending = surface.swap_chain != nullptr;
}

void RenderSystem::destroy_surface_swap_chain(RenderSurfaceState &surface) {
  if (vulkan_platform_ != nullptr) {
    vulkan_platform_->unregister_surface(surface.native_window);
  }
  if (engine_ != nullptr && surface.swap_chain != nullptr) {
    engine_->destroy(surface.swap_chain);
  }
  surface.swap_chain = nullptr;
  surface.unsynchronized.store(false, std::memory_order_relaxed);
  surface.submitted_this_frame = false;
  surface.submitted_last_frame = false;
  surface.initial_frame_pending = false;
}

void RenderSystem::synchronize_surface_presentation(RenderSurfaceState &surface) {
  const bool synchronized =
      vsync_enabled_ || !surface.unsynchronized.load(std::memory_order_relaxed);
  if (surface.renderer == nullptr ||
      surface.renderer_synchronized == synchronized) {
    return;
  }
  surface.renderer_synchronized = synchronized;
  surface.renderer->setDisplayInfo(filament::Renderer::DisplayInfo{
      .refreshRate = synchronized ? display_refresh_rate_hz_ : 0.0f});
}

void RenderSystem::destroy_additional_surface(RenderSurfaceState &surface) {
  destroy_surface_swap_chain(surface);
  if (performance_renderer_ == surface.renderer) {
    performance_renderer_ = renderer_;
  }
  if (engine_ != nullptr && surface.renderer != nullptr) {
    engine_->destroy(surface.renderer);
  }
  surface.owner = nullptr;
  surface.renderer = nullptr;
  std::erase_if(additional_surfaces_, [&surface](const auto &candidate) {
    return candidate.get() == &surface;
  });
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
                           material_library_.instance_selection_fill(),
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
  if (!surface || surface.state_->owner != this) {
    return;
  }
  viewport_renderer.render(*surface.state_->renderer);
}

void RenderSystem::render_material_preview(
    MaterialPreviewRenderer &preview_renderer,
    RenderAdditionalSwapChain &surface) {
  if (!surface || surface.state_->owner != this ||
      !preview_renderer.initialized()) {
    return;
  }
  preview_renderer.render(*surface.state_->renderer);
}

void RenderSystem::apply_scene_settings(
    const SceneSettingsComponent &settings) {
  if (!engine_ || !scene_) {
    return;
  }
  environment_library_.apply(settings.environment);
}

} // namespace quader
