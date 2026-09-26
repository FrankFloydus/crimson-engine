////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "api/quader_editor_api.hpp"
#include "ui/qt_contextual_input_hints.hpp"
#include "ui/qt_double_spin_box.hpp"
#include "ui/qt_face_texture_canvas.hpp"
#include "ui/qt_numeric_spin_box.hpp"
#include "ui/qt_semantic_axis_colors.hpp"
#include "ui/qt_spin_box.hpp"
#include "ui/qt_viewport_control_strip.hpp"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QRegion>
#include <QScopeGuard>
#include <QToolButton>
#include <QWidgetAction>
#include <QWindow>

#include <algorithm>
#include <memory>

namespace quader {
namespace {

class QtContextualInputHintsTests : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(QCoreApplication::instance(), nullptr);
    application_ = std::make_unique<QApplication>(argc_, argv_);
    application_->setQuitOnLastWindowClosed(false);
    QApplication::setStyle(QStringLiteral("Fusion"));
    static_cast<void>(qt_contextual_input_hints_state(*application_));
  }

  void TearDown() override { application_.reset(); }

  void show(QWidget &widget) {
    widget.setAttribute(Qt::WA_DontShowOnScreen);
    widget.resize(420, 320);
    widget.show();
    QApplication::setActiveWindow(&widget);
    QCoreApplication::processEvents();
  }

  QtContextualInputHintState state() {
    return qt_contextual_input_hints_state(*application_);
  }

  std::span<const QuaderOperationInputHint> hints() {
    return qt_contextual_input_hints(*application_);
  }

  bool has(std::string_view key) {
    return std::ranges::any_of(hints(), [key](const auto &hint) {
      return hint.label_key == key;
    });
  }

  bool active(std::string_view key) {
    const auto current = hints();
    const auto found = std::ranges::find_if(current, [key](const auto &hint) {
      return hint.label_key == key;
    });
    return found != current.end() && found->active;
  }

  static void mouse(QWidget &widget, QEvent::Type type, QPointF position,
                    Qt::MouseButton button = Qt::NoButton,
                    Qt::MouseButtons buttons = Qt::NoButton,
                    Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QMouseEvent event(type, position, widget.mapToGlobal(position.toPoint()),
                      button, buttons, modifiers);
    QCoreApplication::sendEvent(&widget, &event);
  }

  static void key(QWidget &widget, int key,
                  Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QKeyEvent event(QEvent::KeyPress, key, modifiers);
    QCoreApplication::sendEvent(&widget, &event);
  }

  static void register_hint(QWidget &widget, bool &active,
                            std::uint64_t &revision, const char *label) {
    qt_register_contextual_input_hints(
        widget, widget,
        [&active, &revision](Qt::KeyboardModifiers) {
          return QtContextualInputHintSourceState{revision, true, active};
        },
        [label](Qt::KeyboardModifiers) {
          return std::vector<QuaderOperationInputHint>{{
              QuaderOperationInputGesture::Information, {}, label, label}};
        });
  }

  int argc_ = 1;
  char executable_[32] = "quader-contextual-hint-tests";
  char *argv_[2] = {executable_, nullptr};
  std::unique_ptr<QApplication> application_;
};

TEST_F(QtContextualInputHintsTests,
       ObservationsDoNotBuildAndUnchangedFramesReuseProjection) {
  QWidget surface;
  std::uint64_t source_revision = 1;
  int builds = 0;
  qt_register_contextual_input_hints(
      surface, surface,
      [&source_revision](Qt::KeyboardModifiers) {
        return QtContextualInputHintSourceState{source_revision};
      },
      [&builds](Qt::KeyboardModifiers modifiers) {
        ++builds;
        return std::vector<QuaderOperationInputHint>{{
            QuaderOperationInputGesture::Shift, {}, "test.precision",
            "Precision", 70, modifiers.testFlag(Qt::ShiftModifier)}};
      });
  show(surface);
  mouse(surface, QEvent::MouseMove, {10, 10});
  const auto first = state();
  EXPECT_TRUE(first.overrides_viewport);
  EXPECT_FALSE(first.active);
  EXPECT_FALSE(first.blocks_viewport);
  EXPECT_EQ(builds, 0);
  ASSERT_EQ(hints().size(), 1u);
  const auto *storage = hints().data();
  for (int frame = 0; frame != 32; ++frame) {
    EXPECT_EQ(state().revision, first.revision);
    EXPECT_EQ(hints().data(), storage);
  }
  EXPECT_EQ(builds, 1);
  ++source_revision;
  EXPECT_GT(state().revision, first.revision);
  EXPECT_EQ(builds, 1);
  EXPECT_FALSE(hints().empty());
  EXPECT_EQ(builds, 2);
  key(surface, Qt::Key_Shift, Qt::ShiftModifier);
  EXPECT_TRUE(active("test.precision"));
  EXPECT_EQ(builds, 3);
}

