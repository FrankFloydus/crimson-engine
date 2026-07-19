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
#include "editor/modeling/services/modeling_session_service.hpp"
#include "ui/inspector_panel.hpp"
#include "ui/qt_color_swatch_button.hpp"
#include "ui/qt_main_menu.hpp"
#include "ui/qt_main_window.hpp"
#include "ui/qt_settings_dialog.hpp"
#include "ui/ui_context.hpp"
#include "ui/ui_settings.hpp"
#include "viewport/viewport_settings.hpp"

#include <QApplication>
#include <QAbstractItemView>
#include <QAction>
#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QFocusEvent>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QStringList>
#include <QTableView>
#include <QTimer>
#include <QTreeView>
#include <QWheelEvent>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#ifndef QUADER_SOURCE_DIR
#define QUADER_SOURCE_DIR "."
#endif

namespace quader {
namespace {

/// Restores Qt's process-default locale after a production-locale test.
class ScopedDefaultLocale final {
public:
  explicit ScopedDefaultLocale(const QLocale &locale) : previous_(QLocale()) {
    QLocale::setDefault(locale);
  }

  ~ScopedDefaultLocale() { QLocale::setDefault(previous_); }

  ScopedDefaultLocale(const ScopedDefaultLocale &) = delete;
  ScopedDefaultLocale &operator=(const ScopedDefaultLocale &) = delete;

private:
  QLocale previous_;
};

int expected_natural_numeric_compare(const QString &left,
                                     const QString &right) {
  qsizetype left_index = 0;
  qsizetype right_index = 0;
  while (left_index < left.size() && right_index < right.size()) {
    if (left.at(left_index).isDigit() && right.at(right_index).isDigit()) {
      const qsizetype left_start = left_index;
      const qsizetype right_start = right_index;
      while (left_index < left.size() && left.at(left_index).isDigit()) {
        ++left_index;
      }
      while (right_index < right.size() && right.at(right_index).isDigit()) {
        ++right_index;
      }
      qsizetype left_number = left_start;
      qsizetype right_number = right_start;
      while (left_number < left_index &&
             left.at(left_number).digitValue() == 0) {
        ++left_number;
      }
      while (right_number < right_index &&
             right.at(right_number).digitValue() == 0) {
        ++right_number;
      }
      const qsizetype left_length = left_index - left_number;
      const qsizetype right_length = right_index - right_number;
      if (left_length != right_length) {
        return left_length < right_length ? -1 : 1;
      }
      for (qsizetype offset = 0; offset < left_length; ++offset) {
        const int left_digit = left.at(left_number + offset).digitValue();
        const int right_digit = right.at(right_number + offset).digitValue();
        if (left_digit != right_digit) {
          return left_digit < right_digit ? -1 : 1;
        }
      }
      const qsizetype left_run_length = left_index - left_start;
      const qsizetype right_run_length = right_index - right_start;
      if (left_run_length != right_run_length) {
        return left_run_length < right_run_length ? -1 : 1;
      }
      continue;
    }

    const ushort left_character = left.at(left_index).toCaseFolded().unicode();
    const ushort right_character =
        right.at(right_index).toCaseFolded().unicode();
    if (left_character != right_character) {
      return left_character < right_character ? -1 : 1;
    }
    ++left_index;
    ++right_index;
  }
  if (left_index == left.size() && right_index == right.size()) {
    return 0;
  }
  return left_index == left.size() ? -1 : 1;
}

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

std::string read_text_file(const std::filesystem::path &path) {
  std::ifstream file(path, std::ios::binary);
  std::ostringstream stream;
  stream << file.rdbuf();
  return stream.str();
}

const QuaderKeybindingCatalogEntry *find_keybinding_catalog_entry(
    const std::vector<QuaderKeybindingCatalogEntry> &entries,
    std::string_view target) {
  const auto found = std::find_if(
      entries.begin(), entries.end(), [&](const auto &entry) {
        return std::find(entry.target_ids.begin(), entry.target_ids.end(),
                         target) != entry.target_ids.end();
      });
  return found != entries.end() ? &*found : nullptr;
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

bool capture_keybinding_and_choose_conflict(
    QWidget &owner, QTableView &table, QString choice_object_name,
    QString conflicting_action, bool returns_to_capture) {
  /// Tracks whether both nested dialogs were observed during the click.
  struct Outcome {
    bool capture_opened = false;
    bool conflict_opened = false;
  };
  const auto outcome = std::make_shared<Outcome>();
  QTimer::singleShot(
      0, &owner,
      [&owner, outcome, choice_object_name, conflicting_action,
       returns_to_capture]() {
        QWidget *modal = QApplication::activeModalWidget();
        if (modal == nullptr) {
          ADD_FAILURE() << "Expected the keybinding capture dialog to open.";
          return;
        }
        outcome->capture_opened = true;
        auto *recorder = modal->findChild<QLineEdit *>(
            QStringLiteral("settingsKeybindingCaptureRecorder"));
        auto *use = modal->findChild<QPushButton *>(
            QStringLiteral("settingsKeybindingCaptureUse"));
        if (recorder == nullptr || use == nullptr) {
          ADD_FAILURE() << "Expected capture controls.";
          modal->close();
          return;
        }
        send_key(*recorder, Qt::Key_W);
        QTimer::singleShot(
            0, &owner,
            [outcome, choice_object_name, conflicting_action,
             returns_to_capture]() {
                         QWidget *conflict =
                             QApplication::activeModalWidget();
                         auto *box = qobject_cast<QMessageBox *>(conflict);
                         if (box == nullptr) {
                           ADD_FAILURE()
                               << "Expected the keybinding conflict dialog.";
                           return;
                         }
                         outcome->conflict_opened = true;
                         EXPECT_EQ(
                             box->text(),
                             QStringLiteral(
                                 "This input mapping clashes with “%1”.")
                                 .arg(conflicting_action));
                         EXPECT_EQ(box->findChild<QPushButton *>(
                                       QStringLiteral(
                                           "settingsKeybindingConflictKeepBoth")),
                                   nullptr);
                         ASSERT_NE(box->defaultButton(), nullptr);
                         EXPECT_EQ(
                             box->defaultButton()->objectName(),
                             QStringLiteral(
                                 "settingsKeybindingConflictCancel"));
                         auto *choice = box->findChild<QPushButton *>(
                             choice_object_name);
                         if (choice == nullptr) {
                           ADD_FAILURE() << "Expected conflict choice button.";
                           box->reject();
                           return;
                         }
                         if (returns_to_capture) {
                           QTimer::singleShot(0, [outcome]() {
                             QWidget *capture =
                                 QApplication::activeModalWidget();
                             ASSERT_NE(capture, nullptr);
                             EXPECT_EQ(capture->objectName(),
                                       QStringLiteral(
                                           "settingsKeybindingCaptureDialog"));
                             auto *recorder = capture->findChild<QLineEdit *>(
                                 QStringLiteral(
                                     "settingsKeybindingCaptureRecorder"));
                             ASSERT_NE(recorder, nullptr);
                             EXPECT_EQ(recorder->text(), QStringLiteral("W"));
                             qobject_cast<QDialog *>(capture)->reject();
                           });
                         }
                         choice->click();
            });
        use->click();
      });
  const QModelIndex mapping =
      table.model()->index(table.currentIndex().row(), 2);
  QMetaObject::invokeMethod(&table, "doubleClicked", Qt::DirectConnection,
                            Q_ARG(QModelIndex, mapping));
  return outcome->capture_opened && outcome->conflict_opened;
}

bool choose_reset_all_confirmation(QWidget &owner, QPushButton &reset_all,
                                   QString choice_object_name) {
  bool opened = false;
  QTimer::singleShot(0, &owner, [&]() {
    auto *confirmation = qobject_cast<QMessageBox *>(
        QApplication::activeModalWidget());
    if (confirmation == nullptr) {
      ADD_FAILURE() << "Expected the Reset All confirmation dialog.";
      return;
    }
    opened = true;
    EXPECT_EQ(confirmation->objectName(),
              QStringLiteral("settingsKeybindingResetAllDialog"));
    auto *button =
        confirmation->findChild<QPushButton *>(choice_object_name);
    ASSERT_NE(button, nullptr);
    button->click();
  });
  reset_all.click();
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
  SceneSession settings_session;
  QuaderEditorApi settings_editor(
      QuaderApiSessionInternalAccess::from_scene_session(settings_session));
  dialog.set_context(
      ui_settings, viewport_settings, localization, settings_editor.commands(),
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
  auto *default_fly_speed = dialog.findChild<QDoubleSpinBox *>(
      QStringLiteral("viewportSetting_DefaultFlyNavigationSpeed"));
  auto *bounding_box_thickness = dialog.findChild<QDoubleSpinBox *>(
      QStringLiteral("viewportSetting_BoundingBoxWireThickness"));
  auto *show_grid = dialog.findChild<QCheckBox *>(
      QStringLiteral("viewportSetting_ShowGrid"));
  auto *show_info = dialog.findChild<QCheckBox *>(
      QStringLiteral("viewportSetting_ShowInfo"));
  auto *locale_combo =
      dialog.findChild<QComboBox *>(QStringLiteral("settingsLocale"));
  auto *mesh_major_swatch = dialog.findChild<QPushButton *>(
      QStringLiteral("viewportSetting_MeshGridMajorColor"));
  auto *instance_background_swatch = dialog.findChild<QPushButton *>(
      QStringLiteral("viewportSetting_InstanceEditBackgroundColor"));
  auto *instance_context_swatch = dialog.findChild<QPushButton *>(
      QStringLiteral("viewportSetting_InstanceEditContextColor"));
  auto *bounding_box_swatch = dialog.findChild<QPushButton *>(
      QStringLiteral("viewportSetting_BoundingBoxWireColor"));

  ASSERT_NE(category_list, nullptr);
  ASSERT_NE(button_box, nullptr);
  ASSERT_NE(apply_button, nullptr);
  ASSERT_NE(ok_button, nullptr);
  ASSERT_NE(cancel_button, nullptr);
  ASSERT_NE(thickness, nullptr);
  ASSERT_NE(default_fly_speed, nullptr);
  ASSERT_NE(bounding_box_thickness, nullptr);
  ASSERT_NE(show_grid, nullptr);
  ASSERT_NE(show_info, nullptr);
  ASSERT_NE(locale_combo, nullptr);
  ASSERT_NE(mesh_major_swatch, nullptr);
  ASSERT_NE(instance_background_swatch, nullptr);
  ASSERT_NE(instance_context_swatch, nullptr);
  ASSERT_NE(bounding_box_swatch, nullptr);
  EXPECT_DOUBLE_EQ(default_fly_speed->value(), 25.0);
  EXPECT_DOUBLE_EQ(bounding_box_thickness->value(), 1.5);
  EXPECT_EQ(bounding_box_swatch->accessibleName(),
            QStringLiteral("Bounding Box Wire Color"));
  EXPECT_EQ(dialog.findChild<QPushButton *>(
                QStringLiteral("settingsResetDefaults")),
            nullptr);

  int grid_row = -1;
  int navigation_row = -1;
  for (int row = 0; row < category_list->count(); ++row) {
    const QString stable_id =
        category_list->item(row)->data(Qt::UserRole).toString();
    if (stable_id == QStringLiteral("grid")) {
      grid_row = row;
    } else if (stable_id == QStringLiteral("navigation")) {
      navigation_row = row;
    }
  }
  ASSERT_GE(grid_row, 0);
  ASSERT_GE(navigation_row, 0);
  EXPECT_EQ(category_list->item(navigation_row)->text(),
            QStringLiteral("Navigation"));
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
  const ViewportSettingColor initial_bounding_box =
      viewport_settings.color(ViewportColorSettingId::BoundingBoxWire);
  const QColor applied_mesh_major = QColor::fromRgb(46, 92, 138, 184);
  ASSERT_TRUE(accept_color_picker(
      dialog, *mesh_major_swatch,
      QStringLiteral("viewportSetting_MeshGridMajorColor_dialog"),
      applied_mesh_major));
  const QColor applied_bounding_box = QColor::fromRgb(26, 77, 128, 230);
  ASSERT_TRUE(accept_color_picker(
      dialog, *bounding_box_swatch,
      QStringLiteral("viewportSetting_BoundingBoxWireColor_dialog"),
      applied_bounding_box));

  thickness->setValue(1.75);
  default_fly_speed->setValue(35.0);
  bounding_box_thickness->setValue(4.25);
  show_grid->setChecked(false);
  locale_combo->setCurrentIndex(italian_index);
  EXPECT_TRUE(apply_button->isEnabled());
  EXPECT_FLOAT_EQ(viewport_settings.float_value(
                      ViewportFloatSettingId::GridMajorLineSize),
                  initial_thickness);
  EXPECT_FLOAT_EQ(viewport_settings.float_value(
                      ViewportFloatSettingId::DefaultFlyNavigationSpeed),
                  25.0f);
  EXPECT_FLOAT_EQ(viewport_settings.float_value(
                      ViewportFloatSettingId::BoundingBoxWireThickness),
                  1.5f);
  EXPECT_TRUE(viewport_settings.bool_value(ViewportBoolSettingId::ShowGrid));
  EXPECT_EQ(ui_settings.locale(), "en-US");
  EXPECT_EQ(localization.current_locale(), "en-US");
  EXPECT_TRUE(viewport_setting_colors_approximately(
      viewport_settings.color(ViewportColorSettingId::MeshGridMajor),
      initial_mesh_major));
  EXPECT_TRUE(viewport_setting_colors_approximately(
      viewport_settings.color(ViewportColorSettingId::BoundingBoxWire),
      initial_bounding_box));
  EXPECT_TRUE(changes.empty());

  apply_button->click();
  QCoreApplication::processEvents();
  EXPECT_TRUE(dialog.isVisible());
  EXPECT_FALSE(apply_button->isEnabled());
  EXPECT_FLOAT_EQ(viewport_settings.float_value(
                      ViewportFloatSettingId::GridMajorLineSize),
                  1.75f);
  EXPECT_FLOAT_EQ(viewport_settings.float_value(
                      ViewportFloatSettingId::DefaultFlyNavigationSpeed),
                  35.0f);
  EXPECT_FLOAT_EQ(viewport_settings.float_value(
                      ViewportFloatSettingId::BoundingBoxWireThickness),
                  4.25f);
  EXPECT_FALSE(viewport_settings.bool_value(ViewportBoolSettingId::ShowGrid));
  EXPECT_EQ(ui_settings.locale(), "it-IT");
  EXPECT_EQ(localization.current_locale(), "it-IT");
  EXPECT_TRUE(viewport_setting_colors_approximately(
      viewport_settings.color(ViewportColorSettingId::MeshGridMajor),
      setting_color(applied_mesh_major)));
  EXPECT_TRUE(viewport_setting_colors_approximately(
      viewport_settings.color(ViewportColorSettingId::BoundingBoxWire),
      setting_color(applied_bounding_box)));
  EXPECT_EQ(count_changes(changes, QtSettingsChangeKind::ViewportCommit), 1u);
  EXPECT_EQ(count_changes(changes, QtSettingsChangeKind::InterfaceCommit), 1u);

  thickness->setValue(2.5);
  default_fly_speed->setValue(80.0);
  bounding_box_thickness->setValue(7.0);
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
  EXPECT_FLOAT_EQ(viewport_settings.float_value(
                      ViewportFloatSettingId::DefaultFlyNavigationSpeed),
                  35.0f);
  EXPECT_FLOAT_EQ(viewport_settings.float_value(
                      ViewportFloatSettingId::BoundingBoxWireThickness),
                  4.25f);
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

TEST(QtSettingsDialogTests,
     KeybindingEditorListsEveryCatalogRowByContext) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-settings-keybindings-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  const ScopedDefaultLocale force_c_locale(QLocale::c());

  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const std::filesystem::path keybindings =
      std::filesystem::temp_directory_path() /
      "quader-settings-keybindings-showcase.json";
  std::error_code ignored;
  std::filesystem::remove(keybindings, ignored);
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path(keybindings).ok());
  ASSERT_TRUE(editor.preferences()
                  .set_shortcut(quader_command::kEditUndo, "Ctrl+Alt+Z")
                  .ok());

  UISettings ui_settings = UISettings::defaults();
  ViewportSettings viewport_settings = ViewportSettings::defaults();
  LocalizationService localization = loaded_localization();
  std::vector<QtSettingsChangeKind> changes;
  QtSettingsDialog dialog;
  dialog.setAttribute(Qt::WA_DontShowOnScreen, true);
  dialog.set_context(
      ui_settings, viewport_settings, localization, editor.commands(),
      [&](QtSettingsChangeKind kind) { changes.push_back(kind); });

  auto *category_list =
      dialog.findChild<QListWidget *>(QStringLiteral("settingsCategories"));
  auto *table = dialog.findChild<QTableView *>(
      QStringLiteral("settingsKeybindingsTable"));
  auto *search = dialog.findChild<QLineEdit *>(
      QStringLiteral("settingsKeybindingsSearch"));
  auto *shortcut_filter = dialog.findChild<QLineEdit *>(
      QStringLiteral("settingsKeybindingsShortcutFilter"));
  auto *shortcut_clear = dialog.findChild<QPushButton *>(
      QStringLiteral("settingsKeybindingsShortcutClear"));
  auto *apply_button =
      dialog.findChild<QPushButton *>(QStringLiteral("settingsApply"));
  auto *locale_combo =
      dialog.findChild<QComboBox *>(QStringLiteral("settingsLocale"));
  auto *reset_action = dialog.findChild<QPushButton *>(
      QStringLiteral("settingsKeybindingsResetAction"));
  auto *reset_all = dialog.findChild<QPushButton *>(
      QStringLiteral("settingsKeybindingsResetAll"));
  ASSERT_NE(category_list, nullptr);
  ASSERT_NE(table, nullptr);
  ASSERT_NE(search, nullptr);
  ASSERT_NE(shortcut_filter, nullptr);
  ASSERT_NE(shortcut_clear, nullptr);
  ASSERT_NE(table->model(), nullptr);
  ASSERT_NE(apply_button, nullptr);
  ASSERT_NE(locale_combo, nullptr);
  EXPECT_EQ(dialog.findChild<QLabel *>(
                QStringLiteral("settingsKeybindingsSaveHint")),
            nullptr);
  EXPECT_EQ(dialog.findChild<QLabel *>(
                QStringLiteral("settingsKeybindingsRebindHint")),
            nullptr);
  EXPECT_EQ(dialog.findChild<QLabel *>(
                QStringLiteral("settingsKeybindingsSelectedAction")),
            nullptr);
  EXPECT_EQ(dialog.findChild<QLabel *>(
                QStringLiteral("settingsKeybindingsDefaultValue")),
            nullptr);
  EXPECT_EQ(dialog.findChild<QLabel *>(
                QStringLiteral("settingsKeybindingsCurrentValue")),
            nullptr);
  EXPECT_EQ(dialog.findChild<QLabel *>(
                QStringLiteral("settingsKeybindingsWhenValue")),
            nullptr);
  EXPECT_EQ(dialog.findChild<QListWidget *>(
                QStringLiteral("settingsKeybindingsCurrentMappings")),
            nullptr);
  EXPECT_EQ(dialog.findChild<QPushButton *>(
                QStringLiteral("settingsKeybindingsAdd")),
            nullptr);
  EXPECT_EQ(dialog.findChild<QPushButton *>(
                QStringLiteral("settingsKeybindingsReplace")),
            nullptr);
  EXPECT_EQ(dialog.findChild<QPushButton *>(
                QStringLiteral("settingsKeybindingsRemove")),
            nullptr);
  ASSERT_NE(reset_action, nullptr);
  ASSERT_NE(reset_all, nullptr);

  QListWidgetItem *keybindings_category = nullptr;
  for (int row = 0; row < category_list->count(); ++row) {
    if (category_list->item(row)->data(Qt::UserRole).toString() ==
        QStringLiteral("keybindings")) {
      keybindings_category = category_list->item(row);
      category_list->setCurrentRow(row);
      break;
    }
  }
  ASSERT_NE(keybindings_category, nullptr);
  EXPECT_EQ(keybindings_category->text(), QStringLiteral("Keybindings"));
  EXPECT_TRUE(table->alternatingRowColors());
  QTableView stock_table;
  EXPECT_EQ(table->editTriggers(), stock_table.editTriggers());
  EXPECT_EQ(table->selectionBehavior(), QAbstractItemView::SelectRows);
  EXPECT_EQ(table->selectionMode(), QAbstractItemView::SingleSelection);
  EXPECT_LE(table->verticalHeader()->defaultSectionSize(), 20);
  EXPECT_EQ(table->horizontalHeader()->sectionResizeMode(0),
            QHeaderView::Interactive);
  EXPECT_EQ(table->horizontalHeader()->sectionResizeMode(1),
            QHeaderView::Interactive);
  EXPECT_EQ(table->horizontalHeader()->sectionResizeMode(2),
            QHeaderView::Interactive);
  EXPECT_TRUE(table->horizontalHeader()->stretchLastSection());
  EXPECT_EQ(table->horizontalHeader()->sortIndicatorSection(), 0);
  EXPECT_EQ(table->horizontalHeader()->sortIndicatorOrder(),
            Qt::AscendingOrder);
  EXPECT_TRUE(shortcut_filter->isReadOnly());

  QAbstractItemModel *model = table->model();
  ASSERT_EQ(model->columnCount(), 3);
  EXPECT_EQ(model->headerData(0, Qt::Horizontal).toString(),
            QStringLiteral("Command Name"));
  EXPECT_EQ(model->headerData(1, Qt::Horizontal).toString(),
            QStringLiteral("Context"));
  EXPECT_EQ(model->headerData(2, Qt::Horizontal).toString(),
            QStringLiteral("Input Mapping"));

  const std::vector<QuaderKeybindingCatalogEntry> catalog =
      editor.commands().keybinding_catalog();
  ASSERT_EQ(model->rowCount(), static_cast<int>(catalog.size()));
  std::multiset<std::string> expected_rows;
  for (const QuaderKeybindingCatalogEntry &entry : catalog) {
    expected_rows.insert(entry.action_id + "|" +
                         (entry.shortcut.empty() ? "Unassigned"
                                                 : entry.shortcut));
  }
  std::multiset<std::string> actual_rows;
  for (int row = 0; row < model->rowCount(); ++row) {
    const QModelIndex command_index = model->index(row, 0);
    const QModelIndex context_index = model->index(row, 1);
    const QModelIndex mapping_index = model->index(row, 2);
    const std::string action_id =
        model->data(command_index, Qt::UserRole).toString().toStdString();
    const std::string mapping =
        model->data(mapping_index, Qt::DisplayRole).toString().toStdString();
    actual_rows.insert(action_id + "|" + mapping);
    EXPECT_FALSE(model->data(command_index).toString().isEmpty()) << action_id;
    EXPECT_FALSE(model->data(context_index).toString().isEmpty()) << action_id;
    EXPECT_FALSE(model->data(context_index, Qt::ToolTipRole).isValid())
        << action_id;
    EXPECT_EQ(model->data(context_index, Qt::UserRole).toString(),
              QString::fromStdString(action_id));
    EXPECT_EQ(model->data(mapping_index, Qt::UserRole).toString(),
              QString::fromStdString(action_id));
  }
  EXPECT_EQ(actual_rows, expected_rows);

  const auto expect_column_order = [&](int column, Qt::SortOrder order) {
    for (int row = 1; row < model->rowCount(); ++row) {
      const QString previous = model->data(model->index(row - 1, column)).toString();
      const QString current = model->data(model->index(row, column)).toString();
      const int compared =
          expected_natural_numeric_compare(previous, current);
      if (order == Qt::AscendingOrder) {
        EXPECT_LE(compared, 0) << column << ":" << row;
      } else {
        EXPECT_GE(compared, 0) << column << ":" << row;
      }
    }
  };
  expect_column_order(0, Qt::AscendingOrder);
  for (int column = 0; column < model->columnCount(); ++column) {
    table->sortByColumn(column, Qt::DescendingOrder);
    expect_column_order(column, Qt::DescendingOrder);
    table->sortByColumn(column, Qt::AscendingOrder);
    expect_column_order(column, Qt::AscendingOrder);
  }
  table->sortByColumn(0, Qt::AscendingOrder);

  QStringList grid_size_labels;
  for (int row = 0; row < model->rowCount(); ++row) {
    const QString label = model->data(model->index(row, 0)).toString();
    if (label.startsWith(QStringLiteral("Set Grid Size to "))) {
      grid_size_labels.push_back(label);
    }
  }
  EXPECT_EQ(grid_size_labels,
            QStringList({QStringLiteral("Set Grid Size to 1"),
                         QStringLiteral("Set Grid Size to 2"),
                         QStringLiteral("Set Grid Size to 4"),
                         QStringLiteral("Set Grid Size to 8"),
                         QStringLiteral("Set Grid Size to 16"),
                         QStringLiteral("Set Grid Size to 32 (Default)"),
                         QStringLiteral("Set Grid Size to 64"),
                         QStringLiteral("Set Grid Size to 128"),
                         QStringLiteral("Set Grid Size to 256"),
                         QStringLiteral("Set Grid Size to 512")}));

  const auto find_row = [&](std::string_view target_id,
                            std::string_view shortcut) {
    for (int row = 0; row < model->rowCount(); ++row) {
      if (model->data(model->index(row, 0), Qt::UserRole).toString() ==
              QString::fromUtf8(target_id.data(),
                                static_cast<qsizetype>(target_id.size())) &&
          model->data(model->index(row, 2)).toString() ==
              QString::fromUtf8(shortcut.data(),
                                static_cast<qsizetype>(shortcut.size()))) {
        return row;
      }
    }
    return -1;
  };

  const QString undo_target = QString::fromUtf8(
      quader_command::kEditUndo.string().data(),
      static_cast<qsizetype>(quader_command::kEditUndo.string().size()));
  const int customized_undo_row =
      find_row(quader_command::kEditUndo.string(), "Ctrl+Alt+Z");
  ASSERT_GE(customized_undo_row, 0);
  table->selectRow(customized_undo_row);
  QCoreApplication::processEvents();
  ASSERT_EQ(table->selectionModel()->selectedRows(0).size(), 1);
  EXPECT_EQ(table->selectionModel()->selectedRows(0).front().data(
                Qt::UserRole),
            undo_target);

  const QString redo_target = QString::fromUtf8(
      quader_command::kEditRedo.string().data(),
      static_cast<qsizetype>(quader_command::kEditRedo.string().size()));
  int redo_row = -1;
  for (int row = 0; row < model->rowCount(); ++row) {
    if (model->index(row, 0).data(Qt::UserRole).toString() == redo_target) {
      redo_row = row;
      break;
    }
  }
  ASSERT_GE(redo_row, 0);
  table->selectionModel()->select(
      model->index(redo_row, 0),
      QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
  ASSERT_TRUE(table->currentIndex().isValid());
  EXPECT_EQ(table->currentIndex().data(Qt::UserRole), undo_target);
  ASSERT_EQ(table->selectionModel()->selectedRows(0).size(), 1);
  EXPECT_EQ(table->selectionModel()->selectedRows(0).front().data(
                Qt::UserRole),
            redo_target)
      << "selection and current are independent Qt item-view state";

  table->sortByColumn(2, Qt::DescendingOrder);
  QCoreApplication::processEvents();
  const QModelIndexList selected_after_sort =
      table->selectionModel()->selectedRows(0);
  ASSERT_EQ(selected_after_sort.size(), 1)
      << "layout-based sorting must preserve the Qt selection";
  EXPECT_EQ(selected_after_sort.front().data(Qt::UserRole), redo_target);
  ASSERT_TRUE(table->currentIndex().isValid());
  EXPECT_EQ(table->currentIndex().data(Qt::UserRole), undo_target);

  table->sortByColumn(0, Qt::AscendingOrder);
  QCoreApplication::processEvents();
  int layout_changed_count = 0;
  int data_changed_count = 0;
  QObject::connect(model, &QAbstractItemModel::layoutChanged, &dialog,
                   [&layout_changed_count]() { ++layout_changed_count; });
  QObject::connect(model, &QAbstractItemModel::dataChanged, &dialog,
                   [&data_changed_count]() { ++data_changed_count; });
  ASSERT_TRUE(editor.preferences()
                  .set_shortcut(quader_command::kEditUndo, "Ctrl+Alt+F18")
                  .ok());
  dialog.refresh();
  QCoreApplication::processEvents();
  EXPECT_EQ(layout_changed_count, 0)
      << "same-order cell updates must not masquerade as model layout changes";
  EXPECT_GT(data_changed_count, 0);
  ASSERT_EQ(table->selectionModel()->selectedRows(0).size(), 1);
  EXPECT_EQ(table->selectionModel()->selectedRows(0).front().data(
                Qt::UserRole),
            redo_target);
  EXPECT_EQ(table->currentIndex().data(Qt::UserRole), redo_target)
      << "the dialog restores its selected singular action after refresh";
  ASSERT_TRUE(editor.preferences()
                  .set_shortcut(quader_command::kEditUndo, "Ctrl+Alt+Z")
                  .ok());
  dialog.refresh();
  QCoreApplication::processEvents();

  table->selectionModel()->clearSelection();
  QCoreApplication::processEvents();
  ASSERT_TRUE(table->currentIndex().isValid())
      << "Qt keeps the current index independently from selection";
  EXPECT_EQ(table->currentIndex().data(Qt::UserRole), redo_target);
  EXPECT_TRUE(table->selectionModel()->selectedRows(0).isEmpty());
  EXPECT_FALSE(reset_action->isEnabled());

  bool unexpected_rebind_dialog = false;
  QTimer::singleShot(0, &dialog, [&unexpected_rebind_dialog]() {
    if (QWidget *modal = QApplication::activeModalWidget();
        modal != nullptr) {
      unexpected_rebind_dialog = true;
      modal->close();
    }
  });
  send_key(*table, Qt::Key_Return);
  QCoreApplication::processEvents();
  EXPECT_FALSE(unexpected_rebind_dialog)
      << "Enter must not rebind a merely-current, unselected row";

  const std::size_t changes_before_unselected_reset = changes.size();
  reset_action->setEnabled(true);
  reset_action->click();
  QCoreApplication::processEvents();
  EXPECT_EQ(changes.size(), changes_before_unselected_reset);
  const auto catalog_after_unselected_reset =
      editor.commands().keybinding_catalog();
  const QuaderKeybindingCatalogEntry *undo_after_unselected_reset =
      find_keybinding_catalog_entry(catalog_after_unselected_reset,
                                    quader_command::kEditUndo.string());
  ASSERT_NE(undo_after_unselected_reset, nullptr);
  EXPECT_EQ(undo_after_unselected_reset->shortcut, "Ctrl+Alt+Z");
  EXPECT_TRUE(undo_after_unselected_reset->customized)
      << "reset must be a no-op without an actual selected row";

  EXPECT_GE(find_row(quader_command::kEditUndo.string(), "Ctrl+Alt+Z"), 0);
  EXPECT_GE(
      find_row(quader_command::kEditRedo.string(), "Ctrl+Shift+Z"), 0);
  EXPECT_GE(find_row(quader_command::kDeleteSelection.string(), "Delete"),
            0);
  EXPECT_GE(find_row("viewport.navigation.orbit", "MiddleMouseDrag"), 0);
  EXPECT_GE(
      find_row(editor.commands().keybinding_target_id_for_selection_mode(
                   QuaderSelectionMode::Vertex),
               "1"),
      0);
  EXPECT_GE(find_row("tools.finalize", "Space"), 0);
  EXPECT_GE(find_row(quader_command::kEditCreateInstance.string(),
                     "Unassigned"),
            0);

  const int finalize_row = find_row("tools.finalize", "Space");
  ASSERT_GE(finalize_row, 0);
  EXPECT_EQ(model->data(model->index(finalize_row, 0)).toString(),
            QStringLiteral("Finalize Tool"));
  EXPECT_EQ(model->data(model->index(finalize_row, 1)).toString(),
            QStringLiteral("Viewport › Modal Tool"));

  search->setText(QStringLiteral("Redo"));
  QCoreApplication::processEvents();
  ASSERT_EQ(model->rowCount(), 1);
  for (int row = 0; row < model->rowCount(); ++row) {
    EXPECT_TRUE(model->data(model->index(row, 0)).toString().contains(
        QStringLiteral("Redo"), Qt::CaseInsensitive));
  }
  EXPECT_FALSE(apply_button->isEnabled());
  EXPECT_TRUE(changes.empty());

  search->clear();
  QKeyEvent shortcut_event(QEvent::KeyPress, Qt::Key_Z,
                           Qt::ControlModifier | Qt::ShiftModifier);
  QApplication::sendEvent(shortcut_filter, &shortcut_event);
  QCoreApplication::processEvents();
  EXPECT_EQ(shortcut_filter->text(), QStringLiteral("Ctrl+Shift+Z"));
  ASSERT_EQ(model->rowCount(), 1);
  EXPECT_TRUE(model->data(model->index(0, 2)).toString().contains(
      QStringLiteral("Ctrl+Shift+Z")));
  EXPECT_FALSE(apply_button->isEnabled());
  EXPECT_TRUE(changes.empty());

  shortcut_clear->click();
  QCoreApplication::processEvents();
  EXPECT_TRUE(shortcut_filter->text().isEmpty());
  EXPECT_EQ(model->rowCount(), static_cast<int>(catalog.size()));

  dialog.show();
  QCoreApplication::processEvents();
  table->selectionModel()->clearSelection();
  QCoreApplication::processEvents();
  const QModelIndex first_row_after_filter = model->index(0, 0);
  ASSERT_TRUE(first_row_after_filter.isValid());
  table->selectionModel()->select(
      first_row_after_filter,
      QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
  QCoreApplication::processEvents();
  ASSERT_EQ(table->selectionModel()->selectedRows(0).size(), 1);
  EXPECT_TRUE(reset_action->isEnabled());
  EXPECT_TRUE(reset_all->isEnabled());
  EXPECT_FALSE(apply_button->isEnabled());
  EXPECT_TRUE(changes.empty());

  const int italian_index = locale_combo->findData(QStringLiteral("it-IT"));
  ASSERT_GE(italian_index, 0);
  locale_combo->setCurrentIndex(italian_index);
  ASSERT_TRUE(apply_button->isEnabled());
  apply_button->click();
  QCoreApplication::processEvents();
  EXPECT_EQ(keybindings_category->text(),
            QStringLiteral("Associazioni tasti"));
  EXPECT_EQ(model->headerData(0, Qt::Horizontal).toString(),
            QStringLiteral("Nome comando"));
  EXPECT_EQ(model->headerData(1, Qt::Horizontal).toString(),
            QStringLiteral("Contesto"));
  EXPECT_EQ(model->headerData(2, Qt::Horizontal).toString(),
            QStringLiteral("Mappatura input"));
  const int italian_finalize_row = find_row("tools.finalize", "Space");
  ASSERT_GE(italian_finalize_row, 0);
  EXPECT_EQ(model->data(model->index(italian_finalize_row, 1)).toString(),
            QStringLiteral("Viewport › Strumento modale"));
  std::filesystem::remove(keybindings, ignored);
}

TEST(QtSettingsDialogTests,
     KeybindingEditorCapturesMixedSequenceAndPersistsImmediately) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-settings-keybinding-edit-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const std::filesystem::path keybindings =
      std::filesystem::temp_directory_path() /
      "quader-settings-keybinding-editor.json";
  std::error_code ignored;
  std::filesystem::remove(keybindings, ignored);
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path(keybindings).ok());

  UISettings ui_settings = UISettings::defaults();
  ViewportSettings viewport_settings = ViewportSettings::defaults();
  LocalizationService localization = loaded_localization();
  std::vector<QtSettingsChangeKind> changes;
  QtSettingsDialog dialog;
  dialog.setAttribute(Qt::WA_DontShowOnScreen, true);
  dialog.set_context(
      ui_settings, viewport_settings, localization, editor.commands(),
      [&](QtSettingsChangeKind kind) { changes.push_back(kind); });
  dialog.show();
  QCoreApplication::processEvents();

  auto *table = dialog.findChild<QTableView *>(
      QStringLiteral("settingsKeybindingsTable"));
  auto *reset_action = dialog.findChild<QPushButton *>(
      QStringLiteral("settingsKeybindingsResetAction"));
  auto *apply =
      dialog.findChild<QPushButton *>(QStringLiteral("settingsApply"));
  ASSERT_NE(table, nullptr);
  ASSERT_NE(table->model(), nullptr);
  ASSERT_NE(reset_action, nullptr);
  ASSERT_NE(apply, nullptr);

  int undo_row = -1;
  for (int row = 0; row < table->model()->rowCount(); ++row) {
    if (table->model()
            ->data(table->model()->index(row, 0), Qt::UserRole)
            .toString() ==
        QString::fromUtf8(quader_command::kEditUndo.string().data(),
                          static_cast<qsizetype>(
                              quader_command::kEditUndo.string().size()))) {
      undo_row = row;
      break;
    }
  }
  ASSERT_GE(undo_row, 0);
  table->selectRow(undo_row);
  QCoreApplication::processEvents();

  QTimer::singleShot(0, &dialog, []() {
    QWidget *modal = QApplication::activeModalWidget();
    ASSERT_NE(modal, nullptr);
    auto *recorder = modal->findChild<QLineEdit *>(
        QStringLiteral("settingsKeybindingCaptureRecorder"));
    auto *use = modal->findChild<QPushButton *>(
        QStringLiteral("settingsKeybindingCaptureUse"));
    auto *clear = modal->findChild<QPushButton *>(
        QStringLiteral("settingsKeybindingCaptureClear"));
    auto *unassign = modal->findChild<QPushButton *>(
        QStringLiteral("settingsKeybindingCaptureUnassign"));
    ASSERT_NE(recorder, nullptr);
    ASSERT_NE(use, nullptr);
    ASSERT_NE(clear, nullptr);
    ASSERT_NE(unassign, nullptr);
    EXPECT_EQ(clear->text(), QStringLiteral("Clear Capture"));
    EXPECT_TRUE(unassign->isEnabled());

    send_key(*recorder, Qt::Key_K, Qt::ControlModifier);
    EXPECT_TRUE(clear->isEnabled());
    clear->click();
    EXPECT_TRUE(recorder->text().isEmpty());
    EXPECT_FALSE(use->isEnabled());
    send_key(*recorder, Qt::Key_K, Qt::ControlModifier);
    send_key(*recorder, Qt::Key_Escape);
    send_key(*recorder, Qt::Key_Return);
    send_key(*recorder, Qt::Key_Tab);
    send_key(*recorder, Qt::Key_Backspace);
    send_key(*recorder, Qt::Key_Delete);
    QWheelEvent wheel(QPointF{12.0, 12.0}, QPointF{112.0, 112.0},
                      QPoint{}, QPoint{0, 120}, Qt::NoButton,
                      Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(recorder, &wheel);

    QMouseEvent double_click(
        QEvent::MouseButtonDblClick, QPointF{12.0, 12.0},
        QPointF{112.0, 112.0}, Qt::ExtraButton4, Qt::ExtraButton4,
        Qt::NoModifier);
    QCoreApplication::sendEvent(recorder, &double_click);

    QMouseEvent press(QEvent::MouseButtonPress, QPointF{8.0, 8.0},
                      QPointF{108.0, 108.0}, Qt::ExtraButton3,
                      Qt::ExtraButton3, Qt::NoModifier);
    QMouseEvent move(QEvent::MouseMove, QPointF{80.0, 8.0},
                     QPointF{180.0, 108.0}, Qt::NoButton,
                     Qt::ExtraButton3, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF{80.0, 8.0},
                        QPointF{180.0, 108.0}, Qt::ExtraButton3,
                        Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(recorder, &press);
    QCoreApplication::sendEvent(recorder, &move);
    QCoreApplication::sendEvent(recorder, &release);

    QMouseEvent click_press(QEvent::MouseButtonPress, QPointF{16.0, 16.0},
                            QPointF{116.0, 116.0}, Qt::MiddleButton,
                            Qt::MiddleButton, Qt::AltModifier);
    QMouseEvent click_release(QEvent::MouseButtonRelease,
                              QPointF{16.0, 16.0}, QPointF{116.0, 116.0},
                              Qt::MiddleButton, Qt::NoButton,
                              Qt::AltModifier);
    QCoreApplication::sendEvent(recorder, &click_press);
    QCoreApplication::sendEvent(recorder, &click_release);
    EXPECT_FALSE(use->isEnabled());
    send_key(*recorder, Qt::Key_L);
    EXPECT_TRUE(use->isEnabled());
    EXPECT_TRUE(recorder->text().contains(
        QStringLiteral("Alt+MiddleMouseClick L")));

    QMouseEvent left_click_press(
        QEvent::MouseButtonPress, QPointF{17.0, 17.0},
        QPointF{117.0, 117.0}, Qt::LeftButton, Qt::LeftButton,
        Qt::NoModifier);
    QMouseEvent left_click_release(
        QEvent::MouseButtonRelease, QPointF{17.0, 17.0},
        QPointF{117.0, 117.0}, Qt::LeftButton, Qt::NoButton,
        Qt::NoModifier);
    QMouseEvent right_click_press(
        QEvent::MouseButtonPress, QPointF{18.0, 18.0},
        QPointF{118.0, 118.0}, Qt::RightButton, Qt::RightButton,
        Qt::NoModifier);
    QMouseEvent right_click_release(
        QEvent::MouseButtonRelease, QPointF{18.0, 18.0},
        QPointF{118.0, 118.0}, Qt::RightButton, Qt::NoButton,
        Qt::NoModifier);
    QCoreApplication::sendEvent(recorder, &left_click_press);
    QCoreApplication::sendEvent(recorder, &left_click_release);
    QCoreApplication::sendEvent(recorder, &right_click_press);
    QCoreApplication::sendEvent(recorder, &right_click_release);
    EXPECT_FALSE(use->isEnabled());
    QTimer::singleShot(
        QApplication::doubleClickInterval() + 50, use, [recorder, use]() {
          EXPECT_TRUE(recorder->text().contains(
              QStringLiteral("LeftMouseClick RightMouseClick")));
          EXPECT_TRUE(use->isEnabled());
          QMouseEvent held_press(
              QEvent::MouseButtonPress, QPointF{20.0, 20.0},
              QPointF{120.0, 120.0}, Qt::RightButton, Qt::RightButton,
              Qt::ShiftModifier);
          QCoreApplication::sendEvent(recorder, &held_press);
          QTimer::singleShot(
              QApplication::doubleClickInterval() + 50, use,
              [recorder, use]() {
                QMouseEvent held_release(
                    QEvent::MouseButtonRelease, QPointF{20.0, 20.0},
                    QPointF{120.0, 120.0}, Qt::RightButton, Qt::NoButton,
                    Qt::ShiftModifier);
                QCoreApplication::sendEvent(recorder, &held_release);
                EXPECT_TRUE(recorder->text().contains(
                    QStringLiteral("Shift+RightMouse")));
                use->click();
              });
        });
  });
  send_key(*table, Qt::Key_Return);
  QCoreApplication::processEvents();

  constexpr std::string_view kCaptured =
      "Ctrl+K Escape Enter Tab Backspace Delete WheelUp Mouse7DoubleClick "
      "Mouse6Drag Alt+MiddleMouseClick L LeftMouseClick RightMouseClick "
      "Shift+RightMouse";
  const auto catalog = editor.commands().keybinding_catalog();
  const QuaderKeybindingCatalogEntry *undo = find_keybinding_catalog_entry(
      catalog, quader_command::kEditUndo.string());
  ASSERT_NE(undo, nullptr);
  EXPECT_EQ(undo->shortcut, std::string(kCaptured));
  EXPECT_TRUE(undo->customized);
  EXPECT_EQ(count_changes(changes, QtSettingsChangeKind::KeybindingsCommit),
            1u);
  EXPECT_FALSE(apply->isEnabled());
  EXPECT_NE(read_text_file(keybindings).find(std::string(kCaptured)),
            std::string::npos);

  QTimer::singleShot(0, &dialog, []() {
    QWidget *capture = QApplication::activeModalWidget();
    ASSERT_NE(capture, nullptr);
    auto *unassign = capture->findChild<QPushButton *>(
        QStringLiteral("settingsKeybindingCaptureUnassign"));
    ASSERT_NE(unassign, nullptr);
    EXPECT_TRUE(unassign->isEnabled());
    unassign->click();
  });
  send_key(*table, Qt::Key_Return);
  QCoreApplication::processEvents();
  const auto reset_catalog = editor.commands().keybinding_catalog();
  undo = find_keybinding_catalog_entry(reset_catalog,
                                       quader_command::kEditUndo.string());
  ASSERT_NE(undo, nullptr);
  EXPECT_TRUE(undo->shortcut.empty());
  EXPECT_TRUE(undo->customized);
  EXPECT_TRUE(undo->unassigned);
  EXPECT_EQ(count_changes(changes, QtSettingsChangeKind::KeybindingsCommit),
            2u);

  QTimer::singleShot(0, &dialog, []() {
    QWidget *capture = QApplication::activeModalWidget();
    ASSERT_NE(capture, nullptr);
    auto *unassign = capture->findChild<QPushButton *>(
        QStringLiteral("settingsKeybindingCaptureUnassign"));
    ASSERT_NE(unassign, nullptr);
    EXPECT_FALSE(unassign->isEnabled());
    qobject_cast<QDialog *>(capture)->reject();
  });
  send_key(*table, Qt::Key_Return);

  reset_action->click();
  QCoreApplication::processEvents();
  const auto restored_catalog = editor.commands().keybinding_catalog();
  undo = find_keybinding_catalog_entry(restored_catalog,
                                       quader_command::kEditUndo.string());
  ASSERT_NE(undo, nullptr);
  EXPECT_EQ(undo->shortcut, undo->default_shortcut);
  EXPECT_FALSE(undo->customized);
  EXPECT_EQ(count_changes(changes, QtSettingsChangeKind::KeybindingsCommit),
            3u);

  dialog.reject();
  std::filesystem::remove(keybindings, ignored);
}

TEST(QtSettingsDialogTests,
     KeybindingConflictCancelReturnsToCaptureAndReplaceIsImmediate) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-settings-keybinding-conflict-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const std::filesystem::path keybindings =
      std::filesystem::temp_directory_path() /
      "quader-settings-keybinding-conflicts.json";
  std::error_code ignored;
  std::filesystem::remove(keybindings, ignored);
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path(keybindings).ok());

  UISettings ui_settings = UISettings::defaults();
  ViewportSettings viewport_settings = ViewportSettings::defaults();
  LocalizationService localization = loaded_localization();
  std::vector<QtSettingsChangeKind> changes;
  QtSettingsDialog dialog;
  dialog.setAttribute(Qt::WA_DontShowOnScreen, true);
  dialog.set_context(
      ui_settings, viewport_settings, localization, editor.commands(),
      [&](QtSettingsChangeKind kind) { changes.push_back(kind); });
  dialog.show();
  QCoreApplication::processEvents();

  auto *table = dialog.findChild<QTableView *>(
      QStringLiteral("settingsKeybindingsTable"));
  auto *reset_all = dialog.findChild<QPushButton *>(
      QStringLiteral("settingsKeybindingsResetAll"));
  ASSERT_NE(table, nullptr);
  ASSERT_NE(table->model(), nullptr);
  ASSERT_NE(reset_all, nullptr);

  const std::string delete_target =
      std::string(quader_command::kDeleteSelection.string());
  const std::string move_target =
      editor.commands().command_id_for_tool_activation("move");
  int delete_row = -1;
  for (int row = 0; row < table->model()->rowCount(); ++row) {
    if (table->model()
            ->data(table->model()->index(row, 0), Qt::UserRole)
            .toString() == QString::fromStdString(delete_target)) {
      delete_row = row;
      break;
    }
  }
  ASSERT_GE(delete_row, 0);
  table->selectRow(delete_row);
  QCoreApplication::processEvents();

  EXPECT_TRUE(capture_keybinding_and_choose_conflict(
      dialog, *table, QStringLiteral("settingsKeybindingConflictCancel"),
      QStringLiteral("Activate Move Tool"), true));
  auto catalog = editor.commands().keybinding_catalog();
  const QuaderKeybindingCatalogEntry *deleted =
      find_keybinding_catalog_entry(catalog, delete_target);
  ASSERT_NE(deleted, nullptr);
  EXPECT_NE(deleted->shortcut, "W");
  EXPECT_EQ(count_changes(changes, QtSettingsChangeKind::KeybindingsCommit),
            0u);

  EXPECT_TRUE(capture_keybinding_and_choose_conflict(
      dialog, *table, QStringLiteral("settingsKeybindingConflictReplace"),
      QStringLiteral("Activate Move Tool"), false));
  catalog = editor.commands().keybinding_catalog();
  deleted = find_keybinding_catalog_entry(catalog, delete_target);
  const QuaderKeybindingCatalogEntry *move =
      find_keybinding_catalog_entry(catalog, move_target);
  ASSERT_NE(deleted, nullptr);
  ASSERT_NE(move, nullptr);
  EXPECT_EQ(deleted->shortcut, "W");
  EXPECT_TRUE(move->shortcut.empty());
  EXPECT_TRUE(move->customized);
  EXPECT_TRUE(move->unassigned);
  EXPECT_EQ(count_changes(changes, QtSettingsChangeKind::KeybindingsCommit),
            1u);

  EXPECT_TRUE(choose_reset_all_confirmation(dialog, *reset_all,
      QStringLiteral("settingsKeybindingResetAllCancel")));
  EXPECT_EQ(count_changes(changes, QtSettingsChangeKind::KeybindingsCommit),
            1u);
  catalog = editor.commands().keybinding_catalog();
  deleted = find_keybinding_catalog_entry(catalog, delete_target);
  ASSERT_NE(deleted, nullptr);
  EXPECT_EQ(deleted->shortcut, "W");

  EXPECT_TRUE(choose_reset_all_confirmation(dialog, *reset_all,
      QStringLiteral("settingsKeybindingResetAllConfirm")));
  EXPECT_EQ(count_changes(changes, QtSettingsChangeKind::KeybindingsCommit),
            2u);
  catalog = editor.commands().keybinding_catalog();
  deleted = find_keybinding_catalog_entry(catalog, delete_target);
  move = find_keybinding_catalog_entry(catalog, move_target);
  ASSERT_NE(deleted, nullptr);
  ASSERT_NE(move, nullptr);
  EXPECT_EQ(deleted->shortcut, deleted->default_shortcut);
  EXPECT_EQ(move->shortcut, move->default_shortcut);

  dialog.reject();
  std::filesystem::remove(keybindings, ignored);
}

TEST(QtSettingsDialogTests,
     KeybindingWriteFailureRetainsSelectionAndCapturedSequence) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-settings-keybinding-failure-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  const std::filesystem::path blocker =
      std::filesystem::temp_directory_path() /
      "quader-settings-keybinding-write-blocker";
  std::error_code ignored;
  std::filesystem::remove_all(blocker, ignored);
  {
    std::ofstream file(blocker);
    file << "not a directory";
  }

  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  ASSERT_TRUE(editor.commands()
                  .set_keybinding_storage_path(blocker / "keybindings.json")
                  .ok());
  UISettings ui_settings = UISettings::defaults();
  ViewportSettings viewport_settings = ViewportSettings::defaults();
  LocalizationService localization = loaded_localization();
  std::vector<QtSettingsChangeKind> changes;
  QtSettingsDialog dialog;
  dialog.setAttribute(Qt::WA_DontShowOnScreen, true);
  dialog.set_context(
      ui_settings, viewport_settings, localization, editor.commands(),
      [&](QtSettingsChangeKind kind) { changes.push_back(kind); });
  dialog.show();
  QCoreApplication::processEvents();

  auto *table = dialog.findChild<QTableView *>(
      QStringLiteral("settingsKeybindingsTable"));
  ASSERT_NE(table, nullptr);
  ASSERT_NE(table->model(), nullptr);
  int undo_row = -1;
  for (int row = 0; row < table->model()->rowCount(); ++row) {
    if (table->model()
            ->data(table->model()->index(row, 0), Qt::UserRole)
            .toString() ==
        QString::fromUtf8(quader_command::kEditUndo.string().data(),
                          static_cast<qsizetype>(
                              quader_command::kEditUndo.string().size()))) {
      undo_row = row;
      break;
    }
  }
  ASSERT_GE(undo_row, 0);
  table->selectRow(undo_row);
  QCoreApplication::processEvents();

  bool error_shown = false;
  bool capture_preserved = false;
  QTimer::singleShot(0, &dialog, [&]() {
    QWidget *capture = QApplication::activeModalWidget();
    ASSERT_NE(capture, nullptr);
    auto *recorder = capture->findChild<QLineEdit *>(
        QStringLiteral("settingsKeybindingCaptureRecorder"));
    auto *use = capture->findChild<QPushButton *>(
        QStringLiteral("settingsKeybindingCaptureUse"));
    ASSERT_NE(recorder, nullptr);
    ASSERT_NE(use, nullptr);
    send_key(*recorder, Qt::Key_U,
             Qt::ControlModifier | Qt::AltModifier);
    QTimer::singleShot(0, &dialog, [&]() {
      auto *error = qobject_cast<QMessageBox *>(
          QApplication::activeModalWidget());
      ASSERT_NE(error, nullptr);
      EXPECT_EQ(error->objectName(),
                QStringLiteral("settingsKeybindingErrorDialog"));
      EXPECT_TRUE(error->text().startsWith(
          QStringLiteral("Unable to save the keybinding change.")));
      error_shown = true;
      QTimer::singleShot(0, &dialog, [&]() {
        QWidget *reopened = QApplication::activeModalWidget();
        ASSERT_NE(reopened, nullptr);
        EXPECT_EQ(reopened->objectName(),
                  QStringLiteral("settingsKeybindingCaptureDialog"));
        auto *preserved = reopened->findChild<QLineEdit *>(
            QStringLiteral("settingsKeybindingCaptureRecorder"));
        ASSERT_NE(preserved, nullptr);
        EXPECT_EQ(preserved->text(), QStringLiteral("Ctrl+Alt+U"));
        capture_preserved = true;
        qobject_cast<QDialog *>(reopened)->reject();
      });
      error->accept();
    });
    use->click();
  });
  send_key(*table, Qt::Key_Return);

  EXPECT_TRUE(error_shown);
  EXPECT_TRUE(capture_preserved);
  EXPECT_TRUE(table->selectionModel()->hasSelection());
  EXPECT_EQ(count_changes(changes, QtSettingsChangeKind::KeybindingsCommit),
            0u);
  const auto catalog = editor.commands().keybinding_catalog();
  const QuaderKeybindingCatalogEntry *undo = find_keybinding_catalog_entry(
      catalog, quader_command::kEditUndo.string());
  ASSERT_NE(undo, nullptr);
  EXPECT_NE(undo->shortcut, "Ctrl+Alt+U");

  dialog.reject();
  std::filesystem::remove(blocker, ignored);
}

TEST(InspectorPanelTests,
     InstanceNameCommitsOnEnterAndFocusLoss) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-inspector-instance-name-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const QuaderApiResult<QuaderModelingObjectRef> box =
      editor.modeling().create_box(
          QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                       .max = {1.0f, 1.0f, 1.0f}},
          "Custom Mesh Name");
  ASSERT_TRUE(box.ok()) << box.error_message();
  ASSERT_TRUE(editor.selection().select_object(box.value()).ok());
  ASSERT_TRUE(editor.modeling().create_instance().ok());

