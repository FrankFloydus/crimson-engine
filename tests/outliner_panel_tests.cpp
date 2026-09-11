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
#include "editor/session/project_session.hpp"
#include "editor/session/quader_editor_api_session_access.hpp"
#include "editor/session/scene_session.hpp"
#include "ui/outliner_panel.hpp"
#include "ui/qt_application_theme.hpp"
#include "ui/qt_ui_scale_metrics.hpp"
#include "ui/ui_context.hpp"
#include "ui/ui_settings.hpp"
#include "viewport/viewport_settings.hpp"

#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QContextMenuEvent>
#include <QImage>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLayout>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QModelIndex>
#include <QPersistentModelIndex>
#include <QPainter>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleOptionViewItem>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeView>

#include <algorithm>
#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

#ifndef QUADER_SOURCE_DIR
#define QUADER_SOURCE_DIR "."
#endif



namespace {

constexpr int kEntityIdRole = Qt::UserRole + 1;

quader::LocalizationService loaded_localization() {
  quader::LocalizationService localization;
  EXPECT_TRUE(localization.load(
      std::filesystem::path(QUADER_SOURCE_DIR) / "resources" / "locales",
      "en-US"));
  return localization;
}

void send_mouse(QWidget &target, QEvent::Type type, QPoint position,
                Qt::MouseButton button, Qt::MouseButtons buttons,
                Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  QMouseEvent event(type, QPointF(position),
                    QPointF(target.mapToGlobal(position)), button, buttons,
                    modifiers);
  QCoreApplication::sendEvent(&target, &event);
  QCoreApplication::processEvents();
}

void click(QWidget &target, QPoint position, Qt::MouseButton button,
           Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  send_mouse(target, QEvent::MouseButtonPress, position, button, button,
             modifiers);
  send_mouse(target, QEvent::MouseButtonRelease, position, button,
             Qt::NoButton, modifiers);
}

void send_key(QWidget &target, int key,
              Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  QKeyEvent press(QEvent::KeyPress, key, modifiers);
  QCoreApplication::sendEvent(&target, &press);
  QKeyEvent release(QEvent::KeyRelease, key, modifiers);
  QCoreApplication::sendEvent(&target, &release);
  QCoreApplication::processEvents();
}

QModelIndex find_entity_index(const QAbstractItemModel &model,
                              quader::EntityId entity,
                              const QModelIndex &parent = {}) {
  for (int row = 0; row < model.rowCount(parent); ++row) {
    const QModelIndex index = model.index(row, 0, parent);
    if (static_cast<quader::EntityId>(
            index.data(kEntityIdRole).toULongLong()) == entity) {
      return index;
    }
    const QModelIndex child = find_entity_index(model, entity, index);
    if (child.isValid()) {
      return child;
    }
  }
  return {};
}

std::vector<quader::EntityId>
sorted_entities(std::vector<quader::EntityId> entities) {
  std::sort(entities.begin(), entities.end());
  return entities;
}

std::vector<quader::EntityId> selected_widget_entities(QTreeView &tree) {
  std::vector<quader::EntityId> selected;
  const QModelIndexList rows = tree.selectionModel()->selectedRows(0);
  selected.reserve(static_cast<std::size_t>(rows.size()));
  for (const QModelIndex &index : rows) {
    selected.push_back(static_cast<quader::EntityId>(
        index.data(kEntityIdRole).toULongLong()));
  }
  return sorted_entities(std::move(selected));
}

void expect_selection(QTreeView &tree, const quader::SceneDocument &document,
                      std::vector<quader::EntityId> expected,
                      quader::EntityId primary) {
  expected = sorted_entities(std::move(expected));
  EXPECT_EQ(selected_widget_entities(tree), expected);
  EXPECT_EQ(sorted_entities(document.selected_entities()), expected);
  EXPECT_EQ(document.primary_selection(), primary);
}

QImage render_branch(QStyle &style, const QPalette &palette,
                     QStyle::State state, Qt::LayoutDirection direction,
                     qreal device_pixel_ratio = 1.0,
                     const QWidget *widget = nullptr) {
  constexpr QSize kLogicalSize{24, 24};
  QImage image(
      QSize(qRound(kLogicalSize.width() * device_pixel_ratio),
            qRound(kLogicalSize.height() * device_pixel_ratio)),
      QImage::Format_ARGB32_Premultiplied);
  image.setDevicePixelRatio(device_pixel_ratio);
  image.fill(Qt::transparent);

  QPainter painter(&image);
  QStyleOptionViewItem option;
  option.rect = QRect(QPoint{}, kLogicalSize);
  option.palette = palette;
  option.direction = direction;
  option.state = state;
  style.drawPrimitive(QStyle::PE_IndicatorBranch, &option, &painter, widget);
  painter.end();
  return image;
}

int count_pixels(const QImage &image, const QColor &color) {
  int count = 0;
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      if (image.pixelColor(x, y) == color) {
        ++count;
      }
    }
  }
  return count;
}

int count_columns_with_color(const QImage &image, const QColor &color) {
  int count = 0;
  for (int x = 0; x < image.width(); ++x) {
    bool found = false;
    for (int y = 0; y < image.height(); ++y) {
      if (image.pixelColor(x, y) == color) {
        found = true;
        break;
      }
    }
    count += found ? 1 : 0;
  }
  return count;
}


} // namespace

/// Drives the documented Qt model drag/drop interface against a live scene.
class OutlinerPanelDropTests : public ::testing::Test {
protected:
  void SetUp() override {
    QApplication::setStyle(QStringLiteral("Fusion"));
    application_.setQuitOnLastWindowClosed(false);
    panel_.setAttribute(Qt::WA_DontShowOnScreen, true);
    panel_.resize(460, 560);
    panel_.show();
    tree_ = panel_.findChild<QTreeView *>(QStringLiteral("outliner.tree"));
    ASSERT_NE(tree_, nullptr);
    refresh();
  }

  quader::EntityId create(const char *name,
                          quader::EntityId parent = quader::kInvalidEntityId) {
    const auto result = editor_.scene().create_entity({
        .name = name,
        .kind = quader::QuaderEntityKind::Empty,
        .parent = parent,
    });
    EXPECT_TRUE(result.ok()) << result.error_message();
    return result.ok() ? result.value().id : quader::kInvalidEntityId;
  }

  void refresh() {
    panel_.refresh(context_);
    QCoreApplication::processEvents();
  }

  QModelIndex index(quader::EntityId entity) const {
    return find_entity_index(*tree_->model(), entity);
  }

  std::unique_ptr<QMimeData>
  mime(std::initializer_list<quader::EntityId> entities) const {
    QModelIndexList indexes;
    for (const quader::EntityId entity : entities) {
      indexes.push_back(index(entity));
    }
    return std::unique_ptr<QMimeData>(tree_->model()->mimeData(indexes));
  }

  bool drop(const QMimeData &data, const QModelIndex &parent, int row = -1) {
    return tree_->model()->dropMimeData(&data, Qt::MoveAction, row, 0, parent);
  }

