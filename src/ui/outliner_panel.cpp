////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/outliner_panel.hpp"
#include "ui/qt_contextual_input_hints.hpp"

#include "api/quader_command_ids.hpp"
#include "api/quader_editor_api.hpp"
#include "core/localization_service.hpp"
#include "diagnostics/profile.hpp"
#include "editor/commands/document/document_command_scene_helpers.hpp"
#include "scene/scene_document.hpp"
#include "ui/qt_multiresolution_icon.hpp"
#include "ui/qt_tree_relationship_presentation.hpp"
#include "ui/qt_ui_scale_metrics.hpp"
#include "ui/qt_context_menu_visibility.hpp"
#include "ui/ui_context.hpp"
#include "ui/ui_panel.hpp"

#include <QAbstractItemView>
#include <QAction>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFocusEvent>
#include <QHeaderView>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QList>
#include <QMenu>
#include <QMetaObject>
#include <QMimeData>
#include <QModelIndex>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QResource>
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
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

static void initialize_quader_outliner_icon_resources() {
  Q_INIT_RESOURCE(quader_outliner_icons);
}

namespace quader {

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
constexpr int kActiveEntityRole = Qt::UserRole + 3;

/// Keeps the five authored entity symbols shared across retained tree rows.
enum class OutlinerIcon : std::size_t {
  Mesh,
  Curve,
  PointLight,
  DirectionalLight,
  Group,
  Count,
};

using OutlinerIconCache =
    std::array<QIcon, static_cast<std::size_t>(OutlinerIcon::Count)>;

OutlinerIconCache load_outliner_icons() {
  static const bool kResourcesInitialized = [] {
    initialize_quader_outliner_icon_resources();
    return true;
  }();
  (void)kResourcesInitialized;
  return {
      qt_multiresolution_icon(QStringLiteral(":/quader/icons/tool_create_box.png")),
      qt_multiresolution_icon(QStringLiteral(":/quader/icons/outliner_curve.png")),
      qt_multiresolution_icon(QStringLiteral(":/quader/icons/outliner_point_light.png")),
      qt_multiresolution_icon(QStringLiteral(":/quader/icons/outliner_directional_light.png")),
      qt_multiresolution_icon(QStringLiteral(":/quader/icons/outliner_group.png")),
  };
}

/// Identifies the resizable scene-tree columns owned by the Qt outliner model.
enum class OutlinerColumn : int {
  Name = 0,
  Count = 1,
};

constexpr int column_index(OutlinerColumn column) {
  return static_cast<int>(column);
}

QString qt_text(std::string_view text) {
  return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

QString with_shortcut(QString label, std::string_view shortcut) {
  if (shortcut.empty()) {
    return label;
  }
  label.append(QLatin1Char('\t'));
  label.append(qt_text(shortcut));
  return label;
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
        a.selectable != b.selectable ||
        a.effectively_selectable != b.effectively_selectable ||
        a.has_children != b.has_children) {
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

struct OutlinerRuntimeBindingToken final {
  const std::uint64_t *generation = nullptr;
  std::uint64_t expected = 0;

  [[nodiscard]] bool current() const noexcept {
    return generation != nullptr && *generation == expected;
  }
};

bool select_current_scene_selection(QuaderEditorApi &editor,
                                    const SceneDocument &document) {
  const std::vector<EntityId> selected = document.selected_entities();
  return select_only(editor, selected, document.primary_selection());
}

void group_selection(QuaderEditorApi &editor, const SceneDocument &document,
                     OutlinerRuntimeBindingToken binding) {
  const std::vector<EntityId> roots =
      document_command_selected_root_entities(document);
  if (roots.empty()) {
    return;
  }

  const QuaderApiVoidResult result =
      editor.commands().execute(quader_command::kGroupSelection);
  if (result.ok && result.changed && binding.current()) {
    (void)select_current_scene_selection(editor, document);
  }
}

void ungroup_selection(QuaderEditorApi &editor, const SceneDocument &document,
                       OutlinerRuntimeBindingToken binding) {
  const std::vector<EntityId> roots =
      document_command_selected_root_entities(document);
  const bool has_group = std::ranges::any_of(roots, [&](EntityId entity) {
    return document.kind(entity) == EntityKind::Empty &&
           document.has_children(entity);
  });
  if (!has_group) {
    return;
  }
  const QuaderApiVoidResult result =
      editor.commands().execute(quader_command::kUngroupSelection);
  if (result.ok && result.changed && binding.current()) {
    (void)select_current_scene_selection(editor, document);
  }
}

void delete_selection(QuaderEditorApi &editor, const SceneDocument &document,
                      OutlinerRuntimeBindingToken binding) {
  const std::vector<EntityId> roots =
      document_command_selected_root_entities(document);
  if (roots.empty()) {
    return;
  }

  const QuaderSelectionMode previous_mode = editor.selection().summary().mode;
  if (previous_mode != QuaderSelectionMode::Object &&
      previous_mode != QuaderSelectionMode::Group) {
    (void)editor.selection().set_mode(QuaderSelectionMode::Object);
    if (!binding.current()) {
      return;
    }
  }
  (void)editor.commands().execute(quader_command::kDeleteSelection);
}

void duplicate_selection(QuaderEditorApi &editor) {
  (void)editor.commands().execute(quader_command::kDuplicateSelection);
}

/// Keeps drag identity inside the originating panel and runtime binding.
class OutlinerMimeData final : public QMimeData {
public:
  QPointer<QAbstractItemModel> source;
  std::uint64_t binding_generation = 0;
  std::uint64_t hierarchy_revision = 0;
  EntityId instance_scope = kInvalidEntityId;
  std::vector<EntityId> entities;

  static QString format() {
    return QStringLiteral("application/x-quader-outliner-entities");
  }
};

/// Adapts Qt drops to scene intent without permitting model-owned row moves.
class OutlinerItemModel final : public QStandardItemModel {
public:
  using MimeHandler = std::function<QMimeData *(const QModelIndexList &)>;
  using DropHandler = std::function<bool(const OutlinerMimeData &, int,
                                         const QModelIndex &, bool)>;

  explicit OutlinerItemModel(QObject *parent) : QStandardItemModel(parent) {}

  MimeHandler mime_handler;
  DropHandler drop_handler;

  QStringList mimeTypes() const override { return {OutlinerMimeData::format()}; }

  QMimeData *mimeData(const QModelIndexList &indexes) const override {
    return mime_handler != nullptr ? mime_handler(indexes) : nullptr;
  }

  Qt::DropActions supportedDropActions() const override { return Qt::MoveAction; }
  Qt::DropActions supportedDragActions() const override { return Qt::MoveAction; }

  bool removeRows(int, int, const QModelIndex & = {}) override {
    // Qt removes source rows after a successful Move drop. The scene API has
    // already moved them atomically; only its refreshed projection may remove
    // rows from this retained model.
    return false;
  }

  void clear_projection() {
    QStandardItemModel::removeRows(0, rowCount());
  }

  bool canDropMimeData(const QMimeData *data, Qt::DropAction action, int row,
                       int column, const QModelIndex &parent) const override {
    const auto *drag = dynamic_cast<const OutlinerMimeData *>(data);
    return action == Qt::MoveAction && column <= 0 && drag != nullptr &&
           drag->source == this && drop_handler != nullptr &&
           drop_handler(*drag, row, parent, false);
  }

  bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row,
                    int column, const QModelIndex &parent) override {
    if (!canDropMimeData(data, action, row, column, parent)) {
      return false;
    }
    return drop_handler(*static_cast<const OutlinerMimeData *>(data), row,
                         parent, true);
  }
};

/// Renders hidden and unselectable entity rows with subdued stock palette text.
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

protected:
  void initStyleOption(QStyleOptionViewItem *option,
                       const QModelIndex &index) const override {
    QStyledItemDelegate::initStyleOption(option, index);
    if (index.data(kActiveEntityRole).toBool() &&
        !option->state.testFlag(QStyle::State_Selected)) {
      option->backgroundBrush = option->palette.brush(QPalette::AlternateBase);
    }
  }
};

/// Coalesces mouse selection into one owner transaction, normalizes Qt's
/// private range anchor after external selection changes, and adds keyboard
/// context targeting.
class OutlinerTreeView final : public QTreeView {
public:
  using ContextMenuHandler = std::function<void(EntityId, QPoint)>;
  using SelectionGestureFinishedHandler = std::function<void()>;
  enum class DropHint { None, Parent, Reorder, Root, InstanceScope };

