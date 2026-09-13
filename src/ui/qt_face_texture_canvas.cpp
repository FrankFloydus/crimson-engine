////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_face_texture_canvas.hpp"

#include "api/quader_editor_api.hpp"
#include "core/color_space.hpp"
#include "core/localization_service.hpp"
#include "ui/qt_numeric_format.hpp"
#include "ui/qt_semantic_axis_colors.hpp"

#include <QBrush>
#include <QEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace quader {
namespace {

QPointF point(QVec2 value) { return {value.x, value.y}; }

QVec2 vector(QPointF value) {
  return {static_cast<float>(value.x()), static_cast<float>(value.y())};
}

QString localized(const LocalizationService *localization, const char *key,
                  const char *fallback) {
  return localization == nullptr
             ? QString::fromUtf8(fallback)
             : QString::fromStdString(localization->text(key, fallback));
}

QColor canvas_display_color(const QColor &linear_color) {
  return QColor::fromRgbF(
      linear_to_srgb(static_cast<float>(linear_color.redF())),
      linear_to_srgb(static_cast<float>(linear_color.greenF())),
      linear_to_srgb(static_cast<float>(linear_color.blueF())),
      linear_color.alphaF());
}

QImage tinted_canvas_image(QImage image, const QColor &linear_tint) {
  if (image.isNull() || linear_tint == QColor(Qt::white)) {
    return image;
  }
  // Owner base colors are linear factors; encoded albedo channels are sRGB.
  // Retain this conversion with the image, outside the paint and frame loops.
  const std::array<float, 3> tint{
      static_cast<float>(linear_tint.redF()),
      static_cast<float>(linear_tint.greenF()),
      static_cast<float>(linear_tint.blueF())};
  std::array<std::array<std::uint8_t, 256>, 3> channels{};
  for (std::size_t channel = 0; channel < channels.size(); ++channel) {
    for (std::size_t value = 0; value < channels[channel].size(); ++value) {
      channels[channel][value] = normalized_to_byte(linear_to_srgb(
          srgb_to_linear(static_cast<float>(value) / 255.0f) * tint[channel]));
    }
  }
  image = image.convertToFormat(QImage::Format_ARGB32);
  for (int row = 0; row < image.height(); ++row) {
    auto *pixels = reinterpret_cast<QRgb *>(image.scanLine(row));
    for (int column = 0; column < image.width(); ++column) {
      const QRgb pixel = pixels[column];
      pixels[column] = qRgba(channels[0][qRed(pixel)],
                             channels[1][qGreen(pixel)],
                             channels[2][qBlue(pixel)], qAlpha(pixel));
    }
  }
  return image;
}

} // namespace

QtFaceTextureCanvas::QtFaceTextureCanvas(QWidget *parent) : QWidget(parent) {
  setObjectName(QStringLiteral("face_texture.canvas"));
  setAccessibleName(QStringLiteral("Face texture canvas"));
  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(true);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  setMinimumSize(96, 96);
}

QtFaceTextureCanvas::~QtFaceTextureCanvas() { abandon(); }

void QtFaceTextureCanvas::bind(QuaderEditorApi &editor) {
  abandon();
  editor_ = &editor;
  refresh();
}

void QtFaceTextureCanvas::unbind() {
  cancel_gesture();
  abandon();
}

void QtFaceTextureCanvas::abandon() {
  ++binding_generation_;
  editor_ = nullptr;
  gesture_active_ = false;
  panning_ = false;
  hovered_ = Handle::None;
  dragged_ = Handle::None;
  revision_.reset();
  presentation_ = {};
  outline_.clear();
  bounds_ = {};
  material_.reset();
  image_ = {};
  decoded_extent_ = 0;
  release_capture();
  unsetCursor();
  setToolTip({});
  update();
}

