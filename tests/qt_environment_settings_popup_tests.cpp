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
#include "ui/qt_environment_settings_popup.hpp"
#include "ui/qt_double_spin_box.hpp"
#include "ui/qt_numeric_spin_box.hpp"
#include "ui/ui_context.hpp"
#include "ui/ui_settings.hpp"
#include "viewport/viewport_settings.hpp"

#include <QApplication>
#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QFile>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLocale>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QSlider>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>

#include <array>
#include <filesystem>
#include <memory>

#ifndef QUADER_SOURCE_DIR
#define QUADER_SOURCE_DIR "."
#endif

namespace quader {
namespace {

struct EnvironmentPopupRuntime {
  SceneSession session;
  QuaderEditorApi editor{
      QuaderApiSessionInternalAccess::from_scene_session(session)};
  UISettings ui_settings = UISettings::defaults();
  ViewportSettings viewport_settings = ViewportSettings::defaults();
  LocalizationService localization;
  UIContext context{
      .intent = {.editor = editor},
      .scene = {.document = session.scene(),
                .assets = session.assets(),
                .modeling = session.modeling(),
                .selection = session.selection()},
      .preferences = {.ui = ui_settings, .viewport = viewport_settings},
      .localization = localization,
  };
};

QDoubleSpinBox *environment_intensity(QtEnvironmentSettingsPopup &popup) {
  return popup.findChild<QDoubleSpinBox *>(
      QStringLiteral("environmentSettings.environment.intensity"));
}

class QtEnvironmentSettingsPopupTests : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(QCoreApplication::instance(), nullptr);
    application_ = std::make_unique<QApplication>(argc_, argv_);
    application_->setQuitOnLastWindowClosed(false);
    QApplication::setStyle(QStringLiteral("Fusion"));
    runtime_ = std::make_unique<EnvironmentPopupRuntime>();
    ASSERT_TRUE(runtime_->localization.load(
        std::filesystem::path(QUADER_SOURCE_DIR) / "resources" / "locales",
        "en-US"));
  }

  void TearDown() override {
    runtime_.reset();
    application_.reset();
  }

  void refresh(QtEnvironmentSettingsPopup &popup) {
    popup.refresh(runtime_->context);
    QCoreApplication::processEvents();
  }

  std::unique_ptr<EnvironmentPopupRuntime> runtime_;

private:
  int argc_ = 1;
  char executable_[32] = "quader-environment-popup-tests";
  char *argv_[2] = {executable_, nullptr};
  std::unique_ptr<QApplication> application_;
};

TEST_F(QtEnvironmentSettingsPopupTests,
       EmptyDocumentExposesAllSectionsWithoutObjectSelection) {
  QtEnvironmentSettingsPopup popup;
  refresh(popup);

  EXPECT_TRUE(runtime_->session.scene().root_entities().empty());
  EXPECT_TRUE(runtime_->session.scene().selected_entities().empty());
  EXPECT_TRUE(popup.findChildren<QSlider *>().empty());
  const auto numeric_fields = popup.findChildren<QDoubleSpinBox *>();
  ASSERT_FALSE(numeric_fields.empty());
  for (auto *field : numeric_fields) {
    EXPECT_NE(dynamic_cast<QtDoubleSpinBox *>(field), nullptr)
        << field->objectName().toStdString();
  }
  auto *tree = popup.findChild<QTreeWidget *>(
      QStringLiteral("environmentSettings.tree"));
  ASSERT_NE(tree, nullptr);
  ASSERT_EQ(tree->topLevelItemCount(), 4);
  const std::array expected_sections{"environment", "fog", "camera",
                                      "post_process"};
  for (int row = 0; row < tree->topLevelItemCount(); ++row) {
    const auto *section = tree->topLevelItem(row);
    EXPECT_EQ(section->data(0, Qt::UserRole).toString(),
              QString::fromLatin1(
                  expected_sections[static_cast<std::size_t>(row)]));
    EXPECT_GT(section->childCount(), 0);
  }

  auto *intensity = environment_intensity(popup);
  ASSERT_NE(intensity, nullptr);
  EXPECT_TRUE(intensity->isEnabled());
  intensity->setValue(0.375);
  intensity->editingFinished();
  EXPECT_FLOAT_EQ(runtime_->session.scene().scene_settings().environment.intensity,
                  0.375f);
  EXPECT_TRUE(runtime_->session.scene().root_entities().empty());
}

