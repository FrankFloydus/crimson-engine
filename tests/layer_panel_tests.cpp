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
#include "ui/layer_panel.hpp"
#include "ui/qt_color_swatch_button.hpp"
#include "ui/qt_ui_scale_metrics.hpp"
#include "ui/ui_context.hpp"
#include "ui/ui_settings.hpp"
#include "viewport/viewport_settings.hpp"

#include <QAction>
#include <QAbstractItemView>
#include <QApplication>
#include <QColorDialog>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLayout>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QWidget>

#include <filesystem>
#include <functional>
#include <initializer_list>
#include <string>

#ifndef QUADER_SOURCE_DIR
#define QUADER_SOURCE_DIR "."
#endif

namespace {

quader::LocalizationService loaded_localization() {
  quader::LocalizationService localization;
  EXPECT_TRUE(localization.load(
      std::filesystem::path(QUADER_SOURCE_DIR) / "resources" / "locales",
      "en-US"));
  return localization;
}

void send_key(QWidget &target, int key) {
  QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
  QCoreApplication::sendEvent(&target, &press);
  QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
  QCoreApplication::sendEvent(&target, &release);
  QCoreApplication::processEvents();
}

void send_mouse(QWidget &target, QEvent::Type type, QPoint position,
                Qt::MouseButton button, Qt::MouseButtons buttons) {
  QMouseEvent event(type, QPointF(position),
                    QPointF(target.mapToGlobal(position)), button, buttons,
                    Qt::NoModifier);
  QCoreApplication::sendEvent(&target, &event);
  QCoreApplication::processEvents();
}

bool use_color_dialog(QWidget &owner, QPushButton &swatch, QColor color,
                      bool accept,
                      std::function<void()> inspect_preview = {}) {
  bool opened = false;
  QTimer::singleShot(0, &owner, [&]() {
    auto *dialog = owner.findChild<QColorDialog *>(
        QStringLiteral("layers.color.dialog"));
    if (dialog == nullptr) {
      ADD_FAILURE() << "Expected the layer color dialog to open.";
      if (QWidget *modal = QApplication::activeModalWidget()) {
        modal->close();
      }
      return;
    }
    opened = true;
    dialog->setCurrentColor(color);
    if (inspect_preview) {
      inspect_preview();
    }
    accept ? dialog->accept() : dialog->reject();
  });
  swatch.click();
  return opened;
}

} // namespace

TEST(LayerPanelTests, UsesCompactTopActionsAndEditableMultiSelectRows) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-layer-panel-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));

  quader::LayerPanel panel;
  panel.resize(320, 480);
  ASSERT_NE(panel.layout(), nullptr);
  ASSERT_TRUE(panel.layout()->activate());

  auto *actions =
      panel.findChild<QWidget *>(QStringLiteral("layers.actions"));
  auto *tree = panel.findChild<QTreeView *>(QStringLiteral("layers.tree"));
  auto *create =
      panel.findChild<QToolButton *>(QStringLiteral("layers.create"));
  auto *create_from_selection = panel.findChild<QToolButton *>(
      QStringLiteral("layers.create_from_selection"));
  ASSERT_NE(actions, nullptr);
  ASSERT_NE(tree, nullptr);
  ASSERT_NE(create, nullptr);
  ASSERT_NE(create_from_selection, nullptr);

  EXPECT_LT(actions->geometry().top(), tree->geometry().top());
  EXPECT_EQ(tree->selectionMode(), QAbstractItemView::ExtendedSelection);
  EXPECT_EQ(tree->selectionBehavior(), QAbstractItemView::SelectRows);
  QTreeView stock_tree;
  EXPECT_EQ(tree->editTriggers(), stock_tree.editTriggers());
  EXPECT_EQ(panel.focusProxy(), tree);
  ASSERT_NE(tree->model(), nullptr);
  ASSERT_EQ(tree->model()->columnCount(), 4);
  EXPECT_TRUE(create->text().isEmpty());
  EXPECT_EQ(create->toolButtonStyle(), Qt::ToolButtonIconOnly);
  EXPECT_FALSE(create_from_selection->isEnabled());
}

