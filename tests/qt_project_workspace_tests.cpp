////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "api/quader_polygon_document_internal_access.hpp"
#include "assets/default_material_catalog.hpp"
#include "core/localization_service.hpp"
#include "editor/material/project_material_usage_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/modeling/services/primitive_creation_tool_service.hpp"
#include "editor/session/project_session.hpp"
#include "editor/session/quader_editor_api_session_access.hpp"
#include "editor/session/scene_session.hpp"
#include "render/render_system.hpp"
#include "quader/modeling/neutral/polygon/document_model.hpp"
#include "ui/qt_application_theme.hpp"
#include "ui/qt_auxiliary_window_state.hpp"
#include "ui/qt_main_menu.hpp"
#include "ui/qt_material_browser_window.hpp"
#include "ui/qt_operation_input_hints.hpp"
#include "ui/qt_project_home_page.hpp"
#include "ui/qt_scene_workspace.hpp"
#include "ui/qt_selection_toolbar.hpp"
#include "ui/qt_snapping_toolbar.hpp"
#include "ui/qt_status_message.hpp"
#include "ui/qt_texture_toolbar.hpp"
#include "ui/qt_toolbar_button_presentation.hpp"
#include "ui/qt_tool_toolbar.hpp"
#include "ui/qt_ui_scale_metrics.hpp"
#include "ui/qt_viewport_panel.hpp"
#include "ui/qt_viewport_surface.hpp"
#include "ui/qt_viewport_display_toolbar.hpp"
#include "ui/ui_system.hpp"
#include "viewport/host_viewport_contract_adapter.hpp"
#include "viewport/viewport_settings.hpp"

#include <QAction>
#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QApplication>
#include <QBuffer>
#include <QByteArray>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDataStream>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFrame>
#include <QFocusEvent>
#include <QGuiApplication>
#include <QHeaderView>
#include <QIcon>
#include <QIODevice>
#include <QKeyEvent>
#include <QMainWindow>
#include <QListView>
#include <QMenu>
#include <QMenuBar>
#include <QMetaObject>
#include <QMimeData>
#include <QModelIndex>
#include <QItemSelectionModel>
#include <QLabel>
#include <QImage>
#include <QLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPushButton>
#include <QPixmap>
#include <QScreen>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyleOption>
#include <QTableView>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>

#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#ifndef QUADER_SOURCE_DIR
#define QUADER_SOURCE_DIR "."
#endif

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

bool set_local_app_data(const std::optional<std::string> &value) {
#if defined(_WIN32)
  return _putenv_s("LOCALAPPDATA", value ? value->c_str() : "") == 0;
#else
  return value ? ::setenv("LOCALAPPDATA", value->c_str(), 1) == 0
               : ::unsetenv("LOCALAPPDATA") == 0;
#endif
}

class ScopedIsolatedLocalAppData final {
public:
  explicit ScopedIsolatedLocalAppData(std::filesystem::path profile)
      : profile_(std::move(profile)) {
    if (const char *current = std::getenv("LOCALAPPDATA")) {
      previous_ = current;
    }
    std::error_code error;
    std::filesystem::remove_all(profile_, error);
    error.clear();
    if (!std::filesystem::create_directories(profile_, error) || error) {
      return;
    }
    active_ = set_local_app_data(profile_.string());
  }

  ~ScopedIsolatedLocalAppData() {
    (void)set_local_app_data(previous_);
    std::error_code error;
    std::filesystem::remove_all(profile_, error);
  }

  ScopedIsolatedLocalAppData(const ScopedIsolatedLocalAppData &) = delete;
  ScopedIsolatedLocalAppData &
  operator=(const ScopedIsolatedLocalAppData &) = delete;

  [[nodiscard]] bool active() const noexcept { return active_; }
  [[nodiscard]] const std::filesystem::path &profile() const noexcept {
    return profile_;
  }

private:
  std::filesystem::path profile_;
  std::optional<std::string> previous_;
  bool active_ = false;
};

std::optional<std::string>
read_binary_file(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) {
    return std::nullopt;
  }
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

} // namespace

TEST(QtProjectWorkspaceTests,
     RefreshesOnlyGeneratedCurveIconForPaletteChanges) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader_tool_toolbar_palette_test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));

  QPalette palette = QApplication::palette();
  palette.setColor(QPalette::ButtonText, QColor(211, 37, 53));
  palette.setColor(QPalette::PlaceholderText, QColor(113, 23, 41));
  palette.setColor(QPalette::Highlight, QColor(241, 193, 17));
  QApplication::setPalette(palette);

  quader::QtToolToolbar toolbar;
  auto *curve =
      toolbar.findChild<QAction *>(QStringLiteral("tool_curve"));
  auto *select =
      toolbar.findChild<QAction *>(QStringLiteral("tool_select"));
  ASSERT_NE(curve, nullptr);
  ASSERT_NE(select, nullptr);
  const qint64 initial_curve_key = curve->icon().cacheKey();
  const qint64 initial_select_key = select->icon().cacheKey();

  palette.setColor(QPalette::ButtonText, QColor(29, 197, 83));
  palette.setColor(QPalette::PlaceholderText, QColor(17, 97, 47));
  QApplication::setPalette(palette);
  QCoreApplication::processEvents();
  const qint64 application_curve_key = curve->icon().cacheKey();
  EXPECT_NE(application_curve_key, initial_curve_key);
  EXPECT_EQ(select->icon().cacheKey(), initial_select_key)
      << "palette changes must not rebuild resource-backed tool icons";

  palette.setColor(QPalette::ButtonText, QColor(37, 89, 223));
  palette.setColor(QPalette::Highlight, QColor(173, 73, 229));
  toolbar.setPalette(palette);
  EXPECT_NE(curve->icon().cacheKey(), application_curve_key);
  EXPECT_EQ(select->icon().cacheKey(), initial_select_key);
}

