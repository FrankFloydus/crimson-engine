////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_numeric_spin_box.hpp"
#include "ui/qt_horizontal_pointer_capture.hpp"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLineEdit>
#include <QMouseEvent>
#include <QSpinBox>
#include <QToolButton>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace quader {
namespace {

constexpr double kLogicalPixelsPerStep = 8.0;
constexpr auto kControllerName = "quader.numeric_spin_box.controller";
constexpr auto kInstallerName = "quader.numeric_spin_box.installer";

const QString &installer_name() {
  static const QString kName = QString::fromLatin1(kInstallerName);
  return kName;
}

int arrow_extent(const QAbstractSpinBox &spin_box) {
  return std::max(14, spin_box.fontMetrics().height() + 2);
}

bool is_numeric_spin_box(QObject *object) {
  return qobject_cast<QSpinBox *>(object) ||
         qobject_cast<QDoubleSpinBox *>(object);
}

void invoke_callback(const std::function<void()> &callback) {
  // A callback can synchronously delete its owner and this controller. Keep
  // its callable alive independently of the controller member being invoked.
  const auto retained = callback;
  if (retained) {
    retained();
  }
}

/// Adopts Qt-created numeric editors once, before their first presentation.
class NumericSpinBoxInstaller final : public QObject {
public:
  explicit NumericSpinBoxInstaller(QApplication &application)
      : QObject(&application) {
    setObjectName(installer_name());
    application.installEventFilter(this);
  }

  void set_snap_provider(QObject &context, std::function<double()> provider) {
    if (!provider && snap_context_ != &context) {
      return;
    }
    snap_context_ = provider ? &context : nullptr;
    snap_provider_ = std::move(provider);
    ++snap_generation_;
  }

  [[nodiscard]] double snap_increment() const {
    const QPointer<const NumericSpinBoxInstaller> guard(this);
    const QPointer<QObject> context = snap_context_;
    const auto provider = snap_provider_;
    const std::uint64_t generation = snap_generation_;
    if (!context || !provider) {
      return 1.0;
    }
    const double increment = provider();
    return guard && context && snap_generation_ == generation &&
                   std::isfinite(increment) && increment > 0.0
               ? increment
               : 1.0;
  }

  void begin_drag() { ++active_drag_count_; }
  void end_drag() {
    Q_ASSERT(active_drag_count_ > 0);
    if (active_drag_count_ > 0) {
      --active_drag_count_;
    }
  }
  [[nodiscard]] bool drag_active() const { return active_drag_count_ > 0; }

protected:
  bool eventFilter(QObject *watched, QEvent *event) override {
    if (event->type() == QEvent::Polish && is_numeric_spin_box(watched)) {
      qt_install_numeric_spin_box(*static_cast<QAbstractSpinBox *>(watched));
    }
    return QObject::eventFilter(watched, event);
  }

private:
  QPointer<QObject> snap_context_;
  std::function<double()> snap_provider_;
  std::uint64_t snap_generation_ = 0;
  int active_drag_count_ = 0;
};

double current_snap_increment() {
  auto *application = qobject_cast<QApplication *>(QCoreApplication::instance());
  if (!application) {
    return 1.0;
  }
  auto *installer = application->findChild<QObject *>(
      installer_name(), Qt::FindDirectChildrenOnly);
  return installer
             ? static_cast<NumericSpinBoxInstaller *>(installer)->snap_increment()
             : 1.0;
}

} // namespace

