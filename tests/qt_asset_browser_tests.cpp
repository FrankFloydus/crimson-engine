////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "ui/qt_asset_browser.hpp"
#include "ui/qt_application_theme.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDebug>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QImage>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListView>
#include <QMimeData>
#include <QPainter>
#include <QPersistentModelIndex>
#include <QPointer>
#include <QSpinBox>
#include <QStyleOptionComboBox>
#include <QTableView>
#include <QTreeWidget>

#include <memory>
#include <vector>

namespace quader {
namespace {

QtAssetBrowserItem browser_item(QString id, QString path,
                                 QtAssetBrowserKind kind) {
  QtAssetBrowserItem item;
  item.id = std::move(id);
  item.relative_path = std::move(path);
  item.name = item.relative_path.section(QLatin1Char('/'), -1).section(QLatin1Char('.'), 0, 0);
  item.kind = kind;
  return item;
}

QtAssetBrowserSnapshot browser_snapshot() {
  QtAssetBrowserSnapshot snapshot;
  snapshot.generation = 4;
  snapshot.revision = 7;
  snapshot.dependency_revision = 3;
  snapshot.items = {
      browser_item(QStringLiteral("folder"), QStringLiteral("Maps"), QtAssetBrowserKind::Folder),
      browser_item(QStringLiteral("empty"), QStringLiteral("Maps/Empty"), QtAssetBrowserKind::Folder),
      browser_item(QStringLiteral("scene"), QStringLiteral("Maps/Courtyard.qdr"), QtAssetBrowserKind::Scene),
      browser_item(QStringLiteral("material"), QStringLiteral("Metal.qmat"), QtAssetBrowserKind::Material),
      browser_item(QStringLiteral("texture"), QStringLiteral("Metal.png"), QtAssetBrowserKind::Texture),
  };
  return snapshot;
}

class QtAssetBrowserTests : public testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(QCoreApplication::instance(), nullptr);
    app_ = std::make_unique<QApplication>(argc_, argv_);
    app_->setQuitOnLastWindowClosed(false);
    QApplication::setStyle(QStringLiteral("Fusion"));
    apply_qt_obsidian_application_theme(QApplication::palette());
    const QIcon application_icon(QStringLiteral(QUADER_SOURCE_DIR "/resources/icons/quader_256.png"));
    ASSERT_FALSE(application_icon.isNull());
    QApplication::setWindowIcon(application_icon);
    browser_ = std::make_unique<QtAssetBrowser>();
    browser_->setAttribute(Qt::WA_DontShowOnScreen, true);
    browser_->resize(900, 440);
    browser_->set_snapshot(browser_snapshot());
    browser_->set_action_callback([this](auto request) {
      requests_.push_back(std::move(request));
    });
  }
  void TearDown() override {
    browser_.reset();
    app_.reset();
  }
  QListView *grid() const {
    return browser_->findChild<QListView *>(QStringLiteral("assetBrowser.grid"));
  }
  QTableView *list() const {
    return browser_->findChild<QTableView *>(QStringLiteral("assetBrowser.list"));
  }
  void show() {
    browser_->show();
    QCoreApplication::processEvents();
  }
  bool drop(const QMimeData *mime, Qt::KeyboardModifiers modifiers = {},
              bool expect_acceptance = true) {
    EXPECT_TRUE(grid()->acceptDrops());
    EXPECT_TRUE(grid()->viewport()->acceptDrops());
    EXPECT_TRUE(grid()->isVisible());
    const QPoint point(grid()->viewport()->width() - 5,
                        grid()->viewport()->height() - 5);
    QDragEnterEvent enter(point, Qt::CopyAction | Qt::MoveAction, mime,
                           Qt::LeftButton, modifiers);
    QCoreApplication::sendEvent(grid()->viewport(), &enter);
    EXPECT_EQ(enter.isAccepted(), expect_acceptance)
        << "Asset drag entry in folder " << browser_->selected_folder().toStdString();
    if (!enter.isAccepted()) {
      return false;
    }
    QDropEvent event(QPointF(point), Qt::CopyAction | Qt::MoveAction,
                      mime, Qt::LeftButton, modifiers);
    QCoreApplication::sendEvent(grid()->viewport(), &event);
    return event.isAccepted();
  }
  int argc_ = 1;
  char app_name_[32] = "quader-asset-browser-tests";
  char *argv_[2] = {app_name_, nullptr};
  std::unique_ptr<QApplication> app_;
  std::unique_ptr<QtAssetBrowser> browser_;
  std::vector<QtAssetBrowserActionRequest> requests_;
};