TEST(LayerPanelTests, AppliesInterfaceScaleFromDesignBaselines) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-layer-panel-scale-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));

  quader::LayerPanel panel;
  auto *tree = panel.findChild<QTreeView *>(QStringLiteral("layers.tree"));
  auto *create =
      panel.findChild<QToolButton *>(QStringLiteral("layers.create"));
  auto *actions =
      panel.findChild<QWidget *>(QStringLiteral("layers.actions"));
  ASSERT_NE(tree, nullptr);
  ASSERT_NE(create, nullptr);
  ASSERT_NE(actions, nullptr);
  ASSERT_NE(actions->layout(), nullptr);

  panel.apply_ui_scale(quader::QtUiScaleMetrics(2.0F, 1U));
  EXPECT_EQ(tree->iconSize(), QSize(36, 36));
  EXPECT_EQ(create->size(), QSize(56, 56));
  EXPECT_EQ(create->iconSize(), QSize(40, 40));
  EXPECT_EQ(actions->layout()->contentsMargins(), QMargins(8, 8, 8, 8));
  EXPECT_EQ(actions->layout()->spacing(), 8);

  panel.apply_ui_scale(quader::QtUiScaleMetrics(1.0F, 2U));
  EXPECT_EQ(tree->iconSize(), QSize(18, 18));
  EXPECT_EQ(create->size(), QSize(28, 28));
  EXPECT_EQ(create->iconSize(), QSize(20, 20));
  EXPECT_EQ(actions->layout()->contentsMargins(), QMargins(4, 4, 4, 4));
  EXPECT_EQ(actions->layout()->spacing(), 4);
}

TEST(LayerPanelTests, RefreshesPaletteTintedRowIconsOnApplicationChange) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-layer-panel-palette-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));

  constexpr QColor kInitialTextColor(211, 37, 53);
  constexpr QColor kUpdatedTextColor(29, 197, 83);
  QPalette palette = QApplication::palette();
  palette.setColor(QPalette::Text, kInitialTextColor);
  QApplication::setPalette(palette);

  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto created = editor.layers().create();
  ASSERT_TRUE(created.ok()) << created.error_message();
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

  quader::LayerPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.show();
  panel.refresh(context);
  QCoreApplication::processEvents();

  const QString id = QString::number(
      static_cast<qulonglong>(created.value().id));
  auto *visibility = panel.findChild<QToolButton *>(
      QStringLiteral("layers.visibility.%1").arg(id));
  auto *lock = panel.findChild<QToolButton *>(
      QStringLiteral("layers.lock.%1").arg(id));
  ASSERT_NE(visibility, nullptr);
  ASSERT_NE(lock, nullptr);
  const qint64 initial_visibility_key = visibility->icon().cacheKey();
  const qint64 initial_lock_key = lock->icon().cacheKey();

  palette.setColor(QPalette::Text, kUpdatedTextColor);
  QApplication::setPalette(palette);
  QCoreApplication::processEvents();

  EXPECT_NE(visibility->icon().cacheKey(), initial_visibility_key);
  EXPECT_NE(lock->icon().cacheKey(), initial_lock_key);
}

