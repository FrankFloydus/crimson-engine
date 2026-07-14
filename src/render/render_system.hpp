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

namespace filament {
class Renderer;
class Scene;
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

    [[nodiscard]] filament::SwapChain *get() const { return swap_chain_; }
    [[nodiscard]] explicit operator bool() const {
      return swap_chain_ != nullptr && renderer_ != nullptr;
    }
    void reset();

private:
    friend class RenderSystem;

    RenderAdditionalSwapChain(RenderSystem &owner,
                              filament::SwapChain *swap_chain,
                              filament::Renderer *renderer);

    RenderSystem* owner_ = nullptr;
    filament::SwapChain *swap_chain_ = nullptr;
    filament::Renderer *renderer_ = nullptr;
};

/**
 * Owns Render System behavior within the render extraction and Filament resource layer.
 */
class RenderSystem {
public:
    RenderSystem() = default;
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
    RenderAdditionalSwapChain
    create_additional_swap_chain(void *native_window,
                                 SwapChainPresentationColorSpace color_space =
                                     SwapChainPresentationColorSpace::Native);
    void destroy_additional_swap_chain(filament::SwapChain *swap_chain);

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
                                   filament::SwapChain *swap_chain);
    void end_frame_with_renderer(filament::Renderer *renderer);
    void configure_renderer(filament::Renderer &renderer) const;
    void destroy_additional_surface(filament::SwapChain *swap_chain,
                                    filament::Renderer *renderer);
    void update_gpu_frame_performance_stats();

    filament::Engine* engine_ = nullptr;
    filament::Renderer* renderer_ = nullptr;
    filament::Renderer *performance_renderer_ = nullptr;
    filament::SwapChain *swap_chain_ = nullptr;
    filament::Scene* scene_ = nullptr;
    FilamentEnvironmentLibrary environment_library_;
    FilamentMaterialLibrary material_library_;
    TextureRegistry texture_registry_;
    RenderFramePerformanceStats frame_performance_stats_;
    RenderPresentationFrameTracker presentation_frame_tracker_;
    std::array<double, 4> renderer_clear_color_ = {};
    std::filesystem::path resource_root_;
};

} // namespace quader