TEST_F(QtContextualInputHintsTests,
       CaptureKeepsItsOwnerUntilSettlementAndDestroyedSourcesCannotLeak) {
  QWidget host;
  auto *first = new QWidget(&host);
  QWidget second(&host);
  first->setGeometry(0, 0, 150, 150);
  second.setGeometry(160, 0, 150, 150);
  bool first_active = false;
  bool second_active = false;
  std::uint64_t first_revision = 1;
  std::uint64_t second_revision = 1;
  register_hint(*first, first_active, first_revision, "first");
  register_hint(second, second_active, second_revision, "second");
  show(host);
  mouse(*first, QEvent::MouseMove, {10, 10});
  EXPECT_TRUE(has("first"));
  first_active = true;
  ++first_revision;
  // Move outside before the next status refresh: capture comes from the owner.
  mouse(second, QEvent::MouseMove, {10, 10});
  EXPECT_TRUE(state().active);
  EXPECT_TRUE(has("first"));
  first_active = false;
  ++first_revision;
  EXPECT_TRUE(has("second"));
  mouse(*first, QEvent::MouseMove, {10, 10});
  EXPECT_TRUE(has("first"));
  const auto before_destroy = state().revision;
  delete first;
  EXPECT_GT(state().revision, before_destroy);
  EXPECT_TRUE(hints().empty());
}

TEST_F(QtContextualInputHintsTests,
       PropagatedChildMouseMovesKeepTheDeepestHoveredOwner) {
  QWidget host;
  QWidget child(&host);
  child.setGeometry(0, 0, 100, 100);
  child.setMouseTracking(true);
  host.setMouseTracking(true);
  bool active = false;
  std::uint64_t revision = 1;
  register_hint(child, active, revision, "child");
  show(host);
  mouse(child, QEvent::MouseMove, {10, 10});
  EXPECT_TRUE(has("child"));
  // This is the same global position as the event propagated from the child.
  mouse(host, QEvent::MouseMove, child.mapTo(&host, QPoint(10, 10)));
  EXPECT_TRUE(has("child"));
  mouse(host, QEvent::MouseMove, {200, 200});
  EXPECT_FALSE(state().overrides_viewport);
  EXPECT_TRUE(hints().empty());
}

TEST_F(QtContextualInputHintsTests,
       NativeDragDestinationOverridesTheOriginalCaptureAcrossSurfaces) {
  QWidget host;
  QWidget origin(&host);
  QWidget destination(&host);
  origin.setGeometry(0, 0, 150, 150);
  destination.setGeometry(160, 0, 150, 150);
  destination.setAcceptDrops(true);
  bool origin_active = true;
  bool destination_active = true;
  std::uint64_t revision = 1;
  register_hint(origin, origin_active, revision, "origin");
  register_hint(destination, destination_active, revision, "destination");
  show(host);
  mouse(origin, QEvent::MouseMove, {10, 10});
  ASSERT_TRUE(has("origin"));
  QMimeData mime;
  mime.setText(QStringLiteral("drag fixture"));
  QDragEnterEvent enter(QPoint(10, 10), Qt::MoveAction, &mime,
                        Qt::LeftButton, Qt::NoModifier);
  QCoreApplication::sendEvent(&destination, &enter);
  EXPECT_TRUE(has("destination"));
  EXPECT_FALSE(has("origin"));
  QDragLeaveEvent leave;
  QCoreApplication::sendEvent(&destination, &leave);
  EXPECT_TRUE(state().blocks_viewport);
  EXPECT_TRUE(hints().empty());
  origin_active = false;
  destination_active = false;
  key(host, Qt::Key_Escape);
  mouse(origin, QEvent::MouseMove, {10, 10});
  EXPECT_TRUE(has("origin"));
}