QtNumericSpinBoxController::QtNumericSpinBoxController(
    QAbstractSpinBox &spin_box, bool reserves_arrow_space)
    : QObject(&spin_box), spin_box_(spin_box),
      pointer_capture_(std::make_unique<QtHorizontalPointerCapture>()),
      reserves_arrow_space_(reserves_arrow_space),
      authored_minimum_width_(spin_box.minimumWidth()),
      applied_minimum_width_(spin_box.minimumWidth()) {
  setObjectName(QString::fromLatin1(kControllerName));
  line_edit_ = spin_box.findChild<QLineEdit *>();
  Q_ASSERT(line_edit_);
  spin_box_.setButtonSymbols(QAbstractSpinBox::NoButtons);
  spin_box_.setAlignment(Qt::AlignCenter);
  spin_box_.setMouseTracking(true);
  line_edit_->setMouseTracking(true);
  spin_box_.installEventFilter(this);
  line_edit_->installEventFilter(this);

  const auto make_arrow = [&](const QString &name, Qt::ArrowType arrow,
                              int direction) {
    auto *button = new QToolButton(&spin_box_);
    button->setObjectName(name);
    button->setArrowType(arrow);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setCursor(Qt::ArrowCursor);
    button->hide();
    connect(button, &QToolButton::clicked, this,
            [this, direction]() { step_once(direction); });
    return button;
  };
  decrease_button_ = make_arrow(QStringLiteral("numeric_spin_box.decrease"),
                                Qt::LeftArrow, -1);
  increase_button_ = make_arrow(QStringLiteral("numeric_spin_box.increase"),
                                Qt::RightArrow, 1);

  if (auto *integer = qobject_cast<QSpinBox *>(&spin_box_)) {
    connect(integer, &QSpinBox::valueChanged, this,
            [this]() { refresh_presentation(); });
  } else if (auto *floating = qobject_cast<QDoubleSpinBox *>(&spin_box_)) {
    connect(floating, &QDoubleSpinBox::valueChanged, this,
            [this]() { refresh_presentation(); });
  }
  connect(&spin_box_, &QAbstractSpinBox::editingFinished, this,
          [this]() { finish_native_edit(); });
  connect(line_edit_, &QLineEdit::textEdited, this, [this]() {
    if (!editing_ && !dragging_) {
      begin_editing(false);
    }
  });
  refresh_geometry();
  refresh_presentation();
}

QtNumericSpinBoxController::~QtNumericSpinBoxController() {
  dragging_ = false;
  press_pending_ = false;
  end_drag_activity();
  pointer_capture_->stop();
}

double QtNumericSpinBoxController::value() const {
  if (auto *integer = qobject_cast<QSpinBox *>(&spin_box_)) {
    return integer->value();
  }
  return static_cast<QDoubleSpinBox &>(spin_box_).value();
}

void QtNumericSpinBoxController::set_value(double next_value) {
  if (auto *integer = qobject_cast<QSpinBox *>(&spin_box_)) {
    integer->setValue(static_cast<int>(next_value));
  } else {
    static_cast<QDoubleSpinBox &>(spin_box_).setValue(next_value);
  }
}

bool QtNumericSpinBoxController::can_interact() const {
  return spin_box_.isEnabled() && !spin_box_.isReadOnly();
}

bool QtNumericSpinBoxController::can_step(int direction) const {
  if (!can_interact()) {
    return false;
  }
  if (auto *integer = qobject_cast<QSpinBox *>(&spin_box_)) {
    return integer->wrapping()
               ? integer->minimum() != integer->maximum()
               : (direction < 0 ? integer->value() > integer->minimum()
                                : integer->value() < integer->maximum());
  }
  auto &floating = static_cast<QDoubleSpinBox &>(spin_box_);
  return floating.wrapping()
             ? floating.minimum() != floating.maximum()
             : (direction < 0 ? floating.value() > floating.minimum()
                              : floating.value() < floating.maximum());
}

void QtNumericSpinBoxController::restore_text() {
  // Native setValue also discards pending text when the number is unchanged;
  // this preserves owner formatting, special-value text, prefixes and locale.
  const QPointer<QtNumericSpinBoxController> guard(this);
  set_value(value());
  if (guard) {
    line_edit_->setModified(false);
    line_edit_->deselect();
  }
}

void QtNumericSpinBoxController::step_once(int direction) {
  if (!can_step(direction)) {
    return;
  }
  const QPointer<QtNumericSpinBoxController> guard(this);
  const QString previous_text = line_edit_->text();
  const double previous = value();
  // These public slots route through virtual stepBy, including owner-defined
  // nonlinear steps. The controller never duplicates numeric stepping.
  if (direction < 0) {
    spin_box_.stepDown();
  } else {
    spin_box_.stepUp();
  }
  if (guard && (value() != previous || line_edit_->text() != previous_text)) {
    emit_commit();
  }
}