TEST_F(QtAssetBrowserTests, CatalogContainsMixedAssetsAndEmptyPhysicalFolders) {
  ASSERT_NE(grid(), nullptr);
  ASSERT_NE(list(), nullptr);
  EXPECT_EQ(grid()->model(), list()->model());
  EXPECT_EQ(grid()->selectionModel(), list()->selectionModel());
  EXPECT_EQ(grid()->model()->rowCount(), 3);
  EXPECT_EQ(grid()->model()->index(0, 0).data().toString(), QStringLiteral("Maps"));
  browser_->set_selected_folder(QStringLiteral("Maps"));
  EXPECT_EQ(grid()->model()->rowCount(), 2);
  browser_->set_selected_folder(QStringLiteral("Maps/Empty"));
  EXPECT_EQ(grid()->model()->rowCount(), 0);
  EXPECT_TRUE(requests_.empty());
}

TEST_F(QtAssetBrowserTests, SearchRecursesWithinFolderAndTypeFilterKeepsNavigation) {
  browser_->set_type_filter(QtAssetBrowserKind::Scene);
  EXPECT_EQ(grid()->model()->rowCount(), 1);
  browser_->set_search_text(QStringLiteral("court"));
  ASSERT_EQ(grid()->model()->rowCount(), 1);
  EXPECT_EQ(grid()->model()->index(0, 0).data().toString(), QStringLiteral("Courtyard"));
  browser_->set_selected_folder(QStringLiteral("Maps/Empty"));
  EXPECT_EQ(grid()->model()->rowCount(), 0);
  browser_->set_search_text({});
  browser_->set_selected_folder(QStringLiteral("Maps"));
  EXPECT_EQ(grid()->model()->rowCount(), 2);
}

TEST_F(QtAssetBrowserTests, SameOwnerStampsAndDependencyOnlyChangesKeepModelRows) {
  int resets = 0;
  int changes = 0;
  QObject::connect(grid()->model(), &QAbstractItemModel::modelReset,
                     [&] { ++resets; });
  QObject::connect(grid()->model(), &QAbstractItemModel::dataChanged,
                     [&] { ++changes; });
  EXPECT_FALSE(browser_->set_snapshot(browser_snapshot()));
  EXPECT_EQ(resets, 0);
  auto snapshot = browser_snapshot();
  ++snapshot.dependency_revision;
  EXPECT_TRUE(browser_->set_snapshot(std::move(snapshot)));
  EXPECT_EQ(resets, 0);
  EXPECT_EQ(changes, 0);
  EXPECT_EQ(grid()->model()->rowCount(), 3);
}

