////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "core/localization_service.hpp"
#include "core/logger.hpp"
#include "editor/session/scene_session.hpp"
#include "ui/console_panel.hpp"
#include "ui/qt_application_theme.hpp"
#include "ui/qt_settings_control_widgets.hpp"
#include "ui/qt_ui_scale_metrics.hpp"
#include "ui/ui_context.hpp"
#include "ui/ui_settings.hpp"
#include "viewport/viewport_settings.hpp"

#include <gtest/gtest.h>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDir>
#include <QImage>
#include <QKeyEvent>
#include <QLayout>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QStyle>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>

#include <memory>
#include <string>
#include <string_view>

namespace quader {
namespace {

struct ConsoleRuntime {
  SceneSession session;
  QuaderEditorApi editor{
      QuaderApiSessionInternalAccess::from_scene_session(session)};
  UISettings ui_settings = UISettings::defaults();
  ViewportSettings viewport_settings = ViewportSettings::defaults();
  LocalizationService localization;
  UIContext context{
      .intent = {.editor = editor},
      .scene = {.document = session.scene(),
                .assets = session.assets(),
                .modeling = session.modeling(),
                .selection = session.selection()},
      .preferences = {.ui = ui_settings, .viewport = viewport_settings},
      .localization = localization,
  };
};

void send_key(QWidget &widget, int key,
              Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  QKeyEvent press(QEvent::KeyPress, key, modifiers);
  QCoreApplication::sendEvent(&widget, &press);
  QKeyEvent release(QEvent::KeyRelease, key, modifiers);
  QCoreApplication::sendEvent(&widget, &release);
}

class QtConsolePanelTests : public testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(QCoreApplication::instance(), nullptr);
    application_ = std::make_unique<QApplication>(argc_, argv_);
    application_->setQuitOnLastWindowClosed(false);
    QApplication::setStyle(QStringLiteral("Fusion"));
    QApplication::setPalette(qt_obsidian_application_palette(
        QApplication::style()->standardPalette()));
    runtime_ = std::make_unique<ConsoleRuntime>();
    Logger::initialize({.name = "console-widget-tests", .file_path = ""});
    panel_ = std::make_unique<ConsolePanel>();
    panel_->setAttribute(Qt::WA_DontShowOnScreen);
    panel_->resize(720, 320);
    panel_->show();
    refresh();
    ASSERT_NE(entries(), nullptr);
  }

  void TearDown() override {
    panel_.reset();
    runtime_.reset();
    application_.reset();
  }

  void refresh() {
    panel_->refresh(runtime_->context);
    QCoreApplication::processEvents();
  }

  QPlainTextEdit *entries() const {
    return panel_->findChild<QPlainTextEdit *>(
        QStringLiteral("console.entries"));
  }

  QAction *category(const char *name) const {
    return panel_->findChild<QAction *>(
        QStringLiteral("console.level.%1").arg(QString::fromLatin1(name)));
  }

  void select_text(const QString &text) {
    const int start = entries()->toPlainText().indexOf(text);
    ASSERT_GE(start, 0);
    QTextCursor cursor(entries()->document());
    cursor.setPosition(start);
    cursor.setPosition(start + static_cast<int>(text.size()),
                       QTextCursor::KeepAnchor);
    entries()->setTextCursor(cursor);
  }

  QColor text_color(const QString &text) const {
    const int start = entries()->toPlainText().indexOf(text);
    EXPECT_GE(start, 0);
    QTextCursor cursor(entries()->document());
    cursor.setPosition(start + 1);
    return cursor.charFormat().foreground().color();
  }

  void commit_search(const QString &text) {
    auto *search =
        panel_->findChild<QLineEdit *>(QStringLiteral("console.search"));
    ASSERT_NE(search, nullptr);
    search->setText(text);
    auto *timer = panel_->findChild<QTimer *>();
    ASSERT_NE(timer, nullptr);
    timer->stop();
    ASSERT_TRUE(
        QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection));
  }

  void capture_widget(const QString &filename) {
    const QString directory = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
    if (directory.isEmpty()) {
      return;
    }
    ASSERT_TRUE(QDir().mkpath(directory));
    QImage image(panel_->size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    panel_->render(&image);
    EXPECT_TRUE(image.save(QDir(directory).filePath(filename)));
  }

  int argc_ = 1;
  char application_name_[21] = "quader-console-tests";
  char *argv_[2] = {application_name_, nullptr};
  std::unique_ptr<QApplication> application_;
  std::unique_ptr<ConsoleRuntime> runtime_;
  std::unique_ptr<ConsolePanel> panel_;
};

