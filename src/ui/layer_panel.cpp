////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/layer_panel.hpp"

#include "api/quader_editor_api.hpp"
#include "core/localization_service.hpp"
#include "scene/scene_document.hpp"
#include "ui/qt_color_swatch_button.hpp"
#include "ui/ui_context.hpp"
#include "ui/ui_panel.hpp"

#include <QAbstractItemView>
#include <QAction>
#include <QColorDialog>
#include <QContextMenuEvent>
#include <QDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSize>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>
#include <QVariant>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace quader {
namespace {

constexpr int kLayerIdRole = Qt::UserRole + 1;

/// Fixed columns used by the retained layer-list presentation model.
enum class LayerColumn : int {
  Color = 0,
  Name = 1,
  Visible = 2,
  Locked = 3,
  Count = 4,
};

constexpr int column_index(LayerColumn column) {
  return static_cast<int>(column);
}

QString qt_text(std::string_view text) {
  return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

QuaderLayerId layer_id(const QModelIndex &index) {
  if (!index.isValid()) {
    return kInvalidQuaderLayerId;
  }
  return static_cast<QuaderLayerId>(index.data(kLayerIdRole).toULongLong());
}

QColor qt_color(QVec3 color) {
  return QColor::fromRgbF(std::clamp(color.x, 0.0f, 1.0f),
                          std::clamp(color.y, 0.0f, 1.0f),
                          std::clamp(color.z, 0.0f, 1.0f));
}

QVec3 layer_color(const QColor &color) {
  return {static_cast<float>(color.redF()),
          static_cast<float>(color.greenF()),
          static_cast<float>(color.blueF())};
}

bool same_color(const QColor &left, const QColor &right) {
  return left.toRgb().rgba() == right.toRgb().rgba();
}

QIcon palette_icon(const QString &resource, const QPalette &palette) {
  QPixmap pixmap(resource);
  if (pixmap.isNull()) {
    return QIcon(resource);
  }
  QPainter painter(&pixmap);
  painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
  painter.fillRect(pixmap.rect(), palette.color(QPalette::Text));
  painter.end();
  return QIcon(pixmap);
}

QIcon layer_visibility_icon(bool visible, const QPalette &palette) {
  return palette_icon(
      visible ? QStringLiteral(":/quader/icons/material_visibility.png")
              : QStringLiteral(
                    ":/quader/icons/material_visibility_off.png"),
      palette);
}

QIcon layer_lock_icon(bool locked, const QPalette &palette) {
  return palette_icon(
      locked ? QStringLiteral(":/quader/icons/material_lock.png")
             : QStringLiteral(":/quader/icons/material_lock_open.png"),
      palette);
}

/// Restricts in-place edits to layer-name constraints owned by the public API.
class LayerNameDelegate final : public QStyledItemDelegate {
public:
  explicit LayerNameDelegate(QObject *parent = nullptr)
      : QStyledItemDelegate(parent) {}

  QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                        const QModelIndex &index) const override {
    QWidget *editor = QStyledItemDelegate::createEditor(parent, option, index);
    if (index.column() == column_index(LayerColumn::Name)) {
      if (auto *line_edit = qobject_cast<QLineEdit *>(editor)) {
        line_edit->setMaxLength(127);
        line_edit->selectAll();
      }
    }
    return editor;
  }
};

/// Adds only keyboard context targeting.
class LayerTreeView final : public QTreeView {
public:
  using ContextMenuHandler = std::function<void(QuaderLayerId, QPoint)>;

  explicit LayerTreeView(QWidget *parent = nullptr) : QTreeView(parent) {}

  ContextMenuHandler context_menu_handler;