TEST(QtProjectWorkspaceTests,
     EditorToolbarsKeepIdleFacesQuietAndInteractionStatesVisible) {
  int argc = 1;
  char application_name[] = "quader_toolbar_test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);

  quader::QtSelectionToolbar selection_toolbar;
  quader::QtToolToolbar tool_toolbar;
  const QPalette theme =
      quader::qt_obsidian_application_palette(application.palette());
  selection_toolbar.setPalette(theme);
  tool_toolbar.setPalette(theme);

  EXPECT_EQ(selection_toolbar.iconSize(), QSize(32, 32));
  EXPECT_EQ(tool_toolbar.iconSize(), QSize(32, 32));
  EXPECT_EQ(expect_persistent_button_faces(selection_toolbar, 30), 5);
  EXPECT_EQ(expect_persistent_button_faces(tool_toolbar, 40), 19);
  QToolButton *scale_metric_probe = nullptr;
  for (QToolButton *candidate :
       selection_toolbar.findChildren<QToolButton *>()) {
    if (candidate->defaultAction() != nullptr &&
        candidate->defaultAction()->isCheckable()) {
      scale_metric_probe = candidate;
      break;
    }
  }
  ASSERT_NE(scale_metric_probe, nullptr);
  const int baseline_button_margin = scale_metric_probe->style()->pixelMetric(
      QStyle::PM_ButtonMargin, nullptr, scale_metric_probe);

  selection_toolbar.apply_ui_scale(
      quader::QtUiScaleMetrics(2.0F, 1U));
  tool_toolbar.apply_ui_scale(quader::QtUiScaleMetrics(2.0F, 1U));
  EXPECT_EQ(selection_toolbar.iconSize(), QSize(64, 64));
  EXPECT_EQ(tool_toolbar.iconSize(), QSize(64, 64));
  EXPECT_EQ(expect_persistent_button_faces(selection_toolbar, 60), 5);
  EXPECT_EQ(expect_persistent_button_faces(tool_toolbar, 80), 19);
  EXPECT_EQ(scale_metric_probe->style()->pixelMetric(
                QStyle::PM_ButtonMargin, nullptr, scale_metric_probe),
            baseline_button_margin * 2);

  selection_toolbar.apply_ui_scale(
      quader::QtUiScaleMetrics(1.0F, 2U));
  tool_toolbar.apply_ui_scale(quader::QtUiScaleMetrics(1.0F, 2U));
  EXPECT_EQ(selection_toolbar.iconSize(), QSize(32, 32));
  EXPECT_EQ(tool_toolbar.iconSize(), QSize(32, 32));
  EXPECT_EQ(expect_persistent_button_faces(selection_toolbar, 30), 5);
  EXPECT_EQ(expect_persistent_button_faces(tool_toolbar, 40), 19);
  EXPECT_EQ(scale_metric_probe->style()->pixelMetric(
                QStyle::PM_ButtonMargin, nullptr, scale_metric_probe),
            baseline_button_margin);
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
  EXPECT_EQ(quader::qt_toolbar_button_color_group(default_state),
            QPalette::Active);
  EXPECT_EQ(quader::qt_toolbar_button_color_group(
                QStyle::State_Enabled | QStyle::State_Raised),
            QPalette::Inactive);
  EXPECT_EQ(quader::qt_toolbar_button_color_group(QStyle::State_Raised),
            QPalette::Disabled);
  const QImage default_face =
      render_toolbar_button_face(*button, default_state);
  const QImage hover_face = render_toolbar_button_face(
      *button, default_state | QStyle::State_MouseOver);
  const QImage selected_face = render_toolbar_button_face(
      *button, default_state | QStyle::State_On);
  const QImage selected_hover_face = render_toolbar_button_face(
      *button, default_state | QStyle::State_On | QStyle::State_MouseOver);
  const QImage focused_face = render_toolbar_button_face(
      *button, default_state | QStyle::State_HasFocus |
                   QStyle::State_KeyboardFocusChange);
  const QImage inactive_selected_face = render_toolbar_button_face(
      *button, QStyle::State_Enabled | QStyle::State_Raised |
                   QStyle::State_On);
  const QImage disabled_selected_face = render_toolbar_button_face(
      *button, QStyle::State_Raised | QStyle::State_On);
  const QImage pressed_face = render_toolbar_button_face(
      *button, QStyle::State_Active | QStyle::State_Enabled |
                   QStyle::State_Sunken);

  QImage transparent_face(default_face.size(), default_face.format());
  transparent_face.fill(Qt::transparent);
  EXPECT_EQ(default_face, transparent_face)
      << "idle toolbar actions leave the shared toolbar surface visible";
  const QPoint face_center = default_face.rect().center();
  for (const QImage *interactive_face :
       {&hover_face, &selected_face, &selected_hover_face, &focused_face,
        &inactive_selected_face, &disabled_selected_face, &pressed_face}) {
    EXPECT_EQ(interactive_face->pixelColor(face_center).alpha(), 255)
        << "interactive and selected states retain a visible native face";
  }
  EXPECT_NE(default_face, hover_face);
  EXPECT_NE(default_face, selected_face);
  EXPECT_NE(default_face, focused_face);
  EXPECT_NE(hover_face, selected_face);
  EXPECT_NE(selected_face, selected_hover_face);
  EXPECT_NE(selected_face, inactive_selected_face);
  EXPECT_NE(selected_face, disabled_selected_face);
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
  auto *transform_space = tool_toolbar.findChild<QAction *>(
      QStringLiteral("toggle_transform_space"));
  ASSERT_NE(transform_space, nullptr);
  EXPECT_EQ(transform_space->actionGroup(), nullptr);
  EXPECT_FALSE(transform_space->isChecked());
  EXPECT_TRUE(transform_space->toolTip().contains(QStringLiteral("[T]")));

  ASSERT_TRUE(editor.viewports()
                  .set_transform_space(quader::QuaderTransformSpace::Local)
                  .ok());
  tool_toolbar.synchronize(editor, selection);
  EXPECT_TRUE(transform_space->isChecked());

  int transform_space_toggle_requests = 0;
  tool_toolbar.set_transform_space_toggle_requested_callback(
      [&]() { ++transform_space_toggle_requests; });
  transform_space->trigger();
  EXPECT_EQ(transform_space_toggle_requests, 1);

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

  toolbar.apply_ui_scale(quader::QtUiScaleMetrics(2.0F, 1U));
  EXPECT_EQ(toolbar.iconSize(), QSize(64, 64));
  EXPECT_EQ(expect_persistent_button_faces(toolbar, 60), 4);
  EXPECT_EQ(grid_combo->minimumHeight(), 60);
  EXPECT_EQ(grid_combo->maximumHeight(), 60);
  EXPECT_EQ(angle_combo->minimumHeight(), 60);
  EXPECT_EQ(angle_combo->maximumHeight(), 60);
  toolbar.apply_ui_scale(quader::QtUiScaleMetrics(1.0F, 2U));
  EXPECT_EQ(toolbar.iconSize(), QSize(32, 32));
  EXPECT_EQ(expect_persistent_button_faces(toolbar, 30), 4);
  EXPECT_EQ(grid_combo->minimumHeight(), 30);
  EXPECT_EQ(grid_combo->maximumHeight(), 30);

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
  toolbar.apply_ui_scale(quader::QtUiScaleMetrics(2.0F, 1U));
  EXPECT_EQ(toolbar.iconSize(), QSize(64, 64));
  EXPECT_EQ(expect_persistent_button_faces(toolbar, 60), 4);
  toolbar.apply_ui_scale(quader::QtUiScaleMetrics(1.0F, 2U));
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
     TextureToolbarKeepsIndependentLocksAndUnchangedRefreshCheap) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader_texture_toolbar_test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  ASSERT_NE(QApplication::setStyle(QStringLiteral("Fusion")), nullptr);

  QMainWindow window;
  quader::QtViewportDisplayToolbar display_toolbar;
  quader::QtTextureToolbar toolbar;
  quader::LocalizationService localization;
  ASSERT_TRUE(localization.load(
      std::filesystem::path(QUADER_SOURCE_DIR) / "resources" / "locales",
      "en-US"));
  toolbar.set_localization(localization);
  window.addToolBar(Qt::TopToolBarArea, &display_toolbar);
  window.addToolBar(Qt::TopToolBarArea, &toolbar);
  window.resize(1000, 400);
  window.show();
  application.processEvents();
  EXPECT_EQ(toolbar.objectName(), QStringLiteral("textureToolbar"));
  EXPECT_EQ(toolbar.windowTitle(), QStringLiteral("Texturing"));
  EXPECT_TRUE(toolbar.isMovable());
  EXPECT_TRUE(toolbar.isFloatable());
  EXPECT_EQ(toolbar.iconSize(), QSize(32, 32));
  EXPECT_EQ(expect_persistent_button_faces(toolbar, 30), 2);
  toolbar.apply_ui_scale(quader::QtUiScaleMetrics(2.0F, 1U));
  EXPECT_EQ(toolbar.iconSize(), QSize(64, 64));
  EXPECT_EQ(expect_persistent_button_faces(toolbar, 60), 2);
  toolbar.apply_ui_scale(quader::QtUiScaleMetrics(1.0F, 2U));
  EXPECT_EQ(toolbar.iconSize(), QSize(32, 32));
  EXPECT_EQ(expect_persistent_button_faces(toolbar, 30), 2);
  EXPECT_GE(toolbar.geometry().left(), display_toolbar.geometry().right());
  const auto actions = toolbar.actions();
  ASSERT_EQ(actions.size(), 3);
  EXPECT_TRUE(actions[0]->isSeparator());
  EXPECT_EQ(actions[0]->objectName(), QStringLiteral("textureToolbarDivider"));
  EXPECT_EQ(actions[1]->objectName(), QStringLiteral("textureLock"));
  EXPECT_EQ(actions[2]->objectName(), QStringLiteral("textureLockScale"));
  EXPECT_EQ(actions[1]->text(), QStringLiteral("Texture Lock"));
  EXPECT_EQ(actions[2]->text(), QStringLiteral("Texture Lock Scale"));

  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto keybindings = std::filesystem::temp_directory_path() /
                           "quader-texture-toolbar-keybindings.json";
  std::filesystem::remove(keybindings);
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path(keybindings).ok());
  std::array<bool, 2> owner_state{true, false};
  const auto set_state = [&](std::size_t index, bool enabled) {
    const bool changed = owner_state[index] != enabled;
    owner_state[index] = enabled;
    return quader::QuaderApiVoidResult{
        .ok = true,
        .changed = changed,
        .mutation_kind = quader::QuaderMutationKind::Runtime,
    };
  };
  editor.preferences().set_runtime_hooks(
      quader::QuaderPreferencesRuntimeHooks{
          .texture_lock_enabled = [&] { return owner_state[0]; },
          .set_texture_lock_enabled = [&](bool value) { return set_state(0, value); },
          .texture_lock_scale_enabled = [&] { return owner_state[1]; },
          .set_texture_lock_scale_enabled = [&](bool value) { return set_state(1, value); },
      });
  toolbar.synchronize(editor, true);
  EXPECT_TRUE(actions[1]->isChecked());
  EXPECT_FALSE(actions[2]->isChecked());

  int presentation_changes = 0;
  for (QAction *action : actions) {
    QObject::connect(action, &QAction::changed, &toolbar,
                     [&] { ++presentation_changes; });
  }
  for (int frame = 0; frame < 100; ++frame) {
    toolbar.synchronize(editor);
  }
  EXPECT_EQ(presentation_changes, 0);

  const auto initial_binding_revision =
      editor.preferences().keybindings_revision();
  EXPECT_GT(initial_binding_revision, 0u);
  ASSERT_TRUE(editor.preferences()
                  .set_shortcut(
                      quader::quader_command::kViewportToggleTextureLock,
                      "Ctrl+Alt+F18")
                  .changed());
  EXPECT_GT(editor.preferences().keybindings_revision(),
            initial_binding_revision);
  EXPECT_EQ(editor.commands().keybindings_revision(),
            editor.preferences().keybindings_revision());
  toolbar.synchronize(editor);
  EXPECT_EQ(actions[1]->toolTip(),
            QStringLiteral("Texture Lock [Ctrl+Alt+F18]"));
  EXPECT_EQ(actions[2]->toolTip(), QStringLiteral("Texture Lock Scale"));

  const auto assigned_revision = editor.preferences().keybindings_revision();
  const int assigned_presentation_changes = presentation_changes;
  EXPECT_FALSE(editor.preferences()
                   .set_shortcut(
                       quader::quader_command::kViewportToggleTextureLock,
                       "Ctrl+Alt+F18")
                   .changed());
  toolbar.synchronize(editor);
  EXPECT_EQ(editor.preferences().keybindings_revision(), assigned_revision);
  EXPECT_EQ(presentation_changes, assigned_presentation_changes);
  ASSERT_TRUE(editor.commands()
                  .set_shortcut(
                      quader::quader_command::kViewportToggleTextureLockScale,
                      "Ctrl+Alt+F19")
                  .changed());
  toolbar.synchronize(editor);
  EXPECT_EQ(actions[2]->toolTip(),
            QStringLiteral("Texture Lock Scale [Ctrl+Alt+F19]"));

  ASSERT_TRUE(localization.select_locale("it-IT"));
  toolbar.set_localization(localization);
  toolbar.synchronize(editor);
  EXPECT_EQ(toolbar.windowTitle(), QStringLiteral("Texture"));
  EXPECT_EQ(actions[1]->text(), QStringLiteral("Blocco texture"));
  EXPECT_EQ(actions[1]->toolTip(),
            QStringLiteral("Blocco texture [Ctrl+Alt+F18]"));
  EXPECT_EQ(actions[2]->toolTip(),
            QStringLiteral("Blocco scala texture [Ctrl+Alt+F19]"));
  ASSERT_TRUE(editor.preferences()
                  .reset_shortcut(
                      quader::quader_command::kViewportToggleTextureLock)
                  .changed());
  toolbar.synchronize(editor);
  EXPECT_EQ(actions[1]->toolTip(), QStringLiteral("Blocco texture"));
  EXPECT_EQ(actions[2]->toolTip(),
            QStringLiteral("Blocco scala texture [Ctrl+Alt+F19]"));

  quader::SceneSession saved_session;
  quader::QuaderEditorApi saved_editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(saved_session));
  ASSERT_TRUE(saved_editor.commands()
                  .set_keybinding_storage_path(keybindings)
                  .ok());
  ASSERT_TRUE(saved_editor.preferences()
                  .set_shortcut(
                      quader::quader_command::kViewportToggleTextureLock,
                      "Ctrl+Alt+F20")
                  .changed());
  const auto before_reload_revision =
      editor.preferences().keybindings_revision();
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path(keybindings).ok());
  EXPECT_GT(editor.preferences().keybindings_revision(), before_reload_revision);
  toolbar.synchronize(editor);
  EXPECT_EQ(actions[1]->toolTip(),
            QStringLiteral("Blocco texture [Ctrl+Alt+F20]"));
  const int reloaded_presentation_changes = presentation_changes;
  const auto reloaded_revision = editor.preferences().keybindings_revision();
  for (int frame = 0; frame < 100; ++frame) {
    toolbar.synchronize(editor);
  }
  EXPECT_EQ(editor.preferences().keybindings_revision(), reloaded_revision);
  EXPECT_EQ(presentation_changes, reloaded_presentation_changes);

  std::vector<quader::QuaderCommandId> requested;
  toolbar.set_command_requested_callback([&](quader::QuaderCommandId command) {
    requested.push_back(command);
    EXPECT_TRUE(editor.commands().execute(command).changed());
  });
  actions[1]->trigger();
  toolbar.synchronize(editor);
  EXPECT_EQ(owner_state, (std::array<bool, 2>{false, false}));
  EXPECT_FALSE(actions[1]->isChecked());
  EXPECT_FALSE(actions[2]->isChecked());
  actions[2]->trigger();
  toolbar.synchronize(editor);
  EXPECT_EQ(owner_state, (std::array<bool, 2>{false, true}));
  EXPECT_FALSE(actions[1]->isChecked());
  EXPECT_TRUE(actions[2]->isChecked());
  ASSERT_EQ(requested.size(), 2u);
  EXPECT_EQ(requested[0].string(),
            quader::quader_command::kViewportToggleTextureLock.string());
  EXPECT_EQ(requested[1].string(),
            quader::quader_command::kViewportToggleTextureLockScale.string());

  const auto saved_layout = window.saveState();
  toolbar.hide();
  ASSERT_TRUE(window.restoreState(saved_layout));
  EXPECT_FALSE(toolbar.isHidden());
  toolbar.setEnabled(false);
  for (std::size_t index = 1; index < 3; ++index) {
    auto *button = qobject_cast<QToolButton *>(toolbar.widgetForAction(actions[index]));
    ASSERT_NE(button, nullptr);
    EXPECT_FALSE(button->isEnabled());
  }
  std::filesystem::remove(keybindings);
}