TEST_F(QtConsolePanelTests,
       CopiesPartialAndMultilineTextWithNativeShortcutsAndMenu) {
  LOG_INFO("alpha beta\nsecond line");
  LOG_WARN("third line");
  refresh();
  EXPECT_TRUE(entries()->isReadOnly());
  EXPECT_EQ(entries()->lineWrapMode(), QPlainTextEdit::NoWrap);
  select_text(QStringLiteral("beta\nsecond"));
  entries()->setFocus();
  send_key(*entries(), Qt::Key_C, Qt::ControlModifier);
  EXPECT_EQ(QApplication::clipboard()->text(), QStringLiteral("beta\nsecond"));
  const auto selected = entries()->textCursor().selectedText();
  std::unique_ptr<QMenu> menu(entries()->createStandardContextMenu());
  QAction *copy = nullptr;
  for (QAction *action : menu->actions()) {
    if (action->text().contains(QStringLiteral("Copy"))) {
      copy = action;
      break;
    }
  }
  ASSERT_NE(copy, nullptr);
  EXPECT_TRUE(copy->isEnabled());
  QApplication::clipboard()->clear();
  copy->trigger();
  EXPECT_EQ(QApplication::clipboard()->text(), QStringLiteral("beta\nsecond"));
  EXPECT_EQ(entries()->textCursor().selectedText(), selected);
  send_key(*entries(), Qt::Key_A, Qt::ControlModifier);
  send_key(*entries(), Qt::Key_C, Qt::ControlModifier);
  EXPECT_EQ(QApplication::clipboard()->text(), entries()->toPlainText());
}

TEST_F(QtConsolePanelTests,
       AppendsWithoutClobberingSelectionOrScrolledPosition) {
  for (int index = 0; index < 80; ++index) {
    LOG_INFO("retained line {}", index);
  }
  refresh();
  select_text(QStringLiteral("retained line 4"));
  QScrollBar *scroll = entries()->verticalScrollBar();
  ASSERT_GT(scroll->maximum(), 10);
  scroll->setValue(3);
  const int anchor = entries()->textCursor().anchor();
  const int position = entries()->textCursor().position();
  const int scroll_value = scroll->value();
  LOG_INFO("appended while reading");
  refresh();
  EXPECT_EQ(entries()->textCursor().anchor(), anchor);
  EXPECT_EQ(entries()->textCursor().position(), position);
  EXPECT_EQ(entries()->textCursor().selectedText(),
            QStringLiteral("retained line 4"));
  EXPECT_EQ(scroll->value(), scroll_value);
  scroll->setValue(scroll->maximum());
  LOG_INFO("appended while following");
  refresh();
  EXPECT_EQ(scroll->value(), scroll->maximum());
  auto *automatic =
      panel_->findChild<QCheckBox *>(QStringLiteral("console.auto_scroll"));
  ASSERT_NE(automatic, nullptr);
  automatic->setChecked(false);
  const int old_bottom = scroll->value();
  LOG_INFO("appended with following disabled");
  refresh();
  EXPECT_EQ(scroll->value(), old_bottom);
  EXPECT_LT(scroll->value(), scroll->maximum());
}