  void clear_layer_selection() {
    if (selectionModel() == nullptr) {
      return;
    }
    selectionModel()->clearSelection();
    selectionModel()->setCurrentIndex(QModelIndex{},
                                      QItemSelectionModel::NoUpdate);
  }

protected:
  void contextMenuEvent(QContextMenuEvent *event) override {
    const QModelIndex index =
        event->reason() == QContextMenuEvent::Keyboard
            ? currentIndex()
            : indexAt(event->pos());
    if (!index.isValid() || context_menu_handler == nullptr) {
      QTreeView::contextMenuEvent(event);
      return;
    }
    if (event->reason() == QContextMenuEvent::Keyboard &&
        selectionModel() != nullptr &&
        !selectionModel()->isRowSelected(index.row(), index.parent())) {
      const QModelIndex name_index =
          index.siblingAtColumn(column_index(LayerColumn::Name));
      selectionModel()->select(name_index,
                               QItemSelectionModel::ClearAndSelect |
                                   QItemSelectionModel::Rows);
      selectionModel()->setCurrentIndex(name_index,
                                        QItemSelectionModel::NoUpdate);
    }
    const QPoint global_position =
        event->reason() == QContextMenuEvent::Keyboard
            ? viewport()->mapToGlobal(visualRect(index).center())
            : event->globalPos();
    context_menu_handler(layer_id(index), global_position);
    event->accept();
  }
};

} // namespace

/// Owns retained Qt controls and revision-gated layer snapshot presentation.
struct LayerPanel::Impl {
  explicit Impl(LayerPanel &panel) : panel_(panel) {
    panel_.setObjectName(
        QString::fromLatin1(ui_panel_stable_id(UIPanelId::Layers)));
    panel_.setWindowTitle(
        QString::fromLatin1(ui_panel_key(UIPanelId::Layers)));

    auto *layout = new QVBoxLayout(&panel_);
    layout->setObjectName(QStringLiteral("layers.layout"));
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    tree_ = new LayerTreeView(&panel_);
    tree_->setObjectName(QStringLiteral("layers.tree"));
    tree_->setAccessibleName(QStringLiteral("Layers"));
    tree_->setRootIsDecorated(false);
    tree_->setAlternatingRowColors(true);
    tree_->setUniformRowHeights(true);
    tree_->setIconSize(QSize(18, 18));
    tree_->setSelectionBehavior(QAbstractItemView::SelectRows);
    tree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tree_->setItemDelegate(new LayerNameDelegate(tree_));
    panel_.setFocusProxy(tree_);

    model_ = new QStandardItemModel(tree_);
    model_->setObjectName(QStringLiteral("layers.model"));
    model_->setColumnCount(column_index(LayerColumn::Count));
    tree_->setModel(model_);

    QHeaderView *header = tree_->header();
    header->setSectionsMovable(false);
    header->setStretchLastSection(false);
    header->setSectionResizeMode(column_index(LayerColumn::Color),
                                 QHeaderView::ResizeToContents);
    header->setSectionResizeMode(column_index(LayerColumn::Name),
                                 QHeaderView::Stretch);
    header->setSectionResizeMode(column_index(LayerColumn::Visible),
                                 QHeaderView::ResizeToContents);
    header->setSectionResizeMode(column_index(LayerColumn::Locked),
                                 QHeaderView::ResizeToContents);
    auto *actions = new QWidget(&panel_);
    actions->setObjectName(QStringLiteral("layers.actions"));
    auto *actions_layout = new QHBoxLayout(actions);
    actions_layout->setContentsMargins(4, 4, 4, 4);
    actions_layout->setSpacing(4);

    create_button_ = new QToolButton(actions);
    create_button_->setObjectName(QStringLiteral("layers.create"));
    create_button_->setIcon(
        QIcon(QStringLiteral(":/quader/icons/layer_add.png")));
    create_button_->setAccessibleName(QStringLiteral("Create Layer"));
    create_button_->setIconSize(QSize(20, 20));
    create_button_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    create_button_->setAutoRaise(false);
    create_button_->setFixedSize(28, 28);
    actions_layout->addWidget(create_button_);

    create_from_selection_button_ = new QToolButton(actions);
    create_from_selection_button_->setObjectName(
        QStringLiteral("layers.create_from_selection"));
    create_from_selection_button_->setIcon(QIcon(QStringLiteral(
        ":/quader/icons/layer_create_from_selection.png")));
    create_from_selection_button_->setAccessibleName(
        QStringLiteral("Create Layer from Selection"));
    create_from_selection_button_->setIconSize(QSize(20, 20));
    create_from_selection_button_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    create_from_selection_button_->setAutoRaise(false);
    create_from_selection_button_->setFixedSize(28, 28);
    create_from_selection_button_->setEnabled(false);
    actions_layout->addWidget(create_from_selection_button_);
    actions_layout->addStretch(1);
    layout->addWidget(actions);
    layout->addWidget(tree_);

    tree_->context_menu_handler = [this](QuaderLayerId id,
                                          QPoint global_position) {
      show_context_menu(id, global_position);
    };
    QObject::connect(model_, &QStandardItemModel::itemChanged, &panel_,
                     [this](QStandardItem *item) {
                       handle_item_changed(item);
                     });
    QObject::connect(create_button_, &QToolButton::clicked, &panel_, [this]() {
      create_layer(false);
    });
    QObject::connect(create_from_selection_button_, &QToolButton::clicked,
                     &panel_, [this]() { create_layer(true); });
  }

