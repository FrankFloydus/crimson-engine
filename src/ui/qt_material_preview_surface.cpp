////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_material_preview_surface.hpp"

#include "core/logger.hpp"
#include "ui/qt_contextual_input_hints.hpp"
#include "ui/qt_input_adapter.hpp"
#include "ui/qt_vulkan_composition_widget.hpp"

#include <QEvent>
#include <QExposeEvent>
#include <QMouseEvent>
#include <QPlatformSurfaceEvent>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace quader {

QtMaterialPreviewSurface::QtMaterialPreviewSurface(QWindow *parent)
    : QWindow(parent) {
  setObjectName(QStringLiteral("standardMaterialEditor.previewSurface"));
  setSurfaceType(QSurface::RasterSurface);
  register_input_hints(*this);
}

void QtMaterialPreviewSurface::register_input_hints(QObject& source) {
  qt_register_contextual_input_hints(
      source, *this,
      [this](Qt::KeyboardModifiers) {
        const bool navigable = input_hints_available();
        return QtContextualInputHintSourceState{
            .revision = static_cast<std::uint64_t>(drag_mode_) |
                        (static_cast<std::uint64_t>(navigable) << 8u),
            .active = navigable && drag_mode_ != DragMode::None};
      },
      [this](Qt::KeyboardModifiers modifiers) {
        using Gesture = QuaderOperationInputGesture;
        std::vector<QuaderOperationInputHint> hints;
        if (!input_hints_available()) {
          return hints;
        }
        const bool captured = drag_mode_ != DragMode::None;
        DragMode mode = drag_mode_;
        if (!captured) {
          if (modifiers.testFlag(Qt::ControlModifier) ||
              modifiers.testFlag(Qt::MetaModifier)) {
            mode = DragMode::RotateEnvironment;
          } else if (modifiers.testFlag(Qt::ShiftModifier)) {
            mode = DragMode::Pan;
          } else {
            mode = DragMode::Tumble;
          }
        }
        if (!captured || mode == DragMode::Tumble) {
          hints.push_back({Gesture::MiddleDrag, {},
                           "operation.hints.preview.rotate", "Rotate preview",
                           90, mode == DragMode::Tumble});
        }
        if (!captured || mode == DragMode::Pan) {
          hints.push_back({Gesture::ShiftMiddleDrag, {},
                           "operation.hints.preview.pan", "Pan preview",
                           80, mode == DragMode::Pan});
        }
        if (!captured || mode == DragMode::RotateEnvironment) {
          hints.push_back({Gesture::ControlMiddleDrag, {},
                           "operation.hints.preview.environment",
                           "Rotate environment horizontally", 80,
                           mode == DragMode::RotateEnvironment});
        }
        if (!captured) {
          hints.push_back({Gesture::Wheel, {}, "operation.hints.preview.zoom",
                           "Zoom preview", 85});
          hints.push_back({Gesture::MiddleDoubleClick, {},
                           "operation.hints.preview.reset", "Reset preview", 50});
        }
        return hints;
      });
}

QtMaterialPreviewSurface::~QtMaterialPreviewSurface() {
  // The semantic QWindow can be a sibling of its composed carrier in document
  // tools. Remove callbacks before QObject child deletion can reorder them.
  if(auto* host=static_cast<QtVulkanCompositionWidget*>(composed_host_.data())) {
    host->set_prepare_callback({});host->set_render_callback({});host->set_release_callback({});
    host->set_render_system(nullptr);host->removeEventFilter(this);
  }
  shutdown();
}

QWidget *QtMaterialPreviewSurface::create_composed_widget(QWidget *parent,RenderHost &render_system) {
  if(composed_host_)return composed_host_;
  auto* host=new QtVulkanCompositionWidget(parent,render_system.composition_api());
  composed_host_=host;render_system_=&render_system;
  host->setObjectName(QStringLiteral("standardMaterialEditor.previewSurfaceContainer"));
  host->setFocusPolicy(Qt::StrongFocus);host->setMouseTracking(true);
  host->installEventFilter(this);host->set_render_system(&render_system);register_input_hints(*host);
  host->set_prepare_callback([this] {
    if(!render_system_||!render_system_->initialized())return false;
    if(!initialized_&&!initialize(*render_system_))return false;
    synchronize_render_size();return renderer_&&render_system_->prepare_material_preview(*renderer_,composed_host_->window()->isActiveWindow());
  });
  host->set_render_callback([this](const RenderVulkanCompositionTarget& target) {
    if(!renderer_||!render_system_)return false;
    const bool submitted=render_system_->render_composited_material_preview(*renderer_,target);
    if(submitted||!renderer_->pending())render_pending_=renderer_->pending();
    return submitted;
  });
  host->set_release_callback([this] {
    if(renderer_){if(render_system_)render_system_->release_material_preview(*renderer_);renderer_->shutdown();renderer_.reset();}initialized_=false;render_pending_=true;
  });
  QObject::connect(host,&QObject::destroyed,this,[this]{composed_host_=nullptr;});
  return host;
}