  int argc_ = 1;
  char application_name_[21] = "quader-outliner-drop";
  char *argv_[2] = {application_name_, nullptr};
  QApplication application_{argc_, argv_};
  quader::SceneSession session_;
  quader::QuaderEditorApi editor_{
      quader::QuaderApiSessionInternalAccess::from_scene_session(session_)};
  quader::UISettings ui_settings_ = quader::UISettings::defaults();
  quader::ViewportSettings viewport_settings_ =
      quader::ViewportSettings::defaults();
  quader::LocalizationService localization_ = loaded_localization();
  quader::UIContext context_{
      .intent = {.editor = editor_},
      .scene = {.document = session_.scene(),
                .assets = session_.assets(),
                .modeling = session_.modeling(),
                .selection = session_.selection()},
      .preferences = {.ui = ui_settings_, .viewport = viewport_settings_},
      .localization = localization_,
  };
  quader::OutlinerPanel panel_;
  QTreeView *tree_ = nullptr;
};

TEST_F(OutlinerPanelDropTests,
       MultiSelectionMovesIntoAndOutOfGroupsWithWorldPlacementAndSingleUndo) {
  const quader::EntityId first = create("First");
  const quader::EntityId second = create("Second");
  const quader::EntityId group = create("Translated Group");
  const quader::EntityId existing = create("Existing Member", group);
  ASSERT_TRUE(editor_.scene()
                  .set_transform(group, {.position = {10.0f, 20.0f, 30.0f}})
                  .ok());
  ASSERT_TRUE(editor_.scene()
                  .set_transform(first, {.position = {2.0f, 3.0f, 4.0f}})
                  .ok());
  const quader::QMat4 first_world = session_.scene().world_transform_matrix(first);
  const quader::QMat4 second_world = session_.scene().world_transform_matrix(second);
  ASSERT_TRUE(editor_.selection()
                  .select_objects_for_inspection({first, second}, second)
                  .ok());
  refresh();
  tree_->collapse(index(group));
  const auto data = mime({second, first});
  ASSERT_NE(data, nullptr);
  ASSERT_TRUE(tree_->model()->canDropMimeData(data.get(), Qt::MoveAction,
                                             -1, 0, index(group)));
  ASSERT_TRUE(drop(*data, index(group)));
  refresh();
  EXPECT_EQ(session_.scene().children(group),
            (std::vector<quader::EntityId>{existing, first, second}));
  EXPECT_TRUE(tree_->isExpanded(index(group)));
  expect_selection(*tree_, session_.scene(), {first, second}, second);
  EXPECT_EQ(session_.scene().world_transform_matrix(first).values, first_world.values);
  EXPECT_EQ(session_.scene().world_transform_matrix(second).values, second_world.values);

  const QString screenshot_path = qEnvironmentVariable("QUADER_OUTLINER_SCREENSHOT");
  if (!screenshot_path.isEmpty()) {
    QApplication::setPalette(quader::qt_obsidian_application_palette(
        QApplication::style()->standardPalette()));
    panel_.resize(320, 360);
    panel_.show();
    QCoreApplication::processEvents();
    EXPECT_TRUE(panel_.grab().save(screenshot_path, "PNG"));
  }

  ASSERT_TRUE(editor_.history().undo().ok());
  refresh();
  EXPECT_EQ(session_.scene().children(session_.scene().scene_root()),
            (std::vector<quader::EntityId>{first, second, group}));
  EXPECT_EQ(session_.scene().children(group),
            std::vector<quader::EntityId>{existing});
  expect_selection(*tree_, session_.scene(), {first, second}, second);
  ASSERT_TRUE(editor_.history().redo().ok());
  refresh();
  EXPECT_EQ(session_.scene().children(group),
            (std::vector<quader::EntityId>{existing, first, second}));

  const auto outward = mime({second, first});
  ASSERT_NE(outward, nullptr);
  ASSERT_TRUE(drop(*outward, {}));
  refresh();
  EXPECT_EQ(session_.scene().children(session_.scene().scene_root()),
            (std::vector<quader::EntityId>{group, first, second}));
  EXPECT_EQ(session_.scene().world_transform_matrix(first).values, first_world.values);
  EXPECT_EQ(session_.scene().world_transform_matrix(second).values, second_world.values);
  expect_selection(*tree_, session_.scene(), {first, second}, second);
}

TEST_F(OutlinerPanelDropTests,
       SingleAndNoncontiguousRowsReorderThroughQtInsertionPositions) {
  const quader::EntityId first = create("First");
  const quader::EntityId second = create("Second");
  const quader::EntityId third = create("Third");
  const quader::EntityId fourth = create("Fourth");
  refresh();
  const auto single = mime({fourth});
  ASSERT_NE(single, nullptr);
  ASSERT_TRUE(drop(*single, index(first).parent(), index(first).row()));
  refresh();
  EXPECT_EQ(session_.scene().children(session_.scene().scene_root()),
            (std::vector<quader::EntityId>{fourth, first, second, third}));
  ASSERT_TRUE(editor_.history().undo().ok());
  refresh();

  const auto multiple = mime({third, first});
  ASSERT_NE(multiple, nullptr);
  ASSERT_TRUE(drop(*multiple, index(fourth).parent(), index(fourth).row() + 1));
  // Qt's stock startDrag removes original rows after MoveAction. That cleanup
  // must not remove a second set of rows from our scene-owned projection.
  EXPECT_FALSE(tree_->model()->removeRows(0, 1, index(first).parent()));
  refresh();
  EXPECT_EQ(session_.scene().children(session_.scene().scene_root()),
            (std::vector<quader::EntityId>{second, fourth, first, third}));
  EXPECT_EQ(index(first).row(), 2);
  EXPECT_EQ(index(third).row(), 3);
  ASSERT_TRUE(editor_.history().undo().ok());
  refresh();
  EXPECT_EQ(session_.scene().children(session_.scene().scene_root()),
            (std::vector<quader::EntityId>{first, second, third, fourth}));
}

TEST_F(OutlinerPanelDropTests, NestedSelectionMovesAsOneSubtreeAndRejectsCycles) {
  const quader::EntityId group = create("Source Group");
  const quader::EntityId child = create("Child", group);
  const quader::EntityId target = create("Target Group");
  refresh();
  const auto data = mime({child, group});
  ASSERT_NE(data, nullptr);
  EXPECT_FALSE(tree_->model()->canDropMimeData(data.get(), Qt::MoveAction,
                                              -1, 0, index(child)));
  EXPECT_FALSE(drop(*data, index(child)));
  ASSERT_TRUE(drop(*data, index(target)));
  refresh();
  EXPECT_EQ(session_.scene().children(target),
            std::vector<quader::EntityId>{group});
  EXPECT_EQ(session_.scene().children(group),
            std::vector<quader::EntityId>{child});
}

TEST_F(OutlinerPanelDropTests, StaleForeignLockedAndRootDragsCannotMutateScene) {
  const quader::EntityId source = create("Source");
  const quader::EntityId target = create("Target");
  refresh();
  const auto data = mime({source});
  ASSERT_NE(data, nullptr);
  EXPECT_EQ(mime({session_.scene().scene_root()}), nullptr);
  EXPECT_FALSE(tree_->model()->dropMimeData(data.get(), Qt::CopyAction,
                                           -1, 0, index(target)));
  QMimeData foreign;
  foreign.setData(tree_->model()->mimeTypes().front(), QByteArray{});
  EXPECT_FALSE(drop(foreign, index(target)));

  ASSERT_TRUE(editor_.scene().rename(source, "Renamed Source").ok());
  refresh();
  EXPECT_FALSE(drop(*data, index(target)));
  ASSERT_TRUE(editor_.scene().set_locked(source, true).ok());
  refresh();
  EXPECT_EQ(mime({source}), nullptr);
  ASSERT_TRUE(editor_.scene().set_locked(source, false).ok());
  ASSERT_TRUE(editor_.scene().set_locked(target, true).ok());
  refresh();
  const auto unlocked = mime({source});
  ASSERT_NE(unlocked, nullptr);
  EXPECT_FALSE(drop(*unlocked, index(target)));

  ASSERT_TRUE(editor_.scene().set_locked(target, false).ok());
  refresh();
  const auto before_rebind = mime({source});
  ASSERT_NE(before_rebind, nullptr);
  panel_.abandon_runtime();
  refresh();
  EXPECT_FALSE(drop(*before_rebind, index(target)));
  EXPECT_EQ(session_.scene().children(session_.scene().scene_root()),
            (std::vector<quader::EntityId>{source, target}));
}