void QtNumericSpinBoxController::begin_editing(bool select_text) {
  if (editing_ || dragging_ || !can_interact()) {
    return;
  }
  original_value_ = value();
  editing_ = true;
  press_pending_ = false;
  const QPointer<QtNumericSpinBoxController> guard(this);
  invoke_callback(editing_started);
  if (!guard || !editing_) {
    return;
  }
  refresh_presentation();
  spin_box_.setFocus(Qt::MouseFocusReason);
  if (guard && editing_ && select_text) {
    spin_box_.selectAll();
  }
}

void QtNumericSpinBoxController::finish_editing() {
  if (!editing_) {
    return;
  }
  editing_ = false;
  line_edit_->deselect();
  refresh_presentation();
  invoke_callback(editing_finished);
}

void QtNumericSpinBoxController::set_drag_mode(QtNumericDragMode mode) {
  drag_mode_ = mode;
  rebase_drag();
}

void QtNumericSpinBoxController::set_drag_sensitivity(
    double units_per_logical_pixel) {
  drag_sensitivity_override_ =
      std::isfinite(units_per_logical_pixel) && units_per_logical_pixel > 0.0
          ? units_per_logical_pixel
          : 0.0;
}

void QtNumericSpinBoxController::set_drag_snap_increment(double increment) {
  drag_snap_increment_override_ =
      std::isfinite(increment) && increment > 0.0 ? increment : 0.0;
}

void QtNumericSpinBoxController::set_drag_snap_scale(double scale) {
  drag_snap_scale_ = std::isfinite(scale) && scale > 0.0 ? scale : 1.0;
}

double QtNumericSpinBoxController::drag_snap_increment() const {
  if (drag_snap_increment_override_ > 0.0) {
    return drag_snap_increment_override_;
  }
  // The provider may invoke its owner synchronously. Copy all controller
  // state needed after that call so it may safely destroy this field.
  const double scale = drag_snap_scale_;
  const double increment = current_snap_increment() * scale;
  return std::isfinite(increment) && increment > 0.0 ? increment : 1.0;
}

bool QtNumericSpinBoxController::normalized_drag() const {
  const auto *floating = qobject_cast<QDoubleSpinBox *>(&spin_box_);
  return floating && drag_sensitivity_override_ == 0.0 &&
         floating->minimum() >= 0.0 && floating->maximum() <= 1.0 &&
         floating->minimum() < floating->maximum();
}

double QtNumericSpinBoxController::drag_sensitivity() const {
  if (drag_sensitivity_override_ > 0.0) {
    return drag_sensitivity_override_;
  }
  if (const auto *integer = qobject_cast<QSpinBox *>(&spin_box_)) {
    const double range =
        static_cast<double>(integer->maximum()) - integer->minimum();
    return range > 256.0 ? 1.0 : (range > 32.0 ? 0.5 : 1.0 / 16.0);
  }
  if (normalized_drag()) {
    const auto &floating = static_cast<QDoubleSpinBox &>(spin_box_);
    const QMargins margins = line_edit_->textMargins();
    const int usable_width = std::max(
        1, line_edit_->contentsRect().width() - margins.left() - margins.right());
    return (floating.maximum() - floating.minimum()) / usable_width;
  }
  return 0.1;
}

void QtNumericSpinBoxController::begin_drag(QPointF pointer,
                                           Qt::KeyboardModifiers modifiers) {
  press_pending_ = false;
  dragging_ = true;
  drag_remainder_ = 0.0;
  drag_modifiers_ = modifiers & (Qt::ShiftModifier | Qt::ControlModifier);
  const QPointer<QtNumericSpinBoxController> guard(this);
  const auto origin = drag_origin;
  const double origin_value = origin ? origin() : value();
  if (!guard || !dragging_) {
    return;
  }
  drag_accumulator_ = origin_value;
  drag_applied_value_ = drag_accumulator_;
  const bool started =
      pointer_capture_->start(*line_edit_, pointer, press_pointer_);
  if (!guard || !dragging_) {
    return;
  }
  if (!started) {
    // Failed capture never starts an owner edit or emits a phantom commit.
    dragging_ = false;
    refresh_presentation();
    return;
  }
  begin_drag_activity();
  if (!guard || !dragging_) {
    return;
  }
  invoke_callback(drag_started);
}

