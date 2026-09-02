////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_navigation_gizmo.hpp"

#include "ui/qt_semantic_axis_colors.hpp"
#include "viewport/viewport_camera_adapter.hpp"
#include "viewport/viewport_layout.hpp"

#include <QColor>
#include <QHideEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>

#include <algorithm>
#include <cmath>
#include <string_view>

namespace quader {
namespace {

constexpr double kPreferredWidgetSize = 70.0;
constexpr float kDragThresholdSquared = 9.0f;
constexpr float kOrientationRefreshEpsilon = 0.000001f;
constexpr qreal kAxisMarkerRadius = 8.0;

QVec3 runtime_vector(QVec3 value) { return {value.x, value.y, value.z}; }

quader_poly::Transform3
runtime_camera_transform(const ViewportCameraController &camera) {
  const ViewportCameraBasis basis = ViewportCameraAdapter::basis(camera);
  quader_poly::Transform3 transform;
  transform.origin = runtime_vector(basis.eye);
  transform.x_axis = runtime_vector(basis.right);
  transform.y_axis = runtime_vector(basis.up);
  transform.z_axis = runtime_vector(basis.forward * -1.0f);
  return transform;
}

ViewportViewAxis viewport_axis(quader_editor_core::ViewAxis axis) {
  switch (axis) {
  case quader_editor_core::ViewAxis::X:
    return ViewportViewAxis::X;
  case quader_editor_core::ViewAxis::Y:
    return ViewportViewAxis::Y;
  case quader_editor_core::ViewAxis::Z:
    return ViewportViewAxis::Z;
  }
  return ViewportViewAxis::Z;
}

QPointF painter_point(QVec2 point) {
  return {std::floor(static_cast<qreal>(point.x)) + 0.5,
          std::floor(static_cast<qreal>(point.y)) + 0.5};
}

bool orientation_axis_matches(QVec3 left, QVec3 right) {
  return std::fabs(left.x - right.x) <= kOrientationRefreshEpsilon &&
         std::fabs(left.y - right.y) <= kOrientationRefreshEpsilon &&
         std::fabs(left.z - right.z) <= kOrientationRefreshEpsilon;
}

} // namespace

QtNavigationGizmoCameraSnapshot qt_navigation_gizmo_camera_snapshot(
    const ViewportCameraController *camera) {
  if (camera == nullptr) {
    return {};
  }
  const ViewportCameraBasis basis = ViewportCameraAdapter::basis(*camera);
  return {
      .camera = camera,
      .forward = basis.forward,
      .right = basis.right,
      .up = basis.up,
  };
}

bool qt_navigation_gizmo_camera_refresh_required(
    const std::optional<QtNavigationGizmoCameraSnapshot> &current,
    const QtNavigationGizmoCameraSnapshot &next) {
  if (!current.has_value() || current->camera != next.camera) {
    return true;
  }
  if (next.camera == nullptr) {
    return false;
  }
  return !orientation_axis_matches(current->forward, next.forward) ||
         !orientation_axis_matches(current->right, next.right) ||
         !orientation_axis_matches(current->up, next.up);
}

bool qt_navigation_gizmo_hits_match(
    const quader_editor_core::ViewCubeHit &left,
    const quader_editor_core::ViewCubeHit &right) {
  if (left.hit != right.hit) {
    return false;
  }
  return !left.hit || quader_editor_core::view_cube_hits_match(left, right);
}

quader_editor_core::ViewCubePrimitiveInput
QtNavigationGizmoInteraction::primitive_input(
    const ViewportCameraController &camera, QVec2 viewport_size) const {
  quader_editor_core::ViewCubePrimitiveInput input;
  input.camera_transform = runtime_camera_transform(camera);
  input.viewport_size = {std::max(viewport_size.x, 1.0f),
                         std::max(viewport_size.y, 1.0f)};
  input.widget_size = std::min(
      kPreferredWidgetSize,
      std::max(48.0, static_cast<double>(
                         std::min(input.viewport_size.x,
                                  input.viewport_size.y) *
                         0.72f)));
  input.margin = {input.viewport_size.x * 0.5f,
                  input.viewport_size.y * 0.5f};
  return input;
}

quader_editor_core::ViewCubePrimitives
QtNavigationGizmoInteraction::primitives(
    const ViewportCameraController &camera, QVec2 viewport_size) const {
  return quader_editor_core::build_view_cube_primitives(
      primitive_input(camera, viewport_size));
}

quader_editor_core::ViewCubeHit QtNavigationGizmoInteraction::hit_test(
    const ViewportCameraController &camera, QVec2 viewport_size,
    QVec2 local_position) const {
  return quader_editor_core::pick_view_cube({
      .primitives = primitive_input(camera, viewport_size),
      .screen_position = local_position,
  });
}

bool QtNavigationGizmoInteraction::press(
    ViewportCameraController &camera, ViewportPaneId pane_id,
    QVec2 viewport_size, QVec2 local_position) {
  if (dragging_) {
    return pane_id == pane_id_;
  }
  const quader_editor_core::ViewCubeHit hit =
      hit_test(camera, viewport_size, local_position);
  if (!hit.hit) {
    return false;
  }

  dragging_ = true;
  drag_started_ = false;
  pane_id_ = pane_id;
  accumulated_drag_ = {};
  pressed_hit_ = hit;
  camera.unlock_orthographic_view();
  camera.begin_drag(local_position, ViewportDragMode::Orbit);
  return true;
}

bool QtNavigationGizmoInteraction::drag(ViewportCameraController &camera,
                                        ViewportPaneId pane_id, QVec2 delta) {
  if (!dragging_ || pane_id != pane_id_) {
    return false;
  }
  accumulated_drag_ = accumulated_drag_ + delta;
  const float accumulated_length_squared =
      accumulated_drag_.x * accumulated_drag_.x +
      accumulated_drag_.y * accumulated_drag_.y;
  if (accumulated_length_squared > kDragThresholdSquared) {
    drag_started_ = true;
  }
  const float delta_length_squared =
      delta.x * delta.x + delta.y * delta.y;
  if (drag_started_ && delta_length_squared > 0.000001f) {
    camera.update_drag_delta(delta);
  }
  return true;
}

bool QtNavigationGizmoInteraction::release(ViewportCameraController &camera,
                                           ViewportPaneId pane_id) {
  if (!dragging_ || pane_id != pane_id_) {
    return false;
  }
  if (!drag_started_ && pressed_hit_.hit &&
      pressed_hit_.kind == quader_editor_core::ViewCubeHitKind::Face) {
    camera.view_axis(viewport_axis(pressed_hit_.axis), pressed_hit_.sign,
                     ViewportAxisLock::Unlocked);
  }
  camera.end_drag();
  dragging_ = false;
  drag_started_ = false;
  accumulated_drag_ = {};
  pressed_hit_ = {};
  return true;
}

void QtNavigationGizmoInteraction::cancel(
    ViewportCameraController &camera) {
  if (!dragging_) {
    return;
  }
  camera.end_drag();
  dragging_ = false;
  drag_started_ = false;
  accumulated_drag_ = {};
  pressed_hit_ = {};
}

void QtNavigationGizmoInteraction::abandon() {
  dragging_ = false;
  drag_started_ = false;
  pane_id_ = ViewportPaneId::Perspective;
  accumulated_drag_ = {};
  pressed_hit_ = {};
}

QtNavigationGizmo::QtNavigationGizmo(ViewportPaneId pane_id,
                                     QWidget *parent)
    : QFrame(parent), pane_id_(pane_id) {
  setObjectName(QStringLiteral("viewportNavigationGizmo_%1")
                    .arg(QString::fromUtf8(
                        viewport_pane_stable_id(pane_id).data())));
  setFrameShape(QFrame::StyledPanel);
  setFrameShadow(QFrame::Plain);
  setFocusPolicy(Qt::NoFocus);
  setMouseTracking(true);
  setAutoFillBackground(true);
  setAttribute(Qt::WA_NativeWindow);
  hide();
}

void QtNavigationGizmo::set_camera(ViewportCameraController *camera) {
  const QtNavigationGizmoCameraSnapshot next_snapshot =
      qt_navigation_gizmo_camera_snapshot(camera);
  if (!qt_navigation_gizmo_camera_refresh_required(camera_snapshot_,
                                                    next_snapshot)) {
    return;
  }
  const bool camera_changed = camera_ != camera;
  if (camera_changed) {
    finish_interaction(true);
  }
  camera_ = camera;
  camera_snapshot_ = next_snapshot;
  if (camera_changed) {
    hover_hit_ = {};
  }
  update();
}

void QtNavigationGizmo::abandon_camera() {
  interaction_.abandon();
  camera_ = nullptr;
  camera_snapshot_ = qt_navigation_gizmo_camera_snapshot(nullptr);
  if (QWidget::mouseGrabber() == this) {
    releaseMouse();
  }
  unsetCursor();
  hover_hit_ = {};
  interaction_activity_ = false;
  update();
}

void QtNavigationGizmo::set_gizmo_visible(bool visible) {
  if (!qt_navigation_gizmo_visibility_refresh_required(gizmo_visible_,
                                                        visible)) {
    return;
  }
  gizmo_visible_ = visible;
  if (!visible) {
    finish_interaction(true);
    hover_hit_ = {};
  }
  setVisible(visible);
  if (visible) {
    update();
  }
}

void QtNavigationGizmo::set_focus_pane_callback(
    FocusPaneCallback callback) {
  focus_pane_callback_ = std::move(callback);
}

bool QtNavigationGizmo::take_interaction_activity() {
  const bool activity = interaction_activity_;
  interaction_activity_ = false;
  return activity;
}

void QtNavigationGizmo::paintEvent(QPaintEvent *event) {
  QFrame::paintEvent(event);
  if (camera_ == nullptr) {
    return;
  }
  const quader_editor_core::ViewCubePrimitives primitives =
      interaction_.primitives(*camera_, viewport_size());
  if (!primitives.ok) {
    return;
  }

  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);
  const auto colors = palette();
  for (const quader_editor_core::ViewCubeFacePrimitive &face :
       primitives.faces) {
    if (face.polygon.size() < 3u) {
      continue;
    }
    QPainterPath path;
    path.moveTo(painter_point(face.polygon.front()));
    for (std::size_t index = 1; index < face.polygon.size(); ++index) {
      path.lineTo(painter_point(face.polygon[index]));
    }
    path.closeSubpath();

    const bool pressed = interaction_.input_locked() &&
                         !interaction_.drag_started() &&
                         qt_navigation_gizmo_hits_match(
                             interaction_.pressed_hit(),
                             {.hit = true,
                              .kind =
                                  quader_editor_core::ViewCubeHitKind::Face,
                              .axis = face.axis,
                              .sign = face.sign});
    const bool highlighted = pressed ||
                             (hover_hit_.hit &&
                              hover_hit_.kind == quader_editor_core::
                                                     ViewCubeHitKind::Face &&
                              hover_hit_.axis == face.axis &&
                              hover_hit_.sign == face.sign);
    QColor fill = highlighted ? colors.highlight().color()
                              : colors.button().color();
    if (!highlighted) {
      const int factor = static_cast<int>(std::lround(
          92.0 + std::clamp(face.depth, 0.0, 1.0) * 16.0));
      fill = fill.lighter(factor);
    }
    painter.fillPath(path, fill);
  }

