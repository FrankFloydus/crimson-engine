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
#include <QPalette>
#include <QPixmap>
#include <QProxyStyle>
#include <QPushButton>
#include <QScrollBar>
#include <QSlider>
#include <QStackedWidget>
#include <QTableView>
#include <QToolButton>

#include <optional>
#include <string_view>
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

QByteArray encoded_high_resolution_preview() {
  // 3072 backing pixels cover the largest 512px tile at 300% UI scale on a
  // 2x-DPR display without making this focused test retain a square 36MiB
  // raster.
  QImage image(3072, 192, QImage::Format_RGBA8888);
  image.fill(Qt::black);
  for (int y = 0; y < image.height(); ++y) {
    image.setPixelColor(1537, y, Qt::red);
  }
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

ProjectAssetId required_project_id(std::string_view text) {
  const std::optional<ProjectAssetId> parsed = ProjectAssetId::parse(text);
  EXPECT_TRUE(parsed.has_value());
  return parsed.value_or(ProjectAssetId{});
}

class PaletteBrowserIconStyle final : public QProxyStyle {
public:
  PaletteBrowserIconStyle() : QProxyStyle(QStringLiteral("fusion")) {}

  QIcon standardIcon(StandardPixmap standard_pixmap,
                     const QStyleOption *option,
                     const QWidget *widget) const override {
    if (standard_pixmap == SP_FileDialogListView ||
        standard_pixmap == SP_FileDialogDetailedView) {
      const QPalette palette =
          widget != nullptr ? widget->palette() : QApplication::palette();
      QPixmap pixmap(16, 16);
      pixmap.fill(palette.color(QPalette::ButtonText));
      return QIcon(pixmap);
    }
    return QProxyStyle::standardIcon(standard_pixmap, option, widget);
  }
};

QColor icon_center_color(const QIcon &icon) {
  return icon.pixmap(QSize(16, 16)).toImage().pixelColor(8, 8);
}

TEST(QtMaterialBrowserWindowTests,
     RefreshesOnlyRetainedViewModeIconsOnPaletteChanges) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-material-browser-palette-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  application.setStyle(new PaletteBrowserIconStyle());

  QPalette palette = QApplication::palette();
  const QColor initial_color(211, 37, 53);
  palette.setColor(QPalette::ButtonText, initial_color);
  QApplication::setPalette(palette);

  QtMaterialBrowserWindow window;
  auto *grid = window.findChild<QToolButton *>(
      QStringLiteral("materialBrowser.grid"));
  auto *list = window.findChild<QToolButton *>(
      QStringLiteral("materialBrowser.list"));
  auto *tile_size = window.findChild<QSlider *>(
      QStringLiteral("materialBrowser.tileSize"));
  ASSERT_NE(grid, nullptr);
  ASSERT_NE(list, nullptr);
  ASSERT_NE(tile_size, nullptr);
  const qint64 initial_grid_key = grid->icon().cacheKey();
  const qint64 initial_list_key = list->icon().cacheKey();
  const int retained_tile_size = tile_size->value();
  EXPECT_EQ(icon_center_color(grid->icon()), initial_color);
  EXPECT_EQ(icon_center_color(list->icon()), initial_color);

  const QColor application_color(29, 197, 83);
  palette.setColor(QPalette::ButtonText, application_color);
  QApplication::setPalette(palette);
  QCoreApplication::processEvents();
  const qint64 application_grid_key = grid->icon().cacheKey();
  const qint64 application_list_key = list->icon().cacheKey();
  EXPECT_NE(application_grid_key, initial_grid_key);
  EXPECT_NE(application_list_key, initial_list_key);
  EXPECT_EQ(icon_center_color(grid->icon()), application_color);
  EXPECT_EQ(icon_center_color(list->icon()), application_color);
  EXPECT_EQ(tile_size->value(), retained_tile_size);

  const QColor local_color(37, 89, 223);
  palette.setColor(QPalette::ButtonText, local_color);
  window.setPalette(palette);
  QCoreApplication::processEvents();
  EXPECT_NE(grid->icon().cacheKey(), application_grid_key);
  EXPECT_NE(list->icon().cacheKey(), application_list_key);
  EXPECT_EQ(icon_center_color(grid->icon()), local_color);
  EXPECT_EQ(icon_center_color(list->icon()), local_color);
  EXPECT_EQ(tile_size->value(), retained_tile_size);
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
      .accept = QStringLiteral("Choose"),
      .picker_window_title = QStringLiteral("Choose replacement"),
  });
  EXPECT_EQ(window->windowTitle(), QStringLiteral("Materials"));

  const QuaderMaterialReference zinc_reference =
      QuaderMaterialReference::project(required_project_id(
          "bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb"));
  const QuaderMaterialReference default_reference =
      QuaderMaterialReference::project(required_project_id(
          "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa"));
  std::vector<QtMaterialBrowserItem> materials{
      {
          .reference = zinc_reference,
          .stable_id = qt_material_reference_stable_id(zinc_reference),
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
          .reference = default_reference,
          .stable_id = qt_material_reference_stable_id(default_reference),
          .name = QStringLiteral("Default Material"),
          .project_relative_path =
              QStringLiteral("Materials/default_material.qmat"),
          .qmat_size_bytes = 512,
          .encoded_albedo = encoded_wide_preview(),
          .fallback_color = QColor(240, 240, 240),
          .content_revision = 1,
      },
  };
  for (QtMaterialBrowserItem &material : materials) {
    material.thumbnail = make_qt_material_thumbnail(material);
  }
  window->set_materials(materials, default_reference);

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
  auto *accept_button = window->findChild<QPushButton *>(
      QStringLiteral("materialBrowser.accept"));
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
  ASSERT_NE(accept_button, nullptr);
  EXPECT_EQ(file_menu->title(), QStringLiteral("Project File"));
  EXPECT_EQ(import_action->text(), QStringLiteral("Bring textures in…"));
  EXPECT_EQ(accept_button->text(), QStringLiteral("Choose"));
  EXPECT_EQ(accept_button->accessibleName(), QStringLiteral("Choose"));
  EXPECT_TRUE(accept_button->isEnabled());

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
            qt_material_reference_stable_id(default_reference));
  const QIcon preview = grid->model()
                            ->index(0, 0)
                            .data(Qt::DecorationRole)
                            .value<QIcon>();
  EXPECT_FALSE(preview.isNull());
  EXPECT_EQ(preview.actualSize(QSize(128, 128)), QSize(128, 64));
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

  QuaderMaterialReference activated;
  int activation_count = 0;
  window->set_activate_material_callback(
      [&activated, &activation_count](QuaderMaterialReference reference) {
        activated = reference;
        ++activation_count;
      });
  const QModelIndex zinc = grid->model()->index(1, 0);
  ASSERT_TRUE(QMetaObject::invokeMethod(
      grid, "clicked", Qt::DirectConnection, Q_ARG(QModelIndex, zinc)));
  EXPECT_EQ(activated, zinc_reference);
  EXPECT_EQ(activation_count, 1);
  ASSERT_TRUE(QMetaObject::invokeMethod(
      grid, "activated", Qt::DirectConnection, Q_ARG(QModelIndex, zinc)));
  EXPECT_EQ(activation_count, 1)
      << "a platform mouse activation following clicked must not dispatch "
         "the material intent twice";

  activated = {};
  grid->selectionModel()->setCurrentIndex(
      zinc, QItemSelectionModel::ClearAndSelect);
  send_key(*grid, Qt::Key_Return);
  EXPECT_EQ(activated, zinc_reference)
      << "keyboard activation must dispatch the same intent as a click";
  EXPECT_EQ(activation_count, 2);

  activated = {};
  list->selectionModel()->setCurrentIndex(
      zinc, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
  send_key(*list, Qt::Key_Return);
  EXPECT_EQ(activated, zinc_reference)
      << "both material views must honor QAbstractItemView::activated";
  EXPECT_EQ(activation_count, 3);

  QuaderMaterialReference opened;
  window->set_open_material_callback(
      [&opened](QuaderMaterialReference reference) { opened = reference; });
  ASSERT_TRUE(QMetaObject::invokeMethod(
      grid, "doubleClicked", Qt::DirectConnection,
      Q_ARG(QModelIndex, zinc)));
  EXPECT_EQ(opened, zinc_reference);

  search->setText(QStringLiteral("metals/"));
  EXPECT_EQ(grid->model()->rowCount(), 1);
  EXPECT_EQ(grid->model()->index(0, 0).data().toString(),
            QStringLiteral("Zinc"));
  search->setText(QStringLiteral("missing"));
  EXPECT_EQ(grid->model()->rowCount(), 0);
  EXPECT_EQ(stack->currentWidget(), empty);
  EXPECT_EQ(empty->text(), QStringLiteral("No matches"));
  search->clear();
  ASSERT_GT(grid->model()->rowCount(), 0);

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
    const QIcon current_preview = grid->model()
                                      ->index(0, 0)
                                      .data(Qt::DecorationRole)
                                      .value<QIcon>();
    EXPECT_EQ(current_preview.actualSize(QSize(preset, preset)),
              QSize(preset, preset / 2));
    EXPECT_EQ(slider->toolTip(),
              QStringLiteral("Tile size = %1 pixels").arg(preset));
    EXPECT_EQ(slider->accessibleName(), slider->toolTip());
  }

  window->restore_presentation_state(QtMaterialBrowserViewMode::Grid, 128);
  const QSize unscaled_window_size = window->size();
  const int changes_before_scale = state_changes;
  window->apply_ui_scale(QtUiScaleMetrics(2.0F, 1U));
  EXPECT_EQ(window->tile_size(), 128)
      << "interface scaling must not rewrite the authored tile preference";
  EXPECT_EQ(grid->iconSize(), QSize(256, 256));
  EXPECT_EQ(grid->gridSize(), QSize(288, 356));
  EXPECT_EQ(grid->spacing(), 16);
  EXPECT_EQ(slider->minimumWidth(), 300);
  EXPECT_EQ(slider->maximumWidth(), 300);
  EXPECT_EQ(window->size(), unscaled_window_size)
      << "a live scale event must preserve retained top-level geometry";
  EXPECT_EQ(state_changes, changes_before_scale)
      << "presentation scaling is not a material-browser preference edit";
  window->apply_ui_scale(QtUiScaleMetrics(2.0F, 1U));
  EXPECT_EQ(grid->gridSize(), QSize(288, 356))
      << "reapplying one snapshot must never compound dimensions";
  window->apply_ui_scale(QtUiScaleMetrics(1.0F, 2U));
  EXPECT_EQ(grid->iconSize(), QSize(128, 128));
  EXPECT_EQ(grid->gridSize(), QSize(144, 178));
  EXPECT_EQ(slider->minimumWidth(), 150);
  int dpi_decoration_changes = 0;
  QObject::connect(
      grid->model(), &QAbstractItemModel::dataChanged, window,
      [&dpi_decoration_changes](const QModelIndex &, const QModelIndex &,
                                const QList<int> &roles) {
        if (roles.contains(Qt::DecorationRole)) {
          ++dpi_decoration_changes;
        }
      });
  QEvent dpi_event(QEvent::DevicePixelRatioChange);
  QCoreApplication::sendEvent(window, &dpi_event);
  EXPECT_EQ(dpi_decoration_changes, 1)
      << "a live DPR event must discard and republish retained icons";

  QtMaterialBrowserItem high_resolution_material{
      .reference = QuaderMaterialReference::project(required_project_id(
          "cccccccc-cccc-cccc-cccc-cccccccccccc")),
      .name = QStringLiteral("Detailed"),
      .encoded_albedo = encoded_high_resolution_preview(),
      .content_revision = 1,
  };
  high_resolution_material.stable_id = qt_material_reference_stable_id(
      high_resolution_material.reference);
  high_resolution_material.thumbnail =
      make_qt_material_thumbnail(high_resolution_material);
  ASSERT_EQ(high_resolution_material.thumbnail->image.size(),
            QSize(512, 32));

  const QPixmap direct_preview = qt_material_preview_pixmap(
      high_resolution_material, QSize(1536, 1536), 2.0);
  EXPECT_EQ(direct_preview.size(), QSize(3072, 192));
  EXPECT_EQ(direct_preview.devicePixelRatio(), 2.0);
  EXPECT_EQ(direct_preview.toImage().pixelColor(1537, 100), Qt::red)
      << "a large-DPR preview must decode the source instead of enlarging "
         "the retained 512px thumbnail";

  QtMaterialBrowserModel high_resolution_model;
  ASSERT_TRUE(high_resolution_model.set_items({high_resolution_material}));
  EXPECT_TRUE(high_resolution_model.set_preview_raster_context(
      QSize(1536, 1536), 2.0));
  EXPECT_FALSE(high_resolution_model.set_preview_raster_context(
      QSize(1536, 1536), 2.0));
  const QIcon high_resolution_icon =
      high_resolution_model.index(0, 0)
          .data(Qt::DecorationRole)
          .value<QIcon>();
  const QPixmap high_resolution_icon_pixmap =
      high_resolution_icon.pixmap(QSize(1536, 1536), 2.0);
  EXPECT_EQ(high_resolution_icon_pixmap.size(), QSize(3072, 192));
  EXPECT_EQ(high_resolution_icon_pixmap.toImage().pixelColor(1537, 100),
            Qt::red);

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
  window->set_active_material(zinc_reference);
  EXPECT_EQ(window->selected_material(), zinc_reference);
  EXPECT_TRUE(accept_button->isEnabled());
  accept_button->click();
  EXPECT_EQ(activation_count, 4)
      << "normal Accept must reaffirm the live active selection";
  EXPECT_EQ(activated, zinc_reference);
  EXPECT_FALSE(window->isVisible());

  window->show();
  QCoreApplication::processEvents();
  EXPECT_TRUE(window->close());
  EXPECT_FALSE(window->isVisible());
  EXPECT_EQ(activation_count, 4)
      << "closing with X must not roll back or redispatch live activation";
  EXPECT_FALSE(window->testAttribute(Qt::WA_DeleteOnClose));

  window->clear_materials();
  EXPECT_EQ(stack->currentWidget(), empty);
  EXPECT_EQ(empty->text(), QStringLiteral("Empty catalog"));

  auto *picker = new QtMaterialBrowserWindow(&owner);
  picker->setAttribute(Qt::WA_DontShowOnScreen, true);
  picker->set_purpose(QtMaterialBrowserPurpose::Picker);
  picker->set_materials(materials);
  EXPECT_EQ(picker->purpose(), QtMaterialBrowserPurpose::Picker);
  EXPECT_EQ(picker->windowTitle(), QStringLiteral("Replace Material Browser"));
  auto *picker_grid = picker->findChild<QListView *>(
      QStringLiteral("materialBrowser.gridView"));
  auto *picker_accept = picker->findChild<QPushButton *>(
      QStringLiteral("materialBrowser.accept"));
  auto *picker_file_menu = picker->findChild<QMenu *>(
      QStringLiteral("materialBrowser.fileMenu"));
  auto *picker_search = picker->findChild<QLineEdit *>(
      QStringLiteral("materialBrowser.search"));
  ASSERT_NE(picker_grid, nullptr);
  ASSERT_NE(picker_accept, nullptr);
  ASSERT_NE(picker_file_menu, nullptr);
  ASSERT_NE(picker_search, nullptr);
  EXPECT_FALSE(picker_file_menu->menuAction()->isVisible());
  EXPECT_FALSE(picker_accept->isEnabled());

  int picker_activation_count = 0;
  int picker_open_count = 0;
  int picker_accept_count = 0;
  QuaderMaterialReference picked;
  picker->set_activate_material_callback(
      [&picker_activation_count](QuaderMaterialReference) {
        ++picker_activation_count;
      });
  picker->set_open_material_callback(
      [&picker_open_count](QuaderMaterialReference) {
        ++picker_open_count;
      });
  picker->set_accept_material_callback(
      [&picker_accept_count, &picked](QuaderMaterialReference reference) {
        ++picker_accept_count;
        picked = reference;
      });

  const QModelIndex picker_zinc = picker_grid->model()->index(1, 0);
  picker_grid->selectionModel()->setCurrentIndex(
      picker_zinc, QItemSelectionModel::ClearAndSelect);
  ASSERT_TRUE(QMetaObject::invokeMethod(
      picker_grid, "clicked", Qt::DirectConnection,
      Q_ARG(QModelIndex, picker_zinc)));
  send_key(*picker_grid, Qt::Key_Return);
  ASSERT_TRUE(QMetaObject::invokeMethod(
      picker_grid, "doubleClicked", Qt::DirectConnection,
      Q_ARG(QModelIndex, picker_zinc)));
  EXPECT_EQ(picker_activation_count, 0);
  EXPECT_EQ(picker_open_count, 0);
  EXPECT_EQ(picker_accept_count, 0)
      << "picker clicks, Enter, and double-click must remain isolated";
  EXPECT_TRUE(picker_accept->isEnabled());

  picker->set_materials({materials[1]}, zinc_reference);
  EXPECT_FALSE(picker->selected_material().valid());
  EXPECT_FALSE(picker_accept->isEnabled())
      << "rebinding after a candidate is deleted must clear stale picker "
         "selection";
  picker->set_materials(materials, zinc_reference);
  EXPECT_EQ(picker->selected_material(), zinc_reference);
  EXPECT_TRUE(picker_accept->isEnabled());

  picker_search->setText(QStringLiteral("default"));
  EXPECT_EQ(picker_grid->model()->rowCount(), 1);
  picker->set_materials(materials, zinc_reference);
  EXPECT_TRUE(picker_search->text().isEmpty())
      << "a hidden picker rebind must clear the previous session's filter "
         "before restoring its requested material";
  EXPECT_EQ(picker->selected_material(), zinc_reference);
  EXPECT_TRUE(picker_accept->isEnabled());

  picker->show();
  QCoreApplication::processEvents();
  picker_accept->click();
  EXPECT_EQ(picker_accept_count, 1);
  EXPECT_EQ(picked, zinc_reference);
  EXPECT_FALSE(picker->isVisible());
}

} // namespace
} // namespace quader