TEST_F(QtAssetBrowserTests, CreationMoveAndRemovalRetainUnchangedRowsAndFolderTree) {
  show();
  ASSERT_TRUE(browser_->reveal_item(QStringLiteral("texture")));
  const QPersistentModelIndex texture(grid()->currentIndex());
  auto *tree = browser_->findChild<QTreeWidget *>(QStringLiteral("assetBrowser.folderTree"));
  ASSERT_NE(tree, nullptr);
  auto *root = tree->topLevelItem(0);
  auto *maps = root->child(0);
  maps->setExpanded(true);
  int resets = 0;
  QObject::connect(grid()->model(), &QAbstractItemModel::modelReset,
                     [&] { ++resets; });

  auto snapshot = browser_snapshot();
  ++snapshot.revision;
  snapshot.items.insert(snapshot.items.begin(), browser_item(QStringLiteral("new"),
      QStringLiteral("New Scene.qdr"), QtAssetBrowserKind::Scene));
  EXPECT_TRUE(browser_->set_snapshot(snapshot));
  EXPECT_TRUE(texture.isValid());
  EXPECT_EQ(texture.data().toString(), QStringLiteral("Metal"));
  EXPECT_EQ(tree->topLevelItem(0), root);
  EXPECT_EQ(root->child(0), maps);
  EXPECT_TRUE(maps->isExpanded());
  ASSERT_EQ(browser_->selected_items().size(), 1U);
  EXPECT_EQ(browser_->selected_items().front().id, QStringLiteral("texture"));
  EXPECT_EQ(grid()->model()->rowCount(), 4);

  ++snapshot.revision;
  snapshot.items.front().relative_path = QStringLiteral("Maps/New Scene.qdr");
  EXPECT_TRUE(browser_->set_snapshot(snapshot));
  EXPECT_EQ(grid()->model()->rowCount(), 3);
  EXPECT_TRUE(texture.isValid());
  EXPECT_EQ(tree->topLevelItem(0), root);

  ++snapshot.revision;
  snapshot.items.erase(snapshot.items.begin());
  EXPECT_TRUE(browser_->set_snapshot(std::move(snapshot)));
  EXPECT_TRUE(texture.isValid());
  EXPECT_EQ(tree->topLevelItem(0), root);
  EXPECT_EQ(resets, 0);
  ASSERT_EQ(browser_->selected_items().size(), 1U);
  EXPECT_EQ(browser_->selected_items().front().id, QStringLiteral("texture"));
}

TEST_F(QtAssetBrowserTests, RepeatedDuplicateIdentitiesDoNotCorruptIncrementalRows) {
  auto snapshot = browser_snapshot();
  snapshot.items.push_back(browser_item(QStringLiteral("copy"),
      QStringLiteral("First Copy.qdr"), QtAssetBrowserKind::Scene));
  snapshot.items.push_back(browser_item(QStringLiteral("copy"),
      QStringLiteral("Second Copy.qdr"), QtAssetBrowserKind::Scene));
  for (int revision = 0; revision < 3; ++revision) {
    ++snapshot.revision;
    ASSERT_TRUE(browser_->set_snapshot(snapshot));
    EXPECT_EQ(grid()->model()->rowCount(), 5);
    EXPECT_EQ(browser_->snapshot().items.size(), 7U);
  }
  snapshot.items.pop_back();
  ++snapshot.revision;
  ASSERT_TRUE(browser_->set_snapshot(snapshot));
  EXPECT_EQ(grid()->model()->rowCount(), 4);
  EXPECT_EQ(browser_->snapshot().items.size(), 6U);
  ++snapshot.revision;
  ASSERT_TRUE(browser_->set_snapshot(snapshot));
  EXPECT_EQ(grid()->model()->rowCount(), 4);
}

