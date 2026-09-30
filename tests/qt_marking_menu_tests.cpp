////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "api/quader_input_types.hpp"
#include "ui/qt_application_theme.hpp"
#include "ui/qt_marking_menu.hpp"
#include "ui/qt_ui_scale_metrics.hpp"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QEvent>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QScreen>
#include <QScrollBar>
#include <QStyle>
#include <QTimer>
#include <QToolButton>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

namespace quader {
namespace {

struct MarkingActions {
  MarkingActions() {
    for (std::size_t index = 0; index < slot_actions.size(); ++index) {
      owned[index] = std::make_unique<QAction>(
          QStringLiteral("Operation %1").arg(static_cast<int>(index)));
      slot_actions[index] = owned[index].get();
    }
  }

  std::array<std::unique_ptr<QAction>, 8> owned;
  std::array<QAction *, 8> slot_actions{};
};

class GeometryEventCounter final : public QObject {
public:
  int move_count() const { return move_count_; }
  int resize_count() const { return resize_count_; }

protected:
  bool eventFilter(QObject *watched, QEvent *event) override {
    if (event->type() == QEvent::Move) {
      ++move_count_;
    } else if (event->type() == QEvent::Resize) {
      ++resize_count_;
    }
    return QObject::eventFilter(watched, event);
  }

private:
  int move_count_ = 0;
  int resize_count_ = 0;
};

QuaderInputChord opening_chord() {
  return {.device = QuaderInputDevice::Keyboard, .key = QuaderKeyCode::Q};
}

void send_key(QWidget &receiver, QEvent::Type type, int key,
              Qt::KeyboardModifiers modifiers = Qt::NoModifier,
              bool auto_repeat = false) {
  QKeyEvent event(type, key, modifiers, QString{}, auto_repeat);
  QCoreApplication::sendEvent(&receiver, &event);
}

void move_pointer(QWidget &receiver, const QPoint &global_position) {
  const QPoint local_position = receiver.mapFromGlobal(global_position);
  QMouseEvent event(QEvent::MouseMove, QPointF(local_position),
                    QPointF(global_position), Qt::NoButton, Qt::NoButton,
                    Qt::NoModifier);
  QCoreApplication::sendEvent(&receiver, &event);
}

QToolButton *slot_button(QtMarkingMenu &menu, int slot) {
  return menu.findChild<QToolButton *>(
      QStringLiteral("markingMenu.slot.%1").arg(slot));
}

QListWidget *overflow_list(QtMarkingMenu &menu) {
  return menu.findChild<QListWidget *>(QStringLiteral("markingMenu.overflow"));
}

void capture_menu(QtMarkingMenu &menu, const QString &filename) {
  const QString directory = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
  if (directory.isEmpty()) {
    return;
  }
  EXPECT_TRUE(QDir().mkpath(directory));
  QImage image(menu.size(), QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  menu.render(&image);
  QRect content_bounds;
  for (int row = 0; row < image.height(); ++row) {
    const auto *pixels =
        reinterpret_cast<const QRgb *>(image.constScanLine(row));
    for (int column = 0; column < image.width(); ++column) {
      if (qAlpha(pixels[column]) != 0) {
        content_bounds |= QRect(column, row, 1, 1);
      }
    }
  }
  if (!content_bounds.isEmpty()) {
    image = image.copy(
        content_bounds.adjusted(-8, -8, 8, 8).intersected(image.rect()));
  }
  EXPECT_TRUE(image.save(QDir(directory).filePath(filename)));
}

class QtMarkingMenuTests : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(QCoreApplication::instance(), nullptr);
    application_ = std::make_unique<QApplication>(argc_, argv_);
    application_->setQuitOnLastWindowClosed(false);
    QApplication::setStyle(QStringLiteral("Fusion"));
    QApplication::setPalette(
        qt_obsidian_application_palette(QApplication::palette()));
    ASSERT_NE(QApplication::primaryScreen(), nullptr);
    anchor_ = QApplication::primaryScreen()->availableGeometry().center();
  }

  void TearDown() override { application_.reset(); }

  QAction *run_menu(QtMarkingMenu &menu,
                    const std::array<QAction *, 8> &slot_actions,
                    const std::vector<QAction *> &overflow,
                    const std::function<void()> &drive,
                    const QuaderInputChord &chord = opening_chord(),
                    const QString &title = QStringLiteral("Modeling")) {
    menu.setAttribute(Qt::WA_DontShowOnScreen, true);
    bool visited_popup = false;
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, &menu, [&]() {
      ADD_FAILURE() << "The marking menu did not finish its nested event loop.";
      menu.close();
    });
    watchdog.start(1500);
    QTimer::singleShot(0, &watchdog, [&]() {
      visited_popup = true;
      EXPECT_TRUE(menu.isVisible());
      EXPECT_EQ(menu.objectName(), QStringLiteral("markingMenu"));
      drive();
      if (menu.isVisible()) {
        ADD_FAILURE()
            << "The scripted selection or cancellation left the menu open.";
        menu.close();
      }
    });
    QAction *result = menu.exec(anchor_, chord, slot_actions, overflow, title);
    watchdog.stop();
    EXPECT_TRUE(visited_popup);
    EXPECT_FALSE(menu.isVisible());
    return result;
  }

  QPoint anchor_;

private:
  int argc_ = 1;
  char executable_[32] = "quader-marking-menu-tests";
  char *argv_[2] = {executable_, nullptr};
  std::unique_ptr<QApplication> application_;
};

TEST_F(QtMarkingMenuTests,
       InitiatingKeyReleaseReturnsHoveredActionWithoutTriggering) {
  QtMarkingMenu menu;
  MarkingActions actions;
  int trigger_count = 0;
  QObject::connect(actions.slot_actions[2], &QAction::triggered, &menu,
                   [&]() { ++trigger_count; });
  QAction *selected = run_menu(menu, actions.slot_actions, {}, [&]() {
    auto *button = slot_button(menu, 2);
    ASSERT_NE(button, nullptr);
    EXPECT_EQ(button->text(), actions.slot_actions[2]->text());
    move_pointer(*button, button->mapToGlobal(button->rect().center()));
    send_key(*button, QEvent::KeyRelease, Qt::Key_Q);
  });
  EXPECT_EQ(selected, actions.slot_actions[2]);
  EXPECT_EQ(trigger_count, 0);
}