TEST(OutlinerPanelTests, TreeFillsThePanelContentsRect) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-outliner-layout-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));

  quader::OutlinerPanel panel;
  panel.resize(320, 480);
  ASSERT_NE(panel.layout(), nullptr);
  ASSERT_TRUE(panel.layout()->activate());

  auto *tree = panel.findChild<QTreeView *>(QStringLiteral("outliner.tree"));
  ASSERT_NE(tree, nullptr);
  EXPECT_EQ(tree->geometry(), panel.contentsRect());
  EXPECT_EQ(panel.focusProxy(), tree);
  EXPECT_EQ(tree->selectionMode(), QAbstractItemView::ExtendedSelection);
  EXPECT_EQ(tree->selectionBehavior(), QAbstractItemView::SelectRows);
  EXPECT_TRUE(tree->dragEnabled());
  EXPECT_EQ(tree->dragDropMode(), QAbstractItemView::InternalMove);
  EXPECT_EQ(tree->model()->columnCount(), 1);

  EXPECT_EQ(tree->editTriggers(), QAbstractItemView::EditKeyPressed)
      << "double-click remains available for tree expansion; F2 and the "
         "context menu own rename";
}

TEST(OutlinerPanelTests,
     RelationshipStyleRetainsFusionArrowsDirectionScaleAndPlatformPalette) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-outliner-relationship-style-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));

  const QColor connector(17, 121, 213);
  const QColor arrow(229, 31, 71);
  QPalette themed_palette = application.palette();
  for (const QPalette::ColorGroup group :
       {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
    themed_palette.setColor(group, QPalette::Midlight, connector);
    themed_palette.setColor(group, QPalette::Text, QColor(43, 211, 97));
    themed_palette.setColor(group, QPalette::WindowText, arrow);
  }
  QApplication::setPalette(themed_palette);

  quader::OutlinerPanel panel;
  const quader::QtUiScaleMetrics ui_scale(1.5F, 7U);
  panel.apply_ui_scale(ui_scale);
  auto *tree = panel.findChild<QTreeView *>(QStringLiteral("outliner.tree"));
  ASSERT_NE(tree, nullptr);
  ASSERT_NE(tree->style(), nullptr);
  EXPECT_EQ(tree->style()->objectName(),
            QStringLiteral("quaderTreeRelationshipPresentation"));

  std::unique_ptr<QStyle> stock_fusion(
      QStyleFactory::create(QStringLiteral("fusion")));
  ASSERT_NE(stock_fusion, nullptr);
  const int design_indentation = stock_fusion->pixelMetric(
      QStyle::PM_TreeViewIndentation, nullptr, tree);
  EXPECT_EQ(tree->style()->pixelMetric(QStyle::PM_TreeViewIndentation, nullptr,
                                      tree),
            ui_scale.px(design_indentation));

  constexpr QStyle::State kLeafState =
      QStyle::State_Enabled | QStyle::State_Active | QStyle::State_Item |
      QStyle::State_Sibling;
  const QImage left_to_right =
      render_branch(*tree->style(), themed_palette, kLeafState,
                    Qt::LeftToRight, 1.0, tree);
  const QImage right_to_left =
      render_branch(*tree->style(), themed_palette, kLeafState,
                    Qt::RightToLeft, 1.0, tree);
  constexpr int kCenter = 11;
  EXPECT_EQ(left_to_right.pixelColor(kCenter, 0), connector);
  EXPECT_EQ(left_to_right.pixelColor(kCenter, 23), connector);
  EXPECT_EQ(left_to_right.pixelColor(23, kCenter), connector);
  EXPECT_EQ(left_to_right.pixelColor(0, kCenter), QColor(Qt::transparent));
  EXPECT_EQ(right_to_left.pixelColor(0, kCenter), connector);
  EXPECT_EQ(right_to_left.pixelColor(23, kCenter), QColor(Qt::transparent));

  for (const QStyle::State expansion :
       {QStyle::State_None, QStyle::State_Open}) {
    const QStyle::State child_state =
        kLeafState | QStyle::State_Children | expansion;
    const QImage presented =
        render_branch(*tree->style(), themed_palette, child_state,
                      Qt::LeftToRight, 1.0, tree);
    const QImage stock = render_branch(*stock_fusion, themed_palette,
                                       child_state, Qt::LeftToRight);
    int arrow_pixels = 0;
    for (int y = kCenter - 4; y <= kCenter + 4; ++y) {
      for (int x = kCenter - 4; x <= kCenter + 4; ++x) {
        EXPECT_EQ(presented.pixelColor(x, y), stock.pixelColor(x, y));
        arrow_pixels += presented.pixelColor(x, y).alpha() != 0 ? 1 : 0;
      }
    }
    EXPECT_GT(arrow_pixels, 0)
        << "the custom relationship painter must leave Fusion's disclosure "
           "triangle intact";
  }

  const QImage high_dpi =
      render_branch(*tree->style(), themed_palette, QStyle::State_Sibling,
                    Qt::LeftToRight, 2.0, tree);
  EXPECT_EQ(count_columns_with_color(high_dpi, connector), 1)
      << "a cosmetic hierarchy spine remains one backing pixel wide";
  EXPECT_GE(count_pixels(high_dpi, connector), high_dpi.height() - 2);

  QApplication::setPalette(QPalette{});
  QCoreApplication::processEvents();
  const QPalette platform_palette = QApplication::palette();
  const QImage platform_spine = render_branch(
      *tree->style(), platform_palette,
      QStyle::State_Enabled | QStyle::State_Active | QStyle::State_Sibling,
      Qt::LeftToRight, 1.0, tree);
  EXPECT_EQ(platform_spine.pixelColor(kCenter, 0),
            platform_palette.color(QPalette::Active, QPalette::Text));
}


TEST(OutlinerPanelTests,
     RepeatedCollapseExpandWithStableSelectionDoesNotResynchronize) {
  constexpr std::uint64_t kSelectionRevision = 41;
  for (int interaction = 0; interaction < 1000; ++interaction) {
    EXPECT_FALSE(quader::outliner_selection_sync_required(
        false, true, kSelectionRevision, kSelectionRevision));
  }

  EXPECT_TRUE(quader::outliner_selection_sync_required(
      true, true, kSelectionRevision, kSelectionRevision));
  EXPECT_TRUE(quader::outliner_selection_sync_required(
      false, false, 0, kSelectionRevision));
  EXPECT_TRUE(quader::outliner_selection_sync_required(
      false, true, kSelectionRevision, kSelectionRevision + 1));
}

