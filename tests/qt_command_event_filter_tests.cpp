////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "api/quader_command_ids.hpp"
#include "api/quader_editor_api.hpp"
#include "editor/session/project_session.hpp"
#include "editor/session/quader_editor_api_session_access.hpp"
#include "ui/qt_command_event_filter.hpp"
#include "ui/qt_viewport_surface.hpp"

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QWidget>

#include <cstdint>
#include <filesystem>
#include <optional>

namespace {

void send_key_press(QWidget &target, int key,
                    Qt::KeyboardModifiers modifiers,
                    const QString &text = {}) {
  QKeyEvent event(QEvent::KeyPress, key, modifiers, text);
  QCoreApplication::sendEvent(&target, &event);
}

TEST(QtCommandEventFilterTests,
     DispatchContextPreservesActiveToolAndSelectionMode) {
  const quader::ViewportToolState state =
      quader::qt_command_dispatch_tool_state(
          []() {
            return quader::ViewportToolState{
                .active_tool = quader::ViewportToolId::CreatePrimitive,
                .selection_mode = quader::ViewportSelectionMode::Face,
                .grid_preset = 2,
            };
          },
          9);

  EXPECT_EQ(state.active_tool, quader::ViewportToolId::CreatePrimitive);
  EXPECT_EQ(state.selection_mode, quader::ViewportSelectionMode::Face);
  EXPECT_EQ(state.grid_preset, 9);
}

TEST(QtCommandEventFilterTests, DispatchContextHasCanonicalFallback) {
  const quader::ViewportToolState state =
      quader::qt_command_dispatch_tool_state({}, 4);

  EXPECT_EQ(state.active_tool, quader::ViewportToolId::Select);
  EXPECT_EQ(state.selection_mode, quader::ViewportSelectionMode::Object);
  EXPECT_EQ(state.grid_preset, 4);
}

TEST(QtCommandEventFilterTests,
     ApplicationCommandsYieldToFocusedToolWindowContext) {
  EXPECT_FALSE(quader::qt_application_command_input_owner_active({}));

  int queries = 0;
  EXPECT_TRUE(quader::qt_application_command_input_owner_active([&queries]() {
    ++queries;
    return true;
  }));
  EXPECT_EQ(queries, 1);
}

TEST(QtCommandEventFilterTests,
     FocusedNativeControlsKeepQtKeysAndOrdinaryEditorFocusRoutesCommand) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-command-focus-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));
  application.setQuitOnLastWindowClosed(false);

  quader::QuaderStandaloneEditor standalone =
      quader::create_standalone_editor();
  quader::QuaderEditorApi &editor = standalone.editor();
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  const auto first = editor.modeling().create_box(
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      "First Focus Object");
  const auto second = editor.modeling().create_box(
      {.min = {2.0f, -1.0f, -1.0f}, .max = {4.0f, 1.0f, 1.0f}},
      "Second Focus Object");
  ASSERT_TRUE(first.ok()) << first.error_message();
  ASSERT_TRUE(second.ok()) << second.error_message();
  ASSERT_TRUE(editor.selection().select_object(first.value()).ok());
  ASSERT_EQ(editor.selection().summary().selected_object_count, 1u);
  ASSERT_EQ(editor.query().modeling_objects().size(), 2u);
  ASSERT_TRUE(editor.commands()
                  .remove_shortcut(quader::quader_command::kSelectAll)
                  .ok());
  ASSERT_TRUE(editor.commands()
                  .set_shortcut(quader::quader_command::kEditUndo,
                                "Ctrl+A")
                  .ok());
  const std::uint64_t status_revision_before_item_view =
      editor.commands().last_status_revision();

  quader::QtCommandEventFilter command_filter(editor, {});
  application.installEventFilter(&command_filter);

  QWidget editor_window;
  editor_window.resize(320, 390);
  QTreeWidget item_view(&editor_window);
  item_view.setGeometry(0, 0, 320, 145);
  item_view.setSelectionMode(QAbstractItemView::ExtendedSelection);
  item_view.addTopLevelItem(new QTreeWidgetItem({QStringLiteral("One")}));
  item_view.addTopLevelItem(new QTreeWidgetItem({QStringLiteral("Two")}));
  item_view.addTopLevelItem(new QTreeWidgetItem({QStringLiteral("Three")}));
  QComboBox combo(&editor_window);
  combo.setGeometry(0, 145, 320, 35);
  combo.addItems({QStringLiteral("Alpha"), QStringLiteral("Hotel")});
  QWidget editor_surface(&editor_window);
  editor_surface.setGeometry(0, 180, 320, 60);
  editor_surface.setFocusPolicy(Qt::StrongFocus);
  QTabBar scene_tabs(&editor_window);
  scene_tabs.setGeometry(0, 240, 320, 30);
  scene_tabs.addTab(QStringLiteral("Scene One"));
  scene_tabs.addTab(QStringLiteral("Scene Two"));
  QPushButton focus_before(QStringLiteral("Before"), &editor_window);
  QPushButton focus_after(QStringLiteral("After"), &editor_window);
  focus_before.setGeometry(0, 280, 150, 32);
  focus_after.setGeometry(170, 280, 150, 32);
  QWidget::setTabOrder(&focus_before, &focus_after);
  QLineEdit line_edit(&editor_window);
  line_edit.setGeometry(0, 325, 320, 32);
  editor_window.show();
  editor_window.activateWindow();
  item_view.setFocus(Qt::OtherFocusReason);
  QCoreApplication::processEvents();
  ASSERT_EQ(QApplication::focusWidget(), &item_view);

  send_key_press(item_view, Qt::Key_A, Qt::ControlModifier);

  EXPECT_EQ(item_view.selectedItems().size(), 3);
  EXPECT_EQ(editor.commands().last_status_revision(),
            status_revision_before_item_view)
      << "Qt item-view Ctrl+A must not execute a customized editor command";
  EXPECT_EQ(editor.query().modeling_objects().size(), 2u)
      << "Qt item-view Ctrl+A must not become a customized editor command";

  editor_surface.setFocus(Qt::OtherFocusReason);
  QCoreApplication::processEvents();
  ASSERT_EQ(QApplication::focusWidget(), &editor_surface);
  send_key_press(editor_surface, Qt::Key_A, Qt::ControlModifier);

  EXPECT_GT(editor.commands().last_status_revision(),
            status_revision_before_item_view)
      << "ordinary editor focus must retain global command routing";

  line_edit.setText(QStringLiteral("Native text selection"));
  line_edit.setCursorPosition(line_edit.text().size());
  line_edit.setFocus(Qt::OtherFocusReason);
  QCoreApplication::processEvents();
  const std::uint64_t status_revision_before_line_edit =
      editor.commands().last_status_revision();
  send_key_press(line_edit, Qt::Key_A, Qt::ControlModifier,
                 QStringLiteral("a"));
  EXPECT_EQ(line_edit.selectedText(), line_edit.text());
  EXPECT_EQ(editor.commands().last_status_revision(),
            status_revision_before_line_edit)
      << "the canonical text-input context must leave Ctrl+A with QLineEdit";

  ASSERT_TRUE(editor.commands()
                  .remove_shortcut(
                      quader::quader_command::kSceneHideSelection)
                  .ok());
  ASSERT_TRUE(editor.commands()
                  .set_shortcut(quader::quader_command::kEditUndo, "H")
                  .ok());
  const std::uint64_t status_revision_before_combo =
      editor.commands().last_status_revision();
  combo.setCurrentIndex(0);
  combo.setFocus(Qt::OtherFocusReason);
  QCoreApplication::processEvents();
  ASSERT_EQ(QApplication::focusWidget(), &combo);
  send_key_press(combo, Qt::Key_H, Qt::NoModifier, QStringLiteral("h"));

  EXPECT_EQ(combo.currentIndex(), 1)
      << "non-editable combo keyboard search must remain a Qt interaction";
  EXPECT_EQ(editor.commands().last_status_revision(),
            status_revision_before_combo)
      << "combo keyboard search must not become a customized global command";

  ASSERT_TRUE(editor.commands()
                  .set_shortcut(quader::quader_command::kEditUndo, "Right")
                  .ok());
  scene_tabs.setCurrentIndex(0);
  scene_tabs.setFocus(Qt::OtherFocusReason);
  QCoreApplication::processEvents();
  const std::uint64_t status_revision_before_tab_bar =
      editor.commands().last_status_revision();
  send_key_press(scene_tabs, Qt::Key_Right, Qt::NoModifier);
  EXPECT_EQ(scene_tabs.currentIndex(), 1);
  EXPECT_EQ(editor.commands().last_status_revision(),
            status_revision_before_tab_bar);

  ASSERT_TRUE(editor.commands()
                  .set_shortcut(quader::quader_command::kEditUndo, "Tab")
                  .ok());
  focus_before.setFocus(Qt::OtherFocusReason);
  QCoreApplication::processEvents();
  const std::uint64_t status_revision_before_focus_traversal =
      editor.commands().last_status_revision();
  send_key_press(focus_before, Qt::Key_Tab, Qt::NoModifier);
  EXPECT_EQ(QApplication::focusWidget(), &focus_after);
  EXPECT_EQ(editor.commands().last_status_revision(),
            status_revision_before_focus_traversal);

  application.removeEventFilter(&command_filter);
  editor_window.hide();
}