TEST_F(QtMarkingMenuTests,
       GlobalPointerDirectionSelectsEverySectorBeyondButtons) {
  QtMarkingMenu menu;
  MarkingActions actions;
  const std::array<QPoint, 8> directions{
      QPoint(0, -1), QPoint(1, -1), QPoint(1, 0),  QPoint(1, 1),
      QPoint(0, 1),  QPoint(-1, 1), QPoint(-1, 0), QPoint(-1, -1)};
  const QRect available = QApplication::primaryScreen()->availableGeometry();
  const int outside_distance =
      std::max(available.width(), available.height()) + 1;
  for (std::size_t index = 0; index < directions.size(); ++index) {
    SCOPED_TRACE(index);
    QAction *selected = run_menu(menu, actions.slot_actions, {}, [&]() {
      const QPoint global_position =
          anchor_ + directions[index] * outside_distance;
      EXPECT_FALSE(menu.rect().contains(menu.mapFromGlobal(global_position)));
      // Deliberately inconsistent local coordinates catch accidental reliance
      // on the receiver's position instead of the event's global position.
      QMouseEvent move(QEvent::MouseMove, QPointF(menu.rect().center()),
                       QPointF(global_position), Qt::NoButton, Qt::NoButton,
                       Qt::NoModifier);
      QCoreApplication::sendEvent(&menu, &move);
      send_key(menu, QEvent::KeyRelease, Qt::Key_Q);
    });
    EXPECT_EQ(selected, actions.slot_actions[index]);
  }
}

TEST_F(QtMarkingMenuTests,
       PointerMotionKeepsPopupAndAllTargetsFixedWithoutGeometryEvents) {
  QtMarkingMenu menu;
  MarkingActions actions;
  QAction supplemental(QStringLiteral("Radial Align"));
  const QRect available = QApplication::primaryScreen()->availableGeometry();
  const QRect pointer_bounds = available.adjusted(8, 8, -8, -8);
  const int beyond_distance =
      std::max(available.width(), available.height()) + 1;
  const auto global_rect = [](QWidget &widget) {
    return QRect(widget.mapToGlobal(QPoint{}), widget.size());
  };
  for (const float factor : {1.0F, 2.0F}) {
    SCOPED_TRACE(factor);
    menu.apply_ui_scale(QtUiScaleMetrics(factor));
    QAction *selected =
        run_menu(menu, actions.slot_actions, {&supplemental}, [&]() {
          auto *list = overflow_list(menu);
          ASSERT_NE(list, nullptr);
          ASSERT_TRUE(list->isVisible());
          const QRect popup_geometry = menu.geometry();
          const QRect list_geometry = global_rect(*list);
          std::array<QRect, 8> button_geometries;
          for (int slot = 0; slot < 8; ++slot) {
            auto *button = slot_button(menu, slot);
            ASSERT_NE(button, nullptr);
            button_geometries[static_cast<std::size_t>(slot)] =
                global_rect(*button);
          }
          GeometryEventCounter geometry_events;
          menu.installEventFilter(&geometry_events);
          const std::array<QPoint, 12> pointer_positions{
              pointer_bounds.topLeft(),
              pointer_bounds.bottomRight(),
              pointer_bounds.topRight(),
              pointer_bounds.bottomLeft(),
              anchor_ + QPoint(beyond_distance, 0),
              anchor_ + QPoint(0, -beyond_distance),
              anchor_ + QPoint(-beyond_distance, 0),
              anchor_ + QPoint(0, beyond_distance),
              list->viewport()->mapToGlobal(
                  list->visualItemRect(list->item(0)).center()),
              button_geometries[2].center(),
              button_geometries[7].center(),
              anchor_};
          for (const QPoint &position : pointer_positions) {
            move_pointer(menu, position);
            EXPECT_EQ(menu.geometry(), popup_geometry);
            EXPECT_EQ(global_rect(*list), list_geometry);
            for (int slot = 0; slot < 8; ++slot) {
              EXPECT_EQ(global_rect(*slot_button(menu, slot)),
                        button_geometries[static_cast<std::size_t>(slot)]);
            }
            EXPECT_EQ(geometry_events.move_count(), 0);
            EXPECT_EQ(geometry_events.resize_count(), 0);
          }
          EXPECT_EQ(list->currentRow(), -1);
          for (int slot = 0; slot < 8; ++slot) {
            EXPECT_FALSE(slot_button(menu, slot)->isChecked());
          }
          send_key(menu, QEvent::KeyRelease, Qt::Key_Q);
        });
    EXPECT_EQ(selected, nullptr);
  }
}

TEST_F(QtMarkingMenuTests,
       RemappedKeyReleaseIgnoresModifierAndUnrelatedReleases) {
  QtMarkingMenu menu;
  MarkingActions actions;
  const QuaderInputChord chord{
      .primary = true,
      .device = QuaderInputDevice::Keyboard,
      .key = QuaderKeyCode::T,
  };
  QAction *selected = run_menu(
      menu, actions.slot_actions, {},
      [&]() {
        auto *button = slot_button(menu, 4);
        ASSERT_NE(button, nullptr);
        move_pointer(menu, button->mapToGlobal(button->rect().center()));
        send_key(menu, QEvent::KeyRelease, Qt::Key_Control);
        EXPECT_TRUE(menu.isVisible());
        send_key(menu, QEvent::KeyRelease, Qt::Key_Q);
        EXPECT_TRUE(menu.isVisible());
        send_key(menu, QEvent::KeyPress, Qt::Key_T, Qt::ControlModifier, true);
        send_key(menu, QEvent::KeyRelease, Qt::Key_T, Qt::ControlModifier,
                 true);
        EXPECT_TRUE(menu.isVisible());
        send_key(menu, QEvent::KeyRelease, Qt::Key_T);
      },
      chord);
  EXPECT_EQ(selected, actions.slot_actions[4]);
}