  const EntityId occurrence = session.scene().primary_selection();
  ASSERT_NE(occurrence, kInvalidEntityId);
  ASSERT_NE(session.scene().instance_occurrence(occurrence), nullptr);
  ASSERT_NE(session.scene().name(occurrence), nullptr);
  EXPECT_EQ(session.scene().name(occurrence)->name, "Custom Mesh Name");

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
  auto *name = panel.findChild<QLineEdit *>(
      QStringLiteral("inspector.general.name"));
  auto *tree = panel.findChild<QTreeView *>(
      QStringLiteral("inspector.tree"));
  ASSERT_NE(name, nullptr);
  ASSERT_NE(tree, nullptr);
  EXPECT_EQ(panel.focusProxy(), tree);

  name->deselect();
  name->setCursorPosition(3);
  QFocusEvent mouse_focus_in(QEvent::FocusIn, Qt::MouseFocusReason);
  QCoreApplication::sendEvent(name, &mouse_focus_in);
  QCoreApplication::processEvents();
  EXPECT_TRUE(name->selectedText().isEmpty())
      << "mouse focus must preserve the caret instead of selecting all";

  QFocusEvent focus_out_before_keyboard(QEvent::FocusOut,
                                        Qt::TabFocusReason);
  QCoreApplication::sendEvent(name, &focus_out_before_keyboard);
  name->deselect();
  QFocusEvent keyboard_focus_in(QEvent::FocusIn, Qt::TabFocusReason);
  QCoreApplication::sendEvent(name, &keyboard_focus_in);
  QCoreApplication::processEvents();
  EXPECT_EQ(name->selectedText(), name->text())
      << "Tab focus should retain the editor's keyboard-select-all behavior";

