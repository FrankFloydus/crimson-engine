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
#include "api/quader_project_assets_api_internal_access.hpp"
#include "app/app.hpp"
#include "app/app_frame_schedule.hpp"
#include "assets/default_material_catalog.hpp"
#include "core/localization_service.hpp"
#include "core/logger.hpp"
#include "editor/assets/project_asset_service.hpp"
#include "editor/material/project_material_usage_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/modeling/services/primitive_creation_tool_service.hpp"
#include "editor/render/quader_render_api_runtime_access.hpp"
#include "editor/session/project_session.hpp"
#include "editor/session/quader_editor_api_session_access.hpp"
#include "editor/session/scene_session.hpp"
#include "platform/window_placement.hpp"
#include "platform/fatal_error_handler.hpp"
#include "render/render_system.hpp"
#include "render/scene_render_bridge.hpp"
#include "support/qt_viewport_input_test_helpers.hpp"
#include "quader/modeling/neutral/polygon/document_model.hpp"
#include "ui/qt_application_cursor.hpp"
#include "ui/qt_application_theme.hpp"
#include "ui/qt_application_style.hpp"
#include "ui/qt_asset_browser.hpp"
#include "ui/qt_auxiliary_window_state.hpp"
#include "ui/qt_contextual_input_hints.hpp"
#include "ui/qt_double_spin_box.hpp"
#include "ui/qt_main_menu.hpp"
#include "ui/qt_multiresolution_icon.hpp"
#include "ui/qt_numeric_spin_box.hpp"
#include "ui/qt_material_browser_window.hpp"
#include "ui/qt_material_preview_surface.hpp"
#include "ui/qt_operation_input_hints.hpp"
#include "ui/qt_project_asset_dialog.hpp"
#include "ui/qt_project_home_page.hpp"
#include "ui/qt_scene_workspace.hpp"
#include "ui/qt_selection_toolbar.hpp"
#include "ui/qt_snapping_toolbar.hpp"
#include "ui/qt_status_message.hpp"
#include "ui/qt_texture_toolbar.hpp"
#include "ui/qt_toolbar_button_presentation.hpp"
#include "ui/qt_tool_toolbar.hpp"
#include "ui/qt_tool_settings_panel.hpp"
#include "ui/qt_ui_scale_metrics.hpp"
#include "ui/qt_viewport_control_strip.hpp"
#include "ui/qt_viewport_panel.hpp"
#include "ui/qt_viewport_surface.hpp"
#include "ui/qt_viewport_display_toolbar.hpp"
#include "ui/qt_vulkan_composition_widget.hpp"
#include "ui/ui_system.hpp"
#include "ui/ui_settings.hpp"
#include "viewport/host_viewport_contract_adapter.hpp"
#include "viewport/viewport_settings.hpp"

#include <QAction>
#include <QActionGroup>
#include <QAbstractNativeEventFilter>
#include <QAbstractAnimation>
#include <QAbstractItemModel>
#include <QAbstractItemDelegate>
#include <QAbstractItemView>
#include <QApplication>
#include <QBackingStore>
#include <QBuffer>
#include <QByteArray>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDataStream>
#include <QDialog>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEventLoop>
#include <QFrame>
#include <QFocusEvent>
#include <QGuiApplication>
#include <QGraphicsOpacityEffect>
#include <QHeaderView>
#include <QIcon>
#include <QIODevice>
#include <QKeyEvent>
#include <QMainWindow>
#include <QListView>
#include <QLocale>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMetaObject>
#include <QMimeData>
#include <QModelIndex>
#include <QItemSelectionModel>
#include <QLabel>
#include <QImage>
#include <QImageReader>
#include <QLayout>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPalette>
#include <QPointer>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QPropertyAnimation>
#include <QPixmap>
#include <QRegion>
#include <QResizeEvent>
#include <QScreen>
#include <QScopeGuard>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyleOption>
#include <QStyleFactory>
#include <QStyleOptionViewItem>
#include <QTableView>
#include <QTabBar>
#include <QToolBar>
#include <QToolButton>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeView>
#include <QTreeWidgetItemIterator>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QScopedValueRollback>
#include <qpa/qwindowsysteminterface.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

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
  explicit ScopedIsolatedLocalAppData(std::filesystem::path profile,
                                     bool preserve_files = false)
      : profile_(std::move(profile)), preserve_files_(preserve_files) {
    if (const char *current = std::getenv("LOCALAPPDATA")) {
      previous_ = current;
    }
    std::error_code error;
    if (!preserve_files_) std::filesystem::remove_all(profile_, error);
    error.clear();
    std::filesystem::create_directories(profile_, error);
    if (error || !std::filesystem::is_directory(profile_)) {
      return;
    }
    active_ = set_local_app_data(profile_.string());
  }

  ~ScopedIsolatedLocalAppData() {
    (void)set_local_app_data(previous_);
    std::error_code error;
    if (!preserve_files_) std::filesystem::remove_all(profile_, error);
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
  bool preserve_files_ = false;
};

class RuntimeRebindWindowHideProbe final : public QObject {
public:
  bool fallback_rebind_in_progress = false;
  bool hidden_during_fallback_rebind = false;
  int hide_count = 0;

protected:
  bool eventFilter(QObject *watched, QEvent *event) override {
    if (event->type() == QEvent::Hide) {
      ++hide_count;
      hidden_during_fallback_rebind = fallback_rebind_in_progress;
    }
    return QObject::eventFilter(watched, event);
  }
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
     PrimitiveToolbarUsesDelayedPopupAndRemembersSuccessfulOwnerChoices) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader_primitive_toolbar_test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  ASSERT_NE(QApplication::setStyle(QStringLiteral("Fusion")), nullptr);
  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  QMainWindow host;
  quader::QtToolToolbar toolbar;
  quader::LocalizationService localization;
  toolbar.set_localization(localization);
  host.addToolBar(Qt::TopToolBarArea, &toolbar);
  host.resize(1100, 180);
  host.show();
  application.processEvents();

  auto *primitive = toolbar.findChild<QAction *>(
      QStringLiteral("tool_create_primitive"));
  ASSERT_NE(primitive, nullptr);
  auto *button = qobject_cast<QToolButton *>(toolbar.widgetForAction(primitive));
  ASSERT_NE(button, nullptr);
  auto *menu = primitive->menu();
  ASSERT_NE(menu, nullptr);
  ASSERT_EQ(menu->actions().size(), 5);
  EXPECT_EQ(button->popupMode(), QToolButton::DelayedPopup);
  EXPECT_EQ(button->focusPolicy(), Qt::StrongFocus);
  const std::array<quader::QuaderCommandId, 5> commands{
      quader::quader_command::kToolsActivateCreateBox,
      quader::quader_command::kToolsActivateCreateCylinder,
      quader::quader_command::kToolsActivateCreateSphere,
      quader::quader_command::kToolsActivateCreateCone,
      quader::quader_command::kToolsActivateCreateTorus};
  for (std::size_t index = 0; index < commands.size(); ++index) {
    EXPECT_EQ(menu->actions()[index]->objectName(),
              QStringLiteral("command_%1").arg(
                  QString::fromUtf8(commands[index].string().data())));
    EXPECT_TRUE(menu->actions()[index]->shortcut().isEmpty());
  }
  int requests = 0;
  std::string last_command;
  toolbar.set_command_requested_callback([&](quader::QuaderCommandId command) {
    ++requests;
    last_command = command.string();
    EXPECT_TRUE(editor.commands().execute(command).ok());
    toolbar.synchronize(editor, editor.selection().summary(), true);
  });
  toolbar.synchronize(editor, editor.selection().summary());
  EXPECT_EQ(primitive->text(), QStringLiteral("Create Box"));
  EXPECT_TRUE(primitive->toolTip().contains(QStringLiteral("[Shift+B]")));
  EXPECT_FALSE(primitive->isChecked());

  click_widget(*button, button->rect().center());
  EXPECT_EQ(requests, 1);
  EXPECT_EQ(last_command, commands[0].string());
  EXPECT_TRUE(primitive->isChecked());
  EXPECT_EQ(editor.tools().preferred_primitive_shape(),
            quader::QuaderPrimitiveCreationShape::Box);
  const auto initial_revision = editor.tools().active_tool_revision();
  click_widget(*button, button->rect().center());
  EXPECT_EQ(requests, 2);
  EXPECT_EQ(editor.tools().active_tool_revision(), initial_revision)
      << "clicking the active shape must retain its creation session";

  bool held_popup_opened = false;
  QEventLoop held_events;
  const auto popup_connection = QObject::connect(
      menu, &QMenu::aboutToShow, &held_events, [&]() {
        QTimer::singleShot(0, &held_events, [&]() {
          held_popup_opened = menu->isVisible();
          EXPECT_EQ(requests, 2);
          send_key(*menu, Qt::Key_Escape);
          held_events.quit();
        });
      });
  QTimer timeout;
  timeout.setSingleShot(true);
  QObject::connect(&timeout, &QTimer::timeout, &held_events, [&]() {
    menu->close();
    held_events.quit();
  });
  timeout.start(button->style()->styleHint(
                    QStyle::SH_ToolButton_PopupDelay, nullptr, button) + 1500);
  send_mouse_event(*button, QEvent::MouseButtonPress, button->rect().center(),
                   Qt::LeftButton, Qt::LeftButton);
  held_events.exec();
  timeout.stop();
  QObject::disconnect(popup_connection);
  send_mouse_event(*button, QEvent::MouseButtonRelease, button->rect().center(),
                   Qt::LeftButton, Qt::NoButton);
  EXPECT_TRUE(held_popup_opened);
  EXPECT_FALSE(menu->isVisible());
  EXPECT_EQ(requests, 2) << "holding and dismissing must not activate anything";
  EXPECT_EQ(editor.tools().active_tool_revision(), initial_revision);

  // Holding Space uses Qt's native button-press delay, just like holding the
  // mouse. Arrow keys navigate toolbar focus rather than opening this popup.
  bool keyboard_popup_opened = false;
  QEventLoop keyboard_events;
  const auto keyboard_popup_connection = QObject::connect(
      menu, &QMenu::aboutToShow, &keyboard_events, [&]() {
        QTimer::singleShot(0, &keyboard_events, [&]() {
          keyboard_popup_opened = menu->isVisible();
          menu->setActiveAction(menu->actions()[1]);
          send_key(*menu, Qt::Key_Return);
          menu->close();
          keyboard_events.quit();
        });
      });
  QTimer keyboard_timeout;
  keyboard_timeout.setSingleShot(true);
  QObject::connect(&keyboard_timeout, &QTimer::timeout, &keyboard_events, [&]() {
    menu->close();
    keyboard_events.quit();
  });
  keyboard_timeout.start(button->style()->styleHint(
      QStyle::SH_ToolButton_PopupDelay, nullptr, button) + 1500);
  button->setFocus();
  QKeyEvent space_press(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
  QCoreApplication::sendEvent(button, &space_press);
  keyboard_events.exec();
  keyboard_timeout.stop();
  QObject::disconnect(keyboard_popup_connection);
  QKeyEvent space_release(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
  QCoreApplication::sendEvent(button, &space_release);
  EXPECT_TRUE(keyboard_popup_opened);
  EXPECT_EQ(requests, 3);
  EXPECT_EQ(last_command, commands[1].string());
  EXPECT_EQ(primitive->text(), QStringLiteral("Create Cylinder"));
  EXPECT_EQ(button->accessibleName(), primitive->text());
  EXPECT_TRUE(primitive->toolTip().contains(QStringLiteral("[Shift+C]")));
  EXPECT_EQ(primitive->icon().cacheKey(), menu->actions()[1]->icon().cacheKey());

  ASSERT_TRUE(editor.commands().execute(
      quader::quader_command::kToolsActivateSelect).ok());
  toolbar.synchronize(editor, editor.selection().summary());
  EXPECT_FALSE(primitive->isChecked());
  EXPECT_EQ(primitive->text(), QStringLiteral("Create Cylinder"));
  click_widget(*button, button->rect().center());
  EXPECT_EQ(requests, 4);
  EXPECT_EQ(last_command, commands[1].string());

  ASSERT_TRUE(editor.commands().execute(commands[2]).ok());
  toolbar.synchronize(editor, editor.selection().summary());
  EXPECT_EQ(primitive->text(), QStringLiteral("Create Sphere"));
  EXPECT_TRUE(menu->actions()[2]->isChecked());
  EXPECT_TRUE(primitive->toolTip().contains(QStringLiteral("[Shift+S]")));
  const auto rebind = editor.preferences().set_shortcut(commands[2], "Ctrl+Alt+F23");
  ASSERT_TRUE(rebind.ok()) << rebind.error_message();
  toolbar.synchronize(editor, editor.selection().summary());
  EXPECT_TRUE(primitive->toolTip().contains(QStringLiteral("[Ctrl+Alt+F23]")));
  EXPECT_TRUE(menu->actions()[2]->text().endsWith(QStringLiteral("\tCtrl+Alt+F23")));
  EXPECT_TRUE(menu->actions()[2]->shortcut().isEmpty());

  int presentation_changes = 0;
  for (QAction *action : toolbar.findChildren<QAction *>()) {
    QObject::connect(action, &QAction::changed, &toolbar,
                     [&]() { ++presentation_changes; });
  }
  for (int frame = 0; frame < 5; ++frame) {
    toolbar.synchronize(editor, editor.selection().summary());
  }
  EXPECT_EQ(presentation_changes, 0);

  // A failed intent cannot become the remembered face merely because Qt
  // checked its menu row before dispatching it.
  toolbar.set_command_requested_callback([&](quader::QuaderCommandId) {
    toolbar.synchronize(editor, editor.selection().summary(), true);
  });
  menu->actions()[0]->trigger();
  EXPECT_EQ(primitive->text(), QStringLiteral("Create Sphere"));
  EXPECT_TRUE(menu->actions()[2]->isChecked());
  EXPECT_FALSE(menu->actions()[0]->isChecked());
}

TEST(QtProjectWorkspaceTests,
     PrimitiveToolbarKeepsNativePopupPresentationAcrossScaleAndDockChanges) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader_primitive_toolbar_presentation_test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  ASSERT_NE(QApplication::setStyle(QStringLiteral("Fusion")), nullptr);
  const QPalette native_palette = application.palette();
  quader::apply_qt_obsidian_application_theme(native_palette);
  quader::QtUiScaleApplicationController scale_controller(application);
  QMainWindow host;
  host.setAnimated(false);
  quader::QtToolToolbar toolbar;
  quader::LocalizationService localization;
  toolbar.set_localization(localization);
  host.addToolBar(Qt::TopToolBarArea, &toolbar);
  host.resize(1000, 240);
  host.show();

  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  ASSERT_TRUE(editor.commands().execute(
      quader::quader_command::kToolsActivateCreateSphere).ok());
  auto *primitive = toolbar.findChild<QAction *>(
      QStringLiteral("tool_create_primitive"));
  ASSERT_NE(primitive, nullptr);
  auto *button = qobject_cast<QToolButton *>(toolbar.widgetForAction(primitive));
  ASSERT_NE(button, nullptr);
  QMenu *menu = primitive->menu();
  ASSERT_NE(menu, nullptr);
  const QString review_directory = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
  // The test executable has no application QRC. Use the identical shipping
  // assets for native-widget geometry and optional captures.
  const QDir icons(QStringLiteral(QUADER_SOURCE_DIR "/resources/icons"));
  for (QAction *action : toolbar.actions()) {
    const QString name = action->objectName();
    if (!name.startsWith(QStringLiteral("tool_")) || action == primitive ||
        name == QStringLiteral("tool_curve") ||
        name == QStringLiteral("tool_create_stairs")) {
      continue;
    }
    const QString file_name =
        name == QStringLiteral("tool_lasso_select")
            ? QStringLiteral("tool_lasso.png")
            : name + QStringLiteral(".png");
    const QIcon icon = quader::qt_multiresolution_icon(
        icons.filePath(file_name));
    ASSERT_FALSE(icon.isNull()) << name.toStdString();
    action->setIcon(icon);
  }
  const std::array<const char *, 5> names{
      "box", "cylinder", "sphere", "cone", "torus"};
  for (std::size_t index = 0; index < names.size(); ++index) {
    const QIcon icon = quader::qt_multiresolution_icon(icons.filePath(
        QStringLiteral("tool_create_%1.png").arg(QString::fromUtf8(names[index]))));
    ASSERT_FALSE(icon.isNull()) << names[index];
    menu->actions()[index]->setIcon(icon);
  }
  if (!review_directory.isEmpty()) {
    ASSERT_TRUE(QDir().mkpath(review_directory));
    QToolBar icon_review;
    icon_review.setMovable(false);
    for (QAction *action : menu->actions()) {
      icon_review.addAction(action->icon(), action->text());
    }
    for (const int logical_size : {32, 64}) {
      icon_review.setIconSize(QSize(logical_size, logical_size));
      icon_review.adjustSize();
      icon_review.show();
      application.processEvents();
      EXPECT_TRUE(icon_review.grab().save(QDir(review_directory).filePath(
          QStringLiteral("primitive-icons-%1.png").arg(logical_size))));
    }
  }
  toolbar.synchronize(editor, editor.selection().summary(), true);
  ASSERT_EQ(primitive->text(), QStringLiteral("Create Sphere"));

  const auto settle_layout = [&]() {
    QEventLoop events;
    QTimer::singleShot(100, &events, &QEventLoop::quit);
    events.exec();
    for (int pass = 0; pass < 3; ++pass) {
      QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
      application.processEvents();
      host.layout()->invalidate();
      host.layout()->activate();
      toolbar.layout()->invalidate();
      toolbar.layout()->activate();
    }
  };
  const auto review = [&](const QString &suffix) {
    menu->popup(button->mapToGlobal(QPoint(button->width(), button->height())));
    application.processEvents();
    EXPECT_TRUE(menu->isVisible());
    EXPECT_EQ(button->accessibleName(), QStringLiteral("Create Sphere"));
    EXPECT_EQ(button->accessibleDescription(), primitive->toolTip());
    EXPECT_EQ(button->popupMode(), QToolButton::DelayedPopup);
    for (QAction *action : menu->actions()) {
      EXPECT_TRUE(menu->actionGeometry(action).isValid());
      EXPECT_TRUE(menu->rect().contains(menu->actionGeometry(action)));
      EXPECT_TRUE(action->shortcut().isEmpty());
    }
    EXPECT_TRUE(menu->actions()[2]->isChecked());
    if (!review_directory.isEmpty()) {
      EXPECT_TRUE(toolbar.grab().save(QDir(review_directory).filePath(
          QStringLiteral("primitive-toolbar-%1.png").arg(suffix))));
      EXPECT_TRUE(menu->grab().save(QDir(review_directory).filePath(
          QStringLiteral("primitive-menu-%1.png").arg(suffix))));
    }
    send_key(*menu, Qt::Key_Escape);
    EXPECT_FALSE(menu->isVisible());
  };
  const auto expect_compact_vertical_toolbar = [&]() {
    int widest_button = scale_controller.metrics().px(40);
    for (QAction *action : toolbar.actions()) {
      auto *visible_button =
          qobject_cast<QToolButton *>(toolbar.widgetForAction(action));
      if (visible_button == nullptr || !visible_button->isVisible()) {
        continue;
      }
      widest_button = std::max(widest_button, visible_button->sizeHint().width());
      EXPECT_TRUE(toolbar.rect().contains(visible_button->geometry()));
    }
    const QMargins outer_margins = toolbar.contentsMargins();
    const QMargins layout_margins = toolbar.layout()->contentsMargins();
    const int native_chrome =
        toolbar.style()->pixelMetric(QStyle::PM_ToolBarHandleExtent, nullptr, &toolbar) +
        2 * toolbar.style()->pixelMetric(QStyle::PM_ToolBarFrameWidth, nullptr, &toolbar) +
        2 * toolbar.style()->pixelMetric(QStyle::PM_ToolBarItemMargin, nullptr, &toolbar);
    const int compact_width = widest_button + native_chrome +
        outer_margins.left() + outer_margins.right() +
        layout_margins.left() + layout_margins.right();
    // Text-only launchers retain their native font width in a vertical dock;
    // the toolbar adds only its own measured margins and native chrome.
    EXPECT_LE(toolbar.width(), compact_width);
  };
  int cycle = 0;
  for (const float factor : {1.0F, 2.0F, 1.0F}) {
    scale_controller.apply_factor(factor);
    const auto &metrics = scale_controller.metrics();
    toolbar.apply_ui_scale(metrics);
    host.resize(metrics.px(1000), metrics.px(240));
    settle_layout();
    EXPECT_EQ(toolbar.iconSize(), metrics.size(QSize(32, 32)));
    EXPECT_EQ(button->minimumSize(), metrics.size(QSize(40, 40)));
    review(QStringLiteral("%1-%2").arg(qRound(factor * 100)).arg(cycle++));
  }

  host.removeToolBar(&toolbar);
  host.addToolBar(Qt::LeftToolBarArea, &toolbar);
  toolbar.show();
  host.resize(360, 880);
  settle_layout();
  EXPECT_EQ(toolbar.orientation(), Qt::Vertical);
  expect_compact_vertical_toolbar();
  EXPECT_TRUE(button->isVisible());
  review(QStringLiteral("narrow-dock"));
  for (const float factor : {2.0F, 1.0F}) {
    scale_controller.apply_factor(factor);
    const auto &metrics = scale_controller.metrics();
    toolbar.apply_ui_scale(metrics);
    host.resize(metrics.size(QSize(360, 880)));
    settle_layout();
    expect_compact_vertical_toolbar();
    review(QStringLiteral("narrow-dock-%1").arg(qRound(factor * 100.0F)));
  }
  toolbar.setLayoutDirection(Qt::RightToLeft);
  review(QStringLiteral("rtl"));
  EXPECT_EQ(menu->layoutDirection(), Qt::RightToLeft);

  host.removeToolBar(&toolbar);
  toolbar.setParent(nullptr);
  toolbar.setWindowFlags(Qt::Tool);
  toolbar.setOrientation(Qt::Horizontal);
  toolbar.resize(toolbar.sizeHint());
  toolbar.show();
  application.processEvents();
  EXPECT_TRUE(toolbar.isFloating());
  review(QStringLiteral("floating-rtl"));

  quader::apply_qt_application_theme_for_contrast_preference(
      native_palette, Qt::ContrastPreference::HighContrast);
  application.processEvents();
  EXPECT_EQ(application.palette().resolveMask(), 0u);
  review(QStringLiteral("native-high-contrast-fallback"));
  toolbar.setOrientation(Qt::Vertical);
  toolbar.resize(toolbar.sizeHint());
  application.processEvents();
  review(QStringLiteral("vertical-native-high-contrast-fallback"));
}

TEST(QtProjectWorkspaceTests,
     EditorToolbarsKeepIdleFacesQuietAndInteractionStatesVisible) {
  int argc = 1;
  char application_name[] = "quader_toolbar_test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);

  quader::QtSelectionToolbar selection_toolbar;
  quader::QtToolToolbar tool_toolbar;
  quader::QtSnappingToolbar snapping_toolbar;
  for (const auto tool : {quader::ViewportToolId::Select,
                          quader::ViewportToolId::LassoSelect,
                          quader::ViewportToolId::Move,
                          quader::ViewportToolId::Rotate,
                          quader::ViewportToolId::Scale}) {
    QAction *source = tool_toolbar.tool_action(tool);
    ASSERT_NE(source, nullptr);
    EXPECT_EQ(source->parent(), &tool_toolbar);
    EXPECT_FALSE(tool_toolbar.actions().contains(source));
    EXPECT_EQ(tool_toolbar.widgetForAction(source), nullptr);
    EXPECT_TRUE(source->isVisible());
    EXPECT_TRUE(source->isCheckable());
    EXPECT_TRUE(source->shortcuts().isEmpty());
  }
  const QPalette theme =
      quader::qt_obsidian_application_palette(application.palette());
  selection_toolbar.setPalette(theme);
  tool_toolbar.setPalette(theme);

  EXPECT_EQ(selection_toolbar.iconSize(), QSize(32, 32));
  EXPECT_EQ(tool_toolbar.iconSize(), QSize(32, 32));
  EXPECT_EQ(expect_persistent_button_faces(selection_toolbar, 30), 5);
  EXPECT_EQ(expect_persistent_button_faces(tool_toolbar, 40), 15);
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
  EXPECT_EQ(expect_persistent_button_faces(tool_toolbar, 80), 15);
  EXPECT_EQ(scale_metric_probe->style()->pixelMetric(
                QStyle::PM_ButtonMargin, nullptr, scale_metric_probe),
            baseline_button_margin * 2);

  selection_toolbar.apply_ui_scale(
      quader::QtUiScaleMetrics(1.0F, 2U));
  tool_toolbar.apply_ui_scale(quader::QtUiScaleMetrics(1.0F, 2U));
  EXPECT_EQ(selection_toolbar.iconSize(), QSize(32, 32));
  EXPECT_EQ(tool_toolbar.iconSize(), QSize(32, 32));
  EXPECT_EQ(expect_persistent_button_faces(selection_toolbar, 30), 5);
  EXPECT_EQ(expect_persistent_button_faces(tool_toolbar, 40), 15);
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
  snapping_toolbar.set_localization(localization);
  const quader::QuaderSelectionSummary selection =
      editor.selection().summary();
  tool_toolbar.synchronize(editor, selection, true);
  snapping_toolbar.synchronize(editor, true);
  auto *move = tool_toolbar.findChild<QAction *>(QStringLiteral("tool_move"));
  ASSERT_NE(move, nullptr);
  EXPECT_TRUE(move->toolTip().contains(QStringLiteral("[W]")));
  EXPECT_EQ(tool_toolbar.findChild<QAction *>(
                QStringLiteral("toggle_transform_space")), nullptr);
  auto *transform_space = snapping_toolbar.findChild<QAction *>(
      QStringLiteral("toggle_transform_space"));
  ASSERT_NE(transform_space, nullptr);
  EXPECT_EQ(transform_space->actionGroup(), nullptr);
  EXPECT_FALSE(transform_space->isChecked());
  EXPECT_TRUE(transform_space->toolTip().contains(QStringLiteral("[T]")));

  ASSERT_TRUE(editor.viewports()
                  .set_transform_space(quader::QuaderTransformSpace::Local)
                  .ok());
  snapping_toolbar.synchronize(editor);
  EXPECT_TRUE(transform_space->isChecked());

  int transform_space_toggle_requests = 0;
  snapping_toolbar.set_transform_space_toggle_requested_callback([&]() {
    ++transform_space_toggle_requests;
    EXPECT_TRUE(editor.commands().execute(
        quader::quader_command::kViewportToggleTransformSpace).ok());
  });
  transform_space->trigger();
  EXPECT_EQ(transform_space_toggle_requests, 1);
  EXPECT_EQ(editor.viewports().transform_space(),
            quader::QuaderTransformSpace::Global);
  snapping_toolbar.synchronize(editor);
  EXPECT_FALSE(transform_space->isChecked());

  ASSERT_TRUE(editor.preferences().set_shortcut(
                  quader::quader_command::kViewportToggleTransformSpace,
                  "Ctrl+Alt+T").ok());
  snapping_toolbar.synchronize(editor);
  EXPECT_TRUE(transform_space->toolTip().contains(QStringLiteral("[Ctrl+Alt+T]")));
  int transform_presentation_changes = 0;
  QObject::connect(transform_space, &QAction::changed, &snapping_toolbar,
                   [&]() { ++transform_presentation_changes; });
  snapping_toolbar.synchronize(editor);
  EXPECT_EQ(transform_presentation_changes, 0);

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
     SelectionToolbarKeepsAllModesAvailableAndRetainsUnchangedControls) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-selection-toolbar-test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  ASSERT_NE(QApplication::setStyle(QStringLiteral("Fusion")), nullptr);
  const QPalette native_palette = application.palette();
  quader::apply_qt_obsidian_application_theme(native_palette);
  quader::QtUiScaleApplicationController scale_controller(application);
  quader::SceneSession scene;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(scene));
  const auto keybindings = std::filesystem::temp_directory_path() /
      ("quader-mode-toolbar-bindings-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()) + ".json");
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path(keybindings).ok());
  const auto first = editor.modeling().create_box(
      {.min = {-1, -1, -1}, .max = {1, 1, 1}}, "First edit participant");
  const auto second = editor.modeling().create_box(
      {.min = {3, -1, -1}, .max = {5, 1, 1}}, "Second edit participant");
  ASSERT_TRUE(first.ok());
  ASSERT_TRUE(second.ok());
  ASSERT_TRUE(editor.selection().select_objects({first.value(), second.value()}).ok());
  QMainWindow host;
  host.setAnimated(false);
  ASSERT_NE(application.primaryScreen(), nullptr);
  host.move(application.primaryScreen()->availableGeometry().topLeft() + QPoint(40, 40));
  quader::QtSelectionToolbar toolbar;
  quader::LocalizationService localization;
  ASSERT_TRUE(localization.load(
      std::filesystem::path(QUADER_SOURCE_DIR) / "resources" / "locales", "en-US"));
  toolbar.set_localization(localization);
  host.addToolBar(Qt::TopToolBarArea, &toolbar);
  // This test target has no application QRC; load the same shipping icons so
  // layout assertions and optional native captures use the real toolbar faces.
  const QDir icons(QStringLiteral(QUADER_SOURCE_DIR "/resources/icons"));
  QList<QAction *> retained_actions;
  QList<QToolButton *> retained_buttons;
  for (const auto *kind : {"Vertex", "Edge", "Face", "Mesh", "Group"}) {
    const QString suffix = QString::fromLatin1(kind);
    auto *action = toolbar.findChild<QAction *>(
        QStringLiteral("selectionMode%1").arg(suffix));
    ASSERT_NE(action, nullptr);
    const QIcon icon = quader::qt_multiresolution_icon(icons.filePath(
        QStringLiteral("selection_%1.png").arg(suffix.toLower())));
    ASSERT_FALSE(icon.isNull());
    action->setIcon(icon);
    auto *button = qobject_cast<QToolButton *>(toolbar.widgetForAction(action));
    ASSERT_NE(button, nullptr);
    retained_actions.push_back(action);
    retained_buttons.push_back(button);
  }
  EXPECT_EQ(toolbar.findChild<QComboBox *>(QStringLiteral("editorModeCombo")), nullptr);
  ASSERT_EQ(toolbar.actions().size(), 5);
  EXPECT_EQ(toolbar.findChild<QLabel *>(QStringLiteral("selectionToolbarLabel")), nullptr);
  EXPECT_EQ(toolbar.toolButtonStyle(), Qt::ToolButtonIconOnly);
  int selection_requests = 0;
  int group_toggle_requests = 0;
  toolbar.set_mode_requested_callback([&](quader::QuaderSelectionMode mode) {
    ++selection_requests;
    EXPECT_TRUE(editor.selection().set_mode(mode).ok());
    toolbar.synchronize(editor, editor.selection().summary().mode);
  });
  toolbar.set_group_selection_toggle_requested_callback([&]() {
    ++group_toggle_requests;
    EXPECT_TRUE(editor.commands().execute(
        quader::quader_command::kSelectToggleGroupSelection).ok());
    toolbar.synchronize(editor, editor.selection().summary().mode, true);
  });
  const auto verify_controls = [&]() {
    EXPECT_EQ(toolbar.actions().size(), 5);
    const auto mode = editor.selection().summary().mode;
    int checked_mode_actions = 0;
    for (qsizetype index = 0; index < retained_actions.size(); ++index) {
      QAction *action = retained_actions[index];
      QToolButton *button = retained_buttons[index];
      EXPECT_EQ(toolbar.findChild<QAction *>(action->objectName()), action);
      EXPECT_EQ(toolbar.widgetForAction(action), button);
      EXPECT_TRUE(action->isVisible());
      const bool group = action->objectName() == "selectionModeGroup";
      EXPECT_EQ(action->isEnabled(), !group ||
          editor.mode().current() == quader::QuaderEditorMode::Object);
      if (!group && action->isChecked()) {
        ++checked_mode_actions;
      }
      if (group) {
        EXPECT_EQ(action->isChecked(), mode == quader::QuaderSelectionMode::Group);
      }
      EXPECT_FALSE(action->text().isEmpty());
      EXPECT_FALSE(action->toolTip().isEmpty());
      EXPECT_FALSE(action->icon().isNull());
      EXPECT_EQ(button->toolButtonStyle(), Qt::ToolButtonIconOnly);
      EXPECT_EQ(button->accessibleName(), action->text());
      EXPECT_EQ(button->accessibleDescription(), action->toolTip());
      EXPECT_EQ(button->focusPolicy(), Qt::StrongFocus);
    }
    EXPECT_EQ(checked_mode_actions, 1);
  };
  const QString review_directory = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
  if (!review_directory.isEmpty()) {
    ASSERT_TRUE(QDir().mkpath(review_directory));
  }
  const auto review_layout = [&](const QString &suffix, float factor,
                                 int design_width) {
    SCOPED_TRACE(suffix.toStdString());
    (void)scale_controller.apply_factor(factor);
    const auto &metrics = scale_controller.metrics();
    toolbar.apply_ui_scale(metrics);
    host.resize(metrics.px(design_width), metrics.px(180));
    host.show();
    for (int pass = 0; pass < 3; ++pass) {
      QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
      application.processEvents();
      host.layout()->invalidate();
      host.layout()->activate();
      toolbar.layout()->invalidate();
      toolbar.layout()->activate();
    }
    verify_controls();
    QList<QAction *> overflowed_actions;
    for (qsizetype index = 0; index < retained_actions.size(); ++index) {
      QAction *action = retained_actions[index];
      const QToolButton *button = retained_buttons[index];
      if (!action->isVisible()) {
        EXPECT_FALSE(button->isVisible());
      } else if (!button->isVisible()) {
        overflowed_actions.push_back(action);
      }
      if (button->isVisible()) {
        EXPECT_EQ(button->height(), metrics.px(30));
        EXPECT_TRUE(toolbar.rect().contains(button->geometry()));
      }
    }
    if (!review_directory.isEmpty()) {
      EXPECT_TRUE(toolbar.grab().save(QDir(review_directory).filePath(
          QStringLiteral("selection-toolbar-%1.png").arg(suffix))));
    }
    if (!overflowed_actions.isEmpty()) {
      auto *extension = toolbar.findChild<QToolButton *>(
          QStringLiteral("qt_toolbar_ext_button"));
      ASSERT_NE(extension, nullptr);
      ASSERT_TRUE(extension->isVisible());
      ASSERT_TRUE(extension->isEnabled());
      if (QMenu *menu = extension->menu()) {
        menu->popup(extension->mapToGlobal(QPoint(0, extension->height())));
        application.processEvents();
        ASSERT_TRUE(menu->isVisible());
        for (QAction *action : overflowed_actions) {
          EXPECT_TRUE(menu->actions().contains(action));
          EXPECT_TRUE(menu->actionGeometry(action).isValid());
          EXPECT_TRUE(menu->rect().contains(menu->actionGeometry(action)));
        }
        if (!review_directory.isEmpty()) {
          EXPECT_TRUE(menu->grab().save(QDir(review_directory).filePath(
              QStringLiteral("selection-toolbar-%1-overflow.png").arg(suffix))));
        }
        send_key(*menu, Qt::Key_Escape);
        EXPECT_FALSE(menu->isVisible());
      } else {
        click_widget(*extension, extension->rect().center());
        application.processEvents();
        for (QAction *action : overflowed_actions) {
          auto *button = qobject_cast<QToolButton *>(toolbar.widgetForAction(action));
          ASSERT_NE(button, nullptr);
          EXPECT_TRUE(retained_buttons.contains(button));
          EXPECT_TRUE(button->isVisible());
        }
        click_widget(*extension, extension->rect().center());
        application.processEvents();
      }
    }
  };
  toolbar.synchronize(editor, quader::QuaderSelectionMode::Object);
  verify_controls();
  auto *object = toolbar.findChild<QAction *>(QStringLiteral("selectionModeMesh"));
  ASSERT_NE(object, nullptr);
  EXPECT_TRUE(object->isChecked());
  EXPECT_TRUE(object->toolTip().contains(QStringLiteral("[4]")));
  ASSERT_NE(object->actionGroup(), nullptr);
  EXPECT_EQ(object->actionGroup()->actions().size(), 4);
  EXPECT_EQ(object->actionGroup()->checkedAction(), object);
  review_layout(QStringLiteral("object-100"), 1.0F, 1000);
  auto *group = toolbar.findChild<QAction *>(QStringLiteral("selectionModeGroup"));
  ASSERT_NE(group, nullptr);
  auto *group_button = qobject_cast<QToolButton *>(toolbar.widgetForAction(group));
  ASSERT_NE(group_button, nullptr);
  ASSERT_TRUE(group_button->isVisible());
  EXPECT_EQ(group->actionGroup(), nullptr);
  EXPECT_FALSE(group->isChecked());
  EXPECT_TRUE(group->toolTip().contains(QStringLiteral("Shift+G")));
  click_widget(*group_button, group_button->rect().center());
  EXPECT_EQ(group_toggle_requests, 1);
  EXPECT_EQ(selection_requests, 0);
  EXPECT_EQ(editor.selection().summary().mode, quader::QuaderSelectionMode::Group);
  EXPECT_TRUE(group->isChecked());
  EXPECT_TRUE(object->isChecked());
  review_layout(QStringLiteral("object-group-selected-100"), 1.0F, 1000);
  click_widget(*group_button, group_button->rect().center());
  EXPECT_EQ(group_toggle_requests, 2);
  EXPECT_EQ(selection_requests, 0);
  EXPECT_EQ(editor.selection().summary().mode, quader::QuaderSelectionMode::Object);
  EXPECT_FALSE(group->isChecked());
  ASSERT_TRUE(editor.preferences().set_shortcut(
      quader::quader_command::kSelectToggleGroupSelection, "Ctrl+Alt+F21").ok());
  toolbar.synchronize(editor, quader::QuaderSelectionMode::Object);
  EXPECT_TRUE(group->toolTip().contains(QStringLiteral("Ctrl+Alt+F21")));
  EXPECT_FALSE(group->toolTip().contains(QStringLiteral("Shift+G")));
  EXPECT_EQ(group_button->accessibleDescription(), group->toolTip());
  review_layout(QStringLiteral("object-200"), 2.0F, 1000);
  review_layout(QStringLiteral("object-narrow-200"), 2.0F, 320);
  review_layout(QStringLiteral("object-narrow-100-down"), 1.0F, 320);
  auto *vertex = toolbar.findChild<QAction *>(QStringLiteral("selectionModeVertex"));
  ASSERT_NE(vertex, nullptr);
  vertex->trigger();
  EXPECT_EQ(selection_requests, 1);
  EXPECT_EQ(editor.selection().summary().mode, quader::QuaderSelectionMode::Vertex);
  EXPECT_EQ(editor.mode().current(), quader::QuaderEditorMode::Edit);
  EXPECT_EQ(editor.mode().state().participants.size(), 2u);
  verify_controls();
  review_layout(QStringLiteral("edit-100"), 1.0F, 1000);
  review_layout(QStringLiteral("edit-200"), 2.0F, 1000);
  review_layout(QStringLiteral("edit-narrow-200"), 2.0F, 320);
  review_layout(QStringLiteral("edit-narrow-100-down"), 1.0F, 320);
  auto *face = toolbar.findChild<QAction *>(QStringLiteral("selectionModeFace"));
  ASSERT_NE(face, nullptr);
  auto *face_button = qobject_cast<QToolButton *>(toolbar.widgetForAction(face));
  ASSERT_NE(face_button, nullptr);
  review_layout(QStringLiteral("edit-before-keyboard-focus"), 1.0F, 1000);
  ASSERT_TRUE(face_button->isVisible());
  face_button->setFocus(Qt::TabFocusReason);
  send_key(*face_button, Qt::Key_Space);
  EXPECT_EQ(editor.selection().summary().mode, quader::QuaderSelectionMode::Face);
  EXPECT_TRUE(face->isChecked());
  EXPECT_EQ(host.focusWidget(), face_button);
  review_layout(QStringLiteral("edit-keyboard-focus"), 1.0F, 1000);
  ASSERT_TRUE(editor.preferences().set_shortcut(
      editor.commands().keybinding_target_id_for_selection_mode(
          quader::QuaderSelectionMode::Face), "Ctrl+Alt+F22").ok());
  toolbar.synchronize(editor, quader::QuaderSelectionMode::Face);
  EXPECT_TRUE(face->toolTip().contains(QStringLiteral("Ctrl+Alt+F22")));
  EXPECT_EQ(face_button->accessibleDescription(), face->toolTip());
  int action_changes = 0;
  for (QAction *action : toolbar.actions()) {
    QObject::connect(action, &QAction::changed, &toolbar,
                     [&] { ++action_changes; });
  }
  const auto selection_before_refresh = editor.selection().summary();
  const auto mode_before_refresh = editor.mode().state();
  const auto undo_count_before_refresh = scene.history().undo_count();
  const auto redo_count_before_refresh = scene.history().redo_count();
  const auto work_before = scene.modeling().modeling_sdk_projection_stats_for_tests();
  for (int frame = 0; frame < 12; ++frame) {
    toolbar.synchronize(editor, quader::QuaderSelectionMode::Face);
  }
  EXPECT_EQ(action_changes, 0);
  const auto work_after = scene.modeling().modeling_sdk_projection_stats_for_tests();
  EXPECT_EQ(editor.selection().summary().selection_revision,
            selection_before_refresh.selection_revision);
  EXPECT_EQ(editor.mode().generation(), mode_before_refresh.generation);
  EXPECT_EQ(editor.mode().revision(), mode_before_refresh.revision);
  EXPECT_EQ(scene.history().undo_count(), undo_count_before_refresh);
  EXPECT_EQ(scene.history().redo_count(), redo_count_before_refresh);
  EXPECT_EQ(work_after.selection_summary_build_count, work_before.selection_summary_build_count);
  EXPECT_EQ(work_after.selection_topology_build_count, work_before.selection_topology_build_count);
  EXPECT_EQ(work_after.native_object_id_scene_scan_count, work_before.native_object_id_scene_scan_count);
  ASSERT_TRUE(localization.select_locale("it-IT"));
  toolbar.set_localization(localization);
  toolbar.synchronize(editor, quader::QuaderSelectionMode::Face);
  int scale_cycle = 0;
  for (const float factor : {1.0F, 2.0F, 1.0F}) {
    review_layout(QStringLiteral("edit-italian-narrow-%1-%2")
                      .arg(qRound(factor * 100)).arg(scale_cycle++),
                  factor, 320);
  }
  toolbar.setLayoutDirection(Qt::RightToLeft);
  review_layout(QStringLiteral("edit-italian-rtl"), 1.0F, 1000);
  review_layout(QStringLiteral("edit-italian-rtl-narrow"), 1.0F, 320);
  toolbar.setLayoutDirection(Qt::LeftToRight);
  host.removeToolBar(&toolbar);
  host.addToolBar(Qt::LeftToolBarArea, &toolbar);
  toolbar.show();
  EXPECT_EQ(toolbar.orientation(), Qt::Vertical);
  review_layout(QStringLiteral("edit-vertical-100"), 1.0F, 320);
  review_layout(QStringLiteral("edit-vertical-200"), 2.0F, 320);
  review_layout(QStringLiteral("edit-vertical-100-down"), 1.0F, 320);
  host.removeToolBar(&toolbar);
  toolbar.setParent(nullptr);
  toolbar.setWindowFlags(Qt::Tool);
  toolbar.setOrientation(Qt::Horizontal);
  toolbar.move(application.primaryScreen()->availableGeometry().topLeft() + QPoint(40, 40));
  toolbar.resize(toolbar.sizeHint());
  toolbar.show();
  EXPECT_TRUE(toolbar.isFloating());
  review_layout(QStringLiteral("edit-floating-100"), 1.0F, 320);
  toolbar.setParent(&host);
  toolbar.setWindowFlags(Qt::Widget);
  host.addToolBar(Qt::TopToolBarArea, &toolbar);
  toolbar.show();
  QPalette light_palette = native_palette;
  light_palette.setColor(QPalette::Window, Qt::white);
  light_palette.setColor(QPalette::WindowText, Qt::black);
  light_palette.setColor(QPalette::Button, QColor(240, 240, 240));
  light_palette.setColor(QPalette::ButtonText, Qt::black);
  light_palette.setColor(QPalette::Base, Qt::white);
  light_palette.setColor(QPalette::Text, Qt::black);
  toolbar.setPalette(light_palette);
  review_layout(QStringLiteral("edit-explicit-light-fallback"), 1.0F, 320);
  QStyleOption light_option;
  light_option.initFrom(&toolbar);
  EXPECT_FALSE(quader::qt_application_style_active(&light_option));
  toolbar.setPalette(QPalette{});
  quader::apply_qt_application_theme_for_contrast_preference(
      native_palette, Qt::ContrastPreference::HighContrast);
  review_layout(QStringLiteral("edit-native-high-contrast"), 1.0F, 1000);
  EXPECT_EQ(application.palette().resolveMask(), 0u);
  object->trigger();
  EXPECT_EQ(editor.selection().summary().mode, quader::QuaderSelectionMode::Object);
  EXPECT_EQ(editor.mode().current(), quader::QuaderEditorMode::Object);
  toolbar.synchronize(editor, quader::QuaderSelectionMode::Object);
  verify_controls();
  EXPECT_FALSE(group->isChecked());
  review_layout(QStringLiteral("object-native-high-contrast"), 1.0F, 1000);
  EXPECT_EQ(editor.selection().summary().selected_object_count, 2u);
  const int object_toolbar_width = toolbar.sizeHint().width();
  quader::SceneSession empty_scene;
  quader::QuaderEditorApi empty_editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(empty_scene));
  ASSERT_TRUE(empty_editor.commands().set_keybinding_storage_path({}).ok());
  toolbar.set_mode_requested_callback([&](quader::QuaderSelectionMode mode) {
    EXPECT_TRUE(empty_editor.selection().set_mode(mode).ok());
    toolbar.synchronize(empty_editor, empty_editor.selection().summary().mode, true);
  });
  toolbar.synchronize(empty_editor, quader::QuaderSelectionMode::Object);
  EXPECT_EQ(toolbar.sizeHint().width(), object_toolbar_width);
  action_changes = 0;
  for (int frame = 0; frame < 12; ++frame) {
    toolbar.synchronize(empty_editor, quader::QuaderSelectionMode::Object);
  }
  EXPECT_EQ(action_changes, 0);
  vertex->trigger();
  EXPECT_EQ(empty_editor.selection().summary().mode, quader::QuaderSelectionMode::Vertex);
  EXPECT_TRUE(vertex->isChecked());
  EXPECT_FALSE(object->isChecked());
  EXPECT_EQ(object->actionGroup()->checkedAction(), vertex);
  EXPECT_TRUE(object->isVisible());
  EXPECT_TRUE(group->isVisible());
  object->trigger();
  EXPECT_EQ(empty_editor.selection().summary().mode, quader::QuaderSelectionMode::Object);
  EXPECT_TRUE(object->isChecked());
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
  host.setAnimated(false);
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
  EXPECT_EQ(expect_persistent_button_faces(toolbar, 30), 0);

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
  const auto mounted_actions = toolbar.actions();
  ASSERT_EQ(mounted_actions.size(), 4);
  EXPECT_EQ(toolbar.widgetForAction(mounted_actions[0]),
            toolbar.findChild<QLabel *>(QStringLiteral("snappingGridSizeLabel")));
  EXPECT_EQ(toolbar.widgetForAction(mounted_actions[1]), grid_combo);
  EXPECT_EQ(toolbar.widgetForAction(mounted_actions[2]),
            toolbar.findChild<QLabel *>(QStringLiteral("snappingAngleLabel")));
  EXPECT_EQ(toolbar.widgetForAction(mounted_actions[3]), angle_combo);
  for (QAction *action : mounted_actions) {
    EXPECT_FALSE(action->isSeparator());
    EXPECT_FALSE(action->isCheckable());
  }
  EXPECT_EQ(grid_combo->minimumHeight(), 30);
  EXPECT_EQ(grid_combo->maximumHeight(), 30);
  EXPECT_EQ(angle_combo->minimumHeight(), 30);
  EXPECT_EQ(angle_combo->maximumHeight(), 30);
  const int baseline_button_margin = toolbar.style()->pixelMetric(
      QStyle::PM_ButtonMargin, nullptr, &toolbar);

  toolbar.apply_ui_scale(quader::QtUiScaleMetrics(2.0F, 1U));
  EXPECT_EQ(toolbar.iconSize(), QSize(64, 64));
  EXPECT_EQ(expect_persistent_button_faces(toolbar, 60), 0);
  EXPECT_EQ(toolbar.style()->pixelMetric(QStyle::PM_ButtonMargin, nullptr,
                                        &toolbar),
            baseline_button_margin * 2);
  EXPECT_EQ(grid_combo->minimumHeight(), 60);
  EXPECT_EQ(grid_combo->maximumHeight(), 60);
  EXPECT_EQ(angle_combo->minimumHeight(), 60);
  EXPECT_EQ(angle_combo->maximumHeight(), 60);
  toolbar.apply_ui_scale(quader::QtUiScaleMetrics(1.0F, 2U));
  EXPECT_EQ(toolbar.iconSize(), QSize(32, 32));
  EXPECT_EQ(expect_persistent_button_faces(toolbar, 30), 0);
  EXPECT_EQ(toolbar.style()->pixelMetric(QStyle::PM_ButtonMargin, nullptr,
                                        &toolbar),
            baseline_button_margin);
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

  const auto viewport_actions = toolbar.viewport_actions();
  for (QAction *action : viewport_actions) {
    ASSERT_NE(action, nullptr);
    EXPECT_EQ(action->parent(), &toolbar);
    EXPECT_TRUE(action->isVisible());
    EXPECT_TRUE(action->isCheckable());
    EXPECT_TRUE(action->shortcuts().isEmpty());
    EXPECT_FALSE(toolbar.actions().contains(action));
    EXPECT_EQ(toolbar.widgetForAction(action), nullptr);
  }
  QAction *transform_space = viewport_actions[0];
  EXPECT_EQ(transform_space->objectName(),
            QStringLiteral("toggle_transform_space"));
  const std::array<QAction *, 4> toggle_actions{
      viewport_actions[1], viewport_actions[2], viewport_actions[3],
      viewport_actions[4]};
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
  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  ASSERT_TRUE(editor.commands()
                  .set_keybinding_storage_path(std::filesystem::path{})
                  .ok());
  constexpr std::array kControls{quader::QuaderViewportControl::SnappingEnabled,
      quader::QuaderViewportControl::GridSnap, quader::QuaderViewportControl::VertexSnap,
      quader::QuaderViewportControl::RotationSnap};
  for (const auto control : kControls) {
    ASSERT_TRUE(editor.viewports().set_control(control, false).ok());
  }
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
  // removeToolBar hides the toolbar; re-adding it does not clear that state.
  toolbar.show();
  application.processEvents();
  ASSERT_TRUE(toolbar.isVisible());
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
  for (std::size_t index = 0; index < kControls.size(); ++index) {
    ASSERT_TRUE(editor.viewports().set_control(kControls[index], owner_toggles[index]).ok());
  }
  ASSERT_TRUE(editor.viewports().set_rotation_snap_degrees(owner_angle).ok());
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
  EXPECT_EQ(toolbar.viewport_actions(), viewport_actions);
  EXPECT_EQ(toolbar.actions(), mounted_actions);

  const QString review_directory = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
  if (!review_directory.isEmpty()) {
    ASSERT_TRUE(QDir().mkpath(review_directory));
    quader::apply_qt_obsidian_application_theme(application.palette());
    quader::QtUiScaleApplicationController scale_controller(application);
    int cycle = 0;
    for (const float factor : {1.0F, 2.0F, 1.0F}) {
      scale_controller.apply_factor(factor);
      const auto &metrics = scale_controller.metrics();
      toolbar.apply_ui_scale(metrics);
      host.resize(metrics.px(600), metrics.px(200));
      // Native window resize delivery can finish after Qt's posted layout pass.
      QEventLoop resize_events;
      QTimer::singleShot(100, &resize_events, &QEventLoop::quit);
      resize_events.exec();
      for (int pass = 0; pass < 3; ++pass) {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        application.processEvents();
        host.layout()->invalidate();
        host.layout()->activate();
        toolbar.layout()->invalidate();
        toolbar.layout()->activate();
      }
      EXPECT_GE(toolbar.width(), metrics.px(600));
      EXPECT_GE(toolbar.height(), metrics.px(30));
      QImage image(toolbar.size(), QImage::Format_ARGB32_Premultiplied);
      image.fill(Qt::transparent);
      toolbar.render(&image);
      EXPECT_TRUE(image.save(QDir(review_directory).filePath(
          QStringLiteral("snapping-fields-toolbar-%1-%2.png")
              .arg(qRound(factor * 100.0F))
              .arg(cycle++))));
    }
  }
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
  EXPECT_EQ(expect_persistent_button_faces(toolbar, 30), 6);
  toolbar.apply_ui_scale(quader::QtUiScaleMetrics(2.0F, 1U));
  EXPECT_EQ(toolbar.iconSize(), QSize(64, 64));
  EXPECT_EQ(expect_persistent_button_faces(toolbar, 60), 6);
  toolbar.apply_ui_scale(quader::QtUiScaleMetrics(1.0F, 2U));
  EXPECT_EQ(toolbar.iconSize(), QSize(32, 32));
  EXPECT_EQ(expect_persistent_button_faces(toolbar, 30), 6);

  std::vector<QAction *> actions;
  for (QAction *action : toolbar.actions()) {
    if (action->isCheckable()) {
      actions.push_back(action);
    }
  }
  ASSERT_EQ(actions.size(), 6u);
  const std::array<QString, 6> expected_names{
      QStringLiteral("viewportWireframeOnShaded"),
      QStringLiteral("viewportBackfaceCulling"),
      QStringLiteral("viewportGrid"),
      QStringLiteral("viewportGizmos"),
      QStringLiteral("viewportMeshGrid"),
      QStringLiteral("viewportOverlays"),
  };
  const std::array<QString, 6> expected_labels{
      QStringLiteral("Show Wireframe"),
      QStringLiteral("Backface Culling"),
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
  bool backface_culling = false;
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
          .backface_culling_enabled = [&] { return backface_culling; },
          .set_backface_culling_enabled = [&](bool value) {
            const bool changed = backface_culling != value;
            backface_culling = value;
            return runtime_result(changed);
          },
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
  constexpr std::array kControls{quader::QuaderViewportControl::WireframeOnShaded,
      quader::QuaderViewportControl::ShowGrid,
      quader::QuaderViewportControl::ShowGizmos, quader::QuaderViewportControl::ShowMeshGrid,
      quader::QuaderViewportControl::ShowOverlays};
  EXPECT_FALSE(editor.viewports().control(quader::QuaderViewportControl::WireframeOnShaded));
  EXPECT_TRUE(editor.commands().shortcut_for(
      quader::quader_command::kViewportToggleWireframeOnShaded).empty());
  EXPECT_TRUE(editor.commands().shortcut_for(
      quader::quader_command::kViewportToggleBackfaceCulling).empty());
  for (const auto control : kControls) {
    ASSERT_TRUE(editor.viewports().set_control(control, false).ok());
  }
  toolbar.synchronize(editor, true);
  ASSERT_TRUE(editor.preferences()
                  .set_shortcut(quader::quader_command::kViewportToggleGrid,
                                "Ctrl+Alt+F12")
                  .ok());
  toolbar.synchronize(editor, true);
  EXPECT_TRUE(
      actions[2]->toolTip().contains(QStringLiteral("[Ctrl+Alt+F12]")));

  const std::array<quader::QuaderCommandId, 6> expected_commands{
      quader::quader_command::kViewportToggleWireframeOnShaded,
      quader::quader_command::kViewportToggleBackfaceCulling,
      quader::quader_command::kViewportToggleGrid,
      quader::quader_command::kViewportToggleGizmos,
      quader::quader_command::kViewportToggleMeshGrid,
      quader::quader_command::kViewportToggleOverlays,
  };
  QMenuBar menu_bar;
  auto viewport_settings = quader::ViewportSettings::defaults();
  quader::QtMainMenu menu;
  menu.initialize(menu_bar, {.editor = editor, .modeling = session.modeling(),
      .viewport_settings = viewport_settings, .localization = localization});
  auto *wireframe_menu = menu_bar.findChild<QAction *>(
      QStringLiteral("command_viewport_toggle_wireframe_on_shaded"));
  ASSERT_NE(wireframe_menu, nullptr);
  menu.refresh();
  EXPECT_TRUE(wireframe_menu->isCheckable());
  EXPECT_FALSE(wireframe_menu->isChecked());
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
    if (index == 1u) {
      EXPECT_TRUE(editor.preferences().backface_culling_enabled());
    } else {
      EXPECT_TRUE(editor.viewports().control(kControls[index > 1u ? index - 1u : index]));
    }
    EXPECT_EQ(owner_state, (std::array<bool, 4>{}));
  }
  ASSERT_EQ(requested_commands.size(), expected_commands.size());
  for (std::size_t index = 0; index < expected_commands.size(); ++index) {
    EXPECT_EQ(requested_commands[index], expected_commands[index].string());
  }
  menu.refresh();
  EXPECT_TRUE(wireframe_menu->isChecked());
  wireframe_menu->trigger();
  menu.refresh();
  toolbar.synchronize(editor);
  EXPECT_FALSE(wireframe_menu->isChecked());
  EXPECT_FALSE(actions.front()->isChecked());
  ASSERT_TRUE(editor.preferences().set_shortcut(
      quader::quader_command::kViewportToggleBackfaceCulling, "Ctrl+Alt+F23").ok());
  toolbar.synchronize(editor);
  EXPECT_TRUE(actions[1]->toolTip().contains(QStringLiteral("[Ctrl+Alt+F23]")));
  EXPECT_TRUE(editor.preferences().set_backface_culling_enabled(false).changed());
  EXPECT_FALSE(editor.preferences().set_backface_culling_enabled(false).changed());
  toolbar.synchronize(editor);
  EXPECT_FALSE(actions[1]->isChecked());
  quader::SceneSession reloaded_session;
  quader::QuaderEditorApi reloaded_editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(reloaded_session));
  ASSERT_TRUE(reloaded_editor.commands().set_keybinding_storage_path(keybindings).ok());
  EXPECT_EQ(reloaded_editor.commands().shortcut_for(
      quader::quader_command::kViewportToggleBackfaceCulling), "Ctrl+Alt+F23");
  std::filesystem::remove(keybindings);

  const QString review_directory = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
  if (!review_directory.isEmpty()) {
    ASSERT_TRUE(QDir().mkpath(review_directory));
    // Load the shipping artwork because this test host has no application QRC.
    const QDir icon_directory(QStringLiteral(QUADER_SOURCE_DIR "/resources/icons"));
    const std::array<const char *, 6> icon_names{
        "viewport_wireframe_on_shaded.png",
        "viewport_backface_culling.png",
        "viewport_grid.png", "viewport_gizmos.png", "viewport_mesh_grid.png",
        "viewport_overlays.png"};
    for (std::size_t index = 0; index < actions.size(); ++index) {
      const QIcon icon = quader::qt_multiresolution_icon(
          icon_directory.filePath(QString::fromUtf8(icon_names[index])));
      ASSERT_FALSE(icon.isNull());
      actions[index]->setIcon(icon);
    }
    quader::apply_qt_obsidian_application_theme(application.palette());
    quader::QtUiScaleApplicationController scale_controller(application);
    toolbar.show();
    int cycle = 0;
    for (const float factor : {1.0F, 2.0F, 1.0F}) {
      scale_controller.apply_factor(factor);
      toolbar.apply_ui_scale(scale_controller.metrics());
      toolbar.resize(toolbar.sizeHint());
      QEventLoop resize_events;
      QTimer::singleShot(100, &resize_events, &QEventLoop::quit);
      resize_events.exec();
      for (int pass = 0; pass < 3; ++pass) {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        application.processEvents();
        toolbar.layout()->invalidate();
        toolbar.layout()->activate();
      }
      QImage image(toolbar.size(), QImage::Format_ARGB32_Premultiplied);
      image.fill(Qt::transparent);
      toolbar.render(&image);
      EXPECT_TRUE(image.save(QDir(review_directory).filePath(
          QStringLiteral("viewport-display-toolbar-%1-%2.png")
              .arg(qRound(factor * 100.0F))
              .arg(cycle++))));
    }
  }
}

TEST(QtProjectWorkspaceTests,
     ViewportStripSharesActionsAndDispatchesEachUserIntentOnce) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-viewport-strip-actions-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  quader::QtSnappingToolbar snapping;
  quader::QtViewportDisplayToolbar display;
  quader::QtViewportControlStrip strip;
  quader::LocalizationService localization;
  ASSERT_TRUE(localization.load(
      std::filesystem::path(QUADER_SOURCE_DIR) / "resources/locales", "en-US"));
  snapping.set_localization(localization);
  display.set_localization(localization);
  const auto snap_actions = snapping.viewport_actions();
  const auto display_actions = display.viewport_actions();
  std::vector<QAction *> actions;
  actions.insert(actions.end(), snap_actions.begin(), snap_actions.end());
  actions.insert(actions.end(), display_actions.begin(), display_actions.end());
  for (QAction *action : actions) {
    ASSERT_NE(action, nullptr);
    strip.add_action(*action);
  }
  const auto button_for = [](QWidget &host, QAction *action) -> QToolButton * {
    for (QToolButton *button : host.findChildren<QToolButton *>()) {
      if (button->defaultAction() == action) {
        return button;
      }
    }
    return nullptr;
  };
  ASSERT_EQ(strip.findChildren<QToolButton *>().size(), 11);
  EXPECT_TRUE(strip.findChildren<QComboBox *>().empty());
  EXPECT_TRUE(strip.findChildren<QDoubleSpinBox *>().empty());
  EXPECT_EQ(snapping.findChildren<QComboBox *>().size(), 2);

  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  std::array<bool, 8> values{};
  std::array<int, 8> preference_writes{};
  bool backface_culling = false;
  int culling_writes = 0;
  const auto set_value = [&](std::size_t index, bool value) {
    ++preference_writes[index];
    const bool changed = values[index] != value;
    values[index] = value;
    return quader::QuaderApiVoidResult{
        .ok = true, .changed = changed,
        .mutation_kind = quader::QuaderMutationKind::Runtime};
  };
  editor.preferences().set_runtime_hooks({
      .snapping_enabled = [&] { return values[0]; },
      .set_snapping_enabled = [&](bool value) { return set_value(0, value); },
      .backface_culling_enabled = [&] { return backface_culling; },
      .set_backface_culling_enabled = [&](bool value) {
        ++culling_writes;
        const bool changed = backface_culling != value;
        backface_culling = value;
        return quader::QuaderApiVoidResult{
            .ok = true, .changed = changed,
            .mutation_kind = quader::QuaderMutationKind::Runtime};
      },
      .grid_visible = [&] { return values[4]; },
      .set_grid_visible = [&](bool value) { return set_value(4, value); },
      .gizmos_visible = [&] { return values[5]; },
      .set_gizmos_visible = [&](bool value) { return set_value(5, value); },
      .mesh_grid_visible = [&] { return values[6]; },
      .set_mesh_grid_visible = [&](bool value) { return set_value(6, value); },
      .overlays_visible = [&] { return values[7]; },
      .set_overlays_visible = [&](bool value) { return set_value(7, value); },
      .grid_snap_enabled = [&] { return values[1]; },
      .set_grid_snap_enabled = [&](bool value) { return set_value(1, value); },
      .vertex_snap_enabled = [&] { return values[2]; },
      .set_vertex_snap_enabled = [&](bool value) { return set_value(2, value); },
      .rotation_snap_enabled = [&] { return values[3]; },
      .set_rotation_snap_enabled = [&](bool value) { return set_value(3, value); },
  });
  constexpr std::array kControls{quader::QuaderViewportControl::SnappingEnabled,
      quader::QuaderViewportControl::GridSnap, quader::QuaderViewportControl::VertexSnap,
      quader::QuaderViewportControl::RotationSnap, quader::QuaderViewportControl::WireframeOnShaded,
      quader::QuaderViewportControl::ShowGrid,
      quader::QuaderViewportControl::ShowGizmos, quader::QuaderViewportControl::ShowMeshGrid,
      quader::QuaderViewportControl::ShowOverlays};
  for (const auto control : kControls) {
    ASSERT_TRUE(editor.viewports().set_control(control, false).ok());
  }
  int transform_requests = 0;
  int display_requests = 0;
  snapping.set_transform_space_toggle_requested_callback([&] {
    ++transform_requests;
    EXPECT_TRUE(editor.commands().execute(
        quader::quader_command::kViewportToggleTransformSpace).ok());
  });
  snapping.set_snapping_enabled_requested_callback([&](bool value) {
    EXPECT_TRUE(editor.viewports().set_control(quader::QuaderViewportControl::SnappingEnabled, value).ok());
  });
  snapping.set_grid_snap_enabled_requested_callback([&](bool value) {
    EXPECT_TRUE(editor.viewports().set_control(quader::QuaderViewportControl::GridSnap, value).ok());
  });
  snapping.set_vertex_snap_enabled_requested_callback([&](bool value) {
    EXPECT_TRUE(editor.viewports().set_control(quader::QuaderViewportControl::VertexSnap, value).ok());
  });
  snapping.set_rotation_snap_enabled_requested_callback([&](bool value) {
    EXPECT_TRUE(editor.viewports().set_control(quader::QuaderViewportControl::RotationSnap, value).ok());
  });
  display.set_command_requested_callback([&](quader::QuaderCommandId command) {
    ++display_requests;
    EXPECT_TRUE(editor.commands().execute(command).ok());
  });
  const auto synchronize = [&] {
    snapping.synchronize(editor);
    display.synchronize(editor);
  };
  synchronize();
  for (std::size_t index = 0; index < actions.size(); ++index) {
    SCOPED_TRACE(index);
    QAction *action = actions[index];
    QToolButton *strip_button = button_for(strip, action);
    QToolButton *top_button = button_for(
        index < snap_actions.size() ? static_cast<QWidget &>(snapping)
                                    : static_cast<QWidget &>(display), action);
    ASSERT_NE(strip_button, nullptr);
    if (index < snap_actions.size()) {
      EXPECT_EQ(top_button, nullptr);
    } else {
      ASSERT_NE(top_button, nullptr);
      EXPECT_NE(strip_button, top_button);
    }
    const bool initial = action->isChecked();
    strip_button->click();
    synchronize();
    EXPECT_EQ(action->isChecked(), !initial);
    EXPECT_EQ(strip_button->isChecked(), !initial);
    if (top_button != nullptr) {
      EXPECT_EQ(top_button->isChecked(), !initial);
      top_button->click();
    } else {
      action->trigger();
    }
    synchronize();
    EXPECT_EQ(action->isChecked(), initial);
    EXPECT_EQ(strip_button->isChecked(), initial);
    if (top_button != nullptr) {
      EXPECT_EQ(top_button->isChecked(), initial);
    }
    EXPECT_EQ(preference_writes, (std::array<int, 8>{}));
    action->setEnabled(false);
    EXPECT_FALSE(strip_button->isEnabled());
    if (top_button != nullptr) {
      EXPECT_FALSE(top_button->isEnabled());
    }
    strip_button->click();
    EXPECT_EQ(action->isChecked(), initial);
    action->setEnabled(true);
  }
  EXPECT_EQ(transform_requests, 2);
  EXPECT_EQ(display_requests, 12);
  EXPECT_EQ(culling_writes, 2);
  EXPECT_EQ(preference_writes, (std::array<int, 8>{}));

  for (const auto control : kControls) {
    ASSERT_TRUE(editor.viewports().set_control(control, true).ok());
  }
  ASSERT_TRUE(editor.viewports().set_transform_space(
      quader::QuaderTransformSpace::Local).ok());
  ASSERT_TRUE(editor.preferences().set_backface_culling_enabled(true).ok());
  synchronize();
  for (std::size_t index = 0; index < actions.size(); ++index) {
    EXPECT_TRUE(button_for(strip, actions[index])->isChecked());
    if (index < snap_actions.size()) {
      EXPECT_EQ(button_for(snapping, actions[index]), nullptr);
    } else {
      EXPECT_TRUE(button_for(display, actions[index])->isChecked());
    }
  }

  const QString english_text = snap_actions[0]->text();
  ASSERT_TRUE(localization.select_locale("it-IT"));
  snapping.set_localization(localization);
  display.set_localization(localization);
  synchronize();
  EXPECT_NE(snap_actions[0]->text(), english_text);
  for (QAction *action : actions) {
    auto *button = button_for(strip, action);
    ASSERT_NE(button, nullptr);
    EXPECT_EQ(button->accessibleName(), action->text());
    EXPECT_EQ(button->accessibleDescription(), action->toolTip());
    EXPECT_EQ(button->toolTip(), action->toolTip());
  }
  int changed_signals = 0;
  for (QAction *action : actions) {
    QObject::connect(action, &QAction::changed, &strip,
                     [&] { ++changed_signals; });
  }
  for (int frame = 0; frame != 12; ++frame) {
    synchronize();
  }
  EXPECT_EQ(changed_signals, 0);
  EXPECT_EQ(transform_requests, 2);
  EXPECT_EQ(display_requests, 12);
  EXPECT_EQ(culling_writes, 3);
  EXPECT_EQ(preference_writes, (std::array<int, 8>{}));
}

TEST(QtProjectWorkspaceTests,
     ViewportStripKeepsNativeButtonsReadableAcrossScaleWidthAndPalette) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-viewport-strip-presentation-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  ASSERT_NE(QApplication::setStyle(QStringLiteral("Fusion")), nullptr);
  const QPalette native_palette = application.palette();
  quader::apply_qt_obsidian_application_theme(native_palette);
  quader::QtUiScaleApplicationController scale_controller(application);
  quader::QtToolToolbar tools;
  quader::QtSnappingToolbar snapping;
  quader::QtViewportDisplayToolbar display;
  quader::QtSceneWorkspace workspace;
  workspace.setAttribute(Qt::WA_DontShowOnScreen, true);
  quader::QtViewportControlStrip strip;
  auto *viewport_container = new QWidget;
  auto *viewport_layout = new QVBoxLayout(viewport_container);
  viewport_layout->setContentsMargins(0, 0, 0, 0);
  viewport_layout->setSpacing(0);
  viewport_layout->addWidget(&strip);
  viewport_layout->addStretch(1);
  workspace.set_viewport_widget(viewport_container);
  ASSERT_TRUE(workspace.set_scene_tabs(
      {{.session_id = 1, .name = QStringLiteral("Courtyard")},
       {.session_id = 2, .name = QStringLiteral("Interior")}}, 1));
  auto *tabs_row = workspace.findChild<QWidget *>(
      QStringLiteral("sceneWorkspace.tabsRow"));
  ASSERT_NE(tabs_row, nullptr);
  std::vector<QAction *> actions;
  for (const auto tool : {quader::ViewportToolId::Select,
                          quader::ViewportToolId::Move,
                          quader::ViewportToolId::Rotate,
                          quader::ViewportToolId::Scale}) {
    QAction *action = tools.tool_action(tool);
    ASSERT_NE(action, nullptr);
    strip.add_action(*action);
    actions.push_back(action);
  }
  for (QAction *action : snapping.viewport_actions()) {
    ASSERT_NE(action, nullptr);
    strip.add_action(*action);
    actions.push_back(action);
    if (action == snapping.viewport_actions().front()) {
      strip.add_separator();
    }
  }
  strip.add_separator();
  for (QAction *action : display.viewport_actions()) {
    ASSERT_NE(action, nullptr);
    strip.add_action(*action);
    actions.push_back(action);
  }
  const QDir icon_directory(QStringLiteral(QUADER_SOURCE_DIR "/resources/icons"));
  const std::array<const char *, 15> icon_names{
      "tool_box_select.png", "tool_move.png", "tool_rotate.png", "tool_scale.png",
      "tool_transform_local.png", "snapping_master.png", "snapping_grid.png",
      "snapping_vertex.png", "snapping_rotation.png", "viewport_wireframe_on_shaded.png",
      "viewport_backface_culling.png",
      "viewport_grid.png",
      "viewport_gizmos.png", "viewport_mesh_grid.png", "viewport_overlays.png"};
  for (std::size_t index = 0; index < actions.size(); ++index) {
    const auto icon = quader::qt_multiresolution_icon(
        icon_directory.filePath(QString::fromUtf8(icon_names[index])));
    ASSERT_FALSE(icon.isNull());
    actions[index]->setIcon(icon);
  }
  actions[0]->setChecked(true);
  actions[5]->setChecked(true);
  actions[9]->setChecked(true);
  actions[10]->setEnabled(false);
  auto *camera = new QPushButton(QStringLiteral("Perspective"), &strip);
  auto *shading = new QPushButton(QStringLiteral("Shaded"), &strip);
  camera->setMenu(new QMenu(camera));
  shading->setMenu(new QMenu(shading));
  camera->menu()->addAction(QStringLiteral("Top"));
  shading->menu()->addAction(QStringLiteral("Wireframe"));
  strip.set_trailing_controls(camera, shading);
  workspace.show();
  const QString review_directory = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
  if (!review_directory.isEmpty()) {
    ASSERT_TRUE(QDir().mkpath(review_directory));
  }
  std::vector<QToolButton *> buttons;
  for (auto *action : actions) {
    auto *button = strip.findChild<QToolButton *>(QStringLiteral("viewportStrip_") + action->objectName());
    ASSERT_NE(button, nullptr);
    buttons.push_back(button);
  }
  const auto settle_layout = [&] {
    for (int pass = 0; pass != 4; ++pass) {
      QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
      application.processEvents();
      workspace.layout()->activate();
      viewport_layout->activate();
      strip.layout()->activate();
    }
  };
  const auto review = [&](const QString &suffix) {
    settle_layout();
    const auto &metrics = scale_controller.metrics();
    const QPoint strip_origin = strip.mapTo(&workspace, QPoint());
    EXPECT_EQ(tabs_row->mapTo(&workspace, tabs_row->rect().bottomLeft()).y() + 1,
              strip_origin.y());
    EXPECT_EQ(strip.width(), workspace.width());
    EXPECT_EQ(strip.height(), metrics.px(36));
    EXPECT_EQ(workspace.findChild<QToolButton *>(
                  QStringLiteral("sceneWorkspace.newScene")), nullptr);
    for (QToolButton *button : buttons) {
      SCOPED_TRACE(button->defaultAction()->objectName().toStdString());
      if (button->isVisible()) {
        EXPECT_TRUE(strip.rect().contains(button->geometry()));
      }
      EXPECT_EQ(button->toolButtonStyle(), Qt::ToolButtonIconOnly);
      EXPECT_EQ(button->focusPolicy(), Qt::StrongFocus);
      EXPECT_FALSE(button->autoRaise());
      EXPECT_TRUE(button->testAttribute(Qt::WA_Hover));
      EXPECT_EQ(button->accessibleName(), button->defaultAction()->text());
      EXPECT_EQ(button->size(), metrics.size(QSize(28, 28)));
      EXPECT_EQ(button->minimumSize(), metrics.size(QSize(28, 28)));
      EXPECT_EQ(button->maximumSize(), metrics.size(QSize(28, 28)));
      EXPECT_EQ(button->iconSize(), metrics.size(QSize(24, 24)));
      if (button->isVisible()) {
        EXPECT_EQ(button->geometry().top(), metrics.px(4));
        EXPECT_EQ(strip.height() - button->geometry().bottom() - 1, metrics.px(4));
      }
      auto *overflow = strip.findChild<QMenu *>(QStringLiteral("viewportStripOverflowMenu"));
      const int overflow_entries = overflow == nullptr ? 0 : overflow->actions().count(button->defaultAction());
      EXPECT_EQ(static_cast<int>(button->isVisible()) + overflow_entries, 1)
          << "each control remains reachable exactly once";
    }
    for (std::size_t index = 1; index < buttons.size(); ++index) {
      if (buttons[index - 1]->isVisible() && buttons[index]->isVisible()) {
        EXPECT_FALSE(buttons[index - 1]->geometry().intersects(buttons[index]->geometry()));
        const QRect previous = buttons[index - 1]->geometry();
        const QRect current = buttons[index]->geometry();
        const int gap = std::max(previous.left(), current.left()) -
                        std::min(previous.right(), current.right()) - 1;
        EXPECT_EQ(gap, metrics.px(index == 5 || index == 9 ? 12 : 2));
      }
    }
    EXPECT_TRUE(camera->isVisible());
    EXPECT_TRUE(shading->isVisible());
    EXPECT_TRUE(strip.rect().contains(camera->geometry()));
    EXPECT_TRUE(strip.rect().contains(shading->geometry()));
    EXPECT_FALSE(camera->geometry().intersects(shading->geometry()));
    if (auto *overflow = strip.findChild<QToolButton *>(QStringLiteral("viewportStripOverflow"));
        overflow != nullptr && overflow->isVisible()) {
      SCOPED_TRACE(suffix.toStdString());
      EXPECT_EQ(overflow->size(), metrics.size(QSize(28, 28)));
      EXPECT_EQ(overflow->minimumSize(), metrics.size(QSize(28, 28)));
      EXPECT_EQ(overflow->maximumSize(), metrics.size(QSize(28, 28)));
      EXPECT_EQ(overflow->geometry().top(), metrics.px(4));
      QStyleOption option;
      option.initFrom(&strip);
      const auto active_group = option.state.testFlag(QStyle::State_Active)
          ? QPalette::Active : QPalette::Inactive;
      for (const auto mode : {QIcon::Normal, QIcon::Disabled}) {
        SCOPED_TRACE(mode == QIcon::Disabled ? "Disabled" : "Normal");
        const QColor expected = strip.palette().color(
            mode == QIcon::Disabled ? QPalette::Disabled : active_group,
            QPalette::ButtonText);
        const QImage glyph = overflow->icon().pixmap(
            overflow->iconSize(), overflow->devicePixelRatioF(), mode).toImage();
        bool readable_pixel = false;
        for (int y = 0; y < glyph.height(); ++y) {
          for (int x = 0; x < glyph.width(); ++x) {
            const QColor pixel = glyph.pixelColor(x, y);
            readable_pixel = readable_pixel ||
                (pixel.alpha() > 128 && std::abs(pixel.red() - expected.red()) <= 2 &&
                 std::abs(pixel.green() - expected.green()) <= 2 &&
                 std::abs(pixel.blue() - expected.blue()) <= 2);
          }
        }
        EXPECT_TRUE(readable_pixel)
            << "overflow glyph must use the live ButtonText role: "
            << expected.name(QColor::HexArgb).toStdString()
            << "; enabled group=" << static_cast<int>(active_group);
      }
    }
    const QRect chrome_rect(0, 0, workspace.width(),
                            strip_origin.y() + strip.height());
    const QImage chrome = workspace.grab(chrome_rect).toImage();
    const auto pixel_at = [&](QPoint point) {
      const qreal ratio = chrome.devicePixelRatio();
      return chrome.pixelColor(
          static_cast<int>((point.x() + 0.5) * ratio),
          static_cast<int>((point.y() + 0.5) * ratio));
    };
    QStyleOption tab_surface;
    tab_surface.palette = workspace.tab_bar().palette();
    if (quader::qt_application_style_active(&tab_surface)) {
      const QRect selected_tab = workspace.tab_bar().tabRect(
          workspace.tab_bar().currentIndex());
      int tab_content_height = workspace.tab_bar().fontMetrics().height();
      for (const auto side : {QTabBar::LeftSide, QTabBar::RightSide}) {
        if (auto *button = workspace.tab_bar().tabButton(
                workspace.tab_bar().currentIndex(), side)) {
          tab_content_height = std::max(tab_content_height, button->height());
        }
      }
      EXPECT_EQ(selected_tab.height(), tab_content_height + metrics.px(4));
      const QPoint selected_bottom = workspace.tab_bar().mapTo(
          &workspace, QPoint(selected_tab.center().x(), selected_tab.bottom()));
      const QColor surface = strip.palette().color(QPalette::AlternateBase);
      EXPECT_EQ(pixel_at(selected_bottom), surface)
          << "the selected tab must meet the strip without an accent underline";
      EXPECT_EQ(pixel_at(QPoint(selected_bottom.x(), strip_origin.y())), surface);
      const int blank_x = workspace.layoutDirection() == Qt::RightToLeft
                              ? metrics.px(4)
                              : tabs_row->width() - metrics.px(4);
      EXPECT_EQ(pixel_at(tabs_row->mapTo(
                    &workspace, QPoint(blank_x, tabs_row->height() / 2))),
                tabs_row->palette().color(QPalette::Window));
      EXPECT_NE(tabs_row->palette().color(QPalette::Window), surface);
    }
    if (!review_directory.isEmpty()) {
      EXPECT_TRUE(chrome.save(QDir(review_directory).filePath(
          QStringLiteral("viewport-strip-%1.png").arg(suffix))));
    }
  };
  QAction *snap_action = actions[5];
  QToolButton *snap_button = buttons[5];
  ASSERT_EQ(snap_action->actionGroup(), nullptr);
  ASSERT_FALSE(snap_button->autoExclusive());
  int snap_triggers = 0;
  QObject::connect(snap_action, &QAction::triggered, &strip,
                   [&] { ++snap_triggers; });
  const auto review_snap_interaction = [&](const QString &suffix) {
    settle_layout();
    ASSERT_TRUE(snap_button->isVisible());
    QApplication::setActiveWindow(&workspace);
    // Directly delivered mouse events do not acquire native window focus.
    // This presentation fixture starts with pointer focus on the toggle;
    // the real pane's action handler still owns any viewport focus handback.
    snap_button->setFocus(Qt::MouseFocusReason);
    ASSERT_TRUE(snap_button->hasFocus());
    snap_action->setChecked(true);
    const int triggers_before = snap_triggers;
    click_widget(*snap_button, snap_button->rect().center());
    EXPECT_EQ(snap_triggers, triggers_before + 1);
    EXPECT_FALSE(snap_action->isChecked());
    EXPECT_FALSE(snap_button->isChecked());
    EXPECT_TRUE(snap_button->hasFocus());
    EXPECT_EQ(snap_button->focusPolicy(), Qt::StrongFocus);
    EXPECT_FALSE(workspace.testAttribute(Qt::WA_KeyboardFocusChange));
    review(suffix + QStringLiteral("-pointer-toggle-off"));

    buttons[0]->setFocus(Qt::TabFocusReason);
    snap_button->setFocus(Qt::ShortcutFocusReason);
    ASSERT_TRUE(snap_button->hasFocus());
    EXPECT_FALSE(snap_button->isChecked());
    EXPECT_TRUE(workspace.testAttribute(Qt::WA_KeyboardFocusChange));
    review(suffix + QStringLiteral("-keyboard-focus"));
    send_key(*snap_button, Qt::Key_Space);
    EXPECT_EQ(snap_triggers, triggers_before + 2);
    EXPECT_TRUE(snap_action->isChecked());
    EXPECT_TRUE(snap_button->isChecked());
    EXPECT_TRUE(workspace.testAttribute(Qt::WA_KeyboardFocusChange));
    click_widget(*snap_button, snap_button->rect().center());
    EXPECT_EQ(snap_triggers, triggers_before + 3);
    EXPECT_FALSE(snap_action->isChecked());
    EXPECT_FALSE(snap_button->isChecked());
    EXPECT_TRUE(snap_button->hasFocus());
    EXPECT_EQ(snap_button->focusPolicy(), Qt::StrongFocus);
    EXPECT_FALSE(workspace.testAttribute(Qt::WA_KeyboardFocusChange));
    review(suffix + QStringLiteral("-keyboard-then-pointer-toggle-off"));
  };
  QSize base_button_size;
  int cycle = 0;
  for (const float factor : {1.0F, 2.0F, 1.0F}) {
    SCOPED_TRACE(factor);
    scale_controller.apply_factor(factor);
    const auto &metrics = scale_controller.metrics();
    workspace.apply_ui_scale(metrics);
    strip.apply_ui_scale(metrics);
    for (QToolButton *button : buttons) {
      button->defaultAction()->setToolTip(
          QStringLiteral("Scale cycle %1").arg(cycle));
      QEvent font_change(QEvent::FontChange);
      QCoreApplication::sendEvent(button, &font_change);
      QEvent style_change(QEvent::StyleChange);
      QCoreApplication::sendEvent(button, &style_change);
    }
    settle_layout();
    workspace.resize(metrics.px(480), metrics.px(180));
    review(QStringLiteral("%1-%2-narrow").arg(qRound(factor * 100)).arg(cycle));
    EXPECT_EQ(strip.width(), metrics.px(480));
    auto *overflow = strip.findChild<QToolButton *>(QStringLiteral("viewportStripOverflow"));
    ASSERT_NE(overflow, nullptr);
    EXPECT_TRUE(overflow->isVisible());
    ASSERT_NE(overflow->menu(), nullptr);
    EXPECT_FALSE(overflow->menu()->actions().empty());
    EXPECT_TRUE(strip.rect().contains(overflow->geometry()));
    ASSERT_TRUE(overflow->menu()->actions().contains(actions.back()));
    const bool overflow_checked = actions.back()->isChecked();
    actions.back()->trigger();
    EXPECT_EQ(actions.back()->isChecked(), !overflow_checked);
    if (cycle == 0) {
      base_button_size = buttons[0]->size();
    } else if (cycle == 1) {
      EXPECT_EQ(buttons[0]->size(), base_button_size * 2);
    } else {
      EXPECT_EQ(buttons[0]->size(), base_button_size);
    }
    workspace.resize(metrics.px(1200), metrics.px(180));
    const QString suffix =
        QStringLiteral("%1-%2").arg(qRound(factor * 100)).arg(cycle++);
    review(suffix + QStringLiteral("-wide"));
    review_snap_interaction(suffix);
  }
  ASSERT_TRUE(workspace.set_scene_tabs(
      {{.session_id = 1, .name = QStringLiteral("Courtyard")}}, 1));
  workspace.resize(1200, 180);
  review(QStringLiteral("single-tab"));
  ASSERT_TRUE(workspace.set_scene_tabs(
      {{.session_id = 1, .name = QStringLiteral("Courtyard")},
       {.session_id = 2, .name = QStringLiteral("Interior")}}, 1));
  workspace.resize(480, 180);
  workspace.setLayoutDirection(Qt::RightToLeft);
  review(QStringLiteral("rtl"));
  EXPECT_GT(buttons[0]->x(), buttons[1]->x());
  workspace.setLayoutDirection(Qt::LeftToRight);
  workspace.layout()->activate();
  strip.layout()->activate();
  application.processEvents();
  QApplication::setActiveWindow(&workspace);
  buttons[0]->setFocus(Qt::TabFocusReason);
  ASSERT_TRUE(buttons[0]->hasFocus());
  send_key(*buttons[0], Qt::Key_Right);
  ASSERT_TRUE(buttons[1]->hasFocus());
  int keyboard_triggers = 0;
  QObject::connect(actions[1], &QAction::triggered, &strip,
                   [&] { ++keyboard_triggers; });
  const bool checked_before_keyboard = actions[1]->isChecked();
  send_key(*buttons[1], Qt::Key_Space);
  EXPECT_EQ(keyboard_triggers, 1);
  EXPECT_EQ(actions[1]->isChecked(), !checked_before_keyboard);
  review(QStringLiteral("keyboard-focus"));
  QStyleOption option;
  option.palette = strip.palette();
  EXPECT_TRUE(quader::qt_application_style_active(&option));
  quader::apply_qt_application_theme_for_contrast_preference(
      native_palette, Qt::ContrastPreference::HighContrast);
  review(QStringLiteral("native-high-contrast"));
  workspace.resize(1200, 180);
  review_snap_interaction(QStringLiteral("native-high-contrast"));
  workspace.resize(480, 180);
  option.palette = strip.palette();
  EXPECT_FALSE(quader::qt_application_style_active(&option));
  EXPECT_EQ(strip.palette().color(QPalette::Window),
            QApplication::palette().color(QPalette::Window));
  QPalette light = native_palette;
  light.setColor(QPalette::Window, Qt::white);
  light.setColor(QPalette::Button, QColor(240, 240, 240));
  light.setColor(QPalette::ButtonText, Qt::black);
  workspace.setPalette(light);
  review(QStringLiteral("explicit-light-palette"));
  option.palette = strip.palette();
  EXPECT_FALSE(quader::qt_application_style_active(&option));
}

TEST(QtProjectWorkspaceTests,
     PaintSelectionMenuRemainsReadableAcrossScaleAndPaletteChanges) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-paint-selection-menu-test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  ASSERT_NE(QApplication::setStyle(QStringLiteral("Fusion")), nullptr);
  const auto native_palette = application.palette();
  quader::apply_qt_obsidian_application_theme(native_palette);
  quader::QtUiScaleApplicationController scale_controller(application);
  quader::SceneSession session;
  quader::QuaderEditorApi editor(quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  quader::LocalizationService localization;
  quader::QtToolToolbar tools;
  tools.set_localization(localization);
  quader::QtViewportPanel panel;
  panel.attach_workspace(session.viewport_workspace());
  panel.set_localization(localization);
  auto *select = tools.tool_action(quader::ViewportToolId::Select);
  auto *lasso = tools.tool_action(quader::ViewportToolId::LassoSelect);
  auto *paint = tools.tool_action(quader::ViewportToolId::PaintSelect);
  ASSERT_NE(select, nullptr);
  ASSERT_NE(lasso, nullptr);
  ASSERT_NE(paint, nullptr);
  const QDir icons(QStringLiteral(QUADER_SOURCE_DIR "/resources/icons"));
  EXPECT_FALSE(select->icon().isNull());
  EXPECT_FALSE(paint->icon().isNull());
  const auto box_artwork = quader::qt_multiresolution_icon(
      icons.filePath(QStringLiteral("tool_box_select.png")));
  const auto paint_artwork = quader::qt_multiresolution_icon(
      icons.filePath(QStringLiteral("tool_paint_select.png")));
  EXPECT_EQ(select->icon().pixmap(QSize(24, 24)).toImage(), box_artwork.pixmap(QSize(24, 24)).toImage());
  EXPECT_EQ(paint->icon().pixmap(QSize(24, 24)).toImage(), paint_artwork.pixmap(QSize(24, 24)).toImage());
  lasso->setIcon(quader::qt_multiresolution_icon(icons.filePath(QStringLiteral("tool_lasso.png"))));
  panel.set_control_actions({select}, lasso, paint);
  tools.set_tool_requested_callback([&](std::string_view id) {
    EXPECT_TRUE(editor.tools().activate(id).ok());
  });
  panel.set_control_action_requested_callback([&](quader::ViewportPaneId, QAction &action) {
    action.trigger();
  });
  tools.synchronize(editor, editor.selection().summary(), true);
  panel.show();
  auto *menu = panel.findChild<QMenu *>(QStringLiteral("viewportSelectionToolMenu_perspective"));
  ASSERT_NE(menu, nullptr);
  ASSERT_EQ(menu->actions().size(), 3);
  auto *paint_choice = menu->actions()[2];
  EXPECT_EQ(paint_choice->text(), QStringLiteral("Paint Select"));
  paint_choice->trigger();
  panel.refresh_control_actions(editor);
  EXPECT_EQ(paint_choice->text(), QStringLiteral("Paint Select\tCtrl+Q"));
  auto *launcher = panel.findChild<QToolButton *>(QStringLiteral("viewportStrip_tool_select"));
  ASSERT_NE(launcher, nullptr);
  EXPECT_TRUE(launcher->isChecked());
  EXPECT_EQ(launcher->accessibleName(), QStringLiteral("Paint Select"));
  EXPECT_FALSE(launcher->icon().isNull());
  const QImage authored_icon = launcher->icon().pixmap(QSize(24, 24), QIcon::Normal).toImage();
  EXPECT_FALSE(authored_icon.isNull());
  EXPECT_EQ(editor.tools().active_tool(), "paint_select");
  const QString review_directory = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
  if (!review_directory.isEmpty()) ASSERT_TRUE(QDir().mkpath(review_directory));
  int cycle = 0;
  for (const float factor : {1.0F, 2.0F, 1.0F}) {
    scale_controller.apply_factor(factor);
    panel.apply_ui_scale(scale_controller.metrics());
    panel.resize(scale_controller.metrics().size(QSize(360, 180)));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    application.processEvents();
    menu->popup(launcher->mapToGlobal(QPoint(0, launcher->height())));
    application.processEvents();
    EXPECT_TRUE(menu->isVisible());
    EXPECT_TRUE(paint_choice->isChecked());
    for (auto *choice : menu->actions()) {
      EXPECT_TRUE(menu->rect().contains(menu->actionGeometry(choice)));
      EXPECT_GE(menu->actionGeometry(choice).height(), menu->fontMetrics().height());
    }
    if (!review_directory.isEmpty()) {
      const auto suffix = QStringLiteral("%1-%2").arg(qRound(factor * 100)).arg(cycle);
      EXPECT_TRUE(menu->grab().save(QDir(review_directory).filePath(
          QStringLiteral("paint-selection-menu-%1.png").arg(suffix))));
      EXPECT_TRUE(launcher->grab().save(QDir(review_directory).filePath(
          QStringLiteral("paint-selection-launcher-%1.png").arg(suffix))));
    }
    menu->close();
    ++cycle;
  }
  QPalette high_contrast = native_palette;
  high_contrast.setColor(QPalette::ButtonText, Qt::yellow);
  const auto old_icon = paint->icon().cacheKey();
  tools.setPalette(high_contrast);
  EXPECT_EQ(paint->icon().cacheKey(), old_icon);
  EXPECT_EQ(launcher->icon().cacheKey(), paint->icon().cacheKey());
  EXPECT_EQ(launcher->icon().pixmap(QSize(24, 24), QIcon::Normal).toImage(), authored_icon);
  if (!review_directory.isEmpty()) {
    panel.setPalette(high_contrast);
    EXPECT_TRUE(launcher->grab().save(QDir(review_directory).filePath(
        QStringLiteral("paint-selection-launcher-high-contrast.png"))));
  }
  QPalette light = native_palette;
  light.setColor(QPalette::Window, Qt::white);
  light.setColor(QPalette::Button, QColor(240, 240, 240));
  light.setColor(QPalette::ButtonText, Qt::black);
  tools.setPalette(light);
  panel.setPalette(light);
  EXPECT_EQ(launcher->icon().pixmap(QSize(24, 24), QIcon::Normal).toImage(), authored_icon);
  if (!review_directory.isEmpty()) {
    EXPECT_TRUE(launcher->grab().save(QDir(review_directory).filePath(
        QStringLiteral("paint-selection-launcher-light.png"))));
  }
}

TEST(QtProjectWorkspaceTests,
     PaneHeadersKeepIndependentControlsWhenToolbarsHideAndLayoutsRestore) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  const auto isolated_profile = std::filesystem::temp_directory_path() /
      ("quader-pane-header-workspace-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  ScopedIsolatedLocalAppData scoped_profile(isolated_profile);
  ASSERT_TRUE(scoped_profile.active());
  int argc = 1;
  char application_name[] = "quader-pane-header-workspace-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  const auto resource_root = std::filesystem::path(QUADER_SOURCE_DIR) / "resources";
  quader::RenderSystem render_system;
  quader::ProjectSession project;
  project.set_resource_root(resource_root / "modeling_assets");
  quader::QuaderEditorApi editor(project);
  quader::UISystem ui;
  ASSERT_TRUE(ui.initialize(render_system, resource_root));
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  auto *window = ui.main_window();
  ASSERT_NE(window, nullptr);
  window->setAttribute(Qt::WA_DontShowOnScreen, true);
  const auto project_root = scoped_profile.profile() / "Pane Controls Project";
  ASSERT_TRUE(std::filesystem::create_directory(project_root));
  ASSERT_TRUE(editor.project().create(project_root).ok());
  ASSERT_NE(project.active_scene(), nullptr);
  auto &scene = *project.active_scene()->session;
  ui.install_runtime_hooks(editor, scene.scene(), scene.assets(), scene.modeling(),
      scene.selection(), scene.native_tools(), scene.point_placement(),
      quader::kInvalidAssetId);
  auto *workspace = dynamic_cast<quader::QtSceneWorkspace *>(
      window->findChild<QWidget *>(QStringLiteral("sceneWorkspace")));
  auto *container = window->findChild<QWidget *>(
      QStringLiteral("sceneViewportContainer"));
  auto *viewport = dynamic_cast<quader::QtViewportPanel *>(
      window->findChild<QWidget *>(QStringLiteral("viewportWorkspace")));
  auto *tools = dynamic_cast<quader::QtToolToolbar *>(
      window->findChild<QToolBar *>(QStringLiteral("toolToolbar")));
  auto *snapping = dynamic_cast<quader::QtSnappingToolbar *>(
      window->findChild<QToolBar *>(QStringLiteral("snappingToolbar")));
  auto *display = dynamic_cast<quader::QtViewportDisplayToolbar *>(
      window->findChild<QToolBar *>(QStringLiteral("viewportDisplaySources")));
  ASSERT_NE(workspace, nullptr);
  ASSERT_NE(container, nullptr);
  ASSERT_NE(viewport, nullptr);
  ASSERT_NE(tools, nullptr);
  ASSERT_NE(snapping, nullptr);
  ASSERT_NE(display, nullptr);
  EXPECT_EQ(window->toolBarArea(display), Qt::NoToolBarArea);
  EXPECT_TRUE(display->isHidden());
  EXPECT_FALSE(display->toggleViewAction()->isVisible());
  EXPECT_EQ(window->findChild<QToolBar *>(QStringLiteral("viewportDisplayToolbar")),
            nullptr);
  viewport->set_startup_presentation_blocked(false);
  EXPECT_EQ(window->findChild<QWidget *>(QStringLiteral("viewportControlStrip")), nullptr);
  EXPECT_EQ(workspace->viewport_widget(), container);
  ASSERT_NE(container->layout(), nullptr);
  EXPECT_EQ(container->layout()->contentsMargins(), QMargins());
  EXPECT_EQ(container->layout()->count(), 1);
  EXPECT_EQ(container->layout()->itemAt(0)->widget(), viewport);
  tools->setEnabled(true);
  const auto synchronize = [&] {
    tools->synchronize(editor, editor.selection().summary(), true);
    snapping->synchronize(editor, true);
    display->synchronize(editor, true);
    viewport->refresh_control_actions(editor);
    viewport->refresh_workspace_layout();
    for (int pass = 0; pass != 5; ++pass) {
      QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
      application.processEvents();
    }
  };
  ASSERT_TRUE(workspace->set_scene_tabs(
      {{.session_id = 1, .name = QStringLiteral("Pane controls")}}, 1));
  auto *stack = qobject_cast<QStackedWidget *>(window->centralWidget());
  ASSERT_NE(stack, nullptr);
  stack->setCurrentWidget(workspace);
  ASSERT_TRUE(editor.viewports().set_layout(quader::ViewportLayoutId::FourQuad).ok());
  window->resize(1800, 1000);
  window->show();
  synchronize();
  EXPECT_EQ(window->toolBarArea(display), Qt::NoToolBarArea);
  EXPECT_TRUE(display->isHidden());
  QByteArray legacy_layout;
  {
    QToolBar obsolete_display(window);
    obsolete_display.setObjectName(QStringLiteral("viewportDisplayToolbar"));
    window->addToolBar(Qt::TopToolBarArea, &obsolete_display);
    legacy_layout = window->saveState();
  }
  ASSERT_TRUE(window->restoreState(legacy_layout));
  synchronize();
  EXPECT_EQ(window->toolBarArea(display), Qt::NoToolBarArea);
  EXPECT_TRUE(display->isHidden());
  EXPECT_EQ(window->findChild<QToolBar *>(QStringLiteral("viewportDisplayToolbar")),
            nullptr);
  {
    std::unique_ptr<QMenu> toolbar_menu(window->createPopupMenu());
    ASSERT_NE(toolbar_menu, nullptr);
    for (const QAction *action : toolbar_menu->actions()) {
      if (action == display->toggleViewAction()) {
        EXPECT_FALSE(action->isVisible());
      }
    }
  }
  const auto saved_layout = window->saveState();
  for (QToolBar *toolbar : window->findChildren<QToolBar *>()) toolbar->hide();
  synchronize();

  constexpr std::array kPaneIds{quader::ViewportPaneId::Perspective,
      quader::ViewportPaneId::Top, quader::ViewportPaneId::Front,
      quader::ViewportPaneId::Side};
  std::array<quader::QtViewportControlStrip *, 4> strips{};
  std::vector<QAction *> source_actions;
  for (const auto tool : {quader::ViewportToolId::Select, quader::ViewportToolId::Move,
                          quader::ViewportToolId::Rotate, quader::ViewportToolId::Scale}) {
    source_actions.push_back(tools->tool_action(tool));
  }
  const auto snap_actions = snapping->viewport_actions();
  source_actions.insert(source_actions.end(), snap_actions.begin(), snap_actions.end());
  const auto display_actions = display->viewport_actions();
  source_actions.insert(source_actions.end(), display_actions.begin(), display_actions.end());
  const auto button = [&](std::size_t pane, const char *name) {
    return strips[pane]->findChild<QToolButton *>(
        QStringLiteral("viewportStrip_") + QString::fromUtf8(name));
  };
  for (std::size_t pane = 0; pane < kPaneIds.size(); ++pane) {
    const QString suffix = QString::fromUtf8(quader::viewport_pane_stable_id(kPaneIds[pane]).data());
    strips[pane] = dynamic_cast<quader::QtViewportControlStrip *>(
        viewport->findChild<QWidget *>(QStringLiteral("viewportToolbar_") + suffix));
    ASSERT_NE(strips[pane], nullptr);
    ASSERT_TRUE(strips[pane]->isVisible());
    EXPECT_EQ(dynamic_cast<QToolBar *>(strips[pane]), nullptr);
    EXPECT_EQ(dynamic_cast<QDockWidget *>(strips[pane]), nullptr);
    for (QAction *source : source_actions) {
      ASSERT_NE(source, nullptr);
      auto *clone = strips[pane]->findChild<QAction *>(source->objectName());
      ASSERT_NE(clone, nullptr);
      EXPECT_NE(clone, source);
      if (pane > 0) EXPECT_NE(clone, strips[0]->findChild<QAction *>(source->objectName()));
      if (source != tools->tool_action(quader::ViewportToolId::Select)) {
        EXPECT_EQ(clone->toolTip(), source->toolTip());
      }
      EXPECT_TRUE(clone->shortcuts().empty());
    }
    auto *selection = button(pane, "tool_select");
    ASSERT_NE(selection, nullptr);
    ASSERT_NE(selection->defaultAction()->menu(), nullptr);
    EXPECT_EQ(selection->popupMode(), QToolButton::DelayedPopup);
    EXPECT_EQ(selection->defaultAction()->menu()->objectName(),
              QStringLiteral("viewportSelectionToolMenu_") + suffix);
    ASSERT_EQ(selection->defaultAction()->menu()->actions().size(), 3);
    EXPECT_EQ(selection->defaultAction()->menu()->actions()[0]->objectName(), QStringLiteral("selectionBox"));
    EXPECT_EQ(selection->defaultAction()->menu()->actions()[1]->objectName(), QStringLiteral("selectionLasso"));
    EXPECT_EQ(selection->defaultAction()->menu()->actions()[2]->objectName(), QStringLiteral("selectionPaint"));
    EXPECT_TRUE(selection->defaultAction()->menu()->actions()[0]->text().startsWith(QStringLiteral("Box Select")));
    EXPECT_TRUE(selection->defaultAction()->menu()->actions()[1]->text().startsWith(QStringLiteral("Lasso")));
    EXPECT_EQ(selection->defaultAction()->menu()->actions()[2]->text(), QStringLiteral("Paint Select\tCtrl+Q"));
    for (QAction *choice : selection->defaultAction()->menu()->actions()) {
      EXPECT_TRUE(choice->shortcuts().empty());
    }
    EXPECT_EQ(editor.viewports().preferred_selection_tool(kPaneIds[pane]),
              quader::ViewportToolId::Select);
    auto *local = button(pane, "toggle_transform_space");
    auto *backface = button(pane, "backfaceSelection");
    auto *wireframe = button(pane, "viewportWireframeOnShaded");
    auto *culling = button(pane, "viewportBackfaceCulling");
    auto *grid = button(pane, "viewportGrid");
    ASSERT_NE(local, nullptr);
    ASSERT_NE(backface, nullptr);
    ASSERT_NE(wireframe, nullptr);
    ASSERT_NE(culling, nullptr);
    ASSERT_NE(grid, nullptr);
    EXPECT_FALSE(wireframe->isChecked());
    EXPECT_TRUE(culling->isChecked());
    EXPECT_EQ(strips[pane]->layout()->indexOf(wireframe) + 1,
              strips[pane]->layout()->indexOf(culling));
    EXPECT_EQ(strips[pane]->layout()->indexOf(culling) + 1,
              strips[pane]->layout()->indexOf(grid));
    EXPECT_EQ(strips[pane]->layout()->indexOf(backface),
              strips[pane]->layout()->indexOf(local) + 1);
    EXPECT_EQ(viewport->findChild<QWidget *>(
                  QStringLiteral("viewportNavigationGizmo_") + suffix), nullptr);
  }

  // Press-and-hold enters the native popup; opening and Escape do not dispatch
  // the launcher's remembered choice. Picking a row dispatches exactly once.
  const auto hold_selection_popup = [&](QToolButton &selection, QAction *choice) {
    QMenu *menu = selection.defaultAction()->menu();
    ASSERT_NE(menu, nullptr);
    bool opened = false;
    QEventLoop events;
    const auto opening = QObject::connect(menu, &QMenu::aboutToShow, &events, [&] {
      QTimer::singleShot(0, &events, [&] {
        opened = menu->isVisible();
        if (choice != nullptr) {
          menu->setActiveAction(choice);
          send_key(*menu, Qt::Key_Return);
        } else {
          send_key(*menu, Qt::Key_Escape);
        }
        menu->close();
        events.quit();
      });
    });
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &events, [&] {
      menu->close();
      events.quit();
    });
    timeout.start(selection.style()->styleHint(
        QStyle::SH_ToolButton_PopupDelay, nullptr, &selection) + 1500);
    send_mouse_event(selection, QEvent::MouseButtonPress, selection.rect().center(),
                     Qt::LeftButton, Qt::LeftButton);
    events.exec();
    timeout.stop();
    QObject::disconnect(opening);
    send_mouse_event(selection, QEvent::MouseButtonRelease, selection.rect().center(),
                     Qt::LeftButton, Qt::NoButton);
    ASSERT_TRUE(opened);
    EXPECT_FALSE(menu->isVisible());
  };
  auto *perspective_select = button(0, "tool_select");
  auto *top_select = button(1, "tool_select");
  const auto origin_before_popup = editor.tools().active_settings_presentation_origin();
  const auto active_revision_before_popup = editor.tools().active_tool_revision();
  const auto command_revision_before_popup = editor.commands().last_status_revision();
  ASSERT_NO_FATAL_FAILURE(hold_selection_popup(*top_select, nullptr));
  EXPECT_EQ(editor.tools().active_settings_presentation_origin(), origin_before_popup);
  EXPECT_EQ(editor.tools().active_tool_revision(), active_revision_before_popup);
  EXPECT_EQ(editor.commands().last_status_revision(), command_revision_before_popup);
  EXPECT_EQ(editor.viewports().preferred_selection_tool(kPaneIds[1]), quader::ViewportToolId::Select);

  auto *lasso_choice = top_select->defaultAction()->menu()->actions()[1];
  int lasso_dispatches = 0;
  QObject::connect(tools->tool_action(quader::ViewportToolId::LassoSelect),
                   &QAction::triggered, top_select, [&] { ++lasso_dispatches; });
  ASSERT_NO_FATAL_FAILURE(hold_selection_popup(*top_select, lasso_choice));
  synchronize();
  EXPECT_EQ(lasso_dispatches, 1);
  EXPECT_EQ(editor.viewports().selected_tool(kPaneIds[1]), quader::ViewportToolId::LassoSelect);
  EXPECT_EQ(editor.viewports().preferred_selection_tool(kPaneIds[1]), quader::ViewportToolId::LassoSelect);
  EXPECT_EQ(editor.viewports().preferred_selection_tool(kPaneIds[0]), quader::ViewportToolId::Select);
  EXPECT_EQ(top_select->icon().cacheKey(), lasso_choice->icon().cacheKey());
  EXPECT_TRUE(top_select->defaultAction()->text().startsWith(QStringLiteral("Lasso")));
  EXPECT_TRUE(top_select->isChecked());
  click_widget(*perspective_select, perspective_select->rect().center());
  synchronize();
  EXPECT_EQ(editor.viewports().selected_tool(kPaneIds[0]), quader::ViewportToolId::Select);
  EXPECT_EQ(editor.viewports().preferred_selection_tool(kPaneIds[1]), quader::ViewportToolId::LassoSelect);
  click_widget(*button(1, "tool_move"), button(1, "tool_move")->rect().center());
  synchronize();
  EXPECT_FALSE(top_select->isChecked());
  click_widget(*top_select, top_select->rect().center());
  synchronize();
  EXPECT_EQ(lasso_dispatches, 2);
  EXPECT_EQ(editor.viewports().selected_tool(kPaneIds[1]), quader::ViewportToolId::LassoSelect);
  EXPECT_EQ(editor.viewports().selected_tool(kPaneIds[0]), quader::ViewportToolId::LassoSelect);
  EXPECT_EQ(editor.viewports().preferred_selection_tool(kPaneIds[0]), quader::ViewportToolId::Select);

  auto *paint_choice = top_select->defaultAction()->menu()->actions()[2];
  int paint_dispatches = 0;
  QObject::connect(tools->tool_action(quader::ViewportToolId::PaintSelect),
                   &QAction::triggered, top_select, [&] { ++paint_dispatches; });
  ASSERT_NO_FATAL_FAILURE(hold_selection_popup(*top_select, paint_choice));
  synchronize();
  EXPECT_EQ(paint_dispatches, 1);
  EXPECT_EQ(editor.viewports().selected_tool(kPaneIds[1]), quader::ViewportToolId::PaintSelect);
  EXPECT_EQ(editor.viewports().preferred_selection_tool(kPaneIds[1]), quader::ViewportToolId::PaintSelect);
  EXPECT_EQ(editor.viewports().preferred_selection_tool(kPaneIds[0]), quader::ViewportToolId::Select);
  EXPECT_EQ(top_select->defaultAction()->text(), QStringLiteral("Paint Select"));
  EXPECT_FALSE(top_select->icon().isNull());
  EXPECT_TRUE(top_select->isChecked());
  click_widget(*button(1, "tool_move"), button(1, "tool_move")->rect().center());
  synchronize();
  EXPECT_FALSE(top_select->isChecked());
  click_widget(*top_select, top_select->rect().center());
  synchronize();
  EXPECT_EQ(paint_dispatches, 2);
  EXPECT_EQ(editor.viewports().selected_tool(kPaneIds[1]), quader::ViewportToolId::PaintSelect);
  EXPECT_EQ(editor.viewports().preferred_selection_tool(kPaneIds[0]), quader::ViewportToolId::Select);

  const bool backfaces = editor.preferences().backface_selection_enabled();
  click_widget(*button(1, "backfaceSelection"), button(1, "backfaceSelection")->rect().center());
  synchronize();
  EXPECT_EQ(editor.preferences().backface_selection_enabled(), !backfaces);
  for (std::size_t pane = 0; pane < kPaneIds.size(); ++pane) {
    EXPECT_EQ(button(pane, "backfaceSelection")->isChecked(), !backfaces);
  }
  click_widget(*button(0, "backfaceSelection"), button(0, "backfaceSelection")->rect().center());
  synchronize();
  EXPECT_EQ(editor.preferences().backface_selection_enabled(), backfaces);

  viewport->setMaximumWidth(viewport->minimumWidth());
  window->resize(1000, 700);
  synchronize();
  auto *selection_overflow = strips[1]->findChild<QToolButton *>(
      QStringLiteral("viewportStripOverflow"));
  ASSERT_NE(selection_overflow, nullptr);
  ASSERT_TRUE(selection_overflow->isVisible());
  ASSERT_TRUE(top_select->isHidden());
  auto *selection_overflow_menu = selection_overflow->menu();
  ASSERT_NE(selection_overflow_menu, nullptr);
  ASSERT_TRUE(selection_overflow_menu->actions().contains(top_select->defaultAction()));
  ASSERT_EQ(top_select->defaultAction()->menu(),
            strips[1]->findChild<QMenu *>(QStringLiteral("viewportSelectionToolMenu_top")));
  const auto overflow_origin = editor.tools().active_settings_presentation_origin();
  const auto overflow_command_revision = editor.commands().last_status_revision();
  bool overflow_opened = false;
  bool selection_submenu_opened = false;
  const auto overflow_opening = QObject::connect(
      selection_overflow_menu, &QMenu::aboutToShow, selection_overflow_menu, [&] {
        QTimer::singleShot(0, selection_overflow_menu, [&] {
          overflow_opened = selection_overflow_menu->isVisible();
          EXPECT_EQ(editor.tools().active_settings_presentation_origin(), overflow_origin);
          EXPECT_EQ(editor.commands().last_status_revision(), overflow_command_revision);
          selection_overflow_menu->setActiveAction(top_select->defaultAction());
          send_key(*selection_overflow_menu, Qt::Key_Right);
        });
      });
  const auto submenu_opening = QObject::connect(
      top_select->defaultAction()->menu(), &QMenu::aboutToShow, top_select->defaultAction()->menu(), [&] {
        QTimer::singleShot(0, top_select->defaultAction()->menu(), [&] {
          selection_submenu_opened = top_select->defaultAction()->menu()->isVisible();
          EXPECT_EQ(editor.tools().active_settings_presentation_origin(), overflow_origin);
          EXPECT_EQ(editor.commands().last_status_revision(), overflow_command_revision);
          top_select->defaultAction()->menu()->setActiveAction(top_select->defaultAction()->menu()->actions()[0]);
          send_key(*top_select->defaultAction()->menu(), Qt::Key_Return);
          top_select->defaultAction()->menu()->close();
          selection_overflow_menu->close();
        });
      });
  QTimer overflow_timeout;
  overflow_timeout.setSingleShot(true);
  QObject::connect(&overflow_timeout, &QTimer::timeout, selection_overflow_menu, [&] {
    top_select->defaultAction()->menu()->close();
    selection_overflow_menu->close();
  });
  overflow_timeout.start(2500);
  click_widget(*selection_overflow, selection_overflow->rect().center());
  overflow_timeout.stop();
  QObject::disconnect(overflow_opening);
  QObject::disconnect(submenu_opening);
  ASSERT_TRUE(overflow_opened);
  ASSERT_TRUE(selection_submenu_opened);
  synchronize();
  EXPECT_EQ(editor.viewports().selected_tool(kPaneIds[1]), quader::ViewportToolId::Select);
  EXPECT_EQ(editor.viewports().preferred_selection_tool(kPaneIds[1]), quader::ViewportToolId::Select);
  EXPECT_EQ(editor.viewports().preferred_selection_tool(kPaneIds[0]), quader::ViewportToolId::Select);
  viewport->setMaximumWidth(QWIDGETSIZE_MAX);
  window->resize(1800, 1000);
  synchronize();
  ASSERT_NO_FATAL_FAILURE(hold_selection_popup(*top_select, lasso_choice));
  synchronize();

  ASSERT_NE(button(0, "tool_move"), nullptr);
  ASSERT_NE(button(1, "tool_rotate"), nullptr);
  ASSERT_TRUE(button(0, "tool_move")->isEnabled());
  ASSERT_TRUE(button(1, "tool_rotate")->isEnabled());
  ASSERT_TRUE(editor.tools().can_activate(quader::ViewportToolId::Move));
  button(0, "tool_move")->click();
  button(1, "tool_rotate")->click();
  synchronize();
  for (std::size_t pane = 0; pane < kPaneIds.size(); ++pane) {
    EXPECT_EQ(editor.viewports().selected_tool(kPaneIds[pane]), quader::ViewportToolId::Rotate);
    EXPECT_FALSE(button(pane, "tool_move")->isChecked());
    EXPECT_TRUE(button(pane, "tool_rotate")->isChecked());
  }
  EXPECT_TRUE(tools->tool_action(quader::ViewportToolId::Rotate)->isChecked());

  const bool base_snapping = editor.preferences().snapping_enabled();
  const bool top_snapping = editor.viewports().control(kPaneIds[1], quader::QuaderViewportControl::SnappingEnabled);
  button(0, "snappingEnabled")->click();
  synchronize();
  EXPECT_EQ(editor.viewports().focused_pane(), kPaneIds[0]);
  EXPECT_EQ(editor.viewports().control(kPaneIds[0], quader::QuaderViewportControl::SnappingEnabled), !base_snapping);
  EXPECT_EQ(editor.viewports().control(kPaneIds[1], quader::QuaderViewportControl::SnappingEnabled), top_snapping);
  EXPECT_EQ(editor.preferences().snapping_enabled(), base_snapping);
  const bool top_grid = editor.viewports().control(kPaneIds[1], quader::QuaderViewportControl::ShowGrid);
  button(1, "viewportWireframeOnShaded")->click();
  synchronize();
  EXPECT_TRUE(editor.viewports().control(kPaneIds[1], quader::QuaderViewportControl::WireframeOnShaded));
  EXPECT_TRUE(button(1, "viewportWireframeOnShaded")->isChecked());
  EXPECT_FALSE(editor.viewports().control(kPaneIds[0], quader::QuaderViewportControl::WireframeOnShaded));
  EXPECT_FALSE(button(0, "viewportWireframeOnShaded")->isChecked());
  const auto history_before_culling = editor.history().snapshot();
  const auto selection_before_culling = editor.selection().summary();
  const bool selection_backfaces = editor.preferences().backface_selection_enabled();
  button(1, "viewportBackfaceCulling")->click();
  synchronize();
  EXPECT_FALSE(editor.preferences().backface_culling_enabled());
  EXPECT_EQ(editor.preferences().backface_selection_enabled(), selection_backfaces);
  for (std::size_t pane = 0; pane < kPaneIds.size(); ++pane) {
    EXPECT_FALSE(button(pane, "viewportBackfaceCulling")->isChecked());
  }
  button(0, "viewportBackfaceCulling")->click();
  synchronize();
  EXPECT_TRUE(editor.preferences().backface_culling_enabled());
  for (std::size_t pane = 0; pane < kPaneIds.size(); ++pane) {
    EXPECT_TRUE(button(pane, "viewportBackfaceCulling")->isChecked());
  }
  EXPECT_EQ(editor.history().snapshot().undo_label, history_before_culling.undo_label);
  EXPECT_EQ(editor.history().snapshot().redo_label, history_before_culling.redo_label);
  EXPECT_EQ(editor.selection().summary().selection_revision,
            selection_before_culling.selection_revision);
  button(1, "viewportGrid")->click();
  button(1, "toggle_transform_space")->click();
  synchronize();
  EXPECT_EQ(editor.viewports().control(kPaneIds[1], quader::QuaderViewportControl::ShowGrid), !top_grid);
  EXPECT_EQ(editor.viewports().transform_space(kPaneIds[1]), quader::QuaderTransformSpace::Local);
  EXPECT_EQ(editor.viewports().transform_space(kPaneIds[0]), quader::QuaderTransformSpace::Global);
  tools->tool_action(quader::ViewportToolId::Scale)->trigger();
  synchronize();
  EXPECT_EQ(editor.viewports().selected_tool(kPaneIds[1]), quader::ViewportToolId::Scale);
  EXPECT_EQ(editor.viewports().selected_tool(kPaneIds[0]), quader::ViewportToolId::Scale);

  ASSERT_TRUE(editor.preferences().set_shortcut(
      quader::quader_command::kToolsActivateMove, "Ctrl+Alt+F23").ok());
  synchronize();
  for (auto *strip : strips) {
    auto *move = strip->findChild<QToolButton *>(QStringLiteral("viewportStrip_tool_move"));
    EXPECT_TRUE(move->toolTip().contains(QStringLiteral("[Ctrl+Alt+F23]")));
    EXPECT_EQ(move->accessibleDescription(), tools->tool_action(quader::ViewportToolId::Move)->toolTip());
  }
  const auto executing_tool = editor.tools().active_tool();
  const auto active_tool_revision = editor.tools().active_tool_revision();
  const auto undo_count = scene.history().undo_count();
  auto *perspective_surface = viewport->surface(kPaneIds[0]);
  ASSERT_NE(perspective_surface, nullptr);
  const QPoint local(20, 20);
  const QPoint global = perspective_surface->mapToGlobal(local);
  QMouseEvent press(QEvent::MouseButtonPress, QPointF(local), QPointF(global),
                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  QCoreApplication::sendEvent(perspective_surface, &press);
  QMouseEvent release(QEvent::MouseButtonRelease, QPointF(local), QPointF(global),
                      Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  QCoreApplication::sendEvent(perspective_surface, &release);
  synchronize();
  EXPECT_EQ(editor.viewports().focused_pane(), kPaneIds[0]);
  EXPECT_EQ(editor.tools().active_tool(), executing_tool);
  EXPECT_EQ(editor.tools().active_tool_revision(), active_tool_revision);
  EXPECT_EQ(scene.history().undo_count(), undo_count);
  EXPECT_TRUE(tools->tool_action(quader::ViewportToolId::Scale)->isChecked());

  window->resize(1000, 700);
  synchronize();
  auto *overflow_menu = strips[1]->findChild<QMenu *>(QStringLiteral("viewportStripOverflowMenu"));
  ASSERT_NE(overflow_menu, nullptr);
  auto *overflow_grid = strips[1]->findChild<QAction *>(QStringLiteral("viewportGrid"));
  ASSERT_NE(overflow_grid, nullptr);
  ASSERT_TRUE(overflow_menu->actions().contains(overflow_grid));
  const bool perspective_grid = editor.viewports().control(kPaneIds[0], quader::QuaderViewportControl::ShowGrid);
  const bool top_grid_before_overflow = editor.viewports().control(kPaneIds[1], quader::QuaderViewportControl::ShowGrid);
  overflow_grid->trigger();
  synchronize();
  EXPECT_EQ(editor.viewports().focused_pane(), kPaneIds[1]);
  EXPECT_EQ(editor.viewports().control(kPaneIds[1], quader::QuaderViewportControl::ShowGrid), !top_grid_before_overflow);
  EXPECT_EQ(editor.viewports().control(kPaneIds[0], quader::QuaderViewportControl::ShowGrid), perspective_grid);
  window->resize(1800, 1000);
  synchronize();
  int clone_changes = 0;
  for (auto *strip : strips) {
    for (auto *action : strip->findChildren<QAction *>()) {
      QObject::connect(action, &QAction::changed, strip, [&] { ++clone_changes; });
    }
  }
  for (int pass = 0; pass < 8; ++pass) viewport->refresh_control_actions(editor);
  EXPECT_EQ(clone_changes, 0);

  for (const auto layout : {quader::ViewportLayoutId::Single,
                            quader::ViewportLayoutId::FourQuad,
                            quader::ViewportLayoutId::TwoVertical}) {
    ASSERT_TRUE(editor.viewports().set_layout(layout).ok());
    synchronize();
    for (std::size_t pane = 0; pane < kPaneIds.size(); ++pane) {
      if (!strips[pane]->isVisible()) continue;
      auto *surface_container = viewport->findChild<QWidget *>(
          QStringLiteral("viewportSurfaceContainer_") +
          QString::fromUtf8(quader::viewport_pane_stable_id(kPaneIds[pane]).data()));
      ASSERT_NE(surface_container, nullptr);
      EXPECT_EQ(strips[pane]->x(), 0);
      EXPECT_EQ(strips[pane]->y(), 0);
      EXPECT_EQ(strips[pane]->width(), strips[pane]->parentWidget()->width());
      EXPECT_EQ(surface_container->mapToGlobal(QPoint()).y(),
                strips[pane]->mapToGlobal(QPoint(0, strips[pane]->height())).y());
      EXPECT_LT(workspace->tab_bar().mapToGlobal(workspace->tab_bar().rect().bottomLeft()).y(),
                strips[pane]->mapToGlobal(QPoint()).y());
    }
  }
  ASSERT_TRUE(window->restoreState(saved_layout));
  synchronize();
  EXPECT_EQ(window->toolBarArea(display), Qt::NoToolBarArea);
  EXPECT_TRUE(display->isHidden());
  for (const auto pane : kPaneIds) {
    EXPECT_EQ(editor.viewports().selected_tool(pane), quader::ViewportToolId::Scale);
  }
  EXPECT_NE(snapping->findChild<QComboBox *>(QStringLiteral("snappingGridSizeCombo")), nullptr);
  EXPECT_NE(snapping->findChild<QComboBox *>(QStringLiteral("snappingAngleCombo")), nullptr);
  ui.shutdown();
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
     LoadingCoverRetainsLogoAndSettlesHomeAcrossRepeatedLoads) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  const auto profile = std::filesystem::temp_directory_path() /
      ("quader-loading-cover-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  ScopedIsolatedLocalAppData scoped_profile(profile);
  ASSERT_TRUE(scoped_profile.active());
  int argc = 1;
  char application_name[] = "quader-loading-cover-test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  ASSERT_NE(QApplication::setStyle(QStringLiteral("Fusion")), nullptr);
  const QPalette native_palette = application.palette();
  quader::apply_qt_obsidian_application_theme(native_palette);
  application.setWindowIcon(quader::qt_multiresolution_icon(
      QStringLiteral(QUADER_SOURCE_DIR "/resources/icons/quader_256.png")));
  const auto resources = std::filesystem::path(QUADER_SOURCE_DIR) / "resources";
  quader::RenderSystem renderer;
  quader::ProjectSession project;
  project.set_resource_root(resources / "modeling_assets");
  quader::QuaderEditorApi editor(project);
  quader::UISystem ui;
  ASSERT_TRUE(ui.initialize(renderer, resources));
  auto *window = ui.main_window();
  ASSERT_NE(window, nullptr);
  window->resize(1000, 740);
  const QSize saved_normal_extent = window->size();
  window->setWindowState(window->windowState() | Qt::WindowMaximized);
  ui.show();
  EXPECT_EQ(window->normalGeometry().size(), saved_normal_extent);
  auto *cover = window->findChild<QWidget *>(QStringLiteral("quaderLoadingCover"));
  ASSERT_NE(cover, nullptr);
  EXPECT_TRUE(window->updatesEnabled());
  EXPECT_TRUE(ui.loading_presentation_active());
  EXPECT_TRUE(cover->isVisible());
  EXPECT_EQ(cover->geometry(), window->rect());
  for (int frame = 0; frame < 8; ++frame) {
    application.processEvents();
    ui.refresh({}, {});
  }
  EXPECT_TRUE(ui.loading_presentation_active())
      << "the first reveal must wait for the editor binding";

  auto &scene = project.fallback_scene();
  ui.install_runtime_hooks(editor, scene.scene(), scene.assets(), scene.modeling(),
      scene.selection(), scene.native_tools(), scene.point_placement(),
      quader::kInvalidAssetId);
  ASSERT_TRUE(window->isMaximized());
  ASSERT_TRUE(quader::platform_window_is_maximized(ui.native_window_handle()));
  EXPECT_EQ(window->normalGeometry().size(), saved_normal_extent);
  // Restore through the native title-bar command while the initial cover is
  // still held. Readiness must follow the new state, not the original maximize.
#if defined(_WIN32)
  ASSERT_NE(PostMessageW(static_cast<HWND>(ui.native_window_handle()),
                        WM_SYSCOMMAND, SC_RESTORE, 0), FALSE);
#else
  window->showNormal();
#endif
  QElapsedTimer restore_deadline;
  restore_deadline.start();
  while ((window->isMaximized() ||
          quader::platform_window_is_maximized(ui.native_window_handle())) &&
         restore_deadline.elapsed() < 1000) {
    application.processEvents();
    ui.refresh({}, {});
  }
  ASSERT_FALSE(window->isMaximized());
  ASSERT_FALSE(quader::platform_window_is_maximized(ui.native_window_handle()));
  EXPECT_EQ(window->size(), saved_normal_extent);
  EXPECT_EQ(window->normalGeometry().size(), saved_normal_extent);
  EXPECT_TRUE(ui.loading_presentation_active());
  EXPECT_TRUE(cover->isVisible());
  const QSize restored_extent = window->size();
  const auto settle_home = [&] {
    QElapsedTimer deadline;
    deadline.start();
    while (ui.loading_presentation_active() && deadline.elapsed() < 3000) {
      application.processEvents();
      ui.refresh({}, {});
    }
    EXPECT_FALSE(ui.loading_presentation_active());
    EXPECT_TRUE(cover->isHidden());
    EXPECT_TRUE(window->updatesEnabled());
  };
  settle_home();
  EXPECT_EQ(window->size(), restored_extent);
  const auto native_extent = quader::platform_window_client_extent(ui.native_window_handle());
  EXPECT_TRUE(quader::qt_startup_client_extents_match(
      qRound(window->width() * window->devicePixelRatioF()),
      qRound(window->height() * window->devicePixelRatioF()),
      native_extent.width, native_extent.height));
  auto *stack = window->findChild<QStackedWidget *>(QStringLiteral("mainCentralStack"));
  ASSERT_NE(stack, nullptr);
  EXPECT_NE(dynamic_cast<quader::QtProjectHomePage *>(stack->currentWidget()), nullptr);
  std::vector<std::pair<QWidget *, QRect>> revealed_home_geometry;
  for (const QString &name : {
           QStringLiteral("projectHome.title"),
           QStringLiteral("projectHome.recentProjects.frame"),
           QStringLiteral("projectHome.tutorial.card"),
           QStringLiteral("projectHome.documentation.card")}) {
    auto *widget = window->findChild<QWidget *>(name);
    ASSERT_NE(widget, nullptr);
    ASSERT_TRUE(widget->geometry().isValid());
    revealed_home_geometry.emplace_back(widget, widget->geometry());
  }
  for (int frame = 0; frame < 8; ++frame) {
    application.processEvents();
    ui.refresh({}, {});
    for (const auto &[widget, geometry] : revealed_home_geometry) {
      EXPECT_EQ(widget->geometry(), geometry)
          << widget->objectName().toStdString()
          << " must reach its final layout before Home is revealed";
    }
  }
  class CoverEventProbe final : public QObject {
  public:
    int transitions = 0;
  protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
      if (event->type() == QEvent::Show || event->type() == QEvent::Hide ||
          event->type() == QEvent::Resize || event->type() == QEvent::Move) {
        ++transitions;
      }
      return QObject::eventFilter(watched, event);
    }
  } probe;
  cover->installEventFilter(&probe);
  const QString review = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
  if (!review.isEmpty()) ASSERT_TRUE(QDir().mkpath(review));
  const QImage authored_logo(QStringLiteral(
      QUADER_SOURCE_DIR "/resources/icons/quader_256.png"));
  ASSERT_FALSE(authored_logo.isNull());
  QRect authored_alpha_bounds;
  for (int y = 0; y < authored_logo.height(); ++y) {
    for (int x = 0; x < authored_logo.width(); ++x) {
      if (authored_logo.pixelColor(x, y).alpha() != 0) {
        authored_alpha_bounds |= QRect(x, y, 1, 1);
      }
    }
  }
  ASSERT_TRUE(authored_alpha_bounds.isValid());
  QSize painted_logo_size;
  const auto expect_logo_paint = [&](const QString &name) {
    EXPECT_TRUE(ui.loading_presentation_active());
    EXPECT_TRUE(cover->isVisible());
    EXPECT_EQ(cover->geometry(), window->rect());
    const QImage image = cover->grab().toImage();
    ASSERT_FALSE(image.isNull());
    const QColor background = cover->palette().color(QPalette::Window);
    EXPECT_EQ(image.pixelColor(0, 0), background);
    EXPECT_EQ(image.pixelColor(image.width() - 1, image.height() - 1), background);
    QRect logo_bounds;
    for (int y = 0; y < image.height(); ++y) {
      for (int x = 0; x < image.width(); ++x) {
        if (image.pixelColor(x, y) != background) {
          logo_bounds |= QRect(x, y, 1, 1);
        }
      }
    }
    EXPECT_TRUE(logo_bounds.isValid()) << "the loading cover must paint the logo";
    // Center the authored canvas. The Q tail makes its alpha bounds asymmetric
    // (19..245 horizontally in the 256px asset), so visible pixels sit right
    // of the canvas center; their expected offset scales with the artwork.
    const QPointF authored_offset = QRectF(authored_alpha_bounds).center() -
                                    QRectF(authored_logo.rect()).center();
    const QPointF expected_center = QRectF(image.rect()).center() + QPointF(
        authored_offset.x() * logo_bounds.width() / authored_alpha_bounds.width(),
        authored_offset.y() * logo_bounds.height() / authored_alpha_bounds.height());
    EXPECT_NEAR(QRectF(logo_bounds).center().x(), expected_center.x(), 1);
    EXPECT_NEAR(QRectF(logo_bounds).center().y(), expected_center.y(), 1);
    EXPECT_LT(logo_bounds.width(), image.width() / 2);
    EXPECT_LT(logo_bounds.height(), image.height() / 2);
    painted_logo_size = logo_bounds.size();
    if (!review.isEmpty()) EXPECT_TRUE(image.save(QDir(review).filePath(name)));
  };
  int cycle = 0;
  QSize baseline_logo_size;
  for (const double factor : {1.0, 2.0, 1.0}) {
    ASSERT_TRUE(editor.preferences().set_interface_scale(factor).ok());
    ui.refresh({}, {});
    ui.begin_loading_presentation();
    application.processEvents();
    expect_logo_paint(QStringLiteral("loading-cover-%1-%2.png")
                         .arg(qRound(factor * 100)).arg(cycle++));
    if (cycle == 1) {
      baseline_logo_size = painted_logo_size;
    } else if (factor == 2.0) {
      EXPECT_GT(painted_logo_size.width(), baseline_logo_size.width());
      EXPECT_GT(painted_logo_size.height(), baseline_logo_size.height());
    } else {
      EXPECT_EQ(painted_logo_size, baseline_logo_size);
    }
    const int stable_transitions = probe.transitions;
    for (int frame = 0; frame < 8; ++frame) {
      ui.refresh({}, {});
      application.processEvents();
    }
    EXPECT_TRUE(ui.loading_presentation_active())
        << "stable geometry cannot finish an operation still in progress";
    EXPECT_EQ(probe.transitions, stable_transitions);
    ui.end_loading_presentation(true);
    EXPECT_TRUE(ui.loading_presentation_active())
        << "success must wait for the resulting layout";
    settle_home();
    const int completed_transitions = probe.transitions;
    for (int frame = 0; frame < 8; ++frame) {
      ui.refresh({}, {});
      application.processEvents();
    }
    EXPECT_EQ(probe.transitions, completed_transitions);
  }
  window->resize(660, 740);
  ui.begin_loading_presentation();
  application.processEvents();
  expect_logo_paint(QStringLiteral("loading-cover-narrow.png"));
  ui.end_loading_presentation(false);
  EXPECT_FALSE(ui.loading_presentation_active());
  EXPECT_TRUE(cover->isHidden());
  EXPECT_NE(dynamic_cast<quader::QtProjectHomePage *>(stack->currentWidget()), nullptr);
  ui.begin_loading_presentation();
  ui.begin_loading_presentation();
  ui.end_loading_presentation(true);
  EXPECT_TRUE(ui.loading_presentation_active());
  ui.end_loading_presentation(false);
  EXPECT_FALSE(ui.loading_presentation_active());
  ui.end_loading_presentation(true);
  EXPECT_FALSE(ui.loading_presentation_active());
  {
    QLineEdit draft(stack->currentWidget());
    draft.setGeometry(20, 20, 180, 32);
    draft.show();
    window->activateWindow();
    QApplication::setActiveWindow(window);
    draft.setFocus(Qt::OtherFocusReason);
    application.processEvents();
    ASSERT_EQ(QApplication::focusWidget(), &draft);
    draft.setText(QStringLiteral("Uncommitted draft"));
    draft.setModified(true);
    int edits_finished = 0;
    QObject::connect(&draft, &QLineEdit::editingFinished,
                     &draft, [&] { ++edits_finished; });
    ui.begin_loading_presentation();
    EXPECT_EQ(QApplication::focusWidget(), &draft);
    EXPECT_EQ(edits_finished, 0);
    send_key(draft, Qt::Key_Return);
    EXPECT_EQ(edits_finished, 0) << "covered input must not commit a retained draft";
    ui.end_loading_presentation(false);
    EXPECT_EQ(QApplication::focusWidget(), &draft);
    EXPECT_EQ(edits_finished, 0);
    EXPECT_EQ(draft.text(), QStringLiteral("Uncommitted draft"));
    EXPECT_TRUE(draft.isModified());
  }

  quader::apply_qt_application_theme_for_contrast_preference(
      native_palette, Qt::ContrastPreference::HighContrast);
  ui.begin_loading_presentation();
  application.processEvents();
  expect_logo_paint(QStringLiteral("loading-cover-native-palette.png"));
  ui.end_loading_presentation(false);
  EXPECT_TRUE(cover->isHidden());
  EXPECT_EQ(window->findChildren<QWidget *>(QStringLiteral("quaderLoadingCover")).size(), 1);
  cover->removeEventFilter(&probe);
  quader::apply_qt_application_theme_for_contrast_preference(
      native_palette, Qt::ContrastPreference::NoPreference);
  ui.shutdown();
}

TEST(QtProjectWorkspaceTests,
     HomeLayoutKeepsCardsReadableAcrossScaleWidthAndNativePalettes) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader_project_home_presentation_test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  ASSERT_NE(QApplication::setStyle(QStringLiteral("Fusion")), nullptr);
  const QPalette native_palette = application.palette();
  quader::apply_qt_obsidian_application_theme(native_palette);
  quader::QtUiScaleApplicationController scale_controller(application);
  const QIcon application_icon = quader::qt_multiresolution_icon(
      QStringLiteral(QUADER_SOURCE_DIR "/resources/icons/quader_256.png"));
  ASSERT_FALSE(application_icon.isNull());
  QApplication::setWindowIcon(application_icon);
  quader::QtProjectHomePage home;
  const std::vector<quader::QtRecentProjectItem> projects{
      {.name = QStringLiteral("Courtyard"),
       .manifest_path = QStringLiteral("C:/Projects/Courtyard/Courtyard.qproj"),
       .manifest_last_modified_unix_ms = 1790250000000,
       .last_saved_app_version = QStringLiteral("0.1.42")},
      {.name = QStringLiteral("Industrial passage"),
       .manifest_path = QStringLiteral(
           "C:/Projects/Experiments/Environment blockouts/Industrial passage/"
           "Industrial passage.qproj"),
       .manifest_last_modified_unix_ms = 1790163600000,
       .last_saved_app_version = QStringLiteral("0.1.67")},
      {.name = QStringLiteral("Lighting studies"),
       .manifest_path = QStringLiteral(
           "C:/Projects/Lighting studies/Lighting studies.qproj"),
       .manifest_last_modified_unix_ms = 1789990800000,
       .last_saved_app_version = QStringLiteral("0.1.5")},
      {.name = QStringLiteral("Archived prototype"),
       .manifest_path = QStringLiteral("D:/Archive/Prototype/Prototype.qproj"),
       .available = false},
  };
  home.set_recent_projects(projects);
  home.resize(1200, 740);
  home.show();
  auto *scroll = home.findChild<QScrollArea *>(
      QStringLiteral("projectHome.scroll"));
  auto *table = home.findChild<QTableView *>(
      QStringLiteral("projectHome.recentProjects.view"));
  auto *relative_time_timer = home.findChild<QTimer *>(
      QStringLiteral("projectHome.recentProjects.relativeTimeTimer"));
  auto *recent_frame = home.findChild<QFrame *>(
      QStringLiteral("projectHome.recentProjects.frame"));
  auto *title = home.findChild<QLabel *>(QStringLiteral("projectHome.title"));
  auto *logo = home.findChild<QLabel *>(QStringLiteral("projectHome.logo"));
  auto *new_project = home.findChild<QPushButton *>(
      QStringLiteral("projectHome.newProject"));
  auto *open_project = home.findChild<QPushButton *>(
      QStringLiteral("projectHome.openProject"));
  auto *tutorial = home.findChild<QWidget *>(
      QStringLiteral("projectHome.tutorial.card"));
  auto *documentation = home.findChild<QWidget *>(
      QStringLiteral("projectHome.documentation.card"));
  ASSERT_NE(scroll, nullptr);
  ASSERT_NE(scroll->widget(), nullptr);
  ASSERT_NE(table, nullptr);
  ASSERT_NE(relative_time_timer, nullptr);
  EXPECT_TRUE(relative_time_timer->isActive());
  ASSERT_NE(recent_frame, nullptr);
  ASSERT_NE(title, nullptr);
  ASSERT_NE(logo, nullptr);
  ASSERT_NE(new_project, nullptr);
  ASSERT_NE(open_project, nullptr);
  ASSERT_NE(tutorial, nullptr);
  ASSERT_NE(documentation, nullptr);
  EXPECT_EQ(new_project->text(), QStringLiteral("New project"));
  EXPECT_EQ(open_project->text(), QStringLiteral("Open project\u2026"));
  EXPECT_TRUE(new_project->isDefault());
  EXPECT_FALSE(QImage(QStringLiteral(":/quader/home/tutorial.png")).isNull());
  EXPECT_FALSE(QImage(QStringLiteral(":/quader/home/documentation.png")).isNull());
  const QString review_directory = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
  if (!review_directory.isEmpty()) {
    ASSERT_TRUE(QDir().mkpath(review_directory));
  }
  const auto settle_layout = [&]() {
    for (int pass = 0; pass < 4; ++pass) {
      QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
      application.processEvents();
      home.layout()->activate();
      scroll->widget()->layout()->activate();
    }
  };
  const auto render_project_name = [&](const QString &manifest_path) {
    home.set_recent_projects({{
        .name = QStringLiteral("Same project name"),
        .manifest_path = manifest_path,
    }});
    QStyleOptionViewItem option;
    option.initFrom(table);
    option.rect = QRect(0, 0, 480,
                        table->verticalHeader()->defaultSectionSize());
    option.widget = table;
    option.state = QStyle::State_Enabled | QStyle::State_Active;
    option.font = table->font();
    option.fontMetrics = QFontMetrics(option.font);
    QImage rendered(option.rect.size(), QImage::Format_ARGB32_Premultiplied);
    rendered.fill(table->palette().color(QPalette::Base));
    QPainter painter(&rendered);
    const QModelIndex index = table->model()->index(
        0, quader::QtRecentProjectsModel::NameColumn);
    table->itemDelegateForIndex(index)->paint(&painter, option, index);
    painter.end();
    return rendered;
  };
  const QImage first_path = render_project_name(
      QStringLiteral("C:/Alpha/project.qproj"));
  const QImage second_path = render_project_name(
      QStringLiteral("C:/Omega/project.qproj"));
  const QRect path_line(0, first_path.height() / 2, first_path.width(),
                        first_path.height() - first_path.height() / 2);
  EXPECT_NE(first_path.copy(path_line), second_path.copy(path_line))
      << "same-name projects must display their distinct paths on the second line";
  home.set_recent_projects(projects);
  const auto content_rect = [&](QWidget *widget) {
    return QRect(widget->mapTo(scroll->widget(), QPoint()), widget->size());
  };
  const auto expect_rounded_recent_frame = [&]() {
    QStyleOption option;
    option.initFrom(recent_frame);
    if (!quader::qt_application_style_active(&option)) {
      return;
    }
    QWidget *content = scroll->widget();
    QImage rendered(content->size(), QImage::Format_ARGB32_Premultiplied);
    rendered.fill(content->palette().color(QPalette::Window));
    QPainter painter(&rendered);
    content->render(&painter);
    painter.end();
    const QRect bounds = content_rect(recent_frame);
    ASSERT_TRUE(rendered.rect().contains(bounds.adjusted(-2, -2, 2, 2)));
    const QColor exterior = rendered.pixelColor(bounds.topLeft() - QPoint(2, 2));
    for (const QPoint &corner : {bounds.topLeft(), bounds.topRight(),
                                  bounds.bottomLeft(), bounds.bottomRight()}) {
      EXPECT_EQ(rendered.pixelColor(corner), exterior)
          << "table children must not cover the rounded outer corners";
    }
    const QColor border = recent_frame->palette().color(QPalette::Midlight);
    EXPECT_NE(border, exterior);
    for (const QPoint &edge : {
             QPoint(bounds.center().x(), bounds.top()),
             QPoint(bounds.center().x(), bounds.bottom()),
             QPoint(bounds.left(), bounds.center().y()),
             QPoint(bounds.right(), bounds.center().y())}) {
      EXPECT_EQ(rendered.pixelColor(edge), border)
          << "the contained recent-project surface must retain its quiet border";
    }
  };
  const auto review = [&](const QString &suffix, bool stacked) {
    settle_layout();
    SCOPED_TRACE(suffix.toStdString());
    expect_rounded_recent_frame();
    for (const int column : {quader::QtRecentProjectsModel::LastModifiedColumn,
                             quader::QtRecentProjectsModel::EditorVersionColumn}) {
      const QString header_text =
          table->model()->headerData(column, Qt::Horizontal).toString();
      EXPECT_GE(table->columnWidth(column),
                table->horizontalHeader()->fontMetrics().horizontalAdvance(
                    header_text));
      const QString value = table->model()->index(0, column).data().toString();
      EXPECT_GE(table->columnWidth(column),
                table->fontMetrics().horizontalAdvance(value));
    }
    const QModelIndex actions_index = table->model()->index(
        0, quader::QtRecentProjectsModel::ActionsColumn);
    const int previous_horizontal_scroll = table->horizontalScrollBar()->value();
    const int previous_vertical_scroll = table->verticalScrollBar()->value();
    table->scrollTo(actions_index);
    const QRect actions_rect = table->visualRect(actions_index);
    ASSERT_TRUE(table->viewport()->rect().contains(actions_rect.center()));
    send_mouse_event(*table->viewport(), QEvent::MouseMove,
                     actions_rect.center(), Qt::NoButton, Qt::NoButton);
    EXPECT_EQ(table->viewport()->cursor().shape(), Qt::PointingHandCursor);
    QEvent leave_actions(QEvent::Leave);
    QCoreApplication::sendEvent(table->viewport(), &leave_actions);
    EXPECT_EQ(table->viewport()->cursor().shape(), Qt::ArrowCursor);
    table->horizontalScrollBar()->setValue(previous_horizontal_scroll);
    table->verticalScrollBar()->setValue(previous_vertical_scroll);
    EXPECT_TRUE(title->isVisible());
    EXPECT_TRUE(logo->isVisible());
    ASSERT_FALSE(logo->pixmap().isNull());
    EXPECT_EQ(logo->accessibleName(), QStringLiteral("Quader"));
    EXPECT_LE(logo->pixmap().deviceIndependentSize().width(), logo->width());
    EXPECT_LE(logo->pixmap().deviceIndependentSize().height(), logo->height());
    if (home.layoutDirection() == Qt::RightToLeft) {
      EXPECT_GT(content_rect(logo).left(), content_rect(title).right());
    } else {
      EXPECT_LT(content_rect(logo).right(), content_rect(title).left());
    }
    EXPECT_LE(std::abs(content_rect(logo).center().y() -
                       content_rect(title).center().y()),
              1);
    EXPECT_TRUE(new_project->isVisible());
    EXPECT_TRUE(open_project->isVisible());
    EXPECT_GE(title->height(), title->fontMetrics().height());
    EXPECT_GE(new_project->width(), new_project->sizeHint().width());
    EXPECT_GE(open_project->width(), open_project->sizeHint().width());
    EXPECT_GT(content_rect(tutorial).top(), content_rect(table).bottom());
    if (stacked) {
      EXPECT_GT(content_rect(documentation).top(),
                content_rect(tutorial).bottom());
    } else {
      EXPECT_EQ(content_rect(tutorial).top(), content_rect(documentation).top());
      EXPECT_LE(std::abs(tutorial->width() - documentation->width()), 1);
      EXPECT_FALSE(content_rect(tutorial).intersects(content_rect(documentation)));
    }
    for (const QString &kind : {QStringLiteral("tutorial"),
                                QStringLiteral("documentation")}) {
      auto *card = home.findChild<QWidget *>(
          QStringLiteral("projectHome.%1.card").arg(kind));
      auto *card_title = home.findChild<QLabel *>(
          QStringLiteral("projectHome.%1.title").arg(kind));
      auto *description = home.findChild<QLabel *>(
          QStringLiteral("projectHome.%1.description").arg(kind));
      auto *action = home.findChild<QPushButton *>(
          QStringLiteral("projectHome.%1.action").arg(kind));
      ASSERT_NE(card, nullptr);
      ASSERT_NE(card_title, nullptr);
      ASSERT_NE(description, nullptr);
      ASSERT_NE(action, nullptr);
      EXPECT_TRUE(card->isVisible());
      EXPECT_TRUE(card_title->isVisible());
      EXPECT_TRUE(description->isVisible());
      EXPECT_TRUE(action->isVisible());
      EXPECT_FALSE(card_title->text().isEmpty());
      EXPECT_FALSE(description->text().isEmpty());
      EXPECT_TRUE(description->wordWrap());
      EXPECT_GE(description->height(),
                description->heightForWidth(description->width()));
      EXPECT_GE(card_title->height(),
                card_title->heightForWidth(card_title->width()));
      EXPECT_GE(action->width(), action->sizeHint().width());
      EXPECT_TRUE(card->rect().contains(
          QRect(action->mapTo(card, QPoint()), action->size())));
      EXPECT_LE(card_title->mapTo(card, QPoint()).y() + card_title->height(),
                description->mapTo(card, QPoint()).y());
      EXPECT_LE(description->mapTo(card, QPoint()).y() + description->height(),
                action->mapTo(card, QPoint()).y());
      EXPECT_LE(content_rect(card).right(), scroll->widget()->rect().right());
      EXPECT_GE(content_rect(card).left(), 0);
    }
    if (!review_directory.isEmpty()) {
      EXPECT_TRUE(home.grab().save(QDir(review_directory).filePath(
          QStringLiteral("launcher-%1.png").arg(suffix))));
    }
  };

  int cycle = 0;
  int baseline_row_height = 0;
  QSize baseline_logo_size;
  for (const float factor : {1.0F, 2.0F, 1.0F}) {
    scale_controller.apply_factor(factor);
    const auto &metrics = scale_controller.metrics();
    home.apply_ui_scale(metrics);
    home.resize(metrics.px(1200), metrics.px(740));
    settle_layout();
    table->setCurrentIndex(table->model()->index(0, 0));
    table->selectRow(0);
    table->setFocus();
    const int row_height = table->verticalHeader()->defaultSectionSize();
    EXPECT_GE(row_height, table->fontMetrics().height() * 2);
    if (cycle == 0) {
      baseline_row_height = row_height;
      baseline_logo_size = logo->size();
    } else if (factor == 1.0F) {
      EXPECT_EQ(row_height, baseline_row_height);
      EXPECT_EQ(logo->size(), baseline_logo_size);
    } else {
      EXPECT_GT(row_height, baseline_row_height);
      EXPECT_GT(logo->width(), baseline_logo_size.width());
      EXPECT_GT(logo->height(), baseline_logo_size.height());
    }
    review(QStringLiteral("%1-%2").arg(qRound(factor * 100)).arg(cycle++), false);
    if (factor == 2.0F) {
      quader::QtProjectHomePage restored_at_saved_scale;
      restored_at_saved_scale.apply_ui_scale(metrics);
      const auto *restored_logo = restored_at_saved_scale.findChild<QLabel *>(
          QStringLiteral("projectHome.logo"));
      ASSERT_NE(restored_logo, nullptr);
      EXPECT_FALSE(restored_logo->pixmap().isNull());
      EXPECT_EQ(restored_logo->pixmap().deviceIndependentSize(),
                logo->pixmap().deviceIndependentSize());
      for (const QString &object_name : {
               QStringLiteral("projectHome.title"),
               QStringLiteral("projectHome.recentProjects.label"),
               QStringLiteral("projectHome.tutorial.title"),
               QStringLiteral("projectHome.documentation.title")}) {
        const auto *live_label = home.findChild<QLabel *>(object_name);
        const auto *restored_label =
            restored_at_saved_scale.findChild<QLabel *>(object_name);
        ASSERT_NE(live_label, nullptr);
        ASSERT_NE(restored_label, nullptr);
        EXPECT_DOUBLE_EQ(restored_label->font().pointSizeF(),
                         live_label->font().pointSizeF())
            << object_name.toStdString()
            << " must look identical after saved-scale startup and live scaling";
        EXPECT_EQ(restored_label->font().weight(), live_label->font().weight());
      }
      home.resize(1200, 740);
      review(QStringLiteral("200-fixed-window"), true);
      EXPECT_GT(scroll->verticalScrollBar()->maximum(), 0);
      scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
      settle_layout();
      if (!review_directory.isEmpty()) {
        EXPECT_TRUE(home.grab().save(QDir(review_directory).filePath(
            QStringLiteral("launcher-200-fixed-window-scrolled.png"))));
      }
      scroll->verticalScrollBar()->setValue(0);
    }
  }

  home.resize(660, 740);
  review(QStringLiteral("narrow"), true);
  auto translated = quader::QtProjectHomeLabels::defaults();
  translated.title = QStringLiteral("Progetti");
  translated.new_project = QStringLiteral("Nuovo progetto");
  translated.open_project = QStringLiteral("Apri progetto\u2026");
  translated.recent_projects = QStringLiteral("Progetti recenti");
  translated.recent_name_column = QStringLiteral("Progetto");
  translated.recent_last_modified_column = QStringLiteral("Ultima modifica");
  translated.recent_editor_version_column = QStringLiteral("Versione editor");
  translated.tutorial_title = QStringLiteral("La tua prima ora in Quader");
  translated.tutorial_description =
      QStringLiteral("Impara le basi e costruisci la tua prima scena.");
  translated.tutorial_action = QStringLiteral("Inizia il tutorial");
  translated.documentation_title = QStringLiteral("Leggi la documentazione");
  translated.documentation_description = QStringLiteral(
      "Esplora le guide agli strumenti e ai flussi di lavoro di Quader.");
  translated.documentation_action = QStringLiteral("Apri la documentazione");
  home.set_labels(std::move(translated));
  review(QStringLiteral("narrow-italian"), true);
  home.set_labels(quader::QtProjectHomeLabels::defaults());
  home.resize(1200, 740);
  home.setLayoutDirection(Qt::RightToLeft);
  review(QStringLiteral("rtl"), false);
  EXPECT_GT(content_rect(tutorial).left(), content_rect(documentation).left());
  EXPECT_GT(content_rect(title).left(), content_rect(new_project).left());

  home.setLayoutDirection(Qt::LeftToRight);
  QPalette light_palette(QColor(238, 238, 238), QColor(250, 250, 250));
  light_palette.setColor(QPalette::Window, QColor(250, 250, 250));
  light_palette.setColor(QPalette::Base, QColor(255, 255, 255));
  light_palette.setColor(QPalette::AlternateBase, QColor(242, 242, 242));
  light_palette.setColor(QPalette::Button, QColor(238, 238, 238));
  light_palette.setColor(QPalette::Text, QColor(20, 20, 20));
  light_palette.setColor(QPalette::WindowText, QColor(45, 45, 45));
  light_palette.setColor(QPalette::ButtonText, QColor(20, 20, 20));
  light_palette.setColor(QPalette::Highlight, QColor(0, 90, 180));
  light_palette.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
  home.setPalette(light_palette);
  review(QStringLiteral("explicit-light-palette"), false);
  EXPECT_EQ(tutorial->palette().color(QPalette::Text),
            light_palette.color(QPalette::Text));
  home.setPalette(QPalette{});
  quader::apply_qt_application_theme_for_contrast_preference(
      native_palette, Qt::ContrastPreference::HighContrast);
  review(QStringLiteral("native-high-contrast-fallback"), false);
  EXPECT_EQ(application.palette().resolveMask(), 0u);
  quader::apply_qt_application_theme_for_contrast_preference(
      native_palette, Qt::ContrastPreference::NoPreference);

  home.set_recent_projects({});
  settle_layout();
  EXPECT_FALSE(relative_time_timer->isActive());
  expect_rounded_recent_frame();
  EXPECT_TRUE(table->isHidden());
  EXPECT_TRUE(tutorial->isVisible());
  EXPECT_TRUE(documentation->isVisible());
  if (!review_directory.isEmpty()) {
    EXPECT_TRUE(home.grab().save(QDir(review_directory).filePath(
        QStringLiteral("launcher-empty.png"))));
  }

  std::vector<quader::QtRecentProjectItem> many_projects;
  for (int index = 0; index < 22; ++index) {
    many_projects.push_back({
        .name = QStringLiteral("Project %1").arg(index),
        .manifest_path = QStringLiteral("C:/Projects/%1/project.qproj").arg(index),
    });
  }
  home.set_recent_projects(many_projects);
  settle_layout();
  EXPECT_TRUE(relative_time_timer->isActive());
  ASSERT_EQ(home.recent_projects_model().rowCount(), 20);
  EXPECT_EQ(home.recent_projects_model().projects().front().name,
            QStringLiteral("Project 0"));
  EXPECT_EQ(home.recent_projects_model().projects().back().name,
            QStringLiteral("Project 19"));
  EXPECT_FALSE(home.recent_projects_model().index(20, 0).isValid());
  const QModelIndex last_recent = home.recent_projects_model().index(19, 0);
  table->scrollTo(last_recent);
  settle_layout();
  EXPECT_TRUE(table->viewport()->rect().intersects(table->visualRect(last_recent)))
      << "all twenty retained recent projects must remain reachable";
  scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
  settle_layout();
  EXPECT_TRUE(tutorial->isVisible());
  EXPECT_TRUE(documentation->isVisible());
  EXPECT_LE(tutorial->mapTo(scroll->viewport(), QPoint()).y() + tutorial->height(),
            scroll->viewport()->height());
  if (!review_directory.isEmpty()) {
    EXPECT_TRUE(home.grab().save(QDir(review_directory).filePath(
        QStringLiteral("launcher-twenty-recent-scrolled.png"))));
  }
  home.hide();
  EXPECT_FALSE(relative_time_timer->isActive());
}

TEST(QtProjectWorkspaceTests,
     RecentProjectMetadataUsesSavedVersionsAndStableRelativeTimes) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader_recent_project_metadata_test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  quader::QtRecentProjectsModel model;
  constexpr std::uint64_t kNow = 1'800'000'000'000ULL;
  constexpr std::uint64_t kMinute = 60'000ULL;
  constexpr std::uint64_t kHour = 60 * kMinute;
  constexpr std::uint64_t kDay = 24 * kHour;
  struct RelativeTimeCase {
    std::optional<std::uint64_t> modified;
    QString expected;
  };
  const std::vector<RelativeTimeCase> cases{
      {kNow - 30'000, QStringLiteral("Just now")},
      {kNow - kMinute - 30'000, QStringLiteral("1 minute ago")},
      {kNow - 12 * kMinute - 30'000, QStringLiteral("12 minutes ago")},
      {kNow - kHour - 30'000, QStringLiteral("1 hour ago")},
      {kNow - 23 * kHour - 30'000, QStringLiteral("23 hours ago")},
      {kNow - kDay - 30'000, QStringLiteral("Yesterday")},
      {kNow - 3 * kDay - 30'000, QStringLiteral("3 days ago")},
      {kNow - 8 * kDay - 30'000, QStringLiteral("1 week ago")},
      {kNow - 22 * kDay - 30'000, QStringLiteral("3 weeks ago")},
      {kNow - 31 * kDay - 30'000, QStringLiteral("1 month ago")},
      {kNow - 95 * kDay - 30'000, QStringLiteral("3 months ago")},
      {kNow - 366 * kDay - 30'000, QStringLiteral("1 year ago")},
      {kNow - 800 * kDay - 30'000, QStringLiteral("2 years ago")},
      {kNow + kDay, QStringLiteral("Just now")},
      {std::nullopt, QStringLiteral("\u2014")},
      {std::numeric_limits<std::uint64_t>::max(), QStringLiteral("\u2014")},
  };
  std::vector<quader::QtRecentProjectItem> projects;
  for (std::size_t index = 0; index < cases.size(); ++index) {
    projects.push_back({
        .name = QStringLiteral("Project %1").arg(index),
        .manifest_path = QStringLiteral("C:/Projects/%1/project.qproj").arg(index),
        .manifest_last_modified_unix_ms = cases[index].modified,
        .last_saved_app_version = index == 0 ? QStringLiteral("0.1.42") : QString{},
    });
  }
  ASSERT_TRUE(model.set_projects(projects));
  model.refresh_relative_times(kNow);
  for (int row = 0; row < static_cast<int>(cases.size()); ++row) {
    SCOPED_TRACE(row);
    EXPECT_EQ(model.index(row, quader::QtRecentProjectsModel::LastModifiedColumn)
                  .data().toString(),
              cases[static_cast<std::size_t>(row)].expected);
  }
  const QModelIndex saved_version =
      model.index(0, quader::QtRecentProjectsModel::EditorVersionColumn);
  EXPECT_EQ(saved_version.data().toString(), QStringLiteral("v0.1.42"));
  EXPECT_EQ(model.index(1, quader::QtRecentProjectsModel::EditorVersionColumn)
                .data().toString(),
            QStringLiteral("\u2014"))
      << "projects without saved-version metadata must not claim the running version";
  const QModelIndex modified =
      model.index(1, quader::QtRecentProjectsModel::LastModifiedColumn);
  const QString absolute_timestamp = modified.data(Qt::ToolTipRole).toString();
  EXPECT_FALSE(absolute_timestamp.isEmpty());
  EXPECT_NE(absolute_timestamp, modified.data().toString());
  EXPECT_EQ(modified.data(Qt::AccessibleDescriptionRole).toString(),
            absolute_timestamp);
  auto translated_labels = quader::QtProjectHomeLabels::defaults();
  translated_labels.modified_week_ago = QStringLiteral("1 settimana fa");
  model.set_labels(std::move(translated_labels));
  EXPECT_EQ(model.index(7, quader::QtRecentProjectsModel::LastModifiedColumn)
                .data().toString(),
            QStringLiteral("1 settimana fa"));
  model.set_labels(quader::QtProjectHomeLabels::defaults());

  int model_resets = 0;
  int data_changes = 0;
  QObject::connect(&model, &QAbstractItemModel::modelReset, &application,
                   [&]() { ++model_resets; });
  QObject::connect(&model, &QAbstractItemModel::dataChanged, &application,
                   [&](const QModelIndex &first, const QModelIndex &last) {
                     ++data_changes;
                     EXPECT_EQ(first.column(),
                               quader::QtRecentProjectsModel::LastModifiedColumn);
                     EXPECT_EQ(last.column(),
                               quader::QtRecentProjectsModel::LastModifiedColumn);
                   });
  model.refresh_relative_times(kNow + 1'000);
  EXPECT_EQ(data_changes, 0)
      << "unchanged relative labels must not invalidate retained rows";
  model.refresh_relative_times(kNow + kMinute);
  EXPECT_GT(data_changes, 0);
  EXPECT_EQ(modified.data().toString(), QStringLiteral("2 minutes ago"));
  EXPECT_EQ(modified.data(Qt::ToolTipRole).toString(), absolute_timestamp);
  EXPECT_EQ(saved_version.data().toString(), QStringLiteral("v0.1.42"));
  EXPECT_EQ(model.projects(), projects);
  EXPECT_EQ(model_resets, 0);
}

TEST(QtProjectWorkspaceTests,
     HomeAndWorkspaceDispatchPresentationIntentWithoutMutatingModels) {
  int argc = 1;
  char application_name[] = "quader_ui_test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
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
       .last_saved_app_version = QStringLiteral("0.1.42")},
      {.name = QStringLiteral("Missing"),
       .manifest_path = QStringLiteral("C:/Projects/Missing/Missing.qproj"),
       .available = false},
  };
  home.set_recent_projects(recent_projects);
  home.resize(900, 600);
  home.show();
  QCoreApplication::processEvents();

  auto *title = home.findChild<QLabel *>(QStringLiteral("projectHome.title"));
  ASSERT_NE(title, nullptr);
  EXPECT_EQ(title->text(), QStringLiteral("Projects"));
  EXPECT_NE(home.findChild<QLabel *>(
                QStringLiteral("projectHome.recentProjects.label")),
            nullptr);
  EXPECT_EQ(home.findChild<QWidget *>(
                QStringLiteral("projectHome.leftRail")),
            nullptr);
  EXPECT_EQ(home.findChild<QLabel *>(
                QStringLiteral("projectHome.brand.name")),
            nullptr);
  EXPECT_EQ(home.findChild<QPushButton *>(
                QStringLiteral("projectHome.navigation.recent")),
            nullptr);
  auto *tutorial_action = home.findChild<QPushButton *>(
      QStringLiteral("projectHome.tutorial.action"));
  auto *documentation_action = home.findChild<QPushButton *>(
      QStringLiteral("projectHome.documentation.action"));
  ASSERT_NE(tutorial_action, nullptr);
  ASSERT_NE(documentation_action, nullptr);
  EXPECT_EQ(tutorial_action->text(), QStringLiteral("Start tutorial"));
  EXPECT_EQ(documentation_action->text(), QStringLiteral("Open docs"));
  const auto retained_projects = home.recent_projects_model().projects();
  tutorial_action->click();
  documentation_action->click();
  tutorial_action->setFocus();
  send_key(*tutorial_action, Qt::Key_Space);
  documentation_action->setFocus();
  send_key(*documentation_action, Qt::Key_Space);
  EXPECT_EQ(new_project_requests, 0);
  EXPECT_EQ(open_project_requests, 0);
  EXPECT_TRUE(recent_actions.empty());
  EXPECT_EQ(home.recent_projects_model().projects(), retained_projects)
      << "the first-version resource cards are visual placeholders";
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
  const int baseline_row_height =
      recent_view->verticalHeader()->defaultSectionSize();
  const int baseline_metadata_width = recent_view->columnWidth(
      quader::QtRecentProjectsModel::LastModifiedColumn);
  const double baseline_title_size = title->font().pointSizeF();
  home.apply_ui_scale(quader::QtUiScaleMetrics(2.0F, 1U));
  EXPECT_GT(recent_view->verticalHeader()->defaultSectionSize(),
            baseline_row_height);
  EXPECT_GT(recent_view->columnWidth(
                quader::QtRecentProjectsModel::LastModifiedColumn),
            baseline_metadata_width);
  EXPECT_GT(title->font().pointSizeF(), baseline_title_size);

  home.apply_ui_scale(quader::QtUiScaleMetrics(1.0F, 2U));
  EXPECT_EQ(recent_view->verticalHeader()->defaultSectionSize(),
            baseline_row_height);
  EXPECT_EQ(recent_view->columnWidth(
                quader::QtRecentProjectsModel::LastModifiedColumn),
            baseline_metadata_width);
  EXPECT_DOUBLE_EQ(title->font().pointSizeF(), baseline_title_size);
  ASSERT_EQ(recent_view->model()->columnCount(),
            quader::QtRecentProjectsModel::ColumnCount);
  EXPECT_EQ(recent_view->model()->columnCount(), 4);
  EXPECT_EQ(recent_view->model()
                ->headerData(quader::QtRecentProjectsModel::NameColumn,
                             Qt::Horizontal)
                .toString(),
            QStringLiteral("Project"));
  EXPECT_TRUE(recent_view->model()
                  ->headerData(quader::QtRecentProjectsModel::ActionsColumn,
                               Qt::Horizontal)
                  .toString()
                  .isEmpty());
  EXPECT_EQ(recent_view->model()
                ->headerData(quader::QtRecentProjectsModel::ActionsColumn,
                             Qt::Horizontal, Qt::AccessibleTextRole)
                .toString(),
            QStringLiteral("Actions"));
  EXPECT_EQ(recent_view->model()
                ->headerData(
                    quader::QtRecentProjectsModel::LastModifiedColumn,
                    Qt::Horizontal)
                .toString(),
            QStringLiteral("Last modified"));
  EXPECT_EQ(recent_view->model()
                ->headerData(quader::QtRecentProjectsModel::EditorVersionColumn,
                             Qt::Horizontal)
                .toString(),
            QStringLiteral("Editor version"));
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
  EXPECT_EQ(recent_view->model()
                ->index(0, quader::QtRecentProjectsModel::EditorVersionColumn)
                .data()
                .toString(),
            QStringLiteral("v0.1.42"));
  EXPECT_EQ(recent_view->model()
                ->index(1, quader::QtRecentProjectsModel::EditorVersionColumn)
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
  const QRect actions_rect = recent_view->visualRect(recent_view->model()->index(
      0, quader::QtRecentProjectsModel::ActionsColumn));
  ASSERT_TRUE(actions_rect.isValid());
  send_mouse_event(*recent_view->viewport(), QEvent::MouseMove,
                   actions_rect.center(), Qt::NoButton, Qt::NoButton);
  EXPECT_EQ(recent_view->viewport()->cursor().shape(), Qt::PointingHandCursor);
  send_mouse_event(*recent_view->viewport(), QEvent::MouseMove,
                   available_rect.center(), Qt::NoButton, Qt::NoButton);
  EXPECT_EQ(recent_view->viewport()->cursor().shape(), Qt::ArrowCursor);
  const QPoint empty_position =
      recent_view->viewport()->rect().bottomRight() - QPoint(2, 2);
  ASSERT_FALSE(recent_view->indexAt(empty_position).isValid());
  send_mouse_event(*recent_view->viewport(), QEvent::MouseMove,
                   actions_rect.center(), Qt::NoButton, Qt::NoButton);
  send_mouse_event(*recent_view->viewport(), QEvent::MouseMove,
                   empty_position, Qt::NoButton, Qt::NoButton);
  EXPECT_EQ(recent_view->viewport()->cursor().shape(), Qt::ArrowCursor);
  click_widget(*recent_view->viewport(), available_rect.center());
  EXPECT_EQ(recent_view->currentIndex().row(), 0);
  int recent_model_resets = 0;
  int recent_data_changes = 0;
  QObject::connect(recent_view->model(), &QAbstractItemModel::modelReset,
                   &home, [&]() { ++recent_model_resets; });
  QObject::connect(recent_view->model(), &QAbstractItemModel::dataChanged,
                   &home, [&]() { ++recent_data_changes; });
  for (int frame = 0; frame < 5; ++frame) {
    home.set_recent_projects(recent_projects);
  }
  EXPECT_EQ(recent_model_resets, 0);
  EXPECT_EQ(recent_data_changes, 0);
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

  EXPECT_EQ(workspace.findChild<QToolButton *>(
                QStringLiteral("sceneWorkspace.newScene")), nullptr);
  EXPECT_TRUE(workspace.set_scene_tabs({}));
  auto *new_scene = workspace.findChild<QPushButton *>(
      QStringLiteral("sceneWorkspace.empty.newScene"));
  ASSERT_NE(new_scene, nullptr);
  new_scene->click();
  EXPECT_EQ(new_scene_requests, 1);

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
  int last_window_closed_count = 0;
  QObject::connect(&application, &QGuiApplication::lastWindowClosed,
                   &application, [&] { ++last_window_closed_count; });

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
  RuntimeRebindWindowHideProbe hide_probe;
  ui.main_window()->installEventFilter(&hide_probe);
  const auto bind_scene = [&](quader::SceneSession &scene) {
    ++scene_bind_count;
    bound_scene = &scene;
    project_scene_was_bound = project_scene_was_bound || project.has_project();
    fallback_bound_after_project_release =
        fallback_bound_after_project_release ||
        (project_scene_was_bound && !project.has_project() &&
         project.active_scene() == nullptr &&
         bound_scene == &project.fallback_scene());
    hide_probe.fallback_rebind_in_progress =
        project_scene_was_bound && !project.has_project();
    ui.install_runtime_hooks(
        editor, scene.scene(), scene.assets(), scene.modeling(),
        scene.selection(), scene.native_tools(), scene.point_placement(),
        quader::kInvalidAssetId);
    hide_probe.fallback_rebind_in_progress = false;
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
  QTimer close_timeout;
  close_timeout.setSingleShot(true);
  QObject::connect(&close_timeout, &QTimer::timeout, &application, [&] {
    ADD_FAILURE() << "main-window close did not finish";
    application.quit();
  });
  close_timeout.start(1000);
  QTimer::singleShot(0, ui.main_window(), [&] {
    EXPECT_TRUE(ui.main_window()->close());
    close_timeout.stop();
    application.quit();
  });
  EXPECT_EQ(application.exec(), 0);
  EXPECT_FALSE(ui.main_window()->isVisible());
  EXPECT_FALSE(editor.project().has_project());
  EXPECT_EQ(project.active_scene(), nullptr);
  EXPECT_EQ(bound_scene, &project.fallback_scene());
  EXPECT_NE(bound_scene, outgoing_scene);
  EXPECT_EQ(scene_bind_count, 3);
  EXPECT_TRUE(fallback_bound_after_project_release);
  EXPECT_EQ(hide_probe.hide_count, 1);
  EXPECT_TRUE(hide_probe.hidden_during_fallback_rebind)
      << "the window must hide before the synchronous fallback rebind returns";
  EXPECT_EQ(last_window_closed_count, 1);
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
     QuitKeepsDirtyProjectVisibleUntilAcceptedAndProjectCloseKeepsLauncherVisible) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  const std::filesystem::path isolated_profile =
      std::filesystem::temp_directory_path() /
      ("quader-ui-quit-visibility-" +
       std::to_string(std::chrono::steady_clock::now()
                          .time_since_epoch()
                          .count()));
  ScopedIsolatedLocalAppData scoped_profile(isolated_profile);
  ASSERT_TRUE(scoped_profile.active());

  int argc = 1;
  char application_name[] = "quader-quit-visibility-test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  int last_window_closed_count = 0;
  QObject::connect(&application, &QGuiApplication::lastWindowClosed,
                   &application, [&] { ++last_window_closed_count; });

  const std::filesystem::path resource_root =
      std::filesystem::path(QUADER_SOURCE_DIR) / "resources";
  const std::filesystem::path project_root =
      scoped_profile.profile() / "Quit Visibility Project";
  ASSERT_TRUE(std::filesystem::create_directory(project_root));
  quader::RenderSystem render_system;
  quader::ProjectSession project;
  project.set_resource_root(resource_root / "modeling_assets");
  quader::QuaderEditorApi editor(project);
  quader::UISystem ui;
  ASSERT_TRUE(ui.initialize(render_system, resource_root));
  ASSERT_NE(ui.main_window(), nullptr);
  ui.main_window()->setAttribute(Qt::WA_DontShowOnScreen, true);
  for (QMessageBox *dialog : ui.main_window()->findChildren<QMessageBox *>()) {
    dialog->setAttribute(Qt::WA_DontShowOnScreen, true);
  }
  RuntimeRebindWindowHideProbe hide_probe;
  ui.main_window()->installEventFilter(&hide_probe);
  int fallback_bind_count = 0;
  const auto bind_scene = [&](quader::SceneSession &scene) {
    hide_probe.fallback_rebind_in_progress = !project.has_project();
    if (hide_probe.fallback_rebind_in_progress) {
      ++fallback_bind_count;
    }
    ui.install_runtime_hooks(
        editor, scene.scene(), scene.assets(), scene.modeling(),
        scene.selection(), scene.native_tools(), scene.point_placement(),
        quader::kInvalidAssetId);
    hide_probe.fallback_rebind_in_progress = false;
  };
  quader::QuaderEditorApiSessionAccess::set_scene_binding_changed_callback(
      editor, bind_scene);
  bind_scene(project.fallback_scene());
  const auto created = editor.project().create(project_root);
  ASSERT_TRUE(created.ok()) << created.error_message();
  // This owner fixture has no renderer or exposed native window. Present the
  // bound workspace directly; App loading readiness has its own GPU fixture.
  ui.main_window()->show();
  ASSERT_TRUE(ui.main_window()->isVisible());
  ASSERT_TRUE(editor.scene().create_point_light().ok());
  ASSERT_TRUE(editor.scene_documents().has_dirty_scenes());
  const auto active_scene = editor.scene_documents().active_scene_id();
  ASSERT_TRUE(active_scene.has_value());
  const std::filesystem::path manifest_path = editor.project().manifest_path();

  int confirmation_count = 0;
  editor.scene_documents().set_runtime_hooks({
      .confirm_dirty_scene =
          [&](const quader::QuaderSceneDocumentInfo &scene,
              std::string_view action_id) {
            ++confirmation_count;
            EXPECT_TRUE(ui.main_window()->isVisible());
            EXPECT_TRUE(editor.project().has_project());
            QTimer::singleShot(0, ui.main_window(), [&] {
              auto *dialog =
                  qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
              EXPECT_NE(dialog, nullptr);
              if (dialog == nullptr) {
                return;
              }
              EXPECT_EQ(dialog->objectName(),
                        QStringLiteral("scene_unsaved_confirmation"));
              EXPECT_TRUE(dialog->isVisible());
              EXPECT_TRUE(ui.main_window()->isVisible());
              auto *cancel_button = dialog->findChild<QPushButton *>(
                  QStringLiteral("unsavedConfirmation.cancel"));
              EXPECT_NE(cancel_button, nullptr);
              if (cancel_button != nullptr) {
                cancel_button->click();
              } else {
                dialog->reject();
              }
            });
            return ui.confirm_dirty_scene(scene, action_id);
          },
  });
  EXPECT_FALSE(ui.main_window()->close());
  EXPECT_EQ(confirmation_count, 1);
  EXPECT_TRUE(ui.main_window()->isVisible());
  EXPECT_TRUE(editor.project().has_project());
  EXPECT_EQ(editor.scene_documents().active_scene_id(), active_scene);
  EXPECT_TRUE(editor.scene_documents().has_dirty_scenes());
  EXPECT_EQ(fallback_bind_count, 1);
  EXPECT_EQ(hide_probe.hide_count, 0);
  EXPECT_EQ(last_window_closed_count, 0);

  // An unavailable confirmation hook fails the project-close preflight.
  editor.scene_documents().set_runtime_hooks({});
  EXPECT_FALSE(ui.main_window()->close());
  EXPECT_TRUE(ui.main_window()->isVisible());
  EXPECT_TRUE(editor.project().has_project());
  EXPECT_EQ(editor.scene_documents().active_scene_id(), active_scene);
  EXPECT_TRUE(editor.scene_documents().has_dirty_scenes());
  EXPECT_EQ(fallback_bind_count, 1);
  EXPECT_EQ(hide_probe.hide_count, 0);
  EXPECT_EQ(last_window_closed_count, 0);

  editor.scene_documents().set_runtime_hooks({
      .confirm_dirty_scene = [](const quader::QuaderSceneDocumentInfo &,
                                std::string_view) {
        return quader::QuaderDirtySceneDecision::Discard;
      },
  });
  const auto closed = editor.project().close();
  ASSERT_TRUE(closed.ok()) << closed.error_message();
  EXPECT_FALSE(editor.project().has_project());
  EXPECT_TRUE(ui.main_window()->isVisible());
  EXPECT_EQ(fallback_bind_count, 2);
  EXPECT_EQ(hide_probe.hide_count, 0);
  EXPECT_EQ(last_window_closed_count, 0);
  auto *central_stack = ui.main_window()->findChild<QStackedWidget *>(
      QStringLiteral("mainCentralStack"));
  ASSERT_NE(central_stack, nullptr);
  EXPECT_NE(dynamic_cast<quader::QtProjectHomePage *>(
                central_stack->currentWidget()),
            nullptr);

  ui.refresh({}, {});
  auto *recent_projects = ui.main_window()->findChild<QTableView *>(
      QStringLiteral("projectHome.recentProjects.view"));
  ASSERT_NE(recent_projects, nullptr);
  ASSERT_EQ(recent_projects->model()->rowCount(), 1);
  recent_projects->setCurrentIndex(recent_projects->model()->index(0, 0));
  send_key(*recent_projects, Qt::Key_Return);
  ASSERT_TRUE(editor.project().has_project());
  EXPECT_EQ(editor.project().manifest_path(), manifest_path);
  auto *status_message = ui.main_window()->findChild<QLabel *>(
      QStringLiteral("statusMessage"));
  ASSERT_NE(status_message, nullptr);
  EXPECT_TRUE(status_message->text().isEmpty())
      << "opening a recent project must not publish a success banner";
  EXPECT_TRUE(status_message->isHidden());
  ui.refresh({}, {});
  EXPECT_TRUE(status_message->text().isEmpty());
  EXPECT_TRUE(status_message->isHidden());
  auto *exit_action = ui.main_window()->findChild<QAction *>(
      QStringLiteral("action_exit"));
  ASSERT_NE(exit_action, nullptr);
  ASSERT_TRUE(exit_action->isEnabled());
  QTimer close_timeout;
  close_timeout.setSingleShot(true);
  QObject::connect(&close_timeout, &QTimer::timeout, &application, [&] {
    ADD_FAILURE() << "File > Exit did not finish";
    application.quit();
  });
  close_timeout.start(1000);
  QTimer::singleShot(0, ui.main_window(), [&] {
    exit_action->trigger();
    close_timeout.stop();
    application.quit();
  });
  EXPECT_EQ(application.exec(), 0);
  EXPECT_FALSE(ui.main_window()->isVisible());
  EXPECT_FALSE(editor.project().has_project());
  EXPECT_EQ(fallback_bind_count, 3);
  EXPECT_EQ(hide_probe.hide_count, 1);
  EXPECT_TRUE(hide_probe.hidden_during_fallback_rebind);
  EXPECT_EQ(last_window_closed_count, 1);

  ui.shutdown();
}

TEST(QtProjectWorkspaceTests,
     SameProjectReopenResetsAssetFoldersAndRejectsDragBeforeNextFrame) {
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

  auto *asset_dock = ui.main_window()->findChild<QDockWidget *>(
      QStringLiteral("dock_asset_browser"));
  auto *browser = dynamic_cast<quader::QtAssetBrowser *>(
      ui.main_window()->findChild<QWidget *>(QStringLiteral("assetBrowser")));
  ASSERT_NE(asset_dock, nullptr);
  ASSERT_NE(browser, nullptr);
  // This fixture has no renderer, so the native-workspace menu stays disabled.
  // Present the retained consumer through the public panel intent.
  ASSERT_TRUE(editor.ui().show_panel("asset_browser").ok());
  browser->set_type_filter(quader::QtAssetBrowserKind::Material);
  ui.refresh({}, {});
  application.processEvents();
  ASSERT_TRUE(browser->isVisible());
  auto *tree = browser->findChild<QTreeWidget *>(QStringLiteral("assetBrowser.folderTree"));
  auto *grid = browser->findChild<QListView *>(QStringLiteral("assetBrowser.grid"));
  ASSERT_NE(tree, nullptr);
  ASSERT_NE(grid, nullptr);
  const auto find_folder = [&](const QString &path) -> QTreeWidgetItem * {
    for (QTreeWidgetItemIterator it(tree); *it != nullptr; ++it) {
      if ((*it)->data(0, Qt::UserRole + 2).toString() == path) {
        return *it;
      }
    }
    return nullptr;
  };
  auto *dev = find_folder(QStringLiteral("Materials/Dev"));
  ASSERT_NE(dev, nullptr);
  ASSERT_NE(dev->parent(), nullptr);
  dev->parent()->setExpanded(true);
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
    for (auto *parent = target->parent(); parent != nullptr;
         parent = parent->parent()) {
      parent->setExpanded(true);
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
  EXPECT_TRUE(browser->selected_folder().isEmpty());
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
     StatusDiagnosticsKeepPriorityAndExpireWithoutRevivingOldFailures) {
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
  ASSERT_TRUE(editor.selection().select_object(mesh.value()).ok());
  ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Edit).ok());
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Face).ok());
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
  ASSERT_FALSE(combine->enabled);
  const auto invalid_geometry = editor.commands().execute(
      quader::quader_command::kModelingCombineFaces);
  ASSERT_FALSE(invalid_geometry.ok());
  EXPECT_EQ(invalid_geometry.error_message(),
            "Command is disabled.");
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

  ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Object).ok());
  const auto activated = editor.commands().execute(
      quader::quader_command::kToolsActivateCreateBox);
  ASSERT_TRUE(activated.ok()) << activated.error_message();
  ui.refresh({}, {});
  EXPECT_TRUE(message->text().isEmpty())
      << "successful tool activation must not replace contextual hints";
  EXPECT_TRUE(message->isHidden());
  EXPECT_TRUE(hints->hints().empty())
      << "A tool alone does not publish hints before a viewport receives input.";
  // Error priority is independent of the particular tool. Supply a real local
  // receiving surface without requiring the uninitialized GPU viewport here.
  QWidget hint_surface;
  hint_surface.setAttribute(Qt::WA_DontShowOnScreen, true);
  hint_surface.setMouseTracking(true);
  hint_surface.resize(120, 30);
  quader::qt_register_contextual_input_hints(
      hint_surface, hint_surface,
      [](Qt::KeyboardModifiers) {
        return quader::QtContextualInputHintSourceState{1, true, true};
      },
      [](Qt::KeyboardModifiers) {
        return std::vector{quader::QuaderOperationInputHint{
            quader::QuaderOperationInputGesture::Information, {},
            "test.active_input_context", "Active Control"}};
      });
  hint_surface.show();
  const QPoint hint_position(10, 10);
  QMouseEvent hint_hover(QEvent::MouseMove, QPointF(hint_position),
      QPointF(hint_surface.mapToGlobal(hint_position)), Qt::NoButton,
      Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(&hint_surface, &hint_hover);
  ui.refresh({}, {});
  ASSERT_FALSE(hints->hints().empty());
  EXPECT_FALSE(hints->isHidden());
  EXPECT_EQ(announcements, 0);

  const auto object_selection_before_failure = editor.selection().summary();
  const auto history_before_failure = scene.history().undo_count();
  const auto failed = editor.commands().execute(
      quader::quader_command::kModelingCombineFaces);
  ASSERT_FALSE(failed.ok());
  EXPECT_EQ(failed.error_message(), "Command is disabled.");
  EXPECT_FALSE(failed.changed());
  EXPECT_EQ(editor.selection().summary().selected_face_count, 0u);
  EXPECT_EQ(editor.selection().summary().selected_object_count, 1u);
  EXPECT_EQ(editor.selection().summary().selection_revision,
            object_selection_before_failure.selection_revision);
  EXPECT_EQ(scene.history().undo_count(), history_before_failure);
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
  EXPECT_EQ(message->text(), QStringLiteral("Some optional items were skipped."));
  EXPECT_FALSE(message->isHidden());
  EXPECT_TRUE(hints->isHidden());
  EXPECT_EQ(announcements, 4);

  ASSERT_TRUE(editor.ui().notify(notification).ok());
  EXPECT_EQ(announcements, 5);
  ASSERT_TRUE(editor.commands()
                  .execute(quader::quader_command::kViewportToggleGrid)
                  .ok());
  ui.refresh({}, {});
  EXPECT_FALSE(message->is_error());
  EXPECT_FALSE(message->autoFillBackground());
  EXPECT_TRUE(message->text().isEmpty());
  EXPECT_TRUE(message->isHidden());
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
  EXPECT_TRUE(message->isHidden());
  EXPECT_FALSE(hints->isHidden());
  EXPECT_EQ(announcements, 6);

  const auto final_failure = editor.commands().execute(
      quader::quader_command::kModelingCombineFaces);
  ASSERT_FALSE(final_failure.ok());
  ui.refresh({}, {});
  ASSERT_TRUE(message->is_error());
  ASSERT_TRUE(hints->isHidden());
  EXPECT_EQ(announcements, 7);
  const auto failed_command_revision = editor.commands().last_status_revision();
  ASSERT_TRUE(editor.ui()
                  .notify({.level = quader::QuaderNotificationLevel::Info,
                           .message = "Operation completed successfully."})
                  .ok());
  EXPECT_EQ(editor.commands().last_status_revision(), failed_command_revision);
  EXPECT_FALSE(message->is_error());
  EXPECT_TRUE(message->text().isEmpty());
  EXPECT_TRUE(message->isHidden());
  EXPECT_FALSE(hints->isHidden());
  const auto restored_hint_revision = hints->projection_revision();
  for (int frame = 0; frame < 100; ++frame) {
    ui.refresh({}, {});
  }
  EXPECT_TRUE(message->text().isEmpty());
  EXPECT_TRUE(message->isHidden());
  EXPECT_FALSE(hints->isHidden());
  EXPECT_EQ(hints->projection_revision(), restored_hint_revision);
  EXPECT_EQ(announcements, 7);

  ASSERT_TRUE(editor.ui()
                  .notify({.level = quader::QuaderNotificationLevel::Warning,
                           .message = "Some optional items were skipped."})
                  .ok());
  ASSERT_FALSE(message->isHidden());
  ASSERT_TRUE(hints->isHidden());
  auto *hold_timer = message->findChild<QTimer *>();
  auto *fade = message->findChild<QPropertyAnimation *>();
  auto *opacity = qobject_cast<QGraphicsOpacityEffect *>(message->graphicsEffect());
  ASSERT_NE(hold_timer, nullptr);
  ASSERT_NE(fade, nullptr);
  ASSERT_NE(opacity, nullptr);
  ASSERT_TRUE(hold_timer->isActive());
  // Advance the direct warning through the real widget phases without a second
  // five-second wait. Its owner must stay authoritative until fading finishes.
  hold_timer->stop();
  ASSERT_TRUE(QMetaObject::invokeMethod(hold_timer, "timeout", Qt::DirectConnection));
  ASSERT_EQ(fade->state(), QAbstractAnimation::Running);
  fade->setCurrentTime(fade->duration() / 2);
  ASSERT_GT(opacity->opacity(), 0.0);
  ASSERT_LT(opacity->opacity(), 1.0);
  ui.refresh({}, {});
  EXPECT_FALSE(message->isHidden());
  EXPECT_TRUE(hints->isHidden());
  fade->setCurrentTime(fade->duration());
  EXPECT_TRUE(message->text().isEmpty());
  EXPECT_TRUE(message->isHidden());
  EXPECT_FALSE(hints->isHidden());
  EXPECT_EQ(announcements, 7);

  QElapsedTimer elapsed;
  elapsed.start();
  const qint64 hold_started_ms = elapsed.msecsSinceReference();
  const auto expiring_failure = editor.commands().execute(
      quader::quader_command::kModelingCombineFaces);
  ASSERT_FALSE(expiring_failure.ok());
  ui.refresh({}, {});
  ASSERT_TRUE(message->is_error());
  ASSERT_FALSE(message->isHidden());
  ASSERT_TRUE(hints->isHidden());
  ASSERT_TRUE(hold_timer->isActive());
  EXPECT_DOUBLE_EQ(opacity->opacity(), 1.0);
  EXPECT_EQ(announcements, 8);
  const auto expiring_revision = editor.commands().last_status_revision();
  // Keep the five-second hold on the real clock. Qt's thread-local animation
  // timer outlives the QApplications recreated by this suite, so step the fade
  // explicitly after the real timeout instead of depending on its retained clock.
  QEventLoop expiry_loop;
  int hold_timeouts = 0;
  QObject::connect(hold_timer, &QTimer::timeout, &expiry_loop, [&] {
    ++hold_timeouts;
    expiry_loop.quit();
  });
  QTimer expiry_observer;
  expiry_observer.setInterval(25);
  QObject::connect(&expiry_observer, &QTimer::timeout, &expiry_loop, [&] {
    ui.refresh({}, {});
    if (elapsed.elapsed() < 4900) {
      EXPECT_DOUBLE_EQ(opacity->opacity(), 1.0);
      EXPECT_FALSE(message->isHidden());
    }
  });
  QTimer locale_change;
  locale_change.setSingleShot(true);
  QObject::connect(&locale_change, &QTimer::timeout, &expiry_loop, [&] {
    const int remaining_before = hold_timer->remainingTime();
    EXPECT_TRUE(editor.preferences().set_locale("it-IT").ok());
    ui.refresh({}, {});
    EXPECT_LE(hold_timer->remainingTime(), remaining_before)
        << "localization must not restart the existing diagnostic hold";
    EXPECT_FALSE(message->isHidden());
    EXPECT_EQ(announcements, 8);
  });
  QTimer expiry_deadline;
  expiry_deadline.setSingleShot(true);
  QObject::connect(&expiry_deadline, &QTimer::timeout, &expiry_loop, [&] {
    ADD_FAILURE()
        << "the diagnostic hold did not expire within its notification timeout; elapsed="
        << elapsed.elapsed() << ", hold timeouts=" << hold_timeouts
        << ", timer active=" << hold_timer->isActive()
        << ", remaining=" << hold_timer->remainingTime()
        << ", fade state=" << static_cast<int>(fade->state())
        << ", fade time=" << fade->currentTime();
    expiry_loop.quit();
  });
  expiry_observer.start();
  locale_change.start(3000);
  expiry_deadline.start(6500);
  expiry_loop.exec();
  expiry_observer.stop();
  locale_change.stop();
  expiry_deadline.stop();
  ASSERT_EQ(hold_timeouts, 1);
  EXPECT_EQ(hold_timer->interval(), 5000);
  EXPECT_EQ(hold_timer->timerType(), Qt::PreciseTimer);
  // Compare on Qt's monotonic millisecond time base. Truncating the elapsed
  // duration independently can report 4999 after the 5000th clock tick.
  QElapsedTimer hold_finished;
  hold_finished.start();
  EXPECT_GE(hold_finished.msecsSinceReference() - hold_started_ms, 5000);
  EXPECT_FALSE(hold_timer->isActive());
  ASSERT_EQ(fade->state(), QAbstractAnimation::Running);
  EXPECT_EQ(fade->duration(), 200);
  EXPECT_TRUE(opacity->isEnabled());
  EXPECT_FALSE(message->isHidden());
  EXPECT_TRUE(hints->isHidden());
  fade->setCurrentTime(fade->duration() / 2);
  EXPECT_NEAR(opacity->opacity(), 0.5, 0.01);
  for (int frame = 0; frame < 8; ++frame) {
    ui.refresh({}, {});
  }
  EXPECT_EQ(fade->state(), QAbstractAnimation::Running);
  EXPECT_EQ(fade->currentTime(), fade->duration() / 2);
  EXPECT_NEAR(opacity->opacity(), 0.5, 0.01);
  EXPECT_FALSE(message->isHidden());
  EXPECT_TRUE(hints->isHidden());
  fade->setCurrentTime(fade->duration());
  EXPECT_TRUE(message->text().isEmpty());
  EXPECT_TRUE(message->isHidden());
  EXPECT_FALSE(message->is_error());
  EXPECT_FALSE(hints->isHidden())
      << "expiry must restore hints without waiting for another app frame";
  EXPECT_EQ(announcements, 8);
  EXPECT_EQ(editor.commands().last_status_revision(), expiring_revision);
  ASSERT_TRUE(editor.preferences().set_locale("en-US").ok());
  ui.refresh({}, {});
  EXPECT_TRUE(message->text().isEmpty());
  EXPECT_TRUE(message->isHidden());
  EXPECT_FALSE(hints->isHidden());
  EXPECT_EQ(announcements, 8);
  ui.shutdown();
}

TEST(QtProjectWorkspaceTests,
     AlignImmediateActionWarnsOnlyOnAttemptAndLocalizesUntilNextSuccess) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  const auto isolated_profile = std::filesystem::temp_directory_path() /
      ("quader-align-status-warning-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  ScopedIsolatedLocalAppData scoped_profile(isolated_profile);
  ASSERT_TRUE(scoped_profile.active());

  int argc = 1;
  char application_name[] = "quader-align-status-warning-test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  const auto resource_root = std::filesystem::path(QUADER_SOURCE_DIR) / "resources";
  const auto project_root = scoped_profile.profile() / "Align Status Project";
  ASSERT_TRUE(std::filesystem::create_directory(project_root));
  quader::RenderSystem render_system;
  quader::ProjectSession project;
  project.set_resource_root(resource_root / "modeling_assets");
  quader::QuaderEditorApi editor(project);
  quader::UISystem ui;
  ASSERT_TRUE(ui.initialize(render_system, resource_root));
  ASSERT_NE(ui.main_window(), nullptr);
  ui.main_window()->setAttribute(Qt::WA_DontShowOnScreen, true);
  ASSERT_TRUE(editor.project().create(project_root).ok());
  ASSERT_NE(project.active_scene(), nullptr);
  auto &scene = *project.active_scene()->session;
  ui.install_runtime_hooks(editor, scene.scene(), scene.assets(), scene.modeling(),
      scene.selection(), scene.native_tools(), scene.point_placement(),
      quader::kInvalidAssetId);
  auto *message = dynamic_cast<quader::QtStatusMessage *>(
      ui.main_window()->findChild<QLabel *>(QStringLiteral("statusMessage")));
  ASSERT_NE(message, nullptr);
  message->set_error_sound_callback([] {});

  constexpr std::array<float, 3> kPositions{0.0f, 1.0f, 7.1f};
  std::array<quader::EntityId, 3> entities;
  for (std::size_t index = 0; index < entities.size(); ++index) {
    const auto created = editor.modeling().create_box(
        {.min = {-1, -1, -1}, .max = {1, 1, 1}},
        "Align warning source " + std::to_string(index));
    ASSERT_TRUE(created.ok()) << created.error_message();
    entities[index] = created.value().entity;
    ASSERT_TRUE(editor.scene().set_transform(entities[index],
        {.location = {kPositions[index], 0, 0}}).ok());
  }
  ASSERT_TRUE(editor.selection().select_objects(entities, entities[1]).ok());
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Align).ok());
  ASSERT_TRUE(editor.tools().set_setting("align", "align.use_snapping", true).ok());
  ui.refresh({}, {});
  EXPECT_TRUE(message->isHidden());
  ASSERT_TRUE(editor.tools().execute_action("align", "align.distribute.x").ok());
  ui.refresh({}, {});
  ASSERT_FALSE(message->isHidden());
  EXPECT_FALSE(message->is_error());
  const QString english_warning = message->text();
  ASSERT_EQ(english_warning,
            QStringLiteral("No suitable grid size was found. Grid alignment was not used."));

  EXPECT_TRUE(scene.native_tools().active("align"));
  for (int frame = 0; frame < 3; ++frame) {
    ui.refresh({}, {});
    EXPECT_FALSE(message->isHidden());
    EXPECT_FALSE(message->is_error());
    EXPECT_EQ(message->text(), english_warning);
  }

  ASSERT_TRUE(editor.preferences().set_locale("it-IT").ok());
  quader::LocalizationService italian;
  ASSERT_TRUE(italian.load(resource_root / "locales", "it-IT"));
  const QString italian_warning = QString::fromStdString(
      italian.text("tool_settings.align.snap_unavailable", "Missing warning translation"));
  ASSERT_NE(italian_warning, english_warning);
  ui.refresh({}, {});
  EXPECT_FALSE(message->isHidden());
  EXPECT_FALSE(message->is_error());
  EXPECT_EQ(message->text(), italian_warning);

  ASSERT_TRUE(editor.commands().execute(
      quader::quader_command::kViewportToggleGrid).ok());
  ui.refresh({}, {});
  EXPECT_TRUE(message->text().isEmpty());
  EXPECT_TRUE(message->isHidden());
  EXPECT_FALSE(message->is_error());
  ASSERT_TRUE(editor.preferences().set_locale("en-US").ok());
  ui.shutdown();
}

TEST(QtProjectWorkspaceTests,
     StatusHintsKeepBarAndWorkspaceGeometryStableAcrossContextsAndScale) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  const auto isolated_profile = std::filesystem::temp_directory_path() /
      ("quader-status-hint-geometry-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  ScopedIsolatedLocalAppData scoped_profile(isolated_profile);
  ASSERT_TRUE(scoped_profile.active());
  int argc = 1;
  char application_name[] = "quader-status-hint-geometry-test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  const auto resource_root = std::filesystem::path(QUADER_SOURCE_DIR) / "resources";
  quader::RenderSystem render_system;
  quader::ProjectSession project;
  project.set_resource_root(resource_root / "modeling_assets");
  quader::QuaderEditorApi editor(project);
  const auto project_root = isolated_profile / "Status hint geometry project";
  ASSERT_TRUE(std::filesystem::create_directory(project_root));
  ASSERT_TRUE(editor.project().create(project_root).ok());
  ASSERT_NE(project.active_scene(), nullptr);
  auto &scene = *project.active_scene()->session;
  quader::UISystem ui;
  ASSERT_TRUE(ui.initialize(render_system, resource_root));
  ui.main_window()->setAttribute(Qt::WA_DontShowOnScreen, true);
  ui.install_runtime_hooks(editor, scene.scene(), scene.assets(), scene.modeling(),
      scene.selection(), scene.native_tools(), scene.point_placement(),
      quader::kInvalidAssetId);
  ui.main_window()->resize(1800, 1100);
  auto *status_bar = ui.main_window()->statusBar();
  QWidget *workspace = ui.main_window()->centralWidget();
  ASSERT_NE(workspace, nullptr);
  auto *workspace_stack = qobject_cast<QStackedWidget *>(workspace);
  QWidget *scene_workspace = workspace->findChild<QWidget *>(
      QStringLiteral("sceneWorkspace"));
  ASSERT_NE(workspace_stack, nullptr);
  ASSERT_NE(scene_workspace, nullptr);
  // Scene rendering requires an initialized GPU. Exercise the retained native
  // workspace and status-bar layout directly without starting that lifecycle.
  workspace_stack->setCurrentWidget(scene_workspace);
  status_bar->show();
  ui.main_window()->show();
  auto *hints = dynamic_cast<quader::QtOperationInputHints *>(
      status_bar->findChild<QWidget *>(QStringLiteral("operationInputHints")));
  auto *message = dynamic_cast<quader::QtStatusMessage *>(
      status_bar->findChild<QLabel *>(QStringLiteral("statusMessage")));
  ASSERT_NE(hints, nullptr);
  ASSERT_NE(message, nullptr);
  message->set_error_sound_callback([] {});

  QWidget hint_surface;
  hint_surface.setAttribute(Qt::WA_DontShowOnScreen, true);
  hint_surface.setMouseTracking(true);
  hint_surface.resize(120, 30);
  std::uint64_t context_revision = 1;
  std::vector<quader::QuaderOperationInputHint> context_hints;
  quader::qt_register_contextual_input_hints(
      hint_surface, hint_surface,
      [&](Qt::KeyboardModifiers) {
        return quader::QtContextualInputHintSourceState{
            context_revision, true, true};
      },
      [&](Qt::KeyboardModifiers) { return context_hints; });
  hint_surface.show();
  const auto receive_context = [&] {
    send_mouse_event(hint_surface, QEvent::MouseMove, QPoint(10, 10),
        Qt::NoButton, Qt::NoButton);
    ui.refresh({}, {});
  };
  const auto settle_layout = [&] {
    for (int delivery = 0; delivery < 8; ++delivery) {
      QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
      QCoreApplication::processEvents();
    }
  };
  receive_context();
  settle_layout();

  class GeometryObserver final : public QObject {
  public:
    struct Sample {
      const QWidget *widget;
      QRect geometry;
    };
    bool recording = false;
    std::vector<Sample> samples;
  protected:
    bool eventFilter(QObject *target, QEvent *event) override {
      if (recording && (event->type() == QEvent::Move ||
                        event->type() == QEvent::Resize ||
                        event->type() == QEvent::Show)) {
        auto *widget = qobject_cast<QWidget *>(target);
        if (widget != nullptr && widget->isVisible()) {
          samples.push_back({widget, widget->geometry()});
        }
      }
      return false;
    }
  } observer;
  status_bar->installEventFilter(&observer);
  workspace->installEventFilter(&observer);
  hints->installEventFilter(&observer);
  const QString review_directory = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
  if (!review_directory.isEmpty()) {
    ASSERT_TRUE(QDir().mkpath(review_directory));
  }
  std::array<int, 3> status_heights{};
  const std::array scale_factors{1.0, 2.0, 1.0};
  for (std::size_t stage = 0; stage < scale_factors.size(); ++stage) {
    SCOPED_TRACE("scale stage " + std::to_string(stage));
    observer.recording = false;
    context_hints.clear();
    ++context_revision;
    ASSERT_TRUE(editor.ui().notify({}).ok());
    ASSERT_TRUE(editor.preferences().set_interface_scale(scale_factors[stage]).ok());
    receive_context();
    settle_layout();
    ASSERT_TRUE(status_bar->isVisible());
    ASSERT_TRUE(hints->hints().empty());
    ASSERT_TRUE(hints->isHidden());
    ASSERT_TRUE(message->isHidden());
    const QRect status_geometry = status_bar->geometry();
    const QRect workspace_geometry = workspace->geometry();
    ASSERT_GT(status_geometry.height(), 0);
    ASSERT_GT(workspace_geometry.height(), 0);
    status_heights[stage] = status_geometry.height();
    std::optional<QRect> hint_row_geometry;
    observer.samples.clear();
    observer.recording = true;
    const auto expect_stable_geometry = [&] {
      EXPECT_EQ(status_bar->geometry(), status_geometry);
      EXPECT_EQ(workspace->geometry(), workspace_geometry);
      if (!hints->isHidden()) {
        if (!hint_row_geometry.has_value()) {
          hint_row_geometry = hints->geometry();
        }
        EXPECT_EQ(hints->y(), hint_row_geometry->y());
        EXPECT_EQ(hints->height(), hint_row_geometry->height());
        EXPECT_GE(hints->height(), hints->sizeHint().height());
      }
      for (QAbstractAnimation *animation :
           ui.main_window()->findChildren<QAbstractAnimation *>()) {
        const auto *property_animation = qobject_cast<QPropertyAnimation *>(animation);
        if (property_animation != nullptr &&
            property_animation->propertyName() == "opacity") {
          continue;
        }
        EXPECT_NE(animation->state(), QAbstractAnimation::Running)
            << "a status-context change must not animate the workspace";
      }
    };
    const auto inspect_transition = [&] {
      expect_stable_geometry();
      for (int delivery = 0; delivery < 8; ++delivery) {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        QCoreApplication::processEvents();
        expect_stable_geometry();
      }
      for (const auto &sample : observer.samples) {
        if (sample.widget == status_bar) {
          EXPECT_EQ(sample.geometry, status_geometry);
        } else if (sample.widget == workspace) {
          EXPECT_EQ(sample.geometry, workspace_geometry);
        } else if (hint_row_geometry.has_value()) {
          EXPECT_EQ(sample.geometry.y(), hint_row_geometry->y());
          EXPECT_EQ(sample.geometry.height(), hint_row_geometry->height());
        }
      }
      observer.samples.clear();
    };
    const auto capture = [&](const QString &state) {
      if (!review_directory.isEmpty()) {
        EXPECT_TRUE(status_bar->grab().save(QDir(review_directory).filePath(
            QStringLiteral("status-hint-geometry-%1-%2.png")
                .arg(static_cast<int>(stage)).arg(state))));
      }
    };

    capture(QStringLiteral("empty"));
    context_hints = {{quader::QuaderOperationInputGesture::MiddleDrag, {},
        "test.status.orbit", "Orbit"}};
    ++context_revision;
    receive_context();
    ASSERT_FALSE(hints->isHidden());
    inspect_transition();
    capture(QStringLiteral("one-hint"));

    context_hints = {
        {quader::QuaderOperationInputGesture::Shift, {},
         "test.status.precision", "Precision", 100, true},
        {quader::QuaderOperationInputGesture::Control, {},
         "test.status.snap", "Grid Snap"},
        {quader::QuaderOperationInputGesture::EscapeOrRightClick, {},
         "test.status.cancel", "Cancel Value"}};
    ++context_revision;
    receive_context();
    ASSERT_EQ(hints->hints().size(), 3u);
    inspect_transition();
    capture(QStringLiteral("changed-hints"));

    for (const auto level : {quader::QuaderNotificationLevel::Error,
                            quader::QuaderNotificationLevel::Warning}) {
      ASSERT_TRUE(editor.ui().notify({.level = level,
          .message = "Unable to complete the requested operation."}).ok());
      ASSERT_FALSE(message->isHidden());
      ASSERT_TRUE(hints->isHidden());
      inspect_transition();
      capture(level == quader::QuaderNotificationLevel::Error
          ? QStringLiteral("error") : QStringLiteral("warning"));
    }
    ASSERT_TRUE(editor.ui().notify({.message = "Operation completed."}).ok());
    ASSERT_TRUE(message->isHidden());
    ASSERT_FALSE(hints->isHidden());
    inspect_transition();
    capture(QStringLiteral("restored-hints"));

    context_hints.clear();
    ++context_revision;
    receive_context();
    ASSERT_TRUE(hints->isHidden());
    ASSERT_TRUE(message->isHidden());
    inspect_transition();
    capture(QStringLiteral("empty-again"));
  }
  observer.recording = false;
  EXPECT_GT(status_heights[1], status_heights[0]);
  EXPECT_EQ(status_heights[2], status_heights[0]);
  ui.shutdown();
}

TEST(QtProjectWorkspaceTests,
     OutlinerLightIconsFollowSubtypeChangesThroughRetainedFrameRefresh) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  const auto isolated_profile = std::filesystem::temp_directory_path() /
      ("quader-outliner-icon-refresh-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  ScopedIsolatedLocalAppData scoped_profile(isolated_profile);
  ASSERT_TRUE(scoped_profile.active());
  int argc = 1;
  char application_name[] = "quader-outliner-icon-refresh-test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  const auto resource_root = std::filesystem::path(QUADER_SOURCE_DIR) / "resources";
  quader::RenderSystem render_system;
  quader::ProjectSession project;
  project.set_resource_root(resource_root / "modeling_assets");
  quader::QuaderEditorApi editor(project);
  quader::UISystem ui;
  ASSERT_TRUE(ui.initialize(render_system, resource_root));
  ui.main_window()->setAttribute(Qt::WA_DontShowOnScreen, true);
  const auto project_root = scoped_profile.profile() / "Outliner Icon Project";
  ASSERT_TRUE(std::filesystem::create_directory(project_root));
  ASSERT_TRUE(editor.project().create(project_root).ok());
  ASSERT_NE(project.active_scene(), nullptr);
  auto &scene = *project.active_scene()->session;
  ui.install_runtime_hooks(editor, scene.scene(), scene.assets(), scene.modeling(),
      scene.selection(), scene.native_tools(), scene.point_placement(),
      quader::kInvalidAssetId);
  ui.show_workspace();
  ui.main_window()->show();
  ASSERT_TRUE(editor.ui().show_panel("outliner").ok());
  const auto light = editor.scene().create_point_light();
  ASSERT_TRUE(light.ok()) << light.error_message();
  QCoreApplication::processEvents();
  ui.refresh({}, {});
  auto *tree = ui.main_window()->findChild<QTreeView *>(
      QStringLiteral("outliner.tree"));
  ASSERT_NE(tree, nullptr);
  ASSERT_TRUE(tree->isVisible());
  ASSERT_EQ(tree->model()->rowCount(), 1);
  const QPersistentModelIndex row(tree->model()->index(0, 0));
  const auto point_icon = row.data(Qt::DecorationRole).value<QIcon>();
  ASSERT_FALSE(point_icon.pixmap(16, 16).isNull());

  int model_resets = 0;
  int decoration_changes = 0;
  QObject::connect(tree->model(), &QAbstractItemModel::modelReset, &application,
                   [&]() { ++model_resets; });
  QObject::connect(tree->model(), &QAbstractItemModel::dataChanged, &application,
      [&](const QModelIndex &, const QModelIndex &, const QList<int> &roles) {
        if (roles.isEmpty() || roles.contains(Qt::DecorationRole)) {
          ++decoration_changes;
        }
      });
  const auto hierarchy_revision = scene.scene().hierarchy_presentation_revision();
  ASSERT_TRUE(editor.scene().set_light_component(light.value().id,
      quader::default_quader_directional_light_component()).ok());
  EXPECT_EQ(scene.scene().hierarchy_presentation_revision(), hierarchy_revision);
  ui.refresh({}, {});
  ASSERT_TRUE(row.isValid());
  const auto sun_icon = row.data(Qt::DecorationRole).value<QIcon>();
  EXPECT_NE(sun_icon.cacheKey(), point_icon.cacheKey());
  EXPECT_EQ(decoration_changes, 1);
  EXPECT_EQ(model_resets, 0);
  for (int frame = 0; frame < 3; ++frame) {
    ui.refresh({}, {});
  }
  EXPECT_EQ(decoration_changes, 1);
  EXPECT_EQ(row.data(Qt::DecorationRole).value<QIcon>().cacheKey(),
            sun_icon.cacheKey());
  ASSERT_TRUE(editor.history().undo().ok());
  ui.refresh({}, {});
  EXPECT_EQ(row.data(Qt::DecorationRole).value<QIcon>().cacheKey(),
            point_icon.cacheKey());
  EXPECT_EQ(decoration_changes, 2);
  EXPECT_EQ(model_resets, 0);
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
  auto *group_mode_action = ui.main_window()->findChild<QAction *>(
      QStringLiteral("selectionModeGroup"));
  ASSERT_NE(counter, nullptr);
  ASSERT_NE(separator, nullptr);
  ASSERT_NE(version, nullptr);
  ASSERT_NE(vertex_mode_action, nullptr);
  ASSERT_NE(face_mode_action, nullptr);
  ASSERT_NE(group_mode_action, nullptr);
  EXPECT_NE(ui.main_window()->findChild<QAction *>(
      QStringLiteral("selectionModeMesh")), nullptr);
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
        ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Object).ok());
        const bool components = mode == quader::QuaderSelectionMode::Vertex ||
            mode == quader::QuaderSelectionMode::Edge ||
            mode == quader::QuaderSelectionMode::Face;
        if (components) {
          ASSERT_TRUE(editor.selection().select_object(mesh.value()).ok());
          ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Edit).ok());
        }
        ASSERT_TRUE(editor.selection().set_mode(mode).ok());
        ASSERT_TRUE(editor.selection().clear().ok());
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
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Object).ok());
  ASSERT_TRUE(editor.selection().select_object(light.value().id).ok());
  const auto mode_before_empty_entry = editor.mode().state();
  const auto empty_entry = editor.mode().set(quader::QuaderEditorMode::Edit);
  EXPECT_TRUE(empty_entry.ok());
  EXPECT_FALSE(empty_entry.changed());
  EXPECT_EQ(editor.mode().current(), quader::QuaderEditorMode::Object);
  EXPECT_EQ(editor.mode().revision(), mode_before_empty_entry.revision);
  EXPECT_EQ(editor.mode().generation(), mode_before_empty_entry.generation);
  ui.refresh({}, {});
  EXPECT_FALSE(group_mode_action->isChecked());
  EXPECT_EQ(counter->text(), QStringLiteral("1 Mesh Selected"));
  ASSERT_TRUE(editor.selection().select_objects(
      {mesh.value().entity, light.value().id}).ok());
  ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Edit).ok());
  for (const auto mode : {quader::QuaderSelectionMode::Face,
                         quader::QuaderSelectionMode::Vertex}) {
    ASSERT_TRUE(editor.selection().set_mode(mode).ok());
    ASSERT_TRUE(editor.selection().clear().ok());
    ASSERT_EQ(editor.selection().summary().mode, mode);
    ASSERT_TRUE(editor.mode().state().participants.empty());
    ASSERT_EQ(scene.modeling().selection_mode(),
              mode == quader::QuaderSelectionMode::Face
                  ? quader::ModelingSelectionMode::Face
                  : quader::ModelingSelectionMode::Vertex);
    ui.refresh({}, {});
    EXPECT_EQ(face_mode_action->isChecked(),
              mode == quader::QuaderSelectionMode::Face);
    EXPECT_EQ(vertex_mode_action->isChecked(),
              mode == quader::QuaderSelectionMode::Vertex);
    EXPECT_FALSE(group_mode_action->isChecked());
    EXPECT_EQ(counter->text(),
              mode == quader::QuaderSelectionMode::Face
                  ? QStringLiteral("0 Faces Selected")
                  : QStringLiteral("0 Vertices Selected"));
  }
  ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Object).ok());
  ASSERT_TRUE(editor.selection().clear().ok());
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Object).ok());
  ASSERT_TRUE(editor.selection().select_objects(
      {mesh.value().entity, light.value().id, curve.value().entity}).ok());
  ui.refresh({}, {});
  EXPECT_EQ(counter->text(), QStringLiteral("3 Meshes Selected"));
  group_mode_action->trigger();
  EXPECT_EQ(editor.selection().summary().mode, quader::QuaderSelectionMode::Group);
  EXPECT_TRUE(group_mode_action->isChecked());
  group_mode_action->trigger();
  EXPECT_EQ(editor.selection().summary().mode, quader::QuaderSelectionMode::Object);
  EXPECT_FALSE(group_mode_action->isChecked());
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Group).ok());
  ASSERT_TRUE(editor.selection().select_objects(
      {first_group.value().id, first_group.value().id,
       mesh.value().entity, light.value().id, curve.value().entity}).ok());
  ui.refresh({}, {});
  EXPECT_EQ(counter->text(), QStringLiteral("1 Group Selected"));
  EXPECT_TRUE(group_mode_action->isChecked());

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
     OperationSettingsDispatchSurvivesTheRealUiFrameRefresh) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  const auto isolated_profile = std::filesystem::temp_directory_path() /
      ("quader-ui-live-operation-settings-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  ScopedIsolatedLocalAppData scoped_profile(isolated_profile);
  ASSERT_TRUE(scoped_profile.active());
  int argc = 1;
  char application_name[] = "quader-live-operation-settings-test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  const auto resource_root = std::filesystem::path(QUADER_SOURCE_DIR) / "resources";
  quader::RenderSystem render_system;
  quader::ProjectSession project;
  project.set_resource_root(resource_root / "modeling_assets");
  quader::QuaderEditorApi editor(project);
  quader::UISystem ui;
  ASSERT_TRUE(ui.initialize(render_system, resource_root));
  ui.main_window()->setAttribute(Qt::WA_DontShowOnScreen, true);
  const auto project_root = scoped_profile.profile() / "Live settings project";
  ASSERT_TRUE(std::filesystem::create_directory(project_root));
  ASSERT_TRUE(editor.project().create(project_root).ok());
  ASSERT_NE(project.active_scene(), nullptr);
  auto &scene = *project.active_scene()->session;
  ui.install_runtime_hooks(editor, scene.scene(), scene.assets(), scene.modeling(),
      scene.selection(), scene.native_tools(), scene.point_placement(),
      quader::kInvalidAssetId);
  ui.show_workspace();
  ui.main_window()->show();
  ui.main_window()->activateWindow();
  auto *settings = dynamic_cast<quader::QtToolSettingsPanel *>(
      ui.main_window()->findChild<QWidget *>(
          QStringLiteral("viewport.settings_overlay")));
  ASSERT_NE(settings, nullptr);
  settings->set_expanded(true);

  const auto source = editor.modeling().create_box(
      {.min = {-1, -1, -1}, .max = {1, 1, 1}}, "Live operation settings");
  ASSERT_TRUE(source.ok());
  ASSERT_TRUE(editor.selection().select_object(source.value()).ok());
  ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Edit).ok());
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Edge).ok());
  ASSERT_TRUE(editor.selection().select_edge(source.value(), 1, 2).ok());
  ASSERT_TRUE(editor.selection().select_all().ok());
  ASSERT_TRUE(editor.commands().execute(
      editor.commands().command_id_for_modeling_action("bevel_edges")).ok());
  ASSERT_TRUE(editor.tools().set_setting(
      "edge_bevel", "operation.edge_bevel.width", 0.1).ok());
  ui.refresh({}, {});
  QCoreApplication::processEvents();
  const auto before = editor.query().compiled_object_mesh(source.value());
  ASSERT_TRUE(before.has_value());
  auto *width = settings->findChild<QDoubleSpinBox *>(QStringLiteral(
      "tool_settings.control.operation.edge_bevel.width"));
  ASSERT_NE(width, nullptr);
  ASSERT_TRUE(width->isEnabled());
  click_widget(*width, width->rect().center());
  send_key(*width, Qt::Key_Up);
  const double edited_width = width->value();
  ASSERT_GT(edited_width, 0.1);
  ASSERT_TRUE(settings->numeric_interaction_active());
  const auto settings_revision = editor.tools().active_settings_revision();
  const auto tool_revision = editor.tools().active_tool_revision();
  const auto origin = editor.tools().active_settings_presentation_origin();
  for (int frame = 0; frame < 3; ++frame) {
    ui.refresh({}, {});
    ui.process_viewport_input();
    EXPECT_EQ(editor.tools().active_settings_revision(), settings_revision);
    EXPECT_EQ(editor.tools().active_tool_revision(), tool_revision);
    EXPECT_EQ(editor.tools().active_settings_presentation_origin(), origin);
  }
  QEventLoop loop;
  QTimer frames;
  QObject::connect(&frames, &QTimer::timeout, &loop, [&] {
    ui.refresh({}, {});
    ui.process_viewport_input();
  });
  frames.start(5);
  QTimer::singleShot(120, &loop, &QEventLoop::quit);
  loop.exec();
  frames.stop();
  EXPECT_DOUBLE_EQ(scene.modeling().interaction_settings().edge_bevel_width,
                   edited_width);
  EXPECT_TRUE(editor.tools().active_settings().preview_active);
  const auto after = editor.query().compiled_object_mesh(source.value());
  ASSERT_TRUE(after.has_value());
  ASSERT_EQ(after->vertices.size(), before->vertices.size());
  bool position_changed = false;
  for (std::size_t index = 0; index < after->vertices.size(); ++index) {
    const auto &a = before->vertices[index].position;
    const auto &b = after->vertices[index].position;
    position_changed = position_changed || a.x != b.x || a.y != b.y || a.z != b.z;
  }
  EXPECT_TRUE(position_changed);
  ui.shutdown();
}

TEST(QtProjectWorkspaceTests,
     StandaloneNumericDragDefersStructuralInspectorRefreshUntilRelease) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  const auto isolated_profile = std::filesystem::temp_directory_path() /
      ("quader-ui-numeric-drag-retained-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  ScopedIsolatedLocalAppData scoped_profile(isolated_profile);
  ASSERT_TRUE(scoped_profile.active());
  int argc = 1;
  char application_name[] = "quader-numeric-drag-retained-test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  const auto resource_root = std::filesystem::path(QUADER_SOURCE_DIR) / "resources";
  quader::RenderSystem render_system;
  quader::ProjectSession project;
  project.set_resource_root(resource_root / "modeling_assets");
  quader::QuaderEditorApi editor(project);
  const auto project_root = isolated_profile / "Numeric drag project";
  ASSERT_TRUE(std::filesystem::create_directory(project_root));
  ASSERT_TRUE(editor.project().create(project_root).ok());
  const auto source = editor.modeling().create_box(
      {.min = {-1, -1, -1}, .max = {1, 1, 1}}, "Before numeric drag");
  ASSERT_TRUE(source.ok());
  ASSERT_TRUE(editor.selection().select_object(source.value()).ok());
  ASSERT_NE(project.active_scene(), nullptr);
  auto &scene = *project.active_scene()->session;
  quader::UISystem ui;
  ASSERT_TRUE(ui.initialize(render_system, resource_root));
  ui.main_window()->setAttribute(Qt::WA_DontShowOnScreen, true);
  ui.install_runtime_hooks(editor, scene.scene(), scene.assets(), scene.modeling(),
      scene.selection(), scene.native_tools(), scene.point_placement(),
      quader::kInvalidAssetId);
  ui.show();
  ASSERT_TRUE(editor.ui().show_panel("inspector").ok());
  application.processEvents();
  ui.refresh({}, {});
  auto *dock = ui.main_window()->findChild<QDockWidget *>(
      QStringLiteral("dock_inspector"));
  ASSERT_NE(dock, nullptr);
  QPointer<QLineEdit> retained_name = dock->findChild<QLineEdit *>(
      QStringLiteral("inspector.general.name"));
  QPointer<QDoubleSpinBox> retained_position = dock->findChild<QDoubleSpinBox *>(
      QStringLiteral("inspector.transform.location.x"));
  ASSERT_NE(retained_name, nullptr);
  ASSERT_NE(retained_position, nullptr);
  ASSERT_EQ(retained_name->text(), QStringLiteral("Before numeric drag"));

  // A modal or settings field has no tool-panel interaction owner. Its shared
  // numeric gesture must still protect expensive structural presentation.
  quader::QtDoubleSpinBox field;
  field.setAttribute(Qt::WA_DontShowOnScreen, true);
  field.resize(220, 32);
  field.show();
  application.processEvents();
  auto *line = field.findChild<QLineEdit *>();
  ASSERT_NE(line, nullptr);
  const QPoint start = line->rect().center();
  const QPoint crossed = start + QPoint(QApplication::startDragDistance() + 1, 0);
  send_mouse_event(*line, QEvent::MouseButtonPress, start,
                   Qt::LeftButton, Qt::LeftButton);
  send_mouse_event(*line, QEvent::MouseMove, crossed,
                   Qt::NoButton, Qt::LeftButton);
  ASSERT_TRUE(quader::qt_install_numeric_spin_box(field).is_dragging());
  ASSERT_TRUE(editor.scene().rename(source.value().entity,
                                     "After numeric drag").ok());
  for (int frame = 0; frame < 5; ++frame) {
    ui.refresh({}, {});
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    ASSERT_NE(retained_name, nullptr);
    ASSERT_NE(retained_position, nullptr);
    EXPECT_EQ(retained_name->text(), QStringLiteral("Before numeric drag"));
    EXPECT_EQ(dock->findChild<QDoubleSpinBox *>(
                  QStringLiteral("inspector.transform.location.x")),
              retained_position.data());
  }

  send_mouse_event(*line, QEvent::MouseButtonRelease, crossed,
                   Qt::LeftButton, Qt::NoButton);
  ASSERT_FALSE(quader::qt_install_numeric_spin_box(field).is_dragging());
  ui.refresh({}, {});
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  auto *updated_name = dock->findChild<QLineEdit *>(
      QStringLiteral("inspector.general.name"));
  ASSERT_NE(updated_name, nullptr);
  EXPECT_EQ(updated_name->text(), QStringLiteral("After numeric drag"));
  EXPECT_EQ(retained_name, nullptr);
  EXPECT_EQ(retained_position, nullptr);
  ui.shutdown();
}

TEST(QtProjectWorkspaceTests,
     EditModeSettlesInspectorDraftAndRejectsSamplesFromEarlierModeGeneration) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  const auto isolated_profile = std::filesystem::temp_directory_path() /
      ("quader-ui-mode-numeric-draft-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  ScopedIsolatedLocalAppData scoped_profile(isolated_profile);
  ASSERT_TRUE(scoped_profile.active());
  int argc = 1;
  char application_name[] = "quader-mode-numeric-draft-test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  const auto resource_root = std::filesystem::path(QUADER_SOURCE_DIR) / "resources";
  quader::RenderSystem render_system;
  quader::ProjectSession project;
  project.set_resource_root(resource_root / "modeling_assets");
  quader::QuaderEditorApi editor(project);
  const auto project_root = isolated_profile / "Mode draft project";
  ASSERT_TRUE(std::filesystem::create_directory(project_root));
  ASSERT_TRUE(editor.project().create(project_root).ok());
  const auto source = editor.modeling().create_box(
      {.min = {-1, -1, -1}, .max = {1, 1, 1}}, "Mode draft mesh");
  ASSERT_TRUE(source.ok());
  ASSERT_TRUE(editor.selection().select_object(source.value()).ok());
  ASSERT_NE(project.active_scene(), nullptr);
  auto &scene = *project.active_scene()->session;
  quader::UISystem ui;
  ASSERT_TRUE(ui.initialize(render_system, resource_root));
  ui.main_window()->setAttribute(Qt::WA_DontShowOnScreen, true);
  ui.install_runtime_hooks(editor, scene.scene(), scene.assets(), scene.modeling(),
      scene.selection(), scene.native_tools(), scene.point_placement(),
      quader::kInvalidAssetId);
  // Exercise retained Inspector input without starting the App/GPU loading gate.
  ui.main_window()->show();
  ASSERT_TRUE(editor.ui().show_panel("inspector").ok());
  application.processEvents();
  ui.refresh({}, {});
  auto *dock = ui.main_window()->findChild<QDockWidget *>(
      QStringLiteral("dock_inspector"));
  ASSERT_NE(dock, nullptr);
  QPointer<QDoubleSpinBox> position = dock->findChild<QDoubleSpinBox *>(
      QStringLiteral("inspector.transform.location.x"));
  auto *timer = dock->findChild<QTimer *>(
      QStringLiteral("inspector.transform.dispatchTimer"));
  ASSERT_NE(position, nullptr);
  ASSERT_NE(timer, nullptr);
  auto *line = position->findChild<QLineEdit *>();
  ASSERT_NE(line, nullptr);
  const auto initial_position = scene.scene().transform(source.value().entity)->location;
  const QString review_directory = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
  if (!review_directory.isEmpty()) {
    ASSERT_TRUE(QDir().mkpath(review_directory));
  }
  const auto capture_inspector = [&](const QString &suffix) {
    if (!review_directory.isEmpty()) {
      EXPECT_TRUE(dock->grab().save(QDir(review_directory).filePath(
          QStringLiteral("editor-mode-inspector-%1.png").arg(suffix))));
    }
  };
  capture_inspector(QStringLiteral("object-before-draft"));
  const QPoint start = line->rect().center();
  const QPoint crossed = start + QPoint(QApplication::startDragDistance() + 8, 0);
  send_mouse_event(*line, QEvent::MouseButtonPress, start,
                   Qt::LeftButton, Qt::LeftButton);
  send_mouse_event(*line, QEvent::MouseMove, crossed,
                   Qt::NoButton, Qt::LeftButton);
  ASSERT_TRUE(quader::qt_install_numeric_spin_box(*position).is_dragging());
  position->setValue(initial_position.x + 3.0);
  ASSERT_TRUE(timer->isActive());
  const auto draft_value = position->value();
  const auto history_before_transition = scene.history().undo_count();
  const auto generation = editor.mode().generation();
  ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Edit).ok());
  EXPECT_GT(editor.mode().generation(), generation);
  EXPECT_FALSE(timer->isActive());
  EXPECT_FALSE(quader::qt_install_numeric_spin_box(*position).is_dragging());
  const auto settled_transform = *scene.scene().transform(source.value().entity);
  EXPECT_NEAR(settled_transform.location.x, draft_value, 0.0001);
  const auto settled_history = editor.history().snapshot();
  EXPECT_EQ(scene.history().undo_count(), history_before_transition + 1u);
  ASSERT_EQ(editor.mode().state().participants.size(), 1u);
  EXPECT_EQ(editor.selection().summary().selected_vertex_count, 0u);

  // Old fields can still receive queued Qt signals before the retained refresh.
  position->setValue(draft_value + 10.0);
  ASSERT_TRUE(QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection));
  ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Object).ok());
  position->setValue(draft_value + 20.0);
  ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Edit).ok());
  position->setValue(draft_value + 30.0);
  application.processEvents();
  for (int frame = 0; frame < 4; ++frame) {
    ui.refresh({}, {});
  }
  // QTreeWidget retires index widgets with deleteLater. Finish that normal
  // event-loop phase before inspecting the new mode's live controls.
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  EXPECT_EQ(position, nullptr);
  const auto &after = *scene.scene().transform(source.value().entity);
  EXPECT_FLOAT_EQ(after.location.x, settled_transform.location.x);
  EXPECT_FLOAT_EQ(after.location.y, settled_transform.location.y);
  EXPECT_FLOAT_EQ(after.location.z, settled_transform.location.z);
  EXPECT_EQ(editor.history().snapshot().undo_label, settled_history.undo_label);
  EXPECT_EQ(editor.history().snapshot().redo_label, settled_history.redo_label);
  EXPECT_EQ(scene.history().undo_count(), history_before_transition + 1u);
  auto *edit_location = dock->findChild<QDoubleSpinBox *>(
      QStringLiteral("inspector.transform.location.x"));
  ASSERT_NE(edit_location, nullptr);
  EXPECT_TRUE(edit_location->isEnabled());
  EXPECT_NEAR(edit_location->value(), settled_transform.location.x, 0.0001);
  EXPECT_EQ(ui.main_window()->findChild<QComboBox *>(
      QStringLiteral("editorModeCombo")), nullptr);
  auto *vertex_action = ui.main_window()->findChild<QAction *>(
      QStringLiteral("selectionModeVertex"));
  ASSERT_NE(vertex_action, nullptr);
  EXPECT_TRUE(vertex_action->isChecked());
  capture_inspector(QStringLiteral("edit-stale-samples-rejected"));
  ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Object).ok());
  ui.refresh({}, {});
  auto *restored = dock->findChild<QDoubleSpinBox *>(
      QStringLiteral("inspector.transform.location.x"));
  ASSERT_NE(restored, nullptr);
  EXPECT_NEAR(restored->value(), settled_transform.location.x, 0.0001);
  capture_inspector(QStringLiteral("object-settled-draft"));
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
  ASSERT_TRUE(editor.selection().select_object(mesh.value()).ok());
  ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Edit).ok());
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
      QStringLiteral("dock_asset_browser"));
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
  EXPECT_EQ(browser->selected_folder(), QStringLiteral("Materials"));
  EXPECT_FALSE(browser->sidebar_visible());
  EXPECT_EQ(grid->model()->rowCount(), proxy->sourceModel()->rowCount());
  auto *search = browser->findChild<QLineEdit *>(
      QStringLiteral("materialBrowser.search"));
  ASSERT_NE(search, nullptr);
  search->setText(QStringLiteral("Materials/Usage"));
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
     InspectorShowsGizmoRotationDuringDragWithoutReplacingRetainedFields) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  const auto isolated_profile = std::filesystem::temp_directory_path() /
      ("quader-inspector-live-gizmo-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  ScopedIsolatedLocalAppData scoped_profile(isolated_profile);
  ASSERT_TRUE(scoped_profile.active());
  int argc = 1;
  char application_name[] = "quader-inspector-live-gizmo-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  const auto resource_root = std::filesystem::path(QUADER_SOURCE_DIR) / "resources";
  const auto project_root = isolated_profile / "Live Transform Project";
  ASSERT_TRUE(std::filesystem::create_directory(project_root));
  quader::RenderSystem render_system;
  quader::ProjectSession project;
  project.set_resource_root(resource_root / "modeling_assets");
  quader::QuaderEditorApi editor(project);
  const auto created = editor.project().create(project_root);
  ASSERT_TRUE(created.ok()) << created.error_message();
  const auto box = editor.modeling().create_box(
      quader::QuaderBounds{.min = {-1.0f, -2.0f, -3.0f},
                           .max = {1.0f, 2.0f, 3.0f}},
      "Live Inspector Rotation Mesh");
  ASSERT_TRUE(box.ok()) << box.error_message();
  ASSERT_TRUE(editor.selection().select_object(box.value()).ok());
  ASSERT_NE(project.active_scene(), nullptr);
  auto &scene = *project.active_scene()->session;
  quader::UISystem ui;
  ASSERT_TRUE(ui.initialize(render_system, resource_root));
  ASSERT_NE(ui.main_window(), nullptr);
  ui.main_window()->setAttribute(Qt::WA_DontShowOnScreen, true);
  ui.install_runtime_hooks(
      editor, scene.scene(), scene.assets(), scene.modeling(), scene.selection(),
      scene.native_tools(), scene.point_placement(), quader::kInvalidAssetId);
  ui.show();
  ASSERT_TRUE(editor.ui().show_panel("inspector").ok());
  application.processEvents();
  ui.refresh({}, {});
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  auto *dock = ui.main_window()->findChild<QDockWidget *>(
      QStringLiteral("dock_inspector"));
  ASSERT_NE(dock, nullptr);
  ASSERT_TRUE(dock->isVisible());
  const auto rotation_field = [&]() {
    return dock->findChild<QDoubleSpinBox *>(
        QStringLiteral("inspector.transform.rotation.z"));
  };
  QPointer<QDoubleSpinBox> retained_rotation(rotation_field());
  ASSERT_NE(retained_rotation, nullptr);
  EXPECT_NEAR(retained_rotation->value(), 0.0, 0.001);
  const std::size_t undo_before = scene.history().undo_count();
  int field_signals = 0;
  QObject::connect(retained_rotation.data(), &QDoubleSpinBox::valueChanged, dock,
                   [&](double) { ++field_signals; });
  const quader::QuaderViewportCamera camera{
      .orthographic = true,
      .origin = {0.0f, 0.0f, 8.0f},
      .x_axis = {1.0f, 0.0f, 0.0f},
      .y_axis = {0.0f, 1.0f, 0.0f},
      .z_axis = {0.0f, 0.0f, 1.0f},
      .viewport_size = {800.0f, 600.0f},
      .orthographic_size = 8.0,
      .fov_degrees = 60.0,
  };
  const quader::QuaderRay ray{
      .origin = {0.0f, 0.0f, 8.0f},
      .direction = {0.0f, 0.0f, -1.0f},
  };
  const auto begin = editor.modeling().begin_transform_drag({
      .tool = quader::QuaderTransformTool::Rotate,
      .axis = quader::QuaderTransformAxis::Z,
      .screen_position = {120.0f, 100.0f},
      .screen_pivot = {100.0f, 100.0f},
      .screen_tip = {120.0f, 100.0f},
      .has_world_direction = true,
      .world_direction = {0.0f, 0.0f, 1.0f},
      .ray = ray,
      .camera = camera,
      .vertex_snap_radius = 0.0,
      .grid_snap_enabled = false,
      .rotation_snap_degrees = 0.0,
  });
  ASSERT_TRUE(begin.ok()) << begin.error_message();
  ASSERT_TRUE(begin.value().active);
  ASSERT_TRUE(begin.value().scene_transform);
  double previous_display = 0.0;
  for (const quader::QVec2 point :
       std::array<quader::QVec2, 2>{{{140.0f, 80.0f}, {100.0f, 80.0f}}}) {
    const auto update = editor.modeling().update_transform_drag({
        .screen_position = point,
        .ray = ray,
        .camera = camera,
        .vertex_snap_radius = 0.0,
        .grid_snap_enabled = false,
        .rotation_snap_degrees = 0.0,
    });
    ASSERT_TRUE(update.ok()) << update.error_message();
    ASSERT_TRUE(update.value().changed);
    ASSERT_TRUE(scene.modeling().transform_drag_active());
    const auto entity = editor.query().entity(box.value().entity);
    ASSERT_TRUE(entity.has_value());
    double expected = std::fmod(
        static_cast<double>(entity->transform.rotation_euler.z) *
            57.29577951308232,
        360.0);
    if (expected < 0.0) {
      expected += 360.0;
    }
    ASSERT_GT(std::abs(expected - previous_display), 1.0);
    const auto revision_before_refresh = scene.scene().revision();
    ui.refresh({}, {});
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    ASSERT_NE(retained_rotation, nullptr);
    EXPECT_EQ(rotation_field(), retained_rotation.data());
    EXPECT_NEAR(retained_rotation->value(), expected, 0.001);
    EXPECT_EQ(scene.scene().revision(), revision_before_refresh);
    EXPECT_EQ(scene.history().undo_count(), undo_before);
    EXPECT_EQ(field_signals, 0);
    for (int frame = 0; frame < 3; ++frame) {
      ui.refresh({}, {});
    }
    EXPECT_EQ(rotation_field(), retained_rotation.data());
    EXPECT_NEAR(retained_rotation->value(), expected, 0.001);
    EXPECT_EQ(scene.scene().revision(), revision_before_refresh);
    EXPECT_EQ(scene.history().undo_count(), undo_before);
    EXPECT_EQ(field_signals, 0);
    previous_display = expected;
  }
  const auto commit = editor.modeling().commit_transform_drag();
  ASSERT_TRUE(commit.ok()) << commit.error_message();
  EXPECT_FALSE(scene.modeling().transform_drag_active());
  ui.refresh({}, {});
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  ASSERT_NE(rotation_field(), nullptr);
  EXPECT_NEAR(rotation_field()->value(), previous_display, 0.001);
  EXPECT_EQ(scene.history().undo_count(), undo_before + 1u);
  ui.shutdown();
}

TEST(QtProjectWorkspaceTests, FaceTextureBrowserRefreshesAfterDeletingAndReimportingAllMaterials) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  const auto profile = std::filesystem::temp_directory_path() /
      ("quader-face-material-reimport-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  ScopedIsolatedLocalAppData scoped_profile(profile);
  ASSERT_TRUE(scoped_profile.active());
  int argc = 1;
  char application_name[] = "quader-face-material-reimport-test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  const auto resources = std::filesystem::path(QUADER_SOURCE_DIR) / "resources";
  const auto root = profile / "Project";
  const auto recycle = profile / "Recycled";
  ASSERT_TRUE(std::filesystem::create_directory(root));
  ASSERT_TRUE(std::filesystem::create_directory(recycle));
  const std::array source_paths{profile / "First.png", profile / "Second.png"};
  QImage image(3, 2, QImage::Format_RGBA8888);
  for (const auto &source : source_paths) {
    image.fill(Qt::red);
    ASSERT_TRUE(image.save(QString::fromStdWString(source.wstring())));
  }
  quader::ProjectSession project;
  project.set_resource_root(resources / "modeling_assets");
  quader::QuaderEditorApi editor(project);
  ASSERT_TRUE(editor.project().create(root).ok());
  quader::QuaderProjectAssetsApiInternalAccess::set_recycle_port(
      editor.project_assets(), [&](std::span<const std::filesystem::path> sources) {
        quader::ProjectAssetRecycleResult result{.ok = true};
        for (const auto &source : sources) {
          const auto destination = recycle / std::to_string(
              std::chrono::steady_clock::now().time_since_epoch().count());
          std::filesystem::rename(source, destination);
          result.items.push_back({.original_path = source, .recycled = true});
        }
        return result;
      });
  auto imported = editor.project_assets().import_textures_as_materials(source_paths, "Materials");
  ASSERT_TRUE(imported.ok()) << imported.error_message();
  ASSERT_EQ(imported.value().imported.size(), source_paths.size());
  quader::RenderSystem renderer;
  quader::UISystem ui;
  ASSERT_TRUE(ui.initialize(renderer, resources));
  auto *window = ui.main_window();
  window->setAttribute(Qt::WA_DontShowOnScreen, true);
  auto &scene = *project.active_scene()->session;
  ui.install_runtime_hooks(editor, scene.scene(), scene.assets(), scene.modeling(),
      scene.selection(), scene.native_tools(), scene.point_placement(), quader::kInvalidAssetId);
  auto *browser = dynamic_cast<quader::QtMaterialBrowserWindow *>(
      window->findChild<QWidget *>(QStringLiteral("materialBrowser.embedded")));
  ASSERT_NE(browser, nullptr);
  auto *grid = browser->findChild<QListView *>(QStringLiteral("materialBrowser.gridView"));
  ASSERT_NE(grid, nullptr);
  auto *proxy = dynamic_cast<quader::QtMaterialBrowserProxyModel *>(grid->model());
  ASSERT_NE(proxy, nullptr);
  auto *model = dynamic_cast<quader::QtMaterialBrowserModel *>(proxy->sourceModel());
  ASSERT_NE(model, nullptr);
  ui.show();
  ASSERT_TRUE(editor.ui().show_panel("face_texture").ok());
  application.processEvents();
  ui.refresh({}, {});
  const auto expect_imported_rows = [&] {
    EXPECT_EQ(model->rowCount(), static_cast<int>(source_paths.size()) + 1);
    EXPECT_EQ(proxy->rowCount(), model->rowCount());
    for (const auto &entry : imported.value().imported) {
      const auto reference = quader::QuaderMaterialReference::project(entry.material.id);
      EXPECT_TRUE(std::ranges::any_of(model->items(), [&](const auto &item) {
        return item.reference == reference;
      }));
    }
  };
  expect_imported_rows();
  for (const bool hidden : {false, true}) {
    SCOPED_TRACE(hidden);
    if (hidden) ASSERT_TRUE(editor.ui().hide_panel("face_texture").ok());
    std::vector<std::string> paths;
    for (const auto &entry : imported.value().imported) paths.push_back(entry.material.project_relative_path);
    const auto preview = editor.project_assets().preview_delete_assets(paths);
    ASSERT_TRUE(preview.ok()) << preview.error_message();
    ASSERT_TRUE(preview.value().complete);
    const auto deleted = editor.project_assets().delete_assets(paths, preview.value().revision);
    ASSERT_TRUE(deleted.ok()) << deleted.error_message();
    const auto remaining = editor.project_assets().materials();
    ASSERT_TRUE(remaining.ok());
    EXPECT_EQ(remaining.value().size(), 1u);
    ui.refresh({}, {});
    if (!hidden) {
      EXPECT_EQ(model->rowCount(), 1);
      EXPECT_EQ(proxy->rowCount(), 1);
    }
    imported = editor.project_assets().import_textures_as_materials(source_paths, "Materials");
    ASSERT_TRUE(imported.ok()) << imported.error_message();
    ASSERT_TRUE(imported.value().failed.empty());
    ASSERT_EQ(imported.value().imported.size(), source_paths.size());
    ui.refresh({}, {});
    if (hidden) ASSERT_TRUE(editor.ui().show_panel("face_texture").ok());
    ui.refresh({}, {});
    expect_imported_rows();
    int resets = 0;
    const auto connection = QObject::connect(model, &QAbstractItemModel::modelReset,
        browser, [&] { ++resets; });
    const auto builds = ui.material_presentation_snapshot_build_count();
    for (int frame = 0; frame < 8; ++frame) ui.refresh({}, {});
    EXPECT_EQ(resets, 0);
    EXPECT_EQ(ui.material_presentation_snapshot_build_count(), builds);
    QObject::disconnect(connection);
  }
  // Exercise the Asset Browser's actual external-drop Import route too.
  std::vector<std::string> material_paths;
  for (const auto &entry : imported.value().imported) {
    material_paths.push_back(entry.material.project_relative_path);
  }
  const auto deletion = editor.project_assets().preview_delete_assets(material_paths);
  ASSERT_TRUE(deletion.ok());
  ASSERT_TRUE(deletion.value().complete);
  ASSERT_TRUE(editor.project_assets().delete_assets(
      material_paths, deletion.value().revision).ok());
  ASSERT_TRUE(editor.project_assets().create_folder("", "Imported Materials").ok());
  ASSERT_TRUE(editor.ui().show_panel("asset_browser").ok());
  ui.refresh({}, {});
  application.processEvents();
  auto *asset_browser = dynamic_cast<quader::QtAssetBrowser *>(
      window->findChild<QWidget *>(QStringLiteral("assetBrowser")));
  ASSERT_NE(asset_browser, nullptr);
  asset_browser->set_selected_folder(QStringLiteral("Imported Materials"));
  auto *asset_grid = asset_browser->findChild<QListView *>(QStringLiteral("assetBrowser.grid"));
  ASSERT_NE(asset_grid, nullptr);
  QMimeData images;
  QList<QUrl> urls;
  for (const auto &source : source_paths) {
    urls.push_back(QUrl::fromLocalFile(QString::fromStdWString(source.wstring())));
  }
  const auto drop_images = [&] {
    images.setUrls(urls);
    const QPoint point(asset_grid->viewport()->width() - 5,
        asset_grid->viewport()->height() - 5);
    QDragEnterEvent enter(point, Qt::CopyAction, &images, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(asset_grid->viewport(), &enter);
    EXPECT_TRUE(enter.isAccepted());
    QDropEvent drop(QPointF(point), Qt::CopyAction, &images, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(asset_grid->viewport(), &drop);
    EXPECT_TRUE(drop.isAccepted());
    ui.refresh({}, {});
  };
  for (const int batch : {1, 2}) {
    drop_images();
    EXPECT_EQ(asset_grid->model()->rowCount(), batch * static_cast<int>(source_paths.size()));
    EXPECT_EQ(model->rowCount(), batch * static_cast<int>(source_paths.size()) + 1);
    EXPECT_EQ(proxy->rowCount(), model->rowCount());
  }
  const auto imported_catalog = editor.project_assets().assets();
  ASSERT_TRUE(imported_catalog.ok());
  material_paths.clear();
  for (const auto &asset : imported_catalog.value().assets) {
    if (!asset.project_relative_path.starts_with("Imported Materials/")) continue;
    ASSERT_EQ(asset.kind, quader::QuaderProjectAssetKind::Material);
    material_paths.push_back(asset.project_relative_path);
    const auto albedo = editor.project_assets().material_albedo_texture_info(asset.asset_id);
    ASSERT_TRUE(albedo.ok()) << albedo.error_message();
    EXPECT_TRUE(albedo.value().texture_id.valid());
    EXPECT_TRUE(std::filesystem::exists(albedo.value().absolute_path));
  }
  ASSERT_EQ(material_paths.size(), source_paths.size() * 2u);
  const auto deleted_batch = editor.project_assets().preview_delete_assets(material_paths);
  ASSERT_TRUE(deleted_batch.ok());
  ASSERT_TRUE(deleted_batch.value().complete);
  ASSERT_TRUE(editor.project_assets().delete_assets(
      material_paths, deleted_batch.value().revision).ok());
  ASSERT_TRUE(editor.ui().hide_panel("face_texture").ok());
  ui.refresh({}, {});
  const auto invalid_source = profile / "Broken.png";
  {
    std::ofstream broken(invalid_source, std::ios::binary);
    broken << "Not a PNG image";
  }
  urls.push_back(QUrl::fromLocalFile(QString::fromStdWString(invalid_source.wstring())));
  bool warned = false;
  QTimer warning_response;
  warning_response.setSingleShot(true);
  QObject::connect(&warning_response, &QTimer::timeout, [&] {
    auto *warning = window->findChild<QMessageBox *>(
        QStringLiteral("material_browser_import_warning_message_box"));
    if (warning == nullptr || !warning->isVisible()) {
      ADD_FAILURE() << "Expected the partial material-import warning.";
      if (auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget())) modal->reject();
      return;
    }
    warned = true;
    EXPECT_TRUE(warning->informativeText().contains(QStringLiteral("Broken.png")));
    auto *accept = warning->findChild<QPushButton *>(
        QStringLiteral("materialBrowserImportWarningMessageBox.ok"));
    if (accept != nullptr) accept->click();
    else warning->reject();
  });
  warning_response.start(0);
  drop_images();
  warning_response.stop();
  EXPECT_TRUE(warned);
  ASSERT_TRUE(editor.ui().show_panel("face_texture").ok());
  ui.refresh({}, {});
  EXPECT_EQ(asset_grid->model()->rowCount(), static_cast<int>(source_paths.size()));
  EXPECT_EQ(editor.project_assets().materials().value().size(), source_paths.size() + 1u);
  EXPECT_EQ(model->rowCount(), static_cast<int>(source_paths.size()) + 1);
  EXPECT_EQ(proxy->rowCount(), model->rowCount());
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
  ASSERT_TRUE(editor.selection().select_object(mesh.value()).ok());
  ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Edit).ok());
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
  auto *windowed = dynamic_cast<quader::QtAssetBrowser *>(
      ui.main_window()->findChild<QWidget *>(
          QStringLiteral("assetBrowser")));
  auto *asset_dock = ui.main_window()->findChild<QDockWidget *>(
      QStringLiteral("dock_asset_browser"));
  auto *viewport = dynamic_cast<quader::QtViewportPanel *>(
      ui.main_window()->findChild<QWidget *>(QStringLiteral("viewportWorkspace")));
  ASSERT_NE(embedded, nullptr);
  ASSERT_NE(windowed, nullptr);
  ASSERT_NE(asset_dock, nullptr);
  ASSERT_NE(viewport, nullptr);
  ui.show();
  ASSERT_TRUE(editor.ui().show_panel("face_texture").ok());
  ASSERT_TRUE(editor.ui().show_panel("asset_browser").ok());
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
  const QString first_asset_id = QString::fromStdString(
      std::string(first.project_asset.str()));
  ASSERT_TRUE(windowed->reveal_item(first_asset_id));
  const auto expect_asset_selection = [&]() {
    const auto selected = windowed->selected_items();
    ASSERT_EQ(selected.size(), 1u);
    EXPECT_EQ(selected.front().id, first_asset_id);
    EXPECT_EQ(selected.front().kind, quader::QtAssetBrowserKind::Material);
  };
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
  expect_asset_selection();
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
  ASSERT_TRUE(editor.history().redo().ok());
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
  const auto revisions_after_deselect = editor.revisions();
  const auto history_after_deselect = editor.history().snapshot();
  ui.refresh({}, {});
  EXPECT_FALSE(embedded->selected_material().valid());
  EXPECT_FALSE(embedded->current_selected_material().valid());
  EXPECT_TRUE(grid->selectionModel()->selectedRows().isEmpty());
  EXPECT_FALSE(grid->selectionModel()->currentIndex().isValid());
  expect_asset_selection();
  EXPECT_EQ(editor.revisions().modeling_content_revision,
            revisions_after_deselect.modeling_content_revision);
  EXPECT_EQ(editor.history().snapshot().undo_label, history_after_deselect.undo_label);
  EXPECT_EQ(editor.history().snapshot().redo_label, history_after_deselect.redo_label);

  // Deselecting clears the old face highlight once. A subsequent browser choice
  // remains available to primitive creation throughout unchanged refreshes.
  ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Object).ok());
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Object).ok());
  ui.refresh({}, {});
  embedded->set_selected_material(second);
  const auto usage_after_deselect = project.material_usage().work_stats();
  const int resets_after_deselect = catalog_resets;
  for (int frame = 0; frame < 12; ++frame) {
    ui.refresh({}, {});
  }
  EXPECT_EQ(embedded->current_selected_material(), second);
  expect_asset_selection();
  EXPECT_EQ(catalog_resets, resets_after_deselect);
  EXPECT_EQ(project.material_usage().work_stats(), usage_after_deselect);
  auto *surface = viewport->surface(quader::ViewportPaneId::Perspective);
  ASSERT_NE(surface, nullptr);
  quader::test_support::resize_viewport_surface(*viewport, *surface, {800, 600});
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
    ASSERT_TRUE(windowed->reveal_item(first_asset_id));
    ui.refresh({}, {});
    EXPECT_EQ(embedded->current_selected_material().valid(), choose_material);
    QFocusEvent focus(QEvent::FocusIn);
    QApplication::sendEvent(surface, &focus);
    process();
    QMouseEvent press(QEvent::MouseButtonPress, {300, 300}, {300, 300},
                       Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    quader::test_support::send_visible_viewport_event(surface, &press);
    process();
    QMouseEvent release(QEvent::MouseButtonRelease, {500, 430}, {500, 430},
                         Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    quader::test_support::send_visible_viewport_event(surface, &release);
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
      QStringLiteral("dock_asset_browser"));
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


namespace {

/// Runs native Qt pane events through the production router without a GPU.
struct NativePaneInputHarness {
  int argc = 1;
  char name[40] = "quader-native-pane-input-tests";
  char *argv[2] = {name, nullptr};
  QApplication application{argc, argv};
  quader::SceneSession scene;
  quader::QuaderEditorApi editor{
      quader::QuaderApiSessionInternalAccess::from_scene_session(scene)};
  quader::LocalizationService localization;
  quader::QtViewportPanel panel;

  void initialize() {
    application.setQuitOnLastWindowClosed(false);
    ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
    ASSERT_TRUE(localization.load(
        std::filesystem::path(QUADER_SOURCE_DIR) / "resources/locales", "en-US"));
    ASSERT_TRUE(editor.viewports().set_layout(quader::ViewportLayoutId::FourQuad).ok());
    panel.setAttribute(Qt::WA_DontShowOnScreen, true);
    panel.resize(1600, 1200);
    panel.attach_workspace(scene.viewport_workspace());
    panel.set_localization(localization);
    panel.set_startup_presentation_blocked(false);
    panel.show();
    panel.refresh_workspace_layout();
    application.processEvents();
    for (const auto pane : {quader::ViewportPaneId::Perspective, quader::ViewportPaneId::Top}) {
      auto *surface = panel.surface(pane);
      ASSERT_NE(surface, nullptr);
      quader::test_support::resize_viewport_surface(panel, *surface, {800, 600});
    }
    focus(quader::ViewportPaneId::Perspective);
    process();
  }

  ~NativePaneInputHarness() { panel.abandon_interactions(); }

  void process() {
    panel.process_input(editor, scene.scene(), scene.assets(), scene.modeling(),
        quader::kInvalidAssetId, scene.native_tools(), &scene.point_placement(),
        1.0 / 60.0);
  }

  void focus(quader::ViewportPaneId pane) {
    for (const auto other : {quader::ViewportPaneId::Perspective, quader::ViewportPaneId::Top}) {
      if (other != pane) {
        QFocusEvent blur(QEvent::FocusOut);
        QCoreApplication::sendEvent(panel.surface(other), &blur);
      }
    }
    ASSERT_TRUE(editor.viewports().focus_pane(pane).ok());
    QFocusEvent focus_event(QEvent::FocusIn);
    QCoreApplication::sendEvent(panel.surface(pane), &focus_event);
  }

  void mouse(quader::ViewportPaneId pane, QEvent::Type type, QPoint local,
             Qt::MouseButton button, Qt::MouseButtons buttons) {
    auto *surface = panel.surface(pane);
    const QPoint global = surface->visible_local_to_global(local);
    QMouseEvent event(type, QPointF(local), QPointF(global), button, buttons,
                      Qt::NoModifier);
    quader::test_support::send_visible_viewport_event(surface, &event);
  }
};

} // namespace

TEST(QtProjectWorkspaceTests,
     ItalianShiftDigitsSwitchSingleSelectionModeThroughNativeViewportEvents) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  NativePaneInputHarness harness;
  ASSERT_NO_FATAL_FAILURE(harness.initialize());
  for (const auto mode : {quader::QuaderSelectionMode::Edge,
                         quader::QuaderSelectionMode::Face}) {
    ASSERT_TRUE(harness.editor.commands().set_shortcut(
        harness.editor.commands().keybinding_target_id_for_selection_mode(mode),
        mode == quader::QuaderSelectionMode::Edge ? "Shift+2" : "Shift+3").ok());
  }
  auto *surface = harness.panel.surface(quader::ViewportPaneId::Perspective);
  ASSERT_NE(surface, nullptr);
  const auto box = harness.editor.modeling().create_box(
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      "Italian selection shortcut box");
  ASSERT_TRUE(box.ok()) << box.error_message();
  ASSERT_TRUE(harness.editor.selection().select_object(box.value()).ok());
  ASSERT_TRUE(harness.editor.mode().set(quader::QuaderEditorMode::Edit).ok());
  const auto selected_ids = [&] {
    std::vector<std::array<int, 3>> ids;
    for (const auto &object : harness.scene.modeling().export_selection().objects) {
      if (object.modeling_object_id != box.value().modeling_object_id) continue;
      for (const auto &edge : object.selected_edges) {
        ids.push_back({1, static_cast<int>(std::min(edge.a, edge.b)),
                         static_cast<int>(std::max(edge.a, edge.b))});
      }
      for (const auto &face : object.selected_faces) {
        ids.push_back({2, static_cast<int>(face.id), 0});
      }
    }
    std::sort(ids.begin(), ids.end());
    return ids;
  };
  for (const auto mode : {quader::QuaderSelectionMode::Edge,
                          quader::QuaderSelectionMode::Face}) {
    SCOPED_TRACE(static_cast<int>(mode));
    ASSERT_TRUE(harness.editor.selection().set_mode(mode).ok());
    const bool edge_mode = mode == quader::QuaderSelectionMode::Edge;
    const auto target_mode = edge_mode ? quader::QuaderSelectionMode::Face
                                       : quader::QuaderSelectionMode::Edge;
    const auto target_viewport_mode = edge_mode
        ? quader::ViewportSelectionMode::Face
        : quader::ViewportSelectionMode::Edge;
    const auto selected = edge_mode
        ? harness.editor.selection().select_edge(box.value(), 1, 2)
        : harness.editor.selection().select_face(box.value(), 1);
    ASSERT_TRUE(selected.ok()) << selected.error_message();
    const std::vector<std::array<int, 3>> expected{
        edge_mode ? std::array<int, 3>{1, 1, 2} : std::array<int, 3>{2, 1, 0}};
    ASSERT_EQ(selected_ids(), expected);
    const auto before = harness.editor.selection().summary();
    const int key = edge_mode ? 0x00a3 : Qt::Key_QuoteDbl;
    const std::uint32_t native_scan_code = edge_mode ? 4u : 3u;
    const std::uint32_t native_virtual_key = edge_mode ? 0x33u : 0x32u;
    const QString text = edge_mode ? QStringLiteral("\u00a3") : QStringLiteral("\"");
    QKeyEvent shortcut_override(QEvent::ShortcutOverride, key, Qt::ShiftModifier,
                                native_scan_code, native_virtual_key, 0u, text);
    shortcut_override.setAccepted(false);
    QCoreApplication::sendEvent(surface, &shortcut_override);
    EXPECT_TRUE(shortcut_override.isAccepted());
    harness.process();
    EXPECT_EQ(harness.editor.selection().summary().mode, mode);
    EXPECT_EQ(harness.editor.selection().summary().selection_revision,
              before.selection_revision);
    EXPECT_EQ(selected_ids(), expected);
    QKeyEvent press(QEvent::KeyPress, key, Qt::ShiftModifier,
                    native_scan_code, native_virtual_key, 0u, text);
    QCoreApplication::sendEvent(surface, &press);
    harness.process();
    EXPECT_EQ(harness.editor.selection().summary().mode, target_mode);
    EXPECT_EQ(harness.panel.tool_state().selection_mode, target_viewport_mode);
    const auto switched = harness.editor.selection().summary();
    const auto switched_ids = selected_ids();
    QKeyEvent release(QEvent::KeyRelease, key, Qt::ShiftModifier,
                      native_scan_code, native_virtual_key, 0u, text);
    QCoreApplication::sendEvent(surface, &release);
    harness.process();
    EXPECT_EQ(harness.editor.selection().summary().mode, target_mode);
    EXPECT_EQ(harness.panel.tool_state().selection_mode, target_viewport_mode);
    EXPECT_EQ(selected_ids(), switched_ids);
    EXPECT_EQ(harness.editor.selection().summary().selection_revision,
              switched.selection_revision);
  }
}

TEST(QtProjectWorkspaceTests,
     SameToolKeyboardShortcutPreservesSharedOperationAndOrigin) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  NativePaneInputHarness harness;
  ASSERT_NO_FATAL_FAILURE(harness.initialize());
  constexpr auto kPerspective = quader::ViewportPaneId::Perspective;
  constexpr auto kTop = quader::ViewportPaneId::Top;
  harness.focus(kPerspective);
  ASSERT_TRUE(harness.editor.tools().activate(quader::ViewportToolId::Move).ok());
  harness.process();
  ASSERT_TRUE(harness.editor.tools().pane_owns_executing_tool(kPerspective));
  ASSERT_EQ(harness.editor.viewports().selected_tool(kTop), quader::ViewportToolId::Move);
  const auto origin = harness.editor.tools().active_settings_presentation_origin();
  const auto undo_count = harness.scene.history().undo_count();
  harness.focus(kTop);
  harness.process();
  EXPECT_EQ(harness.editor.tools().active_settings_presentation_origin(), origin);
  ASSERT_EQ(harness.editor.commands().shortcut_for(quader::quader_command::kToolsActivateMove), "W");
  QKeyEvent press(QEvent::KeyPress, Qt::Key_W, Qt::NoModifier, QStringLiteral("w"));
  QCoreApplication::sendEvent(harness.panel.surface(kTop), &press);
  harness.process();
  QKeyEvent release(QEvent::KeyRelease, Qt::Key_W, Qt::NoModifier, QStringLiteral("w"));
  QCoreApplication::sendEvent(harness.panel.surface(kTop), &release);
  harness.process();
  EXPECT_TRUE(harness.editor.tools().pane_owns_executing_tool(kTop));
  EXPECT_TRUE(harness.editor.tools().pane_owns_executing_tool(kPerspective));
  EXPECT_EQ(harness.editor.tools().active_settings_presentation_origin(), origin);
  EXPECT_EQ(origin.pane, kPerspective);
  EXPECT_EQ(harness.editor.viewports().selected_tool(kPerspective), quader::ViewportToolId::Move);
  EXPECT_EQ(harness.editor.viewports().selected_tool(kTop), quader::ViewportToolId::Move);
  EXPECT_EQ(harness.scene.history().undo_count(), undo_count);
}

TEST(QtProjectWorkspaceTests,
     OtherPaneRightClickPreservesSharedPendingExtrusionWithoutQOperations) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  NativePaneInputHarness harness;
  ASSERT_NO_FATAL_FAILURE(harness.initialize());
  constexpr auto kPerspective = quader::ViewportPaneId::Perspective;
  constexpr auto kTop = quader::ViewportPaneId::Top;
  const auto object = harness.editor.modeling().create_box(
      {.min = {-1, -1, -1}, .max = {1, 1, 1}}, "Pending pane extrusion");
  ASSERT_TRUE(object.ok()) << object.error_message();
  ASSERT_TRUE(harness.editor.selection().select_object(object.value()).ok());
  ASSERT_TRUE(harness.editor.mode().set(quader::QuaderEditorMode::Edit).ok());
  ASSERT_TRUE(harness.editor.selection().set_mode(quader::QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(harness.editor.selection().select_face(object.value(), 1).ok());
  harness.focus(kTop);
  ASSERT_TRUE(harness.editor.tools().activate(quader::ViewportToolId::Move).ok());
  harness.focus(kPerspective);
  ASSERT_TRUE(harness.editor.tools().activate_extrude(quader::QuaderExtrudeMode::Region).ok());
  ASSERT_TRUE(harness.editor.tools().set_setting("extrude", "extrude.amount", 2.0).ok());
  ASSERT_TRUE(harness.editor.tools().active_settings().preview_active);
  harness.process();
  const auto origin = harness.editor.tools().active_settings_presentation_origin();
  const auto undo_count = harness.scene.history().undo_count();
  const auto redo_count = harness.scene.history().redo_count();
  const auto selection = harness.editor.selection().summary();
  const auto candidate = harness.editor.query().compiled_object_mesh(object.value());
  ASSERT_TRUE(candidate.has_value());
  harness.focus(kTop);
  harness.mouse(kTop, QEvent::MouseMove, {40, 40}, Qt::NoButton, Qt::NoButton);
  harness.process();
  harness.mouse(kTop, QEvent::MouseButtonPress, {40, 40}, Qt::RightButton, Qt::RightButton);
  harness.process();
  bool menu_observed = false;
  QTimer dismiss_menu;
  QObject::connect(&dismiss_menu, &QTimer::timeout, &harness.panel, [&] {
    if (auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget())) {
      menu_observed = true;
      menu->close();
    }
  });
  dismiss_menu.start(1);
  harness.mouse(kTop, QEvent::MouseButtonRelease, {40, 40}, Qt::RightButton, Qt::NoButton);
  harness.process();
  dismiss_menu.stop();
  EXPECT_FALSE(menu_observed) << "generic operations are presented through Q";
  EXPECT_EQ(QApplication::activePopupWidget(), nullptr);
  EXPECT_TRUE(harness.editor.tools().active_settings().preview_active);
  EXPECT_TRUE(harness.editor.tools().pane_owns_executing_tool(kPerspective));
  EXPECT_TRUE(harness.editor.tools().pane_owns_executing_tool(kTop));
  EXPECT_EQ(harness.editor.tools().active_settings_presentation_origin(), origin);
  EXPECT_EQ(harness.editor.viewports().selected_tool(kTop), quader::ViewportToolId::Extrude);
  EXPECT_EQ(harness.editor.viewports().selected_tool(kPerspective), quader::ViewportToolId::Extrude);
  EXPECT_EQ(harness.scene.history().undo_count(), undo_count);
  EXPECT_EQ(harness.scene.history().redo_count(), redo_count);
  const auto selection_after = harness.editor.selection().summary();
  EXPECT_EQ(selection_after.selection_revision, selection.selection_revision);
  EXPECT_EQ(selection_after.active_object_id, selection.active_object_id);
  EXPECT_EQ(selection_after.active_face_id, selection.active_face_id);
  EXPECT_EQ(selection_after.selected_face_count, selection.selected_face_count);
  const auto after = harness.editor.query().compiled_object_mesh(object.value());
  ASSERT_TRUE(after.has_value());
  EXPECT_EQ(after->indices, candidate->indices);
  ASSERT_EQ(after->vertices.size(), candidate->vertices.size());
  for (std::size_t index = 0; index < after->vertices.size(); ++index) {
    EXPECT_FLOAT_EQ(after->vertices[index].position.x, candidate->vertices[index].position.x);
    EXPECT_FLOAT_EQ(after->vertices[index].position.y, candidate->vertices[index].position.y);
    EXPECT_FLOAT_EQ(after->vertices[index].position.z, candidate->vertices[index].position.z);
  }
}

TEST(QtProjectWorkspaceTests,
     SelectionDragsKeepTheirInitiatingPaneUntilRelease) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  NativePaneInputHarness harness;
  ASSERT_NO_FATAL_FAILURE(harness.initialize());
  constexpr auto kPerspective = quader::ViewportPaneId::Perspective;
  constexpr auto kTop = quader::ViewportPaneId::Top;
  for (const auto tool : {quader::ViewportToolId::Select, quader::ViewportToolId::LassoSelect,
                          quader::ViewportToolId::PaintSelect}) {
    SCOPED_TRACE(static_cast<int>(tool));
    harness.focus(kPerspective);
    ASSERT_TRUE(harness.editor.tools().activate(tool).ok());
    harness.process();
    harness.mouse(kPerspective, QEvent::MouseButtonPress, {40, 40}, Qt::LeftButton, Qt::LeftButton);
    harness.process();
    harness.mouse(kPerspective, QEvent::MouseMove, {120, 120}, Qt::NoButton, Qt::LeftButton);
    harness.process();
    const auto expected_gesture = tool == quader::ViewportToolId::Select
        ? quader::QuaderInputSelectionGesture::Box : tool == quader::ViewportToolId::LassoSelect
        ? quader::QuaderInputSelectionGesture::Lasso : quader::QuaderInputSelectionGesture::Paint;
    ASSERT_EQ(harness.panel.operation_input_context().selection_gesture, expected_gesture);
    ASSERT_TRUE(harness.panel.operation_input_context().pointer_dragging);
    const auto origin = harness.editor.tools().active_settings_presentation_origin();
    harness.mouse(kTop, QEvent::MouseMove, {200, 180}, Qt::NoButton, Qt::LeftButton);
    harness.process();
    EXPECT_EQ(harness.editor.viewports().focused_pane(), kPerspective);
    EXPECT_EQ(harness.panel.operation_input_context().selection_gesture, expected_gesture);
    EXPECT_TRUE(harness.panel.operation_input_context().pointer_dragging);
    EXPECT_FALSE(harness.panel.operation_input_context().orthographic);
    EXPECT_EQ(harness.editor.tools().active_settings_presentation_origin(), origin);
    harness.mouse(kPerspective, QEvent::MouseButtonRelease, {180, 180}, Qt::LeftButton, Qt::NoButton);
    harness.process();
    harness.mouse(kTop, QEvent::MouseMove, {200, 180}, Qt::NoButton, Qt::NoButton);
    harness.process();
    harness.mouse(kTop, QEvent::MouseButtonPress, {200, 180}, Qt::LeftButton, Qt::LeftButton);
    harness.process();
    harness.mouse(kTop, QEvent::MouseButtonRelease, {200, 180}, Qt::LeftButton, Qt::NoButton);
    harness.process();
    EXPECT_EQ(harness.editor.viewports().focused_pane(), kTop);
    EXPECT_FALSE(harness.panel.operation_input_context().pointer_dragging);
  }
}

TEST(QtProjectWorkspaceTests,
     ComposedViewportResizeDefersPreparationUntilEnabledPaint) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-composed-viewport-paint-test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  int preparations = 0;
  int renders = 0;
  QWidget parent;
  quader::QtVulkanCompositionWidget widget(&parent);
  widget.resize(320, 240);
  widget.set_prepare_callback([&] {
    ++preparations;
    return false;
  });
  widget.set_render_callback([&](const quader::RenderVulkanCompositionTarget &) {
    ++renders;
    return false;
  });

  QResizeEvent resize_event(widget.size(), QSize(160, 120));
  QCoreApplication::sendEvent(&widget, &resize_event);
  EXPECT_EQ(preparations, 0);
  EXPECT_EQ(renders, 0);

  QPaintEvent paint_event(widget.rect());
  QCoreApplication::sendEvent(&widget, &paint_event);
  EXPECT_EQ(preparations, 1);
  EXPECT_EQ(renders, 0);

  parent.setUpdatesEnabled(false);
  ASSERT_FALSE(widget.updatesEnabled());
  QResizeEvent disabled_resize(widget.size(), QSize(160, 120));
  QCoreApplication::sendEvent(&widget, &disabled_resize);
  QPaintEvent disabled_paint(widget.rect());
  QCoreApplication::sendEvent(&widget, &disabled_paint);
  EXPECT_EQ(preparations, 1);
  EXPECT_EQ(renders, 0);
}

TEST(QtProjectWorkspaceTests,
     ComposedVulkanCapabilitiesRemainPinnedAcrossResizeAndRelease) {
  if (qEnvironmentVariableIntValue("QUADER_RUN_VULKAN_COMPOSITION_CAPABILITY_TESTS") != 1) {
    GTEST_SKIP() << "Set QUADER_RUN_VULKAN_COMPOSITION_CAPABILITY_TESTS=1 for the Qt device contract";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-composition-capability-test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  std::optional<quader::RenderVulkanSharedContext> pinned_context;
  QEventLoop frame_loop;
  QTimer timeout;
  timeout.setSingleShot(true);
  QObject::connect(&timeout, &QTimer::timeout, &frame_loop, &QEventLoop::quit);
  {
    QWidget root;
    QVBoxLayout layout(&root);
    quader::QtVulkanCompositionWidget widget(&root);
    layout.addWidget(&widget);
    root.resize(240, 180);
    bool submitted = false;
    bool failed = false;
    QObject::connect(&widget, &QRhiWidget::frameSubmitted, &frame_loop, [&] {
      submitted = true;
      frame_loop.quit();
    });
    QObject::connect(&widget, &QRhiWidget::renderFailed, &frame_loop, [&] {
      failed = true;
      frame_loop.quit();
    });
    const auto wait_for_frame = [&] {
      timeout.start(10000);
      if (!submitted && !failed) {
        frame_loop.exec();
      }
      timeout.stop();
    };
    root.show();
    wait_for_frame();
    ASSERT_FALSE(failed);
    ASSERT_TRUE(submitted);
    ASSERT_NE(widget.shared_context(), nullptr);
    pinned_context = *widget.shared_context();
    ASSERT_NE(pinned_context->enabled_capabilities, nullptr);
    const auto &capabilities = *pinned_context->enabled_capabilities;
    EXPECT_GE(capabilities.api_version, VK_API_VERSION_1_0);
    EXPECT_EQ(capabilities.features.robustBufferAccess, VK_FALSE);
    EXPECT_NE(std::find(capabilities.device_extensions.begin(),
                        capabilities.device_extensions.end(),
                        VK_KHR_SWAPCHAIN_EXTENSION_NAME),
              capabilities.device_extensions.end());
    EXPECT_NE(std::find(capabilities.instance_extensions.begin(),
                        capabilities.instance_extensions.end(),
                        VK_KHR_SURFACE_EXTENSION_NAME),
              capabilities.instance_extensions.end());
    submitted = false;
    root.resize(320, 220);
    wait_for_frame();
    ASSERT_FALSE(failed);
    ASSERT_TRUE(submitted);
    ASSERT_NE(widget.shared_context(), nullptr);
    EXPECT_EQ(widget.shared_context()->device, pinned_context->device);
    EXPECT_EQ(widget.shared_context()->enabled_capabilities,
              pinned_context->enabled_capabilities);
  }
  // The descriptor and its strings remain usable after Qt retires the device;
  // retaining the capability pin never grants continued access to its handles.
  ASSERT_TRUE(pinned_context.has_value());
  const auto &capabilities = *pinned_context->enabled_capabilities;
  EXPECT_EQ(capabilities.features.robustBufferAccess, VK_FALSE);
  EXPECT_FALSE(capabilities.instance_extensions.empty());
  EXPECT_FALSE(capabilities.device_extensions.empty());
}

#if defined(_WIN32)
namespace {

class NativeLoadingTransitionCapture {
public:
  struct Sample {
    std::int64_t elapsed_ms = 0;
    QRect native_client;
    bool visible = false;
    bool incoming = false;
    QRect logo_bounds;
    QRect caption_bounds;
    QRect window_pixels;
    QImage pixels;
  };

  ~NativeLoadingTransitionCapture() { stop(); }
  static QColor background_color() { return QColor(60, 72, 64); }
  bool begin(const QRect &physical_capture_rect, const QString &label);
  void set_window(HWND window, bool incoming) {
    incoming_.store(incoming, std::memory_order_relaxed);
    window_.store(reinterpret_cast<std::uintptr_t>(window),
                  std::memory_order_release);
  }
  std::vector<Sample> finish(const QString &review_dir);

private:
  static constexpr std::uint64_t kMaximumFrameBytes = 64ull * 1024 * 1024;
  static constexpr std::uint64_t kMaximumRetainedBytes = 128ull * 1024 * 1024;
  static constexpr std::size_t kMaximumSamples = 320;
  static constexpr std::size_t kMaximumChangedImages = 24;

  struct DesktopSurface {
    HDC desktop = nullptr;
    HDC memory = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previous_bitmap = nullptr;
    void *bits = nullptr;

    ~DesktopSurface() {
      if (memory && previous_bitmap && previous_bitmap != HGDI_ERROR) {
        SelectObject(memory, previous_bitmap);
      }
      if (bitmap) {
        DeleteObject(bitmap);
      }
      if (memory) {
        DeleteDC(memory);
      }
      if (desktop) {
        ReleaseDC(nullptr, desktop);
      }
    }
  };

  void stop() {
    stop_requested_.store(true, std::memory_order_relaxed);
    if (worker_.joinable()) {
      worker_.join();
    }
  }
  void signal_finished() {
    {
      std::lock_guard lock(ready_mutex_);
      finished_ = true;
    }
    ready_condition_.notify_all();
  }
  void capture();

  QRect capture_rect_;
  QString label_;
  std::atomic<std::uintptr_t> window_ = 0;
  std::atomic<bool> incoming_ = false;
  std::atomic<bool> stop_requested_ = false;
  std::thread worker_;
  std::mutex ready_mutex_;
  std::condition_variable ready_condition_;
  bool ready_ = false;
  bool finished_ = false;
  std::vector<Sample> samples_;
};

bool NativeLoadingTransitionCapture::begin(const QRect &physical_capture_rect,
                                           const QString &label) {
  stop();
  samples_.clear();
  capture_rect_ = physical_capture_rect;
  label_ = label;
  {
    std::lock_guard lock(ready_mutex_);
    ready_ = false;
    finished_ = false;
  }
  stop_requested_.store(false, std::memory_order_relaxed);
  if (capture_rect_.isEmpty()) {
    return false;
  }
  const auto byte_count = static_cast<std::uint64_t>(capture_rect_.width()) *
                          static_cast<std::uint64_t>(capture_rect_.height()) * 4;
  if (byte_count > kMaximumFrameBytes) {
    return false;
  }
  worker_ = std::thread([this] { capture(); });
  std::unique_lock lock(ready_mutex_);
  ready_condition_.wait_for(lock, std::chrono::milliseconds(900),
                            [this] { return ready_ || finished_; });
  return ready_;
}

std::vector<NativeLoadingTransitionCapture::Sample>
NativeLoadingTransitionCapture::finish(const QString &review_dir) {
  stop();
  if (!review_dir.isEmpty()) {
    EXPECT_TRUE(QDir().mkpath(review_dir));
    std::ofstream trace(std::filesystem::path(QDir(review_dir).filePath(
        label_ + QStringLiteral(".csv")).toStdWString()));
    trace << "frame,elapsed_ms,visible,incoming,native_x,native_y,native_w,native_h,"
             "logo_x,logo_y,logo_w,logo_h,caption_x,caption_y,caption_w,caption_h,"
             "window_x,window_y,window_w,window_h\n";
    for (std::size_t index = 0; index < samples_.size(); ++index) {
      const auto &sample = samples_[index];
      trace << index << ',' << sample.elapsed_ms << ',' << sample.visible << ','
            << sample.incoming << ',' << sample.native_client.x() << ','
            << sample.native_client.y() << ',' << sample.native_client.width() << ','
            << sample.native_client.height() << ',' << sample.logo_bounds.x() << ','
            << sample.logo_bounds.y() << ',' << sample.logo_bounds.width() << ','
            << sample.logo_bounds.height() << ',' << sample.caption_bounds.x() << ','
            << sample.caption_bounds.y() << ',' << sample.caption_bounds.width() << ','
            << sample.caption_bounds.height() << ',' << sample.window_pixels.x() << ','
            << sample.window_pixels.y() << ',' << sample.window_pixels.width() << ','
            << sample.window_pixels.height() << '\n';
      if (!samples_[index].pixels.isNull()) {
        const QString name = QStringLiteral("%1-%2.png")
                                 .arg(label_)
                                 .arg(static_cast<qulonglong>(index), 3, 10,
                                      QLatin1Char('0'));
        EXPECT_TRUE(samples_[index].pixels.save(QDir(review_dir).filePath(name)));
      }
    }
    EXPECT_TRUE(trace.good());
  }
  return std::move(samples_);
}

void NativeLoadingTransitionCapture::capture() {
  DesktopSurface surface;
  surface.desktop = GetDC(nullptr);
  surface.memory = surface.desktop ? CreateCompatibleDC(surface.desktop) : nullptr;
  if (!surface.memory) {
    signal_finished();
    return;
  }
  BITMAPINFO bitmap_info{};
  bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bitmap_info.bmiHeader.biWidth = capture_rect_.width();
  bitmap_info.bmiHeader.biHeight = -capture_rect_.height();
  bitmap_info.bmiHeader.biPlanes = 1;
  bitmap_info.bmiHeader.biBitCount = 32;
  bitmap_info.bmiHeader.biCompression = BI_RGB;
  surface.bitmap = CreateDIBSection(surface.desktop, &bitmap_info,
                                    DIB_RGB_COLORS, &surface.bits, nullptr, 0);
  if (!surface.bitmap || !surface.bits) {
    signal_finished();
    return;
  }
  surface.previous_bitmap = SelectObject(surface.memory, surface.bitmap);
  if (!surface.previous_bitmap || surface.previous_bitmap == HGDI_ERROR) {
    signal_finished();
    return;
  }

  const auto started_at = std::chrono::steady_clock::now();
  const auto frame_bytes = static_cast<std::uint64_t>(capture_rect_.width()) *
                           static_cast<std::uint64_t>(capture_rect_.height()) * 4;
  std::uint64_t retained_bytes = 0;
  std::size_t changed_images = 0;
  const int capture_width = capture_rect_.width();
  const int capture_height = capture_rect_.height();
  std::vector<unsigned char> purple_mask(static_cast<std::size_t>(capture_width) * capture_height);
  std::vector<int> connected_pixels;
  std::vector<int> component_rows(static_cast<std::size_t>(capture_height));
  std::vector<QRect> logo_components;
  std::vector<QRect> caption_bands;
  samples_.reserve(kMaximumSamples);
  while (!stop_requested_.load(std::memory_order_relaxed) &&
         samples_.size() < kMaximumSamples) {
    const auto sampled_at = std::chrono::steady_clock::now();
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                sampled_at - started_at)
                                .count();
    if (elapsed_ms >= 2500) {
      break;
    }
    const auto sampled_window = window_.load(std::memory_order_acquire);
    const bool sampled_incoming = incoming_.load(std::memory_order_relaxed);
    if (!BitBlt(surface.memory, 0, 0, capture_rect_.width(),
                capture_rect_.height(), surface.desktop, capture_rect_.x(),
                capture_rect_.y(), SRCCOPY | CAPTUREBLT)) {
      break;
    }
    GdiFlush();
    Sample sample;
    sample.elapsed_ms = elapsed_ms;
    const auto window = reinterpret_cast<HWND>(
        window_.load(std::memory_order_acquire));
    sample.visible = window && IsWindowVisible(window);
    RECT client{};
    POINT origin{};
    if (window && GetClientRect(window, &client) &&
        ClientToScreen(window, &origin)) {
      sample.native_client = QRect(origin.x, origin.y,
                                   client.right - client.left,
                                   client.bottom - client.top);
    }
    // A capture crossing the handoff boundary can still contain the outgoing
    // surface. Keep its pixels, but do not label it as a completed incoming read.
    sample.incoming = sampled_incoming &&
        sampled_window == reinterpret_cast<std::uintptr_t>(window) &&
        incoming_.load(std::memory_order_relaxed);

    const auto *pixels = static_cast<const QRgb *>(surface.bits);
    int window_left = capture_rect_.width();
    int window_top = capture_rect_.height();
    int window_right = -1;
    int window_bottom = -1;
    for (int y = 0; y < capture_rect_.height(); ++y) {
      const auto *row = pixels + static_cast<std::size_t>(y) * capture_rect_.width();
      for (int x = 0; x < capture_rect_.width(); ++x) {
        const int red = qRed(row[x]);
        const int green = qGreen(row[x]);
        const int blue = qBlue(row[x]);
        purple_mask[static_cast<std::size_t>(y) * capture_width + x] =
            red > green + 20 && blue > green + 20;
        const bool backdrop = std::abs(red - 60) <= 3 &&
                              std::abs(green - 72) <= 3 &&
                              std::abs(blue - 64) <= 3;
        const bool screen_gap = red <= 3 && green <= 3 && blue <= 3;
        if (!backdrop && !screen_gap) {
          window_left = std::min(window_left, x);
          window_top = std::min(window_top, y);
          window_right = std::max(window_right, x);
          window_bottom = std::max(window_bottom, y);
        }
      }
    }
    const QRect observed_window = window_right >= window_left
        ? QRect(QPoint(window_left, window_top), QPoint(window_right, window_bottom)) : QRect{};
    RECT native_frame{};
    const UINT native_dpi = window ? GetDpiForWindow(window) : 96;
    const int small_icon_width = GetSystemMetricsForDpi(SM_CXSMICON, native_dpi);
    const int small_icon_height = GetSystemMetricsForDpi(SM_CYSMICON, native_dpi);
    QRect caption_icon_slot;
    if (!observed_window.isEmpty() && window && GetWindowRect(window, &native_frame)) {
      const int frame_inset = std::max(0, sample.native_client.left() - static_cast<int>(native_frame.left));
      const int caption_height = std::max(0, sample.native_client.top() - static_cast<int>(native_frame.top));
      // Follow the painted footprint through DWM's transform, not the final
      // client coordinates. Native frame insets allow for its exterior shadow.
      caption_icon_slot = QRect(observed_window.topLeft(),
          QSize(2 * small_icon_width + 3 * frame_inset, caption_height + 2 * frame_inset));
    }
    // Windows can join an accent-colored caption to the thin window border.
    // Its sparse bounding rectangle and dense caption bands identify this chrome
    // separately from the authored logo's filled components.
    // Search the entire capture, including locations outside the final client.
    logo_components.clear();
    caption_bands.clear();
    for (int pixel_index = 0; pixel_index < capture_width * capture_height; ++pixel_index) {
      if (purple_mask[static_cast<std::size_t>(pixel_index)] == 0) continue;
      connected_pixels.clear();
      connected_pixels.push_back(pixel_index);
      purple_mask[static_cast<std::size_t>(pixel_index)] = 0;
      QRect component;
      for (std::size_t index = 0; index < connected_pixels.size(); ++index) {
        const int current = connected_pixels[index];
        const int x = current % capture_width;
        const int y = current / capture_width;
        component = component.united(QRect(x, y, 1, 1));
        const auto append = [&](int adjacent) {
          auto &unvisited = purple_mask[static_cast<std::size_t>(adjacent)];
          if (unvisited == 0) return;
          unvisited = 0;
          connected_pixels.push_back(adjacent);
        };
        if (x > 0) append(current - 1);
        if (x + 1 < capture_width) append(current + 1);
        if (y > 0) append(current - capture_width);
        if (y + 1 < capture_height) append(current + capture_width);
      }
      const auto component_area = static_cast<std::uint64_t>(component.width()) * component.height();
      if (component.width() > 4 * component.height() ||
          connected_pixels.size() * 10 < component_area) {
        sample.caption_bounds = sample.caption_bounds.united(component);
        std::fill(component_rows.begin(), component_rows.end(), 0);
        for (const int connected : connected_pixels) {
          ++component_rows[static_cast<std::size_t>(connected / capture_width)];
        }
        int band_start = -1;
        for (int y = component.top(); y <= component.bottom() + 1; ++y) {
          const bool dense_row = y <= component.bottom() &&
              component_rows[static_cast<std::size_t>(y)] * 2 >= component.width();
          if (dense_row && band_start < 0) band_start = y;
          if (!dense_row && band_start >= 0) {
            caption_bands.emplace_back(component.left(), band_start, component.width(), y - band_start);
            band_start = -1;
          }
        }
      } else {
        logo_components.push_back(component);
      }
    }
    QRect caption_icon;
    for (const QRect &component : logo_components) {
      if (caption_icon_slot.contains(component)) caption_icon = caption_icon.united(component);
    }
    const bool identified_caption_icon = !caption_icon.isEmpty() &&
        caption_icon.width() >= small_icon_width / 2 &&
        caption_icon.height() >= small_icon_height / 2 &&
        caption_icon.width() <= small_icon_width + 2 &&
        caption_icon.height() <= small_icon_height + 2;
    for (const QRect &component : logo_components) {
      const bool caption_glyph = std::any_of(caption_bands.begin(), caption_bands.end(),
          [&](const QRect &band) { return band.contains(component); }) ||
          (identified_caption_icon && caption_icon.contains(component));
      if (caption_glyph) sample.caption_bounds = sample.caption_bounds.united(component);
      else sample.logo_bounds = sample.logo_bounds.united(component);
    }
    if (!sample.logo_bounds.isEmpty()) sample.logo_bounds.translate(capture_rect_.topLeft());
    if (!sample.caption_bounds.isEmpty()) sample.caption_bounds.translate(capture_rect_.topLeft());
    if (window_right >= window_left) {
      sample.window_pixels = QRect(QPoint(window_left, window_top),
                                   QPoint(window_right, window_bottom))
                                 .translated(capture_rect_.topLeft());
    }
    const bool changed = samples_.empty() ||
                         sample.logo_bounds != samples_.back().logo_bounds ||
                         sample.caption_bounds != samples_.back().caption_bounds ||
                         sample.window_pixels != samples_.back().window_pixels;
    // Reserve one image for the final frame even if intermediate changes fill
    // the image budget. Metadata is retained for every successful desktop read.
    if (samples_.empty() ||
        (changed && changed_images < kMaximumChangedImages &&
         retained_bytes + 2 * frame_bytes <= kMaximumRetainedBytes)) {
      sample.pixels = QImage(static_cast<const uchar *>(surface.bits),
                            capture_rect_.width(), capture_rect_.height(),
                            QImage::Format_RGB32)
                          .copy();
      retained_bytes += frame_bytes;
      if (!samples_.empty()) {
        ++changed_images;
      }
    }
    samples_.push_back(std::move(sample));
    if (samples_.size() == 1) {
      {
        std::lock_guard lock(ready_mutex_);
        ready_ = true;
      }
      ready_condition_.notify_all();
    }
    std::this_thread::sleep_until(sampled_at + std::chrono::milliseconds(6));
  }
  if (!samples_.empty() && samples_.back().pixels.isNull()) {
    samples_.back().pixels = QImage(static_cast<const uchar *>(surface.bits),
                                   capture_rect_.width(), capture_rect_.height(),
                                   QImage::Format_RGB32)
                                 .copy();
  }
  signal_finished();
}

} // namespace
#endif

TEST(QtProjectWorkspaceTests, LoadingCoverRealAppWaitsForComposedProjectFrames) {
  if (qEnvironmentVariableIntValue("QUADER_RUN_VULKAN_LOADING_APP_TESTS") != 1) {
    GTEST_SKIP() << "Set QUADER_RUN_VULKAN_LOADING_APP_TESTS=1 for loading transitions";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  const QString preview_root = qEnvironmentVariable("QUADER_LOADING_APP_PREVIEW_ROOT");
  const auto profile = preview_root.isEmpty()
      ? std::filesystem::temp_directory_path() /
            ("quader-loading-app-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()))
      : std::filesystem::absolute(std::filesystem::path(preview_root.toStdWString()));
  if (!preview_root.isEmpty()) {
    ASSERT_TRUE(!std::filesystem::exists(profile) ||
                (std::filesystem::is_directory(profile) && std::filesystem::is_empty(profile)))
        << "QUADER_LOADING_APP_PREVIEW_ROOT must name a missing or empty dedicated directory";
  }
  ScopedIsolatedLocalAppData scoped_profile(profile, !preview_root.isEmpty());
  ASSERT_TRUE(scoped_profile.active());
  int argc = 1;
  char application_name[] = "quader-loading-real-app-test";
  char *argv[] = {application_name, nullptr};
  class LoadingPaintApplication final : public QApplication {
  public:
    using QApplication::QApplication;
    std::function<void(QWidget *)> cover_painted;
    std::function<void(QWidget *)> root_updated;
    std::function<void(QWidget *, QEvent::Type)> root_window_event;
    std::chrono::steady_clock::time_point cover_paint_started{};

    bool notify(QObject *receiver, QEvent *event) override {
      const bool cover_paint = event->type() == QEvent::Paint &&
          receiver->objectName() == QStringLiteral("quaderLoadingCover");
      const bool root_update = event->type() == QEvent::UpdateRequest &&
          receiver->objectName() == QStringLiteral("quaderMainWindow");
      const bool root_event = receiver->objectName() == QStringLiteral("quaderMainWindow") &&
          (event->type() == QEvent::Show || event->type() == QEvent::Hide ||
           event->type() == QEvent::Move || event->type() == QEvent::Resize);
      const QPointer<QWidget> widget = cover_paint || root_update || root_event
          ? qobject_cast<QWidget *>(receiver) : nullptr;
#if defined(_WIN32)
      if (widget != nullptr) {
        auto *root = widget->window();
        const auto native_root = reinterpret_cast<HWND>(root->internalWinId());
        if (root->isVisible() && native_root != nullptr &&
            (!capture_window_raised_ ||
             (GetWindowLongPtrW(native_root, GWL_EXSTYLE) & WS_EX_TOPMOST) == 0)) {
          capture_window_raised_ = true;
          EXPECT_NE(SetWindowPos(reinterpret_cast<HWND>(root->internalWinId()),
              HWND_TOPMOST, 0, 0, 0, 0,
              SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_NOOWNERZORDER), FALSE);
        }
      }
#endif
      if (cover_paint) cover_paint_started = std::chrono::steady_clock::now();
      const bool handled = QApplication::notify(receiver, event);
      if (widget != nullptr && cover_paint && cover_painted) cover_painted(widget);
      if (widget != nullptr && root_update && root_updated) root_updated(widget);
      if (widget != nullptr && root_event && root_window_event) {
        root_window_event(widget, event->type());
      }
      return handled;
    }
  private:
    bool capture_window_raised_ = false;
  } application(argc, argv);
  application.setQuitOnLastWindowClosed(true);
  application.setApplicationName(QStringLiteral("Quader"));
  application.setOrganizationName(QStringLiteral("Quader"));
  QStyle *style = QStyleFactory::create(QStringLiteral("Fusion"));
  ASSERT_NE(style, nullptr);
  QApplication::setStyle(style);
  quader::apply_qt_obsidian_application_theme(style->standardPalette());
  application.setWindowIcon(QIcon(QStringLiteral(
      QUADER_SOURCE_DIR "/resources/icons/quader.ico")));
  const auto executable = std::filesystem::path(
      QCoreApplication::applicationFilePath().toStdWString());
  const auto resources = executable.parent_path();
  const QString review = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
  if (!review.isEmpty()) ASSERT_TRUE(QDir().mkpath(review));
  class ScopedLoadingLogger final {
  public:
    explicit ScopedLoadingLogger(const std::filesystem::path &path) {
      quader::Logger::initialize({.name = "loading-native-test", .file_path = path.string()});
    }
    ~ScopedLoadingLogger() {
      for (const auto &entry : quader::Logger::entries_snapshot()) {
        if (entry.level >= spdlog::level::warn) std::cout << entry.message;
      }
      quader::Logger::shutdown();
    }
  } loading_logger(review.isEmpty() ? profile / "loading-app.log"
      : std::filesystem::path(QDir(review).filePath(QStringLiteral("loading-app.log")).toStdWString()));
  const bool saved_maximized = qEnvironmentVariable(
      "QUADER_LOADING_APP_MAXIMIZED", QStringLiteral("1")) == QStringLiteral("1");
  const bool home_only = qEnvironmentVariableIntValue("QUADER_LOADING_APP_HOME_ONLY") == 1;
  const int scale_percent = qEnvironmentVariable(
      "QUADER_LOADING_APP_SCALE", QStringLiteral("100")).toInt();
  ASSERT_TRUE(scale_percent == 100 || scale_percent == 200);
  const auto screens = application.screens();
  ASSERT_FALSE(screens.isEmpty());
  const int screen_index = qEnvironmentVariable(
      "QUADER_LOADING_APP_SCREEN", screens.size() > 1 ? QStringLiteral("1")
                                                      : QStringLiteral("0")).toInt();
  ASSERT_GE(screen_index, 0);
  ASSERT_LT(screen_index, screens.size());
  const QRect work_area = screens[screen_index]->availableGeometry();
  // Match a saved, nearly full-size normal window on a vertically offset display;
  // the native maximize acknowledgement can arrive after the first Qt paint.
  auto window_settings = quader::AppWindowSettings::defaults();
  window_settings.set_placement({
      .normal_frame = {work_area.x(), work_area.y() + 46,
                       std::min(1874, work_area.width()),
                       std::min(986, work_area.height() - 46)},
      .maximized = saved_maximized});
  ASSERT_TRUE(window_settings.save(quader::AppWindowSettings::resolve_path(resources)));
  const auto project_root = profile / "Atrium Blockout";
  ASSERT_TRUE(std::filesystem::create_directory(project_root));
  std::filesystem::path manifest_path;
  std::filesystem::path second_manifest_path;
  const std::array<const char *, 19> preview_names{
      "Harbor Warehouse", "Canal District", "Metro Interchange", "Glasshouse",
      "Old Town Courtyard", "Mountain Relay", "Underground Service Tunnels",
      "Coastal Observatory", "Foundry North", "Station Platform", "Riverside Depot",
      "Market Square", "Industrial Passage", "Hotel Atrium", "Fortress Approach",
      "Museum Annex", "Desert Outpost", "Research Campus", "Warehouse Loading Dock"};
  std::vector<std::filesystem::path> fixture_manifests;
  for (std::size_t index = 0; index < preview_names.size(); ++index) {
    const auto root = profile / preview_names[index];
    ASSERT_TRUE(std::filesystem::create_directory(root));
    quader::ProjectSession project;
    project.set_resource_root(resources);
    quader::QuaderEditorApi editor(project);
    ASSERT_TRUE(editor.project().create(root).ok());
    const auto scene_id = editor.scene_documents().active_scene_id();
    ASSERT_TRUE(scene_id.has_value());
    const auto scene_path = quader::ProjectRelativePath::parse_portable("Main.qdr");
    ASSERT_TRUE(scene_path.ok());
    ASSERT_TRUE(editor.scene_documents().save_as(*scene_id, scene_path.value()).ok());
    fixture_manifests.push_back(editor.project().manifest_path());
    if (index == 0) second_manifest_path = fixture_manifests.back();
  }
  std::filesystem::path unopened_scene_path;
  QString unopened_scene_identity;
  {
    quader::ProjectSession project;
    project.set_resource_root(resources);
    quader::QuaderEditorApi editor(project);
    ASSERT_TRUE(editor.project().create(project_root).ok());
    ASSERT_TRUE(editor.viewports().set_layout(quader::ViewportLayoutId::FourQuad).ok());
    const auto box = editor.modeling().create_box(
        {.min = {-2, -1, -3}, .max = {2, 3, 1}}, "Loading fixture box");
    ASSERT_TRUE(box.ok()) << box.error_message();
    ASSERT_TRUE(editor.selection().select_object(box.value()).ok());
    const auto scene_id = editor.scene_documents().active_scene_id();
    ASSERT_TRUE(scene_id.has_value());
    const auto path = quader::ProjectRelativePath::parse_portable("Loaded.qdr");
    ASSERT_TRUE(path.ok());
    ASSERT_TRUE(editor.scene_documents().save_as(*scene_id, path.value()).ok());
    const auto unopened = editor.project_assets().create_scene_asset("", "Unopened");
    ASSERT_TRUE(unopened.ok()) << unopened.error_message();
    unopened_scene_path = project_root / unopened.value().project_relative_path;
    unopened_scene_identity = QString::fromStdString(
        std::string(unopened.value().scene_id.str()));
    manifest_path = editor.project().manifest_path();
  }
  fixture_manifests.push_back(manifest_path);
  for (std::size_t index = 0; index < fixture_manifests.size(); ++index) {
    std::filesystem::last_write_time(fixture_manifests[index],
        std::filesystem::file_time_type::clock::now() - std::chrono::hours(24 * index));
  }
  const auto first_placement_path = quader::AppWindowSettings::resolve_project_path(
      resources, manifest_path);
  const auto second_placement_path = quader::AppWindowSettings::resolve_project_path(
      resources, second_manifest_path);
  ASSERT_NE(first_placement_path, second_placement_path);
  const QRect first_editor_rect(work_area.topLeft() + QPoint(40, 40),
      QSize(std::min(1500, work_area.width() - 80),
            std::min(800, work_area.height() - 80)));
  window_settings.set_placement({
      .normal_frame = {first_editor_rect.x(), first_editor_rect.y(),
                       first_editor_rect.width(), first_editor_rect.height()},
      .maximized = saved_maximized});
  ASSERT_TRUE(window_settings.save(first_placement_path));
  auto second_window_settings = quader::AppWindowSettings::defaults();
  const QRect second_work_area = screens.front()->availableGeometry();
  const QRect second_editor_rect(second_work_area.topLeft() + QPoint(70, 70),
      QSize(std::min(1700, second_work_area.width() - 140),
            std::min(1000, second_work_area.height() - 140)));
  second_window_settings.set_placement({
      .normal_frame = {second_editor_rect.x(), second_editor_rect.y(),
                       second_editor_rect.width(), second_editor_rect.height()},
      .maximized = false});
  ASSERT_TRUE(second_window_settings.save(second_placement_path));
  const auto first_placement_before_home = read_binary_file(first_placement_path);
  const auto second_placement_before_home = read_binary_file(second_placement_path);
  auto settings = quader::UISettings::defaults();
  settings.set_interface_scale(static_cast<float>(scale_percent) / 100.0f);
  settings.set_panel_visible(quader::UIPanelId::AssetBrowser, true);
  ASSERT_TRUE(settings.save(quader::UISettings::resolve_paths(resources).settings_file));
  class LoadingRevealProbe final : public QObject {
  public:
    std::chrono::steady_clock::time_point first_paint{};
    std::function<void()> revealed;
  protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
      if (watched->objectName() != QStringLiteral("quaderLoadingCover")) {
        return QObject::eventFilter(watched, event);
      }
      if (event->type() == QEvent::Show) first_paint = {};
      if (event->type() == QEvent::Hide && revealed &&
          qobject_cast<QWidget *>(watched)->window()->isVisible()) revealed();
      return QObject::eventFilter(watched, event);
    }
  } reveal_probe;
  application.installEventFilter(&reveal_probe);
  const auto purple_bounds = [](const QImage &pixels) {
    QRect bounds;
    for (int y = 0; y < pixels.height(); ++y) {
      for (int x = 0; x < pixels.width(); ++x) {
        const QColor color = pixels.pixelColor(x, y);
        if (color.alpha() > 100 && color.red() > color.green() + 20 &&
            color.blue() > color.green() + 20) {
          bounds = bounds.united(QRect(x, y, 1, 1));
        }
      }
    }
    return bounds;
  };
  const auto native_client_rect = [](QWidget *widget) {
    const auto extent = quader::platform_window_client_extent(
        reinterpret_cast<void *>(widget->internalWinId()));
    QPoint origin = widget->mapToGlobal(QPoint{});
#if defined(_WIN32)
    POINT native_origin{};
    if (ClientToScreen(reinterpret_cast<HWND>(widget->internalWinId()), &native_origin)) {
      origin = QPoint(native_origin.x, native_origin.y);
    }
#endif
    return QRect(origin, QSize(extent.width, extent.height));
  };
  bool reported_desktop_mismatch = false;
  QPointer<QWidget> painted_backing_root;
  const auto desktop_matches_quader = [&](QWidget *root, const QImage &desktop) {
    if (root != painted_backing_root.data()) return false;
    auto *store = root->backingStore();
    auto *device = store != nullptr ? store->paintDevice() : nullptr;
    if (device == nullptr || device->devType() != QInternal::Image) return false;
    const auto &painted = *static_cast<const QImage *>(device);
    const QRect common = painted.rect().intersected(desktop.rect());
    int compared = 0;
    int matching = 0;
    // GPU viewports occupy transparent backing pixels. Compare only opaque Qt
    // chrome/content, including the full loading cover, to identify our window.
    for (int y = common.top() + 6; y < common.bottom(); y += 13) {
      for (int x = common.left() + 6; x < common.right(); x += 13) {
        const QColor expected = painted.pixelColor(x, y);
        if (expected.alpha() < 250) continue;
        const QColor actual = desktop.pixelColor(x, y);
        ++compared;
        if (std::abs(expected.red() - actual.red()) <= 3 &&
            std::abs(expected.green() - actual.green()) <= 3 &&
            std::abs(expected.blue() - actual.blue()) <= 3) {
          ++matching;
        }
      }
    }
    const bool matched = compared >= 100 && matching * 100 >= compared * 98;
    if (!matched && !reported_desktop_mismatch) {
      reported_desktop_mismatch = true;
      std::cout << "native capture not yet Quader: matching_opaque_pixels="
                << matching << '/' << compared << "; image not saved\n";
    }
    return matched;
  };
  const auto native_client_is_unoccluded = [&](QWidget *root) {
#if defined(_WIN32)
    const auto native_root = reinterpret_cast<HWND>(root->internalWinId());
    DWORD process_id = 0;
    GetWindowThreadProcessId(native_root, &process_id);
    if (process_id != GetCurrentProcessId() || !IsWindowVisible(native_root) ||
        (GetWindowLongPtrW(native_root, GWL_EXSTYLE) & WS_EX_TOPMOST) == 0) {
      return false;
    }
    const QRect client = native_client_rect(root);
    for (const qreal y_fraction : {0.05, 0.5, 0.95}) {
      for (const qreal x_fraction : {0.05, 0.5, 0.95}) {
        const POINT point{client.x() + qRound(client.width() * x_fraction),
                          client.y() + qRound(client.height() * y_fraction)};
        const HWND at_point = WindowFromPoint(point);
        DWORD point_process_id = 0;
        GetWindowThreadProcessId(at_point, &point_process_id);
        if (point_process_id != GetCurrentProcessId() ||
            GetAncestor(at_point, GA_ROOT) != native_root) {
          return false;
        }
      }
    }
    return true;
#else
    return false;
#endif
  };
  struct EarlyLoadingFrame {
    QRect native_client;
    QSize retained_pixels;
    QRect cover_pixels;
    QRect logo_pixels;
    QPointF authored_offset;
  };
  std::vector<EarlyLoadingFrame> early_frames;
  bool initialization_returned = false;
  bool observed_early_logo = false;
  bool recording_early_frames = true;
  int desktop_frame_count = 0;
  application.cover_painted = [&](QWidget *painted_cover) {
    auto *root = painted_cover->window();
    if (root == painted_backing_root.data() &&
        reveal_probe.first_paint != std::chrono::steady_clock::time_point{} &&
        (!recording_early_frames || (initialization_returned && observed_early_logo))) return;
    auto *backing_store = root->backingStore();
    ASSERT_NE(backing_store, nullptr);
    auto *device = backing_store->paintDevice();
    ASSERT_NE(device, nullptr);
    ASSERT_EQ(device->devType(), QInternal::Image);
    // Read the completed raster paint directly. QWidget::grab/render would
    // repaint at a newer geometry and erase the first-frame evidence.
    const QImage pixels = static_cast<const QImage *>(device)->copy();
    painted_backing_root = root;
    const qreal ratio = pixels.devicePixelRatio();
    const QPoint cover_origin = painted_cover->mapTo(root, QPoint{});
    const QRect cover_pixels(qRound(cover_origin.x() * ratio),
        qRound(cover_origin.y() * ratio), qRound(painted_cover->width() * ratio),
        qRound(painted_cover->height() * ratio));
    const QRect visible_cover = cover_pixels.intersected(pixels.rect());
    const QRect local_logo = purple_bounds(pixels.copy(visible_cover));
    const QRect logo_pixels = local_logo.isEmpty() ? QRect{}
        : local_logo.translated(visible_cover.topLeft());
    if (!logo_pixels.isEmpty() &&
        reveal_probe.first_paint == std::chrono::steady_clock::time_point{}) {
      reveal_probe.first_paint = application.cover_paint_started;
    }
    if (!recording_early_frames || (initialization_returned && observed_early_logo)) return;
    const int logo_extent = qRound(96.0 * scale_percent / 100.0 * ratio);
    const QImage authored_logo = application.windowIcon().pixmap(
        QSize(logo_extent, logo_extent)).toImage();
    const QRect authored_bounds = purple_bounds(authored_logo);
    ASSERT_FALSE(authored_bounds.isEmpty());
    const QPointF authored_offset = QRectF(authored_bounds).center() -
        QRectF(authored_logo.rect()).center();
    const auto client = native_client_rect(root);
    early_frames.push_back({client,
        QSize(qRound(root->width() * ratio), qRound(root->height() * ratio)),
        cover_pixels, logo_pixels, authored_offset});
    if (!observed_early_logo && !logo_pixels.isEmpty()) {
      reveal_probe.first_paint = application.cover_paint_started;
    }
    observed_early_logo = observed_early_logo || !logo_pixels.isEmpty();
    const QString frame_name = QStringLiteral("loading-early-%1-painted")
        .arg(early_frames.size(), 3, 10, QLatin1Char('0'));
    std::cout << frame_name.toStdString() << " before_initialize_return="
              << !initialization_returned << " native_client="
              << client.x() << ',' << client.y() << ',' << client.width() << ','
              << client.height() << " retained=" << root->width() << 'x'
              << root->height() << " cover=" << cover_pixels.width() << 'x'
              << cover_pixels.height() << " backing=" << pixels.width() << 'x'
              << pixels.height() << " dpr=" << ratio << " logo=" << logo_pixels.x() << ','
              << logo_pixels.y() << ',' << logo_pixels.width() << ','
              << logo_pixels.height() << '\n';
    if (!review.isEmpty()) {
      EXPECT_TRUE(pixels.save(QDir(review).filePath(frame_name + QStringLiteral(".png"))));
    }
  };
  application.root_updated = [&](QWidget *root) {
    if (!recording_early_frames || (initialization_returned && observed_early_logo)) return;
    ASSERT_NE(root->screen(), nullptr);
    // Window-DC capture misses Vulkan composition. Qt's desktop capture takes
    // screen-local logical coordinates and adds the native screen origin itself.
    const QPoint origin = root->mapToGlobal(QPoint{}) - root->screen()->geometry().topLeft();
    const QImage pixels = root->screen()->grabWindow(
        0, origin.x(), origin.y(), root->width(), root->height()).toImage();
    ASSERT_FALSE(pixels.isNull());
    const QRect logo = purple_bounds(pixels);
    const QString frame_name = QStringLiteral("loading-early-%1-desktop")
        .arg(++desktop_frame_count, 3, 10, QLatin1Char('0'));
    std::cout << frame_name.toStdString() << " native_pixels=" << pixels.width()
              << 'x' << pixels.height() << " logo=" << logo.x() << ',' << logo.y()
              << ',' << logo.width() << ',' << logo.height() << '\n';
    const bool presented = desktop_matches_quader(root, pixels);
    if (presented && !review.isEmpty()) {
      EXPECT_TRUE(pixels.save(QDir(review).filePath(frame_name + QStringLiteral(".png"))));
    }
  };
  quader::App app;
  ASSERT_TRUE(app.initialize(application, executable));
  initialization_returned = true;
  QPointer<QMainWindow> window;
  for (auto *widget : QApplication::topLevelWidgets()) {
    if (widget->objectName() == QStringLiteral("quaderMainWindow")) {
      window = qobject_cast<QMainWindow *>(widget);
    }
  }
  ASSERT_NE(window, nullptr);
  QPointer<QWidget> cover = window->findChild<QWidget *>(QStringLiteral("quaderLoadingCover"));
  QPointer<QTableView> recent = window->findChild<QTableView *>(
      QStringLiteral("projectHome.recentProjects.view"));
  QPointer<QStackedWidget> stack = window->findChild<QStackedWidget *>(QStringLiteral("mainCentralStack"));
  QPointer<QAction> close_project = window->findChild<QAction *>(QStringLiteral("command_project_close"));
  QPointer<quader::QtViewportPanel> viewport = dynamic_cast<quader::QtViewportPanel *>(
      window->findChild<QWidget *>(QStringLiteral("viewportWorkspace")));
  ASSERT_NE(cover, nullptr);
  ASSERT_NE(recent, nullptr);
  ASSERT_NE(stack, nullptr);
  ASSERT_NE(close_project, nullptr);
  ASSERT_NE(viewport, nullptr);
  QPointer<quader::QtVulkanCompositionWidget> composition_anchor = dynamic_cast<quader::QtVulkanCompositionWidget *>(
      window->findChild<QWidget *>(QStringLiteral("viewportCompositionAnchor")));
  ASSERT_NE(composition_anchor, nullptr);
  ASSERT_NE(composition_anchor->shared_context(), nullptr);
  std::uint64_t root_generation = 0;
  std::uint64_t destroyed_roots = 0;
  std::uint64_t destroyed_anchors = 0;
  std::uint64_t generation_before_replacement = 0;
  std::uint64_t destroyed_roots_before_replacement = 0;
  std::uint64_t destroyed_anchors_before_replacement = 0;
  bool expecting_replacement = false;
  QPointer<QMainWindow> retiring_window;
  QPointer<quader::QtVulkanCompositionWidget> retiring_anchor;
#if defined(_WIN32)
  NativeLoadingTransitionCapture transition_capture;
  std::vector<std::unique_ptr<QWidget>> transition_backdrops;
  bool transition_capture_active = false;
  bool transition_incoming_seen = false;
  const auto begin_transition_capture = [&](const QString &label) {
    if (transition_backdrops.empty()) {
      for (auto *screen : screens) {
        auto backdrop = std::make_unique<QWidget>(nullptr,
            Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        backdrop->setAttribute(Qt::WA_ShowWithoutActivating);
        backdrop->setAttribute(Qt::WA_QuitOnClose, false);
        backdrop->setFocusPolicy(Qt::NoFocus);
        backdrop->setAutoFillBackground(true);
        auto palette = backdrop->palette();
        palette.setColor(QPalette::Window, NativeLoadingTransitionCapture::background_color());
        backdrop->setPalette(palette);
        backdrop->setScreen(screen);
        backdrop->setGeometry(screen->geometry());
        backdrop->show();
        backdrop->repaint();
        QCoreApplication::sendPostedEvents(backdrop.get(), QEvent::UpdateRequest);
        transition_backdrops.push_back(std::move(backdrop));
      }
    }
    QRect capture_rect;
    for (const auto &backdrop : transition_backdrops) {
      capture_rect = capture_rect.united(native_client_rect(backdrop.get()));
      ASSERT_NE(SetWindowPos(reinterpret_cast<HWND>(backdrop->internalWinId()),
          HWND_TOPMOST, 0, 0, 0, 0,
          SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_NOOWNERZORDER), FALSE);
    }
    // This is outside project API dispatch. Deliver Qt exposure before arming
    // the desktop sampler; DwmFlush alone does not deliver QWidget paint events.
    QElapsedTimer backdrop_deadline;
    backdrop_deadline.start();
    bool backdrops_presented = false;
    while (!backdrops_presented && backdrop_deadline.elapsed() < 1000) {
      QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 10);
      backdrops_presented = true;
      for (const auto &backdrop : transition_backdrops) {
        backdrop->repaint();
        QCoreApplication::sendPostedEvents(backdrop.get(), QEvent::UpdateRequest);
        auto *screen = backdrop->screen();
        const auto native_backdrop = reinterpret_cast<HWND>(backdrop->internalWinId());
        const QRect client = native_client_rect(backdrop.get());
        if (!backdrop->windowHandle()->isExposed() || !IsWindowVisible(native_backdrop)) {
          backdrops_presented = false;
          break;
        }
        for (const qreal y_fraction : {0.05, 0.5, 0.95}) {
          for (const qreal x_fraction : {0.05, 0.5, 0.95}) {
            const POINT point{client.x() + qRound(client.width() * x_fraction),
                              client.y() + qRound(client.height() * y_fraction)};
            if (GetAncestor(WindowFromPoint(point), GA_ROOT) != native_backdrop) {
              backdrops_presented = false;
            }
          }
        }
        if (!backdrops_presented) break;
        const QPoint origin = backdrop->mapToGlobal(QPoint{}) - screen->geometry().topLeft();
        const QImage pixels = screen->grabWindow(0, origin.x(), origin.y(),
            backdrop->width(), backdrop->height()).toImage().convertToFormat(QImage::Format_RGB32);
        if (pixels.isNull() || pixels.size() != client.size()) {
          backdrops_presented = false;
          break;
        }
        for (int y = 0; y < pixels.height() && backdrops_presented; ++y) {
          const auto *row = reinterpret_cast<const QRgb *>(pixels.constScanLine(y));
          for (int x = 0; x < pixels.width(); ++x) {
            if (std::abs(qRed(row[x]) - 60) > 3 || std::abs(qGreen(row[x]) - 72) > 3 ||
                std::abs(qBlue(row[x]) - 64) > 3) {
              backdrops_presented = false;
              break;
            }
          }
        }
        if (!backdrops_presented) break;
      }
    }
    ASSERT_TRUE(backdrops_presented) << "controlled desktop backdrops must be fully presented before capture";
    ASSERT_NE(SetWindowPos(reinterpret_cast<HWND>(window->internalWinId()),
        HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_NOOWNERZORDER), FALSE);
    // Complete the controlled backdrop before observation starts. Never flush
    // DWM during the transition itself: intermediate composition is evidence.
    const HMODULE dwm = LoadLibraryExW(L"dwmapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    ASSERT_NE(dwm, nullptr);
    using FlushCompositor = HRESULT(WINAPI *)();
    const auto flush_compositor = reinterpret_cast<FlushCompositor>(GetProcAddress(dwm, "DwmFlush"));
    if (flush_compositor != nullptr) EXPECT_EQ(flush_compositor(), S_OK);
    FreeLibrary(dwm);
    transition_incoming_seen = false;
    ASSERT_TRUE(transition_capture.begin(capture_rect, label));
    transition_capture.set_window(reinterpret_cast<HWND>(window->internalWinId()), false);
    transition_capture_active = true;
  };
  application.root_window_event = [&](QWidget *root, QEvent::Type event) {
    if (!transition_capture_active) return;
    if (event == QEvent::Show) transition_incoming_seen = true;
    transition_capture.set_window(reinterpret_cast<HWND>(root->internalWinId()),
                                  transition_incoming_seen);
    const QRect client = native_client_rect(root);
    std::cout << "transition-root event=" << static_cast<int>(event)
              << " visible=" << root->isVisible() << " native_client="
              << client.x() << ',' << client.y() << ',' << client.width() << ','
              << client.height() << '\n';
  };
  const auto finish_transition_capture = [&] {
    if (!transition_capture_active) return;
    transition_capture.set_window(reinterpret_cast<HWND>(window->internalWinId()), false);
    const auto samples = transition_capture.finish(review);
    transition_capture_active = false;
    const QRect final_client = native_client_rect(window);
    const qreal ratio = window->devicePixelRatioF();
    const int extent = qRound(96.0 * scale_percent / 100.0 * ratio);
    const QImage authored = application.windowIcon().pixmap(QSize(extent, extent)).toImage();
    const QRect authored_bounds = purple_bounds(authored);
    const QPointF authored_offset = QRectF(authored_bounds).center() -
        QRectF(authored.rect()).center();
    int presented_logo_frames = 0;
    int caption_frames = 0;
    int intermediate_frames = 0;
    std::int64_t maximum_gap_ms = 0;
    std::int64_t previous_sample_ms = 0;
    for (const auto &sample : samples) {
      maximum_gap_ms = std::max(maximum_gap_ms, sample.elapsed_ms - previous_sample_ms);
      previous_sample_ms = sample.elapsed_ms;
      if (!sample.caption_bounds.isEmpty()) ++caption_frames;
      if (!sample.incoming || !sample.visible || sample.logo_bounds.isEmpty()) continue;
      ++presented_logo_frames;
      const QPointF center = QRectF(sample.logo_bounds).center() - authored_offset;
      if (sample.native_client != final_client ||
          std::abs(center.x() - QRectF(final_client).center().x()) > 2.0 ||
          std::abs(center.y() - QRectF(final_client).center().y()) > 2.0 ||
          std::abs(sample.logo_bounds.width() - authored_bounds.width()) > 2 ||
          std::abs(sample.logo_bounds.height() - authored_bounds.height()) > 2) {
        if (++intermediate_frames == 1) {
          std::cout << "transition-first-intermediate elapsed_ms=" << sample.elapsed_ms
                    << " logo=" << sample.logo_bounds.x() << ',' << sample.logo_bounds.y()
                    << ',' << sample.logo_bounds.width() << ',' << sample.logo_bounds.height()
                    << " final_client=" << final_client.x() << ',' << final_client.y()
                    << ',' << final_client.width() << ',' << final_client.height() << '\n';
        }
      }
    }
    std::cout << "transition-samples=" << samples.size()
              << " incoming_logo_frames=" << presented_logo_frames
              << " caption_frames=" << caption_frames
              << " maximum_gap_ms=" << maximum_gap_ms << '\n';
    EXPECT_GE(presented_logo_frames, 10);
    EXPECT_EQ(intermediate_frames, 0)
        << "the incoming native window must present its logo at final position and size";
  };
#else
  const auto begin_transition_capture = [](const QString &) {};
  const auto finish_transition_capture = [] {};
#endif
  RuntimeRebindWindowHideProbe root_hide_probe;
  const auto expect_live_composition = [&] {
    ASSERT_NE(window, nullptr);
    EXPECT_NE(window->internalWinId(), 0);
    ASSERT_NE(composition_anchor, nullptr);
    ASSERT_NE(composition_anchor->shared_context(), nullptr);
    EXPECT_NE(composition_anchor->shared_context()->device, VK_NULL_HANDLE);
  };
  const auto expect_home_placement = [&] {
    ASSERT_NE(window->screen(), nullptr);
    ASSERT_NE(window->windowHandle(), nullptr);
    const QRect area = window->screen()->availableGeometry();
    const QMargins margins = window->windowHandle()->frameMargins();
    EXPECT_FALSE(window->isMaximized());
    EXPECT_FALSE(quader::platform_window_is_maximized(
        reinterpret_cast<void *>(window->internalWinId())));
    EXPECT_EQ(window->size(), QSize(
        std::min(1200, area.width() - margins.left() - margins.right()),
        std::min(740, area.height() - margins.top() - margins.bottom())));
    EXPECT_NEAR(QRectF(window->frameGeometry()).center().x(), QRectF(area).center().x(), 1.0);
    EXPECT_NEAR(QRectF(window->frameGeometry()).center().y(), QRectF(area).center().y(), 1.0);
    EXPECT_EQ(recent->model()->rowCount(), 20);
    expect_live_composition();
  };
  const auto current_placement = [&] {
    const QRect normal = window->normalGeometry();
    return quader::PlatformWindowPlacement{
        .normal_frame = {normal.x(), normal.y(), normal.width(), normal.height()},
        .maximized = window->isMaximized()};
  };
  const auto expect_placement = [](const quader::PlatformWindowPlacement &actual,
                                   const quader::PlatformWindowPlacement &expected) {
    EXPECT_EQ(actual.normal_frame.x, expected.normal_frame.x);
    EXPECT_EQ(actual.normal_frame.y, expected.normal_frame.y);
    EXPECT_EQ(actual.normal_frame.width, expected.normal_frame.width);
    EXPECT_EQ(actual.normal_frame.height, expected.normal_frame.height);
    EXPECT_EQ(actual.maximized, expected.maximized);
  };
  const auto expect_saved_placement = [&](const std::filesystem::path &path,
                                         const quader::PlatformWindowPlacement &expected) {
    const auto saved = quader::AppWindowSettings::load(path);
    ASSERT_TRUE(saved.placement().has_value());
    expect_placement(*saved.placement(), expected);
  };
  quader::PlatformWindowPlacement first_expected = *window_settings.placement();
  quader::PlatformWindowPlacement second_expected = *second_window_settings.placement();
  EXPECT_FALSE(cover->testAttribute(Qt::WA_OpaquePaintEvent));
  EXPECT_FALSE(cover->autoFillBackground());
  EXPECT_TRUE(window->updatesEnabled());
  constexpr std::array kLoadingPaneIds{
      quader::ViewportPaneId::Perspective, quader::ViewportPaneId::Top,
      quader::ViewportPaneId::Front, quader::ViewportPaneId::Side};
  std::array<QPointer<quader::QtVulkanCompositionWidget>, 4> hosts{};
  std::array<std::uint64_t, 4> submitted_frames{};
  std::array<std::uint64_t, 4> frames_before_open{};
  QPointer<QMainWindow> bound_window;
  const auto bind_current_root = [&] {
    QMainWindow *candidate = nullptr;
    for (auto *widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() == QStringLiteral("quaderMainWindow") && widget->isVisible()) {
        candidate = qobject_cast<QMainWindow *>(widget);
        break;
      }
    }
    if (candidate == nullptr) return false;
    if (candidate == bound_window.data()) return true;
    window = candidate;
    cover = window->findChild<QWidget *>(QStringLiteral("quaderLoadingCover"));
    recent = window->findChild<QTableView *>(QStringLiteral("projectHome.recentProjects.view"));
    stack = window->findChild<QStackedWidget *>(QStringLiteral("mainCentralStack"));
    close_project = window->findChild<QAction *>(QStringLiteral("command_project_close"));
    viewport = dynamic_cast<quader::QtViewportPanel *>(
        window->findChild<QWidget *>(QStringLiteral("viewportWorkspace")));
    composition_anchor = dynamic_cast<quader::QtVulkanCompositionWidget *>(
        window->findChild<QWidget *>(QStringLiteral("viewportCompositionAnchor")));
    if (!cover || !recent || !stack || !close_project || !viewport || !composition_anchor) return false;
    for (std::size_t index = 0; index < hosts.size(); ++index) {
      const QString pane_name = QString::fromUtf8(
          quader::viewport_pane_stable_id(kLoadingPaneIds[index]).data());
      hosts[index] = dynamic_cast<quader::QtVulkanCompositionWidget *>(
          window->findChild<QWidget *>(QStringLiteral("viewportSurfaceContainer_%1").arg(pane_name)));
      if (!hosts[index]) return false;
      const QPointer<quader::QtVulkanCompositionWidget> host = hosts[index];
      QObject::connect(host.data(), &QRhiWidget::frameSubmitted, &application, [&, index, host] {
        if (host && host->isVisible()) ++submitted_frames[index];
      });
    }
    window->installEventFilter(&root_hide_probe);
    QObject::connect(window.data(), &QObject::destroyed, &application, [&] { ++destroyed_roots; });
    QObject::connect(composition_anchor.data(), &QObject::destroyed, &application,
                     [&] { ++destroyed_anchors; });
    bound_window = window;
    ++root_generation;
    return true;
  };
  ASSERT_TRUE(bind_current_root());
  const auto expect_new_presentation = [&] {
    EXPECT_EQ(root_generation, generation_before_replacement + 1);
    EXPECT_EQ(destroyed_roots, destroyed_roots_before_replacement + 1);
    EXPECT_EQ(destroyed_anchors, destroyed_anchors_before_replacement + 1);
    EXPECT_TRUE(retiring_window.isNull());
    EXPECT_TRUE(retiring_anchor.isNull());
    expect_live_composition();
  };
  const auto expect_replacement_after_commit = [&] {
    generation_before_replacement = root_generation;
    destroyed_roots_before_replacement = destroyed_roots;
    destroyed_anchors_before_replacement = destroyed_anchors;
    retiring_window = window;
    retiring_anchor = composition_anchor;
    expecting_replacement = true;
  };
  const auto expect_presented_panes = [&] {
    for (std::size_t index = 0; index < hosts.size(); ++index) {
      SCOPED_TRACE(index);
      EXPECT_GT(submitted_frames[index], frames_before_open[index])
          << "every visible pane requires its own submitted frame before reveal";
      EXPECT_TRUE(hosts[index]->isVisible());
      auto *surface = viewport->surface(kLoadingPaneIds[index]);
      ASSERT_NE(surface, nullptr);
      EXPECT_TRUE(surface->ready_to_render());
      const auto extent = surface->render_size_pixels();
      EXPECT_EQ(surface->renderer().width(), extent.x);
      EXPECT_EQ(surface->renderer().height(), extent.y);
    }
  };
  bool saved_mismatch_diagnostic = false;
  bool capture_failed = false;
  QElapsedTimer capture_mismatch_deadline;
  const auto capture = [&](const QString &name) {
    // A fresh exposed root can precede creation of its raster paint device.
    // Only inspect the backing store after an actual cover Paint completed.
    if (!window || window.data() != painted_backing_root.data()) return false;
    if (window->screen() == nullptr) return false;
    const QPoint origin = window->mapToGlobal(QPoint{}) -
        window->screen()->geometry().topLeft();
    const QImage image = window->screen()->grabWindow(
        0, origin.x(), origin.y(), window->width(), window->height()).toImage();
    if (image.isNull() || !desktop_matches_quader(window, image)) {
      if (!cover->isVisible()) {
        if (!saved_mismatch_diagnostic && !image.isNull() &&
            native_client_is_unoccluded(window)) {
          saved_mismatch_diagnostic = true;
          auto *device = window->backingStore()->paintDevice();
          if (device != nullptr && device->devType() == QInternal::Image) {
            const QImage painted = static_cast<const QImage *>(device)->copy();
            std::cout << "verified own topmost native HWND at 9 client points; diagnostic "
                      << name.toStdString() << " desktop=" << image.width() << 'x'
                      << image.height() << " format=" << image.format()
                      << " dpr=" << image.devicePixelRatio() << " backing="
                      << painted.width() << 'x' << painted.height()
                      << " format=" << painted.format()
                      << " dpr=" << painted.devicePixelRatio() << '\n';
            if (!review.isEmpty()) {
              EXPECT_TRUE(image.save(QDir(review).filePath(
                  QStringLiteral("diagnostic-native-") + name)));
              EXPECT_TRUE(painted.save(QDir(review).filePath(
                  QStringLiteral("diagnostic-backing-") + name)));
            }
          }
        }
        if (!capture_mismatch_deadline.isValid()) capture_mismatch_deadline.start();
        if (capture_mismatch_deadline.elapsed() >= 1000) {
          capture_failed = true;
          ADD_FAILURE() << name.toStdString()
                        << " native pixels differ from the settled Qt backing store";
          application.quit();
        }
      }
      return false;
    }
    capture_mismatch_deadline.invalidate();
    if (cover->isVisible() && purple_bounds(image).isEmpty()) return false;
    return review.isEmpty() || image.save(QDir(review).filePath(name));
  };
  enum class Phase {
    InitialHome, FirstProject, FailedWorkspaceOpen, OpenedSiblingScene,
    ResizeFirstProject, ReturningHome, FailedHomeOpen, ReopenedProject,
    HomeForSecondProject, SecondProject, MaximizeSecondProject,
    HomeForFirstProject, FinalFirstProject, QuitFirstProject, Complete
  };
  Phase phase = Phase::InitialHome;
  bool awaiting_project_reveal = false;
  bool awaiting_home_reveal = false;
  bool quad_layout_pending = false;
  bool observing_transition = false;
  int successful_reveals = 0;
  std::vector<std::pair<QPointer<QWidget>, QRect>> revealed_geometry;
  QRect scene_switch_geometry;
  QRect scene_switch_normal_geometry;
  int scene_switch_hide_count = 0;
  std::uint64_t scene_switch_generation = 0;
  VkDevice scene_switch_device = VK_NULL_HANDLE;
  std::uint64_t failed_open_generation = 0;
  const QRect edited_first_rect(work_area.topLeft() + QPoint(65, 65),
      QSize(std::min(1550, work_area.width() - 130),
            std::min(820, work_area.height() - 130)));
  const QRect quitting_first_rect = edited_first_rect.adjusted(10, 10, 30, 20);
  QElapsedTimer reveal_observation;
  reveal_probe.revealed = [&] {
    if (phase != Phase::InitialHome && !awaiting_project_reveal && !awaiting_home_reveal) return;
    ASSERT_TRUE(bind_current_root());
    if (expecting_replacement && root_generation == generation_before_replacement) return;
    if (expecting_replacement) {
      expect_new_presentation();
      expecting_replacement = false;
    }
    expect_live_composition();
    finish_transition_capture();
    if (phase == Phase::InitialHome) {
      recording_early_frames = false;
      const QRect final_client = native_client_rect(window);
      std::cout << "loading-first-reveal native_client=" << final_client.x() << ','
                << final_client.y() << ',' << final_client.width() << ','
                << final_client.height() << " center="
                << QRectF(final_client).center().x() << ','
                << QRectF(final_client).center().y() << '\n';
      expect_home_placement();
      EXPECT_EQ(read_binary_file(first_placement_path), first_placement_before_home);
      EXPECT_EQ(read_binary_file(second_placement_path), second_placement_before_home);
      EXPECT_TRUE(observed_early_logo) << "must inspect actual logo pixels before reveal";
      EXPECT_GT(desktop_frame_count, 0);
      for (std::size_t index = 0; index < early_frames.size(); ++index) {
        SCOPED_TRACE(index);
        const auto &frame = early_frames[index];
        // A transitional frame may contain only the backdrop. Assert geometry
        // only when pixels prove the logo was actually drawn in this frame.
        if (frame.logo_pixels.isEmpty()) continue;
        EXPECT_EQ(frame.retained_pixels, frame.native_client.size());
        EXPECT_EQ(frame.cover_pixels.size(), frame.native_client.size());
        const QPointF painted_center = QRectF(frame.logo_pixels).center() +
            QPointF(frame.native_client.topLeft()) - frame.authored_offset;
        EXPECT_NEAR(painted_center.x(), QRectF(frame.native_client).center().x(), 2.0);
        EXPECT_NEAR(painted_center.y(), QRectF(frame.native_client).center().y(), 2.0);
        EXPECT_NEAR(painted_center.x(), QRectF(final_client).center().x(), 2.0)
            << "the first visible logo must already have its final screen position";
        EXPECT_NEAR(painted_center.y(), QRectF(final_client).center().y(), 2.0)
            << "the first visible logo must already have its final screen position";
      }
    }
    if (awaiting_project_reveal) {
      EXPECT_FALSE(quad_layout_pending);
      expect_presented_panes();
    }
    if (awaiting_home_reveal) expect_home_placement();
    ASSERT_NE(reveal_probe.first_paint, std::chrono::steady_clock::time_point{});
    const auto displayed_for = std::chrono::steady_clock::now() - reveal_probe.first_paint;
    EXPECT_GE(std::chrono::duration_cast<std::chrono::milliseconds>(displayed_for).count(), 2000);
    ++successful_reveals;
    awaiting_project_reveal = false;
    awaiting_home_reveal = false;
    revealed_geometry.clear();
    revealed_geometry.emplace_back(window, window->geometry());
    revealed_geometry.emplace_back(stack->currentWidget(), stack->currentWidget()->geometry());
    for (auto *widget : window->findChildren<QWidget *>()) {
      const auto name = widget->objectName();
      if (widget->isVisible() && (qobject_cast<QDockWidget *>(widget) != nullptr ||
          name.startsWith(QStringLiteral("viewportPane_")) ||
          name.startsWith(QStringLiteral("viewportSurfaceContainer_")) ||
          name == QStringLiteral("projectHome.title") ||
          name == QStringLiteral("projectHome.recentProjects.frame") ||
          name == QStringLiteral("projectHome.tutorial.card") ||
          name == QStringLiteral("projectHome.documentation.card"))) {
        revealed_geometry.emplace_back(widget, widget->geometry());
      }
    }
    reveal_observation.start();
  };
  bool captured_initial_cover = false;
  bool captured_project_cover = false;
  bool captured_reopen_cover = false;
  int uncovered_samples = 0;
  const auto select_recent = [&](const std::filesystem::path &path) {
    for (int row = 0; row < recent->model()->rowCount(); ++row) {
      const auto index = recent->model()->index(row, 0);
      const auto candidate = std::filesystem::path(
          index.data(quader::QtRecentProjectsModel::ManifestPathRole).toString().toStdWString());
      if (candidate.lexically_normal() == path.lexically_normal()) {
        recent->setCurrentIndex(index);
        recent->scrollTo(index);
        return;
      }
    }
    FAIL() << "missing recent fixture " << path.string();
  };
  const auto open_recent = [&](const std::filesystem::path &path) {
    ASSERT_EQ(recent->model()->rowCount(), 20);
    frames_before_open = submitted_frames;
    select_recent(path);
    expect_replacement_after_commit();
    send_key(*recent, Qt::Key_Return);
    // Layout is transient scene-session state. Apply its ordinary command once
    // the App refresh has rebound its retained actions, while loading is covered.
    quad_layout_pending = true;
    awaiting_project_reveal = true;
    revealed_geometry.clear();
  };
  const auto close_to_home = [&](Phase next) {
    ASSERT_TRUE(close_project->isEnabled());
    if (next == Phase::HomeForFirstProject) {
      begin_transition_capture(QStringLiteral("transition-project-to-home"));
    }
    revealed_geometry.clear();
    expect_replacement_after_commit();
    close_project->trigger();
    phase = next;
    awaiting_home_reveal = true;
  };
  int acknowledged_errors = 0;
  const auto acknowledge_expected_error = [&] {
    QTimer::singleShot(0, &application, [&] {
      auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
      ASSERT_NE(dialog, nullptr);
      EXPECT_EQ(dialog->objectName(), QStringLiteral("lifecycle_error_message_box"));
      auto *accept = dialog->findChild<QPushButton *>(QStringLiteral("lifecycleMessageBox.ok"));
      ASSERT_NE(accept, nullptr);
      ++acknowledged_errors;
      accept->click();
    });
  };
  QTimer observer(&application);
  observer.setInterval(5);
  QObject::connect(&observer, &QTimer::timeout, &application, [&] {
    if (observing_transition || capture_failed) return;
    const QScopedValueRollback observation_guard(observing_transition, true);
    if (!bind_current_root()) return;
    if (expecting_replacement && root_generation == generation_before_replacement) return;
    if (cover->isVisible()) {
      uncovered_samples = 0;
      const auto *perspective_surface = viewport->surface(quader::ViewportPaneId::Perspective);
      if (quad_layout_pending && perspective_surface != nullptr &&
          perspective_surface->ready_to_render()) {
        auto *layout_menu = window->findChild<QMenu *>(
            QStringLiteral("viewportLayoutMenu_perspective"));
        ASSERT_NE(layout_menu, nullptr);
        ASSERT_TRUE(QMetaObject::invokeMethod(layout_menu, "aboutToShow", Qt::DirectConnection));
        auto *quad_layout = layout_menu->findChild<QAction *>(QStringLiteral("layout_four_quad"));
        ASSERT_NE(quad_layout, nullptr);
        if (quad_layout->isEnabled()) {
          quad_layout->trigger();
          quad_layout_pending = false;
        }
      }
      if (phase == Phase::InitialHome && !captured_initial_cover) {
        captured_initial_cover = capture(QStringLiteral("loading-app-initial-cover.png"));
      } else if (phase == Phase::FirstProject && !captured_project_cover) {
        captured_project_cover = capture(QStringLiteral("loading-app-project-cover.png"));
      } else if (phase == Phase::ReopenedProject && !captured_reopen_cover) {
        captured_reopen_cover = capture(QStringLiteral("loading-app-reopen-cover.png"));
      }
      return;
    }
    for (const auto &[widget, geometry] : revealed_geometry) {
      ASSERT_NE(widget, nullptr);
      EXPECT_EQ(widget->geometry(), geometry)
          << widget->objectName().toStdString() << " shifted after the cover was removed";
    }
    if (!revealed_geometry.empty() && reveal_observation.elapsed() < 250) return;
    // Give the native compositor a turn after the retained cover is removed.
    if (++uncovered_samples < 3) return;
    uncovered_samples = 0;
    if (phase == Phase::InitialHome) {
      EXPECT_NE(dynamic_cast<quader::QtProjectHomePage *>(stack->currentWidget()), nullptr);
      expect_home_placement();
      if (!capture(QStringLiteral("loading-app-home-ready.png"))) return;
      if (home_only) {
        phase = Phase::Complete;
        application.quit();
        return;
      }
      phase = Phase::FirstProject;
      begin_transition_capture(QStringLiteral("transition-home-to-project"));
      open_recent(manifest_path);
    } else if (phase == Phase::FirstProject || phase == Phase::ReopenedProject) {
      expect_presented_panes();
      expect_placement(current_placement(), first_expected);
      EXPECT_EQ(window->screen(), screens[screen_index]);
      if (!capture(phase == Phase::FirstProject
          ? QStringLiteral("loading-app-project-ready.png")
          : QStringLiteral("loading-app-reopen-ready.png"))) return;
      if (phase == Phase::ReopenedProject) {
        close_to_home(Phase::HomeForSecondProject);
      } else {
        auto *dock = window->findChild<QDockWidget *>(QStringLiteral("dock_asset_browser"));
        ASSERT_NE(dock, nullptr);
        auto *browser = dynamic_cast<quader::QtAssetBrowser *>(dock->widget());
        ASSERT_NE(browser, nullptr);
        ASSERT_TRUE(dock->isVisible());
        ASSERT_TRUE(browser->reveal_item(unopened_scene_identity));
        auto *tabs = window->findChild<QTabBar *>(QStringLiteral("sceneWorkspace.tabs"));
        ASSERT_NE(tabs, nullptr);
        const int tab_count = tabs->count();
        const int active_tab = tabs->currentIndex();
        const QString tab_name = tabs->tabText(active_tab);
        const auto valid_scene = read_binary_file(unopened_scene_path);
        ASSERT_TRUE(valid_scene.has_value());
        {
          std::ofstream corrupt(unopened_scene_path, std::ios::binary | std::ios::trunc);
          corrupt << "invalid scene fixture";
        }
        class PaneHideProbe final : public QObject {
        public:
          int hidden_count = 0;
        protected:
          bool eventFilter(QObject *watched, QEvent *event) override {
            if (event->type() == QEvent::Hide) ++hidden_count;
            return QObject::eventFilter(watched, event);
          }
        } hide_probe;
        std::array<QRect, 4> previous_geometry{};
        for (std::size_t index = 0; index < hosts.size(); ++index) {
          hosts[index]->installEventFilter(&hide_probe);
          previous_geometry[index] = hosts[index]->geometry();
        }
        failed_open_generation = root_generation;
        acknowledge_expected_error();
        EXPECT_TRUE(browser->handle_action(quader::QtAssetBrowserAction::Open));
        EXPECT_TRUE(cover->isHidden());
        EXPECT_EQ(hide_probe.hidden_count, 0)
            << "a failed preflight must preserve the displayed viewport texture";
        for (std::size_t index = 0; index < hosts.size(); ++index) {
          hosts[index]->removeEventFilter(&hide_probe);
          EXPECT_TRUE(hosts[index]->isVisible());
          EXPECT_EQ(hosts[index]->geometry(), previous_geometry[index]);
        }
        EXPECT_EQ(tabs->count(), tab_count);
        EXPECT_EQ(tabs->currentIndex(), active_tab);
        EXPECT_EQ(tabs->tabText(active_tab), tab_name);
        {
          std::ofstream restored(unopened_scene_path, std::ios::binary | std::ios::trunc);
          restored << *valid_scene;
        }
        phase = Phase::FailedWorkspaceOpen;
      }
    } else if (phase == Phase::FailedWorkspaceOpen) {
      EXPECT_EQ(root_generation, failed_open_generation);
      expect_presented_panes();
      if (!capture(QStringLiteral("loading-app-failed-workspace-open.png"))) return;
      auto *dock = window->findChild<QDockWidget *>(QStringLiteral("dock_asset_browser"));
      ASSERT_NE(dock, nullptr);
      auto *browser = dynamic_cast<quader::QtAssetBrowser *>(dock->widget());
      ASSERT_NE(browser, nullptr);
      ASSERT_TRUE(browser->reveal_item(unopened_scene_identity));
      scene_switch_geometry = window->geometry();
      scene_switch_normal_geometry = window->normalGeometry();
      scene_switch_hide_count = root_hide_probe.hide_count;
      scene_switch_generation = root_generation;
      scene_switch_device = composition_anchor->shared_context()->device;
      frames_before_open = submitted_frames;
      ASSERT_TRUE(browser->handle_action(quader::QtAssetBrowserAction::Open));
      awaiting_project_reveal = true;
      quad_layout_pending = true;
      revealed_geometry.clear();
      phase = Phase::OpenedSiblingScene;
    } else if (phase == Phase::OpenedSiblingScene) {
      expect_presented_panes();
      EXPECT_EQ(window->geometry(), scene_switch_geometry);
      EXPECT_EQ(window->normalGeometry(), scene_switch_normal_geometry);
      EXPECT_EQ(root_hide_probe.hide_count, scene_switch_hide_count)
          << "opening another scene in one project must preserve editor placement";
      EXPECT_EQ(root_generation, scene_switch_generation);
      expect_live_composition();
      EXPECT_EQ(composition_anchor->shared_context()->device, scene_switch_device);
      revealed_geometry.clear();
      window->showNormal();
      window->setGeometry(edited_first_rect);
      phase = Phase::ResizeFirstProject;
    } else if (phase == Phase::ResizeFirstProject) {
      const auto extent = native_client_rect(window).size();
      const qreal ratio = window->devicePixelRatioF();
      if (window->isMaximized() || window->geometry() != edited_first_rect ||
          extent != QSize(qRound(window->width() * ratio), qRound(window->height() * ratio))) return;
      first_expected = current_placement();
      close_to_home(Phase::ReturningHome);
    } else if (phase == Phase::ReturningHome) {
      EXPECT_NE(dynamic_cast<quader::QtProjectHomePage *>(stack->currentWidget()), nullptr);
      expect_home_placement();
      expect_saved_placement(first_placement_path, first_expected);
      expect_saved_placement(second_placement_path, second_expected);
      std::ifstream input(manifest_path, std::ios::binary);
      const std::string valid_manifest((std::istreambuf_iterator<char>(input)),
                                       std::istreambuf_iterator<char>());
      input.close();
      ASSERT_FALSE(valid_manifest.empty());
      {
        std::ofstream corrupt(manifest_path, std::ios::binary | std::ios::trunc);
        corrupt << "invalid project fixture";
      }
      select_recent(manifest_path);
      failed_open_generation = root_generation;
      acknowledge_expected_error();
      send_key(*recent, Qt::Key_Return);
      EXPECT_TRUE(cover->isHidden()) << "a failed open must immediately restore Home";
      EXPECT_NE(dynamic_cast<quader::QtProjectHomePage *>(stack->currentWidget()), nullptr);
      {
        std::ofstream restored(manifest_path, std::ios::binary | std::ios::trunc);
        restored << valid_manifest;
      }
      phase = Phase::FailedHomeOpen;
    } else if (phase == Phase::FailedHomeOpen) {
      EXPECT_EQ(root_generation, failed_open_generation);
      if (!capture(QStringLiteral("loading-app-failed-open.png"))) return;
      phase = Phase::ReopenedProject;
      open_recent(manifest_path);
    } else if (phase == Phase::HomeForSecondProject) {
      expect_home_placement();
      expect_saved_placement(first_placement_path, first_expected);
      phase = Phase::SecondProject;
      open_recent(second_manifest_path);
    } else if (phase == Phase::SecondProject) {
      expect_presented_panes();
      expect_placement(current_placement(), second_expected);
      EXPECT_EQ(window->screen(), screens.front());
      if (!capture(QStringLiteral("loading-app-second-project-ready.png"))) return;
      revealed_geometry.clear();
      window->showMaximized();
      phase = Phase::MaximizeSecondProject;
    } else if (phase == Phase::MaximizeSecondProject) {
      if (!window->isMaximized() || !quader::platform_window_is_maximized(
          reinterpret_cast<void *>(window->internalWinId()))) return;
      second_expected = current_placement();
      close_to_home(Phase::HomeForFirstProject);
    } else if (phase == Phase::HomeForFirstProject) {
      expect_home_placement();
      expect_saved_placement(first_placement_path, first_expected);
      expect_saved_placement(second_placement_path, second_expected);
      phase = Phase::FinalFirstProject;
      open_recent(manifest_path);
    } else if (phase == Phase::FinalFirstProject) {
      expect_presented_panes();
      expect_placement(current_placement(), first_expected);
      EXPECT_EQ(window->screen(), screens[screen_index]);
      if (!capture(QStringLiteral("loading-app-first-project-restored.png"))) return;
      revealed_geometry.clear();
      window->setGeometry(quitting_first_rect);
      phase = Phase::QuitFirstProject;
    } else if (phase == Phase::QuitFirstProject) {
      const qreal ratio = window->devicePixelRatioF();
      if (window->geometry() != quitting_first_rect || native_client_rect(window).size() !=
          QSize(qRound(window->width() * ratio), qRound(window->height() * ratio))) return;
      first_expected = current_placement();
      phase = Phase::Complete;
      application.quit();
    }
  });
  QTimer deadline(&application);
  deadline.setSingleShot(true);
  QObject::connect(&deadline, &QTimer::timeout, &application, [&] {
    ADD_FAILURE() << "loading transition did not finish; phase=" << static_cast<int>(phase);
    application.quit();
  });
  observer.start();
  deadline.start(45000);
  EXPECT_EQ(app.run(), 0);
  observer.stop();
  deadline.stop();
#if defined(_WIN32)
  if (transition_capture_active) {
    // Initialization can fail before the incoming cover reveals. Retain those
    // intermediate desktop pixels without treating an unfinished run as proof.
    transition_capture.set_window(nullptr, false);
    const auto samples = transition_capture.finish(review);
    transition_capture_active = false;
    std::cout << "unfinished transition retained " << samples.size()
              << " desktop samples; phase=" << static_cast<int>(phase) << '\n';
  }
#endif
  EXPECT_EQ(phase, Phase::Complete);
  EXPECT_TRUE(captured_initial_cover);
  EXPECT_EQ(captured_project_cover, !home_only);
  EXPECT_EQ(captured_reopen_cover, !home_only);
  EXPECT_EQ(successful_reveals, home_only ? 1 : 9);
  EXPECT_EQ(acknowledged_errors, home_only ? 0 : 2);
  application.removeEventFilter(&reveal_probe);
  if (window) window->removeEventFilter(&root_hide_probe);
  application.cover_painted = {};
  application.root_updated = {};
  application.root_window_event = {};
  app.shutdown();
  if (home_only) {
    EXPECT_EQ(read_binary_file(first_placement_path), first_placement_before_home);
    EXPECT_EQ(read_binary_file(second_placement_path), second_placement_before_home);
  } else {
    expect_saved_placement(first_placement_path, first_expected);
    expect_saved_placement(second_placement_path, second_expected);
  }
  if (!preview_root.isEmpty()) {
    std::cout << "retained project-list preview LOCALAPPDATA=" << profile.string() << '\n';
  }
}

TEST(QtProjectWorkspaceTests, KnifeRealAppShowsHintAngleAndLengthOnTopFace) {
  if (qEnvironmentVariableIntValue("QUADER_RUN_VULKAN_KNIFE_APP_TESTS") != 1) {
    GTEST_SKIP() << "Set QUADER_RUN_VULKAN_KNIFE_APP_TESTS=1 for the real App Knife presentation.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  const auto profile = std::filesystem::temp_directory_path() /
      ("quader-knife-app-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  ScopedIsolatedLocalAppData scoped_profile(profile);
  ASSERT_TRUE(scoped_profile.active());
  int argc = 1;
  char application_name[] = "quader-knife-real-app-test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  application.setApplicationName(QStringLiteral("Quader"));
  application.setOrganizationName(QStringLiteral("Quader"));
  QApplication::setStyle(QStringLiteral("Fusion"));
  quader::apply_qt_obsidian_application_theme(QApplication::style()->standardPalette());
  quader::install_qt_application_default_cursor(application);
  const auto executable = std::filesystem::path(
      QCoreApplication::applicationFilePath().toStdWString());
  const auto resources = executable.parent_path();
  const quader::ViewportCameraPose pose{
      .eye = {6, 7, 9}, .target = {0, 1, 0}, .up = {0, 1, 0}};
  const auto project_root = profile / "Knife Project";
  ASSERT_TRUE(std::filesystem::create_directory(project_root));
  {
    quader::ProjectSession project;
    project.set_resource_root(resources);
    quader::QuaderEditorApi editor(project);
    ASSERT_TRUE(editor.project().create(project_root).ok());
    ASSERT_TRUE(editor.viewports().set_layout(quader::ViewportLayoutId::Single).ok());
    ASSERT_TRUE(editor.viewports().set_shading_mode(
        quader::ViewportPaneId::Perspective, quader::QuaderShadingMode::Shaded).ok());
    ASSERT_TRUE(editor.modeling().create_box(
        {.min = {-2, -1, -2}, .max = {2, 1, 2}}, "Knife App box").ok());
    ASSERT_TRUE(project.active_scene()->session->restore_persisted_viewport_camera_pose(pose));
    const auto scene = editor.scene_documents().active_scene_id();
    ASSERT_TRUE(scene.has_value());
    const auto path = quader::ProjectRelativePath::parse_portable("Knife.qdr");
    ASSERT_TRUE(path.ok());
    ASSERT_TRUE(editor.scene_documents().save_as(*scene, path.value()).ok());
  }
  const auto settings_paths = quader::UISettings::resolve_paths(resources);
  auto settings = quader::UISettings::defaults();
  settings.set_interface_scale(1.0F);
  settings.set_panel_visible(quader::UIPanelId::AssetBrowser, false);
  ASSERT_TRUE(settings.save(settings_paths.settings_file));
  auto viewport_settings = quader::ViewportSettings::defaults();
  viewport_settings.set_bool_value(quader::ViewportBoolSettingId::ShowDimensions, true);
  viewport_settings.set_bool_value(quader::ViewportBoolSettingId::ShowHoverDimensions, false);
  ASSERT_TRUE(viewport_settings.save(settings_paths.viewport_settings_file));
  const QString review = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
  if (!review.isEmpty()) ASSERT_TRUE(QDir{}.mkpath(review));
  const bool dimension_review = qEnvironmentVariableIntValue(
      "QUADER_KNIFE_APP_DIMENSION_REVIEW") == 1;
  ASSERT_FALSE(dimension_review && review.isEmpty())
      << "Dimension review requires QUADER_UI_REVIEW_DIR for desktop captures.";
  constexpr std::array kDimensionReviewFontPoints{6.0F, 8.0F, 10.0F, 12.0F};
  constexpr std::array kDimensionReviewScales{1.0F, 2.0F, 1.0F};
  constexpr std::size_t kDimensionReviewCount =
      kDimensionReviewFontPoints.size() * kDimensionReviewScales.size();
  const auto review_screens = QGuiApplication::screens();
  const std::size_t screen_review_count = review_screens.size() >= 2 ? 2U : 0U;
  const bool screen_review_only = dimension_review &&
      qEnvironmentVariableIntValue("QUADER_KNIFE_APP_SCREEN_REVIEW_ONLY") == 1;
  std::size_t dimension_review_index = screen_review_only ? kDimensionReviewCount : 0U;
  QPointer<QScreen> original_review_screen;
  QPointer<QScreen> requested_review_screen;
  bool dimension_review_pending = false;
  if (dimension_review && screen_review_count == 0U) {
    std::cout << "DIMENSION_APP_SCREEN_REVIEW skipped: fewer than two physical screens."
              << std::endl;
  }
  quader::App app;
  ASSERT_TRUE(app.initialize(application, executable));
  QPointer<QMainWindow> window;
  QPointer<quader::QtVulkanCompositionWidget> host;
  quader::QtViewportPanel *viewport = nullptr;
  quader::QtViewportSurface *surface = nullptr;
  quader::ViewportCameraController camera;
  ASSERT_TRUE(camera.restore_perspective_pose(pose));
  std::uint64_t submitted = 0U;
  std::uint64_t phase_submission = 0U;
  int phase = 0;
  QElapsedTimer clock;
  clock.start();
  qint64 phase_started = 0;
  QRect angle_pixels;
  QRect length_pixels;
  const auto advance = [&] {
    ++phase;
    phase_started = clock.elapsed();
    phase_submission = submitted;
    std::cout << "KNIFE_APP_PHASE " << phase << " submitted=" << submitted << std::endl;
  };
  const auto key = [&](int value) {
    QKeyEvent press(QEvent::KeyPress, value, Qt::NoModifier);
    QKeyEvent release(QEvent::KeyRelease, value, Qt::NoModifier);
    QApplication::sendEvent(host, &press);
    QApplication::sendEvent(host, &release);
  };
  const auto local_point = [&](quader::QVec3 world) {
    const auto point = camera.project_world_point(world);
    EXPECT_TRUE(point.visible);
    return QPoint{qRound(point.position.x / surface->input_device_pixel_ratio()),
                  qRound(point.position.y / surface->input_device_pixel_ratio())};
  };
  const auto warm_pixels = [&](const QImage &image, quader::QVec3 world, bool orange) {
    const auto point = camera.project_world_point(world);
    const QRect patch(qRound(point.position.x) - 5, qRound(point.position.y) - 5, 11, 11);
    int count = 0;
    for (int y = patch.top(); y <= patch.bottom(); ++y) {
      for (int x = patch.left(); x <= patch.right(); ++x) {
        if (!image.rect().contains(x, y)) continue;
        const QColor color = image.pixelColor(x, y);
        const bool warm = color.red() > 130 && color.green() > 80 &&
                          color.green() > color.blue() + 35;
        count += orange ? warm && color.red() > color.green() + 70
                        : warm && color.green() > 180 && std::abs(color.red() - color.green()) < 65;
      }
    }
    return count;
  };
  const auto glyph_pixels = [](const QImage &image, QRect bounds) {
    int count = 0;
    bounds = bounds.intersected(image.rect());
    for (int y = bounds.top(); y <= bounds.bottom(); ++y) {
      for (int x = bounds.left(); x <= bounds.right(); ++x) {
        const auto color = image.pixelColor(x, y);
        count += color.red() > 195 && color.green() > 195 && color.blue() > 195;
      }
    }
    return count;
  };
  const auto capture = [&](const char *name) {
    const QPoint origin = window->mapToGlobal(QPoint{}) - window->screen()->geometry().topLeft();
    const QImage pixels = window->screen()->grabWindow(
        0, origin.x(), origin.y(), window->width(), window->height()).toImage();
    EXPECT_FALSE(pixels.isNull());
    if (!review.isEmpty()) EXPECT_TRUE(pixels.save(QDir(review).filePath(
        QStringLiteral("knife-real-app-%1.png").arg(QString::fromLatin1(name)))));
    return pixels;
  };
  const auto trigger = [&](quader::QuaderCommandId id) {
    for (auto *action : window->findChildren<QAction *>()) {
      if (action->property("quaderCommandId").toString() !=
          QString::fromUtf8(id.string().data(), static_cast<qsizetype>(id.string().size()))) continue;
      if (auto *menu = qobject_cast<QMenu *>(action->parent())) {
        while (auto *parent = qobject_cast<QMenu *>(menu->parent())) menu = parent;
        EXPECT_TRUE(QMetaObject::invokeMethod(menu, "aboutToShow", Qt::DirectConnection));
      }
      std::cout << "KNIFE_APP_COMMAND " << id.string()
                << " enabled=" << action->isEnabled() << " phase=" << phase << std::endl;
      if (!action->isEnabled()) {
        ADD_FAILURE() << "Disabled App command " << id.string() << " at phase " << phase;
        capture("disabled-command");
        application.quit();
        return false;
      }
      action->trigger();
      return true;
    }
    ADD_FAILURE() << "Missing App command action " << id.string();
    capture("missing-command");
    application.quit();
    return false;
  };
  QTimer observer(&application);
  observer.setInterval(25);
  QObject::connect(&observer, &QTimer::timeout, &application, [&] {
    if (::testing::Test::HasFailure()) {
      if (window) capture("failed");
      observer.stop();
      application.quit();
      return;
    }
    for (auto *widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("quaderMainWindow") || !widget->isVisible()) continue;
      auto *root = qobject_cast<QMainWindow *>(widget);
      if (window == root) break;
      window = root;
      viewport = dynamic_cast<quader::QtViewportPanel *>(root->findChild<QWidget *>(QStringLiteral("viewportWorkspace")));
      ASSERT_NE(viewport, nullptr);
      surface = viewport->surface(quader::ViewportPaneId::Perspective);
      host = dynamic_cast<quader::QtVulkanCompositionWidget *>(root->findChild<QWidget *>(
          QStringLiteral("viewportSurfaceContainer_perspective")));
      ASSERT_NE(host, nullptr);
      QObject::connect(host, &QRhiWidget::frameSubmitted, &application, [&] { ++submitted; });
      root->raise();
      QApplication::setActiveWindow(root);
#if defined(_WIN32)
      ASSERT_NE(SetWindowPos(reinterpret_cast<HWND>(root->internalWinId()), HWND_TOPMOST,
          0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE), FALSE);
#endif
      break;
    }
    if (!window || !host) return;
    auto *cover = window->findChild<QWidget *>(QStringLiteral("quaderLoadingCover"));
    if (cover == nullptr || cover->isVisible()) return;
    if (phase == 0) {
      auto *recent = window->findChild<QTableView *>(QStringLiteral("projectHome.recentProjects.view"));
      ASSERT_NE(recent, nullptr);
      ASSERT_EQ(recent->model()->rowCount(), 1);
      ASSERT_TRUE(QMetaObject::invokeMethod(recent, "activated", Qt::DirectConnection,
          Q_ARG(QModelIndex, recent->model()->index(0, 0))));
      advance();
      return;
    }
    if (!surface->ready_to_render() || submitted < phase_submission + 2U ||
        clock.elapsed() - phase_started < 200) return;
    const auto extent = surface->render_size_pixels();
    camera.set_viewport_size(extent.x, extent.y);
    auto *angle = viewport->findChild<QLabel *>(QStringLiteral("viewportKnifeAngle_perspective"));
    auto *length = viewport->findChild<QLabel *>(QStringLiteral("viewportKnifeLength_perspective"));
    ASSERT_NE(angle, nullptr);
    ASSERT_NE(length, nullptr);
    switch (phase) {
    case 1: {
      auto *tree = window->findChild<QTreeView *>(QStringLiteral("outliner.tree"));
      ASSERT_NE(tree, nullptr);
      const auto matches = tree->model()->match(tree->model()->index(0, 0),
          Qt::DisplayRole, QStringLiteral("Knife App box"), 1,
          Qt::MatchExactly | Qt::MatchRecursive);
      ASSERT_EQ(matches.size(), 1);
      tree->selectionModel()->setCurrentIndex(matches.front(),
          QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
      EXPECT_EQ(tree->selectionModel()->selectedRows().size(), 1);
      std::cout << "KNIFE_APP_TARGET name=" << matches.front().data().toString().toStdString()
                << " entity=" << matches.front().data(Qt::UserRole + 1).toULongLong() << std::endl;
      break;
    }
    case 2: {
      // Outliner selection publishes through a queued owner callback. Enter
      // component mode only after a later App frame has received that selection.
      if (dimension_review &&
          dimension_review_index < kDimensionReviewCount + screen_review_count) {
        const bool screen_review = dimension_review_index >= kDimensionReviewCount;
        const std::size_t scale_index = screen_review ? 2U :
            dimension_review_index / kDimensionReviewFontPoints.size();
        const float font_points = screen_review ? 12.0F : kDimensionReviewFontPoints[
            dimension_review_index % kDimensionReviewFontPoints.size()];
        const float scale = kDimensionReviewScales[scale_index];
        if (!dimension_review_pending) {
          if (screen_review) {
            if (original_review_screen == nullptr) original_review_screen = window->screen();
            QScreen *target = original_review_screen;
            if (dimension_review_index == kDimensionReviewCount) {
              for (auto *candidate : review_screens) {
                if (candidate != original_review_screen) {
                  target = candidate;
                  break;
                }
              }
            }
            ASSERT_NE(target, nullptr);
            requested_review_screen = target;
            window->showNormal();
            window->windowHandle()->setScreen(target);
            window->setGeometry(target->availableGeometry());
            window->showMaximized();
          } else {
            // Commit through the retained Settings owner so the real App applies
            // both its font preference and scale to the composed viewport.
            ASSERT_TRUE(viewport_settings.set_float_value(
                quader::ViewportFloatSettingId::DimensionFontSize, font_points));
            auto *settings_action = window->findChild<QAction *>(
                QStringLiteral("action_settings"));
            ASSERT_NE(settings_action, nullptr);
            settings_action->trigger();
            auto *dialog = window->findChild<QDialog *>(
                QStringLiteral("settingsDialog"));
            ASSERT_NE(dialog, nullptr);
            ASSERT_TRUE(dialog->isVisible());
            auto *font_setting = dialog->findChild<QDoubleSpinBox *>(
                QStringLiteral("viewportSetting_DimensionFontSize"));
            auto *scale_setting = dialog->findChild<QSpinBox *>(
                QStringLiteral("settingsInterfaceScale"));
            auto *accept = dialog->findChild<QPushButton *>(
                QStringLiteral("settingsOk"));
            ASSERT_NE(font_setting, nullptr);
            ASSERT_NE(scale_setting, nullptr);
            ASSERT_NE(accept, nullptr);
            font_setting->setValue(viewport_settings.float_value(
                quader::ViewportFloatSettingId::DimensionFontSize));
            scale_setting->setValue(qRound(100.0F * scale));
            accept->click();
            ASSERT_FALSE(dialog->isVisible());
          }
          window->raise();
          QApplication::setActiveWindow(window);
          dimension_review_pending = true;
          phase_started = clock.elapsed();
          phase_submission = submitted;
          return;
        }
        const QString name = screen_review
            ? QStringLiteral("dimensions-12pt-screen-transition%1")
                  .arg(dimension_review_index - kDimensionReviewCount)
            : QStringLiteral("dimensions-%1pt-scale%2-pass%3")
                  .arg(qRound(font_points)).arg(qRound(scale)).arg(scale_index);
        const QImage pixels = capture(name.toUtf8().constData());
        const double ratio = static_cast<double>(pixels.width()) / window->width();
        ASSERT_NE(window->screen(), nullptr);
        if (screen_review) EXPECT_EQ(window->screen(), requested_review_screen);
        const qreal screen_pixel_ratio = window->screen()->devicePixelRatio();
        int visible_dimensions = 0;
        for (const QString &axis : {QStringLiteral("X"), QStringLiteral("Y"),
                                    QStringLiteral("Z")}) {
          auto *label = viewport->findChild<QLabel *>(
              QStringLiteral("viewportSelectionDimension%1_perspective").arg(axis));
          ASSERT_NE(label, nullptr);
          if (!label->isVisible()) continue;
          ++visible_dimensions;
          EXPECT_DOUBLE_EQ(label->font().pointSizeF(), font_points * scale);
          EXPECT_EQ(label->internalWinId(), 0U);
          EXPECT_TRUE(label->mask().isEmpty());
          EXPECT_DOUBLE_EQ(label->devicePixelRatioF(), screen_pixel_ratio);
          EXPECT_DOUBLE_EQ(label->property("quaderDimensionRasterDevicePixelRatio").toReal(),
                           screen_pixel_ratio);
          const QPoint origin = label->mapTo(window, QPoint{});
          const QRect bounds(qRound(origin.x() * ratio), qRound(origin.y() * ratio),
              qRound(label->width() * ratio), qRound(label->height() * ratio));
          EXPECT_TRUE(pixels.copy(bounds.intersected(pixels.rect())).save(
              QDir(review).filePath(QStringLiteral("knife-real-app-%1-%2.png")
                  .arg(name, axis))));
          std::cout << "DIMENSION_APP_REVIEW font_pt=" << font_points
                    << " scale=" << scale << " pass=" << scale_index
                    << " axis=" << axis.toStdString()
                    << " screen=" << window->screen()->name().toStdString()
                    << " screen_dpr=" << screen_pixel_ratio
                    << " dpr=" << label->devicePixelRatioF()
                    << " qt_scale_factor=" << qEnvironmentVariable(
                        "QT_SCALE_FACTOR", QStringLiteral("unset")).toStdString()
                    << std::endl;
        }
        EXPECT_GE(visible_dimensions, 2);
        ++dimension_review_index;
        dimension_review_pending = false;
        phase_started = clock.elapsed();
        phase_submission = submitted;
        return;
      }
      auto *face_mode = window->findChild<QAction *>(QStringLiteral("selectionModeFace"));
      ASSERT_NE(face_mode, nullptr);
      ASSERT_TRUE(face_mode->isEnabled());
      face_mode->trigger();
      ASSERT_TRUE(face_mode->isChecked());
      host->setFocus(Qt::OtherFocusReason);
      QEnterEvent enter(QPointF(host->rect().center()), QPointF(host->rect().center()),
          QPointF(host->mapToGlobal(host->rect().center())));
      QApplication::sendEvent(host, &enter);
      break;
    }
    case 3:
      if (!trigger(quader::quader_command::kToolsActivateKnife)) return;
      break;
    case 4: {
      auto *knife = window->findChild<QAction *>(QStringLiteral("tool_knife"));
      ASSERT_NE(knife, nullptr);
      ASSERT_TRUE(knife->isChecked());
      send_mouse_event(*host, QEvent::MouseMove, local_point({0, 1, -2}), Qt::NoButton, Qt::NoButton);
      send_mouse_event(*host, QEvent::MouseButtonPress, local_point({0, 1, -2}), Qt::LeftButton, Qt::LeftButton);
      break;
    }
    case 5:
      send_mouse_event(*host, QEvent::MouseButtonRelease, local_point({0, 1, -2}), Qt::LeftButton, Qt::NoButton);
      break;
    case 6:
      send_mouse_event(*host, QEvent::MouseMove, local_point({1, 1, 0}), Qt::NoButton, Qt::NoButton);
      break;
    case 7: {
      ASSERT_TRUE(angle->isVisible());
      ASSERT_TRUE(length->isVisible());
      EXPECT_TRUE(angle->text().endsWith(QChar(0x00b0)));
      EXPECT_GT(length->text().toDouble(), 0.0);
      EXPECT_EQ(angle->internalWinId(), 0U);
      EXPECT_EQ(length->internalWinId(), 0U);
      const QImage viewport_pixels = host->grabFramebuffer();
      EXPECT_GE(warm_pixels(viewport_pixels, {0, 1, 1}, true), 3);
      EXPECT_GE(warm_pixels(viewport_pixels, {0.5F, 1, -1}, false), 3);
      const QImage pixels = capture("live");
      const double ratio = static_cast<double>(pixels.width()) / window->width();
      const auto glyph_bounds = [&](QLabel &label) {
        const QPoint origin = label.mapTo(window, QPoint{});
        return QRect(qRound(origin.x() * ratio), qRound(origin.y() * ratio),
                     qRound(label.width() * ratio), qRound(label.height() * ratio));
      };
      angle_pixels = glyph_bounds(*angle);
      length_pixels = glyph_bounds(*length);
      EXPECT_GT(glyph_pixels(pixels, angle_pixels), 5);
      EXPECT_GT(glyph_pixels(pixels, length_pixels), 5);
      if (!trigger(quader::quader_command::kViewportToggleDimensions)) return;
      break;
    }
    case 8: {
      EXPECT_TRUE(angle->isHidden());
      EXPECT_TRUE(length->isHidden());
      EXPECT_GE(warm_pixels(host->grabFramebuffer(), {0, 1, 1}, true), 3);
      const QImage pixels = capture("dimensions-off");
      EXPECT_EQ(glyph_pixels(pixels, angle_pixels), 0);
      EXPECT_EQ(glyph_pixels(pixels, length_pixels), 0);
      if (!trigger(quader::quader_command::kViewportToggleDimensions)) return;
      break;
    }
    case 9:
      EXPECT_TRUE(angle->isVisible());
      EXPECT_TRUE(length->isVisible());
      key(Qt::Key_Escape);
      break;
    case 10: {
      EXPECT_TRUE(angle->isHidden());
      EXPECT_TRUE(length->isHidden());
      const QImage viewport_pixels = host->grabFramebuffer();
      EXPECT_EQ(warm_pixels(viewport_pixels, {0, 1, 1}, true), 0);
      EXPECT_EQ(warm_pixels(viewport_pixels, {0.5F, 1, -1}, false), 0);
      capture("cancelled");
      advance();
      observer.stop();
      application.quit();
      return;
    }
    }
    advance();
  });
  QTimer::singleShot(45000, &application, [&] {
    ADD_FAILURE() << "Knife App presentation timed out at phase " << phase;
    application.quit();
  });
  observer.start();
  EXPECT_EQ(app.run(), 0);
  observer.stop();
  EXPECT_EQ(phase, 11);
  if (dimension_review) {
    EXPECT_EQ(dimension_review_index, kDimensionReviewCount + screen_review_count);
    if (screen_review_count > 0U) EXPECT_EQ(window->screen(), original_review_screen);
  }
  app.shutdown();
}

TEST(QtProjectWorkspaceTests, AssetBrowserRealAppFrameLoopAfterSettledToggle) {
  if (qEnvironmentVariableIntValue("QUADER_RUN_VULKAN_ASSET_BROWSER_APP_TESTS") != 1) {
    GTEST_SKIP() << "Set QUADER_RUN_VULKAN_ASSET_BROWSER_APP_TESTS=1 for the real App frame loop";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  const QString empty_scene = qEnvironmentVariable(
      "QUADER_NATIVE_BROWSER_APP_EMPTY", QStringLiteral("0"));
  ASSERT_TRUE(empty_scene == QStringLiteral("0") || empty_scene == QStringLiteral("1"));
  const QString project_source = qEnvironmentVariable("QUADER_NATIVE_BROWSER_APP_PROJECT");
  const QString settings_source = qEnvironmentVariable("QUADER_NATIVE_BROWSER_APP_SETTINGS_SOURCE");
  ASSERT_FALSE(!project_source.isEmpty() && empty_scene == QStringLiteral("1"))
      << "A copied project cannot use the generated empty-scene fixture";
  const QString close_origin = qEnvironmentVariable(
      "QUADER_NATIVE_BROWSER_APP_CLOSE_ORIGIN", QStringLiteral("viewport"));
  ASSERT_TRUE(close_origin == QStringLiteral("viewport") ||
              close_origin == QStringLiteral("browser"))
      << "QUADER_NATIVE_BROWSER_APP_CLOSE_ORIGIN must be viewport or browser";
  const bool composition_smoke = qEnvironmentVariableIntValue(
      "QUADER_NATIVE_BROWSER_APP_COMPOSITION_SMOKE") == 1;
  bool manual_seconds_valid = false;
  const int manual_seconds = qEnvironmentVariable(
      "QUADER_NATIVE_BROWSER_APP_MANUAL_SECONDS", QStringLiteral("0"))
                                 .toInt(&manual_seconds_valid);
  ASSERT_TRUE(manual_seconds_valid &&
              (manual_seconds == 0 || manual_seconds == 60 || manual_seconds == 120));
  bool phase_toggles_valid = false;
  const int phase_toggles = qEnvironmentVariable(
      "QUADER_NATIVE_BROWSER_APP_PHASE_TOGGLES", QStringLiteral("0"))
                               .toInt(&phase_toggles_valid);
  ASSERT_TRUE(phase_toggles_valid && (phase_toggles == 0 || phase_toggles == 24));
  ASSERT_FALSE(manual_seconds > 0 && (phase_toggles > 0 || composition_smoke));
  ASSERT_FALSE(phase_toggles > 0 && composition_smoke);
  ASSERT_FALSE(composition_smoke &&
      (empty_scene == QStringLiteral("1") || !project_source.isEmpty()))
      << "Composition smoke requires the selected box fixture";
  const auto profile = std::filesystem::temp_directory_path() /
      ("quader-browser-app-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  ScopedIsolatedLocalAppData scoped_profile(profile);
  ASSERT_TRUE(scoped_profile.active());
  int argc = 1;
  char application_name[] = "quader-asset-browser-real-app-test";
  char *argv[] = {application_name, nullptr};
  QLocale::setDefault(QLocale::c());
  /** Observes widget key delivery before and after the ordinary Qt dispatch. */
  class BrowserInputApplication final : public QApplication {
  public:
    BrowserInputApplication(int &argc, char **argv) : QApplication(argc, argv) {}
    std::function<void(QObject *, const QKeyEvent &, bool)> key_delivery;
    bool notify(QObject *target, QEvent *event) override {
      const bool relevant = key_delivery && !key_delivery_active_ &&
          (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) &&
          static_cast<QKeyEvent *>(event)->key() == Qt::Key_Space;
      const QScopedValueRollback delivery_guard(key_delivery_active_, key_delivery_active_ || relevant);
      if (relevant) key_delivery(target, *static_cast<QKeyEvent *>(event), true);
      const bool result = QApplication::notify(target, event);
      if (relevant) key_delivery(target, *static_cast<QKeyEvent *>(event), false);
      return result;
    }
  private:
    bool key_delivery_active_ = false;
  } application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  application.setApplicationName(QStringLiteral("Quader"));
  application.setOrganizationName(QStringLiteral("Quader"));
  const auto screens = QGuiApplication::screens();
  for (qsizetype index = 0; index < screens.size(); ++index) {
    const auto *screen = screens[index];
    const QRect geometry = screen->geometry();
    std::cout << "APP_BROWSER_SCREEN index=" << index
              << " name=" << screen->name().toStdString()
              << " geometry=" << geometry.x() << ',' << geometry.y() << ','
              << geometry.width() << 'x' << geometry.height()
              << " dpr=" << screen->devicePixelRatio()
              << " refresh=" << screen->refreshRate() << std::endl;
  }
  QScreen *requested_screen = nullptr;
  if (qEnvironmentVariableIsSet("QUADER_NATIVE_BROWSER_APP_SCREEN")) {
    bool screen_valid = false;
    const int screen_index = qEnvironmentVariable("QUADER_NATIVE_BROWSER_APP_SCREEN")
                                 .toInt(&screen_valid);
    ASSERT_TRUE(screen_valid && screen_index >= 0 && screen_index < screens.size())
        << "QUADER_NATIVE_BROWSER_APP_SCREEN must identify an available screen";
    requested_screen = screens[screen_index];
  }
  QStyle *fusion_style = QStyleFactory::create(QStringLiteral("Fusion"));
  ASSERT_NE(fusion_style, nullptr);
  QApplication::setStyle(fusion_style);
  quader::apply_qt_obsidian_application_theme(fusion_style->standardPalette());
  quader::install_qt_application_default_cursor(application);
  quader::qt_install_default_numeric_spin_boxes(application);
  const QIcon application_icon(QStringLiteral(QUADER_SOURCE_DIR "/resources/icons/quader.ico"));
  ASSERT_FALSE(application_icon.isNull());
  application.setWindowIcon(application_icon);
  const auto executable = std::filesystem::path(
      QCoreApplication::applicationFilePath().toStdWString());
  const auto resources = executable.parent_path();
  const auto root = profile / (project_source.isEmpty()
      ? std::filesystem::path("Observed Project")
      : std::filesystem::path(project_source.toStdWString()).stem());
  ASSERT_TRUE(std::filesystem::create_directory(root));
  // Only ordinary files/directories may enter the isolated writable fixture.
  const auto is_plain_path = [](const std::filesystem::path &path) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error || std::filesystem::is_symlink(status) ||
        (!std::filesystem::is_directory(status) &&
         !std::filesystem::is_regular_file(status))) {
      return false;
    }
#if defined(_WIN32)
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
#else
    return true;
#endif
  };
  std::filesystem::path copied_manifest;
  if (!project_source.isEmpty()) {
    const std::filesystem::path source_manifest(project_source.toStdWString());
    ASSERT_TRUE(source_manifest.is_absolute());
    ASSERT_TRUE(is_plain_path(source_manifest));
    ASSERT_TRUE(std::filesystem::is_regular_file(source_manifest));
    ASSERT_EQ(source_manifest.extension(), std::filesystem::path(".qproj"));
    ASSERT_TRUE(is_plain_path(source_manifest.parent_path()));
    const auto source_root = std::filesystem::canonical(source_manifest.parent_path());
    const auto target_root = std::filesystem::canonical(root);
    // Native equivalence also catches Windows case differences and path aliases.
    for (auto ancestor = target_root;; ancestor = ancestor.parent_path()) {
      ASSERT_FALSE(std::filesystem::equivalent(ancestor, source_root))
          << "The fixture destination must be outside the source project";
      if (ancestor == ancestor.parent_path()) break;
    }
    std::vector<std::filesystem::path> source_entries;
    for (const auto &entry : std::filesystem::recursive_directory_iterator(source_root)) {
      ASSERT_TRUE(is_plain_path(entry.path()))
          << "Project copies reject links/reparse points: " << entry.path().string();
      source_entries.push_back(entry.path());
    }
    std::uintmax_t copied_bytes = 0;
    std::size_t copied_files = 0;
    for (const auto &source : source_entries) {
      ASSERT_TRUE(is_plain_path(source));
      const auto destination = target_root / source.lexically_relative(source_root);
      if (std::filesystem::is_directory(source)) {
        (void)std::filesystem::create_directories(destination);
      } else {
        (void)std::filesystem::create_directories(destination.parent_path());
        ASSERT_TRUE(std::filesystem::copy_file(source, destination));
        copied_bytes += std::filesystem::file_size(destination);
        ++copied_files;
      }
    }
    copied_manifest = target_root / source_manifest.filename();
    ASSERT_TRUE(std::filesystem::is_regular_file(copied_manifest));
    std::cout << "APP_BROWSER_PROJECT_COPY source=" << source_manifest.string()
              << " destination=" << copied_manifest.string()
              << " files=" << copied_files << " bytes=" << copied_bytes << std::endl;
  }
  {
    quader::ProjectSession project;
    project.set_resource_root(resources);
    quader::QuaderEditorApi editor(project);
    if (!copied_manifest.empty()) {
      const auto opened = editor.project().open(copied_manifest);
      ASSERT_TRUE(opened.ok()) << opened.error_message();
      EXPECT_TRUE(std::filesystem::equivalent(editor.project().manifest_path(), copied_manifest));
      const auto active_scene = editor.scene_documents().active_scene();
      ASSERT_TRUE(active_scene.has_value());
      std::cout << "APP_BROWSER_PROJECT_OPEN active_scene="
                << (active_scene->path.has_value()
                    ? active_scene->path->portable_string() : active_scene->name)
                << " open_scenes=" << editor.scene_documents().open_scenes().size()
                << " entities=" << editor.query().entities().size() << std::endl;
    } else {
      ASSERT_TRUE(editor.project().create(root).ok());
      ASSERT_TRUE(editor.viewports().set_layout(quader::ViewportLayoutId::Single).ok());
      ASSERT_TRUE(editor.viewports().set_shading_mode(
          quader::ViewportPaneId::Perspective, quader::QuaderShadingMode::Shaded).ok());
      if (empty_scene == QStringLiteral("0")) {
        const auto box = editor.modeling().create_box(
            {.min = {-2, -1, -3}, .max = {2, 3, 1}}, "Observed box");
        ASSERT_TRUE(box.ok()) << box.error_message();
        ASSERT_TRUE(editor.selection().select_object(box.value()).ok());
      }
      const auto scene_id = editor.scene_documents().active_scene_id();
      ASSERT_TRUE(scene_id.has_value());
      const auto path = quader::ProjectRelativePath::parse_portable("Observed.qdr");
      ASSERT_TRUE(path.ok());
      const auto saved = editor.scene_documents().save_as(*scene_id, path.value());
      ASSERT_TRUE(saved.ok()) << saved.error_message();
    }
  }
  const auto settings_paths = quader::UISettings::resolve_paths(resources);
  if (!settings_source.isEmpty()) {
    const std::filesystem::path source_directory(settings_source.toStdWString());
    ASSERT_TRUE(source_directory.is_absolute());
    ASSERT_TRUE(is_plain_path(source_directory));
    ASSERT_TRUE(std::filesystem::is_directory(source_directory));
    (void)std::filesystem::create_directories(settings_paths.directory);
    constexpr std::array kCopiedSettingsFiles{
        "ui_settings.ini", "viewport_settings.ini", "qt_window_state.bin",
        "qt_auxiliary_windows.ini", "window_settings.ini"};
    for (const auto *name : kCopiedSettingsFiles) {
      const auto source = source_directory / name;
      if (!std::filesystem::exists(source)) continue;
      ASSERT_TRUE(is_plain_path(source));
      ASSERT_TRUE(std::filesystem::is_regular_file(source));
      ASSERT_TRUE(std::filesystem::copy_file(source, settings_paths.directory / name));
      std::cout << "APP_BROWSER_SETTINGS_COPY file=" << name << std::endl;
    }
    std::cout << "APP_BROWSER_SETTINGS_SOURCE source=" << source_directory.string()
              << " destination=" << settings_paths.directory.string() << std::endl;
  }
  auto settings = settings_source.isEmpty()
      ? quader::UISettings::defaults() : quader::UISettings::load(settings_paths.settings_file);
  if (settings_source.isEmpty()) settings.set_interface_scale(1.0f);
  if (qEnvironmentVariableIsSet("QUADER_NATIVE_BROWSER_APP_SCALE")) {
    bool scale_valid = false;
    const float scale = qEnvironmentVariable("QUADER_NATIVE_BROWSER_APP_SCALE").toFloat(&scale_valid);
    ASSERT_TRUE(scale_valid && scale >= quader::UISettings::kMinimumInterfaceScale &&
        scale <= quader::UISettings::kMaximumInterfaceScale);
    settings.set_interface_scale(scale);
  }
  settings.set_panel_visible(quader::UIPanelId::AssetBrowser, false);
  const auto vsync = qEnvironmentVariable("QUADER_NATIVE_BROWSER_VSYNC",
      settings.vsync_enabled() ? QStringLiteral("1") : QStringLiteral("0"));
  ASSERT_TRUE(vsync == QStringLiteral("0") || vsync == QStringLiteral("1"));
  settings.set_vsync_enabled(vsync == QStringLiteral("1"));
  ASSERT_TRUE(settings.save(settings_paths.settings_file));
  std::cout << "APP_BROWSER_SETTINGS scale=" << settings.interface_scale()
            << " vsync=" << settings.vsync_enabled() << std::endl;
  quader::App app;
  ASSERT_TRUE(app.initialize(application, executable));
  QMainWindow *window = nullptr;
  for (auto *widget : QApplication::topLevelWidgets()) {
    if (widget->objectName() == QStringLiteral("quaderMainWindow")) {
      window = qobject_cast<QMainWindow *>(widget);
    }
  }
  ASSERT_NE(window, nullptr);
  auto *recent = window->findChild<QTableView *>(QStringLiteral("projectHome.recentProjects.view"));
  auto *viewport = dynamic_cast<quader::QtViewportPanel *>(
      window->findChild<QWidget *>(QStringLiteral("viewportWorkspace")));
  auto *dock = window->findChild<QDockWidget *>(QStringLiteral("dock_asset_browser"));
  auto *action = window->findChild<QAction *>(QStringLiteral("panel_asset_browser"));
  auto *clip = window->findChild<QWidget *>(QStringLiteral("viewportSurfaceContainer_perspective"));
  ASSERT_NE(recent, nullptr);
  ASSERT_NE(viewport, nullptr);
  ASSERT_NE(dock, nullptr);
  ASSERT_NE(action, nullptr);
  ASSERT_NE(clip, nullptr);
  auto *surface = viewport->surface(quader::ViewportPaneId::Perspective);
  auto *browser = dynamic_cast<quader::QtAssetBrowser *>(dock->widget());
  ASSERT_NE(surface, nullptr);
  ASSERT_NE(browser, nullptr);
  const auto qpc_ticks = []() -> std::int64_t {
#if defined(_WIN32)
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return value.QuadPart;
#else
    return std::chrono::steady_clock::now().time_since_epoch().count();
#endif
  };
#if defined(_WIN32)
  LARGE_INTEGER frequency{};
  ASSERT_NE(QueryPerformanceFrequency(&frequency), FALSE);
  std::cout << "APP_BROWSER_CLOCK frequency=" << frequency.QuadPart << '\n';
  const double qpc_frequency = static_cast<double>(frequency.QuadPart);
#else
  const double qpc_frequency =
      static_cast<double>(std::chrono::steady_clock::period::den) /
      std::chrono::steady_clock::period::num;
#endif
  std::cout << "APP_BROWSER_WINDOW hwnd=" << window->internalWinId()
            << " qpc=" << qpc_ticks() << " empty=" << empty_scene.toStdString()
            << " vsync=" << vsync.toStdString() << std::endl;
  QElapsedTimer clock;
  clock.start();
  const auto input_schedule_started = std::chrono::steady_clock::now();
  struct ToggleObservation {
    std::int64_t queued_qpc = -1;
    std::int64_t received_qpc = -1;
    qint64 started_ns = -1;
    qint64 qt_started_ns = -1;
    qint64 published_ns = -1;
    std::int64_t visibility_qpc = -1;
    std::int64_t submitted_qpc = -1;
    bool showing = false;
    QSize expected;
  };
  const int synthetic_toggles = manual_seconds > 0 ? 0 : phase_toggles > 0 ? phase_toggles : 4;
  std::vector<ToggleObservation> observations(static_cast<std::size_t>(synthetic_toggles));
  int current_toggle = -1;
  std::vector<int> queued_qpa_toggles;
  std::size_t next_qpa_toggle = 0;
  std::atomic<std::int64_t> last_submitted_qpc = 0;
  std::uint64_t frame_attempts = 0U;
  class AppTimerObserver final : public QObject {
  public:
    std::uint64_t *frame_attempts = nullptr;
  protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
      auto *timer = qobject_cast<QTimer *>(watched);
      if (event->type() == QEvent::Timer && timer != nullptr &&
          timer->parent() == nullptr && timer->timerType() == Qt::PreciseTimer) {
        ++*frame_attempts;
      }
      return QObject::eventFilter(watched, event);
    }
  } timer_observer;
  timer_observer.frame_attempts = &frame_attempts;
  application.installEventFilter(&timer_observer);
  QTimer observer(&application);
  observer.setInterval(5);
  QSize last_renderer_size;
  qint64 last_heartbeat_ms = -1000;
  QObject::connect(&observer, &QTimer::timeout, &application, [&] {
    const QSize renderer_size(static_cast<int>(surface->renderer().width()),
                              static_cast<int>(surface->renderer().height()));
    const auto native_extent = surface->render_size_pixels();
    if (renderer_size != last_renderer_size || clock.elapsed() - last_heartbeat_ms >= 1000) {
      std::cout << "APP_BROWSER_OBSERVE qpc=" << qpc_ticks()
                << " elapsed_ms=" << clock.elapsed() << " toggle=" << current_toggle
                << " clip=" << native_extent.x << 'x' << native_extent.y
                << " renderer=" << renderer_size.width() << 'x' << renderer_size.height()
                << " frame_attempts=" << frame_attempts
                << " settings_apply=" << surface->renderer().scene_settings_apply_count()
                << " exposed=" << surface->isExposed() << std::endl;
      last_renderer_size = renderer_size;
      last_heartbeat_ms = clock.elapsed();
    }
    if (current_toggle >= 0) {
      auto &observation = observations[static_cast<std::size_t>(current_toggle)];
      if (manual_seconds > 0 && observation.visibility_qpc >= 0 && observation.expected.isEmpty()) {
        observation.expected = QSize(static_cast<int>(native_extent.x), static_cast<int>(native_extent.y));
      }
      if (observation.published_ns < 0 && renderer_size == observation.expected) {
        observation.published_ns = clock.nsecsElapsed();
        std::cout << "APP_BROWSER_RENDERER_EXTENT qpc=" << qpc_ticks()
                  << " toggle=" << current_toggle << " latency_ms="
                  << (observation.published_ns - observation.started_ns) / 1000000.0
                  << std::endl;
      }
    }
  });
  QTimer::singleShot(500, &application, [&] {
    // Settle the requested placement before workspace startup latches its
    // native maximized/client-geometry requirements and suppresses painting.
    if (requested_screen != nullptr) {
      window->showNormal();
      window->windowHandle()->setScreen(requested_screen);
      window->setGeometry(requested_screen->availableGeometry());
    }
    window->showMaximized();
    QApplication::setActiveWindow(window);
    ASSERT_EQ(recent->model()->rowCount(), 1);
    ASSERT_TRUE(QMetaObject::invokeMethod(recent, "activated", Qt::DirectConnection,
        Q_ARG(QModelIndex, recent->model()->index(0, 0))));
    auto *selected_screen = window->windowHandle()->screen();
    std::cout << "APP_BROWSER_SELECTED_SCREEN name="
              << (selected_screen != nullptr ? selected_screen->name().toStdString() : "none")
              << " index=" << screens.indexOf(selected_screen)
              << " dpr=" << (selected_screen != nullptr ? selected_screen->devicePixelRatio() : 0.0)
              << " refresh=" << (selected_screen != nullptr ? selected_screen->refreshRate() : 0.0)
              << std::endl;
  });
  if (project_source.isEmpty() && empty_scene == QStringLiteral("0")) {
    QTimer::singleShot(2000, &application, [&] {
      auto *tree = window->findChild<QTreeView *>(QStringLiteral("outliner.tree"));
      ASSERT_NE(tree, nullptr);
      ASSERT_EQ(tree->model()->rowCount(), 1);
      tree->selectionModel()->setCurrentIndex(tree->model()->index(0, 0),
          QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
      ASSERT_TRUE(viewport->focus_active_pane());
      QAction *frame_action = nullptr;
      const auto command_id = quader::quader_command::kViewportFrameSelection.string();
      for (auto *candidate : window->findChildren<QAction *>()) {
        if (candidate->property("quaderCommandId").toString() ==
            QString::fromUtf8(command_id.data(), static_cast<qsizetype>(command_id.size()))) {
          frame_action = candidate;
          break;
        }
      }
      ASSERT_NE(frame_action, nullptr);
      auto *menu = qobject_cast<QMenu *>(frame_action->parent());
      ASSERT_NE(menu, nullptr);
      ASSERT_TRUE(QMetaObject::invokeMethod(menu, "aboutToShow", Qt::DirectConnection));
      ASSERT_TRUE(frame_action->isEnabled());
      frame_action->trigger();
      std::cout << "APP_BROWSER_FRAMED qpc=" << qpc_ticks() << std::endl;
    });
  }
  const auto toggle = [&](int index, std::int64_t queued_qpc) {
    const auto received_qpc = qpc_ticks();
    auto &observation = observations[static_cast<std::size_t>(index)];
    observation.queued_qpc = queued_qpc;
    observation.received_qpc = received_qpc;
    std::cout << "APP_BROWSER_INPUT_RECEIVED qpc=" << received_qpc
              << " toggle=" << index << " queued_qpc=" << queued_qpc
              << " queue_ms=" << (received_qpc - queued_qpc) * 1000.0 / qpc_frequency
              << std::endl;
    const bool showing = index % 2 == 0;
    const bool browser_origin = !showing && (phase_toggles > 0 || index >= 3) &&
        close_origin == QStringLiteral("browser");
    observation.showing = showing;
    current_toggle = index;
    observation.started_ns = clock.nsecsElapsed();
    std::cout << "APP_BROWSER_TOGGLE_BEGIN qpc=" << qpc_ticks()
              << " toggle=" << index << " action=" << (showing ? "show" : "hide")
              << " trigger=" << (phase_toggles > 0 ? "qpa_ctrl_space" : index < 2 ? "panel_menu" : "synthetic_ctrl_space")
              << " close_origin=" << close_origin.toStdString() << std::endl;
    if (phase_toggles > 0 || index >= 2) {
      if (browser_origin) {
        browser->focus_browser();
        ASSERT_TRUE(browser->owns_browser_focus());
      } else {
        EXPECT_TRUE(viewport->focus_active_pane());
        ASSERT_FALSE(browser->owns_browser_focus());
      }
    }
    if (phase_toggles == 0 && index < 2) {
      action->trigger();
    } else {
      QObject *target = browser_origin ? QApplication::focusWidget() : clip;
      ASSERT_NE(target, nullptr);
      if (phase_toggles > 0) {
        queued_qpa_toggles.push_back(index);
        QWindowSystemInterface::handleKeyEvent<QWindowSystemInterface::AsynchronousDelivery>(window->windowHandle(),
            QEvent::KeyPress, Qt::Key_Space, Qt::ControlModifier);
        QWindowSystemInterface::handleKeyEvent<QWindowSystemInterface::AsynchronousDelivery>(window->windowHandle(),
            QEvent::KeyRelease, Qt::Key_Space, Qt::ControlModifier);
        return;
      }
      QKeyEvent press(QEvent::KeyPress, Qt::Key_Space, Qt::ControlModifier);
      QCoreApplication::sendEvent(target, &press);
      QKeyEvent release(QEvent::KeyRelease, Qt::Key_Space, Qt::ControlModifier);
      QCoreApplication::sendEvent(target, &release);
    }
    EXPECT_EQ(dock->isVisible(), showing);
    if (showing && index >= 2) EXPECT_TRUE(browser->owns_browser_focus());
    const auto final_extent = surface->render_size_pixels();
    observation.expected = QSize(static_cast<int>(final_extent.x), static_cast<int>(final_extent.y));
    std::cout << "APP_BROWSER_TOGGLE_END qpc=" << qpc_ticks() << " toggle=" << index
              << " dispatch_ms=" << (clock.nsecsElapsed() - observation.started_ns) / 1000000.0
              << " final_clip=" << final_extent.x << 'x' << final_extent.y << std::endl;
  };
  QObject::connect(dock, &QDockWidget::visibilityChanged, &application, [&](bool visible) {
    if (manual_seconds > 0 &&
        (current_toggle < 0 || observations[static_cast<std::size_t>(current_toggle)].submitted_qpc >= 0)) {
      current_toggle = static_cast<int>(observations.size());
      observations.emplace_back();
      observations.back().started_ns = clock.nsecsElapsed();
    }
    if (current_toggle < 0) return;
    auto &observation = observations[static_cast<std::size_t>(current_toggle)];
    observation.visibility_qpc = qpc_ticks();
    if (manual_seconds > 0) observation.showing = visible;
    std::cout << "APP_BROWSER_VISIBILITY qpc=" << observation.visibility_qpc
              << " toggle=" << current_toggle << " visible=" << visible
              << " clip=" << surface->render_size_pixels().x << 'x'
              << surface->render_size_pixels().y << std::endl;
  });
  auto *observed_host = dynamic_cast<quader::QtVulkanCompositionWidget *>(clip);
  ASSERT_NE(observed_host, nullptr);
  QObject::connect(observed_host, &QRhiWidget::frameSubmitted, &application, [&] {
    const auto submitted_qpc = qpc_ticks();
    last_submitted_qpc.store(submitted_qpc, std::memory_order_relaxed);
    if (current_toggle < 0) return;
    auto &observation = observations[static_cast<std::size_t>(current_toggle)];
    if (observation.submitted_qpc >= 0 || observation.visibility_qpc < 0) return;
    const auto extent = surface->render_size_pixels();
    const QSize size(static_cast<int>(extent.x), static_cast<int>(extent.y));
    if (manual_seconds > 0 && observation.expected.isEmpty()) observation.expected = size;
    if (observation.expected != size || surface->renderer().width() != extent.x ||
        surface->renderer().height() != extent.y) return;
    observation.submitted_qpc = submitted_qpc;
    std::cout << "APP_BROWSER_FRAME_SUBMITTED qpc=" << submitted_qpc
              << " toggle=" << current_toggle << " clip=" << extent.x << 'x' << extent.y
              << " latency_ms=" << (clock.nsecsElapsed() - observation.started_ns) / 1000000.0
              << std::endl;
  });
  constexpr std::array kCompositionPaneIds{
      quader::ViewportPaneId::Perspective, quader::ViewportPaneId::Top,
      quader::ViewportPaneId::Front, quader::ViewportPaneId::Side};
  std::array<quader::QtVulkanCompositionWidget *, 4> composed_hosts{};
  std::array<std::uint64_t, 4> composed_submissions{};
  std::array<std::uint64_t, 4> split_submissions{};
  std::uint64_t composed_failures = 0;
  int composition_phases = 0;
  quader::QtVulkanCompositionWidget *composition_anchor = nullptr;
  std::optional<quader::RenderVulkanSharedContext> composition_context;
  QImage before_navigation;
  quader::QtMaterialPreviewSurface *preview_surface = nullptr;
  QWidget *preview_window = nullptr;
  QWidget *settings_dialog = nullptr;
  QCheckBox *vsync_checkbox = nullptr;
  const auto expect_composed_panes = [&] {
    ASSERT_TRUE(composition_context.has_value());
    ASSERT_NE(composition_anchor, nullptr);
    ASSERT_NE(composition_anchor->shared_context(), nullptr);
    EXPECT_EQ(composition_anchor->shared_context()->device, composition_context->device);
    for (std::size_t index = 0; index < composed_hosts.size(); ++index) {
      SCOPED_TRACE(index);
      auto *host = composed_hosts[index];
      ASSERT_NE(host, nullptr);
      ASSERT_TRUE(host->isVisible());
      ASSERT_NE(host->shared_context(), nullptr);
      EXPECT_EQ(host->shared_context()->device, composition_context->device);
      EXPECT_EQ(host->shared_context()->graphics_queue, composition_context->graphics_queue);
      EXPECT_EQ(host->internalWinId(), 0U);
      auto *pane_surface = viewport->surface(kCompositionPaneIds[index]);
      ASSERT_NE(pane_surface, nullptr);
      EXPECT_EQ(pane_surface->handle(), nullptr);
      EXPECT_TRUE(pane_surface->ready_to_render());
      const auto extent = pane_surface->render_size_pixels();
      EXPECT_EQ(pane_surface->renderer().width(), static_cast<std::uint32_t>(extent.x));
      EXPECT_EQ(pane_surface->renderer().height(), static_cast<std::uint32_t>(extent.y));
      EXPECT_GT(composed_submissions[index], 0U);
    }
    EXPECT_EQ(composed_failures, 0U);
  };
  if (composition_smoke) {
    composition_anchor = dynamic_cast<quader::QtVulkanCompositionWidget *>(
        window->findChild<QWidget *>(QStringLiteral("viewportCompositionAnchor")));
    ASSERT_NE(composition_anchor, nullptr);
    ASSERT_NE(composition_anchor->shared_context(), nullptr);
    composition_context = *composition_anchor->shared_context();
    for (std::size_t index = 0; index < composed_hosts.size(); ++index) {
      const auto pane_name = quader::viewport_pane_stable_id(kCompositionPaneIds[index]);
      composed_hosts[index] = dynamic_cast<quader::QtVulkanCompositionWidget *>(
          window->findChild<QWidget *>(QStringLiteral("viewportSurfaceContainer_%1")
              .arg(QString::fromUtf8(pane_name.data(), static_cast<qsizetype>(pane_name.size())))));
      ASSERT_NE(composed_hosts[index], nullptr);
      QObject::connect(composed_hosts[index], &QRhiWidget::frameSubmitted,
          &application, [&, index] { ++composed_submissions[index]; });
      QObject::connect(composed_hosts[index], &QRhiWidget::renderFailed,
          &application, [&] { ++composed_failures; });
    }
    QTimer::singleShot(18500, &application, [&] {
      auto *menu = viewport->findChild<QMenu *>(QStringLiteral("viewportLayoutMenu_perspective"));
      ASSERT_NE(menu, nullptr);
      ASSERT_TRUE(QMetaObject::invokeMethod(menu, "aboutToShow", Qt::DirectConnection));
      const auto layout_name = quader::viewport_layout_stable_id(quader::ViewportLayoutId::FourQuad);
      auto *layout_action = menu->findChild<QAction *>(QStringLiteral("layout_%1")
          .arg(QString::fromUtf8(layout_name.data(), static_cast<qsizetype>(layout_name.size()))));
      ASSERT_NE(layout_action, nullptr);
      ASSERT_TRUE(layout_action->isEnabled());
      layout_action->trigger();
      ++composition_phases;
    });
    QTimer::singleShot(20000, &application, [&] {
      expect_composed_panes();
      split_submissions = composed_submissions;
      int visible_dimensions = 0;
      for (auto *label : viewport->findChildren<QLabel *>()) {
        if (label->objectName().startsWith(QStringLiteral("viewportSelectionDimension")) &&
            label->isVisible()) {
          ++visible_dimensions;
          EXPECT_FALSE(label->text().isEmpty());
          EXPECT_EQ(label->internalWinId(), 0U);
        }
      }
      EXPECT_GT(visible_dimensions, 0);
      before_navigation = composed_hosts[0]->grabFramebuffer();
      ASSERT_FALSE(before_navigation.isNull());
      composed_hosts[0]->setFocus(Qt::OtherFocusReason);
      const QPoint center = composed_hosts[0]->rect().center();
      QWheelEvent wheel(QPointF(center), QPointF(composed_hosts[0]->mapToGlobal(center)),
          QPoint{}, QPoint{0, 240}, Qt::NoButton, Qt::NoModifier,
          Qt::NoScrollPhase, false);
      QCoreApplication::sendEvent(composed_hosts[0], &wheel);
      ++composition_phases;
    });
    QTimer::singleShot(21000, &application, [&] {
      const QImage after_navigation = composed_hosts[0]->grabFramebuffer();
      ASSERT_FALSE(after_navigation.isNull());
      EXPECT_EQ(after_navigation.size(), before_navigation.size());
      EXPECT_NE(after_navigation, before_navigation);
      auto *face_mode = window->findChild<QAction *>(QStringLiteral("selectionModeFace"));
      ASSERT_NE(face_mode, nullptr);
      face_mode->trigger();
      EXPECT_TRUE(face_mode->isChecked());
      click_widget(*composed_hosts[0], composed_hosts[0]->rect().center());
      ++composition_phases;
    });
    QTimer::singleShot(22000, &application, [&] {
      auto *status = window->findChild<QLabel *>(QStringLiteral("statusSelection"));
      ASSERT_NE(status, nullptr);
      EXPECT_EQ(status->text(), QStringLiteral("1 Face Selected"));
      const QImage selected_face = composed_hosts[0]->grabFramebuffer();
      ASSERT_FALSE(selected_face.isNull());
      const QString review_directory = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
      if (!review_directory.isEmpty()) {
        ASSERT_TRUE(QDir().mkpath(review_directory));
        ASSERT_TRUE(selected_face.save(QDir(review_directory).filePath(
            QStringLiteral("composed-app-selected-face.png"))));
      }
      preview_window = window->findChild<QWidget *>(QStringLiteral("standardMaterialEditor.window"));
      ASSERT_NE(preview_window, nullptr);
      auto *new_material = preview_window->findChild<QAction *>(
          QStringLiteral("standardMaterialEditor.file.new"));
      ASSERT_NE(new_material, nullptr);
      new_material->trigger();
      EXPECT_TRUE(preview_window->isVisible());
      for (auto *candidate : QGuiApplication::allWindows()) {
        if (auto *preview = dynamic_cast<quader::QtMaterialPreviewSurface *>(candidate)) {
          ASSERT_EQ(preview_surface, nullptr);
          preview_surface = preview;
        }
      }
      ASSERT_NE(preview_surface, nullptr);
      ++composition_phases;
    });
    QTimer::singleShot(23000, &application, [&] {
      ASSERT_NE(preview_surface, nullptr);
      EXPECT_TRUE(preview_surface->ready_to_render());
      EXPECT_FALSE(preview_surface->needs_rendering());
      auto *rounded_box = preview_window->findChild<QAction *>(
          QStringLiteral("standardMaterialEditor.previewMesh.roundedBox"));
      ASSERT_NE(rounded_box, nullptr);
      rounded_box->trigger();
      EXPECT_EQ(preview_surface->preview_state().mesh_kind(),
          quader::MaterialPreviewMeshKind::RoundedBox);
      ++composition_phases;
    });
    QTimer::singleShot(24000, &application, [&] {
      ASSERT_NE(preview_surface, nullptr);
      EXPECT_FALSE(preview_surface->needs_rendering());
      preview_window->hide();
      EXPECT_FALSE(preview_window->isVisible());
      auto *settings_action = window->findChild<QAction *>(QStringLiteral("action_settings"));
      ASSERT_NE(settings_action, nullptr);
      settings_action->trigger();
      settings_dialog = window->findChild<QWidget *>(QStringLiteral("settingsDialog"));
      ASSERT_NE(settings_dialog, nullptr);
      vsync_checkbox = settings_dialog->findChild<QCheckBox *>(QStringLiteral("settingsVSync"));
      ASSERT_NE(vsync_checkbox, nullptr);
      vsync_checkbox->setChecked(false);
      ++composition_phases;
    });
    QTimer::singleShot(25000, &application, [&] {
      expect_composed_panes();
      EXPECT_FALSE(composition_anchor->presentation_is_synchronized());
      EXPECT_TRUE(composition_anchor->resolved_present_mode() == VK_PRESENT_MODE_MAILBOX_KHR ||
          composition_anchor->resolved_present_mode() == VK_PRESENT_MODE_IMMEDIATE_KHR);
      ASSERT_NE(vsync_checkbox, nullptr);
      vsync_checkbox->setChecked(true);
      ++composition_phases;
    });
    QTimer::singleShot(26000, &application, [&] {
      expect_composed_panes();
      EXPECT_TRUE(composition_anchor->presentation_is_synchronized());
      ASSERT_NE(settings_dialog, nullptr);
      settings_dialog->close();
      for (std::size_t index = 0; index < composed_submissions.size(); ++index) {
        EXPECT_GT(composed_submissions[index], split_submissions[index]);
        std::cout << "APP_COMPOSITION_PANE index=" << index
                  << " submitted=" << composed_submissions[index] << std::endl;
      }
      ++composition_phases;
    });
  }
  QTimer::singleShot(manual_seconds > 0 ? manual_seconds * 1000 :
      composition_smoke || phase_toggles > 0 ? 27000 : 18000,
      &application, &QCoreApplication::quit);
#if defined(_WIN32)
  class AppBrowserNativeInputObserver final : public QAbstractNativeEventFilter {
  public:
    HWND window_handle = nullptr;
    UINT input_message = 0U;
    std::function<void(int, std::int64_t)> toggle;
    std::function<void(const MSG &)> key_received;

    bool nativeEventFilter(const QByteArray &, void *message, qintptr *result) override {
      const auto &native_message = *static_cast<MSG *>(message);
      if (native_message.hwnd == window_handle && key_received &&
          (native_message.message == WM_KEYDOWN || native_message.message == WM_KEYUP) &&
          (native_message.wParam == VK_SPACE || native_message.wParam == VK_CONTROL)) {
        key_received(native_message);
      }
      if (native_message.hwnd != window_handle || native_message.message != input_message) {
        return false;
      }
      toggle(static_cast<int>(native_message.wParam),
             static_cast<std::int64_t>(native_message.lParam));
      if (result != nullptr) *result = 0;
      return true;
    }
  } native_input_observer;
  native_input_observer.window_handle = reinterpret_cast<HWND>(window->internalWinId());
  native_input_observer.input_message = RegisterWindowMessageW(
      L"Quader.AssetBrowserRealAppFrameLoopAfterSettledToggle.Input");
  ASSERT_NE(native_input_observer.input_message, 0U);
  native_input_observer.toggle = toggle;
  if (manual_seconds > 0) {
    native_input_observer.key_received = [&](const MSG &message) {
      std::cout << "APP_BROWSER_NATIVE_KEY qpc=" << qpc_ticks()
                << " message=" << message.message << " key=" << message.wParam
                << " msg_time=" << message.time
                << " queue_age_ms=" << static_cast<DWORD>(GetTickCount() - message.time)
                << " repeat=" << ((message.lParam >> 30) & 1)
                << " visible=" << dock->isVisible() << std::endl;
    };
  }
  application.installNativeEventFilter(&native_input_observer);
#endif
  if (manual_seconds > 0 || phase_toggles > 0) {
    application.key_delivery = [&](QObject *target, const QKeyEvent &key, bool before) {
      if (key.type() != QEvent::KeyPress || key.isAutoRepeat() ||
          !key.modifiers().testFlag(Qt::ControlModifier)) return;
      const auto delivery_qpc = qpc_ticks();
      if (before) {
        if (manual_seconds > 0) {
          current_toggle = static_cast<int>(observations.size());
          observations.emplace_back();
          auto &observation = observations.back();
          observation.received_qpc = delivery_qpc;
          observation.started_ns = clock.nsecsElapsed();
          observation.showing = !dock->isVisible();
          std::cout << "APP_BROWSER_TOGGLE_BEGIN qpc=" << delivery_qpc
                    << " toggle=" << current_toggle
                    << " action=" << (observation.showing ? "show" : "hide")
                    << " trigger=native_qt_ctrl_space" << std::endl;
        } else if (next_qpa_toggle < queued_qpa_toggles.size()) {
          current_toggle = queued_qpa_toggles[next_qpa_toggle++];
        }
        if (current_toggle >= 0) {
          observations[static_cast<std::size_t>(current_toggle)].qt_started_ns = clock.nsecsElapsed();
        }
      }
      std::cout << "APP_BROWSER_QT_KEY qpc=" << delivery_qpc
                << " phase=" << (before ? "before" : "after")
                << " toggle=" << current_toggle << " timestamp=" << key.timestamp()
                << " target=" << target->objectName().toStdString()
                << " visible=" << dock->isVisible();
#if defined(_WIN32)
      if (manual_seconds > 0 && key.timestamp() != 0) {
        std::cout << " queue_age_ms=" << static_cast<DWORD>(
            GetTickCount() - static_cast<DWORD>(key.timestamp()));
      }
#endif
      std::cout << std::endl;
      if (!before && current_toggle >= 0) {
        auto &observation = observations[static_cast<std::size_t>(current_toggle)];
        if (phase_toggles > 0) EXPECT_EQ(dock->isVisible(), observation.showing);
        if (observation.visibility_qpc < 0) return;
        const auto extent = surface->render_size_pixels();
        observation.expected = QSize(static_cast<int>(extent.x), static_cast<int>(extent.y));
        std::cout << "APP_BROWSER_TOGGLE_END qpc=" << qpc_ticks()
                  << " toggle=" << current_toggle << " dispatch_ms="
                  << (clock.nsecsElapsed() - observation.qt_started_ns) / 1000000.0
                  << " final_clip=" << extent.x << 'x' << extent.y << std::endl;
      }
    };
  }
  std::atomic<bool> input_enqueue_failed = false;
  std::jthread input_producer([&](std::stop_token stop) {
    std::condition_variable_any input_condition;
    std::mutex input_mutex;
    std::unique_lock input_lock(input_mutex);
    constexpr std::array kPhaseOffsets{0, 3, 7, 11, 1, 5, 9, 13};
    for (int index = 0; index < synthetic_toggles; ++index) {
      const int delay_ms = phase_toggles > 0
          ? 5000 + index * 800 + kPhaseOffsets[static_cast<std::size_t>(index) % kPhaseOffsets.size()]
          : 5000 + index * 3000;
      input_condition.wait_until(input_lock, stop,
          input_schedule_started + std::chrono::milliseconds(delay_ms),
          [] { return false; });
      if (stop.stop_requested()) return;
      const auto queued_qpc = qpc_ticks();
#if defined(_WIN32)
      if (!PostMessageW(native_input_observer.window_handle,
              native_input_observer.input_message, static_cast<WPARAM>(index),
              static_cast<LPARAM>(queued_qpc))) {
        input_enqueue_failed.store(true, std::memory_order_relaxed);
      }
#else
      if (!QMetaObject::invokeMethod(&application,
              [&, index, queued_qpc] { toggle(index, queued_qpc); }, Qt::QueuedConnection)) {
        input_enqueue_failed.store(true, std::memory_order_relaxed);
      }
#endif
      std::printf("APP_BROWSER_INPUT_QUEUED qpc=%lld toggle=%d last_frame_qpc=%lld\n",
                  static_cast<long long>(queued_qpc), index,
                  static_cast<long long>(last_submitted_qpc.load(std::memory_order_relaxed)));
      std::fflush(stdout);
    }
  });
  observer.start();
  EXPECT_EQ(app.run(), 0);
  input_producer.request_stop();
  input_producer.join();
#if defined(_WIN32)
  application.removeNativeEventFilter(&native_input_observer);
#endif
  EXPECT_FALSE(input_enqueue_failed.load(std::memory_order_relaxed));
  observer.stop();
  application.removeEventFilter(&timer_observer);
  application.key_delivery = {};
  if (manual_seconds == 0) {
    for (const auto &observation : observations) {
      EXPECT_GE(observation.queued_qpc, 0);
      EXPECT_GE(observation.received_qpc, observation.queued_qpc);
      EXPECT_GE(observation.started_ns, 0);
      EXPECT_GE(observation.published_ns, observation.started_ns);
      EXPECT_GE(observation.submitted_qpc, observation.received_qpc);
    }
  }
  EXPECT_GT(frame_attempts, 4U);
  if (composition_smoke) {
    EXPECT_EQ(composition_phases, 8);
    EXPECT_EQ(composed_failures, 0U);
  }
  app.shutdown();
}

TEST(QtProjectWorkspaceTests, AssetBrowserNativeCtrlSpaceHasNoIntermediateDockAnimation) {
  if (qEnvironmentVariableIntValue("QUADER_RUN_VULKAN_ASSET_BROWSER_TESTS") != 1) {
    GTEST_SKIP() << "Set QUADER_RUN_VULKAN_ASSET_BROWSER_TESTS=1 for native dock transitions";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  bool latency_samples_valid = false;
  const int latency_samples = qEnvironmentVariable(
      "QUADER_NATIVE_BROWSER_LATENCY_SAMPLES", QStringLiteral("1"))
      .toInt(&latency_samples_valid);
  ASSERT_TRUE(latency_samples_valid && latency_samples >= 1 && latency_samples <= 20)
      << "QUADER_NATIVE_BROWSER_LATENCY_SAMPLES must be between 1 and 20";
  bool latency_folders_valid = false;
  const int latency_folders = qEnvironmentVariable(
      "QUADER_NATIVE_BROWSER_LATENCY_FOLDERS", QStringLiteral("0"))
      .toInt(&latency_folders_valid);
  ASSERT_TRUE(latency_folders_valid && latency_folders >= 0 && latency_folders <= 2000)
      << "QUADER_NATIVE_BROWSER_LATENCY_FOLDERS must be between 0 and 2000";
  const auto profile = std::filesystem::temp_directory_path() /
      ("quader-asset-browser-native-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  ScopedIsolatedLocalAppData scoped_profile(profile);
  ASSERT_TRUE(scoped_profile.active());
  int argc = 1;
  char application_name[] = "quader-asset-browser-native-test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  QApplication::setStyle(QStringLiteral("Fusion"));
  quader::apply_qt_obsidian_application_theme(QApplication::palette());
  const auto resources = std::filesystem::path(
      QCoreApplication::applicationDirPath().toStdWString());
  auto native_viewport_settings = quader::ViewportSettings::defaults();
  (void)native_viewport_settings.set_color(
      quader::ViewportColorSettingId::Background,
      {.r = 0.0f, .g = 0.0f, .b = 0.0f, .a = 1.0f});
  ASSERT_TRUE(native_viewport_settings.save(
      quader::UISettings::resolve_paths(resources).viewport_settings_file));
  const auto root = scoped_profile.profile() / "Project";
  ASSERT_TRUE(std::filesystem::create_directory(root));
  quader::RenderSystem renderer;
  ASSERT_TRUE(renderer.initialize(nullptr, resources));
  quader::ProjectSession project;
  project.set_resource_root(std::filesystem::path(QUADER_SOURCE_DIR) /
                            "resources" / "modeling_assets");
  quader::QuaderEditorApi editor(project);
  const auto created = editor.project().create(root);
  ASSERT_TRUE(created.ok()) << created.error_message();
  for (int folder = 0; folder < latency_folders; ++folder) {
    ASSERT_TRUE(std::filesystem::create_directory(
        root / ("Latency directory " + std::to_string(folder))));
  }
  ASSERT_NE(project.active_scene(), nullptr);
  auto &scene = *project.active_scene()->session;
  quader::UISystem ui;
  ASSERT_TRUE(ui.initialize(renderer, resources));
  quader::SceneSession *bound_scene = &scene;
  const auto bind_scene = [&](quader::SceneSession &current) {
    bound_scene = &current;
    ui.install_runtime_hooks(editor, current.scene(), current.assets(), current.modeling(),
        current.selection(), current.native_tools(), current.point_placement(),
        quader::kInvalidAssetId);
  };
  quader::QuaderEditorApiSessionAccess::set_scene_binding_changed_callback(editor, bind_scene);
  bind_scene(scene);
  const QString native_vsync = qEnvironmentVariable("QUADER_NATIVE_BROWSER_VSYNC");
  if (qEnvironmentVariableIsSet("QUADER_NATIVE_BROWSER_VSYNC")) {
    ASSERT_TRUE(native_vsync == QStringLiteral("0") ||
                native_vsync == QStringLiteral("1"))
        << "QUADER_NATIVE_BROWSER_VSYNC must be 0 or 1";
    const bool enabled = native_vsync == QStringLiteral("1");
    const auto changed = editor.preferences().set_vsync_enabled(enabled);
    ASSERT_TRUE(changed.ok()) << changed.error_message();
    EXPECT_EQ(editor.preferences().vsync_enabled(), enabled);
    EXPECT_EQ(renderer.vsync_enabled(), enabled);
  }
  const QString interrupted_resize =
      qEnvironmentVariable("QUADER_NATIVE_BROWSER_INTERRUPTED_RESIZE");
  if (qEnvironmentVariableIsSet("QUADER_NATIVE_BROWSER_INTERRUPTED_RESIZE")) {
    ASSERT_TRUE(interrupted_resize == QStringLiteral("0") ||
                interrupted_resize == QStringLiteral("1"))
        << "QUADER_NATIVE_BROWSER_INTERRUPTED_RESIZE must be 0 or 1";
  }
  auto *window = ui.main_window();
  ASSERT_NE(window, nullptr);
  auto *central_stack = window->findChild<QWidget *>(QStringLiteral("mainCentralStack"));
  auto *scene_workspace = window->findChild<QWidget *>(QStringLiteral("sceneWorkspace"));
  auto *viewport_container = window->findChild<QWidget *>(
      QStringLiteral("sceneViewportContainer"));
  ASSERT_NE(central_stack, nullptr);
  ASSERT_NE(scene_workspace, nullptr);
  ASSERT_NE(viewport_container, nullptr);
  const auto expect_alien_workspace = [&] {
    for (auto *widget : {central_stack, scene_workspace, viewport_container}) {
      EXPECT_EQ(widget->internalWinId(), 0U) << widget->objectName().toStdString();
    }
  };
  expect_alien_workspace();
  auto *viewport = dynamic_cast<quader::QtViewportPanel *>(
      window->findChild<QWidget *>(QStringLiteral("viewportWorkspace")));
  ASSERT_NE(viewport, nullptr);
  const auto object = editor.modeling().create_box(
      {.min = {-2, -1, -3}, .max = {2, 3, 1}}, "Native transition dimensions");
  ASSERT_TRUE(object.ok()) << object.error_message();
  ASSERT_TRUE(editor.selection().select_object(object.value()).ok());
  ASSERT_TRUE(editor.viewports().set_layout(quader::ViewportLayoutId::Single).ok());
  const QString native_shading = qEnvironmentVariable(
      "QUADER_NATIVE_BROWSER_SHADING", QStringLiteral("shaded"));
  ASSERT_TRUE(native_shading == QStringLiteral("shaded") ||
              native_shading == QStringLiteral("rendered"))
      << "QUADER_NATIVE_BROWSER_SHADING must be shaded or rendered";
  ASSERT_TRUE(editor.viewports().set_shading_mode(
      quader::ViewportPaneId::Perspective,
      native_shading == QStringLiteral("rendered")
          ? quader::QuaderShadingMode::Rendered : quader::QuaderShadingMode::Shaded).ok());
  constexpr std::array kPaneIds{quader::ViewportPaneId::Perspective,
                                quader::ViewportPaneId::Top,
                                quader::ViewportPaneId::Front,
                                quader::ViewportPaneId::Side};
  for (const auto pane : kPaneIds) {
    ASSERT_TRUE(editor.viewports().frame_selection(pane).ok());
  }
  quader::SceneRenderBridge bridge;
  renderer.initialize_scene_render_bridge(bridge, resources);
  quader::RenderOverlaySnapshotCache overlay_cache;
  ui.show();
  ASSERT_NE(window->screen(), nullptr);
  ASSERT_NE(window->windowHandle(), nullptr);
  const QRect available_geometry = window->screen()->availableGeometry();
  const QSize available_client_size = available_geometry.marginsRemoved(
      window->windowHandle()->frameMargins()).size();
  ASSERT_GT(available_client_size.width(), 0);
  ASSERT_GT(available_client_size.height(), 0);
  window->resize(QSize(1600, 1000).boundedTo(available_client_size));
  if (!available_geometry.contains(window->frameGeometry())) {
    window->move(available_geometry.topLeft());
  }
  window->activateWindow();
  QElapsedTimer startup;
  startup.start();
  while (!window->updatesEnabled() && startup.elapsed() < 3000) {
    application.processEvents(QEventLoop::AllEvents, 10);
    ui.refresh({}, {});
  }
  ASSERT_TRUE(window->updatesEnabled());
  expect_alien_workspace();
  auto *dock = window->findChild<QDockWidget *>(QStringLiteral("dock_asset_browser"));
  auto *outliner = window->findChild<QDockWidget *>(QStringLiteral("dock_outliner"));
  auto *inspector = window->findChild<QDockWidget *>(QStringLiteral("dock_inspector"));
  auto *outliner_tree = window->findChild<QTreeView *>(QStringLiteral("outliner.tree"));
  ASSERT_NE(dock, nullptr);
  ASSERT_NE(outliner, nullptr);
  ASSERT_NE(inspector, nullptr);
  ASSERT_NE(outliner_tree, nullptr);
  auto *browser = dynamic_cast<quader::QtAssetBrowser *>(dock->widget());
  ASSERT_NE(browser, nullptr);
  ASSERT_TRUE(editor.ui().hide_panel("asset_browser").ok());
  std::function<void()> viewport_prepared;
  std::function<void()> viewport_accepted;
  const auto present_viewports = [&] {
    ui.refresh({.delta_seconds = 1.0 / 60.0}, renderer.frame_performance_stats());
    const auto synchronized = quader::QuaderRenderApiRuntimeAccess::sync_scene(
        editor.render(), bridge);
    EXPECT_TRUE(synchronized.ok()) << synchronized.error_message();
    ui.prepare_viewports(bridge, overlay_cache);
    if (viewport_prepared) viewport_prepared();
    return ui.render_viewports();
  };
  const auto settle_viewports = [&](const char *stage, int frame_count,
                                     const auto &verify_frame,
                                     int minimum_elapsed_ms = 0) {
    SCOPED_TRACE(stage);
    quader::AppFrameSchedule schedule;
    QEventLoop loop;
    QTimer frame_timer;
    frame_timer.setTimerType(Qt::PreciseTimer);
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    const int display_interval_ms = std::max(
        1, static_cast<int>(std::lround(1000.0 / renderer.display_refresh_rate_hz())));
    std::size_t submitted = 0U;
    int accepted_frames = 0;
    QElapsedTimer presentation_settle_timer;
    qint64 first_accepted_ms = -1;
    presentation_settle_timer.start();
    QObject::connect(&frame_timer, &QTimer::timeout, &loop, [&] {
      submitted = 0U;
      if (renderer.presentation_is_synchronized() || renderer.ready_for_next_frame()) {
        submitted = present_viewports();
      }
      expect_alien_workspace();
      verify_frame();
      if (submitted == editor.viewports().active_pane_count()) {
        if (first_accepted_ms < 0) first_accepted_ms = presentation_settle_timer.elapsed();
        if (viewport_accepted) viewport_accepted();
        ++accepted_frames;
      }
      if (accepted_frames >= frame_count &&
          presentation_settle_timer.elapsed() - first_accepted_ms >= minimum_elapsed_ms) {
        frame_timer.stop();
        loop.quit();
        return;
      }
      frame_timer.setInterval(schedule.next_interval_ms(
          true, renderer.presentation_is_synchronized(), display_interval_ms,
          submitted != 0U));
    });
    timeout.start(3000);
    frame_timer.start(0);
    loop.exec();
    if (minimum_elapsed_ms == 0) {
      EXPECT_EQ(accepted_frames, frame_count)
          << "Last accepted pane count: " << submitted;
    } else {
      EXPECT_GE(accepted_frames, frame_count);
      EXPECT_GE(presentation_settle_timer.elapsed() - first_accepted_ms, minimum_elapsed_ms);
    }
    if (accepted_frames < frame_count) {
      for (const auto pane : kPaneIds) {
        const auto *surface = viewport->surface(pane);
        std::cout << "Native pane " << static_cast<int>(pane)
                  << " visible=" << surface->isVisible()
                  << " exposed=" << surface->isExposed()
                  << " ready=" << surface->ready_to_render() << '\n';
      }
    }
  };
  const auto no_frame_check = [] {};
  const auto expect_dimensions_hidden = [&] {
    for (const auto pane : kPaneIds) {
      const QString pane_name = QString::fromUtf8(
          quader::viewport_pane_stable_id(pane).data());
      for (const QString &axis : {QStringLiteral("X"), QStringLiteral("Y"),
                                 QStringLiteral("Z")}) {
        auto *label = viewport->findChild<QLabel *>(
            QStringLiteral("viewportSelectionDimension%1_%2").arg(axis, pane_name));
        ASSERT_NE(label, nullptr);
        EXPECT_TRUE(label->isHidden())
            << label->objectName().toStdString()
            << " must retire synchronously before the viewport changes.";
      }
    }
  };
  const auto expect_native_viewport_origins = [&] {
#if defined(_WIN32)
    const WId root_handle = window->internalWinId();
    ASSERT_NE(root_handle, 0U);
    const auto root_native = reinterpret_cast<void *>(
        static_cast<std::uintptr_t>(root_handle));
    for (const auto pane : kPaneIds) {
      const QString pane_name = QString::fromUtf8(
          quader::viewport_pane_stable_id(pane).data());
      SCOPED_TRACE(pane_name.toStdString());
      auto *clip = viewport->findChild<QWidget *>(
          QStringLiteral("viewportSurfaceContainer_%1").arg(pane_name));
      ASSERT_NE(clip, nullptr);
      if (!clip->isVisible()) continue;
      const WId clip_handle = clip->internalWinId();
      ASSERT_NE(clip_handle, 0U);
      const auto clip_native = reinterpret_cast<void *>(
          static_cast<std::uintptr_t>(clip_handle));
      const auto clip_bounds = quader::platform_window_client_bounds_in_window(
          clip_native, root_native);
      ASSERT_GT(clip_bounds.width, 0);
      ASSERT_GT(clip_bounds.height, 0);
      const QPoint clip_offset = clip->mapTo(window, QPoint{});
      const double dpr = clip->devicePixelRatioF();
      EXPECT_NEAR(clip_bounds.x, clip_offset.x() * dpr, 1.0);
      EXPECT_NEAR(clip_bounds.y, clip_offset.y() * dpr, 1.0);
      auto *pane_surface = viewport->surface(pane);
      ASSERT_NE(pane_surface, nullptr);
      const auto backing_bounds = quader::platform_window_client_bounds_in_window(
          reinterpret_cast<void *>(static_cast<std::uintptr_t>(pane_surface->winId())),
          clip_native);
      EXPECT_EQ(backing_bounds.x, 0);
      EXPECT_EQ(backing_bounds.y, 0);
      EXPECT_GE(backing_bounds.width, clip_bounds.width);
      EXPECT_GE(backing_bounds.height, clip_bounds.height);
    }
#endif
  };
  const auto expect_current_dimensions = [&] {
    expect_native_viewport_origins();
    for (const auto pane : kPaneIds) {
      const QString pane_name = QString::fromUtf8(
          quader::viewport_pane_stable_id(pane).data());
      auto *frame = viewport->findChild<QFrame *>(
          QStringLiteral("viewportPane_%1").arg(pane_name));
      auto *content = viewport->findChild<QWidget *>(
          QStringLiteral("viewportSurfaceContainer_%1").arg(pane_name));
      ASSERT_NE(frame, nullptr);
      ASSERT_NE(content, nullptr);
      int visible_dimensions = 0;
      for (const QString &axis : {QStringLiteral("X"), QStringLiteral("Y"),
                                 QStringLiteral("Z")}) {
        auto *label = viewport->findChild<QLabel *>(
            QStringLiteral("viewportSelectionDimension%1_%2").arg(axis, pane_name));
        ASSERT_NE(label, nullptr);
        if (!frame->isVisible()) {
          EXPECT_TRUE(label->isHidden());
          continue;
        }
        if (label->isHidden()) continue;
        ++visible_dimensions;
        EXPECT_TRUE(content->geometry().contains(label->geometry()));
        EXPECT_EQ(label->text(), QStringLiteral("128"));
        EXPECT_FALSE(label->mask().isEmpty());
        EXPECT_FALSE(label->mask() == QRegion(label->rect()));
      }
      if (frame->isVisible() && visible_dimensions < 2) {
        const auto selection = editor.selection().summary();
        const auto &pane_settings = bound_scene->viewport_workspace().effective_settings(pane);
        std::cout << "NATIVE_DIMENSIONS_MISSING pane=" << pane_name.toStdString()
                  << " root_mapped=" << window->testAttribute(Qt::WA_Mapped)
                  << " root_updates=" << window->updatesEnabled()
                  << " pane_updates=" << frame->updatesEnabled()
                  << " pane_mapped=" << frame->testAttribute(Qt::WA_Mapped)
                  << " shading=" << static_cast<int>(editor.viewports().shading_mode(pane))
                  << " selection_mode=" << static_cast<int>(selection.mode)
                  << " selected_objects=" << selection.selected_object_count
                  << " show_dimensions=" << pane_settings.bool_value(
                         quader::ViewportBoolSettingId::ShowDimensions)
                  << " show_overlays=" << pane_settings.bool_value(
                         quader::ViewportBoolSettingId::ShowOverlays);
        for (const QString &axis : {QStringLiteral("X"), QStringLiteral("Y"),
                                   QStringLiteral("Z")}) {
          auto *label = viewport->findChild<QLabel *>(
              QStringLiteral("viewportSelectionDimension%1_%2").arg(axis, pane_name));
          std::cout << ' ' << axis.toStdString() << "_hidden=" << label->isHidden();
        }
        std::cout << '\n';
      }
      if (frame->isVisible()) EXPECT_GE(visible_dimensions, 2);
    }
  };
  const QString review = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
  const auto capture_native_window = [&](const QString &name) {
    if (review.isEmpty()) return;
    ASSERT_TRUE(QDir().mkpath(review));
    ASSERT_NE(window->screen(), nullptr);
    const QPoint origin = window->mapToGlobal(QPoint{});
    const QPixmap capture = window->screen()->grabWindow(
        0, origin.x(), origin.y(), window->width(), window->height());
    ASSERT_FALSE(capture.isNull());
    EXPECT_TRUE(capture.save(QDir(review).filePath(name)));
  };
  settle_viewports("initial selected box", 4, no_frame_check, 250);
  std::cout << "NATIVE_BROWSER_PRESENTATION requested_vsync="
            << editor.preferences().vsync_enabled()
            << " resolved_synchronized=" << renderer.presentation_is_synchronized()
            << " shading=" << native_shading.toStdString()
            << '\n';
  application.processEvents();
  QApplication::setActiveWindow(window);
  expect_current_dimensions();
  const auto send_toggle = [&](QObject &target) {
    QKeyEvent shortcut_override(
        QEvent::ShortcutOverride, Qt::Key_Space, Qt::ControlModifier);
    QCoreApplication::sendEvent(&target, &shortcut_override);
    EXPECT_TRUE(shortcut_override.isAccepted());
    QKeyEvent press(QEvent::KeyPress, Qt::Key_Space, Qt::ControlModifier);
    QCoreApplication::sendEvent(&target, &press);
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_Space, Qt::ControlModifier);
    QCoreApplication::sendEvent(&target, &release);
  };
  enum class InputOrigin { Outliner, NativeViewport, TextEditor, NumericEditor };
  constexpr std::array kInputOrigins{
      InputOrigin::Outliner, InputOrigin::NativeViewport,
      InputOrigin::TextEditor, InputOrigin::NumericEditor};
  const auto focus_input_origin = [&](InputOrigin origin) -> QObject * {
    window->activateWindow();
    QApplication::setActiveWindow(window);
    if (origin == InputOrigin::NativeViewport) {
      EXPECT_TRUE(viewport->focus_active_pane());
      application.processEvents();
      auto *surface = viewport->surface(editor.viewports().focused_pane());
      EXPECT_NE(surface, nullptr);
      if (surface != nullptr) {
        surface->requestActivate();
        application.processEvents();
        EXPECT_EQ(QGuiApplication::focusWindow(), surface);
      }
      return surface;
    }
    QWidget *target = nullptr;
    QDoubleSpinBox *numeric_target = nullptr;
    if (origin == InputOrigin::Outliner) {
      EXPECT_TRUE(editor.ui().focus_panel("outliner").ok());
      target = outliner_tree;
    } else {
      EXPECT_TRUE(editor.ui().focus_panel("inspector").ok());
      if (origin == InputOrigin::TextEditor) {
        target = inspector->findChild<QLineEdit *>(
            QStringLiteral("inspector.general.name"));
      } else {
        numeric_target = inspector->findChild<QDoubleSpinBox *>(
            QStringLiteral("inspector.transform.location.x"));
        EXPECT_NE(numeric_target, nullptr);
        target = numeric_target;
      }
    }
    EXPECT_NE(target, nullptr);
    if (target != nullptr) {
      const Qt::FocusReason focus_reason = numeric_target != nullptr
          ? Qt::TabFocusReason : Qt::OtherFocusReason;
      target->setFocus(focus_reason);
      application.processEvents();
      target->setFocus(focus_reason);
      if (numeric_target != nullptr) {
        auto *numeric_editor = numeric_target->findChild<QLineEdit *>();
        EXPECT_NE(numeric_editor, nullptr);
        QWidget *focus = QApplication::focusWidget();
        const bool numeric_owns_focus = focus == numeric_target ||
            (numeric_editor != nullptr && focus == numeric_editor);
        EXPECT_TRUE(numeric_owns_focus);
        EXPECT_TRUE(quader::qt_install_numeric_spin_box(
            *numeric_target).is_editing());
        return numeric_owns_focus ? focus : nullptr;
      }
      EXPECT_EQ(QApplication::focusWidget(), target);
    }
    return target;
  };
  const auto expect_no_queued_toggle = [&] {
    auto *surface = viewport->surface(editor.viewports().focused_pane());
    ASSERT_NE(surface, nullptr);
    const auto queued = surface->take_input_frame(1.0f / 60.0f, false);
    EXPECT_TRUE(queued.input.pressed_input_chords.empty());
    EXPECT_TRUE(queued.input.released_input_chords.empty());
    EXPECT_TRUE(queued.input.active_input_chords.empty());
  };
  for (int transition = 0;
       transition < static_cast<int>(kInputOrigins.size()); ++transition) {
    SCOPED_TRACE(transition);
    QObject *toggle_target = focus_input_origin(kInputOrigins[transition]);
    ASSERT_NE(toggle_target, nullptr);
    send_toggle(*toggle_target);
    ASSERT_TRUE(editor.ui().panel_visible("asset_browser"));
    EXPECT_TRUE(browser->owns_browser_focus());
    expect_no_queued_toggle();
    expect_dimensions_hidden();
    capture_native_window(
        QStringLiteral("asset-browser-native-before-projection-%1.png").arg(transition));
    const QRect opening = dock->geometry();
    // Native toolbars sit outside the dock area; the browser spans both side
    // docks, rather than only the viewport between them.
    EXPECT_NEAR(opening.left(), outliner->geometry().left(), 2);
    EXPECT_NEAR(opening.right(), inspector->geometry().right(), 2);
    settle_viewports("browser opening", 8, [&] {
      EXPECT_TRUE(editor.ui().panel_visible("asset_browser"));
      EXPECT_TRUE(browser->owns_browser_focus());
      EXPECT_EQ(dock->geometry(), opening);
      for (auto *animation : window->findChildren<QPropertyAnimation *>()) {
        EXPECT_FALSE(animation->state() == QAbstractAnimation::Running &&
                     animation->propertyName() == QByteArrayLiteral("geometry"));
      }
    });
    expect_current_dimensions();
    // A visible browser closes on the first chord from every main-editor
    // input owner, without spending a press on acquiring browser focus.
    toggle_target = focus_input_origin(kInputOrigins[transition]);
    ASSERT_NE(toggle_target, nullptr);
    ASSERT_FALSE(browser->owns_browser_focus());
    send_toggle(*toggle_target);
    ASSERT_FALSE(editor.ui().panel_visible("asset_browser"));
    expect_no_queued_toggle();
    settle_viewports("unfocused browser close", 2, no_frame_check);
    toggle_target = focus_input_origin(kInputOrigins[transition]);
    ASSERT_NE(toggle_target, nullptr);
    send_toggle(*toggle_target);
    ASSERT_TRUE(editor.ui().panel_visible("asset_browser"));
    EXPECT_TRUE(browser->owns_browser_focus());
    settle_viewports("browser reopen", 2, no_frame_check);
    capture_native_window(
        QStringLiteral("asset-browser-native-desktop-open-%1.png").arg(transition));
    for (const auto layout : {quader::ViewportLayoutId::FourQuad,
                              quader::ViewportLayoutId::TwoVertical,
                              quader::ViewportLayoutId::Single}) {
      SCOPED_TRACE(static_cast<int>(layout));
      ASSERT_TRUE(editor.viewports().set_layout(layout).ok());
      viewport->refresh_workspace_layout();
      expect_dimensions_hidden();
      settle_viewports("layout transition", 4, no_frame_check);
      expect_current_dimensions();
      capture_native_window(QStringLiteral("viewport-layout-native-%1-%2.png")
          .arg(transition).arg(static_cast<int>(layout)));
    }
    QWidget *focus = QApplication::focusWidget();
    ASSERT_NE(focus, nullptr);
    EXPECT_TRUE(browser->owns_browser_focus());
    send_toggle(*focus);
    EXPECT_FALSE(editor.ui().panel_visible("asset_browser"));
    expect_dimensions_hidden();
    application.processEvents();
    settle_viewports("browser closing", 4, no_frame_check);
    expect_current_dimensions();
  }
  // Keep the normal frame timer running while the maximized Shaded viewport
  // grows, so the request is tested against an already pending regular frame.
  auto *panel_action = window->findChild<QAction *>(QStringLiteral("panel_asset_browser"));
  auto *window_menu = window->findChild<QMenu *>(QStringLiteral("menu_window"));
  ASSERT_NE(panel_action, nullptr);
  ASSERT_NE(window_menu, nullptr);
  const auto trigger_panel_menu = [&] {
    ASSERT_TRUE(QMetaObject::invokeMethod(window_menu, "aboutToShow", Qt::DirectConnection));
    panel_action->trigger();
  };
  class LatencyPaintObserver final : public QObject {
  public:
    QElapsedTimer *timer = nullptr;
    QObject *root_window = nullptr;
    qint64 first_root_paint_ns = -1;
    qint64 first_browser_paint_ns = -1;

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
      if (timer != nullptr && event->type() == QEvent::Paint) {
        qint64 &first_paint_ns = watched == root_window
            ? first_root_paint_ns : first_browser_paint_ns;
        if (first_paint_ns < 0) first_paint_ns = timer->nsecsElapsed();
      }
      return QObject::eventFilter(watched, event);
    }
  } paint_observer;
  paint_observer.root_window = window;
  window->installEventFilter(&paint_observer);
  for (auto *view : browser->findChildren<QAbstractItemView *>()) {
    view->viewport()->installEventFilter(&paint_observer);
  }
  for (const bool selected : {true, false}) {
    ASSERT_TRUE((selected ? editor.selection().select_object(object.value())
                          : editor.selection().clear()).ok());
    for (const bool immediate : {false, true}) {
      for (int sample = 0; sample < latency_samples; ++sample) {
        SCOPED_TRACE(sample);
        SCOPED_TRACE(selected);
        SCOPED_TRACE(immediate);
        const bool menu_trigger = !immediate;
        SCOPED_TRACE(menu_trigger ? "panel_menu" : "ctrl_space");
        window->showMaximized();
        application.processEvents();
        QApplication::setActiveWindow(window);
        ASSERT_FALSE(window->isMinimized());
        auto *native_surface = viewport->surface(quader::ViewportPaneId::Perspective);
        auto *visible_content = viewport->findChild<QWidget *>(
            QStringLiteral("viewportSurfaceContainer_perspective"));
        ASSERT_NE(native_surface, nullptr);
        ASSERT_NE(visible_content, nullptr);
        viewport_accepted = [&] {
          const auto render_extent = native_surface->render_size_pixels();
          const auto camera_extent = bound_scene->viewport_workspace().camera(
              quader::ViewportPaneId::Perspective).viewport_size();
          EXPECT_NEAR(visible_content->width() * visible_content->devicePixelRatioF(),
                      render_extent.x, 1.0);
          EXPECT_NEAR(visible_content->height() * visible_content->devicePixelRatioF(),
                      render_extent.y, 1.0);
          EXPECT_FLOAT_EQ(camera_extent.x, render_extent.x);
          EXPECT_FLOAT_EQ(camera_extent.y, render_extent.y);
          EXPECT_EQ(native_surface->renderer().width(),
                    static_cast<std::uint32_t>(render_extent.x));
          EXPECT_EQ(native_surface->renderer().height(),
                    static_cast<std::uint32_t>(render_extent.y));
        };
        settle_viewports("maximized viewport before browser command", 4, no_frame_check, 250);
        viewport_accepted = {};
        const WId native_window = native_surface->winId();
        const QSize backing_size = native_surface->size();
        const auto backing_extent = quader::platform_window_client_extent(
            reinterpret_cast<void *>(static_cast<std::uintptr_t>(native_window)));
        const auto expect_stable_backing = [&] {
          expect_native_viewport_origins();
          EXPECT_EQ(native_surface->winId(), native_window);
          EXPECT_EQ(native_surface->size(), backing_size);
          const auto actual_extent = quader::platform_window_client_extent(
              reinterpret_cast<void *>(static_cast<std::uintptr_t>(native_surface->winId())));
          EXPECT_EQ(actual_extent.width, backing_extent.width);
          EXPECT_EQ(actual_extent.height, backing_extent.height);
        };
        ASSERT_FALSE(editor.ui().panel_visible("asset_browser"));
        if (!menu_trigger) ASSERT_TRUE(viewport->focus_active_pane());
        QElapsedTimer show_timer;
        qint64 show_first_accepted_ns = -1;
        viewport_accepted = [&] {
          if (show_first_accepted_ns < 0) show_first_accepted_ns = show_timer.nsecsElapsed();
        };
        const auto show_work_before = project.project_assets().work_stats();
        paint_observer.first_root_paint_ns = -1;
        paint_observer.first_browser_paint_ns = -1;
        paint_observer.timer = &show_timer;
        show_timer.start();
        if (menu_trigger) {
          trigger_panel_menu();
        } else {
          send_toggle(*native_surface);
        }
        const qint64 show_dispatch_ns = show_timer.nsecsElapsed();
        const auto show_work_after = project.project_assets().work_stats();
        ASSERT_TRUE(editor.ui().panel_visible("asset_browser"));
        settle_viewports("maximized browser before close", 4, no_frame_check);
        viewport_accepted = {};
        paint_observer.timer = nullptr;
        EXPECT_GE(show_first_accepted_ns, 0);
        EXPECT_GE(paint_observer.first_browser_paint_ns, 0);
        EXPECT_EQ(show_work_after.scans, show_work_before.scans);
        EXPECT_EQ(show_work_after.inspected_entries, show_work_before.inspected_entries);
        std::cout << "NATIVE_BROWSER_LATENCY action=show sample=" << sample
                  << " selected=" << selected
                  << " trigger=" << (menu_trigger ? "panel_menu" : "ctrl_space")
                  << " dispatch_ms=" << show_dispatch_ns / 1000000.0
                  << " first_browser_paint_begin_ms=" << paint_observer.first_browser_paint_ns / 1000000.0
                  << " first_root_paint_begin_ms=" << paint_observer.first_root_paint_ns / 1000000.0
                  << " first_accepted_ms=" << show_first_accepted_ns / 1000000.0
                  << " retained_items=" << browser->snapshot().items.size()
                  << " dispatch_scans=" << show_work_after.scans - show_work_before.scans
                  << " dispatch_inspected_entries="
                  << show_work_after.inspected_entries - show_work_before.inspected_entries
                  << " requested_vsync=" << editor.preferences().vsync_enabled()
                  << " resolved_synchronized=" << renderer.presentation_is_synchronized()
                  << " dpr=" << window->devicePixelRatioF() << '\n';
        ASSERT_FALSE(window->isMinimized());
        expect_stable_backing();
        const auto old_extent = native_surface->render_size_pixels();
        quader::AppFrameSchedule schedule;
        quader::AppFramePresentationRequest presentation_request;
        QEventLoop loop;
        QTimer frame_timer;
        frame_timer.setTimerType(Qt::PreciseTimer);
        QTimer timeout;
        timeout.setSingleShot(true);
        QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        QElapsedTimer transition_timer;
        bool closing = false;
        int requests = 0;
        int immediate_restarts = 0;
        int accepted_frames = 0;
        qint64 first_prepare_ns = -1;
        qint64 first_accepted_ns = -1;
        qint64 hide_dispatch_ns = -1;
        std::uint64_t hide_scans = 0U;
        std::uint64_t hide_inspected_entries = 0U;
        quader::QVec2 final_extent{};
        viewport_prepared = [&] {
          if (closing && first_prepare_ns < 0) {
            first_prepare_ns = transition_timer.nsecsElapsed();
          }
        };
        ui.set_viewport_presentation_requested_callback([&] {
          ++requests;
          if (!immediate || !presentation_request.request()) return;
          QMetaObject::invokeMethod(&frame_timer, [&] {
            presentation_request.deliver();
            if (presentation_request.take(false)) {
              ++immediate_restarts;
              frame_timer.start(0);
            }
          }, Qt::QueuedConnection);
        });
        const int display_interval_ms = std::max(
            1, static_cast<int>(std::lround(1000.0 / renderer.display_refresh_rate_hz())));
        QObject::connect(&frame_timer, &QTimer::timeout, &loop, [&] {
          if (window->isMinimized()) {
            ADD_FAILURE() << "Native transition test window was minimized";
            frame_timer.stop();
            loop.quit();
            return;
          }
          std::size_t submitted = 0U;
          if (renderer.presentation_is_synchronized() || renderer.ready_for_next_frame()) {
            submitted = present_viewports();
          }
          if (closing && submitted == editor.viewports().active_pane_count()) {
            if (first_accepted_ns < 0) first_accepted_ns = transition_timer.nsecsElapsed();
            expect_stable_backing();
            EXPECT_FLOAT_EQ(native_surface->render_size_pixels().x, final_extent.x);
            EXPECT_FLOAT_EQ(native_surface->render_size_pixels().y, final_extent.y);
            EXPECT_NEAR(visible_content->width() * visible_content->devicePixelRatioF(),
                        final_extent.x, 1.0);
            EXPECT_NEAR(visible_content->height() * visible_content->devicePixelRatioF(),
                        final_extent.y, 1.0);
            const auto camera_extent = bound_scene->viewport_workspace().camera(
                quader::ViewportPaneId::Perspective).viewport_size();
            EXPECT_FLOAT_EQ(camera_extent.x, final_extent.x);
            EXPECT_FLOAT_EQ(camera_extent.y, final_extent.y);
            EXPECT_EQ(native_surface->renderer().width(),
                      static_cast<std::uint32_t>(final_extent.x));
            EXPECT_EQ(native_surface->renderer().height(),
                      static_cast<std::uint32_t>(final_extent.y));
            if (++accepted_frames == 4) {
              frame_timer.stop();
              loop.quit();
              return;
            }
          }
          frame_timer.setInterval(schedule.next_interval_ms(
              true, renderer.presentation_is_synchronized(), display_interval_ms,
              submitted != 0U));
        });
        frame_timer.start(display_interval_ms);
        QTimer::singleShot(0, &loop, [&] {
          const auto hide_work_before = project.project_assets().work_stats();
          paint_observer.first_root_paint_ns = -1;
          paint_observer.first_browser_paint_ns = -1;
          paint_observer.timer = &transition_timer;
          transition_timer.start();
          closing = true;
          if (menu_trigger) {
            trigger_panel_menu();
          } else {
            QWidget *focus = QApplication::focusWidget();
            ASSERT_NE(focus, nullptr);
            EXPECT_TRUE(browser->owns_browser_focus());
            send_toggle(*focus);
          }
          hide_dispatch_ns = transition_timer.nsecsElapsed();
          const auto hide_work_after = project.project_assets().work_stats();
          hide_scans = hide_work_after.scans - hide_work_before.scans;
          hide_inspected_entries = hide_work_after.inspected_entries - hide_work_before.inspected_entries;
          EXPECT_EQ(hide_scans, 0U);
          EXPECT_EQ(hide_inspected_entries, 0U);
          EXPECT_FALSE(editor.ui().panel_visible("asset_browser"));
          final_extent = native_surface->render_size_pixels();
          EXPECT_FLOAT_EQ(final_extent.x, old_extent.x);
          EXPECT_GT(final_extent.y, old_extent.y);
          expect_stable_backing();
        });
        timeout.start(3000);
        loop.exec();
        paint_observer.timer = nullptr;
        viewport_prepared = {};
        frame_timer.stop();
        EXPECT_EQ(accepted_frames, 4);
        EXPECT_GT(requests, 0);
        EXPECT_GE(first_prepare_ns, 0);
        EXPECT_GE(first_accepted_ns, first_prepare_ns);
        if (immediate) EXPECT_GT(immediate_restarts, 0);
        else EXPECT_EQ(immediate_restarts, 0);
        application.processEvents();
        const int settled_requests = requests;
        for (int unchanged = 0; unchanged < 3; ++unchanged) {
          ui.refresh({}, renderer.frame_performance_stats());
          viewport->refresh_workspace_layout();
          application.processEvents();
        }
        EXPECT_EQ(requests, settled_requests);
        ui.set_viewport_presentation_requested_callback({});
        std::cout << "NATIVE_BROWSER_CLOSE selected=" << selected
                  << " immediate=" << immediate
                  << " requests=" << requests
                  << " restarts=" << immediate_restarts
                  << " first_prepare_ms=" << first_prepare_ns / 1000000.0
                  << " first_accepted_ms=" << first_accepted_ns / 1000000.0
                  << " trigger=" << (menu_trigger ? "panel_menu" : "ctrl_space") << '\n';
        std::cout << "NATIVE_BROWSER_LATENCY action=hide sample=" << sample
                  << " selected=" << selected
                  << " trigger=" << (menu_trigger ? "panel_menu" : "ctrl_space")
                  << " dispatch_ms=" << hide_dispatch_ns / 1000000.0
                  << " first_root_paint_begin_ms=" << paint_observer.first_root_paint_ns / 1000000.0
                  << " first_accepted_ms=" << first_accepted_ns / 1000000.0
                  << " retained_items=" << browser->snapshot().items.size()
                  << " dispatch_scans=" << hide_scans
                  << " dispatch_inspected_entries=" << hide_inspected_entries
                  << " requested_vsync=" << editor.preferences().vsync_enabled()
                  << " resolved_synchronized=" << renderer.presentation_is_synchronized()
                  << " dpr=" << window->devicePixelRatioF() << '\n';
      }
    }
  }
  settle_viewports("last maximized browser close presentation", 4, no_frame_check, 250);
  ASSERT_TRUE(editor.selection().select_object(object.value()).ok());
  if (interrupted_resize == QStringLiteral("1")) {
    auto *resizing_surface = viewport->surface(quader::ViewportPaneId::Perspective);
    auto *visible_content = viewport->findChild<QWidget *>(
        QStringLiteral("viewportSurfaceContainer_perspective"));
    ASSERT_NE(resizing_surface, nullptr);
    ASSERT_NE(visible_content, nullptr);
    ASSERT_TRUE(editor.ui().show_panel("asset_browser").ok());
    settle_viewports("interrupted resize browser baseline", 4, no_frame_check);
    expect_current_dimensions();
    ASSERT_TRUE(editor.ui().hide_panel("asset_browser").ok());
    expect_dimensions_hidden();
    settle_viewports("interrupted resize first replacement", 1, no_frame_check);
    const auto first_replacement_extent = resizing_surface->render_size_pixels();
    // Interrupt a just-submitted replacement without waiting for GPU completion.
    window->showMinimized();
    application.processEvents();
    ASSERT_TRUE(window->isMinimized());
    ASSERT_FALSE(ui.can_render_viewports());
    ASSERT_TRUE(editor.ui().show_panel("asset_browser").ok());
    application.processEvents();
    expect_dimensions_hidden();
    EXPECT_LT(resizing_surface->render_size_pixels().y, first_replacement_extent.y);
    EXPECT_EQ(ui.render_surfaces(ui.can_render_viewports()), 0U);
    window->showMaximized();
    application.processEvents();
    ASSERT_FALSE(window->isMinimized());
    ASSERT_TRUE(ui.can_render_viewports());
    viewport_accepted = [&] {
      const auto final_extent = resizing_surface->render_size_pixels();
      const auto native_extent = quader::platform_window_client_extent(
          reinterpret_cast<void *>(static_cast<std::uintptr_t>(resizing_surface->winId())));
      const auto camera_extent = bound_scene->viewport_workspace().camera(
          quader::ViewportPaneId::Perspective).viewport_size();
      EXPECT_FLOAT_EQ(camera_extent.x, final_extent.x);
      EXPECT_FLOAT_EQ(camera_extent.y, final_extent.y);
      EXPECT_GE(static_cast<float>(native_extent.width), final_extent.x);
      EXPECT_GE(static_cast<float>(native_extent.height), final_extent.y);
      EXPECT_NEAR(visible_content->width() * visible_content->devicePixelRatioF(),
                  final_extent.x, 1.0);
      EXPECT_NEAR(visible_content->height() * visible_content->devicePixelRatioF(),
                  final_extent.y, 1.0);
      EXPECT_EQ(resizing_surface->renderer().width(),
                static_cast<std::uint32_t>(final_extent.x));
      EXPECT_EQ(resizing_surface->renderer().height(),
                static_cast<std::uint32_t>(final_extent.y));
      expect_current_dimensions();
    };
    settle_viewports("interrupted resize restored replacement", 4, no_frame_check);
    viewport_accepted = {};
    std::cout << "NATIVE_BROWSER_INTERRUPTED_RESIZE restored=1\n";
  }
  window->showNormal();
  application.processEvents();
  settle_viewports("selected dimensions restored", 4, no_frame_check);
  expect_current_dimensions();
  auto *content = viewport->findChild<QWidget *>(
      QStringLiteral("viewportSurfaceContainer_perspective"));
  auto *dimension = viewport->findChild<QLabel *>(
      QStringLiteral("viewportSelectionDimensionX_perspective"));
  auto *surface = viewport->surface(quader::ViewportPaneId::Perspective);
  ASSERT_NE(content, nullptr);
  ASSERT_NE(dimension, nullptr);
  ASSERT_NE(surface, nullptr);
  const double baseline_font_points = dimension->font().pointSizeF();
  constexpr std::array kScaleFactors{1.0, 2.0, 1.0};
  constexpr std::array kDockWidths{
      std::array{520, 600}, std::array{600, 520}, std::array{240, 320}};
  for (const double scale : kScaleFactors) {
    SCOPED_TRACE(scale);
    ASSERT_TRUE(editor.preferences().set_interface_scale(scale).ok());
    window->resize(static_cast<int>(1600 * scale),
                   static_cast<int>(1000 * scale));
    window->resizeDocks({outliner, inspector},
        {static_cast<int>(240 * scale), static_cast<int>(320 * scale)},
        Qt::Horizontal);
    (void)window->layout()->activate();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    application.processEvents();
    settle_viewports("scaled dock baseline", 4, no_frame_check);
    expect_current_dimensions();
    EXPECT_DOUBLE_EQ(dimension->font().pointSizeF(),
                     baseline_font_points * scale);
    for (int repetition = 0; repetition < 2; ++repetition) {
      SCOPED_TRACE(repetition);
      for (std::size_t resize_index = 0U;
           resize_index < kDockWidths.size(); ++resize_index) {
        SCOPED_TRACE(resize_index);
        const QSize prior_content_size = content->size();
        const QPoint prior_viewport_origin = viewport->mapTo(window, QPoint{});
        const auto &dock_widths = kDockWidths[resize_index];
        window->resizeDocks({outliner, inspector},
            {static_cast<int>(dock_widths[0] * scale),
             static_cast<int>(dock_widths[1] * scale)}, Qt::Horizontal);
        (void)window->layout()->activate();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        expect_dimensions_hidden();
        application.processEvents();
        expect_dimensions_hidden();
        const QPoint viewport_origin = viewport->mapTo(window, QPoint{});
        if (resize_index == 1U) {
          // Exchanging equal dock widths moves native ancestors while keeping
          // pane-local geometry unchanged; the previous glyphs still retire.
          EXPECT_EQ(content->size(), prior_content_size);
          EXPECT_NE(viewport_origin.x(), prior_viewport_origin.x());
        } else {
          EXPECT_NE(content->width(), prior_content_size.width());
        }
        if (resize_index != 2U) {
          EXPECT_LT(content->width(), static_cast<int>(600 * scale));
        }
        settle_viewports("lateral dock geometry transition", 4, no_frame_check);
        expect_current_dimensions();
        const auto render_size = surface->render_size_pixels();
        const auto camera_size = scene.viewport_workspace().camera(
            quader::ViewportPaneId::Perspective).viewport_size();
        EXPECT_FLOAT_EQ(camera_size.x, render_size.x);
        EXPECT_FLOAT_EQ(camera_size.y, render_size.y);
        EXPECT_NEAR(render_size.x,
                    content->width() * surface->devicePixelRatio(), 1.0);
        EXPECT_NEAR(render_size.y,
                    content->height() * surface->devicePixelRatio(), 1.0);
      }
    }
  }
  ui.shutdown();
}

TEST(QtProjectWorkspaceTests, NativeTextureCollectionImportPaintsEveryAssetAndMaterialThumbnail) {
  const QString source_directory = qEnvironmentVariable("QUADER_TEXTURE_IMPORT_REPRO_DIR");
  if (source_directory.isEmpty() ||
      qEnvironmentVariableIntValue("QUADER_RUN_VULKAN_ASSET_BROWSER_TESTS") != 1) {
    GTEST_SKIP() << "Set QUADER_TEXTURE_IMPORT_REPRO_DIR and "
                   "QUADER_RUN_VULKAN_ASSET_BROWSER_TESTS=1 for native import reproduction.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  const auto profile = std::filesystem::temp_directory_path() /
      ("quader-native-texture-import-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  ScopedIsolatedLocalAppData scoped_profile(profile, true);
  ASSERT_TRUE(scoped_profile.active());
  const auto log_path = profile / "native-texture-import.log";
  quader::Logger::initialize({.file_path = log_path.string()});
  quader::install_fatal_error_handler(log_path);
  quader::install_qt_fatal_error_handler();
  std::cout << "NATIVE_TEXTURE_IMPORT_LOG=" << log_path.string() << std::endl;
  int argc = 1;
  char application_name[] = "quader-native-texture-import-test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  QApplication::setStyle(QStringLiteral("Fusion"));
  quader::apply_qt_obsidian_application_theme(QApplication::palette());
  const auto source_root = std::filesystem::path(source_directory.toStdWString());
  std::error_code source_error;
  ASSERT_TRUE(std::filesystem::is_directory(source_root, source_error));
  ASSERT_FALSE(source_error) << source_error.message();
  const auto supported = QImageReader::supportedImageFormats();
  std::vector<std::filesystem::path> sources;
  for (std::filesystem::recursive_directory_iterator source(source_root, source_error), end;
       source != end && !source_error; source.increment(source_error)) {
    if (!source->is_regular_file(source_error)) continue;
    const auto extension = QString::fromStdWString(
        source->path().extension().wstring()).mid(1).toLower();
    if (supported.contains(extension.toLatin1())) sources.push_back(source->path());
  }
  ASSERT_FALSE(source_error) << source_error.message();
  std::ranges::sort(sources);
  ASSERT_FALSE(sources.empty());
  LOG_INFO("Native texture import reproduction: {} read-only source images", sources.size());
  const auto resources = std::filesystem::path(
      QCoreApplication::applicationDirPath().toStdWString());
  const auto root = profile / "Project";
  ASSERT_TRUE(std::filesystem::create_directory(root));
  quader::RenderSystem renderer;
  ASSERT_TRUE(renderer.initialize(nullptr, resources));
  quader::ProjectSession project;
  project.set_resource_root(std::filesystem::path(QUADER_SOURCE_DIR) /
      "resources/modeling_assets");
  quader::QuaderEditorApi editor(project);
  ASSERT_TRUE(editor.project().create(root).ok());
  ASSERT_TRUE(editor.viewports().set_layout(quader::ViewportLayoutId::Single).ok());
  quader::UISystem ui;
  ASSERT_TRUE(ui.initialize(renderer, resources));
  auto &scene = *project.active_scene()->session;
  ui.install_runtime_hooks(editor, scene.scene(), scene.assets(), scene.modeling(),
      scene.selection(), scene.native_tools(), scene.point_placement(), quader::kInvalidAssetId);
  quader::SceneRenderBridge bridge;
  renderer.initialize_scene_render_bridge(bridge, resources);
  quader::RenderOverlaySnapshotCache overlay_cache;
  ui.show();
  auto *window = ui.main_window();
  ASSERT_NE(window->screen(), nullptr);
  window->resize(QSize(1600, 1000).boundedTo(window->screen()->availableGeometry().size()));
  window->activateWindow();
  auto *dock = window->findChild<QDockWidget *>(QStringLiteral("dock_asset_browser"));
  ASSERT_NE(dock, nullptr);
  auto *browser = dynamic_cast<quader::QtAssetBrowser *>(dock->widget());
  ASSERT_NE(browser, nullptr);
  auto *grid = browser->findChild<QListView *>(QStringLiteral("assetBrowser.grid"));
  ASSERT_NE(grid, nullptr);
  class ImportPaintObserver final : public QObject {
  public:
    std::size_t count = 0u;
  protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
      if (event->type() == QEvent::Paint) ++count;
      return QObject::eventFilter(watched, event);
    }
  } paint_observer;
  grid->viewport()->installEventFilter(&paint_observer);
  std::set<QString> material_ids;
  std::set<QString> completed_thumbnails;
  QObject::connect(grid->model(), &QAbstractItemModel::dataChanged, browser,
      [&](const QModelIndex &first, const QModelIndex &last, const QList<int> &roles) {
        if (first != last || !roles.contains(Qt::DecorationRole)) return;
        const auto id = first.data(Qt::UserRole + 1).toString();
        if (material_ids.contains(id)) completed_thumbnails.insert(id);
      });
  const auto shutdown = qScopeGuard([&] { ui.shutdown(); });
  std::size_t accepted_frames = 0u;
  const auto wait_until = [&](const char *stage, const auto &ready) {
    LOG_INFO("Native texture import stage: {}", stage);
    quader::AppFrameSchedule schedule;
    QEventLoop loop;
    QTimer frame_timer;
    frame_timer.setTimerType(Qt::PreciseTimer);
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    const int display_interval_ms = std::max(1,
        static_cast<int>(std::lround(1000.0 / renderer.display_refresh_rate_hz())));
    bool finished = false;
    QObject::connect(&frame_timer, &QTimer::timeout, &loop, [&] {
      ui.refresh({.delta_seconds = 1.0 / 60.0}, renderer.frame_performance_stats());
      std::size_t submitted = 0u;
      if (renderer.presentation_is_synchronized() || renderer.ready_for_next_frame()) {
        const auto synchronized = quader::QuaderRenderApiRuntimeAccess::sync_scene(
            editor.render(), bridge);
        if (!synchronized.ok()) {
          ADD_FAILURE() << synchronized.error_message();
          loop.quit();
          return;
        }
        ui.prepare_viewports(bridge, overlay_cache);
        submitted = ui.render_viewports();
        if (submitted == editor.viewports().active_pane_count()) ++accepted_frames;
      }
      finished = ready();
      if (finished) {
        loop.quit();
        return;
      }
      frame_timer.setInterval(schedule.next_interval_ms(true,
          renderer.presentation_is_synchronized(), display_interval_ms, submitted != 0u));
    });
    deadline.start(20000);
    frame_timer.start(0);
    loop.exec();
    LOG_INFO("Native texture import stage {}: completed={}, viewport frames={}, paints={}, thumbnails={}",
        stage, finished, accepted_frames, paint_observer.count, completed_thumbnails.size());
    return finished;
  };
  ASSERT_TRUE(editor.ui().show_panel("asset_browser").ok());
  browser->restore_presentation_state(quader::QtAssetBrowserViewMode::Grid, 64);
  ASSERT_TRUE(wait_until("initial native presentation", [&] {
    return accepted_frames >= 4u && browser->has_completed_visible_paint();
  }));
  ASSERT_TRUE(editor.project_assets().create_folder("", "Texture Batch").ok());
  LOG_INFO("Importing {} sources as texture assets", sources.size());
  const auto textures = editor.project_assets().import_textures(sources, "Texture Batch");
  ASSERT_TRUE(textures.ok()) << textures.error_message();
  ASSERT_EQ(textures.value().size(), sources.size());
  ui.refresh({}, {});
  browser->set_selected_folder(QStringLiteral("Texture Batch"));
  ASSERT_EQ(grid->model()->rowCount(), static_cast<int>(sources.size()));
  std::size_t texture_paints = paint_observer.count;
  int texture_row = 0;
  grid->scrollTo(grid->model()->index(texture_row, 0), QAbstractItemView::PositionAtCenter);
  grid->viewport()->update();
  ASSERT_TRUE(wait_until("paint imported texture rows", [&] {
    if (paint_observer.count == texture_paints) return false;
    texture_paints = paint_observer.count;
    if (++texture_row == grid->model()->rowCount()) return true;
    grid->scrollTo(grid->model()->index(texture_row, 0), QAbstractItemView::PositionAtCenter);
    grid->viewport()->update();
    return false;
  }));
  ASSERT_TRUE(editor.project_assets().create_material_folder("Materials", "Material Batch").ok());
  LOG_INFO("Importing {} sources as textured materials", sources.size());
  const auto materials = editor.project_assets().import_textures_as_materials(
      sources, "Materials/Material Batch");
  ASSERT_TRUE(materials.ok()) << materials.error_message();
  ASSERT_TRUE(materials.value().failed.empty());
  ASSERT_EQ(materials.value().imported.size(), sources.size());
  for (const auto &imported : materials.value().imported) {
    material_ids.insert(QString::fromStdString(std::string(imported.material.id.str())));
  }
  ui.refresh({}, {});
  browser->set_selected_folder(QStringLiteral("Materials/Material Batch"));
  ASSERT_EQ(grid->model()->rowCount(), static_cast<int>(sources.size()));
  int material_row = 0;
  grid->scrollTo(grid->model()->index(material_row, 0), QAbstractItemView::PositionAtCenter);
  grid->viewport()->update();
  ASSERT_TRUE(wait_until("render every imported material thumbnail", [&] {
    while (material_row < grid->model()->rowCount() && completed_thumbnails.contains(
        grid->model()->index(material_row, 0).data(Qt::UserRole + 1).toString())) {
      ++material_row;
    }
    if (material_row == grid->model()->rowCount()) return true;
    grid->scrollTo(grid->model()->index(material_row, 0), QAbstractItemView::PositionAtCenter);
    grid->viewport()->update();
    return false;
  }));
  EXPECT_EQ(completed_thumbnails.size(), sources.size());
  EXPECT_GT(accepted_frames, 4u);
  EXPECT_GT(paint_observer.count, sources.size());
  EXPECT_FALSE(grid->viewport()->grab().isNull());
  LOG_INFO("Native import reproduction passed: {} textures, {} material thumbnails", sources.size(),
      completed_thumbnails.size());
  quader::Logger::instance()->flush();
}

TEST(QtProjectWorkspaceTests, SavingWithUnregisteredSceneCopiesRetainsDistinctBrowserRows) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  const auto profile = std::filesystem::temp_directory_path() /
      ("quader-asset-copy-save-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  ScopedIsolatedLocalAppData scoped_profile(profile);
  ASSERT_TRUE(scoped_profile.active());
  int argc = 1;
  char application_name[] = "quader-asset-copy-save-test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  ASSERT_NE(QApplication::setStyle(QStringLiteral("Fusion")), nullptr);
  quader::apply_qt_obsidian_application_theme(QApplication::palette());
  const auto resource_root = std::filesystem::path(QUADER_SOURCE_DIR) / "resources";
  const auto root = scoped_profile.profile() / "Copy Project";
  ASSERT_TRUE(std::filesystem::create_directory(root));
  quader::RenderSystem renderer;
  quader::ProjectSession project;
  project.set_resource_root(resource_root / "modeling_assets");
  quader::QuaderEditorApi editor(project);
  quader::UISystem ui;
  ASSERT_TRUE(ui.initialize(renderer, resource_root));
  ui.main_window()->setAttribute(Qt::WA_DontShowOnScreen, true);
  const auto bind = [&](quader::SceneSession &scene) {
    ui.install_runtime_hooks(editor, scene.scene(), scene.assets(),
        scene.modeling(), scene.selection(), scene.native_tools(),
        scene.point_placement(), quader::kInvalidAssetId);
  };
  quader::QuaderEditorApiSessionAccess::set_scene_binding_changed_callback(editor, bind);
  bind(project.fallback_scene());
  ASSERT_TRUE(editor.project().create(root).ok());
  ASSERT_TRUE(editor.project_assets().create_folder("", "Backups").ok());
  for (const std::string name : {"First", "Second"}) {
    const auto created = editor.project_assets().create_scene_asset("", name);
    ASSERT_TRUE(created.ok()) << created.error_message();
    ASSERT_TRUE(std::filesystem::copy_file(
        root / created.value().project_relative_path,
        root / "Backups" / (name + ".qdr")));
    ASSERT_TRUE(editor.project_assets().assets(true).ok());
  }
  const auto catalog = editor.project_assets().assets(true);
  ASSERT_TRUE(catalog.ok()) << catalog.error_message();
  std::size_t unregistered = 0;
  for (const auto &asset : catalog.value().assets) {
    if (asset.kind == quader::QuaderProjectAssetKind::Scene &&
        asset.project_relative_path.starts_with("Backups/")) {
      EXPECT_FALSE(asset.scene_id.valid());
      ++unregistered;
    }
  }
  ASSERT_EQ(unregistered, 2U);
  auto *dock = ui.main_window()->findChild<QDockWidget *>(
      QStringLiteral("dock_asset_browser"));
  ASSERT_NE(dock, nullptr);
  auto *browser = dynamic_cast<quader::QtAssetBrowser *>(dock->widget());
  ASSERT_NE(browser, nullptr);
  ASSERT_TRUE(editor.ui().show_panel("asset_browser").ok());
  ui.refresh({}, {});
  const auto scene = editor.scene_documents().active_scene_id();
  ASSERT_TRUE(scene.has_value());
  const auto path = quader::ProjectRelativePath::parse_portable("Working.qdr");
  ASSERT_TRUE(path.ok());
  const auto saved = editor.scene_documents().save_as(*scene, path.value());
  ASSERT_TRUE(saved.ok()) << saved.error_message();
  ui.refresh({}, {});
  const auto box = editor.modeling().create_box({
      .bounds = {.min = {-1.0f, -1.0f, -1.0f},
                 .max = {1.0f, 1.0f, 1.0f}},
      .name = "Saved Box"});
  ASSERT_TRUE(box.ok()) << box.error_message();
  ASSERT_TRUE(editor.scene_documents().save(*scene).ok());
  ui.refresh({}, {});
  QStringList identities;
  for (const auto &item : browser->snapshot().items) {
    EXPECT_FALSE(identities.contains(item.id)) << item.relative_path.toStdString();
    identities.push_back(item.id);
  }
  browser->set_selected_folder(QStringLiteral("Backups"));
  auto *grid = browser->findChild<QListView *>(QStringLiteral("assetBrowser.grid"));
  ASSERT_NE(grid, nullptr);
  EXPECT_EQ(grid->model()->rowCount(), 2);
  EXPECT_TRUE(std::filesystem::exists(root / "Working.qdr"));
  ui.shutdown();
}

TEST(QtProjectWorkspaceTests, AssetBrowserRetainsRevealsAndDiscoversHiddenDirectoryReplacements) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  const auto profile = std::filesystem::temp_directory_path() /
      ("quader-asset-reveal-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  ScopedIsolatedLocalAppData scoped_profile(profile);
  ASSERT_TRUE(scoped_profile.active());
  int argc = 1;
  char application_name[] = "quader-asset-reveal-test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  const auto resources = std::filesystem::path(QUADER_SOURCE_DIR) / "resources";
  const auto root = scoped_profile.profile() / "Project";
  ASSERT_TRUE(std::filesystem::create_directory(root));
  quader::ProjectSession project;
  project.set_resource_root(resources / "modeling_assets");
  quader::QuaderEditorApi editor(project);
  ASSERT_TRUE(editor.project().create(root).ok());
  const auto baseline = editor.project_assets().create_scene_asset("", "Baseline");
  ASSERT_TRUE(baseline.ok()) << baseline.error_message();
  quader::RenderSystem renderer;
  quader::UISystem ui;
  ASSERT_TRUE(ui.initialize(renderer, resources));
  auto &scene = *project.active_scene()->session;
  ui.install_runtime_hooks(editor, scene.scene(), scene.assets(), scene.modeling(),
      scene.selection(), scene.native_tools(), scene.point_placement(), quader::kInvalidAssetId);
  auto *window = ui.main_window();
  window->setAttribute(Qt::WA_DontShowOnScreen, true);
  window->resize(1600, 1000);
  window->show();
  auto *dock = window->findChild<QDockWidget *>(QStringLiteral("dock_asset_browser"));
  ASSERT_NE(dock, nullptr);
  auto *browser = dynamic_cast<quader::QtAssetBrowser *>(dock->widget());
  ASSERT_NE(browser, nullptr);
  const auto settle_until = [&](const auto &condition) {
    QElapsedTimer timeout;
    timeout.start();
    while (!condition() && timeout.elapsed() < 3000) {
      application.processEvents(QEventLoop::AllEvents, 10);
      ui.refresh({}, {});
    }
    return condition();
  };
  ASSERT_TRUE(editor.ui().show_panel("asset_browser").ok());
  ASSERT_TRUE(settle_until([&] { return browser->has_completed_visible_paint(); }));
  for (int frame = 0; frame < 8; ++frame) {
    application.processEvents();
    ui.refresh({}, {});
  }
  const bool measure = qEnvironmentVariableIntValue("QUADER_MEASURE_ASSET_BROWSER_REVEAL") == 1;
  for (int sample = 0; sample < 3; ++sample) {
    ASSERT_TRUE(editor.ui().hide_panel("asset_browser").ok());
    application.processEvents();
    qint64 forced_nanoseconds = 0;
    if (measure) {
      // Both samples use this build, document, catalog and final dock geometry.
      // The reference retains the former synchronous discovery before reveal.
      QElapsedTimer forced_timer;
      forced_timer.start();
      ASSERT_TRUE(editor.project_assets().assets(true).ok());
      ASSERT_TRUE(editor.ui().show_panel("asset_browser").ok());
      forced_nanoseconds = forced_timer.nsecsElapsed();
      application.processEvents();
      ui.refresh({}, {});
      ASSERT_TRUE(editor.ui().hide_panel("asset_browser").ok());
      application.processEvents();
    }
    const auto before_reveal_work = project.project_assets().work_stats();
    const auto before_reveal_textures = project.material_texture_revision();
    QElapsedTimer retained_timer;
    retained_timer.start();
    ASSERT_TRUE(editor.ui().show_panel("asset_browser").ok());
    const qint64 retained_nanoseconds = retained_timer.nsecsElapsed();
    if (measure) {
      std::cout << "ASSET_BROWSER_REVEAL sample=" << sample
                << " forced_catalog_then_reveal_ms=" << forced_nanoseconds / 1000000.0
                << " retained_reveal_ms=" << retained_nanoseconds / 1000000.0 << std::endl;
    }
    EXPECT_EQ(project.project_assets().work_stats().scans, before_reveal_work.scans);
    EXPECT_FALSE(browser->has_completed_visible_paint());
    ui.refresh({}, {});
    EXPECT_EQ(project.project_assets().work_stats().scans, before_reveal_work.scans);
    ASSERT_TRUE(settle_until([&] {
      return project.project_assets().work_stats().metadata_checks ==
          before_reveal_work.metadata_checks + 1u;
    }));
    EXPECT_EQ(project.project_assets().work_stats().scans, before_reveal_work.scans);
    EXPECT_EQ(project.material_texture_revision(), before_reveal_textures);
    const auto retained_work = project.project_assets().work_stats();
    for (int frame = 0; frame < 8; ++frame) {
      application.processEvents();
      ui.refresh({}, {});
    }
    EXPECT_EQ(project.project_assets().work_stats().scans, retained_work.scans);
    EXPECT_EQ(project.project_assets().work_stats().inspected_entries, retained_work.inspected_entries);
    EXPECT_EQ(project.project_assets().work_stats().metadata_checks, retained_work.metadata_checks);
  }
  ASSERT_TRUE(editor.ui().hide_panel("asset_browser").ok());
  application.processEvents();
  const auto quick_toggle_work = project.project_assets().work_stats();
  ASSERT_TRUE(editor.ui().show_panel("asset_browser").ok());
  ASSERT_FALSE(browser->has_completed_visible_paint());
  ASSERT_TRUE(editor.ui().hide_panel("asset_browser").ok());
  for (int frame = 0; frame < 8; ++frame) {
    application.processEvents();
    ui.refresh({}, {});
  }
  EXPECT_EQ(project.project_assets().work_stats().scans, quick_toggle_work.scans);
  EXPECT_EQ(project.project_assets().work_stats().metadata_checks, quick_toggle_work.metadata_checks);
  ASSERT_TRUE(editor.ui().show_panel("asset_browser").ok());
  ASSERT_TRUE(settle_until([&] {
    return project.project_assets().work_stats().metadata_checks ==
        quick_toggle_work.metadata_checks + 1u;
  }));
  EXPECT_EQ(project.project_assets().work_stats().scans, quick_toggle_work.scans);
  ASSERT_TRUE(editor.project_assets().create_folder("", "Authored").ok());
  const auto authored_work = project.project_assets().work_stats();
  for (int frame = 0; frame < 8; ++frame) {
    application.processEvents();
    ui.refresh({}, {});
  }
  EXPECT_EQ(project.project_assets().work_stats().scans, authored_work.scans);
  const auto reveal_external = [&](const std::string &relative_path, const auto &mutation) {
    EXPECT_TRUE(editor.ui().hide_panel("asset_browser").ok());
    application.processEvents();
    const auto hidden_work = project.project_assets().work_stats();
    mutation();
    for (int frame = 0; frame < 4; ++frame) {
      application.processEvents();
      ui.refresh({}, {});
    }
    EXPECT_EQ(project.project_assets().work_stats().scans, hidden_work.scans);
    EXPECT_TRUE(editor.ui().show_panel("asset_browser").ok());
    EXPECT_FALSE(browser->has_completed_visible_paint());
    ui.refresh({}, {});
    EXPECT_EQ(project.project_assets().work_stats().scans, hidden_work.scans);
    EXPECT_TRUE(settle_until([&] {
      return std::ranges::any_of(browser->snapshot().items, [&](const auto &item) {
        return item.relative_path.toStdString() == relative_path;
      });
    }));
    EXPECT_GT(project.project_assets().work_stats().scans, hidden_work.scans);
  };
  const auto source = root / baseline.value().project_relative_path;
  reveal_external("External/Deep/First.qdr", [&] {
    ASSERT_TRUE(std::filesystem::create_directories(root / "External/Deep"));
    ASSERT_TRUE(std::filesystem::copy_file(source, root / "External/Deep/First.qdr"));
  });
  // Discovery must retain no Windows handles that prevent replacing folders.
  reveal_external("External/Deep/Replacement.qdr", [&] {
    std::filesystem::rename(root / "External", root / "Moved");
    ASSERT_TRUE(std::filesystem::create_directories(root / "External/Deep"));
    ASSERT_TRUE(std::filesystem::copy_file(source, root / "External/Deep/Replacement.qdr"));
  });
  reveal_external("External/Deep/AfterReplacement.qdr", [&] {
    ASSERT_TRUE(std::filesystem::copy_file(source, root / "External/Deep/AfterReplacement.qdr"));
  });
  ui.shutdown();
}

TEST(QtProjectWorkspaceTests, AssetBrowserDeleteResolvesInvalidatedCatalogOnFirstAttempt) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  const auto profile = std::filesystem::temp_directory_path() /
      ("quader-asset-delete-dialog-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  ScopedIsolatedLocalAppData scoped_profile(profile);
  ASSERT_TRUE(scoped_profile.active());
  int argc = 1;
  char application_name[] = "quader-asset-delete-dialog-test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  const auto resources = std::filesystem::path(QUADER_SOURCE_DIR) / "resources";
  const auto root = scoped_profile.profile() / "Project";
  const auto recycle = scoped_profile.profile() / "Recycled";
  ASSERT_TRUE(std::filesystem::create_directory(root));
  ASSERT_TRUE(std::filesystem::create_directory(recycle));
  quader::ProjectSession project;
  project.set_resource_root(resources / "modeling_assets");
  quader::QuaderEditorApi editor(project);
  ASSERT_TRUE(editor.project().create(root).ok());
  ASSERT_TRUE(editor.project_assets().create_folder("", "Disposable").ok());
  std::vector<std::string> paths;
  for (int index = 0; index < 36; ++index) {
    const auto created = editor.project_assets().create_scene_asset(
        "Disposable", "Scene " + std::to_string(index));
    ASSERT_TRUE(created.ok()) << created.error_message();
    paths.push_back(created.value().project_relative_path);
  }
  std::size_t recycled_count = 0u;
  quader::QuaderProjectAssetsApiInternalAccess::set_recycle_port(
      editor.project_assets(), [&](std::span<const std::filesystem::path> sources) {
        quader::ProjectAssetRecycleResult result{.ok = true};
        for (const auto &source : sources) {
          std::filesystem::rename(source, recycle / source.filename());
          result.items.push_back({.original_path = source, .recycled = true});
          ++recycled_count;
        }
        return result;
      });
  quader::RenderSystem renderer;
  quader::UISystem ui;
  ASSERT_TRUE(ui.initialize(renderer, resources));
  auto &scene = *project.active_scene()->session;
  ui.install_runtime_hooks(editor, scene.scene(), scene.assets(), scene.modeling(),
      scene.selection(), scene.native_tools(), scene.point_placement(), quader::kInvalidAssetId);
  auto *window = ui.main_window();
  window->setAttribute(Qt::WA_DontShowOnScreen, true);
  window->resize(1600, 1000);
  window->show();
  auto *dock = window->findChild<QDockWidget *>(QStringLiteral("dock_asset_browser"));
  ASSERT_NE(dock, nullptr);
  auto *browser = dynamic_cast<quader::QtAssetBrowser *>(dock->widget());
  ASSERT_NE(browser, nullptr);
  ASSERT_TRUE(editor.ui().show_panel("asset_browser").ok());
  application.processEvents();
  ui.refresh({}, {});
  browser->set_selected_folder(QStringLiteral("Disposable"));
  auto *grid = browser->findChild<QListView *>(QStringLiteral("assetBrowser.grid"));
  ASSERT_NE(grid, nullptr);
  grid->selectAll();
  browser->focus_browser();
  ASSERT_EQ(browser->selected_items().size(), paths.size());

  const auto attempt_delete = [&](bool confirm, bool expected_confirmation) {
    bool presented = false;
    QTimer response;
    response.setSingleShot(true);
    QObject::connect(&response, &QTimer::timeout, [&] {
      auto *dialog = window->findChild<QMessageBox *>(
          QStringLiteral("asset_browser_delete_confirmation"));
      if (dialog == nullptr || !dialog->isVisible()) {
        ADD_FAILURE() << "Expected the asset deletion confirmation.";
        if (auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
          modal->reject();
        }
        return;
      }
      presented = true;
      auto *details = dialog->findChild<QPlainTextEdit *>(
          QStringLiteral("retainedMessageBox.informativeText"));
      EXPECT_NE(details, nullptr);
      if (details != nullptr) {
        EXPECT_TRUE(details->isVisible());
        EXPECT_GT(details->verticalScrollBar()->maximum(), 0);
        for (const auto &path : paths) {
          EXPECT_TRUE(details->toPlainText().contains(QString::fromStdString(path)));
        }
      }
      const auto role = confirm ? QMessageBox::DestructiveRole : QMessageBox::RejectRole;
      for (auto *button : dialog->buttons()) {
        if (dialog->buttonRole(button) == role) {
          EXPECT_TRUE(button->isVisible());
          EXPECT_TRUE(dialog->rect().contains(QRect(
              button->mapTo(dialog, QPoint{}), button->size())));
          button->click();
          return;
        }
      }
      ADD_FAILURE() << "Deletion confirmation has no expected decision button.";
      dialog->reject();
    });
    response.start(0);
    EXPECT_TRUE(browser->handle_action(quader::QtAssetBrowserAction::Delete));
    response.stop();
    EXPECT_EQ(presented, expected_confirmation);
  };

  const auto displayed_revision = browser->snapshot().revision;
  ASSERT_NE(displayed_revision, 0u);
  ASSERT_TRUE(project.refresh_asset_catalog(false).ok);
  ASSERT_EQ(editor.project_assets().asset_revision().value().revision, 0u);
  attempt_delete(false, true);
  EXPECT_EQ(recycled_count, 0u);
  for (const auto &path : paths) EXPECT_TRUE(std::filesystem::exists(root / path));
  EXPECT_EQ(editor.project_assets().asset_revision().value().revision, displayed_revision);

  const auto stale_snapshot = browser->snapshot();
  const auto renamed = editor.project_assets().rename_asset(paths.front(), "Changed");
  ASSERT_TRUE(renamed.ok()) << renamed.error_message();
  paths.front() = "Disposable/Changed.qdr";
  ASSERT_NE(editor.project_assets().asset_revision().value().revision,
            stale_snapshot.revision);
  (void)browser->set_snapshot(stale_snapshot);
  grid->selectAll();
  ASSERT_TRUE(project.refresh_asset_catalog(false).ok);
  ASSERT_EQ(editor.project_assets().asset_revision().value().revision, 0u);
  attempt_delete(false, false);
  EXPECT_EQ(recycled_count, 0u);
  for (const auto &path : paths) EXPECT_TRUE(std::filesystem::exists(root / path));

  grid->selectAll();
  ASSERT_EQ(browser->selected_items().size(), paths.size());
  ASSERT_TRUE(project.refresh_asset_catalog(false).ok);
  ASSERT_EQ(editor.project_assets().asset_revision().value().revision, 0u);
  attempt_delete(true, true);
  EXPECT_EQ(recycled_count, paths.size());
  for (const auto &path : paths) {
    EXPECT_FALSE(std::filesystem::exists(root / path));
    EXPECT_TRUE(std::filesystem::exists(recycle / std::filesystem::path(path).filename()));
  }
  ui.shutdown();
}

TEST(QtProjectWorkspaceTests, AssetBrowserCreatesDetachedScenesInFullWidthBottomDock) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  const auto profile = std::filesystem::temp_directory_path() /
      ("quader-asset-browser-shell-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  ScopedIsolatedLocalAppData scoped_profile(profile);
  ASSERT_TRUE(scoped_profile.active());
  int argc = 1;
  char application_name[] = "quader-asset-browser-shell-test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  ASSERT_NE(QApplication::setStyle(QStringLiteral("Fusion")), nullptr);
  quader::apply_qt_obsidian_application_theme(QApplication::palette());
  const auto resource_root = std::filesystem::path(QUADER_SOURCE_DIR) / "resources";
  const auto root = scoped_profile.profile() / "Asset Project";
  ASSERT_TRUE(std::filesystem::create_directory(root));
  quader::RenderSystem renderer;
  quader::ProjectSession project;
  project.set_resource_root(resource_root / "modeling_assets");
  quader::QuaderEditorApi editor(project);
  quader::UISystem ui;
  ASSERT_TRUE(ui.initialize(renderer, resource_root));
  ui.main_window()->setAttribute(Qt::WA_DontShowOnScreen, true);
  const auto bind = [&](quader::SceneSession &scene) {
    ui.install_runtime_hooks(editor, scene.scene(), scene.assets(),
        scene.modeling(), scene.selection(), scene.native_tools(),
        scene.point_placement(), quader::kInvalidAssetId);
  };
  quader::QuaderEditorApiSessionAccess::set_scene_binding_changed_callback(editor, bind);
  bind(project.fallback_scene());
  ASSERT_TRUE(editor.project().create(root).ok());
  ASSERT_TRUE(editor.project_assets().create_folder("", "Levels").ok());
  const auto active = editor.scene_documents().active_scene_id();
  const auto open_count = editor.scene_documents().open_scenes().size();
  auto *dock = ui.main_window()->findChild<QDockWidget *>(QStringLiteral("dock_asset_browser"));
  auto *console = ui.main_window()->findChild<QDockWidget *>(QStringLiteral("dock_console"));
  ASSERT_NE(dock, nullptr);
  ASSERT_NE(console, nullptr);
  EXPECT_EQ(ui.main_window()->dockWidgetArea(dock), Qt::BottomDockWidgetArea);
  EXPECT_EQ(ui.main_window()->corner(Qt::BottomLeftCorner), Qt::BottomDockWidgetArea);
  EXPECT_EQ(ui.main_window()->corner(Qt::BottomRightCorner), Qt::BottomDockWidgetArea);
  // Qt's tabifiedDockWidgets queries the presented tab bar; hidden docks do
  // not have tabs until the window has laid out both visible panels.
  ui.main_window()->setAnimated(false);
  ui.main_window()->show();
  console->show();
  dock->show();
  QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
  application.processEvents();
  EXPECT_TRUE(ui.main_window()->tabifiedDockWidgets(dock).contains(console));
  console->hide();
  auto *browser = dynamic_cast<quader::QtAssetBrowser *>(dock->widget());
  ASSERT_NE(browser, nullptr);
  auto *window = ui.main_window();
  auto *stack = qobject_cast<QStackedWidget *>(window->centralWidget());
  auto *workspace = window->findChild<QWidget *>(QStringLiteral("sceneWorkspace"));
  ASSERT_NE(stack, nullptr);
  ASSERT_NE(workspace, nullptr);
  QWidget *previous_page = stack->currentWidget();
  stack->setCurrentWidget(workspace);
  window->resize(1600, 1000);
  ASSERT_TRUE(editor.ui().show_panel("outliner").ok());
  ASSERT_TRUE(editor.ui().show_panel("inspector").ok());
  ASSERT_TRUE(editor.ui().show_panel("face_texture").ok());
  (void)window->layout()->activate();
  application.processEvents();
  window->setAnimated(true);
  const auto usage_before_browser = project.material_usage().work_stats();
  for (int transition = 0; transition < 3; ++transition) {
    ASSERT_TRUE(editor.ui().hide_panel("asset_browser").ok());
    application.processEvents();
    ASSERT_TRUE(window->isAnimated());
    ASSERT_TRUE(editor.ui().show_panel("asset_browser").ok());
    // Native viewport children must see the final dock layout immediately;
    // draining queued animation frames must not move the browser afterwards.
    const QRect opening_geometry = dock->geometry();
    EXPECT_LE(opening_geometry.left(), window->contentsRect().left() + 2);
    EXPECT_GE(opening_geometry.right(), window->contentsRect().right() - 2);
    EXPECT_TRUE(window->isAnimated());
    for (auto *animation : window->findChildren<QPropertyAnimation *>()) {
      EXPECT_FALSE(animation->state() == QAbstractAnimation::Running &&
                   animation->propertyName() == QByteArrayLiteral("geometry"));
    }
    application.processEvents();
    EXPECT_EQ(dock->geometry(), opening_geometry);
  }
  EXPECT_EQ(project.material_usage().work_stats(), usage_before_browser);
  const QByteArray bottom_layout = window->saveState();
  ASSERT_TRUE(editor.ui().hide_panel("asset_browser").ok());
  window->addDockWidget(Qt::RightDockWidgetArea, dock);
  ASSERT_TRUE(editor.ui().focus_panel("asset_browser").ok());
  EXPECT_EQ(window->dockWidgetArea(dock), Qt::RightDockWidgetArea);
  ASSERT_TRUE(window->restoreState(bottom_layout));
  stack->setCurrentWidget(previous_page);
  ASSERT_TRUE(editor.ui().show_panel("asset_browser").ok());
  ui.refresh({}, {});
  auto *asset_grid = browser->findChild<QListView *>(QStringLiteral("assetBrowser.grid"));
  ASSERT_NE(asset_grid, nullptr);
  int retained_model_resets = 0;
  QObject::connect(asset_grid->model(), &QAbstractItemModel::modelReset,
                   browser, [&]() { ++retained_model_resets; });
  const auto idle_usage_work = project.material_usage().work_stats();
  for (int frame = 0; frame < 12; ++frame) ui.refresh({}, {});
  EXPECT_EQ(retained_model_resets, 0);
  EXPECT_EQ(project.material_usage().work_stats(), idle_usage_work);
  browser->set_selected_folder(QStringLiteral("Levels"));
  const auto material_projection_before_scene = ui.material_presentation_snapshot_build_count();
  ASSERT_TRUE(editor.project_assets().create_folder("", "Scene Folder").ok());
  ui.refresh({}, {});
  EXPECT_EQ(ui.material_presentation_snapshot_build_count(), material_projection_before_scene);
  ASSERT_TRUE(browser->handle_action(quader::QtAssetBrowserAction::CreateScene));
  EXPECT_EQ(ui.material_presentation_snapshot_build_count(), material_projection_before_scene);
  EXPECT_EQ(editor.scene_documents().open_scenes().size(), open_count);
  EXPECT_EQ(editor.scene_documents().active_scene_id(), active);
  const auto created = editor.project_assets().assets();
  ASSERT_TRUE(created.ok()) << created.error_message();
  const auto scene = std::ranges::find_if(created.value().assets, [](const auto &asset) {
    return asset.kind == quader::QuaderProjectAssetKind::Scene &&
           asset.project_relative_path.starts_with("Levels/");
  });
  ASSERT_NE(scene, created.value().assets.end());
  EXPECT_TRUE(std::filesystem::exists(root / scene->project_relative_path));
  auto asset_revision = editor.project_assets().asset_revision();
  ASSERT_TRUE(asset_revision.ok());
  ASSERT_TRUE(editor.project_assets().move_assets(
      std::array{scene->project_relative_path}, "",
      asset_revision.value().project_id, asset_revision.value().revision).ok());
  ui.refresh({}, {});
  EXPECT_EQ(ui.material_presentation_snapshot_build_count(), material_projection_before_scene);
  asset_revision = editor.project_assets().asset_revision();
  ASSERT_TRUE(asset_revision.ok());
  ASSERT_TRUE(editor.project_assets().move_assets(
      std::array{std::filesystem::path(scene->project_relative_path).filename().generic_string()},
      "Levels", asset_revision.value().project_id, asset_revision.value().revision).ok());
  ui.refresh({}, {});
  EXPECT_EQ(ui.material_presentation_snapshot_build_count(), material_projection_before_scene);
  ASSERT_TRUE(browser->reveal_item(QString::fromStdString(std::string(scene->scene_id.str()))));
  ASSERT_TRUE(browser->handle_action(quader::QtAssetBrowserAction::Open));
  EXPECT_EQ(editor.scene_documents().open_scenes().size(), open_count + 1u);
  const auto opened = editor.scene_documents().active_scene();
  ASSERT_TRUE(opened.has_value());
  EXPECT_EQ(opened->scene_id, scene->scene_id);
  ASSERT_TRUE(browser->handle_action(quader::QtAssetBrowserAction::Open));
  EXPECT_EQ(editor.scene_documents().open_scenes().size(), open_count + 1u);
  EXPECT_EQ(editor.scene_documents().active_scene_id(), opened->session_id);
  const auto texture_path = scoped_profile.profile() / "picker-texture.png";
  QImage source_image(8, 8, QImage::Format_RGBA8888);
  source_image.fill(QColor(245, 30, 40));
  ASSERT_TRUE(source_image.save(QString::fromStdWString(texture_path.wstring())));
  const auto textures = editor.project_assets().import_textures(
      std::array{texture_path}, "");
  ASSERT_TRUE(textures.ok()) << textures.error_message();
  ASSERT_EQ(textures.value().size(), 1u);
  const auto texture_id = textures.value().front().asset_id;
  const auto material = editor.project_assets().create_material_asset("", "Picker Material");
  ASSERT_TRUE(material.ok()) << material.error_message();
  ASSERT_TRUE(editor.ui().show_panel("asset_browser").ok());
  ASSERT_TRUE(browser->reveal_item(QString::fromStdString(
      std::string(material.value().asset_id.str()))));
  const QString review_directory = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
  if (!review_directory.isEmpty()) {
    ASSERT_TRUE(QDir().mkpath(review_directory));
    auto *window = ui.main_window();
    auto *stack = qobject_cast<QStackedWidget *>(window->centralWidget());
    auto *workspace = window->findChild<QWidget *>(QStringLiteral("sceneWorkspace"));
    ASSERT_NE(stack, nullptr);
    ASSERT_NE(workspace, nullptr);
    QWidget *previous_page = stack->currentWidget();
    // This fixture owns the real Qt shell but no GPU. Present its existing
    // workspace page explicitly; renderer pixels have a separate GPU review.
    stack->setCurrentWidget(workspace);
    window->menuBar()->show();
    window->statusBar()->show();
    ASSERT_TRUE(editor.ui().show_panel("outliner").ok());
    ASSERT_TRUE(editor.ui().show_panel("inspector").ok());
    ASSERT_TRUE(editor.ui().show_panel("face_texture").ok());
    window->show();
    dock->show();
    dock->raise();
    const std::array factors{1.0, 2.0, 1.0};
    for (std::size_t stage = 0; stage < factors.size(); ++stage) {
      ASSERT_TRUE(editor.preferences().set_interface_scale(factors[stage]).ok());
      window->resize(static_cast<int>(1600 * factors[stage]),
                     static_cast<int>(1000 * factors[stage]));
      window->resizeDocks({dock}, {static_cast<int>(300 * factors[stage])}, Qt::Vertical);
      for (int pass = 0; pass < 3; ++pass) {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        application.processEvents();
      }
      EXPECT_LE(dock->geometry().left(), window->contentsRect().left() + 2);
      EXPECT_GE(dock->geometry().right(), window->contentsRect().right() - 2);
      EXPECT_TRUE(window->grab().save(QDir(review_directory).filePath(
          QStringLiteral("asset-browser-shell-%1-%2.png")
              .arg(static_cast<int>(factors[stage] * 100)).arg(stage))));
    }
    stack->setCurrentWidget(previous_page);
  }
  ASSERT_TRUE(browser->handle_action(quader::QtAssetBrowserAction::Open));
  auto *browse = ui.main_window()->findChild<QToolButton *>(
      QStringLiteral("standardMaterialEditor.albedoTexture.browse"));
  ASSERT_NE(browse, nullptr);
  bool picker_visited = false;
  int image_pixels = 0;
  QTimer::singleShot(0, &application, [&]() {
    auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
    if (dialog == nullptr) return;
    auto *picker = dynamic_cast<quader::QtAssetBrowser *>(
        dialog->findChild<QWidget *>(QStringLiteral("assetBrowser")));
    if (picker == nullptr) { dialog->reject(); return; }
    picker_visited = true;
    const bool selected = picker->reveal_item(
        QString::fromStdString(std::string(texture_id.str())));
    EXPECT_TRUE(selected);
    application.processEvents();
    const QImage painted = picker->grab().toImage().convertToFormat(QImage::Format_RGB32);
    if (!review_directory.isEmpty()) {
      EXPECT_TRUE(dialog->grab().save(QDir(review_directory).filePath(
          QStringLiteral("asset-browser-texture-picker.png"))));
    }
    for (int y = 0; y < painted.height(); ++y) {
      for (int x = 0; x < painted.width(); ++x) {
        const QColor pixel = painted.pixelColor(x, y);
        if (pixel.red() > 210 && pixel.green() < 70 && pixel.blue() < 80) ++image_pixels;
      }
    }
    dialog->accept();
  });
  browse->click();
  EXPECT_TRUE(picker_visited);
  EXPECT_GT(image_pixels, 100) << "The picker must show decoded image thumbnails.";
  const auto material_document = editor.standard_material_documents().current();
  ASSERT_TRUE(material_document.ok()) << material_document.error_message();
  ASSERT_TRUE(material_document.value().material.base_color_texture.has_value());
  EXPECT_EQ(material_document.value().material.base_color_texture->id, texture_id);
  ASSERT_TRUE(editor.ui().hide_panel("asset_browser").ok());
  ui.refresh({}, {});
  EXPECT_FALSE(ui.needs_continuous_rendering());
  const auto save_material = [&](const QString &action_name, const QString &relative_path) {
    auto *action = ui.main_window()->findChild<QAction *>(action_name);
    ASSERT_NE(action, nullptr);
    bool visited = false;
    QTimer::singleShot(0, &application, [&]() {
      auto *dialog = dynamic_cast<quader::QtProjectAssetDialog *>(
          QApplication::activeModalWidget());
      if (dialog == nullptr) {
        if (auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
          modal->reject();
        }
        return;
      }
      visited = true;
      EXPECT_TRUE(dialog->browser().reveal_item(QString::fromStdString(
          std::string(material.value().asset_id.str()))));
      EXPECT_FALSE(dialog->browser().grab().isNull());
      // This fixture has no GPU, so a visible sphere request remains queued.
      // It must keep the pump awake even while the management dock is hidden.
      EXPECT_TRUE(ui.needs_continuous_rendering());
      ui.refresh({}, {});
      EXPECT_TRUE(ui.needs_continuous_rendering());
      dialog->set_initial_path(relative_path);
      if (!review_directory.isEmpty()) {
        EXPECT_TRUE(dialog->grab().save(QDir(review_directory).filePath(
            QStringLiteral("asset-browser-material-save.png"))));
      }
      dialog->accept();
    });
    action->trigger();
    EXPECT_TRUE(visited) << "Material saves must use the rooted project asset dialog.";
    EXPECT_FALSE(ui.needs_continuous_rendering());
    const auto saved = editor.standard_material_documents().current();
    ASSERT_TRUE(saved.ok());
    ASSERT_TRUE(saved.value().path.has_value());
    EXPECT_EQ(saved.value().path->portable_string(), relative_path.toStdString());
    EXPECT_TRUE(std::filesystem::exists(root / saved.value().path->native_path()));
  };
  save_material(QStringLiteral("standardMaterialEditor.file.saveAs"),
                QStringLiteral("Levels/Material Copy.qmat"));
  EXPECT_TRUE(std::filesystem::exists(root / material.value().project_relative_path));
  auto *new_material = ui.main_window()->findChild<QAction *>(
      QStringLiteral("standardMaterialEditor.file.new"));
  ASSERT_NE(new_material, nullptr);
  new_material->trigger();
  const auto unsaved_material = editor.standard_material_documents().current();
  ASSERT_TRUE(unsaved_material.ok());
  ASSERT_FALSE(unsaved_material.value().path.has_value());
  save_material(QStringLiteral("standardMaterialEditor.file.save"),
                QStringLiteral("Levels/New Material.qmat"));
  ui.shutdown();
}

TEST(QtProjectWorkspaceTests,
     ComposedPresentationRequestsOnlyContinuousSynchronizedFrames) {
  class PresentationRenderSystem final : public quader::RenderSystem {
  public:
    [[nodiscard]] bool initialized() const override { return true; }
    [[nodiscard]] bool presentation_is_synchronized() const override {
      return synchronized;
    }
    void complete_composited_presentation_frame() override { ++completed_frames; }

    bool synchronized = true;
    std::size_t completed_frames = 0u;
  };

  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  const auto profile = std::filesystem::temp_directory_path() /
      ("quader-composed-presentation-request-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  ScopedIsolatedLocalAppData scoped_profile(profile);
  ASSERT_TRUE(scoped_profile.active());
  int argc = 1;
  char application_name[] = "quader-composed-presentation-request-test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  PresentationRenderSystem renderer;
  quader::UISystem ui;
  const auto resources = std::filesystem::path(QUADER_SOURCE_DIR) / "resources";
  ASSERT_TRUE(ui.initialize(renderer, resources, true));
  ASSERT_NE(ui.main_window(), nullptr);
  ASSERT_FALSE(ui.main_window()->isVisible());
  QPointer<quader::QtVulkanCompositionWidget> anchor =
      dynamic_cast<quader::QtVulkanCompositionWidget *>(
          ui.main_window()->findChild<QRhiWidget *>(
              QStringLiteral("viewportCompositionAnchor")));
  ASSERT_NE(anchor, nullptr);
  EXPECT_EQ(anchor->shared_context(), nullptr);

  std::vector<std::size_t> requested_after_frames;
  const auto request_frame = [&] {
    requested_after_frames.push_back(renderer.completed_frames);
  };
  ui.set_viewport_presentation_requested_callback(request_frame);
  const auto submit_root = [&] {
    return QMetaObject::invokeMethod(anchor.data(), "frameSubmitted",
                                     Qt::DirectConnection);
  };

  // Home is idle; root presentation accounting still completes each signal.
  ASSERT_FALSE(ui.needs_continuous_rendering());
  ASSERT_TRUE(submit_root());
  EXPECT_EQ(renderer.completed_frames, 1u);
  EXPECT_TRUE(requested_after_frames.empty());

  // Loading requests continuous work without showing or initializing the RHI.
  ui.begin_loading_presentation();
  ASSERT_TRUE(ui.needs_continuous_rendering());
  for (std::size_t frame = 2u; frame != 5u; ++frame) {
    ASSERT_TRUE(submit_root());
    EXPECT_EQ(renderer.completed_frames, frame);
    EXPECT_EQ(requested_after_frames.size(), frame - 1u);
    ASSERT_FALSE(requested_after_frames.empty());
    EXPECT_EQ(requested_after_frames.back(), frame)
        << "complete root accounting before requesting the next app frame";
  }

  renderer.synchronized = false;
  ASSERT_TRUE(submit_root());
  EXPECT_EQ(renderer.completed_frames, 5u);
  EXPECT_EQ(requested_after_frames.size(), 3u);

  renderer.synchronized = true;
  ui.end_loading_presentation(false);
  ASSERT_FALSE(ui.needs_continuous_rendering());
  ASSERT_TRUE(submit_root());
  EXPECT_EQ(renderer.completed_frames, 6u);
  EXPECT_EQ(requested_after_frames.size(), 3u);

  ui.begin_loading_presentation();
  ASSERT_TRUE(ui.needs_continuous_rendering());
  ui.set_viewport_presentation_requested_callback({});
  ASSERT_TRUE(submit_root());
  EXPECT_EQ(renderer.completed_frames, 7u);
  EXPECT_EQ(requested_after_frames.size(), 3u);

  ui.set_viewport_presentation_requested_callback(request_frame);
  ASSERT_TRUE(submit_root());
  EXPECT_EQ(renderer.completed_frames, 8u);
  ASSERT_EQ(requested_after_frames.size(), 4u);
  EXPECT_EQ(requested_after_frames.back(), 8u);

  ui.detach_render_resources();
  ASSERT_TRUE(submit_root());
  EXPECT_EQ(renderer.completed_frames, 9u);
  EXPECT_EQ(requested_after_frames.size(), 4u);
  EXPECT_EQ(anchor->shared_context(), nullptr);

  ASSERT_TRUE(QMetaObject::invokeMethod(anchor.data(), "frameSubmitted",
                                       Qt::QueuedConnection));
  ui.shutdown();
  EXPECT_EQ(anchor, nullptr);
  QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
  EXPECT_EQ(renderer.completed_frames, 9u);
  EXPECT_EQ(requested_after_frames.size(), 4u);
}
