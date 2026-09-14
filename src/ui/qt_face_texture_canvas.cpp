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
#include <QLabel>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QTextLayout>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
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

struct FeedbackTextLayout {
  QString text;
  QSize size;
};

FeedbackTextLayout wrap_feedback(const QString &text, const QFont &font,
                                  int available_width) {
  QTextLayout layout(text, font);
  QTextOption option;
  option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
  layout.setTextOption(option);
  layout.beginLayout();
  FeedbackTextLayout result;
  int count = 0;
  const QFontMetrics metrics(font);
  while (true) {
    QTextLine line = layout.createLine();
    if (!line.isValid()) {
      break;
    }
    line.setLineWidth(available_width);
    const QString segment =
        text.mid(line.textStart(), line.textLength()).trimmed();
    if (count++ > 0) {
      result.text += QLatin1Char('\n');
    }
    result.text += segment;
    result.size.setWidth(std::max(result.size.width(),
                                  metrics.horizontalAdvance(segment)));
  }
  layout.endLayout();
  result.size.setHeight(metrics.height() +
                         std::max(0, count - 1) * metrics.lineSpacing());
  return result;
}

} // namespace

QtFaceTextureCanvas::QtFaceTextureCanvas(QWidget *parent) : QWidget(parent) {
  setObjectName(QStringLiteral("face_texture.canvas"));
  setAccessibleName(QStringLiteral("Face texture canvas"));
  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(true);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  setMinimumSize(96, 96);
  feedback_ = new QLabel(this);
  feedback_->setObjectName(QStringLiteral("face_texture.canvas.measurement"));
  feedback_->setAttribute(Qt::WA_TransparentForMouseEvents);
  feedback_->setTextFormat(Qt::PlainText);
  feedback_->setAutoFillBackground(true);
  feedback_->setBackgroundRole(QPalette::ToolTipBase);
  feedback_->setForegroundRole(QPalette::ToolTipText);
  feedback_->setAlignment(Qt::AlignCenter);
  feedback_->setMargin(metrics_.px(8));
  feedback_->hide();
  refresh_rotation_cursor();
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
  update_feedback();
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
  update_cursor();
  update_feedback();
  update();
}

void QtFaceTextureCanvas::set_localization(
    const LocalizationService &localization) {
  localization_ = &localization;
  setAccessibleName(localized(localization_, "face_texture.canvas",
                              "Face texture canvas"));
  update_feedback();
  update();
}