  name->setText(QStringLiteral("Renamed With Enter"));
  send_key(*name, Qt::Key_Return);
  ASSERT_NE(session.scene().name(occurrence), nullptr);
  EXPECT_EQ(session.scene().name(occurrence)->name, "Renamed With Enter");

  name->setText(QStringLiteral("Renamed On Focus Loss"));
  QFocusEvent focus_out(QEvent::FocusOut, Qt::TabFocusReason);
  QCoreApplication::sendEvent(name, &focus_out);
  QCoreApplication::processEvents();
  ASSERT_NE(session.scene().name(occurrence), nullptr);
  EXPECT_EQ(session.scene().name(occurrence)->name,
            "Renamed On Focus Loss");
}

TEST(InspectorPanelTests,
     EqualRevisionDocumentSwitchRebuildsSameIdProperties) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-inspector-document-switch-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  SceneSession first_session;
  SceneSession second_session;
  QuaderEditorApi first_editor(
      QuaderApiSessionInternalAccess::from_scene_session(first_session));
  QuaderEditorApi second_editor(
      QuaderApiSessionInternalAccess::from_scene_session(second_session));
  const auto first_entity = first_editor.scene().create_entity({
      .name = "First Document Object",
      .kind = QuaderEntityKind::Empty,
  });
  const auto second_entity = second_editor.scene().create_entity({
      .name = "Second Document Object",
      .kind = QuaderEntityKind::Empty,
  });
  ASSERT_TRUE(first_entity.ok()) << first_entity.error_message();
  ASSERT_TRUE(second_entity.ok()) << second_entity.error_message();
  ASSERT_EQ(first_entity.value().id, second_entity.value().id);
  ASSERT_TRUE(first_editor.scene().select(first_entity.value().id).ok());
  ASSERT_TRUE(second_editor.scene().select(second_entity.value().id).ok());
  ASSERT_EQ(first_session.scene().revision(), second_session.scene().revision());
  ASSERT_EQ(first_session.modeling().content_revision(),
            second_session.modeling().content_revision());
  ASSERT_EQ(first_session.modeling().selection_revision(),
            second_session.modeling().selection_revision());

  UISettings ui_settings = UISettings::defaults();
  ViewportSettings viewport_settings = ViewportSettings::defaults();
  LocalizationService localization = loaded_localization();
  UIInspectorPreviewContext first_preview{.document = first_session.scene(),
                                           .assets = first_session.assets()};
  UIInspectorPreviewContext second_preview{.document = second_session.scene(),
                                            .assets = second_session.assets()};
  UIContext first_context{
      .intent = {.editor = first_editor},
      .scene = {.document = first_session.scene(),
                .assets = first_session.assets(),
                .modeling = first_session.modeling(),
                .selection = first_session.selection()},
      .inspector_preview = &first_preview,
      .preferences = {.ui = ui_settings, .viewport = viewport_settings},
      .localization = localization,
  };
  UIContext second_context{
      .intent = {.editor = second_editor},
      .scene = {.document = second_session.scene(),
                .assets = second_session.assets(),
                .modeling = second_session.modeling(),
                .selection = second_session.selection()},
      .inspector_preview = &second_preview,
      .preferences = {.ui = ui_settings, .viewport = viewport_settings},
      .localization = localization,
  };

  InspectorPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.refresh(first_context);
  auto *name = panel.findChild<QLineEdit *>(
      QStringLiteral("inspector.general.name"));
  ASSERT_NE(name, nullptr);
  EXPECT_EQ(name->text(), QStringLiteral("First Document Object"));

  panel.refresh(second_context);
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  name = panel.findChild<QLineEdit *>(
      QStringLiteral("inspector.general.name"));
  ASSERT_NE(name, nullptr);
  EXPECT_EQ(name->text(), QStringLiteral("Second Document Object"));
}

