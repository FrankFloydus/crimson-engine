////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_retained_message_box.hpp"
#include "ui/qt_application_theme.hpp"

#include <gtest/gtest.h>

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QLabel>
#include <QLayout>
#include <QMouseEvent>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QProxyStyle>
#include <QPushButton>
#include <QScopeGuard>
#include <QScreen>
#include <QScrollBar>
#include <QStringList>
#include <QTextBlock>
#include <QTextCursor>
#include <QTimer>
#include <QWidget>
#include <qpa/qwindowsysteminterface.h>

using namespace quader;

namespace {

class PaletteMessageIconStyle final : public QProxyStyle {
public:
  PaletteMessageIconStyle() : QProxyStyle(QStringLiteral("fusion")) {}

  QIcon standardIcon(StandardPixmap standard_pixmap,
                     const QStyleOption *option,
                     const QWidget *widget) const override {
    switch (standard_pixmap) {
    case SP_MessageBoxCritical:
    case SP_MessageBoxWarning:
    case SP_MessageBoxQuestion:
    case SP_MessageBoxInformation: {
      QPixmap pixmap(24, 24);
      pixmap.fill(QApplication::palette().color(QPalette::Text));
      return QIcon(pixmap);
    }
    default:
      return QProxyStyle::standardIcon(standard_pixmap, option, widget);
    }
  }
};

} // namespace

