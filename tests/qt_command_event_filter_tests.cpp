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
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/session/project_session.hpp"
#include "editor/session/quader_editor_api_session_access.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/tools/extrude_tool_service.hpp"
#include "ui/outliner_panel.hpp"
#include "ui/qt_command_event_filter.hpp"
#include "ui/qt_apply_cancel_input_filter.hpp"
#include "ui/qt_face_texture_panel.hpp"
#include "ui/qt_face_texture_canvas.hpp"
#include "ui/qt_material_browser_window.hpp"
#include "ui/qt_numeric_spin_box.hpp"
#include "ui/qt_viewport_panel.hpp"
#include "ui/qt_tool_settings_panel.hpp"
#include "ui/qt_tool_toolbar.hpp"
#include "ui/qt_viewport_surface.hpp"
#include "ui/ui_context.hpp"
#include "ui/ui_settings.hpp"
#include "viewport/viewport_settings.hpp"
#include "viewport/host_viewport_contract_adapter.hpp"

#include <QApplication>
#include <QAbstractButton>
#include <QComboBox>
#include <QCoreApplication>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QMainWindow>
#include <QScrollArea>
#include <QWindow>
#include <QEnterEvent>
#include <QEventLoop>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLineEdit>
#include <QLocale>
#include <QListView>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QTabBar>
#include <QToolButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeView>
#include <QWidget>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <utility>
#include <vector>

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
     InsertEdgeLoopToolbarActivationCancelsFromEditorFocusWithCanonicalMapping) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-loop-toolbar-cancel-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  for (const int mapping : {0, 1, 2}) {
    SCOPED_TRACE(mapping);
    quader::SceneSession session;
    quader::QuaderEditorApi editor(
        quader::QuaderApiSessionInternalAccess::from_scene_session(session));
    ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
    if (mapping == 1) {
      ASSERT_TRUE(editor.commands().set_shortcut(
          "viewport.cancel_active_tool", "F18").ok());
    } else if (mapping == 2) {
      ASSERT_TRUE(editor.commands().remove_shortcut("viewport.cancel_active_tool").ok());
    }
    const auto box = editor.modeling().create_box(
        {.min = {-1, -1, -1}, .max = {1, 1, 1}}, "Toolbar loop box");
    ASSERT_TRUE(box.ok()) << box.error_message();
    ASSERT_TRUE(editor.selection().select_edge(box.value(), 1, 2).ok());
    session.history().clear();
    ASSERT_TRUE(editor.scene().rename(box.value().entity, "Redo toolbar loop box").ok());
    ASSERT_TRUE(editor.history().undo().ok());
    const auto content_before = editor.selection().summary().content_revision;

    QMainWindow window;
    QWidget panel(&window);
    panel.setFocusPolicy(Qt::StrongFocus);
    window.setCentralWidget(&panel);
    quader::QtToolToolbar toolbar(&window);
    window.addToolBar(&toolbar);
    toolbar.set_tool_requested_callback([&](std::string_view tool_id) {
      const auto activated = editor.tools().activate(tool_id);
      EXPECT_TRUE(activated.ok()) << activated.error_message();
    });
    auto *button = qobject_cast<QToolButton *>(toolbar.widgetForAction(
        toolbar.tool_action(quader::ViewportToolId::InsertEdgeLoop)));
    ASSERT_NE(button, nullptr);
    window.resize(700, 400);
    window.show();
    window.activateWindow();
    panel.setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    ASSERT_EQ(QApplication::focusWidget(), &panel);

    quader::QtCommandEventFilter filter(editor, [&] {
      return quader::ViewportToolState{
          .active_tool = editor.viewports().selected_tool(),
          .selection_mode = quader::ViewportSelectionMode::Edge};
    });
    filter.set_input_context_provider([&] {
      return quader::QuaderViewportKeybindingContext{
          .modal_tool_active = session.modeling().modal_tool_owns_viewport()};
    });
    application.installEventFilter(&filter);
    const QPointF local = button->rect().center();
    const QPointF global = button->mapToGlobal(local.toPoint());
    QMouseEvent press(QEvent::MouseButtonPress, local, global,
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, local, global,
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(button, &press);
    QApplication::sendEvent(button, &release);
    ASSERT_EQ(editor.viewports().selected_tool(), quader::ViewportToolId::InsertEdgeLoop);
    ASSERT_EQ(editor.tools().active_tool(), "insert_edge_loop");
    const auto selected_before = editor.selection().summary();
    const auto key = [&](int value, bool repeat = false) {
      auto *focused = QApplication::focusWidget();
      ASSERT_NE(focused, nullptr);
      QKeyEvent key_press(QEvent::KeyPress, value, Qt::NoModifier, {}, repeat);
      QApplication::sendEvent(focused, &key_press);
    };
    const auto key_release = [&](int value) {
      auto *focused = QApplication::focusWidget();
      ASSERT_NE(focused, nullptr);
      QKeyEvent event(QEvent::KeyRelease, value, Qt::NoModifier);
      QApplication::sendEvent(focused, &event);
    };
    key(Qt::Key_Escape);
    if (mapping != 0) {
      EXPECT_EQ(editor.tools().active_tool(), "insert_edge_loop");
      key_release(Qt::Key_Escape);
      key(Qt::Key_F18);
    }
    if (mapping == 2) {
      EXPECT_EQ(editor.tools().active_tool(), "insert_edge_loop");
    } else {
      EXPECT_EQ(editor.tools().active_tool(), "select");
      EXPECT_EQ(editor.viewports().selected_tool(), quader::ViewportToolId::Select);
      key(mapping == 0 ? Qt::Key_Escape : Qt::Key_F18, true);
      EXPECT_EQ(editor.selection().summary().selected_edge_count,
                selected_before.selected_edge_count);
    }
    key_release(mapping == 0 ? Qt::Key_Escape : Qt::Key_F18);
    const auto after = editor.selection().summary();
    EXPECT_EQ(after.mode, quader::QuaderSelectionMode::Edge);
    EXPECT_EQ(after.active_edge_a, selected_before.active_edge_a);
    EXPECT_EQ(after.active_edge_b, selected_before.active_edge_b);
    EXPECT_EQ(after.selection_revision, selected_before.selection_revision);
    EXPECT_EQ(after.content_revision, content_before);
    EXPECT_EQ(session.history().undo_count(), 0u);
    EXPECT_EQ(session.history().redo_count(), 1u);

    ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Knife).ok());
    key(Qt::Key_Escape);
    key_release(Qt::Key_Escape);
    EXPECT_EQ(editor.tools().active_tool(), "knife");
    application.removeEventFilter(&filter);
  }
}