  void refresh(UIContext &context) {
    const bool owner_changed =
        editor_ != &context.intent.editor ||
        document_ != &context.scene.document;
    editor_ = &context.intent.editor;
    document_ = &context.scene.document;
    localization_ = &context.localization;
    if (owner_changed) {
      select_after_refresh_.reset();
      tree_->clear_layer_selection();
      synchronization_valid_ = false;
    }
    synchronize(owner_changed);
  }

private:
  QString localized(std::string_view key, std::string_view fallback) const {
    return localization_ != nullptr
               ? qt_text(localization_->text(key, fallback))
               : qt_text(fallback);
  }

  void refresh_labels() {
    panel_.setWindowTitle(localized(
        ui_panel_localization_key(UIPanelId::Layers),
        ui_panel_key(UIPanelId::Layers)));
    tree_->setAccessibleName(localized("ui.panel.layers", "Layers"));
    model_->setHeaderData(column_index(LayerColumn::Color), Qt::Horizontal,
                          localized("ui.layers.color", "Color"));
    model_->setHeaderData(column_index(LayerColumn::Name), Qt::Horizontal,
                          localized("ui.layers.name", "Name"));
    model_->setHeaderData(column_index(LayerColumn::Visible), Qt::Horizontal,
                          localized("ui.layers.visible", "Visible"));
    model_->setHeaderData(column_index(LayerColumn::Locked), Qt::Horizontal,
                          localized("ui.layers.locked", "Locked"));

    const QString create_text =
        localized("ui.layers.create", "Create Layer");
    create_button_->setToolTip(create_text);
    create_button_->setAccessibleName(create_text);
    const QString create_from_selection_text = localized(
        "ui.layers.create_from_selection", "Create Layer from Selection");
    create_from_selection_button_->setToolTip(create_from_selection_text);
    create_from_selection_button_->setAccessibleName(
        create_from_selection_text);
  }

  std::vector<QuaderLayerId> selected_layer_ids() const {
    std::vector<QuaderLayerId> ids;
    if (tree_->selectionModel() == nullptr) {
      return ids;
    }
    const QModelIndexList selected = tree_->selectionModel()->selectedRows(
        column_index(LayerColumn::Name));
    ids.reserve(static_cast<std::size_t>(selected.size()));
    for (const QModelIndex &index : selected) {
      const QuaderLayerId id = layer_id(index);
      if (id != kInvalidQuaderLayerId) {
        ids.push_back(id);
      }
    }
    return ids;
  }

  void synchronize(bool force) {
    if (editor_ == nullptr || document_ == nullptr) {
      return;
    }
    const std::uint64_t layer_revision = editor_->layers().revision();
    const std::uint64_t selection_revision = document_->selection_revision();
    const std::string locale = localization_ != nullptr
                                   ? localization_->current_locale()
                                   : std::string{};
    const bool rebuild_model =
        force || !synchronization_valid_ ||
        synchronized_layer_revision_ != layer_revision ||
        synchronized_locale_ != locale;
    const bool selection_changed =
        force || !synchronization_valid_ ||
        synchronized_selection_revision_ != selection_revision;
    if (!rebuild_model && !selection_changed) {
      return;
    }

    if (rebuild_model) {
      refresh_labels();
      rebuild(editor_->layers().all());
      synchronized_layer_revision_ = layer_revision;
      synchronized_locale_ = locale;
    }
    if (rebuild_model || selection_changed) {
      const bool enabled = editor_->layers().can_create_from_selection();
      if (create_from_selection_button_->isEnabled() != enabled) {
        create_from_selection_button_->setEnabled(enabled);
      }
      synchronized_selection_revision_ = selection_revision;
    }
    synchronization_valid_ = true;
  }