TEST(OutlinerPanelTests,
     NonPresentedLayerAndTransformChangesRetainExistingRows) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-outliner-retained-row-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));

  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto entity = editor.scene().create_entity({
      .name = "Layer Member",
      .kind = quader::QuaderEntityKind::Empty,
  });
  ASSERT_TRUE(entity.ok()) << entity.error_message();
  ASSERT_TRUE(editor.scene().select(entity.value().id).ok());

  quader::UISettings ui_settings = quader::UISettings::defaults();
  quader::ViewportSettings viewport_settings =
      quader::ViewportSettings::defaults();
  quader::LocalizationService localization = loaded_localization();
  quader::UIContext context{
      .intent = {.editor = editor},
      .scene = {.document = session.scene(),
                .assets = session.assets(),
                .modeling = session.modeling(),
                .selection = session.selection()},
      .preferences = {.ui = ui_settings, .viewport = viewport_settings},
      .localization = localization,
  };

  quader::OutlinerPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.resize(420, 420);
  panel.show();
  panel.refresh(context);
  QCoreApplication::processEvents();

  auto *tree = panel.findChild<QTreeView *>(QStringLiteral("outliner.tree"));
  ASSERT_NE(tree, nullptr);
  QAbstractItemModel *const stable_model = tree->model();
  ASSERT_NE(stable_model, nullptr);
  const QModelIndex entity_index =
      find_entity_index(*stable_model, entity.value().id);
  ASSERT_TRUE(entity_index.isValid());
  const QPersistentModelIndex persistent_entity_index(entity_index);
  int rows_removed = 0;
  QObject::connect(stable_model, &QAbstractItemModel::rowsRemoved, &panel,
                   [&rows_removed](const QModelIndex &, int, int) {
                     ++rows_removed;
                   });

  const std::uint64_t content_revision_before =
      session.scene().content_revision();
  const std::uint64_t presentation_revision_before =
      session.scene().hierarchy_presentation_revision();
  const auto layer =
      editor.layers().create_from_selection("Presentation-Neutral Layer");
  ASSERT_TRUE(layer.ok()) << layer.error_message();
  ASSERT_TRUE(layer.changed());
  ASSERT_TRUE(layer.value().visible);
  ASSERT_FALSE(layer.value().locked);
  ASSERT_GT(session.scene().content_revision(), content_revision_before);
  ASSERT_EQ(session.scene().hierarchy_presentation_revision(),
            presentation_revision_before);
  ASSERT_EQ(editor.query().entity(entity.value().id)->layer_id,
            layer.value().id);
  quader::TransformComponent moved =
      *session.scene().transform(entity.value().id);
  moved.position = {4.0f, 5.0f, 6.0f};
  ASSERT_TRUE(session.scene().set_transform(entity.value().id, moved));
  ASSERT_GT(session.scene().content_revision(), content_revision_before);
  ASSERT_EQ(session.scene().hierarchy_presentation_revision(),
            presentation_revision_before);

  panel.refresh(context);
  QCoreApplication::processEvents();

  EXPECT_EQ(tree->model(), stable_model);
  EXPECT_EQ(rows_removed, 0);
  ASSERT_TRUE(persistent_entity_index.isValid());
  EXPECT_EQ(static_cast<quader::EntityId>(
                persistent_entity_index.data(kEntityIdRole).toULongLong()),
            entity.value().id);
  EXPECT_EQ(persistent_entity_index.data().toString(),
            QStringLiteral("Layer Member"));
}

