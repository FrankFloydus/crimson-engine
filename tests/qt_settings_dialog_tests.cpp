////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_api_type_conversions.hpp"
#include "api/quader_editor_api.hpp"
#include "core/localization_service.hpp"
#include "editor/metadata/command_presentation_catalog.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/session/project_session.hpp"
#include "editor/session/quader_editor_api_session_access.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/transform/scene_transform_drag_service.hpp"
#include "math/glm_bridge.hpp"
#include "scene/light_properties.hpp"
#include "ui/inspector_panel.hpp"
#include "ui/qt_environment_settings_popup.hpp"
#include "ui/qt_color_swatch_button.hpp"
#include "ui/qt_double_spin_box.hpp"
#include "ui/qt_main_menu.hpp"
#include "ui/qt_main_window.hpp"
#include "ui/qt_numeric_spin_box.hpp"
#include "ui/qt_retained_message_box.hpp"
#include "ui/qt_settings_dialog.hpp"
#include "ui/qt_settings_control_widgets.hpp"
#include "ui/qt_application_theme.hpp"
#include "ui/qt_ui_scale_metrics.hpp"
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
#include <QDir>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QFocusEvent>
#include <QFormLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QImage>
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
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSlider>
#include <QStackedWidget>
#include <QStringList>
#include <QTableView>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QTreeWidget>
#include <QVariant>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cmath>
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
#include <utility>
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

QString settings_section_title(const QWidget &section) {
  const auto *header = section.findChild<QToolButton *>(
      section.objectName() + QStringLiteral(".header"));
  return header != nullptr ? header->text() : QString{};
}

std::vector<qt_settings_detail::QtSettingsSection *>
settings_sections(QWidget &parent) {
  std::vector<qt_settings_detail::QtSettingsSection *> sections;
  for (QWidget *widget : parent.findChildren<QWidget *>()) {
    if (auto *section =
            dynamic_cast<qt_settings_detail::QtSettingsSection *>(widget)) {
      sections.push_back(section);
    }
  }
  return sections;
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

std::size_t count_changes(const std::vector<QtSettingsChange> &changes,
                          QtSettingsChangeKind kind) {
  return static_cast<std::size_t>(
      std::count_if(changes.begin(), changes.end(),
                    [kind](const QtSettingsChange &change) {
                      return change.kind == kind;
                    }));
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
    QString conflicting_action, bool returns_to_capture,
    std::function<void(QMessageBox &)> inspect = {}) {
  /// Tracks whether both nested dialogs were observed during the click.
  struct Outcome {
    bool capture_opened = false;
    bool conflict_opened = false;
  };
  const auto outcome = std::make_shared<Outcome>();
  QTimer::singleShot(
      0, &owner,
      [&owner, outcome, choice_object_name, conflicting_action,
       returns_to_capture, inspect = std::move(inspect)]() {
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
             returns_to_capture, inspect]() {
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
                             static_cast<QtRetainedMessageBox *>(box)
                                 ->presented_icon(),
                             QMessageBox::Warning);
                         EXPECT_EQ(box->objectName(),
                                   QStringLiteral(
                                       "settingsKeybindingConflictDialog"));
                         EXPECT_EQ(box->windowTitle(),
                                   QStringLiteral("Keybinding Conflict"));
                         EXPECT_EQ(
                             box->text().toStdString(),
                             QStringLiteral(
                                 "This input mapping clashes with “%1”.")
                                 .arg(conflicting_action).toStdString());
                         EXPECT_TRUE(box->informativeText().isEmpty());
                         auto *replace = box->findChild<QPushButton *>(
                             QStringLiteral(
                                 "settingsKeybindingConflictReplace"));
                         auto *cancel = box->findChild<QPushButton *>(
                             QStringLiteral(
                                 "settingsKeybindingConflictCancel"));
                         ASSERT_NE(replace, nullptr);
                         ASSERT_NE(cancel, nullptr);
                         EXPECT_EQ(replace->text(), QStringLiteral("Replace"));
                         EXPECT_EQ(cancel->text(), QStringLiteral("Cancel"));
                         EXPECT_EQ(box->buttonRole(replace),
                                   QMessageBox::AcceptRole);
                         EXPECT_EQ(box->buttonRole(cancel),
                                   QMessageBox::RejectRole);
                         EXPECT_EQ(box->findChild<QPushButton *>(
                                       QStringLiteral(
                                           "settingsKeybindingConflictKeepBoth")),
                                   nullptr);
                         ASSERT_NE(box->defaultButton(), nullptr);
                         EXPECT_EQ(
                             box->defaultButton()->objectName(),
                             QStringLiteral(
                                 "settingsKeybindingConflictCancel"));
                         EXPECT_EQ(box->escapeButton(), cancel);
                         if (inspect) {
                           inspect(*box);
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
                         if (choice_object_name.isEmpty()) {
                           box->close();
                           return;
                         }
                         auto *choice = box->findChild<QPushButton *>(
                             choice_object_name);
                         if (choice == nullptr) {
                           ADD_FAILURE() << "Expected conflict choice button.";
                           box->close();
                           return;
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
                                   QString choice_object_name,
                                   std::function<void(QMessageBox &)> inspect =
                                       {}) {
  bool opened = false;
  QTimer::singleShot(0, &owner, [&]() {
    auto *confirmation = qobject_cast<QMessageBox *>(
        QApplication::activeModalWidget());
    if (confirmation == nullptr) {
      ADD_FAILURE() << "Expected the Reset All confirmation dialog.";
      return;
    }
    opened = true;
    EXPECT_EQ(static_cast<QtRetainedMessageBox *>(confirmation)
                  ->presented_icon(),
              QMessageBox::Question);
    EXPECT_EQ(confirmation->objectName(),
              QStringLiteral("settingsKeybindingResetAllDialog"));
    EXPECT_EQ(confirmation->windowTitle(),
              QStringLiteral("Reset All Keybindings"));
    EXPECT_EQ(confirmation->text(),
              QStringLiteral(
                  "Reset every keybinding to its default mapping?"));
    EXPECT_TRUE(confirmation->informativeText().isEmpty());
    auto *confirm = confirmation->findChild<QPushButton *>(
        QStringLiteral("settingsKeybindingResetAllConfirm"));
    auto *cancel = confirmation->findChild<QPushButton *>(
        QStringLiteral("settingsKeybindingResetAllCancel"));
    ASSERT_NE(confirm, nullptr);
    ASSERT_NE(cancel, nullptr);
    EXPECT_EQ(confirm->text(), QStringLiteral("Reset All"));
    EXPECT_EQ(cancel->text(), QStringLiteral("Cancel"));
    EXPECT_EQ(confirmation->buttonRole(confirm), QMessageBox::AcceptRole);
    EXPECT_EQ(confirmation->buttonRole(cancel), QMessageBox::RejectRole);
    EXPECT_EQ(confirmation->defaultButton(), cancel);
    EXPECT_EQ(confirmation->escapeButton(), cancel);
    if (inspect) {
      inspect(*confirmation);
    }
    if (choice_object_name.isEmpty()) {
      confirmation->close();
      return;
    }
    auto *button =
        confirmation->findChild<QPushButton *>(choice_object_name);
    ASSERT_NE(button, nullptr);
    button->click();
  });
  reset_all.click();
  return opened;
}

bool capture_keybinding_and_expect_error(
    QWidget &owner, QTableView &table, int key,
    Qt::KeyboardModifiers modifiers, QString expected_shortcut,
    bool close_error, std::function<void(QMessageBox &)> inspect = {}) {
  /// Tracks the complete failure-and-return nested modal sequence.
  struct Outcome {
    bool error_opened = false;
    bool capture_preserved = false;
  };
  const auto outcome = std::make_shared<Outcome>();
  QTimer::singleShot(
      0, &owner,
      [&owner, outcome, key, modifiers, expected_shortcut, close_error,
       inspect = std::move(inspect)]() {
        QWidget *capture = QApplication::activeModalWidget();
        if (capture == nullptr) {
          ADD_FAILURE() << "Expected the keybinding capture dialog to open.";
          return;
        }
        auto *recorder = capture->findChild<QLineEdit *>(
            QStringLiteral("settingsKeybindingCaptureRecorder"));
        auto *use = capture->findChild<QPushButton *>(
            QStringLiteral("settingsKeybindingCaptureUse"));
        ASSERT_NE(recorder, nullptr);
        ASSERT_NE(use, nullptr);
        send_key(*recorder, key, modifiers);
        QTimer::singleShot(
            0, &owner,
            [outcome, expected_shortcut, close_error, inspect]() {
              auto *error = qobject_cast<QMessageBox *>(
                  QApplication::activeModalWidget());
              ASSERT_NE(error, nullptr);
              outcome->error_opened = true;
              EXPECT_EQ(static_cast<QtRetainedMessageBox *>(error)
                            ->presented_icon(),
                        QMessageBox::Critical);
              EXPECT_EQ(error->objectName(),
                        QStringLiteral("settingsKeybindingErrorDialog"));
              EXPECT_EQ(error->windowTitle(),
                        QStringLiteral("Keybinding Error"));
              EXPECT_TRUE(error->text().startsWith(QStringLiteral(
                  "Unable to save the keybinding change.")));
              EXPECT_TRUE(error->informativeText().isEmpty());
              ASSERT_NE(error->defaultButton(), nullptr);
              EXPECT_EQ(error->defaultButton()->text(), QStringLiteral("OK"));
              EXPECT_TRUE(error->defaultButton()->objectName().isEmpty());
              EXPECT_EQ(error->buttonRole(error->defaultButton()),
                        QMessageBox::AcceptRole);
              EXPECT_EQ(error->escapeButton(), error->defaultButton());
              if (inspect) {
                inspect(*error);
              }
              QTimer::singleShot(0, [outcome, expected_shortcut]() {
                QWidget *reopened = QApplication::activeModalWidget();
                ASSERT_NE(reopened, nullptr);
                EXPECT_EQ(reopened->objectName(),
                          QStringLiteral("settingsKeybindingCaptureDialog"));
                auto *preserved = reopened->findChild<QLineEdit *>(
                    QStringLiteral("settingsKeybindingCaptureRecorder"));
                ASSERT_NE(preserved, nullptr);
                EXPECT_EQ(preserved->text(), expected_shortcut);
                outcome->capture_preserved = true;
                qobject_cast<QDialog *>(reopened)->reject();
              });
              if (close_error) {
                error->close();
              } else {
                error->defaultButton()->click();
              }
            });
        use->click();
      });
  send_key(table, Qt::Key_Return);
  return outcome->error_opened && outcome->capture_preserved;
}

TEST(QtDoubleSpinBoxTests, TrimsZeroesAndRoundsOnlyTheDisplayToFourDecimals) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-number-format-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);

  QtDoubleSpinBox spin;
  spin.setLocale(QLocale::c());
  spin.setDecimals(8);
  spin.setRange(-100.0, 100.0);
  const std::array<std::pair<double, QString>, 7> examples = {{
      {15.0, QStringLiteral("15")},
      {15.2, QStringLiteral("15.2")},
      {14.123456, QStringLiteral("14.1235")},
      {-14.123456, QStringLiteral("-14.1235")},
      {0.000001, QStringLiteral("0")},
      {-0.000001, QStringLiteral("0")},
      {0.0, QStringLiteral("0")},
  }};
  for (const auto &[value, expected] : examples) {
    spin.setValue(value);
    EXPECT_EQ(spin.text(), expected) << value;
    EXPECT_DOUBLE_EQ(spin.value(), value);
  }
}

TEST(QtDoubleSpinBoxTests, UsesLocaleDecimalSeparatorAndLocalizedDigits) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-number-locale-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);

  QtDoubleSpinBox spin;
  spin.setDecimals(6);
  spin.setRange(-100.0, 100.0);
  spin.setLocale(QLocale(QLocale::German, QLocale::Germany));
  spin.setValue(15.2);
  EXPECT_EQ(spin.text(), QStringLiteral("15,2"));
  spin.setValue(14.123456);
  EXPECT_EQ(spin.text(), QStringLiteral("14,1235"));

  const QLocale arabic(QLocale::Arabic, QLocale::Egypt);
  spin.setLocale(arabic);
  spin.setValue(15.0);
  EXPECT_EQ(spin.text(), arabic.toString(15));
  spin.setValue(15.2);
  EXPECT_EQ(spin.text(), arabic.toString(15.2, 'f', 1));
  spin.setValue(-0.000001);
  EXPECT_EQ(spin.text(), arabic.toString(0));
}

TEST(QtDoubleSpinBoxTests, PreservesAffixesGroupingAndSpecialValueText) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-number-affix-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);

  QtDoubleSpinBox spin;
  spin.setLocale(QLocale(QLocale::German, QLocale::Germany));
  spin.setDecimals(6);
  spin.setRange(0.0, 100000.0);
  spin.setPrefix(QStringLiteral("X: "));
  spin.setSuffix(QStringLiteral(" mm"));
  spin.setValue(12345.2);
  EXPECT_EQ(spin.text(), QStringLiteral("X: 12345,2 mm"));
  EXPECT_EQ(spin.cleanText(), QStringLiteral("12345,2"));
  spin.setGroupSeparatorShown(true);
  EXPECT_EQ(spin.text(), QStringLiteral("X: 12.345,2 mm"));
  spin.setValue(12345.0);
  EXPECT_EQ(spin.text(), QStringLiteral("X: 12.345 mm"));

  spin.setSpecialValueText(QStringLiteral("Automatic"));
  spin.setValue(spin.minimum());
  EXPECT_EQ(spin.text(), QStringLiteral("Automatic"));
}

TEST(QtDoubleSpinBoxTests, CommitsLocalizedEditingAndRetainsKeyboardStepping) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-number-edit-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);

  QtDoubleSpinBox spin;
  spin.setLocale(QLocale(QLocale::German, QLocale::Germany));
  spin.setDecimals(6);
  spin.setRange(-100.0, 100.0);
  spin.setSuffix(QStringLiteral(" mm"));
  spin.setSingleStep(0.1);
  spin.setKeyboardTracking(false);
  spin.setValue(15.0);
  auto *edit = spin.findChild<QLineEdit *>();
  ASSERT_NE(edit, nullptr);
  edit->selectAll();
  edit->insert(QStringLiteral("15,2 mm"));
  EXPECT_DOUBLE_EQ(spin.value(), 15.0);
  send_key(spin, Qt::Key_Return);
  EXPECT_DOUBLE_EQ(spin.value(), 15.2);
  EXPECT_EQ(spin.text(), QStringLiteral("15,2 mm"));

  send_key(spin, Qt::Key_Up);
  EXPECT_DOUBLE_EQ(spin.value(), 15.3);
  EXPECT_EQ(spin.text(), QStringLiteral("15,3 mm"));
}

TEST(QtDoubleSpinBoxTests, UneditedFocusAndReturnPreserveMorePreciseValues) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-number-precision-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);

  QtDoubleSpinBox spin;
  spin.setLocale(QLocale::c());
  spin.setDecimals(6);
  spin.setRange(0.000001, 1.0);
  spin.setSuffix(QStringLiteral(" s"));
  std::vector<double> published_values;
  QObject::connect(&spin, &QDoubleSpinBox::valueChanged,
                   [&published_values](double value) {
                     published_values.push_back(value);
                   });
  for (const double value : {0.000001, 0.000002, 0.123456}) {
    spin.setValue(value);
    published_values.clear();
    QFocusEvent focus_in(QEvent::FocusIn, Qt::TabFocusReason);
    QCoreApplication::sendEvent(&spin, &focus_in);
    QFocusEvent focus_out(QEvent::FocusOut, Qt::TabFocusReason);
    QCoreApplication::sendEvent(&spin, &focus_out);
    EXPECT_DOUBLE_EQ(spin.value(), value);
    EXPECT_TRUE(published_values.empty());
    send_key(spin, Qt::Key_Return);
    EXPECT_DOUBLE_EQ(spin.value(), value);
    // Native Qt Return can republish the current value with keyboard tracking.
    for (const double published : published_values) {
      EXPECT_DOUBLE_EQ(published, value);
    }
  }
}

TEST(QtDoubleSpinBoxTests,
     RepeatedCommitPreservesPublishedPrecisionWhenTheDisplayIsUnchanged) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-number-republication-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);

  QtDoubleSpinBox spin;
  spin.setLocale(QLocale::c());
  spin.setDecimals(6);
  spin.setRange(0.0, 1.0);
  spin.setKeyboardTracking(false);
  auto *edit = spin.findChild<QLineEdit *>();
  ASSERT_NE(edit, nullptr);
  edit->selectAll();
  edit->insert(QStringLiteral("0.1235"));
  send_key(spin, Qt::Key_Return);
  ASSERT_DOUBLE_EQ(spin.value(), 0.1235);
  ASSERT_EQ(spin.text(), QStringLiteral("0.1235"));

  // An owner refresh can publish a more precise value with identical text.
  spin.setValue(0.123456);
  ASSERT_EQ(spin.text(), QStringLiteral("0.1235"));
  send_key(spin, Qt::Key_Return);
  EXPECT_DOUBLE_EQ(spin.value(), 0.123456);
}

TEST(QtDoubleSpinBoxTests, ExplicitSameDisplayEditingCommitsTheEnteredValue) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-number-explicit-rounding-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);

  QtDoubleSpinBox spin;
  spin.setLocale(QLocale::c());
  spin.setDecimals(6);
  spin.setRange(0.0, 1.0);
  spin.setKeyboardTracking(false);
  spin.setValue(0.123456);
  ASSERT_EQ(spin.text(), QStringLiteral("0.1235"));
  auto *edit = spin.findChild<QLineEdit *>();
  ASSERT_NE(edit, nullptr);
  edit->selectAll();
  edit->insert(QStringLiteral("0.1235"));
  ASSERT_DOUBLE_EQ(spin.value(), 0.123456);
  send_key(spin, Qt::Key_Return);
  EXPECT_DOUBLE_EQ(spin.value(), 0.1235);
}

TEST(QtDoubleSpinBoxTests,
     SignalBlockedOwnerRefreshSupersedesPendingEditsAndRetainsPrecision) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-number-blocked-refresh-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);

  for (const QString &pending_text :
       {QStringLiteral("0.2"), QStringLiteral("0.1235")}) {
    SCOPED_TRACE(pending_text.toStdString());
    QtDoubleSpinBox spin;
    spin.setLocale(QLocale::c());
    spin.setDecimals(6);
    spin.setRange(0.0, 1.0);
    spin.setKeyboardTracking(false);
    spin.setValue(0.1);
    auto *edit = spin.findChild<QLineEdit *>();
    ASSERT_NE(edit, nullptr);
    edit->selectAll();
    edit->insert(pending_text);
    ASSERT_DOUBLE_EQ(spin.value(), 0.1);

    // Retained presenters replace drafts without publishing widget signals.
    {
      const QSignalBlocker blocker(spin);
      spin.setValue(0.123456);
    }
    ASSERT_EQ(spin.text(), QStringLiteral("0.1235"));
    send_key(spin, Qt::Key_Return);
    EXPECT_DOUBLE_EQ(spin.value(), 0.123456);
  }
}

TEST(QtSettingsDialogTests,
     CategoriesContainOnlyPreferencesWithoutDuplicateEditorControls) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-settings-category-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  UISettings ui_settings = UISettings::defaults();
  ViewportSettings viewport_settings = ViewportSettings::defaults();
  LocalizationService localization = loaded_localization();
  QtSettingsDialog dialog;
  dialog.setAttribute(Qt::WA_DontShowOnScreen, true);
  dialog.set_context(ui_settings, viewport_settings, localization,
                     editor.commands(), {});

  auto *categories = dialog.findChild<QListWidget *>(
      QStringLiteral("settingsCategories"));
  auto *pages = dialog.findChild<QStackedWidget *>(
      QStringLiteral("settingsPages"));
  ASSERT_NE(categories, nullptr);
  ASSERT_NE(pages, nullptr);
  const QStringList expected_ids = {
      QStringLiteral("interface"), QStringLiteral("navigation"),
      QStringLiteral("keybindings"), QStringLiteral("viewport"),
      QStringLiteral("appearance"), QStringLiteral("editing"),
      QStringLiteral("save_recovery"),
  };
  const QString capture_directory = QString::fromUtf8(
      qEnvironmentVariableIsSet("QUADER_UI_REVIEW_DIR")
          ? qgetenv("QUADER_UI_REVIEW_DIR")
          : qgetenv("QUADER_SETTINGS_CAPTURE_DIR"));
  const auto capture_pages = [&](const QString &locale) {
    if (capture_directory.isEmpty()) {
      return;
    }
    ASSERT_TRUE(QDir().mkpath(capture_directory));
    const QDir output(capture_directory);
    dialog.show();
    for (int index = 0; index < categories->count(); ++index) {
      categories->setCurrentRow(index);
      QCoreApplication::processEvents();
      const QString path = output.filePath(
          QStringLiteral("settings-") + locale + QStringLiteral("_") + expected_ids[index] +
          QStringLiteral(".png"));
      EXPECT_TRUE(dialog.grab().save(path)) << path.toStdString();
    }
  };
  ASSERT_EQ(categories->count(), expected_ids.size());
  ASSERT_EQ(pages->count(), categories->count());
  for (int index = 0; index < categories->count(); ++index) {
    EXPECT_EQ(categories->item(index)->data(Qt::UserRole).toString(),
              expected_ids[index]);
    categories->setCurrentRow(index);
    EXPECT_EQ(pages->currentWidget()->objectName(),
              QStringLiteral("settingsPage_%1").arg(expected_ids[index]));
  }

  const auto expect_group = [&](const char *control_name,
                                const char *group_name,
                                const char *page_name) {
    auto *control = dialog.findChild<QWidget *>(QString::fromUtf8(control_name));
    auto *group = dialog.findChild<QWidget *>(QString::fromUtf8(group_name));
    auto *page = dialog.findChild<QWidget *>(QString::fromUtf8(page_name));
    ASSERT_NE(control, nullptr) << control_name;
    ASSERT_NE(group, nullptr) << group_name;
    ASSERT_NE(page, nullptr) << page_name;
    EXPECT_TRUE(group->isAncestorOf(control)) << control_name;
    EXPECT_TRUE(page->isAncestorOf(group)) << group_name;
  };
  expect_group("settingsVSync", "settingsGroup_display",
                "settingsPage_viewport");
  expect_group("settingsDefaultMaterialBrowse", "settingsGroup_default_material",
                "settingsPage_viewport");
  expect_group("settingsUndoSteps", "settingsGroup_undo",
                "settingsPage_editing");
  expect_group("settingsAutosaveEnabled", "settingsGroup_autosave",
                "settingsPage_save_recovery");
  expect_group("viewportSetting_DefaultFlyNavigationSpeed",
                "settingsGroup_navigation", "settingsPage_navigation");
  expect_group("viewportSetting_GridMajorLineSize", "settingsGroup_grid",
                "settingsPage_appearance");
  expect_group("viewportSetting_Grid2DIntensity", "settingsGroup_grid",
                "settingsPage_appearance");
  expect_group("viewportSetting_Grid3DIntensity", "settingsGroup_grid",
                "settingsPage_appearance");
  expect_group("viewportSetting_SelectionFaceColor", "settingsGroup_selection",
                "settingsPage_appearance");
  expect_group("viewportSetting_ActiveSelectionColor", "settingsGroup_selection",
                "settingsPage_appearance");
  expect_group("viewportSetting_ActiveSelectionLineSize", "settingsGroup_selection",
                "settingsPage_appearance");
  expect_group("viewportSetting_PickVertexRadius", "settingsGroup_picking",
                "settingsPage_viewport");
  expect_group("viewportSetting_GizmoSize", "settingsGroup_gizmo",
                "settingsPage_viewport");
  expect_group("viewportSetting_BackgroundColor", "settingsGroup_shading",
                "settingsPage_viewport");
  expect_group("settingsLocale", "settingsGroup_language",
                "settingsPage_interface");
  expect_group("settingsInterfaceScale", "settingsGroup_interface_scale",
                "settingsPage_interface");
  auto *language_group = dialog.findChild<QWidget *>(
      QStringLiteral("settingsGroup_language"));
  auto *scale_group = dialog.findChild<QWidget *>(
      QStringLiteral("settingsGroup_interface_scale"));
  auto *picking_group = dialog.findChild<QWidget *>(
      QStringLiteral("settingsGroup_picking"));
  auto *navigation_group = dialog.findChild<QWidget *>(
      QStringLiteral("settingsGroup_navigation"));
  auto *selection_group = dialog.findChild<QWidget *>(
      QStringLiteral("settingsGroup_selection"));
  ASSERT_NE(language_group, nullptr);
  ASSERT_NE(scale_group, nullptr);
  ASSERT_NE(picking_group, nullptr);
  ASSERT_NE(navigation_group, nullptr);
  ASSERT_NE(selection_group, nullptr);
  EXPECT_EQ(settings_section_title(*language_group), QStringLiteral("Language"));
  EXPECT_EQ(settings_section_title(*scale_group), QStringLiteral("Interface Scaling"));
  EXPECT_EQ(settings_section_title(*picking_group), QStringLiteral("Picking"));
  EXPECT_EQ(settings_section_title(*navigation_group), QStringLiteral("Fly Navigation"));
  EXPECT_EQ(settings_section_title(*selection_group), QStringLiteral("Selection Appearance"));
  EXPECT_EQ(dialog.findChild<QWidget *>(
                QStringLiteral("settingsGroup_snapping")),
            nullptr);
  EXPECT_EQ(dialog.findChild<QWidget *>(
                QStringLiteral("settingsGroup_texturing")),
            nullptr);
  auto *editing_page = dialog.findChild<QWidget *>(
      QStringLiteral("settingsPage_editing"));
  ASSERT_NE(editing_page, nullptr);
  const auto editing_groups = settings_sections(*editing_page);
  ASSERT_EQ(editing_groups.size(), 1);
  EXPECT_EQ(editing_groups.front()->objectName(),
            QStringLiteral("settingsGroup_undo"));
  for (auto *group : settings_sections(dialog)) {
    EXPECT_TRUE(group->findChild<QCheckBox *>() != nullptr ||
                group->findChild<QSpinBox *>() != nullptr ||
                group->findChild<QDoubleSpinBox *>() != nullptr ||
                group->findChild<QComboBox *>() != nullptr ||
                group->findChild<QPushButton *>() != nullptr ||
                group->findChild<QTableView *>() != nullptr)
        << "Settings must not retain an empty group: "
        << group->objectName().toStdString();
  }

  const auto expect_control_count = [&](const auto &descriptor,
                                       qsizetype expected_count) {
    const QString name = QStringLiteral("viewportSetting_%1").arg(
        QString::fromUtf8(descriptor.key.data(),
                          static_cast<qsizetype>(descriptor.key.size())));
    EXPECT_EQ(dialog.findChildren<QWidget *>(name).size(), expected_count)
        << descriptor.key;
  };
  for (const auto &descriptor : viewport_color_settings()) {
    expect_control_count(descriptor, 1);
  }
  for (const auto &descriptor : viewport_float_settings()) {
    expect_control_count(
        descriptor,
        descriptor.id == ViewportFloatSettingId::RotationSnapDegrees ? 0 : 1);
  }
  for (const auto &descriptor : viewport_bool_settings()) {
    expect_control_count(
        descriptor,
        descriptor.id == ViewportBoolSettingId::ShowHoverDimensions ? 1 : 0);
  }
  capture_pages(QStringLiteral("en-US"));

  const auto original_pages = pages->findChildren<QWidget *>();
  auto *undo_group = dialog.findChild<QWidget *>(
      QStringLiteral("settingsGroup_undo"));
  ASSERT_NE(undo_group, nullptr);
  ASSERT_TRUE(localization.select_locale("it-IT"));
  ui_settings.set_locale("it-IT");
  dialog.refresh();
  EXPECT_EQ(categories->item(5)->text(), QStringLiteral("Modifica"));
  EXPECT_EQ(settings_section_title(*undo_group), QStringLiteral("Cronologia annullamenti"));
  EXPECT_EQ(settings_section_title(*selection_group),
            QStringLiteral("Aspetto della selezione"));
  EXPECT_EQ(pages->findChildren<QWidget *>(), original_pages)
      << "localization must update the retained pages in place";
  capture_pages(QStringLiteral("it-IT"));
  dialog.reject();
}