TEST(QtProjectWorkspaceTests,
     TextureToolbarRestoredWorkspaceAndMenuKeepBothButtonsVisible) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader_texture_toolbar_restore_test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  ASSERT_NE(QApplication::setStyle(QStringLiteral("Fusion")), nullptr);

  const auto populate_workspace = [](QMainWindow &window) {
    window.setAttribute(Qt::WA_DontShowOnScreen);
    auto *selection = new quader::QtSelectionToolbar(&window);
    auto *snapping = new quader::QtSnappingToolbar(&window);
    auto *display = new quader::QtViewportDisplayToolbar(&window);
    auto *tools = new quader::QtToolToolbar(&window);
    window.addToolBar(Qt::TopToolBarArea, selection);
    window.addToolBar(Qt::TopToolBarArea, snapping);
    window.addToolBar(Qt::TopToolBarArea, display);
    window.addToolBar(Qt::LeftToolBarArea, tools);
    auto *pages = new QStackedWidget(&window);
    pages->addWidget(new QWidget(pages));
    pages->addWidget(new QWidget(pages));
    window.setCentralWidget(pages);
    auto *closed_dock = new QDockWidget(QStringLiteral("Layers"), &window);
    closed_dock->setObjectName(QStringLiteral("dock_layers"));
    closed_dock->setWidget(new QWidget(closed_dock));
    window.addDockWidget(Qt::RightDockWidgetArea, closed_dock);
    closed_dock->hide();
    window.resize(1800, 700);
    return std::array<QToolBar *, 4>{selection, snapping, display, tools};
  };
  const auto toolbar_geometry = [](QToolBar &toolbar) {
    QString description =
        QStringLiteral("%1 rect=%2,%3 %4x%5 minimumHint=%6x%7 hint=%8x%9")
            .arg(toolbar.objectName())
            .arg(toolbar.x()).arg(toolbar.y())
            .arg(toolbar.width()).arg(toolbar.height())
            .arg(toolbar.minimumSizeHint().width())
            .arg(toolbar.minimumSizeHint().height())
            .arg(toolbar.sizeHint().width()).arg(toolbar.sizeHint().height());
    for (QAction *action : toolbar.actions()) {
      if (QWidget *widget = toolbar.widgetForAction(action)) {
        description += QStringLiteral("; [%1 visible=%2 rect=%3,%4 %5x%6]")
                           .arg(action->text()).arg(widget->isVisible())
                           .arg(widget->x()).arg(widget->y())
                           .arg(widget->width()).arg(widget->height());
      }
    }
    return description.toStdString();
  };

  QByteArray legacy_state;
  QByteArray collapsed_state;
  {
    QMainWindow previous_window;
    (void)populate_workspace(previous_window);
    previous_window.show();
    application.processEvents();
    legacy_state = previous_window.saveState(1);

    // Qt stores a user's dragged toolbar extent as positive preferredSize,
    // whereas automatic row stretching saves -1. Seed that persisted user
    // layout explicitly: QString name, shown byte, position, preferred extent.
    QByteArray display_record_name;
    QDataStream name_stream(&display_record_name, QIODevice::WriteOnly);
    name_stream.setVersion(QDataStream::Qt_5_0);
    name_stream << QStringLiteral("viewportDisplayToolbar");
    ASSERT_EQ(name_stream.status(), QDataStream::Ok);
    const qsizetype record_offset = legacy_state.indexOf(display_record_name);
    ASSERT_GE(record_offset, 0);
    ASSERT_EQ(legacy_state.lastIndexOf(display_record_name), record_offset);
    const qsizetype extent_offset =
        record_offset + display_record_name.size() +
        static_cast<qsizetype>(sizeof(quint8) + sizeof(qint32));
    ASSERT_LE(extent_offset + static_cast<qsizetype>(sizeof(qint32)),
              legacy_state.size());
    QBuffer state_buffer(&legacy_state);
    ASSERT_TRUE(state_buffer.open(QIODevice::ReadWrite));
    ASSERT_TRUE(state_buffer.seek(extent_offset));
    QDataStream state_stream(&state_buffer);
    state_stream.setVersion(QDataStream::Qt_5_0);
    qint32 initial_extent = 0;
    state_stream >> initial_extent;
    ASSERT_EQ(state_stream.status(), QDataStream::Ok);
    ASSERT_EQ(initial_extent, -1);
    ASSERT_TRUE(state_buffer.seek(extent_offset));
    state_stream << qint32{10000};
    ASSERT_EQ(state_stream.status(), QDataStream::Ok);
    state_buffer.close();

    // The saved display extent consumes the row above other toolbar minima.
    // Native QToolBar's first-item minimum hides both new lock buttons.
    auto *previous_texture = new QToolBar(&previous_window);
    previous_texture->setObjectName(QStringLiteral("textureToolbar"));
    previous_texture->setIconSize(QSize(32, 32));
    previous_texture->addSeparator();
    for (const QString &label : {QStringLiteral("Texture Lock"),
                                  QStringLiteral("Texture Lock Scale")}) {
      QAction *action = previous_texture->addAction(label);
      action->setCheckable(true);
      auto *button = qobject_cast<QToolButton *>(
          previous_texture->widgetForAction(action));
      ASSERT_NE(button, nullptr);
      button->setMinimumWidth(40);
      button->setFixedHeight(30);
    }
    previous_window.addToolBar(Qt::TopToolBarArea, previous_texture);
    ASSERT_TRUE(previous_window.restoreState(legacy_state, 1));
    application.processEvents();
    ASSERT_TRUE(previous_texture->isVisible());
    ASSERT_FALSE(previous_texture->widgetForAction(
        previous_texture->actions()[1])->isVisible())
        << toolbar_geometry(*previous_texture);
    ASSERT_FALSE(previous_texture->widgetForAction(
        previous_texture->actions()[2])->isVisible())
        << toolbar_geometry(*previous_texture);
    collapsed_state = previous_window.saveState(1);
  }

  for (const QByteArray &saved_state : {legacy_state, collapsed_state}) {
    SCOPED_TRACE(saved_state == legacy_state ? "legacy layout"
                                             : "previously collapsed layout");
    QMainWindow window;
    const auto existing_toolbars = populate_workspace(window);
    quader::QtTextureToolbar texture(&window);
    window.addToolBar(Qt::TopToolBarArea, &texture);
    quader::SceneSession session;
    quader::QuaderEditorApi editor(
        quader::QuaderApiSessionInternalAccess::from_scene_session(session));
    auto settings = quader::ViewportSettings::defaults();
    quader::LocalizationService localization;
    quader::QtMainMenu menu;
    menu.initialize(*window.menuBar(), quader::QtMainMenuContext{
        .editor = editor,
        .modeling = session.modeling(),
        .viewport_settings = settings,
        .localization = localization,
        .texture_toolbar_visible = [&] { return texture.isVisible(); },
        .set_texture_toolbar_visible =
            [&](bool visible) { texture.setVisible(visible); },
    });
    auto *toggle = window.menuBar()->findChild<QAction *>(
        QStringLiteral("toolbar_texture"));
    ASSERT_NE(toggle, nullptr);
    ASSERT_TRUE(window.restoreState(saved_state, 1));

    // Match UISystem: restore the layout before Home, remember workspace
    // visibility, hide its chrome, then show the retained workspace chrome.
    const bool texture_visible_in_workspace = !texture.isHidden();
    std::array<bool, 4> existing_visibility{};
    for (std::size_t index = 0; index < existing_toolbars.size(); ++index) {
      existing_visibility[index] = !existing_toolbars[index]->isHidden();
      existing_toolbars[index]->hide();
    }
    texture.hide();
    window.menuBar()->hide();
    window.show();
    application.processEvents();
    auto *pages = qobject_cast<QStackedWidget *>(window.centralWidget());
    ASSERT_NE(pages, nullptr);
    pages->setCurrentIndex(1);
    window.menuBar()->show();
    for (std::size_t index = 0; index < existing_toolbars.size(); ++index) {
      existing_toolbars[index]->setVisible(existing_visibility[index]);
    }
    texture.setVisible(texture_visible_in_workspace);
    application.processEvents();
    menu.refresh();

    const auto expect_usable_group = [&] {
      SCOPED_TRACE(toolbar_geometry(texture));
      EXPECT_TRUE(toggle->isChecked());
      EXPECT_TRUE(texture.isVisible());
      const auto actions = texture.actions();
      ASSERT_EQ(actions.size(), 3);
      for (QAction *action : actions) {
        QWidget *widget = texture.widgetForAction(action);
        ASSERT_NE(widget, nullptr);
        EXPECT_TRUE(widget->isVisible()) << action->objectName().toStdString();
        EXPECT_FALSE(widget->geometry().isEmpty());
        EXPECT_TRUE(texture.rect().contains(widget->geometry()));
        const QRect window_bounds(widget->mapTo(&window, QPoint()),
                                   widget->size());
        EXPECT_TRUE(window.rect().contains(window_bounds));
      }
      auto *closed_dock = window.findChild<QDockWidget *>(
          QStringLiteral("dock_layers"));
      ASSERT_NE(closed_dock, nullptr);
      EXPECT_TRUE(closed_dock->isHidden());
    };
    expect_usable_group();
    toggle->trigger();
    application.processEvents();
    EXPECT_TRUE(texture.isHidden());
    const QByteArray user_hidden_state = window.saveState(1);
    toggle->trigger();
    application.processEvents();
    expect_usable_group();

    ASSERT_TRUE(window.restoreState(user_hidden_state, 1));
    application.processEvents();
    menu.refresh();
    EXPECT_FALSE(toggle->isChecked());
    EXPECT_TRUE(texture.isHidden());
    toggle->trigger();
    application.processEvents();
    expect_usable_group();

    window.addToolBar(Qt::LeftToolBarArea, &texture);
    application.processEvents();
    EXPECT_EQ(texture.orientation(), Qt::Vertical);
    expect_usable_group();
  }
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
  EXPECT_EQ(left_rail->backgroundRole(), QPalette::AlternateBase);
  QPalette updated_palette = application.palette();
  updated_palette.setColor(QPalette::AlternateBase, QColor(57, 43, 68));
  QApplication::setPalette(updated_palette);
  QCoreApplication::processEvents();
  EXPECT_EQ(left_rail->palette().color(left_rail->backgroundRole()),
            QColor(57, 43, 68));
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
  auto *brand_name = home.findChild<QLabel *>(
      QStringLiteral("projectHome.brand.name"));
  ASSERT_NE(brand_name, nullptr);

  home.apply_ui_scale(quader::QtUiScaleMetrics(2.0F, 1U));
  EXPECT_EQ(left_rail->minimumWidth(), 496);
  EXPECT_EQ(left_rail->maximumWidth(), 496);
  EXPECT_EQ(left_rail->layout()->contentsMargins(), QMargins(56, 64, 56, 56));
  EXPECT_EQ(brand_icon->minimumSize(), QSize(88, 88));
  EXPECT_EQ(brand_icon->maximumSize(), QSize(88, 88));
  EXPECT_DOUBLE_EQ(brand_name->font().pointSizeF(), 30.0);
  EXPECT_EQ(recent_view->verticalHeader()->defaultSectionSize(), 96);
  EXPECT_EQ(recent_view->columnWidth(
                quader::QtRecentProjectsModel::LastModifiedColumn),
            368);
  EXPECT_EQ(recent_view->columnWidth(
                quader::QtRecentProjectsModel::SizeColumn),
            224);
  EXPECT_EQ(recent_view->columnWidth(
                quader::QtRecentProjectsModel::ActionsColumn),
            144);

  home.apply_ui_scale(quader::QtUiScaleMetrics(1.0F, 2U));
  EXPECT_EQ(left_rail->minimumWidth(), 248);
  EXPECT_EQ(left_rail->maximumWidth(), 248);
  EXPECT_EQ(left_rail->layout()->contentsMargins(), QMargins(28, 32, 28, 28));
  EXPECT_EQ(brand_icon->minimumSize(), QSize(44, 44));
  EXPECT_EQ(brand_icon->maximumSize(), QSize(44, 44));
  EXPECT_DOUBLE_EQ(brand_name->font().pointSizeF(), 15.0);
  EXPECT_EQ(recent_view->verticalHeader()->defaultSectionSize(), 48);
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
  workspace.apply_ui_scale(quader::QtUiScaleMetrics(2.0F, 1U));
  EXPECT_EQ(breadcrumb_bar->layout()->contentsMargins(),
            QMargins(12, 4, 12, 4));
  EXPECT_EQ(breadcrumb_bar->layout()->spacing(), 8);
  workspace.apply_ui_scale(quader::QtUiScaleMetrics(1.0F, 2U));
  EXPECT_EQ(breadcrumb_bar->layout()->contentsMargins(),
            QMargins(6, 2, 6, 2));
  EXPECT_EQ(breadcrumb_bar->layout()->spacing(), 4);
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

TEST(QtProjectWorkspaceTests,
     ProjectCloseDropsExpiredViewportInteractionOwnersBeforeFallbackRebind) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-project-close-rebind-test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  const char *local_app_data = std::getenv("LOCALAPPDATA");
  ASSERT_NE(local_app_data, nullptr);
  const std::filesystem::path project_root =
      std::filesystem::path(local_app_data) /
      "ui-project-close-runtime-rebind";
  std::error_code cleanup_error;
  std::filesystem::remove_all(project_root, cleanup_error);
  cleanup_error.clear();
  ASSERT_TRUE(std::filesystem::create_directory(project_root, cleanup_error));
  ASSERT_FALSE(cleanup_error);
  struct ScopedProjectRoot final {
    std::filesystem::path path;
    ~ScopedProjectRoot() {
      std::error_code error;
      std::filesystem::remove_all(path, error);
    }
  } scoped_project_root{project_root};

  const std::filesystem::path resource_root =
      std::filesystem::path(QUADER_SOURCE_DIR) / "resources";
  quader::RenderSystem render_system;
  quader::ProjectSession project;
  project.set_resource_root(resource_root / "modeling_assets");
  quader::QuaderEditorApi editor(project);
  quader::UISystem ui;
  ASSERT_TRUE(ui.initialize(render_system, resource_root));
  ASSERT_NE(ui.main_window(), nullptr);
  ui.main_window()->setAttribute(Qt::WA_DontShowOnScreen, true);

  quader::SceneSession *bound_scene = nullptr;
  int scene_bind_count = 0;
  bool project_scene_was_bound = false;
  bool fallback_bound_after_project_release = false;
  const auto bind_scene = [&](quader::SceneSession &scene) {
    ++scene_bind_count;
    bound_scene = &scene;
    project_scene_was_bound = project_scene_was_bound || project.has_project();
    fallback_bound_after_project_release =
        fallback_bound_after_project_release ||
        (project_scene_was_bound && !project.has_project() &&
         project.active_scene() == nullptr &&
         bound_scene == &project.fallback_scene());
    ui.install_runtime_hooks(
        editor, scene.scene(), scene.assets(), scene.modeling(),
        scene.selection(), scene.native_tools(), scene.point_placement(),
        quader::kInvalidAssetId);
  };
  quader::QuaderEditorApiSessionAccess::set_scene_binding_changed_callback(
      editor, bind_scene);
  bind_scene(project.fallback_scene());
  ui.show();
  ASSERT_TRUE(ui.main_window()->isVisible());

  const quader::QuaderApiVoidResult created =
      editor.project().create(project_root);
  ASSERT_TRUE(created.ok()) << created.error_message();
  ASSERT_TRUE(editor.project().has_project());
  ASSERT_NE(project.active_scene(), nullptr);
  ASSERT_EQ(bound_scene, project.active_scene()->session.get());

  QWidget *viewport_widget = ui.main_window()->findChild<QWidget *>(
      QStringLiteral("viewportWorkspace"));
  ASSERT_NE(viewport_widget, nullptr);
  auto *viewport_panel =
      dynamic_cast<quader::QtViewportPanel *>(viewport_widget);
  ASSERT_NE(viewport_panel, nullptr);
  viewport_panel->process_input(
      editor, bound_scene->scene(), bound_scene->assets(),
      bound_scene->modeling(), quader::kInvalidAssetId,
      bound_scene->native_tools(), &bound_scene->point_placement(), 1.0 / 60.0);

  quader::SceneSession *const outgoing_scene = bound_scene;
  EXPECT_TRUE(ui.main_window()->close());
  EXPECT_FALSE(ui.main_window()->isVisible());
  EXPECT_FALSE(editor.project().has_project());
  EXPECT_EQ(project.active_scene(), nullptr);
  EXPECT_EQ(bound_scene, &project.fallback_scene());
  EXPECT_NE(bound_scene, outgoing_scene);
  EXPECT_EQ(scene_bind_count, 3);
  EXPECT_TRUE(fallback_bound_after_project_release);
  // Consume the rebound owners once: merely storing the fallback pointers does
  // not prove that the panel dropped every non-owning pointer to the released
  // project scene.
  viewport_panel->process_input(
      editor, bound_scene->scene(), bound_scene->assets(),
      bound_scene->modeling(), quader::kInvalidAssetId,
      bound_scene->native_tools(), &bound_scene->point_placement(), 1.0 / 60.0);

  ui.shutdown();
}

