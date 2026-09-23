////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_tool_settings_drag_spinner.hpp"

#include "ui/qt_numeric_spin_box.hpp"
#include "ui/qt_spin_box.hpp"

#include <QHBoxLayout>
#include <QLineEdit>
#include <QSizePolicy>
#include <QString>

#include <algorithm>
#include <utility>

namespace quader {
namespace {

/// Keeps compact tool rows wide enough for their five-digit integer values.
class ToolSettingsSpinBox final : public QtSpinBox {
public:
  explicit ToolSettingsSpinBox(QWidget *parent) : QtSpinBox(parent) {
    lineEdit()->setObjectName(QStringLiteral("drag_spinner.editor.line_edit"));
  }

  [[nodiscard]] QSize sizeHint() const override {
    QSize size = QtSpinBox::sizeHint();
    const int range_text_width = std::max(
        fontMetrics().horizontalAdvance(textFromValue(minimum())),
        fontMetrics().horizontalAdvance(textFromValue(maximum())));
    const int five_digit_width =
        fontMetrics().horizontalAdvance(QStringLiteral("99999"));
    size.rwidth() += std::max(0, five_digit_width - range_text_width);
    return size;
  }
};

} // namespace

QtToolSettingsDragSpinner::QtToolSettingsDragSpinner(QWidget *parent)
    : QWidget(parent) {
  spin_box_ = new ToolSettingsSpinBox(this);
  spin_box_->setObjectName(QStringLiteral("drag_spinner.editor"));
  spin_box_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
  numeric_controller_ = &qt_install_numeric_spin_box(*spin_box_);

  auto *layout = new QHBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addWidget(spin_box_);
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
  setFocusProxy(spin_box_);

  connect(spin_box_, &QSpinBox::valueChanged, this, [this](int next_value) {
    // Text drafts are local until committed; a live scrub may already have
    // published previews, so its restored value must reach the same owner.
    if ((!numeric_controller_->is_canceling() ||
         numeric_controller_->is_dragging()) &&
        value_changed) {
      value_changed(next_value);
    }
  });
  numeric_controller_->editing_started = [this]() {
    if (editing_started) {
      editing_started();
    }
  };
  numeric_controller_->editing_canceled = [this]() {
    if (editing_canceled) {
      editing_canceled();
    }
  };
  numeric_controller_->editing_finished = [this]() {
    if (editing_finished) {
      editing_finished();
    }
  };
  numeric_controller_->drag_started = [this]() {
    if (drag_started) {
      drag_started();
    }
  };
  numeric_controller_->drag_finished = [this]() {
    if (drag_finished) {
      drag_finished();
    }
  };
}

int QtToolSettingsDragSpinner::minimum() const { return spin_box_->minimum(); }

int QtToolSettingsDragSpinner::maximum() const { return spin_box_->maximum(); }

int QtToolSettingsDragSpinner::value() const { return spin_box_->value(); }

int QtToolSettingsDragSpinner::single_step() const {
  return spin_box_->singleStep();
}

QSpinBox *QtToolSettingsDragSpinner::spin_box() const { return spin_box_; }

void QtToolSettingsDragSpinner::set_range(int minimum_value, int maximum_value) {
  if (maximum_value < minimum_value) {
    std::swap(minimum_value, maximum_value);
  }
  if (minimum_value == minimum() && maximum_value == maximum()) {
    return;
  }
  spin_box_->setRange(minimum_value, maximum_value);
  numeric_controller_->rebase_drag();
}

void QtToolSettingsDragSpinner::set_value(int next_value) {
  if (next_value != value()) {
    spin_box_->setValue(next_value);
    numeric_controller_->rebase_drag();
  }
}

void QtToolSettingsDragSpinner::set_single_step(int step) {
  const int bounded_step = std::max(1, step);
  if (bounded_step != single_step()) {
    spin_box_->setSingleStep(bounded_step);
  }
}

void QtToolSettingsDragSpinner::set_accessible_label(const QString &label) {
  setAccessibleName(label);
  spin_box_->setAccessibleName(label);
}

bool QtToolSettingsDragSpinner::is_editing() const {
  return numeric_controller_->is_editing();
}

bool QtToolSettingsDragSpinner::is_dragging() const {
  return numeric_controller_->is_dragging();
}

} // namespace quader
