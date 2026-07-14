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
#include "editor/session/project_session.hpp"
#include "ui/inspector_panel.hpp"
#include "ui/qt_color_swatch_button.hpp"
#include "ui/qt_main_menu.hpp"
#include "ui/qt_main_window.hpp"
#include "ui/qt_settings_dialog.hpp"
#include "ui/ui_context.hpp"
#include "ui/ui_settings.hpp"
#include "viewport/viewport_settings.hpp"

#include <QApplication>
#include <QAction>
#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QKeyEvent>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QPushButton>
#include <QTimer>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#ifndef QUADER_SOURCE_DIR
#define QUADER_SOURCE_DIR "."
#endif

namespace quader {
namespace {

LocalizationService loaded_localization(std::string_view locale = "en-US") {
  LocalizationService localization;
  EXPECT_TRUE(localization.load(
      std::filesystem::path(QUADER_SOURCE_DIR) / "resources" / "locales",
      std::string{locale}));
  return localization;
}

void process_animated_clicks() {
  QEventLoop loop;
  QTimer::singleShot(150, &loop, &QEventLoop::quit);
  loop.exec();
}

void send_key(QWidget &target, int key,
              Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  QKeyEvent press(QEvent::KeyPress, key, modifiers);
  QCoreApplication::sendEvent(&target, &press);
  QKeyEvent release(QEvent::KeyRelease, key, modifiers);
  QCoreApplication::sendEvent(&target, &release);
  process_animated_clicks();
}

std::size_t count_changes(const std::vector<QtSettingsChangeKind> &changes,
                          QtSettingsChangeKind kind) {
  return static_cast<std::size_t>(
      std::count(changes.begin(), changes.end(), kind));
}

ViewportSettingColor setting_color(const QColor &color) {
  return {
      .r = static_cast<float>(color.redF()),
      .g = static_cast<float>(color.greenF()),
      .b = static_cast<float>(color.blueF()),
      .a = static_cast<float>(color.alphaF()),
  };
}

std::optional<std::string> history_label(const std::string *label) {
  return label != nullptr ? std::optional<std::string>(*label) : std::nullopt;
}

bool accept_color_picker(QWidget &owner, QPushButton &swatch,
                         QString object_name, QColor color) {
  bool opened = false;
  QTimer::singleShot(0, &owner, [&]() {
    auto *picker = owner.findChild<QColorDialog *>(object_name);
    if (picker == nullptr) {
      ADD_FAILURE() << "Expected the staged color picker to open.";
      if (QWidget *modal = QApplication::activeModalWidget()) {
        modal->close();
      }
      return;
    }
    opened = true;
    picker->setCurrentColor(color);
    picker->accept();
  });
  swatch.click();
  return opened;
}

bool reject_color_picker(QWidget &owner, QPushButton &swatch,
                         QString object_name, QColor color,
                         std::function<void()> inspect_preview = {}) {
  bool opened = false;
  QTimer::singleShot(0, &owner, [&]() {
    auto *picker = owner.findChild<QColorDialog *>(object_name);
    if (picker == nullptr) {
      ADD_FAILURE() << "Expected the staged color picker to open.";
      if (QWidget *modal = QApplication::activeModalWidget()) {
        modal->close();
      }
      return;
    }
    opened = true;
    picker->setCurrentColor(color);
    if (inspect_preview) {
      inspect_preview();
    }
    picker->reject();
  });
  swatch.click();
  return opened;
}

TEST(QtSettingsDialogTests,
     ApplyOkAndCancelOwnOneStagedSettingsTransaction) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-settings-dialog-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  UISettings ui_settings = UISettings::defaults();
  ViewportSettings viewport_settings = ViewportSettings::defaults();
  LocalizationService localization = loaded_localization();
  const float initial_thickness = viewport_settings.float_value(
      ViewportFloatSettingId::GridMajorLineSize);

  std::vector<QtSettingsChangeKind> changes;
  QtSettingsDialog dialog;
  dialog.setAttribute(Qt::WA_DontShowOnScreen, true);
  dialog.set_context(
      ui_settings, viewport_settings, localization,
      [&](QtSettingsChangeKind kind) { changes.push_back(kind); });

