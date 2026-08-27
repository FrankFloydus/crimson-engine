////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/inspector_panel.hpp"

#include "api/quader_api_type_conversions.hpp"
#include "api/quader_editor_api.hpp"
#include "core/localization_service.hpp"
#include "core/scene_environment_catalog.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/modeling/services/selection_mode_adapter.hpp"
#include "editor/transform/scene_transform_drag_service.hpp"
#include "scene/scene_document.hpp"
#include "scene/transform_pivot.hpp"
#include "ui/qt_color_swatch_button.hpp"
#include "ui/qt_semantic_axis_colors.hpp"
#include "ui/ui_context.hpp"
#include "ui/ui_continuous_edit_tracker.hpp"
#include "ui/ui_panel.hpp"

#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFocusEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QPointer>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSizePolicy>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QString>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QVariant>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace quader {
namespace {

constexpr float kRadToDeg = 57.29577951308232f;
constexpr float kDegToRad = 0.017453292519943295f;
constexpr float kInspectorLightRangeMin = 0.1f;
constexpr float kInspectorLightRangeSliderMax = 100.0f;
constexpr int kInspectorMinimumValueColumnWidth = 96;
constexpr int kInspectorMaterialPreviewSize = 56;
constexpr int kInspectorMaterialActionSize = 24;
constexpr int kInspectorMaterialActionIconSize = 16;

QString qt_text(std::string_view text) {
  return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

const char *selection_mode_key(ModelingSelectionMode mode) {
  switch (mode) {
  case ModelingSelectionMode::Vertex:
    return "selection.mode.vertex";
  case ModelingSelectionMode::Edge:
    return "selection.mode.edge";
  case ModelingSelectionMode::Face:
    return "selection.mode.face";
  case ModelingSelectionMode::Object:
    return "selection.mode.object";
  case ModelingSelectionMode::Group:
    return "selection.mode.group";
  }
  return "selection.mode.object";
}

const char *selection_mode_fallback(ModelingSelectionMode mode) {
  switch (mode) {
  case ModelingSelectionMode::Vertex:
    return "Vertex";
  case ModelingSelectionMode::Edge:
    return "Edge";
  case ModelingSelectionMode::Face:
    return "Face";
  case ModelingSelectionMode::Object:
    return "Object";
  case ModelingSelectionMode::Group:
    return "Group";
  }
  return "Object";
}

const char *active_selection_kind_key(ModelingActiveSelectionKind kind) {
  switch (kind) {
  case ModelingActiveSelectionKind::None:
    return "selection.active.none";
  case ModelingActiveSelectionKind::Object:
    return "selection.active.object";
  case ModelingActiveSelectionKind::Vertex:
    return "selection.active.vertex";
  case ModelingActiveSelectionKind::Edge:
    return "selection.active.edge";
  case ModelingActiveSelectionKind::Face:
    return "selection.active.face";
  case ModelingActiveSelectionKind::Light:
    return "selection.active.light";
  }
  return "selection.active.none";
}

const char *active_selection_kind_fallback(ModelingActiveSelectionKind kind) {
  switch (kind) {
  case ModelingActiveSelectionKind::None:
    return "None";
  case ModelingActiveSelectionKind::Object:
    return "Object";
  case ModelingActiveSelectionKind::Vertex:
    return "Vertex";
  case ModelingActiveSelectionKind::Edge:
    return "Edge";
  case ModelingActiveSelectionKind::Face:
    return "Face";
  case ModelingActiveSelectionKind::Light:
    return "Light";
  }
  return "None";
}

/// Summarizes a boolean property across the inspected entity selection.
struct BoolAggregate {
  bool has_value = false;
  bool all_true = true;
  bool any_true = false;

  [[nodiscard]] bool mixed() const { return any_true && !all_true; }
};

BoolAggregate aggregate_visibility(const SceneDocument &document,
                                   const std::vector<EntityId> &selected) {
  BoolAggregate result;
  for (const EntityId id : selected) {
    if (const VisibilityComponent *visibility = document.visibility(id)) {
      result.has_value = true;
      result.all_true = result.all_true && visibility->visible;
      result.any_true = result.any_true || visibility->visible;
    }
  }
  return result;
}

BoolAggregate aggregate_lock(const SceneDocument &document,
                             const std::vector<EntityId> &selected) {
  BoolAggregate result;
  for (const EntityId id : selected) {
    if (const LockComponent *lock = document.lock(id)) {
      result.has_value = true;
      result.all_true = result.all_true && lock->locked;
      result.any_true = result.any_true || lock->locked;
    }
  }
  return result;
}

bool same_float(float left, float right) {
  return std::fabs(left - right) <= 0.0001f;
}

std::array<bool, 3> mixed_position_axes(const SceneDocument &document,
                                        const std::vector<EntityId> &selected,
                                        const TransformComponent &primary) {
  std::array<bool, 3> mixed{false, false, false};
  for (const EntityId id : selected) {
    if (const TransformComponent *transform = document.transform(id)) {
      mixed[0] =
          mixed[0] || !same_float(transform->position.x, primary.position.x);
      mixed[1] =
          mixed[1] || !same_float(transform->position.y, primary.position.y);
      mixed[2] =
          mixed[2] || !same_float(transform->position.z, primary.position.z);
    }
  }
  return mixed;
}

std::array<bool, 3> mixed_rotation_axes(const SceneDocument &document,
                                        const std::vector<EntityId> &selected,
                                        const TransformComponent &primary) {
  std::array<bool, 3> mixed{false, false, false};
  for (const EntityId id : selected) {
    if (const TransformComponent *transform = document.transform(id)) {
      mixed[0] = mixed[0] || !same_float(transform->rotation_euler.x,
                                         primary.rotation_euler.x);
      mixed[1] = mixed[1] || !same_float(transform->rotation_euler.y,
                                         primary.rotation_euler.y);
      mixed[2] = mixed[2] || !same_float(transform->rotation_euler.z,
                                         primary.rotation_euler.z);
    }
  }
  return mixed;
}

std::array<bool, 3> mixed_scale_axes(const SceneDocument &document,
                                     const std::vector<EntityId> &selected,
                                     const TransformComponent &primary) {
  std::array<bool, 3> mixed{false, false, false};
  for (const EntityId id : selected) {
    if (const TransformComponent *transform = document.transform(id)) {
      mixed[0] = mixed[0] || !same_float(transform->scale.x, primary.scale.x);
      mixed[1] = mixed[1] || !same_float(transform->scale.y, primary.scale.y);
      mixed[2] = mixed[2] || !same_float(transform->scale.z, primary.scale.z);
    }
  }
  return mixed;
}

/// Selects the complete editor value when keyboard focus enters the field.
class SelectAllLineEdit final : public QLineEdit {
public:
  explicit SelectAllLineEdit(QWidget *parent = nullptr) : QLineEdit(parent) {}

protected:
  void focusInEvent(QFocusEvent *event) override {
    QLineEdit::focusInEvent(event);
    if (event != nullptr &&
        (event->reason() == Qt::TabFocusReason ||
         event->reason() == Qt::BacktabFocusReason ||
         event->reason() == Qt::ShortcutFocusReason)) {
      QTimer::singleShot(0, this, &QLineEdit::selectAll);
    }
  }
};

/// Retains the complete accessible name while painting a compact elided row.
class InspectorElidedLabel final : public QLabel {
public:
  explicit InspectorElidedLabel(QString text, QWidget *parent = nullptr)
      : QLabel(parent), full_text_(std::move(text)) {
    setTextFormat(Qt::PlainText);
    setAccessibleName(full_text_);
    update_elided_text();
  }

protected:
  void resizeEvent(QResizeEvent *event) override {
    QLabel::resizeEvent(event);
    update_elided_text();
  }

private:
  void update_elided_text() {
    const QString elided =
        fontMetrics().elidedText(full_text_, Qt::ElideRight, width());
    QLabel::setText(elided);
    setToolTip(elided == full_text_ ? QString{} : full_text_);
  }

  QString full_text_;
};

enum class InspectorMaterialActionIcon { Edit, Replace };

QPixmap material_action_pixmap(InspectorMaterialActionIcon action,
                               const QColor &color) {
  QPixmap pixmap(kInspectorMaterialActionIconSize,
                 kInspectorMaterialActionIconSize);
  pixmap.fill(Qt::transparent);

  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing, true);
  QPen pen(color);
  pen.setWidthF(1.6);
  pen.setCapStyle(Qt::RoundCap);
  pen.setJoinStyle(Qt::RoundJoin);
  painter.setPen(pen);

  if (action == InspectorMaterialActionIcon::Edit) {
    painter.drawLine(QPointF(4.0, 12.0), QPointF(11.5, 4.5));
    painter.drawLine(QPointF(5.7, 13.0), QPointF(13.0, 5.7));
    painter.drawLine(QPointF(3.5, 12.4), QPointF(3.0, 14.0));
    painter.drawLine(QPointF(3.0, 14.0), QPointF(4.6, 13.5));
    painter.drawLine(QPointF(10.9, 4.0), QPointF(13.5, 6.6));
  } else {
    // Two overlapping swatches communicate substitution without arrows.
    painter.drawRoundedRect(QRectF(2.5, 2.5, 8.0, 8.0), 1.0, 1.0);
    painter.drawRoundedRect(QRectF(5.5, 5.5, 8.0, 8.0), 1.0, 1.0);
  }
  return pixmap;
}

QIcon material_action_icon(InspectorMaterialActionIcon action,
                            const QPalette &palette) {
  QIcon icon;
  icon.addPixmap(material_action_pixmap(action,
                                        palette.color(QPalette::ButtonText)),
                 QIcon::Normal, QIcon::Off);
  icon.addPixmap(material_action_pixmap(action, palette.color(QPalette::Mid)),
                 QIcon::Disabled, QIcon::Off);
  return icon;
}

/// Extends the stock spin box with an explicit cancel callback for previews.
class InspectorDoubleSpinBox final : public QDoubleSpinBox {
public:
  explicit InspectorDoubleSpinBox(QWidget *parent = nullptr)
      : QDoubleSpinBox(parent) {}