TEST_F(QtEnvironmentSettingsPopupTests,
       SelectionChangesRetainActiveNumericGestureAndControl) {
  const auto object = runtime_->editor.scene().create_empty();
  ASSERT_TRUE(object.ok()) << object.error_message();
  QtEnvironmentSettingsPopup popup;
  refresh(popup);
  QPointer<QDoubleSpinBox> intensity = environment_intensity(popup);
  ASSERT_NE(intensity, nullptr);
  intensity->setValue(0.375);
  refresh(popup);
  const auto updates = popup.presentation_update_count();

  ASSERT_TRUE(runtime_->editor.scene().select(object.value().id).ok());
  refresh(popup);
  ASSERT_NE(intensity, nullptr);
  EXPECT_EQ(environment_intensity(popup), intensity.data());
  EXPECT_EQ(popup.presentation_update_count(), updates);
  EXPECT_NEAR(intensity->value(), 0.375, 0.000001);

  intensity->setValue(0.625);
  refresh(popup);
  EXPECT_EQ(environment_intensity(popup), intensity.data());
  intensity->editingFinished();
  EXPECT_FLOAT_EQ(runtime_->session.scene().scene_settings().environment.intensity,
                  0.625f);
  EXPECT_EQ(runtime_->session.scene().primary_selection(), object.value().id);
}

TEST_F(QtEnvironmentSettingsPopupTests,
       UnchangedSettingsSkipPresentationForUnrelatedDocumentChanges) {
  const auto object = runtime_->editor.scene().create_empty();
  ASSERT_TRUE(object.ok()) << object.error_message();
  QtEnvironmentSettingsPopup popup;
  refresh(popup);
  QPointer<QDoubleSpinBox> intensity = environment_intensity(popup);
  ASSERT_NE(intensity, nullptr);
  const auto updates = popup.presentation_update_count();
  const auto settings_revision = runtime_->session.scene().scene_settings_revision();

  for (int index = 0; index < 8; ++index) {
    refresh(popup);
  }
  ASSERT_TRUE(runtime_->editor.scene()
                  .set_transform(object.value().id,
                                 {.position = {3.0f, 4.0f, 5.0f}})
                  .ok());
  refresh(popup);
  EXPECT_EQ(runtime_->session.scene().scene_settings_revision(), settings_revision);
  EXPECT_EQ(popup.presentation_update_count(), updates);
  EXPECT_EQ(environment_intensity(popup), intensity.data());

  auto settings = runtime_->editor.query().scene_settings();
  settings.environment.intensity = 0.875f;
  ASSERT_TRUE(runtime_->editor.scene().set_scene_settings(settings).ok());
  refresh(popup);
  EXPECT_GT(popup.presentation_update_count(), updates);
  EXPECT_EQ(environment_intensity(popup), intensity.data());
  EXPECT_NEAR(intensity->value(), 0.875, 0.000001);
}

TEST_F(QtEnvironmentSettingsPopupTests,
       LocalizationOwnerAndSameLocaleReloadRefreshRetainedSections) {
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  const auto write_catalog = [&](const QByteArray &environment_title) {
    QFile file(directory.filePath(QStringLiteral("en-US.lang")));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      return false;
    }
    const QByteArray catalog =
        QByteArray("ui.inspector.environment=") + environment_title + '\n';
    return file.write(catalog) == catalog.size();
  };
  ASSERT_TRUE(write_catalog("Studio Environment"));
  LocalizationService replacement;
  ASSERT_TRUE(replacement.load(
      std::filesystem::path(directory.path().toStdWString()), "en-US"));
  ASSERT_EQ(replacement.current_locale(), runtime_->localization.current_locale());
  ASSERT_EQ(replacement.revision(), runtime_->localization.revision());

  QtEnvironmentSettingsPopup popup;
  refresh(popup);
  auto *tree = popup.findChild<QTreeWidget *>(
      QStringLiteral("environmentSettings.tree"));
  ASSERT_NE(tree, nullptr);
  auto *section = tree->topLevelItem(0);
  ASSERT_NE(section, nullptr);
  QPointer<QDoubleSpinBox> intensity = environment_intensity(popup);
  ASSERT_NE(intensity, nullptr);
  section->setExpanded(false);
  const auto initial_updates = popup.presentation_update_count();
  popup.refresh(runtime_->editor, runtime_->session.scene(), &replacement);
  EXPECT_GT(popup.presentation_update_count(), initial_updates);
  EXPECT_EQ(section->text(0), QStringLiteral("Studio Environment"));
  EXPECT_EQ(environment_intensity(popup), intensity.data());
  EXPECT_FALSE(section->isExpanded());

  const auto replacement_updates = popup.presentation_update_count();
  const auto old_revision = replacement.revision();
  ASSERT_TRUE(write_catalog("Reloaded Environment"));
  ASSERT_TRUE(replacement.load(
      std::filesystem::path(directory.path().toStdWString()), "en-US"));
  ASSERT_GT(replacement.revision(), old_revision);
  popup.refresh(runtime_->editor, runtime_->session.scene(), &replacement);
  EXPECT_GT(popup.presentation_update_count(), replacement_updates);
  EXPECT_EQ(section->text(0), QStringLiteral("Reloaded Environment"));
  EXPECT_EQ(environment_intensity(popup), intensity.data());
  EXPECT_FALSE(section->isExpanded());
}