TEST_F(QtConsolePanelTests,
       FiltersIndependentCategoriesAndGroupsCriticalAsError) {
  LOG_TRACE("trace payload");
  LOG_DEBUG("debug payload");
  LOG_INFO("info payload");
  LOG_WARN("warning payload");
  LOG_ERROR("error payload");
  LOG_CRITICAL("critical payload");
  refresh();
  auto *menu = panel_->findChild<QMenu *>(QStringLiteral("console.level.menu"));
  ASSERT_NE(menu, nullptr);
  ASSERT_EQ(menu->actions().size(), 5);
  for (QAction *action : menu->actions()) {
    EXPECT_TRUE(action->isCheckable());
    EXPECT_TRUE(action->isChecked());
    action->setChecked(false);
  }
  EXPECT_TRUE(entries()->toPlainText().isEmpty());
  category("info")->setChecked(true);
  category("warning")->setChecked(true);
  EXPECT_TRUE(
      entries()->toPlainText().contains(QStringLiteral("info payload")));
  EXPECT_TRUE(
      entries()->toPlainText().contains(QStringLiteral("warning payload")));
  EXPECT_FALSE(
      entries()->toPlainText().contains(QStringLiteral("error payload")));
  EXPECT_FALSE(
      entries()->toPlainText().contains(QStringLiteral("critical payload")));
  category("error")->setChecked(true);
  EXPECT_TRUE(entries()->toPlainText().contains(
      QStringLiteral("[Error] critical payload")));
  commit_search(QStringLiteral("CRITICAL"));
  EXPECT_EQ(entries()->document()->blockCount(), 1);
  EXPECT_TRUE(
      entries()->toPlainText().contains(QStringLiteral("critical payload")));
}

TEST_F(QtConsolePanelTests, ShowsSecondsAndCompactMultilineParagraphs) {
  LOG_INFO("first\nsecond\n");
  LOG_CRITICAL("third");
  refresh();
  const QString text = entries()->toPlainText();
  EXPECT_TRUE(
      QRegularExpression(
          QStringLiteral(
              "^\\[\\d{2}:\\d{2}:\\d{2}\\] \\[Info\\] "
              "first\\nsecond\\n\\[\\d{2}:\\d{2}:\\d{2}\\] \\[Error\\] third$"))
          .match(text)
          .hasMatch());
  EXPECT_EQ(entries()->document()->blockCount(), 3);
  for (QTextBlock block = entries()->document()->begin(); block.isValid();
       block = block.next()) {
    EXPECT_EQ(block.blockFormat().topMargin(), 0);
    EXPECT_EQ(block.blockFormat().bottomMargin(), 0);
  }
}

TEST_F(QtConsolePanelTests,
       UnchangedRefreshDoesNotRewriteTheDocumentOrControls) {
  LOG_INFO("retained contents");
  refresh();
  select_text(QStringLiteral("contents"));
  int document_changes = 0;
  QObject::connect(entries()->document(), &QTextDocument::contentsChanged,
                   panel_.get(), [&document_changes]() { ++document_changes; });
  const int revision = entries()->document()->revision();
  for (int iteration = 0; iteration < 32; ++iteration) {
    refresh();
  }
  EXPECT_EQ(document_changes, 0);
  EXPECT_EQ(entries()->document()->revision(), revision);
  EXPECT_EQ(entries()->textCursor().selectedText(), QStringLiteral("contents"));
}

TEST_F(QtConsolePanelTests,
       EvictsWholeMultilineEntriesAndKeepsSurvivingSelection) {
  Logger::initialize(
      {.name = "console-small-ring", .file_path = "", .max_entries = 3});
  LOG_INFO("old first\nold continuation\nold tail");
  LOG_WARN("second first\nsecond tail");
  LOG_ERROR("third first\nthird tail");
  refresh();
  select_text(QStringLiteral("second tail"));
  LOG_INFO("fourth first\nfourth tail");
  refresh();
  EXPECT_FALSE(entries()->toPlainText().contains(QStringLiteral("old")));
  EXPECT_TRUE(entries()->toPlainText().startsWith(QChar('[')));
  EXPECT_EQ(entries()->document()->blockCount(), 6);
  EXPECT_EQ(entries()->textCursor().selectedText(),
            QStringLiteral("second tail"));
  for (int index = 0; index < 5; ++index) {
    LOG_INFO("missed entry {}\ncontinuation {}", index, index);
  }
  refresh();
  EXPECT_EQ(entries()->document()->blockCount(), 6);
  EXPECT_FALSE(entries()->toPlainText().contains(QStringLiteral("second")));
}