TEST_F(
    QtMarkingMenuTests,
    InitiatingMouseButtonReleaseIgnoresUnrelatedReleaseAndReturnsHoveredAction) {
  QtMarkingMenu menu;
  MarkingActions actions;
  int trigger_count = 0;
  QObject::connect(actions.slot_actions[2], &QAction::triggered, &menu,
                   [&]() { ++trigger_count; });
  const QuaderInputChord chord{
      .device = QuaderInputDevice::Mouse,
      .mouse_button = QuaderMouseButton::Middle,
  };
  QAction *selected = run_menu(
      menu, actions.slot_actions, {},
      [&]() {
        auto *button = slot_button(menu, 2);
        ASSERT_NE(button, nullptr);
        const QPoint local_position = button->rect().center();
        const QPoint global_position = button->mapToGlobal(local_position);
        move_pointer(*button, global_position);
        QMouseEvent unrelated_release(QEvent::MouseButtonRelease,
                                      QPointF(local_position),
                                      QPointF(global_position), Qt::RightButton,
                                      Qt::MiddleButton, Qt::NoModifier);
        QCoreApplication::sendEvent(button, &unrelated_release);
        EXPECT_TRUE(menu.isVisible());
        QMouseEvent initiating_release(
            QEvent::MouseButtonRelease, QPointF(local_position),
            QPointF(global_position), Qt::MiddleButton, Qt::NoButton,
            Qt::NoModifier);
        QCoreApplication::sendEvent(button, &initiating_release);
      },
      chord);
  EXPECT_EQ(selected, actions.slot_actions[2]);
  EXPECT_EQ(trigger_count, 0);
}

TEST_F(QtMarkingMenuTests, ReturningToCenterCancelsPreviouslyHoveredAction) {
  QtMarkingMenu menu;
  MarkingActions actions;
  QAction *selected = run_menu(menu, actions.slot_actions, {}, [&]() {
    move_pointer(menu, anchor_ + QPoint(2000, 0));
    move_pointer(menu, anchor_);
    send_key(menu, QEvent::KeyRelease, Qt::Key_Q);
  });
  EXPECT_EQ(selected, nullptr);
}

TEST_F(QtMarkingMenuTests, EscapeOutsideClickAndDeactivationCancelSelection) {
  QtMarkingMenu menu;
  MarkingActions actions;
  const std::array<QEvent::Type, 3> cancel_events{
      QEvent::KeyPress, QEvent::MouseButtonPress, QEvent::WindowDeactivate};
  for (const auto event_type : cancel_events) {
    SCOPED_TRACE(static_cast<int>(event_type));
    QAction *selected = run_menu(menu, actions.slot_actions, {}, [&]() {
      move_pointer(menu, anchor_ + QPoint(2000, 0));
      if (event_type == QEvent::KeyPress) {
        send_key(menu, QEvent::KeyPress, Qt::Key_Escape);
      } else if (event_type == QEvent::MouseButtonPress) {
        const QPoint global_position = menu.mapToGlobal(QPoint(-10, -10));
        QMouseEvent click(QEvent::MouseButtonPress, QPointF(-10, -10),
                          QPointF(global_position), Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&menu, &click);
      } else {
        QEvent deactivate(QEvent::WindowDeactivate);
        QCoreApplication::sendEvent(&menu, &deactivate);
      }
    });
    EXPECT_EQ(selected, nullptr);
  }
}

TEST_F(QtMarkingMenuTests, DisabledAndEmptySectorsCannotReturnAnAction) {
  QtMarkingMenu menu;
  MarkingActions actions;
  actions.slot_actions[0]->setEnabled(false);
  actions.slot_actions[2] = nullptr;
  for (const QPoint direction : {QPoint(0, -2000), QPoint(2000, 0)}) {
    QAction *selected = run_menu(menu, actions.slot_actions, {}, [&]() {
      auto *disabled_button = slot_button(menu, 0);
      ASSERT_NE(disabled_button, nullptr);
      EXPECT_FALSE(disabled_button->isEnabled());
      move_pointer(menu, anchor_ + direction);
      send_key(menu, QEvent::KeyRelease, Qt::Key_Q);
    });
    EXPECT_EQ(selected, nullptr);
  }
}

TEST_F(QtMarkingMenuTests,
       ArrowNavigationSkipsDisabledSlotsAndEnterOnlyReturnsAction) {
  QtMarkingMenu menu;
  MarkingActions actions;
  actions.slot_actions[1]->setEnabled(false);
  actions.slot_actions[3] = nullptr;
  int trigger_count = 0;
  for (auto &action : actions.owned) {
    QObject::connect(action.get(), &QAction::triggered, &menu,
                     [&]() { ++trigger_count; });
  }
  const std::array<int, 5> keys{Qt::Key_Right, Qt::Key_Down, Qt::Key_Right,
                                Qt::Key_Up, Qt::Key_Left};
  const std::array<std::size_t, 5> expected_slots{0U, 2U, 4U, 2U, 0U};
  for (std::size_t last_key = 0; last_key < keys.size(); ++last_key) {
    SCOPED_TRACE(last_key);
    QAction *selected = run_menu(menu, actions.slot_actions, {}, [&]() {
      for (std::size_t index = 0; index <= last_key; ++index) {
        send_key(menu, QEvent::KeyPress, keys[index]);
      }
      send_key(menu, QEvent::KeyPress, Qt::Key_Return);
    });
    EXPECT_EQ(selected, actions.slot_actions[expected_slots[last_key]]);
  }
  EXPECT_EQ(trigger_count, 0);
}

