////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/outliner_panel.hpp"

#include "api/quader_editor_api.hpp"
#include "core/localization_service.hpp"
#include "editor/commands/document/document_command_scene_helpers.hpp"
#include "scene/scene_document.hpp"
#include "ui/ui_context.hpp"
#include "ui/ui_panel.hpp"

#include <QAbstractItemView>
#include <QAction>
#include <QContextMenuEvent>
#include <QHeaderView>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLineEdit>
#include <QList>
#include <QMenu>
#include <QModelIndex>
#include <QMouseEvent>
#include <QPainter>
#include <QSignalBlocker>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QString>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QTreeView>
#include <QVBoxLayout>
#include <QVariant>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace quader {

OutlinerPropertyState
outliner_visibility_property_state(bool visible,
                                   bool effectively_visible) noexcept {
  if (!visible) {
    return OutlinerPropertyState::Off;
  }
  return effectively_visible ? OutlinerPropertyState::On
                             : OutlinerPropertyState::Inherited;
}

OutlinerPropertyState
outliner_lock_property_state(bool locked,
                             bool effectively_locked) noexcept {
  if (locked) {
    return OutlinerPropertyState::On;
  }
  return effectively_locked ? OutlinerPropertyState::Inherited
                            : OutlinerPropertyState::Off;
}

bool outliner_selection_sync_required(
    bool model_rebuilt, bool cached_revision_valid,
    std::uint64_t cached_selection_revision,
    std::uint64_t current_selection_revision) noexcept {
  return model_rebuilt || !cached_revision_valid ||
         cached_selection_revision != current_selection_revision;
}

