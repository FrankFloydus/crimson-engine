////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_material_preview_surface.hpp"

#include "core/logger.hpp"
#include "ui/qt_input_adapter.hpp"

#include <QEvent>
#include <QExposeEvent>
#include <QMouseEvent>
#include <QPlatformSurfaceEvent>
#include <QResizeEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace quader {

QtMaterialPreviewSurface::QtMaterialPreviewSurface(QWindow *parent)
    : QWindow(parent) {
  setObjectName(QStringLiteral("standardMaterialEditor.previewSurface"));
  setSurfaceType(QSurface::RasterSurface);
}

QtMaterialPreviewSurface::~QtMaterialPreviewSurface() { shutdown(); }

bool QtMaterialPreviewSurface::initialize(RenderSystem &render_system) {
  if (initialized_) {
    return true;
  }
  render_system_ = &render_system;
  if (!render_system_->initialize_material_preview_renderer(renderer_)) {
    render_system_ = nullptr;
    return false;
  }
  renderer_.set_preview_state(preview_state_);
  initialized_ = true;

  (void)winId();
  native_surface_available_ = handle() != nullptr;
  recreate_swap_chain();
  if (native_surface_available_ && !swap_chain_) {
    LOG_ERROR("Unable to initialize the material preview native surface");
    shutdown();
    return false;
  }
  synchronize_render_size();
  // A retained Qt::Tool can be initialized while hidden, before the platform
  // exposes its child surface. SurfaceCreated/expose/render will retry the
  // swapchain without discarding the otherwise valid preview renderer.
  return true;
}

void QtMaterialPreviewSurface::shutdown() {
  if (!initialized_) {
    return;
  }
  drag_mode_ = DragMode::None;
  swap_chain_.reset();
  swap_chain_native_id_ = 0u;
  renderer_.shutdown();
  render_system_ = nullptr;
  initialized_ = false;
  native_surface_available_ = false;
}

bool QtMaterialPreviewSurface::render_frame() {
  synchronize_render_size();
  if (!swap_chain_ && native_surface_available_ && isVisible()) {
    recreate_swap_chain();
  }
  if (!ready_to_render() || !render_system_->begin_frame(swap_chain_)) {
    return false;
  }
  render_system_->render_material_preview(renderer_, swap_chain_);
  render_system_->end_frame(swap_chain_);
  return true;
}

bool QtMaterialPreviewSurface::ready_to_render() const {
  return initialized_ && render_system_ != nullptr &&
         render_system_->initialized() && renderer_.initialized() &&
         static_cast<bool>(swap_chain_) && native_surface_available_ &&
         isExposed() && isVisible() && width() > 0 && height() > 0;
}

void QtMaterialPreviewSurface::synchronize_render_size() {
  if (!initialized_) {
    return;
  }
  const qreal scale = devicePixelRatio();
  const std::uint32_t pixel_width = static_cast<std::uint32_t>(
      std::max<qreal>(1.0, std::round(width() * scale)));
  const std::uint32_t pixel_height = static_cast<std::uint32_t>(
      std::max<qreal>(1.0, std::round(height() * scale)));
  if (renderer_.width() != pixel_width || renderer_.height() != pixel_height) {
    renderer_.resize(pixel_width, pixel_height);
  }
}

void QtMaterialPreviewSurface::set_material(
    StandardMaterialPreviewSnapshot snapshot) {
  renderer_.set_material(std::move(snapshot));
  if (surface_changed_callback_) {
    surface_changed_callback_();
  }
}

bool QtMaterialPreviewSurface::set_preview_mesh(
    MaterialPreviewMeshKind mesh) {
  if (!preview_state_.set_mesh_kind(mesh)) {
    return false;
  }
  publish_state_change();
  return true;
}

bool QtMaterialPreviewSurface::set_preview_shading(
    MaterialPreviewShadingMode shading) {
  if (!preview_state_.set_shading_mode(shading)) {
    return false;
  }
  publish_state_change();
  return true;
}

bool QtMaterialPreviewSurface::set_preview_environment(
    std::string_view environment_id) {
  if (!preview_state_.set_environment(environment_id)) {
    return false;
  }
  publish_state_change();
  return true;
}

bool QtMaterialPreviewSurface::set_show_environment_background(bool visible) {
  if (!preview_state_.set_show_environment_background(visible)) {
    return false;
  }
  publish_state_change();
  return true;
}

bool QtMaterialPreviewSurface::reset_preview() {
  if (!preview_state_.reset_view()) {
    return false;
  }
  publish_state_change();
  return true;
}

bool QtMaterialPreviewSurface::event(QEvent *event) {
  if (event != nullptr && event->type() == QEvent::PlatformSurface) {
    auto *surface_event = static_cast<QPlatformSurfaceEvent *>(event);
    if (surface_event->surfaceEventType() ==
        QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed) {
      native_surface_available_ = false;
      swap_chain_.reset();
      swap_chain_native_id_ = 0u;
    } else if (surface_event->surfaceEventType() ==
               QPlatformSurfaceEvent::SurfaceCreated) {
      native_surface_available_ = true;
      recreate_swap_chain();
      synchronize_render_size();
    }
  } else if (event != nullptr &&
             (event->type() == QEvent::DevicePixelRatioChange ||
              event->type() == QEvent::ScreenChangeInternal ||
              event->type() == QEvent::WinIdChange)) {
    if (event->type() == QEvent::WinIdChange) {
      recreate_swap_chain();
    }
    synchronize_render_size();
  }
  return QWindow::event(event);
}