TEST_F(QtAssetBrowserTests, FolderAndSceneArtworkAreFilledAndUsageFilterIsAbsent) {
  EXPECT_EQ(browser_->findChild<QComboBox *>(QStringLiteral("assetBrowser.usageFilter")), nullptr);
  auto *tree = browser_->findChild<QTreeWidget *>(QStringLiteral("assetBrowser.folderTree"));
  ASSERT_NE(tree, nullptr);
  const auto folder = qvariant_cast<QIcon>(
      grid()->model()->index(0, 0).data(Qt::DecorationRole)).pixmap(192, 192).toImage();
  const auto sidebar = tree->topLevelItem(0)->child(0)->icon(0).pixmap(192, 192).toImage();
  EXPECT_EQ(folder, sidebar);
  const QColor yellow = folder.pixelColor(96, 120);
  EXPECT_EQ(yellow.alpha(), 255);
  EXPECT_GT(yellow.red(), 200);
  EXPECT_GT(yellow.green(), 140);
  EXPECT_LT(yellow.blue(), 100);
  EXPECT_EQ(folder.pixelColor(0, 0).alpha(), 0);

  ASSERT_TRUE(browser_->reveal_item(QStringLiteral("scene")));
  const auto scene = qvariant_cast<QIcon>(
      grid()->currentIndex().data(Qt::DecorationRole)).pixmap(192, 192).toImage();
  const QColor paper = scene.pixelColor(56, 56);
  EXPECT_EQ(paper.alpha(), 255);
  EXPECT_GT(paper.red(), 200);
  EXPECT_GT(paper.green(), 200);
  EXPECT_GT(paper.blue(), 200);
  EXPECT_EQ(scene.pixelColor(0, 0).alpha(), 0);
  int logo_pixels = 0;
  for (int y = 80; y < 144; ++y) {
    for (int x = 64; x < 128; ++x) {
      const QColor pixel = scene.pixelColor(x, y);
      if (pixel.alpha() > 240 && pixel.blue() > pixel.green() + 25 &&
          pixel.red() > pixel.green() + 25) {
        ++logo_pixels;
      }
    }
  }
  EXPECT_GT(logo_pixels, 100);
}

TEST_F(QtAssetBrowserTests, RenameEmitsBasenameAndOwnerFenceWithoutEditingCatalog) {
  ASSERT_TRUE(browser_->reveal_item(QStringLiteral("scene")));
  const auto index = grid()->currentIndex();
  EXPECT_EQ(index.data(Qt::EditRole).toString(), QStringLiteral("Courtyard"));
  EXPECT_TRUE(grid()->model()->setData(index, QStringLiteral("Atrium"), Qt::EditRole));
  ASSERT_EQ(requests_.size(), 1U);
  EXPECT_EQ(requests_[0].action, QtAssetBrowserAction::Rename);
  EXPECT_EQ(requests_[0].proposed_name, QStringLiteral("Atrium"));
  EXPECT_EQ(requests_[0].generation, 4U);
  EXPECT_EQ(requests_[0].catalog_revision, 7U);
  EXPECT_EQ(requests_[0].dependency_revision, 3U);
  EXPECT_EQ(requests_[0].selected_items[0].relative_path, QStringLiteral("Maps/Courtyard.qdr"));
  EXPECT_EQ(index.data().toString(), QStringLiteral("Courtyard"));
  EXPECT_FALSE(grid()->model()->setData(index, QStringLiteral("../Escape"), Qt::EditRole));
  EXPECT_EQ(requests_.size(), 1U);
}

TEST_F(QtAssetBrowserTests, CreationUsesCurrentFolderAndOpeningIsExplicit) {
  browser_->set_selected_folder(QStringLiteral("Maps/Empty"));
  ASSERT_TRUE(browser_->handle_action(QtAssetBrowserAction::CreateScene));
  ASSERT_EQ(requests_.size(), 1U);
  EXPECT_EQ(requests_.front().destination_folder, QStringLiteral("Maps/Empty"));
  EXPECT_TRUE(requests_.front().selected_items.empty());
  ASSERT_TRUE(browser_->reveal_item(QStringLiteral("scene")));
  EXPECT_EQ(requests_.size(), 1U);
  ASSERT_TRUE(browser_->handle_action(QtAssetBrowserAction::Open));
  ASSERT_EQ(requests_.size(), 2U);
  EXPECT_EQ(requests_.back().action, QtAssetBrowserAction::Open);
  EXPECT_EQ(requests_.back().selected_items.front().id, QStringLiteral("scene"));
}

