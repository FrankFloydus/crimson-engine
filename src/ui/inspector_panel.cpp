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
#include "scene/light_properties.hpp"
#include "scene/scene_document.hpp"
#include "scene/transform_pivot.hpp"
#include "ui/qt_color_swatch_button.hpp"
#include "ui/qt_double_spin_box.hpp"
#include "ui/qt_semantic_axis_colors.hpp"
#include "ui/qt_tool_settings_number_slider.hpp"
#include "ui/qt_ui_scale_metrics.hpp"
#include "ui/ui_context.hpp"
#include "ui/ui_continuous_edit_tracker.hpp"
#include "ui/ui_panel.hpp"

#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QEvent>
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
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStandardItemModel>
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
constexpr float kInspectorLightRangeMin = 0.001f;
constexpr float kInspectorLightRangeSliderMax = 100.0f;
constexpr int kInspectorMinimumValueColumnWidth = 96;
constexpr int kInspectorMaterialPreviewSize = 56;
constexpr int kInspectorMaterialActionSize = 24;
constexpr int kInspectorMaterialActionIconSize = 16;
constexpr std::array<int, 10> kInspectorMaterialActionRasterExtents = {
    16, 24, 32, 48, 64, 96, 128, 192, 256, 384};

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
                               const QColor &color, int raster_extent) {
  raster_extent = std::max(1, raster_extent);
  QPixmap pixmap(raster_extent, raster_extent);
  pixmap.fill(Qt::transparent);

  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing, true);
  const qreal design_scale =
      static_cast<qreal>(raster_extent) /
      static_cast<qreal>(kInspectorMaterialActionIconSize);
  painter.scale(design_scale, design_scale);
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
  for (const int raster_extent : kInspectorMaterialActionRasterExtents) {
    icon.addPixmap(material_action_pixmap(
                       action, palette.color(QPalette::ButtonText),
                       raster_extent),
                   QIcon::Normal, QIcon::Off);
    icon.addPixmap(material_action_pixmap(
                       action,
                       palette.color(QPalette::Disabled, QPalette::ButtonText),
                       raster_extent),
                   QIcon::Disabled, QIcon::Off);
  }
  return icon;
}

/// Extends the stock spin box with an explicit cancel callback for previews.
class InspectorDoubleSpinBox final : public QtDoubleSpinBox {
public:
  explicit InspectorDoubleSpinBox(QWidget *parent = nullptr)
      : QtDoubleSpinBox(parent) {}

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

  struct MaterialRow {
    QTreeWidgetItem *item = nullptr;
    QHBoxLayout *layout = nullptr;
    QLabel *preview = nullptr;
    QPushButton *edit = nullptr;
    QPushButton *replace = nullptr;
    QtMaterialPresentationItem material;
    QSize preview_logical_bounds;
    qreal preview_device_pixel_ratio = 0.0;
  };

  /// Retains the numeric value independently from the slider's bounded span.
  struct NumberSlider {
    QPointer<QDoubleSpinBox> number;
    QPointer<QtToolSettingsNumberSlider> slider;
    QtToolSettingsNumberSliderMapping mapping;
    double numeric_minimum = 0.0;
    double numeric_maximum = 0.0;
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