TEST(QtCommandEventFilterTests,
     AssetBrowserOwnsRemappedClipboardAndToggleWithoutEditingTheScene) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-asset-browser-command-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  quader::ProjectSession project;
  quader::QuaderEditorApi editor(project);
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  EXPECT_FALSE(editor.ui().asset_browser_available());
  EXPECT_FALSE(editor.ui().toggle_asset_browser().ok());
  int toggles = 0;
  editor.ui().set_runtime_hooks({
      .toggle_asset_browser = [&toggles] {
        ++toggles;
        return quader::QuaderApiVoidResult{
            .ok = true, .changed = true,
            .mutation_kind = quader::QuaderMutationKind::Runtime};
      }});
  EXPECT_TRUE(editor.ui().asset_browser_available());
  EXPECT_FALSE(editor.commands().descriptor(
      quader::quader_command::kUiAssetBrowser)->enabled);
  const auto project_root =
      std::filesystem::path(temporary.path().toStdWString()) / "Project";
  ASSERT_TRUE(std::filesystem::create_directory(project_root));
  ASSERT_TRUE(editor.project().create(project_root).ok());
  EXPECT_TRUE(editor.commands().descriptor(
      quader::quader_command::kUiAssetBrowser)->enabled);
  const auto box = editor.modeling().create_box(
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      "Preserved Scene Box");
  ASSERT_TRUE(box.ok());
  ASSERT_TRUE(editor.selection().select_object(box.value()).ok());
  const auto active_scene = editor.scene_documents().active_scene_id();
  ASSERT_TRUE(active_scene.has_value());
  const auto scene_path = editor.project().root() / "Scenes" / "Input.qdr";
  ASSERT_TRUE(editor.scene_documents().save_as_native_path(
      *active_scene, scene_path).ok());
  ASSERT_TRUE(editor.scene().rename(box.value().entity, "Unsaved Scene Name").ok());
  ASSERT_TRUE(editor.project().dirty());

  std::vector<std::string> asset_actions;
  quader::QtCommandEventFilter filter(editor, [] {
    return quader::ViewportToolState{
        .selection_mode = quader::ViewportSelectionMode::Vertex};
  });
  filter.set_asset_browser_action_handler(
      [&asset_actions](quader::QuaderCommandId command) {
        asset_actions.emplace_back(command.string());
        return true;
      });
  application.installEventFilter(&filter);
  QWidget window;
  window.resize(400, 280);
  QWidget asset_panel(&window);
  asset_panel.setGeometry(0, 0, 400, 240);
  asset_panel.setProperty("quaderAssetBrowserInput", true);
  QTreeWidget tree(&asset_panel);
  tree.setGeometry(0, 0, 400, 190);
  tree.setSelectionMode(QAbstractItemView::ExtendedSelection);
  tree.addTopLevelItem(new QTreeWidgetItem({QStringLiteral("One")}));
  tree.addTopLevelItem(new QTreeWidgetItem({QStringLiteral("Two")}));
  QLineEdit text_editor(&asset_panel);
  text_editor.setGeometry(0, 195, 400, 32);
  QWidget other_panel(&window);
  other_panel.setGeometry(0, 245, 400, 30);
  other_panel.setFocusPolicy(Qt::StrongFocus);
  window.show();
  window.activateWindow();
  tree.setFocus(Qt::OtherFocusReason);
  QCoreApplication::processEvents();
  ASSERT_EQ(QApplication::focusWidget(), &tree);
  send_key_press(tree, Qt::Key_Space, Qt::ControlModifier);
  EXPECT_EQ(toggles, 1);
  for (const int key : {Qt::Key_X, Qt::Key_C, Qt::Key_V, Qt::Key_D}) {
    send_key_press(tree, key, Qt::ControlModifier);
  }
  send_key_press(tree, Qt::Key_Delete, Qt::NoModifier);
  EXPECT_EQ(asset_actions, (std::vector<std::string>{
      "edit.cut", "edit.copy", "edit.paste", "edit.duplicate", "edit.delete"}));
  EXPECT_EQ(editor.query().modeling_objects().size(), 1u);
  const auto status_before = editor.commands().last_status_revision();
  send_key_press(tree, Qt::Key_Z, Qt::ControlModifier);
  send_key_press(tree, Qt::Key_B, Qt::ShiftModifier);
  EXPECT_EQ(editor.commands().last_status_revision(), status_before);
  send_key_press(tree, Qt::Key_S, Qt::ControlModifier);
  EXPECT_FALSE(editor.project().dirty());
  EXPECT_EQ(asset_actions.size(), 5u);
  send_key_press(tree, Qt::Key_A, Qt::ControlModifier);
  EXPECT_EQ(tree.selectedItems().size(), 2);

  text_editor.setText(QStringLiteral("Material"));
  text_editor.setFocus(Qt::OtherFocusReason);
  QCoreApplication::processEvents();
  send_key_press(text_editor, Qt::Key_A, Qt::ControlModifier);
  EXPECT_EQ(text_editor.selectedText(), QStringLiteral("Material"));
  send_key_press(text_editor, Qt::Key_C, Qt::ControlModifier);
  EXPECT_EQ(asset_actions.size(), 5u);
  send_key_press(text_editor, Qt::Key_T, Qt::NoModifier, QStringLiteral("t"));
  EXPECT_EQ(text_editor.text(), QStringLiteral("t"));
  send_key_press(text_editor, Qt::Key_Space, Qt::ControlModifier);
  EXPECT_EQ(toggles, 2);
  EXPECT_EQ(text_editor.text(), QStringLiteral("t"));
  ASSERT_TRUE(editor.scene().rename(box.value().entity, "Another Scene Name").ok());
  ASSERT_TRUE(editor.project().dirty());
  send_key_press(text_editor, Qt::Key_S, Qt::ControlModifier);
  EXPECT_FALSE(editor.project().dirty());
  EXPECT_EQ(QApplication::focusWidget(), &text_editor);
  EXPECT_EQ(text_editor.text(), QStringLiteral("t"));
  EXPECT_EQ(asset_actions.size(), 5u);

  ASSERT_TRUE(editor.commands().set_shortcut(
      quader::quader_command::kUiAssetBrowser, "Ctrl+K Ctrl+B").ok());
  tree.setFocus(Qt::OtherFocusReason);
  QCoreApplication::processEvents();
  QKeyEvent prefix_override(QEvent::ShortcutOverride, Qt::Key_K,
                            Qt::ControlModifier);
  prefix_override.ignore();
  QCoreApplication::sendEvent(&tree, &prefix_override);
  EXPECT_TRUE(prefix_override.isAccepted());
  EXPECT_FALSE(editor.commands().has_pending_keybinding_chord());
  send_key_press(tree, Qt::Key_K, Qt::ControlModifier);
  EXPECT_TRUE(editor.commands().has_pending_keybinding_chord());
  send_key_press(tree, Qt::Key_B, Qt::ControlModifier);
  EXPECT_EQ(toggles, 3);
  EXPECT_FALSE(editor.commands().has_pending_keybinding_chord());
  send_key_press(tree, Qt::Key_Space, Qt::ControlModifier);
  EXPECT_EQ(toggles, 3);

  ASSERT_TRUE(editor.commands().set_shortcut(
      quader::quader_command::kDeleteSelection, "Alt+F11").ok());
  send_key_press(tree, Qt::Key_Delete, Qt::NoModifier);
  EXPECT_EQ(asset_actions.size(), 5u);
  send_key_press(tree, Qt::Key_F11, Qt::AltModifier);
  ASSERT_EQ(asset_actions.size(), 6u);
  EXPECT_EQ(asset_actions.back(), "edit.delete");
  filter.set_asset_browser_action_handler({});
  send_key_press(tree, Qt::Key_F11, Qt::AltModifier);
  EXPECT_EQ(editor.query().modeling_objects().size(), 1u);

  ASSERT_TRUE(editor.commands().remove_shortcut(
      quader::quader_command::kUiAssetBrowser).ok());
  send_key_press(tree, Qt::Key_K, Qt::ControlModifier);
  EXPECT_FALSE(editor.commands().has_pending_keybinding_chord());
  EXPECT_EQ(toggles, 3);
  application.removeEventFilter(&filter);
}

TEST(QtCommandEventFilterTests,
     NativeViewportReservesAltModeShortcutsBeforeMenuMnemonics) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-viewport-menu-mnemonic-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  const auto box = editor.modeling().create_box(
      {.min = {-1.0F, -1.0F, -1.0F}, .max = {1.0F, 1.0F, 1.0F}},
      "Mnemonic Extrusion Source");
  ASSERT_TRUE(box.ok());
  ASSERT_TRUE(editor.selection().select_object(box.value()).ok());
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection().select_face(box.value(), 1).ok());
  const auto tool_state = [&editor]() {
    quader::ViewportToolState state{
        .selection_mode = quader::ViewportSelectionMode::Face};
    EXPECT_TRUE(quader::viewport_tool_from_contract_id(
        editor.tools().active_tool(), state.active_tool));
    return state;
  };
  quader::QtCommandEventFilter filter(editor, tool_state);
  application.installEventFilter(&filter);

  QMainWindow window;
  window.resize(480, 320);
  QMenu *edit_menu = window.menuBar()->addMenu(QStringLiteral("&Edit"));
  edit_menu->addAction(QStringLiteral("Edit Action"));
  EXPECT_EQ(QKeySequence::mnemonic(edit_menu->title()),
            QKeySequence(QStringLiteral("Alt+E")));
  int menu_openings = 0;
  QObject::connect(edit_menu, &QMenu::aboutToShow, [&menu_openings]() {
    ++menu_openings;
  });
  auto *surface = new quader::QtViewportSurface(quader::ViewportPaneId::Perspective);
  QWidget *container = QWidget::createWindowContainer(surface, &window);
  container->setObjectName(QStringLiteral("viewportSurfaceContainer_mnemonic"));
  container->setFocusPolicy(Qt::StrongFocus);
  container->installEventFilter(surface);
  window.setCentralWidget(container);
  window.show();
  window.activateWindow();
  container->setFocus(Qt::OtherFocusReason);
  QCoreApplication::processEvents();

  for (const bool native_surface : {true, false}) {
    SCOPED_TRACE(native_surface);
    const int key = native_surface ? Qt::Key_E : Qt::Key_R;
    if (!native_surface) {
      ASSERT_TRUE(editor.commands().set_shortcut(
          quader::quader_command::kToolsActivateExtrudeAlongNormals,
          "Alt+R").ok());
      edit_menu->setTitle(QStringLiteral("&Region"));
    }
    QObject *receiver = native_surface ? static_cast<QObject *>(surface)
                                      : static_cast<QObject *>(container);
    // This is Qt's negotiation before shortcut/mnemonic activation. Native
    // child-window focus must reserve the chord just like its widget proxy.
    QKeyEvent override_event(QEvent::ShortcutOverride, key, Qt::AltModifier);
    override_event.ignore();
    QCoreApplication::sendEvent(receiver, &override_event);
    ASSERT_TRUE(override_event.isAccepted());
    const auto before = editor.query().compiled_object_mesh(box.value());
    ASSERT_TRUE(before.has_value());
    const auto history_before = session.history().undo_count();
    QKeyEvent press(QEvent::KeyPress, key, Qt::AltModifier);
    QCoreApplication::sendEvent(receiver, &press);
    const auto frame = surface->take_input_frame(0.0F, false);
    ASSERT_EQ(frame.input.pressed_input_chords.size(), 1u);
    const auto dispatch = editor.commands().dispatch_viewport_input_binding(
        frame.input.pressed_input_chords.front(), tool_state(), true, false);
    ASSERT_EQ(dispatch.kind,
              quader::QuaderViewportKeybindingDispatch::Kind::Command);
    ASSERT_EQ(dispatch.command_id,
              quader::quader_command::kToolsActivateExtrudeAlongNormals.string());
    const double remembered_amount = session.extrude().amount();
    ASSERT_TRUE(editor.commands().execute(dispatch.command_id).ok());
    const auto after = editor.query().compiled_object_mesh(box.value());
    ASSERT_TRUE(after.has_value());
    if (remembered_amount == 0.0) {
      EXPECT_EQ(after->indices, before->indices);
      EXPECT_EQ(after->vertices.size(), before->vertices.size());
    } else {
      EXPECT_GT(after->indices.size(), before->indices.size());
    }
    EXPECT_EQ(editor.tools().active_tool(), "extrude");
    EXPECT_TRUE(editor.tools().active_settings().preview_active);
    EXPECT_DOUBLE_EQ(session.extrude().amount(), remembered_amount);
    EXPECT_EQ(session.history().undo_count(), history_before);
    ASSERT_TRUE(editor.tools().set_setting("extrude", "extrude.amount", 1.0).ok());
    const auto preview = editor.query().compiled_object_mesh(box.value());
    ASSERT_TRUE(preview.has_value());
    EXPECT_GT(preview->indices.size(), before->indices.size());
    ASSERT_TRUE(editor.tools().commit_active_tool_preview().ok());
    EXPECT_FALSE(editor.tools().active_settings().preview_active);
    EXPECT_DOUBLE_EQ(session.extrude().amount(), 1.0);
    EXPECT_EQ(session.history().undo_count(), history_before + 1u);
    EXPECT_EQ(menu_openings, 0);
    QKeyEvent release(QEvent::KeyRelease, key, Qt::AltModifier);
    QCoreApplication::sendEvent(receiver, &release);
    (void)surface->take_input_frame(0.0F, false);
  }

  QKeyEvent close_override(QEvent::ShortcutOverride, Qt::Key_F4, Qt::AltModifier);
  close_override.ignore();
  QCoreApplication::sendEvent(surface, &close_override);
  EXPECT_FALSE(close_override.isAccepted());
  QLineEdit text_editor(&window);
  text_editor.show();
  text_editor.setFocus(Qt::OtherFocusReason);
  QCoreApplication::processEvents();
  QKeyEvent text_override(QEvent::ShortcutOverride, Qt::Key_R, Qt::AltModifier);
  text_override.ignore();
  QCoreApplication::sendEvent(&text_editor, &text_override);
  EXPECT_FALSE(text_override.isAccepted());
  const auto command_revision = editor.commands().last_status_revision();
  send_key_press(text_editor, Qt::Key_R, Qt::AltModifier);
  EXPECT_EQ(editor.commands().last_status_revision(), command_revision);
  application.removeEventFilter(&filter);
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
  ASSERT_NE(group, quader::kInvalidEntityId);
  EXPECT_EQ(document.parent(second.value().id), group);
  send_key_press(*tree, Qt::Key_G,
                 Qt::ControlModifier | Qt::ShiftModifier, QStringLiteral("G"));
  EXPECT_FALSE(document.exists(group));
  EXPECT_EQ(document.parent(first.value().id), quader::kInvalidEntityId);
  EXPECT_EQ(document.parent(second.value().id), quader::kInvalidEntityId);

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
     FocusedTextDraftSettlesBeforeRemappedMultiChordSaveAllSerializes) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-command-text-save-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));
  application.setQuitOnLastWindowClosed(false);

  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const std::filesystem::path scene_path =
      std::filesystem::path(temporary.path().toStdWString()) /
      "focused-text-save.qdr";

  quader::QuaderStandaloneEditor standalone =
      quader::create_standalone_editor();
  quader::QuaderEditorApi &editor = standalone.editor();
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  const auto object = editor.modeling().create_box(
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      "Before Focused Draft");
  ASSERT_TRUE(object.ok()) << object.error_message();
  const std::optional<quader::SceneSessionId> scene_id =
      editor.scene_documents().active_scene_id();
  ASSERT_TRUE(scene_id.has_value());
  ASSERT_TRUE(
      editor.scene_documents().save_as_native_path(*scene_id, scene_path).ok());
  ASSERT_FALSE(editor.project().dirty());

  // Exercise command identity instead of relying on the physical default.
  ASSERT_TRUE(editor.commands()
                  .set_shortcut(quader::quader_command::kSceneDocumentSaveAll,
                                "Ctrl+Alt+K Ctrl+Alt+L")
                  .ok());
  quader::QtCommandEventFilter command_filter(editor, {});
  application.installEventFilter(&command_filter);

  QWidget editor_window;
  editor_window.resize(320, 80);
  QLineEdit name_editor(&editor_window);
  name_editor.setGeometry(0, 0, 300, 32);
  int committed_drafts = 0;
  bool rename_succeeded = false;
  QObject::connect(&name_editor, &QLineEdit::editingFinished, &editor_window,
                   [&]() {
                     ++committed_drafts;
                     rename_succeeded =
                         editor.scene()
                             .rename(object.value().entity,
                                     name_editor.text().toStdString())
                             .ok();
                   });

  editor_window.show();
  editor_window.activateWindow();
  name_editor.setFocus(Qt::OtherFocusReason);
  QCoreApplication::processEvents();
  ASSERT_EQ(QApplication::focusWidget(), &name_editor);
  name_editor.insert(QStringLiteral("Committed Before Save"));
  name_editor.setSelection(0, QStringLiteral("Committed").size());
  ASSERT_FALSE(editor.project().dirty())
      << "the focused draft has not committed yet";

  send_key_press(name_editor, Qt::Key_K,
                 Qt::ControlModifier | Qt::AltModifier,
                 QStringLiteral("k"));
  EXPECT_TRUE(editor.commands().has_pending_keybinding_chord());
  EXPECT_EQ(committed_drafts, 0)
      << "a save prefix must not settle the editor before it resolves";
  EXPECT_FALSE(editor.project().dirty());

  send_key_press(name_editor, Qt::Key_L,
                 Qt::ControlModifier | Qt::AltModifier,
                 QStringLiteral("l"));

  ASSERT_EQ(committed_drafts, 1);
  ASSERT_TRUE(rename_succeeded);
  EXPECT_FALSE(editor.project().dirty())
      << "the focus settlement and save must complete in one key event";
  EXPECT_EQ(QApplication::focusWidget(), &name_editor)
      << "a surviving main-window editor should regain focus after save";
  EXPECT_EQ(name_editor.selectedText(), QStringLiteral("Committed"))
      << "settling a save must not collapse the editor's text selection";

  quader::QuaderStandaloneEditor reopened =
      quader::create_standalone_editor();
  ASSERT_TRUE(reopened.editor()
                  .scene_documents()
                  .open_native_path(scene_path)
                  .ok());
  bool saved_name_found = false;
  for (const quader::QuaderEntitySnapshot &entity :
       reopened.editor().query().entities()) {
    saved_name_found |= entity.name == "Committed Before Save";
  }
  EXPECT_TRUE(saved_name_found)
      << "the save must serialize the mutation published by editingFinished";

  name_editor.setText(QStringLiteral("Undo stays native"));
  name_editor.setCursorPosition(name_editor.text().size());
  name_editor.insert(QStringLiteral(" draft"));
  ASSERT_EQ(name_editor.text(), QStringLiteral("Undo stays native draft"));
  send_key_press(name_editor, Qt::Key_Z, Qt::ControlModifier,
                 QStringLiteral("z"));
  EXPECT_EQ(name_editor.text(), QStringLiteral("Undo stays native"))
      << "ordinary text Ctrl+Z must remain owned by QLineEdit";

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