TEST_F(QtAssetBrowserTests, PickerLimitsMutationAndPublicGateOmitsMaterialAssets) {
  browser_->set_material_authoring_available(false);
  EXPECT_EQ(grid()->model()->rowCount(), 1);
  EXPECT_FALSE(browser_->handle_action(QtAssetBrowserAction::CreateMaterial));
  EXPECT_FALSE(browser_->handle_action(QtAssetBrowserAction::Import));
  browser_->set_picker_mode(true);
  browser_->set_type_filter(QtAssetBrowserKind::Scene);
  EXPECT_FALSE(browser_->handle_action(QtAssetBrowserAction::CreateScene));
  EXPECT_TRUE(browser_->handle_action(QtAssetBrowserAction::CreateFolder));
  EXPECT_FALSE(grid()->dragEnabled());
  EXPECT_FALSE(grid()->viewport()->acceptDrops());
  EXPECT_FALSE(list()->viewport()->acceptDrops());
  EXPECT_EQ(grid()->selectionMode(), QAbstractItemView::SingleSelection);
  auto *folders = browser_->findChild<QTreeWidget *>(QStringLiteral("assetBrowser.folderTree"));
  ASSERT_NE(folders, nullptr);
  EXPECT_FALSE(folders->viewport()->acceptDrops());
  browser_->set_picker_mode(false);
  EXPECT_TRUE(grid()->viewport()->acceptDrops());
  EXPECT_TRUE(list()->viewport()->acceptDrops());
  EXPECT_TRUE(folders->viewport()->acceptDrops());
}

TEST_F(QtAssetBrowserTests, CopyActionsCarryWholeSelectionAndPasteUsesCurrentFolder) {
  auto *selection = grid()->selectionModel();
  selection->select(grid()->model()->index(1, 0),
                    QItemSelectionModel::Select | QItemSelectionModel::Rows);
  selection->select(grid()->model()->index(2, 0),
                    QItemSelectionModel::Select | QItemSelectionModel::Rows);
  EXPECT_TRUE(browser_->handle_action(QtAssetBrowserAction::Copy));
  ASSERT_EQ(requests_.size(), 1U);
  ASSERT_EQ(requests_.back().selected_items.size(), 2U);
  browser_->set_selected_folder(QStringLiteral("Maps"));
  EXPECT_FALSE(browser_->handle_action(QtAssetBrowserAction::Paste));
  browser_->set_paste_available(true);
  EXPECT_TRUE(browser_->handle_action(QtAssetBrowserAction::Paste));
  EXPECT_EQ(requests_.back().destination_folder, QStringLiteral("Maps"));
}

TEST_F(QtAssetBrowserTests, TextureDragPreservesOpaqueIdentityAndRejectsMixedSelection) {
  ASSERT_TRUE(browser_->reveal_item(QStringLiteral("texture")));
  std::unique_ptr<QMimeData> mime(grid()->model()->mimeData({grid()->currentIndex()}));
  QString id;
  std::uint64_t generation = 0;
  EXPECT_TRUE(qt_asset_browser_texture_drag(mime.get(), id, generation));
  EXPECT_EQ(id, QStringLiteral("texture"));
  EXPECT_EQ(generation, 4U);
  ASSERT_TRUE(browser_->reveal_item(QStringLiteral("material")));
  mime.reset(grid()->model()->mimeData({grid()->currentIndex()}));
  EXPECT_FALSE(qt_asset_browser_texture_drag(mime.get(), id, generation));
}