TEST_F(QtConsolePanelTests,
       CommandsRetainTextFocusAndRepliesIgnoreFiltersWithBoundedRetention) {
  for (QAction *action :
       panel_->findChild<QMenu *>(QStringLiteral("console.level.menu"))
           ->actions()) {
    action->setChecked(false);
  }
  commit_search(QStringLiteral("cannot match this reply"));
  auto *command =
      panel_->findChild<QLineEdit *>(QStringLiteral("console.command"));
  ASSERT_NE(command, nullptr);
  std::string received;
  panel_->set_command_handler([this, &received](std::string_view text) {
    received = text;
    panel_->append_command_reply(spdlog::level::info,
                                 "selection dump\nsecond line");
  });
  QApplication::setActiveWindow(panel_.get());
  command->setFocus(Qt::OtherFocusReason);
  QCoreApplication::processEvents();
  ASSERT_TRUE(command->hasFocus());
  command->setText(QStringLiteral("  /dump  "));
  send_key(*command, Qt::Key_Return);
  EXPECT_EQ(received, "/dump");
  EXPECT_TRUE(command->text().isEmpty());
  EXPECT_TRUE(command->hasFocus());
  EXPECT_TRUE(entries()->toPlainText().contains(
      QStringLiteral("selection dump\nsecond line")));
  panel_->set_command_handler({});
  command->setText(QStringLiteral("/unknown"));
  send_key(*command, Qt::Key_Return);
  EXPECT_TRUE(entries()->toPlainText().contains(
      QStringLiteral("[Error] Unknown command: /unknown")));
  for (int index = 0; index < 200; ++index) {
    panel_->append_command_reply(spdlog::level::info,
                                 "retained reply\ncontinuation");
  }
  EXPECT_EQ(entries()->document()->blockCount(), 400);
  EXPECT_FALSE(
      entries()->toPlainText().contains(QStringLiteral("selection dump")));
  panel_->findChild<QPushButton *>(QStringLiteral("console.clear"))->click();
  EXPECT_TRUE(entries()->toPlainText().isEmpty());
}

TEST_F(QtConsolePanelTests,
       InterleavedRepliesSurviveLogEvictionAndKeepMultilineSelection) {
  Logger::initialize(
      {.name = "console-interleaved-ring", .file_path = "", .max_entries = 2});
  LOG_INFO("old log\nold continuation");
  refresh();
  panel_->append_command_reply(spdlog::level::info,
                               "reply first line\nreply second line");
  LOG_WARN("middle log\nmiddle continuation");
  refresh();
  panel_->append_command_reply(spdlog::level::info,
                               "latest reply\nlatest continuation");
  select_text(QStringLiteral("reply first line\nreply second line"));
  const QString selection = entries()->textCursor().selectedText();
  LOG_ERROR("new log\nnew continuation");
  refresh();
  EXPECT_FALSE(entries()->toPlainText().contains(QStringLiteral("old log")));
  EXPECT_EQ(entries()->textCursor().selectedText(), selection);
  EXPECT_EQ(entries()->document()->blockCount(), 8);
  LOG_INFO("newest log\nnewest continuation");
  refresh();
  EXPECT_FALSE(entries()->toPlainText().contains(QStringLiteral("middle log")));
  EXPECT_TRUE(entries()->toPlainText().contains(
      QStringLiteral("latest reply\nlatest continuation")));
  EXPECT_EQ(entries()->textCursor().selectedText(), selection);
  EXPECT_EQ(entries()->document()->blockCount(), 8);
  entries()->setFocus();
  send_key(*entries(), Qt::Key_C, Qt::ControlModifier);
  EXPECT_EQ(QApplication::clipboard()->text(),
            QStringLiteral("reply first line\nreply second line"));
}

