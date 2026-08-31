////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/outliner_panel.hpp"

#include "api/quader_editor_api.hpp"
#include "core/localization_service.hpp"
#include "diagnostics/profile.hpp"
#include "editor/commands/document/document_command_scene_helpers.hpp"
#include "scene/scene_document.hpp"
#include "ui/ui_context.hpp"
#include "ui/ui_panel.hpp"

#include <QAbstractItemView>
#include <QAction>
#include <QContextMenuEvent>
#include <QEvent>
#include <QFocusEvent>
#include <QHeaderView>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QList>
#include <QMenu>
#include <QMetaObject>
#include <QModelIndex>
#include <QMouseEvent>
#include <QPainter>
#include <QScopedValueRollback>
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
#include <string>
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

bool outliner_projection_matches(std::span<const EntityRecord> left,
                                  std::span<const EntityRecord> right) {
  if (left.size() != right.size()) {
    return false;
  }
  for (std::size_t index = 0; index < left.size(); ++index) {
    const EntityRecord &a = left[index];
    const EntityRecord &b = right[index];
    // Keep this comparison limited to the fields rendered by Outliner rows or
    // consumed by their actions. Other record changes must retain the model.
    if (a.id != b.id || a.name != b.name || a.kind != b.kind ||
        a.parent != b.parent || a.visible != b.visible ||
        a.effectively_visible != b.effectively_visible ||
        a.locked != b.locked ||
        a.effectively_locked != b.effectively_locked ||
        a.has_children != b.has_children ||
        a.is_scene_root != b.is_scene_root) {
      return false;
    }
  }
  return true;
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

/// Coalesces mouse selection into one owner transaction, normalizes Qt's
/// private range anchor after external selection changes, and adds keyboard
/// context targeting.
class OutlinerTreeView final : public QTreeView {
public:
  using ContextMenuHandler = std::function<void(EntityId, QPoint)>;
  using SelectionGestureFinishedHandler = std::function<void()>;

  explicit OutlinerTreeView(QWidget *parent = nullptr) : QTreeView(parent) {}

  ContextMenuHandler context_menu_handler;
  SelectionGestureFinishedHandler selection_gesture_finished_handler;

  [[nodiscard]] bool selection_gesture_active() const noexcept {
    return selection_gesture_active_;
  }

  void reset_selection_anchor(const QModelIndex &index) {
    const QScopedValueRollback<bool> reset_guard(resetting_selection_anchor_,
                                                  true);
    externally_synchronized_anchor_ = index;
    if (!index.isValid() && model() != nullptr) {
      const QModelIndex fallback = model()->index(0, 0, rootIndex());
      if (fallback.isValid()) {
        externally_synchronized_anchor_ = fallback;
      }
    }
    externally_synchronized_anchor_valid_ =
        externally_synchronized_anchor_.isValid();
    QTreeView::setCurrentIndex(index);
  }

protected:
  QItemSelectionModel::SelectionFlags
  selectionCommand(const QModelIndex &index,
                   const QEvent *event = nullptr) const override {
    if (resetting_selection_anchor_) {
      // Reset the current/Shift-selection anchor without replacing the
      // externally synchronized multi-row selection.
      return QItemSelectionModel::NoUpdate;
    }
    return QTreeView::selectionCommand(index, event);
  }

  void mousePressEvent(QMouseEvent *event) override {
    if (event->button() == Qt::LeftButton) {
      selection_gesture_active_ = true;
      if ((event->modifiers() & Qt::ShiftModifier) != 0 &&
          externally_synchronized_anchor_valid_ &&
          selectionModel() != nullptr) {
        const QModelIndex clicked = indexAt(event->position().toPoint());
        if (clicked.isValid()) {
          // QAbstractItemView retains a private range anchor which cannot be
          // updated when an external owner keeps the same current row. Apply
          // the first Shift range from the explicit owner-synchronized anchor
          // while preserving Qt's normal row geometry and deferred commit.
          const QRect range(visualRect(externally_synchronized_anchor_).center(),
                            event->position().toPoint());
          selectionModel()->setCurrentIndex(
              clicked, QItemSelectionModel::NoUpdate);
          setSelection(range, QItemSelectionModel::ClearAndSelect |
                                  QItemSelectionModel::Rows);
          event->accept();
          return;
        }
      }
      externally_synchronized_anchor_valid_ = false;
    }
    QTreeView::mousePressEvent(event);
  }

  void mouseReleaseEvent(QMouseEvent *event) override {
    QTreeView::mouseReleaseEvent(event);
    if (event->button() == Qt::LeftButton) {
      finish_selection_gesture();
    }
  }

  void focusOutEvent(QFocusEvent *event) override {
    QTreeView::focusOutEvent(event);
    finish_selection_gesture();
  }

  bool event(QEvent *event) override {
    const bool handled = QTreeView::event(event);
    if (event->type() == QEvent::UngrabMouse ||
        event->type() == QEvent::WindowDeactivate ||
        event->type() == QEvent::Hide) {
      finish_selection_gesture();
    }
    return handled;
  }

  void contextMenuEvent(QContextMenuEvent *event) override {
    const QModelIndex index =
        event->reason() == QContextMenuEvent::Keyboard
            ? currentIndex()
            : indexAt(event->pos());
    if (!index.isValid() || context_menu_handler == nullptr) {
      QTreeView::contextMenuEvent(event);
      return;
    }
    const QPoint global_position =
        event->reason() == QContextMenuEvent::Keyboard
            ? viewport()->mapToGlobal(visualRect(index).center())
            : event->globalPos();
    context_menu_handler(entity_id(index), global_position);
    event->accept();
  }

private:
  void finish_selection_gesture() {
    if (!selection_gesture_active_) {
      return;
    }
    selection_gesture_active_ = false;
    if (selection_gesture_finished_handler != nullptr) {
      selection_gesture_finished_handler();
    }
  }

  bool selection_gesture_active_ = false;
  bool resetting_selection_anchor_ = false;
  QPersistentModelIndex externally_synchronized_anchor_;
  bool externally_synchronized_anchor_valid_ = false;
};

} // namespace