namespace {

constexpr int kEntityIdRole = Qt::UserRole + 1;
constexpr int kSubduedRole = Qt::UserRole + 2;

/// Identifies the resizable scene-tree columns owned by the Qt outliner model.
enum class OutlinerColumn : int {
  Name = 0,
  Visible = 1,
  Locked = 2,
  Count = 3,
};

constexpr int column_index(OutlinerColumn column) {
  return static_cast<int>(column);
}

Qt::CheckState qt_check_state(OutlinerPropertyState state) {
  switch (state) {
  case OutlinerPropertyState::Off:
    return Qt::Unchecked;
  case OutlinerPropertyState::On:
    return Qt::Checked;
  case OutlinerPropertyState::Inherited:
    return Qt::PartiallyChecked;
  }
  return Qt::Unchecked;
}

QString qt_text(std::string_view text) {
  return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

EntityId entity_id(const QModelIndex &index) {
  if (!index.isValid()) {
    return kInvalidEntityId;
  }
  return static_cast<EntityId>(index.data(kEntityIdRole).toULongLong());
}

bool contains_entity(const std::vector<EntityId> &ids, EntityId id) {
  return std::find(ids.begin(), ids.end(), id) != ids.end();
}

std::vector<EntityId>
entity_range_selection(const std::vector<EntityRecord> &records,
                       EntityId anchor, EntityId clicked) {
  if (anchor == kInvalidEntityId || anchor == clicked) {
    return {clicked};
  }

  std::vector<EntityId> selection;
  bool selecting = false;
  for (const EntityRecord &record : records) {
    const bool endpoint = record.id == anchor || record.id == clicked;
    if (endpoint && !selecting) {
      selecting = true;
      selection.push_back(record.id);
      continue;
    }

    if (selecting) {
      selection.push_back(record.id);
    }

    if (endpoint) {
      return selection;
    }
  }

  return {clicked};
}

bool select_only(QuaderEditorApi &editor, const std::vector<EntityId> &entities,
                 EntityId active = kInvalidEntityId) {
  const QuaderApiVoidResult result =
      editor.selection().select_objects_for_inspection(
          std::span<const EntityId>(entities.data(), entities.size()), active);
  return result.ok && result.changed;
}

bool select_current_scene_selection(QuaderEditorApi &editor,
                                    const SceneDocument &document) {
  const std::vector<EntityId> selected = document.selected_entities();
  return select_only(editor, selected, document.primary_selection());
}

void group_selection(QuaderEditorApi &editor, const SceneDocument &document) {
  const std::vector<EntityId> roots =
      document_command_selected_root_entities(document);
  if (roots.empty()) {
    return;
  }

  const QuaderApiVoidResult result =
      editor.commands().execute(quader_command::kGroupSelection);
  if (result.ok && result.changed) {
    (void)select_current_scene_selection(editor, document);
  }
}

void ungroup_entity(QuaderEditorApi &editor, const SceneDocument &document,
                    EntityId group) {
  if (!document.exists(group) || document.kind(group) != EntityKind::Empty ||
      !document.has_children(group)) {
    return;
  }

  if (!contains_entity(document.selected_entities(), group)) {
    (void)select_only(editor, {group}, group);
  }
  const QuaderApiVoidResult result =
      editor.commands().execute(quader_command::kUngroupSelection);
  if (result.ok && result.changed) {
    (void)select_current_scene_selection(editor, document);
  }
}

void delete_selection(QuaderEditorApi &editor, const SceneDocument &document) {
  const std::vector<EntityId> roots =
      document_command_selected_root_entities(document);
  if (roots.empty()) {
    return;
  }

  const QuaderSelectionMode previous_mode = editor.selection().summary().mode;
  if (previous_mode != QuaderSelectionMode::Object &&
      previous_mode != QuaderSelectionMode::Group) {
    (void)editor.selection().set_mode(QuaderSelectionMode::Object);
  }
  (void)editor.commands().execute(quader_command::kDeleteSelection);
}

void duplicate_selection(QuaderEditorApi &editor) {
  (void)editor.commands().execute(quader_command::kDuplicateSelection);
}

/// Renders hidden and locked entity rows with subdued stock palette text.
class SubduedRowDelegate final : public QStyledItemDelegate {
public:
  explicit SubduedRowDelegate(QObject *parent = nullptr)
      : QStyledItemDelegate(parent) {}

  QWidget *createEditor(QWidget *parent,
                        const QStyleOptionViewItem &option,
                        const QModelIndex &index) const override {
    QWidget *editor = QStyledItemDelegate::createEditor(parent, option, index);
    if (index.column() == column_index(OutlinerColumn::Name)) {
      if (auto *line_edit = qobject_cast<QLineEdit *>(editor)) {
        line_edit->setMaxLength(127);
        line_edit->selectAll();
      }
    }
    return editor;
  }

  void paint(QPainter *painter, const QStyleOptionViewItem &option,
             const QModelIndex &index) const override {
    QStyleOptionViewItem styled_option(option);
    if (index.data(kSubduedRole).toBool()) {
      styled_option.state &= ~QStyle::State_Enabled;
    }
    QStyledItemDelegate::paint(painter, styled_option, index);
  }
};

/// Routes outliner row presses and context requests through semantic handlers.
class OutlinerTreeView final : public QTreeView {
public:
  using RowPressHandler =
      std::function<void(EntityId, Qt::MouseButton, Qt::KeyboardModifiers)>;
  using PropertyToggleHandler = std::function<void(EntityId, int)>;
  using ContextMenuHandler = std::function<void(EntityId, QPoint)>;

  explicit OutlinerTreeView(QWidget *parent = nullptr) : QTreeView(parent) {}

  RowPressHandler row_press_handler;
  PropertyToggleHandler property_toggle_handler;
  ContextMenuHandler context_menu_handler;

protected:
  void keyPressEvent(QKeyEvent *event) override {
    const QModelIndex index = currentIndex();
    if (event->key() == Qt::Key_Space && index.isValid() &&
        index.column() != column_index(OutlinerColumn::Name) &&
        property_toggle_handler != nullptr) {
      property_toggle_handler(entity_id(index), index.column());
      event->accept();
      return;
    }
    QTreeView::keyPressEvent(event);
  }

  void mousePressEvent(QMouseEvent *event) override {
    const QModelIndex index = indexAt(event->position().toPoint());
    const EntityId pressed_entity = entity_id(index);
    if (index.isValid() && event->button() == Qt::LeftButton &&
        index.column() != column_index(OutlinerColumn::Name) &&
        row_press_handler != nullptr && property_toggle_handler != nullptr) {
      row_press_handler(pressed_entity, event->button(), event->modifiers());
      property_toggle_handler(pressed_entity, index.column());
      event->accept();
      return;
    }

    const bool was_expanded = index.isValid() && isExpanded(index);
    QTreeView::mousePressEvent(event);
    const bool toggled_open =
        index.isValid() && was_expanded != isExpanded(index);

    if (!index.isValid() || row_press_handler == nullptr) {
      return;
    }
    if (event->button() == Qt::LeftButton && !toggled_open) {
      row_press_handler(pressed_entity, event->button(), event->modifiers());
    } else if (event->button() == Qt::RightButton) {
      row_press_handler(pressed_entity, event->button(), event->modifiers());
    }
  }

  void contextMenuEvent(QContextMenuEvent *event) override {
    const QModelIndex index = indexAt(event->pos());
    if (!index.isValid() || context_menu_handler == nullptr) {
      QTreeView::contextMenuEvent(event);
      return;
    }
    context_menu_handler(entity_id(index), event->globalPos());
    event->accept();
  }
};

} // namespace

/// Owns OutlinerPanel's retained tree model, cache and interaction wiring.
struct OutlinerPanel::Impl {
  /// Caches the scene hierarchy projection for revision-gated tree rebuilds.
  struct TreeCache {
    bool valid = false;
    std::uint64_t scene_revision = 0;
    std::vector<EntityRecord> records;
    std::unordered_map<EntityId, std::size_t> record_index_by_entity;
    std::unordered_map<EntityId, std::vector<EntityId>> children_by_entity;
    std::vector<EntityId> roots;
  };

