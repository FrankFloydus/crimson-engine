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
#include "ui/qt_project_home_page.hpp"
#include "ui/qt_scene_workspace.hpp"
#include "ui/qt_selection_toolbar.hpp"
#include "ui/qt_snapping_toolbar.hpp"
#include "ui/qt_tool_toolbar.hpp"
#include "ui/qt_viewport_display_toolbar.hpp"
#include "viewport/host_viewport_contract_adapter.hpp"
#include "viewport/viewport_settings.hpp"

#include <QAction>
#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QHeaderView>
#include <QIcon>
#include <QKeyEvent>
#include <QMainWindow>
#include <QMenu>
#include <QMetaObject>
#include <QModelIndex>
#include <QItemSelectionModel>
#include <QLabel>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QPixmap>
#include <QScreen>
#include <QStackedWidget>
#include <QStyleOption>
#include <QTableView>
#include <QToolBar>
#include <QToolButton>

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

void send_mouse_event(QWidget &target, QEvent::Type type, QPoint position,
                      Qt::MouseButton button, Qt::MouseButtons buttons) {
  const QPoint global = target.mapToGlobal(position);
  QMouseEvent event(type, QPointF(position), QPointF(global), button, buttons,
                    Qt::NoModifier);
  QCoreApplication::sendEvent(&target, &event);
}

void click_widget(QWidget &target, QPoint position) {
  send_mouse_event(target, QEvent::MouseButtonPress, position,
                   Qt::LeftButton, Qt::LeftButton);
  send_mouse_event(target, QEvent::MouseButtonRelease, position,
                   Qt::LeftButton, Qt::NoButton);
}

void send_key(QWidget &target, int key) {
  QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
  QCoreApplication::sendEvent(&target, &press);
  QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
  QCoreApplication::sendEvent(&target, &release);
}

int expect_persistent_button_faces(QToolBar &toolbar,
                                   int expected_minimum_height) {
  int action_button_count = 0;
  for (QToolButton *button : toolbar.findChildren<QToolButton *>()) {
    QAction *action = button->defaultAction();
    if (action == nullptr || !action->isCheckable()) {
      continue;
    }
    ++action_button_count;
    EXPECT_FALSE(button->autoRaise());
    EXPECT_TRUE(button->testAttribute(Qt::WA_Hover));
    EXPECT_TRUE(button->hasMouseTracking());
    EXPECT_EQ(button->focusPolicy(), Qt::StrongFocus);
    EXPECT_GE(button->minimumWidth(), 40);
    EXPECT_EQ(button->minimumHeight(), expected_minimum_height);
    EXPECT_FALSE(button->isDown());
    EXPECT_EQ(button->accessibleName(), action->text());
  }
  return action_button_count;
}