  QColor neutral_edge = colors.buttonText().color();
  neutral_edge.setAlpha(205);
  painter.setPen(QPen(neutral_edge, 1.5));
  for (const quader_editor_core::ViewCubeEdgePrimitive &edge :
       primitives.edges) {
    painter.drawLine(painter_point(edge.screen_start),
                     painter_point(edge.screen_end));
  }

  for (const quader_editor_core::ViewCubeEdgePrimitive &edge :
       primitives.axis_edges) {
    painter.setPen(QPen(qt_semantic_axis_color(edge.axis), 2.8));
    painter.drawLine(painter_point(edge.screen_start),
                     painter_point(edge.screen_end));
  }

  for (const quader_editor_core::ViewCubeAxisMarkerPrimitive &marker :
       primitives.axis_markers) {
    const QPointF center = painter_point(marker.screen_position);
    painter.setPen(QPen(colors.shadow().color(), 1.2));
    painter.setBrush(qt_semantic_axis_color(marker.axis));
    painter.drawEllipse(center, kAxisMarkerRadius, kAxisMarkerRadius);
    // Axis fills are deliberately bright semantic colors. The palette shadow
    // keeps their compact labels readable in both light and dark themes.
    painter.setPen(colors.shadow().color());
    const std::string_view label =
        quader_editor_core::view_axis_label(marker.axis, marker.sign);
    const QRectF label_rect(center.x() - kAxisMarkerRadius,
                            center.y() - kAxisMarkerRadius,
                            kAxisMarkerRadius * 2.0,
                            kAxisMarkerRadius * 2.0);
    painter.drawText(label_rect, Qt::AlignCenter,
                     QString::fromUtf8(label.data(),
                                       static_cast<qsizetype>(label.size())));
  }
}