  auto *category_list =
      dialog.findChild<QListWidget *>(QStringLiteral("settingsCategories"));
  auto *button_box =
      dialog.findChild<QDialogButtonBox *>(QStringLiteral("settingsButtonBox"));
  auto *apply_button =
      dialog.findChild<QPushButton *>(QStringLiteral("settingsApply"));
  auto *ok_button =
      dialog.findChild<QPushButton *>(QStringLiteral("settingsOk"));
  auto *cancel_button =
      dialog.findChild<QPushButton *>(QStringLiteral("settingsCancel"));
  auto *thickness = dialog.findChild<QDoubleSpinBox *>(
      QStringLiteral("viewportSetting_GridMajorLineSize"));
  auto *show_grid = dialog.findChild<QCheckBox *>(
      QStringLiteral("viewportSetting_ShowGrid"));
  auto *show_info = dialog.findChild<QCheckBox *>(
      QStringLiteral("viewportSetting_ShowInfo"));
  auto *locale_combo =
      dialog.findChild<QComboBox *>(QStringLiteral("settingsLocale"));
  auto *mesh_major_swatch = dialog.findChild<QPushButton *>(
      QStringLiteral("viewportSetting_MeshGridMajorColor"));

  ASSERT_NE(category_list, nullptr);
  ASSERT_NE(button_box, nullptr);
  ASSERT_NE(apply_button, nullptr);
  ASSERT_NE(ok_button, nullptr);
  ASSERT_NE(cancel_button, nullptr);
  ASSERT_NE(thickness, nullptr);
  ASSERT_NE(show_grid, nullptr);
  ASSERT_NE(show_info, nullptr);
  ASSERT_NE(locale_combo, nullptr);
  ASSERT_NE(mesh_major_swatch, nullptr);
  EXPECT_EQ(dialog.findChild<QPushButton *>(
                QStringLiteral("settingsResetDefaults")),
            nullptr);

  int grid_row = -1;
  for (int row = 0; row < category_list->count(); ++row) {
    if (category_list->item(row)->data(Qt::UserRole).toString() ==
        QStringLiteral("grid")) {
      grid_row = row;
      break;
    }
  }
  ASSERT_GE(grid_row, 0);
  category_list->setCurrentRow(grid_row);

  dialog.show();
  QCoreApplication::processEvents();
  EXPECT_TRUE(dialog.isVisible());
  EXPECT_EQ(dialog.windowModality(), Qt::WindowModal);
  EXPECT_EQ(button_box->standardButtons(),
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
                QDialogButtonBox::Apply);
  EXPECT_TRUE(ok_button->autoDefault());
  EXPECT_TRUE(ok_button->isDefault());
  EXPECT_FALSE(apply_button->autoDefault());
  EXPECT_FALSE(apply_button->isDefault());
  EXPECT_FALSE(cancel_button->autoDefault());
  EXPECT_FALSE(cancel_button->isDefault());
  EXPECT_FALSE(apply_button->isEnabled());

  for (const ViewportColorSettingDescriptor &descriptor :
       viewport_color_settings()) {
    auto *swatch = dialog.findChild<QPushButton *>(
        QStringLiteral("viewportSetting_%1")
            .arg(QString::fromUtf8(descriptor.key.data(),
                                   static_cast<qsizetype>(
                                       descriptor.key.size()))));
    ASSERT_NE(swatch, nullptr) << descriptor.key;
    ASSERT_NE(dynamic_cast<QtColorSwatchButton *>(swatch), nullptr)
        << descriptor.key;
    EXPECT_FALSE(swatch->autoDefault()) << descriptor.key;
    EXPECT_FALSE(swatch->isDefault()) << descriptor.key;
  }

  const int italian_index = locale_combo->findData(QStringLiteral("it-IT"));
  const int english_index = locale_combo->findData(QStringLiteral("en-US"));
  ASSERT_GE(italian_index, 0);
  ASSERT_GE(english_index, 0);