bool QtMaterialPreviewSurface::initialize(RenderHost &render_system) {
  if (initialized_) {
    return true;
  }
  render_system_ = &render_system;
  renderer_ = render_system_->create_material_preview_view();
  if (!renderer_) {
    render_system_ = nullptr;
    return false;
  }
  renderer_->set_preview_state(preview_state_);
  if(record_callback_)render_system_->set_material_preview_recorder(*renderer_,record_callback_);
  if (material_snapshot_) {
    (void)renderer_->set_material(*material_snapshot_);
  }
  initialized_ = true;
  mark_render_pending();

  if(composed_host_) {synchronize_render_size();return true;}

  (void)winId();
  native_surface_available_ = handle() != nullptr;
  recreate_swap_chain(true);
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
  mark_shared_commands_pending();
  if(render_system_)render_system_->release_material_preview(*renderer_);
  renderer_->shutdown();
  renderer_.reset();
  render_system_ = nullptr;
  initialized_ = false;
  native_surface_available_ = false;
  swap_chain_creation_failed_ = false;
  render_pending_ = false;
}

bool QtMaterialPreviewSurface::render_frame() {
  if(composed_host_) {
    if(!ready_to_render()||!render_pending_)return false;
    synchronize_render_size();
    if(!render_system_->prepare_material_preview(*renderer_,composed_host_->window()->isActiveWindow()))return false;
    composed_host_->update();return true;
  }
  if (!render_pending_ && !(swap_chain_ && swap_chain_->needs_initial_frame())) {
    return false;
  }
  synchronize_render_size();
  if (!swap_chain_ && native_surface_available_ && isVisible() &&
      isExposed()) {
    recreate_swap_chain();
  }
  if (!ready_to_render() || !render_system_->begin_frame(*swap_chain_)) {
    return false;
  }
  render_system_->render_material_preview(*renderer_, *swap_chain_);
  render_system_->end_frame(*swap_chain_);
  render_pending_ = false;
  return true;
}

bool QtMaterialPreviewSurface::ready_to_render() const {
  if(composed_host_)return initialized_&&render_system_&&render_system_->initialized()&&renderer_&&renderer_->initialized()&&
      composed_host_->isVisible()&&composed_host_->width()>0&&composed_host_->height()>0;
  return initialized_ && render_system_ != nullptr &&
         render_system_->initialized() && renderer_ && renderer_->initialized() &&
         static_cast<bool>(swap_chain_) && native_surface_available_ &&
         isExposed() && isVisible() && width() > 0 && height() > 0;
}

bool QtMaterialPreviewSurface::needs_rendering() const {
  return (render_pending_ || (swap_chain_ && swap_chain_->needs_initial_frame())) &&
         ready_to_render();
}

bool QtMaterialPreviewSurface::input_hints_available() const noexcept {
  if(composed_host_)return composed_host_->isVisible()&&composed_host_->width()>0&&composed_host_->height()>0;
  return native_surface_available_ && isVisible() && width() > 0 && height() > 0;
}

void QtMaterialPreviewSurface::synchronize_render_size() {
  if (!initialized_) {
    return;
  }
  const qreal scale = composed_host_?composed_host_->devicePixelRatioF():devicePixelRatio();
  const std::uint32_t pixel_width = static_cast<std::uint32_t>(
      std::max<qreal>(1.0, std::round((composed_host_?composed_host_->width():width()) * scale)));
  const std::uint32_t pixel_height = static_cast<std::uint32_t>(
      std::max<qreal>(1.0, std::round((composed_host_?composed_host_->height():height()) * scale)));
  if (renderer_->width() != pixel_width || renderer_->height() != pixel_height) {
    mark_shared_commands_pending();
    renderer_->resize(pixel_width, pixel_height);
    mark_render_pending();
  }
}

void QtMaterialPreviewSurface::set_material(
    StandardMaterialPreviewSnapshot snapshot) {
  material_snapshot_ = std::move(snapshot);
  if (renderer_ && renderer_->set_material(*material_snapshot_)) {
    mark_shared_commands_pending();
  }
  mark_render_pending();
  if (surface_changed_callback_) {
    surface_changed_callback_();
  }
}

void QtMaterialPreviewSurface::set_record_callback(RenderMaterialPreviewRecorder callback) {
  record_callback_=std::move(callback);
  if(renderer_&&render_system_)render_system_->set_material_preview_recorder(*renderer_,record_callback_);
  mark_dirty();
}

