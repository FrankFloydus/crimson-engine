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
  EXPECT_EQ(tree->editTriggers(), QAbstractItemView::DoubleClicked |
                                      QAbstractItemView::EditKeyPressed);
  ASSERT_NE(tree->model(), nullptr);
  ASSERT_EQ(tree->model()->columnCount(), 4);
  EXPECT_TRUE(create->text().isEmpty());
  EXPECT_EQ(create->toolButtonStyle(), Qt::ToolButtonIconOnly);
  EXPECT_FALSE(create_from_selection->isEnabled());
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

  const QModelIndex first_eye = tree->model()->index(0, 2);
  const QPoint first_eye_position = tree->visualRect(first_eye).center();
  send_mouse(*tree->viewport(), QEvent::MouseButtonPress,
             first_eye_position, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*tree->viewport(), QEvent::MouseButtonRelease,
             first_eye_position, Qt::LeftButton, Qt::NoButton);
  ASSERT_TRUE(editor.layers().layer(first_layer).has_value());
  EXPECT_FALSE(editor.layers().layer(first_layer)->visible);
  EXPECT_TRUE(tree->selectionModel()->selectedRows(1).isEmpty());
  EXPECT_FALSE(tree->currentIndex().isValid());
  EXPECT_EQ(tree->model()
                ->index(0, 2)
                .data(Qt::AccessibleTextRole)
                .toString(),
            QStringLiteral("Layer hidden"));

  auto *visibility_button = qobject_cast<QToolButton *>(
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
  EXPECT_TRUE(tree->selectionModel()->selectedRows(1).isEmpty());
  EXPECT_FALSE(tree->currentIndex().isValid());

  const QModelIndex first_lock = tree->model()->index(0, 3);
  const QPoint first_lock_position = tree->visualRect(first_lock).center();
  send_mouse(*tree->viewport(), QEvent::MouseButtonPress,
             first_lock_position, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*tree->viewport(), QEvent::MouseButtonRelease,
             first_lock_position, Qt::LeftButton, Qt::NoButton);
  ASSERT_TRUE(editor.layers().layer(first_layer).has_value());
  EXPECT_TRUE(editor.layers().layer(first_layer)->locked);
  EXPECT_TRUE(tree->selectionModel()->selectedRows(1).isEmpty());
  EXPECT_FALSE(tree->currentIndex().isValid());
  EXPECT_EQ(tree->model()
                ->index(0, 3)
                .data(Qt::AccessibleTextRole)
                .toString(),
            QStringLiteral("Layer locked"));

  auto *lock_button = qobject_cast<QToolButton *>(
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
  EXPECT_TRUE(tree->selectionModel()->selectedRows(1).isEmpty());
  EXPECT_FALSE(tree->currentIndex().isValid());

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
  EXPECT_TRUE(tree->selectionModel()->selectedRows(1).isEmpty());
  EXPECT_FALSE(tree->currentIndex().isValid());

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
    auto *delete_action = menu->findChild<QAction *>(
        QStringLiteral("layers.action.delete"));
    ASSERT_NE(delete_action, nullptr);
    EXPECT_EQ(delete_action->text(), QStringLiteral("Delete Layers"));
    EXPECT_EQ(tree->selectionModel()->selectedRows(1).size(), 2);
    menu->close();
  });
  QContextMenuEvent context_event(
      QContextMenuEvent::Mouse, first_name_position,
      tree->viewport()->mapToGlobal(first_name_position));
  QCoreApplication::sendEvent(tree->viewport(), &context_event);
  EXPECT_TRUE(menu_opened);
  EXPECT_EQ(editor.layers().all().size(), 2u);

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
  const QPoint empty_position(4, tree->viewport()->height() - 4);
  ASSERT_FALSE(tree->indexAt(empty_position).isValid());
  send_mouse(*tree->viewport(), QEvent::MouseButtonPress, empty_position,
             Qt::LeftButton, Qt::LeftButton);
  send_mouse(*tree->viewport(), QEvent::MouseButtonRelease, empty_position,
             Qt::LeftButton, Qt::NoButton);
  EXPECT_TRUE(tree->selectionModel()->selectedRows(1).isEmpty());
  EXPECT_FALSE(tree->currentIndex().isValid());
}
