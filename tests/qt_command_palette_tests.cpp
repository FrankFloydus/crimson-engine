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
#include "ui/qt_command_palette.hpp"
#include "ui/qt_status_message.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QEventLoop>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLabel>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include <array>
#include <filesystem>
#include <string>
#include <string_view>

#ifndef QUADER_SOURCE_DIR
#define QUADER_SOURCE_DIR "."
#endif

namespace quader {
namespace {

LocalizationService loaded_localization() {
  LocalizationService localization;
  EXPECT_TRUE(localization.load(
      std::filesystem::path(QUADER_SOURCE_DIR) / "resources" / "locales",
      "en-US"));
  return localization;
}

void run_next_event_turn() {
  QEventLoop loop;
  QTimer::singleShot(0, &loop, &QEventLoop::quit);
  loop.exec();
}

void send_key(QWidget &target, int key) {
  QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
  QCoreApplication::sendEvent(&target, &press);
  QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
  QCoreApplication::sendEvent(&target, &release);
}

QTreeWidgetItem *find_command_item(QTreeWidget &results,
                                   std::string_view command_id) {
  const QString expected = QString::fromUtf8(
      command_id.data(), static_cast<qsizetype>(command_id.size()));
  for (int row = 0; row < results.topLevelItemCount(); ++row) {
    QTreeWidgetItem *item = results.topLevelItem(row);
    if (item->toolTip(0) == expected) {
      return item;
    }
  }
  return nullptr;
}

TEST(QtCommandPaletteTests,
     SearchesCatalogMetadataAndDefersOpeningAndExecution) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-command-palette-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  LocalizationService localization = loaded_localization();
  QtCommandPalette palette(editor.commands(), localization);
  palette.setAttribute(Qt::WA_DontShowOnScreen, true);
  palette.set_metadata_resolver([](std::string_view command_id) {
    QtCommandPaletteMetadata metadata;
    if (command_id == quader_command::kEditUndo.string()) {
      metadata.disabled_reason = QStringLiteral("Nothing to undo");
    }
    return metadata;
  });

  std::string executed_command;
  palette.set_execution_observer(
      [&](std::string command_id) { executed_command = std::move(command_id); });

  palette.open_palette();
  EXPECT_FALSE(palette.isVisible());
  run_next_event_turn();
  EXPECT_TRUE(palette.isVisible());

  auto *search = palette.findChild<QLineEdit *>(
      QStringLiteral("commandPalette.search"));
  auto *results = palette.findChild<QTreeWidget *>(
      QStringLiteral("commandPalette.results"));
  ASSERT_NE(search, nullptr);
  ASSERT_NE(results, nullptr);
  EXPECT_TRUE(search->hasFocus());
  EXPECT_EQ(results->topLevelItemCount(), 0);

  search->setText(QStringLiteral("select.all"));
  QTreeWidgetItem *select_all =
      find_command_item(*results, quader_command::kSelectAll.string());
  ASSERT_NE(select_all, nullptr);
  EXPECT_FALSE(select_all->text(0).isEmpty());
  EXPECT_FALSE(select_all->text(1).isEmpty());
  EXPECT_EQ(select_all->text(2), QStringLiteral("Ctrl+A"));

  search->setText(QStringLiteral("modeling.uv_peel"));
  QTreeWidgetItem *uv_peel =
      find_command_item(*results, quader_command::kModelingUvPeel.string());
  ASSERT_NE(uv_peel, nullptr);
  EXPECT_EQ(uv_peel->text(0), QStringLiteral("UV Peel"));
  EXPECT_TRUE(
      uv_peel->text(1).startsWith(QStringLiteral("UV \u203a Texture")));

  search->setText(QStringLiteral("modeling.texture.align_to_view"));
  QTreeWidgetItem *align_uv_to_view = find_command_item(
      *results, quader_command::kModelingTextureAlignToView.string());
  ASSERT_NE(align_uv_to_view, nullptr);
  EXPECT_EQ(align_uv_to_view->text(0),
            QStringLiteral("Align Texture to View"));
  EXPECT_TRUE(align_uv_to_view->text(1).startsWith(
      QStringLiteral("UV \u203a Texture")));

  search->setText(QStringLiteral("modeling.texture.rotate_clockwise"));
  QTreeWidgetItem *rotate_clockwise = find_command_item(
      *results, quader_command::kModelingTextureRotateClockwise.string());
  ASSERT_NE(rotate_clockwise, nullptr);
  EXPECT_EQ(rotate_clockwise->text(0),
            QStringLiteral("Rotate Texture Clockwise"));
  EXPECT_TRUE(rotate_clockwise->text(1).startsWith(
      QStringLiteral("UV \u203a Texture")));

