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
class QStyle;
class QToolButton;

namespace quader {

/** Fixed viewport chrome presenting actions owned by the main toolbars. */
class QtViewportControlStrip final : public QWidget {
public:
  explicit QtViewportControlStrip(QWidget *parent = nullptr);
  ~QtViewportControlStrip() override;

  void add_action(QAction &action);
  void add_separator();
  void apply_ui_scale(const QtUiScaleMetrics &metrics);

protected:
  void paintEvent(QPaintEvent *event) override;

private:
  void scale_button(QToolButton &button);

  QHBoxLayout *layout_ = nullptr;
  QStyle *button_style_ = nullptr;
  std::vector<QPointer<QToolButton>> buttons_;
  std::vector<QWidget *> separators_;
  QtUiScaleMetrics metrics_;
};

} // namespace quader
