////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "ui/qt_vulkan_composition_widget.hpp"

#include "core/logger.hpp"

#include <QColor>
#include <QPaintEvent>
#include <QPalette>
#include <QResizeEvent>
#include <QScopedValueRollback>
#include <QSurfaceFormat>
#include <QVulkanInstance>
#include <QWindow>
#include <QtWidgets/private/qrhiwidget_p.h>
#include <QtGui/private/qrhi_p.h>
#include <QtGui/private/qrhivulkan_p.h>

#include "render/render_system.hpp"

#include <utility>

namespace quader {
namespace {

static_assert(QT_VERSION == QT_VERSION_CHECK(6, 10, 3),
              "Review the pinned Qt Vulkan composition adapter after upgrading Qt.");

/** Inserts the render queue handoff at Qt's actual root-composition boundary. */
class QtVulkanCompositionWidgetPrivate final : public QRhiWidgetPrivate {
public:
  std::function<void()> before_composition;

  void beginCompose() override {
    if (before_composition) {
      before_composition();
    }
  }
};

QRhiVulkan *vulkan_implementation(QRhi *rhi) {
  if (rhi == nullptr || rhi->backend() != QRhi::Vulkan) {
    return nullptr;
  }
  // The pinned private accessor avoids assuming QRhi's object layout. Release
  // the empty pooled batch immediately; it never becomes GPU work.
  QRhiResourceUpdateBatch *batch = rhi->nextResourceUpdateBatch();
  auto *implementation = static_cast<QRhiVulkan *>(
      QRhiResourceUpdateBatchPrivate::get(batch)->rhi);
  batch->release();
  return implementation;
}

VkPresentModeKHR swap_chain_present_mode(const QVkSwapChain &swap_chain) {
  if (swap_chain.flags().testFlag(QRhiSwapChain::NoVSync)) {
    if (!swap_chain.stereo && swap_chain.supportedPresentationModes.contains(
                                 VK_PRESENT_MODE_MAILBOX_KHR)) {
      return VK_PRESENT_MODE_MAILBOX_KHR;
    }
    if (swap_chain.supportedPresentationModes.contains(
            VK_PRESENT_MODE_IMMEDIATE_KHR)) {
      return VK_PRESENT_MODE_IMMEDIATE_KHR;
    }
  }
  return VK_PRESENT_MODE_FIFO_KHR;
}

} // namespace

QtVulkanCompositionWidget::QtVulkanCompositionWidget(QWidget *parent)
    : QRhiWidget(*new QtVulkanCompositionWidgetPrivate, parent) {
  auto *widget_private = static_cast<QtVulkanCompositionWidgetPrivate *>(
      QWidgetPrivate::get(this));
  widget_private->before_composition = [this] { begin_composition(); };
  setApi(Api::Vulkan);
  setSampleCount(1);
  setColorBufferFormat(TextureFormat::RGBA8);
  setAutoRenderTarget(false);
}

QtVulkanCompositionWidget::~QtVulkanCompositionWidget() {
  auto *widget_private = static_cast<QtVulkanCompositionWidgetPrivate *>(
      QWidgetPrivate::get(this));
  widget_private->before_composition = {};
  releaseResources();
}

void QtVulkanCompositionWidget::set_render_system(RenderSystem *render_system) {
  render_system_ = render_system;
  if (render_system_ != nullptr && vsync_request_configured_ &&
      !vsync_change_pending_) {
    render_system_->set_composited_present_mode(resolved_present_mode_);
  }
}

void QtVulkanCompositionWidget::set_prepare_callback(
    std::function<bool()> callback) {
  prepare_callback_ = std::move(callback);
}

void QtVulkanCompositionWidget::set_render_callback(
    std::function<bool(const RenderVulkanCompositionTarget &)> callback) {
  render_callback_ = std::move(callback);
}

void QtVulkanCompositionWidget::set_release_callback(
    std::function<void()> callback) {
  release_callback_ = std::move(callback);
}

const RenderVulkanSharedContext *
QtVulkanCompositionWidget::shared_context() const {
  return shared_context_ ? &*shared_context_ : nullptr;
}

void QtVulkanCompositionWidget::set_vsync_enabled(bool enabled) {
  if (vsync_request_configured_ && vsync_requested_ == enabled) {
    return;
  }
  vsync_requested_ = enabled;
  vsync_request_configured_ = true;
  vsync_change_pending_ = true;
  update_requested_surface_format();
  apply_pending_vsync();
  update();
}

bool QtVulkanCompositionWidget::presentation_is_synchronized() const {
  return resolved_present_mode_ == VK_PRESENT_MODE_FIFO_KHR ||
         resolved_present_mode_ == VK_PRESENT_MODE_FIFO_RELAXED_KHR;
}

VkPresentModeKHR QtVulkanCompositionWidget::resolved_present_mode() const {
  return resolved_present_mode_;
}

void QtVulkanCompositionWidget::resizeEvent(QResizeEvent *event) {
  // Qt 6.10.3's resize handler only sets noSize and paints synchronously.
  // Queue the paint after layout settles; the normal QRhiWidget paint still
  // resizes textures and initializes resources before rendering.
  auto *widget_private =
      static_cast<QRhiWidgetPrivate *>(QWidgetPrivate::get(this));
  widget_private->noSize = event->size().isEmpty();
  if (!widget_private->noSize) {
    update();
  }
}

void QtVulkanCompositionWidget::paintEvent(QPaintEvent *event) {
  if (!updatesEnabled()) {
    return;
  }
  if (prepare_callback_active_ ||
      (initialized_rhi_ != nullptr && initialized_rhi_->isRecordingFrame())) {
    // Layout may request a paint while preparation is still updating camera
    // projections. The completed preparation will be used by the queued paint.
    update();
    return;
  }
  if (prepare_callback_) {
    QScopedValueRollback<bool> guard(prepare_callback_active_, true);
    if (!prepare_callback_()) {
      update();
      return;
    }
  }
  // Qt enters its offscreen frame before invoking render(), so draining here
  // also covers scene uploads and nested paints outside the ordinary App tick.
  apply_pending_vsync();
  finish_external_submission();
  QRhiWidget::paintEvent(event);
}

void QtVulkanCompositionWidget::initialize(QRhiCommandBuffer *) {
  QRhi *current_rhi = rhi();
  if (current_rhi == nullptr || current_rhi->backend() != QRhi::Vulkan) {
    LOG_ERROR("Qt viewport composition requires the Vulkan RHI backend");
    return;
  }
  const auto *native = static_cast<const QRhiVulkanNativeHandles *>(
      current_rhi->nativeHandles());
  if (native == nullptr || native->inst == nullptr ||
      native->dev == VK_NULL_HANDLE || native->gfxQueue == VK_NULL_HANDLE) {
    LOG_ERROR("Qt viewport composition has no valid Vulkan device and queue");
    return;
  }
  const bool context_changed = initialized_rhi_ != current_rhi ||
                               !shared_context_ ||
                               shared_context_->device != native->dev;
  if (shared_context_ && shared_context_->device != native->dev) {
    releaseResources();
  }
  initialized_rhi_ = current_rhi;
  shared_context_ = RenderVulkanSharedContext{
      .instance = native->inst->vkInstance(),
      .physical_device = native->physDev,
      .device = native->dev,
      .graphics_queue = native->gfxQueue,
      .graphics_queue_family = native->gfxQueueFamilyIdx,
      .graphics_queue_index = native->gfxQueueIdx};
  reset_empty_render_target();
  if (context_changed && vsync_request_configured_) {
    vsync_change_pending_ = true;
    update_requested_surface_format();
  }
}

void QtVulkanCompositionWidget::render(QRhiCommandBuffer *command_buffer) {
  QRhiTexture *texture = colorTexture();
  if (!shared_context_ || texture == nullptr) {
    return;
  }
  if (render_callback_) {
    const QRhiTexture::NativeTexture native = texture->nativeTexture();
    const QSize size = texture->pixelSize();
    const RenderVulkanCompositionTarget target{
        .image = VkImage(native.object),
        .width = static_cast<std::uint32_t>(size.width()),
        .height = static_cast<std::uint32_t>(size.height()),
        .old_layout = static_cast<VkImageLayout>(native.layout)};
    const bool submitted = render_callback_(target);
    finish_external_submission();
    if (submitted) {
      texture->setNativeLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
      return;
    }
  }
  // The persistent bootstrap anchor is transparent. A pane that cannot yet
  // submit gets a valid palette-colored target instead of uninitialized pixels.
  if (ensure_empty_render_target()) {
    const QColor clear_color = render_callback_ ? palette().color(QPalette::Base)
                                                : QColor(0, 0, 0, 0);
    command_buffer->beginPass(empty_render_target_.get(), clear_color,
                             {1.0f, 0});
    command_buffer->endPass();
  }
}

void QtVulkanCompositionWidget::releaseResources() {
  if (shared_context_) {
    if (release_callback_) {
      release_callback_();
    }
    finish_external_submission();
  }
  reset_empty_render_target();
  shared_context_.reset();
  initialized_rhi_ = nullptr;
}

void QtVulkanCompositionWidget::finish_external_submission() {
  if (render_system_ == nullptr || !render_system_->initialized()) {
    return;
  }
  if (render_system_->finish_external_queue_submission()) {
    return;
  }
  LOG_ERROR("Unable to hand the Vulkan queue from Filament to Qt composition");
  // beginCompose has no cancellation result. Continuing would permit concurrent
  // host access to the shared Vulkan queue after an unrecoverable handoff error.
  qFatal("Qt Vulkan composition stopped: Filament shared-queue CPU handoff failed");
}

void QtVulkanCompositionWidget::begin_composition() {
  apply_pending_vsync();
  finish_external_submission();
}

void QtVulkanCompositionWidget::update_requested_surface_format() {
  QWindow *root_window = window()->windowHandle();
  if (root_window == nullptr) {
    return;
  }
  QSurfaceFormat format = root_window->requestedFormat();
  const int interval = vsync_requested_ ? 1 : 0;
  if (format.swapInterval() != interval) {
    format.setSwapInterval(interval);
    root_window->setFormat(format);
  }
}

void QtVulkanCompositionWidget::apply_pending_vsync() {
  if (!vsync_change_pending_ || initialized_rhi_ == nullptr) {
    return;
  }
  update_requested_surface_format();
  QRhiVulkan *implementation = vulkan_implementation(initialized_rhi_);
  if (implementation == nullptr || implementation->inFrame) {
    return;
  }
  QWindow *root_window = window()->windowHandle();
  for (QVkSwapChain *swap_chain : implementation->swapchains) {
    if (swap_chain->window != root_window) {
      continue;
    }
    QRhiSwapChain::Flags flags = swap_chain->flags();
    flags.setFlag(QRhiSwapChain::NoVSync, !vsync_requested_);
    if (flags != swap_chain->flags()) {
      finish_external_submission();
      const QRhiSwapChain::Flags previous_flags = swap_chain->flags();
      swap_chain->setFlags(flags);
      if (!swap_chain->createOrResize()) {
        swap_chain->setFlags(previous_flags);
        LOG_ERROR("Unable to update the Qt composition swapchain's VSync mode");
        return;
      }
    }
    resolved_present_mode_ = swap_chain_present_mode(*swap_chain);
    vsync_change_pending_ = false;
    if (render_system_ != nullptr) {
      render_system_->set_composited_present_mode(resolved_present_mode_);
    }
    return;
  }
}

bool QtVulkanCompositionWidget::ensure_empty_render_target() {
  if (empty_render_target_) {
    return true;
  }
  const QRhiTextureRenderTargetDescription description{
      QRhiColorAttachment(colorTexture())};
  empty_render_target_.reset(rhi()->newTextureRenderTarget(description));
  empty_render_pass_.reset(
      empty_render_target_->newCompatibleRenderPassDescriptor());
  empty_render_target_->setRenderPassDescriptor(empty_render_pass_.get());
  if (!empty_render_target_->create()) {
    reset_empty_render_target();
    LOG_ERROR("Unable to initialize the Qt Vulkan composition anchor");
    return false;
  }
  return true;
}

void QtVulkanCompositionWidget::reset_empty_render_target() {
  empty_render_target_.reset();
  empty_render_pass_.reset();
}

} // namespace quader