TEST(QtSettingsDialogTests,
     DisclosureRetainsDraftAndAlignedFieldsAcrossScaleAndLocale) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-settings-section-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  QApplication::setStyle(QStringLiteral("Fusion"));
  apply_qt_obsidian_application_theme(application.palette());
  QtUiScaleApplicationController scale_controller(application);
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  UISettings settings = UISettings::defaults();
  ViewportSettings viewport = ViewportSettings::defaults();
  LocalizationService localization = loaded_localization();
  std::vector<QtSettingsChange> changes;
  QtSettingsDialog dialog;
  dialog.setAttribute(Qt::WA_DontShowOnScreen, true);
  scale_controller.add_change_listener(
      dialog, [&](const QtUiScaleMetrics &metrics) {
        dialog.apply_ui_scale(metrics);
      });
  dialog.set_context(settings, viewport, localization, editor.commands(),
      [&](const QtSettingsChange &change) { changes.push_back(change); },
      []() { return true; });
  auto *categories = dialog.findChild<QListWidget *>(
      QStringLiteral("settingsCategories"));
  auto *pages = dialog.findChild<QStackedWidget *>(
      QStringLiteral("settingsPages"));
  auto *undo = dialog.findChild<QSpinBox *>(
      QStringLiteral("settingsUndoSteps"));
  auto *header = dialog.findChild<QToolButton *>(
      QStringLiteral("settingsGroup_undo.header"));
  auto *apply = dialog.findChild<QPushButton *>(
      QStringLiteral("settingsApply"));
  ASSERT_NE(categories, nullptr);
  ASSERT_NE(pages, nullptr);
  ASSERT_NE(undo, nullptr);
  ASSERT_NE(header, nullptr);
  ASSERT_NE(apply, nullptr);
  dialog.show();
  const auto settle = [&]() {
    for (int iteration = 0; iteration < 4; ++iteration) {
      QCoreApplication::processEvents();
      dialog.layout()->activate();
    }
  };
  std::vector<QPointer<QWidget>> original_controls;
  for (QWidget *control : pages->findChildren<QWidget *>()) {
    original_controls.emplace_back(control);
  }
  for (const float scale : {1.0F, 2.0F, 1.0F}) {
    scale_controller.apply_factor(scale);
    dialog.resize(scale_controller.metrics().size(QSize(720, 560)));
    for (const char *locale : {"en-US", "it-IT"}) {
      ASSERT_TRUE(localization.select_locale(locale));
      dialog.refresh();
      for (int category : {0, 3, 4, 6}) {
        categories->setCurrentRow(category);
        settle();
        int field_left = -1;
        for (auto *section : settings_sections(*pages->currentWidget())) {
          auto *section_header = section->findChild<QToolButton *>(
              section->objectName() + QStringLiteral(".header"));
          ASSERT_NE(section_header, nullptr);
          const int section_inset = scale_controller.metrics().px(1);
          EXPECT_EQ(section_header->width(),
                    section->width() - 2 * section_inset);
          EXPECT_EQ(section_header->mapTo(section, QPoint()),
                    QPoint(section_inset, section_inset));
          EXPECT_EQ(section_header->geometry().left(),
                    section->content()->geometry().left());
          EXPECT_EQ(section_header->width(), section->content()->width());
          EXPECT_GE(section_header->height(),
                    scale_controller.metrics().px(30));
          auto *form = qobject_cast<QFormLayout *>(section->content()->layout());
          ASSERT_NE(form, nullptr);
          for (int row = 0; row < form->rowCount(); ++row) {
            auto *label_item = form->itemAt(row, QFormLayout::LabelRole);
            auto *field_item = form->itemAt(row, QFormLayout::FieldRole);
            if (label_item == nullptr || field_item == nullptr ||
                label_item->widget() == nullptr ||
                field_item->widget() == nullptr) {
              continue;
            }
            QWidget *label = label_item->widget();
            QWidget *field = field_item->widget();
            const int left = field->mapTo(pages->currentWidget(), QPoint()).x();
            if (field_left < 0) {
              field_left = left;
            }
            EXPECT_EQ(left, field_left);
            EXPECT_LT(label->geometry().right(), field->geometry().left());
            EXPECT_GE(field->width(), scale_controller.metrics().px(88));
          }
        }
        const QString output = QString::fromUtf8(qgetenv("QUADER_UI_REVIEW_DIR"));
        if (!output.isEmpty()) {
          ASSERT_TRUE(QDir().mkpath(output));
          const QString filename = QStringLiteral("settings-%1-%2-%3.png")
              .arg(QString::fromUtf8(locale))
              .arg(categories->currentItem()->data(Qt::UserRole).toString())
              .arg(static_cast<int>(scale * 100.0F));
          EXPECT_TRUE(dialog.grab().save(QDir(output).filePath(filename)));
        }
      }
    }
  }
  const auto retained_controls = pages->findChildren<QWidget *>();
  EXPECT_EQ(retained_controls.size(), original_controls.size());
  // Raising the active stacked page changes QObject child traversal order.
  // Every original control must still be alive in the retained page hierarchy.
  for (const QPointer<QWidget> &control : original_controls) {
    ASSERT_NE(control.data(), nullptr);
    EXPECT_TRUE(retained_controls.contains(control.data()));
  }
  categories->setCurrentRow(5);
  settle();
  const int original_undo_steps = settings.session_settings().undo_steps;
  undo->setValue(17);
  header->click();
  settle();
  EXPECT_FALSE(header->isChecked());
  EXPECT_FALSE(undo->isVisible());
  EXPECT_EQ(settings.session_settings().undo_steps, original_undo_steps);
  EXPECT_TRUE(changes.empty());
  dialog.refresh();
  EXPECT_FALSE(header->isChecked());
  EXPECT_EQ(undo->value(), 17);
  header->click();
  settle();
  EXPECT_TRUE(undo->isVisible());
  EXPECT_EQ(dialog.findChild<QSpinBox *>(QStringLiteral("settingsUndoSteps")),
            undo);
  apply->click();
  EXPECT_EQ(settings.session_settings().undo_steps, 17);
  EXPECT_EQ(count_changes(changes, QtSettingsChangeKind::SessionCommit), 1u);
  undo->setValue(23);
  header->click();
  dialog.reject();
  EXPECT_EQ(settings.session_settings().undo_steps, 17);
}

TEST(QtSettingsDialogTests,
     FloatingPreferencesUseConsistentStepsPrecisionAndUnitControlSnap) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-settings-numeric-step-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  UISettings ui_settings = UISettings::defaults();
  ViewportSettings viewport_settings = ViewportSettings::defaults();
  LocalizationService localization = loaded_localization();
  QtSettingsDialog dialog;
  dialog.setAttribute(Qt::WA_DontShowOnScreen, true);
  dialog.set_context(ui_settings, viewport_settings, localization,
                     editor.commands(), {});
  qt_set_numeric_snap_increment_provider(application, dialog,
                                         []() { return 8.0; });
  auto *pages = dialog.findChild<QStackedWidget *>(
      QStringLiteral("settingsPages"));
  ASSERT_NE(pages, nullptr);
  dialog.show();
  QCoreApplication::processEvents();

  const auto show_field = [&](QWidget &field) {
    for (int page = 0; page < pages->count(); ++page) {
      if (pages->widget(page)->isAncestorOf(&field)) {
        pages->setCurrentIndex(page);
        QCoreApplication::processEvents();
        return;
      }
    }
    ADD_FAILURE() << "Missing Settings page for "
                  << field.objectName().toStdString();
  };
  const auto scrub = [](QAbstractSpinBox &field, int pixels,
                         Qt::KeyboardModifiers modifiers) {
    auto *line = field.findChild<QLineEdit *>();
    ASSERT_NE(line, nullptr);
    const auto send_mouse = [&](QEvent::Type type, QPoint position,
                                Qt::MouseButton button,
                                Qt::MouseButtons buttons) {
      QMouseEvent event(type, QPointF(position),
                        QPointF(line->mapToGlobal(position)), button, buttons,
                        modifiers);
      QCoreApplication::sendEvent(line, &event);
    };
    const QPoint center = line->rect().center();
    const QPoint origin =
        center + QPoint(QApplication::startDragDistance() + 1, 0);
    send_mouse(QEvent::MouseButtonPress, center, Qt::LeftButton,
                 Qt::LeftButton);
    send_mouse(QEvent::MouseMove, origin, Qt::NoButton, Qt::LeftButton);
    ASSERT_TRUE(qt_install_numeric_spin_box(field).is_dragging());
    const QPoint destination = origin + QPoint(pixels, 0);
    send_mouse(QEvent::MouseMove, destination, Qt::NoButton, Qt::LeftButton);
    send_mouse(QEvent::MouseButtonRelease, destination, Qt::LeftButton,
                 Qt::NoButton);
  };

  const auto floating_fields = dialog.findChildren<QDoubleSpinBox *>();
  ASSERT_EQ(floating_fields.size(), kViewportFloatSettingCount - 1u);
  for (auto *field : floating_fields) {
    SCOPED_TRACE(field->objectName().toStdString());
    show_field(*field);
    ASSERT_TRUE(field->isVisible());
    ASSERT_TRUE(field->isEnabled());
    EXPECT_DOUBLE_EQ(field->singleStep(), 0.1);
    field->setValue(std::max(0.26, field->minimum() + 0.26));
    const double initial = field->value();
    field->stepUp();
    EXPECT_NEAR(field->value(), initial + 0.1, 0.000001);

    field->setValue(initial);
    scrub(*field, 1, Qt::NoModifier);
    EXPECT_NEAR(field->value(), initial + 0.1, 0.000001);
    field->setValue(initial);
    scrub(*field, 1, Qt::ShiftModifier);
    EXPECT_NEAR(field->value(), initial + 0.01, 0.000001);
    field->setValue(initial);
    scrub(*field, 4, Qt::ControlModifier);
    EXPECT_NEAR(field->value(),
                std::clamp(std::round(initial + 0.4), field->minimum(),
                           field->maximum()),
                0.000001);
  }

  auto *autosave = dialog.findChild<QCheckBox *>(
      QStringLiteral("settingsAutosaveEnabled"));
  ASSERT_NE(autosave, nullptr);
  autosave->setChecked(true);
  for (auto *field : dialog.findChildren<QSpinBox *>()) {
    SCOPED_TRACE(field->objectName().toStdString());
    show_field(*field);
    ASSERT_TRUE(field->isEnabled());
    field->setValue(field->minimum() + 5);
    const int initial = field->value();
    // Engaging Ctrl must retain an existing whole value rather than snap it to
    // the unrelated viewport grid, even before any further pointer travel.
    scrub(*field, 0, Qt::ControlModifier);
    EXPECT_EQ(field->value(), initial);
  }
  dialog.reject();
}

TEST(QtSettingsDialogTests,
     ExternalViewportControlsSurviveApplyAndCancelOfPreferenceDrafts) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-settings-external-controls-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  UISettings ui_settings = UISettings::defaults();
  ViewportSettings viewport_settings = ViewportSettings::defaults();
  const ViewportSettings initial_viewport_settings = viewport_settings;
  LocalizationService localization = loaded_localization();
  std::vector<QtSettingsChange> changes;
  QtSettingsDialog dialog;
  dialog.setAttribute(Qt::WA_DontShowOnScreen, true);
  dialog.set_context(
      ui_settings, viewport_settings, localization, editor.commands(),
      [&](const QtSettingsChange &change) { changes.push_back(change); });
  auto *hover_dimensions = dialog.findChild<QCheckBox *>(
      QStringLiteral("viewportSetting_ShowHoverDimensions"));
  auto *apply = dialog.findChild<QPushButton *>(QStringLiteral("settingsApply"));
  ASSERT_NE(hover_dimensions, nullptr);
  ASSERT_NE(apply, nullptr);
  dialog.show();
  QCoreApplication::processEvents();
  ASSERT_TRUE(hover_dimensions->isChecked());
  hover_dimensions->setChecked(false);

  // Menu and toolbar actions publish directly to this shared settings owner.
  // Applying an unrelated dialog draft must preserve even unrefreshed changes.
  for (const auto &descriptor : viewport_bool_settings()) {
    if (descriptor.id != ViewportBoolSettingId::ShowHoverDimensions) {
      ASSERT_TRUE(viewport_settings.set_bool_value(
          descriptor.id, !initial_viewport_settings.bool_value(descriptor.id)));
    }
  }
  ASSERT_TRUE(viewport_settings.set_float_value(
      ViewportFloatSettingId::RotationSnapDegrees, 45.0f));
  apply->click();
  EXPECT_FALSE(viewport_settings.bool_value(
      ViewportBoolSettingId::ShowHoverDimensions));
  for (const auto &descriptor : viewport_bool_settings()) {
    if (descriptor.id != ViewportBoolSettingId::ShowHoverDimensions) {
      EXPECT_EQ(viewport_settings.bool_value(descriptor.id),
                !initial_viewport_settings.bool_value(descriptor.id))
          << descriptor.key;
    }
  }
  EXPECT_FLOAT_EQ(viewport_settings.float_value(
                      ViewportFloatSettingId::RotationSnapDegrees),
                  45.0f);
  ASSERT_EQ(changes.size(), 1u);
  EXPECT_EQ(changes.front().kind, QtSettingsChangeKind::ViewportCommit);
  EXPECT_FALSE(apply->isEnabled());

  hover_dimensions->setChecked(true);
  for (const auto &descriptor : viewport_bool_settings()) {
    if (descriptor.id != ViewportBoolSettingId::ShowHoverDimensions) {
      ASSERT_TRUE(viewport_settings.set_bool_value(
          descriptor.id, initial_viewport_settings.bool_value(descriptor.id)));
    }
  }
  ASSERT_TRUE(viewport_settings.set_float_value(
      ViewportFloatSettingId::RotationSnapDegrees, 90.0f));
  dialog.refresh();
  EXPECT_TRUE(hover_dimensions->isChecked());
  EXPECT_TRUE(apply->isEnabled());
  dialog.reject();
  EXPECT_FALSE(viewport_settings.bool_value(
      ViewportBoolSettingId::ShowHoverDimensions));
  for (const auto &descriptor : viewport_bool_settings()) {
    if (descriptor.id != ViewportBoolSettingId::ShowHoverDimensions) {
      EXPECT_EQ(viewport_settings.bool_value(descriptor.id),
                initial_viewport_settings.bool_value(descriptor.id))
          << descriptor.key;
    }
  }
  EXPECT_FLOAT_EQ(viewport_settings.float_value(
                      ViewportFloatSettingId::RotationSnapDegrees),
                  90.0f);
  EXPECT_EQ(changes.size(), 1u);
}

TEST(QtSettingsDialogTests,
     UndoAndAutosaveStageApplyCancelAndMergeUntouchedExternalValues) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-settings-session-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  UISettings ui_settings = UISettings::defaults();
  ViewportSettings viewport_settings = ViewportSettings::defaults();
  LocalizationService localization = loaded_localization();
  std::vector<QtSettingsChange> changes;
  std::vector<SessionPreferences> persisted;
  QtSettingsDialog dialog;
  dialog.setAttribute(Qt::WA_DontShowOnScreen, true);
  dialog.set_context(
      ui_settings, viewport_settings, localization, editor.commands(),
      [&](const QtSettingsChange &change) { changes.push_back(change); },
      [&]() {
        persisted.push_back(ui_settings.session_settings());
        return true;
      });
  auto *undo = dialog.findChild<QSpinBox *>(QStringLiteral("settingsUndoSteps"));
  auto *enabled = dialog.findChild<QCheckBox *>(
      QStringLiteral("settingsAutosaveEnabled"));
  auto *interval = dialog.findChild<QSpinBox *>(
      QStringLiteral("settingsAutosaveInterval"));
  auto *copies = dialog.findChild<QSpinBox *>(
      QStringLiteral("settingsAutosaveMaxFiles"));
  auto *apply = dialog.findChild<QPushButton *>(QStringLiteral("settingsApply"));
  ASSERT_NE(undo, nullptr);
  ASSERT_NE(enabled, nullptr);
  ASSERT_NE(interval, nullptr);
  ASSERT_NE(copies, nullptr);
  ASSERT_NE(apply, nullptr);
  EXPECT_EQ(dialog.findChild<QPushButton *>(
                QStringLiteral("settingsRecoverAutosave")), nullptr);
  EXPECT_EQ(dialog.findChild<QWidget *>(
                QStringLiteral("settingsGroup_recovery")), nullptr);
  EXPECT_EQ(undo->value(), 32);
  EXPECT_EQ(undo->minimum(), 0);
  EXPECT_EQ(undo->maximum(), 256);
  EXPECT_TRUE(enabled->isChecked());
  EXPECT_EQ(interval->value(), 2);
  EXPECT_EQ(interval->minimum(), 1);
  EXPECT_EQ(interval->maximum(), 60);
  EXPECT_EQ(copies->value(), 3);
  EXPECT_EQ(copies->maximum(), 20);
  EXPECT_FALSE(undo->accessibleName().isEmpty());
  EXPECT_EQ(undo->accessibleDescription(), undo->toolTip());
  dialog.show();
  QCoreApplication::processEvents();

  undo->setValue(8);
  interval->setValue(5);
  copies->setValue(4);
  enabled->setChecked(false);
  EXPECT_FALSE(interval->isEnabled());
  EXPECT_FALSE(copies->isEnabled());
  EXPECT_TRUE(changes.empty());
  EXPECT_TRUE(persisted.empty());
  EXPECT_EQ(ui_settings.session_settings(), SessionPreferences{});
  EXPECT_TRUE(apply->isEnabled());

  apply->click();
  const SessionPreferences applied{.undo_steps = 8,
                                   .autosave_enabled = false,
                                   .autosave_interval_minutes = 5,
                                   .autosave_max_files = 4};
  EXPECT_EQ(ui_settings.session_settings(), applied);
  ASSERT_EQ(persisted.size(), 1u);
  EXPECT_EQ(persisted.back(), applied);
  ASSERT_EQ(changes.size(), 1u);
  EXPECT_EQ(changes.back().kind, QtSettingsChangeKind::SessionCommit);
  EXPECT_EQ(changes.back().session_settings, applied);
  EXPECT_FALSE(apply->isEnabled());
  undo->setValue(0);
  enabled->setChecked(true);
  EXPECT_TRUE(interval->isEnabled());
  dialog.reject();
  EXPECT_EQ(ui_settings.session_settings(), applied);
  EXPECT_EQ(changes.size(), 1u);

  dialog.show();
  QCoreApplication::processEvents();
  undo->setValue(4);
  SessionPreferences externally_changed = applied;
  externally_changed.autosave_max_files = 7;
  ASSERT_TRUE(ui_settings.set_session_settings(externally_changed));
  dialog.refresh();
  EXPECT_EQ(undo->value(), 4);
  EXPECT_EQ(copies->value(), 7);
  apply->click();
  EXPECT_EQ(ui_settings.session_settings().undo_steps, 4);
  EXPECT_EQ(ui_settings.session_settings().autosave_max_files, 7);
  EXPECT_EQ(count_changes(changes, QtSettingsChangeKind::SessionCommit), 2u);

  undo->setValue(0);
  externally_changed = ui_settings.session_settings();
  externally_changed.autosave_interval_minutes = 9;
  ASSERT_TRUE(ui_settings.set_session_settings(externally_changed));
  dialog.synchronize_session_settings();
  EXPECT_EQ(undo->value(), 0)
      << "an external autosave commit must preserve a staged undo limit";
  EXPECT_EQ(interval->value(), 9);
  EXPECT_TRUE(apply->isEnabled());
  externally_changed.undo_steps = 16;
  ASSERT_TRUE(ui_settings.set_session_settings(externally_changed));
  dialog.synchronize_session_settings();
  EXPECT_EQ(undo->value(), 16);
  EXPECT_FALSE(apply->isEnabled());
  dialog.reject();
}

TEST(QtSettingsDialogTests,
     FailedSessionPreferencePersistenceKeepsRuntimeHistoryUntouched) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-settings-session-persistence-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  UISettings ui_settings = UISettings::defaults();
  ViewportSettings viewport_settings = ViewportSettings::defaults();
  LocalizationService localization = loaded_localization();
  bool persistence_succeeds = false;
  std::vector<QtSettingsChange> changes;
  QtSettingsDialog dialog;
  dialog.setAttribute(Qt::WA_DontShowOnScreen, true);
  dialog.set_context(
      ui_settings, viewport_settings, localization, editor.commands(),
      [&](const QtSettingsChange &change) { changes.push_back(change); },
      [&]() { return persistence_succeeds; });
  auto *undo = dialog.findChild<QSpinBox *>(QStringLiteral("settingsUndoSteps"));
  auto *apply = dialog.findChild<QPushButton *>(QStringLiteral("settingsApply"));
  ASSERT_NE(undo, nullptr);
  ASSERT_NE(apply, nullptr);
  dialog.show();
  QCoreApplication::processEvents();
  undo->setValue(0);
  apply->click();
  EXPECT_EQ(ui_settings.session_settings(), SessionPreferences{});
  EXPECT_TRUE(changes.empty());
  EXPECT_EQ(undo->value(), 0);
  EXPECT_TRUE(apply->isEnabled());
  auto *error = dynamic_cast<QtRetainedMessageBox *>(
      dialog.findChild<QWidget *>(QStringLiteral("settingsPersistenceErrorDialog")));
  ASSERT_NE(error, nullptr);
  EXPECT_TRUE(error->isVisible());
  error->done(QDialog::Accepted);
  persistence_succeeds = true;
  apply->click();
  EXPECT_EQ(ui_settings.session_settings().undo_steps, 0);
  ASSERT_EQ(changes.size(), 1u);
  EXPECT_EQ(changes.back().kind, QtSettingsChangeKind::SessionCommit);
  dialog.reject();
}

TEST(QtSettingsDialogTests,
     ActiveSelectionAppearanceStagesColorAndWidthUntilApply) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-settings-active-selection-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  UISettings ui_settings = UISettings::defaults();
  ViewportSettings viewport_settings = ViewportSettings::defaults();
  LocalizationService localization = loaded_localization();
  const ViewportSettingColor initial_color =
      viewport_settings.color(ViewportColorSettingId::ActiveSelection);
  const float initial_width = viewport_settings.float_value(
      ViewportFloatSettingId::ActiveSelectionLineSize);
  EXPECT_TRUE(viewport_setting_colors_approximately(
      initial_color, setting_color(QColor(255, 105, 180, 255))));
  EXPECT_FLOAT_EQ(initial_width, 2.5f);

  const auto color_descriptors = viewport_color_settings();
  const auto color_descriptor = std::find_if(
      color_descriptors.begin(), color_descriptors.end(), [](const auto &entry) {
        return entry.id == ViewportColorSettingId::ActiveSelection;
      });
  ASSERT_NE(color_descriptor, color_descriptors.end());
  EXPECT_EQ(color_descriptor->key, "ActiveSelectionColor");
  EXPECT_EQ(color_descriptor->section, ViewportSettingsSectionId::Selection);
  const auto &width_descriptors = viewport_float_settings();
  const auto width_descriptor = std::find_if(
      width_descriptors.begin(), width_descriptors.end(), [](const auto &entry) {
        return entry.id == ViewportFloatSettingId::ActiveSelectionLineSize;
      });
  ASSERT_NE(width_descriptor, width_descriptors.end());
  EXPECT_EQ(width_descriptor->key, "ActiveSelectionLineSize");
  EXPECT_EQ(width_descriptor->section, ViewportSettingsSectionId::Selection);

  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  std::vector<QtSettingsChange> changes;
  QtSettingsDialog dialog;
  dialog.setAttribute(Qt::WA_DontShowOnScreen, true);
  dialog.set_context(
      ui_settings, viewport_settings, localization, editor.commands(),
      [&](const QtSettingsChange &change) { changes.push_back(change); });
  auto *swatch = dialog.findChild<QPushButton *>(
      QStringLiteral("viewportSetting_ActiveSelectionColor"));
  auto *width = dialog.findChild<QDoubleSpinBox *>(
      QStringLiteral("viewportSetting_ActiveSelectionLineSize"));
  auto *apply = dialog.findChild<QPushButton *>(QStringLiteral("settingsApply"));
  auto *cancel = dialog.findChild<QPushButton *>(QStringLiteral("settingsCancel"));
  ASSERT_NE(swatch, nullptr);
  ASSERT_NE(width, nullptr);
  ASSERT_NE(apply, nullptr);
  ASSERT_NE(cancel, nullptr);
  EXPECT_EQ(swatch->accessibleName(), QStringLiteral("Active Selection Color"));
  EXPECT_EQ(width->accessibleName(), QStringLiteral("Active Selection Width"));
  EXPECT_DOUBLE_EQ(width->minimum(), 0.25);
  EXPECT_DOUBLE_EQ(width->maximum(), 8.0);
  EXPECT_DOUBLE_EQ(width->value(), initial_width);
  EXPECT_EQ(width->parentWidget(), swatch->parentWidget());
  auto *section = dynamic_cast<qt_settings_detail::QtSettingsSection *>(
      swatch->parentWidget()->parentWidget());
  ASSERT_NE(section, nullptr);
  EXPECT_EQ(settings_section_title(*section), QStringLiteral("Selection Appearance"));
  auto *categories = dialog.findChild<QListWidget *>(
      QStringLiteral("settingsCategories"));
  ASSERT_NE(categories, nullptr);
  int appearance_row = -1;
  for (int row = 0; row < categories->count(); ++row) {
    if (categories->item(row)->data(Qt::UserRole).toString() ==
        QStringLiteral("appearance")) {
      appearance_row = row;
      break;
    }
  }
  ASSERT_GE(appearance_row, 0);
  categories->setCurrentRow(appearance_row);
  section->set_expanded(true);

  const QString picker_name =
      QStringLiteral("viewportSetting_ActiveSelectionColor_dialog");
  const QColor applied_color(51, 102, 204, 230);
  dialog.show();
  QCoreApplication::processEvents();
  EXPECT_TRUE(swatch->isVisible());
  EXPECT_TRUE(width->isVisible());
  ASSERT_TRUE(accept_color_picker(dialog, *swatch, picker_name, applied_color));
  width->setValue(5.0);
  EXPECT_TRUE(apply->isEnabled());
  EXPECT_TRUE(changes.empty());
  EXPECT_TRUE(viewport_setting_colors_approximately(
      viewport_settings.color(ViewportColorSettingId::ActiveSelection), initial_color));
  EXPECT_FLOAT_EQ(viewport_settings.float_value(
      ViewportFloatSettingId::ActiveSelectionLineSize), initial_width);
  cancel->click();
  EXPECT_FALSE(dialog.isVisible());
  EXPECT_TRUE(changes.empty());
  EXPECT_TRUE(viewport_setting_colors_approximately(
      viewport_settings.color(ViewportColorSettingId::ActiveSelection), initial_color));
  EXPECT_FLOAT_EQ(viewport_settings.float_value(
      ViewportFloatSettingId::ActiveSelectionLineSize), initial_width);

  dialog.show();
  QCoreApplication::processEvents();
  EXPECT_DOUBLE_EQ(width->value(), initial_width);
  ASSERT_TRUE(accept_color_picker(dialog, *swatch, picker_name, applied_color));
  width->setValue(5.0);
  apply->click();
  EXPECT_TRUE(dialog.isVisible());
  EXPECT_FALSE(apply->isEnabled());
  ASSERT_EQ(changes.size(), 1u);
  EXPECT_EQ(changes.front().kind, QtSettingsChangeKind::ViewportCommit);
  EXPECT_TRUE(viewport_setting_colors_approximately(
      viewport_settings.color(ViewportColorSettingId::ActiveSelection),
      setting_color(applied_color)));
  EXPECT_FLOAT_EQ(viewport_settings.float_value(
      ViewportFloatSettingId::ActiveSelectionLineSize), 5.0f);
  dialog.reject();
}

TEST(QtSettingsDialogTests,
     CollisionFillStagesOpacityAndPersistsOnlyAppliedChanges) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-settings-collision-fill-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  const std::filesystem::path path =
      std::filesystem::path(directory.path().toStdWString()) / "viewport.ini";
  UISettings ui_settings = UISettings::defaults();
  ViewportSettings viewport_settings = ViewportSettings::defaults();
  const ViewportSettingColor initial =
      viewport_settings.color(ViewportColorSettingId::CollisionFill);
  LocalizationService localization = loaded_localization();
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  std::vector<QtSettingsChange> changes;
  QtSettingsDialog dialog;
  dialog.setAttribute(Qt::WA_DontShowOnScreen, true);
  dialog.set_context(
      ui_settings, viewport_settings, localization, editor.commands(),
      [&](const QtSettingsChange &change) {
        changes.push_back(change);
        EXPECT_TRUE(viewport_settings.save(path));
      });
  auto *swatch = dialog.findChild<QPushButton *>(
      QStringLiteral("viewportSetting_CollisionFillColor"));
  auto *apply = dialog.findChild<QPushButton *>(QStringLiteral("settingsApply"));
  auto *cancel = dialog.findChild<QPushButton *>(QStringLiteral("settingsCancel"));
  ASSERT_NE(swatch, nullptr);
  ASSERT_NE(apply, nullptr);
  ASSERT_NE(cancel, nullptr);
  const auto *group = dynamic_cast<qt_settings_detail::QtSettingsSection *>(
      swatch->parentWidget()->parentWidget());
  ASSERT_NE(group, nullptr);
  EXPECT_EQ(settings_section_title(*group), QStringLiteral("Mesh Overlays"));

  dialog.show();
  QCoreApplication::processEvents();
  EXPECT_EQ(swatch->accessibleName(), QStringLiteral("Collision Color"));
  EXPECT_TRUE(swatch->toolTip().contains(QStringLiteral("Opacity: 42%")));
  const QColor applied = QColor::fromRgb(51, 102, 153, 77);
  const QString picker_name =
      QStringLiteral("viewportSetting_CollisionFillColor_dialog");
  ASSERT_TRUE(reject_color_picker(dialog, *swatch, picker_name, applied));
  EXPECT_FALSE(apply->isEnabled());
  EXPECT_TRUE(changes.empty());
  EXPECT_TRUE(viewport_setting_colors_approximately(
      viewport_settings.color(ViewportColorSettingId::CollisionFill), initial));

  ASSERT_TRUE(accept_color_picker(dialog, *swatch, picker_name, applied));
  EXPECT_TRUE(apply->isEnabled());
  EXPECT_TRUE(changes.empty());
  EXPECT_FALSE(std::filesystem::exists(path));
  EXPECT_TRUE(viewport_setting_colors_approximately(
      viewport_settings.color(ViewportColorSettingId::CollisionFill), initial));
  apply->click();
  EXPECT_FALSE(apply->isEnabled());
  EXPECT_EQ(count_changes(changes, QtSettingsChangeKind::ViewportCommit), 1u);
  EXPECT_TRUE(viewport_setting_colors_approximately(
      viewport_settings.color(ViewportColorSettingId::CollisionFill),
      setting_color(applied)));
  EXPECT_TRUE(viewport_setting_colors_approximately(
      ViewportSettings::load(path).color(ViewportColorSettingId::CollisionFill),
      setting_color(applied)));

  ASSERT_TRUE(accept_color_picker(dialog, *swatch, picker_name,
                                 QColor::fromRgb(200, 10, 100, 230)));
  cancel->click();
  EXPECT_EQ(count_changes(changes, QtSettingsChangeKind::ViewportCommit), 1u);
  EXPECT_TRUE(viewport_setting_colors_approximately(
      viewport_settings.color(ViewportColorSettingId::CollisionFill),
      setting_color(applied)));
  EXPECT_TRUE(viewport_setting_colors_approximately(
      ViewportSettings::load(path).color(ViewportColorSettingId::CollisionFill),
      setting_color(applied)));
}