TEST(QtCommandEventFilterTests,
     FocusedAuxiliaryControlBlocksUnclaimedEditorGlobalCommand) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-command-auxiliary-focus-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));
  application.setQuitOnLastWindowClosed(false);

  quader::QuaderStandaloneEditor standalone =
      quader::create_standalone_editor();
  quader::QuaderEditorApi &editor = standalone.editor();
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  const auto first = editor.modeling().create_box(
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      "First Auxiliary Object");
  const auto second = editor.modeling().create_box(
      {.min = {2.0f, -1.0f, -1.0f}, .max = {4.0f, 1.0f, 1.0f}},
      "Second Auxiliary Object");
  ASSERT_TRUE(first.ok()) << first.error_message();
  ASSERT_TRUE(second.ok()) << second.error_message();
  ASSERT_TRUE(editor.selection().select_object(first.value()).ok());
  ASSERT_EQ(editor.query().modeling_objects().size(), 2u);
  ASSERT_TRUE(editor.commands()
                  .remove_shortcut(quader::quader_command::kSelectAll)
                  .ok());
  ASSERT_TRUE(editor.commands()
                  .set_shortcut(quader::quader_command::kEditUndo,
                                "Ctrl+A")
                  .ok());
  const std::uint64_t status_revision_before_auxiliary =
      editor.commands().last_status_revision();

  QWidget auxiliary_window;
  auxiliary_window.resize(200, 100);
  QPushButton auxiliary_button(QStringLiteral("Auxiliary Control"),
                               &auxiliary_window);
  auxiliary_button.setGeometry(10, 10, 180, 32);
  quader::QtCommandEventFilter command_filter(editor, {});
  command_filter.set_command_input_owner_provider([&auxiliary_window]() {
    return quader::qt_window_owns_command_input(&auxiliary_window);
  });
  application.installEventFilter(&command_filter);

  auxiliary_window.show();
  auxiliary_window.activateWindow();
  auxiliary_button.setFocus(Qt::OtherFocusReason);
  QCoreApplication::processEvents();
  ASSERT_EQ(QApplication::focusWidget(), &auxiliary_button);
  ASSERT_TRUE(quader::qt_window_owns_command_input(&auxiliary_window));

  send_key_press(auxiliary_button, Qt::Key_A, Qt::ControlModifier);

  EXPECT_EQ(editor.commands().last_status_revision(),
            status_revision_before_auxiliary)
      << "an active auxiliary window must not execute a global command";

  application.removeEventFilter(&command_filter);
  auxiliary_window.hide();
}