TEST_F(QtEnvironmentSettingsPopupTests,
       MultipleViewportsShareDocumentSettingsAndUndo) {
  QtEnvironmentSettingsPopup first;
  QtEnvironmentSettingsPopup second;
  refresh(first);
  refresh(second);
  QPointer<QDoubleSpinBox> first_intensity = environment_intensity(first);
  QPointer<QDoubleSpinBox> second_intensity = environment_intensity(second);
  ASSERT_NE(first_intensity, nullptr);
  ASSERT_NE(second_intensity, nullptr);
  const float baseline =
      runtime_->session.scene().scene_settings().environment.intensity;
  first_intensity->setValue(0.375);
  first_intensity->editingFinished();
  refresh(first);
  refresh(second);
  EXPECT_EQ(environment_intensity(first), first_intensity.data());
  EXPECT_EQ(environment_intensity(second), second_intensity.data());
  EXPECT_NEAR(second_intensity->value(), 0.375, 0.000001);

  ASSERT_TRUE(runtime_->editor.history().undo().ok());
  refresh(first);
  refresh(second);
  EXPECT_NEAR(first_intensity->value(), baseline, 0.000001);
  EXPECT_NEAR(second_intensity->value(), baseline, 0.000001);
}

TEST_F(QtEnvironmentSettingsPopupTests,
       PopupDestructionDuringNestedColorDialogSafelyUnwinds) {
  auto popup = std::make_unique<QtEnvironmentSettingsPopup>();
  popup->setAttribute(Qt::WA_DontShowOnScreen, true);
  refresh(*popup);
  QPointer<QPushButton> color = popup->findChild<QPushButton *>(
      QStringLiteral("environmentSettings.environment.clear_color"));
  ASSERT_NE(color, nullptr);
  QPointer<QColorDialog> dialog;
  bool visited_dialog = false;
  QTimer::singleShot(0, QCoreApplication::instance(), [&]() {
    dialog = popup->findChild<QColorDialog *>(
        QStringLiteral("environmentSettings.environment.clear_color.dialog"));
    EXPECT_NE(dialog, nullptr);
    if (dialog) {
      visited_dialog = true;
      dialog->setCurrentColor(QColor::fromRgbF(0.7, 0.2, 0.3));
    }
    popup.reset();
    runtime_.reset();
    EXPECT_TRUE(color.isNull());
    EXPECT_TRUE(dialog.isNull());
  });

  color->click();
  EXPECT_TRUE(visited_dialog);
  EXPECT_EQ(popup, nullptr);
  EXPECT_EQ(runtime_, nullptr);
  EXPECT_TRUE(dialog.isNull());
}

