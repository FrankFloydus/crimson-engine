////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "ui/qt_material_browser_window.hpp"
#include "ui/qt_application_theme.hpp"

#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QDataStream>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFrame>
#include <QImage>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMainWindow>
#include <QMetaObject>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPalette>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QProxyStyle>
#include <QPushButton>
#include <QScrollBar>
#include <QSlider>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyleOption>
#include <QTableView>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
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

QTreeWidgetItem *folder_item(QTreeWidget &tree, const QString &path) {
  for (QTreeWidgetItemIterator it(&tree); *it != nullptr; ++it) {
    if ((*it)->data(0, Qt::UserRole).toString() == path) {
      return *it;
    }
  }
  return nullptr;
}

QtMaterialBrowserItem folder_material(std::string_view id, const QString &name,
                                       const QString &path) {
  QtMaterialBrowserItem material;
  material.reference = QuaderMaterialReference::project(required_project_id(id));
  material.stable_id = qt_material_reference_stable_id(material.reference);
  material.name = name;
  material.project_relative_path = path;
  return material;
}

bool drop_on_folder(QTreeWidget &tree, QTreeWidgetItem &target,
                    const QMimeData &mime) {
  tree.scrollToItem(&target);
  QCoreApplication::processEvents();
  const QPoint position = tree.visualItemRect(&target).center();
  QDragEnterEvent enter(position, Qt::MoveAction, &mime, Qt::LeftButton,
                        Qt::NoModifier);
  QCoreApplication::sendEvent(tree.viewport(), &enter);
  QDropEvent drop(QPointF(position), Qt::MoveAction, &mime, Qt::LeftButton,
                  Qt::NoModifier);
  QCoreApplication::sendEvent(tree.viewport(), &drop);
  return drop.isAccepted();
}

QImage render_tree_primitive(QStyle &style, QStyle::PrimitiveElement element,
                             QStyle::State state,
                             Qt::LayoutDirection direction,
                             QSize logical_size, QRect option_rect,
                             qreal device_pixel_ratio = 1.0) {
  const QSize physical_size(
      static_cast<int>(std::lround(logical_size.width() * device_pixel_ratio)),
      static_cast<int>(std::lround(logical_size.height() * device_pixel_ratio)));
  QImage image(physical_size, QImage::Format_ARGB32_Premultiplied);
  image.setDevicePixelRatio(device_pixel_ratio);
  image.fill(Qt::transparent);

  QStyleOption option;
  option.rect = option_rect;
  option.state = state;
  option.direction = direction;
  option.palette.setColor(QPalette::All, QPalette::Midlight,
                          QColor(82, 86, 94));
  option.palette.setColor(QPalette::All, QPalette::Text, Qt::white);
  QPainter painter(&image);
  style.drawPrimitive(element, &option, &painter);
  return image;
}

QImage render_branch(QStyle &style, QStyle::State state,
                     Qt::LayoutDirection direction, QSize logical_size,
                     qreal device_pixel_ratio = 1.0) {
  return render_tree_primitive(
      style, QStyle::PE_IndicatorBranch, state, direction, logical_size,
      QRect(QPoint{}, logical_size), device_pixel_ratio);
}

QRect centered_square(QSize bounds, int extent) {
  QRect square(QPoint{}, QSize(extent, extent));
  square.moveCenter(QRect(QPoint{}, bounds).center());
  return square;
}

QImage render_selected_item(QStyle &style, const QPalette &palette,
                            bool branch_area) {
  QImage image(80, 24, QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QStyleOptionViewItem option;
  option.rect = image.rect();
  option.state = QStyle::State_Enabled | QStyle::State_Active |
                 QStyle::State_Selected;
  option.palette = palette;
  option.showDecorationSelected = true;
  QPainter painter(&image);
  if (branch_area) {
    style.drawPrimitive(QStyle::PE_PanelItemViewRow, &option, &painter);
  } else {
    style.drawControl(QStyle::CE_ItemViewItem, &option, &painter);
  }
  return image;
}

QRect physical_region(const QImage &image, const QRect &logical_region) {
  const qreal ratio = image.devicePixelRatio();
  const int left =
      static_cast<int>(std::floor(logical_region.left() * ratio));
  const int top =
      static_cast<int>(std::floor(logical_region.top() * ratio));
  const int right = static_cast<int>(std::ceil(
                        (logical_region.right() + 1) * ratio)) -
                    1;
  const int bottom = static_cast<int>(std::ceil(
                         (logical_region.bottom() + 1) * ratio)) -
                     1;
  return QRect(QPoint(left, top), QPoint(right, bottom))
      .intersected(image.rect());
}

bool region_contains_ink(const QImage &image, const QRect &logical_region) {
  const QRect physical = physical_region(image, logical_region);
  for (int y = physical.top(); y <= physical.bottom(); ++y) {
    for (int x = physical.left(); x <= physical.right(); ++x) {
      if (image.pixelColor(x, y).alpha() != 0) {
        return true;
      }
    }
  }
  return false;
}

void expect_regions_equal(const QImage &actual, const QImage &expected,
                          const QRect &logical_region) {
  ASSERT_EQ(actual.size(), expected.size());
  ASSERT_EQ(actual.devicePixelRatio(), expected.devicePixelRatio());
  const QRect physical = physical_region(actual, logical_region);
  for (int y = physical.top(); y <= physical.bottom(); ++y) {
    for (int x = physical.left(); x <= physical.right(); ++x) {
      EXPECT_EQ(actual.pixel(x, y), expected.pixel(x, y))
          << "pixel mismatch at (" << x << ", " << y << ")";
    }
  }
}

TEST(QtMaterialBrowserWindowTests,
     FilterIconResourceRetainsVisibleAndTransparentRgbaPixels) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-material-filter-icon-tests";
  char *argv[] = {name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  const QImage icon(QStringLiteral(":/quader/icons/material_filter.png"));
  ASSERT_FALSE(icon.isNull())
      << "the material-browser filter icon must be registered as a Qt resource";
  EXPECT_TRUE(icon.hasAlphaChannel());

  bool has_transparent_pixel = false;
  bool has_visible_pixel = false;
  for (int y = 0; y < icon.height(); ++y) {
    for (int x = 0; x < icon.width(); ++x) {
      const int alpha = icon.pixelColor(x, y).alpha();
      has_transparent_pixel = has_transparent_pixel || alpha == 0;
      has_visible_pixel = has_visible_pixel || alpha > 0;
    }
  }
  EXPECT_TRUE(has_transparent_pixel)
      << "the generated asset must keep a genuinely transparent background";
  EXPECT_TRUE(has_visible_pixel);
}

TEST(QtMaterialBrowserWindowTests,
     FolderTreePaintsRelationshipsWithFusionDisclosureAndApplicationSelection) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-material-tree-presentation-tests";
  char *argv[] = {name, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));
  application.setQuitOnLastWindowClosed(false);

  QPalette application_palette = application.palette();
  const QColor harsh_highlight(167, 148, 221);
  application_palette.setColor(QPalette::All, QPalette::Highlight,
                               harsh_highlight);
  application_palette.setColor(QPalette::All, QPalette::HighlightedText,
                               QColor(23, 24, 28));
  QApplication::setPalette(application_palette);
  ASSERT_NE(QApplication::palette().resolveMask(), 0U);

  QtMaterialBrowserWindow window;
  window.setAttribute(Qt::WA_DontShowOnScreen, true);
  auto *tree = window.findChild<QTreeWidget *>(
      QStringLiteral("materialBrowser.folderTree"));
  ASSERT_NE(tree, nullptr);
  ASSERT_NE(tree->style(), QApplication::style());
  EXPECT_EQ(tree->style()->objectName(),
            QStringLiteral("quaderTreeRelationshipPresentation"));
  EXPECT_EQ(tree->palette().color(QPalette::Active, QPalette::Highlight),
            harsh_highlight)
      << "the presentation must not replace the tree or application palette";
  EXPECT_EQ(QApplication::palette().color(QPalette::Active,
                                          QPalette::Highlight),
            harsh_highlight);

  const QImage selected_item =
      render_selected_item(*tree->style(), tree->palette(), false);
  const QImage selected_branch_area =
      render_selected_item(*tree->style(), tree->palette(), true);
  EXPECT_EQ(selected_item.pixelColor(selected_item.rect().center()),
            harsh_highlight);
  EXPECT_EQ(selected_branch_area.pixelColor(
                selected_branch_area.rect().center()),
            harsh_highlight);

  constexpr QStyle::State kCollapsedBranch =
      QStyle::State_Enabled | QStyle::State_Active | QStyle::State_Item |
      QStyle::State_Sibling | QStyle::State_Children;
  const QSize branch_size(21, 21);
  const QRect disclosure = centered_square(branch_size, 9);
  for (const qreal ratio : {1.0, 2.0}) {
    const QImage left_to_right =
        render_branch(*tree->style(), kCollapsedBranch, Qt::LeftToRight,
                      branch_size, ratio);
    EXPECT_TRUE(region_contains_ink(left_to_right, QRect(16, 9, 5, 3)));
    EXPECT_FALSE(region_contains_ink(left_to_right, QRect(0, 9, 5, 3)));
    EXPECT_TRUE(region_contains_ink(left_to_right, QRect(9, 0, 3, 5)));
    EXPECT_TRUE(region_contains_ink(left_to_right, QRect(9, 16, 3, 5)));
    const QImage expected_right = render_tree_primitive(
        *tree->style(), QStyle::PE_IndicatorArrowRight, kCollapsedBranch,
        Qt::LeftToRight, branch_size, disclosure, ratio);
    expect_regions_equal(left_to_right, expected_right, disclosure);

    const QImage right_to_left =
        render_branch(*tree->style(), kCollapsedBranch, Qt::RightToLeft,
                      branch_size, ratio);
    EXPECT_TRUE(region_contains_ink(right_to_left, QRect(0, 9, 5, 3)));
    EXPECT_FALSE(region_contains_ink(right_to_left, QRect(16, 9, 5, 3)));
    const QImage expected_left = render_tree_primitive(
        *tree->style(), QStyle::PE_IndicatorArrowLeft, kCollapsedBranch,
        Qt::RightToLeft, branch_size, disclosure, ratio);
    expect_regions_equal(right_to_left, expected_left, disclosure);

    const QImage open = render_branch(
        *tree->style(), kCollapsedBranch | QStyle::State_Open,
        Qt::LeftToRight, branch_size, ratio);
    const QImage expected_down = render_tree_primitive(
        *tree->style(), QStyle::PE_IndicatorArrowDown,
        kCollapsedBranch | QStyle::State_Open, Qt::LeftToRight, branch_size,
        disclosure, ratio);
    expect_regions_equal(open, expected_down, disclosure);
  }

  const int unscaled_indentation = tree->indentation();
  const QSize scaled_test_size(41, 41);
  const QRect unscaled_disclosure = centered_square(scaled_test_size, 9);
  const QImage unscaled_indicator =
      render_branch(*tree->style(), QStyle::State_Children, Qt::LeftToRight,
                    scaled_test_size);
  const QImage expected_unscaled = render_tree_primitive(
      *tree->style(), QStyle::PE_IndicatorArrowRight,
      QStyle::State_Children, Qt::LeftToRight, scaled_test_size,
      unscaled_disclosure);
  expect_regions_equal(unscaled_indicator, expected_unscaled,
                       unscaled_disclosure);

  window.apply_ui_scale(QtUiScaleMetrics(2.0F, 1U));
  EXPECT_EQ(tree->indentation(), unscaled_indentation * 2);
  const QImage scaled_indicator =
      render_branch(*tree->style(), QStyle::State_Children, Qt::LeftToRight,
                    scaled_test_size);
  const QImage expected_scaled = render_tree_primitive(
      *tree->style(), QStyle::PE_IndicatorArrowRight,
      QStyle::State_Children, Qt::LeftToRight, scaled_test_size,
      unscaled_disclosure);
  expect_regions_equal(scaled_indicator, expected_scaled,
                       unscaled_disclosure);
  window.apply_ui_scale(QtUiScaleMetrics(2.0F, 1U));
  EXPECT_EQ(tree->indentation(), unscaled_indentation * 2);
  window.apply_ui_scale(QtUiScaleMetrics(1.0F, 2U));
  EXPECT_EQ(tree->indentation(), unscaled_indentation);

  constexpr QStyle::State kConnectedLeaf =
      QStyle::State_Enabled | QStyle::State_Active | QStyle::State_Item |
      QStyle::State_Sibling;
  const QImage themed_connectors =
      render_branch(*tree->style(), kConnectedLeaf, Qt::LeftToRight,
                    branch_size);
  EXPECT_EQ(themed_connectors.pixelColor(10, 1), QColor(82, 86, 94));

  QApplication::setPalette(QPalette{});
  QCoreApplication::processEvents();
  ASSERT_EQ(QApplication::palette().resolveMask(), 0U);
  const QImage platform_connectors =
      render_branch(*tree->style(), kConnectedLeaf, Qt::LeftToRight,
                    branch_size);
  EXPECT_EQ(platform_connectors.pixelColor(10, 1), Qt::white)
      << "an unresolved application palette uses the platform text color for "
         "high-contrast relationship lines";
}