TEST(InspectorPanelTests,
     ColorCancelDoesNotMutateDocumentHistoryOrDirtyState) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-inspector-color-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
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

TEST(QtMainMenuTests, SelectMenuTracksSelectionModeAndOwnsSelectionCommands) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-select-menu-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const QuaderApiResult<QuaderModelingObjectRef> box =
      editor.modeling().create_box(
          QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                       .max = {1.0f, 1.0f, 1.0f}},
          "Select Menu Box");
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

  auto *edit_menu =
      menu_bar.findChild<QMenu *>(QStringLiteral("menu_edit"));
  auto *select_menu =
      menu_bar.findChild<QMenu *>(QStringLiteral("menu_select"));
  auto *align_menu =
      menu_bar.findChild<QMenu *>(QStringLiteral("menu_align"));
  ASSERT_NE(edit_menu, nullptr);
  ASSERT_NE(select_menu, nullptr);
  ASSERT_NE(align_menu, nullptr);

  const QList<QAction *> top_level_actions = menu_bar.actions();
  EXPECT_EQ(top_level_actions.indexOf(select_menu->menuAction()),
            top_level_actions.indexOf(edit_menu->menuAction()) + 1);
  EXPECT_EQ(top_level_actions.indexOf(align_menu->menuAction()),
            top_level_actions.indexOf(select_menu->menuAction()) + 1);

  auto *select_all = menu_bar.findChild<QAction *>(
      QStringLiteral("command_select_all"));
  auto *select_inverse = menu_bar.findChild<QAction *>(
      QStringLiteral("command_select_inverse"));
  auto *select_direction = menu_bar.findChild<QAction *>(
      QStringLiteral("command_select_edge_direction"));
  auto *select_loops = menu_bar.findChild<QAction *>(
      QStringLiteral("command_select_edge_loops"));
  auto *select_ring = menu_bar.findChild<QAction *>(
      QStringLiteral("command_select_edge_ring"));
  auto *select_same_material = menu_bar.findChild<QAction *>(
      QStringLiteral("command_select_face_same_material"));
  auto *select_coplanar = menu_bar.findChild<QAction *>(
      QStringLiteral("command_select_face_coplanar"));
  auto *select_normal = menu_bar.findChild<QAction *>(
      QStringLiteral("command_select_face_normal"));
  auto *select_ngons = menu_bar.findChild<QAction *>(
      QStringLiteral("command_select_mesh_ngons"));
  auto *select_similar = menu_bar.findChild<QAction *>(
      QStringLiteral("command_select_mesh_similar"));
  auto *grow = menu_bar.findChild<QAction *>(
      QStringLiteral("command_select_grow"));
  auto *shrink = menu_bar.findChild<QAction *>(
      QStringLiteral("command_select_shrink"));
  auto *clear_selection = menu_bar.findChild<QAction *>(
      QStringLiteral("command_edit_clear_selection"));

  const std::array<QAction *, 12> select_actions{
      select_all,           select_inverse,  select_direction,
      select_loops,         select_ring,     select_same_material,
      select_coplanar,      select_normal,   select_ngons,
      select_similar,
      grow,                 shrink,
  };
  for (QAction *action : select_actions) {
    ASSERT_NE(action, nullptr);
    EXPECT_TRUE(select_menu->actions().contains(action));
    EXPECT_FALSE(edit_menu->actions().contains(action));
  }
  ASSERT_NE(clear_selection, nullptr);
  EXPECT_TRUE(edit_menu->actions().contains(clear_selection));
  EXPECT_FALSE(select_menu->actions().contains(clear_selection));

  const std::array<QAction *, 10> mode_specific_actions{
      select_direction, select_loops,         select_ring,
      select_same_material, select_coplanar,  select_normal,
      select_ngons,     select_similar,       grow,
      shrink,
  };
  const auto expect_visible_for_mode =
      [&](QuaderSelectionMode mode,
          std::initializer_list<QAction *> expected_visible) {
        ASSERT_TRUE(editor.selection().set_mode(mode).ok());
        ASSERT_TRUE(QMetaObject::invokeMethod(select_menu, "aboutToShow",
                                              Qt::DirectConnection));
        const std::set<QAction *> visible(expected_visible);
        EXPECT_TRUE(select_all->isVisible());
        EXPECT_TRUE(select_inverse->isVisible());
        for (QAction *action : mode_specific_actions) {
          EXPECT_EQ(action->isVisible(), visible.contains(action));
        }
      };

  expect_visible_for_mode(QuaderSelectionMode::Object,
                          {select_ngons, select_similar});
  expect_visible_for_mode(
      QuaderSelectionMode::Edge,
      {select_direction, select_loops, select_ring, grow, shrink});
  expect_visible_for_mode(
      QuaderSelectionMode::Face,
      {select_same_material, select_coplanar, select_normal, grow, shrink});
  expect_visible_for_mode(QuaderSelectionMode::Vertex, {grow, shrink});
  expect_visible_for_mode(QuaderSelectionMode::Group, {});
}

