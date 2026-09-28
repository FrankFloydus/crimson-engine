////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "ui/qt_project_asset_dialog.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QLayout>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QTemporaryDir>

#include <filesystem>
#include <memory>
#include <system_error>
#include <utility>

namespace quader {
namespace {

/** Owns a fresh Qt application and isolated project for each picker scenario. */
class QtProjectAssetDialogTests : public testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(QCoreApplication::instance(), nullptr);
    application_ = std::make_unique<QApplication>(argc_, argv_);
    application_->setQuitOnLastWindowClosed(false);
    ASSERT_TRUE(project_.isValid());
    ASSERT_TRUE(QDir(project_.path()).mkpath(QStringLiteral("Maps")));
    ASSERT_TRUE(QDir(project_.path()).mkpath(QStringLiteral("Maps/Sub")));
    QFile scene(project_.filePath(QStringLiteral("Maps/Main.qdr")));
    ASSERT_TRUE(scene.open(QIODevice::WriteOnly));
    scene.close();
    snapshot_.generation = 7;
    snapshot_.revision = 11;
    snapshot_.dependency_revision = 5;
    snapshot_.items = {
        {.id = QStringLiteral("folder:Maps"),
         .relative_path = QStringLiteral("Maps"),
         .name = QStringLiteral("Maps"),
         .kind = QtAssetBrowserKind::Folder},
        {.id = QStringLiteral("folder:Maps/Sub"),
         .relative_path = QStringLiteral("Maps/Sub"),
         .name = QStringLiteral("Sub"),
         .kind = QtAssetBrowserKind::Folder},
        {.id = QStringLiteral("scene-main"),
         .relative_path = QStringLiteral("Maps/Main.qdr"),
         .name = QStringLiteral("Main"),
         .kind = QtAssetBrowserKind::Scene},
    };
  }

  [[nodiscard]] QLineEdit *name_field(QtProjectAssetDialog &dialog) {
    return dialog.findChild<QLineEdit *>(QStringLiteral("projectAsset.name"));
  }

  [[nodiscard]] QPushButton *accept_button(QtProjectAssetDialog &dialog) {
    return dialog.findChild<QPushButton *>(QStringLiteral("projectAsset.accept"));
  }

  int argc_ = 1;
  char application_name_[27] = "quader-project-picker-test";
  char *argv_[2] = {application_name_, nullptr};
  std::unique_ptr<QApplication> application_;
  QTemporaryDir project_;
  QtAssetBrowserSnapshot snapshot_;
};

TEST_F(QtProjectAssetDialogTests, SaveUsesCurrentProjectFolderAndQdrSuffix) {
  QtProjectAssetDialog dialog(QtProjectAssetDialogMode::SaveScene,
                              project_.path(), snapshot_);
  dialog.set_initial_path(QStringLiteral("Maps"));
  ASSERT_NE(name_field(dialog), nullptr);
  name_field(dialog)->setText(QStringLiteral("New Scene"));
  EXPECT_TRUE(dialog.selected_relative_path().isEmpty());
  ASSERT_NE(accept_button(dialog), nullptr);
  ASSERT_TRUE(accept_button(dialog)->isEnabled());
  dialog.accept();
  EXPECT_EQ(dialog.result(), QDialog::Accepted);
  EXPECT_EQ(dialog.selected_relative_path(), QStringLiteral("Maps/New Scene.qdr"));
  EXPECT_FALSE(QFile::exists(project_.filePath(QStringLiteral("Maps/New Scene.qdr"))));
}

TEST_F(QtProjectAssetDialogTests, FirstSaveCanUseProjectRoot) {
  QtProjectAssetDialog dialog(QtProjectAssetDialogMode::SaveScene,
                              project_.path(), snapshot_);
  dialog.set_initial_path(QStringLiteral("Untitled.qdr"));
  dialog.accept();
  EXPECT_EQ(dialog.result(), QDialog::Accepted);
  EXPECT_EQ(dialog.selected_relative_path(), QStringLiteral("Untitled.qdr"));
}