void QtFaceTextureCanvas::apply_ui_scale(const QtUiScaleMetrics &metrics) {
  if (metrics_.revision() == metrics.revision() &&
      metrics_.factor() == metrics.factor()) {
    return;
  }
  metrics_ = metrics;
  setMinimumSize(metrics.size(QSize(96, 96)));
  feedback_->setMargin(metrics_.px(8));
  refresh_rotation_cursor();
  update_cursor();
  update_feedback();
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
  if (event->type() == QEvent::DevicePixelRatioChange) {
    refresh_rotation_cursor();
    update_cursor();
  }
  if (event->type() == QEvent::FontChange ||
      event->type() == QEvent::LocaleChange) {
    update_feedback();
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
  // Keep the white stroke broad and uninterrupted. Its narrow outside halo
  // supplies contrast over light texels without consuming the white core.
  painter.setPen(QPen(QColor(0, 0, 0, 190), metrics_.px(3.5),
                      Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
  painter.drawPolygon(polygon);
  painter.setPen(QPen(QColor(Qt::white), metrics_.px(2.0),
                      Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
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
}

void QtFaceTextureCanvas::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  if (fit_pending_ || !view_manipulated_) {
    fit_face();
  }
  refresh_image();
  update_feedback();
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
  hovered_ = handle;
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
    update_feedback();
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
  update_feedback();
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
    update_feedback();
    update();
  }
  update_cursor();
}

void QtFaceTextureCanvas::update_cursor() {
  if (panning_) {
    if (cursor().shape() != Qt::ClosedHandCursor) {
      setCursor(Qt::ClosedHandCursor);
    }
    return;
  }
  const Handle next = gesture_active_ ? dragged_ : hovered_;
  if (!presentation_.editable && !gesture_active_) {
    if (testAttribute(Qt::WA_SetCursor)) {
      unsetCursor();
    }
  } else if (next == Handle::Rotation) {
    if (cursor().shape() != Qt::BitmapCursor ||
        cursor().pixmap().cacheKey() != rotation_cursor_.pixmap().cacheKey()) {
      setCursor(rotation_cursor_);
    }
  } else {
    const Qt::CursorShape shape =
        next == Handle::Height ? Qt::SizeVerCursor
        : next == Handle::Width ? Qt::SizeHorCursor
        : gesture_active_ ? Qt::ClosedHandCursor : Qt::OpenHandCursor;
    if (cursor().shape() != shape) {
      setCursor(shape);
    }
  }
}

void QtFaceTextureCanvas::refresh_rotation_cursor() {
  const int extent = metrics_.px(32);
  QPixmap pixmap(metrics_.device_pixels(32, devicePixelRatioF()),
                 metrics_.device_pixels(32, devicePixelRatioF()));
  pixmap.setDevicePixelRatio(devicePixelRatioF());
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.scale(metrics_.factor(), metrics_.factor());
  QPainterPath arrows;
  const QPointF center(16.0, 16.0);
  const auto circle_point = [center](double degrees) {
    const double radians = degrees * std::numbers::pi / 180.0;
    return center + QPointF(std::cos(radians), -std::sin(radians)) * 9.0;
  };
  arrows.moveTo(circle_point(45.0));
  arrows.arcTo(QRectF(7.0, 7.0, 18.0, 18.0), 45.0, 270.0);
  for (const double end_angle : {45.0, 315.0}) {
    const QPointF tip = circle_point(end_angle);
    const double radians = end_angle * std::numbers::pi / 180.0;
    const double direction = end_angle == 45.0 ? -1.0 : 1.0;
    const QPointF forward(-std::sin(radians) * direction,
                           -std::cos(radians) * direction);
    const QPointF side(-forward.y(), forward.x());
    arrows.moveTo(tip - forward * 4.5 + side * 3.5);
    arrows.lineTo(tip);
    arrows.lineTo(tip - forward * 4.5 - side * 3.5);
  }
  painter.setPen(QPen(QColor(20, 23, 28), 4.5, Qt::SolidLine,
                      Qt::RoundCap, Qt::RoundJoin));
  painter.drawPath(arrows);
  painter.setPen(QPen(Qt::white, 2.0, Qt::SolidLine,
                      Qt::RoundCap, Qt::RoundJoin));
  painter.drawPath(arrows);
  painter.end();
  rotation_cursor_ = QCursor(pixmap, extent / 2, extent / 2);
}

void QtFaceTextureCanvas::update_feedback() {
  if (feedback_ == nullptr) {
    return;
  }
  QString text;
  const bool can_present = presentation_.editable || gesture_active_;
  if (can_present && gesture_active_ && dragged_ == Handle::Rotation) {
    text = localized(localization_, "face_texture.rotation_angle",
                     "Rotation: %1°")
               .arg(qt_format_number(presentation_.rotation_degrees, locale()));
  } else if (can_present &&
             (hovered_ == Handle::Height || hovered_ == Handle::Width ||
              dragged_ == Handle::Height || dragged_ == Handle::Width)) {
    text = localized(localization_, "face_texture.measurements",
                     "X: %1   Y: %2 map units")
               .arg(qt_format_number(presentation_.repeat_width, locale()),
                    qt_format_number(presentation_.repeat_height, locale()));
  }
  if (feedback_->accessibleName() != text) {
    feedback_->setAccessibleName(text);
  }
  if (feedback_->isHidden() == !text.isEmpty()) {
    feedback_->setVisible(!text.isEmpty());
  }
  if (text.isEmpty()) {
    if (!feedback_->text().isEmpty()) {
      feedback_->clear();
    }
    return;
  }
  const int margin = metrics_.px(8);
  const int padding = feedback_->margin() * 2;
  const QSize available(std::max(1, width() - margin * 2),
                         std::max(1, height() - margin * 2));
  const int text_width = std::max(1, available.width() - padding);
  const int text_height = std::max(1, available.height() - padding);
  QFont font = this->font();
  FeedbackTextLayout layout = wrap_feedback(text, font, text_width);
  if (layout.size.height() > text_height || layout.size.width() > text_width) {
    // Exceptionally small canvases can be shorter than a fully wrapped readout.
    // Fit only this overlay, keeping the full value available to accessibility.
    int low = 1;
    int high = std::max(1, font.pixelSize() > 0
                             ? font.pixelSize()
                             : static_cast<int>(std::ceil(font.pointSizeF() *
                                                           logicalDpiY() / 72.0)));
    while (low < high) {
      const int candidate = (low + high + 1) / 2;
      QFont fitted = font;
      fitted.setPixelSize(candidate);
      const auto wrapped = wrap_feedback(text, fitted, text_width);
      if (wrapped.size.height() <= text_height &&
          wrapped.size.width() <= text_width) {
        low = candidate;
      } else {
        high = candidate - 1;
      }
    }
    font.setPixelSize(low);
    layout = wrap_feedback(text, font, text_width);
  }
  if (feedback_->font() != font) {
    feedback_->setFont(font);
  }
  if (feedback_->text() != layout.text) {
    feedback_->setText(layout.text);
  }
  const QSize size = (layout.size + QSize(padding, padding)).boundedTo(available);
  const QRect geometry(margin, height() - size.height() - margin,
                        size.width(), size.height());
  if (feedback_->geometry() != geometry) {
    feedback_->setGeometry(geometry);
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