void QtNumericSpinBoxController::begin_drag_activity() {
  if (drag_activity_owner_) {
    return;
  }
  auto *application = qobject_cast<QApplication *>(QCoreApplication::instance());
  if (!application) {
    return;
  }
  const QPointer<QtNumericSpinBoxController> guard(this);
  qt_install_default_numeric_spin_boxes(*application);
  if (!guard || !dragging_) {
    return;
  }
  auto *installer = application->findChild<QObject *>(
      installer_name(), Qt::FindDirectChildrenOnly);
  drag_activity_owner_ = installer;
  static_cast<NumericSpinBoxInstaller *>(installer)->begin_drag();
}

void QtNumericSpinBoxController::end_drag_activity() {
  if (!drag_activity_owner_) {
    return;
  }
  auto *installer = static_cast<NumericSpinBoxInstaller *>(
      drag_activity_owner_.data());
  drag_activity_owner_.clear();
  installer->end_drag();
}

void QtNumericSpinBoxController::update_drag_modifiers(
    Qt::KeyboardModifiers modifiers) {
  const auto next = modifiers & (Qt::ShiftModifier | Qt::ControlModifier);
  if (next == drag_modifiers_) {
    return;
  }
  drag_modifiers_ = next;
  drag_remainder_ = 0.0;
  // Precision changes affect future travel, without releasing a hidden
  // remainder. Ctrl still applies its absolute grid immediately.
  drag_accumulator_ = drag_applied_value_;
  if (drag_mode_ == QtNumericDragMode::Automatic ||
      drag_modifiers_.testFlag(Qt::ControlModifier)) {
    apply_linear_drag();
  }
}

void QtNumericSpinBoxController::update_drag(
    QPointF pointer, Qt::KeyboardModifiers modifiers) {
  if (!dragging_ || !can_interact()) {
    return;
  }
  const QPointer<QtNumericSpinBoxController> guard(this);
  update_drag_modifiers(modifiers);
  if (!guard || !dragging_) {
    return;
  }
  const double delta = pointer_capture_->delta(pointer);
  if (!guard || !dragging_) {
    return;
  }
  if (drag_mode_ == QtNumericDragMode::NativeSteps &&
      !drag_modifiers_.testFlag(Qt::ControlModifier)) {
    update_native_drag(delta);
    return;
  }
  if (delta == 0.0 && !drag_modifiers_.testFlag(Qt::ControlModifier)) {
    return;
  }
  double sensitivity = drag_sensitivity();
  if (qobject_cast<QDoubleSpinBox *>(&spin_box_) &&
      drag_modifiers_.testFlag(Qt::ShiftModifier)) {
    sensitivity *= normalized_drag() ? 0.05 : 0.1;
  }
  drag_accumulator_ += delta * sensitivity;
  apply_linear_drag();
}

void QtNumericSpinBoxController::apply_linear_drag() {
  const QPointer<QtNumericSpinBoxController> guard(this);
  double increment = 1.0;
  if (drag_modifiers_.testFlag(Qt::ControlModifier)) {
    increment = drag_snap_increment();
    if (!guard || !dragging_) {
      return;
    }
    if (drag_modifiers_.testFlag(Qt::ShiftModifier)) {
      increment *= 0.1;
    }
  }
  auto *integer = qobject_cast<QSpinBox *>(&spin_box_);
  auto *floating = qobject_cast<QDoubleSpinBox *>(&spin_box_);
  const double minimum = integer ? integer->minimum() : floating->minimum();
  const double maximum = integer ? integer->maximum() : floating->maximum();
  // Drop travel beyond either bound from the accumulator itself, allowing the
  // next reverse movement to affect the value immediately.
  drag_accumulator_ = std::clamp(drag_accumulator_, minimum, maximum);
  double next = drag_accumulator_;
  if (next > minimum && next < maximum &&
      drag_modifiers_.testFlag(Qt::ControlModifier)) {
    if (integer) {
      increment = std::max(1.0, std::round(increment));
      next = std::trunc(next / increment) * increment;
    } else {
      next = std::round(next / increment) * increment;
    }
  }
  if (integer) {
    next = std::trunc(next);
  } else if (floating->decimals() < 16) {
    const double scale = std::pow(10.0, floating->decimals());
    if (std::isfinite(next * scale)) {
      next = std::round(next * scale) / scale;
    }
  }
  next = std::clamp(next, minimum, maximum);
  if (next == drag_applied_value_) {
    return;
  }
  // Compare with the effective origin, rather than the widget's stale scalar:
  // resolving a mixed field can have meaning even when that scalar matches.
  drag_applied_value_ = next;
  const auto apply = apply_drag_value;
  if (apply) {
    apply(next);
  } else {
    set_value(next);
  }
  if (guard && dragging_) {
    drag_applied_value_ = value();
  }
}