TEST_F(QtContextualInputHintsTests,
       TextPopupAndApplicationDeactivationSuppressViewportFallback) {
  QWidget host;
  QLineEdit text(&host);
  QWidget surface(&host);
  text.setGeometry(0, 0, 150, 30);
  surface.setGeometry(0, 40, 200, 150);
  bool gesture = false;
  std::uint64_t revision = 1;
  register_hint(surface, gesture, revision, "surface");
  show(host);
  text.setFocus();
  mouse(surface, QEvent::MouseMove, {10, 10});
  EXPECT_TRUE(state().blocks_viewport);
  EXPECT_TRUE(hints().empty());
  text.clearFocus();
  EXPECT_TRUE(has("surface"));

  QMenu popup(&host);
  popup.addAction(QStringLiteral("Menu action"));
  // Keep the menu native: WA_DontShowOnScreen leaves its QWindow invisible,
  // which Qt excludes from the active popup stack even when the QWidget shows.
  // Native menu effects defer QWidget::show(), and thus popup ownership. This
  // fixture checks routing after opening, independently of host animation prefs.
  const bool animate_menus = QApplication::isEffectEnabled(Qt::UI_AnimateMenu);
  const auto restore_menu_effects = qScopeGuard([animate_menus]() {
    QApplication::setEffectEnabled(Qt::UI_AnimateMenu, animate_menus);
  });
  QApplication::setEffectEnabled(Qt::UI_AnimateMenu, false);
  popup.popup(host.mapToGlobal(QPoint(30, 30)));
  QCoreApplication::processEvents();
  ASSERT_EQ(QApplication::activePopupWidget(), &popup);
  EXPECT_TRUE(state().blocks_viewport);
  EXPECT_TRUE(hints().empty());
  popup.close();

  QEvent deactivate(QEvent::ApplicationDeactivate);
  QCoreApplication::sendEvent(application_.get(), &deactivate);
  EXPECT_TRUE(state().blocks_viewport);
  EXPECT_TRUE(hints().empty());
  QEvent activate(QEvent::ApplicationActivate);
  QCoreApplication::sendEvent(application_.get(), &activate);
  mouse(surface, QEvent::MouseMove, {10, 10});
  EXPECT_TRUE(has("surface"));
}

