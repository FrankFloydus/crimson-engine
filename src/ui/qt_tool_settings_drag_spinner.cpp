////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_tool_settings_drag_spinner.hpp"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QSizePolicy>
#include <QSpinBox>
#include <QString>
#include <QStyle>
#include <QStyleOptionSpinBox>
#include <QToolButton>

#include <algorithm>
#include <cmath>
#include <utility>

namespace quader {
namespace {

constexpr double kLogicalPixelsPerStep = 8.0;

class DragSpinnerSpinBox final : public QSpinBox {
public:
  explicit DragSpinnerSpinBox(QWidget *parent = nullptr) : QSpinBox(parent) {}

  [[nodiscard]] QLineEdit *editor() const { return lineEdit(); }

  [[nodiscard]] QSize sizeHint() const override {
    QStyleOptionSpinBox option;
    initStyleOption(&option);
    const QSize five_digit_contents(
        fontMetrics().horizontalAdvance(QStringLiteral("99999")),
        editor()->sizeHint().height());
    return QSpinBox::sizeHint().expandedTo(style()->sizeFromContents(
        QStyle::CT_SpinBox, &option, five_digit_contents, this));
  }

  void restore_canonical_text() {
    lineEdit()->setText(prefix() + textFromValue(value()) + suffix());
    lineEdit()->setModified(false);
  }
};

int bounded_value(long long value, int minimum, int maximum) {
  return static_cast<int>(std::clamp(value, static_cast<long long>(minimum),
                                     static_cast<long long>(maximum)));
}

} // namespace

QtToolSettingsDragSpinner::QtToolSettingsDragSpinner(QWidget *parent)
    : QWidget(parent) {
  decrease_button_ = new QToolButton(this);
  decrease_button_->setObjectName(QStringLiteral("drag_spinner.decrease"));
  decrease_button_->setArrowType(Qt::LeftArrow);
  decrease_button_->setAutoRepeat(true);
  decrease_button_->setFocusPolicy(Qt::NoFocus);
  decrease_button_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

  auto *owned_spin_box = new DragSpinnerSpinBox(this);
  spin_box_ = owned_spin_box;
  spin_box_->setObjectName(QStringLiteral("drag_spinner.editor"));
  spin_box_->setButtonSymbols(QAbstractSpinBox::NoButtons);
  spin_box_->setAlignment(Qt::AlignCenter);
  spin_box_->setReadOnly(true);
  spin_box_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

  line_edit_ = owned_spin_box->editor();
  line_edit_->setObjectName(QStringLiteral("drag_spinner.editor.line_edit"));
  line_edit_->installEventFilter(this);

  increase_button_ = new QToolButton(this);
  increase_button_->setObjectName(QStringLiteral("drag_spinner.increase"));
  increase_button_->setArrowType(Qt::RightArrow);
  increase_button_->setAutoRepeat(true);
  increase_button_->setFocusPolicy(Qt::NoFocus);
  increase_button_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

  const int button_extent =
      std::max(style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, this),
               spin_box_->sizeHint().height());
  decrease_button_->setFixedWidth(button_extent);
  increase_button_->setFixedWidth(button_extent);

  auto *layout = new QHBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addWidget(decrease_button_);
  layout->addWidget(spin_box_);
  layout->addWidget(increase_button_);

  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

  setFocusProxy(spin_box_);
  synchronize_editor_cursor();
  synchronize_buttons();

  connect(decrease_button_, &QToolButton::clicked, this,
          [this]() { step_once(-1); });
  connect(increase_button_, &QToolButton::clicked, this,
          [this]() { step_once(1); });
  connect(spin_box_, &QSpinBox::valueChanged, this, [this](int next_value) {
    synchronize_buttons();
    if (!finishing_edit_ && value_changed) {
      value_changed(next_value);
    }
  });
  connect(spin_box_, &QSpinBox::editingFinished, this,
          [this]() { commit_editing(); });
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
  edit_original_value_ =
      std::clamp(edit_original_value_, minimum_value, maximum_value);
  if (dragging_) {
    drag_anchor_x_ = last_pointer_x_;
    drag_anchor_value_ = value();
  }
  synchronize_buttons();
}