  std::function<void()> cancel_handler;

protected:
  void keyPressEvent(QKeyEvent *event) override {
    if (event != nullptr && event->key() == Qt::Key_Escape &&
        cancel_handler != nullptr) {
      cancel_handler();
      clearFocus();
      event->accept();
      return;
    }
    QDoubleSpinBox::keyPressEvent(event);
  }
};

/// Draws inspector property-cell separators from the active Fusion palette.
class InspectorPropertyDelegate final : public QStyledItemDelegate {
public:
  using QStyledItemDelegate::QStyledItemDelegate;

  void paint(QPainter *painter, const QStyleOptionViewItem &option,
             const QModelIndex &index) const override {
    QStyledItemDelegate::paint(painter, option, index);
    if (painter == nullptr || !index.isValid()) {
      return;
    }

    painter->save();
    QPen separator(option.palette.mid().color());
    separator.setCosmetic(true);
    painter->setPen(separator);
    painter->drawLine(option.rect.bottomLeft(), option.rect.bottomRight());
    if (index.parent().isValid() && index.column() == 0) {
      painter->drawLine(option.rect.topRight(), option.rect.bottomRight());
    }
    painter->restore();
  }
};

/// Identifies which transform component an inspector preview edits.
enum class TransformEditKind {
  Position,
  Rotation,
  Scale,
};

/// Captures the selection and baseline values for one transform edit preview.
struct TransformEditState {
  TransformEditKind kind = TransformEditKind::Position;
  EntityId primary = kInvalidEntityId;
  std::vector<EntityId> selection;
  std::array<float, 3> values{};
};

} // namespace

/// Owns InspectorPanel's retained sections and transform preview state.
struct InspectorPanel::Impl {
  /// Identifies the expandable property-tree item that owns one section.
  struct Section {
    QTreeWidgetItem *item = nullptr;
    QWidget *editor_parent = nullptr;
  };

  explicit Impl(InspectorPanel &panel) : panel_(panel) {
    panel_.setObjectName(
        QString::fromLatin1(ui_panel_stable_id(UIPanelId::Inspector)));
    panel_.setWindowTitle(
        QString::fromLatin1(ui_panel_key(UIPanelId::Inspector)));

    auto *root_layout = new QVBoxLayout(&panel_);
    root_layout->setObjectName(QStringLiteral("inspector.layout"));
    root_layout->setContentsMargins(0, 0, 0, 0);

    tree_ = new QTreeWidget(&panel_);
    tree_->setObjectName(QStringLiteral("inspector.tree"));
    tree_->setColumnCount(2);
    tree_->setRootIsDecorated(true);
    tree_->setItemsExpandable(true);
    tree_->setAlternatingRowColors(true);
    tree_->setUniformRowHeights(false);
    tree_->setWordWrap(false);
    tree_->setSelectionMode(QAbstractItemView::NoSelection);
    tree_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    tree_->setTextElideMode(Qt::ElideRight);
    tree_->setItemDelegate(new InspectorPropertyDelegate(tree_));
    panel_.setFocusProxy(tree_);
    tree_->header()->setVisible(true);
    tree_->header()->setSectionsMovable(false);
    tree_->header()->setSectionsClickable(false);
    tree_->header()->setHighlightSections(false);
    tree_->header()->setMinimumSectionSize(64);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    tree_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    tree_->header()->setStretchLastSection(true);
    tree_->header()->resizeSection(0, 140);
    root_layout->addWidget(tree_);

    const auto remember_section_state = [this](QTreeWidgetItem *item,
                                                bool expanded) {
      if (rebuilding_ || item == nullptr) {
        return;
      }
      const QString stable_id = item->data(0, Qt::UserRole).toString();
      if (!stable_id.isEmpty()) {
        expanded_sections_[stable_id.toUtf8().toStdString()] = expanded;
      }
    };
    QObject::connect(tree_, &QTreeWidget::itemExpanded, &panel_,
                     [remember_section_state](QTreeWidgetItem *item) {
                       remember_section_state(item, true);
                     });
    QObject::connect(tree_, &QTreeWidget::itemCollapsed, &panel_,
                     [remember_section_state](QTreeWidgetItem *item) {
                       remember_section_state(item, false);
                     });
    QObject::connect(tree_->header(), &QHeaderView::sectionResized, &panel_,
                     [this](int, int, int) {
                       clamp_label_column_width();
                     });
    QObject::connect(tree_->header(), &QHeaderView::geometriesChanged, &panel_,
                     [this]() { clamp_label_column_width(); });
  }

  void set_material_presentation(
      InspectorMaterialPresentationSnapshot snapshot) {
    const bool presentation_changed =
        material_presentation_.revision != snapshot.revision ||
        material_presentation_.section_visible != snapshot.section_visible ||
        static_cast<bool>(material_presentation_.edit_material) !=
            static_cast<bool>(snapshot.edit_material) ||
        static_cast<bool>(material_presentation_.replace_material) !=
            static_cast<bool>(snapshot.replace_material);
    if (!presentation_changed) {
      // Callback rebinding does not require rebuilding immutable controls.
      material_presentation_.edit_material =
          std::move(snapshot.edit_material);
      material_presentation_.replace_material =
          std::move(snapshot.replace_material);
      return;
    }
    material_presentation_ = std::move(snapshot);
    force_rebuild_ = true;
  }

  void refresh(UIContext &context) {
    const SceneDocument *next_document = &context.scene.document;
    const ModelingSessionService *next_modeling = &context.scene.modeling;
    const bool owner_changed =
        document_ != next_document || modeling_ != next_modeling;

    std::vector<EntityId> next_selection =
        next_document->selected_entities();
    EntityId next_primary = next_document->primary_selection();
    if (next_primary == kInvalidEntityId && !next_selection.empty()) {
      next_primary = next_selection.front();
    }
    const bool selection_changed =
        owner_changed || next_selection != selection_ ||
        next_primary != primary_;
    if (selection_changed && active_color_dialog_ != nullptr) {
      active_color_dialog_->reject();
    }
    if (selection_changed) {
      cancel_active_transform_edit();
      active_continuous_edit_.reset();
      continuous_edit_active_ = false;
      active_continuous_id_.clear();
      selection_ = std::move(next_selection);
      primary_ = next_primary;
      force_rebuild_ = true;
    }

    editor_ = &context.intent.editor;
    document_ = next_document;
    modeling_ = next_modeling;
    localization_ = &context.localization;
    preview_document_ = context.inspector_preview != nullptr
                            ? &context.inspector_preview->document
                            : nullptr;
    preview_assets_ = context.inspector_preview != nullptr
                          ? &context.inspector_preview->assets
                          : nullptr;

    const ModelingSelectionSummary summary = modeling_->selection_summary();
    const bool presentation_changed =
        force_rebuild_ || built_scene_revision_ != document_->revision() ||
        built_modeling_content_revision_ != summary.content_revision ||
        built_modeling_selection_revision_ != summary.selection_revision ||
        built_material_presentation_revision_ !=
            material_presentation_.revision ||
        built_locale_ != localization_->current_locale();
    if (!presentation_changed) {
      return;
    }
    if (!selection_changed &&
        (active_transform_edit_.has_value() || continuous_edit_active_)) {
      return;
    }

    rebuild(summary);
  }

private:
  void clamp_label_column_width() {
    if (adjusting_label_column_width_ || tree_ == nullptr ||
        !panel_.isVisible()) {
      return;
    }
    QHeaderView *header = tree_->header();
    const int maximum_label_width =
        std::max(header->minimumSectionSize(),
                 tree_->viewport()->width() -
                     kInspectorMinimumValueColumnWidth);
    if (header->sectionSize(0) <= maximum_label_width) {
      return;
    }
    adjusting_label_column_width_ = true;
    header->resizeSection(0, maximum_label_width);
    adjusting_label_column_width_ = false;
  }