TEST(QtProjectWorkspaceTests,
     SameProjectReopenResetsMaterialFoldersAndRejectsDragBeforeNextFrame) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  const std::filesystem::path isolated_profile =
      std::filesystem::temp_directory_path() /
      ("quader-ui-material-folder-lifecycle-" +
       std::to_string(std::chrono::steady_clock::now()
                          .time_since_epoch()
                          .count()));
  ScopedIsolatedLocalAppData scoped_profile(isolated_profile);
  ASSERT_TRUE(scoped_profile.active());

  int argc = 1;
  char application_name[] = "quader-material-folder-lifecycle-test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  const std::filesystem::path resource_root =
      std::filesystem::path(QUADER_SOURCE_DIR) / "resources";
  const std::filesystem::path project_root =
      scoped_profile.profile() / "Material Folder Project";
  ASSERT_TRUE(std::filesystem::create_directory(project_root));
  quader::RenderSystem render_system;
  quader::ProjectSession project;
  project.set_resource_root(resource_root / "modeling_assets");
  quader::QuaderEditorApi editor(project);
  quader::UISystem ui;
  ASSERT_TRUE(ui.initialize(render_system, resource_root));
  ASSERT_NE(ui.main_window(), nullptr);
  ui.main_window()->setAttribute(Qt::WA_DontShowOnScreen, true);
  const auto bind_scene = [&](quader::SceneSession &scene) {
    ui.install_runtime_hooks(
        editor, scene.scene(), scene.assets(), scene.modeling(),
        scene.selection(), scene.native_tools(), scene.point_placement(),
        quader::kInvalidAssetId);
  };
  quader::QuaderEditorApiSessionAccess::set_scene_binding_changed_callback(
      editor, bind_scene);
  bind_scene(project.fallback_scene());
  ui.show();

  const auto created = editor.project().create(project_root);
  ASSERT_TRUE(created.ok()) << created.error_message();
  const auto first_scene = editor.scene_documents().active_scene_id();
  ASSERT_TRUE(first_scene.has_value());
  const auto first_path =
      quader::ProjectRelativePath::parse_portable("Scenes/One.qdr");
  ASSERT_TRUE(first_path.ok());
  ASSERT_TRUE(editor.scene_documents().save_as(*first_scene, first_path.value()).ok());
  const auto second_scene = editor.scene_documents().new_scene();
  ASSERT_TRUE(second_scene.ok()) << second_scene.error_message();
  const auto second_path =
      quader::ProjectRelativePath::parse_portable("Scenes/Two.qdr");
  ASSERT_TRUE(second_path.ok());
  ASSERT_TRUE(editor.scene_documents().save_as(second_scene.value(), second_path.value()).ok());
  ASSERT_TRUE(editor.project_assets().create_material_folder("Materials", "Dev").ok());
  ASSERT_TRUE(editor.project_assets().create_material_folder("Materials/Dev", "Wood").ok());
  ASSERT_TRUE(editor.project_assets().create_material_folder("Materials", "Other").ok());
  const auto imported = editor.project_assets().import_textures_as_materials(
      std::array{resource_root / "modeling_assets/materials/default/default_albedo.png"},
      "Materials/Dev/Wood");
  ASSERT_TRUE(imported.ok()) << imported.error_message();
  ASSERT_TRUE(imported.value().failed.empty());
  ASSERT_EQ(imported.value().imported.size(), 1u);
  const auto material = imported.value().imported.front().material;
  ASSERT_FALSE(editor.scene_documents().has_dirty_scenes());
  ui.refresh({}, {});

  auto *browser = dynamic_cast<quader::QtMaterialBrowserWindow *>(
      ui.main_window()->findChild<QWidget *>(QStringLiteral("materialBrowser.window")));
  ASSERT_NE(browser, nullptr);
  browser->setAttribute(Qt::WA_DontShowOnScreen, true);
  // This fixture has no renderer, so the native-workspace menu stays disabled.
  // Present the retained consumer directly and let UISystem supply its state.
  browser->show();
  ui.refresh_project_workspace();
  application.processEvents();
  ASSERT_TRUE(browser->isVisible());
  auto *tree = browser->findChild<QTreeWidget *>(QStringLiteral("materialBrowser.folderTree"));
  auto *grid = browser->findChild<QListView *>(QStringLiteral("materialBrowser.gridView"));
  ASSERT_NE(tree, nullptr);
  ASSERT_NE(grid, nullptr);
  const auto find_folder = [&](const QString &path) -> QTreeWidgetItem * {
    for (QTreeWidgetItemIterator it(tree); *it != nullptr; ++it) {
      if ((*it)->data(0, Qt::UserRole).toString() == path) {
        return *it;
      }
    }
    return nullptr;
  };
  auto *dev = find_folder(QStringLiteral("Materials/Dev"));
  ASSERT_NE(dev, nullptr);
  dev->setExpanded(true);
  browser->set_selected_folder(QStringLiteral("Materials/Dev/Wood"));
  ASSERT_EQ(grid->model()->rowCount(), 1);

  const auto activated = editor.scene_documents().activate(*first_scene);
  ASSERT_TRUE(activated.ok()) << activated.error_message();
  ui.refresh({}, {});
  EXPECT_EQ(browser->selected_folder(), QStringLiteral("Materials/Dev/Wood"));
  dev = find_folder(QStringLiteral("Materials/Dev"));
  ASSERT_NE(dev, nullptr);
  EXPECT_TRUE(dev->isExpanded())
      << "a scene-tab rebind must retain this project's folder presentation";
  ASSERT_EQ(grid->model()->rowCount(), 1);
  std::unique_ptr<QMimeData> old_drag(
      grid->model()->mimeData({grid->model()->index(0, 0)}));
  ASSERT_NE(old_drag, nullptr);
  application.processEvents();
  auto *destination = find_folder(QStringLiteral("Materials/Other"));
  ASSERT_NE(destination, nullptr);
  ASSERT_FALSE(tree->visualItemRect(destination).isEmpty());
  const auto catalog_before = editor.project_assets().materials();
  ASSERT_TRUE(catalog_before.ok());
  const std::filesystem::path manifest = editor.project().manifest_path();
  const std::filesystem::path material_path = project_root / material.project_relative_path;
  const std::filesystem::path moved_path =
      project_root / "Materials/Other" / material_path.filename();
  ASSERT_TRUE(std::filesystem::exists(material_path));
  ASSERT_FALSE(std::filesystem::exists(moved_path));

  const auto reopened = editor.project().open(manifest);
  ASSERT_TRUE(reopened.ok()) << reopened.error_message();
  ASSERT_TRUE(reopened.changed());
  const auto drop_without_frame = [&](const QMimeData &mime) {
    QTreeWidgetItem *target = find_folder(QStringLiteral("Materials/Other"));
    EXPECT_NE(target, nullptr);
    if (target == nullptr) {
      return false;
    }
    const QPoint position = tree->visualItemRect(target).center();
    QDragEnterEvent enter(position, Qt::MoveAction, &mime,
                          Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(tree->viewport(), &enter);
    QDropEvent drop(QPointF(position), Qt::MoveAction, &mime,
                    Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(tree->viewport(), &drop);
    return drop.isAccepted();
  };
  // No event processing or UI refresh may close the lifecycle-event race here.
  // The semantic owner must reject a still-present old window's drag payload.
  (void)drop_without_frame(*old_drag);
  EXPECT_TRUE(std::filesystem::exists(material_path));
  EXPECT_FALSE(std::filesystem::exists(moved_path));
  const auto catalog_after = editor.project_assets().materials();
  ASSERT_TRUE(catalog_after.ok());
  ASSERT_EQ(catalog_after.value().size(), catalog_before.value().size());
  for (std::size_t index = 0; index < catalog_before.value().size(); ++index) {
    EXPECT_EQ(catalog_after.value()[index].id, catalog_before.value()[index].id);
    EXPECT_EQ(catalog_after.value()[index].project_relative_path,
              catalog_before.value()[index].project_relative_path);
  }

  ui.refresh({}, {});
  EXPECT_EQ(browser->selected_folder(), QStringLiteral("Materials"));
  dev = find_folder(QStringLiteral("Materials/Dev"));
  ASSERT_NE(dev, nullptr);
  EXPECT_FALSE(dev->isExpanded());
  EXPECT_FALSE(drop_without_frame(*old_drag))
      << "the refreshed view must also reject the previous project session's MIME";

  // A new drag of the same material proves the destination and owner routing
  // work; stale identity, rather than an unrelated move restriction, blocked it.
  browser->set_selected_folder(QStringLiteral("Materials/Dev/Wood"));
  ASSERT_EQ(grid->model()->rowCount(), 1);
  std::unique_ptr<QMimeData> current_drag(
      grid->model()->mimeData({grid->model()->index(0, 0)}));
  ASSERT_NE(current_drag, nullptr);
  EXPECT_TRUE(drop_without_frame(*current_drag));
  EXPECT_FALSE(std::filesystem::exists(material_path));
  EXPECT_TRUE(std::filesystem::exists(moved_path));
  ui.shutdown();
}

TEST(QtProjectWorkspaceTests,
     CommandErrorsRemainVisibleAndAnnounceOncePerFailureUntilSuccess) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  const std::filesystem::path isolated_profile =
      std::filesystem::temp_directory_path() /
      ("quader-ui-system-status-errors-" +
       std::to_string(std::chrono::steady_clock::now()
                          .time_since_epoch()
                          .count()));
  ScopedIsolatedLocalAppData scoped_profile(isolated_profile);
  ASSERT_TRUE(scoped_profile.active());

  int argc = 1;
  char application_name[] = "quader-status-error-ui-system-test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  const std::filesystem::path resource_root =
      std::filesystem::path(QUADER_SOURCE_DIR) / "resources";
  const std::filesystem::path project_root =
      scoped_profile.profile() / "Status Error Project";
  ASSERT_TRUE(std::filesystem::create_directory(project_root));
  quader::RenderSystem render_system;
  quader::ProjectSession project;
  project.set_resource_root(resource_root / "modeling_assets");
  quader::QuaderEditorApi editor(project);
  quader::UISystem ui;
  ASSERT_TRUE(ui.initialize(render_system, resource_root));
  ASSERT_NE(ui.main_window(), nullptr);
  ui.main_window()->setAttribute(Qt::WA_DontShowOnScreen, true);
  const auto created_project = editor.project().create(project_root);
  ASSERT_TRUE(created_project.ok()) << created_project.error_message();
  ASSERT_NE(project.active_scene(), nullptr);
  quader::SceneSession &scene = *project.active_scene()->session;
  ui.install_runtime_hooks(
      editor, scene.scene(), scene.assets(), scene.modeling(),
      scene.selection(), scene.native_tools(), scene.point_placement(),
      quader::kInvalidAssetId);

  auto *message = dynamic_cast<quader::QtStatusMessage *>(
      ui.main_window()->findChild<QLabel *>(QStringLiteral("statusMessage")));
  auto *hints = dynamic_cast<quader::QtOperationInputHints *>(
      ui.main_window()->findChild<QWidget *>(
          QStringLiteral("operationInputHints")));
  ASSERT_NE(message, nullptr);
  ASSERT_NE(hints, nullptr);
  int announcements = 0;
  message->set_error_sound_callback([&]() { ++announcements; });
  const QColor ordinary_background = message->palette().color(QPalette::Window);
  const QColor ordinary_text = message->palette().color(QPalette::WindowText);

  // Both faces are valid, but dissolving their shared boundary would enclose
  // the rectangular opening and require a polygon with two boundary loops.
  quader_poly::Document document;
  const std::array vertices{
      quader_poly::add_vertex(document, {0.0f, 0.0f, 0.0f}),
      quader_poly::add_vertex(document, {3.0f, 0.0f, 0.0f}),
      quader_poly::add_vertex(document, {3.0f, 2.0f, 0.0f}),
      quader_poly::add_vertex(document, {2.0f, 2.0f, 0.0f}),
      quader_poly::add_vertex(document, {2.0f, 3.0f, 0.0f}),
      quader_poly::add_vertex(document, {3.0f, 3.0f, 0.0f}),
      quader_poly::add_vertex(document, {3.0f, 8.0f, 0.0f}),
      quader_poly::add_vertex(document, {0.0f, 8.0f, 0.0f}),
      quader_poly::add_vertex(document, {6.0f, 0.0f, 0.0f}),
      quader_poly::add_vertex(document, {6.0f, 8.0f, 0.0f}),
  };
  const auto notched_face = quader_poly::add_face(
      document, std::array{vertices[0], vertices[1], vertices[2], vertices[3],
                           vertices[4], vertices[5], vertices[6], vertices[7]});
  const auto neighboring_face = quader_poly::add_face(
      document, std::array{vertices[1], vertices[8], vertices[9], vertices[6],
                           vertices[5], vertices[2]});
  ASSERT_NE(notched_face, quader_poly::kInvalidElementId);
  ASSERT_NE(neighboring_face, quader_poly::kInvalidElementId);
  const auto mesh = editor.modeling().create_mesh(
      quader::QuaderPolygonDocumentInternalAccess::from_backend(
          std::move(document)),
      {.name = "Faces surrounding an opening"});
  ASSERT_TRUE(mesh.ok()) << mesh.error_message();
  ASSERT_TRUE(editor.selection()
                  .select_faces(
                      mesh.value(),
                      {static_cast<quader::QuaderElementId>(notched_face),
                       static_cast<quader::QuaderElementId>(neighboring_face)})
                  .ok());
  ASSERT_EQ(editor.selection().summary().selected_face_count, 2u);

  const auto combine = editor.commands().descriptor(
      quader::quader_command::kModelingCombineFaces);
  ASSERT_TRUE(combine.has_value());
  ASSERT_TRUE(combine->enabled);
  const auto invalid_geometry = editor.commands().execute(
      quader::quader_command::kModelingCombineFaces);
  ASSERT_FALSE(invalid_geometry.ok());
  EXPECT_EQ(invalid_geometry.error_message(),
            "Combine Faces would create invalid face geometry.");
  EXPECT_FALSE(invalid_geometry.changed());
  EXPECT_EQ(editor.selection().summary().selected_face_count, 2u);
  ui.refresh({}, {});
  EXPECT_TRUE(message->is_error());
  EXPECT_FALSE(message->isHidden());
  EXPECT_EQ(message->text(),
            QString::fromStdString(invalid_geometry.error_message()));
  EXPECT_EQ(message->palette().color(QPalette::Window), QColor("#B3261E"));
  EXPECT_EQ(message->palette().color(QPalette::WindowText), QColor(Qt::white));
  EXPECT_EQ(announcements, 1);
  ui.refresh({}, {});
  EXPECT_EQ(announcements, 1);
  ASSERT_TRUE(editor.commands()
                  .execute(quader::quader_command::kViewportToggleGrid)
                  .ok());
  ui.refresh({}, {});
  EXPECT_FALSE(message->is_error());
  announcements = 0;

  ASSERT_TRUE(editor.tools()
                  .activate(quader::ViewportToolId::CreatePrimitive)
                  .ok());
  ui.refresh({}, {});
  ASSERT_FALSE(hints->hints().empty());
  EXPECT_FALSE(hints->isHidden());
  EXPECT_EQ(announcements, 0);

  const auto failed = editor.commands().execute(
      quader::quader_command::kModelingCombineFaces);
  ASSERT_FALSE(failed.ok());
  EXPECT_EQ(failed.error_message(), "Command is disabled.");
  EXPECT_FALSE(failed.changed());
  EXPECT_EQ(editor.selection().summary().selected_face_count, 2u);
  ui.refresh({}, {});
  EXPECT_TRUE(message->is_error());
  EXPECT_FALSE(message->isHidden());
  EXPECT_TRUE(message->autoFillBackground());
  EXPECT_EQ(message->palette().color(QPalette::Window), QColor("#B3261E"));
  EXPECT_EQ(message->palette().color(QPalette::WindowText), QColor(Qt::white));
  EXPECT_EQ(message->text(), QString::fromStdString(failed.error_message()));
  EXPECT_TRUE(hints->isHidden());
  EXPECT_EQ(announcements, 1);

  const auto hint_revision = hints->projection_revision();
  for (int frame = 0; frame < 100; ++frame) {
    ui.refresh({}, {});
  }
  EXPECT_EQ(announcements, 1);
  EXPECT_EQ(hints->projection_revision(), hint_revision);
  EXPECT_FALSE(message->isHidden());

  ASSERT_TRUE(editor.preferences().set_locale("it-IT").ok());
  ui.refresh({}, {});
  EXPECT_TRUE(message->is_error());
  EXPECT_FALSE(message->isHidden());
  EXPECT_TRUE(hints->isHidden());
  EXPECT_EQ(announcements, 1)
      << "localization must not announce an already presented error";

  const auto repeated = editor.commands().execute(
      quader::quader_command::kModelingCombineFaces);
  ASSERT_FALSE(repeated.ok());
  EXPECT_EQ(repeated.error_message(), failed.error_message());
  ui.refresh({}, {});
  EXPECT_EQ(announcements, 2)
      << "a new attempt must announce even when its error text is unchanged";

  ASSERT_TRUE(editor.commands()
                  .execute(quader::quader_command::kViewportToggleGrid)
                  .ok());
  ui.refresh({}, {});
  EXPECT_FALSE(message->is_error());
  EXPECT_FALSE(message->autoFillBackground());
  EXPECT_EQ(message->palette().color(QPalette::Window), ordinary_background);
  EXPECT_EQ(message->palette().color(QPalette::WindowText), ordinary_text);
  EXPECT_FALSE(hints->hints().empty());
  EXPECT_FALSE(hints->isHidden());
  EXPECT_TRUE(message->isHidden());
  EXPECT_EQ(announcements, 2);

  const quader::QuaderNotification notification{
      .level = quader::QuaderNotificationLevel::Error,
      .message = "Unable to finish the requested operation.",
  };
  ASSERT_TRUE(editor.ui().notify(notification).ok());
  EXPECT_TRUE(message->is_error());
  EXPECT_FALSE(message->isHidden());
  EXPECT_EQ(message->text(), QString::fromStdString(notification.message));
  EXPECT_EQ(message->palette().color(QPalette::Window), QColor("#B3261E"));
  EXPECT_EQ(message->palette().color(QPalette::WindowText), QColor(Qt::white));
  EXPECT_TRUE(hints->isHidden());
  EXPECT_EQ(announcements, 3);

  ui.refresh({}, {});
  ASSERT_TRUE(editor.preferences().set_locale("en-US").ok());
  ui.refresh({}, {});
  EXPECT_TRUE(message->is_error());
  EXPECT_FALSE(message->isHidden());
  EXPECT_EQ(message->text(), QString::fromStdString(notification.message));
  EXPECT_EQ(announcements, 3)
      << "refresh and localization must retain a direct error silently";

  ASSERT_TRUE(editor.ui().notify(notification).ok());
  ui.refresh({}, {});
  EXPECT_EQ(announcements, 4);
  ASSERT_TRUE(editor.ui()
                  .notify({.level = quader::QuaderNotificationLevel::Warning,
                           .message = "Some optional items were skipped."})
                  .ok());
  ui.refresh({}, {});
  EXPECT_FALSE(message->is_error());
  EXPECT_FALSE(message->autoFillBackground());
  EXPECT_EQ(message->palette().color(QPalette::Window), ordinary_background);
  EXPECT_EQ(message->palette().color(QPalette::WindowText), ordinary_text);
  EXPECT_EQ(announcements, 4);

  ASSERT_TRUE(editor.ui().notify(notification).ok());
  EXPECT_EQ(announcements, 5);
  ASSERT_TRUE(editor.commands()
                  .execute(quader::quader_command::kViewportToggleGrid)
                  .ok());
  ui.refresh({}, {});
  EXPECT_FALSE(message->is_error());
  EXPECT_FALSE(message->autoFillBackground());
  EXPECT_FALSE(hints->isHidden());
  EXPECT_EQ(announcements, 5)
      << "a subsequent successful command dismisses a direct error silently";
  ASSERT_TRUE(editor.ui().notify(notification).ok());
  EXPECT_EQ(announcements, 6);
  const auto command_revision = editor.commands().last_status_revision();
  auto *angle = ui.main_window()->findChild<QComboBox *>(
      QStringLiteral("snappingAngleCombo"));
  ASSERT_NE(angle, nullptr);
  ASSERT_GT(angle->count(), 1);
  const int next_angle = (angle->currentIndex() + 1) % angle->count();
  angle->setCurrentIndex(next_angle);
  ASSERT_TRUE(QMetaObject::invokeMethod(angle, "activated", Qt::DirectConnection,
                                       Q_ARG(int, next_angle)));
  EXPECT_EQ(editor.commands().last_status_revision(), command_revision);
  ui.refresh({}, {});
  EXPECT_FALSE(message->is_error())
      << "a successful direct edit with an empty result must clear the error";
  EXPECT_TRUE(message->text().isEmpty());
  EXPECT_FALSE(hints->isHidden());
  EXPECT_EQ(announcements, 6);
  ui.shutdown();
}