  const ViewportSettingColor initial_mesh_major =
      viewport_settings.color(ViewportColorSettingId::MeshGridMajor);
  const QColor applied_mesh_major = QColor::fromRgb(46, 92, 138, 184);
  ASSERT_TRUE(accept_color_picker(
      dialog, *mesh_major_swatch,
      QStringLiteral("viewportSetting_MeshGridMajorColor_dialog"),
      applied_mesh_major));

  thickness->setValue(1.75);
  show_grid->setChecked(false);
  locale_combo->setCurrentIndex(italian_index);
  EXPECT_TRUE(apply_button->isEnabled());
  EXPECT_FLOAT_EQ(viewport_settings.float_value(
                      ViewportFloatSettingId::GridMajorLineSize),
                  initial_thickness);
  EXPECT_TRUE(viewport_settings.bool_value(ViewportBoolSettingId::ShowGrid));
  EXPECT_EQ(ui_settings.locale(), "en-US");
  EXPECT_EQ(localization.current_locale(), "en-US");
  EXPECT_TRUE(viewport_setting_colors_approximately(
      viewport_settings.color(ViewportColorSettingId::MeshGridMajor),
      initial_mesh_major));
  EXPECT_TRUE(changes.empty());

  apply_button->click();
  QCoreApplication::processEvents();
  EXPECT_TRUE(dialog.isVisible());
  EXPECT_FALSE(apply_button->isEnabled());
  EXPECT_FLOAT_EQ(viewport_settings.float_value(
                      ViewportFloatSettingId::GridMajorLineSize),
                  1.75f);
  EXPECT_FALSE(viewport_settings.bool_value(ViewportBoolSettingId::ShowGrid));
  EXPECT_EQ(ui_settings.locale(), "it-IT");
  EXPECT_EQ(localization.current_locale(), "it-IT");
  EXPECT_TRUE(viewport_setting_colors_approximately(
      viewport_settings.color(ViewportColorSettingId::MeshGridMajor),
      setting_color(applied_mesh_major)));
  EXPECT_EQ(count_changes(changes, QtSettingsChangeKind::ViewportCommit), 1u);
  EXPECT_EQ(count_changes(changes, QtSettingsChangeKind::InterfaceCommit), 1u);

  thickness->setValue(2.5);
  show_grid->setChecked(true);
  locale_combo->setCurrentIndex(english_index);
  const QColor discarded_mesh_major = QColor::fromRgb(186, 130, 74, 105);
  ASSERT_TRUE(accept_color_picker(
      dialog, *mesh_major_swatch,
      QStringLiteral("viewportSetting_MeshGridMajorColor_dialog"),
      discarded_mesh_major));
  EXPECT_TRUE(apply_button->isEnabled());
  const std::size_t viewport_commits_before_cancel =
      count_changes(changes, QtSettingsChangeKind::ViewportCommit);
  const std::size_t interface_commits_before_cancel =
      count_changes(changes, QtSettingsChangeKind::InterfaceCommit);
  cancel_button->click();
  QCoreApplication::processEvents();
  EXPECT_FALSE(dialog.isVisible());
  EXPECT_EQ(dialog.result(), QDialog::Rejected);
  EXPECT_FLOAT_EQ(viewport_settings.float_value(
                      ViewportFloatSettingId::GridMajorLineSize),
                  1.75f);
  EXPECT_FALSE(viewport_settings.bool_value(ViewportBoolSettingId::ShowGrid));
  EXPECT_EQ(ui_settings.locale(), "it-IT");
  EXPECT_EQ(localization.current_locale(), "it-IT");
  EXPECT_TRUE(viewport_setting_colors_approximately(
      viewport_settings.color(ViewportColorSettingId::MeshGridMajor),
      setting_color(applied_mesh_major)));
  EXPECT_EQ(count_changes(changes, QtSettingsChangeKind::ViewportCommit),
            viewport_commits_before_cancel);
  EXPECT_EQ(count_changes(changes, QtSettingsChangeKind::InterfaceCommit),
            interface_commits_before_cancel);

  dialog.show();
  QCoreApplication::processEvents();
  EXPECT_DOUBLE_EQ(thickness->value(), 1.75);
  EXPECT_FALSE(show_grid->isChecked());