TEST_F(QtProjectAssetDialogTests, MaterialSaveUsesQmatAndShowsOnlyMaterials) {
  snapshot_.items.push_back({
      .id = QStringLiteral("material-existing"),
      .relative_path = QStringLiteral("Maps/Stone.qmat"),
      .name = QStringLiteral("Stone"),
      .kind = QtAssetBrowserKind::Material,
  });
  QtProjectAssetDialog dialog(QtProjectAssetDialogMode::SaveMaterial,
                              project_.path(), snapshot_);
  EXPECT_EQ(dialog.windowTitle(), QStringLiteral("Save Material"));
  dialog.set_initial_path(QStringLiteral("Maps/Stone.qmat"));
  ASSERT_NE(name_field(dialog), nullptr);
  EXPECT_EQ(name_field(dialog)->text(), QStringLiteral("Stone.qmat"));
  EXPECT_EQ(dialog.browser().selected_folder(), QStringLiteral("Maps"));
  auto *grid = dialog.browser().findChild<QListView *>(
      QStringLiteral("assetBrowser.grid"));
  ASSERT_NE(grid, nullptr);
  // The Sub folder stays navigable, while Main.qdr is excluded by asset type.
  EXPECT_EQ(grid->model()->rowCount(), 2);
  EXPECT_FALSE(dialog.browser().reveal_item(QStringLiteral("scene-main")));
  name_field(dialog)->setText(QStringLiteral("Stone Copy"));
  dialog.accept();
  EXPECT_EQ(dialog.result(), QDialog::Accepted);
  EXPECT_EQ(dialog.selected_relative_path(), QStringLiteral("Maps/Stone Copy.qmat"));
  EXPECT_FALSE(QFile::exists(project_.filePath(QStringLiteral("Maps/Stone Copy.qmat"))));
}

TEST_F(QtProjectAssetDialogTests, MaterialSaveRejectsOtherAssetTypesAndEscapingNames) {
  QtProjectAssetDialog dialog(QtProjectAssetDialogMode::SaveMaterial,
                              project_.path(), snapshot_);
  ASSERT_NE(name_field(dialog), nullptr);
  ASSERT_NE(accept_button(dialog), nullptr);
  for (const QString &name : {QStringLiteral("Scene.qdr"),
                              QStringLiteral("Image.png"),
                              QStringLiteral("../Outside.qmat"),
                              QStringLiteral("C:\\Outside.qmat")}) {
    SCOPED_TRACE(name.toStdString());
    name_field(dialog)->setText(name);
    EXPECT_FALSE(accept_button(dialog)->isEnabled());
    dialog.accept();
    EXPECT_NE(dialog.result(), QDialog::Accepted);
    EXPECT_TRUE(dialog.selected_relative_path().isEmpty());
  }
  name_field(dialog)->setText(QStringLiteral("Root Material.QMAT"));
  dialog.accept();
  EXPECT_EQ(dialog.selected_relative_path(), QStringLiteral("Root Material.QMAT"));
}

TEST_F(QtProjectAssetDialogTests, SaveAsStartsWithExistingSceneFolderAndFilename) {
  QtProjectAssetDialog dialog(QtProjectAssetDialogMode::SaveScene,
                              project_.path(), snapshot_);
  dialog.set_initial_path(QStringLiteral("Maps/Main.qdr"));
  EXPECT_EQ(dialog.browser().selected_folder(), QStringLiteral("Maps"));
  ASSERT_NE(name_field(dialog), nullptr);
  EXPECT_EQ(name_field(dialog)->text(), QStringLiteral("Main.qdr"));
  name_field(dialog)->setText(QStringLiteral("Copy.qdr"));
  dialog.accept();
  EXPECT_EQ(dialog.selected_relative_path(), QStringLiteral("Maps/Copy.qdr"));
}

TEST_F(QtProjectAssetDialogTests, OpenRequiresExactlyOneCatalogSceneAndExistingFile) {
  QtProjectAssetDialog dialog(QtProjectAssetDialogMode::OpenScene,
                              project_.path(), snapshot_);
  ASSERT_NE(accept_button(dialog), nullptr);
  EXPECT_FALSE(accept_button(dialog)->isEnabled());
  dialog.set_initial_path(QStringLiteral("Maps/Main.qdr"));
  ASSERT_TRUE(accept_button(dialog)->isEnabled());
  dialog.accept();
  EXPECT_EQ(dialog.result(), QDialog::Accepted);
  EXPECT_EQ(dialog.selected_relative_path(), QStringLiteral("Maps/Main.qdr"));
}