TEST_F(QtMarkingMenuTests,
       OverflowIsAlwaysVisibleBelowTheRingAndCenterReleaseCancels) {
  QtMarkingMenu menu;
  MarkingActions actions;
  QAction overflow_action(QStringLiteral("Overflow operation"));
  QAction *selected =
      run_menu(menu, actions.slot_actions, {&overflow_action}, [&]() {
        auto *list = overflow_list(menu);
        ASSERT_NE(list, nullptr);
        ASSERT_TRUE(list->isVisible());
        EXPECT_EQ(list->window(), &menu);
        EXPECT_TRUE(menu.isAncestorOf(list));
        EXPECT_TRUE(menu.rect().contains(list->geometry()));
        EXPECT_EQ(list->count(), 1);
        EXPECT_EQ(list->currentRow(), -1);
        EXPECT_EQ(list->item(0)->text(), overflow_action.text());
        for (int slot = 0; slot < 8; ++slot) {
          auto *button = slot_button(menu, slot);
          ASSERT_NE(button, nullptr);
          EXPECT_EQ(button->accessibleName(),
                    actions.slot_actions[slot]->text());
          EXPECT_LT(button->geometry().bottom(), list->geometry().top());
        }
        move_pointer(menu, anchor_);
        send_key(menu, QEvent::KeyRelease, Qt::Key_Q);
      });
  EXPECT_EQ(selected, nullptr);
}

TEST_F(QtMarkingMenuTests,
       NorthwestActionKeepsItsLabelAndSelectionWhenOverflowExists) {
  QtMarkingMenu menu;
  MarkingActions actions;
  QAction overflow_action(QStringLiteral("Overflow operation"));
  const QString label = QStringLiteral("Operazione nordovest");
  actions.slot_actions[7]->setText(label);
  QAction *selected =
      run_menu(menu, actions.slot_actions, {&overflow_action}, [&]() {
        auto *button = slot_button(menu, 7);
        auto *list = overflow_list(menu);
        ASSERT_NE(button, nullptr);
        ASSERT_NE(list, nullptr);
        EXPECT_EQ(button->accessibleName(), label);
        EXPECT_EQ(button->text(), label);
        move_pointer(*button, button->mapToGlobal(button->rect().center()));
        EXPECT_TRUE(list->isVisible());
        EXPECT_EQ(list->currentRow(), -1);
        send_key(menu, QEvent::KeyRelease, Qt::Key_Q);
      });
  EXPECT_EQ(selected, actions.slot_actions[7]);
}

TEST_F(QtMarkingMenuTests, OverflowHoverReturnsEnabledRowWithoutTriggering) {
  QtMarkingMenu menu;
  MarkingActions actions;
  QAction disabled(QStringLiteral("Unavailable operation"));
  QAction separator;
  QAction available(QStringLiteral("Available operation"));
  QAction hidden(QStringLiteral("Hidden operation"));
  disabled.setEnabled(false);
  separator.setSeparator(true);
  hidden.setVisible(false);
  int trigger_count = 0;
  QObject::connect(&available, &QAction::triggered, &menu,
                   [&]() { ++trigger_count; });
  for (int row = 0; row < 3; ++row) {
    SCOPED_TRACE(row);
    QAction *selected = run_menu(
        menu, actions.slot_actions,
        {&disabled, nullptr, &separator, &hidden, &available}, [&]() {
          auto *list = overflow_list(menu);
          ASSERT_NE(list, nullptr);
          ASSERT_TRUE(list->isVisible());
          ASSERT_EQ(list->count(), 3);
          EXPECT_FALSE(list->item(0)->flags().testFlag(Qt::ItemIsEnabled));
          EXPECT_FALSE(list->item(1)->flags().testFlag(Qt::ItemIsEnabled));
          EXPECT_FALSE(list->item(1)->flags().testFlag(Qt::ItemIsSelectable));
          EXPECT_LT(list->visualItemRect(list->item(1)).height(),
                    list->visualItemRect(list->item(2)).height());
          EXPECT_EQ(list->item(2)->text(), available.text());
          const QRect row_rect = list->visualItemRect(list->item(row));
          ASSERT_FALSE(row_rect.isEmpty());
          move_pointer(*list->viewport(),
                       list->viewport()->mapToGlobal(row_rect.center()));
          EXPECT_EQ(list->currentRow(), row == 2 ? 2 : -1);
          send_key(*list, QEvent::KeyRelease, Qt::Key_Q);
        });
    EXPECT_EQ(selected, row == 2 ? &available : nullptr);
  }
  EXPECT_EQ(trigger_count, 0);
}

TEST_F(QtMarkingMenuTests,
       KeyboardEntersPermanentListAndSkipsDisabledRowsAndSeparators) {
  QtMarkingMenu menu;
  MarkingActions actions;
  QAction disabled(QStringLiteral("Unavailable operation"));
  QAction first(QStringLiteral("First available operation"));
  QAction separator;
  QAction last(QStringLiteral("Last available operation"));
  disabled.setEnabled(false);
  separator.setSeparator(true);
  int trigger_count = 0;
  for (QAction *action : {&first, &last}) {
    QObject::connect(action, &QAction::triggered, &menu,
                     [&]() { ++trigger_count; });
  }
  for (const int direction : {Qt::Key_Down, Qt::Key_Up}) {
    const int expected_row = direction == Qt::Key_Down ? 1 : 3;
    QAction *selected =
        run_menu(menu, actions.slot_actions,
                 {&disabled, &first, &separator, &last}, [&]() {
                   auto *list = overflow_list(menu);
                   ASSERT_NE(list, nullptr);
                   ASSERT_TRUE(list->isVisible());
                   send_key(menu, QEvent::KeyPress, direction);
                   EXPECT_EQ(list->currentRow(), expected_row);
                   send_key(*list, QEvent::KeyPress, Qt::Key_Return);
                 });
    EXPECT_EQ(selected, direction == Qt::Key_Down ? &first : &last);
  }
  QAction *selected =
      run_menu(menu, actions.slot_actions,
               {&disabled, &first, &separator, &last}, [&]() {
                 auto *list = overflow_list(menu);
                 ASSERT_NE(list, nullptr);
                 for (int step = 0; step < 9; ++step) {
                   send_key(menu, QEvent::KeyPress, Qt::Key_Right);
                 }
                 EXPECT_EQ(list->currentRow(), 1);
                 send_key(*list, QEvent::KeyPress, Qt::Key_Down);
                 EXPECT_EQ(list->currentRow(), 3);
                 send_key(*list, QEvent::KeyPress, Qt::Key_Down);
                 EXPECT_EQ(list->currentRow(), -1);
                 ASSERT_NE(slot_button(menu, 0), nullptr);
                 EXPECT_TRUE(slot_button(menu, 0)->isChecked());
                 send_key(menu, QEvent::KeyPress, Qt::Key_Up);
                 EXPECT_EQ(list->currentRow(), 3);
                 send_key(*list, QEvent::KeyPress, Qt::Key_Up);
                 EXPECT_EQ(list->currentRow(), 1);
                 send_key(*list, QEvent::KeyRelease, Qt::Key_Q);
               });
  EXPECT_EQ(selected, &first);
  EXPECT_EQ(trigger_count, 0);

  first.setEnabled(false);
  last.setEnabled(false);
  selected = run_menu(menu, actions.slot_actions,
                      {&disabled, &first, &separator, &last}, [&]() {
                        auto *list = overflow_list(menu);
                        ASSERT_NE(list, nullptr);
                        ASSERT_TRUE(list->isVisible());
                        send_key(menu, QEvent::KeyPress, Qt::Key_Down);
                        EXPECT_EQ(list->currentRow(), -1);
                        send_key(menu, QEvent::KeyRelease, Qt::Key_Q);
                      });
  EXPECT_EQ(selected, nullptr);
  EXPECT_EQ(trigger_count, 0);
}