TEST(QtRetainedMessageBoxTests,
     ReusesNativeWindowLayoutAndButtonsAcrossPresentations) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  const bool native_siblings_disabled = QCoreApplication::testAttribute(
      Qt::AA_DontCreateNativeWidgetSiblings);
  const auto restore_native_siblings = qScopeGuard([native_siblings_disabled]() {
    QCoreApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings,
                                   native_siblings_disabled);
  });
  QCoreApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings);
  int argc = 1;
  char application_name[] = "quader-retained-message-box-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  application.setStyle(QStringLiteral("Fusion"));
  QtUiScaleApplicationController scale_controller(application);

  QWidget owner;
  owner.resize(640, 480);
  QWidget sibling(&owner);
  sibling.setGeometry(10, 10, 120, 80);
  owner.show();
  QCoreApplication::processEvents();
  ASSERT_NE(owner.internalWinId(), WId{});
  ASSERT_EQ(sibling.internalWinId(), WId{});

  QtRetainedMessageBox message_box(&owner);
  QPushButton *const primary = message_box.retained_button(
      QtRetainedMessageBoxButton::Primary);
  QPushButton *const secondary = message_box.retained_button(
      QtRetainedMessageBoxButton::Secondary);
  QPushButton *const tertiary = message_box.retained_button(
      QtRetainedMessageBoxButton::Tertiary);
  ASSERT_NE(primary, nullptr);
  ASSERT_NE(secondary, nullptr);
  ASSERT_NE(tertiary, nullptr);
  const QtRetainedMessageBoxPresentation first{
      .icon = QMessageBox::Critical,
      .object_name = QStringLiteral("first_message_box"),
      .title = QStringLiteral("First title"),
      .text = QStringLiteral("First text"),
      .informative_text = QStringLiteral("First details"),
      .buttons =
          {
              {
                  .button = QtRetainedMessageBoxButton::Primary,
                  .object_name = QStringLiteral("first.primary"),
                  .label = QStringLiteral("Proceed"),
                  .role = QMessageBox::AcceptRole,
              },
              {
                  .button = QtRetainedMessageBoxButton::Secondary,
                  .object_name = QStringLiteral("first.secondary"),
                  .label = QStringLiteral("Remove"),
                  .role = QMessageBox::DestructiveRole,
              },
              {
                  .button = QtRetainedMessageBoxButton::Tertiary,
                  .object_name = QStringLiteral("first.tertiary"),
                  .label = QStringLiteral("Cancel"),
                  .role = QMessageBox::RejectRole,
              },
          },
      .default_button = QtRetainedMessageBoxButton::Primary,
      .escape_button = QtRetainedMessageBoxButton::Tertiary,
      .close_button = QtRetainedMessageBoxButton::Tertiary,
  };
  ASSERT_TRUE(message_box.set_presentation(first));
  message_box.prewarm();
  EXPECT_FALSE(message_box.isVisible());
  EXPECT_TRUE(message_box.testAttribute(Qt::WA_WState_Created));
  QLayout *const retained_layout = message_box.layout();
  ASSERT_NE(retained_layout, nullptr);
  const WId retained_window_id = message_box.internalWinId();
  ASSERT_NE(retained_window_id, WId{});
  const auto expect_native_isolation = [&]() {
    EXPECT_EQ(message_box.internalWinId(), retained_window_id);
    EXPECT_EQ(sibling.internalWinId(), WId{});
  };
  expect_native_isolation();
  auto *const icon_label = message_box.findChild<QLabel *>(
      QStringLiteral("qt_msgboxex_icon_label"));
  ASSERT_NE(icon_label, nullptr);
  ASSERT_FALSE(icon_label->pixmap().isNull());
  const QSizeF unscaled_icon_size =
      icon_label->pixmap().deviceIndependentSize();
  ASSERT_TRUE(scale_controller.apply_factor(2.0F));
  message_box.apply_ui_scale(scale_controller.metrics());
  expect_native_isolation();
  EXPECT_EQ(message_box.layout(), retained_layout);
  EXPECT_GT(icon_label->pixmap().deviceIndependentSize().width(),
            unscaled_icon_size.width());
  const qint64 scaled_icon_key = icon_label->pixmap().cacheKey();
  EXPECT_FALSE(scale_controller.apply_factor(2.0F));
  message_box.apply_ui_scale(scale_controller.metrics());
  EXPECT_EQ(icon_label->pixmap().cacheKey(), scaled_icon_key)
      << "one scale revision must not churn retained standard icons";
  ASSERT_TRUE(scale_controller.apply_factor(1.0F));
  message_box.apply_ui_scale(scale_controller.metrics());
  EXPECT_EQ(icon_label->pixmap().deviceIndependentSize(), unscaled_icon_size);
  expect_native_isolation();

  QTimer::singleShot(0, &message_box, [&]() {
    expect_native_isolation();
    EXPECT_EQ(message_box.objectName(), first.object_name);
    EXPECT_EQ(message_box.windowTitle(), first.title);
    EXPECT_EQ(message_box.text(), first.text);
    EXPECT_EQ(message_box.informativeText(), first.informative_text);
    EXPECT_EQ(message_box.presented_icon(), QMessageBox::Critical);
    EXPECT_EQ(message_box.defaultButton(), primary);
    EXPECT_EQ(message_box.escapeButton(), tertiary);
    EXPECT_EQ(message_box.buttonRole(primary), QMessageBox::AcceptRole);
    EXPECT_EQ(message_box.buttonRole(secondary),
              QMessageBox::DestructiveRole);
    EXPECT_EQ(message_box.buttonRole(tertiary), QMessageBox::RejectRole);
    secondary->click();
  });
  EXPECT_EQ(message_box.present(first),
            QtRetainedMessageBoxButton::Secondary);
  EXPECT_TRUE(message_box.testAttribute(Qt::WA_WState_Created));
  expect_native_isolation();

  const QtRetainedMessageBoxPresentation second{
      .icon = QMessageBox::Question,
      .object_name = QStringLiteral("second_message_box"),
      .title = QStringLiteral("Second title"),
      .text = QStringLiteral("Second text"),
      .buttons =
          {
              {
                  .button = QtRetainedMessageBoxButton::Primary,
                  .object_name = QStringLiteral("second.primary"),
                  .label = QStringLiteral("Inspect"),
                  .role = QMessageBox::HelpRole,
              },
              {
                  .button = QtRetainedMessageBoxButton::Secondary,
                  .object_name = QStringLiteral("second.secondary"),
                  .label = QStringLiteral("Keep Editing"),
                  .role = QMessageBox::RejectRole,
              },
          },
      .default_button = QtRetainedMessageBoxButton::Secondary,
      .escape_button = QtRetainedMessageBoxButton::Secondary,
      .close_button = QtRetainedMessageBoxButton::Secondary,
  };
  QTimer::singleShot(0, &message_box, [&]() {
    expect_native_isolation();
    EXPECT_EQ(message_box.layout(), retained_layout);
    EXPECT_EQ(message_box.retained_button(
                  QtRetainedMessageBoxButton::Primary),
              primary);
    EXPECT_EQ(message_box.retained_button(
                  QtRetainedMessageBoxButton::Secondary),
              secondary);
    EXPECT_EQ(message_box.retained_button(
                  QtRetainedMessageBoxButton::Tertiary),
              tertiary);
    EXPECT_EQ(message_box.objectName(), second.object_name);
    EXPECT_EQ(message_box.presented_icon(), QMessageBox::Question);
    EXPECT_EQ(message_box.buttonRole(primary), QMessageBox::HelpRole);
    EXPECT_EQ(message_box.buttonRole(secondary), QMessageBox::RejectRole);
    EXPECT_EQ(message_box.defaultButton(), secondary);
    EXPECT_EQ(message_box.escapeButton(), secondary);
    EXPECT_FALSE(tertiary->isVisible());
    message_box.close();
  });
  EXPECT_EQ(message_box.present(second),
            QtRetainedMessageBoxButton::Secondary);
  EXPECT_TRUE(message_box.testAttribute(Qt::WA_WState_Created));
  expect_native_isolation();
  EXPECT_EQ(message_box.layout(), retained_layout);
}