  ASSERT_TRUE(viewport_settings.set_bool_value(
      ViewportBoolSettingId::ShowInfo, true));
  thickness->setValue(3.0);
  dialog.refresh();
  EXPECT_TRUE(show_info->isChecked());
  ok_button->click();
  QCoreApplication::processEvents();
  EXPECT_FALSE(dialog.isVisible());
  EXPECT_EQ(dialog.result(), QDialog::Accepted);
  EXPECT_FLOAT_EQ(viewport_settings.float_value(
                      ViewportFloatSettingId::GridMajorLineSize),
                  3.0f);
  EXPECT_TRUE(viewport_settings.bool_value(ViewportBoolSettingId::ShowInfo));
  EXPECT_EQ(count_changes(changes, QtSettingsChangeKind::ViewportCommit), 2u);

  dialog.show();
  QCoreApplication::processEvents();
  thickness->setValue(4.0);
  thickness->setFocus(Qt::OtherFocusReason);
  const std::size_t commits_before_escape = changes.size();
  send_key(*thickness, Qt::Key_Escape);
  EXPECT_FALSE(dialog.isVisible());
  EXPECT_FLOAT_EQ(viewport_settings.float_value(
                  ViewportFloatSettingId::GridMajorLineSize),
                  3.0f);
  EXPECT_EQ(changes.size(), commits_before_escape);
  EXPECT_EQ(dialog.result(), QDialog::Rejected);

  dialog.show();
  QCoreApplication::processEvents();
  thickness->setValue(5.0);
  const std::size_t commits_before_close = changes.size();
  dialog.close();
  QCoreApplication::processEvents();
  EXPECT_FALSE(dialog.isVisible());
  EXPECT_FLOAT_EQ(viewport_settings.float_value(
                  ViewportFloatSettingId::GridMajorLineSize),
                  3.0f);
  EXPECT_EQ(changes.size(), commits_before_close);
  EXPECT_EQ(dialog.result(), QDialog::Rejected);

  dialog.show();
  QCoreApplication::processEvents();
  thickness->setValue(1.25);
  thickness->setFocus(Qt::OtherFocusReason);
  send_key(*thickness, Qt::Key_Return);
  EXPECT_FALSE(dialog.isVisible());
  EXPECT_EQ(dialog.result(), QDialog::Accepted);
  EXPECT_FLOAT_EQ(viewport_settings.float_value(
                      ViewportFloatSettingId::GridMajorLineSize),
                  1.25f);

  dialog.show();
  QCoreApplication::processEvents();
  thickness->setValue(1.5);
  thickness->setFocus(Qt::OtherFocusReason);
  send_key(*thickness, Qt::Key_Enter, Qt::KeypadModifier);
  EXPECT_FALSE(dialog.isVisible());
  EXPECT_FLOAT_EQ(viewport_settings.float_value(
                      ViewportFloatSettingId::GridMajorLineSize),
                  1.5f);
  EXPECT_EQ(count_changes(changes, QtSettingsChangeKind::ViewportCommit), 4u);
  EXPECT_EQ(count_changes(changes, QtSettingsChangeKind::InterfaceCommit), 1u);

