////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "core/localization_service.hpp"
#include "editor/session/scene_session.hpp"
#include "ui/inspector_panel.hpp"
#include "ui/qt_application_theme.hpp"
#include "ui/qt_double_spin_box.hpp"
#include "ui/qt_numeric_spin_box.hpp"
#include "ui/qt_settings_control_widgets.hpp"
#include "ui/qt_spin_box.hpp"
#include "ui/qt_ui_scale_metrics.hpp"
#include "ui/ui_context.hpp"
#include "ui/ui_settings.hpp"
#include "viewport/viewport_settings.hpp"

#include <QApplication>
#include <QColorDialog>
#include <QDir>
#include <QEnterEvent>
#include <QEventLoop>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLocale>
#include <QMouseEvent>
#include <QPixmap>
#include <QSignalBlocker>
#include <QToolButton>
#include <QTimer>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <limits>
#include <optional>
#include <vector>

namespace {

class QtNumericSpinBoxTests : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(QCoreApplication::instance(), nullptr);
    application_ = std::make_unique<QApplication>(argc_, argv_);
    application_->setQuitOnLastWindowClosed(false);
    QApplication::setStyle(QStringLiteral("Fusion"));
    application_->setPalette(
        quader::qt_obsidian_application_palette(application_->palette()));
  }

  void show(QWidget &widget) {
    widget.setAttribute(Qt::WA_DontShowOnScreen, true);
    widget.resize(220, 32);
    widget.show();
    QCoreApplication::processEvents();
  }

  QApplication &application() { return *application_; }

private:
  int argc_ = 1;
  char executable_[30] = "quader-numeric-spin-box-tests";
  char *argv_[2] = {executable_, nullptr};
  std::unique_ptr<QApplication> application_;
};

void send_mouse(QWidget &target, QEvent::Type type, QPoint position,
                Qt::MouseButton button, Qt::MouseButtons buttons,
                Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  QMouseEvent event(type, QPointF(position),
                   QPointF(target.mapToGlobal(position)), button, buttons,
                   modifiers);
  QCoreApplication::sendEvent(&target, &event);
}

void send_key(QWidget &target, int key, const QString &text = {},
              Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  QKeyEvent event(QEvent::KeyPress, key, modifiers, text);
  QCoreApplication::sendEvent(&target, &event);
}

void enter(QWidget &target) {
  const QPointF position(target.rect().center());
  QEnterEvent event(position, position,
                    QPointF(target.mapToGlobal(position.toPoint())));
  QCoreApplication::sendEvent(&target, &event);
}

void leave(QWidget &target) {
  QEvent event(QEvent::Leave);
  QCoreApplication::sendEvent(&target, &event);
}

void click_center(QLineEdit &editor) {
  const QPoint center = editor.rect().center();
  send_mouse(editor, QEvent::MouseButtonPress, center, Qt::LeftButton,
             Qt::LeftButton);
  send_mouse(editor, QEvent::MouseButtonRelease, center, Qt::LeftButton,
             Qt::NoButton);
}

QPoint begin_drag(QLineEdit &editor,
                  Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  const QPoint center = editor.rect().center();
  send_mouse(editor, QEvent::MouseButtonPress, center, Qt::LeftButton,
             Qt::LeftButton, modifiers);
  const QPoint crossed =
      center + QPoint(QApplication::startDragDistance() + 1, 0);
  send_mouse(editor, QEvent::MouseMove, crossed, Qt::NoButton,
             Qt::LeftButton, modifiers);
  return crossed;
}

void type_text(QWidget &target, const QString &text) {
  for (QChar character : text) {
    send_key(target, character.unicode(), QString(character));
  }
}

void process_editor_events() {
  QEventLoop loop;
  QTimer::singleShot(30, &loop, &QEventLoop::quit);
  loop.exec();
}

void export_review_image(QWidget &widget, const QString &name) {
  const QString directory =
      qEnvironmentVariable("QUADER_NUMERIC_SPIN_BOX_SCREENSHOT_DIR");
  if (directory.isEmpty()) {
    return;
  }
  ASSERT_TRUE(QDir().mkpath(directory));
  ASSERT_TRUE(widget.grab().save(QDir(directory).filePath(name)));
}

// Domain-specific steps (for example texture scale powers of two) must remain
// owned by the native spin box for both buttons and horizontal scrubbing.
class BinaryStepSpinBox final : public quader::QtDoubleSpinBox {
public:
  BinaryStepSpinBox() {
    quader::qt_install_numeric_spin_box(*this).set_drag_mode(
        quader::QtNumericDragMode::NativeSteps);
  }

  int applied_steps = 0;

protected:
  void stepBy(int steps) override {
    applied_steps += steps;
    setValue(value() * std::pow(2.0, steps));
  }
};

TEST_F(QtNumericSpinBoxTests,
       HoverRevealsArrowsAndStationaryClickEntersSelectedText) {
  quader::QtDoubleSpinBox spin;
  spin.setValue(1.25);
  show(spin);
  auto &control = quader::qt_install_numeric_spin_box(spin);
  auto *editor = spin.findChild<QLineEdit *>();
  auto *decrease = spin.findChild<QToolButton *>(
      QStringLiteral("numeric_spin_box.decrease"));
  auto *increase = spin.findChild<QToolButton *>(
      QStringLiteral("numeric_spin_box.increase"));
  ASSERT_NE(editor, nullptr);
  ASSERT_NE(decrease, nullptr);
  ASSERT_NE(increase, nullptr);

  leave(spin);
  EXPECT_FALSE(decrease->isVisible());
  EXPECT_FALSE(increase->isVisible());
  export_review_image(spin, QStringLiteral("numeric-idle.png"));

  enter(spin);
  EXPECT_TRUE(decrease->isVisible());
  EXPECT_TRUE(increase->isVisible());
  EXPECT_EQ(editor->cursor().shape(), Qt::SizeHorCursor);
  EXPECT_EQ(spin.alignment(), Qt::AlignCenter);
  export_review_image(spin, QStringLiteral("numeric-hover.png"));

  click_center(*editor);
  EXPECT_TRUE(control.is_editing());
  EXPECT_EQ(editor->selectedText(), editor->text());
  EXPECT_EQ(editor->cursor().shape(), Qt::IBeamCursor);
  EXPECT_FALSE(decrease->isVisible());
  EXPECT_FALSE(increase->isVisible());
  export_review_image(spin, QStringLiteral("numeric-edit.png"));
}