TEST_F(QtEnvironmentSettingsPopupTests,
       AbandonClosesNestedColorDialogBeforeOutgoingRuntimeIsDestroyed) {
  QWidget launcher;
  launcher.setAttribute(Qt::WA_DontShowOnScreen, true);
  launcher.resize(100, 30);
  launcher.show();
  QtEnvironmentSettingsPopup popup;
  popup.setAttribute(Qt::WA_DontShowOnScreen, true);
  refresh(popup);
  popup.show_below(launcher);
  ASSERT_TRUE(popup.isVisible());
  auto *color = popup.findChild<QPushButton *>(
      QStringLiteral("environmentSettings.environment.clear_color"));
  ASSERT_NE(color, nullptr);
  bool visited_dialog = false;
  QTimer::singleShot(0, &popup, [&]() {
    auto *dialog = popup.findChild<QColorDialog *>(
        QStringLiteral("environmentSettings.environment.clear_color.dialog"));
    EXPECT_NE(dialog, nullptr);
    if (dialog != nullptr) {
      visited_dialog = true;
      dialog->setCurrentColor(QColor::fromRgbF(0.7, 0.2, 0.3));
    }
    popup.abandon_runtime();
    EXPECT_FALSE(popup.isVisible());
    if (dialog != nullptr) {
      EXPECT_FALSE(dialog->isVisible());
    }
    runtime_.reset();
  });
  color->click();
  EXPECT_TRUE(visited_dialog);
  EXPECT_EQ(runtime_, nullptr);
  EXPECT_FALSE(popup.isVisible());

  runtime_ = std::make_unique<EnvironmentPopupRuntime>();
  popup.refresh(runtime_->editor, runtime_->session.scene(),
                &runtime_->localization);
  EXPECT_NE(environment_intensity(popup), nullptr);
  EXPECT_EQ(runtime_->session.history().undo_count(), 0u);
}

TEST_F(QtEnvironmentSettingsPopupTests,
       SameOwnerDocumentReplacementClosesAndAbandonsNumericGesture) {
  QWidget launcher;
  launcher.setAttribute(Qt::WA_DontShowOnScreen, true);
  launcher.resize(100, 30);
  launcher.show();
  QtEnvironmentSettingsPopup popup;
  popup.setAttribute(Qt::WA_DontShowOnScreen, true);
  refresh(popup);
  popup.show_below(launcher);
  ASSERT_TRUE(popup.isVisible());
  QPointer<QDoubleSpinBox> intensity = environment_intensity(popup);
  ASSERT_NE(intensity, nullptr);
  intensity->setValue(0.375);
  const SceneDocument *document = &runtime_->session.scene();
  const auto old_binding = runtime_->editor.query().document_binding();

  runtime_->session.replace_document(SceneContentDocument{});
  const auto replacement_binding = runtime_->editor.query().document_binding();
  EXPECT_EQ(&runtime_->session.scene(), document);
  EXPECT_EQ(replacement_binding.scene_session_id, old_binding.scene_session_id);
  EXPECT_NE(replacement_binding, old_binding);
  // A trailing field callback can arrive before the next presentation frame.
  // It must not apply the old document's draft to the replacement document.
  intensity->setValue(0.875);
  EXPECT_FALSE(runtime_->session.dirty());
  EXPECT_EQ(runtime_->session.history().undo_count(), 0u);
  refresh(popup);
  EXPECT_FALSE(popup.isVisible());
  ASSERT_NE(intensity, nullptr);
  EXPECT_EQ(environment_intensity(popup), intensity.data());
  EXPECT_FLOAT_EQ(static_cast<float>(intensity->value()),
                  runtime_->session.scene().scene_settings().environment.intensity);
  intensity->editingFinished();
  EXPECT_EQ(runtime_->session.history().undo_count(), 0u);
  EXPECT_FALSE(runtime_->session.dirty());

  popup.show_below(launcher);
  EXPECT_TRUE(popup.isVisible());
  intensity->setValue(0.625);
  intensity->editingFinished();
  EXPECT_FLOAT_EQ(runtime_->session.scene().scene_settings().environment.intensity,
                  0.625f);
  EXPECT_EQ(runtime_->session.history().undo_count(), 1u);
}

TEST_F(QtEnvironmentSettingsPopupTests,
       SameOwnerDocumentReplacementClosesNestedColorDialog) {
  QWidget launcher;
  launcher.setAttribute(Qt::WA_DontShowOnScreen, true);
  launcher.resize(100, 30);
  launcher.show();
  QtEnvironmentSettingsPopup popup;
  popup.setAttribute(Qt::WA_DontShowOnScreen, true);
  refresh(popup);
  popup.show_below(launcher);
  auto *color = popup.findChild<QPushButton *>(
      QStringLiteral("environmentSettings.environment.clear_color"));
  ASSERT_NE(color, nullptr);
  bool visited_dialog = false;
  QTimer::singleShot(0, &popup, [&]() {
    QPointer<QColorDialog> dialog = popup.findChild<QColorDialog *>(
        QStringLiteral("environmentSettings.environment.clear_color.dialog"));
    EXPECT_NE(dialog, nullptr);
    if (!dialog) {
      popup.abandon_runtime();
      return;
    }
    visited_dialog = true;
    dialog->setCurrentColor(QColor::fromRgbF(0.7, 0.2, 0.3));
    runtime_->session.replace_document(SceneContentDocument{});
    popup.refresh(runtime_->context);
    EXPECT_FALSE(popup.isVisible());
    EXPECT_FALSE(dialog->isVisible());
  });
  color->click();
  EXPECT_TRUE(visited_dialog);
  EXPECT_FALSE(popup.isVisible());
  EXPECT_EQ(runtime_->session.history().undo_count(), 0u);
  EXPECT_FALSE(runtime_->session.dirty());
}