  explicit OutlinerTreeView(QWidget *parent = nullptr) : QTreeView(parent) {
    install_qt_tree_relationship_presentation(*this);
  }

  ContextMenuHandler context_menu_handler;
  SelectionGestureFinishedHandler selection_gesture_finished_handler;
  std::uint64_t hint_revision = 0;

  [[nodiscard]] bool hint_drag_active() const noexcept {
    return hint_source_drag_ || hint_drag_hovering_;
  }

  void publish_drop_hint(DropHint kind, std::string_view target = {}) {
    hint_validation_seen_ = true;
    if (!hint_drag_active()) {
      return;
    }
    if (hint_kind_ != kind || hint_target_ != target) {
      hint_kind_ = kind;
      hint_target_ = target;
      ++hint_revision;
    }
  }

  void invalidate_drop_hint() { clear_drop_hint(); }

  [[nodiscard]] std::vector<QuaderOperationInputHint> input_hints() const {
    if (!hint_drag_active() || hint_kind_ == DropHint::None) {
      return {};
    }
    QuaderOperationInputHint hint{
        .gesture = QuaderOperationInputGesture::PrimaryDrag,
        .priority = 100,
        .active = true,
    };
    switch (hint_kind_) {
    case DropHint::Parent:
      hint.label_key = "operation.hints.outliner.parent_to";
      hint.label_fallback = "Parent to";
      hint.detail = hint_target_;
      break;
    case DropHint::Reorder:
      hint.label_key = "operation.hints.outliner.reorder";
      hint.label_fallback = "Reorder";
      break;
    case DropHint::Root:
      hint.label_key = "operation.hints.outliner.move_root";
      hint.label_fallback = "Move to scene root";
      break;
    case DropHint::InstanceScope:
      hint.label_key = "operation.hints.outliner.move_scope";
      hint.label_fallback = "Move to current instance scope";
      break;
    case DropHint::None:
      break;
    }
    return {std::move(hint)};
  }