QImage render_toolbar_button_face(QToolButton &button,
                                  QStyle::State state) {
  QImage image(QSize(32, 32), QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  QStyleOption option;
  option.rect = image.rect();
  option.palette = button.palette();
  option.state = state;
  button.style()->drawPrimitive(QStyle::PE_PanelButtonCommand, &option,
                                &painter, &button);
  return image;
}

void expect_popup_restores_committed_combo_item(QComboBox &combo,
                                                QApplication &application) {
  ASSERT_GE(combo.currentIndex(), 0);
  ASSERT_GT(combo.count(), 1);

  for (int index = 0; index < combo.count(); ++index) {
    EXPECT_FALSE(combo.itemData(index, Qt::CheckStateRole).isValid())
        << "native combo rows must not render checkboxes: " << index;
  }

  const QModelIndex committed_index = combo.model()->index(
      combo.currentIndex(), combo.modelColumn(), combo.rootModelIndex());
  ASSERT_TRUE(committed_index.isValid());
  const int hovered_row = combo.currentIndex() == 0 ? 1 : 0;
  const QModelIndex hovered_index = combo.model()->index(
      hovered_row, combo.modelColumn(), combo.rootModelIndex());
  ASSERT_TRUE(hovered_index.isValid());

  combo.showPopup();
  application.processEvents();
  EXPECT_EQ(combo.view()->currentIndex(), committed_index);
  ASSERT_NE(combo.view()->selectionModel(), nullptr);
  EXPECT_TRUE(combo.view()->selectionModel()->isSelected(committed_index));
  EXPECT_TRUE(combo.view()->visualRect(committed_index).intersects(
      combo.view()->viewport()->rect()));

  combo.view()->selectionModel()->setCurrentIndex(
      hovered_index,
      QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
  EXPECT_EQ(combo.currentIndex(), committed_index.row());

  combo.hidePopup();
  combo.showPopup();
  application.processEvents();
  EXPECT_EQ(combo.view()->currentIndex(), committed_index);
  EXPECT_TRUE(combo.view()->selectionModel()->isSelected(committed_index));
  EXPECT_TRUE(combo.view()->visualRect(committed_index).intersects(
      combo.view()->viewport()->rect()));
  combo.hidePopup();
}

} // namespace

TEST(QtProjectWorkspaceTests,
     EditorToolbarsKeepNativeButtonFacesVisibleAtRest) {
  int argc = 1;
  char application_name[] = "quader_toolbar_test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);

  quader::QtSelectionToolbar selection_toolbar;
  quader::QtToolToolbar tool_toolbar;

  EXPECT_EQ(selection_toolbar.iconSize(), QSize(32, 32));
  EXPECT_EQ(tool_toolbar.iconSize(), QSize(32, 32));
  EXPECT_EQ(expect_persistent_button_faces(selection_toolbar, 30), 5);
  EXPECT_EQ(expect_persistent_button_faces(tool_toolbar, 40), 14);
  for (QToolButton *button :
       selection_toolbar.findChildren<QToolButton *>()) {
    if (button->defaultAction() != nullptr &&
        button->defaultAction()->isCheckable()) {
      EXPECT_EQ(button->maximumHeight(), 30);
    }
  }

  QToolButton *button = nullptr;
  for (QToolButton *candidate : tool_toolbar.findChildren<QToolButton *>()) {
    if (candidate->defaultAction() != nullptr &&
        candidate->defaultAction()->isCheckable()) {
      button = candidate;
      break;
    }
  }
  ASSERT_NE(button, nullptr);
  const QStyle::State default_state =
      QStyle::State_Active | QStyle::State_Enabled | QStyle::State_Raised;
  const QImage default_face =
      render_toolbar_button_face(*button, default_state);
  const QImage hover_face = render_toolbar_button_face(
      *button, default_state | QStyle::State_MouseOver);
  const QImage selected_face = render_toolbar_button_face(
      *button, default_state | QStyle::State_On);
  const QImage pressed_face = render_toolbar_button_face(
      *button, QStyle::State_Active | QStyle::State_Enabled |
                   QStyle::State_Sunken);

  EXPECT_NE(default_face, hover_face);
  EXPECT_NE(default_face, selected_face);
  EXPECT_NE(hover_face, selected_face);
  EXPECT_NE(default_face, pressed_face);

  QAction *action = button->defaultAction();
  action->setChecked(true);
  EXPECT_TRUE(action->isChecked());
  EXPECT_FALSE(button->isDown());
  action->setToolTip(QStringLiteral("Selection Tool [Q]"));
  EXPECT_EQ(button->accessibleDescription(),
            QStringLiteral("Selection Tool [Q]"));

  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  const std::filesystem::path keybindings =
      std::filesystem::temp_directory_path() /
      "quader-tool-toolbar-effective-shortcut.json";
  std::filesystem::remove(keybindings);
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path(keybindings).ok());
  quader::LocalizationService localization;
  tool_toolbar.set_localization(localization);
  const quader::QuaderSelectionSummary selection =
      editor.selection().summary();
  tool_toolbar.synchronize(editor, selection, true);
  auto *move = tool_toolbar.findChild<QAction *>(QStringLiteral("tool_move"));
  ASSERT_NE(move, nullptr);
  EXPECT_TRUE(move->toolTip().contains(QStringLiteral("[W]")));

  ASSERT_TRUE(editor.preferences()
                  .set_shortcut(quader::quader_command::kToolsActivateMove,
                                "Ctrl+Alt+W")
                  .ok());
  tool_toolbar.synchronize(editor, selection, true);
  EXPECT_TRUE(move->toolTip().contains(QStringLiteral("[Ctrl+Alt+W]")));
  EXPECT_FALSE(move->toolTip().contains(QStringLiteral("[W]")));
  std::filesystem::remove(keybindings);
}

