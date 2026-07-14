////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <QString>
#include <QWidget>

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

class QLabel;
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

  [[nodiscard]] static QtSceneWorkspaceLabels defaults();
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
  void refresh_content_page();

  QtSceneWorkspaceLabels labels_ = QtSceneWorkspaceLabels::defaults();
  std::vector<QtSceneTabItem> scenes_;
  QTabBar *tab_bar_ = nullptr;
  QToolButton *new_scene_button_ = nullptr;
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
};

} // namespace quader
