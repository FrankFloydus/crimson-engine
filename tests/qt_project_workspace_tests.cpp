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
#include "ui/qt_application_style.hpp"
#include "ui/qt_asset_browser.hpp"
#include "ui/qt_auxiliary_window_state.hpp"
#include "ui/qt_contextual_input_hints.hpp"
#include "ui/qt_double_spin_box.hpp"
#include "ui/qt_main_menu.hpp"
#include "ui/qt_multiresolution_icon.hpp"
#include "ui/qt_numeric_spin_box.hpp"
#include "ui/qt_material_browser_window.hpp"
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
#include "ui/ui_system.hpp"
#include "viewport/host_viewport_contract_adapter.hpp"
#include "viewport/viewport_settings.hpp"

#include <QAction>
#include <QAbstractAnimation>
#include <QAbstractItemModel>
#include <QAbstractItemDelegate>
#include <QAbstractItemView>
#include <QApplication>
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
#include <QLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPointer>
#include <QPushButton>
#include <QPropertyAnimation>
#include <QPixmap>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyleOption>
#include <QStyleOptionViewItem>
#include <QTableView>
#include <QTabBar>
#include <QToolBar>
#include <QToolButton>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeView>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include <array>
#include <chrono>
#include <cmath>
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
        name == QStringLiteral("tool_curve")) {
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
  EXPECT_LE(toolbar.width(), 70);
  EXPECT_TRUE(button->isVisible());
  review(QStringLiteral("narrow-dock"));
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
  EXPECT_EQ(expect_persistent_button_faces(tool_toolbar, 40), 13);
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
  EXPECT_EQ(expect_persistent_button_faces(tool_toolbar, 80), 13);
  EXPECT_EQ(scale_metric_probe->style()->pixelMetric(
                QStyle::PM_ButtonMargin, nullptr, scale_metric_probe),
            baseline_button_margin * 2);

  selection_toolbar.apply_ui_scale(
      quader::QtUiScaleMetrics(1.0F, 2U));
  tool_toolbar.apply_ui_scale(quader::QtUiScaleMetrics(1.0F, 2U));
  EXPECT_EQ(selection_toolbar.iconSize(), QSize(32, 32));
  EXPECT_EQ(tool_toolbar.iconSize(), QSize(32, 32));
  EXPECT_EQ(expect_persistent_button_faces(selection_toolbar, 30), 5);
  EXPECT_EQ(expect_persistent_button_faces(tool_toolbar, 40), 13);
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
  constexpr std::array kControls{quader::QuaderViewportControl::ShowGrid,
      quader::QuaderViewportControl::ShowGizmos, quader::QuaderViewportControl::ShowMeshGrid,
      quader::QuaderViewportControl::ShowOverlays};
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
    EXPECT_TRUE(editor.viewports().control(kControls[index]));
    EXPECT_FALSE(owner_state[index]);
  }
  ASSERT_EQ(requested_commands.size(), expected_commands.size());
  for (std::size_t index = 0; index < expected_commands.size(); ++index) {
    EXPECT_EQ(requested_commands[index], expected_commands[index].string());
  }
  std::filesystem::remove(keybindings);

  const QString review_directory = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
  if (!review_directory.isEmpty()) {
    ASSERT_TRUE(QDir().mkpath(review_directory));
    // Load the shipping artwork because this test host has no application QRC.
    const QDir icon_directory(QStringLiteral(QUADER_SOURCE_DIR "/resources/icons"));
    const std::array<const char *, 4> icon_names{
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
  ASSERT_EQ(strip.findChildren<QToolButton *>().size(), 9);
  EXPECT_TRUE(strip.findChildren<QComboBox *>().empty());
  EXPECT_TRUE(strip.findChildren<QDoubleSpinBox *>().empty());
  EXPECT_EQ(snapping.findChildren<QComboBox *>().size(), 2);

  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  std::array<bool, 8> values{};
  std::array<int, 8> preference_writes{};
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
      quader::QuaderViewportControl::RotationSnap, quader::QuaderViewportControl::ShowGrid,
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
    if (index != 0) {
      EXPECT_EQ(preference_writes[index - 1], 0);
    }
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
  EXPECT_EQ(display_requests, 8);
  EXPECT_EQ(preference_writes, (std::array<int, 8>{}));

  for (const auto control : kControls) {
    ASSERT_TRUE(editor.viewports().set_control(control, true).ok());
  }
  ASSERT_TRUE(editor.viewports().set_transform_space(
      quader::QuaderTransformSpace::Local).ok());
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
  EXPECT_EQ(display_requests, 8);
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
  const std::array<const char *, 13> icon_names{
      "tool_select.png", "tool_move.png", "tool_rotate.png", "tool_scale.png",
      "tool_transform_local.png", "snapping_master.png", "snapping_grid.png",
      "snapping_vertex.png", "snapping_rotation.png", "viewport_grid.png",
      "viewport_gizmos.png", "viewport_mesh_grid.png", "viewport_overlays.png"};
  for (std::size_t index = 0; index < actions.size(); ++index) {
    const auto icon = quader::qt_multiresolution_icon(
        icon_directory.filePath(QString::fromUtf8(icon_names[index])));
    ASSERT_FALSE(icon.isNull());
    actions[index]->setIcon(icon);
  }
  actions[0]->setChecked(true);
  actions[5]->setChecked(true);
  actions[9]->setEnabled(false);
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
      window->findChild<QToolBar *>(QStringLiteral("viewportDisplayToolbar")));
  ASSERT_NE(workspace, nullptr);
  ASSERT_NE(container, nullptr);
  ASSERT_NE(viewport, nullptr);
  ASSERT_NE(tools, nullptr);
  ASSERT_NE(snapping, nullptr);
  ASSERT_NE(display, nullptr);
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
    ASSERT_EQ(selection->defaultAction()->menu()->actions().size(), 2);
    EXPECT_EQ(selection->defaultAction()->menu()->actions()[0]->objectName(), QStringLiteral("selectionBox"));
    EXPECT_EQ(selection->defaultAction()->menu()->actions()[1]->objectName(), QStringLiteral("selectionLasso"));
    EXPECT_TRUE(selection->defaultAction()->menu()->actions()[0]->text().startsWith(QStringLiteral("Box Select")));
    EXPECT_TRUE(selection->defaultAction()->menu()->actions()[1]->text().startsWith(QStringLiteral("Lasso")));
    for (QAction *choice : selection->defaultAction()->menu()->actions()) {
      EXPECT_TRUE(choice->shortcuts().empty());
    }
    EXPECT_EQ(editor.viewports().preferred_selection_tool(kPaneIds[pane]),
              quader::ViewportToolId::Select);
    auto *local = button(pane, "toggle_transform_space");
    auto *backface = button(pane, "backfaceSelection");
    ASSERT_NE(local, nullptr);
    ASSERT_NE(backface, nullptr);
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
  EXPECT_EQ(editor.viewports().selected_tool(kPaneIds[0]), quader::ViewportToolId::Select);

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
  EXPECT_EQ(editor.viewports().selected_tool(kPaneIds[0]), quader::ViewportToolId::Move);
  EXPECT_EQ(editor.viewports().selected_tool(kPaneIds[1]), quader::ViewportToolId::Rotate);
  EXPECT_EQ(editor.viewports().selected_tool(kPaneIds[2]), quader::ViewportToolId::Select);
  EXPECT_TRUE(button(0, "tool_move")->isChecked());
  EXPECT_TRUE(button(1, "tool_rotate")->isChecked());
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
  button(1, "viewportGrid")->click();
  button(1, "toggle_transform_space")->click();
  synchronize();
  EXPECT_EQ(editor.viewports().control(kPaneIds[1], quader::QuaderViewportControl::ShowGrid), !top_grid);
  EXPECT_EQ(editor.viewports().transform_space(kPaneIds[1]), quader::QuaderTransformSpace::Local);
  EXPECT_EQ(editor.viewports().transform_space(kPaneIds[0]), quader::QuaderTransformSpace::Global);
  tools->tool_action(quader::ViewportToolId::Scale)->trigger();
  synchronize();
  EXPECT_EQ(editor.viewports().selected_tool(kPaneIds[1]), quader::ViewportToolId::Scale);
  EXPECT_EQ(editor.viewports().selected_tool(kPaneIds[0]), quader::ViewportToolId::Move);

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
  EXPECT_TRUE(tools->tool_action(quader::ViewportToolId::Move)->isChecked());

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
  EXPECT_EQ(editor.viewports().selected_tool(kPaneIds[0]), quader::ViewportToolId::Move);
  EXPECT_EQ(editor.viewports().selected_tool(kPaneIds[1]), quader::ViewportToolId::Scale);
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
       .manifest_size_bytes = 32768u},
      {.name = QStringLiteral("Industrial passage"),
       .manifest_path = QStringLiteral(
           "C:/Projects/Experiments/Environment blockouts/Industrial passage/"
           "Industrial passage.qproj"),
       .manifest_last_modified_unix_ms = 1790163600000,
       .manifest_size_bytes = 18432u},
      {.name = QStringLiteral("Lighting studies"),
       .manifest_path = QStringLiteral(
           "C:/Projects/Lighting studies/Lighting studies.qproj"),
       .manifest_last_modified_unix_ms = 1789990800000,
       .manifest_size_bytes = 4096u},
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
  translated.recent_size_column = QStringLiteral("Dimensione");
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
  expect_rounded_recent_frame();
  EXPECT_TRUE(table->isHidden());
  EXPECT_TRUE(tutorial->isVisible());
  EXPECT_TRUE(documentation->isVisible());
  if (!review_directory.isEmpty()) {
    EXPECT_TRUE(home.grab().save(QDir(review_directory).filePath(
        QStringLiteral("launcher-empty.png"))));
  }

  std::vector<quader::QtRecentProjectItem> many_projects;
  for (int index = 0; index < 12; ++index) {
    many_projects.push_back({
        .name = QStringLiteral("Project %1").arg(index),
        .manifest_path = QStringLiteral("C:/Projects/%1/project.qproj").arg(index),
    });
  }
  home.set_recent_projects(many_projects);
  settle_layout();
  EXPECT_EQ(home.recent_projects_model().rowCount(),
            quader::kQtProjectHomeMaximumRecentProjects);
  EXPECT_EQ(home.recent_projects_model().projects().back().name,
            QStringLiteral("Project 9"));
  scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
  settle_layout();
  EXPECT_TRUE(tutorial->isVisible());
  EXPECT_TRUE(documentation->isVisible());
  EXPECT_LE(tutorial->mapTo(scroll->viewport(), QPoint()).y() + tutorial->height(),
            scroll->viewport()->height());
  if (!review_directory.isEmpty()) {
    EXPECT_TRUE(home.grab().save(QDir(review_directory).filePath(
        QStringLiteral("launcher-ten-recent-scrolled.png"))));
  }
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
       .manifest_size_bytes = 2048u},
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
  ui.show();
  ASSERT_TRUE(ui.main_window()->isVisible());

  const auto created = editor.project().create(project_root);
  ASSERT_TRUE(created.ok()) << created.error_message();
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
  EXPECT_GE(elapsed.elapsed(), 5000);
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
      QStringLiteral("inspector.transform.position.x"));
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
                  QStringLiteral("inspector.transform.position.x")),
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
    ASSERT_TRUE(windowed->reveal_item(first_asset_id));
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
      surface->resize(800, 600);
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
    const QPoint global = surface->mapToGlobal(local);
    QMouseEvent event(type, QPointF(local), QPointF(global), button, buttons,
                      Qt::NoModifier);
    QCoreApplication::sendEvent(surface, &event);
  }
};

} // namespace