TEST_F(QtAssetBrowserTests, DragMoveAndControlCopySendIntentAndRejectStaleGeneration) {
  show();
  ASSERT_TRUE(browser_->reveal_item(QStringLiteral("texture")));
  std::unique_ptr<QMimeData> mime(grid()->model()->mimeData({grid()->currentIndex()}));
  QString source_id;
  std::uint64_t source_generation = 0;
  ASSERT_TRUE(qt_asset_browser_texture_drag(mime.get(), source_id, source_generation));
  ASSERT_EQ(source_id, QStringLiteral("texture"));
  ASSERT_EQ(source_generation, browser_->snapshot().generation);
  browser_->set_selected_folder(QStringLiteral("Maps/Empty"));
  ASSERT_EQ(browser_->selected_folder(), QStringLiteral("Maps/Empty"));
  ASSERT_EQ(grid()->model()->rowCount(), 0);
  QCoreApplication::processEvents();
  EXPECT_TRUE(drop(mime.get()));
  ASSERT_EQ(requests_.size(), 1U);
  EXPECT_EQ(requests_.back().action, QtAssetBrowserAction::Move);
  EXPECT_EQ(requests_.back().destination_folder, QStringLiteral("Maps/Empty"));
  EXPECT_TRUE(drop(mime.get(), Qt::ControlModifier));
  EXPECT_EQ(requests_.back().action, QtAssetBrowserAction::CopyTo);
  auto replacement = browser_snapshot();
  ++replacement.generation;
  browser_->set_snapshot(std::move(replacement));
  EXPECT_FALSE(drop(mime.get(), {}, false));
  EXPECT_EQ(requests_.size(), 2U);
}

TEST_F(QtAssetBrowserTests, ModelLayoutDoesNotRequestThumbnailsAndPaintingDoes) {
  int requested = 0;
  browser_->set_thumbnail_provider([&](const auto &, QSize, qreal) {
    ++requested;
    return QIcon{};
  });
  for (int row = 0; row < grid()->model()->rowCount(); ++row) {
    (void)grid()->model()->index(row, 0).data(Qt::DecorationRole);
  }
  EXPECT_EQ(requested, 0);
  show();
  const QPixmap rendered = browser_->grab();
  EXPECT_FALSE(rendered.isNull());
  EXPECT_GT(requested, 0);
}

TEST_F(QtAssetBrowserTests, DiscoveryReadinessRequiresPaintForEveryExposure) {
  EXPECT_FALSE(browser_->has_completed_visible_paint());
  browser_->show();
  EXPECT_FALSE(browser_->has_completed_visible_paint());
  QCoreApplication::processEvents();
  EXPECT_TRUE(browser_->has_completed_visible_paint());
  browser_->hide();
  EXPECT_FALSE(browser_->has_completed_visible_paint());
  browser_->show();
  EXPECT_FALSE(browser_->has_completed_visible_paint());
  QCoreApplication::processEvents();
  EXPECT_TRUE(browser_->has_completed_visible_paint());
  EXPECT_FALSE(browser_->set_snapshot(browser_snapshot()));
  EXPECT_TRUE(browser_->has_completed_visible_paint());
}

TEST_F(QtAssetBrowserTests, InlineEscapeKeepsCreatedNameWithoutRenameRequest) {
  show();
  ASSERT_TRUE(browser_->begin_rename(QStringLiteral("scene")));
  auto *editor = grid()->findChild<QLineEdit *>();
  ASSERT_NE(editor, nullptr);
  editor->setText(QStringLiteral("Uncommitted"));
  QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
  QCoreApplication::sendEvent(editor, &escape);
  QCoreApplication::processEvents();
  EXPECT_TRUE(requests_.empty());
  EXPECT_EQ(browser_->selected_items().front().name, QStringLiteral("Courtyard"));
}

TEST_F(QtAssetBrowserTests, NewProjectClearsNavigationButSameProjectRetainsSelection) {
  ASSERT_TRUE(browser_->reveal_item(QStringLiteral("scene")));
  auto update = browser_snapshot();
  ++update.revision;
  browser_->set_snapshot(update);
  EXPECT_EQ(browser_->selected_folder(), QStringLiteral("Maps"));
  ASSERT_EQ(browser_->selected_items().size(), 1U);
  ++update.generation;
  browser_->set_snapshot(update);
  EXPECT_TRUE(browser_->selected_folder().isEmpty());
  EXPECT_TRUE(browser_->selected_items().empty());
  browser_->set_selected_folder(QStringLiteral("../Outside"));
  EXPECT_TRUE(browser_->selected_folder().isEmpty());
}