TEST(QtSettingsDialogTests,
     BackgroundColorStagesAnOpaqueEditorPreferenceAndPersistsOnApply) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-settings-background-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  const std::filesystem::path path =
      std::filesystem::path(directory.path().toStdWString()) / "viewport.ini";
  UISettings ui_settings = UISettings::defaults();
  ViewportSettings viewport_settings = ViewportSettings::defaults();
  const ViewportSettingColor initial =
      viewport_settings.color(ViewportColorSettingId::Background);
  LocalizationService localization = loaded_localization();
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  std::vector<QtSettingsChange> changes;
  QtSettingsDialog dialog;
  dialog.setAttribute(Qt::WA_DontShowOnScreen, true);
  dialog.set_context(
      ui_settings, viewport_settings, localization, editor.commands(),
      [&](const QtSettingsChange &change) {
        changes.push_back(change);
        EXPECT_TRUE(viewport_settings.save(path));
      });
  auto *swatch = dialog.findChild<QPushButton *>(
      QStringLiteral("viewportSetting_BackgroundColor"));
  auto *apply = dialog.findChild<QPushButton *>(QStringLiteral("settingsApply"));
  auto *cancel = dialog.findChild<QPushButton *>(QStringLiteral("settingsCancel"));
  ASSERT_NE(swatch, nullptr);
  ASSERT_NE(apply, nullptr);
  ASSERT_NE(cancel, nullptr);
  const auto *group = dynamic_cast<qt_settings_detail::QtSettingsSection *>(
      swatch->parentWidget()->parentWidget());
  ASSERT_NE(group, nullptr);
  EXPECT_EQ(settings_section_title(*group), QStringLiteral("Shading"));

  dialog.show();
  QCoreApplication::processEvents();
  EXPECT_EQ(swatch->accessibleName(), QStringLiteral("Background Color"));
  EXPECT_TRUE(swatch->toolTip().contains(
      QStringLiteral("Rendered views use the scene environment.")));
  EXPECT_FALSE(swatch->toolTip().contains(QStringLiteral("Opacity")));
  const QColor applied = QColor::fromRgb(51, 102, 153);
  const QString picker_name =
      QStringLiteral("viewportSetting_BackgroundColor_dialog");
  ASSERT_TRUE(reject_color_picker(
      dialog, *swatch, picker_name, applied, [&]() {
        const auto *picker = dialog.findChild<QColorDialog *>(picker_name);
        ASSERT_NE(picker, nullptr);
        EXPECT_FALSE(picker->testOption(QColorDialog::ShowAlphaChannel));
      }));
  EXPECT_FALSE(apply->isEnabled());
  EXPECT_TRUE(changes.empty());
  EXPECT_TRUE(viewport_setting_colors_approximately(
      viewport_settings.color(ViewportColorSettingId::Background), initial));

  ASSERT_TRUE(accept_color_picker(dialog, *swatch, picker_name, applied));
  EXPECT_TRUE(apply->isEnabled());
  EXPECT_TRUE(changes.empty());
  EXPECT_FALSE(std::filesystem::exists(path));
  EXPECT_TRUE(viewport_setting_colors_approximately(
      viewport_settings.color(ViewportColorSettingId::Background), initial));
  apply->click();
  EXPECT_FALSE(apply->isEnabled());
  EXPECT_EQ(count_changes(changes, QtSettingsChangeKind::ViewportCommit), 1u);
  EXPECT_TRUE(viewport_setting_colors_approximately(
      viewport_settings.color(ViewportColorSettingId::Background),
      setting_color(applied)));
  EXPECT_TRUE(viewport_setting_colors_approximately(
      ViewportSettings::load(path).color(ViewportColorSettingId::Background),
      setting_color(applied)));

  ASSERT_TRUE(localization.select_locale("it-IT"));
  dialog.refresh();
  EXPECT_EQ(swatch->accessibleName(), QStringLiteral("Colore dello sfondo"));
  ASSERT_TRUE(accept_color_picker(dialog, *swatch, picker_name,
                                 QColor::fromRgb(200, 10, 100)));
  cancel->click();
  EXPECT_EQ(count_changes(changes, QtSettingsChangeKind::ViewportCommit), 1u);
  EXPECT_TRUE(viewport_setting_colors_approximately(
      viewport_settings.color(ViewportColorSettingId::Background),
      setting_color(applied)));
  EXPECT_TRUE(viewport_setting_colors_approximately(
      ViewportSettings::load(path).color(ViewportColorSettingId::Background),
      setting_color(applied)));
}

TEST(QtSettingsDialogTests,
     CanonicalApplyCancelSettleNumericDraftsAndPreserveInputOwnership) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  const ScopedDefaultLocale force_c_locale(QLocale::c());
  int argc = 1;
  char name[] = "quader-settings-lifecycle-key-tests";
  char *argv[] = {name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  UISettings ui_settings = UISettings::defaults();
  ViewportSettings viewport_settings = ViewportSettings::defaults();
  auto localization = loaded_localization();
  std::vector<QtSettingsChange> changes;
  QtSettingsDialog dialog;
  dialog.setAttribute(Qt::WA_DontShowOnScreen, true);
  dialog.set_context(ui_settings, viewport_settings, localization,
                     editor.commands(), [&](const QtSettingsChange &change) {
                       changes.push_back(change);
                     });
  dialog.show();
  application.processEvents();
  auto *thickness = dialog.findChild<QDoubleSpinBox *>(
      QStringLiteral("viewportSetting_GridMajorLineSize"));
  auto *apply = dialog.findChild<QPushButton *>(QStringLiteral("settingsApply"));
  auto *cancel = dialog.findChild<QPushButton *>(QStringLiteral("settingsCancel"));
  auto *search = dialog.findChild<QLineEdit *>(
      QStringLiteral("settingsKeybindingsSearch"));
  auto *capture = dialog.findChild<QLineEdit *>(
      QStringLiteral("settingsKeybindingsShortcutFilter"));
  ASSERT_NE(thickness, nullptr);
  ASSERT_NE(apply, nullptr);
  ASSERT_NE(cancel, nullptr);
  ASSERT_NE(search, nullptr);
  ASSERT_NE(capture, nullptr);
  auto *line = thickness->findChild<QLineEdit *>();
  ASSERT_NE(line, nullptr);
  thickness->setKeyboardTracking(false);
  line->setText(QStringLiteral("2.5"));
  ASSERT_TRUE(thickness->hasAcceptableInput());
  EXPECT_FALSE(apply->isEnabled());
  QKeyEvent press(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier,
                   QStringLiteral(" "));
  QCoreApplication::sendEvent(line, &press);
  EXPECT_TRUE(dialog.isVisible());
  EXPECT_FLOAT_EQ(viewport_settings.float_value(
                      ViewportFloatSettingId::GridMajorLineSize), 2.5F);
  EXPECT_EQ(count_changes(changes, QtSettingsChangeKind::ViewportCommit), 1U);

  thickness->setValue(3.0);
  QKeyEvent repeat(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier,
                    QStringLiteral(" "), true);
  QCoreApplication::sendEvent(cancel, &repeat);
  QKeyEvent release(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
  QCoreApplication::sendEvent(cancel, &release);
  EXPECT_TRUE(dialog.isVisible());
  EXPECT_FLOAT_EQ(viewport_settings.float_value(
                      ViewportFloatSettingId::GridMajorLineSize), 2.5F);

  search->setText(QStringLiteral("Undo"));
  search->setCursorPosition(4);
  QKeyEvent text_space(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier,
                        QStringLiteral(" "));
  QCoreApplication::sendEvent(search, &text_space);
  EXPECT_EQ(search->text(), QStringLiteral("Undo "));
  EXPECT_FLOAT_EQ(viewport_settings.float_value(
                      ViewportFloatSettingId::GridMajorLineSize), 2.5F);
  send_key(*capture, Qt::Key_Space);
  EXPECT_EQ(capture->text(), QStringLiteral("Space"));
  send_key(*capture, Qt::Key_Escape);
  EXPECT_EQ(capture->text(), QStringLiteral("Escape"));
  EXPECT_TRUE(dialog.isVisible());

  QDialog child(&dialog);
  child.setAttribute(Qt::WA_DontShowOnScreen, true);
  child.setModal(true);
  child.show();
  application.processEvents();
  send_key(child, Qt::Key_Space);
  EXPECT_FLOAT_EQ(viewport_settings.float_value(
                      ViewportFloatSettingId::GridMajorLineSize), 2.5F);
  send_key(child, Qt::Key_Escape);
  EXPECT_FALSE(child.isVisible());
  EXPECT_TRUE(dialog.isVisible());
  send_key(*thickness, Qt::Key_Escape);
  EXPECT_FALSE(dialog.isVisible());
  EXPECT_FLOAT_EQ(viewport_settings.float_value(
                      ViewportFloatSettingId::GridMajorLineSize), 2.5F);
}

TEST(QtSettingsDialogTests,
     NumericEscapeCancelsOnlyTheDraftBeforeCanonicalDialogCancellation) {
  for (const bool numeric_filter_first : {false, true}) {
    for (const bool remap_cancel : {false, true}) {
      SCOPED_TRACE(numeric_filter_first);
      SCOPED_TRACE(remap_cancel);
      ASSERT_EQ(QCoreApplication::instance(), nullptr);
      const ScopedDefaultLocale force_c_locale(QLocale::c());
      int argc = 1;
      char name[] = "quader-settings-nested-numeric-escape-tests";
      char *argv[] = {name, nullptr};
      QApplication application(argc, argv);
      application.setQuitOnLastWindowClosed(false);
      if (numeric_filter_first) {
        qt_install_default_numeric_spin_boxes(application);
      }
      SceneSession session;
      QuaderEditorApi editor(
          QuaderApiSessionInternalAccess::from_scene_session(session));
      ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
      if (remap_cancel) {
        const auto catalog = editor.commands().keybinding_catalog();
        const auto cancel = std::find_if(
            catalog.begin(), catalog.end(), [](const auto &entry) {
              return entry.key.action_id == "tools.cancel";
            });
        ASSERT_NE(cancel, catalog.end());
        ASSERT_TRUE(editor.commands().set_action_shortcut(
            cancel->key, "Ctrl+Alt+F23").ok());
      }
      UISettings ui_settings = UISettings::defaults();
      ViewportSettings viewport_settings = ViewportSettings::defaults();
      const auto original = viewport_settings.float_value(
          ViewportFloatSettingId::GridMajorLineSize);
      auto localization = loaded_localization();
      std::vector<QtSettingsChange> changes;
      QtSettingsDialog dialog;
      dialog.setAttribute(Qt::WA_DontShowOnScreen, true);
      dialog.set_context(ui_settings, viewport_settings, localization,
                         editor.commands(), [&](const QtSettingsChange &change) {
                           changes.push_back(change);
                         });
      if (!numeric_filter_first) {
        qt_install_default_numeric_spin_boxes(application);
      }
      auto *categories = dialog.findChild<QListWidget *>(
          QStringLiteral("settingsCategories"));
      ASSERT_NE(categories, nullptr);
      for (int row = 0; row < categories->count(); ++row) {
        if (categories->item(row)->data(Qt::UserRole).toString() ==
            QStringLiteral("appearance")) {
          categories->setCurrentRow(row);
          break;
        }
      }
      auto *thickness = dialog.findChild<QDoubleSpinBox *>(
          QStringLiteral("viewportSetting_GridMajorLineSize"));
      auto *cancel = dialog.findChild<QPushButton *>(
          QStringLiteral("settingsCancel"));
      ASSERT_NE(thickness, nullptr);
      ASSERT_NE(cancel, nullptr);
      auto *line = thickness->findChild<QLineEdit *>();
      ASSERT_NE(line, nullptr);
      dialog.show();
      dialog.activateWindow();
      application.processEvents();
      QApplication::setActiveWindow(&dialog);
      thickness->setValue(2.0);
      thickness->setKeyboardTracking(false);
      thickness->setFocus(Qt::OtherFocusReason);
      ASSERT_TRUE(thickness->isVisible());
      ASSERT_EQ(QApplication::focusWidget(), thickness);
      send_key(*line, Qt::Key_F2);
      auto &controller = qt_install_numeric_spin_box(*thickness);
      ASSERT_TRUE(controller.is_editing());
      line->setText(QStringLiteral("3.5"));
      ASSERT_TRUE(thickness->hasAcceptableInput());
      QKeyEvent reserve(QEvent::ShortcutOverride, Qt::Key_Escape,
                        Qt::NoModifier);
      reserve.ignore();
      QCoreApplication::sendEvent(line, &reserve);
      EXPECT_TRUE(reserve.isAccepted());
      EXPECT_TRUE(controller.is_editing());
      QKeyEvent press(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
      QCoreApplication::sendEvent(line, &press);
      EXPECT_FALSE(controller.is_editing());
      EXPECT_DOUBLE_EQ(thickness->value(), 2.0);
      EXPECT_TRUE(dialog.isVisible());
      EXPECT_FLOAT_EQ(viewport_settings.float_value(
                          ViewportFloatSettingId::GridMajorLineSize), original);
      EXPECT_EQ(count_changes(changes, QtSettingsChangeKind::ViewportCommit), 0U);
      QKeyEvent repeat(QEvent::KeyPress, Qt::Key_Escape,
                       Qt::NoModifier, {}, true);
      QCoreApplication::sendEvent(cancel, &repeat);
      QKeyEvent repeated_release(QEvent::KeyRelease, Qt::Key_Escape,
                                 Qt::NoModifier, {}, true);
      QCoreApplication::sendEvent(cancel, &repeated_release);
      EXPECT_TRUE(dialog.isVisible());
      QKeyEvent release(QEvent::KeyRelease, Qt::Key_Escape, Qt::NoModifier);
      QCoreApplication::sendEvent(cancel, &release);
      EXPECT_TRUE(dialog.isVisible());
      send_key(*line, Qt::Key_Escape);
      if (remap_cancel) {
        EXPECT_TRUE(dialog.isVisible());
        send_key(*line, Qt::Key_F23, Qt::ControlModifier | Qt::AltModifier);
      }
      EXPECT_FALSE(dialog.isVisible());
      EXPECT_EQ(dialog.result(), QDialog::Rejected);
      EXPECT_FLOAT_EQ(viewport_settings.float_value(
                          ViewportFloatSettingId::GridMajorLineSize), original);
      EXPECT_EQ(count_changes(changes, QtSettingsChangeKind::ViewportCommit), 0U);
    }
  }
}

TEST(QtSettingsDialogTests,
     LifecycleBindingsHonorRemappingUnassignmentAndScopedSequences) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-settings-remapped-lifecycle-tests";
  char *argv[] = {name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path(
      std::filesystem::path(temporary.path().toStdWString()) /
      "keybindings.json").ok());
  const auto catalog = editor.commands().keybinding_catalog();
  const auto finalize = std::find_if(catalog.begin(), catalog.end(),
      [](const auto &entry) { return entry.key.action_id == "tools.finalize"; });
  const auto cancel = std::find_if(catalog.begin(), catalog.end(),
      [](const auto &entry) { return entry.key.action_id == "tools.cancel"; });
  ASSERT_NE(finalize, catalog.end());
  ASSERT_NE(cancel, catalog.end());
  ASSERT_TRUE(editor.commands().set_action_shortcut(
      finalize->key, "Ctrl+Alt+F24 G").ok());
  ASSERT_TRUE(editor.commands().set_action_shortcut(
      cancel->key, "Ctrl+Alt+F23").ok());
  UISettings ui_settings = UISettings::defaults();
  ViewportSettings viewport_settings = ViewportSettings::defaults();
  auto localization = loaded_localization();
  QtSettingsDialog dialog;
  dialog.setAttribute(Qt::WA_DontShowOnScreen, true);
  dialog.set_context(ui_settings, viewport_settings, localization,
                     editor.commands(), {});
  dialog.show();
  application.processEvents();
  auto *thickness = dialog.findChild<QDoubleSpinBox *>(
      QStringLiteral("viewportSetting_GridMajorLineSize"));
  ASSERT_NE(thickness, nullptr);
  auto *cancel_button = dialog.findChild<QPushButton *>(
      QStringLiteral("settingsCancel"));
  ASSERT_NE(cancel_button, nullptr);
  auto *ok_button = dialog.findChild<QPushButton *>(
      QStringLiteral("settingsOk"));
  ASSERT_NE(ok_button, nullptr);
  const auto original = viewport_settings.float_value(
      ViewportFloatSettingId::GridMajorLineSize);
  thickness->setValue(2.5);
  send_key(*thickness, Qt::Key_Space);
  send_key(*thickness, Qt::Key_Escape);
  send_key(*cancel_button, Qt::Key_Space);
  send_key(*ok_button, Qt::Key_Space);
  EXPECT_TRUE(dialog.isVisible());
  EXPECT_FLOAT_EQ(viewport_settings.float_value(
                      ViewportFloatSettingId::GridMajorLineSize), original);
  send_key(*thickness, Qt::Key_F24, Qt::ControlModifier | Qt::AltModifier);
  EXPECT_TRUE(editor.commands().has_pending_keybinding_chord());
  send_key(*thickness, Qt::Key_G);
  EXPECT_FLOAT_EQ(viewport_settings.float_value(
                      ViewportFloatSettingId::GridMajorLineSize), 2.5F);
  EXPECT_TRUE(dialog.isVisible());

  thickness->setValue(3.0);
  send_key(*thickness, Qt::Key_F24, Qt::ControlModifier | Qt::AltModifier);
  dialog.hide();
  EXPECT_FALSE(editor.commands().has_pending_keybinding_chord());
  dialog.show();
  application.processEvents();
  thickness->setValue(3.0);
  send_key(*thickness, Qt::Key_G);
  EXPECT_FLOAT_EQ(viewport_settings.float_value(
                      ViewportFloatSettingId::GridMajorLineSize), 2.5F);
  ASSERT_TRUE(editor.commands().remove_action_shortcut(finalize->key).ok());
  ASSERT_TRUE(editor.commands().remove_action_shortcut(cancel->key).ok());
  send_key(*cancel_button, Qt::Key_Space);
  send_key(*ok_button, Qt::Key_Space);
  send_key(*thickness, Qt::Key_Space);
  send_key(*thickness, Qt::Key_Escape);
  send_key(*thickness, Qt::Key_F23, Qt::ControlModifier | Qt::AltModifier);
  EXPECT_TRUE(dialog.isVisible());
  EXPECT_FLOAT_EQ(viewport_settings.float_value(
                      ViewportFloatSettingId::GridMajorLineSize), 2.5F);
  ASSERT_TRUE(editor.commands().set_action_shortcut(
      cancel->key, "Ctrl+Alt+F23").ok());
  send_key(*thickness, Qt::Key_F23, Qt::ControlModifier | Qt::AltModifier);
  EXPECT_FALSE(dialog.isVisible());
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

  std::vector<QtSettingsChange> changes;
  QtSettingsDialog dialog;
  dialog.setAttribute(Qt::WA_DontShowOnScreen, true);
  SceneSession settings_session;
  QuaderEditorApi settings_editor(
      QuaderApiSessionInternalAccess::from_scene_session(settings_session));
  dialog.set_context(
      ui_settings, viewport_settings, localization, settings_editor.commands(),
      [&](const QtSettingsChange &change) { changes.push_back(change); });

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
  auto *dimension_font_size = dialog.findChild<QDoubleSpinBox *>(
      QStringLiteral("viewportSetting_DimensionFontSize"));
  auto *show_hover_dimensions = dialog.findChild<QCheckBox *>(
      QStringLiteral("viewportSetting_ShowHoverDimensions"));
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
  std::vector<std::pair<ViewportFloatSettingId, QDoubleSpinBox *>>
      instance_controls;
  for (const ViewportFloatSettingDescriptor &descriptor :
       viewport_float_settings()) {
    if (descriptor.section != ViewportSettingsSectionId::InstanceEditing) {
      continue;
    }
    auto *control = dialog.findChild<QDoubleSpinBox *>(
        QStringLiteral("viewportSetting_%1")
            .arg(QString::fromUtf8(descriptor.key.data(),
                                   static_cast<qsizetype>(descriptor.key.size()))));
    ASSERT_NE(control, nullptr) << descriptor.key;
    EXPECT_DOUBLE_EQ(control->minimum(), 0.0) << descriptor.key;
    EXPECT_DOUBLE_EQ(control->maximum(), 1.0) << descriptor.key;
    EXPECT_EQ(control->accessibleName(),
              QString::fromUtf8(descriptor.label.data(),
                                 static_cast<qsizetype>(descriptor.label.size())));
    EXPECT_TRUE(control->toolTip().contains(QStringLiteral("Use Apply")));
    EXPECT_EQ(control->accessibleDescription(), control->toolTip());
    instance_controls.emplace_back(descriptor.id, control);
  }

  ASSERT_NE(category_list, nullptr);
  ASSERT_NE(button_box, nullptr);
  ASSERT_NE(apply_button, nullptr);
  ASSERT_NE(ok_button, nullptr);
  ASSERT_NE(cancel_button, nullptr);
  ASSERT_NE(thickness, nullptr);
  ASSERT_NE(default_fly_speed, nullptr);
  ASSERT_NE(bounding_box_thickness, nullptr);
  ASSERT_NE(dimension_font_size, nullptr);
  ASSERT_NE(show_hover_dimensions, nullptr);
  ASSERT_NE(locale_combo, nullptr);
  ASSERT_NE(mesh_major_swatch, nullptr);
  ASSERT_NE(instance_background_swatch, nullptr);
  ASSERT_NE(instance_context_swatch, nullptr);
  ASSERT_NE(bounding_box_swatch, nullptr);
  ASSERT_EQ(instance_controls.size(), 5u);
  EXPECT_TRUE(instance_context_swatch->toolTip().contains(
      QStringLiteral("target opacity")));
  for (const auto &[id, control] : instance_controls) {
    EXPECT_EQ(control->parentWidget(), instance_context_swatch->parentWidget());
    EXPECT_NEAR(control->value(), viewport_settings.float_value(id), 0.000001);
  }
  EXPECT_DOUBLE_EQ(default_fly_speed->value(), 25.0);
  EXPECT_EQ(default_fly_speed->cleanText(),
            default_fly_speed->locale().toString(25));
  EXPECT_DOUBLE_EQ(bounding_box_thickness->value(), 1.5);
  EXPECT_EQ(bounding_box_thickness->cleanText(),
            bounding_box_thickness->locale().toString(1.5, 'f', 1));
  EXPECT_DOUBLE_EQ(dimension_font_size->minimum(), 6.0);
  EXPECT_DOUBLE_EQ(dimension_font_size->maximum(), 24.0);
  EXPECT_DOUBLE_EQ(dimension_font_size->value(), 12.0);
  EXPECT_EQ(dimension_font_size->cleanText(),
            dimension_font_size->locale().toString(12));
  EXPECT_EQ(dimension_font_size->accessibleName(),
            QStringLiteral("Dimension Font Size (pt)"));
  EXPECT_TRUE(show_hover_dimensions->isChecked());
  EXPECT_EQ(show_hover_dimensions->text(),
            QStringLiteral("Show Dimensions on Hover"));
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
    if (stable_id == QStringLiteral("appearance")) {
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
  const ViewportSettings initial_instance_settings = viewport_settings;
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
  const QColor applied_instance_background = QColor::fromRgb(33, 73, 113, 63);
  const QColor applied_instance_context = QColor::fromRgb(170, 50, 90, 2);
  ASSERT_TRUE(accept_color_picker(
      dialog, *instance_background_swatch,
      QStringLiteral("viewportSetting_InstanceEditBackgroundColor_dialog"),
      applied_instance_background));
  ASSERT_TRUE(accept_color_picker(
      dialog, *instance_context_swatch,
      QStringLiteral("viewportSetting_InstanceEditContextColor_dialog"),
      applied_instance_context));
  EXPECT_TRUE(instance_context_swatch->toolTip().contains(
      QStringLiteral("target opacity")));
  for (std::size_t index = 0; index < instance_controls.size(); ++index) {
    instance_controls[index].second->setValue(
        0.3 + 0.1 * static_cast<double>(index));
  }

  thickness->setValue(1.75);
  default_fly_speed->setValue(35.0);
  bounding_box_thickness->setValue(4.25);
  dimension_font_size->setValue(13.0);
  show_hover_dimensions->setChecked(false);
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
  EXPECT_FLOAT_EQ(viewport_settings.float_value(
                      ViewportFloatSettingId::DimensionFontSize),
                  12.0f);
  EXPECT_TRUE(
      viewport_settings.bool_value(ViewportBoolSettingId::ShowHoverDimensions));
  EXPECT_EQ(ui_settings.locale(), "en-US");
  EXPECT_EQ(localization.current_locale(), "en-US");
  EXPECT_TRUE(viewport_setting_colors_approximately(
      viewport_settings.color(ViewportColorSettingId::MeshGridMajor),
      initial_mesh_major));
  EXPECT_TRUE(viewport_setting_colors_approximately(
      viewport_settings.color(ViewportColorSettingId::BoundingBoxWire),
      initial_bounding_box));
  for (const ViewportColorSettingId id :
       {ViewportColorSettingId::InstanceEditBackground,
        ViewportColorSettingId::InstanceEditContext}) {
    EXPECT_TRUE(viewport_setting_colors_approximately(
        viewport_settings.color(id), initial_instance_settings.color(id)));
  }
  for (const auto &[id, control] : instance_controls) {
    EXPECT_FLOAT_EQ(viewport_settings.float_value(id),
                    initial_instance_settings.float_value(id));
  }
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
  EXPECT_FLOAT_EQ(viewport_settings.float_value(
                      ViewportFloatSettingId::DimensionFontSize),
                  13.0f);
  EXPECT_FALSE(
      viewport_settings.bool_value(ViewportBoolSettingId::ShowHoverDimensions));
  EXPECT_EQ(ui_settings.locale(), "it-IT");
  EXPECT_EQ(localization.current_locale(), "it-IT");
  EXPECT_TRUE(viewport_setting_colors_approximately(
      viewport_settings.color(ViewportColorSettingId::MeshGridMajor),
      setting_color(applied_mesh_major)));
  EXPECT_TRUE(viewport_setting_colors_approximately(
      viewport_settings.color(ViewportColorSettingId::BoundingBoxWire),
      setting_color(applied_bounding_box)));
  EXPECT_TRUE(viewport_setting_colors_approximately(
      viewport_settings.color(ViewportColorSettingId::InstanceEditBackground),
      setting_color(applied_instance_background)));
  EXPECT_TRUE(viewport_setting_colors_approximately(
      viewport_settings.color(ViewportColorSettingId::InstanceEditContext),
      setting_color(applied_instance_context)));
  for (std::size_t index = 0; index < instance_controls.size(); ++index) {
    const auto &[id, control] = instance_controls[index];
    EXPECT_NEAR(viewport_settings.float_value(id),
                0.3 + 0.1 * static_cast<double>(index), 0.000001);
    EXPECT_TRUE(control->toolTip().contains(QStringLiteral("Premi Applica")));
  }
  EXPECT_TRUE(instance_context_swatch->toolTip().contains(
      QStringLiteral("opacità finale")));
  EXPECT_EQ(count_changes(changes, QtSettingsChangeKind::ViewportCommit), 1u);
  EXPECT_EQ(count_changes(changes, QtSettingsChangeKind::InterfaceCommit), 1u);

  thickness->setValue(2.5);
  default_fly_speed->setValue(80.0);
  bounding_box_thickness->setValue(7.0);
  dimension_font_size->setValue(18.0);
  show_hover_dimensions->setChecked(true);
  locale_combo->setCurrentIndex(english_index);
  const QColor discarded_mesh_major = QColor::fromRgb(186, 130, 74, 105);
  ASSERT_TRUE(accept_color_picker(
      dialog, *mesh_major_swatch,
      QStringLiteral("viewportSetting_MeshGridMajorColor_dialog"),
      discarded_mesh_major));
  ASSERT_TRUE(accept_color_picker(
      dialog, *instance_background_swatch,
      QStringLiteral("viewportSetting_InstanceEditBackgroundColor_dialog"),
      QColor::fromRgb(213, 77, 32, 105)));
  ASSERT_TRUE(accept_color_picker(
      dialog, *instance_context_swatch,
      QStringLiteral("viewportSetting_InstanceEditContextColor_dialog"),
      QColor::fromRgb(13, 193, 33, 204)));
  for (const auto &[id, control] : instance_controls) {
    control->setValue(0.0);
  }
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
  EXPECT_FLOAT_EQ(viewport_settings.float_value(
                      ViewportFloatSettingId::DimensionFontSize),
                  13.0f);
  EXPECT_FALSE(
      viewport_settings.bool_value(ViewportBoolSettingId::ShowHoverDimensions));
  EXPECT_EQ(ui_settings.locale(), "it-IT");
  EXPECT_EQ(localization.current_locale(), "it-IT");
  EXPECT_TRUE(viewport_setting_colors_approximately(
      viewport_settings.color(ViewportColorSettingId::MeshGridMajor),
      setting_color(applied_mesh_major)));
  EXPECT_TRUE(viewport_setting_colors_approximately(
      viewport_settings.color(ViewportColorSettingId::InstanceEditBackground),
      setting_color(applied_instance_background)));
  EXPECT_TRUE(viewport_setting_colors_approximately(
      viewport_settings.color(ViewportColorSettingId::InstanceEditContext),
      setting_color(applied_instance_context)));
  for (std::size_t index = 0; index < instance_controls.size(); ++index) {
    EXPECT_NEAR(viewport_settings.float_value(instance_controls[index].first),
                0.3 + 0.1 * static_cast<double>(index), 0.000001);
  }
  EXPECT_EQ(count_changes(changes, QtSettingsChangeKind::ViewportCommit),
            viewport_commits_before_cancel);
  EXPECT_EQ(count_changes(changes, QtSettingsChangeKind::InterfaceCommit),
            interface_commits_before_cancel);

  dialog.show();
  QCoreApplication::processEvents();
  EXPECT_DOUBLE_EQ(thickness->value(), 1.75);
  EXPECT_DOUBLE_EQ(dimension_font_size->value(), 13.0);
  EXPECT_FALSE(show_hover_dimensions->isChecked());
  for (std::size_t index = 0; index < instance_controls.size(); ++index) {
    EXPECT_NEAR(instance_controls[index].second->value(),
                0.3 + 0.1 * static_cast<double>(index), 0.000001);
    instance_controls[index].second->setValue(0.85);
  }
  const QColor accepted_instance_background = QColor::fromRgb(73, 123, 173, 102);
  const QColor accepted_instance_context = QColor::fromRgb(13, 93, 33, 128);
  ASSERT_TRUE(accept_color_picker(
      dialog, *instance_background_swatch,
      QStringLiteral("viewportSetting_InstanceEditBackgroundColor_dialog"),
      accepted_instance_background));
  ASSERT_TRUE(accept_color_picker(
      dialog, *instance_context_swatch,
      QStringLiteral("viewportSetting_InstanceEditContextColor_dialog"),
      accepted_instance_context));

  ASSERT_TRUE(viewport_settings.set_bool_value(
      ViewportBoolSettingId::ShowInfo, true));
  thickness->setValue(3.0);
  dialog.refresh();
  EXPECT_EQ(dialog.findChild<QCheckBox *>(
                QStringLiteral("viewportSetting_ShowInfo")),
            nullptr);
  ok_button->click();
  QCoreApplication::processEvents();
  EXPECT_FALSE(dialog.isVisible());
  EXPECT_EQ(dialog.result(), QDialog::Accepted);
  EXPECT_FLOAT_EQ(viewport_settings.float_value(
                      ViewportFloatSettingId::GridMajorLineSize),
                  3.0f);
  EXPECT_TRUE(viewport_settings.bool_value(ViewportBoolSettingId::ShowInfo));
  EXPECT_EQ(count_changes(changes, QtSettingsChangeKind::ViewportCommit), 2u);
  for (const auto &[id, control] : instance_controls) {
    EXPECT_FLOAT_EQ(viewport_settings.float_value(id), 0.85f);
  }
  EXPECT_TRUE(viewport_setting_colors_approximately(
      viewport_settings.color(ViewportColorSettingId::InstanceEditBackground),
      setting_color(accepted_instance_background)));
  EXPECT_TRUE(viewport_setting_colors_approximately(
      viewport_settings.color(ViewportColorSettingId::InstanceEditContext),
      setting_color(accepted_instance_context)));

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
     InterfaceScalePreviewsAppliesAndRollsBackToLastAppliedValue) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-interface-scale-settings-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  UISettings ui_settings = UISettings::defaults();
  ViewportSettings viewport_settings = ViewportSettings::defaults();
  LocalizationService localization = loaded_localization();
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  std::vector<QtSettingsChange> changes;
  QtSettingsDialog dialog;
  dialog.setAttribute(Qt::WA_DontShowOnScreen, true);
  dialog.set_context(
      ui_settings, viewport_settings, localization, editor.commands(),
      [&](const QtSettingsChange &change) { changes.push_back(change); });

  auto *scale = dialog.findChild<QSpinBox *>(
      QStringLiteral("settingsInterfaceScale"));
  auto *apply =
      dialog.findChild<QPushButton *>(QStringLiteral("settingsApply"));
  auto *cancel =
      dialog.findChild<QPushButton *>(QStringLiteral("settingsCancel"));
  auto *ok = dialog.findChild<QPushButton *>(QStringLiteral("settingsOk"));
  auto *categories =
      dialog.findChild<QListWidget *>(QStringLiteral("settingsCategories"));
  auto *keybindings_table = dialog.findChild<QTableView *>(
      QStringLiteral("settingsKeybindingsTable"));
  ASSERT_NE(scale, nullptr);
  ASSERT_NE(apply, nullptr);
  ASSERT_NE(cancel, nullptr);
  ASSERT_NE(ok, nullptr);
  ASSERT_NE(categories, nullptr);
  ASSERT_NE(keybindings_table, nullptr);

  QHeaderView *vertical_header = keybindings_table->verticalHeader();
  QHeaderView *horizontal_header = keybindings_table->horizontalHeader();
  horizontal_header->resizeSection(0, 350);
  horizontal_header->resizeSection(1, 190);
  horizontal_header->resizeSection(2, 170);
  dialog.apply_ui_scale(QtUiScaleMetrics{2.0f, 1u});
  EXPECT_EQ(categories->minimumWidth(), 320);
  EXPECT_EQ(categories->maximumWidth(), 440);
  EXPECT_EQ(vertical_header->minimumSectionSize(), 36);
  EXPECT_EQ(vertical_header->defaultSectionSize(), 40);
  EXPECT_EQ(horizontal_header->minimumSectionSize(), 160);
  EXPECT_EQ(horizontal_header->sectionSize(0), 700);
  EXPECT_EQ(horizontal_header->sectionSize(1), 380);
  EXPECT_EQ(horizontal_header->sectionSize(2), 340);
  dialog.apply_ui_scale(QtUiScaleMetrics{2.0f, 2u});
  EXPECT_EQ(horizontal_header->sectionSize(0), 700);
  horizontal_header->resizeSection(0, 750);
  dialog.apply_ui_scale(QtUiScaleMetrics{1.0f, 3u});
  EXPECT_EQ(horizontal_header->sectionSize(0), 375);
  EXPECT_EQ(horizontal_header->sectionSize(1), 190);
  EXPECT_EQ(horizontal_header->sectionSize(2), 170);

  dialog.show();
  QCoreApplication::processEvents();
  EXPECT_EQ(scale->minimum(), 50);
  EXPECT_EQ(scale->maximum(), 300);
  EXPECT_EQ(scale->singleStep(), 5);
  EXPECT_EQ(scale->suffix(), QStringLiteral("%"));
  EXPECT_EQ(scale->value(), 100);
  EXPECT_FLOAT_EQ(ui_settings.interface_scale(), 1.0f);
  EXPECT_FALSE(apply->isEnabled());

  scale->setValue(150);
  ASSERT_EQ(changes.size(), 1u);
  EXPECT_EQ(changes.back().kind, QtSettingsChangeKind::InterfacePreview);
  ASSERT_TRUE(changes.back().interface_scale.has_value());
  EXPECT_FLOAT_EQ(*changes.back().interface_scale, 1.5f);
  EXPECT_FLOAT_EQ(ui_settings.interface_scale(), 1.0f);
  EXPECT_TRUE(apply->isEnabled());

  apply->click();
  QCoreApplication::processEvents();
  EXPECT_TRUE(dialog.isVisible());
  EXPECT_FLOAT_EQ(ui_settings.interface_scale(), 1.5f);
  EXPECT_FALSE(apply->isEnabled());
  ASSERT_EQ(changes.size(), 2u);
  EXPECT_EQ(changes.back().kind, QtSettingsChangeKind::InterfaceCommit);
  ASSERT_TRUE(changes.back().interface_scale.has_value());
  EXPECT_FLOAT_EQ(*changes.back().interface_scale, 1.5f);

  scale->setValue(200);
  ASSERT_EQ(changes.size(), 3u);
  EXPECT_EQ(changes.back().kind, QtSettingsChangeKind::InterfacePreview);
  ASSERT_TRUE(changes.back().interface_scale.has_value());
  EXPECT_FLOAT_EQ(*changes.back().interface_scale, 2.0f);
  cancel->click();
  QCoreApplication::processEvents();
  EXPECT_FALSE(dialog.isVisible());
  EXPECT_FLOAT_EQ(ui_settings.interface_scale(), 1.5f);
  ASSERT_EQ(changes.size(), 4u);
  EXPECT_EQ(changes.back().kind, QtSettingsChangeKind::InterfacePreview);
  ASSERT_TRUE(changes.back().interface_scale.has_value());
  EXPECT_FLOAT_EQ(*changes.back().interface_scale, 1.5f);
  EXPECT_EQ(count_changes(changes, QtSettingsChangeKind::InterfaceCommit), 1u);

  dialog.show();
  QCoreApplication::processEvents();
  EXPECT_EQ(scale->value(), 150);
  scale->setValue(200);
  ASSERT_EQ(changes.back().kind, QtSettingsChangeKind::InterfacePreview);
  ui_settings.set_interface_scale(1.25f);
  dialog.refresh();
  EXPECT_EQ(scale->value(), 125);
  EXPECT_FALSE(apply->isEnabled());
  scale->setValue(300);
  ok->click();
  QCoreApplication::processEvents();
  EXPECT_FALSE(dialog.isVisible());
  EXPECT_EQ(dialog.result(), QDialog::Accepted);
  EXPECT_FLOAT_EQ(ui_settings.interface_scale(), 3.0f);
  EXPECT_EQ(count_changes(changes, QtSettingsChangeKind::InterfaceCommit), 2u);
  EXPECT_EQ(changes.back().kind, QtSettingsChangeKind::InterfaceCommit);
  ASSERT_TRUE(changes.back().interface_scale.has_value());
  EXPECT_FLOAT_EQ(*changes.back().interface_scale, 3.0f);
}

TEST(QtSettingsDialogTests,
     VSyncPreviewsPersistsOnceAndRollsBackToLastAppliedValue) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-vsync-settings-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  QTemporaryDir profile;
  ASSERT_TRUE(profile.isValid());
  const std::filesystem::path settings_path =
      std::filesystem::path(profile.path().toStdWString()) / "ui_settings.ini";

  UISettings ui_settings = UISettings::defaults();
  ViewportSettings viewport_settings = ViewportSettings::defaults();
  LocalizationService localization = loaded_localization();
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  std::vector<QtSettingsChange> changes;
  int saves = 0;
  QtSettingsDialog dialog;
  dialog.setAttribute(Qt::WA_DontShowOnScreen, true);
  dialog.set_context(
      ui_settings, viewport_settings, localization, editor.commands(),
      [&](const QtSettingsChange &change) { changes.push_back(change); },
      [&]() {
        ++saves;
        return ui_settings.save(settings_path);
      });
  auto *vsync = dialog.findChild<QCheckBox *>(QStringLiteral("settingsVSync"));
  auto *scale = dialog.findChild<QSpinBox *>(
      QStringLiteral("settingsInterfaceScale"));
  auto *apply = dialog.findChild<QPushButton *>(QStringLiteral("settingsApply"));
  auto *cancel = dialog.findChild<QPushButton *>(QStringLiteral("settingsCancel"));
  ASSERT_NE(vsync, nullptr);
  ASSERT_NE(scale, nullptr);
  ASSERT_NE(apply, nullptr);
  ASSERT_NE(cancel, nullptr);

  dialog.show();
  QCoreApplication::processEvents();
  EXPECT_TRUE(vsync->isChecked());
  EXPECT_FALSE(vsync->toolTip().isEmpty());
  EXPECT_FALSE(apply->isEnabled());
  vsync->setChecked(false);
  ASSERT_EQ(changes.size(), 1u);
  EXPECT_EQ(changes.back().kind, QtSettingsChangeKind::RenderingPreview);
  ASSERT_TRUE(changes.back().vsync_enabled.has_value());
  EXPECT_FALSE(*changes.back().vsync_enabled);
  EXPECT_TRUE(ui_settings.vsync_enabled());
  EXPECT_EQ(saves, 0);
  EXPECT_TRUE(apply->isEnabled());

  scale->setValue(150);
  apply->click();
  EXPECT_TRUE(dialog.isVisible());
  EXPECT_FALSE(ui_settings.vsync_enabled());
  EXPECT_FLOAT_EQ(ui_settings.interface_scale(), 1.5f);
  EXPECT_FALSE(UISettings::load(settings_path).vsync_enabled());
  EXPECT_FLOAT_EQ(UISettings::load(settings_path).interface_scale(), 1.5f);
  EXPECT_EQ(saves, 1) << "combined settings share one persisted UI owner";
  EXPECT_EQ(count_changes(changes, QtSettingsChangeKind::RenderingCommit), 1u);
  EXPECT_FALSE(apply->isEnabled());

  vsync->setChecked(true);
  cancel->click();
  EXPECT_FALSE(dialog.isVisible());
  EXPECT_FALSE(ui_settings.vsync_enabled());
  EXPECT_EQ(changes.back().kind, QtSettingsChangeKind::RenderingPreview);
  ASSERT_TRUE(changes.back().vsync_enabled.has_value());
  EXPECT_FALSE(*changes.back().vsync_enabled);
  EXPECT_EQ(saves, 1);

  dialog.show();
  QCoreApplication::processEvents();
  EXPECT_FALSE(vsync->isChecked());
  vsync->setChecked(true);
  scale->setValue(200);
  dialog.synchronize_vsync_enabled();
  EXPECT_FALSE(vsync->isChecked());
  EXPECT_EQ(scale->value(), 200) << "external VSync intent preserves other drafts";
  EXPECT_TRUE(apply->isEnabled());
  dialog.reject();
  EXPECT_EQ(saves, 1);

  dialog.show();
  QCoreApplication::processEvents();
  ui_settings.set_vsync_enabled(true);
  dialog.refresh();
  EXPECT_TRUE(vsync->isChecked());
  EXPECT_FALSE(apply->isEnabled());
  dialog.accept();
  EXPECT_EQ(saves, 1) << "clean OK does not rewrite settings";
}