TEST(QtMaterialBrowserWindowTests,
     FolderHierarchyFiltersDescendantsAndRetainsProjectPresentation) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-material-folders-tests";
  char *argv[] = {name, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));
  QApplication::setPalette(
      qt_obsidian_application_palette(QApplication::style()->standardPalette()));
  application.setQuitOnLastWindowClosed(false);
  QtMaterialBrowserWindow window;
  window.setAttribute(Qt::WA_DontShowOnScreen, true);
  window.set_project_generation(10);
  auto materials = std::vector<QtMaterialBrowserItem>{
      folder_material("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa",
          QStringLiteral("Base"), QStringLiteral("Materials/base.qmat")),
      folder_material("bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb",
          QStringLiteral("Dev"), QStringLiteral("Materials/Dev/base.qmat")),
      folder_material("cccccccc-cccc-cccc-cccc-cccccccccccc",
          QStringLiteral("Oak"), QStringLiteral("Materials/Dev/Wood/oak.qmat")),
      folder_material("dddddddd-dddd-dddd-dddd-dddddddddddd",
          QStringLiteral("Oak other"), QStringLiteral("Materials/Development/oak.qmat")),
  };
  window.set_folders({QStringLiteral("Materials/Zebra"),
                     QStringLiteral("Materials/Dev/Empty"),
                     QStringLiteral("Materials/Dev/Wood"),
                     QStringLiteral("../invalid"),
                     QStringLiteral("Materials/../invalid")});
  window.set_materials(materials);
  auto *tree = window.findChild<QTreeWidget *>(QStringLiteral("materialBrowser.folderTree"));
  auto *grid = window.findChild<QListView *>(QStringLiteral("materialBrowser.gridView"));
  auto *search = window.findChild<QLineEdit *>(QStringLiteral("materialBrowser.search"));
  auto *import_button = window.findChild<QPushButton *>(QStringLiteral("materialBrowser.import"));
  auto *toggle = window.findChild<QToolButton *>(QStringLiteral("materialBrowser.folders"));
  ASSERT_NE(tree, nullptr);
  ASSERT_NE(grid, nullptr);
  ASSERT_NE(search, nullptr);
  ASSERT_NE(import_button, nullptr);
  ASSERT_NE(toggle, nullptr);
  EXPECT_EQ(tree->topLevelItem(0)->text(0), QStringLiteral("All"));
  EXPECT_EQ(tree->topLevelItem(1)->text(0), QStringLiteral("Dev"));
  EXPECT_EQ(tree->topLevelItem(2)->text(0), QStringLiteral("Development"));
  EXPECT_EQ(tree->topLevelItem(3)->text(0), QStringLiteral("Zebra"));
  ASSERT_NE(folder_item(*tree, QStringLiteral("Materials/Dev/Empty")), nullptr);
  EXPECT_EQ(folder_item(*tree, QStringLiteral("Materials/../invalid")), nullptr);
  EXPECT_EQ(grid->model()->rowCount(), 4);
  window.set_selected_folder(QStringLiteral("Materials/Dev"));
  auto *dev = folder_item(*tree, QStringLiteral("Materials/Dev"));
  ASSERT_NE(dev, nullptr);
  dev->setExpanded(true);
  EXPECT_EQ(grid->model()->rowCount(), 2);
  search->setText(QStringLiteral("OAK"));
  ASSERT_EQ(grid->model()->rowCount(), 1);
  EXPECT_EQ(grid->model()->index(0, 0).data().toString(), QStringLiteral("Oak"));
  search->clear();
  window.set_selected_folder(QStringLiteral("Materials/Dev/Empty"));
  EXPECT_EQ(grid->model()->rowCount(), 0);
  auto *empty = window.findChild<QLabel *>(QStringLiteral("materialBrowser.empty"));
  ASSERT_NE(empty, nullptr);
  EXPECT_EQ(empty->text(), QStringLiteral("No materials in this folder"));
  search->setText(QStringLiteral("missing"));
  EXPECT_EQ(empty->text(), QStringLiteral("No materials match this search"));
  search->clear();
  QString imported_to;
  window.set_import_textures_callback([&](QString destination) {
    imported_to = std::move(destination);
  });
  import_button->click();
  EXPECT_EQ(imported_to, QStringLiteral("Materials/Dev/Empty"));
  int resets = 0;
  QObject::connect(grid->model(), &QAbstractItemModel::modelReset, &window,
                   [&]() { ++resets; });
  window.set_materials(materials);
  EXPECT_EQ(resets, 0);
  window.set_folders({QStringLiteral("Materials/Dev/Empty"),
                     QStringLiteral("Materials/Dev/New"),
                     QStringLiteral("Materials/Zebra")});
  EXPECT_EQ(window.selected_folder(), QStringLiteral("Materials/Dev/Empty"));
  EXPECT_TRUE(folder_item(*tree, QStringLiteral("Materials/Dev"))->isExpanded());
  window.set_folders({QStringLiteral("Materials/Dev/New")});
  EXPECT_EQ(window.selected_folder(), QStringLiteral("Materials/Dev"));
  EXPECT_EQ(grid->model()->rowCount(), 2);
  window.follow_moved_folder(QStringLiteral("Materials/Dev"), QStringLiteral("Materials/Art"));
  materials[1].project_relative_path = QStringLiteral("Materials/Art/base.qmat");
  materials[2].project_relative_path = QStringLiteral("Materials/Art/Wood/oak.qmat");
  window.set_folders({QStringLiteral("Materials/Art/New")});
  EXPECT_EQ(window.selected_folder(), QStringLiteral("Materials/Art"));
  window.set_materials(materials);
  EXPECT_EQ(window.selected_folder(), QStringLiteral("Materials/Art"));
  EXPECT_TRUE(folder_item(*tree, QStringLiteral("Materials/Art"))->isExpanded());
  int state_changes = 0;
  window.set_presentation_state_changed_callback([&]() { ++state_changes; });
  window.restore_presentation_state(QtMaterialBrowserViewMode::List, 64, false, 270);
  EXPECT_FALSE(window.sidebar_visible());
  EXPECT_TRUE(tree->isHidden());
  EXPECT_EQ(window.sidebar_width(), 270);
  EXPECT_EQ(state_changes, 0);
  toggle->click();
  EXPECT_TRUE(window.sidebar_visible());
  EXPECT_FALSE(tree->isHidden());
  EXPECT_EQ(state_changes, 1);
  window.set_project_generation(10);
  EXPECT_EQ(window.selected_folder(), QStringLiteral("Materials/Art"));
  const QString screenshot_path = qEnvironmentVariable("QUADER_MATERIAL_BROWSER_SCREENSHOT");
  if (!screenshot_path.isEmpty()) {
    window.set_materials(materials, materials[1].reference);
    window.restore_presentation_state(QtMaterialBrowserViewMode::Grid, 128, true, 230);
    window.resize(980, 600);
    window.show();
    search->setFocus();
    QCoreApplication::processEvents();
    EXPECT_TRUE(window.grab().save(screenshot_path, "PNG"));
  }
  window.set_project_generation(11);
  EXPECT_EQ(window.selected_folder(), QStringLiteral("Materials"));
  EXPECT_EQ(tree->topLevelItemCount(), 1);
  EXPECT_TRUE(window.sidebar_visible());
}