TEST(QtCommandEventFilterTests,
     ScopedLifecycleSequencesSurviveEitherApplicationFilterOrder) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-scoped-lifecycle-filter-order-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  for (const auto flags : {Qt::Dialog, Qt::Popup, Qt::Tool}) {
    for (const bool global_first : {false, true}) {
      SCOPED_TRACE(static_cast<int>(flags));
      SCOPED_TRACE(global_first);
      auto standalone = quader::create_standalone_editor();
      auto &editor = standalone.editor();
      ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
      ASSERT_TRUE(editor.preferences().set_shortcut(
          "viewport.modal_tool.finalize", "Ctrl+Alt+F24 G").ok());
      QWidget surface(nullptr, flags);
      surface.setAttribute(Qt::WA_DontShowOnScreen, true);
      if (flags == Qt::Dialog) {
        surface.setWindowModality(Qt::ApplicationModal);
      }
      QPushButton apply(QStringLiteral("Apply"), &surface);
      QPushButton cancel(QStringLiteral("Cancel"), &surface);
      cancel.move(0, 40);
      int applies = 0;
      int cancels = 0;
      QObject::connect(&apply, &QPushButton::clicked, &surface,
                       [&] { ++applies; });
      QObject::connect(&cancel, &QPushButton::clicked, &surface,
                       [&] { ++cancels; });
      quader::QtCommandEventFilter global_filter(editor, {});
      global_filter.set_command_input_owner_provider(
          [&] { return flags == Qt::Tool && surface.isVisible(); });
      application.installEventFilter(&global_filter);
      quader::QtApplyCancelInputFilter scoped_filter(
          surface, apply, cancel, [&] { return &editor.commands(); });
      if (global_first) {
        application.removeEventFilter(&global_filter);
        application.installEventFilter(&global_filter);
      }
      surface.show();
      surface.activateWindow();
      QCoreApplication::processEvents();
      QApplication::setActiveWindow(&surface);
      cancel.setFocus(Qt::OtherFocusReason);
      ASSERT_EQ(QApplication::focusWidget(), &cancel);
      for (const int key : {Qt::Key_F24, Qt::Key_G}) {
        const auto modifiers = key == Qt::Key_F24
            ? Qt::ControlModifier | Qt::AltModifier : Qt::NoModifier;
        QKeyEvent override_event(QEvent::ShortcutOverride, key, modifiers);
        override_event.ignore();
        QCoreApplication::sendEvent(&cancel, &override_event);
        EXPECT_TRUE(override_event.isAccepted());
        send_key_press(cancel, key, modifiers);
        QKeyEvent release(QEvent::KeyRelease, key, modifiers);
        QCoreApplication::sendEvent(&cancel, &release);
        EXPECT_EQ(editor.commands().has_pending_keybinding_chord(),
                  key == Qt::Key_F24);
      }
      EXPECT_EQ(applies, 1);
      EXPECT_EQ(cancels, 0);
      surface.hide();
      application.removeEventFilter(&global_filter);
    }
  }
}