TEST(QtSettingsDialogTests,
     RuntimeRebindPreservesDraftAndUsesReplacementCallbacks) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-settings-runtime-rebind-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  UISettings ui_settings = UISettings::defaults();
  ViewportSettings viewport_settings = ViewportSettings::defaults();
  LocalizationService localization = loaded_localization();
  SceneSession old_session;
  SceneSession replacement_session;
  QuaderEditorApi old_editor(
      QuaderApiSessionInternalAccess::from_scene_session(old_session));
  QuaderEditorApi replacement_editor(
      QuaderApiSessionInternalAccess::from_scene_session(replacement_session));
  std::vector<QtSettingsChange> old_changes;
  std::vector<QtSettingsChange> new_changes;
  int old_persist_count = 0;
  int new_persist_count = 0;
  QtSettingsDialog dialog;
  dialog.setAttribute(Qt::WA_DontShowOnScreen, true);
  dialog.set_context(
      ui_settings, viewport_settings, localization, old_editor.commands(),
      [&](const QtSettingsChange &change) { old_changes.push_back(change); },
      [&]() {
        ++old_persist_count;
        return true;
      });
  auto *undo = dialog.findChild<QSpinBox *>(QStringLiteral("settingsUndoSteps"));
  auto *interval = dialog.findChild<QSpinBox *>(
      QStringLiteral("settingsAutosaveInterval"));
  auto *scale = dialog.findChild<QSpinBox *>(
      QStringLiteral("settingsInterfaceScale"));
  auto *vsync = dialog.findChild<QCheckBox *>(QStringLiteral("settingsVSync"));
  auto *show_hover_dimensions = dialog.findChild<QCheckBox *>(
      QStringLiteral("viewportSetting_ShowHoverDimensions"));
  auto *apply = dialog.findChild<QPushButton *>(QStringLiteral("settingsApply"));
  ASSERT_NE(undo, nullptr);
  ASSERT_NE(interval, nullptr);
  ASSERT_NE(scale, nullptr);
  ASSERT_NE(vsync, nullptr);
  ASSERT_NE(show_hover_dimensions, nullptr);
  ASSERT_NE(apply, nullptr);
  dialog.show();
  QCoreApplication::processEvents();
  undo->setValue(8);
  interval->setValue(5);
  scale->setValue(150);
  vsync->setChecked(false);
  show_hover_dimensions->setChecked(false);
  ASSERT_EQ(old_changes.size(), 2u);
  ASSERT_TRUE(apply->isEnabled());

  dialog.set_context(
      ui_settings, viewport_settings, localization, replacement_editor.commands(),
      [&](const QtSettingsChange &change) { new_changes.push_back(change); },
      [&]() {
        ++new_persist_count;
        return true;
      });
  EXPECT_TRUE(dialog.isVisible());
  EXPECT_EQ(undo->value(), 8);
  EXPECT_EQ(interval->value(), 5);
  EXPECT_EQ(scale->value(), 150);
  EXPECT_FALSE(vsync->isChecked());
  EXPECT_FALSE(show_hover_dimensions->isChecked());
  EXPECT_TRUE(apply->isEnabled());
  EXPECT_EQ(old_changes.size(), 2u)
      << "runtime replacement must not roll back previews owned by the same preferences";
  EXPECT_TRUE(new_changes.empty());
  EXPECT_EQ(ui_settings.session_settings(), SessionPreferences{});
  EXPECT_FLOAT_EQ(ui_settings.interface_scale(), 1.0f);
  EXPECT_TRUE(ui_settings.vsync_enabled());
  EXPECT_TRUE(viewport_settings.bool_value(
      ViewportBoolSettingId::ShowHoverDimensions));

  apply->click();
  EXPECT_EQ(old_persist_count, 0);
  EXPECT_EQ(new_persist_count, 1);
  EXPECT_EQ(old_changes.size(), 2u);
  EXPECT_EQ(count_changes(new_changes, QtSettingsChangeKind::SessionCommit), 1u);
  EXPECT_EQ(count_changes(new_changes, QtSettingsChangeKind::InterfaceCommit), 1u);
  EXPECT_EQ(count_changes(new_changes, QtSettingsChangeKind::RenderingCommit), 1u);
  EXPECT_EQ(count_changes(new_changes, QtSettingsChangeKind::ViewportCommit), 1u);
  EXPECT_EQ(ui_settings.session_settings().undo_steps, 8);
  EXPECT_EQ(ui_settings.session_settings().autosave_interval_minutes, 5);
  EXPECT_FLOAT_EQ(ui_settings.interface_scale(), 1.5f);
  EXPECT_FALSE(ui_settings.vsync_enabled());
  EXPECT_FALSE(viewport_settings.bool_value(
      ViewportBoolSettingId::ShowHoverDimensions));
  EXPECT_FALSE(apply->isEnabled());

  undo->setValue(0);
  scale->setValue(200);
  dialog.reject();
  EXPECT_EQ(ui_settings.session_settings().undo_steps, 8);
  EXPECT_FLOAT_EQ(ui_settings.interface_scale(), 1.5f);
  ASSERT_FALSE(new_changes.empty());
  EXPECT_EQ(new_changes.back().kind, QtSettingsChangeKind::InterfacePreview);
  EXPECT_EQ(new_changes.back().interface_scale, 1.5f);
  EXPECT_EQ(old_changes.size(), 2u);
}

TEST(QtSettingsDialogTests,
     VisibleContextRebindRollsBackTheOldPreviewAndStartsANewTransaction) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-interface-scale-rebind-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  UISettings old_ui_settings = UISettings::defaults();
  old_ui_settings.set_interface_scale(1.25f);
  ViewportSettings old_viewport_settings = ViewportSettings::defaults();
  UISettings new_ui_settings = UISettings::defaults();
  new_ui_settings.set_interface_scale(0.75f);
  SessionPreferences new_session_settings;
  new_session_settings.undo_steps = 64;
  ASSERT_TRUE(new_ui_settings.set_session_settings(new_session_settings));
  ViewportSettings new_viewport_settings = ViewportSettings::defaults();
  LocalizationService localization = loaded_localization();
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  std::vector<QtSettingsChange> changes;
  const auto record_change = [&](const QtSettingsChange &change) {
    changes.push_back(change);
  };

  QtSettingsDialog dialog;
  dialog.setAttribute(Qt::WA_DontShowOnScreen, true);
  dialog.set_context(old_ui_settings, old_viewport_settings, localization,
                     editor.commands(), record_change);
  auto *undo = dialog.findChild<QSpinBox *>(QStringLiteral("settingsUndoSteps"));
  auto *scale = dialog.findChild<QSpinBox *>(
      QStringLiteral("settingsInterfaceScale"));
  auto *apply =
      dialog.findChild<QPushButton *>(QStringLiteral("settingsApply"));
  auto *cancel =
      dialog.findChild<QPushButton *>(QStringLiteral("settingsCancel"));
  ASSERT_NE(scale, nullptr);
  ASSERT_NE(undo, nullptr);
  ASSERT_NE(apply, nullptr);
  ASSERT_NE(cancel, nullptr);

  dialog.show();
  QCoreApplication::processEvents();
  scale->setValue(200);
  undo->setValue(8);
  ASSERT_EQ(changes.size(), 1u);
  EXPECT_FLOAT_EQ(*changes.back().interface_scale, 2.0f);

  dialog.set_context(new_ui_settings, new_viewport_settings, localization,
                     editor.commands(), record_change);
  ASSERT_TRUE(dialog.isVisible());
  ASSERT_EQ(changes.size(), 2u);
  EXPECT_EQ(changes.back().kind, QtSettingsChangeKind::InterfacePreview);
  ASSERT_TRUE(changes.back().interface_scale.has_value());
  EXPECT_FLOAT_EQ(*changes.back().interface_scale, 1.25f)
      << "rebinding must first roll back the old owner's live preview";
  EXPECT_EQ(scale->value(), 75);
  EXPECT_EQ(undo->value(), 64);
  EXPECT_EQ(old_ui_settings.session_settings(), SessionPreferences{});
  EXPECT_FALSE(apply->isEnabled());

  scale->setValue(100);
  EXPECT_TRUE(apply->isEnabled());
  cancel->click();
  QCoreApplication::processEvents();
  EXPECT_FALSE(dialog.isVisible());
  EXPECT_FLOAT_EQ(new_ui_settings.interface_scale(), 0.75f);
  ASSERT_TRUE(changes.back().interface_scale.has_value());
  EXPECT_FLOAT_EQ(*changes.back().interface_scale, 0.75f);
}

TEST(QtSettingsDialogTests,
     ExternalScaleCommitMatchingRollbackBaselineClearsActivePreview) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-interface-scale-external-sync-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  UISettings ui_settings = UISettings::defaults();
  ViewportSettings viewport_settings = ViewportSettings::defaults();
  LocalizationService localization = loaded_localization();
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  std::vector<QtSettingsChange> changes;

  QtSettingsDialog dialog;
  dialog.setAttribute(Qt::WA_DontShowOnScreen, true);
  dialog.set_context(
      ui_settings, viewport_settings, localization, editor.commands(),
      [&](const QtSettingsChange &change) { changes.push_back(change); });
  auto *scale = dialog.findChild<QSpinBox *>(
      QStringLiteral("settingsInterfaceScale"));
  auto *apply =
      dialog.findChild<QPushButton *>(QStringLiteral("settingsApply"));
  ASSERT_NE(scale, nullptr);
  ASSERT_NE(apply, nullptr);

  dialog.show();
  QCoreApplication::processEvents();
  scale->setValue(200);
  ASSERT_TRUE(apply->isEnabled());
  ASSERT_EQ(changes.size(), 1u);
  ASSERT_FLOAT_EQ(*changes.back().interface_scale, 2.0f);

  // The public API has reapplied the already-persisted 100% baseline. The
  // dialog must drop only its stale scale draft even though the target value
  // itself did not change.
  dialog.synchronize_interface_scale();
  EXPECT_EQ(scale->value(), 100);
  EXPECT_FALSE(apply->isEnabled());
  EXPECT_EQ(changes.size(), 1u);
  EXPECT_FLOAT_EQ(ui_settings.interface_scale(), 1.0f);
}

TEST(QtSettingsDialogTests,
     FailedInterfacePersistenceRollsBackCombinedApplyAndKeepsDraftForRetry) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-interface-scale-save-failure-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  UISettings ui_settings = UISettings::defaults();
  ViewportSettings viewport_settings = ViewportSettings::defaults();
  LocalizationService localization = loaded_localization();
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  std::vector<QtSettingsChange> changes;
  int persistence_attempts = 0;

  QtSettingsDialog dialog;
  dialog.setAttribute(Qt::WA_DontShowOnScreen, true);
  dialog.set_context(
      ui_settings, viewport_settings, localization, editor.commands(),
      [&](const QtSettingsChange &change) { changes.push_back(change); },
      [&]() {
        ++persistence_attempts;
        return false;
      });
  auto *scale = dialog.findChild<QSpinBox *>(
      QStringLiteral("settingsInterfaceScale"));
  auto *vsync = dialog.findChild<QCheckBox *>(QStringLiteral("settingsVSync"));
  auto *grid_major_line_size = dialog.findChild<QDoubleSpinBox *>(
      QStringLiteral("viewportSetting_GridMajorLineSize"));
  auto *apply =
      dialog.findChild<QPushButton *>(QStringLiteral("settingsApply"));
  ASSERT_NE(scale, nullptr);
  ASSERT_NE(vsync, nullptr);
  ASSERT_NE(grid_major_line_size, nullptr);
  ASSERT_NE(apply, nullptr);

  const float initial_grid_major_line_size = viewport_settings.float_value(
      ViewportFloatSettingId::GridMajorLineSize);

  dialog.show();
  QCoreApplication::processEvents();
  scale->setValue(150);
  vsync->setChecked(false);
  grid_major_line_size->setValue(2.5);
  apply->click();
  QCoreApplication::processEvents();

  EXPECT_TRUE(dialog.isVisible());
  EXPECT_EQ(persistence_attempts, 1);
  EXPECT_FLOAT_EQ(ui_settings.interface_scale(), 1.0f);
  EXPECT_TRUE(ui_settings.vsync_enabled());
  EXPECT_FALSE(vsync->isChecked());
  EXPECT_FLOAT_EQ(viewport_settings.float_value(
                      ViewportFloatSettingId::GridMajorLineSize),
                  initial_grid_major_line_size);
  EXPECT_EQ(scale->value(), 150);
  EXPECT_DOUBLE_EQ(grid_major_line_size->value(), 2.5);
  EXPECT_TRUE(apply->isEnabled());
  ASSERT_EQ(changes.size(), 4u);
  EXPECT_EQ(changes[0].kind, QtSettingsChangeKind::InterfacePreview);
  EXPECT_EQ(changes[1].kind, QtSettingsChangeKind::RenderingPreview);
  EXPECT_EQ(changes[2].kind, QtSettingsChangeKind::InterfaceCommit);
  ASSERT_TRUE(changes[2].interface_scale.has_value());
  EXPECT_FLOAT_EQ(*changes[2].interface_scale, 1.0f);
  EXPECT_EQ(changes[3].kind, QtSettingsChangeKind::RenderingCommit);
  ASSERT_TRUE(changes[3].vsync_enabled.has_value());
  EXPECT_TRUE(*changes[3].vsync_enabled);
  EXPECT_EQ(count_changes(changes, QtSettingsChangeKind::ViewportCommit), 0u)
      << "a failed combined Apply must not publish or persist viewport state";

  auto *error = dynamic_cast<QtRetainedMessageBox *>(
      dialog.findChild<QWidget *>(
          QStringLiteral("settingsPersistenceErrorDialog")));
  ASSERT_NE(error, nullptr);
  EXPECT_TRUE(error->isVisible());
  error->done(QDialog::Accepted);
  dialog.reject();
}