  explicit Impl(OutlinerPanel &panel) : panel_(panel) {
    panel_.setObjectName(
        QString::fromLatin1(ui_panel_stable_id(UIPanelId::Outliner)));
    panel_.setWindowTitle(
        QString::fromLatin1(ui_panel_key(UIPanelId::Outliner)));

    auto *layout = new QVBoxLayout(&panel_);
    tree_ = new OutlinerTreeView(&panel_);
    tree_->setObjectName(QStringLiteral("outliner.tree"));
    tree_->setHeaderHidden(false);
    tree_->setAnimated(false);
    tree_->setItemsExpandable(true);
    tree_->setExpandsOnDoubleClick(true);
    tree_->setRootIsDecorated(true);
    tree_->setEditTriggers(QAbstractItemView::SelectedClicked |
                           QAbstractItemView::EditKeyPressed);
    tree_->setSelectionBehavior(QAbstractItemView::SelectRows);
    tree_->setSelectionMode(QAbstractItemView::NoSelection);
    tree_->setUniformRowHeights(true);
    tree_->setItemDelegate(new SubduedRowDelegate(tree_));

    model_ = new QStandardItemModel(tree_);
    model_->setObjectName(QStringLiteral("outliner.model"));
    model_->setColumnCount(column_index(OutlinerColumn::Count));
    tree_->setModel(model_);
    refresh_headers();

    QHeaderView *header = tree_->header();
    header->setSectionsMovable(false);
    header->setStretchLastSection(false);
    header->setSectionResizeMode(column_index(OutlinerColumn::Name),
                                 QHeaderView::Stretch);
    header->setSectionResizeMode(column_index(OutlinerColumn::Visible),
                                 QHeaderView::ResizeToContents);
    header->setSectionResizeMode(column_index(OutlinerColumn::Locked),
                                 QHeaderView::ResizeToContents);
    layout->addWidget(tree_);

    tree_->row_press_handler = [this](EntityId entity, Qt::MouseButton button,
                                      Qt::KeyboardModifiers modifiers) {
      handle_row_press(entity, button, modifiers);
    };
    tree_->property_toggle_handler = [this](EntityId entity, int column) {
      handle_property_toggle(entity, column);
    };
    tree_->context_menu_handler = [this](EntityId entity,
                                         QPoint global_position) {
      show_context_menu(entity, global_position);
    };

    QObject::connect(model_, &QStandardItemModel::itemChanged, &panel_,
                     [this](QStandardItem *item) {
                       handle_item_changed(item);
                     });

    QObject::connect(tree_, &QTreeView::collapsed, &panel_,
                     [this](const QModelIndex &index) {
                       if (!syncing_expansion_) {
                         collapsed_entities_.insert(entity_id(index));
                       }
                     });
    QObject::connect(tree_, &QTreeView::expanded, &panel_,
                     [this](const QModelIndex &index) {
                       if (!syncing_expansion_) {
                         collapsed_entities_.erase(entity_id(index));
                       }
                     });
  }

