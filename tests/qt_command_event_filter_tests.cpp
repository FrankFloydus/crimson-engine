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
#include "ui/qt_face_texture_panel.hpp"
#include "ui/qt_material_browser_window.hpp"
#include "ui/qt_viewport_panel.hpp"
#include "ui/qt_tool_settings_panel.hpp"
#include "ui/qt_viewport_surface.hpp"
#include "ui/ui_context.hpp"
#include "ui/ui_settings.hpp"
#include "viewport/viewport_settings.hpp"
#include "viewport/host_viewport_contract_adapter.hpp"

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QMainWindow>
#include <QScrollArea>
#include <QWindow>
#include <QEnterEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMenuBar>
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
    ASSERT_TRUE(editor.commands().execute(dispatch.command_id).ok());
    const auto after = editor.query().compiled_object_mesh(box.value());
    ASSERT_TRUE(after.has_value());
    EXPECT_GT(after->indices.size(), before->indices.size());
    EXPECT_EQ(editor.tools().active_tool(), "extrude");
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
     RejectedDirectToolSettingsCommitNotifiesOncePerAttempt) {
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
  QLineEdit edit(&panel);
  panel.show();
  panel.activateWindow();
  edit.setFocus(Qt::OtherFocusReason);
  QCoreApplication::processEvents();
  ASSERT_EQ(QApplication::focusWidget(), &edit);
  const auto command_revision = editor.commands().last_status_revision();

  send_key_press(edit, Qt::Key_Space, Qt::NoModifier);
  ASSERT_EQ(notifications.size(), 1u);
  EXPECT_EQ(notifications.front().level, quader::QuaderNotificationLevel::Error);
  EXPECT_FALSE(notifications.front().message.empty());
  EXPECT_EQ(editor.commands().last_status_revision(), command_revision);
  QCoreApplication::processEvents();
  EXPECT_EQ(notifications.size(), 1u);
  send_key_press(edit, Qt::Key_Space, Qt::NoModifier);
  ASSERT_EQ(notifications.size(), 2u);
  EXPECT_EQ(notifications.back().message, notifications.front().message);
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
     EditorPanelModeAndGridBindingsUseOwnersAndCancelActivePreviews) {
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
  send_key_press(panel, Qt::Key_4, Qt::NoModifier, QStringLiteral("4"));
  EXPECT_EQ(editor.selection().summary().mode, quader::QuaderSelectionMode::Object);
  EXPECT_FALSE(editor.modeling().can_cancel_active_tool_or_preview());
  const auto mesh_after = editor.query().mesh(box.value().mesh);
  ASSERT_TRUE(mesh_after.has_value());
  EXPECT_EQ(mesh_after->vertex_count, mesh_before->vertex_count);
  EXPECT_EQ(mesh_after->primitive_count, mesh_before->primitive_count);

  const std::string face_target = editor.commands().keybinding_target_id_for_selection_mode(
      quader::QuaderSelectionMode::Face);
  ASSERT_TRUE(editor.commands().set_shortcut(face_target, "Down").ok());
  send_key_press(panel, Qt::Key_Down, Qt::NoModifier);
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
     FaceTextureNumericEditorsKeepKeyboardOwnershipDockedAndFloating) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-face-texture-keyboard-ownership-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
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