TEST_F(QtMarkingMenuTests,
       OverflowMinimumWidthScalesAndLongNativeContentCanExpandIt) {
  QtMarkingMenu menu;
  MarkingActions actions;
  QAction supplemental(QStringLiteral("Cut"));
  std::array<int, 3> widths{};
  const std::array<float, 3> factors{1.0F, 2.0F, 1.0F};
  const int available_width =
      QApplication::primaryScreen()->availableGeometry().width();
  for (std::size_t index = 0; index < factors.size(); ++index) {
    const QtUiScaleMetrics metrics(factors[index], index + 1U);
    menu.apply_ui_scale(metrics);
    EXPECT_EQ(run_menu(menu, actions.slot_actions, {&supplemental},
                       [&]() {
                         auto *list = overflow_list(menu);
                         ASSERT_NE(list, nullptr);
                         widths[index] = list->width();
                         EXPECT_GE(list->width(),
                                   std::min(metrics.px(180), available_width));
                         send_key(menu, QEvent::KeyPress, Qt::Key_Escape);
                       }),
              nullptr);
  }
  EXPECT_GT(widths[1], widths[0]);
  EXPECT_EQ(widths[2], widths[0]);
  supplemental.setText(QStringLiteral(
      "Apply the selected modeling operation to the current object selection"));
  EXPECT_EQ(
      run_menu(menu, actions.slot_actions, {&supplemental},
               [&]() {
                 auto *list = overflow_list(menu);
                 ASSERT_NE(list, nullptr);
                 EXPECT_GT(list->width(), widths[0]);
                 EXPECT_GT(list->width(), std::min(300, available_width - 1));
                 EXPECT_LE(list->width(), available_width);
                 EXPECT_EQ(list->item(0)->text(), supplemental.text());
                 capture_menu(
                     menu, QStringLiteral("marking-menu-overflow-width.png"));
                 send_key(menu, QEvent::KeyPress, Qt::Key_Escape);
               }),
      nullptr);
}

TEST_F(QtMarkingMenuTests,
       ReturningFromOverflowToRadialButtonClearsListSelection) {
  QtMarkingMenu menu;
  MarkingActions actions;
  QAction available(QStringLiteral("Available operation"));
  QAction *selected = run_menu(menu, actions.slot_actions, {&available}, [&]() {
    auto *button = slot_button(menu, 2);
    auto *list = overflow_list(menu);
    ASSERT_NE(button, nullptr);
    ASSERT_NE(list, nullptr);
    ASSERT_TRUE(list->isVisible());
    ASSERT_EQ(list->count(), 1);
    const QRect row_rect = list->visualItemRect(list->item(0));
    ASSERT_FALSE(row_rect.isEmpty());
    move_pointer(*list->viewport(),
                 list->viewport()->mapToGlobal(row_rect.center()));
    EXPECT_EQ(list->currentRow(), 0);
    move_pointer(menu, button->mapToGlobal(button->rect().center()));
    EXPECT_EQ(list->currentRow(), -1);
    EXPECT_TRUE(list->isVisible());
    send_key(menu, QEvent::KeyRelease, Qt::Key_Q);
  });
  EXPECT_EQ(selected, actions.slot_actions[2]);
}

TEST_F(QtMarkingMenuTests,
       IndependentLabelWidthsKeepCircularCentersAndLivePointerHairline) {
  QtMarkingMenu menu;
  MarkingActions actions;
  actions.slot_actions[0]->setText(QStringLiteral("Cut"));
  actions.slot_actions[2]->setText(QStringLiteral("Connect selected vertices"));
  const std::array<QPoint, 8> directions{
      QPoint(0, -1), QPoint(1, -1), QPoint(1, 0),  QPoint(1, 1),
      QPoint(0, 1),  QPoint(-1, 1), QPoint(-1, 0), QPoint(-1, -1)};
  QAction *selected = run_menu(menu, actions.slot_actions, {}, [&]() {
    auto *short_button = slot_button(menu, 0);
    auto *long_button = slot_button(menu, 2);
    ASSERT_NE(short_button, nullptr);
    ASSERT_NE(long_button, nullptr);
    EXPECT_LT(short_button->width(), long_button->width());
    const QPoint north =
        short_button->mapToGlobal(short_button->rect().center());
    const double radius =
        std::hypot(north.x() - anchor_.x(), north.y() - anchor_.y());
    EXPECT_GT(radius, 0.0);
    for (std::size_t slot = 0; slot < directions.size(); ++slot) {
      auto *button = slot_button(menu, static_cast<int>(slot));
      ASSERT_NE(button, nullptr);
      const QPoint center = button->mapToGlobal(button->rect().center());
      const QPoint delta = center - anchor_;
      const double length =
          std::hypot(directions[slot].x(), directions[slot].y());
      EXPECT_NEAR(std::hypot(delta.x(), delta.y()), radius, 2.0);
      EXPECT_NEAR(delta.x(), directions[slot].x() * radius / length, 2.0);
      EXPECT_NEAR(delta.y(), directions[slot].y() * radius / length, 2.0);
    }

    move_pointer(menu, anchor_);
    QImage before(menu.size(), QImage::Format_ARGB32_Premultiplied);
    before.fill(Qt::transparent);
    menu.render(&before);
    const QPoint pointer = anchor_ + QPoint(36, -12);
    move_pointer(menu, pointer);
    QImage after(menu.size(), QImage::Format_ARGB32_Premultiplied);
    after.fill(Qt::transparent);
    menu.render(&after);
    const QPoint middle = menu.mapFromGlobal(anchor_ + QPoint(18, -6));
    ASSERT_TRUE(after.rect().contains(middle));
    int added_opaque_pixels = 0;
    for (int offset_y = -2; offset_y <= 2; ++offset_y) {
      for (int offset_x = -2; offset_x <= 2; ++offset_x) {
        const QPoint sample = middle + QPoint(offset_x, offset_y);
        ASSERT_TRUE(after.rect().contains(sample));
        if (qAlpha(after.pixel(sample)) > qAlpha(before.pixel(sample))) {
          ++added_opaque_pixels;
        }
      }
    }
    EXPECT_GT(added_opaque_pixels, 0);
    for (const QPoint corner :
         {after.rect().topLeft(), after.rect().topRight(),
          after.rect().bottomLeft(), after.rect().bottomRight()}) {
      EXPECT_EQ(qAlpha(after.pixel(corner)), 0);
    }
    capture_menu(menu, QStringLiteral("marking-menu-circle-pointer.png"));
    move_pointer(menu, anchor_);
    send_key(menu, QEvent::KeyRelease, Qt::Key_Q);
  });
  EXPECT_EQ(selected, nullptr);
}

