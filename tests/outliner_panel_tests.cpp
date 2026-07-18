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
#include "ui/outliner_panel.hpp"
#include "ui/ui_context.hpp"
#include "ui/ui_settings.hpp"
#include "viewport/viewport_settings.hpp"

#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QApplication>
#include <QCoreApplication>
#include <QContextMenuEvent>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLayout>
#include <QLineEdit>
#include <QMouseEvent>
#include <QModelIndex>
#include <QMenu>
#include <QPersistentModelIndex>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QTimer>
#include <QTreeView>

#include <algorithm>
#include <array>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#ifndef QUADER_SOURCE_DIR
#define QUADER_SOURCE_DIR "."
#endif

using quader::OutlinerPropertyState;

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

QPoint check_indicator_center(QTreeView &tree, const QModelIndex &index) {
  QStyleOptionViewItem option;
  option.initFrom(&tree);
  option.rect = tree.visualRect(index);
  option.widget = &tree;
  option.features |= QStyleOptionViewItem::HasCheckIndicator;
  option.checkState = static_cast<Qt::CheckState>(
      index.data(Qt::CheckStateRole).toInt());
  return tree.style()
      ->subElementRect(QStyle::SE_ItemViewItemCheckIndicator, &option,
                       tree.viewport())
      .center();
}

} // namespace

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
  EXPECT_FALSE(tree->dragEnabled())
      << "item dragging must stay disabled so mouse motion selects rows";

  EXPECT_EQ(tree->editTriggers(), QAbstractItemView::EditKeyPressed)
      << "double-click remains available for tree expansion; F2 and the "
         "context menu own rename";
}

TEST(OutlinerPanelTests, PropertyStatesDistinguishLocalAndInheritedValues) {
  EXPECT_EQ(quader::outliner_visibility_property_state(false, false),
            OutlinerPropertyState::Off);
  EXPECT_EQ(quader::outliner_visibility_property_state(true, true),
            OutlinerPropertyState::On);
  EXPECT_EQ(quader::outliner_visibility_property_state(true, false),
            OutlinerPropertyState::Inherited);

  EXPECT_EQ(quader::outliner_lock_property_state(false, false),
            OutlinerPropertyState::Off);
  EXPECT_EQ(quader::outliner_lock_property_state(true, true),
            OutlinerPropertyState::On);
  EXPECT_EQ(quader::outliner_lock_property_state(false, true),
            OutlinerPropertyState::Inherited);
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
     QtSelectionModelDrivesClickShiftControlDragKeyboardAndBlankSelection) {
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
  send_mouse(viewport, QEvent::MouseMove, second_position, Qt::NoButton,
             Qt::LeftButton);
  panel.refresh(context);
  EXPECT_EQ(tree->model(), stable_model);
  EXPECT_TRUE(persistent_first.isValid());
  send_mouse(viewport, QEvent::MouseMove, third_position, Qt::NoButton,
             Qt::LeftButton);
  panel.refresh(context);
  EXPECT_EQ(tree->model(), stable_model);
  EXPECT_TRUE(persistent_first.isValid());
  send_mouse(viewport, QEvent::MouseButtonRelease, third_position,
             Qt::LeftButton, Qt::NoButton);
  {
    SCOPED_TRACE("drag selection");
    expect_selection(*tree, session.scene(),
                     {first.value().id, second.value().id, third.value().id},
                     third.value().id);
  }
  ASSERT_TRUE(
      editor.commands().execute(quader::quader_command::kEditUndo).ok());
  panel.refresh(context);
  QCoreApplication::processEvents();
  {
    SCOPED_TRACE("single Undo reverts the complete drag gesture");
    expect_selection(*tree, session.scene(), {}, quader::kInvalidEntityId);
  }
  ASSERT_TRUE(
      editor.commands().execute(quader::quader_command::kEditRedo).ok());
  panel.refresh(context);
  QCoreApplication::processEvents();
  {
    SCOPED_TRACE("Redo reapplies the complete drag gesture");
    expect_selection(*tree, session.scene(),
                     {first.value().id, second.value().id, third.value().id},
                     third.value().id);
  }

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

  QModelIndex first_visible =
      find_entity_index(*tree->model(), first.value().id).siblingAtColumn(1);
  ASSERT_TRUE(first_visible.isValid());
  click(viewport, check_indicator_center(*tree, first_visible),
        Qt::LeftButton);
  ASSERT_TRUE(editor.query().entity(first.value().id).has_value());
  EXPECT_FALSE(editor.query().entity(first.value().id)->visible)
      << "the stock item delegate must route checkbox changes to the owner";
  panel.refresh(context);
  QCoreApplication::processEvents();
  first_visible =
      find_entity_index(*tree->model(), first.value().id).siblingAtColumn(1);
  ASSERT_TRUE(first_visible.isValid());
  {
    SCOPED_TRACE("checkbox click preserves stock delegate selection semantics");
    expect_selection(*tree, session.scene(), {second.value().id},
                     second.value().id);
    EXPECT_FALSE(tree->selectionModel()->isSelected(first_visible))
        << "Qt checkbox delegates toggle the property without selecting its row";
  }
  EXPECT_EQ(first_visible.data(Qt::CheckStateRole).toInt(), Qt::Unchecked);

  QModelIndex first_locked =
      find_entity_index(*tree->model(), first.value().id).siblingAtColumn(2);
  ASSERT_TRUE(first_locked.isValid());
  click(viewport, check_indicator_center(*tree, first_locked), Qt::LeftButton);
  ASSERT_TRUE(editor.query().entity(first.value().id).has_value());
  EXPECT_TRUE(editor.query().entity(first.value().id)->locked);
  panel.refresh(context);
  QCoreApplication::processEvents();
  first_locked =
      find_entity_index(*tree->model(), first.value().id).siblingAtColumn(2);
  ASSERT_TRUE(first_locked.isValid());
  EXPECT_EQ(first_locked.data(Qt::CheckStateRole).toInt(), Qt::Checked);

  tree->selectionModel()->setCurrentIndex(first_locked,
                                           QItemSelectionModel::NoUpdate);
  send_key(*tree, Qt::Key_Space);
  ASSERT_TRUE(editor.query().entity(first.value().id).has_value());
  EXPECT_FALSE(editor.query().entity(first.value().id)->locked)
      << "the stock delegate's Space gesture must toggle checkable items";

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
