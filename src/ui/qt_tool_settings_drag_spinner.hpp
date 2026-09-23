////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <QWidget>

#include <functional>

class QSpinBox;
class QString;

namespace quader {

class QtNumericSpinBoxController;

/**
 * Compact tool-settings facade over the shared numeric input control.
 * Retains the panel's gesture callbacks and five-digit sizing contract.
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

private:
  QSpinBox *spin_box_ = nullptr;
  QtNumericSpinBoxController *numeric_controller_ = nullptr;
};

} // namespace quader