TEST_F(QtContextualInputHintsTests,
       ViewportStripControlsKeepFallbackAcrossFocusPressAndRelease) {
  QWidget host;
  QtViewportControlStrip strip(&host);
  strip.setGeometry(0, 0, 420, 36);
  QAction action(QStringLiteral("Toggle snapping"), &host);
  action.setObjectName(QStringLiteral("hintToggle"));
  action.setCheckable(true);
  strip.add_action(action);
  QWidget panel(&host);
  panel.setGeometry(0, 60, 200, 150);
  bool panel_active = false;
  std::uint64_t panel_revision = 1;
  register_hint(panel, panel_active, panel_revision, "test.panel_hint");
  int commands = 0;
  QObject::connect(&action, &QAction::triggered, &host,
                   [&commands]() { ++commands; });
  show(host);
  auto *button = strip.findChild<QToolButton *>(
      QStringLiteral("viewportStrip_hintToggle"));
  ASSERT_NE(button, nullptr);
  ASSERT_TRUE(button->isVisible());
  mouse(panel, QEvent::MouseMove, {10, 10});
  ASSERT_TRUE(has("test.panel_hint"));

  const QPointF center(button->rect().center());
  mouse(*button, QEvent::MouseMove, center);
  button->setFocus(Qt::TabFocusReason);
  ASSERT_TRUE(button->hasFocus());
  const auto before = state();
  EXPECT_FALSE(before.overrides_viewport);
  EXPECT_FALSE(before.blocks_viewport);
  EXPECT_FALSE(has("test.panel_hint"));
  const auto expect_fallback = [&]() {
    const auto current = state();
    EXPECT_FALSE(current.overrides_viewport);
    EXPECT_FALSE(current.blocks_viewport);
    EXPECT_EQ(current.revision, before.revision);
  };
  mouse(*button, QEvent::MouseButtonPress, center,
         Qt::LeftButton, Qt::LeftButton);
  expect_fallback();
  EXPECT_EQ(commands, 0);
  mouse(*button, QEvent::MouseButtonRelease, center, Qt::LeftButton);
  expect_fallback();
  EXPECT_EQ(commands, 1);
  key(*button, Qt::Key_Space);
  expect_fallback();
  QKeyEvent release(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
  QCoreApplication::sendEvent(button, &release);
  expect_fallback();
  EXPECT_EQ(commands, 2);
  QCoreApplication::processEvents();
  expect_fallback();
}

TEST_F(QtContextualInputHintsTests,
       ViewportStripMenusKeepFallbackWithoutLeakingAnUnrelatedPanelHint) {
  QWidget host;
  QtViewportControlStrip strip(&host);
  strip.setGeometry(0, 0, 420, 36);
  QWidget panel(&host);
  panel.setGeometry(0, 60, 200, 150);
  bool panel_active = false;
  std::uint64_t panel_revision = 1;
  register_hint(panel, panel_active, panel_revision, "test.panel_hint");
  QMenu menu(&strip);
  auto *submenu = menu.addMenu(QStringLiteral("Shading"));
  submenu->addAction(QStringLiteral("Wireframe"));
  QMenu unrelated(&host);
  unrelated.addAction(QStringLiteral("Panel action"));
  show(host);
  mouse(panel, QEvent::MouseMove, {10, 10});
  ASSERT_TRUE(has("test.panel_hint"));

  const bool animate_menus = QApplication::isEffectEnabled(Qt::UI_AnimateMenu);
  const auto restore_menu_effects = qScopeGuard([animate_menus]() {
    QApplication::setEffectEnabled(Qt::UI_AnimateMenu, animate_menus);
  });
  QApplication::setEffectEnabled(Qt::UI_AnimateMenu, false);
  menu.popup(host.mapToGlobal(QPoint(20, 36)));
  QCoreApplication::processEvents();
  ASSERT_EQ(QApplication::activePopupWidget(), &menu);
  const auto expect_fallback = [&]() {
    const auto current = state();
    EXPECT_FALSE(current.overrides_viewport);
    EXPECT_FALSE(current.blocks_viewport);
    EXPECT_FALSE(has("test.panel_hint"));
  };
  expect_fallback();
  const auto menu_revision = state().revision;
  for (int frame = 0; frame < 16; ++frame) {
    EXPECT_EQ(state().revision, menu_revision);
  }
  submenu->popup(menu.mapToGlobal(QPoint(menu.width(), 0)));
  QCoreApplication::processEvents();
  ASSERT_EQ(QApplication::activePopupWidget(), submenu);
  mouse(*submenu, QEvent::MouseMove, {8, 8});
  expect_fallback();
  submenu->close();
  menu.close();

  unrelated.popup(host.mapToGlobal(QPoint(20, 60)));
  QCoreApplication::processEvents();
  ASSERT_EQ(QApplication::activePopupWidget(), &unrelated);
  EXPECT_TRUE(state().blocks_viewport);
  EXPECT_TRUE(hints().empty());
  unrelated.close();
  mouse(panel, QEvent::MouseMove, {10, 10});
  EXPECT_TRUE(has("test.panel_hint"));
}

TEST_F(QtContextualInputHintsTests,
       ViewportStripOwnershipDoesNotBypassTextOrModalSuppression) {
  QWidget host;
  QtViewportControlStrip strip(&host);
  strip.setGeometry(0, 0, 420, 36);
  QLineEdit field(&host);
  field.setGeometry(0, 60, 200, 30);
  QMenu menu(&strip);
  auto *editor_action = new QWidgetAction(&menu);
  auto *popup_editor = new QLineEdit;
  editor_action->setDefaultWidget(popup_editor);
  menu.addAction(editor_action);
  show(host);
  field.setFocus();
  ASSERT_TRUE(field.hasFocus());
  mouse(strip, QEvent::MouseMove, {10, 10});
  EXPECT_TRUE(state().blocks_viewport);
  EXPECT_TRUE(hints().empty());
  field.clearFocus();

  const bool animate_menus = QApplication::isEffectEnabled(Qt::UI_AnimateMenu);
  const auto restore_menu_effects = qScopeGuard([animate_menus]() {
    QApplication::setEffectEnabled(Qt::UI_AnimateMenu, animate_menus);
  });
  QApplication::setEffectEnabled(Qt::UI_AnimateMenu, false);
  menu.popup(host.mapToGlobal(QPoint(20, 36)));
  QCoreApplication::processEvents();
  ASSERT_EQ(QApplication::activePopupWidget(), &menu);
  popup_editor->setFocus(Qt::TabFocusReason);
  ASSERT_EQ(QApplication::focusWidget(), popup_editor);
  mouse(*popup_editor, QEvent::MouseMove, {8, 8});
  EXPECT_TRUE(state().blocks_viewport);
  EXPECT_TRUE(hints().empty());
  menu.close();

  QDialog dialog(&host);
  dialog.setModal(true);
  show(dialog);
  ASSERT_EQ(QApplication::activeModalWidget(), &dialog);
  mouse(strip, QEvent::MouseMove, {10, 10});
  EXPECT_TRUE(state().blocks_viewport);
  EXPECT_TRUE(hints().empty());
  dialog.close();
}

TEST_F(QtContextualInputHintsTests,
       EventRefreshIsQueuedCoalescedAndRespectsPresenterLifetime) {
  QWidget surface;
  show(surface);
  auto lifetime = std::make_unique<QObject>();
  int refreshes = 0;
  qt_set_contextual_input_hints_refresh_callback(
      *application_, *lifetime, [&refreshes]() { ++refreshes; });
  mouse(surface, QEvent::MouseMove, {10, 10});
  mouse(surface, QEvent::MouseMove, {11, 10});
  EXPECT_EQ(refreshes, 0);
  QCoreApplication::processEvents();
  EXPECT_EQ(refreshes, 1);
  mouse(surface, QEvent::MouseMove, {12, 10});
  lifetime.reset();
  QCoreApplication::processEvents();
  EXPECT_EQ(refreshes, 1);
}

TEST_F(QtContextualInputHintsTests,
       NativeWindowProvidersUseTheSameLazyOwnershipRoute) {
  QWidget host;
  auto *surface = new QWindow;
  auto *container = QWidget::createWindowContainer(surface, &host);
  container->setGeometry(0, 0, 200, 150);
  qt_register_contextual_input_hints(
      *surface, *surface,
      [](Qt::KeyboardModifiers) { return QtContextualInputHintSourceState{}; },
      [](Qt::KeyboardModifiers) {
        return std::vector<QuaderOperationInputHint>{{
            QuaderOperationInputGesture::MiddleDrag, {},
            "test.preview", "Rotate Preview"}};
      });
  show(host);
  surface->setVisible(true);
  QMouseEvent move(QEvent::MouseMove, QPointF(10, 10), QPointF(10, 10),
                   Qt::NoButton, Qt::NoButton, Qt::NoModifier);
  QCoreApplication::sendEvent(surface, &move);
  EXPECT_TRUE(state().overrides_viewport);
  EXPECT_TRUE(has("test.preview"));
}

TEST_F(QtContextualInputHintsTests,
       NumericDraftAndScrubReplaceEnclosingHintsAndRespectIntegerModifiers) {
  QWidget host;
  QtDoubleSpinBox floating(&host);
  QtSpinBox integer(&host);
  floating.setGeometry(0, 0, 200, 32);
  integer.setGeometry(0, 40, 200, 32);
  floating.setRange(-1000, 1000);
  integer.setRange(-1000, 1000);
  show(host);
  auto *field = floating.findChild<QLineEdit *>();
  ASSERT_NE(field, nullptr);
  field->setFocus();
  const QPointF center(field->rect().center());
  mouse(*field, QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
  mouse(*field, QEvent::MouseButtonRelease, center, Qt::LeftButton);
  ASSERT_TRUE(qt_install_numeric_spin_box(floating).is_editing());
  EXPECT_TRUE(state().active);
  EXPECT_TRUE(has("operation.hints.accept_value"));
  EXPECT_TRUE(has("operation.hints.restore_value"));
  EXPECT_FALSE(has("operation.hints.numeric_snap"));
  key(*field, Qt::Key_Escape);
  ASSERT_FALSE(qt_install_numeric_spin_box(floating).is_editing());

  for (QAbstractSpinBox *spin : {static_cast<QAbstractSpinBox *>(&floating),
                                static_cast<QAbstractSpinBox *>(&integer)}) {
    auto *editor = spin->findChild<QLineEdit *>();
    const QPointF start(editor->rect().center());
    mouse(*editor, QEvent::MouseButtonPress, start,
           Qt::LeftButton, Qt::LeftButton);
    mouse(*editor, QEvent::MouseMove,
           start + QPointF(QApplication::startDragDistance() + 4, 0),
           Qt::NoButton, Qt::LeftButton, Qt::ControlModifier | Qt::ShiftModifier);
    ASSERT_TRUE(qt_install_numeric_spin_box(*spin).is_dragging());
    EXPECT_TRUE(state().active);
    EXPECT_TRUE(has("operation.hints.cancel_value"));
    EXPECT_TRUE(active("operation.hints.numeric_fine_snap"));
    EXPECT_EQ(has("operation.hints.precision"), spin == &floating);
    EXPECT_FALSE(std::ranges::any_of(hints(), [](const auto &hint) {
      return hint.gesture == QuaderOperationInputGesture::Wheel;
    }));
    mouse(*editor, QEvent::MouseButtonPress, start,
           Qt::RightButton, Qt::LeftButton | Qt::RightButton);
    EXPECT_FALSE(qt_install_numeric_spin_box(*spin).is_dragging());
  }
}

TEST_F(QtContextualInputHintsTests,
       NumericEditorInsideModalKeepsItsOwnHints) {
  QDialog dialog;
  dialog.setModal(true);
  QtDoubleSpinBox spin(&dialog);
  spin.setGeometry(0, 0, 200, 32);
  show(dialog);
  auto *field = spin.findChild<QLineEdit *>();
  ASSERT_NE(field, nullptr);
  const QPointF center(field->rect().center());
  mouse(*field, QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
  mouse(*field, QEvent::MouseButtonRelease, center, Qt::LeftButton);
  EXPECT_TRUE(state().active);
  EXPECT_FALSE(state().blocks_viewport);
  EXPECT_TRUE(has("operation.hints.restore_value"));
}

TEST_F(QtContextualInputHintsTests,
       TextureGestureHintsRespectModifiersAndNavigationNeverShowsEscape) {
  auto standalone = create_standalone_editor();
  auto &editor = standalone.editor();
  const auto created = editor.modeling().create_box(
      {.min = {-64, -32, -16}, .max = {64, 32, 16}}, "Texture hints");
  ASSERT_TRUE(created.ok());
  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection().select_face(created.value(), 1).ok());
  QtFaceTextureCanvas canvas;
  canvas.bind(editor);
  show(canvas);
  QPolygonF outline;
  for (const auto &point : canvas.presentation().outline) {
    outline.push_back(QPointF(point.x, point.y));
  }
  const QRectF bounds = outline.boundingRect();
  const QPointF point = canvas.plane_to_view().map(
      bounds.center() + QPointF(bounds.width() * 0.2, bounds.height() * 0.2));
  mouse(canvas, QEvent::MouseMove, point);
  EXPECT_TRUE(has("operation.hints.pan"));
  EXPECT_TRUE(has("operation.hints.zoom"));
  EXPECT_FALSE(has("operation.hints.cancel_texture_edit"));
  mouse(canvas, QEvent::MouseButtonPress, point,
         Qt::LeftButton, Qt::LeftButton);
  ASSERT_TRUE(canvas.interaction_active());
  mouse(canvas, QEvent::MouseMove, point + QPointF(20, 10),
         Qt::NoButton, Qt::LeftButton, Qt::ControlModifier);
  EXPECT_TRUE(active("operation.hints.texture_pixel_snap"));
  EXPECT_TRUE(has("operation.hints.cancel_texture_edit"));
  EXPECT_FALSE(has("operation.hints.pan"));
  key(canvas, Qt::Key_Shift, Qt::ShiftModifier | Qt::ControlModifier);
  EXPECT_FALSE(active("operation.hints.texture_pixel_snap"));
  key(canvas, Qt::Key_Escape);
  ASSERT_FALSE(canvas.interaction_active());

  // Pick the actual painted U/V grips instead of reproducing their placement.
  for (const auto axis : {quader_editor_core::ViewAxis::X,
                          quader_editor_core::ViewAxis::Y}) {
    QImage image(canvas.size(), QImage::Format_ARGB32);
    canvas.render(&image, QPoint(), QRegion(), QWidget::DrawWindowBackground);
    const QRgb marker_color = qt_semantic_axis_color(axis).rgb();
    QRect marker;
    for (int y = 0; y < image.height(); ++y) {
      for (int x = 0; x < image.width(); ++x) {
        if (image.pixel(x, y) == marker_color) {
          marker = marker.united(QRect(x, y, 1, 1));
        }
      }
    }
    ASSERT_FALSE(marker.isEmpty());
    const QPointF handle(marker.center());
    mouse(canvas, QEvent::MouseMove, handle);
    EXPECT_TRUE(has("operation.hints.texture_unit_repeats"));
    EXPECT_TRUE(has("operation.hints.texture_grid_repeats"));
    EXPECT_FALSE(has("operation.hints.texture_pixel_snap"));
    mouse(canvas, QEvent::MouseButtonPress, handle,
           Qt::LeftButton, Qt::LeftButton);
    ASSERT_TRUE(canvas.interaction_active());
    key(canvas, Qt::Key_Control, Qt::ControlModifier);
    EXPECT_TRUE(active("operation.hints.texture_unit_repeats"));
    key(canvas, Qt::Key_Shift, Qt::ControlModifier | Qt::ShiftModifier);
    EXPECT_FALSE(active("operation.hints.texture_unit_repeats"));
    EXPECT_TRUE(active("operation.hints.texture_grid_repeats"));
    key(canvas, Qt::Key_Escape);
  }
  const QPointF rotation = canvas.plane_to_view().map(bounds.center());
  mouse(canvas, QEvent::MouseMove, rotation);
  EXPECT_TRUE(has("operation.hints.texture_degree_steps"));
  EXPECT_TRUE(has("operation.hints.texture_angle_steps"));
  mouse(canvas, QEvent::MouseButtonPress, rotation,
         Qt::LeftButton, Qt::LeftButton);
  ASSERT_TRUE(canvas.interaction_active());
  key(canvas, Qt::Key_Control, Qt::ControlModifier);
  EXPECT_TRUE(active("operation.hints.texture_degree_steps"));
  key(canvas, Qt::Key_Shift, Qt::ControlModifier | Qt::ShiftModifier);
  EXPECT_TRUE(active("operation.hints.texture_angle_steps"));
  EXPECT_FALSE(active("operation.hints.texture_degree_steps"));
  key(canvas, Qt::Key_Escape);
  ASSERT_FALSE(canvas.interaction_active());
  mouse(canvas, QEvent::MouseButtonPress, point,
         Qt::MiddleButton, Qt::MiddleButton);
  EXPECT_TRUE(state().active);
  EXPECT_TRUE(hints().empty());
  mouse(canvas, QEvent::MouseButtonRelease, point, Qt::MiddleButton);
  ASSERT_TRUE(editor.selection().clear().ok());
  canvas.refresh();
  EXPECT_FALSE(state().overrides_viewport);
  EXPECT_TRUE(hints().empty());
  canvas.abandon();
}

} // namespace
} // namespace quader