void QtFaceTextureCanvas::refresh() {
  if (editor_ == nullptr) {
    return;
  }
  const std::uint64_t generation = binding_generation_;
  QuaderEditorApi *editor = editor_;
  const QuaderFaceTextureRevision revision =
      editor->modeling().face_texture_settings_revision();
  if (generation != binding_generation_ || editor != editor_) {
    return;
  }
  if (revision_.has_value() && *revision_ == revision) {
    return;
  }

  QuaderFaceTextureCanvas next = editor->modeling().face_texture_canvas();
  if (generation != binding_generation_ || editor != editor_) {
    return;
  }
  const bool selection_changed =
      presentation_.selection_token != next.selection_token ||
      presentation_.object_id != next.object_id ||
      presentation_.face_id != next.face_id ||
      presentation_.selected_face_count != next.selected_face_count;
  if (gesture_active_ && (!next.gesture_active || selection_changed)) {
    // Selection replacement invalidates the owner's captured target. Releasing
    // local capture must not turn the trailing release into another gesture.
    gesture_active_ = false;
    dragged_ = Handle::None;
    release_capture();
  }
  if (selection_changed) {
    panning_ = false;
    hovered_ = Handle::None;
    view_manipulated_ = false;
    release_capture();
  }
  const bool outline_changed = !std::equal(
      presentation_.outline.begin(), presentation_.outline.end(),
      next.outline.begin(), next.outline.end(),
      [](QVec2 left, QVec2 right) {
        return left.x == right.x && left.y == right.y;
      });
  presentation_ = std::move(next);
  revision_ = editor_->modeling().face_texture_settings_revision();
  if (selection_changed || outline_changed) {
    outline_.clear();
    if (presentation_.selected_face_count == 1) {
      outline_.reserve(static_cast<qsizetype>(presentation_.outline.size()));
      for (const QVec2 vertex : presentation_.outline) {
        outline_.push_back(point(vertex));
      }
    }
    bounds_ = outline_.boundingRect();
    if (selection_changed || !view_manipulated_) {
      fit_face();
    }
  }
  setToolTip(QString::fromStdString(presentation_.disabled_reason));
  update();
}

void QtFaceTextureCanvas::set_localization(
    const LocalizationService &localization) {
  localization_ = &localization;
  setAccessibleName(localized(localization_, "face_texture.canvas",
                              "Face texture canvas"));
  update();
}

void QtFaceTextureCanvas::apply_ui_scale(const QtUiScaleMetrics &metrics) {
  if (metrics_.revision() == metrics.revision() &&
      metrics_.factor() == metrics.factor()) {
    return;
  }
  metrics_ = metrics;
  setMinimumSize(metrics.size(QSize(96, 96)));
  if (!view_manipulated_) {
    fit_face();
  }
  updateGeometry();
  update();
}

void QtFaceTextureCanvas::set_material_presentation(
    std::optional<QtMaterialPresentationItem> material) {
  if (!material.has_value() && !material_.has_value()) {
    return;
  }
  if (material.has_value() && material_.has_value() &&
      material->reference == material_->reference &&
      material->content_revision == material_->content_revision &&
      material->thumbnail == material_->thumbnail &&
      material->fallback_color == material_->fallback_color &&
      material->has_valid_albedo == material_->has_valid_albedo &&
      material->available == material_->available) {
    return;
  }
  material_ = std::move(material);
  image_ = {};
  decoded_extent_ = 0;
  refresh_image();
  update();
}

bool QtFaceTextureCanvas::interaction_active() const noexcept {
  return gesture_active_ || panning_;
}

QuaderMaterialReference QtFaceTextureCanvas::material_reference() const {
  if (presentation_.selected_face_count != 1) {
    return {};
  }
  return presentation_.material_id.valid()
             ? QuaderMaterialReference::project(presentation_.material_id)
             : QuaderMaterialReference::embedded(
                   presentation_.embedded_material);
}

const QuaderFaceTextureCanvas &
QtFaceTextureCanvas::presentation() const noexcept {
  return presentation_;
}

::QTransform QtFaceTextureCanvas::plane_to_view() const {
  return {zoom_, 0.0, 0.0, -zoom_,
          width() * 0.5 - center_.x() * zoom_,
          height() * 0.5 + center_.y() * zoom_};
}

QSize QtFaceTextureCanvas::sizeHint() const {
  return metrics_.size(QSize(300, 260));
}

bool QtFaceTextureCanvas::event(QEvent *event) {
  if ((event->type() == QEvent::UngrabMouse && !releasing_capture_) ||
      event->type() == QEvent::WindowDeactivate ||
      event->type() == QEvent::FocusOut || event->type() == QEvent::Hide) {
    cancel_gesture();
  }
  return QWidget::event(event);
}

