////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "api/quader_command_ids.hpp"
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "core/localization_service.hpp"
#include "editor/session/project_session.hpp"
#include "editor/session/quader_editor_api_session_access.hpp"
#include "editor/session/scene_session.hpp"
#include "ui/outliner_panel.hpp"
#include "ui/qt_command_event_filter.hpp"
#include "ui/qt_viewport_surface.hpp"
#include "ui/ui_context.hpp"
#include "ui/ui_settings.hpp"
#include "viewport/viewport_settings.hpp"

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QEnterEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QPointer>
#include <QPushButton>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QTreeView>
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
                  .remove_shortcut(
                      quader::quader_command::kToolsTogglePrimitiveHemisphere)
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

/// Supplies the real retained Outliner with a standalone editor's live owners.
struct OutlinerCommandContext {
  quader::UISettings ui_settings;
  quader::ViewportSettings viewport_settings;
  quader::LocalizationService localization;

  void refresh(quader::OutlinerPanel &panel, quader::QuaderEditorApi &editor,
               quader::SceneSession &session) {
    quader::UIContext context{
        .intent = {editor},
        .scene = {session.scene(), session.assets(), session.modeling(),
                  session.selection()},
        .preferences = {ui_settings, viewport_settings},
        .localization = localization,
    };
    panel.refresh(context);
  }
};

TEST(QtCommandEventFilterTests,
     OutlinerFocusRoutesSceneCommandsAndCustomSequencesWithoutTreeSearch) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-outliner-command-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  const auto first = editor.scene().create_entity({.name = "First", .kind = quader::QuaderEntityKind::Empty});
  const auto second = editor.scene().create_entity({.name = "Second", .kind = quader::QuaderEntityKind::Empty});
  ASSERT_TRUE(first.ok());
  ASSERT_TRUE(second.ok());
  ASSERT_TRUE(editor.selection().select_objects_for_inspection(
      {first.value().id, second.value().id}).ok());
  auto &document = session.scene();
  OutlinerCommandContext context;
  quader::OutlinerPanel outliner;
  context.refresh(outliner, editor, session);
  outliner.resize(340, 400);
  outliner.show();
  outliner.activateWindow();
  auto *tree = outliner.findChild<QTreeView *>(QStringLiteral("outliner.tree"));
  ASSERT_NE(tree, nullptr);
  tree->setFocus(Qt::OtherFocusReason);
  QCoreApplication::processEvents();
  ASSERT_EQ(QApplication::focusWidget(), tree);
  quader::QtCommandEventFilter filter(editor, {});
  filter.set_outliner_input_owner_provider([&outliner]() {
    return quader::qt_outliner_owns_command_input(&outliner);
  });
  application.installEventFilter(&filter);

  send_key_press(*tree, Qt::Key_G, Qt::ControlModifier, QStringLiteral("g"));
  const auto group = document.parent(first.value().id);
  ASSERT_NE(group, document.scene_root());
  EXPECT_EQ(document.parent(second.value().id), group);
  send_key_press(*tree, Qt::Key_G,
                 Qt::ControlModifier | Qt::ShiftModifier, QStringLiteral("G"));
  EXPECT_FALSE(document.exists(group));
  EXPECT_EQ(document.parent(first.value().id), document.scene_root());
  EXPECT_EQ(document.parent(second.value().id), document.scene_root());

  send_key_press(*tree, Qt::Key_Delete, Qt::NoModifier);
  EXPECT_FALSE(document.exists(first.value().id));
  EXPECT_FALSE(document.exists(second.value().id));
  send_key_press(*tree, Qt::Key_Z, Qt::ControlModifier, QStringLiteral("z"));
  EXPECT_TRUE(document.exists(first.value().id));
  EXPECT_TRUE(document.exists(second.value().id));

  ASSERT_TRUE(editor.commands().set_shortcut(
      quader::quader_command::kDeleteSelection, "K D").ok());
  const auto status_before = editor.commands().last_status_revision();
  send_key_press(*tree, Qt::Key_K, Qt::NoModifier, QStringLiteral("k"));
  ASSERT_TRUE(editor.commands().has_pending_keybinding_chord());
  EXPECT_EQ(editor.commands().last_status_revision(), status_before);
  // Idle viewport frames may clear their own input while this tree owns K D.
  editor.commands().reset_pending_viewport_keybinding_chord();
  ASSERT_TRUE(editor.commands().has_pending_keybinding_chord());
  send_key_press(*tree, Qt::Key_D, Qt::NoModifier, QStringLiteral("d"));
  EXPECT_FALSE(editor.commands().has_pending_keybinding_chord());
  EXPECT_FALSE(document.exists(first.value().id));
  EXPECT_FALSE(document.exists(second.value().id));
  send_key_press(*tree, Qt::Key_Z, Qt::ControlModifier, QStringLiteral("z"));
  ASSERT_TRUE(document.exists(first.value().id));

  ASSERT_TRUE(editor.commands().remove_shortcut(
      quader::quader_command::kDeleteSelection).ok());
  send_key_press(*tree, Qt::Key_Delete, Qt::NoModifier);
  EXPECT_TRUE(document.exists(first.value().id));
  EXPECT_TRUE(document.exists(second.value().id));
  application.removeEventFilter(&filter);
}