TEST(QtProjectWorkspaceTests,
     SnappingToolbarUsesCanonicalControlsAndDispatchesOnlyUserIntent) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader_snapping_toolbar_test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  ASSERT_NE(QApplication::setStyle(QStringLiteral("Fusion")), nullptr);

  QMainWindow host;
  quader::QtSnappingToolbar toolbar;
  quader::LocalizationService localization;
  toolbar.set_localization(localization);

  EXPECT_EQ(toolbar.objectName(), QStringLiteral("snappingToolbar"));
  EXPECT_EQ(toolbar.windowTitle(), QStringLiteral("Snapping"));
  EXPECT_TRUE(toolbar.isMovable());
  EXPECT_TRUE(toolbar.isFloatable());
  EXPECT_EQ(toolbar.allowedAreas(), Qt::AllToolBarAreas);
  EXPECT_TRUE(toolbar.isAreaAllowed(Qt::TopToolBarArea));
  EXPECT_TRUE(toolbar.isAreaAllowed(Qt::LeftToolBarArea));
  EXPECT_TRUE(toolbar.isAreaAllowed(Qt::RightToolBarArea));
  EXPECT_TRUE(toolbar.isAreaAllowed(Qt::BottomToolBarArea));
  EXPECT_EQ(toolbar.iconSize(), QSize(32, 32));
  EXPECT_EQ(toolbar.toolButtonStyle(), Qt::ToolButtonIconOnly);
  EXPECT_EQ(expect_persistent_button_faces(toolbar, 30), 4);

  host.addToolBar(Qt::TopToolBarArea, &toolbar);
  EXPECT_EQ(host.toolBarArea(&toolbar), Qt::TopToolBarArea);
  host.removeToolBar(&toolbar);
  host.addToolBar(Qt::LeftToolBarArea, &toolbar);
  EXPECT_EQ(host.toolBarArea(&toolbar), Qt::LeftToolBarArea);

  const QList<QComboBox *> combos = toolbar.findChildren<QComboBox *>();
  ASSERT_EQ(combos.size(), 2);
  auto *grid_combo = toolbar.findChild<QComboBox *>(
      QStringLiteral("snappingGridSizeCombo"));
  auto *angle_combo = toolbar.findChild<QComboBox *>(
      QStringLiteral("snappingAngleCombo"));
  ASSERT_NE(grid_combo, nullptr);
  ASSERT_NE(angle_combo, nullptr);
  EXPECT_EQ(grid_combo->minimumHeight(), 30);
  EXPECT_EQ(grid_combo->maximumHeight(), 30);
  EXPECT_EQ(angle_combo->minimumHeight(), 30);
  EXPECT_EQ(angle_combo->maximumHeight(), 30);

  const auto grid_catalog = quader::viewport_grid_preset_catalog();
  ASSERT_EQ(grid_combo->count(), static_cast<int>(grid_catalog.size()));
  int default_grid_preset = 0;
  for (std::size_t index = 0; index < grid_catalog.size(); ++index) {
    const auto &preset = grid_catalog[index];
    EXPECT_EQ(grid_combo->itemText(static_cast<int>(index)),
              QString::number(preset.map_units, 'g', 15));
    EXPECT_EQ(grid_combo->itemData(static_cast<int>(index)).toInt(),
              preset.preset);
    if (preset.is_default) {
      ASSERT_EQ(default_grid_preset, 0)
          << "the canonical grid catalog must have one default";
      default_grid_preset = preset.preset;
    }
  }
  EXPECT_EQ(default_grid_preset, 6);

  constexpr std::array<double, 11> kExpectedAngles{
      0.25, 0.5, 1.0, 5.0, 5.625, 11.25,
      15.0, 22.5, 30.0, 45.0, 90.0,
  };
  const auto &angle_catalog =
      quader::viewport_rotation_snap_degrees_catalog();
  ASSERT_EQ(angle_catalog.size(), kExpectedAngles.size());
  ASSERT_EQ(angle_combo->count(), static_cast<int>(kExpectedAngles.size()));
  for (std::size_t index = 0; index < kExpectedAngles.size(); ++index) {
    EXPECT_DOUBLE_EQ(angle_catalog[index], kExpectedAngles[index]);
    EXPECT_DOUBLE_EQ(
        angle_combo->itemData(static_cast<int>(index)).toDouble(),
        kExpectedAngles[index]);
    EXPECT_EQ(angle_combo->itemText(static_cast<int>(index)),
              QStringLiteral("%1\u00B0").arg(
                  QString::number(kExpectedAngles[index], 'g', 15)));
  }

  std::vector<QAction *> toggle_actions;
  for (QAction *action : toolbar.actions()) {
    if (action->isCheckable()) {
      toggle_actions.push_back(action);
    }
  }
  ASSERT_EQ(toggle_actions.size(), 4u);
  const std::array<QString, 4> expected_action_names{
      QStringLiteral("snappingEnabled"),
      QStringLiteral("gridSnapping"),
      QStringLiteral("vertexSnapping"),
      QStringLiteral("rotationSnapping"),
  };
  const std::array<QString, 4> expected_action_labels{
      QStringLiteral("Toggle Snapping"),
      QStringLiteral("Grid Snapping"),
      QStringLiteral("Vertex Snapping"),
      QStringLiteral("Rotation Snapping"),
  };
  for (std::size_t index = 0; index < toggle_actions.size(); ++index) {
    EXPECT_EQ(toggle_actions[index]->objectName(),
              expected_action_names[index]);
    EXPECT_EQ(toggle_actions[index]->text(), expected_action_labels[index]);
    EXPECT_EQ(toggle_actions[index]->toolTip(),
              expected_action_labels[index]);
  }
  EXPECT_EQ(toggle_actions.front()->objectName(),
            QStringLiteral("snappingEnabled"))
      << "the global snapping master must be the first icon action";

  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  ASSERT_TRUE(editor.commands()
                  .set_keybinding_storage_path(std::filesystem::path{})
                  .ok());
  toolbar.synchronize(editor, true);

  EXPECT_EQ(editor.viewports().grid_preset(), default_grid_preset);
  EXPECT_EQ(grid_combo->currentData().toInt(), default_grid_preset);
  EXPECT_DOUBLE_EQ(editor.preferences().rotation_snap_degrees(), 15.0);
  EXPECT_DOUBLE_EQ(angle_combo->currentData().toDouble(), 15.0);
  EXPECT_EQ(editor.commands().shortcut_for(
                editor.commands().keybinding_target_id_for_grid_preset(
                    default_grid_preset)),
            "");
  EXPECT_EQ(grid_combo->toolTip(), QStringLiteral("Grid Size"));
  EXPECT_EQ(grid_combo->accessibleDescription(),
            QStringLiteral("Grid Size"));
  EXPECT_EQ(angle_combo->toolTip(),
            QStringLiteral("Rotation Snap Angle"));
  EXPECT_EQ(angle_combo->accessibleDescription(),
            QStringLiteral("Rotation Snap Angle"));

  host.removeToolBar(&toolbar);
  host.addToolBar(Qt::TopToolBarArea, &toolbar);
  host.resize(800, 600);
  host.move(40, 40);
  host.show();
  application.processEvents();
  expect_popup_restores_committed_combo_item(*grid_combo, application);
  expect_popup_restores_committed_combo_item(*angle_combo, application);
  ASSERT_EQ(angle_combo->height(), 30);
  const QPoint popup_anchor =
      angle_combo->mapToGlobal(QPoint(0, angle_combo->height()));
  angle_combo->showPopup();
  application.processEvents();
  QWidget *angle_popup = angle_combo->view()->window();
  ASSERT_NE(angle_popup, nullptr);
  ASSERT_TRUE(angle_popup->isVisible());
  QScreen *popup_screen = QGuiApplication::screenAt(popup_anchor);
  ASSERT_NE(popup_screen, nullptr);
  ASSERT_LE(popup_anchor.y() + angle_popup->height() - 1,
            popup_screen->availableGeometry().bottom());
  EXPECT_EQ(angle_popup->geometry().top(), popup_anchor.y());
  angle_combo->hidePopup();

  int grid_requests = 0;
  int requested_grid_preset = 0;
  int angle_requests = 0;
  double requested_angle = 0.0;
  std::array<int, 4> toggle_requests{};
  std::array<bool, 4> requested_toggles{};
  toolbar.set_grid_preset_requested_callback(
      [&](int preset) {
        ++grid_requests;
        requested_grid_preset = preset;
      });
  toolbar.set_rotation_snap_degrees_requested_callback(
      [&](double degrees) {
        ++angle_requests;
        requested_angle = degrees;
      });
  toolbar.set_snapping_enabled_requested_callback([&](bool enabled) {
    ++toggle_requests[0];
    requested_toggles[0] = enabled;
  });
  toolbar.set_grid_snap_enabled_requested_callback([&](bool enabled) {
    ++toggle_requests[1];
    requested_toggles[1] = enabled;
  });
  toolbar.set_vertex_snap_enabled_requested_callback([&](bool enabled) {
    ++toggle_requests[2];
    requested_toggles[2] = enabled;
  });
  toolbar.set_rotation_snap_enabled_requested_callback([&](bool enabled) {
    ++toggle_requests[3];
    requested_toggles[3] = enabled;
  });

  ASSERT_TRUE(QMetaObject::invokeMethod(
      grid_combo, "activated", Qt::DirectConnection, Q_ARG(int, 2)));
  EXPECT_EQ(grid_requests, 1);
  EXPECT_EQ(requested_grid_preset, 3);
  ASSERT_TRUE(QMetaObject::invokeMethod(
      angle_combo, "activated", Qt::DirectConnection, Q_ARG(int, 8)));
  EXPECT_EQ(angle_requests, 1);
  EXPECT_DOUBLE_EQ(requested_angle, 30.0);
  for (std::size_t index = 0; index < toggle_actions.size(); ++index) {
    ASSERT_FALSE(toggle_actions[index]->isChecked());
    toggle_actions[index]->trigger();
    EXPECT_TRUE(toggle_actions[index]->isChecked());
    EXPECT_EQ(toggle_requests[index], 1);
    EXPECT_TRUE(requested_toggles[index]);
  }

  const std::array<bool, 4> owner_toggles{
      false, true, false, true,
  };
  const double owner_angle = 90.0;
  editor.preferences().set_runtime_hooks(
      quader::QuaderPreferencesRuntimeHooks{
          .snapping_enabled = [&] { return owner_toggles[0]; },
          .grid_snap_enabled = [&] { return owner_toggles[1]; },
          .vertex_snap_enabled = [&] { return owner_toggles[2]; },
          .rotation_snap_enabled = [&] { return owner_toggles[3]; },
          .rotation_snap_degrees = [&] { return owner_angle; },
      });
  const quader::QuaderApiVoidResult grid_result =
      editor.viewports().set_grid_preset(10);
  ASSERT_TRUE(grid_result.ok()) << grid_result.error_message();

  int grid_index_change_signals = 0;
  int angle_index_change_signals = 0;
  std::array<int, 4> toggle_change_signals{};
  QObject::connect(
      grid_combo, qOverload<int>(&QComboBox::currentIndexChanged), &toolbar,
      [&](int) { ++grid_index_change_signals; });
  QObject::connect(
      angle_combo, qOverload<int>(&QComboBox::currentIndexChanged), &toolbar,
      [&](int) { ++angle_index_change_signals; });
  for (std::size_t index = 0; index < toggle_actions.size(); ++index) {
    QObject::connect(toggle_actions[index], &QAction::toggled, &toolbar,
                     [&, index](bool) {
                       ++toggle_change_signals[index];
                     });
  }

  toolbar.synchronize(editor, true);
  EXPECT_EQ(grid_combo->currentData().toInt(), 10);
  EXPECT_DOUBLE_EQ(angle_combo->currentData().toDouble(), owner_angle);
  EXPECT_EQ(grid_combo->toolTip(), QStringLiteral("Grid Size"));
  EXPECT_EQ(grid_index_change_signals, 0);
  EXPECT_EQ(angle_index_change_signals, 0);
  EXPECT_EQ(toggle_change_signals, (std::array<int, 4>{}));
  EXPECT_EQ(grid_requests, 1);
  EXPECT_EQ(angle_requests, 1);
  EXPECT_EQ(toggle_requests, (std::array<int, 4>{1, 1, 1, 1}));
  expect_popup_restores_committed_combo_item(*grid_combo, application);
  expect_popup_restores_committed_combo_item(*angle_combo, application);
  for (std::size_t index = 0; index < toggle_actions.size(); ++index) {
    EXPECT_EQ(toggle_actions[index]->isChecked(), owner_toggles[index]);
  }

  toolbar.synchronize(editor);
  EXPECT_EQ(grid_index_change_signals, 0);
  EXPECT_EQ(angle_index_change_signals, 0);
  EXPECT_EQ(toggle_change_signals, (std::array<int, 4>{}));
  EXPECT_EQ(grid_requests, 1);
  EXPECT_EQ(angle_requests, 1);
  EXPECT_EQ(toggle_requests, (std::array<int, 4>{1, 1, 1, 1}));
}