void QtNavigationGizmo::mousePressEvent(QMouseEvent *event) {
  if (event == nullptr || event->button() != Qt::LeftButton ||
      camera_ == nullptr) {
    QFrame::mousePressEvent(event);
    return;
  }
  if (!interaction_.press(*camera_, pane_id_, viewport_size(),
                          local_position(event->position()))) {
    QFrame::mousePressEvent(event);
    return;
  }

  last_pointer_position_ = event->position();
  if (focus_pane_callback_) {
    focus_pane_callback_(pane_id_);
  }
  grabMouse();
  setCursor(Qt::BlankCursor);
  mark_interaction_activity();
  event->accept();
  update();
}

void QtNavigationGizmo::mouseMoveEvent(QMouseEvent *event) {
  if (event == nullptr || camera_ == nullptr) {
    QFrame::mouseMoveEvent(event);
    return;
  }
  const quader_editor_core::ViewCubeHit next_hover =
      interaction_.hit_test(*camera_, viewport_size(),
                            local_position(event->position()));
  const bool hover_changed =
      !qt_navigation_gizmo_hits_match(hover_hit_, next_hover);
  hover_hit_ = next_hover;
  if (!interaction_.input_locked()) {
    if (hover_changed) {
      update();
    }
    QFrame::mouseMoveEvent(event);
    return;
  }
  if (!event->buttons().testFlag(Qt::LeftButton)) {
    finish_interaction(false);
    event->accept();
    return;
  }

  const QPointF logical_delta = event->position() - last_pointer_position_;
  const qreal scale = std::max<qreal>(devicePixelRatioF(), 1.0);
  const QVec2 delta{static_cast<float>(logical_delta.x() * scale),
                    static_cast<float>(logical_delta.y() * scale)};
  last_pointer_position_ = event->position();
  (void)interaction_.drag(*camera_, pane_id_, delta);
  camera_snapshot_ = qt_navigation_gizmo_camera_snapshot(camera_);
  mark_interaction_activity();
  event->accept();
  update();
}