  [[nodiscard]] bool selection_gesture_active() const noexcept {
    return selection_gesture_active_;
  }

  void abandon_selection_gesture() noexcept {
    ++interaction_generation_;
    selection_gesture_active_ = false;
    resetting_selection_anchor_ = false;
    clear_drag_hints();
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
  void startDrag(Qt::DropActions supported_actions) override {
    const std::uint64_t generation = interaction_generation_;
    // A drag can start before mouse release; publish its row selection once
    // before entering QDrag's nested event loop.
    finish_selection_gesture();
    if (interaction_generation_ != generation) {
      return;
    }
    hint_source_drag_ = true;
    ++hint_revision;
    QPointer<OutlinerTreeView> guard(this);
    QTreeView::startDrag(supported_actions);
    if (guard) {
      clear_drag_hints();
    }
  }

  void dragEnterEvent(QDragEnterEvent *event) override {
    const auto *data = dynamic_cast<const OutlinerMimeData *>(event->mimeData());
    hint_drag_hovering_ = data != nullptr && data->source == model();
    ++hint_revision;
    QTreeView::dragEnterEvent(event);
  }

  void dragMoveEvent(QDragMoveEvent *event) override {
    hint_validation_seen_ = false;
    QTreeView::dragMoveEvent(event);
    if (!hint_validation_seen_ || !event->isAccepted()) {
      clear_drop_hint();
    }
  }

  void dragLeaveEvent(QDragLeaveEvent *event) override {
    hint_drag_hovering_ = false;
    clear_drop_hint();
    ++hint_revision;
    QTreeView::dragLeaveEvent(event);
  }

  void dropEvent(QDropEvent *event) override {
    clear_drag_hints();
    QTreeView::dropEvent(event);
  }

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
      clear_drag_hints();
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
  void clear_drop_hint() {
    if (hint_kind_ != DropHint::None || !hint_target_.empty()) {
      hint_kind_ = DropHint::None;
      hint_target_.clear();
      ++hint_revision;
    }
  }

  void clear_drag_hints() noexcept {
    if (hint_drag_active()) {
      hint_source_drag_ = false;
      hint_drag_hovering_ = false;
      ++hint_revision;
    }
    clear_drop_hint();
  }

  DropHint hint_kind_ = DropHint::None;
  std::string hint_target_;
  bool hint_source_drag_ = false;
  bool hint_drag_hovering_ = false;
  bool hint_validation_seen_ = false;

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
  std::uint64_t interaction_generation_ = 0;
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
    tree_->setIconSize(QSize(16, 16));
    tree_->setDragDropMode(QAbstractItemView::InternalMove);
    tree_->setDefaultDropAction(Qt::MoveAction);
    tree_->setDragDropOverwriteMode(false);
    tree_->setDropIndicatorShown(true);
    tree_->setAutoExpandDelay(600);
    tree_->setItemDelegate(new SubduedRowDelegate(tree_));
    panel_.setFocusProxy(tree_);

    model_ = new OutlinerItemModel(tree_);
    model_->setObjectName(QStringLiteral("outliner.model"));
    model_->setColumnCount(column_index(OutlinerColumn::Count));
    tree_->setModel(model_);
    refresh_headers();
    tree_->setHeaderHidden(true);

    QHeaderView *header = tree_->header();
    header->setSectionsMovable(false);
    header->setStretchLastSection(false);
    header->setSectionResizeMode(column_index(OutlinerColumn::Name),
                                 QHeaderView::Stretch);
    layout->addWidget(tree_);

    qt_register_contextual_input_hints(
        panel_, *tree_,
        [tree = tree_](Qt::KeyboardModifiers) {
          return QtContextualInputHintSourceState{
              .revision = tree->hint_revision,
              .active = tree->hint_drag_active(),
          };
        },
        [tree = tree_](Qt::KeyboardModifiers) { return tree->input_hints(); });

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
    model_->mime_handler = [this](const QModelIndexList &indexes) {
      return create_drag_data(indexes);
    };
    model_->drop_handler = [this](const OutlinerMimeData &drag, int row,
                                   const QModelIndex &parent, bool commit) {
      const bool accepted = handle_drop(drag, row, parent, commit);
      if (!commit && !accepted) {
        tree_->invalidate_drop_hint();
      }
      return accepted;
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

  void apply_ui_scale(const QtUiScaleMetrics &metrics) {
    apply_ui_scale_to_qt_tree_relationship_presentation(*tree_, metrics);
    const QSize icon_size = metrics.size(QSize(16, 16));
    if (tree_->iconSize() != icon_size) {
      tree_->setIconSize(icon_size);
    }
  }

  void abandon_runtime() noexcept {
    ++runtime_binding_generation_;
    editor_ = nullptr;
    document_ = nullptr;
    localization_ = nullptr;
    instance_scope_definition_ = kInvalidEntityId;
    cache_ = {};
    item_by_entity_.clear();
    light_items_.clear();
    collapsed_entities_.clear();
    synced_selection_revision_ = 0;
    presented_mode_revision_.reset();
    presented_active_entity_ = kInvalidEntityId;
    syncing_expansion_ = false;
    syncing_model_ = false;
    syncing_selection_ = false;
    pending_widget_selection_sync_ = false;
    widget_selection_sync_deferred_ = false;
    selection_revision_valid_ = false;
    selection_sync_after_model_rebuild_ = true;
    tree_->abandon_selection_gesture();
  }

  void refresh(UIContext &context) {
    if ((editor_ != nullptr && editor_ != &context.intent.editor) ||
        (document_ != nullptr && document_ != &context.scene.document)) {
      abandon_runtime();
    }
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
    refresh_light_icons_if_needed();
    sync_active_entity();
    sync_selection();
  }

private:
  void sync_active_entity() {
    const std::uint64_t revision = editor_->mode().revision();
    if (presented_mode_revision_ == revision &&
        !selection_sync_after_model_rebuild_) {
      return;
    }
    const EntityId active = editor_->mode().state().active_entity.id;
    const QScopedValueRollback<bool> model_guard(syncing_model_, true);
    const auto set_active = [&](EntityId entity, bool value) {
      const auto found = item_by_entity_.find(entity);
      if (found != item_by_entity_.end() &&
          found->second->data(kActiveEntityRole).toBool() != value) {
        found->second->setData(value, kActiveEntityRole);
      }
    };
    if (presented_active_entity_ != active) {
      set_active(presented_active_entity_, false);
    }
    set_active(active, true);
    presented_active_entity_ = active;
    presented_mode_revision_ = revision;
  }

  const QIcon &light_icon(EntityId entity) const {
    const LightComponent *light = document_->light(entity);
    const OutlinerIcon type =
        light != nullptr && light->type == LightType::Directional
            ? OutlinerIcon::DirectionalLight
            : OutlinerIcon::PointLight;
    return icons_[static_cast<std::size_t>(type)];
  }

  const QIcon &entity_icon(const EntityRecord &entity) const {
    switch (entity.kind) {
    case EntityKind::Mesh:
    case EntityKind::Instance:
      return icons_[static_cast<std::size_t>(OutlinerIcon::Mesh)];
    case EntityKind::Curve:
      return icons_[static_cast<std::size_t>(OutlinerIcon::Curve)];
    case EntityKind::Light:
      return light_icon(entity.id);
    case EntityKind::Empty:
      return icons_[static_cast<std::size_t>(OutlinerIcon::Group)];
    case EntityKind::Generic:
      return empty_icon_;
    }
    return empty_icon_;
  }

  void refresh_light_icons_if_needed() {
    if (document_ == nullptr ||
        light_property_revision_ == document_->light_property_revision()) {
      return;
    }
    for (QStandardItem *item : light_items_) {
      const QIcon &icon = light_icon(entity_id(item->index()));
      if (item->icon().cacheKey() != icon.cacheKey()) {
        item->setIcon(icon);
      }
    }
    light_property_revision_ = document_->light_property_revision();
  }

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
    tree_->invalidate_drop_hint();

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
      model_->clear_projection();
      item_by_entity_.clear();
      light_items_.clear();
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
          name_item->setIcon(entity_icon(*entity_record));
          if (entity_record->kind == EntityKind::Light) {
            light_items_.push_back(name_item);
          }
          name_item->setEditable(true);
          name_item->setDragEnabled(entity_record->effectively_selectable);
          name_item->setDropEnabled(entity_record->effectively_selectable &&
                                    !entity_record->has_instance_occurrence);
          name_item->setData(static_cast<qulonglong>(entity_record->id),
                             kEntityIdRole);
          name_item->setData(!entity_record->effectively_visible ||
                                 !entity_record->effectively_selectable,
                             kSubduedRole);

          if (parent != nullptr) {
            parent->appendRow(name_item);
          } else {
            model_->appendRow(name_item);
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
      light_property_revision_ = document_->light_property_revision();
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
    const std::uint64_t scheduled_generation = runtime_binding_generation_;
    QMetaObject::invokeMethod(
        &panel_,
        [this, scheduled_generation]() {
          if (runtime_binding_generation_ != scheduled_generation) {
            return;
          }
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

    const std::uint64_t mutation_generation = runtime_binding_generation_;
    const QuaderApiVoidResult result =
        editor_->selection().select_objects_for_inspection(
            std::span<const EntityId>(selected.data(), selected.size()),
            active);
    if (runtime_binding_generation_ != mutation_generation) {
      return;
    }
    if (!result.ok) {
      selection_sync_after_model_rebuild_ = true;
      sync_selection();
      return;
    }

    synced_selection_revision_ = document_->selection_revision();
    selection_revision_valid_ = true;
    selection_sync_after_model_rebuild_ = false;
  }

  QMimeData *create_drag_data(const QModelIndexList &indexes) const {
    if (editor_ == nullptr || document_ == nullptr || !cache_.valid ||
        cache_.hierarchy_presentation_revision !=
            document_->hierarchy_presentation_revision()) {
      return nullptr;
    }
    auto data = std::make_unique<OutlinerMimeData>();
    for (const QModelIndex &index : indexes) {
      const EntityRecord *entity_record = record(entity_id(index));
      if (index.model() != model_ || entity_record == nullptr ||
          !entity_record->effectively_selectable) {
        return nullptr;
      }
      data->entities.push_back(entity_record->id);
    }
    if (data->entities.empty()) {
      return nullptr;
    }
    data->source = model_;
    data->binding_generation = runtime_binding_generation_;
    data->hierarchy_revision = cache_.hierarchy_presentation_revision;
    data->instance_scope = instance_scope_definition_;
    data->setData(OutlinerMimeData::format(), QByteArray{});
    return data.release();
  }

  bool handle_drop(const OutlinerMimeData &drag, int row,
                    const QModelIndex &parent, bool commit) {
    if (editor_ == nullptr || document_ == nullptr || !cache_.valid ||
        drag.binding_generation != runtime_binding_generation_ ||
        drag.hierarchy_revision != document_->hierarchy_presentation_revision() ||
        drag.instance_scope != instance_scope_definition_ ||
        (parent.isValid() && parent.model() != model_) ||
        row < -1 || row > model_->rowCount(parent)) {
      return false;
    }
    const EntityId destination =
        parent.isValid() ? entity_id(parent)
                        : instance_scope_definition_;
    const EntityRecord *destination_record = record(destination);
    if ((destination_record == nullptr &&
         destination != instance_scope_definition_) ||
        (destination_record != nullptr &&
         (!destination_record->effectively_selectable ||
          destination_record->has_instance_occurrence))) {
      return false;
    }
    const std::unordered_set<EntityId> dragged(drag.entities.begin(),
                                              drag.entities.end());
    for (const EntityId entity : drag.entities) {
      bool dragged_ancestor = false;
      for (EntityId ancestor = document_->parent(entity);
           ancestor != kInvalidEntityId; ancestor = document_->parent(ancestor)) {
        if (dragged.contains(ancestor)) {
          dragged_ancestor = true;
          break;
        }
      }
      if (dragged_ancestor) {
        continue;
      }
      if (!document_->can_set_parent(entity, destination)) {
        return false;
      }
    }
    const EntityId before_sibling =
        row >= 0 && row < model_->rowCount(parent)
            ? entity_id(model_->index(row, 0, parent))
            : kInvalidEntityId;
    if (!commit) {
      const OutlinerTreeView::DropHint hint =
          row >= 0 ? OutlinerTreeView::DropHint::Reorder
          : parent.isValid() ? OutlinerTreeView::DropHint::Parent
          : instance_scope_definition_ != kInvalidEntityId
              ? OutlinerTreeView::DropHint::InstanceScope
              : OutlinerTreeView::DropHint::Root;
      tree_->publish_drop_hint(hint, destination_record != nullptr
                                       ? std::string_view(destination_record->name)
                                       : std::string_view{});
      return true;
    }
    const std::uint64_t scheduled_generation = runtime_binding_generation_;
    const QuaderApiVoidResult result = editor_->scene().move_entities(
        drag.entities, destination, before_sibling);
    if (runtime_binding_generation_ != scheduled_generation) {
      return result.ok();
    }
    if (!result.ok()) {
      return false;
    }
    if (row == -1) {
      collapsed_entities_.erase(destination);
    }
    QMetaObject::invokeMethod(
        &panel_,
        [this, scheduled_generation]() {
          if (runtime_binding_generation_ != scheduled_generation) {
            return;
          }
          refresh_model_if_needed();
          sync_selection();
        },
        Qt::QueuedConnection);
    return true;
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

    const std::string next_name = item->text().toUtf8().toStdString();
    if (next_name == entity_record->name) {
      return;
    }

    const std::uint64_t mutation_generation = runtime_binding_generation_;
    const QuaderApiVoidResult result =
        editor_->scene().rename(entity, next_name);
    if (runtime_binding_generation_ != mutation_generation) {
      return;
    }
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
      const std::vector<EntityId> selected = document_->selected_entities();
      if (!contains_entity(selected, entity) ||
          document_->primary_selection() != entity) {
        const std::uint64_t selection_generation =
            runtime_binding_generation_;
        (void)select_only(*editor_,
                          contains_entity(selected, entity)
                              ? selected
                              : std::vector<EntityId>{entity},
                          entity);
        if (runtime_binding_generation_ != selection_generation) {
          return;
        }
      }
      sync_selection();
    }
    const EntityRecord *entity_record = record(entity);
    if (entity_record == nullptr) {
      return;
    }

    const std::vector<EntityId> selected_entities =
        document_->selected_entities();
    const std::uint64_t menu_selection_revision =
        document_->selection_revision();
    std::vector<EntityId> selected_objects;
    selected_objects.reserve(selected_entities.size());
    bool all_selected_visible = true;
    bool any_selected_selectable = false;
    for (const EntityId selected : selected_entities) {
      const EntityRecord *selected_record = record(selected);
      if (selected_record == nullptr) {
        continue;
      }
      selected_objects.push_back(selected);
      all_selected_visible =
          all_selected_visible && selected_record->visible;
      any_selected_selectable =
          any_selected_selectable || selected_record->selectable;
    }

    const std::vector<EntityId> mutable_roots =
        document_command_selected_root_entities(*document_);
    const bool has_mutable_roots = !mutable_roots.empty();
    const bool can_ungroup = std::ranges::any_of(
        mutable_roots, [this](EntityId selected) {
          const EntityRecord *selected_record = record(selected);
          return selected_record != nullptr &&
                 selected_record->kind == EntityKind::Empty &&
                 selected_record->has_children;
        });

    QMenu menu(tree_);
    QAction *rename_action = nullptr;
    QAction *focus_action = nullptr;
    QAction *visibility_action = nullptr;
    QAction *selectability_action = nullptr;
    QAction *delete_action = nullptr;
    QAction *duplicate_action = nullptr;
    QAction *group_action = nullptr;
    QAction *ungroup_action = nullptr;
    QAction *parent_action = nullptr;
    QAction *unparent_action = nullptr;
    {
      QDR_PROFILE_SCOPE("Qt outliner context menu build");
      menu.setObjectName(QStringLiteral("outliner.context_menu"));
      menu.setToolTipsVisible(true);

      const auto add_command_action =
          [this, &menu](std::string_view localization_key,
                        std::string_view fallback, QuaderCommandId command_id,
                        std::string_view object_name, bool context_enabled,
                        bool respect_command_availability = true) {
            const std::optional<QuaderCommandDescriptor> descriptor =
                editor_->commands().descriptor(command_id);
            const std::string shortcut =
                descriptor.has_value()
                    ? descriptor->shortcut
                    : editor_->commands().shortcut_for(command_id);
            QAction *action = menu.addAction(with_shortcut(
                localized(localization_key, fallback), shortcut));
            action->setObjectName(qt_text(object_name));
            action->setEnabled(
                context_enabled && descriptor.has_value() &&
                (!respect_command_availability || descriptor->enabled));
            return action;
          };

      rename_action =
          menu.addAction(localized("ui.outliner.rename", "Rename"));
      rename_action->setObjectName(QStringLiteral("outliner.action.rename"));

      focus_action = add_command_action(
          "ui.outliner.focus_selection", "Focus Selection",
          quader_command::kViewportFrameSelection,
          "outliner.action.focus_selection", !selected_objects.empty());
      menu.addSeparator();

      const bool show_selected = !all_selected_visible;
      if (show_selected) {
        visibility_action = menu.addAction(localized(
            "ui.outliner.show_selected", "Show Objects"));
        visibility_action->setObjectName(
            QStringLiteral("outliner.action.show_selected"));
        visibility_action->setEnabled(!selected_objects.empty());
      } else {
        visibility_action = add_command_action(
            "ui.outliner.hide_selected", "Hide Objects",
            quader_command::kSceneHideSelection,
            "outliner.action.hide_selected", !selected_objects.empty());
      }

      const bool select_selected = !any_selected_selectable;
      selectability_action = menu.addAction(localized(
          select_selected ? "ui.outliner.make_selectable"
                        : "ui.outliner.make_unselectable",
          select_selected ? "Make Selectable" : "Make Unselectable"));
      selectability_action->setObjectName(
          select_selected ? QStringLiteral("outliner.action.make_selectable")
                        : QStringLiteral("outliner.action.make_unselectable"));
      selectability_action->setEnabled(!selected_objects.empty());
      menu.addSeparator();

      duplicate_action = add_command_action(
          "ui.outliner.duplicate", "Duplicate Objects",
          quader_command::kDuplicateSelection, "outliner.action.duplicate",
          has_mutable_roots);

      group_action = add_command_action(
          "ui.outliner.group", "Group Objects",
          quader_command::kGroupSelection, "outliner.action.group",
          has_mutable_roots);

      ungroup_action = add_command_action(
          "ui.outliner.ungroup", "Ungroup Groups",
          quader_command::kUngroupSelection, "outliner.action.ungroup",
          can_ungroup);
      parent_action = add_command_action(
          "ui.menu.edit.parent_to_active", "Parent to Active",
          quader_command::kParentToActive, "outliner.action.parent_to_active",
          has_mutable_roots && selected_objects.size() > 1u);
      unparent_action = add_command_action(
          "ui.menu.edit.unparent", "Unparent",
          quader_command::kUnparentSelection, "outliner.action.unparent",
          has_mutable_roots);
      menu.addSeparator();

      delete_action = add_command_action(
          "ui.outliner.delete", "Delete Objects",
          quader_command::kDeleteSelection, "outliner.action.delete",
          has_mutable_roots, false);
      hide_unavailable_context_menu_actions(menu);
    }

    const std::uint64_t menu_generation = runtime_binding_generation_;
    QAction *chosen = nullptr;
    {
      QDR_PROFILE_SCOPE("Qt outliner context menu exec");
      chosen = menu.exec(global_position);
    }
    if (runtime_binding_generation_ != menu_generation) {
      return;
    }
    if (document_->selection_revision() != menu_selection_revision) {
      refresh_model_if_needed();
      sync_selection();
      return;
    }
    const OutlinerRuntimeBindingToken binding{
        .generation = &runtime_binding_generation_,
        .expected = menu_generation,
    };
    {
      QDR_PROFILE_SCOPE("Qt outliner context menu dispatch action");
      if (chosen == rename_action) {
        begin_rename(entity);
        return;
      } else if (chosen == focus_action) {
        (void)editor_->commands().execute(
            quader_command::kViewportFrameSelection);
      } else if (chosen == visibility_action) {
        if (all_selected_visible) {
          (void)editor_->commands().execute(
              quader_command::kSceneHideSelection);
        } else {
          std::vector<QuaderSceneVisibilityEdit> edits;
          edits.reserve(selected_objects.size());
          for (const EntityId selected : selected_objects) {
            const VisibilityComponent *visibility =
                document_->visibility(selected);
            if (visibility != nullptr && !visibility->visible) {
              edits.push_back({
                  .entity = selected,
                  .before = false,
                  .after = true,
              });
            }
          }
          (void)editor_->scene().set_visibility(edits);
        }
      } else if (chosen == selectability_action) {
        std::vector<QuaderSceneSelectabilityEdit> edits;
        edits.reserve(selected_objects.size());
        for (const EntityId selected : selected_objects) {
          const SelectableComponent *selectable = document_->selectable(selected);
          if (selectable != nullptr &&
              selectable->selectable != !any_selected_selectable) {
            edits.push_back({
                .entity = selected,
                .before = selectable->selectable,
                .after = !any_selected_selectable,
            });
          }
        }
        (void)editor_->scene().set_selectability(edits);
      } else if (chosen == delete_action) {
        delete_selection(*editor_, *document_, binding);
      } else if (chosen == duplicate_action) {
        duplicate_selection(*editor_);
      } else if (chosen == group_action) {
        group_selection(*editor_, *document_, binding);
      } else if (chosen == ungroup_action) {
        ungroup_selection(*editor_, *document_, binding);
      } else if (chosen == parent_action) {
        (void)editor_->commands().execute(quader_command::kParentToActive);
      } else if (chosen == unparent_action) {
        (void)editor_->commands().execute(quader_command::kUnparentSelection);
      }
    }

    if (!binding.current()) {
      return;
    }

    {
      QDR_PROFILE_SCOPE("Qt outliner context menu synchronize result");
      refresh_model_if_needed();
      sync_selection();
    }
  }

  OutlinerPanel &panel_;
  const OutlinerIconCache icons_ = load_outliner_icons();
  const QIcon empty_icon_;
  OutlinerTreeView *tree_ = nullptr;
  OutlinerItemModel *model_ = nullptr;
  QuaderEditorApi *editor_ = nullptr;
  const SceneDocument *document_ = nullptr;
  LocalizationService *localization_ = nullptr;
  std::string localized_locale_;
  std::uint64_t localized_revision_ = 0u;
  TreeCache cache_;
  std::unordered_map<EntityId, QStandardItem *> item_by_entity_;
  std::vector<QStandardItem *> light_items_;
  std::unordered_set<EntityId> collapsed_entities_;
  EntityId instance_scope_definition_ = kInvalidEntityId;
  std::uint64_t synced_selection_revision_ = 0;
  std::optional<std::uint64_t> presented_mode_revision_;
  EntityId presented_active_entity_ = kInvalidEntityId;
  std::uint64_t light_property_revision_ = 0;
  std::uint64_t runtime_binding_generation_ = 0;
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

void OutlinerPanel::abandon_runtime() noexcept { impl_->abandon_runtime(); }

void OutlinerPanel::apply_ui_scale(const QtUiScaleMetrics &metrics) {
  impl_->apply_ui_scale(metrics);
}

void OutlinerPanel::refresh(UIContext &context) { impl_->refresh(context); }

} // namespace quader