void QtFaceTextureCanvas::paintEvent(QPaintEvent *) {
  QPainter painter(this);
  painter.fillRect(rect(), palette().brush(QPalette::Base));
  if (presentation_.selected_face_count != 1 || outline_.size() < 3) {
    return;
  }

  const ::QTransform view = plane_to_view();
  const auto &uv = presentation_.uv_from_plane;
  const ::QTransform plane_to_uv(uv[0], uv[2], uv[1], uv[3],
                               presentation_.uv_offset.x,
                               presentation_.uv_offset.y);
  bool invertible = false;
  const ::QTransform uv_to_plane = plane_to_uv.inverted(&invertible);
  if (!image_.isNull() && invertible && material_.has_value() &&
      material_->reference == material_reference()) {
    QBrush texture(image_);
    const ::QTransform image_to_uv(1.0 / image_.width(), 0.0, 0.0,
                                 1.0 / image_.height(), 0.0, 0.0);
    texture.setTransform(image_to_uv * uv_to_plane * view);
    painter.fillRect(rect(), texture);
  } else if (material_.has_value()) {
    painter.fillRect(rect(), canvas_display_color(material_->fallback_color));
  }

  painter.setRenderHint(QPainter::Antialiasing);
  const QPolygonF polygon = view.map(outline_);
  painter.setBrush(Qt::NoBrush);
  painter.setPen(QPen(QColor(0, 0, 0, 190), metrics_.px(3.0)));
  painter.drawPolygon(polygon);
  painter.setPen(QPen(QColor(238, 242, 247), metrics_.px(1.0)));
  painter.drawPolygon(polygon);
  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor(130, 228, 237));
  for (const QPointF vertex : polygon) {
    painter.drawEllipse(vertex, metrics_.px(2.5), metrics_.px(2.5));
  }

  if (!presentation_.editable && !gesture_active_) {
    return;
  }
  const double half_size = metrics_.px(6.0);
  for (const Handle handle : {Handle::Height, Handle::Width}) {
    const QPointF location = handle_position(handle);
    const QColor color = qt_semantic_axis_color(
        handle == Handle::Height ? quader_editor_core::ViewAxis::X
                                 : quader_editor_core::ViewAxis::Y);
    painter.setBrush(color);
    painter.setPen(QPen(handle == hovered_ || handle == dragged_
                            ? QColor(Qt::white)
                            : QColor(0, 0, 0, 150),
                        metrics_.px(1.0)));
    painter.drawRect(QRectF(location.x() - half_size,
                            location.y() - half_size,
                            half_size * 2.0, half_size * 2.0));
  }
  painter.setBrush(QColor(229, 204, 51, 180));
  painter.setPen(QPen(hovered_ == Handle::Rotation ||
                             dragged_ == Handle::Rotation
                         ? QColor(Qt::white)
                         : QColor(255, 232, 77),
                     metrics_.px(1.5)));
  painter.drawEllipse(handle_position(Handle::Rotation), metrics_.px(7.0),
                       metrics_.px(7.0));

  const bool show_measurement =
      hovered_ == Handle::Height || hovered_ == Handle::Width ||
      dragged_ == Handle::Height || dragged_ == Handle::Width;
  if (show_measurement) {
    const QString label =
        localized(localization_, "face_texture.measurements",
                   "X: %1   Y: %2 map units")
            .arg(qt_format_number(presentation_.repeat_width, locale()),
                 qt_format_number(presentation_.repeat_height, locale()));
    const int margin = metrics_.px(8);
    const QRect text_bounds = painter.fontMetrics().boundingRect(label);
    QRect badge(margin, height() - text_bounds.height() - margin * 3,
                 text_bounds.width() + margin * 2,
                 text_bounds.height() + margin * 2);
    painter.fillRect(badge, QColor(20, 23, 28, 235));
    painter.setPen(QColor(245, 245, 245));
    painter.drawText(badge, Qt::AlignCenter, label);
  }
}

void QtFaceTextureCanvas::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  if (fit_pending_ || !view_manipulated_) {
    fit_face();
  }
  refresh_image();
}