TEST(QtProjectWorkspaceTests,
     SameToolKeyboardShortcutAcquiresTheReceivingPane) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  NativePaneInputHarness harness;
  ASSERT_NO_FATAL_FAILURE(harness.initialize());
  constexpr auto kPerspective = quader::ViewportPaneId::Perspective;
  constexpr auto kTop = quader::ViewportPaneId::Top;
  harness.focus(kTop);
  ASSERT_TRUE(harness.editor.tools().activate(quader::ViewportToolId::Move).ok());
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
  EXPECT_FALSE(harness.editor.tools().pane_owns_executing_tool(kPerspective));
  EXPECT_EQ(harness.editor.tools().active_settings_presentation_origin().pane, kTop);
  EXPECT_GT(harness.editor.tools().active_settings_presentation_origin().serial, origin.serial);
  EXPECT_EQ(harness.editor.viewports().selected_tool(kPerspective), quader::ViewportToolId::Move);
  EXPECT_EQ(harness.editor.viewports().selected_tool(kTop), quader::ViewportToolId::Move);
  EXPECT_EQ(harness.scene.history().undo_count(), undo_count);
}

TEST(QtProjectWorkspaceTests,
     PassiveOtherPaneContextMenuPreservesPendingExtrusion) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  NativePaneInputHarness harness;
  ASSERT_NO_FATAL_FAILURE(harness.initialize());
  constexpr auto kPerspective = quader::ViewportPaneId::Perspective;
  constexpr auto kTop = quader::ViewportPaneId::Top;
  const auto object = harness.editor.modeling().create_box(
      {.min = {-1, -1, -1}, .max = {1, 1, 1}}, "Pending pane extrusion");
  ASSERT_TRUE(object.ok()) << object.error_message();
  ASSERT_TRUE(harness.editor.selection().select_object(object.value()).ok());
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
  ASSERT_TRUE(menu_observed) << "the real native context menu must have opened";
  EXPECT_TRUE(harness.editor.tools().active_settings().preview_active);
  EXPECT_TRUE(harness.editor.tools().pane_owns_executing_tool(kPerspective));
  EXPECT_EQ(harness.editor.tools().active_settings_presentation_origin(), origin);
  EXPECT_EQ(harness.editor.viewports().selected_tool(kTop), quader::ViewportToolId::Move);
  EXPECT_EQ(harness.editor.viewports().selected_tool(kPerspective), quader::ViewportToolId::Extrude);
  EXPECT_EQ(harness.scene.history().undo_count(), undo_count);
  EXPECT_EQ(harness.scene.history().redo_count(), redo_count);
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
     BoxAndLassoDragsKeepTheirInitiatingPaneUntilRelease) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  NativePaneInputHarness harness;
  ASSERT_NO_FATAL_FAILURE(harness.initialize());
  constexpr auto kPerspective = quader::ViewportPaneId::Perspective;
  constexpr auto kTop = quader::ViewportPaneId::Top;
  for (const auto tool : {quader::ViewportToolId::Select, quader::ViewportToolId::LassoSelect}) {
    SCOPED_TRACE(static_cast<int>(tool));
    harness.focus(kPerspective);
    ASSERT_TRUE(harness.editor.tools().activate(tool).ok());
    harness.process();
    harness.mouse(kPerspective, QEvent::MouseButtonPress, {40, 40}, Qt::LeftButton, Qt::LeftButton);
    harness.process();
    harness.mouse(kPerspective, QEvent::MouseMove, {120, 120}, Qt::NoButton, Qt::LeftButton);
    harness.process();
    const auto expected_gesture = tool == quader::ViewportToolId::Select
        ? quader::QuaderInputSelectionGesture::Box : quader::QuaderInputSelectionGesture::Lasso;
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

TEST(QtProjectWorkspaceTests, AssetBrowserNativeCtrlSpaceHasNoIntermediateDockAnimation) {
  if (qEnvironmentVariableIntValue("QUADER_RUN_VULKAN_ASSET_BROWSER_TESTS") != 1) {
    GTEST_SKIP() << "Set QUADER_RUN_VULKAN_ASSET_BROWSER_TESTS=1 for native dock transitions";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
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
      qEnvironmentVariable("QT_PLUGIN_PATH").toStdWString());
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
  ASSERT_NE(project.active_scene(), nullptr);
  auto &scene = *project.active_scene()->session;
  quader::UISystem ui;
  ASSERT_TRUE(ui.initialize(renderer, resources));
  ui.install_runtime_hooks(editor, scene.scene(), scene.assets(), scene.modeling(),
      scene.selection(), scene.native_tools(), scene.point_placement(), quader::kInvalidAssetId);
  auto *window = ui.main_window();
  window->resize(1600, 1000);
  ui.show();
  window->activateWindow();
  QElapsedTimer startup;
  startup.start();
  while (!window->updatesEnabled() && startup.elapsed() < 3000) {
    application.processEvents(QEventLoop::AllEvents, 10);
    ui.refresh({}, {});
  }
  ASSERT_TRUE(window->updatesEnabled());
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
  const auto send_toggle = [&](QWidget &target) {
    QKeyEvent press(QEvent::KeyPress, Qt::Key_Space, Qt::ControlModifier);
    QCoreApplication::sendEvent(&target, &press);
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_Space, Qt::ControlModifier);
    QCoreApplication::sendEvent(&target, &release);
  };
  for (int transition = 0; transition < 3; ++transition) {
    // Closing the browser activates the native viewport QWindow. Settle that
    // activation before sending the next physical chord from a Qt panel.
    window->activateWindow();
    QApplication::setActiveWindow(window);
    ASSERT_TRUE(editor.ui().focus_panel("outliner").ok());
    outliner_tree->setFocus(Qt::OtherFocusReason);
    application.processEvents();
    outliner_tree->setFocus(Qt::OtherFocusReason);
    ASSERT_EQ(QApplication::focusWidget(), outliner_tree);
    send_toggle(*outliner_tree);
    ASSERT_TRUE(editor.ui().panel_visible("asset_browser"));
    EXPECT_TRUE(browser->owns_browser_focus());
    const QRect opening = dock->geometry();
    // Native toolbars sit outside the dock area; the browser spans both side
    // docks, rather than only the viewport between them.
    EXPECT_NEAR(opening.left(), outliner->geometry().left(), 2);
    EXPECT_NEAR(opening.right(), inspector->geometry().right(), 2);
    for (int frame = 0; frame < 8; ++frame) {
      application.processEvents();
      ui.refresh({}, {});
      EXPECT_EQ(dock->geometry(), opening);
      for (auto *animation : window->findChildren<QPropertyAnimation *>()) {
        EXPECT_FALSE(animation->state() == QAbstractAnimation::Running &&
                     animation->propertyName() == QByteArrayLiteral("geometry"));
      }
    }
    const QString review = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
    if (!review.isEmpty()) {
      ASSERT_TRUE(QDir().mkpath(review));
      EXPECT_TRUE(window->grab().save(QDir(review).filePath(
          QStringLiteral("asset-browser-native-open-%1.png").arg(transition))));
    }
    QWidget *focus = QApplication::focusWidget();
    ASSERT_NE(focus, nullptr);
    EXPECT_TRUE(browser->owns_browser_focus());
    send_toggle(*focus);
    EXPECT_FALSE(editor.ui().panel_visible("asset_browser"));
    application.processEvents();
  }
  ui.shutdown();
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