TEST_F(QtProjectAssetDialogTests, MissingFileCannotBeOpenedFromStaleSnapshot) {
  QtProjectAssetDialog dialog(QtProjectAssetDialogMode::OpenScene,
                              project_.path(), snapshot_);
  dialog.set_initial_path(QStringLiteral("Maps/Main.qdr"));
  ASSERT_TRUE(QFile::remove(project_.filePath(QStringLiteral("Maps/Main.qdr"))));
  dialog.accept();
  EXPECT_NE(dialog.result(), QDialog::Accepted);
  EXPECT_TRUE(dialog.selected_relative_path().isEmpty());
}

TEST_F(QtProjectAssetDialogTests, FilenameCannotEscapeRootOrChangeAssetType) {
  QtProjectAssetDialog dialog(QtProjectAssetDialogMode::SaveScene,
                              project_.path(), snapshot_);
  ASSERT_NE(name_field(dialog), nullptr);
  ASSERT_NE(accept_button(dialog), nullptr);
  for (const QString &name : {QStringLiteral("../Outside.qdr"),
                              QStringLiteral("..\\Outside.qdr"),
                              QStringLiteral("/Outside.qdr"),
                              QStringLiteral("C:\\Outside.qdr"),
                              QStringLiteral("C:Outside.qdr"),
                              QStringLiteral("Image.png"),
                              QStringLiteral("Scene."),
                              QStringLiteral("  ")}) {
    SCOPED_TRACE(name.toStdString());
    name_field(dialog)->setText(name);
    EXPECT_FALSE(accept_button(dialog)->isEnabled());
    dialog.accept();
    EXPECT_NE(dialog.result(), QDialog::Accepted);
    EXPECT_TRUE(dialog.selected_relative_path().isEmpty());
  }
}

TEST_F(QtProjectAssetDialogTests, DeletedFolderDestinationsAndChildrenAreExcluded) {
  QtProjectAssetDialog dialog(QtProjectAssetDialogMode::SaveScene,
                              project_.path(), snapshot_);
  dialog.set_excluded_paths({QStringLiteral("maps/")});
  dialog.set_initial_path(QStringLiteral("Maps/Sub/Copy.qdr"));
  ASSERT_NE(accept_button(dialog), nullptr);
  EXPECT_FALSE(accept_button(dialog)->isEnabled());
  dialog.accept();
  EXPECT_TRUE(dialog.selected_relative_path().isEmpty());

  dialog.set_initial_path(QStringLiteral("MapsCopy.qdr"));
  EXPECT_TRUE(accept_button(dialog)->isEnabled());
  dialog.accept();
  EXPECT_EQ(dialog.selected_relative_path(), QStringLiteral("MapsCopy.qdr"));
}

TEST_F(QtProjectAssetDialogTests, DeletedSceneExclusionLeavesSiblingNamesAvailable) {
  QtProjectAssetDialog dialog(QtProjectAssetDialogMode::SaveScene,
                              project_.path(), snapshot_);
  dialog.set_excluded_paths({QStringLiteral("Maps/Main.qdr")});
  dialog.set_initial_path(QStringLiteral("Maps/Main.qdr"));
  ASSERT_NE(accept_button(dialog), nullptr);
  EXPECT_FALSE(accept_button(dialog)->isEnabled());
  name_field(dialog)->setText(QStringLiteral("Main Copy"));
  EXPECT_TRUE(accept_button(dialog)->isEnabled());
  dialog.accept();
  EXPECT_EQ(dialog.selected_relative_path(), QStringLiteral("Maps/Main Copy.qdr"));
}

TEST_F(QtProjectAssetDialogTests, ReplacingProjectRejectsPendingDialog) {
  for (const auto mode : {QtProjectAssetDialogMode::SaveScene,
                          QtProjectAssetDialogMode::SaveMaterial}) {
    SCOPED_TRACE(static_cast<int>(mode));
    QtProjectAssetDialog dialog(mode, project_.path(), snapshot_);
    dialog.set_initial_path(QStringLiteral("Pending"));
    ++snapshot_.generation;
    dialog.set_snapshot(snapshot_);
    EXPECT_EQ(dialog.result(), QDialog::Rejected);
    EXPECT_TRUE(dialog.selected_relative_path().isEmpty());
  }
}