  struct PaletteOnlyUvCommand final {
    QuaderCommandId command_id;
    std::string_view full_label;
  };
  constexpr std::array<PaletteOnlyUvCommand, 10> kPaletteOnlyUvCommands{{
      {quader_command::kModelingTextureScaleXUp,
       "Increase Texture Scale X"},
      {quader_command::kModelingTextureScaleXDown,
       "Decrease Texture Scale X"},
      {quader_command::kModelingTextureScaleYUp,
       "Increase Texture Scale Y"},
      {quader_command::kModelingTextureScaleYDown,
       "Decrease Texture Scale Y"},
      {quader_command::kModelingTextureShiftLeft, "Shift Texture Left"},
      {quader_command::kModelingTextureShiftRight, "Shift Texture Right"},
      {quader_command::kModelingTextureShiftUp, "Shift Texture Up"},
      {quader_command::kModelingTextureShiftDown, "Shift Texture Down"},
      {quader_command::kModelingTextureFlipFaceHorizontal,
       "Flip Face Horizontally"},
      {quader_command::kModelingTextureFlipFaceVertical,
       "Flip Face Vertically"},
  }};
  for (const PaletteOnlyUvCommand &expected : kPaletteOnlyUvCommands) {
    search->setText(QString::fromUtf8(expected.command_id.string().data(),
                                      static_cast<qsizetype>(
                                          expected.command_id.string().size())));
    QTreeWidgetItem *item =
        find_command_item(*results, expected.command_id.string());
    ASSERT_NE(item, nullptr) << expected.command_id.string();
    EXPECT_EQ(item->text(0),
              QString::fromUtf8(
                  expected.full_label.data(),
                  static_cast<qsizetype>(expected.full_label.size())));
    EXPECT_TRUE(item->text(1).startsWith(
        QStringLiteral("UV \u203a Texture")))
        << expected.command_id.string();
  }

  // "deselect all" is catalog-owned alias metadata for Clear Selection.
  search->setText(QStringLiteral("deselect all"));
  QTreeWidgetItem *clear_selection = find_command_item(
      *results, quader_command::kEditClearSelection.string());
  ASSERT_NE(clear_selection, nullptr);
  results->setCurrentItem(clear_selection);
  send_key(*search, Qt::Key_Return);
  EXPECT_FALSE(palette.isVisible());
  EXPECT_TRUE(executed_command.empty());
  run_next_event_turn();
  EXPECT_EQ(executed_command,
            quader_command::kEditClearSelection.string());

  palette.open_palette();
  run_next_event_turn();
  ASSERT_EQ(results->topLevelItemCount(), 1);
  EXPECT_EQ(results->topLevelItem(0)->toolTip(0),
            QString::fromStdString(executed_command));
  EXPECT_TRUE(results->topLevelItem(0)->text(1).startsWith(
      QStringLiteral("Recent")));

  search->setText(QStringLiteral("edit.undo"));
  QTreeWidgetItem *undo =
      find_command_item(*results, quader_command::kEditUndo.string());
  ASSERT_NE(undo, nullptr);
  EXPECT_TRUE(undo->text(1).contains(QStringLiteral("Nothing to undo")));

  // The command which opens search is deliberately self-exempt.
  search->setText(QStringLiteral("command palette"));
  EXPECT_EQ(find_command_item(*results, "ui.search_commands"), nullptr);

  send_key(*search, Qt::Key_Escape);
  EXPECT_FALSE(palette.isVisible());
}

TEST(QtCommandPaletteTests,
     RejectedExecutionShowsErrorWithoutDuplicatingTheCommandAnnouncement) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-command-palette-error-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto box = editor.modeling().create_box(
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      "Palette Failure");
  ASSERT_TRUE(box.ok());
  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection().select_face(box.value(), 1).ok());
  LocalizationService localization = loaded_localization();
  QtCommandPalette palette(editor.commands(), localization);
  palette.setAttribute(Qt::WA_DontShowOnScreen, true);
  palette.open_palette();
  run_next_event_turn();
  auto *search = palette.findChild<QLineEdit *>(QStringLiteral("commandPalette.search"));
  auto *results = palette.findChild<QTreeWidget *>(QStringLiteral("commandPalette.results"));
  auto *status = dynamic_cast<QtStatusMessage *>(
      palette.findChild<QLabel *>(QStringLiteral("commandPalette.status")));
  ASSERT_NE(search, nullptr);
  ASSERT_NE(results, nullptr);
  ASSERT_NE(status, nullptr);
  int local_announcements = 0;
  status->set_error_sound_callback([&] { ++local_announcements; });
  search->setText(QString::fromStdString(
      std::string(quader_command::kModelingTextureFlipFaceHorizontal.string())));
  auto *flip = find_command_item(*results,
      quader_command::kModelingTextureFlipFaceHorizontal.string());
  ASSERT_NE(flip, nullptr);
  results->setCurrentItem(flip);
  send_key(*search, Qt::Key_Return);
  run_next_event_turn();
  ASSERT_TRUE(editor.commands().last_status().has_value());
  EXPECT_FALSE(editor.commands().last_status()->ok());
  EXPECT_TRUE(palette.isVisible());
  EXPECT_TRUE(status->is_error());
  EXPECT_FALSE(status->text().isEmpty());
  EXPECT_EQ(local_announcements, 0);
  search->setText(QStringLiteral("select.all"));
  EXPECT_FALSE(status->is_error());
  EXPECT_EQ(local_announcements, 0);
}

} // namespace
} // namespace quader