  const std::size_t commits_before_clean_ok = changes.size();
  dialog.show();
  QCoreApplication::processEvents();
  ok_button->click();
  QCoreApplication::processEvents();
  EXPECT_FALSE(dialog.isVisible());
  EXPECT_EQ(changes.size(), commits_before_clean_ok);
}

TEST(InspectorPanelTests,
     ColorCancelDoesNotMutateDocumentHistoryOrDirtyState) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-inspector-color-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  ProjectSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_project_session(session));
  const QuaderApiResult<QuaderEntityRef> light =
      editor.scene().create_point_light(
          QuaderCreatePointLightArgs{.name = "Inspector Color Light",
                                     .select_created = true});
  ASSERT_TRUE(light.ok()) << light.error_message();

  UISettings ui_settings = UISettings::defaults();
  ViewportSettings viewport_settings = ViewportSettings::defaults();
  LocalizationService localization = loaded_localization();
  UIInspectorPreviewContext preview{.document = session.scene(),
                                    .assets = session.assets()};
  UIContext context{
      .intent = {.editor = editor},
      .scene = {.document = session.scene(),
                .assets = session.assets(),
                .modeling = session.modeling(),
                .selection = session.selection()},
      .inspector_preview = &preview,
      .preferences = {.ui = ui_settings, .viewport = viewport_settings},
      .localization = localization,
  };

  InspectorPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.refresh(context);
  auto *light_swatch = panel.findChild<QPushButton *>(
      QStringLiteral("inspector.light.color"));
  ASSERT_NE(light_swatch, nullptr);
  const LightComponent initial_light = *session.scene().light(light.value().id);
  const std::optional<std::string> light_undo_before =
      history_label(session.history().undo_label());
  const std::optional<std::string> light_redo_before =
      history_label(session.history().redo_label());
  session.mark_clean();
  ASSERT_TRUE(reject_color_picker(
      panel, *light_swatch, QStringLiteral("inspector.light.color.dialog"),
      QColor::fromRgb(31, 127, 223), [&]() {
        const LightComponent *during = session.scene().light(light.value().id);
        EXPECT_NE(during, nullptr);
        if (during != nullptr) {
          EXPECT_FLOAT_EQ(during->color.x, initial_light.color.x);
          EXPECT_FLOAT_EQ(during->color.y, initial_light.color.y);
          EXPECT_FLOAT_EQ(during->color.z, initial_light.color.z);
        }
        EXPECT_FALSE(session.dirty());
        EXPECT_EQ(history_label(session.history().undo_label()),
                  light_undo_before);
        EXPECT_EQ(history_label(session.history().redo_label()),
                  light_redo_before);
      }));
  const LightComponent *after_light = session.scene().light(light.value().id);
  ASSERT_NE(after_light, nullptr);
  EXPECT_FLOAT_EQ(after_light->color.x, initial_light.color.x);
  EXPECT_FLOAT_EQ(after_light->color.y, initial_light.color.y);
  EXPECT_FLOAT_EQ(after_light->color.z, initial_light.color.z);
  EXPECT_FALSE(session.dirty());
  EXPECT_EQ(history_label(session.history().undo_label()), light_undo_before);
  EXPECT_EQ(history_label(session.history().redo_label()), light_redo_before);

  const QColor committed_light_color = QColor::fromRgb(31, 127, 223);
  ASSERT_TRUE(accept_color_picker(
      panel, *light_swatch, QStringLiteral("inspector.light.color.dialog"),
      committed_light_color));
  const LightComponent *committed_light =
      session.scene().light(light.value().id);
  ASSERT_NE(committed_light, nullptr);
  EXPECT_NEAR(committed_light->color.x,
              static_cast<float>(committed_light_color.redF()), 0.00001f);
  EXPECT_NEAR(committed_light->color.y,
              static_cast<float>(committed_light_color.greenF()), 0.00001f);
  EXPECT_NEAR(committed_light->color.z,
              static_cast<float>(committed_light_color.blueF()), 0.00001f);
  EXPECT_TRUE(session.dirty());
  EXPECT_EQ(history_label(session.history().undo_label()),
            std::optional<std::string>("Set Light Component"));
  ASSERT_TRUE(editor.history().undo().ok());

  const EntityId root = session.scene().scene_root();
  ASSERT_TRUE(editor.selection()
                  .select_objects_for_inspection(
                      std::span<const EntityId>(&root, 1u), root)
                  .ok());
  panel.refresh(context);
  auto *clear_color_swatch = panel.findChild<QPushButton *>(
      QStringLiteral("inspector.environment.clear_color"));
  ASSERT_NE(clear_color_swatch, nullptr);
  const SceneSettingsComponent initial_scene =
      *session.scene().scene_settings(root);
  const std::optional<std::string> scene_undo_before =
      history_label(session.history().undo_label());
  const std::optional<std::string> scene_redo_before =
      history_label(session.history().redo_label());
  session.mark_clean();
  ASSERT_TRUE(reject_color_picker(
      panel, *clear_color_swatch,
      QStringLiteral("inspector.environment.clear_color.dialog"),
      QColor::fromRgb(211, 83, 47), [&]() {
        const SceneSettingsComponent *during =
            session.scene().scene_settings(root);
        EXPECT_NE(during, nullptr);
        if (during != nullptr) {
          EXPECT_FLOAT_EQ(during->environment.clear_color.x,
                          initial_scene.environment.clear_color.x);
          EXPECT_FLOAT_EQ(during->environment.clear_color.y,
                          initial_scene.environment.clear_color.y);
          EXPECT_FLOAT_EQ(during->environment.clear_color.z,
                          initial_scene.environment.clear_color.z);
        }
        EXPECT_FALSE(session.dirty());
        EXPECT_EQ(history_label(session.history().undo_label()),
                  scene_undo_before);
        EXPECT_EQ(history_label(session.history().redo_label()),
                  scene_redo_before);
      }));
  const SceneSettingsComponent *after_scene =
      session.scene().scene_settings(root);
  ASSERT_NE(after_scene, nullptr);
  EXPECT_FLOAT_EQ(after_scene->environment.clear_color.x,
                  initial_scene.environment.clear_color.x);
  EXPECT_FLOAT_EQ(after_scene->environment.clear_color.y,
                  initial_scene.environment.clear_color.y);
  EXPECT_FLOAT_EQ(after_scene->environment.clear_color.z,
                  initial_scene.environment.clear_color.z);
  EXPECT_FALSE(session.dirty());
  EXPECT_EQ(history_label(session.history().undo_label()), scene_undo_before);
  EXPECT_EQ(history_label(session.history().redo_label()), scene_redo_before);

  const QColor committed_scene_color = QColor::fromRgb(211, 83, 47);
  ASSERT_TRUE(accept_color_picker(
      panel, *clear_color_swatch,
      QStringLiteral("inspector.environment.clear_color.dialog"),
      committed_scene_color));
  const SceneSettingsComponent *committed_scene =
      session.scene().scene_settings(root);
  ASSERT_NE(committed_scene, nullptr);
  EXPECT_NEAR(committed_scene->environment.clear_color.x,
              static_cast<float>(committed_scene_color.redF()), 0.00001f);
  EXPECT_NEAR(committed_scene->environment.clear_color.y,
              static_cast<float>(committed_scene_color.greenF()), 0.00001f);
  EXPECT_NEAR(committed_scene->environment.clear_color.z,
              static_cast<float>(committed_scene_color.blueF()), 0.00001f);
  EXPECT_TRUE(session.dirty());
  EXPECT_EQ(history_label(session.history().undo_label()),
            std::optional<std::string>("Set Scene Settings"));
}