TEST(QtRetainedMessageBoxTests,
     RefreshesRetainedStyleIconsAcrossApplicationPaletteChanges) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-retained-message-box-palette-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  QApplication::setStyle(new PaletteMessageIconStyle);

  QPalette palette = QApplication::palette();
  palette.setColor(QPalette::Text, QColor(211, 37, 53));
  QApplication::setPalette(palette);

  QtRetainedMessageBox message_box;
  ASSERT_TRUE(message_box.set_presentation(
      QtRetainedMessageBoxPresentation{
          .icon = QMessageBox::Critical,
          .object_name = QStringLiteral("palette_message_box"),
          .title = QStringLiteral("Palette"),
          .text = QStringLiteral("Palette-aware icon"),
          .buttons =
              {{
                  .button = QtRetainedMessageBoxButton::Primary,
                  .object_name = QStringLiteral("palette_message_box.ok"),
                  .label = QStringLiteral("OK"),
                  .role = QMessageBox::AcceptRole,
              }},
          .default_button = QtRetainedMessageBoxButton::Primary,
          .escape_button = QtRetainedMessageBoxButton::Primary,
          .close_button = QtRetainedMessageBoxButton::Primary,
      }));
  message_box.prewarm();
  auto *const icon_label = message_box.findChild<QLabel *>(
      QStringLiteral("qt_msgboxex_icon_label"));
  ASSERT_NE(icon_label, nullptr);
  ASSERT_FALSE(icon_label->pixmap().isNull());
  const WId retained_window_id = message_box.winId();
  QLayout *const retained_layout = message_box.layout();
  const qint64 original_icon_key = icon_label->pixmap().cacheKey();
  EXPECT_EQ(icon_label->pixmap().toImage().pixelColor(0, 0),
            QColor(211, 37, 53));

  palette.setColor(QPalette::Text, QColor(29, 197, 83));
  QApplication::setPalette(palette);
  QCoreApplication::processEvents();

  ASSERT_FALSE(icon_label->pixmap().isNull());
  EXPECT_NE(icon_label->pixmap().cacheKey(), original_icon_key);
  EXPECT_EQ(icon_label->pixmap().toImage().pixelColor(0, 0),
            QColor(29, 197, 83));
  EXPECT_EQ(message_box.winId(), retained_window_id);
  EXPECT_EQ(message_box.layout(), retained_layout);
}