TEST_F(QtMarkingMenuTests, GuideLineAndCenterRingKeepTextColorAfterSelection) {
  QtMarkingMenu menu;
  MarkingActions actions;
  QPalette palette = menu.palette();
  const QColor guide_color(225, 226, 229);
  for (const auto group : {QPalette::Active, QPalette::Inactive}) {
    palette.setColor(group, QPalette::Text, guide_color);
    palette.setColor(group, QPalette::WindowText, QColor(89, 95, 103));
    palette.setColor(group, QPalette::Accent, QColor(154, 100, 216));
  }
  menu.setPalette(palette);
  EXPECT_EQ(
      run_menu(
          menu, actions.slot_actions, {},
          [&]() {
            const QPoint pointer = anchor_ + QPoint(0, -16);
            move_pointer(menu, pointer);
            auto *button = slot_button(menu, 0);
            ASSERT_NE(button, nullptr);
            EXPECT_FALSE(button->isChecked());

            const auto render_menu = [&]() {
              QImage image(menu.size(), QImage::Format_ARGB32_Premultiplied);
              image.fill(Qt::transparent);
              menu.render(&image);
              return image;
            };
            const QImage before = render_menu();
            send_key(menu, QEvent::KeyPress, Qt::Key_Right);
            EXPECT_TRUE(button->isChecked());
            const QImage after = render_menu();
            ASSERT_EQ(before.size(), after.size());

            for (const QPoint sample_center :
                 {anchor_ + QPoint(0, -9), anchor_ + QPoint(4, 0)}) {
              const QPoint local_center = menu.mapFromGlobal(sample_center);
              int visible_samples = 0;
              for (int offset_y = -1; offset_y <= 1; ++offset_y) {
                for (int offset_x = -1; offset_x <= 1; ++offset_x) {
                  const QPoint sample =
                      local_center + QPoint(offset_x, offset_y);
                  ASSERT_TRUE(before.rect().contains(sample));
                  const QColor before_color = before.pixelColor(sample);
                  const QColor after_color = after.pixelColor(sample);
                  EXPECT_EQ(before_color, after_color);
                  if (before_color.alpha() < 64) {
                    continue;
                  }
                  ++visible_samples;
                  EXPECT_NEAR(before_color.red(), guide_color.red(), 3);
                  EXPECT_NEAR(before_color.green(), guide_color.green(), 3);
                  EXPECT_NEAR(before_color.blue(), guide_color.blue(), 3);
                }
              }
              EXPECT_GT(visible_samples, 0);
            }
            capture_menu(menu,
                         QStringLiteral("marking-menu-guide-selected.png"));
            send_key(menu, QEvent::KeyPress, Qt::Key_Escape);
          }),
      nullptr);
}

TEST_F(QtMarkingMenuTests, UiScaleRoundTripRestoresRetainedButtonGeometry) {
  QtMarkingMenu menu;
  MarkingActions actions;
  std::array<QSize, 3> button_sizes;
  std::array<QSize, 3> icon_sizes;
  std::array<QToolButton *, 3> buttons{};
  const std::array<float, 3> factors{1.0F, 2.0F, 1.0F};
  for (std::size_t index = 0; index < factors.size(); ++index) {
    menu.apply_ui_scale(QtUiScaleMetrics(factors[index], index + 1U));
    EXPECT_EQ(run_menu(menu, actions.slot_actions, {},
                       [&]() {
                         auto *button = slot_button(menu, 0);
                         ASSERT_NE(button, nullptr);
                         buttons[index] = button;
                         button_sizes[index] = button->size();
                         icon_sizes[index] = button->iconSize();
                         for (int slot = 0; slot < 8; ++slot) {
                           auto *visible_button = slot_button(menu, slot);
                           ASSERT_NE(visible_button, nullptr);
                           EXPECT_TRUE(visible_button->isVisible());
                           EXPECT_TRUE(menu.rect().contains(
                               visible_button->geometry()));
                         }
                         capture_menu(
                             menu, QStringLiteral("marking-menu-scale-%1.png")
                                       .arg(static_cast<int>(index)));
                         send_key(menu, QEvent::KeyPress, Qt::Key_Escape);
                       }),
              nullptr);
  }
  EXPECT_EQ(buttons[0], buttons[1]);
  EXPECT_EQ(buttons[0], buttons[2]);
  EXPECT_GT(button_sizes[1].width(), button_sizes[0].width());
  EXPECT_GT(button_sizes[1].height(), button_sizes[0].height());
  EXPECT_EQ(button_sizes[2], button_sizes[0]);
  EXPECT_GT(icon_sizes[1].width(), icon_sizes[0].width());
  EXPECT_EQ(icon_sizes[2], icon_sizes[0]);
}

