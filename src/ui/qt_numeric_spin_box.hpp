////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <QObject>
#include <QPointer>
#include <QPointF>
#include <QSize>

#include <functional>
#include <memory>

class QAbstractSpinBox;
class QApplication;
class QEvent;
class QLineEdit;
class QToolButton;
class QWidget;

namespace quader {

class QtHorizontalPointerCapture;

/// Most fields scrub numerically; domain-specific fields retain virtual steps.
enum class QtNumericDragMode { Automatic, NativeSteps };

/// One input-time observation of the originating viewport's distance grid.
struct QtNumericGridSnapState {
  double increment = 1.0;
  bool enabled = false;
};

/// Presentation and pointer gestures shared by native integer/double editors.
/// The spin box retains numeric validation, stepping, locale and accessibility.
class QtNumericSpinBoxController final : public QObject {
public:
  ~QtNumericSpinBoxController() override;

  [[nodiscard]] bool is_editing() const { return editing_; }
  [[nodiscard]] bool is_dragging() const { return dragging_; }
  [[nodiscard]] bool is_canceling() const { return canceling_; }

  /// Drop interaction state without calling outgoing owners or changing values.
  void abandon_interaction();
  /// Restore the gesture's starting value and notify cancellation, not commit.
  bool cancel_interaction();
  /// Rebase pointer travel after an owner replaces the value or range.
  void rebase_drag();
  /// Follow a constrained owner value without changing the gesture's cancel value.
  void synchronize_drag_value();

  void set_drag_mode(QtNumericDragMode mode);
  /// Positive values override automatic units per logical pixel; zero resets.
  void set_drag_sensitivity(double units_per_logical_pixel);
  /// Override the field's Ctrl grid; zero uses the application grid provider.
  void set_drag_snap_increment(double increment);
  /// Convert the application grid into this field's numeric units.
  void set_drag_snap_scale(double scale);
  /// Quantize ordinary floating scrubs while the originating grid snap is on.
  void set_grid_aware_drag(bool enabled);

  /// Optional field-local observation; increments are in world units before
  /// set_drag_snap_scale. Missing observers retain the application Ctrl grid.
  std::function<QtNumericGridSnapState()> grid_snap_state;

  /// Mixed-value fields supply their neutral origin and presentation transition.
  std::function<double()> drag_origin;
  std::function<void(double)> apply_drag_value;

  /// Keep rejected domain drafts available to their host's validation handling.
  std::function<bool()> can_commit_text;

  std::function<void()> editing_started;
  std::function<void()> editing_canceled;
  std::function<void()> editing_finished;
  std::function<void()> drag_started;
  std::function<void()> drag_canceled;
  std::function<void()> drag_finished;
  std::function<void()> interaction_canceled;

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  friend class NumericSpinBoxInstaller;
  friend QtNumericSpinBoxController &
  qt_install_numeric_spin_box(QAbstractSpinBox &spin_box,
                              bool reserves_arrow_space);
  explicit QtNumericSpinBoxController(QAbstractSpinBox &spin_box,
                                      bool reserves_arrow_space);

  [[nodiscard]] double value() const;
  [[nodiscard]] bool can_step(int direction) const;
  [[nodiscard]] bool can_interact() const;
  void set_value(double value);
  void restore_text();
  void step_once(int direction);
  void begin_editing(bool select_text = true);
  void finish_editing();
  void commit_text_edit();
  void begin_drag(QPointF pointer, Qt::KeyboardModifiers modifiers);
  void update_drag(QPointF pointer, Qt::KeyboardModifiers modifiers);
  void update_drag_modifiers(Qt::KeyboardModifiers modifiers);
  void apply_linear_drag();
  void update_native_drag(double delta);
  void begin_drag_activity();
  void end_drag_activity();
  [[nodiscard]] double drag_sensitivity() const;
  [[nodiscard]] QtNumericGridSnapState drag_grid_snap_state() const;
  [[nodiscard]] bool normalized_drag() const;
  void finish_drag(bool notify = true);
  void finish_native_edit();
  void emit_commit();
  void refresh_geometry(bool refresh_minimum = false);
  void schedule_geometry_refresh();
  void refresh_presentation();
  void watch_window();

  QAbstractSpinBox &spin_box_;
  QLineEdit *line_edit_ = nullptr;
  QToolButton *decrease_button_ = nullptr;
  QToolButton *increase_button_ = nullptr;
  QPointer<QWidget> window_;
  QPointer<QObject> drag_activity_owner_;
  std::unique_ptr<QtHorizontalPointerCapture> pointer_capture_;
  QtNumericDragMode drag_mode_ = QtNumericDragMode::Automatic;
  Qt::KeyboardModifiers drag_modifiers_ = Qt::NoModifier;
  bool hovered_ = false;
  bool editing_ = false;
  bool dragging_ = false;
  bool press_pending_ = false;
  bool canceling_ = false;
  bool forwarding_event_ = false;
  bool committing_ = false;
  bool suppress_context_menu_ = false;
  bool reserves_arrow_space_ = false;
  bool refreshing_geometry_ = false;
  bool geometry_refresh_queued_ = false;
  bool grid_aware_drag_ = false;
  int authored_minimum_width_ = 0;
  int applied_minimum_width_ = 0;
  double original_value_ = 0.0;
  QPointF press_pointer_;
  double drag_remainder_ = 0.0;
  double drag_accumulator_ = 0.0;
  double drag_applied_value_ = 0.0;
  double drag_sensitivity_override_ = 0.0;
  double drag_snap_increment_override_ = 0.0;
  double drag_snap_scale_ = 1.0;
};

/// Idempotently attach to a QSpinBox or QDoubleSpinBox, including Qt-made fields.
/// Subclasses whose sizeHint already includes arrows pass reserves_arrow_space.
QtNumericSpinBoxController &
qt_install_numeric_spin_box(QAbstractSpinBox &spin_box,
                            bool reserves_arrow_space = false);

/// Install once at application startup; new Qt-made fields attach when polished.
void qt_install_default_numeric_spin_boxes(QApplication &application);

/// Give an active numeric text edit Escape before enclosing lifecycle handlers.
/// Includes shortcut reservation and the matching repeat/release events.
bool qt_handle_numeric_text_edit_key(QObject *target, QEvent *event);

/// Bind the current grid provider to a live owner; an empty provider clears
/// only that owner's binding. Missing or expired providers use a unit grid.
void qt_set_numeric_snap_increment_provider(
    QApplication &application, QObject &context,
    std::function<double()> provider);

/// Constant-size application state for retained presenters during numeric scrubs.
[[nodiscard]] bool qt_numeric_drag_active(const QApplication &application);

/// Reserve stable arrow space in the reusable subclasses' native size hints.
QSize qt_numeric_spin_box_size_hint(const QAbstractSpinBox &spin_box,
                                   QSize native_size);

} // namespace quader