void QtNumericSpinBoxController::update_native_drag(double delta) {
  drag_remainder_ += delta;
  const double pixels_per_step =
      kLogicalPixelsPerStep *
      (drag_modifiers_.testFlag(Qt::ShiftModifier) ? 10.0 : 1.0);
  const QPointer<QtNumericSpinBoxController> guard(this);
  while (std::abs(drag_remainder_) >= pixels_per_step) {
    const int direction = drag_remainder_ < 0.0 ? -1 : 1;
    drag_remainder_ -= static_cast<double>(direction) * pixels_per_step;
    if (!can_step(direction)) {
      drag_remainder_ = 0.0;
      break;
    }
    const double previous = value();
    const QString previous_text = line_edit_->text();
    if (direction < 0) {
      spin_box_.stepDown();
    } else {
      spin_box_.stepUp();
    }
    if (!guard || !dragging_) {
      return;
    }
    drag_applied_value_ = value();
    drag_accumulator_ = drag_applied_value_;
    if ((value() == previous && line_edit_->text() == previous_text) ||
        !can_step(direction)) {
      // Discard unreachable travel at a bound, so reversing immediately works.
      drag_remainder_ = 0.0;
      break;
    }
  }
}

void QtNumericSpinBoxController::finish_drag(bool notify) {
  if (!dragging_) {
    return;
  }
  dragging_ = false;
  press_pending_ = false;
  end_drag_activity();
  drag_remainder_ = 0.0;
  const QPointer<QtNumericSpinBoxController> guard(this);
  pointer_capture_->stop();
  if (!guard) {
    return;
  }
  refresh_presentation();
  if (notify) {
    invoke_callback(drag_finished);
  }
  if (guard && notify) {
    emit_commit();
  }
}

void QtNumericSpinBoxController::finish_native_edit() {
  if (!committing_ && editing_) {
    finish_editing();
  }
}

void QtNumericSpinBoxController::emit_commit() {
  committing_ = true;
  const QPointer<QtNumericSpinBoxController> guard(this);
  emit spin_box_.editingFinished();
  if (guard) {
    committing_ = false;
  }
}

void QtNumericSpinBoxController::abandon_interaction() {
  editing_ = false;
  dragging_ = false;
  press_pending_ = false;
  end_drag_activity();
  drag_remainder_ = 0.0;
  const QPointer<QtNumericSpinBoxController> guard(this);
  pointer_capture_->stop();
  if (!guard) {
    return;
  }
  line_edit_->deselect();
  refresh_presentation();
}

bool QtNumericSpinBoxController::cancel_interaction() {
  if (!editing_ && !dragging_ && !press_pending_) {
    return false;
  }
  const bool was_editing = editing_;
  const bool was_dragging = dragging_;
  const QPointer<QtNumericSpinBoxController> guard(this);
  canceling_ = true;
  set_value(original_value_);
  if (!guard) {
    return true;
  }
  restore_text();
  if (!guard) {
    return true;
  }
  canceling_ = false;
  abandon_interaction();
  if (!guard) {
    return true;
  }
  if (was_editing) {
    invoke_callback(editing_canceled);
  } else if (was_dragging) {
    invoke_callback(drag_canceled);
  }
  if (!guard) {
    return true;
  }
  invoke_callback(interaction_canceled);
  if (!guard) {
    return true;
  }
  if (was_editing) {
    invoke_callback(editing_finished);
  } else if (was_dragging) {
    invoke_callback(drag_finished);
  }
  return true;
}

void QtNumericSpinBoxController::rebase_drag() {
  drag_remainder_ = 0.0;
  if (press_pending_ || dragging_) {
    original_value_ = value();
    drag_accumulator_ = original_value_;
    drag_applied_value_ = original_value_;
  }
}