/// Owns OutlinerPanel's retained tree model, cache and interaction wiring.
struct OutlinerPanel::Impl {
  /// Caches the scene hierarchy projection for revision-gated tree rebuilds.
  struct TreeCache {
    bool valid = false;
    const SceneDocument *document = nullptr;
    std::uint64_t hierarchy_presentation_revision = 0;
    EntityId instance_scope_definition = kInvalidEntityId;
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
    layout->setObjectName(QStringLiteral("outliner.layout"));
    layout->setContentsMargins(0, 0, 0, 0);
    tree_ = new OutlinerTreeView(&panel_);
    tree_->setObjectName(QStringLiteral("outliner.tree"));
    tree_->setSelectionBehavior(QAbstractItemView::SelectRows);
    tree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tree_->setEditTriggers(QAbstractItemView::EditKeyPressed);
    tree_->setAlternatingRowColors(true);
    tree_->setUniformRowHeights(true);
    tree_->setItemDelegate(new SubduedRowDelegate(tree_));
    panel_.setFocusProxy(tree_);

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

    tree_->context_menu_handler = [this](EntityId entity,
                                         QPoint global_position) {
      show_context_menu(entity, global_position);
    };
    tree_->selection_gesture_finished_handler = [this]() {
      if (!widget_selection_sync_deferred_) {
        return;
      }
      widget_selection_sync_deferred_ = false;
      handle_widget_selection_changed();
    };