TEST(QtProjectWorkspaceTests,
     StatusSelectionCountsEveryModeAndLocaleThroughRetainedOwnerRevisions) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  const std::filesystem::path isolated_profile =
      std::filesystem::temp_directory_path() /
      ("quader-ui-system-selection-count-" +
       std::to_string(std::chrono::steady_clock::now()
                          .time_since_epoch().count()));
  ScopedIsolatedLocalAppData scoped_profile(isolated_profile);
  ASSERT_TRUE(scoped_profile.active());
  int argc = 1;
  char application_name[] = "quader-status-selection-ui-test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  const std::filesystem::path resource_root =
      std::filesystem::path(QUADER_SOURCE_DIR) / "resources";
  quader::RenderSystem render_system;
  quader::ProjectSession project;
  project.set_resource_root(resource_root / "modeling_assets");
  quader::QuaderEditorApi editor(project);
  quader::UISystem ui;
  ASSERT_TRUE(ui.initialize(render_system, resource_root));
  ui.main_window()->setAttribute(Qt::WA_DontShowOnScreen, true);
  const std::filesystem::path project_root =
      scoped_profile.profile() / "Selection Counter Project";
  ASSERT_TRUE(std::filesystem::create_directory(project_root));
  const auto created_project = editor.project().create(project_root);
  ASSERT_TRUE(created_project.ok()) << created_project.error_message();
  ASSERT_NE(project.active_scene(), nullptr);
  quader::SceneSession &scene = *project.active_scene()->session;
  const auto bind = [&]() {
    ui.install_runtime_hooks(
        editor, scene.scene(), scene.assets(), scene.modeling(),
        scene.selection(), scene.native_tools(), scene.point_placement(),
        quader::kInvalidAssetId);
  };
  bind();
  auto *counter = ui.main_window()->findChild<QLabel *>(
      QStringLiteral("statusSelection"));
  auto *separator = ui.main_window()->findChild<QFrame *>(
      QStringLiteral("statusSelectionSeparator"));
  auto *version = ui.main_window()->findChild<QLabel *>(
      QStringLiteral("statusVersion"));
  auto *vertex_mode_action = ui.main_window()->findChild<QAction *>(
      QStringLiteral("selectionModeVertex"));
  auto *face_mode_action = ui.main_window()->findChild<QAction *>(
      QStringLiteral("selectionModeFace"));
  auto *mesh_mode_action = ui.main_window()->findChild<QAction *>(
      QStringLiteral("selectionModeMesh"));
  ASSERT_NE(counter, nullptr);
  ASSERT_NE(separator, nullptr);
  ASSERT_NE(version, nullptr);
  ASSERT_NE(vertex_mode_action, nullptr);
  ASSERT_NE(face_mode_action, nullptr);
  ASSERT_NE(mesh_mode_action, nullptr);
  EXPECT_EQ(separator->frameShape(), QFrame::VLine);

  quader_poly::Document document;
  const std::array vertices{
      quader_poly::add_vertex(document, {0.0F, 0.0F, 0.0F}),
      quader_poly::add_vertex(document, {1.0F, 0.0F, 0.0F}),
      quader_poly::add_vertex(document, {1.0F, 1.0F, 0.0F}),
      quader_poly::add_vertex(document, {0.0F, 1.0F, 0.0F}),
  };
  const std::array faces{
      quader_poly::add_face(document,
                           std::array{vertices[0], vertices[1], vertices[2]}),
      quader_poly::add_face(document,
                           std::array{vertices[0], vertices[2], vertices[3]}),
  };
  const auto mesh = editor.modeling().create_mesh(
      quader::QuaderPolygonDocumentInternalAccess::from_backend(
          std::move(document)), {.name = "Selection Counter Mesh"});
  const auto light = editor.scene().create_point_light();
  const auto curve = editor.modeling().create_curve({
      .points = {{.position = {0.0F, 0.0F, 0.0F}},
                 {.position = {2.0F, 0.0F, 0.0F}}},
      .name = "Selection Counter Curve",
  });
  const auto first_group = editor.scene().create_entity(
      {.name = "First Group", .kind = quader::QuaderEntityKind::Empty});
  const auto second_group = editor.scene().create_entity(
      {.name = "Second Group", .kind = quader::QuaderEntityKind::Empty});
  ASSERT_TRUE(mesh.ok()) << mesh.error_message();
  ASSERT_TRUE(light.ok()) << light.error_message();
  ASSERT_TRUE(curve.ok()) << curve.error_message();
  ASSERT_TRUE(first_group.ok()) << first_group.error_message();
  ASSERT_TRUE(second_group.ok()) << second_group.error_message();
  ASSERT_TRUE(editor.scene()
                  .reparent(mesh.value().entity, first_group.value().id)
                  .ok());

  const std::array modes{
      quader::QuaderSelectionMode::Vertex, quader::QuaderSelectionMode::Edge,
      quader::QuaderSelectionMode::Face, quader::QuaderSelectionMode::Object,
      quader::QuaderSelectionMode::Group};
  const std::array english_one{"Vertex Selected", "Edge Selected",
                               "Face Selected", "Mesh Selected",
                               "Group Selected"};
  const std::array english_other{"Vertices Selected", "Edges Selected",
                                 "Faces Selected", "Meshes Selected",
                                 "Groups Selected"};
  const std::array italian_one{"Vertice selezionato", "Spigolo selezionato",
                               "Faccia selezionata", "Mesh selezionata",
                               "Gruppo selezionato"};
  const std::array italian_other{"Vertici selezionati", "Spigoli selezionati",
                                 "Facce selezionate", "Mesh selezionate",
                                 "Gruppi selezionati"};
  for (const bool italian : {false, true}) {
    ASSERT_TRUE(editor.preferences().set_locale(italian ? "it-IT" : "en-US").ok());
    for (std::size_t mode_index = 0; mode_index < modes.size(); ++mode_index) {
      const auto mode = modes[mode_index];
      for (int count = 0; count <= 2; ++count) {
        ASSERT_TRUE(editor.selection().clear().ok());
        ASSERT_TRUE(editor.selection().set_mode(mode).ok());
        for (int index = 0; index < count; ++index) {
          quader::QuaderApiVoidResult selected;
          switch (mode) {
          case quader::QuaderSelectionMode::Vertex:
            selected = editor.selection().select_vertex(
                mesh.value(), vertices[index], quader::QuaderSelectionEdit::Add);
            break;
          case quader::QuaderSelectionMode::Edge:
            selected = editor.selection().select_edge(
                mesh.value(), vertices[index], vertices[index + 1],
                quader::QuaderSelectionEdit::Add);
            break;
          case quader::QuaderSelectionMode::Face:
            selected = editor.selection().select_face(
                mesh.value(), faces[index], quader::QuaderSelectionEdit::Add);
            break;
          case quader::QuaderSelectionMode::Object:
            selected = editor.selection().select_object(
                index == 0 ? mesh.value().entity : light.value().id,
                quader::QuaderSelectionEdit::Add);
            break;
          case quader::QuaderSelectionMode::Group:
            selected = editor.selection().select_object(
                index == 0 ? first_group.value().id
                           : second_group.value().id,
                quader::QuaderSelectionEdit::Add);
            break;
          }
          ASSERT_TRUE(selected.ok()) << selected.error_message();
        }
        ui.refresh({}, {});
        const auto &words = italian ? (count == 1 ? italian_one : italian_other)
                                    : (count == 1 ? english_one : english_other);
        const QString expected = QStringLiteral("%1 %2")
                                     .arg(count).arg(words[mode_index]);
        EXPECT_EQ(counter->text(), expected)
            << "mode " << mode_index << ", count " << count;
        EXPECT_EQ(counter->accessibleName(), expected);
      }
    }
  }

  ASSERT_TRUE(editor.preferences().set_locale("en-US").ok());
  ASSERT_TRUE(editor.selection().select_object(light.value().id).ok());
  for (const auto mode : {quader::QuaderSelectionMode::Face,
                         quader::QuaderSelectionMode::Vertex}) {
    ASSERT_TRUE(editor.selection().set_mode(mode).ok());
    // A scene-only selection is summarized as Object even while the active
    // component mode remains unchanged. The counter follows the active mode.
    ASSERT_EQ(editor.selection().summary().mode,
              quader::QuaderSelectionMode::Object);
    ASSERT_EQ(scene.modeling().selection_mode(),
              mode == quader::QuaderSelectionMode::Face
                  ? quader::ModelingSelectionMode::Face
                  : quader::ModelingSelectionMode::Vertex);
    ui.refresh({}, {});
    EXPECT_EQ(face_mode_action->isChecked(),
              mode == quader::QuaderSelectionMode::Face);
    EXPECT_EQ(vertex_mode_action->isChecked(),
              mode == quader::QuaderSelectionMode::Vertex);
    EXPECT_FALSE(mesh_mode_action->isChecked());
    EXPECT_EQ(counter->text(),
              mode == quader::QuaderSelectionMode::Face
                  ? QStringLiteral("0 Faces Selected")
                  : QStringLiteral("0 Vertices Selected"));
  }
  ASSERT_TRUE(editor.selection().clear().ok());
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Object).ok());
  ASSERT_TRUE(editor.selection().select_objects(
      {mesh.value().entity, light.value().id, curve.value().entity}).ok());
  ui.refresh({}, {});
  EXPECT_EQ(counter->text(), QStringLiteral("3 Meshes Selected"));
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Group).ok());
  ASSERT_TRUE(editor.selection().select_objects(
      {first_group.value().id, first_group.value().id,
       mesh.value().entity, light.value().id, curve.value().entity}).ok());
  ui.refresh({}, {});
  EXPECT_EQ(counter->text(), QStringLiteral("1 Group Selected"));

  // A retained value is untouched on unchanged frames; rebinding invalidates
  // the gate even when both runtime owners and their revisions are identical.
  counter->setText(QStringLiteral("retained presentation probe"));
  for (int frame = 0; frame < 50; ++frame) {
    ui.refresh({}, {});
  }
  EXPECT_EQ(counter->text(), QStringLiteral("retained presentation probe"));
  bind();
  ui.refresh({}, {});
  EXPECT_EQ(counter->text(), QStringLiteral("1 Group Selected"));

  ASSERT_TRUE(editor.selection().select_object(
      second_group.value().id, quader::QuaderSelectionEdit::Add).ok());
  ui.refresh({}, {});
  EXPECT_EQ(counter->text(), QStringLiteral("2 Groups Selected"));
  ASSERT_TRUE(editor.history().undo().ok());
  ui.refresh({}, {});
  EXPECT_EQ(counter->text(), QStringLiteral("1 Group Selected"));
  ASSERT_TRUE(editor.history().redo().ok());
  ui.refresh({}, {});
  EXPECT_EQ(counter->text(), QStringLiteral("2 Groups Selected"));
  ui.shutdown();
}