TEST_F(QtAssetBrowserTests, FolderRowsFollowLiveFontWhenScaleMetricsArriveFirst) {
  QtUiScaleApplicationController scale_controller(*app_);
  browser_->apply_ui_scale(scale_controller.metrics());
  show();
  ASSERT_TRUE(browser_->reveal_item(QStringLiteral("scene")));
  auto *tree = browser_->findChild<QTreeWidget *>(QStringLiteral("assetBrowser.folderTree"));
  ASSERT_NE(tree, nullptr);
  auto *root = tree->topLevelItem(0);
  ASSERT_NE(root, nullptr);
  auto *maps = root->child(0);
  ASSERT_NE(maps, nullptr);
  maps->setExpanded(true);
  QCoreApplication::processEvents();
  const int initial_height = tree->visualItemRect(maps).height();
  const QSize initial_icon_size = tree->iconSize();
  EXPECT_GE(initial_height, 26);
  EXPECT_EQ(initial_icon_size, QSize(16, 16));

  scale_controller.apply_factor(2.0F);
  browser_->apply_ui_scale(scale_controller.metrics());
  QCoreApplication::processEvents();
  EXPECT_GT(tree->visualItemRect(maps).height(), initial_height);
  EXPECT_EQ(tree->iconSize(), QSize(32, 32));

  // Main-window scale listeners may run before inherited font notifications.
  // The row must not retain a size hint measured with the outgoing large font.
  browser_->apply_ui_scale(QtUiScaleMetrics(1.0F, 100U));
  scale_controller.apply_factor(1.0F);
  QCoreApplication::processEvents();
  EXPECT_EQ(tree->visualItemRect(maps).height(), initial_height);
  EXPECT_EQ(tree->iconSize(), initial_icon_size);
  EXPECT_EQ(tree->topLevelItem(0)->child(0), maps);
  EXPECT_TRUE(maps->isExpanded());
  EXPECT_EQ(browser_->selected_folder(), QStringLiteral("Maps"));
  ASSERT_EQ(browser_->selected_items().size(), 1U);
  EXPECT_EQ(browser_->selected_items().front().id, QStringLiteral("scene"));
}

TEST_F(QtAssetBrowserTests, NativeWidgetReviewAt100And200PercentAndScaleDown) {
  QtUiScaleApplicationController scale_controller(*app_);
  auto snapshot = browser_snapshot();
  ++snapshot.revision;
  snapshot.items.push_back(browser_item(QStringLiteral("root-scene"),
      QStringLiteral("Atrium.qdr"), QtAssetBrowserKind::Scene));
  browser_->set_snapshot(std::move(snapshot));
  const QString capture = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
  if (!capture.isEmpty()) {
    ASSERT_TRUE(QDir{}.mkpath(capture));
  }
  show();
  for (const float factor : {1.0F, 2.0F, 1.0F}) {
    scale_controller.apply_factor(factor);
    browser_->apply_ui_scale(scale_controller.metrics());
    browser_->resize(static_cast<int>(900 * factor), static_cast<int>(360 * factor));
    QCoreApplication::processEvents();
    EXPECT_GT(grid()->viewport()->width(), 0);
    if (!capture.isEmpty()) {
      EXPECT_TRUE(browser_->grab().save(QDir(capture).filePath(
          QStringLiteral("asset-browser-%1.png").arg(factor))));
    }
  }
  browser_->restore_presentation_state(QtAssetBrowserViewMode::Grid, 64, false);
  browser_->resize(680, 320);
  QCoreApplication::processEvents();
  if (!capture.isEmpty()) {
    EXPECT_TRUE(browser_->grab().save(QDir(capture).filePath(
        QStringLiteral("asset-browser-narrow.png"))));
  }
  browser_->setLayoutDirection(Qt::RightToLeft);
  QCoreApplication::processEvents();
  if (!capture.isEmpty()) {
    EXPECT_TRUE(browser_->grab().save(QDir(capture).filePath(
        QStringLiteral("asset-browser-rtl.png"))));
  }
  apply_qt_application_theme_for_contrast_preference(
      QApplication::style()->standardPalette(), Qt::ContrastPreference::HighContrast);
  QCoreApplication::processEvents();
  if (!capture.isEmpty()) {
    EXPECT_TRUE(browser_->grab().save(QDir(capture).filePath(
        QStringLiteral("asset-browser-high-contrast.png"))));
  }
}