  void refresh(UIContext &context) {
    editor_ = &context.intent.editor;
    document_ = &context.scene.document;
    localization_ = &context.localization;

    panel_.setWindowTitle(qt_text(
        localization_->text(ui_panel_localization_key(UIPanelId::Outliner),
                            ui_panel_key(UIPanelId::Outliner))));
    refresh_headers();
    refresh_model_if_needed();
    sync_selection();
    if (outliner_anchor_ != kInvalidEntityId &&
        !document_->exists(outliner_anchor_)) {
      outliner_anchor_ = kInvalidEntityId;
    }
  }

private:
  const EntityRecord *record(EntityId entity) const {
    const auto found = cache_.record_index_by_entity.find(entity);
    if (found == cache_.record_index_by_entity.end() ||
        found->second >= cache_.records.size()) {
      return nullptr;
    }
    return &cache_.records[found->second];
  }

  bool row_is_revealed(EntityId entity) const {
    const EntityRecord *entity_record = record(entity);
    while (entity_record != nullptr &&
           entity_record->parent != kInvalidEntityId) {
      if (collapsed_entities_.contains(entity_record->parent)) {
        return false;
      }
      entity_record = record(entity_record->parent);
    }
    return true;
  }

  void refresh_headers() {
    if (model_ == nullptr) {
      return;
    }
    model_->setHeaderData(
        column_index(OutlinerColumn::Name), Qt::Horizontal,
        localized("ui.outliner.name", "Name"));
    model_->setHeaderData(
        column_index(OutlinerColumn::Visible), Qt::Horizontal,
        localized("ui.outliner.visible", "Visible"));
    model_->setHeaderData(
        column_index(OutlinerColumn::Locked), Qt::Horizontal,
        localized("ui.outliner.locked", "Locked"));
  }