TEST(QtProjectWorkspaceTests,
     InterfaceScaleAndVSyncFlowThroughTheRealUiSystemAndPublicPreferencesHooks) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  const std::filesystem::path isolated_profile =
      std::filesystem::temp_directory_path() /
      ("quader-ui-system-interface-scale-" +
       std::to_string(std::chrono::steady_clock::now()
                          .time_since_epoch()
                          .count()));
  ScopedIsolatedLocalAppData scoped_profile(isolated_profile);
  ASSERT_TRUE(scoped_profile.active());

  int argc = 1;
  char application_name[] = "quader-interface-scale-ui-system-test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  const std::filesystem::path resource_root =
      std::filesystem::path(QUADER_SOURCE_DIR) / "resources";
  const std::filesystem::path settings_path =
      scoped_profile.profile() / "Quader" / "ui_settings.ini";
  quader::RenderSystem render_system;
  quader::ProjectSession project;
  project.set_resource_root(resource_root / "modeling_assets");
  quader::QuaderEditorApi editor(project);
  quader::UISystem ui;
  ASSERT_TRUE(ui.initialize(render_system, resource_root));
  ASSERT_NE(ui.main_window(), nullptr);
  ui.main_window()->setAttribute(Qt::WA_DontShowOnScreen, true);

  quader::SceneSession &scene = project.fallback_scene();
  ui.install_runtime_hooks(
      editor, scene.scene(), scene.assets(), scene.modeling(),
      scene.selection(), scene.native_tools(), scene.point_placement(),
      quader::kInvalidAssetId);

  auto *scale_style =
      dynamic_cast<quader::QtUiScaleProxyStyle *>(QApplication::style());
  ASSERT_NE(scale_style, nullptr);
  EXPECT_FLOAT_EQ(scale_style->metrics().factor(), 1.0F);

  QStatusBar *status_bar = ui.main_window()->statusBar();
  const auto expect_status_padding = [&](int padding) {
    const QMargins margins = status_bar->contentsMargins();
    EXPECT_EQ(margins.top() + margins.bottom(), padding);
    const int padded_height = status_bar->sizeHint().height();
    status_bar->setContentsMargins(0, 0, 0, 0);
    const int unpadded_height = status_bar->sizeHint().height();
    status_bar->setContentsMargins(margins);
    EXPECT_EQ(padded_height, unpadded_height + padding);
  };
  expect_status_padding(10);

  QAction *settings_action = ui.main_window()->findChild<QAction *>(
      QStringLiteral("action_settings"));
  QWidget *settings_dialog = ui.main_window()->findChild<QWidget *>(
      QStringLiteral("settingsDialog"));
  ASSERT_NE(settings_action, nullptr);
  ASSERT_NE(settings_dialog, nullptr);
  settings_dialog->setAttribute(Qt::WA_DontShowOnScreen, true);
  settings_action->trigger();
  QCoreApplication::processEvents();
  ASSERT_TRUE(settings_dialog->isVisible());

  auto *scale = settings_dialog->findChild<QSpinBox *>(
      QStringLiteral("settingsInterfaceScale"));
  auto *apply = settings_dialog->findChild<QPushButton *>(
      QStringLiteral("settingsApply"));
  ASSERT_NE(scale, nullptr);
  ASSERT_NE(apply, nullptr);
  EXPECT_EQ(scale->value(), 100);
  EXPECT_FALSE(apply->isEnabled());

  scale->setValue(150);
  QCoreApplication::processEvents();
  EXPECT_FLOAT_EQ(scale_style->metrics().factor(), 1.5F);
  expect_status_padding(15);
  EXPECT_DOUBLE_EQ(editor.preferences().interface_scale(), 1.0);
  EXPECT_TRUE(apply->isEnabled());

  const quader::QuaderApiVoidResult committed =
      editor.preferences().set_interface_scale(2.0);
  ASSERT_TRUE(committed.ok()) << committed.error_message();
  EXPECT_TRUE(committed.changed());
  QCoreApplication::processEvents();
  EXPECT_FLOAT_EQ(scale_style->metrics().factor(), 2.0F);
  expect_status_padding(20);
  const QSize committed_status_size = status_bar->sizeHint();
  EXPECT_DOUBLE_EQ(editor.preferences().interface_scale(), 2.0);
  EXPECT_EQ(scale->value(), 200);
  EXPECT_FALSE(apply->isEnabled());
  ASSERT_TRUE(std::filesystem::exists(settings_path));
  EXPECT_FLOAT_EQ(quader::UISettings::load(settings_path).interface_scale(),
                  2.0F);

  const std::optional<std::string> committed_contents =
      read_binary_file(settings_path);
  ASSERT_TRUE(committed_contents.has_value());
  std::error_code timestamp_error;
  const auto committed_write_time =
      std::filesystem::last_write_time(settings_path, timestamp_error);
  ASSERT_FALSE(timestamp_error);
  std::filesystem::last_write_time(
      settings_path, committed_write_time - std::chrono::hours(24),
      timestamp_error);
  ASSERT_FALSE(timestamp_error);
  const auto no_op_write_time =
      std::filesystem::last_write_time(settings_path, timestamp_error);
  ASSERT_FALSE(timestamp_error);

  const quader::QuaderApiVoidResult no_op =
      editor.preferences().set_interface_scale(2.0);
  ASSERT_TRUE(no_op.ok()) << no_op.error_message();
  EXPECT_FALSE(no_op.changed());
  EXPECT_EQ(read_binary_file(settings_path), committed_contents);
  EXPECT_EQ(std::filesystem::last_write_time(settings_path, timestamp_error),
            no_op_write_time);
  EXPECT_FALSE(timestamp_error);
  EXPECT_FLOAT_EQ(scale_style->metrics().factor(), 2.0F);
  expect_status_padding(20);
  EXPECT_EQ(status_bar->sizeHint(), committed_status_size);
  EXPECT_EQ(scale->value(), 200);
  EXPECT_FALSE(apply->isEnabled());

  auto *vsync = settings_dialog->findChild<QCheckBox *>(
      QStringLiteral("settingsVSync"));
  ASSERT_NE(vsync, nullptr);
  EXPECT_TRUE(vsync->isChecked());
  EXPECT_TRUE(editor.preferences().vsync_enabled());
  EXPECT_TRUE(render_system.vsync_enabled());
  vsync->setChecked(false);
  EXPECT_FALSE(render_system.vsync_enabled());
  EXPECT_TRUE(editor.preferences().vsync_enabled());
  EXPECT_TRUE(apply->isEnabled());

  // Reapplying the persisted value clears a different live Settings preview.
  const quader::QuaderApiVoidResult restore_vsync =
      editor.preferences().set_vsync_enabled(true);
  ASSERT_TRUE(restore_vsync.ok()) << restore_vsync.error_message();
  EXPECT_TRUE(restore_vsync.changed());
  EXPECT_TRUE(vsync->isChecked());
  EXPECT_TRUE(render_system.vsync_enabled());
  EXPECT_FALSE(apply->isEnabled());

  const quader::QuaderApiVoidResult disable_vsync =
      editor.preferences().set_vsync_enabled(false);
  ASSERT_TRUE(disable_vsync.ok()) << disable_vsync.error_message();
  EXPECT_TRUE(disable_vsync.changed());
  EXPECT_FALSE(editor.preferences().vsync_enabled());
  EXPECT_FALSE(render_system.vsync_enabled());
  EXPECT_FALSE(vsync->isChecked());
  EXPECT_FALSE(quader::UISettings::load(settings_path).vsync_enabled());
  EXPECT_FALSE(apply->isEnabled());

  std::filesystem::last_write_time(settings_path, no_op_write_time,
                                   timestamp_error);
  ASSERT_FALSE(timestamp_error);
  const std::optional<std::string> disabled_contents =
      read_binary_file(settings_path);
  const quader::QuaderApiVoidResult vsync_no_op =
      editor.preferences().set_vsync_enabled(false);
  EXPECT_TRUE(vsync_no_op.ok());
  EXPECT_FALSE(vsync_no_op.changed());
  EXPECT_EQ(read_binary_file(settings_path), disabled_contents);
  EXPECT_EQ(std::filesystem::last_write_time(settings_path, timestamp_error),
            no_op_write_time);
  EXPECT_FALSE(timestamp_error);

  vsync->setChecked(true);
  EXPECT_TRUE(render_system.vsync_enabled());
  EXPECT_FALSE(editor.preferences().vsync_enabled());
  const std::filesystem::path backup_settings =
      settings_path.parent_path() / "ui_settings_backup.ini";
  std::filesystem::rename(settings_path, backup_settings);
  ASSERT_TRUE(std::filesystem::create_directory(settings_path));
  const quader::QuaderApiVoidResult failed_vsync =
      editor.preferences().set_vsync_enabled(false);
  EXPECT_FALSE(failed_vsync.ok());
  EXPECT_FALSE(failed_vsync.changed());
  EXPECT_FALSE(failed_vsync.error_message().empty());
  EXPECT_FALSE(editor.preferences().vsync_enabled());
  EXPECT_TRUE(render_system.vsync_enabled())
      << "failed persistence restores the prior live preview";
  EXPECT_TRUE(vsync->isChecked());
  EXPECT_TRUE(apply->isEnabled());
  ASSERT_TRUE(std::filesystem::remove(settings_path));
  std::filesystem::rename(backup_settings, settings_path);

  settings_dialog->close();
  QCoreApplication::processEvents();
  EXPECT_FALSE(render_system.vsync_enabled());
  ui.shutdown();
}