TEST(QtCommandEventFilterTests,
     SettingsLifecycleKeysRespectNumericEditsForNativeAndOperationPreviews) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-settings-lifecycle-focus-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  for (const bool bevel : {false, true}) {
    for (const int key : {Qt::Key_Space, Qt::Key_Escape}) {
      for (const QString &focus_name : {
               QStringLiteral("number"), QStringLiteral("tool_settings.apply"),
               QStringLiteral("tool_settings.cancel"),
               QStringLiteral("tool_settings.target_title")}) {
        SCOPED_TRACE(bevel);
        SCOPED_TRACE(key);
        SCOPED_TRACE(focus_name.toStdString());
        quader::SceneSession session;
        quader::QuaderEditorApi editor(
            quader::QuaderApiSessionInternalAccess::from_scene_session(session));
        ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
        const auto box = editor.modeling().create_box(
            {.min = {-1, -1, -1}, .max = {1, 1, 1}});
        ASSERT_TRUE(box.ok());
        ASSERT_TRUE(editor.selection().select_object(box.value()).ok());
        ASSERT_TRUE(editor.selection().set_mode(
            bevel ? quader::QuaderSelectionMode::Edge
                  : quader::QuaderSelectionMode::Face).ok());
        ASSERT_TRUE((bevel ? editor.selection().select_edge(box.value(), 1, 2)
                           : editor.selection().select_face(box.value(), 1)).ok());
        const auto source = editor.query().compiled_object_mesh(box.value());
        ASSERT_TRUE(source.has_value());
        session.history().clear();
        if (bevel) {
          const auto started = editor.commands().execute(
              editor.commands().command_id_for_modeling_action("bevel_edges"));
          ASSERT_TRUE(started.ok()) << started.error_message();
          ASSERT_TRUE(editor.tools().set_setting(
              "edge_bevel", "operation.edge_bevel.width", 0.1).ok());
        } else {
          ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Extrude).ok());
          ASSERT_TRUE(editor.tools().set_setting(
              "extrude", "extrude.amount", 1.0).ok());
        }
        ASSERT_TRUE(editor.tools().active_settings().preview_active);
        quader::QtCommandEventFilter filter(editor, [bevel] {
          return quader::ViewportToolState{
              .active_tool = bevel ? quader::ViewportToolId::Select
                                   : quader::ViewportToolId::Extrude,
              .selection_mode = bevel ? quader::ViewportSelectionMode::Edge
                                      : quader::ViewportSelectionMode::Face};
        });
        application.installEventFilter(&filter);
        quader::QtToolSettingsPanel panel;
        panel.setAttribute(Qt::WA_DontShowOnScreen, true);
        panel.bind(editor);
        panel.set_expanded(true);
        auto *number = panel.findChild<QDoubleSpinBox *>(bevel
            ? QStringLiteral("tool_settings.control.operation.edge_bevel.width")
            : QStringLiteral("tool_settings.control.extrude.amount"));
        auto *apply = panel.findChild<QPushButton *>(
            QStringLiteral("tool_settings.apply"));
        auto *cancel = panel.findChild<QPushButton *>(
            QStringLiteral("tool_settings.cancel"));
        ASSERT_NE(number, nullptr);
        ASSERT_NE(apply, nullptr);
        ASSERT_NE(cancel, nullptr);
        int button_clicks = 0;
        QObject::connect(apply, &QPushButton::clicked, &panel,
                         [&] { ++button_clicks; });
        QObject::connect(cancel, &QPushButton::clicked, &panel,
                         [&] { ++button_clicks; });
        QWidget *target = focus_name == QStringLiteral("number")
            ? number : panel.findChild<QWidget *>(focus_name);
        ASSERT_NE(target, nullptr);
        ASSERT_TRUE(target->isEnabled());
        panel.show();
        panel.activateWindow();
        QCoreApplication::processEvents();
        QApplication::setActiveWindow(&panel);
        ASSERT_TRUE(target->isVisible());
        ASSERT_TRUE(target->isEnabled());
        target->setFocus(Qt::OtherFocusReason);
        ASSERT_EQ(QApplication::focusWidget(), target);
        if (target == number) {
          number->setLocale(QLocale::c());
          auto *line = number->findChild<QLineEdit *>();
          ASSERT_NE(line, nullptr);
          send_key_press(*line, Qt::Key_A, Qt::ControlModifier);
          send_key_press(*line, Qt::Key_2, Qt::NoModifier,
                         bevel ? QStringLiteral("0.2") : QStringLiteral("2.5"));
        }
        QObject *receiver = target;
        if (bevel && target == number) {
          // Native window delivery precedes QWidget dispatch in the real app.
          ASSERT_NE(panel.windowHandle(), nullptr);
          receiver = panel.windowHandle();
        }
        const auto press_lifecycle_key = [&] {
          QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
          QCoreApplication::sendEvent(receiver, &press);
        };
        QKeyEvent override_event(QEvent::ShortcutOverride, key, Qt::NoModifier);
        override_event.ignore();
        QCoreApplication::sendEvent(receiver, &override_event);
        EXPECT_TRUE(override_event.isAccepted());
        EXPECT_TRUE(editor.tools().active_settings().preview_active);
        press_lifecycle_key();
        if (target == number && key == Qt::Key_Escape) {
          EXPECT_FALSE(quader::qt_install_numeric_spin_box(*number).is_editing());
          EXPECT_TRUE(editor.tools().active_settings().preview_active);
          EXPECT_DOUBLE_EQ(number->value(), bevel ? 0.1 : 1.0);
          EXPECT_DOUBLE_EQ(bevel
              ? session.modeling().interaction_settings().edge_bevel_width
              : session.extrude().amount(), bevel ? 0.1 : 1.0);
          EXPECT_EQ(session.history().undo_count(), 0U);
          QKeyEvent repeat(QEvent::KeyPress, key, Qt::NoModifier, {}, true);
          QCoreApplication::sendEvent(cancel, &repeat);
          QKeyEvent repeated_release(QEvent::KeyRelease, key,
                                     Qt::NoModifier, {}, true);
          QCoreApplication::sendEvent(cancel, &repeated_release);
          EXPECT_TRUE(editor.tools().active_settings().preview_active);
          QKeyEvent first_release(QEvent::KeyRelease, key, Qt::NoModifier);
          QCoreApplication::sendEvent(receiver, &first_release);
          EXPECT_TRUE(editor.tools().active_settings().preview_active);
          press_lifecycle_key();
        }
        QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
        QCoreApplication::sendEvent(receiver, &release);
        EXPECT_FALSE(editor.tools().active_settings().preview_active);
        EXPECT_EQ(button_clicks, 0);
        EXPECT_EQ(session.history().undo_count(), key == Qt::Key_Space ? 1U : 0U);
        EXPECT_TRUE(panel.expanded());
        if (key == Qt::Key_Space) {
          if (target == number) {
            EXPECT_DOUBLE_EQ(bevel
                ? session.modeling().interaction_settings().edge_bevel_width
                : session.extrude().amount(), bevel ? 0.2 : 2.5);
          }
          ASSERT_TRUE(editor.history().undo().ok());
        }
        const auto restored = editor.query().compiled_object_mesh(box.value());
        ASSERT_TRUE(restored.has_value());
        EXPECT_EQ(restored->indices, source->indices);
        EXPECT_EQ(restored->vertices.size(), source->vertices.size());
        application.removeEventFilter(&filter);
        panel.unbind();
      }
    }
  }
}

TEST(QtCommandEventFilterTests,
     StickySettingsLifecycleKeysCompleteTheSharedPreviewAfterFocusTransfer) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-pane-settings-lifecycle-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  for (const int key : {Qt::Key_Escape, Qt::Key_Space}) {
    SCOPED_TRACE(key);
    quader::SceneSession session;
    quader::QuaderEditorApi editor(
        quader::QuaderApiSessionInternalAccess::from_scene_session(session));
    ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
    const auto box = editor.modeling().create_box(
        {.min = {-1, -1, -1}, .max = {1, 1, 1}});
    ASSERT_TRUE(box.ok());
    ASSERT_TRUE(editor.selection().select_face(box.value(), 1).ok());
    const auto source = editor.query().compiled_object_mesh(box.value());
    ASSERT_TRUE(source.has_value());
    ASSERT_TRUE(editor.viewports().set_layout(
        quader::ViewportLayoutId::TwoVertical).ok());
    ASSERT_TRUE(editor.viewports().record_selected_tool(
        quader::ViewportPaneId::Top, quader::ViewportToolId::Move).ok());
    ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Extrude).ok());
    ASSERT_TRUE(editor.tools().set_setting("extrude", "extrude.amount", 1.0).ok());
    ASSERT_TRUE(editor.tools().active_settings().preview_active);
    const auto origin = editor.tools().active_settings_presentation_origin();
    const auto history_count = session.history().undo_count();
    ASSERT_TRUE(editor.viewports().focus_pane(quader::ViewportPaneId::Top).ok());

    quader::QtCommandEventFilter filter(editor, [&editor] {
      return quader::ViewportToolState{
          .active_tool = editor.viewports().selected_tool(),
          .selection_mode = quader::ViewportSelectionMode::Face};
    });
    application.installEventFilter(&filter);
    quader::QtToolSettingsPanel panel;
    panel.setAttribute(Qt::WA_DontShowOnScreen, true);
    panel.bind(editor);
    ASSERT_EQ(panel.target_id(), "extrude");
    EXPECT_EQ(editor.tools().active_settings_presentation_origin(), origin);
    EXPECT_EQ(editor.viewports().selected_tool(quader::ViewportPaneId::Top),
              quader::ViewportToolId::Extrude);
    auto *title = panel.findChild<QAbstractButton *>(
        QStringLiteral("tool_settings.target_title"));
    ASSERT_NE(title, nullptr);
    panel.show();
    panel.activateWindow();
    title->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    ASSERT_EQ(QApplication::focusWidget(), title);
    send_key_press(*title, key, Qt::NoModifier);
    QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
    QCoreApplication::sendEvent(title, &release);
    EXPECT_FALSE(editor.tools().active_settings().preview_active);
    EXPECT_EQ(editor.viewports().focused_pane(), quader::ViewportPaneId::Top);
    EXPECT_EQ(session.history().undo_count(),
              history_count + (key == Qt::Key_Space ? 1U : 0U));
    if (key == Qt::Key_Space) {
      ASSERT_TRUE(editor.history().undo().ok());
    }
    const auto restored = editor.query().compiled_object_mesh(box.value());
    ASSERT_TRUE(restored.has_value());
    EXPECT_EQ(restored->indices, source->indices);
    EXPECT_EQ(restored->vertices.size(), source->vertices.size());
    application.removeEventFilter(&filter);
    panel.unbind();
  }
}