void QtNumericSpinBoxController::refresh_geometry() {
  if (!decrease_button_ || refreshing_geometry_) {
    return;
  }
  refreshing_geometry_ = true;
  if (!reserves_arrow_space_) {
    const int current_minimum = spin_box_.minimumWidth();
    if (current_minimum != applied_minimum_width_) {
      authored_minimum_width_ = current_minimum;
    }
    // Qt-created fields cannot override sizeHint. Reserve their arrow space
    // through a minimum derived from current native metrics, never from the
    // previously enlarged minimum, so live scaling returns without drift.
    const int required_width =
        spin_box_.sizeHint().width() + 2 * arrow_extent(spin_box_);
    applied_minimum_width_ = std::max(authored_minimum_width_, required_width);
    if (current_minimum != applied_minimum_width_) {
      spin_box_.setMinimumWidth(applied_minimum_width_);
    }
  }
  const QRect contents = line_edit_->geometry();
  const int extent = std::min(arrow_extent(spin_box_), contents.width() / 3);
  decrease_button_->setGeometry(contents.x(), contents.y(), extent,
                                contents.height());
  increase_button_->setGeometry(contents.right() - extent + 1, contents.y(),
                                extent, contents.height());
  line_edit_->setTextMargins(extent, 0, extent, 0);
  decrease_button_->raise();
  increase_button_->raise();
  refreshing_geometry_ = false;
}

void QtNumericSpinBoxController::schedule_geometry_refresh() {
  if (geometry_refresh_queued_) {
    return;
  }
  geometry_refresh_queued_ = true;
  QMetaObject::invokeMethod(this, [this]() {
    geometry_refresh_queued_ = false;
    refresh_geometry();
  }, Qt::QueuedConnection);
}

void QtNumericSpinBoxController::refresh_presentation() {
  const bool interactive = can_interact();
  spin_box_.setCursor(interactive && !editing_ ? Qt::SizeHorCursor
                                              : Qt::ArrowCursor);
  line_edit_->setCursor(interactive && !editing_ ? Qt::SizeHorCursor
                                                : Qt::IBeamCursor);
  const bool arrows_visible = hovered_ && interactive && !editing_;
  decrease_button_->setVisible(arrows_visible);
  increase_button_->setVisible(arrows_visible);
  decrease_button_->setEnabled(can_step(-1));
  increase_button_->setEnabled(can_step(1));
}

void QtNumericSpinBoxController::watch_window() {
  QWidget *current_window = spin_box_.window();
  if (window_ == current_window) {
    return;
  }
  if (window_ && window_ != &spin_box_) {
    window_->removeEventFilter(this);
  }
  window_ = current_window;
  if (window_ && window_ != &spin_box_) {
    window_->installEventFilter(this);
  }
}