TEST(QtProjectWorkspaceTests,
     LegacyMaterialDockLayoutMigratesOnceAndPreservesSurroundingDocks) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  const auto isolated_profile = std::filesystem::temp_directory_path() /
      ("quader-face-texture-layout-migration-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  ScopedIsolatedLocalAppData scoped_profile(isolated_profile);
  ASSERT_TRUE(scoped_profile.active());
  int argc = 1;
  char application_name[] = "quader-face-texture-layout-migration-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  const auto paths = quader::UISettings::resolve_paths(isolated_profile);
  ASSERT_TRUE(std::filesystem::create_directories(paths.directory));
  {
    QMainWindow legacy;
    legacy.setAttribute(Qt::WA_DontShowOnScreen, true);
    legacy.resize(1100, 800);
    auto *outliner = new QDockWidget(&legacy);
    outliner->setObjectName(QStringLiteral("dock_outliner"));
    outliner->setWidget(new QWidget(outliner));
    auto *inspector = new QDockWidget(&legacy);
    inspector->setObjectName(QStringLiteral("dock_inspector"));
    inspector->setWidget(new QWidget(inspector));
    auto *materials = new QDockWidget(&legacy);
    materials->setObjectName(QStringLiteral("dock_active_material"));
    materials->setWidget(new QWidget(materials));
    legacy.addDockWidget(Qt::RightDockWidgetArea, outliner);
    legacy.addDockWidget(Qt::LeftDockWidgetArea, inspector);
    legacy.addDockWidget(Qt::LeftDockWidgetArea, materials);
    legacy.tabifyDockWidget(inspector, materials);
    legacy.show();
    QCoreApplication::processEvents();
    ASSERT_TRUE(legacy.tabifiedDockWidgets(inspector).contains(materials));
    materials->hide();
    const QByteArray state = legacy.saveState(1);
    std::ofstream persisted(paths.qt_window_state_file, std::ios::binary);
    ASSERT_TRUE(persisted.is_open());
    persisted.write(state.constData(), static_cast<std::streamsize>(state.size()));
    ASSERT_TRUE(persisted.good());
  }
  {
    std::ofstream settings(paths.settings_file);
    ASSERT_TRUE(settings.is_open());
    settings << "[Panels]\nactive_material=false\noutliner=true\ninspector=true\n";
  }

  quader::RenderSystem render_system;
  const auto resource_root = std::filesystem::path(QUADER_SOURCE_DIR) / "resources";
  for (int load = 0; load != 2; ++load) {
    SCOPED_TRACE(load);
    quader::UISystem ui;
    ASSERT_TRUE(ui.initialize(render_system, resource_root));
    auto *window = ui.main_window();
    ASSERT_NE(window, nullptr);
    window->setAttribute(Qt::WA_DontShowOnScreen, true);
    auto *face_texture = window->findChild<QDockWidget *>(
        QStringLiteral("dock_face_texture"));
    auto *inspector = window->findChild<QDockWidget *>(
        QStringLiteral("dock_inspector"));
    auto *outliner = window->findChild<QDockWidget *>(
        QStringLiteral("dock_outliner"));
    ASSERT_NE(face_texture, nullptr);
    ASSERT_NE(inspector, nullptr);
    ASSERT_NE(outliner, nullptr);
    EXPECT_EQ(window->findChild<QDockWidget *>(
                  QStringLiteral("dock_active_material")), nullptr);
    EXPECT_EQ(window->dockWidgetArea(face_texture), Qt::LeftDockWidgetArea);
    EXPECT_EQ(window->dockWidgetArea(inspector), Qt::LeftDockWidgetArea);
    EXPECT_EQ(window->dockWidgetArea(outliner), Qt::RightDockWidgetArea);
    EXPECT_TRUE(face_texture->isHidden());
    // Home hides every dock. Qt reports tab peers only while they participate
    // in the visible dock layout, so inspect their restored workspace positions.
    window->show();
    inspector->show();
    face_texture->show();
    QCoreApplication::processEvents();
    EXPECT_TRUE(window->tabifiedDockWidgets(inspector).contains(face_texture));
    face_texture->hide();
    inspector->hide();
    EXPECT_EQ(face_texture->features(),
              QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable |
                  QDockWidget::DockWidgetFloatable);
    ui.shutdown();
    EXPECT_TRUE(quader::QtAuxiliaryWindowState::load(
                    paths.qt_auxiliary_window_state_file).face_texture_layout_migrated);
    EXPECT_FALSE(quader::UISettings::load(paths.settings_file)
                     .is_panel_visible(quader::UIPanelId::FaceTexture));
  }
}

TEST(QtProjectWorkspaceTests,
     EmbeddedMaterialUsageRefreshesOnRevealAndAlreadyDirtyAssignmentAndUndo) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  const auto isolated_profile = std::filesystem::temp_directory_path() /
      ("quader-embedded-usage-refresh-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  ScopedIsolatedLocalAppData scoped_profile(isolated_profile);
  ASSERT_TRUE(scoped_profile.active());
  int argc = 1;
  char application_name[] = "quader-embedded-usage-refresh-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  const auto paths = quader::UISettings::resolve_paths(isolated_profile);
  ASSERT_TRUE(std::filesystem::create_directories(paths.directory));
  auto settings = quader::UISettings::defaults();
  settings.set_panel_visible(quader::UIPanelId::FaceTexture, false);
  ASSERT_TRUE(settings.save(paths.settings_file));
  const auto resource_root = std::filesystem::path(QUADER_SOURCE_DIR) / "resources";
  const auto project_root = isolated_profile / "Usage Project";
  ASSERT_TRUE(std::filesystem::create_directory(project_root));
  const std::array texture_paths{isolated_profile / "First.png",
                                  isolated_profile / "Second.png"};
  QImage texture(2, 2, QImage::Format_RGBA8888);
  texture.fill(Qt::red);
  ASSERT_TRUE(texture.save(QString::fromStdWString(texture_paths[0].wstring())));
  texture.fill(Qt::green);
  ASSERT_TRUE(texture.save(QString::fromStdWString(texture_paths[1].wstring())));

  quader::RenderSystem render_system;
  quader::ProjectSession project;
  project.set_resource_root(resource_root / "modeling_assets");
  quader::QuaderEditorApi editor(project);
  const auto created = editor.project().create(project_root);
  ASSERT_TRUE(created.ok()) << created.error_message();
  ASSERT_TRUE(editor.project_assets()
                  .create_material_folder("Materials", "Usage").ok());
  const auto imported = editor.project_assets().import_textures_as_materials(
      texture_paths, "Materials/Usage");
  ASSERT_TRUE(imported.ok()) << imported.error_message();
  ASSERT_TRUE(imported.value().failed.empty());
  ASSERT_EQ(imported.value().imported.size(), 2u);
  const auto first = quader::QuaderMaterialReference::project(
      imported.value().imported[0].material.id);
  const auto second = quader::QuaderMaterialReference::project(
      imported.value().imported[1].material.id);
  const auto mesh = editor.modeling().create_box({
      .bounds = {.min = {0.0f, 0.0f, 0.0f}, .max = {16.0f, 16.0f, 16.0f}},
      .name = "Usage box",
      .material = first,
  });
  ASSERT_TRUE(mesh.ok()) << mesh.error_message();
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection()
                  .select_faces(mesh.value(), {1, 2, 3, 4, 5, 6}).ok());
  ASSERT_TRUE(editor.scene_documents().has_dirty_scenes());
  ASSERT_NE(project.active_scene(), nullptr);

  quader::UISystem ui;
  ASSERT_TRUE(ui.initialize(render_system, resource_root));
  ASSERT_NE(ui.main_window(), nullptr);
  ui.main_window()->setAttribute(Qt::WA_DontShowOnScreen, true);
  quader::SceneSession &scene = *project.active_scene()->session;
  ui.install_runtime_hooks(
      editor, scene.scene(), scene.assets(), scene.modeling(), scene.selection(),
      scene.native_tools(), scene.point_placement(), quader::kInvalidAssetId);
  auto *dock = ui.main_window()->findChild<QDockWidget *>(
      QStringLiteral("dock_face_texture"));
  auto *browser = dynamic_cast<quader::QtMaterialBrowserWindow *>(
      ui.main_window()->findChild<QWidget *>(
          QStringLiteral("materialBrowser.embedded")));
  auto *windowed = ui.main_window()->findChild<QWidget *>(
      QStringLiteral("materialBrowser.window"));
  ASSERT_NE(dock, nullptr);
  ASSERT_NE(browser, nullptr);
  ASSERT_NE(windowed, nullptr);
  EXPECT_TRUE(dock->isHidden());
  EXPECT_TRUE(windowed->isHidden());
  ui.show();
  application.processEvents();
  ui.refresh({}, {});
  ui.refresh({}, {});
  EXPECT_TRUE(dock->isHidden());
  EXPECT_FALSE(windowed->isVisible());

  // The embedded browser must initialize from an ordinary frame even though
  // its project/catalog events were consumed while the dock was hidden.
  ASSERT_TRUE(editor.ui().show_panel("face_texture").ok());
  application.processEvents();
  ui.refresh({}, {});
  ASSERT_TRUE(dock->isVisible());
  auto *grid = browser->findChild<QListView *>(
      QStringLiteral("materialBrowser.gridView"));
  auto *filter = browser->findChild<QToolButton *>(
      QStringLiteral("materialBrowser.filter"));
  auto *usage_status = browser->findChild<QLabel *>(
      QStringLiteral("materialBrowser.usageStatus"));
  ASSERT_NE(grid, nullptr);
  ASSERT_NE(filter, nullptr);
  ASSERT_NE(usage_status, nullptr);
  auto *proxy = dynamic_cast<quader::QtMaterialBrowserProxyModel *>(grid->model());
  ASSERT_NE(proxy, nullptr);
  ASSERT_TRUE(proxy->usage_complete());
  EXPECT_TRUE(usage_status->isHidden());
  browser->set_selected_folder(QStringLiteral("Materials/Usage"));
  ASSERT_EQ(grid->model()->rowCount(), 2);
  const auto choose_usage = [&](bool used) {
    filter->click();
    auto *popup = browser->findChild<QFrame *>(
        QStringLiteral("materialBrowser.filterPopup"));
    ASSERT_NE(popup, nullptr);
    auto *choice = popup->findChild<QCheckBox *>(
        used ? QStringLiteral("materialBrowser.filterUsed")
             : QStringLiteral("materialBrowser.filterNotUsed"));
    ASSERT_NE(choice, nullptr);
    ASSERT_TRUE(choice->isEnabled());
    choice->click();
    popup->close();
    application.processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  };
  const auto expect_only_material = [&](quader::QuaderMaterialReference material) {
    ASSERT_EQ(grid->model()->rowCount(), 1);
    EXPECT_EQ(grid->model()->index(0, 0)
                  .data(quader::QtMaterialBrowserModel::StableIdRole).toString(),
              quader::qt_material_reference_stable_id(material));
  };
  choose_usage(true);
  expect_only_material(first);
  choose_usage(false);
  expect_only_material(second);

  // Use the modeling API directly: browser click handlers explicitly request
  // a workspace refresh and would conceal the already-dirty refresh defect.
  ui.refresh({}, {});
  const auto catalog_before = editor.project_assets().materials();
  ASSERT_TRUE(catalog_before.ok());
  const auto work_before_assignment = project.material_usage().work_stats();
  int catalog_resets = 0;
  QObject::connect(proxy->sourceModel(), &QAbstractItemModel::modelReset,
                   browser, [&]() { ++catalog_resets; });
  const auto token = editor.modeling().face_texture_settings().selection_token;
  const auto assigned = editor.modeling().apply_material_to_selected_faces(
      second, mesh.value().scene_session_id, token);
  ASSERT_TRUE(assigned.ok()) << assigned.error_message();
  ASSERT_TRUE(assigned.changed());
  ASSERT_TRUE(editor.scene_documents().has_dirty_scenes());
  ui.refresh({}, {});
  EXPECT_TRUE(proxy->usage_complete());
  expect_only_material(first);
  EXPECT_GT(project.material_usage().work_stats().snapshot_build_count,
            work_before_assignment.snapshot_build_count);
  choose_usage(true);
  expect_only_material(second);
  EXPECT_FALSE(windowed->isVisible());

  const auto undone = editor.history().undo();
  ASSERT_TRUE(undone.ok()) << undone.error_message();
  ASSERT_TRUE(undone.changed());
  ASSERT_TRUE(editor.scene_documents().has_dirty_scenes());
  ui.refresh({}, {});
  expect_only_material(first);
  choose_usage(false);
  expect_only_material(second);
  ASSERT_TRUE(editor.history().can_redo());
  const auto work_before_idle = project.material_usage().work_stats();
  const auto history_before_idle = editor.history().snapshot();
  const auto revisions_before_idle = editor.revisions();
  for (int frame = 0; frame != 12; ++frame) {
    ui.refresh({}, {});
  }
  EXPECT_EQ(project.material_usage().work_stats(), work_before_idle);
  EXPECT_EQ(catalog_resets, 0);
  const auto history_after_idle = editor.history().snapshot();
  EXPECT_EQ(history_after_idle.can_undo, history_before_idle.can_undo);
  EXPECT_EQ(history_after_idle.can_redo, history_before_idle.can_redo);
  EXPECT_EQ(history_after_idle.undo_label, history_before_idle.undo_label);
  EXPECT_EQ(history_after_idle.redo_label, history_before_idle.redo_label);
  const auto revisions_after_idle = editor.revisions();
  EXPECT_EQ(revisions_after_idle.scene_revision,
            revisions_before_idle.scene_revision);
  EXPECT_EQ(revisions_after_idle.asset_revision,
            revisions_before_idle.asset_revision);
  EXPECT_EQ(revisions_after_idle.modeling_content_revision,
            revisions_before_idle.modeling_content_revision);
  for (quader::QuaderElementId face = 1; face != 7; ++face) {
    const auto assigned_material =
        editor.modeling().project_material_for_face(
            mesh.value().modeling_object_id, face);
    ASSERT_TRUE(assigned_material.ok()) << assigned_material.error_message();
    EXPECT_EQ(assigned_material.value().id, first.project_asset);
  }
  const auto catalog_after = editor.project_assets().materials();
  ASSERT_TRUE(catalog_after.ok());
  ASSERT_EQ(catalog_after.value().size(), catalog_before.value().size());
  for (std::size_t index = 0; index < catalog_before.value().size(); ++index) {
    EXPECT_EQ(catalog_after.value()[index].id, catalog_before.value()[index].id);
    EXPECT_EQ(catalog_after.value()[index].project_relative_path,
              catalog_before.value()[index].project_relative_path);
  }
  EXPECT_FALSE(windowed->isVisible());
  ui.shutdown();
}