TEST(QtMainMenuTests, ToolActionsUseEffectiveKeybindingsAndCommandRouting) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-main-menu-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  ProjectSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_project_session(session));
  const std::filesystem::path keybindings =
      std::filesystem::temp_directory_path() /
      "quader-main-menu-effective-tool-shortcut.json";
  std::filesystem::remove(keybindings);
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path(keybindings).ok());
  const QuaderApiResult<QuaderModelingObjectRef> box =
      editor.modeling().create_box(
          QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                       .max = {1.0f, 1.0f, 1.0f}},
          "Menu Pivot Box");
  ASSERT_TRUE(box.ok()) << box.error_message();
  ASSERT_TRUE(editor.selection().select_object(box.value()).ok());

  ViewportSettings viewport_settings = ViewportSettings::defaults();
  LocalizationService localization = loaded_localization();
  QMenuBar menu_bar;
  QtMainMenu menu;
  menu.initialize(menu_bar,
                  QtMainMenuContext{
                      .editor = editor,
                      .modeling = session.modeling(),
                      .viewport_settings = viewport_settings,
                      .localization = localization,
                  });
  auto *pivot_action =
      menu_bar.findChild<QAction *>(QStringLiteral("tool_pivot"));
  ASSERT_NE(pivot_action, nullptr);
  EXPECT_TRUE(pivot_action->text().contains(QStringLiteral("\tP")));

  ASSERT_TRUE(editor.preferences()
                  .set_shortcut(quader_command::kToolsActivatePivot,
                                "Ctrl+Alt+P")
                  .ok());
  menu.refresh();
  EXPECT_TRUE(
      pivot_action->text().contains(QStringLiteral("\tCtrl+Alt+P")));
  EXPECT_FALSE(editor.commands().last_status().has_value());

  pivot_action->trigger();
  ASSERT_TRUE(editor.commands().last_status().has_value());
  EXPECT_TRUE(editor.commands().last_status()->ok());
  EXPECT_EQ(editor.tools().active_tool(), "pivot");

  std::filesystem::remove(keybindings);
}