TEST(QtCommandEventFilterTests,
     SettingsLifecycleRespectsPopupOwnershipUnassignmentAndRemappedSequences) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-settings-lifecycle-sequence-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  const auto box = editor.modeling().create_box(
      {.min = {-1, -1, -1}, .max = {1, 1, 1}});
  ASSERT_TRUE(box.ok());
  ASSERT_TRUE(editor.selection().select_face(box.value(), 1).ok());
  session.history().clear();
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Extrude).ok());
  ASSERT_TRUE(editor.tools().set_setting("extrude", "extrude.amount", 1.0).ok());
  quader::QtCommandEventFilter filter(editor, [] {
    return quader::ViewportToolState{
        .active_tool = quader::ViewportToolId::Extrude,
        .selection_mode = quader::ViewportSelectionMode::Face};
  });
  application.installEventFilter(&filter);
  quader::QtToolSettingsPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.bind(editor);
  panel.set_expanded(true);
  auto *amount = panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("tool_settings.control.extrude.amount"));
  ASSERT_NE(amount, nullptr);
  panel.show();
  panel.activateWindow();
  amount->setFocus(Qt::OtherFocusReason);
  QCoreApplication::processEvents();
  ASSERT_EQ(QApplication::focusWidget(), amount);

  QMenu popup(&panel);
  popup.addAction(QStringLiteral("Settings popup"));
  popup.popup(amount->mapToGlobal(QPoint(0, 0)));
  QCoreApplication::processEvents();
  ASSERT_EQ(QApplication::activePopupWidget(), &popup);
  send_key_press(popup, Qt::Key_Escape, Qt::NoModifier);
  QKeyEvent escape_release(QEvent::KeyRelease, Qt::Key_Escape, Qt::NoModifier);
  QCoreApplication::sendEvent(&popup, &escape_release);
  EXPECT_FALSE(popup.isVisible());
  EXPECT_TRUE(editor.tools().active_settings().preview_active);
  EXPECT_EQ(session.history().undo_count(), 0U);
  amount->setFocus(Qt::OtherFocusReason);
  QCoreApplication::processEvents();
  ASSERT_EQ(QApplication::focusWidget(), amount);

  const auto catalog = editor.commands().keybinding_catalog();
  const auto finalize = std::ranges::find_if(catalog, [](const auto &entry) {
    return std::ranges::find(entry.target_ids, "viewport.modal_tool.finalize") !=
           entry.target_ids.end();
  });
  ASSERT_NE(finalize, catalog.end());
  ASSERT_TRUE(editor.commands().remove_action_shortcut(finalize->key).ok());
  send_key_press(*amount, Qt::Key_Space, Qt::NoModifier);
  QKeyEvent space_release(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
  QCoreApplication::sendEvent(amount, &space_release);
  EXPECT_TRUE(editor.tools().active_settings().preview_active);
  EXPECT_EQ(session.history().undo_count(), 0U);
  const auto expect_space_does_not_activate_a_button = [&] {
    for (const QString &name : {QStringLiteral("tool_settings.apply"),
                               QStringLiteral("tool_settings.cancel"),
                               QStringLiteral("tool_settings.target_title")}) {
      SCOPED_TRACE(name.toStdString());
      auto *button = panel.findChild<QAbstractButton *>(name);
      ASSERT_NE(button, nullptr);
      int clicks = 0;
      const auto connection = QObject::connect(
          button, &QAbstractButton::clicked, &panel, [&] { ++clicks; });
      button->setFocus(Qt::OtherFocusReason);
      QCoreApplication::processEvents();
      ASSERT_EQ(QApplication::focusWidget(), button);
      QKeyEvent override_event(QEvent::ShortcutOverride, Qt::Key_Space,
                               Qt::NoModifier);
      override_event.ignore();
      QCoreApplication::sendEvent(button, &override_event);
      EXPECT_TRUE(override_event.isAccepted());
      send_key_press(*button, Qt::Key_Space, Qt::NoModifier);
      QKeyEvent release(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
      QCoreApplication::sendEvent(button, &release);
      EXPECT_EQ(clicks, 0);
      EXPECT_TRUE(editor.tools().active_settings().preview_active);
      EXPECT_TRUE(panel.expanded());
      EXPECT_EQ(session.history().undo_count(), 0U);
      QObject::disconnect(connection);
    }
  };
  expect_space_does_not_activate_a_button();
  const auto remapped = editor.commands().set_action_shortcut(
      finalize->key, "Ctrl+Alt+F24 G");
  ASSERT_TRUE(remapped.ok()) << remapped.error_message();
  expect_space_does_not_activate_a_button();
  amount->setFocus(Qt::OtherFocusReason);
  QCoreApplication::processEvents();
  ASSERT_EQ(QApplication::focusWidget(), amount);
  const QString numeric_text = amount->text();
  send_key_press(*amount, Qt::Key_F24, Qt::ControlModifier | Qt::AltModifier);
  ASSERT_TRUE(editor.commands().has_pending_keybinding_chord());
  QKeyEvent mismatch_override(QEvent::ShortcutOverride, Qt::Key_9, Qt::NoModifier);
  mismatch_override.ignore();
  QCoreApplication::sendEvent(amount, &mismatch_override);
  EXPECT_TRUE(mismatch_override.isAccepted());
  send_key_press(*amount, Qt::Key_9, Qt::NoModifier, QStringLiteral("9"));
  EXPECT_FALSE(editor.commands().has_pending_keybinding_chord());
  EXPECT_EQ(amount->text(), numeric_text);
  EXPECT_TRUE(editor.tools().active_settings().preview_active);
  for (const int key : {Qt::Key_F24, Qt::Key_9}) {
    QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
    QCoreApplication::sendEvent(amount, &release);
  }
  for (const int key : {Qt::Key_F24, Qt::Key_G}) {
    const auto modifiers = key == Qt::Key_F24
        ? Qt::ControlModifier | Qt::AltModifier : Qt::NoModifier;
    QKeyEvent override_event(QEvent::ShortcutOverride, key, modifiers);
    override_event.ignore();
    QCoreApplication::sendEvent(amount, &override_event);
    EXPECT_TRUE(override_event.isAccepted());
    // Probing must leave a prefix unchanged until its physical press arrives.
    EXPECT_EQ(editor.commands().has_pending_keybinding_chord(), key == Qt::Key_G);
    send_key_press(*amount, key, modifiers);
    QKeyEvent release(QEvent::KeyRelease, key, modifiers);
    QCoreApplication::sendEvent(amount, &release);
    editor.commands().reset_pending_viewport_keybinding_chord();
    EXPECT_EQ(editor.commands().has_pending_keybinding_chord(), key == Qt::Key_F24);
  }
  EXPECT_FALSE(editor.tools().active_settings().preview_active);
  EXPECT_EQ(session.history().undo_count(), 1U);
  application.removeEventFilter(&filter);
  panel.unbind();
}

TEST(QtCommandEventFilterTests,
     NativeToolSettingsFinalizeFlushesTypedAmountAndEscapeFirstDiscardsItsDraft) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-tool-settings-pending-number-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  for (const int completion_key : {Qt::Key_Space, Qt::Key_Return,
                                   Qt::Key_Escape}) {
    SCOPED_TRACE(completion_key);
    quader::SceneSession session;
    quader::QuaderEditorApi editor(
        quader::QuaderApiSessionInternalAccess::from_scene_session(session));
    ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
    if (completion_key == Qt::Key_Return) {
      const auto catalog = editor.commands().keybinding_catalog();
      const auto finalize = std::ranges::find_if(catalog, [](const auto &entry) {
        return std::ranges::find(entry.target_ids, "viewport.modal_tool.finalize") !=
               entry.target_ids.end();
      });
      ASSERT_NE(finalize, catalog.end());
      // Enter already belongs to the Radial Array completion scope.
      const auto remapped = editor.commands().set_action_shortcut(
          finalize->key, "Enter", quader::QuaderKeybindingConflictPolicy::Replace);
      ASSERT_TRUE(remapped.ok()) << remapped.error_message();
    }
    const auto box = editor.modeling().create_box(
        {.min = {-1.0F, -1.0F, -1.0F}, .max = {1.0F, 1.0F, 1.0F}});
    ASSERT_TRUE(box.ok());
    ASSERT_TRUE(editor.selection().select_face(box.value(), 1).ok());
    const auto source = editor.query().compiled_object_mesh(box.value());
    ASSERT_TRUE(source.has_value());
    session.history().clear();
    ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Extrude).ok());
    ASSERT_TRUE(editor.tools().active_settings().preview_active);
    ASSERT_DOUBLE_EQ(session.extrude().amount(), 0.0);
    ASSERT_TRUE(editor.tools().set_setting("extrude", "extrude.amount", 7.25).ok());
    const auto expected = editor.query().compiled_object_mesh(box.value());
    ASSERT_TRUE(expected.has_value());
    ASSERT_TRUE(editor.tools().set_setting("extrude", "extrude.amount", 0.0).ok());

    quader::QtCommandEventFilter filter(editor, [] {
      return quader::ViewportToolState{
          .active_tool = quader::ViewportToolId::Extrude,
          .selection_mode = quader::ViewportSelectionMode::Face};
    });
    application.installEventFilter(&filter);
    quader::QtToolSettingsPanel panel;
    panel.setAttribute(Qt::WA_DontShowOnScreen, true);
    panel.bind(editor);
    panel.set_expanded(true);
    auto *amount = panel.findChild<QDoubleSpinBox *>(
        QStringLiteral("tool_settings.control.extrude.amount"));
    ASSERT_NE(amount, nullptr);
    // Production main installs C locale before constructing QApplication.
    amount->setLocale(QLocale::c());
    auto *line = amount->findChild<QLineEdit *>();
    ASSERT_NE(line, nullptr);
    panel.show();
    panel.activateWindow();
    QApplication::setActiveWindow(&panel);
    line->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    ASSERT_EQ(QApplication::focusWidget(), amount);
    send_key_press(*line, Qt::Key_A, Qt::ControlModifier);
    send_key_press(*line, Qt::Key_7, Qt::NoModifier, QStringLiteral("7.25"));
    ASSERT_TRUE(panel.numeric_interaction_active());
    ASSERT_DOUBLE_EQ(session.extrude().amount(), 0.0);

    if (completion_key == Qt::Key_Escape) {
      // Preserve live typed previews, then restore them when the draft is canceled.
      QEventLoop pause;
      QTimer::singleShot(40, &pause, &QEventLoop::quit);
      pause.exec();
      ASSERT_DOUBLE_EQ(session.extrude().amount(), 7.25);
      ASSERT_TRUE(quader::qt_install_numeric_spin_box(*amount).is_editing());
    }

    // Apply must settle the queued draft; Escape must discard it.
    send_key_press(*line, completion_key, Qt::NoModifier);
    QKeyEvent completion_release(QEvent::KeyRelease, completion_key, Qt::NoModifier);
    QCoreApplication::sendEvent(line, &completion_release);
    if (completion_key == Qt::Key_Escape) {
      EXPECT_TRUE(editor.tools().active_settings().preview_active);
      EXPECT_EQ(editor.tools().active_tool(), "extrude");
      EXPECT_FALSE(quader::qt_install_numeric_spin_box(*amount).is_editing());
      EXPECT_FALSE(panel.numeric_interaction_active());
      EXPECT_DOUBLE_EQ(amount->value(), 0.0);
      EXPECT_DOUBLE_EQ(session.extrude().amount(), 0.0);
      EXPECT_EQ(session.history().undo_count(), 0U);
      send_key_press(*line, completion_key, Qt::NoModifier);
      QCoreApplication::sendEvent(line, &completion_release);
    }
    EXPECT_FALSE(editor.tools().active_settings().preview_active);
    EXPECT_FALSE(panel.numeric_interaction_active());
    EXPECT_TRUE(editor.render().tool_preview_meshes().empty());
    if (completion_key == Qt::Key_Escape) {
      EXPECT_EQ(editor.tools().active_tool(), "select");
      EXPECT_EQ(session.history().undo_count(), 0U);
      EXPECT_DOUBLE_EQ(session.extrude().amount(), 0.0);
      const auto restored = editor.query().compiled_object_mesh(box.value());
      ASSERT_TRUE(restored.has_value());
      EXPECT_EQ(restored->indices, source->indices);
      EXPECT_EQ(restored->vertices.size(), source->vertices.size());
    } else {
      EXPECT_EQ(session.history().undo_count(), 1U);
      EXPECT_DOUBLE_EQ(session.extrude().amount(), 7.25);
      panel.refresh();
      EXPECT_DOUBLE_EQ(amount->value(), 7.25);
      const auto committed = editor.query().compiled_object_mesh(box.value());
      ASSERT_TRUE(committed.has_value());
      EXPECT_EQ(committed->indices, expected->indices);
      ASSERT_EQ(committed->vertices.size(), expected->vertices.size());
      for (std::size_t index = 0; index < committed->vertices.size(); ++index) {
        const auto &position = committed->vertices[index].position;
        const auto &expected_position = expected->vertices[index].position;
        EXPECT_FLOAT_EQ(position.x, expected_position.x);
        EXPECT_FLOAT_EQ(position.y, expected_position.y);
        EXPECT_FLOAT_EQ(position.z, expected_position.z);
      }
      // The next step may start and finish before a frame observes its preview.
      // Retained focus must accept the new amount and keep the accepted option.
      ASSERT_TRUE(amount->hasFocus());
      send_key_press(*line, Qt::Key_A, Qt::ControlModifier);
      send_key_press(*line, Qt::Key_2, Qt::NoModifier, QStringLiteral("2.5"));
      ASSERT_DOUBLE_EQ(session.extrude().amount(), 7.25);
      ASSERT_FALSE(editor.tools().active_settings().preview_active);
      send_key_press(*line, completion_key, Qt::NoModifier);
      QCoreApplication::sendEvent(line, &completion_release);
      EXPECT_EQ(session.history().undo_count(), 2U);
      EXPECT_FALSE(editor.tools().active_settings().preview_active);
      EXPECT_DOUBLE_EQ(session.extrude().amount(), 2.5);
      panel.refresh();
      EXPECT_DOUBLE_EQ(amount->value(), 2.5);
      const auto second_step = editor.query().compiled_object_mesh(box.value());
      ASSERT_TRUE(second_step.has_value());
      EXPECT_GT(second_step->indices.size(), committed->indices.size());
      ASSERT_TRUE(editor.history().undo().ok());
      const auto first_step = editor.query().compiled_object_mesh(box.value());
      ASSERT_TRUE(first_step.has_value());
      EXPECT_EQ(first_step->indices, committed->indices);
      ASSERT_EQ(first_step->vertices.size(), committed->vertices.size());
      for (std::size_t index = 0; index < first_step->vertices.size(); ++index) {
        const auto &position = first_step->vertices[index].position;
        const auto &committed_position = committed->vertices[index].position;
        EXPECT_FLOAT_EQ(position.x, committed_position.x);
        EXPECT_FLOAT_EQ(position.y, committed_position.y);
        EXPECT_FLOAT_EQ(position.z, committed_position.z);
      }
      ASSERT_TRUE(editor.history().undo().ok());
      const auto restored = editor.query().compiled_object_mesh(box.value());
      ASSERT_TRUE(restored.has_value());
      EXPECT_EQ(restored->indices, source->indices);
      ASSERT_TRUE(editor.history().redo().ok());
    }
    application.removeEventFilter(&filter);
    panel.unbind();
  }
}

