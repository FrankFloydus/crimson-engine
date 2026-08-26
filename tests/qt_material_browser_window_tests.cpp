////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "ui/qt_material_browser_window.hpp"

#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QCoreApplication>
#include <QImage>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMainWindow>
#include <QMetaObject>
#include <QMenu>
#include <QScrollBar>
#include <QSlider>
#include <QStackedWidget>
#include <QTableView>
#include <QToolButton>

#include <vector>

namespace quader {
namespace {

void send_key(QWidget &target, int key) {
  QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
  QCoreApplication::sendEvent(&target, &press);
  QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
  QCoreApplication::sendEvent(&target, &release);
  QCoreApplication::processEvents();
}

QByteArray encoded_wide_preview() {
  QImage image(2, 1, QImage::Format_RGBA8888);
  image.fill(QColor(40, 120, 200));
  QByteArray encoded;
  QBuffer buffer(&encoded);
  EXPECT_TRUE(buffer.open(QIODevice::WriteOnly));
  EXPECT_TRUE(image.save(&buffer, "PNG"));
  return encoded;
}

QByteArray encoded_tga_preview() {
  return QByteArray::fromBase64(
      "AAACAAAAAAAAAAAAAgACABgAv18fv18fv18fv18fAAAAAAAAAABUUlVFVklTSU9O"
      "LVhGSUxFLgA=",
      QByteArray::AbortOnBase64DecodingErrors);
}

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
  EXPECT_NE(qobject_cast<QMainWindow *>(window), nullptr);
  EXPECT_EQ(window->windowType(), Qt::Tool);
  EXPECT_FALSE(window->testAttribute(Qt::WA_DeleteOnClose));
  EXPECT_FALSE(window->windowFlags().testFlag(Qt::WindowMaximizeButtonHint));

