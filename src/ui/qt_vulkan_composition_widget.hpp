////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/render_vulkan_composition.hpp"

#include <QRhiWidget>

#include <functional>
#include <memory>
#include <optional>

class QPaintEvent;
class QResizeEvent;
class QRhi;
class QRhiCommandBuffer;
class QRhiRenderPassDescriptor;
class QRhiTextureRenderTarget;

namespace quader {

class RenderHost;

/**
 * Places an externally rendered image in Qt's native window composition.
 * Qt owns its device and output texture; the renderer owns its GPU resources.
 */
class QtVulkanCompositionWidget final : public QRhiWidget {
public:
  explicit QtVulkanCompositionWidget(QWidget *parent = nullptr,
      RenderCompositionApi api = RenderCompositionApi::Vulkan);
  ~QtVulkanCompositionWidget() override;

  void set_render_system(RenderHost *render_system);
  /** Settles scene and camera state before Qt begins recording; false defers. */
  void set_prepare_callback(std::function<bool()> callback);
  /** True means the target was filled and transitioned to shader-read layout. */
  void set_render_callback(
      std::function<bool(const RenderVulkanCompositionTarget &)> callback);
  /** Releases this widget's users before Qt retires its device, not on resize. */
  void set_release_callback(std::function<void()> callback);

  [[nodiscard]] const RenderVulkanSharedContext *shared_context() const;
  void set_vsync_enabled(bool enabled);
  [[nodiscard]] bool presentation_is_synchronized() const;
  [[nodiscard]] VkPresentModeKHR resolved_present_mode() const;

protected:
  void resizeEvent(QResizeEvent *event) override;
  void paintEvent(QPaintEvent *event) override;
  void initialize(QRhiCommandBuffer *command_buffer) override;
  void render(QRhiCommandBuffer *command_buffer) override;
  void releaseResources() override;

private:
  void finish_external_submission();
  void begin_composition();
  void update_requested_surface_format();
  void apply_pending_vsync();
  bool ensure_empty_render_target();
  void reset_empty_render_target();

  RenderHost *render_system_ = nullptr;
  QRhi *initialized_rhi_ = nullptr;
  std::optional<RenderVulkanSharedContext> shared_context_;
  std::function<bool()> prepare_callback_;
  std::function<bool(const RenderVulkanCompositionTarget &)> render_callback_;
  std::function<void()> release_callback_;
  std::unique_ptr<QRhiTextureRenderTarget> empty_render_target_;
  std::unique_ptr<QRhiRenderPassDescriptor> empty_render_pass_;
  bool prepare_callback_active_ = false;
  std::uint64_t destination_generation_ = 1;
  std::uint64_t accepted_destination_generation_ = 0;
  QRhiTexture *observed_color_texture_ = nullptr;
  quint64 observed_native_object_ = 0;
  QSize observed_pixel_size_;
  bool vsync_requested_ = true;
  bool vsync_request_configured_ = false;
  bool vsync_change_pending_ = false;
  VkPresentModeKHR resolved_present_mode_ = VK_PRESENT_MODE_FIFO_KHR;
};

} // namespace quader