  QString localized(std::string_view key, std::string_view fallback) const {
    return localization_ != nullptr
               ? qt_text(localization_->text(key, fallback))
               : qt_text(fallback);
  }

  QString stable_name(std::string_view stable_id) const {
    QString result = QStringLiteral("inspector.");
    result.append(qt_text(stable_id));
    return result;
  }

  void clear_content() {
    tree_->clear();
  }

  void rebuild(const ModelingSelectionSummary &summary) {
    const int previous_scroll = tree_->verticalScrollBar()->value();
    rebuilding_ = true;
    clear_content();

    panel_.setWindowTitle(qt_text(
        localization_->text(ui_panel_localization_key(UIPanelId::Inspector),
                            ui_panel_key(UIPanelId::Inspector))));
    tree_->setHeaderLabels(
        {localized("ui.inspector.property", "Property"),
         localized("ui.inspector.value", "Value")});

    if (summary.mode == ModelingSelectionMode::Vertex ||
        summary.mode == ModelingSelectionMode::Edge) {
      // Vertex and Edge modes intentionally present no Inspector properties.
    } else if (summary.mode == ModelingSelectionMode::Face) {
      build_material_section();
    } else if (selection_.empty()) {
      build_empty_selection(summary);
    } else {
      build_general_section();
      build_transform_section();
      build_material_section();

      if (selection_.size() == 1u && document_->is_scene_root(primary_)) {
        build_scene_settings_sections();
      } else if (selection_.size() == 1u) {
        build_mesh_sections();
        build_light_section();
      }
      if (selection_.size() > 1u) {
        build_component_count_section();
      }
    }

    built_scene_revision_ = document_->revision();
    built_modeling_content_revision_ = summary.content_revision;
    built_modeling_selection_revision_ = summary.selection_revision;
    built_material_presentation_revision_ = material_presentation_.revision;
    built_locale_ = localization_->current_locale();
    force_rebuild_ = false;
    rebuilding_ = false;

    QTimer::singleShot(0, &panel_, [this, previous_scroll]() {
      tree_->verticalScrollBar()->setValue(previous_scroll);
      clamp_label_column_width();
    });
  }

  QTreeWidgetItem *add_summary_label(std::string_view stable_id,
                                     const QString &text) {
    auto *item = new QTreeWidgetItem(tree_);
    item->setText(0, text);
    item->setData(0, Qt::UserRole, stable_name(stable_id));
    item->setFlags(Qt::ItemIsEnabled);
    item->setFirstColumnSpanned(true);
    return item;
  }

  void build_empty_selection(const ModelingSelectionSummary &summary) {
    if (!summary.has_selection) {
      add_summary_label("summary.no_selection",
                        localized("ui.inspector.no_selection", "No selection"));
      return;
    }

    const QString mode = localized(selection_mode_key(summary.mode),
                                   selection_mode_fallback(summary.mode));
    const QString active =
        localized(active_selection_kind_key(summary.active_kind),
                  active_selection_kind_fallback(summary.active_kind));
    add_summary_label("summary.mode",
                      QStringLiteral("%1: %2").arg(
                          localized("ui.inspector.mode", "Mode"), mode));
    add_summary_label("summary.active",
                      QStringLiteral("%1: %2").arg(
                          localized("ui.inspector.active", "Active"), active));
    add_summary_label("summary.vertices",
                      QStringLiteral("%1: %2")
                          .arg(localized("ui.inspector.vertices", "Vertices"))
                          .arg(summary.selected_vertex_count));
    add_summary_label("summary.edges",
                      QStringLiteral("%1: %2")
                          .arg(localized("ui.inspector.edges", "Edges"))
                          .arg(summary.selected_edge_count));
    add_summary_label("summary.faces",
                      QStringLiteral("%1: %2")
                          .arg(localized("ui.inspector.faces", "Faces"))
                          .arg(summary.selected_face_count));
  }

  void
  build_component_selection_summary(const ModelingSelectionSummary &summary) {
    const QString active =
        localized(active_selection_kind_key(summary.active_kind),
                  active_selection_kind_fallback(summary.active_kind));
    add_summary_label("summary.active",
                      QStringLiteral("%1: %2").arg(
                          localized("ui.inspector.active", "Active"), active));
    if (summary.selected_vertex_count > 0u) {
      add_summary_label("summary.vertices",
                        QStringLiteral("%1: %2")
                            .arg(localized("ui.inspector.vertices", "Vertices"))
                            .arg(summary.selected_vertex_count));
    }
    if (summary.selected_edge_count > 0u) {
      add_summary_label("summary.edges",
                        QStringLiteral("%1: %2")
                            .arg(localized("ui.inspector.edges", "Edges"))
                            .arg(summary.selected_edge_count));
    }
    if (summary.selected_face_count > 0u) {
      add_summary_label("summary.faces",
                        QStringLiteral("%1: %2")
                            .arg(localized("ui.inspector.faces", "Faces"))
                            .arg(summary.selected_face_count));
    }
  }

  Section add_section(std::string stable_id, const QString &title,
                      bool default_expanded = false) {
    const auto state =
        expanded_sections_.try_emplace(stable_id, default_expanded).first;
    auto *item = new QTreeWidgetItem(tree_);
    item->setText(0, title);
    item->setData(0, Qt::UserRole, qt_text(stable_id));
    item->setFlags(Qt::ItemIsEnabled);
    item->setFirstColumnSpanned(true);
    item->setExpanded(state->second);
    return {.item = item, .editor_parent = tree_->viewport()};
  }

  void add_row(Section &section, std::string_view stable_id,
               const QString &label, QWidget *field) {
    auto *item = new QTreeWidgetItem(section.item);
    item->setText(0, label);
    item->setData(0, Qt::UserRole, stable_name(stable_id));
    item->setFlags(Qt::ItemIsEnabled);

    field->setMinimumWidth(0);
    QSizePolicy policy = field->sizePolicy();
    policy.setHorizontalPolicy(QSizePolicy::Ignored);
    policy.setHorizontalStretch(1);
    field->setSizePolicy(policy);
    tree_->setItemWidget(item, 1, field);
  }