TEST(QtCommandEventFilterTests,
     NativeToolSettingsFinalizeStopsWhenNumericInterpretationRebindsRuntime) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-tool-settings-number-rebind-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  for (const int rebind_case : {0, 1, 2}) {
    SCOPED_TRACE(rebind_case);
    quader::SceneSession outgoing_session;
    quader::SceneSession replacement_session;
    quader::QuaderEditorApi outgoing(
        quader::QuaderApiSessionInternalAccess::from_scene_session(outgoing_session));
    quader::QuaderEditorApi replacement(
        quader::QuaderApiSessionInternalAccess::from_scene_session(replacement_session));
    for (quader::QuaderEditorApi *editor : {&outgoing, &replacement}) {
      ASSERT_TRUE(editor->commands().set_keybinding_storage_path({}).ok());
      const auto box = editor->modeling().create_box(
          {.min = {-1.0F, -1.0F, -1.0F}, .max = {1.0F, 1.0F, 1.0F}});
      ASSERT_TRUE(box.ok());
      ASSERT_TRUE(editor->selection().select_face(box.value(), 1).ok());
      ASSERT_TRUE(editor->tools().activate(quader::ViewportToolId::Extrude).ok());
      ASSERT_TRUE(editor->tools().active_settings().preview_active);
      if (editor == &replacement) {
        ASSERT_TRUE(editor->tools().set_setting(
            "extrude", "extrude.amount", 1.0).ok());
      }
    }
    const auto outgoing_history = outgoing_session.history().undo_count();
    const auto replacement_history = replacement_session.history().undo_count();
    quader::QtCommandEventFilter filter(outgoing, [] {
      return quader::ViewportToolState{
          .active_tool = quader::ViewportToolId::Extrude,
          .selection_mode = quader::ViewportSelectionMode::Face};
    });
    application.installEventFilter(&filter);
    quader::QtToolSettingsPanel panel;
    panel.setAttribute(Qt::WA_DontShowOnScreen, true);
    panel.bind(outgoing);
    panel.set_expanded(true);
    auto *amount = panel.findChild<QDoubleSpinBox *>(
        QStringLiteral("tool_settings.control.extrude.amount"));
    ASSERT_NE(amount, nullptr);
    amount->setLocale(QLocale::c());
    auto *line = amount->findChild<QLineEdit *>();
    ASSERT_NE(line, nullptr);
    panel.show();
    panel.activateWindow();
    line->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    ASSERT_EQ(QApplication::focusWidget(), amount);
    send_key_press(*line, Qt::Key_A, Qt::ControlModifier);
    send_key_press(*line, Qt::Key_7, Qt::NoModifier, QStringLiteral("7.25"));
    int rebinds = 0;
    QObject::connect(amount, &QDoubleSpinBox::valueChanged, &panel, [&](double) {
      ++rebinds;
      filter.rebind_editor(rebind_case == 2 ? outgoing : replacement);
      if (rebind_case == 1) {
        panel.bind(replacement);
      }
    });
    send_key_press(*line, Qt::Key_Space, Qt::NoModifier);
    EXPECT_EQ(rebinds, 1);
    EXPECT_TRUE(outgoing.tools().active_settings().preview_active);
    EXPECT_TRUE(replacement.tools().active_settings().preview_active);
    EXPECT_EQ(outgoing_session.history().undo_count(), outgoing_history);
    EXPECT_EQ(replacement_session.history().undo_count(), replacement_history);

    // A held key remains reserved even while a popup takes ownership and Qt
    // preprocesses its shortcut before delivering the repeated press.
    QMenu popup(&panel);
    popup.addAction(QStringLiteral("Popup during runtime replacement"));
    popup.popup(panel.mapToGlobal(QPoint(0, 0)));
    QCoreApplication::processEvents();
    ASSERT_EQ(QApplication::activePopupWidget(), &popup);
    QKeyEvent repeat_override(QEvent::ShortcutOverride, Qt::Key_Space,
                              Qt::NoModifier, QStringLiteral(" "), true);
    repeat_override.ignore();
    QCoreApplication::sendEvent(&popup, &repeat_override);
    EXPECT_TRUE(repeat_override.isAccepted());
    popup.close();

    filter.rebind_editor(replacement);
    panel.bind(replacement);
    auto *cancel = panel.findChild<QPushButton *>(
        QStringLiteral("tool_settings.cancel"));
    ASSERT_NE(cancel, nullptr);
    cancel->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    ASSERT_EQ(QApplication::focusWidget(), cancel);
    QKeyEvent repeat(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier,
                     QStringLiteral(" "), true);
    QCoreApplication::sendEvent(cancel, &repeat);
    QKeyEvent repeat_release(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier,
                             QStringLiteral(" "), true);
    QCoreApplication::sendEvent(cancel, &repeat_release);
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
    QCoreApplication::sendEvent(cancel, &release);
    EXPECT_TRUE(replacement.tools().active_settings().preview_active);
    EXPECT_EQ(replacement_session.history().undo_count(), replacement_history);
    // Only a new physical press may act on the replacement runtime.
    send_key_press(*cancel, Qt::Key_Space, Qt::NoModifier);
    QCoreApplication::sendEvent(cancel, &release);
    EXPECT_FALSE(replacement.tools().active_settings().preview_active);
    EXPECT_EQ(replacement_session.history().undo_count(), replacement_history + 1U);
    application.removeEventFilter(&filter);
    panel.unbind();
  }
}

TEST(QtCommandEventFilterTests,
     DisabledToolSettingsApplyRemainsInactiveWithoutErrorNotifications) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-tool-settings-failure-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  auto standalone = quader::create_standalone_editor();
  auto &editor = standalone.editor();
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Curve).ok());
  ASSERT_TRUE(editor.tools().active_native_tool_lifecycle(
      quader::ViewportToolId::Curve));
  ASSERT_FALSE(editor.tools().active_settings().can_apply);

  std::vector<quader::QuaderNotification> notifications;
  editor.ui().set_runtime_hooks(
      {.notify = [&](quader::QuaderNotification notification) {
         notifications.push_back(std::move(notification));
         return quader::QuaderApiVoidResult{.ok = true};
       }});
  quader::QtCommandEventFilter command_filter(editor, []() {
    return quader::ViewportToolState{
        .active_tool = quader::ViewportToolId::Curve};
  });
  application.installEventFilter(&command_filter);
  quader::QtToolSettingsPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.bind(editor);
  QLineEdit edit(&panel);
  panel.show();
  panel.activateWindow();
  edit.setFocus(Qt::OtherFocusReason);
  QCoreApplication::processEvents();
  ASSERT_EQ(QApplication::focusWidget(), &edit);
  const auto command_revision = editor.commands().last_status_revision();

  send_key_press(edit, Qt::Key_Space, Qt::NoModifier);
  EXPECT_TRUE(notifications.empty());
  EXPECT_EQ(editor.commands().last_status_revision(), command_revision);
  QCoreApplication::processEvents();
  send_key_press(edit, Qt::Key_Space, Qt::NoModifier);
  EXPECT_TRUE(notifications.empty());
  EXPECT_EQ(editor.tools().active_tool(), "curve");
  EXPECT_EQ(editor.commands().last_status_revision(), command_revision);
  application.removeEventFilter(&command_filter);
}