  void rebuild(const std::vector<QuaderLayerSnapshot> &layers) {
    std::vector<QuaderLayerId> selected = selected_layer_ids();
    if (select_after_refresh_.has_value()) {
      selected = {*select_after_refresh_};
    }
    const QuaderLayerId current =
        select_after_refresh_.has_value()
            ? *select_after_refresh_
            : layer_id(tree_->currentIndex());
    select_after_refresh_.reset();

    tree_->setUpdatesEnabled(false);
    syncing_model_ = true;
    model_->removeRows(0, model_->rowCount());
    layer_by_id_.clear();
    name_item_by_id_.clear();
    layer_by_id_.reserve(layers.size());
    name_item_by_id_.reserve(layers.size());

    for (const QuaderLayerSnapshot &layer : layers) {
      auto *color_item = new QStandardItem;
      color_item->setEditable(false);
      color_item->setData(static_cast<qulonglong>(layer.id), kLayerIdRole);

      auto *name_item = new QStandardItem(qt_text(layer.name));
      name_item->setEditable(true);
      name_item->setData(static_cast<qulonglong>(layer.id), kLayerIdRole);

      auto *visible_item = new QStandardItem;
      visible_item->setEditable(false);
      const QString visibility_text =
          layer.visible
              ? localized("ui.layers.visibility_visible", "Layer visible")
              : localized("ui.layers.visibility_hidden", "Layer hidden");
      visible_item->setToolTip(visibility_text);
      visible_item->setData(visibility_text, Qt::AccessibleTextRole);
      visible_item->setData(visibility_text,
                            Qt::AccessibleDescriptionRole);
      visible_item->setData(static_cast<qulonglong>(layer.id), kLayerIdRole);

      auto *locked_item = new QStandardItem;
      locked_item->setEditable(false);
      const QString locked_text =
          layer.locked
              ? localized("ui.layers.lock_state_locked", "Layer locked")
              : localized("ui.layers.lock_state_unlocked", "Layer unlocked");
      locked_item->setToolTip(locked_text);
      locked_item->setData(locked_text, Qt::AccessibleTextRole);
      locked_item->setData(locked_text, Qt::AccessibleDescriptionRole);
      locked_item->setData(static_cast<qulonglong>(layer.id), kLayerIdRole);

      model_->appendRow({color_item, name_item, visible_item, locked_item});
      layer_by_id_.emplace(layer.id, layer);
      name_item_by_id_.emplace(layer.id, name_item);

      auto *color_button = new QtColorSwatchButton(tree_);
      color_button->setObjectName(
          QStringLiteral("layers.color.%1")
              .arg(static_cast<qulonglong>(layer.id)));
      color_button->setFixedSize(22, 22);
      color_button->setAccessibleName(
          localized("ui.layers.color", "Color"));
      color_button->set_swatch_color(qt_color(layer.wireframe_color));
      tree_->setIndexWidget(color_item->index(), color_button);
      QObject::connect(color_button, &QPushButton::clicked, &panel_,
                       [this, id = layer.id, color_button]() {
                         choose_color(id, *color_button);
                       });

      auto *visibility_button = new QToolButton(tree_);
      visibility_button->setObjectName(
          QStringLiteral("layers.visibility.%1")
              .arg(static_cast<qulonglong>(layer.id)));
      visibility_button->setIcon(
          layer_visibility_icon(layer.visible, tree_->palette()));
      visibility_button->setIconSize(QSize(18, 18));
      visibility_button->setToolButtonStyle(Qt::ToolButtonIconOnly);
      visibility_button->setAutoRaise(true);
      visibility_button->setFixedSize(22, 22);
      visibility_button->setFocusPolicy(Qt::StrongFocus);
      visibility_button->setToolTip(visibility_text);
      visibility_button->setAccessibleName(visibility_text);
      visibility_button->setAccessibleDescription(visibility_text);
      tree_->setIndexWidget(visible_item->index(), visibility_button);
      QObject::connect(visibility_button, &QToolButton::clicked, &panel_,
                       [this, id = layer.id]() { toggle_visibility(id); });

      auto *lock_button = new QToolButton(tree_);
      lock_button->setObjectName(
          QStringLiteral("layers.lock.%1")
              .arg(static_cast<qulonglong>(layer.id)));
      lock_button->setIcon(layer_lock_icon(layer.locked, tree_->palette()));
      lock_button->setIconSize(QSize(18, 18));
      lock_button->setToolButtonStyle(Qt::ToolButtonIconOnly);
      lock_button->setAutoRaise(true);
      lock_button->setFixedSize(22, 22);
      lock_button->setFocusPolicy(Qt::StrongFocus);
      lock_button->setToolTip(locked_text);
      lock_button->setAccessibleName(locked_text);
      lock_button->setAccessibleDescription(locked_text);
      tree_->setIndexWidget(locked_item->index(), lock_button);
      QObject::connect(lock_button, &QToolButton::clicked, &panel_,
                       [this, id = layer.id]() { toggle_lock(id); });
    }

    syncing_model_ = false;
    if (tree_->selectionModel() != nullptr) {
      QItemSelection selection;
      for (const QuaderLayerId id : selected) {
        const auto item = name_item_by_id_.find(id);
        if (item != name_item_by_id_.end()) {
          selection.select(item->second->index(), item->second->index());
        }
      }
      tree_->selectionModel()->clearSelection();
      tree_->selectionModel()->select(selection,
                                      QItemSelectionModel::Select |
                                          QItemSelectionModel::Rows);
    }
    const auto current_item = name_item_by_id_.find(current);
    if (tree_->selectionModel() != nullptr) {
      tree_->selectionModel()->setCurrentIndex(
          current_item != name_item_by_id_.end()
              ? current_item->second->index()
              : QModelIndex{},
          QItemSelectionModel::NoUpdate);
    }
    tree_->setUpdatesEnabled(true);
  }