TEST(QtProjectWorkspaceTests,
     ViewportDisplayToolbarDispatchesTypedCommandsAndReflectsOwnerState) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader_viewport_display_toolbar_test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  ASSERT_NE(QApplication::setStyle(QStringLiteral("Fusion")), nullptr);

  quader::QtViewportDisplayToolbar toolbar;
  quader::LocalizationService localization;
  toolbar.set_localization(localization);
  EXPECT_EQ(toolbar.objectName(), QStringLiteral("viewportDisplayToolbar"));
  EXPECT_EQ(toolbar.windowTitle(), QStringLiteral("Viewport Display"));
  EXPECT_TRUE(toolbar.isMovable());
  EXPECT_TRUE(toolbar.isFloatable());
  EXPECT_EQ(toolbar.iconSize(), QSize(32, 32));
  EXPECT_EQ(expect_persistent_button_faces(toolbar, 30), 4);

  std::vector<QAction *> actions;
  for (QAction *action : toolbar.actions()) {
    if (action->isCheckable()) {
      actions.push_back(action);
    }
  }
  ASSERT_EQ(actions.size(), 4u);
  const std::array<QString, 4> expected_names{
      QStringLiteral("viewportGrid"),
      QStringLiteral("viewportGizmos"),
      QStringLiteral("viewportMeshGrid"),
      QStringLiteral("viewportOverlays"),
  };
  const std::array<QString, 4> expected_labels{
      QStringLiteral("Toggle Grid"),
      QStringLiteral("Toggle Gizmos"),
      QStringLiteral("Toggle Mesh Grid"),
      QStringLiteral("Toggle Overlays"),
  };
  for (std::size_t index = 0; index < actions.size(); ++index) {
    EXPECT_EQ(actions[index]->objectName(), expected_names[index]);
    EXPECT_EQ(actions[index]->text(), expected_labels[index]);
  }

  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  const std::filesystem::path keybindings =
      std::filesystem::temp_directory_path() /
      "quader-viewport-display-effective-shortcut.json";
  std::filesystem::remove(keybindings);
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path(keybindings).ok());
  std::array<bool, 4> owner_state{};
  const auto runtime_result = [](bool changed) {
    return quader::QuaderApiVoidResult{
        .ok = true,
        .changed = changed,
        .mutation_kind = quader::QuaderMutationKind::Runtime,
    };
  };
  const auto set_state = [&](std::size_t index, bool visible) {
    const bool changed = owner_state[index] != visible;
    owner_state[index] = visible;
    return runtime_result(changed);
  };
  editor.preferences().set_runtime_hooks(
      quader::QuaderPreferencesRuntimeHooks{
          .grid_visible = [&] { return owner_state[0]; },
          .set_grid_visible = [&](bool value) { return set_state(0, value); },
          .gizmos_visible = [&] { return owner_state[1]; },
          .set_gizmos_visible = [&](bool value) { return set_state(1, value); },
          .mesh_grid_visible = [&] { return owner_state[2]; },
          .set_mesh_grid_visible =
              [&](bool value) { return set_state(2, value); },
          .overlays_visible = [&] { return owner_state[3]; },
          .set_overlays_visible =
              [&](bool value) { return set_state(3, value); },
      });
  toolbar.synchronize(editor, true);
  ASSERT_TRUE(editor.preferences()
                  .set_shortcut(quader::quader_command::kViewportToggleGrid,
                                "Ctrl+Alt+F12")
                  .ok());
  toolbar.synchronize(editor, true);
  EXPECT_TRUE(
      actions[0]->toolTip().contains(QStringLiteral("[Ctrl+Alt+F12]")));

  const std::array<quader::QuaderCommandId, 4> expected_commands{
      quader::quader_command::kViewportToggleGrid,
      quader::quader_command::kViewportToggleGizmos,
      quader::quader_command::kViewportToggleMeshGrid,
      quader::quader_command::kViewportToggleOverlays,
  };
  std::vector<std::string> requested_commands;
  toolbar.set_command_requested_callback(
      [&](quader::QuaderCommandId command_id) {
        requested_commands.emplace_back(command_id.string());
        const quader::QuaderApiVoidResult result =
            editor.commands().execute(command_id);
        EXPECT_TRUE(result.ok()) << result.error_message();
      });

  for (std::size_t index = 0; index < actions.size(); ++index) {
    EXPECT_FALSE(actions[index]->isChecked());
    actions[index]->trigger();
    toolbar.synchronize(editor, true);
    EXPECT_TRUE(actions[index]->isChecked());
    EXPECT_TRUE(owner_state[index]);
  }
  ASSERT_EQ(requested_commands.size(), expected_commands.size());
  for (std::size_t index = 0; index < expected_commands.size(); ++index) {
    EXPECT_EQ(requested_commands[index], expected_commands[index].string());
  }
  std::filesystem::remove(keybindings);
}