TEST(QtCommandEventFilterTests,
     OutlinerHoverPreemptsViewportQueueButTextAndPopupKeepInput) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-outliner-hover-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  const auto object = editor.scene().create_entity({.name = "Hover Object", .kind = quader::QuaderEntityKind::Empty});
  ASSERT_TRUE(object.ok());
  ASSERT_TRUE(editor.selection().select_objects_for_inspection(
      {object.value().id}).ok());
  auto &document = session.scene();
  QWidget editor_window;
  editor_window.resize(660, 420);
  quader::OutlinerPanel outliner(&editor_window);
  outliner.setGeometry(0, 0, 320, 400);
  QWidget viewport_container(&editor_window);
  viewport_container.setObjectName(QStringLiteral("viewportSurfaceContainer_test"));
  viewport_container.setFocusPolicy(Qt::StrongFocus);
  viewport_container.setGeometry(340, 0, 300, 300);
  QLineEdit inspector_text(&editor_window);
  inspector_text.setGeometry(340, 320, 300, 30);
  OutlinerCommandContext context;
  context.refresh(outliner, editor, session);
  quader::QtViewportSurface surface(quader::ViewportPaneId::Perspective);
  quader::QtCommandEventFilter filter(editor, {});
  filter.set_outliner_input_owner_provider([&outliner]() {
    return quader::qt_outliner_owns_command_input(&outliner);
  });
  application.installEventFilter(&filter);
  editor_window.show();
  editor_window.activateWindow();
  viewport_container.setFocus(Qt::OtherFocusReason);
  QCoreApplication::processEvents();
  ASSERT_EQ(QApplication::focusWidget(), &viewport_container);
  QEnterEvent enter(QPointF(20, 30), QPointF(20, 30),
                    QPointF(outliner.mapToGlobal(QPoint(20, 30))));
  QCoreApplication::sendEvent(&outliner, &enter);
  ASSERT_TRUE(quader::qt_outliner_owns_command_input(&outliner));
  ASSERT_TRUE(editor.commands().set_shortcut(
      quader::quader_command::kDeleteSelection, "K D").ok());
  const auto status_before_sequence = editor.commands().last_status_revision();
  QKeyEvent prefix(QEvent::KeyPress, Qt::Key_K, Qt::NoModifier,
                    QStringLiteral("k"));
  QCoreApplication::sendEvent(&surface, &prefix);
  ASSERT_TRUE(editor.commands().has_pending_keybinding_chord());
  QKeyEvent mismatch(QEvent::KeyPress, Qt::Key_D, Qt::ControlModifier,
                      QStringLiteral("d"));
  QCoreApplication::sendEvent(&surface, &mismatch);
  EXPECT_FALSE(editor.commands().has_pending_keybinding_chord());
  EXPECT_EQ(editor.commands().last_status_revision(), status_before_sequence);
  EXPECT_TRUE(surface.take_input_frame(0.0f, false).input.pressed_input_chords.empty());
  ASSERT_TRUE(editor.commands().reset_shortcut(
      quader::quader_command::kDeleteSelection).ok());
  QKeyEvent delete_event(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
  QCoreApplication::sendEvent(&surface, &delete_event);
  EXPECT_FALSE(document.exists(object.value().id));
  EXPECT_TRUE(surface.take_input_frame(0.0f, false).input.pressed_input_chords.empty());
  ASSERT_TRUE(editor.commands().execute(quader::quader_command::kEditUndo).ok());
  ASSERT_TRUE(document.exists(object.value().id));

  inspector_text.setText(QStringLiteral("Text"));
  inspector_text.setCursorPosition(0);
  inspector_text.setFocus(Qt::OtherFocusReason);
  QCoreApplication::processEvents();
  send_key_press(inspector_text, Qt::Key_Delete, Qt::NoModifier);
  EXPECT_EQ(inspector_text.text(), QStringLiteral("ext"));
  EXPECT_TRUE(document.exists(object.value().id));

  auto *tree = outliner.findChild<QTreeView *>(QStringLiteral("outliner.tree"));
  ASSERT_NE(tree, nullptr);
  context.refresh(outliner, editor, session);
  tree->setFocus(Qt::OtherFocusReason);
  QCoreApplication::processEvents();
  send_key_press(*tree, Qt::Key_F2, Qt::NoModifier);
  QCoreApplication::processEvents();
  auto *rename_editor = tree->findChild<QLineEdit *>();
  ASSERT_NE(rename_editor, nullptr);
  ASSERT_TRUE(rename_editor->hasFocus());
  rename_editor->setText(QStringLiteral("Rename"));
  rename_editor->setCursorPosition(0);
  send_key_press(*rename_editor, Qt::Key_Delete, Qt::NoModifier);
  EXPECT_EQ(rename_editor->text(), QStringLiteral("ename"));
  EXPECT_TRUE(document.exists(object.value().id));
  send_key_press(*rename_editor, Qt::Key_Escape, Qt::NoModifier);
  QCoreApplication::processEvents();

  QMenu popup(&editor_window);
  popup.addAction(QStringLiteral("Keep popup input"));
  popup.popup(outliner.mapToGlobal(QPoint(20, 30)));
  QCoreApplication::processEvents();
  ASSERT_EQ(QApplication::activePopupWidget(), &popup);
  send_key_press(popup, Qt::Key_Delete, Qt::NoModifier);
  EXPECT_TRUE(document.exists(object.value().id));
  popup.close();
  application.removeEventFilter(&filter);
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
