////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_viewport_layout_types.hpp"
#include "editor/interactions/viewport_navigation.hpp"
#include "viewport/viewport_camera_controller.hpp"

#include <QFrame>
#include <QPointF>

#include <functional>
#include <optional>

class QHideEvent;
class QLeaveEvent;
class QMouseEvent;
class QPaintEvent;

namespace quader {

/// Captures the camera identity and normalized orientation that can invalidate
/// the retained gizmo image. Camera translation and zoom do not affect the
/// view-cube projection.
struct QtNavigationGizmoCameraSnapshot {
  const ViewportCameraController *camera = nullptr;
  QVec3 forward = {0.0f, 0.0f, -1.0f};
  QVec3 right = {1.0f, 0.0f, 0.0f};
  QVec3 up = {0.0f, 1.0f, 0.0f};
};

[[nodiscard]] QtNavigationGizmoCameraSnapshot
qt_navigation_gizmo_camera_snapshot(
    const ViewportCameraController *camera);
[[nodiscard]] bool qt_navigation_gizmo_camera_refresh_required(
    const std::optional<QtNavigationGizmoCameraSnapshot> &current,
    const QtNavigationGizmoCameraSnapshot &next);
[[nodiscard]] bool qt_navigation_gizmo_hits_match(
    const quader_editor_core::ViewCubeHit &left,
    const quader_editor_core::ViewCubeHit &right);

[[nodiscard]] constexpr bool
qt_navigation_gizmo_visibility_refresh_required(bool current,
                                                 bool requested) {
  return current != requested;
}

/**
 * Owns the toolkit-independent press/drag/release state for one Qt view cube.
 * Geometry and picking remain delegated to quader_editor_core.
 */
class QtNavigationGizmoInteraction final {
public:
  [[nodiscard]] quader_editor_core::ViewCubePrimitiveInput
  primitive_input(const ViewportCameraController &camera,
                  QVec2 viewport_size) const;
  [[nodiscard]] quader_editor_core::ViewCubePrimitives
  primitives(const ViewportCameraController &camera,
             QVec2 viewport_size) const;
  [[nodiscard]] quader_editor_core::ViewCubeHit
  hit_test(const ViewportCameraController &camera, QVec2 viewport_size,
           QVec2 local_position) const;

  bool press(ViewportCameraController &camera, ViewportPaneId pane_id,
             QVec2 viewport_size, QVec2 local_position);
  bool drag(ViewportCameraController &camera, ViewportPaneId pane_id,
            QVec2 delta);
  bool release(ViewportCameraController &camera, ViewportPaneId pane_id);
  void cancel(ViewportCameraController &camera);
  // Clears retained pointer state without touching the camera. Runtime rebinds
  // use this after the previous camera owner may already have been released.
  void abandon();

  [[nodiscard]] bool input_locked() const { return dragging_; }
  [[nodiscard]] bool drag_started() const { return drag_started_; }
  [[nodiscard]] ViewportPaneId locked_pane_id() const { return pane_id_; }
  [[nodiscard]] const quader_editor_core::ViewCubeHit &pressed_hit() const {
    return pressed_hit_;
  }

private:
  bool dragging_ = false;
  bool drag_started_ = false;
  ViewportPaneId pane_id_ = ViewportPaneId::Perspective;
  QVec2 accumulated_drag_ = {};
  quader_editor_core::ViewCubeHit pressed_hit_ = {};
};

/**
 * Stock-Fusion Qt presentation for one pane's interactive navigation gizmo.
 * The only explicit colors are the editor's semantic X/Y/Z axis colors.
 */
class QtNavigationGizmo final : public QFrame {
public:
  using FocusPaneCallback = std::function<void(ViewportPaneId)>;

  explicit QtNavigationGizmo(ViewportPaneId pane_id,
                             QWidget *parent = nullptr);

  void set_camera(ViewportCameraController *camera);
  // Detaches a potentially expired non-owning camera without dereferencing it.
  void abandon_camera();
  void set_gizmo_visible(bool visible);
  void set_focus_pane_callback(FocusPaneCallback callback);

  [[nodiscard]] bool input_locked() const {
    return interaction_.input_locked();
  }
  [[nodiscard]] ViewportPaneId locked_pane_id() const {
    return interaction_.locked_pane_id();
  }
  [[nodiscard]] bool take_interaction_activity();

protected:
  void paintEvent(QPaintEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void leaveEvent(QEvent *event) override;
  void hideEvent(QHideEvent *event) override;

private:
  [[nodiscard]] QVec2 viewport_size() const;
  [[nodiscard]] QVec2 local_position(QPointF position) const;
  void finish_interaction(bool cancel);
  void mark_interaction_activity();

  ViewportPaneId pane_id_ = ViewportPaneId::Perspective;
  ViewportCameraController *camera_ = nullptr;
  std::optional<QtNavigationGizmoCameraSnapshot> camera_snapshot_;
  QtNavigationGizmoInteraction interaction_;
  quader_editor_core::ViewCubeHit hover_hit_ = {};
  QPointF last_pointer_position_ = {};
  FocusPaneCallback focus_pane_callback_;
  bool interaction_activity_ = false;
  bool gizmo_visible_ = false;
};

} // namespace quader