TEST_F(QtNumericSpinBoxTests,
       ArrowsAndDragPreserveTheNativeVirtualStepPolicy) {
  BinaryStepSpinBox spin;
  spin.setRange(0.0625, 64.0);
  spin.setValue(1.0);
  show(spin);
  auto *editor = spin.findChild<QLineEdit *>();
  auto *decrease = spin.findChild<QToolButton *>(
      QStringLiteral("numeric_spin_box.decrease"));
  auto *increase = spin.findChild<QToolButton *>(
      QStringLiteral("numeric_spin_box.increase"));
  ASSERT_NE(editor, nullptr);
  ASSERT_NE(decrease, nullptr);
  ASSERT_NE(increase, nullptr);
  enter(spin);

  increase->click();
  EXPECT_DOUBLE_EQ(spin.value(), 2.0);
  EXPECT_EQ(spin.applied_steps, 1);
  decrease->click();
  EXPECT_DOUBLE_EQ(spin.value(), 1.0);
  EXPECT_EQ(spin.applied_steps, 0);

  const QPoint origin = begin_drag(*editor);
  send_mouse(*editor, QEvent::MouseMove, origin + QPoint(16, 0),
             Qt::NoButton, Qt::LeftButton);
  EXPECT_DOUBLE_EQ(spin.value(), 4.0);
  EXPECT_EQ(spin.applied_steps, 2);
  send_mouse(*editor, QEvent::MouseButtonRelease, origin + QPoint(16, 0),
             Qt::LeftButton, Qt::NoButton);

  QObject grid_context;
  quader::qt_set_numeric_snap_increment_provider(
      application(), grid_context, []() { return 0.5; });
  const QPoint grid_origin = begin_drag(*editor, Qt::ControlModifier);
  send_mouse(*editor, QEvent::MouseMove, grid_origin + QPoint(7, 0),
             Qt::NoButton, Qt::LeftButton, Qt::ControlModifier);
  EXPECT_DOUBLE_EQ(spin.value(), 4.5);
  EXPECT_EQ(spin.applied_steps, 2)
      << "Ctrl scrubs the shared grid without invoking binary arrow steps";
  send_mouse(*editor, QEvent::MouseButtonRelease, grid_origin + QPoint(7, 0),
             Qt::LeftButton, Qt::NoButton, Qt::ControlModifier);
}

TEST_F(QtNumericSpinBoxTests,
       MixedNumberRangeBoundsDoNotCommitUnchangedArrowClicks) {
  quader::qt_settings_detail::QtMixedNumberSpinBox spin(1.0, nullptr, true);
  constexpr double kUpperScale = 2.0;
  constexpr double kLowerScale = 1.0;
  spin.setRange(kLowerScale, kUpperScale);
  spin.setSingleStep(0.1);
  spin.setValue(kUpperScale);
  show(spin);
  auto *decrease = spin.findChild<QToolButton *>(
      QStringLiteral("numeric_spin_box.decrease"));
  auto *increase = spin.findChild<QToolButton *>(
      QStringLiteral("numeric_spin_box.increase"));
  ASSERT_NE(decrease, nullptr);
  ASSERT_NE(increase, nullptr);
  enter(spin);
  int changes = 0;
  int finishes = 0;
  QObject::connect(&spin, &QDoubleSpinBox::valueChanged, &spin,
                   [&]() { ++changes; });
  QObject::connect(&spin, &QDoubleSpinBox::editingFinished, &spin,
                   [&]() { ++finishes; });

  increase->click();
  EXPECT_DOUBLE_EQ(spin.value(), kUpperScale);
  EXPECT_EQ(changes, 0);
  EXPECT_EQ(finishes, 0);
  decrease->click();
  EXPECT_DOUBLE_EQ(spin.value(), kUpperScale - 0.1);
  EXPECT_EQ(changes, 1);
  EXPECT_EQ(finishes, 1);

  spin.setValue(kLowerScale);
  changes = 0;
  finishes = 0;
  decrease->click();
  EXPECT_DOUBLE_EQ(spin.value(), kLowerScale);
  EXPECT_EQ(changes, 0);
  EXPECT_EQ(finishes, 0);
  increase->click();
  EXPECT_DOUBLE_EQ(spin.value(), kLowerScale + 0.1);
  EXPECT_EQ(changes, 1);
  EXPECT_EQ(finishes, 1);
}

TEST_F(QtNumericSpinBoxTests,
       ResolvingAMixedFieldFinishesEvenWhenItsStaleScalarMatches) {
  quader::qt_settings_detail::QtMixedNumberSpinBox spin(1.0, nullptr, true);
  spin.setSingleStep(0.1);
  spin.present(1.1);
  spin.present(std::nullopt);
  show(spin);
  auto *increase = spin.findChild<QToolButton *>(
      QStringLiteral("numeric_spin_box.increase"));
  ASSERT_NE(increase, nullptr);
  enter(spin);
  int finishes = 0;
  QObject::connect(&spin, &QDoubleSpinBox::editingFinished, &spin,
                   [&]() { ++finishes; });

  increase->click();
  EXPECT_DOUBLE_EQ(spin.value(), 1.1);
  EXPECT_NE(spin.text(), QStringLiteral("--"));
  EXPECT_EQ(finishes, 1);
}