TEST(OutlinerPanelTests,
     QtSelectionModelDrivesClickShiftControlKeyboardAndBlankSelection) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-outliner-interaction-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));
  application.setQuitOnLastWindowClosed(false);

  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto create = [&editor](const char *name,
                                quader::EntityId parent =
                                    quader::kInvalidEntityId) {
    return editor.scene().create_entity(quader::QuaderCreateEntityArgs{
        .name = name,
        .kind = quader::QuaderEntityKind::Empty,
        .parent = parent,
    });
  };
  const auto first = create("First");
  const auto second = create("Second");
  const auto third = create("Third");
  const auto fourth = create("Fourth");
  ASSERT_TRUE(first.ok()) << first.error_message();
  ASSERT_TRUE(second.ok()) << second.error_message();
  ASSERT_TRUE(third.ok()) << third.error_message();
  ASSERT_TRUE(fourth.ok()) << fourth.error_message();
  ASSERT_TRUE(editor.selection()
                  .select_objects_for_inspection(
                      std::span<const quader::EntityId>{},
                      quader::kInvalidEntityId)
                  .ok());

  quader::UISettings ui_settings = quader::UISettings::defaults();
  quader::ViewportSettings viewport_settings =
      quader::ViewportSettings::defaults();
  quader::LocalizationService localization = loaded_localization();
  quader::UIContext context{
      .intent = {.editor = editor},
      .scene = {.document = session.scene(),
                .assets = session.assets(),
                .modeling = session.modeling(),
                .selection = session.selection()},
      .preferences = {.ui = ui_settings, .viewport = viewport_settings},
      .localization = localization,
  };

  quader::OutlinerPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.resize(460, 560);
  panel.show();
  panel.refresh(context);
  QCoreApplication::processEvents();

  auto *tree = panel.findChild<QTreeView *>(QStringLiteral("outliner.tree"));
  ASSERT_NE(tree, nullptr);
  tree->expandAll();
  QCoreApplication::processEvents();
  QAbstractItemModel *const stable_model = tree->model();
  ASSERT_NE(stable_model, nullptr);

  const QModelIndex first_index =
      find_entity_index(*stable_model, first.value().id);
  const QModelIndex second_index =
      find_entity_index(*stable_model, second.value().id);
  const QModelIndex third_index =
      find_entity_index(*stable_model, third.value().id);
  const QModelIndex fourth_index =
      find_entity_index(*stable_model, fourth.value().id);
  ASSERT_TRUE(first_index.isValid());
  ASSERT_TRUE(second_index.isValid());
  ASSERT_TRUE(third_index.isValid());
  ASSERT_TRUE(fourth_index.isValid());
  const QPersistentModelIndex persistent_first(first_index);

  QWidget &viewport = *tree->viewport();
  const QPoint first_position = tree->visualRect(first_index).center();
  const QPoint second_position = tree->visualRect(second_index).center();
  const QPoint third_position = tree->visualRect(third_index).center();

  const std::uint64_t before_plain_click = session.scene().selection_revision();
  click(viewport, first_position, Qt::LeftButton);
  {
    SCOPED_TRACE("plain click");
    expect_selection(*tree, session.scene(), {first.value().id},
                     first.value().id);
  }
  EXPECT_EQ(tree->currentIndex(), first_index);
  EXPECT_EQ(session.scene().selection_revision(), before_plain_click + 1u);

  send_key(*tree, Qt::Key_End, Qt::ShiftModifier);
  {
    SCOPED_TRACE("Shift+End range");
    expect_selection(*tree, session.scene(),
                     {first.value().id, second.value().id, third.value().id,
                      fourth.value().id},
                     fourth.value().id);
  }

  click(viewport, second_position, Qt::LeftButton, Qt::ControlModifier);
  {
    SCOPED_TRACE("Control click");
    expect_selection(*tree, session.scene(),
                     {first.value().id, third.value().id, fourth.value().id},
                     fourth.value().id);
  }

  const QPoint empty_position(4, viewport.height() - 4);
  ASSERT_FALSE(tree->indexAt(empty_position).isValid());
  const QModelIndex current_before_blank = tree->currentIndex();
  ASSERT_TRUE(current_before_blank.isValid());
  click(viewport, empty_position, Qt::LeftButton);
  {
    SCOPED_TRACE("blank click");
    expect_selection(*tree, session.scene(), {}, quader::kInvalidEntityId);
  }
  EXPECT_EQ(tree->currentIndex(), current_before_blank)
      << "Qt keeps currentIndex independent from the cleared selection";

  send_mouse(viewport, QEvent::MouseButtonPress, first_position,
             Qt::LeftButton, Qt::LeftButton);
  panel.refresh(context);
  EXPECT_EQ(tree->model(), stable_model)
      << "selection-only changes must not rebuild the model mid-drag";
  EXPECT_TRUE(persistent_first.isValid())
      << "selection-only refreshes must not reset persistent indexes";
  send_mouse(viewport, QEvent::MouseButtonRelease, first_position,
             Qt::LeftButton, Qt::NoButton);
  expect_selection(*tree, session.scene(), {first.value().id}, first.value().id);
  ASSERT_TRUE(editor.history().undo().ok());
  panel.refresh(context);
  QCoreApplication::processEvents();
  expect_selection(*tree, session.scene(), {}, quader::kInvalidEntityId);
  ASSERT_TRUE(editor.history().redo().ok());
  panel.refresh(context);
  QCoreApplication::processEvents();
  expect_selection(*tree, session.scene(), {first.value().id}, first.value().id);
  const std::array same_primary_external_selection{third.value().id};
  ASSERT_TRUE(editor.selection()
                  .select_objects_for_inspection(
                      same_primary_external_selection, third.value().id)
                  .ok());
  panel.refresh(context);
  EXPECT_TRUE(persistent_first.isValid());
  click(viewport, second_position, Qt::LeftButton, Qt::ShiftModifier);
  {
    SCOPED_TRACE("external same-primary selection then Shift click");
    expect_selection(*tree, session.scene(),
                     {second.value().id, third.value().id},
                     second.value().id);
  }

  ASSERT_TRUE(editor.selection()
                  .select_objects_for_inspection(
                      std::span<const quader::EntityId>{},
                      quader::kInvalidEntityId)
                  .ok());
  panel.refresh(context);
  EXPECT_FALSE(tree->currentIndex().isValid());
  click(viewport, second_position, Qt::LeftButton, Qt::ShiftModifier);
  {
    SCOPED_TRACE("external empty selection resets the stale Shift anchor");
    expect_selection(*tree, session.scene(),
                     {session.scene().scene_root(), first.value().id,
                      second.value().id},
                     second.value().id);
  }

  click(viewport, first_position, Qt::LeftButton);
  send_key(*tree, Qt::Key_Down, Qt::ShiftModifier);
  {
    SCOPED_TRACE("Shift+Down");
    expect_selection(*tree, session.scene(),
                     {first.value().id, second.value().id},
                     second.value().id);
  }

  const std::array external_selection{first.value().id, third.value().id};
  ASSERT_TRUE(editor.selection()
                  .select_objects_for_inspection(external_selection,
                                                 third.value().id)
                  .ok());
  panel.refresh(context);
  EXPECT_TRUE(persistent_first.isValid());
  {
    SCOPED_TRACE("external selection sync");
    expect_selection(*tree, session.scene(),
                     {first.value().id, third.value().id}, third.value().id);
  }

  bool keyboard_menu_opened = false;
  QTimer::singleShot(0, &panel, [&]() {
    auto *menu = panel.findChild<QMenu *>(
        QStringLiteral("outliner.context_menu"));
    if (menu != nullptr) {
      keyboard_menu_opened = true;
      const QList<QAction *> actions = menu->actions();
      if (actions.size() != 11) {
        ADD_FAILURE() << "Unexpected outliner context-menu action count: "
                      << actions.size();
        menu->close();
        return;
      }
      EXPECT_EQ(actions.back()->objectName(),
                QStringLiteral("outliner.action.delete"));
      EXPECT_TRUE(actions[2]->isSeparator());
      EXPECT_TRUE(actions[5]->isSeparator());
      EXPECT_TRUE(actions[9]->isSeparator());

      auto *hide_action = menu->findChild<QAction *>(
          QStringLiteral("outliner.action.hide_selected"));
      auto *duplicate_action = menu->findChild<QAction *>(
          QStringLiteral("outliner.action.duplicate"));
      auto *group_action = menu->findChild<QAction *>(
          QStringLiteral("outliner.action.group"));
      EXPECT_NE(hide_action, nullptr);
      EXPECT_NE(duplicate_action, nullptr);
      EXPECT_NE(group_action, nullptr);
      if (hide_action != nullptr) {
        EXPECT_TRUE(hide_action->text().endsWith(
            QStringLiteral("\t") + QString::fromStdString(
                                       editor.commands().shortcut_for(
                                           quader::quader_command::
                                               kSceneHideSelection))));
      }
      if (duplicate_action != nullptr) {
        EXPECT_TRUE(duplicate_action->text().endsWith(
            QStringLiteral("\t") + QString::fromStdString(
                                       editor.commands().shortcut_for(
                                           quader::quader_command::
                                               kDuplicateSelection))));
      }
      if (group_action != nullptr) {
        EXPECT_TRUE(group_action->text().endsWith(
            QStringLiteral("\t") + QString::fromStdString(
                                       editor.commands().shortcut_for(
                                           quader::quader_command::
                                               kGroupSelection))));
      }
      menu->close();
    }
  });
  QContextMenuEvent keyboard_context_menu(
      QContextMenuEvent::Keyboard, QPoint{}, QPoint{});
  QCoreApplication::sendEvent(tree->viewport(), &keyboard_context_menu);
  EXPECT_TRUE(keyboard_menu_opened)
      << "the stock keyboard context-menu gesture must target currentIndex";

  click(viewport, first_position, Qt::RightButton);
  {
    SCOPED_TRACE("right click selected row");
    expect_selection(*tree, session.scene(),
                     {first.value().id, third.value().id}, first.value().id);
  }
  click(viewport, second_position, Qt::RightButton);
  {
    SCOPED_TRACE("right click unselected row");
    expect_selection(*tree, session.scene(), {second.value().id},
                     second.value().id);
  }

  EXPECT_EQ(tree->model()->columnCount(), 1);
  ASSERT_TRUE(editor.scene().set_visible(first.value().id, false).ok());
  panel.refresh(context);
  QModelIndex styled_first = find_entity_index(*tree->model(), first.value().id);
  ASSERT_TRUE(styled_first.isValid());
  EXPECT_FALSE(styled_first.data(Qt::CheckStateRole).isValid());
  EXPECT_FALSE((styled_first.flags() & Qt::ItemIsUserCheckable) != 0);
  EXPECT_TRUE(styled_first.data(Qt::UserRole + 2).toBool());
  ASSERT_TRUE(editor.scene().set_visible(first.value().id, true).ok());
  ASSERT_TRUE(editor.scene().set_locked(first.value().id, true).ok());
  panel.refresh(context);
  styled_first = find_entity_index(*tree->model(), first.value().id);
  EXPECT_TRUE(styled_first.data(Qt::UserRole + 2).toBool());
  EXPECT_FALSE((styled_first.flags() & Qt::ItemIsDragEnabled) != 0);
  EXPECT_FALSE((styled_first.flags() & Qt::ItemIsDropEnabled) != 0);
  ASSERT_TRUE(editor.scene().set_locked(first.value().id, false).ok());
  panel.refresh(context);
  styled_first = find_entity_index(*tree->model(), first.value().id);
  EXPECT_FALSE(styled_first.data(Qt::UserRole + 2).toBool());
  const QModelIndex old_root = tree->model()->index(0, 0);
  ASSERT_TRUE(old_root.isValid());
  ASSERT_TRUE(tree->model()->hasChildren(old_root));
  tree->collapse(old_root);
  QCoreApplication::processEvents();
  ASSERT_FALSE(tree->isExpanded(old_root));

  quader::SceneSession other_session;
  quader::QuaderEditorApi other_editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(
          other_session));
  const auto other = other_editor.scene().create_entity({
      .name = "Other Document",
      .kind = quader::QuaderEntityKind::Empty,
  });
  ASSERT_TRUE(other.ok()) << other.error_message();
  bool alternate_name = false;
  std::string other_name = "Other Document";
  while (other_session.scene().content_revision() <
         session.scene().content_revision()) {
    alternate_name = !alternate_name;
    other_name = alternate_name ? "Other A" : "Other B";
    ASSERT_TRUE(other_editor.scene()
                    .rename(other.value().id, other_name)
                    .ok());
  }
  ASSERT_EQ(other_session.scene().content_revision(),
            session.scene().content_revision());

  quader::UIContext other_context{
      .intent = {.editor = other_editor},
      .scene = {.document = other_session.scene(),
                .assets = other_session.assets(),
                .modeling = other_session.modeling(),
                .selection = other_session.selection()},
      .preferences = {.ui = ui_settings, .viewport = viewport_settings},
      .localization = localization,
  };
  panel.refresh(other_context);
  QCoreApplication::processEvents();
  const QModelIndex other_root = tree->model()->index(0, 0);
  ASSERT_TRUE(other_root.isValid());
  EXPECT_TRUE(tree->isExpanded(other_root))
      << "document-local collapsed IDs must not leak between scenes";
  EXPECT_FALSE(
      find_entity_index(*tree->model(), fourth.value().id).isValid());
  const QModelIndex other_index =
      find_entity_index(*tree->model(), other.value().id);
  ASSERT_TRUE(other_index.isValid());
  EXPECT_EQ(other_index.data().toString(),
            QString::fromStdString(other_name));
  EXPECT_NE(other_index.data().toString(), QStringLiteral("First"))
      << "equal revisions from different documents must not reuse a tree";
}