TEST(QtSettingsDialogTests,
     KeybindingEditorListsEditableCatalogRowsByContext) {
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
  std::vector<QtSettingsChange> changes;
  QtSettingsDialog dialog;
  dialog.setAttribute(Qt::WA_DontShowOnScreen, true);
  dialog.set_context(
      ui_settings, viewport_settings, localization, editor.commands(),
      [&](const QtSettingsChange &change) { changes.push_back(change); });

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
  const int editable_row_count = static_cast<int>(std::count_if(
      catalog.begin(), catalog.end(),
      [](const auto &entry) { return entry.editable; }));
  ASSERT_EQ(model->rowCount(), editable_row_count);
  const QuaderKeybindingCatalogEntry *mirror = find_keybinding_catalog_entry(
      catalog, quader_command::kToolsActivateMirror.string());
  ASSERT_NE(mirror, nullptr);
  EXPECT_EQ(mirror->default_shortcut, "M");
  EXPECT_EQ(mirror->shortcut, "M");
  std::multiset<std::string> expected_rows;
  for (const QuaderKeybindingCatalogEntry &entry : catalog) {
    if (!entry.editable) {
      continue;
    }
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
  for (const std::string_view fixed_target : {
           "viewport.navigation.orbit", "viewport.navigation.orbit_snap",
           "viewport.navigation.pan"}) {
    const auto *fixed = find_keybinding_catalog_entry(catalog, fixed_target);
    ASSERT_NE(fixed, nullptr);
    EXPECT_FALSE(fixed->editable);
    EXPECT_EQ(find_row(fixed_target, fixed->shortcut), -1);
  }
  EXPECT_GE(find_row("viewport.navigation.fly", "RightMouseDrag"), 0);
  EXPECT_GE(find_row("viewport.navigation.fly_forward", "W"), 0);
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
  EXPECT_EQ(model->rowCount(), editable_row_count);

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
  std::vector<QtSettingsChange> changes;
  QtSettingsDialog dialog;
  dialog.setAttribute(Qt::WA_DontShowOnScreen, true);
  dialog.set_context(
      ui_settings, viewport_settings, localization, editor.commands(),
      [&](const QtSettingsChange &change) { changes.push_back(change); });
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
                            Qt::MiddleButton,
                            Qt::ControlModifier | Qt::AltModifier);
    QMouseEvent click_release(QEvent::MouseButtonRelease,
                              QPointF{16.0, 16.0}, QPointF{116.0, 116.0},
                              Qt::MiddleButton, Qt::NoButton,
                              Qt::ControlModifier | Qt::AltModifier);
    QCoreApplication::sendEvent(recorder, &click_press);
    QCoreApplication::sendEvent(recorder, &click_release);
    EXPECT_FALSE(use->isEnabled());
    send_key(*recorder, Qt::Key_L);
    EXPECT_TRUE(use->isEnabled());
    EXPECT_TRUE(recorder->text().contains(
        QStringLiteral("Ctrl+Alt+MiddleMouseClick L")));

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
      "Mouse6Drag Ctrl+Alt+MiddleMouseClick L LeftMouseClick RightMouseClick "
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
  std::vector<QtSettingsChange> changes;
  QtSettingsDialog dialog;
  dialog.setAttribute(Qt::WA_DontShowOnScreen, true);
  dialog.set_context(
      ui_settings, viewport_settings, localization, editor.commands(),
      [&](const QtSettingsChange &change) { changes.push_back(change); });
  dialog.show();
  QCoreApplication::processEvents();

  const QList<QMessageBox *> retained_boxes =
      dialog.findChildren<QMessageBox *>(QString{},
                                         Qt::FindDirectChildrenOnly);
  ASSERT_EQ(retained_boxes.size(), 1);
  QPointer<QMessageBox> retained_message_box(retained_boxes.front());
  ASSERT_NE(retained_message_box, nullptr);
  EXPECT_FALSE(retained_message_box->isVisible());
  EXPECT_TRUE(
      retained_message_box->testAttribute(Qt::WA_WState_Created));
  const WId retained_window_id = retained_message_box->winId();
  EXPECT_NE(retained_window_id, WId{0});
  QPointer<QPushButton> retained_primary(
      retained_message_box->findChild<QPushButton *>(
          QStringLiteral("retainedMessageBox.primary")));
  QPointer<QPushButton> retained_cancel(
      retained_message_box->findChild<QPushButton *>(
          QStringLiteral("retainedMessageBox.tertiary")));
  ASSERT_NE(retained_primary, nullptr);
  ASSERT_NE(retained_cancel, nullptr);

  const auto expect_retained_identity =
      [&](QMessageBox &box, const QString &primary_object_name,
          const QString &cancel_object_name) {
        EXPECT_EQ(&box, retained_message_box.data());
        EXPECT_EQ(box.winId(), retained_window_id);
        EXPECT_EQ(box.findChild<QPushButton *>(primary_object_name),
                  retained_primary.data());
        EXPECT_EQ(box.findChild<QPushButton *>(cancel_object_name),
                  retained_cancel.data());
      };
  const auto expect_conflict_identity = [&](QMessageBox &box) {
    expect_retained_identity(
        box, QStringLiteral("settingsKeybindingConflictReplace"),
        QStringLiteral("settingsKeybindingConflictCancel"));
  };
  const auto expect_reset_identity = [&](QMessageBox &box) {
    expect_retained_identity(
        box, QStringLiteral("settingsKeybindingResetAllConfirm"),
        QStringLiteral("settingsKeybindingResetAllCancel"));
  };

  auto *table = dialog.findChild<QTableView *>(
      QStringLiteral("settingsKeybindingsTable"));
  auto *reset_all = dialog.findChild<QPushButton *>(
      QStringLiteral("settingsKeybindingsResetAll"));
  ASSERT_NE(table, nullptr);
  ASSERT_NE(table->model(), nullptr);
  ASSERT_NE(reset_all, nullptr);

  // Keep this dialog-lifecycle fixture scoped to editor commands. Asset
  // clipboard commands also apply in the browser and may overlap navigation.
  const std::string edited_target =
      std::string(quader_command::kViewportFrameSelection.string());
  const std::string move_target =
      editor.commands().command_id_for_tool_activation("move");
  int edited_row = -1;
  for (int row = 0; row < table->model()->rowCount(); ++row) {
    if (table->model()
            ->data(table->model()->index(row, 0), Qt::UserRole)
            .toString() == QString::fromStdString(edited_target)) {
      edited_row = row;
      break;
    }
  }
  ASSERT_GE(edited_row, 0);
  table->selectRow(edited_row);
  QCoreApplication::processEvents();

  EXPECT_TRUE(capture_keybinding_and_choose_conflict(
      dialog, *table, {}, QStringLiteral("Activate Move Tool"), true,
      expect_conflict_identity));
  EXPECT_TRUE(capture_keybinding_and_choose_conflict(
      dialog, *table, QStringLiteral("settingsKeybindingConflictCancel"),
      QStringLiteral("Activate Move Tool"), true,
      expect_conflict_identity));
  auto catalog = editor.commands().keybinding_catalog();
  const QuaderKeybindingCatalogEntry *edited =
      find_keybinding_catalog_entry(catalog, edited_target);
  ASSERT_NE(edited, nullptr);
  EXPECT_NE(edited->shortcut, "W");
  EXPECT_EQ(count_changes(changes, QtSettingsChangeKind::KeybindingsCommit),
            0u);

  EXPECT_TRUE(capture_keybinding_and_choose_conflict(
      dialog, *table, QStringLiteral("settingsKeybindingConflictReplace"),
      QStringLiteral("Activate Move Tool"), false,
      expect_conflict_identity));
  catalog = editor.commands().keybinding_catalog();
  edited = find_keybinding_catalog_entry(catalog, edited_target);
  const QuaderKeybindingCatalogEntry *move =
      find_keybinding_catalog_entry(catalog, move_target);
  ASSERT_NE(edited, nullptr);
  ASSERT_NE(move, nullptr);
  EXPECT_EQ(edited->shortcut, "W");
  EXPECT_TRUE(move->shortcut.empty());
  EXPECT_TRUE(move->customized);
  EXPECT_TRUE(move->unassigned);
  EXPECT_EQ(count_changes(changes, QtSettingsChangeKind::KeybindingsCommit),
            1u);

  EXPECT_TRUE(choose_reset_all_confirmation(dialog, *reset_all, {},
                                            expect_reset_identity));
  EXPECT_TRUE(choose_reset_all_confirmation(
      dialog, *reset_all,
      QStringLiteral("settingsKeybindingResetAllCancel"),
      expect_reset_identity));
  EXPECT_EQ(count_changes(changes, QtSettingsChangeKind::KeybindingsCommit),
            1u);
  catalog = editor.commands().keybinding_catalog();
  edited = find_keybinding_catalog_entry(catalog, edited_target);
  ASSERT_NE(edited, nullptr);
  EXPECT_EQ(edited->shortcut, "W");

  EXPECT_TRUE(choose_reset_all_confirmation(
      dialog, *reset_all,
      QStringLiteral("settingsKeybindingResetAllConfirm"),
      expect_reset_identity));
  EXPECT_EQ(count_changes(changes, QtSettingsChangeKind::KeybindingsCommit),
            2u);
  catalog = editor.commands().keybinding_catalog();
  edited = find_keybinding_catalog_entry(catalog, edited_target);
  move = find_keybinding_catalog_entry(catalog, move_target);
  ASSERT_NE(edited, nullptr);
  ASSERT_NE(move, nullptr);
  EXPECT_EQ(edited->shortcut, edited->default_shortcut);
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
  std::vector<QtSettingsChange> changes;
  QtSettingsDialog dialog;
  dialog.setAttribute(Qt::WA_DontShowOnScreen, true);
  dialog.set_context(
      ui_settings, viewport_settings, localization, editor.commands(),
      [&](const QtSettingsChange &change) { changes.push_back(change); });
  dialog.show();
  QCoreApplication::processEvents();

  const QList<QMessageBox *> retained_boxes =
      dialog.findChildren<QMessageBox *>(QString{},
                                         Qt::FindDirectChildrenOnly);
  ASSERT_EQ(retained_boxes.size(), 1);
  QPointer<QMessageBox> retained_message_box(retained_boxes.front());
  ASSERT_NE(retained_message_box, nullptr);
  EXPECT_FALSE(retained_message_box->isVisible());
  EXPECT_TRUE(
      retained_message_box->testAttribute(Qt::WA_WState_Created));
  const WId retained_window_id = retained_message_box->winId();
  EXPECT_NE(retained_window_id, WId{0});
  QPointer<QPushButton> retained_primary(
      retained_message_box->findChild<QPushButton *>(
          QStringLiteral("retainedMessageBox.primary")));
  ASSERT_NE(retained_primary, nullptr);
  const auto expect_error_identity = [&](QMessageBox &box) {
    EXPECT_EQ(&box, retained_message_box.data());
    EXPECT_EQ(box.winId(), retained_window_id);
    EXPECT_EQ(box.defaultButton(), retained_primary.data());
  };

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

  EXPECT_TRUE(capture_keybinding_and_expect_error(
      dialog, *table, Qt::Key_U, Qt::ControlModifier | Qt::AltModifier,
      QStringLiteral("Ctrl+Alt+U"), true, expect_error_identity));
  EXPECT_TRUE(capture_keybinding_and_expect_error(
      dialog, *table, Qt::Key_U, Qt::ControlModifier | Qt::AltModifier,
      QStringLiteral("Ctrl+Alt+U"), false, expect_error_identity));
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
     HiddenCollisionSelectorsPreserveLegacyMetadataThroughToggleEdits) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-inspector-collision-options-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto box = editor.modeling().create_box(
      QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                   .max = {1.0f, 1.0f, 1.0f}},
      "Collision Mesh");
  ASSERT_TRUE(box.ok()) << box.error_message();
  ASSERT_TRUE(editor.selection().select_object(box.value()).ok());
  const EntityId entity = box.value().entity;
  ASSERT_NE(session.scene().mesh(entity), nullptr);
  EXPECT_EQ(session.scene().mesh(entity)->collision_type, MeshCollisionType::Mesh);
  EXPECT_TRUE(session.scene().mesh(entity)->generate_collisions);
  EXPECT_TRUE(session.scene().mesh(entity)->display_collision);
  MeshComponent legacy = *session.scene().mesh(entity);
  legacy.collision_type = MeshCollisionType::Box;
  legacy.body_type = MeshPhysicsBodyType::RigidBody;
  legacy.display_collision = true;
  ASSERT_TRUE(editor.scene()
                  .set_mesh_component(entity, to_api_mesh_component(legacy))
                  .ok());

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
  const auto refresh = [&]() {
    panel.refresh(context);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  };
  const auto combo = [&](const char *name) {
    return panel.findChild<QComboBox *>(QString::fromUtf8(name));
  };
  const auto status = [&]() {
    return panel.findChild<QLabel *>(
        QStringLiteral("inspector.physics.collision_status"));
  };
  const auto scene_revision = session.scene().revision();
  const auto undo_label = editor.query().history().undo_label;
  refresh();
  EXPECT_EQ(combo("inspector.physics.collision_type"), nullptr);
  EXPECT_EQ(combo("inspector.physics.body_type"), nullptr);
  ASSERT_NE(status(), nullptr);
  EXPECT_EQ(status()->text(), QString::fromStdString(std::string(localization.text(
      "ui.inspector.collision_unsupported", ""))));
  EXPECT_EQ(*session.scene().mesh(entity), legacy);
  EXPECT_EQ(session.scene().revision(), scene_revision);
  EXPECT_EQ(editor.query().history().undo_label, undo_label);
  auto *generate = panel.findChild<QCheckBox *>(
      QStringLiteral("inspector.physics.generate_collisions"));
  auto *display = panel.findChild<QCheckBox *>(
      QStringLiteral("inspector.physics.display_collision"));
  ASSERT_NE(generate, nullptr);
  ASSERT_NE(display, nullptr);
  EXPECT_TRUE(generate->isChecked());
  EXPECT_TRUE(display->isChecked());
  for (int frame = 0; frame < 8; ++frame) {
    refresh();
    EXPECT_EQ(combo("inspector.physics.collision_type"), nullptr);
    EXPECT_EQ(combo("inspector.physics.body_type"), nullptr);
    EXPECT_EQ(panel.findChild<QCheckBox *>(
                  QStringLiteral("inspector.physics.generate_collisions")), generate);
    EXPECT_EQ(panel.findChild<QCheckBox *>(
                  QStringLiteral("inspector.physics.display_collision")), display);
    EXPECT_EQ(session.scene().revision(), scene_revision);
    EXPECT_EQ(*session.scene().mesh(entity), legacy);
  }

  for (const char *field : {"generate_collisions", "display_collision"}) {
    refresh();
    auto *check = panel.findChild<QCheckBox *>(
        QStringLiteral("inspector.physics.%1").arg(field));
    ASSERT_NE(check, nullptr);
    const auto undo_count = session.history().undo_count();
    check->setChecked(false);
    MeshComponent changed = legacy;
    if (std::string_view(field) == "generate_collisions") {
      changed.generate_collisions = false;
    } else {
      changed.display_collision = false;
    }
    EXPECT_EQ(*session.scene().mesh(entity), changed);
    EXPECT_EQ(session.history().undo_count(), undo_count + 1);
    refresh();
    ASSERT_NE(status(), nullptr);
    ASSERT_TRUE(editor.history().undo().ok());
    EXPECT_EQ(*session.scene().mesh(entity), legacy);
    ASSERT_TRUE(editor.history().redo().ok());
    EXPECT_EQ(*session.scene().mesh(entity), changed);
    ASSERT_TRUE(editor.history().undo().ok());
    EXPECT_EQ(*session.scene().mesh(entity), legacy);
  }

  // Supported metadata arriving through the scene owner updates the retained UI
  // without restoring the selectors or rewriting unrelated mesh properties.
  MeshComponent supported = legacy;
  supported.collision_type = MeshCollisionType::Mesh;
  supported.body_type = MeshPhysicsBodyType::StaticBody;
  ASSERT_TRUE(editor.scene()
                  .set_mesh_component(entity, to_api_mesh_component(supported))
                  .ok());
  refresh();
  EXPECT_EQ(status(), nullptr);
  EXPECT_EQ(combo("inspector.physics.collision_type"), nullptr);
  EXPECT_EQ(combo("inspector.physics.body_type"), nullptr);
  EXPECT_EQ(*session.scene().mesh(entity), supported);
  ASSERT_TRUE(editor.history().undo().ok());
  refresh();
  ASSERT_NE(status(), nullptr);
  EXPECT_EQ(*session.scene().mesh(entity), legacy);
}

TEST(InspectorPanelTests,
     SharedPhysicsEditsOnlySelectedMeshesWithMixedValuesAndOneUndoStep) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-inspector-shared-physics-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const QuaderBounds bounds{.min = {-1.0f, -1.0f, -1.0f},
                            .max = {1.0f, 1.0f, 1.0f}};
  const auto first = editor.modeling().create_box(bounds, "First mesh");
  const auto second = editor.modeling().create_box(bounds, "Second mesh");
  const auto unselected = editor.modeling().create_box(bounds, "Unselected mesh");
  const auto light = editor.scene().create_point_light();
  ASSERT_TRUE(first.ok());
  ASSERT_TRUE(second.ok());
  ASSERT_TRUE(light.ok());
  ASSERT_TRUE(unselected.ok());
  const MeshComponent original_unselected =
      *session.scene().mesh(unselected.value().entity);
  const EntityId first_id = first.value().entity;
  const EntityId second_id = second.value().entity;
  MeshComponent first_mesh = *session.scene().mesh(first_id);
  first_mesh.collision_type = MeshCollisionType::Box;
  first_mesh.body_type = MeshPhysicsBodyType::RigidBody;
  first_mesh.generate_collisions = true;
  first_mesh.display_collision = false;
  MeshComponent second_mesh = *session.scene().mesh(second_id);
  second_mesh.generate_collisions = false;
  second_mesh.display_collision = true;
  ASSERT_TRUE(editor.scene()
                  .set_mesh_component(first_id, to_api_mesh_component(first_mesh))
                  .ok());
  ASSERT_TRUE(editor.scene()
                  .set_mesh_component(second_id, to_api_mesh_component(second_mesh))
                  .ok());
  const std::array<EntityId, 3> selected{first_id, second_id, light.value().id};
  ASSERT_TRUE(editor.selection()
                  .select_objects_for_inspection(selected, light.value().id)
                  .ok());
  const LightComponent original_light = *session.scene().light(light.value().id);
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
  const auto refresh = [&]() {
    panel.refresh(context);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  };
  const auto combo = [&](const char *name) {
    return panel.findChild<QComboBox *>(QString::fromUtf8(name));
  };
  refresh();
  EXPECT_EQ(combo("inspector.physics.collision_type"), nullptr);
  EXPECT_EQ(combo("inspector.physics.body_type"), nullptr);
  for (const char *field : {"generate_collisions", "display_collision"}) {
    EXPECT_NE(panel.findChild<QLabel *>(
                  QStringLiteral("inspector.physics.%1.mixed").arg(field)),
              nullptr);
  }
  const auto before_revision = session.scene().revision();
  const auto before_undo_count = session.history().undo_count();
  auto *retained_generate = panel.findChild<QCheckBox *>(
      QStringLiteral("inspector.physics.generate_collisions"));
  auto *retained_display = panel.findChild<QCheckBox *>(
      QStringLiteral("inspector.physics.display_collision"));
  for (int frame = 0; frame < 8; ++frame) {
    refresh();
    EXPECT_EQ(combo("inspector.physics.collision_type"), nullptr);
    EXPECT_EQ(combo("inspector.physics.body_type"), nullptr);
    EXPECT_EQ(panel.findChild<QCheckBox *>(
                  QStringLiteral("inspector.physics.generate_collisions")), retained_generate);
    EXPECT_EQ(panel.findChild<QCheckBox *>(
                  QStringLiteral("inspector.physics.display_collision")), retained_display);
    EXPECT_EQ(session.scene().revision(), before_revision);
    EXPECT_EQ(session.history().undo_count(), before_undo_count);
  }
  for (const char *field : {"generate_collisions", "display_collision"}) {
    refresh();
    const MeshComponent before_first = *session.scene().mesh(first_id);
    const MeshComponent before_second = *session.scene().mesh(second_id);
    auto *check = panel.findChild<QCheckBox *>(
        QStringLiteral("inspector.physics.%1").arg(field));
    ASSERT_NE(check, nullptr);
    EXPECT_FALSE(check->isChecked());
    const auto undo_count = session.history().undo_count();
    check->setChecked(true);
    EXPECT_EQ(session.history().undo_count(), undo_count + 1);
    if (std::string_view(field) == "generate_collisions") {
      EXPECT_TRUE(session.scene().mesh(first_id)->generate_collisions);
      EXPECT_TRUE(session.scene().mesh(second_id)->generate_collisions);
    } else {
      EXPECT_TRUE(session.scene().mesh(first_id)->display_collision);
      EXPECT_TRUE(session.scene().mesh(second_id)->display_collision);
    }
    EXPECT_EQ(session.scene().mesh(first_id)->collision_type, first_mesh.collision_type);
    EXPECT_EQ(session.scene().mesh(first_id)->body_type, first_mesh.body_type);
    EXPECT_EQ(session.scene().mesh(second_id)->collision_type, second_mesh.collision_type);
    EXPECT_EQ(session.scene().mesh(second_id)->body_type, second_mesh.body_type);
    EXPECT_EQ(*session.scene().mesh(unselected.value().entity), original_unselected);
    const MeshComponent changed_first = *session.scene().mesh(first_id);
    const MeshComponent changed_second = *session.scene().mesh(second_id);
    ASSERT_TRUE(editor.history().undo().ok());
    EXPECT_EQ(*session.scene().mesh(first_id), before_first);
    EXPECT_EQ(*session.scene().mesh(second_id), before_second);
    ASSERT_TRUE(editor.history().redo().ok());
    EXPECT_EQ(*session.scene().mesh(first_id), changed_first);
    EXPECT_EQ(*session.scene().mesh(second_id), changed_second);
    ASSERT_TRUE(editor.history().undo().ok());
    EXPECT_EQ(*session.scene().mesh(first_id), before_first);
    EXPECT_EQ(*session.scene().mesh(second_id), before_second);
  }
  EXPECT_EQ(*session.scene().light(light.value().id), original_light);
  const auto other_light = editor.scene().create_point_light();
  ASSERT_TRUE(other_light.ok());
  const std::array<EntityId, 2> non_meshes{light.value().id, other_light.value().id};
  ASSERT_TRUE(editor.selection()
                  .select_objects_for_inspection(non_meshes, light.value().id)
                  .ok());
  refresh();
  EXPECT_EQ(combo("inspector.physics.collision_type"), nullptr);
  EXPECT_EQ(combo("inspector.physics.body_type"), nullptr);
  EXPECT_EQ(panel.findChild<QCheckBox *>(
                QStringLiteral("inspector.physics.generate_collisions")), nullptr);
  EXPECT_EQ(panel.findChild<QCheckBox *>(
                QStringLiteral("inspector.physics.display_collision")), nullptr);
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
     ExternalTransformPreviewUpdatesRetainedFieldsBeforeCommitWithoutEdits) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-inspector-live-transform-preview-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  constexpr float kDegreesToRadians = 0.017453292519943295f;
  constexpr std::array<const char *, 9> kFields{{
      "inspector.transform.position.x", "inspector.transform.position.y",
      "inspector.transform.position.z", "inspector.transform.rotation.x",
      "inspector.transform.rotation.y", "inspector.transform.rotation.z",
      "inspector.transform.scale.x", "inspector.transform.scale.y",
      "inspector.transform.scale.z"}};

  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto box = editor.modeling().create_box(
      QuaderBounds{.min = {-1.0f, -2.0f, -3.0f},
                   .max = {1.0f, 2.0f, 3.0f}},
      "Live Inspector Transform Mesh");
  ASSERT_TRUE(box.ok()) << box.error_message();
  ASSERT_TRUE(editor.selection().select_object(box.value()).ok());
  const EntityId entity = box.value().entity;
  const TransformComponent baseline = *session.scene().transform(entity);
  const std::size_t undo_before = session.history().undo_count();
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
  const auto refresh = [&]() {
    panel.refresh(context);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  };
  refresh();
  std::array<QPointer<QDoubleSpinBox>, 9> retained;
  int value_changed_count = 0;
  int editing_finished_count = 0;
  for (std::size_t index = 0; index < kFields.size(); ++index) {
    auto *field = panel.findChild<QDoubleSpinBox *>(QString::fromUtf8(kFields[index]));
    ASSERT_NE(field, nullptr);
    retained[index] = field;
    QObject::connect(field, &QDoubleSpinBox::valueChanged, &panel,
                     [&](double) { ++value_changed_count; });
    QObject::connect(field, &QDoubleSpinBox::editingFinished, &panel,
                     [&]() { ++editing_finished_count; });
  }
  const auto expect_fields = [&](const TransformComponent &transform) {
    const std::array<double, 9> expected{{
        transform.position.x, transform.position.y, transform.position.z,
        transform.rotation_euler.x / kDegreesToRadians,
        transform.rotation_euler.y / kDegreesToRadians,
        transform.rotation_euler.z / kDegreesToRadians,
        transform.scale.x, transform.scale.y, transform.scale.z}};
    for (std::size_t index = 0; index < kFields.size(); ++index) {
      SCOPED_TRACE(kFields[index]);
      auto *field = panel.findChild<QDoubleSpinBox *>(QString::fromUtf8(kFields[index]));
      ASSERT_NE(field, nullptr);
      EXPECT_FALSE(retained[index].isNull());
      EXPECT_EQ(field, retained[index].data());
      EXPECT_NEAR(field->value(), expected[index], 0.001);
    }
    EXPECT_EQ(value_changed_count, 0);
    EXPECT_EQ(editing_finished_count, 0);
  };
  const std::array<TransformComponent, 2> samples{{
      {.position = {2.0f, 3.0f, 4.0f},
       .rotation_euler = {15.0f * kDegreesToRadians, 35.0f * kDegreesToRadians,
                          55.0f * kDegreesToRadians},
       .scale = {1.2f, 1.4f, 1.6f}},
      {.position = {3.0f, 4.0f, 5.0f},
       .rotation_euler = {25.0f * kDegreesToRadians, 45.0f * kDegreesToRadians,
                          65.0f * kDegreesToRadians},
       .scale = {2.0f, 3.0f, 4.0f}},
  }};
  const auto refresh_live = [&]() {
    panel.refresh_transform_preview(context);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  };
  SceneTransformDragService drag;
  ASSERT_TRUE(drag.begin(session.scene(), {}, true, "Gizmo Transform",
                         {}, &session.assets()));
  for (const TransformComponent &sample : samples) {
    const auto update = drag.preview_transform(entity, sample);
    ASSERT_TRUE(update.ok) << update.message;
    ASSERT_TRUE(drag.preview_active());
    refresh_live();
    expect_fields(sample);
    EXPECT_EQ(session.history().undo_count(), undo_before);
    const auto scene_revision = session.scene().revision();
    for (int frame = 0; frame < 4; ++frame) {
      refresh_live();
      expect_fields(sample);
    }
    EXPECT_EQ(session.scene().revision(), scene_revision);
    EXPECT_EQ(session.history().undo_count(), undo_before);
  }

  ASSERT_TRUE(drag.cancel().ok);
  refresh();
  expect_fields(baseline);
  EXPECT_EQ(session.history().undo_count(), undo_before);

  ASSERT_TRUE(drag.begin(session.scene(), {}, true, "Gizmo Transform",
                         {}, &session.assets()));
  ASSERT_TRUE(drag.preview_transform(entity, samples.back()).ok);
  refresh_live();
  expect_fields(samples.back());
  ASSERT_TRUE(drag.commit(editor).ok());
  refresh();
  expect_fields(samples.back());
  EXPECT_EQ(session.history().undo_count(), undo_before + 1u);
  ASSERT_TRUE(editor.history().undo().ok());
  refresh();
  expect_fields(baseline);
  ASSERT_TRUE(editor.history().redo().ok());
  refresh();
  expect_fields(samples.back());
  EXPECT_EQ(session.history().undo_count(), undo_before + 1u);

  auto *position_x = panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("inspector.transform.position.x"));
  ASSERT_NE(position_x, nullptr);
  position_x->setValue(7.5);
  position_x->editingFinished();
  refresh();
  const auto edited = editor.query().entity(entity);
  ASSERT_TRUE(edited.has_value());
  EXPECT_NEAR(edited->transform.position.x, 7.5f, 0.0001f);
  EXPECT_NEAR(edited->transform.position.y, samples.back().position.y, 0.0001f);
  EXPECT_NEAR(edited->transform.position.z, samples.back().position.z, 0.0001f);
  EXPECT_EQ(session.history().undo_count(), undo_before + 2u);
  EXPECT_EQ(value_changed_count, 1);
  EXPECT_EQ(editing_finished_count, 1);
}

TEST(InspectorPanelTests,
     ExternalTransformPreviewChangesMixedFieldsToNumericAndBack) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-inspector-live-mixed-transform-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto first = editor.scene().create_empty();
  const auto second = editor.scene().create_empty();
  ASSERT_TRUE(first.ok());
  ASSERT_TRUE(second.ok());
  ASSERT_TRUE(editor.scene()
                  .select({first.value().id, second.value().id}, first.value().id)
                  .ok());
  const std::size_t undo_before = session.history().undo_count();
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
  const QString field_name = QStringLiteral("inspector.transform.position.x");
  ASSERT_NE(panel.findChild<QDoubleSpinBox *>(field_name), nullptr);
  const auto refresh_live = [&]() {
    panel.refresh_transform_preview(context);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  };
  SceneTransformDragService drag;
  ASSERT_TRUE(drag.begin(session.scene(), {}, true, "Gizmo Transform"));
  ASSERT_TRUE(drag.preview_transform(second.value().id,
                                    {.position = {2.0f, 0.0f, 0.0f}}).ok);
  refresh_live();
  EXPECT_EQ(panel.findChild<QDoubleSpinBox *>(field_name), nullptr);
  auto *mixed = panel.findChild<QLineEdit *>(field_name);
  ASSERT_NE(mixed, nullptr);
  EXPECT_EQ(mixed->text(), QStringLiteral("Mixed"));
  EXPECT_TRUE(mixed->isReadOnly());
  EXPECT_FALSE(mixed->isEnabled());

  ASSERT_TRUE(drag.preview_transform(second.value().id, {}).ok);
  refresh_live();
  auto *numeric = panel.findChild<QDoubleSpinBox *>(field_name);
  ASSERT_NE(numeric, nullptr);
  EXPECT_DOUBLE_EQ(numeric->value(), 0.0);
  QPointer<QDoubleSpinBox> retained(numeric);
  refresh_live();
  EXPECT_EQ(panel.findChild<QDoubleSpinBox *>(field_name), retained.data());
  ASSERT_TRUE(drag.preview_transform(second.value().id,
                                    {.position = {3.0f, 0.0f, 0.0f}}).ok);
  refresh_live();
  EXPECT_EQ(panel.findChild<QDoubleSpinBox *>(field_name), nullptr);
  ASSERT_NE(panel.findChild<QLineEdit *>(field_name), nullptr);
  ASSERT_TRUE(drag.cancel().ok);
  panel.refresh(context);
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  numeric = panel.findChild<QDoubleSpinBox *>(field_name);
  ASSERT_NE(numeric, nullptr);
  EXPECT_DOUBLE_EQ(numeric->value(), 0.0);
  EXPECT_EQ(session.history().undo_count(), undo_before);
}

TEST(InspectorPanelTests,
     MeshQuarterTurnRemainsRequestedAfterEditingFinishesAndRefreshes) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-inspector-mesh-quarter-turn-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  constexpr float kHalfPi = 1.57079632679f;
  constexpr std::array<const char *, 3> kFields{{
      "inspector.transform.rotation.x", "inspector.transform.rotation.y",
      "inspector.transform.rotation.z"}};

  for (std::size_t axis = 0; axis < kFields.size(); ++axis) {
    SCOPED_TRACE(kFields[axis]);
    SceneSession session;
    QuaderEditorApi editor(
        QuaderApiSessionInternalAccess::from_scene_session(session));
    const auto box = editor.modeling().create_box(
        QuaderBounds{.min = {-1.0f, -2.0f, -3.0f},
                     .max = {1.0f, 2.0f, 3.0f}},
        "Inspector Quarter Turn Mesh");
    ASSERT_TRUE(box.ok()) << box.error_message();
    ASSERT_TRUE(editor.selection().select_object(box.value()).ok());
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
    const auto refresh = [&]() {
      panel.refresh(context);
      QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    };
    refresh();
    auto *rotation = panel.findChild<QDoubleSpinBox *>(
        QString::fromUtf8(kFields[axis]));
    ASSERT_NE(rotation, nullptr);
    const std::size_t undo_before = session.history().undo_count();
    rotation->setValue(90.0);
    EXPECT_EQ(session.history().undo_count(), undo_before);
    rotation->editingFinished();
    refresh();

    const auto snapshot = editor.query().entity(box.value().entity);
    ASSERT_TRUE(snapshot.has_value());
    const QVec3 stored = snapshot->transform.rotation_euler;
    const std::array<float, 3> components{{stored.x, stored.y, stored.z}};
    for (std::size_t component = 0; component < kFields.size(); ++component) {
      auto *field = panel.findChild<QDoubleSpinBox *>(
          QString::fromUtf8(kFields[component]));
      ASSERT_NE(field, nullptr);
      EXPECT_NEAR(field->value(), component == axis ? 90.0 : 0.0, 0.001);
      EXPECT_NEAR(components[component], component == axis ? kHalfPi : 0.0f,
                  0.0001f);
    }
    EXPECT_EQ(session.history().undo_count(), undo_before + 1u);
    QPointer<QDoubleSpinBox> retained(panel.findChild<QDoubleSpinBox *>(
        QString::fromUtf8(kFields[axis])));
    const auto scene_revision = session.scene().revision();
    refresh();
    refresh();
    EXPECT_EQ(panel.findChild<QDoubleSpinBox *>(
                  QString::fromUtf8(kFields[axis])),
              retained.data());
    EXPECT_EQ(session.scene().revision(), scene_revision);
    EXPECT_EQ(session.history().undo_count(), undo_before + 1u);
  }
}