TEST(QtCommandEventFilterTests,
     ViewportFocusedSaveCompletesBeforeAFollowingWindowCloseCheck) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-command-viewport-save-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));
  application.setQuitOnLastWindowClosed(false);

  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const std::filesystem::path scene_path =
      std::filesystem::path(temporary.path().toStdWString()) /
      "viewport-save.qdr";

  quader::QuaderStandaloneEditor standalone =
      quader::create_standalone_editor();
  quader::QuaderEditorApi &editor = standalone.editor();
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  const auto initial = editor.modeling().create_box(
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      "Initial Saved Object");
  ASSERT_TRUE(initial.ok()) << initial.error_message();
  const std::optional<quader::SceneSessionId> scene_id =
      editor.scene_documents().active_scene_id();
  ASSERT_TRUE(scene_id.has_value());
  ASSERT_TRUE(
      editor.scene_documents().save_as_native_path(*scene_id, scene_path).ok());
  ASSERT_FALSE(editor.project().dirty());

  const auto unsaved = editor.modeling().create_box(
      {.min = {2.0f, -1.0f, -1.0f}, .max = {4.0f, 1.0f, 1.0f}},
      "Unsaved Viewport Object");
  ASSERT_TRUE(unsaved.ok()) << unsaved.error_message();
  ASSERT_TRUE(editor.project().dirty());

  quader::QtCommandEventFilter command_filter(editor, {});
  application.installEventFilter(&command_filter);
  quader::QtViewportSurface surface(quader::ViewportPaneId::Perspective);

  QKeyEvent save_event(QEvent::KeyPress, Qt::Key_S, Qt::ControlModifier,
                       QStringLiteral("s"));
  QCoreApplication::sendEvent(&surface, &save_event);

  EXPECT_FALSE(editor.project().dirty())
      << "Ctrl+S must finish synchronously before a following close event";
  EXPECT_TRUE(std::filesystem::exists(scene_path));
  const quader::QtViewportInputFrame queued_input =
      surface.take_input_frame(0.0f, false);
  EXPECT_TRUE(queued_input.input.pressed_input_chords.empty())
      << "the synchronously dispatched global command must not run again on "
         "the next viewport frame";

  application.removeEventFilter(&command_filter);
}