  window->set_labels(QtMaterialBrowserLabels{
      .window_title = QStringLiteral("Materials"),
      .search_placeholder = QStringLiteral("Find material"),
      .grid = QStringLiteral("Tiles"),
      .list = QStringLiteral("Details"),
      .tile_size = QStringLiteral("Tile size"),
      .tile_size_value = QStringLiteral("Tile size = %1 pixels"),
      .file = QStringLiteral("Project File"),
      .import_textures = QStringLiteral("Bring textures in…"),
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
          .encoded_albedo = encoded_tga_preview(),
          .fallback_color = QColor(120, 125, 130),
          .content_revision = 3,
      },
      {
          .stable_id = QStringLiteral("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa"),
          .name = QStringLiteral("Default Material"),
          .project_relative_path =
              QStringLiteral("Materials/default_material.qmat"),
          .qmat_size_bytes = 512,
          .encoded_albedo = encoded_wide_preview(),
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
  auto *file_menu = window->findChild<QMenu *>(
      QStringLiteral("materialBrowser.fileMenu"));
  auto *import_action = window->findChild<QAction *>(
      QStringLiteral("materialBrowser.file.import"));
  ASSERT_NE(grid, nullptr);
  ASSERT_NE(list, nullptr);
  ASSERT_NE(search, nullptr);
  ASSERT_NE(stack, nullptr);
  ASSERT_NE(empty, nullptr);
  ASSERT_NE(grid_button, nullptr);
  ASSERT_NE(list_button, nullptr);
  ASSERT_NE(slider, nullptr);
  ASSERT_NE(file_menu, nullptr);
  ASSERT_NE(import_action, nullptr);
  EXPECT_EQ(file_menu->title(), QStringLiteral("Project File"));
  EXPECT_EQ(import_action->text(), QStringLiteral("Bring textures in…"));

  int import_count = 0;
  window->set_import_textures_callback(
      [&import_count]() { ++import_count; });
  import_action->trigger();
  EXPECT_EQ(import_count, 1);

  EXPECT_EQ(slider->minimum(), 0);
  EXPECT_EQ(slider->maximum(), 5);
  EXPECT_EQ(slider->singleStep(), 1);
  EXPECT_EQ(slider->pageStep(), 1);
  EXPECT_EQ(slider->tickInterval(), 1);
  EXPECT_EQ(slider->tickPosition(), QSlider::TicksBelow);
  EXPECT_EQ(slider->toolTip(), QStringLiteral("Tile size = 128 pixels"));
  EXPECT_EQ(slider->accessibleName(), QStringLiteral("Tile size = 128 pixels"));

  QListView stock_grid;
  QTableView stock_list;
  EXPECT_EQ(grid->selectionMode(), QAbstractItemView::SingleSelection);
  EXPECT_EQ(list->selectionMode(), QAbstractItemView::SingleSelection);
  EXPECT_EQ(grid->editTriggers(), stock_grid.editTriggers());
  EXPECT_EQ(list->editTriggers(), stock_list.editTriggers());
  ASSERT_EQ(grid->model(), list->model());
  ASSERT_EQ(grid->selectionModel(), list->selectionModel());
  ASSERT_EQ(grid->model()->rowCount(), 2);
  EXPECT_EQ(grid->model()->index(0, 0).data().toString(),
            QStringLiteral("Default Material"));
  EXPECT_EQ(grid->currentIndex()
                .data(QtMaterialBrowserModel::StableIdRole)
                .toString(),
            QStringLiteral("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa"));
  const QIcon preview = grid->model()
                            ->index(0, 0)
                            .data(Qt::DecorationRole)
                            .value<QIcon>();
  EXPECT_FALSE(preview.isNull());
  for (const int preset : {16, 32, 64, 128, 256, 512}) {
    EXPECT_EQ(preview.actualSize(QSize(preset, preset)),
              QSize(preset, preset / 2));
  }
  const QIcon tga_preview = grid->model()
                                ->index(1, 0)
                                .data(Qt::DecorationRole)
                                .value<QIcon>();
  ASSERT_FALSE(tga_preview.isNull());
  const QColor tga_color =
      tga_preview.pixmap(QSize(16, 16)).toImage().pixelColor(8, 8);
  EXPECT_LT(tga_color.red(), 80)
      << "the TGA preview must not fall back to the material swatch";
  EXPECT_GT(tga_color.blue(), 150);
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
  int activation_count = 0;
  window->set_activate_material_callback(
      [&activated, &activation_count](QString stable_id) {
        activated = std::move(stable_id);
        ++activation_count;
      });
  const QModelIndex zinc = grid->model()->index(1, 0);
  ASSERT_TRUE(QMetaObject::invokeMethod(
      grid, "clicked", Qt::DirectConnection, Q_ARG(QModelIndex, zinc)));
  EXPECT_EQ(activated,
            QStringLiteral("bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb"));
  EXPECT_EQ(activation_count, 1);
  ASSERT_TRUE(QMetaObject::invokeMethod(
      grid, "activated", Qt::DirectConnection, Q_ARG(QModelIndex, zinc)));
  EXPECT_EQ(activation_count, 1)
      << "a platform mouse activation following clicked must not dispatch "
         "the material intent twice";

  activated.clear();
  grid->selectionModel()->setCurrentIndex(
      zinc, QItemSelectionModel::ClearAndSelect);
  send_key(*grid, Qt::Key_Return);
  EXPECT_EQ(activated,
            QStringLiteral("bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb"))
      << "keyboard activation must dispatch the same intent as a click";
  EXPECT_EQ(activation_count, 2);

  activated.clear();
  list->selectionModel()->setCurrentIndex(
      zinc, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
  send_key(*list, Qt::Key_Return);
  EXPECT_EQ(activated,
            QStringLiteral("bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb"))
      << "both material views must honor QAbstractItemView::activated";
  EXPECT_EQ(activation_count, 3);

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
  const std::vector<int> tile_presets{16, 32, 64, 128, 256, 512};
  for (int index = 0; index < static_cast<int>(tile_presets.size()); ++index) {
    slider->setValue(index);
    const int preset = tile_presets[static_cast<std::size_t>(index)];
    EXPECT_EQ(window->view_mode(), QtMaterialBrowserViewMode::Grid);
    EXPECT_EQ(window->tile_size(), preset);
    EXPECT_EQ(grid->iconSize(), QSize(preset, preset));
    EXPECT_EQ(grid->gridSize(), QSize(preset + 16, preset + 50));
    EXPECT_EQ(slider->toolTip(),
              QStringLiteral("Tile size = %1 pixels").arg(preset));
    EXPECT_EQ(slider->accessibleName(), slider->toolTip());
  }

  const int changes_before_restore = state_changes;
  window->restore_presentation_state(QtMaterialBrowserViewMode::Grid, 24);
  EXPECT_EQ(window->tile_size(), 16)
      << "an exact legacy tie must resolve to the smaller preset";
  window->restore_presentation_state(QtMaterialBrowserViewMode::Grid, 25);
  EXPECT_EQ(window->tile_size(), 32);
  window->restore_presentation_state(QtMaterialBrowserViewMode::Grid, 192);
  EXPECT_EQ(window->tile_size(), 128)
      << "an exact legacy tie must resolve to the smaller preset";
  window->restore_presentation_state(QtMaterialBrowserViewMode::List, 999);
  EXPECT_EQ(window->view_mode(), QtMaterialBrowserViewMode::List);
  EXPECT_EQ(window->tile_size(), 512);
  EXPECT_EQ(state_changes, changes_before_restore)
      << "restoring preferences must not write them back";

  window->restore_presentation_state(QtMaterialBrowserViewMode::Grid, 512);
  window->resize(440, 520);
  search->setText(QStringLiteral("zinc"));
  window->show();
  QCoreApplication::processEvents();
  EXPECT_TRUE(search->text().isEmpty())
      << "search is transient and resets whenever the browser reopens";
  EXPECT_EQ(stack->currentWidget(), grid);
  EXPECT_EQ(grid->model()->rowCount(), 2)
      << "the largest tile preset must retain the complete catalog";
  EXPECT_GT(grid->verticalScrollBar()->maximum(), 0)
      << "large tiles must remain reachable through vertical scrolling";
  EXPECT_GT(grid->horizontalScrollBar()->maximum(), 0)
      << "a narrow restored window must not clip a 512px tile permanently";
  EXPECT_TRUE(window->close());
  EXPECT_FALSE(window->isVisible());
  EXPECT_FALSE(window->testAttribute(Qt::WA_DeleteOnClose));

  window->clear_materials();
  EXPECT_EQ(stack->currentWidget(), empty);
  EXPECT_EQ(empty->text(), QStringLiteral("Empty catalog"));
}

} // namespace
} // namespace quader
