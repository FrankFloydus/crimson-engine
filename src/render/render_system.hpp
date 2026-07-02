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
#include <filament/Renderer.h>
#include <filament/Scene.h>
#include <filament/Skybox.h>
#include <filament/SwapChain.h>

#include <chrono>
#include <filesystem>
#include <memory>

namespace quader {

/**
 * Enumerates SwapChainPresentationColorSpace values used by the render extraction and Filament resource layer.
 */
enum class SwapChainPresentationColorSpace {
    Native,
    Srgb,
};

class RenderSystem;

/**
 * Owns one additional Filament swapchain through the RenderSystem that created it.
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
      return swap_chain_ != nullptr;
    }
    void reset();

private:
    friend class RenderSystem;

    RenderAdditionalSwapChain(RenderSystem &owner,
                              filament::SwapChain *swap_chain);

    RenderSystem* owner_ = nullptr;
    filament::SwapChain *swap_chain_ = nullptr;
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
    void end_frame();
    [[nodiscard]] RenderFramePerformanceStats frame_performance_stats() const {
      return frame_performance_stats_;
    }
    RenderAdditionalSwapChain
    create_additional_swap_chain(void *native_window,
                                 SwapChainPresentationColorSpace color_space =
                                     SwapChainPresentationColorSpace::Native);
    void destroy_additional_swap_chain(filament::SwapChain *swap_chain);

    filament::Engine& engine() { return *engine_; }
    filament::Renderer& renderer() { return *renderer_; }
    filament::Scene& scene() { return *scene_; }
    filament::Material &quader_mesh_material() {
      return material_library_.quader_mesh();
    }
    filament::Material &quader_mesh_lit_material() {
      return material_library_.quader_mesh_lit();
    }
    filament::Material &vertex_color_overlay_material() {
      return material_library_.vertex_color_overlay();
    }
    filament::Material &overlay_line_material() {
      return material_library_.overlay_line();
    }
    filament::Material &selected_object_line_material() {
      return material_library_.selected_object_line();
    }
    filament::Material &overlay_point_material() {
      return material_library_.overlay_point();
    }
    filament::Material &imgui_material() { return material_library_.imgui(); }
    filament::Material &ground_grid_material() {
      return material_library_.ground_grid();
    }
    TextureRegistry &textures() { return texture_registry_; }
    void apply_scene_settings(const SceneSettingsComponent &settings);

  private:
    bool begin_frame_with_swap_chain(filament::SwapChain *swap_chain);
    void update_frame_performance_stats_after_end_frame();

    filament::Engine* engine_ = nullptr;
    filament::Renderer* renderer_ = nullptr;
    filament::SwapChain *swap_chain_ = nullptr;
    filament::Scene* scene_ = nullptr;
    FilamentEnvironmentLibrary environment_library_;
    FilamentMaterialLibrary material_library_;
    TextureRegistry texture_registry_;
    RenderFramePerformanceStats frame_performance_stats_;
    std::chrono::steady_clock::time_point frame_cpu_begin_{};
    bool frame_cpu_active_ = false;
};

} // namespace quader