TEST(QtCommandEventFilterTests,
     EmbeddedMaterialBrowserRoutesToolsAndSequencesBeforeTypeAhead) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-material-browser-command-focus-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  auto standalone = quader::create_standalone_editor();
  auto &editor = standalone.editor();
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  // This fixture deliberately remaps a tool to bare B to exercise browser
  // type-ahead precedence; release its normal quick-Bridge assignment first.
  ASSERT_TRUE(editor.commands()
                  .remove_shortcut(quader::quader_command::kModelingBridge)
                  .ok());
  const auto box = editor.modeling().create_box(
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      "Browser Shortcut Selection");
  ASSERT_TRUE(box.ok());
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection().select_face(box.value(), 1).ok());
  const auto primitive = quader::quader_command::kToolsActivateCreatePrimitive;
  ASSERT_EQ(editor.commands().shortcut_for(primitive), "Shift+B");

  QMainWindow window;
  window.resize(800, 900);
  auto *dock = new QDockWidget(&window);
  dock->setObjectName(QStringLiteral("dock_face_texture"));
  auto *panel = new quader::QtFaceTexturePanel(dock);
  dock->setWidget(panel);
  window.addDockWidget(Qt::RightDockWidgetArea, dock);
  panel->bind(editor);
  auto *browser = dynamic_cast<quader::QtMaterialBrowserWindow *>(
      panel->findChild<QMainWindow *>(QStringLiteral("materialBrowser.embedded")));
  ASSERT_NE(browser, nullptr);
  const auto first = quader::QuaderMaterialReference::embedded(111);
  const auto second = quader::QuaderMaterialReference::embedded(222);
  browser->set_materials({
      {.reference = first, .stable_id = QStringLiteral("embedded:111"),
       .name = QStringLiteral("Alpha")},
      {.reference = second, .stable_id = QStringLiteral("embedded:222"),
       .name = QStringLiteral("Brick")}}, first);
  quader::QtCommandEventFilter filter(editor, {});
  application.installEventFilter(&filter);
  window.show();
  QCoreApplication::processEvents();

  for (bool floating : {false, true}) {
    SCOPED_TRACE(floating);
    dock->setFloating(floating);
    browser->restore_presentation_state(
        quader::QtMaterialBrowserViewMode::Grid, 64, false);
    browser->set_selected_material(first);
    auto *view = browser->findChild<QListView *>(
        QStringLiteral("materialBrowser.gridView"));
    ASSERT_NE(view, nullptr);
    dock->window()->activateWindow();
    view->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    ASSERT_EQ(QApplication::focusWidget(), view);
    const QModelIndex selected_before = view->currentIndex();
    ASSERT_TRUE(selected_before.isValid());
    ASSERT_TRUE(editor.commands().execute(
        quader::quader_command::kToolsActivateSelect).ok());
    send_key_press(*view, Qt::Key_B, Qt::ShiftModifier, QStringLiteral("B"));
    EXPECT_EQ(editor.tools().active_tool(), "create_primitive");
    EXPECT_EQ(editor.tools().active_primitive_type(), "box");
    send_key_press(*view, Qt::Key_C, Qt::ShiftModifier, QStringLiteral("C"));
    EXPECT_EQ(editor.tools().active_primitive_type(), "cylinder");
    send_key_press(*view, Qt::Key_S, Qt::ShiftModifier, QStringLiteral("S"));
    EXPECT_EQ(editor.tools().active_primitive_type(), "sphere");
    send_key_press(*view, Qt::Key_B, Qt::ShiftModifier, QStringLiteral("B"));
    EXPECT_EQ(editor.tools().active_primitive_type(), "box");
    EXPECT_EQ(view->currentIndex(), selected_before);
    EXPECT_EQ(browser->current_selected_material(), first);
    EXPECT_EQ(QApplication::focusWidget(), view);
    EXPECT_EQ(editor.selection().summary().selected_face_count, 1u);

    // The browser's explicit navigation owns arrows even after a command
    // is customized to the same chord; ordinary tool letters stay global.
    ASSERT_TRUE(editor.commands().set_shortcut(primitive, "Down").ok());
    ASSERT_TRUE(editor.commands().execute(
        quader::quader_command::kToolsActivateSelect).ok());
    const auto status_before_navigation = editor.commands().last_status_revision();
    send_key_press(*view, Qt::Key_Down, Qt::NoModifier);
    EXPECT_EQ(editor.commands().last_status_revision(), status_before_navigation);
    EXPECT_EQ(editor.tools().active_tool(), "select");
    ASSERT_TRUE(editor.commands().set_shortcut(primitive, "B").ok());
    browser->set_selected_material(first);
    send_key_press(*view, Qt::Key_B, Qt::NoModifier, QStringLiteral("b"));
    EXPECT_EQ(editor.tools().active_tool(), "create_primitive");
    EXPECT_EQ(browser->current_selected_material(), first);

    ASSERT_TRUE(editor.commands().set_shortcut(primitive, "Ctrl+Alt+F24 G").ok());
    ASSERT_TRUE(editor.commands().execute(
        quader::quader_command::kToolsActivateSelect).ok());
    send_key_press(*view, Qt::Key_F24, Qt::ControlModifier | Qt::AltModifier);
    ASSERT_TRUE(editor.commands().has_pending_keybinding_chord());
    editor.commands().reset_pending_viewport_keybinding_chord();
    ASSERT_TRUE(editor.commands().has_pending_keybinding_chord());
    send_key_press(*view, Qt::Key_G, Qt::NoModifier, QStringLiteral("g"));
    EXPECT_EQ(editor.tools().active_tool(), "create_primitive");
    EXPECT_FALSE(editor.commands().has_pending_keybinding_chord());
    ASSERT_TRUE(editor.commands().set_shortcut(primitive, "Ctrl+B").ok());
    ASSERT_TRUE(editor.commands().execute(
        quader::quader_command::kToolsActivateSelect).ok());
    send_key_press(*view, Qt::Key_B, Qt::ControlModifier, QStringLiteral("b"));
    EXPECT_EQ(editor.tools().active_tool(), "create_primitive");
    EXPECT_EQ(browser->current_selected_material(), first);
    ASSERT_TRUE(editor.commands().reset_shortcut(primitive).ok());
  }
  panel->unbind();
  application.removeEventFilter(&filter);
}

TEST(QtCommandEventFilterTests,
     EditorPanelModeAndGridBindingsUseOwnersAndAcceptActivePreviews) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-editor-panel-mode-shortcut-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  auto standalone = quader::create_standalone_editor();
  auto &editor = standalone.editor();
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  const auto box = editor.modeling().create_box(
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      "Panel Mode Preview");
  ASSERT_TRUE(box.ok());
  ASSERT_TRUE(editor.selection().select_object(box.value()).ok());
  QTreeWidget panel;
  panel.setSelectionMode(QAbstractItemView::ExtendedSelection);
  panel.addTopLevelItem(new QTreeWidgetItem({QStringLiteral("1 First item")}));
  panel.addTopLevelItem(new QTreeWidgetItem({QStringLiteral("2 Second item")}));
  panel.setCurrentItem(panel.topLevelItem(0));
  panel.resize(320, 240);
  panel.show();
  panel.activateWindow();
  panel.setFocus(Qt::OtherFocusReason);
  QCoreApplication::processEvents();
  ASSERT_EQ(QApplication::focusWidget(), &panel);
  quader::QtCommandEventFilter filter(editor, {});
  application.installEventFilter(&filter);

  for (const auto mode : {quader::QuaderSelectionMode::Vertex,
                         quader::QuaderSelectionMode::Edge,
                         quader::QuaderSelectionMode::Face,
                         quader::QuaderSelectionMode::Object,
                         quader::QuaderSelectionMode::Group}) {
    SCOPED_TRACE(static_cast<int>(mode));
    const std::string target = editor.commands().keybinding_target_id_for_selection_mode(mode);
    const QString mapping = QString::fromStdString(editor.commands().shortcut_for(target));
    ASSERT_EQ(mapping.size(), 1);
    send_key_press(panel, mapping.at(0).unicode(), Qt::NoModifier, mapping);
    EXPECT_EQ(editor.selection().summary().mode, mode);
    EXPECT_EQ(panel.currentItem(), panel.topLevelItem(0));
  }

  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Object).ok());
  ASSERT_TRUE(editor.selection().select_object(box.value()).ok());
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::CreatePrimitive).ok());
  ASSERT_TRUE(editor.tools().set_setting("create_primitive", "primitive.type", "sphere").ok());
  ASSERT_TRUE(editor.tools().has_active_primitive_creation());
  send_key_press(panel, Qt::Key_3, Qt::NoModifier, QStringLiteral("3"));
  EXPECT_EQ(editor.selection().summary().mode, quader::QuaderSelectionMode::Face);
  EXPECT_FALSE(editor.tools().has_active_primitive_creation());
  EXPECT_EQ(editor.query().modeling_objects().size(), 1u);

  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Edge).ok());
  ASSERT_TRUE(editor.selection().select_edge(box.value(), 1, 2).ok());
  ASSERT_TRUE(editor.selection().select_all().ok());
  const auto mesh_before = editor.query().mesh(box.value().mesh);
  ASSERT_TRUE(mesh_before.has_value());
  ASSERT_TRUE(editor.commands().execute(
      editor.commands().command_id_for_modeling_action("bevel_edges")).ok());
  ASSERT_TRUE(editor.modeling().can_cancel_active_tool_or_preview());
  const auto preview_mesh = editor.query().mesh(box.value().mesh);
  ASSERT_TRUE(preview_mesh.has_value());
  send_key_press(panel, Qt::Key_4, Qt::NoModifier, QStringLiteral("4"));
  EXPECT_EQ(editor.selection().summary().mode, quader::QuaderSelectionMode::Object);
  EXPECT_FALSE(editor.modeling().can_cancel_active_tool_or_preview());
  const auto mesh_after = editor.query().mesh(box.value().mesh);
  ASSERT_TRUE(mesh_after.has_value());
  EXPECT_EQ(mesh_after->vertex_count, preview_mesh->vertex_count);
  EXPECT_EQ(mesh_after->primitive_count, preview_mesh->primitive_count);
  ASSERT_TRUE(editor.history().undo().ok());
  const auto restored = editor.query().mesh(box.value().mesh);
  ASSERT_TRUE(restored.has_value());
  EXPECT_EQ(restored->vertex_count, mesh_before->vertex_count);
  EXPECT_EQ(restored->primitive_count, mesh_before->primitive_count);
  ASSERT_EQ(editor.selection().summary().mode, quader::QuaderSelectionMode::Edge);
  const auto restored_mode =
      editor.selection().set_mode(quader::QuaderSelectionMode::Object);
  ASSERT_TRUE(restored_mode.ok()) << restored_mode.error_message();
  EXPECT_TRUE(restored_mode.changed());
  ASSERT_EQ(editor.selection().summary().mode, quader::QuaderSelectionMode::Object);

  const std::string face_target = editor.commands().keybinding_target_id_for_selection_mode(
      quader::QuaderSelectionMode::Face);
  ASSERT_TRUE(editor.commands().set_shortcut(face_target, "Down").ok());
  ASSERT_EQ(editor.selection().summary().mode, quader::QuaderSelectionMode::Object);
  const auto status_before_navigation = editor.commands().last_status_revision();
  send_key_press(panel, Qt::Key_Down, Qt::NoModifier);
  EXPECT_EQ(editor.commands().last_status_revision(), status_before_navigation);
  EXPECT_EQ(panel.currentItem(), panel.topLevelItem(1));
  EXPECT_EQ(editor.selection().summary().mode, quader::QuaderSelectionMode::Object);
  ASSERT_TRUE(editor.commands().set_shortcut(face_target, "Ctrl+Alt+F23").ok());
  send_key_press(panel, Qt::Key_F23, Qt::ControlModifier | Qt::AltModifier);
  EXPECT_EQ(editor.selection().summary().mode, quader::QuaderSelectionMode::Face);
  ASSERT_TRUE(editor.commands().reset_shortcut(face_target).ok());

  const std::string grid_target = editor.commands().keybinding_target_id_for_grid_preset(2);
  const auto grid_binding = editor.commands().set_shortcut(grid_target, "Ctrl+Alt+F24 G");
  ASSERT_TRUE(grid_binding.ok()) << grid_binding.error_message();
  ASSERT_TRUE(editor.viewports().set_grid_preset(6).ok());
  send_key_press(panel, Qt::Key_F24, Qt::ControlModifier | Qt::AltModifier);
  ASSERT_TRUE(editor.commands().has_pending_keybinding_chord());
  editor.commands().reset_pending_viewport_keybinding_chord();
  send_key_press(panel, Qt::Key_G, Qt::NoModifier, QStringLiteral("g"));
  EXPECT_EQ(editor.viewports().grid_preset(), 2);
  EXPECT_FALSE(editor.commands().has_pending_keybinding_chord());
  application.removeEventFilter(&filter);
}

