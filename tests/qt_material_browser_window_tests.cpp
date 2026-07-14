////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "ui/qt_material_browser_window.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMetaObject>
#include <QSlider>
#include <QStackedWidget>
#include <QTableView>
#include <QToolButton>

#include <vector>

namespace quader {
namespace {

TEST(QtMaterialBrowserWindowTests,
     PresentsFiltersActivatesAndRetainsOnlyLayoutPreferences) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-material-browser-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));
  application.setQuitOnLastWindowClosed(false);

  QWidget owner;
  auto *window = new QtMaterialBrowserWindow(&owner);
  window->setAttribute(Qt::WA_DontShowOnScreen, true);
  EXPECT_EQ(window->windowType(), Qt::Tool);
  EXPECT_FALSE(window->testAttribute(Qt::WA_DeleteOnClose));

  window->set_labels(QtMaterialBrowserLabels{
      .window_title = QStringLiteral("Materials"),
      .search_placeholder = QStringLiteral("Find material"),
      .grid = QStringLiteral("Tiles"),
      .list = QStringLiteral("Details"),
      .tile_size = QStringLiteral("Tile size"),
      .name_column = QStringLiteral("Material name"),
      .type_column = QStringLiteral("Kind"),
      .size_column = QStringLiteral("File size"),
      .material_type = QStringLiteral("Material asset"),
      .empty_catalog = QStringLiteral("Empty catalog"),
      .no_results = QStringLiteral("No matches"),
  });
  EXPECT_EQ(window->windowTitle(), QStringLiteral("Materials"));

  const std::vector<QtMaterialBrowserItem> materials{
      {
          .stable_id = QStringLiteral("bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb"),
          .name = QStringLiteral("Zinc"),
          .project_relative_path =
              QStringLiteral("Materials/Metals/zinc.qmat"),
          .type_display_name = QStringLiteral("Standard Material"),
          .qmat_size_bytes = 2048,
          .fallback_color = QColor(120, 125, 130),
          .content_revision = 3,
      },
      {
          .stable_id = QStringLiteral("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa"),
          .name = QStringLiteral("Default Material"),
          .project_relative_path =
              QStringLiteral("Materials/default_material.qmat"),
          .qmat_size_bytes = 512,
          .fallback_color = QColor(240, 240, 240),
          .content_revision = 1,
      },
  };
  window->set_materials(materials,
                        QStringLiteral("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa"));

  auto *grid = window->findChild<QListView *>(
      QStringLiteral("materialBrowser.gridView"));
  auto *list = window->findChild<QTableView *>(
      QStringLiteral("materialBrowser.listView"));
  auto *search = window->findChild<QLineEdit *>(
      QStringLiteral("materialBrowser.search"));
  auto *stack = window->findChild<QStackedWidget *>(
      QStringLiteral("materialBrowser.content"));
  auto *empty = window->findChild<QLabel *>(
      QStringLiteral("materialBrowser.empty"));
  auto *grid_button = window->findChild<QToolButton *>(
      QStringLiteral("materialBrowser.grid"));
  auto *list_button = window->findChild<QToolButton *>(
      QStringLiteral("materialBrowser.list"));
  auto *slider = window->findChild<QSlider *>(
      QStringLiteral("materialBrowser.tileSize"));
  ASSERT_NE(grid, nullptr);
  ASSERT_NE(list, nullptr);
  ASSERT_NE(search, nullptr);
  ASSERT_NE(stack, nullptr);
  ASSERT_NE(empty, nullptr);
  ASSERT_NE(grid_button, nullptr);
  ASSERT_NE(list_button, nullptr);
  ASSERT_NE(slider, nullptr);

  ASSERT_EQ(grid->model(), list->model());
  ASSERT_EQ(grid->selectionModel(), list->selectionModel());
  ASSERT_EQ(grid->model()->rowCount(), 2);
  EXPECT_EQ(grid->model()->index(0, 0).data().toString(),
            QStringLiteral("Default Material"));
  EXPECT_EQ(grid->currentIndex()
                .data(QtMaterialBrowserModel::StableIdRole)
                .toString(),
            QStringLiteral("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa"));
  EXPECT_FALSE(grid->model()
                   ->index(0, 0)
                   .data(Qt::DecorationRole)
                   .value<QIcon>()
                   .isNull());
  EXPECT_EQ(grid->model()->index(0, 1).data().toString(),
            QStringLiteral("Material asset"));
  EXPECT_EQ(grid->model()->index(1, 1).data().toString(),
            QStringLiteral("Standard Material"));
  EXPECT_EQ(grid->model()
                ->index(0, 0)
                .data(QtMaterialBrowserModel::SizeBytesRole)
                .toLongLong(),
            512);
  list->sortByColumn(QtMaterialBrowserModel::SizeColumn,
                     Qt::DescendingOrder);
  EXPECT_EQ(list->model()->index(0, 0).data().toString(),
            QStringLiteral("Zinc"))
      << "the Size column must sort using QMAT bytes, not display text";
  list->sortByColumn(QtMaterialBrowserModel::NameColumn,
                     Qt::AscendingOrder);

  QString activated;
  window->set_activate_material_callback(
      [&activated](QString stable_id) { activated = std::move(stable_id); });
  const QModelIndex zinc = grid->model()->index(1, 0);
  ASSERT_TRUE(QMetaObject::invokeMethod(
      grid, "clicked", Qt::DirectConnection, Q_ARG(QModelIndex, zinc)));
  EXPECT_EQ(activated,
            QStringLiteral("bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb"));

  QString opened;
  window->set_open_material_callback(
      [&opened](QString stable_id) { opened = std::move(stable_id); });
  ASSERT_TRUE(QMetaObject::invokeMethod(
      grid, "doubleClicked", Qt::DirectConnection,
      Q_ARG(QModelIndex, zinc)));
  EXPECT_EQ(opened,
            QStringLiteral("bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb"));

  search->setText(QStringLiteral("metals/"));
  EXPECT_EQ(grid->model()->rowCount(), 1);
  EXPECT_EQ(grid->model()->index(0, 0).data().toString(),
            QStringLiteral("Zinc"));
  search->setText(QStringLiteral("missing"));
  EXPECT_EQ(grid->model()->rowCount(), 0);
  EXPECT_EQ(stack->currentWidget(), empty);
  EXPECT_EQ(empty->text(), QStringLiteral("No matches"));

  int state_changes = 0;
  window->set_presentation_state_changed_callback(
      [&state_changes]() { ++state_changes; });
  list_button->click();
  EXPECT_EQ(window->view_mode(), QtMaterialBrowserViewMode::List);
  EXPECT_TRUE(slider->isHidden());
  grid_button->click();
  EXPECT_FALSE(slider->isHidden());
  slider->setValue(192);
  EXPECT_EQ(window->view_mode(), QtMaterialBrowserViewMode::Grid);
  EXPECT_EQ(window->tile_size(), 192);
  EXPECT_EQ(grid->iconSize(), QSize(192, 192));
  EXPECT_EQ(state_changes, 3);

  window->restore_presentation_state(QtMaterialBrowserViewMode::List, 999);
  EXPECT_EQ(window->view_mode(), QtMaterialBrowserViewMode::List);
  EXPECT_EQ(window->tile_size(), 256);
  EXPECT_EQ(state_changes, 3)
      << "restoring preferences must not write them back";

  search->setText(QStringLiteral("zinc"));
  window->show();
  QCoreApplication::processEvents();
  EXPECT_TRUE(search->text().isEmpty())
      << "search is transient and resets whenever the browser reopens";
  EXPECT_TRUE(window->close());
  EXPECT_FALSE(window->isVisible());
  EXPECT_FALSE(window->testAttribute(Qt::WA_DeleteOnClose));

  window->clear_materials();
  EXPECT_EQ(stack->currentWidget(), empty);
  EXPECT_EQ(empty->text(), QStringLiteral("Empty catalog"));
}

} // namespace
} // namespace quader