TEST(OutlinerPanelTests,
     ContextMenuAppliesVisibilityAndLockStateToTheSelectedSet) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-outliner-context-state-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));
  application.setQuitOnLastWindowClosed(false);

  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto first = editor.scene().create_entity({
      .name = "First",
      .kind = quader::QuaderEntityKind::Empty,
  });
  const auto second = editor.scene().create_entity({
      .name = "Second",
      .kind = quader::QuaderEntityKind::Empty,
  });
  ASSERT_TRUE(first.ok()) << first.error_message();
  ASSERT_TRUE(second.ok()) << second.error_message();
  ASSERT_TRUE(editor.scene().set_visible(second.value().id, false).ok());
  const std::array selected{first.value().id, second.value().id};
  ASSERT_TRUE(editor.selection()
                  .select_objects_for_inspection(selected, first.value().id)
                  .ok());
  ASSERT_TRUE(editor.commands()
                  .set_shortcut(quader::quader_command::kDuplicateSelection,
                                "Ctrl+Alt+D")
                  .ok());

  quader::UISettings ui_settings = quader::UISettings::defaults();
  quader::ViewportSettings viewport_settings =
      quader::ViewportSettings::defaults();
  quader::LocalizationService localization = loaded_localization();
  quader::UIContext context{
      .intent = {.editor = editor},
      .scene = {.document = session.scene(),
                .assets = session.assets(),
                .modeling = session.modeling(),
                .selection = session.selection()},
      .preferences = {.ui = ui_settings, .viewport = viewport_settings},
      .localization = localization,
  };

  quader::OutlinerPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.resize(460, 560);
  panel.show();
  panel.refresh(context);
  QCoreApplication::processEvents();
  auto *tree = panel.findChild<QTreeView *>(QStringLiteral("outliner.tree"));
  ASSERT_NE(tree, nullptr);
  tree->expandAll();
  QCoreApplication::processEvents();

  const auto invoke_action =
      [&](const QString &object_name,
          const std::function<void(const QMenu &, const QAction &)> &inspect =
              {}) {
        bool invoked = false;
        QTimer::singleShot(0, &panel, [&]() {
          auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
          if (menu == nullptr ||
              menu->objectName() != QStringLiteral("outliner.context_menu")) {
            ADD_FAILURE() << "Expected the outliner context menu to open.";
            if (QWidget *popup = QApplication::activePopupWidget()) {
              popup->close();
            }
            return;
          }
          auto *action = menu->findChild<QAction *>(object_name);
          if (action == nullptr || !action->isEnabled()) {
            ADD_FAILURE() << "Expected enabled action "
                          << object_name.toStdString();
            menu->close();
            return;
          }
          EXPECT_EQ(menu->actions().back()->objectName(),
                    QStringLiteral("outliner.action.delete"));
          if (inspect) {
            inspect(*menu, *action);
          }
          invoked = true;
          menu->setActiveAction(action);
          QKeyEvent accept_action(QEvent::KeyPress, Qt::Key_Return,
                                  Qt::NoModifier);
          QApplication::sendEvent(menu, &accept_action);
          if (menu->isVisible()) {
            menu->close();
          }
        });

        const QModelIndex target =
            find_entity_index(*tree->model(), first.value().id);
        ASSERT_TRUE(target.isValid());
        const QPoint position = tree->visualRect(target).center();
        QContextMenuEvent event(QContextMenuEvent::Mouse, position,
                                tree->viewport()->mapToGlobal(position));
        QCoreApplication::sendEvent(tree->viewport(), &event);
        QCoreApplication::processEvents();
        EXPECT_TRUE(invoked);
        panel.refresh(context);
        QCoreApplication::processEvents();
      };

  invoke_action(
      QStringLiteral("outliner.action.show_selected"),
      [&](const QMenu &menu, const QAction &) {
        auto *duplicate_action = menu.findChild<QAction *>(
            QStringLiteral("outliner.action.duplicate"));
        ASSERT_NE(duplicate_action, nullptr);
        EXPECT_TRUE(duplicate_action->text().endsWith(
            QStringLiteral("\tCtrl+Alt+D")));
        EXPECT_TRUE(duplicate_action->shortcut().isEmpty())
            << "context rows display effective shortcuts but must not own "
               "a second Qt shortcut route";
      });
  ASSERT_NE(session.scene().visibility(first.value().id), nullptr);
  ASSERT_NE(session.scene().visibility(second.value().id), nullptr);
  EXPECT_TRUE(session.scene().visibility(first.value().id)->visible);
  EXPECT_TRUE(session.scene().visibility(second.value().id)->visible);
  EXPECT_EQ(sorted_entities(session.scene().selected_entities()),
            sorted_entities({first.value().id, second.value().id}));

  invoke_action(
      QStringLiteral("outliner.action.hide_selected"),
      [&](const QMenu &, const QAction &action) {
        EXPECT_TRUE(action.text().endsWith(
            QStringLiteral("\t") + QString::fromStdString(
                                       editor.commands().shortcut_for(
                                           quader::quader_command::
                                               kSceneHideSelection))));
      });
  EXPECT_FALSE(session.scene().visibility(first.value().id)->visible);
  EXPECT_FALSE(session.scene().visibility(second.value().id)->visible);
  EXPECT_TRUE(session.scene().selected_entities().empty())
      << "the context route must preserve Hide Selection command side effects";

  ASSERT_TRUE(editor.selection()
                  .select_objects_for_inspection(selected, first.value().id)
                  .ok());
  panel.refresh(context);
  invoke_action(QStringLiteral("outliner.action.show_selected"));
  invoke_action(QStringLiteral("outliner.action.lock_selected"));
  ASSERT_NE(session.scene().lock(first.value().id), nullptr);
  ASSERT_NE(session.scene().lock(second.value().id), nullptr);
  EXPECT_TRUE(session.scene().lock(first.value().id)->locked);
  EXPECT_TRUE(session.scene().lock(second.value().id)->locked);
  EXPECT_TRUE(session.scene().selected_entities().empty());

  ASSERT_TRUE(editor.selection()
                  .select_objects_for_inspection(selected, first.value().id)
                  .ok());
  panel.refresh(context);
  invoke_action(QStringLiteral("outliner.action.unlock_selected"));
  EXPECT_FALSE(session.scene().lock(first.value().id)->locked);
  EXPECT_FALSE(session.scene().lock(second.value().id)->locked);

  invoke_action(
      QStringLiteral("outliner.action.delete"),
      [&](const QMenu &, const QAction &) {
        ASSERT_TRUE(editor.selection()
                        .select_objects_for_inspection(
                            {second.value().id}, second.value().id)
                        .ok());
      });
  EXPECT_TRUE(session.scene().exists(first.value().id));
  EXPECT_TRUE(session.scene().exists(second.value().id));
  EXPECT_EQ(session.scene().selected_entities(),
            std::vector<quader::EntityId>{second.value().id})
      << "a menu must not apply its Selected Objects command to a newer "
         "selection created while the popup was open";
}