TEST_F(QtNumericSpinBoxTests,
       LiveInterfaceScalingResizesArrowTargetsAndReturnsWithoutDrift) {
  quader::QtUiScaleApplicationController scale_controller(application());
  quader::QtSpinBox spin;
  spin.setRange(-99'999, 99'999);
  spin.setValue(12'345);
  show(spin);
  auto *decrease = spin.findChild<QToolButton *>(
      QStringLiteral("numeric_spin_box.decrease"));
  auto *increase = spin.findChild<QToolButton *>(
      QStringLiteral("numeric_spin_box.increase"));
  ASSERT_NE(decrease, nullptr);
  ASSERT_NE(increase, nullptr);
  enter(spin);
  const int initial_width = increase->width();
  const QSize initial_hint = spin.sizeHint();
  ASSERT_GT(initial_width, 0);
  EXPECT_EQ(decrease->width(), initial_width);

  ASSERT_TRUE(scale_controller.apply_factor(2.0F));
  spin.resize(440, 64);
  QCoreApplication::processEvents();
  EXPECT_GT(increase->width(), initial_width);
  EXPECT_EQ(decrease->width(), increase->width());
  EXPECT_GT(spin.sizeHint().height(), initial_hint.height());
  export_review_image(spin, QStringLiteral("numeric-scale-200.png"));

  ASSERT_TRUE(scale_controller.apply_factor(1.0F));
  spin.resize(220, 32);
  QCoreApplication::processEvents();
  EXPECT_EQ(increase->width(), initial_width);
  EXPECT_EQ(decrease->width(), initial_width);
  EXPECT_EQ(spin.sizeHint(), initial_hint);
  export_review_image(spin, QStringLiteral("numeric-scale-100.png"));
}

TEST_F(QtNumericSpinBoxTests,
       IntegerDragDiscardsThresholdAndFinishesExactlyOnceOnRelease) {
  quader::QtSpinBox spin;
  spin.setRange(-100, 100);
  spin.setSingleStep(2);
  spin.setValue(10);
  show(spin);
  auto &control = quader::qt_install_numeric_spin_box(spin);
  auto *editor = spin.findChild<QLineEdit *>();
  ASSERT_NE(editor, nullptr);
  int starts = 0;
  int finishes = 0;
  int native_finishes = 0;
  control.drag_started = [&]() { ++starts; };
  control.drag_finished = [&]() { ++finishes; };
  QObject::connect(&spin, &QSpinBox::editingFinished, &spin,
                   [&]() { ++native_finishes; });

  const QPoint center = editor->rect().center();
  send_mouse(*editor, QEvent::MouseButtonPress, center, Qt::LeftButton,
             Qt::LeftButton);
  send_mouse(*editor, QEvent::MouseMove,
             center + QPoint(std::max(0, QApplication::startDragDistance() - 1),
                             0),
             Qt::NoButton, Qt::LeftButton);
  EXPECT_FALSE(control.is_dragging());
  EXPECT_EQ(spin.value(), 10);
  const QPoint origin =
      center + QPoint(QApplication::startDragDistance() + 1, 0);
  send_mouse(*editor, QEvent::MouseMove, origin, Qt::NoButton,
             Qt::LeftButton);
  ASSERT_TRUE(control.is_dragging());
  EXPECT_TRUE(quader::qt_numeric_drag_active(application()));
  EXPECT_EQ(spin.value(), 10);
  send_mouse(*editor, QEvent::MouseMove, origin + QPoint(2, 0),
             Qt::NoButton, Qt::LeftButton);
  EXPECT_EQ(spin.value(), 11);
  send_mouse(*editor, QEvent::MouseMove, origin + QPoint(24, 0),
             Qt::NoButton, Qt::LeftButton);
  EXPECT_EQ(spin.value(), 22);
  send_mouse(*editor, QEvent::MouseButtonRelease, origin + QPoint(24, 0),
             Qt::LeftButton, Qt::NoButton);
  EXPECT_FALSE(control.is_dragging());
  EXPECT_FALSE(control.is_editing());
  EXPECT_EQ(starts, 1);
  EXPECT_EQ(finishes, 1);
  EXPECT_EQ(native_finishes, 1);
  EXPECT_FALSE(quader::qt_numeric_drag_active(application()));
}

TEST_F(QtNumericSpinBoxTests, FailedCaptureDoesNotStartOrCommitAnOwnerEdit) {
  quader::QtDoubleSpinBox spin;
  spin.setValue(2.0);
  // A hidden production widget cannot acquire capture. Unlike the synthetic
  // fixture widgets, this one deliberately does not opt into offscreen input.
  auto &control = quader::qt_install_numeric_spin_box(spin);
  auto *editor = spin.findChild<QLineEdit *>();
  ASSERT_NE(editor, nullptr);
  int starts = 0;
  int finishes = 0;
  int commits = 0;
  control.drag_started = [&]() { ++starts; };
  control.drag_finished = [&]() { ++finishes; };
  QObject::connect(&spin, &QDoubleSpinBox::editingFinished, &spin,
                   [&]() { ++commits; });

  const QPoint origin = begin_drag(*editor);
  send_mouse(*editor, QEvent::MouseMove, origin + QPoint(40, 0),
             Qt::NoButton, Qt::LeftButton);
  send_mouse(*editor, QEvent::MouseButtonRelease, origin + QPoint(40, 0),
             Qt::LeftButton, Qt::NoButton);
  EXPECT_FALSE(control.is_dragging());
  EXPECT_FALSE(control.is_editing());
  EXPECT_EQ(starts, 0);
  EXPECT_EQ(finishes, 0);
  EXPECT_EQ(commits, 0);
  EXPECT_DOUBLE_EQ(spin.value(), 2.0);
  EXPECT_FALSE(quader::qt_numeric_drag_active(application()));
}

TEST_F(QtNumericSpinBoxTests,
       AbandoningOrDestroyingAScrubClearsApplicationDragActivity) {
  auto spin = std::make_unique<quader::QtSpinBox>();
  show(*spin);
  auto &control = quader::qt_install_numeric_spin_box(*spin);
  auto *editor = spin->findChild<QLineEdit *>();
  ASSERT_NE(editor, nullptr);
  int finishes = 0;
  control.drag_finished = [&]() { ++finishes; };
  begin_drag(*editor);
  ASSERT_TRUE(quader::qt_numeric_drag_active(application()));
  control.abandon_interaction();
  EXPECT_FALSE(quader::qt_numeric_drag_active(application()));
  EXPECT_EQ(finishes, 0);

  begin_drag(*editor);
  ASSERT_TRUE(quader::qt_numeric_drag_active(application()));
  spin.reset();
  EXPECT_FALSE(quader::qt_numeric_drag_active(application()));
  EXPECT_EQ(finishes, 0);
}

TEST_F(QtNumericSpinBoxTests,
       FloatingDragReversesImmediatelyAfterClamping) {
  quader::QtDoubleSpinBox spin;
  spin.setRange(-1.0, 1.0);
  spin.setSingleStep(0.125);
  spin.setValue(0.0);
  show(spin);
  auto *editor = spin.findChild<QLineEdit *>();
  ASSERT_NE(editor, nullptr);

  const QPoint origin = begin_drag(*editor);
  send_mouse(*editor, QEvent::MouseMove, origin + QPoint(2, 0),
             Qt::NoButton, Qt::LeftButton);
  EXPECT_DOUBLE_EQ(spin.value(), 0.2);
  const QPoint bound = origin + QPoint(800, 0);
  send_mouse(*editor, QEvent::MouseMove, bound, Qt::NoButton,
             Qt::LeftButton);
  EXPECT_DOUBLE_EQ(spin.value(), 1.0);
  send_mouse(*editor, QEvent::MouseMove, bound - QPoint(1, 0),
             Qt::NoButton, Qt::LeftButton);
  EXPECT_DOUBLE_EQ(spin.value(), 0.9)
      << "returning from a bound must not traverse accumulated excess travel";
  send_mouse(*editor, QEvent::MouseButtonRelease, bound - QPoint(1, 0),
             Qt::LeftButton, Qt::NoButton);
}

TEST_F(QtNumericSpinBoxTests,
       FloatingDragHasUsableSensitivityIndependentOfKeyboardSteps) {
  for (double keyboard_step : {0.0001, 0.01, 1.0}) {
    SCOPED_TRACE(keyboard_step);
    quader::QtDoubleSpinBox spin;
    spin.setRange(-10'000.0, 10'000.0);
    spin.setSingleStep(keyboard_step);
    spin.setValue(0.0);
    show(spin);
    auto *editor = spin.findChild<QLineEdit *>();
    auto *increase = spin.findChild<QToolButton *>(
        QStringLiteral("numeric_spin_box.increase"));
    ASSERT_NE(editor, nullptr);
    ASSERT_NE(increase, nullptr);
    increase->click();
    EXPECT_DOUBLE_EQ(spin.value(), keyboard_step);
    spin.setValue(0.0);

    const QPoint origin = begin_drag(*editor);
    send_mouse(*editor, QEvent::MouseMove, origin + QPoint(10, 0),
               Qt::NoButton, Qt::LeftButton);
    EXPECT_DOUBLE_EQ(spin.value(), 1.0);
    send_mouse(*editor, QEvent::MouseButtonRelease, origin + QPoint(10, 0),
               Qt::LeftButton, Qt::NoButton);

    const QPoint precise_origin = begin_drag(*editor, Qt::ShiftModifier);
    send_mouse(*editor, QEvent::MouseMove, precise_origin + QPoint(10, 0),
               Qt::NoButton, Qt::LeftButton, Qt::ShiftModifier);
    EXPECT_DOUBLE_EQ(spin.value(), 1.1);
    send_mouse(*editor, QEvent::MouseButtonRelease,
               precise_origin + QPoint(10, 0), Qt::LeftButton, Qt::NoButton,
               Qt::ShiftModifier);
  }
}

TEST_F(QtNumericSpinBoxTests,
       ModifierChangesRebaseTravelWithoutReintroducingRoundedValues) {
  quader::QtDoubleSpinBox spin;
  spin.setRange(-100.0, 100.0);
  spin.setSingleStep(0.0001);
  show(spin);
  auto *editor = spin.findChild<QLineEdit *>();
  ASSERT_NE(editor, nullptr);
  const QPoint origin = begin_drag(*editor);
  send_mouse(*editor, QEvent::MouseMove, origin + QPoint(10, 0),
             Qt::NoButton, Qt::LeftButton);
  EXPECT_DOUBLE_EQ(spin.value(), 1.0);

  send_key(*editor, Qt::Key_Shift, {}, Qt::ShiftModifier);
  EXPECT_DOUBLE_EQ(spin.value(), 1.0);
  send_mouse(*editor, QEvent::MouseMove, origin + QPoint(110, 0),
             Qt::NoButton, Qt::LeftButton, Qt::ShiftModifier);
  EXPECT_DOUBLE_EQ(spin.value(), 2.0);
  send_mouse(*editor, QEvent::MouseMove, origin + QPoint(110, 0),
             Qt::NoButton, Qt::LeftButton, Qt::ControlModifier);
  EXPECT_DOUBLE_EQ(spin.value(), 2.0);
  send_mouse(*editor, QEvent::MouseMove, origin + QPoint(113, 0),
             Qt::NoButton, Qt::LeftButton, Qt::ControlModifier);
  EXPECT_DOUBLE_EQ(spin.value(), 2.0);
  send_mouse(*editor, QEvent::MouseMove, origin + QPoint(116, 0),
             Qt::NoButton, Qt::LeftButton, Qt::ControlModifier);
  EXPECT_DOUBLE_EQ(spin.value(), 3.0);

  constexpr auto kPreciseSnap = Qt::ControlModifier | Qt::ShiftModifier;
  send_mouse(*editor, QEvent::MouseMove, origin + QPoint(116, 0),
             Qt::NoButton, Qt::LeftButton, kPreciseSnap);
  EXPECT_DOUBLE_EQ(spin.value(), 3.0);
  send_mouse(*editor, QEvent::MouseMove, origin + QPoint(124, 0),
             Qt::NoButton, Qt::LeftButton, kPreciseSnap);
  EXPECT_DOUBLE_EQ(spin.value(), 3.1);
  send_mouse(*editor, QEvent::MouseMove, origin + QPoint(124, 0),
             Qt::NoButton, Qt::LeftButton);
  EXPECT_DOUBLE_EQ(spin.value(), 3.1);
  send_mouse(*editor, QEvent::MouseMove, origin + QPoint(134, 0),
             Qt::NoButton, Qt::LeftButton);
  EXPECT_DOUBLE_EQ(spin.value(), 4.1);
  send_mouse(*editor, QEvent::MouseButtonRelease, origin + QPoint(134, 0),
             Qt::LeftButton, Qt::NoButton);
  EXPECT_DOUBLE_EQ(spin.value(), 4.1);
}

TEST_F(QtNumericSpinBoxTests,
       FloatingControlSnapUsesTheSharedGridAndFieldUnitScale) {
  struct SnapCase {
    double minimum;
    double maximum;
    double start;
    int pixels;
    Qt::KeyboardModifiers modifiers;
    double grid;
    double scale;
    double expected;
  };
  constexpr auto kPreciseSnap = Qt::ControlModifier | Qt::ShiftModifier;
  const std::array cases{
      SnapCase{-1.0, 1.0, 0.023, 8, Qt::ControlModifier, 0.25, 1.0, 0.75},
      SnapCase{-1.0, 1.0, 0.023, 8, kPreciseSnap, 0.25, 1.0, 0.10},
      SnapCase{-100.0, 100.0, 0.25, 80, Qt::ControlModifier, 8.0, 1.0, 8.0},
      SnapCase{-100.0, 100.0, -0.25, -80, Qt::ControlModifier, 8.0, 1.0, -8.0},
      SnapCase{-100.0, 100.0, 0.25, 80, kPreciseSnap, 8.0, 1.0, 0.8},
      SnapCase{-100.0, 100.0, -0.25, -80, kPreciseSnap, 8.0, 1.0, -0.8},
      SnapCase{-100.0, 100.0, 0.25, 8, Qt::ControlModifier, 32.0,
               1.0 / 32.0, 1.0},
      SnapCase{-100.0, 100.0, 0.25, 8, kPreciseSnap, 32.0,
               1.0 / 32.0, 0.3},
  };
  double grid = 1.0;
  QObject provider_context;
  quader::qt_set_numeric_snap_increment_provider(
      application(), provider_context, [&]() { return grid; });
  for (const SnapCase &sample : cases) {
    SCOPED_TRACE(sample.grid);
    SCOPED_TRACE(sample.scale);
    SCOPED_TRACE(static_cast<int>(sample.modifiers));
    grid = sample.grid;
    quader::QtDoubleSpinBox spin;
    spin.setRange(sample.minimum, sample.maximum);
    spin.setValue(sample.start);
    show(spin);
    auto &control = quader::qt_install_numeric_spin_box(spin);
    control.set_drag_snap_scale(sample.scale);
    auto *editor = spin.findChild<QLineEdit *>();
    ASSERT_NE(editor, nullptr);
    const QPoint origin = begin_drag(*editor, sample.modifiers);
    const QPoint moved = origin + QPoint(sample.pixels, 0);
    send_mouse(*editor, QEvent::MouseMove, moved, Qt::NoButton,
               Qt::LeftButton, sample.modifiers);
    EXPECT_DOUBLE_EQ(spin.value(), sample.expected);
    send_mouse(*editor, QEvent::MouseButtonRelease, moved, Qt::LeftButton,
               Qt::NoButton, sample.modifiers);
    EXPECT_DOUBLE_EQ(spin.value(), sample.expected);
  }
}

TEST_F(QtNumericSpinBoxTests,
       IntegerDragUsesRangeSensitivityAndRepresentableSharedGridSteps) {
  struct IntegerCase {
    int minimum;
    int maximum;
    int pixels;
    Qt::KeyboardModifiers modifiers;
    double grid;
    int expected;
  };
  constexpr auto kPreciseSnap = Qt::ControlModifier | Qt::ShiftModifier;
  const std::array cases{
      IntegerCase{-512, 512, 32, Qt::NoModifier, 8.0, 32},
      IntegerCase{-100, 100, 32, Qt::NoModifier, 8.0, 16},
      IntegerCase{-16, 16, 16, Qt::NoModifier, 8.0, 1},
      IntegerCase{-512, 512, 32, Qt::ShiftModifier, 8.0, 32},
      IntegerCase{-512, 512, 19, Qt::ControlModifier, 8.0, 16},
      IntegerCase{-512, 512, -19, Qt::ControlModifier, 8.0, -16},
      IntegerCase{-512, 512, 19, kPreciseSnap, 8.0, 19},
      IntegerCase{-512, 512, -19, kPreciseSnap, 8.0, -19},
      IntegerCase{-512, 512, 19, kPreciseSnap, 32.0, 18},
      IntegerCase{-512, 512, -19, kPreciseSnap, 32.0, -18},
      IntegerCase{-512, 512, 19, Qt::ControlModifier, 0.25, 19},
  };
  double grid = 1.0;
  QObject provider_context;
  quader::qt_set_numeric_snap_increment_provider(
      application(), provider_context, [&]() { return grid; });
  for (const IntegerCase &sample : cases) {
    SCOPED_TRACE(sample.maximum);
    SCOPED_TRACE(sample.pixels);
    SCOPED_TRACE(static_cast<int>(sample.modifiers));
    grid = sample.grid;
    quader::QtSpinBox spin;
    spin.setRange(sample.minimum, sample.maximum);
    spin.setSingleStep(2);
    show(spin);
    auto *editor = spin.findChild<QLineEdit *>();
    ASSERT_NE(editor, nullptr);
    const QPoint origin = begin_drag(*editor, sample.modifiers);
    const QPoint moved = origin + QPoint(sample.pixels, 0);
    send_mouse(*editor, QEvent::MouseMove, moved, Qt::NoButton,
               Qt::LeftButton, sample.modifiers);
    EXPECT_EQ(spin.value(), sample.expected);
    send_mouse(*editor, QEvent::MouseButtonRelease, moved, Qt::LeftButton,
               Qt::NoButton, sample.modifiers);
    EXPECT_EQ(spin.value(), sample.expected);
  }
}

TEST_F(QtNumericSpinBoxTests,
       SharedGridUpdatesLiveAndProviderLifetimeCannotClearAReplacement) {
  quader::QtDoubleSpinBox spin;
  spin.setRange(-100.0, 100.0);
  show(spin);
  auto *editor = spin.findChild<QLineEdit *>();
  ASSERT_NE(editor, nullptr);
  double first_grid = 4.0;
  int first_calls = 0;
  auto first_context = std::make_unique<QObject>();
  quader::qt_set_numeric_snap_increment_provider(
      application(), *first_context, [&]() {
        ++first_calls;
        return first_grid;
      });

  const QPoint origin = begin_drag(*editor, Qt::ControlModifier);
  const QPoint moved = origin + QPoint(30, 0);
  send_mouse(*editor, QEvent::MouseMove, moved, Qt::NoButton,
             Qt::LeftButton, Qt::ControlModifier);
  EXPECT_DOUBLE_EQ(spin.value(), 4.0);
  ASSERT_GT(first_calls, 0);
  const int calls_before_live_change = first_calls;
  first_grid = 1.0;
  send_mouse(*editor, QEvent::MouseMove, moved, Qt::NoButton,
             Qt::LeftButton, Qt::ControlModifier);
  EXPECT_DOUBLE_EQ(spin.value(), 3.0);
  EXPECT_GT(first_calls, calls_before_live_change);

  int second_calls = 0;
  auto second_context = std::make_unique<QObject>();
  quader::qt_set_numeric_snap_increment_provider(
      application(), *second_context, [&]() {
        ++second_calls;
        return 2.0;
      });
  const int old_provider_calls = first_calls;
  first_context.reset();
  QObject unrelated_context;
  quader::qt_set_numeric_snap_increment_provider(application(),
                                                unrelated_context, {});
  send_mouse(*editor, QEvent::MouseMove, moved, Qt::NoButton,
             Qt::LeftButton, Qt::ControlModifier);
  EXPECT_DOUBLE_EQ(spin.value(), 4.0);
  EXPECT_EQ(first_calls, old_provider_calls);
  ASSERT_GT(second_calls, 0);

  const int calls_before_expiration = second_calls;
  second_context.reset();
  send_mouse(*editor, QEvent::MouseMove, moved, Qt::NoButton,
             Qt::LeftButton, Qt::ControlModifier);
  EXPECT_DOUBLE_EQ(spin.value(), 3.0);
  EXPECT_EQ(second_calls, calls_before_expiration);

  QObject rebound_context;
  int rebinding_calls = 0;
  quader::qt_set_numeric_snap_increment_provider(
      application(), unrelated_context, [&]() {
        ++rebinding_calls;
        quader::qt_set_numeric_snap_increment_provider(
            application(), rebound_context, []() { return 2.0; });
        return 8.0;
      });
  send_mouse(*editor, QEvent::MouseMove, moved, Qt::NoButton,
             Qt::LeftButton, Qt::ControlModifier);
  EXPECT_DOUBLE_EQ(spin.value(), 3.0)
      << "a replaced callback's result must not reach the numeric owner";
  EXPECT_EQ(rebinding_calls, 1);
  send_mouse(*editor, QEvent::MouseMove, moved, Qt::NoButton,
             Qt::LeftButton, Qt::ControlModifier);
  EXPECT_DOUBLE_EQ(spin.value(), 4.0);
  EXPECT_EQ(rebinding_calls, 1);

  auto self_expiring_context = std::make_unique<QObject>();
  int expiring_calls = 0;
  quader::qt_set_numeric_snap_increment_provider(
      application(), *self_expiring_context, [&]() {
        ++expiring_calls;
        self_expiring_context.reset();
        return 8.0;
      });
  send_mouse(*editor, QEvent::MouseMove, moved, Qt::NoButton,
             Qt::LeftButton, Qt::ControlModifier);
  EXPECT_DOUBLE_EQ(spin.value(), 3.0);
  EXPECT_EQ(expiring_calls, 1);
  send_mouse(*editor, QEvent::MouseMove, moved, Qt::NoButton,
             Qt::LeftButton, Qt::ControlModifier);
  EXPECT_DOUBLE_EQ(spin.value(), 3.0);
  EXPECT_EQ(expiring_calls, 1);

  for (double invalid_grid : {0.0, -4.0,
                              std::numeric_limits<double>::quiet_NaN(),
                              std::numeric_limits<double>::infinity()}) {
    quader::qt_set_numeric_snap_increment_provider(
        application(), unrelated_context, [invalid_grid]() {
          return invalid_grid;
        });
    send_mouse(*editor, QEvent::MouseMove, moved, Qt::NoButton,
               Qt::LeftButton, Qt::ControlModifier);
    EXPECT_DOUBLE_EQ(spin.value(), 3.0);
  }
  send_mouse(*editor, QEvent::MouseButtonRelease, moved, Qt::LeftButton,
             Qt::NoButton, Qt::ControlModifier);
}

TEST_F(QtNumericSpinBoxTests,
       ExplicitSensitivityOverridesNormalizedRangeAndPreservesPrecision) {
  quader::QtDoubleSpinBox spin;
  spin.setRange(0.0, 1.0);
  spin.setSingleStep(0.0001);
  show(spin);
  auto &control = quader::qt_install_numeric_spin_box(spin);
  control.set_drag_sensitivity(0.01);
  auto *editor = spin.findChild<QLineEdit *>();
  ASSERT_NE(editor, nullptr);
  const QPoint origin = begin_drag(*editor);
  send_mouse(*editor, QEvent::MouseMove, origin + QPoint(8, 0),
             Qt::NoButton, Qt::LeftButton);
  EXPECT_DOUBLE_EQ(spin.value(), 0.08);
  send_mouse(*editor, QEvent::MouseButtonRelease, origin + QPoint(8, 0),
             Qt::LeftButton, Qt::NoButton);

  const QPoint precise_origin = begin_drag(*editor, Qt::ShiftModifier);
  send_mouse(*editor, QEvent::MouseMove, precise_origin + QPoint(8, 0),
             Qt::NoButton, Qt::LeftButton, Qt::ShiftModifier);
  EXPECT_DOUBLE_EQ(spin.value(), 0.088);
  send_key(*editor, Qt::Key_Escape);
  EXPECT_DOUBLE_EQ(spin.value(), 0.08);
  EXPECT_FALSE(control.is_dragging());
}

TEST_F(QtNumericSpinBoxTests,
       NormalizedDragTraversesItsRangeAcrossTheUsableTextWidth) {
  quader::QtDoubleSpinBox spin;
  spin.setDecimals(6);
  spin.setRange(0.2, 0.8);
  spin.setSingleStep(0.0001);
  spin.setValue(0.2);
  show(spin);
  auto *editor = spin.findChild<QLineEdit *>();
  ASSERT_NE(editor, nullptr);
  const QMargins margins = editor->textMargins();
  const int usable_width = editor->contentsRect().width() - margins.left() -
                           margins.right();
  ASSERT_GT(usable_width, 0);
  const QPoint origin = begin_drag(*editor);
  send_mouse(*editor, QEvent::MouseMove, origin + QPoint(usable_width, 0),
             Qt::NoButton, Qt::LeftButton);
  EXPECT_DOUBLE_EQ(spin.value(), 0.8);
  send_mouse(*editor, QEvent::MouseButtonRelease,
             origin + QPoint(usable_width, 0), Qt::LeftButton, Qt::NoButton);

  spin.setValue(0.2);
  const QPoint precise_origin = begin_drag(*editor, Qt::ShiftModifier);
  send_mouse(*editor, QEvent::MouseMove,
             precise_origin + QPoint(usable_width, 0), Qt::NoButton,
             Qt::LeftButton, Qt::ShiftModifier);
  EXPECT_DOUBLE_EQ(spin.value(), 0.23);
  send_mouse(*editor, QEvent::MouseButtonRelease,
             precise_origin + QPoint(usable_width, 0), Qt::LeftButton,
             Qt::NoButton, Qt::ShiftModifier);
}

TEST_F(QtNumericSpinBoxTests,
       StationaryControlPressSnapsImmediatelyAndEscapeRestoresTheOrigin) {
  quader::QtDoubleSpinBox spin;
  spin.setRange(-100.0, 100.0);
  spin.setValue(1.26);
  show(spin);
  auto &control = quader::qt_install_numeric_spin_box(spin);
  auto *editor = spin.findChild<QLineEdit *>();
  ASSERT_NE(editor, nullptr);
  int finishes = 0;
  QObject::connect(&spin, &QDoubleSpinBox::editingFinished, &spin,
                   [&]() { ++finishes; });
  const QPoint origin = begin_drag(*editor);
  ASSERT_TRUE(control.is_dragging());
  EXPECT_DOUBLE_EQ(spin.value(), 1.26);
  send_key(*editor, Qt::Key_Control, {}, Qt::ControlModifier);
  EXPECT_DOUBLE_EQ(spin.value(), 1.0);
  send_key(*editor, Qt::Key_Escape);
  EXPECT_DOUBLE_EQ(spin.value(), 1.26);
  EXPECT_FALSE(control.is_dragging());
  send_mouse(*editor, QEvent::MouseButtonRelease, origin,
             Qt::LeftButton, Qt::NoButton);
  EXPECT_DOUBLE_EQ(spin.value(), 1.26);
  EXPECT_EQ(finishes, 0);
}

TEST_F(QtNumericSpinBoxTests,
       MixedDragResolvesFromItsOriginEvenWhenTheScalarAlreadyMatches) {
  quader::qt_settings_detail::QtMixedNumberSpinBox spin(0.0);
  spin.setValue(1.0);
  spin.present(std::nullopt);
  show(spin);
  auto *editor = spin.findChild<QLineEdit *>();
  ASSERT_NE(editor, nullptr);
  ASSERT_EQ(editor->text(), QStringLiteral("--"));
  int edits = 0;
  std::optional<double> published;
  spin.edited = [&](std::optional<double> value) {
    ++edits;
    published = value;
  };

  const QPoint origin = begin_drag(*editor);
  send_mouse(*editor, QEvent::MouseMove, origin + QPoint(10, 0),
             Qt::NoButton, Qt::LeftButton);
  ASSERT_TRUE(published.has_value());
  EXPECT_DOUBLE_EQ(*published, 1.0);
  EXPECT_DOUBLE_EQ(spin.value(), 1.0);
  EXPECT_EQ(edits, 1);
  EXPECT_EQ(editor->text(), QStringLiteral("1"));
  send_mouse(*editor, QEvent::MouseButtonRelease, origin + QPoint(10, 0),
             Qt::LeftButton, Qt::NoButton);
  EXPECT_EQ(edits, 1);
}

TEST_F(QtNumericSpinBoxTests,
       EscapeRestoresTheDragOriginAndIgnoresTheTrailingRelease) {
  quader::QtSpinBox spin;
  spin.setRange(-100, 100);
  spin.setValue(17);
  show(spin);
  auto &control = quader::qt_install_numeric_spin_box(spin);
  auto *editor = spin.findChild<QLineEdit *>();
  ASSERT_NE(editor, nullptr);
  int finishes = 0;
  control.drag_finished = [&]() { ++finishes; };

  const QPoint origin = begin_drag(*editor);
  send_mouse(*editor, QEvent::MouseMove, origin + QPoint(32, 0),
             Qt::NoButton, Qt::LeftButton);
  ASSERT_NE(spin.value(), 17);
  send_key(*editor, Qt::Key_Escape);
  EXPECT_EQ(spin.value(), 17);
  EXPECT_FALSE(control.is_dragging());
  EXPECT_FALSE(quader::qt_numeric_drag_active(application()));
  EXPECT_EQ(finishes, 1);
  send_mouse(*editor, QEvent::MouseButtonRelease, origin + QPoint(32, 0),
             Qt::LeftButton, Qt::NoButton);
  EXPECT_EQ(spin.value(), 17);
  EXPECT_FALSE(control.is_editing());
  EXPECT_EQ(finishes, 1);
}

TEST_F(QtNumericSpinBoxTests, NativeTextEntryCommitsAndEscapeRestoresTheValue) {
  quader::QtSpinBox spin;
  spin.setRange(0, 999);
  spin.setKeyboardTracking(false);
  spin.setValue(12);
  show(spin);
  auto &control = quader::qt_install_numeric_spin_box(spin);
  auto *editor = spin.findChild<QLineEdit *>();
  ASSERT_NE(editor, nullptr);

  click_center(*editor);
  type_text(*editor, QStringLiteral("27"));
  send_key(*editor, Qt::Key_Return);
  EXPECT_EQ(spin.value(), 27);
  EXPECT_FALSE(control.is_editing());

  click_center(*editor);
  type_text(*editor, QStringLiteral("93"));
  send_key(*editor, Qt::Key_Escape);
  EXPECT_EQ(spin.value(), 27);
  EXPECT_EQ(editor->text(), QStringLiteral("27"));
  EXPECT_FALSE(control.is_editing());
}

TEST_F(QtNumericSpinBoxTests, TabFocusEntersTextWithoutRequiringAPointer) {
  quader::QtSpinBox spin;
  spin.setValue(34);
  show(spin);
  auto &control = quader::qt_install_numeric_spin_box(spin);
  auto *editor = spin.findChild<QLineEdit *>();
  ASSERT_NE(editor, nullptr);

  QFocusEvent focus(QEvent::FocusIn, Qt::TabFocusReason);
  QCoreApplication::sendEvent(&spin, &focus);
  EXPECT_TRUE(control.is_editing());
  EXPECT_EQ(editor->selectedText(), editor->text());
  EXPECT_EQ(editor->cursor().shape(), Qt::IBeamCursor);
  type_text(*editor, QStringLiteral("45"));
  send_key(*editor, Qt::Key_Return);
  EXPECT_EQ(spin.value(), 45);
}

TEST_F(QtNumericSpinBoxTests, ReadOnlyAndDisabledFieldsCannotScrubOrStep) {
  quader::QtSpinBox spin;
  spin.setValue(20);
  show(spin);
  auto &control = quader::qt_install_numeric_spin_box(spin);
  auto *editor = spin.findChild<QLineEdit *>();
  auto *increase = spin.findChild<QToolButton *>(
      QStringLiteral("numeric_spin_box.increase"));
  ASSERT_NE(editor, nullptr);
  ASSERT_NE(increase, nullptr);

  for (bool read_only : {true, false}) {
    SCOPED_TRACE(read_only ? "read-only" : "disabled");
    spin.setReadOnly(read_only);
    spin.setEnabled(read_only);
    enter(spin);
    EXPECT_FALSE(increase->isVisible());
    EXPECT_NE(editor->cursor().shape(), Qt::SizeHorCursor);
    const QPoint origin = begin_drag(*editor);
    send_mouse(*editor, QEvent::MouseMove, origin + QPoint(32, 0),
               Qt::NoButton, Qt::LeftButton);
    send_mouse(*editor, QEvent::MouseButtonRelease, origin + QPoint(32, 0),
               Qt::LeftButton, Qt::NoButton);
    increase->click();
    EXPECT_FALSE(control.is_dragging());
    EXPECT_FALSE(control.is_editing());
    EXPECT_EQ(spin.value(), 20);
  }
}

TEST_F(QtNumericSpinBoxTests,
       LostCaptureOrFocusSettlesTheDragAndStopsFurtherMotion) {
  for (QEvent::Type interruption : {QEvent::UngrabMouse, QEvent::FocusOut,
                                   QEvent::WindowDeactivate}) {
    SCOPED_TRACE(static_cast<int>(interruption));
    quader::QtSpinBox spin;
    spin.setValue(20);
    show(spin);
    auto &control = quader::qt_install_numeric_spin_box(spin);
    auto *editor = spin.findChild<QLineEdit *>();
    ASSERT_NE(editor, nullptr);
    int finishes = 0;
    control.drag_finished = [&]() { ++finishes; };

    const QPoint origin = begin_drag(*editor);
    send_mouse(*editor, QEvent::MouseMove, origin + QPoint(16, 0),
               Qt::NoButton, Qt::LeftButton);
    ASSERT_TRUE(control.is_dragging());
    const int settled_value = spin.value();
    if (interruption == QEvent::FocusOut) {
      QFocusEvent event(interruption, Qt::OtherFocusReason);
      QCoreApplication::sendEvent(&spin, &event);
    } else {
      QEvent event(interruption);
      QCoreApplication::sendEvent(&spin, &event);
    }
    EXPECT_FALSE(control.is_dragging());
    EXPECT_EQ(finishes, 1);
    EXPECT_FALSE(quader::qt_numeric_drag_active(application()));
    send_mouse(*editor, QEvent::MouseMove, origin + QPoint(40, 0),
               Qt::NoButton, Qt::LeftButton);
    send_mouse(*editor, QEvent::MouseButtonRelease, origin + QPoint(40, 0),
               Qt::LeftButton, Qt::NoButton);
    EXPECT_EQ(spin.value(), settled_value);
    EXPECT_FALSE(control.is_editing());
    EXPECT_EQ(finishes, 1);
  }
}

TEST_F(QtNumericSpinBoxTests,
       LocaleAndCompactPresentationRetainUneditedFullPrecision) {
  quader::QtDoubleSpinBox spin;
  spin.setLocale(QLocale(QLocale::German, QLocale::Germany));
  spin.setDecimals(8);
  spin.setRange(-100.0, 100.0);
  spin.setKeyboardTracking(false);
  spin.setSuffix(QStringLiteral(" cm"));
  spin.setValue(1.23456789);
  show(spin);
  auto *editor = spin.findChild<QLineEdit *>();
  ASSERT_NE(editor, nullptr);
  EXPECT_TRUE(editor->text().contains(QStringLiteral("1,2346")));

  click_center(*editor);
  send_key(*editor, Qt::Key_Return);
  EXPECT_DOUBLE_EQ(spin.value(), 1.23456789);

  click_center(*editor);
  type_text(*editor, QStringLiteral("2,75"));
  send_key(*editor, Qt::Key_Return);
  EXPECT_DOUBLE_EQ(spin.value(), 2.75);
  EXPECT_TRUE(editor->text().endsWith(QStringLiteral(" cm")));
}

TEST_F(QtNumericSpinBoxTests,
       ApplicationDefaultAdoptsExistingAndQtCreatedDialogFieldsOnce) {
  quader::QtUiScaleApplicationController scale_controller(application());
  QSpinBox existing;
  existing.setValue(12);
  show(existing);
  quader::qt_install_default_numeric_spin_boxes(application());
  quader::qt_install_default_numeric_spin_boxes(application());

  QColorDialog dialog;
  dialog.setOption(QColorDialog::DontUseNativeDialog);
  dialog.setOption(QColorDialog::ShowAlphaChannel);
  dialog.setAttribute(Qt::WA_DontShowOnScreen, true);
  dialog.show();
  QCoreApplication::processEvents();
  auto fields = dialog.findChildren<QSpinBox *>();
  ASSERT_FALSE(fields.isEmpty());
  // Exercise the full-width hue/RGB/alpha numbers without causing color-space
  // synchronization to replace another field's sample before it is measured.
  for (QSpinBox *field : fields) {
    const QSignalBlocker blocker(field);
    field->setValue(field->maximum());
  }
  QCoreApplication::processEvents();
  fields.push_back(&existing);
  const auto expect_readable_fields = [&]() {
    for (QSpinBox *field : fields) {
      SCOPED_TRACE(field->objectName().toStdString());
      auto *editor = field->findChild<QLineEdit *>();
      ASSERT_NE(editor, nullptr);
      EXPECT_EQ(field->findChildren<QToolButton *>(
                         QStringLiteral("numeric_spin_box.decrease")).size(),
                1);
      EXPECT_EQ(field->findChildren<QToolButton *>(
                         QStringLiteral("numeric_spin_box.increase")).size(),
                1);
      enter(*field);
      EXPECT_EQ(editor->cursor().shape(), Qt::SizeHorCursor);
      if (field != &existing) {
        EXPECT_EQ(field->value(), field->maximum());
      }
      const QMargins margins = editor->textMargins();
      const int text_space = editor->contentsRect().width() - margins.left() -
                             margins.right();
      EXPECT_GE(text_space,
                editor->fontMetrics().horizontalAdvance(editor->text()))
          << "numeric arrows must reserve width for the complete native text: "
          << editor->text().toStdString();
    }
  };
  std::vector<int> initial_minimum_widths;
  for (QSpinBox *field : fields) {
    initial_minimum_widths.push_back(field->minimumWidth());
  }
  expect_readable_fields();
  export_review_image(dialog, QStringLiteral("numeric-color-dialog.png"));

  ASSERT_TRUE(scale_controller.apply_factor(2.0F));
  QCoreApplication::processEvents();
  dialog.adjustSize();
  QCoreApplication::processEvents();
  expect_readable_fields();
  export_review_image(dialog, QStringLiteral("numeric-color-dialog-200.png"));

  ASSERT_TRUE(scale_controller.apply_factor(1.0F));
  QCoreApplication::processEvents();
  dialog.adjustSize();
  QCoreApplication::processEvents();
  expect_readable_fields();
  for (qsizetype index = 0; index < fields.size(); ++index) {
    EXPECT_EQ(fields[index]->minimumWidth(),
              initial_minimum_widths[static_cast<std::size_t>(index)])
        << fields[index]->objectName().toStdString();
  }

  QObject grid_context;
  quader::qt_set_numeric_snap_increment_provider(
      application(), grid_context, []() { return 8.0; });
  QSpinBox created_after_provider;
  created_after_provider.setRange(-1000, 1000);
  show(created_after_provider);
  fields.push_back(&created_after_provider);
  for (QSpinBox *field : fields) {
    SCOPED_TRACE(field->objectName().toStdString());
    const QSignalBlocker blocker(field);
    field->setValue(0);
    auto &control = quader::qt_install_numeric_spin_box(*field);
    control.set_drag_sensitivity(1.0);
    auto *editor = field->findChild<QLineEdit *>();
    ASSERT_NE(editor, nullptr);
    const QPoint origin = begin_drag(*editor, Qt::ControlModifier);
    send_mouse(*editor, QEvent::MouseMove, origin + QPoint(19, 0),
               Qt::NoButton, Qt::LeftButton, Qt::ControlModifier);
    EXPECT_EQ(field->value(), 16);
    send_mouse(*editor, QEvent::MouseButtonRelease, origin + QPoint(19, 0),
               Qt::LeftButton, Qt::NoButton, Qt::ControlModifier);
  }
}

TEST_F(QtNumericSpinBoxTests,
       InspectorDragCommitsOneUndoAndEscapeRollsBackTheOwnerPreview) {
  using namespace quader;
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto entity = editor.scene().create_empty();
  ASSERT_TRUE(entity.ok()) << entity.error_message();
  ASSERT_TRUE(editor.scene().select(entity.value().id).ok());
  const std::size_t undo_before = session.history().undo_count();
  UISettings ui_settings = UISettings::defaults();
  ViewportSettings viewport_settings = ViewportSettings::defaults();
  LocalizationService localization;
  UIInspectorPreviewContext preview{.document = session.scene(),
                                    .assets = session.assets()};
  UIContext context{
      .intent = {.editor = editor},
      .scene = {.document = session.scene(),
                .assets = session.assets(),
                .modeling = session.modeling(),
                .selection = session.selection()},
      .inspector_preview = &preview,
      .preferences = {.ui = ui_settings, .viewport = viewport_settings},
      .localization = localization,
  };
  InspectorPanel panel;
  panel.refresh(context);
  show(panel);
  panel.resize(640, 600);
  QCoreApplication::processEvents();
  const QString field_name = QStringLiteral("inspector.transform.position.x");
  auto *position_x = panel.findChild<QDoubleSpinBox *>(field_name);
  ASSERT_NE(position_x, nullptr);
  auto *line_edit = position_x->findChild<QLineEdit *>();
  ASSERT_NE(line_edit, nullptr);
  constexpr double kPositionSensitivity = 0.01;

  const QPoint origin = begin_drag(*line_edit);
  for (int steps : {1, 2, 4}) {
    send_mouse(*line_edit, QEvent::MouseMove, origin + QPoint(steps * 8, 0),
               Qt::NoButton, Qt::LeftButton);
    process_editor_events();
    const auto live = editor.query().entity(entity.value().id);
    ASSERT_TRUE(live.has_value());
    EXPECT_NEAR(live->transform.position.x,
                steps * 8.0 * kPositionSensitivity, 0.0001);
    EXPECT_EQ(session.history().undo_count(), undo_before);
  }
  send_mouse(*line_edit, QEvent::MouseButtonRelease, origin + QPoint(32, 0),
             Qt::LeftButton, Qt::NoButton);
  EXPECT_EQ(session.history().undo_count(), undo_before + 1u);
  ASSERT_TRUE(editor.history().undo().ok());
  auto snapshot = editor.query().entity(entity.value().id);
  ASSERT_TRUE(snapshot.has_value());
  EXPECT_FLOAT_EQ(snapshot->transform.position.x, 0.0f);
  ASSERT_TRUE(editor.history().redo().ok());
  panel.refresh(context);
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  position_x = panel.findChild<QDoubleSpinBox *>(field_name);
  ASSERT_NE(position_x, nullptr);
  line_edit = position_x->findChild<QLineEdit *>();
  ASSERT_NE(line_edit, nullptr);
  const double committed_value = position_x->value();
  EXPECT_NEAR(committed_value, 32.0 * kPositionSensitivity, 0.0001);

  const QPoint cancel_origin = begin_drag(*line_edit);
  send_mouse(*line_edit, QEvent::MouseMove, cancel_origin + QPoint(48, 0),
             Qt::NoButton, Qt::LeftButton);
  process_editor_events();
  snapshot = editor.query().entity(entity.value().id);
  ASSERT_TRUE(snapshot.has_value());
  EXPECT_GT(snapshot->transform.position.x, committed_value);
  send_key(*line_edit, Qt::Key_Escape);
  snapshot = editor.query().entity(entity.value().id);
  ASSERT_TRUE(snapshot.has_value());
  EXPECT_NEAR(snapshot->transform.position.x, committed_value, 0.0001);
  EXPECT_EQ(session.history().undo_count(), undo_before + 1u);

  send_mouse(*line_edit, QEvent::MouseButtonRelease,
             cancel_origin + QPoint(48, 0), Qt::LeftButton, Qt::NoButton);
  QFocusEvent focus_out(QEvent::FocusOut, Qt::OtherFocusReason);
  QCoreApplication::sendEvent(position_x, &focus_out);
  panel.refresh(context);
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  EXPECT_EQ(session.history().undo_count(), undo_before + 1u);
  snapshot = editor.query().entity(entity.value().id);
  ASSERT_TRUE(snapshot.has_value());
  EXPECT_NEAR(snapshot->transform.position.x, committed_value, 0.0001);
  panel.abandon_runtime();
}

} // namespace