  void refresh_model_if_needed() {
    if (document_ == nullptr ||
        (cache_.valid && cache_.scene_revision == document_->revision())) {
      return;
    }

    cache_.scene_revision = document_->revision();
    cache_.records = document_->entity_records_tree_order();
    cache_.record_index_by_entity.clear();
    cache_.record_index_by_entity.reserve(cache_.records.size());
    cache_.children_by_entity.clear();
    cache_.children_by_entity.reserve(cache_.records.size());
    for (std::size_t index = 0; index < cache_.records.size(); ++index) {
      const EntityRecord &entity_record = cache_.records[index];
      cache_.record_index_by_entity.emplace(entity_record.id, index);
      if (entity_record.parent != kInvalidEntityId) {
        cache_.children_by_entity[entity_record.parent].push_back(
            entity_record.id);
      }
    }
    cache_.roots = document_->root_entities();
    cache_.valid = true;

    std::erase_if(collapsed_entities_, [this](EntityId entity) {
      return !cache_.record_index_by_entity.contains(entity);
    });

    tree_->setUpdatesEnabled(false);
    syncing_expansion_ = true;
    syncing_model_ = true;
    model_->removeRows(0, model_->rowCount());
    item_by_entity_.clear();
    item_by_entity_.reserve(cache_.records.size());

    std::unordered_set<EntityId> appended;
    appended.reserve(cache_.records.size());
    std::function<void(EntityId, QStandardItem *)> append_entity =
        [&](EntityId entity, QStandardItem *parent) {
          if (!appended.insert(entity).second) {
            return;
          }
          const EntityRecord *entity_record = record(entity);
          if (entity_record == nullptr) {
            return;
          }

          auto *name_item = new QStandardItem(qt_text(entity_record->name));
          name_item->setEditable(!entity_record->is_scene_root);
          name_item->setData(static_cast<qulonglong>(entity_record->id),
                             kEntityIdRole);
          name_item->setData(!entity_record->effectively_visible ||
                                 entity_record->effectively_locked,
                             kSubduedRole);

          auto *visible_item = new QStandardItem;
          visible_item->setEditable(false);
          visible_item->setCheckable(true);
          visible_item->setData(static_cast<qulonglong>(entity_record->id),
                                kEntityIdRole);
          visible_item->setData(
              qt_check_state(outliner_visibility_property_state(
                  entity_record->visible, entity_record->effectively_visible)),
              Qt::CheckStateRole);

          auto *locked_item = new QStandardItem;
          locked_item->setEditable(false);
          locked_item->setCheckable(true);
          locked_item->setData(static_cast<qulonglong>(entity_record->id),
                               kEntityIdRole);
          locked_item->setData(
              qt_check_state(outliner_lock_property_state(
                  entity_record->locked, entity_record->effectively_locked)),
              Qt::CheckStateRole);

          QList<QStandardItem *> row{name_item, visible_item, locked_item};
          if (parent != nullptr) {
            parent->appendRow(row);
          } else {
            model_->appendRow(row);
          }
          item_by_entity_.emplace(entity_record->id, name_item);

          const auto children = cache_.children_by_entity.find(entity);
          if (children == cache_.children_by_entity.end()) {
            return;
          }
          for (const EntityId child : children->second) {
            append_entity(child, name_item);
          }
        };

    for (const EntityId root : cache_.roots) {
      append_entity(root, nullptr);
    }

    for (const EntityRecord &entity_record : cache_.records) {
      if (!entity_record.has_children) {
        continue;
      }
      const auto item = item_by_entity_.find(entity_record.id);
      if (item != item_by_entity_.end()) {
        tree_->setExpanded(item->second->index(),
                           !collapsed_entities_.contains(entity_record.id));
      }
    }
    selection_sync_after_model_rebuild_ = true;
    syncing_model_ = false;
    syncing_expansion_ = false;
    tree_->setUpdatesEnabled(true);
  }

  void sync_selection() {
    if (document_ == nullptr || tree_->selectionModel() == nullptr) {
      return;
    }

    const std::uint64_t selection_revision = document_->selection_revision();
    if (!outliner_selection_sync_required(
            selection_sync_after_model_rebuild_, selection_revision_valid_,
            synced_selection_revision_, selection_revision)) {
      return;
    }

    const std::vector<EntityId> selected = document_->selected_entities();
    QItemSelection widget_selection;
    for (const EntityId entity : selected) {
      const auto item = item_by_entity_.find(entity);
      if (item != item_by_entity_.end()) {
        const QModelIndex index = item->second->index();
        widget_selection.select(index, index);
      }
    }

    const QSignalBlocker selection_blocker(tree_->selectionModel());
    tree_->selectionModel()->clearSelection();
    tree_->selectionModel()->select(widget_selection,
                                    QItemSelectionModel::Select |
                                        QItemSelectionModel::Rows);

    const auto primary = item_by_entity_.find(document_->primary_selection());
    tree_->setCurrentIndex(primary != item_by_entity_.end() &&
                                   row_is_revealed(
                                       document_->primary_selection())
                               ? primary->second->index()
                               : QModelIndex{});
    synced_selection_revision_ = selection_revision;
    selection_revision_valid_ = true;
    selection_sync_after_model_rebuild_ = false;
  }