  void build_material_section() {
    if (!material_presentation_.section_visible) {
      return;
    }

    Section section = add_section(
        "material", localized("ui.inspector.material", "Material"), true);
    std::vector<QuaderMaterialReference> presented_references;
    presented_references.reserve(material_presentation_.materials.size());

    std::size_t row_index = 0;
    for (const QtMaterialPresentationItem &material :
         material_presentation_.materials) {
      if (std::find(presented_references.begin(), presented_references.end(),
                    material.reference) != presented_references.end()) {
        continue;
      }
      presented_references.push_back(material.reference);

      const std::string stable_id =
          "material." + std::to_string(row_index++);
      auto *item = new QTreeWidgetItem(section.item);
      item->setData(0, Qt::UserRole, stable_name(stable_id));
      item->setFlags(Qt::ItemIsEnabled);
      item->setFirstColumnSpanned(true);
      item->setSizeHint(
          0, QSize(0, kInspectorMaterialPreviewSize + 8));

      auto *row = new QWidget(section.editor_parent);
      row->setObjectName(stable_name(stable_id + ".row"));
      auto *layout = new QHBoxLayout(row);
      layout->setContentsMargins(4, 4, 4, 4);
      layout->setSpacing(6);

      auto *preview = new QLabel(row);
      preview->setObjectName(stable_name(stable_id + ".preview"));
      preview->setFixedSize(kInspectorMaterialPreviewSize,
                            kInspectorMaterialPreviewSize);
      preview->setAlignment(Qt::AlignCenter);
      preview->setFrameShape(QFrame::StyledPanel);
      preview->setPixmap(qt_material_preview_pixmap(
          material, QSize(kInspectorMaterialPreviewSize,
                          kInspectorMaterialPreviewSize),
          preview->devicePixelRatioF()));
      layout->addWidget(preview);

      const QString display_name =
          material.name.isEmpty()
              ? localized("ui.inspector.material.missing", "Missing material")
              : material.name;
      auto *name = new InspectorElidedLabel(display_name, row);
      name->setObjectName(stable_name(stable_id + ".name"));
      name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
      layout->addWidget(name, 1);

      auto *edit = new QPushButton(row);
      edit->setObjectName(stable_name(stable_id + ".edit"));
      edit->setFixedSize(kInspectorMaterialActionSize,
                         kInspectorMaterialActionSize);
      edit->setIcon(material_action_icon(InspectorMaterialActionIcon::Edit,
                                         edit->palette()));
      edit->setIconSize(QSize(kInspectorMaterialActionIconSize,
                              kInspectorMaterialActionIconSize));
      const QString edit_label =
          localized("ui.inspector.material.edit", "Edit material");
      edit->setAccessibleName(edit_label);
      edit->setToolTip(edit_label);
      edit->setEnabled(material.reference.project_owned() &&
                       material.editable && material.available &&
                       material.valid() &&
                       static_cast<bool>(material_presentation_.edit_material));
      layout->addWidget(edit, 0, Qt::AlignVCenter);

      auto *replace = new QPushButton(row);
      replace->setObjectName(stable_name(stable_id + ".replace"));
      replace->setFixedSize(kInspectorMaterialActionSize,
                            kInspectorMaterialActionSize);
      replace->setIcon(material_action_icon(
          InspectorMaterialActionIcon::Replace, replace->palette()));
      replace->setIconSize(QSize(kInspectorMaterialActionIconSize,
                                 kInspectorMaterialActionIconSize));
      const QString replace_label =
          localized("ui.inspector.material.replace", "Replace material");
      replace->setAccessibleName(replace_label);
      replace->setToolTip(replace_label);
      replace->setEnabled(
          material.reference.valid() &&
          static_cast<bool>(material_presentation_.replace_material));
      layout->addWidget(replace, 0, Qt::AlignVCenter);

      const QuaderMaterialReference reference = material.reference;
      QObject::connect(edit, &QPushButton::clicked, &panel_,
                       [this, reference]() {
                         if (material_presentation_.edit_material) {
                           material_presentation_.edit_material(reference);
                         }
                       });
      QObject::connect(replace, &QPushButton::clicked, &panel_,
                       [this, reference]() {
                         if (material_presentation_.replace_material) {
                           material_presentation_.replace_material(reference);
                         }
                       });

      tree_->setItemWidget(item, 0, row);
    }
  }

  QCheckBox *add_checkbox(Section &section, std::string stable_id,
                          const QString &label, bool checked,
                          std::function<void(bool)> changed) {
    auto *box = new QCheckBox(section.editor_parent);
    box->setObjectName(stable_name(stable_id));
    box->setChecked(checked);
    add_row(section, stable_id, label, box);
    QObject::connect(box, &QCheckBox::toggled, &panel_,
                     [this, changed = std::move(changed)](bool value) {
                       if (!rebuilding_) {
                         changed(value);
                       }
                     });
    return box;
  }

  void add_mixed_checkbox(Section &section, std::string stable_id,
                          const QString &label, bool checked, bool mixed,
                          std::function<void(bool)> changed) {
    auto *field = new QWidget(section.editor_parent);
    field->setObjectName(stable_name(stable_id + ".field"));
    auto *layout = new QHBoxLayout(field);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *box = new QCheckBox(field);
    box->setObjectName(stable_name(stable_id));
    box->setChecked(checked);
    layout->addWidget(box);
    if (mixed) {
      auto *mixed_label =
          new QLabel(localized("ui.inspector.mixed", "Mixed"), field);
      mixed_label->setObjectName(stable_name(stable_id + ".mixed"));
      layout->addWidget(mixed_label);
    }
    layout->addStretch(1);
    add_row(section, stable_id, label, field);
    QObject::connect(box, &QCheckBox::toggled, &panel_,
                     [this, changed = std::move(changed)](bool value) {
                       if (!rebuilding_) {
                         changed(value);
                       }
                     });
  }

  template <typename Enum>
  QComboBox *
  add_enum_combo(Section &section, std::string stable_id, const QString &label,
                 Enum current,
                 const std::vector<std::pair<Enum, QString>> &options,
                 std::function<void(Enum)> changed) {
    auto *combo = new QComboBox(section.editor_parent);
    combo->setObjectName(stable_name(stable_id));
    int selected_index = -1;
    for (std::size_t index = 0; index < options.size(); ++index) {
      combo->addItem(options[index].second,
                     static_cast<int>(options[index].first));
      if (options[index].first == current) {
        selected_index = static_cast<int>(index);
      }
    }
    combo->setCurrentIndex(selected_index);
    add_row(section, stable_id, label, combo);
    QObject::connect(combo, &QComboBox::activated, &panel_,
                     [this, combo, changed = std::move(changed)](int index) {
                       if (rebuilding_ || index < 0) {
                         return;
                       }
                       changed(
                           static_cast<Enum>(combo->itemData(index).toInt()));
                     });
    return combo;
  }

  QDoubleSpinBox *
  add_continuous_number(Section &section, std::string stable_id,
                        const QString &label, double value, double minimum,
                        double maximum, double step, int decimals,
                        std::function<void(double, std::string)> changed) {
    auto *spin = new InspectorDoubleSpinBox(section.editor_parent);
    spin->setObjectName(stable_name(stable_id));
    spin->setRange(minimum, maximum);
    spin->setSingleStep(step);
    spin->setDecimals(decimals);
    spin->setKeyboardTracking(true);
    spin->setValue(value);
    add_row(section, stable_id, label, spin);

    QObject::connect(
        spin, &QDoubleSpinBox::valueChanged, &panel_,
        [this, stable_id, changed = std::move(changed)](double next) {
          if (rebuilding_) {
            return;
          }
          const std::span<const EntityId> selection(selection_.data(),
                                                    selection_.size());
          const std::string merge_key =
              active_continuous_edit_.ensure_merge_key(stable_id, primary_,
                                                       selection);
          continuous_edit_active_ = true;
          active_continuous_id_ = stable_id;
          changed(next, merge_key);
        });
    QObject::connect(spin, &QDoubleSpinBox::editingFinished, &panel_,
                     [this, stable_id]() {
                       if (rebuilding_ || active_continuous_id_ != stable_id) {
                         return;
                       }
                       active_continuous_edit_.reset();
                       continuous_edit_active_ = false;
                       active_continuous_id_.clear();
                       force_rebuild_ = true;
                     });
    return spin;
  }

  QtColorSwatchButton *
  add_color_button(Section &section, std::string stable_id,
                    const QString &label, QVec3 value,
                    std::function<void(QVec3)> changed) {
    const QColor initial = QColor::fromRgbF(std::clamp(value.x, 0.0f, 1.0f),
                                            std::clamp(value.y, 0.0f, 1.0f),
                                            std::clamp(value.z, 0.0f, 1.0f));
    auto *button = new QtColorSwatchButton(section.editor_parent);
    button->setObjectName(stable_name(stable_id));
    button->setAccessibleName(label);
    button->set_swatch_color(initial);
    add_row(section, stable_id, label, button);

    QObject::connect(
        button, &QtColorSwatchButton::clicked, &panel_,
        [this, stable_id, initial, button,
         changed = std::move(changed)]() mutable {
          QColorDialog dialog(initial, &panel_);
          dialog.setObjectName(stable_name(stable_id + ".dialog"));
          dialog.setOption(QColorDialog::DontUseNativeDialog, true);
          active_color_dialog_ = &dialog;
          QPointer<QtColorSwatchButton> guarded_button(button);
          QObject::connect(
              &dialog, &QColorDialog::currentColorChanged, &dialog,
              [guarded_button](const QColor &color) {
                if (!color.isValid()) {
                  return;
                }
                if (guarded_button != nullptr) {
                  guarded_button->set_swatch_color(color);
                }
              });

          const int result = dialog.exec();
          const QColor committed = dialog.currentColor();
          if (result == QDialog::Accepted && committed.isValid() &&
              committed != initial) {
            changed({static_cast<float>(committed.redF()),
                     static_cast<float>(committed.greenF()),
                     static_cast<float>(committed.blueF())});
          } else if (guarded_button != nullptr) {
            guarded_button->set_swatch_color(initial);
          }
          active_color_dialog_ = nullptr;
          force_rebuild_ = true;
        });
    return button;
  }

