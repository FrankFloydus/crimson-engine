////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/qt_ui_scale_metrics.hpp"

#include <QPointer>
#include <QWidget>

#include <vector>

class QAction;
class QHBoxLayout;
class QMenu;
class QStyle;
class QToolButton;

namespace quader {

class QtViewportControlStrip;

/// Finds the viewport chrome that owns a control or one of its native menus.
[[nodiscard]] const QtViewportControlStrip *
qt_viewport_control_strip_ancestor(const QObject *object);

/** Fixed viewport chrome presenting retained pane actions and native menus. */
class QtViewportControlStrip final : public QWidget {
public:
  explicit QtViewportControlStrip(QWidget *parent = nullptr);
  ~QtViewportControlStrip() override;

  void add_action(QAction &action);
  void add_separator();
  void set_trailing_controls(QWidget *camera, QWidget *shading,
                             QWidget *environment = nullptr,
                             QAction *environment_overflow = nullptr);
  void apply_ui_scale(const QtUiScaleMetrics &metrics);
  [[nodiscard]] QSize sizeHint() const override;
  [[nodiscard]] QSize minimumSizeHint() const override;

protected:
  bool event(QEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  void paintEvent(QPaintEvent *event) override;

private:
  /** Preserves authored action/separator order across visible and overflow hosts. */
  struct Entry {
    QPointer<QAction> action;
    QPointer<QWidget> widget;
    bool separator = false;
  };

  void scale_button(QToolButton &button);
  void update_overflow();
  void ensure_overflow_button();
  void update_overflow_icon();

  QHBoxLayout *layout_ = nullptr;
  QStyle *button_style_ = nullptr;
  std::vector<Entry> entries_;
  std::vector<QPointer<QToolButton>> buttons_;
  std::vector<QWidget *> separators_;
  QPointer<QWidget> camera_;
  QPointer<QWidget> shading_;
  QPointer<QWidget> environment_;
  QPointer<QAction> environment_overflow_;
  QToolButton *overflow_button_ = nullptr;
  QAction *overflow_action_ = nullptr;
  QMenu *overflow_menu_ = nullptr;
  bool updating_overflow_ = false;
  QtUiScaleMetrics metrics_;
};

} // namespace quader
