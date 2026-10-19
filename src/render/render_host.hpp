////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/backend.hpp"
#include "render/render_performance_stats.hpp"
#include "render/render_scene_sink.hpp"
#include "render/render_view.hpp"
#include "render/render_vulkan_composition.hpp"

#include <filesystem>
#include <memory>

namespace quader {

/** Requests explicit display encoding when a native presentation surface supports it. */
enum class SwapChainPresentationColorSpace { Native, Srgb };

/** Opaque retained surface lifetime; releasing it retires its submitted GPU use. */
class RenderSurface {
public:
  virtual ~RenderSurface() = default;
  [[nodiscard]] virtual bool valid() const = 0;
  [[nodiscard]] virtual bool needs_initial_frame() const = 0;
  [[nodiscard]] virtual RenderVulkanCompositionCapacity capacity() const = 0;
  virtual void reset() = 0;
};

/** One coordinated renderer owner shared by all panes in a Qt root generation. */
class RenderHost {
public:
  virtual ~RenderHost() = default;
  virtual bool initialize(void *native_window, const std::filesystem::path &resources,
      RenderConfig config = {}, const RenderVulkanSharedContext *context = nullptr) = 0;
  virtual void shutdown() = 0;
  [[nodiscard]] virtual bool initialized() const = 0;
  [[nodiscard]] virtual bool supports_hidden_native_pixels() const = 0;
  [[nodiscard]] virtual bool uses_composited_vulkan() const = 0;
  [[nodiscard]] virtual std::unique_ptr<RenderSceneSink> create_scene_sink(
      const std::filesystem::path &resources) = 0;
  [[nodiscard]] virtual std::unique_ptr<RenderView> create_view(
      std::uint8_t layer_mask, ViewportRenderSurface surface) = 0;
  [[nodiscard]] virtual std::unique_ptr<RenderMaterialPreviewView> create_material_preview_view() = 0;
  [[nodiscard]] virtual std::unique_ptr<RenderSurface> create_composited_surface(
      std::uint32_t width, std::uint32_t height) = 0;
  [[nodiscard]] virtual std::unique_ptr<RenderSurface> create_native_surface(
      void *native_window, SwapChainPresentationColorSpace color_space = SwapChainPresentationColorSpace::Native) = 0;
  virtual bool begin_frame(RenderSurface &surface) = 0;
  virtual void end_frame(RenderSurface &surface) = 0;
  virtual void render_viewport(RenderView &view, RenderSurface &surface) = 0;
  virtual bool render_composited_viewport(RenderView &view, RenderSurface &surface,
      const RenderVulkanCompositionTarget &target) = 0;
  virtual void render_material_preview(RenderMaterialPreviewView &view, RenderSurface &surface) = 0;
  virtual void bind_view_surface(RenderView &view, RenderSurface &surface) {
    (void)view;
    (void)surface;
  }
  // Preparation visits the complete visible pane set before Qt paints any pane.
  // A backend may stage one shared frame here and submit it on the first copy.
  virtual void begin_view_preparation() {}
  virtual void complete_view_preparation() {}
  virtual void begin_presentation_frame() = 0;
  virtual void complete_presentation_frame(std::size_t submissions) = 0;
  virtual void complete_composited_presentation_frame() = 0;
  virtual bool finish_external_queue_submission() = 0;
  virtual void begin_shared_command_batch() = 0;
  virtual void end_shared_command_batch() = 0;
  virtual void mark_shared_commands_pending() = 0;
  virtual void set_composited_present_mode(VkPresentModeKHR mode) = 0;
  virtual void set_display_refresh_rate_hz(float refresh_rate) = 0;
  virtual void set_vsync_enabled(bool enabled) = 0;
  [[nodiscard]] virtual bool vsync_enabled() const = 0;
  [[nodiscard]] virtual bool presentation_is_synchronized() const = 0;
  [[nodiscard]] virtual bool ready_for_next_frame() const = 0;
  [[nodiscard]] virtual RenderFramePerformanceStats frame_performance_stats() const = 0;
  virtual void apply_scene_settings(const SceneSettings &settings) = 0;
  [[nodiscard]] virtual std::shared_ptr<const MaterialThumbnailReadback> submit_material_thumbnail(
      RenderMaterialEvaluationSnapshot snapshot, std::uint32_t size) = 0;
  virtual void poll_material_thumbnails() = 0;
};

/** Supplied by each executable's linked composition, outside the shared runtime. */
[[nodiscard]] std::unique_ptr<RenderHost> create_app_render_host();

} // namespace quader