  void add_read_only_value(Section &section, std::string stable_id,
                           const QString &label, const QString &value) {
    auto *text = new QLabel(value, section.editor_parent);
    text->setObjectName(stable_name(stable_id));
    text->setTextInteractionFlags(Qt::TextSelectableByMouse);
    add_row(section, stable_id, label, text);
  }

  void apply_scene_settings(
      EntityId entity,
      const std::function<void(SceneSettingsComponent &)> &mutation,
      std::string merge_key = {}) {
    if (editor_ == nullptr || document_ == nullptr) {
      return;
    }
    const SceneSettingsComponent *current = document_->scene_settings(entity);
    if (current == nullptr) {
      return;
    }
    SceneSettingsComponent next = *current;
    mutation(next);
    next.environment.intensity = std::max(next.environment.intensity, 0.0f);
    next.environment.background_intensity =
        std::max(next.environment.background_intensity, 0.0f);
    next.camera.aperture = std::max(next.camera.aperture, 0.01f);
    next.camera.shutter_speed = std::max(next.camera.shutter_speed, 0.000001f);
    next.camera.sensitivity = std::max(next.camera.sensitivity, 1.0f);
    next.camera.near_plane = std::max(next.camera.near_plane, 0.001f);
    next.camera.far_plane =
        std::max(next.camera.far_plane, next.camera.near_plane + 0.001f);
    next.post_process.ambient_occlusion_intensity =
        std::max(next.post_process.ambient_occlusion_intensity, 0.0f);
    next.post_process.ambient_occlusion_radius =
        std::max(next.post_process.ambient_occlusion_radius, 0.0f);
    next.post_process.bloom_strength =
        std::max(next.post_process.bloom_strength, 0.0f);
    (void)editor_->scene().set_scene_settings(
        entity, to_api_scene_settings(next),
        QuaderSceneEditOptions{.merge_key = std::move(merge_key)});
  }

  void
  apply_mesh_component(EntityId entity,
                       const std::function<void(MeshComponent &)> &mutation) {
    if (editor_ == nullptr || document_ == nullptr) {
      return;
    }
    const MeshComponent *current = document_->mesh(entity);
    if (current == nullptr) {
      return;
    }
    MeshComponent next = *current;
    mutation(next);
    (void)editor_->scene().set_mesh_component(entity,
                                              to_api_mesh_component(next));
  }

  void
  apply_light_component(EntityId entity,
                        const std::function<void(LightComponent &)> &mutation,
                        std::string merge_key = {}) {
    if (editor_ == nullptr || document_ == nullptr) {
      return;
    }
    const LightComponent *current = document_->light(entity);
    if (current == nullptr) {
      return;
    }
    LightComponent next = *current;
    mutation(next);
    next.range = std::max(next.range, kInspectorLightRangeMin);
    next.intensity = std::max(next.intensity, 0.0f);
    (void)editor_->scene().set_light_component(
        entity, to_api_light_component(next),
        QuaderSceneEditOptions{.merge_key = std::move(merge_key)});
  }

  void build_general_section() {
    std::vector<EntityId> editable_selected;
    editable_selected.reserve(selection_.size());
    for (const EntityId entity : selection_) {
      if (!document_->is_scene_root(entity)) {
        editable_selected.push_back(entity);
      }
    }
    const BoolAggregate visibility =
        aggregate_visibility(*document_, editable_selected);
    const BoolAggregate lock = aggregate_lock(*document_, editable_selected);
    if (selection_.size() != 1u && !visibility.has_value && !lock.has_value) {
      return;
    }

    Section section =
        add_section("general", localized("ui.inspector.general", "General"));
    if (selection_.size() == 1u) {
      auto *name = new SelectAllLineEdit(section.editor_parent);
      name->setObjectName(stable_name("general.name"));
      name->setMaxLength(127);
      if (const NameComponent *component = document_->name(primary_)) {
        name->setText(qt_text(component->name));
      }
      const bool scene_root = document_->is_scene_root(primary_);
      name->setReadOnly(scene_root);
      add_row(section, "general.name", localized("ui.inspector.name", "Name"),
              name);
      if (!scene_root) {
        const EntityId entity = primary_;
        QObject::connect(name, &QLineEdit::editingFinished, &panel_,
                         [this, name, entity]() {
                           if (!rebuilding_) {
                             (void)editor_->scene().rename(
                                 entity, name->text().toUtf8().toStdString());
                           }
                         });
      }
    }

    if (visibility.has_value) {
      const bool hidden = !visibility.all_true;
      add_mixed_checkbox(section, "general.hidden",
                         localized("ui.inspector.hidden", "Hidden"), hidden,
                         visibility.mixed(),
                         [this, editable_selected](bool next_hidden) {
                           std::vector<QuaderSceneVisibilityEdit> edits;
                           edits.reserve(editable_selected.size());
                           for (const EntityId entity : editable_selected) {
                             if (const VisibilityComponent *current =
                                     document_->visibility(entity)) {
                               edits.push_back({.entity = entity,
                                                .before = current->visible,
                                                .after = !next_hidden});
                             }
                           }
                           (void)editor_->scene().set_visibility(
                               std::span<const QuaderSceneVisibilityEdit>(
                                   edits.data(), edits.size()));
                         });
    }

    if (lock.has_value) {
      add_mixed_checkbox(
          section, "general.locked", localized("ui.inspector.locked", "Locked"),
          lock.all_true, lock.mixed(),
          [this, editable_selected](bool next_locked) {
            std::vector<QuaderSceneLockEdit> edits;
            edits.reserve(editable_selected.size());
            for (const EntityId entity : editable_selected) {
              if (const LockComponent *current = document_->lock(entity)) {
                edits.push_back({.entity = entity,
                                 .before = current->locked,
                                 .after = next_locked});
              }
            }
            (void)editor_->scene().set_locks(
                std::span<const QuaderSceneLockEdit>(edits.data(),
                                                     edits.size()));
          });
    }
  }

  void build_transform_section() {
    const TransformComponent *transform = document_->transform(primary_);
    if (transform == nullptr) {
      return;
    }
    Section section = add_section(
        "transform", localized("ui.inspector.transform", "Transform"));

    if (selection_.size() > 1u) {
      std::size_t transform_count = 0;
      for (const EntityId entity : selection_) {
        transform_count += document_->transform(entity) != nullptr ? 1u : 0u;
      }
      const std::string value = localization_->format(
          "ui.inspector.spatial_entities", "{count} spatial entities",
          {{"count", std::to_string(transform_count)}});
      add_read_only_value(section, "transform.selection",
                          localized("ui.inspector.selection", "Selection"),
                          qt_text(value));
    }

    const std::array<double, 3> position{
        transform->position.x,
        transform->position.y,
        transform->position.z,
    };
    add_transform_row(section, "transform.position",
                      localized("ui.inspector.position", "Position"),
                      TransformEditKind::Position, position,
                      mixed_position_axes(*document_, selection_, *transform),
                      0.05, -std::numeric_limits<double>::max(),
                      std::numeric_limits<double>::max());

    const QVec3 rotation = positive_euler_radians(transform->rotation_euler);
    const std::array<double, 3> rotation_degrees{
        rotation.x * kRadToDeg,
        rotation.y * kRadToDeg,
        rotation.z * kRadToDeg,
    };
    add_transform_row(section, "transform.rotation",
                      localized("ui.inspector.rotation", "Rotation"),
                      TransformEditKind::Rotation, rotation_degrees,
                      mixed_rotation_axes(*document_, selection_, *transform),
                      0.5, -std::numeric_limits<double>::max(),
                      std::numeric_limits<double>::max());

    const std::array<double, 3> scale{
        transform->scale.x,
        transform->scale.y,
        transform->scale.z,
    };
    add_transform_row(section, "transform.scale",
                      localized("ui.inspector.scale", "Scale"),
                      TransformEditKind::Scale, scale,
                      mixed_scale_axes(*document_, selection_, *transform),
                      0.02, 0.001, 1000.0);
  }