bool QtNumericSpinBoxController::eventFilter(QObject *watched, QEvent *event) {
  // QWidget can deliver Hide/WindowDeactivate while its numeric subclass has
  // already been destroyed. Our QObject child is still alive at that point,
  // but the native numeric storage and line editor must no longer be used.
  if (!is_numeric_spin_box(&spin_box_) || forwarding_event_) {
    return false;
  }
  const bool spin_event = watched == &spin_box_;
  const bool editor_event = watched == line_edit_;
  const QPointer<QtNumericSpinBoxController> guard(this);
  if (event->type() == QEvent::WindowDeactivate ||
      (spin_event && (event->type() == QEvent::Hide ||
                     (event->type() == QEvent::EnabledChange &&
                      !spin_box_.isEnabled()) ||
                     (event->type() == QEvent::ReadOnlyChange &&
                      spin_box_.isReadOnly())))) {
    press_pending_ = false;
    if (dragging_) {
      finish_drag();
    }
    if (!guard) {
      return true;
    }
    if (editing_) {
      spin_box_.interpretText();
      if (!guard) {
        return true;
      }
      finish_editing();
      if (!guard) {
        return true;
      }
      emit_commit();
    }
    if (guard) {
      refresh_presentation();
    }
    return !guard;
  }
  if (!spin_event && !editor_event) {
    return false;
  }

  switch (event->type()) {
  case QEvent::Show:
    if (spin_event) {
      watch_window();
      spin_box_.setButtonSymbols(QAbstractSpinBox::NoButtons);
    }
    refresh_geometry();
    refresh_presentation();
    break;
  case QEvent::Resize:
  case QEvent::Move:
    refresh_geometry();
    break;
  case QEvent::StyleChange:
  case QEvent::FontChange:
  case QEvent::ApplicationFontChange:
    refresh_geometry();
    // Native spin boxes invalidate cached size hints in their own handler,
    // after this filter has observed the font/style transition.
    schedule_geometry_refresh();
    break;
  case QEvent::ReadOnlyChange:
  case QEvent::EnabledChange:
    refresh_presentation();
    break;
  case QEvent::Enter:
    hovered_ = true;
    refresh_presentation();
    break;
  case QEvent::Leave:
    if (spin_event) {
      hovered_ = false;
      refresh_presentation();
    }
    break;
  case QEvent::FocusIn: {
    const auto reason = static_cast<QFocusEvent *>(event)->reason();
    if (reason == Qt::TabFocusReason || reason == Qt::BacktabFocusReason ||
        reason == Qt::ShortcutFocusReason) {
      begin_editing();
    }
    break;
  }
  case QEvent::FocusOut:
    if (spin_event && !editing_) {
      if (dragging_) {
        finish_drag();
      }
      if (!guard) {
        return true;
      }
      press_pending_ = false;
      // Native focus cleanup is still required, but a display-mode focus loss
      // must not commit again after Return, scrub release or Escape.
      const bool signals_were_blocked = spin_box_.blockSignals(true);
      forwarding_event_ = true;
      QApplication::sendEvent(watched, event);
      if (guard) {
        forwarding_event_ = false;
        spin_box_.blockSignals(signals_were_blocked);
      }
      return true;
    }
    break;
  case QEvent::UngrabMouse:
    press_pending_ = false;
    if (dragging_) {
      finish_drag();
    }
    return !guard;
  case QEvent::KeyPress: {
    auto *key = static_cast<QKeyEvent *>(event);
    if (key->key() == Qt::Key_Escape && cancel_interaction()) {
      key->accept();
      return true;
    }
    if (dragging_) {
      auto modifiers = key->modifiers();
      if (key->key() == Qt::Key_Shift) {
        modifiers |= Qt::ShiftModifier;
      } else if (key->key() == Qt::Key_Control) {
        modifiers |= Qt::ControlModifier;
      }
      update_drag_modifiers(modifiers);
      key->accept();
      return true;
    }
    if (!editing_ && can_interact()) {
      if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
        // Preserve native interpretation even for programmatically supplied
        // drafts, which may not have QLineEdit's modified flag set.
        begin_editing(false);
        return !guard;
      }
      if (key->key() == Qt::Key_F2) {
        begin_editing();
        key->accept();
        return true;
      }
      if (!key->text().isEmpty() || key->key() == Qt::Key_Backspace ||
          key->key() == Qt::Key_Delete || key->key() == Qt::Key_Up ||
          key->key() == Qt::Key_Down || key->key() == Qt::Key_PageUp ||
          key->key() == Qt::Key_PageDown || key->matches(QKeySequence::Paste)) {
        begin_editing();
        if (!guard) {
          return true;
        }
      }
    }
    break;
  }
  case QEvent::KeyRelease:
    if (dragging_) {
      auto *key = static_cast<QKeyEvent *>(event);
      auto modifiers = key->modifiers();
      if (key->key() == Qt::Key_Shift) {
        modifiers &= ~Qt::ShiftModifier;
      } else if (key->key() == Qt::Key_Control) {
        modifiers &= ~Qt::ControlModifier;
      }
      update_drag_modifiers(modifiers);
      key->accept();
      return true;
    }
    break;
  case QEvent::Wheel:
    if (dragging_) {
      event->accept();
      return true;
    }
    if (!editing_ && !dragging_ && can_interact()) {
      const double previous = value();
      forwarding_event_ = true;
      QApplication::sendEvent(watched, event);
      if (guard) {
        forwarding_event_ = false;
        if (value() != previous) {
          emit_commit();
        }
      }
      return true;
    }
    break;
  case QEvent::InputMethod:
    if (!editing_) {
      begin_editing();
    }
    return !guard;
  case QEvent::MouseButtonDblClick:
    if (!editing_ && can_interact()) {
      begin_editing();
      event->accept();
      return true;
    }
    break;
  case QEvent::MouseButtonPress: {
    auto *mouse = static_cast<QMouseEvent *>(event);
    suppress_context_menu_ = false;
    if (mouse->button() == Qt::RightButton && dragging_) {
      suppress_context_menu_ = true;
      cancel_interaction();
      mouse->accept();
      return true;
    }
    if (mouse->button() == Qt::LeftButton && dragging_) {
      mouse->accept();
      return true;
    }
    if (mouse->button() == Qt::LeftButton && !editing_ && can_interact()) {
      press_pending_ = true;
      press_pointer_ = mouse->globalPosition();
      original_value_ = value();
      spin_box_.setFocus(Qt::MouseFocusReason);
      mouse->accept();
      return true;
    }
    break;
  }
  case QEvent::ContextMenu:
    if (dragging_ || suppress_context_menu_) {
      suppress_context_menu_ = false;
      event->accept();
      return true;
    }
    break;
  case QEvent::MouseMove: {
    auto *mouse = static_cast<QMouseEvent *>(event);
    if (!press_pending_ && !dragging_) {
      break;
    }
    if (!(mouse->buttons() & Qt::LeftButton)) {
      press_pending_ = false;
      finish_drag();
      return !guard;
    }
    const QPointF pointer = mouse->globalPosition();
    if (dragging_) {
      update_drag(pointer, mouse->modifiers());
    } else if (std::abs(pointer.x() - press_pointer_.x()) >=
               std::max(1, QApplication::startDragDistance())) {
      begin_drag(pointer, mouse->modifiers());
    }
    mouse->accept();
    return true;
  }
  case QEvent::MouseButtonRelease: {
    auto *mouse = static_cast<QMouseEvent *>(event);
    if (mouse->button() != Qt::LeftButton || (!press_pending_ && !dragging_)) {
      break;
    }
    if (dragging_) {
      update_drag(mouse->globalPosition(), mouse->modifiers());
      if (guard) {
        finish_drag();
      }
    } else {
      press_pending_ = false;
      begin_editing();
    }
    mouse->accept();
    return true;
  }
  default:
    break;
  }
  return false;
}