TEST(QtProjectWorkspaceTests,
     FaceMaterialHighlightAndEmbeddedPrimitiveChoiceStayPresentationOnly) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  const auto isolated_profile = std::filesystem::temp_directory_path() /
      ("quader-browser-primitive-choice-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  ScopedIsolatedLocalAppData scoped_profile(isolated_profile);
  ASSERT_TRUE(scoped_profile.active());
  int argc = 1;
  char application_name[] = "quader-browser-primitive-choice-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  const auto resource_root = std::filesystem::path(QUADER_SOURCE_DIR) / "resources";
  const auto project_root = isolated_profile / "Primitive Materials";
  ASSERT_TRUE(std::filesystem::create_directory(project_root));
  const std::array texture_paths{isolated_profile / "First.png",
                                  isolated_profile / "Second.png"};
  QImage texture(2, 2, QImage::Format_RGBA8888);
  texture.fill(Qt::red);
  ASSERT_TRUE(texture.save(QString::fromStdWString(texture_paths[0].wstring())));
  texture.fill(Qt::green);
  ASSERT_TRUE(texture.save(QString::fromStdWString(texture_paths[1].wstring())));
  quader::RenderSystem render_system;
  quader::ProjectSession project;
  project.set_resource_root(resource_root / "modeling_assets");
  quader::QuaderEditorApi editor(project);
  const auto created = editor.project().create(project_root);
  ASSERT_TRUE(created.ok()) << created.error_message();
  const auto imported = editor.project_assets().import_textures_as_materials(
      texture_paths, "Materials");
  ASSERT_TRUE(imported.ok()) << imported.error_message();
  ASSERT_TRUE(imported.value().failed.empty());
  ASSERT_EQ(imported.value().imported.size(), 2u);
  const auto first = quader::QuaderMaterialReference::project(
      imported.value().imported[0].material.id);
  const auto second = quader::QuaderMaterialReference::project(
      imported.value().imported[1].material.id);
  const auto mesh = editor.modeling().create_box({
      .bounds = {.min = {1000.0f, 0.0f, 0.0f},
                  .max = {1016.0f, 16.0f, 16.0f}},
      .name = "Existing material assignments",
      .material = first,
  });
  ASSERT_TRUE(mesh.ok()) << mesh.error_message();
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection().select_face(mesh.value(), 2).ok());
  const auto token = editor.modeling().face_texture_settings().selection_token;
  ASSERT_TRUE(editor.modeling().apply_material_to_selected_faces(
      second, mesh.value().scene_session_id, token).ok());
  ASSERT_TRUE(editor.selection().select_face(mesh.value(), 1).ok());

  quader::UISystem ui;
  ASSERT_TRUE(ui.initialize(render_system, resource_root));
  ui.main_window()->setAttribute(Qt::WA_DontShowOnScreen, true);
  ASSERT_NE(project.active_scene(), nullptr);
  auto &scene = *project.active_scene()->session;
  ui.install_runtime_hooks(
      editor, scene.scene(), scene.assets(), scene.modeling(), scene.selection(),
      scene.native_tools(), scene.point_placement(), quader::kInvalidAssetId);
  auto *embedded = dynamic_cast<quader::QtMaterialBrowserWindow *>(
      ui.main_window()->findChild<QWidget *>(
          QStringLiteral("materialBrowser.embedded")));
  auto *windowed = dynamic_cast<quader::QtMaterialBrowserWindow *>(
      ui.main_window()->findChild<QWidget *>(
          QStringLiteral("materialBrowser.window")));
  auto *viewport = dynamic_cast<quader::QtViewportPanel *>(
      ui.main_window()->findChild<QWidget *>(QStringLiteral("viewportWorkspace")));
  ASSERT_NE(embedded, nullptr);
  ASSERT_NE(windowed, nullptr);
  ASSERT_NE(viewport, nullptr);
  ui.show();
  ASSERT_TRUE(editor.ui().show_panel("face_texture").ok());
  windowed->setAttribute(Qt::WA_DontShowOnScreen, true);
  windowed->show();
  application.processEvents();
  const auto history_before_sync = editor.history().snapshot();
  const auto revisions_before_sync = editor.revisions();
  ui.refresh({}, {});
  ui.refresh({}, {});
  EXPECT_EQ(embedded->selected_material(), first);
  EXPECT_EQ(editor.revisions().modeling_content_revision,
            revisions_before_sync.modeling_content_revision);
  EXPECT_EQ(editor.history().snapshot().undo_label, history_before_sync.undo_label);
  EXPECT_EQ(editor.history().snapshot().redo_label, history_before_sync.redo_label);

  // Programmatic highlighting must neither assign faces nor force the face's
  // material back over a later browser choice on every retained refresh.
  embedded->set_selected_material(second);
  windowed->set_selected_material(first);
  auto *grid = embedded->findChild<QListView *>(
      QStringLiteral("materialBrowser.gridView"));
  ASSERT_NE(grid, nullptr);
  auto *proxy = dynamic_cast<quader::QtMaterialBrowserProxyModel *>(grid->model());
  ASSERT_NE(proxy, nullptr);
  int catalog_resets = 0;
  QObject::connect(proxy->sourceModel(), &QAbstractItemModel::modelReset,
                   embedded, [&]() { ++catalog_resets; });
  const auto usage_work = project.material_usage().work_stats();
  for (int frame = 0; frame < 12; ++frame) {
    ui.refresh({}, {});
  }
  EXPECT_EQ(embedded->selected_material(), second);
  EXPECT_EQ(windowed->selected_material(), first);
  EXPECT_EQ(catalog_resets, 0);
  EXPECT_EQ(project.material_usage().work_stats(), usage_work);
  EXPECT_EQ(editor.revisions().modeling_content_revision,
            revisions_before_sync.modeling_content_revision);
  EXPECT_EQ(editor.history().snapshot().undo_label, history_before_sync.undo_label);
  EXPECT_EQ(editor.history().snapshot().redo_label, history_before_sync.redo_label);

  const auto rotated = editor.modeling().set_face_texture_setting({
      .field = quader::QuaderFaceTextureField::Rotate,
      .value = 30.0,
      .selection_token = editor.modeling().face_texture_settings().selection_token,
  });
  ASSERT_TRUE(rotated.ok()) << rotated.error_message();
  ASSERT_TRUE(rotated.changed());
  ui.refresh({}, {});
  EXPECT_EQ(embedded->selected_material(), second);
  ASSERT_TRUE(editor.history().undo().ok());
  ui.refresh({}, {});
  EXPECT_EQ(embedded->selected_material(), second);
  EXPECT_TRUE(editor.history().can_redo());

  const std::array additional_source{first.project_asset};
  const auto additional = editor.project_assets().duplicate_materials(additional_source);
  ASSERT_TRUE(additional.ok()) << additional.error_message();
  ui.refresh({}, {});
  EXPECT_EQ(embedded->selected_material(), second);
  EXPECT_GT(catalog_resets, 0);
  ASSERT_TRUE(editor.selection().select_face(mesh.value(), 2).ok());
  ui.refresh({}, {});
  EXPECT_EQ(embedded->selected_material(), second);
  ASSERT_TRUE(editor.selection().select_face(mesh.value(), 3).ok());
  ui.refresh({}, {});
  EXPECT_EQ(embedded->selected_material(), first);
  for (quader::QuaderElementId face = 1; face != 7; ++face) {
    const auto material = editor.modeling().project_material_for_face(
        mesh.value().modeling_object_id, face);
    ASSERT_TRUE(material.ok()) << material.error_message();
    EXPECT_EQ(material.value().id,
              face == 2 ? second.project_asset : first.project_asset);
  }

  ASSERT_TRUE(editor.selection().clear().ok());
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Object).ok());
  ui.refresh({}, {});
  auto *surface = viewport->surface(quader::ViewportPaneId::Perspective);
  ASSERT_NE(surface, nullptr);
  surface->resize(800, 600);
  auto &camera = scene.viewport_workspace().camera(quader::ViewportPaneId::Perspective);
  ASSERT_TRUE(camera.restore_perspective_pose(
      {.eye = {8, 7, 11}, .target = {}, .up = {0, 1, 0}}));
  const auto process = [&] {
    viewport->process_input(editor, scene.scene(), scene.assets(), scene.modeling(),
        quader::kInvalidAssetId, scene.native_tools(), &scene.point_placement(),
        1.0 / 60.0);
  };
  for (const bool choose_material : {true, false}) {
    SCOPED_TRACE(choose_material);
    ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::CreatePrimitive).ok());
    ASSERT_TRUE(editor.tools().set_setting(quader::kCreatePrimitiveToolId,
        quader::kPrimitiveTypeSetting,
        static_cast<double>(quader::PrimitiveCreationKind::Sphere)).ok());
    ui.refresh({}, {});
    embedded->set_selected_material(second);
    if (!choose_material) {
      // Ctrl-deselect and clicking blank grid space leave the keyboard current
      // index intact. A focused but unselected material must not seed creation.
      grid->selectionModel()->clearSelection();
      ASSERT_TRUE(grid->currentIndex().isValid());
    }
    windowed->set_selected_material(first);
    ui.refresh({}, {});
    EXPECT_EQ(embedded->current_selected_material().valid(), choose_material);
    QFocusEvent focus(QEvent::FocusIn);
    QApplication::sendEvent(surface, &focus);
    process();
    QMouseEvent press(QEvent::MouseButtonPress, {300, 300}, {300, 300},
                       Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(surface, &press);
    process();
    QMouseEvent release(QEvent::MouseButtonRelease, {500, 430}, {500, 430},
                         Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(surface, &release);
    process();
    ASSERT_TRUE(editor.tools().live_primitive_creation());
    const auto entity = scene.scene().primary_selection();
    const auto *material = scene.scene().material(entity);
    ASSERT_NE(material, nullptr);
    ASSERT_TRUE(material->material.project_owned());
    if (choose_material) {
      EXPECT_EQ(material->material.project_asset(), second.project_asset);
    } else {
      EXPECT_EQ(material->material.project_asset().str(),
                quader::kDefaultProjectMaterialId);
    }
    ASSERT_TRUE(editor.tools().commit_active_tool_preview().ok());
    ASSERT_TRUE(editor.selection().clear().ok());
  }
  ui.shutdown();
}

TEST(QtProjectWorkspaceTests,
     VisibleEmbeddedUsageSurvivesProjectCreateAndReopenGenerationEvents) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  const auto isolated_profile = std::filesystem::temp_directory_path() /
      ("quader-embedded-usage-generation-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  ScopedIsolatedLocalAppData scoped_profile(isolated_profile);
  ASSERT_TRUE(scoped_profile.active());
  int argc = 1;
  char application_name[] = "quader-embedded-usage-generation-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  const auto resource_root = std::filesystem::path(QUADER_SOURCE_DIR) / "resources";
  const auto project_root = isolated_profile / "Generation Project";
  ASSERT_TRUE(std::filesystem::create_directory(project_root));
  quader::RenderSystem render_system;
  quader::ProjectSession project;
  project.set_resource_root(resource_root / "modeling_assets");
  quader::QuaderEditorApi editor(project);
  quader::UISystem ui;
  ASSERT_TRUE(ui.initialize(render_system, resource_root));
  ASSERT_NE(ui.main_window(), nullptr);
  ui.main_window()->setAttribute(Qt::WA_DontShowOnScreen, true);
  int binding_count = 0;
  const auto bind_scene = [&](quader::SceneSession &scene) {
    ++binding_count;
    ui.install_runtime_hooks(
        editor, scene.scene(), scene.assets(), scene.modeling(), scene.selection(),
        scene.native_tools(), scene.point_placement(), quader::kInvalidAssetId);
  };
  quader::QuaderEditorApiSessionAccess::set_scene_binding_changed_callback(
      editor, bind_scene);
  bind_scene(project.fallback_scene());
  ui.show();
  application.processEvents();
  auto *dock = ui.main_window()->findChild<QDockWidget *>(
      QStringLiteral("dock_face_texture"));
  auto *browser = dynamic_cast<quader::QtMaterialBrowserWindow *>(
      ui.main_window()->findChild<QWidget *>(
          QStringLiteral("materialBrowser.embedded")));
  auto *windowed = ui.main_window()->findChild<QWidget *>(
      QStringLiteral("materialBrowser.window"));
  ASSERT_NE(dock, nullptr);
  ASSERT_NE(browser, nullptr);
  ASSERT_NE(windowed, nullptr);
  auto *grid = browser->findChild<QListView *>(
      QStringLiteral("materialBrowser.gridView"));
  auto *status = browser->findChild<QLabel *>(
      QStringLiteral("materialBrowser.usageStatus"));
  ASSERT_NE(grid, nullptr);
  ASSERT_NE(status, nullptr);
  auto *proxy = dynamic_cast<quader::QtMaterialBrowserProxyModel *>(grid->model());
  ASSERT_NE(proxy, nullptr);

  // This fixture has no renderer, so automatic workspace restoration cannot
  // initialize a viewport. Present the retained dock through its public intent
  // before the project binds, preserving the lifecycle ordering under test.
  ASSERT_TRUE(editor.ui().show_panel("face_texture").ok());
  application.processEvents();
  ASSERT_TRUE(dock->isVisible());

  // Project creation binds its scene synchronously before ProjectOpened reaches
  // retained UI consumers. Its later generation update must republish usage.
  const auto created = editor.project().create(project_root);
  ASSERT_TRUE(created.ok()) << created.error_message();
  ASSERT_GE(binding_count, 2);
  ui.refresh({}, {});
  ASSERT_TRUE(dock->isVisible());
  EXPECT_FALSE(windowed->isVisible());
  EXPECT_TRUE(proxy->usage_complete());
  EXPECT_TRUE(status->isHidden());
  EXPECT_GT(grid->model()->rowCount(), 0);
  ui.refresh({}, {});
  EXPECT_TRUE(proxy->usage_complete());

  const auto active_scene = editor.scene_documents().active_scene_id();
  ASSERT_TRUE(active_scene.has_value());
  const auto scene_path =
      quader::ProjectRelativePath::parse_portable("Scenes/Usage.qdr");
  ASSERT_TRUE(scene_path.ok());
  const auto saved =
      editor.scene_documents().save_as(*active_scene, scene_path.value());
  ASSERT_TRUE(saved.ok()) << saved.error_message();
  ui.refresh({}, {});
  const auto bindings_before_reopen = binding_count;
  const auto reopened = editor.project().open(editor.project().manifest_path());
  ASSERT_TRUE(reopened.ok()) << reopened.error_message();
  ASSERT_TRUE(reopened.changed());
  EXPECT_GT(binding_count, bindings_before_reopen);
  ui.refresh({}, {});
  ASSERT_TRUE(dock->isVisible());
  EXPECT_FALSE(windowed->isVisible());
  EXPECT_TRUE(proxy->usage_complete());
  EXPECT_TRUE(status->isHidden());
  EXPECT_GT(grid->model()->rowCount(), 0);
  const auto retained_work = project.material_usage().work_stats();
  for (int frame = 0; frame != 3; ++frame) {
    ui.refresh({}, {});
    EXPECT_TRUE(proxy->usage_complete());
  }
  EXPECT_EQ(project.material_usage().work_stats(), retained_work);
  ui.shutdown();
}