  void handle_row_press(EntityId entity, Qt::MouseButton button,
                        Qt::KeyboardModifiers modifiers) {
    if (editor_ == nullptr || document_ == nullptr ||
        entity == kInvalidEntityId) {
      return;
    }
    refresh_model_if_needed();
    if (record(entity) == nullptr) {
      return;
    }

    const std::vector<EntityId> selected = document_->selected_entities();
    if (button == Qt::RightButton) {
      if (!contains_entity(selected, entity)) {
        (void)select_only(*editor_, {entity}, entity);
        sync_selection();
      }
      return;
    }
    if (button != Qt::LeftButton) {
      return;
    }

    EntityId active_selection = entity;
    std::vector<EntityId> next_selection;
    if (modifiers.testFlag(Qt::ShiftModifier)) {
      next_selection =
          entity_range_selection(cache_.records, outliner_anchor_, entity);
    } else if (modifiers.testFlag(Qt::ControlModifier) ||
               modifiers.testFlag(Qt::MetaModifier)) {
      next_selection = selected;
      if (contains_entity(next_selection, entity)) {
        next_selection.erase(
            std::remove(next_selection.begin(), next_selection.end(), entity),
            next_selection.end());
        active_selection = document_->primary_selection() == entity
                               ? kInvalidEntityId
                               : document_->primary_selection();
      } else {
        next_selection.push_back(entity);
      }
    } else {
      next_selection = {entity};
    }

    outliner_anchor_ = entity;
    (void)select_only(*editor_, next_selection, active_selection);
    sync_selection();
  }

  void handle_property_toggle(EntityId entity, int column) {
    if (editor_ == nullptr || document_ == nullptr) {
      return;
    }
    refresh_model_if_needed();
    const EntityRecord *entity_record = record(entity);
    if (entity_record == nullptr) {
      return;
    }

    QuaderApiVoidResult result;
    if (column == column_index(OutlinerColumn::Visible)) {
      result = editor_->scene().set_visible(entity, !entity_record->visible);
    } else if (column == column_index(OutlinerColumn::Locked)) {
      result = editor_->scene().set_locked(entity, !entity_record->locked);
    } else {
      return;
    }

    if (result.ok && result.changed) {
      refresh_model_if_needed();
      sync_selection();
    }
  }

  void handle_item_changed(QStandardItem *item) {
    if (syncing_model_ || item == nullptr || editor_ == nullptr ||
        document_ == nullptr ||
        item->column() != column_index(OutlinerColumn::Name)) {
      return;
    }

    const EntityId entity = entity_id(item->index());
    const EntityRecord *entity_record = record(entity);
    if (entity_record == nullptr || entity_record->is_scene_root) {
      return;
    }

    const std::string next_name = item->text().toUtf8().toStdString();
    if (next_name == entity_record->name) {
      return;
    }

    const QuaderApiVoidResult result =
        editor_->scene().rename(entity, next_name);
    if (!result.ok) {
      const QSignalBlocker model_blocker(model_);
      item->setText(qt_text(entity_record->name));
    }
  }

  void begin_rename(EntityId entity) {
    const auto item = item_by_entity_.find(entity);
    if (item == item_by_entity_.end() || item->second == nullptr ||
        !item->second->isEditable()) {
      return;
    }
    const QModelIndex index = item->second->index();
    tree_->setCurrentIndex(index);
    tree_->scrollTo(index);
    tree_->edit(index);
  }