void QtFaceTextureCanvas::mousePressEvent(QMouseEvent *event) {
  refresh();
  if (interaction_active() || presentation_.selected_face_count != 1 ||
      outline_.size() < 3) {
    QWidget::mousePressEvent(event);
    return;
  }
  if (event->button() == Qt::MiddleButton) {
    setFocus(Qt::MouseFocusReason);
    pointer_start_ = event->position();
    pan_start_ = center_;
    panning_ = true;
    view_manipulated_ = true;
    setCursor(Qt::ClosedHandCursor);
    grabMouse();
    event->accept();
    return;
  }
  if (event->button() != Qt::LeftButton || editor_ == nullptr ||
      !presentation_.editable) {
    QWidget::mousePressEvent(event);
    return;
  }

  setFocus(Qt::MouseFocusReason);
  const Handle handle = hit_handle(event->position());
  QuaderFaceTextureGestureBegin request;
  request.selection_token = presentation_.selection_token;
  request.start = vector(plane_position(event->position()));
  gesture_pointer_offset_ = {};
  QPointF pivot = bounds_.center();
  switch (handle) {
  case Handle::Height:
    request.kind = QuaderFaceTextureGestureKind::ScaleHeight;
    pivot.setY(bounds_.top());
    break;
  case Handle::Width:
    request.kind = QuaderFaceTextureGestureKind::ScaleWidth;
    pivot.setX(bounds_.left());
    break;
  case Handle::Rotation:
    request.kind = QuaderFaceTextureGestureKind::Rotate;
    if (QLineF(event->position(), handle_position(handle)).length() <
        metrics_.px(3.0)) {
      request.start = vector(pivot + QPointF(metrics_.px(7.0) / zoom_, 0.0));
      gesture_pointer_offset_ = point(request.start) -
                                plane_position(event->position());
    }
    break;
  case Handle::None:
    request.kind = QuaderFaceTextureGestureKind::Translate;
    break;
  }
  request.pivot = vector(pivot);
  const std::uint64_t generation = binding_generation_;
  const auto result = editor_->modeling().begin_face_texture_gesture(request);
  if (generation != binding_generation_ || editor_ == nullptr) {
    event->accept();
    return;
  }
  if (!result.ok()) {
    present_failure(result);
    event->accept();
    return;
  }
  failure_announced_ = false;
  gesture_active_ = true;
  dragged_ = handle;
  last_pointer_ = event->position();
  grabMouse();
  revision_.reset();
  refresh();
  event->accept();
}

void QtFaceTextureCanvas::mouseMoveEvent(QMouseEvent *event) {
  last_pointer_ = event->position();
  if (panning_) {
    const QPointF delta = event->position() - pointer_start_;
    center_ = pan_start_ + QPointF(-delta.x() / zoom_, delta.y() / zoom_);
    update();
    event->accept();
    return;
  }
  if (gesture_active_) {
    update_gesture(event->position(), event->modifiers());
    event->accept();
    return;
  }
  update_hover(event->position());
  QWidget::mouseMoveEvent(event);
}

void QtFaceTextureCanvas::mouseReleaseEvent(QMouseEvent *event) {
  if (panning_ && event->button() == Qt::MiddleButton) {
    panning_ = false;
    release_capture();
    update_hover(event->position());
    event->accept();
    return;
  }
  if (!gesture_active_ || event->button() != Qt::LeftButton) {
    QWidget::mouseReleaseEvent(event);
    return;
  }
  update_gesture(event->position(), event->modifiers());
  if (gesture_active_ && editor_ != nullptr) {
    const std::uint64_t generation = binding_generation_;
    gesture_active_ = false;
    dragged_ = Handle::None;
    release_capture();
    const auto result = editor_->modeling().commit_face_texture_gesture();
    if (generation == binding_generation_ && editor_ != nullptr) {
      present_failure(result);
      revision_.reset();
      refresh();
      update_hover(event->position());
    }
  }
  event->accept();
}

void QtFaceTextureCanvas::wheelEvent(QWheelEvent *event) {
  if (presentation_.selected_face_count != 1 || outline_.size() < 3 ||
      interaction_active()) {
    QWidget::wheelEvent(event);
    return;
  }
  const QPointF before = plane_position(event->position());
  const double amount = event->pixelDelta().isNull()
                            ? event->angleDelta().y() / 120.0
                            : event->pixelDelta().y() / 40.0;
  zoom_ = std::clamp(zoom_ * std::pow(1.15, amount), 0.00001, 100000.0);
  center_ += before - plane_position(event->position());
  view_manipulated_ = true;
  update();
  event->accept();
}

void QtFaceTextureCanvas::keyPressEvent(QKeyEvent *event) {
  // This cancels the captured widget gesture, like a stock slider's Escape;
  // editor commands and their user mappings remain in the shared dispatcher.
  if (interaction_active() && event->matches(QKeySequence::Cancel)) {
    cancel_gesture();
    event->accept();
    return;
  }
  if (gesture_active_) {
    update_gesture(last_pointer_, event->modifiers());
  }
  QWidget::keyPressEvent(event);
}

void QtFaceTextureCanvas::keyReleaseEvent(QKeyEvent *event) {
  if (gesture_active_) {
    update_gesture(last_pointer_, event->modifiers());
  }
  QWidget::keyReleaseEvent(event);
}

void QtFaceTextureCanvas::leaveEvent(QEvent *event) {
  if (!interaction_active()) {
    hovered_ = Handle::None;
    unsetCursor();
    update();
  }
  QWidget::leaveEvent(event);
}