TEST(LayerPanelTests,
     RoutesCreateRenameColorVisibilityLockAndRightClickThroughOwners) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-layer-panel-interaction-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));
  application.setQuitOnLastWindowClosed(false);

  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto member = editor.scene().create_entity(
      quader::QuaderCreateEntityArgs{
          .name = "Layer Panel Member",
          .kind = quader::QuaderEntityKind::Mesh,
      });
  ASSERT_TRUE(member.ok()) << member.error_message();
  ASSERT_TRUE(editor.scene().select(member.value().id).ok());

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

  quader::LayerPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.resize(360, 480);
  panel.show();
  panel.refresh(context);
  QCoreApplication::processEvents();

  auto *tree =
      panel.findChild<QTreeView *>(QStringLiteral("layers.tree"));
  auto *create =
      panel.findChild<QToolButton *>(QStringLiteral("layers.create"));
  auto *create_from_selection = panel.findChild<QToolButton *>(
      QStringLiteral("layers.create_from_selection"));
  ASSERT_NE(tree, nullptr);
  ASSERT_NE(create, nullptr);
  ASSERT_NE(create_from_selection, nullptr);
  EXPECT_FALSE(tree->accessibleName().isEmpty());
  EXPECT_FALSE(create->accessibleName().isEmpty());
  EXPECT_FALSE(create_from_selection->accessibleName().isEmpty());
  EXPECT_TRUE(create_from_selection->isEnabled());
  EXPECT_EQ(tree->model()->headerData(0, Qt::Horizontal).toString(),
            QStringLiteral("Color"));
  EXPECT_EQ(tree->model()->headerData(1, Qt::Horizontal).toString(),
            QStringLiteral("Name"));
  EXPECT_EQ(tree->model()->headerData(2, Qt::Horizontal).toString(),
            QStringLiteral("Visible"));
  EXPECT_EQ(tree->model()->headerData(3, Qt::Horizontal).toString(),
            QStringLiteral("Locked"));

  create_from_selection->click();
  QCoreApplication::processEvents();
  ASSERT_EQ(editor.layers().all().size(), 1u);
  const quader::QuaderLayerId first_layer = editor.layers().all().front().id;
  EXPECT_EQ(editor.query().entity(member.value().id)->layer_id, first_layer);
  EXPECT_EQ(tree->model()->rowCount(), 1);
  EXPECT_FALSE(tree->findChild<QLineEdit *>())
      << "new layers must not enter rename automatically";

  create->click();
  QCoreApplication::processEvents();
  const std::vector<quader::QuaderLayerSnapshot> created_layers =
      editor.layers().all();
  ASSERT_EQ(created_layers.size(), 2u);
  const quader::QuaderLayerId second_layer = created_layers[1].id;
  ASSERT_EQ(tree->model()->rowCount(), 2);

  QModelIndex second_name = tree->model()->index(1, 1);
  tree->selectionModel()->setCurrentIndex(
      second_name, QItemSelectionModel::ClearAndSelect |
                       QItemSelectionModel::Rows);
  tree->setFocus(Qt::OtherFocusReason);
  send_key(*tree, Qt::Key_F2);
  auto *name_editor = tree->findChild<QLineEdit *>();
  ASSERT_NE(name_editor, nullptr);
  name_editor->setText(QStringLiteral("Renamed With F2"));
  send_key(*name_editor, Qt::Key_Return);
  ASSERT_TRUE(editor.layers().layer(second_layer).has_value());
  EXPECT_EQ(editor.layers().layer(second_layer)->name, "Renamed With F2");

  second_name = tree->model()->index(1, 1);
  const QPoint second_name_position = tree->visualRect(second_name).center();
  send_mouse(*tree->viewport(), QEvent::MouseButtonPress,
             second_name_position, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*tree->viewport(), QEvent::MouseButtonRelease,
             second_name_position, Qt::LeftButton, Qt::NoButton);
  send_mouse(*tree->viewport(), QEvent::MouseButtonDblClick,
             second_name_position, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*tree->viewport(), QEvent::MouseButtonRelease,
             second_name_position, Qt::LeftButton, Qt::NoButton);
  name_editor = tree->findChild<QLineEdit *>();
  ASSERT_NE(name_editor, nullptr);
  name_editor->setText(QStringLiteral("Renamed With Double Click"));
  send_key(*name_editor, Qt::Key_Return);
  EXPECT_EQ(editor.layers().layer(second_layer)->name,
            "Renamed With Double Click");

  auto *visibility_button = qobject_cast<QToolButton *>(
      tree->indexWidget(tree->model()->index(0, 2)));
  ASSERT_NE(visibility_button, nullptr);
  panel.apply_ui_scale(quader::QtUiScaleMetrics(2.0F, 1U));
  EXPECT_EQ(visibility_button->size(), QSize(44, 44));
  EXPECT_EQ(visibility_button->iconSize(), QSize(36, 36));
  panel.apply_ui_scale(quader::QtUiScaleMetrics(1.0F, 2U));
  EXPECT_EQ(visibility_button->size(), QSize(22, 22));
  EXPECT_EQ(visibility_button->iconSize(), QSize(18, 18));
  QPointer<QToolButton> replaced_visibility_button(visibility_button);
  visibility_button->click();
  QCoreApplication::processEvents();
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  EXPECT_TRUE(replaced_visibility_button.isNull())
      << "rebuilt index widgets must not outlive their model row";
  ASSERT_TRUE(editor.layers().layer(first_layer).has_value());
  EXPECT_FALSE(editor.layers().layer(first_layer)->visible);
  ASSERT_EQ(tree->selectionModel()->selectedRows(1).size(), 1);
  EXPECT_EQ(tree->selectionModel()->selectedRows(1).front().row(), 1);
  EXPECT_EQ(tree->currentIndex().row(), 1)
      << "an index widget action must preserve the stock row selection";
  EXPECT_EQ(tree->model()
                ->index(0, 2)
                .data(Qt::AccessibleTextRole)
                .toString(),
            QStringLiteral("Layer hidden"));

  visibility_button = qobject_cast<QToolButton *>(
      tree->indexWidget(tree->model()->index(0, 2)));
  ASSERT_NE(visibility_button, nullptr);
  EXPECT_EQ(visibility_button->accessibleName(),
            QStringLiteral("Layer hidden"));
  tree->selectionModel()->select(
      tree->model()->index(0, 1),
      QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
  tree->selectionModel()->setCurrentIndex(tree->model()->index(0, 1),
                                          QItemSelectionModel::NoUpdate);
  visibility_button->setFocus(Qt::OtherFocusReason);
  send_key(*visibility_button, Qt::Key_Space);
  ASSERT_TRUE(editor.layers().layer(first_layer).has_value());
  EXPECT_TRUE(editor.layers().layer(first_layer)->visible);
  ASSERT_EQ(tree->selectionModel()->selectedRows(1).size(), 1);
  EXPECT_EQ(tree->selectionModel()->selectedRows(1).front().row(), 0);
  EXPECT_EQ(tree->currentIndex().row(), 0);

  auto *lock_button = qobject_cast<QToolButton *>(
      tree->indexWidget(tree->model()->index(0, 3)));
  ASSERT_NE(lock_button, nullptr);
  lock_button->click();
  QCoreApplication::processEvents();
  ASSERT_TRUE(editor.layers().layer(first_layer).has_value());
  EXPECT_TRUE(editor.layers().layer(first_layer)->locked);
  ASSERT_EQ(tree->selectionModel()->selectedRows(1).size(), 1);
  EXPECT_EQ(tree->selectionModel()->selectedRows(1).front().row(), 0);
  EXPECT_EQ(tree->currentIndex().row(), 0);
  EXPECT_EQ(tree->model()
                ->index(0, 3)
                .data(Qt::AccessibleTextRole)
                .toString(),
            QStringLiteral("Layer locked"));

  lock_button = qobject_cast<QToolButton *>(
      tree->indexWidget(tree->model()->index(0, 3)));
  ASSERT_NE(lock_button, nullptr);
  EXPECT_EQ(lock_button->accessibleName(), QStringLiteral("Layer locked"));
  tree->selectionModel()->select(
      tree->model()->index(0, 1),
      QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
  tree->selectionModel()->setCurrentIndex(tree->model()->index(0, 1),
                                          QItemSelectionModel::NoUpdate);
  lock_button->setFocus(Qt::OtherFocusReason);
  send_key(*lock_button, Qt::Key_Space);
  ASSERT_TRUE(editor.layers().layer(first_layer).has_value());
  EXPECT_FALSE(editor.layers().layer(first_layer)->locked);
  ASSERT_EQ(tree->selectionModel()->selectedRows(1).size(), 1);
  EXPECT_EQ(tree->selectionModel()->selectedRows(1).front().row(), 0);
  EXPECT_EQ(tree->currentIndex().row(), 0);

  auto *swatch = dynamic_cast<quader::QtColorSwatchButton *>(
      tree->indexWidget(tree->model()->index(0, 0)));
  ASSERT_NE(swatch, nullptr);
  tree->selectionModel()->select(
      tree->model()->index(0, 1),
      QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
  tree->selectionModel()->setCurrentIndex(tree->model()->index(0, 1),
                                          QItemSelectionModel::NoUpdate);
  const QColor initial_swatch = swatch->swatch_color();
  const std::uint64_t before_cancel = editor.layers().revision();
  QPointer<quader::QtColorSwatchButton> guarded_swatch(swatch);
  ASSERT_TRUE(use_color_dialog(
      panel, *swatch, QColor::fromRgb(220, 40, 30), false, [&]() {
        ASSERT_NE(guarded_swatch, nullptr);
        EXPECT_EQ(guarded_swatch->swatch_color(),
                  QColor::fromRgb(220, 40, 30));
        EXPECT_EQ(editor.layers().revision(), before_cancel);
      }));
  ASSERT_NE(guarded_swatch, nullptr);
  EXPECT_EQ(guarded_swatch->swatch_color(), initial_swatch);
  EXPECT_EQ(editor.layers().revision(), before_cancel);
  ASSERT_EQ(tree->selectionModel()->selectedRows(1).size(), 1);
  EXPECT_EQ(tree->selectionModel()->selectedRows(1).front().row(), 0);
  EXPECT_EQ(tree->currentIndex().row(), 0);

  const QColor committed_color = QColor::fromRgb(24, 96, 208);
  const std::uint64_t before_accept = editor.layers().revision();
  ASSERT_TRUE(
      use_color_dialog(panel, *swatch, committed_color, true));
  ASSERT_TRUE(editor.layers().layer(first_layer).has_value());
  EXPECT_EQ(editor.layers().revision(), before_accept + 1u);
  EXPECT_NEAR(editor.layers().layer(first_layer)->wireframe_color.x,
              committed_color.redF(), 0.0001f);
  EXPECT_NEAR(editor.layers().layer(first_layer)->wireframe_color.y,
              committed_color.greenF(), 0.0001f);
  EXPECT_NEAR(editor.layers().layer(first_layer)->wireframe_color.z,
              committed_color.blueF(), 0.0001f);

  QItemSelection both_rows;
  both_rows.select(tree->model()->index(0, 1),
                   tree->model()->index(0, 1));
  both_rows.select(tree->model()->index(1, 1),
                   tree->model()->index(1, 1));
  tree->selectionModel()->clearSelection();
  tree->selectionModel()->select(
      both_rows, QItemSelectionModel::Select | QItemSelectionModel::Rows);
  tree->selectionModel()->setCurrentIndex(tree->model()->index(0, 1),
                                          QItemSelectionModel::NoUpdate);
  const QPoint first_name_position =
      tree->visualRect(tree->model()->index(0, 1)).center();
  send_mouse(*tree->viewport(), QEvent::MouseButtonPress,
             first_name_position, Qt::RightButton, Qt::RightButton);
  EXPECT_EQ(tree->selectionModel()->selectedRows(1).size(), 2);

  bool menu_opened = false;
  QTimer::singleShot(0, &panel, [&]() {
    auto *menu = panel.findChild<QMenu *>(
        QStringLiteral("layers.context_menu"));
    if (menu == nullptr) {
      ADD_FAILURE() << "Expected the layer context menu to open.";
      if (QWidget *popup = QApplication::activePopupWidget()) {
        popup->close();
      }
      return;
    }
    menu_opened = true;
    const QList<QAction *> actions = menu->actions();
    if (actions.size() != 11) {
      ADD_FAILURE() << "Unexpected layer context-menu action count: "
                    << actions.size();
      menu->close();
      return;
    }
    EXPECT_TRUE(actions[2]->isSeparator());
    EXPECT_TRUE(actions[5]->isSeparator());
    EXPECT_TRUE(actions[8]->isSeparator());
    EXPECT_EQ(actions.back()->objectName(),
              QStringLiteral("layers.action.delete"));

    auto *rename_action = menu->findChild<QAction *>(
        QStringLiteral("layers.action.rename"));
    auto *color_action = menu->findChild<QAction *>(
        QStringLiteral("layers.action.choose_color"));
    auto *visibility_action = menu->findChild<QAction *>(
        QStringLiteral("layers.action.hide"));
    auto *lock_action = menu->findChild<QAction *>(
        QStringLiteral("layers.action.lock"));
    auto *add_action = menu->findChild<QAction *>(
        QStringLiteral("layers.action.add_selection"));
    auto *remove_action = menu->findChild<QAction *>(
        QStringLiteral("layers.action.remove_selection"));
    auto *empty_action = menu->findChild<QAction *>(
        QStringLiteral("layers.action.empty"));
    auto *delete_action = menu->findChild<QAction *>(
        QStringLiteral("layers.action.delete"));
    EXPECT_NE(rename_action, nullptr);
    EXPECT_NE(color_action, nullptr);
    EXPECT_NE(visibility_action, nullptr);
    EXPECT_NE(lock_action, nullptr);
    EXPECT_NE(add_action, nullptr);
    EXPECT_NE(remove_action, nullptr);
    EXPECT_NE(empty_action, nullptr);
    EXPECT_NE(delete_action, nullptr);
    if (rename_action == nullptr || color_action == nullptr ||
        visibility_action == nullptr || lock_action == nullptr ||
        add_action == nullptr || remove_action == nullptr ||
        empty_action == nullptr || delete_action == nullptr) {
      menu->close();
      return;
    }
    EXPECT_EQ(rename_action->text(), QStringLiteral("Rename Layer"));
    EXPECT_EQ(color_action->text(), QStringLiteral("Choose Layer Color"));
    EXPECT_EQ(visibility_action->text(), QStringLiteral("Hide Layer"));
    EXPECT_EQ(lock_action->text(), QStringLiteral("Lock Layer"));
    EXPECT_EQ(add_action->text(),
              QStringLiteral("Add Objects to Layer"));
    EXPECT_EQ(remove_action->text(),
              QStringLiteral("Remove Objects from Layer"));
    EXPECT_EQ(add_action->isVisible(),
              editor.layers().can_add_selection_to(first_layer));
    EXPECT_EQ(remove_action->isVisible(),
              editor.layers().can_remove_selection(first_layer));
    EXPECT_EQ(empty_action->isVisible(),
              !editor.layers().layer(first_layer)->members.empty());
    EXPECT_EQ(empty_action->text(), QStringLiteral("Empty Layer"));
    EXPECT_EQ(delete_action->text(),
              QStringLiteral("Delete Layers"));
    EXPECT_EQ(tree->selectionModel()->selectedRows(1).size(), 2);
    menu->setActiveAction(visibility_action);
    QKeyEvent accept_action(QEvent::KeyPress, Qt::Key_Return,
                            Qt::NoModifier);
    QApplication::sendEvent(menu, &accept_action);
    if (menu->isVisible()) {
      menu->close();
    }
  });
  QContextMenuEvent context_event(
      QContextMenuEvent::Mouse, first_name_position,
      tree->viewport()->mapToGlobal(first_name_position));
  QCoreApplication::sendEvent(tree->viewport(), &context_event);
  EXPECT_TRUE(menu_opened);
  EXPECT_EQ(editor.layers().all().size(), 2u);
  ASSERT_TRUE(editor.layers().layer(first_layer).has_value());
  ASSERT_TRUE(editor.layers().layer(second_layer).has_value());
  EXPECT_FALSE(editor.layers().layer(first_layer)->visible)
      << "target-specific layer actions must affect only This Layer";
  EXPECT_TRUE(editor.layers().layer(second_layer)->visible);
  EXPECT_EQ(tree->selectionModel()->selectedRows(1).size(), 2);

  bool keyboard_menu_opened = false;
  QTimer::singleShot(0, &panel, [&]() {
    auto *menu = panel.findChild<QMenu *>(
        QStringLiteral("layers.context_menu"));
    if (menu != nullptr) {
      keyboard_menu_opened = true;
      menu->close();
    }
  });
  QContextMenuEvent keyboard_context_event(
      QContextMenuEvent::Keyboard, QPoint{}, QPoint{});
  QCoreApplication::sendEvent(tree->viewport(), &keyboard_context_event);
  EXPECT_TRUE(keyboard_menu_opened);
  EXPECT_EQ(tree->selectionModel()->selectedRows(1).size(), 2);

  tree->selectionModel()->select(
      tree->model()->index(0, 1),
      QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
  tree->selectionModel()->setCurrentIndex(tree->model()->index(0, 1),
                                          QItemSelectionModel::NoUpdate);
  const QPoint unselected_position =
      tree->visualRect(tree->model()->index(1, 1)).center();
  send_mouse(*tree->viewport(), QEvent::MouseButtonPress,
             unselected_position, Qt::RightButton, Qt::RightButton);
  const QModelIndexList selected_after_unselected_right_click =
      tree->selectionModel()->selectedRows(1);
  ASSERT_EQ(selected_after_unselected_right_click.size(), 1);
  EXPECT_EQ(selected_after_unselected_right_click.front().row(), 1);

  tree->selectionModel()->setCurrentIndex(
      tree->model()->index(1, 1),
      QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
  const QModelIndex current_before_blank = tree->currentIndex();
  const QPoint empty_position(4, tree->viewport()->height() - 4);
  ASSERT_FALSE(tree->indexAt(empty_position).isValid());
  send_mouse(*tree->viewport(), QEvent::MouseButtonPress, empty_position,
             Qt::LeftButton, Qt::LeftButton);
  send_mouse(*tree->viewport(), QEvent::MouseButtonRelease, empty_position,
             Qt::LeftButton, Qt::NoButton);
  EXPECT_TRUE(tree->selectionModel()->selectedRows(1).isEmpty());
  EXPECT_EQ(tree->currentIndex(), current_before_blank)
      << "Qt keeps currentIndex independent from the cleared selection";
}

TEST(LayerPanelTests,
     ContextMenuRejectsLayerSceneSelectionAndTargetChangesDuringNestedLoop) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-layer-panel-context-fence-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));
  application.setQuitOnLastWindowClosed(false);

  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto first_entity = editor.scene().create_entity(
      quader::QuaderCreateEntityArgs{
          .name = "First Layer Context Entity",
          .kind = quader::QuaderEntityKind::Mesh,
      });
  const auto second_entity = editor.scene().create_entity(
      quader::QuaderCreateEntityArgs{
          .name = "Second Layer Context Entity",
          .kind = quader::QuaderEntityKind::Mesh,
      });
  ASSERT_TRUE(first_entity.ok()) << first_entity.error_message();
  ASSERT_TRUE(second_entity.ok()) << second_entity.error_message();
  ASSERT_TRUE(editor.scene().select(first_entity.value().id).ok());

  const auto first_layer_result = editor.layers().create("First Layer");
  const auto second_layer_result = editor.layers().create("Second Layer");
  ASSERT_TRUE(first_layer_result.ok()) << first_layer_result.error_message();
  ASSERT_TRUE(second_layer_result.ok()) << second_layer_result.error_message();
  const quader::QuaderLayerId first_layer = first_layer_result.value().id;
  const quader::QuaderLayerId second_layer = second_layer_result.value().id;

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

  quader::LayerPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.resize(360, 480);
  panel.show();
  panel.refresh(context);
  QCoreApplication::processEvents();

  auto *tree =
      panel.findChild<QTreeView *>(QStringLiteral("layers.tree"));
  ASSERT_NE(tree, nullptr);
  ASSERT_EQ(tree->model()->rowCount(), 2);
  ASSERT_NE(tree->selectionModel(), nullptr);

  const auto select_rows = [&](std::initializer_list<int> rows,
                               int current_row) {
    QItemSelection selection;
    for (const int row : rows) {
      const QModelIndex index = tree->model()->index(row, 1);
      selection.select(index, index);
    }
    tree->selectionModel()->clearSelection();
    tree->selectionModel()->select(
        selection, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    tree->selectionModel()->setCurrentIndex(
        tree->model()->index(current_row, 1),
        QItemSelectionModel::NoUpdate);
  };

  const auto invoke_context_action =
      [&](int target_row, const QString &action_name,
          const std::function<void()> &mutate_while_open) {
        bool menu_opened = false;
        QTimer::singleShot(0, &panel, [&]() {
          auto *menu = panel.findChild<QMenu *>(
              QStringLiteral("layers.context_menu"));
          if (menu == nullptr) {
            ADD_FAILURE() << "Expected the layer context menu to open.";
            if (QWidget *popup = QApplication::activePopupWidget()) {
              popup->close();
            }
            return;
          }
          menu_opened = true;
          auto *action = menu->findChild<QAction *>(action_name);
          if (action == nullptr) {
            ADD_FAILURE() << "Expected layer context action "
                          << action_name.toStdString();
            menu->close();
            return;
          }
          mutate_while_open();
          menu->setActiveAction(action);
          QKeyEvent accept_action(QEvent::KeyPress, Qt::Key_Return,
                                  Qt::NoModifier);
          QApplication::sendEvent(menu, &accept_action);
          if (menu->isVisible()) {
            menu->close();
          }
        });

        const QModelIndex target = tree->model()->index(target_row, 1);
        const QPoint position = tree->visualRect(target).center();
        QContextMenuEvent context_event(
            QContextMenuEvent::Mouse, position,
            tree->viewport()->mapToGlobal(position));
        QCoreApplication::sendEvent(tree->viewport(), &context_event);
        EXPECT_TRUE(menu_opened);
        QCoreApplication::processEvents();
      };

  select_rows({0}, 0);
  invoke_context_action(
      0, QStringLiteral("layers.action.delete"), [&]() {
        const auto renamed =
            editor.layers().rename(second_layer, "Changed While Menu Open");
        EXPECT_TRUE(renamed.ok()) << renamed.error_message();
      });
  ASSERT_TRUE(editor.layers().layer(first_layer).has_value());
  ASSERT_TRUE(editor.layers().layer(second_layer).has_value());
  EXPECT_EQ(editor.layers().layer(second_layer)->name,
            "Changed While Menu Open");

  select_rows({0, 1}, 0);
  invoke_context_action(
      0, QStringLiteral("layers.action.delete"), [&]() {
        select_rows({1}, 1);
      });
  EXPECT_TRUE(editor.layers().layer(first_layer).has_value());
  EXPECT_TRUE(editor.layers().layer(second_layer).has_value());

  select_rows({0, 1}, 0);
  invoke_context_action(
      0, QStringLiteral("layers.action.hide"), [&]() {
        tree->selectionModel()->setCurrentIndex(
            tree->model()->index(1, 1), QItemSelectionModel::NoUpdate);
      });
  ASSERT_TRUE(editor.layers().layer(first_layer).has_value());
  EXPECT_TRUE(editor.layers().layer(first_layer)->visible);

  select_rows({0}, 0);
  invoke_context_action(
      0, QStringLiteral("layers.action.add_selection"), [&]() {
        const auto selected = editor.scene().select(second_entity.value().id);
        EXPECT_TRUE(selected.ok()) << selected.error_message();
      });
  ASSERT_TRUE(editor.layers().layer(first_layer).has_value());
  EXPECT_TRUE(editor.layers().layer(first_layer)->members.empty());
}

TEST(LayerPanelTests,
     EqualRevisionDocumentSwitchRebuildsRowsAndClearsDocumentLocalSelection) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-layer-panel-document-switch-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));

  quader::SceneSession first_session;
  quader::SceneSession second_session;
  quader::QuaderEditorApi first_editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(
          first_session));
  quader::QuaderEditorApi second_editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(
          second_session));
  const auto first_layer = first_editor.layers().create();
  const auto second_layer = second_editor.layers().create();
  ASSERT_TRUE(first_layer.ok()) << first_layer.error_message();
  ASSERT_TRUE(second_layer.ok()) << second_layer.error_message();
  ASSERT_TRUE(first_editor.layers()
                  .rename(first_layer.value().id, "First Document Layer")
                  .ok());
  ASSERT_TRUE(second_editor.layers()
                  .rename(second_layer.value().id, "Second Document Layer")
                  .ok());
  ASSERT_EQ(first_editor.layers().revision(),
            second_editor.layers().revision());
  ASSERT_EQ(first_session.scene().selection_revision(),
            second_session.scene().selection_revision());

  quader::UISettings ui_settings = quader::UISettings::defaults();
  quader::ViewportSettings viewport_settings =
      quader::ViewportSettings::defaults();
  quader::LocalizationService localization = loaded_localization();
  quader::UIContext first_context{
      .intent = {.editor = first_editor},
      .scene = {.document = first_session.scene(),
                .assets = first_session.assets(),
                .modeling = first_session.modeling(),
                .selection = first_session.selection()},
      .preferences = {.ui = ui_settings, .viewport = viewport_settings},
      .localization = localization,
  };
  quader::UIContext second_context{
      .intent = {.editor = second_editor},
      .scene = {.document = second_session.scene(),
                .assets = second_session.assets(),
                .modeling = second_session.modeling(),
                .selection = second_session.selection()},
      .preferences = {.ui = ui_settings, .viewport = viewport_settings},
      .localization = localization,
  };

  quader::LayerPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.resize(360, 480);
  panel.show();
  panel.refresh(first_context);
  QCoreApplication::processEvents();
  auto *tree = panel.findChild<QTreeView *>(QStringLiteral("layers.tree"));
  ASSERT_NE(tree, nullptr);
  ASSERT_EQ(tree->model()->rowCount(), 1);
  EXPECT_EQ(tree->model()->index(0, 1).data().toString(),
            QStringLiteral("First Document Layer"));
  tree->selectionModel()->select(
      tree->model()->index(0, 1),
      QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
  tree->selectionModel()->setCurrentIndex(tree->model()->index(0, 1),
                                           QItemSelectionModel::NoUpdate);

  panel.refresh(second_context);
  QCoreApplication::processEvents();
  ASSERT_EQ(tree->model()->rowCount(), 1);
  EXPECT_EQ(tree->model()->index(0, 1).data().toString(),
            QStringLiteral("Second Document Layer"));
  EXPECT_TRUE(tree->selectionModel()->selectedRows(1).isEmpty());
  EXPECT_FALSE(tree->currentIndex().isValid());
}