  void create_layer(bool from_selection) {
    if (editor_ == nullptr) {
      return;
    }
    const QuaderApiResult<QuaderLayerSnapshot> result =
        from_selection ? editor_->layers().create_from_selection()
                       : editor_->layers().create();
    if (!result.ok()) {
      return;
    }
    select_after_refresh_ = result.value().id;
    synchronize(true);
  }

  void toggle_visibility(QuaderLayerId id) {
    if (editor_ == nullptr) {
      return;
    }
    const auto found = layer_by_id_.find(id);
    if (found == layer_by_id_.end()) {
      return;
    }
    const QuaderApiVoidResult result =
        editor_->layers().set_visible(id, !found->second.visible);
    if (result.ok() && result.changed()) {
      synchronize(true);
    }
  }

  void toggle_lock(QuaderLayerId id) {
    if (editor_ == nullptr) {
      return;
    }
    const auto found = layer_by_id_.find(id);
    if (found == layer_by_id_.end()) {
      return;
    }
    const QuaderApiVoidResult result =
        editor_->layers().set_locked(id, !found->second.locked);
    if (result.ok() && result.changed()) {
      synchronize(true);
    }
  }

  void handle_item_changed(QStandardItem *item) {
    if (syncing_model_ || item == nullptr || editor_ == nullptr ||
        item->column() != column_index(LayerColumn::Name)) {
      return;
    }
    const QuaderLayerId id = layer_id(item->index());
    const auto found = layer_by_id_.find(id);
    if (found == layer_by_id_.end()) {
      return;
    }
    const std::string next_name = item->text().toUtf8().toStdString();
    if (next_name == found->second.name) {
      return;
    }
    const QuaderApiVoidResult result = editor_->layers().rename(id, next_name);
    if (!result.ok()) {
      const QSignalBlocker blocker(model_);
      item->setText(qt_text(found->second.name));
      return;
    }
    synchronize(true);
  }

  void choose_color(QuaderLayerId id, QtColorSwatchButton &button) {
    if (editor_ == nullptr) {
      return;
    }
    const auto found = layer_by_id_.find(id);
    if (found == layer_by_id_.end()) {
      return;
    }
    const QColor initial = qt_color(found->second.wireframe_color);
    QColorDialog dialog(initial, &panel_);
    dialog.setObjectName(QStringLiteral("layers.color.dialog"));
    dialog.setWindowTitle(
        localized("ui.layers.choose_color", "Choose Layer Color"));
    dialog.setOption(QColorDialog::DontUseNativeDialog, true);

    QPointer<QtColorSwatchButton> guarded_button(&button);
    QObject::connect(&dialog, &QColorDialog::currentColorChanged, &dialog,
                     [guarded_button](const QColor &color) {
                       if (guarded_button != nullptr && color.isValid()) {
                         guarded_button->set_swatch_color(color);
                       }
                     });

    const int dialog_result = dialog.exec();
    const QColor committed = dialog.selectedColor();
    if (dialog_result == QDialog::Accepted && committed.isValid() &&
        !same_color(committed, initial)) {
      const QuaderApiVoidResult result =
          editor_->layers().set_wireframe_color(id, layer_color(committed));
      if (result.ok()) {
        synchronize(true);
        return;
      }
    }
    if (guarded_button != nullptr) {
      guarded_button->set_swatch_color(initial);
    }
  }