  void add_transform_row(Section &section, std::string stable_id,
                         const QString &label, TransformEditKind kind,
                         const std::array<double, 3> &initial_values,
                         const std::array<bool, 3> &mixed, double step,
                         double minimum, double maximum) {
    auto values = std::make_shared<std::array<double, 3>>(initial_values);
    auto *field = new QWidget(section.editor_parent);
    field->setObjectName(stable_name(stable_id));
    field->setMinimumWidth(0);
    field->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto *layout = new QGridLayout(field);
    layout->setContentsMargins(0, 0, 0, 0);
    constexpr std::array<const char *, 3> kAxes{{"x", "y", "z"}};
    constexpr std::array<const char *, 3> kAxisLabels{{"X", "Y", "Z"}};
    constexpr std::array<quader_editor_core::ViewAxis, 3> kAxisKinds{{
        quader_editor_core::ViewAxis::X,
        quader_editor_core::ViewAxis::Y,
        quader_editor_core::ViewAxis::Z,
    }};

    for (std::size_t axis = 0; axis < kAxes.size(); ++axis) {
      const int label_column = static_cast<int>(axis * 2u);
      const int field_column = label_column + 1;
      auto *axis_label =
          new QLabel(QString::fromLatin1(kAxisLabels[axis]), field);
      axis_label->setObjectName(
          stable_name(stable_id + "." + kAxes[axis] + ".label"));
      axis_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
      apply_qt_semantic_axis_label_color(*axis_label, kAxisKinds[axis]);
      layout->addWidget(axis_label, 0, label_column);
      layout->setColumnStretch(field_column, 1);

      if (mixed[axis]) {
        auto *mixed_field =
            new QLineEdit(localized("ui.inspector.mixed", "Mixed"), field);
        mixed_field->setObjectName(stable_name(stable_id + "." + kAxes[axis]));
        mixed_field->setReadOnly(true);
        mixed_field->setEnabled(false);
        mixed_field->setMinimumWidth(0);
        mixed_field->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        layout->addWidget(mixed_field, 0, field_column);
        continue;
      }

      auto *spin = new InspectorDoubleSpinBox(field);
      spin->setObjectName(stable_name(stable_id + "." + kAxes[axis]));
      spin->setRange(minimum, maximum);
      spin->setSingleStep(step);
      spin->setDecimals(3);
      spin->setKeyboardTracking(true);
      spin->setValue((*values)[axis]);
      spin->setMinimumWidth(0);
      spin->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
      layout->addWidget(spin, 0, field_column);

      QObject::connect(spin, &QDoubleSpinBox::valueChanged, &panel_,
                       [this, kind, axis, values](double next) {
                         if (rebuilding_) {
                           return;
                         }
                         (*values)[axis] = next;
                         preview_transform_edit(kind, *values);
                       });
      QObject::connect(spin, &QDoubleSpinBox::editingFinished, &panel_,
                       [this, kind]() {
                         if (!rebuilding_) {
                           finish_transform_edit(kind);
                         }
                       });
      spin->cancel_handler = [this, kind]() {
        if (active_transform_edit_ && active_transform_edit_->kind == kind) {
          cancel_active_transform_edit();
          force_rebuild_ = true;
        }
      };
    }
    add_row(section, stable_id, label, field);
  }

  bool transform_edit_matches(TransformEditKind kind) const {
    return active_transform_edit_ && active_transform_edit_->kind == kind &&
           active_transform_edit_->primary == primary_ &&
           active_transform_edit_->selection == selection_;
  }

  bool begin_transform_edit(TransformEditKind kind,
                            const std::array<double, 3> &values) {
    if (transform_edit_matches(kind) &&
        active_transform_preview_.preview_active()) {
      return true;
    }
    if (preview_document_ == nullptr || editor_ == nullptr) {
      return false;
    }

    cancel_active_transform_edit();
    const TextureLockSettings texture_locks{
        .texture_lock = editor_->preferences().texture_lock_enabled(),
        .texture_lock_scale =
            editor_->preferences().texture_lock_scale_enabled(),
    };
    bool began = false;
    switch (kind) {
    case TransformEditKind::Position:
      began = active_transform_preview_
                  .begin_move_selected_to_primary_position_preview(
                      *preview_document_, primary_, "Set Transform",
                      texture_locks);
      break;
    case TransformEditKind::Rotation:
      began = preview_assets_ != nullptr &&
              active_transform_preview_
                  .begin_rotate_selected_to_primary_euler_preview(
                      *preview_document_, *preview_assets_, primary_,
                      "Set Transform", texture_locks);
      break;
    case TransformEditKind::Scale:
      began = preview_assets_ != nullptr &&
              active_transform_preview_
                  .begin_scale_selected_to_primary_scale_preview(
                      *preview_document_, *preview_assets_, primary_,
                      "Set Transform", texture_locks);
      break;
    }
    if (!began) {
      return false;
    }

    active_transform_edit_ = TransformEditState{
        .kind = kind,
        .primary = primary_,
        .selection = selection_,
        .values = {static_cast<float>(values[0]), static_cast<float>(values[1]),
                   static_cast<float>(values[2])},
    };
    return true;
  }

  void preview_transform_edit(TransformEditKind kind,
                              const std::array<double, 3> &values) {
    if (!begin_transform_edit(kind, values) ||
        !active_transform_preview_.preview_active()) {
      force_rebuild_ = true;
      return;
    }
    active_transform_edit_->values = {
        static_cast<float>(values[0]),
        static_cast<float>(values[1]),
        static_cast<float>(values[2]),
    };

    DocumentCommandResult result;
    switch (kind) {
    case TransformEditKind::Position:
      result = active_transform_preview_.preview_selected_to_primary_position(
          *preview_document_, primary_,
          {active_transform_edit_->values[0], active_transform_edit_->values[1],
           active_transform_edit_->values[2]});
      break;
    case TransformEditKind::Rotation:
      result = active_transform_preview_.preview_selected_to_primary_euler(
          *preview_document_, primary_,
          {active_transform_edit_->values[0] * kDegToRad,
           active_transform_edit_->values[1] * kDegToRad,
           active_transform_edit_->values[2] * kDegToRad});
      break;
    case TransformEditKind::Scale:
      result = active_transform_preview_.preview_selected_to_primary_scale(
          *preview_document_, primary_,
          {active_transform_edit_->values[0], active_transform_edit_->values[1],
           active_transform_edit_->values[2]});
      break;
    }
    if (!result.ok) {
      cancel_active_transform_edit();
      force_rebuild_ = true;
    }
  }

  void finish_transform_edit(TransformEditKind kind) {
    if (!transform_edit_matches(kind)) {
      return;
    }
    if (active_transform_preview_.preview_active()) {
      (void)active_transform_preview_.commit(*editor_);
    }
    active_transform_edit_.reset();
    force_rebuild_ = true;
  }

  void cancel_active_transform_edit() {
    if (active_transform_preview_.preview_active()) {
      (void)active_transform_preview_.cancel();
    }
    active_transform_edit_.reset();
  }