void QtMaterialPreviewSurface::exposeEvent(QExposeEvent *event) {
  QWindow::exposeEvent(event);
  native_surface_available_ = handle() != nullptr;
  if (isExposed()) {
    recreate_swap_chain();
  }
  synchronize_render_size();
  if (surface_changed_callback_) {
    surface_changed_callback_();
  }
}

void QtMaterialPreviewSurface::resizeEvent(QResizeEvent *event) {
  QWindow::resizeEvent(event);
  synchronize_render_size();
  if (surface_changed_callback_) {
    surface_changed_callback_();
  }
}

void QtMaterialPreviewSurface::mousePressEvent(QMouseEvent *event) {
  const std::optional<QuaderInputChord> chord =
      event == nullptr
          ? std::nullopt
          : quader_mouse_input_chord_from_qt_event(
                *event, QuaderInputGesture::Drag);
  if (chord.has_value() &&
      chord->mouse_button == QuaderMouseButton::Middle) {
    requestActivate();
    if (chord->primary) {
      drag_mode_ = DragMode::RotateEnvironment;
    } else if (chord->shift) {
      drag_mode_ = DragMode::Pan;
    } else {
      drag_mode_ = DragMode::Tumble;
    }
    last_drag_position_ = event->position();
    event->accept();
    return;
  }
  QWindow::mousePressEvent(event);
}

void QtMaterialPreviewSurface::mouseReleaseEvent(QMouseEvent *event) {
  if (event != nullptr && event->button() == Qt::MiddleButton) {
    drag_mode_ = DragMode::None;
    event->accept();
    return;
  }
  QWindow::mouseReleaseEvent(event);
}

void QtMaterialPreviewSurface::mouseDoubleClickEvent(QMouseEvent *event) {
  if (event != nullptr && event->button() == Qt::MiddleButton) {
    drag_mode_ = DragMode::None;
    apply_interaction(
        MaterialPreviewInteractionFrame{.reset_requested = true});
    event->accept();
    return;
  }
  QWindow::mouseDoubleClickEvent(event);
}

void QtMaterialPreviewSurface::mouseMoveEvent(QMouseEvent *event) {
  if (event == nullptr || drag_mode_ == DragMode::None ||
      !event->buttons().testFlag(Qt::MiddleButton)) {
    if (event != nullptr && !event->buttons().testFlag(Qt::MiddleButton)) {
      drag_mode_ = DragMode::None;
    }
    QWindow::mouseMoveEvent(event);
    return;
  }
  const QPointF delta = event->position() - last_drag_position_;
  last_drag_position_ = event->position();
  const float scale = static_cast<float>(devicePixelRatio());
  const QVec2 delta_pixels{
      static_cast<float>(delta.x()) * scale,
      static_cast<float>(delta.y()) * scale,
  };
  MaterialPreviewInteractionFrame interaction;
  switch (drag_mode_) {
  case DragMode::Tumble:
    interaction.tumble_delta_pixels = delta_pixels;
    interaction.tumble_active = true;
    break;
  case DragMode::Pan:
    interaction.pan_delta_pixels = delta_pixels;
    interaction.pan_active = true;
    break;
  case DragMode::RotateEnvironment:
    interaction.environment_rotation_delta_pixels = delta_pixels.x;
    interaction.environment_rotation_active = true;
    break;
  case DragMode::None:
    break;
  }
  apply_interaction(interaction);
  event->accept();
}

void QtMaterialPreviewSurface::wheelEvent(QWheelEvent *event) {
  if (event == nullptr) {
    return;
  }
  const QtWheelInputTranslation wheel =
      quader_wheel_input_from_qt_event(*event);
  apply_interaction(MaterialPreviewInteractionFrame{
      .zoom_wheel_steps = wheel.mouse_wheel,
  });
  event->accept();
}

void QtMaterialPreviewSurface::apply_interaction(
    const MaterialPreviewInteractionFrame &input) {
  const MaterialPreviewInteractionResult result =
      input_router_.route(preview_state_, input);
  if (result.changed) {
    publish_state_change();
  }
}

void QtMaterialPreviewSurface::publish_state_change() {
  renderer_.set_preview_state(preview_state_);
  if (surface_changed_callback_) {
    surface_changed_callback_();
  }
}

void QtMaterialPreviewSurface::recreate_swap_chain() {
  if (!initialized_ || render_system_ == nullptr ||
      !render_system_->initialized() || !native_surface_available_) {
    return;
  }
  void *native = native_window_handle();
  if (native == nullptr) {
    return;
  }
  const std::uintptr_t native_id = reinterpret_cast<std::uintptr_t>(native);
  if (swap_chain_ && swap_chain_native_id_ == native_id) {
    return;
  }
  swap_chain_.reset();
  swap_chain_native_id_ = 0u;
  swap_chain_ = render_system_->create_additional_swap_chain(
      native, SwapChainPresentationColorSpace::Srgb);
  if (!swap_chain_) {
    LOG_ERROR("Unable to create Filament swapchain for material preview");
    return;
  }
  swap_chain_native_id_ = native_id;
}

void *QtMaterialPreviewSurface::native_window_handle() const {
  const WId id = const_cast<QtMaterialPreviewSurface *>(this)->winId();
  return reinterpret_cast<void *>(static_cast<std::uintptr_t>(id));
}

} // namespace quader