  void show_context_menu(QuaderLayerId id, QPoint global_position) {
    if (editor_ == nullptr || !layer_by_id_.contains(id)) {
      return;
    }
    const auto name_item = name_item_by_id_.find(id);
    if (name_item != name_item_by_id_.end() &&
        tree_->selectionModel() != nullptr) {
      tree_->selectionModel()->setCurrentIndex(
          name_item->second->index(), QItemSelectionModel::NoUpdate);
    }

    QMenu menu(tree_);
    menu.setObjectName(QStringLiteral("layers.context_menu"));
    QAction *add_selection_action = menu.addAction(localized(
        "ui.layers.add_selection", "Add Selection to Layer"));
    add_selection_action->setObjectName(
        QStringLiteral("layers.action.add_selection"));
    add_selection_action->setEnabled(
        editor_->layers().can_add_selection_to(id));

    QAction *remove_selection_action = menu.addAction(localized(
        "ui.layers.remove_selection", "Remove Object from Layer"));
    remove_selection_action->setObjectName(
        QStringLiteral("layers.action.remove_selection"));
    remove_selection_action->setEnabled(
        editor_->layers().can_remove_selection(id));
    menu.addSeparator();

    QAction *empty_action =
        menu.addAction(localized("ui.layers.empty", "Empty Layer"));
    empty_action->setObjectName(QStringLiteral("layers.action.empty"));
    empty_action->setEnabled(!layer_by_id_.at(id).members.empty());

    const std::vector<QuaderLayerId> selected = selected_layer_ids();
    QAction *delete_action = menu.addAction(
        selected.size() > 1
            ? localized("ui.layers.delete_multiple", "Delete Layers")
            : localized("ui.layers.delete", "Delete Layer"));
    delete_action->setObjectName(QStringLiteral("layers.action.delete"));
    delete_action->setEnabled(!selected.empty());

    QAction *chosen = menu.exec(global_position);
    QuaderApiVoidResult result;
    if (chosen == add_selection_action) {
      result = editor_->layers().add_selection(id);
    } else if (chosen == remove_selection_action) {
      result = editor_->layers().remove_selection(id);
    } else if (chosen == empty_action) {
      result = editor_->layers().empty(id);
    } else if (chosen == delete_action) {
      result = editor_->layers().destroy(std::span<const QuaderLayerId>(
          selected.data(), selected.size()));
    } else {
      return;
    }
    if (result.ok() && result.changed()) {
      synchronize(true);
    }
  }

  LayerPanel &panel_;
  LayerTreeView *tree_ = nullptr;
  QStandardItemModel *model_ = nullptr;
  QToolButton *create_button_ = nullptr;
  QToolButton *create_from_selection_button_ = nullptr;
  QuaderEditorApi *editor_ = nullptr;
  const SceneDocument *document_ = nullptr;
  LocalizationService *localization_ = nullptr;
  std::unordered_map<QuaderLayerId, QuaderLayerSnapshot> layer_by_id_;
  std::unordered_map<QuaderLayerId, QStandardItem *> name_item_by_id_;
  std::optional<QuaderLayerId> select_after_refresh_;
  std::uint64_t synchronized_layer_revision_ = 0;
  std::uint64_t synchronized_selection_revision_ = 0;
  std::string synchronized_locale_;
  bool synchronization_valid_ = false;
  bool syncing_model_ = false;
};

LayerPanel::LayerPanel(QWidget *parent)
    : QWidget(parent), impl_(std::make_unique<Impl>(*this)) {}

LayerPanel::~LayerPanel() = default;

void LayerPanel::refresh(UIContext &context) { impl_->refresh(context); }

} // namespace quader