TEST(QtProjectWorkspaceTests,
     SceneLabelsUseShortestUniqueParentsAndDirtyAsterisk) {
  const std::vector<quader::QtSceneTabItem> scenes{
      {.session_id = 1,
       .name = QStringLiteral("Main"),
       .project_relative_path = QStringLiteral("Scenes/A/Main.qdr")},
      {.session_id = 2,
       .name = QStringLiteral("Main"),
       .project_relative_path = QStringLiteral("Archive/A/Main.qdr"),
       .dirty = true},
      {.session_id = 3,
       .name = QStringLiteral("Props"),
       .project_relative_path = QStringLiteral("Scenes/Props.qdr")},
  };

  const std::vector<QString> labels =
      quader::qt_scene_tab_display_labels(scenes);

  ASSERT_EQ(labels.size(), 3u);
  EXPECT_EQ(labels[0], QStringLiteral("Scenes/A/Main"));
  EXPECT_EQ(labels[1], QStringLiteral("Archive/A/Main *"));
  EXPECT_EQ(labels[2], QStringLiteral("Props"));
}

TEST(QtProjectWorkspaceTests,
     DuplicateUnsavedNamesUseStableOrdinalDisambiguation) {
  const std::vector<quader::QtSceneTabItem> scenes{
      {.session_id = 11, .name = QStringLiteral("Untitled Scene")},
      {.session_id = 12,
       .name = QStringLiteral("Untitled Scene"),
       .dirty = true},
  };

  const std::vector<QString> labels =
      quader::qt_scene_tab_display_labels(scenes);

  ASSERT_EQ(labels.size(), 2u);
  EXPECT_EQ(labels[0], QStringLiteral("Untitled Scene (1)"));
  EXPECT_EQ(labels[1], QStringLiteral("Untitled Scene (2) *"));
}