TEST(QtMainMenuTests, ToolActionsUseEffectiveKeybindingsAndCommandRouting) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-main-menu-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
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
  const QuaderApiResult<QuaderModelingObjectRef> baseline_box =
      editor.modeling().create_box(
          QuaderBounds{.min = {3.0f, -1.0f, -1.0f},
                       .max = {5.0f, 1.0f, 1.0f}},
          "Menu Local Visibility Baseline");
  ASSERT_TRUE(baseline_box.ok()) << baseline_box.error_message();
  ASSERT_TRUE(editor.selection().select_object(box.value()).ok());

  ViewportSettings viewport_settings = ViewportSettings::defaults();
  LocalizationService localization = loaded_localization();
  QMenuBar menu_bar;
  QtMainMenu menu;
  int material_browser_requests = 0;
  bool material_browser_available = false;
  menu.initialize(menu_bar,
                  QtMainMenuContext{
                      .editor = editor,
                      .modeling = session.modeling(),
                      .viewport_settings = viewport_settings,
                      .localization = localization,
                      .show_material_browser =
                          [&material_browser_requests]() {
                            ++material_browser_requests;
                          },
                      .material_browser_available =
                          [&material_browser_available]() {
                            return material_browser_available;
                          },
                  });
  // Menu descriptors are intentionally synchronized on demand; initialization
  // only builds the retained hierarchy and global chrome.
  menu.refresh();
  auto *pivot_action =
      menu_bar.findChild<QAction *>(QStringLiteral("tool_pivot"));
  ASSERT_NE(pivot_action, nullptr);
  EXPECT_TRUE(pivot_action->text().contains(QStringLiteral("\tP")));
  auto *two_vertical = menu_bar.findChild<QAction *>(
      QStringLiteral("layout_two_vertical"));
  ASSERT_NE(two_vertical, nullptr);
  EXPECT_TRUE(two_vertical->text().contains(QStringLiteral("\tCtrl+2")));
  auto *local_visibility_action = menu_bar.findChild<QAction *>(
      QStringLiteral("command_viewport_toggle_local_visibility"));
  ASSERT_NE(local_visibility_action, nullptr);
  EXPECT_TRUE(local_visibility_action->isCheckable());
  EXPECT_FALSE(local_visibility_action->isChecked());
  EXPECT_TRUE(local_visibility_action->isEnabled());
  EXPECT_TRUE(local_visibility_action->shortcut().isEmpty());
  EXPECT_EQ(local_visibility_action->text(),
            QStringLiteral("Toggle Local Visibility\tBackslash"));
  auto *material_browser_action = menu_bar.findChild<QAction *>(
      QStringLiteral("action_material_browser"));
  ASSERT_NE(material_browser_action, nullptr);
  EXPECT_FALSE(material_browser_action->isEnabled());
  material_browser_available = true;
  menu.refresh();
  EXPECT_TRUE(material_browser_action->isEnabled());
  EXPECT_EQ(material_browser_action->text(),
            QStringLiteral("Material Browser"));
  material_browser_action->trigger();
  EXPECT_EQ(material_browser_requests, 1);
  auto *create_instance_action = menu_bar.findChild<QAction *>(
      QStringLiteral("command_edit_create_instance"));
  auto *flatten_instance_action = menu_bar.findChild<QAction *>(
      QStringLiteral("command_edit_flatten_instance"));
  ASSERT_NE(create_instance_action, nullptr);
  ASSERT_NE(flatten_instance_action, nullptr);
  EXPECT_EQ(create_instance_action->text(), QStringLiteral("Create Instance"));
  EXPECT_EQ(flatten_instance_action->text(),
            QStringLiteral("Flatten Instance"));
  EXPECT_TRUE(create_instance_action->isEnabled());
  EXPECT_FALSE(flatten_instance_action->isEnabled());

  ASSERT_TRUE(editor.preferences()
                  .set_shortcut(quader_command::kToolsActivatePivot,
                                "Ctrl+Alt+P")
                  .ok());
  ASSERT_TRUE(editor.preferences()
                  .set_shortcut(quader_command::kViewportLayoutTwoVertical,
                                "Ctrl+Alt+2")
                  .ok());
  menu.refresh();
  EXPECT_TRUE(
      pivot_action->text().contains(QStringLiteral("\tCtrl+Alt+P")));
  EXPECT_TRUE(
      two_vertical->text().contains(QStringLiteral("\tCtrl+Alt+2")));
  EXPECT_FALSE(editor.commands().last_status().has_value());

  pivot_action->trigger();
  ASSERT_TRUE(editor.commands().last_status().has_value());
  EXPECT_TRUE(editor.commands().last_status()->ok());
  EXPECT_EQ(editor.tools().active_tool(), "pivot");

  two_vertical->trigger();
  EXPECT_EQ(editor.viewports().layout(), ViewportLayoutId::TwoVertical);

  ASSERT_TRUE(editor.tools().activate("select").ok());
  local_visibility_action->trigger();
  EXPECT_TRUE(editor.viewports().local_visibility_active());
  EXPECT_TRUE(local_visibility_action->isChecked());
  local_visibility_action->trigger();
  EXPECT_FALSE(editor.viewports().local_visibility_active());
  EXPECT_FALSE(local_visibility_action->isChecked());

  ASSERT_TRUE(editor.commands()
                  .execute(
                      quader_command::kViewportToggleLocalVisibility)
                  .ok());
  EXPECT_TRUE(editor.viewports().local_visibility_active());
  EXPECT_FALSE(local_visibility_action->isChecked());
  auto *view_menu =
      menu_bar.findChild<QMenu *>(QStringLiteral("menu_view"));
  ASSERT_NE(view_menu, nullptr);
  ASSERT_TRUE(QMetaObject::invokeMethod(view_menu, "aboutToShow",
                                        Qt::DirectConnection));
  EXPECT_TRUE(local_visibility_action->isChecked());
  local_visibility_action->trigger();
  EXPECT_FALSE(editor.viewports().local_visibility_active());
  EXPECT_FALSE(local_visibility_action->isChecked());

  std::filesystem::remove(keybindings);
}

TEST(QtMainMenuTests, RebuildAndDestructionReleaseOwnedHierarchy) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-main-menu-lifetime-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
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

  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
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