TEST_F(QtMarkingMenuTests,
       LongLabelsFromNarrowOwnerRemainReachableAtScreenEdge) {
  QWidget owner;
  owner.setAttribute(Qt::WA_DontShowOnScreen, true);
  const QRect available = QApplication::primaryScreen()->availableGeometry();
  owner.setGeometry(available.right() - 239, available.bottom() - 119, 240,
                    120);
  owner.show();
  anchor_ = owner.mapToGlobal(owner.rect().bottomRight());
  QtMarkingMenu menu(&owner);
  MarkingActions actions;
  const QString long_label = QStringLiteral(
      "Apply the selected modeling operation to the current object selection");
  actions.slot_actions[2]->setText(long_label);
  const QString title =
      QStringLiteral("Long localized modeling operations title");
  for (const float factor : {1.0F, 2.0F}) {
    menu.apply_ui_scale(QtUiScaleMetrics(factor));
    QAction *selected = run_menu(
        menu, actions.slot_actions, {},
        [&]() {
          auto *label =
              menu.findChild<QLabel *>(QStringLiteral("markingMenu.title"));
          auto *button = slot_button(menu, 2);
          ASSERT_NE(label, nullptr);
          ASSERT_NE(button, nullptr);
          EXPECT_EQ(label->text(), title);
          EXPECT_EQ(button->accessibleName(), long_label);
          EXPECT_FALSE(button->text().isEmpty());
          EXPECT_TRUE(available.contains(menu.frameGeometry()));
          EXPECT_TRUE(menu.rect().contains(button->geometry()));
          EXPECT_GE(button->height(), button->fontMetrics().height());
          capture_menu(menu, QStringLiteral("marking-menu-long-label-%1.png")
                                 .arg(static_cast<int>(factor * 100.0F)));
          move_pointer(*button, button->mapToGlobal(button->rect().center()));
          send_key(*button, QEvent::KeyRelease, Qt::Key_Q);
        },
        opening_chord(), title);
    EXPECT_EQ(selected, actions.slot_actions[2]);
  }
}

TEST_F(QtMarkingMenuTests,
       BottomEdgeKeepsEntireSupplementalListBelowTheTranslatedRadialRing) {
  const QRect available = QApplication::primaryScreen()->availableGeometry();
  QWidget owner;
  owner.setAttribute(Qt::WA_DontShowOnScreen, true);
  owner.setGeometry(available.center().x() - 60, available.bottom() - 79, 120,
                    80);
  owner.show();
  QtMarkingMenu menu(&owner);
  MarkingActions actions;
  MarkingActions supplemental;
  const std::vector<QAction *> overflow(supplemental.slot_actions.begin(),
                                        supplemental.slot_actions.begin() + 6);
  for (const float factor : {1.0F, 2.0F}) {
    SCOPED_TRACE(factor);
    const QtUiScaleMetrics metrics(factor);
    menu.apply_ui_scale(metrics);
    anchor_ = available.center();
    std::array<QPoint, 8> original_centers;
    QRect original_ring_bounds;
    EXPECT_EQ(run_menu(menu, actions.slot_actions, overflow,
                       [&]() {
                         for (int slot = 0; slot < 8; ++slot) {
                           auto *button = slot_button(menu, slot);
                           ASSERT_NE(button, nullptr);
                           original_centers[slot] =
                               button->mapToGlobal(button->rect().center());
                           original_ring_bounds |= QRect(
                               button->mapToGlobal(QPoint()), button->size());
                         }
                         send_key(menu, QEvent::KeyPress, Qt::Key_Escape);
                       }),
              nullptr);

    anchor_ = QPoint(available.center().x(), available.bottom() - 8);
    for (const bool send_initial_move : {false, true}) {
      SCOPED_TRACE(send_initial_move);
      EXPECT_EQ(
          run_menu(
              menu, actions.slot_actions, overflow,
              [&]() {
                auto *list = overflow_list(menu);
                ASSERT_NE(list, nullptr);
                ASSERT_TRUE(list->isVisible());
                ASSERT_EQ(list->count(), 6);
                EXPECT_EQ(menu.geometry(), available);
                const QRect list_bounds(list->mapToGlobal(QPoint()),
                                        list->size());
                EXPECT_TRUE(available.contains(list_bounds));
                EXPECT_GT(list->height(), owner.height());
                for (int row = 0; row < list->count(); ++row) {
                  const QRect row_rect = list->visualItemRect(list->item(row));
                  EXPECT_FALSE(row_rect.isEmpty());
                  EXPECT_TRUE(list->viewport()->rect().contains(row_rect));
                }

                EXPECT_FALSE(list->horizontalScrollBar()->isVisible());
                EXPECT_FALSE(list->verticalScrollBar()->isVisible());
                EXPECT_EQ(list->horizontalScrollBar()->maximum(), 0);
                EXPECT_EQ(list->verticalScrollBar()->maximum(), 0);
                const bool fits_below = original_ring_bounds.height() +
                                            list->height() + metrics.px(16) <=
                                        available.height();
                std::array<QPoint, 8> shifted_centers;
                for (int slot = 0; slot < 8; ++slot) {
                  auto *button = slot_button(menu, slot);
                  ASSERT_NE(button, nullptr);
                  ASSERT_TRUE(button->isVisible());
                  shifted_centers[slot] =
                      button->mapToGlobal(button->rect().center());
                  const QRect button_bounds(button->mapToGlobal(QPoint()),
                                            button->size());
                  EXPECT_TRUE(available.contains(button_bounds));
                  if (fits_below) {
                    EXPECT_LT(button_bounds.bottom(), list_bounds.top());
                  } else {
                    EXPECT_FALSE(button_bounds.intersects(list_bounds));
                  }
                  EXPECT_EQ(shifted_centers[slot] - shifted_centers[0],
                            original_centers[slot] - original_centers[0]);
                }
                const QPoint ring_center =
                    (shifted_centers[0] + shifted_centers[4]) / 2;
                if (fits_below &&
                    original_ring_bounds.width() <= available.width()) {
                  EXPECT_EQ(ring_center.x(), anchor_.x());
                }
                EXPECT_LT(ring_center.y(), anchor_.y());
                if (send_initial_move) {
                  move_pointer(menu, anchor_);
                }
                EXPECT_EQ(menu.geometry(), available);
                EXPECT_EQ(list->currentRow(), -1);
                for (int slot = 0; slot < 8; ++slot) {
                  EXPECT_FALSE(slot_button(menu, slot)->isChecked());
                }
                capture_menu(
                    menu,
                    QStringLiteral("marking-menu-bottom-edge-full-list-%1.png")
                        .arg(static_cast<int>(factor * 100.0F)));
                send_key(menu, QEvent::KeyRelease, Qt::Key_Q);
              }),
          nullptr);
    }
  }
}