namespace {

void expect_outliner_stops_after_project_rebind(bool drag_drop) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-outliner-reentrant-group-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));
  application.setQuitOnLastWindowClosed(false);

  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const std::filesystem::path project_root =
      std::filesystem::path(temporary.path().toStdWString()) / "Project";
  std::error_code directory_error;
  ASSERT_TRUE(
      std::filesystem::create_directory(project_root, directory_error));
  ASSERT_FALSE(directory_error) << directory_error.message();
  quader::ProjectSession project;
  const quader::ProjectSessionResult created = project.create(project_root);
  ASSERT_TRUE(created.ok) << created.message;
  quader::QuaderEditorApi editor(project);
  editor.scene_documents().set_runtime_hooks({
      .confirm_dirty_scene =
          [](const quader::QuaderSceneDocumentInfo &, std::string_view) {
            return quader::QuaderDirtySceneDecision::Discard;
          },
  });

  quader::OpenSceneSession *active = project.active_scene();
  ASSERT_NE(active, nullptr);
  const auto first = editor.scene().create_entity({
      .name = "First Group Member",
      .kind = quader::QuaderEntityKind::Empty,
  });
  const auto second = editor.scene().create_entity({
      .name = "Second Group Member",
      .kind = quader::QuaderEntityKind::Empty,
  });
  ASSERT_TRUE(first.ok()) << first.error_message();
  ASSERT_TRUE(second.ok()) << second.error_message();
  const auto drop_target = editor.scene().create_entity({
      .name = "Drop Target",
      .kind = quader::QuaderEntityKind::Empty,
  });
  ASSERT_TRUE(drop_target.ok()) << drop_target.error_message();
  const std::array selected{first.value().id, second.value().id};
  ASSERT_TRUE(editor.selection()
                  .select_objects_for_inspection(selected, first.value().id)
                  .ok());
  auto saved_path =
      quader::ProjectRelativePath::parse_portable("Scenes/ReentrantGroup.qdr");
  ASSERT_TRUE(saved_path.ok()) << saved_path.error().message;
  const quader::QuaderApiVoidResult saved = editor.scene_documents().save_as(
      active->session_id, saved_path.take_value());
  ASSERT_TRUE(saved.ok()) << saved.error_message();
  const quader::EntityId fallback_entity =
      project.fallback_scene().scene().create_entity("Fallback Entity");
  ASSERT_NE(fallback_entity, quader::kInvalidEntityId);

  quader::UISettings ui_settings = quader::UISettings::defaults();
  quader::ViewportSettings viewport_settings =
      quader::ViewportSettings::defaults();
  quader::LocalizationService localization = loaded_localization();
  quader::UIContext context{
      .intent = {.editor = editor},
      .scene = {.document = active->session->scene(),
                .assets = active->session->assets(),
                .modeling = active->session->modeling(),
                .selection = active->session->selection()},
      .preferences = {.ui = ui_settings, .viewport = viewport_settings},
      .localization = localization,
  };

  quader::OutlinerPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.resize(460, 560);
  panel.show();
  panel.refresh(context);
  QCoreApplication::processEvents();
  auto *tree = panel.findChild<QTreeView *>(QStringLiteral("outliner.tree"));
  ASSERT_NE(tree, nullptr);
  tree->expandAll();
  QCoreApplication::processEvents();
  const QModelIndex target =
      find_entity_index(*tree->model(), first.value().id);
  ASSERT_TRUE(target.isValid());

  bool fallback_bound = false;
  quader::QuaderEditorApiSessionAccess::set_scene_binding_changed_callback(
      editor, [&](quader::SceneSession &replacement) {
        panel.abandon_runtime();
        quader::UIContext replacement_context{
            .intent = {.editor = editor},
            .scene = {.document = replacement.scene(),
                      .assets = replacement.assets(),
                      .modeling = replacement.modeling(),
                      .selection = replacement.selection()},
            .preferences = {.ui = ui_settings,
                            .viewport = viewport_settings},
            .localization = localization,
        };
        panel.refresh(replacement_context);
        fallback_bound = &replacement == &project.fallback_scene();
      });

  bool close_dispatched = false;
  const quader::QuaderEventSubscription dirty_subscription =
      editor.events().subscribe(
          quader::quader_event::kSceneDirtyChanged,
          [&](const quader::QuaderEvent &) {
            if (close_dispatched) {
              return;
            }
            close_dispatched = true;
            const quader::QuaderApiVoidResult closed =
                editor.project().close();
            EXPECT_TRUE(closed.ok()) << closed.error_message();
            EXPECT_TRUE(closed.changed());
          });
  ASSERT_TRUE(dirty_subscription.valid());

  bool action_dispatched = false;
  if (drag_drop) {
    const std::unique_ptr<QMimeData> data(
        tree->model()->mimeData(tree->selectionModel()->selectedRows(0)));
    ASSERT_NE(data, nullptr);
    const QModelIndex destination =
        find_entity_index(*tree->model(), drop_target.value().id);
    ASSERT_TRUE(destination.isValid());
    action_dispatched = tree->model()->dropMimeData(
        data.get(), Qt::MoveAction, -1, 0, destination);
  } else {
    QTimer::singleShot(0, &panel, [&]() {
      auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
      if (menu == nullptr ||
          menu->objectName() != QStringLiteral("outliner.context_menu")) {
        ADD_FAILURE() << "Expected the outliner context menu to open.";
        if (QWidget *popup = QApplication::activePopupWidget()) {
          popup->close();
        }
        return;
      }
      auto *group_action = menu->findChild<QAction *>(
          QStringLiteral("outliner.action.group"));
      ASSERT_NE(group_action, nullptr);
      ASSERT_TRUE(group_action->isEnabled());
      action_dispatched = true;
      menu->setActiveAction(group_action);
      QKeyEvent accept_action(QEvent::KeyPress, Qt::Key_Return,
                              Qt::NoModifier);
      QApplication::sendEvent(menu, &accept_action);
      if (menu->isVisible()) {
        menu->close();
      }
    });
    const QPoint target_position = tree->visualRect(target).center();
    QContextMenuEvent context_event(
        QContextMenuEvent::Mouse, target_position,
        tree->viewport()->mapToGlobal(target_position));
    QCoreApplication::sendEvent(tree->viewport(), &context_event);
  }
  QCoreApplication::processEvents();

  EXPECT_TRUE(action_dispatched);
  EXPECT_TRUE(close_dispatched);
  EXPECT_TRUE(fallback_bound);
  EXPECT_FALSE(project.has_project());
  EXPECT_TRUE(find_entity_index(*tree->model(), fallback_entity).isValid());
  panel.abandon_runtime();
}

} // namespace