TEST(QtRetainedMessageBoxTests,
     ScrollablePathsKeepActionsOnScreenAndRetainNativeWidgetsAcrossScaleChanges) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-retained-message-box-scroll-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  application.setStyle(QStringLiteral("Fusion"));
  QFont application_font = application.font();
  application_font.setPointSizeF(10.0);
  application.setFont(application_font);
  const QPalette native_palette = application.style()->standardPalette();
  apply_qt_obsidian_application_theme(native_palette);
  QtUiScaleApplicationController scale_controller(application);

  QScreen *const primary_screen = QApplication::primaryScreen();
  ASSERT_NE(primary_screen, nullptr);
  const bool small_screen =
      qEnvironmentVariableIntValue("QUADER_TEST_SMALL_SCREEN") == 1;
  const QRect original_geometry = primary_screen->geometry();
  const QRect original_available_geometry = primary_screen->availableGeometry();
  const auto restore_screen_geometry = qScopeGuard([&]() {
    if (small_screen) {
      QWindowSystemInterface::handleScreenGeometryChange(
          primary_screen, original_geometry, original_available_geometry);
      QWindowSystemInterface::sendWindowSystemEvents(QEventLoop::AllEvents);
    }
  });
  if (small_screen) {
    // Only this QApplication sees the smaller work area; the Windows display
    // mode and desktop geometry are never changed.
    const QRect test_geometry(original_geometry.topLeft(), QSize(800, 600));
    QWindowSystemInterface::handleScreenGeometryChange(
        primary_screen, test_geometry, test_geometry);
    QWindowSystemInterface::sendWindowSystemEvents(QEventLoop::AllEvents);
    ASSERT_EQ(primary_screen->availableGeometry(), test_geometry);
  }
  const QString capture_suffix =
      small_screen ? QStringLiteral("-800x600") : QString{};

  QWidget owner;
  owner.setScreen(primary_screen);
  owner.resize(640, 480);
  owner.move(primary_screen->availableGeometry().topLeft());
  owner.setAttribute(Qt::WA_DontShowOnScreen);
  owner.show();
  QtRetainedMessageBox message_box(&owner);
  message_box.setScreen(primary_screen);
  message_box.setAttribute(Qt::WA_DontShowOnScreen);
  QStringList asset_paths;
  for (int index = 0; index < 200; ++index) {
    asset_paths.push_back(QStringLiteral("Materials/stone_%1.qmat").arg(index));
  }
  asset_paths.push_back(QStringLiteral("Materials/") +
                       QString(600, QLatin1Char('x')) +
                       QStringLiteral("/<b>final_asset</b>.qmat"));
  QtRetainedMessageBoxPresentation presentation{
      .icon = QMessageBox::Warning,
      .object_name = QStringLiteral("asset_delete_message_box"),
      .title = QStringLiteral("Delete Assets"),
      .text = QStringLiteral("Move the selected assets to the Recycle Bin?"),
      .informative_text = asset_paths.join(QLatin1Char('\n')),
      .scrollable_informative_text = true,
      .buttons =
          {
              {
                  .button = QtRetainedMessageBoxButton::Primary,
                  .object_name = QStringLiteral("asset_delete.confirm"),
                  .label = QStringLiteral("Delete"),
                  .role = QMessageBox::DestructiveRole,
              },
              {
                  .button = QtRetainedMessageBoxButton::Secondary,
                  .object_name = QStringLiteral("asset_delete.cancel"),
                  .label = QStringLiteral("Cancel"),
                  .role = QMessageBox::RejectRole,
              },
          },
      .default_button = QtRetainedMessageBoxButton::Secondary,
      .escape_button = QtRetainedMessageBoxButton::Secondary,
      .close_button = QtRetainedMessageBoxButton::Secondary,
  };
  ASSERT_TRUE(message_box.set_presentation(presentation));
  message_box.prewarm();
  const WId retained_window_id = message_box.internalWinId();
  QLayout *const retained_layout = message_box.layout();
  auto *const editor = message_box.findChild<QPlainTextEdit *>(
      QStringLiteral("retainedMessageBox.informativeText"));
  auto *const informative_label = message_box.findChild<QLabel *>(
      QStringLiteral("qt_msgbox_informativelabel"));
  QPushButton *const confirm = message_box.retained_button(
      QtRetainedMessageBoxButton::Primary);
  QPushButton *const cancel = message_box.retained_button(
      QtRetainedMessageBoxButton::Secondary);
  ASSERT_NE(editor, nullptr);
  ASSERT_NE(informative_label, nullptr);
  ASSERT_NE(confirm, nullptr);
  ASSERT_NE(cancel, nullptr);
  ASSERT_NE(retained_window_id, WId{});
  const QString review_directory = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
  if (!review_directory.isEmpty()) {
    ASSERT_TRUE(QDir().mkpath(review_directory));
  }

  QSize first_size;
  int presentation_index = 0;
  for (const float factor : {1.0F, 2.0F, 1.0F}) {
    scale_controller.apply_factor(factor);
    message_box.apply_ui_scale(scale_controller.metrics());
    QTimer::singleShot(0, &message_box, [&]() {
      EXPECT_EQ(message_box.layout(), retained_layout);
      EXPECT_EQ(message_box.internalWinId(), retained_window_id);
      EXPECT_EQ(message_box.findChild<QPlainTextEdit *>(
                    QStringLiteral("retainedMessageBox.informativeText")),
                editor);
      EXPECT_TRUE(editor->isVisible());
      EXPECT_TRUE(editor->isReadOnly());
      EXPECT_FALSE(informative_label->isVisible());
      EXPECT_EQ(message_box.informativeText(), presentation.informative_text);
      EXPECT_EQ(editor->toPlainText(), presentation.informative_text);
      EXPECT_EQ(editor->verticalScrollBar()->value(), 0);
      EXPECT_GT(editor->verticalScrollBar()->maximum(), 0);
      EXPECT_EQ(editor->horizontalScrollBar()->maximum(), 0);
      EXPECT_EQ(message_box.screen(), primary_screen);
      if (small_screen) {
        EXPECT_EQ(message_box.screen()->availableGeometry().size(),
                  QSize(800, 600));
      }
      const QSize fitted_size = message_box.size();
      QEvent layout_request(QEvent::LayoutRequest);
      QCoreApplication::sendEvent(&message_box, &layout_request);
      EXPECT_EQ(message_box.size(), fitted_size);
      EXPECT_TRUE(message_box.rect().contains(editor->geometry()));
      const QRect available = message_box.screen()->availableGeometry();
      EXPECT_TRUE(available.contains(message_box.frameGeometry()));
      for (QPushButton *const button : {confirm, cancel}) {
        const QRect button_rect(button->mapTo(&message_box, QPoint{}),
                                button->size());
        EXPECT_TRUE(message_box.rect().contains(button_rect));
        EXPECT_GT(button_rect.top(), editor->geometry().bottom());
      }
      if (presentation_index == 0) {
        first_size = message_box.size();
      } else if (presentation_index == 2) {
        EXPECT_EQ(message_box.size(), first_size);
      }
      if (!review_directory.isEmpty()) {
        EXPECT_TRUE(message_box.grab().save(QDir(review_directory).filePath(
            QStringLiteral("asset-delete-scroll-%1%2.png")
                .arg(presentation_index).arg(capture_suffix))));
      }
      editor->moveCursor(QTextCursor::End);
      editor->ensureCursorVisible();
      EXPECT_EQ(editor->textCursor().block().text(), asset_paths.back());
      EXPECT_GT(editor->verticalScrollBar()->value(), 0);
      EXPECT_TRUE(editor->viewport()->rect().contains(editor->cursorRect()));
      if (!review_directory.isEmpty()) {
        EXPECT_TRUE(message_box.grab().save(QDir(review_directory).filePath(
            QStringLiteral("asset-delete-scroll-%1%2-end.png")
                .arg(presentation_index).arg(capture_suffix))));
      }

      // Exercise a pointer click at the displayed action's center after the
      // list scrolled, preserving the native button's hit-testing behavior.
      const QPointF position(confirm->rect().center());
      const QPointF global_position(confirm->mapToGlobal(position.toPoint()));
      QMouseEvent press(QEvent::MouseButtonPress, position, global_position,
                        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
      QCoreApplication::sendEvent(confirm, &press);
      QMouseEvent release(QEvent::MouseButtonRelease, position, global_position,
                          Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
      QCoreApplication::sendEvent(confirm, &release);
    });
    EXPECT_EQ(message_box.present(presentation),
              QtRetainedMessageBoxButton::Primary);
    ++presentation_index;
  }

  // A shorter subsequent confirmation should reclaim space, then an ordinary
  // retained message must return to QMessageBox's unscrolled label layout.
  presentation.informative_text = QStringLiteral("Materials/one.qmat");
  QTimer::singleShot(0, &message_box, [&]() {
    EXPECT_EQ(editor->toPlainText(), presentation.informative_text);
    EXPECT_EQ(editor->verticalScrollBar()->maximum(), 0);
    EXPECT_LT(message_box.height(), first_size.height());
    cancel->click();
  });
  EXPECT_EQ(message_box.present(presentation),
            QtRetainedMessageBoxButton::Secondary);
  presentation.scrollable_informative_text = false;
  QTimer::singleShot(0, &message_box, [&]() {
    EXPECT_FALSE(editor->isVisible());
    EXPECT_TRUE(informative_label->isVisible());
    EXPECT_EQ(message_box.informativeText(), presentation.informative_text);
    EXPECT_EQ(message_box.layout(), retained_layout);
    EXPECT_EQ(message_box.internalWinId(), retained_window_id);
    cancel->click();
  });
  EXPECT_EQ(message_box.present(presentation),
            QtRetainedMessageBoxButton::Secondary);

  apply_qt_application_theme_for_contrast_preference(
      native_palette, Qt::ContrastPreference::HighContrast);
  presentation.scrollable_informative_text = true;
  presentation.informative_text = asset_paths.join(QLatin1Char('\n'));
  QTimer::singleShot(0, &message_box, [&]() {
    EXPECT_EQ(editor->palette().color(QPalette::Base),
              QApplication::palette().color(QPalette::Base));
    EXPECT_EQ(editor->palette().color(QPalette::Text),
              QApplication::palette().color(QPalette::Text));
    EXPECT_TRUE(message_box.screen()->availableGeometry().contains(
        message_box.frameGeometry()));
    if (!review_directory.isEmpty()) {
      EXPECT_TRUE(message_box.grab().save(QDir(review_directory).filePath(
          QStringLiteral("asset-delete-scroll-high-contrast%1.png")
              .arg(capture_suffix))));
    }
    cancel->click();
  });
  EXPECT_EQ(message_box.present(presentation),
            QtRetainedMessageBoxButton::Secondary);
}