TEST_F(QtConsolePanelTests,
       UsesSemanticColorsAndResolvesLivePaletteAndHighContrast) {
  LOG_TRACE("subdued text");
  LOG_INFO("normal text");
  LOG_WARN("warning text");
  LOG_ERROR("error text");
  refresh();
  const QColor error = text_color(QStringLiteral("error text"));
  const QColor warning = text_color(QStringLiteral("warning text"));
  EXPECT_GT(error.red(), error.green());
  EXPECT_GT(warning.green(), warning.blue());
  EXPECT_NE(text_color(QStringLiteral("subdued text")),
            text_color(QStringLiteral("normal text")));
  select_text(QStringLiteral("normal text"));
  QPalette light_palette;
  light_palette.setColor(QPalette::All, QPalette::Window, Qt::white);
  light_palette.setColor(QPalette::All, QPalette::Base, Qt::white);
  light_palette.setColor(QPalette::All, QPalette::WindowText, Qt::black);
  light_palette.setColor(QPalette::All, QPalette::Text, Qt::black);
  panel_->setPalette(light_palette);
  QCoreApplication::processEvents();
  EXPECT_EQ(text_color(QStringLiteral("error text")), QColor(Qt::black));
  EXPECT_EQ(entries()->textCursor().selectedText(),
            QStringLiteral("normal text"));
  capture_widget(QStringLiteral("console_light_palette.png"));
  panel_->setPalette(QPalette{});
  apply_qt_application_theme_for_contrast_preference(
      QPalette{}, Qt::ContrastPreference::HighContrast);
  QCoreApplication::processEvents();
  EXPECT_EQ(text_color(QStringLiteral("error text")),
            entries()->palette().color(QPalette::Text));
  EXPECT_EQ(text_color(QStringLiteral("warning text")),
            entries()->palette().color(QPalette::Text));
  capture_widget(QStringLiteral("console_native_contrast.png"));
}

TEST_F(QtConsolePanelTests, RendersNativeWidgetsAtBothScalesAndNarrowWidth) {
  LOG_TRACE("Mesh cache revision 42");
  LOG_INFO("Imported level geometry");
  LOG_WARN("Missing texture: stone_wall.png");
  LOG_ERROR("Unable to open scene\nAccess denied");
  refresh();
  QtUiScaleApplicationController scale(*application_);
  const QString review_directory = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
  if (!review_directory.isEmpty()) {
    ASSERT_TRUE(QDir().mkpath(review_directory));
  }
  for (const float factor : {1.0F, 2.0F, 1.0F}) {
    scale.apply_factor(factor);
    qt_settings_detail::apply_scalable_layout_metrics(*panel_, scale.metrics());
    panel_->resize(scale.metrics().px(460), scale.metrics().px(260));
    panel_->ensurePolished();
    ASSERT_NE(panel_->layout(), nullptr);
    panel_->layout()->activate();
    QCoreApplication::processEvents();
    auto *levels =
        panel_->findChild<QToolButton *>(QStringLiteral("console.level"));
    auto *search =
        panel_->findChild<QLineEdit *>(QStringLiteral("console.search"));
    auto *clear =
        panel_->findChild<QPushButton *>(QStringLiteral("console.clear"));
    auto *automatic =
        panel_->findChild<QCheckBox *>(QStringLiteral("console.auto_scroll"));
    ASSERT_NE(levels, nullptr);
    ASSERT_NE(search, nullptr);
    ASSERT_NE(clear, nullptr);
    ASSERT_NE(automatic, nullptr);
    EXPECT_LT(levels->geometry().right(), search->geometry().left());
    EXPECT_LT(search->geometry().right(), clear->geometry().left());
    EXPECT_LT(clear->geometry().right(), automatic->geometry().left());
    EXPECT_GT(search->width(), scale.metrics().px(40));
    QImage image(panel_->size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    panel_->render(&image);
    EXPECT_FALSE(image.isNull());
    if (!review_directory.isEmpty()) {
      EXPECT_TRUE(
          image.save(QDir(review_directory)
                         .filePath(QStringLiteral("console_%1.png")
                                       .arg(factor == 2.0F ? 200 : 100))));
      QMenu *menu = levels->menu();
      menu->ensurePolished();
      menu->resize(menu->sizeHint());
      QImage menu_image(menu->size(), QImage::Format_ARGB32_Premultiplied);
      menu_image.fill(Qt::transparent);
      menu->render(&menu_image);
      EXPECT_TRUE(
          menu_image.save(QDir(review_directory)
                              .filePath(QStringLiteral("console_levels_%1.png")
                                            .arg(factor == 2.0F ? 200 : 100))));
    }
  }
}

} // namespace
} // namespace quader