void QtNavigationGizmo::mouseReleaseEvent(QMouseEvent *event) {
  if (event == nullptr || event->button() != Qt::LeftButton ||
      !interaction_.input_locked()) {
    QFrame::mouseReleaseEvent(event);
    return;
  }
  finish_interaction(false);
  event->accept();
}

void QtNavigationGizmo::leaveEvent(QEvent *event) {
  if (!interaction_.input_locked()) {
    hover_hit_ = {};
    update();
  }
  QFrame::leaveEvent(event);
}

void QtNavigationGizmo::hideEvent(QHideEvent *event) {
  finish_interaction(true);
  QFrame::hideEvent(event);
}

QVec2 QtNavigationGizmo::viewport_size() const {
  return {static_cast<float>(std::max(width(), 1)),
          static_cast<float>(std::max(height(), 1))};
}

QVec2 QtNavigationGizmo::local_position(QPointF position) const {
  return {static_cast<float>(position.x()),
          static_cast<float>(position.y())};
}

void QtNavigationGizmo::finish_interaction(bool cancel) {
  if (!interaction_.input_locked() || camera_ == nullptr) {
    return;
  }
  if (cancel) {
    interaction_.cancel(*camera_);
  } else {
    (void)interaction_.release(*camera_, pane_id_);
  }
  camera_snapshot_ = qt_navigation_gizmo_camera_snapshot(camera_);
  if (QWidget::mouseGrabber() == this) {
    releaseMouse();
  }
  unsetCursor();
  hover_hit_ = {};
  mark_interaction_activity();
  update();
}

void QtNavigationGizmo::mark_interaction_activity() {
  interaction_activity_ = true;
}

} // namespace quader