TEST_F(QtAssetBrowserTests, ToolbarKeepsLabelsAndThreeDigitSizeReadableAcrossScaling) {
  QtUiScaleApplicationController scale_controller(*app_);
  auto *types = browser_->findChild<QComboBox *>(QStringLiteral("assetBrowser.typeFilter"));
  auto *size = browser_->findChild<QSpinBox *>(QStringLiteral("assetBrowser.previewSize"));
  ASSERT_NE(types, nullptr);
  ASSERT_NE(size, nullptr);
  size->setValue(112);
  show();
  const auto verify = [&] {
    QStyleOptionComboBox option;
    option.initFrom(types);
    option.editable = false;
    option.frame = true;
    option.currentText = types->currentText();
    const QRect content = types->style()->subControlRect(
        QStyle::CC_ComboBox, &option, QStyle::SC_ComboBoxEditField, types);
    EXPECT_GE(content.width(), types->fontMetrics().horizontalAdvance(types->currentText()));
    const auto *line = size->findChild<QLineEdit *>();
    ASSERT_NE(line, nullptr);
    const int readable = line->contentsRect().width() -
                         line->textMargins().left() - line->textMargins().right();
    EXPECT_GE(readable, line->fontMetrics().horizontalAdvance(line->text()) + 2);
    EXPECT_GE(size->width(), size->minimumSizeHint().width());
  };
  int initial_combo_width = 0;
  int initial_size_width = 0;
  for (const float factor : {1.0F, 2.0F, 1.0F}) {
    // Exercise the shell's possible metrics-before-inherited-font ordering.
    browser_->apply_ui_scale(QtUiScaleMetrics(factor, 100U));
    scale_controller.apply_factor(factor);
    browser_->resize(static_cast<int>(680 * factor), static_cast<int>(320 * factor));
    QCoreApplication::processEvents();
    const QVariant item_font = types->itemData(types->currentIndex(), Qt::FontRole);
    const QString diagnostic = QStringLiteral(
        "scale=%1 appFont=%2 comboFont=%3 width=%4 hint=%5 minHint=%6 minimum=%7 "
        "policy=%8 itemFontRole=%9 spinFont=%10 spinWidth=%11 spinHint=%12 spinMinimum=%13")
        .arg(factor).arg(QApplication::font().pointSizeF()).arg(types->font().pointSizeF())
        .arg(types->width()).arg(types->sizeHint().width())
        .arg(types->minimumSizeHint().width()).arg(types->minimumWidth())
        .arg(static_cast<int>(types->sizeAdjustPolicy()))
        .arg(item_font.isValid() ? item_font.value<QFont>().toString() : QStringLiteral("unset"))
        .arg(size->font().pointSizeF()).arg(size->width())
        .arg(size->sizeHint().width()).arg(size->minimumWidth());
    SCOPED_TRACE(diagnostic.toStdString());
    if (qEnvironmentVariableIsSet("QUADER_UI_METRICS_DIAGNOSTICS")) {
      qInfo().noquote() << diagnostic;
    }
    verify();
    if (initial_combo_width == 0) {
      initial_combo_width = types->width();
      initial_size_width = size->width();
    } else if (factor == 1.0F) {
      EXPECT_EQ(types->width(), initial_combo_width);
      EXPECT_EQ(size->width(), initial_size_width);
    }
  }
  QtAssetBrowserLabels labels;
  labels.all_types = QStringLiteral("Tutte le risorse del progetto");
  browser_->set_labels(std::move(labels));
  QCoreApplication::processEvents();
  verify();
}

} // namespace
} // namespace quader
