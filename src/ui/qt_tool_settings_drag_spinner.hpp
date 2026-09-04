////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <QWidget>

#include <functional>

class QEvent;
class QLineEdit;
class QSpinBox;
class QString;
class QToolButton;

namespace quader {

/**
 * Compact integer editor with Blender-style horizontal value scrubbing.
 *
 * The child QSpinBox remains the text, validation, locale, keyboard, IME, and
 * accessibility owner. This wrapper adds one-step arrow buttons and turns a
 * display-mode horizontal drag into an absolute, bounded value scrub.
 */
class QtToolSettingsDragSpinner final : public QWidget {
public:
  explicit QtToolSettingsDragSpinner(QWidget *parent = nullptr);

  [[nodiscard]] int minimum() const;
  [[nodiscard]] int maximum() const;
  [[nodiscard]] int value() const;
  [[nodiscard]] int single_step() const;
  [[nodiscard]] QSpinBox *spin_box() const;

  void set_range(int minimum, int maximum);
  void set_value(int value);
  void set_single_step(int step);
  void set_accessible_label(const QString &label);

  [[nodiscard]] bool is_editing() const;
  [[nodiscard]] bool is_dragging() const;

  std::function<void(int)> value_changed;
  std::function<void()> editing_started;
  std::function<void()> editing_canceled;
  std::function<void()> editing_finished;
  std::function<void()> drag_started;
  std::function<void()> drag_finished;

protected:
  bool event(QEvent *event) override;
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  void step_once(int direction);
  void begin_editing();
  void commit_editing();
  void cancel_editing();
  void finish_editing(bool canceled = false);
  void begin_drag(double pointer_x);
  void update_drag(double pointer_x);
  void finish_drag();
  void apply_value(int value);
  void refresh_button_extents();
  void synchronize_buttons();
  void synchronize_editor_cursor();

  QToolButton *decrease_button_ = nullptr;
  QSpinBox *spin_box_ = nullptr;
  QLineEdit *line_edit_ = nullptr;
  QToolButton *increase_button_ = nullptr;

  bool press_pending_ = false;
  bool editing_ = false;
  bool dragging_ = false;
  bool finishing_edit_ = false;
  double press_pointer_x_ = 0.0;
  double drag_anchor_x_ = 0.0;
  double last_pointer_x_ = 0.0;
  int press_value_ = 0;
  int drag_anchor_value_ = 0;
  int edit_original_value_ = 0;
};

} // namespace quader