TEST(InspectorPanelTests,
     MixedMeshRotationUsesAbsoluteFieldsAndZeroRestoresOriginalBasis) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-inspector-mixed-mesh-rotation-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  constexpr float kDegreesToRadians = 0.017453292519943295f;
  constexpr std::array<const char *, 3> kFields{{
      "inspector.transform.rotation.x", "inspector.transform.rotation.y",
      "inspector.transform.rotation.z"}};

  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto box = editor.modeling().create_box(
      QuaderBounds{.min = {-1.0f, -2.0f, -3.0f},
                   .max = {1.0f, 2.0f, 3.0f}},
      "Inspector Mixed Rotation Mesh");
  ASSERT_TRUE(box.ok()) << box.error_message();
  ASSERT_TRUE(editor.selection().select_object(box.value()).ok());
  const EntityId entity = box.value().entity;
  const auto original_mesh = editor.query().compiled_object_mesh(box.value());
  ASSERT_TRUE(original_mesh.has_value());
  const QMat4 original_world = session.scene().world_transform_matrix(entity);
  ASSERT_TRUE(editor.scene()
                  .set_transform(entity,
                                 {.rotation_euler =
                                      {20.0f * kDegreesToRadians,
                                       35.0f * kDegreesToRadians,
                                       50.0f * kDegreesToRadians}})
                  .ok());

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
  const auto refresh = [&]() {
    panel.refresh(context);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  };
  const auto edit_axis = [&](std::size_t axis, double degrees) {
    auto *field = panel.findChild<QDoubleSpinBox *>(
        QString::fromUtf8(kFields[axis]));
    ASSERT_NE(field, nullptr);
    field->setValue(degrees);
    field->editingFinished();
    refresh();
  };
  const auto expect_angles = [&](const std::array<double, 3> &degrees) {
    const auto snapshot = editor.query().entity(entity);
    ASSERT_TRUE(snapshot.has_value());
    const QVec3 stored = snapshot->transform.rotation_euler;
    const std::array<float, 3> components{{stored.x, stored.y, stored.z}};
    for (std::size_t axis = 0; axis < kFields.size(); ++axis) {
      auto *field = panel.findChild<QDoubleSpinBox *>(
          QString::fromUtf8(kFields[axis]));
      ASSERT_NE(field, nullptr);
      EXPECT_NEAR(field->value(), degrees[axis], 0.001);
      EXPECT_NEAR(components[axis], degrees[axis] * kDegreesToRadians, 0.0001);
    }
  };
  refresh();
  edit_axis(0u, 70.0);
  expect_angles({70.0, 35.0, 50.0});
  edit_axis(0u, 0.0);
  expect_angles({0.0, 35.0, 50.0});
  edit_axis(1u, 0.0);
  expect_angles({0.0, 0.0, 50.0});
  edit_axis(2u, 0.0);
  expect_angles({0.0, 0.0, 0.0});

  const auto expect_original_geometry = [&]() {
    const auto mesh = editor.query().compiled_object_mesh(box.value());
    ASSERT_TRUE(mesh.has_value());
    ASSERT_EQ(mesh->vertices.size(), original_mesh->vertices.size());
    EXPECT_EQ(mesh->indices, original_mesh->indices);
    for (std::size_t index = 0; index < mesh->vertices.size(); ++index) {
      const QVec3 original = original_mesh->vertices[index].position;
      const QVec3 actual = mesh->vertices[index].position;
      EXPECT_FLOAT_EQ(actual.x, original.x);
      EXPECT_FLOAT_EQ(actual.y, original.y);
      EXPECT_FLOAT_EQ(actual.z, original.z);
    }
    const QMat4 restored = session.scene().world_transform_matrix(entity);
    for (const QVec3 axis : std::array<QVec3, 3>{{
             {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}}) {
      const QVec3 expected = transform_vector(original_world, axis);
      const QVec3 actual = transform_vector(restored, axis);
      EXPECT_NEAR(actual.x, expected.x, 0.0001f);
      EXPECT_NEAR(actual.y, expected.y, 0.0001f);
      EXPECT_NEAR(actual.z, expected.z, 0.0001f);
    }
  };
  expect_original_geometry();
  ASSERT_TRUE(editor.history().undo().ok());
  refresh();
  expect_angles({0.0, 0.0, 50.0});
  ASSERT_TRUE(editor.history().redo().ok());
  refresh();
  expect_angles({0.0, 0.0, 0.0});
  expect_original_geometry();
}

TEST(InspectorPanelTests,
     RuntimeAbandonDisarmsTransformPreviewBeforeSceneOwnerRelease) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-inspector-runtime-abandon-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  UISettings ui_settings = UISettings::defaults();
  ViewportSettings viewport_settings = ViewportSettings::defaults();
  LocalizationService localization = loaded_localization();
  InspectorPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);

  auto expired_session = std::make_unique<SceneSession>();
  auto expired_editor = std::make_unique<QuaderEditorApi>(
      QuaderApiSessionInternalAccess::from_scene_session(*expired_session));
  const QuaderApiResult<QuaderEntityRef> expired_entity =
      expired_editor->scene().create_entity({
          .name = "Expiring Inspector Owner",
          .kind = QuaderEntityKind::Empty,
      });
  ASSERT_TRUE(expired_entity.ok()) << expired_entity.error_message();
  ASSERT_TRUE(
      expired_editor->scene().select(expired_entity.value().id).ok());
  ASSERT_NE(expired_session->history().undo_label(), nullptr);
  const std::string undo_label_before_preview =
      *expired_session->history().undo_label();
  {
    UIInspectorPreviewContext preview{
        .document = expired_session->scene(),
        .assets = expired_session->assets(),
    };
    UIContext context{
        .intent = {.editor = *expired_editor},
        .scene = {.document = expired_session->scene(),
                  .assets = expired_session->assets(),
                  .modeling = expired_session->modeling(),
                  .selection = expired_session->selection()},
        .inspector_preview = &preview,
        .preferences = {.ui = ui_settings, .viewport = viewport_settings},
        .localization = localization,
    };
    panel.refresh(context);
  }

  auto *position_x = panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("inspector.transform.position.x"));
  ASSERT_NE(position_x, nullptr);
  position_x->setValue(4.25);
  const TransformComponent *previewed =
      expired_session->scene().transform(expired_entity.value().id);
  ASSERT_NE(previewed, nullptr);
  EXPECT_FLOAT_EQ(previewed->position.x, 4.25f);
  ASSERT_NE(expired_session->history().undo_label(), nullptr);
  EXPECT_EQ(*expired_session->history().undo_label(),
            undo_label_before_preview);

  // The host is allowed to release the old SceneSession before its binding
  // callback. Abandonment must therefore be pointer-free and leave no preview
  // destructor that tries to restore through the expired document.
  panel.abandon_runtime();
  expired_editor.reset();
  expired_session.reset();

  SceneSession replacement_session;
  QuaderEditorApi replacement_editor(
      QuaderApiSessionInternalAccess::from_scene_session(replacement_session));
  const QuaderApiResult<QuaderEntityRef> replacement_entity =
      replacement_editor.scene().create_entity({
          .name = "Replacement Inspector Owner",
          .kind = QuaderEntityKind::Empty,
      });
  ASSERT_TRUE(replacement_entity.ok()) << replacement_entity.error_message();
  ASSERT_TRUE(
      replacement_editor.scene().select(replacement_entity.value().id).ok());
  UIInspectorPreviewContext replacement_preview{
      .document = replacement_session.scene(),
      .assets = replacement_session.assets(),
  };
  UIContext replacement_context{
      .intent = {.editor = replacement_editor},
      .scene = {.document = replacement_session.scene(),
                .assets = replacement_session.assets(),
                .modeling = replacement_session.modeling(),
                .selection = replacement_session.selection()},
      .inspector_preview = &replacement_preview,
      .preferences = {.ui = ui_settings, .viewport = viewport_settings},
      .localization = localization,
  };
  panel.refresh(replacement_context);
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

  position_x = panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("inspector.transform.position.x"));
  ASSERT_NE(position_x, nullptr);
  EXPECT_DOUBLE_EQ(position_x->value(), 0.0);
  panel.abandon_runtime();
}

TEST(EnvironmentSettingsPopupTests,
     SectionsDefaultExpandedRememberChoiceAndEnvironmentRotationUsesCustomSpinBox) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-inspector-section-rotation-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
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
  QtEnvironmentSettingsPopup panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.refresh(context);

  auto *tree = panel.findChild<QTreeWidget *>(QStringLiteral("environmentSettings.tree"));
  auto *rotation = panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("environmentSettings.environment.rotation"));
  ASSERT_NE(tree, nullptr);
  ASSERT_NE(rotation, nullptr);
  ASSERT_NE(dynamic_cast<QtDoubleSpinBox *>(rotation), nullptr);
  EXPECT_TRUE(panel.findChildren<QSlider *>().empty());
  ASSERT_GT(tree->topLevelItemCount(), 0);
  for (int row = 0; row < tree->topLevelItemCount(); ++row) {
    EXPECT_TRUE(tree->topLevelItem(row)->isExpanded())
        << tree->topLevelItem(row)->text(0).toStdString();
  }
  EXPECT_DOUBLE_EQ(rotation->minimum(), -180.0);
  EXPECT_DOUBLE_EQ(rotation->maximum(), 180.0);
  EXPECT_DOUBLE_EQ(rotation->singleStep(), 1.0);
  EXPECT_DOUBLE_EQ(rotation->value(), 0.0);

  rotation->setValue(rotation->minimum());
  EXPECT_DOUBLE_EQ(rotation->value(), -180.0);
  EXPECT_NEAR(session.scene().scene_settings().environment.rotation_y,
              -3.14159265358979323846, 1.0e-5);
  rotation->setValue(rotation->maximum());
  EXPECT_DOUBLE_EQ(rotation->value(), 180.0);
  EXPECT_NEAR(session.scene().scene_settings().environment.rotation_y,
              3.14159265358979323846, 1.0e-5);
  rotation->setValue(0);
  EXPECT_DOUBLE_EQ(rotation->value(), 0.0);
  EXPECT_FLOAT_EQ(
      session.scene().scene_settings().environment.rotation_y, 0.0f);
  rotation->setValue(45.0);
  rotation->editingFinished();
  EXPECT_NEAR(session.scene().scene_settings().environment.rotation_y,
              0.78539816339744830962, 1.0e-5);

  const auto find_section = [tree](const QString &stable_id) {
    for (int row = 0; row < tree->topLevelItemCount(); ++row) {
      QTreeWidgetItem *item = tree->topLevelItem(row);
      if (item->data(0, Qt::UserRole).toString() == stable_id) {
        return item;
      }
    }
    return static_cast<QTreeWidgetItem *>(nullptr);
  };
  QTreeWidgetItem *environment = find_section(QStringLiteral("environment"));
  ASSERT_NE(environment, nullptr);
  environment->setExpanded(false);
  ASSERT_TRUE(localization.select_locale("it-IT"));
  panel.refresh(context);
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  tree = panel.findChild<QTreeWidget *>(QStringLiteral("environmentSettings.tree"));
  ASSERT_NE(tree, nullptr);
  QTreeWidgetItem *rebuilt_environment = nullptr;
  for (int row = 0; row < tree->topLevelItemCount(); ++row) {
    QTreeWidgetItem *item = tree->topLevelItem(row);
    if (item->data(0, Qt::UserRole).toString() ==
        QStringLiteral("environment")) {
      rebuilt_environment = item;
      break;
    }
  }
  ASSERT_NE(rebuilt_environment, nullptr);
  EXPECT_FALSE(rebuilt_environment->isExpanded());
}