TEST(QtCommandEventFilterTests,
     FaceTexturePanEscapeKeepsTheActiveNativeToolPreview) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-face-texture-pan-command-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  const auto box = editor.modeling().create_box(
      {.min = {-1, -1, -1}, .max = {1, 1, 1}});
  ASSERT_TRUE(box.ok());
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection().select_face(box.value(), 1).ok());
  session.history().clear();
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Extrude).ok());
  ASSERT_TRUE(editor.tools().set_setting("extrude", "extrude.amount", 1.0).ok());
  quader::QtCommandEventFilter filter(editor, [] {
    return quader::ViewportToolState{
        .active_tool = quader::ViewportToolId::Extrude,
        .selection_mode = quader::ViewportSelectionMode::Face};
  });
  application.installEventFilter(&filter);
  quader::QtFaceTexturePanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.resize(700, 900);
  panel.bind(editor);
  auto *canvas = dynamic_cast<quader::QtFaceTextureCanvas *>(
      panel.findChild<QWidget *>(QStringLiteral("face_texture.canvas")));
  ASSERT_NE(canvas, nullptr);
  panel.show();
  panel.activateWindow();
  QCoreApplication::processEvents();
  const QPointF position(canvas->rect().center());
  QMouseEvent press(QEvent::MouseButtonPress, position,
                    QPointF(canvas->mapToGlobal(position.toPoint())),
                    Qt::MiddleButton, Qt::MiddleButton, Qt::NoModifier);
  QCoreApplication::sendEvent(canvas, &press);
  ASSERT_TRUE(canvas->interaction_active());
  ASSERT_EQ(QApplication::focusWidget(), canvas);
  send_key_press(*canvas, Qt::Key_Escape, Qt::NoModifier);
  EXPECT_FALSE(canvas->interaction_active());
  EXPECT_TRUE(editor.tools().active_settings().preview_active);
  EXPECT_EQ(session.history().undo_count(), 0U);
  QMouseEvent release(QEvent::MouseButtonRelease, position,
                      QPointF(canvas->mapToGlobal(position.toPoint())),
                      Qt::MiddleButton, Qt::NoButton, Qt::NoModifier);
  QCoreApplication::sendEvent(canvas, &release);
  panel.unbind();
  application.removeEventFilter(&filter);
}

TEST(QtCommandEventFilterTests,
     FaceTextureNumericEditorsKeepKeyboardOwnershipDockedAndFloating) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-face-texture-keyboard-ownership-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  // Floating the dock starts Qt animations whose thread-local timers survive
  // QApplication. Drain both scheduler levels after the window is destroyed,
  // before application teardown discards their queued start/stop callbacks.
  struct AnimationTeardown {
    ~AnimationTeardown() {
      QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
      QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    }
  } animation_teardown;
  QApplication::setStyle(QStringLiteral("Fusion"));
  application.setQuitOnLastWindowClosed(false);
  auto standalone = quader::create_standalone_editor();
  auto &editor = standalone.editor();
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  const auto box = editor.modeling().create_box(
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      "Face Texture Keyboard Ownership");
  ASSERT_TRUE(box.ok()) << box.error_message();
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection().select_face(box.value(), 1).ok());

  quader::QtCommandEventFilter command_filter(editor, []() {
    return quader::ViewportToolState{
        .active_tool = quader::ViewportToolId::Select,
        .selection_mode = quader::ViewportSelectionMode::Face};
  });
  application.installEventFilter(&command_filter);
  QMainWindow window;
  window.resize(900, 950);
  auto *unrelated_content = new QWidget(&window);
  unrelated_content->setMinimumWidth(160);
  window.setCentralWidget(unrelated_content);
  auto *dock = new QDockWidget(&window);
  dock->setObjectName(QStringLiteral("dock_face_texture"));
  auto *panel = new quader::QtFaceTexturePanel(dock);
  dock->setWidget(panel);
  window.addDockWidget(Qt::RightDockWidgetArea, dock);
  panel->bind(editor);
  auto *scale = panel->findChild<QDoubleSpinBox *>(
      QStringLiteral("face_texture.texture.scale_x"));
  auto *scroll = panel->findChild<QScrollArea *>(
      QStringLiteral("face_texture.controls_scroll"));
  ASSERT_NE(scale, nullptr);
  ASSERT_NE(scroll, nullptr);
  auto *line = scale->findChild<QLineEdit *>();
  ASSERT_NE(line, nullptr);
  window.show();
  QCoreApplication::processEvents();
  const auto active_tool = editor.tools().active_tool();
  const auto command_revision = editor.commands().last_status_revision();

  for (bool floating : {false, true}) {
    SCOPED_TRACE(floating);
    dock->setFloating(floating);
    dock->window()->activateWindow();
    dock->window()->raise();
    scroll->ensureWidgetVisible(scale);
    line->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    ASSERT_TRUE(scale->isEnabled());
    ASSERT_EQ(QApplication::focusWidget(), scale)
        << "QAbstractSpinBox owns focus through its line editor's focus proxy";
    EXPECT_TRUE(quader::qt_event_target_belongs_to_tool_settings(line));
    ASSERT_TRUE(line->visibleRegion().contains(line->rect().center()));
    const QPoint editor_center = line->mapToGlobal(line->rect().center());
    EXPECT_TRUE(quader::qt_event_target_belongs_to_tool_settings(
        dock->window()->windowHandle(), editor_center))
        << "the native dock window keeps clicks in the numeric editor out of "
           "the viewport's external-pointer fallback";
    EXPECT_FALSE(quader::qt_event_target_belongs_to_tool_settings(
        window.windowHandle(), unrelated_content->mapToGlobal(
                                   unrelated_content->rect().center())))
        << "an unrelated main-window region keeps its own pointer ownership";

    send_key_press(*line, Qt::Key_A, Qt::ControlModifier, QStringLiteral("a"));
    EXPECT_EQ(line->selectedText(), line->text());
    send_key_press(*line, Qt::Key_Delete, Qt::NoModifier);
    EXPECT_TRUE(line->text().isEmpty());
    EXPECT_EQ(editor.selection().summary().selected_face_count, 1u);
    EXPECT_EQ(editor.query().modeling_objects().size(), 1u);
    EXPECT_EQ(editor.commands().last_status_revision(), command_revision);

    const double expected = floating ? 5.0 : 3.0;
    send_key_press(*line, floating ? Qt::Key_5 : Qt::Key_3, Qt::NoModifier,
                   floating ? QStringLiteral("5") : QStringLiteral("3"));
    send_key_press(*line, Qt::Key_Return, Qt::NoModifier);
    QCoreApplication::processEvents();
    EXPECT_EQ(editor.modeling().face_texture_settings().scale_x,
              std::optional<double>(expected));
    EXPECT_EQ(editor.selection().summary().mode, quader::QuaderSelectionMode::Face);
    EXPECT_EQ(editor.selection().summary().selected_face_count, 1u);
    EXPECT_EQ(editor.query().modeling_objects().size(), 1u);
    EXPECT_EQ(editor.tools().active_tool(), active_tool);
    EXPECT_EQ(editor.commands().last_status_revision(), command_revision);
  }
  panel->unbind();
  application.removeEventFilter(&command_filter);
}

} // namespace