  void build_scene_settings_sections() {
    const SceneSettingsComponent *settings =
        document_->scene_settings(primary_);
    if (settings == nullptr) {
      return;
    }
    const EntityId entity = primary_;

    Section environment = add_section(
        "environment", localized("ui.inspector.environment", "Environment"));
    auto *environment_combo = new QComboBox(environment.editor_parent);
    environment_combo->setObjectName(
        stable_name("environment.environment_preset"));
    int environment_index = -1;
    int descriptor_index = 0;
    for (const SceneEnvironmentDescriptor &descriptor :
         scene_environment_catalog()) {
      environment_combo->addItem(
          localized(descriptor.label_key, descriptor.fallback_label),
          qt_text(descriptor.id));
      if (descriptor.id == settings->environment.environment_id) {
        environment_index = descriptor_index;
      }
      ++descriptor_index;
    }
    environment_combo->setCurrentIndex(environment_index);
    add_row(environment, "environment.environment_preset",
            localized("ui.inspector.environment_preset", "Preset"),
            environment_combo);
    QObject::connect(environment_combo, &QComboBox::activated, &panel_,
                     [this, environment_combo, entity](int index) {
                       if (rebuilding_ || index < 0) {
                         return;
                       }
                       const std::string id = environment_combo->itemData(index)
                                                  .toString()
                                                  .toUtf8()
                                                  .toStdString();
                       apply_scene_settings(
                           entity, [id](SceneSettingsComponent &next) {
                             next.environment.environment_id = id;
                           });
                     });
    add_checkbox(environment, "environment.enabled",
                 localized("ui.inspector.enabled", "Enabled"),
                 settings->environment.enabled, [this, entity](bool enabled) {
                   apply_scene_settings(
                       entity, [enabled](SceneSettingsComponent &next) {
                         next.environment.enabled = enabled;
                       });
                 });
    add_checkbox(
        environment, "environment.background",
        localized("ui.inspector.background", "Background"),
        settings->environment.show_background, [this, entity](bool visible) {
          apply_scene_settings(entity, [visible](SceneSettingsComponent &next) {
            next.environment.show_background = visible;
          });
        });
    add_color_button(environment, "environment.clear_color",
                     localized("ui.inspector.clear_color", "Clear Color"),
                     settings->environment.clear_color,
                     [this, entity](QVec3 color) {
                       apply_scene_settings(
                           entity, [color](SceneSettingsComponent &next) {
                             next.environment.clear_color = {
                                 std::clamp(color.x, 0.0f, 1.0f),
                                 std::clamp(color.y, 0.0f, 1.0f),
                                 std::clamp(color.z, 0.0f, 1.0f),
                             };
                           });
                     });
    add_continuous_number(environment, "environment.intensity",
                          localized("ui.inspector.intensity", "Intensity"),
                          settings->environment.intensity, 0.0, 16.0, 0.01, 3,
                          [this, entity](double value, std::string merge_key) {
                            apply_scene_settings(
                                entity,
                                [value](SceneSettingsComponent &next) {
                                  next.environment.intensity =
                                      static_cast<float>(value);
                                },
                                std::move(merge_key));
                          });
    add_continuous_number(
        environment, "environment.background_intensity",
        localized("ui.inspector.background_intensity", "Background Intensity"),
        settings->environment.background_intensity, 0.0, 16.0, 0.01, 3,
        [this, entity](double value, std::string merge_key) {
          apply_scene_settings(
              entity,
              [value](SceneSettingsComponent &next) {
                next.environment.background_intensity =
                    static_cast<float>(value);
              },
              std::move(merge_key));
        });
    add_continuous_number(environment, "environment.rotation",
                          localized("ui.inspector.rotation", "Rotation"),
                          settings->environment.rotation_y * kRadToDeg, -360.0,
                          360.0, 0.5, 2,
                          [this, entity](double value, std::string merge_key) {
                            apply_scene_settings(
                                entity,
                                [value](SceneSettingsComponent &next) {
                                  next.environment.rotation_y =
                                      static_cast<float>(value) * kDegToRad;
                                },
                                std::move(merge_key));
                          });

    Section camera =
        add_section("camera", localized("ui.inspector.camera", "Camera"));
    add_continuous_number(camera, "camera.aperture",
                          localized("ui.inspector.aperture", "Aperture"),
                          settings->camera.aperture, 0.01, 128.0, 0.05, 2,
                          [this, entity](double value, std::string merge_key) {
                            apply_scene_settings(
                                entity,
                                [value](SceneSettingsComponent &next) {
                                  next.camera.aperture =
                                      static_cast<float>(value);
                                },
                                std::move(merge_key));
                          });
    add_continuous_number(
        camera, "camera.shutter",
        localized("ui.inspector.shutter_speed", "Shutter"),
        settings->camera.shutter_speed, 0.000001, 10.0, 0.0001, 6,
        [this, entity](double value, std::string merge_key) {
          apply_scene_settings(
              entity,
              [value](SceneSettingsComponent &next) {
                next.camera.shutter_speed = static_cast<float>(value);
              },
              std::move(merge_key));
        });
    add_continuous_number(camera, "camera.sensitivity",
                          localized("ui.inspector.sensitivity", "ISO"),
                          settings->camera.sensitivity, 1.0, 51200.0, 1.0, 0,
                          [this, entity](double value, std::string merge_key) {
                            apply_scene_settings(
                                entity,
                                [value](SceneSettingsComponent &next) {
                                  next.camera.sensitivity =
                                      static_cast<float>(value);
                                },
                                std::move(merge_key));
                          });
    add_continuous_number(
        camera, "camera.near", localized("ui.inspector.near_plane", "Near"),
        settings->camera.near_plane, 0.001, 10000.0, 0.001, 4,
        [this, entity](double value, std::string merge_key) {
          apply_scene_settings(
              entity,
              [value](SceneSettingsComponent &next) {
                next.camera.near_plane = static_cast<float>(value);
              },
              std::move(merge_key));
        });
    add_continuous_number(
        camera, "camera.far", localized("ui.inspector.far_plane", "Far"),
        settings->camera.far_plane, 0.001, 1000000.0, 1.0, 2,
        [this, entity](double value, std::string merge_key) {
          apply_scene_settings(
              entity,
              [value](SceneSettingsComponent &next) {
                next.camera.far_plane = static_cast<float>(value);
              },
              std::move(merge_key));
        });

    Section post = add_section(
        "post_process", localized("ui.inspector.post_process", "Post Process"));
    add_checkbox(post, "post_process.enabled",
                 localized("ui.inspector.enabled", "Enabled"),
                 settings->post_process.enabled, [this, entity](bool enabled) {
                   apply_scene_settings(
                       entity, [enabled](SceneSettingsComponent &next) {
                         next.post_process.enabled = enabled;
                       });
                 });
    add_checkbox(
        post, "post_process.anti_aliasing",
        localized("ui.inspector.anti_aliasing", "Anti-Aliasing"),
        settings->post_process.anti_aliasing, [this, entity](bool enabled) {
          apply_scene_settings(entity, [enabled](SceneSettingsComponent &next) {
            next.post_process.anti_aliasing = enabled;
          });
        });
    add_checkbox(
        post, "post_process.dithering",
        localized("ui.inspector.dithering", "Dithering"),
        settings->post_process.dithering, [this, entity](bool enabled) {
          apply_scene_settings(entity, [enabled](SceneSettingsComponent &next) {
            next.post_process.dithering = enabled;
          });
        });
    add_enum_combo<SceneToneMapping>(
        post, "post_process.tone_mapping",
        localized("ui.inspector.tone_mapping", "Tone Mapping"),
        settings->post_process.tone_mapping,
        {
            {SceneToneMapping::Linear,
             localized("ui.inspector.tone_mapping.linear", "Linear")},
            {SceneToneMapping::AcesLegacy,
             localized("ui.inspector.tone_mapping.aces_legacy", "ACES Legacy")},
            {SceneToneMapping::Aces,
             localized("ui.inspector.tone_mapping.aces", "ACES")},
            {SceneToneMapping::Filmic,
             localized("ui.inspector.tone_mapping.filmic", "Filmic")},
            {SceneToneMapping::DisplayRange,
             localized("ui.inspector.tone_mapping.display_range",
                       "Display Range")},
        },
        [this, entity](SceneToneMapping value) {
          apply_scene_settings(entity, [value](SceneSettingsComponent &next) {
            next.post_process.tone_mapping = value;
          });
        });
    add_checkbox(
        post, "post_process.ambient_occlusion",
        localized("ui.inspector.ambient_occlusion", "Ambient Occlusion"),
        settings->post_process.ambient_occlusion, [this, entity](bool enabled) {
          apply_scene_settings(entity, [enabled](SceneSettingsComponent &next) {
            next.post_process.ambient_occlusion = enabled;
          });
        });
    if (settings->post_process.ambient_occlusion) {
      add_continuous_number(
          post, "post_process.ambient_occlusion_intensity",
          localized("ui.inspector.ambient_occlusion_intensity", "AO Intensity"),
          settings->post_process.ambient_occlusion_intensity, 0.0, 8.0, 0.01, 3,
          [this, entity](double value, std::string merge_key) {
            apply_scene_settings(
                entity,
                [value](SceneSettingsComponent &next) {
                  next.post_process.ambient_occlusion_intensity =
                      static_cast<float>(value);
                },
                std::move(merge_key));
          });
      add_continuous_number(
          post, "post_process.ambient_occlusion_radius",
          localized("ui.inspector.ambient_occlusion_radius", "AO Radius"),
          settings->post_process.ambient_occlusion_radius, 0.0, 10.0, 0.01, 3,
          [this, entity](double value, std::string merge_key) {
            apply_scene_settings(
                entity,
                [value](SceneSettingsComponent &next) {
                  next.post_process.ambient_occlusion_radius =
                      static_cast<float>(value);
                },
                std::move(merge_key));
          });
    }
    add_checkbox(
        post, "post_process.bloom", localized("ui.inspector.bloom", "Bloom"),
        settings->post_process.bloom, [this, entity](bool enabled) {
          apply_scene_settings(entity, [enabled](SceneSettingsComponent &next) {
            next.post_process.bloom = enabled;
          });
        });
    add_continuous_number(
        post, "post_process.bloom_strength",
        localized("ui.inspector.bloom_strength", "Bloom Strength"),
        settings->post_process.bloom_strength, 0.0, 1.0, 0.01, 3,
        [this, entity](double value, std::string merge_key) {
          apply_scene_settings(
              entity,
              [value](SceneSettingsComponent &next) {
                next.post_process.bloom_strength = static_cast<float>(value);
              },
              std::move(merge_key));
        });
  }