void QtToolSettingsDragSpinner::set_value(int next_value) {
  if (next_value == value()) {
    return;
  }
  spin_box_->setValue(next_value);
  if (dragging_) {
    drag_anchor_x_ = last_pointer_x_;
    drag_anchor_value_ = value();
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
  decrease_button_->setAccessibleName(label + QStringLiteral(" -"));
  increase_button_->setAccessibleName(label + QStringLiteral(" +"));
}

bool QtToolSettingsDragSpinner::is_editing() const { return editing_; }

bool QtToolSettingsDragSpinner::is_dragging() const { return dragging_; }

bool QtToolSettingsDragSpinner::event(QEvent *event) {
  const bool interaction_interrupted =
      event->type() == QEvent::Hide ||
      event->type() == QEvent::WindowDeactivate ||
      (event->type() == QEvent::EnabledChange && !isEnabled());
  if (interaction_interrupted) {
    press_pending_ = false;
    if (dragging_) {
      finish_drag();
    }
    if (editing_) {
      commit_editing();
    }
  }
  return QWidget::event(event);
}

bool QtToolSettingsDragSpinner::eventFilter(QObject *watched, QEvent *event) {
  if (watched != line_edit_) {
    return QWidget::eventFilter(watched, event);
  }

  if (event->type() == QEvent::UngrabMouse) {
    press_pending_ = false;
    if (dragging_) {
      finish_drag();
    }
    return QWidget::eventFilter(watched, event);
  }

  if (editing_) {
    if (event->type() == QEvent::KeyPress) {
      auto *key_event = static_cast<QKeyEvent *>(event);
      if (key_event->key() == Qt::Key_Escape) {
        cancel_editing();
        key_event->accept();
        return true;
      }
    }
    return QWidget::eventFilter(watched, event);
  }

  switch (event->type()) {
  case QEvent::MouseButtonPress: {
    auto *mouse_event = static_cast<QMouseEvent *>(event);
    if (mouse_event->button() != Qt::LeftButton || !isEnabled()) {
      break;
    }
    press_pending_ = true;
    press_pointer_x_ = mouse_event->globalPosition().x();
    last_pointer_x_ = press_pointer_x_;
    press_value_ = value();
    mouse_event->accept();
    return true;
  }
  case QEvent::MouseMove: {
    auto *mouse_event = static_cast<QMouseEvent *>(event);
    if (!press_pending_ && !dragging_) {
      break;
    }
    if (!(mouse_event->buttons() & Qt::LeftButton)) {
      press_pending_ = false;
      if (dragging_) {
        finish_drag();
      }
      break;
    }
    last_pointer_x_ = mouse_event->globalPosition().x();
    if (!dragging_) {
      const double travel = last_pointer_x_ - press_pointer_x_;
      const int threshold = std::max(1, QApplication::startDragDistance());
      if (std::abs(travel) >= static_cast<double>(threshold)) {
        begin_drag(last_pointer_x_);
      }
    } else {
      update_drag(last_pointer_x_);
    }
    mouse_event->accept();
    return true;
  }
  case QEvent::MouseButtonRelease: {
    auto *mouse_event = static_cast<QMouseEvent *>(event);
    if (mouse_event->button() != Qt::LeftButton ||
        (!press_pending_ && !dragging_)) {
      break;
    }
    last_pointer_x_ = mouse_event->globalPosition().x();
    if (dragging_) {
      update_drag(last_pointer_x_);
      finish_drag();
    } else {
      press_pending_ = false;
      begin_editing();
    }
    mouse_event->accept();
    return true;
  }
  default:
    break;
  }

  return QWidget::eventFilter(watched, event);
}

void QtToolSettingsDragSpinner::step_once(int direction) {
  if (direction == 0) {
    return;
  }
  const long long delta = static_cast<long long>(direction) * single_step();
  apply_value(bounded_value(static_cast<long long>(value()) + delta, minimum(),
                            maximum()));
}

void QtToolSettingsDragSpinner::begin_editing() {
  if (editing_ || dragging_ || !isEnabled()) {
    return;
  }
  editing_ = true;
  edit_original_value_ = value();
  if (editing_started) {
    editing_started();
  }
  spin_box_->setReadOnly(false);
  synchronize_editor_cursor();
  spin_box_->setFocus(Qt::MouseFocusReason);
  line_edit_->selectAll();
}

void QtToolSettingsDragSpinner::commit_editing() {
  if (!editing_ || finishing_edit_) {
    return;
  }
  spin_box_->interpretText();
  finish_editing();
}

void QtToolSettingsDragSpinner::cancel_editing() {
  if (!editing_ || finishing_edit_) {
    return;
  }
  finishing_edit_ = true;
  spin_box_->setValue(edit_original_value_);
  static_cast<DragSpinnerSpinBox *>(spin_box_)->restore_canonical_text();
  finishing_edit_ = false;
  finish_editing(true);
}

void QtToolSettingsDragSpinner::finish_editing(bool canceled) {
  if (!editing_) {
    return;
  }
  editing_ = false;
  spin_box_->setReadOnly(true);
  synchronize_editor_cursor();
  if (canceled && editing_canceled) {
    editing_canceled();
  }
  if (editing_finished) {
    editing_finished();
  }
}

void QtToolSettingsDragSpinner::begin_drag(double pointer_x) {
  if (dragging_) {
    return;
  }
  press_pending_ = false;
  dragging_ = true;

  // Threshold travel only initiates the gesture. Every subsequent eight
  // logical pixels represent one complete singleStep from the unchanged press
  // value.
  drag_anchor_x_ = pointer_x;
  drag_anchor_value_ = press_value_;
  last_pointer_x_ = pointer_x;
  if (drag_started) {
    drag_started();
  }
}

void QtToolSettingsDragSpinner::update_drag(double pointer_x) {
  if (!dragging_) {
    return;
  }
  last_pointer_x_ = pointer_x;
  const long long step_count = static_cast<long long>(
      std::trunc((pointer_x - drag_anchor_x_) / kLogicalPixelsPerStep));
  const long long raw_value =
      static_cast<long long>(drag_anchor_value_) +
      step_count * static_cast<long long>(single_step());
  apply_value(bounded_value(raw_value, minimum(), maximum()));

  // Once pointer travel exceeds a bound, discard the unreachable overflow so
  // reversing direction changes the value after one normal scrub increment.
  if (raw_value < minimum() || raw_value > maximum()) {
    drag_anchor_x_ = pointer_x;
    drag_anchor_value_ = value();
  }
}

void QtToolSettingsDragSpinner::finish_drag() {
  if (!dragging_) {
    return;
  }
  dragging_ = false;
  press_pending_ = false;
  if (drag_finished) {
    drag_finished();
  }
}

void QtToolSettingsDragSpinner::apply_value(int next_value) {
  if (next_value != value()) {
    spin_box_->setValue(next_value);
  }
}

void QtToolSettingsDragSpinner::synchronize_buttons() {
  decrease_button_->setEnabled(value() > minimum());
  increase_button_->setEnabled(value() < maximum());
}

void QtToolSettingsDragSpinner::synchronize_editor_cursor() {
  line_edit_->setCursor(editing_ ? Qt::IBeamCursor : Qt::SizeHorCursor);
}

} // namespace quader