TEST(EnvironmentSettingsPopupTests,
      CameraClipEditsPreserveInactivePhotographicMetadata) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-inspector-exposure-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  QuaderSceneSettings settings =
      to_api_scene_settings(session.scene().scene_settings());
  settings.camera.use_exposure = true;
  settings.camera.aperture = 4.0f;
  settings.camera.shutter_speed = 0.01f;
  settings.camera.sensitivity = 800.0f;
  ASSERT_TRUE(editor.scene().set_scene_settings(settings).ok());

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
  QtEnvironmentSettingsPopup panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  const auto expect_controls = [&]() {
    panel.refresh(context);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    const auto *toggle = panel.findChild<QCheckBox *>(
        QStringLiteral("environmentSettings.camera.use_exposure"));
    EXPECT_EQ(toggle, nullptr);
    for (const char *name : {"aperture", "shutter", "sensitivity"}) {
      const auto *input = panel.findChild<QDoubleSpinBox *>(
          QStringLiteral("environmentSettings.camera.") + QString::fromLatin1(name));
      EXPECT_EQ(input, nullptr) << name;
    }
    for (const char *name : {"near", "far"}) {
      const auto *input = panel.findChild<QDoubleSpinBox *>(
          QStringLiteral("environmentSettings.camera.") + QString::fromLatin1(name));
      ASSERT_NE(input, nullptr);
      EXPECT_TRUE(input->isEnabled()) << name;
    }
    const SceneCameraSettings &camera =
        session.scene().scene_settings().camera;
    EXPECT_EQ(camera.use_exposure, settings.camera.use_exposure);
    EXPECT_FLOAT_EQ(camera.aperture, settings.camera.aperture);
    EXPECT_FLOAT_EQ(camera.shutter_speed, settings.camera.shutter_speed);
    EXPECT_FLOAT_EQ(camera.sensitivity, settings.camera.sensitivity);
  };
  session.mark_clean();
  const std::size_t undo_count = session.history().undo_count();
  expect_controls();
  EXPECT_FALSE(session.dirty());
  EXPECT_EQ(session.history().undo_count(), undo_count);
  auto *near_plane = panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("environmentSettings.camera.near"));
  ASSERT_NE(near_plane, nullptr);
  near_plane->setValue(1.25);
  near_plane->editingFinished();
  expect_controls();
  EXPECT_FLOAT_EQ(session.scene().scene_settings().camera.near_plane, 1.25f);
  ASSERT_TRUE(editor.history().undo().ok());
  expect_controls();
  EXPECT_FLOAT_EQ(session.scene().scene_settings().camera.near_plane,
                  settings.camera.near_plane);
  ASSERT_TRUE(editor.history().redo().ok());
  expect_controls();
  EXPECT_FLOAT_EQ(session.scene().scene_settings().camera.near_plane, 1.25f);
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
  const QColor committed_light_color = QColor::fromRgb(31, 127, 223);
  const QVec3 expected_light_color = light_srgb_to_linear(
      {static_cast<float>(committed_light_color.redF()),
       static_cast<float>(committed_light_color.greenF()),
       static_cast<float>(committed_light_color.blueF())});
  session.mark_clean();
  ASSERT_TRUE(reject_color_picker(
      panel, *light_swatch, QStringLiteral("inspector.light.color.dialog"),
      committed_light_color, [&]() {
        const LightComponent *during = session.scene().light(light.value().id);
        EXPECT_NE(during, nullptr);
        if (during != nullptr) {
          EXPECT_FLOAT_EQ(during->color.x, expected_light_color.x);
          EXPECT_FLOAT_EQ(during->color.y, expected_light_color.y);
          EXPECT_FLOAT_EQ(during->color.z, expected_light_color.z);
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

  ASSERT_TRUE(accept_color_picker(
      panel, *light_swatch, QStringLiteral("inspector.light.color.dialog"),
      committed_light_color));
  const LightComponent *committed_light =
      session.scene().light(light.value().id);
  ASSERT_NE(committed_light, nullptr);
  EXPECT_FLOAT_EQ(committed_light->color.x, expected_light_color.x);
  EXPECT_FLOAT_EQ(committed_light->color.y, expected_light_color.y);
  EXPECT_FLOAT_EQ(committed_light->color.z, expected_light_color.z);
  EXPECT_TRUE(session.dirty());
  EXPECT_EQ(history_label(session.history().undo_label()),
            std::optional<std::string>("Set Light Component"));
  ASSERT_TRUE(editor.history().undo().ok());

  QtEnvironmentSettingsPopup environment_panel;
  environment_panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  environment_panel.refresh(context);
  auto *clear_color_swatch = environment_panel.findChild<QPushButton *>(
      QStringLiteral("environmentSettings.environment.clear_color"));
  ASSERT_NE(clear_color_swatch, nullptr);
  const SceneSettings initial_scene =
      session.scene().scene_settings();
  const std::optional<std::string> scene_undo_before =
      history_label(session.history().undo_label());
  const std::optional<std::string> scene_redo_before =
      history_label(session.history().redo_label());
  const QColor committed_scene_color = QColor::fromRgb(211, 83, 47);
  session.mark_clean();
  ASSERT_TRUE(reject_color_picker(
      environment_panel, *clear_color_swatch,
      QStringLiteral("environmentSettings.environment.clear_color.dialog"),
      committed_scene_color, [&]() {
        const SceneSettings *during =
            &session.scene().scene_settings();
        EXPECT_NE(during, nullptr);
        if (during != nullptr) {
          EXPECT_FLOAT_EQ(during->environment.clear_color.x,
                          static_cast<float>(committed_scene_color.redF()));
          EXPECT_FLOAT_EQ(during->environment.clear_color.y,
                          static_cast<float>(committed_scene_color.greenF()));
          EXPECT_FLOAT_EQ(during->environment.clear_color.z,
                          static_cast<float>(committed_scene_color.blueF()));
        }
        EXPECT_FALSE(session.dirty());
        EXPECT_EQ(history_label(session.history().undo_label()),
                  scene_undo_before);
        EXPECT_EQ(history_label(session.history().redo_label()),
                  scene_redo_before);
      }));
  const SceneSettings *after_scene =
      &session.scene().scene_settings();
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

  ASSERT_TRUE(accept_color_picker(
      environment_panel, *clear_color_swatch,
      QStringLiteral("environmentSettings.environment.clear_color.dialog"),
      committed_scene_color));
  const SceneSettings *committed_scene =
      &session.scene().scene_settings();
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

TEST(QtMainMenuTests,
     CollisionDisplayTogglePersistsWithoutChangingMeshMetadata) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-collision-display-menu-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  const std::filesystem::path preferences_path =
      std::filesystem::path(directory.path().toStdWString()) / "viewport.ini";

  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  ASSERT_TRUE(editor.commands()
                  .set_keybinding_storage_path(
                      preferences_path.parent_path() / "keybindings.json")
                  .ok());
  const auto box = editor.modeling().create_box(
      QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                   .max = {1.0f, 1.0f, 1.0f}}, "Collision Display Mesh");
  ASSERT_TRUE(box.ok()) << box.error_message();
  const MeshComponent original = *session.scene().mesh(box.value().entity);
  const auto scene_revision = session.scene().revision();
  const auto undo_label = editor.query().history().undo_label;
  ViewportSettings settings = ViewportSettings::defaults();
  EXPECT_TRUE(settings.bool_value(ViewportBoolSettingId::ShowCollisions));
  editor.preferences().set_runtime_hooks(QuaderPreferencesRuntimeHooks{
      .collisions_visible = [&] {
        return settings.bool_value(ViewportBoolSettingId::ShowCollisions);
      },
      .set_collisions_visible = [&](bool visible) {
        const bool changed = settings.set_bool_value(
            ViewportBoolSettingId::ShowCollisions, visible);
        if (changed) {
          EXPECT_TRUE(settings.save(preferences_path));
        }
        return QuaderApiVoidResult{
            .ok = true,
            .changed = changed,
            .mutation_kind = QuaderMutationKind::Runtime,
        };
      },
  });
  LocalizationService localization = loaded_localization();
  QMenuBar menu_bar;
  QtMainMenu menu;
  menu.initialize(menu_bar, QtMainMenuContext{
                               .editor = editor,
                               .modeling = session.modeling(),
                               .viewport_settings = settings,
                               .localization = localization,
                           });
  auto *view = menu_bar.findChild<QMenu *>(QStringLiteral("menu_view"));
  auto *display = menu_bar.findChild<QMenu *>(QStringLiteral("menu_view_display"));
  auto *action = menu_bar.findChild<QAction *>(
      QStringLiteral("command_viewport_toggle_collisions"));
  ASSERT_NE(view, nullptr);
  ASSERT_NE(display, nullptr);
  ASSERT_NE(action, nullptr);
  ASSERT_TRUE(view->actions().contains(display->menuAction()));
  ASSERT_TRUE(display->actions().contains(action));
  ASSERT_TRUE(QMetaObject::invokeMethod(view, "aboutToShow", Qt::DirectConnection));
  EXPECT_EQ(action->text(), QStringLiteral("Collisions"));
  EXPECT_TRUE(action->isCheckable());
  EXPECT_TRUE(action->isChecked());
  EXPECT_TRUE(action->shortcut().isEmpty());

  action->trigger();
  menu.refresh();
  EXPECT_FALSE(action->isChecked());
  EXPECT_FALSE(editor.preferences().collisions_visible());
  EXPECT_FALSE(ViewportSettings::load(preferences_path)
                   .bool_value(ViewportBoolSettingId::ShowCollisions));
  EXPECT_TRUE(settings.bool_value(ViewportBoolSettingId::ShowOverlays));
  EXPECT_EQ(*session.scene().mesh(box.value().entity), original);
  EXPECT_EQ(session.scene().revision(), scene_revision);
  EXPECT_EQ(editor.query().history().undo_label, undo_label);
  EXPECT_FALSE(editor.preferences().set_collisions_visible(false).changed());

  ASSERT_TRUE(editor.commands()
                  .execute(quader_command::kViewportToggleCollisions)
                  .changed());
  ASSERT_TRUE(QMetaObject::invokeMethod(view, "aboutToShow", Qt::DirectConnection));
  EXPECT_TRUE(action->isChecked());
  EXPECT_TRUE(ViewportSettings::load(preferences_path)
                  .bool_value(ViewportBoolSettingId::ShowCollisions));
  const auto catalog = editor.commands().keybinding_catalog();
  const auto *binding = find_keybinding_catalog_entry(
      catalog, quader_command::kViewportToggleCollisions.string());
  ASSERT_NE(binding, nullptr);
  EXPECT_TRUE(binding->default_shortcut.empty());
  EXPECT_TRUE(binding->unassigned);
  EXPECT_EQ(loaded_localization("it-IT").text("ui.menu.view.collisions", ""),
            "Collisioni");
}

TEST(QtMainMenuTests,
     StableTaxonomyKeepsSelectionCommandsDiscoverableInSelectMenu) {
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
          QuaderBounds{.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
          "Select Menu Box");
  ASSERT_TRUE(box.ok()) << box.error_message();
  ASSERT_TRUE(editor.selection().select_object(box.value()).ok());

  ViewportSettings viewport_settings = ViewportSettings::defaults();
  const auto runtime_result = [](bool changed) {
    return QuaderApiVoidResult{
        .ok = true,
        .changed = changed,
        .message = {},
        .mutation_kind = QuaderMutationKind::Runtime,
    };
  };
  editor.preferences().set_runtime_hooks(QuaderPreferencesRuntimeHooks{
      .backface_selection_enabled =
          [&] {
            return viewport_settings.bool_value(
                ViewportBoolSettingId::BackfaceSelection);
          },
      .set_backface_selection_enabled =
          [&](bool enabled) {
            return runtime_result(viewport_settings.set_bool_value(
                ViewportBoolSettingId::BackfaceSelection, enabled));
          },
  });
  LocalizationService localization = loaded_localization();
  QMenuBar menu_bar;
  QtMainMenu menu;
  menu.initialize(menu_bar, QtMainMenuContext{
                                .editor = editor,
                                .modeling = session.modeling(),
                                .viewport_settings = viewport_settings,
                                .localization = localization,
                            });

  auto *edit_menu = menu_bar.findChild<QMenu *>(QStringLiteral("menu_edit"));
  auto *select_menu =
      menu_bar.findChild<QMenu *>(QStringLiteral("menu_select"));
  auto *file_menu = menu_bar.findChild<QMenu *>(QStringLiteral("menu_file"));
  auto *create_menu =
      menu_bar.findChild<QMenu *>(QStringLiteral("menu_create"));
  auto *object_menu =
      menu_bar.findChild<QMenu *>(QStringLiteral("menu_object"));
  auto *mesh_menu = menu_bar.findChild<QMenu *>(QStringLiteral("menu_mesh"));
  auto *vertex_menu =
      menu_bar.findChild<QMenu *>(QStringLiteral("menu_vertex"));
  auto *edge_menu = menu_bar.findChild<QMenu *>(QStringLiteral("menu_edge"));
  auto *face_menu = menu_bar.findChild<QMenu *>(QStringLiteral("menu_face"));
  auto *uv_menu = menu_bar.findChild<QMenu *>(QStringLiteral("menu_uv"));
  auto *uv_projection_menu =
      menu_bar.findChild<QMenu *>(QStringLiteral("menu_uv_projection"));
  auto *uv_align_menu =
      menu_bar.findChild<QMenu *>(QStringLiteral("menu_uv_align"));
  auto *uv_flip_menu =
      menu_bar.findChild<QMenu *>(QStringLiteral("menu_uv_flip"));
  auto *uv_fit_menu =
      menu_bar.findChild<QMenu *>(QStringLiteral("menu_uv_fit"));
  auto *uv_justify_menu =
      menu_bar.findChild<QMenu *>(QStringLiteral("menu_uv_justify"));
  auto *view_menu = menu_bar.findChild<QMenu *>(QStringLiteral("menu_view"));
  auto *window_menu =
      menu_bar.findChild<QMenu *>(QStringLiteral("menu_window"));
  auto *help_menu = menu_bar.findChild<QMenu *>(QStringLiteral("menu_help"));
  auto *edge_normals_menu =
      menu_bar.findChild<QMenu *>(QStringLiteral("menu_edge_normals"));
  ASSERT_NE(file_menu, nullptr);
  ASSERT_NE(edit_menu, nullptr);
  ASSERT_NE(select_menu, nullptr);
  ASSERT_NE(create_menu, nullptr);
  ASSERT_NE(object_menu, nullptr);
  ASSERT_NE(mesh_menu, nullptr);
  ASSERT_NE(vertex_menu, nullptr);
  ASSERT_NE(edge_menu, nullptr);
  ASSERT_NE(face_menu, nullptr);
  ASSERT_NE(uv_menu, nullptr);
  ASSERT_NE(uv_projection_menu, nullptr);
  ASSERT_NE(uv_align_menu, nullptr);
  ASSERT_NE(uv_flip_menu, nullptr);
  ASSERT_NE(uv_fit_menu, nullptr);
  ASSERT_NE(uv_justify_menu, nullptr);
  ASSERT_NE(view_menu, nullptr);
  ASSERT_NE(window_menu, nullptr);
  ASSERT_NE(help_menu, nullptr);
  ASSERT_NE(edge_normals_menu, nullptr);

  // Command-backed rows synchronize immediately before the menu is shown.
  // Exercise that retained-menu lifecycle before asserting user-visible labels.
  ASSERT_TRUE(QMetaObject::invokeMethod(uv_menu, "aboutToShow",
                                        Qt::DirectConnection));

  const QList<QAction *> top_level_actions = menu_bar.actions();
  const std::array<QMenu *, 13> expected_top_level{
      file_menu, edit_menu, view_menu, select_menu, create_menu, object_menu,
      mesh_menu, vertex_menu, edge_menu, face_menu, uv_menu,
      window_menu, help_menu,
  };
  ASSERT_EQ(top_level_actions.size(),
            static_cast<qsizetype>(expected_top_level.size()));
  for (qsizetype index = 0;
       index < static_cast<qsizetype>(expected_top_level.size()); ++index) {
    EXPECT_EQ(
        top_level_actions[index],
        expected_top_level[static_cast<std::size_t>(index)]->menuAction());
  }
  EXPECT_EQ(menu_bar.findChild<QMenu *>(QStringLiteral("menu_align")), nullptr);
  EXPECT_EQ(menu_bar.findChild<QMenu *>(QStringLiteral("menu_cleanup")),
            nullptr);
  EXPECT_EQ(menu_bar.findChild<QMenu *>(QStringLiteral("menu_tools")), nullptr);
  EXPECT_EQ(menu_bar.findChild<QMenu *>(QStringLiteral("menu_layout")),
            nullptr);
  EXPECT_EQ(menu_bar.findChild<QMenu *>(QStringLiteral("menu_face_texture")),
            nullptr);
  EXPECT_EQ(
      menu_bar.findChild<QMenu *>(QStringLiteral("menu_face_texture_align")),
      nullptr);
  EXPECT_EQ(
      menu_bar.findChild<QMenu *>(QStringLiteral("menu_face_texture_scale")),
      nullptr);
  EXPECT_EQ(
      menu_bar.findChild<QMenu *>(QStringLiteral("menu_face_texture_shift")),
      nullptr);
  EXPECT_EQ(
      menu_bar.findChild<QMenu *>(QStringLiteral("menu_face_texture_rotate")),
      nullptr);
  EXPECT_EQ(
      menu_bar.findChild<QMenu *>(QStringLiteral("menu_face_texture_flip")),
      nullptr);
  EXPECT_EQ(
      menu_bar.findChild<QMenu *>(QStringLiteral("menu_face_texture_fit")),
      nullptr);
  EXPECT_EQ(
      menu_bar.findChild<QMenu *>(QStringLiteral("menu_face_texture_justify")),
      nullptr);

  std::multiset<std::string> projected_main_menu_commands;
  for (QAction *action : menu_bar.findChildren<QAction *>()) {
    const QVariant command_id = action->property("quaderCommandId");
    if (command_id.isValid()) {
      projected_main_menu_commands.insert(command_id.toString().toStdString());
    }
  }
  std::size_t expected_main_menu_commands = 0u;
  for (const CommandPresentationDescriptor &presentation :
       command_presentation_catalog()) {
    if (!has_command_presentation_surface(
            presentation, CommandPresentationSurface::MainMenu)) {
      continue;
    }
    ++expected_main_menu_commands;
    EXPECT_EQ(projected_main_menu_commands.count(
                  std::string(presentation.command_id.string())),
              1u)
        << presentation.command_id.string();
  }
  EXPECT_EQ(projected_main_menu_commands.size(), expected_main_menu_commands);

  const QList<QAction *> uv_actions = uv_menu->actions();
  ASSERT_EQ(uv_actions.size(), 10);
  EXPECT_EQ(uv_actions[0]->objectName(),
            QStringLiteral("tool_uv_peel"));
  EXPECT_EQ(uv_actions[1], uv_projection_menu->menuAction());
  EXPECT_TRUE(uv_actions[2]->isSeparator());
  EXPECT_EQ(uv_actions[3], uv_align_menu->menuAction());
  EXPECT_EQ(uv_actions[4]->objectName(),
            QStringLiteral("command_modeling_texture_rotate_clockwise"));
  EXPECT_EQ(uv_actions[4]->text(), QStringLiteral("Rotate CW"));
  EXPECT_EQ(uv_actions[5]->objectName(),
            QStringLiteral(
                "command_modeling_texture_rotate_counter_clockwise"));
  EXPECT_EQ(uv_actions[5]->text(), QStringLiteral("Rotate CCW"));
  EXPECT_TRUE(uv_actions[6]->isSeparator());
  EXPECT_EQ(uv_actions[7], uv_flip_menu->menuAction());
  EXPECT_EQ(uv_actions[8], uv_fit_menu->menuAction());
  EXPECT_EQ(uv_actions[9], uv_justify_menu->menuAction());

  const auto expect_uv_submenu = [](QMenu &submenu, std::string_view title,
                                    std::initializer_list<std::string_view>
                                        command_object_names,
                                    std::initializer_list<std::string_view>
                                        labels) {
    EXPECT_EQ(submenu.title(),
              QString::fromUtf8(title.data(),
                                static_cast<qsizetype>(title.size())));
    const QList<QAction *> actions = submenu.actions();
    ASSERT_EQ(actions.size(),
              static_cast<qsizetype>(command_object_names.size()));
    ASSERT_EQ(command_object_names.size(), labels.size());
    auto command = command_object_names.begin();
    auto label = labels.begin();
    for (qsizetype index = 0; index < actions.size(); ++index) {
      EXPECT_EQ(actions[index]->menu(), nullptr);
      EXPECT_EQ(actions[index]->objectName(),
                QString::fromUtf8(command->data(),
                                  static_cast<qsizetype>(command->size())));
      EXPECT_EQ(actions[index]->text(),
                QString::fromUtf8(label->data(),
                                  static_cast<qsizetype>(label->size())));
      ++command;
      ++label;
    }
  };
  expect_uv_submenu(
      *uv_projection_menu, "Projection",
      {"command_modeling_texture_project_cube",
       "command_modeling_texture_project_cylinder",
       "command_modeling_texture_project_sphere"},
      {"Cube", "Cylinder", "Sphere"});
  expect_uv_submenu(
      *uv_align_menu, "Align",
      {"command_modeling_texture_align_to_grid",
       "command_modeling_texture_align_to_face",
       "command_modeling_texture_align_to_view"},
      {"To Grid", "To Face", "To View"});
  expect_uv_submenu(*uv_flip_menu, "Flip",
                    {"command_modeling_texture_flip_texture_horizontal",
                     "command_modeling_texture_flip_texture_vertical"},
                    {"Horizontal", "Vertical"});
  expect_uv_submenu(*uv_fit_menu, "Fit",
                    {"command_modeling_texture_fit_both",
                     "command_modeling_texture_fit_x",
                     "command_modeling_texture_fit_y"},
                    {"Both", "X", "Y"});
  expect_uv_submenu(*uv_justify_menu, "Justify",
                    {"command_modeling_texture_justify_left",
                     "command_modeling_texture_justify_right",
                     "command_modeling_texture_justify_top",
                     "command_modeling_texture_justify_bottom",
                     "command_modeling_texture_justify_center"},
                    {"Left", "Right", "Top", "Bottom", "Center"});

  for (const std::string_view removed_from_main_menu :
       {"command_modeling_texture_scale_x_up",
        "command_modeling_texture_scale_x_down",
        "command_modeling_texture_scale_y_up",
        "command_modeling_texture_scale_y_down",
        "command_modeling_texture_shift_left",
        "command_modeling_texture_shift_right",
        "command_modeling_texture_shift_up",
        "command_modeling_texture_shift_down",
        "command_modeling_texture_flip_face_horizontal",
        "command_modeling_texture_flip_face_vertical"}) {
    const QString object_name = QString::fromUtf8(
        removed_from_main_menu.data(),
        static_cast<qsizetype>(removed_from_main_menu.size()));
    EXPECT_EQ(menu_bar.findChild<QAction *>(object_name), nullptr)
        << removed_from_main_menu;
  }

  auto *connect_vertices = menu_bar.findChild<QAction *>(
      QStringLiteral("command_modeling_connect_vertices"));
  auto *bevel_vertices = menu_bar.findChild<QAction *>(
      QStringLiteral("command_modeling_bevel_vertices"));
  auto *dissolve_vertices = menu_bar.findChild<QAction *>(
      QStringLiteral("command_modeling_dissolve_vertices"));
  auto *insert_edge_loop = menu_bar.findChild<QAction *>(
      QStringLiteral("tool_insert_edge_loop"));
  auto *bevel_edges = menu_bar.findChild<QAction *>(
      QStringLiteral("command_modeling_bevel_edges"));
  auto *fill_hole = menu_bar.findChild<QAction *>(
      QStringLiteral("command_modeling_fill_hole"));
  auto *dissolve_edges = menu_bar.findChild<QAction *>(
      QStringLiteral("command_modeling_dissolve_edges"));
  auto *uv_peel = menu_bar.findChild<QAction *>(
      QStringLiteral("tool_uv_peel"));
  auto *project_texture_cube = menu_bar.findChild<QAction *>(
      QStringLiteral("command_modeling_texture_project_cube"));
  auto *extract_faces = menu_bar.findChild<QAction *>(
      QStringLiteral("command_modeling_extract_faces"));
  auto *thicken_faces = menu_bar.findChild<QAction *>(
      QStringLiteral("command_modeling_thicken_faces"));
  auto *invert_faces = menu_bar.findChild<QAction *>(
      QStringLiteral("command_modeling_invert_faces"));
  auto *align_texture_to_grid = menu_bar.findChild<QAction *>(
      QStringLiteral("command_modeling_texture_align_to_grid"));
  auto *duplicate_meshes = menu_bar.findChild<QAction *>(
      QStringLiteral("command_modeling_duplicate_meshes"));
  auto *combine_mesh = menu_bar.findChild<QAction *>(
      QStringLiteral("command_modeling_combine_mesh"));
  auto *merge_edges = menu_bar.findChild<QAction *>(
      QStringLiteral("command_modeling_merge_edges"));
  auto *default_edge_normals = menu_bar.findChild<QAction *>(
      QStringLiteral("command_modeling_default_edge_normals"));
  auto *harden_edge_normals = menu_bar.findChild<QAction *>(
      QStringLiteral("command_modeling_harden_edge_normals"));
  auto *soften_edge_normals = menu_bar.findChild<QAction *>(
      QStringLiteral("command_modeling_soften_edge_normals"));
  auto *mirror_selection_x = menu_bar.findChild<QAction *>(
      QStringLiteral("command_modeling_mirror_selection_x"));
  auto *recalculate_normals = menu_bar.findChild<QAction *>(
      QStringLiteral("command_modeling_recalculate_face_normals"));
  auto *shade_faces_smooth = menu_bar.findChild<QAction *>(
      QStringLiteral("command_modeling_shade_faces_smooth"));
  auto *invert_mesh = menu_bar.findChild<QAction *>(
      QStringLiteral("command_modeling_invert_mesh"));
  auto *create_outer_hull = menu_bar.findChild<QAction *>(
      QStringLiteral("command_modeling_create_outer_hull"));
  auto *half_cut = menu_bar.findChild<QAction *>(
      QStringLiteral("command_modeling_half_cut"));

  ASSERT_NE(duplicate_meshes, nullptr);
  ASSERT_NE(combine_mesh, nullptr);
  ASSERT_NE(merge_edges, nullptr);
  ASSERT_NE(default_edge_normals, nullptr);
  ASSERT_NE(harden_edge_normals, nullptr);
  ASSERT_NE(soften_edge_normals, nullptr);
  ASSERT_NE(mirror_selection_x, nullptr);
  ASSERT_NE(recalculate_normals, nullptr);
  ASSERT_NE(shade_faces_smooth, nullptr);
  ASSERT_NE(create_outer_hull, nullptr);
  ASSERT_NE(half_cut, nullptr);
  ASSERT_TRUE(QMetaObject::invokeMethod(mesh_menu, "aboutToShow",
                                        Qt::DirectConnection));
  EXPECT_TRUE(create_outer_hull->isVisible());
  EXPECT_FALSE(create_outer_hull->isEnabled());
  EXPECT_TRUE(half_cut->isVisible());
  EXPECT_TRUE(half_cut->isEnabled());
  EXPECT_LT(mesh_menu->actions().indexOf(duplicate_meshes),
            mesh_menu->actions().indexOf(combine_mesh));
  EXPECT_LT(edge_menu->actions().indexOf(merge_edges),
            edge_menu->actions().indexOf(edge_normals_menu->menuAction()));
  EXPECT_LT(edge_normals_menu->actions().indexOf(default_edge_normals),
            edge_normals_menu->actions().indexOf(harden_edge_normals));
  EXPECT_LT(edge_normals_menu->actions().indexOf(harden_edge_normals),
            edge_normals_menu->actions().indexOf(soften_edge_normals));

  const std::array<QMenu *, 5> component_menus{
      mesh_menu, vertex_menu, edge_menu, face_menu, uv_menu};
  const auto expect_owned_only_by =
      [&](QAction *action, QMenu *expected_owner) {
        ASSERT_NE(action, nullptr);
        for (QMenu *candidate : component_menus) {
          EXPECT_EQ(candidate->findChildren<QAction *>().contains(action),
                    candidate == expected_owner)
              << action->objectName().toStdString();
        }
      };
  for (QAction *action :
       {connect_vertices, bevel_vertices, dissolve_vertices}) {
    expect_owned_only_by(action, vertex_menu);
  }
  for (QAction *action :
       {insert_edge_loop, bevel_edges, fill_hole, dissolve_edges}) {
    expect_owned_only_by(action, edge_menu);
  }
  for (QAction *action : {extract_faces, thicken_faces, invert_faces}) {
    expect_owned_only_by(action, face_menu);
  }
  for (QAction *action :
       {uv_peel, project_texture_cube, align_texture_to_grid}) {
    expect_owned_only_by(action, uv_menu);
  }
  for (QAction *action : {invert_mesh, create_outer_hull, half_cut}) {
    expect_owned_only_by(action, mesh_menu);
  }

  const auto expect_cleanup_tail =
      [](QMenu &owner,
         std::initializer_list<std::string_view> cleanup_object_names) {
        const QList<QAction *> actions = owner.actions();
        ASSERT_GT(cleanup_object_names.size(), 0u);
        ASSERT_GT(actions.size(),
                  static_cast<qsizetype>(cleanup_object_names.size()));
        const qsizetype first_cleanup =
            actions.size() -
            static_cast<qsizetype>(cleanup_object_names.size());
        ASSERT_GT(first_cleanup, 0);
        EXPECT_TRUE(actions[first_cleanup - 1]->isSeparator());
        qsizetype action_index = first_cleanup;
        for (const std::string_view object_name : cleanup_object_names) {
          EXPECT_EQ(actions[action_index]->objectName(),
                    QString::fromUtf8(
                        object_name.data(),
                        static_cast<qsizetype>(object_name.size())));
          ++action_index;
        }
      };
  auto *object_cleanup = object_menu->findChild<QMenu *>(
      QStringLiteral("menu_object_cleanup"));
  ASSERT_NE(object_cleanup, nullptr);
  EXPECT_EQ(object_cleanup->title(), QStringLiteral("Clean Up"));
  const QList<QAction *> object_actions = object_menu->actions();
  ASSERT_GE(object_actions.size(), 2);
  EXPECT_EQ(object_actions.back(), object_cleanup->menuAction());
  EXPECT_TRUE(object_actions[object_actions.size() - 2]->isSeparator());
  ASSERT_EQ(object_cleanup->actions().size(), 1);
  EXPECT_EQ(object_cleanup->actions().front()->objectName(),
            QStringLiteral("command_modeling_remove_doubles"));
  EXPECT_EQ(mesh_menu->findChild<QAction *>(
                QStringLiteral("command_modeling_remove_doubles")), nullptr);
  expect_cleanup_tail(*vertex_menu,
                      {"command_modeling_dissolve_vertices"});
  expect_cleanup_tail(*edge_menu, {"command_modeling_dissolve_edges",
                                   "command_modeling_collapse_edges"});
  expect_cleanup_tail(*face_menu, {"command_modeling_collapse_faces"});

  struct ComponentActionExpectation final {
    QAction *action = nullptr;
    QuaderCommandId command_id;
    std::optional<QuaderSelectionMode> operand_mode;
  };
  const std::array<ComponentActionExpectation, 18> component_actions{{
      {connect_vertices, quader_command::kModelingConnectVertices,
       QuaderSelectionMode::Vertex},
      {bevel_vertices, quader_command::kModelingBevelVertices,
       QuaderSelectionMode::Vertex},
      {dissolve_vertices, quader_command::kModelingDissolveVertices,
       QuaderSelectionMode::Vertex},
      {insert_edge_loop, quader_command::kToolsActivateInsertEdgeLoop,
       std::nullopt},
      {bevel_edges, quader_command::kModelingBevelEdges,
       QuaderSelectionMode::Edge},
      {fill_hole, quader_command::kModelingFillHole,
       QuaderSelectionMode::Edge},
      {dissolve_edges, quader_command::kModelingDissolveEdges,
       QuaderSelectionMode::Edge},
      {default_edge_normals, quader_command::kModelingDefaultEdgeNormals,
       QuaderSelectionMode::Edge},
      {harden_edge_normals, quader_command::kModelingHardenEdgeNormals,
       QuaderSelectionMode::Edge},
      {soften_edge_normals, quader_command::kModelingSoftenEdgeNormals,
       QuaderSelectionMode::Edge},
      {uv_peel, quader_command::kModelingUvPeel, std::nullopt},
      {extract_faces, quader_command::kModelingExtractFaces,
       QuaderSelectionMode::Face},
      {thicken_faces, quader_command::kModelingThickenFaces,
       QuaderSelectionMode::Face},
      {invert_faces, quader_command::kModelingInvertFaces,
       QuaderSelectionMode::Face},
      {align_texture_to_grid, quader_command::kModelingTextureAlignToGrid,
       QuaderSelectionMode::Face},
      {invert_mesh, quader_command::kModelingInvertMesh, std::nullopt},
      {create_outer_hull, quader_command::kModelingCreateOuterHull,
       std::nullopt},
      {half_cut, quader_command::kModelingHalfCut, std::nullopt},
  }};
  for (const QuaderSelectionMode mode :
       {QuaderSelectionMode::Object, QuaderSelectionMode::Vertex,
        QuaderSelectionMode::Edge, QuaderSelectionMode::Face,
        QuaderSelectionMode::Group}) {
    ASSERT_TRUE(editor.selection().set_mode(mode).ok());
    for (QMenu *component_menu : component_menus) {
      ASSERT_TRUE(QMetaObject::invokeMethod(component_menu, "aboutToShow",
                                            Qt::DirectConnection));
      EXPECT_TRUE(component_menu->menuAction()->isVisible());
    }
    for (const auto &[action, command_id, operand_mode] : component_actions) {
      ASSERT_NE(action, nullptr);
      EXPECT_TRUE(action->isVisible()) << command_id.string();
      const std::optional<QuaderCommandDescriptor> descriptor =
          editor.commands().descriptor(command_id);
      ASSERT_TRUE(descriptor.has_value()) << command_id.string();
      const bool presentation_domain_matches =
          !operand_mode.has_value() || *operand_mode == mode;
      EXPECT_EQ(action->isEnabled(),
                descriptor->enabled && presentation_domain_matches)
          << command_id.string();
    }
    std::size_t projected_uv_commands = 0u;
    for (QAction *action : uv_menu->findChildren<QAction *>()) {
      const QVariant command_property = action->property("quaderCommandId");
      if (!command_property.isValid()) {
        continue;
      }
      ++projected_uv_commands;
      const std::string command_id =
          command_property.toString().toStdString();
      const std::optional<QuaderCommandDescriptor> descriptor =
          editor.commands().descriptor(
              QuaderCommandId{std::string_view{command_id}});
      ASSERT_TRUE(descriptor.has_value()) << command_id;
      const bool is_projection =
          uv_projection_menu->actions().contains(action);
      const bool presentation_domain_matches =
          action == uv_peel
              ? true
              : is_projection
                    ? mode == QuaderSelectionMode::Object ||
                          mode == QuaderSelectionMode::Face
                    : mode == QuaderSelectionMode::Face;
      EXPECT_EQ(action->isEnabled(),
                descriptor->enabled && presentation_domain_matches)
          << command_id;
    }
    EXPECT_EQ(projected_uv_commands, 19u);
  }

  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Object).ok());
  ASSERT_TRUE(editor.selection().select_object(box.value()).ok());
  ASSERT_TRUE(QMetaObject::invokeMethod(vertex_menu, "aboutToShow",
                                        Qt::DirectConnection));
  ASSERT_TRUE(QMetaObject::invokeMethod(edge_menu, "aboutToShow",
                                        Qt::DirectConnection));
  ASSERT_TRUE(QMetaObject::invokeMethod(face_menu, "aboutToShow",
                                        Qt::DirectConnection));
  ASSERT_TRUE(QMetaObject::invokeMethod(uv_menu, "aboutToShow",
                                        Qt::DirectConnection));
  EXPECT_FALSE(connect_vertices->isEnabled());
  EXPECT_FALSE(bevel_edges->isEnabled());
  EXPECT_FALSE(default_edge_normals->isEnabled());
  EXPECT_FALSE(harden_edge_normals->isEnabled());
  EXPECT_FALSE(soften_edge_normals->isEnabled());
  EXPECT_FALSE(invert_faces->isEnabled());
  EXPECT_TRUE(uv_peel->isEnabled());
  EXPECT_TRUE(project_texture_cube->isEnabled());
  EXPECT_FALSE(align_texture_to_grid->isEnabled());
  const std::optional<QuaderCommandDescriptor> shared_face_shading =
      editor.commands().descriptor(quader_command::kModelingShadeFacesSmooth);
  ASSERT_TRUE(shared_face_shading.has_value());
  EXPECT_TRUE(shared_face_shading->enabled);
  EXPECT_FALSE(shade_faces_smooth->isEnabled());
  EXPECT_EQ(uv_peel->text(), QStringLiteral("UV Peel"));
  EXPECT_EQ(align_texture_to_grid->text(), QStringLiteral("To Grid"));

  ASSERT_TRUE(insert_edge_loop->isEnabled());
  insert_edge_loop->trigger();
  EXPECT_EQ(editor.selection().summary().mode, QuaderSelectionMode::Edge);
  EXPECT_EQ(editor.tools().active_tool(), "insert_edge_loop");
  EXPECT_EQ(editor.viewports().selected_tool(), ViewportToolId::InsertEdgeLoop);
  ASSERT_TRUE(editor.tools().activate(ViewportToolId::Select).ok());

  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Vertex).ok());
  ASSERT_TRUE(editor.selection().select_vertex(box.value(), 1).ok());
  ASSERT_TRUE(editor.selection().select_all().ok());
  ASSERT_GT(editor.selection().summary().selected_vertex_count, 0u);
  ASSERT_TRUE(QMetaObject::invokeMethod(vertex_menu, "aboutToShow",
                                        Qt::DirectConnection));
  EXPECT_TRUE(bevel_vertices->isEnabled());

  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Edge).ok());
  ASSERT_TRUE(editor.selection().clear().ok());
  ASSERT_EQ(editor.selection().summary().selected_edge_count, 0u);
  ASSERT_TRUE(QMetaObject::invokeMethod(edge_menu, "aboutToShow",
                                        Qt::DirectConnection));
  EXPECT_FALSE(default_edge_normals->isEnabled());
  EXPECT_FALSE(harden_edge_normals->isEnabled());
  EXPECT_FALSE(soften_edge_normals->isEnabled());

  ASSERT_TRUE(editor.selection().select_edge(box.value(), 1, 2).ok());
  ASSERT_TRUE(editor.selection().select_all().ok());
  ASSERT_GT(editor.selection().summary().selected_edge_count, 0u);
  ASSERT_TRUE(QMetaObject::invokeMethod(edge_menu, "aboutToShow",
                                        Qt::DirectConnection));
  ASSERT_TRUE(QMetaObject::invokeMethod(uv_menu, "aboutToShow",
                                        Qt::DirectConnection));
  EXPECT_TRUE(bevel_edges->isEnabled());
  EXPECT_TRUE(default_edge_normals->isEnabled());
  EXPECT_TRUE(harden_edge_normals->isEnabled());
  EXPECT_TRUE(soften_edge_normals->isEnabled());
  EXPECT_TRUE(uv_peel->isEnabled());
  EXPECT_FALSE(project_texture_cube->isEnabled());
  EXPECT_FALSE(align_texture_to_grid->isEnabled());

  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection().select_faces(box.value(), {1}).ok());
  ASSERT_TRUE(QMetaObject::invokeMethod(face_menu, "aboutToShow",
                                        Qt::DirectConnection));
  ASSERT_TRUE(QMetaObject::invokeMethod(uv_menu, "aboutToShow",
                                        Qt::DirectConnection));
  EXPECT_TRUE(shade_faces_smooth->isEnabled());
  EXPECT_TRUE(uv_peel->isEnabled());
  EXPECT_TRUE(project_texture_cube->isEnabled());
  EXPECT_TRUE(align_texture_to_grid->isEnabled());

  auto *select_all =
      menu_bar.findChild<QAction *>(QStringLiteral("command_select_all"));
  auto *select_inverse =
      menu_bar.findChild<QAction *>(QStringLiteral("command_select_inverse"));
  auto *toggle_backface_selection = menu_bar.findChild<QAction *>(
      QStringLiteral("command_select_toggle_backface_selection"));
  auto *select_direction = menu_bar.findChild<QAction *>(
      QStringLiteral("command_select_edge_direction"));
  auto *select_loops = menu_bar.findChild<QAction *>(
      QStringLiteral("command_select_edge_loops"));
  auto *select_ring =
      menu_bar.findChild<QAction *>(QStringLiteral("command_select_edge_ring"));
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
  auto *grow =
      menu_bar.findChild<QAction *>(QStringLiteral("command_select_grow"));
  auto *shrink =
      menu_bar.findChild<QAction *>(QStringLiteral("command_select_shrink"));
  auto *clear_selection = menu_bar.findChild<QAction *>(
      QStringLiteral("command_edit_clear_selection"));

  EXPECT_EQ(toggle_backface_selection, nullptr);

  const std::array<QAction *, 13> select_actions{
      clear_selection,
      select_all,
      select_inverse,
      select_direction,
      select_loops,
      select_ring,
      select_same_material,
      select_coplanar,
      select_normal,
      select_ngons,
      select_similar,
      grow,
      shrink,
  };
  for (QAction *action : select_actions) {
    ASSERT_NE(action, nullptr);
    EXPECT_TRUE(select_menu->actions().contains(action));
    EXPECT_FALSE(edit_menu->actions().contains(action));
  }

  const std::array<QAction *, 10> mode_specific_actions{
      select_direction,
      select_loops,
      select_ring,
      select_same_material,
      select_coplanar,
      select_normal,
      select_ngons,
      select_similar,
      grow,
      shrink,
  };
  const auto expect_visible_for_mode = [&](QuaderSelectionMode mode) {
    ASSERT_TRUE(editor.selection().set_mode(mode).ok());
    ASSERT_TRUE(QMetaObject::invokeMethod(select_menu, "aboutToShow",
                                          Qt::DirectConnection));
    EXPECT_TRUE(select_all->isVisible());
    EXPECT_TRUE(select_inverse->isVisible());
    for (QAction *action : mode_specific_actions) {
      EXPECT_TRUE(action->isVisible());
    }
  };

  expect_visible_for_mode(QuaderSelectionMode::Object);
  expect_visible_for_mode(QuaderSelectionMode::Edge);
  expect_visible_for_mode(QuaderSelectionMode::Face);
  expect_visible_for_mode(QuaderSelectionMode::Vertex);
  expect_visible_for_mode(QuaderSelectionMode::Group);

  EXPECT_EQ(editor.commands().shortcut_for(
                quader_command::kSelectToggleBackfaceSelection),
            "Ctrl+Shift+F9");
  ASSERT_TRUE(editor.commands()
                  .execute(quader_command::kSelectToggleBackfaceSelection)
                  .ok());
  EXPECT_TRUE(
      viewport_settings.bool_value(ViewportBoolSettingId::BackfaceSelection));

  ASSERT_TRUE(localization.select_locale("it-IT"));
  ASSERT_TRUE(QMetaObject::invokeMethod(mesh_menu, "aboutToShow",
                                        Qt::DirectConnection));
  ASSERT_TRUE(QMetaObject::invokeMethod(face_menu, "aboutToShow",
                                        Qt::DirectConnection));
  ASSERT_TRUE(QMetaObject::invokeMethod(uv_menu, "aboutToShow",
                                        Qt::DirectConnection));
  EXPECT_TRUE(mirror_selection_x->text().startsWith(
      QStringLiteral("Specchia lungo X")));
  EXPECT_TRUE(recalculate_normals->text().startsWith(
      QStringLiteral("Ricalcola normali")));
  EXPECT_TRUE(shade_faces_smooth->text().startsWith(
      QStringLiteral("Ombreggia morbido")));
  menu.rebuild();
  EXPECT_EQ(menu_bar.findChild<QMenu *>(QStringLiteral("menu_mesh"))->title(),
            QStringLiteral("Me&sh"));
  EXPECT_EQ(menu_bar.findChild<QMenu *>(QStringLiteral("menu_vertex"))->title(),
            QStringLiteral("Ver&tice"));
  EXPECT_EQ(menu_bar.findChild<QMenu *>(QStringLiteral("menu_edge"))->title(),
            QStringLiteral("Spi&golo"));
  EXPECT_EQ(menu_bar.findChild<QMenu *>(QStringLiteral("menu_face"))->title(),
            QStringLiteral("Facc&ia"));
  auto *localized_uv_menu =
      menu_bar.findChild<QMenu *>(QStringLiteral("menu_uv"));
  ASSERT_NE(localized_uv_menu, nullptr);
  EXPECT_EQ(localized_uv_menu->title(), QStringLiteral("&UV"));
  auto *localized_uv_align =
      menu_bar.findChild<QMenu *>(QStringLiteral("menu_uv_align"));
  auto *localized_uv_flip =
      menu_bar.findChild<QMenu *>(QStringLiteral("menu_uv_flip"));
  auto *localized_uv_fit =
      menu_bar.findChild<QMenu *>(QStringLiteral("menu_uv_fit"));
  auto *localized_uv_justify =
      menu_bar.findChild<QMenu *>(QStringLiteral("menu_uv_justify"));
  ASSERT_NE(localized_uv_align, nullptr);
  ASSERT_NE(localized_uv_flip, nullptr);
  ASSERT_NE(localized_uv_fit, nullptr);
  ASSERT_NE(localized_uv_justify, nullptr);
  ASSERT_TRUE(QMetaObject::invokeMethod(localized_uv_menu, "aboutToShow",
                                        Qt::DirectConnection));
  EXPECT_EQ(localized_uv_align->title(), QStringLiteral("Allinea"));
  EXPECT_EQ(localized_uv_flip->title(), QStringLiteral("Ribalta"));
  EXPECT_EQ(localized_uv_fit->title(), QStringLiteral("Adatta"));
  EXPECT_EQ(localized_uv_justify->title(), QStringLiteral("Giustifica"));
  EXPECT_EQ(localized_uv_align->actions()[0]->text(),
            QStringLiteral("Alla griglia"));
  EXPECT_EQ(localized_uv_flip->actions()[0]->text(),
            QStringLiteral("Orizzontale"));
  EXPECT_EQ(localized_uv_fit->actions()[0]->text(),
            QStringLiteral("Entrambi gli assi"));
  EXPECT_EQ(localized_uv_justify->actions()[0]->text(),
            QStringLiteral("Sinistra"));
  auto *localized_rotate_cw = menu_bar.findChild<QAction *>(
      QStringLiteral("command_modeling_texture_rotate_clockwise"));
  auto *localized_rotate_ccw = menu_bar.findChild<QAction *>(
      QStringLiteral("command_modeling_texture_rotate_counter_clockwise"));
  ASSERT_NE(localized_rotate_cw, nullptr);
  ASSERT_NE(localized_rotate_ccw, nullptr);
  EXPECT_EQ(localized_rotate_cw->text(),
            QStringLiteral("Ruota in senso orario"));
  EXPECT_EQ(localized_rotate_ccw->text(),
            QStringLiteral("Ruota in senso antiorario"));
  EXPECT_EQ(
      menu_bar.findChild<QMenu *>(QStringLiteral("menu_face_texture_align")),
      nullptr);
  EXPECT_EQ(
      menu_bar.findChild<QMenu *>(QStringLiteral("menu_face_texture_justify")),
      nullptr);
}