QtNumericSpinBoxController &
qt_install_numeric_spin_box(QAbstractSpinBox &spin_box,
                            bool reserves_arrow_space) {
  Q_ASSERT(is_numeric_spin_box(&spin_box));
  if (auto *existing = spin_box.findChild<QObject *>(
          QString::fromLatin1(kControllerName), Qt::FindDirectChildrenOnly)) {
    return *static_cast<QtNumericSpinBoxController *>(existing);
  }
  return *new QtNumericSpinBoxController(spin_box, reserves_arrow_space);
}

void qt_install_default_numeric_spin_boxes(QApplication &application) {
  if (application.findChild<QObject *>(installer_name(),
                                       Qt::FindDirectChildrenOnly)) {
    return;
  }
  new NumericSpinBoxInstaller(application);
  for (QWidget *widget : QApplication::allWidgets()) {
    if (is_numeric_spin_box(widget)) {
      qt_install_numeric_spin_box(*static_cast<QAbstractSpinBox *>(widget));
    }
  }
}

void qt_set_numeric_snap_increment_provider(
    QApplication &application, QObject &context,
    std::function<double()> provider) {
  qt_install_default_numeric_spin_boxes(application);
  auto *installer = application.findChild<QObject *>(
      installer_name(), Qt::FindDirectChildrenOnly);
  static_cast<NumericSpinBoxInstaller *>(installer)->set_snap_provider(
      context, std::move(provider));
}

bool qt_numeric_drag_active(const QApplication &application) {
  auto *installer = application.findChild<QObject *>(
      installer_name(), Qt::FindDirectChildrenOnly);
  return installer &&
         static_cast<NumericSpinBoxInstaller *>(installer)->drag_active();
}

QSize qt_numeric_spin_box_size_hint(const QAbstractSpinBox &spin_box,
                                   QSize native_size) {
  native_size.rwidth() += 2 * arrow_extent(spin_box);
  return native_size;
}

} // namespace quader