TEST_F(QtMarkingMenuTests,
       ShiftedRadialAndSupplementalTargetsAcceptActualDragsAndDirectClicks) {
  const QRect available = QApplication::primaryScreen()->availableGeometry();
  anchor_ = QPoint(available.center().x(), available.bottom() - 8);
  QtMarkingMenu menu;
  MarkingActions actions;
  MarkingActions supplemental;
  const std::vector<QAction *> overflow(supplemental.slot_actions.begin(),
                                        supplemental.slot_actions.begin() + 6);
  for (const float factor : {1.0F, 2.0F}) {
    SCOPED_TRACE(factor);
    menu.apply_ui_scale(QtUiScaleMetrics(factor));
    for (const bool select_supplemental : {false, true}) {
      SCOPED_TRACE(select_supplemental);
      for (const bool click_target : {false, true}) {
        SCOPED_TRACE(click_target);
        QAction *selected =
            run_menu(menu, actions.slot_actions, overflow, [&]() {
              QWidget *receiver = nullptr;
              QPoint global_position;
              if (select_supplemental) {
                auto *list = overflow_list(menu);
                ASSERT_NE(list, nullptr);
                ASSERT_EQ(list->count(), 6);
                const QRect row_rect = list->visualItemRect(list->item(5));
                ASSERT_TRUE(list->viewport()->rect().contains(row_rect));
                receiver = list->viewport();
                global_position = receiver->mapToGlobal(row_rect.center());
              } else {
                auto *button = slot_button(menu, 2);
                ASSERT_NE(button, nullptr);
                receiver = button;
                global_position = button->mapToGlobal(button->rect().center());
              }
              ASSERT_NE(global_position, anchor_);
              if (click_target) {
                const QPointF local_position(
                    receiver->mapFromGlobal(global_position));
                QMouseEvent press(QEvent::MouseButtonPress, local_position,
                                  QPointF(global_position), Qt::LeftButton,
                                  Qt::LeftButton, Qt::NoModifier);
                QCoreApplication::sendEvent(receiver, &press);
                EXPECT_TRUE(menu.isVisible());
                EXPECT_EQ(menu.geometry(), available);
                QMouseEvent release(QEvent::MouseButtonRelease, local_position,
                                    QPointF(global_position), Qt::LeftButton,
                                    Qt::NoButton, Qt::NoModifier);
                QCoreApplication::sendEvent(receiver, &release);
              } else {
                move_pointer(*receiver, global_position);
                EXPECT_EQ(menu.geometry(), available);
                send_key(*receiver, QEvent::KeyRelease, Qt::Key_Q);
              }
            });
        EXPECT_EQ(selected,
                  select_supplemental ? overflow[5] : actions.slot_actions[2]);
      }
    }
  }
}

TEST_F(
    QtMarkingMenuTests,
    LightPaletteAndRightToLeftLayoutRetainAccessibleLabelsAndOverflowBounds) {
  QtMarkingMenu menu;
  MarkingActions actions;
  const QPalette light_palette = QApplication::style()->standardPalette();
  menu.setPalette(light_palette);
  menu.setLayoutDirection(Qt::RightToLeft);
  for (std::size_t index = 0; index < actions.slot_actions.size(); ++index) {
    actions.slot_actions[index]->setText(
        QStringLiteral("Localized operation %1 with a long accessible label")
            .arg(static_cast<int>(index)));
  }
  QAction overflow_action(QStringLiteral(
      "Localized overflow operation with a long accessible label"));
  const QRect available = QApplication::primaryScreen()->availableGeometry();
  for (const float factor : {1.0F, 2.0F}) {
    menu.apply_ui_scale(QtUiScaleMetrics(factor));
    QAction *selected =
        run_menu(menu, actions.slot_actions, {&overflow_action}, [&]() {
          EXPECT_EQ(menu.layoutDirection(), Qt::RightToLeft);
          EXPECT_EQ(menu.palette().color(QPalette::Window),
                    light_palette.color(QPalette::Window));
          for (int slot = 0; slot < 8; ++slot) {
            auto *button = slot_button(menu, slot);
            ASSERT_NE(button, nullptr);
            EXPECT_EQ(button->layoutDirection(), Qt::RightToLeft);
            EXPECT_EQ(button->accessibleName(),
                      actions.slot_actions[slot]->text());
            EXPECT_FALSE(button->text().isEmpty());
            EXPECT_TRUE(menu.rect().contains(button->geometry()));
          }
          auto *list = overflow_list(menu);
          ASSERT_NE(list, nullptr);
          ASSERT_TRUE(list->isVisible());
          ASSERT_EQ(list->count(), 1);
          EXPECT_EQ(list->layoutDirection(), Qt::RightToLeft);
          EXPECT_EQ(list->item(0)->text(), overflow_action.text());
          EXPECT_EQ(list->window(), &menu);
          EXPECT_TRUE(menu.rect().contains(list->geometry()));
          EXPECT_TRUE(available.contains(menu.frameGeometry()));
          capture_menu(menu, QStringLiteral("marking-menu-light-rtl-%1.png")
                                 .arg(static_cast<int>(factor * 100.0F)));
          send_key(menu, QEvent::KeyPress, Qt::Key_Escape);
        });
    EXPECT_EQ(selected, nullptr);
  }
}

} // namespace
} // namespace quader