TEST_F(QtProjectAssetDialogTests, CancelDoesNotPublishSelectedPathOrCreateFiles) {
  QtProjectAssetDialog dialog(QtProjectAssetDialogMode::SaveScene,
                              project_.path(), snapshot_);
  dialog.set_initial_path(QStringLiteral("Maps/Cancelled.qdr"));
  dialog.reject();
  EXPECT_EQ(dialog.result(), QDialog::Rejected);
  EXPECT_TRUE(dialog.selected_relative_path().isEmpty());
  EXPECT_FALSE(QFile::exists(project_.filePath(QStringLiteral("Maps/Cancelled.qdr"))));
}

TEST_F(QtProjectAssetDialogTests, FolderCreationDispatchesOwnerIntentOnly) {
  QtProjectAssetDialog dialog(QtProjectAssetDialogMode::SaveScene,
                              project_.path(), snapshot_);
  dialog.set_initial_path(QStringLiteral("Maps"));
  int calls = 0;
  dialog.set_create_folder_callback([&](const QtAssetBrowserActionRequest &request) {
    ++calls;
    EXPECT_EQ(request.action, QtAssetBrowserAction::CreateFolder);
    EXPECT_EQ(request.destination_folder, QStringLiteral("Maps"));
    EXPECT_EQ(request.generation, snapshot_.generation);
    EXPECT_EQ(request.dependency_revision, snapshot_.dependency_revision);
  });
  EXPECT_TRUE(dialog.browser().handle_action(QtAssetBrowserAction::CreateFolder));
  EXPECT_EQ(calls, 1);
  EXPECT_EQ(QDir(project_.filePath(QStringLiteral("Maps")))
                .entryList(QDir::Dirs | QDir::NoDotAndDotDot),
            QStringList{QStringLiteral("Sub")});
}

TEST_F(QtProjectAssetDialogTests, ScaleRoundtripUsesOriginalLayoutMetrics) {
  QtProjectAssetDialog dialog(QtProjectAssetDialogMode::SaveScene,
                              project_.path(), snapshot_);
  const QMargins initial = dialog.layout()->contentsMargins();
  const int spacing = dialog.layout()->spacing();
  dialog.apply_ui_scale(QtUiScaleMetrics(2.0F, 1));
  EXPECT_EQ(dialog.layout()->contentsMargins(), QMargins(24, 24, 24, 24));
  EXPECT_EQ(dialog.layout()->spacing(), spacing * 2);
  dialog.apply_ui_scale(QtUiScaleMetrics(1.0F, 2));
  EXPECT_EQ(dialog.layout()->contentsMargins(), initial);
  EXPECT_EQ(dialog.layout()->spacing(), spacing);
}

TEST_F(QtProjectAssetDialogTests, PhysicalDirectoryLinkCannotEscapeProjectOnSave) {
  QTemporaryDir outside;
  ASSERT_TRUE(outside.isValid());
  std::error_code error;
  std::filesystem::create_directory_symlink(
      std::filesystem::path(outside.path().toStdWString()),
      std::filesystem::path(project_.filePath(QStringLiteral("Linked")).toStdWString()),
      error);
  if (error) {
    GTEST_SKIP() << "Host cannot create test directory links: " << error.message();
  }
  snapshot_.items.push_back({.id = QStringLiteral("folder:Linked"),
                             .relative_path = QStringLiteral("Linked"),
                             .name = QStringLiteral("Linked"),
                             .kind = QtAssetBrowserKind::Folder});
  QtProjectAssetDialog dialog(QtProjectAssetDialogMode::SaveScene,
                              project_.path(), snapshot_);
  dialog.set_initial_path(QStringLiteral("Linked/Escape.qdr"));
  dialog.accept();
  EXPECT_NE(dialog.result(), QDialog::Accepted);
  EXPECT_TRUE(dialog.selected_relative_path().isEmpty());
  EXPECT_FALSE(QFile::exists(outside.filePath(QStringLiteral("Escape.qdr"))));
}

} // namespace
} // namespace quader