TEST(QtCommandEventFilterTests,
     CloseSceneRebindKeepsTheDispatchingApplicationFilterAlive) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-command-close-rebind-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));
  application.setQuitOnLastWindowClosed(false);

  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const std::filesystem::path project_root =
      std::filesystem::path(temporary.path().toStdWString()) / "Project";
  std::error_code directory_error;
  ASSERT_TRUE(std::filesystem::create_directory(project_root, directory_error))
      << directory_error.message();

  quader::ProjectSession project;
  const quader::ProjectSessionResult created = project.create(project_root);
  ASSERT_TRUE(created.ok) << created.message;
  const quader::ProjectSessionResult second = project.new_scene();
  ASSERT_TRUE(second.ok) << second.message;
  ASSERT_EQ(project.open_scenes().size(), 2u);

  quader::QuaderEditorApi editor(project);
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  quader::QtCommandEventFilter command_filter(editor, {});
  QPointer<quader::QtCommandEventFilter> retained_filter(&command_filter);
  int synchronous_rebinds = 0;
  quader::QuaderEditorApiSessionAccess::set_scene_binding_changed_callback(
      editor, [&](quader::SceneSession &) {
        ++synchronous_rebinds;
        command_filter.rebind_editor(editor);
        EXPECT_EQ(retained_filter.data(), &command_filter);
      });
  application.installEventFilter(&command_filter);

  QWidget editor_surface;
  editor_surface.setFocusPolicy(Qt::StrongFocus);
  editor_surface.show();
  editor_surface.activateWindow();
  editor_surface.setFocus(Qt::OtherFocusReason);
  QCoreApplication::processEvents();
  ASSERT_EQ(QApplication::focusWidget(), &editor_surface);

  send_key_press(editor_surface, Qt::Key_W, Qt::ControlModifier,
                 QStringLiteral("w"));

  EXPECT_EQ(synchronous_rebinds, 1);
  EXPECT_EQ(project.open_scenes().size(), 1u);
  EXPECT_EQ(retained_filter.data(), &command_filter);
  EXPECT_TRUE(editor.commands().last_status().has_value());
  EXPECT_TRUE(editor.commands().last_status()->ok());

  application.removeEventFilter(&command_filter);
  editor_surface.hide();
}

} // namespace