  QString localized(std::string_view key, std::string_view fallback) const {
    return localization_ != nullptr
               ? qt_text(localization_->text(key, fallback))
               : qt_text(fallback);
  }

  void show_context_menu(EntityId entity, QPoint global_position) {
    if (editor_ == nullptr || document_ == nullptr ||
        entity == kInvalidEntityId) {
      return;
    }
    refresh_model_if_needed();
    sync_selection();
    const EntityRecord *entity_record = record(entity);
    if (entity_record == nullptr) {
      return;
    }

    const bool can_mutate_hierarchy =
        !entity_record->is_scene_root && !entity_record->effectively_locked;
    const bool can_ungroup = can_mutate_hierarchy &&
                             entity_record->kind == EntityKind::Empty &&
                             entity_record->has_children;

    QMenu menu(tree_);
    menu.setObjectName(QStringLiteral("outliner.context_menu"));
    QAction *rename_action =
        menu.addAction(localized("ui.outliner.rename", "Rename"));
    rename_action->setObjectName(QStringLiteral("outliner.action.rename"));
    rename_action->setEnabled(!entity_record->is_scene_root);
    menu.addSeparator();

    QAction *delete_action =
        menu.addAction(localized("ui.outliner.delete", "Delete"));
    delete_action->setObjectName(QStringLiteral("outliner.action.delete"));
    delete_action->setEnabled(can_mutate_hierarchy);

    QAction *duplicate_action =
        menu.addAction(localized("ui.outliner.duplicate", "Duplicate"));
    duplicate_action->setObjectName(
        QStringLiteral("outliner.action.duplicate"));
    duplicate_action->setEnabled(can_mutate_hierarchy);

    QAction *group_action =
        menu.addAction(localized("ui.outliner.group", "Group"));
    group_action->setObjectName(QStringLiteral("outliner.action.group"));
    group_action->setEnabled(can_mutate_hierarchy);

    QAction *ungroup_action =
        menu.addAction(localized("ui.outliner.ungroup", "Ungroup"));
    ungroup_action->setObjectName(QStringLiteral("outliner.action.ungroup"));
    ungroup_action->setEnabled(can_ungroup);

    QAction *chosen = menu.exec(global_position);
    if (chosen == rename_action) {
      begin_rename(entity);
      return;
    } else if (chosen == delete_action) {
      delete_selection(*editor_, *document_);
    } else if (chosen == duplicate_action) {
      duplicate_selection(*editor_);
    } else if (chosen == group_action) {
      group_selection(*editor_, *document_);
    } else if (chosen == ungroup_action) {
      ungroup_entity(*editor_, *document_, entity);
    }

    refresh_model_if_needed();
    sync_selection();
  }

  OutlinerPanel &panel_;
  OutlinerTreeView *tree_ = nullptr;
  QStandardItemModel *model_ = nullptr;
  QuaderEditorApi *editor_ = nullptr;
  const SceneDocument *document_ = nullptr;
  LocalizationService *localization_ = nullptr;
  TreeCache cache_;
  std::unordered_map<EntityId, QStandardItem *> item_by_entity_;
  std::unordered_set<EntityId> collapsed_entities_;
  EntityId outliner_anchor_ = kInvalidEntityId;
  std::uint64_t synced_selection_revision_ = 0;
  bool syncing_expansion_ = false;
  bool syncing_model_ = false;
  bool selection_revision_valid_ = false;
  bool selection_sync_after_model_rebuild_ = true;
};

OutlinerPanel::OutlinerPanel(QWidget *parent)
    : QWidget(parent), impl_(std::make_unique<Impl>(*this)) {}

OutlinerPanel::~OutlinerPanel() = default;

void OutlinerPanel::refresh(UIContext &context) { impl_->refresh(context); }

} // namespace quader