  ~Impl() {
    // Child destruction can publish header geometry/focus signals. Remove the
    // tree while its callback state still exists, before QWidget tears down
    // panel children after the Impl unique_ptr has already been destroyed.
    QObject::disconnect(tree_, nullptr, &panel_, nullptr);
    QObject::disconnect(tree_->header(), nullptr, &panel_, nullptr);
    rebuilding_ = true;
    material_rows_.clear();
    delete tree_;
    tree_ = nullptr;
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

  void abandon_runtime() noexcept {
    ++runtime_binding_generation_;
    active_transform_preview_.abandon();
    active_transform_edit_.reset();
    active_continuous_edit_.reset();
    continuous_edit_active_ = false;
    active_continuous_id_.clear();
    selection_.clear();
    primary_ = kInvalidEntityId;
    editor_ = nullptr;
    document_ = nullptr;
    modeling_ = nullptr;
    localization_ = nullptr;
    preview_document_ = nullptr;
    preview_assets_ = nullptr;
    force_rebuild_ = true;
    if (active_color_dialog_ != nullptr) {
      active_color_dialog_->reject();
    }
    active_color_dialog_ = nullptr;
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
    if (owner_changed && (document_ != nullptr || modeling_ != nullptr)) {
      // The former SceneSession may already be gone when a host rebind arrives.
      // Disarm previews without restoring through their non-owning document.
      abandon_runtime();
    }
    const bool selection_changed =
        owner_changed || next_selection != selection_ ||
        next_primary != primary_;
    if (!owner_changed && selection_changed &&
        active_color_dialog_ != nullptr) {
      active_color_dialog_->reject();
    }
    if (!owner_changed && selection_changed) {
      cancel_active_transform_edit();
      active_continuous_edit_.reset();
      continuous_edit_active_ = false;
      active_continuous_id_.clear();
    }
    if (selection_changed) {
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
    const bool layout_changed =
        force_rebuild_ ||
        built_modeling_content_revision_ != summary.content_revision ||
        built_modeling_selection_revision_ != summary.selection_revision ||
        built_material_presentation_revision_ !=
            material_presentation_.revision ||
        built_locale_ != localization_->current_locale();
    if (!layout_changed && built_scene_revision_ == document_->revision()) {
      return;
    }
    if (!selection_changed &&
        (active_transform_edit_.has_value() || continuous_edit_active_)) {
      return;
    }

    if (!layout_changed && refresh_light_properties()) {
      return;
    }

    if (!selection_changed && active_color_dialog_ != nullptr) {
      return;
    }

    rebuild(summary);
  }

  void apply_ui_scale(const QtUiScaleMetrics &metrics) {
    if (ui_scale_metrics_.revision() == metrics.revision() &&
        qFuzzyCompare(static_cast<qreal>(ui_scale_metrics_.factor()),
                      static_cast<qreal>(metrics.factor()))) {
      return;
    }

    QHeaderView *header = tree_->header();
    const bool label_column_uses_scaled_default =
        header->sectionSize(0) == scaled_default_label_column_width_;
    ui_scale_metrics_ = metrics;
    header->setMinimumSectionSize(metrics.px(64));
    scaled_default_label_column_width_ = metrics.px(140);
    if (label_column_uses_scaled_default) {
      header->resizeSection(0, scaled_default_label_column_width_);
    }

    const QSize preview_size = metrics.size(
        QSize(kInspectorMaterialPreviewSize, kInspectorMaterialPreviewSize));
    const QSize action_size = metrics.size(
        QSize(kInspectorMaterialActionSize, kInspectorMaterialActionSize));
    const QSize action_icon_size = metrics.size(QSize(
        kInspectorMaterialActionIconSize, kInspectorMaterialActionIconSize));
    for (MaterialRow &row : material_rows_) {
      row.item->setSizeHint(
          0, QSize(0, metrics.px(kInspectorMaterialPreviewSize + 8)));
      row.layout->setContentsMargins(metrics.margins(QMargins(4, 4, 4, 4)));
      row.layout->setSpacing(metrics.px(6));
      row.preview->setFixedSize(preview_size);
      row.edit->setFixedSize(action_size);
      row.edit->setIconSize(action_icon_size);
      row.replace->setFixedSize(action_size);
      row.replace->setIconSize(action_icon_size);
    }
    refresh_material_preview_rasters();

    clamp_label_column_width();
    panel_.updateGeometry();
    tree_->viewport()->update();
  }

  void refresh_material_preview_rasters() {
    for (MaterialRow &row : material_rows_) {
      refresh_material_preview_raster(row);
    }
  }

  void refresh_material_action_icons() {
    if (material_rows_.empty()) {
      return;
    }
    const QIcon edit_icon = material_action_icon(
        InspectorMaterialActionIcon::Edit, panel_.palette());
    const QIcon replace_icon = material_action_icon(
        InspectorMaterialActionIcon::Replace, panel_.palette());
    for (MaterialRow &row : material_rows_) {
      row.edit->setIcon(edit_icon);
      row.replace->setIcon(replace_icon);
    }
    tree_->viewport()->update();
  }

private:
  [[nodiscard]] int scaled_px(int baseline) const {
    return ui_scale_metrics_.px(baseline);
  }

  void refresh_material_preview_raster(MaterialRow &row) {
    const QSize logical_bounds = row.preview->size();
    const qreal reported_device_pixel_ratio = row.preview->devicePixelRatioF();
    const qreal device_pixel_ratio =
        std::isfinite(reported_device_pixel_ratio) &&
                reported_device_pixel_ratio > 0.0
            ? std::max<qreal>(1.0, reported_device_pixel_ratio)
            : 1.0;
    if (row.preview_logical_bounds == logical_bounds &&
        qFuzzyCompare(row.preview_device_pixel_ratio, device_pixel_ratio)) {
      return;
    }
    row.preview->setPixmap(qt_material_preview_pixmap(
        row.material, logical_bounds, device_pixel_ratio));
    row.preview_logical_bounds = logical_bounds;
    row.preview_device_pixel_ratio = device_pixel_ratio;
  }

  void clamp_label_column_width() {
    if (adjusting_label_column_width_ || tree_ == nullptr ||
        !panel_.isVisible()) {
      return;
    }
    QHeaderView *header = tree_->header();
    const int maximum_label_width =
        std::max(header->minimumSectionSize(),
                 tree_->viewport()->width() -
                     scaled_px(kInspectorMinimumValueColumnWidth));
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
    material_rows_.clear();
    light_range_field_ = nullptr;
    light_intensity_field_ = {};
    light_falloff_field_ = {};
    light_temperature_field_ = {};
    light_color_button_ = nullptr;
    light_preset_combo_ = nullptr;
    light_color_mode_combo_ = nullptr;
    presented_light_.reset();
    tree_->clear();
  }

  bool refresh_light_properties() {
    const std::uint64_t scene_revision = document_->revision();
    const std::uint64_t light_revision = document_->light_property_revision();
    if (!presented_light_.has_value() ||
        scene_revision <= built_scene_revision_ ||
        light_revision <= built_light_property_revision_ ||
        scene_revision - built_scene_revision_ !=
            light_revision - built_light_property_revision_) {
      return false;
    }
    const LightComponent *light = document_->light(primary_);
    if (light == nullptr) {
      return false;
    }
    if (light->type != presented_light_->type ||
        light->casts_shadows != presented_light_->casts_shadows) {
      return false;
    }

    // Equal revision deltas exclude coalesced name, transform, component or
    // hierarchy changes before touching a retained widget. The component
    // comparison excludes properties that change the control layout.
    if (light_range_field_ != nullptr &&
        light->range != presented_light_->range) {
      const QSignalBlocker blocker(light_range_field_.data());
      if (light->range > light_range_field_->maximum()) {
        light_range_field_->setMaximum(light->range);
      }
      light_range_field_->setValue(light->range);
    }
    synchronize_light_controls(*light);
    presented_light_ = *light;
    built_scene_revision_ = scene_revision;
    built_light_property_revision_ = light_revision;
    return true;
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
      build_physics_section();
      if (selection_.size() > 1u) {
        build_component_count_section();
      }
    }

    built_scene_revision_ = document_->revision();
    built_light_property_revision_ = document_->light_property_revision();
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
    const QIcon edit_icon = material_action_icon(
        InspectorMaterialActionIcon::Edit, panel_.palette());
    const QIcon replace_icon = material_action_icon(
        InspectorMaterialActionIcon::Replace, panel_.palette());

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
      const int material_preview_size =
          scaled_px(kInspectorMaterialPreviewSize);
      const int material_action_size =
          scaled_px(kInspectorMaterialActionSize);
      const int material_action_icon_size =
          scaled_px(kInspectorMaterialActionIconSize);
      item->setSizeHint(
          0, QSize(0, scaled_px(kInspectorMaterialPreviewSize + 8)));

      auto *row = new QWidget(section.editor_parent);
      row->setObjectName(stable_name(stable_id + ".row"));
      auto *layout = new QHBoxLayout(row);
      layout->setContentsMargins(scaled_px(4), scaled_px(4), scaled_px(4),
                                 scaled_px(4));
      layout->setSpacing(scaled_px(6));

      auto *preview = new QLabel(row);
      preview->setObjectName(stable_name(stable_id + ".preview"));
      preview->setFixedSize(material_preview_size, material_preview_size);
      preview->setAlignment(Qt::AlignCenter);
      preview->setFrameShape(QFrame::StyledPanel);
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
      edit->setFixedSize(material_action_size, material_action_size);
      edit->setIcon(edit_icon);
      edit->setIconSize(
          QSize(material_action_icon_size, material_action_icon_size));
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
      replace->setFixedSize(material_action_size, material_action_size);
      replace->setIcon(replace_icon);
      replace->setIconSize(
          QSize(material_action_icon_size, material_action_icon_size));
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

      material_rows_.push_back(MaterialRow{
          .item = item,
          .layout = layout,
          .preview = preview,
          .edit = edit,
          .replace = replace,
          .material = material,
      });
      refresh_material_preview_raster(material_rows_.back());
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
                 std::function<void(Enum)> changed,
                 std::optional<Enum> supported = std::nullopt,
                 bool mixed = false) {
    auto *combo = new QComboBox(section.editor_parent);
    combo->setObjectName(stable_name(stable_id));
    int selected_index = -1;
    for (std::size_t index = 0; index < options.size(); ++index) {
      combo->addItem(options[index].second,
                     static_cast<int>(options[index].first));
      if (supported.has_value() && options[index].first != *supported) {
        const int item_index = static_cast<int>(index);
        combo->setItemText(
            item_index,
            localized("ui.inspector.unsupported_option", "%1 (unsupported)")
                .arg(options[index].second));
        auto *model = qobject_cast<QStandardItemModel *>(combo->model());
        model->item(item_index)->setEnabled(false);
      }
      if (options[index].first == current) {
        selected_index = static_cast<int>(index);
      }
    }
    if (mixed) {
      combo->setPlaceholderText(localized("ui.inspector.mixed", "Mixed"));
    }
    combo->setCurrentIndex(mixed ? -1 : selected_index);
    add_row(section, stable_id, label, combo);
    QObject::connect(combo, &QComboBox::activated, &panel_,
                     [this, combo, changed = std::move(changed)](int index) {
                       if (rebuilding_ || index < 0 ||
                           !(combo->model()->flags(
                                 combo->model()->index(index, 0)) &
                             Qt::ItemIsEnabled)) {
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

  void synchronize_number_slider(const NumberSlider &field, double value) {
    if (field.number == nullptr) {
      return;
    }
    const QSignalBlocker number_blocker(field.number.data());
    const double minimum = std::min(field.numeric_minimum, value);
    const double maximum = std::max(field.numeric_maximum, value);
    if (field.number->minimum() != minimum || field.number->maximum() != maximum) {
      field.number->setRange(minimum, maximum);
    }
    if (field.number->value() != value) {
      field.number->setValue(value);
    }
    if (field.slider != nullptr) {
      const int position = qt_tool_settings_number_slider_position(field.mapping, value);
      if (field.slider->value() != position) {
        const QSignalBlocker slider_blocker(field.slider.data());
        field.slider->setValue(position);
      }
    }
  }

  NumberSlider add_number_slider(
      Section &section, const std::string &stable_id, const QString &label,
      double value, double minimum, double slider_maximum,
      double numeric_maximum, double step,
      std::function<void(double, std::string)> changed, bool with_slider = true) {
    auto *container = new QWidget(section.editor_parent);
    auto *layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(scaled_px(4));
    NumberSlider field;
    field.numeric_minimum = minimum;
    field.numeric_maximum = numeric_maximum;
    auto *spin = new InspectorDoubleSpinBox(container);
    field.number = spin;
    spin->setObjectName(stable_name(stable_id));
    // Retain conversion precision; QtDoubleSpinBox formats at most four decimals.
    spin->setDecimals(12);
    spin->setSingleStep(step);
    spin->setKeyboardTracking(true);
    spin->setMinimumWidth(0);
    spin->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    if (with_slider) {
      field.mapping = qt_tool_settings_number_slider_mapping(
          minimum, slider_maximum, false, step);
      auto *slider = new QtToolSettingsNumberSlider(Qt::Horizontal, container);
      field.slider = slider;
      slider->setObjectName(stable_name(stable_id + ".slider"));
      slider->setRange(field.mapping.slider_minimum,
                        field.mapping.slider_maximum);
      slider->setSingleStep(qt_tool_settings_number_slider_step(
          field.mapping, value, step));
      slider->setMinimumWidth(scaled_px(24));
      layout->addWidget(slider, 1);
    }
    layout->addWidget(spin, 1);
    synchronize_number_slider(field, value);
    add_row(section, stable_id, label, container);

    const auto update = [this, stable_id, field,
                         changed = std::move(changed)](double requested) {
      if (rebuilding_) {
        return;
      }
      const double next = std::clamp(requested, field.numeric_minimum,
                                     field.numeric_maximum);
      const std::span<const EntityId> selection(selection_.data(),
                                                selection_.size());
      const std::string merge_key = active_continuous_edit_.ensure_merge_key(
          stable_id, primary_, selection);
      continuous_edit_active_ = true;
      active_continuous_id_ = stable_id;
      const std::uint64_t generation = runtime_binding_generation_;
      QPointer<InspectorPanel> panel_guard(&panel_);
      changed(next, merge_key);
      if (panel_guard == nullptr || generation != runtime_binding_generation_) {
        return;
      }
      if (const LightComponent *light = document_->light(primary_)) {
        synchronize_light_controls(*light);
      }
    };
    QObject::connect(spin, &QDoubleSpinBox::valueChanged, &panel_, update);
    const auto finish = [this, stable_id]() {
      if (rebuilding_ || active_continuous_id_ != stable_id) {
        return;
      }
      active_continuous_edit_.reset();
      continuous_edit_active_ = false;
      active_continuous_id_.clear();
    };
    QObject::connect(spin, &QDoubleSpinBox::editingFinished, &panel_, finish);
    if (field.slider != nullptr) {
      QObject::connect(field.slider.data(), &QSlider::valueChanged, &panel_,
                       [this, field, update, finish](int position) {
                         const std::uint64_t generation = runtime_binding_generation_;
                         QPointer<InspectorPanel> panel_guard(&panel_);
                         update(qt_tool_settings_number_slider_value(
                             field.mapping, position));
                         if (panel_guard == nullptr ||
                             generation != runtime_binding_generation_) {
                           return;
                         }
                         if (field.slider != nullptr &&
                             !field.slider->isSliderDown()) {
                           finish();
                         }
                       });
      QObject::connect(field.slider.data(), &QSlider::sliderReleased, &panel_,
                       finish);
    }
    return field;
  }

  void synchronize_light_controls(const LightComponent &light) {
    synchronize_number_slider(light_intensity_field_, light.intensity);
    synchronize_number_slider(light_falloff_field_, light.distance_attenuation);
    synchronize_number_slider(light_temperature_field_,
                               light.color_temperature_kelvin);
    const bool temperature_mode =
        light.color_mode == LightColorMode::ColorTemperature;
    if (light_temperature_field_.number != nullptr &&
        light_temperature_field_.number->isEnabled() != temperature_mode) {
      light_temperature_field_.number->setEnabled(temperature_mode);
      light_temperature_field_.slider->setEnabled(temperature_mode);
    }
    if (light_color_button_ != nullptr) {
      if (!presented_light_.has_value() ||
          presented_light_->color.x != light.color.x ||
          presented_light_->color.y != light.color.y ||
          presented_light_->color.z != light.color.z) {
        const QVec3 display = light_linear_to_srgb(light.color);
        const QColor swatch = QColor::fromRgbF(display.x, display.y, display.z);
        if (light_color_button_->swatch_color() != swatch) {
          light_color_button_->set_swatch_color(swatch);
        }
      }
      if (light_color_button_->isEnabled() == temperature_mode) {
        light_color_button_->setEnabled(!temperature_mode);
      }
    }
    if (light_preset_combo_ != nullptr &&
        light_preset_combo_->currentData().toInt() != static_cast<int>(light.source_preset)) {
      const QSignalBlocker blocker(light_preset_combo_.data());
      light_preset_combo_->setCurrentIndex(light_preset_combo_->findData(
          static_cast<int>(light.source_preset)));
    }
    if (light_color_mode_combo_ != nullptr &&
        light_color_mode_combo_->currentData().toInt() != static_cast<int>(light.color_mode)) {
      const QSignalBlocker blocker(light_color_mode_combo_.data());
      light_color_mode_combo_->setCurrentIndex(light_color_mode_combo_->findData(
          static_cast<int>(light.color_mode)));
    }
  }

  QtColorSwatchButton *
  add_color_button(Section &section, std::string stable_id,
                    const QString &label, QVec3 value,
                    EntityId entity, bool light_color) {
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
        [this, stable_id, button, entity, light_color]() {
          if (editor_ == nullptr) {
            return;
          }
          const std::uint64_t dialog_generation = runtime_binding_generation_;
          QPointer<InspectorPanel> panel_guard(&panel_);
          const auto edit = light_color
                                ? editor_->scene().begin_light_color_edit(entity)
                                : editor_->scene().begin_environment_color_edit(
                                      entity);
          if (panel_guard == nullptr ||
              runtime_binding_generation_ != dialog_generation) {
            return;
          }
          if (!edit.ok()) {
            (void)editor_->ui().notify({.level = QuaderNotificationLevel::Error,
                                        .message = edit.error_message()});
            return;
          }
          const QColor initial = button->swatch_color();
          QColorDialog dialog(initial, &panel_);
          dialog.setObjectName(stable_name(stable_id + ".dialog"));
          dialog.setOption(QColorDialog::DontUseNativeDialog, true);
          active_color_dialog_ = &dialog;
          QPointer<QtColorSwatchButton> guarded_button(button);
          QObject::connect(
              &dialog, &QColorDialog::currentColorChanged, &dialog,
              [this, &dialog, guarded_button, panel_guard, dialog_generation,
               session = edit.value(), light_color](const QColor &color) {
                if (panel_guard == nullptr || !color.isValid() ||
                    runtime_binding_generation_ != dialog_generation ||
                    editor_ == nullptr) {
                  return;
                }
                QVec3 next{static_cast<float>(color.redF()),
                           static_cast<float>(color.greenF()),
                           static_cast<float>(color.blueF())};
                if (light_color) {
                  next = light_srgb_to_linear(next);
                }
                const auto result = light_color
                                        ? editor_->scene().update_light_color_edit(
                                              session, next)
                                        : editor_->scene().update_environment_color_edit(
                                              session, next);
                if (panel_guard == nullptr ||
                    runtime_binding_generation_ != dialog_generation) {
                  return;
                }
                if (!result.ok()) {
                  dialog.reject();
                  return;
                }
                if (guarded_button != nullptr) {
                  guarded_button->set_swatch_color(color);
                }
                if (light_color && document_ != nullptr) {
                  if (const LightComponent *light = document_->light(primary_)) {
                    synchronize_light_controls(*light);
                  }
                }
              });

          const int result = dialog.exec();
          if (panel_guard == nullptr) {
            return;
          }
          if (active_color_dialog_ == &dialog) {
            active_color_dialog_ = nullptr;
          }
          if (runtime_binding_generation_ != dialog_generation) {
            return;
          }
          const bool cancel = result != QDialog::Accepted;
          const auto finished = light_color
                                    ? editor_->scene().finish_light_color_edit(
                                          edit.value(), cancel)
                                    : editor_->scene().finish_environment_color_edit(
                                          edit.value(), cancel);
          if (panel_guard == nullptr ||
              runtime_binding_generation_ != dialog_generation) {
            return;
          }
          if (cancel && guarded_button != nullptr) {
            guarded_button->set_swatch_color(initial);
          }
          if (!finished.ok()) {
            (void)editor_->ui().notify({.level = QuaderNotificationLevel::Error,
                                        .message = finished.error_message()});
            if (panel_guard == nullptr ||
                runtime_binding_generation_ != dialog_generation) {
              return;
            }
          }
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

  void apply_mesh_components(
      const std::vector<EntityId> &entities,
      const std::function<void(MeshComponent &)> &mutation) {
    if (editor_ == nullptr || document_ == nullptr) {
      return;
    }
    if (entities.size() == 1u) {
      apply_mesh_component(entities.front(), mutation);
      return;
    }
    auto batch = editor_->scene().batch("Set Mesh Physics");
    for (const EntityId entity : entities) {
      const MeshComponent *current = document_->mesh(entity);
      if (current == nullptr) {
        return;
      }
      MeshComponent next = *current;
      mutation(next);
      batch.set_mesh_component(entity, to_api_mesh_component(next));
    }
    (void)batch.commit();
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
                     settings->environment.clear_color, entity, false);
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
    const SceneToneMapping tone_mapping = settings->post_process.tone_mapping;
    const SceneToneMapping displayed_tone_mapping =
        tone_mapping == SceneToneMapping::AcesLegacy ||
                tone_mapping == SceneToneMapping::DisplayRange
            ? SceneToneMapping::Aces
            : tone_mapping;
    add_enum_combo<SceneToneMapping>(
        post, "post_process.tone_mapping",
        localized("ui.inspector.tone_mapping", "Tone Mapping"),
        displayed_tone_mapping,
        {
            {SceneToneMapping::Linear,
             localized("ui.inspector.tone_mapping.linear", "Linear")},
            {SceneToneMapping::Aces,
             localized("ui.inspector.tone_mapping.aces", "ACES")},
            {SceneToneMapping::Agx,
             localized("ui.inspector.tone_mapping.agx", "AgX")},
            {SceneToneMapping::Filmic,
             localized("ui.inspector.tone_mapping.filmic", "Filmic")},
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
  }

  void build_physics_section() {
    std::vector<EntityId> meshes;
    meshes.reserve(selection_.size());
    BoolAggregate generate;
    BoolAggregate display;
    std::optional<MeshCollisionType> collision_type;
    std::optional<MeshPhysicsBodyType> body_type;
    bool mixed_collision_type = false;
    bool mixed_body_type = false;
    bool unsupported = false;
    for (const EntityId entity : selection_) {
      const MeshComponent *mesh = document_->mesh(entity);
      if (mesh == nullptr || document_->is_scene_root(entity)) {
        continue;
      }
      meshes.push_back(entity);
      generate.all_true = generate.all_true && mesh->generate_collisions;
      generate.any_true = generate.any_true || mesh->generate_collisions;
      display.all_true = display.all_true && mesh->display_collision;
      display.any_true = display.any_true || mesh->display_collision;
      if (collision_type.has_value()) {
        mixed_collision_type = mixed_collision_type ||
                               *collision_type != mesh->collision_type;
        mixed_body_type = mixed_body_type || *body_type != mesh->body_type;
      } else {
        collision_type = mesh->collision_type;
        body_type = mesh->body_type;
      }
      unsupported = unsupported || !mesh_collision_supported(*mesh);
    }
    if (meshes.empty()) {
      return;
    }

    Section physics =
        add_section("physics", localized("ui.inspector.physics", "Physics"));
    add_mixed_checkbox(
        physics, "physics.generate_collisions",
        localized("ui.inspector.generate_collisions", "Generate Collisions"),
        generate.all_true, generate.mixed(), [this, meshes](bool enabled) {
          apply_mesh_components(meshes, [enabled](MeshComponent &next) {
            next.generate_collisions = enabled;
          });
        });
    add_enum_combo<MeshCollisionType>(
        physics, "physics.collision_type",
        localized("ui.inspector.collision_type", "Collision Type"),
        *collision_type,
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
        [this, meshes](MeshCollisionType value) {
          apply_mesh_components(meshes, [value](MeshComponent &next) {
            next.collision_type = value;
          });
        }, MeshCollisionType::Mesh, mixed_collision_type);
    add_mixed_checkbox(
        physics, "physics.display_collision",
        localized("ui.inspector.display_collision", "Display Collision"),
        display.all_true, display.mixed(), [this, meshes](bool enabled) {
          apply_mesh_components(meshes, [enabled](MeshComponent &next) {
            next.display_collision = enabled;
          });
        });
    add_enum_combo<MeshPhysicsBodyType>(
        physics, "physics.body_type",
        localized("ui.inspector.body_type", "Body type"), *body_type,
        {
            {MeshPhysicsBodyType::StaticBody,
             localized("ui.inspector.body_type.static_body", "Static Body")},
            {MeshPhysicsBodyType::RigidBody,
             localized("ui.inspector.body_type.rigid_body", "Rigid Body")},
        },
        [this, meshes](MeshPhysicsBodyType value) {
          apply_mesh_components(
              meshes, [value](MeshComponent &next) { next.body_type = value; });
        }, MeshPhysicsBodyType::StaticBody, mixed_body_type);
    if (unsupported) {
      auto *status = new QLabel(
          localized("ui.inspector.collision_unsupported",
                    "Select Mesh and Static Body to generate collisions."),
          physics.editor_parent);
      status->setObjectName(stable_name("physics.collision_status"));
      status->setWordWrap(true);
      add_row(physics, "physics.collision_status",
              localized("ui.inspector.collision_status", "Collision Status"),
              status);
    }
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
        section, "light.type", localized("ui.inspector.light_type", "Shape"),
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
    std::vector<std::pair<LightSourcePreset, QString>> preset_options;
    for (const LightSourcePresetDefinition &preset : light_source_presets()) {
      preset_options.emplace_back(
          preset.preset, localized(preset.label_key, preset.fallback_label));
    }
    light_preset_combo_ = add_enum_combo<LightSourcePreset>(
        section, "light.source_preset", localized("ui.inspector.source_type", "Type"),
        light->source_preset, preset_options,
        [this, entity](LightSourcePreset preset) {
          apply_light_component(entity, [preset](LightComponent &next) {
            (void)apply_light_source_preset(next, preset);
          });
        });
    light_preset_combo_->setToolTip(localized(
        "ui.inspector.source_type_tooltip",
        "Applies source color and intensity."));
    light_color_mode_combo_ = add_enum_combo<LightColorMode>(
        section, "light.color_mode", localized("ui.inspector.color_mode", "Color Mode"),
        light->color_mode,
        {{LightColorMode::ColorTemperature,
          localized("ui.inspector.color_temperature", "Color Temperature")},
         {LightColorMode::Color, localized("ui.inspector.color", "Color")}},
        [this, entity](LightColorMode mode) {
          apply_light_component(entity, [mode](LightComponent &next) {
            next.color_mode = mode;
            next.source_preset = LightSourcePreset::Custom;
            if (mode == LightColorMode::ColorTemperature) {
              next.color = light_color_temperature_rgb(next.color_temperature_kelvin);
            }
          });
        });
    light_temperature_field_ = add_number_slider(
        section, "light.color_temperature_kelvin",
        localized("ui.inspector.temperature", "Temperature (K)"),
        light->color_temperature_kelvin, kLightMinimumTemperatureKelvin,
        kLightMaximumTemperatureKelvin, kLightMaximumTemperatureKelvin, 1.0,
        [this, entity](double value, std::string merge_key) {
          apply_light_component(
              entity, [value](LightComponent &next) {
                next.color_temperature_kelvin = static_cast<float>(value);
                next.color = light_color_temperature_rgb(next.color_temperature_kelvin);
                next.source_preset = LightSourcePreset::Custom;
              }, std::move(merge_key));
        });
    light_color_button_ = add_color_button(
        section, "light.color", localized("ui.inspector.color", "Color"),
        light_linear_to_srgb(light->color), entity, true);

    light_intensity_field_ = add_number_slider(
        section, "light.intensity", localized("ui.inspector.intensity", "Intensity"),
        light->intensity, 0.0, 0.0, std::numeric_limits<float>::max(), 0.01,
        [this, entity](double value, std::string merge_key) {
          apply_light_component(
              entity, [value](LightComponent &next) {
                next.intensity = static_cast<float>(value);
                next.source_preset = LightSourcePreset::Custom;
              }, std::move(merge_key));
        }, false);
    if (light->type == LightType::Point || light->type == LightType::Spot) {
      light_range_field_ = add_continuous_number(
          section, "light.range", localized("ui.inspector.range", "Range"),
          light->range, kInspectorLightRangeMin,
          std::max(kInspectorLightRangeSliderMax, light->range),
          0.1, 4, [this, entity](double value, std::string merge_key) {
            apply_light_component(
                entity,
                [value](LightComponent &next) {
                  next.range = static_cast<float>(value);
                },
                std::move(merge_key));
          });
      light_falloff_field_ = add_number_slider(
          section, "light.distance_attenuation",
          localized("ui.inspector.distance_attenuation", "Falloff"),
          light->distance_attenuation, kLightMinimumFalloff,
          kLightMaximumFalloff, kLightMaximumFalloff, 0.01,
          [this, entity](double value, std::string merge_key) {
            apply_light_component(
                entity,
                [value](LightComponent &next) {
                  next.distance_attenuation = static_cast<float>(value);
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
    synchronize_light_controls(*light);
    presented_light_ = *light;
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
  std::vector<MaterialRow> material_rows_;
  QPointer<QDoubleSpinBox> light_range_field_;
  NumberSlider light_intensity_field_;
  NumberSlider light_falloff_field_;
  NumberSlider light_temperature_field_;
  QPointer<QtColorSwatchButton> light_color_button_;
  QPointer<QComboBox> light_preset_combo_;
  QPointer<QComboBox> light_color_mode_combo_;
  std::optional<LightComponent> presented_light_;
  SceneTransformDragService active_transform_preview_;
  std::optional<TransformEditState> active_transform_edit_;
  UIContinuousEditTracker active_continuous_edit_;
  QColorDialog *active_color_dialog_ = nullptr;
  std::vector<EntityId> selection_;
  EntityId primary_ = kInvalidEntityId;
  std::unordered_map<std::string, bool> expanded_sections_;
  std::uint64_t built_scene_revision_ =
      std::numeric_limits<std::uint64_t>::max();
  std::uint64_t built_light_property_revision_ =
      std::numeric_limits<std::uint64_t>::max();
  std::uint64_t built_modeling_content_revision_ =
      std::numeric_limits<std::uint64_t>::max();
  std::uint64_t built_modeling_selection_revision_ =
      std::numeric_limits<std::uint64_t>::max();
  std::uint64_t built_material_presentation_revision_ =
      std::numeric_limits<std::uint64_t>::max();
  std::uint64_t runtime_binding_generation_ = 0;
  QtUiScaleMetrics ui_scale_metrics_;
  int scaled_default_label_column_width_ = 140;
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

void InspectorPanel::abandon_runtime() noexcept { impl_->abandon_runtime(); }

void InspectorPanel::apply_ui_scale(const QtUiScaleMetrics &metrics) {
  impl_->apply_ui_scale(metrics);
}

bool InspectorPanel::event(QEvent *event) {
  const bool handled = QWidget::event(event);
  if (impl_ != nullptr && event != nullptr) {
    if (event->type() == QEvent::DevicePixelRatioChange ||
        event->type() == QEvent::ScreenChangeInternal) {
      impl_->refresh_material_preview_rasters();
    } else if (event->type() == QEvent::PaletteChange ||
               event->type() == QEvent::ApplicationPaletteChange) {
      impl_->refresh_material_action_icons();
    }
  }
  return handled;
}

void InspectorPanel::refresh(UIContext &context) { impl_->refresh(context); }

} // namespace quader
