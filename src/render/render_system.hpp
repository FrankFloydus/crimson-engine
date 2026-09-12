////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/backend.hpp"
#include "render/filament_environment_library.hpp"
#include "render/filament_material_library.hpp"
#include "render/render_performance_stats.hpp"
#include "render/texture_registry.hpp"

#include <filament/Engine.h>
#include <filament/SwapChain.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace filament {
class Renderer;
class Scene;
class Texture;
} // namespace filament

namespace quader {

/**
 * Enumerates SwapChainPresentationColorSpace values used by the render extraction and Filament resource layer.
 */
enum class SwapChainPresentationColorSpace {
    Native,
    Srgb,
};

class RenderSystem;
class MaterialPreviewRenderer;
class SceneRenderBridge;
class ViewportRenderer;
class RenderVulkanPlatform;
struct RenderSurfaceState;
enum class ViewportRenderSurface;

/**
 * Owns one native-surface Filament swapchain and its dedicated Renderer.
 * Filament renderer frame-pacing state is per OS window, so a Renderer must
 * never be shared by multiple native viewport swapchains.
 */
class RenderAdditionalSwapChain {
public:
    RenderAdditionalSwapChain() = default;
    RenderAdditionalSwapChain(const RenderAdditionalSwapChain&) = delete;
    RenderAdditionalSwapChain& operator=(const RenderAdditionalSwapChain&) = delete;
    RenderAdditionalSwapChain(RenderAdditionalSwapChain&& other) noexcept;
    RenderAdditionalSwapChain& operator=(RenderAdditionalSwapChain&& other) noexcept;
    ~RenderAdditionalSwapChain();

    [[nodiscard]] filament::SwapChain *get() const;
    [[nodiscard]] explicit operator bool() const;
    // Recreated swapchains have no retained pixels, even if their view is clean.
    [[nodiscard]] bool needs_initial_frame() const;
    void reset();

private:
    friend class RenderSystem;

    explicit RenderAdditionalSwapChain(std::shared_ptr<RenderSurfaceState> state);

    std::shared_ptr<RenderSurfaceState> state_;
};

/**
 * Owns Render System behavior within the render extraction and Filament resource layer.
 */
class RenderSystem {
public:
    RenderSystem();
    RenderSystem(const RenderSystem&) = delete;
    RenderSystem& operator=(const RenderSystem&) = delete;
    RenderSystem(RenderSystem&&) = delete;
    RenderSystem& operator=(RenderSystem&&) = delete;
    ~RenderSystem();

    bool initialize(void *native_window,
                    const std::filesystem::path &executable_dir,
                    RenderConfig config = {});
    void shutdown();
    [[nodiscard]] bool initialized() const { return engine_ != nullptr; }

    bool begin_frame();
    bool begin_frame(filament::SwapChain *swap_chain);
    bool begin_frame(RenderAdditionalSwapChain &surface);
    void end_frame();
    void end_frame(RenderAdditionalSwapChain &surface);
    void begin_presentation_frame();
    void complete_presentation_frame(std::size_t swap_chain_submissions);
    [[nodiscard]] RenderFramePerformanceStats frame_performance_stats() const {
      return frame_performance_stats_;
    }
    void set_display_refresh_rate_hz(float refresh_rate_hz);
    [[nodiscard]] float display_refresh_rate_hz() const {
      return display_refresh_rate_hz_;
    }
    void set_vsync_enabled(bool enabled);
    [[nodiscard]] bool vsync_enabled() const { return vsync_enabled_; }
    // Uses the backend-confirmed mode. Unknown surfaces conservatively use
    // display cadence until their first swapchain has been created.
    [[nodiscard]] bool presentation_is_synchronized() const;
    // Nonblocking GPU readiness check, before input/modeling/UI preparation.
    // Filament retains its own two-frame maximum latency and beginFrame gate.
    [[nodiscard]] bool ready_for_next_frame() const;
    RenderAdditionalSwapChain
    create_additional_swap_chain(void *native_window,
                                 SwapChainPresentationColorSpace color_space =
                                     SwapChainPresentationColorSpace::Native);

    void initialize_scene_render_bridge(
        SceneRenderBridge &render_bridge,
        const std::filesystem::path &executable_dir);
    void initialize_viewport_renderer(ViewportRenderer &viewport_renderer,
                                      std::uint8_t grid_layer_mask);
    void initialize_viewport_renderer(ViewportRenderer &viewport_renderer,
                                      std::uint8_t grid_layer_mask,
                                      ViewportRenderSurface surface);
    [[nodiscard]] bool initialize_material_preview_renderer(
        MaterialPreviewRenderer &preview_renderer);
    void render_viewport(ViewportRenderer &viewport_renderer);
    void render_viewport(ViewportRenderer &viewport_renderer,
                         RenderAdditionalSwapChain &surface);
    void render_material_preview(MaterialPreviewRenderer &preview_renderer,
                                 RenderAdditionalSwapChain &surface);
    void apply_scene_settings(const SceneSettingsComponent &settings);

  private:
    friend class RenderAdditionalSwapChain;

    bool begin_frame_with_swap_chain(filament::SwapChain *swap_chain);
    bool begin_frame_with_renderer(filament::Renderer *renderer,
                                   filament::SwapChain *swap_chain,
                                   RenderSurfaceState *surface = nullptr);
    void end_frame_with_renderer(filament::Renderer *renderer);
    void configure_renderer(filament::Renderer &renderer) const;
    void destroy_additional_surface(RenderSurfaceState &surface);
    void destroy_surface_swap_chain(RenderSurfaceState &surface);
    void create_surface_swap_chain(RenderSurfaceState &surface);
    void synchronize_surface_presentation(RenderSurfaceState &surface);
    void update_gpu_frame_performance_stats();

    filament::Engine* engine_ = nullptr;
    filament::Renderer* renderer_ = nullptr;
    filament::Renderer *performance_renderer_ = nullptr;
    std::shared_ptr<RenderSurfaceState> primary_surface_;
    std::unique_ptr<RenderVulkanPlatform> vulkan_platform_;
    filament::Scene* scene_ = nullptr;
    FilamentEnvironmentLibrary environment_library_;
    FilamentMaterialLibrary material_library_;
    // Borrowed by scene and preview material instances; destroyed at shutdown.
    filament::Texture *solid_color_texture_ = nullptr;
    TextureRegistry texture_registry_;
    RenderFramePerformanceStats frame_performance_stats_;
    RenderPresentationFrameTracker presentation_frame_tracker_;
    std::vector<std::shared_ptr<RenderSurfaceState>> additional_surfaces_;
    std::array<double, 4> renderer_clear_color_ = {};
    float display_refresh_rate_hz_ = 60.0f;
    bool vsync_enabled_ = true;
    std::filesystem::path resource_root_;
};

} // namespace quader