    QObject::connect(model_, &QStandardItemModel::itemChanged, &panel_,
                     [this](QStandardItem *item) {
                       handle_item_changed(item);
                     });
    QObject::connect(tree_->selectionModel(),
                     &QItemSelectionModel::selectionChanged, &panel_,
                     [this](const QItemSelection &, const QItemSelection &) {
                       schedule_widget_selection_sync();
                     });
    QObject::connect(tree_->selectionModel(),
                     &QItemSelectionModel::currentChanged, &panel_,
                     [this](const QModelIndex &, const QModelIndex &) {
                       schedule_widget_selection_sync();
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
    const bool localization_changed =
        localization_ != &context.localization ||
        localized_locale_ != context.localization.current_locale() ||
        localized_revision_ != context.localization.revision();
    localization_ = &context.localization;
    const QuaderInstanceEditState instance_edit =
        editor_->modeling().instance_edit_state();
    instance_scope_definition_ =
        instance_edit.active ? instance_edit.definition.id : kInvalidEntityId;

    if (localization_changed) {
      localized_locale_ = localization_->current_locale();
      localized_revision_ = localization_->revision();
      panel_.setWindowTitle(qt_text(
          localization_->text(ui_panel_localization_key(UIPanelId::Outliner),
                              ui_panel_key(UIPanelId::Outliner))));
      refresh_headers();
    }
    refresh_model_if_needed();
    sync_selection();
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
    QDR_PROFILE_SCOPE("Qt outliner refresh model if needed");
    const bool document_changed = cache_.document != document_;
    if (document_ == nullptr ||
        (cache_.valid &&
         !document_changed &&
         cache_.hierarchy_presentation_revision ==
             document_->hierarchy_presentation_revision() &&
         cache_.instance_scope_definition == instance_scope_definition_)) {
      return;
    }

    if (document_changed) {
      collapsed_entities_.clear();
    }

    std::vector<EntityRecord> next_records;
    {
      QDR_PROFILE_SCOPE("Qt outliner project records");
      next_records =
          instance_scope_definition_ != kInvalidEntityId
              ? document_->entity_records_for_instance_definition(
                    instance_scope_definition_)
              : document_->entity_records_tree_order();
    }

    if (cache_.valid && !document_changed &&
        cache_.instance_scope_definition == instance_scope_definition_ &&
        outliner_projection_matches(cache_.records, next_records)) {
      QDR_PROFILE_SCOPE("Qt outliner retain unchanged projection");
      cache_.hierarchy_presentation_revision =
          document_->hierarchy_presentation_revision();
      return;
    }

    cache_.document = document_;
    cache_.hierarchy_presentation_revision =
        document_->hierarchy_presentation_revision();
    cache_.instance_scope_definition = instance_scope_definition_;
    cache_.records = std::move(next_records);
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
    cache_.roots = instance_scope_definition_ != kInvalidEntityId
                       ? document_->children(instance_scope_definition_)
                       : document_->root_entities();
    cache_.valid = true;

    std::erase_if(collapsed_entities_, [this](EntityId entity) {
      return !cache_.record_index_by_entity.contains(entity);
    });

    {
      QDR_PROFILE_SCOPE("Qt outliner rebuild item model");
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
  }

  [[nodiscard]] QItemSelection
  batched_row_selection(std::span<const EntityId> entities) const {
    std::vector<QStandardItem *> items;
    items.reserve(entities.size());
    for (const EntityId entity : entities) {
      const auto found = item_by_entity_.find(entity);
      if (found != item_by_entity_.end() && found->second != nullptr) {
        items.push_back(found->second);
      }
    }
    std::sort(items.begin(), items.end(),
              [](QStandardItem *left, QStandardItem *right) {
                if (left->parent() != right->parent()) {
                  return std::less<QStandardItem *>{}(left->parent(),
                                                      right->parent());
                }
                return left->row() < right->row();
              });

    QItemSelection selection;
    if (items.empty()) {
      return selection;
    }

    QStandardItem *range_first = items.front();
    QStandardItem *range_last = range_first;
    const auto append_range = [&selection](QStandardItem *first,
                                            QStandardItem *last) {
      selection.select(first->index(), last->index());
    };
    for (std::size_t index = 1; index < items.size(); ++index) {
      QStandardItem *item = items[index];
      if (item == range_last) {
        continue;
      }
      if (item->parent() == range_last->parent() &&
          item->row() == range_last->row() + 1) {
        range_last = item;
        continue;
      }
      append_range(range_first, range_last);
      range_first = item;
      range_last = item;
    }
    append_range(range_first, range_last);
    return selection;
  }

  void sync_selection() {
    QDR_PROFILE_SCOPE("Qt outliner synchronize selection");
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
    const EntityId primary_selection = document_->primary_selection();

    QItemSelectionModel *selection_model = tree_->selectionModel();
    std::unordered_set<EntityId> widget_entities;
    const QModelIndexList widget_rows = selection_model->selectedRows(
        column_index(OutlinerColumn::Name));
    widget_entities.reserve(static_cast<std::size_t>(widget_rows.size()));
    for (const QModelIndex &index : widget_rows) {
      const EntityId entity = entity_id(index);
      if (entity != kInvalidEntityId) {
        widget_entities.insert(entity);
      }
    }

    std::unordered_set<EntityId> desired_entities;
    desired_entities.reserve(selected.size());
    for (const EntityId entity : selected) {
      if (item_by_entity_.contains(entity)) {
        desired_entities.insert(entity);
      }
    }

    std::vector<EntityId> entities_to_select;
    entities_to_select.reserve(desired_entities.size());
    for (const EntityId entity : desired_entities) {
      if (!widget_entities.contains(entity)) {
        entities_to_select.push_back(entity);
      }
    }
    std::vector<EntityId> entities_to_deselect;
    entities_to_deselect.reserve(widget_entities.size());
    for (const EntityId entity : widget_entities) {
      if (!desired_entities.contains(entity)) {
        entities_to_deselect.push_back(entity);
      }
    }

    const QItemSelection rows_to_select =
        batched_row_selection(entities_to_select);
    const QItemSelection rows_to_deselect =
        batched_row_selection(entities_to_deselect);
    const QScopedValueRollback<bool> selection_guard(syncing_selection_, true);
    const bool updates_enabled = tree_->updatesEnabled();
    if (updates_enabled) {
      tree_->setUpdatesEnabled(false);
    }
    {
      // Row membership synchronization must remain signal-silent. Release the
      // blocker before resetting the current index below: QAbstractItemView
      // observes that currentChanged signal to reset its private Shift anchor.
      const QSignalBlocker selection_model_blocker(selection_model);
      if (!rows_to_deselect.isEmpty()) {
        selection_model->select(rows_to_deselect,
                                QItemSelectionModel::Deselect |
                                    QItemSelectionModel::Rows);
      }
      if (!rows_to_select.isEmpty()) {
        selection_model->select(rows_to_select, QItemSelectionModel::Select |
                                                    QItemSelectionModel::Rows);
      }
    }

    const auto primary = item_by_entity_.find(primary_selection);
    tree_->reset_selection_anchor(
        primary != item_by_entity_.end() &&
                row_is_revealed(primary_selection)
            ? primary->second->index()
            : QModelIndex{});
    if (updates_enabled) {
      tree_->setUpdatesEnabled(true);
      tree_->viewport()->update();
    }
    synced_selection_revision_ = selection_revision;
    selection_revision_valid_ = true;
    selection_sync_after_model_rebuild_ = false;
  }

  void schedule_widget_selection_sync() {
    if (syncing_model_ || syncing_selection_) {
      return;
    }
    if (tree_->selection_gesture_active()) {
      widget_selection_sync_deferred_ = true;
      return;
    }
    if (pending_widget_selection_sync_) {
      return;
    }
    pending_widget_selection_sync_ = true;
    QMetaObject::invokeMethod(
        &panel_,
        [this]() {
          pending_widget_selection_sync_ = false;
          handle_widget_selection_changed();
        },
        Qt::QueuedConnection);
  }

  void handle_widget_selection_changed() {
    if (syncing_model_ || syncing_selection_ || editor_ == nullptr ||
        document_ == nullptr ||
        tree_->selectionModel() == nullptr) {
      return;
    }

    const QModelIndexList selected_rows =
        tree_->selectionModel()->selectedRows(
            column_index(OutlinerColumn::Name));
    std::vector<EntityId> selected;
    selected.reserve(static_cast<std::size_t>(selected_rows.size()));
    for (const QModelIndex &index : selected_rows) {
      const EntityId selected_entity = entity_id(index);
      if (selected_entity != kInvalidEntityId) {
        selected.push_back(selected_entity);
      }
    }

    EntityId active = entity_id(tree_->currentIndex());
    if (!contains_entity(selected, active)) {
      const EntityId previous_active = document_->primary_selection();
      active = contains_entity(selected, previous_active)
                   ? previous_active
                   : (selected.empty() ? kInvalidEntityId : selected.back());
    }

    const QuaderApiVoidResult result =
        editor_->selection().select_objects_for_inspection(
            std::span<const EntityId>(selected.data(), selected.size()),
            active);
    if (!result.ok) {
      selection_sync_after_model_rebuild_ = true;
      sync_selection();
      return;
    }

    synced_selection_revision_ = document_->selection_revision();
    selection_revision_valid_ = true;
    selection_sync_after_model_rebuild_ = false;
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

    if (!result.ok || !result.changed) {
      cache_.valid = false;
    }
    QMetaObject::invokeMethod(
        &panel_,
        [this]() {
          refresh_model_if_needed();
          sync_selection();
        },
        Qt::QueuedConnection);
  }

  void handle_item_changed(QStandardItem *item) {
    if (syncing_model_ || item == nullptr || editor_ == nullptr ||
        document_ == nullptr) {
      return;
    }

    const EntityId entity = entity_id(item->index());
    const EntityRecord *entity_record = record(entity);
    if (entity_record == nullptr) {
      return;
    }

    if (item->column() != column_index(OutlinerColumn::Name)) {
      handle_property_toggle(entity, item->column());
      return;
    }

    if (entity_record->is_scene_root) {
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
    tree_->selectionModel()->setCurrentIndex(index,
                                             QItemSelectionModel::NoUpdate);
    tree_->scrollTo(index);
    tree_->edit(index);
  }

  QString localized(std::string_view key, std::string_view fallback) const {
    return localization_ != nullptr
               ? qt_text(localization_->text(key, fallback))
               : qt_text(fallback);
  }

  void show_context_menu(EntityId entity, QPoint global_position) {
    QDR_PROFILE_SCOPE("Qt outliner context menu show");
    if (editor_ == nullptr || document_ == nullptr ||
        entity == kInvalidEntityId) {
      return;
    }
    {
      QDR_PROFILE_SCOPE("Qt outliner context menu query state");
      refresh_model_if_needed();
      if (!contains_entity(document_->selected_entities(), entity)) {
        (void)select_only(*editor_, {entity}, entity);
      }
      sync_selection();
    }
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
    QAction *rename_action = nullptr;
    QAction *delete_action = nullptr;
    QAction *duplicate_action = nullptr;
    QAction *group_action = nullptr;
    QAction *ungroup_action = nullptr;
    {
      QDR_PROFILE_SCOPE("Qt outliner context menu build");
      menu.setObjectName(QStringLiteral("outliner.context_menu"));
      rename_action =
          menu.addAction(localized("ui.outliner.rename", "Rename"));
      rename_action->setObjectName(QStringLiteral("outliner.action.rename"));
      rename_action->setEnabled(!entity_record->is_scene_root);
      menu.addSeparator();

      delete_action =
          menu.addAction(localized("ui.outliner.delete", "Delete"));
      delete_action->setObjectName(QStringLiteral("outliner.action.delete"));
      delete_action->setEnabled(can_mutate_hierarchy);

      duplicate_action =
          menu.addAction(localized("ui.outliner.duplicate", "Duplicate"));
      duplicate_action->setObjectName(
          QStringLiteral("outliner.action.duplicate"));
      duplicate_action->setEnabled(can_mutate_hierarchy);

      group_action =
          menu.addAction(localized("ui.outliner.group", "Group"));
      group_action->setObjectName(QStringLiteral("outliner.action.group"));
      group_action->setEnabled(can_mutate_hierarchy);

      ungroup_action =
          menu.addAction(localized("ui.outliner.ungroup", "Ungroup"));
      ungroup_action->setObjectName(
          QStringLiteral("outliner.action.ungroup"));
      ungroup_action->setEnabled(can_ungroup);
    }

    QAction *chosen = nullptr;
    {
      QDR_PROFILE_SCOPE("Qt outliner context menu exec");
      chosen = menu.exec(global_position);
    }
    {
      QDR_PROFILE_SCOPE("Qt outliner context menu dispatch action");
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
    }

    {
      QDR_PROFILE_SCOPE("Qt outliner context menu synchronize result");
      refresh_model_if_needed();
      sync_selection();
    }
  }

  OutlinerPanel &panel_;
  OutlinerTreeView *tree_ = nullptr;
  QStandardItemModel *model_ = nullptr;
  QuaderEditorApi *editor_ = nullptr;
  const SceneDocument *document_ = nullptr;
  LocalizationService *localization_ = nullptr;
  std::string localized_locale_;
  std::uint64_t localized_revision_ = 0u;
  TreeCache cache_;
  std::unordered_map<EntityId, QStandardItem *> item_by_entity_;
  std::unordered_set<EntityId> collapsed_entities_;
  EntityId instance_scope_definition_ = kInvalidEntityId;
  std::uint64_t synced_selection_revision_ = 0;
  bool syncing_expansion_ = false;
  bool syncing_model_ = false;
  bool syncing_selection_ = false;
  bool pending_widget_selection_sync_ = false;
  bool widget_selection_sync_deferred_ = false;
  bool selection_revision_valid_ = false;
  bool selection_sync_after_model_rebuild_ = true;
};

OutlinerPanel::OutlinerPanel(QWidget *parent)
    : QWidget(parent), impl_(std::make_unique<Impl>(*this)) {}

OutlinerPanel::~OutlinerPanel() = default;

void OutlinerPanel::refresh(UIContext &context) { impl_->refresh(context); }

} // namespace quader
