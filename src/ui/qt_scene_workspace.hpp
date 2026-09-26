////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/qt_ui_scale_metrics.hpp"

#include <QString>
#include <QWidget>

#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <vector>

class QLabel;
class QHBoxLayout;
class QPushButton;
class QStackedWidget;
class QTabBar;
class QToolButton;
class QVBoxLayout;

namespace quader {

using QtSceneSessionId = std::uint64_t;
inline constexpr QtSceneSessionId kInvalidQtSceneSessionId = 0;

/// Carries read-only presentation state for one open scene tab.
struct QtSceneTabItem {
  QtSceneSessionId session_id = kInvalidQtSceneSessionId;
  QString name;
  QString project_relative_path;
  bool dirty = false;

  bool operator==(const QtSceneTabItem &) const = default;
};

/// Carries user-facing workspace text from the localization boundary.
struct QtSceneWorkspaceLabels {
  QString untitled_scene;
  QString unsaved_scene;
  QString new_scene;
  QString open_scene;
  QString empty_title;
  QString empty_description;
  QString return_to_scene;
  QString editing_instance;

  [[nodiscard]] static QtSceneWorkspaceLabels defaults();
};

/// Transient presentation state for the instance-edit breadcrumb row.
struct QtInstanceEditBreadcrumb {
  QString scene_name;
  QString instance_name;

  bool operator==(const QtInstanceEditBreadcrumb &) const = default;
};

/**
 * Computes tab labels, adding the shortest unique parent suffix only where
 * scene names collide. Dirty state is represented by a trailing asterisk.
 */
[[nodiscard]] std::vector<QString> qt_scene_tab_display_labels(
    std::span<const QtSceneTabItem> scenes,
    const QString &untitled_scene = QStringLiteral("Untitled Scene"));

/** Returns a forward-slash project-relative tooltip or the unsaved fallback. */
[[nodiscard]] QString qt_scene_tab_tooltip(
    const QtSceneTabItem &scene,
    const QString &unsaved_scene = QStringLiteral("Unsaved scene"));

/**
 * Stock-Qt shell that retains one viewport widget below a scene tab bar. Scene
 * ownership and mutations stay outside the widget and are dispatched through
 * request callbacks. The Qt parent hierarchy assumes ownership of a supplied
 * viewport widget.
 */
class QtSceneWorkspace final : public QWidget {
public:
  using RequestCallback = std::function<void()>;
  using SceneRequestCallback = std::function<void(QtSceneSessionId)>;

  explicit QtSceneWorkspace(QWidget *parent = nullptr);

  /** Reflows retained scene chrome from immutable 100% design metrics. */
  void apply_ui_scale(const QtUiScaleMetrics &metrics);

  void set_labels(QtSceneWorkspaceLabels labels);
  void set_viewport_widget(QWidget *viewport_widget);
  [[nodiscard]] QWidget *viewport_widget() const { return viewport_widget_; }

  /**
   * Replaces the presentation projection. Invalid or duplicate session IDs
   * reject the projection and leave the current tabs unchanged.
   */
  [[nodiscard]] bool set_scene_tabs(
      std::vector<QtSceneTabItem> scenes,
      std::optional<QtSceneSessionId> active_scene = std::nullopt);
  [[nodiscard]] bool set_active_scene(QtSceneSessionId session_id);

  void set_new_scene_requested_callback(RequestCallback callback);
  void set_open_scene_requested_callback(RequestCallback callback);
  void set_activate_scene_requested_callback(SceneRequestCallback callback);
  void set_close_scene_requested_callback(SceneRequestCallback callback);
  void set_exit_instance_edit_requested_callback(RequestCallback callback);

  /** Shows or hides the compact row below the unchanged scene tab bar. */
  void set_instance_edit_breadcrumb(
      std::optional<QtInstanceEditBreadcrumb> breadcrumb);
  [[nodiscard]] const std::optional<QtInstanceEditBreadcrumb> &
  instance_edit_breadcrumb() const {
    return instance_edit_breadcrumb_;
  }

  [[nodiscard]] const std::vector<QtSceneTabItem> &scene_tabs() const {
    return scenes_;
  }
  [[nodiscard]] std::optional<QtSceneSessionId> active_scene() const;
  [[nodiscard]] QTabBar &tab_bar() { return *tab_bar_; }
  [[nodiscard]] const QTabBar &tab_bar() const { return *tab_bar_; }

private:
  [[nodiscard]] int tab_index(QtSceneSessionId session_id) const;
  [[nodiscard]] std::optional<QtSceneSessionId>
  session_id_at(int index) const;
  void rebuild_tabs(std::optional<QtSceneSessionId> active_scene);
  void refresh_labels();
  void refresh_instance_edit_breadcrumb();
  void refresh_content_page();

  QtSceneWorkspaceLabels labels_ = QtSceneWorkspaceLabels::defaults();
  std::vector<QtSceneTabItem> scenes_;
  QTabBar *tab_bar_ = nullptr;
  QWidget *instance_edit_bar_ = nullptr;
  QHBoxLayout *instance_edit_layout_ = nullptr;
  QToolButton *instance_scene_crumb_ = nullptr;
  QLabel *instance_separator_ = nullptr;
  QLabel *instance_name_crumb_ = nullptr;
  QStackedWidget *content_stack_ = nullptr;
  QWidget *empty_page_ = nullptr;
  QLabel *empty_title_label_ = nullptr;
  QLabel *empty_description_label_ = nullptr;
  QPushButton *empty_new_scene_button_ = nullptr;
  QPushButton *empty_open_scene_button_ = nullptr;
  QWidget *viewport_page_ = nullptr;
  QVBoxLayout *viewport_layout_ = nullptr;
  QWidget *viewport_widget_ = nullptr;
  RequestCallback new_scene_requested_;
  RequestCallback open_scene_requested_;
  SceneRequestCallback activate_scene_requested_;
  SceneRequestCallback close_scene_requested_;
  RequestCallback exit_instance_edit_requested_;
  std::optional<QtInstanceEditBreadcrumb> instance_edit_breadcrumb_;
  float applied_ui_scale_factor_ = 0.0F;
  std::uint64_t applied_ui_scale_revision_ =
      std::numeric_limits<std::uint64_t>::max();
};

} // namespace quader