TEST_F(QtEnvironmentSettingsPopupTests,
       FirstEscapeCancelsNumericTextAndSecondEscapeDismissesPopup) {
  QWidget launcher;
  launcher.setAttribute(Qt::WA_DontShowOnScreen, true);
  launcher.resize(100, 30);
  launcher.show();
  QtEnvironmentSettingsPopup popup;
  popup.setAttribute(Qt::WA_DontShowOnScreen, true);
  refresh(popup);
  popup.show_below(launcher);
  QCoreApplication::processEvents();
  ASSERT_TRUE(popup.isVisible());
  auto *intensity = environment_intensity(popup);
  ASSERT_NE(intensity, nullptr);
  // Production main sets C locale before QApplication; this fixture types a dot.
  intensity->setLocale(QLocale::c());
  auto *editor = intensity->findChild<QLineEdit *>();
  ASSERT_NE(editor, nullptr);
  auto &control = qt_install_numeric_spin_box(*intensity);
  const double original_value = intensity->value();
  const QPoint local = editor->rect().center();
  const QPoint global = editor->mapToGlobal(local);
  QMouseEvent press(QEvent::MouseButtonPress, QPointF(local), QPointF(global),
                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  QCoreApplication::sendEvent(editor, &press);
  QMouseEvent release(QEvent::MouseButtonRelease, QPointF(local), QPointF(global),
                      Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  QCoreApplication::sendEvent(editor, &release);
  ASSERT_TRUE(control.is_editing());
  for (QChar character : QStringLiteral("0.375")) {
    QKeyEvent key(QEvent::KeyPress, character.unicode(), Qt::NoModifier,
                  QString(character));
    QCoreApplication::sendEvent(editor, &key);
  }
  ASSERT_DOUBLE_EQ(intensity->value(), 0.375);
  ASSERT_FLOAT_EQ(
      runtime_->session.scene().scene_settings().environment.intensity, 0.375f);

  QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
  QCoreApplication::sendEvent(editor, &escape);
  QKeyEvent escape_release(QEvent::KeyRelease, Qt::Key_Escape, Qt::NoModifier);
  QCoreApplication::sendEvent(editor, &escape_release);
  EXPECT_TRUE(escape.isAccepted());
  EXPECT_TRUE(escape_release.isAccepted());
  EXPECT_FALSE(control.is_editing());
  EXPECT_DOUBLE_EQ(intensity->value(), original_value);
  EXPECT_FLOAT_EQ(
      runtime_->session.scene().scene_settings().environment.intensity,
      static_cast<float>(original_value));
  ASSERT_TRUE(popup.isVisible());

  QKeyEvent second_escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
  QCoreApplication::sendEvent(editor, &second_escape);
  EXPECT_FALSE(popup.isVisible());
}

TEST_F(QtEnvironmentSettingsPopupTests, PopupClampsToScreenAndDismissesWithEscape) {
  QScreen *screen = QApplication::primaryScreen();
  ASSERT_NE(screen, nullptr);
  const QRect available = screen->availableGeometry();
  QWidget launcher;
  launcher.setAttribute(Qt::WA_DontShowOnScreen, true);
  launcher.setGeometry(available.right() - 95, available.bottom() - 29, 96, 30);
  launcher.show();
  QtEnvironmentSettingsPopup popup;
  popup.setAttribute(Qt::WA_DontShowOnScreen, true);
  refresh(popup);
  popup.show_below(launcher);
  QCoreApplication::processEvents();
  ASSERT_TRUE(popup.isVisible());
  EXPECT_TRUE(available.contains(popup.frameGeometry()));

  QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
  QCoreApplication::sendEvent(&popup, &escape);
  EXPECT_FALSE(popup.isVisible());
}

} // namespace
} // namespace quader