TEST(OutlinerPanelTests,
     GroupCommandStopsOldDocumentAccessAfterSynchronousProjectRebind) {
  expect_outliner_stops_after_project_rebind(false);
}

TEST(OutlinerPanelTests,
     DropStopsOldDocumentAccessAfterSynchronousProjectRebind) {
  expect_outliner_stops_after_project_rebind(true);
}

TEST(OutlinerPanelTests, DoubleClickExpandsGroupsWithoutStartingRename) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-outliner-double-click-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));

  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto parent = editor.scene().create_entity({
      .name = "Expandable Group",
      .kind = quader::QuaderEntityKind::Empty,
  });
  ASSERT_TRUE(parent.ok()) << parent.error_message();
  const auto child = editor.scene().create_entity({
      .name = "Child",
      .kind = quader::QuaderEntityKind::Empty,
      .parent = parent.value().id,
  });
  ASSERT_TRUE(child.ok()) << child.error_message();

  quader::UISettings ui_settings = quader::UISettings::defaults();
  quader::ViewportSettings viewport_settings =
      quader::ViewportSettings::defaults();
  quader::LocalizationService localization = loaded_localization();
  quader::UIContext context{
      .intent = {.editor = editor},
      .scene = {.document = session.scene(),
                .assets = session.assets(),
                .modeling = session.modeling(),
                .selection = session.selection()},
      .preferences = {.ui = ui_settings, .viewport = viewport_settings},
      .localization = localization,
  };

  quader::OutlinerPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.resize(420, 420);
  panel.show();
  panel.refresh(context);
  QCoreApplication::processEvents();
  auto *tree = panel.findChild<QTreeView *>(QStringLiteral("outliner.tree"));
  ASSERT_NE(tree, nullptr);
  const QModelIndex parent_index =
      find_entity_index(*tree->model(), parent.value().id);
  ASSERT_TRUE(parent_index.isValid());
  ASSERT_TRUE(tree->isExpanded(parent_index));
  const QPoint position = tree->visualRect(parent_index).center();

  click(*tree->viewport(), position, Qt::LeftButton);
  send_mouse(*tree->viewport(), QEvent::MouseButtonDblClick, position,
             Qt::LeftButton, Qt::LeftButton);
  send_mouse(*tree->viewport(), QEvent::MouseButtonRelease, position,
             Qt::LeftButton, Qt::NoButton);
  EXPECT_FALSE(tree->isExpanded(parent_index));
  EXPECT_EQ(tree->findChild<QLineEdit *>(), nullptr);

  send_mouse(*tree->viewport(), QEvent::MouseButtonDblClick, position,
             Qt::LeftButton, Qt::LeftButton);
  send_mouse(*tree->viewport(), QEvent::MouseButtonRelease, position,
             Qt::LeftButton, Qt::NoButton);
  EXPECT_TRUE(tree->isExpanded(parent_index));
  EXPECT_EQ(tree->findChild<QLineEdit *>(), nullptr);
}

TEST(OutlinerPanelTests, ShiftRangeUsesVisibleRowsAndSkipsCollapsedChildren) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-outliner-visible-range-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));

  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto parent = editor.scene().create_entity({
      .name = "Parent",
      .kind = quader::QuaderEntityKind::Empty,
  });
  ASSERT_TRUE(parent.ok()) << parent.error_message();
  const auto child = editor.scene().create_entity({
      .name = "Hidden Child",
      .kind = quader::QuaderEntityKind::Empty,
      .parent = parent.value().id,
  });
  const auto sibling = editor.scene().create_entity({
      .name = "Sibling",
      .kind = quader::QuaderEntityKind::Empty,
  });
  ASSERT_TRUE(child.ok()) << child.error_message();
  ASSERT_TRUE(sibling.ok()) << sibling.error_message();

  quader::UISettings ui_settings = quader::UISettings::defaults();
  quader::ViewportSettings viewport_settings =
      quader::ViewportSettings::defaults();
  quader::LocalizationService localization = loaded_localization();
  quader::UIContext context{
      .intent = {.editor = editor},
      .scene = {.document = session.scene(),
                .assets = session.assets(),
                .modeling = session.modeling(),
                .selection = session.selection()},
      .preferences = {.ui = ui_settings, .viewport = viewport_settings},
      .localization = localization,
  };

  quader::OutlinerPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.resize(420, 420);
  panel.show();
  panel.refresh(context);
  QCoreApplication::processEvents();
  auto *tree = panel.findChild<QTreeView *>(QStringLiteral("outliner.tree"));
  ASSERT_NE(tree, nullptr);

  const QModelIndex parent_index =
      find_entity_index(*tree->model(), parent.value().id);
  const QModelIndex child_index =
      find_entity_index(*tree->model(), child.value().id);
  const QModelIndex sibling_index =
      find_entity_index(*tree->model(), sibling.value().id);
  ASSERT_TRUE(parent_index.isValid());
  ASSERT_TRUE(child_index.isValid());
  ASSERT_TRUE(sibling_index.isValid());
  tree->collapse(parent_index);
  QCoreApplication::processEvents();
  EXPECT_TRUE(tree->isRowHidden(child_index.row(), child_index.parent()) ||
              tree->visualRect(child_index).isEmpty());

  click(*tree->viewport(), tree->visualRect(parent_index).center(),
        Qt::LeftButton);
  send_key(*tree, Qt::Key_Down, Qt::ShiftModifier);
  {
    SCOPED_TRACE("collapsed-tree Shift+Down");
    expect_selection(*tree, session.scene(),
                     {parent.value().id, sibling.value().id},
                     sibling.value().id);
  }
  const std::vector<quader::EntityId> selected =
      session.scene().selected_entities();
  EXPECT_EQ(std::find(selected.begin(), selected.end(), child.value().id),
            selected.end());
}