void QtMaterialPreviewSurface::mark_dirty() {
  if(renderer_)renderer_->invalidate();
  mark_render_pending();
  if(composed_host_)composed_host_->update();
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
  if (event != nullptr &&
      (event->type() == QEvent::UngrabMouse ||
       event->type() == QEvent::FocusOut ||
       event->type() == QEvent::WindowDeactivate ||
       event->type() == QEvent::WindowBlocked ||
       event->type() == QEvent::Hide)) {
    drag_mode_ = DragMode::None;
  }
  if (event != nullptr && event->type() == QEvent::PlatformSurface) {
    auto *surface_event = static_cast<QPlatformSurfaceEvent *>(event);
    if (surface_event->surfaceEventType() ==
        QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed) {
      drag_mode_ = DragMode::None;
      native_surface_available_ = false;
      swap_chain_.reset();
      swap_chain_native_id_ = 0u;
      swap_chain_creation_failed_ = false;
      render_pending_ = false;
    } else if (surface_event->surfaceEventType() ==
               QPlatformSurfaceEvent::SurfaceCreated) {
      native_surface_available_ = true;
      recreate_swap_chain(true);
      synchronize_render_size();
      mark_render_pending();
    }
  } else if (event != nullptr &&
             (event->type() == QEvent::DevicePixelRatioChange ||
              event->type() == QEvent::ScreenChangeInternal ||
              event->type() == QEvent::WinIdChange)) {
    if (event->type() == QEvent::WinIdChange) {
      recreate_swap_chain(true);
    }
    synchronize_render_size();
    mark_render_pending();
  }
  return QWindow::event(event);
}

bool QtMaterialPreviewSurface::eventFilter(QObject* source,QEvent* event) {
  if(source!=composed_host_||!event)return QWindow::eventFilter(source,event);
  switch(event->type()) {
  case QEvent::MouseButtonPress:mousePressEvent(static_cast<QMouseEvent*>(event));return event->isAccepted();
  case QEvent::MouseButtonRelease:mouseReleaseEvent(static_cast<QMouseEvent*>(event));return event->isAccepted();
  case QEvent::MouseButtonDblClick:mouseDoubleClickEvent(static_cast<QMouseEvent*>(event));return event->isAccepted();
  case QEvent::MouseMove:mouseMoveEvent(static_cast<QMouseEvent*>(event));return event->isAccepted();
  case QEvent::Wheel:wheelEvent(static_cast<QWheelEvent*>(event));return event->isAccepted();
  case QEvent::Resize:case QEvent::DevicePixelRatioChange:case QEvent::ScreenChangeInternal:
    synchronize_render_size();mark_render_pending();if(surface_changed_callback_)surface_changed_callback_();break;
  case QEvent::WindowActivate:case QEvent::FocusIn:
    mark_render_pending();if(surface_changed_callback_)surface_changed_callback_();break;
  case QEvent::Hide:case QEvent::FocusOut:case QEvent::WindowDeactivate:case QEvent::WindowBlocked:case QEvent::UngrabMouse:
    drag_mode_=DragMode::None;break;
  default:break;
  }
  return false;
}

void QtMaterialPreviewSurface::exposeEvent(QExposeEvent *event) {
  QWindow::exposeEvent(event);
  native_surface_available_ = handle() != nullptr;
  if (isExposed()) {
    recreate_swap_chain(true);
    mark_render_pending();
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
    if(composed_host_)composed_host_->setFocus(Qt::MouseFocusReason);else requestActivate();
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
  const float scale = static_cast<float>(composed_host_?composed_host_->devicePixelRatioF():devicePixelRatio());
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
  mark_shared_commands_pending();
  if (renderer_) {
    renderer_->set_preview_state(preview_state_);
  }
  mark_render_pending();
  if (surface_changed_callback_) {
    surface_changed_callback_();
  }
}

void QtMaterialPreviewSurface::mark_shared_commands_pending() {
  if (render_system_ != nullptr && renderer_ && renderer_->initialized()) {
    render_system_->mark_shared_commands_pending();
  }
}

void QtMaterialPreviewSurface::mark_render_pending() {
  render_pending_ = true;
}

void QtMaterialPreviewSurface::recreate_swap_chain(bool force_retry) {
  if (!initialized_ || render_system_ == nullptr ||
      !render_system_->initialized() || !native_surface_available_) {
    return;
  }
  if (swap_chain_creation_failed_ && !force_retry) {
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
  swap_chain_ = render_system_->create_native_surface(
      native, SwapChainPresentationColorSpace::Srgb);
  if (!swap_chain_) {
    if (!swap_chain_creation_failed_) {
      LOG_ERROR("Unable to create renderer surface for material preview");
    }
    swap_chain_creation_failed_ = true;
    return;
  }
  swap_chain_creation_failed_ = false;
  swap_chain_native_id_ = native_id;
  mark_render_pending();
}

void *QtMaterialPreviewSurface::native_window_handle() const {
  const WId id = const_cast<QtMaterialPreviewSurface *>(this)->winId();
  return reinterpret_cast<void *>(static_cast<std::uintptr_t>(id));
}

} // namespace quader