TEST(QtMaterialBrowserWindowTests,
     BothViewsDragMaterialBatchesAndTreeDropsRejectStaleOrCyclicIntent) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-material-drag-tests";
  char *argv[] = {name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  QtMaterialBrowserWindow window;
  window.setAttribute(Qt::WA_DontShowOnScreen, true);
  window.set_project_generation(27);
  auto materials = std::vector<QtMaterialBrowserItem>{
      folder_material("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa",
          QStringLiteral("A"), QStringLiteral("Materials/a.qmat")),
      folder_material("bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb",
          QStringLiteral("B"), QStringLiteral("Materials/b.qmat")),
  };
  window.set_materials(materials, materials[0].reference);
  window.set_folders({QStringLiteral("Materials/Dev/Wood"), QStringLiteral("Materials/Other")});
  window.show();
  QCoreApplication::processEvents();
  auto *tree = window.findChild<QTreeWidget *>(QStringLiteral("materialBrowser.folderTree"));
  auto *grid = window.findChild<QListView *>(QStringLiteral("materialBrowser.gridView"));
  auto *list = window.findChild<QTableView *>(QStringLiteral("materialBrowser.listView"));
  ASSERT_NE(tree, nullptr);
  ASSERT_NE(grid, nullptr);
  ASSERT_NE(list, nullptr);
  int material_moves = 0;
  window.set_move_materials_callback([&](std::vector<QuaderMaterialReference> refs,
                                        QString destination, std::uint64_t generation) {
    EXPECT_EQ(refs.size(), 2);
    EXPECT_EQ(refs[0], materials[0].reference);
    EXPECT_EQ(refs[1], materials[1].reference);
    EXPECT_EQ(destination, QStringLiteral("Materials/Other"));
    EXPECT_EQ(generation, 27);
    ++material_moves;
  });
  auto *destination = folder_item(*tree, QStringLiteral("Materials/Other"));
  ASSERT_NE(destination, nullptr);
  for (QAbstractItemView *view : std::vector<QAbstractItemView *>{grid, list}) {
    view->selectionModel()->select(QItemSelection(view->model()->index(0, 0),
                                                 view->model()->index(1, 0)),
                                  QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    view->selectionModel()->setCurrentIndex(view->model()->index(1, 0), QItemSelectionModel::NoUpdate);
    EXPECT_EQ(window.selected_material(), materials[1].reference);
    window.set_selected_material(materials[1].reference);
    EXPECT_EQ(view->selectionModel()->selectedRows().size(), 2);
    std::unique_ptr<QMimeData> mime(view->model()->mimeData(view->selectionModel()->selectedIndexes()));
    ASSERT_NE(mime, nullptr);
    EXPECT_TRUE(drop_on_folder(*tree, *destination, *mime));
    EXPECT_EQ(view->model()->rowCount(), 2)
        << "the owner publishes moves; a successful Qt drop does not remove source rows";
  }
  EXPECT_EQ(material_moves, 2);
  std::unique_ptr<QMimeData> stale(grid->model()->mimeData(grid->selectionModel()->selectedIndexes()));
  ASSERT_NE(stale, nullptr);
  materials[0].project_relative_path = QStringLiteral("Materials/Dev/a.qmat");
  window.set_materials(materials, materials[1].reference);
  EXPECT_EQ(grid->selectionModel()->selectedRows().size(), 2);
  destination = folder_item(*tree, QStringLiteral("Materials/Other"));
  ASSERT_NE(destination, nullptr);
  EXPECT_FALSE(drop_on_folder(*tree, *destination, *stale));
  EXPECT_EQ(material_moves, 2);
  int folder_moves = 0;
  QString last_destination;
  window.set_move_folder_callback([&](QString source, QString target, std::uint64_t generation) {
    EXPECT_EQ(source, QStringLiteral("Materials/Dev"));
    EXPECT_EQ(generation, 27);
    last_destination = std::move(target);
    ++folder_moves;
  });
  QByteArray bytes;
  QDataStream stream(&bytes, QIODevice::WriteOnly);
  stream << quint64{27} << QStringLiteral("Materials/Dev");
  QMimeData folder_mime;
  folder_mime.setData(QStringLiteral("application/x-quader-material-browser-folder"), bytes);
  auto *dev = folder_item(*tree, QStringLiteral("Materials/Dev"));
  auto *wood = folder_item(*tree, QStringLiteral("Materials/Dev/Wood"));
  ASSERT_NE(dev, nullptr);
  ASSERT_NE(wood, nullptr);
  dev->setExpanded(true);
  EXPECT_FALSE(drop_on_folder(*tree, *dev, folder_mime));
  EXPECT_FALSE(drop_on_folder(*tree, *wood, folder_mime));
  EXPECT_TRUE(drop_on_folder(*tree, *destination, folder_mime));
  EXPECT_EQ(last_destination, QStringLiteral("Materials/Other"));
  EXPECT_TRUE(drop_on_folder(*tree, *tree->topLevelItem(0), folder_mime));
  EXPECT_EQ(last_destination, QStringLiteral("Materials"));
  EXPECT_EQ(folder_moves, 2);
  window.set_purpose(QtMaterialBrowserPurpose::Picker);
  EXPECT_FALSE(grid->dragEnabled());
  EXPECT_FALSE(list->dragEnabled());
  EXPECT_FALSE(tree->acceptDrops());
  EXPECT_EQ(grid->selectionMode(), QAbstractItemView::SingleSelection);
  EXPECT_EQ(list->selectionMode(), QAbstractItemView::SingleSelection);
  EXPECT_EQ(grid->selectionModel()->selectedRows().size(), 1);
  EXPECT_FALSE(drop_on_folder(*tree, *destination, folder_mime));
  EXPECT_EQ(folder_moves, 2);
  window.set_purpose(QtMaterialBrowserPurpose::Management);
  window.set_project_generation(28);
  window.set_folders({QStringLiteral("Materials/Dev/Wood"), QStringLiteral("Materials/Other")});
  destination = folder_item(*tree, QStringLiteral("Materials/Other"));
  ASSERT_NE(destination, nullptr);
  EXPECT_FALSE(drop_on_folder(*tree, *destination, folder_mime));
  EXPECT_EQ(folder_moves, 2);
}

TEST(QtMaterialBrowserWindowTests,
     UsageProxyCombinesPredicatesAndUsesOnlyConfirmedIncompleteResults) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-material-usage-proxy-tests";
  char *argv[] = {name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  QtMaterialBrowserModel model;
  const auto used_in_dev = folder_material(
      "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa", QStringLiteral("Brick"),
      QStringLiteral("Materials/Dev/brick.qmat"));
  const auto unused_in_dev = folder_material(
      "bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb", QStringLiteral("Oak"),
      QStringLiteral("Materials/Dev/Wood/oak.qmat"));
  const auto used_elsewhere = folder_material(
      "cccccccc-cccc-cccc-cccc-cccccccccccc", QStringLiteral("Steel"),
      QStringLiteral("Materials/Other/steel.qmat"));
  ASSERT_TRUE(model.set_items({used_in_dev, unused_in_dev, used_elsewhere}));

  QtMaterialBrowserProxyModel proxy;
  proxy.setSourceModel(&model);
  proxy.set_folder(QStringLiteral("Materials/Dev"));
  proxy.set_used_material_ids({used_elsewhere.stable_id, used_in_dev.stable_id},
                              true);
  proxy.set_usage_mode(QtMaterialBrowserUsageMode::Used);
  ASSERT_EQ(proxy.rowCount(), 1);
  EXPECT_EQ(proxy.index(0, 0).data().toString(), QStringLiteral("Brick"));

  proxy.set_usage_mode(QtMaterialBrowserUsageMode::NotUsed);
  ASSERT_EQ(proxy.rowCount(), 1);
  EXPECT_EQ(proxy.index(0, 0).data().toString(), QStringLiteral("Oak"));
  EXPECT_TRUE(proxy.has_non_usage_match());
  proxy.set_search_text(QStringLiteral("brick"));
  EXPECT_EQ(proxy.rowCount(), 0)
      << "folder, search, and usage predicates must compose with AND";
  EXPECT_TRUE(proxy.has_non_usage_match());

  proxy.set_used_material_ids({used_in_dev.stable_id}, false);
  EXPECT_EQ(proxy.rowCount(), 0)
      << "incomplete inspection cannot establish that a material is unused";
  proxy.set_usage_mode(QtMaterialBrowserUsageMode::Used);
  ASSERT_EQ(proxy.rowCount(), 1);
  EXPECT_EQ(proxy.index(0, 0).data().toString(), QStringLiteral("Brick"));
  proxy.set_search_text({});
  ASSERT_EQ(proxy.rowCount(), 1)
      << "confirmed used IDs remain valid when another scene is unreadable";
  EXPECT_EQ(proxy.index(0, 0).data().toString(), QStringLiteral("Brick"));
  proxy.set_usage_mode(QtMaterialBrowserUsageMode::All);
  EXPECT_EQ(proxy.rowCount(), 2);
}

TEST(QtMaterialBrowserWindowTests,
     UsageDialogStagesExclusiveChoiceAndResetsOnlyForANewProject) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-material-usage-dialog-tests";
  char *argv[] = {name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  QtMaterialBrowserWindow window;
  window.setAttribute(Qt::WA_DontShowOnScreen, true);
  window.set_project_generation(41);
  auto used = folder_material("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa",
                              QStringLiteral("Used"),
                              QStringLiteral("Materials/used.qmat"));
  auto unused = folder_material("bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb",
                                QStringLiteral("Unused"),
                                QStringLiteral("Materials/unused.qmat"));
  window.set_materials({used, unused});
  window.set_material_usage({
      .project_generation = 41,
      .revision = 1,
      .complete = true,
      .used_materials = {used.reference},
  });
  auto *filter =
      window.findChild<QToolButton *>(QStringLiteral("materialBrowser.filter"));
  auto *grid =
      window.findChild<QListView *>(QStringLiteral("materialBrowser.gridView"));
  auto *empty =
      window.findChild<QLabel *>(QStringLiteral("materialBrowser.empty"));
  ASSERT_NE(filter, nullptr);
  ASSERT_NE(grid, nullptr);
  ASSERT_NE(empty, nullptr);
  EXPECT_EQ(filter->toolButtonStyle(), Qt::ToolButtonIconOnly);
  EXPECT_FALSE(filter->isChecked());

  filter->click();
  auto *popup = window.findChild<QFrame *>(
      QStringLiteral("materialBrowser.filterPopup"));
  ASSERT_NE(popup, nullptr);
  {
    auto *used_box = popup->findChild<QCheckBox *>(
        QStringLiteral("materialBrowser.filterUsed"));
    auto *not_used_box = popup->findChild<QCheckBox *>(
        QStringLiteral("materialBrowser.filterNotUsed"));
    auto *apply = popup->findChild<QPushButton *>(
        QStringLiteral("materialBrowser.filterApply"));
    ASSERT_NE(used_box, nullptr);
    ASSERT_NE(not_used_box, nullptr);
    ASSERT_NE(apply, nullptr);
    used_box->click();
    EXPECT_TRUE(used_box->isChecked());
    EXPECT_FALSE(not_used_box->isChecked());
    apply->click();
    application.processEvents();
  }
  EXPECT_EQ(window.usage_mode(), QtMaterialBrowserUsageMode::Used);
  EXPECT_TRUE(filter->isChecked());
  ASSERT_EQ(grid->model()->rowCount(), 1);
  EXPECT_EQ(grid->model()->index(0, 0).data().toString(),
            QStringLiteral("Used"));

  filter->click();
  popup = window.findChild<QFrame *>(
      QStringLiteral("materialBrowser.filterPopup"));
  ASSERT_NE(popup, nullptr);
  {
    auto *not_used_box = popup->findChild<QCheckBox *>(
        QStringLiteral("materialBrowser.filterNotUsed"));
    auto *cancel = popup->findChild<QPushButton *>(
        QStringLiteral("materialBrowser.filterCancel"));
    ASSERT_NE(not_used_box, nullptr);
    ASSERT_NE(cancel, nullptr);
    not_used_box->click();
    cancel->click();
    application.processEvents();
  }
  EXPECT_EQ(window.usage_mode(), QtMaterialBrowserUsageMode::Used)
      << "Cancel must preserve the previously applied choice";
  EXPECT_TRUE(filter->isChecked());

  filter->click();
  popup = window.findChild<QFrame *>(
      QStringLiteral("materialBrowser.filterPopup"));
  ASSERT_NE(popup, nullptr);
  {
    auto *clear = popup->findChild<QPushButton *>(
        QStringLiteral("materialBrowser.filterClear"));
    auto *apply = popup->findChild<QPushButton *>(
        QStringLiteral("materialBrowser.filterApply"));
    ASSERT_NE(clear, nullptr);
    ASSERT_NE(apply, nullptr);
    clear->click();
    apply->click();
    application.processEvents();
  }
  EXPECT_EQ(window.usage_mode(), QtMaterialBrowserUsageMode::All);
  EXPECT_FALSE(filter->isChecked());
  EXPECT_EQ(grid->model()->rowCount(), 2);

  auto *usage_status = window.findChild<QLabel *>(
      QStringLiteral("materialBrowser.usageStatus"));
  ASSERT_NE(usage_status, nullptr);
  window.set_material_usage({
      .project_generation = 41,
      .revision = 2,
      .complete = true,
  });
  filter->click();
  popup = window.findChild<QFrame *>(
      QStringLiteral("materialBrowser.filterPopup"));
  ASSERT_NE(popup, nullptr);
  {
    auto *used_box = popup->findChild<QCheckBox *>(
        QStringLiteral("materialBrowser.filterUsed"));
    auto *apply = popup->findChild<QPushButton *>(
        QStringLiteral("materialBrowser.filterApply"));
    ASSERT_NE(used_box, nullptr);
    ASSERT_NE(apply, nullptr);
    used_box->click();
    apply->click();
    application.processEvents();
  }
  EXPECT_EQ(grid->model()->rowCount(), 0);
  EXPECT_EQ(empty->text(),
            QStringLiteral("No materials match this usage filter"));

  const QtMaterialBrowserUsageSnapshot incomplete{
      .project_generation = 41,
      .revision = 3,
      .complete = false,
      .errors = {QStringLiteral("Unreadable scene")},
  };
  window.set_material_usage(incomplete);
  EXPECT_EQ(grid->model()->rowCount(), 0);
  EXPECT_TRUE(usage_status->text().contains(QStringLiteral("Unreadable scene")));
  EXPECT_FALSE(usage_status->isHidden());
  const QString retained_error = usage_status->text();
  window.set_material_usage(incomplete);
  EXPECT_EQ(usage_status->text(), retained_error)
      << "unchanged errors retain a single inline status";

  window.set_material_usage({
      .project_generation = 41,
      .revision = 4,
      .complete = true,
  });
  EXPECT_EQ(grid->model()->rowCount(), 0);
  EXPECT_TRUE(usage_status->isHidden());
  window.set_purpose(QtMaterialBrowserPurpose::Picker);
  EXPECT_TRUE(filter->isHidden());
  EXPECT_EQ(grid->model()->rowCount(), 2);
  window.set_purpose(QtMaterialBrowserPurpose::Management);
  EXPECT_EQ(grid->model()->rowCount(), 0);
  EXPECT_EQ(window.usage_mode(), QtMaterialBrowserUsageMode::Used);

  window.set_project_generation(42);
  EXPECT_EQ(window.usage_mode(), QtMaterialBrowserUsageMode::All);
  EXPECT_FALSE(filter->isChecked());
}

TEST(QtMaterialBrowserWindowTests,
     EmbeddedUsagePopupAppliesImmediatelyAndChipPreservesFolderAndSearch) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-embedded-material-usage-tests";
  char *argv[] = {name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  QWidget owner;
  owner.setAttribute(Qt::WA_DontShowOnScreen, true);
  owner.resize(340, 520);
  QtMaterialBrowserWindow browser(&owner, QtMaterialBrowserHost::Embedded);
  browser.resize(340, 520);
  browser.restore_presentation_state(QtMaterialBrowserViewMode::Grid, 64,
                                     false, 120);
  const auto used = folder_material(
      "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa", QStringLiteral("Oak Used"),
      QStringLiteral("Materials/Wood/oak_used.qmat"));
  const auto unused = folder_material(
      "bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb", QStringLiteral("Oak Unused"),
      QStringLiteral("Materials/Wood/oak_unused.qmat"));
  const auto elsewhere = folder_material(
      "cccccccc-cccc-cccc-cccc-cccccccccccc", QStringLiteral("Oak Elsewhere"),
      QStringLiteral("Materials/Other/oak.qmat"));
  browser.set_project_generation(41);
  int assignments = 0;
  browser.set_assign_material_callback(
      [&](QuaderMaterialReference) { ++assignments; });
  browser.set_assignment_available(true);
  browser.set_folders({QStringLiteral("Materials/Wood"),
                       QStringLiteral("Materials/Other")});
  browser.set_materials({used, unused, elsewhere});
  browser.set_selected_folder(QStringLiteral("Materials/Wood"));
  browser.set_material_usage({
      .project_generation = 41,
      .revision = 1,
      .complete = true,
      .used_materials = {used.reference, elsewhere.reference},
  });

  auto *search = browser.findChild<QLineEdit *>(
      QStringLiteral("materialBrowser.search"));
  auto *filter = browser.findChild<QToolButton *>(
      QStringLiteral("materialBrowser.filter"));
  auto *chip = browser.findChild<QWidget *>(
      QStringLiteral("materialBrowser.usageChip"));
  auto *chip_label = browser.findChild<QLabel *>(
      QStringLiteral("materialBrowser.usageChipLabel"));
  auto *chip_remove = browser.findChild<QToolButton *>(
      QStringLiteral("materialBrowser.usageChipRemove"));
  auto *active_filters = browser.findChild<QWidget *>(
      QStringLiteral("materialBrowser.activeFilters"));
  auto *filtered_by = browser.findChild<QLabel *>(
      QStringLiteral("materialBrowser.filteredBy"));
  auto *grid = browser.findChild<QListView *>(
      QStringLiteral("materialBrowser.gridView"));
  auto *empty = browser.findChild<QLabel *>(
      QStringLiteral("materialBrowser.empty"));
  auto *status = browser.findChild<QLabel *>(
      QStringLiteral("materialBrowser.usageStatus"));
  ASSERT_NE(search, nullptr);
  ASSERT_NE(filter, nullptr);
  ASSERT_NE(chip, nullptr);
  ASSERT_NE(chip_label, nullptr);
  ASSERT_NE(chip_remove, nullptr);
  ASSERT_NE(active_filters, nullptr);
  ASSERT_NE(filtered_by, nullptr);
  ASSERT_NE(grid, nullptr);
  ASSERT_NE(empty, nullptr);
  ASSERT_NE(status, nullptr);
  search->setText(QStringLiteral("oak"));
  EXPECT_EQ(grid->model()->rowCount(), 2);
  EXPECT_FALSE(chip->isVisible());
  EXPECT_TRUE(active_filters->isHidden());
  EXPECT_TRUE(status->isHidden());
  owner.show();
  browser.show();
  application.processEvents();

  filter->click();
  auto *popup = browser.findChild<QFrame *>(
      QStringLiteral("materialBrowser.filterPopup"));
  ASSERT_NE(popup, nullptr);
  EXPECT_EQ(popup->findChild<QPushButton *>(
                QStringLiteral("materialBrowser.filterApply")),
            nullptr);
  EXPECT_EQ(popup->findChild<QPushButton *>(
                QStringLiteral("materialBrowser.filterCancel")),
            nullptr);
  auto *used_box = popup->findChild<QCheckBox *>(
      QStringLiteral("materialBrowser.filterUsed"));
  auto *not_used_box = popup->findChild<QCheckBox *>(
      QStringLiteral("materialBrowser.filterNotUsed"));
  ASSERT_NE(used_box, nullptr);
  ASSERT_NE(not_used_box, nullptr);
  used_box->click();
  EXPECT_EQ(browser.usage_mode(), QtMaterialBrowserUsageMode::Used);
  EXPECT_TRUE(used_box->isChecked());
  EXPECT_FALSE(not_used_box->isChecked());
  ASSERT_EQ(grid->model()->rowCount(), 1);
  EXPECT_EQ(grid->model()->index(0, 0).data().toString(), used.name);
  EXPECT_FALSE(chip->isHidden());
  EXPECT_FALSE(active_filters->isHidden());
  EXPECT_EQ(filtered_by->text(), QStringLiteral("Filtered by:"));
  EXPECT_EQ(chip_label->text(), QStringLiteral("Used"));
  EXPECT_EQ(chip_label->focusPolicy(), Qt::NoFocus);
  EXPECT_EQ(qobject_cast<QAbstractButton *>(chip), nullptr);
  EXPECT_FALSE(chip_remove->icon().isNull());
  EXPECT_EQ(chip_remove->accessibleName(), QStringLiteral("Remove Used filter"));
  EXPECT_TRUE(filter->isChecked());
  EXPECT_TRUE(filter->toolTip().contains(QStringLiteral("1 active")));
  EXPECT_FALSE(popup->isVisible());
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

  const QPointF label_position(chip_label->rect().center());
  const QPointF label_global(chip_label->mapToGlobal(label_position.toPoint()));
  QMouseEvent label_press(QEvent::MouseButtonPress, label_position, label_global,
                         Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  QMouseEvent label_release(QEvent::MouseButtonRelease, label_position, label_global,
                           Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  QCoreApplication::sendEvent(chip_label, &label_press);
  QCoreApplication::sendEvent(chip_label, &label_release);
  EXPECT_EQ(browser.usage_mode(), QtMaterialBrowserUsageMode::Used)
      << "the badge body describes the active condition without clearing it";

  filter->click();
  popup = browser.findChild<QFrame *>(
      QStringLiteral("materialBrowser.filterPopup"));
  ASSERT_NE(popup, nullptr);
  used_box = popup->findChild<QCheckBox *>(
      QStringLiteral("materialBrowser.filterUsed"));
  not_used_box = popup->findChild<QCheckBox *>(
      QStringLiteral("materialBrowser.filterNotUsed"));
  ASSERT_NE(used_box, nullptr);
  ASSERT_NE(not_used_box, nullptr);
  EXPECT_TRUE(used_box->isChecked());
  EXPECT_FALSE(not_used_box->isChecked());

  not_used_box->click();
  EXPECT_EQ(browser.usage_mode(), QtMaterialBrowserUsageMode::NotUsed);
  EXPECT_FALSE(used_box->isChecked());
  EXPECT_TRUE(not_used_box->isChecked());
  ASSERT_EQ(grid->model()->rowCount(), 1);
  EXPECT_EQ(grid->model()->index(0, 0).data().toString(), unused.name);
  EXPECT_EQ(chip_label->text(), QStringLiteral("Not Used"));
  EXPECT_EQ(chip_remove->accessibleName(), QStringLiteral("Remove Not Used filter"));
  EXPECT_FALSE(popup->isVisible());
  application.processEvents();
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  EXPECT_EQ(browser.usage_mode(), QtMaterialBrowserUsageMode::NotUsed)
      << "popup dismissal retains its immediate choice";

  int presentation_changes = 0;
  browser.set_presentation_state_changed_callback(
      [&]() { ++presentation_changes; });
  filter->click();
  popup = browser.findChild<QFrame *>(
      QStringLiteral("materialBrowser.filterPopup"));
  ASSERT_NE(popup, nullptr);
  not_used_box = popup->findChild<QCheckBox *>(
      QStringLiteral("materialBrowser.filterNotUsed"));
  ASSERT_NE(not_used_box, nullptr);
  EXPECT_TRUE(not_used_box->isChecked());
  not_used_box->click();
  EXPECT_FALSE(popup->isVisible())
      << "choosing the current predicate also dismisses the popup";
  EXPECT_EQ(browser.usage_mode(), QtMaterialBrowserUsageMode::NotUsed);
  EXPECT_EQ(presentation_changes, 0);
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

  const QtMaterialBrowserUsageSnapshot incomplete{
      .project_generation = 41,
      .revision = 2,
      .complete = false,
      .used_materials = {used.reference},
      .errors = {QStringLiteral("Unreadable scene"),
                 QStringLiteral("Unreadable scene"),
                 QStringLiteral("Missing linked document")},
  };
  browser.set_material_usage(incomplete);
  EXPECT_EQ(browser.usage_mode(), QtMaterialBrowserUsageMode::NotUsed);
  EXPECT_EQ(grid->model()->rowCount(), 0);
  EXPECT_EQ(empty->text(),
            QtMaterialBrowserLabels{}.usage_incomplete);
  EXPECT_FALSE(status->isHidden());
  EXPECT_EQ(status->text().count(QStringLiteral("Unreadable scene")), 1);
  EXPECT_EQ(status->text().count(QStringLiteral("Missing linked document")),
            1);
  const QString retained_status = status->text();
  int catalog_resets = 0;
  QObject::connect(grid->model(), &QAbstractItemModel::modelReset, &browser,
                   [&]() { ++catalog_resets; });
  browser.set_material_usage(incomplete);
  EXPECT_EQ(status->text(), retained_status);
  EXPECT_EQ(catalog_resets, 0);

  filter->click();
  popup = browser.findChild<QFrame *>(
      QStringLiteral("materialBrowser.filterPopup"));
  ASSERT_NE(popup, nullptr);
  used_box = popup->findChild<QCheckBox *>(
      QStringLiteral("materialBrowser.filterUsed"));
  not_used_box = popup->findChild<QCheckBox *>(
      QStringLiteral("materialBrowser.filterNotUsed"));
  ASSERT_NE(used_box, nullptr);
  ASSERT_NE(not_used_box, nullptr);
  EXPECT_TRUE(used_box->isEnabled());
  EXPECT_FALSE(not_used_box->isEnabled());
  used_box->click();
  EXPECT_EQ(browser.usage_mode(), QtMaterialBrowserUsageMode::Used);
  ASSERT_EQ(grid->model()->rowCount(), 1);
  EXPECT_EQ(grid->model()->index(0, 0).data().toString(), used.name);
  EXPECT_FALSE(popup->isVisible());
  application.processEvents();
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

  chip_remove->click();
  EXPECT_EQ(browser.usage_mode(), QtMaterialBrowserUsageMode::All);
  EXPECT_FALSE(chip->isVisible());
  EXPECT_TRUE(active_filters->isHidden());
  EXPECT_FALSE(filter->isChecked());
  EXPECT_FALSE(filter->toolTip().contains(QStringLiteral("1 active")));
  EXPECT_EQ(owner.focusWidget(), filter);
  EXPECT_EQ(browser.selected_folder(), QStringLiteral("Materials/Wood"));
  EXPECT_EQ(search->text(), QStringLiteral("oak"));
  EXPECT_EQ(grid->model()->rowCount(), 2);
  EXPECT_EQ(catalog_resets, 0);

  browser.set_material_usage({
      .project_generation = 40,
      .revision = 99,
      .complete = true,
  });
  EXPECT_EQ(status->text(), retained_status)
      << "a stale project snapshot cannot replace current usage evidence";
  browser.set_material_usage({
      .project_generation = 41,
      .revision = 3,
      .complete = true,
      .used_materials = {used.reference},
  });
  EXPECT_TRUE(status->isHidden());
  EXPECT_EQ(assignments, 0)
      << "usage, folder and search presentation never assigns a material";
}

TEST(QtMaterialBrowserWindowTests,
     EmbeddedUsageKeyboardDismissalAndStaleChoicesNeverAssign) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-embedded-filter-keyboard-tests";
  char *argv[] = {name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  QWidget owner;
  owner.setAttribute(Qt::WA_DontShowOnScreen, true);
  owner.resize(380, 480);
  QtMaterialBrowserWindow browser(&owner, QtMaterialBrowserHost::Embedded);
  browser.resize(owner.size());
  browser.set_project_generation(7);
  const auto material = folder_material(
      "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa", QStringLiteral("Oak"),
      QStringLiteral("Materials/oak.qmat"));
  browser.set_materials({material});
  browser.set_material_usage({
      .project_generation = 7,
      .revision = 1,
      .complete = true,
      .used_materials = {material.reference},
  });
  int assignments = 0;
  browser.set_assign_material_callback(
      [&](QuaderMaterialReference) { ++assignments; });
  browser.set_assignment_available(true);
  owner.show();
  browser.show();
  application.processEvents();

  auto *filter = browser.findChild<QToolButton *>(
      QStringLiteral("materialBrowser.filter"));
  auto *remove = browser.findChild<QToolButton *>(
      QStringLiteral("materialBrowser.usageChipRemove"));
  ASSERT_NE(filter, nullptr);
  ASSERT_NE(remove, nullptr);
  filter->click();
  QPointer<QFrame> popup = browser.findChild<QFrame *>(
      QStringLiteral("materialBrowser.filterPopup"));
  ASSERT_NE(popup, nullptr);
  auto *used = popup->findChild<QCheckBox *>(
      QStringLiteral("materialBrowser.filterUsed"));
  ASSERT_NE(used, nullptr);
  send_key(*used, Qt::Key_Space);
  EXPECT_EQ(browser.usage_mode(), QtMaterialBrowserUsageMode::Used);
  EXPECT_TRUE(popup.isNull() || !popup->isVisible());
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  EXPECT_EQ(owner.focusWidget(), filter);

  filter->click();
  popup = browser.findChild<QFrame *>(
      QStringLiteral("materialBrowser.filterPopup"));
  ASSERT_NE(popup, nullptr);
  used = popup->findChild<QCheckBox *>(
      QStringLiteral("materialBrowser.filterUsed"));
  ASSERT_NE(used, nullptr);
  EXPECT_TRUE(used->isChecked());
  send_key(*used, Qt::Key_Escape);
  EXPECT_TRUE(popup.isNull() || !popup->isVisible());
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  EXPECT_EQ(browser.usage_mode(), QtMaterialBrowserUsageMode::Used);
  EXPECT_TRUE(filter->isChecked());
  EXPECT_EQ(owner.focusWidget(), filter);

  remove->setFocus(Qt::TabFocusReason);
  send_key(*remove, Qt::Key_Space);
  EXPECT_EQ(browser.usage_mode(), QtMaterialBrowserUsageMode::All);
  EXPECT_EQ(owner.focusWidget(), filter);

  filter->click();
  popup = browser.findChild<QFrame *>(
      QStringLiteral("materialBrowser.filterPopup"));
  ASSERT_NE(popup, nullptr);
  QPointer<QCheckBox> stale_used = popup->findChild<QCheckBox *>(
      QStringLiteral("materialBrowser.filterUsed"));
  ASSERT_NE(stale_used, nullptr);
  browser.set_material_usage({
      .project_generation = 7,
      .revision = 2,
      .complete = true,
      .used_materials = {material.reference},
  });
  ASSERT_NE(stale_used, nullptr);
  stale_used->click();
  EXPECT_EQ(browser.usage_mode(), QtMaterialBrowserUsageMode::All)
      << "queued intent from the previous usage revision is rejected";
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

  filter->click();
  popup = browser.findChild<QFrame *>(
      QStringLiteral("materialBrowser.filterPopup"));
  ASSERT_NE(popup, nullptr);
  stale_used = popup->findChild<QCheckBox *>(
      QStringLiteral("materialBrowser.filterUsed"));
  ASSERT_NE(stale_used, nullptr);
  browser.set_project_generation(8);
  ASSERT_NE(stale_used, nullptr);
  stale_used->click();
  EXPECT_EQ(browser.usage_mode(), QtMaterialBrowserUsageMode::All)
      << "a closed popup cannot apply its previous project's choice";
  EXPECT_EQ(assignments, 0);
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

TEST(QtMaterialBrowserWindowTests,
     EmbeddedUsageBadgeScalesAndRepaintsForLivePalettes) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-embedded-filter-render-tests";
  char *argv[] = {name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  QtUiScaleApplicationController scale_controller(application);
  const QPalette light_palette = QApplication::style()->standardPalette();

  QWidget owner;
  owner.setAttribute(Qt::WA_DontShowOnScreen, true);
  QtMaterialBrowserWindow browser(&owner, QtMaterialBrowserHost::Embedded);
  browser.restore_presentation_state(QtMaterialBrowserViewMode::Grid, 64,
                                     false, 120);
  browser.set_project_generation(1);
  const auto material = folder_material(
      "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa", QStringLiteral("Oak"),
      QStringLiteral("Materials/oak.qmat"));
  browser.set_materials({material});
  browser.set_material_usage({
      .project_generation = 1,
      .revision = 1,
      .complete = true,
      .used_materials = {material.reference},
  });
  owner.resize(380, 320);
  browser.resize(owner.size());
  owner.show();
  browser.show();
  application.processEvents();
  auto *filter = browser.findChild<QToolButton *>(
      QStringLiteral("materialBrowser.filter"));
  ASSERT_NE(filter, nullptr);
  filter->click();
  auto *used = browser.findChild<QCheckBox *>(
      QStringLiteral("materialBrowser.filterUsed"));
  ASSERT_NE(used, nullptr);
  used->click();
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

  auto *row = browser.findChild<QWidget *>(
      QStringLiteral("materialBrowser.activeFilters"));
  auto *filtered_by = browser.findChild<QLabel *>(
      QStringLiteral("materialBrowser.filteredBy"));
  auto *badge = browser.findChild<QWidget *>(
      QStringLiteral("materialBrowser.usageChip"));
  auto *label = browser.findChild<QLabel *>(
      QStringLiteral("materialBrowser.usageChipLabel"));
  auto *remove = browser.findChild<QToolButton *>(
      QStringLiteral("materialBrowser.usageChipRemove"));
  ASSERT_NE(row, nullptr);
  ASSERT_NE(filtered_by, nullptr);
  ASSERT_NE(badge, nullptr);
  ASSERT_NE(label, nullptr);
  ASSERT_NE(remove, nullptr);
  const QString screenshot_directory =
      qEnvironmentVariable("QUADER_MATERIAL_FILTER_BADGE_SCREENSHOT_DIR");
  if (!screenshot_directory.isEmpty()) {
    ASSERT_TRUE(QDir().mkpath(screenshot_directory));
  }

  QColor dark_badge_background;
  for (const QString &theme : {QStringLiteral("dark"), QStringLiteral("light"),
                               QStringLiteral("high-contrast")}) {
    SCOPED_TRACE(theme.toStdString());
    QPalette palette = theme == QStringLiteral("dark")
                           ? qt_obsidian_application_palette(light_palette)
                           : light_palette;
    const bool high_contrast = theme == QStringLiteral("high-contrast");
    if (high_contrast) {
      QApplication::setPalette(QPalette{});
      palette.setColor(QPalette::Window, Qt::black);
      palette.setColor(QPalette::WindowText, Qt::white);
      palette.setColor(QPalette::Button, Qt::black);
      palette.setColor(QPalette::ButtonText, Qt::white);
      palette.setColor(QPalette::Base, Qt::black);
      palette.setColor(QPalette::Text, Qt::white);
      palette.setColor(QPalette::Accent, Qt::yellow);
      browser.setPalette(palette);
    } else {
      palette.setColor(QPalette::Accent, QColor(110, 92, 185));
      QApplication::setPalette(palette);
      browser.setPalette(QPalette{});
    }
    for (const float factor : {1.0F, 1.5F, 2.0F}) {
      SCOPED_TRACE(factor);
      scale_controller.apply_factor(factor);
      const QtUiScaleMetrics metrics = scale_controller.metrics();
      browser.apply_ui_scale(metrics);
      owner.resize(metrics.size(QSize(380, 320)));
      browser.resize(owner.size());
      // Live application-font changes invalidate nested layouts in successive
      // event-loop turns. Let the badge, row and browser settle before capture.
      for (int layout_turn = 0; layout_turn < 3; ++layout_turn) {
        application.processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
      }
      EXPECT_EQ(remove->size(), metrics.size(QSize(20, 20)));
      EXPECT_EQ(remove->iconSize(), metrics.size(QSize(12, 12)));
      EXPECT_GE(remove->geometry().left(), label->geometry().right());
      EXPECT_GE(label->width(), label->fontMetrics().horizontalAdvance(label->text()));
      EXPECT_GE(filtered_by->width(),
                filtered_by->fontMetrics().horizontalAdvance(filtered_by->text()));
      EXPECT_EQ(label->palette().color(QPalette::WindowText),
                browser.palette().color(QPalette::WindowText));
      const QImage close_icon = remove->icon().pixmap(remove->iconSize()).toImage();
      ASSERT_FALSE(close_icon.isNull());
      bool visible_close = false;
      for (int y = 0; y < close_icon.height(); ++y) {
        for (int x = 0; x < close_icon.width(); ++x) {
          const QColor pixel = close_icon.pixelColor(x, y);
          if (pixel.alpha() == 255) {
            visible_close = true;
            EXPECT_EQ(pixel.rgb(), browser.palette().color(QPalette::WindowText).rgb());
          }
        }
      }
      EXPECT_TRUE(visible_close);

      QImage badge_render(badge->size(), QImage::Format_ARGB32_Premultiplied);
      badge_render.fill(Qt::transparent);
      badge->render(&badge_render);
      const QColor background = badge_render.pixelColor(
          metrics.px(3), badge_render.height() / 2);
      EXPECT_EQ(background.alpha(), 255);
      if (high_contrast) {
        EXPECT_EQ(background, palette.color(QPalette::Window))
            << "high contrast must not blend an accent into system colors";
      } else {
        EXPECT_NE(background, palette.color(QPalette::Window));
        if (factor == 1.0F && theme == QStringLiteral("dark")) {
          dark_badge_background = background;
        } else if (factor == 1.0F) {
          EXPECT_NE(background, dark_badge_background)
              << "an existing badge repaints when its palette changes";
        }
      }
      if (!screenshot_directory.isEmpty()) {
        QImage preview(browser.size(), QImage::Format_ARGB32_Premultiplied);
        preview.fill(Qt::transparent);
        browser.render(&preview);
        const int bottom = row->mapTo(&browser, QPoint{}).y() + row->height() +
                           metrics.px(8);
        const QString filename = QStringLiteral("filter-badge-%1-%2.png")
                                     .arg(theme)
                                     .arg(qRound(factor * 100));
        ASSERT_TRUE(preview.copy(0, 0, preview.width(), bottom)
                        .save(QDir(screenshot_directory).filePath(filename)));
      }
      const qint64 retained_icon = remove->icon().cacheKey();
      browser.apply_ui_scale(metrics);
      EXPECT_EQ(remove->icon().cacheKey(), retained_icon)
          << "reapplying an unchanged scale retains the icon";
    }
  }
}

TEST(QtMaterialBrowserWindowTests,
     CatalogRefreshKeepsRestoredBatchSeparateFromNewBrowserSelection) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-material-browser-selection-refresh-tests";
  char *argv[] = {name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  QtMaterialBrowserWindow window;
  window.setAttribute(Qt::WA_DontShowOnScreen, true);
  auto oak = folder_material("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa",
                             QStringLiteral("Oak"),
                             QStringLiteral("Materials/oak.qmat"));
  auto steel = folder_material("bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb",
                               QStringLiteral("Steel"),
                               QStringLiteral("Materials/steel.qmat"));
  auto oak_copy = folder_material("cccccccc-cccc-cccc-cccc-cccccccccccc",
                                  QStringLiteral("Oak Copy"),
                                  QStringLiteral("Materials/oak_copy.qmat"));
  window.set_materials({oak, steel}, oak.reference);
  auto *grid = window.findChild<QListView *>(
      QStringLiteral("materialBrowser.gridView"));
  auto *accept = window.findChild<QPushButton *>(
      QStringLiteral("materialBrowser.accept"));
  ASSERT_NE(grid, nullptr);
  ASSERT_NE(accept, nullptr);
  const auto index_for = [grid](const QString &stable_id) {
    for (int row = 0; row < grid->model()->rowCount(); ++row) {
      const QModelIndex index = grid->model()->index(row, 0);
      if (index.data(QtMaterialBrowserModel::StableIdRole).toString() ==
          stable_id) {
        return index;
      }
    }
    return QModelIndex{};
  };
  const QModelIndex oak_index = index_for(oak.stable_id);
  const QModelIndex steel_index = index_for(steel.stable_id);
  ASSERT_TRUE(oak_index.isValid());
  ASSERT_TRUE(steel_index.isValid());
  QItemSelection batch;
  batch.select(oak_index, oak_index);
  batch.select(steel_index, steel_index);
  grid->selectionModel()->select(batch, QItemSelectionModel::ClearAndSelect |
                                            QItemSelectionModel::Rows);
  grid->selectionModel()->setCurrentIndex(steel_index,
                                          QItemSelectionModel::NoUpdate);

  window.set_materials({oak, steel, oak_copy}, oak_copy.reference);

  const QModelIndexList selected =
      grid->selectionModel()->selectedRows(QtMaterialBrowserModel::NameColumn);
  ASSERT_EQ(selected.size(), 2);
  QStringList selected_ids;
  for (const QModelIndex &index : selected) {
    selected_ids.push_back(
        index.data(QtMaterialBrowserModel::StableIdRole).toString());
  }
  EXPECT_TRUE(selected_ids.contains(oak.stable_id));
  EXPECT_TRUE(selected_ids.contains(steel.stable_id));
  EXPECT_FALSE(selected_ids.contains(oak_copy.stable_id));
  EXPECT_EQ(grid->currentIndex()
                .data(QtMaterialBrowserModel::StableIdRole)
                .toString(),
            oak_copy.stable_id);
  EXPECT_EQ(window.selected_material(), oak_copy.reference);
  EXPECT_FALSE(accept->isEnabled());
  EXPECT_TRUE(accept->isHidden());
}

TEST(QtMaterialBrowserWindowTests,
     ContextActionsRejectClipboardProtectionAndCallbackChanges) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-material-browser-action-fence-tests";
  char *argv[] = {name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  QtMaterialBrowserWindow window;
  window.setAttribute(Qt::WA_DontShowOnScreen, true);
  auto oak = folder_material("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa",
                             QStringLiteral("Oak"),
                             QStringLiteral("Materials/oak.qmat"));
  oak.encoded_albedo = encoded_wide_preview();
  oak.has_valid_albedo = true;
  oak.editable = true;
  window.set_project_generation(81);
  window.set_material_usage({
      .project_generation = 81,
      .revision = 12,
      .complete = true,
  });
  window.set_dependency_revision(12);
  window.set_materials({oak}, oak.reference);
  window.set_material_paste_available(true);
  int first_callback_count = 0;
  int replacement_callback_count = 0;
  window.set_material_action_callback(
      [&](QtMaterialBrowserActionRequest) { ++first_callback_count; });
  window.resize(720, 480);
  window.show();
  QCoreApplication::processEvents();

  auto *grid = window.findChild<QListView *>(
      QStringLiteral("materialBrowser.gridView"));
  ASSERT_NE(grid, nullptr);
  const QModelIndex oak_index = grid->model()->index(0, 0);
  ASSERT_TRUE(oak_index.isValid());
  const auto send_context_menu = [&]() {
    const QPoint position = grid->visualRect(oak_index).center();
    QContextMenuEvent event(QContextMenuEvent::Mouse, position,
                            grid->viewport()->mapToGlobal(position));
    QCoreApplication::sendEvent(grid->viewport(), &event);
  };

  QTimer::singleShot(0, &window, [&]() {
    auto *menu = window.findChild<QMenu *>(
        QStringLiteral("materialBrowser.contextMenu"));
    ASSERT_NE(menu, nullptr);
    auto *paste = menu->findChild<QAction *>(
        QStringLiteral("materialBrowser.action.paste"));
    ASSERT_NE(paste, nullptr);
    ASSERT_TRUE(paste->isEnabled());
    window.set_material_paste_available(false);
    menu->setActiveAction(paste);
    QKeyEvent accept(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(menu, &accept);
  });
  send_context_menu();
  EXPECT_EQ(first_callback_count, 0)
      << "clipboard eligibility changed inside QMenu::exec";

  window.set_material_paste_available(true);
  QTimer::singleShot(0, &window, [&]() {
    auto *menu = window.findChild<QMenu *>(
        QStringLiteral("materialBrowser.contextMenu"));
    ASSERT_NE(menu, nullptr);
    auto *remove = menu->findChild<QAction *>(
        QStringLiteral("materialBrowser.action.delete"));
    ASSERT_NE(remove, nullptr);
    ASSERT_TRUE(remove->isEnabled());
    window.set_protected_material(oak.reference);
    menu->setActiveAction(remove);
    QKeyEvent accept(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(menu, &accept);
  });
  send_context_menu();
  EXPECT_EQ(first_callback_count, 0)
      << "protected-material eligibility changed inside QMenu::exec";

  window.set_protected_material({});
  QTimer::singleShot(0, &window, [&]() {
    auto *menu = window.findChild<QMenu *>(
        QStringLiteral("materialBrowser.contextMenu"));
    ASSERT_NE(menu, nullptr);
    auto *copy = menu->findChild<QAction *>(
        QStringLiteral("materialBrowser.action.copy"));
    ASSERT_NE(copy, nullptr);
    window.set_material_action_callback([&](QtMaterialBrowserActionRequest) {
      ++replacement_callback_count;
    });
    menu->setActiveAction(copy);
    QKeyEvent accept(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(menu, &accept);
  });
  send_context_menu();
  EXPECT_EQ(first_callback_count, 0);
  EXPECT_EQ(replacement_callback_count, 0)
      << "replacing the action owner inside QMenu::exec must reject intent";

  window.set_material_action_callback(
      [&](QtMaterialBrowserActionRequest) { ++first_callback_count; });
  QTimer::singleShot(0, &window, [&]() {
    auto *menu = window.findChild<QMenu *>(
        QStringLiteral("materialBrowser.contextMenu"));
    ASSERT_NE(menu, nullptr);
    auto *rename = menu->findChild<QAction *>(
        QStringLiteral("materialBrowser.action.rename"));
    ASSERT_NE(rename, nullptr);
    QTimer::singleShot(0, &window, [&]() {
      auto *dialog = window.findChild<QInputDialog *>(
          QStringLiteral("materialBrowser.renameDialog"));
      ASSERT_NE(dialog, nullptr);
      dialog->setTextValue(QStringLiteral("Oak Renamed"));
      window.set_protected_material(oak.reference);
      dialog->accept();
    });
    menu->setActiveAction(rename);
    QKeyEvent accept(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(menu, &accept);
  });
  send_context_menu();
  EXPECT_EQ(first_callback_count, 0)
      << "rename eligibility changed inside QInputDialog::exec";
}

TEST(QtMaterialBrowserWindowTests,
     NestedMenusAndDialogsPermitBrowserDestruction) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-material-browser-nested-destruction-tests";
  char *argv[] = {name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  bool saw_filter = false;
  {
    QPointer<QtMaterialBrowserWindow> window =
        new QtMaterialBrowserWindow();
    window->setAttribute(Qt::WA_DontShowOnScreen, true);
    auto *filter = window->findChild<QToolButton *>(
        QStringLiteral("materialBrowser.filter"));
    ASSERT_NE(filter, nullptr);
    filter->click();
    saw_filter = window->findChild<QFrame *>(
                     QStringLiteral("materialBrowser.filterPopup")) !=
                 nullptr;
    delete window.data();
    EXPECT_TRUE(window.isNull());
  }
  EXPECT_TRUE(saw_filter);

  for (const bool dismiss_with_escape : {false, true}) {
    SCOPED_TRACE(dismiss_with_escape);
    QWidget owner;
    owner.setAttribute(Qt::WA_DontShowOnScreen, true);
    owner.resize(380, 480);
    QPointer<QtMaterialBrowserWindow> browser =
        new QtMaterialBrowserWindow(&owner, QtMaterialBrowserHost::Embedded);
    browser->resize(owner.size());
    browser->set_project_generation(5);
    browser->set_material_usage({
        .project_generation = 5,
        .revision = 1,
        .complete = true,
    });
    int assignments = 0;
    browser->set_assign_material_callback(
        [&](QuaderMaterialReference) { ++assignments; });
    browser->set_assignment_available(true);
    owner.show();
    browser->show();
    application.processEvents();
    auto *filter = browser->findChild<QToolButton *>(
        QStringLiteral("materialBrowser.filter"));
    ASSERT_NE(filter, nullptr);
    filter->click();
    QPointer<QFrame> first_popup = browser->findChild<QFrame *>(
        QStringLiteral("materialBrowser.filterPopup"));
    ASSERT_NE(first_popup, nullptr);
    QPointer<QCheckBox> previous_used = first_popup->findChild<QCheckBox *>(
        QStringLiteral("materialBrowser.filterUsed"));
    ASSERT_NE(previous_used, nullptr);
    previous_used->click();
    EXPECT_FALSE(first_popup->isVisible());
    EXPECT_EQ(browser->usage_mode(), QtMaterialBrowserUsageMode::Used);

    filter->click();
    EXPECT_TRUE(first_popup.isNull())
        << "immediate reopening retires the previous pending-delete popup";
    EXPECT_TRUE(previous_used.isNull());
    QPointer<QFrame> reopened_popup = browser->findChild<QFrame *>(
        QStringLiteral("materialBrowser.filterPopup"));
    ASSERT_NE(reopened_popup, nullptr);
    auto *used = reopened_popup->findChild<QCheckBox *>(
        QStringLiteral("materialBrowser.filterUsed"));
    ASSERT_NE(used, nullptr);
    EXPECT_TRUE(used->isChecked());
    if (dismiss_with_escape) {
      QKeyEvent press(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
      QKeyEvent release(QEvent::KeyRelease, Qt::Key_Escape, Qt::NoModifier);
      QCoreApplication::sendEvent(used, &press);
      QCoreApplication::sendEvent(used, &release);
    } else {
      used->click();
    }
    EXPECT_FALSE(reopened_popup->isVisible());
    EXPECT_EQ(browser->usage_mode(), QtMaterialBrowserUsageMode::Used);
    delete browser.data();
    EXPECT_TRUE(browser.isNull());
    EXPECT_TRUE(reopened_popup.isNull())
        << "browser destruction owns popup teardown before DeferredDelete runs";
    EXPECT_EQ(assignments, 0);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }

  const auto make_window = []() {
    QPointer<QtMaterialBrowserWindow> window =
        new QtMaterialBrowserWindow();
    window->setAttribute(Qt::WA_DontShowOnScreen, true);
    auto oak = folder_material("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa",
                               QStringLiteral("Oak"),
                               QStringLiteral("Materials/oak.qmat"));
    oak.editable = true;
    window->set_materials({oak}, oak.reference);
    window->set_material_action_callback(
        [](QtMaterialBrowserActionRequest) {});
    window->resize(720, 480);
    window->show();
    QCoreApplication::processEvents();
    return window;
  };
  const auto send_context_menu = [](QListView *grid) {
    const QModelIndex index = grid->model()->index(0, 0);
    const QPoint position = grid->visualRect(index).center();
    QContextMenuEvent event(QContextMenuEvent::Mouse, position,
                            grid->viewport()->mapToGlobal(position));
    QCoreApplication::sendEvent(grid->viewport(), &event);
  };

  bool saw_menu = false;
  {
    QPointer<QtMaterialBrowserWindow> window = make_window();
    auto *grid = window->findChild<QListView *>(
        QStringLiteral("materialBrowser.gridView"));
    ASSERT_NE(grid, nullptr);
    QTimer::singleShot(0, [&]() {
      if (!window) {
        return;
      }
      saw_menu = window->findChild<QMenu *>(
                     QStringLiteral("materialBrowser.contextMenu")) !=
                 nullptr;
      delete window.data();
    });
    send_context_menu(grid);
    EXPECT_TRUE(window.isNull());
  }
  EXPECT_TRUE(saw_menu);

  bool saw_rename = false;
  {
    QPointer<QtMaterialBrowserWindow> window = make_window();
    auto *grid = window->findChild<QListView *>(
        QStringLiteral("materialBrowser.gridView"));
    ASSERT_NE(grid, nullptr);
    QTimer::singleShot(0, [&]() {
      if (!window) {
        return;
      }
      auto *menu = window->findChild<QMenu *>(
          QStringLiteral("materialBrowser.contextMenu"));
      if (menu == nullptr) {
        delete window.data();
        return;
      }
      auto *rename = menu->findChild<QAction *>(
          QStringLiteral("materialBrowser.action.rename"));
      if (rename == nullptr) {
        delete window.data();
        return;
      }
      QTimer::singleShot(0, [&]() {
        if (!window) {
          return;
        }
        saw_rename = window->findChild<QInputDialog *>(
                         QStringLiteral("materialBrowser.renameDialog")) !=
                     nullptr;
        delete window.data();
      });
      menu->setActiveAction(rename);
      QKeyEvent accept(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
      QApplication::sendEvent(menu, &accept);
    });
    send_context_menu(grid);
    EXPECT_TRUE(window.isNull());
  }
  EXPECT_TRUE(saw_rename);
}

TEST(QtMaterialBrowserWindowTests,
     BothHostsEditMaterialsThroughStableContextMenuCallbacks) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-material-edit-context-tests";
  char *argv[] = {name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  for (const auto host : {QtMaterialBrowserHost::Windowed,
                          QtMaterialBrowserHost::Embedded}) {
    SCOPED_TRACE(static_cast<int>(host));
    QWidget owner;
    owner.setAttribute(Qt::WA_DontShowOnScreen, true);
    owner.resize(720, 480);
    QtMaterialBrowserWindow browser(&owner, host);
    browser.setAttribute(Qt::WA_DontShowOnScreen, true);
    browser.resize(720, 480);
    const auto oak = folder_material(
        "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa", QStringLiteral("Oak"),
        QStringLiteral("Materials/oak.qmat"));
    browser.set_project_generation(81);
    browser.set_dependency_revision(12);
    browser.set_material_usage({
        .project_generation = 81,
        .revision = 100,
        .complete = true,
    });
    browser.set_materials({oak}, oak.reference);
    browser.set_assignment_available(true);
    int assignments = 0;
    int opens = 0;
    int replacement_opens = 0;
    browser.set_assign_material_callback(
        [&](QuaderMaterialReference) { ++assignments; });
    browser.set_open_material_callback([&](QuaderMaterialReference reference) {
      EXPECT_EQ(reference, oak.reference);
      ++opens;
    });
    owner.show();
    browser.show();
    application.processEvents();
    auto *grid = browser.findChild<QListView *>(
        QStringLiteral("materialBrowser.gridView"));
    ASSERT_NE(grid, nullptr);
    const auto send_context_menu = [&]() {
      const QPoint position =
          grid->visualRect(grid->model()->index(0, 0)).center();
      QContextMenuEvent event(QContextMenuEvent::Mouse, position,
                              grid->viewport()->mapToGlobal(position));
      QCoreApplication::sendEvent(grid->viewport(), &event);
    };
    const auto choose_edit = [](QMenu &menu, QAction &action) {
      menu.setActiveAction(&action);
      QKeyEvent accept(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
      QApplication::sendEvent(&menu, &accept);
    };

    QTimer::singleShot(0, &browser, [&]() {
      auto *menu = browser.findChild<QMenu *>(
          QStringLiteral("materialBrowser.contextMenu"));
      ASSERT_NE(menu, nullptr);
      auto *edit = menu->findChild<QAction *>(
          QStringLiteral("materialBrowser.action.editMaterial"));
      ASSERT_NE(edit, nullptr);
      EXPECT_EQ(edit->text(), QStringLiteral("Edit Material"));
      ASSERT_TRUE(edit->isEnabled());
      choose_edit(*menu, *edit);
    });
    send_context_menu();
    EXPECT_EQ(opens, 1)
        << "editing uses the existing open callback without an action owner";
    EXPECT_EQ(assignments, 0);

    QTimer::singleShot(0, &browser, [&]() {
      auto *menu = browser.findChild<QMenu *>(
          QStringLiteral("materialBrowser.contextMenu"));
      ASSERT_NE(menu, nullptr);
      auto *edit = menu->findChild<QAction *>(
          QStringLiteral("materialBrowser.action.editMaterial"));
      ASSERT_NE(edit, nullptr);
      browser.set_material_usage({
          .project_generation = 81,
          .revision = 101,
          .complete = true,
          .used_materials = {oak.reference},
      });
      choose_edit(*menu, *edit);
    });
    send_context_menu();
    EXPECT_EQ(opens, 2)
        << "usage presentation changes do not change dependency identity";

    ASSERT_TRUE(QMetaObject::invokeMethod(
        grid, "doubleClicked", Qt::DirectConnection,
        Q_ARG(QModelIndex, grid->currentIndex())));
    EXPECT_EQ(opens, 3);
    EXPECT_EQ(assignments, 0);

    QTimer::singleShot(0, &browser, [&]() {
      auto *menu = browser.findChild<QMenu *>(
          QStringLiteral("materialBrowser.contextMenu"));
      ASSERT_NE(menu, nullptr);
      auto *edit = menu->findChild<QAction *>(
          QStringLiteral("materialBrowser.action.editMaterial"));
      ASSERT_NE(edit, nullptr);
      browser.set_dependency_revision(13);
      choose_edit(*menu, *edit);
    });
    send_context_menu();
    EXPECT_EQ(opens, 3);

    QTimer::singleShot(0, &browser, [&]() {
      auto *menu = browser.findChild<QMenu *>(
          QStringLiteral("materialBrowser.contextMenu"));
      ASSERT_NE(menu, nullptr);
      auto *edit = menu->findChild<QAction *>(
          QStringLiteral("materialBrowser.action.editMaterial"));
      ASSERT_NE(edit, nullptr);
      browser.set_open_material_callback(
          [&](QuaderMaterialReference) { ++replacement_opens; });
      choose_edit(*menu, *edit);
    });
    send_context_menu();
    EXPECT_EQ(opens, 3);
    EXPECT_EQ(replacement_opens, 0)
        << "a replacement callback cannot receive intent from an old menu";

    QTimer::singleShot(0, &browser, [&]() {
      auto *menu = browser.findChild<QMenu *>(
          QStringLiteral("materialBrowser.contextMenu"));
      ASSERT_NE(menu, nullptr);
      auto *edit = menu->findChild<QAction *>(
          QStringLiteral("materialBrowser.action.editMaterial"));
      ASSERT_NE(edit, nullptr);
      browser.set_project_generation(82);
      choose_edit(*menu, *edit);
    });
    send_context_menu();
    EXPECT_EQ(opens, 3);
    EXPECT_EQ(replacement_opens, 0);
    EXPECT_EQ(assignments, 0);
  }
}

TEST(QtMaterialBrowserWindowTests,
     MaterialContextMenusPreserveBatchSelectionAndEmitStableIntent) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-material-context-menu-tests";
  char *argv[] = {name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  QtMaterialBrowserWindow window;
  window.setAttribute(Qt::WA_DontShowOnScreen, true);
  window.set_project_generation(71);
  window.set_material_usage({
      .project_generation = 71,
      .revision = 119,
      .complete = true,
  });
  window.set_dependency_revision(19);
  auto default_material =
      folder_material("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa",
                      QStringLiteral("Default Material"),
                      QStringLiteral("Materials/default_material.qmat"));
  auto oak = folder_material("bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb",
                             QStringLiteral("Oak"),
                             QStringLiteral("Materials/Wood/oak.qmat"));
  auto steel = folder_material("cccccccc-cccc-cccc-cccc-cccccccccccc",
                               QStringLiteral("Steel"),
                               QStringLiteral("Materials/Metal/steel.qmat"));
  auto corrupt = folder_material("dddddddd-dddd-dddd-dddd-dddddddddddd",
                                 QStringLiteral("Corrupt"),
                                 QStringLiteral("Materials/corrupt.qmat"));
  for (QtMaterialBrowserItem *item : {&default_material, &oak, &steel,
                                     &corrupt}) {
    item->encoded_albedo = encoded_wide_preview();
    item->has_valid_albedo = true;
    item->editable = true;
  }
  corrupt.encoded_albedo = QByteArray("this is not an encoded image");
  corrupt.thumbnail = make_qt_material_thumbnail(corrupt);
  ASSERT_NE(corrupt.thumbnail, nullptr);
  EXPECT_FALSE(corrupt.thumbnail->source_image_valid);
  EXPECT_FALSE(corrupt.thumbnail->image.isNull());
  corrupt.has_valid_albedo = corrupt.thumbnail->source_image_valid;
  steel.editable = false;
  window.set_materials({default_material, oak, steel, corrupt});
  window.set_protected_material(default_material.reference);
  window.set_material_paste_available(true);
  std::vector<QtMaterialBrowserActionRequest> requests;
  window.set_material_action_callback(
      [&](QtMaterialBrowserActionRequest request) {
        requests.push_back(std::move(request));
      });
  window.resize(900, 560);
  window.show();
  QCoreApplication::processEvents();

  auto *grid =
      window.findChild<QListView *>(QStringLiteral("materialBrowser.gridView"));
  auto *list = window.findChild<QTableView *>(
      QStringLiteral("materialBrowser.listView"));
  auto *list_button =
      window.findChild<QToolButton *>(QStringLiteral("materialBrowser.list"));
  ASSERT_NE(grid, nullptr);
  ASSERT_NE(list, nullptr);
  ASSERT_NE(list_button, nullptr);
  EXPECT_EQ(grid->selectionBehavior(), QAbstractItemView::SelectRows);
  const auto index_for = [](QAbstractItemView &view, const QString &stable_id) {
    for (int row = 0; row < view.model()->rowCount(); ++row) {
      const QModelIndex index = view.model()->index(row, 0);
      if (index.data(QtMaterialBrowserModel::StableIdRole).toString() ==
          stable_id) {
        return index;
      }
    }
    return QModelIndex{};
  };
  const auto send_context_menu = [](QAbstractItemView &view,
                                    const QPoint &position) {
    QContextMenuEvent event(QContextMenuEvent::Mouse, position,
                            view.viewport()->mapToGlobal(position));
    QCoreApplication::sendEvent(view.viewport(), &event);
  };

  const QModelIndex oak_grid = index_for(*grid, oak.stable_id);
  const QModelIndex steel_grid = index_for(*grid, steel.stable_id);
  ASSERT_TRUE(oak_grid.isValid());
  ASSERT_TRUE(steel_grid.isValid());
  QItemSelection batch;
  batch.select(oak_grid, oak_grid);
  batch.select(steel_grid, steel_grid);
  grid->selectionModel()->select(batch, QItemSelectionModel::ClearAndSelect |
                                            QItemSelectionModel::Rows);
  grid->selectionModel()->setCurrentIndex(steel_grid,
                                          QItemSelectionModel::NoUpdate);

  bool inspected_order = false;
  QTimer::singleShot(0, &window, [&]() {
    auto *menu = window.findChild<QMenu *>(
        QStringLiteral("materialBrowser.contextMenu"));
    ASSERT_NE(menu, nullptr);
    QList<QAction *> actions = menu->actions();
    auto *edit = menu->findChild<QAction *>(
        QStringLiteral("materialBrowser.action.editMaterial"));
    ASSERT_NE(edit, nullptr);
    EXPECT_FALSE(edit->isEnabled());
    EXPECT_TRUE(actions.removeOne(edit));
    ASSERT_EQ(actions.size(), 13);
    EXPECT_EQ(actions[0]->objectName(),
              QStringLiteral("materialBrowser.action.copyTextureName"));
    EXPECT_EQ(actions[1]->objectName(),
              QStringLiteral("materialBrowser.action.copyTexturePath"));
    EXPECT_TRUE(actions[2]->isSeparator());
    EXPECT_EQ(actions[3]->objectName(),
              QStringLiteral("materialBrowser.action.openFileLocation"));
    EXPECT_EQ(actions[4]->objectName(),
              QStringLiteral("materialBrowser.action.showTextureInfo"));
    EXPECT_TRUE(actions[5]->isSeparator());
    EXPECT_EQ(actions[6]->objectName(),
              QStringLiteral("materialBrowser.action.rename"));
    EXPECT_EQ(actions[7]->objectName(),
              QStringLiteral("materialBrowser.action.duplicate"));
    EXPECT_EQ(actions[8]->objectName(),
              QStringLiteral("materialBrowser.action.cut"));
    EXPECT_EQ(actions[9]->objectName(),
              QStringLiteral("materialBrowser.action.copy"));
    EXPECT_EQ(actions[10]->objectName(),
              QStringLiteral("materialBrowser.action.paste"));
    EXPECT_TRUE(actions[11]->isSeparator());
    EXPECT_EQ(actions[12]->objectName(),
              QStringLiteral("materialBrowser.action.delete"));
    for (int index : {0, 1, 3, 4, 6, 7, 8, 9, 10, 12}) {
      EXPECT_TRUE(actions[index]->isEnabled());
    }
    inspected_order = true;
    menu->setActiveAction(actions[9]);
    QKeyEvent accept(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(menu, &accept);
  });
  send_context_menu(*grid, grid->visualRect(steel_grid).center());
  EXPECT_TRUE(inspected_order);
  ASSERT_EQ(requests.size(), 1);
  EXPECT_EQ(requests.back().action, QtMaterialBrowserAction::Copy);
  EXPECT_EQ(requests.back().target, steel.reference);
  EXPECT_EQ(requests.back().selected_materials.size(), 2);
  EXPECT_EQ(requests.back().destination_folder, QStringLiteral("Materials"));
  EXPECT_EQ(requests.back().project_generation, 71);
  EXPECT_EQ(requests.back().dependency_revision, 19);

  const QModelIndex corrupt_grid = index_for(*grid, corrupt.stable_id);
  ASSERT_TRUE(corrupt_grid.isValid());
  grid->selectionModel()->setCurrentIndex(
      corrupt_grid, QItemSelectionModel::ClearAndSelect |
                        QItemSelectionModel::Rows);
  QTimer::singleShot(0, &window, [&]() {
    auto *menu = window.findChild<QMenu *>(
        QStringLiteral("materialBrowser.contextMenu"));
    ASSERT_NE(menu, nullptr);
    QList<QAction *> actions = menu->actions();
    auto *edit = menu->findChild<QAction *>(
        QStringLiteral("materialBrowser.action.editMaterial"));
    ASSERT_NE(edit, nullptr);
    EXPECT_TRUE(actions.removeOne(edit));
    ASSERT_EQ(actions.size(), 13);
    for (int index : {0, 1, 3, 4}) {
      EXPECT_FALSE(actions[index]->isEnabled());
    }
    for (int index : {6, 7, 8, 9, 10, 12}) {
      EXPECT_TRUE(actions[index]->isEnabled());
    }
    menu->close();
  });
  send_context_menu(*grid, grid->visualRect(corrupt_grid).center());
  EXPECT_EQ(requests.size(), 1);

  const QModelIndex default_grid = index_for(*grid, default_material.stable_id);
  ASSERT_TRUE(default_grid.isValid());
  batch.clear();
  batch.select(default_grid, default_grid);
  batch.select(oak_grid, oak_grid);
  grid->selectionModel()->select(batch, QItemSelectionModel::ClearAndSelect |
                                            QItemSelectionModel::Rows);
  grid->selectionModel()->setCurrentIndex(oak_grid,
                                          QItemSelectionModel::NoUpdate);
  QTimer::singleShot(0, &window, [&]() {
    auto *menu = window.findChild<QMenu *>(
        QStringLiteral("materialBrowser.contextMenu"));
    ASSERT_NE(menu, nullptr);
    auto *duplicate = menu->findChild<QAction *>(
        QStringLiteral("materialBrowser.action.duplicate"));
    auto *cut = menu->findChild<QAction *>(
        QStringLiteral("materialBrowser.action.cut"));
    auto *copy = menu->findChild<QAction *>(
        QStringLiteral("materialBrowser.action.copy"));
    auto *remove = menu->findChild<QAction *>(
        QStringLiteral("materialBrowser.action.delete"));
    ASSERT_NE(duplicate, nullptr);
    ASSERT_NE(cut, nullptr);
    ASSERT_NE(copy, nullptr);
    ASSERT_NE(remove, nullptr);
    EXPECT_TRUE(duplicate->isEnabled());
    EXPECT_FALSE(cut->isEnabled());
    EXPECT_TRUE(copy->isEnabled());
    EXPECT_FALSE(remove->isEnabled());
    menu->close();
  });
  send_context_menu(*grid, grid->visualRect(oak_grid).center());
  EXPECT_EQ(requests.size(), 1);

  grid->selectionModel()->setCurrentIndex(oak_grid,
                                          QItemSelectionModel::ClearAndSelect |
                                              QItemSelectionModel::Rows);
  QTimer::singleShot(0, &window, [&]() {
    auto *menu = window.findChild<QMenu *>(
        QStringLiteral("materialBrowser.contextMenu"));
    ASSERT_NE(menu, nullptr);
    EXPECT_EQ(grid->selectionModel()->selectedRows().size(), 1);
    EXPECT_EQ(grid->currentIndex()
                  .data(QtMaterialBrowserModel::StableIdRole)
                  .toString(),
              steel.stable_id);
    menu->close();
  });
  send_context_menu(*grid, grid->visualRect(steel_grid).center());
  ASSERT_EQ(grid->selectionModel()->selectedRows().size(), 1);
  EXPECT_EQ(grid->currentIndex()
                .data(QtMaterialBrowserModel::StableIdRole)
                .toString(),
            steel.stable_id);

  list_button->click();
  const QModelIndex oak_list = index_for(*list, oak.stable_id);
  const QModelIndex steel_list = index_for(*list, steel.stable_id);
  const QModelIndex corrupt_list = index_for(*list, corrupt.stable_id);
  ASSERT_TRUE(oak_list.isValid());
  ASSERT_TRUE(steel_list.isValid());
  ASSERT_TRUE(corrupt_list.isValid());
  batch.clear();
  batch.select(oak_list, oak_list);
  batch.select(steel_list, steel_list);
  list->selectionModel()->select(batch, QItemSelectionModel::ClearAndSelect |
                                            QItemSelectionModel::Rows);
  list->selectionModel()->setCurrentIndex(steel_list,
                                          QItemSelectionModel::NoUpdate);
  QTimer::singleShot(0, &window, [&]() {
    auto *menu = window.findChild<QMenu *>(
        QStringLiteral("materialBrowser.contextMenu"));
    ASSERT_NE(menu, nullptr);
    auto *duplicate = menu->findChild<QAction *>(
        QStringLiteral("materialBrowser.action.duplicate"));
    ASSERT_NE(duplicate, nullptr);
    menu->setActiveAction(duplicate);
    QKeyEvent accept(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(menu, &accept);
  });
  send_context_menu(*list, list->visualRect(steel_list).center());
  ASSERT_EQ(requests.size(), 2);
  EXPECT_EQ(requests.back().action, QtMaterialBrowserAction::Duplicate);
  EXPECT_EQ(requests.back().selected_materials.size(), 2);

  list->selectionModel()->setCurrentIndex(
      corrupt_list, QItemSelectionModel::ClearAndSelect |
                        QItemSelectionModel::Rows);
  QTimer::singleShot(0, &window, [&]() {
    auto *menu = window.findChild<QMenu *>(
        QStringLiteral("materialBrowser.contextMenu"));
    ASSERT_NE(menu, nullptr);
    QList<QAction *> actions = menu->actions();
    auto *edit = menu->findChild<QAction *>(
        QStringLiteral("materialBrowser.action.editMaterial"));
    ASSERT_NE(edit, nullptr);
    EXPECT_TRUE(actions.removeOne(edit));
    ASSERT_EQ(actions.size(), 13);
    for (int index : {0, 1, 3, 4}) {
      EXPECT_FALSE(actions[index]->isEnabled());
    }
    for (int index : {6, 7, 8, 9, 10, 12}) {
      EXPECT_TRUE(actions[index]->isEnabled());
    }
    menu->close();
  });
  send_context_menu(*list, list->visualRect(corrupt_list).center());
  EXPECT_EQ(requests.size(), 2);

  QTimer::singleShot(0, &window, [&]() {
    auto *menu = window.findChild<QMenu *>(
        QStringLiteral("materialBrowser.contextMenu"));
    ASSERT_NE(menu, nullptr);
    ASSERT_EQ(menu->actions().size(), 1);
    QAction *paste = menu->actions().front();
    EXPECT_EQ(paste->objectName(),
              QStringLiteral("materialBrowser.action.paste"));
    EXPECT_TRUE(paste->isEnabled());
    menu->setActiveAction(paste);
    QKeyEvent accept(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(menu, &accept);
  });
  send_context_menu(*list, QPoint(-4, -4));
  ASSERT_EQ(requests.size(), 3);
  EXPECT_EQ(requests.back().action, QtMaterialBrowserAction::Paste);
  EXPECT_FALSE(requests.back().target.valid());
  EXPECT_EQ(requests.back().destination_folder, QStringLiteral("Materials"));

  QTimer::singleShot(0, &window, [&]() {
    auto *menu = window.findChild<QMenu *>(
        QStringLiteral("materialBrowser.contextMenu"));
    ASSERT_NE(menu, nullptr);
    auto *copy = menu->findChild<QAction *>(
        QStringLiteral("materialBrowser.action.copy"));
    ASSERT_NE(copy, nullptr);
    window.set_dependency_revision(20);
    menu->setActiveAction(copy);
    QKeyEvent accept(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(menu, &accept);
  });
  send_context_menu(*list, list->visualRect(steel_list).center());
  EXPECT_EQ(requests.size(), 3)
      << "a dependency change inside QMenu::exec must reject stale intent";

  QTimer::singleShot(0, &window, [&]() {
    auto *menu = window.findChild<QMenu *>(
        QStringLiteral("materialBrowser.contextMenu"));
    ASSERT_NE(menu, nullptr);
    auto *copy = menu->findChild<QAction *>(
        QStringLiteral("materialBrowser.action.copy"));
    ASSERT_NE(copy, nullptr);
    window.set_project_generation(72);
    menu->setActiveAction(copy);
    QKeyEvent accept(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(menu, &accept);
  });
  send_context_menu(*list, list->visualRect(steel_list).center());
  EXPECT_EQ(requests.size(), 3)
      << "a project rebind inside QMenu::exec must reject stale intent";

  auto *empty =
      window.findChild<QLabel *>(QStringLiteral("materialBrowser.empty"));
  ASSERT_NE(empty, nullptr);
  ASSERT_FALSE(empty->isHidden());
  window.set_material_paste_available(true);
  QTimer::singleShot(0, &window, [&]() {
    auto *menu = window.findChild<QMenu *>(
        QStringLiteral("materialBrowser.contextMenu"));
    ASSERT_NE(menu, nullptr);
    ASSERT_EQ(menu->actions().size(), 1);
    QAction *paste = menu->actions().front();
    EXPECT_EQ(paste->objectName(),
              QStringLiteral("materialBrowser.action.paste"));
    EXPECT_TRUE(paste->isEnabled());
    menu->setActiveAction(paste);
    QKeyEvent accept(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(menu, &accept);
  });
  QContextMenuEvent empty_menu(
      QContextMenuEvent::Mouse, empty->rect().center(),
      empty->mapToGlobal(empty->rect().center()));
  QCoreApplication::sendEvent(empty, &empty_menu);
  ASSERT_EQ(requests.size(), 4);
  EXPECT_EQ(requests.back().action, QtMaterialBrowserAction::Paste);
  EXPECT_EQ(requests.back().project_generation, 72);
  EXPECT_EQ(requests.back().dependency_revision, 0);

  window.set_purpose(QtMaterialBrowserPurpose::Picker);
  send_context_menu(*list, QPoint(-4, -4));
  EXPECT_EQ(
      window.findChild<QMenu *>(QStringLiteral("materialBrowser.contextMenu")),
      nullptr);
  EXPECT_EQ(requests.size(), 4);
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
     TooltipsFollowMaterialFilenamesWithoutChangingPathSearchOrAssignment) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-material-filename-tooltip-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  QWidget owner;
  for (const auto host : {QtMaterialBrowserHost::Embedded,
                          QtMaterialBrowserHost::Windowed}) {
    QtMaterialBrowserWindow browser(&owner, host);
    browser.set_project_generation(9);
    browser.set_assignment_available(true);
    int assignments = 0;
    browser.set_assign_material_callback(
        [&](QuaderMaterialReference) { ++assignments; });
    auto oak = folder_material("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa",
                               QStringLiteral("Display name"),
                               QStringLiteral("Materials/Wood/Oak.fine.QMAT"));
    const auto steel = folder_material("bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb",
                                      QStringLiteral("Steel"),
                                      QStringLiteral("Materials/Metal/steel.qmat"));
    browser.set_materials({oak, steel}, steel.reference);
    auto *grid = browser.findChild<QListView *>(
        QStringLiteral("materialBrowser.gridView"));
    auto *list = browser.findChild<QTableView *>(
        QStringLiteral("materialBrowser.listView"));
    auto *search = browser.findChild<QLineEdit *>(
        QStringLiteral("materialBrowser.search"));
    ASSERT_NE(grid, nullptr);
    ASSERT_NE(list, nullptr);
    ASSERT_NE(search, nullptr);
    ASSERT_EQ(grid->model()->rowCount(), 2);
    EXPECT_EQ(grid->model()->index(0, 0).data(Qt::ToolTipRole).toString(),
              QStringLiteral("Oak.fine.QMAT"));
    EXPECT_EQ(grid->model()->index(1, 0).data(Qt::ToolTipRole).toString(),
              QStringLiteral("steel.qmat"));
    EXPECT_EQ(list->model()->index(0, 1).data(Qt::ToolTipRole).toString(),
              QStringLiteral("Oak.fine.QMAT"));
    EXPECT_EQ(grid->model()->index(0, 0)
                  .data(QtMaterialBrowserModel::RelativePathRole).toString(),
              oak.project_relative_path);
    search->setText(QStringLiteral("Wood/"));
    ASSERT_EQ(grid->model()->rowCount(), 1)
        << "folder paths remain searchable despite shorter tooltips";
    EXPECT_EQ(grid->model()->index(0, 0).data(Qt::ToolTipRole).toString(),
              QStringLiteral("Oak.fine.QMAT"));

    oak.project_relative_path = QStringLiteral("Materials/Wood/Oak.renamed.qmat");
    browser.set_materials({oak, steel}, steel.reference);
    EXPECT_EQ(grid->model()->index(0, 0).data(Qt::ToolTipRole).toString(),
              QStringLiteral("Oak.renamed.qmat"));
    int resets = 0;
    QObject::connect(grid->model(), &QAbstractItemModel::modelReset, &browser,
                     [&]() { ++resets; });
    browser.set_materials({oak, steel}, steel.reference);
    EXPECT_EQ(resets, 0);
    EXPECT_EQ(assignments, 0)
        << "catalog refresh and filtering must never assign materials";
  }
}

TEST(QtMaterialBrowserWindowTests,
     RevealingVisibleMaterialReplacesBatchSelectionWithoutAssignment) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-material-reveal-visible-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  QWidget owner;
  owner.setAttribute(Qt::WA_DontShowOnScreen, true);
  QtMaterialBrowserWindow browser(&owner, QtMaterialBrowserHost::Embedded);
  browser.resize(360, 360);
  browser.restore_presentation_state(QtMaterialBrowserViewMode::Grid, 128,
                                     false, 120);
  const auto first = folder_material(
      "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa", QStringLiteral("Oak First"),
      QStringLiteral("Materials/Wood/oak_first.qmat"));
  const auto second = folder_material(
      "bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb", QStringLiteral("Oak Second"),
      QStringLiteral("Materials/Wood/oak_second.qmat"));
  browser.set_project_generation(7);
  browser.set_materials({first, second});
  browser.set_selected_folder(QStringLiteral("Materials/Wood"));
  browser.set_material_usage({
      .project_generation = 7,
      .revision = 1,
      .complete = true,
      .used_materials = {first.reference, second.reference},
  });
  browser.set_assignment_available(true);
  int assignments = 0;
  browser.set_assign_material_callback(
      [&](QuaderMaterialReference) { ++assignments; });
  auto *search = browser.findChild<QLineEdit *>(
      QStringLiteral("materialBrowser.search"));
  auto *filter = browser.findChild<QToolButton *>(
      QStringLiteral("materialBrowser.filter"));
  auto *grid = browser.findChild<QListView *>(
      QStringLiteral("materialBrowser.gridView"));
  ASSERT_NE(search, nullptr);
  ASSERT_NE(filter, nullptr);
  ASSERT_NE(grid, nullptr);
  search->setText(QStringLiteral("  OAK  "));
  owner.show();
  browser.show();
  application.processEvents();
  filter->click();
  auto *used = browser.findChild<QCheckBox *>(
      QStringLiteral("materialBrowser.filterUsed"));
  ASSERT_NE(used, nullptr);
  used->click();
  browser.set_selected_material(first.reference);
  browser.set_selected_material(second.reference);
  ASSERT_EQ(grid->selectionModel()->selectedRows().size(), 2);
  int catalog_resets = 0;
  QObject::connect(grid->model(), &QAbstractItemModel::modelReset, &browser,
                   [&]() { ++catalog_resets; });

  ASSERT_TRUE(browser.reveal_material(second.reference));
  EXPECT_EQ(browser.selected_material(), second.reference);
  ASSERT_EQ(grid->selectionModel()->selectedRows().size(), 1);
  EXPECT_EQ(grid->selectionModel()->selectedRows().front(),
            grid->selectionModel()->currentIndex());
  EXPECT_EQ(search->text(), QStringLiteral("  OAK  "));
  EXPECT_EQ(browser.selected_folder(), QStringLiteral("Materials/Wood"));
  EXPECT_EQ(browser.usage_mode(), QtMaterialBrowserUsageMode::Used);
  application.processEvents();
  EXPECT_TRUE(grid->viewport()->rect().intersects(
      grid->visualRect(grid->selectionModel()->currentIndex())));
  EXPECT_EQ(catalog_resets, 0);
  EXPECT_EQ(assignments, 0);
}

TEST(QtMaterialBrowserWindowTests,
     RevealingHiddenMaterialRelaxesOnlyBlockingFiltersAndIgnoresMissingTarget) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-material-reveal-filtered-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  QWidget owner;
  owner.setAttribute(Qt::WA_DontShowOnScreen, true);
  QtMaterialBrowserWindow browser(&owner, QtMaterialBrowserHost::Embedded);
  browser.resize(360, 360);
  const auto oak = folder_material(
      "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa", QStringLiteral("Oak"),
      QStringLiteral("Materials/Wood/oak.qmat"));
  const auto steel = folder_material(
      "bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb", QStringLiteral("Steel"),
      QStringLiteral("Materials/Metal/steel.qmat"));
  const auto pine = folder_material(
      "cccccccc-cccc-cccc-cccc-cccccccccccc", QStringLiteral("Pine"),
      QStringLiteral("Materials/Wood/pine.qmat"));
  browser.set_project_generation(7);
  browser.set_materials({oak, steel, pine});
  browser.set_material_usage({
      .project_generation = 7,
      .revision = 1,
      .complete = true,
      .used_materials = {oak.reference, steel.reference},
  });
  browser.set_assignment_available(true);
  int assignments = 0;
  browser.set_assign_material_callback(
      [&](QuaderMaterialReference) { ++assignments; });
  auto *search = browser.findChild<QLineEdit *>(
      QStringLiteral("materialBrowser.search"));
  auto *filter = browser.findChild<QToolButton *>(
      QStringLiteral("materialBrowser.filter"));
  ASSERT_NE(search, nullptr);
  ASSERT_NE(filter, nullptr);
  owner.show();
  browser.show();
  application.processEvents();
  const auto choose_usage = [&](bool used) {
    filter->click();
    auto *choice = browser.findChild<QCheckBox *>(
        used ? QStringLiteral("materialBrowser.filterUsed")
             : QStringLiteral("materialBrowser.filterNotUsed"));
    ASSERT_NE(choice, nullptr);
    choice->click();
    application.processEvents();
  };

  browser.set_selected_folder(QStringLiteral("Materials/Wood"));
  search->setText(QStringLiteral("Materials/"));
  choose_usage(true);
  ASSERT_TRUE(browser.reveal_material(steel.reference));
  EXPECT_EQ(browser.selected_material(), steel.reference);
  EXPECT_EQ(browser.selected_folder(), QStringLiteral("Materials"));
  EXPECT_EQ(search->text(), QStringLiteral("Materials/"));
  EXPECT_EQ(browser.usage_mode(), QtMaterialBrowserUsageMode::Used);

  browser.set_selected_folder(QStringLiteral("Materials/Wood"));
  search->setText(QStringLiteral("steel"));
  ASSERT_TRUE(browser.reveal_material(pine.reference));
  EXPECT_EQ(browser.selected_material(), pine.reference);
  EXPECT_EQ(browser.selected_folder(), QStringLiteral("Materials/Wood"));
  EXPECT_TRUE(search->text().isEmpty());
  EXPECT_EQ(browser.usage_mode(), QtMaterialBrowserUsageMode::All);

  search->setText(QStringLiteral("oak"));
  choose_usage(false);
  ASSERT_TRUE(browser.reveal_material(oak.reference));
  EXPECT_EQ(browser.selected_material(), oak.reference);
  EXPECT_EQ(browser.selected_folder(), QStringLiteral("Materials/Wood"));
  EXPECT_EQ(search->text(), QStringLiteral("oak"));
  EXPECT_EQ(browser.usage_mode(), QtMaterialBrowserUsageMode::All);
  choose_usage(true);
  EXPECT_FALSE(browser.reveal_material({}));
  EXPECT_FALSE(browser.reveal_material(QuaderMaterialReference::project(
      required_project_id("dddddddd-dddd-dddd-dddd-dddddddddddd"))));
  EXPECT_EQ(browser.selected_material(), oak.reference);
  EXPECT_EQ(browser.selected_folder(), QStringLiteral("Materials/Wood"));
  EXPECT_EQ(search->text(), QStringLiteral("oak"));
  EXPECT_EQ(browser.usage_mode(), QtMaterialBrowserUsageMode::Used);
  EXPECT_TRUE(filter->isChecked());
  EXPECT_EQ(assignments, 0);
}

TEST(QtMaterialBrowserWindowTests,
     EmbeddedHostAssignsFromGridAndKeepsCompactToolbarAtDockWidth) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-embedded-material-browser-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));
  application.setQuitOnLastWindowClosed(false);

  QWidget owner;
  owner.setAttribute(Qt::WA_DontShowOnScreen, true);
  owner.resize(340, 520);
  QtMaterialBrowserWindow browser(&owner, QtMaterialBrowserHost::Embedded);
  browser.resize(340, 520);
  browser.restore_presentation_state(QtMaterialBrowserViewMode::List, 64,
                                     false, 120);
  EXPECT_EQ(browser.host(), QtMaterialBrowserHost::Embedded);
  EXPECT_EQ(browser.purpose(), QtMaterialBrowserPurpose::FaceAssignment);
  EXPECT_EQ(browser.windowType(), Qt::Widget);
  EXPECT_FALSE(browser.isWindow());
  EXPECT_EQ(browser.view_mode(), QtMaterialBrowserViewMode::Grid)
      << "old embedded List preferences migrate to its sole Grid view";

  const auto oak = folder_material("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa",
                                   QStringLiteral("Oak"),
                                   QStringLiteral("Materials/Wood/oak.qmat"));
  const auto steel = folder_material("bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb",
                                     QStringLiteral("Steel"),
                                     QStringLiteral("Materials/steel.qmat"));
  browser.set_project_generation(9);
  browser.set_folders({QStringLiteral("Materials/Wood"),
                       QStringLiteral("Materials/Empty")});
  browser.set_materials({oak, steel}, oak.reference);
  auto *grid = browser.findChild<QListView *>(
      QStringLiteral("materialBrowser.gridView"));
  auto *list = browser.findChild<QTableView *>(
      QStringLiteral("materialBrowser.listView"));
  auto *import_button = browser.findChild<QToolButton *>(
      QStringLiteral("materialBrowser.importTool"));
  auto *thumbnail_size = browser.findChild<QToolButton *>(
      QStringLiteral("materialBrowser.thumbnailSize"));
  auto *search = browser.findChild<QLineEdit *>(
      QStringLiteral("materialBrowser.search"));
  auto *filter = browser.findChild<QToolButton *>(
      QStringLiteral("materialBrowser.filter"));
  auto *folders = browser.findChild<QToolButton *>(
      QStringLiteral("materialBrowser.folders"));
  auto *grid_button = browser.findChild<QToolButton *>(
      QStringLiteral("materialBrowser.grid"));
  auto *list_button = browser.findChild<QToolButton *>(
      QStringLiteral("materialBrowser.list"));
  auto *slider = browser.findChild<QSlider *>(
      QStringLiteral("materialBrowser.tileSize"));
  auto *tree = browser.findChild<QTreeWidget *>(
      QStringLiteral("materialBrowser.folderTree"));
  ASSERT_NE(grid, nullptr);
  ASSERT_NE(list, nullptr);
  EXPECT_EQ(browser.findChild<QPushButton *>(
                QStringLiteral("materialBrowser.accept")),
            nullptr);
  EXPECT_EQ(browser.findChild<QPushButton *>(
                QStringLiteral("materialBrowser.import")),
            nullptr);
  ASSERT_NE(import_button, nullptr);
  ASSERT_NE(thumbnail_size, nullptr);
  ASSERT_NE(search, nullptr);
  ASSERT_NE(filter, nullptr);
  ASSERT_NE(folders, nullptr);
  ASSERT_NE(grid_button, nullptr);
  ASSERT_NE(list_button, nullptr);
  ASSERT_NE(slider, nullptr);
  ASSERT_NE(tree, nullptr);
  EXPECT_FALSE(import_button->isHidden());
  EXPECT_FALSE(filter->isHidden());
  EXPECT_TRUE(grid_button->isHidden());
  EXPECT_TRUE(list_button->isHidden());
  EXPECT_FALSE(slider->isVisible());
  for (QToolButton *button : {filter, folders, import_button, thumbnail_size}) {
    EXPECT_EQ(button->toolButtonStyle(), Qt::ToolButtonIconOnly);
    EXPECT_FALSE(button->icon().isNull());
  }
  EXPECT_TRUE(grid->dragEnabled());
  EXPECT_TRUE(list->dragEnabled());
  EXPECT_TRUE(tree->acceptDrops());
  EXPECT_EQ(grid->selectionMode(), QAbstractItemView::ExtendedSelection);
  EXPECT_EQ(grid->model(), list->model());
  EXPECT_EQ(grid->selectionModel(), list->selectionModel());
  int catalog_resets = 0;
  QObject::connect(grid->model(), &QAbstractItemModel::modelReset, &browser,
                   [&]() { ++catalog_resets; });
  const QTreeWidgetItem *retained_folder = tree->topLevelItem(1);
  browser.set_folders({QStringLiteral("Materials/Wood"),
                       QStringLiteral("Materials/Empty")});
  browser.set_materials({oak, steel}, oak.reference);
  EXPECT_EQ(catalog_resets, 0);
  EXPECT_EQ(tree->topLevelItem(1), retained_folder)
      << "unchanged snapshots keep the retained folder tree and catalog";

  int assignment_count = 0;
  QuaderMaterialReference assigned;
  browser.set_assign_material_callback(
      [&](QuaderMaterialReference reference) {
        assigned = reference;
        ++assignment_count;
      });
  const auto click_current = [&]() {
    EXPECT_TRUE(QMetaObject::invokeMethod(
        grid, "clicked", Qt::DirectConnection,
        Q_ARG(QModelIndex, grid->currentIndex())));
  };
  click_current();
  send_key(*grid, Qt::Key_Return);
  EXPECT_EQ(assignment_count, 0);

  browser.set_assignment_available(true);
  browser.set_selected_material(steel.reference);
  EXPECT_EQ(assignment_count, 0)
      << "programmatic highlighting never assigns a material";
  click_current();
  EXPECT_EQ(assignment_count, 1);
  EXPECT_EQ(assigned, steel.reference);
  ASSERT_TRUE(QMetaObject::invokeMethod(
      grid, "activated", Qt::DirectConnection,
      Q_ARG(QModelIndex, grid->currentIndex())));
  EXPECT_EQ(assignment_count, 1)
      << "the platform activation following a mouse click is not duplicated";
  send_key(*grid, Qt::Key_Return);
  EXPECT_EQ(assignment_count, 2);
  list_button->click();
  EXPECT_EQ(browser.view_mode(), QtMaterialBrowserViewMode::Grid);
  EXPECT_TRUE(grid_button->isHidden());
  EXPECT_TRUE(list_button->isHidden());
  EXPECT_FALSE(slider->isVisible());

  QuaderMaterialReference opened;
  browser.set_open_material_callback(
      [&](QuaderMaterialReference reference) { opened = reference; });
  ASSERT_TRUE(QMetaObject::invokeMethod(
      grid, "doubleClicked", Qt::DirectConnection,
      Q_ARG(QModelIndex, grid->currentIndex())));
  EXPECT_EQ(opened, steel.reference);
  EXPECT_EQ(assignment_count, 2);

  browser.set_selected_folder(QStringLiteral("Materials/Wood"));
  EXPECT_EQ(grid->model()->rowCount(), 1);
  QString imported_to;
  browser.set_import_textures_callback(
      [&](QString destination) { imported_to = std::move(destination); });
  import_button->click();
  EXPECT_EQ(imported_to, QStringLiteral("Materials/Wood"));
  browser.set_selected_folder(QStringLiteral("Materials/Empty"));
  EXPECT_EQ(grid->model()->rowCount(), 0);
  send_key(*grid, Qt::Key_Return);
  EXPECT_EQ(assignment_count, 2);
  browser.set_selected_folder(QStringLiteral("Materials"));
  browser.set_selected_material(oak.reference);
  search->setText(QStringLiteral("oak"));
  EXPECT_EQ(grid->model()->rowCount(), 1);

  owner.show();
  browser.show();
  QCoreApplication::processEvents();
  EXPECT_EQ(search->text(), QStringLiteral("oak"));
  EXPECT_LE(browser.minimumSizeHint().width(), 340);
  const int toolbar_center_y =
      search->mapTo(&browser, search->rect().center()).y();
  const int grid_bottom = grid->mapTo(&browser, grid->rect().bottomLeft()).y();
  for (QWidget *control : std::vector<QWidget *>{
           folders, search, filter, import_button, thumbnail_size}) {
    const QRect bounds(control->mapTo(&browser, QPoint{}), control->size());
    EXPECT_TRUE(control->isVisible());
    EXPECT_TRUE(browser.rect().contains(bounds))
        << control->objectName().toStdString();
    EXPECT_LE(std::abs(bounds.center().y() - toolbar_center_y), 1)
        << "embedded tools share one row: "
        << control->objectName().toStdString();
    EXPECT_GT(bounds.top(), grid_bottom)
        << "the embedded toolbar belongs below the material grid";
  }
  int presentation_changes = 0;
  browser.set_presentation_state_changed_callback(
      [&]() { ++presentation_changes; });
  thumbnail_size->click();
  QCoreApplication::processEvents();
  auto *size_popup = browser.findChild<QWidget *>(
      QStringLiteral("materialBrowser.thumbnailSizePopup"));
  ASSERT_NE(size_popup, nullptr);
  EXPECT_TRUE(size_popup->isVisible());
  EXPECT_EQ(size_popup->findChild<QSlider *>(
                QStringLiteral("materialBrowser.tileSize")),
            slider);
  EXPECT_TRUE(slider->isVisible());
  slider->setValue(3);
  EXPECT_EQ(browser.tile_size(), 128);
  EXPECT_GT(presentation_changes, 0);
  size_popup->close();
  QCoreApplication::processEvents();
  EXPECT_EQ(browser.tile_size(), 128)
      << "preview size changes apply immediately and survive popup dismissal";
  thumbnail_size->click();
  QCoreApplication::processEvents();
  size_popup = browser.findChild<QWidget *>(
      QStringLiteral("materialBrowser.thumbnailSizePopup"));
  ASSERT_NE(size_popup, nullptr);
  EXPECT_EQ(size_popup->findChild<QSlider *>(
                QStringLiteral("materialBrowser.tileSize")),
            slider);
  EXPECT_EQ(slider->value(), 3);
  size_popup->close();
  QCoreApplication::processEvents();
  click_current();
  EXPECT_EQ(assignment_count, 3);
  EXPECT_EQ(assigned, oak.reference);
  EXPECT_TRUE(browser.isVisible());
  browser.hide();
  browser.show();
  EXPECT_EQ(search->text(), QStringLiteral("oak"));

  browser.set_assignment_available(false);
  click_current();
  EXPECT_EQ(assignment_count, 3);
  browser.set_assignment_available(true);
  browser.set_project_generation(10);
  browser.set_materials({oak}, oak.reference);
  click_current();
  send_key(*grid, Qt::Key_Return);
  EXPECT_EQ(assignment_count, 3)
      << "a new project must wait for refreshed face-selection eligibility";
}

TEST(QtMaterialBrowserWindowTests,
     PresentsFiltersManagesAndRetainsOnlyLayoutPreferences) {
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
  EXPECT_TRUE(window->windowFlags().testFlag(Qt::WindowMaximizeButtonHint));

  window->set_labels(QtMaterialBrowserLabels{
      .window_title = QStringLiteral("Materials"),
      .search_placeholder = QStringLiteral("Find material"),
      .grid = QStringLiteral("Tiles"),
      .list = QStringLiteral("Details"),
      .tile_size = QStringLiteral("Tile size"),
      .tile_size_value = QStringLiteral("Tile size = %1 pixels"),
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
  auto *import_button = window->findChild<QPushButton *>(
      QStringLiteral("materialBrowser.import"));
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
  EXPECT_EQ(file_menu, nullptr);
  ASSERT_NE(import_button, nullptr);
  ASSERT_NE(accept_button, nullptr);
  EXPECT_EQ(import_button->text(), QStringLiteral("Bring textures in…"));
  EXPECT_EQ(accept_button->text(), QStringLiteral("Choose"));
  EXPECT_EQ(accept_button->accessibleName(), QStringLiteral("Choose"));
  EXPECT_FALSE(accept_button->isEnabled());
  EXPECT_TRUE(accept_button->isHidden());

  int import_count = 0;
  window->set_import_textures_callback(
      [&import_count](const QString &destination) {
        EXPECT_EQ(destination, QStringLiteral("Materials"));
        ++import_count;
      });
  import_button->click();
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
  EXPECT_EQ(grid->selectionMode(), QAbstractItemView::ExtendedSelection);
  EXPECT_EQ(list->selectionMode(), QAbstractItemView::ExtendedSelection);
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
  window->set_assign_material_callback(
      [&activated, &activation_count](QuaderMaterialReference reference) {
        activated = reference;
        ++activation_count;
      });
  const QModelIndex zinc = grid->model()->index(1, 0);
  ASSERT_TRUE(QMetaObject::invokeMethod(
      grid, "clicked", Qt::DirectConnection, Q_ARG(QModelIndex, zinc)));
  EXPECT_FALSE(activated.valid());
  EXPECT_EQ(activation_count, 0);
  ASSERT_TRUE(QMetaObject::invokeMethod(
      grid, "activated", Qt::DirectConnection, Q_ARG(QModelIndex, zinc)));
  EXPECT_EQ(activation_count, 0)
      << "a platform mouse activation following clicked must not dispatch "
         "the material intent twice";

  activated = {};
  grid->selectionModel()->setCurrentIndex(
      zinc, QItemSelectionModel::ClearAndSelect);
  send_key(*grid, Qt::Key_Return);
  EXPECT_FALSE(activated.valid())
      << "management keyboard browsing must not assign a material";
  EXPECT_EQ(activation_count, 0);

  activated = {};
  list->selectionModel()->setCurrentIndex(
      zinc, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
  send_key(*list, Qt::Key_Return);
  EXPECT_FALSE(activated.valid())
      << "both management views keep material selection local";
  EXPECT_EQ(activation_count, 0);

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
  EXPECT_LT(search->mapTo(window, search->rect().bottomLeft()).y(),
            grid->mapTo(window, QPoint{}).y())
      << "the windowed browser retains its toolbar above the catalog";
  EXPECT_GT(grid->verticalScrollBar()->maximum(), 0)
      << "large tiles must remain reachable through vertical scrolling";
  EXPECT_GT(grid->horizontalScrollBar()->maximum(), 0)
      << "a narrow restored window must not clip a 512px tile permanently";
  window->set_selected_material(zinc_reference);
  EXPECT_EQ(window->selected_material(), zinc_reference);
  EXPECT_FALSE(accept_button->isEnabled());
  EXPECT_TRUE(accept_button->isHidden());
  accept_button->click();
  EXPECT_EQ(activation_count, 0)
      << "a hidden management acceptance button must not assign";
  EXPECT_FALSE(activated.valid());
  EXPECT_TRUE(window->isVisible());

  window->show();
  QCoreApplication::processEvents();
  EXPECT_TRUE(window->close());
  EXPECT_FALSE(window->isVisible());
  EXPECT_EQ(activation_count, 0)
      << "closing with X must not assign a material";
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
  auto *picker_import = picker->findChild<QPushButton *>(
      QStringLiteral("materialBrowser.import"));
  auto *picker_search = picker->findChild<QLineEdit *>(
      QStringLiteral("materialBrowser.search"));
  ASSERT_NE(picker_grid, nullptr);
  ASSERT_NE(picker_accept, nullptr);
  ASSERT_NE(picker_import, nullptr);
  ASSERT_NE(picker_search, nullptr);
  EXPECT_TRUE(picker_import->isHidden());
  EXPECT_EQ(picker_grid->selectionMode(), QAbstractItemView::SingleSelection);
  EXPECT_FALSE(picker_grid->dragEnabled());
  EXPECT_FALSE(picker_accept->isEnabled());

  int picker_activation_count = 0;
  int picker_open_count = 0;
  int picker_accept_count = 0;
  QuaderMaterialReference picked;
  picker->set_assign_material_callback(
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