TEST(QtMainMenuTests,
     AlignToolLauncherRemainsModeScopedAndUsesAuthoredIcon) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-align-menu-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  const QuaderApiResult<QuaderModelingObjectRef> first =
      editor.modeling().create_box(
          QuaderBounds{.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
          "Align Menu First");
  const QuaderApiResult<QuaderModelingObjectRef> second =
      editor.modeling().create_box(
          QuaderBounds{.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
          "Align Menu Second");
  ASSERT_TRUE(first.ok()) << first.error_message();
  ASSERT_TRUE(second.ok()) << second.error_message();
  ASSERT_TRUE(editor.selection().select_object(first.value()).ok());
  const auto first_group = editor.scene().group_selection("Align Menu First Group");
  ASSERT_TRUE(first_group.ok()) << first_group.error_message();
  ASSERT_TRUE(editor.selection().select_object(second.value()).ok());
  const auto second_group = editor.scene().group_selection("Align Menu Second Group");
  ASSERT_TRUE(second_group.ok()) << second_group.error_message();
  ASSERT_TRUE(editor.selection()
                  .select_objects({first_group.value().id, second_group.value().id},
                                  first_group.value().id)
                  .ok());

  ViewportSettings viewport_settings = ViewportSettings::defaults();
  LocalizationService localization = loaded_localization();
  QMenuBar menu_bar;
  QtMainMenu menu;
  menu.initialize(menu_bar, QtMainMenuContext{
                                .editor = editor,
                                .modeling = session.modeling(),
                                .viewport_settings = viewport_settings,
                                .localization = localization,
                            });

  auto *edit_menu = menu_bar.findChild<QMenu *>(QStringLiteral("menu_edit"));
  auto *object_menu =
      menu_bar.findChild<QMenu *>(QStringLiteral("menu_object"));
  auto *align = object_menu != nullptr
      ? object_menu->findChild<QAction *>(QStringLiteral("tool_align"))
      : nullptr;
  auto *grid_snap =
      menu_bar.findChild<QAction *>(QStringLiteral("action_grid_snap"));
  ASSERT_NE(edit_menu, nullptr);
  ASSERT_NE(object_menu, nullptr);
  ASSERT_NE(align, nullptr);
  EXPECT_EQ(object_menu->findChildren<QAction *>(QStringLiteral("tool_align")).size(), 1);
  EXPECT_EQ(align->property("quaderCommandId").toString(),
            QStringLiteral("tools.activate.align"));
  EXPECT_FALSE(align->icon().isNull());
  EXPECT_FALSE(align->icon().pixmap(QSize(32, 32), 1.0).isNull());
  EXPECT_EQ(align->menu(), nullptr);
  for (const auto *name : {
           "menu_object_arrange", "menu_object_snapping", "menu_object_align",
           "menu_object_align_x", "menu_object_align_y", "menu_object_align_z",
           "menu_object_distribute_centers", "menu_object_distribute_gaps"}) {
    EXPECT_EQ(menu_bar.findChild<QMenu *>(QString::fromUtf8(name)), nullptr);
  }
  for (const auto *action : object_menu->findChildren<QAction *>()) {
    EXPECT_FALSE(action->objectName().startsWith(QStringLiteral("command_modeling_align_")));
    EXPECT_FALSE(action->objectName().startsWith(QStringLiteral("command_modeling_distribute_")));
  }
  EXPECT_EQ(grid_snap, nullptr);
  EXPECT_TRUE(object_menu->actions().contains(align));
  EXPECT_EQ(menu_bar.findChild<QAction *>(
                QStringLiteral("action_align_snap_to_grid")),
            nullptr);

  for (const QuaderSelectionMode mode :
       {QuaderSelectionMode::Object, QuaderSelectionMode::Edge,
        QuaderSelectionMode::Face, QuaderSelectionMode::Vertex,
        QuaderSelectionMode::Group}) {
    ASSERT_TRUE(editor.selection().set_mode(mode).ok());
    const bool arrangement_mode = mode == QuaderSelectionMode::Object ||
                                  mode == QuaderSelectionMode::Group;
    if (arrangement_mode) {
      ASSERT_TRUE(editor.selection().select_objects(
          {first_group.value().id, second_group.value().id},
          first_group.value().id).ok());
    }

    // Reproduce the regression through the same path as opening an unrelated
    // top-level menu after changing selection mode.
    ASSERT_TRUE(QMetaObject::invokeMethod(edit_menu, "aboutToShow",
                                          Qt::DirectConnection));
    EXPECT_TRUE(object_menu->menuAction()->isVisible());

    ASSERT_TRUE(QMetaObject::invokeMethod(object_menu, "aboutToShow",
                                          Qt::DirectConnection));
    EXPECT_EQ(align->isEnabled(), arrangement_mode);
  }

  ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Object).ok());
  ASSERT_TRUE(editor.selection().select_objects(
      {first_group.value().id, second_group.value().id}, first_group.value().id).ok());
  ASSERT_TRUE(QMetaObject::invokeMethod(object_menu, "aboutToShow",
                                        Qt::DirectConnection));
  EXPECT_EQ(align->text(), QStringLiteral("Align Tool"));
  ASSERT_TRUE(localization.select_locale("it-IT"));
  ASSERT_TRUE(QMetaObject::invokeMethod(object_menu, "aboutToShow",
                                        Qt::DirectConnection));
  EXPECT_EQ(align->text(), QStringLiteral("Strumento Allinea"));
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
          QuaderBounds{.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
          "Menu Pivot Box");
  ASSERT_TRUE(box.ok()) << box.error_message();
  const QuaderApiResult<QuaderModelingObjectRef> baseline_box =
      editor.modeling().create_box(
          QuaderBounds{.min = {3.0f, -1.0f, -1.0f}, .max = {5.0f, 1.0f, 1.0f}},
          "Menu Local Visibility Baseline");
  ASSERT_TRUE(baseline_box.ok()) << baseline_box.error_message();
  ASSERT_TRUE(editor.selection().select_object(box.value()).ok());

  ViewportSettings viewport_settings = ViewportSettings::defaults();
  LocalizationService localization = loaded_localization();
  QMenuBar menu_bar;
  QtMainMenu menu;
  int asset_browser_toggles = 0;
  UISettings panel_settings = UISettings::defaults();
  panel_settings.set_panel_visible(UIPanelId::AssetBrowser, false);
  menu.initialize(
      menu_bar,
      QtMainMenuContext{
          .editor = editor,
          .modeling = session.modeling(),
          .viewport_settings = viewport_settings,
          .localization = localization,
          .panel_visible =
              [&panel_settings](UIPanelId id) {
                return panel_settings.is_panel_visible(id);
              },
          .set_panel_visible =
              [&panel_settings, &asset_browser_toggles](UIPanelId id, bool visible) {
                panel_settings.set_panel_visible(id, visible);
                if (id == UIPanelId::AssetBrowser) {
                  ++asset_browser_toggles;
                }
              },
      });
  // Menu descriptors are intentionally synchronized on demand; initialization
  // only builds the retained hierarchy and global chrome.
  menu.refresh();
  auto *pivot_action =
      menu_bar.findChild<QAction *>(QStringLiteral("tool_pivot"));
  EXPECT_EQ(pivot_action, nullptr);
  ASSERT_TRUE(editor.commands()
                  .descriptor(quader_command::kToolsActivatePivot)
                  .has_value());
  // P belongs to Parent to Active; Pivot remains available without a default.
  EXPECT_TRUE(editor.commands()
                  .shortcut_for(quader_command::kToolsActivatePivot)
                  .empty());
  auto *two_vertical =
      menu_bar.findChild<QAction *>(QStringLiteral("layout_two_vertical"));
  EXPECT_EQ(two_vertical, nullptr);
  EXPECT_EQ(menu_bar.findChild<QMenu *>(QStringLiteral("menu_window_layout")),
            nullptr);
  EXPECT_EQ(editor.commands().shortcut_for(
                quader_command::kViewportLayoutTwoVertical), "Ctrl+2");
  auto *local_visibility_action = menu_bar.findChild<QAction *>(
      QStringLiteral("command_viewport_toggle_local_visibility"));
  ASSERT_NE(local_visibility_action, nullptr);
  auto *view_root = menu_bar.findChild<QMenu *>(QStringLiteral("menu_view"));
  ASSERT_NE(view_root, nullptr);
  ASSERT_GE(view_root->actions().size(), 2);
  EXPECT_EQ(view_root->actions().front(), local_visibility_action);
  EXPECT_TRUE(view_root->actions()[1]->isSeparator());
  EXPECT_TRUE(local_visibility_action->isCheckable());
  EXPECT_FALSE(local_visibility_action->isChecked());
  EXPECT_TRUE(local_visibility_action->isEnabled());
  EXPECT_TRUE(local_visibility_action->shortcut().isEmpty());
  EXPECT_EQ(local_visibility_action->text(),
            QStringLiteral("Toggle Local View\tBackslash"));
  auto *asset_browser_action =
      menu_bar.findChild<QAction *>(QStringLiteral("panel_asset_browser"));
  auto *panels_menu =
      menu_bar.findChild<QMenu *>(QStringLiteral("menu_window_panels"));
  ASSERT_NE(asset_browser_action, nullptr);
  ASSERT_NE(panels_menu, nullptr);
  EXPECT_TRUE(panels_menu->actions().contains(asset_browser_action));
  EXPECT_TRUE(asset_browser_action->isCheckable());
  EXPECT_FALSE(asset_browser_action->isChecked());
  EXPECT_TRUE(asset_browser_action->shortcut().isEmpty());
  EXPECT_EQ(asset_browser_action->text(),
            QStringLiteral("Asset Browser\tCtrl+Space"));
  asset_browser_action->trigger();
  EXPECT_EQ(asset_browser_toggles, 1);
  EXPECT_TRUE(panel_settings.is_panel_visible(UIPanelId::AssetBrowser));
  asset_browser_action->trigger();
  EXPECT_EQ(asset_browser_toggles, 2);
  EXPECT_FALSE(panel_settings.is_panel_visible(UIPanelId::AssetBrowser));
  menu.refresh();
  EXPECT_FALSE(asset_browser_action->isChecked());
  auto *face_texture_action =
      menu_bar.findChild<QAction *>(QStringLiteral("panel_face_texture"));
  ASSERT_NE(face_texture_action, nullptr);
  EXPECT_EQ(face_texture_action->text(), QStringLiteral("Face Texture"));
  EXPECT_TRUE(face_texture_action->isChecked());
  EXPECT_TRUE(face_texture_action->shortcut().isEmpty());
  face_texture_action->trigger();
  EXPECT_FALSE(panel_settings.is_panel_visible(UIPanelId::FaceTexture));
  face_texture_action->trigger();
  EXPECT_TRUE(panel_settings.is_panel_visible(UIPanelId::FaceTexture));
  menu.refresh();
  EXPECT_TRUE(face_texture_action->isChecked());
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

  ASSERT_TRUE(
      editor.preferences()
          .set_shortcut(quader_command::kToolsActivatePivot, "Ctrl+Alt+P")
          .ok());
  ASSERT_TRUE(editor.preferences()
                  .set_shortcut(quader_command::kViewportLayoutTwoVertical,
                                "Ctrl+Alt+2")
                  .ok());
  ASSERT_TRUE(editor.preferences()
                  .set_shortcut(quader_command::kUiAssetBrowser,
                                "Ctrl+Alt+Space")
                  .ok());
  menu.refresh();
  EXPECT_EQ(editor.commands().shortcut_for(
                quader_command::kToolsActivatePivot), "Ctrl+Alt+P");
  EXPECT_EQ(editor.commands().shortcut_for(
                quader_command::kViewportLayoutTwoVertical), "Ctrl+Alt+2");
  EXPECT_EQ(asset_browser_action->text(),
            QStringLiteral("Asset Browser\tCtrl+Alt+Space"));
  EXPECT_TRUE(asset_browser_action->shortcut().isEmpty());
  EXPECT_FALSE(editor.commands().last_status().has_value());

  ASSERT_TRUE(editor.commands()
                  .execute(quader_command::kToolsActivatePivot)
                  .ok());
  ASSERT_TRUE(editor.commands().last_status().has_value());
  EXPECT_TRUE(editor.commands().last_status()->ok());
  EXPECT_EQ(editor.tools().active_tool(), "pivot");

  ASSERT_TRUE(editor.commands()
                  .execute(quader_command::kViewportLayoutTwoVertical)
                  .ok());
  EXPECT_EQ(editor.viewports().layout(), ViewportLayoutId::TwoVertical);

  ASSERT_TRUE(editor.tools().activate("select").ok());
  local_visibility_action->trigger();
  EXPECT_TRUE(editor.viewports().local_visibility_active());
  EXPECT_TRUE(local_visibility_action->isChecked());
  local_visibility_action->trigger();
  EXPECT_FALSE(editor.viewports().local_visibility_active());
  EXPECT_FALSE(local_visibility_action->isChecked());

  ASSERT_TRUE(editor.commands()
                  .execute(quader_command::kViewportToggleLocalVisibility)
                  .ok());
  EXPECT_TRUE(editor.viewports().local_visibility_active());
  EXPECT_FALSE(local_visibility_action->isChecked());
  auto *view_menu = menu_bar.findChild<QMenu *>(QStringLiteral("menu_view"));
  ASSERT_NE(view_menu, nullptr);
  ASSERT_TRUE(QMetaObject::invokeMethod(view_menu, "aboutToShow",
                                        Qt::DirectConnection));
  EXPECT_TRUE(local_visibility_action->isChecked());
  local_visibility_action->trigger();
  EXPECT_FALSE(editor.viewports().local_visibility_active());
  EXPECT_FALSE(local_visibility_action->isChecked());

  std::filesystem::remove(keybindings);
}

TEST(QtMainMenuTests,
     EditFindReplaceIsPresentationOnlyAndTracksAvailabilityAndLocale) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-main-menu-find-replace-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  SceneSession session;
  QuaderEditorApi editor(
      QuaderApiSessionInternalAccess::from_scene_session(session));
  ViewportSettings viewport_settings = ViewportSettings::defaults();
  LocalizationService localization = loaded_localization();
  QMenuBar menu_bar;
  QtMainMenu menu;
  bool available = false;
  int presentation_requests = 0;
  menu.initialize(
      menu_bar, QtMainMenuContext{
                    .editor = editor,
                    .modeling = session.modeling(),
                    .viewport_settings = viewport_settings,
                    .localization = localization,
                    .show_material_find_replace =
                        [&presentation_requests]() { ++presentation_requests; },
                    .material_find_replace_available =
                        [&available]() { return available; },
                });

  auto *object_menu =
      menu_bar.findChild<QMenu *>(QStringLiteral("menu_object"));
  auto *edit_menu = menu_bar.findChild<QMenu *>(QStringLiteral("menu_edit"));
  auto *materials_menu =
      menu_bar.findChild<QMenu *>(QStringLiteral("menu_object_materials"));
  auto *find_replace = menu_bar.findChild<QAction *>(
      QStringLiteral("action_material_find_replace"));
  ASSERT_NE(object_menu, nullptr);
  ASSERT_NE(edit_menu, nullptr);
  EXPECT_EQ(materials_menu, nullptr);
  ASSERT_NE(find_replace, nullptr);
  EXPECT_TRUE(edit_menu->actions().contains(find_replace));
  EXPECT_FALSE(object_menu->actions().contains(find_replace));
  EXPECT_TRUE(find_replace->shortcut().isEmpty());
  EXPECT_FALSE(find_replace->isEnabled());
  find_replace->trigger();
  EXPECT_EQ(presentation_requests, 0);
  EXPECT_FALSE(editor.commands().last_status().has_value());

  available = true;
  ASSERT_TRUE(QMetaObject::invokeMethod(edit_menu, "aboutToShow",
                                        Qt::DirectConnection));
  EXPECT_TRUE(find_replace->isEnabled());
  EXPECT_EQ(find_replace->text(),
            QStringLiteral("Find and Replace Materials"));
  find_replace->trigger();
  EXPECT_EQ(presentation_requests, 1);
  EXPECT_FALSE(editor.commands().last_status().has_value());

  ASSERT_TRUE(localization.select_locale("it-IT"));
  ASSERT_TRUE(QMetaObject::invokeMethod(edit_menu, "aboutToShow",
                                        Qt::DirectConnection));
  EXPECT_EQ(find_replace->text(),
            QStringLiteral("Trova e sostituisci materiali"));

  available = false;
  ASSERT_TRUE(QMetaObject::invokeMethod(edit_menu, "aboutToShow",
                                        Qt::DirectConnection));
  EXPECT_FALSE(find_replace->isEnabled());
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

TEST(QtMainMenuTests,
     SceneCloseDispatchAndProjectMenuRebindWithoutDestroyingRetainedActions) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-main-menu-rebind-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const std::filesystem::path project_root =
      std::filesystem::path(temporary.path().toStdWString()) / "Project";
  std::error_code directory_error;
  ASSERT_TRUE(std::filesystem::create_directory(project_root, directory_error))
      << directory_error.message();

  ProjectSession project;
  const ProjectSessionResult created = project.create(project_root);
  ASSERT_TRUE(created.ok) << created.message;
  const ProjectSessionResult second = project.new_scene();
  ASSERT_TRUE(second.ok) << second.message;
  ASSERT_EQ(project.open_scenes().size(), 2u);
  ASSERT_NE(project.active_scene(), nullptr);

  QuaderEditorApi editor(project);
  ViewportSettings viewport_settings = ViewportSettings::defaults();
  LocalizationService localization = loaded_localization();
  QMenuBar menu_bar;
  QtMainMenu menu;
  menu.initialize(menu_bar,
                  QtMainMenuContext{
                      .editor = editor,
                      .modeling = project.active_scene()->session->modeling(),
                      .viewport_settings = viewport_settings,
                      .localization = localization,
                  });

  QPointer<QMenu> file_menu =
      menu_bar.findChild<QMenu *>(QStringLiteral("menu_file"));
  QPointer<QAction> close_project =
      menu_bar.findChild<QAction *>(QStringLiteral("command_project_close"));
  ASSERT_FALSE(file_menu.isNull());
  EXPECT_EQ(menu_bar.findChild<QAction *>(
                QStringLiteral("command_scene_document_close")),
            nullptr);
  ASSERT_FALSE(close_project.isNull());

  int synchronous_rebinds = 0;
  QuaderEditorApiSessionAccess::set_scene_binding_changed_callback(
      editor, [&](SceneSession &replacement) {
        ++synchronous_rebinds;
        menu.rebind_runtime(editor, replacement.modeling());
        EXPECT_FALSE(file_menu.isNull());
        EXPECT_FALSE(close_project.isNull());
      });

  QMenu *const retained_file_menu = file_menu.data();
  QAction *const retained_project_action = close_project.data();
  const auto close_scene_result =
      editor.commands().execute(quader_command::kSceneDocumentClose);
  ASSERT_TRUE(close_scene_result.ok()) << close_scene_result.error_message();
  EXPECT_EQ(synchronous_rebinds, 1);
  EXPECT_EQ(project.open_scenes().size(), 1u);
  EXPECT_EQ(file_menu.data(), retained_file_menu);
  EXPECT_EQ(close_project.data(), retained_project_action);

  close_project->trigger();
  EXPECT_EQ(synchronous_rebinds, 2);
  EXPECT_FALSE(project.has_project());
  EXPECT_EQ(file_menu.data(), retained_file_menu);
  EXPECT_EQ(close_project.data(), retained_project_action);
  EXPECT_EQ(menu_bar.findChild<QAction *>(
                QStringLiteral("command_scene_document_close")),
            nullptr);
  menu.refresh();
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
  const Qt::WindowFlags window_flags = main_window.windowFlags();
  EXPECT_TRUE(window_flags.testFlag(Qt::WindowTitleHint));
  EXPECT_TRUE(window_flags.testFlag(Qt::WindowSystemMenuHint));
  EXPECT_TRUE(window_flags.testFlag(Qt::WindowMinimizeButtonHint));
  EXPECT_TRUE(window_flags.testFlag(Qt::WindowMaximizeButtonHint));
  EXPECT_TRUE(window_flags.testFlag(Qt::WindowCloseButtonHint));
  EXPECT_FALSE(window_flags.testFlag(Qt::FramelessWindowHint));
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

TEST(QtSettingsDialogTests, DefaultMaterialStagesThenPublishesOnlyAfterPersistence) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-default-material-settings";
  char *argv[] = {name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  QtUiScaleApplicationController scale_controller(application);
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const std::filesystem::path directory(temporary.path().toStdWString());
  const std::filesystem::path image_path = directory / "selected.png";
  QImage image(16, 8, QImage::Format_RGBA8888);
  image.fill(QColor(28, 180, 70));
  ASSERT_TRUE(image.save(QString::fromStdWString(image_path.wstring())));
  SceneSession session;
  QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
  UISettings settings;
  ViewportSettings viewport = ViewportSettings::defaults();
  LocalizationService localization = loaded_localization();
  std::optional<std::filesystem::path> selection;
  std::vector<QtSettingsChange> changes;
  int persistence_calls = 0;
  QtSettingsDialog dialog;
  dialog.setAttribute(Qt::WA_DontShowOnScreen, true);
  scale_controller.add_change_listener(dialog, [&](const QtUiScaleMetrics &metrics) {
    dialog.apply_ui_scale(metrics);
  });
  dialog.set_context(settings, viewport, localization, editor.commands(),
      [&](const QtSettingsChange &change) {
        EXPECT_EQ(UISettings::load(directory / "ui.ini").default_material_texture(),
                  settings.default_material_texture());
        changes.push_back(change);
      }, [&]() {
        ++persistence_calls;
        return settings.save(directory / "ui.ini");
      }, directory, image_path, [&]() { return selection; });
  auto *browse = dialog.findChild<QPushButton *>(QStringLiteral("settingsDefaultMaterialBrowse"));
  auto *preview = dialog.findChild<QLabel *>(QStringLiteral("settingsDefaultMaterialPreview"));
  auto *path = dialog.findChild<QLabel *>(QStringLiteral("settingsDefaultMaterialPath"));
  auto *apply = dialog.findChild<QPushButton *>(QStringLiteral("settingsApply"));
  ASSERT_NE(browse, nullptr);
  ASSERT_NE(preview, nullptr);
  ASSERT_NE(path, nullptr);
  ASSERT_NE(apply, nullptr);
  dialog.show();
  QCoreApplication::processEvents();
  browse->click();
  EXPECT_FALSE(apply->isEnabled());
  selection = image_path;
  browse->click();
  EXPECT_TRUE(apply->isEnabled());
  EXPECT_TRUE(settings.default_material_texture().empty());
  EXPECT_TRUE(changes.empty());
  EXPECT_EQ(persistence_calls, 0);
  EXPECT_EQ(path->text(), QStringLiteral("selected.png"));
  ASSERT_FALSE(preview->pixmap().isNull());
  EXPECT_EQ(preview->pixmap().toImage().pixelColor(2, 2), QColor(28, 180, 70));
  dialog.reject();
  EXPECT_TRUE(settings.default_material_texture().empty());
  EXPECT_FALSE(std::filesystem::exists(directory / "default_material"));
  dialog.show();
  browse->click();
  const QString capture_directory = QString::fromUtf8(
      qgetenv("QUADER_DEFAULT_MATERIAL_SCREENSHOT_DIR"));
  auto *categories = dialog.findChild<QListWidget *>(QStringLiteral("settingsCategories"));
  ASSERT_NE(categories, nullptr);
  for (int index = 0; index < categories->count(); ++index) {
    if (categories->item(index)->data(Qt::UserRole).toString() ==
        QStringLiteral("viewport")) {
      categories->setCurrentRow(index);
      break;
    }
  }
  for (float scale : {1.0f, 1.5f, 2.0f}) {
    (void)scale_controller.apply_factor(scale);
    EXPECT_EQ(preview->size(), QSize(qRound(64 * scale), qRound(64 * scale)));
    EXPECT_FALSE(preview->pixmap().isNull());
    if (!capture_directory.isEmpty() && scale != 1.5f) {
      ASSERT_TRUE(QDir().mkpath(capture_directory));
      dialog.resize(qRound(760 * scale), qRound(580 * scale));
      QCoreApplication::processEvents();
      const QString capture_path = QDir(capture_directory).filePath(
          QStringLiteral("default-material-%1.png").arg(qRound(100 * scale)));
      EXPECT_TRUE(dialog.grab().save(capture_path)) << capture_path.toStdString();
    }
  }
  apply->click();
  ASSERT_EQ(persistence_calls, 1);
  ASSERT_EQ(changes.size(), 1u);
  EXPECT_EQ(changes.back().kind, QtSettingsChangeKind::DefaultMaterialCommit);
  const std::filesystem::path imported = settings.default_material_texture();
  EXPECT_EQ(imported.parent_path().parent_path(), directory / "default_material");
  EXPECT_EQ(imported.filename(), image_path.filename());
  EXPECT_EQ(path->text(), QStringLiteral("selected.png"));
  EXPECT_EQ(changes.back().default_material_texture, imported);
  EXPECT_FALSE(apply->isEnabled());
  ASSERT_TRUE(std::filesystem::remove(image_path));
  EXPECT_TRUE(std::filesystem::exists(imported));
  EXPECT_EQ(UISettings::load(directory / "ui.ini").default_material_texture(), imported);
  selection = std::filesystem::path{};
  browse->click();
  dialog.accept();
  EXPECT_TRUE(settings.default_material_texture().empty());
  EXPECT_EQ(persistence_calls, 2);
  ASSERT_EQ(changes.size(), 2u);
  EXPECT_TRUE(changes.back().default_material_texture->empty());
}

TEST(QtSettingsDialogTests, DefaultMaterialFailurePreservesDraftForRetryAndRebind) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-default-material-failure";
  char *argv[] = {name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const std::filesystem::path directory(temporary.path().toStdWString());
  const std::filesystem::path selected_path = directory / "new.png";
  QImage image(4, 4, QImage::Format_RGB32);
  image.fill(Qt::blue);
  ASSERT_TRUE(image.save(QString::fromStdWString(selected_path.wstring())));
  SceneSession first;
  SceneSession second;
  QuaderEditorApi old_editor(QuaderApiSessionInternalAccess::from_scene_session(first));
  QuaderEditorApi new_editor(QuaderApiSessionInternalAccess::from_scene_session(second));
  UISettings settings;
  ViewportSettings viewport = ViewportSettings::defaults();
  LocalizationService localization = loaded_localization();
  std::filesystem::path selected = selected_path;
  bool persistence_succeeds = false;
  int old_notifications = 0;
  int new_notifications = 0;
  QtSettingsDialog dialog;
  dialog.setAttribute(Qt::WA_DontShowOnScreen, true);
  const auto persist = [&]() { return persistence_succeeds; };
  const auto picker = [&]() { return std::optional{selected}; };
  dialog.set_context(settings, viewport, localization, old_editor.commands(),
      [&](const QtSettingsChange &) { ++old_notifications; }, persist,
      directory, selected_path, picker);
  auto *browse = dialog.findChild<QPushButton *>(QStringLiteral("settingsDefaultMaterialBrowse"));
  auto *apply = dialog.findChild<QPushButton *>(QStringLiteral("settingsApply"));
  auto *label = dialog.findChild<QLabel *>(QStringLiteral("settingsDefaultMaterialPath"));
  ASSERT_NE(browse, nullptr);
  ASSERT_NE(apply, nullptr);
  ASSERT_NE(label, nullptr);
  dialog.show();
  browse->click();
  apply->click();
  EXPECT_TRUE(settings.default_material_texture().empty());
  EXPECT_EQ(old_notifications, 0);
  EXPECT_TRUE(apply->isEnabled());
  EXPECT_EQ(label->text(), QStringLiteral("new.png"));
  auto *failure = dialog.findChild<QMessageBox *>(QStringLiteral("settingsPersistenceErrorDialog"));
  ASSERT_NE(failure, nullptr);
  failure->accept();
  selected = directory / "missing.png";
  browse->click();
  EXPECT_EQ(label->text(), QStringLiteral("new.png"));
  EXPECT_TRUE(settings.default_material_texture().empty());
  failure = dialog.findChild<QMessageBox *>(QStringLiteral("settingsDefaultMaterialErrorDialog"));
  ASSERT_NE(failure, nullptr);
  failure->accept();
  dialog.set_context(settings, viewport, localization, new_editor.commands(),
      [&](const QtSettingsChange &) { ++new_notifications; }, persist,
      directory, selected_path, picker);
  EXPECT_TRUE(apply->isEnabled());
  persistence_succeeds = true;
  apply->click();
  EXPECT_FALSE(settings.default_material_texture().empty());
  EXPECT_EQ(old_notifications, 0);
  EXPECT_EQ(new_notifications, 1);
  selected = selected_path;
  browse->click();
  settings.set_default_material_texture({});
  dialog.synchronize_default_material_texture();
  EXPECT_FALSE(apply->isEnabled());
  EXPECT_EQ(label->text(), QStringLiteral("Default"));
  dialog.reject();
}

TEST(QtSettingsDialogTests, DefaultMaterialPickerRejectsAStaleRuntimeSelection) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-default-material-picker-rebind";
  char *argv[] = {name, nullptr};
  QApplication application(argc, argv);
  SceneSession session;
  QuaderEditorApi editor(QuaderApiSessionInternalAccess::from_scene_session(session));
  UISettings first;
  UISettings replacement;
  ViewportSettings viewport = ViewportSettings::defaults();
  LocalizationService localization = loaded_localization();
  QtSettingsDialog dialog;
  dialog.setAttribute(Qt::WA_DontShowOnScreen, true);
  dialog.set_context(first, viewport, localization, editor.commands(), {}, {}, {}, {},
      [&]() -> std::optional<std::filesystem::path> {
        dialog.set_context(replacement, viewport, localization, editor.commands(), {});
        return std::filesystem::path{"not-a-valid-file.png"};
      });
  dialog.show();
  auto *browse = dialog.findChild<QPushButton *>(QStringLiteral("settingsDefaultMaterialBrowse"));
  ASSERT_NE(browse, nullptr);
  browse->click();
  EXPECT_TRUE(first.default_material_texture().empty());
  EXPECT_TRUE(replacement.default_material_texture().empty());
  EXPECT_EQ(dialog.findChild<QMessageBox *>(QStringLiteral("settingsDefaultMaterialErrorDialog")), nullptr);
  dialog.reject();
}

} // namespace
} // namespace quader