TEST(QtProjectWorkspaceTests,
     HomeAndWorkspaceDispatchPresentationIntentWithoutMutatingModels) {
  int argc = 1;
  char application_name[] = "quader_ui_test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  QPixmap application_icon_pixmap(16, 16);
  application_icon_pixmap.fill(Qt::cyan);
  QApplication::setWindowIcon(QIcon(application_icon_pixmap));

  quader::QtProjectHomePage home;
  int new_project_requests = 0;
  int open_project_requests = 0;
  std::vector<std::pair<quader::QtRecentProjectAction, QString>>
      recent_actions;
  home.set_new_project_requested_callback(
      [&new_project_requests]() { ++new_project_requests; });
  home.set_open_project_requested_callback(
      [&open_project_requests]() { ++open_project_requests; });
  home.set_recent_project_action_requested_callback(
      [&recent_actions](quader::QtRecentProjectAction action, QString path) {
        recent_actions.emplace_back(action, std::move(path));
      });
  const std::vector<quader::QtRecentProjectItem> recent_projects{
      {.name = QStringLiteral("Available"),
       .manifest_path = QStringLiteral("C:/Projects/Available/Available.qproj"),
       .manifest_last_modified_unix_ms = 1704067200000,
       .manifest_size_bytes = 2048u},
      {.name = QStringLiteral("Missing"),
       .manifest_path = QStringLiteral("C:/Projects/Missing/Missing.qproj"),
       .available = false},
  };
  home.set_recent_projects(recent_projects);
  home.resize(900, 600);
  home.show();
  QCoreApplication::processEvents();

  EXPECT_EQ(home.findChild<QLabel *>(QStringLiteral("projectHome.title")),
            nullptr);
  EXPECT_NE(home.findChild<QLabel *>(
                QStringLiteral("projectHome.recentProjects.label")),
            nullptr);
  auto *left_rail = home.findChild<QWidget *>(
      QStringLiteral("projectHome.leftRail"));
  ASSERT_NE(left_rail, nullptr);
  EXPECT_EQ(left_rail->minimumWidth(), left_rail->maximumWidth());
  auto *brand_icon = home.findChild<QLabel *>(
      QStringLiteral("projectHome.brand.icon"));
  ASSERT_NE(brand_icon, nullptr);
  EXPECT_FALSE(brand_icon->pixmap().isNull());
  EXPECT_EQ(brand_icon->accessibleName(), QStringLiteral("Quader"));
  EXPECT_NE(home.findChild<QLabel *>(
                QStringLiteral("projectHome.brand.name")),
            nullptr);
  auto *recent_navigation = home.findChild<QPushButton *>(
      QStringLiteral("projectHome.navigation.recent"));
  ASSERT_NE(recent_navigation, nullptr);
  EXPECT_TRUE(recent_navigation->isChecked());
  auto *actions_menu = home.findChild<QMenu *>(
      QStringLiteral("projectHome.recentProjects.actionsMenu"));
  ASSERT_NE(actions_menu, nullptr)
      << "the recent actions menu must be built with the Home page";
  ASSERT_EQ(actions_menu->actions().size(), 4);
  QAction *open_recent = actions_menu->actions()[0];
  QAction *open_location = actions_menu->actions()[1];
  QAction *copy_path = actions_menu->actions()[2];
  QAction *remove_recent = actions_menu->actions()[3];
  EXPECT_EQ(open_recent->text(), QStringLiteral("Open"));
  EXPECT_EQ(open_location->text(),
            QStringLiteral("Open file location"));
  EXPECT_EQ(copy_path->text(), QStringLiteral("Copy path"));
  EXPECT_EQ(remove_recent->text(),
            QStringLiteral("Remove from recent"));

  home.findChild<QPushButton *>(
          QStringLiteral("projectHome.newProject"))
      ->click();
  home.findChild<QPushButton *>(
          QStringLiteral("projectHome.openProject"))
      ->click();
  auto *recent_view = home.findChild<QTableView *>(
      QStringLiteral("projectHome.recentProjects.view"));
  ASSERT_NE(recent_view, nullptr);
  ASSERT_EQ(recent_view->model()->columnCount(),
            quader::QtRecentProjectsModel::ColumnCount);
  EXPECT_EQ(recent_view->model()
                ->headerData(quader::QtRecentProjectsModel::NameColumn,
                             Qt::Horizontal)
                .toString(),
            QStringLiteral("Name"));
  EXPECT_EQ(recent_view->model()
                ->headerData(
                    quader::QtRecentProjectsModel::LastModifiedColumn,
                    Qt::Horizontal)
                .toString(),
            QStringLiteral("Last modified"));
  EXPECT_EQ(recent_view->model()
                ->headerData(quader::QtRecentProjectsModel::SizeColumn,
                             Qt::Horizontal)
                .toString(),
            QStringLiteral("Size"));
  EXPECT_EQ(recent_view->model()
                ->index(0, quader::QtRecentProjectsModel::NameColumn)
                .data()
                .toString(),
            QStringLiteral(
                "Available\nC:/Projects/Available/Available.qproj"));
  EXPECT_FALSE(recent_view->model()
                   ->index(0,
                           quader::QtRecentProjectsModel::LastModifiedColumn)
                   .data()
                   .toString()
                   .isEmpty());
  EXPECT_NE(recent_view->model()
                ->index(0, quader::QtRecentProjectsModel::SizeColumn)
                .data()
                .toString(),
            QStringLiteral("\u2014"));
  EXPECT_EQ(recent_view->model()
                ->index(1,
                        quader::QtRecentProjectsModel::LastModifiedColumn)
                .data()
                .toString(),
            QStringLiteral("\u2014"));
  EXPECT_TRUE(recent_view->wordWrap());
  EXPECT_GE(recent_view->verticalHeader()->defaultSectionSize(), 48);
  EXPECT_EQ(recent_view->model()
                ->index(0, quader::QtRecentProjectsModel::ActionsColumn)
                .data()
                .toString(),
            QStringLiteral("\u2026"));
  QTableView stock_table;
  EXPECT_EQ(recent_view->selectionMode(),
            QAbstractItemView::SingleSelection);
  EXPECT_EQ(recent_view->editTriggers(), stock_table.editTriggers());
  EXPECT_TRUE(recent_view->model()->flags(
                  recent_view->model()->index(1, 0)) & Qt::ItemIsEnabled)
      << "missing projects must remain selectable so they can be removed";
  const QModelIndex available_index = recent_view->model()->index(0, 0);
  const QRect available_rect = recent_view->visualRect(available_index);
  ASSERT_TRUE(available_rect.isValid());
  click_widget(*recent_view->viewport(), available_rect.center());
  EXPECT_EQ(recent_view->currentIndex().row(), 0);
  home.set_recent_projects(recent_projects);
  EXPECT_EQ(recent_view->currentIndex().row(), 0)
      << "an unchanged app refresh must preserve the selected recent project";
  send_key(*recent_view, Qt::Key_Return);
  EXPECT_EQ(new_project_requests, 1);
  EXPECT_EQ(open_project_requests, 1);
  ASSERT_EQ(recent_actions.size(), 1u);
  EXPECT_EQ(recent_actions.back().first,
            quader::QtRecentProjectAction::Open);
  EXPECT_EQ(recent_actions.back().second,
            QStringLiteral("C:/Projects/Available/Available.qproj"));
  recent_actions.clear();
  const QModelIndex missing_index = recent_view->model()->index(1, 0);
  const QRect missing_rect = recent_view->visualRect(missing_index);
  ASSERT_TRUE(missing_rect.isValid());
  click_widget(*recent_view->viewport(), missing_rect.center());
  EXPECT_EQ(recent_view->currentIndex().row(), 1);
  send_key(*recent_view, Qt::Key_Return);
  EXPECT_TRUE(recent_actions.empty());

  const QModelIndex missing_actions_index = recent_view->model()->index(
      1, quader::QtRecentProjectsModel::ActionsColumn);
  const QRect missing_actions_rect =
      recent_view->visualRect(missing_actions_index);
  ASSERT_TRUE(missing_actions_rect.isValid());
  click_widget(*recent_view->viewport(), missing_actions_rect.center());
  QCoreApplication::processEvents();
  EXPECT_FALSE(open_recent->isEnabled());
  EXPECT_TRUE(open_location->isEnabled());
  EXPECT_TRUE(copy_path->isEnabled());
  EXPECT_TRUE(remove_recent->isEnabled());
  for (int row = 0; row < recent_view->model()->rowCount(); ++row) {
    EXPECT_EQ(recent_view->indexWidget(recent_view->model()->index(
                  row, quader::QtRecentProjectsModel::ActionsColumn)),
              nullptr)
        << "the shared menu must not create a widget for each row";
  }

  open_recent->trigger();
  EXPECT_TRUE(recent_actions.empty());
  open_location->trigger();
  copy_path->trigger();
  remove_recent->trigger();
  ASSERT_EQ(recent_actions.size(), 3u);
  EXPECT_EQ(recent_actions[0].first,
            quader::QtRecentProjectAction::OpenFileLocation);
  EXPECT_EQ(recent_actions[1].first,
            quader::QtRecentProjectAction::CopyPath);
  EXPECT_EQ(recent_actions[2].first,
            quader::QtRecentProjectAction::RemoveFromRecent);
  for (const auto &[action, path] : recent_actions) {
    (void)action;
    EXPECT_EQ(path,
              QStringLiteral("C:/Projects/Missing/Missing.qproj"));
  }

  actions_menu->hide();
  const QModelIndex available_actions_index = recent_view->model()->index(
      0, quader::QtRecentProjectsModel::ActionsColumn);
  click_widget(*recent_view->viewport(),
               recent_view->visualRect(available_actions_index).center());
  QCoreApplication::processEvents();
  EXPECT_EQ(home.findChild<QMenu *>(
                QStringLiteral("projectHome.recentProjects.actionsMenu")),
            actions_menu)
      << "every row must reuse the menu built with the Home page";
  EXPECT_TRUE(open_recent->isEnabled());
  EXPECT_EQ(home.findChild<QPushButton *>(
                QStringLiteral("projectHome.removeRecent")),
            nullptr);
  actions_menu->hide();

  QWidget viewport;
  quader::QtSceneWorkspace workspace;
  workspace.set_viewport_widget(&viewport);
  std::optional<quader::QtSceneSessionId> activated;
  std::optional<quader::QtSceneSessionId> closed;
  int new_scene_requests = 0;
  int open_scene_requests = 0;
  workspace.set_new_scene_requested_callback(
      [&new_scene_requests]() { ++new_scene_requests; });
  workspace.set_open_scene_requested_callback(
      [&open_scene_requests]() { ++open_scene_requests; });
  workspace.set_activate_scene_requested_callback(
      [&activated](quader::QtSceneSessionId id) { activated = id; });
  workspace.set_close_scene_requested_callback(
      [&closed](quader::QtSceneSessionId id) { closed = id; });
  int exit_instance_edit_requests = 0;
  workspace.set_exit_instance_edit_requested_callback(
      [&exit_instance_edit_requests]() { ++exit_instance_edit_requests; });

  EXPECT_TRUE(workspace.set_scene_tabs(
      {{.session_id = 41, .name = QStringLiteral("One")},
       {.session_id = 42,
        .name = QStringLiteral("Two"),
        .project_relative_path = QStringLiteral("Scenes/Two.qdr")}},
      41));
  EXPECT_EQ(workspace.tab_bar().tabData(0).toULongLong(), 41u);
  EXPECT_EQ(workspace.tab_bar().tabData(1).toULongLong(), 42u);
  workspace.tab_bar().setCurrentIndex(1);
  EXPECT_EQ(activated, 42u);
  ASSERT_TRUE(QMetaObject::invokeMethod(
      &workspace.tab_bar(), "tabCloseRequested", Qt::DirectConnection,
      Q_ARG(int, 1)));
  EXPECT_EQ(closed, 42u);

  QWidget *breadcrumb_bar = workspace.findChild<QWidget *>(
      QStringLiteral("sceneWorkspace.instanceBreadcrumb"));
  auto *scene_crumb = workspace.findChild<QToolButton *>(
      QStringLiteral("sceneWorkspace.instanceBreadcrumb.scene"));
  auto *instance_crumb = workspace.findChild<QLabel *>(
      QStringLiteral("sceneWorkspace.instanceBreadcrumb.instance"));
  ASSERT_NE(breadcrumb_bar, nullptr);
  ASSERT_NE(scene_crumb, nullptr);
  ASSERT_NE(instance_crumb, nullptr);
  EXPECT_TRUE(breadcrumb_bar->isHidden());
  const int unchanged_tab_count = workspace.tab_bar().count();
  workspace.set_instance_edit_breadcrumb(quader::QtInstanceEditBreadcrumb{
      .scene_name = QStringLiteral("Two"),
      .instance_name = QStringLiteral("Cylinder Instance"),
  });
  EXPECT_FALSE(breadcrumb_bar->isHidden());
  EXPECT_EQ(workspace.tab_bar().count(), unchanged_tab_count)
      << "instance editing adds a lower row rather than replacing scene tabs";
  EXPECT_EQ(scene_crumb->text(), QStringLiteral("Two"));
  EXPECT_EQ(instance_crumb->text(), QStringLiteral("Cylinder Instance"));
  EXPECT_FALSE(scene_crumb->accessibleName().isEmpty());
  EXPECT_FALSE(instance_crumb->accessibleName().isEmpty());
  scene_crumb->click();
  EXPECT_EQ(exit_instance_edit_requests, 1);
  workspace.set_instance_edit_breadcrumb(std::nullopt);
  EXPECT_TRUE(breadcrumb_bar->isHidden());

  auto *new_scene = workspace.findChild<QToolButton *>(
      QStringLiteral("sceneWorkspace.newScene"));
  ASSERT_NE(new_scene, nullptr);
  new_scene->click();
  EXPECT_EQ(new_scene_requests, 1);

  EXPECT_TRUE(workspace.set_scene_tabs({}));
  auto *open_scene = workspace.findChild<QPushButton *>(
      QStringLiteral("sceneWorkspace.empty.openScene"));
  ASSERT_NE(open_scene, nullptr);
  open_scene->click();
  EXPECT_EQ(open_scene_requests, 1);

  workspace.set_viewport_widget(nullptr);
}