  void build_mesh_sections() {
    const MeshComponent *mesh = document_->mesh(primary_);
    if (mesh == nullptr) {
      return;
    }
    const EntityId entity = primary_;

    Section export_section =
        add_section("export", localized("ui.inspector.export", "Export"));
    add_checkbox(export_section, "export.editor_only",
                 localized("ui.inspector.editor_only", "Editor Only"),
                 mesh->editor_only, [this, entity](bool enabled) {
                   apply_mesh_component(entity, [enabled](MeshComponent &next) {
                     next.editor_only = enabled;
                   });
                 });

    Section lighting =
        add_section("lighting", localized("ui.inspector.lighting", "Lighting"));
    add_checkbox(lighting, "lighting.cast_shadows",
                 localized("ui.inspector.cast_shadows", "Cast Shadows"),
                 mesh->cast_shadows, [this, entity](bool enabled) {
                   apply_mesh_component(entity, [enabled](MeshComponent &next) {
                     next.cast_shadows = enabled;
                   });
                 });
    add_checkbox(lighting, "lighting.double_sided",
                 localized("ui.inspector.double_sided", "Double sided"),
                 mesh->double_sided, [this, entity](bool enabled) {
                   apply_mesh_component(entity, [enabled](MeshComponent &next) {
                     next.double_sided = enabled;
                   });
                 });

    Section physics =
        add_section("physics", localized("ui.inspector.physics", "Physics"));
    add_checkbox(
        physics, "physics.generate_collisions",
        localized("ui.inspector.generate_collisions", "Generate Collisions"),
        mesh->generate_collisions, [this, entity](bool enabled) {
          apply_mesh_component(entity, [enabled](MeshComponent &next) {
            next.generate_collisions = enabled;
          });
        });
    add_enum_combo<MeshCollisionType>(
        physics, "physics.collision_type",
        localized("ui.inspector.collision_type", "Collision Type"),
        mesh->collision_type,
        {
            {MeshCollisionType::Box,
             localized("ui.inspector.collision_type.box", "Box")},
            {MeshCollisionType::Sphere,
             localized("ui.inspector.collision_type.sphere", "Sphere")},
            {MeshCollisionType::Cylinder,
             localized("ui.inspector.collision_type.cylinder", "Cylinder")},
            {MeshCollisionType::ConvexHull,
             localized("ui.inspector.collision_type.convex_hull",
                       "Convex Hull")},
            {MeshCollisionType::Mesh,
             localized("ui.inspector.collision_type.mesh", "Mesh")},
        },
        [this, entity](MeshCollisionType value) {
          apply_mesh_component(entity, [value](MeshComponent &next) {
            next.collision_type = value;
          });
        });
    add_checkbox(
        physics, "physics.display_collision",
        localized("ui.inspector.display_collision", "Display Collision"),
        mesh->display_collision, [this, entity](bool enabled) {
          apply_mesh_component(entity, [enabled](MeshComponent &next) {
            next.display_collision = enabled;
          });
        });
    add_enum_combo<MeshPhysicsBodyType>(
        physics, "physics.body_type",
        localized("ui.inspector.body_type", "Body type"), mesh->body_type,
        {
            {MeshPhysicsBodyType::StaticBody,
             localized("ui.inspector.body_type.static_body", "Static Body")},
            {MeshPhysicsBodyType::RigidBody,
             localized("ui.inspector.body_type.rigid_body", "Rigid Body")},
        },
        [this, entity](MeshPhysicsBodyType value) {
          apply_mesh_component(
              entity, [value](MeshComponent &next) { next.body_type = value; });
        });
  }

  void build_light_section() {
    const LightComponent *light = document_->light(primary_);
    if (light == nullptr) {
      return;
    }
    const EntityId entity = primary_;
    Section section =
        add_section("light", localized("ui.inspector.light", "Light"));
    add_enum_combo<LightType>(
        section, "light.type", localized("ui.inspector.light_type", "Type"),
        light->type,
        {
            {LightType::Directional,
             localized("ui.inspector.light_type.directional", "Directional")},
            {LightType::Point,
             localized("ui.inspector.light_type.point", "Point")},
            {LightType::Spot,
             localized("ui.inspector.light_type.spot", "Spot")},
        },
        [this, entity](LightType value) {
          apply_light_component(
              entity, [value](LightComponent &next) { next.type = value; });
        });
    add_color_button(section, "light.color",
                     localized("ui.inspector.color", "Color"), light->color,
                     [this, entity](QVec3 color) {
                       apply_light_component(
                           entity, [color](LightComponent &next) {
                             next.color = {
                                 std::clamp(color.x, 0.0f, 1.0f),
                                 std::clamp(color.y, 0.0f, 1.0f),
                                 std::clamp(color.z, 0.0f, 1.0f),
                             };
                           });
                     });
    add_continuous_number(section, "light.intensity",
                          localized("ui.inspector.intensity", "Intensity"),
                          light->intensity, 0.0, 16.0, 0.01, 3,
                          [this, entity](double value, std::string merge_key) {
                            apply_light_component(
                                entity,
                                [value](LightComponent &next) {
                                  next.intensity = static_cast<float>(value);
                                },
                                std::move(merge_key));
                          });
    if (light->type == LightType::Point || light->type == LightType::Spot) {
      add_continuous_number(
          section, "light.range", localized("ui.inspector.range", "Range"),
          light->range, kInspectorLightRangeMin, kInspectorLightRangeSliderMax,
          0.1, 2, [this, entity](double value, std::string merge_key) {
            apply_light_component(
                entity,
                [value](LightComponent &next) {
                  next.range = static_cast<float>(value);
                },
                std::move(merge_key));
          });
    }
    add_checkbox(section, "light.cast_shadows",
                 localized("ui.inspector.cast_shadows", "Cast Shadows"),
                 light->casts_shadows, [this, entity](bool enabled) {
                   apply_light_component(entity,
                                         [enabled](LightComponent &next) {
                                           next.casts_shadows = enabled;
                                         });
                 });
  }

  void build_component_count_section() {
    std::size_t mesh_count = 0;
    std::size_t light_count = 0;
    for (const EntityId entity : selection_) {
      mesh_count += document_->mesh(entity) != nullptr ? 1u : 0u;
      light_count += document_->light(entity) != nullptr ? 1u : 0u;
    }
    Section section = add_section(
        "components", localized("ui.inspector.components", "Components"));
    add_read_only_value(section, "components.meshes",
                        localized("ui.inspector.meshes", "Meshes"),
                        QString::number(static_cast<qulonglong>(mesh_count)));
    add_read_only_value(section, "components.lights",
                        localized("ui.inspector.lights", "Lights"),
                        QString::number(static_cast<qulonglong>(light_count)));
  }

  InspectorPanel &panel_;
  QTreeWidget *tree_ = nullptr;
  QuaderEditorApi *editor_ = nullptr;
  const SceneDocument *document_ = nullptr;
  const ModelingSessionService *modeling_ = nullptr;
  LocalizationService *localization_ = nullptr;
  SceneDocument *preview_document_ = nullptr;
  const AssetRegistry *preview_assets_ = nullptr;
  InspectorMaterialPresentationSnapshot material_presentation_;
  SceneTransformDragService active_transform_preview_;
  std::optional<TransformEditState> active_transform_edit_;
  UIContinuousEditTracker active_continuous_edit_;
  QColorDialog *active_color_dialog_ = nullptr;
  std::vector<EntityId> selection_;
  EntityId primary_ = kInvalidEntityId;
  std::unordered_map<std::string, bool> expanded_sections_;
  std::uint64_t built_scene_revision_ =
      std::numeric_limits<std::uint64_t>::max();
  std::uint64_t built_modeling_content_revision_ =
      std::numeric_limits<std::uint64_t>::max();
  std::uint64_t built_modeling_selection_revision_ =
      std::numeric_limits<std::uint64_t>::max();
  std::uint64_t built_material_presentation_revision_ =
      std::numeric_limits<std::uint64_t>::max();
  std::string built_locale_;
  std::string active_continuous_id_;
  bool continuous_edit_active_ = false;
  bool rebuilding_ = false;
  bool force_rebuild_ = true;
  bool adjusting_label_column_width_ = false;
};

InspectorPanel::InspectorPanel(QWidget *parent)
    : QWidget(parent), impl_(std::make_unique<Impl>(*this)) {}

InspectorPanel::~InspectorPanel() = default;

void InspectorPanel::set_material_presentation(
    InspectorMaterialPresentationSnapshot snapshot) {
  impl_->set_material_presentation(std::move(snapshot));
}

void InspectorPanel::refresh(UIContext &context) { impl_->refresh(context); }

} // namespace quader