TEST(QtMainMenuTests, RebuildAndDestructionReleaseOwnedHierarchy) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-main-menu-lifetime-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  ProjectSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_project_session(session));
  ViewportSettings viewport_settings = ViewportSettings::defaults();
  LocalizationService localization = loaded_localization();
  QMenuBar menu_bar;
  QPointer<QAction> retained_action;
  QPointer<QMenu> retained_menu;

  {
    QtMainMenu menu;
    menu.initialize(menu_bar,
                    QtMainMenuContext{
                        .editor = editor,
                        .modeling = session.modeling(),
                        .viewport_settings = viewport_settings,
                        .localization = localization,
                    });

    const qsizetype menu_count = menu_bar.findChildren<QMenu *>().size();
    const qsizetype action_count = menu_bar.findChildren<QAction *>().size();
    ASSERT_GT(menu_count, 0);
    ASSERT_GT(action_count, 0);

    for (int rebuild = 0; rebuild < 5; ++rebuild) {
      retained_menu =
          menu_bar.findChild<QMenu *>(QStringLiteral("menu_file"));
      retained_action =
          menu_bar.findChild<QAction *>(QStringLiteral("action_exit"));
      ASSERT_FALSE(retained_menu.isNull());
      ASSERT_FALSE(retained_action.isNull());

      menu.rebuild();

      EXPECT_TRUE(retained_menu.isNull());
      EXPECT_TRUE(retained_action.isNull());
      EXPECT_EQ(menu_bar.findChildren<QMenu *>().size(), menu_count);
      EXPECT_EQ(menu_bar.findChildren<QAction *>().size(), action_count);
    }

    retained_menu =
        menu_bar.findChild<QMenu *>(QStringLiteral("menu_file"));
    retained_action =
        menu_bar.findChild<QAction *>(QStringLiteral("action_exit"));
    ASSERT_FALSE(retained_menu.isNull());
    ASSERT_FALSE(retained_action.isNull());
  }

  EXPECT_TRUE(retained_menu.isNull());
  EXPECT_TRUE(retained_action.isNull());
  EXPECT_TRUE(menu_bar.actions().isEmpty());
  EXPECT_TRUE(menu_bar.findChildren<QMenu *>().isEmpty());
}

TEST(QtMainWindowTests, MenuExitAndNativeCloseShareOneRequestBoundary) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-main-window-close-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  ProjectSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_project_session(session));
  ViewportSettings viewport_settings = ViewportSettings::defaults();
  LocalizationService localization = loaded_localization();

  QtMainWindow main_window;
  main_window.setAttribute(Qt::WA_DontShowOnScreen, true);
  bool allow_close = false;
  int close_requests = 0;
  main_window.set_close_request_handler([&]() {
    ++close_requests;
    return allow_close;
  });
  QtMainMenu menu;
  menu.initialize(
      *main_window.menuBar(),
      QtMainMenuContext{
          .editor = editor,
          .modeling = session.modeling(),
          .viewport_settings = viewport_settings,
          .localization = localization,
          .request_quit = [&]() { (void)main_window.close(); },
      });
  main_window.show();
  ASSERT_TRUE(main_window.isVisible());

  auto *exit_action = main_window.findChild<QAction *>(
      QStringLiteral("action_exit"));
  ASSERT_NE(exit_action, nullptr);
  exit_action->trigger();
  EXPECT_EQ(close_requests, 1);
  EXPECT_TRUE(main_window.isVisible());

  EXPECT_FALSE(main_window.close());
  EXPECT_EQ(close_requests, 2);
  EXPECT_TRUE(main_window.isVisible());

  allow_close = true;
  EXPECT_TRUE(main_window.close());
  EXPECT_EQ(close_requests, 3);
  EXPECT_FALSE(main_window.isVisible());
}

} // namespace
} // namespace quader
