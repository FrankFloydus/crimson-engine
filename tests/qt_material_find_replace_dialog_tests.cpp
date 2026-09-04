////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "ui/qt_material_find_replace_dialog.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>

#include <optional>
#include <string_view>
#include <utility>

namespace quader {
namespace {

ProjectAssetId required_dialog_project_id(std::string_view text) {
  const std::optional<ProjectAssetId> parsed = ProjectAssetId::parse(text);
  EXPECT_TRUE(parsed.has_value());
  return parsed.value_or(ProjectAssetId{});
}

QtMaterialPresentationItem presentation_item(
    QuaderMaterialReference reference, QString name, QColor color,
    bool available = true) {
  QtMaterialPresentationItem item{
      .reference = reference,
      .stable_id = qt_material_reference_stable_id(reference),
      .name = std::move(name),
      .fallback_color = std::move(color),
      .content_revision = 1,
      .available = available,
  };
  item.thumbnail = make_qt_material_thumbnail(item);
  return item;
}

TEST(QtMaterialFindReplaceDialogTests,
     PresentsTypedColumnsValidatesAndDispatchesOnlyFinalReplace) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-material-find-replace-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));
  application.setQuitOnLastWindowClosed(false);

  QWidget owner;
  auto *dialog = new QtMaterialFindReplaceDialog(&owner);
  dialog->setAttribute(Qt::WA_DontShowOnScreen, true);
  EXPECT_NE(qobject_cast<QDialog *>(dialog), nullptr);
  EXPECT_EQ(dialog->windowModality(), Qt::WindowModal);
  EXPECT_FALSE(dialog->testAttribute(Qt::WA_DeleteOnClose));
  EXPECT_FALSE(dialog->windowFlags().testFlag(Qt::WindowMaximizeButtonHint));

  dialog->set_labels(QtMaterialFindReplaceLabels{
      .window_title = QStringLiteral("Swap materials"),
      .find = QStringLiteral("Current"),
      .replace = QStringLiteral("New"),
      .browse = QStringLiteral("Choose…"),
      .empty = QStringLiteral("Nothing chosen"),
      .replace_action = QStringLiteral("Swap"),
      .cancel_action = QStringLiteral("Leave"),
  });
  EXPECT_EQ(dialog->windowTitle(), QStringLiteral("Swap materials"));

  auto *find_preview = dialog->findChild<QLabel *>(
      QStringLiteral("materialFindReplace.find.preview"));
  auto *replacement_preview = dialog->findChild<QLabel *>(
      QStringLiteral("materialFindReplace.replace.preview"));
  auto *find_name = dialog->findChild<QLabel *>(
      QStringLiteral("materialFindReplace.find.name"));
  auto *replacement_name = dialog->findChild<QLabel *>(
      QStringLiteral("materialFindReplace.replace.name"));
  auto *find_browse = dialog->findChild<QPushButton *>(
      QStringLiteral("materialFindReplace.find.browse"));
  auto *replacement_browse = dialog->findChild<QPushButton *>(
      QStringLiteral("materialFindReplace.replace.browse"));
  auto *replace_button = dialog->findChild<QPushButton *>(
      QStringLiteral("materialFindReplace.action.replace"));
  auto *cancel_button = dialog->findChild<QPushButton *>(
      QStringLiteral("materialFindReplace.action.cancel"));
  ASSERT_NE(find_preview, nullptr);
  ASSERT_NE(replacement_preview, nullptr);
  ASSERT_NE(find_name, nullptr);
  ASSERT_NE(replacement_name, nullptr);
  ASSERT_NE(find_browse, nullptr);
  ASSERT_NE(replacement_browse, nullptr);
  ASSERT_NE(replace_button, nullptr);
  ASSERT_NE(cancel_button, nullptr);
  EXPECT_EQ(find_preview->size(), QSize(128, 128));
  EXPECT_EQ(replacement_preview->size(), QSize(128, 128));
  EXPECT_FALSE(find_preview->hasScaledContents());
  EXPECT_FALSE(replacement_preview->hasScaledContents());
  EXPECT_EQ(find_preview->text(), QStringLiteral("Nothing chosen"));
  EXPECT_EQ(replacement_preview->text(), QStringLiteral("Nothing chosen"));
  EXPECT_TRUE(find_browse->icon().isNull());
  EXPECT_TRUE(replacement_browse->icon().isNull());
  EXPECT_EQ(find_browse->text(), QStringLiteral("Choose…"));
  EXPECT_EQ(replacement_browse->text(), QStringLiteral("Choose…"));
  EXPECT_FALSE(find_browse->accessibleName().isEmpty());
  EXPECT_FALSE(replacement_browse->accessibleName().isEmpty());
  EXPECT_EQ(replace_button->text(), QStringLiteral("Swap"));
  EXPECT_EQ(cancel_button->text(), QStringLiteral("Leave"));
  EXPECT_FALSE(replace_button->isEnabled());
  EXPECT_FALSE(dialog->replace_available());

  int find_browse_count = 0;
  int replacement_browse_count = 0;
  dialog->set_browse_material_callback(
      [&find_browse_count,
       &replacement_browse_count](QtMaterialFindReplaceColumn column) {
        if (column == QtMaterialFindReplaceColumn::Find) {
          ++find_browse_count;
        } else {
          ++replacement_browse_count;
        }
      });
  find_browse->click();
  replacement_browse->click();
  EXPECT_EQ(find_browse_count, 1);
  EXPECT_EQ(replacement_browse_count, 1);

  const QuaderMaterialReference missing_source =
      QuaderMaterialReference::embedded(42);
  const QuaderMaterialReference replacement =
      QuaderMaterialReference::project(required_dialog_project_id(
          "22222222-2222-2222-2222-222222222222"));
  const QtMaterialPresentationItem source_item = presentation_item(
      missing_source, QStringLiteral("Missing legacy material"),
      QColor(180, 80, 80), false);
  const QtMaterialPresentationItem replacement_item = presentation_item(
      replacement, QStringLiteral("Painted Steel"), QColor(90, 120, 150));

  dialog->set_find_material(source_item);
  ASSERT_TRUE(dialog->material(QtMaterialFindReplaceColumn::Find).has_value());
  EXPECT_TRUE(find_preview->text().isEmpty());
  EXPECT_EQ(find_name->toolTip(), QStringLiteral("Missing legacy material"));
  EXPECT_FALSE(replace_button->isEnabled())
      << "a valid missing source remains findable but needs a destination";

  QtMaterialPresentationItem unavailable_replacement = replacement_item;
  unavailable_replacement.available = false;
  dialog->set_replacement_material(unavailable_replacement);
  EXPECT_FALSE(replace_button->isEnabled())
      << "an unresolved destination must never enable replacement";

  QtMaterialPresentationItem identical_destination = source_item;
  identical_destination.available = true;
  dialog->set_replacement_material(identical_destination);
  EXPECT_FALSE(replace_button->isEnabled())
      << "the same typed material reference is a no-op";

  dialog->set_replacement_material(replacement_item);
  EXPECT_TRUE(replacement_preview->text().isEmpty());
  EXPECT_EQ(replacement_name->toolTip(), QStringLiteral("Painted Steel"));
  EXPECT_TRUE(dialog->replace_available());
  EXPECT_TRUE(replace_button->isEnabled());

  const qint64 unscaled_preview_key = replacement_preview->pixmap().cacheKey();
  dialog->apply_ui_scale(QtUiScaleMetrics(2.0F, 1U));
  EXPECT_EQ(find_preview->size(), QSize(256, 256));
  EXPECT_EQ(replacement_preview->size(), QSize(256, 256));
  EXPECT_EQ(find_name->minimumWidth(), 256);
  EXPECT_EQ(find_name->maximumWidth(), 256);
  EXPECT_NE(replacement_preview->pixmap().cacheKey(), unscaled_preview_key);
  EXPECT_EQ(replacement_preview->pixmap().deviceIndependentSize(),
            QSizeF(256.0, 256.0));
  const qint64 scaled_preview_key = replacement_preview->pixmap().cacheKey();
  dialog->apply_ui_scale(QtUiScaleMetrics(2.0F, 1U));
  EXPECT_EQ(replacement_preview->pixmap().cacheKey(), scaled_preview_key)
      << "one scale revision must not rebuild a retained preview twice";
  dialog->apply_ui_scale(QtUiScaleMetrics(1.0F, 2U));
  EXPECT_EQ(find_preview->size(), QSize(128, 128));
  EXPECT_EQ(replacement_name->minimumWidth(), 128);
  EXPECT_EQ(replacement_preview->pixmap().deviceIndependentSize(),
            QSizeF(128.0, 128.0));

  QtMaterialPresentationItem deleted_replacement = replacement_item;
  deleted_replacement.name = QStringLiteral("Missing replacement");
  deleted_replacement.content_revision = 2u;
  deleted_replacement.available = false;
  deleted_replacement.fallback_color = QColor(150, 40, 40);
  deleted_replacement.thumbnail =
      make_qt_material_thumbnail(deleted_replacement);
  dialog->set_replacement_material(deleted_replacement);
  EXPECT_EQ(replacement_name->toolTip(),
            QStringLiteral("Missing replacement"));
  EXPECT_FALSE(dialog->replace_available());
  EXPECT_FALSE(replace_button->isEnabled())
      << "a retained destination deleted during a cache refresh must be "
         "revalidated before replacement";

  QtMaterialPresentationItem refreshed_source = source_item;
  refreshed_source.name = QStringLiteral("Missing source refreshed");
  refreshed_source.content_revision = 2u;
  refreshed_source.thumbnail = make_qt_material_thumbnail(refreshed_source);
  dialog->set_find_material(refreshed_source);
  ASSERT_TRUE(dialog->material(QtMaterialFindReplaceColumn::Find).has_value());
  EXPECT_EQ(dialog->material(QtMaterialFindReplaceColumn::Find)->reference,
            missing_source);
  EXPECT_EQ(find_name->toolTip(),
            QStringLiteral("Missing source refreshed"));

  dialog->set_replacement_material(replacement_item);
  EXPECT_TRUE(replace_button->isEnabled());

  int replacement_count = 0;
  QuaderMaterialReference dispatched_source;
  QuaderMaterialReference dispatched_replacement;
  dialog->set_replace_materials_callback(
      [&replacement_count, &dispatched_source,
       &dispatched_replacement](QuaderMaterialReference source,
                                QuaderMaterialReference target) {
        ++replacement_count;
        dispatched_source = source;
        dispatched_replacement = target;
        return false;
      });

  dialog->show();
  QCoreApplication::processEvents();
  cancel_button->click();
  EXPECT_EQ(replacement_count, 0)
      << "Cancel must be side-effect free";
  EXPECT_FALSE(dialog->isVisible());
  EXPECT_TRUE(dialog->material(QtMaterialFindReplaceColumn::Find).has_value())
      << "retained state is cleared explicitly by its UISystem owner";

  dialog->show();
  QCoreApplication::processEvents();
  replace_button->click();
  EXPECT_EQ(replacement_count, 1);
  EXPECT_EQ(dispatched_source, missing_source);
  EXPECT_EQ(dispatched_replacement, replacement);
  EXPECT_TRUE(dialog->isVisible())
      << "a rejected owner operation must leave the dialog available";

  dialog->set_replace_materials_callback(
      [&replacement_count](QuaderMaterialReference,
                           QuaderMaterialReference) {
        ++replacement_count;
        return true;
      });
  replace_button->click();
  EXPECT_EQ(replacement_count, 2);
  EXPECT_FALSE(dialog->isVisible());
  EXPECT_EQ(dialog->result(), QDialog::Accepted);

  dialog->clear_materials();
  EXPECT_FALSE(dialog->material(QtMaterialFindReplaceColumn::Find).has_value());
  EXPECT_FALSE(
      dialog->material(QtMaterialFindReplaceColumn::Replace).has_value());
  EXPECT_FALSE(replace_button->isEnabled());
}

} // namespace
} // namespace quader