void QtFaceTextureCanvas::fit_face() {
  if (outline_.size() < 3 || bounds_.width() <= 0.0 ||
      bounds_.height() <= 0.0) {
    return;
  }
  const int margin = metrics_.px(28);
  fit_pending_ = width() <= margin * 2 || height() <= margin * 2;
  if (fit_pending_) {
    return;
  }
  center_ = bounds_.center();
  zoom_ = std::clamp(std::min((width() - margin * 2) / bounds_.width(),
                             (height() - margin * 2) / bounds_.height()),
                     0.00001, 100000.0);
}

void QtFaceTextureCanvas::refresh_image() {
  if (!material_.has_value()) {
    return;
  }
  const int extent = std::clamp(
      static_cast<int>(std::ceil(std::max(width(), height()) *
                                devicePixelRatioF())),
      1024, 4096);
  if (decoded_extent_ >= extent) {
    return;
  }
  if (material_->has_valid_albedo ||
      (material_->thumbnail && material_->thumbnail->source_image_valid)) {
    image_ = tinted_canvas_image(qt_material_preview_image(*material_, extent),
                                 material_->fallback_color);
  } else {
    image_ = QImage(1, 1, QImage::Format_ARGB32);
    image_.fill(canvas_display_color(material_->fallback_color));
  }
  decoded_extent_ = extent;
}

void QtFaceTextureCanvas::release_capture() {
  releasing_capture_ = true;
  if (QWidget::mouseGrabber() == this) {
    releaseMouse();
  }
  releasing_capture_ = false;
}

void QtFaceTextureCanvas::cancel_gesture() {
  const bool cancel_owner = gesture_active_ && editor_ != nullptr;
  gesture_active_ = false;
  panning_ = false;
  dragged_ = Handle::None;
  hovered_ = Handle::None;
  release_capture();
  unsetCursor();
  if (cancel_owner) {
    const std::uint64_t generation = binding_generation_;
    const auto result = editor_->modeling().cancel_face_texture_gesture();
    if (generation == binding_generation_ && editor_ != nullptr) {
      present_failure(result);
      revision_.reset();
      refresh();
    }
  }
  update();
}

void QtFaceTextureCanvas::update_gesture(
    QPointF position, Qt::KeyboardModifiers modifiers) {
  if (!gesture_active_ || editor_ == nullptr) {
    return;
  }
  const std::uint64_t generation = binding_generation_;
  const auto result = editor_->modeling().update_face_texture_gesture({
      .current = vector(plane_position(position) + gesture_pointer_offset_),
      .precision = modifiers.testFlag(Qt::ControlModifier),
      .snap = modifiers.testFlag(Qt::ShiftModifier),
  });
  if (generation != binding_generation_ || editor_ == nullptr) {
    return;
  }
  if (!result.ok() && !failure_announced_) {
    failure_announced_ = true;
    present_failure(result);
  }
  refresh();
}

QPointF QtFaceTextureCanvas::plane_position(QPointF position) const {
  return plane_to_view().inverted().map(position);
}

QPointF QtFaceTextureCanvas::handle_position(Handle handle) const {
  QPointF location = bounds_.center();
  if (handle == Handle::Height) {
    // QRect top/bottom are numeric extrema; the plane's Y axis points up.
    location.setY(bounds_.bottom());
  } else if (handle == Handle::Width) {
    location.setX(bounds_.right());
  }
  return plane_to_view().map(location);
}

QtFaceTextureCanvas::Handle
QtFaceTextureCanvas::hit_handle(QPointF position) const {
  const double distance = metrics_.px(10.0);
  for (const Handle handle :
       {Handle::Rotation, Handle::Height, Handle::Width}) {
    if (QLineF(position, handle_position(handle)).length() <= distance) {
      return handle;
    }
  }
  return Handle::None;
}

void QtFaceTextureCanvas::update_hover(QPointF position) {
  const Handle next = presentation_.editable ? hit_handle(position)
                                            : Handle::None;
  if (hovered_ != next) {
    hovered_ = next;
    update();
  }
  if (!presentation_.editable) {
    unsetCursor();
  } else if (next == Handle::Height) {
    setCursor(Qt::SizeVerCursor);
  } else if (next == Handle::Width) {
    setCursor(Qt::SizeHorCursor);
  } else if (next == Handle::Rotation) {
    setCursor(Qt::CrossCursor);
  } else {
    setCursor(Qt::OpenHandCursor);
  }
}

void QtFaceTextureCanvas::present_failure(const QuaderApiVoidResult &result) {
  if (editor_ != nullptr && !result.ok()) {
    (void)editor_->ui().notify({
        .level = QuaderNotificationLevel::Error,
        .message = result.error_message(),
    });
  }
}

} // namespace quader
